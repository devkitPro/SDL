/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

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

#include "SDL_internal.h"

#ifdef SDL_PLATFORM_OGC

#include "../SDL_main_callbacks.h"

#include "../../video/ogc/SDL_ogcevents_c.h"

/* Standard includes */
#include <stdio.h>

/* OGC includes */
#include <fat.h>
#include <ogc/usbmouse.h>
#include <ogcsys.h>

static void ShutdownCB()
{
    OGC_PowerOffRequested = true;
}

static void ResetCB(u32, void *)
{
    OGC_ResetRequested = true;
}

int SDL_RunApp(int argc, char *argv[], SDL_main_func mainFunction, void * reserved)
{
    int result;

#ifdef __wii__
    u32 version;
    s32 preferred;

    L2Enhance();
    version = IOS_GetVersion();
    preferred = IOS_GetPreferredVersion();

    if (preferred > 0 && version != (u32)preferred)
        IOS_ReloadIOS(preferred);

    // Wii Power/Reset buttons
    SYS_SetPowerCallback(ShutdownCB);
    SYS_SetResetCallback(ResetCB);

    MOUSE_Init();
#endif

    fatInitDefault();

    /* Call the user's main function. Make sure that argv contains at least one
     * element. */
    if (!argv || argv[0] == NULL) {
        static const char *dummy_argv[2] = { "app", NULL };
        argc = 1;
        argv = (char**)dummy_argv;
    }

    result = SDL_CallMainFunction(argc, argv, mainFunction);
    return result;
}

#endif /* SDL_PLATFORM_OGC */
