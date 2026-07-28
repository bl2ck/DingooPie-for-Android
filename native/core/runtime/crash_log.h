#ifndef DINGOO_PIE_RUNTIME_CRASH_LOG_H
#define DINGOO_PIE_RUNTIME_CRASH_LOG_H

#include "runtime/execution_backend.h"
#include "runtime/native_runtime.h"

#include <string>

struct CrashLogContext
{
    const char* appPath;
    const char* appMainPath;
    const char* appSha256;
    const char* compatProfile;
    ExecutionBackend backend;
    uint32_t appEntry;
    uint32_t bootEntry;
    uint32_t origin;
    uint32_t appSize;
};

struct CcCrashLogContext
{
    const char* gamePath;
    const char* gameSha256;
    const char* saveDirectory;
    const char* error;
    const uint32_t* registers;
    uint32_t cpsr;
    uint32_t unsupportedInstruction;
    uint64_t instructions;
    uint32_t importCalls;
    uint32_t unknownImports;
    uint32_t framesSubmitted;
    uint32_t tasksCreated;
    const char* lastImport;
};

bool crashLogWriteGuestFailure(
    NativeRuntime* runtime,
    RuntimeError err,
    const CrashLogContext& context,
    std::string* outFileName);

bool crashLogWriteCcFailure(
    const CcCrashLogContext& context,
    std::string* outFileName);

#endif
