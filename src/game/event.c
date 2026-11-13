/** ****************************************************************************
 *
 * \file	event.c
 *
 * \brief	The game's main input loop: waits for keys, clicks on hotspots
 * 			and timeouts, and moves the cursor.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/event.h"

#include <stdbool.h>
#include <string.h>

#include "dos/dos.h"
#include "game/cursor.h"
#include "game/exe.h"
#include "game/globals.h"
#include "game/input.h"
#include "game/ui.h"
#include "game/video.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define EVENT_KEY_TABLE	   0x579f /**< DS offset: key map of each screen. */
#define EVENT_MUSIC_KEY	   0x57f3 /**< DS offset: pointer to the M key pair. */
#define EVENT_MAX_KEYS	   0x1e	  /**< Key pairs of a screen at most, + 1. */
#define EVENT_KEY_OFFSET   0x32	  /**< Added to negative key codes. */
#define EVENT_MUSIC_RESULT -0x28  /**< Key result that toggles the music. */
#define EVENT_QUIT_KEY	   0x7f	  /**< Key code that ends the game. */
#define EVENT_FIRST_LETTER 0x20	  /**< Lowest key code of a character. */
#define EVENT_CYCLE_MASK   0x0f	  /**< Palette cycle: every 16th tick. */
#define EVENT_CYCLE_COLORS 0x698  /**< DS offset: the 3 cycling colors. */
#define EVENT_CYCLE_FIRST  6	  /**< First palette entry that cycles. */
#define EVENT_NO_HOTSPOT   32000  /**< globals_hotId before a test. */

#define EVENT_SCREEN_PARTS 0x16	  /**< Screen with its own status text. */
#define EVENT_SCREEN_SHOP  0x25	  /**< Screen with its own status text. */
#define EVENT_EMPTY_TEXTS  0x68c  /**< DS offset: six empty texts. */
#define EVENT_STATUS_TEXT  0x7f32 /**< DS offset: status text of a screen. */
#define EVENT_STATUS_LEFT  0x51ee /**< DS offset: its left edge. */
#define EVENT_STATUS_WIDTH 0x8bc0 /**< DS offset: its width. */

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

static void event__mapKey(
	const uint8_t * pPair,
	int8_t *		pSaved
);

static void event__cyclePalette(void);

static bool event__key(
	int16_t * pResult
);

static void event__pollMouse(void);

static bool event__click(
	uint32_t  timeoutAtEntry,
	int16_t * pResult
);

static bool event__outsideHotRect(
	int16_t column,
	int16_t row
);

static void event__moveCursor(
	bool * pHoverShown
);

static void event__screenStatus(
	uint16_t firstText
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
 * \brief	Wait for an event.
 *
 * \param	ticks			Ticks to wait at most (72.8 per second); 0 looks
 * 							once.
 *
 * \return	0: nothing happened in the time. > 0: the number of the
 * 			clicked hotspot of the active screen. < 0: a key the active
 * 			screen maps to a result (key code + 50). EVENT_TIMEOUT: the
 * 			tick in globals_timeout passed. EVENT_KEY: a key (in
 * 			globals_eventKey) or a click (0) came while
 * 			globals_anyKeyEnds is set, or a click while a message box is
 * 			open.
 *
 * \details	Port of WaitEvent (1000:1417). While waiting it moves the
 * 			cursor by the mouse (the timer handler moves it by keys),
 * 			redraws it, and shows the text of the hotspot under it in the
 * 			status line. Key code 0x7f ends the game. Not ported: the
 * 			timed event queue (DS:0x566, animations) and starting or
 * 			stopping the music when M is pressed (FUN_1000_0dd8,
 * 			FUN_1000_0d99; Phase 5). The port tells the mouse driver
 * 			where the cursor is, so that the cursor follows the host
 * 			pointer (the original cursor was the only pointer).
 *
 ******************************************************************************/
int16_t event_wait(
	int16_t ticks
) {
	bool hoverShown = false;

	if (globals_keyScreen == EVENT_SCREEN_PARTS) {
		event__screenStatus(EVENT_EMPTY_TEXTS);
		exe_data(EVENT_STATUS_TEXT)[0] = 0;
	}

	uint32_t timeoutAtEntry = globals_timeout;
	int32_t	 deadline = (int32_t)globals_ticks + ticks;

	for (;;) {
		int16_t result;

		dos_service();

		bool demoEnded = globals_demoMode == GLOBALS_DEMO_STOPPING;

		if (demoEnded && globals_exiting != GLOBALS_DEMO_STOPPING) {
			globals_exiting = GLOBALS_DEMO_STOPPING;
			dos_exit();
		}

		event__cyclePalette();

		if (event__key(&result)) {
			return result;
		}

		if (globals_timedEventTime <= globals_ticks) {
			/* Timed events (animations) are not ported yet. */
			globals_timedEventTime = GLOBALS_NO_EVENT;
		}

		event__pollMouse();

		if (event__click(timeoutAtEntry, &result)) {
			return result;
		}

		if (globals_cursorShown > 0 && globals_cursorMoved != 0) {
			event__moveCursor(&hoverShown);
		}

		if ((int32_t)globals_ticks >= deadline) {
			return 0;
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Make the keys of a screen active.
 *
 * \param	screen			Screen number.
 *
 * \details	Port of FUN_1000_12de. Each screen has a list of scan code
 * 			and key code pairs (table at DS:0x579f) that replace entries
 * 			of globals_keyCodes while the screen is active; the M key
 * 			pair (DS:0x57f3) is added to every screen. The replaced codes
 * 			are saved in globals_savedKeys. Not ported: DS:0x47cc, which
 * 			is cleared during demo playback.
 *
 ******************************************************************************/
void event_setScreenKeys(
	int16_t screen
) {
	globals_keyScreen = screen;
	event_restoreKeys(0);
	globals_keysChanged = (uint8_t)screen;

	uint16_t		tableEntry = (uint16_t)(EVENT_KEY_TABLE + screen * 2);
	const uint8_t * pPair = exe_data(exe_readU16(tableEntry));
	int8_t *		pSaved = globals_savedKeys;

	for (int count = 1; pPair[0] != 0 && count < EVENT_MAX_KEYS; count++) {
		event__mapKey(pPair, pSaved);
		pPair += 2;
		pSaved += 2;
	}

	event__mapKey(exe_data(exe_readU16(EVENT_MUSIC_KEY)), pSaved);
	pSaved[2] = 0;
}

/** ****************************************************************************
 *
 * \brief	Give the keys their codes from before event_setScreenKeys.
 *
 * \param	keepLetters		0: restore all keys and end the screen's key map.
 * 							Not 0: restore only the keys that were characters
 * 							(for typing), and keep the map active.
 *
 * \return	The screen whose keys were active, or 0.
 *
 * \details	Port of FUN_1000_1391. Also forgets the held direction keys.
 *
 ******************************************************************************/
int16_t event_restoreKeys(
	int16_t keepLetters
) {
	memset(globals_directionHeld, 0, sizeof globals_directionHeld);
	globals_lastDirectionY = 0;
	globals_lastDirectionX = 0;
	globals_keyDirectionY = 0;
	globals_keyDirectionX = 0;

	uint8_t screen = globals_keysChanged;

	if (screen == 0) {
		return 0;
	}

	for (int8_t * pSaved = globals_savedKeys; pSaved[0] != 0; pSaved += 2) {
		bool restore = keepLetters == 0 || pSaved[1] >= EVENT_FIRST_LETTER;
		int	 scanCode = (uint8_t)pSaved[0];

		if (restore && scanCode < GLOBALS_SCAN_CODES) {
			globals_keyCodes[scanCode] = pSaved[1];
		}
	}

	if (keepLetters == 0) {
		globals_keysChanged = 0;
	}

	return (int8_t)screen;
}

/** ****************************************************************************
 *
 * \brief	Forget pending input and make the next event_wait look at the
 * 			cursor again.
 *
 * \details	Port of FUN_1000_0d78: the cursor counts as moved, the
 * 			hotspot under it as unknown, clicks and the last key are
 * 			dropped.
 *
 ******************************************************************************/
void event_reset(void) {
	globals_cursorMoved = 1;
	globals_hotRight = -1;
	globals_hotId = EVENT_NO_HOTSPOT;
	globals_leftClick = 0;
	globals_rightClick = 0;
	input_readKey();
}

/** ****************************************************************************
 *
 * \brief	Replace the key code of one scan code, saving the old one.
 *
 * \param	pPair			Scan code and its new key code.
 * \param	pSaved			Output: the scan code and its old key code.
 *
 ******************************************************************************/
static void event__mapKey(
	const uint8_t * pPair,
	int8_t *		pSaved
) {
	int scanCode = pPair[0];

	pSaved[0] = (int8_t)pPair[0];
	pSaved[1] = 0;

	if (scanCode < GLOBALS_SCAN_CODES) {
		pSaved[1] = globals_keyCodes[scanCode];
		globals_keyCodes[scanCode] = (int8_t)pPair[1];
	}
}

/** ****************************************************************************
 *
 * \brief	Rotate the colors of palette entries 6 to 8 while music plays.
 *
 * \details	Part of WaitEvent: while globals_paletteCycle is set (the
 * 			question screen), on every tick that is a multiple of 16, and
 * 			only with the music on and the speaker sounding. The original
 * 			rotated on every pass of its loop during such a tick; the
 * 			three colors are at DS:0x698.
 *
 ******************************************************************************/
static void event__cyclePalette(void) {
	bool onTick = (globals_ticks & EVENT_CYCLE_MASK) == 0;
	bool playing = globals_musicOn != 0 && globals_speakerOn != 0;

	if (globals_paletteCycle == 0 || !onTick || !playing) {
		return;
	}

	uint8_t * pColors = exe_data(EVENT_CYCLE_COLORS);
	uint8_t	  last = pColors[2];

	pColors[2] = pColors[1];
	pColors[1] = pColors[0];
	pColors[0] = last;

	for (int index = 0; index < 3; index++) {
		int color = (int8_t)pColors[index];

		video_setPaletteColor(EVENT_CYCLE_FIRST + index, color);
	}
}

/** ****************************************************************************
 *
 * \brief	Handle the last key for event_wait.
 *
 * \param	pResult			Output: the result of event_wait, when it ends.
 *
 * \retval	true	event_wait ends with *pResult.
 * \retval	false	It goes on.
 *
 ******************************************************************************/
static bool event__key(
	int16_t * pResult
) {
	int16_t key = input_readKey();

	if (key < 0) {
		int16_t keyResult = (int16_t)(key + EVENT_KEY_OFFSET);

		if (keyResult != EVENT_MUSIC_RESULT) {
			*pResult = keyResult;
			return true;
		}

		if (globals_musicAvailable == 1) {
			/* The music itself starts and stops in Phase 5. */
			globals_musicOn = globals_musicOn == 0 ? 1 : 0;
		}

	} else if (key > 0) {
		if (globals_anyKeyEnds != 0) {
			globals_eventKey = key;
			*pResult = EVENT_KEY;
			return true;
		}

		if (key == EVENT_QUIT_KEY) {
			dos_exit();
		}
	}

	return false;
}

/** ****************************************************************************
 *
 * \brief	Move the cursor by the mouse.
 *
 * \details	Part of WaitEvent, with one addition: dos_mouseSetPosition
 * 			before the poll (see event_wait).
 *
 ******************************************************************************/
static void event__pollMouse(void) {
	if (globals_demoMode != 0 || globals_mousePresent == 0) {
		return;
	}

	dos_mouseSetPosition(globals_cursorX, globals_cursorY);
	input_mousePoll();

	if (globals_mouseStepX != 0 || globals_mouseStepY != 0) {
		globals_cursorMoved = 1;
	}

	if (globals_cursorMoved != 0) {
		globals_cursorX += globals_mouseStepX;
		globals_cursorY += globals_mouseStepY;
	}
}

/** ****************************************************************************
 *
 * \brief	Handle a pending click, or the timeout when there is none.
 *
 * \param	timeoutAtEntry	globals_timeout when event_wait started.
 * \param	pResult			Output: the result of event_wait, when it ends.
 *
 * \retval	true	event_wait ends with *pResult.
 * \retval	false	It goes on.
 *
 * \details	Part of WaitEvent. A right click counts like a left click, at
 * 			the position of the last left click.
 *
 ******************************************************************************/
static bool event__click(
	uint32_t  timeoutAtEntry,
	int16_t * pResult
) {
	if (globals_leftClick == 0 && globals_rightClick == 0) {
		if (timeoutAtEntry != 0 && globals_timeout < globals_ticks) {
			globals_timeout = 0;
			*pResult = EVENT_TIMEOUT;
			return true;
		}

		return false;
	}

	int16_t column = globals_leftClickX;
	int16_t row = globals_leftClickY;

	globals_lastClick = globals_leftClick;
	globals_leftClick = 0;
	globals_rightClick = 0;

	if (globals_messageShown == 0 && globals_screenDepth != 0) {
		int16_t hotspot = globals_hotId;

		if (event__outsideHotRect(column, row)) {
			hotspot = ui_hitTest(column, row);
		}

		if (hotspot != 0) {
			*pResult = hotspot;
			return true;
		}
	}

	if (globals_messageShown != 0 || globals_anyKeyEnds != 0) {
		globals_eventKey = 0;
		*pResult = EVENT_KEY;
		return true;
	}

	return false;
}

/** ****************************************************************************
 *
 * \brief	Check whether a point is outside the rectangle of the last
 * 			hotspot test.
 *
 * \param	column			Column of the point.
 * \param	row				Row of the point.
 *
 * \retval	true	The hotspot at the point must be searched.
 * \retval	false	It is globals_hotId.
 *
 ******************************************************************************/
static bool event__outsideHotRect(
	int16_t column,
	int16_t row
) {
	return column < globals_hotLeft || column >= globals_hotRight ||
		row < globals_hotTop || row >= globals_hotBottom;
}

/** ****************************************************************************
 *
 * \brief	Redraw the moved cursor and show the text of the hotspot under
 * 			it.
 *
 * \param	pHoverShown		In, out: the status line shows a hotspot text.
 *
 * \details	Part of WaitEvent. The cursor is kept inside columns 0..314
 * 			and rows 0..194. When the cursor comes onto another hotspot,
 * 			its text goes to the status line; when it leaves all
 * 			hotspots, the status line is cleared. Screen 0x25 shows its
 * 			own status text instead of an empty line.
 *
 ******************************************************************************/
static void event__moveCursor(
	bool * pHoverShown
) {
	if (globals_cursorX < 0) {
		globals_cursorX = 0;
	} else if (globals_cursorX > CURSOR_MAX_X) {
		globals_cursorX = CURSOR_MAX_X;
	}

	if (globals_cursorY < 0) {
		globals_cursorY = 0;
	} else if (globals_cursorY > CURSOR_MAX_Y) {
		globals_cursorY = CURSOR_MAX_Y;
	}

	int16_t column = globals_cursorX;
	int16_t row = globals_cursorY;

	globals_cursorDrawnX = column;
	globals_cursorDrawnY = row;
	globals_cursorMoved = 0;
	cursor_draw(column, row);

	if (globals_screenDepth == 0 || !event__outsideHotRect(column, row)) {
		return;
	}

	int16_t previous = globals_hotId;
	int16_t hotspot = ui_hitTest(column, row);

	if (hotspot == 0) {
		if (*pHoverShown) {
			ui_status(-1, exe_text(EVENT_EMPTY_TEXTS + 4), 0);
			*pHoverShown = false;
		}

		if (globals_keyScreen == EVENT_SCREEN_SHOP) {
			ui_status(
				exe_readS16(EVENT_STATUS_LEFT),
				exe_text(EVENT_STATUS_TEXT),
				exe_readS16(EVENT_STATUS_WIDTH)
			);
		} else if (previous != 0) {
			ui_status(-1, exe_text(EVENT_EMPTY_TEXTS + 5), 0);
		}

	} else if (previous != hotspot) {
		if (globals_keyScreen == EVENT_SCREEN_SHOP) {
			event__screenStatus(EVENT_EMPTY_TEXTS + 2);
		}

		uint16_t textField = (uint16_t)(globals_hotElement + 4);
		uint16_t text = (uint16_t)(0x239e + exe_readS16(textField));

		ui_status(-1, exe_text(text), 0);
		*pHoverShown = true;
	}
}

/** ****************************************************************************
 *
 * \brief	Clear the status line and the status text area of a screen.
 *
 * \param	firstText		DS offset of two empty texts in a row.
 *
 * \details	Part of WaitEvent, for screens 0x16 and 0x25: an empty message
 * 			in the default place and one in the screen's own place
 * 			(DS:0x51ee, width DS:0x8bc0), both with the line cleared
 * 			first.
 *
 ******************************************************************************/
static void event__screenStatus(
	uint16_t firstText
) {
	globals_statusShown = 1;
	ui_status(-1, exe_text(firstText), 0);
	globals_statusShown = 1;
	ui_status(
		exe_readS16(EVENT_STATUS_LEFT),
		exe_text((uint16_t)(firstText + 1)),
		exe_readS16(EVENT_STATUS_WIDTH)
	);
}

/** ****************************************************************************
 *
 * \brief	Wait for any key or click, at most a number of ticks.
 *
 * \param	ticks			Ticks to wait.
 *
 * \details	Port of WaitAnyKey (1000:3a83): sets the deadline and any-key
 * 			mode, waits up to 30000 ticks, then clears the deadline and
 * 			mode.
 *
 ******************************************************************************/
void event_waitAnyKey(
	int16_t ticks
) {
	globals_timeout = globals_ticks + (uint32_t)(int32_t)ticks;
	globals_anyKeyEnds = 1;
	event_wait(30000);
	globals_timeout = 0;
	globals_anyKeyEnds = 0;
}
