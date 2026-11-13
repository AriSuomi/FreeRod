/** ****************************************************************************
 *
 * \file	fame.c
 *
 * \brief	The end of a game: the summer is over, the hall of fame and the
 * 			dialog that follows.
 *
 * \details	Port of FUN_1f38_0d62 (the summer is over), FUN_1000_339f (the
 * 			hall of fame) with its helpers FUN_1000_32d0, FUN_1000_32f4,
 * 			FUN_1000_3375, FUN_1f38_182c and FUN_1f38_6dd2, and
 * 			FUN_1000_315b (the dialog after the end of a game). The hall
 * 			of fame holds up to 10 entries of 23 bytes at DS:0x82ca: the
 * 			name (16 bytes) and the score as decimal text (offset 16), the
 * 			hours of game time the player needed. Fewer hours rank higher.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/fame.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/calendar.h"
#include "game/cursor.h"
#include "game/event.h"
#include "game/exe.h"
#include "game/game.h"
#include "game/input.h"
#include "game/globals.h"
#include "game/screen.h"
#include "game/state.h"
#include "game/text.h"
#include "game/ui.h"
#include "game/video.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define FAME_FILE		   "Hall_dat" /**< The hall of fame file. */
#define FAME_COUNT		   0x7646	  /**< DS offset: number of entries. */
#define FAME_ENTRIES	   0x82ca	  /**< DS offset: the entries. */
#define FAME_ENTRY		   0x17		  /**< Bytes of an entry. */
#define FAME_SCORE		   0x10		  /**< Offset of the score text. */
#define FAME_MAX		   10		  /**< Entries of the table. */
#define FAME_NAME_LENGTH   15		  /**< Characters of a name at most. */
#define FAME_FILE_SCRAMBLE 0x80		  /**< Bit set in each file byte. */

#define FAME_ROWS		   0x4bca /**< DS: int16[10], row of each entry. */
#define FAME_LAST_ROW	   0x4bdc /**< DS: row of the 10th entry. */
#define FAME_HOURS_TEXT	   0x4bde /**< DS: "hr" after each score. */
#define FAME_INPUT_STYLE   0x4be2 /**< DS: 5 bytes, style 8 on EGA. */
#define FAME_BUTTON_STYLE  0x4be8 /**< DS: 5 bytes, style 7 on EGA. */
#define FAME_ALT_PALETTE   0x462  /**< DS: palette of the hall of fame. */
#define FAME_PALETTE	   0x440  /**< DS: the game's palette. */
#define FAME_PALETTE_SIZE  0x11	  /**< 16 entries and the border color. */

#define FAME_RESOURCE	   5	/**< Picture of the hall of fame. */
#define FAME_STYLE_7	   0x23 /**< Offset of style 7 in the table. */
#define FAME_STYLE_8	   0x28 /**< Offset of style 8 in the table. */
#define FAME_STYLE_SIZE	   5	/**< Bytes of a style. */
#define FAME_NAME_LEFT	   0x2e /**< Column of the names. */
#define FAME_SCORE_LEFT	   0xa1 /**< Column of a 4-digit score. */
#define FAME_DIGIT_WIDTH   6	/**< Width of a digit. */
#define FAME_HOURS_LEFT	   0xbf /**< Column of "hr". */
#define FAME_RIGHT		   0xd3 /**< Right edge copied to the display. */
#define FAME_TEXT_COLOR	   4	/**< Text color of the entries (EGA). */
#define FAME_TEXT_BACK	   0	/**< Background of the entries (EGA). */
#define FAME_WAIT_TICKS	   700	/**< Ticks shown without a new entry. */
#define FAME_DAY_HOURS	   24	/**< Hours of a day. */
#define FAME_HOUR_OFFSET   12	/**< Added to the hour index (noon). */
#define FAME_FULL_HEIGHT   200	/**< Rows drawn of the picture. */

#define FAME_SUMMER_TEXT   0x17bb /**< Message: the summer is over. */
#define FAME_SUMMER_WAIT   300	  /**< Ticks the calendar shows at most. */
#define FAME_BLINK_ENTRY   12	  /**< Palette entry that blinks. */
#define FAME_BLINK_OFF	   5	  /**< Standard color when off. */

#define FAME_DIALOG		   0x29 /**< Screen of the dialog. */
#define FAME_DIALOG_ROWS   0xbe /**< Rows above the status line. */
#define FAME_BOX_LEFT	   100	/**< Outer box of the dialog. */
#define FAME_BOX_TOP	   0x32 /**< Outer box of the dialog. */
#define FAME_BOX_WIDTH	   0x78 /**< Outer box of the dialog. */
#define FAME_BOX_HEIGHT	   0x5a /**< Outer box of the dialog. */
#define FAME_BOX_INSET	   2	/**< Inner box: inset of the edges. */
#define FAME_DIALOG_TEXT   15	/**< Text color of the dialog (EGA). */
#define FAME_DIALOG_BACK   1	/**< Background of the dialog (EGA). */
#define FAME_DIALOG_LINE   13	/**< Line color of the dialog (EGA). */
#define FAME_DIALOG_FILL   3	/**< Fill color of the dialog (EGA). */

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

static bool fame__isEga(void);

static uint8_t * fame__entry(
	int16_t index
);

static bool fame__beats(
	int16_t index
);

static void fame__insert(
	int16_t index
);

static int16_t fame__playerScore(void);

static void fame__swapPalette(void);

static void fame__drawEntries(void);

static void fame__save(void);

static void fame__box(
	int16_t inset
);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

static int16_t fame__score; /**< DS:0x6c5e: score of the player. */

/** The hall of fame file as written: the count and the entries. */
static uint8_t fame__file[2 + FAME_MAX * FAME_ENTRY];

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Tell the player that the summer is over and show the calendar.
 *
 * \details	Port of FUN_1f38_0d62: message 0x17bb, then the calendar on a
 * 			cleared screen until a key, a click or 300 ticks.
 *
 ******************************************************************************/
void fame_summerOver(void) {
	game_message(FAME_SUMMER_TEXT);
	video_blankPalette();
	cursor_set(CURSOR_HIDE_NOW);
	globals_drawThrough = 1;
	screen_fillRect(0, 0, 320, FAME_FULL_HEIGHT, 0);
	calendar_show(1);
	video_setPaletteColor(FAME_BLINK_ENTRY, FAME_BLINK_OFF);
	video_showPalette();
	event_waitAnyKey(FAME_SUMMER_WAIT);
}

/** ****************************************************************************
 *
 * \brief	Show the hall of fame; a player who won gets an entry if the
 * 			score is good enough and types a name for it.
 *
 * \param	won				Nonzero: the game was won (bit 1 of
 * 							globals_endFlags).
 *
 * \details	Port of FUN_1000_339f. Resource 5 is drawn with its own
 * 			palette (FUN_1f38_182c swaps it in), styles 7 and 8 get the
 * 			hall of fame colors on EGA while the name is typed, and the
 * 			table is saved after the name was typed. Without a new entry
 * 			the table shows for 700 ticks or until a key or a click.
 *
 ******************************************************************************/
void fame_show(
	int16_t won
) {
	int16_t savedHeight = globals_drawHeight;
	int16_t slot = -1;

	video_blankPalette();
	globals_drawHeight = FAME_FULL_HEIGHT;
	globals_drawThrough = 1;
	screen_drawResource(FAME_RESOURCE, -1);
	globals_drawHeight = savedHeight;
	fame__swapPalette();

	uint8_t * pStyles = exe_data(globals_styleTable);
	uint8_t	  savedStyles[2 * FAME_STYLE_SIZE];

	memcpy(savedStyles, pStyles + FAME_STYLE_7, sizeof savedStyles);

	if (fame__isEga()) {
		memcpy(
			pStyles + FAME_STYLE_8,
			exe_data(FAME_INPUT_STYLE),
			FAME_STYLE_SIZE
		);
		memcpy(
			pStyles + FAME_STYLE_7,
			exe_data(FAME_BUTTON_STYLE),
			FAME_STYLE_SIZE
		);
	}

	video_showPalette();

	if (won != 0) {
		int16_t count = exe_readS16(FAME_COUNT);

		fame__score = fame__playerScore();

		for (int16_t index = 0; index < count; index++) {
			if (fame__beats(index)) {
				slot = index;
				break;
			}
		}

		if (count < FAME_MAX) {
			if (slot < 0) {
				slot = count;
			}

			exe_writeU16(FAME_COUNT, (uint16_t)(count + 1));
		}

		fame__insert(slot);
	}

	if (fame__isEga()) {
		globals_textColor = FAME_TEXT_COLOR;
		globals_textBackground = FAME_TEXT_BACK;
	} else {
		globals_textColor = 3;
		globals_textBackground = 2;
	}

	fame__drawEntries();
	screen_copyToDisplay(
		FAME_NAME_LEFT,
		exe_readS16(FAME_ROWS),
		FAME_RIGHT,
		(int16_t)(exe_readS16(FAME_LAST_ROW) + TEXT_ROWS)
	);

	if (slot < 0) {
		input_waitTicks(FAME_WAIT_TICKS);
	} else {
		char	  name[FAME_NAME_LENGTH + 1];
		uint8_t * pEntry = fame__entry(slot);

		memcpy(name, pEntry, sizeof name);
		name[FAME_NAME_LENGTH] = '\0';
		ui_textInput(
			FAME_NAME_LEFT,
			exe_readS16((uint16_t)(FAME_ROWS + 2 * slot)),
			name,
			FAME_NAME_LENGTH
		);
		memcpy(pEntry, name, strlen(name) + 1);
		fame__save();
	}

	video_blankPalette();
	memcpy(pStyles + FAME_STYLE_7, savedStyles, sizeof savedStyles);
	fame__swapPalette();
}

/** ****************************************************************************
 *
 * \brief	Show the dialog after the end of a game.
 *
 * \return	The result of screen 0x29: FAME_END_QUIT ends the program,
 * 			GAME_NEW starts a new game, other values load a game.
 *
 * \details	Port of FUN_1000_315b: the status line moves to row 190, the
 * 			screen is cleared and two nested boxes drawn on both surfaces,
 * 			then screen 0x29 runs. Not ported: releasing the preloaded
 * 			resources (DS:0x4f4c, FUN_1f38_68ca), as the port does not
 * 			preload them.
 *
 ******************************************************************************/
int16_t fame_endDialog(void) {
	ui_statusInit(FAME_DIALOG_ROWS);
	globals_drawThrough = 1;

	uint8_t textColor = globals_textColor;
	uint8_t textBackground = globals_textBackground;
	uint8_t lineColor = globals_lineColor;
	uint8_t fillColor = globals_fillColor;

	if (fame__isEga()) {
		globals_textColor = FAME_DIALOG_TEXT;
		globals_textBackground = FAME_DIALOG_BACK;
		globals_lineColor = FAME_DIALOG_LINE;
		globals_fillColor = FAME_DIALOG_FILL;
	} else {
		globals_textColor = 3;
		globals_textBackground = 0;
		globals_lineColor = 2;
		globals_fillColor = 1;
	}

	screen_fillRect(0, 0, 320, FAME_DIALOG_ROWS, globals_textBackground);
	fame__box(0);
	fame__box(FAME_BOX_INSET);

	globals_textColor = textColor;
	globals_textBackground = textBackground;
	globals_lineColor = lineColor;
	globals_fillColor = fillColor;

	cursor_set(CURSOR_SHOW_NOW);
	video_showPalette();

	int16_t result = ui_run(FAME_DIALOG);

	cursor_set(CURSOR_HIDE_NOW);

	return result;
}

/** ****************************************************************************
 *
 * \brief	Whether the EGA/VGA colors are used.
 *
 * \retval	true	The adapter is -2 or -6.
 * \retval	false	Another adapter.
 *
 ******************************************************************************/
static bool fame__isEga(void) {
	return globals_videoAdapter == -2 || globals_videoAdapter == -6;
}

/** ****************************************************************************
 *
 * \brief	An entry of the hall of fame.
 *
 * \param	index			Number of the entry, 0..9.
 *
 * \return	The 23 bytes of the entry in the data segment image.
 *
 ******************************************************************************/
static uint8_t * fame__entry(
	int16_t index
) {
	return exe_data((uint16_t)(FAME_ENTRIES + index * FAME_ENTRY));
}

/** ****************************************************************************
 *
 * \brief	Whether the player's score is better than an entry.
 *
 * \param	index			Number of the entry.
 *
 * \retval	true	fame__score is lower than the entry's score.
 * \retval	false	It is not.
 *
 * \details	Port of FUN_1000_32d0.
 *
 ******************************************************************************/
static bool fame__beats(
	int16_t index
) {
	const char * pScore = (const char *)fame__entry(index) + FAME_SCORE;

	return fame__score < (int16_t)atoi(pScore);
}

/** ****************************************************************************
 *
 * \brief	Put the player into the hall of fame.
 *
 * \param	index			Number of the new entry; negative: none.
 *
 * \details	Port of FUN_1000_32f4. The entries from index on move down
 * 			one place (the count is already updated, so a full table
 * 			drops its last entry), and the new entry gets the player's
 * 			name and fame__score.
 *
 ******************************************************************************/
static void fame__insert(
	int16_t index
) {
	int16_t moved = (int16_t)(exe_readS16(FAME_COUNT) - index - 1);

	if (index < 0) {
		return;
	}

	uint8_t * pEntry = fame__entry(index);

	if (moved > 0) {
		memmove(pEntry + FAME_ENTRY, pEntry, (size_t)moved * FAME_ENTRY);
	}

	const char * pName = (const char *)exe_data(STATE_PLAYER_NAME);

	memcpy(pEntry, pName, strlen(pName) + 1);
	snprintf(
		(char *)pEntry + FAME_SCORE,
		FAME_ENTRY - FAME_SCORE,
		"%d",
		fame__score
	);
}

/** ****************************************************************************
 *
 * \brief	The player's score: hours of game time played.
 *
 * \return	Day times 24 plus the hour index plus 12.
 *
 * \details	Port of FUN_1000_3375.
 *
 ******************************************************************************/
static int16_t fame__playerScore(void) {
	uint16_t hour;
	uint16_t minute;

	calendar_timeOfDay(&hour, &minute);

	return (int16_t)(calendar_day() * FAME_DAY_HOURS + (int16_t)hour +
					 FAME_HOUR_OFFSET);
}

/** ****************************************************************************
 *
 * \brief	Swap the game's palette and the hall of fame palette.
 *
 * \details	Port of FUN_1f38_182c: swaps the 17 bytes at DS:0x440 and
 * 			DS:0x462. The first 16 of DS:0x440 are video_gamePalette in
 * 			the port; the 17th (the border color) stays in the image.
 *
 ******************************************************************************/
static void fame__swapPalette(void) {
	uint8_t * pOther = exe_data(FAME_ALT_PALETTE);
	uint8_t * pImage = exe_data(FAME_PALETTE);

	for (int index = 0; index < FAME_PALETTE_SIZE; index++) {
		uint8_t * pOwn = index < 16 ? &video_gamePalette[index]
									: &pImage[index];
		uint8_t	  swap = *pOwn;

		*pOwn = pOther[index];
		pOther[index] = swap;
	}
}

/** ****************************************************************************
 *
 * \brief	Draw the entries of the hall of fame on the work surface.
 *
 * \details	Part of FUN_1000_339f: the name, the score right-aligned for
 * 			up to 4 digits and "hr".
 *
 ******************************************************************************/
static void fame__drawEntries(void) {
	int16_t count = exe_readS16(FAME_COUNT);

	for (int16_t index = 0; index < count; index++) {
		int16_t		 top = exe_readS16((uint16_t)(FAME_ROWS + 2 * index));
		const char * pEntry = (const char *)fame__entry(index);
		int16_t		 left = FAME_SCORE_LEFT;

		text_draw(globals_workSurface, FAME_NAME_LEFT, top, pEntry);

		/* The original loops forever on a score of 0 or less. */
		for (int16_t value = (int16_t)atoi(pEntry + FAME_SCORE);
			 value > 0 && value < 1000;
			 value = (int16_t)(value * 10)) {
			left = (int16_t)(left + FAME_DIGIT_WIDTH);
		}

		text_draw(globals_workSurface, left, top, pEntry + FAME_SCORE);
		text_draw(
			globals_workSurface,
			FAME_HOURS_LEFT,
			top,
			exe_text(FAME_HOURS_TEXT)
		);
	}
}

/** ****************************************************************************
 *
 * \brief	Write the hall of fame file.
 *
 * \details	Port of FUN_1f38_6dd2 (with FUN_1f38_6ccf and MaskHallOfFame):
 * 			the count, then the entries, with bit 7 set in every byte of
 * 			the entries. The port writes the file to the saved games
 * 			directory, not to the game files; hotdata_loadHallOfFame
 * 			reads it from there first. The original asked for a disk and
 * 			ended the game when the file could not be written; the port
 * 			logs it.
 *
 ******************************************************************************/
static void fame__save(void) {
	int16_t			count = exe_readS16(FAME_COUNT);
	size_t			size = (size_t)count * FAME_ENTRY;
	const uint8_t * pEntries = exe_data(FAME_ENTRIES);

	fame__file[0] = (uint8_t)(count & 0xff);
	fame__file[1] = (uint8_t)((uint16_t)count >> 8);

	for (size_t index = 0; index < size; index++) {
		fame__file[2 + index] = (uint8_t)(pEntries[index] | FAME_FILE_SCRAMBLE);
	}

	if (!plat_writeSave(FAME_FILE, fame__file, 2 + size)) {
		plat_log("could not write %s", FAME_FILE);
	}
}

/** ****************************************************************************
 *
 * \brief	Draw a box of the end dialog on the work and display surfaces.
 *
 * \param	inset			Distance of the edges from the outer box.
 *
 ******************************************************************************/
static void fame__box(
	int16_t inset
) {
	int16_t left = (int16_t)(FAME_BOX_LEFT + inset);
	int16_t top = (int16_t)(FAME_BOX_TOP + inset);
	int16_t width = (int16_t)(FAME_BOX_WIDTH - 2 * inset);
	int16_t height = (int16_t)(FAME_BOX_HEIGHT - 2 * inset);

	screen_box(globals_workSurface, left, top, width, height);
	screen_box(globals_displaySurface, left, top, width, height);
}
