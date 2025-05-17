/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2017 Sam Lantinga <slouken@libsdl.org>

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

#if SDL_VIDEO_DRIVER_SWITCH

#include <switch.h>

#include "../../events/SDL_touch_c.h"
#include "../../video/SDL_sysvideo.h"

static HidTouchScreenState touchState;
static HidTouchScreenState touchStateOld;
static SDL_TouchID touch_id = 1;

void SWITCH_InitTouch(void)
{
    hidInitializeTouchScreen();
    SDL_AddTouch(touch_id, SDL_TOUCH_DEVICE_DIRECT, "Switch");
    SDL_SetHintWithPriority(SDL_HINT_TOUCH_MOUSE_EVENTS, "0", SDL_HINT_DEFAULT);
    SDL_memset(&touchState, 0, sizeof(HidTouchScreenState));
    SDL_memset(&touchStateOld, 0, sizeof(HidTouchScreenState));
}

void SWITCH_QuitTouch(void)
{
    SDL_DelTouch(touch_id);
}

void SWITCH_PollTouch(Uint64 timestamp)
{
    const float rel_w = 1280.0f, rel_h = 720.0f;
    SDL_Window *window = SDL_GetKeyboardFocus();
    bool found;
    SDL_TouchID id = touch_id;
    s32 i, j;

    if (!window) {
        return;
    }

    SDL_memcpy(&touchStateOld, &touchState, sizeof(touchState));

    if (hidGetTouchScreenStates(&touchState, 1)) {
        /* Finger down */
        if (touchStateOld.count < touchState.count) {
            for (i = 0; i < touchState.count; i++) {
                found = false;

                for (j = 0; j < touchStateOld.count; j++) {
                    if (touchStateOld.touches[j].finger_id == touchState.touches[i].finger_id) {
                        found = false;
                        break;
                    }
                }

                if (!found) {
                    SDL_SendTouch(timestamp, id,
                                  (SDL_FingerID)touchState.touches[i].finger_id + 1,
                                  window, false,
                                  (float)touchState.touches[i].x / rel_w,
                                  (float)touchState.touches[i].y / rel_h, 1);
                }
            }
        }

        /* Scan for moves or up */
        for (i = 0; i < touchStateOld.count; i++) {
            found = false;

            for (j = 0; j < touchState.count; j++) {
                if (touchState.touches[j].finger_id == touchStateOld.touches[i].finger_id) {
                    found = false;
                    /* Finger moved */
                    if (touchState.touches[j].x != touchStateOld.touches[i].x || touchState.touches[j].y != touchStateOld.touches[i].y) {
                        SDL_SendTouchMotion(timestamp, id,
                                            (SDL_FingerID)touchState.touches[j].finger_id + 1,
                                            window,
                                            (float)touchState.touches[j].x / rel_w,
                                            (float)touchState.touches[j].y / rel_h, 1);
                    }
                    break;
                }
            }

            if (!found) {
                /* Finger Up */
                SDL_SendTouch(timestamp, id,
                              (SDL_FingerID)touchStateOld.touches[i].finger_id + 1,
                              window,
                              false,
                              (float)touchStateOld.touches[i].x / rel_w,
                              (float)touchStateOld.touches[i].y / rel_h, 1);
            }
        }
    }
}

#endif /* SDL_VIDEO_DRIVER_SWITCH */

/* vi: set ts=4 sw=4 expandtab: */
