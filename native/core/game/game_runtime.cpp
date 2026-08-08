#include "game/game_runtime.h"

#include "app/app_mips_runtime.h"
#include "cc/cc_arm_runtime.h"
#include "game/game_paths.h"
#include "frontend/sdl_audio.h"
#include "frontend/sdl_frontend.h"
#include "runtime/thread_join.h"

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
static RuntimeThreadCompletion g_ccRuntimeThreadCompletion =
    RUNTIME_THREAD_COMPLETION_INITIALIZER;

static const uint32_t kRuntimeStopTimeoutMs = 5000;

static void* runCcGame(void*)
{
    struct RuntimeThreadCompletionGuard
    {
        ~RuntimeThreadCompletionGuard()
        {
            runtimeThreadCompletionSignal(&g_ccRuntimeThreadCompletion);
        }
    } completionGuard;
    CcArmRuntimeStats stats = {};
    bool ok = ccArmRuntimeRunFile(g_ccGamePath.c_str(), g_ccCheatFeatureKeys, &stats);
    const bool guestCompleted = ok && stats.guestCompleted;
    if (!ok)
    {
        printf("cc-runtime: execution failed: %s\n", stats.error[0] ? stats.error : "unknown error");
        frontendRequestQuit();
    }
    else if (guestCompleted)
    {
        printf("cc-runtime: guest completed; returning to library\n");
        frontendRequestGameExit();
    }
    return NULL;
}

bool gameRuntimeStart(
    const char* gamePath,
    const EmulatorOptions& options,
    const std::vector<std::string>& enabledCheatFeatureKeys)
{
    const std::string normalizedPath = gamePathNormalize(gamePath);
    const GameFormat format = gameFormatFromPath(normalizedPath);

    if (format == GAME_FORMAT_APP)
    {
        if (!appRuntimeStart(normalizedPath.c_str(), options, enabledCheatFeatureKeys))
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
        g_ccGamePath = normalizedPath;
        g_ccCheatFeatureKeys = enabledCheatFeatureKeys;
        runtimeThreadCompletionReset(&g_ccRuntimeThreadCompletion);
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

bool gameRuntimeStop(void)
{
    pthread_t ccThread = {};
    bool joinCcThread = false;

    pthread_mutex_lock(&g_gameRuntimeMutex);
    const GameFormat format = g_activeGameFormat;
    if (format == GAME_FORMAT_CC && g_ccRuntimeThreadStarted)
    {
        ccThread = g_ccRuntimeThread;
        joinCcThread = true;
    }
    pthread_mutex_unlock(&g_gameRuntimeMutex);

    if (format == GAME_FORMAT_APP)
    {
        const bool stopped = appRuntimeStop();
        if (stopped)
        {
            pthread_mutex_lock(&g_gameRuntimeMutex);
            g_activeGameFormat = GAME_FORMAT_UNKNOWN;
            pthread_mutex_unlock(&g_gameRuntimeMutex);
        }
        return stopped;
    }

    if (!joinCcThread)
    {
        return true;
    }

    ccArmRuntimeRequestStop();
    int joinError = 0;
    const RuntimeThreadJoinResult joinResult = runtimeThreadJoinWithTimeout(
        ccThread, &g_ccRuntimeThreadCompletion, kRuntimeStopTimeoutMs, &joinError);
    if (joinResult != RUNTIME_THREAD_JOINED)
    {
        if (joinResult == RUNTIME_THREAD_JOIN_TIMEOUT)
        {
            printf("game-runtime: CC runtime thread did not stop within %u ms\n",
                kRuntimeStopTimeoutMs);
        }
        else
        {
            printf("game-runtime: CC runtime thread join failed: %d\n", joinError);
        }
        return false;
    }

    pthread_mutex_lock(&g_gameRuntimeMutex);
    g_ccRuntimeThreadStarted = false;
    g_activeGameFormat = GAME_FORMAT_UNKNOWN;
    pthread_mutex_unlock(&g_gameRuntimeMutex);
    mixerResetAfterRuntimeStop();

    return true;
}
