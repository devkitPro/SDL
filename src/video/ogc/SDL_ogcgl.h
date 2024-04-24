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

#ifndef SDL_OGC_gl_h_
#define SDL_OGC_gl_h_

#ifdef SDL_VIDEO_OPENGL

#include "../SDL_sysvideo.h"

int SDL_OGC_GL_LoadLibrary(_THIS, const char *path);
void *SDL_OGC_GL_GetProcAddress(_THIS, const char *proc);
void SDL_OGC_GL_UnloadLibrary(_THIS);
SDL_GLContext SDL_OGC_GL_CreateContext(_THIS, SDL_Window * window);
int SDL_OGC_GL_MakeCurrent(_THIS, SDL_Window * window, SDL_GLContext context);
int SDL_OGC_GL_SetSwapInterval(_THIS, int interval);
int SDL_OGC_GL_GetSwapInterval(_THIS);
int SDL_OGC_GL_SwapWindow(_THIS, SDL_Window * window);
void SDL_OGC_GL_DeleteContext(_THIS, SDL_GLContext context);
void SDL_OGC_GL_DefaultProfileConfig(_THIS, int *mask, int *major, int *minor);

#endif /* SDL_VIDEO_OPENGL */

#endif /* SDL_OGC_gl_h_ */

/* vi: set ts=4 sw=4 expandtab: */
