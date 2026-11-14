/** ****************************************************************************
 *
 * \file	input.c
 *
 * \brief	Timer, keyboard and mouse: the game's interrupt handlers and the
 * 			functions that read them.
 *
 * \details	Not ported yet: the joystick (port 201h; the port reports none),
 * 			demo playback (DS:0x610), the music and the PC speaker (Phase
 * 			5), and the timed event queue at DS:0x566.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <string.h>

#include "game/input.h"

#include "dos/dos.h"
#include "game/anim.h"
#include "game/globals.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define INPUT_BIOS_EVERY	 3	  /**< Countdown: BIOS tick every 4th tick. */
#define INPUT_QUIET_TICKS	 10	  /**< input_flush: ticks without a key. */
#define INPUT_RELEASE		 0x80 /**< Scan code bit of a release. */
#define INPUT_LAST_INDEX	 0x56 /**< Table index of higher scan codes. */
#define INPUT_MIN_CODE		 -30  /**< Lowest direction code. */

#define INPUT_HELD_KEY		 0x01 /**< globals_buttonsHeld: click key. */
#define INPUT_HELD_MOUSE	 0x04 /**< globals_buttonsHeld: mouse button. */
#define INPUT_SCAN_BACKSPACE 0x0e /**< Scan code of Backspace. */
#define INPUT_SCAN_COMMA	 0x33 /**< Scan code of the comma key. */
#define INPUT_SCAN_PERIOD	 0x34 /**< Scan code of the period key. */
#define INPUT_SCAN_SPACE	 0x39 /**< Scan code of the space bar. */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/*******************************************************************************
;
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

static int16_t input__sign(
	int value
);

static void input__directionKey(
	int8_t code,
	bool   released
);

static void input__commandKey(
	int8_t code,
	bool   released
);

static int16_t input__mouseStep(
	int16_t * pMickeys,
	int16_t	  shift
);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Set up the mouse, the timer and the keyboard handler.
 *
 * \details	Port of InitTimerAndInput (1000:1111). Not ported: the
 * 			joystick calibration (no joystick), the values it stores at
 * 			DS:0x8ef6..0x8efc and DS:0x79b0..0x79e0 (use unknown), the
 * 			critical-error handler (_harderr) and the speaker setup.
 *
 ******************************************************************************/
void input_init(void) {
	globals_joystickPresent = 0;

	bool useMouse = globals_demoMode == 0 && globals_mouseAllowed != 0;

	if (useMouse && input_mouseReset() == -1) {
		globals_mousePresent = 1;
		globals_mouseMickeysY = 0;
		globals_mouseMickeysX = 0;
		globals_mouseShiftX = 1;
		globals_mouseShiftY = 1;
	} else {
		globals_mousePresent = 0;
	}

	dos_setTimerDivisor(INPUT_TIMER_DIVISOR);
	dos_setTimerHandler(input_timerInterrupt);
	dos_setKeyboardHandler(input_keyboardInterrupt);
	globals_keyboardInstalled = 1;
	input_flush();
	globals_timerInstalled = 1;
}

/** ****************************************************************************
 *
 * \brief	Give the timer and the keyboard back to the BIOS.
 *
 * \details	Port of RestoreTimerAndKeyboard (1000:0fb7).
 *
 ******************************************************************************/
void input_shutdown(void) {
	dos_setTimerDivisor(0);
	dos_setTimerHandler(NULL);
	dos_setKeyboardHandler(NULL);
	globals_keyboardInstalled = 0;
	globals_timerInstalled = 0;
}

/** ****************************************************************************
 *
 * \brief	Timer interrupt handler: count a tick and move the cursor.
 *
 * \details	Port of TimerIsr (1000:1fe2), run 72.8 times a second. Every
 * 			4th tick the original also called the BIOS handler. While the
 * 			cursor is shown, held direction keys move it one pixel per
 * 			tick. Not ported: the speaker, demo playback, the music and
 * 			the joystick.
 *
 ******************************************************************************/
void input_timerInterrupt(void) {
	uint8_t countdown = globals_biosCountdown;

	globals_ticks++;
	globals_biosCountdown--;

	if (countdown == 0) {
		/* The original chained to the BIOS handler (time of day) here. */
		globals_biosCountdown = INPUT_BIOS_EVERY;
		globals_biosTicks++;
	}

	if (globals_cursorShown <= 0 || globals_timerBusy) {
		return;
	}

	globals_timerBusy = 1;

	int16_t stepColumns = 0;
	int16_t stepRows = 0;

	if (globals_keyboardBusy == 0) {
		stepColumns = input__sign(globals_keyDirectionX);
		stepRows = input__sign(globals_keyDirectionY);
	}

	if (stepColumns != 0 || stepRows != 0) {
		globals_cursorX += globals_cursorStepX[stepColumns + 1];
		globals_cursorY += globals_cursorStepY[stepRows + 1];
		globals_cursorMoved = 1;
	}

	globals_timerBusy = 0;
}

/** ****************************************************************************
 *
 * \brief	Keyboard interrupt handler: turn a scan code into game input.
 *
 * \param	scanCode		Byte from port 60h: scan code, plus 0x80 for a
 * 							release.
 *
 * \details	Port of KeyboardIsr (1000:2c42). The table globals_keyCodes
 * 			gives each scan code a key code: direction codes update the
 * 			held directions, the click codes latch clicks at the cursor,
 * 			0x1d toggles the sound and other codes become the last key.
 * 			During demo playback only Ctrl and M count, and Esc stops it.
 *
 ******************************************************************************/
void input_keyboardInterrupt(
	uint8_t scanCode
) {
	bool demoKey = scanCode == 0x1d || scanCode == 0x9d || scanCode == 0x32;

	if (globals_demoMode != 0 && !demoKey) {
		if (scanCode == 0x01) {
			globals_demoMode = GLOBALS_DEMO_STOPPING;
		}

		return;
	}

	/* 0xE0/0xE1 prefixes of AT keyboards and the overrun code 0xFF */
	if (scanCode >= 0xe0) {
		return;
	}

	bool released = (scanCode & INPUT_RELEASE) != 0;
	int	 index = scanCode & ~INPUT_RELEASE;

	if (index > INPUT_LAST_INDEX - 1) {
		index = INPUT_LAST_INDEX;
		released = false;
	}

	int8_t code = globals_keyCodes[index];

	if (code == 0) {
		return;
	}

	if (code < 0 && code >= INPUT_MIN_CODE) {
		input__directionKey(code, released);
	} else {
		input__commandKey(code, released);
	}
}

/** ****************************************************************************
 *
 * \brief	Read the last key code and clear it.
 *
 * \return	Key code from globals_keyCodes, or 0 when no key came.
 *
 * \details	Port of ReadKey (1000:2db6).
 *
 ******************************************************************************/
int8_t input_readKey(void) {
	int8_t key = globals_lastKey;

	globals_lastKey = 0;

	return key;
}

/** ****************************************************************************
 *
 * \brief	Drop pending input: wait until no key has come for 10 ticks,
 * 			then clear clicks and held directions.
 *
 * \details	Port of FlushInput (1000:0e24). It first empties the timed
 * 			event queue (anim_resetQueue). The deadline of event_wait
 * 			(globals_timeout) is cleared.
 *
 ******************************************************************************/
void input_flush(void) {
	anim_resetQueue();

	uint32_t quietUntil = globals_ticks + INPUT_QUIET_TICKS;

	while (globals_ticks < quietUntil) {
		int keys = 0;

		dos_service();

		while (input_readKey() != 0) {
			keys++;
		}

		if (keys != 0) {
			quietUntil = globals_ticks + INPUT_QUIET_TICKS;
		}
	}

	globals_timeout = 0;

	if (globals_demoMode == 0 && globals_mousePresent) {
		input_mousePoll();
		globals_mouseMickeysY = 0;
		globals_mouseMickeysX = 0;
	}

	globals_leftClick = 0;
	memset(globals_directionHeld, 0, sizeof globals_directionHeld);
	globals_lastDirectionY = 0;
	globals_lastDirectionX = 0;
	globals_keyDirectionY = 0;
	globals_keyDirectionX = 0;
	globals_buttonsHeld = 0;
}

/** ****************************************************************************
 *
 * \brief	Reset the mouse driver.
 *
 * \return	-1 when a mouse is installed, 0 when not.
 *
 * \details	Port of MouseReset (1f38:799a): INT 33h, AX = 0.
 *
 ******************************************************************************/
int16_t input_mouseReset(void) {
	return (int16_t)dos_mouseReset();
}

/** ****************************************************************************
 *
 * \brief	Read the mouse buttons into globals_mouseLeft and
 * 			globals_mouseRight.
 *
 * \details	Port of MouseReadButtons (1f38:79d7): INT 33h, AX = 3.
 *
 ******************************************************************************/
void input_mouseReadButtons(void) {
	uint16_t buttons = dos_mouseButtons();

	globals_mouseLeft = (int16_t)(buttons & 1);
	globals_mouseRight = (int16_t)(buttons & 2);
}

/** ****************************************************************************
 *
 * \brief	Read the mouse motion since the last call.
 *
 * \param	pHorizontal		Output: mickeys moved right.
 * \param	pVertical		Output: mickeys moved down.
 *
 * \details	Port of MouseReadMotion (1f38:7a14): INT 33h, AX = 0Bh.
 *
 ******************************************************************************/
void input_mouseReadMotion(
	int16_t * pHorizontal,
	int16_t * pVertical
) {
	dos_mouseMotion(pHorizontal, pVertical);
}

/** ****************************************************************************
 *
 * \brief	Turn mouse motion into cursor steps and latch left clicks.
 *
 * \details	Port of MousePoll (1f38:7a47). Motion collects in
 * 			globals_mouseMickeysX/Y until it makes at least one step;
 * 			the steps go to globals_mouseStepX/Y for the caller to add to
 * 			the cursor. A left button press latches a click at the
 * 			cursor unless another click source is held.
 *
 ******************************************************************************/
void input_mousePoll(void) {
	int16_t horizontal;
	int16_t vertical;

	input_mouseReadMotion(&horizontal, &vertical);
	globals_mouseMickeysX += horizontal;
	globals_mouseMickeysY += vertical;
	globals_mouseStepX =
		input__mouseStep(&globals_mouseMickeysX, globals_mouseShiftX);
	globals_mouseStepY =
		input__mouseStep(&globals_mouseMickeysY, globals_mouseShiftY);

	input_mouseReadButtons();

	if (globals_mouseLeft != 0) {
		if (globals_buttonsHeld == 0) {
			globals_leftClick = 1;
			globals_leftClickX = globals_cursorX;
			globals_leftClickY = globals_cursorY;
		}

		globals_buttonsHeld |= INPUT_HELD_MOUSE;
	} else {
		globals_buttonsHeld &= (uint8_t)~INPUT_HELD_MOUSE;
	}
}

/** ****************************************************************************
 *
 * \brief	Wait a number of ticks, or until the player continues.
 *
 * \param	ticks			Ticks to wait (72.8 per second).
 *
 * \details	Port of WaitTicksOrInput (1f38:0b51). Returns early on a
 * 			latched click (click key or mouse), the left mouse button,
 * 			Enter or Esc. M turns the music on or off and keeps waiting;
 * 			other keys are ignored. Not ported: the joystick button, and
 * 			starting or stopping the music itself (FUN_1000_0dd8 and
 * 			FUN_1000_0d99, Phase 5).
 *
 ******************************************************************************/
void input_waitTicks(
	int16_t ticks
) {
	/* The original sign-extends the count. */
	uint32_t until = globals_ticks + (uint32_t)(int32_t)ticks;

	for (;;) {
		dos_service();

		if (globals_ticks >= until) {
			return;
		}

		if (globals_leftClick != 0) {
			globals_leftClick = 0;
			return;
		}

		if (globals_mousePresent) {
			input_mouseReadButtons();

			if (globals_mouseLeft != 0) {
				return;
			}
		}

		int8_t key = input_readKey();

		if (key == 'M') {
			globals_musicOn = globals_musicOn == 0 ? 1 : 0;
		} else if (key == '\r' || key == 0x1b) {
			return;
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Sign of a number.
 *
 * \param	value			The number.
 *
 * \return	-1, 0 or 1.
 *
 ******************************************************************************/
static int16_t input__sign(
	int value
) {
	if (value > 0) {
		return 1;
	}

	return value < 0 ? -1 : 0;
}

/** ****************************************************************************
 *
 * \brief	Update the held directions for a direction key.
 *
 * \param	code			Direction code, -30..-1 (the table uses -16..-2).
 * \param	released		true when the key went up.
 *
 * \details	Part of KeyboardIsr (1000:2c42). A key adds its step to the
 * 			sum of held directions when it goes down and removes it when
 * 			it goes up; repeats of a held key are ignored.
 * 			globals_keyboardBusy tells the timer handler that the sums
 * 			are being changed.
 *
 ******************************************************************************/
static void input__directionKey(
	int8_t code,
	bool   released
) {
	if (code < -GLOBALS_DIRECTIONS) {
		/* No such codes in the table; the original state ends here. */
		return;
	}

	int		  held = GLOBALS_DIRECTIONS + code;
	int8_t	  stepColumns = globals_directionSteps[held];
	int8_t	  stepRows = globals_directionSteps[held + 1];
	uint8_t * pHeld = &globals_directionHeld[held];

	if (!released) {
		if (*pHeld != 0) {
			return;
		}

		*pHeld = 1;
		globals_keyboardBusy = -1;
		globals_keyDirectionX += stepColumns;
		globals_keyDirectionY += stepRows;
		globals_lastDirectionX = stepColumns;
		globals_lastDirectionY = stepRows;
		globals_keyboardBusy = 0;
	} else if (*pHeld != 0) {
		globals_keyboardBusy = -1;
		*pHeld = 0;
		globals_keyDirectionX -= stepColumns;
		globals_keyDirectionY -= stepRows;
		globals_keyboardBusy = 0;
	}
}

/** ****************************************************************************
 *
 * \brief	Handle a key that is not a direction.
 *
 * \param	code			Key code from globals_keyCodes.
 * \param	released		true when the key went up.
 *
 * \details	Part of KeyboardIsr (1000:2c42). The click key latches a left
 * 			click at the cursor (unless a click source is held) and marks
 * 			itself held until released; the right-click key latches a
 * 			right click; 0x1d toggles the sound. Other keys are stored
 * 			when they go down.
 *
 ******************************************************************************/
static void input__commandKey(
	int8_t code,
	bool   released
) {
	if (released) {
		if (code == GLOBALS_CLICK_KEY) {
			globals_keyboardBusy = -1;
			globals_buttonsHeld &= (uint8_t)~INPUT_HELD_KEY;
			globals_keyboardBusy = 0;
		}

		return;
	}

	switch (code) {
		case GLOBALS_CLICK_KEY:
			if (globals_buttonsHeld == 0) {
				globals_keyboardBusy = -1;
				globals_leftClick = GLOBALS_CLICK_KEY;
				globals_leftClickX = globals_cursorX;
				globals_leftClickY = globals_cursorY;
				globals_keyboardBusy = 0;
			}

			globals_buttonsHeld |= INPUT_HELD_KEY;
			globals_clickKeyFlag = 1;
			globals_clickKeyFlag2 = 1;
			break;

		case GLOBALS_RIGHT_KEY:
			if (globals_buttonsHeld == 0) {
				globals_keyboardBusy = -1;
				globals_rightClick = GLOBALS_RIGHT_KEY;
				globals_rightClickX = globals_cursorX;
				globals_rightClickY = globals_cursorY;
				globals_keyboardBusy = 0;
			}

			break;

		case GLOBALS_SOUND_KEY:
			globals_soundOn ^= 1;
			break;

		default:
			globals_lastKey = code;
			break;
	}
}

/** ****************************************************************************
 *
 * \brief	Turn collected mouse motion into cursor steps.
 *
 * \param	pMickeys		Collected motion; reset to 0 once it makes a step.
 * \param	shift			Mickeys per step as a power of two.
 *
 * \return	Steps, with the sign of the motion.
 *
 * \details	Part of MousePoll (1f38:7a47). The rest of the motion is
 * 			dropped once a step is made, as in the original.
 *
 ******************************************************************************/
static int16_t input__mouseStep(
	int16_t * pMickeys,
	int16_t	  shift
) {
	bool	negative = *pMickeys < 0;
	int16_t magnitude = (int16_t)(negative ? -*pMickeys : *pMickeys);
	int16_t steps = (int16_t)(magnitude >> shift);

	if (steps != 0) {
		*pMickeys = 0;
	}

	return negative ? (int16_t)-steps : steps;
}

/** ****************************************************************************
 *
 * \brief	Make the keys needed for typing produce characters.
 *
 * \param	numeric			0: the space bar types a space; otherwise it
 * 							stays the click key.
 *
 * \details	Port of BeginTextInput (1000:2c04): Backspace gives 8, the
 * 			comma and period keys their characters. The click key no
 * 			longer counts as held.
 *
 ******************************************************************************/
void input_beginTextInput(
	int16_t numeric
) {
	if (numeric == 0) {
		globals_keyCodes[INPUT_SCAN_SPACE] = ' ';
	}

	globals_keyCodes[INPUT_SCAN_BACKSPACE] = '\b';
	globals_keyCodes[INPUT_SCAN_COMMA] = ',';
	globals_keyCodes[INPUT_SCAN_PERIOD] = '.';
	globals_buttonsHeld &= (uint8_t)~INPUT_HELD_KEY;
}

/** ****************************************************************************
 *
 * \brief	Undo input_beginTextInput.
 *
 * \details	Port of EndTextInput (1000:2c2b): Backspace, comma and period
 * 			are ignored again and the space bar is the click key.
 *
 ******************************************************************************/
void input_endTextInput(void) {
	globals_keyCodes[INPUT_SCAN_BACKSPACE] = 0;
	globals_keyCodes[INPUT_SCAN_COMMA] = 0;
	globals_keyCodes[INPUT_SCAN_PERIOD] = 0;
	globals_keyCodes[INPUT_SCAN_SPACE] = GLOBALS_CLICK_KEY;
}
