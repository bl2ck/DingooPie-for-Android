#include "frontend/menu_strings.h"

const wchar_t* androidMenuText(UiLanguage language, AndroidMenuTextId id)
{
    bool zh = language == UI_LANGUAGE_CHINESE;
    switch (id)
    {
    case ANDROID_TEXT_FILE_RESTART:
        return zh ? L"\u91cd\u542f\u6e38\u620f(&R)" : L"Restart Game(&R)";
    case ANDROID_TEXT_CONFIRM_RESTART_GAME_TITLE:
        return zh ? L"\u91cd\u542f\u6e38\u620f" : L"Restart Game";
    case ANDROID_TEXT_CONFIRM_RESTART_GAME_BODY:
        return zh ? L"\u662f\u5426\u91cd\u542f\u5f53\u524d\u6e38\u620f\uff1f\u672a\u4fdd\u5b58\u7684\u8fdb\u5ea6\u5c06\u4f1a\u4e22\u5931\u3002" :
            L"Restart the current game? Unsaved progress will be lost.";
    case ANDROID_TEXT_CONFIRM_RESTART_GAME_ACCEPT:
        return zh ? L"\u91cd\u542f" : L"Restart";
    case ANDROID_TEXT_CONFIRM_EXIT_TITLE:
        return zh ? L"\u9000\u51fa\u6a21\u62df\u5668" : L"Exit Emulator";
    case ANDROID_TEXT_CONFIRM_EXIT_BODY:
        return zh ? L"\u662f\u5426\u9000\u51fa\u6a21\u62df\u5668\uff1f" : L"Exit the emulator?";
    case ANDROID_TEXT_CONFIRM_EXIT_ACCEPT:
        return zh ? L"\u9000\u51fa" : L"Exit";
    case ANDROID_TEXT_CONFIRM_SWITCH_GAME_TITLE:
        return zh ? L"\u5207\u6362\u6e38\u620f" : L"Switch Game";
    case ANDROID_TEXT_CONFIRM_SWITCH_GAME_BODY:
        return zh ? L"\u662f\u5426\u7ed3\u675f\u5f53\u524d\u6e38\u620f\u5e76\u8fd4\u56de\u6e38\u620f\u5217\u8868\uff1f" :
            L"End the current game and return to the game list?";
    case ANDROID_TEXT_CONFIRM_SWITCH_GAME_ACCEPT:
        return zh ? L"\u5207\u6362" : L"Switch";
    case ANDROID_TEXT_CONFIRM_CANCEL:
        return zh ? L"\u53d6\u6d88" : L"Cancel";
    case ANDROID_TEXT_ROOT_OPTIONS:
        return zh ? L"\u9009\u9879(&O)" : L"Options(&O)";
    case ANDROID_TEXT_ROOT_VIDEO:
        return zh ? L"\u89c6\u9891(&V)" : L"Video(&V)";
    case ANDROID_TEXT_VIDEO_ANTI_ALIASING:
        return zh ? L"\u6297\u952f\u9f7f(&A)" : L"Anti-aliasing(&A)";
    case ANDROID_TEXT_VIDEO_AA_OFF:
        return zh ? L"\u5173\u95ed" : L"Off";
    case ANDROID_TEXT_VIDEO_AA_LOW:
        return zh ? L"\u8f7b\u5ea6" : L"Low";
    case ANDROID_TEXT_VIDEO_AA_CLEAR:
        return zh ? L"\u6e05\u6670" : L"Clear";
    case ANDROID_TEXT_VIDEO_EFFECT:
        return zh ? L"\u6ee4\u955c(&E)" : L"Effect(&E)";
    case ANDROID_TEXT_VIDEO_EFFECT_NORMAL:
        return zh ? L"\u6b63\u5e38" : L"Normal";
    case ANDROID_TEXT_VIDEO_EFFECT_GRAYSCALE:
        return zh ? L"\u9ed1\u767d" : L"Black && White";
    case ANDROID_TEXT_VIDEO_EFFECT_INVERT:
        return zh ? L"\u53cd\u8272" : L"Invert";
    case ANDROID_TEXT_VIDEO_EFFECT_SOFT_BLUR:
        return zh ? L"\u67d4\u5316" : L"Soft Blur";
    case ANDROID_TEXT_VIDEO_EFFECT_SHARPEN:
        return zh ? L"\u9510\u5316" : L"Sharpen";
    case ANDROID_TEXT_VIDEO_EFFECT_VIVID:
        return zh ? L"\u8272\u5f69\u589e\u5f3a" : L"Vivid";
    case ANDROID_TEXT_VIDEO_EFFECT_SEPIA:
        return zh ? L"\u6000\u65e7\u8910\u8272" : L"Sepia";
    case ANDROID_TEXT_VIDEO_EFFECT_PIXEL_GRID:
        return zh ? L"\u50cf\u7d20\u7f51\u683c" : L"Pixel Grid";
    case ANDROID_TEXT_VIDEO_EFFECT_LCD_SCANLINE:
        return zh ? L"LCD \u626b\u63cf\u7ebf" : L"LCD Scanline";
    case ANDROID_TEXT_VIDEO_EFFECT_LIGHT_CRT:
        return zh ? L"\u8f7b\u91cf CRT" : L"Light CRT";
    case ANDROID_TEXT_VIDEO_BRIGHTNESS:
        return zh ? L"\u4eae\u5ea6(&B)" : L"Brightness(&B)";
    case ANDROID_TEXT_VIDEO_CONTRAST:
        return zh ? L"\u5bf9\u6bd4\u5ea6(&C)" : L"Contrast(&C)";
    case ANDROID_TEXT_VIDEO_GAMMA:
        return zh ? L"\u4f3d\u9a6c(&G)" : L"Gamma(&G)";
    case ANDROID_TEXT_VIDEO_SATURATION:
        return zh ? L"\u9971\u548c\u5ea6(&A)" : L"Saturation(&A)";
    case ANDROID_TEXT_VIDEO_MINIMIZED_BEHAVIOR:
        return zh ? L"\u6700\u5c0f\u5316\u65f6(&M)" : L"When Minimized(&M)";
    case ANDROID_TEXT_VIDEO_MINIMIZED_NORMAL:
        return zh ? L"\u6b63\u5e38\u8fd0\u884c" : L"Run Normally";
    case ANDROID_TEXT_VIDEO_MINIMIZED_PAUSE:
        return zh ? L"\u81ea\u52a8\u6682\u505c" : L"Auto Pause";
    case ANDROID_TEXT_VIDEO_MINIMIZED_THROTTLE:
        return zh ? L"\u964d\u4f4e\u5e27\u7387" : L"Throttle Frame Rate";
    case ANDROID_TEXT_VIDEO_SCREEN_ORIENTATION:
        return zh ? L"\u5c4f\u5e55\u65b9\u5411" : L"Screen Orientation";
    case ANDROID_TEXT_VIDEO_SCREEN_ORIENTATION_AUTO:
        return zh ? L"\u81ea\u52a8" : L"Auto";
    case ANDROID_TEXT_VIDEO_SCREEN_ORIENTATION_LANDSCAPE:
        return zh ? L"\u6a2a\u5c4f" : L"Landscape";
    case ANDROID_TEXT_VIDEO_SCREEN_ORIENTATION_PORTRAIT:
        return zh ? L"\u7ad6\u5c4f" : L"Portrait";
    case ANDROID_TEXT_VIDEO_SHOW_FPS:
        return zh ? L"\u663e\u793a FPS(&F)" : L"Show FPS(&F)";
    case ANDROID_TEXT_ROOT_AUDIO:
        return zh ? L"\u97f3\u9891(&A)" : L"Audio(&A)";
    case ANDROID_TEXT_AUDIO_VOLUME:
        return zh ? L"\u4e3b\u97f3\u91cf(&V)" : L"Master Volume(&V)";
    case ANDROID_TEXT_AUDIO_BUFFER:
        return zh ? L"\u97f3\u9891\u7f13\u51b2(&B)" : L"Audio Buffer(&B)";
    case ANDROID_TEXT_AUDIO_EFFECT:
        return zh ? L"\u97f3\u9891\u6548\u679c(&E)" : L"Audio Effect(&E)";
    case ANDROID_TEXT_AUDIO_EFFECT_OFF:
        return zh ? L"\u5173\u95ed" : L"Off";
    case ANDROID_TEXT_AUDIO_EFFECT_SOFT:
        return zh ? L"\u67d4\u548c" : L"Soft";
    case ANDROID_TEXT_AUDIO_EFFECT_CLEAR:
        return zh ? L"\u6e05\u4eae" : L"Clear";
    case ANDROID_TEXT_AUDIO_EFFECT_BASS_BOOST:
        return zh ? L"\u4f4e\u97f3\u589e\u5f3a" : L"Bass Boost";
    case ANDROID_TEXT_AUDIO_EFFECT_MONO:
        return zh ? L"\u5355\u58f0\u9053" : L"Mono";
    case ANDROID_TEXT_AUDIO_DISABLE:
        return zh ? L"\u7981\u7528\u97f3\u9891(&A)" : L"Disable Audio(&A)";
    case ANDROID_TEXT_ROOT_INPUT:
        return zh ? L"\u8f93\u5165(&I)" : L"Input(&I)";
    case ANDROID_TEXT_INPUT_SYSTEM_IME:
        return zh ? L"\u7981\u7528\u8f93\u5165\u6cd5(&M)" : L"Disable IME(&M)";
    case ANDROID_TEXT_INPUT_VIRTUAL_CONTROLS:
        return zh ? L"\u663e\u793a\u865a\u62df\u6309\u952e(&V)" : L"Show Virtual Controls(&V)";
    case ANDROID_TEXT_INPUT_CONTROLLER_MAPPING:
        return zh ? L"\u624b\u67c4\u6309\u952e\u6620\u5c04" : L"Controller Mapping";
    case ANDROID_TEXT_CONTROLLER_MAPPING_PRESS:
        return zh ? L"\u8bf7\u6309\u624b\u67c4\u6309\u952e..." : L"Press a controller input...";
    case ANDROID_TEXT_CONTROLLER_MAPPING_NO_DEVICE:
        return zh ? L"\u672a\u68c0\u6d4b\u5230 SDL GameController \u517c\u5bb9\u624b\u67c4\u3002" :
            L"No SDL GameController-compatible controller detected.";
    case ANDROID_TEXT_ROOT_SETTINGS:
        return zh ? L"\u8bbe\u7f6e(&S)" : L"Settings(&S)";
    case ANDROID_TEXT_SETTINGS_EXECUTION_MODE:
        return zh ? L"CPU \u6267\u884c\u6a21\u5f0f(&C)" : L"CPU Execution Mode(&C)";
    case ANDROID_TEXT_SETTINGS_EXECUTION_MODE_AUTO:
        return zh ? L"\u81ea\u52a8" : L"Auto";
    case ANDROID_TEXT_SETTINGS_EXECUTION_MODE_COMPATIBILITY:
        return zh ? L"\u517c\u5bb9" : L"Compatibility";
    case ANDROID_TEXT_SETTINGS_CPU_CLOCK:
        return zh ? L"CPU \u65f6\u949f(&H)" : L"CPU Clock(&H)";
    case ANDROID_TEXT_SETTINGS_AUTO:
        return zh ? L"\u81ea\u52a8" : L"Auto";
    case ANDROID_TEXT_SETTINGS_SPEED_SCALE:
        return zh ? L"\u8fd0\u884c\u901f\u5ea6(&R)" : L"Runtime Speed(&R)";
    case ANDROID_TEXT_SETTINGS_OS_TIME_DELAY_SCALE:
        return zh ? L"\u5ef6\u8fdf\u6bd4\u4f8b(&D)" : L"Delay Scale(&D)";
    case ANDROID_TEXT_SETTINGS_CHEAT_MANAGER:
        return zh ? L"\u91d1\u624b\u6307\u7ba1\u7406\u5668" : L"Cheat Manager";
    case ANDROID_TEXT_CHEAT_MANAGER_TITLE:
        return zh ? L"\u91d1\u624b\u6307\u7ba1\u7406\u5668" : L"Cheat Manager";
    case ANDROID_TEXT_CHEAT_MANAGER_ENABLE:
        return zh ? L"\u542f\u7528\u91d1\u624b\u6307" : L"Enable Cheats";
    case ANDROID_TEXT_CHEAT_MANAGER_FILE:
        return zh ? L"\u6587\u4ef6" : L"File";
    case ANDROID_TEXT_CHEAT_MANAGER_STATUS:
        return zh ? L"\u72b6\u6001" : L"Status";
    case ANDROID_TEXT_CHEAT_MANAGER_NO_GAME:
        return zh ? L"\u672a\u8fd0\u884c\u6e38\u620f" : L"No game running";
    case ANDROID_TEXT_CHEAT_MANAGER_NO_FILE:
        return zh ? L"\u65e0\u540c\u76ee\u5f55\u540c\u540d .cht" : L"No same-directory .cht";
    case ANDROID_TEXT_CHEAT_MANAGER_MISMATCH:
        return zh ? L"\u4e0e\u5f53\u524d\u6e38\u620f\u4e0d\u5339\u914d" : L"Game mismatch";
    case ANDROID_TEXT_CHEAT_MANAGER_ENABLE_ALL:
        return zh ? L"\u5168\u90e8\u542f\u7528" : L"Enable All";
    case ANDROID_TEXT_CHEAT_MANAGER_DISABLE_ALL:
        return zh ? L"\u5168\u90e8\u7981\u7528" : L"Disable All";
    case ANDROID_TEXT_CHEAT_MANAGER_APPLY:
        return zh ? L"\u7acb\u5373\u5e94\u7528" : L"Apply Now";
    case ANDROID_TEXT_CHEAT_MANAGER_REFRESH:
        return zh ? L"\u5237\u65b0" : L"Refresh";
    case ANDROID_TEXT_CHEAT_MANAGER_NOT_LOADED:
        return zh ? L"\u672a\u52a0\u8f7d" : L"Not loaded";
    case ANDROID_TEXT_CHEAT_MANAGER_UNNAMED:
        return zh ? L"\u672a\u547d\u540d" : L"Unnamed";
    case ANDROID_TEXT_SETTINGS_LANGUAGE:
        return zh ? L"\u8bed\u8a00(&L)" : L"Language(&L)";
    case ANDROID_TEXT_SETTINGS_RESET:
        return zh ? L"\u6062\u590d\u9ed8\u8ba4\u8bbe\u7f6e(&R)" : L"Restore Default Settings(&R)";
    case ANDROID_TEXT_SETTINGS_RESET_CONFIRM:
        return zh ? L"\u786e\u5b9a\u8981\u5c06\u6240\u6709\u914d\u7f6e\u6062\u590d\u4e3a\u9ed8\u8ba4\u8bbe\u7f6e\u5417\uff1f" :
            L"Restore all configuration items to their default settings?";
    case ANDROID_TEXT_SETTINGS_RESET_ACCEPT:
        return zh ? L"\u6062\u590d" : L"Restore";
    case ANDROID_TEXT_SETTINGS_RESET_CANCEL:
        return zh ? L"\u53d6\u6d88" : L"Cancel";
    case ANDROID_TEXT_SETTINGS_RESET_SUCCESS:
        return zh ? L"\u5df2\u6062\u590d\u9ed8\u8ba4\u8bbe\u7f6e\u3002" : L"Default settings have been restored.";
    case ANDROID_TEXT_SETTINGS_RESET_SAVE_FAILED:
        return zh ? L"\u5df2\u6062\u590d\u5f53\u524d\u8fd0\u884c\u914d\u7f6e\uff0c\u4f46\u4fdd\u5b58\u9ed8\u8ba4\u8bbe\u7f6e\u5931\u8d25\u3002" :
            L"The current settings were restored, but saving the defaults failed.";
    case ANDROID_TEXT_HELP_ABOUT:
        return zh ? L"\u5173\u4e8e(&A)" : L"About(&A)";
    case ANDROID_TEXT_ABOUT_TITLE:
        return zh ? L"\u5173\u4e8e \u4e01\u679c\u6d3e DingooPie" : L"About DingooPie";
    default:
        return L"";
    }
}
