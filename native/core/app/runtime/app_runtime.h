#ifndef DINGOO_PIE_APP_RUNTIME_APP_RUNTIME_H
#define DINGOO_PIE_APP_RUNTIME_APP_RUNTIME_H

#include "config/settings/emulator_options.h"
#include "app/runtime/app_runtime_state.h"

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
bool appRuntimeCaptureState(AppRuntimeState* out, std::string* error = 0);
bool appRuntimeRestoreState(const AppRuntimeState& state, std::string* error = 0);

#endif
