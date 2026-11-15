/** ****************************************************************************
 *
 * \file	cars.h
 *
 * \brief	The player's cars and parts: the records and how they are made.
 *
 ******************************************************************************/

#ifndef GAME_CARS_H_INCLUDED
#define GAME_CARS_H_INCLUDED

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <stdint.h>

#include "dos/fpu.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define CARS_DRAW_SCROLL_IN		1 /**< Scroll car in from the left. */
#define CARS_DRAW_SCROLL_RIGHT	2 /**< Scroll car out to the right. */
#define CARS_DRAW_STATIC_GARAGE 3 /**< Draw car in garage, copy screen. */
#define CARS_DRAW_DRIVE_IN		4 /**< Drive car into garage from left. */
#define CARS_DRAW_STICKER		5 /**< Update sticker on the car door. */
#define CARS_DRAW_SCROLL_LEFT	6 /**< Scroll car out to the left. */
#define CARS_DRAW_PREVIEW		7 /**< Draw car in newspaper preview. */
#define CARS_DRAW_UPDATE_GARAGE 8 /**< Redraw car after body mod. */

#define CARS_BACKGROUND_PAGE	6 /**< Page a moving car is drawn over. */

/*
 * DS:0x5a24: 1 makes cars_draw drive the car in and on out to the right
 * (a car passing by on the street); cars_draw sets it to 2 while the car
 * leaves and to 0 when done.
 */
#define CARS_PASSING		   0x5a24

#define CARS_FLAG_FRONT_BUMPER 1 /**< Front bumper removed. */
#define CARS_FLAG_REAR_BUMPER  2 /**< Rear bumper removed. */
#define CARS_FLAG_ROOF		   4 /**< Roof chopped. */
#define CARS_FLAG_BLOWER	   8 /**< Supercharger blower fitted. */

#define CARS_MOD_ROOF		   0x2000 /**< Car +0x24: chopped roof. */
#define CARS_MOD_REAR		   0x4000 /**< Car +0x24: rear removed. */
#define CARS_MOD_FRONT		   0x8000 /**< Car +0x24: front removed. */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/**
 * What cars_measure finds out about a car.
 */
typedef struct cars_setup_tag {
	int16_t	 model;	  /**< Its model. */
	int		 gearbox; /**< Subtype of its transmission. */
	int		 engine;  /**< Subtype of its engine. */
	int		 tires;	  /**< Subtype of its tires. */
	fpu_Real factor;  /**< 0.7..1: how good its parts are. */
	fpu_Real speed;	  /**< Top speed in mph, at most 130. */
} cars_Setup;

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

uint16_t cars_allocCar(void);

void cars_freeCar(
	uint16_t car
);

uint16_t cars_allocPart(void);

void cars_freePart(
	uint16_t part
);

uint16_t cars_partBasePrice(
	uint16_t part
);

uint16_t cars_disposePart(
	uint16_t part,
	int16_t	 keepSpare
);

int16_t cars_destroyCar(
	uint16_t car,
	int16_t	 keepParts
);

uint16_t cars_create(
	int16_t	 model,
	int16_t	 price,
	uint16_t car
);

void cars_setPart(
	uint16_t record,
	int16_t	 part,
	int		 kind
);

void cars_addSparePart(
	int16_t part,
	int16_t price
);

void cars_setPaint(
	int16_t paint
);

uint8_t cars_drawFlags(
	uint16_t car
);

void cars_draw(
	int16_t	 mode,
	int16_t	 model,
	uint8_t	 flags,
	int16_t	 sticker,
	int16_t	 hoodOpen,
	int16_t	 bottomY,
	int16_t	 foreground,
	uint16_t tireTable
);

void cars_restoreDoor(void);

void cars_drawOnWork(void);

void cars_drawOnWorkPart(
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height
);

void cars_drawForeground(
	int16_t left,
	int16_t right
);

void cars_applySticker(
	int16_t sticker
);

void cars_waitress(
	int16_t leaving,
	int16_t here
);

int16_t cars_isComplete(
	uint16_t car,
	int16_t	 checkRunningGear
);

void cars_measure(
	int16_t		 model,
	cars_Setup * pSetup
);

int16_t cars_maxSpeed(
	int16_t model
);

uint16_t cars_takeSparePart(
	uint16_t part
);

void cars_fitPart(
	uint16_t part,
	uint16_t slotAt
);

void cars_removePart(
	uint16_t slotAt
);

void cars_buildForTires(
	int16_t			   model,
	uint8_t			   flags,
	int16_t			   sticker,
	plat_ImageHandle * pCar,
	plat_ImageHandle * pShadow,
	int16_t *		   pWidth,
	int16_t *		   pHeight
);

plat_ImageHandle cars_buildWithTires(
	int16_t	 model,
	uint8_t	 flags,
	int16_t	 sticker,
	uint16_t tireTable
);

plat_ImageHandle cars_tireImage(
	int16_t	  tireId,
	bool	  well,
	int16_t * pWidth,
	int16_t * pHeight
);

#endif // GAME_CARS_H_INCLUDED
