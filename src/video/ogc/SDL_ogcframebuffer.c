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
#include "../../SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_OGC

#include "../SDL_sysvideo.h"
#include "SDL_ogcframebuffer_c.h"
#include "SDL_ogcgxcommon.h"
#include "SDL_ogcpixels.h"
#include "SDL_ogcvideo.h"

#include <malloc.h>
#include <ogc/cache.h>
#include <ogc/gx.h>
#include <ogc/system.h>
#include <ogc/video.h>

static void draw_screen_rect(SDL_Window *window)
{
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2s16(0, 0);
    GX_TexCoord2u8(0, 0);
    GX_Position2s16(window->w, 0);
    GX_TexCoord2u8(1, 0);
    GX_Position2s16(window->w, window->h);
    GX_TexCoord2u8(1, 1);
    GX_Position2s16(0, window->h);
    GX_TexCoord2u8(0, 1);
    GX_End();
}

static void free_window_data(SDL_Window *window)
{
    SDL_WindowData *windowdata = window->internal;
    if (windowdata) {
        if (windowdata->pixels) {
            SDL_free(windowdata->pixels);
        }
        if (windowdata->texels) {
            free(windowdata->texels);
        }
        SDL_free(windowdata);
        window->internal = NULL;
    }
}

bool SDL_OGC_CreateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window, Uint32 *format, void **pixels, int *pitch)
{
    SDL_WindowData *windowdata;
    int bytes_per_pixel = 4;
    size_t texture_size;
    int w, h;

    free_window_data(window);
    windowdata = (SDL_WindowData *)SDL_calloc(1, sizeof(SDL_WindowData));
    if (!windowdata) {
        SDL_OutOfMemory();
        return false;
    }

    w = window->w;
    h = window->h;
    windowdata->pixels = SDL_malloc(w * h * bytes_per_pixel);
    texture_size = GX_GetTexBufferSize(w, h, GX_TF_RGBA8, GX_FALSE, 0);
    windowdata->texels = memalign(32, texture_size);
    windowdata->surface_format = SDL_PIXELFORMAT_RGBA8888;
    window->internal = windowdata;

    *pitch = w * bytes_per_pixel;
    *format = windowdata->surface_format;
    *pixels = windowdata->pixels;
    return true;
}

bool SDL_OGC_UpdateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    SDL_WindowData *windowdata = window->internal;
    u32 texture_size;
    u8 gx_format;

    gx_format = OGC_texture_format_from_SDL(windowdata->surface_format);
    for (int i = 0; i < numrects; i++) {
        OGC_pixels_to_texture(windowdata->pixels, windowdata->surface_format, &rects[i],
                              window->surface->pitch, windowdata->texels, window->w);
    }
    texture_size = GX_GetTexBufferSize(window->w, window->h, gx_format,
                                       GX_FALSE, 0);
    DCStoreRange(windowdata->texels, texture_size);
    GX_InvalidateTexAll();
    OGC_load_texture(windowdata->texels, window->w, window->h, gx_format,
                     SDL_SCALEMODE_NEAREST);
    OGC_setup_2d_drawing();
    draw_screen_rect(window);
    GX_DrawDone();

    OGC_video_flip(_this, true);

    return true;
}

void SDL_OGC_DestroyWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Destroying window");
    free_window_data(window);
}

#endif /* SDL_VIDEO_DRIVER_OGC */
