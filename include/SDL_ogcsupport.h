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

#ifndef SDL_ogcsupport_h_
#define SDL_ogcsupport_h_

/**
 *  \file SDL_ogcsupport.h
 *
 *  Header for the Wii/GameCube support routines.
 */

#include "SDL_stdinc.h"
#include "SDL_events.h"

#include "begin_code.h"
/* Set up for C function definitions, even when using C++ */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct SDL_OGC_DriverData SDL_OGC_DriverData;

typedef struct SDL_OGC_VkContext
{
    size_t struct_size;

    SDL_OGC_DriverData *driverdata;

    SDL_bool is_open;
    SDL_Window *window;
    SDL_Rect input_rect;
    int screen_pan_y;
} SDL_OGC_VkContext;

typedef struct SDL_OGC_VkPlugin
{
    size_t struct_size;

    void (*Init)(SDL_OGC_VkContext *context);
    void (*RenderKeyboard)(SDL_OGC_VkContext *context);
    SDL_bool (*ProcessEvent)(SDL_OGC_VkContext *context, SDL_Event *event);
    void (*StartTextInput)(SDL_OGC_VkContext *context);
    void (*StopTextInput)(SDL_OGC_VkContext *context);
    void (*SetTextInputRect)(SDL_OGC_VkContext *context, const SDL_Rect *rect);
    void (*ShowScreenKeyboard)(SDL_OGC_VkContext *context);
    void (*HideScreenKeyboard)(SDL_OGC_VkContext *context);
} SDL_OGC_VkPlugin;

extern DECLSPEC const SDL_OGC_VkPlugin *
SDL_OGC_RegisterVkPlugin(const SDL_OGC_VkPlugin *plugin);

/**
 * Processes the given \a event: if a virtual keyboard is active, input events
 * will be passed over to the keyboard module and should not be processed by
 * the application.
 *
 * \param event The SDL_Event to be processed
 * \returns SDL_TRUE if the event was processed and must be ignored by the
 * application.
 */
extern DECLSPEC SDL_bool SDL_OGC_ProcessEvent(SDL_Event *event);

/**
 * A SDL_PollEvent() wrapper which invokes SDL_OGC_ProcessEvent() for every
 * received event.
 */
SDL_FORCE_INLINE int SDL_OGC_PollEvent(SDL_Event *event)
{
    while (SDL_PollEvent(event)) {
        if (!SDL_OGC_ProcessEvent(event)) {
            return 1;
        }
    }
    return 0;
}

/* Should we add some preprocessor conditions to this? */
#ifndef SDL_PollEvent
#define SDL_PollEvent SDL_OGC_PollEvent
#endif

/* Functions for OSK plugin implementations */
extern DECLSPEC int SDL_OGC_SendKeyboardText(const char *text);
extern DECLSPEC int SDL_OGC_SendVirtualKeyboardKey(Uint8 state,
                                                   SDL_Scancode scancode);

/* Ends C function definitions when using C++ */
#ifdef __cplusplus
}
#endif
#include "close_code.h"

#endif /* SDL_ogcsupport_h_ */

/* vi: set ts=4 sw=4 expandtab: */
