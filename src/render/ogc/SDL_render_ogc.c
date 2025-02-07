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

#ifdef SDL_VIDEO_RENDER_OGC

#include "../SDL_sysrender.h"
#include "SDL_hints.h"

#include "../../video/ogc/SDL_ogcgxcommon.h"
#include "../../video/ogc/SDL_ogcpixels.h"
#include "../../video/ogc/SDL_ogcvideo.h"

#include <malloc.h>
#include <ogc/cache.h>
#include <ogc/gx.h>
#include <ogc/video.h>

#define MAX_EFB_WIDTH 640
#define MAX_EFB_HEIGHT 528

typedef struct
{
    SDL_BlendMode current_blend_mode;
    GXColor clear_color;
    int ops_after_present;
    bool vsync;
    u8 efb_pixel_format;
    SDL_Texture *render_target;
    SDL_Texture *saved_efb_texture;
} OGC_RenderData;

typedef struct
{
    void *texels;
    void *pixels;
    SDL_Rect pixels_rect;
    u16 pitch;
    u16 pixels_pitch;
    u8 format;
    u8 needed_stages; // Normally 1, set to 2 for palettized formats
} OGC_TextureData;

static void OGC_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture);

static void OGC_WindowEvent(SDL_Renderer *renderer, const SDL_WindowEvent *event)
{
}

static void set_blend_mode_real(SDL_Renderer *renderer, SDL_BlendMode blend_mode)
{
    OGC_RenderData *data = renderer->driverdata;

    switch (blend_mode) {
    case SDL_BLENDMODE_NONE:
        GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
        break;
    case SDL_BLENDMODE_BLEND:
        GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
        break;
    case SDL_BLENDMODE_ADD:
        GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
        break;
    case SDL_BLENDMODE_MOD:
        GX_SetBlendMode(GX_BM_BLEND, GX_BL_DSTCLR, GX_BL_ZERO, GX_LO_CLEAR);
        break;
    case SDL_BLENDMODE_MUL:
        GX_SetBlendMode(GX_BM_BLEND, GX_BL_DSTCLR, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
        break;
    default:
        return;
    }

    data->current_blend_mode = blend_mode;
}

static inline void OGC_SetBlendMode(SDL_Renderer *renderer, SDL_BlendMode blend_mode)
{
    OGC_RenderData *data = renderer->driverdata;

    if (data->ops_after_present > 0 &&
        blend_mode == data->current_blend_mode) {
        /* Nothing to do */
        return;
    }

    set_blend_mode_real(renderer, blend_mode);
}

static void load_efb_from_texture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    OGC_TextureData *ogc_tex = texture->driverdata;

    OGC_load_texture(ogc_tex->texels, texture->w, texture->h,
                     ogc_tex->format, SDL_ScaleModeNearest);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_S16, 0);

    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_U8, 0);
    GX_SetNumTexGens(1);

    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GX_SetNumTevStages(1);

    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2s16(0, 0);
    GX_TexCoord2u8(0, 0);
    GX_Position2s16(texture->w, 0);
    GX_TexCoord2u8(1, 0);
    GX_Position2s16(texture->w, texture->h);
    GX_TexCoord2u8(1, 1);
    GX_Position2s16(0, texture->h);
    GX_TexCoord2u8(0, 1);
    GX_End();
}

static void save_efb_to_texture(SDL_Texture *texture)
{
    OGC_TextureData *ogc_tex = texture->driverdata;
    u32 texture_size;

    texture_size = GX_GetTexBufferSize(texture->w, texture->h, ogc_tex->format,
                                       GX_FALSE, 0);
    DCInvalidateRange(ogc_tex->texels, texture_size);

    GX_SetTexCopySrc(0, 0, texture->w, texture->h);
    GX_SetTexCopyDst(texture->w, texture->h, ogc_tex->format, GX_FALSE);
    GX_CopyTex(ogc_tex->texels, GX_TRUE);
    GX_PixModeSync();
}

static void update_texture(SDL_Texture *texture, const SDL_Rect *rect,
                           const void *pixels, int pitch)
{
    OGC_TextureData *ogc_tex = texture->driverdata;
    u32 texture_size;

    OGC_pixels_to_texture((void*)pixels, texture->format, rect,
                          pitch, ogc_tex->texels, texture->w);
    texture_size = GX_GetTexBufferSize(texture->w, texture->h, ogc_tex->format,
                                       GX_FALSE, 0);
    /* It would be more effective if we updated only the changed range here,
     * but the complexity is probably not worth the effort. */
    DCStoreRange(ogc_tex->texels, texture_size);
    GX_InvalidateTexAll();
}

static int OGC_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    u32 texture_size;
    OGC_TextureData *ogc_tex;

    ogc_tex = SDL_calloc(1, sizeof(OGC_TextureData));
    if (!ogc_tex) {
        return SDL_OutOfMemory();
    }

    ogc_tex->format = OGC_texture_format_from_SDL(texture->format);
    ogc_tex->needed_stages = (ogc_tex->format == GX_TF_CI8) ? 2 : 1;
    texture_size = GX_GetTexBufferSize(texture->w, texture->h, ogc_tex->format,
                                       GX_FALSE, 0);
    ogc_tex->texels = memalign(32, texture_size);
    if (!ogc_tex->texels) {
        SDL_free(ogc_tex);
        return SDL_OutOfMemory();
    }

    texture->driverdata = ogc_tex;
    return 0;
}

static int OGC_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                           const SDL_Rect *rect, void **pixels, int *pitch)
{
    OGC_TextureData *ogc_tex = texture->driverdata;

    ogc_tex->pixels = SDL_malloc(rect->w * rect->h * SDL_BYTESPERPIXEL(texture->format));
    ogc_tex->pixels_pitch = rect->w * SDL_BYTESPERPIXEL(texture->format);
    ogc_tex->pixels_rect = *rect;
    *pixels = ogc_tex->pixels;
    *pitch = ogc_tex->pixels_pitch;
    return 0;
}

static void OGC_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    OGC_TextureData *ogc_tex = texture->driverdata;

    update_texture(texture, &ogc_tex->pixels_rect,
                   ogc_tex->pixels, ogc_tex->pixels_pitch);

    if (ogc_tex->pixels) {
        SDL_free(ogc_tex->pixels);
        ogc_tex->pixels = NULL;
    }
}

static int OGC_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                             const SDL_Rect *rect, const void *pixels, int pitch)
{
    update_texture(texture, rect, pixels, pitch);
    return 0;
}

static void OGC_SetTextureScaleMode(SDL_Renderer *renderer,
                                    SDL_Texture *texture,
                                    SDL_ScaleMode scaleMode)
{
    /* Nothing to do here: the scale mode is applied to the texture when
     * loading it in OGC_load_texture(). */
}

static SDL_Texture *create_efb_texture(OGC_RenderData *data, SDL_Texture *target)
{
    /* Note: we do return a SDL_Texture, but not via SDL's API, since that does
     * a bunch of other stuffs we don't care about. We create this texture for
     * our internal use, so we initialize only those fields we care about. */
    SDL_Texture *texture;
    OGC_TextureData *ogc_tex;
    u32 texture_size;

    texture = SDL_calloc(1, sizeof(*texture));
    if (!texture) goto fail_texture_alloc;

    ogc_tex = SDL_calloc(1, sizeof(OGC_TextureData));
    if (!ogc_tex) goto fail_ogc_tex_alloc;

    ogc_tex->format = data->efb_pixel_format == GX_PF_RGB565_Z16 ?
        GX_TF_RGB565 : GX_TF_RGBA8;
    texture->w = target->w;
    texture->h = target->h;
    texture_size = GX_GetTexBufferSize(texture->w, texture->h, ogc_tex->format,
                                       GX_FALSE, 0);
    ogc_tex->texels = memalign(32, texture_size);
    if (!ogc_tex->texels) goto fail_texels_alloc;

    texture->driverdata = ogc_tex;
    return texture;

fail_texels_alloc:
    SDL_free(ogc_tex->texels);
fail_ogc_tex_alloc:
    SDL_free(texture);
fail_texture_alloc:
    SDL_OutOfMemory();
    return NULL;
}

static int OGC_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    OGC_RenderData *data = renderer->driverdata;
    u8 desired_efb_pixel_format = GX_PF_RGB8_Z24;

    if (texture) {
        if (texture->w > MAX_EFB_WIDTH || texture->h > MAX_EFB_HEIGHT) {
            return SDL_SetError("Render target bigger than EFB");
        }

        if (data->ops_after_present > 0) {
            /* Save the current EFB contents if we already drew something onto
             * it. We'll restore it later, when the rendering target is reset
             * to NULL (the screen). */
            data->saved_efb_texture = create_efb_texture(data, texture);
            save_efb_to_texture(data->saved_efb_texture);
        }

        if (SDL_ISPIXELFORMAT_ALPHA(texture->format)) {
            desired_efb_pixel_format = GX_PF_RGBA6_Z24;
        }
    } else if (data->render_target) {
        save_efb_to_texture(data->render_target);
    }

    data->render_target = texture;

    if (desired_efb_pixel_format != data->efb_pixel_format) {
        data->efb_pixel_format = desired_efb_pixel_format;
        GX_SetPixelFmt(data->efb_pixel_format, GX_ZC_LINEAR);
    }

    /* Restore the EFB to how it was before the we started to render to a
     * texture. */
    if (!texture && data->saved_efb_texture) {
        load_efb_from_texture(renderer, data->saved_efb_texture);
        /* Flush the draw operation before destroying the texture */
        GX_DrawDone();
        OGC_DestroyTexture(renderer, data->saved_efb_texture);
        SDL_free(data->saved_efb_texture);
        data->saved_efb_texture = NULL;
    }

    return 0;
}

static int OGC_QueueSetViewport(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    return 0; /* nothing to do in this backend. */
}

static int OGC_QueueDrawPoints(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                               const SDL_FPoint *points, int count)
{
    size_t size = count * sizeof(SDL_FPoint);
    SDL_FPoint *vertices = SDL_AllocateRenderVertices(renderer, size,
                                                      4, &cmd->data.draw.first);
    if (!vertices) {
        return -1;
    }

    cmd->data.draw.count = count;
    SDL_memcpy(vertices, points, size);
    return 0;
}

static int OGC_QueueFillRects(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                               const SDL_FRect *rects, int count)
{
    size_t size = count * sizeof(SDL_FPoint) * 4;
    SDL_FPoint *vertices = SDL_AllocateRenderVertices(renderer, size,
                                                      4, &cmd->data.draw.first);
    if (!vertices) {
        return -1;
    }

    cmd->data.draw.count = count;
    for (int i = 0; i < count; i++) {
        vertices[i].x = rects[i].x;
        vertices[i].y = rects[i].y;
        vertices[i+1].x = rects[i].x + rects[i].w;
        vertices[i+1].y = rects[i].y;
        vertices[i+2].x = rects[i].x + rects[i].w;
        vertices[i+2].y = rects[i].y + rects[i].h;
        vertices[i+3].x = rects[i].x;
        vertices[i+3].y = rects[i].y + rects[i].h;
    }
    return 0;
}

static int OGC_QueueGeometry(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
                             const float *xy, int xy_stride, const SDL_Color *color, int color_stride,
                             const float *uv, int uv_stride,
                             int num_vertices, const void *indices, int num_indices, int size_indices,
                             float scale_x, float scale_y)
{
    int i;
    int count = indices ? num_indices : num_vertices;
    size_t size_per_element;
    char *vertices;

    cmd->data.draw.count = count;
    size_indices = indices ? size_indices : 0;

    size_per_element = sizeof(SDL_FPoint) + sizeof(SDL_Color);
    if (texture) {
        size_per_element += sizeof(SDL_FPoint);
    }

    vertices = SDL_AllocateRenderVertices(renderer, count * size_per_element,
                                          4, &cmd->data.draw.first);
    if (!vertices) {
        return -1;
    }

    for (i = 0; i < count; i++) {
        int j;
        float *xy_;
        float *uv_;
        SDL_Color col;
        char *vertex;
        SDL_FPoint *vertex_xy;
        if (size_indices == 4) {
            j = ((const Uint32 *)indices)[i];
        } else if (size_indices == 2) {
            j = ((const Uint16 *)indices)[i];
        } else if (size_indices == 1) {
            j = ((const Uint8 *)indices)[i];
        } else {
            j = i;
        }

        xy_ = (float *)((char *)xy + j * xy_stride);
        col = *(SDL_Color *)((char *)color + j * color_stride);
        uv_ = (float *)((char *)uv + j * uv_stride);

        vertex = vertices + size_per_element * i;

        vertex_xy = (SDL_FPoint *)vertex;
        vertex_xy->x = xy_[0] * scale_x;
        vertex_xy->y = xy_[1] * scale_x;

        *(SDL_Color *)(vertex + sizeof(SDL_FPoint)) = col;

        if (texture) {
            SDL_FPoint *vertex_uv = (SDL_FPoint *)(vertex + sizeof(SDL_FPoint) + sizeof(SDL_Color));
            vertex_uv->x = uv_[0];
            vertex_uv->y = uv_[1];
        }
    }

    return 0;
}

static int OGC_RenderSetViewPort(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    const SDL_Rect *viewport = &cmd->data.viewport.rect;

    OGC_set_viewport(viewport->x, viewport->y, viewport->w, viewport->h);
    return 0;
}

static int OGC_RenderSetClipRect(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    const SDL_Rect *rect = &cmd->data.cliprect.rect;

    if (cmd->data.cliprect.enabled) {
        GX_SetScissor(renderer->viewport.x + rect->x,
                      renderer->viewport.y + rect->y,
                      rect->w, rect->h);
        GX_SetClipMode(GX_CLIP_ENABLE);
    } else {
        GX_SetClipMode(GX_CLIP_DISABLE);
    }

    return 0;
}

static int OGC_RenderClear(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    OGC_RenderData *data = renderer->driverdata;

    GXColor c = {
        cmd->data.color.r,
        cmd->data.color.g,
        cmd->data.color.b,
        cmd->data.color.a
    };

    /* If nothing has been drawn after Present, and if the clear color has not
     * changed, there's no need to do anything here. */
    if (data->ops_after_present == 0 &&
        GX_COLOR_AS_U32(c) == GX_COLOR_AS_U32(data->clear_color)) {
        return 0;
    }

    data->clear_color = c;
    GX_SetCopyClear(c, GX_MAX_Z24);
    if (renderer->target) {
        save_efb_to_texture(renderer->target);
    } else {
        GX_CopyDisp(OGC_video_get_xfb(SDL_GetVideoDevice()), GX_TRUE);
    }

    return 0;
}

static int OGC_RenderGeometry(SDL_Renderer *renderer, void *vertices,
                              SDL_RenderCommand *cmd)
{
    OGC_RenderData *data = renderer->driverdata;
    const size_t count = cmd->data.draw.count;
    SDL_Texture *texture = cmd->data.draw.texture;
    size_t size_per_element;

    data->ops_after_present++;
    OGC_SetBlendMode(renderer, cmd->data.draw.blend);

    size_per_element = sizeof(SDL_FPoint) + sizeof(SDL_Color);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    if (texture) {
        OGC_TextureData *ogc_tex = texture->driverdata;
        u8 stage;

        size_per_element += sizeof(SDL_FPoint);
        OGC_load_texture(ogc_tex->texels, texture->w, texture->h,
                         ogc_tex->format, texture->scaleMode);
        stage = GX_TEVSTAGE0 + ogc_tex->needed_stages - 1;

        GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
        GX_SetNumTexGens(1);

        GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
        GX_SetTevOrder(stage, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        GX_SetTevOp(stage, GX_MODULATE);
        GX_SetNumTevStages(stage - GX_TEVSTAGE0 + 1);
    } else {
        GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    }

    GX_Begin(GX_TRIANGLES, GX_VTXFMT0, count);
    for (int i = 0; i < count; i++) {
        void *vertex = vertices + cmd->data.draw.first + size_per_element * i;
        SDL_FPoint *vertex_xy = vertex;
        SDL_Color *c = (SDL_Color*)(vertex + sizeof(SDL_FPoint));

        GX_Position2f32(vertex_xy->x, vertex_xy->y);
        GX_Color4u8(c->r, c->g, c->b, c->a);
        if (texture) {
            SDL_FPoint *vertex_uv = (SDL_FPoint *)(vertex + sizeof(SDL_FPoint) + sizeof(SDL_Color));
            GX_TexCoord2f32(vertex_uv->x, vertex_uv->y);
        }
    }
    GX_End();
    return 0;
}

int OGC_RenderPrimitive(SDL_Renderer *renderer, u8 primitive,
                        void *vertices, SDL_RenderCommand *cmd)
{
    OGC_RenderData *data = renderer->driverdata;
    size_t count = cmd->data.draw.count;
    const SDL_FPoint *verts = (SDL_FPoint *)(vertices + cmd->data.draw.first);
    GXColor c = {
        cmd->data.draw.r,
        cmd->data.draw.g,
        cmd->data.draw.b,
        cmd->data.draw.a
    };

    data->ops_after_present++;
    OGC_SetBlendMode(renderer, cmd->data.draw.blend);

    /* TODO: optimize state changes. */
    GX_SetTevColor(GX_TEVREG0, c);
    GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_C0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
    GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_A0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);

    if (primitive == GX_QUADS) count *= 4;

    GX_Begin(primitive, GX_VTXFMT0, count);
    for (int i = 0; i < count; i++) {
        GX_Position2f32(verts[i].x, verts[i].y);
    }
    GX_End();

    /* The last point is not drawn */
    if (primitive == GX_LINESTRIP) {
        GX_Begin(GX_POINTS, GX_VTXFMT0, 1);
        GX_Position2f32(verts[count - 1].x, verts[count - 1].y);
        GX_End();
    }

    return 0;
}

static int OGC_RunCommandQueue(SDL_Renderer *renderer, SDL_RenderCommand *cmd, void *vertices, size_t vertsize)
{
    while (cmd) {
        switch (cmd->command) {
        case SDL_RENDERCMD_SETVIEWPORT:
            OGC_RenderSetViewPort(renderer, cmd);
            break;
        case SDL_RENDERCMD_SETCLIPRECT:
            OGC_RenderSetClipRect(renderer, cmd);
            break;
        case SDL_RENDERCMD_SETDRAWCOLOR:
            /* This is a no-op, since every command carries the color, and
             * setting it on the FIFO is not expensive. */
            break;
        case SDL_RENDERCMD_CLEAR:
            OGC_RenderClear(renderer, cmd);
            break;
        case SDL_RENDERCMD_DRAW_POINTS:
            OGC_RenderPrimitive(renderer, GX_POINTS, vertices, cmd);
            break;
        case SDL_RENDERCMD_DRAW_LINES:
            OGC_RenderPrimitive(renderer, GX_LINESTRIP, vertices, cmd);
            break;
        case SDL_RENDERCMD_FILL_RECTS:
            OGC_RenderPrimitive(renderer, GX_QUADS, vertices, cmd);
            break;
        case SDL_RENDERCMD_COPY: /* unused */
            break;
        case SDL_RENDERCMD_COPY_EX: /* unused */
            break;
        case SDL_RENDERCMD_GEOMETRY:
            OGC_RenderGeometry(renderer, vertices, cmd);
            break;
        case SDL_RENDERCMD_NO_OP:
            break;
        }
        cmd = cmd->next;
    }

    GX_DrawDone();
    return 0;
}

static int OGC_RenderReadPixels(SDL_Renderer *renderer, const SDL_Rect *rect,
                                Uint32 format, void *pixels, int pitch)
{
    return SDL_Unsupported();
}

static int OGC_RenderPresent(SDL_Renderer *renderer)
{
    OGC_RenderData *data = renderer->driverdata;

    GX_DrawDone();

    OGC_video_flip(SDL_GetVideoDevice(), data->vsync);

    /* Mouse cursor and OSK can change the blending mode; restore it. */
    set_blend_mode_real(renderer, data->current_blend_mode);

    data->ops_after_present = 0;
    return 0;
}

static void OGC_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    OGC_TextureData *ogc_tex = texture->driverdata;

    if (ogc_tex) {
        free(ogc_tex->texels);
        SDL_free(ogc_tex);
        texture->driverdata = NULL;
    }
}

static void OGC_DestroyRenderer(SDL_Renderer *renderer)
{
    OGC_RenderData *data = renderer->driverdata;

    if (data) {
        SDL_free(data);
    }

    SDL_free(renderer);
}

static int OGC_SetVSync(SDL_Renderer *renderer, const int vsync)
{
    OGC_RenderData *data = (OGC_RenderData *)renderer->driverdata;
    data->vsync = vsync ? 1 : 0;
    return 0;
}

static SDL_Renderer *OGC_CreateRenderer(SDL_Window *window, Uint32 flags)
{
    SDL_Renderer *renderer;
    OGC_RenderData *data;

    renderer = (SDL_Renderer *)SDL_calloc(1, sizeof(*renderer));
    if (!renderer) {
        SDL_OutOfMemory();
        return NULL;
    }

    data = (OGC_RenderData *)SDL_calloc(1, sizeof(*data));
    if (!data) {
        OGC_DestroyRenderer(renderer);
        SDL_OutOfMemory();
        return NULL;
    }

    data->efb_pixel_format = GX_PF_RGB8_Z24;
    data->current_blend_mode = SDL_BLENDMODE_INVALID;
    data->vsync = true;

    renderer->WindowEvent = OGC_WindowEvent;
    renderer->CreateTexture = OGC_CreateTexture;
    renderer->UpdateTexture = OGC_UpdateTexture;
    renderer->LockTexture = OGC_LockTexture;
    renderer->UnlockTexture = OGC_UnlockTexture;
    renderer->SetTextureScaleMode = OGC_SetTextureScaleMode;
    renderer->SetRenderTarget = OGC_SetRenderTarget;
    renderer->QueueSetViewport = OGC_QueueSetViewport;
    renderer->QueueSetDrawColor = OGC_QueueSetViewport;
    renderer->QueueDrawPoints = OGC_QueueDrawPoints;
    renderer->QueueDrawLines = OGC_QueueDrawPoints;
    renderer->QueueFillRects = OGC_QueueFillRects;
    renderer->QueueGeometry = OGC_QueueGeometry;
    renderer->RunCommandQueue = OGC_RunCommandQueue;
    renderer->RenderReadPixels = OGC_RenderReadPixels;
    renderer->RenderPresent = OGC_RenderPresent;
    renderer->DestroyTexture = OGC_DestroyTexture;
    renderer->DestroyRenderer = OGC_DestroyRenderer;
    renderer->SetVSync = OGC_SetVSync;
    renderer->info = OGC_RenderDriver.info;
    renderer->driverdata = data;
    renderer->window = window;

    if (!SDL_GetHint(SDL_HINT_RENDER_LINE_METHOD)) {
        /* SDL sets the default one to point drawing, but we prefer lines */
        SDL_SetHint(SDL_HINT_RENDER_LINE_METHOD, "2");
    }

    return renderer;
}

SDL_RenderDriver OGC_RenderDriver = {
    .CreateRenderer = OGC_CreateRenderer,
    .info = {
        .name = "ogc",
        .flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE,
        .num_texture_formats = 5,
        .texture_formats = {
            [0] = SDL_PIXELFORMAT_RGB565,
            [1] = SDL_PIXELFORMAT_RGBA8888,
            [2] = SDL_PIXELFORMAT_ARGB8888,
            [3] = SDL_PIXELFORMAT_RGB24,
            [4] = SDL_PIXELFORMAT_XRGB8888,
            // TODO: add more
        },
        .max_texture_width = 1024,
        .max_texture_height = 1024,
    }
};

#endif /* SDL_VIDEO_RENDER_OGC */

/* vi: set ts=4 sw=4 expandtab: */
