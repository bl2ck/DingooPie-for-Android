#ifndef DINGOO_PIE_SHARED_GAME_GAME_RUNTIME_H
#define DINGOO_PIE_SHARED_GAME_GAME_RUNTIME_H

#include "config/settings/emulator_options.h"
#include "shared/game/game_paths.h"
#include "shared/save/save_state_slots.h"

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

bool gameRuntimeStart(
    const char* gamePath,
    const EmulatorOptions& options,
    const std::vector<std::string>& enabledCheatFeatureKeys);
bool gameRuntimeStop(void);
void gameRuntimeNotifyPauseRequested(void);
void gameRuntimeApplySettings(void);
GameFileFormat gameRuntimeActiveFileFormat(void);
uint32_t gameRuntimeActiveUnitCount(void);
void gameRuntimeCopyDiagnostics(char* identity, size_t identitySize,
    char* lastTask, size_t lastTaskSize, char* lastHle, size_t lastHleSize);
bool gameRuntimeWriteState(const std::string& gamePath, int slot,
    std::string* error, SaveStateProgressCallback progressCallback = 0,
    void* progressUserData = 0);
bool gameRuntimeReadState(const std::string& gamePath, int slot,
    std::string* error, SaveStateProgressCallback progressCallback = 0,
    void* progressUserData = 0);

#endif
