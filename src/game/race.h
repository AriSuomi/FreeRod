/** ****************************************************************************
 *
 * \file	race.h
 *
 * \brief	After a race on the street: the result, the money, the car.
 *
 ******************************************************************************/

#ifndef GAME_RACE_H_INCLUDED
#define GAME_RACE_H_INCLUDED

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

/*
 * What race_result returns: how the player leaves the race.
 */
#define RACE_HOME	1 /**< Drives home. */
#define RACE_TOWED	2 /**< Goes home without driving (no car or broken). */
#define RACE_STREET 3 /**< Back on the street. */
#define RACE_JAILED 4 /**< Sentenced for debts. */

#define RACE_WON	0x7676 /**< s16 DS offset: 1 when the player won. */

/** Value of race_testFlags when no outcome was given. */
#define RACE_NO_TEST -1

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

extern int32_t race_testFlags;
extern int16_t race_testSpeed;

/*******************************************************************************
;
;	F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

bool race_testOutcome(void);

int16_t race_result(void);

#endif // GAME_RACE_H_INCLUDED
