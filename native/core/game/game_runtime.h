#ifndef DINGOO_PIE_GAME_GAME_RUNTIME_H
#define DINGOO_PIE_GAME_GAME_RUNTIME_H

#include "config/emulator_options.h"

#include <string>
#include <vector>

bool gameRuntimeStart(
    const char* gamePath,
    const EmulatorOptions& options,
    bool clearRecentOnStartupFailure,
    const std::vector<std::string>& enabledCheatFeatureKeys);
void gameRuntimeStop(void);
void gameRuntimeNotifyPauseRequested(void);
void gameRuntimeApplySettings(void);

#endif
