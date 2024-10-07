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

#include "SDL_wiiukeyboard.h"

#include "../../SDL_internal.h"

#include "../../events/SDL_keyboard_c.h"

#include <nsyskbd/nsyskbd.h>

#define EVENT_BUFFER_SIZE 10

static struct WIIUKBD_EventBuffer {
	KBDKeyEvent events[EVENT_BUFFER_SIZE];
	int current;
} event_buffer = {0};

/* I'm not sure if this is really necessary, but I'm adding
 * it anyway in case the wii u calls back from another thread */
static SDL_mutex *event_buffer_mutex = NULL;

static void SDL_WIIUKeyboard_AttachCallback(KBDAttachEvent *kde)
{
	(void)kde;
}

static void SDL_WIIUKeyboard_DetachCallback(KBDAttachEvent *kde)
{
	(void)kde;
}

static void SDL_WIIUKeyboard_KeyCallback(KBDKeyEvent *e)
{
	SDL_LockMutex(event_buffer_mutex);

	/* add the key event to our buffer */
	if (event_buffer.current < EVENT_BUFFER_SIZE)
		event_buffer.events[event_buffer.current++] = *e;

	SDL_UnlockMutex(event_buffer_mutex);
}

static void WIIU_SendKeyEventText(KBDKeyEvent *e)
{
	const Uint16 symbol = e->asUTF16Character;
	unsigned char utf8[4] = {0};

	/* ignore private symbols, used for special keys */
	if ((symbol >= 0xE000 && symbol <= 0xF8FF) || symbol == 0xFFFF)
		return;

	/* convert UCS-2 to UTF-8 */
	if (symbol < 0x80) {
		utf8[0] = symbol;
	} else if (symbol < 0x800) {
		utf8[0] = 0xC0 | (symbol >> 6);
		utf8[1] = 0x80 | (symbol & 0x3F);
	} else {
		utf8[0] = 0xE0 |  (symbol >> 12);
		utf8[1] = 0x80 | ((symbol >> 6) & 0x3F);
		utf8[2] = 0x80 |  (symbol & 0x3F);
	}

	SDL_SendKeyboardText((char *)utf8);
}

void SDL_WIIU_PumpKeyboardEvents(_THIS)
{
	int i;

	SDL_LockMutex(event_buffer_mutex);

	/* process each key event */
	for (i = 0; i < event_buffer.current; i++) {
		SDL_SendKeyboardKey(
			event_buffer.events[i].isPressedDown ? SDL_PRESSED : SDL_RELEASED,
			(SDL_Scancode)event_buffer.events[i].hidCode
		);

		if (event_buffer.events[i].isPressedDown)
			WIIU_SendKeyEventText(&event_buffer.events[i]);
	}

	/* reset the buffer */
	event_buffer.current = 0;

	SDL_UnlockMutex(event_buffer_mutex);
}

/* returns non-zero on success */
int SDL_WIIU_InitKeyboard(_THIS)
{
	if (KBDSetup(SDL_WIIUKeyboard_AttachCallback, SDL_WIIUKeyboard_DetachCallback, SDL_WIIUKeyboard_KeyCallback))
		return 0;

	event_buffer_mutex = SDL_CreateMutex();
	if (!event_buffer_mutex) {
		KBDTeardown();
		return 0;
	}

	return 1;
}

/* returns non-zero on success; this should ONLY be called after a successful keyboard init */
int SDL_WIIU_QuitKeyboard(_THIS)
{
	if (KBDTeardown())
		return 0;

	/* by this point the mutex is definitely not locked */
	SDL_DestroyMutex(event_buffer_mutex);

	return 1;
}