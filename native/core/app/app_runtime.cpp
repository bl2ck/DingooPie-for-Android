#include "app/app_runtime.h"

#include "guest/guest_package.h"
#include "game/game_paths.h"
#include "config/cheat_runtime.h"
#include "runtime/crash_log.h"
#include "runtime/debug_log.h"
#include "game/game_history.h"
#include "app/sdk_hle.h"
#include "frontend/framebuffer.h"
#include "guest/guest_text_format.h"
#include "config/compat_profile.h"
#include "runtime/pause_gate.h"
#include "platform_services.h"
#include "app/instruction_compat.h"
#include "app/emulated_memory.h"
#include "app/ppsspp_irjit_backend.h"
#include "runtime/runtime_debug.h"
#include "frontend/sdl_frontend.h"
#include "frontend/sdl_audio.h"
#include "app/task_scheduler.h"
#include "guest/guest_filesystem.h"
#include "Common/Crypto/sha256.h"

#include <algorithm>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <atomic>
#include <chrono>
#include <string>
#include <string.h>
#include <thread>
#include <vector>
#include "runtime/native_runtime.h"

static std::string g_appLoadPath;
static std::string g_appMainPath;
static std::string g_currentAppSha256;
static std::vector<std::string> g_enabledCheatFeatureKeys;
static ExecutionBackend g_activeAppBackend = EXECUTION_BACKEND_COMPATIBILITY;
static FILE* makeSeekableAndroidAppFile(FILE* source, void** bufferOut, uint32_t* sizeOut)
{
    if (!source || !bufferOut || !sizeOut)
    {
        return NULL;
    }

    const size_t chunkSize = 64 * 1024;
    size_t capacity = chunkSize;
    size_t dataSize = 0;
    uint8_t* buffer = (uint8_t*)malloc(capacity);
    if (!buffer)
    {
        return NULL;
    }

    clearerr(source);
    while (true)
    {
        if (dataSize == capacity)
        {
            size_t nextCapacity = capacity <= UINT32_MAX / 2 ? capacity * 2 : UINT32_MAX;
            if (nextCapacity <= capacity)
            {
                free(buffer);
                return NULL;
            }
            uint8_t* resized = (uint8_t*)realloc(buffer, nextCapacity);
            if (!resized)
            {
                free(buffer);
                return NULL;
            }
            buffer = resized;
            capacity = nextCapacity;
        }

        size_t available = capacity - dataSize;
        size_t count = fread(buffer + dataSize, 1, available, source);
        dataSize += count;
        if (count < available)
        {
            if (ferror(source))
            {
                free(buffer);
                return NULL;
            }
            break;
        }
    }

    if (dataSize == 0 || dataSize > UINT32_MAX)
    {
        free(buffer);
        return NULL;
    }

    FILE* seekableFile = fmemopen(buffer, dataSize, "rb");
    if (!seekableFile)
    {
        free(buffer);
        return NULL;
    }

    *bufferOut = buffer;
    *sizeOut = (uint32_t)dataSize;
    return seekableFile;
}

static EmulatorOptions g_options;
static bool g_clearRecentOnStartupFailure = false;
static pthread_mutex_t g_runtimeThreadMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_runtimeThread;
static bool g_runtimeThreadStarted = false;
static NativeRuntime* g_mainRuntime = NULL;
static std::atomic<bool> g_runtimeStopRequested(false);
static std::atomic<bool> g_lastRunExitedNormally(false);
static std::string g_lastRunAppPath;

uint32_t s_AppDataAddr = 0;
uint32_t s_AppDataBuffSize = 0;
void* s_AppDataBuff = 0;
GuestPackage* s_guestPackage = NULL;

static uint32_t g_appMainEntry = 0;
static uint32_t g_appMainInitCheckAddress = 0;

static void clearMainRuntimeIfCurrent(NativeRuntime* runtime)
{
    bool shouldUnbindCheats = false;
    pthread_mutex_lock(&g_runtimeThreadMutex);
    if (g_mainRuntime == runtime)
    {
        g_mainRuntime = NULL;
        g_currentAppSha256.clear();
        shouldUnbindCheats = true;
    }
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    if (shouldUnbindCheats)
    {
        // Cheat runtime has its own mutex and can flush JIT state; keep it
        // outside g_runtimeThreadMutex to avoid frontend/runtime lock inversion.
        cheatRuntimeUnbind(runtime);
    }
}

static void destroyMainRuntime(NativeRuntime* runtime)
{
    if (!runtime)
    {
        return;
    }
    clearMainRuntimeIfCurrent(runtime);
    nativeRuntimeDestroy(runtime);
}

static std::string sha256Hex(const uint8_t* data, uint32_t size)
{
    static const char kHex[] = "0123456789ABCDEF";
    uint8_t digest[32];
    sha256_context context;
    sha256_starts(&context);
    sha256_update(&context, data, size);
    sha256_finish(&context, digest);

    std::string out;
    out.resize(64);
    for (size_t i = 0; i < sizeof(digest); ++i)
    {
        out[i * 2] = kHex[digest[i] >> 4];
        out[i * 2 + 1] = kHex[digest[i] & 0x0f];
    }
    return out;
}

static GuestPackage* loadApp(const char* appPath)
{
    std::string path = gamePathNormalize(appPath);
    long fileSize = 0;
    void* androidMemoryFileBuffer = NULL;

    FILE* file = platformOpenGameFile(path);

    if (!file)
    {
        printf("DingooPie: failed to open app: %s\n", path.c_str());
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        uint32_t memoryFileSize = 0;
        FILE* seekableFile = makeSeekableAndroidAppFile(file, &androidMemoryFileBuffer,
            &memoryFileSize);
        fclose(file);
        file = seekableFile;
        fileSize = (long)memoryFileSize;
        if (!file)
        {
            printf("DingooPie: failed to read non-seekable app: %s errno=%d\n",
                path.c_str(), errno);
            return NULL;
        }
    }

    if (!androidMemoryFileBuffer)
    {
        fileSize = ftell(file);
    }
    if (fileSize <= 0 || (uint64_t)fileSize > UINT32_MAX || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        free(androidMemoryFileBuffer);
        printf("DingooPie: invalid app size: %s\n", path.c_str());
        return NULL;
    }

    GuestPackage* loadedApp = guestPackageCreate(file, (uint32_t)fileSize);
    fclose(file);
    free(androidMemoryFileBuffer);

    if (!loadedApp)
    {
        printf("DingooPie: failed to parse app: %s\n", path.c_str());
        return NULL;
    }

    return loadedApp;
}
static void hookDingooPieDebug(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData)
{
    (void)address;
    (void)size;
    (void)userData;
    dingoo_debug(runtime);
}

static bool hookMemInvalid(NativeRuntime* runtime, RuntimeMemoryAccess type, uint64_t address, int size, int64_t value, void* userData)
{
    (void)userData;
    FILE* debugLog = debugLogFile();

    fprintf(debugLog, ">>> mem_invalid type:%s addr:0x%" PRIx64 " size:0x%x value:0x%" PRIx64 "\n",
        memTypeStr(type), address, size, value);
    dumpREG2File(runtime, debugLog);
    dumpAsm(runtime);

    printf(">>> mem_invalid type:%s addr:0x%" PRIx64 " size:0x%x value:0x%" PRIx64 "\n",
        memTypeStr(type), address, size, value);
    dumpREG(runtime);
    return false;
}

static void hookForceAppInitSuccess(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData)
{
    (void)address;
    (void)size;
    (void)userData;

    uint32_t forceSuccess = 1;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &forceSuccess);
}

static std::vector<uint16_t> utf8ToGuestUtf16(const std::string& text)
{
    std::vector<uint16_t> result;
    result.reserve(text.size() + 1);
    for (size_t offset = 0; offset < text.size();)
    {
        uint32_t codePoint = 0xfffdu;
        unsigned char lead = (unsigned char)text[offset++];
        if (lead < 0x80u)
        {
            codePoint = lead;
        }
        else
        {
            uint32_t minimum = 0;
            int continuationCount = 0;
            if ((lead & 0xe0u) == 0xc0u)
            {
                codePoint = lead & 0x1fu;
                minimum = 0x80u;
                continuationCount = 1;
            }
            else if ((lead & 0xf0u) == 0xe0u)
            {
                codePoint = lead & 0x0fu;
                minimum = 0x800u;
                continuationCount = 2;
            }
            else if ((lead & 0xf8u) == 0xf0u)
            {
                codePoint = lead & 0x07u;
                minimum = 0x10000u;
                continuationCount = 3;
            }

            size_t sequenceEnd = offset + (size_t)continuationCount;
            if (continuationCount == 0 || sequenceEnd > text.size())
            {
                codePoint = 0xfffdu;
            }
            else
            {
                bool valid = true;
                for (; offset < sequenceEnd; ++offset)
                {
                    unsigned char continuation = (unsigned char)text[offset];
                    if ((continuation & 0xc0u) != 0x80u)
                    {
                        valid = false;
                        break;
                    }
                    codePoint = (codePoint << 6) | (continuation & 0x3fu);
                }
                if (!valid || codePoint < minimum || codePoint > 0x10ffffu ||
                    (codePoint >= 0xd800u && codePoint <= 0xdfffu))
                {
                    codePoint = 0xfffdu;
                }
            }
        }

        if (codePoint <= 0xffffu)
        {
            result.push_back((uint16_t)codePoint);
        }
        else
        {
            codePoint -= 0x10000u;
            result.push_back((uint16_t)(0xd800u | (codePoint >> 10)));
            result.push_back((uint16_t)(0xdc00u | (codePoint & 0x3ffu)));
        }
    }
    result.push_back(0);
    return result;
}

static void hookAppMain(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData)
{
    (void)size;
    RuntimeError err;

    std::vector<uint16_t> appPathW = utf8ToGuestUtf16(g_appMainPath);
    uint32_t pathBytes = (uint32_t)(appPathW.size() * sizeof(uint16_t));
    uint32_t pathPtr = vm_malloc(pathBytes);
    if (!pathPtr)
    {
        printf("DingooPie: vm_malloc failed for AppMain path, size=%u\n", pathBytes);
        exit(1);
    }

    err = nativeRuntimeWriteMemory(runtime, pathPtr, appPathW.data(), pathBytes);
    if (err)
    {
        printf("DingooPie: nativeRuntimeWriteMemory(AppMain path) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        exit(1);
    }

    err = nativeRuntimeWriteRegister(runtime, RUNTIME_REG_A0, &pathPtr);
    if (err)
    {
        printf("DingooPie: nativeRuntimeWriteRegister(A0) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        exit(1);
    }

    uint32_t appMainEntry = (uint32_t)address;
    err = nativeRuntimeWriteRegister(runtime, RUNTIME_REG_T9, &appMainEntry);
    if (err)
    {
        printf("DingooPie: nativeRuntimeWriteRegister(T9) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        exit(1);
    }

    RuntimeHook* hookHandle = (RuntimeHook*)userData;
    err = nativeRuntimeRemoveHook(runtime, *hookHandle);
    if (err)
    {
        printf("DingooPie: nativeRuntimeRemoveHook(AppMain) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        exit(1);
    }

    free(userData);
}

static bool mapAppMemory(NativeRuntime* runtime, GuestPackage* loadedApp)
{
    s_AppDataAddr = loadedApp->origin;
    s_AppDataBuffSize = loadedApp->bin_size;
    s_AppDataBuff = loadedApp->bin_data;
    s_guestPackage = loadedApp;

    RuntimeError err = nativeRuntimeMapMemory(runtime, s_AppDataAddr, s_AppDataBuffSize, RUNTIME_PROT_ALL, s_AppDataBuff);
    if (err)
    {
        printf("DingooPie: failed to map app memory: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return false;
    }

    uint32_t aliasAddr = s_AppDataAddr & 0x1fffffff;
    err = nativeRuntimeMapMemory(runtime, aliasAddr, s_AppDataBuffSize, RUNTIME_PROT_ALL, s_AppDataBuff);
    if (err)
    {
        printf("DingooPie: failed to map app alias: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return false;
    }

    return true;
}

static uint32_t findAppMainEntry(GuestPackage* loadedApp)
{
    for (uint32_t i = 0; i < loadedApp->export_count; i++)
    {
        if (strcmp(loadedApp->export_data[i]->name, "AppMain") == 0)
        {
            return loadedApp->export_data[i]->offset;
        }
    }

    return 0;
}

static bool installCoreHooks(NativeRuntime* runtime, GuestPackage* loadedApp, uint32_t appMainEntry, RuntimeHook** appMainHook)
{
    RuntimeError err;
    RuntimeHook trace;

    err = nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_MEM_INVALID, (void*)hookMemInvalid, NULL, 1, 0);
    if (err != RUNTIME_OK)
    {
        printf("DingooPie: failed to add mem invalid hook: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return false;
    }

    nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_CODE, (void*)hookDingooPieDebug, NULL, 0x80B0F060, 0x80B0F060);

    err = runtimeCompatInstallHooks(runtime, loadedApp, g_options);
    if (err != RUNTIME_OK)
    {
        printf("DingooPie: failed to add runtime compat hook: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return false;
    }

    g_appMainEntry = appMainEntry;
    g_appMainInitCheckAddress = appMainEntry + 0x34;

    err = nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_CODE, (void*)hookForceAppInitSuccess, NULL,
        g_appMainInitCheckAddress, g_appMainInitCheckAddress, 0);
    if (err != RUNTIME_OK)
    {
        printf("DingooPie: failed to add AppMain init hook: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return false;
    }

    *appMainHook = (RuntimeHook*)malloc(sizeof(RuntimeHook));
    if (!*appMainHook)
    {
        printf("DingooPie: failed to allocate AppMain hook handle\n");
        return false;
    }

    err = nativeRuntimeAddHook(runtime, *appMainHook, RUNTIME_HOOK_CODE, (void*)hookAppMain, (void*)*appMainHook,
        appMainEntry, appMainEntry, 0);
    if (err != RUNTIME_OK)
    {
        printf("DingooPie: failed to add AppMain hook: %u (%s)\n", err, nativeRuntimeErrorString(err));
        free(*appMainHook);
        *appMainHook = NULL;
        return false;
    }

    return true;
}

static void logEmulationFailure(NativeRuntime* runtime, RuntimeError err, const CrashLogContext& context)
{
    printf("DingooPie: native runtime failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
    std::string crashLogFileName;
    if (crashLogWriteGuestFailure(runtime, err, context, &crashLogFileName))
    {
        printf("crash-log:wrote file=%s reason=guest-runtime-failure\n", crashLogFileName.c_str());
    }
    else
    {
        printf("crash-log:failed reason=guest-runtime-failure\n");
    }
}

static CompatGuestExitDecision noGuestExitDecision(void)
{
    CompatGuestExitDecision decision;
    decision.matched = false;
    decision.shouldExit = false;
    decision.label = NULL;
    return decision;
}

static CompatGuestExitDecision runtimeExceptionGuestExitDecision(NativeRuntime* runtime, const std::string& appSha256)
{
    CompatRuntimeExceptionExitContext context;
    context.pc = 0;
    context.returnAddress = 0;
    context.v0 = 0;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_PC, &context.pc);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &context.returnAddress);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_V0, &context.v0);

    return compatRuntimeExceptionGuestExitDecision(appSha256.c_str(), &context);
}

static NativeRuntime* initDingooPie(void)
{
    taskSchedulerResetShutdown();

    NativeRuntime* runtime;
    RuntimeError err = nativeRuntimeCreate(&runtime);
    if (err)
    {
        printf("DingooPie: nativeRuntimeCreate failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return NULL;
    }
    pthread_mutex_lock(&g_runtimeThreadMutex);
    g_mainRuntime = runtime;
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    if (g_runtimeStopRequested.load(std::memory_order_acquire))
    {
        nativeRuntimeRequestStop(runtime);
    }

    GuestPackage* loadedApp = loadApp(g_appLoadPath.c_str());
    if (!loadedApp)
    {
        destroyMainRuntime(runtime);
        return NULL;
    }
    std::string appSha256 = sha256Hex(loadedApp->file_data, loadedApp->file_size);
    pthread_mutex_lock(&g_runtimeThreadMutex);
    g_currentAppSha256 = appSha256;
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    bridge_set_game_identity(appSha256.c_str());
    fsys_set_game_identity(appSha256.c_str());
    fsys_set_game_name(g_appMainPath.c_str());
    std::string saveDirectory = platformAndroidGetSaveDirectory(g_appLoadPath);
    fsys_set_save_directory(saveDirectory.c_str());
    printf("DingooPie: save directory: %s\n", saveDirectory.c_str());
    cheatRuntimeLoadForGame(
        appSha256.c_str(),
        g_appLoadPath.c_str(),
        g_enabledCheatFeatureKeys);
    printf("DingooPie: app sha256: %s\n", appSha256.c_str());
    printf("DingooPie: compat profile: %s\n", compatProfileName(appSha256.c_str()));
    ppssppShimSetFastMemoryOverride(-1);

    ExecutionBackend effectiveBackend = g_options.backend;
    if (compatForcedBackend(appSha256.c_str(), &effectiveBackend))
    {
        printf("DingooPie: compat backend override: requested=%s effective=%s\n",
            executionBackendName(g_options.backend), executionBackendName(effectiveBackend));
    }
    if (effectiveBackend == EXECUTION_BACKEND_PPSSPP_IRJIT && !ppssppIrJitBackendAvailable())
    {
        printf("DingooPie: PPSSPP IR JIT unavailable, falling back to compatibility mode\n");
        effectiveBackend = EXECUTION_BACKEND_COMPATIBILITY;
    }

    pthread_mutex_lock(&g_runtimeThreadMutex);
    g_activeAppBackend = effectiveBackend;
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    appRuntimeApplySettings();

    err = nativeRuntimeSetBackend(runtime, effectiveBackend);
    if (err)
    {
        printf("DingooPie: nativeRuntimeSetBackend failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        destroyMainRuntime(runtime);
        return NULL;
    }
    printf("DingooPie: execution backend effective: %s\n", executionBackendName(effectiveBackend));

    if (!mapAppMemory(runtime, loadedApp))
    {
        destroyMainRuntime(runtime);
        return NULL;
    }
    cheatRuntimeBind(runtime);

    printf("DingooPie: init bridge begin\n");
    err = bridge_init(runtime, loadedApp);
    if (err)
    {
        printf("DingooPie: bridge_init failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        destroyMainRuntime(runtime);
        return NULL;
    }
    printf("DingooPie: init bridge done\n");

    printf("DingooPie: init vm memory begin\n");
    if (InitVmMem(runtime, loadedApp))
    {
        printf("DingooPie: InitVmMem failed\n");
        destroyMainRuntime(runtime);
        return NULL;
    }
    printf("DingooPie: init vm memory done\n");

    printf("DingooPie: init framebuffer begin\n");
    if (framebufferInitialize(runtime))
    {
        printf("DingooPie: framebuffer initialization failed\n");
        destroyMainRuntime(runtime);
        return NULL;
    }
    printf("DingooPie: init framebuffer done\n");

    uint32_t value = 0;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_ZERO, &value);

    uint32_t appMainEntry = findAppMainEntry(loadedApp);
    if (!appMainEntry)
    {
        printf("DingooPie: AppMain export not found\n");
        destroyMainRuntime(runtime);
        return NULL;
    }
    printf("DingooPie: AppMain entry=0x%08x boot entry=0x%08x origin=0x%08x size=0x%08x\n",
        appMainEntry, loadedApp->bin_entry, loadedApp->origin, loadedApp->bin_size);

    RuntimeHook* appMainHook = NULL;
    printf("DingooPie: install core hooks begin\n");
    if (!installCoreHooks(runtime, loadedApp, appMainEntry, &appMainHook))
    {
        destroyMainRuntime(runtime);
        return NULL;
    }
    printf("DingooPie: install core hooks done\n");
    cheatRuntimeApplyStartup(runtime);

    err = nativeRuntimeWriteRegister(runtime, RUNTIME_REG_RA, &appMainEntry);
    if (err)
    {
        printf("DingooPie: nativeRuntimeWriteRegister(RA) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        destroyMainRuntime(runtime);
        return NULL;
    }

    uint32_t mallocLcdBuffer = 0;
    err = nativeRuntimeWriteRegister(runtime, RUNTIME_REG_A1, &mallocLcdBuffer);
    if (err)
    {
        printf("DingooPie: nativeRuntimeWriteRegister(A1) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        destroyMainRuntime(runtime);
        return NULL;
    }

    err = nativeRuntimeWriteRegister(runtime, RUNTIME_REG_T9, &loadedApp->bin_entry);
    if (err)
    {
        printf("DingooPie: nativeRuntimeWriteRegister(T9 entry) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        destroyMainRuntime(runtime);
        return NULL;
    }

    CrashLogContext crashContext;
    memset(&crashContext, 0, sizeof(crashContext));
    crashContext.appPath = g_appLoadPath.c_str();
    crashContext.appMainPath = g_appMainPath.c_str();
    crashContext.appSha256 = appSha256.c_str();
    crashContext.compatProfile = compatProfileName(appSha256.c_str());
    crashContext.backend = effectiveBackend;
    crashContext.appEntry = appMainEntry;
    crashContext.bootEntry = loadedApp->bin_entry;
    crashContext.origin = loadedApp->origin;
    crashContext.appSize = loadedApp->bin_size;

    if (getenv("DINGOO_PIE_FORCE_GUEST_CRASH"))
    {
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &loadedApp->bin_entry);
        printf("DingooPie: forcing guest crash for diagnostics\n");
        g_lastRunExitedNormally.store(false, std::memory_order_release);
        logEmulationFailure(runtime, RUNTIME_ERROR_EXCEPTION, crashContext);
        frontendRequestQuit();
        return runtime;
    }

    printf("DingooPie: native runtime start pc=0x%08x until=0xffffffff\n", loadedApp->bin_entry);
    err = nativeRuntimeStart(runtime, loadedApp->bin_entry, 0xFFFFFFFF, 0, 0);
    printf("DingooPie: native runtime returned err=%u (%s)\n", err, nativeRuntimeErrorString(err));
    if (err)
    {
        CompatGuestExitDecision exitDecision = (err == RUNTIME_ERROR_EXCEPTION)
            ? runtimeExceptionGuestExitDecision(runtime, appSha256)
            : noGuestExitDecision();
        if (exitDecision.shouldExit)
        {
            printf("DingooPie: treating %s as normal guest exit\n",
                exitDecision.label ? exitDecision.label : "compat exception");
            g_lastRunAppPath = g_appLoadPath;
            g_lastRunExitedNormally.store(true, std::memory_order_release);
            frontendRequestGameExit();
            return runtime;
        }
        g_lastRunExitedNormally.store(false, std::memory_order_release);
        logEmulationFailure(runtime, err, crashContext);
        frontendRequestQuit();
        return runtime;
    }

    g_lastRunAppPath = g_appLoadPath;
    g_lastRunExitedNormally.store(true, std::memory_order_release);
    if (!g_runtimeStopRequested.load(std::memory_order_acquire))
    {
        printf("DingooPie: guest completed; returning to library\n");
        frontendRequestGameExit();
    }
    return runtime;
}

static void* dingoopieRun(void* data)
{
    (void)data;
    NativeRuntime* runtime = initDingooPie();
    if (!runtime)
    {
        gameHistoryClearRecentIfCurrent(
            g_appLoadPath, g_clearRecentOnStartupFailure, "APP startup failure");
    }
    printf("DingooPie: runtime thread exited runtime=%p\n", (void*)runtime);
    if (runtime)
    {
        destroyMainRuntime(runtime);
        printf("DingooPie: native runtime destroyed runtime=%p\n", (void*)runtime);
    }
    return 0;
}

bool appRuntimeStart(
    const char* appPath,
    const EmulatorOptions& options,
    bool clearRecentOnStartupFailure,
    const std::vector<std::string>& enabledCheatFeatureKeys)
{
    g_runtimeStopRequested.store(false, std::memory_order_release);
    g_lastRunExitedNormally.store(false, std::memory_order_release);
    g_lastRunAppPath.clear();
    pthread_mutex_lock(&g_runtimeThreadMutex);
    g_appMainEntry = 0;
    g_appMainInitCheckAddress = 0;
    g_currentAppSha256.clear();
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    g_options = options;
    g_clearRecentOnStartupFailure = clearRecentOnStartupFailure;
    g_enabledCheatFeatureKeys = enabledCheatFeatureKeys;
    g_appLoadPath = gamePathNormalize(appPath);
    if (g_appLoadPath.empty() || !gamePathHasAppExtension(g_appLoadPath))
    {
        printf("app-runtime: invalid APP path: %s\n",
            g_appLoadPath.empty() ? "(empty)" : g_appLoadPath.c_str());
        return false;
    }
    g_appMainPath = appGuestMainPathFromGamePath(g_appLoadPath);

    printf("app-runtime: start APP: %s\n", g_appLoadPath.c_str());
    printf("app-runtime: AppMain path: %s\n", g_appMainPath.c_str());
    printf("app-runtime: execution backend: %s\n", executionBackendName(options.backend));

    pthread_t tid;
    int ret = pthread_create(&tid, NULL, dingoopieRun, NULL);
    if (ret)
    {
        printf("DingooPie: pthread_create failed\n");
        assert(0);
        return false;
    }
    pthread_mutex_lock(&g_runtimeThreadMutex);
    g_runtimeThread = tid;
    g_runtimeThreadStarted = true;
    pthread_mutex_unlock(&g_runtimeThreadMutex);

    return true;
}

void appRuntimeApplySettings(void)
{
    bridge_apply_runtime_settings();
    pthread_mutex_lock(&g_runtimeThreadMutex);
    const ExecutionBackend backend = g_activeAppBackend;
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    if (backend == EXECUTION_BACKEND_PPSSPP_IRJIT)
    {
        ppssppShimApplyRuntimeSettings();
    }
}

void appRuntimeNotifyPauseRequested(void)
{
    pthread_mutex_lock(&g_runtimeThreadMutex);
    NativeRuntime* runtime = g_mainRuntime;
    if (runtime)
    {
        ppssppShimRequestPause(runtime);
    }
    pthread_mutex_unlock(&g_runtimeThreadMutex);
}

void appRuntimeStop(void)
{
    pthread_t tid = {};
    NativeRuntime* runtime = NULL;
    bool shouldJoin = false;
    bool joinedRuntime = false;
    bool requestStop = !g_runtimeStopRequested.exchange(true, std::memory_order_acq_rel);

    pthread_mutex_lock(&g_runtimeThreadMutex);
    if (g_runtimeThreadStarted)
    {
        tid = g_runtimeThread;
        shouldJoin = true;
        g_runtimeThreadStarted = false;
    }
    runtime = g_mainRuntime;
    if (runtime && requestStop)
    {
        printf("DingooPie: stop requested runtime=%p\n", (void*)runtime);
        taskSchedulerRequestShutdown("frontend exit");
        nativeRuntimeRequestStop(runtime);
    }
    pthread_mutex_unlock(&g_runtimeThreadMutex);

    if (shouldJoin)
    {
        int joinResult = pthread_join(tid, NULL);
        joinedRuntime = joinResult == 0;
        if (joinedRuntime)
        {
            printf("DingooPie: runtime thread joined\n");
        }
        else
        {
            printf("DingooPie: runtime thread join failed: %d\n", joinResult);
        }
    }

    bool exitedNormally = joinedRuntime && g_lastRunExitedNormally.load(std::memory_order_acquire);
    if (joinedRuntime)
    {
        MixerResetAfterRuntimeStop();
    }

    if (shouldJoin && !joinedRuntime)
    {
        printf("DingooPie: recent app not saved because runtime thread did not join\n");
    }
    else if (shouldJoin && !exitedNormally)
    {
        printf("DingooPie: recent app not saved because runtime did not exit normally\n");
    }
    if (exitedNormally)
    {
        gameHistorySaveRecent(g_lastRunAppPath, "normal APP runtime exit");
    }
}
