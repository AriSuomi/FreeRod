/** ****************************************************************************
 *
 * \file	road.h
 *
 * \brief	The tables of the 3D road (segment 3645).
 *
 * \details	One word per tile in five tables, which the original keeps
 * 			one after the other in segment 489b. Meaning of the flag
 * 			bits: docs/road.md.
 *
 ******************************************************************************/

#ifndef GAME_ROAD_H_INCLUDED
#define GAME_ROAD_H_INCLUDED

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

#define ROAD_TILES	   0x460 /**< Tiles of the road. */

#define ROAD_LEFT	   0				/**< 489b:33f0: left side flags. */
#define ROAD_RIGHT	   (1 * ROAD_TILES) /**< 489b:3cb0: right side flags. */
#define ROAD_CURVE	   (2 * ROAD_TILES) /**< 489b:4570: sideways shift. */
#define ROAD_FLAGS	   (3 * ROAD_TILES) /**< 489b:4e30: road flags. */
#define ROAD_SLOPE	   (4 * ROAD_TILES) /**< 489b:56f0: slope. */

#define ROAD_SLACK	   0x100 /**< Words kept after the slope table. */
#define ROAD_WORDS	   (5 * ROAD_TILES + ROAD_SLACK)

#define ROAD_NEW_ROUTE 100 /**< road_build: pick a route at random. */

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

extern uint16_t road_table[ROAD_WORDS];

/*******************************************************************************
;
;	F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

void road_init(void);

void road_build(
	int16_t route
);

#endif // GAME_ROAD_H_INCLUDED
