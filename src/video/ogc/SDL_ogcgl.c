/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

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

#if defined(SDL_VIDEO_DRIVER_OGC) && defined(SDL_VIDEO_OPENGL)

#include "../SDL_sysvideo.h"

#include "SDL_ogcgl.h"
#include "SDL_ogcvideo.h"

#include <opengx.h>

typedef struct SDL_GLContextState
{
    SDL_Window *window;
    int swap_interval;
} OGC_GL_Context;

/* Weak symbols for the opengx functions used by SDL, so that the client does
 * not need to link to opengx, unless it actually uses OpenGL. */
void __attribute__((weak)) ogx_initialize(void)
{
    SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
                "%s() called but opengx not used in build!", __func__);
}

void __attribute__((weak)) ogx_stencil_create(OgxStencilFlags)
{
    SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
                "%s() called but opengx not used in build!", __func__);
}

void __attribute__((weak)) *ogx_get_proc_address(const char *)
{
    SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
                "%s() called but opengx not used in build!", __func__);
    return NULL;
}

int __attribute__((weak)) ogx_prepare_swap_buffers()
{
    SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
                "%s() called but opengx not used in build!", __func__);
    return 0;
}

bool SDL_OGC_GL_LoadLibrary(SDL_VideoDevice *_this, const char *path)
{
    return true;
}

SDL_FunctionPointer SDL_OGC_GL_GetProcAddress(SDL_VideoDevice *_this, const char *proc)
{
    return ogx_get_proc_address(proc);
}

void SDL_OGC_GL_UnloadLibrary(SDL_VideoDevice *_this)
{
    // nothing to do
}

SDL_GLContext SDL_OGC_GL_CreateContext(SDL_VideoDevice *_this, SDL_Window * window)
{
    OGC_GL_Context *context = SDL_calloc(1, sizeof(*context));
    context->window = window;
    context->swap_interval = 1;
    ogx_initialize();
    glViewport(0, 0, window->w, window->h);
    if (_this->gl_config.stencil_size > 0) {
        OgxStencilFlags flags = 0; /* Don't care if Z gets dirty on discarded fragments */
        if (_this->gl_config.stencil_size > 4) flags |= OGX_STENCIL_8BIT;
        ogx_stencil_create(flags);
    }
    return context;
}

bool SDL_OGC_GL_MakeCurrent(SDL_VideoDevice *_this, SDL_Window * window, SDL_GLContext context)
{
    return true;
}

bool SDL_OGC_GL_SetSwapInterval(SDL_VideoDevice *_this, int interval)
{
    OGC_GL_Context *context = _this->current_glctx;
    context->swap_interval = interval;
    return true;
}

bool SDL_OGC_GL_GetSwapInterval(SDL_VideoDevice *_this, int *interval)
{
    OGC_GL_Context *context = _this->current_glctx;
    *interval = context->swap_interval;
    return true;
}

bool SDL_OGC_GL_SwapWindow(SDL_VideoDevice *_this, SDL_Window * window)
{
    OGC_GL_Context *context = _this->current_glctx;

    bool vsync = context->swap_interval == 1;
    OGC_video_flip(_this, vsync);
    return true;
}

bool SDL_OGC_GL_DestroyContext(SDL_VideoDevice *_this, SDL_GLContext context)
{
    SDL_free(context);
    return true;
}

void SDL_OGC_GL_DefaultProfileConfig(SDL_VideoDevice *_this, int *mask, int *major, int *minor)
{
    *mask = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
    *major = 1;
    *minor = 1;
}

#endif /* SDL_VIDEO_DRIVER_OGC */
