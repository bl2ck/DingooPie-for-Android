#include "app/runtime/app_runtime.h"
#include "app/runtime/app_runtime_context.h"

#include "shared/services/guest_package.h"
#include "shared/game/game_paths.h"
#include "config/cheats/cheat_runtime.h"
#include "app/runtime/app_cheat_adapter.h"
#include "app/runtime/app_crash_report.h"
#include "app/hle/app_hle.h"
#include "frontend/video/framebuffer.h"
#include "app/memory/app_framebuffer_mapping.h"
#include "app/hle/app_text_format.h"
#include "config/compatibility/compat_profile.h"
#include "shared/execution/pause_gate.h"
#include "shared/platform/storage_services.h"
#include "app/cpu/mips_compat.h"
#include "app/memory/app_memory.h"
#include "app/cpu/ppsspp_backend.h"
#include "app/runtime/app_runtime_debug.h"
#include "shared/execution/thread_join.h"
#include "frontend/shell/frontend_shell.h"
#include "frontend/audio/sdl_audio.h"
#include "app/hle/app_task_scheduler.h"
#include "shared/services/guest_filesystem.h"
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
#include "app/cpu/mips_runtime.h"

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
static pthread_mutex_t g_runtimeThreadMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_runtimeThread;
static bool g_runtimeThreadStarted = false;
static NativeRuntime* g_mainRuntime = NULL;
static GuestPackage* g_mainPackage = NULL;
static std::atomic<bool> g_runtimeStopRequested(false);
static std::atomic<bool> g_appMainHookFailed(false);
static RuntimeThreadCompletion g_runtimeThreadCompletion =
    RUNTIME_THREAD_COMPLETION_INITIALIZER;

static const uint32_t kRuntimeStopTimeoutMs = 5000;
static const uint32_t kSaveStatePauseTimeoutMs = 2000;

static bool waitForSaveStatePause(void)
{
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(kSaveStatePauseTimeoutMs);
    do
    {
        const uint32_t requiredWaiters =
            1u + (uint32_t)taskSchedulerRuntimeCount();
        if (pauseGateWaitForPausedWaiters(10, requiredWaiters))
        {
            return true;
        }
    }
    while (std::chrono::steady_clock::now() < deadline);

    printf("save-state: pause timeout waiters=%u runtimes=%u\n",
        pauseGateWaiterCount(),
        1u + (unsigned int)taskSchedulerRuntimeCount());
    return false;
}

static void setSaveStateRuntimeError(std::string* error, const char* message)
{
    if (error)
    {
        *error = message ? message : "";
    }
}

static void captureRuntimeRegisters(
    NativeRuntime* runtime, AppRuntimeRegisterSnapshot* out)
{
    memset(out, 0, sizeof(*out));
    if (!runtime)
    {
        return;
    }
    if (nativeRuntimeGetBackend(runtime) == EXECUTION_BACKEND_PPSSPP_IRJIT)
    {
        ppssppShimSyncStateToRuntime(runtime);
    }
    out->running = true;
    for (int index = 0; index < 32; ++index)
    {
        nativeRuntimeReadRegister(runtime, index, &out->gpr[index]);
    }
    float* fpr = nativeRuntimeFpr(runtime);
    float* vfpu = nativeRuntimeVfpu(runtime);
    uint32_t* vfpuCtrl = nativeRuntimeVfpuCtrl(runtime);
    uint32_t* fcr31 = nativeRuntimeFcr31(runtime);
    uint32_t* fpcond = nativeRuntimeFpCond(runtime);
    if (fpr) memcpy(out->fpr, fpr, sizeof(out->fpr));
    if (vfpu) memcpy(out->vfpu, vfpu, sizeof(out->vfpu));
    if (vfpuCtrl) memcpy(out->vfpuCtrl, vfpuCtrl, sizeof(out->vfpuCtrl));
    if (fcr31) out->fcr31 = *fcr31;
    if (fpcond) out->fpcond = *fpcond;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_PC, &out->pc);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_HI, &out->hi);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_LO, &out->lo);
}

static bool restoreRuntimeRegisters(
    NativeRuntime* runtime, const AppRuntimeRegisterSnapshot& state)
{
    if (!runtime || !state.running)
    {
        return false;
    }
    uint32_t zero = 0;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_ZERO, &zero);
    for (int index = 1; index < 32; ++index)
    {
        if (nativeRuntimeWriteRegister(runtime, index, &state.gpr[index]) != RUNTIME_OK)
        {
            return false;
        }
    }
    if (nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &state.pc) != RUNTIME_OK ||
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_HI, &state.hi) != RUNTIME_OK ||
        nativeRuntimeWriteRegister(runtime, RUNTIME_REG_LO, &state.lo) != RUNTIME_OK)
    {
        return false;
    }
    float* fpr = nativeRuntimeFpr(runtime);
    float* vfpu = nativeRuntimeVfpu(runtime);
    uint32_t* vfpuCtrl = nativeRuntimeVfpuCtrl(runtime);
    uint32_t* fcr31 = nativeRuntimeFcr31(runtime);
    uint32_t* fpcond = nativeRuntimeFpCond(runtime);
    if (fpr) memcpy(fpr, state.fpr, sizeof(state.fpr));
    if (vfpu) memcpy(vfpu, state.vfpu, sizeof(state.vfpu));
    if (vfpuCtrl) memcpy(vfpuCtrl, state.vfpuCtrl, sizeof(state.vfpuCtrl));
    if (fcr31) *fcr31 = state.fcr31;
    if (fpcond) *fpcond = state.fpcond;
    if (nativeRuntimeGetBackend(runtime) == EXECUTION_BACKEND_PPSSPP_IRJIT)
    {
        ppssppShimSyncStateFromRuntime(runtime);
    }
    return true;
}

static uint32_t s_appDataAddress = 0;
static uint32_t s_appDataSize = 0;
static void* s_appDataBuffer = NULL;
static GuestPackage* s_guestPackage = NULL;

static uint32_t g_appMainEntry = 0;
static uint32_t g_appMainInitCheckAddress = 0;

AppRuntimeProgramImage appRuntimeProgramImage(void)
{
    AppRuntimeProgramImage image = {};
    image.address = s_appDataAddress;
    image.size = s_appDataSize;
    image.data = s_appDataBuffer;
    image.package = s_guestPackage;
    return image;
}

static bool isPpssppRunBlockMarkerValue(uint32_t value)
{
    return (value & 0xff000000u) == 0x68000000u;
}

static uint32_t loadStateLe32(const uint8_t* bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static void storeStateLe32(uint8_t* bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xff);
    bytes[1] = (uint8_t)((value >> 8) & 0xff);
    bytes[2] = (uint8_t)((value >> 16) & 0xff);
    bytes[3] = (uint8_t)((value >> 24) & 0xff);
}

static bool addressInRange32(uint32_t address, uint32_t rangeStart, uint32_t rangeSize)
{
    return rangeSize != 0 && address >= rangeStart &&
        address - rangeStart < rangeSize;
}

static bool addressInMappedAppCode(uint32_t address)
{
    if (s_appDataSize == 0)
    {
        return false;
    }
    uint32_t aliasStart = s_appDataAddress & 0x1fffffffu;
    return addressInRange32(address, s_appDataAddress, s_appDataSize) ||
        addressInRange32(address, aliasStart, s_appDataSize);
}

struct RestoredIrJitMarkerReplacement
{
    size_t offset;
    uint32_t original;
};

static void stripIrJitMarkersFromCapturedRegion(AppRuntimeStateRegion* region)
{
    if (!region || region->data.size() != region->size ||
        region->size < sizeof(uint32_t))
    {
        return;
    }
    for (size_t offset = 0;
        offset + sizeof(uint32_t) <= region->data.size();
        offset += sizeof(uint32_t))
    {
        uint8_t* bytes = region->data.data() + offset;
        uint32_t value = loadStateLe32(bytes);
        uint32_t original = 0;
        if (isPpssppRunBlockMarkerValue(value) &&
            ppssppShimResolveEmuHack(
                region->start + (uint32_t)offset, value, &original))
        {
            storeStateLe32(bytes, original);
        }
    }
}

static void collectRestoredIrJitMarkerReplacements(
    const RuntimeMemoryRegion& target,
    const AppRuntimeStateRegion& region,
    std::vector<RestoredIrJitMarkerReplacement>* replacements)
{
    replacements->clear();
    if (!target.data || region.data.size() != region.size ||
        region.start != target.start || region.size != target.size)
    {
        return;
    }
    for (size_t offset = 0;
        offset + sizeof(uint32_t) <= region.data.size();
        offset += sizeof(uint32_t))
    {
        uint32_t value = loadStateLe32(region.data.data() + offset);
        if (!isPpssppRunBlockMarkerValue(value))
        {
            continue;
        }
        uint32_t original = 0;
        bool resolved = ppssppShimResolveEmuHack(
                region.start + (uint32_t)offset, value, &original) &&
            !isPpssppRunBlockMarkerValue(original);
        if (!resolved && addressInMappedAppCode(region.start + (uint32_t)offset))
        {
            uint32_t current = loadStateLe32(target.data + offset);
            if (isPpssppRunBlockMarkerValue(current))
            {
                resolved = ppssppShimResolveEmuHack(
                        region.start + (uint32_t)offset, current, &original) &&
                    !isPpssppRunBlockMarkerValue(original);
            }
            else
            {
                original = current;
                resolved = true;
            }
        }
        if (resolved)
        {
            RestoredIrJitMarkerReplacement replacement;
            replacement.offset = offset;
            replacement.original = original;
            replacements->push_back(replacement);
        }
    }
}

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
        appCheatUnbind(runtime);
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

static void destroyMainPackage(void)
{
    pthread_mutex_lock(&g_runtimeThreadMutex);
    GuestPackage* package = g_mainPackage;
    g_mainPackage = NULL;
    s_guestPackage = NULL;
    s_appDataAddress = 0;
    s_appDataSize = 0;
    s_appDataBuffer = NULL;
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    guestPackageDestroy(package);
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
        printf("app-runtime: failed to open app: %s\n", path.c_str());
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
            printf("app-runtime: failed to read non-seekable app: %s errno=%d\n",
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
        printf("app-runtime: invalid app size: %s\n", path.c_str());
        return NULL;
    }

    GuestPackage* loadedApp = guestPackageCreate(file, (uint32_t)fileSize);
    fclose(file);
    free(androidMemoryFileBuffer);

    if (!loadedApp)
    {
        printf("app-runtime: failed to parse app: %s\n", path.c_str());
        return NULL;
    }

    return loadedApp;
}
static void hookDingooPieDebug(NativeRuntime* runtime, uint64_t address, uint32_t size, void* userData)
{
    (void)address;
    (void)size;
    (void)userData;
    appTextFormatDebugPrint(runtime);
}

static bool hookMemInvalid(NativeRuntime* runtime, RuntimeMemoryAccess type, uint64_t address, int size, int64_t value, void* userData)
{
    (void)userData;
    appRuntimeDebugReportInvalidMemory(runtime, type, address, size, value);
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
        printf("app-runtime: vm_malloc failed for AppMain path, size=%u\n", pathBytes);
        g_appMainHookFailed.store(true, std::memory_order_release);
        nativeRuntimeRequestStop(runtime);
        return;
    }

    err = nativeRuntimeWriteMemory(runtime, pathPtr, appPathW.data(), pathBytes);
    if (err)
    {
        printf("app-runtime: nativeRuntimeWriteMemory(AppMain path) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        vm_free(pathPtr);
        g_appMainHookFailed.store(true, std::memory_order_release);
        nativeRuntimeRequestStop(runtime);
        return;
    }

    err = nativeRuntimeWriteRegister(runtime, RUNTIME_REG_A0, &pathPtr);
    if (err)
    {
        printf("app-runtime: nativeRuntimeWriteRegister(A0) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        vm_free(pathPtr);
        g_appMainHookFailed.store(true, std::memory_order_release);
        nativeRuntimeRequestStop(runtime);
        return;
    }

    uint32_t appMainEntry = (uint32_t)address;
    err = nativeRuntimeWriteRegister(runtime, RUNTIME_REG_T9, &appMainEntry);
    if (err)
    {
        printf("app-runtime: nativeRuntimeWriteRegister(T9) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        vm_free(pathPtr);
        g_appMainHookFailed.store(true, std::memory_order_release);
        nativeRuntimeRequestStop(runtime);
        return;
    }

    RuntimeHook* hookHandle = (RuntimeHook*)userData;
    err = nativeRuntimeRemoveHook(runtime, *hookHandle);
    if (err)
    {
        printf("app-runtime: nativeRuntimeRemoveHook(AppMain) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        vm_free(pathPtr);
        g_appMainHookFailed.store(true, std::memory_order_release);
        nativeRuntimeRequestStop(runtime);
        return;
    }

    free(userData);
}

static bool mapAppMemory(NativeRuntime* runtime, GuestPackage* loadedApp)
{
    s_appDataAddress = loadedApp->origin;
    s_appDataSize = loadedApp->bin_size;
    s_appDataBuffer = loadedApp->bin_data;
    s_guestPackage = loadedApp;

    RuntimeError err = nativeRuntimeMapMemory(runtime, s_appDataAddress, s_appDataSize, RUNTIME_PROT_ALL, s_appDataBuffer);
    if (err)
    {
        printf("app-runtime: failed to map app memory: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return false;
    }

    uint32_t aliasAddr = s_appDataAddress & 0x1fffffff;
    err = nativeRuntimeMapMemory(runtime, aliasAddr, s_appDataSize, RUNTIME_PROT_ALL, s_appDataBuffer);
    if (err)
    {
        printf("app-runtime: failed to map app alias: %u (%s)\n", err, nativeRuntimeErrorString(err));
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
        printf("app-runtime: failed to add mem invalid hook: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return false;
    }

    nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_CODE, (void*)hookDingooPieDebug, NULL, 0x80B0F060, 0x80B0F060);

    err = runtimeCompatInstallHooks(runtime, loadedApp, g_options);
    if (err != RUNTIME_OK)
    {
        printf("app-runtime: failed to add runtime compat hook: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return false;
    }

    g_appMainEntry = appMainEntry;
    g_appMainInitCheckAddress = appMainEntry + 0x34;

    err = nativeRuntimeAddHook(runtime, &trace, RUNTIME_HOOK_CODE, (void*)hookForceAppInitSuccess, NULL,
        g_appMainInitCheckAddress, g_appMainInitCheckAddress, 0);
    if (err != RUNTIME_OK)
    {
        printf("app-runtime: failed to add AppMain init hook: %u (%s)\n", err, nativeRuntimeErrorString(err));
        return false;
    }

    *appMainHook = (RuntimeHook*)malloc(sizeof(RuntimeHook));
    if (!*appMainHook)
    {
        printf("app-runtime: failed to allocate AppMain hook handle\n");
        return false;
    }

    err = nativeRuntimeAddHook(runtime, *appMainHook, RUNTIME_HOOK_CODE, (void*)hookAppMain, (void*)*appMainHook,
        appMainEntry, appMainEntry, 0);
    if (err != RUNTIME_OK)
    {
        printf("app-runtime: failed to add AppMain hook: %u (%s)\n", err, nativeRuntimeErrorString(err));
        free(*appMainHook);
        *appMainHook = NULL;
        return false;
    }

    return true;
}

static void logEmulationFailure(NativeRuntime* runtime, RuntimeError err, const CrashLogContext& context)
{
    printf("app-runtime: native runtime failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
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
    g_appMainHookFailed.store(false, std::memory_order_release);

    NativeRuntime* runtime;
    RuntimeError err = nativeRuntimeCreate(&runtime);
    if (err)
    {
        printf("app-runtime: nativeRuntimeCreate failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
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
    pthread_mutex_lock(&g_runtimeThreadMutex);
    g_mainPackage = loadedApp;
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    std::string appSha256 = sha256Hex(loadedApp->file_data, loadedApp->file_size);
    pthread_mutex_lock(&g_runtimeThreadMutex);
    g_currentAppSha256 = appSha256;
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    bridge_set_game_identity(appSha256.c_str());
    fsys_set_game_identity(appSha256.c_str());
    fsys_set_game_name(g_appMainPath.c_str());
    std::string saveDirectory = platformGetAppSaveDirectory(
        g_appLoadPath, appSha256);
    fsys_set_save_directory(saveDirectory.c_str());
    printf("app-runtime: save directory: %s\n", saveDirectory.c_str());
    cheatRuntimeLoadForGame(
        appSha256.c_str(),
        g_appLoadPath.c_str(),
        g_enabledCheatFeatureKeys);
    printf("app-runtime: app sha256: %s\n", appSha256.c_str());
    printf("app-runtime: compat profile: %s\n", compatProfileName(appSha256.c_str()));

    ExecutionBackend effectiveBackend = g_options.backend;
    if (compatForcedBackend(appSha256.c_str(), &effectiveBackend))
    {
        printf("app-runtime: compat backend override: requested=%s effective=%s\n",
            executionBackendName(g_options.backend), executionBackendName(effectiveBackend));
    }
    if (effectiveBackend == EXECUTION_BACKEND_PPSSPP_IRJIT && !ppssppIrJitBackendAvailable())
    {
        printf("app-runtime: PPSSPP IR JIT unavailable, falling back to compatibility mode\n");
        effectiveBackend = EXECUTION_BACKEND_COMPATIBILITY;
    }

    pthread_mutex_lock(&g_runtimeThreadMutex);
    g_activeAppBackend = effectiveBackend;
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    appRuntimeApplySettings();

    err = nativeRuntimeSetBackend(runtime, effectiveBackend);
    if (err)
    {
        printf("app-runtime: nativeRuntimeSetBackend failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        destroyMainRuntime(runtime);
        return NULL;
    }
    printf("app-runtime: execution backend effective: %s\n", executionBackendName(effectiveBackend));

    if (!mapAppMemory(runtime, loadedApp))
    {
        destroyMainRuntime(runtime);
        return NULL;
    }
    appCheatBind(runtime);

    err = bridge_init(runtime, loadedApp);
    if (err)
    {
        printf("app-runtime: bridge_init failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        destroyMainRuntime(runtime);
        return NULL;
    }

    if (appMemoryInitialize(runtime, loadedApp))
    {
        printf("app-runtime: appMemoryInitialize failed\n");
        destroyMainRuntime(runtime);
        return NULL;
    }

    if (appFramebufferInitialize(runtime))
    {
        printf("app-runtime: framebuffer initialization failed\n");
        destroyMainRuntime(runtime);
        return NULL;
    }

    uint32_t value = 0;
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_ZERO, &value);

    uint32_t appMainEntry = findAppMainEntry(loadedApp);
    if (!appMainEntry)
    {
        printf("app-runtime: AppMain export not found\n");
        destroyMainRuntime(runtime);
        return NULL;
    }
    printf("app-runtime: AppMain entry=0x%08x boot entry=0x%08x origin=0x%08x size=0x%08x\n",
        appMainEntry, loadedApp->bin_entry, loadedApp->origin, loadedApp->bin_size);

    RuntimeHook* appMainHook = NULL;
    if (!installCoreHooks(runtime, loadedApp, appMainEntry, &appMainHook))
    {
        destroyMainRuntime(runtime);
        return NULL;
    }
    appCheatApplyStartup(runtime);

    err = nativeRuntimeWriteRegister(runtime, RUNTIME_REG_RA, &appMainEntry);
    if (err)
    {
        printf("app-runtime: nativeRuntimeWriteRegister(RA) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        destroyMainRuntime(runtime);
        return NULL;
    }

    uint32_t mallocLcdBuffer = 0;
    err = nativeRuntimeWriteRegister(runtime, RUNTIME_REG_A1, &mallocLcdBuffer);
    if (err)
    {
        printf("app-runtime: nativeRuntimeWriteRegister(A1) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
        destroyMainRuntime(runtime);
        return NULL;
    }

    err = nativeRuntimeWriteRegister(runtime, RUNTIME_REG_T9, &loadedApp->bin_entry);
    if (err)
    {
        printf("app-runtime: nativeRuntimeWriteRegister(T9 entry) failed: %u (%s)\n", err, nativeRuntimeErrorString(err));
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
        printf("app-runtime: forcing guest crash for diagnostics\n");
        logEmulationFailure(runtime, RUNTIME_ERROR_EXCEPTION, crashContext);
        frontendRequestQuit();
        return runtime;
    }

    printf("app-runtime: native runtime start pc=0x%08x until=0xffffffff\n", loadedApp->bin_entry);
    err = nativeRuntimeStart(runtime, loadedApp->bin_entry, 0xFFFFFFFF, 0, 0);
    printf("app-runtime: native runtime returned err=%u (%s)\n", err, nativeRuntimeErrorString(err));
    if (g_appMainHookFailed.load(std::memory_order_acquire))
    {
        printf("app-runtime: AppMain hook initialization failed; returning to library\n");
        frontendRequestGameExit();
        return runtime;
    }
    if (err)
    {
        if (frontendGameExitRequested())
        {
            return runtime;
        }
        CompatGuestExitDecision exitDecision = (err == RUNTIME_ERROR_EXCEPTION)
            ? runtimeExceptionGuestExitDecision(runtime, appSha256)
            : noGuestExitDecision();
        if (exitDecision.shouldExit)
        {
            printf("app-runtime: treating %s as normal guest exit\n",
                exitDecision.label ? exitDecision.label : "compat exception");
            frontendRequestGameExit();
            return runtime;
        }
        logEmulationFailure(runtime, err, crashContext);
        frontendRequestQuit();
        return runtime;
    }

    if (!g_runtimeStopRequested.load(std::memory_order_acquire))
    {
        printf("app-runtime: guest completed; returning to library\n");
        frontendRequestGameExit();
    }
    return runtime;
}

static void* dingoopieRun(void* data)
{
    struct RuntimeThreadCompletionGuard
    {
        ~RuntimeThreadCompletionGuard()
        {
            runtimeThreadCompletionSignal(&g_runtimeThreadCompletion);
        }
    } completionGuard;
    (void)data;
    NativeRuntime* runtime = initDingooPie();
    taskSchedulerRequestShutdown("main runtime exit");
    taskSchedulerWaitForTasks();
    bridge_release_game_resources();
    if (runtime)
    {
        destroyMainRuntime(runtime);
    }
    destroyMainPackage();
    return 0;
}

bool appRuntimeStart(
    const char* appPath,
    const EmulatorOptions& options,
    const std::vector<std::string>& enabledCheatFeatureKeys)
{
    g_runtimeStopRequested.store(false, std::memory_order_release);
    runtimeThreadCompletionReset(&g_runtimeThreadCompletion);
    pthread_mutex_lock(&g_runtimeThreadMutex);
    g_appMainEntry = 0;
    g_appMainInitCheckAddress = 0;
    g_currentAppSha256.clear();
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    g_options = options;
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
        printf("app-runtime: pthread_create failed\n");
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

uint32_t appRuntimeActiveThreadCount(void)
{
    pthread_mutex_lock(&g_runtimeThreadMutex);
    uint32_t count = g_mainRuntime ?
        1u + (uint32_t)taskSchedulerRuntimeCount() : 0u;
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    return count;
}

bool appRuntimeCaptureState(AppRuntimeState* out, std::string* error)
{
    if (!out)
    {
        setSaveStateRuntimeError(error, "runtime state output is invalid");
        return false;
    }
    out->regions.clear();
    out->taskRegisters.clear();
    out->hleSemaphoreCounts.clear();
    memset(&out->registers, 0, sizeof(out->registers));
    memset(&out->heap, 0, sizeof(out->heap));
    out->osTicks = 0;

    if (!waitForSaveStatePause())
    {
        setSaveStateRuntimeError(error, "runtime did not pause in time");
        return false;
    }

    pthread_mutex_lock(&g_runtimeThreadMutex);
    NativeRuntime* runtime = g_mainRuntime;
    if (!runtime)
    {
        pthread_mutex_unlock(&g_runtimeThreadMutex);
        setSaveStateRuntimeError(error, "runtime state is not available");
        return false;
    }
    captureRuntimeRegisters(runtime, &out->registers);
    out->osTicks = bridge_capture_os_ticks();
    uint32_t semaphoreCount = bridge_semaphore_state_count();
    out->hleSemaphoreCounts.resize(semaphoreCount);
    bridge_capture_semaphore_counts(out->hleSemaphoreCounts.data(), semaphoreCount);
    if (!vmHeapCaptureSnapshot(&out->heap))
    {
        pthread_mutex_unlock(&g_runtimeThreadMutex);
        return false;
    }

    std::vector<NativeRuntime*> taskRuntimes;
    taskSchedulerSnapshotRuntimes(&taskRuntimes);
    out->taskRegisters.reserve(taskRuntimes.size());
    for (size_t index = 0; index < taskRuntimes.size(); ++index)
    {
        AppRuntimeRegisterSnapshot snapshot;
        captureRuntimeRegisters(taskRuntimes[index], &snapshot);
        out->taskRegisters.push_back(snapshot);
    }

    std::vector<const uint8_t*> capturedPointers;
    size_t regionCount = nativeRuntimeMemoryRegionCount(runtime);
    for (size_t index = 0; index < regionCount; ++index)
    {
        RuntimeMemoryRegion region;
        if (!nativeRuntimeGetMemoryRegion(runtime, index, &region) || !region.data ||
            !(region.perms & RUNTIME_PROT_WRITE) || region.size == 0 ||
            std::find(capturedPointers.begin(), capturedPointers.end(), region.data) !=
                capturedPointers.end())
        {
            continue;
        }
        AppRuntimeStateRegion captured;
        captured.start = region.start;
        captured.size = region.size;
        captured.perms = region.perms;
        captured.data.assign(region.data, region.data + region.size);
        stripIrJitMarkersFromCapturedRegion(&captured);
        out->regions.push_back(std::move(captured));
        capturedPointers.push_back(region.data);
    }
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    bool captured = out->registers.running && !out->regions.empty();
    if (!captured)
    {
        setSaveStateRuntimeError(error, "runtime state is not available");
    }
    return captured;
}

bool appRuntimeRestoreState(const AppRuntimeState& state, std::string* error)
{
    if (!state.registers.running)
    {
        setSaveStateRuntimeError(error, "saved runtime state is invalid");
        return false;
    }
    if (!waitForSaveStatePause())
    {
        setSaveStateRuntimeError(error, "runtime did not pause in time");
        return false;
    }
    pthread_mutex_lock(&g_runtimeThreadMutex);
    NativeRuntime* runtime = g_mainRuntime;
    if (!runtime)
    {
        pthread_mutex_unlock(&g_runtimeThreadMutex);
        setSaveStateRuntimeError(error, "runtime state is not available");
        return false;
    }
    std::vector<NativeRuntime*> taskRuntimes;
    taskSchedulerSnapshotRuntimes(&taskRuntimes);
    if (taskRuntimes.size() != state.taskRegisters.size())
    {
        pthread_mutex_unlock(&g_runtimeThreadMutex);
        setSaveStateRuntimeError(error, "runtime thread count does not match save state");
        return false;
    }

    std::vector<RuntimeMemoryRegion> runtimeRegions;
    size_t regionCount = nativeRuntimeMemoryRegionCount(runtime);
    for (size_t index = 0; index < regionCount; ++index)
    {
        RuntimeMemoryRegion region;
        if (nativeRuntimeGetMemoryRegion(runtime, index, &region) && region.data &&
            (region.perms & RUNTIME_PROT_WRITE) && region.size)
        {
            runtimeRegions.push_back(region);
        }
    }
    std::vector<size_t> restoreTargets(state.regions.size(), (size_t)-1);
    std::vector<bool> usedRuntimeRegions(runtimeRegions.size(), false);
    for (size_t index = 0; index < state.regions.size(); ++index)
    {
        const AppRuntimeStateRegion& saved = state.regions[index];
        if (saved.size == 0 || saved.data.size() != saved.size)
        {
            pthread_mutex_unlock(&g_runtimeThreadMutex);
            setSaveStateRuntimeError(error, "runtime memory layout does not match save state");
            return false;
        }
        for (size_t targetIndex = 0; targetIndex < runtimeRegions.size(); ++targetIndex)
        {
            if (!usedRuntimeRegions[targetIndex] &&
                runtimeRegions[targetIndex].start == saved.start &&
                runtimeRegions[targetIndex].size == saved.size)
            {
                restoreTargets[index] = targetIndex;
                usedRuntimeRegions[targetIndex] = true;
                break;
            }
        }
        if (restoreTargets[index] == (size_t)-1)
        {
            pthread_mutex_unlock(&g_runtimeThreadMutex);
            setSaveStateRuntimeError(error, "runtime memory layout does not match save state");
            return false;
        }
    }
    std::vector<std::vector<RestoredIrJitMarkerReplacement> > markerReplacements(
        state.regions.size());
    for (size_t index = 0; index < state.regions.size(); ++index)
    {
        collectRestoredIrJitMarkerReplacements(
            runtimeRegions[restoreTargets[index]], state.regions[index],
            &markerReplacements[index]);
    }
    for (size_t index = 0; index < state.regions.size(); ++index)
    {
        const AppRuntimeStateRegion& saved = state.regions[index];
        RuntimeMemoryRegion& target = runtimeRegions[restoreTargets[index]];
        memcpy(target.data, saved.data.data(), saved.size);
        for (size_t replacementIndex = 0;
            replacementIndex < markerReplacements[index].size();
            ++replacementIndex)
        {
            const RestoredIrJitMarkerReplacement& replacement =
                markerReplacements[index][replacementIndex];
            storeStateLe32(target.data + replacement.offset, replacement.original);
        }
    }

    bool restored = vmHeapRestoreSnapshot(state.heap) &&
        bridge_restore_semaphore_counts(state.hleSemaphoreCounts.data(),
            (uint32_t)state.hleSemaphoreCounts.size()) &&
        restoreRuntimeRegisters(runtime, state.registers);
    for (size_t index = 0; restored && index < taskRuntimes.size(); ++index)
    {
        restored = restoreRuntimeRegisters(taskRuntimes[index], state.taskRegisters[index]);
    }
    if (restored)
    {
        bridge_restore_os_ticks(state.osTicks);
        nativeRuntimeFlushCodeCache(runtime);
        for (size_t index = 0; index < taskRuntimes.size(); ++index)
        {
            nativeRuntimeFlushCodeCache(taskRuntimes[index]);
        }
        framebufferPresentRestoredFrame();
    }
    pthread_mutex_unlock(&g_runtimeThreadMutex);
    if (!restored)
    {
        setSaveStateRuntimeError(error, "failed to restore runtime state");
    }
    return restored;
}

bool appRuntimeStop(void)
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
    }
    runtime = g_mainRuntime;
    if (runtime && requestStop)
    {
        taskSchedulerRequestShutdown("frontend exit");
        nativeRuntimeRequestStop(runtime);
    }
    pthread_mutex_unlock(&g_runtimeThreadMutex);

    if (shouldJoin)
    {
        int joinError = 0;
        const RuntimeThreadJoinResult joinResult = runtimeThreadJoinWithTimeout(
            tid, &g_runtimeThreadCompletion, kRuntimeStopTimeoutMs, &joinError);
        joinedRuntime = joinResult == RUNTIME_THREAD_JOINED;
        if (joinedRuntime)
        {
            pthread_mutex_lock(&g_runtimeThreadMutex);
            g_runtimeThreadStarted = false;
            pthread_mutex_unlock(&g_runtimeThreadMutex);
        }
        else if (joinResult == RUNTIME_THREAD_JOIN_TIMEOUT)
        {
            printf("app-runtime: runtime thread did not stop within %u ms\n",
                kRuntimeStopTimeoutMs);
        }
        else
        {
            printf("app-runtime: runtime thread join failed: %d\n", joinError);
        }
    }

    if (joinedRuntime)
    {
        audioOutputResetAfterRuntimeStop();
    }

    return !shouldJoin || joinedRuntime;
}
