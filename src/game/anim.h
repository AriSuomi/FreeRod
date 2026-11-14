/** ****************************************************************************
 *
 * \file	anim.h
 *
 * \brief	The timed event queue and the animations it steps.
 *
 ******************************************************************************/

#ifndef GAME_ANIM_H_INCLUDED
#define GAME_ANIM_H_INCLUDED

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <stdint.h>

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define ANIM_GAS_METER 2 /**< Animation: the pump's meter while gas flows. */
#define ANIM_ATTENDANT 3 /**< Animation: the attendant getting up. */
#define ANIM_GAUGE	   8 /**< Animation: the tuning gauge. */

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

int16_t anim_start(
	int16_t animation
);

bool anim_isRunning(
	int16_t animation
);

void anim_stop(
	int16_t animation
);

void anim_play(
	int16_t animation
);

void anim_resetQueue(void);

void anim_timedEvent(void);

#endif // GAME_ANIM_H_INCLUDED
