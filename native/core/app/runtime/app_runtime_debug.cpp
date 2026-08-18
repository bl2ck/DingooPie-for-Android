#include "app/runtime/app_runtime_debug.h"
#include "shared/diagnostics/debug_log.h"
#include <inttypes.h>
#include <time.h>
#include "app/memory/app_memory.h"
#include <capstone/capstone.h>
#include <vector>

void appRuntimeDebugDumpRegistersToFile(NativeRuntime* runtime, FILE* fp);

const char* appRuntimeMemoryAccessName(RuntimeMemoryAccess type)
{
    // clang-format off
    switch (type)
    {
    case RUNTIME_MEM_READ:return "RUNTIME_MEM_READ";
    case RUNTIME_MEM_WRITE:return "RUNTIME_MEM_WRITE";
    case RUNTIME_MEM_FETCH:return "RUNTIME_MEM_FETCH";
    case RUNTIME_MEM_READ_UNMAPPED:return "RUNTIME_MEM_READ_UNMAPPED";
    case RUNTIME_MEM_WRITE_UNMAPPED:return "RUNTIME_MEM_WRITE_UNMAPPED";
    case RUNTIME_MEM_FETCH_UNMAPPED:return "RUNTIME_MEM_FETCH_UNMAPPED";
    case RUNTIME_MEM_WRITE_PROT:return "RUNTIME_MEM_WRITE_PROT";
    case RUNTIME_MEM_READ_PROT:return "RUNTIME_MEM_READ_PROT";
    case RUNTIME_MEM_FETCH_PROT:return "RUNTIME_MEM_FETCH_PROT";
    case RUNTIME_MEM_READ_AFTER:return "RUNTIME_MEM_READ_AFTER";
    }
    // clang-format on
    return "<error type>";
}

void appRuntimeDebugReportInvalidMemory(NativeRuntime* runtime,
    RuntimeMemoryAccess type, uint64_t address, int size, int64_t value)
{
    FILE* logFile = debugLogFile();
    fprintf(logFile,
        "app-runtime: invalid memory access type=%s address=0x%" PRIx64
        " size=0x%x value=0x%" PRIx64 "\n",
        appRuntimeMemoryAccessName(type), address, size, value);
    appRuntimeDebugDumpRegistersToFile(runtime, logFile);

    if (logFile != stdout)
    {
        printf("app-runtime: invalid memory access type=%s address=0x%" PRIx64
            " size=0x%x value=0x%" PRIx64
            "; details written to debug log\n",
            appRuntimeMemoryAccessName(type), address, size, value);
    }
    appRuntimeDebugDumpReturnDisassembly(runtime);
}

void appRuntimeDebugDumpStack(NativeRuntime* runtime, uint32_t stackStartAddress)
{
    if (!runtime)
    {
        printf("debug: no runtime for stack dump\n");
        return;
    }

    uint32_t v = 0;
    printf("==========================STACK=================================\n");
    if (nativeRuntimeReadRegister(runtime, RUNTIME_REG_SP, &v) != RUNTIME_OK)
    {
        printf("debug: failed to read SP\n");
        printf("==============================================================\n");
        return;
    }
    if (stackStartAddress < v)
    {
        printf("debug: invalid stack range sp=0x%08x end=0x%08x\n", v, stackStartAddress);
        printf("==============================================================\n");
        return;
    }

    uint64_t stackBytes = (uint64_t)stackStartAddress - v;
    for (uint64_t offset = 0; offset < stackBytes && (uint64_t)v + offset <= 0xffffffffull; offset += 4u)
    {
        if ((offset % 16u) == 0)
        {
            if (offset)
            {
                printf("\n");
            }
            printf("0x%08x:\t", (uint32_t)((uint64_t)v + offset));
        }

        uint32_t word = 0;
        if (nativeRuntimeReadMemory(runtime, (uint32_t)((uint64_t)v + offset), &word, sizeof(word)) == RUNTIME_OK)
        {
            printf("%08x ", word);
        }
        else
        {
            printf("???????? ");
        }
    }
    printf("\n");

    printf("==============================================================\n");
}

void appRuntimeDebugDumpRegisters(NativeRuntime* runtime)
{
    appRuntimeDebugDumpRegistersToFile(runtime, stdout);
}

void appRuntimeDebugDumpRegistersToFile(NativeRuntime* runtime, FILE* fp)
{
    if (!fp)
    {
        fp = stdout;
    }

    auto regValue = [runtime](int reg) -> uint32_t {
        uint32_t v = 0;
        if (runtime)
        {
            nativeRuntimeReadRegister(runtime, reg, &v);
        }
        return v;
    };

    fprintf(fp, "==========================REG=================================\n");
    fprintf(fp, "AT=%08X\t", regValue(RUNTIME_REG_AT));
    fprintf(fp, "V0=%08X\t", regValue(RUNTIME_REG_V0));
    fprintf(fp, "V1=%08X\t\n", regValue(RUNTIME_REG_V1));

    fprintf(fp, "A0=%08X\t", regValue(RUNTIME_REG_A0));
    fprintf(fp, "A1=%08X\t", regValue(RUNTIME_REG_A1));
    fprintf(fp, "A2=%08X\t", regValue(RUNTIME_REG_A2));
    fprintf(fp, "A3=%08X\t\n", regValue(RUNTIME_REG_A3));

    fprintf(fp, "S0=%08X\t", regValue(RUNTIME_REG_S0));
    fprintf(fp, "S1=%08X\t", regValue(RUNTIME_REG_S1));
    fprintf(fp, "S2=%08X\t", regValue(RUNTIME_REG_S2));
    fprintf(fp, "S3=%08X\t\n", regValue(RUNTIME_REG_S3));
    fprintf(fp, "S4=%08X\t", regValue(RUNTIME_REG_S4));
    fprintf(fp, "S5=%08X\t", regValue(RUNTIME_REG_S5));
    fprintf(fp, "S6=%08X\t", regValue(RUNTIME_REG_S6));
    fprintf(fp, "S7=%08X\t\n", regValue(RUNTIME_REG_S7));

    fprintf(fp, "LO=%08X\t", regValue(RUNTIME_REG_LO));
    fprintf(fp, "HI=%08X\t\n", regValue(RUNTIME_REG_HI));

    fprintf(fp, "PC=%08X\t", regValue(RUNTIME_REG_PC));
    fprintf(fp, "SP=%08X\t", regValue(RUNTIME_REG_SP));
    fprintf(fp, "FP=%08X\t", regValue(RUNTIME_REG_FP));
    fprintf(fp, "RA=%08X\t\n", regValue(RUNTIME_REG_RA));
    fprintf(fp, "==============================================================\n");
}

static bool appRuntimeDebugOpenDisassembler(csh* handle)
{
    if (!handle || cs_open(CS_ARCH_MIPS, CS_MODE_MIPS32, handle) != CS_ERR_OK)
    {
        printf("debug: cs_open failed\n");
        return false;
    }
    return true;
}

static void appRuntimeDebugDumpOneInstruction(csh handle, NativeRuntime* runtime, uint32_t address)
{
    cs_insn* insn = NULL;
    uint32_t binary = 0;
    const uint32_t size = 4;

    if (!runtime || nativeRuntimeReadMemory(runtime, address, &binary, size) != RUNTIME_OK)
    {
        printf("%08X:    --------  -----------unmapped----------- \n", address);
        return;
    }

    size_t count = cs_disasm(handle, (uint8_t*)&binary, size, address, 1, &insn);
    if (count > 0)
    {
        for (size_t j = 0; j < count; j++)
        {
            printf("%08X:    %08x    %s\t%s\n", address, binary, insn[j].mnemonic, insn[j].op_str);
        }
        cs_free(insn, count);
    }
    else
    {
        printf("%08X:    %08x  -----------disasm-error----------- \n", address, binary);
    }
}

void appRuntimeDebugDumpReturnDisassembly(NativeRuntime* runtime)
{
    uint32_t ra = 0;
    printf("==========================DISASM==============================\n");
    if (!runtime || nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &ra) != RUNTIME_OK)
    {
        printf("debug: failed to read RA\n");
        printf("==============================================================\n");
        return;
    }

    csh handle = 0;
    if (!appRuntimeDebugOpenDisassembler(&handle))
    {
        printf("==============================================================\n");
        return;
    }

    uint64_t address = ra >= 256u ? (uint64_t)ra - 256u : 0;
    uint64_t end = (uint64_t)ra + 4u;
    if (end > 0x100000000ull)
    {
        end = 0x100000000ull;
    }
    while (address < end)
    {
        appRuntimeDebugDumpOneInstruction(handle, runtime, (uint32_t)address);
        address += 4u;
    }
    cs_close(&handle);

    printf("==============================================================\n");
}

void appRuntimeDebugDumpMemory(const void* buffer, uint32_t count)
{
    const uint8_t* data = (const uint8_t*)buffer;
    for (int i = 0; i < count; ++i)
    {
        printf("%02x ", data[i]);
        if ((i + 1)%16 == 0)
        {
            printf("\n");
        }
    }

    printf("\n");
}
