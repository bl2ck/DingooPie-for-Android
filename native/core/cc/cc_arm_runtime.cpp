#include "cc/cc_arm_runtime.h"
#include "cc/cc_save_state.h"
#include "cc/cc_package_layout.h"

#include "guest/guest_package.h"
#include "game/game_paths.h"
#include "cc/arm32_interpreter.h"
#include "cc/cc_graphics_compat.h"
#include "cc/cc_runtime_timing.h"
#include "cc/cc_input_mapping.h"
#include "config/cheat_runtime.h"
#include "config/emulator_config.h"
#include "runtime/execution_backend.h"
#include "runtime/crash_log.h"
#include "frontend/framebuffer.h"
#include "guest/guest_audio.h"
#include "guest/guest_filesystem.h"
#include "frontend/input_controls.h"
#include "frontend/input_state.h"
#include "runtime/pause_gate.h"
#include "platform_services.h"
#include "runtime/runtime_log.h"
#include "Common/Crypto/sha256.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <thread>
#include <vector>
#include <mutex>

static const uint32_t kCcRetailRamStart = 0x10000000u;
static const uint32_t kCcRetailRamSize = 0x04000000u;
static const uint32_t kCcRetailHeapStart = 0x21000000u;
static const uint32_t kCcRetailHeapSize = 0x02000000u;
static const uint32_t kCcHomebrewHeapStart = 0x09000000u;
static const uint32_t kCcHomebrewHeapSize = 0x02000000u;
static const uint32_t kCcHomebrewSystemRamStart = 0x10000000u;
static const uint32_t kCcHomebrewSystemRamSize = 0x03800000u;
// The CC1800 SDK linker script defines one 16 MiB application window.
static const uint32_t kCcHomebrewRamStart = kCcHomebrewProgramOrigin;
static const uint32_t kCcHomebrewRamSize = 0x01000000u;
static const uint32_t kStackStart = 0x1ff00000u;
static const uint32_t kStackSize = 0x00100000u;
static const uint32_t kLegacyLowMemorySize = 0x00010000u;
static const uint32_t kLoaderHandle = kStackStart + 0x100u;
static const uint32_t kAppPathWideString = kStackStart + 0x200u;
static const uint32_t kLocaleString = kStackStart + 0x600u;
static const uint32_t kDynamicThunkStart = kStackStart + 0x1000u;
static const uint32_t kExitAddress = kStackStart + kStackSize - 4u;
static const uint32_t kFramebufferAddress = 0x80000000u;
static const uint32_t kFramebufferWidth = 320u;
static const uint32_t kFramebufferHeight = 240u;
static const uint32_t kFramebuffer16Size =
    kFramebufferWidth * kFramebufferHeight * sizeof(uint16_t);
static const uint32_t kFramebuffer32Size =
    kFramebufferWidth * kFramebufferHeight * sizeof(uint32_t);
static const uint32_t kCcFramebufferStride = 0x0004c000u;
static const uint32_t kCcFramebufferCount = 4u;
static const uint32_t kCcFramebufferSize =
    kCcFramebufferStride * kCcFramebufferCount;
static const uint32_t kCcVideoMemorySize = 0x00800000u;
static const uint32_t kLegacyMmioStart = 0x04000000u;
static const uint32_t kLegacyMmioSize = 0x00100000u;
static const uint32_t kLegacyAudioMmioStart = 0x08a00000u;
static const uint32_t kLegacyAudioMmioSize = 0x00010000u;
static const uint32_t kDvcAudioHandle = 1u;
static const uint32_t kDvcAudioDefaultSampleRate = 44100u;
static const uint32_t kDvcAudioMaxVolume = 30u;
static const uint32_t kDvcAudioSetSampleRate = 0x0du;
static const uint32_t kDvcAudioStartPlayback = 0x0bu;
static const char kDvcAudioDeviceName[] = "ROOT\\DVC\\IIS\\IIS0";
static const uint32_t kLegacySystemMmioStart = 0x09300000u;
static const uint32_t kLegacySystemMmioSize = 0x00010000u;
static const uint32_t kLegacyFramebufferAddress = 0x11800000u;
static const uint32_t kLegacyGraphicsSurface = 0x0930201cu;
static const uint32_t kLegacyGraphicsStride = 0x09302020u;
static const uint32_t kLegacyGraphicsStatus = 0x09303054u;
static const uint32_t kLegacyGraphicsReady = 1u << 2;
static const uint64_t kSliceInstructions = 50000u;
static const uint64_t kNonAudioSliceInstructions = 5000u;
static const double kAutoRuntimeSpeedScale = 0.65;
static const uint64_t kReferenceCpuClockHz = 336000000u;
static const uint64_t kReferenceInterpreterIps = 15000000u;

static std::atomic<bool> s_stopRequested(false);
static std::atomic<bool> s_running(false);
static std::atomic<double> s_runtimeSpeedScale(kAutoRuntimeSpeedScale);
static std::atomic<double> s_hostDelayScale(1.0);
static std::atomic<uint64_t> s_targetInstructionsPerSecond(0);
static std::atomic<bool> s_compatibilityExecutionMode(false);
static std::mutex s_runtimeMutex;
struct CcArmRuntime;
static CcArmRuntime* s_activeRuntime = NULL;
static std::string sha256Hex(const uint8_t* data, uint32_t size);

struct CcArmRuntime
{
    struct HeapBlock
    {
        uint32_t address;
        uint32_t size;
        bool free;
    };

    struct Task
    {
        Arm32State state;
        uint32_t entry;
        uint32_t argument;
        uint32_t stack;
        uint32_t priority;
        uint32_t delayTicks;
        bool started;
        bool finished;
        bool audioProducer;
    };

    struct ResourceHandle
    {
        uint32_t address;
        GuestResourceEntry* entry;
        uint32_t position;
        uint32_t dataAddress;
    };

    struct FileHandle
    {
        uint32_t address;
        uint32_t stream;
    };

    struct Semaphore
    {
        uint32_t address;
        uint32_t count;
    };

    GuestPackage* package;
    std::vector<uint8_t> ram;
    std::vector<uint8_t> systemMemory;
    std::vector<uint8_t> stack;
    std::vector<uint8_t> heapMemory;
    std::vector<uint8_t> legacyLowMemory;
    std::vector<uint8_t> framebuffer;
    std::vector<uint8_t> legacyMmio;
    std::vector<uint8_t> legacyAudioMmio;
    std::vector<uint8_t> legacySystemMmio;
    std::vector<Arm32InstructionCacheEntry> instructionCache;
    std::vector<HeapBlock> heap;
    std::vector<Task> tasks;
    std::vector<ResourceHandle> resources;
    std::vector<FileHandle> files;
    std::vector<Semaphore> semaphores;
    std::vector<std::string> dynamicImports;
    std::vector<std::string> unknownImportNames;
    std::vector<uint32_t> openStreams;
    uint32_t ramStart;
    uint32_t heapStart;
    uint32_t heapCursor;
    uint32_t currentTaskIndex;
    uint32_t currentDelayTicks;
    uint32_t dvcAudioHandle;
    uint32_t dvcAudioSampleRate;
    uint32_t dvcAudioVolume;
    uint32_t framebufferAddress;
    uint32_t framebufferBits;
    uint32_t framebufferWriteHighWater[kCcFramebufferCount];
    bool framebufferBitsExplicit;
    uint32_t faultAddress;
    uint32_t faultSize;
    bool faultWrite;
    bool faultFetch;
    bool yielded;
    bool cc1800Compatibility;
    bool dvcAudioStarted;
    Arm32Bus bus;
    CcArmRuntimeStats* stats;
    std::chrono::steady_clock::time_point startTime;
    uint64_t profileLastMillis;
    uint64_t profileLastInstructions;
};

static uint64_t currentGuestMicros(const CcArmRuntime* runtime);

static void presentFramebuffer(CcArmRuntime* runtime, uint32_t requestedAddress);

static void setCcStateError(std::string* error, const char* text)
{
    if (error) *error = text;
}

static GuestResourceEntry* findCcResourceByName(CcArmRuntime* runtime,
    const std::string& name)
{
    return runtime && runtime->package ? guestPackageFindResource(
        runtime->package, name.c_str()) : NULL;
}

uint32_t ccArmRuntimeActiveTaskCount(void)
{
    std::lock_guard<std::mutex> lock(s_runtimeMutex);
    return s_activeRuntime ? (uint32_t)s_activeRuntime->tasks.size() : 0;
}

bool ccArmRuntimeCaptureState(CcRuntimeState* out, std::string* error)
{
    if (!out)
    {
        setCcStateError(error, "runtime state output is invalid");
        return false;
    }
    if (!pauseGateWaitForPausedWaiters(2000, 1))
    {
        setCcStateError(error, "runtime did not pause in time");
        return false;
    }
    std::lock_guard<std::mutex> lock(s_runtimeMutex);
    CcArmRuntime* runtime = s_activeRuntime;
    if (!runtime || !runtime->package)
    {
        setCcStateError(error, "runtime state is not available");
        return false;
    }
    for (size_t i = 0; i < runtime->openStreams.size(); ++i)
    {
        if (fsys_stream_is_external_file(runtime->openStreams[i]))
        {
            setCcStateError(error, "CC state has unsupported external file stream");
            return false;
        }
    }
    *out = CcRuntimeState();
    out->gameSha256 = sha256Hex(runtime->package->file_data, runtime->package->file_size);
    out->ram = runtime->ram;
    out->systemMemory = runtime->systemMemory;
    out->stack = runtime->stack;
    out->heapMemory = runtime->heapMemory;
    out->legacyLowMemory = runtime->legacyLowMemory;
    out->framebuffer = runtime->framebuffer;
    out->legacyMmio = runtime->legacyMmio;
    out->legacyAudioMmio = runtime->legacyAudioMmio;
    out->legacySystemMmio = runtime->legacySystemMmio;
    for (size_t i = 0; i < runtime->heap.size(); ++i)
    {
        out->heap.push_back({ runtime->heap[i].address,
            runtime->heap[i].size, runtime->heap[i].free });
    }
    for (size_t i = 0; i < runtime->tasks.size(); ++i)
    {
        const CcArmRuntime::Task& task = runtime->tasks[i];
        CcSaveTask savedTask = {};
        savedTask.state = task.state;
        savedTask.entry = task.entry;
        savedTask.argument = task.argument;
        savedTask.stack = task.stack;
        savedTask.priority = task.priority;
        savedTask.delayTicks = task.delayTicks;
        savedTask.started = task.started;
        savedTask.finished = task.finished;
        savedTask.audioProducer = task.audioProducer;
        out->tasks.push_back(savedTask);
    }
    for (size_t i = 0; i < runtime->resources.size(); ++i)
    {
        const CcArmRuntime::ResourceHandle& resource = runtime->resources[i];
        if (!resource.entry)
        {
            continue;
        }
        out->resources.push_back({ resource.address,
            resource.entry->name ? resource.entry->name : "",
            resource.position, resource.dataAddress });
    }
    for (size_t i = 0; i < runtime->openStreams.size(); ++i)
    {
        uint32_t stream = runtime->openStreams[i];
        out->streams.push_back({ stream, fsys_stream_request_name(stream),
            fsys_stream_position(stream) });
    }
    for (size_t i = 0; i < runtime->files.size(); ++i)
    {
        out->files.push_back({ runtime->files[i].address, runtime->files[i].stream });
    }
    for (size_t i = 0; i < runtime->semaphores.size(); ++i)
    {
        out->semaphores.push_back({ runtime->semaphores[i].address,
            runtime->semaphores[i].count });
    }
    out->dynamicImports = runtime->dynamicImports;
    out->unknownImportNames = runtime->unknownImportNames;
    out->elapsedGuestMicros = currentGuestMicros(runtime);
    out->runtimeInstructions = runtime->stats->instructions;
    out->heapStart = runtime->heapStart;
    out->heapCursor = runtime->heapCursor;
    out->dvcAudioHandle = runtime->dvcAudioHandle;
    out->dvcAudioSampleRate = runtime->dvcAudioSampleRate;
    out->dvcAudioVolume = runtime->dvcAudioVolume;
    out->framebufferAddress = runtime->framebufferAddress;
    out->framebufferBits = runtime->framebufferBits;
    memcpy(out->framebufferWriteHighWater, runtime->framebufferWriteHighWater,
        sizeof(out->framebufferWriteHighWater));
    out->framebufferBitsExplicit = runtime->framebufferBitsExplicit;
    out->cc1800Compatibility = runtime->cc1800Compatibility;
    out->dvcAudioStarted = runtime->dvcAudioStarted;
    return !out->tasks.empty();
}

bool ccArmRuntimeRestoreState(const CcRuntimeState& state, std::string* error)
{
    if (!state.runtimeInstructions || state.tasks.empty() || state.tasks.size() > 32)
    {
        setCcStateError(error, "saved runtime state is invalid");
        return false;
    }
    if (!pauseGateWaitForPausedWaiters(2000, 1))
    {
        setCcStateError(error, "runtime did not pause in time");
        return false;
    }

    std::lock_guard<std::mutex> lock(s_runtimeMutex);
    CcArmRuntime* runtime = s_activeRuntime;
    if (!runtime || !runtime->package ||
        runtime->ram.size() != state.ram.size() ||
        runtime->systemMemory.size() != state.systemMemory.size() ||
        runtime->stack.size() != state.stack.size() ||
        runtime->heapMemory.size() != state.heapMemory.size() ||
        state.heapStart != runtime->heapStart ||
        state.heapCursor < state.heapStart ||
        (uint64_t)state.heapCursor >
            (uint64_t)state.heapStart + state.heapMemory.size() ||
        runtime->framebuffer.size() != state.framebuffer.size())
    {
        setCcStateError(error, "runtime memory layout does not match save state");
        return false;
    }
    if (sha256Hex(runtime->package->file_data, runtime->package->file_size) !=
        state.gameSha256)
    {
        setCcStateError(error, "save-state belongs to a different game");
        return false;
    }

    std::vector<GuestResourceEntry*> restoredResourceEntries;
    restoredResourceEntries.reserve(state.resources.size());
    for (size_t i = 0; i < state.resources.size(); ++i)
    {
        GuestResourceEntry* entry = findCcResourceByName(runtime, state.resources[i].name);
        if (!entry)
        {
            setCcStateError(error, "saved resource is not available");
            return false;
        }
        restoredResourceEntries.push_back(entry);
    }

    std::vector<uint32_t> restoredStreams;
    restoredStreams.reserve(state.streams.size());
    auto closeRestoredStreams = [&restoredStreams]()
    {
        for (size_t i = 0; i < restoredStreams.size(); ++i)
        {
            fsys_fclose(restoredStreams[i]);
        }
    };
    for (size_t i = 0; i < state.streams.size(); ++i)
    {
        uint32_t stream = fsys_fopen(state.streams[i].requestName.c_str(), "rb");
        if (!stream || fsys_fseek(stream, state.streams[i].position, SEEK_SET) != 0)
        {
            if (stream) fsys_fclose(stream);
            closeRestoredStreams();
            setCcStateError(error, "saved file stream is not available");
            return false;
        }
        restoredStreams.push_back(stream);
    }

    auto findRestoredStream = [&state, &restoredStreams](uint32_t savedStream)
    {
        for (size_t i = 0; i < state.streams.size(); ++i)
        {
            if (state.streams[i].stream == savedStream)
            {
                return restoredStreams[i];
            }
        }
        return 0u;
    };

    std::vector<CcArmRuntime::Task> restoredTasks;
    restoredTasks.reserve(state.tasks.size());
    for (size_t i = 0; i < state.tasks.size(); ++i)
    {
        const CcSaveTask& savedTask = state.tasks[i];
        CcArmRuntime::Task task = {};
        task.state = savedTask.state;
        task.entry = savedTask.entry;
        task.argument = savedTask.argument;
        task.stack = savedTask.stack;
        task.priority = savedTask.priority;
        task.delayTicks = savedTask.delayTicks;
        task.started = savedTask.started;
        task.finished = savedTask.finished;
        task.audioProducer = savedTask.audioProducer;
        restoredTasks.push_back(task);
    }

    std::vector<CcArmRuntime::ResourceHandle> restoredResources;
    restoredResources.reserve(state.resources.size());
    for (size_t i = 0; i < state.resources.size(); ++i)
    {
        restoredResources.push_back({ state.resources[i].address,
            restoredResourceEntries[i], state.resources[i].position,
            state.resources[i].dataAddress });
    }

    std::vector<CcArmRuntime::FileHandle> restoredFiles;
    restoredFiles.reserve(state.files.size());
    for (size_t i = 0; i < state.files.size(); ++i)
    {
        uint32_t stream = findRestoredStream(state.files[i].stream);
        if (!stream)
        {
            closeRestoredStreams();
            setCcStateError(error, "saved file handle stream is not available");
            return false;
        }
        restoredFiles.push_back({ state.files[i].address, stream });
    }

    for (size_t i = 0; i < runtime->openStreams.size(); ++i)
    {
        fsys_fclose(runtime->openStreams[i]);
    }
    runtime->ram = state.ram;
    runtime->systemMemory = state.systemMemory;
    runtime->stack = state.stack;
    runtime->heapMemory = state.heapMemory;
    runtime->legacyLowMemory = state.legacyLowMemory;
    runtime->framebuffer = state.framebuffer;
    runtime->legacyMmio = state.legacyMmio;
    runtime->legacyAudioMmio = state.legacyAudioMmio;
    runtime->legacySystemMmio = state.legacySystemMmio;
    runtime->heap.clear();
    for (size_t i = 0; i < state.heap.size(); ++i)
    {
        runtime->heap.push_back({ state.heap[i].address,
            state.heap[i].size, state.heap[i].free });
    }
    runtime->tasks.swap(restoredTasks);
    runtime->resources.swap(restoredResources);
    runtime->files.swap(restoredFiles);
    runtime->openStreams.swap(restoredStreams);
    runtime->semaphores.clear();
    for (size_t i = 0; i < state.semaphores.size(); ++i)
    {
        runtime->semaphores.push_back({ state.semaphores[i].address,
            state.semaphores[i].count });
    }
    runtime->dynamicImports = state.dynamicImports;
    runtime->unknownImportNames = state.unknownImportNames;
    runtime->currentTaskIndex = UINT32_MAX;

    runtime->heapStart = state.heapStart;
    runtime->heapCursor = state.heapCursor;
    runtime->dvcAudioHandle = state.dvcAudioHandle;
    runtime->dvcAudioSampleRate = state.dvcAudioSampleRate;
    runtime->dvcAudioVolume = state.dvcAudioVolume;
    runtime->framebufferAddress = state.framebufferAddress;
    runtime->framebufferBits = state.framebufferBits;
    memcpy(runtime->framebufferWriteHighWater, state.framebufferWriteHighWater,
        sizeof(runtime->framebufferWriteHighWater));
    runtime->framebufferBitsExplicit = state.framebufferBitsExplicit;
    runtime->cc1800Compatibility = state.cc1800Compatibility;
    runtime->dvcAudioStarted = state.dvcAudioStarted;
    runtime->stats->instructions = state.runtimeInstructions;
    runtime->startTime = std::chrono::steady_clock::now() -
        std::chrono::microseconds((uint64_t)(state.elapsedGuestMicros /
            (s_runtimeSpeedScale.load() > 0 ? s_runtimeSpeedScale.load() : 1.0)));
    runtime->instructionCache.assign(
        runtime->instructionCache.size(), Arm32InstructionCacheEntry());
    runtime->bus.userData = runtime;
    runtime->bus.directSystemRam = runtime->systemMemory.data();
    runtime->bus.directSystemRamBase = kCcHomebrewSystemRamStart;
    runtime->bus.directSystemRamSize =
        (uint32_t)runtime->systemMemory.size();
    runtime->bus.directRam = runtime->ram.data();
    runtime->bus.directRamBase = runtime->ramStart;
    runtime->bus.directRamSize = (uint32_t)runtime->ram.size();
    runtime->bus.directStack = runtime->stack.data();
    runtime->bus.directStackBase = kStackStart;
    runtime->bus.directStackSize = (uint32_t)runtime->stack.size();
    runtime->bus.directHeap = runtime->heapMemory.data();
    runtime->bus.directHeapBase = runtime->heapStart;
    runtime->bus.directHeapSize = runtime->legacySystemMmio.empty() ?
        (uint32_t)runtime->heapMemory.size() :
        kLegacySystemMmioStart - runtime->heapStart;
    runtime->bus.instructionCache = runtime->instructionCache.data();
    runtime->bus.instructionCacheCount =
        (uint32_t)runtime->instructionCache.size();
    printf("cc-arm: save-state restored tasks=%u streams=%u resources=%u\n",
        (uint32_t)runtime->tasks.size(), (uint32_t)runtime->openStreams.size(),
        (uint32_t)runtime->resources.size());
    return true;
}

static uint8_t* resolveMemorySpan(CcArmRuntime* runtime, uint32_t address,
    uint32_t* available)
{
    if (address < runtime->legacyLowMemory.size())
    {
        if (available) *available =
            (uint32_t)runtime->legacyLowMemory.size() - address;
        return runtime->legacyLowMemory.data() + address;
    }
    uint32_t offset = address - kCcHomebrewSystemRamStart;
    if (address >= kCcHomebrewSystemRamStart &&
        offset < runtime->systemMemory.size())
    {
        if (available) *available =
            (uint32_t)runtime->systemMemory.size() - offset;
        return runtime->systemMemory.data() + offset;
    }
    offset = address - runtime->ramStart;
    if (address >= runtime->ramStart && offset < runtime->ram.size())
    {
        if (available) *available = (uint32_t)runtime->ram.size() - offset;
        return runtime->ram.data() + offset;
    }
    offset = address - kStackStart;
    if (address >= kStackStart && offset < kStackSize)
    {
        if (available) *available = kStackSize - offset;
        return runtime->stack.data() + offset;
    }
    offset = address - kLegacySystemMmioStart;
    if (address >= kLegacySystemMmioStart &&
        offset < runtime->legacySystemMmio.size())
    {
        if (available) *available =
            (uint32_t)runtime->legacySystemMmio.size() - offset;
        return runtime->legacySystemMmio.data() + offset;
    }
    offset = address - kLegacyAudioMmioStart;
    if (address >= kLegacyAudioMmioStart &&
        offset < runtime->legacyAudioMmio.size())
    {
        if (available) *available =
            (uint32_t)runtime->legacyAudioMmio.size() - offset;
        return runtime->legacyAudioMmio.data() + offset;
    }
    offset = address - runtime->heapStart;
    if (address >= runtime->heapStart &&
        offset < runtime->heapMemory.size())
    {
        if (available) *available =
            (uint32_t)runtime->heapMemory.size() - offset;
        return runtime->heapMemory.data() + offset;
    }
    offset = address - kFramebufferAddress;
    if (address >= kFramebufferAddress && offset < runtime->framebuffer.size())
    {
        if (available) *available = (uint32_t)runtime->framebuffer.size() - offset;
        return runtime->framebuffer.data() + offset;
    }
    offset = address - kLegacyMmioStart;
    if (address >= kLegacyMmioStart && offset < runtime->legacyMmio.size())
    {
        if (available) *available = (uint32_t)runtime->legacyMmio.size() - offset;
        return runtime->legacyMmio.data() + offset;
    }
    if (available) *available = 0;
    return NULL;
}

static uint8_t* resolveMemory(CcArmRuntime* runtime, uint32_t address, size_t size)
{
    uint32_t available = 0;
    uint8_t* pointer = resolveMemorySpan(runtime, address, &available);
    return pointer && size <= available ? pointer : NULL;
}

static void noteFramebufferWrite(CcArmRuntime* runtime, uint32_t address, size_t size)
{
    if (address < kFramebufferAddress ||
        address >= kFramebufferAddress + kCcFramebufferSize)
    {
        return;
    }
    uint32_t offset = address - kFramebufferAddress;
    size_t remaining = std::min<size_t>(size, kCcFramebufferSize - offset);
    while (remaining)
    {
        uint32_t bufferIndex = offset / kCcFramebufferStride;
        uint32_t bufferOffset = offset % kCcFramebufferStride;
        uint32_t chunkSize = (uint32_t)std::min<size_t>(
            remaining, kCcFramebufferStride - bufferOffset);
        runtime->framebufferWriteHighWater[bufferIndex] = std::max(
            runtime->framebufferWriteHighWater[bufferIndex], bufferOffset + chunkSize);
        offset += chunkSize;
        remaining -= chunkSize;
    }
    trackFramebufferWrite(address, (uint32_t)std::min<size_t>(size, UINT32_MAX));
}

static bool busRead(void* userData, uint32_t address, void* output, size_t size)
{
    CcArmRuntime* runtime = (CcArmRuntime*)userData;
    uint8_t* source = resolveMemory(runtime, address, size);
    if (!source)
    {
        runtime->faultAddress = address;
        runtime->faultSize = (uint32_t)size;
        runtime->faultWrite = false;
        runtime->faultFetch = false;
    }
    if (!source) return false;
    memcpy(output, source, size);
    return true;
}

static bool busFetch(void* userData, uint32_t address, void* output, size_t size)
{
    CcArmRuntime* runtime = (CcArmRuntime*)userData;
    uint32_t programOffset = address - runtime->package->origin;
    bool program = address >= runtime->package->origin &&
        programOffset < runtime->package->prog_size &&
        size <= runtime->package->prog_size - programOffset;
    uint32_t thunkOffset = address - kDynamicThunkStart;
    bool thunk = address >= kDynamicThunkStart && thunkOffset < 0x10000u &&
        size <= 0x10000u - thunkOffset;
    if (!program && !thunk)
    {
        runtime->faultAddress = address;
        runtime->faultSize = (uint32_t)size;
        runtime->faultWrite = false;
        runtime->faultFetch = true;
        return false;
    }
    return busRead(userData, address, output, size);
}

static bool busWrite(void* userData, uint32_t address, const void* input, size_t size)
{
    CcArmRuntime* runtime = (CcArmRuntime*)userData;
    uint8_t* destination = resolveMemory(runtime, address, size);
    if (!destination)
    {
        runtime->faultAddress = address;
        runtime->faultSize = (uint32_t)size;
        runtime->faultWrite = true;
        runtime->faultFetch = false;
    }
    if (!destination) return false;
    memcpy(destination, input, size);
    if (size == sizeof(uint32_t) && address == kLegacyGraphicsStride)
    {
        uint32_t value = 0;
        memcpy(&value, input, sizeof(value));
        if (value == kFramebufferWidth * sizeof(uint16_t) ||
            value == kFramebufferWidth * sizeof(uint32_t))
        {
            runtime->framebufferBits = value * 8u / kFramebufferWidth;
            runtime->framebufferBitsExplicit = true;
        }
    }
    else if (size == sizeof(uint32_t) && address == kLegacyGraphicsSurface)
    {
        presentFramebuffer(runtime, kLegacyFramebufferAddress);
    }
    noteFramebufferWrite(runtime, address, size);
    return true;
}

static bool cheatReadCallback(void* userData, uint32_t address, void* output, size_t size)
{
    return busRead(userData, address, output, size);
}

static bool cheatWriteCallback(void* userData, uint32_t address, const void* input, size_t size)
{
    return busWrite(userData, address, input, size);
}

static void cheatFlushCallback(void* userData)
{
    CcArmRuntime* runtime = (CcArmRuntime*)userData;
    std::fill(runtime->instructionCache.begin(), runtime->instructionCache.end(),
        Arm32InstructionCacheEntry{});
}

static std::string sha256Hex(const uint8_t* data, uint32_t size)
{
    static const char kHex[] = "0123456789ABCDEF";
    uint8_t digest[32];
    sha256_context context;
    sha256_starts(&context);
    sha256_update(&context, data, size);
    sha256_finish(&context, digest);
    std::string output(64, '0');
    for (size_t i = 0; i < sizeof(digest); ++i)
    {
        output[i * 2] = kHex[digest[i] >> 4];
        output[i * 2 + 1] = kHex[digest[i] & 0x0f];
    }
    return output;
}

static void presentFramebuffer(CcArmRuntime* runtime, uint32_t requestedAddress)
{
    uint32_t frameAddress = runtime->framebufferAddress;
    if (resolveMemory(runtime, requestedAddress, kFramebuffer16Size))
    {
        frameAddress = requestedAddress;
    }
    if (!runtime->framebufferBitsExplicit && frameAddress >= kFramebufferAddress &&
        frameAddress - kFramebufferAddress < kCcFramebufferSize &&
        ccPackageUsesHomebrewLayout(runtime->package->origin) &&
        runtime->framebufferWriteHighWater[
            (frameAddress - kFramebufferAddress) / kCcFramebufferStride] >
            kFramebuffer16Size)
    {
        runtime->framebufferBits = 32u;
    }
    uint32_t frameSize = runtime->framebufferBits == 32u ?
        kFramebuffer32Size : kFramebuffer16Size;
    const uint8_t* frame = resolveMemory(runtime, frameAddress, frameSize);
    if (!frame)
    {
        frameAddress = kFramebufferAddress;
        frame = resolveMemory(runtime, frameAddress, frameSize);
    }
    runtime->framebufferAddress = frameAddress;
    uint16_t* output = (uint16_t*)framebufferPixels();
    if (runtime->framebufferBits == 32u)
    {
        const uint32_t* input = (const uint32_t*)frame;
        for (uint32_t i = 0; i < kFramebufferWidth * kFramebufferHeight; ++i)
        {
            uint32_t pixel = input[i];
            uint32_t red = (pixel >> 16) & 0xffu;
            uint32_t green = (pixel >> 8) & 0xffu;
            uint32_t blue = pixel & 0xffu;
            output[i] = (uint16_t)(((red >> 3) << 11) |
                ((green >> 2) << 5) | (blue >> 3));
        }
    }
    else
    {
        memcpy(output, frame, kFramebuffer16Size);
    }
    framebufferRequestUpdate();
    runtime->stats->framesSubmitted++;
}

static bool readGuestString(CcArmRuntime* runtime, uint32_t address,
    char* output, size_t outputSize)
{
    if (!address || !output || outputSize < 2) return false;
    for (size_t i = 0; i < outputSize - 1; ++i)
    {
        uint8_t* value = resolveMemory(runtime, address + (uint32_t)i, 1);
        if (!value) return false;
        output[i] = (char)*value;
        if (!output[i]) return true;
    }
    output[outputSize - 1] = '\0';
    return true;
}

static void appendUtf8(std::string* output, uint32_t codePoint)
{
    if (codePoint <= 0x7fu) output->push_back((char)codePoint);
    else if (codePoint <= 0x7ffu)
    {
        output->push_back((char)(0xc0u | (codePoint >> 6)));
        output->push_back((char)(0x80u | (codePoint & 0x3fu)));
    }
    else if (codePoint <= 0xffffu)
    {
        output->push_back((char)(0xe0u | (codePoint >> 12)));
        output->push_back((char)(0x80u | ((codePoint >> 6) & 0x3fu)));
        output->push_back((char)(0x80u | (codePoint & 0x3fu)));
    }
    else
    {
        output->push_back((char)(0xf0u | (codePoint >> 18)));
        output->push_back((char)(0x80u | ((codePoint >> 12) & 0x3fu)));
        output->push_back((char)(0x80u | ((codePoint >> 6) & 0x3fu)));
        output->push_back((char)(0x80u | (codePoint & 0x3fu)));
    }
}

static bool readGuestWideString(CcArmRuntime* runtime, uint32_t address,
    std::string* output, size_t maxCharacters)
{
    if (!address || !output) return false;
    output->clear();
    for (size_t i = 0; i < maxCharacters; ++i)
    {
        uint16_t value = 0;
        uint8_t* source = resolveMemory(runtime, address + (uint32_t)i * 2u, 2u);
        if (!source) return false;
        memcpy(&value, source, sizeof(value));
        if (!value) return true;
        uint32_t codePoint = value;
        if (value >= 0xd800u && value <= 0xdbffu && i + 1u < maxCharacters)
        {
            uint16_t low = 0;
            source = resolveMemory(runtime, address + (uint32_t)(i + 1u) * 2u, 2u);
            if (!source) return false;
            memcpy(&low, source, sizeof(low));
            if (low >= 0xdc00u && low <= 0xdfffu)
            {
                codePoint = 0x10000u + ((value - 0xd800u) << 10) + low - 0xdc00u;
                ++i;
            }
            else codePoint = 0xfffdu;
        }
        else if (value >= 0xdc00u && value <= 0xdfffu) codePoint = 0xfffdu;
        appendUtf8(output, codePoint);
    }
    return false;
}

static bool writeGuestString(CcArmRuntime* runtime, uint32_t address,
    uint32_t capacity, const char* value)
{
    if (!address || !capacity || !value) return false;
    size_t length = std::min<size_t>(strlen(value), capacity - 1u);
    if (!busWrite(runtime, address, value, length)) return false;
    const uint8_t zero = 0;
    return busWrite(runtime, address + (uint32_t)length, &zero, 1u);
}

static int compareGuestStringsIgnoreCase(CcArmRuntime* runtime,
    uint32_t leftAddress, uint32_t rightAddress)
{
    char left[512] = {};
    char right[512] = {};
    if (!readGuestString(runtime, leftAddress, left, sizeof(left)) ||
        !readGuestString(runtime, rightAddress, right, sizeof(right)))
    {
        return leftAddress == rightAddress ? 0 : (leftAddress < rightAddress ? -1 : 1);
    }
    for (size_t i = 0; ; ++i)
    {
        unsigned char a = (unsigned char)left[i];
        unsigned char b = (unsigned char)right[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + 'a' - 'A');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + 'a' - 'A');
        if (a != b || !a || !b) return (int)a - (int)b;
    }
}

static uint32_t mapInputForRuntime(const CcArmRuntime* runtime, uint32_t input)
{
    if (ccUsesRetailInputMapping(runtime->package->origin))
    {
        return mapInputToRetailLayout(input);
    }
    return mapInputToHomebrewLayout(input);
}

static uint32_t findExport(const GuestPackage* package, const char* name)
{
    for (uint32_t i = 0; i < package->export_count; ++i)
    {
        if (package->export_data[i] && package->export_data[i]->name &&
            strcmp(package->export_data[i]->name, name) == 0)
        {
            return package->export_data[i]->offset;
        }
    }
    return 0;
}

static uint32_t findImport(const GuestPackage* package, const char* name)
{
    for (uint32_t i = 0; i < package->import_count; ++i)
    {
        if (package->import_data[i] && package->import_data[i]->name &&
            strcmp(package->import_data[i]->name, name) == 0)
        {
            return i;
        }
    }
    return UINT32_MAX;
}

static uint32_t allocateMemory(CcArmRuntime* runtime, uint32_t size)
{
    if (!size) return 0;
    uint32_t aligned = (size + 15u) & ~15u;
    if (aligned < size) return 0;
    for (size_t i = 0; i < runtime->heap.size(); ++i)
    {
        if (!runtime->heap[i].free || runtime->heap[i].size < aligned) continue;
        uint32_t address = runtime->heap[i].address;
        uint32_t oldSize = runtime->heap[i].size;
        runtime->heap[i].size = aligned;
        runtime->heap[i].free = false;
        if (oldSize >= aligned + 32u)
        {
            CcArmRuntime::HeapBlock rest = { address + aligned, oldSize - aligned, true };
            runtime->heap.insert(runtime->heap.begin() + i + 1, rest);
        }
        return address;
    }
    uint64_t allocationEnd = (uint64_t)runtime->heapCursor + aligned;
    if (!runtime->legacySystemMmio.empty() &&
        runtime->heapCursor < kLegacySystemMmioStart + kLegacySystemMmioSize &&
        allocationEnd > kLegacySystemMmioStart)
    {
        runtime->heapCursor = kLegacySystemMmioStart + kLegacySystemMmioSize;
    }
    if ((uint64_t)runtime->heapCursor + aligned >
        (uint64_t)runtime->heapStart + runtime->heapMemory.size()) return 0;
    uint32_t address = runtime->heapCursor;
    runtime->heapCursor += aligned;
    runtime->heap.push_back({ address, aligned, false });
    return address;
}

static CcArmRuntime::HeapBlock* findHeapBlock(CcArmRuntime* runtime, uint32_t address)
{
    for (size_t i = 0; i < runtime->heap.size(); ++i)
    {
        if (runtime->heap[i].address == address) return &runtime->heap[i];
    }
    return NULL;
}

static void freeMemory(CcArmRuntime* runtime, uint32_t address)
{
    for (size_t i = 0; i < runtime->heap.size(); ++i)
    {
        if (runtime->heap[i].address != address || runtime->heap[i].free) continue;
        runtime->heap[i].free = true;
        if (i + 1u < runtime->heap.size() && runtime->heap[i + 1u].free &&
            runtime->heap[i].address + runtime->heap[i].size ==
                runtime->heap[i + 1u].address)
        {
            runtime->heap[i].size += runtime->heap[i + 1u].size;
            runtime->heap.erase(runtime->heap.begin() + i + 1u);
        }
        if (i > 0 && runtime->heap[i - 1u].free &&
            runtime->heap[i - 1u].address + runtime->heap[i - 1u].size ==
                runtime->heap[i].address)
        {
            runtime->heap[i - 1u].size += runtime->heap[i].size;
            runtime->heap.erase(runtime->heap.begin() + i);
            --i;
        }
        while (!runtime->heap.empty() && runtime->heap.back().free &&
            runtime->heap.back().address + runtime->heap.back().size ==
                runtime->heapCursor)
        {
            runtime->heapCursor = runtime->heap.back().address;
            runtime->heap.pop_back();
        }
        return;
    }
    if (address) printf("cc-arm: ignored free address=0x%08x\n", address);
}

static CcArmRuntime::ResourceHandle* findResource(CcArmRuntime* runtime,
    uint32_t address)
{
    for (size_t i = 0; i < runtime->resources.size(); ++i)
    {
        if (runtime->resources[i].address == address) return &runtime->resources[i];
    }
    return NULL;
}

static CcArmRuntime::FileHandle* findFile(CcArmRuntime* runtime, uint32_t address)
{
    for (size_t i = 0; i < runtime->files.size(); ++i)
    {
        if (runtime->files[i].address == address) return &runtime->files[i];
    }
    return NULL;
}

static uint32_t fileStream(CcArmRuntime* runtime, uint32_t handle)
{
    CcArmRuntime::FileHandle* file = findFile(runtime, handle);
    return file ? file->stream : handle;
}

static uint32_t openFile(CcArmRuntime* runtime, const char* name, const char* mode)
{
    uint32_t stream = fsys_fopen(name, mode);
    if (!stream) return 0;
    runtime->openStreams.push_back(stream);
    if (ccPackageUsesRetailLayout(runtime->package->origin)) return stream;

    uint32_t address = allocateMemory(runtime, 16u);
    const uint32_t magic = 0x46535953u;
    if (!address || !busWrite(runtime, address, &magic, sizeof(magic)))
    {
        if (address) freeMemory(runtime, address);
        fsys_fclose(stream);
        runtime->openStreams.pop_back();
        return 0;
    }
    runtime->files.push_back({ address, stream });
    return address;
}

static uint32_t closeFile(CcArmRuntime* runtime, uint32_t handle)
{
    uint32_t stream = fileStream(runtime, handle);
    uint32_t result = fsys_fclose(stream);
    runtime->openStreams.erase(std::remove(runtime->openStreams.begin(),
        runtime->openStreams.end(), stream), runtime->openStreams.end());
    for (size_t i = 0; i < runtime->files.size(); ++i)
    {
        if (runtime->files[i].address != handle) continue;
        freeMemory(runtime, handle);
        runtime->files.erase(runtime->files.begin() + i);
        break;
    }
    return result;
}

static CcArmRuntime::Semaphore* findSemaphore(CcArmRuntime* runtime,
    uint32_t address)
{
    for (size_t i = 0; i < runtime->semaphores.size(); ++i)
    {
        if (runtime->semaphores[i].address == address) return &runtime->semaphores[i];
    }
    return NULL;
}

static size_t findActiveTaskIndex(const CcArmRuntime* runtime,
    uint32_t priority)
{
    for (size_t i = 0; i < runtime->tasks.size(); ++i)
    {
        if (!runtime->tasks[i].finished && runtime->tasks[i].priority == priority)
        {
            return i;
        }
    }
    return runtime->tasks.size();
}

static uint32_t createDynamicImport(CcArmRuntime* runtime, const char* name)
{
    for (size_t i = 0; i < runtime->dynamicImports.size(); ++i)
    {
        if (runtime->dynamicImports[i] == name) return kDynamicThunkStart + (uint32_t)i * 8u;
    }
    uint32_t slot = (uint32_t)runtime->dynamicImports.size();
    if (slot >= 0x1ff0u) return 0;
    uint32_t index = runtime->package->import_count + slot;
    uint32_t stub[2] = { 0xef000000u | index, 0xe12fff1eu };
    uint32_t address = kDynamicThunkStart + slot * 8u;
    if (!busWrite(runtime, address, stub, sizeof(stub))) return 0;
    runtime->dynamicImports.push_back(name);
    return address;
}

static void recordUnknownImport(CcArmRuntime* runtime, const char* name)
{
    runtime->stats->unknownImports++;
    std::string value = name ? name : "(invalid)";
    if (std::find(runtime->unknownImportNames.begin(), runtime->unknownImportNames.end(), value) ==
        runtime->unknownImportNames.end())
    {
        runtime->unknownImportNames.push_back(value);
        printf("cc-arm: unknown import %s\n", value.c_str());
    }
}

static bool runTransparentBlit16(CcArmRuntime* runtime,
    uint32_t objectAddress, uint32_t blendMode)
{
    uint8_t* object = resolveMemory(runtime, objectAddress, 0x4cu);
    if (!object) return false;
    uint32_t destinationStride = 0;
    uint32_t left = 0;
    uint32_t top = 0;
    uint32_t right = 0;
    uint32_t bottom = 0;
    uint32_t sourceAddress = 0;
    uint32_t destinationAddress = 0;
    uint32_t sourceInfoAddress = 0;
    uint16_t transparent = 0;
    memcpy(&destinationStride, object + 0x00, sizeof(destinationStride));
    memcpy(&sourceInfoAddress, object + 0x1c, sizeof(sourceInfoAddress));
    memcpy(&transparent, object + 0x28, sizeof(transparent));
    memcpy(&sourceAddress, object + 0x2c, sizeof(sourceAddress));
    memcpy(&destinationAddress, object + 0x30, sizeof(destinationAddress));
    memcpy(&left, object + 0x3c, sizeof(left));
    memcpy(&top, object + 0x40, sizeof(top));
    memcpy(&right, object + 0x44, sizeof(right));
    memcpy(&bottom, object + 0x48, sizeof(bottom));
    uint16_t* sourceInfo = (uint16_t*)resolveMemory(runtime, sourceInfoAddress, sizeof(uint16_t));
    if (!sourceInfo || right < left || bottom < top) return false;
    uint32_t width = right - left;
    uint32_t height = bottom - top;
    uint32_t sourceStride = *sourceInfo;
    if (!height)
    {
        return true;
    }
    if (width > 2048u || height > 2048u || sourceStride < width ||
        destinationStride < width)
    {
        return false;
    }
    uint64_t sourcePixels = (uint64_t)(height - 1u) * sourceStride + width;
    uint64_t destinationPixels = (uint64_t)(height - 1u) * destinationStride + width;
    uint64_t sourceBytes = sourcePixels * sizeof(uint16_t);
    uint64_t destinationBytes = destinationPixels * sizeof(uint16_t);
    uint64_t sourceAdvance = (uint64_t)height * sourceStride * sizeof(uint16_t);
    uint64_t destinationAdvance =
        (uint64_t)height * destinationStride * sizeof(uint16_t);
    if (sourceBytes > UINT32_MAX || destinationBytes > UINT32_MAX ||
        sourceAdvance > UINT32_MAX || destinationAdvance > UINT32_MAX)
    {
        return false;
    }
    const uint16_t* source = (const uint16_t*)resolveMemory(runtime,
        sourceAddress, (uint32_t)sourceBytes);
    uint16_t* destination = (uint16_t*)resolveMemory(runtime,
        destinationAddress, (uint32_t)destinationBytes);
    if (!source || !destination) return false;
    ccBlitTransparentBlend16(destination, destinationStride, source,
        sourceStride, width, height, transparent, blendMode);
    sourceAddress += (uint32_t)sourceAdvance;
    destinationAddress += (uint32_t)destinationAdvance;
    memcpy(object + 0x2c, &sourceAddress, sizeof(sourceAddress));
    memcpy(object + 0x30, &destinationAddress, sizeof(destinationAddress));
    return true;
}

static bool readScaledBlit16(CcArmRuntime* runtime, uint32_t objectAddress,
    uint32_t* destinationStride, uint32_t* sourceInfoAddress,
    uint32_t* destinationAddress, uint32_t* sourceLeft, uint32_t* sourceTop,
    uint32_t* left, uint32_t* top, uint32_t* right, uint32_t* bottom,
    uint32_t* sourceXStep, uint32_t* sourceYStep)
{
    uint8_t* object = resolveMemory(runtime, objectAddress, 0x64u);
    if (!object) return false;
    memcpy(destinationStride, object + 0x00, sizeof(*destinationStride));
    memcpy(sourceInfoAddress, object + 0x1c, sizeof(*sourceInfoAddress));
    memcpy(destinationAddress, object + 0x30, sizeof(*destinationAddress));
    memcpy(sourceLeft, object + 0x3c, sizeof(*sourceLeft));
    memcpy(sourceTop, object + 0x40, sizeof(*sourceTop));
    memcpy(left, object + 0x4c, sizeof(*left));
    memcpy(top, object + 0x50, sizeof(*top));
    memcpy(right, object + 0x54, sizeof(*right));
    memcpy(bottom, object + 0x58, sizeof(*bottom));
    memcpy(sourceXStep, object + 0x5c, sizeof(*sourceXStep));
    memcpy(sourceYStep, object + 0x60, sizeof(*sourceYStep));
    return true;
}

static bool validateScaledBlit16(uint32_t destinationStride,
    uint32_t sourceStride, uint32_t sourceLeft, uint32_t sourceTop,
    uint32_t sourceXStep, uint32_t sourceYStep, uint32_t width,
    uint32_t height)
{
    if (!width || !height || width > 2048u || height > 2048u ||
        destinationStride < width || !sourceStride)
    {
        return false;
    }
    uint64_t lastX = sourceLeft +
        (((uint64_t)(width - 1u) * sourceXStep) >> 16);
    uint64_t lastY = sourceTop +
        (((uint64_t)(height - 1u) * sourceYStep) >> 16);
    return lastX < sourceStride && lastY < 2048u;
}

static bool runScaledBlit16(CcArmRuntime* runtime, uint32_t objectAddress,
    bool indexed, bool useTransparent)
{
    uint32_t destinationStride = 0;
    uint32_t sourceInfoAddress = 0;
    uint32_t destinationAddress = 0;
    uint32_t sourceLeft = 0;
    uint32_t sourceTop = 0;
    uint32_t left = 0;
    uint32_t top = 0;
    uint32_t right = 0;
    uint32_t bottom = 0;
    uint32_t sourceXStep = 0;
    uint32_t sourceYStep = 0;
    uint16_t transparent = 0;
    if (!readScaledBlit16(runtime, objectAddress, &destinationStride,
            &sourceInfoAddress, &destinationAddress, &sourceLeft, &sourceTop,
            &left, &top, &right, &bottom, &sourceXStep, &sourceYStep) ||
        right < left || bottom < top)
    {
        return false;
    }

    uint8_t* object = resolveMemory(runtime, objectAddress, 0x64u);
    if (!object) return false;
    memcpy(&transparent, object + 0x28, sizeof(transparent));
    uint8_t* sourceInfo = resolveMemory(runtime, sourceInfoAddress, 0x18u);
    if (!sourceInfo) return false;
    uint16_t sourceStride = 0;
    uint16_t sourceHeight = 0;
    uint32_t sourceBase = 0;
    memcpy(&sourceStride, sourceInfo, sizeof(sourceStride));
    memcpy(&sourceHeight, sourceInfo + sizeof(sourceStride),
        sizeof(sourceHeight));
    memcpy(&sourceBase, sourceInfo + 0x14, sizeof(sourceBase));
    uint32_t width = right - left;
    uint32_t height = bottom - top;
    if (sourceLeft <= sourceStride && sourceTop <= sourceHeight)
    {
        sourceXStep = ccNormalizeScaledStep(sourceXStep,
            sourceStride - sourceLeft, width);
        sourceYStep = ccNormalizeScaledStep(sourceYStep,
            sourceHeight - sourceTop, height);
    }
    if (!validateScaledBlit16(destinationStride, sourceStride, sourceLeft,
            sourceTop, sourceXStep, sourceYStep, width, height))
    {
        return false;
    }

    uint32_t sourceAddressOffset = indexed ? 0x38u : 0x2cu;
    uint32_t sourceAddress = 0;
    uint8_t* sourcePointer = resolveMemory(runtime,
        objectAddress + sourceAddressOffset, sizeof(sourceAddress));
    uint8_t* destinationPointer = resolveMemory(runtime,
        objectAddress + 0x30u, sizeof(destinationAddress));
    if (!sourcePointer || !destinationPointer) return false;
    memcpy(&sourceAddress, sourcePointer, sizeof(sourceAddress));

    const uint16_t* palette = NULL;
    if (indexed)
    {
        uint32_t paletteAddress = 0;
        uint8_t* palettePointer = resolveMemory(runtime,
            objectAddress + 0x2cu, sizeof(paletteAddress));
        if (!palettePointer) return false;
        memcpy(&paletteAddress, palettePointer, sizeof(paletteAddress));
        palette = (const uint16_t*)resolveMemory(runtime,
            paletteAddress, 256u * sizeof(uint16_t));
        if (!palette) return false;
    }

    uint64_t lastSample =
        ((uint64_t)(width - 1u) * sourceXStep) >> 16;
    uint32_t sourceY = sourceTop << 16;
    for (uint32_t y = 0; y < height; ++y)
    {
        uint16_t* destination = (uint16_t*)resolveMemory(runtime,
            destinationAddress, width * sizeof(uint16_t));
        uint32_t sourceElementSize = indexed ? 1u : sizeof(uint16_t);
        uint8_t* source = resolveMemory(runtime, sourceAddress,
            (uint32_t)(lastSample + 1u) * sourceElementSize);
        if (!destination || !source) return false;
        uint32_t sourceX = 0;
        for (uint32_t x = 0; x < width; ++x)
        {
            uint32_t sample = sourceX >> 16;
            uint16_t pixel = indexed ? palette[source[sample]] :
                ((const uint16_t*)source)[sample];
            if (!useTransparent || pixel != transparent)
            {
                destination[x] = pixel;
            }
            sourceX += sourceXStep;
        }
        destinationAddress += destinationStride * sizeof(uint16_t);
        sourceY += sourceYStep;
        uint64_t rowOffset = (uint64_t)(sourceY >> 16) * sourceStride +
            sourceLeft;
        uint64_t nextSourceAddress = sourceBase + rowOffset * sourceElementSize;
        if (nextSourceAddress > UINT32_MAX) return false;
        sourceAddress = (uint32_t)nextSourceAddress;
    }
    memcpy(sourcePointer, &sourceAddress, sizeof(sourceAddress));
    memcpy(destinationPointer, &destinationAddress, sizeof(destinationAddress));
    return true;
}

static bool readGuestU32(CcArmRuntime* runtime, uint32_t address, uint32_t* value)
{
    uint8_t* source = resolveMemory(runtime, address, sizeof(*value));
    if (!source || !value) return false;
    memcpy(value, source, sizeof(*value));
    return true;
}

static bool writeGuestU32(CcArmRuntime* runtime, uint32_t address, uint32_t value)
{
    return busWrite(runtime, address, &value, sizeof(value));
}

static uint32_t runFastCopy(CcArmRuntime* runtime, uint32_t destinationAddress,
    uint32_t sourceAddress, uint32_t size)
{
    uint8_t* destination = resolveMemory(runtime, destinationAddress, size ? size : 1u);
    uint8_t* source = resolveMemory(runtime, sourceAddress, size ? size : 1u);
    if (!destination || !source) return 0;
    memmove(destination, source, size);
    return destinationAddress;
}

static uint32_t runFastFill(CcArmRuntime* runtime, uint32_t destinationAddress,
    uint32_t size, uint8_t value)
{
    uint8_t* destination = resolveMemory(runtime, destinationAddress, size ? size : 1u);
    if (!destination) return 0;
    memset(destination, value, size);
    return destinationAddress;
}

static const char kFill32PairsImport[] = "cc_internal_fill32_pairs";
static const char kAdpcmDecodeTailImport[] = "cc_internal_adpcm_decode_tail";
static const char kRowFill32PairsImport[] = "cc_internal_row_fill32_pairs";

static bool runCc1800Fill32Pairs(CcArmRuntime* runtime, Arm32State* state)
{
    uint32_t pairs = state->r[1];
    if (pairs > UINT32_MAX / 8u) return false;
    uint32_t byteCount = pairs * 8u;
    uint32_t destinationAddress = state->r[0] + 4u;
    uint8_t* destination = resolveMemory(runtime, destinationAddress,
        byteCount ? byteCount : 1u);
    if (!destination)
    {
        runtime->faultAddress = destinationAddress;
        runtime->faultSize = byteCount;
        runtime->faultWrite = true;
        runtime->faultFetch = false;
        return false;
    }
    uint32_t* output = (uint32_t*)destination;
    std::fill(output, output + pairs * 2u, state->r[4]);
    state->r[0] += byteCount;
    state->r[1] = 0;
    state->r[15] = 0x10166270u;
    return true;
}

static bool runCc1800AdpcmDecodeTail(CcArmRuntime* runtime, Arm32State* state)
{
    uint32_t inputAddress = state->r[3];
    uint32_t inputEnd = state->r[10];
    if (inputAddress >= inputEnd)
    {
        state->r[15] = 0x10167644u;
        return true;
    }
    uint32_t inputSize = inputEnd - inputAddress;
    bool highNibble = (state->r[2] & 4u) != 0;
    uint64_t sampleCount64 = (uint64_t)inputSize * 2u -
        (highNibble ? 1u : 0u);
    if (sampleCount64 > UINT32_MAX / sizeof(uint16_t)) return false;
    uint32_t sampleCount = (uint32_t)sampleCount64;
    uint8_t* input = resolveMemory(runtime, inputAddress, inputSize);
    uint8_t* outputBytes = resolveMemory(runtime, state->r[12],
        (size_t)sampleCount * sizeof(uint16_t));
    uint8_t* stepBytes = resolveMemory(runtime, state->r[11], 89u * 4u);
    uint8_t* indexBytes = resolveMemory(runtime, state->r[7], 8u * 4u);
    if (!input || !outputBytes || !stepBytes || !indexBytes)
    {
        runtime->faultAddress = !input ? inputAddress : state->r[12];
        runtime->faultSize = !input ? inputSize : sampleCount * sizeof(uint16_t);
        runtime->faultWrite = input != NULL;
        runtime->faultFetch = false;
        return false;
    }

    int32_t predictor = (int32_t)state->r[0];
    int32_t index = (int32_t)state->r[1];
    uint16_t* output = (uint16_t*)outputBytes;
    uint32_t produced = 0;
    uint32_t sourceOffset = 0;
    while (sourceOffset < inputSize && produced < sampleCount)
    {
        uint32_t nibble = (input[sourceOffset] >> (highNibble ? 4u : 0u)) & 0x0fu;
        int32_t step = 0;
        int32_t indexDelta = 0;
        memcpy(&step, stepBytes + (uint32_t)index * 4u, sizeof(step));
        memcpy(&indexDelta, indexBytes + (nibble & 7u) * 4u,
            sizeof(indexDelta));
        int32_t difference = ((step * (int32_t)(nibble & 7u)) >> 2) +
            (step >> 3);
        index = std::max<int32_t>(0, std::min<int32_t>(88,
            index + indexDelta));
        predictor += (nibble & 8u) ? -difference : difference;
        predictor = std::max<int32_t>(-32768,
            std::min<int32_t>(32767, predictor));
        output[produced++] = (uint16_t)predictor;
        if (highNibble)
        {
            ++sourceOffset;
            highNibble = false;
        }
        else
        {
            highNibble = true;
        }
    }
    state->r[0] = (uint32_t)predictor;
    state->r[1] = (uint32_t)index;
    state->r[3] = inputEnd;
    state->r[12] += produced * sizeof(uint16_t);
    state->r[15] = 0x10167644u;
    return true;
}

static bool runCc1800RowFill32Pairs(CcArmRuntime* runtime, Arm32State* state)
{
    if (state->r[1] > state->r[12]) return false;
    uint32_t pixelCount = state->r[12] - state->r[1];
    if (pixelCount & 1u) return false;
    uint32_t destinationBase = 0;
    uint32_t colorHigh = 0;
    uint16_t colorLow = 0;
    if (!readGuestU32(runtime, state->r[0] + 0x20u, &destinationBase) ||
        !readGuestU32(runtime, state->r[0] + 0x58u, &colorHigh))
    {
        return false;
    }
    uint8_t* lowBytes = resolveMemory(runtime, state->r[0] + 0x54u,
        sizeof(colorLow));
    uint64_t destinationAddress64 = (uint64_t)destinationBase +
        (uint64_t)state->r[2] * 4u;
    uint64_t byteCount64 = (uint64_t)pixelCount * 4u;
    if (!lowBytes || destinationAddress64 > UINT32_MAX ||
        byteCount64 > UINT32_MAX)
    {
        return false;
    }
    memcpy(&colorLow, lowBytes, sizeof(colorLow));
    uint32_t destinationAddress = (uint32_t)destinationAddress64;
    uint32_t byteCount = (uint32_t)byteCount64;
    uint8_t* destination = resolveMemory(runtime, destinationAddress,
        byteCount ? byteCount : 1u);
    if (!destination)
    {
        runtime->faultAddress = destinationAddress;
        runtime->faultSize = byteCount;
        runtime->faultWrite = true;
        runtime->faultFetch = false;
        return false;
    }
    uint32_t color = colorHigh | colorLow;
    uint32_t* output = (uint32_t*)destination;
    std::fill(output, output + pixelCount, color);
    state->r[1] += pixelCount;
    state->r[2] += pixelCount;
    state->r[15] = 0x10157be0u;
    return true;
}

static uint32_t runFastFrameCopy(CcArmRuntime* runtime, const Arm32State* state)
{
    uint32_t width = 0;
    uint32_t height = 0;
    if (!readGuestU32(runtime, state->r[0] + 4u, &width) ||
        !readGuestU32(runtime, state->r[0] + 8u, &height))
    {
        return 0;
    }
    uint64_t requested = (uint64_t)width * height;
    uint32_t count = (uint32_t)std::min<uint64_t>(requested,
        (uint64_t)kFramebufferWidth * kFramebufferHeight);
    uint8_t* source = resolveMemory(runtime, state->r[1], count * 4u);
    uint8_t* destination = resolveMemory(runtime, state->r[2], count * 2u);
    if (!source || !destination) return 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        destination[i * 2u] = source[i * 4u];
        destination[i * 2u + 1u] = source[i * 4u + 1u];
    }
    return state->r[2] + count * 2u;
}

static uint32_t runNormalizeVector3Fixed(CcArmRuntime* runtime,
    uint32_t destinationAddress, uint32_t sourceAddress)
{
    int32_t* source = (int32_t*)resolveMemory(runtime, sourceAddress,
        3u * sizeof(int32_t));
    int32_t* destination = (int32_t*)resolveMemory(runtime,
        destinationAddress, 3u * sizeof(int32_t));
    if (!source || !destination) return 0;
    int32_t input[3] = { source[0], source[1], source[2] };
    return ccNormalizeVector3Fixed(destination, input) ?
        destinationAddress : 0;
}

static uint32_t runFastIndexedBlit(CcArmRuntime* runtime, uint32_t context,
    bool scaled, bool transparent)
{
    uint32_t left = 0;
    uint32_t top = 0;
    uint32_t right = 0;
    uint32_t bottom = 0;
    uint32_t image = 0;
    uint32_t sourceRow = 0;
    uint32_t destinationRow = 0;
    uint32_t destinationStride = 0;
    uint32_t flags = 0;
    uint32_t paletteAddress = 0;
    uint32_t sourceStride = 0;
    if (!readGuestU32(runtime, context, &destinationStride) ||
        !readGuestU32(runtime, context + 0x10u, &image) ||
        !readGuestU32(runtime, context + 0x20u, &destinationRow) ||
        !readGuestU32(runtime, context + 0x28u, &sourceRow) ||
        !readGuestU32(runtime, context + (scaled ? 0x3cu : 0x2cu), &left) ||
        !readGuestU32(runtime, context + (scaled ? 0x40u : 0x30u), &top) ||
        !readGuestU32(runtime, context + (scaled ? 0x44u : 0x34u), &right) ||
        !readGuestU32(runtime, context + (scaled ? 0x48u : 0x38u), &bottom) ||
        !readGuestU32(runtime, context + 0x58u, &flags) ||
        !readGuestU32(runtime, image + 0x28u, &sourceStride) ||
        !readGuestU32(runtime, image + 0x54u, &paletteAddress))
    {
        return context;
    }
    int32_t width = (int32_t)right - (int32_t)left;
    int32_t height = (int32_t)bottom - (int32_t)top;
    uint8_t* palette = resolveMemory(runtime, paletteAddress, 512u);
    if (width <= 0 || height <= 0 || width > (int32_t)kFramebufferWidth ||
        height > (int32_t)kFramebufferHeight || !palette || !sourceStride ||
        destinationStride < (uint32_t)width)
    {
        return context;
    }

    uint32_t sourceBase = 0;
    uint32_t sourceX = 0;
    uint32_t xStep = 0x10000u;
    uint32_t yStep = 0x10000u;
    uint32_t yFixed = top << 16;
    if (scaled &&
        (!readGuestU32(runtime, image + 0x4cu, &sourceBase) ||
         !readGuestU32(runtime, context + 0x2cu, &sourceX) ||
         !readGuestU32(runtime, context + 0x4cu, &xStep) ||
         !readGuestU32(runtime, context + 0x50u, &yStep)))
    {
        return context;
    }
    uint8_t transparentIndex = 0;
    if (transparent)
    {
        uint8_t* value = resolveMemory(runtime, context + 0x19u, 1u);
        if (!value) return context;
        transparentIndex = *value;
    }

    for (int32_t y = 0; y < height; ++y)
    {
        uint8_t* source = resolveMemory(runtime, sourceRow, sourceStride);
        uint8_t* destination = resolveMemory(runtime, destinationRow,
            (uint32_t)width * 4u);
        if (!source || !destination) return context;
        uint32_t xFixed = 0;
        for (int32_t x = 0; x < width; ++x)
        {
            uint32_t sourceIndex = scaled ? xFixed >> 16 : (uint32_t)x;
            if (sourceIndex >= sourceStride) return context;
            uint8_t paletteIndex = source[sourceIndex];
            if (!transparent || paletteIndex != transparentIndex)
            {
                uint32_t paletteOffset = (uint32_t)paletteIndex * 2u;
                uint32_t color = flags | (uint32_t)palette[paletteOffset] |
                    ((uint32_t)palette[paletteOffset + 1u] << 8);
                memcpy(destination + (uint32_t)x * 4u, &color, sizeof(color));
            }
            xFixed += xStep;
        }
        destinationRow += destinationStride * 4u;
        if (scaled)
        {
            yFixed += yStep;
            sourceRow = sourceBase + (yFixed >> 16) * sourceStride + sourceX;
        }
        else
        {
            sourceRow += sourceStride;
        }
    }
    writeGuestU32(runtime, context + 0x20u, destinationRow);
    writeGuestU32(runtime, context + 0x28u, sourceRow);
    return context;
}

static uint32_t runSoft3dOpaqueScanlines(CcArmRuntime* runtime,
    uint32_t rendererAddress, uint32_t spanAddress, bool paletteFromSpan,
    bool transparent)
{
    static const uint32_t kSpanSentinel = 0xfffe7961u;
    uint32_t context = rendererAddress + 0x1000u;
    uint32_t image = 0;
    uint32_t textureBase = 0;
    uint32_t paletteBase = 0;
    uint32_t maskU = 0;
    uint32_t maskV = 0;
    uint32_t textureShift = 0;
    uint32_t depth = 0;
    uint32_t depthAccumulatorStep = 0;
    uint32_t pixelDepthStep = 0;
    uint32_t edge = 0;
    uint32_t edgeStep = 0;
    uint32_t depthCorrection = 0;
    uint32_t negativeEdgeBase = 0;
    uint32_t uStep = 0;
    uint32_t vStep = 0;
    if (!readGuestU32(runtime, context + 0xa6cu, &image) ||
        !readGuestU32(runtime, image + 0x4cu, &textureBase) ||
        !readGuestU32(runtime, image + 0x54u, &paletteBase) ||
        !readGuestU32(runtime, context + 0xdb4u, &maskV) ||
        !readGuestU32(runtime, context + 0xdb8u, &maskU) ||
        !readGuestU32(runtime, context + 0xdbcu, &textureShift) ||
        !readGuestU32(runtime, context + 0xdc0u, &edge) ||
        !readGuestU32(runtime, context + 0xdc4u, &depth) ||
        !readGuestU32(runtime, context + 0xdc8u, &depthAccumulatorStep) ||
        !readGuestU32(runtime, context + 0xdccu, &depthCorrection) ||
        !readGuestU32(runtime, context + 0xdd0u, &negativeEdgeBase) ||
        !readGuestU32(runtime, context + 0xdd4u, &edgeStep) ||
        !readGuestU32(runtime, context + 0xdd8u, &pixelDepthStep) ||
        !readGuestU32(runtime, context + 0xddcu, &uStep) ||
        !readGuestU32(runtime, context + 0xde0u, &vStep))
    {
        return rendererAddress;
    }
    if (textureShift >= 32u) return rendererAddress;
    uint32_t textureAvailable = 0;
    uint32_t paletteAvailable = 0;
    uint8_t* texture = resolveMemorySpan(runtime, textureBase, &textureAvailable);
    uint8_t* palette = resolveMemorySpan(runtime, paletteBase, &paletteAvailable);
    uint8_t transparentIndex = 0;
    if (transparent)
    {
        uint8_t* value = resolveMemory(runtime, context + 0xe50u, 1u);
        if (!value) return rendererAddress;
        transparentIndex = *value;
    }

    for (uint32_t span = 0; span < 4096u; ++span, spanAddress += 24u)
    {
        uint32_t left = 0;
        if (!readGuestU32(runtime, spanAddress, &left)) break;
        if (left == kSpanSentinel) break;

        uint32_t previousEdge = edge;
        uint32_t accumulator = depth + depthAccumulatorStep;
        depth = accumulator;
        if ((int32_t)accumulator < 0)
        {
            edge = negativeEdgeBase + previousEdge;
        }
        else
        {
            edge = previousEdge + edgeStep;
            depth = accumulator - depthCorrection;
        }
        writeGuestU32(runtime, context + 0xdc0u, edge);
        writeGuestU32(runtime, context + 0xdc4u, depth);

        int32_t width = (int32_t)previousEdge - (int32_t)left;
        if (width <= 0) continue;
        if (width > 2048) return rendererAddress;

        uint32_t destinationAddress = 0;
        uint32_t interpolatedDepth = 0;
        uint32_t u = 0;
        uint32_t v = 0;
        uint32_t paletteValue = 15u << 8;
        if (!readGuestU32(runtime, spanAddress + 4u, &destinationAddress) ||
            !readGuestU32(runtime, spanAddress + 8u, &interpolatedDepth) ||
            !readGuestU32(runtime, spanAddress + 12u, &u) ||
            !readGuestU32(runtime, spanAddress + 16u, &v) ||
            (paletteFromSpan &&
             !readGuestU32(runtime, spanAddress + 20u, &paletteValue)))
        {
            return rendererAddress;
        }
        uint32_t* destination = (uint32_t*)resolveMemory(runtime,
            destinationAddress, (uint32_t)width * sizeof(uint32_t));
        if (!destination) return rendererAddress;
        for (int32_t x = 0; x < width; ++x)
        {
            uint32_t packedDepth = (interpolatedDepth << 8) & 0xffff00ffu;
            if (packedDepth >= destination[x])
            {
                uint32_t textureOffset = ((maskU & v) << textureShift) +
                    (maskV & u);
                uint32_t textureOffsetBytes = textureOffset >> 16;
                uint8_t* texel = texture &&
                    textureOffsetBytes < textureAvailable ?
                    texture + textureOffsetBytes :
                    resolveMemory(runtime, textureBase + textureOffsetBytes, 1u);
                if (!texel) return rendererAddress;
                if (transparent && *texel == transparentIndex)
                {
                    interpolatedDepth += pixelDepthStep;
                    u += uStep;
                    v += vStep;
                    continue;
                }
                uint32_t paletteIndex =
                    (uint32_t)((int32_t)paletteValue >> 8) |
                    ((uint32_t)*texel << 5);
                uint32_t paletteOffset = paletteIndex * 2u;
                uint16_t* color = palette && paletteAvailable >= sizeof(uint16_t) &&
                    paletteOffset <= paletteAvailable - sizeof(uint16_t) ?
                    (uint16_t*)(palette + paletteOffset) :
                    (uint16_t*)resolveMemory(runtime,
                        paletteBase + paletteOffset, sizeof(uint16_t));
                if (!color) return rendererAddress;
                destination[x] = packedDepth | *color;
            }
            interpolatedDepth += pixelDepthStep;
            u += uStep;
            v += vStep;
        }
    }
    return rendererAddress;
}

static void saveArmCalleeSaved(const Arm32State* state, uint32_t* saved)
{
    memcpy(saved, state->r + 4u, 8u * sizeof(uint32_t));
}

static void restoreArmCalleeSaved(Arm32State* state, const uint32_t* saved)
{
    memcpy(state->r + 4u, saved, 8u * sizeof(uint32_t));
}

static uint32_t runSoft3dFill32(CcArmRuntime* runtime, const Arm32State* state)
{
    int32_t count = (int32_t)state->r[3];
    if (count <= 0 || count > 4096) return state->r[0];
    uint32_t stride = 0;
    uint32_t surface = 0;
    uint32_t pixels = 0;
    uint32_t color = 0;
    if (!readGuestU32(runtime, state->r[0] + 0x220u, &stride) ||
        !readGuestU32(runtime, state->r[0] + 0x1a68u, &surface) ||
        !readGuestU32(runtime, surface + 0x4cu, &pixels) ||
        !readGuestU32(runtime, state->r[13], &color))
    {
        return state->r[0];
    }
    uint64_t pixelOffset = (uint64_t)state->r[2] * stride + state->r[1];
    if (pixelOffset > UINT32_MAX / 4u) return state->r[0];
    uint32_t* destination = (uint32_t*)resolveMemory(runtime,
        pixels + (uint32_t)pixelOffset * 4u, (uint32_t)count * sizeof(uint32_t));
    if (!destination) return state->r[0];
    std::fill(destination, destination + count, color);
    return state->r[0];
}

static uint32_t runFastIndexedAlphaBlit(CcArmRuntime* runtime, uint32_t context)
{
    uint32_t left = 0;
    uint32_t top = 0;
    uint32_t right = 0;
    uint32_t bottom = 0;
    uint32_t image = 0;
    uint32_t sourceRow = 0;
    uint32_t alphaRow = 0;
    uint32_t destinationRow = 0;
    uint32_t sourceStride = 0;
    uint32_t destinationStride = 0;
    uint32_t paletteAddress = 0;
    uint32_t flags = 0;
    if (!readGuestU32(runtime, context, &destinationStride) ||
        !readGuestU32(runtime, context + 0x10u, &image) ||
        !readGuestU32(runtime, context + 0x20u, &destinationRow) ||
        !readGuestU32(runtime, context + 0x24u, &alphaRow) ||
        !readGuestU32(runtime, context + 0x28u, &sourceRow) ||
        !readGuestU32(runtime, context + 0x2cu, &left) ||
        !readGuestU32(runtime, context + 0x30u, &top) ||
        !readGuestU32(runtime, context + 0x34u, &right) ||
        !readGuestU32(runtime, context + 0x38u, &bottom) ||
        !readGuestU32(runtime, context + 0x58u, &flags) ||
        !readGuestU32(runtime, image + 0x28u, &sourceStride) ||
        !readGuestU32(runtime, image + 0x54u, &paletteAddress))
    {
        return context;
    }
    int32_t width = (int32_t)right - (int32_t)left;
    int32_t height = (int32_t)bottom - (int32_t)top;
    uint8_t* palette = resolveMemory(runtime, paletteAddress, 512u);
    if (width <= 0 || height <= 0 || width > (int32_t)kFramebufferWidth ||
        height > (int32_t)kFramebufferHeight || !sourceStride || !palette ||
        destinationStride < (uint32_t)width)
    {
        return context;
    }
    for (int32_t y = 0; y < height; ++y)
    {
        uint8_t* source = resolveMemory(runtime, sourceRow, (uint32_t)width);
        uint8_t* alpha = resolveMemory(runtime, alphaRow, (uint32_t)width);
        uint32_t* destination = (uint32_t*)resolveMemory(runtime, destinationRow,
            (uint32_t)width * sizeof(uint32_t));
        if (!source || !alpha || !destination) return context;
        for (int32_t x = 0; x < width; ++x)
        {
            uint32_t opacity = alpha[x];
            if (opacity <= 1u) continue;
            uint32_t paletteOffset = (uint32_t)source[x] * 2u;
            uint16_t sourceColor = (uint16_t)palette[paletteOffset] |
                ((uint16_t)palette[paletteOffset + 1u] << 8);
            uint32_t packed = destination[x] | flags;
            if (opacity >= 31u)
            {
                destination[x] = (packed & 0xffff0000u) | sourceColor;
                continue;
            }
            uint16_t destinationColor = (uint16_t)packed;
            uint32_t sourceExpanded = ((sourceColor & 0xf800u) << 10) |
                ((sourceColor & 0x07e0u) << 5) | (sourceColor & 0x001fu);
            uint32_t destinationExpanded = ((destinationColor & 0xf800u) << 10) |
                ((destinationColor & 0x07e0u) << 5) | (destinationColor & 0x001fu);
            uint32_t blend = (sourceExpanded - destinationExpanded) * opacity +
                (destinationExpanded << 5);
            uint16_t color = (uint16_t)(((blend & 0x7c000000u) >> 15) |
                ((blend & 0x001f8000u) >> 10) | ((blend & 0x000003e0u) >> 5));
            destination[x] = (packed & 0xffff0000u) | color;
        }
        destinationRow += destinationStride * 4u;
        sourceRow += sourceStride;
        alphaRow += sourceStride;
    }
    writeGuestU32(runtime, context + 0x20u, destinationRow);
    writeGuestU32(runtime, context + 0x24u, alphaRow);
    writeGuestU32(runtime, context + 0x28u, sourceRow);
    return context;
}

static uint32_t runCc1800TransitionBlend(CcArmRuntime* runtime,
    const Arm32State* state)
{
    const uint32_t object = state->r[0];
    uint32_t active = 0;
    if (!readGuestU32(runtime, object + 0x20u, &active) || !active)
    {
        return 0;
    }

    uint32_t current = 0;
    uint32_t duration = 0;
    if (!readGuestU32(runtime, object + 0x18u, &current) ||
        !readGuestU32(runtime, object + 0x1cu, &duration))
    {
        return object;
    }
    if (current != duration)
    {
        ++current;
        writeGuestU32(runtime, object + 0x18u, current);
    }
    else
    {
        writeGuestU32(runtime, object + 0x24u, 1u);
        uint32_t stopWhenComplete = 0;
        if (!readGuestU32(runtime, object + 0x30u, &stopWhenComplete))
        {
            return object;
        }
        if (stopWhenComplete)
        {
            writeGuestU32(runtime, object + 0x20u, 0u);
            uint32_t direction = 0;
            if (!readGuestU32(runtime, object + 0x28u, &direction))
            {
                return object;
            }
            if (!direction)
            {
                uint32_t owner = 0;
                uint32_t globals = 0;
                uint32_t selected = 0;
                if (readGuestU32(runtime, object + 0x14u, &owner) &&
                    readGuestU32(runtime, owner + 0x1a374u, &globals))
                {
                    uint8_t* mode = resolveMemory(runtime,
                        globals + 0x1e4fu, 1u);
                    uint32_t selectionOffset = mode && *mode ? 0x1a84u : 0x1a80u;
                    if (mode && readGuestU32(runtime, globals + selectionOffset,
                            &selected))
                    {
                        writeGuestU32(runtime, globals + 0x1a7cu, selected);
                        writeGuestU32(runtime, selected + 0x10u, 0u);
                    }
                }
            }
        }
    }

    uint32_t direction = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t background = 0;
    if (!duration ||
        !readGuestU32(runtime, object + 0x28u, &direction) ||
        !readGuestU32(runtime, object + 0x04u, &width) ||
        !readGuestU32(runtime, object + 0x08u, &height) ||
        !readGuestU32(runtime, object + 0x2cu, &background))
    {
        return object;
    }
    uint32_t opacity = (current * 31u) / duration;
    if (!direction) opacity = 32u - opacity;

    uint64_t count64 = (uint64_t)width * height;
    if (!count64 || count64 > kFramebufferWidth * kFramebufferHeight)
    {
        return opacity;
    }
    uint32_t count = (uint32_t)count64;
    const uint8_t* source = resolveMemory(runtime, state->r[1], count * 4u);
    uint8_t* destination = resolveMemory(runtime, state->r[2], count * 2u);
    if (!source || !destination) return object;

    uint16_t backgroundColor = (uint16_t)background;
    uint32_t backgroundExpanded = ((backgroundColor & 0xf800u) << 10) |
        ((backgroundColor & 0x07e0u) << 5) | (backgroundColor & 0x001fu);
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t packed = 0;
        memcpy(&packed, source + i * 4u, sizeof(packed));
        uint16_t color = (uint16_t)packed;
        if ((packed >> 16) != 0x6fffu)
        {
            if (opacity >= 31u)
            {
                color = backgroundColor;
            }
            else if (opacity > 1u)
            {
                uint32_t sourceExpanded = ((color & 0xf800u) << 10) |
                    ((color & 0x07e0u) << 5) | (color & 0x001fu);
                uint32_t blend = (backgroundExpanded - sourceExpanded) * opacity +
                    (sourceExpanded << 5);
                color = (uint16_t)(((blend & 0x7c000000u) >> 15) |
                    ((blend & 0x001f8000u) >> 10) |
                    ((blend & 0x000003e0u) >> 5));
            }
        }
        memcpy(destination + i * 2u, &color, sizeof(color));
    }
    return opacity;
}

static const char kTransitionBlendImport[] = {
    99, 99, 95, 105, 110, 116, 101, 114, 110, 97, 108, 95, 116, 114, 97,
    110, 115, 105, 116, 105, 111, 110, 95, 98, 108, 101, 110, 100, 0
};

static const char kSoft3dLitScanlineImport[] = {
    99, 99, 95, 105, 110, 116, 101, 114, 110, 97, 108, 95, 115, 111, 102,
    116, 51, 100, 95, 115, 99, 97, 110, 108, 105, 110, 101, 95, 108, 105,
    116, 0
};

static uint32_t armArithmeticShiftRightRegister(uint32_t value,
    uint32_t shiftRegister)
{
    uint32_t shift = shiftRegister & 0xffu;
    if (!shift) return value;
    if (shift >= 32u) return (int32_t)value < 0 ? UINT32_MAX : 0u;
    return (uint32_t)((int32_t)value >> shift);
}

static uint32_t runCc1800TextureSpanBlock(CcArmRuntime* runtime,
    Arm32State* state)
{
    uint32_t continuation = state->r[15] == 0x1016f2e0u ?
        0x1016f31cu : 0x1016f19cu;
    uint32_t count = state->r[6];
    if (!count || count > 4096u) return state->r[0];

    uint32_t destinationAddress = state->r[0];
    uint32_t textureCoordinate = state->r[4];
    uint32_t paletteCoordinate = state->r[5];
    uint32_t depth = state->r[8];
    uint32_t lastDepthStep = 0;
    uint32_t lastDepthHigh = 0;
    uint32_t textureAvailable = 0;
    uint8_t* textureBase = resolveMemorySpan(runtime, state->r[2],
        &textureAvailable);
    uint8_t* destination = resolveMemory(runtime, destinationAddress,
        (size_t)count * sizeof(uint32_t));
    if (!readGuestU32(runtime, state->r[1] + 0x18u, &lastDepthStep))
    {
        return state->r[0];
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t paletteOffset = state->r[3] &
            armArithmeticShiftRightRegister(paletteCoordinate, state->r[12]);
        uint32_t textureOffset = (uint32_t)((int32_t)textureCoordinate >> 16);
        uint32_t sourceOffset = textureOffset * 2u + paletteOffset * 2u;
        uint8_t* source = NULL;
        if (textureBase && textureAvailable >= sizeof(uint16_t) &&
            sourceOffset <= textureAvailable - sizeof(uint16_t) &&
            sourceOffset <= UINT32_MAX - state->r[2])
        {
            source = textureBase + sourceOffset;
        }
        else
        {
            source = resolveMemory(runtime, state->r[2] + sourceOffset,
                sizeof(uint16_t));
        }
        uint8_t* pixel = destination ? destination + i * sizeof(uint32_t) :
            resolveMemory(runtime, destinationAddress, sizeof(uint32_t));
        if (!source || !pixel)
        {
            return state->r[0];
        }
        uint16_t color = 0;
        memcpy(&color, source, sizeof(color));
        lastDepthHigh = (uint32_t)((int32_t)depth >> 8);
        uint32_t packed = (uint32_t)color | (lastDepthHigh << 16);
        memcpy(pixel, &packed, sizeof(packed));
        destinationAddress += 4u;
        textureCoordinate += state->r[9];
        paletteCoordinate += state->r[10];
        depth += lastDepthStep;
    }

    state->r[0] = destinationAddress;
    state->r[4] = textureCoordinate;
    state->r[5] = paletteCoordinate;
    state->r[6] = 0;
    state->r[7] = lastDepthStep;
    state->r[8] = depth;
    state->r[11] = lastDepthHigh;
    state->r[14] = destinationAddress;
    state->cpsr = (state->cpsr & ~((1u << 31) | (1u << 30) | (1u << 29))) |
        (1u << 30) | (1u << 29);
    state->r[15] = continuation;
    return destinationAddress;
}

static const char kTextureSpanBlockImport[] = {
    99, 99, 95, 105, 110, 116, 101, 114, 110, 97, 108, 95, 116, 101, 120,
    116, 117, 114, 101, 95, 115, 112, 97, 110, 95, 98, 108, 111, 99, 107,
    0
};

static uint32_t armLogicalShiftLeftRegister(uint32_t value,
    uint32_t shiftRegister)
{
    uint32_t shift = shiftRegister & 0xffu;
    if (!shift) return value;
    return shift < 32u ? value << shift : 0u;
}

static uint32_t runCc1800ShiftLeft64(CcArmRuntime* runtime,
    const Arm32State* state)
{
    uint32_t sourceHigh = 0;
    uint32_t sourceLow = 0;
    if (!readGuestU32(runtime, state->r[1], &sourceHigh) ||
        !readGuestU32(runtime, state->r[1] + 4u, &sourceLow))
    {
        return state->r[0];
    }

    int32_t shift = (int32_t)state->r[2];
    uint32_t resultHigh = sourceHigh;
    uint32_t resultLow = sourceLow;
    if (shift != 0 && shift < 63)
    {
        resultHigh = 0;
        if (shift > 0)
        {
            for (uint32_t bit = 32u; bit < 64u; ++bit)
            {
                int32_t sourceBit = (int32_t)bit - shift;
                if (sourceBit < 0 || sourceBit >= 64) continue;
                uint32_t word = sourceBit < 32 ? sourceLow : sourceHigh;
                uint32_t wordBit = (uint32_t)sourceBit & 31u;
                if (word & (1u << wordBit)) resultHigh |= 1u << (bit - 32u);
            }
        }
        resultLow = shift > 31 ? sourceLow << 31 :
            armLogicalShiftLeftRegister(sourceLow, state->r[2]);
    }
    writeGuestU32(runtime, state->r[0], resultHigh);
    writeGuestU32(runtime, state->r[0] + 4u, resultLow);
    return state->r[0];
}

static const char kShiftLeft64Import[] = {
    99, 99, 95, 105, 110, 116, 101, 114, 110, 97, 108, 95, 115, 104, 105,
    102, 116, 95, 108, 101, 102, 116, 54, 52, 0
};

static const char kSoft3dTransparentScanlineImport[] = {
    99, 99, 95, 105, 110, 116, 101, 114, 110, 97, 108, 95, 115, 111, 102,
    116, 51, 100, 95, 116, 114, 97, 110, 115, 112, 97, 114, 101, 110, 116,
    95, 115, 99, 97, 110, 108, 105, 110, 101, 0
};

static uint32_t cc1800FixedMultiply(uint32_t left, uint32_t right)
{
    int64_t product = (int64_t)(int32_t)left * (int32_t)right;
    return (uint32_t)(product >> 16);
}

static uint32_t runCc1800AudioConvertBlock(CcArmRuntime* runtime,
    Arm32State* state)
{
    uint32_t count = state->r[3];
    if (!count || count > 4096u) return state->r[0];
    uint8_t* source = resolveMemory(runtime, state->r[1], count * 4u);
    uint8_t* destination = resolveMemory(runtime, state->r[2], count * 2u);
    if (!source || !destination) return state->r[0];

    int32_t last = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        int32_t sample = 0;
        memcpy(&sample, source + i * 4u, sizeof(sample));
        uint32_t product = (uint32_t)((int32_t)state->r[12]) *
            (uint32_t)(sample >> 8);
        int32_t scaled = (int32_t)product >> 8;
        if (scaled < -32768) scaled = -32768;
        else if (scaled >= 32768) scaled = 32767;
        int16_t output = (int16_t)scaled;
        memcpy(destination + i * 2u, &output, sizeof(output));
        last = scaled;
    }
    state->r[0] = (uint32_t)last;
    state->r[1] += count * 4u;
    state->r[2] += count * 2u;
    state->r[3] = 0;
    state->cpsr = (state->cpsr & ~((1u << 31) | (1u << 30) | (1u << 29))) |
        (1u << 30) | (1u << 29);
    state->r[15] = 0x10150a48u;
    return state->r[0];
}

static const char kAudioConvertBlockImport[] = {
    99, 99, 95, 105, 110, 116, 101, 114, 110, 97, 108, 95, 97, 117, 100,
    105, 111, 95, 99, 111, 110, 118, 101, 114, 116, 95, 98, 108, 111, 99,
    107, 0
};

static uint32_t runCc1800PerspectiveChunkSetup(CcArmRuntime* runtime,
    Arm32State* state)
{
    uint32_t remaining = 0;
    if (!readGuestU32(runtime, state->r[13] + 0x14u, &remaining))
    {
        return state->r[0];
    }
    state->r[6] = 0;
    writeGuestU32(runtime, state->r[13], 0u);
    writeGuestU32(runtime, state->r[13] + 4u, 0u);
    if (remaining <= 16u)
    {
        writeGuestU32(runtime, state->r[13] + 0x14u, 0u);
        if (!remaining)
        {
            state->r[15] = 0x1016f34cu;
            return state->r[0];
        }
        state->r[6] = remaining;
        if (remaining == 1u)
        {
            uint32_t paletteOffset = state->r[3] &
                armArithmeticShiftRightRegister(state->r[5], state->r[12]);
            uint32_t textureOffset = (uint32_t)((int32_t)state->r[4] >> 16);
            uint8_t* source = resolveMemory(runtime, state->r[2] +
                textureOffset * 2u + paletteOffset * 2u, sizeof(uint16_t));
            uint8_t* destination = resolveMemory(runtime, state->r[0],
                sizeof(uint32_t));
            if (!source || !destination) return state->r[0];
            uint16_t color = 0;
            memcpy(&color, source, sizeof(color));
            uint32_t packed = (uint32_t)color |
                ((uint32_t)((int32_t)state->r[8] >> 8) << 16);
            memcpy(destination, &packed, sizeof(packed));
            state->r[0] += 4u;
            state->r[15] = 0x1016f34cu;
            return state->r[0];
        }

        uint32_t spanFixed = (remaining - 1u) << 16;
        uint32_t xStepAddress = 0;
        uint32_t yStepAddress = 0;
        uint32_t xBase = 0;
        uint32_t yBase = 0;
        uint32_t depthStep = 0;
        if (!readGuestU32(runtime, state->r[13] + 0x28u, &xStepAddress) ||
            !readGuestU32(runtime, state->r[13] + 0x30u, &yStepAddress) ||
            !readGuestU32(runtime, state->r[13] + 0x10u, &xBase) ||
            !readGuestU32(runtime, state->r[13] + 0x0cu, &yBase) ||
            !readGuestU32(runtime, state->r[1] + 0x18u, &depthStep))
        {
            return state->r[0];
        }
        uint32_t xStep = 0;
        uint32_t yStep = 0;
        if (!readGuestU32(runtime, xStepAddress, &xStep) ||
            !readGuestU32(runtime, yStepAddress, &yStep))
        {
            return state->r[0];
        }
        uint32_t xEnd = xBase + cc1800FixedMultiply(xStep, spanFixed);
        uint32_t yEnd = yBase + cc1800FixedMultiply(yStep, spanFixed);
        writeGuestU32(runtime, state->r[13] + 0x10u, xEnd);
        writeGuestU32(runtime, state->r[13] + 0x0cu, yEnd);
        uint32_t depthEnd = state->r[8] +
            cc1800FixedMultiply(depthStep, spanFixed);
        uint32_t reciprocalTable = 0;
        if (!readGuestU32(runtime, 0x1016f848u, &reciprocalTable))
        {
            return state->r[0];
        }
        uint32_t depthReciprocal = 0;
        uint32_t depthIndex = (depthEnd << 9) >> 16;
        if (!readGuestU32(runtime, reciprocalTable + depthIndex * 4u,
                &depthReciprocal))
        {
            return state->r[0];
        }
        uint32_t xBiasAddress = 0;
        uint32_t yBiasAddress = 0;
        uint32_t xMaximumAddress = 0;
        uint32_t yMaximumAddress = 0;
        if (!readGuestU32(runtime, state->r[13] + 0x44u, &xBiasAddress) ||
            !readGuestU32(runtime, state->r[13] + 0x40u, &yBiasAddress) ||
            !readGuestU32(runtime, state->r[13] + 0x3cu, &xMaximumAddress) ||
            !readGuestU32(runtime, state->r[13] + 0x38u, &yMaximumAddress))
        {
            return state->r[0];
        }
        uint32_t xBias = 0;
        uint32_t yBias = 0;
        uint32_t xMaximum = 0;
        uint32_t yMaximum = 0;
        if (!readGuestU32(runtime, xBiasAddress, &xBias) ||
            !readGuestU32(runtime, yBiasAddress, &yBias) ||
            !readGuestU32(runtime, xMaximumAddress, &xMaximum) ||
            !readGuestU32(runtime, yMaximumAddress, &yMaximum))
        {
            return state->r[0];
        }
        uint32_t depthFactor = depthReciprocal << 3;
        int32_t projectedX = (int32_t)(cc1800FixedMultiply(xEnd,
            depthFactor) + xBias);
        int32_t projectedY = (int32_t)(cc1800FixedMultiply(yEnd,
            depthFactor) + yBias);
        projectedX = std::max<int32_t>(16,
            std::min<int32_t>(projectedX, (int32_t)xMaximum));
        projectedY = std::max<int32_t>(16,
            std::min<int32_t>(projectedY, (int32_t)yMaximum));
        writeGuestU32(runtime, state->r[13] + 4u, (uint32_t)projectedX);
        writeGuestU32(runtime, state->r[13], (uint32_t)projectedY);

        uint32_t spanReciprocal = 0;
        uint32_t spanIndex = (spanFixed & 0x000f0000u) >> 4;
        if (!readGuestU32(runtime, reciprocalTable + spanIndex * 4u,
                &spanReciprocal))
        {
            return state->r[0];
        }
        uint32_t stepFactor = (uint32_t)((int32_t)spanReciprocal >> 2);
        state->r[9] = cc1800FixedMultiply(
            (uint32_t)(projectedX - (int32_t)state->r[4]), stepFactor);
        state->r[10] = cc1800FixedMultiply(
            (uint32_t)(projectedY - (int32_t)state->r[5]), stepFactor);
        state->r[15] = 0x1016f2e0u;
        return runCc1800TextureSpanBlock(runtime, state);
    }
    writeGuestU32(runtime, state->r[13] + 0x14u, remaining - 16u);

    uint32_t xBase = 0;
    uint32_t yBase = 0;
    uint32_t xOffset = 0;
    uint32_t yOffset = 0;
    uint32_t depthOffset = 0;
    if (!readGuestU32(runtime, state->r[13] + 0x10u, &xBase) ||
        !readGuestU32(runtime, state->r[13] + 0x20u, &xOffset) ||
        !readGuestU32(runtime, state->r[13] + 0x0cu, &yBase) ||
        !readGuestU32(runtime, state->r[13] + 0x1cu, &yOffset) ||
        !readGuestU32(runtime, state->r[13] + 0x18u, &depthOffset))
    {
        return state->r[0];
    }
    uint32_t x = xBase + xOffset;
    uint32_t y = yBase + yOffset;
    writeGuestU32(runtime, state->r[13] + 0x10u, x);
    writeGuestU32(runtime, state->r[13] + 0x0cu, y);

    uint32_t reciprocalIndex = ((depthOffset + state->r[8]) << 9) >> 16;
    uint32_t reciprocalTable = 0;
    if (!readGuestU32(runtime, 0x1016f848u, &reciprocalTable))
    {
        return state->r[0];
    }
    uint32_t reciprocal = 0;
    if (!readGuestU32(runtime, reciprocalTable + reciprocalIndex * 4u,
            &reciprocal))
    {
        return state->r[0];
    }
    uint32_t factor = reciprocal << 3;

    uint32_t xBiasAddress = 0;
    uint32_t yBiasAddress = 0;
    uint32_t xMaximumAddress = 0;
    uint32_t yMaximumAddress = 0;
    if (!readGuestU32(runtime, state->r[13] + 0x44u, &xBiasAddress) ||
        !readGuestU32(runtime, state->r[13] + 0x40u, &yBiasAddress) ||
        !readGuestU32(runtime, state->r[13] + 0x3cu, &xMaximumAddress) ||
        !readGuestU32(runtime, state->r[13] + 0x38u, &yMaximumAddress))
    {
        return state->r[0];
    }
    uint32_t xBias = 0;
    uint32_t yBias = 0;
    uint32_t xMaximum = 0;
    uint32_t yMaximum = 0;
    if (!readGuestU32(runtime, xBiasAddress, &xBias) ||
        !readGuestU32(runtime, yBiasAddress, &yBias) ||
        !readGuestU32(runtime, xMaximumAddress, &xMaximum) ||
        !readGuestU32(runtime, yMaximumAddress, &yMaximum))
    {
        return state->r[0];
    }
    int32_t projectedX = (int32_t)(cc1800FixedMultiply(x, factor) + xBias);
    int32_t projectedY = (int32_t)(cc1800FixedMultiply(y, factor) + yBias);
    projectedX = std::max<int32_t>(16,
        std::min<int32_t>(projectedX, (int32_t)xMaximum));
    projectedY = std::max<int32_t>(16,
        std::min<int32_t>(projectedY, (int32_t)yMaximum));
    writeGuestU32(runtime, state->r[13] + 4u, (uint32_t)projectedX);
    writeGuestU32(runtime, state->r[13], (uint32_t)projectedY);

    state->r[6] = 16u;
    state->r[7] = (uint32_t)(projectedY - (int32_t)state->r[5]);
    state->r[9] = (uint32_t)((projectedX - (int32_t)state->r[4]) >> 4);
    state->r[10] = (uint32_t)((projectedY - (int32_t)state->r[5]) >> 4);
    state->r[15] = 0x1016f15cu;
    return runCc1800TextureSpanBlock(runtime, state);
}

static const char kPerspectiveChunkSetupImport[] = {
    99, 99, 95, 105, 110, 116, 101, 114, 110, 97, 108, 95, 112, 101, 114,
    115, 112, 101, 99, 116, 105, 118, 101, 95, 99, 104, 117, 110, 107, 95,
    115, 101, 116, 117, 112, 0
};

static void handleMemoryImport(CcArmRuntime* runtime, Arm32State* state,
    const char* name)
{
    if (!strcmp(name, "malloc") || !strcmp(name, "OSMalloc") || !strcmp(name, "jmalloc"))
    {
        uint32_t requested = state->r[0];
        state->r[0] = allocateMemory(runtime, requested);
        if (requested && !state->r[0])
        {
            printf("cc-arm: allocation failed api=%s size=%u heap=0x%08x blocks=%u\n",
                name, requested, runtime->heapCursor, (unsigned int)runtime->heap.size());
        }
    }
    else if (!strcmp(name, "calloc"))
    {
        uint64_t requested = (uint64_t)state->r[0] * state->r[1];
        uint32_t address = requested <= UINT32_MAX ?
            allocateMemory(runtime, (uint32_t)requested) : 0;
        if (address && requested) memset(resolveMemory(runtime, address, (size_t)requested), 0, (size_t)requested);
        state->r[0] = address;
    }
    else if (!strcmp(name, "free") || !strcmp(name, "OSFree") || !strcmp(name, "jfree"))
    {
        freeMemory(runtime, state->r[0]);
        state->r[0] = 0;
    }
    else if (!strcmp(name, "realloc"))
    {
        uint32_t oldAddress = state->r[0];
        uint32_t newSize = state->r[1];
        CcArmRuntime::HeapBlock* oldBlock = findHeapBlock(runtime, oldAddress);
        if (!oldAddress) state->r[0] = allocateMemory(runtime, newSize);
        else if (!newSize) { freeMemory(runtime, oldAddress); state->r[0] = 0; }
        else if (oldBlock && oldBlock->size >= newSize) state->r[0] = oldAddress;
        else
        {
            uint32_t oldSize = oldBlock ? oldBlock->size : 0;
            uint32_t address = allocateMemory(runtime, newSize);
            if (address && oldSize)
            {
                memcpy(resolveMemory(runtime, address, std::min(oldSize, newSize)),
                    resolveMemory(runtime, oldAddress, std::min(oldSize, newSize)),
                    std::min(oldSize, newSize));
                freeMemory(runtime, oldAddress);
            }
            state->r[0] = address;
        }
    }
    else if (!strcmp(name, "memset"))
    {
        uint8_t* destination = resolveMemory(runtime, state->r[0], state->r[2]);
        if (destination)
        {
            memset(destination, state->r[1] & 0xffu, state->r[2]);
            noteFramebufferWrite(runtime, state->r[0], state->r[2]);
        }
    }
    else if (!strcmp(name, "memcpy") || !strcmp(name, "memmove"))
    {
        uint8_t* destination = resolveMemory(runtime, state->r[0], state->r[2]);
        uint8_t* source = resolveMemory(runtime, state->r[1], state->r[2]);
        if (destination && source)
        {
            memmove(destination, source, state->r[2]);
            noteFramebufferWrite(runtime, state->r[0], state->r[2]);
        }
    }
    else if (!strcmp(name, "heap_get_block_size"))
    {
        CcArmRuntime::HeapBlock* block = findHeapBlock(runtime, state->r[0]);
        state->r[0] = block ? block->size : 0;
    }
}

static uint32_t currentOsTick(const CcArmRuntime* runtime);

static uint64_t currentGuestMicros(const CcArmRuntime* runtime)
{
    uint64_t hostMicros = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - runtime->startTime).count();
    return ccScaleElapsedMicros(hostMicros, s_runtimeSpeedScale.load());
}

static uint32_t currentTaskSchedulerTick(const CcArmRuntime* runtime,
    bool audioProducer)
{
    uint64_t hostMicros = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - runtime->startTime).count();
    return (uint32_t)(ccTaskSchedulerElapsedMicros(hostMicros,
        s_runtimeSpeedScale.load(), audioProducer) / 10000u);
}

static void markCurrentTaskAsAudioProducer(CcArmRuntime* runtime)
{
    if (runtime->currentTaskIndex < runtime->tasks.size())
    {
        runtime->tasks[runtime->currentTaskIndex].audioProducer = true;
    }
}

static void scheduleCurrentTaskDelay(CcArmRuntime* runtime, uint32_t ticks)
{
    runtime->currentDelayTicks = std::max<uint32_t>(ticks, 1u);
    runtime->yielded = true;
}

static void finishCurrentTask(Arm32State* state)
{
    state->r[0] = 0;
    state->r[15] = kExitAddress;
}

static bool handleLegacyAudioImport(CcArmRuntime* runtime, Arm32State* state,
    const char* name, bool* completed)
{
    *completed = true;
    if (!strcmp(name, "DVCOpenDevice"))
    {
        char deviceName[128] = {};
        if (!readGuestString(runtime, state->r[0], deviceName, sizeof(deviceName)) ||
            strcmp(deviceName, kDvcAudioDeviceName))
        {
            state->r[0] = 0;
            return true;
        }
        runtime->dvcAudioHandle = kDvcAudioHandle;
        runtime->dvcAudioSampleRate = kDvcAudioDefaultSampleRate;
        runtime->dvcAudioVolume = kDvcAudioMaxVolume;
        runtime->dvcAudioStarted = false;
        markCurrentTaskAsAudioProducer(runtime);
        state->r[0] = runtime->dvcAudioHandle;
        return true;
    }
    if (!strcmp(name, "DVCControlDevice"))
    {
        if (state->r[0] != runtime->dvcAudioHandle || !runtime->dvcAudioHandle)
        {
            state->r[0] = UINT32_MAX;
            return true;
        }
        uint32_t* argument = (uint32_t*)resolveMemory(runtime, state->r[3],
            sizeof(uint32_t));
        if (state->r[2] == kDvcAudioSetSampleRate && argument && *argument)
        {
            runtime->dvcAudioSampleRate = *argument;
        }
        else if (state->r[2] == kDvcAudioStartPlayback && argument && *argument &&
            !runtime->dvcAudioStarted)
        {
            waveout_args* args = (waveout_args*)malloc(sizeof(*args));
            if (!args)
            {
                state->r[0] = UINT32_MAX;
                return true;
            }
            args->sample_rate = runtime->dvcAudioSampleRate;
            args->format = AFMT_S16_LE;
            args->channel = 2;
            args->volume = 255;
            runtime->dvcAudioStarted = waveout_open(args) != 0;
        }
        state->r[0] = runtime->dvcAudioStarted ||
            state->r[2] != kDvcAudioStartPlayback ? 0u : UINT32_MAX;
        return true;
    }
    if (!strcmp(name, "DVCWriteDevice"))
    {
        markCurrentTaskAsAudioProducer(runtime);
        if (state->r[2] != runtime->dvcAudioHandle || !runtime->dvcAudioStarted)
        {
            state->r[0] = UINT32_MAX;
            return true;
        }
        const bool skipsAudioOutput = waveout_skips_audio_output();
        if (!skipsAudioOutput && !waveout_can_write_nonblocking())
        {
            state->r[15] -= 4u;
            scheduleCurrentTaskDelay(runtime, 1u);
            *completed = false;
            return true;
        }
        char* data = (char*)resolveMemory(runtime, state->r[0], state->r[1]);
        char* copy = data && state->r[1] ? (char*)malloc(state->r[1]) : NULL;
        if (copy) memcpy(copy, data, state->r[1]);
        if (!copy)
        {
            state->r[0] = UINT32_MAX;
            return true;
        }
        uint32_t written = skipsAudioOutput ?
            waveout_write(runtime->dvcAudioHandle, copy, (int)state->r[1]) :
            waveout_try_write(runtime->dvcAudioHandle, copy, (int)state->r[1]);
        state->r[0] = written ? state->r[1] : UINT32_MAX;
        return true;
    }
    if (!strcmp(name, "DVCCloseDevice"))
    {
        if (runtime->dvcAudioStarted) waveout_close(runtime->dvcAudioHandle);
        runtime->dvcAudioHandle = 0;
        runtime->dvcAudioStarted = false;
        state->r[0] = 0;
        return true;
    }
    if (!strcmp(name, "SYSSetVolume"))
    {
        runtime->dvcAudioVolume = std::min<uint32_t>(state->r[0],
            kDvcAudioMaxVolume);
        waveout_set_volume((runtime->dvcAudioVolume * 255u +
            kDvcAudioMaxVolume / 2u) / kDvcAudioMaxVolume);
        state->r[0] = 0;
        return true;
    }
    if (!strcmp(name, "SYSGetVolume") || !strcmp(name, "get_game_vol"))
    {
        state->r[0] = runtime->dvcAudioVolume ? runtime->dvcAudioVolume :
            kDvcAudioMaxVolume;
        return true;
    }
    if (!strcmp(name, "HP_Mute_sw"))
    {
        state->r[0] = waveout_mute(state->r[0]);
        return true;
    }
    if (!strcmp(name, "wavaopen") || !strcmp(name, "waveioc") ||
        !strcmp(name, "waveclose"))
    {
        state->r[0] = 0;
        return true;
    }
    return false;
}

static void requestCcGuestExit(CcArmRuntime* runtime, Arm32State* state, const char* reason)
{
    printf("cc-arm: guest exit requested by %s task=%u lr=0x%08x\n",
        reason ? reason : "<unknown>", runtime->currentTaskIndex, state->r[14]);
    finishCurrentTask(state);
    runtime->yielded = false;
    runtime->currentDelayTicks = 0;
    runtime->stats->guestCompleted = true;
    s_stopRequested.store(true);
}

static bool parsePositiveScaleEnv(const char* name, double* output)
{
    const char* value = getenv(name);
    if (!value || !value[0] || !output)
    {
        return false;
    }
    char* end = NULL;
    double parsed = strtod(value, &end);
    if (end == value || *end || parsed <= 0.0)
    {
        return false;
    }
    *output = parsed;
    return true;
}

static bool handleSvc(void* userData, Arm32State* state, uint32_t immediate)
{
    CcArmRuntime* runtime = (CcArmRuntime*)userData;
    const char* name = NULL;
    uint32_t svcAddress = state->r[15] - 4u;
    if (immediate < runtime->package->import_count && runtime->package->import_data[immediate])
    {
        name = runtime->package->import_data[immediate]->name;
        if (ccPackageUsesHomebrewLayout(runtime->package->origin) &&
            runtime->package->import_data[immediate]->offset == svcAddress)
        {
            state->r[15] = state->r[14] & ~1u;
            if (state->r[14] & 1u) state->cpsr |= 1u << 5;
            else state->cpsr &= ~(1u << 5);
        }
    }
    else if (immediate >= runtime->package->import_count)
    {
        uint32_t dynamic = immediate - runtime->package->import_count;
        if (dynamic < runtime->dynamicImports.size())
        {
            name = runtime->dynamicImports[dynamic].c_str();
        }
    }
    runtime->stats->importCalls++;
    snprintf(runtime->stats->lastImport, sizeof(runtime->stats->lastImport), "%s",
        name ? name : "(invalid)");
    runtime->stats->lastImportPc = svcAddress;
    runtime->stats->lastImportReturnAddress = state->r[14];
    if (!name)
    {
        recordUnknownImport(runtime, NULL);
        state->r[0] = 0;
        return true;
    }
    if (!strcmp(name, "consoleEnable") || !strcmp(name, "consoleDisable") ||
        !strcmp(name, "PMSetMode"))
    {
        state->r[0] = 0;
        return true;
    }
    if (!strcmp(name, "TaskMediaFunStop"))
    {
        if (runtime->stats->framesSubmitted > 0)
        {
            requestCcGuestExit(runtime, state, name);
        }
        else
        {
            printf("cc-arm: startup TaskMediaFunStop completed without guest exit\n");
            state->r[0] = 0;
        }
        return true;
    }
    if (!strcmp(name, "vxGoHome") || !strcmp(name, "abort") ||
        !strcmp(name, "av_end_thread") || !strcmp(name, "av_queue_abort"))
    {
        requestCcGuestExit(runtime, state, name);
        return true;
    }
    if (!strcmp(name, "cmGetSysModel"))
    {
        state->r[0] = writeGuestString(runtime, state->r[0], state->r[1], "CC1800") ? 0u : UINT32_MAX;
        return true;
    }
    if (!strcmp(name, "cmGetSysVersion"))
    {
        state->r[0] = writeGuestString(runtime, state->r[0], state->r[1], "1.0") ? 0u : UINT32_MAX;
        return true;
    }
    if (!strcmp(name, "get_current_language"))
    {
        state->r[0] = 0;
        return true;
    }

    if (!strcmp(name, "dl_get_proc"))
    {
        char requested[128] = {};
        if (!readGuestString(runtime, state->r[1], requested, sizeof(requested))) { state->r[0] = 0; return true; }
        uint32_t address = findExport(runtime->package, requested);
        uint32_t index = findImport(runtime->package, requested);
        if (!address && index != UINT32_MAX) address = runtime->package->import_data[index]->offset;
        if (!address) address = createDynamicImport(runtime, requested);
        state->r[0] = address;
        snprintf(runtime->stats->lastImport, sizeof(runtime->stats->lastImport), "dl_get_proc:%s", requested);
        return true;
    }
    if (!strcmp(name, "GetDLHandle") || !strcmp(name, "get_dl_handle")) { state->r[0] = kLoaderHandle; return true; }
    if (!strcmp(name, "__to_locale_ansi") || !strcmp(name, "_to_locale_ansi")) { state->r[0] = kLocaleString; return true; }
    if (!strcmp(name, "cc_internal_blit_transparent16"))
    {
        uint32_t object = state->r[0];
        state->r[0] = runTransparentBlit16(runtime, object, 0u) ? object : 0;
        return true;
    }
    if (!strncmp(name, "cc_internal_blit_transparent_blend", 34u))
    {
        uint32_t object = state->r[0];
        uint32_t mode = (uint32_t)(name[34] - '0');
        state->r[0] = runTransparentBlit16(runtime, object, mode) ? object : 0;
        return true;
    }
    if (!strcmp(name, "cc_internal_blit_scaled16"))
    {
        uint32_t object = state->r[0];
        state->r[0] = runScaledBlit16(
            runtime, object, false, false) ? object : 0;
        return true;
    }
    if (!strcmp(name, "cc_internal_blit_scaled_indexed16"))
    {
        uint32_t object = state->r[0];
        state->r[0] = runScaledBlit16(
            runtime, object, true, false) ? object : 0;
        return true;
    }
    if (!strcmp(name, "cc_internal_blit_scaled_transparent16"))
    {
        uint32_t object = state->r[0];
        state->r[0] = runScaledBlit16(
            runtime, object, false, true) ? object : 0;
        return true;
    }
    if (!strcmp(name, "cc_internal_fast_copy"))
    {
        state->r[0] = runFastCopy(runtime, state->r[0], state->r[1], state->r[2]);
        return true;
    }
    if (!strcmp(name, "cc_internal_fast_fill_zero"))
    {
        state->r[0] = runFastFill(runtime, state->r[0], state->r[1], 0);
        return true;
    }
    if (!strcmp(name, "cc_internal_fast_fill_byte"))
    {
        state->r[0] = runFastFill(runtime, state->r[0], state->r[1],
            (uint8_t)state->r[2]);
        return true;
    }
    if (!strcmp(name, kFill32PairsImport))
    {
        return runCc1800Fill32Pairs(runtime, state);
    }
    if (!strcmp(name, kAdpcmDecodeTailImport))
    {
        return runCc1800AdpcmDecodeTail(runtime, state);
    }
    if (!strcmp(name, kRowFill32PairsImport))
    {
        return runCc1800RowFill32Pairs(runtime, state);
    }
    if (!strcmp(name, "cc_internal_frame_copy"))
    {
        state->r[0] = runFastFrameCopy(runtime, state);
        return true;
    }
    if (!strcmp(name, "cc_internal_normalize_vec3_fixed"))
    {
        uint32_t saved[8];
        saveArmCalleeSaved(state, saved);
        state->r[0] = runNormalizeVector3Fixed(runtime,
            state->r[0], state->r[1]);
        restoreArmCalleeSaved(state, saved);
        return true;
    }
    if (!strcmp(name, "cc_internal_indexed_scaled"))
    {
        state->r[0] = runFastIndexedBlit(runtime, state->r[0], true, false);
        return true;
    }
    if (!strcmp(name, "cc_internal_indexed_copy"))
    {
        state->r[0] = runFastIndexedBlit(runtime, state->r[0], false, false);
        return true;
    }
    if (!strcmp(name, "cc_internal_indexed_transparent"))
    {
        state->r[0] = runFastIndexedBlit(runtime, state->r[0], false, true);
        return true;
    }
    if (!strcmp(name, "cc_internal_soft3d_scanline_opaque"))
    {
        uint32_t saved[8];
        saveArmCalleeSaved(state, saved);
        state->r[0] = runSoft3dOpaqueScanlines(runtime, state->r[0], state->r[1],
            false, false);
        restoreArmCalleeSaved(state, saved);
        return true;
    }
    if (!strcmp(name, kSoft3dLitScanlineImport))
    {
        uint32_t saved[8];
        saveArmCalleeSaved(state, saved);
        state->r[0] = runSoft3dOpaqueScanlines(runtime, state->r[0], state->r[1],
            true, false);
        restoreArmCalleeSaved(state, saved);
        return true;
    }
    if (!strcmp(name, kSoft3dTransparentScanlineImport))
    {
        uint32_t saved[8];
        saveArmCalleeSaved(state, saved);
        state->r[0] = runSoft3dOpaqueScanlines(runtime, state->r[0], state->r[1],
            false, true);
        restoreArmCalleeSaved(state, saved);
        return true;
    }
    if (!strcmp(name, "cc_internal_soft3d_fill32"))
    {
        state->r[0] = runSoft3dFill32(runtime, state);
        return true;
    }
    if (!strcmp(name, "cc_internal_indexed_alpha"))
    {
        state->r[0] = runFastIndexedAlphaBlit(runtime, state->r[0]);
        return true;
    }
    if (!strcmp(name, kTransitionBlendImport))
    {
        state->r[0] = runCc1800TransitionBlend(runtime, state);
        return true;
    }

    if (!strcmp(name, kTextureSpanBlockImport))
    {
        state->r[0] = runCc1800TextureSpanBlock(runtime, state);
        return true;
    }
    if (!strcmp(name, kShiftLeft64Import))
    {
        state->r[0] = runCc1800ShiftLeft64(runtime, state);
        return true;
    }
    if (!strcmp(name, kAudioConvertBlockImport))
    {
        state->r[0] = runCc1800AudioConvertBlock(runtime, state);
        return true;
    }
    if (!strcmp(name, kPerspectiveChunkSetupImport))
    {
        state->r[0] = runCc1800PerspectiveChunkSetup(runtime, state);
        return true;
    }
    if (!strcmp(name, "malloc") || !strcmp(name, "calloc") || !strcmp(name, "realloc") ||
        !strcmp(name, "free") || !strcmp(name, "OSMalloc") || !strcmp(name, "OSFree") ||
        !strcmp(name, "jmalloc") || !strcmp(name, "jfree") || !strcmp(name, "memset") ||
        !strcmp(name, "memcpy") || !strcmp(name, "memmove") || !strcmp(name, "heap_get_block_size"))
    {
        handleMemoryImport(runtime, state, name);
        return true;
    }
    if (!strcmp(name, "OSTaskCreate"))
    {
        CcArmRuntime::Task task = {};
        task.entry = state->r[0];
        task.argument = state->r[1];
        task.stack = state->r[2];
        task.priority = state->r[3];
        if (task.entry && task.stack) runtime->tasks.push_back(task);
        runtime->stats->tasksCreated = (uint32_t)runtime->tasks.size();
        state->r[0] = 0;
        return true;
    }
    if (!strcmp(name, "OSTaskDel"))
    {
        const uint32_t priority = state->r[0] & 0xffu;
        size_t targetTaskIndex = findActiveTaskIndex(runtime, priority);
        if (targetTaskIndex == runtime->tasks.size())
        {
            state->r[0] = 41u;
        }
        else if (targetTaskIndex == 0)
        {
            requestCcGuestExit(runtime, state, name);
        }
        else if (targetTaskIndex == runtime->currentTaskIndex)
        {
            printf("cc-arm: task exit requested by OSTaskDel task=%u priority=%u\n",
                runtime->currentTaskIndex, priority);
            finishCurrentTask(state);
        }
        else
        {
            printf("cc-arm: task delete requested by OSTaskDel task=%u priority=%u\n",
                (uint32_t)targetTaskIndex, priority);
            runtime->tasks[targetTaskIndex].finished = true;
            state->r[0] = 0;
        }
        return true;
    }
    if (!strcmp(name, "OSTaskQuery"))
    {
        const uint32_t priority = state->r[0] & 0xffu;
        state->r[0] = findActiveTaskIndex(runtime, priority) <
            runtime->tasks.size() ? 0u : 41u;
        return true;
    }
    if (!strcmp(name, "OSTimeDlyHMSM"))
    {
        scheduleCurrentTaskDelay(runtime, ccHmsmToOsTicks(state->r[0], state->r[1],
            state->r[2], state->r[3], OS_TICKS_PER_SEC));
        return false;
    }
    if (!strcmp(name, "delay_ms"))
    {
        scheduleCurrentTaskDelay(runtime,
            ccMillisecondsToOsTicks(state->r[0], OS_TICKS_PER_SEC));
        return false;
    }
    if (!strcmp(name, "OSTimeDly") || !strcmp(name, "delay"))
    {
        scheduleCurrentTaskDelay(runtime, state->r[0]);
        return false;
    }
    if (!strcmp(name, "OSTimeGet") || !strcmp(name, "GetTickCount") || !strcmp(name, "OSTimerGetTickTimeus"))
    {
        uint64_t micros = currentGuestMicros(runtime);
        if (!strcmp(name, "OSTimerGetTickTimeus")) state->r[0] = (uint32_t)micros;
        else if (!strcmp(name, "GetTickCount")) state->r[0] = (uint32_t)(micros / 1000u);
        else state->r[0] = currentOsTick(runtime);
        return true;
    }
    if (!strcmp(name, "OSSemCreate"))
    {
        uint32_t address = allocateMemory(runtime, 16);
        if (address) runtime->semaphores.push_back({ address, state->r[0] });
        state->r[0] = address;
        return true;
    }
    if (!strcmp(name, "OSSemPend"))
    {
        CcArmRuntime::Semaphore* semaphore = findSemaphore(runtime, state->r[0]);
        if (semaphore && semaphore->count)
        {
            semaphore->count--;
            if (state->r[2]) { uint8_t zero = 0; busWrite(runtime, state->r[2], &zero, 1); }
            state->r[0] = 0;
            return true;
        }
        state->r[15] -= 4u;
        runtime->currentDelayTicks = 1;
        runtime->yielded = true;
        return false;
    }
    if (!strcmp(name, "OSSemPost"))
    {
        CcArmRuntime::Semaphore* semaphore = findSemaphore(runtime, state->r[0]);
        if (semaphore && semaphore->count != UINT32_MAX) semaphore->count++;
        state->r[0] = semaphore ? 0 : 1;
        return true;
    }
    if (!strcmp(name, "OSSemAccept"))
    {
        CcArmRuntime::Semaphore* semaphore = findSemaphore(runtime, state->r[0]);
        state->r[0] = semaphore && semaphore->count ? semaphore->count-- : 0;
        return true;
    }

    if (!strcmp(name, "fsys_fopen"))
    {
        char fileName[512] = {};
        char mode[16] = {};
        if (!readGuestString(runtime, state->r[0], fileName, sizeof(fileName)) ||
            !readGuestString(runtime, state->r[1], mode, sizeof(mode)))
        {
            state->r[0] = 0;
            return true;
        }
        state->r[0] = openFile(runtime, fileName, mode);
        return true;
    }
    if (!strcmp(name, "fsys_fopenW"))
    {
        std::string fileName;
        char mode[16] = {};
        if (!readGuestWideString(runtime, state->r[0], &fileName, 512u) ||
            !readGuestString(runtime, state->r[1], mode, sizeof(mode)))
        {
            state->r[0] = 0;
            return true;
        }
        state->r[0] = openFile(runtime, fileName.c_str(), mode[0] ? mode : "rb");
        return true;
    }
    if (!strcmp(name, "fsys_fclose"))
    {
        state->r[0] = closeFile(runtime, state->r[0]);
        return true;
    }
    if (!strcmp(name, "fsys_fread") || !strcmp(name, "fsys_fwrite"))
    {
        uint64_t requested = (uint64_t)state->r[1] * state->r[2];
        void* data = requested <= UINT32_MAX ?
            resolveMemory(runtime, state->r[0], (uint32_t)requested) : NULL;
        uint32_t stream = fileStream(runtime, state->r[3]);
        state->r[0] = data ? (!strcmp(name, "fsys_fread") ?
            vm_fread(data, state->r[1], state->r[2], stream) :
            fsys_fwrite(data, state->r[1], state->r[2], stream)) : 0;
        return true;
    }
    if (!strcmp(name, "fsys_fseek"))
    {
        state->r[0] = fsys_fseek(fileStream(runtime, state->r[0]),
            state->r[1], state->r[2]);
        return true;
    }
    if (!strcmp(name, "fsys_ftell"))
    {
        state->r[0] = fsys_ftell(fileStream(runtime, state->r[0]));
        return true;
    }
    if (!strcmp(name, "fsys_feof"))
    {
        state->r[0] = fsys_feof(fileStream(runtime, state->r[0]));
        return true;
    }
    if (!strcmp(name, "fsys_ferror")) { state->r[0] = 0; return true; }

    if (!strcmp(name, "lcd_get_frame") || !strcmp(name, "_lcd_get_frame") || !strcmp(name, "LCDGetFB"))
    {
        state->r[0] = kFramebufferAddress;
        return true;
    }
    if (!strcmp(name, "LCDGetWidth") || !strcmp(name, "get_lcd_width"))
    {
        state->r[0] = kFramebufferWidth;
        return true;
    }
    if (!strcmp(name, "LCDGetHeight") || !strcmp(name, "get_lcd_height"))
    {
        state->r[0] = kFramebufferHeight;
        return true;
    }
    if (!strcmp(name, "lcd_set_frame") || !strcmp(name, "_lcd_set_frame"))
    {
        presentFramebuffer(runtime, runtime->framebufferAddress);
        state->r[0] = 0;
        return true;
    }
    if (!strcmp(name, "LCDFlushFB") || !strcmp(name, "LCDFlushFBZoom"))
    {
        presentFramebuffer(runtime, runtime->framebufferAddress);
        state->r[0] = 0;
        return true;
    }
    if (!strcmp(name, "BMF_SetLcdFramePtr"))
    {
        if (resolveMemory(runtime, state->r[0], kFramebuffer16Size))
        {
            runtime->framebufferAddress = state->r[0];
        }
        state->r[0] = 0;
        return true;
    }
    if (!strcmp(name, "SysLcdClear"))
    {
        memset(runtime->framebuffer.data(), 0, runtime->framebuffer.size());
        state->r[0] = 0;
        return true;
    }
    if (!strcmp(name, "LCDSetFBBit"))
    {
        if (state->r[0] == 16u || state->r[0] == 32u)
        {
            runtime->framebufferBits = state->r[0];
            runtime->framebufferBitsExplicit = true;
        }
        printf("cc-arm: framebuffer bits=%u\n", state->r[0]);
        state->r[0] = 0;
        return true;
    }
    if (!strcmp(name, "LCDIsDoubleFBEnabled"))
    {
        state->r[0] = 1;
        return true;
    }
    if (!strcmp(name, "LCDGetFBFormat") || !strcmp(name, "LCDEnableDoubleFB") ||
        !strcmp(name, "LCDDisableDoubleFB") || !strcmp(name, "LCDSetFBFormat") ||
        !strcmp(name, "LCDInit") || !strcmp(name, "LCDSetRefreshRate") ||
        !strcmp(name, "LCDSetBrightness") || !strcmp(name, "FlushDCache") ||
        !strcmp(name, "InvalidICache") || !strcmp(name, "fsys_RefreshCache") ||
        !strcmp(name, "BMF_SelectPixelFunc"))
    {
        state->r[0] = 0;
        return true;
    }

    if (!strcmp(name, "kbd_get_status") || !strcmp(name, "_kbd_get_status") ||
        !strcmp(name, "rmt_get_status"))
    {
        GuestKeyStatus status = {};
        _kbd_get_status(&status);
        status.pressed = mapInputForRuntime(runtime, status.pressed);
        status.released = mapInputForRuntime(runtime, status.released);
        status.status = mapInputForRuntime(runtime, status.status);
        busWrite(runtime, state->r[0], &status, sizeof(status));
        state->r[0] = 0; return true;
    }
    if (!strcmp(name, "_kbd_get_key") || !strcmp(name, "kbd_get_key") ||
        !strcmp(name, "sys_get_key") || !strcmp(name, "KBDGetSKey") ||
        !strcmp(name, "KBDGetSKeyStatus") || !strcmp(name, "RMTGetSKey"))
    {
        uint32_t key = _kbd_get_key();
        state->r[0] = mapInputForRuntime(runtime, key);
        return true;
    }
    if (!strcmp(name, "Tp_Get_Pos"))
    {
        state->r[0] = UINT32_MAX;
        return true;
    }
    if (!strcmp(name, "sys_judge_event") || !strcmp(name, "_sys_judge_event"))
    {
        state->r[0] = inputHasPendingEvent();
        return true;
    }
    if (!strcmp(name, "stricmp"))
    {
        state->r[0] = (uint32_t)compareGuestStringsIgnoreCase(runtime,
            state->r[0], state->r[1]);
        return true;
    }

    if (!strcmp(name, "dl_res_open"))
    {
        char resourceName[256] = {};
        bool hasName = readGuestString(runtime, state->r[2], resourceName, sizeof(resourceName));
        if (!hasName) hasName = readGuestString(runtime, state->r[1], resourceName, sizeof(resourceName));
        if (!hasName) hasName = readGuestString(runtime, state->r[0], resourceName, sizeof(resourceName));
        if (!hasName) { state->r[0] = 0; return true; }
        GuestResourceEntry* entry = guestPackageFindResource(runtime->package, resourceName);
        if (!entry) { state->r[0] = 0; return true; }
        uint32_t address = allocateMemory(runtime, 16);
        runtime->resources.push_back({ address, entry, 0, 0 });
        state->r[0] = address; return true;
    }
    if (!strcmp(name, "dl_res_get_size"))
    {
        CcArmRuntime::ResourceHandle* resource = findResource(runtime, state->r[0]);
        state->r[0] = resource ? resource->entry->size : 0; return true;
    }
    if (!strcmp(name, "dl_res_get_data"))
    {
        CcArmRuntime::ResourceHandle* resource = findResource(runtime, state->r[0]);
        if (!resource) { state->r[0] = 0; return true; }
        const uint8_t* data = guestPackageResourceData(runtime->package, resource->entry);
        if (!data) { state->r[0] = 0; return true; }
        if (!state->r[1])
        {
            if (!resource->dataAddress)
            {
                resource->dataAddress = allocateMemory(runtime, resource->entry->size);
                if (resource->dataAddress)
                {
                    busWrite(runtime, resource->dataAddress, data, resource->entry->size);
                }
            }
            state->r[0] = resource->dataAddress;
            return true;
        }
        uint64_t requested = state->r[3] ? (uint64_t)state->r[2] * state->r[3] : state->r[2];
        if (requested > UINT32_MAX) { state->r[0] = 0; return true; }
        uint32_t available = resource->position < resource->entry->size ? resource->entry->size - resource->position : 0;
        uint32_t bytes = std::min<uint32_t>((uint32_t)requested, available);
        if (!data || !busWrite(runtime, state->r[1], data + resource->position, bytes)) bytes = 0;
        resource->position += bytes;
        state->r[0] = state->r[3] ? bytes / state->r[3] : bytes;
        return true;
    }
    if (!strcmp(name, "dl_res_close"))
    {
        uint32_t address = state->r[0];
        for (size_t i = 0; i < runtime->resources.size(); ++i)
        {
            if (runtime->resources[i].address == address)
            {
                freeMemory(runtime, runtime->resources[i].dataAddress);
                runtime->resources.erase(runtime->resources.begin() + i);
                freeMemory(runtime, address);
                break;
            }
        }
        state->r[0] = 0;
        return true;
    }

    if (!strcmp(name, "waveout_open"))
    {
        markCurrentTaskAsAudioProducer(runtime);
        waveout_args* guest = (waveout_args*)resolveMemory(runtime, state->r[0], sizeof(waveout_args));
        if (!guest) { state->r[0] = 0; return true; }
        waveout_args* copy = (waveout_args*)malloc(sizeof(*copy));
        if (!copy) { state->r[0] = 0; return true; }
        *copy = *guest; state->r[0] = waveout_open(copy); return true;
    }
    if (!strcmp(name, "waveout_write"))
    {
        markCurrentTaskAsAudioProducer(runtime);
        const bool skipsAudioOutput = waveout_skips_audio_output();
        if (!skipsAudioOutput && !waveout_can_write_nonblocking())
        {
            state->r[15] -= 4u;
            scheduleCurrentTaskDelay(runtime, 1u);
            return false;
        }
        char* data = (char*)resolveMemory(runtime, state->r[1], state->r[2]);
        char* copy = data && state->r[2] ? (char*)malloc(state->r[2]) : NULL;
        if (copy) memcpy(copy, data, state->r[2]);
        if (!copy)
        {
            state->r[0] = 0;
        }
        else if (skipsAudioOutput)
        {
            state->r[0] = waveout_write(state->r[0], copy, (int)state->r[2]);
        }
        else
        {
            state->r[0] = waveout_try_write(state->r[0], copy, (int)state->r[2]);
        }
        return true;
    }
    if (!strcmp(name, "waveout_close")) { state->r[0] = waveout_close(state->r[0]); return true; }
    if (!strcmp(name, "waveout_can_write"))
    {
        markCurrentTaskAsAudioProducer(runtime);
        state->r[0] = waveout_can_write_nonblocking();
        return true;
    }
    if (!strcmp(name, "waveout_set_volume")) { state->r[0] = waveout_set_volume(state->r[0]); return true; }
    if (!strcmp(name, "waveout_mute")) { state->r[0] = waveout_mute(state->r[0]); return true; }

    bool legacyAudioCompleted = true;
    if (handleLegacyAudioImport(runtime, state, name, &legacyAudioCompleted))
    {
        return legacyAudioCompleted;
    }

    if (!strcmp(name, "printf"))
    {
        state->r[0] = 0;
        return true;
    }

    recordUnknownImport(runtime, name);
    state->r[0] = 0;
    return true;
}

static Arm32RunResult runState(CcArmRuntime* runtime, Arm32State* state,
    uint64_t sliceInstructions = kSliceInstructions)
{
    pauseGateWaitForResume();
    runtime->yielded = false;
    runtime->currentDelayTicks = 0;
    runtime->faultAddress = 0;
    runtime->faultSize = 0;
    runtime->faultWrite = false;
    runtime->faultFetch = false;
    uint64_t before = state->instructions;
    Arm32RunResult result = arm32Run(state, &runtime->bus, kExitAddress,
        state->instructions + sliceInstructions);
    runtime->stats->instructions += state->instructions - before;
    uint64_t targetIps = s_targetInstructionsPerSecond.load();
    if (targetIps)
    {
        uint64_t expectedMicros = runtime->stats->instructions * 1000000u / targetIps;
        uint64_t hostMicros = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - runtime->startTime).count();
        if (expectedMicros > hostMicros)
        {
            std::this_thread::sleep_for(
                std::chrono::microseconds(expectedMicros - hostMicros));
        }
    }
    return result;
}

static uint32_t currentOsTick(const CcArmRuntime* runtime)
{
    return (uint32_t)(currentGuestMicros(runtime) / 10000u);
}

static void profileCcRuntime(CcArmRuntime* runtime)
{
    if (!runtimeLogProfileEnabled())
    {
        return;
    }
    uint64_t elapsedMillis = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - runtime->startTime).count();
    if (elapsedMillis - runtime->profileLastMillis < runtimeLogProfileIntervalMs())
    {
        return;
    }
    uint32_t activeTasks = 0;
    uint32_t delayedTasks = 0;
    uint32_t firstPc = 0;
    uint32_t firstDelay = 0;
    for (size_t i = 0; i < runtime->tasks.size(); ++i)
    {
        const CcArmRuntime::Task& task = runtime->tasks[i];
        if (task.finished)
        {
            continue;
        }
        ++activeTasks;
        if (task.delayTicks)
        {
            ++delayedTasks;
        }
        if (!firstPc)
        {
            firstPc = task.state.r[15];
            firstDelay = task.delayTicks;
        }
    }
    uint64_t intervalInstructions = runtime->stats->instructions -
        runtime->profileLastInstructions;
    printf("cc-profile: elapsed_ms=%llu ips=%llu tick=%u tasks=%u delayed=%u "
        "pc=0x%08x deadline=%u frames=%u last=%s\n",
        (unsigned long long)elapsedMillis,
        (unsigned long long)runtimeLogRatePerSecond(intervalInstructions,
            elapsedMillis - runtime->profileLastMillis),
        currentOsTick(runtime), activeTasks, delayedTasks, firstPc, firstDelay,
        runtime->stats->framesSubmitted,
        runtime->stats->lastImport[0] ? runtime->stats->lastImport : "(none)");
    runtime->profileLastMillis = elapsedMillis;
    runtime->profileLastInstructions = runtime->stats->instructions;
}


static bool decodeArmBranchTarget(uint32_t address, uint32_t instruction,
    uint32_t* target)
{
    if (!target || (instruction & 0xff000000u) != 0xeb000000u)
    {
        return false;
    }
    int32_t displacement = (int32_t)(instruction << 8) >> 6;
    *target = address + 8u + (uint32_t)displacement;
    return true;
}

static bool writeArmTailBranch(CcArmRuntime* runtime, uint32_t address,
    uint32_t target)
{
    int64_t displacement = (int64_t)target - ((int64_t)address + 8);
    if ((displacement & 3) != 0 || displacement < -0x02000000ll ||
        displacement > 0x01fffffcll)
    {
        return false;
    }
    uint32_t instruction = 0xea000000u |
        ((uint32_t)(displacement >> 2) & 0x00ffffffu);
    return busWrite(runtime, address, &instruction, sizeof(instruction));
}

static bool writeArmAbsoluteTailJump(CcArmRuntime* runtime, uint32_t address,
    uint32_t target)
{
    // LDR pc, [pc, #-4] loads the following literal without changing lr.
    const uint32_t instructions[2] = { 0xe51ff004u, target };
    return busWrite(runtime, address, instructions, sizeof(instructions));
}

static bool writeArmInlineImport(CcArmRuntime* runtime, uint32_t address,
    const char* importName)
{
    uint32_t thunk = createDynamicImport(runtime, importName);
    if (thunk < kDynamicThunkStart) return false;
    uint32_t slot = (thunk - kDynamicThunkStart) / 8u;
    uint32_t index = runtime->package->import_count + slot;
    if (index > 0x00ffffffu) return false;
    uint32_t instruction = 0xef000000u | index;
    return busWrite(runtime, address, &instruction, sizeof(instruction));
}

static void patchLegacySdkAllocator(CcArmRuntime* runtime)
{
    uint32_t mallocIndex = findImport(runtime->package, "malloc");
    uint32_t freeIndex = findImport(runtime->package, "free");
    if (mallocIndex == UINT32_MAX || freeIndex == UINT32_MAX)
    {
        return;
    }
    uint32_t mallocImport = runtime->package->import_data[mallocIndex]->offset;
    uint32_t freeImport = runtime->package->import_data[freeIndex]->offset;
    uint32_t patchedMalloc = 0;
    uint32_t patchedFree = 0;
    uint32_t start = runtime->package->origin;
    uint32_t end = start + runtime->package->prog_size;
    for (uint32_t address = start; address + 48u <= end; address += 4u)
    {
        uint32_t* code = (uint32_t*)resolveMemory(runtime, address, 48u);
        if (!code)
        {
            break;
        }
        uint32_t target = 0;
        if (!patchedMalloc && code[0] == 0xe92d4008u &&
            decodeArmBranchTarget(address + 4u, code[1], &target) &&
            target == mallocImport)
        {
            if (writeArmTailBranch(runtime, address, mallocImport))
            {
                patchedMalloc = address;
            }
        }
        if (!patchedFree && code[0] == 0xe92d4038u && code[2] == 0xe3a04000u)
        {
            for (uint32_t i = 1; i < 12u; ++i)
            {
                if (decodeArmBranchTarget(address + i * 4u, code[i], &target) &&
                    target == freeImport)
                {
                    if (writeArmTailBranch(runtime, address, freeImport))
                    {
                        patchedFree = address;
                    }
                    break;
                }
            }
        }
        if (patchedMalloc && patchedFree)
        {
            break;
        }
    }
    if (patchedMalloc || patchedFree)
    {
        printf("cc-arm: legacy SDK allocator patched malloc=0x%08x free=0x%08x\n",
            patchedMalloc, patchedFree);
    }
}

static uint32_t patchArmFunctionsBySignature(CcArmRuntime* runtime,
    const uint32_t* signature, size_t wordCount, uint32_t occurrence,
    const char* importName);

static void patchCommonGraphicsRoutines(CcArmRuntime* runtime)
{
    static const uint32_t transparentBlitSignature[] = {
        0xe92d4018u, 0xe5901044u, 0xe590203cu, 0xe5903040u,
        0xe0411002u, 0xe5902048u, 0xe0522003u,
    };
    uint32_t start = runtime->package->origin;
    uint32_t end = start + runtime->package->prog_size;
    for (uint32_t address = start;
        address + sizeof(transparentBlitSignature) <= end; address += 4u)
    {
        uint8_t* code = resolveMemory(runtime, address,
            sizeof(transparentBlitSignature));
        if (!code || memcmp(code, transparentBlitSignature,
                sizeof(transparentBlitSignature)) != 0)
        {
            continue;
        }
        uint32_t thunk = createDynamicImport(runtime,
            "cc_internal_blit_transparent16");
        if (thunk && writeArmAbsoluteTailJump(runtime, address, thunk))
        {
            printf("cc-arm: graphics fast path transparent16=0x%08x\n",
                address);
        }
        else
        {
            printf("cc-arm: graphics fast path patch failed address=0x%08x thunk=0x%08x\n",
                address, thunk);
        }
        break;
    }

    static const uint32_t scaledBlitSignature[] = {
        0xe92d40f0u, 0xe5901054u, 0xe590204cu, 0xe3a05000u,
        0xe0416002u, 0xe5902050u, 0xe5901058u,
    };
    static const uint32_t scaledTransparentBlitSignature[] = {
        0xe92d4078u, 0xe5901054u, 0xe590204cu, 0xe3a04000u,
        0xe0415002u, 0xe5902050u, 0xe5901058u, 0xe0416002u,
    };
    uint32_t scaledTransparentPatched = patchArmFunctionsBySignature(runtime,
        scaledTransparentBlitSignature,
        sizeof(scaledTransparentBlitSignature) /
            sizeof(scaledTransparentBlitSignature[0]),
        0u, "cc_internal_blit_scaled_transparent16");
    uint32_t directPatched = 0;
    uint32_t scaledIndexedPatched = 0;
    for (uint32_t address = start;
        address + 0x44u <= end; address += 4u)
    {
        uint32_t* code = (uint32_t*)resolveMemory(runtime, address, 0x44u);
        if (!code || memcmp(code, scaledBlitSignature,
                sizeof(scaledBlitSignature)) != 0)
        {
            continue;
        }
        const char* importName = NULL;
        if (code[16] == 0xe590c02cu)
        {
            importName = "cc_internal_blit_scaled16";
        }
        else if (code[16] == 0xe590c038u)
        {
            importName = "cc_internal_blit_scaled_indexed16";
        }
        if (!importName) continue;
        uint32_t thunk = createDynamicImport(runtime, importName);
        if (!thunk || !writeArmAbsoluteTailJump(runtime, address, thunk))
        {
            continue;
        }
        if (code[16] == 0xe590c02cu) ++directPatched;
        else ++scaledIndexedPatched;
    }
    if (directPatched || scaledIndexedPatched || scaledTransparentPatched)
    {
        printf("cc-arm: graphics fast path scaled16=%u indexed16=%u "
            "transparent16=%u\n", directPatched, scaledIndexedPatched,
            scaledTransparentPatched);
    }

    static const uint32_t transparentBlendSignature[] = {
        0xe92d43f8u, 0xe1a04000u, 0xe594103cu, 0xe5900044u,
        0xe5942040u, 0xe0400001u, 0xe5941048u, 0xe0516002u,
    };
    static const uint32_t transparentHalfBlendSignature[] = {
        0xe92d4078u, 0xe5901044u, 0xe590203cu, 0xe5903040u,
        0xe0411002u, 0xe5902048u, 0xe0523003u, 0xe590201cu,
    };
    uint32_t blendedPatched = 0;
    blendedPatched += patchArmFunctionsBySignature(runtime,
        transparentBlendSignature,
        sizeof(transparentBlendSignature) /
            sizeof(transparentBlendSignature[0]),
        0u, "cc_internal_blit_transparent_blend1");
    blendedPatched += patchArmFunctionsBySignature(runtime,
        transparentBlendSignature,
        sizeof(transparentBlendSignature) /
            sizeof(transparentBlendSignature[0]),
        1u, "cc_internal_blit_transparent_blend2");
    blendedPatched += patchArmFunctionsBySignature(runtime,
        transparentBlendSignature,
        sizeof(transparentBlendSignature) /
            sizeof(transparentBlendSignature[0]),
        2u, "cc_internal_blit_transparent_blend3");
    blendedPatched += patchArmFunctionsBySignature(runtime,
        transparentBlendSignature,
        sizeof(transparentBlendSignature) /
            sizeof(transparentBlendSignature[0]),
        3u, "cc_internal_blit_transparent_blend4");
    blendedPatched += patchArmFunctionsBySignature(runtime,
        transparentHalfBlendSignature,
        sizeof(transparentHalfBlendSignature) /
            sizeof(transparentHalfBlendSignature[0]),
        0u, "cc_internal_blit_transparent_blend5");
    if (blendedPatched)
    {
        printf("cc-arm: graphics fast path transparent blends=%u\n",
            blendedPatched);
    }
}

static uint32_t patchArmFunctionsBySignature(CcArmRuntime* runtime,
    const uint32_t* signature, size_t wordCount, uint32_t occurrence,
    const char* importName)
{
    if (!signature || !wordCount || !importName) return 0;
    uint32_t start = runtime->package->origin;
    uint32_t end = start + runtime->package->prog_size;
    size_t byteCount = wordCount * sizeof(uint32_t);
    uint32_t thunk = 0;
    uint32_t patched = 0;
    uint32_t matched = 0;
    for (uint32_t address = start; address + byteCount <= end; address += 4u)
    {
        uint8_t* code = resolveMemory(runtime, address, byteCount);
        if (!code || memcmp(code, signature, byteCount) != 0)
        {
            continue;
        }
        if (matched++ != occurrence)
        {
            continue;
        }
        if (!thunk)
        {
            thunk = createDynamicImport(runtime, importName);
        }
        if (thunk && writeArmAbsoluteTailJump(runtime, address, thunk))
        {
            ++patched;
        }
        break;
    }
    return patched;
}

static void patchPortableCc1800GraphicsRoutines(CcArmRuntime* runtime)
{
    if (runtime->cc1800Compatibility) return;
    static const uint32_t normalizeVector3Fixed[] = {
        0xe92d4070u, 0xe1a06000u, 0xe5910000u, 0xe1a05001u,
        0xe1a01fc0u, 0xe5952004u, 0xe0200001u, 0xe0401001u,
    };
    uint32_t patched = 0;
    patched += patchArmFunctionsBySignature(runtime, normalizeVector3Fixed,
        sizeof(normalizeVector3Fixed) / sizeof(normalizeVector3Fixed[0]), 0u,
        "cc_internal_normalize_vec3_fixed");
    if (patched)
    {
        runtime->cc1800Compatibility = true;
        printf("cc-arm: portable CC1800 fast paths=%u\n", patched);
    }
}

static uint32_t patchCc1800IndexedGraphicsRoutines(CcArmRuntime* runtime)
{
    struct Patch
    {
        uint32_t address;
        uint32_t first;
        uint32_t second;
        const char* importName;
    };
    static const Patch patches[] = {
        { 0x101540e0u, 0xea011470u, 0xea011450u, "cc_internal_fast_copy" },
        { 0x101992a8u, 0xe92d4001u, 0xeb0000e7u, "cc_internal_fast_copy" },
        { 0x101992d0u, 0xe3a02000u, 0xe3510004u, "cc_internal_fast_fill_zero" },
        { 0x101992c0u, 0xe20230ffu, 0xe1832403u, "cc_internal_fast_fill_byte" },
        { 0x10165494u, 0xe92d0037u, 0xe5b03004u, "cc_internal_frame_copy" },
        { 0x1015696cu, 0xe92d07f0u, 0xe5901044u, "cc_internal_indexed_scaled" },
        { 0x10155d1cu, 0xe92d07f0u, 0xe5902034u, "cc_internal_indexed_copy" },
        { 0x10156308u, 0xe92d03f0u, 0xe5902034u, "cc_internal_indexed_transparent" },
        { 0x10157820u, 0xe92d0ff8u, 0xe5901034u, "cc_internal_indexed_alpha" },
        { 0x1015a724u, 0xe52d4004u, 0xe5904220u, "cc_internal_soft3d_fill32" },
        { 0x101819dcu, 0xe92d47f0u, 0xe1a06000u, kTransitionBlendImport },
    };
    uint32_t end = runtime->package->origin + runtime->package->prog_size;
    uint32_t patched = 0;
    for (size_t i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i)
    {
        const Patch& patch = patches[i];
        if (patch.address < runtime->package->origin || patch.address + 8u > end)
        {
            continue;
        }
        uint32_t* code = (uint32_t*)resolveMemory(runtime, patch.address, 8u);
        if (!code || code[0] != patch.first || code[1] != patch.second)
        {
            continue;
        }
        uint32_t thunk = createDynamicImport(runtime, patch.importName);
        if (thunk && writeArmAbsoluteTailJump(runtime, patch.address, thunk))
        {
            ++patched;
        }
    }
    const uint32_t spanBlockAddress = 0x1016f15cu;
    uint32_t* spanBlock = (uint32_t*)resolveMemory(runtime, spanBlockAddress,
        sizeof(uint32_t));
    if (spanBlock && *spanBlock == 0xe003bc55u &&
        writeArmInlineImport(runtime, spanBlockAddress, kTextureSpanBlockImport))
    {
        ++patched;
    }
    const uint32_t perspectiveSpanBlockAddress = 0x1016f2dcu;
    uint32_t* perspectiveSpanBlock = (uint32_t*)resolveMemory(runtime,
        perspectiveSpanBlockAddress, sizeof(uint32_t));
    if (perspectiveSpanBlock && *perspectiveSpanBlock == 0xe003bc55u &&
        writeArmInlineImport(runtime, perspectiveSpanBlockAddress,
            kTextureSpanBlockImport))
    {
        ++patched;
    }
    const uint32_t audioConvertBlockAddress = 0x10150a18u;
    uint32_t* audioConvertBlock = (uint32_t*)resolveMemory(runtime,
        audioConvertBlockAddress, sizeof(uint32_t));
    if (audioConvertBlock && *audioConvertBlock == 0xe4910004u &&
        writeArmInlineImport(runtime, audioConvertBlockAddress,
            kAudioConvertBlockImport))
    {
        ++patched;
    }
    const uint32_t perspectiveChunkAddress = 0x1016f060u;
    uint32_t* perspectiveChunk = (uint32_t*)resolveMemory(runtime,
        perspectiveChunkAddress, sizeof(uint32_t));
    if (perspectiveChunk && *perspectiveChunk == 0xe3a06000u &&
        writeArmInlineImport(runtime, perspectiveChunkAddress,
            kPerspectiveChunkSetupImport))
    {
        ++patched;
    }
    const uint32_t fill32PairsAddress = 0x10166260u;
    uint32_t* fill32Pairs = (uint32_t*)resolveMemory(runtime,
        fill32PairsAddress, sizeof(uint32_t));
    if (fill32Pairs && *fill32Pairs == 0xe5804004u &&
        writeArmInlineImport(runtime, fill32PairsAddress,
            kFill32PairsImport))
    {
        ++patched;
    }
    const uint32_t adpcmDecodeTailAddress = 0x101675dcu;
    uint32_t* adpcmDecodeTail = (uint32_t*)resolveMemory(runtime,
        adpcmDecodeTailAddress, sizeof(uint32_t));
    if (adpcmDecodeTail && *adpcmDecodeTail == 0xe1a04259u &&
        writeArmInlineImport(runtime, adpcmDecodeTailAddress,
            kAdpcmDecodeTailImport))
    {
        ++patched;
    }
    const uint32_t rowFill32PairsAddress = 0x10157ba4u;
    uint32_t* rowFill32Pairs = (uint32_t*)resolveMemory(runtime,
        rowFill32PairsAddress, sizeof(uint32_t));
    if (rowFill32Pairs && *rowFill32Pairs == 0xe1d095b4u &&
        writeArmInlineImport(runtime, rowFill32PairsAddress,
            kRowFill32PairsImport))
    {
        ++patched;
    }
    if (patched)
    {
        runtime->cc1800Compatibility = true;
        printf("cc-arm: CC1800 indexed graphics fast paths=%u\n", patched);
    }
    return patched;
}

static bool initializeRuntime(CcArmRuntime* runtime, const char* path)
{
    bool homebrewLayout = ccPackageUsesHomebrewLayout(runtime->package->origin);
    runtime->ramStart = homebrewLayout ? kCcHomebrewRamStart : kCcRetailRamStart;
    if (homebrewLayout)
    {
        uint64_t heapStart = ((uint64_t)runtime->package->origin +
            runtime->package->prog_size + 0xfffu) & ~0xfffull;
        if (heapStart > (uint64_t)kCcHomebrewRamStart + kCcHomebrewRamSize)
        {
            return false;
        }
        runtime->heapStart = kCcHomebrewHeapStart;
        runtime->systemMemory.resize(kCcHomebrewSystemRamSize);
        runtime->ram.resize(kCcHomebrewRamSize);
        runtime->heapMemory.resize(kCcHomebrewHeapSize);
    }
    else
    {
        runtime->heapStart = kCcRetailHeapStart;
        runtime->ram.resize(kCcRetailRamSize);
        runtime->heapMemory.resize(kCcRetailHeapSize);
    }
    runtime->stack.resize(kStackSize);
    runtime->framebuffer.resize(kCcVideoMemorySize);
    runtime->legacyMmio.resize(kLegacyMmioSize);
    runtime->framebufferAddress = kFramebufferAddress;
    runtime->framebufferBits = 16u;
    memcpy(runtime->ram.data() + runtime->package->origin - runtime->ramStart,
        runtime->package->bin_data, runtime->package->prog_size);
    if (homebrewLayout)
    {
        runtime->legacyLowMemory.resize(kLegacyLowMemorySize);
        runtime->legacyAudioMmio.resize(kLegacyAudioMmioSize);
        runtime->legacySystemMmio.resize(kLegacySystemMmioSize);
    }
    runtime->heapCursor = runtime->heapStart;
    if (homebrewLayout)
    {
        uint32_t statusOffset = kLegacyGraphicsStatus - kLegacySystemMmioStart;
        memcpy(runtime->legacySystemMmio.data() + statusOffset,
            &kLegacyGraphicsReady, sizeof(kLegacyGraphicsReady));
    }
    runtime->tasks.reserve(32);
    runtime->bus = { runtime, busFetch, busRead, busWrite, handleSvc };
    runtime->bus.directSystemRam = runtime->systemMemory.data();
    runtime->bus.directSystemRamBase = kCcHomebrewSystemRamStart;
    runtime->bus.directSystemRamSize =
        (uint32_t)runtime->systemMemory.size();
    runtime->bus.directRam = runtime->ram.data();
    runtime->bus.directRamBase = runtime->ramStart;
    runtime->bus.directRamSize = (uint32_t)runtime->ram.size();
    runtime->bus.directStack = runtime->stack.data();
    runtime->bus.directStackBase = kStackStart;
    runtime->bus.directStackSize = kStackSize;
    runtime->bus.directHeap = runtime->heapMemory.data();
    runtime->bus.directHeapBase = runtime->heapStart;
    runtime->bus.directHeapSize = runtime->legacySystemMmio.empty() ?
        (uint32_t)runtime->heapMemory.size() :
        kLegacySystemMmioStart - runtime->heapStart;
    runtime->bus.directProgramBase = runtime->package->origin;
    runtime->bus.directProgramSize = runtime->package->prog_size;
    runtime->bus.directThunkBase = kDynamicThunkStart;
    runtime->bus.directThunkSize = 0x10000u;
    patchLegacySdkAllocator(runtime);
    if (!s_compatibilityExecutionMode.load())
    {
        patchCommonGraphicsRoutines(runtime);
        patchCc1800IndexedGraphicsRoutines(runtime);
        patchPortableCc1800GraphicsRoutines(runtime);
    }
    else
    {
        printf("cc-arm: compatibility mode uses base ARM32 execution paths\n");
    }
    std::string fileName = gameFileNameFromPath(path ? path : "game.cc");
    std::string locale = ".\\" + fileName;
    busWrite(runtime, kLocaleString, locale.c_str(), locale.size() + 1);
    std::vector<uint16_t> widePath(locale.size() + 1);
    for (size_t i = 0; i < locale.size(); ++i)
    {
        widePath[i] = (uint8_t)locale[i];
    }
    busWrite(runtime, kAppPathWideString, widePath.data(), widePath.size() * sizeof(uint16_t));
    for (uint32_t i = 0; i < runtime->package->import_count; ++i)
    {
        if (!runtime->package->import_data[i]) continue;
        uint32_t stub[2] = { 0xef000000u | i, 0xe12fff1eu };
        size_t stubSize = homebrewLayout ?
            sizeof(stub[0]) : sizeof(stub);
        if (!busWrite(runtime, runtime->package->import_data[i]->offset,
                stub, stubSize)) return false;
    }
    runtime->instructionCache.resize((runtime->package->prog_size + 3u) / 4u);
    runtime->bus.instructionCache = runtime->instructionCache.data();
    runtime->bus.instructionCacheCount =
        (uint32_t)runtime->instructionCache.size();
    memset(framebufferPixels(), 0, VM_LCD_FB_SIZE);
    memset(runtime->framebuffer.data(), 0, runtime->framebuffer.size());
    return true;
}

bool ccArmRuntimeRunFile(const char* path,
    const std::vector<std::string>& enabledCheatFeatureKeys,
    CcArmRuntimeStats* stats)
{
    if (!path || !stats || s_running.exchange(true)) return false;
    framebufferSetTransientPartialProtectionEnabled(true);
    memset(stats, 0, sizeof(*stats));
    printf("cc-arm: timing runtime_scale=%.3f delay_scale=%.3f\n",
        s_runtimeSpeedScale.load(), s_hostDelayScale.load());
    FILE* file = platformOpenGameFile(path);
    if (!file)
    {
        snprintf(stats->error, sizeof(stats->error), "open failed");
        framebufferSetTransientPartialProtectionEnabled(false);
        s_running.store(false);
        return false;
    }

    std::vector<uint8_t> fileData;
    long fileSize = 0;
    if (fseek(file, 0, SEEK_END) == 0)
    {
        fileSize = ftell(file);
        if (fileSize > 0 && fseek(file, 0, SEEK_SET) != 0) fileSize = 0;
    }
    if (fileSize <= 0)
    {
        clearerr(file);
        uint8_t chunk[64 * 1024];
        while (true)
        {
            size_t count = fread(chunk, 1, sizeof(chunk), file);
            if (count) fileData.insert(fileData.end(), chunk, chunk + count);
            if (count < sizeof(chunk)) break;
        }
        fclose(file);
        file = NULL;
        if (fileData.empty() || fileData.size() > UINT32_MAX)
        {
            snprintf(stats->error, sizeof(stats->error), "invalid file size");
            framebufferSetTransientPartialProtectionEnabled(false);
            s_running.store(false);
            return false;
        }
        file = fmemopen(fileData.data(), fileData.size(), "rb");
        fileSize = (long)fileData.size();
    }
    if (!file || fileSize <= 0 || (uint64_t)fileSize > UINT32_MAX)
    {
        snprintf(stats->error, sizeof(stats->error), "invalid file size");
        if (file) fclose(file);
        framebufferSetTransientPartialProtectionEnabled(false);
        s_running.store(false);
        return false;
    }
    CcArmRuntime runtime = {};
    runtime.currentTaskIndex = UINT32_MAX;
    runtime.stats = stats;
    runtime.package = guestPackageCreate(file, (uint32_t)fileSize);
    fclose(file);
    bool retailLayout = runtime.package &&
        ccPackageUsesRetailLayout(runtime.package->origin);
    bool homebrewLayout = runtime.package &&
        ccPackageUsesHomebrewLayout(runtime.package->origin);
    uint32_t ramStart = homebrewLayout ? kCcHomebrewRamStart : kCcRetailRamStart;
    uint32_t ramSize = homebrewLayout ? kCcHomebrewRamSize : kCcRetailRamSize;
    if ((!retailLayout && !homebrewLayout) ||
        (uint64_t)runtime.package->origin + runtime.package->prog_size >
            (uint64_t)ramStart + ramSize ||
        !initializeRuntime(&runtime, path))
    {
        snprintf(stats->error, sizeof(stats->error), "unsupported ARM CCDL image");
        if (runtime.package) guestPackageDestroy(runtime.package);
        framebufferSetTransientPartialProtectionEnabled(false);
        s_running.store(false); return false;
    }
    runtime.startTime = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(s_runtimeMutex);
        s_activeRuntime = &runtime;
    }
    fsys_reset_guest_package(runtime.package);
    std::string gameSha256 = sha256Hex(runtime.package->file_data,
        runtime.package->file_size);
    fsys_set_game_identity(gameSha256.c_str());
    std::string gameName = gameFileNameFromPath(path);
    fsys_set_game_name(gameName.c_str());
    std::string saveDirectory = platformAndroidGetCcSaveDirectory(path, gameSha256);
    fsys_set_save_directory(saveDirectory.c_str());
    printf("cc-arm: save directory: %s\n", saveDirectory.c_str());
    cheatRuntimeLoadForGame(gameSha256.c_str(), path, enabledCheatFeatureKeys);
    cheatRuntimeBindMemory(&runtime, cheatReadCallback, cheatWriteCallback,
        cheatFlushCallback);
    uint32_t startupCheatApplyCount = cheatRuntimeApplyStartupBound();
    CheatRuntimeStatus cheatStatus = cheatRuntimeGetStatus();
    printf("cc-arm: game settings cheats_enabled=%u cheats_available=%u "
        "cheat_entries=%u cheat_startup_applied=%u app_sha256=%s\n",
        cheatStatus.enabled ? 1u : 0u, cheatStatus.available ? 1u : 0u,
        (unsigned int)cheatStatus.entries.size(), startupCheatApplyCount,
        gameSha256.c_str());

    Arm32State boot = {};
    arm32Reset(&boot, runtime.package->bin_entry, kExitAddress - 16u, kExitAddress);
    Arm32RunResult result;
    do
    {
        result = runState(&runtime, &boot);
    }
    while (result == ARM32_RUN_LIMIT && !s_stopRequested.load());
    Arm32State crashState = boot;
    uint32_t appMain = findExport(runtime.package, "AppMain");
    CcArmRuntime::Task mainTask = {};
    mainTask.entry = appMain;
    mainTask.argument = kAppPathWideString;
    mainTask.stack = kExitAddress - 16u;
    if (result != ARM32_RUN_OK || !appMain)
    {
        snprintf(stats->error, sizeof(stats->error), "boot failed result=%u pc=0x%08x", result, boot.r[15]);
    }
    else
    {
        runtime.tasks.push_back(mainTask);
        if (getenv("DINGOO_PIE_FORCE_GUEST_CRASH"))
        {
            CcArmRuntime::Task& task = runtime.tasks[0];
            arm32Reset(&task.state, task.entry, task.stack, kExitAddress);
            task.state.r[0] = task.argument;
            task.started = true;
            crashState = task.state;
            snprintf(stats->error, sizeof(stats->error),
                "forced guest crash for diagnostics");
            printf("cc-arm: forcing guest crash for diagnostics\n");
        }
        while (!s_stopRequested.load() && stats->error[0] == '\0')
        {
            pauseGateWaitForResume();
            bool anyActive = false;
            bool ranTask = false;
            size_t count = std::min<size_t>(runtime.tasks.size(), 32);
            for (int pass = 0; pass < 2 && !s_stopRequested.load(); ++pass)
            {
                const bool audioPass = pass == 0;
                for (size_t i = 0; i < count && !s_stopRequested.load(); ++i)
                {
                    CcArmRuntime::Task& task = runtime.tasks[i];
                    if (task.finished || task.audioProducer != audioPass) continue;
                    anyActive = true;
                    if (task.delayTicks)
                    {
                        uint32_t now = currentTaskSchedulerTick(&runtime,
                            task.audioProducer);
                        if ((int32_t)(now - task.delayTicks) < 0)
                        {
                            continue;
                        }
                        task.delayTicks = 0;
                    }
                    if (!task.started)
                    {
                        arm32Reset(&task.state, task.entry, task.stack, kExitAddress);
                        task.state.r[0] = task.argument;
                        task.started = true;
                    }
                    ranTask = true;
                    runtime.currentTaskIndex = (uint32_t)i;
                    result = runState(&runtime, &task.state,
                        task.audioProducer ? kSliceInstructions :
                        kNonAudioSliceInstructions);
                    runtime.currentTaskIndex = UINT32_MAX;
                    crashState = task.state;
                    task.audioProducer = task.audioProducer ||
                        runtime.tasks[i].audioProducer;
                    if (result == ARM32_RUN_OK) task.finished = true;
                    else if (result == ARM32_RUN_STOPPED && runtime.yielded)
                    {
                        uint32_t delayTicks = ccScaleDelayTicks(runtime.currentDelayTicks,
                            s_hostDelayScale.load());
                        task.delayTicks = currentTaskSchedulerTick(&runtime,
                            task.audioProducer) + delayTicks;
                    }
                    else if (result != ARM32_RUN_LIMIT)
                    {
                        stats->faultAddress = runtime.faultAddress;
                        stats->faultSize = runtime.faultSize;
                        stats->faultWrite = runtime.faultWrite;
                        stats->faultFetch = runtime.faultFetch;
                        stats->unsupportedPc = task.state.unsupportedPc;
                        stats->failedTaskIndex = (uint32_t)i;
                        stats->failedTaskEntry = task.entry;
                        stats->failedTaskStack = task.stack;
                        stats->failedTaskPriority = task.priority;
                        stats->failedTaskDelayTicks = task.delayTicks;
                        snprintf(stats->error, sizeof(stats->error),
                            "task failed result=%u pc=0x%08x insn=0x%08x sp=0x%08x lr=0x%08x "
                            "r4=0x%08x r5=0x%08x fault=%c%c0x%08x/%u import=%s",
                            result, task.state.r[15], task.state.unsupportedInstruction,
                            task.state.r[13], task.state.r[14], task.state.r[4], task.state.r[5],
                            runtime.faultFetch ? 'F' : '-', runtime.faultWrite ? 'W' : 'R',
                            runtime.faultAddress, runtime.faultSize, stats->lastImport);
                        break;
                    }
                }
                if (stats->error[0] != '\0') break;
            }
            if (!anyActive)
            {
                stats->guestCompleted = true;
                break;
            }
            if (!ranTask)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            profileCcRuntime(&runtime);
        }
    }

    bool ok = stats->error[0] == '\0';
    if (!ok)
    {
        CcCrashLogContext crashContext = {};
        crashContext.gamePath = path;
        crashContext.gameSha256 = gameSha256.c_str();
        crashContext.saveDirectory = saveDirectory.c_str();
        crashContext.error = stats->error;
        crashContext.registers = crashState.r;
        crashContext.cpsr = crashState.cpsr;
        crashContext.unsupportedInstruction = crashState.unsupportedInstruction;
        crashContext.unsupportedPc = stats->unsupportedPc;
        crashContext.faultAddress = stats->faultAddress;
        crashContext.faultSize = stats->faultSize;
        crashContext.faultWrite = stats->faultWrite;
        crashContext.faultFetch = stats->faultFetch;
        crashContext.lastImportPc = stats->lastImportPc;
        crashContext.lastImportReturnAddress = stats->lastImportReturnAddress;
        crashContext.failedTaskIndex = stats->failedTaskIndex;
        crashContext.failedTaskEntry = stats->failedTaskEntry;
        crashContext.failedTaskStack = stats->failedTaskStack;
        crashContext.failedTaskPriority = stats->failedTaskPriority;
        crashContext.failedTaskDelayTicks = stats->failedTaskDelayTicks;
        crashContext.instructions = stats->instructions;
        crashContext.importCalls = stats->importCalls;
        crashContext.unknownImports = stats->unknownImports;
        crashContext.framesSubmitted = stats->framesSubmitted;
        crashContext.tasksCreated = stats->tasksCreated;
        crashContext.lastImport = stats->lastImport;
        std::string crashLogFileName;
        if (crashLogWriteCcFailure(crashContext, &crashLogFileName))
        {
            printf("crash-log:wrote file=%s reason=cc-runtime-failure\n",
                crashLogFileName.c_str());
        }
        else
        {
            printf("crash-log:failed reason=cc-runtime-failure\n");
        }
    }
    for (size_t i = 0; i < runtime.openStreams.size(); ++i)
    {
        fsys_fclose(runtime.openStreams[i]);
    }
    cheatRuntimeUnbindMemory(&runtime);
    fsys_reset_guest_package(NULL);
    fsys_set_save_directory("");
    fsys_set_game_identity("");
    fsys_set_game_name("");
    guestPackageDestroy(runtime.package);
    {
        std::lock_guard<std::mutex> lock(s_runtimeMutex);
        s_activeRuntime = NULL;
    }
    framebufferSetTransientPartialProtectionEnabled(false);
    s_running.store(false);
    printf("cc-arm: stopped ok=%u instructions=%llu imports=%u unknown=%u frames=%u tasks=%u last=%s error=%s\n",
        ok ? 1u : 0u, (unsigned long long)stats->instructions, stats->importCalls,
        stats->unknownImports, stats->framesSubmitted, stats->tasksCreated,
        stats->lastImport[0] ? stats->lastImport : "(none)",
        stats->error[0] ? stats->error : "(none)");
    return ok;
}

void ccArmRuntimeRequestStop(void)
{
    s_stopRequested.store(true);
}

void ccArmRuntimeApplySettings(void)
{
    double runtimeScale = kAutoRuntimeSpeedScale;
    double delayScale = 1.0;
    double envScale = 1.0;
    if (parsePositiveScaleEnv("DINGOO_PIE_RUNTIME_SPEED_SCALE", &envScale))
    {
        runtimeScale = envScale;
    }
    if (parsePositiveScaleEnv("DINGOO_PIE_OSTIMEDLY_SCALE", &envScale))
    {
        delayScale = envScale;
    }
    s_runtimeSpeedScale.store(runtimeScale);
    s_hostDelayScale.store(delayScale);
    uint64_t targetIps = 0;
    const char* cpuClock = getenv("DINGOO_PIE_IRJIT_CLOCK_HZ");
    if (cpuClock && cpuClock[0])
    {
        char* end = NULL;
        unsigned long long parsed = strtoull(cpuClock, &end, 10);
        if (end != cpuClock && !*end && parsed > 0)
        {
            targetIps = ccCpuClockToTargetIps(parsed, kReferenceCpuClockHz,
                kReferenceInterpreterIps);
        }
    }
    s_targetInstructionsPerSecond.store(targetIps);
    const char* requestedBackend = getenv("DINGOO_PIE_BACKEND");
    RuntimeExecutionMode executionMode = runtimeExecutionModeFromName(requestedBackend, NULL);
    bool compatibilityMode = executionMode == RUNTIME_EXECUTION_MODE_COMPATIBILITY;
    s_compatibilityExecutionMode.store(compatibilityMode);
    printf("cc-arm: settings requested_backend=%s effective_backend=arm32_interpreter "
        "execution_mode=%s cpu_clock=%s target_ips=%llu runtime_scale=%.3f "
        "delay_scale=%.3f profile=%u\n",
        runtimeExecutionModeName(executionMode),
        compatibilityMode ? "base" : "optimized",
        cpuClock && cpuClock[0] ? cpuClock : "auto",
        (unsigned long long)targetIps, runtimeScale, delayScale,
        runtimeLogProfileEnabled() ? 1u : 0u);
}

void ccArmRuntimePrepareRun(void)
{
    ccArmRuntimeApplySettings();
    s_stopRequested.store(false);
}

bool ccArmRuntimeIsRunning(void)
{
    return s_running.load();
}
