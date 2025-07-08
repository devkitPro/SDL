/*
  Simple DirectMedia Layer
  Copyright (C) 2025 Daniel K. O. <dkosmari@gmail.com>

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

#ifndef SDL_wiiu_swkbd_h
#define SDL_wiiu_swkbd_h

#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"

#include <padscore/kpad.h>
#include <vpad/input.h>

#if SDL_VIDEO_DRIVER_WIIU

#ifdef __cplusplus
extern "C" {
#endif

void WIIU_SWKBD_Initialize(void);
void WIIU_SWKBD_Finalize(void);

void WIIU_SWKBD_Calc(void);

void WIIU_SWKBD_Draw(SDL_Window *window);

SDL_bool WIIU_SWKBD_HasScreenKeyboardSupport(_THIS);
void WIIU_SWKBD_ShowScreenKeyboard(_THIS, SDL_Window *window);
void WIIU_SWKBD_HideScreenKeyboard(_THIS, SDL_Window *window);
SDL_bool WIIU_SWKBD_IsScreenKeyboardShown(_THIS, SDL_Window *window);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SDL_VIDEO_DRIVER_WIIU */

#endif /* SDL_wiiu_swkbd_h */
