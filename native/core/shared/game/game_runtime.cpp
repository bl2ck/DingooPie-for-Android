#include "shared/game/game_runtime.h"

#include "app/runtime/app_runtime.h"
#include "app/hle/app_hle.h"
#include "app/save/app_save_state.h"
#include "cc/save/cc_save_state.h"
#include "cc/runtime/cc_runtime.h"
#include "shared/game/game_paths.h"
#include "frontend/audio/sdl_audio.h"
#include "frontend/shell/frontend_shell.h"
#include "shared/execution/thread_join.h"

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
    CcRuntimeStats stats = {};
    bool ok = ccRuntimeRunFile(g_ccGamePath.c_str(), g_ccCheatFeatureKeys, &stats);
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
        ccRuntimePrepareRun();

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

GameFormat gameRuntimeActiveFormat(void)
{
    pthread_mutex_lock(&g_gameRuntimeMutex);
    const GameFormat format = g_activeGameFormat;
    pthread_mutex_unlock(&g_gameRuntimeMutex);
    return format;
}

uint32_t gameRuntimeActiveUnitCount(void)
{
    const GameFormat format = gameRuntimeActiveFormat();
    if (format == GAME_FORMAT_APP)
    {
        return appRuntimeActiveThreadCount();
    }
    if (format == GAME_FORMAT_CC)
    {
        return ccRuntimeActiveTaskCount();
    }
    return 0;
}

void gameRuntimeCopyDiagnostics(char* identity, size_t identitySize,
    char* lastTask, size_t lastTaskSize, char* lastHle, size_t lastHleSize)
{
    if (identity && identitySize)
    {
        snprintf(identity, identitySize, "%s",
            gameRuntimeActiveFormat() == GAME_FORMAT_APP ? bridge_get_game_identity() : "");
    }
    if (gameRuntimeActiveFormat() == GAME_FORMAT_APP)
    {
        bridge_copy_last_task_stop_summary(lastTask, lastTaskSize);
        bridge_copy_last_hle_summary(lastHle, lastHleSize);
        return;
    }
    if (lastTask && lastTaskSize) lastTask[0] = '\0';
    if (lastHle && lastHleSize) lastHle[0] = '\0';
}

bool gameRuntimeWriteState(const std::string& gamePath, int slot,
    std::string* error, SaveStateProgressCallback progressCallback,
    void* progressUserData)
{
    const SaveStateGameFormat format = saveStateFormatForPath(gamePath);
    if (format == SAVE_STATE_FORMAT_CC)
    {
        CcRuntimeState state;
        return ccRuntimeCaptureState(&state, error) &&
            saveStateWriteCcSlot(gamePath, slot, state, error,
                progressCallback, progressUserData);
    }
    AppRuntimeState state;
    return appRuntimeCaptureState(&state, error) &&
        saveStateWriteSlot(gamePath, format, slot, state, error,
            progressCallback, progressUserData);
}

bool gameRuntimeReadState(const std::string& gamePath, int slot,
    std::string* error, SaveStateProgressCallback progressCallback,
    void* progressUserData)
{
    const SaveStateGameFormat format = saveStateFormatForPath(gamePath);
    if (format == SAVE_STATE_FORMAT_CC)
    {
        CcRuntimeState state;
        return saveStateReadCcSlot(gamePath, slot, &state, error,
                progressCallback, progressUserData) &&
            ccRuntimeRestoreState(state, error);
    }
    AppRuntimeState state;
    return saveStateReadSlot(gamePath, format, slot, &state, error,
            progressCallback, progressUserData) &&
        appRuntimeRestoreState(state, error);
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
        ccRuntimeApplySettings();
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

    ccRuntimeRequestStop();
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
    audioOutputResetAfterRuntimeStop();

    return true;
}
