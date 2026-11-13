/** ****************************************************************************
 *
 * \file	text.c
 *
 * \brief	Text drawing with the game's proportional 8-row font.
 *
 * \details	The font is part of Sr.exe (see exe.h). Text is drawn on the
 * 			pixel layer of a surface, pixel by pixel.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/text.h"

#include "game/exe.h"
#include "game/globals.h"
#include "game/screen.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define TEXT_LEFT_BIT 0x80 /**< Leftmost pixel of a font row. */

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
 * \brief	Width of a text in pixels.
 *
 * \param	pText			The text.
 *
 * \return	Sum of the widths of its characters.
 *
 * \details	Port of FUN_1f38_17c8. The original did not check the
 * 			characters; the port treats characters outside the font as
 * 			spaces, like the drawing function does.
 *
 ******************************************************************************/
int16_t text_width(
	const char * pText
) {
	int16_t width = 0;

	for (; *pText != '\0'; pText++) {
		width = (int16_t)(width + exe_glyph(*pText)[0]);
	}

	return width;
}

/** ****************************************************************************
 *
 * \brief	Draw a text on a surface.
 *
 * \param	surface			Surface (platform page) to draw on.
 * \param	left			Left edge of the text.
 * \param	top				Top edge of the text.
 * \param	pText			The text.
 *
 * \details	Port of FUN_31a0_113a, entry 3 of the EGA video function table
 * 			(DS:0x78ae). The whole rectangle of the text is written:
 * 			globals_textColor where the font has a pixel and
 * 			globals_textBackground elsewhere. Characters that would pass
 * 			column 320 (counted from the byte the text starts in) are left
 * 			out, and the text is clipped to the surface. globals_textEndX
 * 			and globals_textY receive the column after the text and its
 * 			row.
 *
 ******************************************************************************/
void text_draw(
	int			 surface,
	int16_t		 left,
	int16_t		 top,
	const char * pText
) {
	int byteOffset = left & 7;
	int width = 0;
	int characters = 0;

	for (const char * pNext = pText; *pNext != '\0'; pNext++) {
		int glyphWidth = exe_glyph(*pNext)[0];

		if (byteOffset + width + glyphWidth > PLAT_WIDTH) {
			break;
		}

		width += glyphWidth;
		characters++;
	}

	if (PLAT_WIDTH < left + width) {
		width = PLAT_WIDTH - left;
	}

	int surfaceHeight = screen_surfaceHeight(surface);
	int rows = TEXT_ROWS;

	if (surfaceHeight < top + TEXT_ROWS) {
		rows = surfaceHeight - top;
	}

	globals_textEndX = (int16_t)(left + width);
	globals_textY = top;

	plat_selectPage(surface);

	uint8_t * pPixels = plat_framebuffer();
	int		  column = left;

	for (int index = 0; index < characters; index++) {
		const uint8_t * pGlyph = exe_glyph(pText[index]);

		for (int bit = 0; bit < pGlyph[0]; bit++, column++) {
			if (column < 0 || column >= left + width) {
				continue;
			}

			for (int row = 0; row < rows; row++) {
				if (top + row < 0) {
					continue;
				}

				bool	set = (pGlyph[1 + row] & (TEXT_LEFT_BIT >> bit)) != 0;
				uint8_t color =
					set ? globals_textColor : globals_textBackground;

				pPixels[(top + row) * PLAT_WIDTH + column] = color;
			}
		}
	}
}
