#ifndef DINGOO_PIE_CC_CC_ARM_RUNTIME_H
#define DINGOO_PIE_CC_CC_ARM_RUNTIME_H

#include <stdint.h>
#include <string>
#include <vector>

struct CcRuntimeState;

struct CcArmRuntimeStats
{
    uint64_t instructions;
    uint32_t importCalls;
    uint32_t unknownImports;
    uint32_t framesSubmitted;
    uint32_t tasksCreated;
    uint32_t faultAddress;
    uint32_t faultSize;
    uint32_t unsupportedPc;
    uint32_t lastImportPc;
    uint32_t lastImportReturnAddress;
    uint32_t failedTaskIndex;
    uint32_t failedTaskEntry;
    uint32_t failedTaskStack;
    uint32_t failedTaskPriority;
    uint32_t failedTaskDelayTicks;
    bool faultWrite;
    bool faultFetch;
    bool guestCompleted;
    char lastImport[96];
    char error[160];
};

// CC uses the persisted runtime controls shared with APP, but executes through
// an isolated ARM32 interpreter so APP backend behavior remains unchanged.
bool ccArmRuntimeRunFile(const char* path,
    const std::vector<std::string>& enabledCheatFeatureKeys,
    CcArmRuntimeStats* stats);
void ccArmRuntimeApplySettings(void);
void ccArmRuntimePrepareRun(void);
void ccArmRuntimeRequestStop(void);
bool ccArmRuntimeIsRunning(void);
uint32_t ccArmRuntimeActiveTaskCount(void);
bool ccArmRuntimeCaptureState(CcRuntimeState* out, std::string* error = 0);
bool ccArmRuntimeRestoreState(const CcRuntimeState& state, std::string* error = 0);

#endif
