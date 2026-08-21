/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

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

#ifdef SDL_JOYSTICK_OGC

#include "../SDL_joystick_c.h"
#include "../SDL_sysjoystick.h"
#include "../usb_ids.h"
#include "../../SDL_hints_c.h"
#include "../../video/ogc/SDL_ogcevents_c.h"

#include <embedded-game-controller/drivers/wiimote.h>
#include <gccore.h>
#include <limits.h>
#include <math.h>
#include <unistd.h>

#define MAX_RUMBLE 8

/* The private structure used to keep track of a joystick */
typedef struct joystick_hwdata
{
    _OGC_Controller *controller;
#ifdef __wii__
    bool rotated;  /* Wiimote rotated sideways */
    EgcWiimoteExpType expansion;
#endif
    char sensors_disabled;
    /*  This must be big enough for MAX_RUMBLE */
    char rumble_intensity;
    u16 rumble_loop;
    u32 prev_buttons;
} joystick_hwdata;

static char joy_name[128];

static int print_controller_name(char *buffer, size_t size,
                                 _OGC_Controller *controller);

#define DPAD_MASK \
    ((1 << EGC_GAMEPAD_BUTTON_DPAD_UP) | \
     (1 << EGC_GAMEPAD_BUTTON_DPAD_DOWN) | \
     (1 << EGC_GAMEPAD_BUTTON_DPAD_LEFT) | \
     (1 << EGC_GAMEPAD_BUTTON_DPAD_RIGHT))

#ifdef __wii__
static bool is_wiimote(egc_input_device_t *device)
{
    const egc_device_description_t *desc = device->desc;
    return desc->vendor_id == USB_VENDOR_NINTENDO &&
        desc->product_id == USB_PRODUCT_NINTENDO_WII_REMOTE;
}

/* Return EGC_WIIMOTE_EXP_NONE if this is not a wiimote or if no expansion
 * is connected (the Wiimote+ also count as no expansion, since it's not
 * usable as a separate controller). */
static EgcWiimoteExpType get_wiimote_expansion(egc_input_device_t *device)
{
    return is_wiimote(device) ?
        egc_driver_wiimote_get_exp_type(device) : EGC_WIIMOTE_EXP_NONE;
}

static const char *expansion_name(EgcWiimoteExpType expansion)
{
    switch (expansion) {
    case EGC_WIIMOTE_EXP_MOTION_PLUS:
        return "Motion+";
    case EGC_WIIMOTE_EXP_NUNCHUCK:
        return "Nunchuk";
    case EGC_WIIMOTE_EXP_CLASSIC:
    case EGC_WIIMOTE_EXP_CLASSIC_PRO:
        return "Classic";
    case EGC_WIIMOTE_EXP_GUITAR_HERO_3:
        return "Guitar Hero 3";
    case EGC_WIIMOTE_EXP_BALANCE_BOARD:
        return "Balance board";
    default:
        return "Unknown";
    }
}

const char *connection_name(egc_input_device_t *device)
{
    switch (device->connection) {
    case EGC_CONNECTION_USB:
        return "Usb";
    case EGC_CONNECTION_BT:
        return "Bt";
    default:
        return "";
    }
}

static void handle_accelerometer(Uint64 timestamp,
                                 SDL_Joystick *joystick,
                                 SDL_SensorType type,
                                 int start_axis,
                                 const egc_accelerometer_t *accel,
                                 bool rotated)
{
    float values[3];
    egc_accelerometer_t a = *accel;
    if (rotated) {
        a.x = accel->z;
        a.z = -accel->x;
    }

    values[0] = a.x * SDL_STANDARD_GRAVITY / EGC_ACCELEROMETER_RES_PER_G;
    values[1] = a.y * SDL_STANDARD_GRAVITY / EGC_ACCELEROMETER_RES_PER_G;
    values[2] = a.z * SDL_STANDARD_GRAVITY / EGC_ACCELEROMETER_RES_PER_G;
    SDL_SendJoystickSensor(timestamp, joystick, type, 0, values, 3);
}
#endif

static inline int count_bits(u32 mask)
{
    return __builtin_popcount(mask);
}

static int controller_index_by_device_index(int device_index)
{
    return device_index;
}

static _OGC_Controller *controller_by_device_index(int device_index)
{
    return OGC_get_controller(controller_index_by_device_index(device_index));
}

static int print_controller_name(char *buffer, size_t size,
                                 _OGC_Controller *controller)
{
    const egc_device_description_t *desc = controller->egc_device->desc;
    if (desc->vendor_id == USB_VENDOR_NINTENDO) {
        /* egc's GameCube driver reports this ID for GameCube controllers */
        if (desc->product_id == USB_PRODUCT_NINTENDO_GAMECUBE_ADAPTER)
            return snprintf(buffer, size, "Gamecube");
#ifdef __wii__
        if (desc->product_id == USB_PRODUCT_NINTENDO_WII_REMOTE)
            return snprintf(buffer, size, "Wiimote");
#endif
    }

#ifdef __wii__
    return snprintf(buffer, size, "%s:%04x:%04x",
                    connection_name(controller->egc_device),
                    desc->vendor_id, desc->product_id);
#else
    return 0;
#endif
}

static inline _OGC_Controller *get_controller(SDL_Joystick *joystick)
{
    return joystick->hwdata ? joystick->hwdata->controller : NULL;
}

static inline egc_input_device_t *get_egc_device(SDL_Joystick *joystick)
{
    _OGC_Controller *controller = get_controller(joystick);
    return controller ? controller->egc_device : NULL;
}

static int device_index_to_instance(int device_index)
{
    _OGC_Controller *controller = controller_by_device_index(device_index);
    if (!controller)
        return -1;
    return controller->instance_id;
}

static void joystick_added_cb(_OGC_Controller *controller)
{
    SDL_PrivateJoystickAdded(controller->instance_id);
}

static void joystick_removed_cb(_OGC_Controller *controller)
{
    SDL_PrivateJoystickRemoved(controller->instance_id);
}

/* Function to scan the system for joysticks.
 * This function should return the number of available
 * joysticks.  Joystick 0 should be the system default joystick.
 * It should return -1 on an unrecoverable fatal error.
 */
static bool OGC_JoystickInit(void)
{
#ifdef __wii__
    /* If this is set, the Wiimote directional keys will be translated. */
    {
        const char *sideways_joystick_env = getenv("SDL_WII_JOYSTICK_SIDEWAYS");
        bool wiimote_sideways =
            sideways_joystick_env && strcmp(sideways_joystick_env, "1") == 0;
        egc_driver_wiimote_set_sideways_default(wiimote_sideways);
    }
    /* Enable Wiimote's IR */
    egc_input_device_enable_touch_point_default(true);

    /* By default, all sensors in SDL are disabled */
    egc_input_device_enable_accelerometer_default(false);
    egc_input_device_enable_gyroscope_default(false);
#endif

    OGC_register_joystick_callbacks(joystick_added_cb, joystick_removed_cb);
    return true;
}

static int OGC_JoystickGetCount(void)
{
    return OGC_NumControllers;
}

static void OGC_JoystickDetect(void)
{
}

static bool OGC_JoystickIsDevicePresent(Uint16 vendor_id, Uint16 product_id, Uint16 version, const char *name)
{
    // We don't override any other drivers
    return false;
}

/* Function to get the device-dependent name of a joystick */
static const char *OGC_JoystickGetDeviceName(int device_index)
{
    _OGC_Controller *controller;
    const egc_device_description_t *desc;
    int offset = 0;

    controller = OGC_get_controller(device_index);
    if (!controller)
        return NULL;

    offset += print_controller_name(joy_name, sizeof(joy_name), controller);
    offset += snprintf(joy_name + offset, sizeof(joy_name) - offset,
                       " %d", device_index);
#ifdef __wii__
    desc = controller->egc_device->desc;
    if (desc->vendor_id == USB_VENDOR_NINTENDO &&
        desc->product_id == USB_PRODUCT_NINTENDO_WII_REMOTE) {
        EgcWiimoteExpType expansion =
            egc_driver_wiimote_get_exp_type(controller->egc_device);
        if (expansion != EGC_WIIMOTE_EXP_NONE) {
            offset += snprintf(joy_name + offset, sizeof(joy_name) - offset,
                               " + %s", expansion_name(expansion));
        }
    }
#endif
    return joy_name;
}

static const char *OGC_JoystickGetDevicePath(int index)
{
    return NULL;
}

static int OGC_JoystickGetDeviceSteamVirtualGamepadSlot(int device_index)
{
    return -1;
}

static int OGC_JoystickGetDevicePlayerIndex(int device_index)
{
    return -1;
}

static void OGC_JoystickSetDevicePlayerIndex(int device_index, int player_index)
{
    int index = controller_index_by_device_index(device_index);
    _OGC_Controller *controller = OGC_get_controller(index);
    egc_input_device_t *device;
    u32 num_leds, leds;

    if (!controller) return;

    device = controller->egc_device;
    if (!device) return;

    num_leds = device->desc->num_leds;
    if (num_leds <= 1) return;

    if (player_index < num_leds) {
        leds = 1 << player_index;
    } else {
        /* Invert the leds; first, set all leds on: */
        leds = (1 << num_leds) - 1;
        /* then remove the led */
        leds ^= (1 << (player_index - num_leds));
    }

    egc_input_device_set_leds(device, leds);
}

static SDL_GUID OGC_JoystickGetDeviceGUID(int device_index)
{
    int index = controller_index_by_device_index(device_index);
    _OGC_Controller *controller = OGC_get_controller(index);
    const egc_device_description_t *desc;
    Uint16 bus, product, version;
    Uint8 driver_signature, driver_data;
    const char *name;

    desc = controller->egc_device->desc;
    product = desc->product_id;
#ifdef __wii__
    /* Return a different product ID depending on the connected expansion */
    product += get_wiimote_expansion(controller->egc_device);
#endif
    if (controller->egc_device->connection == EGC_CONNECTION_BT) {
        bus = SDL_HARDWARE_BUS_BLUETOOTH;
    } else if (controller->egc_device->connection == EGC_CONNECTION_USB) {
        bus = SDL_HARDWARE_BUS_USB;
    } else {
        bus = SDL_HARDWARE_BUS_UNKNOWN;
    }
    version = 1;
    driver_signature = 0;
    driver_data = 0;

    name = OGC_JoystickGetDeviceName(device_index);
    return SDL_CreateJoystickGUID(bus, desc->vendor_id, product, version,
                                  NULL, name, driver_signature, driver_data);
}

static SDL_JoystickID OGC_JoystickGetDeviceInstanceID(int device_index)
{
    return device_index_to_instance(device_index);
}

static bool OGC_JoystickOpen(SDL_Joystick *joystick, int device_index)
{
    int index = controller_index_by_device_index(device_index);
    _OGC_Controller *controller;
    const egc_device_description_t *desc;
    u32 dpad_buttons, buttons_excluding_dpad;

    SDL_Log("Open joystick %d (our index: %d)", device_index, index);

    if (index < 0)
        return false;

    /* allocate memory for system specific hardware data */
    joystick->hwdata = SDL_malloc(sizeof(joystick_hwdata));
    if (joystick->hwdata == NULL) {
        SDL_OutOfMemory();
        return false;
    }
    controller = OGC_get_controller(index);
    desc = controller->egc_device->desc;
    joystick->instance_id = controller->instance_id;

    SDL_memset(joystick->hwdata, 0, sizeof(joystick_hwdata));
    joystick->hwdata->controller = controller;
    /* Because of https://github.com/libsdl-org/SDL/issues/8754 (hats and axes
     * being the only way to provide direction information with the SDL
     * joystick API) we need to convert dpad buttons into a hat.
     */
    dpad_buttons = desc->available_buttons & DPAD_MASK;
    buttons_excluding_dpad = desc->available_buttons & ~DPAD_MASK;
    joystick->nbuttons = count_bits(buttons_excluding_dpad);
    joystick->naxes = count_bits(desc->available_axes);
    joystick->nhats = dpad_buttons != 0 ? 1 : 0;
    SDL_PropertiesID props = SDL_GetJoystickProperties(joystick);
    SDL_SetBooleanProperty(props, SDL_PROP_JOYSTICK_CAP_PLAYER_LED_BOOLEAN,
                           desc->num_leds > 0);
    SDL_SetBooleanProperty(props, SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN,
                           desc->has_rumble);

#ifdef __wii__
    if (desc->num_accelerometers > 0) {
        SDL_PrivateJoystickAddSensor(joystick, SDL_SENSOR_ACCEL, 100.0f);
        if (desc->num_accelerometers > 1) {
            SDL_PrivateJoystickAddSensor(joystick, SDL_SENSOR_ACCEL_L, 100.0f);
        }
    }
#endif /* __wii__ */
    return true;
}

static bool OGC_JoystickRumble(SDL_Joystick *joystick,
                       Uint16 low_frequency_rumble,
                       Uint16 high_frequency_rumble)
{
    int rc;
    egc_input_device_t *device = get_egc_device(joystick);
    if (!device->desc->has_rumble) {
        return SDL_Unsupported();
    }

    rc = egc_input_device_set_rumble(device,
                                     low_frequency_rumble,
                                     high_frequency_rumble);
    return rc == 0;
}

static bool OGC_JoystickRumbleTriggers(SDL_Joystick *joystick,
                               Uint16 left_rumble, Uint16 right_rumble)
{
    return SDL_Unsupported();
}

static bool OGC_JoystickSetLED(SDL_Joystick *joystick,
                       Uint8 red, Uint8 green, Uint8 blue)
{
    return SDL_Unsupported();
}

static bool OGC_JoystickSendEffect(SDL_Joystick *joystick, const void *data, int size)
{
    return SDL_Unsupported();
}

static bool OGC_JoystickSetSensorsEnabled(SDL_Joystick *joystick, bool enabled)
{
    /* There is nothing console-specific here, but since the GameCube does not
     * support any controllers with sensors, we disable this code in order to
     * save some memory */
#ifdef __wii__
    egc_input_device_t *device = get_egc_device(joystick);
    if (!device) {
        return false;
    }

    for (int i = 0; i < device->desc->num_accelerometers; i++) {
        egc_input_device_enable_accelerometer(device, i, enabled);
    }

    for (int i = 0; i < device->desc->num_gyroscopes; i++) {
        egc_input_device_enable_gyroscope(device, i, enabled);
    }
    return true;
#else /* GameCUbe: */
    return SDL_Unsupported();
#endif /* __wii__ */
}

static void OGC_JoystickUpdate(SDL_Joystick *joystick)
{
    _OGC_Controller *controller;
    egc_input_device_t *device;
    u32 buttons, prev_buttons, changed, available_buttons;
    int i_button = 0, i_axis = 0;

    Uint64 timestamp = SDL_GetTicksNS();

    controller = get_controller(joystick);
    if (!controller) return;

    device = get_egc_device(joystick);
    if (!device) return;

#ifdef __wii__
    if (get_wiimote_expansion(device) != joystick->hwdata->expansion) {
        /* If the expansion changes we need setup a different mapping, and the
         * simplest way to do this is to pretend that the joystick was
         * disconnected and reconnected. */
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT,
                     "Expansion changed, reconnecting joystick");
        SDL_PrivateJoystickRemoved(controller->instance_id);
        SDL_PrivateJoystickAdded(controller->instance_id);
        return;
    }
#endif /* __wii__ */

    available_buttons = device->desc->available_buttons & ~DPAD_MASK;
    prev_buttons = joystick->hwdata->prev_buttons;
    buttons = egc_input_device_read_buttons(device);
    changed = buttons ^ prev_buttons;

    for (int i = 0; i < EGC_GAMEPAD_BUTTON_COUNT; i++) {
        u32 mask = 1 << i;
        if (!(available_buttons & mask)) continue;
        if (changed & mask) {
            SDL_SendJoystickButton(timestamp, joystick, i_button, (buttons & mask) != 0);
        }
        i_button++;
    }
    /* Handle D-Pad as a hat */
    if (changed & DPAD_MASK) {
        int hat = SDL_HAT_CENTERED;
        if (buttons & (1 << EGC_GAMEPAD_BUTTON_DPAD_UP)) hat |= SDL_HAT_UP;
        if (buttons & (1 << EGC_GAMEPAD_BUTTON_DPAD_RIGHT)) hat |= SDL_HAT_RIGHT;
        if (buttons & (1 << EGC_GAMEPAD_BUTTON_DPAD_DOWN)) hat |= SDL_HAT_DOWN;
        if (buttons & (1 << EGC_GAMEPAD_BUTTON_DPAD_LEFT)) hat |= SDL_HAT_LEFT;
#ifdef __wii__
        if (joystick->hwdata->rotated) {
            /* We can just rotate the bits of the hat mask */
            hat = (hat >> 1) | ((hat & 0x1) << 3);
        }
#endif /* __wii__ */
        SDL_SendJoystickHat(timestamp, joystick, 0, hat);
    }

    joystick->hwdata->prev_buttons = buttons;

    for (int i = 0; i < EGC_GAMEPAD_AXIS_COUNT; i++) {
        u32 mask = 1 << i;
        if (device->desc->available_axes & mask) {
            s16 value = egc_input_device_read_axis(device, i);
            if (i == EGC_GAMEPAD_AXIS_LEFT_TRIGGER ||
                i == EGC_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                /* The SDL joystick API expects trigger values to be in the
                 * full range of an int16, so we need to do some scaling here.
                 */
                value = (value * 2) + INT16_MIN;
            }
            SDL_SendJoystickAxis(timestamp, joystick, i_axis, value);
            i_axis++;
        }
    }

#ifdef __wii__
    if (device->desc->num_accelerometers > 0) {
        const egc_accelerometer_t *accel =
            egc_input_device_read_accelerometer(device, 0);
        handle_accelerometer(timestamp, joystick, SDL_SENSOR_ACCEL,
                             i_axis, accel,
                             joystick->hwdata->rotated);
        i_axis += 2;
        if (device->desc->num_accelerometers > 1) {
            accel = egc_input_device_read_accelerometer(device, 1);
            handle_accelerometer(timestamp, joystick, SDL_SENSOR_ACCEL_L,
                                 i_axis, accel, false);
        }
    }

    if (device->desc->num_gyroscopes) {
        const egc_gyroscope_t *gyro =
            egc_input_device_read_gyroscope(device, 0);
        float values[3];
        values[0] = (float)gyro->x / EGC_GYROSCOPE_RES;
        values[1] = (float)gyro->y / EGC_GYROSCOPE_RES;
        values[2] = (float)gyro->z / EGC_GYROSCOPE_RES;
        SDL_SendJoystickSensor(timestamp, joystick, SDL_SENSOR_GYRO, 0, values, 3);
    }
#endif
}

static void OGC_JoystickClose(SDL_Joystick *joystick)
{
    if (!joystick || !joystick->hwdata) // joystick already closed
        return;

    SDL_free(joystick->hwdata);
    joystick->hwdata = NULL;
}

void OGC_JoystickQuit(void)
{
}

static const SDL_InputMapping s_invalid_mapping = { EMappingKind_None, 255 };

static SDL_InputMapping egc_map_button(u32 available, int index, int *i_button)
{
    return (1 << index) & available ?
        (SDL_InputMapping){ EMappingKind_Button, (*i_button)++ } : s_invalid_mapping;
}

static SDL_InputMapping egc_map_hat(u32 available, int index, int i_hat)
{
    return (1 << index) & available ?
        (SDL_InputMapping){ EMappingKind_Hat, i_hat } : s_invalid_mapping;
}

static SDL_InputMapping egc_map_axis(u32 available, int index, int *i_axis)
{
    return (1 << index) & available ?
        (SDL_InputMapping){ EMappingKind_Axis, (*i_axis)++ } : s_invalid_mapping;
}

static bool OGC_JoystickGetGamepadMapping(int device_index, SDL_GamepadMapping *out)
{
    egc_input_device_t *device;
    u32 buttons, axes;
    int i_button = 0, i_axis = 0;
    _OGC_Controller *controller = OGC_get_controller(device_index);
    if (!controller)
        return false;

    device = controller->egc_device;
    buttons = device->desc->available_buttons;
    axes = device->desc->available_axes;

    *out = (SDL_GamepadMapping){
        .b = egc_map_button(buttons, EGC_GAMEPAD_BUTTON_SOUTH, &i_button),
        .a = egc_map_button(buttons, EGC_GAMEPAD_BUTTON_EAST, &i_button),
        .y = egc_map_button(buttons, EGC_GAMEPAD_BUTTON_WEST, &i_button),
        .x = egc_map_button(buttons, EGC_GAMEPAD_BUTTON_NORTH, &i_button),
        .back = egc_map_button(buttons, EGC_GAMEPAD_BUTTON_BACK, &i_button),
        .guide = egc_map_button(buttons, EGC_GAMEPAD_BUTTON_GUIDE, &i_button),
        .start = egc_map_button(buttons, EGC_GAMEPAD_BUTTON_START, &i_button),
        .leftstick = egc_map_button(buttons, EGC_GAMEPAD_BUTTON_LEFT_STICK, &i_button),
        .rightstick = egc_map_button(buttons, EGC_GAMEPAD_BUTTON_RIGHT_STICK, &i_button),
        .leftshoulder = egc_map_button(buttons, EGC_GAMEPAD_BUTTON_LEFT_SHOULDER, &i_button),
        .rightshoulder = egc_map_button(buttons, EGC_GAMEPAD_BUTTON_RIGHT_SHOULDER, &i_button),
        .dpup = egc_map_hat(buttons, EGC_GAMEPAD_BUTTON_DPAD_UP, SDL_HAT_UP),
        .dpdown = egc_map_hat(buttons, EGC_GAMEPAD_BUTTON_DPAD_DOWN, SDL_HAT_DOWN),
        .dpleft = egc_map_hat(buttons, EGC_GAMEPAD_BUTTON_DPAD_LEFT, SDL_HAT_LEFT),
        .dpright = egc_map_hat(buttons, EGC_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_HAT_RIGHT),
        .misc1 = egc_map_button(buttons, EGC_GAMEPAD_BUTTON_MISC1, &i_button),
        .right_paddle1 = egc_map_button(buttons, EGC_GAMEPAD_BUTTON_RIGHT_PADDLE1, &i_button),
        .left_paddle1 = egc_map_button(buttons, EGC_GAMEPAD_BUTTON_LEFT_PADDLE1, &i_button),
        .right_paddle2 = egc_map_button(buttons, EGC_GAMEPAD_BUTTON_RIGHT_PADDLE2, &i_button),
        .left_paddle2 = egc_map_button(buttons, EGC_GAMEPAD_BUTTON_LEFT_PADDLE2, &i_button),
        .leftx = egc_map_axis(axes, EGC_GAMEPAD_AXIS_LEFTX, &i_axis),
        .lefty = egc_map_axis(axes, EGC_GAMEPAD_AXIS_LEFTY, &i_axis),
        .rightx = egc_map_axis(axes, EGC_GAMEPAD_AXIS_RIGHTX, &i_axis),
        .righty = egc_map_axis(axes, EGC_GAMEPAD_AXIS_RIGHTY, &i_axis),
        .lefttrigger = egc_map_axis(axes, EGC_GAMEPAD_AXIS_LEFT_TRIGGER, &i_axis),
        .righttrigger = egc_map_axis(axes, EGC_GAMEPAD_AXIS_RIGHT_TRIGGER, &i_axis),
    };

    return true;
}

SDL_JoystickDriver SDL_OGC_JoystickDriver = {
    OGC_JoystickInit,
    OGC_JoystickGetCount,
    OGC_JoystickDetect,
    OGC_JoystickIsDevicePresent,
    OGC_JoystickGetDeviceName,
    OGC_JoystickGetDevicePath,
    OGC_JoystickGetDeviceSteamVirtualGamepadSlot,
    OGC_JoystickGetDevicePlayerIndex,
    OGC_JoystickSetDevicePlayerIndex,
    OGC_JoystickGetDeviceGUID,
    OGC_JoystickGetDeviceInstanceID,
    OGC_JoystickOpen,
    OGC_JoystickRumble,
    OGC_JoystickRumbleTriggers,
    OGC_JoystickSetLED,
    OGC_JoystickSendEffect,
    OGC_JoystickSetSensorsEnabled,
    OGC_JoystickUpdate,
    OGC_JoystickClose,
    OGC_JoystickQuit,
    OGC_JoystickGetGamepadMapping,
};

#endif /* SDL_JOYSTICK_OGC */
