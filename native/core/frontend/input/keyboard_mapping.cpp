#include "frontend/input/keyboard_mapping.h"

#include <ctype.h>
#include <stdio.h>

static const size_t kMaxKeyboardBindings = 64;
static KeyboardBinding g_keyboardBindings[kMaxKeyboardBindings];
static size_t g_keyboardBindingCount = 0;
static bool g_keyboardMappingInitialized = false;
static std::string g_appliedKeyboardMapping;

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

static std::string normalizeMappingName(const std::string& text)
{
    std::string out;
    std::string trimmed = trimString(text);
    for (size_t index = 0; index < trimmed.size(); ++index)
    {
        unsigned char ch = (unsigned char)trimmed[index];
        if (ch == ' ' || ch == '\t' || ch == '_' || ch == '-')
        {
            continue;
        }
        out.push_back((char)tolower(ch));
    }
    return out;
}

static bool parseKeyboardControlName(const std::string& name, uint32_t* outControlBit)
{
    if (!outControlBit)
    {
        return false;
    }
    std::string normalized = normalizeMappingName(name);
    if (normalized == "a" || normalized == "buttona") { *outControlBit = CONTROL_BUTTON_A; return true; }
    if (normalized == "b" || normalized == "buttonb") { *outControlBit = CONTROL_BUTTON_B; return true; }
    if (normalized == "x" || normalized == "buttonx") { *outControlBit = CONTROL_BUTTON_X; return true; }
    if (normalized == "y" || normalized == "buttony") { *outControlBit = CONTROL_BUTTON_Y; return true; }
    if (normalized == "start") { *outControlBit = CONTROL_BUTTON_START; return true; }
    if (normalized == "select") { *outControlBit = CONTROL_BUTTON_SELECT; return true; }
    if (normalized == "l" || normalized == "leftshoulder" || normalized == "triggerleft")
    {
        *outControlBit = CONTROL_TRIGGER_LEFT;
        return true;
    }
    if (normalized == "r" || normalized == "rightshoulder" || normalized == "triggerright")
    {
        *outControlBit = CONTROL_TRIGGER_RIGHT;
        return true;
    }
    if (normalized == "up" || normalized == "dpadup") { *outControlBit = CONTROL_DPAD_UP; return true; }
    if (normalized == "down" || normalized == "dpaddown") { *outControlBit = CONTROL_DPAD_DOWN; return true; }
    if (normalized == "left" || normalized == "dpadleft") { *outControlBit = CONTROL_DPAD_LEFT; return true; }
    if (normalized == "right" || normalized == "dpadright") { *outControlBit = CONTROL_DPAD_RIGHT; return true; }
    if (normalized == "power") { *outControlBit = CONTROL_POWER; return true; }
    return false;
}

static bool parseKeyboardSourceName(const std::string& name, SDL_Scancode* outScancode)
{
    if (!outScancode)
    {
        return false;
    }
    std::string normalized = normalizeMappingName(name);
    if (normalized.empty() || normalized == "none" || normalized == "off" ||
        normalized == "unmapped" || normalized == "disabled" || normalized == "0")
    {
        *outScancode = SDL_SCANCODE_UNKNOWN;
        return true;
    }
    if (normalized == "up" || normalized == "arrowup") { *outScancode = SDL_SCANCODE_UP; return true; }
    if (normalized == "down" || normalized == "arrowdown") { *outScancode = SDL_SCANCODE_DOWN; return true; }
    if (normalized == "left" || normalized == "arrowleft") { *outScancode = SDL_SCANCODE_LEFT; return true; }
    if (normalized == "right" || normalized == "arrowright") { *outScancode = SDL_SCANCODE_RIGHT; return true; }
    if (normalized == "lshift" || normalized == "leftshift") { *outScancode = SDL_SCANCODE_LSHIFT; return true; }
    if (normalized == "rshift" || normalized == "rightshift") { *outScancode = SDL_SCANCODE_RSHIFT; return true; }
    if (normalized == "space") { *outScancode = SDL_SCANCODE_SPACE; return true; }
    if (normalized == "enter" || normalized == "return") { *outScancode = SDL_SCANCODE_RETURN; return true; }
    if (normalized == "esc" || normalized == "escape") { *outScancode = SDL_SCANCODE_ESCAPE; return true; }
    if (normalized == "backspace") { *outScancode = SDL_SCANCODE_BACKSPACE; return true; }
    if (normalized == "home") { *outScancode = SDL_SCANCODE_HOME; return true; }

    SDL_Scancode scancode = SDL_GetScancodeFromName(name.c_str());
    if (scancode != SDL_SCANCODE_UNKNOWN)
    {
        *outScancode = scancode;
        return true;
    }
    if (normalized.size() == 1)
    {
        char ch = normalized[0];
        if (ch >= 'a' && ch <= 'z')
        {
            *outScancode = (SDL_Scancode)(SDL_SCANCODE_A + (ch - 'a'));
            return true;
        }
        if (ch >= '0' && ch <= '9')
        {
            *outScancode = ch == '0' ? SDL_SCANCODE_0 :
                (SDL_Scancode)(SDL_SCANCODE_1 + (ch - '1'));
            return true;
        }
    }
    return false;
}

static bool addKeyboardBinding(uint32_t controlBit, SDL_Scancode scancode)
{
    if (scancode == SDL_SCANCODE_UNKNOWN || g_keyboardBindingCount >= kMaxKeyboardBindings)
    {
        return false;
    }
    g_keyboardBindings[g_keyboardBindingCount++] = { controlBit, scancode };
    return true;
}

static void setDefaultKeyboardMapping(void)
{
    g_keyboardBindingCount = 0;
    for (size_t index = 0; index < kDefaultKeyboardBindingCount &&
        g_keyboardBindingCount < kMaxKeyboardBindings; ++index)
    {
        g_keyboardBindings[g_keyboardBindingCount++] = kDefaultKeyboardBindings[index];
    }
}

static void removeKeyboardSource(SDL_Scancode scancode)
{
    if (scancode == SDL_SCANCODE_UNKNOWN)
    {
        return;
    }
    size_t out = 0;
    for (size_t index = 0; index < g_keyboardBindingCount; ++index)
    {
        if (g_keyboardBindings[index].scancode != scancode)
        {
            g_keyboardBindings[out++] = g_keyboardBindings[index];
        }
    }
    g_keyboardBindingCount = out;
}

static void applyKeyboardMappingToken(const std::string& token)
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
        printf("input: invalid keyboard mapping token='%s'\n", trimmed.c_str());
        return;
    }
    std::string sourceName = trimString(trimmed.substr(0, separator));
    std::string targetName = trimString(trimmed.substr(separator + 1));
    SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
    if (!parseKeyboardSourceName(sourceName, &scancode))
    {
        printf("input: unknown keyboard mapping source='%s'\n", sourceName.c_str());
        return;
    }
    removeKeyboardSource(scancode);

    uint32_t controlBit = 0;
    std::string normalizedTarget = normalizeMappingName(targetName);
    if (normalizedTarget.empty() || normalizedTarget == "none" || normalizedTarget == "off" ||
        normalizedTarget == "unmapped" || normalizedTarget == "disabled" ||
        normalizedTarget == "0")
    {
        return;
    }
    if (!parseKeyboardControlName(targetName, &controlBit))
    {
        printf("input: unknown keyboard mapping target='%s'\n", targetName.c_str());
        return;
    }
    addKeyboardBinding(controlBit, scancode);
}

bool keyboardMappingIsCurrent(const std::string& mapping)
{
    return g_keyboardMappingInitialized && mapping == g_appliedKeyboardMapping;
}

bool keyboardMappingInitialized(void)
{
    return g_keyboardMappingInitialized;
}

void keyboardMappingApply(const std::string& mapping)
{
    setDefaultKeyboardMapping();
    size_t begin = 0;
    while (begin <= mapping.size())
    {
        size_t comma = mapping.find_first_of(",;\n", begin);
        std::string token = comma == std::string::npos ?
            mapping.substr(begin) : mapping.substr(begin, comma - begin);
        applyKeyboardMappingToken(token);
        if (comma == std::string::npos)
        {
            break;
        }
        begin = comma + 1;
    }
    g_appliedKeyboardMapping = mapping;
    g_keyboardMappingInitialized = true;
    printf("input: keyboard mapping applied spec='%s'\n",
        mapping.empty() ? "(default)" : mapping.c_str());
}

const KeyboardBinding* keyboardMappingBindings(size_t* outCount)
{
    if (outCount)
    {
        *outCount = g_keyboardBindingCount;
    }
    return g_keyboardBindings;
}
