#include "guest/guest_package.h"
#include "cc/arm32_interpreter.h"
#include "cc/cc_package_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>

#ifndef ARM_TEST_BENCHMARK_RUNS
#define ARM_TEST_BENCHMARK_RUNS 1000
#endif

#ifndef ARM_TEST_USE_INSTRUCTION_CACHE
#define ARM_TEST_USE_INSTRUCTION_CACHE 1
#endif

static const uint32_t kRamStart = kCcHomebrewProgramOrigin;
static const uint32_t kRamSize = 0x01000000u;
static const uint32_t kCcRetailRamStart = 0x10000000u;
static const uint32_t kCcRetailRamSize = 0x04000000u;
static const uint32_t kCcHomebrewSystemRamStart = 0x10000000u;
static const uint32_t kCcHomebrewSystemRamSize = 0x03800000u;
static const uint32_t kStackStart = 0x1ff00000u;
static const uint32_t kStackSize = 0x00100000u;
static const uint32_t kExitAddress = 0x1ffffffcu;

struct TestMemory
{
    std::vector<uint8_t> ram;
    std::vector<uint8_t> systemMemory;
    std::vector<uint8_t> stack;
};

struct TestContext
{
    TestMemory memory;
    GuestPackage* package;
    uint32_t ramStart;
    uint32_t ramSize;
    uint32_t heapCursor;
    std::vector<std::string> dynamicImports;
    uint32_t importCalls;
    char lastImport[96];
    uint32_t faultAddress;
    uint32_t faultSize;
    bool faultWrite;
    uint32_t recentPc[32];
    uint32_t recentPcIndex;
};

static uint8_t* resolve(TestContext* context, uint32_t address, size_t size)
{
    uint32_t systemOffset = address - kCcHomebrewSystemRamStart;
    if (!context->memory.systemMemory.empty() &&
        address >= kCcHomebrewSystemRamStart &&
        systemOffset < context->memory.systemMemory.size() &&
        size <= context->memory.systemMemory.size() - systemOffset)
    {
        return context->memory.systemMemory.data() + systemOffset;
    }
    uint32_t ramStart = context->ramStart ? context->ramStart : kRamStart;
    uint32_t ramSize = context->ramSize ? context->ramSize : kRamSize;
    uint32_t offset = address - ramStart;
    if (address >= ramStart && offset < ramSize && size <= ramSize - offset)
    {
        return context->memory.ram.data() + offset;
    }
    offset = address - kStackStart;
    if (address >= kStackStart && offset < kStackSize && size <= kStackSize - offset)
    {
        return context->memory.stack.data() + offset;
    }
    return NULL;
}

static bool readMemory(void* userData, uint32_t address, void* output, size_t size)
{
    TestContext* context = (TestContext*)userData;
    uint8_t* source = resolve(context, address, size);
    if (!source)
    {
        context->faultAddress = address;
        context->faultSize = (uint32_t)size;
        context->faultWrite = false;
        return false;
    }
    memcpy(output, source, size);
    return true;
}

static bool fetchMemory(void* userData, uint32_t address, void* output, size_t size)
{
    TestContext* context = (TestContext*)userData;
    context->recentPc[context->recentPcIndex++ % 32u] = address;
    uint32_t programOffset = address - context->package->origin;
    bool inProgram = address >= context->package->origin &&
        programOffset < context->package->prog_size &&
        size <= context->package->prog_size - programOffset;
    uint32_t thunkOffset = address - (kStackStart + 0x1000u);
    bool inThunks = address >= kStackStart + 0x1000u &&
        thunkOffset < 0x10000u && size <= 0x10000u - thunkOffset;
    if (!inProgram && !inThunks)
    {
        context->faultAddress = address;
        context->faultSize = (uint32_t)size;
        context->faultWrite = false;
        return false;
    }
    return readMemory(userData, address, output, size);
}

static bool writeMemory(void* userData, uint32_t address, const void* input, size_t size)
{
    TestContext* context = (TestContext*)userData;
    uint8_t* destination = resolve(context, address, size);
    if (!destination)
    {
        context->faultAddress = address;
        context->faultSize = (uint32_t)size;
        context->faultWrite = true;
        return false;
    }
    memcpy(destination, input, size);
    return true;
}

static bool readGuestString(TestContext* context, uint32_t address,
    char* output, size_t outputSize)
{
    if (!address || !output || outputSize < 2) return false;
    for (size_t i = 0; i < outputSize - 1; ++i)
    {
        uint8_t* value = resolve(context, address + (uint32_t)i, 1);
        if (!value) return false;
        output[i] = (char)*value;
        if (!output[i]) return true;
    }
    output[outputSize - 1] = '\0';
    return true;
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

static uint32_t allocateGuest(TestContext* context, uint32_t size)
{
    uint32_t aligned = (size + 15u) & ~15u;
    uint32_t ramStart = context->ramStart ? context->ramStart : kRamStart;
    uint32_t ramSize = context->ramSize ? context->ramSize : kRamSize;
    if (!aligned || context->heapCursor > ramStart + ramSize - aligned)
    {
        return 0;
    }
    uint32_t address = context->heapCursor;
    context->heapCursor += aligned;
    return address;
}

static bool handleSvc(void* userData, Arm32State* state, uint32_t immediate)
{
    TestContext* context = (TestContext*)userData;
    const char* name = NULL;
    if (immediate < context->package->import_count &&
        context->package->import_data[immediate])
    {
        name = context->package->import_data[immediate]->name;
    }
    else if (immediate >= context->package->import_count)
    {
        uint32_t dynamicIndex = immediate - context->package->import_count;
        if (dynamicIndex < context->dynamicImports.size())
        {
            name = context->dynamicImports[dynamicIndex].c_str();
        }
    }
    context->importCalls++;
    snprintf(context->lastImport, sizeof(context->lastImport), "%s",
        name ? name : "(invalid)");
    if (!name)
    {
        state->r[0] = 0;
        return true;
    }

    if (strcmp(name, "dl_get_proc") == 0)
    {
        char requested[96] = {};
        if (!readGuestString(context, state->r[1], requested, sizeof(requested)))
        {
            state->r[0] = 0;
            return true;
        }
        uint32_t address = findExport(context->package, requested);
        uint32_t importIndex = findImport(context->package, requested);
        if (!address && importIndex != UINT32_MAX)
        {
            address = context->package->import_data[importIndex]->offset;
        }
        if (!address)
        {
            uint32_t slot = (uint32_t)context->dynamicImports.size();
            context->dynamicImports.push_back(requested);
            address = kStackStart + 0x1000u + slot * 8u;
            uint32_t stub[2] = {
                0xef000000u | (context->package->import_count + slot),
                0xe12fff1eu
            };
            writeMemory(context, address, stub, sizeof(stub));
        }
        state->r[0] = address;
        snprintf(context->lastImport, sizeof(context->lastImport),
            "dl_get_proc:%s", requested);
        return true;
    }
    if (strcmp(name, "GetDLHandle") == 0 || strcmp(name, "get_dl_handle") == 0)
    {
        state->r[0] = kStackStart + 0x100u;
        return true;
    }
    if (strcmp(name, "__to_locale_ansi") == 0 || strcmp(name, "_to_locale_ansi") == 0)
    {
        static const char locale[] = "MINISYS.PLACEHOLDER";
        writeMemory(context, kStackStart + 0x200u, locale, sizeof(locale));
        state->r[0] = kStackStart + 0x200u;
        return true;
    }
    if (strcmp(name, "malloc") == 0 || strcmp(name, "OSMalloc") == 0 ||
        strcmp(name, "jmalloc") == 0)
    {
        state->r[0] = allocateGuest(context, state->r[0]);
        return true;
    }
    if (strcmp(name, "calloc") == 0)
    {
        uint64_t requested = (uint64_t)state->r[0] * state->r[1];
        state->r[0] = requested <= UINT32_MAX ?
            allocateGuest(context, (uint32_t)requested) : 0;
        if (state->r[0] && requested)
        {
            memset(resolve(context, state->r[0], (size_t)requested), 0,
                (size_t)requested);
        }
        return true;
    }
    if (strcmp(name, "memset") == 0)
    {
        uint8_t* destination = resolve(context, state->r[0], state->r[2]);
        if (destination) memset(destination, (int)(state->r[1] & 0xffu), state->r[2]);
        return true;
    }
    if (strcmp(name, "memcpy") == 0 || strcmp(name, "memmove") == 0)
    {
        uint8_t* destination = resolve(context, state->r[0], state->r[2]);
        uint8_t* source = resolve(context, state->r[1], state->r[2]);
        if (destination && source) memmove(destination, source, state->r[2]);
        return true;
    }
    if (strcmp(name, "OSTimeDly") == 0 || strcmp(name, "OSTimeDlyHMSM") == 0 ||
        strcmp(name, "delay") == 0)
    {
        return false;
    }
    if (strcmp(name, "OSTimeGet") == 0 || strcmp(name, "GetTickCount") == 0 ||
        strcmp(name, "OSTimerGetTickTimeus") == 0)
    {
        state->r[0] = 1;
        return true;
    }
    if (strcmp(name, "OSSemCreate") == 0)
    {
        state->r[0] = allocateGuest(context, 16);
        return true;
    }
    state->r[0] = 0;
    return true;
}

static bool testSignedLongMultiplyAndShift()
{
    TestContext context = {};
    context.memory.ram.resize(kRamSize);
    context.memory.stack.resize(kStackSize);
    const uint32_t instructions[] = {
        0xe0c12190u, // smull r2, r1, r0, r1
        0xe1a00641u, // asr r0, r1, #12
        0xe12fff1eu, // bx lr
    };
    memcpy(context.memory.ram.data(), instructions, sizeof(instructions));

    Arm32Bus bus = {};
    bus.userData = &context;
    bus.fetch = readMemory;
    bus.read = readMemory;
    bus.write = writeMemory;
    bus.svc = handleSvc;
    Arm32State state = {};
    const uint32_t stop = kRamStart + 0x100u;
    arm32Reset(&state, kRamStart, kStackStart + kStackSize - 16u, stop);
    state.r[0] = 0xfffffff0u;
    state.r[1] = 0x00001001u;
    int64_t expected = (int64_t)(int32_t)state.r[0] * (int32_t)state.r[1];
    Arm32RunResult result = arm32Run(&state, &bus, stop, 16u);
    return result == ARM32_RUN_OK && state.r[2] == (uint32_t)expected &&
        state.r[1] == (uint32_t)((uint64_t)expected >> 32) &&
        state.r[0] == (uint32_t)((int32_t)state.r[1] >> 12);
}

static bool testCountLeadingZeros()
{
    TestContext context = {};
    context.memory.ram.resize(kRamSize);
    context.memory.stack.resize(kStackSize);
    const uint32_t instructions[] = {
        0xe16f1f10u, // clz r1, r0
        0xe16f2f13u, // clz r2, r3
        0xe12fff1eu, // bx lr
    };
    memcpy(context.memory.ram.data(), instructions, sizeof(instructions));

    Arm32Bus bus = {};
    bus.userData = &context;
    bus.fetch = readMemory;
    bus.read = readMemory;
    bus.write = writeMemory;
    bus.svc = handleSvc;
    Arm32State state = {};
    const uint32_t stop = kRamStart + 0x100u;
    arm32Reset(&state, kRamStart, kStackStart + kStackSize - 16u, stop);
    state.r[0] = 0x00100000u;
    state.r[3] = 0u;
    Arm32RunResult result = arm32Run(&state, &bus, stop, 16u);
    return result == ARM32_RUN_OK && state.r[1] == 11u && state.r[2] == 32u;
}

static bool testSignedHalfwordMultiply()
{
    TestContext context = {};
    context.memory.ram.resize(kRamSize);
    context.memory.stack.resize(kStackSize);
    const uint32_t instructions[] = {
        0xe1630281u, // smulbb r3, r1, r2
        0xe1054281u, // smlabb r5, r1, r2, r4
        0xe12fff1eu, // bx lr
    };
    memcpy(context.memory.ram.data(), instructions, sizeof(instructions));

    Arm32Bus bus = {};
    bus.userData = &context;
    bus.fetch = readMemory;
    bus.read = readMemory;
    bus.write = writeMemory;
    bus.svc = handleSvc;
    Arm32State state = {};
    const uint32_t stop = kRamStart + 0x100u;
    arm32Reset(&state, kRamStart, kStackStart + kStackSize - 16u, stop);
    state.r[1] = 0x0000fffeu;
    state.r[2] = 3u;
    state.r[4] = 10u;
    Arm32RunResult result = arm32Run(&state, &bus, stop, 16u);
    return result == ARM32_RUN_OK && state.r[3] == 0xfffffffau &&
        state.r[5] == 4u;
}

static bool testDoublewordTransfer()
{
    TestContext context = {};
    context.memory.ram.resize(kRamSize);
    context.memory.stack.resize(kStackSize);
    const uint32_t instructions[] = {
        0xe1cd20f0u, // strd r2, r3, [sp]
        0xe3a02000u, // mov r2, #0
        0xe3a03000u, // mov r3, #0
        0xe1cd20d0u, // ldrd r2, r3, [sp]
        0xe12fff1eu, // bx lr
    };
    memcpy(context.memory.ram.data(), instructions, sizeof(instructions));

    Arm32Bus bus = {};
    bus.userData = &context;
    bus.directSystemRam = context.memory.systemMemory.data();
    bus.directSystemRamBase = kCcHomebrewSystemRamStart;
    bus.directSystemRamSize = (uint32_t)context.memory.systemMemory.size();
    bus.fetch = readMemory;
    bus.read = readMemory;
    bus.write = writeMemory;
    bus.svc = handleSvc;
    Arm32State state = {};
    const uint32_t stop = kRamStart + 0x100u;
    arm32Reset(&state, kRamStart, kStackStart + kStackSize - 16u, stop);
    state.r[2] = 0x12345678u;
    state.r[3] = 0x9abcdef0u;
    Arm32RunResult result = arm32Run(&state, &bus, stop, 16u);
    return result == ARM32_RUN_OK && state.r[2] == 0x12345678u &&
        state.r[3] == 0x9abcdef0u;
}

static bool testThumbLoadMultipleWithBaseInList()
{
    TestContext context = {};
    context.memory.ram.resize(kRamSize);
    context.memory.stack.resize(kStackSize);
    const uint16_t instructions[] = {
        0xca0cu, // ldm r2, {r2, r3}
        0x4770u, // bx lr
    };
    memcpy(context.memory.ram.data(), instructions, sizeof(instructions));
    const uint32_t values[] = { 0x11223344u, 0x55667788u };
    memcpy(context.memory.stack.data(), values, sizeof(values));

    Arm32Bus bus = {};
    bus.userData = &context;
    bus.fetch = readMemory;
    bus.read = readMemory;
    bus.write = writeMemory;
    bus.svc = handleSvc;
    Arm32State state = {};
    const uint32_t stop = kRamStart + 0x100u;
    arm32Reset(&state, kRamStart | 1u, kStackStart + kStackSize - 16u, stop | 1u);
    state.r[2] = kStackStart;
    Arm32RunResult result = arm32Run(&state, &bus, stop & ~1u, 16u);
    return result == ARM32_RUN_OK && state.r[2] == values[0] && state.r[3] == values[1];
}

static bool testInstructionCacheInvalidation()
{
    TestContext context = {};
    context.memory.ram.resize(kRamSize);
    context.memory.stack.resize(kStackSize);
    const uint32_t initialInstructions[] = {
        0xe3a00001u, // mov r0, #1
        0xe12fff1eu, // bx lr
    };
    memcpy(context.memory.ram.data(), initialInstructions,
        sizeof(initialInstructions));

    Arm32InstructionCacheEntry cache[2] = {};
    Arm32Bus bus = {};
    bus.userData = &context;
    bus.fetch = readMemory;
    bus.read = readMemory;
    bus.write = writeMemory;
    bus.svc = handleSvc;
    bus.directProgramBase = kRamStart;
    bus.directProgramSize = sizeof(initialInstructions);
    bus.instructionCache = cache;
    bus.instructionCacheCount = 2u;

    Arm32State state = {};
    const uint32_t stop = kRamStart + 0x100u;
    arm32Reset(&state, kRamStart, kStackStart + kStackSize - 16u, stop);
    Arm32RunResult first = arm32Run(&state, &bus, stop, 16u);
    if (first != ARM32_RUN_OK || state.r[0] != 1u) return false;

    const uint32_t branchToLink = 0xe12fff1eu;
    memcpy(context.memory.ram.data(), &branchToLink, sizeof(branchToLink));
    arm32Reset(&state, kRamStart, kStackStart + kStackSize - 16u, stop);
    state.r[0] = 7u;
    Arm32RunResult second = arm32Run(&state, &bus, stop, 16u);
    return second == ARM32_RUN_OK && state.r[0] == 7u;
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: cc_arm_interpreter_test <game.cc>\n");
        return 2;
    }
    if (!testSignedLongMultiplyAndShift())
    {
        fprintf(stderr, "SMULL/ASR interpreter test failed\n");
        return 7;
    }
    if (!testCountLeadingZeros())
    {
        fprintf(stderr, "CLZ interpreter test failed\n");
        return 12;
    }
    if (!testSignedHalfwordMultiply())
    {
        fprintf(stderr, "SMULxy/SMLAxy interpreter test failed\n");
        return 10;
    }
    if (!testDoublewordTransfer())
    {
        fprintf(stderr, "LDRD/STRD interpreter test failed\n");
        return 8;
    }
    if (!testThumbLoadMultipleWithBaseInList())
    {
        fprintf(stderr, "Thumb LDM base-in-list interpreter test failed\n");
        return 9;
    }
    if (!testInstructionCacheInvalidation())
    {
        fprintf(stderr, "instruction cache invalidation test failed\n");
        return 11;
    }
    FILE* file = fopen(argv[1], "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) return 3;
    long size = ftell(file);
    if (size <= 0 || fseek(file, 0, SEEK_SET) != 0) return 4;
    GuestPackage* package = guestPackageCreate(file, (uint32_t)size);
    fclose(file);
    if (!package) return 5;

    TestContext context = {};
    context.package = package;
    context.ramStart = ccPackageUsesHomebrewLayout(package->origin) ?
        kRamStart : kCcRetailRamStart;
    context.ramSize = ccPackageUsesHomebrewLayout(package->origin) ?
        kRamSize : kCcRetailRamSize;
    context.heapCursor = (package->origin + package->prog_size + 15u) & ~15u;
    if (ccPackageUsesHomebrewLayout(package->origin))
    {
        context.memory.systemMemory.resize(kCcHomebrewSystemRamSize);
    }
    context.memory.ram.resize(context.ramSize);
    context.memory.stack.resize(kStackSize);
    memcpy(context.memory.ram.data() + package->origin - context.ramStart,
        package->bin_data, package->prog_size);

    Arm32Bus bus = {};
    bus.userData = &context;
    bus.fetch = readMemory;
    bus.read = readMemory;
    bus.write = writeMemory;
    bus.svc = handleSvc;
    bus.directSystemRam = context.memory.systemMemory.data();
    bus.directSystemRamBase = kCcHomebrewSystemRamStart;
    bus.directSystemRamSize = (uint32_t)context.memory.systemMemory.size();
    bus.directRam = context.memory.ram.data();
    bus.directRamBase = context.ramStart;
    bus.directRamSize = context.ramSize;
    bus.directStack = context.memory.stack.data();
    bus.directStackBase = kStackStart;
    bus.directStackSize = kStackSize;
    std::vector<Arm32InstructionCacheEntry> instructionCache;
    if (ARM_TEST_USE_INSTRUCTION_CACHE)
    {
        instructionCache.resize((package->prog_size + 3u) / 4u);
        bus.directProgram = context.memory.ram.data() +
            package->origin - context.ramStart;
        bus.directProgramBase = package->origin;
        bus.directProgramSize = package->prog_size;
        bus.instructionCache = instructionCache.data();
        bus.instructionCacheCount = (uint32_t)instructionCache.size();
    }
    Arm32State state;
    arm32Reset(&state, package->bin_entry,
        kStackStart + kStackSize - 16u, kExitAddress);
    Arm32RunResult result = arm32Run(&state, &bus, kExitAddress, 1000000);
    printf("result=%u instructions=%llu pc=0x%08X cpsr=0x%08X "
        "unsupported_pc=0x%08X unsupported=0x%08X\n",
        (unsigned)result, (unsigned long long)state.instructions,
        state.r[15], state.cpsr, state.unsupportedPc,
        state.unsupportedInstruction);

    const uint32_t benchmarkRuns = ARM_TEST_BENCHMARK_RUNS;
    uint64_t benchmarkInstructions = 0;
    timespec beginTime = {};
    timespec endTime = {};
    clock_gettime(CLOCK_MONOTONIC, &beginTime);
    for (uint32_t i = 0; i < benchmarkRuns && result == ARM32_RUN_OK; ++i)
    {
        arm32Reset(&state, package->bin_entry,
            kStackStart + kStackSize - 16u, kExitAddress);
        result = arm32Run(&state, &bus, kExitAddress, 1000000);
        benchmarkInstructions += state.instructions;
    }
    clock_gettime(CLOCK_MONOTONIC, &endTime);
    uint64_t elapsedNanos =
        (uint64_t)(endTime.tv_sec - beginTime.tv_sec) * 1000000000ull +
        (uint64_t)(endTime.tv_nsec - beginTime.tv_nsec);
    uint64_t instructionsPerSecond = elapsedNanos ?
        benchmarkInstructions * 1000000000ull / elapsedNanos : 0;
    printf("benchmark_runs=%u benchmark_instructions=%llu elapsed_us=%llu ips=%llu\n",
        benchmarkRuns, (unsigned long long)benchmarkInstructions,
        (unsigned long long)(elapsedNanos / 1000ull),
        (unsigned long long)instructionsPerSecond);

    uint32_t appMain = findExport(package, "AppMain");
    bus.fetch = fetchMemory;
    for (uint32_t i = 0; i < package->import_count; ++i)
    {
        if (!package->import_data[i]) continue;
        uint32_t stub[2] = { 0xef000000u | i, 0xe12fff1eu };
        writeMemory(&context, package->import_data[i]->offset, stub, sizeof(stub));
    }
    printf("appmain_probe_start=0x%08X\n", appMain);
    fflush(stdout);
    arm32Reset(&state, appMain, kStackStart + kStackSize - 16u, kExitAddress);
    state.r[0] = kStackStart;
    Arm32RunResult appMainResult = arm32Run(&state, &bus, kExitAddress, 1000000);
    printf("appmain=0x%08X result=%u instructions=%llu pc=0x%08X cpsr=0x%08X "
        "unsupported_pc=0x%08X unsupported=0x%08X\n",
        appMain, (unsigned)appMainResult,
        (unsigned long long)state.instructions, state.r[15], state.cpsr,
        state.unsupportedPc, state.unsupportedInstruction);
    printf("appmain_imports=%u last_import=%s dynamic_imports=%u\n",
        context.importCalls, context.lastImport[0] ? context.lastImport : "(none)",
        (unsigned)context.dynamicImports.size());
    printf("appmain_fault=%s:0x%08X/%u r0=0x%08X r1=0x%08X r2=0x%08X "
        "r3=0x%08X sp=0x%08X lr=0x%08X\n",
        context.faultWrite ? "write" : "read", context.faultAddress,
        context.faultSize, state.r[0], state.r[1], state.r[2], state.r[3],
        state.r[13], state.r[14]);
    printf("recent_pc=");
    uint32_t traceCount = context.recentPcIndex < 32u ? context.recentPcIndex : 32u;
    uint32_t traceStart = context.recentPcIndex >= 32u ? context.recentPcIndex % 32u : 0u;
    for (uint32_t i = 0; i < traceCount; ++i)
    {
        printf("%s%08X", i ? "," : "", context.recentPc[(traceStart + i) % 32u]);
    }
    printf("\n");
    guestPackageDestroy(package);
    return result == ARM32_RUN_OK ? 0 : 6;
}
