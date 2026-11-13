/** ****************************************************************************
 *
 * \file	question.c
 *
 * \brief	The manual question asked before the game starts.
 *
 * \details	The copy protection of the original: "What is the color of the
 * 			car key (or car icon) on page N?", answered by clicking one of
 * 			the colors on the screen.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/question.h"

#include <stdio.h> /* snprintf only; no file access or printing */
#include <string.h>

#include "game/cursor.h"
#include "game/exe.h"
#include "game/globals.h"
#include "game/random.h"
#include "game/screen.h"
#include "game/ui.h"
#include "game/video.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define QUESTION_COUNT		 0x33	/**< Questions to choose from. */
#define QUESTION_ICONS		 0x12	/**< The first 18 ask for the icon. */
#define QUESTION_PAGES		 0x4b62 /**< DS offset: page, answer pairs. */
#define QUESTION_EMPTY		 0x4afe /**< DS offset: empty text. */
#define QUESTION_EMPTY2		 0x4b61 /**< DS offset: empty text. */
#define QUESTION_KEY_LINE1	 0x4aff /**< DS offset: "What is the color of". */
#define QUESTION_KEY_LINE2	 0x4b14 /**< DS offset: "the car key on ...". */
#define QUESTION_ICON_LINE1	 0x4b2c /**< DS offset: "What is the color of". */
#define QUESTION_ICON_LINE2	 0x4b41 /**< DS offset: "the car icon on ...". */
#define QUESTION_PICTURE	 8		/**< Resource of the screen (lib1/007). */
#define QUESTION_PICTURE_X	 8		/**< Its left edge. */
#define QUESTION_LINE1_X	 0x79	/**< Left edge of the first line. */
#define QUESTION_LINE1_Y	 0x3f	/**< Row of the first line. */
#define QUESTION_LINE2_Y	 0x48	/**< Row of the second line. */
#define QUESTION_KEY_X		 0x72	/**< Left edge, car key question. */
#define QUESTION_ICON_X		 0x55	/**< Left edge, car icon question. */
#define QUESTION_SCREEN		 0x27	/**< Screen with the color hotspots. */
#define QUESTION_LINE_LENGTH 50		/**< Buffer of the second line. */

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

static void question__format(
	char *		 pOutput,
	size_t		 size,
	const char * pFormat,
	int			 number
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
 * \brief	Ask the manual question and wait for an answer.
 *
 * \return	1: go on with the game. The original left the game on 0, but
 * 			this version of Sr.exe returns 1 whatever is clicked: the
 * 			answer is not checked.
 *
 * \details	Port of FUN_1000_2fc8. A random question is chosen from the
 * 			table at DS:0x4b62 (page number and answer; the first 18 ask
 * 			for the car icon, the others for the car key). The screen
 * 			(resource 8) and the two text lines are drawn on the work
 * 			surface and wiped in, with the palette of DS:0x692; then
 * 			screen 0x27 waits for a click on a color. While it waits,
 * 			colors 6 to 8 cycle if music plays (globals_paletteCycle).
 *
 ******************************************************************************/
int16_t question_ask(void) {
	char	line[QUESTION_LINE_LENGTH];
	int16_t question = random_next(QUESTION_COUNT);
	bool	icon = question < QUESTION_ICONS;
	int		page = (int8_t)exe_data(QUESTION_PAGES)[question * 2];

	ui_status(-1, exe_text(QUESTION_EMPTY), 0);
	globals_drawThrough = 0;
	screen_drawResourceAt(QUESTION_PICTURE, QUESTION_PICTURE_X, 0, 0);
	screen_freeResource(QUESTION_PICTURE);

	uint16_t line1 = icon ? QUESTION_ICON_LINE1 : QUESTION_KEY_LINE1;
	uint16_t line2 = icon ? QUESTION_ICON_LINE2 : QUESTION_KEY_LINE2;
	int16_t	 line2Left = icon ? QUESTION_ICON_X : QUESTION_KEY_X;

	question__format(line, sizeof line, exe_text(line2), page);
	screen_drawText(QUESTION_LINE1_X, QUESTION_LINE1_Y, exe_text(line1));
	screen_drawText(line2Left, QUESTION_LINE2_Y, line);

	cursor_set(CURSOR_SHOW_NOW);
	screen_wipe(globals_displaySurface, globals_workSurface);
	video_setEgaPalette(video_questionPalette);
	globals_paletteShown = 1;
	globals_drawThrough = 1;

	globals_paletteCycle = 1;
	ui_run(QUESTION_SCREEN);
	globals_paletteCycle = 0;

	ui_status(-1, exe_text(QUESTION_EMPTY2), 0);

	return 1;
}

/** ****************************************************************************
 *
 * \brief	Copy a text, replacing its "%d" with a number.
 *
 * \param	pOutput			Buffer for the result.
 * \param	size			Size of the buffer.
 * \param	pFormat			Text with at most one "%d".
 * \param	number			The number.
 *
 * \details	Stands in for sprintf with a format from the game's data, so
 * 			that data is never used as a format string.
 *
 ******************************************************************************/
static void question__format(
	char *		 pOutput,
	size_t		 size,
	const char * pFormat,
	int			 number
) {
	const char * pPlace = strstr(pFormat, "%d");

	if (!pPlace) {
		snprintf(pOutput, size, "%s", pFormat);
		return;
	}

	int before = (int)(pPlace - pFormat);

	snprintf(pOutput, size, "%.*s%d%s", before, pFormat, number, pPlace + 2);
}
