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

#ifndef SDL_ogcvideo_h_
#define SDL_ogcvideo_h_

#include "../SDL_sysvideo.h"

#include <ogc/gx_struct.h>

typedef struct SDL_VideoData
{
    GXRModeObj *vmode;
    u8 *gp_fifo;
    void *xfb[2];
    u8 fb_index;
} SDL_VideoData;

typedef struct SDL_WindowData
{
    void *pixels;
    u8 *texels;
    SDL_PixelFormat surface_format;
} SDL_WindowData;

void *OGC_video_get_xfb(SDL_VideoDevice *device);
void OGC_video_flip(SDL_VideoDevice *device, bool vsync);

#endif /* SDL_ogcvideo_h_ */

/* vi: set ts=4 sw=4 expandtab: */
