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

#if SDL_VIDEO_DRIVER_SWITCH

#include <switch.h>

#include "../../events/SDL_mouse_c.h"
#include "SDL_switchmouse_c.h"

static uint64_t prev_buttons = 0;
static uint64_t last_timestamp = 0;
const uint64_t mouse_read_interval = 15; // in ms
static SDL_MouseID mouse_id = 1;

static bool SWITCH_SetRelativeMouseMode(bool enabled)
{
    return true;
}

void SWITCH_InitMouse(void)
{
    SDL_Mouse *mouse = SDL_GetMouse();
    mouse->SetRelativeMouseMode = SWITCH_SetRelativeMouseMode;
    hidInitializeMouse();

    SDL_AddMouse(1, NULL, false);
}

void SWITCH_PollMouse(Uint64 timestamp)
{
    SDL_Window *window = SDL_GetKeyboardFocus();
    HidMouseState mouse_state;
    size_t state_count;
    uint64_t changed_buttons;
    int dx, dy;

    // We skip polling mouse if no window is created
    if (window == NULL) {
        return;
    }

    state_count = hidGetMouseStates(&mouse_state, 1);
    changed_buttons = mouse_state.buttons ^ prev_buttons;

    if (changed_buttons & HidMouseButton_Left) {
        if (prev_buttons & HidMouseButton_Left) {
            SDL_SendMouseButton(timestamp, window, mouse_id, SDL_BUTTON_LEFT, false);
        } else {
            SDL_SendMouseButton(timestamp, window, mouse_id, SDL_BUTTON_LEFT, true);
        }
    }
    if (changed_buttons & HidMouseButton_Right) {
        if (prev_buttons & HidMouseButton_Right) {
            SDL_SendMouseButton(timestamp, window, mouse_id, SDL_BUTTON_RIGHT, false);
        } else {
            SDL_SendMouseButton(timestamp, window, mouse_id, SDL_BUTTON_RIGHT, true);
        }
    }
    if (changed_buttons & HidMouseButton_Middle) {
        if (prev_buttons & HidMouseButton_Middle) {
            SDL_SendMouseButton(timestamp, window, mouse_id, SDL_BUTTON_MIDDLE, false);
        } else {
            SDL_SendMouseButton(timestamp, window, mouse_id, SDL_BUTTON_MIDDLE, true);
        }
    }

    prev_buttons = mouse_state.buttons;

    if (timestamp > last_timestamp + mouse_read_interval) {
        // if hidMouseRead is called once per frame, a factor two on the velocities
        // results in approximately the same mouse motion as reported by mouse_pos.x and mouse_pos.y
        // but without the clamping to 1280 x 720
        if (state_count > 0) {
            dx = mouse_state.delta_x * 2;
            dy = mouse_state.delta_y * 2;
            if (dx || dy) {
                SDL_SendMouseMotion(timestamp, window, mouse_id, 1, dx, dy);
            }
        }
        last_timestamp = timestamp;
    }
}

void SWITCH_QuitMouse(void)
{
    SDL_RemoveMouse(mouse_id, false);
}

#endif /* SDL_VIDEO_DRIVER_SWITCH */

/* vi: set ts=4 sw=4 expandtab: */
