#include "config/settings/emulator_settings.h"

#include "shared/game/game_paths.h"
#include "shared/services/guest_filesystem.h"
#include "shared/platform/storage_services.h"
#include "shared/diagnostics/runtime_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <ctype.h>
#include <utility>
#include <vector>

static std::string trimIniText(const std::string& text);

template <size_t Count>
static int normalizeIntPreset(int value, const int (&presets)[Count], int fallback)
{
    for (size_t index = 0; index < Count; ++index)
    {
        if (presets[index] == value)
        {
            return value;
        }
    }
    for (size_t index = 0; index < Count; ++index)
    {
        if (presets[index] == fallback)
        {
            return fallback;
        }
    }
    return presets[0];
}

template <size_t Count>
static std::string normalizeStringPreset(
    const std::string& value, const char* const (&presets)[Count],
    const std::string& fallback)
{
    for (size_t index = 0; index < Count; ++index)
    {
        if (value == presets[index])
        {
            return value;
        }
    }
    return fallback;
}

static bool parseBoolText(const char* text, bool fallback)
{
    if (!text || !text[0])
    {
        return fallback;
    }
    if (strcmp(text, "1") == 0)
    {
        return true;
    }
    if (strcmp(text, "0") == 0)
    {
        return false;
    }
    return fallback;
}

static bool parseIntText(const std::string& text, int* out)
{
    if (!out || text.empty())
    {
        return false;
    }

    char* end = NULL;
    errno = 0;
    long parsed = strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE)
    {
        return false;
    }
    if (parsed < INT32_MIN || parsed > INT32_MAX)
    {
        return false;
    }

    *out = (int)parsed;
    return true;
}

static bool parseDoubleText(const std::string& text, double* out)
{
    if (!out || text.empty())
    {
        return false;
    }

    char* end = NULL;
    errno = 0;
    double parsed = strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE || parsed != parsed)
    {
        return false;
    }

    *out = parsed;
    return true;
}

static bool stringEqualsIgnoreCase(const std::string& value, const char* expected)
{
    if (!expected)
    {
        return value.empty();
    }
    if (value.size() != strlen(expected))
    {
        return false;
    }
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (tolower((unsigned char)value[i]) != tolower((unsigned char)expected[i]))
        {
            return false;
        }
    }
    return true;
}

static RuntimeExecutionMode normalizeExecutionMode(
    const std::string& value,
    RuntimeExecutionMode fallback)
{
    bool recognized = false;
    RuntimeExecutionMode mode = runtimeExecutionModeFromName(value.c_str(), &recognized);
    return recognized ? mode : fallback;
}

static std::string normalizeCpuClockHz(const std::string& value, const std::string& fallback)
{
    return normalizeStringPreset(value, EMULATOR_CPU_CLOCK_VALUES, fallback);
}

static std::string normalizeScaleValue(const std::string& value, const std::string& fallback)
{
    if (value.empty())
    {
        return "";
    }

    double parsed = 0.0;
    if (!parseDoubleText(value, &parsed) || parsed < 0.20 || parsed > 1.0)
    {
        return fallback;
    }

    double scaled = parsed * 100.0;
    int percent = (int)(scaled + 0.5);
    double diff = scaled - (double)percent;
    if (diff < 0.0)
    {
        diff = -diff;
    }
    if (diff > 0.0005 || percent < 20 || percent > 100 || (percent % 5) != 0)
    {
        return fallback;
    }

    char normalized[8] = {};
    if (percent == 100)
    {
        snprintf(normalized, sizeof(normalized), "1.0");
    }
    else
    {
        snprintf(normalized, sizeof(normalized), "0.%02d", percent);
    }
    return normalizeStringPreset(normalized, EMULATOR_SCALE_VALUES, fallback);
}

static AntiAliasingMode parseAntiAliasingMode(const std::string& value, AntiAliasingMode fallback)
{
    if (value.empty())
    {
        return fallback;
    }
    if (strcasecmp(value.c_str(), "off") == 0)
    {
        return ANTI_ALIASING_OFF;
    }
    if (strcasecmp(value.c_str(), "low") == 0)
    {
        return ANTI_ALIASING_LOW;
    }
    if (strcasecmp(value.c_str(), "clear") == 0)
    {
        return ANTI_ALIASING_CLEAR;
    }
    return fallback;
}

static ColorEffectMode parseColorEffectMode(const std::string& value, ColorEffectMode fallback)
{
    if (value.empty())
    {
        return fallback;
    }
    if (strcasecmp(value.c_str(), "normal") == 0)
    {
        return COLOR_EFFECT_NORMAL;
    }
    if (strcasecmp(value.c_str(), "grayscale") == 0)
    {
        return COLOR_EFFECT_GRAYSCALE;
    }
    if (strcasecmp(value.c_str(), "invert") == 0)
    {
        return COLOR_EFFECT_INVERT;
    }
    if (strcasecmp(value.c_str(), "soft_blur") == 0)
    {
        return COLOR_EFFECT_SOFT_BLUR;
    }
    if (strcasecmp(value.c_str(), "sharpen") == 0)
    {
        return COLOR_EFFECT_SHARPEN;
    }
    if (strcasecmp(value.c_str(), "vivid") == 0)
    {
        return COLOR_EFFECT_VIVID;
    }
    if (strcasecmp(value.c_str(), "sepia") == 0)
    {
        return COLOR_EFFECT_SEPIA;
    }
    if (strcasecmp(value.c_str(), "pixel_grid") == 0)
    {
        return COLOR_EFFECT_PIXEL_GRID;
    }
    if (strcasecmp(value.c_str(), "lcd_scanline") == 0)
    {
        return COLOR_EFFECT_LCD_SCANLINE;
    }
    if (strcasecmp(value.c_str(), "light_crt") == 0)
    {
        return COLOR_EFFECT_LIGHT_CRT;
    }
    return fallback;
}

static bool audioEffectModeKnown(AudioEffectMode value)
{
    switch (value)
    {
    case AUDIO_EFFECT_OFF:
    case AUDIO_EFFECT_SOFT:
    case AUDIO_EFFECT_CLEAR:
    case AUDIO_EFFECT_BASS_BOOST:
    case AUDIO_EFFECT_MONO:
        return true;
    default:
        return false;
    }
}

static AudioEffectMode normalizeAudioEffectMode(AudioEffectMode value, AudioEffectMode fallback)
{
    if (audioEffectModeKnown(value))
    {
        return value;
    }
    return audioEffectModeKnown(fallback) ? fallback : AUDIO_EFFECT_OFF;
}

static AudioEffectMode parseAudioEffectMode(const std::string& value, AudioEffectMode fallback)
{
    fallback = normalizeAudioEffectMode(fallback, AUDIO_EFFECT_OFF);
    if (value.empty())
    {
        return fallback;
    }
    if (strcasecmp(value.c_str(), "off") == 0)
    {
        return AUDIO_EFFECT_OFF;
    }
    if (strcasecmp(value.c_str(), "soft") == 0)
    {
        return AUDIO_EFFECT_SOFT;
    }
    if (strcasecmp(value.c_str(), "clear") == 0)
    {
        return AUDIO_EFFECT_CLEAR;
    }
    if (strcasecmp(value.c_str(), "bass_boost") == 0)
    {
        return AUDIO_EFFECT_BASS_BOOST;
    }
    if (strcasecmp(value.c_str(), "mono") == 0)
    {
        return AUDIO_EFFECT_MONO;
    }
    return fallback;
}

static DigitalNoiseReductionLevel normalizeDigitalNoiseReductionLevel(
    DigitalNoiseReductionLevel level, DigitalNoiseReductionLevel fallback)
{
    if (level >= DIGITAL_NOISE_REDUCTION_HIGH &&
        level < DIGITAL_NOISE_REDUCTION_LEVEL_COUNT)
    {
        return level;
    }
    return fallback >= DIGITAL_NOISE_REDUCTION_HIGH &&
        fallback < DIGITAL_NOISE_REDUCTION_LEVEL_COUNT ?
        fallback : DIGITAL_NOISE_REDUCTION_HIGH;
}

static DigitalNoiseReductionLevel parseDigitalNoiseReductionLevel(
    const std::string& value, DigitalNoiseReductionLevel fallback)
{
    fallback = normalizeDigitalNoiseReductionLevel(
        fallback, DIGITAL_NOISE_REDUCTION_HIGH);
    if (strcasecmp(value.c_str(), "high") == 0)
    {
        return DIGITAL_NOISE_REDUCTION_HIGH;
    }
    if (strcasecmp(value.c_str(), "medium") == 0)
    {
        return DIGITAL_NOISE_REDUCTION_MEDIUM;
    }
    if (strcasecmp(value.c_str(), "low") == 0)
    {
        return DIGITAL_NOISE_REDUCTION_LOW;
    }
    return fallback;
}

static MinimizedBehavior parseMinimizedBehavior(const std::string& value, MinimizedBehavior fallback)
{
    if (fallback < MINIMIZED_BEHAVIOR_NORMAL || fallback >= MINIMIZED_BEHAVIOR_COUNT)
    {
        fallback = MINIMIZED_BEHAVIOR_PAUSE;
    }
    if (value.empty())
    {
        return fallback;
    }
    if (strcasecmp(value.c_str(), "normal") == 0)
    {
        return MINIMIZED_BEHAVIOR_NORMAL;
    }
    if (strcasecmp(value.c_str(), "pause") == 0)
    {
        return MINIMIZED_BEHAVIOR_PAUSE;
    }
    if (strcasecmp(value.c_str(), "throttle") == 0)
    {
        return MINIMIZED_BEHAVIOR_THROTTLE;
    }
    return fallback;
}

static ScreenOrientationMode parseScreenOrientationMode(
    const std::string& value, ScreenOrientationMode fallback)
{
    if (fallback < SCREEN_ORIENTATION_AUTO || fallback >= SCREEN_ORIENTATION_MODE_COUNT)
    {
        fallback = SCREEN_ORIENTATION_LANDSCAPE;
    }
    if (value.empty())
    {
        return fallback;
    }
    if (strcasecmp(value.c_str(), "auto") == 0)
    {
        return SCREEN_ORIENTATION_AUTO;
    }
    if (strcasecmp(value.c_str(), "landscape") == 0)
    {
        return SCREEN_ORIENTATION_LANDSCAPE;
    }
    if (strcasecmp(value.c_str(), "portrait") == 0)
    {
        return SCREEN_ORIENTATION_PORTRAIT;
    }
    return fallback;
}

static ScreenFillMode parseScreenFill(
    const std::string& value, ScreenFillMode fallback)
{
    if (fallback < SCREEN_FILL_ASPECT || fallback >= SCREEN_FILL_COUNT)
    {
        fallback = SCREEN_FILL_ASPECT;
    }
    if (strcasecmp(value.c_str(), "blurred") == 0)
    {
        return SCREEN_FILL_BLURRED_EXTENSION;
    }
    if (strcasecmp(value.c_str(), "stretch") == 0)
    {
        return SCREEN_FILL_STRETCH;
    }
    if (strcasecmp(value.c_str(), "aspect") == 0)
    {
        return SCREEN_FILL_ASPECT;
    }
    return fallback;
}

static VirtualDpadType parseVirtualDpadType(
    const std::string& value, VirtualDpadType fallback)
{
    if (fallback < VIRTUAL_DPAD_JOYSTICK || fallback >= VIRTUAL_DPAD_TYPE_COUNT)
    {
        fallback = VIRTUAL_DPAD_JOYSTICK;
    }
    if (strcasecmp(value.c_str(), "segmented_ring") == 0)
    {
        return VIRTUAL_DPAD_SEGMENTED_RING;
    }
    if (strcasecmp(value.c_str(), "joystick") == 0)
    {
        return VIRTUAL_DPAD_JOYSTICK;
    }
    return fallback;
}

static int normalizeAudioBufferSamples(int value, int fallback)
{
    switch (value)
    {
    case 512:
    case 1024:
    case 2048:
    case 4096:
    case 8192:
        return value;
    default:
        return fallback;
    }
}

static AudioBufferLatencyMode normalizeAudioBufferLatencyMode(
    AudioBufferLatencyMode value, AudioBufferLatencyMode fallback)
{
    if (value >= AUDIO_BUFFER_LATENCY_AUTO && value < AUDIO_BUFFER_LATENCY_MODE_COUNT)
    {
        return value;
    }
    return fallback >= AUDIO_BUFFER_LATENCY_AUTO && fallback < AUDIO_BUFFER_LATENCY_MODE_COUNT ?
        fallback : AUDIO_BUFFER_LATENCY_AUTO;
}

static AudioBufferLatencyMode parseAudioBufferLatencyMode(
    const std::string& value, AudioBufferLatencyMode fallback)
{
    fallback = normalizeAudioBufferLatencyMode(fallback, AUDIO_BUFFER_LATENCY_AUTO);
    if (strcasecmp(value.c_str(), "auto") == 0)
    {
        return AUDIO_BUFFER_LATENCY_AUTO;
    }
    if (strcasecmp(value.c_str(), "110ms") == 0) return AUDIO_BUFFER_LATENCY_110MS;
    if (strcasecmp(value.c_str(), "120ms") == 0) return AUDIO_BUFFER_LATENCY_120MS;
    if (strcasecmp(value.c_str(), "130ms") == 0) return AUDIO_BUFFER_LATENCY_130MS;
    if (strcasecmp(value.c_str(), "140ms") == 0) return AUDIO_BUFFER_LATENCY_140MS;
    if (strcasecmp(value.c_str(), "150ms") == 0) return AUDIO_BUFFER_LATENCY_150MS;
    return fallback;
}

static UiLanguage parseUiLanguage(const std::string& value, UiLanguage fallback)
{
    if (fallback < UI_LANGUAGE_CHINESE || fallback >= UI_LANGUAGE_COUNT)
    {
        fallback = UI_LANGUAGE_CHINESE;
    }
    if (value.empty())
    {
        return fallback;
    }
    if (strcasecmp(value.c_str(), "english") == 0)
    {
        return UI_LANGUAGE_ENGLISH;
    }
    if (strcasecmp(value.c_str(), "chinese") == 0)
    {
        return UI_LANGUAGE_CHINESE;
    }
    return fallback;
}

static std::string cheatSelectionKeyForGame(const std::string& gamePath)
{
    return gameCheatFileNameFromPath(gamePath);
}

static bool cheatSelectionKeysMatch(const std::string& a, const std::string& b)
{
    return stringEqualsIgnoreCase(a, b.c_str());
}

static int hexDigitValue(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return ch - 'A' + 10;
    }
    return -1;
}

static std::string encodeCheatFeatureKey(const std::string& key)
{
    // Escape only the delimiters used by this INI value; UTF-8 names stay
    // readable so users can still inspect or edit the file by hand.
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (size_t i = 0; i < key.size(); ++i)
    {
        unsigned char ch = (unsigned char)key[i];
        if (ch == '%' || ch == '|')
        {
            out.push_back('%');
            out.push_back(kHex[ch >> 4]);
            out.push_back(kHex[ch & 0x0f]);
        }
        else
        {
            out.push_back((char)ch);
        }
    }
    return out;
}

static std::string decodeCheatFeatureKey(const std::string& key)
{
    std::string out;
    for (size_t i = 0; i < key.size(); ++i)
    {
        if (key[i] == '%' && i + 2 < key.size())
        {
            int hi = hexDigitValue(key[i + 1]);
            int lo = hexDigitValue(key[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                out.push_back((char)((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(key[i]);
    }
    return out;
}

static std::string encodeCheatFeatureKeys(const std::vector<std::string>& featureKeys)
{
    // The [cheats] section stores stable feature keys from the loaded .cht
    // file, not localized labels selected by the current UI language.
    std::string out;
    for (size_t i = 0; i < featureKeys.size(); ++i)
    {
        if (featureKeys[i].empty())
        {
            continue;
        }
        if (!out.empty())
        {
            out.push_back('|');
        }
        out += encodeCheatFeatureKey(featureKeys[i]);
    }
    return out;
}

static std::vector<std::string> decodeCheatFeatureKeys(const std::string& text)
{
    std::vector<std::string> keys;
    size_t begin = 0;
    while (begin <= text.size())
    {
        size_t sep = text.find('|', begin);
        std::string part = sep == std::string::npos ?
            text.substr(begin) : text.substr(begin, sep - begin);
        part = decodeCheatFeatureKey(trimIniText(part));
        if (!part.empty())
        {
            keys.push_back(part);
        }
        if (sep == std::string::npos)
        {
            break;
        }
        begin = sep + 1;
    }
    return keys;
}

static std::string trimIniText(const std::string& text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' || text[begin] == '\n'))
    {
        begin++;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n'))
    {
        end--;
    }
    return text.substr(begin, end - begin);
}

static bool readTextFileUtf8(const std::string& path, std::string* out)
{
    if (!out)
    {
        return false;
    }
    out->clear();

    FILE* file = fopen(path.c_str(), "rb");
    if (!file)
    {
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return false;
    }

    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return false;
    }
    if (size == 0)
    {
        fclose(file);
        return true;
    }
    std::vector<char> bytes((size_t)size);
    bool ok = fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    fclose(file);
    if (!ok)
    {
        return false;
    }
    out->assign(bytes.begin(), bytes.end());
    if (out->size() >= 3 && (uint8_t)(*out)[0] == 0xef && (uint8_t)(*out)[1] == 0xbb && (uint8_t)(*out)[2] == 0xbf)
    {
        out->erase(0, 3);
    }
    return true;
}

static bool writeTextFileUtf8(const std::string& path, const std::string& text)
{
    FILE* file = fopen(path.c_str(), "wb");
    if (!file)
    {
        return false;
    }
    bool ok = text.empty() || fwrite(text.data(), 1, text.size(), file) == text.size();
    if (fclose(file) != 0)
    {
        ok = false;
    }
    return ok;
}

static std::string settingsTemporaryPath(const std::string& path)
{
    return path + ".tmp";
}

static std::string settingsBackupPath(const std::string& path)
{
    return path + ".backup";
}

static bool restoreSettingsFile(const std::string& sourcePath, const std::string& path)
{
    if (rename(sourcePath.c_str(), path.c_str()) == 0)
    {
        return true;
    }
    std::string text;
    if (!readTextFileUtf8(sourcePath, &text) || text.empty() ||
        !writeTextFileUtf8(path, text))
    {
        return false;
    }
    remove(sourcePath.c_str());
    return true;
}

static void recoverSettingsTransaction(const std::string& path)
{
    std::string currentText;
    if (readTextFileUtf8(path, &currentText) && !currentText.empty())
    {
        remove(settingsTemporaryPath(path).c_str());
        remove(settingsBackupPath(path).c_str());
        return;
    }

    const std::string backupPath = settingsBackupPath(path);
    const std::string temporaryPath = settingsTemporaryPath(path);
    std::string recoveryText;
    if (readTextFileUtf8(backupPath, &recoveryText) && !recoveryText.empty())
    {
        if (restoreSettingsFile(backupPath, path))
        {
            remove(temporaryPath.c_str());
            return;
        }
    }
    if (readTextFileUtf8(temporaryPath, &recoveryText) && !recoveryText.empty())
    {
        restoreSettingsFile(temporaryPath, path);
    }
}

static std::string readIniString(const char* section, const char* key, const char* fallback, const std::string& path)
{
    std::string text;
    if (!readTextFileUtf8(path, &text))
    {
        return fallback ? fallback : "";
    }

    std::string currentSection;
    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t lineEnd = text.find('\n', pos);
        std::string line = lineEnd == std::string::npos ? text.substr(pos) : text.substr(pos, lineEnd - pos);
        pos = lineEnd == std::string::npos ? text.size() + 1 : lineEnd + 1;

        line = trimIniText(line);
        if (line.empty() || line[0] == ';' || line[0] == '#')
        {
            continue;
        }
        if (line.front() == '[' && line.back() == ']')
        {
            currentSection = trimIniText(line.substr(1, line.size() - 2));
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }
        std::string itemKey = trimIniText(line.substr(0, eq));
        std::string itemValue = trimIniText(line.substr(eq + 1));
        if (stringEqualsIgnoreCase(currentSection, section) &&
            stringEqualsIgnoreCase(itemKey, key))
        {
            return itemValue;
        }
    }

    return fallback ? fallback : "";
}

static int readIniInt(const char* section, const char* key, int fallback, const std::string& path)
{
    std::string text = readIniString(section, key, "", path);
    int parsed = 0;
    return parseIntText(text, &parsed) ? parsed : fallback;
}

static bool writeIniString(const char* section, const char* key, const std::string& value, const std::string& path);

static bool readIniBool(
    const char* section,
    const char* key,
    bool fallback,
    const std::string& path)
{
    std::string value = readIniString(section, key, fallback ? "1" : "0", path);
    return parseBoolText(value.c_str(), fallback);
}

static bool writeIniBool(
    const char* section,
    const char* key,
    bool value,
    const std::string& path)
{
    return writeIniString(section, key, value ? "1" : "0", path);
}

static bool writeIniInt(
    const char* section,
    const char* key,
    int value,
    const std::string& path)
{
    return writeIniString(section, key, std::to_string(value), path);
}
static std::vector<std::pair<std::string, std::string> > readIniSectionValues(
    const char* section,
    const std::string& path)
{
    std::vector<std::pair<std::string, std::string> > values;
    std::string text;
    if (!section || !readTextFileUtf8(path, &text))
    {
        return values;
    }

    std::string currentSection;
    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t lineEnd = text.find('\n', pos);
        std::string line = lineEnd == std::string::npos ? text.substr(pos) : text.substr(pos, lineEnd - pos);
        pos = lineEnd == std::string::npos ? text.size() + 1 : lineEnd + 1;

        line = trimIniText(line);
        if (line.empty() || line[0] == ';' || line[0] == '#')
        {
            continue;
        }
        if (line.front() == '[' && line.back() == ']')
        {
            currentSection = trimIniText(line.substr(1, line.size() - 2));
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos ||
            !stringEqualsIgnoreCase(currentSection, section))
        {
            continue;
        }
        values.push_back(std::make_pair(
            trimIniText(line.substr(0, eq)),
            trimIniText(line.substr(eq + 1))));
    }

    return values;
}

static bool writeIniString(const char* section, const char* key, const std::string& value, const std::string& path)
{
    if (!section || !section[0] || !key || !key[0] || path.empty())
    {
        return false;
    }

    std::string text;
    readTextFileUtf8(path, &text);
    std::vector<std::string> lines;
    size_t position = 0;
    while (position < text.size())
    {
        size_t lineEnd = text.find('\n', position);
        std::string line = lineEnd == std::string::npos ?
            text.substr(position) : text.substr(position, lineEnd - position);
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        lines.push_back(line);
        if (lineEnd == std::string::npos)
        {
            break;
        }
        position = lineEnd + 1;
    }

    size_t sectionIndex = lines.size();
    size_t sectionEnd = lines.size();
    size_t keyIndex = lines.size();
    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::string trimmed = trimIniText(lines[i]);
        if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']')
        {
            std::string currentSection = trimIniText(trimmed.substr(1, trimmed.size() - 2));
            if (sectionIndex != lines.size())
            {
                sectionEnd = i;
                break;
            }
            if (stringEqualsIgnoreCase(currentSection, section))
            {
                sectionIndex = i;
            }
            continue;
        }
        if (sectionIndex == lines.size())
        {
            continue;
        }
        size_t equals = trimmed.find('=');
        if (equals != std::string::npos &&
            stringEqualsIgnoreCase(trimIniText(trimmed.substr(0, equals)), key))
        {
            keyIndex = i;
            break;
        }
    }

    std::string newLine = std::string(key) + "=" + value;
    if (keyIndex != lines.size())
    {
        lines[keyIndex] = newLine;
    }
    else if (sectionIndex != lines.size())
    {
        lines.insert(lines.begin() + sectionEnd, newLine);
    }
    else
    {
        if (!lines.empty() && !lines.back().empty())
        {
            lines.push_back(std::string());
        }
        lines.push_back(std::string("[") + section + "]");
        lines.push_back(newLine);
    }

    FILE* file = fopen(path.c_str(), "wb");
    if (!file)
    {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (fwrite(lines[i].data(), 1, lines[i].size(), file) != lines[i].size() ||
            fwrite("\n", 1, 1, file) != 1)
        {
            ok = false;
            break;
        }
    }
    if (fclose(file) != 0)
    {
        ok = false;
    }
    return ok;
}

static bool removeIniSection(const char* section, const std::string& path)
{
    if (!section || !section[0] || path.empty())
    {
        return false;
    }

    std::string text;
    if (!readTextFileUtf8(path, &text))
    {
        return true;
    }

    std::vector<std::string> lines;
    bool removing = false;
    bool found = false;
    size_t position = 0;
    while (position < text.size())
    {
        size_t lineEnd = text.find('\n', position);
        std::string line = lineEnd == std::string::npos ?
            text.substr(position) : text.substr(position, lineEnd - position);
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        std::string trimmed = trimIniText(line);
        bool sectionHeader = trimmed.size() >= 2 &&
            trimmed.front() == '[' && trimmed.back() == ']';
        if (sectionHeader)
        {
            std::string currentSection = trimIniText(
                trimmed.substr(1, trimmed.size() - 2));
            removing = stringEqualsIgnoreCase(currentSection, section);
            found = found || removing;
        }
        if (!removing)
        {
            lines.push_back(line);
        }
        if (lineEnd == std::string::npos)
        {
            break;
        }
        position = lineEnd + 1;
    }

    if (!found)
    {
        return true;
    }
    while (!lines.empty() && lines.back().empty())
    {
        lines.pop_back();
    }

    FILE* file = fopen(path.c_str(), "wb");
    if (!file)
    {
        return false;
    }
    bool ok = true;
    for (size_t index = 0; index < lines.size(); ++index)
    {
        if (fwrite(lines[index].data(), 1, lines[index].size(), file) != lines[index].size() ||
            fwrite("\n", 1, 1, file) != 1)
        {
            ok = false;
            break;
        }
    }
    if (fclose(file) != 0)
    {
        ok = false;
    }
    return ok;
}

static void setEnvValue(const char* name, const std::string& value)
{
    if (value.empty())
    {
        unsetenv(name);
    }
    else
    {
        setenv(name, value.c_str(), 1);
    }
}

static bool externalBackendOverrideEnabled(void)
{
    static const bool enabled = []() {
        const char* value = getenv("DINGOO_PIE_BACKEND");
        return value && value[0];
    }();
    return enabled;
}

EmulatorSettings emulatorDefaultSettings(void)
{
    EmulatorSettings settings;
    settings.antiAliasing = ANTI_ALIASING_OFF;
    settings.colorEffect = COLOR_EFFECT_NORMAL;
    settings.brightnessPercent = 100;
    settings.contrastPercent = 100;
    settings.gammaPercent = 100;
    settings.saturationPercent = 100;
    settings.minimizedBehavior = MINIMIZED_BEHAVIOR_PAUSE;
    settings.screenOrientationMode = SCREEN_ORIENTATION_LANDSCAPE;
    settings.screenFill = SCREEN_FILL_ASPECT;
    settings.showFps = false;

    settings.audioVolumePercent = 100;
    settings.audioBufferSamples = 2048;
    settings.audioBufferLatency = AUDIO_BUFFER_LATENCY_AUTO;
    settings.audioEffect = AUDIO_EFFECT_OFF;
    settings.digitalNoiseReduction = DIGITAL_NOISE_REDUCTION_HIGH;
    settings.audioDisabled = false;

    settings.systemImeDisabled = true;
    settings.showVirtualControls = true;
    settings.virtualControlScalePercent = 100;
    settings.virtualDpadType = VIRTUAL_DPAD_JOYSTICK;
    settings.controllerMapping = "";
    settings.controllerCalibration = "";
    settings.keyboardMapping = "";

    settings.executionMode = RUNTIME_EXECUTION_MODE_AUTOMATIC;
    settings.cpuClockHz = "";
    settings.runtimeSpeedScale = "";
    settings.osTimeDelayScale = "";
    settings.cheatsEnabled = false;
    settings.cheatSelections.clear();

    settings.uiLanguage = UI_LANGUAGE_CHINESE;

    settings.debugProfile = false;
    settings.portraitMode = false;
    return settings;
}

std::string emulatorSettingsPath(void)
{
    return "/data/user/0/com.dingoopie.android/files/DingooPie.ini";
}

EmulatorSettings emulatorLoadSettings(void)
{
    EmulatorSettings defaults = emulatorDefaultSettings();
    std::string path = emulatorSettingsPath();
    recoverSettingsTransaction(path);
    EmulatorSettings settings = defaults;
    std::string antiAliasing = readIniString("video", "anti_aliasing", emulatorAntiAliasingName(defaults.antiAliasing), path);
    settings.antiAliasing = parseAntiAliasingMode(antiAliasing, defaults.antiAliasing);
    std::string effect = readIniString("video", "effect", emulatorColorEffectName(defaults.colorEffect), path);
    settings.colorEffect = parseColorEffectMode(effect, defaults.colorEffect);
    settings.brightnessPercent = normalizeIntPreset(
        readIniInt("video", "brightness", defaults.brightnessPercent, path),
        EMULATOR_VIDEO_PERCENT_VALUES, defaults.brightnessPercent);
    settings.contrastPercent = normalizeIntPreset(
        readIniInt("video", "contrast", defaults.contrastPercent, path),
        EMULATOR_VIDEO_PERCENT_VALUES, defaults.contrastPercent);
    settings.gammaPercent = normalizeIntPreset(
        readIniInt("video", "gamma", defaults.gammaPercent, path),
        EMULATOR_VIDEO_PERCENT_VALUES, defaults.gammaPercent);
    settings.saturationPercent = normalizeIntPreset(
        readIniInt("video", "saturation", defaults.saturationPercent, path),
        EMULATOR_VIDEO_PERCENT_VALUES, defaults.saturationPercent);
    std::string minimizedBehavior = readIniString("video", "minimized_behavior", emulatorMinimizedBehaviorName(defaults.minimizedBehavior), path);
    settings.minimizedBehavior = parseMinimizedBehavior(minimizedBehavior, defaults.minimizedBehavior);
    settings.screenOrientationMode = parseScreenOrientationMode(
        readIniString("video", "screen_orientation",
            emulatorScreenOrientationName(defaults.screenOrientationMode), path),
        defaults.screenOrientationMode);
    settings.screenFill = parseScreenFill(
        readIniString("video", "screen_fill",
            emulatorScreenFillName(defaults.screenFill), path),
        defaults.screenFill);
    settings.showFps = readIniBool("video", "show_fps", defaults.showFps, path);

    settings.audioVolumePercent = normalizeIntPreset(
        readIniInt("audio", "volume_percent", defaults.audioVolumePercent, path),
        EMULATOR_AUDIO_VOLUME_VALUES, defaults.audioVolumePercent);
    settings.audioBufferSamples = normalizeAudioBufferSamples(
        readIniInt("audio", "buffer_samples", defaults.audioBufferSamples, path),
        defaults.audioBufferSamples);
    settings.audioBufferLatency = parseAudioBufferLatencyMode(
        readIniString("audio", "buffer_latency", emulatorAudioBufferLatencyName(defaults.audioBufferLatency), path),
        defaults.audioBufferLatency);
    std::string audioEffect = readIniString(
        "audio",
        "effect",
        emulatorAudioEffectName(defaults.audioEffect),
        path);
    settings.audioEffect = parseAudioEffectMode(audioEffect, defaults.audioEffect);
    settings.digitalNoiseReduction = parseDigitalNoiseReductionLevel(
        readIniString("audio", "digital_noise_reduction",
            emulatorDigitalNoiseReductionName(defaults.digitalNoiseReduction), path),
        defaults.digitalNoiseReduction);
    settings.audioDisabled = readIniBool("audio", "audio_disabled", defaults.audioDisabled, path);

    settings.systemImeDisabled = readIniBool(
        "input", "system_ime_disabled", defaults.systemImeDisabled, path);
    settings.showVirtualControls = readIniBool("input", "show_virtual_controls", defaults.showVirtualControls, path);
    settings.virtualControlScalePercent = normalizeIntPreset(
        readIniInt("input", "virtual_control_scale", defaults.virtualControlScalePercent, path),
        EMULATOR_VIRTUAL_CONTROL_SCALE_VALUES,
        defaults.virtualControlScalePercent);
    settings.virtualDpadType = parseVirtualDpadType(
        readIniString("input", "virtual_dpad_type",
            emulatorVirtualDpadTypeName(defaults.virtualDpadType), path),
        defaults.virtualDpadType);
    settings.controllerMapping = readIniString("input", "controller_mapping", defaults.controllerMapping.c_str(), path);
    settings.controllerCalibration = readIniString(
        "input", "controller_calibration", defaults.controllerCalibration.c_str(), path);
    settings.keyboardMapping = readIniString("input", "keyboard_mapping", defaults.keyboardMapping.c_str(), path);

    settings.executionMode = normalizeExecutionMode(
        readIniString("runtime", "backend",
            runtimeExecutionModeConfigValue(defaults.executionMode), path),
        defaults.executionMode);
    settings.cpuClockHz = normalizeCpuClockHz(
        readIniString("runtime", "cpu_hz", defaults.cpuClockHz.c_str(), path),
        defaults.cpuClockHz);
    settings.runtimeSpeedScale = normalizeScaleValue(
        readIniString("runtime", "speed_scale", defaults.runtimeSpeedScale.c_str(), path),
        defaults.runtimeSpeedScale);
    settings.osTimeDelayScale = normalizeScaleValue(
        readIniString("runtime", "ostimedly_scale",
            defaults.osTimeDelayScale.c_str(), path),
        defaults.osTimeDelayScale);
    settings.cheatsEnabled = readIniBool("runtime", "cheats_enabled", defaults.cheatsEnabled, path);
    std::vector<std::pair<std::string, std::string> > cheatValues =
        readIniSectionValues("cheats", path);
    settings.cheatSelections.clear();
    settings.cheatSelections.reserve(cheatValues.size());
    for (size_t i = 0; i < cheatValues.size(); ++i)
    {
        if (cheatValues[i].first.empty())
        {
            continue;
        }
        EmulatorCheatSelection selection;
        selection.cheatFileName = cheatValues[i].first;
        selection.enabledFeatureKeys = decodeCheatFeatureKeys(cheatValues[i].second);
        settings.cheatSelections.push_back(selection);
    }

    std::string language = readIniString("ui", "language", emulatorUiLanguageName(defaults.uiLanguage), path);
    settings.uiLanguage = parseUiLanguage(language, defaults.uiLanguage);

    settings.debugProfile = readIniBool("debug", "profile", defaults.debugProfile, path);
    return settings;
}

static bool writeEmulatorSettings(const EmulatorSettings& settings, const std::string& path)
{
    bool ok = removeIniSection("recent", path);
    const AudioEffectMode audioEffect =
        normalizeAudioEffectMode(settings.audioEffect, AUDIO_EFFECT_OFF);
    const DigitalNoiseReductionLevel digitalNoiseReduction =
        normalizeDigitalNoiseReductionLevel(
            settings.digitalNoiseReduction, DIGITAL_NOISE_REDUCTION_HIGH);
    static const char* managedSections[] =
    {
        "video", "audio", "input", "runtime", "cheats", "ui", "debug"
    };
    for (size_t index = 0; index < sizeof(managedSections) / sizeof(managedSections[0]); ++index)
    {
        ok = removeIniSection(managedSections[index], path) && ok;
    }

    // Recreate managed sections in the same order as the settings menus and structure.
    ok = writeIniString("video", "anti_aliasing", emulatorAntiAliasingName(settings.antiAliasing), path) && ok;
    ok = writeIniString("video", "effect", emulatorColorEffectName(settings.colorEffect), path) && ok;
    ok = writeIniInt("video", "brightness", normalizeIntPreset(
        settings.brightnessPercent, EMULATOR_VIDEO_PERCENT_VALUES, 100), path) && ok;
    ok = writeIniInt("video", "contrast", normalizeIntPreset(
        settings.contrastPercent, EMULATOR_VIDEO_PERCENT_VALUES, 100), path) && ok;
    ok = writeIniInt("video", "gamma", normalizeIntPreset(
        settings.gammaPercent, EMULATOR_VIDEO_PERCENT_VALUES, 100), path) && ok;
    ok = writeIniInt("video", "saturation", normalizeIntPreset(
        settings.saturationPercent, EMULATOR_VIDEO_PERCENT_VALUES, 100), path) && ok;
    ok = writeIniString("video", "minimized_behavior", emulatorMinimizedBehaviorName(settings.minimizedBehavior), path) && ok;
    ok = writeIniString("video", "screen_orientation",
        emulatorScreenOrientationName(settings.screenOrientationMode), path) && ok;
    ok = writeIniString("video", "screen_fill",
        emulatorScreenFillName(settings.screenFill), path) && ok;
    ok = writeIniBool("video", "show_fps", settings.showFps, path) && ok;
    ok = writeIniInt("audio", "volume_percent", normalizeIntPreset(
        settings.audioVolumePercent, EMULATOR_AUDIO_VOLUME_VALUES, 100), path) && ok;
    ok = writeIniInt("audio", "buffer_samples", normalizeAudioBufferSamples(
        settings.audioBufferSamples, 2048), path) && ok;
    ok = writeIniString("audio", "buffer_latency",
        emulatorAudioBufferLatencyName(normalizeAudioBufferLatencyMode(
            settings.audioBufferLatency, AUDIO_BUFFER_LATENCY_AUTO)), path) && ok;
    ok = writeIniString("audio", "effect", emulatorAudioEffectName(audioEffect), path) && ok;
    ok = writeIniString("audio", "digital_noise_reduction",
        emulatorDigitalNoiseReductionName(digitalNoiseReduction), path) && ok;
    ok = writeIniBool("audio", "audio_disabled", settings.audioDisabled, path) && ok;
    ok = writeIniBool(
        "input", "system_ime_disabled", settings.systemImeDisabled, path) && ok;
    ok = writeIniBool("input", "show_virtual_controls", settings.showVirtualControls, path) && ok;
    ok = writeIniInt("input", "virtual_control_scale", normalizeIntPreset(
        settings.virtualControlScalePercent, EMULATOR_VIRTUAL_CONTROL_SCALE_VALUES, 100), path) && ok;
    ok = writeIniString("input", "virtual_dpad_type",
        emulatorVirtualDpadTypeName(settings.virtualDpadType), path) && ok;
    ok = writeIniString("input", "controller_mapping", settings.controllerMapping, path) && ok;
    ok = writeIniString(
        "input", "controller_calibration", settings.controllerCalibration, path) && ok;
    ok = writeIniString("input", "keyboard_mapping", settings.keyboardMapping, path) && ok;
    ok = writeIniString("runtime", "backend",
        runtimeExecutionModeConfigValue(settings.executionMode), path) && ok;
    ok = writeIniString("runtime", "cpu_hz",
        normalizeCpuClockHz(settings.cpuClockHz, ""), path) && ok;
    ok = writeIniString("runtime", "speed_scale",
        normalizeScaleValue(settings.runtimeSpeedScale, ""), path) && ok;
    ok = writeIniString("runtime", "ostimedly_scale",
        normalizeScaleValue(settings.osTimeDelayScale, ""), path) && ok;
    ok = writeIniBool("runtime", "cheats_enabled", settings.cheatsEnabled, path) && ok;
    for (size_t i = 0; i < settings.cheatSelections.size(); ++i)
    {
        const EmulatorCheatSelection& selection = settings.cheatSelections[i];
        if (!selection.cheatFileName.empty() && !selection.enabledFeatureKeys.empty())
        {
            ok = writeIniString("cheats", selection.cheatFileName.c_str(),
                encodeCheatFeatureKeys(selection.enabledFeatureKeys), path) && ok;
        }
    }
    ok = writeIniString("ui", "language", emulatorUiLanguageName(settings.uiLanguage), path) && ok;
    ok = writeIniBool("debug", "profile", settings.debugProfile, path) && ok;
    return ok;
}

bool emulatorSaveSettings(const EmulatorSettings& settings)
{
    const std::string path = emulatorSettingsPath();
    recoverSettingsTransaction(path);

    std::string originalText;
    const bool hadOriginal = readTextFileUtf8(path, &originalText);
    const std::string temporaryPath = settingsTemporaryPath(path);
    const std::string backupPath = settingsBackupPath(path);
    remove(temporaryPath.c_str());
    remove(backupPath.c_str());

    if (!writeTextFileUtf8(temporaryPath, hadOriginal ? originalText : std::string()) ||
        !writeEmulatorSettings(settings, temporaryPath))
    {
        remove(temporaryPath.c_str());
        return false;
    }

    std::string savedText;
    if (!readTextFileUtf8(temporaryPath, &savedText) || savedText.empty())
    {
        remove(temporaryPath.c_str());
        return false;
    }

    if (hadOriginal && rename(path.c_str(), backupPath.c_str()) != 0)
    {
        remove(temporaryPath.c_str());
        return false;
    }
    if (rename(temporaryPath.c_str(), path.c_str()) != 0)
    {
        if (hadOriginal)
        {
            restoreSettingsFile(backupPath, path);
        }
        remove(temporaryPath.c_str());
        return false;
    }

    remove(backupPath.c_str());
    if (settings.debugProfile || runtimeLogEnvEnabled("DINGOO_PIE_LOG_FILE"))
    {
        emulatorTraceSettings("saved", settings);
    }
    return true;
}

std::vector<std::string> emulatorCheatFeatureKeysForGame(
    const EmulatorSettings& settings,
    const std::string& gamePath)
{
    std::string cheatFileName = cheatSelectionKeyForGame(gamePath);
    if (cheatFileName.empty())
    {
        return std::vector<std::string>();
    }

    for (size_t i = 0; i < settings.cheatSelections.size(); ++i)
    {
        if (cheatSelectionKeysMatch(settings.cheatSelections[i].cheatFileName, cheatFileName))
        {
            return settings.cheatSelections[i].enabledFeatureKeys;
        }
    }
    return std::vector<std::string>();
}

bool emulatorSetCheatFeatureKeysForGame(
    EmulatorSettings* settings,
    const std::string& gamePath,
    const std::vector<std::string>& featureKeys)
{
    if (!settings)
    {
        return false;
    }

    std::string cheatFileName = cheatSelectionKeyForGame(gamePath);
    if (cheatFileName.empty())
    {
        return false;
    }

    for (size_t i = 0; i < settings->cheatSelections.size(); ++i)
    {
        if (!cheatSelectionKeysMatch(settings->cheatSelections[i].cheatFileName, cheatFileName))
        {
            continue;
        }
        if (featureKeys.empty())
        {
            settings->cheatSelections.erase(settings->cheatSelections.begin() + i);
            return true;
        }
        if (settings->cheatSelections[i].enabledFeatureKeys == featureKeys)
        {
            return false;
        }
        settings->cheatSelections[i].enabledFeatureKeys = featureKeys;
        return true;
    }

    if (featureKeys.empty())
    {
        return false;
    }

    EmulatorCheatSelection selection;
    selection.cheatFileName = cheatFileName;
    selection.enabledFeatureKeys = featureKeys;
    settings->cheatSelections.push_back(selection);
    return true;
}

void emulatorTraceSettings(const char* reason, const EmulatorSettings& settings)
{
    const char* label = (reason && reason[0]) ? reason : "snapshot";
    const AudioEffectMode audioEffect =
        normalizeAudioEffectMode(settings.audioEffect, AUDIO_EFFECT_OFF);
    const DigitalNoiseReductionLevel digitalNoiseReduction =
        normalizeDigitalNoiseReductionLevel(
            settings.digitalNoiseReduction, DIGITAL_NOISE_REDUCTION_HIGH);
    const std::string cpuClockHz = normalizeCpuClockHz(settings.cpuClockHz, "");
    const std::string runtimeSpeedScale = normalizeScaleValue(settings.runtimeSpeedScale, "");
    const std::string osTimeDelayScale = normalizeScaleValue(settings.osTimeDelayScale, "");
    printf(
        "settings-trace:%s video.anti_aliasing=%s video.effect=%s "
        "video.brightness=%d video.contrast=%d video.gamma=%d "
        "video.saturation=%d video.minimized_behavior=%s "
        "video.screen_orientation=%s video.screen_fill=%s video.portrait=%u "
        "video.show_fps=%u\n",
        label,
        emulatorAntiAliasingName(settings.antiAliasing),
        emulatorColorEffectName(settings.colorEffect),
        normalizeIntPreset(settings.brightnessPercent, EMULATOR_VIDEO_PERCENT_VALUES, 100),
        normalizeIntPreset(settings.contrastPercent, EMULATOR_VIDEO_PERCENT_VALUES, 100),
        normalizeIntPreset(settings.gammaPercent, EMULATOR_VIDEO_PERCENT_VALUES, 100),
        normalizeIntPreset(settings.saturationPercent, EMULATOR_VIDEO_PERCENT_VALUES, 100),
        emulatorMinimizedBehaviorName(settings.minimizedBehavior),
        emulatorScreenOrientationName(settings.screenOrientationMode),
        emulatorScreenFillName(settings.screenFill),
        settings.portraitMode ? 1u : 0u,
        settings.showFps ? 1u : 0u);
    printf(
        "settings-trace:%s audio.volume_percent=%d audio.buffer_samples=%d audio.buffer_latency=%s "
        "audio.effect=%s audio.digital_noise_reduction=%s "
        "audio.audio_disabled=%u\n",
        label,
        normalizeIntPreset(settings.audioVolumePercent, EMULATOR_AUDIO_VOLUME_VALUES, 100),
        normalizeAudioBufferSamples(settings.audioBufferSamples, 2048),
        emulatorAudioBufferLatencyName(normalizeAudioBufferLatencyMode(
            settings.audioBufferLatency, AUDIO_BUFFER_LATENCY_AUTO)),
        emulatorAudioEffectName(audioEffect),
        emulatorDigitalNoiseReductionName(digitalNoiseReduction),
        settings.audioDisabled ? 1u : 0u);
    printf(
        "settings-trace:%s input.system_ime_disabled=%u "
        "input.show_virtual_controls=%u input.virtual_control_scale=%d "
        "input.virtual_dpad_type=%s input.controller_mapping=\"%s\" "
        "input.controller_calibration=\"%s\" input.keyboard_mapping=\"%s\"\n",
        label,
        settings.systemImeDisabled ? 1u : 0u,
        settings.showVirtualControls ? 1u : 0u,
        normalizeIntPreset(settings.virtualControlScalePercent, EMULATOR_VIRTUAL_CONTROL_SCALE_VALUES, 100),
        emulatorVirtualDpadTypeName(settings.virtualDpadType),
        settings.controllerMapping.empty() ? "(default)" : settings.controllerMapping.c_str(),
        settings.controllerCalibration.empty() ? "(default)" : settings.controllerCalibration.c_str(),
        settings.keyboardMapping.empty() ? "(default)" : settings.keyboardMapping.c_str());
    printf("settings-trace:%s runtime.backend=%s runtime.cpu_hz=%s runtime.speed_scale=%s runtime.ostimedly_scale=%s runtime.cheats_enabled=%u\n",
        label,
        runtimeExecutionModeName(settings.executionMode),
        cpuClockHz.empty() ? "auto" : cpuClockHz.c_str(),
        runtimeSpeedScale.empty() ? "auto" : runtimeSpeedScale.c_str(),
        osTimeDelayScale.empty() ? "auto" : osTimeDelayScale.c_str(),
        settings.cheatsEnabled ? 1u : 0u);
    for (size_t i = 0; i < settings.cheatSelections.size(); ++i)
    {
        const EmulatorCheatSelection& selection = settings.cheatSelections[i];
        if (!selection.cheatFileName.empty() && !selection.enabledFeatureKeys.empty())
        {
            printf("settings-trace:%s cheats.%s=\"%s\"\n",
                label,
                selection.cheatFileName.c_str(),
                encodeCheatFeatureKeys(selection.enabledFeatureKeys).c_str());
        }
    }
    printf("settings-trace:%s ui.language=%s\n",
        label,
        emulatorUiLanguageName(settings.uiLanguage));
    printf("settings-trace:%s debug.profile=%u\n",
        label,
        settings.debugProfile ? 1u : 0u);
}

bool emulatorResetSettings(void)
{
    EmulatorSettings defaults = emulatorDefaultSettings();
    return emulatorSaveSettings(defaults);
}

void emulatorApplySettingsToEnvironment(const EmulatorSettings& settings)
{
    if (!externalBackendOverrideEnabled())
    {
        setEnvValue("DINGOO_PIE_BACKEND",
            runtimeExecutionModeConfigValue(settings.executionMode));
    }
    setEnvValue("DINGOO_PIE_IRJIT_CLOCK_HZ", normalizeCpuClockHz(settings.cpuClockHz, ""));

    // Empty runtimeSpeedScale is the menu/INI "Auto" preset. Runtime code maps
    // Auto to the chosen global pace while keeping the UI checked on Auto.
    setEnvValue("DINGOO_PIE_RUNTIME_SPEED_SCALE", normalizeScaleValue(settings.runtimeSpeedScale, ""));
    setEnvValue("DINGOO_PIE_OSTIMEDLY_SCALE", normalizeScaleValue(settings.osTimeDelayScale, ""));
    setEnvValue("DINGOO_PIE_AUDIO_DISABLED", settings.audioDisabled ? "1" : "");
    setEnvValue("DINGOO_PIE_PROFILE", settings.debugProfile || runtimeLogExternalProfileEnabled() ? "1" : "");
    runtimeLogSetProfileEnabled(settings.debugProfile);
}

void emulatorApplySharedRuntimeSettings(const EmulatorSettings& settings)
{
    emulatorApplySettingsToEnvironment(settings);
    bool profileEnabled = runtimeLogProfileEnabled();
    fsys_set_profile_enabled(profileEnabled);
}

const char* emulatorAntiAliasingName(AntiAliasingMode mode)
{
    switch (mode)
    {
    case ANTI_ALIASING_LOW:
        return "low";
    case ANTI_ALIASING_CLEAR:
        return "clear";
    default:
        return "off";
    }
}

const char* emulatorColorEffectName(ColorEffectMode mode)
{
    switch (mode)
    {
    case COLOR_EFFECT_GRAYSCALE:
        return "grayscale";
    case COLOR_EFFECT_INVERT:
        return "invert";
    case COLOR_EFFECT_SOFT_BLUR:
        return "soft_blur";
    case COLOR_EFFECT_SHARPEN:
        return "sharpen";
    case COLOR_EFFECT_VIVID:
        return "vivid";
    case COLOR_EFFECT_SEPIA:
        return "sepia";
    case COLOR_EFFECT_PIXEL_GRID:
        return "pixel_grid";
    case COLOR_EFFECT_LCD_SCANLINE:
        return "lcd_scanline";
    case COLOR_EFFECT_LIGHT_CRT:
        return "light_crt";
    default:
        return "normal";
    }
}

const char* emulatorAudioEffectName(AudioEffectMode mode)
{
    switch (mode)
    {
    case AUDIO_EFFECT_SOFT:
        return "soft";
    case AUDIO_EFFECT_CLEAR:
        return "clear";
    case AUDIO_EFFECT_BASS_BOOST:
        return "bass_boost";
    case AUDIO_EFFECT_MONO:
        return "mono";
    case AUDIO_EFFECT_OFF:
    default:
        return "off";
    }
}

const char* emulatorAudioBufferLatencyName(AudioBufferLatencyMode mode)
{
    switch (mode)
    {
    case AUDIO_BUFFER_LATENCY_110MS: return "110ms";
    case AUDIO_BUFFER_LATENCY_120MS: return "120ms";
    case AUDIO_BUFFER_LATENCY_130MS: return "130ms";
    case AUDIO_BUFFER_LATENCY_140MS: return "140ms";
    case AUDIO_BUFFER_LATENCY_150MS: return "150ms";
    case AUDIO_BUFFER_LATENCY_AUTO:
    default: return "auto";
    }
}

int emulatorAudioBufferLatencyMilliseconds(AudioBufferLatencyMode mode)
{
    switch (mode)
    {
    case AUDIO_BUFFER_LATENCY_110MS: return 110;
    case AUDIO_BUFFER_LATENCY_120MS: return 120;
    case AUDIO_BUFFER_LATENCY_130MS: return 130;
    case AUDIO_BUFFER_LATENCY_140MS: return 140;
    case AUDIO_BUFFER_LATENCY_150MS: return 150;
    case AUDIO_BUFFER_LATENCY_AUTO:
    default: return 130;
    }
}

const char* emulatorDigitalNoiseReductionName(DigitalNoiseReductionLevel level)
{
    switch (level)
    {
    case DIGITAL_NOISE_REDUCTION_HIGH: return "high";
    case DIGITAL_NOISE_REDUCTION_MEDIUM: return "medium";
    case DIGITAL_NOISE_REDUCTION_LOW: return "low";
    default: return "high";
    }
}

const char* emulatorUiLanguageName(UiLanguage language)
{
    switch (language)
    {
    case UI_LANGUAGE_ENGLISH:
        return "english";
    case UI_LANGUAGE_CHINESE:
    default:
        return "chinese";
    }
}

const char* emulatorMinimizedBehaviorName(MinimizedBehavior behavior)
{
    switch (behavior)
    {
    case MINIMIZED_BEHAVIOR_NORMAL:
        return "normal";
    case MINIMIZED_BEHAVIOR_PAUSE:
        return "pause";
    case MINIMIZED_BEHAVIOR_THROTTLE:
        return "throttle";
    default:
        return "pause";
    }
}

const char* emulatorScreenOrientationName(ScreenOrientationMode mode)
{
    switch (mode)
    {
    case SCREEN_ORIENTATION_AUTO:
        return "auto";
    case SCREEN_ORIENTATION_PORTRAIT:
        return "portrait";
    case SCREEN_ORIENTATION_LANDSCAPE:
    default:
        return "landscape";
    }
}

const char* emulatorScreenFillName(ScreenFillMode fill)
{
    switch (fill)
    {
    case SCREEN_FILL_BLURRED_EXTENSION:
        return "blurred";
    case SCREEN_FILL_STRETCH:
        return "stretch";
    case SCREEN_FILL_ASPECT:
    default:
        return "aspect";
    }
}

const char* emulatorVirtualDpadTypeName(VirtualDpadType type)
{
    switch (type)
    {
    case VIRTUAL_DPAD_SEGMENTED_RING:
        return "segmented_ring";
    case VIRTUAL_DPAD_JOYSTICK:
    default:
        return "joystick";
    }
}
