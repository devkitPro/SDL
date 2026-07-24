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

#ifdef SDL_VIDEO_DRIVER_OGC

#include "SDL.h"
#include "../../events/SDL_keyboard_c.h"

#include "SDL_ogcosk.h"
#include "SDL_ogcgxcommon.h"
#include "SDL_ogcvideo.h"

/* The active Virtual Keyboard plugin (if any) */
const SDL_OGC_VkPlugin *ogc_vk_plugin = NULL;

static SDL_OGC_VkContext *ogc_vk_context = NULL;

static void init_context()
{
    if (ogc_vk_context) return;

    ogc_vk_context = SDL_calloc(1, sizeof(SDL_OGC_VkContext));
    if (!ogc_vk_context) {
        SDL_OutOfMemory();
        return;
    }

    ogc_vk_context->struct_size = sizeof(SDL_OGC_VkContext);
    if (ogc_vk_plugin) {
        ogc_vk_plugin->Init(ogc_vk_context);
    }
}

void OGC_StartTextInput(_THIS)
{
    if (!ogc_vk_plugin) return;
    ogc_vk_plugin->StartTextInput(ogc_vk_context);
}

void OGC_StopTextInput(_THIS)
{
    if (!ogc_vk_plugin) return;
    ogc_vk_plugin->StopTextInput(ogc_vk_context);
}

void OGC_SetTextInputRect(_THIS, const SDL_Rect *rect)
{
    if (!ogc_vk_plugin || !ogc_vk_context) {
        return;
    }

    ogc_vk_plugin->SetTextInputRect(ogc_vk_context, rect);
}

void OGC_ClearComposition(_THIS)
{
}

SDL_bool OGC_IsTextInputShown(_THIS)
{
    return ogc_vk_context && ogc_vk_context->is_open;
}


SDL_bool OGC_HasScreenKeyboardSupport(_THIS)
{
    return ogc_vk_plugin != NULL;
}

void OGC_ShowScreenKeyboard(_THIS, SDL_Window *window)
{
    if (!ogc_vk_plugin) return;

    ogc_vk_context->window = window;
    ogc_vk_plugin->ShowScreenKeyboard(ogc_vk_context);
}

void OGC_HideScreenKeyboard(_THIS, SDL_Window *window)
{
    if (!ogc_vk_plugin) return;
    ogc_vk_plugin->HideScreenKeyboard(ogc_vk_context);
}

SDL_bool OGC_IsScreenKeyboardShown(_THIS, SDL_Window *window)
{
    return OGC_IsTextInputShown(_this);
}

const SDL_OGC_VkPlugin *
SDL_OGC_RegisterVkPlugin(const SDL_OGC_VkPlugin *plugin)
{
    const SDL_OGC_VkPlugin *old_plugin = ogc_vk_plugin;
    ogc_vk_plugin = plugin;
    init_context();
    return old_plugin;
}

SDL_bool SDL_OGC_ProcessEvent(SDL_Event *event)
{
    if (!ogc_vk_plugin || !ogc_vk_context || !ogc_vk_context->is_open) {
        return SDL_FALSE;
    }

    return ogc_vk_plugin->ProcessEvent(ogc_vk_context, event);
}

SDL_bool OGC_keyboard_render(_THIS)
{
    if (!ogc_vk_plugin || !ogc_vk_context || !ogc_vk_context->is_open) {
        return SDL_FALSE;
    }

    ogc_vk_plugin->RenderKeyboard(ogc_vk_context);
    return SDL_TRUE;
}

int OGC_keyboard_get_pan_y(_THIS)
{
    if (!ogc_vk_plugin || !ogc_vk_context) {
        return 0;
    }

    return ogc_vk_context->screen_pan_y;
}

int SDL_OGC_SendKeyboardText(const char *text)
{
    return SDL_SendKeyboardText(text);
}

int SDL_OGC_SendVirtualKeyboardKey(Uint8 state, SDL_Scancode scancode)
{
    return SDL_SendVirtualKeyboardKey(state, scancode);
}

#endif /* SDL_VIDEO_DRIVER_OGC */

/* vi: set ts=4 sw=4 expandtab: */
