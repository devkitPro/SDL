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

#include "SDL_ogcpixels.h"

#include <ogc/gx.h>

#define PIXELS_TO_TEXTURE_32(format_func) \
    static void pixels_to_texture_ ## format_func( \
        void *pixels, const SDL_Rect *rect, int16_t pitch, void *texture, int16_t tex_width) \
    { \
        int16_t tex_pitch = (tex_width + 3) / 4 * 4; \
        for (int row = 0; row < rect->h; row++) { \
            int y = rect->y + row; \
            u32 *src = (u32 *)((u8 *)pixels + pitch * row); \
            for (int col = 0; col < rect->w; col++) { \
                int x = rect->x + col; \
                u32 offset = (((y >> 2) << 4) * tex_pitch) + \
                    ((x >> 2) << 6) + (((y % 4 << 2) + x % 4) << 1); \
                set_pixel_to_texture_ ## format_func(texture, offset, *src++); \
            } \
        } \
    }

#define PIXELS_FROM_TEXTURE_32(format_func) \
    static void pixels_from_texture_ ## format_func( \
        void *pixels, int16_t w, int16_t h, int16_t pitch, void *texture) \
    { \
        int tex_width = (w + 3) / 4 * 4; \
        for (int y = 0; y < h; y++) { \
            u32 *dst = (u32 *)((u8 *)pixels + pitch * y); \
            for (int x = 0; x < w; x++) { \
                u32 offset = (((y >> 2) << 4) * tex_width) + \
                    ((x >> 2) << 6) + (((y % 4 << 2) + x % 4) << 1); \
                *dst++ = get_pixel_from_texture_ ## format_func(texture, offset); \
            } \
        } \
    }

static u8 texture_format_from_SDL(const SDL_PixelFormat format)
{
    switch (format) {
    case SDL_PIXELFORMAT_INDEX8:
        return GX_TF_CI8;
    case SDL_PIXELFORMAT_RGB565:
        return GX_TF_RGB565;
    case SDL_PIXELFORMAT_RGB24:
    case SDL_PIXELFORMAT_RGBA8888:
    case SDL_PIXELFORMAT_ARGB8888:
    case SDL_PIXELFORMAT_XRGB8888:
        return GX_TF_RGBA8;
    default:
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
                     "(texture_format_from_SDL) Unsupported SDL pixel format %d",
                     format);
    }
    return 0xff; // invalid
}

static inline void set_pixel_to_texture_ARGB(void *texture, u32 offset, u32 color)
{
    *(u16*)(texture + offset) = color >> 16;
    *(u16*)(texture + offset + 32) = color;
}

static inline u32 get_pixel_from_texture_ARGB(void *texture, u32 offset)
{
    return *(u16*)(texture + offset) << 16 |
           *(u16*)(texture + offset + 32);
}

static inline void set_pixel_to_texture_RGBA(void *texture, u32 offset, u32 color)
{
    set_pixel_to_texture_ARGB(texture, offset, (color << 24) | (color >> 8));
}

static inline u32 get_pixel_from_texture_32(int x, int y, void *texture, int tex_width)
{
    u8 *tex = texture;
    u32 offset;

    offset = (((y >> 2) << 4) * tex_width) + ((x >> 2) << 6) + (((y % 4 << 2) + x % 4) << 1);

    return *(tex + offset) |
           *(tex + offset + 1) << 24 |
           *(tex + offset + 32) << 16 |
           *(tex + offset + 33) << 8;
}

PIXELS_TO_TEXTURE_32(RGBA)

static void pixels_RGBA_from_texture(void *pixels, int16_t w, int16_t h,
                                     int16_t pitch, void *texture)
{
    u32 *dst = pixels;

    int tex_width = (w + 3) / 4 * 4;
    for (int y = 0; y < h; y++) {
        dst = (u32 *)((u8 *)pixels + pitch * y);
        for (int x = 0; x < w; x++) {
            *dst++ = get_pixel_from_texture_32(x, y, texture, tex_width);
        }
    }
}

PIXELS_TO_TEXTURE_32(ARGB)
PIXELS_FROM_TEXTURE_32(ARGB)

static inline void set_pixel_to_texture_XRGB(void *texture, u32 offset, u32 color)
{
    set_pixel_to_texture_ARGB(texture, offset, 0xff000000 | color);
}

PIXELS_TO_TEXTURE_32(XRGB)

static void pixels_XRGB_from_texture(void *pixels, int16_t w, int16_t h,
                                     int16_t pitch, void *texture)
{
    u32 *dst = pixels;

    int tex_width = (w + 3) / 4 * 4;
    for (int y = 0; y < h; y++) {
        dst = (u32 *)((u8 *)pixels + pitch * y);
        for (int x = 0; x < w; x++) {
            *dst++ = get_pixel_from_texture_32(x, y, texture, tex_width) >> 8;
        }
    }
}

static void pixels_RGB_to_texture(void *pixels, const SDL_Rect *rect,
                                  int16_t pitch, void *texture, int16_t tex_width)
{
    u8 *src = pixels;

    int tex_pitch = (tex_width + 3) / 4 * 4;
    for (int row = 0; row < rect->h; row++) {
        int y = rect->y + row;
        src = (u8 *)pixels + pitch * row;
        for (int col = 0; col < rect->w; col++) {
            int x = rect->x + col;
            u32 offset = (((y >> 2) << 4) * tex_pitch) +
                ((x >> 2) << 6) + (((y % 4 << 2) + x % 4) << 1);
            u8 r = *src++;
            u8 g = *src++;
            u8 b = *src++;
            set_pixel_to_texture_ARGB(texture, offset, 0xff000000 | r << 16 | g << 8 | b);
        }
    }
}

static void pixels_RGB_from_texture(void *pixels, int16_t w, int16_t h,
                                    int16_t pitch, void *texture)
{
    u8 *dst = pixels;

    int tex_width = (w + 3) / 4 * 4;
    for (int y = 0; y < h; y++) {
        dst = (u8 *)pixels + pitch * y;
        for (int x = 0; x < w; x++) {
            u32 color = get_pixel_from_texture_32(x, y, texture, tex_width);
            *dst++ = color >> 24;
            *dst++ = color >> 16;
            *dst++ = color >> 8;
        }
    }
}

static void pixels_16_to_texture(void *pixels, const SDL_Rect *rect, int16_t pitch,
                                 void *texture, int16_t tex_width)
{
    /* We only support coordinates multiple of 4. While we don't add support
     * for arbitrary coordinates (TODO), let's restrict the paint area to the
     * full 4x4 cells covered by the rect. In the future we can add code to
     * fill up the remaining borders. */
    int x0 = (rect->x + 3) & ~0x3;
    int x1 = (rect->x + rect->w) & ~0x3;
    int y0 = (rect->y + 3) & ~0x3;
    int y1 = (rect->y + rect->h) & ~0x3;
    int skipped_bytes_left = (x0 - rect->x) * 2;
    int tex_pitch = tex_width * 2;


    for (int row = 0; row < y1 - y0; row += 4) {
        u64 *src1 = pixels + (skipped_bytes_left + row * pitch);
        u64 *src2 = (void*)src1 + (pitch * 1);
        u64 *src3 = (void*)src1 + (pitch * 2);
        u64 *src4 = (void*)src1 + (pitch * 3);
        u64 *dst = texture + (x0 * 8 + (y0 + row) * tex_pitch);
        for (int col = x0; col < x1; col += 4) {
            *dst++ = *src1++;
            *dst++ = *src2++;
            *dst++ = *src3++;
            *dst++ = *src4++;
        }
    }
}

static void pixels_16_from_texture(void *pixels, int16_t pitch, int16_t h,
                                   void *texture)
{
    long long int *src = texture;
    long long int *dst1 = pixels;
    long long int *dst2 = (long long int *)((char *)pixels + (pitch * 1));
    long long int *dst3 = (long long int *)((char *)pixels + (pitch * 2));
    long long int *dst4 = (long long int *)((char *)pixels + (pitch * 3));
    int rowpitch = (pitch >> 3) * 3;

    for (int y = 0; y < h; y += 4) {
        for (int x = 0; x < pitch; x += 8) {
            *dst1++ = *src++;
            *dst2++ = *src++;
            *dst3++ = *src++;
            *dst4++ = *src++;
        }

        dst1 = dst4;
        dst2 += rowpitch;
        dst3 += rowpitch;
        dst4 += rowpitch;
    }
}

static inline void set_pixel_8_to_texture(int x, int y, u8 color, void *texture, int tex_width)
{
    u8 *tex = texture;
    u32 offset;

    offset = ((y & ~3) * tex_width) + ((x & ~7) << 2) + ((y & 3) << 3) + (x & 7);

    tex[offset] = color;
}

static inline u8 get_pixel_8_from_texture(int x, int y, void *texture, int tex_width)
{
    u8 *tex = texture;
    u32 offset;

    offset = ((y & ~3) * tex_width) + ((x & ~7) << 2) + ((y & 3) << 3) + (x & 7);

    return tex[offset];
}

static void pixels_8_to_texture(void *pixels, int16_t w, int16_t h,
                                int16_t pitch, void *texture)
{
    u8 *src = pixels;
    int tex_width = (w + 7) / 8 * 8;

    for (int y = 0; y < h; y++) {
        src = (u8 *)pixels + pitch * y;
        for (int x = 0; x < w; x++) {
            set_pixel_8_to_texture(x, y, *src++, texture, tex_width);
        }
    }
}

static void pixels_8_from_texture(void *pixels, int16_t w, int16_t h,
                                  int16_t pitch, void *texture)
{
    u8 *dst = pixels;
    int tex_width = (w + 7) / 8 * 8;

    for (int y = 0; y < h; y++) {
        dst = (u8 *)pixels + pitch * y;
        for (int x = 0; x < w; x++) {
            *dst++ = get_pixel_8_from_texture(x, y, texture, tex_width);
        }
    }
}

void OGC_pixels_to_texture(void *pixels, const SDL_PixelFormat format,
                           const SDL_Rect *rect, int16_t pitch,
                           void *texture, int16_t tex_width)
{
    switch (format) {
    case SDL_PIXELFORMAT_INDEX8:
        pixels_8_to_texture(pixels, rect->w, rect->h, pitch, texture);
        break;
    case SDL_PIXELFORMAT_RGB565:
        pixels_16_to_texture(pixels, rect, pitch, texture, tex_width);
        break;
    case SDL_PIXELFORMAT_RGB24:
        pixels_RGB_to_texture(pixels, rect, pitch, texture, tex_width);
        break;
    case SDL_PIXELFORMAT_RGBA8888:
        pixels_to_texture_RGBA(pixels, rect, pitch, texture, tex_width);
        break;
    case SDL_PIXELFORMAT_ARGB8888:
        pixels_to_texture_ARGB(pixels, rect, pitch, texture, tex_width);
        break;
    case SDL_PIXELFORMAT_XRGB8888:
        pixels_to_texture_XRGB(pixels, rect, pitch, texture, tex_width);
        break;
    default:
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
                     "ptt: unsupported SDL pixel format %d", format);
        // TODO support more formats
    }
}

void OGC_pixels_from_texture(void *pixels, const SDL_PixelFormat format,
                             int16_t w, int16_t h, int16_t pitch,
                             void *texture)
{
    switch (format) {
    case SDL_PIXELFORMAT_INDEX8:
        pixels_8_from_texture(pixels, w, h, pitch, texture);
        break;
    case SDL_PIXELFORMAT_RGB565:
        pixels_16_from_texture(pixels, pitch, h, texture);
        break;
    case SDL_PIXELFORMAT_RGB24:
        pixels_RGB_from_texture(pixels, w, h, pitch, texture);
        break;
    case SDL_PIXELFORMAT_RGBA8888:
        pixels_RGBA_from_texture(pixels, w, h, pitch, texture);
        break;
    case SDL_PIXELFORMAT_ARGB8888:
        pixels_from_texture_ARGB(pixels, w, h, pitch, texture);
        break;
    case SDL_PIXELFORMAT_XRGB8888:
        pixels_XRGB_from_texture(pixels, w, h, pitch, texture);
        break;
    default:
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
                     "pft: unsupported SDL pixel format %d", format);
        // TODO support more formats
    }
}

u8 OGC_texture_format_from_SDL(const SDL_PixelFormat format)
{
    return texture_format_from_SDL(format);
}
