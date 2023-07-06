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

#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_SWITCH

#include "../SDL_sysvideo.h"
#include "../../render/SDL_sysrender.h"
#include "../../events/SDL_keyboard_c.h"
#include "../../events/SDL_mouse_c.h"
#include "../../events/SDL_windowevents_c.h"

#include "SDL_switchvideo.h"
#include "SDL_switchopengles.h"
#include "SDL_switchtouch.h"
#include "SDL_switchkeyboard.h"
#include "SDL_switchmouse_c.h"
#include "SDL_switchswkb.h"

/* Currently only one window */
static SDL_Window *switch_window = NULL;
static AppletOperationMode operationMode;

static void
SWITCH_Destroy(SDL_VideoDevice *device)
{
    if (device != NULL) {
        if(device->driverdata != NULL) {
            SDL_free(device->driverdata);
        }
        SDL_free(device);
    }
}

static SDL_VideoDevice *
SWITCH_CreateDevice(int devindex)
{
    SDL_VideoDevice *device;

    /* Initialize SDL_VideoDevice structure */
    device = (SDL_VideoDevice *) SDL_calloc(1, sizeof(SDL_VideoDevice));
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
    device->CreateSDLWindowFrom = SWITCH_CreateWindowFrom;
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
    //device->SetWindowMouseGrab = SWITCH_SetWindowGrab; // SDL 2.0.16
    //device->SetWindowKeyboardGrab = SWITCH_SetWindowGrab; // SDL 2.0.16
    device->DestroyWindow = SWITCH_DestroyWindow;

    device->GL_LoadLibrary = SWITCH_GLES_LoadLibrary;
    device->GL_GetProcAddress = SWITCH_GLES_GetProcAddress;
    device->GL_UnloadLibrary = SWITCH_GLES_UnloadLibrary;
    device->GL_CreateContext = SWITCH_GLES_CreateContext;
    device->GL_MakeCurrent = SWITCH_GLES_MakeCurrent;
    device->GL_SetSwapInterval = SWITCH_GLES_SetSwapInterval;
    device->GL_GetSwapInterval = SWITCH_GLES_GetSwapInterval;
    device->GL_SwapWindow = SWITCH_GLES_SwapWindow;
    device->GL_DeleteContext = SWITCH_GLES_DeleteContext;
    device->GL_DefaultProfileConfig = SWITCH_GLES_DefaultProfileConfig;

    device->StartTextInput = SWITCH_StartTextInput;
    device->StopTextInput = SWITCH_StopTextInput;
    device->HasScreenKeyboardSupport = SWITCH_HasScreenKeyboardSupport;
    device->IsScreenKeyboardShown = SWITCH_IsScreenKeyboardShown;

    device->PumpEvents = SWITCH_PumpEvents;

    return device;
}

VideoBootStrap SWITCH_bootstrap = {
    "Switch",
    "Nintendo Switch Video Driver",
    SWITCH_CreateDevice
};

/*****************************************************************************/
/* SDL Video and Display initialization/handling functions                   */
/*****************************************************************************/
int
SWITCH_VideoInit(_THIS)
{
    SDL_VideoDisplay display;
    SDL_DisplayMode current_mode;

    SDL_zero(current_mode);
    current_mode.w = 1920;
    current_mode.h = 1080;
    current_mode.refresh_rate = 60;
    current_mode.format = SDL_PIXELFORMAT_RGBA8888;
    current_mode.driverdata = NULL;

    SDL_zero(display);
    display.desktop_mode = current_mode;
    display.current_mode = current_mode;
    display.driverdata = NULL;
    SDL_AddVideoDisplay(&display, SDL_FALSE);

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

    return 0;
}

void
SWITCH_VideoQuit(_THIS)
{
    // this should not be needed if user code is right (SDL_GL_LoadLibrary/SDL_GL_UnloadLibrary calls match)
    // this (user) error doesn't have the same effect on switch thought, as the driver needs to be unloaded (crash)
    if(_this->gl_config.driver_loaded > 0) {
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

void
SWITCH_GetDisplayModes(_THIS, SDL_VideoDisplay *display)
{
    SDL_DisplayMode mode;

    // 1920x1080 RGBA8888, default mode
    SDL_AddDisplayMode(display, &display->current_mode);

    // 1280x720 RGBA8888
    SDL_zero(mode);
    mode.w = 1280;
    mode.h = 720;
    mode.refresh_rate = 60;
    mode.format = SDL_PIXELFORMAT_RGBA8888;
    SDL_AddDisplayMode(display, &mode);
}

int
SWITCH_SetDisplayMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode)
{
    SDL_WindowData *data = (SDL_WindowData *) SDL_GetFocusWindow()->driverdata;
    SDL_GLContext ctx = SDL_GL_GetCurrentContext();
    NWindow *nWindow = nwindowGetDefault();

    if (data != NULL && data->egl_surface != EGL_NO_SURFACE) {
        SDL_EGL_MakeCurrent(_this, NULL, NULL);
        SDL_EGL_DestroySurface(_this, data->egl_surface);
        nwindowSetDimensions(nWindow, mode->w, mode->h);
        data->egl_surface = SDL_EGL_CreateSurface(_this, nWindow);
        SDL_EGL_MakeCurrent(_this, data->egl_surface, ctx);
    }

    return 0;
}

int
SWITCH_CreateWindow(_THIS, SDL_Window *window)
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

    window_data = (SDL_WindowData *) SDL_calloc(1, sizeof(SDL_WindowData));
    if (window_data == NULL) {
        return SDL_OutOfMemory();
    }

    nWindow = nwindowGetDefault();

    rc = nwindowSetDimensions(nWindow, window->w, window->h);
    if (R_FAILED(rc)) {
        return SDL_SetError("Could not set NWindow dimensions: 0x%x", rc);
    }

    window_data->egl_surface = SDL_EGL_CreateSurface(_this, nWindow);
    if (window_data->egl_surface == EGL_NO_SURFACE) {
        return SDL_SetError("Could not create GLES window surface");
    }

    /* Setup driver data for this window */
    window->driverdata = window_data;
    switch_window = window;

    /* starting operation mode */
    operationMode = appletGetOperationMode();

    /* One window, it always has focus */
    SDL_SetMouseFocus(window);
    SDL_SetKeyboardFocus(window);

    /* Window has been successfully created */
    return 0;
}

void
SWITCH_DestroyWindow(_THIS, SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *) window->driverdata;

    if (window == switch_window) {
        if (data != NULL) {
            if (data->egl_surface != EGL_NO_SURFACE) {
                SDL_EGL_MakeCurrent(_this, NULL, NULL);
                SDL_EGL_DestroySurface(_this, data->egl_surface);
            }
            if(window->driverdata != NULL) {
                SDL_free(window->driverdata);
                window->driverdata = NULL;
            }
        }
        switch_window = NULL;
    }
}

int
SWITCH_CreateWindowFrom(_THIS, SDL_Window *window, const void *data)
{
    return -1;
}
void
SWITCH_SetWindowTitle(_THIS, SDL_Window *window)
{
}
void
SWITCH_SetWindowIcon(_THIS, SDL_Window *window, SDL_Surface *icon)
{
}
void
SWITCH_SetWindowPosition(_THIS, SDL_Window *window)
{
}
void
SWITCH_SetWindowSize(_THIS, SDL_Window *window)
{
    u32 w = 0, h = 0;
    SDL_WindowData *data = (SDL_WindowData *) window->driverdata;
    SDL_GLContext ctx = SDL_GL_GetCurrentContext();
    NWindow *nWindow = nwindowGetDefault();

    if(window->w != w || window->h != h) {
        if (data != NULL && data->egl_surface != EGL_NO_SURFACE) {
            SDL_EGL_MakeCurrent(_this, NULL, NULL);
            SDL_EGL_DestroySurface(_this, data->egl_surface);
            nwindowSetDimensions(nWindow, window->w, window->h);
            data->egl_surface = SDL_EGL_CreateSurface(_this, nWindow);
            SDL_EGL_MakeCurrent(_this, data->egl_surface, ctx);
        }
    }
}
void
SWITCH_ShowWindow(_THIS, SDL_Window *window)
{
}
void
SWITCH_HideWindow(_THIS, SDL_Window *window)
{
}
void
SWITCH_RaiseWindow(_THIS, SDL_Window *window)
{
}
void
SWITCH_MaximizeWindow(_THIS, SDL_Window *window)
{
}
void
SWITCH_MinimizeWindow(_THIS, SDL_Window *window)
{
}
void
SWITCH_RestoreWindow(_THIS, SDL_Window *window)
{
}
void
SWITCH_SetWindowGrab(_THIS, SDL_Window *window, SDL_bool grabbed)
{
}

void
SWITCH_PumpEvents(_THIS)
{
    AppletOperationMode om;

    if (!appletMainLoop()) {
        SDL_Event ev;
        ev.type = SDL_QUIT;
        SDL_PushEvent(&ev);
        return;
    }

    // we don't want other inputs overlapping with software keyboard
    if(!SDL_IsTextInputActive()) {
        SWITCH_PollTouch();
        SWITCH_PollKeyboard();
        SWITCH_PollMouse();
    }
    SWITCH_PollSwkb();

    // handle docked / un-docked modes
    // note that SDL_WINDOW_RESIZABLE is only possible in windowed mode,
    // so we don't care about current fullscreen/windowed status
    if(switch_window != NULL && switch_window->flags & SDL_WINDOW_RESIZABLE) {
        om = appletGetOperationMode();
        if(om != operationMode) {
            operationMode = om;
            if(operationMode == AppletOperationMode_Handheld) {
                SDL_SetWindowSize(switch_window, 1280, 720);
            } else {
                SDL_SetWindowSize(switch_window, 1920, 1080);
            }
        }
    }
}

#endif /* SDL_VIDEO_DRIVER_SWITCH */