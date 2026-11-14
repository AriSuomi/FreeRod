/** ****************************************************************************
 *
 * \file	drive.h
 *
 * \brief	The drive itself: dashboard, the loop of a drive and its steps.
 *
 ******************************************************************************/

#ifndef GAME_DRIVE_H_INCLUDED
#define GAME_DRIVE_H_INCLUDED

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

/*
 * Variables of a drive in the data segment image, set by its callers.
 */
#define DRIVE_STATE		0x79a6 /**< s16: step of a drive, 0 at its start. */
#define DRIVE_RACING	0x8aca /**< u8: 1 race, 0 cruise to a place. */
#define DRIVE_AUTOMATIC 0x58e6 /**< s16: 1 with the first transmission. */
#define DRIVE_GO		0x7d66 /**< s16: 1 once "Go !" shows. */

/*
 * Set by cars_draw (DrawCar 1f38:8e48) while a car scrolls.
 */
#define DRIVE_NOT_DRIVEN   0x286  /**< s16: not 0, the car is not driven. */
#define DRIVE_SCROLL_SOUND 0x5a0e /**< s16: the scrolling car sounds. */

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

int16_t drive_run(void);

void drive_timerOn(void);

void drive_timerOff(void);

void drive_crashMark(
	int16_t position
);

void drive_stopCar(
	uint8_t flags
);

#endif // GAME_DRIVE_H_INCLUDED
