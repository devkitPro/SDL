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

#ifndef SDL_render_wiiu_h
#define SDL_render_wiiu_h

#if SDL_VIDEO_RENDER_WIIU

#include "../SDL_sysrender.h"
#include "SDL_pixels.h"
#include "SDL_shaders_wiiu.h"

#include "../../video/SDL_sysvideo.h"
#include "../../video/wiiu/SDL_wiiuvideo.h"

#include <gx2r/buffer.h>
#include <gx2/context.h>
#include <gx2/sampler.h>
#include <gx2/texture.h>
#include <gx2/surface.h>
#include <gx2/event.h>
#include <gx2/utils.h>

/* Driver internal data structures */
typedef struct WIIU_PixFmt WIIU_PixFmt;
typedef struct WIIU_RenderAllocData WIIU_RenderAllocData;
typedef struct WIIU_DrawState WIIU_DrawState;
typedef struct WIIU_RenderData WIIU_RenderData;
typedef struct WIIU_TextureData WIIU_TextureData;

struct WIIU_PixFmt
{
    GX2SurfaceFormat fmt;
    uint32_t compMap;
};

struct WIIU_RenderAllocData
{
    WIIU_RenderAllocData *next;
    GX2RBuffer buffer;
};

struct WIIU_DrawState
{
    SDL_Texture *target;
    SDL_Texture *texture;

    SDL_Rect viewport;
    SDL_bool viewportDirty;
    int drawableWidth, drawableHeight;
    float projectionMatrix[4][4];

    SDL_bool cliprectEnabled;
    SDL_bool cliprectDirty;
    SDL_Rect cliprect;

    SDL_BlendMode blendMode;

    WIIU_ShaderType shader;
};

struct WIIU_RenderData
{
    GX2ContextState *ctx;
    int actlist;
    WIIU_RenderAllocData *listfree[2];
    OSTime currFrameTimestamp;
    OSTime lastFrameTimestamp;
    SDL_Texture windowTex;
    WIIU_DrawState drawState;
    WIIU_TextureData *curTarget;
};

struct WIIU_TextureData
{
    GX2Sampler sampler;
    GX2Texture texture;
    GX2ColorBuffer cbuf;
    OSTime timestamp;
};

/* Ask texture driver to allocate texture's memory from MEM1 */
#define WIIU_TEXTURE_MEM1_MAGIC (void *)0xCAFE0001

/* SDL_render API implementation */
SDL_Renderer *WIIU_SDL_CreateRenderer(SDL_Window * window, Uint32 flags);
void WIIU_SDL_WindowEvent(SDL_Renderer * renderer,
                             const SDL_WindowEvent *event);
SDL_bool WIIU_SDL_SupportsBlendMode(SDL_Renderer * renderer, SDL_BlendMode blendMode);
int WIIU_SDL_CreateTexture(SDL_Renderer * renderer, SDL_Texture * texture);
int WIIU_SDL_UpdateTexture(SDL_Renderer * renderer, SDL_Texture * texture,
                       const SDL_Rect * rect, const void *pixels,
                       int pitch);
int WIIU_SDL_LockTexture(SDL_Renderer * renderer, SDL_Texture * texture,
                     const SDL_Rect * rect, void **pixels, int *pitch);
void WIIU_SDL_UnlockTexture(SDL_Renderer * renderer, SDL_Texture * texture);
void WIIU_SDL_SetTextureScaleMode(SDL_Renderer * renderer, SDL_Texture * texture, SDL_ScaleMode scaleMode);
int WIIU_SDL_SetRenderTarget(SDL_Renderer * renderer, SDL_Texture * texture);
int WIIU_SDL_QueueSetViewport(SDL_Renderer * renderer, SDL_RenderCommand * cmd);
int WIIU_SDL_QueueSetDrawColor(SDL_Renderer * renderer, SDL_RenderCommand * cmd);
int WIIU_SDL_QueueDrawPoints(SDL_Renderer * renderer, SDL_RenderCommand * cmd, const SDL_FPoint * points, int count);
int WIIU_SDL_QueueDrawLines(SDL_Renderer * renderer, SDL_RenderCommand * cmd, const SDL_FPoint * points, int count);
int WIIU_SDL_QueueGeometry(SDL_Renderer * renderer, SDL_RenderCommand * cmd, SDL_Texture * texture,
        const float * xy, int xy_stride, const SDL_Color * color, int color_stride, const float * uv, int uv_stride,
        int num_vertices, const void * indices, int num_indices, int size_indices,
        float scale_x, float scale_y);
int WIIU_SDL_RunCommandQueue(SDL_Renderer * renderer, SDL_RenderCommand *cmd, void *vertices, size_t vertsize);
int WIIU_SDL_RenderReadPixels(SDL_Renderer * renderer, const SDL_Rect * rect,
                          Uint32 format, void * pixels, int pitch);
int WIIU_SDL_SetVSync(SDL_Renderer * renderer, const int vsync);
int WIIU_SDL_RenderPresent(SDL_Renderer * renderer);
void WIIU_SDL_DestroyTexture(SDL_Renderer * renderer, SDL_Texture * texture);
void WIIU_SDL_DestroyRenderer(SDL_Renderer * renderer);

/* Driver internal functions */
void WIIU_SDL_CreateWindowTex(SDL_Renderer * renderer, SDL_Window * window);
void WIIU_SDL_DestroyWindowTex(SDL_Renderer * renderer, SDL_Window * window);

/* Utility/helper functions */
GX2RBuffer * WIIU_AllocRenderData(WIIU_RenderData *r, GX2RBuffer buffer);
void WIIU_FreeRenderData(WIIU_RenderData *r, int list);

void WIIU_TextureMarkUsed(WIIU_RenderData *r, WIIU_TextureData *t);
SDL_bool WIIU_TextureInUse(WIIU_RenderData *r, WIIU_TextureData *t);
SDL_bool WIIU_TextureWaitDone(WIIU_RenderData *r, WIIU_TextureData *t);

void WIIU_FrameDone(WIIU_RenderData *r);

static inline SDL_Texture * WIIU_GetRenderTarget(SDL_Renderer* renderer)
{
    WIIU_RenderData *data = (WIIU_RenderData *) renderer->driverdata;

    if (renderer->target) {
        return renderer->target;
    }

    return &data->windowTex;
}

static inline WIIU_PixFmt WIIU_SDL_GetPixFmt(Uint32 format)
{
    WIIU_PixFmt outFmt = { .fmt = -1, .compMap = 0 };

    switch (format) {
        /* packed16 formats: 4 bits/channel */
        case SDL_PIXELFORMAT_ARGB4444: {
            outFmt.fmt = GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4;
            outFmt.compMap = GX2_COMP_MAP(GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A, GX2_SQ_SEL_R);
            break;
        }
        case SDL_PIXELFORMAT_RGBA4444: {
            outFmt.fmt = GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4;
            outFmt.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
            break;
        }
        case SDL_PIXELFORMAT_ABGR4444: {
            outFmt.fmt = GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4;
            outFmt.compMap = GX2_COMP_MAP(GX2_SQ_SEL_A, GX2_SQ_SEL_B, GX2_SQ_SEL_G, GX2_SQ_SEL_R);
            break;
        }
        case SDL_PIXELFORMAT_BGRA4444: {
            outFmt.fmt = GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4;
            outFmt.compMap = GX2_COMP_MAP(GX2_SQ_SEL_B, GX2_SQ_SEL_G, GX2_SQ_SEL_R, GX2_SQ_SEL_A);
            break;
        }

        /* packed16 formats: 5 bits/channel */
        case SDL_PIXELFORMAT_ARGB1555: {
            outFmt.fmt = GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1;
            outFmt.compMap = GX2_COMP_MAP(GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A, GX2_SQ_SEL_R);
            break;
        }
        case SDL_PIXELFORMAT_ABGR1555: {
            outFmt.fmt = GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1;
            outFmt.compMap = GX2_COMP_MAP(GX2_SQ_SEL_A, GX2_SQ_SEL_B, GX2_SQ_SEL_G, GX2_SQ_SEL_R);
            break;
        }
        case SDL_PIXELFORMAT_RGBA5551: {
            outFmt.fmt = GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1;
            outFmt.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
            break;
        }
        case SDL_PIXELFORMAT_BGRA5551: {
            outFmt.fmt = GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1;
            outFmt.compMap = GX2_COMP_MAP(GX2_SQ_SEL_B, GX2_SQ_SEL_G, GX2_SQ_SEL_R, GX2_SQ_SEL_A);
            break;
        }

        /* packed16 formats: 565 */
        case SDL_PIXELFORMAT_RGB565: {
            outFmt.fmt = GX2_SURFACE_FORMAT_UNORM_R5_G6_B5;
            outFmt.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
            break;
        }
        case SDL_PIXELFORMAT_BGR565: {
            outFmt.fmt = GX2_SURFACE_FORMAT_UNORM_R5_G6_B5;
            outFmt.compMap = GX2_COMP_MAP(GX2_SQ_SEL_B, GX2_SQ_SEL_G, GX2_SQ_SEL_R, GX2_SQ_SEL_A);
            break;
        }

        /* packed32 formats */
        case SDL_PIXELFORMAT_RGBX8888: {
            outFmt.fmt = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
            outFmt.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_1);
            break;
        }
        case SDL_PIXELFORMAT_RGBA8888: {
            outFmt.fmt = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
            outFmt.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
            break;
        }
        case SDL_PIXELFORMAT_ARGB8888: {
            outFmt.fmt = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
            outFmt.compMap = GX2_COMP_MAP(GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A, GX2_SQ_SEL_R);
            break;
        }
        case SDL_PIXELFORMAT_BGRX8888: {
            outFmt.fmt = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
            outFmt.compMap = GX2_COMP_MAP(GX2_SQ_SEL_B, GX2_SQ_SEL_G, GX2_SQ_SEL_R, GX2_SQ_SEL_1);
            break;
        }
        case SDL_PIXELFORMAT_BGRA8888: {
            outFmt.fmt = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
            outFmt.compMap = GX2_COMP_MAP(GX2_SQ_SEL_B, GX2_SQ_SEL_G, GX2_SQ_SEL_R, GX2_SQ_SEL_A);
            break;
        }
        case SDL_PIXELFORMAT_ABGR8888: {
            outFmt.fmt = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
            outFmt.compMap = GX2_COMP_MAP(GX2_SQ_SEL_A, GX2_SQ_SEL_B, GX2_SQ_SEL_G, GX2_SQ_SEL_R);
            break;
        }
        case SDL_PIXELFORMAT_ARGB2101010: {
            outFmt.fmt = GX2_SURFACE_FORMAT_UNORM_R10_G10_B10_A2;
            outFmt.compMap = GX2_COMP_MAP(GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A, GX2_SQ_SEL_R);
            break;
        }
        default: {
            printf("SDL: WiiU format not recognised (SDL: %08X)\n", format);
            outFmt.fmt = -1;
            break;
        }
    }

    return outFmt;
}

static inline GX2BlendMode WIIU_SDL_GetBlendMode(SDL_BlendFactor factor)
{
    switch (factor) {
        case SDL_BLENDFACTOR_ZERO:
            return GX2_BLEND_MODE_ZERO;
        case SDL_BLENDFACTOR_ONE:
            return GX2_BLEND_MODE_ONE;
        case SDL_BLENDFACTOR_SRC_COLOR:
            return GX2_BLEND_MODE_SRC_COLOR;
        case SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR:
            return GX2_BLEND_MODE_INV_SRC_COLOR;
        case SDL_BLENDFACTOR_SRC_ALPHA:
            return GX2_BLEND_MODE_SRC_ALPHA;
        case SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA:
            return GX2_BLEND_MODE_INV_SRC_ALPHA;
        case SDL_BLENDFACTOR_DST_COLOR:
            return GX2_BLEND_MODE_DST_COLOR;
        case SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR:
            return GX2_BLEND_MODE_INV_DST_COLOR;
        case SDL_BLENDFACTOR_DST_ALPHA:
            return GX2_BLEND_MODE_DST_ALPHA;
        case SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA:
            return GX2_BLEND_MODE_INV_DST_ALPHA;
        default:
            return -1;
    }
}

static inline GX2BlendCombineMode WIIU_SDL_GetBlendCombineMode(SDL_BlendOperation operation)
{
    switch (operation) {
        case SDL_BLENDOPERATION_ADD:
            return GX2_BLEND_COMBINE_MODE_ADD;
        case SDL_BLENDOPERATION_SUBTRACT:
            return GX2_BLEND_COMBINE_MODE_SUB;
        case SDL_BLENDOPERATION_REV_SUBTRACT:
            return GX2_BLEND_COMBINE_MODE_REV_SUB;
        case SDL_BLENDOPERATION_MINIMUM:
            return GX2_BLEND_COMBINE_MODE_MIN;
        case SDL_BLENDOPERATION_MAXIMUM:
            return GX2_BLEND_COMBINE_MODE_MAX;
        default:
            return -1;
    }
}

#endif /* SDL_VIDEO_RENDER_WIIU */

#endif /* SDL_render_wiiu_h */
