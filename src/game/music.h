/** ****************************************************************************
 *
 * \file	music.h
 *
 * \brief	The track player: the music on the PC speaker.
 *
 ******************************************************************************/

#ifndef GAME_MUSIC_H_INCLUDED
#define GAME_MUSIC_H_INCLUDED

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

/**
 * Segments of the tracks as the game holds them (Ghidra shows 3e6b and
 * 3f93).
 */
#define MUSIC_TRACK_SEGMENT	 0x2e6b /**< The music. */
#define MUSIC_EFFECT_SEGMENT 0x2f93 /**< The looping effects. */

#define MUSIC_REPEATS		 0x5858 /**< DS: repeat list of the music. */
#define MUSIC_EFFECT_REPEATS 0x587e /**< DS: repeat list of the effects. */

#define MUSIC_FIRST_WORD	 2	/**< music_startTrack: first word of a track. */
#define MUSIC_START_DELAY	 10 /**< music_startTrack: delay of the music. */

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

void music_init(void);

void music_timerTick(void);

void music_startTrack(
	int16_t index,
	int16_t delay
);

void music_stop(void);

void music_resume(void);

void music_toggle(void);

void music_pause(
	int16_t pause
);

void music_playEffectTrack(
	uint16_t offset,
	uint16_t segment,
	uint16_t repeatList
);

void music_setEffectVibrato(
	int16_t enable,
	int16_t row,
	int16_t steps,
	int16_t depth,
	int16_t speed
);

#endif // GAME_MUSIC_H_INCLUDED
