/** ****************************************************************************
 *
 * \file	opening.c
 *
 * \brief	The opening sequence: California Dreams logo, title, credits.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/opening.h"

#include "game/globals.h"
#include "game/input.h"
#include "game/screen.h"
#include "game/video.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define OPENING_LOGO	   3   /**< "California Dreams presents" (lib1/002). */
#define OPENING_TITLE	   1   /**< Title screen (lib1/000). */
#define OPENING_CREDITS	   4   /**< Credits (lib1/003). */
#define OPENING_LOGO_LEFT  40  /**< Logo position. */
#define OPENING_LOGO_TOP   40  /**< Logo position. */
#define OPENING_WAIT_TICKS 400 /**< Each screen: 5.5 seconds. */

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
 * \brief	Show the opening: logo, title and credits, 400 ticks each.
 *
 * \details	Port of ShowOpening (1f38:0bf0), EGA/VGA path. The logo is
 * 			drawn through to the display; the title and credits are drawn
 * 			on the work surface and wiped in. Each wait ends early on a
 * 			click, Enter or Esc (input_waitTicks). The credits palette is
 * 			set before the credits are wiped in, so the title shows in it
 * 			for the 15 ticks of the wipe, as in the original. Not ported:
 * 			the border color calls for other adapters (INT 10h, AH = 0Bh).
 *
 ******************************************************************************/
void opening_show(void) {
	int16_t savedHeight = globals_drawHeight;

	video_blankPalette();
	globals_drawHeight = PLAT_HEIGHT;
	screen_drawResourceAt(
		OPENING_LOGO,
		OPENING_LOGO_LEFT,
		OPENING_LOGO_TOP,
		0
	);
	screen_freeResource(OPENING_LOGO);
	video_showPalette();

	globals_drawThrough = 0;
	screen_drawResource(OPENING_TITLE, -1);
	screen_freeResource(OPENING_TITLE);
	input_waitTicks(OPENING_WAIT_TICKS);
	video_showPalette();
	screen_wipe(globals_displaySurface, globals_workSurface);

	screen_drawResource(OPENING_CREDITS, 0);
	screen_freeResource(OPENING_CREDITS);
	input_waitTicks(OPENING_WAIT_TICKS);
	video_setEgaPalette(video_creditsPalette);
	screen_wipe(globals_displaySurface, globals_workSurface);
	input_waitTicks(OPENING_WAIT_TICKS);

	globals_drawHeight = savedHeight;
	screen_fillRect(0, 0, PLAT_WIDTH, PLAT_HEIGHT, 0);
	screen_wipe(globals_displaySurface, globals_workSurface);
}
