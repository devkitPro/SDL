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

#include "../../events/SDL_keyboard_c.h"
#include "SDL_switchkeyboard.h"
#include "SDL_switchvideo.h"

static bool keys[SDL_SCANCODE_COUNT] = { 0 };
static SDL_KeyboardID keyboard_id = 1;

void SWITCH_InitKeyboard(void)
{
    hidInitializeKeyboard();
    SDL_AddKeyboard(keyboard_id, NULL, false);
}

void SWITCH_PollKeyboard(Uint64 timestamp)
{
    HidKeyboardState state;
    SDL_Scancode scancode;

    if (SDL_GetKeyboardFocus() == NULL) {
        return;
    }

    if (hidGetKeyboardStates(&state, 1)) {
        for (scancode = SDL_SCANCODE_UNKNOWN; scancode < (SDL_Scancode)HidKeyboardKey_RightGui; scancode++) {
            bool pressed = hidKeyboardStateGetKey(&state, (int)scancode);
            if (pressed && !keys[scancode]) {
                keys[scancode] = true;
            } else if (!pressed && keys[scancode]) {
                keys[scancode] = false;
            }
            SDL_SendKeyboardKey(timestamp, keyboard_id, pressed, scancode, keys[scancode]);
        }
    }
}

void SWITCH_QuitKeyboard(void)
{
    SDL_RemoveKeyboard(keyboard_id, false);
}

#endif /* SDL_VIDEO_DRIVER_SWITCH */

/* vi: set ts=4 sw=4 expandtab: */
