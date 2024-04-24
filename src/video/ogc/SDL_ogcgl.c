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
#include "../../SDL_internal.h"

#if defined(SDL_VIDEO_DRIVER_OGC) && defined(SDL_VIDEO_OPENGL)

#include "../SDL_sysvideo.h"

#include "SDL_ogcgl.h"
#include "SDL_ogcvideo.h"

#include <opengx.h>

typedef struct
{
    SDL_Window *window;
    int swap_interval;
} OGC_GL_Context;

int SDL_OGC_GL_LoadLibrary(_THIS, const char *path)
{
    return 0;
}

void *SDL_OGC_GL_GetProcAddress(_THIS, const char *proc)
{
    return ogx_get_proc_address(proc);
}

void SDL_OGC_GL_UnloadLibrary(_THIS)
{
    // nothing to do
}

SDL_GLContext SDL_OGC_GL_CreateContext(_THIS, SDL_Window * window)
{
    OGC_GL_Context *context = SDL_calloc(1, sizeof(*context));
    context->window = window;
    context->swap_interval = 1;
    ogx_initialize();
    return context;
}

int SDL_OGC_GL_MakeCurrent(_THIS, SDL_Window * window, SDL_GLContext context)
{
    return 0;
}

int SDL_OGC_GL_SetSwapInterval(_THIS, int interval)
{
    OGC_GL_Context *context = _this->current_glctx;
    context->swap_interval = interval;
    return 0;
}

int SDL_OGC_GL_GetSwapInterval(_THIS)
{
    OGC_GL_Context *context = _this->current_glctx;
    return context->swap_interval;
}

int SDL_OGC_GL_SwapWindow(_THIS, SDL_Window * window)
{
    OGC_GL_Context *context = _this->current_glctx;

    bool vsync = context->swap_interval == 1;
    OGC_video_flip(_this, vsync);
    return 0;
}

void SDL_OGC_GL_DeleteContext(_THIS, SDL_GLContext context)
{
    SDL_free(context);
}

void SDL_OGC_GL_DefaultProfileConfig(_THIS, int *mask, int *major, int *minor)
{
    *mask = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
    *major = 1;
    *minor = 1;
}

#endif /* SDL_VIDEO_DRIVER_OGC */
