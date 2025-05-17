/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2018 Sam Lantinga <slouken@libsdl.org>

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

#if SDL_VIDEO_DRIVER_SWITCH

#include "../../events/SDL_keyboard_c.h"
#include "../../events/SDL_mouse_c.h"
#include "../SDL_sysvideo.h"

#include "SDL_switchkeyboard.h"
#include "SDL_switchmouse_c.h"
#include "SDL_switchopengles.h"
#include "SDL_switchswkb.h"
#include "SDL_switchtouch.h"
#include "SDL_switchvideo.h"

/* Currently only one window */
static SDL_Window *switch_window = NULL;
static AppletOperationMode operationMode;

static bool SWITCH_VideoInit(SDL_VideoDevice *_this);
static void SWITCH_VideoQuit(SDL_VideoDevice *_this);
static bool SWITCH_GetDisplayModes(SDL_VideoDevice *_this, SDL_VideoDisplay *display);
static bool SWITCH_SetDisplayMode(SDL_VideoDevice *_this, SDL_VideoDisplay *display, SDL_DisplayMode *mode);
static bool SWITCH_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID create_props);
static void SWITCH_SetWindowTitle(SDL_VideoDevice *_this, SDL_Window *window);
static bool SWITCH_SetWindowIcon(SDL_VideoDevice *_this, SDL_Window *window, SDL_Surface *icon);
static bool SWITCH_SetWindowPosition(SDL_VideoDevice *_this, SDL_Window *window);
static void SWITCH_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window);
static void SWITCH_ShowWindow(SDL_VideoDevice *_this, SDL_Window *window);
static void SWITCH_HideWindow(SDL_VideoDevice *_this, SDL_Window *window);
static void SWITCH_RaiseWindow(SDL_VideoDevice *_this, SDL_Window *window);
static void SWITCH_MaximizeWindow(SDL_VideoDevice *_this, SDL_Window *window);
static void SWITCH_MinimizeWindow(SDL_VideoDevice *_this, SDL_Window *window);
static void SWITCH_RestoreWindow(SDL_VideoDevice *_this, SDL_Window *window);
static void SWITCH_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window);
static void SWITCH_PumpEvents(SDL_VideoDevice *_this);

static void SWITCH_Destroy(SDL_VideoDevice *device)
{
    if (device != NULL) {
        if (device->internal != NULL) {
            SDL_free(device->internal);
        }
        SDL_free(device);
    }
}

static SDL_VideoDevice *SWITCH_CreateDevice()
{
    SDL_VideoDevice *device;

    /* Initialize SDL_VideoDevice structure */
    device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (device == NULL) {
        SDL_OutOfMemory();
        return NULL;
    }

    /* Setup amount of available displays */
    device->num_displays = 0;

    /* Set device free function */
    device->free = SWITCH_Destroy;

    /* Setup all functions which we can handle */
    device->VideoInit = SWITCH_VideoInit;
    device->VideoQuit = SWITCH_VideoQuit;
    device->GetDisplayModes = SWITCH_GetDisplayModes;
    device->SetDisplayMode = SWITCH_SetDisplayMode;
    device->CreateSDLWindow = SWITCH_CreateWindow;
    device->SetWindowTitle = SWITCH_SetWindowTitle;
    device->SetWindowIcon = SWITCH_SetWindowIcon;
    device->SetWindowPosition = SWITCH_SetWindowPosition;
    device->SetWindowSize = SWITCH_SetWindowSize;
    device->ShowWindow = SWITCH_ShowWindow;
    device->HideWindow = SWITCH_HideWindow;
    device->RaiseWindow = SWITCH_RaiseWindow;
    device->MaximizeWindow = SWITCH_MaximizeWindow;
    device->MinimizeWindow = SWITCH_MinimizeWindow;
    device->RestoreWindow = SWITCH_RestoreWindow;
    device->DestroyWindow = SWITCH_DestroyWindow;

    device->GL_LoadLibrary = SWITCH_GLES_LoadLibrary;
    device->GL_GetProcAddress = SWITCH_GLES_GetProcAddress;
    device->GL_UnloadLibrary = SWITCH_GLES_UnloadLibrary;
    device->GL_CreateContext = SWITCH_GLES_CreateContext;
    device->GL_MakeCurrent = SWITCH_GLES_MakeCurrent;
    device->GL_SetSwapInterval = SWITCH_GLES_SetSwapInterval;
    device->GL_GetSwapInterval = SWITCH_GLES_GetSwapInterval;
    device->GL_SwapWindow = SWITCH_GLES_SwapWindow;
    device->GL_DestroyContext = SWITCH_GLES_DestroyContext;
    device->GL_DefaultProfileConfig = SWITCH_GLES_DefaultProfileConfig;

    device->StartTextInput = SWITCH_StartTextInput;
    device->StopTextInput = SWITCH_StopTextInput;
    device->HasScreenKeyboardSupport = SWITCH_HasScreenKeyboardSupport;
    device->IsScreenKeyboardShown = SWITCH_IsScreenKeyboardShown;

    device->PumpEvents = SWITCH_PumpEvents;

    return device;
}

VideoBootStrap SWITCH_bootstrap = {
    .name = "Switch",
    .desc = "Nintendo Switch Video Driver",
    .create = SWITCH_CreateDevice
};

/*****************************************************************************/
/* SDL Video and Display initialization/handling functions                   */
/*****************************************************************************/
bool SWITCH_VideoInit(SDL_VideoDevice *_this)
{
    SDL_DisplayMode current_mode;

    (void)_this;

    SDL_zero(current_mode);

    if (appletGetOperationMode() == AppletOperationMode_Handheld) {
        current_mode.w = 1280;
        current_mode.h = 720;
    } else {
        current_mode.w = 1920;
        current_mode.h = 1080;
    }

    current_mode.refresh_rate = 60.0f;
    current_mode.format = SDL_PIXELFORMAT_RGBA8888;

    if (SDL_AddBasicVideoDisplay(&current_mode) == 0) {
        return false;
    }

    // init psm service
    psmInitialize();
    // init touch
    SWITCH_InitTouch();
    // init keyboard
    SWITCH_InitKeyboard();
    // init mouse
    SWITCH_InitMouse();
    // init software keyboard
    SWITCH_InitSwkb();

    return true;
}

void SWITCH_VideoQuit(SDL_VideoDevice *_this)
{
    // this should not be needed if user code is right (SDL_GL_LoadLibrary/SDL_GL_UnloadLibrary calls match)
    // this (user) error doesn't have the same effect on switch thought, as the driver needs to be unloaded (crash)
    if (_this->gl_config.driver_loaded > 0) {
        SWITCH_GLES_UnloadLibrary(_this);
        _this->gl_config.driver_loaded = 0;
    }

    // exit touch
    SWITCH_QuitTouch();
    // exit keyboard
    SWITCH_QuitKeyboard();
    // exit mouse
    SWITCH_QuitMouse();
    // exit software keyboard
    SWITCH_QuitSwkb();
    // exit psm service
    psmExit();
}

static bool SWITCH_GetDisplayModes(SDL_VideoDevice *_this, SDL_VideoDisplay *display)
{
    SDL_DisplayMode mode;

    (void)_this;

    SDL_zero(mode);
    mode.refresh_rate = 60;
    mode.format = SDL_PIXELFORMAT_RGBA8888;

    // 1280x720 RGBA8888
    mode.w = 1280;
    mode.h = 720;
    SDL_AddFullscreenDisplayMode(display, &mode);

    // 1920x1080 RGBA8888
    mode.w = 1920;
    mode.h = 1080;
    SDL_AddFullscreenDisplayMode(display, &mode);
    return true;
}

bool SWITCH_SetDisplayMode(SDL_VideoDevice *_this, SDL_VideoDisplay *display, SDL_DisplayMode *mode)
{
    (void)display;

    if (switch_window == NULL) {
        return true;
    }

    SDL_WindowData *data = (SDL_WindowData *)switch_window->internal;
    SDL_GLContext ctx = SDL_GL_GetCurrentContext();
    NWindow *nWindow = nwindowGetDefault();

    if (data != NULL && data->egl_surface != EGL_NO_SURFACE) {
        SDL_EGL_MakeCurrent(_this, NULL, NULL);
        SDL_EGL_DestroySurface(_this, data->egl_surface);
        nwindowSetDimensions(nWindow, mode->w, mode->h);
        data->egl_surface = SDL_EGL_CreateSurface(_this, switch_window, nWindow);
        SDL_EGL_MakeCurrent(_this, data->egl_surface, ctx);
    }

    return true;
}

static bool SWITCH_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID create_props)
{
    Result rc;
    SDL_WindowData *window_data = NULL;
    NWindow *nWindow = NULL;

    if (switch_window != NULL) {
        return SDL_SetError("Switch only supports one window");
    }

    if (!_this->egl_data) {
        return SDL_SetError("EGL not initialized");
    }

    window_data = (SDL_WindowData *)SDL_calloc(1, sizeof(SDL_WindowData));
    if (window_data == NULL) {
        return SDL_OutOfMemory();
    }

    nWindow = nwindowGetDefault();

    rc = nwindowSetDimensions(nWindow, window->w, window->h);
    if (R_FAILED(rc)) {
        return SDL_SetError("Could not set NWindow dimensions: 0x%x", rc);
    }

    window_data->egl_surface = SDL_EGL_CreateSurface(_this, window, nWindow);
    if (window_data->egl_surface == EGL_NO_SURFACE) {
        return SDL_SetError("Could not create GLES window surface");
    }

    /* Setup driver data for this window */
    window->internal = window_data;
    switch_window = window;

    /* starting operation mode */
    operationMode = appletGetOperationMode();

    /* One window, it always has focus */
    SDL_SetMouseFocus(window);
    SDL_SetKeyboardFocus(window);

    /* Window has been successfully created */
    return true;
}

static void SWITCH_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->internal;

    if (window == switch_window) {
        if (data != NULL) {
            if (data->egl_surface != EGL_NO_SURFACE) {
                SDL_EGL_MakeCurrent(_this, NULL, NULL);
                SDL_EGL_DestroySurface(_this, data->egl_surface);
            }

            SDL_free(window->internal);
            window->internal = NULL;
        }
        switch_window = NULL;
    }
}

static void SWITCH_SetWindowTitle(SDL_VideoDevice *_this, SDL_Window *window)
{
}

static bool SWITCH_SetWindowIcon(SDL_VideoDevice *_this, SDL_Window *window, SDL_Surface *icon)
{
    return true;
}

static bool SWITCH_SetWindowPosition(SDL_VideoDevice *_this, SDL_Window *window)
{
    return true;
}

static void SWITCH_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window)
{
    u32 w = 0, h = 0;
    SDL_WindowData *data = (SDL_WindowData *)window->internal;
    SDL_GLContext ctx = SDL_GL_GetCurrentContext();
    NWindow *nWindow = nwindowGetDefault();

    if (window->w != w || window->h != h) {
        if (data != NULL && data->egl_surface != EGL_NO_SURFACE) {
            SDL_EGL_MakeCurrent(_this, NULL, NULL);
            SDL_EGL_DestroySurface(_this, data->egl_surface);
            nwindowSetDimensions(nWindow, window->w, window->h);
            data->egl_surface = SDL_EGL_CreateSurface(_this, window, nWindow);
            SDL_EGL_MakeCurrent(_this, data->egl_surface, ctx);
        }
    }
}

static void SWITCH_ShowWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
}

static void SWITCH_HideWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
}

static void SWITCH_RaiseWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
}

static void SWITCH_MaximizeWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
}

static void SWITCH_MinimizeWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
}

static void SWITCH_RestoreWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
}

static void SWITCH_PumpEvents(SDL_VideoDevice *_this)
{
    AppletOperationMode om;
    Uint64 timestamp = SDL_GetTicksNS();

    if (!appletMainLoop()) {
        SDL_Event ev;
        ev.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&ev);
        return;
    }

    // we don't want other inputs overlapping with software keyboard
    if (!SDL_TextInputActive(switch_window)) {
        SWITCH_PollTouch(timestamp);
        SWITCH_PollKeyboard(timestamp);
        SWITCH_PollMouse(timestamp);
    }
    SWITCH_PollSwkb(timestamp);

    // handle docked / un-docked modes
    // note that SDL_WINDOW_RESIZABLE is only possible in windowed mode,
    // so we don't care about current fullscreen/windowed status
    if (switch_window != NULL && switch_window->flags & SDL_WINDOW_RESIZABLE) {
        om = appletGetOperationMode();
        if (om != operationMode) {
            operationMode = om;
            if (operationMode == AppletOperationMode_Handheld) {
                SDL_SetWindowSize(switch_window, 1280, 720);
            } else {
                SDL_SetWindowSize(switch_window, 1920, 1080);
            }
        }
    }
}

#endif /* SDL_VIDEO_DRIVER_SWITCH */
