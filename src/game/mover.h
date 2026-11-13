/** ****************************************************************************
 *
 * \file	mover.h
 *
 * \brief	The sprite mover: a sprite that moves over a background in
 * 			phases, one step per frame.
 *
 ******************************************************************************/

#ifndef GAME_MOVER_H_INCLUDED
#define GAME_MOVER_H_INCLUDED

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

#define MOVER_FRAMES 4 /**< Frames a mover cycles through, at most. */
#define MOVER_FRONTS 2 /**< Pictures drawn in front of the sprite. */
#define MOVER_PHASES 4 /**< Phases, at most (plus the end mark). */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/**
 * A picture the mover draws: its image and size.
 */
typedef struct mover_frame_tag {
	plat_ImageHandle image;	 /**< The image; 0 draws nothing. */
	int16_t			 width;	 /**< Width in pixels. */
	int16_t			 height; /**< Height in pixels. */
} mover_Frame;

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

void mover_start(
	int16_t				left,
	int16_t				bottom,
	const mover_Frame * pFrames,
	int16_t				frameCount,
	int					background
);

void mover_setFront(
	int					index,
	const mover_Frame * pFrame,
	int16_t				left,
	int16_t				top,
	const int16_t *		pClip
);

void mover_setFrame(
	int16_t				index,
	const mover_Frame * pFrame
);

void mover_setPhase(
	int16_t index,
	int16_t count,
	int8_t	stepX,
	int8_t	stepY
);

bool mover_step(void);

int16_t mover_top(void);

void mover_setTop(
	int16_t top
);

#endif // GAME_MOVER_H_INCLUDED
