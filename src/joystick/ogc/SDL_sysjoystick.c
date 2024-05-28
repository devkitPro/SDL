/*
 SDL - Simple DirectMedia Layer
 Copyright (C) 1997-2006 Sam Lantinga

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

 Sam Lantinga
 slouken@libsdl.org
 */
#include "SDL_config.h"

#ifdef SDL_JOYSTICK_OGC

#include "SDL_events.h"
#include "SDL_joystick.h"
#include "../SDL_sysjoystick.h"
#include "../SDL_joystick_c.h"

#include <gccore.h>
#include <unistd.h>
#include <wiiuse/wpad.h>
#include <math.h>

#define MAX_GC_JOYSTICKS	4
#define MAX_WII_JOYSTICKS	4

#ifdef __wii__
#define MAX_JOYSTICKS		(MAX_GC_JOYSTICKS + MAX_WII_JOYSTICKS)
#else
#define MAX_JOYSTICKS		MAX_GC_JOYSTICKS
#endif

#define MAX_GC_AXES			6
#define MAX_GC_BUTTONS		8
#define	MAX_GC_HATS			1

#define MAX_WII_AXES		9
#define MAX_WII_BUTTONS		15
#define	MAX_WII_HATS		1

#define	JOYNAMELEN			10

#define AXIS_MIN	-32767  /* minimum value for axis coordinate */
#define AXIS_MAX	32767   /* maximum value for axis coordinate */

extern SDL_Joystick **SDL_joysticks;

typedef struct joystick_paddata_t
{
	u16 prev_buttons;
	s8 stickX;
	s8 stickY;
	s8 substickX;
	s8 substickY;
	u8 triggerL;
	u8 triggerR;
}joystick_paddata;

typedef struct joystick_wpaddata_t
{
	u32 prev_buttons;
	u32 exp;
	s16 nunchuk_stickX;
	s16 nunchuk_stickY;
	s16 classicL_stickX;
	s16 classicL_stickY;
	s16 classicR_stickX;
	s16 classicR_stickY;
	u8 classic_triggerL;
	u8 classic_triggerR;
	u8 classic_calibrated;
	s8 wiimote_pitch;
	s8 wiimote_roll;
	s8 wiimote_yaw;
	s16 classic_cal[4][3]; // 4x axes, min/center/max
}joystick_wpaddata;

static const u16 sdl_buttons_gc[] =
{
	PAD_BUTTON_A,
	PAD_BUTTON_B,
	PAD_BUTTON_X,
	PAD_BUTTON_Y,
	PAD_TRIGGER_L,
	PAD_TRIGGER_R,
	PAD_TRIGGER_Z,
	PAD_BUTTON_START,
};

/* The private structure used to keep track of a joystick */
typedef struct joystick_hwdata_t
{
	int index;
	int type;
#ifdef __wii__
	union
	{
		joystick_paddata gamecube;
		joystick_wpaddata wiimote;
	};
#else
	joystick_paddata gamecube;
#endif
} joystick_hwdata;

static const int __jspad_enabled = 1;
static const int __numgcjoysticks = 4;

#ifdef __wii__
static const int __jswpad_enabled = 1;
static const int __numwiijoysticks = 4;
static int split_joysticks = 0;
static int num_wii_joysticks;

static const u32 sdl_buttons_wii[] =
{
	WPAD_BUTTON_A|WPAD_CLASSIC_BUTTON_A,
	WPAD_BUTTON_B|WPAD_CLASSIC_BUTTON_B,
	WPAD_BUTTON_1,
	WPAD_BUTTON_2,
	WPAD_BUTTON_MINUS|WPAD_CLASSIC_BUTTON_MINUS,
	WPAD_BUTTON_PLUS|WPAD_CLASSIC_BUTTON_PLUS,
	WPAD_BUTTON_HOME|WPAD_CLASSIC_BUTTON_HOME,
	WPAD_NUNCHUK_BUTTON_Z, /* 7 */
	WPAD_NUNCHUK_BUTTON_C, /* 8 */
	WPAD_CLASSIC_BUTTON_X, /* 9 */
	WPAD_CLASSIC_BUTTON_Y,
	WPAD_CLASSIC_BUTTON_FULL_L,
	WPAD_CLASSIC_BUTTON_FULL_R,
	WPAD_CLASSIC_BUTTON_ZL,
	WPAD_CLASSIC_BUTTON_ZR
};
#define SDL_WII_NUM_BUTTONS_WII \
	(sizeof(sdl_buttons_wii) / sizeof(sdl_buttons_wii[0]))

static const u32 sdl_buttons_wiimote[] =
{
	WPAD_BUTTON_A,
	WPAD_BUTTON_B,
	WPAD_BUTTON_1,
	WPAD_BUTTON_2,
	WPAD_BUTTON_MINUS,
	WPAD_BUTTON_PLUS,
	WPAD_BUTTON_HOME,
};
#define SDL_WII_NUM_BUTTONS_WIIMOTE \
	(sizeof(sdl_buttons_wiimote) / sizeof(sdl_buttons_wiimote[0]))

static const u32 sdl_buttons_nunchuck[] =
{
	WPAD_NUNCHUK_BUTTON_Z,
	WPAD_NUNCHUK_BUTTON_C,
};
#define SDL_WII_NUM_BUTTONS_NUNCHUCK \
	(sizeof(sdl_buttons_nunchuck) / sizeof(sdl_buttons_nunchuck[0]))

static const u32 sdl_buttons_classic[] =
{
	WPAD_CLASSIC_BUTTON_A,
	WPAD_CLASSIC_BUTTON_B,
	WPAD_CLASSIC_BUTTON_X,
	WPAD_CLASSIC_BUTTON_Y,
	WPAD_CLASSIC_BUTTON_FULL_L,
	WPAD_CLASSIC_BUTTON_FULL_R,
	WPAD_CLASSIC_BUTTON_ZL,
	WPAD_CLASSIC_BUTTON_ZR,
	WPAD_CLASSIC_BUTTON_MINUS,
	WPAD_CLASSIC_BUTTON_PLUS,
	WPAD_CLASSIC_BUTTON_HOME,
};
#define SDL_WII_NUM_BUTTONS_CLASSIC \
	(sizeof(sdl_buttons_classic) / sizeof(sdl_buttons_classic[0]))

/* Helpers to separate nunchuk vs classic buttons which share the
 * same scan codes. In particular, up on the classic controller is
 * the same as Z on the nunchuk. The numbers refer to the sdl_buttons_wii
 * list above. */
static int wii_button_is_nunchuk(int idx)
{
	return idx == 7 || idx == 8;
}

static int wii_button_is_classic(int idx)
{
	return idx >= 9;
}

static s16 WPAD_Orient(WPADData *data, int motion)
{
	float out;

	if (motion == 0)
		out = data->orient.pitch;
	else if (motion == 1)
		out = data->orient.roll;
	else
		out = data->orient.yaw;

	return (s16)((out / 180.0) * 128.0);
}

static s16 WPAD_Pitch(WPADData *data)
{
	return WPAD_Orient(data, 0);
}

static s16 WPAD_Roll(WPADData *data)
{
	return WPAD_Orient(data, 1);
}

static s16 WPAD_Yaw(WPADData *data)
{
	return WPAD_Orient(data, 2);
}

static s16 WPAD_Stick(s16 x, s16 min, s16 center, s16 max, int flip)
{
	s16 d;
	int ret;

	x -= center;

	if (x < 0)
		d = center - min;
	else
		d = max - center;

	if (center - min < 5) return 0;
	if (max - center < 5) return 0;

	if (d)
		ret = (x << 15) / d;
	else
		return 0;

	if (flip)
		ret = -ret;

	if (ret < AXIS_MIN)
		ret = AXIS_MIN;
	else if (ret > AXIS_MAX)
		ret = AXIS_MAX;

	return ret;
}

static const u32 _buttons[8] = {
	// wiimote
	WPAD_BUTTON_UP,
	WPAD_BUTTON_DOWN,
	WPAD_BUTTON_LEFT,
	WPAD_BUTTON_RIGHT,
	// classic
	WPAD_CLASSIC_BUTTON_UP,
	WPAD_CLASSIC_BUTTON_DOWN,
	WPAD_CLASSIC_BUTTON_LEFT,
	WPAD_CLASSIC_BUTTON_RIGHT
};

static void HandleWiiHats(SDL_Joystick *joystick,
                          const u32 changed, const u32 pressed,
                          const u32 *buttons)
{
	if (changed & (buttons[0]|buttons[1]|buttons[2]|buttons[3]))
	{
		int hat = SDL_HAT_CENTERED;

		if (pressed & buttons[0]) hat |= SDL_HAT_UP;
		if (pressed & buttons[1]) hat |= SDL_HAT_DOWN;
		if (pressed & buttons[2]) hat |= SDL_HAT_LEFT;
		if (pressed & buttons[3]) hat |= SDL_HAT_RIGHT;
		SDL_PrivateJoystickHat(joystick, 0, hat);
	}
}

static void HandleWiiButtons(SDL_Joystick *joystick,
                             const u32 changed,
                             const WPADData *data,
                             const u32 *buttons,
                             size_t num_buttons)
{
	for (int i = 0; i < num_buttons; i++)
	{
		if (changed & buttons[i])
		{
			if (!split_joysticks &&
				((data->exp.type == WPAD_EXP_CLASSIC && wii_button_is_nunchuk(i)) ||
				(data->exp.type == WPAD_EXP_NUNCHUK && wii_button_is_classic(i))))
				continue;

			SDL_PrivateJoystickButton(joystick, i,
				(data->btns_d & buttons[i]) ? SDL_PRESSED : SDL_RELEASED);
		}
	}
}

static void HandleWiiMotion(SDL_Joystick* joystick,
							joystick_hwdata *prev_state,
							WPADData *data,
							int start_index)
{
	int axis = WPAD_Pitch(data);
	if(prev_state->wiimote.wiimote_pitch != axis)
	{
		SDL_PrivateJoystickAxis(joystick, start_index, -(axis << 8));
		prev_state->wiimote.wiimote_pitch = axis;
	}
	axis = WPAD_Roll(data);
	if(prev_state->wiimote.wiimote_roll != axis)
	{
		SDL_PrivateJoystickAxis(joystick, start_index + 1, axis << 8);
		prev_state->wiimote.wiimote_roll = axis;
	}
	axis = WPAD_Yaw(data);
	if(prev_state->wiimote.wiimote_yaw != axis)
	{
		SDL_PrivateJoystickAxis(joystick, start_index + 2, axis << 8);
		prev_state->wiimote.wiimote_yaw = axis;
	}
}

static void _HandleWiiJoystickUpdate(SDL_Joystick* joystick)
{
	u32 changed, pressed;
	int axis, index;
	joystick_hwdata *prev_state;
	WPADData *data;
	SDL_Joystick *joy_wiimote = NULL;
	SDL_Joystick *joy_expansion = NULL;

	prev_state = (joystick_hwdata*)joystick->hwdata;
	index = prev_state->index;
	if (!WPAD_ReadPending(index, NULL))
		return;
	data = WPAD_Data(index);
	changed = data->btns_d | data->btns_u;
	pressed = data->btns_d | data->btns_h;

	if (joystick->index >= __numwiijoysticks) {
		/* we can get here if using the split joystick feature. But even in
		 * that case, we don't want to do anything here: the update operation
		 * on the wiimote must update the expansion joystick too. */
		return;
	}

	if (split_joysticks) {
		/* To find out the pointer to the linked joystick we use the knowledge
		 * that the SDL_Joystick structures are allocated in a continuous array
		 */
		joy_wiimote = joystick;
		joy_expansion = SDL_joysticks[joystick->index + 4];
	}

	if (split_joysticks) {
		HandleWiiHats(joy_wiimote, changed, pressed, _buttons);
		if (data->exp.type == WPAD_EXP_CLASSIC) { // classic controller
			HandleWiiHats(joy_expansion, changed, pressed, _buttons + 4);
		}
	} else {
		if (data->exp.type == WPAD_EXP_CLASSIC) // classic controller
			HandleWiiHats(joystick, changed, pressed, _buttons + 4);
		HandleWiiHats(joystick, changed, pressed, _buttons);
	}

	if (split_joysticks) {
		HandleWiiButtons(joy_wiimote, changed, data,
						 sdl_buttons_wiimote, SDL_WII_NUM_BUTTONS_WIIMOTE);
		if (data->exp.type == WPAD_EXP_CLASSIC) {
			HandleWiiButtons(joy_expansion, changed, data,
							 sdl_buttons_classic, SDL_WII_NUM_BUTTONS_CLASSIC);
		} else if (data->exp.type == WPAD_EXP_NUNCHUK) {
			HandleWiiButtons(joy_expansion, changed, data,
							 sdl_buttons_nunchuck, SDL_WII_NUM_BUTTONS_NUNCHUCK);
		}
	} else {
		HandleWiiButtons(joystick, changed, data,
						 sdl_buttons_wii, SDL_WII_NUM_BUTTONS_WII);
	}

	if(data->exp.type == WPAD_EXP_CLASSIC)
	{
		if (prev_state->wiimote.exp != WPAD_EXP_CLASSIC)
		{
			prev_state->wiimote.classic_calibrated = 0;
			prev_state->wiimote.classic_cal[0][0] = 5;  // left x min
			prev_state->wiimote.classic_cal[0][2] = 59; // left x max
			prev_state->wiimote.classic_cal[1][0] = 5; // left y min
			prev_state->wiimote.classic_cal[1][2] = 59;  // left y max
			prev_state->wiimote.classic_cal[2][0] = 5;  // right x min
			prev_state->wiimote.classic_cal[2][2] = 27; // right x max
			prev_state->wiimote.classic_cal[3][0] = 5; // right y min
			prev_state->wiimote.classic_cal[3][2] = 27;  // right y max

		}

		// max/min checking
		// left stick x
		if (data->exp.classic.ljs.pos.x < prev_state->wiimote.classic_cal[0][0])
			prev_state->wiimote.classic_cal[0][0] = data->exp.classic.ljs.pos.x;
		else if (data->exp.classic.ljs.pos.x > prev_state->wiimote.classic_cal[0][2])
			prev_state->wiimote.classic_cal[0][2] = data->exp.classic.ljs.pos.x;
		// left stick y
		if (data->exp.classic.ljs.pos.y < prev_state->wiimote.classic_cal[1][0])
			prev_state->wiimote.classic_cal[1][0] = data->exp.classic.ljs.pos.y;
		else if (data->exp.classic.ljs.pos.y > prev_state->wiimote.classic_cal[1][2])
			prev_state->wiimote.classic_cal[1][2] = data->exp.classic.ljs.pos.y;
		// right stick x
		if (data->exp.classic.rjs.pos.x < prev_state->wiimote.classic_cal[2][0])
			prev_state->wiimote.classic_cal[2][0] = data->exp.classic.rjs.pos.x;
		else if (data->exp.classic.rjs.pos.x > prev_state->wiimote.classic_cal[2][2])
			prev_state->wiimote.classic_cal[2][2] = data->exp.classic.rjs.pos.x;
		// right stick y
		if (data->exp.classic.rjs.pos.y < prev_state->wiimote.classic_cal[3][0])
			prev_state->wiimote.classic_cal[3][0] = data->exp.classic.rjs.pos.y;
		else if (data->exp.classic.rjs.pos.y > prev_state->wiimote.classic_cal[3][2])
			prev_state->wiimote.classic_cal[3][2] = data->exp.classic.rjs.pos.y;

		// calibrate center positions
		if (prev_state->wiimote.classic_calibrated < 5)
		{
			prev_state->wiimote.classic_cal[0][1] = data->exp.classic.ljs.pos.x;
			prev_state->wiimote.classic_cal[1][1] = data->exp.classic.ljs.pos.y;
			prev_state->wiimote.classic_cal[2][1] = data->exp.classic.rjs.pos.x;
			prev_state->wiimote.classic_cal[3][1] = data->exp.classic.rjs.pos.y;
			// this is zero if the expansion hasn't finished initializing
			if (data->exp.classic.ljs.max.x)
				prev_state->wiimote.classic_calibrated++;
		}
	}

	if (data->exp.type != prev_state->wiimote.exp)
	{
		SDL_Joystick *joy = split_joysticks ? joy_expansion : joystick;
		// Reset the expansion axes
		for (int i = 0; i < 6; i++)
			SDL_PrivateJoystickAxis(joy, i, 0);
	}

	if(data->exp.type == WPAD_EXP_CLASSIC)
	{
		SDL_Joystick *joy = split_joysticks ? joy_expansion : joystick;

		axis = WPAD_Stick(data->exp.classic.ljs.pos.x, prev_state->wiimote.classic_cal[0][0],
			prev_state->wiimote.classic_cal[0][1], prev_state->wiimote.classic_cal[0][2], 0);
		if(prev_state->wiimote.classicL_stickX != axis)
		{
			SDL_PrivateJoystickAxis(joy, 0, axis);
			prev_state->wiimote.classicL_stickX = axis;
		}
		// y axes are reversed
		axis = WPAD_Stick(data->exp.classic.ljs.pos.y, prev_state->wiimote.classic_cal[1][0],
			prev_state->wiimote.classic_cal[1][1], prev_state->wiimote.classic_cal[1][2], 1);
		if(prev_state->wiimote.classicL_stickY != axis)
		{
			SDL_PrivateJoystickAxis(joy, 1, axis);
			prev_state->wiimote.classicL_stickY = axis;
		}
		axis = WPAD_Stick(data->exp.classic.rjs.pos.x, prev_state->wiimote.classic_cal[2][0],
			prev_state->wiimote.classic_cal[2][1], prev_state->wiimote.classic_cal[2][2], 0);
		if(prev_state->wiimote.classicR_stickX != axis)
		{
			SDL_PrivateJoystickAxis(joy, 2, axis);
			prev_state->wiimote.classicR_stickX = axis;
		}
		axis = WPAD_Stick(data->exp.classic.rjs.pos.y, prev_state->wiimote.classic_cal[3][0],
			prev_state->wiimote.classic_cal[3][1], prev_state->wiimote.classic_cal[3][2], 1);
		if(prev_state->wiimote.classicR_stickY != axis)
		{
			SDL_PrivateJoystickAxis(joy, 3, axis);
			prev_state->wiimote.classicR_stickY = axis;
		}
		axis = data->exp.classic.r_shoulder;
		if(prev_state->wiimote.classic_triggerR != axis)
		{
			SDL_PrivateJoystickAxis(joy, 4, axis<<8);
			prev_state->wiimote.classic_triggerR = axis;
		}
		axis = data->exp.classic.l_shoulder;
		if(prev_state->wiimote.classic_triggerL != axis)
		{
			SDL_PrivateJoystickAxis(joy, 5, axis<<8);
			prev_state->wiimote.classic_triggerL = axis;
		}
	}
	else if(data->exp.type == WPAD_EXP_NUNCHUK)
	{
		SDL_Joystick *joy = split_joysticks ? joy_expansion : joystick;

		axis = WPAD_Stick(data->exp.nunchuk.js.pos.x, data->exp.nunchuk.js.min.x,
			data->exp.nunchuk.js.center.x, data->exp.nunchuk.js.max.x, 0);
		if(prev_state->wiimote.nunchuk_stickX != axis)
		{
			SDL_PrivateJoystickAxis(joy, 0, axis);
			prev_state->wiimote.nunchuk_stickX = axis;
		}
		axis = WPAD_Stick(data->exp.nunchuk.js.pos.y, data->exp.nunchuk.js.min.y,
			data->exp.nunchuk.js.center.y, data->exp.nunchuk.js.max.y, 1);
		if(prev_state->wiimote.nunchuk_stickY != axis)
		{
			SDL_PrivateJoystickAxis(joy, 1, axis);
			prev_state->wiimote.nunchuk_stickY = axis;
		}
	}

	prev_state->wiimote.exp = data->exp.type;

	if (split_joysticks) {
		// also update the expansion
		((joystick_hwdata*)joy_expansion->hwdata)->wiimote.exp = data->exp.type;
	}

	int start_index = split_joysticks ? 0 : 6;
	HandleWiiMotion(split_joysticks ? joy_wiimote : joystick, prev_state, data, start_index);
}

#endif

/* Function to scan the system for joysticks.
 * This function should return the number of available
 * joysticks.  Joystick 0 should be the system default joystick.
 * It should return -1 on an unrecoverable fatal error.
 */
int SDL_SYS_JoystickInit(void)
{
#ifdef __wii__
	const char *split_joystick_env = getenv("SDL_WII_JOYSTICK_SPLIT");
	split_joysticks = split_joystick_env && strcmp(split_joystick_env, "1") == 0;
	/* Multiply by two because each wiimote might have an expansion connected */
	num_wii_joysticks = split_joysticks ? __numwiijoysticks * 2 : __numwiijoysticks;
	return num_wii_joysticks + __numgcjoysticks;
#else
	return MAX_JOYSTICKS;
#endif
}

static char joy_name[64] = "Gamecube 0";

/* Function to get the device-dependent name of a joystick */
const char *SDL_SYS_JoystickName(int index)
{
	if(index>=0) {
#ifdef __wii__
		if((__jswpad_enabled) && (index < num_wii_joysticks)) {
			joystick_hwdata *hwdata = (joystick_hwdata*)SDL_joysticks[index]->hwdata;
			char *name_ptr = joy_name;
			if (index < __numwiijoysticks) {
				name_ptr += sprintf(name_ptr, "Wiimote %d", index);
				if (!split_joysticks) {
				    // Add expansion information
					switch (hwdata->wiimote.exp) {
					case WPAD_EXP_NUNCHUK:
						strcpy(name_ptr, " + Nunchuk"); break;
					case WPAD_EXP_CLASSIC:
						strcpy(name_ptr, " + Classic"); break;
					case WPAD_EXP_GUITARHERO3:
						strcpy(name_ptr, " + Guitar Hero 3"); break;
					case WPAD_EXP_WIIBOARD:
						strcpy(name_ptr, " + Balance board"); break;
					}
				}
			} else {
				/* This is an expansion and we are using the split controllers
				 * option: show only the expansion name, then. */
				int idx = index - __numwiijoysticks;
				switch (hwdata->wiimote.exp) {
				case WPAD_EXP_NUNCHUK:
					sprintf(name_ptr, "Nunchuk %d", idx); break;
				case WPAD_EXP_CLASSIC:
					sprintf(name_ptr, "Classic %d", idx); break;
				case WPAD_EXP_GUITARHERO3:
					sprintf(name_ptr, "Guitar Hero 3 %d", idx); break;
				case WPAD_EXP_WIIBOARD:
					sprintf(name_ptr, "Balance board %d", idx); break;
				case WPAD_EXP_NONE:
					strcpy(name_ptr, "Disconnected"); break;
				default:
					sprintf(name_ptr, "Unknown %d", idx); break;
				}
			}
		} else if((__jspad_enabled) && (index < (num_wii_joysticks + __numgcjoysticks)))
#endif
			sprintf(joy_name, "Gamecube %d", index);
	}
	return (const char *)joy_name;
}

/* Function to open a joystick for use.
 The joystick to open is specified by the index field of the joystick.
 This should fill the nbuttons and naxes fields of the joystick structure.
 It returns 0, or -1 if there is an error.
 */
int SDL_SYS_JoystickOpen(SDL_Joystick *joystick)
{
	/* allocate memory for system specific hardware data */
	joystick->hwdata = SDL_malloc(sizeof(joystick_hwdata));
	if (joystick->hwdata == NULL)
	{
		SDL_OutOfMemory();
		return(-1);
	}
	SDL_memset(joystick->hwdata, 0, sizeof(joystick_hwdata));
#ifdef __wii__
	if((joystick->index < num_wii_joysticks) && (__jswpad_enabled))
	{
		int index = (joystick->index > __numwiijoysticks) ? (joystick->index - __numwiijoysticks) : joystick->index;
		((joystick_hwdata*)(joystick->hwdata))->index = index;
		((joystick_hwdata*)(joystick->hwdata))->type = 0;
		if (split_joysticks) {
			if (joystick->index < __numwiijoysticks) {
				// wiimote
				joystick->nbuttons = SDL_WII_NUM_BUTTONS_WIIMOTE;
				joystick->naxes = 3;
				joystick->nhats = 1;
			} else {
				// expansion
				joystick->nbuttons = SDL_max(SDL_WII_NUM_BUTTONS_NUNCHUCK,
				                             SDL_WII_NUM_BUTTONS_CLASSIC);
				joystick->naxes = 6;
				joystick->nhats = 1;
			}
		} else {
			joystick->nbuttons = MAX_WII_BUTTONS;
			joystick->naxes = MAX_WII_AXES;
			joystick->nhats = MAX_WII_HATS;
		}
	}
	else if((joystick->index < num_wii_joysticks + __numgcjoysticks) && (__jspad_enabled))
	{
		((joystick_hwdata*)(joystick->hwdata))->index = joystick->index - num_wii_joysticks;
		((joystick_hwdata*)(joystick->hwdata))->type = 1;
		joystick->nbuttons = MAX_GC_BUTTONS;
		joystick->naxes = MAX_GC_AXES;
		joystick->nhats = MAX_GC_HATS;
	}
#else
	if((joystick->index < 4) && (__jspad_enabled))
	{
		if(joystick->index < (__numgcjoysticks))
		{
			((joystick_hwdata*)(joystick->hwdata))->index = joystick->index - 4;
			((joystick_hwdata*)(joystick->hwdata))->type = 1;
			joystick->nbuttons = MAX_GC_BUTTONS;
			joystick->naxes = MAX_GC_AXES;
			joystick->nhats = MAX_GC_HATS;
		}
	}
#endif
	return(0);
}


static void _HandleGCJoystickUpdate(SDL_Joystick* joystick)
{
	u16 buttons, prev_buttons, changed;
	int i;
	int axis;
	joystick_hwdata *prev_state;
	int index = joystick->index - num_wii_joysticks;

	buttons = PAD_ButtonsHeld(index);
	prev_state = (joystick_hwdata *)joystick->hwdata;
	prev_buttons = prev_state->gamecube.prev_buttons;
	changed = buttons ^ prev_buttons;

	if(changed & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT | PAD_BUTTON_DOWN | PAD_BUTTON_UP))
	{
		int hat = SDL_HAT_CENTERED;
		if(buttons & PAD_BUTTON_UP) hat |= SDL_HAT_UP;
		if(buttons & PAD_BUTTON_DOWN) hat |= SDL_HAT_DOWN;
		if(buttons & PAD_BUTTON_LEFT) hat |= SDL_HAT_LEFT;
		if(buttons & PAD_BUTTON_RIGHT) hat |= SDL_HAT_RIGHT;
		SDL_PrivateJoystickHat(joystick, 0, hat);
	}

	for(i = 0; i < (sizeof(sdl_buttons_gc) / sizeof(sdl_buttons_gc[0])); i++)
	{
		if (changed & sdl_buttons_gc[i])
			SDL_PrivateJoystickButton(joystick, i,
				(buttons & sdl_buttons_gc[i]) ? SDL_PRESSED : SDL_RELEASED);
	}
	prev_state->gamecube.prev_buttons = buttons;
	axis = PAD_StickX(index);
	if(prev_state->gamecube.stickX != axis)
	{
		SDL_PrivateJoystickAxis(joystick, 0, axis << 8);
		prev_state->gamecube.stickX = axis;
	}

	axis = PAD_StickY(index);
	if(prev_state->gamecube.stickY != axis)
	{
		SDL_PrivateJoystickAxis(joystick, 1, (-axis) << 8);
		prev_state->gamecube.stickY = axis;
	}

	axis = PAD_SubStickX(index);
	if(prev_state->gamecube.substickX != axis)
	{
		SDL_PrivateJoystickAxis(joystick, 2, axis << 8);
		prev_state->gamecube.substickX = axis;
	}

	axis = PAD_SubStickY(index);
	if(prev_state->gamecube.substickY != axis)
	{
		SDL_PrivateJoystickAxis(joystick, 3, (-axis) << 8);
		prev_state->gamecube.substickY = axis;
	}

	axis = PAD_TriggerL(index);
	if(prev_state->gamecube.triggerL != axis)
	{
		SDL_PrivateJoystickAxis(joystick, 4, axis << 7);
		prev_state->gamecube.triggerL = axis;
	}

	axis = PAD_TriggerR(index);
	if(prev_state->gamecube.triggerR != axis)
	{
		SDL_PrivateJoystickAxis(joystick, 5, axis << 7);
		prev_state->gamecube.triggerR = axis;
	}
}

/* Function to update the state of a joystick - called as a device poll.
 * This function shouldn't update the joystick structure directly,
 * but instead should call SDL_PrivateJoystick*() to deliver events
 * and update joystick device state.
 */

void SDL_SYS_JoystickUpdate(SDL_Joystick *joystick)
{
	if(!joystick || !joystick->hwdata)
		return;

	PAD_ScanPads();

	switch(((joystick_hwdata*)(joystick->hwdata))->type)
	{
#ifdef __wii__
		case 0:
			if(__jswpad_enabled) {
				if (split_joysticks && joystick->index >= __numwiijoysticks) {
					/* the actual joystick we want to update is always the wiimote
					 * one */
					joystick = SDL_joysticks[joystick->index - __numwiijoysticks];
				}
				_HandleWiiJoystickUpdate(joystick);
            }
			break;
#endif
		case 1:
			if(__jspad_enabled)
				_HandleGCJoystickUpdate(joystick);
			break;
		default:
			break;
	}
}

/* Function to close a joystick after use */
void SDL_SYS_JoystickClose(SDL_Joystick *joystick)
{
	if(!joystick || !joystick->hwdata) // joystick already closed
		return;

	SDL_free(joystick->hwdata);
	joystick->hwdata = NULL;
}

/* Function to perform any system-specific joystick related cleanup */
void SDL_SYS_JoystickQuit(void)
{

}

#endif /* SDL_JOYSTICK_DC */
