#ifndef DINGOO_PIE_APP_APP_MIPS_RUNTIME_H
#define DINGOO_PIE_APP_APP_MIPS_RUNTIME_H

#include "config/emulator_options.h"
#include "app/emulator_core.h"

#include <stdint.h>
#include <string>
#include <vector>

bool appRuntimeStart(
    const char* appPath,
    const EmulatorOptions& options,
    const std::vector<std::string>& enabledCheatFeatureKeys);
void appRuntimeApplySettings(void);
bool appRuntimeStop(void);
void appRuntimeNotifyPauseRequested(void);
uint32_t appRuntimeActiveThreadCount(void);
bool appRuntimeCaptureState(EmulatorRuntimeState* out, std::string* error = 0);
bool appRuntimeRestoreState(const EmulatorRuntimeState& state, std::string* error = 0);

#endif
