/** ****************************************************************************
 *
 * \file	driving.h
 *
 * \brief	Driving: the records of the two cars and what is set before a
 * 			drive.
 *
 ******************************************************************************/

#ifndef GAME_DRIVING_H_INCLUDED
#define GAME_DRIVING_H_INCLUDED

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
 * The driving records (0x102 bytes each) in the data segment image.
 */
#define DRIVING_PLAYER	 0x78e0 /**< DS: record of the player's car. */
#define DRIVING_OPPONENT 0x79e2 /**< DS: record of the opponent's car. */

/*
 * Words of a driving record. Names say what the code ported so far shows;
 * "unknown" ones are only set there.
 */
#define DRIVING_SPEED		 0x00 /**< Speed in mph (revs / revs per mph). */
#define DRIVING_STEERING	 0x02 /**< Steering -24..24, 0 unless steered. */
#define DRIVING_WHEEL		 0x04 /**< The same, going back to 0 slowly. */
#define DRIVING_BLOWOUT_AT	 0x0a /**< Where worn tires blow, 0: never. */
#define DRIVING_TIRE_WEAR	 0x0c /**< From DS:0x54b2 by tires and wear. */
#define DRIVING_REVS		 0x0e /**< Engine speed, 1500 at the start. */
#define DRIVING_RANGE		 0x10 /**< Copy of the range in use (4 words). */
#define DRIVING_GEAR		 0x18 /**< Gear, 0: neutral. */
#define DRIVING_GEAR_CHANGED 0x1a /**< 1: the gear is to be drawn. */
#define DRIVING_GEARS		 0x1c /**< From DS:0x5560 by transmission. */
#define DRIVING_TOP_REVS	 0x1e /**< From DS:0x555a by engine. */
#define DRIVING_AUTOMATIC	 0x20 /**< 1 with the first transmission. */
#define DRIVING_SHIFT_REVS	 0x22 /**< Automatic: upper revs of the engine. */
#define DRIVING_SHIFTING	 0x24 /**< Automatic: 1 before a drive. */
#define DRIVING_UNKNOWN_26	 0x26 /**< -1 before a drive. */
#define DRIVING_UNKNOWN_28	 0x28 /**< -1 before a drive. */
#define DRIVING_UNKNOWN_2A	 0x2a /**< Copied to +0x40 before a drive. */
#define DRIVING_GEAR_REVS	 0x34 /**< 5 words: revs per mph by gear. */
#define DRIVING_GRIP		 0x3e /**< From weight and tires (unknown use). */
#define DRIVING_REVS_PER_MPH 0x40 /**< Revs per mph in use. */
#define DRIVING_RANGE_INDEX	 0x42 /**< Which range of the gear is in use. */
#define DRIVING_GEAR_RECORDS 0x44 /**< 5 gear records. */
#define DRIVING_FLAGS		 0xbc /**< What went wrong in a race. */
#define DRIVING_END_TIME	 0xbe /**< DRIVING_TIME when the car stopped. */
#define DRIVING_TIME		 0xc0 /**< Steps of the timer handler in a race. */

/*
 * A gear record: 3 ranges of engine speed.
 */
#define DRIVING_GEAR_SIZE	0x18 /**< Bytes of a gear record. */
#define DRIVING_GEAR_COUNT	5	 /**< Gear records. */
#define DRIVING_RANGE_SIZE	8	 /**< Bytes of a range. */
#define DRIVING_RANGE_COUNT 3	 /**< Ranges of a gear record. */
#define DRIVING_RANGE_FROM	0	 /**< Range: revs it starts at. */
#define DRIVING_RANGE_TO	2	 /**< Range: revs it ends at. */
#define DRIVING_RANGE_PULL	4	 /**< Range: revs gained a step with gas. */
#define DRIVING_RANGE_PULL2 6	 /**< Range: revs lost a step without gas. */

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

void driving_prepare(
	int16_t	 model,
	uint16_t drivingAt
);

void driving_reset(
	uint16_t drivingAt
);

void driving_setupOpponent(void);

int16_t driving_run(void);

void driving_setRaceColors(
	int16_t paint
);

#endif // GAME_DRIVING_H_INCLUDED
