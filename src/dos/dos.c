/** ****************************************************************************
 *
 * \file	dos.c
 *
 * \brief	Replacements for the DOS, BIOS and hardware services the game
 * 			uses, built on the platform layer.
 *
 * \details	See dos.h for how interrupt handlers run in the port.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <string.h>

#include "dos/dos.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

/**
 * Most timer ticks run in one dos_service call. After a longer stall (a
 * debugger, a dragged window) the rest are dropped instead of running the
 * game fast to catch up.
 */
#define DOS_MAX_CATCH_UP 73

#define DOS_FRAME_MS	 16	  /**< Shortest time between two presents. */
#define DOS_RELEASE		 0x80 /**< Added to a scan code for a release. */
#define DOS_MICKEYS		 2	  /**< Mickeys per game pixel, both axes. */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/**
 * A row of keys and the scan code of its first key.
 */
typedef struct dos__key_row_tag {
	const char * pKeys;		/**< The keys, unshifted, left to right. */
	uint8_t		 firstCode; /**< Scan code of pKeys[0]. */
} dos__KeyRow;

/**
 * A non-printable key and its scan code.
 */
typedef struct dos__special_key_tag {
	int		key;	  /**< PLAT_KEY_* code or ASCII control code. */
	uint8_t scanCode; /**< Scan code set 1. */
} dos__SpecialKey;

/*******************************************************************************
;
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

static uint8_t dos__scanCode(
	int key
);

static void dos__runKeyboard(void);

static void dos__runTimer(void);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

static dos_TimerHandler	   dos__timerHandler;	 /**< INT 8, or NULL. */
static dos_KeyboardHandler dos__keyboardHandler; /**< INT 9, or NULL. */
static dos_QuitHandler	   dos__quitHandler;	 /**< Window closed. */

static uint32_t dos__timerSeen;	   /**< Last tick handled. */
static uint32_t dos__lastPresent;  /**< plat_ticksMs of the last present. */
static int		dos__mouseLeft;	   /**< Pointer column at the last read. */
static int		dos__mouseTop;	   /**< Pointer row at the last read. */
static int		dos__hostLeft;	   /**< Host pointer column seen last. */
static int		dos__hostTop;	   /**< Host pointer row seen last. */
static unsigned dos__mouseButtons; /**< Clicks seen by dos_mouseMotion. */

static dos_QuitHandler dos__serviceHook; /**< Test hook, or NULL. */

/**
 * Printable keys of a US keyboard by row (scan code set 1).
 */
static const dos__KeyRow dos__keyRows[] = {
	{ "1234567890-=", 0x02 },
	{ "qwertyuiop[]", 0x10 },
	{ "asdfghjkl;'`", 0x1e },
	{ "\\zxcvbnm,./", 0x2b },
	{ " ",			0x39 },
};

/**
 * Other keys. Cursor keys use the keypad codes: the extra cursor keys of
 * an AT keyboard send 0xE0 first, which the game's handler ignores.
 */
static const dos__SpecialKey dos__specialKeys[] = {
	{ PLAT_KEY_ESCAPE,	   0x01 },
	{ PLAT_KEY_BACKSPACE,	  0x0e },
	{ PLAT_KEY_TAB,			0x0f },
	{ PLAT_KEY_ENTER,		  0x1c },
	{ PLAT_KEY_CONTROL,		0x1d },
	{ PLAT_KEY_HOME,		 0x47 },
	{ PLAT_KEY_UP,		   0x48 },
	{ PLAT_KEY_PAGE_UP,		0x49 },
	{ PLAT_KEY_KEYPAD_MINUS, 0x4a },
	{ PLAT_KEY_LEFT,		 0x4b },
	{ PLAT_KEY_RIGHT,		  0x4d },
	{ PLAT_KEY_KEYPAD_PLUS,	0x4e },
	{ PLAT_KEY_END,			0x4f },
	{ PLAT_KEY_DOWN,		 0x50 },
	{ PLAT_KEY_PAGE_DOWN,	  0x51 },
	{ PLAT_KEY_INSERT,	   0x52 },
	{ PLAT_KEY_DELETE,	   0x53 },
};

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Program the timer chip's channel 0.
 *
 * \param	divisor			Divisor of the 1193182 Hz input clock; 0 means
 * 							65536 (18.2 Hz, the BIOS rate).
 *
 * \details	Replaces out(0x43, 0x36), out(0x40, low), out(0x40, high).
 * 			Ticks that were due at the old rate are dropped.
 *
 ******************************************************************************/
void dos_setTimerDivisor(
	uint16_t divisor
) {
	uint32_t fullDivisor = divisor ? divisor : 65536;

	plat_timerSetRate(DOS_PIT_HZ, fullDivisor);
	dos__timerSeen = plat_timerTicks();
}

/** ****************************************************************************
 *
 * \brief	Install a timer interrupt handler (INT 8).
 *
 * \param	pHandler		Called once per timer tick; NULL restores the
 * 							BIOS handler, which only counted time.
 *
 * \details	Replaces _dos_setvect(8, ...).
 *
 ******************************************************************************/
void dos_setTimerHandler(
	dos_TimerHandler pHandler
) {
	dos__timerHandler = pHandler;
}

/** ****************************************************************************
 *
 * \brief	Install a keyboard interrupt handler (INT 9).
 *
 * \param	pHandler		Called once per scan code; NULL restores the BIOS
 * 							handler (keys are dropped).
 *
 * \details	Replaces the INT 21h functions 25h and 35h for vector 9.
 *
 ******************************************************************************/
void dos_setKeyboardHandler(
	dos_KeyboardHandler pHandler
) {
	dos__keyboardHandler = pHandler;
}

/** ****************************************************************************
 *
 * \brief	Set what happens when the user closes the window.
 *
 * \param	pHandler		Function that ends the program, or NULL to ignore
 * 							closing the window.
 *
 ******************************************************************************/
void dos_setQuitHandler(
	dos_QuitHandler pHandler
) {
	dos__quitHandler = pHandler;
}

/** ****************************************************************************
 *
 * \brief	Let the hardware run: handle input, timer ticks and the
 * 			display.
 *
 * \details	Call this in every loop that waited for an interrupt handler
 * 			or the hardware. It runs the keyboard handler for each key
 * 			event, the timer handler for each tick since the last call,
 * 			and shows page 0 about 60 times a second. When there is
 * 			nothing to show it sleeps for a millisecond, so waiting loops
 * 			do not use a whole processor core.
 *
 ******************************************************************************/
void dos_service(void) {
	if (!plat_poll() && dos__quitHandler) {
		dos__quitHandler();
	}

	dos__runKeyboard();
	dos__runTimer();

	if (dos__serviceHook) {
		dos__serviceHook();
	}

	uint32_t now = plat_ticksMs();

	if (now - dos__lastPresent >= DOS_FRAME_MS) {
		dos__lastPresent = now;
		plat_present();
	} else {
		plat_delayMs(1);
	}
}

/** ****************************************************************************
 *
 * \brief	Reset the mouse driver (INT 33h, AX = 0).
 *
 * \return	0xFFFF: a mouse is installed (the platform always has a
 * 			pointer).
 *
 ******************************************************************************/
uint16_t dos_mouseReset(void) {
	unsigned buttons;

	plat_getPointer(&dos__mouseLeft, &dos__mouseTop, &buttons);
	dos__hostLeft = dos__mouseLeft;
	dos__hostTop = dos__mouseTop;
	dos__mouseButtons = 0;

	return 0xffff;
}

/** ****************************************************************************
 *
 * \brief	Read the mouse buttons (INT 33h, AX = 3, result in BX).
 *
 * \return	Bit 0: left button, bit 1: right button.
 *
 * \details	A click that came and went since the last read is reported
 * 			as held once, so short clicks are not lost between polls.
 *
 ******************************************************************************/
uint16_t dos_mouseButtons(void) {
	int		 left;
	int		 top;
	unsigned buttons;

	plat_getPointer(&left, &top, &buttons);
	buttons |= dos__mouseButtons;
	dos__mouseButtons = 0;

	uint16_t result = 0;

	if (buttons & PLAT_BUTTON_LEFT) {
		result |= 1;
	}

	if (buttons & PLAT_BUTTON_RIGHT) {
		result |= 2;
	}

	return result;
}

/** ****************************************************************************
 *
 * \brief	Read the mouse motion (INT 33h, AX = 0Bh, results in CX, DX).
 *
 * \param	pHorizontal		Output: mickeys moved right since the last call.
 * \param	pVertical		Output: mickeys moved down since the last call.
 *
 * \details	The driver's default ratio in the 320x200 modes is 2 mickeys
 * 			per screen pixel on both axes, so the game's cursor (mickeys
 * 			/ 2) follows the pointer exactly.
 *
 ******************************************************************************/
void dos_mouseMotion(
	int16_t * pHorizontal,
	int16_t * pVertical
) {
	int		 left;
	int		 top;
	unsigned buttons;

	plat_getPointer(&left, &top, &buttons);

	/* Keep clicks for the next dos_mouseButtons. */
	dos__mouseButtons |= buttons;

	if (left == dos__hostLeft && top == dos__hostTop) {
		/* The host pointer rests; keys may have moved the cursor. */
		*pHorizontal = 0;
		*pVertical = 0;

		return;
	}

	dos__hostLeft = left;
	dos__hostTop = top;
	*pHorizontal = (int16_t)((left - dos__mouseLeft) * DOS_MICKEYS);
	*pVertical = (int16_t)((top - dos__mouseTop) * DOS_MICKEYS);
	dos__mouseLeft = left;
	dos__mouseTop = top;
}

/** ****************************************************************************
 *
 * \brief	Scan code of a key.
 *
 * \param	key				PLAT_KEY_* or ASCII code.
 *
 * \return	Scan code set 1 (US layout), or 0 for keys without one.
 *
 ******************************************************************************/
static uint8_t dos__scanCode(
	int key
) {
	size_t rowCount = sizeof dos__keyRows / sizeof dos__keyRows[0];
	size_t specialCount = sizeof dos__specialKeys / sizeof dos__specialKeys[0];

	if (key > 0 && key < 128) {
		for (size_t row = 0; row < rowCount; row++) {
			const char * pFound = strchr(dos__keyRows[row].pKeys, key);

			if (pFound) {
				size_t position = (size_t)(pFound - dos__keyRows[row].pKeys);

				return (uint8_t)(dos__keyRows[row].firstCode + position);
			}
		}
	}

	if (key >= PLAT_KEY_F1 && key <= PLAT_KEY_F10) {
		return (uint8_t)(0x3b + (key - PLAT_KEY_F1));
	}

	for (size_t index = 0; index < specialCount; index++) {
		if (dos__specialKeys[index].key == key) {
			return dos__specialKeys[index].scanCode;
		}
	}

	return 0;
}

/** ****************************************************************************
 *
 * \brief	Run the keyboard handler for each key event waiting.
 *
 ******************************************************************************/
static void dos__runKeyboard(void) {
	int	 key;
	bool pressed;

	while (plat_getKeyEvent(&key, &pressed)) {
		uint8_t scanCode = dos__scanCode(key);

		if (scanCode == 0 || !dos__keyboardHandler) {
			continue;
		}

		if (!pressed) {
			scanCode |= DOS_RELEASE;
		}

		dos__keyboardHandler(scanCode);
	}
}

/** ****************************************************************************
 *
 * \brief	Run the timer handler once for each tick since the last call.
 *
 ******************************************************************************/
static void dos__runTimer(void) {
	uint32_t now = plat_timerTicks();

	if (now - dos__timerSeen > DOS_MAX_CATCH_UP) {
		dos__timerSeen = now - DOS_MAX_CATCH_UP;
	}

	while (dos__timerSeen != now) {
		dos__timerSeen++;

		if (dos__timerHandler) {
			dos__timerHandler();
		}
	}
}

/** ****************************************************************************
 *
 * \brief	End the program as the game does when it exits to DOS.
 *
 * \details	Calls the handler set with dos_setQuitHandler, which does not
 * 			return. Without a handler nothing happens.
 *
 ******************************************************************************/
void dos_exit(void) {
	if (dos__quitHandler) {
		dos__quitHandler();
	}
}

/** ****************************************************************************
 *
 * \brief	Tell the mouse driver where the game shows its cursor
 * 			(INT 33h, AX = 4).
 *
 * \param	left			Cursor column in game pixels.
 * \param	top				Cursor row in game pixels.
 *
 * \details	The game never called this function: its cursor was the only
 * 			pointer on the screen. The port calls it so that the next
 * 			motion report leads the game's cursor to the host pointer,
 * 			wherever the two were before.
 *
 ******************************************************************************/
void dos_mouseSetPosition(
	int16_t left,
	int16_t top
) {
	dos__mouseLeft = left;
	dos__mouseTop = top;
}

/** ****************************************************************************
 *
 * \brief	Set a function that runs on every dos_service call.
 *
 * \param	pHook			The function, or NULL for none.
 *
 * \details	For the port's test options (screenshots and clicks at a given
 * 			time); the game does not use it.
 *
 ******************************************************************************/
void dos_setServiceHook(
	dos_QuitHandler pHook
) {
	dos__serviceHook = pHook;
}

/** ****************************************************************************
 *
 * \brief	Press and release a key as if the user had.
 *
 * \param	key				PLAT_KEY_* or ASCII code of the key.
 *
 * \details	For the port's test options; the game does not use it. The
 * 			keyboard handler gets the key's scan code and its release.
 *
 ******************************************************************************/
void dos_pressKey(
	int key
) {
	uint8_t scanCode = dos__scanCode(key);

	if (scanCode != 0 && dos__keyboardHandler) {
		dos__keyboardHandler(scanCode);
		dos__keyboardHandler((uint8_t)(scanCode | DOS_RELEASE));
	}
}
