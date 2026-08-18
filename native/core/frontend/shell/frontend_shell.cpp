#include "frontend/shell/frontend_shell.h"

#include "shared/config/runtime_constants.h"
#include "shared/game/game_paths.h"
#include "config/cheats/cheat_runtime.h"
#include "shared/game/game_runtime.h"
#include "frontend/input/input_controls.h"
#include "frontend/video/frame_processor.h"
#include "frontend/video/framebuffer.h"
#include "shared/execution/pause_gate.h"
#include "shared/platform/storage_services.h"
#include "shared/platform/external_launch_services.h"
#include "frontend/audio/sdl_audio.h"
#include "frontend/menu/menu_model.h"
#include "frontend/menu/menu_overlay.h"
#include "frontend/menu/menu_strings.h"
#include "shared/diagnostics/runtime_log.h"
#include "jni_local_ref.h"

#include <SDL2/SDL.h>
#include <ctype.h>
#include <algorithm>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <time.h>
#include <vector>
#include <jni.h>

static SDL_Window* g_window = NULL;
SDL_Renderer* g_renderer = NULL;
static SDL_Texture* g_frameTexture = NULL;
static SDL_Texture* g_blurredBackdropTexture = NULL;
static SDL_Texture* g_fpsOverlayTexture = NULL;
static SDL_Texture* g_idleTitleTexture = NULL;
static SDL_Texture* g_idleSymbolTextures[4] = {};
static int g_fpsOverlayValue = -1;
static int g_fpsOverlayWidth = 0;
static int g_fpsOverlayHeight = 0;
static int g_fpsOverlayScale = 0;
static int g_idleTitleTextureWidth = 0;
static int g_idleTitleTextureHeight = 0;
static SDL_GameController* g_gameController = NULL;
static uint32_t g_gameControllerButtonControls = 0;
static uint32_t g_gameControllerAxisControls = 0;
static uint32_t g_gameControllerMenuButtons = 0;
static bool g_gameControllerMenuActionActive = false;
static Sint16 g_gameControllerAxes[SDL_CONTROLLER_AXIS_MAX];
static uint32_t g_gameControllerButtonMap[SDL_CONTROLLER_BUTTON_MAX];
static uint32_t g_gameControllerAxisMap[SDL_CONTROLLER_AXIS_MAX][2];
bool g_controllerMappingPending = false;
uint32_t g_controllerMappingTarget = 0;
static bool g_controllerMappingInitialized = false;
static std::string g_appliedControllerMapping;
struct ControllerAxisCalibration
{
    int center;
    int minimum;
    int maximum;
    int deadZone;
};
enum ControllerCalibrationStage
{
    CONTROLLER_CALIBRATION_IDLE = 0,
    CONTROLLER_CALIBRATION_CENTER,
    CONTROLLER_CALIBRATION_RANGE
};
static ControllerAxisCalibration g_controllerAxisCalibration[SDL_CONTROLLER_AXIS_MAX];
static bool g_controllerCalibrationInitialized = false;
static std::string g_appliedControllerCalibration;
static ControllerCalibrationStage g_controllerCalibrationStage = CONTROLLER_CALIBRATION_IDLE;
static uint64_t g_controllerCalibrationStageStartTicks = 0;
static int64_t g_controllerCalibrationCenterSums[SDL_CONTROLLER_AXIS_MAX];
static int g_controllerCalibrationCenterSamples = 0;
static Sint16 g_controllerCalibrationMinimums[SDL_CONTROLLER_AXIS_MAX];
static Sint16 g_controllerCalibrationMaximums[SDL_CONTROLLER_AXIS_MAX];
static SDL_atomic_t g_quitRequested;
static SDL_atomic_t g_gamePaused;
static SDL_atomic_t g_frontendTransitionRequested;
static SDL_atomic_t g_frontendGameLaunchPending;
static SDL_atomic_t g_frontendLoopExitRequested;
static bool g_gameRunning = false;
static bool g_userPauseRequested = false;
static bool g_minimizedPauseActive = false;
static bool g_androidBackgroundActive = false;
static SDL_atomic_t g_androidBackgroundRequested;
static bool g_androidRendererRestorePending = false;
static uint32_t g_androidForegroundStablePumps = 0;
EmulatorSettings* g_frontendSettings = NULL;
std::string g_frontendCurrentGamePath;
std::string g_androidCheatManagerGamePath;
static std::string g_frontendPendingGamePath;
int g_androidSaveStateSelectedSlot = 1;
bool g_androidSaveStateBusy = false;
SaveStateProgress g_androidSaveStateProgress = { SAVE_STATE_PROGRESS_COMPRESS, 0 };
std::string g_androidSaveStateStatus;
SDL_Texture* g_androidSaveStateThumbnail = NULL;
bool g_androidSaveStateSlotExists[kSaveStateSlotCount] = {};
uint64_t g_androidSaveStateSlotModifiedTime[kSaveStateSlotCount] = {};
std::string g_androidSaveStateSlotCacheGamePath;

static const uint64_t kMinimizedThrottlePresentIntervalMs = 250;
static const uint32_t kMinimizedThrottleLoopDelayMs = 50;
static const uint64_t kIdlePresentIntervalUs = 16667;
static const uint64_t kIdleWakeMarginUs = 2000;
static const uint32_t kIdleMaxWaitMs = 4;
static const int kBlurredBackdropWidth = SCREEN_WIDTH / 4;
static const int kBlurredBackdropHeight = SCREEN_HEIGHT / 4;
static const double kPi = 3.14159265358979323846;
static uint32_t g_blurredBackdropUpdateCounter = 0;

static bool inputTraceEnabled(void);
static void openFirstGameController(void);
static void cancelControllerMapping(void);
void resetControllerMapping(void);
void beginControllerCalibration(void);
void resetControllerCalibration(void);
static void cancelControllerCalibration(void);
static void updateControllerCalibration(void);
std::string controllerCalibrationStatusText(void);
static std::string trimString(const std::string& text);
static void releaseVirtualPointerControls(void);
static void updateVirtualPointerControls(uint32_t newMask);
static bool drawIdleScreen(uint64_t animationTimeMs);
static bool createGameFrameTexture(void);
static bool createBlurredBackdropTexture(void);
static bool textureLinearSamplingEnabled(const EmulatorSettings& settings);
static void releaseGameVideoResources(void);

static const char* sdlLogCategoryName(int category)
{
    switch (category)
    {
    case SDL_LOG_CATEGORY_APPLICATION:
        return "application";
    case SDL_LOG_CATEGORY_ERROR:
        return "error";
    case SDL_LOG_CATEGORY_ASSERT:
        return "assert";
    case SDL_LOG_CATEGORY_SYSTEM:
        return "system";
    case SDL_LOG_CATEGORY_AUDIO:
        return "audio";
    case SDL_LOG_CATEGORY_VIDEO:
        return "video";
    case SDL_LOG_CATEGORY_RENDER:
        return "render";
    case SDL_LOG_CATEGORY_INPUT:
        return "input";
    case SDL_LOG_CATEGORY_TEST:
        return "test";
    default:
        return "custom";
    }
}

static const char* sdlLogPriorityName(SDL_LogPriority priority)
{
    switch (priority)
    {
    case SDL_LOG_PRIORITY_VERBOSE:
        return "verbose";
    case SDL_LOG_PRIORITY_DEBUG:
        return "debug";
    case SDL_LOG_PRIORITY_INFO:
        return "info";
    case SDL_LOG_PRIORITY_WARN:
        return "warn";
    case SDL_LOG_PRIORITY_ERROR:
        return "error";
    case SDL_LOG_PRIORITY_CRITICAL:
        return "critical";
    default:
        return "unknown";
    }
}

static void SDLCALL frontendSdlLogOutput(void* userdata, int category,
    SDL_LogPriority priority, const char* message)
{
    (void)userdata;
    printf("sdl-log: %s %s: %s\n",
        sdlLogPriorityName(priority),
        sdlLogCategoryName(category),
        message ? message : "");
}

uint32_t controlMask(uint32_t controlBit)
{
    return 1u << controlBit;
}

static const uint32_t kControllerMenuActionMask = 1u << kControllerMenuActionBit;

static bool confirmExitRequested(void)
{
    inputClearSyntheticControls();
    inputClearControls();
    return true;
}

static const uint8_t kDigitFont[10][7] =
{
    { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e },
    { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },
    { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f },
    { 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e },
    { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },
    { 0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e },
    { 0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e },
    { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
    { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e },
    { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e },
};

static const uint8_t kLetterA[7] = { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 };
static const uint8_t kLetterB[7] = { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e };
static const uint8_t kLetterC[7] = { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e };
static const uint8_t kLetterD[7] = { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e };
static const uint8_t kLetterE[7] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f };
static const uint8_t kLetterF[7] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 };
static const uint8_t kLetterG[7] = { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f };
static const uint8_t kLetterH[7] = { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 };
static const uint8_t kLetterI[7] = { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e };
static const uint8_t kLetterJ[7] = { 0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0e };
static const uint8_t kLetterK[7] = { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
static const uint8_t kLetterL[7] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f };
static const uint8_t kLetterM[7] = { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 };
static const uint8_t kLetterN[7] = { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
static const uint8_t kLetterO[7] = { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e };
static const uint8_t kLetterP[7] = { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 };
static const uint8_t kLetterQ[7] = { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d };
static const uint8_t kLetterR[7] = { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 };
static const uint8_t kLetterS[7] = { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e };
static const uint8_t kLetterT[7] = { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
static const uint8_t kLetterU[7] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e };
static const uint8_t kLetterV[7] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 };
static const uint8_t kLetterW[7] = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11 };
static const uint8_t kLetterX[7] = { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 };
static const uint8_t kLetterY[7] = { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 };
static const uint8_t kLetterZ[7] = { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f };
static const uint8_t kColon[7]   = { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 };
static const uint8_t kLowerE[7]  = { 0x00, 0x00, 0x0e, 0x11, 0x1f, 0x10, 0x0e };
static const uint8_t kLowerG[7]  = { 0x00, 0x00, 0x0f, 0x11, 0x0f, 0x01, 0x0e };
static const uint8_t kLowerI[7]  = { 0x04, 0x00, 0x0c, 0x04, 0x04, 0x04, 0x0e };
static const uint8_t kLowerN[7]  = { 0x00, 0x00, 0x1e, 0x11, 0x11, 0x11, 0x11 };
static const uint8_t kLowerO[7]  = { 0x00, 0x00, 0x0e, 0x11, 0x11, 0x11, 0x0e };

static const uint8_t* glyphForChar(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return kDigitFont[ch - '0'];
    }
    switch (ch)
    {
    case 'F': return kLetterF;
    case 'A': return kLetterA;
    case 'B': return kLetterB;
    case 'C': return kLetterC;
    case 'D': return kLetterD;
    case 'E': return kLetterE;
    case 'G': return kLetterG;
    case 'H': return kLetterH;
    case 'I': return kLetterI;
    case 'J': return kLetterJ;
    case 'K': return kLetterK;
    case 'L': return kLetterL;
    case 'M': return kLetterM;
    case 'N': return kLetterN;
    case 'O': return kLetterO;
    case 'P': return kLetterP;
    case 'Q': return kLetterQ;
    case 'R': return kLetterR;
    case 'S': return kLetterS;
    case 'T': return kLetterT;
    case 'U': return kLetterU;
    case 'V': return kLetterV;
    case 'W': return kLetterW;
    case 'X': return kLetterX;
    case 'Y': return kLetterY;
    case 'Z': return kLetterZ;
    case ':': return kColon;
    case 'e': return kLowerE;
    case 'g': return kLowerG;
    case 'i': return kLowerI;
    case 'n': return kLowerN;
    case 'o': return kLowerO;
    default: return NULL;
    }
}

struct VirtualControlButton
{
    const char* label;
    uint32_t controlMask;
    SDL_Rect rect;
    int dpadDx;
    int dpadDy;
    bool drawFrame;
};
static const int kVirtualControlButtonCapacity = 21;

static uint32_t g_virtualPointerControls = 0;
static uint32_t g_virtualMouseControlMask = 0;
static bool g_virtualMousePointerHeld = false;
static int g_virtualDpadOffsetX = 0;
static int g_virtualDpadOffsetY = 0;
static double g_virtualDpadVisualOffsetX = 0.0;
static double g_virtualDpadVisualOffsetY = 0.0;
static uint64_t g_virtualDpadVisualUpdateTicks = 0;
struct AndroidVirtualTouchContact
{
    SDL_FingerID fingerId;
    uint32_t controlMask;
    bool controlsDpad;
    uint64_t pressedAtTicks;
};

static std::vector<AndroidVirtualTouchContact> g_androidVirtualTouchContacts;
static uint32_t g_androidReleasedTouchControls = 0;
static uint64_t g_androidReleasedTouchControlUntilTicks[32] = {};
static bool g_virtualMouseControlsDpad = false;
static uint64_t g_virtualMouseReleaseAtTicks = 0;
static const uint64_t kVirtualPointerClickHoldMs = 180;
static const uint64_t kVirtualGameplayButtonMinimumPressMs = 48;
static uint64_t g_postRestoreInputBlockUntilTicks = 0;

// Virtual controls and gamepads both feed synthetic Dingoo controls; merge the
// sources before updating input state so releasing one source does not cancel another.
static uint32_t frontendSyntheticControlMask(void)
{
    return g_virtualPointerControls | g_gameControllerButtonControls | g_gameControllerAxisControls;
}

static void applyFrontendSyntheticControlMask(uint32_t oldMask, uint32_t newMask)
{
    uint32_t changed = oldMask ^ newMask;
    uint32_t pressed = changed & newMask;
    if (pressed)
    {
        audioOutputRecordInput(pressed);
    }
    for (uint32_t bit = 0; bit < 32; ++bit)
    {
        uint32_t mask = 1u << bit;
        if (changed & mask)
        {
            inputSetSyntheticControl(bit, (newMask & mask) != 0);
        }
    }
}

static bool frontendPostRestoreInputBlocked(void)
{
    uint64_t until = g_postRestoreInputBlockUntilTicks;
    if (!until)
    {
        return false;
    }

    uint64_t now = SDL_GetTicks64();
    if (now < until)
    {
        return true;
    }
    g_postRestoreInputBlockUntilTicks = 0;
    return false;
}

bool virtualControlsVisible(void)
{
    return g_frontendSettings && g_frontendSettings->showVirtualControls;
}

static int virtualControlScalePercent(void)
{
    if (!g_frontendSettings)
    {
        return 100;
    }
    for (size_t index = 0;
        index < sizeof(EMULATOR_VIRTUAL_CONTROL_SCALE_VALUES) /
            sizeof(EMULATOR_VIRTUAL_CONTROL_SCALE_VALUES[0]);
        ++index)
    {
        if (g_frontendSettings->virtualControlScalePercent ==
            EMULATOR_VIRTUAL_CONTROL_SCALE_VALUES[index])
        {
            return g_frontendSettings->virtualControlScalePercent;
        }
    }
    return 100;
}

static VirtualDpadType virtualDpadType(void)
{
    if (!g_frontendSettings ||
        g_frontendSettings->virtualDpadType < VIRTUAL_DPAD_JOYSTICK ||
        g_frontendSettings->virtualDpadType >= VIRTUAL_DPAD_TYPE_COUNT)
    {
        return VIRTUAL_DPAD_JOYSTICK;
    }
    return g_frontendSettings->virtualDpadType;
}

bool portraitModeEnabled(void)
{
    return g_frontendSettings && g_frontendSettings->portraitMode;
}

static int displayWidthForSettings(const EmulatorSettings* settings)
{
    return (settings && settings->portraitMode) ? SCREEN_HEIGHT : SCREEN_WIDTH;
}

static int displayHeightForSettings(const EmulatorSettings* settings)
{
    return (settings && settings->portraitMode) ? SCREEN_WIDTH : SCREEN_HEIGHT;
}

static bool pixelGridEffectEnabled(void)
{
    return g_frontendSettings && g_frontendSettings->colorEffect == COLOR_EFFECT_PIXEL_GRID;
}

static bool presentBlackTransitionFrame(void);
static bool getLandscapeGameDestination(SDL_Rect* outRect);
static bool getPortraitGameDestination(SDL_Rect* outRect);

static bool pointInRect(int x, int y, const SDL_Rect& rect)
{
    return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}

static bool getVirtualControlCoordinateSize(int* outWidth, int* outHeight)
{
    if (!outWidth || !outHeight || !g_renderer)
    {
        return false;
    }

    int rendererWidth = 0;
    int rendererHeight = 0;
    SDL_GetRendererOutputSize(g_renderer, &rendererWidth, &rendererHeight);
    if (rendererWidth <= 0 || rendererHeight <= 0)
    {
        return false;
    }

    if (portraitModeEnabled())
    {
        *outWidth = rendererHeight;
        *outHeight = rendererWidth;
    }
    else
    {
        *outWidth = rendererWidth;
        *outHeight = rendererHeight;
    }
    return true;
}

static void mapRendererPointToVirtualControls(int* x, int* y)
{
    if (!x || !y || !portraitModeEnabled() || !g_renderer)
    {
        return;
    }

    int rendererWidth = 0;
    int rendererHeight = 0;
    SDL_GetRendererOutputSize(g_renderer, &rendererWidth, &rendererHeight);
    if (rendererWidth <= 0 || rendererHeight <= 0)
    {
        return;
    }

    int mappedX = rendererHeight - 1 - *y;
    int mappedY = *x;
    *x = mappedX;
    *y = mappedY;
}

static SDL_Rect rotateVirtualRectCcw(const SDL_Rect& rect)
{
    int width = 0;
    int height = 0;
    if (!getVirtualControlCoordinateSize(&width, &height))
    {
        return rect;
    }
    (void)height;

    SDL_Rect rotated = { rect.y, width - rect.x - rect.w, rect.h, rect.w };
    return rotated;
}

static void rotateVirtualPointCcw(int x, int y, int* outX, int* outY)
{
    int width = 0;
    int height = 0;
    if (!getVirtualControlCoordinateSize(&width, &height))
    {
        if (outX)
        {
            *outX = x;
        }
        if (outY)
        {
            *outY = y;
        }
        return;
    }
    (void)height;

    if (outX)
    {
        *outX = y;
    }
    if (outY)
    {
        *outY = width - 1 - x;
    }
}

static void renderVirtualFillRect(const SDL_Rect& rect)
{
    if (portraitModeEnabled())
    {
        SDL_Rect rotated = rotateVirtualRectCcw(rect);
        SDL_RenderFillRect(g_renderer, &rotated);
    }
    else
    {
        SDL_RenderFillRect(g_renderer, &rect);
    }
}

static void renderVirtualDrawRect(const SDL_Rect& rect)
{
    if (portraitModeEnabled())
    {
        SDL_Rect rotated = rotateVirtualRectCcw(rect);
        SDL_RenderDrawRect(g_renderer, &rotated);
    }
    else
    {
        SDL_RenderDrawRect(g_renderer, &rect);
    }
}

static void renderVirtualDrawLine(int x1, int y1, int x2, int y2)
{
    if (portraitModeEnabled())
    {
        int rx1 = 0;
        int ry1 = 0;
        int rx2 = 0;
        int ry2 = 0;
        rotateVirtualPointCcw(x1, y1, &rx1, &ry1);
        rotateVirtualPointCcw(x2, y2, &rx2, &ry2);
        SDL_RenderDrawLine(g_renderer, rx1, ry1, rx2, ry2);
    }
    else
    {
        SDL_RenderDrawLine(g_renderer, x1, y1, x2, y2);
    }
}

static void renderVirtualFillCircle(int centerX, int centerY, int radius)
{
    for (int y = -radius; y <= radius; ++y)
    {
        int halfWidth = (int)sqrt((double)(radius * radius - y * y));
        renderVirtualDrawLine(centerX - halfWidth, centerY + y, centerX + halfWidth, centerY + y);
    }
}

static void renderVirtualDrawCircle(int centerX, int centerY, int radius)
{
    const int segments = std::max(64, std::min(256, radius * 4));
    int previousX = centerX + radius;
    int previousY = centerY;
    for (int i = 1; i <= segments; ++i)
    {
        double angle = (double)i * kPi * 2.0 / (double)segments;
        int x = centerX + (int)(cos(angle) * radius);
        int y = centerY + (int)(sin(angle) * radius);
        renderVirtualDrawLine(previousX, previousY, x, y);
        previousX = x;
        previousY = y;
    }
}

static void renderVirtualFillArcBand(int centerX, int centerY,
    int innerRadius, int outerRadius, int directionX, int directionY)
{
    int64_t innerRadiusSquared = (int64_t)innerRadius * innerRadius;
    int64_t outerRadiusSquared = (int64_t)outerRadius * outerRadius;
    int tangentX = -directionY;
    int tangentY = directionX;
    for (int localY = -outerRadius; localY <= outerRadius; ++localY)
    {
        int runStart = 0;
        bool runActive = false;
        for (int localX = -outerRadius; localX <= outerRadius; ++localX)
        {
            int64_t distanceSquared =
                (int64_t)localX * localX + (int64_t)localY * localY;
            int forward = localX * directionX + localY * directionY;
            int lateral = abs(localX * tangentX + localY * tangentY);
            bool inside = distanceSquared >= innerRadiusSquared &&
                distanceSquared <= outerRadiusSquared && forward > 0 &&
                (int64_t)lateral * 1000 <= (int64_t)forward * 424;
            if (inside && !runActive)
            {
                runStart = localX;
                runActive = true;
            }
            if ((!inside || localX == outerRadius) && runActive)
            {
                int runEnd = inside && localX == outerRadius ? localX : localX - 1;
                renderVirtualDrawLine(centerX + runStart, centerY + localY,
                    centerX + runEnd, centerY + localY);
                runActive = false;
            }
        }
    }
}

static void drawPixelGridOverlay(void)
{
    int width = 0;
    int height = 0;
    if (!getVirtualControlCoordinateSize(&width, &height) ||
        width <= SCREEN_WIDTH || height <= SCREEN_HEIGHT)
    {
        return;
    }

    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 42);
    for (int x = 1; x < SCREEN_WIDTH; ++x)
    {
        int lineX = (int)(((int64_t)x * width) / SCREEN_WIDTH);
        renderVirtualDrawLine(lineX, 0, lineX, height - 1);
    }
    for (int y = 1; y < SCREEN_HEIGHT; ++y)
    {
        int lineY = (int)(((int64_t)y * height) / SCREEN_HEIGHT);
        renderVirtualDrawLine(0, lineY, width - 1, lineY);
    }
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
}

static int rendererTextWidth(const char* text, int scale)
{
    return text && *text ? ((int)strlen(text) * 6 - 1) * scale : 0;
}

static void drawRendererGlyph(const uint8_t* glyph, int x, int y, int scale, SDL_Color color)
{
    if (!glyph)
    {
        return;
    }

    SDL_SetRenderDrawColor(g_renderer, color.r, color.g, color.b, color.a);
    for (int row = 0; row < 7; ++row)
    {
        for (int col = 0; col < 5; ++col)
        {
            if (glyph[row] & (1 << (4 - col)))
            {
                SDL_Rect rect = { x + col * scale, y + row * scale, scale, scale };
                SDL_RenderFillRect(g_renderer, &rect);
            }
        }
    }
}

static void drawRendererText(const char* text, int x, int y, int scale, SDL_Color color)
{
    if (!text)
    {
        return;
    }

    int cursor = x;
    for (const char* p = text; *p; ++p)
    {
        const uint8_t* glyph = glyphForChar(*p);
        drawRendererGlyph(glyph, cursor, y, scale, color);
        cursor += 6 * scale;
    }
}

extern const int kAndroidMenuRowTop = 66;
extern const int kAndroidMenuRowHeight = 46;
extern const int kAndroidMenuRowGap = 6;
static const int kAndroidMenuRowHorizontalInset = 24;
static const int kAndroidMenuListBottomInset = 16;
bool androidMenuScreenUsesOverlay(AndroidMenuScreen menuScreen)
{
    return menuScreen == ANDROID_MENU_MAIN || menuScreen == ANDROID_MENU_OPTIONS ||
        menuScreen == ANDROID_MENU_SAVE_STATE ||
        menuScreen == ANDROID_MENU_ABOUT || menuScreen == ANDROID_MENU_SETTINGS ||
        menuScreen == ANDROID_MENU_VIDEO ||
        menuScreen == ANDROID_MENU_AUDIO || menuScreen == ANDROID_MENU_INPUT ||
        menuScreen == ANDROID_MENU_CONTROLLER_MAPPING ||
        menuScreen == ANDROID_MENU_CONTROLLER_CALIBRATION ||
        menuScreen == ANDROID_MENU_CHEAT_MANAGER;
}

AndroidMenuScreen g_androidMenuScreen = ANDROID_MENU_LIBRARY;
static AndroidMenuScreen g_androidMenuScreenAfterGameRestart = ANDROID_MENU_NONE;
static bool g_androidMenuOpeningMouseReleasePending = false;
static bool g_androidMenuOpeningFingerReleasePending = false;
static SDL_FingerID g_androidMenuOpeningFingerId = 0;
static std::string g_androidMenuGameRestartPath;
std::vector<std::string> g_androidGamePaths;
static bool g_androidGameImportPending = false;
static bool g_androidGameLibraryScanWasActive = false;
static int g_androidLibraryScrollOffset = 0;
static bool g_androidLibraryScrollDragging = false;
static bool g_androidLibraryScrollMoved = false;
static int g_androidLibraryScrollStartY = 0;
static int g_androidLibraryScrollStartOffset = 0;
static float g_androidLibraryScrollVelocity = 0.0f;
static uint64_t g_androidLibraryScrollLastMotionTicks = 0;
int g_androidMenuScrollOffset = 0;
static bool g_androidMenuScrollDragging = false;
static bool g_androidMenuScrollMoved = false;
static int g_androidMenuScrollStartY = 0;
static int g_androidMenuScrollStartOffset = 0;
int g_androidMenuSelectedRow = 0;
bool g_androidMenuSelectionHighlightVisible = false;

struct AndroidSystemTextTexture
{
    std::string text;
    int pixelSize;
    uint32_t color;
    bool bold;
    SDL_Texture* texture;
    int width;
    int height;
};

static std::vector<AndroidSystemTextTexture> g_androidSystemTextTextures;

static uint32_t androidTextColor(SDL_Color color)
{
    return ((uint32_t)color.a << 24) | ((uint32_t)color.r << 16) |
        ((uint32_t)color.g << 8) | (uint32_t)color.b;
}

void clearAndroidSystemTextTextures(void)
{
    for (size_t i = 0; i < g_androidSystemTextTextures.size(); ++i)
    {
        if (g_androidSystemTextTextures[i].texture)
        {
            SDL_DestroyTexture(g_androidSystemTextTextures[i].texture);
        }
    }
    g_androidSystemTextTextures.clear();
}

static AndroidSystemTextTexture* androidSystemTextTextureStyled(
    const char* text, int pixelSize, SDL_Color color, bool bold)
{
    if (!text || !text[0] || !g_renderer)
    {
        return NULL;
    }
    if (pixelSize < 10) pixelSize = 10;
    uint32_t packedColor = androidTextColor(color);
    for (size_t i = 0; i < g_androidSystemTextTextures.size(); ++i)
    {
        AndroidSystemTextTexture& entry = g_androidSystemTextTextures[i];
        if (entry.pixelSize == pixelSize && entry.color == packedColor &&
            entry.bold == bold && entry.text == text)
        {
            return &entry;
        }
    }

    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        return NULL;
    }
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID renderMethod = activityClass ? env->GetMethodID(activityClass,
        bold ? "renderSystemTextBold" : "renderSystemText", "([BFI)[I") : NULL;
    jsize textLength = (jsize)strlen(text);
    jbyteArray javaText = renderMethod ? env->NewByteArray(textLength) : NULL;
    if (javaText && textLength > 0)
    {
        env->SetByteArrayRegion(javaText, 0, textLength, (const jbyte*)text);
    }
    jintArray result = javaText ? (jintArray)env->CallObjectMethod(activity, renderMethod,
        javaText, (jfloat)pixelSize, (jint)packedColor) : NULL;
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        result = NULL;
    }
    if (javaText) env->DeleteLocalRef(javaText);
    if (activityClass) env->DeleteLocalRef(activityClass);
    if (!result || env->GetArrayLength(result) < 3)
    {
        if (result) env->DeleteLocalRef(result);
        return NULL;
    }

    jint dimensions[2] = {};
    env->GetIntArrayRegion(result, 0, 2, dimensions);
    int width = dimensions[0];
    int height = dimensions[1];
    jsize expectedLength = 2 + width * height;
    if (width <= 0 || height <= 0 || env->GetArrayLength(result) < expectedLength)
    {
        env->DeleteLocalRef(result);
        return NULL;
    }
    std::vector<jint> pixels((size_t)width * (size_t)height);
    env->GetIntArrayRegion(result, 2, width * height, pixels.data());
    env->DeleteLocalRef(result);

    SDL_Texture* texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STATIC, width, height);
    if (!texture)
    {
        return NULL;
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    if (SDL_UpdateTexture(texture, NULL, pixels.data(), width * (int)sizeof(jint)) != 0)
    {
        SDL_DestroyTexture(texture);
        return NULL;
    }

    AndroidSystemTextTexture entry = {};
    entry.text = text;
    entry.pixelSize = pixelSize;
    entry.color = packedColor;
    entry.bold = bold;
    entry.texture = texture;
    entry.width = width;
    entry.height = height;
    g_androidSystemTextTextures.push_back(entry);
    return &g_androidSystemTextTextures.back();
}

static AndroidSystemTextTexture* androidSystemTextTexture(
    const char* text, int pixelSize, SDL_Color color)
{
    return androidSystemTextTextureStyled(text, pixelSize, color, false);
}

static void drawAndroidSystemTextBold(
    const char* text, int x, int y, int pixelSize, SDL_Color color)
{
    AndroidSystemTextTexture* entry = androidSystemTextTextureStyled(
        text, pixelSize, color, true);
    if (!entry)
    {
        return;
    }
    SDL_Rect destination = { x, y, entry->width, entry->height };
    SDL_RenderCopy(g_renderer, entry->texture, NULL, &destination);
}

void drawAndroidSystemTextCentered(
    const char* text, const SDL_Rect& rect, int pixelSize, SDL_Color color)
{
    AndroidSystemTextTexture* entry = androidSystemTextTexture(text, pixelSize, color);
    if (!entry)
    {
        return;
    }
    SDL_Rect destination =
    {
        rect.x + (rect.w - entry->width) / 2,
        rect.y + (rect.h - entry->height) / 2,
        entry->width,
        entry->height
    };
    SDL_RenderCopy(g_renderer, entry->texture, NULL, &destination);
}

void drawAndroidSystemTextLeftCentered(
    const char* text, const SDL_Rect& rect, int leftInset, int pixelSize, SDL_Color color)
{
    AndroidSystemTextTexture* entry = androidSystemTextTexture(text, pixelSize, color);
    if (!entry)
    {
        return;
    }
    SDL_Rect destination =
    {
        rect.x + leftInset,
        rect.y + (rect.h - entry->height) / 2,
        entry->width,
        entry->height
    };
    SDL_RenderCopy(g_renderer, entry->texture, NULL, &destination);
}

void drawAndroidSystemTextRightCentered(
    const char* text, const SDL_Rect& rect, int rightInset, int pixelSize, SDL_Color color)
{
    AndroidSystemTextTexture* entry = androidSystemTextTexture(text, pixelSize, color);
    if (!entry)
    {
        return;
    }
    SDL_Rect destination =
    {
        rect.x + rect.w - rightInset - entry->width,
        rect.y + (rect.h - entry->height) / 2,
        entry->width,
        entry->height
    };
    SDL_RenderCopy(g_renderer, entry->texture, NULL, &destination);
}

bool androidChineseUi(void)
{
    return !g_frontendSettings || g_frontendSettings->uiLanguage != UI_LANGUAGE_ENGLISH;
}

const char* kZhBack = u8"\u8fd4\u56de";
static const char* kZhAddGame = u8"\u6dfb\u52a0\u6e38\u620f";
static const char* kZhNoGames = u8"\u6682\u65e0\u6e38\u620f";
static const char* kZhRemove = u8"\u79fb\u9664";
static const char* kZhRemoveGame = u8"\u79fb\u9664\u6e38\u620f";
static const char* kZhCancel = u8"\u53d6\u6d88";
const char* kZhMenu = u8"\u83dc\u5355";
const char* kZhGameMenu = u8"\u6e38\u620f\u83dc\u5355";
const char* kZhSwitchGame = u8"\u5207\u6362\u6e38\u620f";
const char* kZhExitApp = u8"\u9000\u51fa\u5e94\u7528";

static ScreenOrientationMode normalizeScreenOrientationMode(int mode)
{
    if (mode < SCREEN_ORIENTATION_AUTO || mode >= SCREEN_ORIENTATION_MODE_COUNT)
    {
        return SCREEN_ORIENTATION_LANDSCAPE;
    }
    return (ScreenOrientationMode)mode;
}

ScreenOrientationMode androidScreenOrientationMode(void)
{
    return g_frontendSettings ?
        normalizeScreenOrientationMode((int)g_frontendSettings->screenOrientationMode) :
        SCREEN_ORIENTATION_LANDSCAPE;
}

static bool androidCurrentScreenIsPortrait(void)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        return false;
    }
    bool portrait = false;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "isCurrentScreenPortrait", "()Z") : NULL;
    if (method)
    {
        portrait = env->CallBooleanMethod(activity, method) == JNI_TRUE;
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        portrait = false;
    }
    if (activityClass) env->DeleteLocalRef(activityClass);
    return portrait;
}

void androidSetScreenOrientationMode(ScreenOrientationMode mode)
{
    mode = normalizeScreenOrientationMode((int)mode);
    if (g_frontendSettings)
    {
        g_frontendSettings->screenOrientationMode = mode;
    }
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        return;
    }
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "setScreenOrientationMode", "(I)V") : NULL;
    if (method)
    {
        env->CallVoidMethod(activity, method, (jint)mode);
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    if (activityClass) env->DeleteLocalRef(activityClass);
}

void syncAndroidScreenOrientation(void)
{
    if (!g_frontendSettings)
    {
        return;
    }
    ScreenOrientationMode mode = androidScreenOrientationMode();
    g_frontendSettings->screenOrientationMode = mode;
    g_frontendSettings->portraitMode = mode == SCREEN_ORIENTATION_PORTRAIT ||
        (mode == SCREEN_ORIENTATION_AUTO && androidCurrentScreenIsPortrait());
}

std::string androidAppVersionName(void)
{
    static const std::string cachedVersion = []()
    {
        JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
        JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
        if (!env || !activity)
        {
            return std::string("unknown");
        }

        std::string version = "unknown";
        jclass activityClass = env->GetObjectClass(activity);
        jmethodID method = activityClass ? env->GetMethodID(activityClass,
            "getAppVersionName", "()Ljava/lang/String;") : NULL;
        jstring result = method ? (jstring)env->CallObjectMethod(activity, method) : NULL;
        if (!env->ExceptionCheck() && result)
        {
            const char* text = env->GetStringUTFChars(result, NULL);
            if (text && text[0])
            {
                version = text;
            }
            if (text) env->ReleaseStringUTFChars(result, text);
        }
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
        }
        if (result) env->DeleteLocalRef(result);
        if (activityClass) env->DeleteLocalRef(activityClass);
        return version;
    }();
    return cachedVersion;
}

static void showAndroidMessageDialog(
    const std::string& title,
    const std::string& body,
    const std::string& positiveButton)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        return;
    }

    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "showMessageDialog",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V") : NULL;
    jstring javaTitle = method ? env->NewStringUTF(title.c_str()) : NULL;
    jstring javaBody = method ? env->NewStringUTF(body.c_str()) : NULL;
    jstring javaPositiveButton = method ? env->NewStringUTF(positiveButton.c_str()) : NULL;
    if (method && javaTitle && javaBody && javaPositiveButton)
    {
        env->CallVoidMethod(activity, method, javaTitle, javaBody, javaPositiveButton);
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    if (javaTitle) env->DeleteLocalRef(javaTitle);
    if (javaBody) env->DeleteLocalRef(javaBody);
    if (javaPositiveButton) env->DeleteLocalRef(javaPositiveButton);
    if (activityClass) env->DeleteLocalRef(activityClass);
}

void showAndroidMessageDialog(const std::string& title, const std::string& body)
{
    showAndroidMessageDialog(title, body,
        androidChineseUi() ? u8"\u786e\u5b9a" : "OK");
}

bool showAndroidConfirmationDialog(
    const std::string& title,
    const std::string& body,
    const std::string& positiveButton,
    const std::string& negativeButton)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        return false;
    }

    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "showConfirmationDialog",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z") : NULL;
    jstring javaTitle = method ? env->NewStringUTF(title.c_str()) : NULL;
    jstring javaBody = method ? env->NewStringUTF(body.c_str()) : NULL;
    jstring javaPositiveButton = method ? env->NewStringUTF(positiveButton.c_str()) : NULL;
    jstring javaNegativeButton = method ? env->NewStringUTF(negativeButton.c_str()) : NULL;
    bool confirmed = false;
    if (method && javaTitle && javaBody && javaPositiveButton && javaNegativeButton)
    {
        confirmed = env->CallBooleanMethod(activity, method, javaTitle, javaBody,
            javaPositiveButton, javaNegativeButton) == JNI_TRUE;
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        confirmed = false;
    }
    if (javaTitle) env->DeleteLocalRef(javaTitle);
    if (javaBody) env->DeleteLocalRef(javaBody);
    if (javaPositiveButton) env->DeleteLocalRef(javaPositiveButton);
    if (javaNegativeButton) env->DeleteLocalRef(javaNegativeButton);
    if (activityClass) env->DeleteLocalRef(activityClass);
    return confirmed;
}

static void requestAndroidGameImport(void)
{
    if (g_androidGameImportPending)
    {
        return;
    }
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        return;
    }
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ?
        env->GetMethodID(activityClass, "requestGameSelection", "(Z)Z") : NULL;
    if (method)
    {
        g_androidGameImportPending =
            env->CallBooleanMethod(activity, method,
                androidChineseUi() ? JNI_TRUE : JNI_FALSE) == JNI_TRUE;
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        g_androidGameImportPending = false;
    }
    if (activityClass) env->DeleteLocalRef(activityClass);
}

static void showAndroidLanFileManager(void)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        return;
    }
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "showLanFileManager", "(Z)V") : NULL;
    if (method)
    {
        env->CallVoidMethod(activity, method,
            androidChineseUi() ? JNI_TRUE : JNI_FALSE);
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    if (activityClass)
    {
        env->DeleteLocalRef(activityClass);
    }
}

static void rememberAndroidGameRun(const std::string& path)
{
    if (path.empty())
    {
        return;
    }

    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        return;
    }
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "rememberGameRun", "(Ljava/lang/String;)V") : NULL;
    jstring pathText = method ? env->NewStringUTF(path.c_str()) : NULL;
    if (pathText)
    {
        env->CallVoidMethod(activity, method, pathText);
        env->DeleteLocalRef(pathText);
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    if (activityClass)
    {
        env->DeleteLocalRef(activityClass);
    }
}

static bool consumeAndroidGameImport(void)
{
    if (!g_androidGameImportPending)
    {
        return false;
    }
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        return false;
    }
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "consumeSelectedGamePath", "()Ljava/lang/String;") : NULL;
    jstring result = method ? (jstring)env->CallObjectMethod(activity, method) : NULL;
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        result = NULL;
    }
    if (activityClass) env->DeleteLocalRef(activityClass);
    if (!result)
    {
        return false;
    }
    const char* path = env->GetStringUTFChars(result, NULL);
    bool imported = path && path[0];
    if (path) env->ReleaseStringUTFChars(result, path);
    env->DeleteLocalRef(result);
    g_androidGameImportPending = false;
    return imported;
}

static bool queryAndroidGameLibraryScanState(
    int* outProcessedEntries, int* outTotalEntries)
{
    if (outProcessedEntries) *outProcessedEntries = 0;
    if (outTotalEntries) *outTotalEntries = 0;
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        return false;
    }
    bool scanning = false;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID isScanningMethodId = activityClass ? env->GetMethodID(activityClass,
        "isGameLibraryScanning", "()Z") : NULL;
    if (isScanningMethodId)
    {
        scanning = env->CallBooleanMethod(activity, isScanningMethodId) == JNI_TRUE;
    }
    if (scanning && activityClass)
    {
        jmethodID processedEntriesMethodId = env->GetMethodID(activityClass,
            "getGameLibraryScanProcessedEntries", "()I");
        jmethodID totalEntriesMethodId = env->GetMethodID(activityClass,
            "getGameLibraryScanTotalEntries", "()I");
        if (outProcessedEntries && processedEntriesMethodId)
        {
            *outProcessedEntries = (int)env->CallIntMethod(activity, processedEntriesMethodId);
        }
        if (outTotalEntries && totalEntriesMethodId)
        {
            *outTotalEntries = (int)env->CallIntMethod(activity, totalEntriesMethodId);
        }
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        scanning = false;
    }
    if (activityClass)
    {
        env->DeleteLocalRef(activityClass);
    }
    return scanning;
}

static bool isAndroidGameLibraryScanning(void)
{
    return queryAndroidGameLibraryScanState(NULL, NULL);
}

static bool removeAndroidGamePath(const std::string& path)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        return false;
    }

    bool removed = false;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "removeGamePath", "(Ljava/lang/String;)Z") : NULL;
    jstring pathText = method ? env->NewStringUTF(path.c_str()) : NULL;
    if (pathText)
    {
        removed = env->CallBooleanMethod(activity, method, pathText) == JNI_TRUE;
        env->DeleteLocalRef(pathText);
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        removed = false;
    }
    if (activityClass)
    {
        env->DeleteLocalRef(activityClass);
    }
    return removed;
}

static int androidUiScale(void)
{
    int width = 0;
    int height = 0;
    if (g_renderer)
    {
        SDL_GetRendererOutputSize(g_renderer, &width, &height);
    }
    int scale = std::min(width / 960, height / 540);
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    return scale;
}

int androidUiMetric(int value)
{
    int width = 0;
    int height = 0;
    if (g_renderer)
    {
        SDL_GetRendererOutputSize(g_renderer, &width, &height);
    }
    double scale = std::min(width / 960.0, height / 540.0);
    if (scale < 0.75) scale = 0.75;
    if (scale > 4.0) scale = 4.0;
    return std::max(1, (int)lround(value * scale));
}

static bool virtualButtonHasControl(const VirtualControlButton& button, uint32_t controlBit);
static int buildVirtualControls(VirtualControlButton* outButtons, int maxButtons);

static int androidFpsOverlayScale(void)
{
    VirtualControlButton buttons[kVirtualControlButtonCapacity];
    int count = buildVirtualControls(buttons, kVirtualControlButtonCapacity);
    int scale = count > 8 ? buttons[8].rect.h / 18 : androidUiScale();
    if (scale < 2) scale = 2;
    if (scale > 8) scale = 8;
    return scale;
}

SDL_Rect androidMenuButtonRect(int width)
{
    VirtualControlButton buttons[kVirtualControlButtonCapacity];
    int count = buildVirtualControls(buttons, kVirtualControlButtonCapacity);
    for (int i = 0; i < count; ++i)
    {
        if (virtualButtonHasControl(buttons[i], CONTROL_BUTTON_START))
        {
            const int gap = std::max(6, buttons[i].rect.h / 4);
            return SDL_Rect{ buttons[i].rect.x, buttons[i].rect.y - buttons[i].rect.h - gap,
                buttons[i].rect.w, buttons[i].rect.h };
        }
    }
    return SDL_Rect{ width / 2 - 52, 12, 104, 42 };
}

int virtualCompactButtonTextSize(const SDL_Rect& rect)
{
    const int scalePercent = virtualControlScalePercent();
    const int minimumSize = std::max(1,
        (androidUiMetric(12) * scalePercent + 50) / 100);
    const int maximumSize = std::max(minimumSize,
        (androidUiMetric(18) * scalePercent + 50) / 100);
    return std::max(minimumSize, std::min(maximumSize, rect.h / 3));
}

static int virtualButtonTextSize(const SDL_Rect& rect)
{
    const int minimumSize = std::max(1,
        (16 * virtualControlScalePercent() + 50) / 100);
    return std::max(minimumSize, rect.h * 2 / 5);
}

static int androidMenuScreenRowCount(void)
{
    switch (g_androidMenuScreen)
    {
    case ANDROID_MENU_PAUSE: return ANDROID_PAUSE_ROW_COUNT;
    case ANDROID_MENU_SAVE_STATE: return 0;
    case ANDROID_MENU_MAIN: return ANDROID_MAIN_ROW_COUNT;
    case ANDROID_MENU_ABOUT: return ANDROID_ABOUT_ROW_COUNT;
    case ANDROID_MENU_OPTIONS: return ANDROID_OPTIONS_ROW_COUNT;
    case ANDROID_MENU_SETTINGS: return ANDROID_SETTINGS_ROW_COUNT;
    case ANDROID_MENU_VIDEO: return ANDROID_VIDEO_ROW_COUNT;
    case ANDROID_MENU_AUDIO: return ANDROID_AUDIO_ROW_COUNT;
    case ANDROID_MENU_INPUT: return ANDROID_INPUT_ROW_COUNT;
    case ANDROID_MENU_CONTROLLER_MAPPING: return ANDROID_CONTROLLER_MAPPING_ROW_COUNT;
    case ANDROID_MENU_CONTROLLER_CALIBRATION: return ANDROID_CONTROLLER_CALIBRATION_ROW_COUNT;
    case ANDROID_MENU_CHEAT_MANAGER:
        return ANDROID_CHEAT_MANAGER_FEATURE_FIRST +
            (int)cheatRuntimeGetStatus().entries.size() +
            ANDROID_CHEAT_MANAGER_ACTION_COUNT;
    default: return 0;
    }
}

static int androidMenuRowHeight(void)
{
    return androidUiMetric(kAndroidMenuRowHeight);
}

static int androidMenuRowStep(void)
{
    return androidUiMetric(kAndroidMenuRowHeight + kAndroidMenuRowGap);
}

static int androidMenuListContentHeight(int rowCount)
{
    if (rowCount <= 0)
    {
        return 0;
    }
    return (rowCount - 1) * androidMenuRowStep() + androidMenuRowHeight();
}

SDL_Rect androidPanelRect(int width, int height)
{
    int horizontalMargin = androidUiMetric(24);
    int verticalMargin = androidUiMetric(24);
    int panelWidth = width * 4 / 5;
    panelWidth = std::min(panelWidth, androidUiMetric(760));
    panelWidth = std::max(panelWidth, androidUiMetric(360));
    panelWidth = std::min(panelWidth, std::max(1, width - 2 * horizontalMargin));
    int rowCount = androidMenuScreenRowCount();
    int contentHeight = androidUiMetric(kAndroidMenuRowTop + kAndroidMenuListBottomInset);
    if (g_androidMenuScreen == ANDROID_MENU_SAVE_STATE)
    {
        const int rowHeight = kAndroidMenuRowHeight;
        const int rowGap = kAndroidMenuRowGap;
        contentHeight = androidUiMetric(kAndroidMenuRowTop +
            rowHeight + rowGap + 5 * rowHeight + 4 * rowGap +
            rowGap + rowHeight + kAndroidMenuListBottomInset);
    }
    else if (rowCount > 0)
    {
        contentHeight += androidMenuListContentHeight(rowCount);
    }
    int maximumHeight = std::max(1, height - 2 * verticalMargin);
    int panelHeight = std::min(maximumHeight, std::max(androidUiMetric(220), contentHeight));
    return SDL_Rect{ (width - panelWidth) / 2, (height - panelHeight) / 2,
        panelWidth, panelHeight };
}

SDL_Rect androidPanelRowRect(const SDL_Rect& panel, int row)
{
    int horizontalInset = androidUiMetric(kAndroidMenuRowHorizontalInset);
    return SDL_Rect{ panel.x + horizontalInset,
        panel.y + androidUiMetric(kAndroidMenuRowTop) +
            row * androidMenuRowStep(),
        panel.w - 2 * horizontalInset,
        androidMenuRowHeight() };
}

SDL_Rect androidMenuRowRect(const SDL_Rect& panel, int row)
{
    SDL_Rect rect = androidPanelRowRect(panel, row);
    rect.y -= g_androidMenuScrollOffset;
    return rect;
}

bool androidMenuScreenHasSettingsList(void)
{
    return g_androidMenuScreen == ANDROID_MENU_MAIN ||
        g_androidMenuScreen == ANDROID_MENU_OPTIONS ||
        g_androidMenuScreen == ANDROID_MENU_SETTINGS ||
        g_androidMenuScreen == ANDROID_MENU_VIDEO ||
        g_androidMenuScreen == ANDROID_MENU_AUDIO ||
        g_androidMenuScreen == ANDROID_MENU_INPUT ||
        g_androidMenuScreen == ANDROID_MENU_CONTROLLER_MAPPING ||
        g_androidMenuScreen == ANDROID_MENU_CONTROLLER_CALIBRATION ||
        g_androidMenuScreen == ANDROID_MENU_CHEAT_MANAGER;
}

SDL_Rect androidMenuViewportRect(const SDL_Rect& panel)
{
    int horizontalInset = androidUiMetric(16);
    return SDL_Rect{ panel.x + horizontalInset,
        panel.y + androidUiMetric(kAndroidMenuRowTop),
        panel.w - 2 * horizontalInset,
        std::max(1, panel.h -
            androidUiMetric(kAndroidMenuRowTop + kAndroidMenuListBottomInset)) };
}

int androidMenuMaxScroll(const SDL_Rect& panel, int rowCount)
{
    int contentHeight = androidMenuListContentHeight(rowCount);
    return std::max(0, contentHeight - androidMenuViewportRect(panel).h);
}

void clampAndroidMenuScroll(const SDL_Rect& panel, int rowCount)
{
    int maxScroll = androidMenuMaxScroll(panel, rowCount);
    if (g_androidMenuScrollOffset < 0) g_androidMenuScrollOffset = 0;
    if (g_androidMenuScrollOffset > maxScroll) g_androidMenuScrollOffset = maxScroll;
}

struct AndroidLibraryLayout
{
    int scale;
    int margin;
    int viewportHeight;
    int top;
    int rowStep;
    int rowHeight;
    int rowGap;
    int cardWidth;
    int actionWidth;
    bool compact;
};

struct AndroidThemeColors
{
    SDL_Color card;
    SDL_Color cardBorder;
    SDL_Color button;
    SDL_Color buttonBorder;
    SDL_Color icon;
    SDL_Color iconBorder;
    SDL_Color text;
    SDL_Color buttonText;
    SDL_Color mutedText;
};

static AndroidThemeColors androidThemeColors(void);
static SDL_Color androidThemeBlend(SDL_Color first, SDL_Color second,
    int secondWeight, uint8_t alpha);

static const int kAndroidLibraryHeaderTop = 10;
static const int kAndroidLibraryActionTop = 18;
static const int kAndroidLibraryHeaderHeight = 44;
static const int kAndroidLibraryHeaderBottomGap = 26;
static const int kAndroidLibraryBrandTextSize = 32;

static int androidLibraryHorizontalMargin(int width)
{
    int scale = androidUiScale();
    return std::max(16 * scale, std::min(48 * scale, width / 16));
}

static AndroidLibraryLayout androidLibraryLayout(int width, int height)
{
    AndroidLibraryLayout layout = {};
    layout.scale = androidUiScale();
    int scale = layout.scale;
    layout.margin = androidLibraryHorizontalMargin(width);
    layout.cardWidth = std::max(1, width - 2 * layout.margin);
    if (layout.cardWidth > 920 * scale)
    {
        layout.cardWidth = 920 * scale;
    }
    layout.top = (kAndroidLibraryHeaderTop + kAndroidLibraryHeaderHeight +
        kAndroidLibraryHeaderBottomGap) * scale;
    layout.rowHeight = 64 * scale;
    layout.viewportHeight = std::max(1, height - layout.top - 16 * scale);
    int availableHeight = layout.viewportHeight;
    if (availableHeight < layout.rowHeight)
    {
        layout.rowHeight = std::max(44 * scale, availableHeight);
    }
    int defaultGap = 10 * scale;
    int heightGap = std::max(4 * scale, availableHeight / 24);
    layout.rowGap = std::min(defaultGap, heightGap);
    layout.rowStep = layout.rowHeight + layout.rowGap;
    int alignedVisibleCount = std::max(1, (availableHeight + layout.rowGap) /
        std::max(1, layout.rowStep));
    layout.viewportHeight = alignedVisibleCount * layout.rowStep - layout.rowGap;
    layout.compact = layout.cardWidth < 560 * scale;
    int preferredActionWidth = layout.cardWidth < 400 * scale ? 58 * scale :
        (layout.compact ? 76 * scale : 96 * scale);
    int minimumNameWidth = layout.cardWidth < 400 * scale ? 72 * scale : 96 * scale;
    int availableActionWidth = layout.cardWidth - 24 * scale - minimumNameWidth;
    layout.actionWidth = std::min(preferredActionWidth,
        std::max(52 * scale, availableActionWidth));
    return layout;
}

static int androidLibraryVisibleCount(int width, int height)
{
    AndroidLibraryLayout layout = androidLibraryLayout(width, height);
    return std::max(1, (layout.viewportHeight + layout.rowGap) /
        std::max(1, layout.rowStep));
}

static int androidLibraryMaxScroll(int width, int height, int count)
{
    AndroidLibraryLayout layout = androidLibraryLayout(width, height);
    int contentHeight = std::max(0, count * layout.rowStep - layout.rowGap);
    return std::max(0, contentHeight - layout.viewportHeight);
}

static void clampAndroidLibraryScroll(int width, int height)
{
    int maxScroll = androidLibraryMaxScroll(width, height, (int)g_androidGamePaths.size());
    if (g_androidLibraryScrollOffset < 0) g_androidLibraryScrollOffset = 0;
    if (g_androidLibraryScrollOffset > maxScroll) g_androidLibraryScrollOffset = maxScroll;
}

static void updateAndroidLibraryScrollInertia(int width, int height)
{
    if (g_androidLibraryScrollDragging || fabs(g_androidLibraryScrollVelocity) < 0.05f)
    {
        g_androidLibraryScrollVelocity = 0.0f;
        g_androidLibraryScrollLastMotionTicks = SDL_GetTicks64();
        return;
    }

    uint64_t now = SDL_GetTicks64();
    uint64_t elapsed = g_androidLibraryScrollLastMotionTicks == 0 ? 16 :
        now - g_androidLibraryScrollLastMotionTicks;
    elapsed = std::min<uint64_t>(elapsed, 50);
    float frameScale = (float)std::max<uint64_t>(1, elapsed) / 16.0f;
    int previousOffset = g_androidLibraryScrollOffset;
    g_androidLibraryScrollOffset += (int)lround(g_androidLibraryScrollVelocity * frameScale);
    clampAndroidLibraryScroll(width, height);
    int maxScroll = androidLibraryMaxScroll(width, height, (int)g_androidGamePaths.size());
    if ((g_androidLibraryScrollOffset == 0 && g_androidLibraryScrollVelocity < 0.0f) ||
        (g_androidLibraryScrollOffset == maxScroll && g_androidLibraryScrollVelocity > 0.0f) ||
        g_androidLibraryScrollOffset == previousOffset)
    {
        g_androidLibraryScrollVelocity = 0.0f;
    }
    else
    {
        g_androidLibraryScrollVelocity *= (float)pow(0.88, frameScale);
    }
    g_androidLibraryScrollLastMotionTicks = now;
}

static SDL_Rect androidLibraryRowRect(int width, int height, int row)
{
    AndroidLibraryLayout layout = androidLibraryLayout(width, height);
    return SDL_Rect{ (width - layout.cardWidth) / 2,
        layout.top + row * layout.rowStep - g_androidLibraryScrollOffset,
        layout.cardWidth, layout.rowHeight };
}

static SDL_Rect androidLibraryRemoveButtonRect(int width, int height, int row)
{
    AndroidLibraryLayout layout = androidLibraryLayout(width, height);
    SDL_Rect card = androidLibraryRowRect(width, height, row);
    return SDL_Rect{ card.x + card.w - layout.actionWidth - 10 * layout.scale,
        card.y + 8 * layout.scale, layout.actionWidth, card.h - 16 * layout.scale };
}

static SDL_Rect androidLibraryAddButtonRect(int width);

static SDL_Rect androidLibrarySettingsButtonRect(int width)
{
    int scale = androidUiScale();
    SDL_Rect addButton = androidLibraryAddButtonRect(width);
    int buttonWidth = std::min(124 * scale, std::max(92 * scale, width / 4));
    int gap = 12 * scale;
    int x = addButton.x - gap - buttonWidth;
    if (x < 16 * scale)
    {
        x = 16 * scale;
        buttonWidth = std::max(1, addButton.x - gap - x);
    }
    return SDL_Rect{ x, addButton.y, buttonWidth, addButton.h };
}

static SDL_Rect androidLibraryFileManagerButtonRect(int width)
{
    int scale = androidUiScale();
    SDL_Rect settingsButton = androidLibrarySettingsButtonRect(width);
    int gap = 12 * scale;
    int buttonSize = settingsButton.h;
    return SDL_Rect{ settingsButton.x - gap - buttonSize,
        settingsButton.y, buttonSize, buttonSize };
}

static SDL_Rect androidLibraryAddButtonRect(int width)
{
    int scale = androidUiScale();
    int margin = androidLibraryHorizontalMargin(width);
    int buttonWidth = std::min(164 * scale, std::max(124 * scale, width / 3));
    return SDL_Rect{ width - margin - buttonWidth, kAndroidLibraryActionTop * scale,
        buttonWidth, kAndroidLibraryHeaderHeight * scale };
}
void drawAndroidRect(const SDL_Rect& rect, SDL_Color color)
{
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(g_renderer, &rect);
}

void drawAndroidOutline(const SDL_Rect& rect, SDL_Color color)
{
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawRect(g_renderer, &rect);
}

static void drawAndroidFolderIcon(const SDL_Rect& button,
    const AndroidThemeColors& colors)
{
    const int scale = androidUiScale();
    const int inset = std::max(10 * scale, button.w / 4);
    const int left = button.x + inset;
    const int top = button.y + inset;
    const int right = button.x + button.w - inset;
    const int bottom = button.y + button.h - inset;
    const int tabHeight = std::max(4 * scale, button.h / 9);
    const int bodyTop = top + tabHeight;
    const int bodyWidth = std::max(1, right - left);
    const int upperWidth = std::max(tabHeight + 1, bodyWidth * 3 / 5);
    const int tabSlopeEnd = std::min(right, left + upperWidth);
    const int tabRight = std::max(left + 1, tabSlopeEnd - tabHeight);
    const int dividerHeight = std::min(3, std::max(1, bottom - bodyTop));
    const SDL_Color fill = colors.buttonText;
    const SDL_Color outline = colors.iconBorder;
    const SDL_Vertex vertices[] = {
        { SDL_FPoint{ (float)left, (float)top }, fill, SDL_FPoint{ 0.0f, 0.0f } },
        { SDL_FPoint{ (float)tabRight, (float)top }, fill, SDL_FPoint{ 0.0f, 0.0f } },
        { SDL_FPoint{ (float)tabSlopeEnd, (float)bodyTop }, fill,
            SDL_FPoint{ 0.0f, 0.0f } },
        { SDL_FPoint{ (float)left, (float)bodyTop }, fill, SDL_FPoint{ 0.0f, 0.0f } },
        { SDL_FPoint{ (float)right, (float)bodyTop }, fill, SDL_FPoint{ 0.0f, 0.0f } },
        { SDL_FPoint{ (float)right, (float)bottom }, fill, SDL_FPoint{ 0.0f, 0.0f } },
        { SDL_FPoint{ (float)left, (float)bottom }, fill, SDL_FPoint{ 0.0f, 0.0f } }
    };
    const int indices[] = {
        0, 1, 2, 0, 2, 3,
        3, 4, 5, 3, 5, 6
    };
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(g_renderer, NULL, vertices,
        (int)(sizeof(vertices) / sizeof(vertices[0])), indices,
        (int)(sizeof(indices) / sizeof(indices[0])));

    const SDL_Rect divider = { left, bodyTop,
        std::max(1, right - left), dividerHeight };
    drawAndroidRect(divider, outline);
    SDL_SetRenderDrawColor(g_renderer, outline.r, outline.g, outline.b, outline.a);
    SDL_Point points[] = {
        { left, bodyTop },
        { left, top },
        { tabRight, top },
        { tabSlopeEnd, bodyTop },
        { right, bodyTop },
        { right, bottom },
        { left, bottom },
        { left, bodyTop }
    };
    SDL_RenderDrawLines(g_renderer, points, (int)(sizeof(points) / sizeof(points[0])));
}

static void appendAndroidPersistedGamePaths(void)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        return;
    }

    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "listImportedGamePaths", "()[Ljava/lang/String;") : NULL;
    jobjectArray paths = method ?
        (jobjectArray)env->CallObjectMethod(activity, method) : NULL;
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        paths = NULL;
    }

    if (paths)
    {
        jsize count = env->GetArrayLength(paths);
        for (jsize i = 0; i < count; ++i)
        {
            jstring pathText = (jstring)env->GetObjectArrayElement(paths, i);
            const char* pathChars = pathText ? env->GetStringUTFChars(pathText, NULL) : NULL;
            if (pathChars)
            {
                std::string path(pathChars);
                bool supported = gamePathHasSupportedExtension(path);
                bool exists = supported && platformFileExists(path);
                if (supported && exists &&
                    std::find(g_androidGamePaths.begin(), g_androidGamePaths.end(), path) ==
                        g_androidGamePaths.end())
                {
                    g_androidGamePaths.push_back(path);
                }
                env->ReleaseStringUTFChars(pathText, pathChars);
            }
            if (pathText)
            {
                env->DeleteLocalRef(pathText);
            }
        }
        env->DeleteLocalRef(paths);
    }
    if (activityClass)
    {
        env->DeleteLocalRef(activityClass);
    }
}

static void refreshAndroidGameLibrary(void)
{
    g_androidGamePaths.clear();
    appendAndroidPersistedGamePaths();

}

static std::string androidGameDisplayName(const std::string& path)
{
    std::string name = gameFileNameFromPath(path);
    if (gamePathHasSupportedExtension(name))
    {
        name.resize(name.size() - (gamePathHasAppExtension(name) ? 4 : 3));
    }
    std::string lower = name;
    for (size_t i = 0; i < lower.size(); ++i)
    {
        lower[i] = (char)tolower((unsigned char)lower[i]);
    }
    for (size_t i = 0; i < name.size(); ++i)
    {
        unsigned char ch = (unsigned char)name[i];
        if (ch < 0x80)
        {
            name[i] = isalnum(ch) ? (char)toupper(ch) : ' ';
        }
    }
    if (name.empty())
    {
        return "GAME";
    }
    if (name.size() > 24)
    {
        size_t end = 0;
        while (end < name.size() && end < 24)
        {
            unsigned char lead = (unsigned char)name[end];
            size_t length = 1;
            if ((lead & 0xe0) == 0xc0) length = 2;
            else if ((lead & 0xf0) == 0xe0) length = 3;
            else if ((lead & 0xf8) == 0xf0) length = 4;
            if (end + length > 24)
            {
                break;
            }
            end += length;
        }
        name.resize(end);
    }
    return name;
}

static bool confirmAndroidGameRemoval(const std::string& path)
{
    std::string name = androidGameDisplayName(path);
    if (androidChineseUi())
    {
        std::string body = u8"\u4ece\u6e38\u620f\u5217\u8868\u4e2d\u79fb\u9664\u201c";
        body += name;
        body += u8"\u201d\uff1f\n\u6e38\u620f\u6587\u4ef6\u4ecd\u4f1a\u4fdd\u7559\u3002";
        return showAndroidConfirmationDialog(kZhRemoveGame, body, kZhRemove, kZhCancel);
    }

    std::string body = "Remove \"" + name + "\" from the game list?\n"
        "The game file will remain on your device.";
    return showAndroidConfirmationDialog("Remove Game", body, "Remove", "Cancel");
}

void requestAndroidGame(const std::string& path,
    AndroidMenuScreen screenAfterRestart = ANDROID_MENU_NONE)
{
    if (SDL_AtomicGet(&g_frontendGameLaunchPending) != 0)
    {
        return;
    }
    bool restartingCurrentGame = frontendGameRunning() &&
        !path.empty() && path == g_frontendCurrentGamePath;
    g_androidMenuScreenAfterGameRestart = restartingCurrentGame ?
        screenAfterRestart : ANDROID_MENU_NONE;
    g_androidMenuGameRestartPath = restartingCurrentGame ? path : "";
    if (frontendGameRunning())
    {
        frontendSetGamePaused(false);
        if (path.empty())
        {
            audioOutputReleaseGameResources();
            releaseGameVideoResources();
        }
    }
    if (presentBlackTransitionFrame())
    {
        SDL_Delay(16);
    }
    g_frontendPendingGamePath = path;
    SDL_AtomicSet(&g_frontendGameLaunchPending, 1);
    SDL_AtomicSet(&g_frontendLoopExitRequested, 1);
    SDL_AtomicSet(&g_frontendTransitionRequested, 1);
    g_androidMenuScreen = restartingCurrentGame ?
        ANDROID_MENU_NONE : ANDROID_MENU_LIBRARY;
    if (g_androidMenuScreen == ANDROID_MENU_LIBRARY)
    {
        refreshAndroidGameLibrary();
    }
    releaseVirtualPointerControls();
}

bool saveAndroidSettings(void)
{
    if (!g_frontendSettings)
    {
        return false;
    }
    bool saved = emulatorSaveSettings(*g_frontendSettings);
    emulatorApplySharedRuntimeSettings(*g_frontendSettings);
    framebufferSetProfileEnabled(runtimeLogProfileEnabled());
    gameRuntimeApplySettings();
    frontendApplyVideoSettings(*g_frontendSettings);
    frontendApplyAudioSettings(*g_frontendSettings);
    frontendApplyInputSettings(*g_frontendSettings);
    return saved;
}

void openAndroidMenu(AndroidMenuScreen screen)
{
    if (g_androidMenuScreen == ANDROID_MENU_SAVE_STATE &&
        screen != ANDROID_MENU_SAVE_STATE && g_androidSaveStateThumbnail)
    {
        SDL_DestroyTexture(g_androidSaveStateThumbnail);
        g_androidSaveStateThumbnail = NULL;
    }
    g_androidMenuScreen = screen;
    g_androidMenuSelectedRow = screen == ANDROID_MENU_SAVE_STATE ?
        std::max(0, std::min(kSaveStateSlotCount - 1, g_androidSaveStateSelectedSlot - 1)) :
        screen == ANDROID_MENU_ABOUT ? ANDROID_ABOUT_BACK : 0;
    g_androidMenuSelectionHighlightVisible = false;
    g_androidMenuScrollOffset = 0;
    g_androidMenuScrollDragging = false;
    g_androidMenuScrollMoved = false;
    if (frontendGameRunning())
    {
        frontendSetGamePaused(screen != ANDROID_MENU_NONE);
    }
    if (screen == ANDROID_MENU_SAVE_STATE)
    {
        g_androidSaveStateStatus.clear();
        if (g_androidSaveStateSlotCacheGamePath != g_frontendCurrentGamePath)
        {
            refreshAndroidSaveStateSlots();
        }
        refreshAndroidSaveStateThumbnail();
    }
    releaseVirtualPointerControls();
}

void navigateBackAndroidMenu(void)
{
    if (g_androidMenuScreen == ANDROID_MENU_PAUSE)
    {
        openAndroidMenu(ANDROID_MENU_NONE);
    }
    else if (g_androidMenuScreen == ANDROID_MENU_SAVE_STATE)
    {
        openAndroidMenu(ANDROID_MENU_PAUSE);
    }
    else if (g_androidMenuScreen == ANDROID_MENU_MAIN)
    {
        openAndroidMenu(frontendGameRunning() ?
            ANDROID_MENU_PAUSE : ANDROID_MENU_LIBRARY);
    }
    else if (g_androidMenuScreen == ANDROID_MENU_ABOUT)
    {
        openAndroidMenu(ANDROID_MENU_MAIN);
    }
    else if (g_androidMenuScreen == ANDROID_MENU_OPTIONS ||
        g_androidMenuScreen == ANDROID_MENU_SETTINGS)
    {
        openAndroidMenu(frontendGameRunning() ?
            ANDROID_MENU_PAUSE : ANDROID_MENU_MAIN);
    }
    else if (g_androidMenuScreen == ANDROID_MENU_VIDEO ||
        g_androidMenuScreen == ANDROID_MENU_AUDIO ||
        g_androidMenuScreen == ANDROID_MENU_INPUT)
    {
        openAndroidMenu(ANDROID_MENU_OPTIONS);
    }
    else if (g_androidMenuScreen == ANDROID_MENU_CONTROLLER_MAPPING)
    {
        cancelControllerMapping();
        openAndroidMenu(ANDROID_MENU_INPUT);
    }
    else if (g_androidMenuScreen == ANDROID_MENU_CONTROLLER_CALIBRATION)
    {
        cancelControllerCalibration();
        openAndroidMenu(ANDROID_MENU_INPUT);
    }
    else if (g_androidMenuScreen == ANDROID_MENU_CHEAT_MANAGER)
    {
        openAndroidMenu(ANDROID_MENU_SETTINGS);
    }
}

static void drawAndroidLibraryBrand(const char* title, int width)
{
    int scale = androidUiScale();
    SDL_Color titleColor = { 255, 255, 255, 255 };
    drawAndroidSystemTextBold(title, androidLibraryHorizontalMargin(width),
        (kAndroidLibraryHeaderTop +
            (kAndroidLibraryHeaderHeight - kAndroidLibraryBrandTextSize) / 2) * scale,
        kAndroidLibraryBrandTextSize * scale, titleColor);
}

static bool drawAndroidLibraryScreen(void)
{
    if (!g_renderer)
    {
        return false;
    }
    int width = 0;
    int height = 0;
    SDL_GetRendererOutputSize(g_renderer, &width, &height);
    int scale = androidUiScale();
    updateAndroidLibraryScrollInertia(width, height);
    int processedEntryCount = 0;
    int totalEntryCount = 0;
    bool isLibraryScanActive = queryAndroidGameLibraryScanState(
        &processedEntryCount, &totalEntryCount);
    if (g_androidGameLibraryScanWasActive && !isLibraryScanActive)
    {
        refreshAndroidGameLibrary();
    }
    g_androidGameLibraryScanWasActive = isLibraryScanActive;
    if (consumeAndroidGameImport())
    {
        refreshAndroidGameLibrary();
    }
    clampAndroidLibraryScroll(width, height);
    AndroidThemeColors colors = androidThemeColors();
    drawAndroidLibraryBrand("DingooPie", width);

    SDL_Rect addButton = androidLibraryAddButtonRect(width);
    drawAndroidRect(addButton, colors.button);
    drawAndroidOutline(addButton, colors.buttonBorder);
    drawAndroidSystemTextCentered(androidChineseUi() ? kZhAddGame : "Add Game",
        addButton, 17 * scale, colors.buttonText);
    SDL_Rect settingsButton = androidLibrarySettingsButtonRect(width);
    drawAndroidRect(settingsButton, colors.button);
    drawAndroidOutline(settingsButton, colors.buttonBorder);
    drawAndroidSystemTextCentered(androidChineseUi() ? kZhMenu : "Menu", settingsButton,
        17 * scale, colors.buttonText);
    SDL_Rect fileManagerButton = androidLibraryFileManagerButtonRect(width);
    drawAndroidRect(fileManagerButton, colors.button);
    drawAndroidOutline(fileManagerButton, colors.buttonBorder);
    drawAndroidFolderIcon(fileManagerButton, colors);

    if (g_androidGamePaths.empty())
    {
        SDL_Rect emptyRect = { 0, height / 2 - 40 * scale, width, 80 * scale };
        drawAndroidSystemTextCentered(androidChineseUi() ? kZhNoGames : "No Games",
            emptyRect, 26 * scale, SDL_Color{ 192, 192, 192, 255 });
    }
    else
    {
        AndroidLibraryLayout layout = androidLibraryLayout(width, height);
        int visibleCount = androidLibraryVisibleCount(width, height);
        int totalCount = (int)g_androidGamePaths.size();
        int firstRow = layout.rowStep > 0 ? g_androidLibraryScrollOffset / layout.rowStep : 0;
        if (firstRow > 0) --firstRow;
        int lastRow = std::min(totalCount, firstRow + visibleCount + 2);
        SDL_Rect viewport = { 0, layout.top, width, layout.viewportHeight };
        SDL_RenderSetClipRect(g_renderer, &viewport);
        const SDL_Color itemBackground = colors.button;
        const SDL_Color itemBackgroundBorder = colors.buttonBorder;
        const SDL_Color itemControl = androidThemeBlend(
            colors.card, colors.button, 40, colors.card.a);
        const SDL_Color itemControlBorder = androidThemeBlend(
            colors.cardBorder, colors.buttonBorder, 40, colors.cardBorder.a);
        for (int i = firstRow; i < lastRow; ++i)
        {
            SDL_Rect card = androidLibraryRowRect(width, height, i);
            drawAndroidRect(card, itemBackground);
            drawAndroidOutline(card, itemBackgroundBorder);
            int iconSize = std::max(32 * scale, card.h - 16 * scale);
            SDL_Rect icon = { card.x + 10 * scale, card.y + (card.h - iconSize) / 2,
                iconSize, iconSize };
            drawAndroidRect(icon, itemControl);
            drawAndroidOutline(icon, itemControlBorder);
            const char* typeLabel = gamePathHasAppExtension(g_androidGamePaths[(size_t)i]) ?
                "APP" : "CC";
            drawAndroidSystemTextCentered(typeLabel, icon, 17 * scale,
                colors.text);
            std::string name = androidGameDisplayName(g_androidGamePaths[(size_t)i]);
            SDL_Rect removeRect = androidLibraryRemoveButtonRect(width, height, i);
            int nameX = icon.x + icon.w + 12 * scale;
            SDL_Rect nameRect = { nameX, card.y,
                std::max(1, removeRect.x - nameX - 10 * scale), card.h };
            drawAndroidSystemTextLeftCentered(name.c_str(), nameRect, 0, 23 * scale,
                colors.text);
            drawAndroidRect(removeRect, itemControl);
            drawAndroidOutline(removeRect, itemControlBorder);
            drawAndroidSystemTextCentered(androidChineseUi() ? kZhRemove : "Remove",
                removeRect, androidLibraryLayout(width, height).compact ? 14 * scale : 17 * scale,
                colors.buttonText);
        }
        SDL_RenderSetClipRect(g_renderer, NULL);
        if (totalCount > visibleCount)
        {
            const int viewportHeight = layout.viewportHeight;
            const int trackWidth = std::max(4, 6 * scale);
            const int trackX = width - layout.margin / 2 - trackWidth / 2;
            SDL_Rect track = { trackX, layout.top, trackWidth, viewportHeight };
            drawAndroidRect(track, SDL_Color{ 80, 80, 80, 180 });
            int thumbHeight = std::max(24 * scale,
                viewportHeight * visibleCount / totalCount);
            int maxScroll = androidLibraryMaxScroll(width, height, totalCount);
            int thumbTravel = std::max(0, viewportHeight - thumbHeight);
            int thumbY = layout.top;
            if (maxScroll > 0)
            {
                thumbY += thumbTravel * g_androidLibraryScrollOffset / maxScroll;
            }
            SDL_Rect thumb = { trackX, thumbY, trackWidth, thumbHeight };
            drawAndroidRect(thumb, colors.buttonBorder);
        }
    }
    if (isLibraryScanActive)
    {
        SDL_Rect dim = { 0, 0, width, height };
        drawAndroidRect(dim, SDL_Color{ 0, 0, 0, 150 });
        SDL_Rect message = { 0, height / 2 - 70 * scale, width, 42 * scale };
        drawAndroidSystemTextCentered(androidChineseUi() ? u8"\u6b63\u5728\u626b\u63cf\u6e38\u620f\u2026" :
            u8"Scanning games\u2026",
            message, 24 * scale, SDL_Color{ 255, 255, 255, 255 });
        int barWidth = std::min(width * 3 / 5, 520 * scale);
        int barHeight = std::max(6, 8 * scale);
        SDL_Rect bar = { (width - barWidth) / 2, height / 2,
            barWidth, barHeight };
        drawAndroidRect(bar, SDL_Color{ 80, 80, 80, 180 });
        if (totalEntryCount > 0)
        {
            int completedWidth = std::max(1,
                barWidth * std::min(processedEntryCount, totalEntryCount) / totalEntryCount);
            SDL_Rect completed = { bar.x, bar.y, completedWidth, barHeight };
            drawAndroidRect(completed, SDL_Color{ 255, 255, 255, 255 });
            char progressText[32];
            int completedEntries = std::min(processedEntryCount, totalEntryCount);
            int progressPercent = completedEntries * 100 / totalEntryCount;
            snprintf(progressText, sizeof(progressText), "%d%%", progressPercent);
            SDL_Rect progressRect = { 0, bar.y + 14 * scale, width, 30 * scale };
            drawAndroidSystemTextCentered(progressText, progressRect, 17 * scale,
                SDL_Color{ 220, 220, 220, 255 });
        }
        else
        {
            int segmentWidth = std::max(24 * scale, barWidth / 4);
            int travel = std::max(1, barWidth - segmentWidth);
            int segmentX = bar.x + (int)((SDL_GetTicks64() / 5) % (uint64_t)travel);
            SDL_Rect segment = { segmentX, bar.y, segmentWidth, barHeight };
            drawAndroidRect(segment, SDL_Color{ 255, 255, 255, 255 });
        }
    }
    if (androidMenuScreenUsesOverlay(g_androidMenuScreen))
    {
        drawAndroidMenuOverlay();
    }
    SDL_RenderPresent(g_renderer);
    return true;
}

int androidSettingsMenuRowCount(void)
{
    switch (g_androidMenuScreen)
    {
    case ANDROID_MENU_MAIN: return ANDROID_MAIN_ROW_COUNT;
    case ANDROID_MENU_SAVE_STATE: return 0;
    case ANDROID_MENU_OPTIONS: return ANDROID_OPTIONS_ROW_COUNT;
    case ANDROID_MENU_SETTINGS: return ANDROID_SETTINGS_ROW_COUNT;
    case ANDROID_MENU_VIDEO: return ANDROID_VIDEO_ROW_COUNT;
    case ANDROID_MENU_AUDIO: return ANDROID_AUDIO_ROW_COUNT;
    case ANDROID_MENU_INPUT: return ANDROID_INPUT_ROW_COUNT;
    case ANDROID_MENU_CONTROLLER_MAPPING: return ANDROID_CONTROLLER_MAPPING_ROW_COUNT;
    case ANDROID_MENU_CONTROLLER_CALIBRATION: return ANDROID_CONTROLLER_CALIBRATION_ROW_COUNT;
    case ANDROID_MENU_CHEAT_MANAGER:
        return ANDROID_CHEAT_MANAGER_FEATURE_FIRST +
            (int)cheatRuntimeGetStatus().entries.size() +
            ANDROID_CHEAT_MANAGER_ACTION_COUNT;
    default: return 0;
    }
}

static SDL_JoystickID activeGameControllerInstanceId(void);

static int androidMenuSelectionRowCount(void)
{
    if (g_androidMenuScreen == ANDROID_MENU_PAUSE)
    {
        return ANDROID_PAUSE_ROW_COUNT;
    }
    if (g_androidMenuScreen == ANDROID_MENU_SAVE_STATE)
    {
        return kSaveStateSlotCount + 4;
    }
    if (g_androidMenuScreen == ANDROID_MENU_ABOUT)
    {
        return ANDROID_ABOUT_ROW_COUNT;
    }
    return androidSettingsMenuRowCount();
}

static void ensureAndroidMenuSelectionVisible(void)
{
    if (!g_renderer || !androidMenuScreenHasSettingsList())
    {
        return;
    }

    int width = 0;
    int height = 0;
    SDL_GetRendererOutputSize(g_renderer, &width, &height);
    SDL_Rect panel = androidPanelRect(width, height);
    SDL_Rect viewport = androidMenuViewportRect(panel);
    int rowStep = androidMenuRowStep();
    int rowTop = g_androidMenuSelectedRow * rowStep;
    int rowBottom = rowTop + androidMenuRowHeight();
    int viewportTop = g_androidMenuScrollOffset;
    int viewportBottom = viewportTop + viewport.h;
    if (rowTop < viewportTop)
    {
        g_androidMenuScrollOffset = rowTop;
    }
    else if (rowBottom > viewportBottom)
    {
        g_androidMenuScrollOffset = rowBottom - viewport.h;
    }
    clampAndroidMenuScroll(panel, androidSettingsMenuRowCount());
}

static void selectAndroidMenuRow(int row, bool showHighlight)
{
    g_androidMenuSelectedRow = row;
    g_androidMenuSelectionHighlightVisible = showHighlight;
    if (showHighlight)
    {
        ensureAndroidMenuSelectionVisible();
    }
}

static void hideAndroidMenuSelectionHighlight(void)
{
    g_androidMenuSelectionHighlightVisible = false;
}

static void moveAndroidMenuSelection(int direction)
{
    if (!g_androidMenuSelectionHighlightVisible)
    {
        selectAndroidMenuRow(0, true);
        return;
    }
    if (g_androidMenuScreen == ANDROID_MENU_ABOUT)
    {
        selectAndroidMenuRow(ANDROID_ABOUT_BACK, true);
        return;
    }
    int rowCount = androidMenuSelectionRowCount();
    if (rowCount <= 0 || direction == 0)
    {
        return;
    }
    int nextRow = (g_androidMenuSelectedRow + direction) % rowCount;
    if (nextRow < 0)
    {
        nextRow += rowCount;
    }
    selectAndroidMenuRow(nextRow, true);
}

static void activateAndroidMenuSelection(void)
{
    int row = g_androidMenuSelectedRow;
    if (g_androidMenuScreen == ANDROID_MENU_PAUSE)
    {
        if (row == ANDROID_PAUSE_SAVE_STATE) openAndroidMenu(ANDROID_MENU_SAVE_STATE);
        else if (row == ANDROID_PAUSE_SWITCH_GAME) requestAndroidSwitchGame();
        else if (row == ANDROID_PAUSE_RESTART_GAME) requestAndroidRestartGame();
        else if (row == ANDROID_PAUSE_OPTIONS) openAndroidMenu(ANDROID_MENU_OPTIONS);
        else if (row == ANDROID_PAUSE_SETTINGS) openAndroidMenu(ANDROID_MENU_SETTINGS);
        else if (row == ANDROID_PAUSE_EXIT_APPLICATION) requestAndroidExitApplication();
        else if (row == ANDROID_PAUSE_BACK) openAndroidMenu(ANDROID_MENU_NONE);
    }
    else if (g_androidMenuScreen == ANDROID_MENU_SAVE_STATE)
    {
        if (row < kSaveStateSlotCount)
        {
            g_androidSaveStateSelectedSlot = row + 1;
            refreshAndroidSaveStateSlotInfo(g_androidSaveStateSelectedSlot);
            refreshAndroidSaveStateThumbnail();
        }
        else if (row == kSaveStateSlotCount) performAndroidSaveStateAction(true);
        else if (row == kSaveStateSlotCount + 1) performAndroidSaveStateAction(false);
        else if (row == kSaveStateSlotCount + 2) deleteAndroidSaveState();
        else if (row == kSaveStateSlotCount + 3) navigateBackAndroidMenu();
    }
    else if (g_androidMenuScreen == ANDROID_MENU_ABOUT)
    {
        navigateBackAndroidMenu();
    }
    else if (g_androidMenuScreen == ANDROID_MENU_MAIN)
    {
        handleAndroidMainMenuSelection(row);
    }
    else if (g_androidMenuScreen == ANDROID_MENU_OPTIONS)
    {
        handleAndroidOptionsSelection(row);
    }
    else if (androidMenuScreenHasSettingsList())
    {
        handleAndroidDetailMenuSelection(g_androidMenuScreen, row);
    }
}

static bool handleAndroidMenuNavigationEvent(const SDL_Event& ev)
{
    if (g_androidMenuScreen == ANDROID_MENU_NONE ||
        g_androidMenuScreen == ANDROID_MENU_LIBRARY)
    {
        return false;
    }
    if (g_controllerMappingPending ||
        g_controllerCalibrationStage != CONTROLLER_CALIBRATION_IDLE)
    {
        return false;
    }
    if (ev.type == SDL_CONTROLLERBUTTONDOWN &&
        ev.cbutton.which != activeGameControllerInstanceId())
    {
        return false;
    }

    bool pressed = ev.type == SDL_KEYDOWN || ev.type == SDL_CONTROLLERBUTTONDOWN;
    if (!pressed || (ev.type == SDL_KEYDOWN && ev.key.repeat))
    {
        return false;
    }

    if (ev.type == SDL_KEYDOWN)
    {
        SDL_Scancode scancode = ev.key.keysym.scancode;
        if (scancode == SDL_SCANCODE_UP) moveAndroidMenuSelection(-1);
        else if (scancode == SDL_SCANCODE_DOWN) moveAndroidMenuSelection(1);
        else if (scancode == SDL_SCANCODE_LEFT) moveAndroidMenuSelection(-1);
        else if (scancode == SDL_SCANCODE_RIGHT) moveAndroidMenuSelection(1);
        else if (scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_SPACE ||
            scancode == SDL_SCANCODE_A) activateAndroidMenuSelection();
        else if (scancode == SDL_SCANCODE_B || scancode == SDL_SCANCODE_ESCAPE ||
            scancode == SDL_SCANCODE_AC_BACK) navigateBackAndroidMenu();
        else return false;
        return true;
    }

    switch ((SDL_GameControllerButton)ev.cbutton.button)
    {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: moveAndroidMenuSelection(-1); break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: moveAndroidMenuSelection(1); break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: moveAndroidMenuSelection(-1); break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: moveAndroidMenuSelection(1); break;
    case SDL_CONTROLLER_BUTTON_A: activateAndroidMenuSelection(); break;
    case SDL_CONTROLLER_BUTTON_B: navigateBackAndroidMenu(); break;
    default: return false;
    }
    return true;
}

bool drawFrame(uint16_t* pixels, int displayedFps);

bool frontendRunCheatManagerFileSwitchAutomation(void)
{
    std::string savedCurrentGamePath = g_frontendCurrentGamePath;
    std::string savedManagerGamePath = g_androidCheatManagerGamePath;
    g_frontendCurrentGamePath.clear();
    g_androidCheatManagerGamePath.clear();

    prepareAndroidCheatManagerGamePath();
    std::string firstGamePath = g_androidCheatManagerGamePath;
    CheatRuntimeStatus firstStatus = cheatRuntimeGetStatus();
    bool switched = selectNextAndroidCheatManagerGamePath();
    std::string secondGamePath = g_androidCheatManagerGamePath;
    CheatRuntimeStatus secondStatus = cheatRuntimeGetStatus();
    bool ok = !firstGamePath.empty() && firstStatus.available && switched &&
        !secondGamePath.empty() && secondStatus.available &&
        firstGamePath != secondGamePath &&
        firstStatus.sourcePath != secondStatus.sourcePath;

    g_frontendCurrentGamePath = savedCurrentGamePath;
    g_androidCheatManagerGamePath = savedManagerGamePath;
    cheatRuntimeLoadForConfiguration(NULL, std::vector<std::string>());
    return ok;
}

bool frontendRunCheatManagerAutomation(void)
{
    CheatRuntimeStatus initialStatus = cheatRuntimeGetStatus();
    if (!g_frontendSettings || g_frontendCurrentGamePath.empty() ||
        !initialStatus.available || initialStatus.entries.empty())
    {
        printf("CHEAT_MANAGER_AUTOMATION result=fail stage=initial loaded=%u available=%u entries=%u\n",
            initialStatus.loaded ? 1u : 0u,
            initialStatus.available ? 1u : 0u,
            (unsigned int)initialStatus.entries.size());
        return false;
    }

    EmulatorSettings originalSettings = *g_frontendSettings;
    bool ok = setAndroidCheatManagerFeatureEnabled(0, true) &&
        setAndroidCheatManagerGlobalEnabled(true);
    EmulatorSettings enabledSettings = emulatorLoadSettings();
    std::vector<std::string> enabledKeys = emulatorCheatFeatureKeysForGame(
        enabledSettings, g_frontendCurrentGamePath);
    ok = ok && enabledSettings.cheatsEnabled && enabledKeys.size() == 1 &&
        enabledKeys[0] == initialStatus.entries[0].name && cheatRuntimeEnabled();

    ok = setAllAndroidCheatManagerFeaturesEnabled(false) &&
        setAndroidCheatManagerGlobalEnabled(false) && ok;
    EmulatorSettings disabledSettings = emulatorLoadSettings();
    std::vector<std::string> disabledKeys = emulatorCheatFeatureKeysForGame(
        disabledSettings, g_frontendCurrentGamePath);
    ok = ok && !disabledSettings.cheatsEnabled && disabledKeys.empty() &&
        !cheatRuntimeEnabled();

    *g_frontendSettings = originalSettings;
    bool restored = emulatorSaveSettings(*g_frontendSettings);
    cheatRuntimeLoadForGame(initialStatus.currentGameSha256.c_str(),
        g_frontendCurrentGamePath.c_str(),
        emulatorCheatFeatureKeysForGame(originalSettings, g_frontendCurrentGamePath));
    cheatRuntimeSetEnabled(originalSettings.cheatsEnabled);
    ok = ok && restored;

    printf("CHEAT_MANAGER_AUTOMATION result=%s file=%s entries=%u feature=%s restored=%u\n",
        ok ? "pass" : "fail",
        initialStatus.sourcePath.empty() ? "(none)" : initialStatus.sourcePath.c_str(),
        (unsigned int)initialStatus.entries.size(),
        initialStatus.entries[0].name.c_str(),
        restored ? 1u : 0u);
    return ok;
}

static uint64_t counterToUs(uint64_t counter, uint64_t frequency)
{
    return frequency ? counter * 1000000ull / frequency : 0;
}

static uint64_t idlePresentIntervalUs(uint64_t activePresentIntervalMs)
{
    uint64_t activeUs = activePresentIntervalMs * 1000ull;
    return activeUs > kIdlePresentIntervalUs ? activeUs : kIdlePresentIntervalUs;
}

static uint32_t idleLoopDelayMs(uint64_t nowCounter, uint64_t lastPresentCounter, uint64_t presentIntervalUs, uint64_t counterFrequency)
{
    if (!lastPresentCounter || !counterFrequency || presentIntervalUs <= kIdleWakeMarginUs)
    {
        return 1;
    }

    uint64_t elapsedUs = counterToUs(nowCounter - lastPresentCounter, counterFrequency);
    if (elapsedUs + kIdleWakeMarginUs >= presentIntervalUs)
    {
        return 1;
    }

    uint64_t delayMs = (presentIntervalUs - elapsedUs - kIdleWakeMarginUs) / 1000;
    if (delayMs > kIdleMaxWaitMs)
    {
        delayMs = kIdleMaxWaitMs;
    }
    return delayMs < 1 ? 1 : (uint32_t)delayMs;
}

static uint8_t blendChannel(uint8_t start, uint8_t end, int index, int count)
{
    if (count <= 1)
    {
        return end;
    }

    int value = (int)start + ((int)end - (int)start) * index / (count - 1);
    if (value < 0)
    {
        value = 0;
    }
    else if (value > 255)
    {
        value = 255;
    }
    return (uint8_t)value;
}

static void drawVerticalGradientRect(
    int x, int y, int w, int h,
    SDL_Color top, SDL_Color bottom)
{
    if (w <= 0 || h <= 0)
    {
        return;
    }

    for (int row = 0; row < h; ++row)
    {
        SDL_SetRenderDrawColor(g_renderer,
            blendChannel(top.r, bottom.r, row, h),
            blendChannel(top.g, bottom.g, row, h),
            blendChannel(top.b, bottom.b, row, h),
            blendChannel(top.a, bottom.a, row, h));
        SDL_RenderDrawLine(g_renderer, x, y + row, x + w - 1, y + row);
    }
}

static uint32_t idleSymbolHash(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static double idleSymbolUnit(uint32_t seed)
{
    return (double)(idleSymbolHash(seed) & 0xffffu) / 65535.0;
}

static uint32_t g_idleRunSeed = 0;
static uint32_t g_idleSeedGeneration = 0;

static void reseedIdleVisuals(void)
{
    uint64_t counter = SDL_GetPerformanceCounter();
    g_idleRunSeed = idleSymbolHash(
        (uint32_t)counter ^
        (uint32_t)(counter >> 32) ^
        (uint32_t)time(NULL) ^
        (uint32_t)(uintptr_t)&g_idleRunSeed ^
        ++g_idleSeedGeneration);
    if (!g_idleRunSeed)
    {
        g_idleRunSeed = 1;
    }
}

static uint32_t idleSymbolRunSeed(void)
{
    if (!g_idleRunSeed)
    {
        reseedIdleVisuals();
    }
    return g_idleRunSeed;
}

static double clampDouble(double value, double minValue, double maxValue)
{
    if (value < minValue)
    {
        return minValue;
    }
    if (value > maxValue)
    {
        return maxValue;
    }
    return value;
}

static void putPixelArgbClipped(uint32_t* pixels, int width, int height, int x, int y, uint8_t alpha)
{
    if (!pixels || x < 0 || y < 0 || x >= width || y >= height || alpha == 0)
    {
        return;
    }

    uint32_t* pixel = pixels + (size_t)y * (size_t)width + (size_t)x;
    uint8_t oldAlpha = (uint8_t)((*pixel >> 24) & 0xff);
    if (alpha > oldAlpha)
    {
        *pixel = ((uint32_t)alpha << 24) | 0x00ffffffu;
    }
}

static double distanceToSegment(double px, double py, double x1, double y1, double x2, double y2)
{
    double vx = x2 - x1;
    double vy = y2 - y1;
    double wx = px - x1;
    double wy = py - y1;
    double lengthSq = vx * vx + vy * vy;
    if (lengthSq <= 0.000001)
    {
        double dx = px - x1;
        double dy = py - y1;
        return sqrt(dx * dx + dy * dy);
    }

    double t = (wx * vx + wy * vy) / lengthSq;
    t = clampDouble(t, 0.0, 1.0);
    double cx = x1 + t * vx;
    double cy = y1 + t * vy;
    double dx = px - cx;
    double dy = py - cy;
    return sqrt(dx * dx + dy * dy);
}

static void drawTextureLine(uint32_t* pixels, int width, int height, double x1, double y1, double x2, double y2, double radius)
{
    int minX = (int)(clampDouble(floor((x1 < x2 ? x1 : x2) - radius - 2.0), 0.0, (double)(width - 1)));
    int maxX = (int)(clampDouble(ceil((x1 > x2 ? x1 : x2) + radius + 2.0), 0.0, (double)(width - 1)));
    int minY = (int)(clampDouble(floor((y1 < y2 ? y1 : y2) - radius - 2.0), 0.0, (double)(height - 1)));
    int maxY = (int)(clampDouble(ceil((y1 > y2 ? y1 : y2) + radius + 2.0), 0.0, (double)(height - 1)));

    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            double dist = distanceToSegment((double)x + 0.5, (double)y + 0.5, x1, y1, x2, y2);
            double coverage = radius + 0.75 - dist;
            if (coverage <= 0.0)
            {
                continue;
            }
            if (coverage > 1.0)
            {
                coverage = 1.0;
            }
            putPixelArgbClipped(pixels, width, height, x, y, (uint8_t)(coverage * 255.0));
        }
    }
}

static void drawTextureCircle(uint32_t* pixels, int width, int height, double centerX, double centerY, double radius, double strokeRadius)
{
    int minX = (int)(clampDouble(floor(centerX - radius - strokeRadius - 2.0), 0.0, (double)(width - 1)));
    int maxX = (int)(clampDouble(ceil(centerX + radius + strokeRadius + 2.0), 0.0, (double)(width - 1)));
    int minY = (int)(clampDouble(floor(centerY - radius - strokeRadius - 2.0), 0.0, (double)(height - 1)));
    int maxY = (int)(clampDouble(ceil(centerY + radius + strokeRadius + 2.0), 0.0, (double)(height - 1)));

    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            double dx = (double)x + 0.5 - centerX;
            double dy = (double)y + 0.5 - centerY;
            double dist = fabs(sqrt(dx * dx + dy * dy) - radius);
            double coverage = strokeRadius + 0.75 - dist;
            if (coverage <= 0.0)
            {
                continue;
            }
            if (coverage > 1.0)
            {
                coverage = 1.0;
            }
            putPixelArgbClipped(pixels, width, height, x, y, (uint8_t)(coverage * 255.0));
        }
    }
}

static void drawTextureSymbolSegment(
    uint32_t* pixels, int width, int height, double halfSize,
    double x1, double y1, double x2, double y2, double strokeRadius)
{
    double centerX = (double)width * 0.5;
    double centerY = (double)height * 0.5;
    drawTextureLine(pixels, width, height,
        centerX + x1 * halfSize, centerY + y1 * halfSize,
        centerX + x2 * halfSize, centerY + y2 * halfSize,
        strokeRadius);
}

static SDL_Texture* createIdleSymbolTexture(int type)
{
    const int textureSize = 128;
    const double halfSize = textureSize * 0.42;
    const double strokeRadius = textureSize * 0.035;
    uint32_t pixels[textureSize * textureSize];
    memset(pixels, 0, sizeof(pixels));

    switch (type & 3)
    {
    case 0:
        drawTextureSymbolSegment(pixels, textureSize, textureSize, halfSize, -0.65, -0.65, 0.65, 0.65, strokeRadius);
        drawTextureSymbolSegment(pixels, textureSize, textureSize, halfSize, -0.65, 0.65, 0.65, -0.65, strokeRadius);
        break;
    case 1:
        drawTextureCircle(pixels, textureSize, textureSize,
            textureSize * 0.5, textureSize * 0.5, halfSize * 0.68, strokeRadius);
        break;
    case 2:
        drawTextureSymbolSegment(pixels, textureSize, textureSize, halfSize, -0.62, -0.62, 0.62, -0.62, strokeRadius);
        drawTextureSymbolSegment(pixels, textureSize, textureSize, halfSize, 0.62, -0.62, 0.62, 0.62, strokeRadius);
        drawTextureSymbolSegment(pixels, textureSize, textureSize, halfSize, 0.62, 0.62, -0.62, 0.62, strokeRadius);
        drawTextureSymbolSegment(pixels, textureSize, textureSize, halfSize, -0.62, 0.62, -0.62, -0.62, strokeRadius);
        break;
    default:
        drawTextureSymbolSegment(pixels, textureSize, textureSize, halfSize, 0.0, -0.72, 0.72, 0.58, strokeRadius);
        drawTextureSymbolSegment(pixels, textureSize, textureSize, halfSize, 0.72, 0.58, -0.72, 0.58, strokeRadius);
        drawTextureSymbolSegment(pixels, textureSize, textureSize, halfSize, -0.72, 0.58, 0.0, -0.72, strokeRadius);
        break;
    }

    SDL_Texture* texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STATIC, textureSize, textureSize);
    if (!texture)
    {
        return NULL;
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
    if (SDL_UpdateTexture(texture, NULL, pixels, textureSize * (int)sizeof(uint32_t)) != 0)
    {
        SDL_DestroyTexture(texture);
        return NULL;
    }
    return texture;
}

static void resetIdleSymbolTextures(void)
{
    for (int i = 0; i < 4; ++i)
    {
        if (g_idleSymbolTextures[i])
        {
            SDL_DestroyTexture(g_idleSymbolTextures[i]);
            g_idleSymbolTextures[i] = NULL;
        }
    }
}

static SDL_Texture* idleSymbolTexture(int type)
{
    int index = type & 3;
    if (!g_idleSymbolTextures[index])
    {
        g_idleSymbolTextures[index] = createIdleSymbolTexture(index);
    }
    return g_idleSymbolTextures[index];
}

static void renderIdleSymbolTexture(SDL_Texture* texture, double centerX, double centerY, double size, double angle)
{
    SDL_FRect dst =
    {
        (float)(centerX - size * 0.5),
        (float)(centerY - size * 0.5),
        (float)size,
        (float)size
    };
    SDL_RenderCopyExF(g_renderer, texture, NULL, &dst, angle * 180.0 / kPi, NULL, SDL_FLIP_NONE);
}

static void drawIdleFloatingSymbol(int type, double centerX, double centerY, double size, double angle, SDL_Color color)
{
    SDL_Texture* texture = idleSymbolTexture(type);
    if (!texture)
    {
        return;
    }

    SDL_SetTextureColorMod(texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(texture, color.a);
    renderIdleSymbolTexture(texture, centerX, centerY, size, angle);
}

struct IdleSymbolSpec
{
    int type;
    double anchorX;
    double anchorY;
    double size;
    double driftX;
    double driftY;
    double speedX;
    double speedY;
    double phaseX;
    double phaseY;
    double rotationPhase;
    double rotationSpeed;
    double rotationAmount;
    uint8_t alpha;
};

struct IdleSymbolSample
{
    double x;
    double y;
    double angle;
};

struct IdleBackgroundGradient
{
    SDL_Color top;
    SDL_Color bottom;
};

struct IdleAnimationClock
{
    uint64_t timeMs = 0;
    uint64_t lastHostTicks = 0;

    void pause(void)
    {
        lastHostTicks = 0;
    }

    void reset(void)
    {
        timeMs = 0;
        pause();
    }

    uint64_t advance(uint64_t hostTicks)
    {
        if (!lastHostTicks)
        {
            lastHostTicks = hostTicks;
            return timeMs;
        }

        if (hostTicks > lastHostTicks)
        {
            timeMs += hostTicks - lastHostTicks;
        }
        lastHostTicks = hostTicks;
        return timeMs;
    }
};

static IdleAnimationClock g_idleAnimationClock;

static int idleSymbolCount(int width, int height, bool smallLayer)
{
    int count = smallLayer ? (width * height) / (80 * 80) : (width * height) / (120 * 120);
    int minCount = smallLayer ? 12 : 8;
    int maxCount = smallLayer ? 48 : 28;
    if (count < minCount)
    {
        return minCount;
    }
    if (count > maxCount)
    {
        return maxCount;
    }
    return count;
}

static IdleSymbolSpec buildIdleSymbolSpec(int width, int height, int index, bool smallLayer)
{
    uint32_t seed = idleSymbolRunSeed() ^ (smallLayer ? (uint32_t)index + 1009u : (uint32_t)index + 17u);
    double sizeBase = (double)(height < width ? height : width);
    double size = smallLayer ?
        sizeBase * (0.050 + idleSymbolUnit(seed * 19u + 301u) * 0.035) :
        sizeBase * (0.125 + idleSymbolUnit(seed * 19u + 3u) * 0.070);
    double minSize = smallLayer ? 14.0 : 28.0;
    if (size < minSize)
    {
        size = minSize;
    }

    IdleSymbolSpec spec = {};
    spec.type = index < 4 ? index : (int)(idleSymbolHash(seed * 71u + 37u) & 3u);
    spec.size = size;
    spec.anchorX = idleSymbolUnit(seed * 31u + 5u) * (double)width;
    spec.anchorY = idleSymbolUnit(seed * 41u + 7u) * (double)height;
    spec.driftX = smallLayer ?
        (10.0 + idleSymbolUnit(seed * 43u + 13u) * 16.0) :
        (20.0 + idleSymbolUnit(seed * 43u + 13u) * 28.0);
    spec.driftY = smallLayer ?
        (8.0 + idleSymbolUnit(seed * 47u + 17u) * 14.0) :
        (18.0 + idleSymbolUnit(seed * 47u + 17u) * 26.0);
    spec.speedX = smallLayer ?
        (0.30 + idleSymbolUnit(seed * 53u + 19u) * 0.22) :
        (0.20 + idleSymbolUnit(seed * 53u + 19u) * 0.20);
    spec.speedY = smallLayer ?
        (0.24 + idleSymbolUnit(seed * 59u + 23u) * 0.20) :
        (0.16 + idleSymbolUnit(seed * 59u + 23u) * 0.18);
    spec.phaseX = idleSymbolUnit(seed * 61u + 29u) * 2.0 * kPi;
    spec.phaseY = idleSymbolUnit(seed * 67u + 31u) * 2.0 * kPi;
    spec.rotationPhase = idleSymbolUnit(seed * 83u + 47u) * 2.0 * kPi;
    spec.rotationSpeed = smallLayer ?
        (0.22 + idleSymbolUnit(seed * 89u + 53u) * 0.18) :
        (0.14 + idleSymbolUnit(seed * 89u + 53u) * 0.14);
    spec.rotationAmount = smallLayer ? 0.34 : 0.42;
    spec.alpha = (uint8_t)(smallLayer ?
        (9 + (int)(idleSymbolUnit(seed * 13u + 11u) * 9.0)) :
        (24 + (int)(idleSymbolUnit(seed * 13u + 11u) * 17.0)));

    if (!smallLayer && index < 4)
    {
        static const double kAnchorX[4] = { 0.20, 0.80, 0.24, 0.76 };
        static const double kAnchorY[4] = { 0.25, 0.30, 0.75, 0.72 };
        spec.anchorX = kAnchorX[index] * (double)width +
            (idleSymbolUnit(seed * 73u + 41u) - 0.5) * (double)width * 0.06;
        spec.anchorY = kAnchorY[index] * (double)height +
            (idleSymbolUnit(seed * 79u + 43u) - 0.5) * (double)height * 0.06;
        spec.driftX = clampDouble(spec.driftX, 6.0, (double)width * 0.055);
        spec.driftY = clampDouble(spec.driftY, 6.0, (double)height * 0.055);
    }

    double visibleMargin = spec.size * 0.55;
    spec.anchorX = clampDouble(spec.anchorX,
        smallLayer ? -spec.size : visibleMargin,
        smallLayer ? (double)width + spec.size : (double)width - visibleMargin);
    spec.anchorY = clampDouble(spec.anchorY,
        smallLayer ? -spec.size : visibleMargin,
        smallLayer ? (double)height + spec.size : (double)height - visibleMargin);
    return spec;
}

static IdleSymbolSample sampleIdleSymbolMotion(const IdleSymbolSpec& spec, int width, int height, double t, int index, bool smallLayer)
{
    IdleSymbolSample sample =
    {
        spec.anchorX +
            sin(t * spec.speedX + spec.phaseX) * spec.driftX +
            sin(t * (spec.speedY * 0.37) + spec.phaseY) * spec.driftX * 0.18,
        spec.anchorY +
            cos(t * spec.speedY + spec.phaseY) * spec.driftY +
            sin(t * (spec.speedX * 0.41) + spec.phaseX) * spec.driftY * 0.16,
        sin(t * spec.rotationSpeed + spec.rotationPhase) * spec.rotationAmount
    };

    if (!smallLayer && index < 4)
    {
        double visibleMargin = spec.size * 0.55;
        sample.x = clampDouble(sample.x, visibleMargin, (double)width - visibleMargin);
        sample.y = clampDouble(sample.y, visibleMargin, (double)height - visibleMargin);
    }
    return sample;
}

static void drawIdleFloatingSymbolLayer(int width, int height, double t, int count, bool smallLayer)
{
    for (int i = 0; i < count; ++i)
    {
        IdleSymbolSpec spec = buildIdleSymbolSpec(width, height, i, smallLayer);
        IdleSymbolSample sample = sampleIdleSymbolMotion(spec, width, height, t, i, smallLayer);
        SDL_Color color = { 255, 255, 255, spec.alpha };
        drawIdleFloatingSymbol(spec.type, sample.x, sample.y, spec.size, sample.angle, color);
    }
}

static void drawIdleFloatingSymbols(int width, int height, double t)
{
    int mainCount = idleSymbolCount(width, height, false);
    int smallCount = idleSymbolCount(width, height, true);

    drawIdleFloatingSymbolLayer(width, height, t, smallCount, true);
    drawIdleFloatingSymbolLayer(width, height, t, mainCount, false);
}

static IdleBackgroundGradient idleBackgroundGradient(void)
{
    static const IdleBackgroundGradient kGradients[] =
    {
        { { 152, 87, 87, 255 }, { 76, 39, 53, 255 } },
        { { 152, 101, 71, 255 }, { 76, 51, 53, 255 } },
        { { 145, 124, 67, 255 }, { 71, 62, 48, 255 } },
        { { 115, 133, 71, 255 }, { 55, 71, 51, 255 } },
        { { 62, 133, 87, 255 }, { 28, 74, 62, 255 } },
        { { 48, 143, 122, 255 }, { 21, 76, 81, 255 } },
        { { 48, 133, 152, 255 }, { 21, 69, 94, 255 } },
        { { 64, 127, 184, 255 }, { 25, 67, 106, 255 } },
        { { 87, 106, 182, 255 }, { 35, 48, 106, 255 } },
        { { 115, 94, 168, 255 }, { 48, 44, 101, 255 } },
        { { 131, 90, 150, 255 }, { 55, 39, 90, 255 } },
        { { 147, 90, 122, 255 }, { 67, 41, 76, 255 } }
    };
    static uint32_t selectedSeed = 0;
    static uint32_t index = 0;
    uint32_t runSeed = idleSymbolRunSeed();
    uint32_t count = (uint32_t)(sizeof(kGradients) / sizeof(kGradients[0]));
    if (selectedSeed != runSeed)
    {
        uint32_t nextIndex = idleSymbolHash(runSeed ^ 0x9e3779b9u) % count;
        if (selectedSeed && count > 1 && nextIndex == index)
        {
            uint32_t offset = 1u + idleSymbolHash(runSeed ^ 0x85ebca6bu) % (count - 1u);
            nextIndex = (index + offset) % count;
        }
        selectedSeed = runSeed;
        index = nextIndex;
    }
    return kGradients[index];
}

static SDL_Color androidThemeBlend(SDL_Color first, SDL_Color second, int secondWeight,
    uint8_t alpha)
{
    int weight = std::max(0, std::min(100, secondWeight));
    SDL_Color result = {
        (uint8_t)((first.r * (100 - weight) + second.r * weight) / 100),
        (uint8_t)((first.g * (100 - weight) + second.g * weight) / 100),
        (uint8_t)((first.b * (100 - weight) + second.b * weight) / 100),
        alpha
    };
    return result;
}

static SDL_Color androidThemeBrighten(SDL_Color color, int percent)
{
    color.r = (uint8_t)std::min(255, (color.r * percent + 50) / 100);
    color.g = (uint8_t)std::min(255, (color.g * percent + 50) / 100);
    color.b = (uint8_t)std::min(255, (color.b * percent + 50) / 100);
    return color;
}

static AndroidThemeColors androidThemeColors(void)
{
    IdleBackgroundGradient background = idleBackgroundGradient();
    SDL_Color base = androidThemeBlend(background.top, background.bottom, 58, 255);
    SDL_Color deep = SDL_Color{ 8, 12, 24, 255 };
    SDL_Color light = SDL_Color{ 238, 246, 255, 255 };
    AndroidThemeColors colors = {};
    colors.card = androidThemeBrighten(androidThemeBlend(base, deep, 54, 230), 108);
    colors.cardBorder = androidThemeBrighten(androidThemeBlend(base, light, 30, 190), 108);
    colors.button = androidThemeBrighten(androidThemeBlend(base, deep, 34, 227), 108);
    colors.buttonBorder = androidThemeBrighten(androidThemeBlend(base, light, 44, 220), 108);
    colors.icon = androidThemeBrighten(androidThemeBlend(base, deep, 20, 238), 108);
    colors.iconBorder = androidThemeBrighten(androidThemeBlend(base, light, 38, 210), 108);
    colors.text = androidThemeBlend(base, light, 78, 248);
    colors.buttonText = androidThemeBlend(base, light, 88, 252);
    colors.mutedText = androidThemeBlend(base, light, 60, 220);
    return colors;
}

static void resetIdleTitleTexture(void)
{
    if (g_idleTitleTexture)
    {
        SDL_DestroyTexture(g_idleTitleTexture);
        g_idleTitleTexture = NULL;
    }
    g_idleTitleTextureWidth = 0;
    g_idleTitleTextureHeight = 0;
}

static void resetIdleTextures(void)
{
    resetIdleTitleTexture();
    resetIdleSymbolTextures();
}

static bool drawIdleTitle(int width, int height)
{
    const char* title = "DingooPie";
    int titleSize = std::min(width, height) / 6;
    titleSize = std::max(48, std::min(160, titleSize));
    int titleAreaHeight = std::max(titleSize * 2, height / 4);
    SDL_Rect titleRect = { 0, (height - titleAreaHeight) / 2, width, titleAreaHeight };
    drawAndroidSystemTextCentered(title, titleRect, titleSize,
        SDL_Color{ 244, 250, 255, 245 });
    return true;
}
static bool presentBlackTransitionFrame(void)
{
    if (!g_renderer)
    {
        return false;
    }

    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    if (SDL_RenderClear(g_renderer) != 0)
    {
        printf("frontend: transition SDL_RenderClear failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_RenderPresent(g_renderer);
    return true;
}

static bool drawIdleScreen(uint64_t animationTimeMs)
{
    if (!g_renderer)
    {
        return false;
    }

    int width = 0;
    int height = 0;
    SDL_GetRendererOutputSize(g_renderer, &width, &height);
    if (width <= 0 || height <= 0)
    {
        return false;
    }

    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
    IdleBackgroundGradient bg = idleBackgroundGradient();
    SDL_SetRenderDrawColor(g_renderer, bg.top.r, bg.top.g, bg.top.b, bg.top.a);
    if (SDL_RenderClear(g_renderer) != 0)
    {
        printf("frontend: idle SDL_RenderClear failed: %s\n", SDL_GetError());
        return false;
    }
    drawVerticalGradientRect(0, 0, width, height, bg.top, bg.bottom);

    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    double t = (double)animationTimeMs / 1000.0;
    drawIdleFloatingSymbols(width, height, t);
    if (g_androidMenuScreen == ANDROID_MENU_LIBRARY ||
        (!frontendGameRunning() && androidMenuScreenUsesOverlay(g_androidMenuScreen)))
    {
        return drawAndroidLibraryScreen();
    }
    if (!drawIdleTitle(width, height))
    {
        SDL_Color fallback = { 244, 250, 255, 245 };
        drawRendererText("DingooPie", width / 2 - rendererTextWidth("DingooPie", 7) / 2,
            height / 2 - 24, 7, fallback);
    }

    SDL_RenderPresent(g_renderer);
    return true;
}

static uint32_t virtualControlMask(uint32_t controlBit)
{
    return 1u << controlBit;
}

static uint32_t virtualGameplayButtonControlMask(void)
{
    return virtualControlMask(CONTROL_BUTTON_A) |
        virtualControlMask(CONTROL_BUTTON_B) |
        virtualControlMask(CONTROL_BUTTON_X) |
        virtualControlMask(CONTROL_BUTTON_Y) |
        virtualControlMask(CONTROL_BUTTON_START) |
        virtualControlMask(CONTROL_BUTTON_SELECT) |
        virtualControlMask(CONTROL_TRIGGER_LEFT) |
        virtualControlMask(CONTROL_TRIGGER_RIGHT);
}

static bool virtualControlMaskOnlyHasGameplayButtons(uint32_t controlMask)
{
    return controlMask &&
        (controlMask & ~virtualGameplayButtonControlMask()) == 0;
}

static bool virtualButtonHasControl(const VirtualControlButton& button, uint32_t controlBit)
{
    return (button.controlMask & virtualControlMask(controlBit)) != 0;
}

static bool virtualButtonPressed(const VirtualControlButton& button)
{
    return button.controlMask && (g_virtualPointerControls & button.controlMask) == button.controlMask;
}

static int buildVirtualControls(VirtualControlButton* outButtons, int maxButtons)
{
    if (!outButtons || maxButtons <= 0 || !g_renderer)
    {
        return 0;
    }

    int width = 0;
    int height = 0;
    if (!getVirtualControlCoordinateSize(&width, &height))
    {
        return 0;
    }

    const int shortSide = std::min(width, height);
    const int minimumUnit = std::max(28, shortSide / 30);
    const int maximumUnit = std::max(minimumUnit, shortSide / 7);
    int unit = std::min(width / 13, height / 10);
    unit = std::max(minimumUnit, std::min(unit, maximumUnit));

    SDL_Rect gameRect = { 0, 0, width, height };
    bool useSideBands = false;
    if (getLandscapeGameDestination(&gameRect))
    {
        int leftBand = gameRect.x;
        int rightBand = width - gameRect.x - gameRect.w;
        int bandPadding = std::max(6, shortSide / 90);
        int sideBandUnit = (std::min(leftBand, rightBand) - bandPadding * 2) / 3;
        if (sideBandUnit >= minimumUnit)
        {
            unit = std::min(sideBandUnit, maximumUnit);
            useSideBands = true;
        }
    }

    unit = std::max(1, (unit * virtualControlScalePercent() + 50) / 100);

    const int gap = std::max(6, unit / 4);
    int margin = std::max(unit / 3, shortSide / 80);
    int bottomMargin = std::max(unit * 2 / 3, shortSide / 45);
    bool fpsVisible = g_frontendSettings && g_frontendSettings->showFps;
    int shoulderTopMargin = margin;
    if (fpsVisible)
    {
        shoulderTopMargin += std::max(unit / 2, shortSide / 32);
    }
    int dpadY = height - bottomMargin - unit * 3;
    int faceY = dpadY;
    const int startSelectW = unit * 5 / 4;
    const int startSelectH = unit * 5 / 8;
    const int shoulderW = unit * 2;
    const int shoulderH = unit * 3 / 4;
    const char* selectLabel = "SELECT";
    const char* startLabel = "START";

    int dpadX = margin;
    int faceX = width - margin - unit * 3;
    int selectX = width / 2 - startSelectW - gap;
    int startX = width / 2 + gap;
    int startSelectY = height - margin - startSelectH;
    int leftShoulderX = margin;
    int rightShoulderX = width - margin - shoulderW;

    if (useSideBands)
    {
        int leftBand = gameRect.x;
        int rightBand = width - gameRect.x - gameRect.w;
        dpadX = std::max(0, (leftBand - unit * 3) / 2);
        faceX = std::min(
            width - unit * 3,
            gameRect.x + gameRect.w + (rightBand - unit * 3) / 2);
        dpadY = height / 2 - unit * 3 / 2;
        faceY = dpadY;
        leftShoulderX = dpadX + (unit * 3 - shoulderW) / 2;
        rightShoulderX = faceX + (unit * 3 - shoulderW) / 2;
        selectX = (leftBand - startSelectW) / 2;
        startX = gameRect.x + gameRect.w + (rightBand - startSelectW) / 2;
        startSelectY = height - margin - startSelectH;
    }

    SDL_Rect selectRect = { selectX, startSelectY, startSelectW, startSelectH };
    SDL_Rect startRect = { startX, startSelectY, startSelectW, startSelectH };
    SDL_Rect menuRect = { 0, 0, 0, 0 };
    SDL_Rect leftShoulderRect = {
        leftShoulderX, shoulderTopMargin, shoulderW, shoulderH };
    SDL_Rect rightShoulderRect = {
        rightShoulderX, shoulderTopMargin, shoulderW, shoulderH };
    SDL_Rect dpadRects[8] =
    {
        { dpadX, dpadY, unit, unit },
        { dpadX + unit * 2, dpadY, unit, unit },
        { dpadX + unit * 2, dpadY + unit * 2, unit, unit },
        { dpadX, dpadY + unit * 2, unit, unit },
        { dpadX + unit, dpadY, unit, unit },
        { dpadX, dpadY + unit, unit, unit },
        { dpadX + unit * 2, dpadY + unit, unit, unit },
        { dpadX + unit, dpadY + unit * 2, unit, unit }
    };
    SDL_Rect faceRects[4] =
    {
        { faceX + unit, faceY, unit, unit },
        { faceX, faceY + unit, unit, unit },
        { faceX + unit * 2, faceY + unit, unit, unit },
        { faceX + unit, faceY + unit * 2, unit, unit }
    };
    SDL_Rect faceChordRects[4] =
    {
        { faceX, faceY, unit, unit },
        { faceX + unit * 2, faceY, unit, unit },
        { faceX + unit * 2, faceY + unit * 2, unit, unit },
        { faceX, faceY + unit * 2, unit, unit }
    };
    int dpadDirections[8][2] =
    {
        { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 },
        { 0, -1 }, { -1, 0 }, { 1, 0 }, { 0, 1 }
    };
    if (portraitModeEnabled())
    {
        // Keep portrait layout and touch directions in screen coordinates, then
        // convert each rectangle back to the shared virtual-control space.
        const int portraitWidth = height;
        const int portraitHeight = width;
        const int rowWidth = startSelectW * 3 + gap * 2;
        const int rowX = (portraitWidth - rowWidth) / 2;
        const int rowY = portraitHeight - margin - startSelectH;
        const int controlY = rowY - gap - unit * 3;
        const int dpadPhysicalX = margin;
        const int facePhysicalX = portraitWidth - margin - unit * 3;
        const SDL_Rect portraitSelect = { rowX, rowY, startSelectW, startSelectH };
        const SDL_Rect portraitMenu = {
            rowX + startSelectW + gap, rowY, startSelectW, startSelectH };
        const SDL_Rect portraitStart = {
            rowX + (startSelectW + gap) * 2, rowY, startSelectW, startSelectH };
        const SDL_Rect portraitLeftShoulder = {
            margin, shoulderTopMargin, shoulderW, shoulderH };
        const SDL_Rect portraitRightShoulder = {
            portraitWidth - margin - shoulderW,
            shoulderTopMargin, shoulderW, shoulderH };
        const auto toVirtualRect = [width](const SDL_Rect& rect)
        {
            return SDL_Rect{ width - rect.y - rect.h, rect.x, rect.h, rect.w };
        };
        selectRect = toVirtualRect(portraitSelect);
        menuRect = toVirtualRect(portraitMenu);
        startRect = toVirtualRect(portraitStart);
        leftShoulderRect = toVirtualRect(portraitLeftShoulder);
        rightShoulderRect = toVirtualRect(portraitRightShoulder);

        const SDL_Rect portraitDpadRects[8] =
        {
            { dpadPhysicalX, controlY, unit, unit },
            { dpadPhysicalX + unit * 2, controlY, unit, unit },
            { dpadPhysicalX + unit * 2, controlY + unit * 2, unit, unit },
            { dpadPhysicalX, controlY + unit * 2, unit, unit },
            { dpadPhysicalX + unit, controlY, unit, unit },
            { dpadPhysicalX, controlY + unit, unit, unit },
            { dpadPhysicalX + unit * 2, controlY + unit, unit, unit },
            { dpadPhysicalX + unit, controlY + unit * 2, unit, unit }
        };
        const SDL_Rect portraitFaceRects[4] =
        {
            { facePhysicalX + unit, controlY, unit, unit },
            { facePhysicalX, controlY + unit, unit, unit },
            { facePhysicalX + unit * 2, controlY + unit, unit, unit },
            { facePhysicalX + unit, controlY + unit * 2, unit, unit }
        };
        const SDL_Rect portraitFaceChordRects[4] =
        {
            { facePhysicalX, controlY, unit, unit },
            { facePhysicalX + unit * 2, controlY, unit, unit },
            { facePhysicalX + unit * 2, controlY + unit * 2, unit, unit },
            { facePhysicalX, controlY + unit * 2, unit, unit }
        };
        const int portraitDpadDirections[8][2] =
        {
            { 1, -1 }, { 1, 1 }, { -1, 1 }, { -1, -1 },
            { 1, 0 }, { 0, -1 }, { 0, 1 }, { -1, 0 }
        };
        for (int i = 0; i < 8; ++i)
        {
            dpadRects[i] = toVirtualRect(portraitDpadRects[i]);
            dpadDirections[i][0] = portraitDpadDirections[i][0];
            dpadDirections[i][1] = portraitDpadDirections[i][1];
        }
        for (int i = 0; i < 4; ++i)
        {
            faceRects[i] = toVirtualRect(portraitFaceRects[i]);
            faceChordRects[i] = toVirtualRect(portraitFaceChordRects[i]);
        }
    }

    int count = 0;
#define ADD_VIRTUAL_BUTTON_EX(text, mask, rx, ry, rw, rh, dx, dy, framed) \
    do { \
        if (count < maxButtons) { \
            outButtons[count].label = text; \
            outButtons[count].controlMask = mask; \
            outButtons[count].rect = SDL_Rect{ rx, ry, rw, rh }; \
            outButtons[count].dpadDx = dx; \
            outButtons[count].dpadDy = dy; \
            outButtons[count].drawFrame = framed; \
            ++count; \
        } \
    } while (0)

    ADD_VIRTUAL_BUTTON_EX("", virtualControlMask(CONTROL_DPAD_UP) | virtualControlMask(CONTROL_DPAD_LEFT),
        dpadRects[0].x, dpadRects[0].y, dpadRects[0].w, dpadRects[0].h,
        dpadDirections[0][0], dpadDirections[0][1], false);
    ADD_VIRTUAL_BUTTON_EX("", virtualControlMask(CONTROL_DPAD_UP) | virtualControlMask(CONTROL_DPAD_RIGHT),
        dpadRects[1].x, dpadRects[1].y, dpadRects[1].w, dpadRects[1].h,
        dpadDirections[1][0], dpadDirections[1][1], false);
    ADD_VIRTUAL_BUTTON_EX("", virtualControlMask(CONTROL_DPAD_DOWN) | virtualControlMask(CONTROL_DPAD_RIGHT),
        dpadRects[2].x, dpadRects[2].y, dpadRects[2].w, dpadRects[2].h,
        dpadDirections[2][0], dpadDirections[2][1], false);
    ADD_VIRTUAL_BUTTON_EX("", virtualControlMask(CONTROL_DPAD_DOWN) | virtualControlMask(CONTROL_DPAD_LEFT),
        dpadRects[3].x, dpadRects[3].y, dpadRects[3].w, dpadRects[3].h,
        dpadDirections[3][0], dpadDirections[3][1], false);

    ADD_VIRTUAL_BUTTON_EX("", virtualControlMask(CONTROL_DPAD_UP),
        dpadRects[4].x, dpadRects[4].y, dpadRects[4].w, dpadRects[4].h,
        dpadDirections[4][0], dpadDirections[4][1], true);
    ADD_VIRTUAL_BUTTON_EX("", virtualControlMask(CONTROL_DPAD_LEFT),
        dpadRects[5].x, dpadRects[5].y, dpadRects[5].w, dpadRects[5].h,
        dpadDirections[5][0], dpadDirections[5][1], true);
    ADD_VIRTUAL_BUTTON_EX("", virtualControlMask(CONTROL_DPAD_RIGHT),
        dpadRects[6].x, dpadRects[6].y, dpadRects[6].w, dpadRects[6].h,
        dpadDirections[6][0], dpadDirections[6][1], true);
    ADD_VIRTUAL_BUTTON_EX("", virtualControlMask(CONTROL_DPAD_DOWN),
        dpadRects[7].x, dpadRects[7].y, dpadRects[7].w, dpadRects[7].h,
        dpadDirections[7][0], dpadDirections[7][1], true);

    ADD_VIRTUAL_BUTTON_EX("",
        virtualControlMask(CONTROL_BUTTON_X) | virtualControlMask(CONTROL_BUTTON_Y),
        faceChordRects[0].x, faceChordRects[0].y,
        faceChordRects[0].w, faceChordRects[0].h, 0, 0, false);
    ADD_VIRTUAL_BUTTON_EX("",
        virtualControlMask(CONTROL_BUTTON_X) | virtualControlMask(CONTROL_BUTTON_A),
        faceChordRects[1].x, faceChordRects[1].y,
        faceChordRects[1].w, faceChordRects[1].h, 0, 0, false);
    ADD_VIRTUAL_BUTTON_EX("",
        virtualControlMask(CONTROL_BUTTON_A) | virtualControlMask(CONTROL_BUTTON_B),
        faceChordRects[2].x, faceChordRects[2].y,
        faceChordRects[2].w, faceChordRects[2].h, 0, 0, false);
    ADD_VIRTUAL_BUTTON_EX("",
        virtualControlMask(CONTROL_BUTTON_B) | virtualControlMask(CONTROL_BUTTON_Y),
        faceChordRects[3].x, faceChordRects[3].y,
        faceChordRects[3].w, faceChordRects[3].h, 0, 0, false);

    ADD_VIRTUAL_BUTTON_EX("X", virtualControlMask(CONTROL_BUTTON_X),
        faceRects[0].x, faceRects[0].y, faceRects[0].w, faceRects[0].h, 0, 0, true);
    ADD_VIRTUAL_BUTTON_EX("Y", virtualControlMask(CONTROL_BUTTON_Y),
        faceRects[1].x, faceRects[1].y, faceRects[1].w, faceRects[1].h, 0, 0, true);
    ADD_VIRTUAL_BUTTON_EX("A", virtualControlMask(CONTROL_BUTTON_A),
        faceRects[2].x, faceRects[2].y, faceRects[2].w, faceRects[2].h, 0, 0, true);
    ADD_VIRTUAL_BUTTON_EX("B", virtualControlMask(CONTROL_BUTTON_B),
        faceRects[3].x, faceRects[3].y, faceRects[3].w, faceRects[3].h, 0, 0, true);

    ADD_VIRTUAL_BUTTON_EX(selectLabel, virtualControlMask(CONTROL_BUTTON_SELECT),
        selectRect.x, selectRect.y, selectRect.w, selectRect.h, 0, 0, true);
    if (portraitModeEnabled())
    {
        ADD_VIRTUAL_BUTTON_EX("MENU", virtualControlMask(CONTROL_POWER),
            menuRect.x, menuRect.y, menuRect.w, menuRect.h, 0, 0, true);
    }
    ADD_VIRTUAL_BUTTON_EX(startLabel, virtualControlMask(CONTROL_BUTTON_START),
        startRect.x, startRect.y, startRect.w, startRect.h, 0, 0, true);

    ADD_VIRTUAL_BUTTON_EX("L", virtualControlMask(CONTROL_TRIGGER_LEFT),
        leftShoulderRect.x, leftShoulderRect.y, leftShoulderRect.w, leftShoulderRect.h, 0, 0, true);
    ADD_VIRTUAL_BUTTON_EX("R", virtualControlMask(CONTROL_TRIGGER_RIGHT),
        rightShoulderRect.x, rightShoulderRect.y, rightShoulderRect.w, rightShoulderRect.h, 0, 0, true);

#undef ADD_VIRTUAL_BUTTON_EX
    return count;
}

static bool getVirtualDpadGeometry(const VirtualControlButton* buttons, int count,
    int* outUnit, int* outCenterX, int* outCenterY)
{
    if (!buttons || count < 8 || !outUnit || !outCenterX || !outCenterY)
    {
        return false;
    }

    int minimumX = buttons[0].rect.x;
    int minimumY = buttons[0].rect.y;
    int maximumX = buttons[0].rect.x + buttons[0].rect.w;
    int maximumY = buttons[0].rect.y + buttons[0].rect.h;
    for (int i = 1; i < 8; ++i)
    {
        minimumX = std::min(minimumX, buttons[i].rect.x);
        minimumY = std::min(minimumY, buttons[i].rect.y);
        maximumX = std::max(maximumX, buttons[i].rect.x + buttons[i].rect.w);
        maximumY = std::max(maximumY, buttons[i].rect.y + buttons[i].rect.h);
    }
    *outUnit = std::min(buttons[4].rect.w, buttons[4].rect.h);
    *outCenterX = (minimumX + maximumX) / 2;
    *outCenterY = (minimumY + maximumY) / 2;
    return true;
}

static void virtualDpadButtonDirection(const VirtualControlButton& button,
    int centerX, int centerY, int* outDirectionX, int* outDirectionY)
{
    int buttonCenterX = button.rect.x + button.rect.w / 2;
    int buttonCenterY = button.rect.y + button.rect.h / 2;
    if (outDirectionX)
    {
        *outDirectionX = buttonCenterX == centerX ? 0 :
            (buttonCenterX < centerX ? -1 : 1);
    }
    if (outDirectionY)
    {
        *outDirectionY = buttonCenterY == centerY ? 0 :
            (buttonCenterY < centerY ? -1 : 1);
    }
}

static void releaseVirtualPointerControls(void)
{
    g_virtualMousePointerHeld = false;
    g_virtualMouseControlMask = 0;
    g_virtualMouseReleaseAtTicks = 0;
    g_androidVirtualTouchContacts.clear();
    g_androidReleasedTouchControls = 0;
    memset(g_androidReleasedTouchControlUntilTicks, 0,
        sizeof(g_androidReleasedTouchControlUntilTicks));
    g_virtualMouseControlsDpad = false;
    g_virtualDpadOffsetX = 0;
    g_virtualDpadOffsetY = 0;
    g_virtualDpadVisualOffsetX = 0.0;
    g_virtualDpadVisualOffsetY = 0.0;
    g_virtualDpadVisualUpdateTicks = 0;
    updateVirtualPointerControls(0);
}

static void updateVirtualDpadVisualPosition(void)
{
    uint64_t now = SDL_GetTicks64();
    uint64_t elapsed = g_virtualDpadVisualUpdateTicks ?
        now - g_virtualDpadVisualUpdateTicks : 16;
    g_virtualDpadVisualUpdateTicks = now;
    elapsed = std::min<uint64_t>(elapsed, 32);
    double follow = 1.0 - exp(-(double)elapsed / 24.0);
    g_virtualDpadVisualOffsetX +=
        (g_virtualDpadOffsetX - g_virtualDpadVisualOffsetX) * follow;
    g_virtualDpadVisualOffsetY +=
        (g_virtualDpadOffsetY - g_virtualDpadVisualOffsetY) * follow;
}

static uint32_t hitTestVirtualControls(int x, int y)
{
    mapRendererPointToVirtualControls(&x, &y);

    VirtualControlButton buttons[kVirtualControlButtonCapacity];
    int count = buildVirtualControls(buttons, kVirtualControlButtonCapacity);
    for (int i = 0; i < count; ++i)
    {
        if (pointInRect(x, y, buttons[i].rect))
        {
            return buttons[i].controlMask;
        }
    }
    return 0;
}

static bool hitTestVirtualDpadDrag(int x, int y, bool requireInside,
    uint32_t currentMask, uint32_t* outMask)
{
    if (!outMask)
    {
        return false;
    }
    *outMask = 0;
    mapRendererPointToVirtualControls(&x, &y);

    VirtualControlButton buttons[kVirtualControlButtonCapacity];
    int count = buildVirtualControls(buttons, kVirtualControlButtonCapacity);
    int unit = 0;
    int centerX = 0;
    int centerY = 0;
    if (!getVirtualDpadGeometry(buttons, count, &unit, &centerX, &centerY))
    {
        return false;
    }
    int deltaX = x - centerX;
    int deltaY = y - centerY;
    int directionX = portraitModeEnabled() ? deltaY : deltaX;
    int directionY = portraitModeEnabled() ? -deltaX : deltaY;
    int radius = unit * 3 / 2;
    int64_t distanceSquared =
        (int64_t)directionX * directionX + (int64_t)directionY * directionY;
    if (requireInside && distanceSquared > (int64_t)radius * radius)
    {
        return false;
    }

    int maxOffset = std::max(1, unit - 3);
    double distance = sqrt((double)distanceSquared);
    if (distance > (double)maxOffset)
    {
        double positionScale = (double)maxOffset / distance;
        directionX = (int)lround(directionX * positionScale);
        directionY = (int)lround(directionY * positionScale);
    }
    g_virtualDpadOffsetX = portraitModeEnabled() ? -directionY : directionX;
    g_virtualDpadOffsetY = portraitModeEnabled() ? directionX : directionY;

    int engageThreshold = std::max(6, unit / 5);
    int releaseThreshold = std::max(4, unit / 8);
    bool leftHeld = (currentMask & virtualControlMask(CONTROL_DPAD_LEFT)) != 0;
    bool rightHeld = (currentMask & virtualControlMask(CONTROL_DPAD_RIGHT)) != 0;
    bool upHeld = (currentMask & virtualControlMask(CONTROL_DPAD_UP)) != 0;
    bool downHeld = (currentMask & virtualControlMask(CONTROL_DPAD_DOWN)) != 0;

    if (directionX <= -engageThreshold || (leftHeld && directionX <= -releaseThreshold))
        *outMask |= virtualControlMask(CONTROL_DPAD_LEFT);
    if (directionX >= engageThreshold || (rightHeld && directionX >= releaseThreshold))
        *outMask |= virtualControlMask(CONTROL_DPAD_RIGHT);
    if (directionY <= -engageThreshold || (upHeld && directionY <= -releaseThreshold))
        *outMask |= virtualControlMask(CONTROL_DPAD_UP);
    if (directionY >= engageThreshold || (downHeld && directionY >= releaseThreshold))
        *outMask |= virtualControlMask(CONTROL_DPAD_DOWN);
    return true;
}

static bool hitTestVirtualDpadSegmentedRing(int x, int y, uint32_t* outMask)
{
    if (!outMask)
    {
        return false;
    }
    *outMask = 0;
    mapRendererPointToVirtualControls(&x, &y);

    VirtualControlButton buttons[kVirtualControlButtonCapacity];
    int count = buildVirtualControls(buttons, kVirtualControlButtonCapacity);
    int unit = 0;
    int centerX = 0;
    int centerY = 0;
    if (!getVirtualDpadGeometry(buttons, count, &unit, &centerX, &centerY))
    {
        return false;
    }

    int deltaX = x - centerX;
    int deltaY = y - centerY;
    int absoluteX = abs(deltaX);
    int absoluteY = abs(deltaY);
    int outerRadius = unit * 142 / 100;
    int innerRadius = std::max(3, unit * 34 / 100);
    int64_t distanceSquared =
        (int64_t)deltaX * deltaX + (int64_t)deltaY * deltaY;
    if (distanceSquared > (int64_t)outerRadius * outerRadius ||
        distanceSquared < (int64_t)innerRadius * innerRadius)
    {
        return false;
    }

    const int cardinalSectorSlope = 625;
    bool horizontalOnly =
        (int64_t)absoluteY * 1000 <= (int64_t)absoluteX * cardinalSectorSlope;
    bool verticalOnly =
        (int64_t)absoluteX * 1000 <= (int64_t)absoluteY * cardinalSectorSlope;
    for (int i = 4; i < 8; ++i)
    {
        int directionX = 0;
        int directionY = 0;
        virtualDpadButtonDirection(
            buttons[i], centerX, centerY, &directionX, &directionY);
        bool horizontalMatch = !verticalOnly &&
            ((directionX < 0 && deltaX < 0) || (directionX > 0 && deltaX > 0));
        bool verticalMatch = !horizontalOnly &&
            ((directionY < 0 && deltaY < 0) || (directionY > 0 && deltaY > 0));
        if (horizontalMatch || verticalMatch)
        {
            *outMask |= buttons[i].controlMask;
        }
    }
    return *outMask != 0;
}

static void updateVirtualPointerControls(uint32_t newMask)
{
    uint32_t changed = g_virtualPointerControls ^ newMask;
    if (!changed)
    {
        return;
    }

    uint32_t oldSyntheticMask = frontendSyntheticControlMask();
    g_virtualPointerControls = newMask;
    applyFrontendSyntheticControlMask(oldSyntheticMask, frontendSyntheticControlMask());
}

static uint32_t combinedVirtualPointerControlMask(void)
{
    uint32_t combinedMask = g_virtualMouseControlMask;
    combinedMask |= g_androidReleasedTouchControls;
    for (size_t i = 0; i < g_androidVirtualTouchContacts.size(); ++i)
    {
        combinedMask |= g_androidVirtualTouchContacts[i].controlMask;
    }
    return combinedMask;
}

static void applyCombinedVirtualPointerControls(void)
{
    updateVirtualPointerControls(combinedVirtualPointerControlMask());
}

static void clearAndroidReleasedTouchControls(uint32_t controlMask)
{
    g_androidReleasedTouchControls &= ~controlMask;
    for (uint32_t bit = 0; bit < 32; ++bit)
    {
        uint32_t mask = 1u << bit;
        if (controlMask & mask)
        {
            g_androidReleasedTouchControlUntilTicks[bit] = 0;
        }
    }
}

static void holdAndroidReleasedTouchControlsUntil(
    uint32_t controlMask, uint64_t untilTicks)
{
    if (!controlMask || !untilTicks)
    {
        return;
    }
    g_androidReleasedTouchControls |= controlMask;
    for (uint32_t bit = 0; bit < 32; ++bit)
    {
        uint32_t mask = 1u << bit;
        if (controlMask & mask)
        {
            g_androidReleasedTouchControlUntilTicks[bit] = std::max(
                g_androidReleasedTouchControlUntilTicks[bit], untilTicks);
        }
    }
}

static void scheduleVirtualMouseRelease(void)
{
    if (!g_virtualMouseControlMask)
    {
        g_virtualMouseReleaseAtTicks = 0;
        return;
    }
    g_virtualMouseReleaseAtTicks = SDL_GetTicks64() + kVirtualPointerClickHoldMs;
}

static void updateVirtualPointerReleaseTimer(void)
{
    uint64_t now = SDL_GetTicks64();
    bool controlsChanged = false;
    if (!g_virtualMousePointerHeld && g_virtualMouseReleaseAtTicks &&
        now >= g_virtualMouseReleaseAtTicks)
    {
        g_virtualMouseControlMask = 0;
        g_virtualMouseReleaseAtTicks = 0;
        controlsChanged = true;
    }
    uint32_t expiredTouchControls = 0;
    for (uint32_t bit = 0; bit < 32; ++bit)
    {
        uint32_t mask = 1u << bit;
        uint64_t untilTicks = g_androidReleasedTouchControlUntilTicks[bit];
        if ((g_androidReleasedTouchControls & mask) && untilTicks &&
            now >= untilTicks)
        {
            expiredTouchControls |= mask;
        }
    }
    if (expiredTouchControls)
    {
        clearAndroidReleasedTouchControls(expiredTouchControls);
        controlsChanged = true;
    }
    if (controlsChanged)
    {
        applyCombinedVirtualPointerControls();
    }
}

static bool virtualFingerPoint(const SDL_TouchFingerEvent& finger, int* outX, int* outY)
{
    if (!g_renderer || !outX || !outY)
    {
        return false;
    }

    int width = 0;
    int height = 0;
    SDL_GetRendererOutputSize(g_renderer, &width, &height);
    if (width <= 0 || height <= 0)
    {
        return false;
    }

    int x = (int)(finger.x * width);
    int y = (int)(finger.y * height);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= width) x = width - 1;
    if (y >= height) y = height - 1;
    *outX = x;
    *outY = y;
    return true;
}

static bool androidMenuEventPoint(const SDL_Event& ev, int* outX, int* outY, bool* outReleased)
{
    if (!outX || !outY || !outReleased)
    {
        return false;
    }
    *outReleased = false;
    if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP)
    {
        if (ev.button.button != SDL_BUTTON_LEFT)
        {
            return false;
        }
        *outX = ev.button.x;
        *outY = ev.button.y;
        *outReleased = ev.type == SDL_MOUSEBUTTONUP;
        return true;
    }
    if (ev.type == SDL_FINGERDOWN || ev.type == SDL_FINGERUP)
    {
        if (!virtualFingerPoint(ev.tfinger, outX, outY))
        {
            return false;
        }
        *outReleased = ev.type == SDL_FINGERUP;
        return true;
    }
    if (ev.type == SDL_MOUSEMOTION)
    {
        *outX = ev.motion.x;
        *outY = ev.motion.y;
        return true;
    }
    if (ev.type == SDL_FINGERMOTION)
    {
        return virtualFingerPoint(ev.tfinger, outX, outY);
    }
    return false;
}

static bool consumeAndroidMenuOpeningPointerEvent(const SDL_Event& ev)
{
    if (g_androidMenuOpeningMouseReleasePending)
    {
        if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT)
        {
            g_androidMenuOpeningMouseReleasePending = false;
            return true;
        }
        if (ev.type == SDL_MOUSEMOTION && (ev.motion.state & SDL_BUTTON_LMASK))
        {
            return true;
        }
        if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT)
        {
            g_androidMenuOpeningMouseReleasePending = false;
        }
    }
    if (g_androidMenuOpeningFingerReleasePending &&
        (ev.type == SDL_FINGERDOWN || ev.type == SDL_FINGERUP ||
            ev.type == SDL_FINGERMOTION) &&
        ev.tfinger.fingerId == g_androidMenuOpeningFingerId)
    {
        if (ev.type == SDL_FINGERDOWN)
        {
            g_androidMenuOpeningFingerReleasePending = false;
            return false;
        }
        if (ev.type == SDL_FINGERUP)
        {
            g_androidMenuOpeningFingerReleasePending = false;
        }
        return true;
    }
    return false;
}

static bool handleAndroidMenuEvent(const SDL_Event& ev)
{
    if (consumeAndroidMenuOpeningPointerEvent(ev))
    {
        return true;
    }
    if (handleAndroidMenuNavigationEvent(ev))
    {
        return true;
    }
    if (ev.type == SDL_KEYDOWN && !ev.key.repeat &&
        (ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE || ev.key.keysym.scancode == SDL_SCANCODE_AC_BACK))
    {
        if (!frontendGameRunning() && g_androidMenuScreen == ANDROID_MENU_LIBRARY)
        {
            frontendRequestQuit();
        }
        else if (g_androidMenuScreen == ANDROID_MENU_NONE)
        {
            openAndroidMenu(ANDROID_MENU_PAUSE);
        }
        else
        {
            navigateBackAndroidMenu();
        }
        return true;
    }

    if (!frontendGameRunning() && g_androidMenuScreen == ANDROID_MENU_LIBRARY &&
        ev.type == SDL_MOUSEWHEEL)
    {
        int width = 0;
        int height = 0;
        SDL_GetRendererOutputSize(g_renderer, &width, &height);
        AndroidLibraryLayout layout = androidLibraryLayout(width, height);
        int scrollDelta = -ev.wheel.y * std::max(1, layout.rowStep * 2);
        g_androidLibraryScrollOffset += scrollDelta;
        g_androidLibraryScrollVelocity = (float)scrollDelta * 0.35f;
        g_androidLibraryScrollLastMotionTicks = SDL_GetTicks64();
        clampAndroidLibraryScroll(width, height);
        return true;
    }
    if (androidMenuScreenHasSettingsList() && ev.type == SDL_MOUSEWHEEL)
    {
        int width = 0;
        int height = 0;
        SDL_GetRendererOutputSize(g_renderer, &width, &height);
        SDL_Rect panel = androidPanelRect(width, height);
        int rowStep = androidMenuRowStep();
        g_androidMenuScrollOffset -= ev.wheel.y * std::max(1, rowStep * 2);
        clampAndroidMenuScroll(panel, androidSettingsMenuRowCount());
        return true;
    }
    int x = 0;
    int y = 0;
    bool released = false;
    if (!androidMenuEventPoint(ev, &x, &y, &released))
    {
        return g_androidMenuScreen != ANDROID_MENU_NONE &&
            (ev.type == SDL_FINGERMOTION || ev.type == SDL_MOUSEMOTION);
    }

    int width = 0;
    int height = 0;
    SDL_GetRendererOutputSize(g_renderer, &width, &height);
    SDL_Rect panel = androidPanelRect(width, height);
    if (androidMenuScreenHasSettingsList())
    {
        int rowCount = androidSettingsMenuRowCount();
        SDL_Rect viewport = androidMenuViewportRect(panel);
        if (!released && (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_FINGERDOWN) &&
            pointInRect(x, y, viewport))
        {
            g_androidMenuScrollDragging = true;
            g_androidMenuScrollMoved = false;
            g_androidMenuScrollStartY = y;
            g_androidMenuScrollStartOffset = g_androidMenuScrollOffset;
            return true;
        }
        if (!released && (ev.type == SDL_MOUSEMOTION || ev.type == SDL_FINGERMOTION) &&
            g_androidMenuScrollDragging)
        {
            int delta = y - g_androidMenuScrollStartY;
            if (std::abs(delta) >= std::max(4, androidUiScale() * 3))
            {
                g_androidMenuScrollMoved = true;
            }
            g_androidMenuScrollOffset = g_androidMenuScrollStartOffset - delta;
            clampAndroidMenuScroll(panel, rowCount);
            return true;
        }
    }
    if (!frontendGameRunning() && g_androidMenuScreen == ANDROID_MENU_LIBRARY)
    {
        AndroidLibraryLayout layout = androidLibraryLayout(width, height);
        const int viewportHeight = layout.viewportHeight;
        SDL_Rect viewport = { 0, layout.top, width, viewportHeight };
        if (ev.type == SDL_MOUSEWHEEL)
        {
            int scrollDelta = -ev.wheel.y * std::max(1, layout.rowStep * 2);
            g_androidLibraryScrollOffset += scrollDelta;
            g_androidLibraryScrollVelocity = (float)scrollDelta * 0.35f;
            g_androidLibraryScrollLastMotionTicks = SDL_GetTicks64();
            clampAndroidLibraryScroll(width, height);
            return true;
        }
        if (!released && (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_FINGERDOWN))
        {
            if (pointInRect(x, y, viewport) &&
                !pointInRect(x, y, androidLibraryAddButtonRect(width)))
            {
                g_androidLibraryScrollDragging = true;
                g_androidLibraryScrollMoved = false;
                g_androidLibraryScrollStartY = y;
                g_androidLibraryScrollStartOffset = g_androidLibraryScrollOffset;
                g_androidLibraryScrollLastMotionTicks = SDL_GetTicks64();
                g_androidLibraryScrollVelocity = 0.0f;
                return true;
            }
        }
        if (!released && (ev.type == SDL_MOUSEMOTION || ev.type == SDL_FINGERMOTION) &&
            g_androidLibraryScrollDragging)
        {
            int delta = y - g_androidLibraryScrollStartY;
            int previousOffset = g_androidLibraryScrollOffset;
            uint64_t now = SDL_GetTicks64();
            uint64_t elapsed = std::max<uint64_t>(1, now - g_androidLibraryScrollLastMotionTicks);
            if (std::abs(delta) >= std::max(4, androidUiScale() * 3))
            {
                g_androidLibraryScrollMoved = true;
            }
            g_androidLibraryScrollOffset = g_androidLibraryScrollStartOffset - delta;
            g_androidLibraryScrollVelocity = (float)(g_androidLibraryScrollOffset - previousOffset) *
                16.0f / (float)std::min<uint64_t>(elapsed, 50);
            g_androidLibraryScrollVelocity = std::max(-80.0f,
                std::min(80.0f, g_androidLibraryScrollVelocity));
            g_androidLibraryScrollLastMotionTicks = now;
            clampAndroidLibraryScroll(width, height);
            return true;
        }
    }
    if (!released)
    {
        if (g_androidMenuScreen != ANDROID_MENU_NONE &&
            g_androidMenuScreen != ANDROID_MENU_LIBRARY)
        {
            hideAndroidMenuSelectionHighlight();
        }
        return g_androidMenuScreen != ANDROID_MENU_NONE ||
            (frontendGameRunning() && !portraitModeEnabled() &&
                pointInRect(x, y, androidMenuButtonRect(width)));
    }

    if (g_androidMenuScreen != ANDROID_MENU_NONE &&
        g_androidMenuScreen != ANDROID_MENU_LIBRARY)
    {
        hideAndroidMenuSelectionHighlight();
    }

    if (released && g_androidMenuScrollDragging)
    {
        bool wasMoved = g_androidMenuScrollMoved;
        g_androidMenuScrollDragging = false;
        g_androidMenuScrollMoved = false;
        if (wasMoved)
        {
            return true;
        }
    }

    if (!frontendGameRunning() && g_androidMenuScreen == ANDROID_MENU_LIBRARY)
    {
        if (released && g_androidLibraryScrollDragging)
        {
            bool wasMoved = g_androidLibraryScrollMoved;
            g_androidLibraryScrollDragging = false;
            g_androidLibraryScrollMoved = false;
            if (wasMoved)
            {
                return true;
            }
        }
        if (isAndroidGameLibraryScanning())
        {
            return true;
        }
        if (pointInRect(x, y, androidLibrarySettingsButtonRect(width)))
        {
            openAndroidMenu(ANDROID_MENU_MAIN);
            return true;
        }
        if (pointInRect(x, y, androidLibraryFileManagerButtonRect(width)))
        {
            showAndroidLanFileManager();
            return true;
        }
        if (pointInRect(x, y, androidLibraryAddButtonRect(width)))
        {
            requestAndroidGameImport();
            return true;
        }
        AndroidLibraryLayout layout = androidLibraryLayout(width, height);
        SDL_Rect viewport = { 0, layout.top, width, layout.viewportHeight };
        if (!pointInRect(x, y, viewport))
        {
            return true;
        }
        int visibleCount = androidLibraryVisibleCount(width, height);
        int totalCount = (int)g_androidGamePaths.size();
        int firstRow = layout.rowStep > 0 ? g_androidLibraryScrollOffset / layout.rowStep : 0;
        if (firstRow > 0) --firstRow;
        int lastRow = std::min(totalCount, firstRow + visibleCount + 2);
        for (int i = firstRow; i < lastRow; ++i)
        {
            const std::string path = g_androidGamePaths[(size_t)i];
            if (pointInRect(x, y, androidLibraryRemoveButtonRect(width, height, i)))
            {
                if (!confirmAndroidGameRemoval(path))
                {
                    return true;
                }
                if (removeAndroidGamePath(path))
                {
                    refreshAndroidGameLibrary();
                }
                return true;
            }
            if (pointInRect(x, y, androidLibraryRowRect(width, height, i)))
            {
                requestAndroidGame(path);
                return true;
            }
        }
        return true;
    }

    if (released && g_androidMenuScreen != ANDROID_MENU_NONE &&
        g_androidMenuScreen != ANDROID_MENU_LIBRARY && !pointInRect(x, y, panel))
    {
        navigateBackAndroidMenu();
        return true;
    }

    if (g_androidMenuScreen == ANDROID_MENU_NONE)
    {
        if (virtualControlsVisible() && !portraitModeEnabled() &&
            pointInRect(x, y, androidMenuButtonRect(width)))
        {
            openAndroidMenu(ANDROID_MENU_PAUSE);
            return true;
        }
        return false;
    }

    if (g_androidMenuScreen == ANDROID_MENU_PAUSE)
    {
        for (int row = 0; row < ANDROID_PAUSE_ROW_COUNT; ++row)
        {
            if (!pointInRect(x, y, androidPanelRowRect(panel, row)))
            {
                continue;
            }
            selectAndroidMenuRow(row, false);
            if (row == ANDROID_PAUSE_SAVE_STATE) openAndroidMenu(ANDROID_MENU_SAVE_STATE);
            else if (row == ANDROID_PAUSE_SWITCH_GAME) requestAndroidSwitchGame();
            else if (row == ANDROID_PAUSE_RESTART_GAME) requestAndroidRestartGame();
            else if (row == ANDROID_PAUSE_OPTIONS) openAndroidMenu(ANDROID_MENU_OPTIONS);
            else if (row == ANDROID_PAUSE_SETTINGS) openAndroidMenu(ANDROID_MENU_SETTINGS);
            else if (row == ANDROID_PAUSE_EXIT_APPLICATION) requestAndroidExitApplication();
            else if (row == ANDROID_PAUSE_BACK) openAndroidMenu(ANDROID_MENU_NONE);
            return true;
        }
    }
    else if (g_androidMenuScreen == ANDROID_MENU_SAVE_STATE)
    {
        if (!released || g_androidSaveStateBusy)
        {
            return true;
        }
        for (int slot = 1; slot <= kSaveStateSlotCount; ++slot)
        {
            if (pointInRect(x, y, androidSaveStateSlotRect(panel, slot)))
            {
                selectAndroidMenuRow(slot - 1, false);
                if (g_androidSaveStateSelectedSlot == slot)
                {
                    return true;
                }
                g_androidSaveStateSelectedSlot = slot;
                refreshAndroidSaveStateSlotInfo(slot);
                refreshAndroidSaveStateThumbnail();
                return true;
            }
        }
        if (pointInRect(x, y, androidSaveStateActionRect(panel, 0)))
        {
            selectAndroidMenuRow(kSaveStateSlotCount, false);
            performAndroidSaveStateAction(true);
            return true;
        }
        if (pointInRect(x, y, androidSaveStateActionRect(panel, 1)))
        {
            selectAndroidMenuRow(kSaveStateSlotCount + 1, false);
            performAndroidSaveStateAction(false);
            return true;
        }
        if (pointInRect(x, y, androidSaveStateActionRect(panel, 2)))
        {
            selectAndroidMenuRow(kSaveStateSlotCount + 2, false);
            deleteAndroidSaveState();
            return true;
        }
        if (pointInRect(x, y, androidSaveStateActionRect(panel, 3)))
        {
            selectAndroidMenuRow(kSaveStateSlotCount + 3, false);
            navigateBackAndroidMenu();
            return true;
        }
        return true;
    }
    else if (g_androidMenuScreen == ANDROID_MENU_ABOUT)
    {
        if (pointInRect(x, y, androidPanelRowRect(panel, ANDROID_ABOUT_BACK)))
        {
            selectAndroidMenuRow(ANDROID_ABOUT_BACK, false);
            navigateBackAndroidMenu();
        }
        return true;
    }
    else if (g_androidMenuScreen == ANDROID_MENU_MAIN ||
        g_androidMenuScreen == ANDROID_MENU_OPTIONS)
    {
        int rowCount = androidSettingsMenuRowCount();
        SDL_Rect viewport = androidMenuViewportRect(panel);
        if (!pointInRect(x, y, viewport))
        {
            return true;
        }
        for (int row = 0; row < rowCount; ++row)
        {
            if (!pointInRect(x, y, androidMenuRowRect(panel, row)))
            {
                continue;
            }
            selectAndroidMenuRow(row, false);
            if (g_androidMenuScreen == ANDROID_MENU_MAIN)
                handleAndroidMainMenuSelection(row);
            else
                handleAndroidOptionsSelection(row);
            return true;
        }
    }
    else if (g_androidMenuScreen == ANDROID_MENU_VIDEO ||
        g_androidMenuScreen == ANDROID_MENU_AUDIO ||
        g_androidMenuScreen == ANDROID_MENU_INPUT ||
        g_androidMenuScreen == ANDROID_MENU_CONTROLLER_MAPPING ||
        g_androidMenuScreen == ANDROID_MENU_CONTROLLER_CALIBRATION ||
        g_androidMenuScreen == ANDROID_MENU_SETTINGS ||
        g_androidMenuScreen == ANDROID_MENU_CHEAT_MANAGER)
    {
        int rowCount = androidSettingsMenuRowCount();
        SDL_Rect viewport = androidMenuViewportRect(panel);
        if (!pointInRect(x, y, viewport))
        {
            return true;
        }
        for (int row = 0; row < rowCount; ++row)
        {
            if (!pointInRect(x, y, androidMenuRowRect(panel, row)))
            {
                continue;
            }
            selectAndroidMenuRow(row, false);
            handleAndroidDetailMenuSelection(g_androidMenuScreen, row);
            return true;
        }
    }
    return true;
}

static bool getLandscapeGameDestination(SDL_Rect* outRect)
{
    if (!outRect || !g_renderer || portraitModeEnabled())
    {
        return false;
    }

    int outputWidth = 0;
    int outputHeight = 0;
    SDL_GetRendererOutputSize(g_renderer, &outputWidth, &outputHeight);
    if (outputWidth <= 0 || outputHeight <= 0)
    {
        return false;
    }

    int width = outputWidth;
    int height = (int)(((int64_t)width * SCREEN_HEIGHT) / SCREEN_WIDTH);
    if (height > outputHeight)
    {
        height = outputHeight;
        width = (int)(((int64_t)height * SCREEN_WIDTH) / SCREEN_HEIGHT);
    }

    outRect->x = (outputWidth - width) / 2;
    outRect->y = (outputHeight - height) / 2;
    outRect->w = width;
    outRect->h = height;
    return true;
}

static bool getPortraitGameDestination(SDL_Rect* outRect)
{
    if (!outRect || !g_renderer || !portraitModeEnabled())
    {
        return false;
    }

    int outputWidth = 0;
    int outputHeight = 0;
    SDL_GetRendererOutputSize(g_renderer, &outputWidth, &outputHeight);
    if (outputWidth <= 0 || outputHeight <= 0)
    {
        return false;
    }

    int width = outputWidth;
    int height = (int)(((int64_t)width * SCREEN_HEIGHT) / SCREEN_WIDTH);
    if (height > outputHeight)
    {
        height = outputHeight;
        width = (int)(((int64_t)height * SCREEN_WIDTH) / SCREEN_HEIGHT);
    }

    outRect->x = (outputWidth - width) / 2;
    outRect->y = (outputHeight - height) / 2;
    outRect->w = width;
    outRect->h = height;
    return true;
}

static uint32_t virtualDpadControlMask(void)
{
    return virtualControlMask(CONTROL_DPAD_UP) |
        virtualControlMask(CONTROL_DPAD_DOWN) |
        virtualControlMask(CONTROL_DPAD_LEFT) |
        virtualControlMask(CONTROL_DPAD_RIGHT);
}

static int findAndroidVirtualTouchContact(SDL_FingerID fingerId)
{
    for (size_t i = 0; i < g_androidVirtualTouchContacts.size(); ++i)
    {
        if (g_androidVirtualTouchContacts[i].fingerId == fingerId)
        {
            return (int)i;
        }
    }
    return -1;
}

static bool androidVirtualDpadTouchActive(void)
{
    for (size_t i = 0; i < g_androidVirtualTouchContacts.size(); ++i)
    {
        if (g_androidVirtualTouchContacts[i].controlsDpad)
        {
            return true;
        }
    }
    return false;
}

static bool handleVirtualControlPointerEvent(const SDL_Event& ev)
{
    if (!virtualControlsVisible())
    {
        releaseVirtualPointerControls();
        return false;
    }

    bool leftMouseEvent =
        (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) ||
        (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) ||
        (ev.type == SDL_MOUSEMOTION && (ev.motion.state & SDL_BUTTON_LMASK));
    bool fingerEvent = ev.type == SDL_FINGERDOWN || ev.type == SDL_FINGERUP ||
        ev.type == SDL_FINGERMOTION;
    if ((leftMouseEvent || fingerEvent) && frontendPostRestoreInputBlocked())
    {
        releaseVirtualPointerControls();
        if (inputTraceEnabled())
        {
            printf("frontend: virtual pointer ignored after restore type=%u\n",
                (unsigned int)ev.type);
        }
        return true;
    }

    if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT)
    {
        g_virtualMouseReleaseAtTicks = 0;
        uint32_t dpadMask = 0;
        bool dpadHit = virtualDpadType() == VIRTUAL_DPAD_SEGMENTED_RING ?
            hitTestVirtualDpadSegmentedRing(ev.button.x, ev.button.y, &dpadMask) :
            hitTestVirtualDpadDrag(ev.button.x, ev.button.y, true, 0, &dpadMask);
        if (dpadHit)
        {
            g_virtualMousePointerHeld = true;
            g_virtualMouseControlsDpad = true;
            g_virtualMouseControlMask = dpadMask;
            applyCombinedVirtualPointerControls();
            return true;
        }
        uint32_t controlMask = hitTestVirtualControls(ev.button.x, ev.button.y);
        controlMask &= ~virtualDpadControlMask();
        if (portraitModeEnabled() &&
            controlMask == virtualControlMask(CONTROL_POWER))
        {
            releaseVirtualPointerControls();
            openAndroidMenu(ANDROID_MENU_PAUSE);
            g_androidMenuOpeningMouseReleasePending = true;
            return true;
        }
        if (controlMask)
        {
            g_virtualMousePointerHeld = true;
            g_virtualMouseControlMask = controlMask;
            applyCombinedVirtualPointerControls();
            if (inputTraceEnabled())
            {
                printf("frontend: virtual mouse down mask=0x%08X x=%d y=%d\n",
                    (unsigned int)controlMask, ev.button.x, ev.button.y);
            }
            return true;
        }
    }
    else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT)
    {
        if (g_virtualMousePointerHeld || g_virtualMouseControlMask)
        {
            g_virtualMousePointerHeld = false;
            if (g_virtualMouseControlsDpad)
            {
                g_virtualMouseControlsDpad = false;
                g_virtualMouseControlMask = 0;
                if (!androidVirtualDpadTouchActive())
                {
                    g_virtualDpadOffsetX = 0;
                    g_virtualDpadOffsetY = 0;
                }
                applyCombinedVirtualPointerControls();
                return true;
            }
            scheduleVirtualMouseRelease();
            if (inputTraceEnabled())
            {
                printf("frontend: virtual mouse release scheduled mask=0x%08X hold_ms=%llu\n",
                    (unsigned int)g_virtualMouseControlMask,
                    (unsigned long long)kVirtualPointerClickHoldMs);
            }
            return true;
        }
    }
    else if (ev.type == SDL_MOUSEMOTION && (ev.motion.state & SDL_BUTTON_LMASK))
    {
        if (g_virtualMousePointerHeld)
        {
            if (g_virtualMouseControlsDpad)
            {
                uint32_t dpadMask = 0;
                if (virtualDpadType() == VIRTUAL_DPAD_SEGMENTED_RING)
                {
                    hitTestVirtualDpadSegmentedRing(ev.motion.x, ev.motion.y, &dpadMask);
                    g_virtualMouseControlMask = dpadMask;
                    applyCombinedVirtualPointerControls();
                }
                else if (hitTestVirtualDpadDrag(ev.motion.x, ev.motion.y, false,
                    g_virtualMouseControlMask, &dpadMask))
                {
                    g_virtualMouseControlMask = dpadMask;
                    applyCombinedVirtualPointerControls();
                }
                return true;
            }
            g_virtualMouseControlMask =
                hitTestVirtualControls(ev.motion.x, ev.motion.y) & ~virtualDpadControlMask();
            applyCombinedVirtualPointerControls();
            return true;
        }
    }
    else if (ev.type == SDL_FINGERDOWN)
    {
        int x = 0;
        int y = 0;
        if (!virtualFingerPoint(ev.tfinger, &x, &y))
        {
            return false;
        }

        int existingIndex = findAndroidVirtualTouchContact(ev.tfinger.fingerId);
        if (existingIndex >= 0)
        {
            g_androidVirtualTouchContacts.erase(
                g_androidVirtualTouchContacts.begin() + existingIndex);
        }

        AndroidVirtualTouchContact contact = {
            ev.tfinger.fingerId, 0, false, SDL_GetTicks64() };
        uint32_t dpadMask = 0;
        bool dpadHit = virtualDpadType() == VIRTUAL_DPAD_SEGMENTED_RING ?
            hitTestVirtualDpadSegmentedRing(x, y, &dpadMask) :
            hitTestVirtualDpadDrag(x, y, true, 0, &dpadMask);
        if (!androidVirtualDpadTouchActive() && dpadHit)
        {
            contact.controlMask = dpadMask;
            contact.controlsDpad = true;
        }
        else
        {
            contact.controlMask = hitTestVirtualControls(x, y) & ~virtualDpadControlMask();
        }
        if (!contact.controlsDpad && portraitModeEnabled() &&
            contact.controlMask == virtualControlMask(CONTROL_POWER))
        {
            releaseVirtualPointerControls();
            openAndroidMenu(ANDROID_MENU_PAUSE);
            g_androidMenuOpeningFingerId = ev.tfinger.fingerId;
            g_androidMenuOpeningFingerReleasePending = true;
            return true;
        }

        if (!contact.controlsDpad && !contact.controlMask)
        {
            applyCombinedVirtualPointerControls();
            return false;
        }

        clearAndroidReleasedTouchControls(contact.controlMask);
        g_androidVirtualTouchContacts.push_back(contact);
        applyCombinedVirtualPointerControls();
        if (inputTraceEnabled())
        {
            printf("frontend: virtual touch down finger=%lld mask=0x%08X x=%d y=%d\n",
                (long long)ev.tfinger.fingerId, (unsigned int)contact.controlMask, x, y);
        }
        return true;
    }
    else if (ev.type == SDL_FINGERUP)
    {
        int contactIndex = findAndroidVirtualTouchContact(ev.tfinger.fingerId);
        if (contactIndex < 0)
        {
            return false;
        }

        AndroidVirtualTouchContact contact = g_androidVirtualTouchContacts[contactIndex];
        g_androidVirtualTouchContacts.erase(
            g_androidVirtualTouchContacts.begin() + contactIndex);
        if (contact.controlsDpad)
        {
            if (!g_virtualMouseControlsDpad && !androidVirtualDpadTouchActive())
            {
                g_virtualDpadOffsetX = 0;
                g_virtualDpadOffsetY = 0;
            }
        }
        else if (contact.controlMask)
        {
            uint64_t now = SDL_GetTicks64();
            uint64_t holdUntilTicks = now + kVirtualPointerClickHoldMs;
            if (virtualControlMaskOnlyHasGameplayButtons(contact.controlMask))
            {
                holdUntilTicks = contact.pressedAtTicks +
                    kVirtualGameplayButtonMinimumPressMs;
            }
            if (holdUntilTicks > now)
            {
                holdAndroidReleasedTouchControlsUntil(
                    contact.controlMask, holdUntilTicks);
            }
        }
        applyCombinedVirtualPointerControls();
        if (inputTraceEnabled())
        {
            printf("frontend: virtual touch up finger=%lld mask=0x%08X\n",
                (long long)ev.tfinger.fingerId, (unsigned int)contact.controlMask);
        }
        return true;
    }
    else if (ev.type == SDL_FINGERMOTION)
    {
        int contactIndex = findAndroidVirtualTouchContact(ev.tfinger.fingerId);
        if (contactIndex < 0)
        {
            return false;
        }

        int x = 0;
        int y = 0;
        if (!virtualFingerPoint(ev.tfinger, &x, &y))
        {
            return true;
        }

        AndroidVirtualTouchContact& contact = g_androidVirtualTouchContacts[contactIndex];
        if (contact.controlsDpad)
        {
            uint32_t dpadMask = 0;
            if (virtualDpadType() == VIRTUAL_DPAD_SEGMENTED_RING)
            {
                hitTestVirtualDpadSegmentedRing(x, y, &dpadMask);
                contact.controlMask = dpadMask;
            }
            else if (hitTestVirtualDpadDrag(x, y, false,
                contact.controlMask, &dpadMask))
            {
                contact.controlMask = dpadMask;
            }
        }
        else
        {
            uint32_t controlMask =
                hitTestVirtualControls(x, y) & ~virtualDpadControlMask();
            if (controlMask != contact.controlMask)
            {
                contact.controlMask = controlMask;
                contact.pressedAtTicks = SDL_GetTicks64();
                clearAndroidReleasedTouchControls(controlMask);
            }
        }
        applyCombinedVirtualPointerControls();
        return true;
    }

    return false;
}

static void drawVirtualButton(const VirtualControlButton& button)
{
    bool pressed = virtualButtonPressed(button);
    if (button.dpadDx || button.dpadDy)
    {
        if (virtualDpadType() == VIRTUAL_DPAD_SEGMENTED_RING)
        {
            return;
        }
        const int minSide = button.rect.w < button.rect.h ? button.rect.w : button.rect.h;
        const int buttonCenterX = button.rect.x + button.rect.w / 2;
        const int buttonCenterY = button.rect.y + button.rect.h / 2;
        const int dpadCenterX = buttonCenterX - button.dpadDx * minSide;
        const int dpadCenterY = buttonCenterY - button.dpadDy * minSide;
        const double directionLength = sqrt((double)(button.dpadDx * button.dpadDx +
            button.dpadDy * button.dpadDy));
        const double directionX = button.dpadDx / directionLength;
        const double directionY = button.dpadDy / directionLength;
        const int cx = dpadCenterX + (int)lround(directionX * minSide);
        const int cy = dpadCenterY + (int)lround(directionY * minSide);
        const int spread = minSide / 5;
        const int depth = minSide / 7;
        SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, pressed ? 255 : 235);
        const int tipX = cx + (int)lround(directionX * depth);
        const int tipY = cy + (int)lround(directionY * depth);
        const int baseX = cx - (int)lround(directionX * depth);
        const int baseY = cy - (int)lround(directionY * depth);
        const int perpX = (int)lround(-directionY * spread);
        const int perpY = (int)lround(directionX * spread);
        renderVirtualDrawLine(tipX, tipY, baseX + perpX, baseY + perpY);
        renderVirtualDrawLine(tipX, tipY, baseX - perpX, baseY - perpY);
        return;
    }
    if (!button.drawFrame && (!button.label || !button.label[0]))
    {
        return;
    }

    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    bool faceButton = virtualButtonHasControl(button, CONTROL_BUTTON_A) ||
        virtualButtonHasControl(button, CONTROL_BUTTON_B) ||
        virtualButtonHasControl(button, CONTROL_BUTTON_X) ||
        virtualButtonHasControl(button, CONTROL_BUTTON_Y);
    bool portraitMenuButton = portraitModeEnabled() &&
        virtualButtonHasControl(button, CONTROL_POWER);
    if (faceButton)
    {
        int radius = (button.rect.w < button.rect.h ? button.rect.w : button.rect.h) / 2 - 2;
        int centerX = button.rect.x + button.rect.w / 2;
        int centerY = button.rect.y + button.rect.h / 2;
        SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, pressed ? 112 : 42);
        renderVirtualFillCircle(centerX, centerY, radius);
        SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, pressed ? 255 : 210);
        renderVirtualDrawCircle(centerX, centerY, radius);
    }
    else
    {
        if (!portraitMenuButton)
        {
            SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, pressed ? 112 : 42);
            renderVirtualFillRect(button.rect);
        }
        SDL_SetRenderDrawColor(g_renderer, 255, 255, 255,
            portraitMenuButton ? 235 : (pressed ? 255 : 210));
        renderVirtualDrawRect(button.rect);
    }

    SDL_Color color = { 255, 255, 255, 235 };
    if (!portraitModeEnabled() &&
        (virtualButtonHasControl(button, CONTROL_BUTTON_START) ||
            virtualButtonHasControl(button, CONTROL_BUTTON_SELECT)))
    {
        drawAndroidSystemTextCentered(button.label, button.rect,
            virtualCompactButtonTextSize(button.rect), color);
        return;
    }
    if (!portraitModeEnabled())
    {
        drawAndroidSystemTextCentered(button.label, button.rect,
            virtualButtonTextSize(button.rect), color);
        return;
    }

    SDL_Rect portraitRect = rotateVirtualRectCcw(button.rect);
    drawAndroidSystemTextCentered(button.label, portraitRect,
        virtualButtonTextSize(portraitRect), color);
}

static void drawVirtualSegmentedRingDpad(
    const VirtualControlButton* buttons, int count, int unit, int centerX, int centerY)
{
    if (!buttons || count < 8 || unit <= 0)
    {
        return;
    }

    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    int outerRadius = unit * 140 / 100;
    int arcInnerRadius = unit * 100 / 100;
    int arcOuterRadius = unit * 124 / 100;
    SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 24);
    renderVirtualFillCircle(centerX, centerY, outerRadius);
    SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 190);
    renderVirtualDrawCircle(centerX, centerY, outerRadius);

    SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 74);
    int separatorInnerRadius = unit * 40 / 100;
    int separatorOuterRadius = arcOuterRadius;
    for (int i = 0; i < 4; ++i)
    {
        double angle = kPi / 4.0 + (double)i * kPi / 2.0;
        int innerX = centerX + (int)lround(cos(angle) * separatorInnerRadius);
        int innerY = centerY + (int)lround(sin(angle) * separatorInnerRadius);
        int outerX = centerX + (int)lround(cos(angle) * separatorOuterRadius);
        int outerY = centerY + (int)lround(sin(angle) * separatorOuterRadius);
        renderVirtualDrawLine(innerX, innerY, outerX, outerY);
    }

    for (int i = 4; i < 8; ++i)
    {
        int directionX = 0;
        int directionY = 0;
        virtualDpadButtonDirection(
            buttons[i], centerX, centerY, &directionX, &directionY);
        bool pressed = virtualButtonPressed(buttons[i]);
        SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, pressed ? 230 : 112);
        renderVirtualFillArcBand(centerX, centerY,
            arcInnerRadius, arcOuterRadius, directionX, directionY);
    }

    int centerDotRadius = std::max(2, unit * 7 / 100);
    SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 145);
    renderVirtualFillCircle(centerX, centerY, centerDotRadius);
}

static void drawVirtualControlsOverlay(void)
{
    if (g_androidMenuScreen != ANDROID_MENU_NONE)
    {
        releaseVirtualPointerControls();
        return;
    }
    if (!virtualControlsVisible())
    {
        releaseVirtualPointerControls();
        return;
    }

    VirtualControlButton buttons[kVirtualControlButtonCapacity];
    int count = buildVirtualControls(buttons, kVirtualControlButtonCapacity);
    int unit = 0;
    int centerX = 0;
    int centerY = 0;
    if (getVirtualDpadGeometry(buttons, count, &unit, &centerX, &centerY))
    {
        if (virtualDpadType() == VIRTUAL_DPAD_SEGMENTED_RING)
        {
            drawVirtualSegmentedRingDpad(buttons, count, unit, centerX, centerY);
        }
        else
        {
            int radius = unit * 3 / 2 - 3;
            SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 24);
            renderVirtualFillCircle(centerX, centerY, radius);
            SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 190);
            renderVirtualDrawCircle(centerX, centerY, radius);

            updateVirtualDpadVisualPosition();
            int thumbX = centerX + (int)lround(g_virtualDpadVisualOffsetX);
            int thumbY = centerY + (int)lround(g_virtualDpadVisualOffsetY);
            SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 58);
            renderVirtualFillCircle(thumbX, thumbY, unit / 2);
            SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 235);
            renderVirtualDrawCircle(thumbX, thumbY, unit / 2);
        }
    }
    for (int i = 0; i < count; ++i)
    {
        drawVirtualButton(buttons[i]);
    }
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
}

static bool inputTraceEnabled(void)
{
    static const bool enabled = []() {
        const char* value = getenv("DINGOO_PIE_INPUT_TRACE");
        return value && value[0] && value[0] != '0';
    }();
    return enabled;
}

static uint64_t parsePositiveEnv(const char* name, uint64_t defaultValue, uint64_t minValue, uint64_t maxValue)
{
    const char* value = getenv(name);
    if (!value || !value[0])
    {
        return defaultValue;
    }

    uint64_t parsed = strtoull(value, NULL, 10);
    if (parsed < minValue)
    {
        return minValue;
    }
    if (parsed > maxValue)
    {
        return maxValue;
    }
    return parsed;
}

static void disableTextComposition(void)
{
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "0");
    SDL_StopTextInput();
    SDL_EventState(SDL_TEXTINPUT, SDL_IGNORE);
    SDL_EventState(SDL_TEXTEDITING, SDL_IGNORE);
}

static void enableTextComposition(void)
{
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
    SDL_EventState(SDL_TEXTINPUT, SDL_ENABLE);
    SDL_EventState(SDL_TEXTEDITING, SDL_ENABLE);
}

static void setAndroidSystemImeDisabled(bool disabled)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        return;
    }
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "setSystemImeDisabledFromNative", "(Z)V") : NULL;
    if (method)
    {
        env->CallVoidMethod(activity, method, disabled ? JNI_TRUE : JNI_FALSE);
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    if (activityClass) env->DeleteLocalRef(activityClass);
}

static void applyWindowImePolicy(bool disabled)
{
    if (disabled)
    {
        disableTextComposition();
    }
    else
    {
        enableTextComposition();
    }
    setAndroidSystemImeDisabled(disabled);
}

static const char* windowEventName(uint8_t eventType)
{
    switch (eventType)
    {
    case SDL_WINDOWEVENT_SHOWN: return "shown";
    case SDL_WINDOWEVENT_HIDDEN: return "hidden";
    case SDL_WINDOWEVENT_EXPOSED: return "exposed";
    case SDL_WINDOWEVENT_MOVED: return "moved";
    case SDL_WINDOWEVENT_RESIZED: return "resized";
    case SDL_WINDOWEVENT_SIZE_CHANGED: return "size_changed";
    case SDL_WINDOWEVENT_MINIMIZED: return "minimized";
    case SDL_WINDOWEVENT_MAXIMIZED: return "maximized";
    case SDL_WINDOWEVENT_RESTORED: return "restored";
    case SDL_WINDOWEVENT_ENTER: return "enter";
    case SDL_WINDOWEVENT_LEAVE: return "leave";
    case SDL_WINDOWEVENT_FOCUS_GAINED: return "focus_gained";
    case SDL_WINDOWEVENT_FOCUS_LOST: return "focus_lost";
    case SDL_WINDOWEVENT_CLOSE: return "close";
    case SDL_WINDOWEVENT_TAKE_FOCUS: return "take_focus";
    case SDL_WINDOWEVENT_HIT_TEST: return "hit_test";
    default: return "unknown";
    }
}

static SDL_JoystickID activeGameControllerInstanceId(void)
{
    if (!g_gameController)
    {
        return -1;
    }
    SDL_Joystick* joystick = SDL_GameControllerGetJoystick(g_gameController);
    return joystick ? SDL_JoystickInstanceID(joystick) : -1;
}

static void applyGameControllerControlMasks(uint32_t buttonMask, uint32_t axisMask)
{
    uint32_t oldSyntheticMask = frontendSyntheticControlMask();
    g_gameControllerButtonControls = buttonMask;
    g_gameControllerAxisControls = axisMask;
    applyFrontendSyntheticControlMask(oldSyntheticMask, frontendSyntheticControlMask());
}

static void releaseGameControllerControls(void)
{
    memset(g_gameControllerAxes, 0, sizeof(g_gameControllerAxes));
    g_gameControllerMenuButtons = 0;
    g_gameControllerMenuActionActive = false;
    applyGameControllerControlMasks(0, 0);
}

static void releaseFrontendInputControls(void)
{
    releaseVirtualPointerControls();
    releaseGameControllerControls();
    inputClearSyntheticControls();
    inputClearControls();
}

bool frontendGameRunning(void)
{
    return g_gameRunning;
}

void frontendSetGameRunning(bool running)
{
    g_gameRunning = running;
}

bool frontendGamePaused(void)
{
    return SDL_AtomicGet(&g_gamePaused) != 0;
}

bool frontendUserGamePaused(void)
{
    return g_userPauseRequested;
}

static bool frontendEffectivePauseRequested(void)
{
    return g_userPauseRequested || g_minimizedPauseActive;
}

static bool isWindowMinimized(void)
{
    return g_androidBackgroundActive ||
        (g_window && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MINIMIZED) != 0);
}

static MinimizedBehavior currentMinimizedBehavior(void)
{
    return g_frontendSettings ? g_frontendSettings->minimizedBehavior : MINIMIZED_BEHAVIOR_PAUSE;
}

void frontendNotifyAndroidBackground(bool backgrounded)
{
    SDL_AtomicSet(&g_androidBackgroundRequested, backgrounded ? 1 : 0);
}

static void applyFrontendPauseState(void)
{
    bool paused = frontendEffectivePauseRequested();
    if (frontendGamePaused() == paused)
    {
        return;
    }

    SDL_AtomicSet(&g_gamePaused, paused ? 1 : 0);
    if (paused)
    {
        g_idleAnimationClock.pause();
        releaseFrontendInputControls();
    }
    pauseGateSetPaused(paused);
    if (paused)
    {
        gameRuntimeNotifyPauseRequested();
    }
    audioOutputSetFrontendPaused(paused);
    printf("frontend: game pause %s\n", paused ? "on" : "off");
}

static void setMinimizedPauseActive(bool active)
{
    if (g_minimizedPauseActive == active)
    {
        return;
    }
    g_minimizedPauseActive = active;
    applyFrontendPauseState();
}

static bool frontendWindowIsForeground(void)
{
    if (!g_window)
    {
        return false;
    }
    const uint32_t flags = SDL_GetWindowFlags(g_window);
    return (flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) == 0 &&
        (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
}

static SDL_Renderer* createFrontendRenderer(void)
{
    SDL_Renderer* renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer)
    {
        renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
    }
    return renderer;
}

static bool restoreRendererTextures(const char* reason)
{
    resetIdleTextures();
    if (g_blurredBackdropTexture)
    {
        SDL_DestroyTexture(g_blurredBackdropTexture);
        g_blurredBackdropTexture = NULL;
        g_blurredBackdropUpdateCounter = 0;
    }
    if (g_frameTexture)
    {
        SDL_DestroyTexture(g_frameTexture);
        g_frameTexture = NULL;
    }
    if (!createGameFrameTexture())
    {
        printf("frontend: renderer texture restore failed after %s: %s\n",
            reason ? reason : "foreground restore", SDL_GetError());
        return false;
    }
    if (g_frontendSettings)
    {
        SDL_SetTextureScaleMode(g_frameTexture,
            textureLinearSamplingEnabled(*g_frontendSettings) ?
                SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    }
    framebufferRequestUpdate();
    if (g_androidMenuScreen == ANDROID_MENU_SAVE_STATE)
    {
        refreshAndroidSaveStateThumbnail();
    }
    printf("frontend: renderer textures restored after %s\n",
        reason ? reason : "foreground restore");
    return true;
}

static bool recreateFrontendRenderer(const char* reason)
{
    resetIdleTextures();
    invalidateAndroidSaveStateThumbnail();
    if (g_blurredBackdropTexture)
    {
        SDL_DestroyTexture(g_blurredBackdropTexture);
        g_blurredBackdropTexture = NULL;
        g_blurredBackdropUpdateCounter = 0;
    }
    if (g_frameTexture)
    {
        SDL_DestroyTexture(g_frameTexture);
        g_frameTexture = NULL;
    }
    if (g_renderer)
    {
        SDL_DestroyRenderer(g_renderer);
        g_renderer = NULL;
    }

    g_renderer = createFrontendRenderer();
    if (!g_renderer)
    {
        printf("frontend: renderer recreation failed after %s: %s\n",
            reason ? reason : "device reset", SDL_GetError());
        return false;
    }
    if (!createGameFrameTexture())
    {
        printf("frontend: frame texture recreation failed after %s: %s\n",
            reason ? reason : "device reset", SDL_GetError());
        return false;
    }
    if (g_frontendSettings)
    {
        SDL_SetTextureScaleMode(g_frameTexture,
            textureLinearSamplingEnabled(*g_frontendSettings) ?
                SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    }
    framebufferRequestUpdate();
    if (g_androidMenuScreen == ANDROID_MENU_SAVE_STATE)
    {
        refreshAndroidSaveStateThumbnail();
    }
    printf("frontend: renderer recreated after %s\n",
        reason ? reason : "device reset");
    return true;
}

static void restoreFrontendFromBackground(const char* reason)
{
    if (!g_androidBackgroundActive && !g_minimizedPauseActive &&
        !g_androidRendererRestorePending)
    {
        return;
    }
    g_androidBackgroundActive = false;
    if (g_minimizedPauseActive)
    {
        setMinimizedPauseActive(false);
    }
    if (g_androidRendererRestorePending)
    {
        g_androidRendererRestorePending = !restoreRendererTextures(reason);
    }
    g_androidForegroundStablePumps = 0;
    printf("frontend: Android foreground restored by %s\n",
        reason ? reason : "window state");
}

static void reconcileAndroidBackgroundRequest(void)
{
    bool requested = SDL_AtomicGet(&g_androidBackgroundRequested) != 0;
    if (requested)
    {
        if (g_androidBackgroundActive)
        {
            return;
        }
    }
    else
    {
        if (!g_androidBackgroundActive && !g_androidRendererRestorePending)
        {
            return;
        }
        if (!frontendWindowIsForeground())
        {
            g_androidForegroundStablePumps = 0;
            return;
        }
        if (g_androidForegroundStablePumps++ == 0)
        {
            return;
        }
        restoreFrontendFromBackground("deferred Android lifecycle");
        return;
    }

    g_androidBackgroundActive = true;
    g_androidRendererRestorePending = true;
    g_androidForegroundStablePumps = 0;
    invalidateAndroidSaveStateThumbnail();
    releaseFrontendInputControls();
    if (frontendGameRunning() &&
        currentMinimizedBehavior() == MINIMIZED_BEHAVIOR_PAUSE)
    {
        setMinimizedPauseActive(true);
    }
    printf("frontend: Android background entered by lifecycle\n");
}

void frontendSetGamePaused(bool paused)
{
    bool changed = g_userPauseRequested != paused;
    if (changed)
    {
        g_userPauseRequested = paused;
    }
    if (!paused)
    {
        g_minimizedPauseActive = false;
    }
    if (!changed && frontendGamePaused() == frontendEffectivePauseRequested())
    {
        return;
    }
    applyFrontendPauseState();
}

static void resetFrontendPauseRequests(void)
{
    g_userPauseRequested = false;
    g_minimizedPauseActive = false;
}

static void clearFrontendPauseRequests(void)
{
    resetFrontendPauseRequests();
    applyFrontendPauseState();
}

void frontendClearPauseRequests(void)
{
    clearFrontendPauseRequests();
}

struct ControllerPhysicalSource
{
    bool axis;
    int index;
    int direction;
    const char* name;
};

static const Sint16 kControllerInputThreshold = 16000;
static const int kControllerCalibrationAxisCount = 4;
static const uint64_t kControllerCalibrationCenterDurationMs = 1500;
static const uint64_t kControllerCalibrationRangeDurationMs = 5000;

static ControllerAxisCalibration defaultControllerAxisCalibration(void)
{
    return ControllerAxisCalibration{ 0, -32768, 32767, 4096 };
}

static void setDefaultControllerCalibration(void)
{
    for (int axis = 0; axis < SDL_CONTROLLER_AXIS_MAX; ++axis)
    {
        g_controllerAxisCalibration[axis] = defaultControllerAxisCalibration();
    }
}

static void applyControllerCalibrationSettings(const std::string& calibration)
{
    if (g_controllerCalibrationInitialized && calibration == g_appliedControllerCalibration)
    {
        return;
    }

    setDefaultControllerCalibration();
    size_t begin = 0;
    while (begin <= calibration.size())
    {
        size_t separator = calibration.find_first_of(",;\n", begin);
        std::string token = trimString(separator == std::string::npos ?
            calibration.substr(begin) : calibration.substr(begin, separator - begin));
        int axis = -1;
        ControllerAxisCalibration parsed = defaultControllerAxisCalibration();
        if (!token.empty() && sscanf(token.c_str(), "%d:%d:%d:%d:%d",
            &axis, &parsed.center, &parsed.minimum, &parsed.maximum, &parsed.deadZone) == 5 &&
            axis >= 0 && axis < kControllerCalibrationAxisCount &&
            parsed.minimum < parsed.center && parsed.center < parsed.maximum &&
            parsed.deadZone >= 0 && parsed.deadZone < 16000)
        {
            g_controllerAxisCalibration[axis] = parsed;
        }
        if (separator == std::string::npos)
        {
            break;
        }
        begin = separator + 1;
    }

    g_appliedControllerCalibration = calibration;
    g_controllerCalibrationInitialized = true;
}

static Sint16 calibratedControllerAxisValue(int axis, Sint16 rawValue)
{
    if (axis < 0 || axis >= kControllerCalibrationAxisCount)
    {
        return rawValue;
    }
    if (!g_controllerCalibrationInitialized)
    {
        applyControllerCalibrationSettings(
            g_frontendSettings ? g_frontendSettings->controllerCalibration : "");
    }

    const ControllerAxisCalibration& calibration = g_controllerAxisCalibration[axis];
    int delta = (int)rawValue - calibration.center;
    int magnitude = abs(delta);
    if (magnitude <= calibration.deadZone)
    {
        return 0;
    }
    int range = delta < 0 ? calibration.center - calibration.minimum :
        calibration.maximum - calibration.center;
    int usableRange = std::max(1, range - calibration.deadZone);
    int normalized = (magnitude - calibration.deadZone) * 32767 / usableRange;
    normalized = std::min(32767, normalized);
    return (Sint16)(delta < 0 ? -normalized : normalized);
}

static std::string buildControllerCalibrationSpec(void)
{
    std::string spec;
    char token[96];
    for (int axis = 0; axis < kControllerCalibrationAxisCount; ++axis)
    {
        const ControllerAxisCalibration& calibration = g_controllerAxisCalibration[axis];
        snprintf(token, sizeof(token), "%s%d:%d:%d:%d:%d",
            spec.empty() ? "" : ";", axis, calibration.center,
            calibration.minimum, calibration.maximum, calibration.deadZone);
        spec += token;
    }
    return spec;
}

static void cancelControllerCalibration(void)
{
    bool wasActive = g_controllerCalibrationStage != CONTROLLER_CALIBRATION_IDLE;
    g_controllerCalibrationStage = CONTROLLER_CALIBRATION_IDLE;
    g_controllerCalibrationStageStartTicks = 0;
    if (wasActive)
    {
        g_controllerCalibrationInitialized = false;
        applyControllerCalibrationSettings(
            g_frontendSettings ? g_frontendSettings->controllerCalibration : "");
        memset(g_gameControllerAxes, 0, sizeof(g_gameControllerAxes));
        releaseGameControllerControls();
    }
}

void resetControllerCalibration(void)
{
    cancelControllerCalibration();
    setDefaultControllerCalibration();
    g_controllerCalibrationInitialized = true;
    g_appliedControllerCalibration.clear();
    if (g_frontendSettings)
    {
        g_frontendSettings->controllerCalibration.clear();
        saveAndroidSettings();
    }
    memset(g_gameControllerAxes, 0, sizeof(g_gameControllerAxes));
    releaseGameControllerControls();
}

void beginControllerCalibration(void)
{
    openFirstGameController();
    if (!g_gameController)
    {
        showAndroidMessageDialog(
            androidMenuString(ANDROID_TEXT_INPUT_CONTROLLER_CALIBRATION),
            androidMenuString(ANDROID_TEXT_CONTROLLER_MAPPING_NO_DEVICE));
        return;
    }

    releaseGameControllerControls();
    memset(g_controllerCalibrationCenterSums, 0, sizeof(g_controllerCalibrationCenterSums));
    g_controllerCalibrationCenterSamples = 0;
    g_controllerCalibrationStage = CONTROLLER_CALIBRATION_CENTER;
    g_controllerCalibrationStageStartTicks = SDL_GetTicks64();
    clearAndroidSystemTextTextures();
}

static void finishControllerCalibration(void)
{
    for (int axis = 0; axis < kControllerCalibrationAxisCount; ++axis)
    {
        ControllerAxisCalibration& calibration = g_controllerAxisCalibration[axis];
        int negativeRange = calibration.center - g_controllerCalibrationMinimums[axis];
        int positiveRange = g_controllerCalibrationMaximums[axis] - calibration.center;
        if (negativeRange < 12000 || positiveRange < 12000)
        {
            calibration = defaultControllerAxisCalibration();
            continue;
        }
        calibration.minimum = g_controllerCalibrationMinimums[axis];
        calibration.maximum = g_controllerCalibrationMaximums[axis];
        calibration.deadZone = std::min(8000, std::max(2048,
            std::min(negativeRange, positiveRange) / 8));
    }

    g_controllerCalibrationInitialized = true;
    g_controllerCalibrationStage = CONTROLLER_CALIBRATION_IDLE;
    if (g_frontendSettings)
    {
        g_frontendSettings->controllerCalibration = buildControllerCalibrationSpec();
        g_appliedControllerCalibration = g_frontendSettings->controllerCalibration;
        saveAndroidSettings();
    }
    memset(g_gameControllerAxes, 0, sizeof(g_gameControllerAxes));
    clearAndroidSystemTextTextures();
    showAndroidMessageDialog(
        androidMenuString(ANDROID_TEXT_INPUT_CONTROLLER_CALIBRATION),
        androidMenuString(ANDROID_TEXT_CONTROLLER_CALIBRATION_COMPLETE));
}

static void updateControllerCalibration(void)
{
    if (g_controllerCalibrationStage == CONTROLLER_CALIBRATION_IDLE || !g_gameController)
    {
        return;
    }

    Sint16 values[kControllerCalibrationAxisCount];
    for (int axis = 0; axis < kControllerCalibrationAxisCount; ++axis)
    {
        values[axis] = SDL_GameControllerGetAxis(
            g_gameController, (SDL_GameControllerAxis)axis);
    }

    uint64_t now = SDL_GetTicks64();
    if (g_controllerCalibrationStage == CONTROLLER_CALIBRATION_CENTER)
    {
        for (int axis = 0; axis < kControllerCalibrationAxisCount; ++axis)
        {
            g_controllerCalibrationCenterSums[axis] += values[axis];
        }
        ++g_controllerCalibrationCenterSamples;
        if (now - g_controllerCalibrationStageStartTicks >=
            kControllerCalibrationCenterDurationMs)
        {
            for (int axis = 0; axis < kControllerCalibrationAxisCount; ++axis)
            {
                int center = g_controllerCalibrationCenterSamples > 0 ?
                    (int)(g_controllerCalibrationCenterSums[axis] /
                        g_controllerCalibrationCenterSamples) : 0;
                g_controllerAxisCalibration[axis] = defaultControllerAxisCalibration();
                g_controllerAxisCalibration[axis].center = center;
                g_controllerCalibrationMinimums[axis] = (Sint16)center;
                g_controllerCalibrationMaximums[axis] = (Sint16)center;
            }
            g_controllerCalibrationStage = CONTROLLER_CALIBRATION_RANGE;
            g_controllerCalibrationStageStartTicks = now;
            clearAndroidSystemTextTextures();
        }
        return;
    }

    for (int axis = 0; axis < kControllerCalibrationAxisCount; ++axis)
    {
        g_controllerCalibrationMinimums[axis] = std::min(
            g_controllerCalibrationMinimums[axis], values[axis]);
        g_controllerCalibrationMaximums[axis] = std::max(
            g_controllerCalibrationMaximums[axis], values[axis]);
    }
    if (now - g_controllerCalibrationStageStartTicks >= kControllerCalibrationRangeDurationMs)
    {
        finishControllerCalibration();
    }
}

std::string controllerCalibrationStatusText(void)
{
    if (g_controllerCalibrationStage == CONTROLLER_CALIBRATION_CENTER)
    {
        return androidMenuString(ANDROID_TEXT_CONTROLLER_CALIBRATION_CENTER);
    }
    if (g_controllerCalibrationStage == CONTROLLER_CALIBRATION_RANGE)
    {
        return androidMenuString(ANDROID_TEXT_CONTROLLER_CALIBRATION_RANGE);
    }
    return g_frontendSettings && !g_frontendSettings->controllerCalibration.empty() ?
        (androidChineseUi() ? u8"\u5df2\u6821\u51c6" : "Calibrated") :
        (androidChineseUi() ? u8"\u9ed8\u8ba4" : "Default");
}

static const ControllerPhysicalSource kControllerMappingSources[] =
{
    { false, SDL_CONTROLLER_BUTTON_A, 0, "A" },
    { false, SDL_CONTROLLER_BUTTON_B, 0, "B" },
    { false, SDL_CONTROLLER_BUTTON_X, 0, "X" },
    { false, SDL_CONTROLLER_BUTTON_Y, 0, "Y" },
    { false, SDL_CONTROLLER_BUTTON_BACK, 0, "Back" },
    { false, SDL_CONTROLLER_BUTTON_GUIDE, 0, "Guide" },
    { false, SDL_CONTROLLER_BUTTON_START, 0, "Start" },
    { false, SDL_CONTROLLER_BUTTON_LEFTSTICK, 0, "LeftStick" },
    { false, SDL_CONTROLLER_BUTTON_RIGHTSTICK, 0, "RightStick" },
    { false, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 0, "LeftShoulder" },
    { false, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0, "RightShoulder" },
    { false, SDL_CONTROLLER_BUTTON_DPAD_UP, 0, "DPadUp" },
    { false, SDL_CONTROLLER_BUTTON_DPAD_DOWN, 0, "DPadDown" },
    { false, SDL_CONTROLLER_BUTTON_DPAD_LEFT, 0, "DPadLeft" },
    { false, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 0, "DPadRight" },
    { false, SDL_CONTROLLER_BUTTON_MISC1, 0, "Misc1" },
    { false, SDL_CONTROLLER_BUTTON_PADDLE1, 0, "Paddle1" },
    { false, SDL_CONTROLLER_BUTTON_PADDLE2, 0, "Paddle2" },
    { false, SDL_CONTROLLER_BUTTON_PADDLE3, 0, "Paddle3" },
    { false, SDL_CONTROLLER_BUTTON_PADDLE4, 0, "Paddle4" },
    { false, SDL_CONTROLLER_BUTTON_TOUCHPAD, 0, "Touchpad" },
    { true, SDL_CONTROLLER_AXIS_LEFTX, 0, "LeftX-" },
    { true, SDL_CONTROLLER_AXIS_LEFTX, 1, "LeftX+" },
    { true, SDL_CONTROLLER_AXIS_LEFTY, 0, "LeftY-" },
    { true, SDL_CONTROLLER_AXIS_LEFTY, 1, "LeftY+" },
    { true, SDL_CONTROLLER_AXIS_RIGHTX, 0, "RightX-" },
    { true, SDL_CONTROLLER_AXIS_RIGHTX, 1, "RightX+" },
    { true, SDL_CONTROLLER_AXIS_RIGHTY, 0, "RightY-" },
    { true, SDL_CONTROLLER_AXIS_RIGHTY, 1, "RightY+" },
    { true, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1, "LeftTrigger" },
    { true, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1, "RightTrigger" },
};

static std::string trimString(const std::string& text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' ||
        text[begin] == '\r' || text[begin] == '\n'))
    {
        begin++;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
        text[end - 1] == '\r' || text[end - 1] == '\n'))
    {
        end--;
    }
    return text.substr(begin, end - begin);
}

static std::string normalizeMappingName(const std::string& text, bool keepTrailingAxisSign)
{
    std::string out;
    std::string trimmed = trimString(text);
    for (size_t i = 0; i < trimmed.size(); ++i)
    {
        unsigned char ch = (unsigned char)trimmed[i];
        if ((ch == '+' || ch == '-') && keepTrailingAxisSign && i == trimmed.size() - 1)
        {
            out.push_back((char)ch);
            continue;
        }
        if (ch == ' ' || ch == '\t' || ch == '_' || ch == '-' || ch == '+')
        {
            continue;
        }
        out.push_back((char)tolower(ch));
    }
    return out;
}

static bool controllerSourceMatches(const ControllerPhysicalSource& source, bool axis, int index, int direction)
{
    return source.axis == axis && source.index == index && source.direction == direction;
}

static const ControllerPhysicalSource* findControllerSource(bool axis, int index, int direction)
{
    for (size_t i = 0; i < sizeof(kControllerMappingSources) / sizeof(kControllerMappingSources[0]); ++i)
    {
        if (controllerSourceMatches(kControllerMappingSources[i], axis, index, direction))
        {
            return &kControllerMappingSources[i];
        }
    }
    return NULL;
}

static const ControllerPhysicalSource* findControllerSourceByName(const std::string& name)
{
    std::string normalized = normalizeMappingName(name, true);
    for (size_t i = 0; i < sizeof(kControllerMappingSources) / sizeof(kControllerMappingSources[0]); ++i)
    {
        if (normalized == normalizeMappingName(kControllerMappingSources[i].name, true))
        {
            return &kControllerMappingSources[i];
        }
    }
    if (normalized == "triggerleft")
    {
        return findControllerSource(true, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1);
    }
    if (normalized == "triggerright")
    {
        return findControllerSource(true, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1);
    }
    return NULL;
}

static const char* controllerTargetName(uint32_t mask)
{
    if (!mask) return "None";
    if (mask == controlMask(CONTROL_BUTTON_A)) return "A";
    if (mask == controlMask(CONTROL_BUTTON_B)) return "B";
    if (mask == controlMask(CONTROL_BUTTON_X)) return "X";
    if (mask == controlMask(CONTROL_BUTTON_Y)) return "Y";
    if (mask == controlMask(CONTROL_BUTTON_START)) return "Start";
    if (mask == controlMask(CONTROL_BUTTON_SELECT)) return "Select";
    if (mask == controlMask(CONTROL_TRIGGER_LEFT)) return "L";
    if (mask == controlMask(CONTROL_TRIGGER_RIGHT)) return "R";
    if (mask == controlMask(CONTROL_DPAD_UP)) return "Up";
    if (mask == controlMask(CONTROL_DPAD_DOWN)) return "Down";
    if (mask == controlMask(CONTROL_DPAD_LEFT)) return "Left";
    if (mask == controlMask(CONTROL_DPAD_RIGHT)) return "Right";
    if (mask == controlMask(CONTROL_POWER)) return "Power";
    if (mask == kControllerMenuActionMask) return "Menu";
    return "None";
}

static bool parseControllerTargetName(const std::string& name, uint32_t* outMask)
{
    if (!outMask)
    {
        return false;
    }
    std::string normalized = normalizeMappingName(name, false);
    if (normalized.empty() || normalized == "none" || normalized == "off" ||
        normalized == "unmapped" || normalized == "disabled" || normalized == "0")
    {
        *outMask = 0;
        return true;
    }
    if (normalized == "a" || normalized == "buttona") { *outMask = controlMask(CONTROL_BUTTON_A); return true; }
    if (normalized == "b" || normalized == "buttonb") { *outMask = controlMask(CONTROL_BUTTON_B); return true; }
    if (normalized == "x" || normalized == "buttonx") { *outMask = controlMask(CONTROL_BUTTON_X); return true; }
    if (normalized == "y" || normalized == "buttony") { *outMask = controlMask(CONTROL_BUTTON_Y); return true; }
    if (normalized == "start") { *outMask = controlMask(CONTROL_BUTTON_START); return true; }
    if (normalized == "select" || normalized == "back") { *outMask = controlMask(CONTROL_BUTTON_SELECT); return true; }
    if (normalized == "l" || normalized == "leftshoulder" || normalized == "triggerleft" || normalized == "lefttrigger")
    {
        *outMask = controlMask(CONTROL_TRIGGER_LEFT);
        return true;
    }
    if (normalized == "r" || normalized == "rightshoulder" || normalized == "triggerright" || normalized == "righttrigger")
    {
        *outMask = controlMask(CONTROL_TRIGGER_RIGHT);
        return true;
    }
    if (normalized == "up" || normalized == "dpadup") { *outMask = controlMask(CONTROL_DPAD_UP); return true; }
    if (normalized == "down" || normalized == "dpaddown") { *outMask = controlMask(CONTROL_DPAD_DOWN); return true; }
    if (normalized == "left" || normalized == "dpadleft") { *outMask = controlMask(CONTROL_DPAD_LEFT); return true; }
    if (normalized == "right" || normalized == "dpadright") { *outMask = controlMask(CONTROL_DPAD_RIGHT); return true; }
    if (normalized == "power") { *outMask = controlMask(CONTROL_POWER); return true; }
    if (normalized == "menu") { *outMask = kControllerMenuActionMask; return true; }
    return false;
}

static void setDefaultGameControllerMapping(uint32_t buttonMap[SDL_CONTROLLER_BUTTON_MAX],
    uint32_t axisMap[SDL_CONTROLLER_AXIS_MAX][2])
{
    memset(buttonMap, 0, sizeof(uint32_t) * SDL_CONTROLLER_BUTTON_MAX);
    memset(axisMap, 0, sizeof(uint32_t) * SDL_CONTROLLER_AXIS_MAX * 2);

    buttonMap[SDL_CONTROLLER_BUTTON_A] = controlMask(CONTROL_BUTTON_A);
    buttonMap[SDL_CONTROLLER_BUTTON_B] = controlMask(CONTROL_BUTTON_B);
    buttonMap[SDL_CONTROLLER_BUTTON_X] = controlMask(CONTROL_BUTTON_X);
    buttonMap[SDL_CONTROLLER_BUTTON_Y] = controlMask(CONTROL_BUTTON_Y);
    buttonMap[SDL_CONTROLLER_BUTTON_BACK] = controlMask(CONTROL_BUTTON_SELECT);
    buttonMap[SDL_CONTROLLER_BUTTON_START] = controlMask(CONTROL_BUTTON_START);
    buttonMap[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = controlMask(CONTROL_TRIGGER_LEFT);
    buttonMap[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = controlMask(CONTROL_TRIGGER_RIGHT);
    buttonMap[SDL_CONTROLLER_BUTTON_DPAD_UP] = controlMask(CONTROL_DPAD_UP);
    buttonMap[SDL_CONTROLLER_BUTTON_DPAD_DOWN] = controlMask(CONTROL_DPAD_DOWN);
    buttonMap[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = controlMask(CONTROL_DPAD_LEFT);
    buttonMap[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = controlMask(CONTROL_DPAD_RIGHT);

    axisMap[SDL_CONTROLLER_AXIS_LEFTX][0] = controlMask(CONTROL_DPAD_LEFT);
    axisMap[SDL_CONTROLLER_AXIS_LEFTX][1] = controlMask(CONTROL_DPAD_RIGHT);
    axisMap[SDL_CONTROLLER_AXIS_LEFTY][0] = controlMask(CONTROL_DPAD_UP);
    axisMap[SDL_CONTROLLER_AXIS_LEFTY][1] = controlMask(CONTROL_DPAD_DOWN);
    axisMap[SDL_CONTROLLER_AXIS_TRIGGERLEFT][1] = controlMask(CONTROL_TRIGGER_LEFT);
    axisMap[SDL_CONTROLLER_AXIS_TRIGGERRIGHT][1] = controlMask(CONTROL_TRIGGER_RIGHT);
}

static uint32_t controllerSourceMaskFromMaps(const ControllerPhysicalSource& source,
    const uint32_t buttonMap[SDL_CONTROLLER_BUTTON_MAX],
    const uint32_t axisMap[SDL_CONTROLLER_AXIS_MAX][2])
{
    if (source.axis)
    {
        return axisMap[source.index][source.direction];
    }
    return buttonMap[source.index];
}

static uint32_t currentControllerSourceMask(const ControllerPhysicalSource& source)
{
    return controllerSourceMaskFromMaps(source, g_gameControllerButtonMap, g_gameControllerAxisMap);
}

static void setCurrentControllerSourceMask(const ControllerPhysicalSource& source, uint32_t mask)
{
    if (source.axis)
    {
        g_gameControllerAxisMap[source.index][source.direction] = mask;
    }
    else
    {
        g_gameControllerButtonMap[source.index] = mask;
    }
}

static void applyControllerMappingToken(const std::string& token)
{
    std::string trimmed = trimString(token);
    if (trimmed.empty())
    {
        return;
    }

    size_t separator = trimmed.find('=');
    if (separator == std::string::npos)
    {
        separator = trimmed.find(':');
    }
    if (separator == std::string::npos)
    {
        printf("frontend: invalid controller mapping token='%s'\n", trimmed.c_str());
        return;
    }

    std::string sourceName = trimString(trimmed.substr(0, separator));
    std::string targetName = trimString(trimmed.substr(separator + 1));
    const ControllerPhysicalSource* source = findControllerSourceByName(sourceName);
    if (!source)
    {
        printf("frontend: unknown controller mapping source='%s'\n", sourceName.c_str());
        return;
    }

    uint32_t targetMask = 0;
    if (!parseControllerTargetName(targetName, &targetMask))
    {
        printf("frontend: unknown controller mapping target='%s'\n", targetName.c_str());
        return;
    }
    uint32_t sourceMask = currentControllerSourceMask(*source);
    if (targetMask == kControllerMenuActionMask)
    {
        setCurrentControllerSourceMask(*source, sourceMask | targetMask);
    }
    else
    {
        setCurrentControllerSourceMask(*source,
            targetMask | (sourceMask & kControllerMenuActionMask));
    }
}

static void applyGameControllerMappingSettings(const std::string& mapping)
{
    if (g_controllerMappingInitialized && mapping == g_appliedControllerMapping)
    {
        return;
    }

    releaseGameControllerControls();
    setDefaultGameControllerMapping(g_gameControllerButtonMap, g_gameControllerAxisMap);

    size_t begin = 0;
    while (begin <= mapping.size())
    {
        size_t comma = mapping.find_first_of(",;\n", begin);
        std::string token = comma == std::string::npos ?
            mapping.substr(begin) : mapping.substr(begin, comma - begin);
        applyControllerMappingToken(token);
        if (comma == std::string::npos)
        {
            break;
        }
        begin = comma + 1;
    }

    g_appliedControllerMapping = mapping;
    g_controllerMappingInitialized = true;
    printf("frontend: controller mapping applied spec='%s'\n",
        mapping.empty() ? "(default)" : mapping.c_str());
}

static std::string buildCurrentControllerMappingSpec(void)
{
    uint32_t defaultButtonMap[SDL_CONTROLLER_BUTTON_MAX];
    uint32_t defaultAxisMap[SDL_CONTROLLER_AXIS_MAX][2];
    setDefaultGameControllerMapping(defaultButtonMap, defaultAxisMap);

    std::string spec;
    for (size_t i = 0; i < sizeof(kControllerMappingSources) / sizeof(kControllerMappingSources[0]); ++i)
    {
        const ControllerPhysicalSource& source = kControllerMappingSources[i];
        uint32_t current = currentControllerSourceMask(source);
        uint32_t currentGameplay = current & ~kControllerMenuActionMask;
        uint32_t fallback = controllerSourceMaskFromMaps(source, defaultButtonMap, defaultAxisMap);
        if (currentGameplay != fallback)
        {
            if (!spec.empty())
            {
                spec += ",";
            }
            spec += source.name;
            spec += "=";
            spec += controllerTargetName(currentGameplay);
        }
        if (current & kControllerMenuActionMask)
        {
            if (!spec.empty())
            {
                spec += ",";
            }
            spec += source.name;
            spec += "=Menu";
        }
    }
    return spec;
}

std::string frontendControllerSourceForControl(uint32_t controlBit)
{
    if (!g_controllerMappingInitialized)
    {
        applyGameControllerMappingSettings(g_frontendSettings ? g_frontendSettings->controllerMapping : "");
    }
    uint32_t targetMask = controlMask(controlBit);
    std::string out;
    for (size_t i = 0; i < sizeof(kControllerMappingSources) / sizeof(kControllerMappingSources[0]); ++i)
    {
        uint32_t sourceMask = currentControllerSourceMask(kControllerMappingSources[i]);
        bool matches = targetMask == kControllerMenuActionMask ?
            (sourceMask & targetMask) != 0 :
            (sourceMask & ~kControllerMenuActionMask) == targetMask;
        if (!matches)
        {
            continue;
        }
        if (!out.empty())
        {
            out += " / ";
        }
        out += kControllerMappingSources[i].name;
    }
    return out.empty() ? "None" : out;
}

static void removeControllerTargetFromCurrentMapping(uint32_t targetMask)
{
    if (!targetMask)
    {
        return;
    }
    for (size_t i = 0; i < sizeof(kControllerMappingSources) / sizeof(kControllerMappingSources[0]); ++i)
    {
        uint32_t sourceMask = currentControllerSourceMask(kControllerMappingSources[i]);
        bool matches = targetMask == kControllerMenuActionMask ?
            (sourceMask & targetMask) != 0 :
            (sourceMask & ~kControllerMenuActionMask) == targetMask;
        if (matches)
        {
            setCurrentControllerSourceMask(kControllerMappingSources[i],
                sourceMask & (targetMask == kControllerMenuActionMask ?
                    ~kControllerMenuActionMask : kControllerMenuActionMask));
        }
    }
}

static bool finishControllerMapping(const ControllerPhysicalSource& source)
{
    if (!g_controllerMappingPending)
    {
        return false;
    }

    if (!g_controllerMappingInitialized)
    {
        applyGameControllerMappingSettings(g_frontendSettings ? g_frontendSettings->controllerMapping : "");
    }

    uint32_t targetMask = g_controllerMappingTarget;
    releaseGameControllerControls();
    removeControllerTargetFromCurrentMapping(targetMask);
    uint32_t sourceMask = currentControllerSourceMask(source);
    setCurrentControllerSourceMask(source,
        targetMask == kControllerMenuActionMask ?
            sourceMask | targetMask :
            targetMask | (sourceMask & kControllerMenuActionMask));

    std::string nextMapping = buildCurrentControllerMappingSpec();
    g_appliedControllerMapping = nextMapping;
    g_controllerMappingInitialized = true;
    g_controllerMappingPending = false;
    g_controllerMappingTarget = 0;

    if (g_frontendSettings)
    {
        g_frontendSettings->controllerMapping = nextMapping;
        emulatorSaveSettings(*g_frontendSettings);
    }
    clearAndroidSystemTextTextures();

    printf("frontend: controller mapping saved target=%s source=%s spec='%s'\n",
        controllerTargetName(targetMask),
        source.name,
        nextMapping.empty() ? "(default)" : nextMapping.c_str());
    return true;
}

static void cancelControllerMapping(void)
{
    if (!g_controllerMappingPending)
    {
        return;
    }
    printf("frontend: controller mapping cancelled target=%s\n",
        controllerTargetName(g_controllerMappingTarget));
    g_controllerMappingPending = false;
    g_controllerMappingTarget = 0;
    clearAndroidSystemTextTextures();
}

void resetControllerMapping(void)
{
    cancelControllerMapping();
    applyGameControllerMappingSettings("");
    if (g_frontendSettings)
    {
        g_frontendSettings->controllerMapping.clear();
        saveAndroidSettings();
    }
    clearAndroidSystemTextTextures();
}

void frontendBeginControllerMapping(uint32_t controlBit)
{
    uint32_t targetMask = controlMask(controlBit);
    if (!parseControllerTargetName(controllerTargetName(targetMask), &targetMask) || !targetMask)
    {
        printf("frontend: rejected controller mapping target bit=%u\n", (unsigned int)controlBit);
        return;
    }

    openFirstGameController();
    if (!g_gameController)
    {
        cancelControllerMapping();
        printf("frontend: controller mapping unavailable because no SDL GameController is connected\n");
        showAndroidMessageDialog(
            androidMenuString(ANDROID_TEXT_INPUT_CONTROLLER_MAPPING),
            androidMenuString(ANDROID_TEXT_CONTROLLER_MAPPING_NO_DEVICE),
            androidChineseUi() ? u8"\u786e\u5b9a" : "OK");
        return;
    }

    if (!g_controllerMappingInitialized)
    {
        applyGameControllerMappingSettings(g_frontendSettings ? g_frontendSettings->controllerMapping : "");
    }

    releaseGameControllerControls();
    g_controllerMappingPending = true;
    g_controllerMappingTarget = targetMask;
    clearAndroidSystemTextTextures();
    printf("frontend: waiting for controller mapping target=%s\n", controllerTargetName(targetMask));
}

static uint32_t gameControllerButtonControlMask(SDL_GameControllerButton button)
{
    if (button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX)
    {
        return 0;
    }
    if (!g_controllerMappingInitialized)
    {
        applyGameControllerMappingSettings(g_frontendSettings ? g_frontendSettings->controllerMapping : "");
    }
    return g_gameControllerButtonMap[button];
}

static bool gameControllerAxisMenuActionActive(void)
{
    for (int axis = 0; axis < SDL_CONTROLLER_AXIS_MAX; ++axis)
    {
        if (g_gameControllerAxes[axis] <= -kControllerInputThreshold &&
            (g_gameControllerAxisMap[axis][0] & kControllerMenuActionMask))
        {
            return true;
        }
        if (g_gameControllerAxes[axis] >= kControllerInputThreshold &&
            (g_gameControllerAxisMap[axis][1] & kControllerMenuActionMask))
        {
            return true;
        }
    }
    return false;
}

static void updateGameControllerMenuAction(void)
{
    bool active = g_gameControllerMenuButtons != 0 || gameControllerAxisMenuActionActive();
    bool pressed = active && !g_gameControllerMenuActionActive;
    g_gameControllerMenuActionActive = active;
    if (pressed && frontendGameRunning() && g_androidMenuScreen == ANDROID_MENU_NONE)
    {
        openAndroidMenu(ANDROID_MENU_PAUSE);
    }
}

static uint32_t gameControllerAxisControlMask(void)
{
    uint32_t mask = 0;

    if (!g_controllerMappingInitialized)
    {
        applyGameControllerMappingSettings(g_frontendSettings ? g_frontendSettings->controllerMapping : "");
    }

    for (int axis = 0; axis < SDL_CONTROLLER_AXIS_MAX; ++axis)
    {
        if (g_gameControllerAxes[axis] <= -kControllerInputThreshold)
        {
            mask |= g_gameControllerAxisMap[axis][0] & ~kControllerMenuActionMask;
        }
        else if (g_gameControllerAxes[axis] >= kControllerInputThreshold)
        {
            mask |= g_gameControllerAxisMap[axis][1] & ~kControllerMenuActionMask;
        }
    }

    return mask;
}

static void openFirstGameController(void)
{
    if (g_gameController)
    {
        return;
    }

    int joystickCount = SDL_NumJoysticks();
    for (int i = 0; i < joystickCount; ++i)
    {
        if (!SDL_IsGameController(i))
        {
            continue;
        }

        g_gameController = SDL_GameControllerOpen(i);
        if (g_gameController)
        {
            memset(g_gameControllerAxes, 0, sizeof(g_gameControllerAxes));
            const char* name = SDL_GameControllerName(g_gameController);
            printf("frontend: game controller opened index=%d name=%s\n",
                i, name ? name : "(unknown)");
            return;
        }
        printf("frontend: SDL_GameControllerOpen failed index=%d error=%s\n", i, SDL_GetError());
    }
}

static void closeGameController(void)
{
    releaseGameControllerControls();
    if (g_gameController)
    {
        const char* name = SDL_GameControllerName(g_gameController);
        printf("frontend: game controller closed name=%s\n",
            name ? name : "(unknown)");
        SDL_GameControllerClose(g_gameController);
        g_gameController = NULL;
    }
}

static void handleGameControllerDeviceAdded(int deviceIndex)
{
    (void)deviceIndex;
    openFirstGameController();
}

static void handleGameControllerDeviceRemoved(SDL_JoystickID instanceId)
{
    if (activeGameControllerInstanceId() != instanceId)
    {
        return;
    }
    if (g_controllerMappingPending)
    {
        cancelControllerMapping();
    }
    cancelControllerCalibration();
    closeGameController();
    openFirstGameController();
}

static void handleGameControllerButtonEvent(const SDL_ControllerButtonEvent& button)
{
    if (button.which != activeGameControllerInstanceId())
    {
        return;
    }

    if (g_controllerMappingPending)
    {
        if (button.state == SDL_PRESSED)
        {
            const ControllerPhysicalSource* source = findControllerSource(false, button.button, 0);
            if (source)
            {
                finishControllerMapping(*source);
            }
        }
        return;
    }
    if (g_controllerCalibrationStage != CONTROLLER_CALIBRATION_IDLE)
    {
        return;
    }

    uint32_t mappingMask = gameControllerButtonControlMask(
        (SDL_GameControllerButton)button.button);
    if (!mappingMask)
    {
        return;
    }

    uint32_t gameplayMask = mappingMask & ~kControllerMenuActionMask;
    uint32_t buttonMask = g_gameControllerButtonControls;
    if (button.state == SDL_PRESSED)
    {
        buttonMask |= gameplayMask;
        if (mappingMask & kControllerMenuActionMask)
        {
            g_gameControllerMenuButtons |= 1u << button.button;
        }
    }
    else
    {
        buttonMask &= ~gameplayMask;
        g_gameControllerMenuButtons &= ~(1u << button.button);
    }
    applyGameControllerControlMasks(buttonMask, g_gameControllerAxisControls);
    updateGameControllerMenuAction();

    if (inputTraceEnabled())
    {
        printf("frontend: controller button=%u state=%u mask=0x%08X\n",
            (unsigned int)button.button,
            (unsigned int)button.state,
            (unsigned int)(g_gameControllerButtonControls | g_gameControllerAxisControls));
    }
}

static void handleGameControllerAxisEvent(const SDL_ControllerAxisEvent& axis)
{
    if (axis.which != activeGameControllerInstanceId() || axis.axis >= SDL_CONTROLLER_AXIS_MAX)
    {
        return;
    }

    if (g_controllerMappingPending)
    {
        if (axis.value <= -kControllerInputThreshold || axis.value >= kControllerInputThreshold)
        {
            int direction = axis.value < 0 ? 0 : 1;
            const ControllerPhysicalSource* source = findControllerSource(true, axis.axis, direction);
            if (source)
            {
                finishControllerMapping(*source);
            }
        }
        return;
    }
    if (g_controllerCalibrationStage != CONTROLLER_CALIBRATION_IDLE)
    {
        return;
    }

    g_gameControllerAxes[axis.axis] = calibratedControllerAxisValue(axis.axis, axis.value);
    applyGameControllerControlMasks(g_gameControllerButtonControls, gameControllerAxisControlMask());
    updateGameControllerMenuAction();

    if (inputTraceEnabled())
    {
        printf("frontend: controller axis=%u value=%d mask=0x%08X\n",
            (unsigned int)axis.axis,
            (int)axis.value,
            (unsigned int)(g_gameControllerButtonControls | g_gameControllerAxisControls));
    }
}

static void resetFpsOverlayTexture(void)
{
    if (g_fpsOverlayTexture)
    {
        SDL_DestroyTexture(g_fpsOverlayTexture);
        g_fpsOverlayTexture = NULL;
    }
    g_fpsOverlayValue = -1;
    g_fpsOverlayWidth = 0;
    g_fpsOverlayHeight = 0;
    g_fpsOverlayScale = 0;
}

static bool createGameFrameTexture(void)
{
    if (g_frameTexture)
    {
        return true;
    }
    if (!g_renderer)
    {
        return false;
    }

    g_frameTexture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!g_frameTexture)
    {
        printf("frontend: SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

static bool createBlurredBackdropTexture(void)
{
    if (g_blurredBackdropTexture)
    {
        return true;
    }
    if (!g_renderer)
    {
        return false;
    }
    g_blurredBackdropTexture = SDL_CreateTexture(g_renderer,
        SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
        kBlurredBackdropWidth, kBlurredBackdropHeight);
    if (!g_blurredBackdropTexture)
    {
        printf("frontend: blurred backdrop texture creation failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(g_blurredBackdropTexture, SDL_ScaleModeLinear);
    g_blurredBackdropUpdateCounter = 0;
    return true;
}

static void releaseGameVideoResources(void)
{
    resetFpsOverlayTexture();
    if (g_blurredBackdropTexture)
    {
        SDL_DestroyTexture(g_blurredBackdropTexture);
        g_blurredBackdropTexture = NULL;
        g_blurredBackdropUpdateCounter = 0;
    }
    if (g_frameTexture)
    {
        SDL_DestroyTexture(g_frameTexture);
        g_frameTexture = NULL;
    }
    printf("frontend: released game video resources\n");
}

static void putPixelRgba(uint32_t* pixels, int pitchPixels, int width, int height,
    int x, int y, uint32_t color)
{
    if (x >= 0 && y >= 0 && x < width && y < height)
    {
        pixels[y * pitchPixels + x] = color;
    }
}

static void fillRectRgba(uint32_t* pixels, int pitchPixels, int width, int height,
    int x, int y, int w, int h, uint32_t color)
{
    for (int row = 0; row < h; ++row)
    {
        for (int col = 0; col < w; ++col)
        {
            putPixelRgba(pixels, pitchPixels, width, height, x + col, y + row, color);
        }
    }
}

static void drawTextToPixels(uint32_t* pixels, int pitchPixels, int width, int height,
    const char* text, int x, int y, int scale, uint32_t color)
{
    int cursor = x;
    for (const char* p = text; *p; ++p)
    {
        const uint8_t* glyph = glyphForChar(*p);
        if (glyph)
        {
            for (int row = 0; row < 7; ++row)
            {
                for (int col = 0; col < 5; ++col)
                {
                    if (glyph[row] & (1 << (4 - col)))
                    {
                        fillRectRgba(pixels, pitchPixels, width, height,
                            cursor + col * scale, y + row * scale, scale, scale, color);
                    }
                }
            }
        }
        cursor += 6 * scale;
    }
}

static bool rebuildFpsOverlayTexture(int displayedFps)
{
    if (!g_renderer)
    {
        return false;
    }

    resetFpsOverlayTexture();

    char text[16];
    snprintf(text, sizeof(text), "FPS:%d", displayedFps);
    const int scale = androidFpsOverlayScale();
    const int padding = scale;
    const int textLength = (int)strlen(text);
    const int textWidth = textLength > 0 ? (textLength * 6 - 1) * scale : 0;
    const int textHeight = 7 * scale;
    g_fpsOverlayWidth = textWidth + 2 * padding;
    g_fpsOverlayHeight = textHeight + 2 * padding;
    g_fpsOverlayScale = scale;

    g_fpsOverlayTexture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING, g_fpsOverlayWidth, g_fpsOverlayHeight);
    if (!g_fpsOverlayTexture)
    {
        printf("frontend: FPS overlay texture creation failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetTextureBlendMode(g_fpsOverlayTexture, SDL_BLENDMODE_BLEND);

    void* lockedPixels = NULL;
    int pitchBytes = 0;
    if (SDL_LockTexture(g_fpsOverlayTexture, NULL, &lockedPixels, &pitchBytes) != 0)
    {
        printf("frontend: FPS overlay texture lock failed: %s\n", SDL_GetError());
        resetFpsOverlayTexture();
        return false;
    }

    memset(lockedPixels, 0, (size_t)pitchBytes * (size_t)g_fpsOverlayHeight);
    uint32_t* pixels = (uint32_t*)lockedPixels;
    int pitchPixels = pitchBytes / (int)sizeof(uint32_t);
    fillRectRgba(pixels, pitchPixels, g_fpsOverlayWidth, g_fpsOverlayHeight,
        0, 0, g_fpsOverlayWidth, g_fpsOverlayHeight, 0x00000080u);
    drawTextToPixels(pixels, pitchPixels, g_fpsOverlayWidth, g_fpsOverlayHeight,
        text, padding, padding, scale, 0xffffffffu);
    SDL_UnlockTexture(g_fpsOverlayTexture);
    g_fpsOverlayValue = displayedFps;
    return true;
}

static void drawFpsOverlay(int displayedFps)
{
    if (g_frontendSettings && !g_frontendSettings->showFps)
    {
        return;
    }
    if (displayedFps < 0)
    {
        displayedFps = 0;
    }
    const int scale = androidFpsOverlayScale();
    if (!g_fpsOverlayTexture || g_fpsOverlayValue != displayedFps ||
        g_fpsOverlayScale != scale)
    {
        if (!rebuildFpsOverlayTexture(displayedFps))
        {
            return;
        }
    }

    SDL_Rect dst = { 2 * scale, 2 * scale, g_fpsOverlayWidth, g_fpsOverlayHeight };
    if (SDL_RenderCopy(g_renderer, g_fpsOverlayTexture, NULL, &dst) != 0)
    {
        printf("frontend: FPS overlay render failed: %s\n", SDL_GetError());
    }
}

static bool textureLinearSamplingEnabled(const EmulatorSettings& settings)
{
    return settings.antiAliasing != ANTI_ALIASING_OFF;
}

void frontendApplyVideoSettings(const EmulatorSettings& settings)
{
    resetIdleTextures();

    if (g_window)
    {
    }

    if (g_frameTexture)
    {
        SDL_SetTextureScaleMode(g_frameTexture,
            textureLinearSamplingEnabled(settings) ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    }

    printf(
        "frontend: video settings anti_aliasing=%s effect=%s brightness=%d "
        "contrast=%d gamma=%d saturation=%d minimized_behavior=%s "
        "screen_orientation=%s screen_fill=%s portrait=%u show_fps=%u\n",
        emulatorAntiAliasingName(settings.antiAliasing),
        emulatorColorEffectName(settings.colorEffect),
        settings.brightnessPercent,
        settings.contrastPercent,
        settings.gammaPercent,
        settings.saturationPercent,
        emulatorMinimizedBehaviorName(settings.minimizedBehavior),
        emulatorScreenOrientationName(settings.screenOrientationMode),
        emulatorScreenFillName(settings.screenFill),
        settings.portraitMode ? 1u : 0u,
        settings.showFps ? 1u : 0u);
}

void frontendApplyAudioSettings(const EmulatorSettings& settings)
{
    audioOutputSetMasterVolumePercent(settings.audioVolumePercent);
    audioOutputSetBufferSamples(settings.audioBufferSamples);
    audioOutputSetEffect(settings.audioEffect);
    audioOutputSetNoiseReduction(settings.digitalNoiseReduction);
    printf(
        "frontend: audio settings volume=%d buffer_samples=%d effect=%s "
        "digital_noise_reduction=%s audio_disabled=%u\n",
        settings.audioVolumePercent,
        settings.audioBufferSamples,
        emulatorAudioEffectName(settings.audioEffect),
        emulatorDigitalNoiseReductionName(settings.digitalNoiseReduction),
        settings.audioDisabled ? 1u : 0u);
}

void frontendApplyInputSettings(const EmulatorSettings& settings)
{
    inputApplyKeyboardMapping(settings.keyboardMapping);
    applyGameControllerMappingSettings(settings.controllerMapping);
    applyControllerCalibrationSettings(settings.controllerCalibration);
    applyWindowImePolicy(settings.systemImeDisabled);
    printf("frontend: input settings system_ime_disabled=%u virtual_controls=%u virtual_control_scale=%d virtual_dpad_type=%s controller_mapping=%s controller_calibration=%s keyboard_mapping=%s\n",
        settings.systemImeDisabled ? 1u : 0u,
        settings.showVirtualControls ? 1u : 0u,
        settings.virtualControlScalePercent,
        emulatorVirtualDpadTypeName(settings.virtualDpadType),
        settings.controllerMapping.empty() ? "(default)" : settings.controllerMapping.c_str(),
        settings.controllerCalibration.empty() ? "(default)" : settings.controllerCalibration.c_str(),
        settings.keyboardMapping.empty() ? "(default)" : settings.keyboardMapping.c_str());
}

static uint32_t hashFramePixels(const uint16_t* pixels)
{
    const uint32_t* words = (const uint32_t*)pixels;
    const size_t wordCount = (SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t)) / sizeof(uint32_t);
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < wordCount; i += 16)
    {
        hash ^= words[i];
        hash *= 16777619u;
    }
    hash ^= words[wordCount - 1];
    hash *= 16777619u;
    return hash;
}

struct AutoPressPlan
{
    bool enabled;
    uint32_t controlBit;
    uint64_t startDelayMs;
    int count;
    uint64_t periodMs;
    uint64_t holdMs;
};

struct AutoControlSequenceEvent
{
    uint32_t controlBit;
    uint64_t startMs;
    uint64_t holdMs;
};

struct AutoVirtualClickEvent
{
    AutoControlSequenceEvent timing;
    uint64_t downElapsedMs;
    bool downSent;
    bool upSent;
};

static const int kMaxAutoPressSequenceEvents = 64;
static const int kMaxAutoVirtualClickEvents = 64;

static bool parseControlName(const char* text, size_t length, uint32_t* outControlBit)
{
    if (!text || !outControlBit || length == 0)
    {
        return false;
    }

    if (length == 1)
    {
        switch (text[0])
        {
        case 'A':
        case 'a':
            *outControlBit = CONTROL_BUTTON_A;
            return true;
        case 'B':
        case 'b':
            *outControlBit = CONTROL_BUTTON_B;
            return true;
        case 'X':
        case 'x':
            *outControlBit = CONTROL_BUTTON_X;
            return true;
        case 'Y':
        case 'y':
            *outControlBit = CONTROL_BUTTON_Y;
            return true;
        case 'U':
        case 'u':
            *outControlBit = CONTROL_DPAD_UP;
            return true;
        case 'D':
        case 'd':
            *outControlBit = CONTROL_DPAD_DOWN;
            return true;
        case 'L':
        case 'l':
            *outControlBit = CONTROL_DPAD_LEFT;
            return true;
        case 'R':
        case 'r':
            *outControlBit = CONTROL_DPAD_RIGHT;
            return true;
        default:
            break;
        }
    }

    if (length == 5 && strncasecmp(text, "START", length) == 0)
    {
        *outControlBit = CONTROL_BUTTON_START;
        return true;
    }
    if (length == 5 && strncasecmp(text, "ENTER", length) == 0)
    {
        *outControlBit = CONTROL_BUTTON_A;
        return true;
    }
    if (length == 4 && strncasecmp(text, "MENU", length) == 0)
    {
        *outControlBit = CONTROL_BUTTON_X;
        return true;
    }
    if (length == 2 && strncasecmp(text, "AB", length) == 0)
    {
        *outControlBit = CONTROL_BUTTON_B;
        return true;
    }
    if (length == 2 && strncasecmp(text, "EQ", length) == 0)
    {
        *outControlBit = 22;
        return true;
    }
    if (length == 6 && strncasecmp(text, "CAMERA", length) == 0)
    {
        *outControlBit = 30;
        return true;
    }
    if (length == 6 && strncasecmp(text, "SELECT", length) == 0)
    {
        *outControlBit = CONTROL_BUTTON_SELECT;
        return true;
    }
    if (length == 2 && strncasecmp(text, "UP", length) == 0)
    {
        *outControlBit = CONTROL_DPAD_UP;
        return true;
    }
    if (length == 4 && strncasecmp(text, "DOWN", length) == 0)
    {
        *outControlBit = CONTROL_DPAD_DOWN;
        return true;
    }
    if (length == 4 && strncasecmp(text, "LEFT", length) == 0)
    {
        *outControlBit = CONTROL_DPAD_LEFT;
        return true;
    }
    if (length == 5 && strncasecmp(text, "RIGHT", length) == 0)
    {
        *outControlBit = CONTROL_DPAD_RIGHT;
        return true;
    }
    return false;
}

static bool parseUnsignedField(const char* text, uint64_t* out)
{
    if (!text || !text[0] || !out)
    {
        return false;
    }

    char* end = NULL;
    uint64_t value = strtoull(text, &end, 10);
    if (!end || *end)
    {
        return false;
    }
    *out = value;
    return true;
}

static int parseTimedControlSequence(const char* envName, const char* logName,
    AutoControlSequenceEvent* events, int capacity)
{
    if (!envName || !logName || !events || capacity <= 0)
    {
        return 0;
    }

    const char* spec = getenv(envName);
    if (!spec || !spec[0])
    {
        return 0;
    }

    char buffer[2048];
    snprintf(buffer, sizeof(buffer), "%s", spec);

    int count = 0;
    char* token = buffer;
    while (token && *token && count < capacity)
    {
        char* next = strchr(token, ',');
        if (next)
        {
            *next = 0;
            next++;
        }

        while (*token == ' ' || *token == '\t')
        {
            token++;
        }
        size_t tokenLength = strlen(token);
        while (tokenLength > 0 && (token[tokenLength - 1] == ' ' || token[tokenLength - 1] == '\t'))
        {
            token[--tokenLength] = 0;
        }

        char* at = strchr(token, '@');
        char* colon = at ? strchr(at + 1, ':') : NULL;
        if (at && colon)
        {
            *at = 0;
            *colon = 0;
            uint32_t controlBit = 0;
            uint64_t startMs = 0;
            uint64_t holdMs = 0;
            if (parseControlName(token, strlen(token), &controlBit) &&
                parseUnsignedField(at + 1, &startMs) &&
                parseUnsignedField(colon + 1, &holdMs))
            {
                if (holdMs < 20)
                {
                    holdMs = 20;
                }
                if (holdMs > 5000)
                {
                    holdMs = 5000;
                }
                events[count].controlBit = controlBit;
                events[count].startMs = startMs;
                events[count].holdMs = holdMs;
                count++;
            }
            else
            {
                printf("frontend: invalid %s token='%s@%s:%s'\n",
                    logName, token, at + 1, colon + 1);
            }
        }
        else if (tokenLength > 0)
        {
            printf("frontend: invalid %s token='%s'\n", logName, token);
        }

        token = next;
    }

    printf("frontend: %s events=%d spec='%s'\n", logName, count, spec);
    return count;
}

static int parseAutoPressSequence(AutoControlSequenceEvent* events, int capacity)
{
    static int initialized = 0;
    static AutoControlSequenceEvent parsedEvents[kMaxAutoPressSequenceEvents] = {};
    static int parsedCount = 0;

    if (!initialized)
    {
        parsedCount = parseTimedControlSequence(
            "DINGOO_PIE_AUTOPRESS_SEQUENCE",
            "autopress sequence",
            parsedEvents,
            kMaxAutoPressSequenceEvents);
        initialized = 1;
    }

    if (!events || capacity <= 0)
    {
        return parsedCount;
    }
    int copyCount = parsedCount < capacity ? parsedCount : capacity;
    for (int i = 0; i < copyCount; ++i)
    {
        events[i] = parsedEvents[i];
    }
    return copyCount;
}

static int parseAutoVirtualClickSequence(AutoVirtualClickEvent* events, int capacity)
{
    if (!events || capacity <= 0)
    {
        return 0;
    }

    AutoControlSequenceEvent parsedEvents[kMaxAutoVirtualClickEvents] = {};
    int count = parseTimedControlSequence(
        "DINGOO_PIE_AUTOTEST_VIRTUAL_CLICK_SEQUENCE",
        "virtual click sequence",
        parsedEvents,
        capacity < kMaxAutoVirtualClickEvents ? capacity : kMaxAutoVirtualClickEvents);
    for (int i = 0; i < count; ++i)
    {
        events[i].timing = parsedEvents[i];
        events[i].downElapsedMs = 0;
        events[i].downSent = false;
        events[i].upSent = false;
    }
    return count;
}

static void updateAutoPressSequence(uint64_t now, uint64_t startTicks)
{
    AutoControlSequenceEvent events[kMaxAutoPressSequenceEvents];
    int count = parseAutoPressSequence(events, kMaxAutoPressSequenceEvents);
    if (count <= 0)
    {
        return;
    }

    uint64_t elapsed = now - startTicks;
    uint32_t referencedControls = 0;
    uint32_t activeControls = 0;
    for (int i = 0; i < count; ++i)
    {
        uint64_t begin = events[i].startMs;
        uint64_t end = begin + events[i].holdMs;
        bool down = elapsed >= begin && elapsed < end;
        uint32_t mask = 1u << events[i].controlBit;
        referencedControls |= mask;
        if (down)
        {
            activeControls |= mask;
        }
    }

    // A sequence can press the same control more than once. Apply the combined
    // state after evaluating every event so future repeats do not clear the
    // current press before the guest polls it.
    for (uint32_t controlBit = 0; controlBit < 32; ++controlBit)
    {
        uint32_t mask = 1u << controlBit;
        if (referencedControls & mask)
        {
            inputSetSyntheticControl(controlBit, (activeControls & mask) != 0);
        }
    }
}

static bool findVirtualControlClickPoint(uint32_t controlBit, int* outX, int* outY)
{
    if (!outX || !outY)
    {
        return false;
    }

    uint32_t targetMask = virtualControlMask(controlBit);
    VirtualControlButton buttons[kVirtualControlButtonCapacity];
    int count = buildVirtualControls(buttons, kVirtualControlButtonCapacity);
    for (int i = 0; i < count; ++i)
    {
        if (buttons[i].controlMask == targetMask)
        {
            int x = buttons[i].rect.x + buttons[i].rect.w / 2;
            int y = buttons[i].rect.y + buttons[i].rect.h / 2;
            if (portraitModeEnabled() && g_renderer)
            {
                int rendererWidth = 0;
                int rendererHeight = 0;
                SDL_GetRendererOutputSize(g_renderer, &rendererWidth, &rendererHeight);
                if (rendererWidth > 0 && rendererHeight > 0)
                {
                    int rendererX = y;
                    int rendererY = rendererHeight - 1 - x;
                    x = rendererX;
                    y = rendererY;
                }
            }
            *outX = x;
            *outY = y;
            return true;
        }
    }
    return false;
}

static bool dispatchAutoVirtualClick(uint32_t controlBit, bool down)
{
    int x = 0;
    int y = 0;
    if (!findVirtualControlClickPoint(controlBit, &x, &y))
    {
        return false;
    }

    SDL_Event ev = {};
    if (down)
    {
        ev.type = SDL_MOUSEBUTTONDOWN;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.state = SDL_PRESSED;
        ev.button.x = x;
        ev.button.y = y;
    }
    else
    {
        ev.type = SDL_MOUSEBUTTONUP;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.state = SDL_RELEASED;
        ev.button.x = x;
        ev.button.y = y;
    }

    bool handled = handleVirtualControlPointerEvent(ev);
    printf("frontend: autotest virtual click %s control=%u handled=%u x=%d y=%d\n",
        down ? "down" : "up",
        (unsigned int)controlBit,
        handled ? 1u : 0u,
        x,
        y);
    return handled;
}

static void runAutoVirtualClickActions(AutoVirtualClickEvent* events, int count, uint64_t elapsedMs)
{
    if (!events || count <= 0 || frontendGamePaused() || frontendPostRestoreInputBlocked())
    {
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        const AutoControlSequenceEvent& timing = events[i].timing;
        if (!events[i].downSent && elapsedMs >= timing.startMs)
        {
            dispatchAutoVirtualClick(timing.controlBit, true);
            events[i].downElapsedMs = elapsedMs;
            events[i].downSent = true;
        }
        if (events[i].downSent && !events[i].upSent &&
            elapsedMs >= events[i].downElapsedMs + timing.holdMs)
        {
            dispatchAutoVirtualClick(timing.controlBit, false);
            events[i].upSent = true;
        }
    }
}

static bool parseAutoPressPlan(AutoPressPlan* out)
{
    static int initialized = 0;
    static AutoPressPlan plan = {};
    if (!initialized)
    {
        const char* spec = getenv("DINGOO_PIE_AUTOPRESS_KEYS");
        if (spec && spec[0])
        {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "%s", spec);
            char* fields[5] = {};
            int fieldCount = 0;
            char* cursor = buffer;
            while (fieldCount < 5)
            {
                fields[fieldCount++] = cursor;
                char* sep = strchr(cursor, ':');
                if (!sep)
                {
                    break;
                }
                *sep = 0;
                cursor = sep + 1;
            }

            uint32_t controlBit = 0;
            if (fieldCount == 5 && parseControlName(fields[0], strlen(fields[0]), &controlBit))
            {
                plan.enabled = true;
                plan.controlBit = controlBit;
                plan.startDelayMs = strtoull(fields[1], NULL, 10);
                plan.count = atoi(fields[2]);
                plan.periodMs = strtoull(fields[3], NULL, 10);
                plan.holdMs = strtoull(fields[4], NULL, 10);
                if (plan.count < 0)
                {
                    plan.count = 0;
                }
                if (plan.count > 64)
                {
                    plan.count = 64;
                }
                if (plan.periodMs < 100)
                {
                    plan.periodMs = 100;
                }
                if (plan.holdMs < 20)
                {
                    plan.holdMs = 20;
                }
                if (plan.holdMs > plan.periodMs)
                {
                    plan.holdMs = plan.periodMs;
                }
                printf("frontend: autopress key control=%u delay=%llums count=%d period=%llums hold=%llums\n",
                    (unsigned int)plan.controlBit,
                    (unsigned long long)plan.startDelayMs,
                    plan.count,
                    (unsigned long long)plan.periodMs,
                    (unsigned long long)plan.holdMs);
            }
            else
            {
                printf("frontend: invalid DINGOO_PIE_AUTOPRESS_KEYS='%s'\n", spec);
            }
        }
        initialized = 1;
    }

    if (out)
    {
        *out = plan;
    }
    return plan.enabled;
}

static void updateAutoPressPlan(uint64_t now, uint64_t startTicks)
{
    AutoPressPlan plan;
    if (!parseAutoPressPlan(&plan) || plan.count == 0)
    {
        return;
    }

    uint64_t elapsed = now - startTicks;
    if (elapsed < plan.startDelayMs)
    {
        inputSetSyntheticControl(plan.controlBit, false);
        return;
    }

    uint64_t sequence = elapsed - plan.startDelayMs;
    int pressIndex = (int)(sequence / plan.periodMs);
    uint64_t phase = sequence % plan.periodMs;
    bool down = pressIndex < plan.count && phase < plan.holdMs;
    inputSetSyntheticControl(plan.controlBit, down);
}

static int getAutoPressARequest(void)
{
    static const int value = []() {
        const char* text = getenv("DINGOO_PIE_AUTOPRESS_A");
        return std::max(0, std::min(16, text ? atoi(text) : 0));
    }();
    return value;
}

static uint64_t getAutoPressAStartDelayMs(void)
{
    static const int value = []() {
        const char* text = getenv("DINGOO_PIE_AUTOPRESS_A_DELAY_MS");
        return std::max(0, std::min(60000, text ? atoi(text) : 1500));
    }();
    return (uint64_t)value;
}

static uint64_t getAutoPressAPeriodMs(void)
{
    static const int value = []() {
        const char* text = getenv("DINGOO_PIE_AUTOPRESS_A_PERIOD_MS");
        return std::max(100, std::min(10000, text ? atoi(text) : 900));
    }();
    return (uint64_t)value;
}

static uint64_t getAutoPressAHoldMs(void)
{
    static const int value = []() {
        const char* text = getenv("DINGOO_PIE_AUTOPRESS_A_HOLD_MS");
        return std::max(20, std::min(5000, text ? atoi(text) : 180));
    }();
    return (uint64_t)value;
}

static void updateAutoPressA(uint64_t now, uint64_t startTicks)
{
    int requested = getAutoPressARequest();
    if (!requested)
    {
        return;
    }

    uint64_t elapsed = now - startTicks;
    const uint64_t initialDelayMs = getAutoPressAStartDelayMs();
    const uint64_t periodMs = getAutoPressAPeriodMs();
    const uint64_t holdMs = getAutoPressAHoldMs();
    if (elapsed < initialDelayMs)
    {
        inputSetSyntheticControl(CONTROL_BUTTON_A, false);
        return;
    }

    uint64_t sequence = elapsed - initialDelayMs;
    int pressIndex = (int)(sequence / periodMs);
    uint64_t phase = sequence % periodMs;
    bool down = pressIndex < requested && phase < holdMs;
    inputSetSyntheticControl(CONTROL_BUTTON_A, down);
}

static ScreenFillMode currentScreenFill(void)
{
    if (!g_frontendSettings || g_frontendSettings->screenFill < SCREEN_FILL_ASPECT ||
        g_frontendSettings->screenFill >= SCREEN_FILL_COUNT)
    {
        return SCREEN_FILL_ASPECT;
    }
    return g_frontendSettings->screenFill;
}

static bool updateBlurredBackdropTexture(const uint16_t* pixels)
{
    if (!pixels || !createBlurredBackdropTexture())
    {
        return false;
    }
    if ((g_blurredBackdropUpdateCounter++ & 1u) != 0)
    {
        return true;
    }

    static uint16_t downsampledPixels[kBlurredBackdropWidth * kBlurredBackdropHeight];
    static uint16_t blurredPixels[kBlurredBackdropWidth * kBlurredBackdropHeight];
    for (int y = 0; y < kBlurredBackdropHeight; ++y)
    {
        for (int x = 0; x < kBlurredBackdropWidth; ++x)
        {
            uint32_t red = 0;
            uint32_t green = 0;
            uint32_t blue = 0;
            const int sourceX = x * 4;
            const int sourceY = y * 4;
            for (int offsetY = 0; offsetY < 4; ++offsetY)
            {
                const uint16_t* source = pixels +
                    (size_t)(sourceY + offsetY) * SCREEN_WIDTH + sourceX;
                for (int offsetX = 0; offsetX < 4; ++offsetX)
                {
                    const uint16_t pixel = source[offsetX];
                    red += (pixel >> 11) & 0x1fu;
                    green += (pixel >> 5) & 0x3fu;
                    blue += pixel & 0x1fu;
                }
            }
            downsampledPixels[(size_t)y * kBlurredBackdropWidth + x] =
                (uint16_t)(((red / 16u) << 11) |
                    ((green / 16u) << 5) | (blue / 16u));
        }
    }

    memcpy(blurredPixels, downsampledPixels, sizeof(blurredPixels));
    for (int y = 1; y < kBlurredBackdropHeight - 1; ++y)
    {
        for (int x = 1; x < kBlurredBackdropWidth - 1; ++x)
        {
            uint32_t red = 0;
            uint32_t green = 0;
            uint32_t blue = 0;
            for (int offsetY = -1; offsetY <= 1; ++offsetY)
            {
                for (int offsetX = -1; offsetX <= 1; ++offsetX)
                {
                    const uint16_t pixel = downsampledPixels[
                        (size_t)(y + offsetY) * kBlurredBackdropWidth +
                        (size_t)(x + offsetX)];
                    const uint32_t weight =
                        offsetX == 0 && offsetY == 0 ? 4u : 1u;
                    red += ((pixel >> 11) & 0x1fu) * weight;
                    green += ((pixel >> 5) & 0x3fu) * weight;
                    blue += (pixel & 0x1fu) * weight;
                }
            }
            blurredPixels[(size_t)y * kBlurredBackdropWidth + x] =
                (uint16_t)(((red / 12u) << 11) |
                    ((green / 12u) << 5) | (blue / 12u));
        }
    }
    for (size_t index = 0;
        index < kBlurredBackdropWidth * kBlurredBackdropHeight; ++index)
    {
        blurredPixels[index] = frontendBlendRgb565WithBlack(blurredPixels[index], 44);
    }
    if (SDL_UpdateTexture(g_blurredBackdropTexture, NULL, blurredPixels,
        kBlurredBackdropWidth * sizeof(uint16_t)) != 0)
    {
        printf("frontend: blurred backdrop update failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

static bool renderBlurredBackdropEdges(const SDL_Rect& foregroundRect)
{
    if (!g_renderer || !g_blurredBackdropTexture)
    {
        return false;
    }
    int outputWidth = 0;
    int outputHeight = 0;
    SDL_GetRendererOutputSize(g_renderer, &outputWidth, &outputHeight);
    if (outputWidth <= 0 || outputHeight <= 0)
    {
        return false;
    }

    const int sampleWidth = std::max(2, kBlurredBackdropWidth / 24);
    const int sampleHeight = std::max(2, kBlurredBackdropHeight / 24);
    if (foregroundRect.x > 0)
    {
        SDL_Rect leftSource = { 0, 0, sampleWidth, kBlurredBackdropHeight };
        SDL_Rect leftDestination = { 0, 0, foregroundRect.x, outputHeight };
        SDL_Rect rightSource = {
            kBlurredBackdropWidth - sampleWidth, 0,
            sampleWidth, kBlurredBackdropHeight };
        SDL_Rect rightDestination = {
            foregroundRect.x + foregroundRect.w, 0,
            outputWidth - foregroundRect.x - foregroundRect.w, outputHeight };
        if (SDL_RenderCopy(g_renderer, g_blurredBackdropTexture,
                &leftSource, &leftDestination) != 0 ||
            SDL_RenderCopy(g_renderer, g_blurredBackdropTexture,
                &rightSource, &rightDestination) != 0)
        {
            return false;
        }
    }
    if (foregroundRect.y > 0)
    {
        SDL_Rect topSource = { 0, 0, kBlurredBackdropWidth, sampleHeight };
        SDL_Rect topDestination = { 0, 0, outputWidth, foregroundRect.y };
        SDL_Rect bottomSource = {
            0, kBlurredBackdropHeight - sampleHeight,
            kBlurredBackdropWidth, sampleHeight };
        SDL_Rect bottomDestination = {
            0, foregroundRect.y + foregroundRect.h,
            outputWidth, outputHeight - foregroundRect.y - foregroundRect.h };
        if (SDL_RenderCopy(g_renderer, g_blurredBackdropTexture,
                &topSource, &topDestination) != 0 ||
            SDL_RenderCopy(g_renderer, g_blurredBackdropTexture,
                &bottomSource, &bottomDestination) != 0)
        {
            return false;
        }
    }
    return true;
}

bool drawFrame(uint16_t* pixels, int displayedFps)
{
    if (!g_renderer || !g_frameTexture || !pixels)
    {
        return false;
    }

    uint16_t effectPixels[SCREEN_WIDTH * SCREEN_HEIGHT];
    uint16_t antiAliasPixels[SCREEN_WIDTH * SCREEN_HEIGHT];
    const uint16_t* uploadPixels = frontendProcessFramePixels(
        pixels,
        effectPixels,
        antiAliasPixels,
        SCREEN_WIDTH * SCREEN_HEIGHT,
        g_frontendSettings);
    if (SDL_UpdateTexture(g_frameTexture, NULL, uploadPixels, SCREEN_WIDTH * sizeof(uint16_t)) != 0)
    {
        printf("frontend: SDL_UpdateTexture failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    if (SDL_RenderClear(g_renderer) != 0)
    {
        printf("frontend: SDL_RenderClear failed: %s\n", SDL_GetError());
        return false;
    }
    int renderResult = 0;
    ScreenFillMode screenFill = currentScreenFill();
    SDL_Rect gameDestination;
    bool hasGameDestination = portraitModeEnabled() ?
        getPortraitGameDestination(&gameDestination) :
        getLandscapeGameDestination(&gameDestination);
    if (!hasGameDestination)
    {
        return false;
    }
    if (screenFill == SCREEN_FILL_BLURRED_EXTENSION &&
        updateBlurredBackdropTexture(uploadPixels))
    {
        if (!renderBlurredBackdropEdges(gameDestination))
        {
            printf("frontend: blurred backdrop render failed: %s\n", SDL_GetError());
            return false;
        }
    }
    if (screenFill == SCREEN_FILL_STRETCH)
    {
        renderResult = SDL_RenderCopy(g_renderer, g_frameTexture, NULL, NULL);
    }
    else
    {
        renderResult = SDL_RenderCopy(
            g_renderer, g_frameTexture, NULL, &gameDestination);
    }
    if (renderResult != 0)
    {
        printf("frontend: SDL_RenderCopy failed: %s\n", SDL_GetError());
        return false;
    }
    if (pixelGridEffectEnabled())
    {
        drawPixelGridOverlay();
    }
    drawVirtualControlsOverlay();
    drawFpsOverlay(displayedFps);
    drawAndroidMenuOverlay();
    SDL_RenderPresent(g_renderer);
    return true;
}

void updateFb(void)
{
    framebufferRequestUpdate();
}

bool frontendInit(EmulatorSettings* settings, const char* currentGamePath)
{
    SDL_LogSetOutputFunction(frontendSdlLogOutput, NULL);
    g_frontendSettings = settings;
    if (g_frontendSettings)
    {
        androidSetScreenOrientationMode(g_frontendSettings->screenOrientationMode);
    }
    syncAndroidScreenOrientation();
    g_frontendCurrentGamePath = currentGamePath ? currentGamePath : "";
    g_frontendPendingGamePath.clear();
    SDL_AtomicSet(&g_frontendGameLaunchPending, 0);
    SDL_AtomicSet(&g_frontendLoopExitRequested, 0);
    g_androidMenuScreen = g_frontendCurrentGamePath.empty() ? ANDROID_MENU_LIBRARY : ANDROID_MENU_NONE;
    g_androidBackgroundActive = false;
    SDL_AtomicSet(&g_androidBackgroundRequested, 0);
    g_androidRendererRestorePending = false;
    g_androidForegroundStablePumps = 0;
    refreshAndroidGameLibrary();
    SDL_AtomicSet(&g_quitRequested, 0);
    SDL_AtomicSet(&g_gamePaused, 0);
    SDL_AtomicSet(&g_frontendTransitionRequested, 0);
    resetFrontendPauseRequests();
    pauseGateSetPaused(false);
    SDL_SetHint(SDL_HINT_IME_SHOW_UI,
        (!settings || settings->systemImeDisabled) ? "0" : "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_AUDIODRIVER, "android");
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE_PAUSEAUDIO, "0");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0)
    {
        printf("frontend: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    reseedIdleVisuals();
    SDL_DisableScreenSaver();

    if (!settings || settings->systemImeDisabled)
    {
        disableTextComposition();
    }
    else
    {
        enableTextComposition();
    }

    g_window = SDL_CreateWindow("DingooPie", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        displayWidthForSettings(settings),
        displayHeightForSettings(settings),
        SDL_WINDOW_HIDDEN);
    if (!g_window)
    {
        printf("frontend: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    if (!settings || settings->systemImeDisabled)
    {
        applyWindowImePolicy(true);
    }
    SDL_GameControllerEventState(SDL_ENABLE);
    openFirstGameController();

    g_renderer = createFrontendRenderer();
    if (!g_renderer)
    {
        printf("frontend: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }

    if (!createGameFrameTexture())
    {
        return false;
    }
    (void)currentGamePath;

    if (settings)
    {
        frontendApplyVideoSettings(*settings);
        frontendApplyAudioSettings(*settings);
        frontendApplyInputSettings(*settings);
    }

    // Keep startup from exposing the default client background before the first
    // emulated frame or idle screen is ready.
    g_idleAnimationClock.reset();
    drawIdleScreen(g_idleAnimationClock.advance(SDL_GetTicks64()));
    SDL_ShowWindow(g_window);
    drawIdleScreen(g_idleAnimationClock.advance(SDL_GetTicks64()));
    SDL_RaiseWindow(g_window);
    SDL_SetWindowInputFocus(g_window);

    return true;
}

void frontendRequestQuit(void)
{
    printf("frontend: quit requested\n");
    clearFrontendPauseRequests();
    SDL_AtomicSet(&g_quitRequested, 1);
}

void frontendRequestGameExit(void)
{
    printf("frontend: game exit requested; returning to library\n");
    clearFrontendPauseRequests();
    SDL_AtomicSet(&g_frontendGameLaunchPending, 1);
    SDL_AtomicSet(&g_frontendLoopExitRequested, 1);
    SDL_AtomicSet(&g_frontendTransitionRequested, 1);
}

bool frontendGameExitRequested(void)
{
    return SDL_AtomicGet(&g_frontendGameLaunchPending) != 0 &&
        SDL_AtomicGet(&g_frontendLoopExitRequested) != 0;
}

void frontendSetCurrentGamePath(const char* gamePath)
{
    g_frontendCurrentGamePath = gamePath ? gamePath : "";
    if (g_frontendCurrentGamePath != g_androidMenuGameRestartPath)
    {
        g_androidMenuScreenAfterGameRestart = ANDROID_MENU_NONE;
        g_androidMenuGameRestartPath.clear();
    }
    SDL_AtomicSet(&g_frontendLoopExitRequested, 0);
    g_androidMenuScreen = g_frontendCurrentGamePath.empty() ? ANDROID_MENU_LIBRARY : ANDROID_MENU_NONE;
    if (g_androidMenuScreen == ANDROID_MENU_LIBRARY)
    {
        refreshAndroidGameLibrary();
    }
    frontendClearPauseRequests();
}

void frontendPrepareForGameLaunch(void)
{
    releaseFrontendInputControls();
    createGameFrameTexture();
    if (presentBlackTransitionFrame())
    {
        SDL_Delay(16);
    }
    while (framebufferConsumeUpdateRequest() != 0)
    {
    }
    g_idleAnimationClock.reset();
    drawIdleScreen(g_idleAnimationClock.advance(SDL_GetTicks64()));
}

void frontendNotifyGameStarted(const char* gamePath)
{
    const std::string startedGamePath = gamePath ? gamePath : "";
    rememberAndroidGameRun(startedGamePath);
    if (g_androidMenuScreenAfterGameRestart != ANDROID_MENU_NONE &&
        startedGamePath == g_androidMenuGameRestartPath)
    {
        AndroidMenuScreen screen = g_androidMenuScreenAfterGameRestart;
        g_androidMenuScreenAfterGameRestart = ANDROID_MENU_NONE;
        g_androidMenuGameRestartPath.clear();
        openAndroidMenu(screen);
    }
}

bool frontendConsumeGameLaunchRequest(std::string* outPath)
{
    if (SDL_AtomicGet(&g_frontendGameLaunchPending) == 0)
    {
        return false;
    }
    if (outPath)
    {
        *outPath = g_frontendPendingGamePath;
    }
    g_frontendPendingGamePath.clear();
    SDL_AtomicSet(&g_frontendGameLaunchPending, 0);
    SDL_AtomicSet(&g_frontendLoopExitRequested, 0);
    return true;
}

bool frontendQuitRequested(void)
{
    return SDL_AtomicGet(&g_quitRequested) != 0;
}

void frontendShutdown(void)
{
    clearFrontendPauseRequests();
    closeGameController();
    releaseGameVideoResources();
    resetIdleTextures();
    clearAndroidSystemTextTextures();
    if (g_renderer)
    {
        SDL_DestroyRenderer(g_renderer);
        g_renderer = NULL;
    }
    if (g_window)
    {
        applyWindowImePolicy(false);
        SDL_DestroyWindow(g_window);
        g_window = NULL;
    }
    g_frontendSettings = NULL;
    // MuMu can block while SDL_Quit closes an already reused audio device.
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER);
    printf("frontend: shutdown complete\n");
}

void frontendRunLoop(const EmulatorOptions& options)
{
    SDL_Event ev;
    bool running = true;
    uint64_t profileLastTicks = SDL_GetTicks64();
    uint64_t fpsLastTicks = profileLastTicks;
    uint32_t profileLoops = 0;
    uint32_t profileDraws = 0;
    uint32_t presentedFrames = 0;
    uint32_t contentFrames = 0;
    uint32_t lastFrameHash = 0;
    uint64_t startTicks = profileLastTicks;
    int displayedPresentedFps = 0;
    int displayedContentFps = 0;
    bool hasPresentedFrame = false;
    uint64_t displayFpsLimit = parsePositiveEnv("DINGOO_PIE_DISPLAY_FPS", 60, 1, 240);
    uint64_t minPresentIntervalMs = 1000 / displayFpsLimit;
    if (minPresentIntervalMs == 0)
    {
        minPresentIntervalMs = 1;
    }
    AutoVirtualClickEvent autotestVirtualClickEvents[kMaxAutoVirtualClickEvents] = {};
    int autotestVirtualClickCount = parseAutoVirtualClickSequence(
        autotestVirtualClickEvents,
        kMaxAutoVirtualClickEvents);
    uint64_t lastPresentTicks = 0;
    uint64_t lastIdlePresentCounter = 0;
    uint64_t performanceFrequency = SDL_GetPerformanceFrequency();
    uint64_t nextExternalLaunchPollTicks = 0;
    bool pendingFrameRequest = false;
    uint16_t frameCopy[SCREEN_WIDTH * SCREEN_HEIGHT];
    while (running && SDL_AtomicGet(&g_frontendLoopExitRequested) == 0 &&
        !SDL_AtomicGet(&g_quitRequested))
    {
        uint64_t loopNow = SDL_GetTicks64();
        if (loopNow >= nextExternalLaunchPollTicks)
        {
            nextExternalLaunchPollTicks = loopNow + 50;
            std::string externalGamePath = platformConsumeExternalGameLaunchPath();
            if (!externalGamePath.empty())
            {
                printf("frontend: external frontend requested game=%s\n",
                    externalGamePath.c_str());
                requestAndroidGame(externalGamePath);
                continue;
            }
        }
        uint64_t loopElapsed = loopNow - startTicks;
        runAutoVirtualClickActions(autotestVirtualClickEvents,
            autotestVirtualClickCount, loopElapsed);
        updateControllerCalibration();

        bool drewFrame = false;
        while (SDL_PollEvent(&ev))
        {
            if (handleAndroidMenuEvent(ev))
            {
                continue;
            }
            if (!frontendGamePaused() && handleVirtualControlPointerEvent(ev))
            {
                continue;
            }

            if (ev.type == SDL_QUIT)
            {
                printf("frontend: SDL_QUIT event received ignoreQuit=%u\n",
                    options.ignoreQuit ? 1u : 0u);
                char identity[65];
                char lastTask[192];
                char lastHle[192];
                gameRuntimeCopyDiagnostics(identity, sizeof(identity),
                    lastTask, sizeof(lastTask), lastHle, sizeof(lastHle));
                printf("frontend: close context game_identity=%s input=0x%08x last_task=\"%s\" last_hle=\"%s\"\n",
                    identity,
                    inputGetCurrentStatus(),
                    lastTask,
                    lastHle);
                if (!options.ignoreQuit)
                {
                    running = false;
                }
                break;
            }

            switch (ev.type)
            {
            case SDL_KEYDOWN:
                if (!ev.key.repeat && ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
                {
                    if (confirmExitRequested())
                    {
                        frontendRequestQuit();
                    }
                    break;
                }
                if (frontendGamePaused())
                {
                    break;
                }
                if (frontendPostRestoreInputBlocked())
                {
                    break;
                }
                if (!ev.key.repeat)
                {
                    inputHandleHostScancode(ev.key.keysym.scancode, true);
                }
                if (inputTraceEnabled())
                {
                    printf("frontend: keydown key=%s scan=%s repeat=%u focus=%u\n",
                        SDL_GetKeyName(ev.key.keysym.sym),
                        SDL_GetScancodeName(ev.key.keysym.scancode),
                        (unsigned int)ev.key.repeat,
                        (unsigned int)(SDL_GetWindowFlags(g_window) & SDL_WINDOW_INPUT_FOCUS));
                }
                break;
            case SDL_KEYUP:
                if (frontendGamePaused())
                {
                    break;
                }
                if (frontendPostRestoreInputBlocked())
                {
                    break;
                }
                inputHandleHostScancode(ev.key.keysym.scancode, false);
                if (inputTraceEnabled())
                {
                    printf("frontend: keyup key=%s scan=%s focus=%u\n",
                        SDL_GetKeyName(ev.key.keysym.sym),
                        SDL_GetScancodeName(ev.key.keysym.scancode),
                        (unsigned int)(SDL_GetWindowFlags(g_window) & SDL_WINDOW_INPUT_FOCUS));
                }
                break;
            case SDL_CONTROLLERDEVICEADDED:
                handleGameControllerDeviceAdded(ev.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                handleGameControllerDeviceRemoved(ev.cdevice.which);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
                if ((ev.type == SDL_CONTROLLERBUTTONUP || !frontendGamePaused() ||
                        g_controllerMappingPending ||
                        g_controllerCalibrationStage != CONTROLLER_CALIBRATION_IDLE) &&
                    !frontendPostRestoreInputBlocked())
                {
                    handleGameControllerButtonEvent(ev.cbutton);
                }
                break;
            case SDL_CONTROLLERAXISMOTION:
                if ((!frontendGamePaused() || g_controllerMappingPending ||
                        g_controllerCalibrationStage != CONTROLLER_CALIBRATION_IDLE) &&
                    !frontendPostRestoreInputBlocked())
                {
                    handleGameControllerAxisEvent(ev.caxis);
                }
                break;
            case SDL_RENDER_TARGETS_RESET:
                g_androidRendererRestorePending =
                    !restoreRendererTextures("SDL_RENDER_TARGETS_RESET");
                break;
            case SDL_RENDER_DEVICE_RESET:
                g_androidRendererRestorePending =
                    !recreateFrontendRenderer("SDL_RENDER_DEVICE_RESET");
                break;
            case SDL_WINDOWEVENT:
                if (inputTraceEnabled())
                {
                    printf("frontend: window event=%u(%s) data1=%d data2=%d\n",
                        (unsigned int)ev.window.event,
                        windowEventName(ev.window.event),
                        ev.window.data1,
                        ev.window.data2);
                }
                if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
                    ev.window.event == SDL_WINDOWEVENT_MINIMIZED)
                {
                    if (ev.window.event == SDL_WINDOWEVENT_MINIMIZED)
                    {
                        frontendNotifyAndroidBackground(true);
                    }
                    if (inputTraceEnabled())
                    {
                        printf("frontend: window event=%u clearing input\n",
                            (unsigned int)ev.window.event);
                    }
                    releaseFrontendInputControls();
                    if (ev.window.event == SDL_WINDOWEVENT_MINIMIZED &&
                        frontendGameRunning() &&
                        currentMinimizedBehavior() == MINIMIZED_BEHAVIOR_PAUSE)
                    {
                        setMinimizedPauseActive(true);
                    }
                }
                else if (ev.window.event == SDL_WINDOWEVENT_RESIZED ||
                    ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                {
                    if (g_frontendSettings &&
                        androidScreenOrientationMode() == SCREEN_ORIENTATION_AUTO &&
                        ev.window.data1 > 0 && ev.window.data2 > 0)
                    {
                        bool portrait = ev.window.data2 > ev.window.data1;
                        if (g_frontendSettings->portraitMode != portrait)
                        {
                            g_frontendSettings->portraitMode = portrait;
                            frontendApplyVideoSettings(*g_frontendSettings);
                        }
                    }
                    resetIdleTextures();
                }
                else if (ev.window.event == SDL_WINDOWEVENT_RESTORED)
                {
                    frontendNotifyAndroidBackground(false);
                    restoreFrontendFromBackground("SDL_WINDOWEVENT_RESTORED");
                }
                else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                {
                    frontendNotifyAndroidBackground(false);
                    if (g_frontendSettings && g_frontendSettings->systemImeDisabled)
                    {
                        applyWindowImePolicy(true);
                    }
                }
                break;
            default:
                break;
            }
        }
        reconcileAndroidBackgroundRequest();
        if (g_androidBackgroundActive)
        {
            if (SDL_AtomicGet(&g_androidBackgroundRequested) == 0 &&
                frontendWindowIsForeground())
            {
                restoreFrontendFromBackground("window flag reconciliation");
            }
        }
        if (g_androidBackgroundActive)
        {
            SDL_Delay(kMinimizedThrottleLoopDelayMs);
            continue;
        }
        bool transitionPresented = false;
        if (SDL_AtomicGet(&g_frontendTransitionRequested) != 0)
        {
            SDL_AtomicSet(&g_frontendTransitionRequested, 0);
            bool blackFramePresented = presentBlackTransitionFrame();
            g_androidMenuScreen = ANDROID_MENU_LIBRARY;
            transitionPresented = blackFramePresented;
            if (transitionPresented)
            {
                hasPresentedFrame = true;
                lastPresentTicks = SDL_GetTicks64();
                lastIdlePresentCounter = SDL_GetPerformanceCounter();
            }
        }
        if (transitionPresented)
        {
            SDL_Delay(1);
            continue;
        }

        if (frontendGamePaused()
            && g_androidMenuScreen == ANDROID_MENU_NONE
            )
        {
            g_idleAnimationClock.pause();
            SDL_Delay(1);
            continue;
        }

        // Throttle mode keeps the guest running, but lowers frontend polling and
        // presentation cadence while the SDL window is minimized.
        bool minimizedThrottle = isWindowMinimized() &&
            currentMinimizedBehavior() == MINIMIZED_BEHAVIOR_THROTTLE;

        updateVirtualPointerReleaseTimer();
        inputPollKeyboardState();
        if (framebufferConsumeUpdateRequest() != 0)
        {
            pendingFrameRequest = true;
        }
        uint64_t now = SDL_GetTicks64();
        updateAutoPressA(now, startTicks);
        updateAutoPressPlan(now, startTicks);
        updateAutoPressSequence(now, startTicks);
        uint64_t activePresentIntervalMs = minimizedThrottle ?
            kMinimizedThrottlePresentIntervalMs : minPresentIntervalMs;
        bool gameRunning = frontendGameRunning();
        if (gameRunning)
        {
            g_idleAnimationClock.pause();
        }
        uint64_t nowCounter = SDL_GetPerformanceCounter();
        uint64_t idleIntervalUs = idlePresentIntervalUs(activePresentIntervalMs);
        bool idlePresentDue = !hasPresentedFrame || !lastIdlePresentCounter ||
            !performanceFrequency ||
            counterToUs(nowCounter - lastIdlePresentCounter, performanceFrequency) >= idleIntervalUs;
        uint64_t presentIntervalMs = activePresentIntervalMs;
        bool gamePresentDue = !hasPresentedFrame || !lastPresentTicks ||
            now - lastPresentTicks >= presentIntervalMs;
        if (!gameRunning && idlePresentDue)
        {
            if (drawIdleScreen(g_idleAnimationClock.advance(now)))
            {
                drewFrame = true;
                hasPresentedFrame = true;
                lastPresentTicks = now;
                lastIdlePresentCounter = nowCounter;
                if (runtimeLogProfileEnabled())
                {
                    profileDraws++;
                }
                presentedFrames++;
            }
        }
        else if (gameRunning && (
            (g_androidMenuScreen != ANDROID_MENU_NONE && gamePresentDue) ||
            (pendingFrameRequest && gamePresentDue)))
        {
            framebufferCopyPresented(frameCopy, sizeof(frameCopy));
            uint32_t frameHash = hashFramePixels(frameCopy);
            bool contentChanged = !hasPresentedFrame || frameHash != lastFrameHash;
            if (contentChanged)
            {
                lastFrameHash = frameHash;
            }

            if (drawFrame(frameCopy, displayedPresentedFps))
            {
                drewFrame = true;
                hasPresentedFrame = true;
                lastPresentTicks = now;
                pendingFrameRequest = false;
                if (runtimeLogProfileEnabled())
                {
                    profileDraws++;
                }
                presentedFrames++;
                if (contentChanged)
                {
                    contentFrames++;
                }
            }
        }

        now = SDL_GetTicks64();
        if (now - fpsLastTicks >= 1000)
        {
            displayedPresentedFps = (int)((presentedFrames * 1000u) / (uint32_t)(now - fpsLastTicks));
            displayedContentFps = (int)((contentFrames * 1000u) / (uint32_t)(now - fpsLastTicks));
            presentedFrames = 0;
            contentFrames = 0;
            fpsLastTicks = now;
        }

        if (runtimeLogProfileEnabled())
        {
            profileLoops++;
            uint64_t profileElapsed = now - profileLastTicks;
            if (profileElapsed >= runtimeLogProfileIntervalMs())
            {
                bool hasFrontendActivity = profileDraws || displayedPresentedFps || displayedContentFps;
                if (hasFrontendActivity ||
                    runtimeLogShouldPrintEmptyProfile())
                {
                    uint32_t loopsPerSecond = (uint32_t)((profileLoops * 1000ull) / profileElapsed);
                    uint32_t drawsPerSecond = (uint32_t)((profileDraws * 1000ull) / profileElapsed);
                    printf("profile:frontend loops=%u/s draws=%u/s presented_fps=%d submitted_fps=%d content_fps=%d\n",
                        loopsPerSecond, drawsPerSecond, displayedPresentedFps, displayedPresentedFps, displayedContentFps);
                }
                profileLoops = 0;
                profileDraws = 0;
                profileLastTicks = now;
            }
        }
        else
        {
            profileLoops = 0;
            profileDraws = 0;
            profileLastTicks = now;
        }

        if (minimizedThrottle)
        {
            SDL_Delay(kMinimizedThrottleLoopDelayMs);
        }
        else if (!drewFrame)
        {
            SDL_Delay(gameRunning ? 1 :
                idleLoopDelayMs(nowCounter, lastIdlePresentCounter, idleIntervalUs, performanceFrequency));
        }
    }

    printf("frontend: run loop exited running=%u quit=%u\n",
        running ? 1u : 0u, (unsigned int)SDL_AtomicGet(&g_quitRequested));
    resetFpsOverlayTexture();
}
