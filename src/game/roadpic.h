/** ****************************************************************************
 *
 * \file	roadpic.h
 *
 * \brief	The pictures of the 3D road (segment 3645).
 *
 ******************************************************************************/

#ifndef GAME_ROADPIC_H_INCLUDED
#define GAME_ROADPIC_H_INCLUDED

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include <stdint.h>

#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define ROADPIC_PAGE	   10	  /**< Page that stands in for DS:0x82b4. */
#define ROADPIC_POSITIONS  13	  /**< Pictures of a car, by position. */
#define ROADPIC_RACE_START 0x7aae /**< s16: height of the start picture. */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/** A picture in memory (a far pointer to a picture buffer). */
typedef struct roadpic_picture_tag {
	int16_t			 gameId;  /**< Resource it comes from. */
	int16_t			 width;	  /**< Width (+0 of the buffer). */
	int16_t			 height;  /**< Height (+2 of the buffer). */
	plat_ImageHandle image;	  /**< Image drawn for it, 0 for none. */
	uint8_t *		 pPixels; /**< Pixels of a scaled copy, else NULL. */
} roadpic_Picture;

/** The pictures FUN_3645_2429 keeps for the drive. */
typedef struct roadpic_set_tag {
	const roadpic_Picture * pOverA;		 /**< DS:0x72f4: 0x3ee (3e3c). */
	const roadpic_Picture * pOverB;		 /**< DS:0x8bca: 0x3ef (3e3c). */
	const roadpic_Picture * pMirror;	 /**< DS:0x8184: 0x3f1. */
	const roadpic_Picture * pMirrorBack; /**< DS:0x8b7e: 0x4db (3edb). */
	const roadpic_Picture * pRaceStart;	 /**< DS:0x6e60: 0x3f5 (3dc3). */

	/** DS:0x8ace: the opponent's car by position (3f8d). */
	const roadpic_Picture * pOpponent[ROADPIC_POSITIONS];

	/** DS:0x7526: the police car, the same way. */
	const roadpic_Picture * pPolice[ROADPIC_POSITIONS];
} roadpic_Set;

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

extern roadpic_Set roadpic_set;

/*******************************************************************************
;
;	F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

void roadpic_load(
	int view
);

#endif // GAME_ROADPIC_H_INCLUDED
