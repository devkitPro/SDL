//
// Created by cpasjuste on 22/04/2020.
//

#ifndef SDL2_SDL_SWITCHSWKB_H
#define SDL2_SDL_SWITCHSWKB_H

#include "../../events/SDL_events_c.h"

extern void SWITCH_InitSwkb();
extern void SWITCH_PollSwkb();
extern void SWITCH_QuitSwkb();

extern SDL_bool SWITCH_HasScreenKeyboardSupport(_THIS);
extern SDL_bool SWITCH_IsScreenKeyboardShown(_THIS, SDL_Window * window);

extern void SWITCH_StartTextInput(_THIS);
extern void SWITCH_StopTextInput(_THIS);

#endif //SDL2_SDL_SWITCHSWKB_H
