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
#include "SDL_ogckeyboard.h"
#include "../../events/SDL_keyboard_c.h"

#if defined(SDL_VIDEO_DRIVER_OGC) && defined(__wii__)
#include <gctypes.h>
#include <wiikeyboard/keyboard.h>

void OGC_PumpKeyboardEvents(_THIS) {
    keyboard_event ke;

    s32 res = KEYBOARD_GetEvent(&ke);
    if (res && (ke.type == KEYBOARD_RELEASED || ke.type == KEYBOARD_PRESSED)) {
        SDL_SendKeyboardKey((ke.type == KEYBOARD_PRESSED) ? SDL_PRESSED : SDL_RELEASED, (SDL_Scancode)ke.keycode);

        if (ke.type == KEYBOARD_PRESSED) {
            const Uint16 symbol = ke.symbol;
            char utf8[4] = {'\0'};

            /* ignore private symbols, used by wiikeyboard for special keys */
            if ((symbol >= 0xE000 && symbol <= 0xF8FF) || symbol == 0xFFFF)
                return;

            /* convert UCS-2 to UTF-8 */
            if (symbol < 0x80) {
                utf8[0] = symbol;
            } else if (symbol < 0x800) {
                utf8[0] = 0xC0 | (symbol >> 6);
                utf8[1] = 0x80 | (symbol & 0x3F);
            } else {
                utf8[0] = 0xE0 |  (symbol >> 12);
                utf8[1] = 0x80 | ((symbol >> 6) & 0x3F);
                utf8[2] = 0x80 |  (symbol & 0x3F);
            }

            SDL_SendKeyboardText(utf8);
        }
    }
}
#endif
