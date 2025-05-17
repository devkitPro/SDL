//
// Created by cpasjuste on 22/04/2020.
//

#ifndef SDL2_SDL_SWITCHSWKB_H
#define SDL2_SDL_SWITCHSWKB_H

#include "../../events/SDL_events_c.h"

extern void SWITCH_InitSwkb(void);
extern void SWITCH_PollSwkb(Uint64 timestamp);
extern void SWITCH_QuitSwkb(void);

extern bool SWITCH_HasScreenKeyboardSupport(SDL_VideoDevice *_this);
extern bool SWITCH_IsScreenKeyboardShown(SDL_VideoDevice *_this, SDL_Window *window);

extern bool SWITCH_StartTextInput(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props);
extern bool SWITCH_StopTextInput(SDL_VideoDevice *_this, SDL_Window *window);

#endif // SDL2_SDL_SWITCHSWKB_H
