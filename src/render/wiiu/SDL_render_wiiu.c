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

#include "SDL_hints.h"
#include "SDL_render_wiiu.h"

#include <gx2/event.h>
#include <gx2/swap.h>
#include <gx2/registers.h>
#include <gx2/state.h>
#include <gx2r/surface.h>

#include <malloc.h>

SDL_Renderer *WIIU_SDL_CreateRenderer(SDL_Window * window, Uint32 flags)
{
    SDL_Renderer *renderer;
    WIIU_RenderData *data;

    renderer = (SDL_Renderer *) SDL_calloc(1, sizeof(*renderer));
    if (!renderer) {
        SDL_OutOfMemory();
        return NULL;
    }

    data = (WIIU_RenderData *) SDL_calloc(1, sizeof(*data));
    if (!data) {
        SDL_free(renderer);
        SDL_OutOfMemory();
        return NULL;
    }

    /* Setup renderer functions */
    renderer->WindowEvent = WIIU_SDL_WindowEvent;
    renderer->SupportsBlendMode = WIIU_SDL_SupportsBlendMode;
    renderer->CreateTexture = WIIU_SDL_CreateTexture;
    renderer->UpdateTexture = WIIU_SDL_UpdateTexture;
    renderer->LockTexture = WIIU_SDL_LockTexture;
    renderer->UnlockTexture = WIIU_SDL_UnlockTexture;
    renderer->SetTextureScaleMode = WIIU_SDL_SetTextureScaleMode;
    renderer->SetRenderTarget = WIIU_SDL_SetRenderTarget;
    renderer->QueueSetViewport = WIIU_SDL_QueueSetViewport;
    renderer->QueueSetDrawColor = WIIU_SDL_QueueSetDrawColor;
    renderer->QueueDrawPoints = WIIU_SDL_QueueDrawPoints;
    renderer->QueueDrawLines = WIIU_SDL_QueueDrawLines;
    renderer->QueueGeometry = WIIU_SDL_QueueGeometry;
    renderer->RunCommandQueue = WIIU_SDL_RunCommandQueue;
    renderer->RenderReadPixels = WIIU_SDL_RenderReadPixels;
    renderer->RenderPresent = WIIU_SDL_RenderPresent;
    renderer->DestroyTexture = WIIU_SDL_DestroyTexture;
    renderer->DestroyRenderer = WIIU_SDL_DestroyRenderer;
    renderer->SetVSync = WIIU_SDL_SetVSync;
    renderer->info = WIIU_RenderDriver.info;
    renderer->driverdata = data;
    renderer->window = window;

    WIIU_SDL_SetVSync(renderer, (flags & SDL_RENDERER_PRESENTVSYNC) != 0);

    /* Prepare shaders */
    WIIU_SDL_CreateShaders();

    /* List of attibutes to free after render */
    data->listfree[0] = NULL;
    data->listfree[1] = NULL;

    /* Setup line and point size */
    GX2SetLineWidth(1.0f);
    GX2SetPointSize(1.0f, 1.0f);

    /* Create a fresh context state */
    data->ctx = (GX2ContextState *) memalign(GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState));
    SDL_memset(data->ctx, 0, sizeof(GX2ContextState));
    GX2SetupContextStateEx(data->ctx, TRUE);
    GX2SetContextState(data->ctx);

    /* Setup some context state options */
    GX2SetAlphaTest(TRUE, GX2_COMPARE_FUNC_GREATER, 0.0f);
    GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_NEVER);
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);

    data->drawState.blendMode = SDL_BLENDMODE_INVALID;
    data->drawState.shader = SHADER_INVALID;
    data->drawState.projectionMatrix[3][0] = -1.0f;
    data->drawState.projectionMatrix[3][1] = 1.0f;
    data->drawState.projectionMatrix[3][3] = 1.0f;

    /* Make a texture for the window */
    WIIU_SDL_CreateWindowTex(renderer, window);

    /* Setup colour buffer, rendering to the window */
    WIIU_SDL_SetRenderTarget(renderer, NULL);

    return renderer;
}

SDL_bool WIIU_SDL_SupportsBlendMode(SDL_Renderer * renderer, SDL_BlendMode blendMode)
{
    SDL_BlendFactor srcColorFactor = SDL_GetBlendModeSrcColorFactor(blendMode);
    SDL_BlendFactor srcAlphaFactor = SDL_GetBlendModeSrcAlphaFactor(blendMode);
    SDL_BlendOperation colorOperation = SDL_GetBlendModeColorOperation(blendMode);
    SDL_BlendFactor dstColorFactor = SDL_GetBlendModeDstColorFactor(blendMode);
    SDL_BlendFactor dstAlphaFactor = SDL_GetBlendModeDstAlphaFactor(blendMode);
    SDL_BlendOperation alphaOperation = SDL_GetBlendModeAlphaOperation(blendMode);

    if (WIIU_SDL_GetBlendMode(srcColorFactor) == -1 ||
        WIIU_SDL_GetBlendMode(srcAlphaFactor) == -1 ||
        WIIU_SDL_GetBlendCombineMode(colorOperation) == -1 ||
        WIIU_SDL_GetBlendMode(dstColorFactor) == -1 ||
        WIIU_SDL_GetBlendMode(dstAlphaFactor) == -1 ||
        WIIU_SDL_GetBlendCombineMode(alphaOperation) == -1) {
        return SDL_FALSE;
    }
    return SDL_TRUE;
}

void WIIU_SDL_CreateWindowTex(SDL_Renderer * renderer, SDL_Window * window)
{
    WIIU_RenderData *data = (WIIU_RenderData *) renderer->driverdata;
    const char *s_hint;
    SDL_ScaleMode s_mode;

    if (data->windowTex.driverdata) {
        WIIU_SDL_DestroyTexture(renderer, &data->windowTex);
        data->windowTex = (SDL_Texture) {0};
    }

    /* Setup scaling mode; this is normally handled by
       SDL_CreateTexture/SDL_GetScaleMode, but those can't
       be called before fully initializinig the renderer */
    s_hint = SDL_GetHint(SDL_HINT_RENDER_SCALE_QUALITY);
    if (!s_hint || SDL_strcasecmp(s_hint, "nearest") == 0) {
        s_mode = SDL_ScaleModeNearest;
    } else if (SDL_strcasecmp(s_hint, "linear") == 0) {
        s_mode = SDL_ScaleModeLinear;
    } else if (SDL_strcasecmp(s_hint, "best") == 0) {
        s_mode = SDL_ScaleModeBest;
    } else {
        s_mode = (SDL_ScaleMode)SDL_atoi(s_hint);
    }

    /* Allocate a buffer for the window */
    data->windowTex = (SDL_Texture) {
        .format = SDL_PIXELFORMAT_RGBA8888,
        .driverdata = WIIU_TEXTURE_MEM1_MAGIC,
        .scaleMode = s_mode,
    };

    SDL_GetWindowSize(window, &data->windowTex.w, &data->windowTex.h);

    /* Setup texture and color buffer for the window */
    WIIU_SDL_CreateTexture(renderer, &data->windowTex);
}

void WIIU_SDL_DestroyWindowTex(SDL_Renderer * renderer, SDL_Window * window)
{
    WIIU_RenderData *data = (WIIU_RenderData *) renderer->driverdata;

    WIIU_SDL_DestroyTexture(renderer, &data->windowTex);
}

int WIIU_SDL_SetRenderTarget(SDL_Renderer * renderer, SDL_Texture * texture)
{
    WIIU_RenderData *data = (WIIU_RenderData *) renderer->driverdata;
    WIIU_VideoData *videodata = (WIIU_VideoData *) SDL_GetVideoDevice()->driverdata;

    /* Set window or texture as target */
    WIIU_TextureData *tdata = (WIIU_TextureData *)((texture) ? texture->driverdata
                                                             : data->windowTex.driverdata);

    if (!videodata->hasForeground) {
        return 0;
    }

    /* Already in use */
    if (data->curTarget == tdata) {
        return 0;
    }

    /* make sure we're using the correct renderer ctx */
    GX2SetContextState(data->ctx);

    /* Wait for the texture rendering to finish if it is still in use by the GPU */
    /*if (WIIU_TextureInUse(data, tdata)) {
        WIIU_TextureWaitDone(data, tdata);
    }*/

    /* Update context state */
    GX2SetColorBuffer(&tdata->cbuf, GX2_RENDER_TARGET_0);

    data->curTarget = tdata;

    return 0;
}

void WIIU_SDL_DestroyRenderer(SDL_Renderer * renderer)
{
    WIIU_RenderData *data = (WIIU_RenderData *) renderer->driverdata;
    WIIU_VideoData *videodata = (WIIU_VideoData *) SDL_GetVideoDevice()->driverdata;

    if (videodata->hasForeground) {
        GX2DrawDone();
    }

    /* Free both render data queues */
    WIIU_FreeRenderData(data, 0);
    WIIU_FreeRenderData(data, 1);

    free(data->ctx);

    WIIU_SDL_DestroyShaders();

    SDL_free(data);
    SDL_free(renderer);
}

int WIIU_SDL_RenderReadPixels(SDL_Renderer * renderer, const SDL_Rect * rect,
                              Uint32 format, void * pixels, int pitch)
{
    SDL_Texture* target = WIIU_GetRenderTarget(renderer);
    WIIU_TextureData* tdata = (WIIU_TextureData*) target->driverdata;
    Uint8 *src_image;
    int ret;

    /* NOTE: The rect is already adjusted according to the viewport by
       SDL_RenderReadPixels */

    if (rect->x < 0 || rect->x+rect->w > tdata->cbuf.surface.width ||
        rect->y < 0 || rect->y+rect->h > tdata->cbuf.surface.height) {
        return SDL_SetError("Tried to read outside of surface bounds");
    }

    src_image = GX2RLockSurfaceEx(&tdata->cbuf.surface, 0, GX2R_RESOURCE_LOCKED_READ_ONLY);

    /* Convert and copy the pixels to target buffer */
    ret = SDL_ConvertPixels(rect->w, rect->h, target->format,
                            src_image + rect->y * tdata->cbuf.surface.pitch + rect->x * 4,
                            tdata->cbuf.surface.pitch,
                            format, pixels, pitch);

    GX2RUnlockSurfaceEx(&tdata->cbuf.surface, 0, GX2R_RESOURCE_LOCKED_READ_ONLY);

    return ret;
}

GX2RBuffer * WIIU_AllocRenderData(WIIU_RenderData *r, GX2RBuffer buffer)
{
    WIIU_RenderAllocData *rdata = SDL_malloc(sizeof(WIIU_RenderAllocData));

    rdata->buffer = buffer;
    if (!GX2RCreateBuffer(&rdata->buffer)) {
        SDL_free(rdata);
        return 0;
    }

    rdata->next = r->listfree[r->actlist];
    r->listfree[r->actlist] = rdata;
    return &rdata->buffer;
}

void WIIU_FreeRenderData(WIIU_RenderData *r, int list)
{
    while (r->listfree[list]) {
        WIIU_RenderAllocData *ptr = r->listfree[list];
        r->listfree[list] = ptr->next;
        GX2RDestroyBufferEx(&ptr->buffer, 0);
        SDL_free(ptr);
    }
}

void WIIU_TextureMarkUsed(WIIU_RenderData *r, WIIU_TextureData *t)
{
    /* Since GX2GetLastSubmittedTimeStamp might not flush and won't return the timestamp of the
       batch at which the texture has been used, we do +1 to act as "whichever timestamp will be done
       next". Only adding +1 is okay, since between the point markUsed is called and the actual draw
       submit call happenning, no other draw should be submitted. */
    t->timestamp = GX2GetLastSubmittedTimeStamp() + 1;
}

SDL_bool WIIU_TextureInUse(WIIU_RenderData *r, WIIU_TextureData *t)
{
    /* If the last retired time stamp of the batch is less than the buffer timestamp,
       the GPU might have not processed this buffer yet */
    return GX2GetRetiredTimeStamp() < t->timestamp;
}

SDL_bool WIIU_TextureWaitDone(WIIU_RenderData *r, WIIU_TextureData *t)
{
    /* If the buffer has never been used, don't do anything */
    if (t->timestamp == 0) {
        return true;
    }

    /* Flushing here is important otherwise we might wait on a buffer that
       hasn't even been submitted to the GPU yet */
    GX2Flush();

    /* Wait on the buffers timestamp */
    return GX2WaitTimeStamp(t->timestamp);
}

void WIIU_FrameDone(WIIU_RenderData *r)
{
    /* This function will be called once all GPU calls of a frame have been submitted..
       We'll save the last submitted frame timestamp and wait to make sure the previous
       frame is fully drawn. Then we can free its buffers. */
    r->lastFrameTimestamp = r->currFrameTimestamp;
    r->currFrameTimestamp = GX2GetLastSubmittedTimeStamp();

    /* Ensure the previous frame has been rendered completely */
    if (GX2GetRetiredTimeStamp() < r->lastFrameTimestamp) {
        GX2WaitTimeStamp(r->lastFrameTimestamp);
    }

    /* Free inactive render data, now that we're sure the GPU is no longer using it */
    WIIU_FreeRenderData(r, !r->actlist);
    r->actlist = !r->actlist;
}

SDL_RenderDriver WIIU_RenderDriver =
{
    .CreateRenderer = WIIU_SDL_CreateRenderer,
    .info = {
        .name = "WiiU GX2",
        .flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE,
        .num_texture_formats = 15,
        .texture_formats = {
            SDL_PIXELFORMAT_RGBA8888,
            SDL_PIXELFORMAT_RGBX8888,

            SDL_PIXELFORMAT_ARGB4444,
            SDL_PIXELFORMAT_RGBA4444,
            SDL_PIXELFORMAT_ABGR4444,
            SDL_PIXELFORMAT_BGRA4444,

            SDL_PIXELFORMAT_ARGB1555,
            SDL_PIXELFORMAT_ABGR1555,
            SDL_PIXELFORMAT_RGBA5551,
            SDL_PIXELFORMAT_BGRA5551,

            SDL_PIXELFORMAT_ARGB8888,
            SDL_PIXELFORMAT_BGRA8888,
            SDL_PIXELFORMAT_BGRX8888,
            SDL_PIXELFORMAT_ABGR8888,

            SDL_PIXELFORMAT_ARGB2101010,
        },
        .max_texture_width = 8192,
        .max_texture_height = 8192,
    },
};

#endif /* SDL_VIDEO_RENDER_WIIU */

/* vi: set ts=4 sw=4 expandtab: */
