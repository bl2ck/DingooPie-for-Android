#ifndef DINGOO_PIE_FRONTEND_INPUT_KEYBOARD_MAPPING_H
#define DINGOO_PIE_FRONTEND_INPUT_KEYBOARD_MAPPING_H

#include "frontend/input/input_controls.h"

#include <stddef.h>
#include <string>

struct KeyboardBinding
{
    uint32_t controlBit;
    SDL_Scancode scancode;
};

static constexpr KeyboardBinding kDefaultKeyboardBindings[] =
{
    { CONTROL_BUTTON_A, SDL_SCANCODE_L },
    { CONTROL_BUTTON_B, SDL_SCANCODE_K },
    { CONTROL_BUTTON_X, SDL_SCANCODE_I },
    { CONTROL_BUTTON_Y, SDL_SCANCODE_J },
    { CONTROL_BUTTON_START, SDL_SCANCODE_O },
    { CONTROL_BUTTON_START, SDL_SCANCODE_0 },
    { CONTROL_BUTTON_SELECT, SDL_SCANCODE_Q },
    { CONTROL_BUTTON_SELECT, SDL_SCANCODE_1 },
    { CONTROL_TRIGGER_LEFT, SDL_SCANCODE_LSHIFT },
    { CONTROL_TRIGGER_RIGHT, SDL_SCANCODE_RSHIFT },
    { CONTROL_DPAD_UP, SDL_SCANCODE_W },
    { CONTROL_DPAD_UP, SDL_SCANCODE_UP },
    { CONTROL_DPAD_DOWN, SDL_SCANCODE_S },
    { CONTROL_DPAD_DOWN, SDL_SCANCODE_DOWN },
    { CONTROL_DPAD_LEFT, SDL_SCANCODE_A },
    { CONTROL_DPAD_LEFT, SDL_SCANCODE_LEFT },
    { CONTROL_DPAD_RIGHT, SDL_SCANCODE_D },
    { CONTROL_DPAD_RIGHT, SDL_SCANCODE_RIGHT },
    { CONTROL_POWER, SDL_SCANCODE_BACKSPACE },
    { CONTROL_POWER, SDL_SCANCODE_HOME },
};

static constexpr size_t kDefaultKeyboardBindingCount =
    sizeof(kDefaultKeyboardBindings) / sizeof(kDefaultKeyboardBindings[0]);

constexpr bool defaultKeyboardHasBinding(uint32_t controlBit, SDL_Scancode scancode)
{
    for (size_t index = 0; index < kDefaultKeyboardBindingCount; ++index)
    {
        if (kDefaultKeyboardBindings[index].controlBit == controlBit &&
            kDefaultKeyboardBindings[index].scancode == scancode)
        {
            return true;
        }
    }
    return false;
}

bool keyboardMappingIsCurrent(const std::string& mapping);
bool keyboardMappingInitialized(void);
void keyboardMappingApply(const std::string& mapping);
const KeyboardBinding* keyboardMappingBindings(size_t* outCount);

#endif
