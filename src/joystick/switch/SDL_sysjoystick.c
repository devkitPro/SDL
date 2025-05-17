/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2018 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "SDL_internal.h"

#if SDL_JOYSTICK_SWITCH

#include "../SDL_sysjoystick.h"

#include <switch.h>

#define JOYSTICK_COUNT 8

typedef struct SWITCHJoystickState
{
    PadState pad;
    HidAnalogStickState sticks_old[2];
    HidVibrationDeviceHandle vibrationDeviceHandles;
    HidVibrationValue vibrationValues;
    HidNpadButton *pad_mapping;
    u32 pad_type;
    u32 pad_type_prev;
    HidNpadStyleTag pad_style;
    HidNpadStyleTag pad_style_prev;
} SWITCHJoystickState;

static SWITCHJoystickState state[JOYSTICK_COUNT];

static const HidNpadButton pad_mapping_default[] = {
    HidNpadButton_A, HidNpadButton_B, HidNpadButton_X, HidNpadButton_Y,
    HidNpadButton_StickL, HidNpadButton_StickR,
    HidNpadButton_L, HidNpadButton_R,
    HidNpadButton_ZL, HidNpadButton_ZR,
    HidNpadButton_Plus, HidNpadButton_Minus,
    HidNpadButton_Left, HidNpadButton_Up, HidNpadButton_Right, HidNpadButton_Down,
    HidNpadButton_StickLLeft, HidNpadButton_StickLUp, HidNpadButton_StickLRight, HidNpadButton_StickLDown,
    HidNpadButton_StickRLeft, HidNpadButton_StickRUp, HidNpadButton_StickRRight, HidNpadButton_StickRDown,
    HidNpadButton_LeftSL, HidNpadButton_LeftSR, HidNpadButton_RightSL, HidNpadButton_RightSR
};

// left single joycon mapping (start = left stick, select = minus)
static const HidNpadButton pad_mapping_left_joy[] = {
    HidNpadButton_Down, HidNpadButton_Left, HidNpadButton_Right, HidNpadButton_Up,
    BIT(31), BIT(31),
    BIT(31), BIT(31),
    HidNpadButton_LeftSL, HidNpadButton_LeftSR,
    HidNpadButton_StickL, HidNpadButton_Minus,
    HidNpadButton_StickLUp, HidNpadButton_StickLRight, HidNpadButton_StickLDown, HidNpadButton_StickLLeft,
    BIT(31), BIT(31), BIT(31), BIT(31),
    BIT(31), BIT(31), BIT(31), BIT(31),
    BIT(31), BIT(31), BIT(31), BIT(31)
};

// right single joycon mapping (start = right stick, select = plus)
static const HidNpadButton pad_mapping_right_joy[] = {
    HidNpadButton_X, HidNpadButton_A, HidNpadButton_Y, HidNpadButton_B,
    BIT(31), BIT(31),
    BIT(31), BIT(31),
    HidNpadButton_RightSL, HidNpadButton_RightSR,
    HidNpadButton_StickR, HidNpadButton_Plus,
    HidNpadButton_StickRDown, HidNpadButton_StickRLeft, HidNpadButton_StickRUp, HidNpadButton_StickRRight,
    BIT(31), BIT(31), BIT(31), BIT(31),
    BIT(31), BIT(31), BIT(31), BIT(31),
    BIT(31), BIT(31), BIT(31), BIT(31)
};

/* Function to perform the mapping from device index to the instance id for this index */
static SDL_JoystickID SWITCH_JoystickGetDeviceInstanceID(int device_index)
{
    return (SDL_JoystickID)(device_index + 1);
}

static void SWITCH_UpdateControllerSupport(Uint64 timestamp, bool handheld)
{
    if (!handheld) {
        HidLaControllerSupportResultInfo info;
        HidLaControllerSupportArg args;
        hidLaCreateControllerSupportArg(&args);
        args.hdr.player_count_max = JOYSTICK_COUNT;
        hidLaShowControllerSupportForSystem(&info, &args, false);
    }

    // update pads states
    for (int i = 0; i < JOYSTICK_COUNT; i++) {
        SDL_Joystick *joy = SDL_GetJoystickFromID(SWITCH_JoystickGetDeviceInstanceID(i));
        if (joy) {
            padUpdate(&state[i].pad);
            state[i].pad_type = state[i].pad_type_prev = hidGetNpadDeviceType((HidNpadIdType)i);
            state[i].pad_style = state[i].pad_style_prev = hidGetNpadStyleSet((HidNpadIdType)i);
            // update pad mapping
            if (!(state[i].pad_style & HidNpadStyleTag_NpadJoyDual) &&
                (state[i].pad_type & HidDeviceTypeBits_JoyLeft)) {
                state[i].pad_mapping = (HidNpadButton *)&pad_mapping_left_joy;
            } else if (!(state[i].pad_style & HidNpadStyleTag_NpadJoyDual) &&
                       (state[i].pad_type & HidDeviceTypeBits_JoyRight)) {
                state[i].pad_mapping = (HidNpadButton *)&pad_mapping_right_joy;
            } else {
                state[i].pad_mapping = (HidNpadButton *)&pad_mapping_default;
            }
            // update vibration stuff ?
            hidInitializeVibrationDevices(&state[i].vibrationDeviceHandles, 1,
                                          HidNpadIdType_No1 + i, state[i].pad_style);
            // reset sdl joysticks states
            SDL_SendJoystickAxis(timestamp, joy, 0, 0);
            SDL_SendJoystickAxis(timestamp, joy, 1, 0);
            SDL_SendJoystickAxis(timestamp, joy, 2, 0);
            SDL_SendJoystickAxis(timestamp, joy, 3, 0);
            state[i].pad.buttons_cur = 0;
            state[i].pad.buttons_old = 0;
            for (int j = 0; j < joy->nbuttons; j++) {
                SDL_SendJoystickButton(timestamp, joy, j, false);
            }
        }
    }
}

/* Function to scan the system for joysticks.
 * It should return 0, or -1 on an unrecoverable fatal error.
 */
static bool SWITCH_JoystickInit(void)
{
    padConfigureInput(JOYSTICK_COUNT, HidNpadStyleSet_NpadStandard);

    // initialize first pad to defaults
    padInitializeDefault(&state[0].pad);
    padUpdate(&state[0].pad);
    hidSetNpadJoyHoldType(HidNpadJoyHoldType_Horizontal);

    state[0].pad_type = state[0].pad_type_prev = hidGetNpadDeviceType((HidNpadIdType)0);
    state[0].pad_style = state[0].pad_style_prev = hidGetNpadStyleSet((HidNpadIdType)0);
    if (!(state[0].pad_style & HidNpadStyleTag_NpadJoyDual) &&
        (state[0].pad_type & HidDeviceTypeBits_JoyLeft)) {
        state[0].pad_mapping = (HidNpadButton *)&pad_mapping_left_joy;
    } else if (!(state[0].pad_style & HidNpadStyleTag_NpadJoyDual) &&
               (state[0].pad_type & HidDeviceTypeBits_JoyRight)) {
        state[0].pad_mapping = (HidNpadButton *)&pad_mapping_right_joy;
    } else {
        state[0].pad_mapping = (HidNpadButton *)&pad_mapping_default;
    }

    // initialize pad and vibrations for pad 1 to 7
    for (int i = 1; i < JOYSTICK_COUNT; i++) {
        padInitialize(&state[i].pad, HidNpadIdType_No1 + i);
        padUpdate(&state[i].pad);
        state[i].pad_type = state[i].pad_type_prev = hidGetNpadDeviceType((HidNpadIdType)i);
        state[i].pad_style = state[i].pad_style_prev = hidGetNpadStyleSet((HidNpadIdType)i);
        if (!(state[i].pad_style & HidNpadStyleTag_NpadJoyDual) &&
            (state[i].pad_type & HidDeviceTypeBits_JoyLeft)) {
            state[i].pad_mapping = (HidNpadButton *)&pad_mapping_left_joy;
        } else if (!(state[i].pad_style & HidNpadStyleTag_NpadJoyDual) &&
                   (state[i].pad_type & HidDeviceTypeBits_JoyRight)) {
            state[i].pad_mapping = (HidNpadButton *)&pad_mapping_right_joy;
        } else {
            state[i].pad_mapping = (HidNpadButton *)&pad_mapping_default;
        }
        hidInitializeVibrationDevices(&state[i].vibrationDeviceHandles, 1,
                                      HidNpadIdType_No1 + i, state[i].pad_style);
    }

    for (int i = 0; i < JOYSTICK_COUNT; ++i) {
        SDL_PrivateJoystickAdded(SWITCH_JoystickGetDeviceInstanceID(i));
    }

    return true;
}

static int SWITCH_JoystickGetCount(void)
{
    return JOYSTICK_COUNT;
}

static void SWITCH_JoystickDetect(void)
{
}

static bool SWITCH_JoystickIsDevicePresent(Uint16 vendor_id, Uint16 product_id, Uint16 version, const char *name)
{
    // We don't override any other drivers
    return false;
}

/* Function to get the device-dependent name of a joystick */
static const char *SWITCH_JoystickGetDeviceName(int device_index)
{
    return "Switch Controller";
}

static const char *SWITCH_JoystickGetDevicePath(int index)
{
    return NULL;
}

static int SWITCH_JoystickGetDeviceSteamVirtualGamepadSlot(int device_index)
{
    return -1;
}

static int SWITCH_JoystickGetDevicePlayerIndex(int device_index)
{
    return -1;
}

static void SWITCH_JoystickSetDevicePlayerIndex(int device_index, int player_index)
{
}

static SDL_GUID SWITCH_JoystickGetDeviceGUID(int device_index)
{
    /* the GUID is just the name for now */
    const char *name = SWITCH_JoystickGetDeviceName(device_index);
    return SDL_CreateJoystickGUIDForName(name);
}

/* Function to open a joystick for use.
   The joystick to open is specified by the device index.
   This should fill the nbuttons and naxes fields of the joystick structure.
   It returns 0, or -1 if there is an error.
 */
static bool SWITCH_JoystickOpen(SDL_Joystick *joystick, int device_index)
{
    joystick->nbuttons = sizeof(pad_mapping_default) / sizeof(*pad_mapping_default);
    joystick->naxes = 4;
    joystick->nhats = 0;
    joystick->instance_id = SWITCH_JoystickGetDeviceInstanceID(device_index);

    SDL_SetBooleanProperty(SDL_GetJoystickProperties(joystick), SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, true);

    return true;
}

static bool SWITCH_JoystickRumble(SDL_Joystick *joystick, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble)
{
    int index = joystick->instance_id - 1;

    state[index].vibrationValues.amp_low =
        state[index].vibrationValues.amp_high = low_frequency_rumble == 0 ? 0.0f : 320.0f;
    state[index].vibrationValues.freq_low =
        low_frequency_rumble == 0 ? 160.0f : (float)low_frequency_rumble / 204;
    state[index].vibrationValues.freq_high =
        high_frequency_rumble == 0 ? 320.0f : (float)high_frequency_rumble / 204;

    hidSendVibrationValues(&state[index].vibrationDeviceHandles, &state[index].vibrationValues, 1);

    return true;
}

static bool SWITCH_JoystickRumbleTriggers(SDL_Joystick *joystick, Uint16 left, Uint16 right)
{
    return SDL_Unsupported();
}

static bool SWITCH_JoystickSetLED(SDL_Joystick *joystick, Uint8 red, Uint8 green, Uint8 blue)
{
    return 0;
}

static bool SWITCH_JoystickSendEffect(SDL_Joystick *joystick, const void *data, int size)
{
    return SDL_Unsupported();
}

static bool SWITCH_JoystickSetSensorsEnabled(SDL_Joystick *joystick, bool enabled)
{
    return SDL_Unsupported();
}

/* Function to update the state of a joystick - called as a device poll.
 * This function shouldn't update the joystick structure directly,
 * but instead should call SDL_PrivateJoystick*() to deliver events
 * and update joystick device state.
 */
static void SWITCH_JoystickUpdate(SDL_Joystick *joystick)
{
    Uint64 timestamp = SDL_GetTicksNS();

    u64 diff;
    int index = joystick->instance_id - 1;

    if (index >= JOYSTICK_COUNT || SDL_TextInputActive(SDL_GetKeyboardFocus())) {
        return;
    }

    padUpdate(&state[index].pad);
    if (!padIsConnected(&state[index].pad)) {
        return;
    }

    // update pad type and style, open controller support applet if needed
    state[index].pad_type = hidGetNpadDeviceType((HidNpadIdType)index);
    state[index].pad_style = hidGetNpadStyleSet((HidNpadIdType)index);
    if (state[index].pad_type != state[index].pad_type_prev || state[index].pad_style != state[index].pad_style_prev) {
        SWITCH_UpdateControllerSupport(timestamp, padIsHandheld(&state[index].pad) ? true : false);
        return;
    }

    // only handle axes in non-single joycon mode
    if (state[index].pad_style & HidNpadStyleTag_NpadJoyDual || (state[index].pad_type != HidDeviceTypeBits_JoyLeft && state[index].pad_type != HidDeviceTypeBits_JoyRight)) {
        // axis left
        if (state[index].sticks_old[0].x != state[index].pad.sticks[0].x) {
            SDL_SendJoystickAxis(timestamp, joystick, 0, (Sint16)state[index].pad.sticks[0].x);
            state[index].sticks_old[0].x = state[index].pad.sticks[0].x;
        }
        if (state[index].sticks_old[0].y != state[index].pad.sticks[0].y) {
            SDL_SendJoystickAxis(timestamp, joystick, 1, (Sint16)-state[index].pad.sticks[0].y);
            state[index].sticks_old[0].y = -state[index].pad.sticks[0].y;
        }
        state[index].sticks_old[0] = padGetStickPos(&state[index].pad, 0);
        // axis right
        if (state[index].sticks_old[1].x != state[index].pad.sticks[1].x) {
            SDL_SendJoystickAxis(timestamp, joystick, 2, (Sint16)state[index].pad.sticks[1].x);
            state[index].sticks_old[1].x = state[index].pad.sticks[1].x;
        }
        if (state[index].sticks_old[1].y != state[index].pad.sticks[1].y) {
            SDL_SendJoystickAxis(timestamp, joystick, 3, (Sint16)-state[index].pad.sticks[1].y);
            state[index].sticks_old[1].y = -state[index].pad.sticks[1].y;
        }
        state[index].sticks_old[1] = padGetStickPos(&state[index].pad, 1);
    }

    // handle buttons
    diff = state[index].pad.buttons_old ^ state[index].pad.buttons_cur;
    if (diff) {
        for (int i = 0; i < joystick->nbuttons; i++) {
            if (diff & state[index].pad_mapping[i]) {
                SDL_SendJoystickButton(
                    timestamp,
                    joystick, i,
                    (state[index].pad.buttons_cur & state[index].pad_mapping[i]) != 0);
            }
        }
    }
}

/* Function to close a joystick after use */
static void SWITCH_JoystickClose(SDL_Joystick *joystick)
{
}

/* Function to perform any system-specific joystick related cleanup */
static void SWITCH_JoystickQuit(void)
{
    for (int i = 0; i < JOYSTICK_COUNT; ++i) {
        SDL_PrivateJoystickRemoved(SWITCH_JoystickGetDeviceInstanceID(i));
    }
}

static bool SWITCH_JoystickGetGamepadMapping(int device_index, SDL_GamepadMapping *out)
{
    // clang-format off
    *out = (SDL_GamepadMapping){
#if 0
        /*
         * This is as they are physically.
         */
        .a             = {.kind = EMappingKind_Button, .target =   0},
        .b             = {.kind = EMappingKind_Button, .target =   1},
        .x             = {.kind = EMappingKind_Button, .target =   2},
        .y             = {.kind = EMappingKind_Button, .target =   3},
#else
        /*
         * This is as they are when swapped, i.e. what everything else expects.
         * This also matches the behaviour of the old SDL2 port.
         */
        .a             = {.kind = EMappingKind_Button, .target =   1},
        .b             = {.kind = EMappingKind_Button, .target =   0},
        .x             = {.kind = EMappingKind_Button, .target =   3},
        .y             = {.kind = EMappingKind_Button, .target =   2},
#endif
        .back          = {.kind = EMappingKind_Button, .target =  11},
        .guide         = {.kind = EMappingKind_None,   .target = 255},
        .start         = {.kind = EMappingKind_Button, .target =  10},
        .leftstick     = {.kind = EMappingKind_Button, .target =   4},
        .rightstick    = {.kind = EMappingKind_Button, .target =   5},
        .leftshoulder  = {.kind = EMappingKind_Button, .target =   6},
        .rightshoulder = {.kind = EMappingKind_Button, .target =   7},
        .dpup          = {.kind = EMappingKind_Button, .target =  13},
        .dpdown        = {.kind = EMappingKind_Button, .target =  15},
        .dpleft        = {.kind = EMappingKind_Button, .target =  12},
        .dpright       = {.kind = EMappingKind_Button, .target =  14},
        .misc1         = {.kind = EMappingKind_None,   .target = 255},
        .misc2         = {.kind = EMappingKind_None,   .target = 255},
        .misc3         = {.kind = EMappingKind_None,   .target = 255},
        .misc4         = {.kind = EMappingKind_None,   .target = 255},
        .misc5         = {.kind = EMappingKind_None,   .target = 255},
        .misc6         = {.kind = EMappingKind_None,   .target = 255},
        .right_paddle1 = {.kind = EMappingKind_None,   .target = 255},
        .left_paddle1  = {.kind = EMappingKind_None,   .target = 255},
        .right_paddle2 = {.kind = EMappingKind_None,   .target = 255},
        .left_paddle2  = {.kind = EMappingKind_None,   .target = 255},
        .leftx         = {.kind = EMappingKind_Axis,   .target =   0},
        .lefty         = {.kind = EMappingKind_Axis,   .target =   1},
        .rightx        = {.kind = EMappingKind_Axis,   .target =   2},
        .righty        = {.kind = EMappingKind_Axis,   .target =   3},
        .lefttrigger   = {.kind = EMappingKind_Button, .target =   8},
        .righttrigger  = {.kind = EMappingKind_Button, .target =   9},
        .touchpad      = {.kind = EMappingKind_None,   .target = 255},
    };
    // clang-format on
    return true;
}

SDL_JoystickDriver SDL_SWITCH_JoystickDriver = {
    .Init = SWITCH_JoystickInit,
    .GetCount = SWITCH_JoystickGetCount,
    .Detect = SWITCH_JoystickDetect,
    .IsDevicePresent = SWITCH_JoystickIsDevicePresent,
    .GetDeviceName = SWITCH_JoystickGetDeviceName,
    .GetDevicePath = SWITCH_JoystickGetDevicePath,
    .GetDeviceSteamVirtualGamepadSlot = SWITCH_JoystickGetDeviceSteamVirtualGamepadSlot,
    .GetDevicePlayerIndex = SWITCH_JoystickGetDevicePlayerIndex,
    .SetDevicePlayerIndex = SWITCH_JoystickSetDevicePlayerIndex,
    .GetDeviceGUID = SWITCH_JoystickGetDeviceGUID,
    .GetDeviceInstanceID = SWITCH_JoystickGetDeviceInstanceID,

    .Open = SWITCH_JoystickOpen,

    .Rumble = SWITCH_JoystickRumble,
    .RumbleTriggers = SWITCH_JoystickRumbleTriggers,

    .SetLED = SWITCH_JoystickSetLED,
    .SendEffect = SWITCH_JoystickSendEffect,
    .SetSensorsEnabled = SWITCH_JoystickSetSensorsEnabled,

    .Update = SWITCH_JoystickUpdate,
    .Close = SWITCH_JoystickClose,
    .Quit = SWITCH_JoystickQuit,

    .GetGamepadMapping = SWITCH_JoystickGetGamepadMapping,
};

#endif /* SDL_JOYSTICK_SWITCH */

/* vi: set ts=4 sw=4 expandtab: */
