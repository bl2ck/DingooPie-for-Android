#include "frontend/menu/menu_overlay_internal.h"

#include "config/cheats/cheat_runtime.h"
#include "frontend/input/input_controls.h"
#include "frontend/frontend_shell.h"
#include "frontend/video/framebuffer.h"
#include "shared/game/game_runtime.h"
#include "shared/platform/storage_services.h"

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

struct AndroidMenuRowContent
{
    std::string label;
    std::string value;
};

static const char kAndroidAuthorHomepageUrl[] = "https://github.com/bl2ck";
static const char kAndroidProjectHomepageUrl[] = "https://github.com/bl2ck/DingooPie";

template <size_t Count>
static int nextAndroidIntPreset(int current, const int (&values)[Count], int fallback)
{
    for (size_t index = 0; index < Count; ++index)
    {
        if (values[index] == current)
        {
            return values[(index + 1) % Count];
        }
    }
    return fallback;
}

static int nextAndroidEnumValue(int current, int count, int fallback)
{
    if (current < 0 || current >= count)
    {
        current = fallback;
    }
    return (current + 1) % count;
}

template <size_t Count>
static std::string nextAndroidStringPreset(const std::string& current,
    const char* const (&values)[Count])
{
    for (size_t index = 0; index < Count; ++index)
    {
        if (current == values[index])
        {
            return values[(index + 1) % Count];
        }
    }
    return values[0];
}

template <size_t Count>
static int androidNormalizedIntPreset(int value, const int (&values)[Count], int fallback)
{
    for (size_t index = 0; index < Count; ++index)
    {
        if (value == values[index])
        {
            return value;
        }
    }
    return fallback;
}

template <size_t Count>
static std::string androidNormalizedStringPreset(
    const std::string& value, const char* const (&values)[Count])
{
    for (size_t index = 0; index < Count; ++index)
    {
        if (value == values[index])
        {
            return value;
        }
    }
    return "";
}

std::string androidMenuString(AndroidMenuTextId id)
{
    UiLanguage language = g_frontendSettings ?
        g_frontendSettings->uiLanguage : UI_LANGUAGE_CHINESE;
    if (language < UI_LANGUAGE_CHINESE || language >= UI_LANGUAGE_COUNT)
    {
        language = UI_LANGUAGE_CHINESE;
    }
    return platformWideToUtf8(androidMenuText(language, id));
}

static std::string androidBooleanValue(bool enabled)
{
    return androidChineseUi() ?
        (enabled ? u8"\u5f00" : u8"\u5173") :
        (enabled ? "On" : "Off");
}

static std::string androidPercentValue(int percent)
{
    char text[24] = {};
    snprintf(text, sizeof(text), "%d%%", percent);
    return text;
}

static std::string androidScaleValue(const std::string& value)
{
    std::string normalized = androidNormalizedStringPreset(value, EMULATOR_SCALE_VALUES);
    return normalized.empty() ? androidMenuString(ANDROID_TEXT_SETTINGS_AUTO) :
        androidPercentValue((int)(strtod(normalized.c_str(), NULL) * 100.0 + 0.5));
}

static std::string androidAudioBufferValue(int samples)
{
    char text[32] = {};
    snprintf(text, sizeof(text), androidChineseUi() ? u8"%d \u91c7\u6837" : "%d samples", samples);
    return text;
}

static AndroidMenuTextId androidAudioBufferLatencyTextId(AudioBufferLatencyMode mode)
{
    switch (mode)
    {
    case AUDIO_BUFFER_LATENCY_110MS: return ANDROID_TEXT_AUDIO_BUFFER_LATENCY_110MS;
    case AUDIO_BUFFER_LATENCY_120MS: return ANDROID_TEXT_AUDIO_BUFFER_LATENCY_120MS;
    case AUDIO_BUFFER_LATENCY_130MS: return ANDROID_TEXT_AUDIO_BUFFER_LATENCY_130MS;
    case AUDIO_BUFFER_LATENCY_140MS: return ANDROID_TEXT_AUDIO_BUFFER_LATENCY_140MS;
    case AUDIO_BUFFER_LATENCY_150MS: return ANDROID_TEXT_AUDIO_BUFFER_LATENCY_150MS;
    case AUDIO_BUFFER_LATENCY_AUTO:
    default: return ANDROID_TEXT_AUDIO_BUFFER_LATENCY_AUTO;
    }
}

static std::string androidExecutionModeValue(RuntimeExecutionMode mode)
{
    if (mode == RUNTIME_EXECUTION_MODE_COMPATIBILITY)
        return androidMenuString(ANDROID_TEXT_SETTINGS_EXECUTION_MODE_COMPATIBILITY);
    return androidMenuString(ANDROID_TEXT_SETTINGS_EXECUTION_MODE_AUTO);
}

static std::string androidCpuClockValue(const std::string& clockHz)
{
    std::string normalized = androidNormalizedStringPreset(clockHz, EMULATOR_CPU_CLOCK_VALUES);
    if (normalized.empty()) return androidMenuString(ANDROID_TEXT_SETTINGS_AUTO);
    char text[32] = {};
    snprintf(text, sizeof(text), "%d MHz", atoi(normalized.c_str()) / 1000000);
    return text;
}

static const uint32_t kAndroidControllerMappingControls[] =
{
    kControllerMenuActionBit,
    CONTROL_BUTTON_A,
    CONTROL_BUTTON_B,
    CONTROL_BUTTON_X,
    CONTROL_BUTTON_Y,
    CONTROL_BUTTON_START,
    CONTROL_BUTTON_SELECT,
    CONTROL_TRIGGER_LEFT,
    CONTROL_TRIGGER_RIGHT,
    CONTROL_DPAD_UP,
    CONTROL_DPAD_DOWN,
    CONTROL_DPAD_LEFT,
    CONTROL_DPAD_RIGHT
};

static_assert(sizeof(kAndroidControllerMappingControls) /
    sizeof(kAndroidControllerMappingControls[0]) == ANDROID_CONTROLLER_MAPPING_RESET,
    "Controller mapping controls must match the visible mapping rows");

static bool androidControllerMappingControlForRow(int row, uint32_t* outControlBit)
{
    if (!outControlBit || row < 0 ||
        row >= (int)(sizeof(kAndroidControllerMappingControls) /
            sizeof(kAndroidControllerMappingControls[0])))
    {
        return false;
    }
    *outControlBit = kAndroidControllerMappingControls[row];
    return true;
}

static std::string androidControllerMappingControlLabel(uint32_t controlBit)
{
    bool chinese = androidChineseUi();
    switch (controlBit)
    {
    case CONTROL_BUTTON_A: return "A";
    case CONTROL_BUTTON_B: return "B";
    case CONTROL_BUTTON_X: return "X";
    case CONTROL_BUTTON_Y: return "Y";
    case CONTROL_BUTTON_START: return "START";
    case CONTROL_BUTTON_SELECT: return "SELECT";
    case CONTROL_TRIGGER_LEFT: return chinese ? u8"\u5de6\u80a9\u952e" : "Left Shoulder";
    case CONTROL_TRIGGER_RIGHT: return chinese ? u8"\u53f3\u80a9\u952e" : "Right Shoulder";
    case CONTROL_DPAD_UP: return chinese ? u8"\u65b9\u5411\u952e\u4e0a" : "D-pad Up";
    case CONTROL_DPAD_DOWN: return chinese ? u8"\u65b9\u5411\u952e\u4e0b" : "D-pad Down";
    case CONTROL_DPAD_LEFT: return chinese ? u8"\u65b9\u5411\u952e\u5de6" : "D-pad Left";
    case CONTROL_DPAD_RIGHT: return chinese ? u8"\u65b9\u5411\u952e\u53f3" : "D-pad Right";
    case kControllerMenuActionBit: return chinese ? u8"\u83dc\u5355" : "Menu";
    default: return "";
    }
}

static std::string androidCheatManagerDisplayName(const CheatRuntimeEntryView& entry)
{
    if (!entry.nameChinese.empty() && !entry.nameEnglish.empty() &&
        entry.nameChinese != entry.nameEnglish)
    {
        return entry.nameChinese + " / " + entry.nameEnglish;
    }
    if (!entry.name.empty()) return entry.name;
    if (!entry.nameChinese.empty()) return entry.nameChinese;
    if (!entry.nameEnglish.empty()) return entry.nameEnglish;
    return androidMenuString(ANDROID_TEXT_CHEAT_MANAGER_UNNAMED);
}

static std::vector<std::string> androidCheatManagerEnabledFeatureKeys(
    const CheatRuntimeStatus& status)
{
    std::vector<std::string> keys;
    for (size_t index = 0; index < status.entries.size(); ++index)
    {
        if (status.entries[index].enabled && !status.entries[index].name.empty())
        {
            keys.push_back(status.entries[index].name);
        }
    }
    return keys;
}

static const std::string& androidCheatManagerGamePath(void)
{
    return g_frontendCurrentGamePath.empty() ?
        g_androidCheatManagerGamePath : g_frontendCurrentGamePath;
}

static bool androidGamePathHasCheatFile(const std::string& gamePath)
{
    std::vector<std::string> fileNames = gameCheatFileNamesFromPath(gamePath);
    for (size_t i = 0; i < fileNames.size(); ++i)
    {
        FILE* file = platformOpenGameSiblingFile(gamePath, fileNames[i]);
        if (file)
        {
            fclose(file);
            return true;
        }
    }
    return false;
}

static void loadAndroidCheatManagerGamePath(const std::string& gamePath)
{
    g_androidCheatManagerGamePath = gamePath;
    if (g_frontendSettings && !gamePath.empty())
    {
        cheatRuntimeLoadForConfiguration(gamePath.c_str(),
            emulatorCheatFeatureKeysForGame(*g_frontendSettings, gamePath));
    }
}

void prepareAndroidCheatManagerGamePath(void)
{
    if (!g_frontendCurrentGamePath.empty())
    {
        g_androidCheatManagerGamePath = g_frontendCurrentGamePath;
        return;
    }
    if (!g_androidCheatManagerGamePath.empty() &&
        std::find(g_androidGamePaths.begin(), g_androidGamePaths.end(),
            g_androidCheatManagerGamePath) != g_androidGamePaths.end() &&
        androidGamePathHasCheatFile(g_androidCheatManagerGamePath))
    {
        loadAndroidCheatManagerGamePath(g_androidCheatManagerGamePath);
        return;
    }
    g_androidCheatManagerGamePath.clear();
    for (size_t index = 0; index < g_androidGamePaths.size(); ++index)
    {
        if (androidGamePathHasCheatFile(g_androidGamePaths[index]))
        {
            loadAndroidCheatManagerGamePath(g_androidGamePaths[index]);
            return;
        }
    }
    cheatRuntimeLoadForConfiguration(NULL, std::vector<std::string>());
}

bool selectNextAndroidCheatManagerGamePath(void)
{
    if (!g_frontendCurrentGamePath.empty() || g_androidGamePaths.empty())
    {
        return false;
    }
    size_t start = 0;
    std::vector<std::string>::const_iterator current = std::find(
        g_androidGamePaths.begin(), g_androidGamePaths.end(),
        g_androidCheatManagerGamePath);
    if (current != g_androidGamePaths.end())
    {
        start = ((size_t)(current - g_androidGamePaths.begin()) + 1) %
            g_androidGamePaths.size();
    }
    for (size_t offset = 0; offset < g_androidGamePaths.size(); ++offset)
    {
        size_t index = (start + offset) % g_androidGamePaths.size();
        if (androidGamePathHasCheatFile(g_androidGamePaths[index]))
        {
            loadAndroidCheatManagerGamePath(g_androidGamePaths[index]);
            g_androidMenuScrollOffset = 0;
            return true;
        }
    }
    return false;
}

static bool saveAndroidCheatManagerSelection(const CheatRuntimeStatus& status)
{
    const std::string& gamePath = androidCheatManagerGamePath();
    if (!g_frontendSettings || gamePath.empty())
    {
        return false;
    }
    if (emulatorSetCheatFeatureKeysForGame(g_frontendSettings,
        gamePath, androidCheatManagerEnabledFeatureKeys(status)))
    {
        return emulatorSaveSettings(*g_frontendSettings);
    }
    return true;
}

bool setAndroidCheatManagerGlobalEnabled(bool enabled)
{
    if (!g_frontendSettings)
    {
        return false;
    }
    CheatRuntimeStatus status = cheatRuntimeGetStatus();
    if (enabled && !status.available)
    {
        enabled = false;
    }
    bool changed = g_frontendSettings->cheatsEnabled != enabled;
    g_frontendSettings->cheatsEnabled = enabled;
    bool saved = !changed || emulatorSaveSettings(*g_frontendSettings);
    cheatRuntimeSetEnabled(enabled);
    if (enabled)
    {
        cheatRuntimeApplyNow();
    }
    return saved;
}

bool setAndroidCheatManagerFeatureEnabled(size_t index, bool enabled)
{
    if (!cheatRuntimeSetEntryEnabled(index, enabled))
    {
        return false;
    }
    CheatRuntimeStatus updated = cheatRuntimeGetStatus();
    bool saved = saveAndroidCheatManagerSelection(updated);
    if (enabled && g_frontendSettings && g_frontendSettings->cheatsEnabled)
    {
        cheatRuntimeApplyNow();
    }
    return saved;
}

bool setAllAndroidCheatManagerFeaturesEnabled(bool enabled)
{
    CheatRuntimeStatus status = cheatRuntimeGetStatus();
    if (!status.available)
    {
        return false;
    }
    for (size_t index = 0; index < status.entries.size(); ++index)
    {
        if (!cheatRuntimeSetEntryEnabled(index, enabled))
        {
            return false;
        }
    }
    CheatRuntimeStatus updated = cheatRuntimeGetStatus();
    bool saved = saveAndroidCheatManagerSelection(updated);
    if (enabled && g_frontendSettings && g_frontendSettings->cheatsEnabled)
    {
        cheatRuntimeApplyNow();
    }
    return saved;
}

static int androidCheatManagerActionFirstRow(const CheatRuntimeStatus& status)
{
    return ANDROID_CHEAT_MANAGER_FEATURE_FIRST + (int)status.entries.size();
}

static std::string androidCheatManagerStatusValue(const CheatRuntimeStatus& status)
{
    if (androidCheatManagerGamePath().empty())
        return androidMenuString(ANDROID_TEXT_CHEAT_MANAGER_NO_GAME);
    if (!status.loaded)
        return androidMenuString(ANDROID_TEXT_CHEAT_MANAGER_NO_FILE);
    if (status.shaMismatch)
        return androidMenuString(ANDROID_TEXT_CHEAT_MANAGER_MISMATCH);

    unsigned int enabledCount = 0;
    for (size_t index = 0; index < status.entries.size(); ++index)
    {
        if (status.entries[index].enabled) enabledCount++;
    }
    char text[48] = {};
    snprintf(text, sizeof(text), androidChineseUi() ?
        u8"%u \u9879\uff0c\u5df2\u542f\u7528 %u \u9879" : "%u total, %u enabled",
        (unsigned int)status.entries.size(), enabledCount);
    return text;
}

static AndroidMenuRowContent androidMenuRowContent(int row)
{
    if (!g_frontendSettings) return AndroidMenuRowContent{};
    bool chinese = androidChineseUi();
    if (g_androidMenuScreen == ANDROID_MENU_MAIN)
    {
        if (row == ANDROID_MAIN_OPTIONS) return { androidMenuString(ANDROID_TEXT_ROOT_OPTIONS), "" };
        if (row == ANDROID_MAIN_SETTINGS) return { androidMenuString(ANDROID_TEXT_ROOT_SETTINGS), "" };
        if (row == ANDROID_MAIN_ABOUT) return { androidMenuString(ANDROID_TEXT_HELP_ABOUT), "" };
        if (row == ANDROID_MAIN_EXIT_APPLICATION) return { chinese ? kZhExitApp : "Exit App", "" };
        if (row == ANDROID_MAIN_BACK) return { chinese ? kZhBack : "Back", "" };
    }
    else if (g_androidMenuScreen == ANDROID_MENU_OPTIONS)
    {
        if (row == ANDROID_OPTIONS_VIDEO) return { androidMenuString(ANDROID_TEXT_ROOT_VIDEO), "" };
        if (row == ANDROID_OPTIONS_AUDIO) return { androidMenuString(ANDROID_TEXT_ROOT_AUDIO), "" };
        if (row == ANDROID_OPTIONS_INPUT) return { androidMenuString(ANDROID_TEXT_ROOT_INPUT), "" };
        if (row == ANDROID_OPTIONS_RESTORE_DEFAULTS)
            return { androidMenuString(ANDROID_TEXT_SETTINGS_RESET), "" };
        if (row == ANDROID_OPTIONS_BACK) return { chinese ? kZhBack : "Back", "" };
    }
    else if (g_androidMenuScreen == ANDROID_MENU_VIDEO)
    {
        static const AndroidMenuTextId effectTextIds[] =
        {
            ANDROID_TEXT_VIDEO_EFFECT_NORMAL, ANDROID_TEXT_VIDEO_EFFECT_GRAYSCALE, ANDROID_TEXT_VIDEO_EFFECT_INVERT,
            ANDROID_TEXT_VIDEO_EFFECT_SOFT_BLUR, ANDROID_TEXT_VIDEO_EFFECT_SHARPEN, ANDROID_TEXT_VIDEO_EFFECT_VIVID,
            ANDROID_TEXT_VIDEO_EFFECT_SEPIA, ANDROID_TEXT_VIDEO_EFFECT_PIXEL_GRID,
            ANDROID_TEXT_VIDEO_EFFECT_LCD_SCANLINE, ANDROID_TEXT_VIDEO_EFFECT_LIGHT_CRT
        };
        static_assert(sizeof(effectTextIds) / sizeof(effectTextIds[0]) == COLOR_EFFECT_MODE_COUNT,
            "Color effect menu text must match ColorEffectMode");
        if (row == ANDROID_VIDEO_ANTI_ALIASING)
        {
            AndroidMenuTextId valueId = g_frontendSettings->antiAliasing == ANTI_ALIASING_LOW ?
                ANDROID_TEXT_VIDEO_AA_LOW : g_frontendSettings->antiAliasing == ANTI_ALIASING_CLEAR ?
                ANDROID_TEXT_VIDEO_AA_CLEAR : ANDROID_TEXT_VIDEO_AA_OFF;
            return { androidMenuString(ANDROID_TEXT_VIDEO_ANTI_ALIASING), androidMenuString(valueId) };
        }
        if (row == ANDROID_VIDEO_COLOR_EFFECT)
        {
            int index = (int)g_frontendSettings->colorEffect;
            if (index < 0 || index >= COLOR_EFFECT_MODE_COUNT) index = 0;
            return { androidMenuString(ANDROID_TEXT_VIDEO_EFFECT), androidMenuString(effectTextIds[index]) };
        }
        if (row == ANDROID_VIDEO_BRIGHTNESS)
            return { androidMenuString(ANDROID_TEXT_VIDEO_BRIGHTNESS), androidPercentValue(androidNormalizedIntPreset(
                g_frontendSettings->brightnessPercent, EMULATOR_VIDEO_PERCENT_VALUES, 100)) };
        if (row == ANDROID_VIDEO_CONTRAST)
            return { androidMenuString(ANDROID_TEXT_VIDEO_CONTRAST), androidPercentValue(androidNormalizedIntPreset(
                g_frontendSettings->contrastPercent, EMULATOR_VIDEO_PERCENT_VALUES, 100)) };
        if (row == ANDROID_VIDEO_GAMMA)
            return { androidMenuString(ANDROID_TEXT_VIDEO_GAMMA), androidPercentValue(androidNormalizedIntPreset(
                g_frontendSettings->gammaPercent, EMULATOR_VIDEO_PERCENT_VALUES, 100)) };
        if (row == ANDROID_VIDEO_SATURATION)
            return { androidMenuString(ANDROID_TEXT_VIDEO_SATURATION), androidPercentValue(androidNormalizedIntPreset(
                g_frontendSettings->saturationPercent, EMULATOR_VIDEO_PERCENT_VALUES, 100)) };
        if (row == ANDROID_VIDEO_MINIMIZED_BEHAVIOR)
        {
            AndroidMenuTextId valueId = g_frontendSettings->minimizedBehavior == MINIMIZED_BEHAVIOR_PAUSE ?
                ANDROID_TEXT_VIDEO_MINIMIZED_PAUSE :
                g_frontendSettings->minimizedBehavior == MINIMIZED_BEHAVIOR_THROTTLE ?
                ANDROID_TEXT_VIDEO_MINIMIZED_THROTTLE :
                g_frontendSettings->minimizedBehavior == MINIMIZED_BEHAVIOR_NORMAL ?
                ANDROID_TEXT_VIDEO_MINIMIZED_NORMAL : ANDROID_TEXT_VIDEO_MINIMIZED_PAUSE;
            return { androidMenuString(ANDROID_TEXT_VIDEO_MINIMIZED_BEHAVIOR), androidMenuString(valueId) };
        }
        if (row == ANDROID_VIDEO_SCREEN_ORIENTATION)
        {
            ScreenOrientationMode mode = androidScreenOrientationMode();
            AndroidMenuTextId valueId = mode == SCREEN_ORIENTATION_LANDSCAPE ?
                ANDROID_TEXT_VIDEO_SCREEN_ORIENTATION_LANDSCAPE :
                mode == SCREEN_ORIENTATION_PORTRAIT ?
                ANDROID_TEXT_VIDEO_SCREEN_ORIENTATION_PORTRAIT :
                ANDROID_TEXT_VIDEO_SCREEN_ORIENTATION_AUTO;
            return { androidMenuString(ANDROID_TEXT_VIDEO_SCREEN_ORIENTATION),
                androidMenuString(valueId) };
        }
        if (row == ANDROID_VIDEO_SCREEN_FILL)
        {
            ScreenFillMode fill = g_frontendSettings->screenFill;
            AndroidMenuTextId valueId = fill == SCREEN_FILL_BLURRED_EXTENSION ?
                ANDROID_TEXT_VIDEO_SCREEN_FILL_BLURRED_EXTENSION :
                fill == SCREEN_FILL_STRETCH ? ANDROID_TEXT_VIDEO_SCREEN_FILL_STRETCH :
                ANDROID_TEXT_VIDEO_SCREEN_FILL_ASPECT;
            return { androidMenuString(ANDROID_TEXT_VIDEO_SCREEN_FILL),
                androidMenuString(valueId) };
        }
        if (row == ANDROID_VIDEO_SHOW_FPS)
            return { androidMenuString(ANDROID_TEXT_VIDEO_SHOW_FPS), androidBooleanValue(g_frontendSettings->showFps) };
        if (row == ANDROID_VIDEO_BACK) return { chinese ? kZhBack : "Back", "" };
    }
    else if (g_androidMenuScreen == ANDROID_MENU_AUDIO)
    {
        static const AndroidMenuTextId effectTextIds[] =
        {
            ANDROID_TEXT_AUDIO_EFFECT_OFF, ANDROID_TEXT_AUDIO_EFFECT_SOFT, ANDROID_TEXT_AUDIO_EFFECT_CLEAR,
            ANDROID_TEXT_AUDIO_EFFECT_BASS_BOOST, ANDROID_TEXT_AUDIO_EFFECT_MONO
        };
        static_assert(sizeof(effectTextIds) / sizeof(effectTextIds[0]) == AUDIO_EFFECT_MODE_COUNT,
            "Audio effect menu text must match AudioEffectMode");
        static const AndroidMenuTextId noiseReductionTextIds[] = {
            ANDROID_TEXT_AUDIO_DIGITAL_NOISE_REDUCTION_HIGH,
            ANDROID_TEXT_AUDIO_DIGITAL_NOISE_REDUCTION_MEDIUM,
            ANDROID_TEXT_AUDIO_DIGITAL_NOISE_REDUCTION_LOW
        };
        static_assert(sizeof(noiseReductionTextIds) / sizeof(noiseReductionTextIds[0]) ==
            DIGITAL_NOISE_REDUCTION_LEVEL_COUNT,
            "Noise reduction menu text must match DigitalNoiseReductionLevel");
        if (row == ANDROID_AUDIO_VOLUME)
            return { androidMenuString(ANDROID_TEXT_AUDIO_VOLUME), androidPercentValue(androidNormalizedIntPreset(
                g_frontendSettings->audioVolumePercent, EMULATOR_AUDIO_VOLUME_VALUES, 100)) };
        if (row == ANDROID_AUDIO_BUFFER)
            return { androidMenuString(ANDROID_TEXT_AUDIO_BUFFER), androidAudioBufferValue(androidNormalizedIntPreset(
                g_frontendSettings->audioBufferSamples, EMULATOR_AUDIO_BUFFER_VALUES, 2048)) };
        if (row == ANDROID_AUDIO_BUFFER_LATENCY)
        {
            AudioBufferLatencyMode mode = g_frontendSettings->audioBufferLatency;
            if (mode < AUDIO_BUFFER_LATENCY_AUTO || mode >= AUDIO_BUFFER_LATENCY_MODE_COUNT)
                mode = AUDIO_BUFFER_LATENCY_AUTO;
            return { androidMenuString(ANDROID_TEXT_AUDIO_BUFFER_LATENCY),
                androidMenuString(androidAudioBufferLatencyTextId(mode)) };
        }
        if (row == ANDROID_AUDIO_EFFECT)
        {
            int index = (int)g_frontendSettings->audioEffect;
            if (index < 0 || index >= AUDIO_EFFECT_MODE_COUNT) index = 0;
            return { androidMenuString(ANDROID_TEXT_AUDIO_EFFECT), androidMenuString(effectTextIds[index]) };
        }
        if (row == ANDROID_AUDIO_DIGITAL_NOISE_REDUCTION)
        {
            int index = (int)g_frontendSettings->digitalNoiseReduction;
            if (index < 0 || index >= DIGITAL_NOISE_REDUCTION_LEVEL_COUNT)
                index = DIGITAL_NOISE_REDUCTION_HIGH;
            return { androidMenuString(ANDROID_TEXT_AUDIO_DIGITAL_NOISE_REDUCTION),
                androidMenuString(noiseReductionTextIds[index]) };
        }
        if (row == ANDROID_AUDIO_DISABLED)
            return { androidMenuString(ANDROID_TEXT_AUDIO_DISABLE), androidBooleanValue(g_frontendSettings->audioDisabled) };
        if (row == ANDROID_AUDIO_BACK) return { chinese ? kZhBack : "Back", "" };
    }
    else if (g_androidMenuScreen == ANDROID_MENU_INPUT)
    {
        if (row == ANDROID_INPUT_SYSTEM_IME)
            return { androidMenuString(ANDROID_TEXT_INPUT_SYSTEM_IME),
                androidBooleanValue(g_frontendSettings->systemImeDisabled) };
        if (row == ANDROID_INPUT_VIRTUAL_CONTROLS)
            return { androidMenuString(ANDROID_TEXT_INPUT_VIRTUAL_CONTROLS), androidBooleanValue(g_frontendSettings->showVirtualControls) };
        if (row == ANDROID_INPUT_VIRTUAL_CONTROL_SCALE)
            return { androidMenuString(ANDROID_TEXT_INPUT_VIRTUAL_CONTROL_SCALE), androidPercentValue(androidNormalizedIntPreset(
                g_frontendSettings->virtualControlScalePercent, EMULATOR_VIRTUAL_CONTROL_SCALE_VALUES, 100)) };
        if (row == ANDROID_INPUT_VIRTUAL_CONTROL_OPACITY)
            return { androidMenuString(ANDROID_TEXT_INPUT_VIRTUAL_CONTROL_OPACITY), androidPercentValue(androidNormalizedIntPreset(
                g_frontendSettings->virtualControlOpacityPercent, EMULATOR_VIRTUAL_CONTROL_OPACITY_VALUES, 100)) };
        if (row == ANDROID_INPUT_VIRTUAL_DPAD_TYPE)
        {
            AndroidMenuTextId valueId =
                g_frontendSettings->virtualDpadType == VIRTUAL_DPAD_SEGMENTED_RING ?
                ANDROID_TEXT_INPUT_VIRTUAL_DPAD_SEGMENTED_RING :
                ANDROID_TEXT_INPUT_VIRTUAL_DPAD_JOYSTICK;
            return { androidMenuString(ANDROID_TEXT_INPUT_VIRTUAL_DPAD_TYPE),
                androidMenuString(valueId) };
        }
        if (row == ANDROID_INPUT_CONTROLLER_MAPPING)
            return { androidMenuString(ANDROID_TEXT_INPUT_CONTROLLER_MAPPING), " " };
        if (row == ANDROID_INPUT_CONTROLLER_CALIBRATION)
            return { androidMenuString(ANDROID_TEXT_INPUT_CONTROLLER_CALIBRATION), " " };
        if (row == ANDROID_INPUT_BACK) return { chinese ? kZhBack : "Back", "" };
    }
    else if (g_androidMenuScreen == ANDROID_MENU_CONTROLLER_MAPPING)
    {
        uint32_t controlBit = 0;
        if (androidControllerMappingControlForRow(row, &controlBit))
        {
            std::string value = g_controllerMappingPending &&
                g_controllerMappingTarget == controlMask(controlBit) ?
                androidMenuString(ANDROID_TEXT_CONTROLLER_MAPPING_PRESS) :
                frontendControllerSourceForControl(controlBit);
            return { androidControllerMappingControlLabel(controlBit), value };
        }
        if (row == ANDROID_CONTROLLER_MAPPING_RESET)
            return { androidMenuString(ANDROID_TEXT_CONTROLLER_MAPPING_RESET), "" };
        if (row == ANDROID_CONTROLLER_MAPPING_BACK)
            return { chinese ? kZhBack : "Back", "" };
    }
    else if (g_androidMenuScreen == ANDROID_MENU_CONTROLLER_CALIBRATION)
    {
        if (row == ANDROID_CONTROLLER_CALIBRATION_START)
            return { androidMenuString(ANDROID_TEXT_CONTROLLER_CALIBRATION_START),
                controllerCalibrationStatusText() };
        if (row == ANDROID_CONTROLLER_CALIBRATION_RESET)
            return { androidMenuString(ANDROID_TEXT_CONTROLLER_CALIBRATION_RESET), "" };
        if (row == ANDROID_CONTROLLER_CALIBRATION_BACK)
            return { chinese ? kZhBack : "Back", "" };
    }
    else if (g_androidMenuScreen == ANDROID_MENU_SETTINGS)
    {
        if (row == ANDROID_SETTINGS_EXECUTION_MODE)
            return { androidMenuString(ANDROID_TEXT_SETTINGS_EXECUTION_MODE),
                androidExecutionModeValue(g_frontendSettings->executionMode) };
        if (row == ANDROID_SETTINGS_CPU_CLOCK)
            return { androidMenuString(ANDROID_TEXT_SETTINGS_CPU_CLOCK), androidCpuClockValue(g_frontendSettings->cpuClockHz) };
        if (row == ANDROID_SETTINGS_SPEED_SCALE)
            return { androidMenuString(ANDROID_TEXT_SETTINGS_SPEED_SCALE), androidScaleValue(g_frontendSettings->runtimeSpeedScale) };
        if (row == ANDROID_SETTINGS_OS_TIME_DELAY_SCALE)
            return { androidMenuString(ANDROID_TEXT_SETTINGS_OS_TIME_DELAY_SCALE), androidScaleValue(g_frontendSettings->osTimeDelayScale) };
        if (row == ANDROID_SETTINGS_CHEAT_MANAGER)
            return { androidMenuString(ANDROID_TEXT_SETTINGS_CHEAT_MANAGER), " " };
        if (row == ANDROID_SETTINGS_LANGUAGE)
            return { androidMenuString(ANDROID_TEXT_SETTINGS_LANGUAGE),
                g_frontendSettings->uiLanguage == UI_LANGUAGE_ENGLISH ? "English" : u8"\u4e2d\u6587" };
        if (row == ANDROID_SETTINGS_RESTORE_DEFAULTS)
            return { androidMenuString(ANDROID_TEXT_SETTINGS_RESET), "" };
        if (row == ANDROID_SETTINGS_BACK) return { chinese ? kZhBack : "Back", "" };
    }
    else if (g_androidMenuScreen == ANDROID_MENU_CHEAT_MANAGER)
    {
        CheatRuntimeStatus status = cheatRuntimeGetStatus();
        if (row == ANDROID_CHEAT_MANAGER_ENABLE)
            return { androidMenuString(ANDROID_TEXT_CHEAT_MANAGER_ENABLE),
                androidBooleanValue(status.available && g_frontendSettings->cheatsEnabled) };
        if (row == ANDROID_CHEAT_MANAGER_FILE)
            return { androidMenuString(ANDROID_TEXT_CHEAT_MANAGER_FILE),
                androidCheatManagerGamePath().empty() || status.sourcePath.empty() ?
                    androidMenuString(ANDROID_TEXT_CHEAT_MANAGER_NOT_LOADED) :
                    gameFileNameFromPath(status.sourcePath) };
        if (row == ANDROID_CHEAT_MANAGER_STATUS)
            return { androidMenuString(ANDROID_TEXT_CHEAT_MANAGER_STATUS),
                androidCheatManagerStatusValue(status) };

        int featureIndex = row - ANDROID_CHEAT_MANAGER_FEATURE_FIRST;
        if (featureIndex >= 0 && featureIndex < (int)status.entries.size())
        {
            const CheatRuntimeEntryView& entry = status.entries[(size_t)featureIndex];
            return { androidCheatManagerDisplayName(entry), androidBooleanValue(entry.enabled) };
        }

        int action = row - androidCheatManagerActionFirstRow(status);
        if (action == ANDROID_CHEAT_MANAGER_ENABLE_ALL_OFFSET)
            return { androidMenuString(ANDROID_TEXT_CHEAT_MANAGER_ENABLE_ALL), "" };
        if (action == ANDROID_CHEAT_MANAGER_DISABLE_ALL_OFFSET)
            return { androidMenuString(ANDROID_TEXT_CHEAT_MANAGER_DISABLE_ALL), "" };
        if (action == ANDROID_CHEAT_MANAGER_APPLY_OFFSET)
            return { androidMenuString(ANDROID_TEXT_CHEAT_MANAGER_APPLY), "" };
        if (action == ANDROID_CHEAT_MANAGER_REFRESH_OFFSET)
            return { androidMenuString(ANDROID_TEXT_CHEAT_MANAGER_REFRESH), "" };
        if (action == ANDROID_CHEAT_MANAGER_BACK_OFFSET)
            return { chinese ? kZhBack : "Back", "" };
    }
    return AndroidMenuRowContent{};
}

void requestAndroidSwitchGame(void)
{
    if (showAndroidConfirmationDialog(
        androidMenuString(ANDROID_TEXT_CONFIRM_SWITCH_GAME_TITLE),
        androidMenuString(ANDROID_TEXT_CONFIRM_SWITCH_GAME_BODY),
        androidMenuString(ANDROID_TEXT_CONFIRM_SWITCH_GAME_ACCEPT),
        androidMenuString(ANDROID_TEXT_CONFIRM_CANCEL)))
    {
        requestAndroidGame("");
    }
}

void requestAndroidRestartGame(void)
{
    if (!frontendGameRunning() || g_frontendCurrentGamePath.empty())
    {
        return;
    }
    if (showAndroidConfirmationDialog(
        androidMenuString(ANDROID_TEXT_CONFIRM_RESTART_GAME_TITLE),
        androidMenuString(ANDROID_TEXT_CONFIRM_RESTART_GAME_BODY),
        androidMenuString(ANDROID_TEXT_CONFIRM_RESTART_GAME_ACCEPT),
        androidMenuString(ANDROID_TEXT_CONFIRM_CANCEL)))
    {
        printf("frontend: restarting current app: %s\n", g_frontendCurrentGamePath.c_str());
        requestAndroidGame(g_frontendCurrentGamePath);
    }
}

void requestAndroidExitApplication(void)
{
    if (showAndroidConfirmationDialog(
        androidMenuString(ANDROID_TEXT_CONFIRM_EXIT_TITLE),
        androidMenuString(ANDROID_TEXT_CONFIRM_EXIT_BODY),
        androidMenuString(ANDROID_TEXT_CONFIRM_EXIT_ACCEPT),
        androidMenuString(ANDROID_TEXT_CONFIRM_CANCEL)))
    {
        frontendRequestQuit();
    }
}

static SaveStateFormat androidCurrentSaveStateFormat(void)
{
    return saveStateFormatForPath(g_frontendCurrentGamePath);
}

static std::string androidSaveStateTimeText(uint64_t timestamp);

static std::string androidSaveStateSlotText(
    int slot, const SaveStateSlotInfo& info)
{
    char label[48] = {};
    snprintf(label, sizeof(label), androidChineseUi() ?
        u8"\u6863\u4f4d %d%s" : "Slot %d%s", slot,
        info.exists ? "" : (androidChineseUi() ? u8"\uff08\u7a7a\uff09" : " (Empty)"));
    std::string text = label;
    if (info.exists && info.modifiedTime)
    {
        text += "\n";
        text += androidSaveStateTimeText(info.modifiedTime);
    }
    return text;
}

static std::string androidSaveStateErrorText(const std::string& error)
{
    if (error.empty()) return "";
    bool chinese = androidChineseUi();
    if (error == "runtime did not pause in time")
        return chinese ? u8"\u6e38\u620f\u672a\u80fd\u53ca\u65f6\u6682\u505c\u3002" :
            "The game did not pause in time.";
    if (error == "runtime state is not available" ||
        error == "runtime state output is invalid")
        return chinese ? u8"\u5f53\u524d\u6e38\u620f\u72b6\u6001\u4e0d\u53ef\u7528\u3002" :
            "The current game state is unavailable.";
    if (error == "saved runtime state is invalid")
        return chinese ? u8"\u5b58\u6863\u4e2d\u7684\u6e38\u620f\u72b6\u6001\u65e0\u6548\u3002" :
            "The saved game state is invalid.";
    if (error == "runtime state has too many records")
        return chinese ? u8"\u6e38\u620f\u72b6\u6001\u6570\u636e\u8fc7\u591a\u3002" :
            "The game state contains too many records.";
    if (error == "invalid state region")
        return chinese ? u8"\u6e38\u620f\u72b6\u6001\u5305\u542b\u65e0\u6548\u5185\u5b58\u533a\u57df\u3002" :
            "The game state contains an invalid memory region.";
    if (error == "runtime thread count does not match save state")
        return chinese ?
            u8"\u5f53\u524d\u6e38\u620f\u8fdb\u5ea6\u4e0e\u5b58\u6863\u4e0d\u5339\u914d\u3002"
            u8"\u8bf7\u8fdb\u5165\u4fdd\u5b58\u65f6\u7684\u76f8\u540c\u573a\u666f\u540e\u91cd\u8bd5\u3002" :
            "The current game state does not match this save. Return to the same scene and try again.";
    if (error == "runtime memory layout does not match save state")
        return chinese ? u8"\u5f53\u524d\u5185\u5b58\u5e03\u5c40\u4e0e\u5b58\u6863\u4e0d\u5339\u914d\u3002" :
            "The current memory layout does not match this save.";
    if (error == "failed to restore runtime state")
        return chinese ? u8"\u65e0\u6cd5\u6062\u590d\u6e38\u620f\u72b6\u6001\u3002" :
            "Could not restore the game state.";
    if (error == "invalid slot")
        return chinese ? u8"\u5b58\u6863\u6863\u4f4d\u65e0\u6548\u3002" : "The save slot is invalid.";
    if (error == "failed to compress save-state file")
        return chinese ? u8"\u65e0\u6cd5\u538b\u7f29\u5b58\u6863\u3002" : "Could not compress the save state.";
    if (error == "save-state file is too large")
        return chinese ? u8"\u5b58\u6863\u6587\u4ef6\u8fc7\u5927\u3002" : "The save-state file is too large.";
    if (error == "save-state payload is too large")
        return chinese ? u8"\u5b58\u6863\u6570\u636e\u8fc7\u5927\u3002" : "The save-state data is too large.";
    if (error == "save-state payload size mismatch")
        return chinese ? u8"\u5b58\u6863\u6570\u636e\u5927\u5c0f\u4e0d\u5339\u914d\u3002" :
            "The save-state data size does not match.";
    if (error == "failed to write save-state file")
        return chinese ? u8"\u65e0\u6cd5\u5199\u5165\u5b58\u6863\u6587\u4ef6\u3002" :
            "Could not write the save-state file.";
    if (error == "save-state file not found")
        return chinese ? u8"\u672a\u627e\u5230\u5b58\u6863\u6587\u4ef6\u3002" :
            "The save-state file was not found.";
    if (error == "save-state file is truncated")
        return chinese ? u8"\u5b58\u6863\u6587\u4ef6\u4e0d\u5b8c\u6574\u3002" :
            "The save-state file is incomplete.";
    if (error == "unsupported save-state file")
        return chinese ? u8"\u4e0d\u652f\u6301\u6b64\u5b58\u6863\u6587\u4ef6\u3002" :
            "This save-state file is not supported.";
    if (error == "save-state belongs to a different game")
        return chinese ? u8"\u6b64\u5b58\u6863\u5c5e\u4e8e\u5176\u4ed6\u6e38\u620f\u3002" :
            "This save state belongs to a different game.";
    if (error == "failed to decompress save-state file")
        return chinese ? u8"\u65e0\u6cd5\u89e3\u538b\u5b58\u6863\u3002" : "Could not decompress the save state.";
    if (error == "save-state file has unexpected data")
        return chinese ? u8"\u5b58\u6863\u6587\u4ef6\u5305\u542b\u5f02\u5e38\u6570\u636e\u3002" :
            "The save-state file contains unexpected data.";
    if (error == "save-state task register table is truncated")
        return chinese ? u8"\u5b58\u6863\u4efb\u52a1\u6570\u636e\u4e0d\u5b8c\u6574\u3002" :
            "The save-state task data is incomplete.";
    if (error == "save-state semaphore table is truncated")
        return chinese ? u8"\u5b58\u6863\u540c\u6b65\u6570\u636e\u4e0d\u5b8c\u6574\u3002" :
            "The save-state synchronization data is incomplete.";
    if (error == "save-state region table is truncated")
        return chinese ? u8"\u5b58\u6863\u5185\u5b58\u6570\u636e\u4e0d\u5b8c\u6574\u3002" :
            "The save-state memory data is incomplete.";
    if (error == "save-state region data is too large")
        return chinese ? u8"\u5b58\u6863\u5185\u5b58\u6570\u636e\u8fc7\u5927\u3002" :
            "The save-state memory data is too large.";
    if (error == "save-state region data is truncated")
        return chinese ? u8"\u5b58\u6863\u5185\u5b58\u6570\u636e\u4e0d\u5b8c\u6574\u3002" :
            "The save-state memory data is incomplete.";
    return (chinese ? std::string(u8"\u64cd\u4f5c\u5931\u8d25\uff1a") :
        std::string("Operation failed: ")) + error;
}

static bool validateAndroidSaveStateRuntimeCount(
    const SaveStateSlotInfo& info, SaveStateFormat format,
    std::string* error)
{
    if (!info.runtimeCountValid)
    {
        if (error) *error = "unsupported save-state file";
        return false;
    }
    uint32_t currentRuntimeCount = gameRuntimeActiveUnitCount();
    if (currentRuntimeCount == 0)
    {
        if (error) *error = "runtime is not available";
        return false;
    }
    if (format != SAVE_STATE_FORMAT_CC && info.runtimeCount != currentRuntimeCount)
    {
        printf("frontend: save-state runtime count mismatch current=%u saved=%u\n",
            currentRuntimeCount, info.runtimeCount);
        if (error) *error = "runtime thread count does not match save state";
        return false;
    }
    if (format == SAVE_STATE_FORMAT_CC && info.runtimeCount != currentRuntimeCount)
    {
        printf("frontend: CC save-state task count differs current=%u saved=%u; restore will rebuild tasks\n",
            currentRuntimeCount, info.runtimeCount);
    }
    return true;
}

void refreshAndroidSaveStateSlotInfo(int slot)
{
    if (slot < 1 || slot > kSaveStateSlotCount ||
        g_frontendCurrentGamePath.empty())
    {
        return;
    }
    SaveStateSlotInfo info = saveStateSlotInfo(
        g_frontendCurrentGamePath, androidCurrentSaveStateFormat(), slot);
    g_androidSaveStateSlotExists[slot - 1] = info.exists;
    g_androidSaveStateSlotModifiedTime[slot - 1] = info.modifiedTime;
}

void refreshAndroidSaveStateSlots(void)
{
    if (g_frontendCurrentGamePath.empty())
    {
        return;
    }
    SaveStateFormat format = androidCurrentSaveStateFormat();
    for (int slot = 1; slot <= kSaveStateSlotCount; ++slot)
    {
        if (slot == g_androidSaveStateSelectedSlot)
        {
            refreshAndroidSaveStateSlotInfo(slot);
            continue;
        }
        g_androidSaveStateSlotExists[slot - 1] = saveStateSlotExists(
            g_frontendCurrentGamePath, format, slot);
        g_androidSaveStateSlotModifiedTime[slot - 1] = 0;
    }
    g_androidSaveStateSlotCacheGamePath = g_frontendCurrentGamePath;
}

void invalidateAndroidSaveStateThumbnail(void)
{
    if (g_androidSaveStateThumbnail)
    {
        SDL_DestroyTexture(g_androidSaveStateThumbnail);
        g_androidSaveStateThumbnail = NULL;
    }
}

void refreshAndroidSaveStateThumbnail(void)
{
    invalidateAndroidSaveStateThumbnail();
    if (!g_renderer || g_frontendCurrentGamePath.empty())
    {
        return;
    }
    if (!g_androidSaveStateSlotExists[g_androidSaveStateSelectedSlot - 1])
    {
        return;
    }

    std::vector<uint8_t> bytes;
    if (!saveStateReadThumbnail(g_frontendCurrentGamePath,
        androidCurrentSaveStateFormat(), g_androidSaveStateSelectedSlot, &bytes) ||
        bytes.empty() || bytes.size() > 0x7fffffffu)
    {
        return;
    }
    SDL_RWops* stream = SDL_RWFromConstMem(bytes.data(), (int)bytes.size());
    SDL_Surface* surface = stream ? SDL_LoadBMP_RW(stream, 1) : NULL;
    if (surface)
    {
        g_androidSaveStateThumbnail = SDL_CreateTextureFromSurface(g_renderer, surface);
        SDL_FreeSurface(surface);
    }
}

static void androidSaveStateProgressCallback(
    const SaveStateProgress& progress, void*)
{
    g_androidSaveStateProgress = progress;
    uint16_t pixels[SCREEN_WIDTH * SCREEN_HEIGHT];
    framebufferCopyPresented(pixels, sizeof(pixels));
    drawFrame(pixels, 0);
}

static bool writeAndroidSaveState(
    SaveStateFormat format, int slot, std::string* error)
{
    if (!gameRuntimeWriteState(g_frontendCurrentGamePath, slot, error,
        androidSaveStateProgressCallback, NULL))
    {
        return false;
    }
    uint16_t pixels[SCREEN_WIDTH * SCREEN_HEIGHT];
    framebufferCopyPresented(pixels, sizeof(pixels));
    saveStateWriteThumbnailRgb565(g_frontendCurrentGamePath, format,
        slot, pixels, SCREEN_WIDTH, SCREEN_HEIGHT);
    return true;
}

static bool loadAndroidSaveState(SaveStateFormat format, int slot,
    const SaveStateSlotInfo& info, std::string* error)
{
    if (!validateAndroidSaveStateRuntimeCount(info, format, error))
    {
        return false;
    }
    return gameRuntimeReadState(g_frontendCurrentGamePath, slot, error,
        androidSaveStateProgressCallback, NULL);
}

void performAndroidSaveStateAction(bool saving)
{
    if (g_androidSaveStateBusy || g_frontendCurrentGamePath.empty())
    {
        return;
    }
    bool chinese = androidChineseUi();
    SaveStateFormat format = androidCurrentSaveStateFormat();
    int slot = g_androidSaveStateSelectedSlot;
    SaveStateSlotInfo slotInfo = saveStateSlotInfo(
        g_frontendCurrentGamePath, format, slot);
    std::string slotText = androidSaveStateSlotText(slot, slotInfo);
    if (saving)
    {
        std::string body = slotInfo.exists ?
            (chinese ? u8"\u6b64\u6863\u4f4d\u5df2\u6709\u5b58\u6863\uff0c\u662f\u5426\u8986\u76d6\uff1f" :
                "This slot already contains a save. Overwrite it?") :
            (chinese ? u8"\u4fdd\u5b58\u5230\u6b64\u6863\u4f4d\uff1f" : "Save to this slot?");
        body += "\n" + slotText;
        if (!showAndroidConfirmationDialog(chinese ?
                u8"\u4fdd\u5b58\u5b58\u6863" : "Save State", body,
            slotInfo.exists ? (chinese ? u8"\u8986\u76d6" : "Overwrite") :
                (chinese ? u8"\u4fdd\u5b58" : "Save"),
            chinese ? u8"\u53d6\u6d88" : "Cancel"))
        {
            return;
        }
    }
    else
    {
        if (!slotInfo.exists)
        {
            showAndroidMessageDialog(chinese ?
                u8"\u8bfb\u53d6\u5b58\u6863" : "Load State",
                (chinese ? u8"\u6b64\u6863\u4f4d\u4e3a\u7a7a\u3002\n" : "This slot is empty.\n") + slotText);
            return;
        }
        std::string body = chinese ?
            u8"\u8bfb\u53d6\u6b64\u5b58\u6863\uff1f\u5f53\u524d\u8fdb\u5ea6\u5c06\u88ab\u8986\u76d6\u3002\n" :
            "Load this state? Current progress will be overwritten.\n";
        body += slotText;
        if (!showAndroidConfirmationDialog(chinese ?
                u8"\u8bfb\u53d6\u5b58\u6863" : "Load State", body,
            chinese ? u8"\u8bfb\u53d6" : "Load",
            chinese ? u8"\u53d6\u6d88" : "Cancel"))
        {
            return;
        }
    }

    g_androidSaveStateBusy = true;
    g_androidSaveStateProgress.phase = saving ?
        SAVE_STATE_PROGRESS_COMPRESS : SAVE_STATE_PROGRESS_DECOMPRESS;
    g_androidSaveStateProgress.percent = 0;
    std::string error;
    bool ok = saving ? writeAndroidSaveState(format, slot, &error) :
        loadAndroidSaveState(format, slot, slotInfo, &error);
    g_androidSaveStateBusy = false;
    g_androidSaveStateStatus = ok ?
        (saving ? (chinese ? u8"\u5b58\u6863\u5df2\u4fdd\u5b58\u3002" : "State saved.") :
            (chinese ? u8"\u5b58\u6863\u5df2\u8bfb\u53d6\u3002" : "State loaded.")) :
        (error.empty() ? (saving ?
            (chinese ? u8"\u65e0\u6cd5\u4fdd\u5b58\u5b58\u6863\u3002" : "Could not save state.") :
            (chinese ? u8"\u65e0\u6cd5\u8bfb\u53d6\u5b58\u6863\u3002" : "Could not load state.")) :
            androidSaveStateErrorText(error));
    refreshAndroidSaveStateSlotInfo(slot);
    refreshAndroidSaveStateThumbnail();
    showAndroidMessageDialog(saving ?
        (chinese ? u8"\u4fdd\u5b58\u5b58\u6863" : "Save State") :
        (chinese ? u8"\u8bfb\u53d6\u5b58\u6863" : "Load State"),
        g_androidSaveStateStatus + "\n" + slotText);
    if (androidSaveStateActionReturnsToGame(saving, ok))
    {
        openAndroidMenu(ANDROID_MENU_NONE);
    }
}

void deleteAndroidSaveState(void)
{
    if (g_androidSaveStateBusy || g_frontendCurrentGamePath.empty()) return;
    SaveStateFormat format = androidCurrentSaveStateFormat();
    int slot = g_androidSaveStateSelectedSlot;
    SaveStateSlotInfo info = saveStateSlotInfo(
        g_frontendCurrentGamePath, format, slot);
    bool chinese = androidChineseUi();
    if (!info.exists)
    {
        showAndroidMessageDialog(chinese ?
            u8"\u5220\u9664\u5b58\u6863" : "Delete State",
            chinese ? u8"\u6b64\u6863\u4f4d\u4e3a\u7a7a\u3002" : "This slot is empty.");
        return;
    }
    std::string body = chinese ?
        u8"\u6240\u9009\u5b58\u6863\u548c\u622a\u56fe\u5c06\u88ab\u6c38\u4e45\u5220\u9664\u3002\u662f\u5426\u7ee7\u7eed\uff1f\n" :
        "The selected state and screenshot will be permanently deleted. Continue?\n";
    body += androidSaveStateSlotText(slot, info);
    if (!showAndroidConfirmationDialog(chinese ?
            u8"\u5220\u9664\u5b58\u6863" : "Delete State", body,
        chinese ? u8"\u5220\u9664" : "Delete",
        chinese ? u8"\u53d6\u6d88" : "Cancel")) return;
    bool ok = saveStateDeleteSlot(g_frontendCurrentGamePath, format, slot);
    refreshAndroidSaveStateSlotInfo(slot);
    refreshAndroidSaveStateThumbnail();
    showAndroidMessageDialog(chinese ?
        u8"\u5220\u9664\u5b58\u6863" : "Delete State",
        ok ? (chinese ? u8"\u5b58\u6863\u5df2\u5220\u9664\u3002" : "State deleted.") :
            (chinese ? u8"\u65e0\u6cd5\u5220\u9664\u5b58\u6863\u3002" : "Could not delete state."));
}

SDL_Rect androidSaveStateSlotRect(const SDL_Rect& panel, int slot)
{
    int gap = androidUiMetric(6);
    int horizontalInset = androidUiMetric(24);
    int top = panel.y + androidUiMetric(kAndroidMenuRowTop);
    int previewGap = gap;
    int contentWidth = panel.w - horizontalInset * 2;
    int previewHeight = androidUiMetric(
        4 * kAndroidMenuRowHeight + 3 * kAndroidMenuRowGap);
    int previewWidth = previewHeight * 4 / 3;
    if (previewWidth > contentWidth * 2 / 5)
    {
        previewWidth = contentWidth * 2 / 5;
    }
    int gridWidth = contentWidth - previewGap - previewWidth;
    int width = (gridWidth - gap * 2) / 3;
    int height = androidUiMetric(kAndroidMenuRowHeight);
    int index = slot - 1;
    return SDL_Rect{ panel.x + horizontalInset + (index % 3) * (width + gap),
        top + (index / 3) * (height + gap), width, height };
}

static SDL_Rect androidSaveStatePreviewRect(const SDL_Rect& panel)
{
    int horizontalInset = androidUiMetric(24);
    int previewGap = androidUiMetric(6);
    int contentWidth = panel.w - horizontalInset * 2;
    int previewHeight = androidUiMetric(
        4 * kAndroidMenuRowHeight + 3 * kAndroidMenuRowGap);
    int previewWidth = previewHeight * 4 / 3;
    if (previewWidth > contentWidth * 2 / 5)
    {
        previewWidth = contentWidth * 2 / 5;
        previewHeight = previewWidth * 3 / 4;
    }
    int gridWidth = contentWidth - previewGap - previewWidth;
    int top = panel.y + androidUiMetric(kAndroidMenuRowTop);
    return SDL_Rect{ panel.x + horizontalInset + gridWidth + previewGap,
        top, previewWidth, previewHeight };
}

SDL_Rect androidSaveStateActionRect(const SDL_Rect& panel, int index)
{
    int gap = androidUiMetric(kAndroidMenuRowGap);
    int horizontalInset = androidUiMetric(24);
    int contentWidth = panel.w - horizontalInset * 2;
    SDL_Rect finalSlotRow = androidSaveStateSlotRect(panel, 13);
    int firstRowTop = finalSlotRow.y + finalSlotRow.h + gap;
    int secondRowTop = firstRowTop + androidUiMetric(kAndroidMenuRowHeight) + gap;
    if (index < 2)
    {
        int firstWidth = (contentWidth - gap) / 2;
        return SDL_Rect{ panel.x + horizontalInset + index * (firstWidth + gap),
            firstRowTop, index == 0 ? firstWidth : contentWidth - gap - firstWidth,
            androidUiMetric(kAndroidMenuRowHeight) };
    }

    int previewHeight = androidUiMetric(
        4 * kAndroidMenuRowHeight + 3 * kAndroidMenuRowGap);
    int previewWidth = previewHeight * 4 / 3;
    if (previewWidth > contentWidth * 2 / 5)
    {
        previewWidth = contentWidth * 2 / 5;
    }
    int gridWidth = contentWidth - gap - previewWidth;
    int deleteWidth = (gridWidth - gap * 2) / 3;
    return index == 2 ?
        SDL_Rect{ panel.x + horizontalInset, secondRowTop, deleteWidth,
            androidUiMetric(kAndroidMenuRowHeight) } :
        SDL_Rect{ panel.x + horizontalInset + deleteWidth + gap, secondRowTop,
            contentWidth - gap - deleteWidth, androidUiMetric(kAndroidMenuRowHeight) };
}

static std::string androidSaveStateTimeText(uint64_t timestamp)
{
    if (!timestamp) return androidChineseUi() ? u8"\u5c1a\u672a\u4fdd\u5b58" : "Not saved";
    time_t value = (time_t)timestamp;
    struct tm localTime;
    if (localtime_r(&value, &localTime) == NULL)
        return androidChineseUi() ? u8"\u65f6\u95f4\u4e0d\u53ef\u7528" : "Time unavailable";
    char text[48] = {};
    snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d:%02d",
        localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
        localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
    return text;
}

void handleAndroidMainMenuSelection(int row)
{
    switch (row)
    {
    case ANDROID_MAIN_OPTIONS:
        openAndroidMenu(ANDROID_MENU_OPTIONS);
        break;
    case ANDROID_MAIN_SETTINGS:
        openAndroidMenu(ANDROID_MENU_SETTINGS);
        break;
    case ANDROID_MAIN_ABOUT:
        openAndroidMenu(ANDROID_MENU_ABOUT);
        break;
    case ANDROID_MAIN_EXIT_APPLICATION:
        requestAndroidExitApplication();
        break;
    case ANDROID_MAIN_BACK:
        navigateBackAndroidMenu();
        break;
    default:
        break;
    }
}

static std::string androidAboutMenuLabel(int row)
{
    switch (row)
    {
    case ANDROID_ABOUT_AUTHOR_HOMEPAGE:
        return androidMenuString(ANDROID_TEXT_HELP_AUTHOR_HOMEPAGE);
    case ANDROID_ABOUT_PROJECT_HOMEPAGE:
        return androidMenuString(ANDROID_TEXT_HELP_PROJECT_HOMEPAGE);
    case ANDROID_ABOUT_BACK:
        return androidChineseUi() ? kZhBack : "Back";
    default:
        return "";
    }
}

static const char* androidAboutHomepageUrl(int row)
{
    switch (row)
    {
    case ANDROID_ABOUT_AUTHOR_HOMEPAGE:
        return kAndroidAuthorHomepageUrl;
    case ANDROID_ABOUT_PROJECT_HOMEPAGE:
        return kAndroidProjectHomepageUrl;
    default:
        return NULL;
    }
}

void handleAndroidAboutMenuSelection(int row)
{
    if (row == ANDROID_ABOUT_BACK)
    {
        navigateBackAndroidMenu();
        return;
    }
    const char* url = androidAboutHomepageUrl(row);
    if (!url)
    {
        return;
    }
    if (SDL_OpenURL(url) != 0)
    {
        printf("frontend: unable to open homepage url=%s error=%s\n",
            url, SDL_GetError());
    }
}

static void requestAndroidRestoreDefaultSettings(void);

void handleAndroidOptionsSelection(int row)
{
    switch (row)
    {
    case ANDROID_OPTIONS_VIDEO:
        openAndroidMenu(ANDROID_MENU_VIDEO);
        break;
    case ANDROID_OPTIONS_AUDIO:
        openAndroidMenu(ANDROID_MENU_AUDIO);
        break;
    case ANDROID_OPTIONS_INPUT:
        openAndroidMenu(ANDROID_MENU_INPUT);
        break;
    case ANDROID_OPTIONS_RESTORE_DEFAULTS:
        requestAndroidRestoreDefaultSettings();
        break;
    case ANDROID_OPTIONS_BACK:
        navigateBackAndroidMenu();
        break;
    default:
        break;
    }
}

static bool restoreAndroidDefaultSettings(void)
{
    if (!g_frontendSettings)
    {
        return false;
    }

    RuntimeExecutionMode previousExecutionMode = g_frontendSettings->executionMode;
    std::string controllerMapping = g_frontendSettings->controllerMapping;
    std::string controllerCalibration = g_frontendSettings->controllerCalibration;
    *g_frontendSettings = emulatorDefaultSettings();
    g_frontendSettings->controllerMapping = controllerMapping;
    g_frontendSettings->controllerCalibration = controllerCalibration;
    androidSetScreenOrientationMode(g_frontendSettings->screenOrientationMode);
    syncAndroidScreenOrientation();
    cheatRuntimeSetEnabled(g_frontendSettings->cheatsEnabled);
    clearAndroidSystemTextTextures();
    bool saved = saveAndroidSettings();
    if (frontendGameRunning() &&
        previousExecutionMode != g_frontendSettings->executionMode)
    {
        requestAndroidGame(g_frontendCurrentGamePath, g_androidMenuScreen);
    }
    return saved;
}

static void requestAndroidRestoreDefaultSettings(void)
{
    std::string title = androidMenuString(ANDROID_TEXT_SETTINGS_RESET);
    std::string confirm = androidMenuString(ANDROID_TEXT_SETTINGS_RESET_CONFIRM);
    std::string accept = androidMenuString(ANDROID_TEXT_SETTINGS_RESET_ACCEPT);
    std::string cancel = androidMenuString(ANDROID_TEXT_SETTINGS_RESET_CANCEL);
    std::string success = androidMenuString(ANDROID_TEXT_SETTINGS_RESET_SUCCESS);
    std::string saveFailed = androidMenuString(ANDROID_TEXT_SETTINGS_RESET_SAVE_FAILED);
    if (!showAndroidConfirmationDialog(title, confirm, accept, cancel))
    {
        return;
    }

    bool saved = restoreAndroidDefaultSettings();
    showAndroidMessageDialog(title, saved ? success : saveFailed);
}

void handleAndroidDetailMenuSelection(AndroidMenuScreen screen, int row)
{
    if ((screen == ANDROID_MENU_VIDEO && row == ANDROID_VIDEO_BACK) ||
        (screen == ANDROID_MENU_AUDIO && row == ANDROID_AUDIO_BACK) ||
        (screen == ANDROID_MENU_INPUT && row == ANDROID_INPUT_BACK) ||
        (screen == ANDROID_MENU_CONTROLLER_MAPPING && row == ANDROID_CONTROLLER_MAPPING_BACK) ||
        (screen == ANDROID_MENU_CONTROLLER_CALIBRATION &&
            row == ANDROID_CONTROLLER_CALIBRATION_BACK) ||
        (screen == ANDROID_MENU_SETTINGS && row == ANDROID_SETTINGS_BACK))
    {
        navigateBackAndroidMenu();
        return;
    }

    if (screen == ANDROID_MENU_CHEAT_MANAGER)
    {
        CheatRuntimeStatus status = cheatRuntimeGetStatus();
        int action = row - androidCheatManagerActionFirstRow(status);
        if (action == ANDROID_CHEAT_MANAGER_BACK_OFFSET)
        {
            navigateBackAndroidMenu();
            return;
        }
        if (!g_frontendSettings)
        {
            return;
        }
        if (row == ANDROID_CHEAT_MANAGER_ENABLE)
        {
            setAndroidCheatManagerGlobalEnabled(!status.enabled);
            return;
        }
        if (row == ANDROID_CHEAT_MANAGER_FILE)
        {
            selectNextAndroidCheatManagerGamePath();
            return;
        }
        int featureIndex = row - ANDROID_CHEAT_MANAGER_FEATURE_FIRST;
        if (featureIndex >= 0 && featureIndex < (int)status.entries.size())
        {
            setAndroidCheatManagerFeatureEnabled((size_t)featureIndex,
                !status.entries[(size_t)featureIndex].enabled);
            return;
        }
        if (action == ANDROID_CHEAT_MANAGER_ENABLE_ALL_OFFSET ||
            action == ANDROID_CHEAT_MANAGER_DISABLE_ALL_OFFSET)
        {
            setAllAndroidCheatManagerFeaturesEnabled(
                action == ANDROID_CHEAT_MANAGER_ENABLE_ALL_OFFSET);
            return;
        }
        if (action == ANDROID_CHEAT_MANAGER_APPLY_OFFSET)
        {
            cheatRuntimeApplyNow();
            return;
        }
        if (action == ANDROID_CHEAT_MANAGER_REFRESH_OFFSET)
        {
            const std::string& gamePath = androidCheatManagerGamePath();
            if (!gamePath.empty())
            {
                if (g_frontendCurrentGamePath.empty())
                {
                    cheatRuntimeLoadForConfiguration(gamePath.c_str(),
                        emulatorCheatFeatureKeysForGame(*g_frontendSettings, gamePath));
                }
                else
                {
                    cheatRuntimeLoadForGame(status.currentGameSha256.c_str(),
                        gamePath.c_str(),
                        emulatorCheatFeatureKeysForGame(*g_frontendSettings, gamePath));
                }
            }
            return;
        }
        return;
    }

    if (!g_frontendSettings)
    {
        return;
    }

    bool restartCurrentGame = false;
    if (screen == ANDROID_MENU_VIDEO)
    {
        switch (row)
        {
        case ANDROID_VIDEO_ANTI_ALIASING:
            g_frontendSettings->antiAliasing =
                (AntiAliasingMode)nextAndroidEnumValue(
                    (int)g_frontendSettings->antiAliasing,
                    ANTI_ALIASING_MODE_COUNT,
                    ANTI_ALIASING_OFF);
            break;
        case ANDROID_VIDEO_COLOR_EFFECT:
            g_frontendSettings->colorEffect =
                (ColorEffectMode)nextAndroidEnumValue(
                    (int)g_frontendSettings->colorEffect,
                    COLOR_EFFECT_MODE_COUNT,
                    COLOR_EFFECT_NORMAL);
            break;
        case ANDROID_VIDEO_BRIGHTNESS:
            g_frontendSettings->brightnessPercent = nextAndroidIntPreset(
                g_frontendSettings->brightnessPercent, EMULATOR_VIDEO_PERCENT_VALUES, 100);
            break;
        case ANDROID_VIDEO_CONTRAST:
            g_frontendSettings->contrastPercent = nextAndroidIntPreset(
                g_frontendSettings->contrastPercent, EMULATOR_VIDEO_PERCENT_VALUES, 100);
            break;
        case ANDROID_VIDEO_GAMMA:
            g_frontendSettings->gammaPercent = nextAndroidIntPreset(
                g_frontendSettings->gammaPercent, EMULATOR_VIDEO_PERCENT_VALUES, 100);
            break;
        case ANDROID_VIDEO_SATURATION:
            g_frontendSettings->saturationPercent = nextAndroidIntPreset(
                g_frontendSettings->saturationPercent, EMULATOR_VIDEO_PERCENT_VALUES, 100);
            break;
        case ANDROID_VIDEO_MINIMIZED_BEHAVIOR:
            g_frontendSettings->minimizedBehavior =
                (MinimizedBehavior)nextAndroidEnumValue(
                    (int)g_frontendSettings->minimizedBehavior,
                    MINIMIZED_BEHAVIOR_COUNT,
                    MINIMIZED_BEHAVIOR_PAUSE);
            break;
        case ANDROID_VIDEO_SCREEN_ORIENTATION:
        {
            ScreenOrientationMode mode = (ScreenOrientationMode)nextAndroidEnumValue(
                (int)androidScreenOrientationMode(),
                SCREEN_ORIENTATION_MODE_COUNT,
                SCREEN_ORIENTATION_LANDSCAPE);
            androidSetScreenOrientationMode(mode);
            syncAndroidScreenOrientation();
            clearAndroidSystemTextTextures();
            break;
        }
        case ANDROID_VIDEO_SCREEN_FILL:
            g_frontendSettings->screenFill =
                (ScreenFillMode)nextAndroidEnumValue(
                    (int)g_frontendSettings->screenFill,
                    SCREEN_FILL_COUNT,
                    SCREEN_FILL_ASPECT);
            break;
        case ANDROID_VIDEO_SHOW_FPS:
            g_frontendSettings->showFps = !g_frontendSettings->showFps;
            break;
        default:
            return;
        }
    }
    else if (screen == ANDROID_MENU_AUDIO)
    {
        switch (row)
        {
        case ANDROID_AUDIO_VOLUME:
            g_frontendSettings->audioVolumePercent = nextAndroidIntPreset(
                g_frontendSettings->audioVolumePercent, EMULATOR_AUDIO_VOLUME_VALUES, 100);
            break;
        case ANDROID_AUDIO_BUFFER:
            g_frontendSettings->audioBufferSamples = nextAndroidIntPreset(
                g_frontendSettings->audioBufferSamples, EMULATOR_AUDIO_BUFFER_VALUES, 2048);
            break;
        case ANDROID_AUDIO_BUFFER_LATENCY:
            g_frontendSettings->audioBufferLatency =
                (AudioBufferLatencyMode)nextAndroidEnumValue(
                    (int)g_frontendSettings->audioBufferLatency,
                    AUDIO_BUFFER_LATENCY_MODE_COUNT,
                    AUDIO_BUFFER_LATENCY_AUTO);
            break;
        case ANDROID_AUDIO_EFFECT:
            g_frontendSettings->audioEffect =
                (AudioEffectMode)nextAndroidEnumValue(
                    (int)g_frontendSettings->audioEffect,
                    AUDIO_EFFECT_MODE_COUNT,
                    AUDIO_EFFECT_OFF);
            break;
        case ANDROID_AUDIO_DIGITAL_NOISE_REDUCTION:
            g_frontendSettings->digitalNoiseReduction =
                (DigitalNoiseReductionLevel)nextAndroidEnumValue(
                    (int)g_frontendSettings->digitalNoiseReduction,
                    DIGITAL_NOISE_REDUCTION_LEVEL_COUNT,
                    DIGITAL_NOISE_REDUCTION_HIGH);
            break;
        case ANDROID_AUDIO_DISABLED:
            g_frontendSettings->audioDisabled = !g_frontendSettings->audioDisabled;
            break;
        default:
            return;
        }
    }
    else if (screen == ANDROID_MENU_INPUT)
    {
        switch (row)
        {
        case ANDROID_INPUT_SYSTEM_IME:
            g_frontendSettings->systemImeDisabled =
                !g_frontendSettings->systemImeDisabled;
            break;
        case ANDROID_INPUT_VIRTUAL_CONTROLS:
            g_frontendSettings->showVirtualControls = !g_frontendSettings->showVirtualControls;
            break;
        case ANDROID_INPUT_VIRTUAL_CONTROL_SCALE:
            g_frontendSettings->virtualControlScalePercent = nextAndroidIntPreset(
                g_frontendSettings->virtualControlScalePercent,
                EMULATOR_VIRTUAL_CONTROL_SCALE_VALUES,
                100);
            break;
        case ANDROID_INPUT_VIRTUAL_CONTROL_OPACITY:
            g_frontendSettings->virtualControlOpacityPercent = nextAndroidIntPreset(
                g_frontendSettings->virtualControlOpacityPercent,
                EMULATOR_VIRTUAL_CONTROL_OPACITY_VALUES,
                100);
            break;
        case ANDROID_INPUT_VIRTUAL_DPAD_TYPE:
            g_frontendSettings->virtualDpadType =
                (VirtualDpadType)nextAndroidEnumValue(
                    (int)g_frontendSettings->virtualDpadType,
                    VIRTUAL_DPAD_TYPE_COUNT,
                    VIRTUAL_DPAD_JOYSTICK);
            break;
        case ANDROID_INPUT_CONTROLLER_MAPPING:
            openAndroidMenu(ANDROID_MENU_CONTROLLER_MAPPING);
            return;
        case ANDROID_INPUT_CONTROLLER_CALIBRATION:
            openAndroidMenu(ANDROID_MENU_CONTROLLER_CALIBRATION);
            return;
        default:
            return;
        }
    }
    else if (screen == ANDROID_MENU_CONTROLLER_MAPPING)
    {
        uint32_t controlBit = 0;
        if (androidControllerMappingControlForRow(row, &controlBit))
        {
            frontendBeginControllerMapping(controlBit);
        }
        else if (row == ANDROID_CONTROLLER_MAPPING_RESET)
        {
            resetControllerMapping();
        }
        return;
    }
    else if (screen == ANDROID_MENU_CONTROLLER_CALIBRATION)
    {
        if (row == ANDROID_CONTROLLER_CALIBRATION_START)
        {
            beginControllerCalibration();
        }
        else if (row == ANDROID_CONTROLLER_CALIBRATION_RESET)
        {
            resetControllerCalibration();
        }
        return;
    }
    else if (screen == ANDROID_MENU_SETTINGS)
    {
        switch (row)
        {
        case ANDROID_SETTINGS_EXECUTION_MODE:
        {
            RuntimeExecutionMode previousExecutionMode = g_frontendSettings->executionMode;
            g_frontendSettings->executionMode = (RuntimeExecutionMode)nextAndroidEnumValue(
                (int)g_frontendSettings->executionMode,
                RUNTIME_EXECUTION_MODE_COUNT,
                RUNTIME_EXECUTION_MODE_AUTOMATIC);
            restartCurrentGame = previousExecutionMode != g_frontendSettings->executionMode;
            break;
        }
        case ANDROID_SETTINGS_CPU_CLOCK:
            g_frontendSettings->cpuClockHz = nextAndroidStringPreset(
                g_frontendSettings->cpuClockHz, EMULATOR_CPU_CLOCK_VALUES);
            break;
        case ANDROID_SETTINGS_SPEED_SCALE:
            g_frontendSettings->runtimeSpeedScale = nextAndroidStringPreset(
                g_frontendSettings->runtimeSpeedScale, EMULATOR_SCALE_VALUES);
            break;
        case ANDROID_SETTINGS_OS_TIME_DELAY_SCALE:
            g_frontendSettings->osTimeDelayScale = nextAndroidStringPreset(
                g_frontendSettings->osTimeDelayScale, EMULATOR_SCALE_VALUES);
            break;
        case ANDROID_SETTINGS_CHEAT_MANAGER:
            prepareAndroidCheatManagerGamePath();
            if (!cheatRuntimeGetStatus().available)
            {
                setAndroidCheatManagerGlobalEnabled(false);
            }
            openAndroidMenu(ANDROID_MENU_CHEAT_MANAGER);
            return;
        case ANDROID_SETTINGS_LANGUAGE:
            g_frontendSettings->uiLanguage = (UiLanguage)nextAndroidEnumValue(
                (int)g_frontendSettings->uiLanguage,
                UI_LANGUAGE_COUNT,
                UI_LANGUAGE_CHINESE);
            clearAndroidSystemTextTextures();
            break;
        case ANDROID_SETTINGS_RESTORE_DEFAULTS:
            requestAndroidRestoreDefaultSettings();
            return;
        default:
            return;
        }
    }
    else
    {
        return;
    }

    saveAndroidSettings();
    if (restartCurrentGame && frontendGameRunning())
    {
        requestAndroidGame(g_frontendCurrentGamePath, screen);
    }
}

void drawAndroidMenuOverlay(void)
{
    if (!g_renderer || (!frontendGameRunning() &&
        !androidMenuScreenUsesOverlay(g_androidMenuScreen)))
    {
        return;
    }

    int width = 0;
    int height = 0;
    SDL_GetRendererOutputSize(g_renderer, &width, &height);
    if (g_androidMenuScreen == ANDROID_MENU_NONE)
    {
        if (!virtualControlsVisible() || portraitModeEnabled())
        {
            return;
        }
        SDL_Rect button = androidMenuButtonRect(width);
        drawAndroidOutline(button,
            SDL_Color{ 255, 255, 255, virtualControlAlpha(235) });
        drawAndroidSystemTextCentered("MENU", button,
            virtualCompactButtonTextSize(button),
            SDL_Color{ 255, 255, 255, virtualControlAlpha(255) });
        return;
    }

    SDL_Rect dim = { 0, 0, width, height };
    drawAndroidRect(dim, SDL_Color{ 0, 0, 0, 150 });
    SDL_Rect panel = androidPanelRect(width, height);
    drawAndroidRect(panel, SDL_Color{ 0, 0, 0, 245 });
    drawAndroidOutline(panel, SDL_Color{ 160, 160, 160, 255 });

    bool chinese = androidChineseUi();
    std::string title = chinese ? kZhMenu : "Menu";
    if (g_androidMenuScreen == ANDROID_MENU_PAUSE)
        title = chinese ? kZhGameMenu : "Game Menu";
    else if (g_androidMenuScreen == ANDROID_MENU_SAVE_STATE)
        title = chinese ? u8"\u5373\u65f6\u5b58\u6863" : "Save States";
    else if (g_androidMenuScreen == ANDROID_MENU_MAIN)
        title = chinese ? kZhMenu : "Menu";
    else if (g_androidMenuScreen == ANDROID_MENU_ABOUT)
        title = androidMenuString(ANDROID_TEXT_ABOUT_TITLE);
    else if (g_androidMenuScreen == ANDROID_MENU_SETTINGS)
        title = androidMenuString(ANDROID_TEXT_ROOT_SETTINGS);
    else if (g_androidMenuScreen == ANDROID_MENU_OPTIONS)
        title = androidMenuString(ANDROID_TEXT_ROOT_OPTIONS);
    else if (g_androidMenuScreen == ANDROID_MENU_VIDEO)
        title = androidMenuString(ANDROID_TEXT_ROOT_VIDEO);
    else if (g_androidMenuScreen == ANDROID_MENU_AUDIO)
        title = androidMenuString(ANDROID_TEXT_ROOT_AUDIO);
    else if (g_androidMenuScreen == ANDROID_MENU_INPUT)
        title = androidMenuString(ANDROID_TEXT_ROOT_INPUT);
    else if (g_androidMenuScreen == ANDROID_MENU_CONTROLLER_MAPPING)
        title = androidMenuString(ANDROID_TEXT_INPUT_CONTROLLER_MAPPING);
    else if (g_androidMenuScreen == ANDROID_MENU_CONTROLLER_CALIBRATION)
        title = androidMenuString(ANDROID_TEXT_INPUT_CONTROLLER_CALIBRATION);
    else if (g_androidMenuScreen == ANDROID_MENU_CHEAT_MANAGER)
        title = androidMenuString(ANDROID_TEXT_CHEAT_MANAGER_TITLE);
    SDL_Rect titleRect = { panel.x, panel.y + androidUiMetric(8), panel.w,
        androidUiMetric(52) };
    drawAndroidSystemTextCentered(title.c_str(), titleRect, androidUiMetric(27),
        SDL_Color{ 255, 255, 255, 255 });

    if (g_androidMenuScreen == ANDROID_MENU_PAUSE)
    {
        std::string rows[] =
        {
            chinese ? u8"\u5373\u65f6\u5b58\u6863" : "Save States",
            chinese ? kZhSwitchGame : "Switch Game",
            androidMenuString(ANDROID_TEXT_FILE_RESTART),
            androidMenuString(ANDROID_TEXT_ROOT_OPTIONS),
            androidMenuString(ANDROID_TEXT_ROOT_SETTINGS),
            chinese ? kZhExitApp : "Exit App",
            chinese ? kZhBack : "Back"
        };
        for (int row = 0; row < ANDROID_PAUSE_ROW_COUNT; ++row)
        {
            SDL_Rect rowRect = androidPanelRowRect(panel, row);
            bool selected = g_androidMenuSelectionHighlightVisible &&
                row == g_androidMenuSelectedRow;
            drawAndroidRect(rowRect, selected ? SDL_Color{ 50, 112, 180, 255 } :
                SDL_Color{ 48, 48, 48, 240 });
            if (selected) drawAndroidOutline(rowRect, SDL_Color{ 150, 215, 255, 255 });
            drawAndroidSystemTextCentered(rows[row].c_str(), rowRect, androidUiMetric(20),
                SDL_Color{ 255, 255, 255, 255 });
        }
    }
    else if (g_androidMenuScreen == ANDROID_MENU_SAVE_STATE)
    {
        for (int slot = 1; slot <= kSaveStateSlotCount; ++slot)
        {
            SDL_Rect slotRect = androidSaveStateSlotRect(panel, slot);
            bool selected = g_androidMenuSelectionHighlightVisible &&
                slot - 1 == g_androidMenuSelectedRow;
            bool activeSlot = slot == g_androidSaveStateSelectedSlot;
            bool saved = g_androidSaveStateSlotExists[slot - 1];
            SDL_Color slotColor = saved ?
                (activeSlot ? SDL_Color{ 48, 72, 96, 255 } :
                    SDL_Color{ 64, 64, 64, 255 }) :
                (activeSlot ? SDL_Color{ 62, 92, 126, 255 } :
                    SDL_Color{ 44, 44, 44, 245 });
            if (selected) slotColor = SDL_Color{ 50, 112, 180, 255 };
            drawAndroidRect(slotRect, slotColor);
            if (selected) drawAndroidOutline(slotRect, SDL_Color{ 150, 215, 255, 255 });
            char slotText[48] = {};
            snprintf(slotText, sizeof(slotText), chinese ? u8"\u6863\u4f4d %d%s" : "Slot %d%s",
                slot, saved ?
                    (chinese ? u8"  \u5df2\u4fdd\u5b58" : "  Saved") :
                    (chinese ? u8"  \u7a7a" : "  Empty"));
            drawAndroidSystemTextCentered(slotText, slotRect, androidUiMetric(15),
                SDL_Color{ 255, 255, 255, 255 });
        }

        SDL_Rect preview = androidSaveStatePreviewRect(panel);
        drawAndroidRect(preview, SDL_Color{ 28, 28, 28, 255 });
        drawAndroidOutline(preview, SDL_Color{ 112, 112, 112, 255 });
        SDL_Rect previewInner = { preview.x + 2, preview.y + 2,
            preview.w - 4, preview.h - 4 };
        if (g_androidSaveStateThumbnail)
        {
            SDL_RenderCopy(g_renderer, g_androidSaveStateThumbnail, NULL, &previewInner);
        }
        else
        {
            drawAndroidSystemTextCentered(
                g_androidSaveStateSlotExists[g_androidSaveStateSelectedSlot - 1] ?
                    (chinese ? u8"\u9884\u89c8\u4e0d\u53ef\u7528" : "Preview unavailable") :
                    (chinese ? u8"\u7a7a\u6863\u4f4d" : "Empty slot"),
                previewInner, androidUiMetric(16), SDL_Color{ 190, 190, 190, 255 });
        }
        SDL_Rect finalSlotRow = androidSaveStateSlotRect(panel, 13);
        SDL_Rect previewLabel = { preview.x, finalSlotRow.y,
            preview.w, finalSlotRow.h };
        drawAndroidRect(previewLabel, SDL_Color{ 36, 36, 36, 245 });
        drawAndroidSystemTextCentered(androidSaveStateTimeText(
            g_androidSaveStateSlotModifiedTime[g_androidSaveStateSelectedSlot - 1]).c_str(),
            previewLabel, androidUiMetric(15),
            SDL_Color{ 220, 220, 220, 255 });

        SDL_Rect save = androidSaveStateActionRect(panel, 0);
        SDL_Rect load = androidSaveStateActionRect(panel, 1);
        SDL_Rect remove = androidSaveStateActionRect(panel, 2);
        SDL_Rect back = androidSaveStateActionRect(panel, 3);
        const int actionFirst = kSaveStateSlotCount;
        drawAndroidRect(save, g_androidMenuSelectionHighlightVisible &&
            g_androidMenuSelectedRow == actionFirst ?
            SDL_Color{ 72, 148, 224, 255 } : SDL_Color{ 50, 112, 180, 255 });
        drawAndroidRect(load, g_androidMenuSelectionHighlightVisible &&
            g_androidMenuSelectedRow == actionFirst + 1 ?
            SDL_Color{ 72, 148, 224, 255 } : SDL_Color{ 50, 112, 180, 255 });
        drawAndroidRect(remove, g_androidMenuSelectionHighlightVisible &&
            g_androidMenuSelectedRow == actionFirst + 2 ?
            SDL_Color{ 176, 76, 76, 255 } : SDL_Color{ 120, 58, 58, 255 });
        drawAndroidRect(back, g_androidMenuSelectionHighlightVisible &&
            g_androidMenuSelectedRow == actionFirst + 3 ?
            SDL_Color{ 50, 112, 180, 255 } : SDL_Color{ 64, 64, 64, 255 });
        if (g_androidMenuSelectionHighlightVisible && g_androidMenuSelectedRow == actionFirst) drawAndroidOutline(save, SDL_Color{ 150, 215, 255, 255 });
        if (g_androidMenuSelectionHighlightVisible && g_androidMenuSelectedRow == actionFirst + 1) drawAndroidOutline(load, SDL_Color{ 150, 215, 255, 255 });
        if (g_androidMenuSelectionHighlightVisible && g_androidMenuSelectedRow == actionFirst + 2) drawAndroidOutline(remove, SDL_Color{ 255, 170, 170, 255 });
        if (g_androidMenuSelectionHighlightVisible && g_androidMenuSelectedRow == actionFirst + 3) drawAndroidOutline(back, SDL_Color{ 150, 215, 255, 255 });
        drawAndroidSystemTextCentered(chinese ? u8"\u4fdd\u5b58\u5373\u65f6\u5b58\u6863" : "Save State",
            save, androidUiMetric(17), SDL_Color{ 255, 255, 255, 255 });
        drawAndroidSystemTextCentered(chinese ? u8"\u8bfb\u53d6\u5373\u65f6\u5b58\u6863" : "Load State",
            load, androidUiMetric(17), SDL_Color{ 255, 255, 255, 255 });
        drawAndroidSystemTextCentered(chinese ? u8"\u5220\u9664" : "Delete", remove,
            androidUiMetric(17), SDL_Color{ 255, 255, 255, 255 });
        drawAndroidSystemTextCentered(chinese ? u8"\u8fd4\u56de" : "Back", back,
            androidUiMetric(17), SDL_Color{ 255, 255, 255, 255 });

        if (g_androidSaveStateBusy)
        {
            SDL_Rect modal = { panel.x + panel.w / 6, panel.y + panel.h / 2 - androidUiMetric(46),
                panel.w * 2 / 3, androidUiMetric(92) };
            drawAndroidRect(modal, SDL_Color{ 0, 0, 0, 245 });
            drawAndroidOutline(modal, SDL_Color{ 210, 210, 210, 255 });
            std::string progressLabel = g_androidSaveStateProgress.phase == SAVE_STATE_PROGRESS_COMPRESS ?
                (chinese ? u8"\u6b63\u5728\u538b\u7f29\u5b58\u6863\u2026" : u8"Compressing state\u2026") :
                (chinese ? u8"\u6b63\u5728\u89e3\u538b\u5b58\u6863\u2026" : u8"Decompressing state\u2026");
            SDL_Rect labelRect = { modal.x, modal.y + androidUiMetric(8), modal.w,
                androidUiMetric(28) };
            drawAndroidSystemTextCentered(progressLabel.c_str(), labelRect,
                androidUiMetric(17), SDL_Color{ 255, 255, 255, 255 });
            SDL_Rect bar = { modal.x + androidUiMetric(18), modal.y + androidUiMetric(50),
                modal.w - androidUiMetric(36), androidUiMetric(12) };
            drawAndroidRect(bar, SDL_Color{ 72, 72, 72, 255 });
            SDL_Rect complete = bar;
            complete.w = bar.w * (int)std::min<uint32_t>(100,
                g_androidSaveStateProgress.percent) / 100;
            drawAndroidRect(complete, SDL_Color{ 90, 180, 255, 255 });
        }
    }
    else if (g_androidMenuScreen == ANDROID_MENU_ABOUT)
    {
        std::string version = androidAppVersionName();
        const std::string lines[] =
        {
            chinese ? std::string(u8"\u4e01\u679c\u6d3e DingooPie Android ") + version :
                std::string("DingooPie Android ") + version,
            chinese ? std::string(u8"\u9002\u7528\u4e8e\u4e01\u679c A320\u3001\u6b4c\u7f8e X760+ \u548c\u6b4c\u7f8e A330 \u7684\u6e38\u620f\u6a21\u62df\u5668") :
                std::string("Game emulator for Dingoo A320, Gemei X760+, and Gemei A330"),
            chinese ? std::string(u8"\u6e38\u620f\u6587\u4ef6\u683c\u5f0f\u5f52\u539f\u5382\u5546\u6240\u6709") :
                std::string("Game file formats belong to their original vendors"),
            chinese ? std::string(u8"\u7531 BL2CK Software \u63d0\u4f9b\u652f\u6301") :
                std::string("Powered by BL2CK Software")
        };
        const int lineCount = (int)(sizeof(lines) / sizeof(lines[0]));
        for (int index = 0; index < lineCount; ++index)
        {
            SDL_Rect textRect =
            {
                panel.x + androidUiMetric(24),
                panel.y + androidUiMetric(78 + index * 34),
                panel.w - androidUiMetric(48),
                androidUiMetric(30)
            };
            drawAndroidSystemTextCentered(lines[index].c_str(), textRect,
                androidUiMetric(index == 0 ? 21 : 18), SDL_Color{ 220, 220, 220, 255 });
        }
        for (int row = ANDROID_ABOUT_AUTHOR_HOMEPAGE;
            row < ANDROID_ABOUT_ROW_COUNT; ++row)
        {
            bool selected = g_androidMenuSelectionHighlightVisible &&
                g_androidMenuSelectedRow == row;
            SDL_Rect rowRect = androidPanelRowRect(panel, row);
            drawAndroidRect(rowRect, selected ?
                SDL_Color{ 50, 112, 180, 255 } : SDL_Color{ 48, 48, 48, 240 });
            if (selected)
            {
                drawAndroidOutline(rowRect, SDL_Color{ 150, 215, 255, 255 });
            }
            std::string label = androidAboutMenuLabel(row);
            drawAndroidSystemTextCentered(label.c_str(), rowRect,
                androidUiMetric(20), SDL_Color{ 255, 255, 255, 255 });
        }
    }
    else if (androidMenuScreenHasSettingsList())
    {
        int rowCount = androidSettingsMenuRowCount();
        clampAndroidMenuScroll(panel, rowCount);
        SDL_Rect viewport = androidMenuViewportRect(panel);
        SDL_RenderSetClipRect(g_renderer, &viewport);
        for (int row = 0; row < rowCount; ++row)
        {
            SDL_Rect rowRect = androidMenuRowRect(panel, row);
            bool selected = g_androidMenuSelectionHighlightVisible &&
                row == g_androidMenuSelectedRow;
            drawAndroidRect(rowRect, selected ? SDL_Color{ 50, 112, 180, 255 } :
                SDL_Color{ 48, 48, 48, 240 });
            if (selected) drawAndroidOutline(rowRect, SDL_Color{ 150, 215, 255, 255 });
            AndroidMenuRowContent content = androidMenuRowContent(row);
            if (content.value.empty())
            {
                drawAndroidSystemTextCentered(content.label.c_str(), rowRect, androidUiMetric(20),
                    SDL_Color{ 255, 255, 255, 255 });
            }
            else
            {
                int textInset = androidUiMetric(16);
                drawAndroidSystemTextLeftCentered(content.label.c_str(), rowRect,
                    textInset, androidUiMetric(19), SDL_Color{ 255, 255, 255, 255 });
                drawAndroidSystemTextRightCentered(content.value.c_str(), rowRect,
                    textInset, androidUiMetric(19), SDL_Color{ 128, 210, 255, 255 });
            }
        }
        SDL_RenderSetClipRect(g_renderer, NULL);

        int maxScroll = androidMenuMaxScroll(panel, rowCount);
        if (maxScroll > 0)
        {
            int trackWidth = std::max(4, androidUiMetric(6));
            int trackX = panel.x + panel.w - androidUiMetric(12);
            SDL_Rect track = { trackX, viewport.y, trackWidth, viewport.h };
            drawAndroidRect(track, SDL_Color{ 80, 80, 80, 190 });
            int contentHeight = viewport.h + maxScroll;
            int thumbHeight = std::max(androidUiMetric(24),
                viewport.h * viewport.h / contentHeight);
            int thumbTravel = std::max(0, viewport.h - thumbHeight);
            int thumbY = viewport.y + thumbTravel * g_androidMenuScrollOffset / maxScroll;
            SDL_Rect thumb = { trackX, thumbY, trackWidth, thumbHeight };
            drawAndroidRect(thumb, SDL_Color{ 235, 235, 235, 235 });
        }
    }
}
