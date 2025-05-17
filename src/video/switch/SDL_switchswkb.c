//
// Created by cpasjuste on 22/04/2020.
//

#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_SWITCH

#include <switch.h>
#include "SDL_switchswkb.h"

static SwkbdInline kbd;
static SwkbdAppearArg kbdAppearArg;
static bool kbdInited = SDL_FALSE;
static bool kbdShown = SDL_FALSE;

void
SWITCH_InitSwkb()
{
}

void
SWITCH_PollSwkb(void)
{
    if(kbdInited) {
        if(kbdShown) {
            swkbdInlineUpdate(&kbd, NULL);
        } else if(SDL_IsTextInputActive()) {
            SDL_StopTextInput();
        }
    }
}

void
SWITCH_QuitSwkb()
{
    if(kbdInited) {
        swkbdInlineClose(&kbd);
        kbdInited = false;
    }
}

SDL_bool
SWITCH_HasScreenKeyboardSupport(_THIS)
{
    return SDL_TRUE;
}

SDL_bool
SWITCH_IsScreenKeyboardShown(_THIS, SDL_Window *window)
{
    return kbdShown;
}

static void
SWITCH_EnterCb(const char *str, SwkbdDecidedEnterArg* arg)
{
    if(arg->stringLen > 0) {
        SDL_SendKeyboardText(str);
    }

    kbdShown = false;
}

static void
SWITCH_CancelCb(void)
{
    SDL_StopTextInput();
}

void
SWITCH_StartTextInput(_THIS)
{
    Result rc;

    if(!kbdInited) {
        rc = swkbdInlineCreate(&kbd);
        if (R_SUCCEEDED(rc)) {
            rc = swkbdInlineLaunchForLibraryApplet(&kbd, SwkbdInlineMode_AppletDisplay, 0);
            if(R_SUCCEEDED(rc)) {
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

    if(kbdInited) {
        swkbdInlineSetInputText(&kbd, "");
        swkbdInlineSetCursorPos(&kbd, 0);
        swkbdInlineUpdate(&kbd, NULL);
        swkbdInlineAppear(&kbd, &kbdAppearArg);
        kbdShown = true;
    }
}

void
SWITCH_StopTextInput(_THIS)
{
    if(kbdInited) {
        swkbdInlineDisappear(&kbd);
    }

    kbdShown = false;
}

#endif
