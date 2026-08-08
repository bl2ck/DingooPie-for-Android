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

$settingsHeader = Get-Content -LiteralPath (Join-Path $projectRoot 'native/core/config/emulator_settings.h') -Raw
$settingsSource = Get-Content -LiteralPath (Join-Path $projectRoot 'native/core/config/emulator_settings.cpp') -Raw
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
    'settings.audioEffect = AUDIO_EFFECT_OFF;',
    'settings.digitalNoiseReduction = DIGITAL_NOISE_REDUCTION_HIGH;',
    'settings.audioDisabled = false;'
)
$audioIniLoad = @(
    'readIniInt("audio", "volume_percent"',
    'readIniInt("audio", "buffer_samples"',
    'readIniString(',
    '"effect"',
    '"digital_noise_reduction"',
    'readIniBool("audio", "audio_disabled"'
)
$audioIniWrite = @(
    'writeIniInt("audio", "volume_percent"',
    'writeIniInt("audio", "buffer_samples"',
    'writeIniString("audio", "effect"',
    'writeIniString("audio", "digital_noise_reduction"',
    'writeIniBool("audio", "audio_disabled"'
)
$audioRows = @(
    'ANDROID_AUDIO_VOLUME = 0,',
    'ANDROID_AUDIO_BUFFER,',
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
    'ANDROID_INPUT_BACK,',
    'ANDROID_INPUT_ROW_COUNT'
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
    'mixerSetMasterVolumePercent(settings.audioVolumePercent);',
    'mixerSetBufferSamples(settings.audioBufferSamples);',
    'mixerSetAudioEffect(settings.audioEffect);',
    'mixerSetDigitalNoiseReduction(settings.digitalNoiseReduction);'
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

Assert-OrderedText 'native\core\config\emulator_settings.h' $noiseReductionValues
Assert-OrderedText 'native\core\config\emulator_settings.h' $cpuClockValues
Assert-OrderedText 'native\core\config\emulator_settings.h' $videoFields
Assert-OrderedText 'native\core\config\emulator_settings.h' $audioFields
Assert-OrderedText 'native\core\config\emulator_settings.h' $inputFields
Assert-OrderedText 'native\core\config\emulator_settings.h' $runtimeFields
Assert-OrderedText 'native\core\config\emulator_settings.cpp' $audioDefaults
Assert-OrderedText 'native\core\config\emulator_settings.cpp' $audioIniLoad
Assert-OrderedText 'native\core\config\emulator_settings.cpp' $managedIniSections
Assert-OrderedText 'native\core\config\emulator_settings.cpp' $videoIniWrite
Assert-OrderedText 'native\core\config\emulator_settings.cpp' $audioIniWrite
Assert-OrderedText 'native\core\config\emulator_settings.cpp' $inputIniWrite
Assert-OrderedText 'native\core\config\emulator_settings.cpp' $runtimeIniLoad
Assert-OrderedText 'native\core\config\emulator_settings.cpp' $runtimeIniWrite
Assert-OrderedText 'native\core\frontend\menu_model.h' $videoRows
Assert-OrderedText 'native\core\frontend\menu_model.h' $audioRows
Assert-OrderedText 'native\core\frontend\menu_model.h' $inputRows
Assert-OrderedText 'native\core\frontend\menu_model.h' $runtimeRows
Assert-OrderedText 'native\core\frontend\menu_strings.h' $noiseReductionText
Assert-OrderedText 'native\core\frontend\sdl_frontend.cpp' $audioApply

Write-Host 'Settings, INI, enum, and menu order validation passed.'
