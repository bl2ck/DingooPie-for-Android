#ifndef DINGOO_PIE_RUNTIME_EXECUTION_BACKEND_H
#define DINGOO_PIE_RUNTIME_EXECUTION_BACKEND_H

enum ExecutionBackend
{
    EXECUTION_BACKEND_COMPATIBILITY = 0,
    EXECUTION_BACKEND_PPSSPP_IRJIT = 1
};

enum RuntimeExecutionMode
{
    RUNTIME_EXECUTION_MODE_AUTOMATIC = 0,
    RUNTIME_EXECUTION_MODE_COMPATIBILITY,
    RUNTIME_EXECUTION_MODE_COUNT
};

const char* executionBackendName(ExecutionBackend backend);
ExecutionBackend executionBackendFromName(const char* value, bool* recognized);
const char* runtimeExecutionModeName(RuntimeExecutionMode mode);
const char* runtimeExecutionModeConfigValue(RuntimeExecutionMode mode);
RuntimeExecutionMode runtimeExecutionModeFromName(const char* value, bool* recognized);

#endif
