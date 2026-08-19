$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Assert-OrderedText {
    param(
        [string]$RelativePath,
        [string[]]$ExpectedText
    )

    $path = Join-Path $projectRoot $RelativePath
    $content = Get-Content -LiteralPath $path -Raw
    $offset = 0
    foreach ($text in $ExpectedText) {
        $index = $content.IndexOf($text, $offset, [System.StringComparison]::Ordinal)
        if ($index -lt 0) {
            throw "Expected ordered text was not found in $RelativePath after offset $offset`: $text"
        }
        $offset = $index + $text.Length
    }
}

$settingsHeader = Get-Content -LiteralPath (Join-Path $projectRoot 'native/core/config/settings/emulator_settings.h') -Raw
$settingsSource = Get-Content -LiteralPath (Join-Path $projectRoot 'native/core/config/settings/emulator_settings.cpp') -Raw
$audioSource = Get-Content -LiteralPath (Join-Path $projectRoot 'native/core/frontend/audio/sdl_audio.cpp') -Raw
$menuSource = Get-Content -LiteralPath (Join-Path $projectRoot 'native/core/frontend/menu/menu_overlay.cpp') -Raw
if (!$settingsHeader.Contains(
        'EMULATOR_AUDIO_BUFFER_VALUES[] = { 512, 1024, 2048, 4096, 8192 };')) {
    throw 'Audio buffer menu values do not match the supported low-latency order.'
}
if (!$settingsSource.Contains('case 8192:') -or !$audioSource.Contains('case 8192:')) {
    throw 'The 8192-sample compatibility buffer is not connected end to end.'
}
foreach ($defaultBufferText in @(
        'settings.audioBufferSamples = 2048;',
        'normalizeAudioBufferSamples(settings.audioBufferSamples, 2048)',
        'static int g_bufferSamples = 2048;',
        'return 2048;',
        'EMULATOR_AUDIO_BUFFER_VALUES, 2048')) {
    if (!$settingsSource.Contains($defaultBufferText) -and
            !$audioSource.Contains($defaultBufferText) -and
            !$menuSource.Contains($defaultBufferText)) {
        throw "Audio buffer default and fallback are inconsistent: $defaultBufferText"
    }
}
if (!$audioSource.Contains('static const uint32_t kDefaultAudioBufferLatencyMs = 130;') -or
        !$audioSource.Contains('audioBufferLatencyMillisecondsLocked()')) {
    throw 'Audio latency configuration is not connected to the runtime queue.'
}
foreach ($latency in @(110, 120, 130, 140, 150)) {
    if (!$settingsHeader.Contains("AUDIO_BUFFER_LATENCY_${latency}MS") -or
            !$settingsSource.Contains(('"' + $latency + 'ms"')) -or
            !$settingsSource.Contains("return $latency;")) {
        throw "Audio latency preset is incomplete: ${latency}ms"
    }
}
foreach ($removedLatency in @(70, 80, 90, 100)) {
    if ($settingsHeader.Contains("AUDIO_BUFFER_LATENCY_${removedLatency}MS")) {
        throw "Removed audio latency preset is still present: ${removedLatency}ms"
    }
}
if (!$audioSource.Contains('static const uint32_t kPendingAudioMaxBytes = 512 * 1024;') -or
        !$audioSource.Contains('return kPendingAudioMaxBytes;')) {
    throw 'Audio pending queue does not use the 512KB compatibility limit.'
}
foreach ($removedRecentSymbol in @(
        'EMULATOR_RECENT_GAME_LIMIT', 'lastGamePath', 'recentGamePaths',
        'emulatorRememberRecentGame', 'emulatorRemoveRecentGame',
        'emulatorClearRecentGames', 'writeIniString("recent"')) {
    if ($settingsHeader.Contains($removedRecentSymbol) -or
            $settingsSource.Contains($removedRecentSymbol)) {
        throw "Removed recent-game symbol is still present: $removedRecentSymbol"
    }
}

$audioFields = @(
    'int audioVolumePercent;',
    'int audioBufferSamples;',
    'AudioBufferLatencyMode audioBufferLatency;',
    'AudioEffectMode audioEffect;',
    'DigitalNoiseReductionLevel digitalNoiseReduction;',
    'bool audioDisabled;'
)
$videoFields = @(
    'AntiAliasingMode antiAliasing;',
    'ColorEffectMode colorEffect;',
    'int brightnessPercent;',
    'int contrastPercent;',
    'int gammaPercent;',
    'int saturationPercent;',
    'MinimizedBehavior minimizedBehavior;',
    'ScreenOrientationMode screenOrientationMode;',
    'ScreenFillMode screenFill;',
    'bool showFps;'
)
$inputFields = @(
    'bool systemImeDisabled;',
    'bool showVirtualControls;',
    'int virtualControlScalePercent;',
    'VirtualDpadType virtualDpadType;',
    'std::string controllerMapping;',
    'std::string controllerCalibration;',
    'std::string keyboardMapping;'
)
$runtimeFields = @(
    'RuntimeExecutionMode executionMode;',
    'std::string cpuClockHz;',
    'std::string runtimeSpeedScale;',
    'std::string osTimeDelayScale;',
    'bool cheatsEnabled;',
    'std::vector<EmulatorCheatSelection> cheatSelections;',
    'UiLanguage uiLanguage;'
)
$audioDefaults = @(
    'settings.audioVolumePercent = 100;',
    'settings.audioBufferSamples = 2048;',
    'settings.audioBufferLatency = AUDIO_BUFFER_LATENCY_AUTO;',
    'settings.audioEffect = AUDIO_EFFECT_OFF;',
    'settings.digitalNoiseReduction = DIGITAL_NOISE_REDUCTION_HIGH;',
    'settings.audioDisabled = false;'
)
$audioIniLoad = @(
    'readIniInt("audio", "volume_percent"',
    'readIniInt("audio", "buffer_samples"',
    'readIniString("audio", "buffer_latency"',
    'readIniString(',
    '"effect"',
    '"digital_noise_reduction"',
    'readIniBool("audio", "audio_disabled"'
)
$audioIniWrite = @(
    'writeIniInt("audio", "volume_percent"',
    'writeIniInt("audio", "buffer_samples"',
    'writeIniString("audio", "buffer_latency"',
    'writeIniString("audio", "effect"',
    'writeIniString("audio", "digital_noise_reduction"',
    'writeIniBool("audio", "audio_disabled"'
)
$audioRows = @(
    'ANDROID_AUDIO_VOLUME = 0,',
    'ANDROID_AUDIO_BUFFER,',
    'ANDROID_AUDIO_BUFFER_LATENCY,',
    'ANDROID_AUDIO_EFFECT,',
    'ANDROID_AUDIO_DIGITAL_NOISE_REDUCTION,',
    'ANDROID_AUDIO_DISABLED,',
    'ANDROID_AUDIO_BACK,',
    'ANDROID_AUDIO_ROW_COUNT'
)
$videoRows = @(
    'ANDROID_VIDEO_ANTI_ALIASING = 0,',
    'ANDROID_VIDEO_COLOR_EFFECT,',
    'ANDROID_VIDEO_BRIGHTNESS,',
    'ANDROID_VIDEO_CONTRAST,',
    'ANDROID_VIDEO_GAMMA,',
    'ANDROID_VIDEO_SATURATION,',
    'ANDROID_VIDEO_MINIMIZED_BEHAVIOR,',
    'ANDROID_VIDEO_SCREEN_ORIENTATION,',
    'ANDROID_VIDEO_SCREEN_FILL,',
    'ANDROID_VIDEO_SHOW_FPS,',
    'ANDROID_VIDEO_BACK,',
    'ANDROID_VIDEO_ROW_COUNT'
)
$inputRows = @(
    'ANDROID_INPUT_SYSTEM_IME = 0,',
    'ANDROID_INPUT_VIRTUAL_CONTROLS,',
    'ANDROID_INPUT_VIRTUAL_CONTROL_SCALE,',
    'ANDROID_INPUT_VIRTUAL_DPAD_TYPE,',
    'ANDROID_INPUT_CONTROLLER_MAPPING,',
    'ANDROID_INPUT_CONTROLLER_CALIBRATION,',
    'ANDROID_INPUT_BACK,',
    'ANDROID_INPUT_ROW_COUNT'
)
$inputControlBits = @(
    'enum InputControlBit : uint32_t',
    'CONTROL_BUTTON_A = 31,',
    'CONTROL_BUTTON_B = 21,',
    'CONTROL_BUTTON_X = 16,',
    'CONTROL_BUTTON_Y = 6,',
    'CONTROL_BUTTON_START = 11,',
    'CONTROL_BUTTON_SELECT = 10,',
    'CONTROL_TRIGGER_LEFT = 8,',
    'CONTROL_TRIGGER_RIGHT = 29,',
    'CONTROL_DPAD_UP = 20,',
    'CONTROL_DPAD_DOWN = 27,',
    'CONTROL_DPAD_LEFT = 28,',
    'CONTROL_DPAD_RIGHT = 18,',
    'CONTROL_POWER = 7'
)
$ccInputSourceMasks = @(
    'enum CcInputSourceMask : uint32_t',
    'CC_INPUT_SOURCE_A = 0x80000000u,',
    'CC_INPUT_SOURCE_B = 0x00200000u,',
    'CC_INPUT_SOURCE_X = 0x00010000u,',
    'CC_INPUT_SOURCE_Y = 0x00000040u,',
    'CC_INPUT_SOURCE_START = 0x00000800u,',
    'CC_INPUT_SOURCE_SELECT = 0x00000400u,',
    'CC_INPUT_SOURCE_L = 0x00000100u,',
    'CC_INPUT_SOURCE_R = 0x20000000u,',
    'CC_INPUT_SOURCE_UP = 0x00100000u,',
    'CC_INPUT_SOURCE_DOWN = 0x08000000u,',
    'CC_INPUT_SOURCE_LEFT = 0x10000000u,',
    'CC_INPUT_SOURCE_RIGHT = 0x00040000u,',
    'CC_INPUT_SOURCE_POWER = 0x00000080u'
)
$runtimeRows = @(
    'ANDROID_SETTINGS_EXECUTION_MODE = 0,',
    'ANDROID_SETTINGS_CPU_CLOCK,',
    'ANDROID_SETTINGS_SPEED_SCALE,',
    'ANDROID_SETTINGS_OS_TIME_DELAY_SCALE,',
    'ANDROID_SETTINGS_CHEAT_MANAGER,',
    'ANDROID_SETTINGS_LANGUAGE,',
    'ANDROID_SETTINGS_RESTORE_DEFAULTS,',
    'ANDROID_SETTINGS_BACK,',
    'ANDROID_SETTINGS_ROW_COUNT'
)
$controllerMappingRows = @(
    'ANDROID_CONTROLLER_MAPPING_MENU = 0,',
    'ANDROID_CONTROLLER_MAPPING_A,',
    'ANDROID_CONTROLLER_MAPPING_B,',
    'ANDROID_CONTROLLER_MAPPING_X,',
    'ANDROID_CONTROLLER_MAPPING_Y,',
    'ANDROID_CONTROLLER_MAPPING_START,',
    'ANDROID_CONTROLLER_MAPPING_SELECT,',
    'ANDROID_CONTROLLER_MAPPING_LEFT_SHOULDER,',
    'ANDROID_CONTROLLER_MAPPING_RIGHT_SHOULDER,',
    'ANDROID_CONTROLLER_MAPPING_DPAD_UP,',
    'ANDROID_CONTROLLER_MAPPING_DPAD_DOWN,',
    'ANDROID_CONTROLLER_MAPPING_DPAD_LEFT,',
    'ANDROID_CONTROLLER_MAPPING_DPAD_RIGHT,',
    'ANDROID_CONTROLLER_MAPPING_RESET,',
    'ANDROID_CONTROLLER_MAPPING_BACK,',
    'ANDROID_CONTROLLER_MAPPING_ROW_COUNT'
)
$controllerMappingControls = @(
    'kControllerMenuActionBit,',
    'CONTROL_BUTTON_A,',
    'CONTROL_BUTTON_B,',
    'CONTROL_BUTTON_X,',
    'CONTROL_BUTTON_Y,',
    'CONTROL_BUTTON_START,',
    'CONTROL_BUTTON_SELECT,',
    'CONTROL_TRIGGER_LEFT,',
    'CONTROL_TRIGGER_RIGHT,',
    'CONTROL_DPAD_UP,',
    'CONTROL_DPAD_DOWN,',
    'CONTROL_DPAD_LEFT,',
    'CONTROL_DPAD_RIGHT'
)
$controllerMenuMapping = @(
    'static const uint32_t kControllerMenuActionMask = 1u << kControllerMenuActionBit;',
    'targetMask | (sourceMask & kControllerMenuActionMask)',
    'spec += "=Menu";',
    'uint32_t gameplayMask = mappingMask & ~kControllerMenuActionMask;',
    'updateGameControllerMenuAction();'
)
$noiseReductionValues = @(
    'DIGITAL_NOISE_REDUCTION_HIGH = 0,',
    'DIGITAL_NOISE_REDUCTION_MEDIUM,',
    'DIGITAL_NOISE_REDUCTION_LOW,',
    'DIGITAL_NOISE_REDUCTION_LEVEL_COUNT'
)
$noiseReductionText = @(
    'ANDROID_TEXT_AUDIO_DIGITAL_NOISE_REDUCTION,',
    'ANDROID_TEXT_AUDIO_DIGITAL_NOISE_REDUCTION_HIGH,',
    'ANDROID_TEXT_AUDIO_DIGITAL_NOISE_REDUCTION_MEDIUM,',
    'ANDROID_TEXT_AUDIO_DIGITAL_NOISE_REDUCTION_LOW,'
)
$audioApply = @(
    'audioOutputSetMasterVolumePercent(settings.audioVolumePercent);',
    'audioOutputSetBufferSamples(settings.audioBufferSamples);',
    'audioOutputSetBufferLatencyMode(settings.audioBufferLatency);',
    'audioOutputSetEffect(settings.audioEffect);',
    'audioOutputSetNoiseReduction(settings.digitalNoiseReduction);'
)
$managedIniSections = @(
    '"video", "audio", "input", "runtime", "cheats", "ui", "debug"'
)
$cpuClockValues = @(
    'static const char* const EMULATOR_CPU_CLOCK_VALUES[] = {',
    '"", "200000000", "336000000", "360000000", "400000000", "430000000"'
)
$videoIniWrite = @(
    'writeIniString("video", "anti_aliasing"',
    'writeIniString("video", "effect"',
    'writeIniInt("video", "brightness"',
    'writeIniInt("video", "contrast"',
    'writeIniInt("video", "gamma"',
    'writeIniInt("video", "saturation"',
    'writeIniString("video", "minimized_behavior"',
    'writeIniString("video", "screen_orientation"',
    'writeIniString("video", "screen_fill"',
    'writeIniBool("video", "show_fps"'
)
$inputIniWrite = @(
    'writeIniBool(',
    '"input", "system_ime_disabled"',
    'writeIniBool("input", "show_virtual_controls"',
    'writeIniInt("input", "virtual_control_scale"',
    'writeIniString("input", "virtual_dpad_type"',
    'writeIniString("input", "controller_mapping"',
    'writeIniString("input", "keyboard_mapping"'
)
$runtimeIniWrite = @(
    'writeIniString("runtime", "backend"',
    'writeIniString("runtime", "cpu_hz"',
    'writeIniString("runtime", "speed_scale"',
    'writeIniString("runtime", "ostimedly_scale"',
    'writeIniBool("runtime", "cheats_enabled"',
    'writeIniString("ui", "language"',
    'writeIniBool("debug", "profile"'
)
$runtimeIniLoad = @(
    'readIniString("runtime", "backend"',
    'readIniString("runtime", "cpu_hz"',
    'readIniString("runtime", "speed_scale"',
    'readIniString("runtime", "ostimedly_scale"',
    'readIniBool("runtime", "cheats_enabled"'
)

Assert-OrderedText 'native\core\config\settings\emulator_settings.h' $noiseReductionValues
Assert-OrderedText 'native\core\config\settings\emulator_settings.h' $cpuClockValues
Assert-OrderedText 'native\core\config\settings\emulator_settings.h' $videoFields
Assert-OrderedText 'native\core\config\settings\emulator_settings.h' $audioFields
Assert-OrderedText 'native\core\config\settings\emulator_settings.h' $inputFields
Assert-OrderedText 'native\core\config\settings\emulator_settings.h' $runtimeFields
Assert-OrderedText 'native\core\config\settings\emulator_settings.cpp' $audioDefaults
Assert-OrderedText 'native\core\config\settings\emulator_settings.cpp' $audioIniLoad
Assert-OrderedText 'native\core\config\settings\emulator_settings.cpp' $managedIniSections
Assert-OrderedText 'native\core\config\settings\emulator_settings.cpp' $videoIniWrite
Assert-OrderedText 'native\core\config\settings\emulator_settings.cpp' $audioIniWrite
Assert-OrderedText 'native\core\config\settings\emulator_settings.cpp' $inputIniWrite
Assert-OrderedText 'native\core\config\settings\emulator_settings.cpp' $runtimeIniLoad
Assert-OrderedText 'native\core\config\settings\emulator_settings.cpp' $runtimeIniWrite
Assert-OrderedText 'native\core\frontend\menu\menu_model.h' $videoRows
Assert-OrderedText 'native\core\frontend\menu\menu_model.h' $audioRows
Assert-OrderedText 'native\core\frontend\menu\menu_model.h' $inputRows
Assert-OrderedText 'native\core\frontend\menu\menu_model.h' $controllerMappingRows
Assert-OrderedText 'native\core\frontend\input\input_controls.h' $inputControlBits
Assert-OrderedText 'native\core\cc\hle\cc_input_mapping.h' $ccInputSourceMasks
Assert-OrderedText 'native\core\frontend\menu\menu_overlay.cpp' $controllerMappingControls
Assert-OrderedText 'native\core\frontend\menu\menu_model.h' $runtimeRows
Assert-OrderedText 'native\core\frontend\menu\menu_strings.h' $noiseReductionText
Assert-OrderedText 'native\core\frontend\shell\frontend_shell.cpp' $audioApply
Assert-OrderedText 'native\core\frontend\shell\frontend_shell.cpp' $controllerMenuMapping

Write-Host 'Settings, INI, enum, and menu order validation passed.'
