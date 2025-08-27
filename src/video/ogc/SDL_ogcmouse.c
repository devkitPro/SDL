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

#if defined(SDL_VIDEO_DRIVER_OGC) && defined(__wii__)

#include "SDL_surface.h"
#include "SDL_hints.h"

#include "SDL_ogccursors.h"
#include "SDL_ogcgxcommon.h"
#include "SDL_ogcmouse.h"
#include "SDL_ogcpixels.h"

#include "../SDL_sysvideo.h"
#include "../../render/SDL_sysrender.h"

#include <limits.h>
#include <malloc.h>
#include <ogc/cache.h>
#include <ogc/gx.h>
#include <ogc/lwp_watchdog.h>
#include <opengx.h>
#include <wiiuse/wpad.h>

typedef struct _OGC_CursorData
{
    void *texels;
    int hot_x, hot_y;
    int w, h;
} OGC_CursorData;

typedef struct
{
    void *texels;
    int16_t x, y;
    uint16_t w, h, maxside;
} OGC_CursorBackground;

static OGC_CursorBackground s_cursor_background;
static int s_draw_counter = 0;
static bool s_extra_draw_enabled = false;
static bool s_2d_viewport_setup = false;

static void draw_rect(s16 x, s16 y, u16 w, u16 h)
{
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2s16(x, y);
    GX_TexCoord2u8(0, 0);
    GX_Position2s16(x + w, y);
    GX_TexCoord2u8(1, 0);
    GX_Position2s16(x + w, h + y);
    GX_TexCoord2u8(1, 1);
    GX_Position2s16(x, h + y);
    GX_TexCoord2u8(0, 1);
    GX_End();
}

static void draw_cursor_rect(OGC_CursorData *curdata)
{
    draw_rect(-curdata->hot_x, -curdata->hot_y, curdata->w, curdata->h);
}

static void setup_2d_viewport(_THIS)
{
    int screen_w, screen_h;

    if (s_2d_viewport_setup) return;

    screen_w = _this->displays[0].current_mode.w;
    screen_h = _this->displays[0].current_mode.h;

    OGC_set_viewport(0, 0, screen_w, screen_h);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_S16, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_U8, 0);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

    GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetNumTevStages(1);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetZMode(GX_DISABLE, GX_ALWAYS, GX_FALSE);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);

    GX_SetNumTexGens(1);
    GX_SetCurrentMtx(GX_PNMTX1);

    s_2d_viewport_setup = true;
}

/* Create a cursor from a surface */
static SDL_Cursor *OGC_CreateCursor(SDL_Surface *surface, int hot_x, int hot_y)
{
    OGC_CursorData *curdata;
    SDL_Cursor *cursor;
    u32 texture_size;
    SDL_Rect rect;

    SDL_assert(surface->pitch == surface->w * 4);

    cursor = SDL_calloc(1, sizeof(*cursor));
    if (!cursor) {
        SDL_OutOfMemory();
        return NULL;
    }

    curdata = SDL_calloc(1, sizeof(*curdata));
    if (!curdata) {
        SDL_OutOfMemory();
        SDL_free(cursor);
        return NULL;
    }

    curdata->hot_x = hot_x;
    curdata->hot_y = hot_y;
    curdata->w = surface->w;
    curdata->h = surface->h;

    texture_size = GX_GetTexBufferSize(surface->w, surface->h, GX_TF_RGBA8,
                                       GX_FALSE, 0);
    curdata->texels = memalign(32, texture_size);
    if (!curdata->texels) {
        SDL_OutOfMemory();
        SDL_free(curdata);
        SDL_free(cursor);
        return NULL;
    }

    rect.x = rect.y = 0;
    rect.w = surface->w;
    rect.h = surface->h;
    OGC_pixels_to_texture(surface->pixels, surface->format->format, &rect,
                          surface->pitch, curdata->texels, surface->w);
    DCStoreRange(curdata->texels, texture_size);
    GX_InvalidateTexAll();

    cursor->driverdata = curdata;

    return cursor;
}

SDL_Cursor *OGC_CreateSystemCursor(SDL_SystemCursor id)
{
    const OGC_Cursor *cursor;
    SDL_Surface *surface;
    SDL_Cursor *c;

    switch (id) {
    case SDL_SYSTEM_CURSOR_ARROW:
        cursor = &OGC_cursor_arrow;
        break;
    case SDL_SYSTEM_CURSOR_HAND:
        cursor = &OGC_cursor_hand;
        break;
    default:
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
                    "System cursor %d not implemented", id);
        return NULL;
    }
    surface =
        SDL_CreateRGBSurfaceWithFormatFrom((void*)cursor->pixel_data,
                                           cursor->width,
                                           cursor->height,
                                           cursor->bytes_per_pixel * 8,
                                           cursor->width * cursor->bytes_per_pixel,
                                           SDL_PIXELFORMAT_RGBA8888);
    c = OGC_CreateCursor(surface, cursor->hot_x, cursor->hot_y);
    SDL_FreeSurface(surface);
    return c;
}

/* Free a window manager cursor */
static void OGC_FreeCursor(SDL_Cursor *cursor)
{
    OGC_CursorData *curdata = cursor->driverdata;

    if (curdata) {
        if (curdata->texels) {
            free(curdata->texels);
        }
        SDL_free(curdata);
    }

    SDL_free(cursor);
}

void OGC_InitMouse(_THIS)
{
    SDL_Mouse *mouse = SDL_GetMouse();

    mouse->CreateCursor = OGC_CreateCursor;
    mouse->CreateSystemCursor = OGC_CreateSystemCursor;
    mouse->FreeCursor = OGC_FreeCursor;

    SDL_SetDefaultCursor(OGC_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND));
}

void OGC_QuitMouse(_THIS)
{
}

void OGC_draw_cursor(_THIS)
{
    SDL_Mouse *mouse = SDL_GetMouse();
    OGC_CursorData *curdata;
    Mtx mv;
    int screen_w, screen_h;
    float angle = 0.0f;

    s_draw_counter++;

    /* mark the texture as invalid */
    s_cursor_background.x = SHRT_MIN;

    if (!mouse || !mouse->cursor_shown ||
        !mouse->cur_cursor || !mouse->cur_cursor->driverdata) {
        return;
    }

    /* If this is the default cursor, rotate it, and if it's not pointed at the
     * screen, hide it */
    if (mouse->cur_cursor == mouse->def_cursor) {
        WPADData *data = WPAD_Data(mouse->mouseID);
        angle = data->ir.angle;
        if (!data->ir.valid) return;
    }

    screen_w = _this->displays[0].current_mode.w;
    screen_h = _this->displays[0].current_mode.h;

    curdata = mouse->cur_cursor->driverdata;

    if (s_extra_draw_enabled) {
        /* Save the are behind the cursor. We could use GX_ReadBoundingBox() to
         * figure out which area to save, but that would require calling the
         * drawing function once mode. So, let's just take a guess at the area,
         * taking into account possible cursor rotation. */
        s16 x, y;
        u16 w, h, radius, side;
        u32 texture_size;

        /* +1 is for the rounding of x and y */
        radius = MAX(curdata->w, curdata->h) + 1;
        x = mouse->x - radius;
        y = mouse->y - radius;
        /* x and y must be multiples of 2 */
        if (x % 2) x--;
        if (y % 2) y--;
        w = h = side = radius * 2;
        if (x < 0) {
            w += x;
            x = 0;
        } else if (x + w > screen_w) {
            w = screen_w - x;
        }

        if (y < 0) {
            h += y;
            y = 0;
        } else if (y + h > screen_h) {
            h = screen_h - y;
        }

        /* Make sure all our variables are properly aligned */
        while (side % 4) side++;
        while (w % 4) w++;
        while (h % 4) h++;

        if (w > 0 && h > 0) {
            texture_size = GX_GetTexBufferSize(side, side, GX_TF_RGBA8,
                                               GX_FALSE, 0);
            if (!s_cursor_background.texels || side > s_cursor_background.maxside) {
                free(s_cursor_background.texels);
                s_cursor_background.texels = memalign(32, texture_size);
                s_cursor_background.maxside = side;
            }
            DCInvalidateRange(s_cursor_background.texels, texture_size);
            GX_SetTexCopySrc(x, y, w, h);
            GX_SetTexCopyDst(w, h, GX_TF_RGBA8, GX_FALSE);
            GX_CopyTex(s_cursor_background.texels, GX_FALSE);
            s_cursor_background.x = x;
            s_cursor_background.y = y;
            s_cursor_background.w = w;
            s_cursor_background.h = h;
        }
    }

    OGC_load_texture(curdata->texels, curdata->w, curdata->h, GX_TF_RGBA8,
                     SDL_ScaleModeNearest);

    guMtxIdentity(mv);
    guMtxScaleApply(mv, mv, screen_w / 640.0f, screen_h / 480.0f, 1.0f);
    if (angle != 0.0f) {
        Mtx rot;
        guMtxRotDeg(rot, 'z', angle);
        guMtxConcat(mv, rot, mv);
    }
    guMtxTransApply(mv, mv, mouse->x, mouse->y, 0);
    GX_LoadPosMtxImm(mv, GX_PNMTX1);

    setup_2d_viewport(_this);

    draw_cursor_rect(curdata);
    GX_DrawDone();
}

void OGC_restore_viewport(_THIS)
{
    /* Restore default state for SDL (opengx restores it at every frame, so we
     * don't care about it) */
    s_2d_viewport_setup = false;
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetCurrentMtx(GX_PNMTX0);
    if (_this->windows) {
        /* Restore previous viewport for the renderer */
        SDL_Renderer *renderer = SDL_GetRenderer(_this->windows);
        if (renderer) {
            OGC_set_viewport(renderer->viewport.x, renderer->viewport.y,
                             renderer->viewport.w, renderer->viewport.h);
        }
    }
}

bool OGC_prep_draw_cursor(_THIS)
{
    GXTexObj background;
    Mtx mv;
    static u32 last_draw_ms = 0;
    u32 current_time_ms, elapsed_ms;
    static int call_counter = 0;
    static int last_draw_counter = 0;

    /* Ignore calls when a render target is set or OpenGL is not ready to swap
     * the framebuffer */
    SDL_Renderer *renderer = SDL_GetRenderer(_this->windows);
    if (renderer && renderer->target) return false;

    if (_this->gl_config.driver_loaded &&
        ogx_prepare_swap_buffers() < 0) return false;

    /* If this function is called repeatedly during the same frame, we assume
     * that this is one of those applications that call SDL_OGC_GL_SwapWindow,
     * SDL_UpdateWindowSurface or SDL_RenderPresent only if the video contents
     * have actually changed.
     * If that's the case, we toggle a flag that makes us redraw the screen
     * when the mouse position has changed.
     */
    if (!s_extra_draw_enabled) {
        if (last_draw_counter != s_draw_counter) {
            call_counter = 1;
            last_draw_counter = s_draw_counter;
            return false;
        }

        if (call_counter++ > 10) {
            s_extra_draw_enabled = true;
        } else {
            return false;
        }
    }

    /* Avoid drawing too often. 30 FPS should be enough */
    current_time_ms = gettime() / TB_TIMER_CLOCK;
    elapsed_ms = current_time_ms - last_draw_ms;
    if (elapsed_ms < 33) return false;

    /* If we have a texture for the cursor background, restore it; otherwise,
     * we shouldn't draw the cursor. */
    if (!s_cursor_background.texels) return false;

    if (s_cursor_background.x != SHRT_MIN) {
        setup_2d_viewport(_this);

        GX_PixModeSync();
        GX_InitTexObj(&background, s_cursor_background.texels,
                      s_cursor_background.w, s_cursor_background.h,
                      GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GX_InitTexObjLOD(&background, GX_NEAR, GX_NEAR,
                         0.0f, 0.0f, 0.0f, 0, 0, GX_ANISO_1);
        GX_LoadTexObj(&background, GX_TEXMAP0);
        GX_InvalidateTexAll();

        guMtxIdentity(mv);
        GX_LoadPosMtxImm(mv, GX_PNMTX1);
        draw_rect(s_cursor_background.x, s_cursor_background.y,
                  s_cursor_background.w, s_cursor_background.h);
        last_draw_ms = current_time_ms;
    }
    return true;
}
#endif /* SDL_VIDEO_DRIVER_OGC */

/* vi: set ts=4 sw=4 expandtab: */
