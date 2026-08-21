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

#ifndef SDL_ogcevents_c_h_
#define SDL_ogcevents_c_h_

#include "SDL_internal.h"

#include "SDL_ogcvideo.h"
#include "../../joystick/SDL_sysjoystick.h"

#include <embedded-game-controller/egc.h>

#ifdef __wii__
/* 4 GameCube controllers + 4 Wiimotes + possibly 4 separate expansions. In
 * theory there could be even more controller, connected via USB and bluetooth,
 * but let's be realistic :-)
 */
#define OGC_MAX_CONTROLLERS 12
#else
#define OGC_MAX_CONTROLLERS 4
#endif

typedef struct {
    egc_input_device_t *egc_device;
    SDL_JoystickID instance_id;
    u32 btns_prev;
    u32 btns_pressed;
    u32 btns_held;
    u32 btns_released;
} _OGC_Controller;

extern int OGC_NumControllers;

extern bool OGC_ResetRequested;
extern bool OGC_PowerOffRequested;

extern void OGC_PumpEvents(SDL_VideoDevice *_this);

void OGC_device_added_cb(egc_input_device_t *device, void *userdata);
void OGC_device_removed_cb(egc_input_device_t *device, void *userdata);
_OGC_Controller *OGC_get_controller(int i);

typedef void (*_OGC_ControllerCb)(_OGC_Controller *controller);
void OGC_register_joystick_callbacks(_OGC_ControllerCb added_cb,
                                     _OGC_ControllerCb removed_cb);

#endif /* SDL_ogcevents_c_h_ */
