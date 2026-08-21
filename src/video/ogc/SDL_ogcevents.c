/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2023 Sam Lantinga <slouken@libsdl.org>

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

#ifdef SDL_VIDEO_DRIVER_OGC

#include "../../events/SDL_events_c.h"

#include "SDL_ogcevents_c.h"
#include "SDL_ogckeyboard.h"
#include "SDL_ogcmouse.h"
#include "SDL_ogcvideo.h"

#include <ogc/system.h>

/* These variables can be set from the handlers registered in SDL_main() */
bool OGC_PowerOffRequested = false;
bool OGC_ResetRequested = false;

int OGC_NumControllers = 0;

/* This is the array of structs holding the controller data */
static _OGC_Controller s_controllers[OGC_MAX_CONTROLLERS];
/* This is an array of indexes to the previous array: this is done to
 * avoid moving the data when a controller is removed, and to preserve the
 * IDs. */
static u8 s_controller_indices[OGC_MAX_CONTROLLERS];
static SDL_JoystickID s_next_instance_id = 1;
static _OGC_ControllerCb s_joystick_added_cb = NULL;
static _OGC_ControllerCb s_joystick_removed_cb = NULL;

#ifdef __wii__
#define MAX_WII_MOUSE_BUTTONS 2
static const struct {
    int wii;
    int mouse;
} s_mouse_button_map[MAX_WII_MOUSE_BUTTONS] = {
    { EGC_GAMEPAD_BUTTON_SOUTH, SDL_BUTTON_LEFT },
    { EGC_GAMEPAD_BUTTON_EAST, SDL_BUTTON_RIGHT },
};

static void pump_ir_events(SDL_VideoDevice *_this)
{
    int screen_w, screen_h;

    if (!_this->windows) return;

    screen_w = _this->displays[0]->current_mode->w;
    screen_h = _this->displays[0]->current_mode->h;

    for (int i = 0; i < OGC_NumControllers; i++) {
        _OGC_Controller *controller = OGC_get_controller(i);
        egc_input_device_t *device = controller->egc_device;
        egc_point_t point;

        if (device->desc->num_touch_points == 0) continue;

        point = egc_input_device_read_touch_point(device, 0);
        if (point.x < 0) continue;

        SDL_SendMouseMotion(0, _this->windows, i, 0,
                            point.x * screen_w / EGC_GAMEPAD_TOUCH_RES,
                            point.y * screen_h / EGC_GAMEPAD_TOUCH_RES);

        for (int b = 0; b < MAX_WII_MOUSE_BUTTONS; b++) {
            if (controller->btns_pressed & s_mouse_button_map[b].wii) {
                SDL_SendMouseButton(0, _this->windows, i,
                                    s_mouse_button_map[b].mouse, true);
            }
            if (controller->btns_released & s_mouse_button_map[b].wii) {
                SDL_SendMouseButton(0, _this->windows, i,
                                    s_mouse_button_map[b].mouse, false);
            }
        }
    }

    if (OGC_prep_draw_cursor(_this)) {
        OGC_video_flip(_this, false);
    }
}
#endif

_OGC_Controller *OGC_get_controller(int i)
{
    if (i < 0 || i >= OGC_NumControllers) return NULL;
    return &s_controllers[s_controller_indices[i]];
}

void OGC_PumpEvents(SDL_VideoDevice *_this)
{
    if (OGC_ResetRequested || OGC_PowerOffRequested) {
        SDL_Event ev;
        ev.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&ev);
        if (OGC_PowerOffRequested) {
            SYS_ResetSystem(SYS_POWEROFF, 0, 0);
        }
    }

    egc_handle_events();
    for (int i = 0; i < OGC_NumControllers; i++) {
        _OGC_Controller *controller = OGC_get_controller(i);
        u32 buttons = egc_input_device_read_buttons(controller->egc_device);
        controller->btns_held = controller->btns_prev & buttons;
        controller->btns_released = controller->btns_prev & ~buttons;
        controller->btns_pressed = buttons & controller->btns_prev;
        controller->btns_prev = buttons;
    }

#ifdef __wii__
    pump_ir_events(_this);
    OGC_PumpKeyboardEvents(_this);
#endif
}

void OGC_device_added_cb(egc_input_device_t *device, void *userdata)
{
    _OGC_Controller *controller;

    int free_index = -1;
    for (int i = 0; i < OGC_MAX_CONTROLLERS; i++) {
        controller = &s_controllers[i];
        if (controller->egc_device == NULL) {
            free_index = i;
            break;
        }
    }
    if (free_index < 0) return;

    memset(controller, 0, sizeof(*controller));
    controller->egc_device = device;
    controller->instance_id = s_next_instance_id++;
    s_controller_indices[OGC_NumControllers++] = free_index;

#ifdef __wii__
    egc_bt_stop_scan();
#endif

    if (s_joystick_added_cb) {
        s_joystick_added_cb(controller);
    }
}

void OGC_device_removed_cb(egc_input_device_t *device, void *userdata)
{
    _OGC_Controller *controller = NULL;
    int found_index = -1;
    for (int i = 0; i < OGC_NumControllers; i++) {
        controller = OGC_get_controller(i);
        if (controller->egc_device == device) {
            found_index = i;
            controller->egc_device = NULL;
            break;
        }
    }
    if (found_index < 0) return;

    if (s_joystick_removed_cb) {
        s_joystick_removed_cb(controller);
    }

    OGC_NumControllers--;
    /* Move back all later indices by one position */
    for (int i = found_index; i < OGC_NumControllers; i++) {
        s_controller_indices[i] = s_controller_indices[i + 1];
    }

#ifdef __wii__
    if (OGC_NumControllers == 0) {
        egc_bt_start_scan();
    }
#endif

}

void OGC_register_joystick_callbacks(_OGC_ControllerCb added_cb,
                                     _OGC_ControllerCb removed_cb)
{
    s_joystick_added_cb = added_cb;
    s_joystick_removed_cb = removed_cb;
}

#endif /* SDL_VIDEO_DRIVER_OGC */

/* vi: set ts=4 sw=4 expandtab: */
