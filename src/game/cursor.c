/** ****************************************************************************
 *
 * \file	cursor.c
 *
 * \brief	The game's own mouse cursor.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/cursor.h"

#include <stdbool.h>

#include "game/globals.h"
#include "game/lib.h"
#include "game/screen.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define CURSOR_RESOURCE	   2	/**< lib1/001: 4 shapes, 24 x 16 each. */
#define CURSOR_ROWS		   16	/**< Height of a shape. */
#define CURSOR_SIZE		   16	/**< Area the original copied. */
#define CURSOR_SHAPES	   4	/**< Shapes in the resource. */
#define CURSOR_START_X	   0x9e /**< Position at program start. */
#define CURSOR_START_Y	   0x80 /**< Position at program start. */
#define CURSOR_FIRST_SHAPE 1	/**< Shape at program start. */

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

static void cursor__present(void);

static bool cursor__touches(
	int16_t cursorLeft,
	int16_t cursorTop,
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height
);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

static plat_ImageHandle cursor__image; /**< The cursor shapes. */

/**
 * DS:0x7b0a: 1 while the cursor is drawn (set by showing, cleared by
 * hiding); cursor_draw does nothing while it is 0.
 */
static int8_t cursor__mode;

static bool	   cursor__inWork;	  /**< Drawn in the work surface. */
static int16_t cursor__workX;	  /**< DS:0x7b0c: column it was drawn at. */
static int16_t cursor__workY;	  /**< DS:0x7b0e: row it was drawn at. */
static bool	   cursor__wasDrawn;  /**< DS:0x7b10 >= 0: position is valid. */
static bool	   cursor__onDisplay; /**< Visible on the display surface. */
static int16_t cursor__displayX;  /**< Column on the display. */
static int16_t cursor__displayY;  /**< Row on the display. */

/**
 * DS:0x6ec: hot spot column and row of each cursor shape.
 */
static const int16_t cursor__hotSpots[CURSOR_SHAPES][2] = {
	{ 1, 1 },
	{ 2, 2 },
	{ 1, 1 },
	{ 8, 8 }
};

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Load the cursor shapes and place the cursor.
 *
 * \details	Port of the parts of FUN_1f38_2b28 the port needs: resource 2
 * 			holds the shapes, the first shape is 1 and the cursor starts
 * 			at (158, 128). The original also built 8 shifted copies of
 * 			the shapes and their masks in spare video memory for drawing
 * 			at any column; the platform draws the image instead. Not
 * 			ported: DS:0x4746/0x4748 (a saved cursor position, used
 * 			later) and DS:0x8247.
 *
 ******************************************************************************/
void cursor_init(void) {
	cursor__image = screen_resourceImage(lib_indexFromGameId(CURSOR_RESOURCE));
	globals_cursorShape = CURSOR_FIRST_SHAPE;
	globals_cursorX = CURSOR_START_X;
	globals_cursorY = CURSOR_START_Y;
	cursor__mode = 0;
	cursor__wasDrawn = false;
	cursor__inWork = false;
	cursor__onDisplay = false;
	cursor__present();
}

/** ****************************************************************************
 *
 * \brief	Forget where the cursor was drawn, after the screen was drawn
 * 			over.
 *
 * \details	DS:0x7b10 = -1, as FUN_1000_ab8b does before showing the
 * 			cursor again.
 *
 ******************************************************************************/
void cursor_forget(void) {
	cursor__wasDrawn = false;
}

/** ****************************************************************************
 *
 * \brief	Show or hide the cursor, or choose its shape.
 *
 * \param	command			CURSOR_SHOW, CURSOR_SHOW_NOW, CURSOR_HIDE,
 * 							CURSOR_HIDE_NOW, or a shape number (0..3).
 *
 * \details	Port of SetMouseCursor (1f38:2d7e). Showing and hiding count
 * 			in globals_cursorShown; the cursor is drawn while the count is
 * 			above 0. Drawing code hides it with CURSOR_HIDE and shows it
 * 			with CURSOR_SHOW around its work, which leaves the display
 * 			alone until the drawn area is copied there; the _NOW commands
 * 			also update the display (while globals_drawThrough is set).
 * 			A cursor that was drawn before reappears where it was last
 * 			drawn, not at the current position.
 *
 ******************************************************************************/
void cursor_set(
	int16_t command
) {
	int16_t left = globals_cursorX;
	int16_t top = globals_cursorY;

	if (left < 0) {
		left = 0;
	} else if (left > PLAT_WIDTH - 1) {
		left = PLAT_WIDTH - 1;
	}

	if (top < 0) {
		top = 0;
	} else if (top > PLAT_HEIGHT - 1) {
		top = PLAT_HEIGHT - 1;
	}

	if (command >= 0) {
		if (command >= CURSOR_SHAPES) {
			return;
		}

		globals_cursorShape = command;
		globals_cursorHotX = cursor__hotSpots[command][0];
		globals_cursorHotY = cursor__hotSpots[command][1];
		cursor_draw(left, top);

		return;
	}

	if (cursor__wasDrawn) {
		left = cursor__workX;
		top = cursor__workY;
	}

	if (command < CURSOR_SHOW_NOW) {
		globals_cursorShown--;

		if (globals_cursorShown >= 0) {
			cursor__mode = command == CURSOR_HIDE ? -2 : -1;
			cursor_draw(left, top);
			cursor__mode = 0;
		}

	} else {
		globals_cursorShown++;

		if (globals_cursorShown > 0) {
			cursor__mode = command == CURSOR_SHOW ? 2 : 1;
			cursor_draw(left, top);
			cursor__mode = 1;
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Draw the cursor at a position, or remove it.
 *
 * \param	left			Column of the cursor.
 * \param	top				Row of the cursor.
 *
 * \details	Port of FUN_1f38_2e8b. What happens depends on the mode set by
 * 			cursor_set: 0 nothing; 1 move the cursor and copy the area to
 * 			the display (while globals_drawThrough is set); 2 draw it in
 * 			the work surface only; -1 remove it, also from the display;
 * 			-2 remove it from the work surface only. The original restored
 * 			the saved background, drew the shape with its mask
 * 			(function table entry 4, DS:0x78b2) and copied the union of
 * 			the old and the new area to the display.
 *
 ******************************************************************************/
void cursor_draw(
	int16_t left,
	int16_t top
) {
	if (cursor__mode == 0) {
		return;
	}

	if (!cursor__inWork) {
		cursor__wasDrawn = false;
	}

	cursor__inWork = false;

	if (cursor__mode < 0) {
		if (!cursor__wasDrawn || cursor__mode < -1) {
			return;
		}

		cursor__mode = 1;

	} else {
		cursor__inWork = true;
	}

	if (globals_drawThrough != 0 && cursor__mode == 1) {
		cursor__onDisplay = cursor__inWork;
		cursor__displayX = left;
		cursor__displayY = top;
	}

	cursor__workX = left;
	cursor__workY = top;
	cursor__wasDrawn = true;
	cursor__present();
}

/** ****************************************************************************
 *
 * \brief	Tell the cursor that part of the work surface was copied to the
 * 			display.
 *
 * \param	left			Left edge of the copied area.
 * \param	top				Top edge of the copied area.
 * \param	width			Width of the copied area.
 * \param	height			Height of the copied area.
 *
 * \details	Not in the original, where the cursor was part of the copied
 * 			pixels: a copy that covers the cursor's place brings the
 * 			cursor of the work surface (or its absence) to the display.
 *
 ******************************************************************************/
void cursor_surfaceCopied(
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height
) {
	int16_t workX = cursor__workX;
	int16_t workY = cursor__workY;
	int16_t shownX = cursor__displayX;
	int16_t shownY = cursor__displayY;

	bool overWork = cursor__inWork &&
		cursor__touches(workX, workY, left, top, width, height);
	bool overDisplay = cursor__onDisplay &&
		cursor__touches(shownX, shownY, left, top, width, height);

	if (overWork) {
		cursor__onDisplay = true;
		cursor__displayX = cursor__workX;
		cursor__displayY = cursor__workY;
	} else if (overDisplay) {
		cursor__onDisplay = false;
	}

	cursor__present();
}

/** ****************************************************************************
 *
 * \brief	Give the platform the cursor to show.
 *
 ******************************************************************************/
static void cursor__present(void) {
	if (cursor__onDisplay && cursor__image) {
		plat_setCursor(
			cursor__image,
			globals_cursorShape * CURSOR_ROWS,
			CURSOR_ROWS,
			cursor__displayX,
			cursor__displayY
		);
	} else {
		plat_setCursor(0, 0, 0, 0, 0);
	}
}

/** ****************************************************************************
 *
 * \brief	Check whether a rectangle touches the cursor's area.
 *
 * \param	cursorLeft		Column of the cursor.
 * \param	cursorTop		Row of the cursor.
 * \param	left			Left edge of the rectangle.
 * \param	top				Top edge of the rectangle.
 * \param	width			Width of the rectangle.
 * \param	height			Height of the rectangle.
 *
 * \retval	true	They overlap.
 * \retval	false	They do not.
 *
 ******************************************************************************/
static bool cursor__touches(
	int16_t cursorLeft,
	int16_t cursorTop,
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height
) {
	return cursorLeft < left + width && left < cursorLeft + CURSOR_SIZE &&
		cursorTop < top + height && top < cursorTop + CURSOR_SIZE;
}
