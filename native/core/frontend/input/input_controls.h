#ifndef DINGOO_PIE_FRONTEND_INPUT_INPUT_CONTROLS_H
#define DINGOO_PIE_FRONTEND_INPUT_INPUT_CONTROLS_H

#include "frontend/input/input_state.h"

#include <SDL2/SDL.h>
#include <string>

// Values are guest key-status bit positions; keep them explicit when reordering.
enum InputControlBit : uint32_t
{
    CONTROL_BUTTON_A = 31,       /*!< Dingoo A320 A / Gemei X760+ A. */
    CONTROL_BUTTON_B = 21,       /*!< Dingoo A320 B / Gemei X760+ B. */
    CONTROL_BUTTON_X = 16,       /*!< Dingoo A320 X / Gemei X760+ Δ. */
    CONTROL_BUTTON_Y = 6,        /*!< Dingoo A320 Y / Gemei X760+ X. */
    CONTROL_BUTTON_START = 11,   /*!< Dingoo A320 START; synthetic only for Gemei X760+ mappings. */
    CONTROL_BUTTON_SELECT = 10,  /*!< Dingoo A320 SELECT; synthetic only for Gemei X760+ mappings. */
    CONTROL_TRIGGER_LEFT = 8,    /*!< Dingoo A320 left shoulder; synthetic only for Gemei X760+ mappings. */
    CONTROL_TRIGGER_RIGHT = 29,  /*!< Dingoo A320 right shoulder; synthetic only for Gemei X760+ mappings. */
    CONTROL_DPAD_UP = 20,        /*!< Directional pad up. */
    CONTROL_DPAD_DOWN = 27,      /*!< Directional pad down. */
    CONTROL_DPAD_LEFT = 28,      /*!< Directional pad left. */
    CONTROL_DPAD_RIGHT = 18,     /*!< Directional pad right. */
    CONTROL_POWER = 7            /*!< Dingoo A320 power slider; HOLD is not tracked separately. */
};

void inputClearControls(void);
void inputClearSyntheticControls(void);
void inputResetTransientControls(void);
void inputSetSyntheticControl(uint32_t controlBit, bool pressed);
void inputHandleHostScancode(SDL_Scancode scancode, bool pressed);
void inputPollKeyboardState(void);
void inputApplyKeyboardMapping(const std::string& mapping);

#endif
