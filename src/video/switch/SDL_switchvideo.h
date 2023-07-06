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

#ifndef __SDL_SWITCHVIDEO_H__
#define __SDL_SWITCHVIDEO_H__

#if SDL_VIDEO_DRIVER_SWITCH

#include <switch.h>

#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"

#include "SDL_egl.h"

typedef struct SDL_WindowData
{
    EGLSurface egl_surface;
} SDL_WindowData;

int SWITCH_VideoInit(_THIS);
void SWITCH_VideoQuit(_THIS);
void SWITCH_GetDisplayModes(_THIS, SDL_VideoDisplay *display);
int SWITCH_SetDisplayMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode);
int SWITCH_CreateWindow(_THIS, SDL_Window *window);
int SWITCH_CreateWindowFrom(_THIS, SDL_Window *window, const void *data);
void SWITCH_SetWindowTitle(_THIS, SDL_Window *window);
void SWITCH_SetWindowIcon(_THIS, SDL_Window *window, SDL_Surface *icon);
void SWITCH_SetWindowPosition(_THIS, SDL_Window *window);
void SWITCH_SetWindowSize(_THIS, SDL_Window *window);
void SWITCH_ShowWindow(_THIS, SDL_Window *window);
void SWITCH_HideWindow(_THIS, SDL_Window *window);
void SWITCH_RaiseWindow(_THIS, SDL_Window *window);
void SWITCH_MaximizeWindow(_THIS, SDL_Window *window);
void SWITCH_MinimizeWindow(_THIS, SDL_Window *window);
void SWITCH_RestoreWindow(_THIS, SDL_Window *window);
void SWITCH_SetWindowGrab(_THIS, SDL_Window *window, SDL_bool grabbed);
void SWITCH_DestroyWindow(_THIS, SDL_Window *window);
void SWITCH_PumpEvents(_THIS);

#endif /* SDL_VIDEO_DRIVER_SWITCH */
#endif /* __SDL_SWITCHVIDEO_H__ */

/* vi: set ts=4 sw=4 expandtab: */
