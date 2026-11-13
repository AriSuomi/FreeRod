/** ****************************************************************************
 *
 * \file	video.c
 *
 * \brief	Video setup shared by game code.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/video.h"

#include "game/globals.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

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

/**
 * The 16 standard colors as 6-bit DAC values (what EGA shows in 200-line
 * modes).
 */
static const uint8_t video__standardColors[16][3] = {
	{ 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x2a },
	{ 0x00, 0x2a, 0x00 },
	{ 0x00, 0x2a, 0x2a },
	{ 0x2a, 0x00, 0x00 },
	{ 0x2a, 0x00, 0x2a },
	{ 0x2a, 0x15, 0x00 },
	{ 0x2a, 0x2a, 0x2a },
	{ 0x15, 0x15, 0x15 },
	{ 0x15, 0x15, 0x3f },
	{ 0x15, 0x3f, 0x15 },
	{ 0x15, 0x3f, 0x3f },
	{ 0x3f, 0x15, 0x15 },
	{ 0x3f, 0x15, 0x3f },
	{ 0x3f, 0x3f, 0x15 },
	{ 0x3f, 0x3f, 0x3f },
};

/**
 * DS:0x440: palette register -> standard color. The game's palette.
 * SetEgaPaletteEntry also stores the colors it sets here, so they stay
 * when the palette is shown again.
 */
uint8_t video_gamePalette[16] = {
	0, 0, 8, 7, 15, 7, 9, 11, 3, 2, 4, 12, 5, 10, 6, 14
};

/**
 * DS:0x452: all black, used to hide the screen while it is drawn.
 */
const uint8_t video_blackPalette[16] = { 0 };

/**
 * DS:0x2d4: palette of the credits screen (resource 4) in the opening.
 */
const uint8_t video_creditsPalette[16] = {
	0, 8, 7, 15, 14, 1, 9, 11, 0, 2, 4, 12, 13, 10, 5, 6
};

/**
 * DS:0x692: palette of the question screen (resource 8).
 */
const uint8_t video_questionPalette[16] = {
	0, 0, 8, 7, 15, 7, 15, 8, 7, 2, 4, 12, 5, 10, 6, 14
};

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Set palette entries 0..15 from a table of standard colors.
 *
 * \param	pTable			16 standard color numbers, one per palette entry.
 *
 * \details	Port of SetEgaPalette (1f38:1fa4), which sets the 16 EGA
 * 			palette registers with INT 10h AX=1002h.
 *
 ******************************************************************************/
void video_setEgaPalette(
	const uint8_t * pTable
) {
	for (int index = 0; index < 16; index++) {
		const uint8_t * pColor = video__standardColors[pTable[index] & 15];

		plat_setPaletteVga(index, 1, pColor);
	}
}

/** ****************************************************************************
 *
 * \brief	Set palette entries 0..15 to the game's colors.
 *
 * \details	SetEgaPalette (1f38:1fa4) with the table main uses
 * 			(DS:0x440).
 *
 ******************************************************************************/
void video_setGamePalette(void) {
	video_setEgaPalette(video_gamePalette);
}

/** ****************************************************************************
 *
 * \brief	Hide the screen by making all colors black.
 *
 * \details	Port of BlankPalette (1f38:0b2d), entry 7 of the EGA video
 * 			function table (DS:0x78be).
 *
 ******************************************************************************/
void video_blankPalette(void) {
	video_setEgaPalette(video_blackPalette);
	globals_paletteShown = 0;
}

/** ****************************************************************************
 *
 * \brief	Show the screen with the game's palette.
 *
 * \details	Port of ShowPalette (1f38:0b3f), entry 8 of the EGA video
 * 			function table (DS:0x78c2).
 *
 ******************************************************************************/
void video_showPalette(void) {
	video_setEgaPalette(video_gamePalette);
	globals_paletteShown = 1;
}

/** ****************************************************************************
 *
 * \brief	Set one palette entry to one of the 16 standard colors.
 *
 * \param	index			Palette entry.
 * \param	standardColor	Standard color 0..15.
 *
 * \details	Port of SetEgaPaletteEntry (1f38:1f4b). The game uses it for
 * 			car paint on indexes 6, 7, 8 and 12.
 *
 ******************************************************************************/
void video_setPaletteColor(
	int index,
	int standardColor
) {
	const uint8_t * pColor = video__standardColors[standardColor & 15];

	if (index >= 0 && index < 16) {
		video_gamePalette[index] = (uint8_t)(standardColor & 15);
	}

	plat_setPaletteVga(index, 1, pColor);
}

/** ****************************************************************************
 *
 * \brief	Give palette entries 6 to 8 their colors for the new game
 * 			screen.
 *
 * \details	Port of FUN_1f38_181c: stores standard colors 9, 1 and 3 in
 * 			the game palette table (DS:0x446..0x448); they show with the
 * 			next video_showPalette.
 *
 ******************************************************************************/
void video_setNewGameColors(void) {
	video_gamePalette[6] = 9;
	video_gamePalette[7] = 1;
	video_gamePalette[8] = 3;
}
