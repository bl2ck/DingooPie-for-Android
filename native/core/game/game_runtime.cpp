#include "game/game_runtime.h"

#include "app/app_runtime.h"
#include "cc/cc_arm_runtime.h"
#include "game/game_history.h"
#include "game/game_paths.h"
#include "frontend/sdl_audio.h"
#include "frontend/sdl_frontend.h"

#include <atomic>
#include <pthread.h>
#include <stdio.h>
#include <string>
#include <vector>

static pthread_mutex_t g_gameRuntimeMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_ccRuntimeThread;
static bool g_ccRuntimeThreadStarted = false;
static GameFormat g_activeGameFormat = GAME_FORMAT_UNKNOWN;
static std::string g_ccGamePath;
static std::vector<std::string> g_ccCheatFeatureKeys;
static bool g_clearRecentOnCcStartupFailure = false;
static std::atomic<bool> g_ccRunExitedNormally(false);

static void* runCcGame(void*)
{
    CcArmRuntimeStats stats = {};
    bool ok = ccArmRuntimeRunFile(g_ccGamePath.c_str(), g_ccCheatFeatureKeys, &stats);
    const bool guestCompleted = ok && stats.guestCompleted;
    g_ccRunExitedNormally.store(guestCompleted, std::memory_order_release);
    if (!ok)
    {
        gameHistoryClearRecentIfCurrent(
            g_ccGamePath, g_clearRecentOnCcStartupFailure, "CC startup failure");
        printf("cc-runtime: execution failed: %s\n", stats.error[0] ? stats.error : "unknown error");
        frontendRequestQuit();
    }
    else if (guestCompleted)
    {
        printf("cc-runtime: guest completed; returning to library\n");
        frontendRequestGameExit();
    }
    else
    {
        printf("cc-runtime: stopped by frontend request\n");
    }
    printf("cc-runtime: thread exited ok=%u\n", ok ? 1u : 0u);
    return NULL;
}

bool gameRuntimeStart(
    const char* gamePath,
    const EmulatorOptions& options,
    bool clearRecentOnStartupFailure,
    const std::vector<std::string>& enabledCheatFeatureKeys)
{
    const std::string normalizedPath = gamePathNormalize(gamePath);
    const GameFormat format = gameFormatFromPath(normalizedPath);

    if (format == GAME_FORMAT_APP)
    {
        MixerSetRuntimeAudioProfile(MIXER_RUNTIME_AUDIO_NATIVE_GUEST);
        if (!appRuntimeStart(normalizedPath.c_str(), options,
                clearRecentOnStartupFailure, enabledCheatFeatureKeys))
        {
            return false;
        }
        pthread_mutex_lock(&g_gameRuntimeMutex);
        g_activeGameFormat = GAME_FORMAT_APP;
        pthread_mutex_unlock(&g_gameRuntimeMutex);
        return true;
    }

    if (format == GAME_FORMAT_CC)
    {
        MixerSetRuntimeAudioProfile(MIXER_RUNTIME_AUDIO_CC_STABLE_HOST);
        g_ccGamePath = normalizedPath;
        g_ccCheatFeatureKeys = enabledCheatFeatureKeys;
        g_clearRecentOnCcStartupFailure = clearRecentOnStartupFailure;
        g_ccRunExitedNormally.store(false, std::memory_order_release);
        ccArmRuntimePrepareRun();

        printf("game-runtime: starting CC game: %s\n", g_ccGamePath.c_str());
        pthread_t thread;
        if (pthread_create(&thread, NULL, runCcGame, NULL) != 0)
        {
            printf("game-runtime: failed to create CC runtime thread\n");
            return false;
        }

        pthread_mutex_lock(&g_gameRuntimeMutex);
        g_ccRuntimeThread = thread;
        g_ccRuntimeThreadStarted = true;
        g_activeGameFormat = GAME_FORMAT_CC;
        pthread_mutex_unlock(&g_gameRuntimeMutex);
        return true;
    }

    printf("game-runtime: unsupported game path: %s\n",
        normalizedPath.empty() ? "(empty)" : normalizedPath.c_str());
    return false;
}

void gameRuntimeNotifyPauseRequested(void)
{
    pthread_mutex_lock(&g_gameRuntimeMutex);
    const GameFormat format = g_activeGameFormat;
    pthread_mutex_unlock(&g_gameRuntimeMutex);
    if (format == GAME_FORMAT_APP)
    {
        appRuntimeNotifyPauseRequested();
    }
}

void gameRuntimeApplySettings(void)
{
    pthread_mutex_lock(&g_gameRuntimeMutex);
    const GameFormat format = g_activeGameFormat;
    pthread_mutex_unlock(&g_gameRuntimeMutex);

    if (format == GAME_FORMAT_APP)
    {
        appRuntimeApplySettings();
    }
    else if (format == GAME_FORMAT_CC)
    {
        ccArmRuntimeApplySettings();
    }
}

void gameRuntimeStop(void)
{
    pthread_t ccThread = {};
    bool joinCcThread = false;

    pthread_mutex_lock(&g_gameRuntimeMutex);
    const GameFormat format = g_activeGameFormat;
    g_activeGameFormat = GAME_FORMAT_UNKNOWN;
    if (format == GAME_FORMAT_CC && g_ccRuntimeThreadStarted)
    {
        ccThread = g_ccRuntimeThread;
        joinCcThread = true;
        g_ccRuntimeThreadStarted = false;
    }
    pthread_mutex_unlock(&g_gameRuntimeMutex);

    if (format == GAME_FORMAT_APP)
    {
        appRuntimeStop();
        return;
    }

    if (!joinCcThread)
    {
        return;
    }

    printf("game-runtime: stop requested for CC runtime\n");
    ccArmRuntimeRequestStop();
    pthread_join(ccThread, NULL);
    MixerResetAfterRuntimeStop();
    printf("game-runtime: CC runtime thread joined\n");

    if (g_ccRunExitedNormally.load(std::memory_order_acquire))
    {
        gameHistorySaveRecent(g_ccGamePath, "normal CC runtime exit");
    }
    else
    {
        printf("game-runtime: recent game not saved because CC runtime did not exit normally\n");
    }
}
