#include "game/game_runtime.h"
#include "game/game_paths.h"
#include "config/cheat_runtime.h"
#include "runtime/debug_log.h"
#include "config/emulator_options.h"
#include "config/emulator_settings.h"
#include "runtime/runtime_log.h"
#include "frontend/sdl_audio.h"
#include "frontend/sdl_frontend.h"
#include "platform_services.h"

#include <stdio.h>
#include <stdlib.h>
#include <chrono>
#include <string>
#include <string.h>
#include <thread>
#include <unistd.h>

static void waitForInitialCheatLoad(uint32_t previousRevision)
{
    for (int i = 0; i < 100; ++i)
    {
        if (cheatRuntimeRevision() != previousRevision)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

static void applyStartupDebugSettings(EmulatorSettings* settings, bool externalDebugLog)
{
    if (!settings)
    {
        return;
    }

    if (settings->debugProfile || externalDebugLog)
    {
        if (!debugLogOpen() && settings->debugProfile)
        {
            settings->debugProfile = false;
            runtimeLogSetProfileEnabled(false);
            printf("main: performance log disabled for this run because the debug log could not be opened\n");
        }
    }
}

static void shutdownApplication(void)
{
    frontendShutdown();
    platformAndroidRequestApplicationExit();
    mixerPrepareApplicationExit();
}

static bool stopGameRuntimeForTransition(void)
{
    const int retryCount = frontendGameExitRequested() ? 1 : 0;
    for (int attempt = 0; attempt <= retryCount; ++attempt)
    {
        if (gameRuntimeStop())
        {
            return true;
        }
    }
    return false;
}

static void exitAfterRuntimeStopTimeout(void)
{
    printf("main: runtime stop timed out; requesting controlled process exit\n");
    platformAndroidRequestApplicationExit();
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    _exit(1);
}

extern "C" int SDL_main(int, char*[])
{
    std::string logDirectory = platformAndroidGetLogDirectory();
    if (!logDirectory.empty())
    {
        std::string stdoutPath = logDirectory + "/dingoopie-native.log";
        std::string stderrPath = logDirectory + "/dingoopie-native.err";
        freopen(stdoutPath.c_str(), "w", stdout);
        freopen(stderrPath.c_str(), "w", stderr);
    }
    bool externalDebugLog = emulatorEnvEnabled("DINGOO_PIE_LOG_FILE");
    if (externalDebugLog)
    {
        debugLogOpen();
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    EmulatorSettings settings = emulatorLoadSettings();
    runtimeLogInitialize(settings.debugProfile, runtimeLogEnvEnabled("DINGOO_PIE_PROFILE"));
    applyStartupDebugSettings(&settings, externalDebugLog);
    printf("main: settings loaded debug.profile=%u external_log=%u\n",
        settings.debugProfile ? 1u : 0u,
        externalDebugLog ? 1u : 0u);
    if (settings.debugProfile || externalDebugLog)
    {
        emulatorTraceSettings("loaded", settings);
    }
    cheatRuntimeSetEnabled(settings.cheatsEnabled || emulatorEnvEnabled("DINGOO_PIE_CHEATS"));
    emulatorApplySharedRuntimeSettings(settings);
    mixerSetValidationCaptureEnabled(
        platformAndroidConsumeAudioValidationAutomationEnabled());

    std::string selectedGamePath = platformAndroidConsumeGameAutomationPath();
    bool gameAutomation = !selectedGamePath.empty();
    bool cheatManagerAutomation = false;
    if (selectedGamePath.empty())
    {
        selectedGamePath = platformAndroidConsumeCheatManagerAutomationGamePath();
        cheatManagerAutomation = !selectedGamePath.empty();
    }
    if (selectedGamePath.empty())
    {
        printf("main: no startup game; frontend is waiting for game selection\n");
    }
    else
    {
        printf("main: %s automation startup game=%s\n",
            gameAutomation ? "game" : "cheat manager", selectedGamePath.c_str());
        platformChangeToGameDirectory(selectedGamePath);
    }

    printf("main: initializing frontend\n");
    if (!frontendInit(&settings, selectedGamePath.c_str()))
    {
        printf("main: frontend initialization failed\n");
        return -1;
    }
    printf("main: frontend initialized\n");
    if (cheatManagerAutomation)
    {
        if (!frontendRunCheatManagerFileSwitchAutomation())
        {
            shutdownApplication();
            return -2;
        }
    }
    std::string currentGamePath = selectedGamePath;
    while (!frontendQuitRequested())
    {
        EmulatorOptions options = loadEmulatorOptions();
        frontendSetCurrentGamePath(currentGamePath.c_str());
        if (!currentGamePath.empty())
        {
            frontendPrepareForGameLaunch();
            printf("main: starting selected game\n");
            uint32_t cheatRevisionBeforeStart = cheatRuntimeRevision();
            bool gameStarted = gameRuntimeStart(
                currentGamePath.c_str(),
                options,
                emulatorCheatFeatureKeysForGame(settings, currentGamePath));
            frontendSetGameRunning(gameStarted);
            if (gameStarted)
            {
                frontendNotifyGameStarted(currentGamePath.c_str());
                emulatorSaveSettings(settings);
                waitForInitialCheatLoad(cheatRevisionBeforeStart);
                if (cheatManagerAutomation)
                {
                    frontendRunCheatManagerAutomation();
                    cheatManagerAutomation = false;
                }
            }
        }

        frontendRunLoop(options);
        frontendSetGameRunning(false);
        if (!stopGameRuntimeForTransition())
        {
            exitAfterRuntimeStopTimeout();
        }

        std::string requestedGamePath;
        if (!frontendConsumeGameLaunchRequest(&requestedGamePath))
        {
            break;
        }

        currentGamePath = requestedGamePath;
        if (!currentGamePath.empty())
        {
            platformChangeToGameDirectory(currentGamePath);
        }
    }
    shutdownApplication();

    return 0;
}
