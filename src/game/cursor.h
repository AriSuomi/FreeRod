/** ****************************************************************************
 *
 * \file	cursor.h
 *
 * \brief	The game's own mouse cursor.
 *
 * \details	The original drew the cursor into the work surface, saved the
 * 			pixels under it and copied the area to the display. The port
 * 			keeps the cursor out of the surfaces: the platform draws it
 * 			over the finished picture, so replacement art under it stays
 * 			whole. The module tracks where the original would have had the
 * 			cursor (in the work surface, on the display) to show and hide
 * 			it at the same moments.
 *
 ******************************************************************************/

#ifndef GAME_CURSOR_H_INCLUDED
#define GAME_CURSOR_H_INCLUDED

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include <stdint.h>

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define CURSOR_SHOW		(-1)  /**< Show again after drawing. */
#define CURSOR_SHOW_NOW (-2)  /**< Show, also on the display. */
#define CURSOR_HIDE		(-3)  /**< Hide while drawing. */
#define CURSOR_HIDE_NOW (-4)  /**< Hide, also on the display. */
#define CURSOR_MAX_X	0x13a /**< Rightmost cursor column. */
#define CURSOR_MAX_Y	0xc2  /**< Lowest cursor row. */

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

void cursor_init(void);

void cursor_forget(void);

void cursor_set(
	int16_t command
);

void cursor_draw(
	int16_t left,
	int16_t top
);

void cursor_surfaceCopied(
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height
);

#endif // GAME_CURSOR_H_INCLUDED
