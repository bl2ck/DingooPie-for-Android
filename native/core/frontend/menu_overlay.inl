#ifndef DINGOO_PIE_FRONTEND_MENU_OVERLAY_INL
#define DINGOO_PIE_FRONTEND_MENU_OVERLAY_INL

struct AndroidMenuRowContent
{
    std::string label;
    std::string value;
};

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

static std::string androidMenuString(AndroidMenuTextId id)
{
    UiLanguage language = g_frontendSettings ?
        g_frontendSettings->uiLanguage : UI_LANGUAGE_CHINESE;
    if (language < UI_LANGUAGE_CHINESE || language >= UI_LANGUAGE_COUNT)
    {
        language = UI_LANGUAGE_CHINESE;
    }
    std::string text = platformWideToUtf8(androidMenuText(language, id));
    size_t accelerator = text.find("(&");
    while (accelerator != std::string::npos)
    {
        size_t end = text.find(')', accelerator + 2);
        if (end == accelerator + 3)
            text.erase(accelerator, 4);
        else
            accelerator += 2;
        accelerator = text.find("(&", accelerator);
    }
    size_t escapedAmpersand = text.find("&&");
    while (escapedAmpersand != std::string::npos)
    {
        text.replace(escapedAmpersand, 2, "&");
        escapedAmpersand = text.find("&&", escapedAmpersand + 1);
    }
    return text;
}

static std::string androidBooleanValue(bool enabled)
{
    return androidChineseUi() ?
        (enabled ? u8"\u5f00\u542f" : u8"\u5173\u95ed") :
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

static void prepareAndroidCheatManagerGamePath(void)
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

static bool selectNextAndroidCheatManagerGamePath(void)
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

static bool setAndroidCheatManagerGlobalEnabled(bool enabled)
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

static bool setAndroidCheatManagerFeatureEnabled(size_t index, bool enabled)
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

static bool setAllAndroidCheatManagerFeaturesEnabled(bool enabled)
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
        u8"%u \u9879 / \u5df2\u542f\u7528 %u" : "%u features / %u enabled",
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
        if (row == ANDROID_MAIN_EXIT_APPLICATION) return { chinese ? kZhExitApp : "Exit app", "" };
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
        if (row == ANDROID_AUDIO_VOLUME)
            return { androidMenuString(ANDROID_TEXT_AUDIO_VOLUME), androidPercentValue(androidNormalizedIntPreset(
                g_frontendSettings->audioVolumePercent, EMULATOR_AUDIO_VOLUME_VALUES, 100)) };
        if (row == ANDROID_AUDIO_BUFFER)
            return { androidMenuString(ANDROID_TEXT_AUDIO_BUFFER), androidAudioBufferValue(androidNormalizedIntPreset(
                g_frontendSettings->audioBufferSamples, EMULATOR_AUDIO_BUFFER_VALUES, 2048)) };
        if (row == ANDROID_AUDIO_EFFECT)
        {
            int index = (int)g_frontendSettings->audioEffect;
            if (index < 0 || index >= AUDIO_EFFECT_MODE_COUNT) index = 0;
            return { androidMenuString(ANDROID_TEXT_AUDIO_EFFECT), androidMenuString(effectTextIds[index]) };
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
        if (row == ANDROID_INPUT_CONTROLLER_MAPPING)
            return { androidMenuString(ANDROID_TEXT_INPUT_CONTROLLER_MAPPING), " " };
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
        if (row == ANDROID_CONTROLLER_MAPPING_BACK)
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

static void requestAndroidSwitchGame(void)
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

static void requestAndroidRestartGame(void)
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

static void requestAndroidExitApplication(void)
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

static void handleAndroidMainMenuSelection(int row)
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

static void requestAndroidRestoreDefaultSettings(void);

static void handleAndroidOptionsSelection(int row)
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
    *g_frontendSettings = emulatorDefaultSettings();
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

static void handleAndroidDetailMenuSelection(AndroidMenuScreen screen, int row)
{
    if ((screen == ANDROID_MENU_VIDEO && row == ANDROID_VIDEO_BACK) ||
        (screen == ANDROID_MENU_AUDIO && row == ANDROID_AUDIO_BACK) ||
        (screen == ANDROID_MENU_INPUT && row == ANDROID_INPUT_BACK) ||
        (screen == ANDROID_MENU_CONTROLLER_MAPPING && row == ANDROID_CONTROLLER_MAPPING_BACK) ||
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
        case ANDROID_AUDIO_EFFECT:
            g_frontendSettings->audioEffect =
                (AudioEffectMode)nextAndroidEnumValue(
                    (int)g_frontendSettings->audioEffect,
                    AUDIO_EFFECT_MODE_COUNT,
                    AUDIO_EFFECT_OFF);
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
        case ANDROID_INPUT_CONTROLLER_MAPPING:
            openAndroidMenu(ANDROID_MENU_CONTROLLER_MAPPING);
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

static void drawAndroidMenuOverlay(void)
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
        drawAndroidRect(button, SDL_Color{ 0, 0, 0, 185 });
        drawAndroidOutline(button, SDL_Color{ 255, 255, 255, 235 });
        drawAndroidSystemTextCentered("MENU", button,
            androidCompactButtonTextSize(button), SDL_Color{ 255, 255, 255, 255 });
        return;
    }

    SDL_Rect dim = { 0, 0, width, height };
    drawAndroidRect(dim, SDL_Color{ 0, 0, 0, 185 });
    SDL_Rect panel = androidPanelRect(width, height);
    drawAndroidRect(panel, SDL_Color{ 0, 0, 0, 245 });
    drawAndroidOutline(panel, SDL_Color{ 160, 160, 160, 255 });

    bool chinese = androidChineseUi();
    std::string title = chinese ? kZhMenu : "Menu";
    if (g_androidMenuScreen == ANDROID_MENU_PAUSE)
        title = chinese ? kZhGameMenu : "Game menu";
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
            chinese ? kZhResume : "Resume",
            androidMenuString(ANDROID_TEXT_FILE_RESTART),
            chinese ? kZhSwitchGame : "Switch game",
            androidMenuString(ANDROID_TEXT_ROOT_OPTIONS),
            androidMenuString(ANDROID_TEXT_ROOT_SETTINGS),
            chinese ? kZhExitApp : "Exit app"
        };
        for (int row = 0; row < ANDROID_PAUSE_ROW_COUNT; ++row)
        {
            SDL_Rect rowRect = androidPanelRowRect(panel, row);
            drawAndroidRect(rowRect, SDL_Color{ 48, 48, 48, 240 });
            drawAndroidSystemTextCentered(rows[row].c_str(), rowRect, androidUiMetric(20),
                SDL_Color{ 255, 255, 255, 255 });
        }
    }
    else if (g_androidMenuScreen == ANDROID_MENU_ABOUT)
    {
        std::string version = androidAppVersionName();
        const std::string lines[] =
        {
            chinese ? std::string(u8"\u4e01\u679c\u6d3e DingooPie \u7248\u672c Android ") + version :
                std::string("DingooPie Version Android ") + version,
            chinese ? std::string(u8"\u4e01\u679c A320 / \u6b4c\u7f8e X760+ / \u6b4c\u7f8e A330 \u6e38\u620f\u6a21\u62df\u5668") :
                std::string("Dingoo A320 / Gemei X760+ / Gemei A330 game emulator"),
            chinese ? std::string(u8".app / .cc \u683c\u5f0f\u6587\u4ef6\u5f52\u4e01\u679c\u79d1\u6280\u6240\u6709\u3002") :
                std::string("The .app and .cc package formats belong to Dingoo Technology."),
            "Powered by BL2CK Software"
        };
        for (int index = 0; index < 4; ++index)
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
        SDL_Rect backRect = androidPanelRowRect(panel, ANDROID_ABOUT_BACK);
        drawAndroidRect(backRect, SDL_Color{ 48, 48, 48, 240 });
        drawAndroidSystemTextCentered(chinese ? kZhBack : "Back", backRect,
            androidUiMetric(20), SDL_Color{ 255, 255, 255, 255 });
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
            drawAndroidRect(rowRect, SDL_Color{ 48, 48, 48, 240 });
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

#endif
