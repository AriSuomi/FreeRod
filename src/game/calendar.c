/** ****************************************************************************
 *
 * \file	calendar.c
 *
 * \brief	The game's calendar: the summer of 1963, from June 16 on.
 *
 * \details	Time passes with the BIOS clock (18.2 ticks a second) since
 * 			the game started, plus globals_gameTime, which actions add
 * 			to. A day is 0x1998 = 6552 of these ticks, 6 minutes of
 * 			playing.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/calendar.h"

#include <stdio.h> /* snprintf only; no file access or printing */

#include "game/cursor.h"
#include "game/event.h"
#include "game/exe.h"
#include "game/globals.h"
#include "game/lib.h"
#include "game/screen.h"
#include "game/text.h"
#include "game/ui.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define CALENDAR_DAY_TICKS	 0x1998	   /**< Clock ticks of a day. */
#define CALENDAR_HOUR_TICKS	 0x222	   /**< Clock ticks of an hour. */
#define CALENDAR_MAX_TICKS	 0x91908UL /**< Max elapsed ticks clamped to. */
#define CALENDAR_MONTHS		 0x5082	   /**< DS: 4 month name pointers. */
#define CALENDAR_DATE_LEFT	 0x61	   /**< Column of newspaper date. */
#define CALENDAR_DATE_STYLE	 13		   /**< Style of the date. */
#define CALENDAR_DATE_LENGTH 30		   /**< Buffer for the date text. */
#define CALENDAR_END_FLAG	 1		   /**< globals_endFlags: summer. */
#define CALENDAR_POPUP_LEFT	 0x50	   /**< Left edge of wall calendar. */
#define CALENDAR_POPUP_TOP	 4		   /**< Top edge of wall calendar. */
#define CALENDAR_RES_EARLY	 0x4c3	   /**< Calendar sheet June/July. */
#define CALENDAR_RES_MARK	 0x4c4	   /**< Circle sprite for day. */
#define CALENDAR_RES_LATE	 0x4c5	   /**< Calendar sheet Aug/Sept. */
#define CALENDAR_MARK_LEFT	 0x5048	   /**< DS: base column of mark. */
#define CALENDAR_MARK_TOP	 0x504a	   /**< DS: base row per month. */
#define CALENDAR_FIRST_WEEK	 0x5052	   /**< DS: first week per month. */
#define CALENDAR_STATUS_TEXT 0x505a	   /**< DS: calendar status text. */
#define CALENDAR_WAIT_TICKS	 500	   /**< Ticks calendar stays up. */

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
 * \brief	The number of the current day.
 *
 * \return	0 (June 16) .. 91.
 *
 * \details	Port of FUN_1000_6476.
 *
 ******************************************************************************/
int16_t calendar_day(void) {
	uint32_t now = globals_gameTime + globals_biosTicks;
	int32_t	 elapsed = (int32_t)(now - globals_biosTicksAtStart);
	int32_t	 day = elapsed / CALENDAR_DAY_TICKS;

	if (day > CALENDAR_LAST_DAY) {
		day = CALENDAR_LAST_DAY;
	}

	return (int16_t)day;
}

/** ****************************************************************************
 *
 * \brief	Turn a day number into a month and a day of the month.
 *
 * \param	day				Day number from calendar_day.
 * \param	pDayOfMonth		Output: the day of the month.
 *
 * \return	The month: 0 June, 1 July, 2 August, 3 September.
 *
 * \details	Port of FUN_1000_653c. Days after 90 are September 15.
 *
 ******************************************************************************/
int16_t calendar_date(
	int16_t	  day,
	int16_t * pDayOfMonth
) {
	if (day < 15) {
		*pDayOfMonth = (int16_t)(day + 16);
		return 0;
	}

	if (day < 46) {
		*pDayOfMonth = (int16_t)(day - 14);
		return 1;
	}

	if (day < 77) {
		*pDayOfMonth = (int16_t)(day - 45);
		return 2;
	}

	*pDayOfMonth = day > 90 ? 15 : (int16_t)(day - 76);

	return 3;
}

/** ****************************************************************************
 *
 * \brief	Draw the date on the work surface, as "June 16, 1963".
 *
 * \param	top				Row of the text.
 *
 * \details	Port of FUN_1000_6750: at column 97, in style 13; the drawing
 * 			style is kept.
 *
 ******************************************************************************/
void calendar_drawDate(
	int16_t top
) {
	char	 text[CALENDAR_DATE_LENGTH];
	int16_t	 dayOfMonth;
	int16_t	 month = calendar_date(calendar_day(), &dayOfMonth);
	uint16_t nameAt = exe_readU16((uint16_t)(CALENDAR_MONTHS + month * 2));

	snprintf(text, sizeof text, "%s %d, 1963", exe_text(nameAt), dayOfMonth);

	uint8_t savedText = globals_textColor;
	uint8_t savedBackground = globals_textBackground;
	uint8_t savedFill = globals_fillColor;
	uint8_t savedLine = globals_lineColor;
	uint8_t savedBorder = globals_borderStyle;

	ui_setStyle(CALENDAR_DATE_STYLE);
	text_draw(globals_workSurface, CALENDAR_DATE_LEFT, top, text);
	globals_textColor = savedText;
	globals_textBackground = savedBackground;
	globals_fillColor = savedFill;
	globals_lineColor = savedLine;
	globals_borderStyle = savedBorder;
}

/** ****************************************************************************
 *
 * \brief	Compute the hour and minute within the current day.
 *
 * \param	pHour			Out: hour index (0..11, 546 ticks each).
 * \param	pMinute			Out: minute within the hour (0..59).
 *
 * \details	Port of FUN_1000_64a3.
 *
 ******************************************************************************/
void calendar_timeOfDay(
	uint16_t * pHour,
	uint16_t * pMinute
) {
	uint32_t now = globals_gameTime + globals_biosTicks;
	int32_t	 elapsed = (int32_t)(now - globals_biosTicksAtStart);

	if (elapsed > (int32_t)CALENDAR_MAX_TICKS) {
		elapsed = (int32_t)CALENDAR_MAX_TICKS;
	}

	uint16_t rem = (uint16_t)(elapsed % CALENDAR_DAY_TICKS);

	*pHour = (uint16_t)(rem / CALENDAR_HOUR_TICKS);

	uint32_t intoHour = (uint32_t)(rem - *pHour * CALENDAR_HOUR_TICKS);

	*pMinute = (uint16_t)(intoHour * 60 / CALENDAR_HOUR_TICKS);
}

/** ****************************************************************************
 *
 * \brief	Show the wall calendar popup with the current day circled.
 *
 * \param	keepOpen		0: wait for a key or click, restore background;
 * 							non-zero: leave calendar on screen.
 *
 * \details	Port of FUN_1000_65af.
 *
 ******************************************************************************/
void calendar_show(
	int16_t keepOpen
) {
	calendar_checkEnd();

	if (keepOpen == 0 && globals_endFlags != 0) {
		return;
	}

	int16_t	 day = calendar_day();
	uint16_t hour;
	uint16_t minute;
	int16_t	 dayOfMonth;

	calendar_timeOfDay(&hour, &minute);

	int16_t month = calendar_date(day, &dayOfMonth);
	int16_t firstWeek =
		exe_readS16((uint16_t)(CALENDAR_FIRST_WEEK + month * 2));
	int16_t monthTop = exe_readS16((uint16_t)(CALENDAR_MARK_TOP + month * 2));
	int16_t week = (int16_t)((day + 21) / 7 - firstWeek);
	int16_t markLeft =
		(int16_t)(((day + 21) % 7) * 18 + exe_readS16(CALENDAR_MARK_LEFT));
	int16_t markTop = (int16_t)(week * 7 + monthTop);

	globals_drawThrough = 1;

	int16_t sheetId = month < 2 ? CALENDAR_RES_EARLY : CALENDAR_RES_LATE;

	const lib_Resource * pSheet = lib_get(lib_indexFromGameId(sheetId));
	int16_t				 width = (int16_t)pSheet->width;
	int16_t				 height = (int16_t)pSheet->height;

	cursor_set(CURSOR_HIDE);

	int saved = screen_saveRect(
		CALENDAR_POPUP_LEFT,
		CALENDAR_POPUP_TOP,
		width,
		height
	);

	screen_drawSprite(
		globals_workSurface,
		sheetId,
		CALENDAR_POPUP_LEFT,
		CALENDAR_POPUP_TOP
	);
	screen_drawSprite(
		globals_workSurface,
		CALENDAR_RES_MARK,
		(int16_t)(CALENDAR_POPUP_LEFT + (markLeft & 0xff)),
		(int16_t)(CALENDAR_POPUP_TOP + (markTop & 0xff))
	);
	cursor_set(CURSOR_SHOW);
	screen_copyToDisplay(
		CALENDAR_POPUP_LEFT,
		CALENDAR_POPUP_TOP,
		(int16_t)(CALENDAR_POPUP_LEFT + width - 1),
		(int16_t)(CALENDAR_POPUP_TOP + height - 1)
	);

	if (keepOpen == 0) {
		ui_status(-1, exe_text(CALENDAR_STATUS_TEXT), 0);
		event_waitAnyKey(CALENDAR_WAIT_TICKS);
		screen_restoreRect(saved, true);
	} else {
		screen_dropRect(saved);
	}
}

/** ****************************************************************************
 *
 * \brief	Note the end of the summer when the last day has come.
 *
 * \details	Port of FUN_1000_6529: after day 90, bit 0 of
 * 			globals_endFlags is set, which ends the game.
 *
 ******************************************************************************/
void calendar_checkEnd(void) {
	if (calendar_day() > CALENDAR_LAST_DAY - 1) {
		globals_endFlags |= CALENDAR_END_FLAG;
	}
}
