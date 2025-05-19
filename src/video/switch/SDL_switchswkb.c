//
// Created by cpasjuste on 22/04/2020.
//

#include "SDL_internal.h"

#if SDL_VIDEO_DRIVER_SWITCH

#include "SDL_switchswkb.h"
#include <switch.h>

static SwkbdInline kbd;
static SwkbdAppearArg kbdAppearArg;
static bool kbdInited = false;
static bool kbdShown = false;
static SDL_Window *current_window = NULL;

void SWITCH_InitSwkb(void)
{
}

void SWITCH_PollSwkb(Uint64 timestamp)
{
    if (kbdInited) {
        if (kbdShown) {
            swkbdInlineUpdate(&kbd, NULL);
        } else if (current_window != NULL && SDL_TextInputActive(current_window)) {
            SDL_StopTextInput(current_window);
        }
    }
}

void SWITCH_QuitSwkb(void)
{
    if (kbdInited) {
        swkbdInlineClose(&kbd);
        kbdInited = false;
    }
}

bool SWITCH_HasScreenKeyboardSupport(SDL_VideoDevice *_this)
{
    return true;
}

bool SWITCH_IsScreenKeyboardShown(SDL_VideoDevice *_this, SDL_Window *window)
{
    return kbdShown;
}

static void SWITCH_EnterCb(const char *str, SwkbdDecidedEnterArg *arg)
{
    if (arg->stringLen > 0) {
        SDL_SendKeyboardText(str);
    }

    kbdShown = false;
}

static void SWITCH_CancelCb(void)
{
    if (current_window != NULL) {
        SDL_StopTextInput(current_window);
    }
}

bool SWITCH_StartTextInput(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props)
{
    Result rc;

    if (!kbdInited) {
        rc = swkbdInlineCreate(&kbd);
        if (R_SUCCEEDED(rc)) {
            rc = swkbdInlineLaunchForLibraryApplet(&kbd, SwkbdInlineMode_AppletDisplay, 0);
            if (R_SUCCEEDED(rc)) {
                current_window = window;
                swkbdInlineSetDecidedEnterCallback(&kbd, SWITCH_EnterCb);
                swkbdInlineSetDecidedCancelCallback(&kbd, SWITCH_CancelCb);
                swkbdInlineMakeAppearArg(&kbdAppearArg, SwkbdType_Normal);
                swkbdInlineAppearArgSetOkButtonText(&kbdAppearArg, "Submit");
                kbdAppearArg.dicFlag = 1;
                kbdAppearArg.returnButtonFlag = 1;
                kbdInited = true;
            }
        }
    }

    if (kbdInited) {
        swkbdInlineSetInputText(&kbd, "");
        swkbdInlineSetCursorPos(&kbd, 0);
        swkbdInlineUpdate(&kbd, NULL);
        swkbdInlineAppear(&kbd, &kbdAppearArg);
        kbdShown = true;
    }

    return true;
}

bool SWITCH_StopTextInput(SDL_VideoDevice *_this, SDL_Window *window)
{
    if (kbdInited) {
        swkbdInlineDisappear(&kbd);
    }

    kbdShown = false;
    current_window = NULL;

    return true;
}

#endif
