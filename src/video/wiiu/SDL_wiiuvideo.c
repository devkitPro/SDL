/*
  Simple DirectMedia Layer
  Copyright (C) 2018-2018 Ash Logan <ash@heyquark.com>
  Copyright (C) 2018-2018 rw-r-r-0644 <r.r.qwertyuiop.r.r@gmail.com>
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

#if SDL_VIDEO_DRIVER_WIIU

/* SDL internals */
#include "../SDL_sysvideo.h"
#include "SDL_version.h"
#include "SDL_syswm.h"
#include "SDL_loadso.h"
#include "SDL_events.h"
#include "SDL_render.h"
#include "../../events/SDL_mouse_c.h"
#include "../../events/SDL_keyboard_c.h"
#include "../../events/SDL_events_c.h"
#include "SDL_wiiuvideo.h"
#include "SDL_wiiukeyboard.h"
#include "SDL_wiiu_gfx_heap.h"

#include "../../render/wiiu/SDL_render_wiiu.h"

#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <stdint.h>

#include <coreinit/foreground.h>
#include <proc_ui/procui.h>
#include <sysapp/launch.h>

#include <gx2/context.h>
#include <gx2/display.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/state.h>
#include <gx2r/mem.h>
#include <gx2r/surface.h>

#define DRC_SCREEN_WIDTH    854
#define DRC_SCREEN_HEIGHT   480

static SDL_bool running = SDL_FALSE;

static int WIIU_ForegroundAcquired(_THIS)
{
	WIIU_VideoData *videodata;
	SDL_Window* window;

	if (!running) {
		return 0;
	}

	videodata = (WIIU_VideoData *) _this->driverdata;
	window = _this->windows;
	videodata->hasForeground = SDL_TRUE;

	// initialize gfx heaps once in forground
	if (WIIU_GfxHeap_ForegroundInit() != 0) {
		return -1;
	}

	if (WIIU_GfxHeap_MEM1Init() != 0) {
		return -1;
	}

	// allocate and set scanbuffers
	videodata->tvScanBuffer = WIIU_GfxHeap_ForegroundAlloc(GX2_SCAN_BUFFER_ALIGNMENT, videodata->tvScanBufferSize);
	if (!videodata->tvScanBuffer) {
		return -1;
	}

	GX2Invalidate(GX2_INVALIDATE_MODE_CPU, videodata->tvScanBuffer, videodata->tvScanBufferSize);
	GX2SetTVBuffer(videodata->tvScanBuffer, videodata->tvScanBufferSize, videodata->tvRenderMode, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, GX2_BUFFERING_MODE_DOUBLE);

	videodata->drcScanBuffer = WIIU_GfxHeap_ForegroundAlloc(GX2_SCAN_BUFFER_ALIGNMENT, videodata->drcScanBufferSize);
	if (!videodata->drcScanBuffer) {
		return -1;
	}

	GX2Invalidate(GX2_INVALIDATE_MODE_CPU, videodata->drcScanBuffer, videodata->drcScanBufferSize);
	GX2SetDRCBuffer(videodata->drcScanBuffer, videodata->drcScanBufferSize, videodata->drcRenderMode, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, GX2_BUFFERING_MODE_DOUBLE);

	if (videodata->handleProcUI) {
		SDL_SendAppEvent(SDL_APP_WILLENTERFOREGROUND);
		SDL_SendAppEvent(SDL_APP_DIDENTERFOREGROUND);
	}

	while (window) {
		SDL_Renderer* renderer = SDL_GetRenderer(window);

		// Recreate the window texture, now that we have foreground memory available
		if (renderer) {
			WIIU_SDL_CreateWindowTex(renderer, window);
		}

		// We're now in foreground, window is visible
		SDL_SendWindowEvent(window, SDL_WINDOWEVENT_SHOWN, 0, 0);

		window = window->next;
	}

	return 0;
}

static int WIIU_ForegroundReleased(_THIS)
{
	WIIU_VideoData *videodata;
	SDL_Window* window;

	if (!running) {
		return 0;
	}

	videodata = (WIIU_VideoData *) _this->driverdata;
	window = _this->windows;

	// make sure the GPU is done drawing
	GX2DrawDone();

	if (videodata->tvScanBuffer) {
		WIIU_GfxHeap_ForegroundFree(videodata->tvScanBuffer);
		videodata->tvScanBuffer = NULL;
	}

	if (videodata->drcScanBuffer) {
		WIIU_GfxHeap_ForegroundFree(videodata->drcScanBuffer);
		videodata->drcScanBuffer = NULL;
	}

	while (window) {
		SDL_Renderer* renderer = SDL_GetRenderer(window);

		// Avoid sending the event if we're handling ProcUI, since we send this
		// event from inside WIIU_PumpEvents().
		// Note: the application won't receive this event until after we return to
		// the foreground.
		if (!videodata->handleProcUI) {
			// No longer in foreground, window is no longer visible.
			SDL_SendWindowEvent(window, SDL_WINDOWEVENT_HIDDEN, 0, 0);
		}

		// Destroy window texture, we no longer have access to foreground memory
		if (renderer) {
			// TODO this causes crashes on exit for some reason?
			// Doesn't matter since we destroy the heap anyways ¯\_(ツ)_/¯
			//WIIU_SDL_DestroyWindowTex(renderer, window);
		}

		window = window->next;
	}

	WIIU_GfxHeap_MEM1Destroy();
	WIIU_GfxHeap_ForegroundDestroy();

	// This is necessary to avoid a black frame on leaving foreground
	GX2SetTVEnable(TRUE);
	GX2SetDRCEnable(TRUE);

	videodata->hasForeground = SDL_FALSE;

	return 0;
}

static uint32_t WiiU_SaveCallback(void * arg)
{
	OSSavesDone_ReadyToRelease();
	return 0;
}

static int WIIU_VideoInit(_THIS)
{
	WIIU_VideoData *videodata = (WIIU_VideoData *) _this->driverdata;
	uint32_t unk;
	SDL_DisplayMode mode;
	uint32_t* initAttribs;

	// check if the user already set up procui or if we should handle it
	if (!ProcUIIsRunning()) {
		ProcUIInitEx(WiiU_SaveCallback, NULL);

		videodata->handleProcUI = SDL_TRUE;
	}

	// allocate command buffer pool
	videodata->commandBufferPool = memalign(GX2_COMMAND_BUFFER_ALIGNMENT, GX2_COMMAND_BUFFER_SIZE);
	if (!videodata->commandBufferPool) {
		return SDL_OutOfMemory();
	}

	// initialize GX2
	initAttribs = (uint32_t[]) {
		GX2_INIT_CMD_BUF_BASE, (uintptr_t) videodata->commandBufferPool,
		GX2_INIT_CMD_BUF_POOL_SIZE, GX2_COMMAND_BUFFER_SIZE,
		GX2_INIT_ARGC, 0,
		GX2_INIT_ARGV, 0,
		GX2_INIT_END
	};
	GX2Init(initAttribs);

	// figure out the TV render mode and size
	switch(GX2GetSystemTVScanMode()) {
	case GX2_TV_SCAN_MODE_480I:
	case GX2_TV_SCAN_MODE_480P:
		videodata->tvRenderMode = GX2_TV_RENDER_MODE_WIDE_480P;
		videodata->tvWidth = 854;
		videodata->tvHeight = 480;
		break;
	case GX2_TV_SCAN_MODE_1080I:
	case GX2_TV_SCAN_MODE_1080P:
		videodata->tvRenderMode = GX2_TV_RENDER_MODE_WIDE_1080P;
		videodata->tvWidth = 1920;
		videodata->tvHeight = 1080;
		break;
	case GX2_TV_SCAN_MODE_720P:
	default:
		videodata->tvRenderMode = GX2_TV_RENDER_MODE_WIDE_720P;
		videodata->tvWidth = 1280;
		videodata->tvHeight = 720;
		break;
	}

	videodata->drcRenderMode = GX2GetSystemDRCScanMode();

	// calculate the scanbuffer sizes
	GX2CalcTVSize(videodata->tvRenderMode, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, GX2_BUFFERING_MODE_DOUBLE, &videodata->tvScanBufferSize, &unk);
	GX2CalcDRCSize(videodata->drcRenderMode, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, GX2_BUFFERING_MODE_DOUBLE, &videodata->drcScanBufferSize, &unk);

	// set GX2R allocator for the gfx heap
	GX2RSetAllocator(&WIIU_GfxHeap_GX2RAlloc, &WIIU_GfxHeap_GX2RFree);

	// register callbacks for acquiring and releasing foreground
	ProcUIRegisterCallback(PROCUI_CALLBACK_ACQUIRE, (ProcUICallback) WIIU_ForegroundAcquired, _this, 100);
	ProcUIRegisterCallback(PROCUI_CALLBACK_RELEASE, (ProcUICallback) WIIU_ForegroundReleased, _this, 100);

	running = SDL_TRUE;

	// if this is running, the application is already in foreground so call the callback
	if (WIIU_ForegroundAcquired(_this) != 0) {
		free(videodata->commandBufferPool);
		videodata->commandBufferPool = NULL;
		return SDL_OutOfMemory();
	}

	GX2SetTVScale(videodata->tvWidth, videodata->tvHeight);
	GX2SetDRCScale(DRC_SCREEN_WIDTH, DRC_SCREEN_HEIGHT);

	// add tv display
	SDL_zero(mode);
	mode.format = SDL_PIXELFORMAT_RGBA8888;
	mode.w = videodata->tvWidth;
	mode.h = videodata->tvHeight;
	mode.refresh_rate = 60;
	SDL_AddBasicVideoDisplay(&mode);

	// add drc display
	SDL_zero(mode);
	mode.format = SDL_PIXELFORMAT_RGBA8888;
	mode.w = DRC_SCREEN_WIDTH;
	mode.h = DRC_SCREEN_HEIGHT;
	mode.refresh_rate = 60;
	SDL_AddBasicVideoDisplay(&mode);

	videodata->kbd_init = SDL_WIIU_InitKeyboard(_this);

	return 0;
}

static void WIIU_VideoQuit(_THIS)
{
	WIIU_VideoData *videodata = (WIIU_VideoData *) _this->driverdata;

	if (videodata->handleProcUI) {
		// Put ProcUI into EXIT/shutdown state if user stopped processing events
		// before SDL_QUIT was generated.
		if (ProcUIIsRunning() && !ProcUIInShutdown()) {
			SDL_bool procui_running = SDL_TRUE;
			SYSLaunchMenu();
			while (procui_running) {
				switch (ProcUIProcessMessages(TRUE)) {
				case PROCUI_STATUS_RELEASE_FOREGROUND:
					ProcUIDrawDoneRelease();
					break;
				case PROCUI_STATUS_EXITING:
					procui_running = SDL_FALSE;
					break;
				default:
					;
				}
			}
		}
	}

	// if we're in foreground, destroy foreground data
	if (videodata->hasForeground) {
		WIIU_ForegroundReleased(_this);
	}

	// shutdown GX2 and free command buffer
	GX2Shutdown();

	if (videodata->commandBufferPool) {
		free(videodata->commandBufferPool);
		videodata->commandBufferPool = NULL;
	}

	if (videodata->handleProcUI) {
		ProcUIShutdown();
	}

	if (videodata->kbd_init)
		SDL_WIIU_QuitKeyboard(_this);

	running = SDL_FALSE;
}

static int WIIU_CreateSDLWindow(_THIS, SDL_Window * window)
{
	// focus the window
	SDL_SetMouseFocus(window);
	SDL_SetKeyboardFocus(window);
	return 0;
}

static void WIIU_SetWindowSize(_THIS, SDL_Window * window)
{
}

static void WIIU_DestroyWindow(_THIS, SDL_Window * window)
{
}

static void WIIU_GetDisplayModes(_THIS, SDL_VideoDisplay * display)
{
	// we currently only have one mode per display, which is the current one
	SDL_AddDisplayMode(display, &display->current_mode);
}

static int WIIU_SetDisplayMode(_THIS, SDL_VideoDisplay * display, SDL_DisplayMode * mode)
{
	return 0;
}

static void WIIU_PumpEvents(_THIS)
{
	WIIU_VideoData *videodata = (WIIU_VideoData *) _this->driverdata;

	if (videodata->handleProcUI && !ProcUIInShutdown()) {
		if (videodata->enteringBackground) {
			// The previous ProcUIProcessMessages() received a
			// PROCUI_STATUS_RELEASE_FOREGROUND.
			videodata->enteringBackground = SDL_FALSE;
			ProcUIDrawDoneRelease();
		}

		if (ProcUIIsRunning() && !ProcUIInShutdown()) {
			ProcUIStatus status = ProcUIProcessMessages(TRUE);
			switch (status) {
			case PROCUI_STATUS_IN_FOREGROUND:
				videodata->enteringBackground = SDL_FALSE;
				break;
			case PROCUI_STATUS_IN_BACKGROUND:
				break;
			case PROCUI_STATUS_RELEASE_FOREGROUND: {
				SDL_Window* window = _this->windows;
				while (window) {
					// No longer in foreground, window is no longer
					// visible.
					SDL_SendWindowEvent(window, SDL_WINDOWEVENT_HIDDEN, 0, 0);
					window = window->next;
				}
				SDL_SendAppEvent(SDL_APP_WILLENTERBACKGROUND);
				SDL_SendAppEvent(SDL_APP_DIDENTERBACKGROUND);
				// Note: we don't call ProcUIDrawDoneRelease() here to give
				// the application a chance to receive and process the
				// events queued above. The next call to WIIU_PumpEvents()
				// is the one that actually enters the background.
				videodata->enteringBackground = SDL_TRUE;
				break;
			}
			case PROCUI_STATUS_EXITING:
				SDL_SendQuit();
				break;
			}
		}
	}

	SDL_WIIU_PumpKeyboardEvents(_this);
}

static void WIIU_DeleteDevice(_THIS)
{
	SDL_free(_this->driverdata);
	SDL_free(_this);
}

static SDL_VideoDevice *WIIU_CreateDevice(void)
{
	SDL_VideoDevice *device;
	WIIU_VideoData *videodata;

	device = (SDL_VideoDevice*) SDL_calloc(1, sizeof(SDL_VideoDevice));
	if(!device) {
		SDL_OutOfMemory();
		return NULL;
	}

	videodata = (WIIU_VideoData*) SDL_calloc(1, sizeof(WIIU_VideoData));
	if(!videodata) {
		SDL_OutOfMemory();
		return NULL;
	}

	device->driverdata = videodata;

	// Setup amount of available displays
	device->num_displays = 0;

	device->VideoInit = WIIU_VideoInit;
	device->VideoQuit = WIIU_VideoQuit;
	device->CreateSDLWindow = WIIU_CreateSDLWindow;
	device->SetWindowSize = WIIU_SetWindowSize;
	device->DestroyWindow = WIIU_DestroyWindow;
	device->GetDisplayModes = WIIU_GetDisplayModes;
	device->SetDisplayMode = WIIU_SetDisplayMode;
	device->PumpEvents = WIIU_PumpEvents;

	device->free = WIIU_DeleteDevice;

	return device;
}

VideoBootStrap WIIU_bootstrap = {
	"WiiU", "Video driver for Nintendo WiiU",
	WIIU_CreateDevice
};

#endif /* SDL_VIDEO_DRIVER_WIIU */
