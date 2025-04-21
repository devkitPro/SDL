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

#ifndef SDL_ogcosk_c_h_
#define SDL_ogcosk_c_h_

#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"

#include "SDL_ogcsupport.h"

extern const SDL_OGC_VkPlugin *OGC_VkPlugin;

void OGC_StartTextInput(_THIS);
void OGC_StopTextInput(_THIS);
void OGC_SetTextInputRect(_THIS, const SDL_Rect *rect);
void OGC_ClearComposition(_THIS);
SDL_bool OGC_IsTextInputShown(_THIS);

SDL_bool OGC_HasScreenKeyboardSupport(_THIS);
void OGC_ShowScreenKeyboard(_THIS, SDL_Window *window);
void OGC_HideScreenKeyboard(_THIS, SDL_Window *window);
SDL_bool OGC_IsScreenKeyboardShown(_THIS, SDL_Window *window);

SDL_bool OGC_keyboard_render(_THIS);
int OGC_keyboard_get_pan_y(_THIS);

#endif /* SDL_ogcosk_c_h_ */

/* vi: set ts=4 sw=4 expandtab: */
