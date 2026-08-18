#ifndef DINGOO_PIE_APP_RUNTIME_APP_RUNTIME_DEBUG_H
#define DINGOO_PIE_APP_RUNTIME_APP_RUNTIME_DEBUG_H

#include "app/cpu/mips_runtime.h"

#include <stdint.h>
#include <stdio.h>

const char* appRuntimeMemoryAccessName(RuntimeMemoryAccess type);
void appRuntimeDebugReportInvalidMemory(NativeRuntime* runtime,
    RuntimeMemoryAccess type, uint64_t address, int size, int64_t value);
void appRuntimeDebugDumpStack(NativeRuntime* runtime, uint32_t stackStartAddress);
void appRuntimeDebugDumpRegisters(NativeRuntime* runtime);
void appRuntimeDebugDumpRegistersToFile(NativeRuntime* runtime, FILE* file);
void appRuntimeDebugDumpReturnDisassembly(NativeRuntime* runtime);
void appRuntimeDebugDumpMemory(const void* buffer, uint32_t count);

#endif
