/** ****************************************************************************
 *
 * \file	road.c
 *
 * \brief	The tables of the 3D road (segment 3645).
 *
 * \details	The town tiles and the roadside are made once at start-up
 * 			(FUN_3645_0d48); the route of the country road (bends, hills,
 * 			narrow parts) is laid for every new or loaded game
 * 			(FUN_3645_0b91). The five tables are one array, as in the
 * 			original's segment 489b, so that a piece writing past the
 * 			end of a table lands where it does in the original. The
 * 			order of the random numbers is the original's.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/road.h"

#include "game/exe.h"
#include "game/globals.h"
#include "game/random.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define ROAD_TOWN_SLOPES  0x5c98 /**< DS: 17 words, the hill at tile 0xdc. */
#define ROAD_SHORT_SLOPES 0x5c20 /**< DS: 22 words, hill of pieces 3, 4. */
#define ROAD_LONG_SLOPES  0x5c4c /**< DS: 38 words, hill of pieces 5, 6. */
#define ROAD_PIECE_COUNT  0x5cba /**< DS: s16, pieces of a route (12). */
#define ROAD_ROUTES		  0x5cbc /**< DS: routes, ROAD_ROUTE_SIZE each. */
#define ROAD_ROUTE		  0x5e0c /**< DS: s16, the route (saved). */

#define ROAD_ROUTE_SIZE	  0x18	 /**< Bytes of a route. */
#define ROAD_ROUTE_COUNT  10	 /**< Routes to pick from. */
#define ROAD_TOWN_TILES	  0xb4	 /**< Tiles of the town. */
#define ROAD_TOWN_END	  0x17b	 /**< Tile marked as the end of the town. */
#define ROAD_ROUTE_BASE	  0x17c	 /**< First tile road_build clears. */
#define ROAD_ROUTE_START  0x186	 /**< Tile the first piece starts at. */
#define ROAD_ROAD_END	  0x3fb	 /**< Tile marked as the end of the road. */
#define ROAD_LAST_WRITE	  0x441	 /**< Last tile a bend or hill writes. */
#define ROAD_PLAIN_SIDE	  0x1801 /**< Side flags kept at a marked tile. */
#define ROAD_NO_CROSS	  0x0ff7 /**< Road flags without 0xf000 and 8. */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/*******************************************************************************
;
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

static void road__townTile(
	int16_t tile,
	int16_t slow
);

static void road__countryTile(
	int16_t tile,
	int16_t slow
);

static void road__markRandom(
	int16_t	 range,
	int16_t	 first,
	uint16_t flag
);

static void road__fillCurve(
	int16_t first,
	int16_t count
);

static void road__ramp(
	int16_t steps
);

static void road__hill(
	int16_t	 lead,
	uint16_t slopes,
	int16_t	 count,
	int16_t	 sign,
	int16_t	 tail
);

static void road__bendGrow(void);

static void road__bendShrink(void);

static void road__doubleBendGrow(void);

static void road__doubleBendShrink(void);

static void road__narrowRight(void);

static void road__narrowLeft(void);

static void road__narrowBoth(void);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/**
 * Segment 489b from 0x33f0: the five tables (ROAD_LEFT, ROAD_RIGHT,
 * ROAD_CURVE, ROAD_FLAGS, ROAD_SLOPE), ROAD_TILES words each.
 */
uint16_t road_table[ROAD_WORDS];

static int16_t road__cursor; /**< DS:0x6e00: tile the next piece starts at. */
static int16_t road__curve;	 /**< DS:0x6dfe: curve value at the cursor. */
static int16_t road__step;	 /**< DS:0x6e02: change of the curve per tile. */

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Make the road tables at start-up.
 *
 * \details	Port of FUN_3645_0d48 without its surface pointers (the
 * 			port has one video path). Tiles 0..0xb3 are the town: blocks
 * 			of 16 tiles with a cross street. The rest gets a random
 * 			roadside. Then the fixed marks: the cruise starts, the ends
 * 			of the town and of the road, three tiles with something
 * 			across the road and the hill at tile 0xdc. The route is laid
 * 			last (road_build). The tables are not cleared first: the
 * 			tiles of a cross street are only written to, as in the
 * 			original, where the memory is zero at this point.
 *
 ******************************************************************************/
void road_init(void) {
	int16_t slow = globals_slowMachine;
	int16_t tile;

	for (tile = 0; tile < ROAD_TOWN_TILES; tile++) {
		road_table[ROAD_SLOPE + tile] = 0;
		road_table[ROAD_CURVE + tile] = 0;
		road_table[ROAD_FLAGS + tile] = 0;
		road_table[ROAD_RIGHT + tile] = 0;
		road_table[ROAD_LEFT + tile] = 0;
		road_table[ROAD_FLAGS + tile] |= 0x0001;

		if ((tile + 1) % (slow * 4 + 4) == 0) {
			road_table[ROAD_FLAGS + tile] |= 0x0008;
		}

		if ((tile + 2) % 0x10 == 0) {
			road_table[ROAD_FLAGS + tile + 2] |= 0x0004;
			road_table[ROAD_FLAGS + tile + 1] |= 0x0004;
			road_table[ROAD_FLAGS + tile] |= 0x0004;
			road_table[ROAD_RIGHT + tile - 1] |= 0x0040;
			road_table[ROAD_LEFT + tile + 3] |= 0x0040;

			for (int16_t ahead = 1; ahead <= 3; ahead++) {
				road_table[ROAD_FLAGS + tile + ahead] |= 0x0001;
				road_table[ROAD_SLOPE + tile + ahead] = 0;
				road_table[ROAD_CURVE + tile + ahead] = 0;
			}

			tile += 3;
		} else {
			road__townTile(tile, slow);
		}
	}

	road__markRandom(0x2d, 10, 0x4000);
	road__markRandom(0x2d, 10, 0x8000);
	road__markRandom(0x37, 0x41, 0x4000);
	road__markRandom(0x37, 0x41, 0x8000);
	road__markRandom(0x37, 0x73, 0x4000);
	road__markRandom(0x37, 0x73, 0x8000);
	road_table[ROAD_RIGHT + 0x2c] |= 0x0001;
	road_table[ROAD_LEFT + 0x1d] |= 0x1000;
	road_table[ROAD_LEFT + 0x18] |= 0x0800;
	road_table[ROAD_LEFT + 0x6c] |= 0x0001;
	road_table[ROAD_LEFT + 0x57] |= 0x1000;
	road_table[ROAD_LEFT + 0x52] |= 0x0800;
	road_table[ROAD_LEFT + 0x9c] |= 0x0001;
	road_table[ROAD_LEFT + 0x87] |= 0x1000;
	road_table[ROAD_LEFT + 0x82] |= 0x0800;

	for (tile = ROAD_TOWN_TILES; tile < ROAD_TILES; tile++) {
		road__countryTile(tile, slow);
	}

	road_table[ROAD_LEFT + ROAD_TOWN_END] &= ROAD_PLAIN_SIDE;
	road_table[ROAD_RIGHT + ROAD_TOWN_END] &= ROAD_PLAIN_SIDE;
	road_table[ROAD_FLAGS + ROAD_TOWN_END] |= 0x1010;
	road_table[ROAD_LEFT + ROAD_ROAD_END] &= ROAD_PLAIN_SIDE;
	road_table[ROAD_RIGHT + ROAD_ROAD_END] &= ROAD_PLAIN_SIDE;
	road_table[ROAD_FLAGS + ROAD_ROAD_END] |= 0x2010;

	for (int16_t mark = 0; mark < 3; mark++) {
		int16_t range = mark == 0 ? 0x8c : 600;
		int16_t first = mark == 0 ? 0xdc : ROAD_ROUTE_START;

		tile = (int16_t)(random_next(range) + first);

		if ((road_table[ROAD_RIGHT + tile] & 0x0002) != 0) {
			tile--;
		}

		road_table[ROAD_LEFT + tile] &= ROAD_PLAIN_SIDE;
		road_table[ROAD_RIGHT + tile] &= ROAD_PLAIN_SIDE;
		road_table[ROAD_FLAGS + tile] |= mark == 2 ? 0x4000 : 0x8000;
	}

	tile = 0xdc;

	do {
		for (int16_t index = 0; index < 0x11; index++) {
			uint16_t offset = (uint16_t)(ROAD_TOWN_SLOPES + index * 2);
			int16_t	 slope = exe_readS16(offset);

			road_table[ROAD_SLOPE + tile + index] = (uint16_t)slope;
			road_table[ROAD_SLOPE + tile + 0x25 + index] = (uint16_t)-slope;
		}

		tile += 0x54;
	} while (tile + 0x5a < ROAD_ROUTE_BASE);

	road_table[ROAD_FLAGS + 0x181] |= 0x0100;
	road_build(ROAD_NEW_ROUTE);
}

/** ****************************************************************************
 *
 * \brief	Lay the route of the country road.
 *
 * \param	route			ROAD_NEW_ROUTE: pick one of the 10 routes at
 * 							random; anything else: keep the one at DS:0x5e0c.
 *
 * \details	Port of FUN_3645_0b91. From tile 0x17c on, the curve and the
 * 			slope are cleared and what earlier routes put into the flags
 * 			is removed; the center line is marked again. Then the pieces
 * 			of the route are laid one after the other from tile 0x186,
 * 			and the rest of the road is straight at the last curve value.
 * 			The step of a bend is 3, or 2 on a slow machine.
 *
 ******************************************************************************/
void road_build(
	int16_t route
) {
	int16_t line = (int16_t)(globals_slowMachine * 4 + 4);
	int16_t tile;

	road__cursor = ROAD_ROUTE_START;
	road__curve = 0;
	road__step = globals_slowMachine != 0 ? 2 : 3;

	for (tile = ROAD_ROUTE_BASE; tile < ROAD_TILES; tile++) {
		road_table[ROAD_CURVE + tile] = 0;
		road_table[ROAD_SLOPE + tile] = 0;
	}

	for (tile = ROAD_ROUTE_BASE; tile < ROAD_TILES; tile++) {
		road_table[ROAD_FLAGS + tile] &= 0xf117;
		road_table[ROAD_RIGHT + tile] &= 0x87ff;
		road_table[ROAD_LEFT + tile] &= 0xe7ff;

		if ((tile + 1) % line == 0) {
			road_table[ROAD_FLAGS + tile] |= 0x0008;
		}
	}

	if (route == ROAD_NEW_ROUTE) {
		exe_writeU16(ROAD_ROUTE, (uint16_t)random_next(ROAD_ROUTE_COUNT));
	}

	int16_t	 count = exe_readS16(ROAD_PIECE_COUNT);
	uint16_t pieces = (uint16_t)(ROAD_ROUTES +
								 exe_readS16(ROAD_ROUTE) * ROAD_ROUTE_SIZE);

	for (int16_t index = 0; index < count; index++) {
		switch (exe_readS16((uint16_t)(pieces + index * 2))) {
			case 1:
				road__bendGrow();
				break;
			case 2:
				road__bendShrink();
				break;
			case 3:
				road__hill(0x14, ROAD_SHORT_SLOPES, 0x16, 1, 8);
				break;
			case 4:
				road__hill(10, ROAD_SHORT_SLOPES, 0x16, -1, 0x12);
				break;
			case 5:
				road__hill(6, ROAD_LONG_SLOPES, 0x26, 1, 6);
				break;
			case 6:
				road__hill(8, ROAD_LONG_SLOPES, 0x26, -1, 4);
				break;
			case 7:
				road__narrowRight();
				break;
			case 8:
				road__narrowLeft();
				break;
			case 9:
				road__narrowBoth();
				break;
			case 10:
				road__doubleBendGrow();
				break;
			case 11:
				road__doubleBendShrink();
				break;
			default:
				break;
		}
	}

	if (road__cursor < ROAD_TILES) {
		int16_t first = road__cursor;
		int16_t rest = (int16_t)(ROAD_TILES - road__cursor);

		for (tile = 0; tile < rest; tile++) {
			road_table[ROAD_SLOPE + first + tile] = 0;
		}

		road__cursor += rest;
		road__fillCurve(first, rest);
	}
}

/** ****************************************************************************
 *
 * \brief	Give a tile of the town its roadside.
 *
 * \param	tile			The tile (not one of a cross street).
 * \param	slow			globals_slowMachine.
 *
 * \details	Part of FUN_3645_0d48. At three places of a block of 16
 * 			tiles each side gets flags 4 (at random also 8) on two or
 * 			three tiles and 0x10 on one; elsewhere 0x80. Then objects
 * 			0x20 and 0x100 at random, where the tile has no 0x10.
 *
 ******************************************************************************/
static void road__townTile(
	int16_t tile,
	int16_t slow
) {
	uint16_t * pLeft = &road_table[ROAD_LEFT + tile];
	uint16_t * pRight = &road_table[ROAD_RIGHT + tile];

	if ((tile + 4) % 0x10 == 0 || (tile + 8) % 0x10 == 0 ||
		(tile + 0xc) % 0x10 == 0) {
		if (random_next(2) == 0) {
			pLeft[-1] |= 0x0004;
			pLeft[0] |= 0x0004;

			if (random_next(3) == 0) {
				pLeft[-1] |= 0x0008;
				pLeft[0] |= 0x0008;
			}

			pLeft[0] |= 0x0010;
		} else {
			pLeft[-2] |= 0x0004;
			pLeft[0] |= 0x0004;
			pLeft[-1] |= 0x0004;

			if (random_next(3) == 0) {
				pLeft[-2] |= 0x0008;
				pLeft[0] |= 0x0008;
				pLeft[-1] |= 0x0008;
			}

			pLeft[-1] |= 0x0010;
		}
	} else {
		pLeft[0] |= 0x0080;
	}

	if ((tile + 5) % 0x10 == 0 || (tile + 9) % 0x10 == 0 ||
		(tile + 0xc) % 0x10 == 0) {
		if (random_next(3) == 0) {
			pRight[-2] |= 0x0004;
			pRight[0] |= 0x0004;
			pRight[-1] |= 0x0004;

			if (random_next(4) == 0) {
				pRight[-2] |= 0x0008;
				pRight[0] |= 0x0008;
				pRight[-1] |= 0x0008;
			}

			pRight[-1] |= 0x0010;
		} else {
			pRight[-1] |= 0x0004;
			pRight[0] |= 0x0004;

			if (random_next(2) == 0) {
				pRight[-1] |= 0x0008;
				pRight[0] |= 0x0008;
			}

			pRight[0] |= 0x0010;
		}
	} else {
		pRight[0] |= 0x0080;
	}

	if (random_next((int16_t)(slow * 0x28 + 6)) == 0 &&
		(pLeft[0] & 0x0010) == 0) {
		pLeft[0] |= 0x0020;
	}

	if (random_next((int16_t)(slow * 0x28 + 7)) == 0 &&
		(pRight[0] & 0x0010) == 0) {
		pRight[0] |= 0x0020;
	}

	if (random_next((int16_t)(slow * 0x28 + 3)) == 0 &&
		(pLeft[0] & 0x0010) == 0) {
		pLeft[0] |= 0x0100;
	}

	if (random_next((int16_t)(slow * 0x28 + 4)) == 0 &&
		(pRight[0] & 0x0010) == 0) {
		pRight[0] |= 0x0100;
	}
}

/** ****************************************************************************
 *
 * \brief	Clear a tile outside the town and give it its roadside.
 *
 * \param	tile			The tile (0xb4 and up).
 * \param	slow			globals_slowMachine: makes the roadside sparser.
 *
 * \details	Part of FUN_3645_0d48: the center line, side flag 1 and
 * 			road flag 2 at fixed distances, the rest at random.
 *
 ******************************************************************************/
static void road__countryTile(
	int16_t tile,
	int16_t slow
) {
	uint16_t * pLeft = &road_table[ROAD_LEFT + tile];
	uint16_t * pRight = &road_table[ROAD_RIGHT + tile];
	uint16_t * pFlags = &road_table[ROAD_FLAGS + tile];
	int16_t	   odds = (int16_t)(slow * 100);

	road_table[ROAD_SLOPE + tile] = 0;
	road_table[ROAD_CURVE + tile] = 0;
	*pFlags = 0;
	*pLeft = 0;
	*pRight = 0;

	if ((tile + 1) % (globals_slowMachine * 4 + 4) == 0) {
		*pFlags |= 0x0008;
	}

	if (tile % (slow * 4 + 4) == 0) {
		*pLeft |= 0x0001;
		*pRight |= 0x0001;
	}

	if ((tile + 2) % (slow * 8 + 8) == 0) {
		*pFlags |= 0x0002;
	}

	if (random_next((int16_t)(slow * 200 + 7)) == 0) {
		*pLeft |= 0x0020;
	}

	if (random_next((int16_t)(slow * 200 + 9)) == 0) {
		*pRight |= 0x0020;
	}

	if (slow == 0) {
		if (random_next(0x23) == 0) {
			pLeft[0] = (pLeft[0] | 0x0040) & 0xffdf;
			pLeft[-1] = (pLeft[-1] | 0x0040) & 0xffdf;
			pLeft[-2] = (pLeft[-2] | 0x0040) & 0xffdf;
		}

		if (random_next(0x19) == 0) {
			pRight[0] = (pRight[0] | 0x0040) & 0xffdf;
			pRight[-2] = (pRight[-2] | 0x0040) & 0xffdf;
		}
	} else {
		if (random_next((int16_t)(odds + 0x23)) == 0) {
			*pLeft = (*pLeft | 0x0040) & 0xffdf;
		}

		if (random_next((int16_t)(odds + 0x19)) == 0) {
			*pRight = (*pRight | 0x0040) & 0xffdf;
		}
	}

	if (random_next((int16_t)(odds + 7)) == 0 && (*pLeft & 0x0080) == 0) {
		*pLeft |= 0x0080;
	}

	if (random_next((int16_t)(odds + 9)) == 0 && (*pRight & 0x0080) == 0) {
		*pRight |= 0x0080;
	}

	if (random_next((int16_t)(odds + 0x15)) == 0) {
		*pLeft |= 0x0004;
	}

	if (random_next((int16_t)(odds + 0x19)) == 0) {
		*pRight |= 0x0004;
	}

	if (random_next((int16_t)(odds + 0x17)) == 0 && (*pLeft & 0x0004) == 0) {
		*pLeft |= 0x0008;
	}

	if (random_next((int16_t)(odds + 0x1e)) == 0 && (*pRight & 0x0004) == 0) {
		*pRight |= 0x0008;
	}

	if (random_next((int16_t)(slow * 0x32 + 4)) == 0) {
		*pLeft |= 0x0100;

		if (random_next(2) == 0) {
			*pLeft |= 0x0200;
		}
	}

	if (random_next((int16_t)(slow * 0x32 + 4)) == 0) {
		*pRight |= 0x0100;

		if (random_next(2) == 0) {
			*pRight |= 0x0200;
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Set a road flag at a random tile.
 *
 * \param	range			Number of tiles to pick from.
 * \param	first			First of them.
 * \param	flag			Road flag to set.
 *
 * \details	Part of FUN_3645_0d48 (the tiles after the cruise starts 10,
 * 			0x41 and 0x73).
 *
 ******************************************************************************/
static void road__markRandom(
	int16_t	 range,
	int16_t	 first,
	uint16_t flag
) {
	road_table[ROAD_FLAGS + random_next(range) + first] |= flag;
}

/** ****************************************************************************
 *
 * \brief	Give a run of tiles the current curve value.
 *
 * \param	first			First tile.
 * \param	count			Number of tiles; nothing is written below 1.
 *
 * \details	The fill every piece of a route does (REP STOSW with
 * 			DS:0x6dfe). Near the end of the road it runs on into the
 * 			road flags, as the original does.
 *
 ******************************************************************************/
static void road__fillCurve(
	int16_t first,
	int16_t count
) {
	for (int16_t index = 0; index < count; index++) {
		road_table[ROAD_CURVE + first + index] = (uint16_t)road__curve;
	}
}

/** ****************************************************************************
 *
 * \brief	Change the curve by one step a tile.
 *
 * \param	steps			Number of tiles; negative: the curve shrinks.
 *
 * \details	The loop shared by the bends (FUN_3645_017a, 0293, 03a8,
 * 			0539). The curve is counted in whole steps from its value
 * 			at the cursor; each tile gets a center line piece. Nothing
 * 			is written after tile 0x441, but the count stops there too,
 * 			so the bend is cut short.
 *
 ******************************************************************************/
static void road__ramp(
	int16_t steps
) {
	int16_t direction = steps < 0 ? -1 : 1;
	int16_t count = (int16_t)(road__curve / road__step);
	int16_t limit = (int16_t)(count + steps);
	int16_t value = (int16_t)(count * road__step);
	int16_t tile = road__cursor;

	while (count != limit && tile <= ROAD_LAST_WRITE) {
		road_table[ROAD_FLAGS + tile] |= 0x0008;
		road_table[ROAD_CURVE + tile] = (uint16_t)value;
		value = (int16_t)(value + direction * road__step);
		count = (int16_t)(count + direction);
		tile++;
	}

	road__cursor = tile;
	road__curve = (int16_t)(count * road__step);
}

/** ****************************************************************************
 *
 * \brief	Lay a hill.
 *
 * \param	lead			Flat tiles before the hill.
 * \param	slopes			DS offset of the slope words.
 * \param	count			Number of them.
 * \param	sign			1: as they are; -1: negated.
 * \param	tail			Flat tiles after the hill.
 *
 * \details	Port of FUN_3645_06ca (piece 3), 075d (4), 07f2 (5) and
 * 			0885 (6), which differ in these values only. The slopes are
 * 			not written after tile 0x441.
 *
 ******************************************************************************/
static void road__hill(
	int16_t	 lead,
	uint16_t slopes,
	int16_t	 count,
	int16_t	 sign,
	int16_t	 tail
) {
	int16_t first = road__cursor;
	int16_t tile;

	road__cursor += lead;
	tile = road__cursor;

	for (int16_t index = 0; index < count; index++) {
		if (tile > ROAD_LAST_WRITE) {
			break;
		}

		int16_t slope = exe_readS16((uint16_t)(slopes + index * 2));

		road_table[ROAD_SLOPE + tile] = (uint16_t)(sign * slope);
		tile++;
	}

	road__cursor = (int16_t)(tile + tail);
	road__fillCurve(first, (int16_t)(road__cursor - first));
}

/** ****************************************************************************
 *
 * \brief	Piece 1: a bend where the curve grows over 0x28 tiles.
 *
 * \details	Port of FUN_3645_017a. Road flag 0x200 at the first tile
 * 			and five tiles on, where the roadside is cleared.
 *
 ******************************************************************************/
static void road__bendGrow(void) {
	road__fillCurve(road__cursor, 0x32);
	road_table[ROAD_FLAGS + road__cursor] |= 0x0200;
	road__cursor += 5;
	road_table[ROAD_LEFT + road__cursor] &= ROAD_PLAIN_SIDE;
	road_table[ROAD_RIGHT + road__cursor] &= ROAD_PLAIN_SIDE;
	road_table[ROAD_FLAGS + road__cursor] |= 0x0200;
	road__cursor += 6;
	road__ramp(0x28);
}

/** ****************************************************************************
 *
 * \brief	Piece 2: a bend where the curve shrinks over 0x2d tiles.
 *
 * \details	Port of FUN_3645_0293. Road flag 0x400 at the first tile,
 * 			where the roadside is cleared, and three tiles on.
 *
 ******************************************************************************/
static void road__bendShrink(void) {
	road__fillCurve(road__cursor, 0x32);
	road_table[ROAD_LEFT + road__cursor] &= ROAD_PLAIN_SIDE;
	road_table[ROAD_RIGHT + road__cursor] &= ROAD_PLAIN_SIDE;
	road_table[ROAD_FLAGS + road__cursor] |= 0x0400;
	road__cursor += 3;
	road_table[ROAD_FLAGS + road__cursor] |= 0x0400;
	road__cursor += 3;
	road__ramp(-0x2d);
}

/** ****************************************************************************
 *
 * \brief	Piece 10: the curve grows over 0x28 tiles and shrinks over
 * 			0x2b.
 *
 * \details	Port of FUN_3645_03a8. Right side flag 0x2000 one and
 * 			seven tiles after the first.
 *
 ******************************************************************************/
static void road__doubleBendGrow(void) {
	road__fillCurve(road__cursor, 0x5a);
	road__cursor += 1;
	road_table[ROAD_LEFT + road__cursor] &= ROAD_PLAIN_SIDE;
	road_table[ROAD_RIGHT + road__cursor] &= ROAD_PLAIN_SIDE;
	road_table[ROAD_RIGHT + road__cursor] |= 0x2000;
	road__cursor += 6;
	road_table[ROAD_RIGHT + road__cursor] |= 0x2000;
	road__ramp(0x28);
	road__ramp(-0x2b);
}

/** ****************************************************************************
 *
 * \brief	Piece 11: the curve shrinks over 0x23 tiles and grows over
 * 			0x30.
 *
 * \details	Port of FUN_3645_0539. Right side flag 0x4000 one and
 * 			seven tiles after the first.
 *
 ******************************************************************************/
static void road__doubleBendShrink(void) {
	road__fillCurve(road__cursor, 0x5a);
	road__cursor += 1;
	road_table[ROAD_LEFT + road__cursor] &= ROAD_PLAIN_SIDE;
	road_table[ROAD_RIGHT + road__cursor] &= ROAD_PLAIN_SIDE;
	road_table[ROAD_RIGHT + road__cursor] |= 0x4000;
	road__cursor += 6;
	road_table[ROAD_RIGHT + road__cursor] |= 0x4000;
	road__ramp(-0x23);
	road__ramp(0x30);
}

/** ****************************************************************************
 *
 * \brief	Piece 7: the road is narrow on the right (flag 0x40) for 10
 * 			tiles.
 *
 * \details	Port of FUN_3645_091a. Right side flags 0x800 and 0x1000
 * 			before it; the narrow tiles and the ones around them lose
 * 			the center line and anything across the road.
 *
 ******************************************************************************/
static void road__narrowRight(void) {
	int16_t first = road__cursor;

	road__cursor += 3;
	road_table[ROAD_RIGHT + road__cursor] |= 0x0800;
	road__cursor += 4;
	road_table[ROAD_FLAGS + road__cursor] &= ROAD_NO_CROSS;
	road__cursor++;
	road_table[ROAD_RIGHT + road__cursor] |= 0x1000;
	road__cursor++;

	for (int16_t index = 0; index < 10; index++) {
		road_table[ROAD_FLAGS + road__cursor] |= 0x0040;
		road_table[ROAD_FLAGS + road__cursor] &= ROAD_NO_CROSS;
		road__cursor++;
	}

	road_table[ROAD_FLAGS + road__cursor] &= ROAD_NO_CROSS;
	road__cursor++;
	road__cursor += 3;
	road__fillCurve(first, (int16_t)(road__cursor - first));
}

/** ****************************************************************************
 *
 * \brief	Piece 8: the road is narrow on the left (flag 0x20) for 10
 * 			tiles.
 *
 * \details	Port of FUN_3645_0abb: FUN_3645_091a with the left side
 * 			flags, one tile later and one tile shorter at the end.
 *
 ******************************************************************************/
static void road__narrowLeft(void) {
	int16_t first = road__cursor;

	road__cursor += 4;
	road_table[ROAD_LEFT + road__cursor] |= 0x0800;
	road__cursor += 4;
	road_table[ROAD_FLAGS + road__cursor] &= ROAD_NO_CROSS;
	road__cursor++;
	road_table[ROAD_LEFT + road__cursor] |= 0x1000;
	road__cursor++;

	for (int16_t index = 0; index < 10; index++) {
		road_table[ROAD_FLAGS + road__cursor] |= 0x0020;
		road_table[ROAD_FLAGS + road__cursor] &= ROAD_NO_CROSS;
		road__cursor++;
	}

	road_table[ROAD_FLAGS + road__cursor] &= ROAD_NO_CROSS;
	road__cursor++;
	road__cursor += 2;
	road__fillCurve(first, (int16_t)(road__cursor - first));
}

/** ****************************************************************************
 *
 * \brief	Piece 9: the road is narrow on both sides (flag 0x80) for
 * 			20 tiles.
 *
 * \details	Port of FUN_3645_09f0. Road flag 0x800 at the first tile
 * 			and four tiles on, where the roadside is cleared.
 *
 ******************************************************************************/
static void road__narrowBoth(void) {
	int16_t first = road__cursor;

	road_table[ROAD_FLAGS + road__cursor] |= 0x0800;
	road__cursor += 4;
	road_table[ROAD_LEFT + road__cursor] &= ROAD_PLAIN_SIDE;
	road_table[ROAD_RIGHT + road__cursor] &= ROAD_PLAIN_SIDE;
	road_table[ROAD_FLAGS + road__cursor] |= 0x0800;
	road__cursor += 6;

	for (int16_t index = 0; index < 20; index++) {
		road_table[ROAD_FLAGS + road__cursor] |= 0x0080;
		road_table[ROAD_FLAGS + road__cursor] &= ROAD_NO_CROSS;
		road__cursor++;
	}

	road_table[ROAD_FLAGS + road__cursor] &= 0x0fff;
	road__fillCurve(first, (int16_t)(road__cursor - first));
}
