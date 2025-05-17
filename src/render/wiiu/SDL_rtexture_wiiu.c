/*
  Simple DirectMedia Layer
  Copyright (C) 2018-2019 Ash Logan <ash@heyquark.com>
  Copyright (C) 2018-2019 Roberto Van Eeden <r.r.qwertyuiop.r.r@gmail.com>
  Copyright (C) 2022 GaryOderNichts <garyodernichts@gmail.com>

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

#if SDL_VIDEO_RENDER_WIIU

#include "../SDL_sysrender.h"
#include "SDL_render_wiiu.h"

#include <gx2/context.h>
#include <gx2/texture.h>
#include <gx2/sampler.h>
#include <gx2/mem.h>
#include <gx2r/surface.h>
#include <gx2r/resource.h>
#include <coreinit/cache.h>
#include <coreinit/memory.h>
#include <dmae/mem.h>
#include <dmae/sync.h>

#include <malloc.h>
#include <stdarg.h>

int WIIU_SDL_CreateTexture(SDL_Renderer * renderer, SDL_Texture * texture)
{
    BOOL res;
    WIIU_PixFmt gx2_fmt;
    GX2RResourceFlags surface_flags;
    WIIU_TextureData *tdata = (WIIU_TextureData *) SDL_calloc(1, sizeof(*tdata));
    if (!tdata) {
        return SDL_OutOfMemory();
    }

    /* Setup sampler */
    if (texture->scaleMode == SDL_ScaleModeNearest) {
        GX2InitSampler(&tdata->sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);
    } else {
        GX2InitSampler(&tdata->sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);
    }

    gx2_fmt = WIIU_SDL_GetPixFmt(texture->format);
    if (gx2_fmt.fmt == -1) {
        return SDL_SetError("Unsupported texture format");
    }

    /* Setup GX2Texture */
    tdata->texture.surface.width = texture->w;
    tdata->texture.surface.height = texture->h;
    tdata->texture.surface.format = gx2_fmt.fmt;
    tdata->texture.surface.depth = 1;
    tdata->texture.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    tdata->texture.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
    tdata->texture.surface.mipLevels = 1;
    tdata->texture.viewNumMips = 1;
    tdata->texture.viewNumSlices = 1;
    tdata->texture.compMap = gx2_fmt.compMap;
    GX2CalcSurfaceSizeAndAlignment(&tdata->texture.surface);
    GX2InitTextureRegs(&tdata->texture);

    /* Setup GX2ColorBuffer */
    tdata->cbuf.surface = tdata->texture.surface;
    tdata->cbuf.viewNumSlices = 1;
    GX2InitColorBufferRegs(&tdata->cbuf);

    /* Texture's surface flags */
    surface_flags = GX2R_RESOURCE_BIND_TEXTURE | GX2R_RESOURCE_BIND_COLOR_BUFFER |
                    GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_CPU_READ |
                    GX2R_RESOURCE_USAGE_GPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ;

    /* Allocate normal textures from MEM2 */
    if (texture->driverdata != WIIU_TEXTURE_MEM1_MAGIC)
        surface_flags |= GX2R_RESOURCE_USAGE_FORCE_MEM2;

    /* Allocate the texture's surface */
    res = GX2RCreateSurface(
        &tdata->texture.surface,
        surface_flags
    );
    if (!res) {
        SDL_free(tdata);
        return SDL_OutOfMemory();
    }

    /* Allocate a colour buffer, using the same backing buffer */
    res = GX2RCreateSurfaceUserMemory(
        &tdata->cbuf.surface,
        tdata->texture.surface.image,
        tdata->texture.surface.mipmaps,
        tdata->texture.surface.resourceFlags
    );
    if (!res) {
        GX2RDestroySurfaceEx(&tdata->texture.surface, 0);
        SDL_free(tdata);
        return SDL_OutOfMemory();
    }

    /* Setup texture driver data */
    texture->driverdata = tdata;

    return 0;
}

/* Somewhat adapted from SDL_render.c: SDL_LockTextureNative
   The app basically wants a pointer to a particular rectangle as well as
   write access to it. Easy GX2R! */
int WIIU_SDL_LockTexture(SDL_Renderer * renderer, SDL_Texture * texture,
                         const SDL_Rect * rect, void **pixels, int *pitch)
{
    WIIU_VideoData *videodata = (WIIU_VideoData *) SDL_GetVideoDevice()->driverdata;
    WIIU_RenderData *data = (WIIU_RenderData *) renderer->driverdata;
    WIIU_TextureData *tdata = (WIIU_TextureData *) texture->driverdata;
    Uint32 BytesPerPixel = SDL_BYTESPERPIXEL(texture->format);
    void* pixel_buffer;

    if (videodata->hasForeground && WIIU_TextureInUse(data, tdata)) {
        /* Wait for the texture rendering to finish */
        WIIU_TextureWaitDone(data, tdata);
    }

    pixel_buffer = GX2RLockSurfaceEx(&tdata->texture.surface, 0, 0);

    /* Calculate pointer to first pixel in rect */
    *pixels = (void *) ((Uint8 *) pixel_buffer +
                        rect->y * (tdata->texture.surface.pitch * BytesPerPixel) +
                        rect->x * BytesPerPixel);
    *pitch = (tdata->texture.surface.pitch * BytesPerPixel);

    /* Not sure we even need to bother keeping track of this */
    texture->locked_rect = *rect;

    return 0;
}

void WIIU_SDL_UnlockTexture(SDL_Renderer * renderer, SDL_Texture * texture)
{
    WIIU_TextureData *tdata = (WIIU_TextureData *) texture->driverdata;
    GX2RUnlockSurfaceEx(&tdata->texture.surface, 0, 0);
}

void WIIU_SDL_SetTextureScaleMode(SDL_Renderer * renderer, SDL_Texture * texture, SDL_ScaleMode scaleMode)
{
    WIIU_TextureData *tdata = (WIIU_TextureData *) texture->driverdata;

    if (texture->scaleMode == SDL_ScaleModeNearest) {
        GX2InitSampler(&tdata->sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);
    } else {
        GX2InitSampler(&tdata->sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);
    }
}

int WIIU_SDL_UpdateTexture(SDL_Renderer * renderer, SDL_Texture * texture,
                           const SDL_Rect * rect, const void *pixels, int pitch)
{
    WIIU_VideoData *videodata = (WIIU_VideoData *) SDL_GetVideoDevice()->driverdata;
    Uint32 BytesPerPixel = SDL_BYTESPERPIXEL(texture->format);
    size_t length = rect->w * BytesPerPixel;
    size_t total_size = length * rect->h;
    Uint8 *src = (Uint8 *) pixels, *dst;
    int row, dst_pitch;
    bool src_aligned, dst_aligned;

    if (!videodata->hasForeground) {
        return 0;
    }

    /* We write the rules, and we say all textures are streaming */
    WIIU_SDL_LockTexture(renderer, texture, rect, (void**)&dst, &dst_pitch);

    if (length == pitch && length == dst_pitch) {
        /* DMA requires 8 byte alignment */
        src_aligned = ((uintptr_t)src & 7U) == 0;
        dst_aligned = ((uintptr_t)dst & 7U) == 0;
        /* DMA works best on textures bigger than 5KiB */
        if ((total_size > 5120) && (src_aligned && dst_aligned)) {
            DCFlushRange(src, total_size);
            /* Run a single DMA transfer and wait until transfer is done */
            while (!DMAEWaitDone(DMAECopyMem(dst, src, total_size >> 2, DMAE_SWAP_NONE)));
        } else {
            /* Otherwise, fallback to a single memory copy */
            OSBlockMove(dst, src, total_size, true);
        }
    } else {
        /* Flush source first */
        DCFlushRange(src, total_size);
        for (row = 0; row < rect->h; ++row) {
            /* Do not flush per line here */
            OSBlockMove(dst, src, length, false);
            src += pitch;
            dst += dst_pitch;
        }
        /* Now that we're done, we can flush the entire destination at once */
        DCFlushRange(dst, dst_pitch * rect->h);
    }

    WIIU_SDL_UnlockTexture(renderer, texture);

    return 0;
}

void WIIU_SDL_DestroyTexture(SDL_Renderer * renderer, SDL_Texture * texture)
{
    WIIU_VideoData *videodata = (WIIU_VideoData *) SDL_GetVideoDevice()->driverdata;
    WIIU_RenderData *data;
    WIIU_TextureData *tdata;

    if (texture == NULL || texture->driverdata == NULL) {
        return;
    }

    data = (WIIU_RenderData *) renderer->driverdata;
    tdata = (WIIU_TextureData *) texture->driverdata;

    if (videodata->hasForeground) {
        /* Wait for the texture rendering to finish */
        if (WIIU_TextureInUse(data, tdata)) {
            WIIU_TextureWaitDone(data, tdata);
        }

        /* When destroying a render target wait for the GPU to catch up completely */
        if (texture->access == SDL_TEXTUREACCESS_TARGET) {
            GX2DrawDone();
        }
    }

    if (data->drawState.texture == texture) {
        data->drawState.texture = NULL;
    }
    if (data->drawState.target == texture) {
        data->drawState.target = NULL;
    }

    GX2RDestroySurfaceEx(&tdata->cbuf.surface, 0);
    GX2RDestroySurfaceEx(&tdata->texture.surface, 0);

    SDL_free(tdata);
}

#endif //SDL_VIDEO_RENDER_WIIU
