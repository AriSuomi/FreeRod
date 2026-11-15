/** ****************************************************************************
 *
 * \file	screen.h
 *
 * \brief	Drawing on the game's two surfaces: resources, filled
 * 			rectangles and the wipe between them.
 *
 * \details	The game draws on a work surface (globals_workSurface) and
 * 			shows the display surface (globals_displaySurface). While
 * 			globals_drawThrough is set, every drawing is also copied to the
 * 			display surface. In the port the surfaces are platform pages:
 * 			the display is page 0, which is shown.
 *
 ******************************************************************************/

#ifndef GAME_SCREEN_H_INCLUDED
#define GAME_SCREEN_H_INCLUDED

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define SCREEN_SCENE_PAGE 7 /**< Scene behind moving sprites. */
#define SCREEN_BAY_PAGE	  8 /**< Engine bay built off-screen (DS:0x8232). */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/*******************************************************************************
;
;	F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

void screen_initSurfaces(void);

plat_ImageHandle screen_resourceImage(
	int index
);

void screen_drawResource(
	int16_t gameId,
	int16_t clearColor
);

int16_t screen_byteColumn(
	int16_t column
);

void screen_drawResourceAt(
	int16_t gameId,
	int16_t left,
	int16_t top,
	int16_t clearColor
);

void screen_fillRect(
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height,
	uint8_t color
);

void screen_freeResource(
	int16_t gameId
);

void screen_wipe(
	int destination,
	int source
);

void screen_copyToDisplay(
	int16_t left,
	int16_t top,
	int16_t right,
	int16_t bottom
);

void screen_fill(
	int		surface,
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height
);

void screen_line(
	int		surface,
	int16_t fromLeft,
	int16_t fromTop,
	int16_t toLeft,
	int16_t toTop,
	uint8_t color
);

void screen_verticalLine(
	int		surface,
	int16_t left,
	int16_t top,
	int16_t height
);

void screen_horizontalLine(
	int		surface,
	int16_t left,
	int16_t top,
	int16_t width
);

void screen_frame(
	int		surface,
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height
);

void screen_box(
	int		surface,
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height
);

void screen_drawText(
	int16_t		 left,
	int16_t		 top,
	const char * pText
);

void screen_recolor(
	int		surface,
	uint8_t fromColor,
	uint8_t toColor,
	int16_t left,
	int16_t top,
	int16_t right,
	int16_t bottom
);

void screen_drawSprite(
	int		surface,
	int16_t gameId,
	int16_t left,
	int16_t top
);

int screen_saveRect(
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height
);

void screen_restoreRect(
	int	 handle,
	bool toDisplay
);

void screen_dropRect(
	int handle
);

int16_t screen_surfaceHeight(
	int surface
);

void screen_resetSaved(void);

void screen_putBackRect(
	int handle
);

int screen_savedPage(
	int handle
);

void screen_drawSpritePart(
	int		surface,
	int16_t gameId,
	int16_t sourceLeft,
	int16_t sourceTop,
	int16_t width,
	int16_t height,
	int16_t left,
	int16_t top,
	bool	opaque
);

void screen_drawSpritePartExact(
	int		surface,
	int16_t gameId,
	int16_t sourceLeft,
	int16_t sourceTop,
	int16_t width,
	int16_t height,
	int16_t left,
	int16_t top,
	bool	opaque
);

void screen_waitRetraces(
	int16_t count
);

void screen_slide(
	int16_t gameId,
	int16_t sourceLeft,
	int16_t width,
	int16_t height,
	int16_t left,
	int16_t fromRow,
	int16_t toRow,
	int16_t stripTop,
	int16_t stripBottom
);

void screen_setSlideOverlay(
	int16_t gameId,
	int16_t left,
	int16_t top
);

void screen_slideAt(
	int16_t gameId,
	int16_t sourceLeft,
	int16_t width,
	int16_t height,
	int16_t left,
	int16_t fromRow,
	int16_t toRow,
	int16_t stripTop,
	int16_t stripBottom
);

#endif // GAME_SCREEN_H_INCLUDED
