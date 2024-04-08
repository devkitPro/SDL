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

#ifdef __wii__

#include "SDL_main.h"

#include "../../video/ogc/SDL_ogcevents_c.h"

#ifdef main
#undef main
#endif

/* Standard includes */
#include <stdio.h>

/* OGC includes */
#include <fat.h>
#include <ogc/usbmouse.h>
#include <ogcsys.h>
#include <wiikeyboard/keyboard.h>
#include <wiiuse/wpad.h>

static void ShutdownCB()
{
    OGC_PowerOffRequested = true;
}

static void ResetCB()
{
    OGC_ResetRequested = true;
}

int main(int argc, char *argv[])
{
    u32 version;
    s32 preferred;

    L2Enhance();
    version = IOS_GetVersion();
    preferred = IOS_GetPreferredVersion();

    if (preferred > 0 && version != (u32)preferred)
        IOS_ReloadIOS(preferred);

    // Wii Power/Reset buttons
    WPAD_Init();
    WPAD_SetPowerButtonCallback((WPADShutdownCallback)ShutdownCB);
    SYS_SetPowerCallback(ShutdownCB);
    SYS_SetResetCallback(ResetCB);
    // TODO OGC_InitVideoSystem();
    WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
    WPAD_SetVRes(WPAD_CHAN_ALL, 640, 480);

    MOUSE_Init();
    KEYBOARD_Init(NULL);
    fatInitDefault();

    /* Call the user's main function. Make sure that argv contains at least one
     * element. */
    if (!argv || argv[0] == NULL) {
        static const char *dummy_argv[2] = { "app", NULL };
        argc = 1;
        argv = (char**)dummy_argv;
    }
    return SDL_main(argc, argv);
}

#endif /* __wii__ */

/* vi: set sts=4 ts=4 sw=4 expandtab: */
