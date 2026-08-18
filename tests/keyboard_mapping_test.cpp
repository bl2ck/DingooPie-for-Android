#include "frontend/input/keyboard_mapping.h"

static_assert(defaultKeyboardHasBinding(CONTROL_BUTTON_A, SDL_SCANCODE_L));
static_assert(defaultKeyboardHasBinding(CONTROL_BUTTON_B, SDL_SCANCODE_K));
static_assert(defaultKeyboardHasBinding(CONTROL_BUTTON_X, SDL_SCANCODE_I));
static_assert(defaultKeyboardHasBinding(CONTROL_BUTTON_Y, SDL_SCANCODE_J));
static_assert(defaultKeyboardHasBinding(CONTROL_BUTTON_START, SDL_SCANCODE_0));
static_assert(defaultKeyboardHasBinding(CONTROL_BUTTON_START, SDL_SCANCODE_O));
static_assert(defaultKeyboardHasBinding(CONTROL_BUTTON_SELECT, SDL_SCANCODE_1));
static_assert(defaultKeyboardHasBinding(CONTROL_BUTTON_SELECT, SDL_SCANCODE_Q));
static_assert(defaultKeyboardHasBinding(CONTROL_TRIGGER_LEFT, SDL_SCANCODE_LSHIFT));
static_assert(defaultKeyboardHasBinding(CONTROL_TRIGGER_RIGHT, SDL_SCANCODE_RSHIFT));
static_assert(defaultKeyboardHasBinding(CONTROL_DPAD_UP, SDL_SCANCODE_W));
static_assert(defaultKeyboardHasBinding(CONTROL_DPAD_UP, SDL_SCANCODE_UP));
static_assert(defaultKeyboardHasBinding(CONTROL_DPAD_DOWN, SDL_SCANCODE_S));
static_assert(defaultKeyboardHasBinding(CONTROL_DPAD_DOWN, SDL_SCANCODE_DOWN));
static_assert(defaultKeyboardHasBinding(CONTROL_DPAD_LEFT, SDL_SCANCODE_A));
static_assert(defaultKeyboardHasBinding(CONTROL_DPAD_LEFT, SDL_SCANCODE_LEFT));
static_assert(defaultKeyboardHasBinding(CONTROL_DPAD_RIGHT, SDL_SCANCODE_D));
static_assert(defaultKeyboardHasBinding(CONTROL_DPAD_RIGHT, SDL_SCANCODE_RIGHT));
static_assert(defaultKeyboardHasBinding(CONTROL_POWER, SDL_SCANCODE_BACKSPACE));
static_assert(defaultKeyboardHasBinding(CONTROL_POWER, SDL_SCANCODE_HOME));

static_assert(!defaultKeyboardHasBinding(CONTROL_BUTTON_A, SDL_SCANCODE_K));

int main()
{
    return 0;
}
