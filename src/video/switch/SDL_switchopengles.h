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
#include "../../SDL_internal.h"

#ifndef SDL_switchteopengles_h_
#define SDL_switchteopengles_h_

#if SDL_VIDEO_DRIVER_SWITCH

#include "../SDL_egl_c.h"
#include "../SDL_sysvideo.h"

/* OpenGLES functions */
#define SWITCH_GLES_GetProcAddress  SDL_EGL_GetProcAddressInternal
#define SWITCH_GLES_UnloadLibrary   SDL_EGL_UnloadLibrary
#define SWITCH_GLES_SetSwapInterval SDL_EGL_SetSwapInterval
#define SWITCH_GLES_GetSwapInterval SDL_EGL_GetSwapInterval
#define SWITCH_GLES_DestroyContext  SDL_EGL_DestroyContext

extern bool SWITCH_GLES_LoadLibrary(SDL_VideoDevice *_this, const char *path);
extern SDL_GLContext SWITCH_GLES_CreateContext(SDL_VideoDevice *_this, SDL_Window *window);
extern bool SWITCH_GLES_SwapWindow(SDL_VideoDevice *_this, SDL_Window *window);
extern bool SWITCH_GLES_MakeCurrent(SDL_VideoDevice *_this, SDL_Window *window, SDL_GLContext context);
extern void SWITCH_GLES_DefaultProfileConfig(SDL_VideoDevice *_this, int *mask, int *major, int *minor);

#endif /* SDL_VIDEO_DRIVER_SWITCH */
#endif /* SDL_switchteopengles_h_ */

/* vi: set ts=4 sw=4 expandtab: */
