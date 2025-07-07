/*
  Simple DirectMedia Layer
  Copyright (C) 2018-2018 Ash Logan <ash@heyquark.com>
  Copyright (C) 2018-2018 Roberto Van Eeden <r.r.qwertyuiop.r.r@gmail.com>
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

#ifndef SDL_wiiuvideo_h
#define SDL_wiiuvideo_h

#if SDL_VIDEO_DRIVER_WIIU

#include <gx2/surface.h>

typedef struct WIIU_VideoData WIIU_VideoData;

struct WIIU_VideoData
{
	// indicate if we're handling procui in SDL's events
	SDL_bool handleProcUI;

	SDL_bool hasForeground;
	SDL_bool enteringBackground;

	void *commandBufferPool;

	GX2TVRenderMode tvRenderMode;
	uint32_t tvWidth;
	uint32_t tvHeight;
	void *tvScanBuffer;
	uint32_t tvScanBufferSize;
	uint32_t prevSwapInterval;

	GX2DrcRenderMode drcRenderMode;
	void *drcScanBuffer;
	uint32_t drcScanBufferSize;

	// did the keyboard code initialize properly?
	int kbd_init;
};

#endif /* SDL_VIDEO_DRIVER_WIIU */

#endif /* SDL_wiiuvideo_h */
