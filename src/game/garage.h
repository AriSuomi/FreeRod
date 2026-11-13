/** ****************************************************************************
 *
 * \file	garage.h
 *
 * \brief	The garage: the screen the game is played from.
 *
 ******************************************************************************/

#ifndef GAME_GARAGE_H_INCLUDED
#define GAME_GARAGE_H_INCLUDED

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

#define GARAGE_KEEP		  0		/**< garage_show: garage on screen. */
#define GARAGE_DRAW		  1		/**< garage_show: draw it. */
#define GARAGE_WIPE_IN	  2		/**< garage_show: draw and wipe in. */
#define GARAGE_SWITCH_CAR 3		/**< garage_show: switch active car. */
#define GARAGE_GAME_END	  0x29a /**< garage_show: game is over. */

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

int16_t garage_show(
	int16_t mode
);

void garage_clearCarName(void);

void garage_sellParts(void);

int16_t garage_ownedCars(void);

void garage_paint(void);

void garage_modifyBody(
	uint8_t partBit
);

void garage_stickers(void);

void garage_carInfo(void);

void garage_gearbox(void);

void garage_tires(void);

void garage_listSpares(
	int8_t	   kind,
	uint16_t * pParts,
	int8_t *   pPercents,
	int16_t	   first
);

#endif // GAME_GARAGE_H_INCLUDED
