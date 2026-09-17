#ifndef DINGOO_PIE_FRONTEND_SHELL_H
#define DINGOO_PIE_FRONTEND_SHELL_H

#include "config/settings/emulator_options.h"
#include "config/settings/emulator_settings.h"

#include <stdint.h>

bool frontendInit(EmulatorSettings* settings, const char* currentGamePath);
void frontendRunLoop(const EmulatorOptions& options);
void frontendSetCurrentGamePath(const char* gamePath);
void frontendPrepareForGameLaunch(void);
void frontendToggleGameLibraryLayout(void);
void frontendRefreshGameLibrary(void);
void frontendNotifyGameStarted(const char* gamePath);
bool frontendConsumeGameLaunchRequest(std::string* outPath);
void frontendRequestQuit(void);
void frontendRequestGameExit(void);
bool frontendGameExitRequested(void);
bool frontendQuitRequested(void);
void frontendShutdown(void);
bool frontendGameRunning(void);
void frontendSetGameRunning(bool running);
bool frontendGamePaused(void);
bool frontendUserGamePaused(void);
void frontendSetGamePaused(bool paused);
void frontendClearPauseRequests(void);
void frontendNotifyAndroidBackground(bool backgrounded);
void frontendApplyVideoSettings(const EmulatorSettings& settings);
void frontendApplyAudioSettings(const EmulatorSettings& settings);
void frontendApplyInputSettings(const EmulatorSettings& settings);
void frontendBeginControllerMapping(uint32_t controlBit);
std::string frontendControllerSourceForControl(uint32_t controlBit);
bool frontendRunCheatManagerAutomation(void);
bool frontendRunCheatManagerFileSwitchAutomation(void);

void updateFb(void);

#endif
