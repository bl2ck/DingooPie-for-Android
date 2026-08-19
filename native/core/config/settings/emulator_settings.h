#ifndef DINGOO_PIE_CONFIG_SETTINGS_EMULATOR_SETTINGS_H
#define DINGOO_PIE_CONFIG_SETTINGS_EMULATOR_SETTINGS_H

#include "shared/execution/execution_backend.h"

#include <string>
#include <vector>

static const int EMULATOR_VIDEO_PERCENT_VALUES[] = { 50, 75, 90, 100, 110, 125, 150 };
static const int EMULATOR_AUDIO_VOLUME_VALUES[] = { 0, 25, 50, 75, 100, 125, 150 };
static const int EMULATOR_AUDIO_BUFFER_VALUES[] = { 512, 1024, 2048, 4096 };
static const int EMULATOR_VIRTUAL_CONTROL_SCALE_VALUES[] = { 75, 100, 125, 150 };
static const char* const EMULATOR_CPU_CLOCK_VALUES[] = {
    "", "200000000", "336000000", "360000000", "400000000", "430000000"
};
static const char* const EMULATOR_SCALE_VALUES[] = {
    "", "1.0", "0.95", "0.90", "0.85", "0.80", "0.75", "0.70", "0.65",
    "0.60", "0.55", "0.50", "0.45", "0.40", "0.35", "0.30", "0.25", "0.20"
};

// Keep persisted option values in the same order as the settings UI. Add new
// values immediately before the corresponding COUNT sentinel.
enum AntiAliasingMode
{
    ANTI_ALIASING_OFF = 0,
    ANTI_ALIASING_LOW,
    ANTI_ALIASING_CLEAR,
    ANTI_ALIASING_MODE_COUNT
};

enum ColorEffectMode
{
    COLOR_EFFECT_NORMAL = 0,
    COLOR_EFFECT_GRAYSCALE,
    COLOR_EFFECT_INVERT,
    COLOR_EFFECT_SOFT_BLUR,
    COLOR_EFFECT_SHARPEN,
    COLOR_EFFECT_VIVID,
    COLOR_EFFECT_SEPIA,
    COLOR_EFFECT_PIXEL_GRID,
    COLOR_EFFECT_LCD_SCANLINE,
    COLOR_EFFECT_LIGHT_CRT,
    COLOR_EFFECT_MODE_COUNT
};

enum AudioEffectMode
{
    AUDIO_EFFECT_OFF = 0,
    AUDIO_EFFECT_SOFT,
    AUDIO_EFFECT_CLEAR,
    AUDIO_EFFECT_BASS_BOOST,
    AUDIO_EFFECT_MONO,
    AUDIO_EFFECT_MODE_COUNT
};

enum AudioBufferLatencyMode
{
    AUDIO_BUFFER_LATENCY_AUTO = 0,
    AUDIO_BUFFER_LATENCY_70MS,
    AUDIO_BUFFER_LATENCY_80MS,
    AUDIO_BUFFER_LATENCY_90MS,
    AUDIO_BUFFER_LATENCY_100MS,
    AUDIO_BUFFER_LATENCY_110MS,
    AUDIO_BUFFER_LATENCY_120MS,
    AUDIO_BUFFER_LATENCY_MODE_COUNT
};

static_assert(AUDIO_BUFFER_LATENCY_70MS == AUDIO_BUFFER_LATENCY_AUTO + 1 &&
    AUDIO_BUFFER_LATENCY_80MS == AUDIO_BUFFER_LATENCY_70MS + 1 &&
    AUDIO_BUFFER_LATENCY_90MS == AUDIO_BUFFER_LATENCY_80MS + 1 &&
    AUDIO_BUFFER_LATENCY_100MS == AUDIO_BUFFER_LATENCY_90MS + 1 &&
    AUDIO_BUFFER_LATENCY_110MS == AUDIO_BUFFER_LATENCY_100MS + 1 &&
    AUDIO_BUFFER_LATENCY_120MS == AUDIO_BUFFER_LATENCY_110MS + 1 &&
    AUDIO_BUFFER_LATENCY_MODE_COUNT == AUDIO_BUFFER_LATENCY_120MS + 1,
    "Audio latency enum order must match the visible menu");

enum DigitalNoiseReductionLevel
{
    DIGITAL_NOISE_REDUCTION_HIGH = 0,
    DIGITAL_NOISE_REDUCTION_MEDIUM,
    DIGITAL_NOISE_REDUCTION_LOW,
    DIGITAL_NOISE_REDUCTION_LEVEL_COUNT
};

static_assert(DIGITAL_NOISE_REDUCTION_MEDIUM ==
        DIGITAL_NOISE_REDUCTION_HIGH + 1 &&
    DIGITAL_NOISE_REDUCTION_LOW == DIGITAL_NOISE_REDUCTION_MEDIUM + 1 &&
    DIGITAL_NOISE_REDUCTION_LEVEL_COUNT == DIGITAL_NOISE_REDUCTION_LOW + 1,
    "Digital noise reduction enum order must match the visible menu");

enum UiLanguage
{
    UI_LANGUAGE_CHINESE = 0,
    UI_LANGUAGE_ENGLISH,
    UI_LANGUAGE_COUNT
};

enum MinimizedBehavior
{
    MINIMIZED_BEHAVIOR_NORMAL = 0,
    MINIMIZED_BEHAVIOR_PAUSE,
    MINIMIZED_BEHAVIOR_THROTTLE,
    MINIMIZED_BEHAVIOR_COUNT
};

enum ScreenOrientationMode
{
    SCREEN_ORIENTATION_AUTO = 0,
    SCREEN_ORIENTATION_LANDSCAPE,
    SCREEN_ORIENTATION_PORTRAIT,
    SCREEN_ORIENTATION_MODE_COUNT
};

enum ScreenFillMode
{
    SCREEN_FILL_ASPECT = 0,
    SCREEN_FILL_BLURRED_EXTENSION,
    SCREEN_FILL_STRETCH,
    SCREEN_FILL_COUNT
};

enum VirtualDpadType
{
    VIRTUAL_DPAD_JOYSTICK = 0,
    VIRTUAL_DPAD_SEGMENTED_RING,
    VIRTUAL_DPAD_TYPE_COUNT
};

struct EmulatorCheatSelection
{
    std::string cheatFileName;
    std::vector<std::string> enabledFeatureKeys;
};

struct EmulatorSettings
{
    // Video fields follow the Android Options > Video menu order.
    AntiAliasingMode antiAliasing;
    ColorEffectMode colorEffect;
    int brightnessPercent;
    int contrastPercent;
    int gammaPercent;
    int saturationPercent;
    MinimizedBehavior minimizedBehavior;
    ScreenOrientationMode screenOrientationMode;
    ScreenFillMode screenFill;
    bool showFps;

    // Audio fields follow the Android Options > Audio menu order.
    int audioVolumePercent;
    int audioBufferSamples;
    AudioBufferLatencyMode audioBufferLatency;
    AudioEffectMode audioEffect;
    DigitalNoiseReductionLevel digitalNoiseReduction;
    bool audioDisabled;

    // Input fields follow the Android Options > Input menu order.
    bool systemImeDisabled;
    bool showVirtualControls;
    int virtualControlScalePercent;
    VirtualDpadType virtualDpadType;
    std::string controllerMapping;
    std::string controllerCalibration;
    std::string keyboardMapping;

    // Runtime fields follow the Android Settings menu order.
    RuntimeExecutionMode executionMode;
    std::string cpuClockHz;
    std::string runtimeSpeedScale;
    std::string osTimeDelayScale;
    bool cheatsEnabled;
    std::vector<EmulatorCheatSelection> cheatSelections;

    UiLanguage uiLanguage;

    bool debugProfile;

    // Derived from screenOrientationMode and the current Android orientation.
    bool portraitMode;
};

EmulatorSettings emulatorDefaultSettings(void);
std::string emulatorSettingsPath(void);
EmulatorSettings emulatorLoadSettings(void);
bool emulatorSaveSettings(const EmulatorSettings& settings);
std::vector<std::string> emulatorCheatFeatureKeysForGame(
    const EmulatorSettings& settings,
    const std::string& gamePath);
bool emulatorSetCheatFeatureKeysForGame(
    EmulatorSettings* settings,
    const std::string& gamePath,
    const std::vector<std::string>& featureKeys);
void emulatorTraceSettings(const char* reason, const EmulatorSettings& settings);
bool emulatorResetSettings(void);
void emulatorApplySettingsToEnvironment(const EmulatorSettings& settings);
void emulatorApplySharedRuntimeSettings(const EmulatorSettings& settings);
const char* emulatorAntiAliasingName(AntiAliasingMode mode);
const char* emulatorColorEffectName(ColorEffectMode mode);
const char* emulatorAudioEffectName(AudioEffectMode mode);
const char* emulatorAudioBufferLatencyName(AudioBufferLatencyMode mode);
int emulatorAudioBufferLatencyMilliseconds(AudioBufferLatencyMode mode);
const char* emulatorDigitalNoiseReductionName(DigitalNoiseReductionLevel level);
const char* emulatorUiLanguageName(UiLanguage language);
const char* emulatorMinimizedBehaviorName(MinimizedBehavior behavior);
const char* emulatorScreenOrientationName(ScreenOrientationMode mode);
const char* emulatorScreenFillName(ScreenFillMode fill);
const char* emulatorVirtualDpadTypeName(VirtualDpadType type);

#endif
