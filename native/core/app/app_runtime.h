#ifndef DINGOO_PIE_APP_APP_RUNTIME_H
#define DINGOO_PIE_APP_APP_RUNTIME_H

#include "config/emulator_options.h"

#include <string>
#include <vector>

bool appRuntimeStart(
    const char* appPath,
    const EmulatorOptions& options,
    bool clearRecentOnStartupFailure,
    const std::vector<std::string>& enabledCheatFeatureKeys);
void appRuntimeApplySettings(void);
void appRuntimeStop(void);
void appRuntimeNotifyPauseRequested(void);

#endif
