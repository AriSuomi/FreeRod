/** ****************************************************************************
 *
 * \file	roadstate.c
 *
 * \brief	The state of the 3D road during a drive (segment 3645).
 *
 * \details	The start of a drive (FUN_3645_2114), a frame of the road
 * 			(FUN_3645_213d) with the projection of the rows ahead and in
 * 			the mirror (FUN_3645_1e9e), the move to the next tile
 * 			(FUN_3645_1efc) and the check of the car against the
 * 			roadside (3645:206b). All arithmetic is 16-bit, as in the
 * 			original. Drawing the view (FUN_3645_4b8c) is not ported
 * 			yet (step 14o).
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/roadstate.h"

#include <stdbool.h>

#include "dos/dos.h"
#include "game/driving.h"
#include "game/exe.h"
#include "game/globals.h"
#include "game/random.h"
#include "game/roaddraw.h"
#include "game/road.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define ROADSTATE_RIVAL_SIDE   0x54d4 /**< s16: opponent's side, 1. */
#define ROADSTATE_SHOULDER	   0x5e0e /**< s16: 1 = on the shoulder. */
#define ROADSTATE_DRIFT_RIGHT  0x5e10 /**< s16: cruise: drifting right. */
#define ROADSTATE_DRIFT_LEFT   0x5e12 /**< s16: cruise: drifting left. */
#define ROADSTATE_LAST_SPOT	   0x6e2a /**< s16: opponent's last position. */
#define ROADSTATE_LEFT_AHEAD   0x70c0 /**< s16: left edge 4 tiles ahead. */
#define ROADSTATE_TIRES_BLOWN  0x787c /**< u8: the tires are blown. */
#define ROADSTATE_SIGN_HEIGHT  0x7aae /**< s16: height of picture 0x3f5. */
#define ROADSTATE_RIGHT_AHEAD  0x7d2e /**< s16: right edge 4 tiles ahead. */
#define ROADSTATE_TILE		   0x7d30 /**< s16: tile of the player. */
#define ROADSTATE_EIGHTHS	   0x7d32 /**< s16: eighths of a tile (0..7). */
#define ROADSTATE_SPEED		   0x7d34 /**< s16: eighths per frame. */
#define ROADSTATE_X			   0x7d36 /**< s16: player's X. */
#define ROADSTATE_EYE		   0x7d38 /**< s16: eye height (Y grows down). */
#define ROADSTATE_UNKNOWN_7D3A 0x7d3a /**< s16: cleared at the start. */
#define ROADSTATE_HEADING	   0x7d3c /**< s16: curve the view is turned to. */
#define ROADSTATE_SLOPE		   0x7d3e /**< s16: slope at the player's tile. */
#define ROADSTATE_CAR_SPOT	   0x7d7c /**< s16: opponent's position. */
#define ROADSTATE_ATTACK	   0x7d7e /**< s16: opponent may ram, 1. */
#define ROADSTATE_LEFT_EDGE	   0x7d82 /**< s16: left edge at the player. */
#define ROADSTATE_RIVAL_X	   0x7e90 /**< s16: opponent's X. */
#define ROADSTATE_KIND		   0x7ec0 /**< s16: 0 drag race, 1 road race. */
#define ROADSTATE_WEAVE		   0x8648 /**< s16: opponent may change side. */
#define ROADSTATE_RACING	   0x8aca /**< u8: 1 in a race, 0 in a cruise. */
#define ROADSTATE_RIGHT_EDGE   0x8bd0 /**< s16: right edge at the player. */

#define ROADSTATE_RACE_TILE	   200	 /**< Start tile of a race. */
#define ROADSTATE_START_X	   0xa00 /**< X at the start; lane 1. */
#define ROADSTATE_OTHER_LANE   0x820 /**< Lane 2, after the finish. */
#define ROADSTATE_LANE_SWITCH  0x960 /**< Opponent's X above it: lane 2. */
#define ROADSTATE_LANE_STEP	   100	 /**< Most X moves to a lane at once. */
#define ROADSTATE_EYE_HEIGHT   0x76c /**< Eye height. */
#define ROADSTATE_GROUND	   2000	 /**< Height of the road. */
#define ROADSTATE_LEFT_SIDE	   0x5dc /**< Left edge of the road. */
#define ROADSTATE_RIGHT_SIDE   0xce4 /**< Right edge of the road. */
#define ROADSTATE_NARROW_LEFT  2000	 /**< Left edge of a narrow road. */
#define ROADSTATE_NARROW_RIGHT 0xaf0 /**< Right edge of a narrow road. */
#define ROADSTATE_LEFT_LINE	   0x44c /**< Left object line. */
#define ROADSTATE_RIGHT_LINE   0xe74 /**< Right object line. */
#define ROADSTATE_DRAG_END	   0x17c /**< Last tile of a drag race. */
#define ROADSTATE_ROAD_END	   0x3fc /**< Last tile of a road race. */
#define ROADSTATE_WHEEL_LIMIT  0x18	 /**< Most the wheel turns in a bend. */
#define ROADSTATE_FRAME_TICKS  9	 /**< Timer ticks of a frame. */
#define ROADSTATE_BLOWN_DRIFT  0x19	 /**< X added a frame, blown tires. */
#define ROADSTATE_PULL_RANGE   200	 /**< Steering pulls the heading below. */
#define ROADSTATE_DRIFT_TURN   3	 /**< Heading change a frame, drifting. */
#define ROADSTATE_CRASH_SPAN   0x104 /**< Beyond an edge by more: crash. */
#define ROADSTATE_CAR_LEFT	   0x1e	 /**< Car's left side: X less this. */
#define ROADSTATE_CAR_RIGHT	   0x46	 /**< Car's right side: X plus this. */

#define ROADSTATE_FLAG_NARROW  0x00e0 /**< Road flags: narrow road. */
#define ROADSTATE_FLAG_BOTH	   0x0080 /**< Road flags: narrow on both sides. */
#define ROADSTATE_FLAG_LEFT	   0x0020 /**< Road flags: narrow on the left. */
#define ROADSTATE_CROSSING	   0x0001 /**< Road flags: crossing. */
#define ROADSTATE_ACROSS	   0xf000 /**< Road flags: across the road. */
#define ROADSTATE_RIGHT_SOLID  0xf002 /**< Road flags solid at the right. */
#define ROADSTATE_SIDE_SOLID   0x1000 /**< Side flags: solid object 6. */
#define ROADSTATE_TURN_OFF	   0x0001 /**< Side flags: cruise turns off. */
#define ROADSTATE_LEFT_TURN	   7	  /**< Tiles ahead the left turn-off. */
#define ROADSTATE_RIGHT_TURN   6	  /**< Tiles ahead the right turn-off. */

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

static int16_t roadstate__read(
	uint16_t offset
);

static void roadstate__write(
	uint16_t offset,
	int16_t	 value
);

static uint16_t roadstate__word(
	uint16_t index
);

static void roadstate__edges(
	uint16_t  flags,
	int16_t * pLeft,
	int16_t * pRight
);

static int16_t roadstate__aheadColumn(
	int16_t distance,
	int16_t worldX,
	int16_t turn
);

static int16_t roadstate__aheadRow(
	int16_t distance,
	int16_t worldY,
	int16_t rise
);

static int16_t roadstate__behindColumn(
	int16_t distance,
	int16_t worldX,
	int16_t turn
);

static int16_t roadstate__behindRow(
	int16_t distance,
	int16_t worldY,
	int16_t rise
);

static void roadstate__projectAhead(
	int16_t row,
	int16_t distance
);

static void roadstate__projectBehind(
	int16_t row,
	int16_t distance
);

static void roadstate__ahead(void);

static void roadstate__behind(void);

static void roadstate__setUp(void);

static void roadstate__show(void);

static void roadstate__drift(
	int16_t direction
);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/** FUN_3645_171d: where the table pointers point. */
roadstate_Cursor roadstate_cursor;

/**
 * FUN_3645_18fb: the rows ahead (DS:0x7884 road, 0x7ab0 eye, 0x72f8
 * left, 0x7ace right, 0x7f8a and 0x7fd8 object lines).
 */
roadstate_Rows roadstate_ahead;

/**
 * FUN_3645_1bcc: the rows in the mirror (DS:0x8638 road, 0x864a eye,
 * 0x7f22 left, 0x865a right, 0x70c2 and 0x70d2 object lines).
 */
roadstate_Rows roadstate_behind;

/** The view (FUN_3645_4b8c) was logged as not ported. */
static bool roadstate__viewLogged;

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Point the table pointers at a tile.
 *
 * \param	tile			Tile of the road.
 *
 * \details	Port of FUN_3645_171d.
 *
 ******************************************************************************/
void roadstate_point(
	int16_t tile
) {
	roadstate_cursor.left = (uint16_t)(ROAD_LEFT + tile);
	roadstate_cursor.right = (uint16_t)(ROAD_RIGHT + tile);
	roadstate_cursor.flags = (uint16_t)(ROAD_FLAGS + tile);
	roadstate_cursor.slope = (uint16_t)(ROAD_SLOPE + tile);
	roadstate_cursor.curve = (uint16_t)(ROAD_CURVE + tile);
}

/** ****************************************************************************
 *
 * \brief	Start the road for a drive.
 *
 * \details	Port of FUN_3645_2114 with FUN_3645_185d and FUN_3645_527a.
 * 			A race starts at tile 200, a cruise at the start tile of
 * 			its town (DS:0x284, globals_place; any other value keeps
 * 			the tile). The car is on the wide road at X 0xa00, looking
 * 			straight. Then the opponent's position is cleared, the
 * 			page to draw to is chosen (roaddraw_selectPage) and the
 * 			first frame is made (FUN_3645_1e9e).
 *
 ******************************************************************************/
void roadstate_start(void) {
	roadstate__write(ROADSTATE_DRIFT_LEFT, 0);
	roadstate__write(ROADSTATE_DRIFT_RIGHT, 0);

	if (*exe_data(ROADSTATE_RACING) == 0) {
		switch (globals_place) {
			case 1:
				roadstate__write(ROADSTATE_TILE, 10);
				break;

			case 2:
				roadstate__write(ROADSTATE_TILE, 0x41);
				break;

			case 3:
				roadstate__write(ROADSTATE_TILE, 0x73);
				break;

			default:
				break;
		}
	} else {
		roadstate__write(ROADSTATE_TILE, ROADSTATE_RACE_TILE);
	}

	roadstate_point(roadstate__read(ROADSTATE_TILE));
	roadstate__write(ROADSTATE_LEFT_EDGE, ROADSTATE_LEFT_SIDE);
	roadstate__write(ROADSTATE_RIGHT_EDGE, ROADSTATE_RIGHT_SIDE);
	roadstate__write(ROADSTATE_EIGHTHS, 0);
	roadstate__write(ROADSTATE_X, ROADSTATE_START_X);
	roadstate__write(ROADSTATE_EYE, ROADSTATE_EYE_HEIGHT);
	roadstate__write(ROADSTATE_UNKNOWN_7D3A, 0);
	roadstate__write(ROADSTATE_SLOPE, 0);
	roadstate__write(ROADSTATE_HEADING, 0);
	roadstate__write(ROADSTATE_SPEED, 0);
	roadstate__write(ROADSTATE_RIVAL_SIDE, 1);
	roadstate__write(ROADSTATE_WEAVE, random_next(2) == 0 ? 1 : 0);
	roadstate__write(ROADSTATE_ATTACK, 1);

	roadstate__write(ROADSTATE_LAST_SPOT, 0);
	roadstate__write(ROADSTATE_CAR_SPOT, 0);
	roaddraw_selectPage();
	roadstate__show();
}

/** ****************************************************************************
 *
 * \brief	Make a frame of the road.
 *
 * \details	Port of FUN_3645_213d. While steered (and moving) and the
 * 			view is less than 200 from the steering, the heading turns
 * 			with it. A cruise runs at 4 eighths a frame (6 on a slow
 * 			machine). Once moving, picture 0x3f5 shrinks by 4 rows a
 * 			frame. After the frame is shown (FUN_3645_1e9e), the car
 * 			drifts sideways by the difference of heading and curve
 * 			times its speed and the record's +0xc (unless in a demo
 * 			or with the auto switch); blown tires instead pull it 0x19
 * 			to the right. A cruise moves the car itself and drifts it
 * 			off the road at a turn-off seen 7 tiles ahead on the left
 * 			or 6 on the right.
 *
 ******************************************************************************/
void roadstate_frame(void) {
	int16_t steering = roadstate__read(DRIVING_PLAYER + DRIVING_STEERING);
	int16_t speed = roadstate__read(DRIVING_PLAYER + DRIVING_SPEED);
	bool	racing = *exe_data(ROADSTATE_RACING) != 0;
	int16_t heading = roadstate__read(ROADSTATE_HEADING);

	if (steering != 0 && speed != 0) {
		int16_t curve = (int16_t)roadstate__word(roadstate_cursor.curve);
		int16_t apart = (int16_t)(heading - curve - steering);

		if (apart < 0) {
			apart = (int16_t)-apart;
		}

		if (apart < ROADSTATE_PULL_RANGE) {
			heading = (int16_t)(heading - ((steering >> 1) + 1));
			roadstate__write(ROADSTATE_HEADING, heading);
		}
	}

	if (!racing) {
		roadstate__write(ROADSTATE_SPEED, globals_slowMachine == 0 ? 4 : 6);
	}

	int16_t height = roadstate__read(ROADSTATE_SIGN_HEIGHT);

	if (roadstate__read(ROADSTATE_SPEED) > 0 && height > 0) {
		roadstate__write(ROADSTATE_SIGN_HEIGHT, (int16_t)(height - 4));
	}

	roadstate__show();

	if (globals_demoMode == 0 && globals_autoSwitch == 0) {
		int16_t carX = roadstate__read(ROADSTATE_X);

		if (*exe_data(ROADSTATE_TIRES_BLOWN) == 0) {
			if (roadstate__read(DRIVING_PLAYER + DRIVING_SPEED) != 0) {
				int16_t curve =
					(int16_t)roadstate__word(roadstate_cursor.curve);
				int16_t off =
					(int16_t)(roadstate__read(ROADSTATE_HEADING) - curve);
				int16_t grip =
					roadstate__read(DRIVING_PLAYER + DRIVING_TIRE_WEAR);
				int16_t pace =
					(int16_t)(roadstate__read(ROADSTATE_SPEED) + grip);

				carX = (int16_t)(carX - ((int16_t)(off * pace) >> 2));
				roadstate__write(ROADSTATE_X, carX);
			}
		} else {
			carX = (int16_t)(carX + ROADSTATE_BLOWN_DRIFT);
			roadstate__write(ROADSTATE_X, carX);
		}
	}

	if (racing) {
		return;
	}

	int16_t eighths = (int16_t)(roadstate__read(ROADSTATE_EIGHTHS) +
								roadstate__read(ROADSTATE_SPEED));

	roadstate__write(ROADSTATE_EIGHTHS, eighths);

	if (eighths > 7) {
		roadstate_advance();
	}

	int16_t tile = roadstate__read(ROADSTATE_TILE);

	if ((roadstate__word((uint16_t)(ROAD_LEFT + tile + ROADSTATE_LEFT_TURN)) &
		 ROADSTATE_TURN_OFF) != 0 ||
		roadstate__read(ROADSTATE_DRIFT_LEFT) != 0) {
		roadstate__write(ROADSTATE_DRIFT_LEFT, 1);
		roadstate__drift(1);
	}

	if ((roadstate__word((uint16_t)(ROAD_RIGHT + tile + ROADSTATE_RIGHT_TURN)) &
		 ROADSTATE_TURN_OFF) != 0 ||
		roadstate__read(ROADSTATE_DRIFT_RIGHT) != 0) {
		roadstate__drift(-1);
		roadstate__write(ROADSTATE_DRIFT_RIGHT, 1);
	}
}

/** ****************************************************************************
 *
 * \brief	Move the player to the tile the eighths have reached.
 *
 * \details	Port of FUN_3645_1efc. Whole tiles leave the eighths; the
 * 			road edges at the tile and 4 tiles ahead follow its road
 * 			flags, the slope that of the tile. In a demo, with the auto
 * 			switch or past the end of the race (tile 0x17c in a drag
 * 			race, else 0x3fc), the car also changes to the lane away
 * 			from the opponent (X 0xa00, or 0x820 when the opponent's X
 * 			is above 0x960), at most 100 off it, and the view follows
 * 			the road: when the curve changed, the steering wheel turns
 * 			by twice the change (at most 0x18) and the heading becomes
 * 			the curve.
 *
 ******************************************************************************/
void roadstate_advance(void) {
	int16_t eighths = roadstate__read(ROADSTATE_EIGHTHS);
	int16_t tile = (int16_t)(roadstate__read(ROADSTATE_TILE) + (eighths >> 3));
	int16_t left;
	int16_t right;

	roadstate__write(ROADSTATE_TILE, tile);
	roadstate_point(tile);
	roadstate__write(ROADSTATE_EIGHTHS, (int16_t)(eighths & 7));

	roadstate__edges(roadstate__word(roadstate_cursor.flags), &left, &right);
	roadstate__write(ROADSTATE_LEFT_EDGE, left);
	roadstate__write(ROADSTATE_RIGHT_EDGE, right);
	roadstate__edges(
		roadstate__word((uint16_t)(roadstate_cursor.flags + 4)),
		&left,
		&right
	);
	roadstate__write(ROADSTATE_LEFT_AHEAD, left);
	roadstate__write(ROADSTATE_RIGHT_AHEAD, right);
	roadstate__write(
		ROADSTATE_SLOPE,
		(int16_t)roadstate__word(roadstate_cursor.slope)
	);

	if (globals_demoMode == 0 && globals_autoSwitch == 0) {
		int16_t last = ROADSTATE_ROAD_END;

		if (roadstate__read(ROADSTATE_KIND) == 0) {
			last = ROADSTATE_DRAG_END;
		}

		if (tile <= last) {
			return;
		}
	}

	int16_t lane = ROADSTATE_OTHER_LANE;

	if (roadstate__read(ROADSTATE_RIVAL_X) <= ROADSTATE_LANE_SWITCH) {
		lane = ROADSTATE_START_X;
	}

	int16_t carX = roadstate__read(ROADSTATE_X);
	int16_t apart = (int16_t)(carX - lane);

	if (apart < 0) {
		apart = (int16_t)-apart;
	}

	if (apart > ROADSTATE_LANE_STEP) {
		if (lane < carX) {
			lane = (int16_t)(lane + ROADSTATE_LANE_STEP);
		} else {
			lane = (int16_t)(lane - ROADSTATE_LANE_STEP);
		}
	}

	roadstate__write(ROADSTATE_X, lane);

	int16_t curve = (int16_t)roadstate__word(roadstate_cursor.curve);
	int16_t heading = roadstate__read(ROADSTATE_HEADING);

	if (curve != heading) {
		int16_t wheel = (int16_t)((heading - curve) * 2);

		if (wheel > ROADSTATE_WHEEL_LIMIT) {
			wheel = ROADSTATE_WHEEL_LIMIT;
		} else if (wheel < -ROADSTATE_WHEEL_LIMIT) {
			wheel = -ROADSTATE_WHEEL_LIMIT;
		}

		roadstate__write(DRIVING_PLAYER + DRIVING_WHEEL, wheel);
	}

	roadstate__write(ROADSTATE_HEADING, curve);
}

/** ****************************************************************************
 *
 * \brief	Check the car against the roadside.
 *
 * \return	1 when the car crashed, else 0.
 *
 * \details	Port of 3645:206b, which the timer handler of a race calls.
 * 			The car spans X - 0x1e to X + 0x46. More than 0x104 beyond
 * 			an edge is a crash. Beyond the left edge it is a crash when
 * 			the left side flag 0x1000 is set at the tile or the one
 * 			before, or road flags 0xf000 at the next tile; beyond the
 * 			right edge the same with the right side flags and road
 * 			flags 0xf002. Otherwise beyond an edge the car is on the
 * 			shoulder (DS:0x5e0e = 1). The tables are read by the tile
 * 			(DS:0x7d30), not through the pointers.
 *
 ******************************************************************************/
int16_t roadstate_check(void) {
	int16_t	 carX = roadstate__read(ROADSTATE_X);
	int16_t	 carLeft = (int16_t)(carX - ROADSTATE_CAR_LEFT);
	int16_t	 carRight = (int16_t)(carX + ROADSTATE_CAR_RIGHT);
	int16_t	 leftEdge = roadstate__read(ROADSTATE_LEFT_EDGE);
	int16_t	 rightEdge = roadstate__read(ROADSTATE_RIGHT_EDGE);
	uint16_t tile = (uint16_t)roadstate__read(ROADSTATE_TILE);
	uint16_t solid;
	uint16_t side;

	roadstate__write(ROADSTATE_SHOULDER, 0);

	if ((int16_t)(leftEdge - ROADSTATE_CRASH_SPAN) >= carLeft ||
		carRight >= (int16_t)(rightEdge + ROADSTATE_CRASH_SPAN)) {
		return 1;
	}

	if (leftEdge > carLeft) {
		side = ROAD_LEFT;
		solid = ROADSTATE_ACROSS;
	} else if (carRight > rightEdge) {
		side = ROAD_RIGHT;
		solid = ROADSTATE_RIGHT_SOLID;
	} else {
		return 0;
	}

	if ((roadstate__word((uint16_t)(side + tile)) & ROADSTATE_SIDE_SOLID) !=
			0 ||
		(roadstate__word((uint16_t)(side + tile - 1)) &
		 ROADSTATE_SIDE_SOLID) != 0 ||
		(roadstate__word((uint16_t)(ROAD_FLAGS + tile + 1)) & solid) != 0) {
		return 1;
	}

	roadstate__write(ROADSTATE_SHOULDER, 1);

	return 0;
}

/** ****************************************************************************
 *
 * \brief	Read a word of the data segment image as signed.
 *
 * \param	offset			DS offset.
 *
 * \return	The word.
 *
 ******************************************************************************/
static int16_t roadstate__read(
	uint16_t offset
) {
	return exe_readS16(offset);
}

/** ****************************************************************************
 *
 * \brief	Write a signed word to the data segment image.
 *
 * \param	offset			DS offset.
 * \param	value			The word.
 *
 ******************************************************************************/
static void roadstate__write(
	uint16_t offset,
	int16_t	 value
) {
	exe_writeU16(offset, (uint16_t)value);
}

/** ****************************************************************************
 *
 * \brief	Read a word of the road tables.
 *
 * \param	index			Index in road_table.
 *
 * \return	The word; 0 beyond the tables.
 *
 * \details	The original reads on into segment 489b; the routes end
 * 			before tile 1000, so this only happens far past the end
 * 			of a race.
 *
 ******************************************************************************/
static uint16_t roadstate__word(
	uint16_t index
) {
	if (index >= ROAD_WORDS) {
		return 0;
	}

	return road_table[index];
}

/** ****************************************************************************
 *
 * \brief	The road edges a tile's road flags give.
 *
 * \param	flags			Road flags of the tile.
 * \param	pLeft			Output: X of the left edge.
 * \param	pRight			Output: X of the right edge.
 *
 * \details	Flag 0x80 narrows both sides, 0x20 the left, otherwise
 * 			0x40 the right; as written out in FUN_3645_18fb, 1bcc and
 * 			1efc.
 *
 ******************************************************************************/
static void roadstate__edges(
	uint16_t  flags,
	int16_t * pLeft,
	int16_t * pRight
) {
	*pLeft = ROADSTATE_LEFT_SIDE;
	*pRight = ROADSTATE_RIGHT_SIDE;

	if ((flags & ROADSTATE_FLAG_NARROW) == 0) {
		return;
	}

	if ((flags & ROADSTATE_FLAG_BOTH) != 0) {
		*pLeft = ROADSTATE_NARROW_LEFT;
		*pRight = ROADSTATE_NARROW_RIGHT;
	} else if ((flags & ROADSTATE_FLAG_LEFT) != 0) {
		*pLeft = ROADSTATE_NARROW_LEFT;
	} else {
		*pRight = ROADSTATE_NARROW_RIGHT;
	}
}

/** ****************************************************************************
 *
 * \brief	Screen column of a point ahead.
 *
 * \param	distance		Distance ahead in eighths of a tile.
 * \param	worldX			X of the point.
 * \param	turn			Heading less the curve at the point.
 *
 * \return	The column.
 *
 * \details	Port of FUN_3645_176f.
 *
 ******************************************************************************/
static int16_t roadstate__aheadColumn(
	int16_t distance,
	int16_t worldX,
	int16_t turn
) {
	int16_t shift = (int16_t)(distance * turn);
	int16_t apart =
		(int16_t)(roadstate__read(ROADSTATE_X) - (worldX + (shift >> 2)));

	if (distance == 0) {
		distance = 1;
	}

	return (int16_t)(0xa0 - (int16_t)(apart * 4) / distance);
}

/** ****************************************************************************
 *
 * \brief	Screen row of a point ahead.
 *
 * \param	distance		Distance ahead in eighths of a tile.
 * \param	worldY			Height of the point (Y grows down).
 * \param	rise			Slope at the player less that at the point.
 *
 * \return	The row.
 *
 * \details	Port of FUN_3645_17b1.
 *
 ******************************************************************************/
static int16_t roadstate__aheadRow(
	int16_t distance,
	int16_t worldY,
	int16_t rise
) {
	int16_t lift = (int16_t)(distance * rise);
	int16_t below =
		(int16_t)((worldY + lift) - roadstate__read(ROADSTATE_EYE));

	if (distance == 0) {
		distance = 1;
	}

	return (int16_t)((int16_t)(below * 4) / distance + 0x3c);
}

/** ****************************************************************************
 *
 * \brief	Column of a point behind in the mirror.
 *
 * \param	distance		Distance behind in eighths of a tile.
 * \param	worldX			X of the point.
 * \param	turn			Curve at the point less the heading.
 *
 * \return	The column.
 *
 * \details	Port of FUN_3645_17ed.
 *
 ******************************************************************************/
static int16_t roadstate__behindColumn(
	int16_t distance,
	int16_t worldX,
	int16_t turn
) {
	int16_t shift = (int16_t)(distance * turn);
	int16_t apart =
		(int16_t)(roadstate__read(ROADSTATE_X) - (worldX + (shift >> 2)));

	if (distance == 0) {
		distance = 1;
	}

	return (int16_t)(0x118 - apart / distance);
}

/** ****************************************************************************
 *
 * \brief	Row of a point behind in the mirror.
 *
 * \param	distance		Distance behind in eighths of a tile.
 * \param	worldY			Height of the point (Y grows down).
 * \param	rise			Slope at the point less that at the player.
 *
 * \return	The row.
 *
 * \details	Port of FUN_3645_1828.
 *
 ******************************************************************************/
static int16_t roadstate__behindRow(
	int16_t distance,
	int16_t worldY,
	int16_t rise
) {
	int16_t lift = (int16_t)(distance * rise);
	int16_t below =
		(int16_t)((worldY + lift) - roadstate__read(ROADSTATE_EYE));

	if (distance == 0) {
		distance = 1;
	}

	return (int16_t)(below / distance + 0x24);
}

/** ****************************************************************************
 *
 * \brief	Project the tile the pointers point at as a row ahead and
 * 			move them on.
 *
 * \param	row				Row of roadstate_ahead to fill.
 * \param	distance		Its distance in eighths of a tile.
 *
 * \details	One row of FUN_3645_18fb. The row at eye height is only
 * 			made at a crossing or where something is across the road;
 * 			otherwise the row keeps its value.
 *
 ******************************************************************************/
static void roadstate__projectAhead(
	int16_t row,
	int16_t distance
) {
	uint16_t flags = roadstate__word(roadstate_cursor.flags++);
	bool	 high = (flags & (ROADSTATE_CROSSING | ROADSTATE_ACROSS)) != 0;
	int16_t	 slope = (int16_t)roadstate__word(roadstate_cursor.slope++);
	int16_t	 curve = (int16_t)roadstate__word(roadstate_cursor.curve++);
	int16_t	 rise = (int16_t)(roadstate__read(ROADSTATE_SLOPE) - slope);
	int16_t	 turn = (int16_t)(roadstate__read(ROADSTATE_HEADING) - curve);
	int16_t	 left;
	int16_t	 right;

	roadstate__edges(flags, &left, &right);
	roadstate_ahead.road[row] =
		roadstate__aheadRow(distance, ROADSTATE_GROUND, rise);
	roadstate_ahead.right[row] =
		roadstate__aheadColumn(distance, right, turn);
	roadstate_ahead.left[row] = roadstate__aheadColumn(distance, left, turn);
	roadstate_ahead.rightLine[row] =
		roadstate__aheadColumn(distance, ROADSTATE_RIGHT_LINE, turn);
	roadstate_ahead.leftLine[row] =
		roadstate__aheadColumn(distance, ROADSTATE_LEFT_LINE, turn);

	if (high) {
		roadstate_ahead.eye[row] =
			roadstate__aheadRow(distance, ROADSTATE_EYE_HEIGHT, rise);
	}
}

/** ****************************************************************************
 *
 * \brief	Project the tile the pointers point at as a row in the
 * 			mirror and move them back.
 *
 * \param	row				Row of roadstate_behind to fill.
 * \param	distance		Its distance in eighths of a tile.
 *
 * \details	One row of FUN_3645_1bcc; see roadstate__projectAhead.
 *
 ******************************************************************************/
static void roadstate__projectBehind(
	int16_t row,
	int16_t distance
) {
	uint16_t flags = roadstate__word(roadstate_cursor.flags--);
	bool	 high = (flags & (ROADSTATE_CROSSING | ROADSTATE_ACROSS)) != 0;
	int16_t	 slope = (int16_t)roadstate__word(roadstate_cursor.slope--);
	int16_t	 curve = (int16_t)roadstate__word(roadstate_cursor.curve--);
	int16_t	 rise = (int16_t)(slope - roadstate__read(ROADSTATE_SLOPE));
	int16_t	 turn = (int16_t)(curve - roadstate__read(ROADSTATE_HEADING));
	int16_t	 left;
	int16_t	 right;

	roadstate__edges(flags, &left, &right);
	roadstate_behind.road[row] =
		roadstate__behindRow(distance, ROADSTATE_GROUND, rise);
	roadstate_behind.right[row] =
		roadstate__behindColumn(distance, right, turn);
	roadstate_behind.left[row] =
		roadstate__behindColumn(distance, left, turn);
	roadstate_behind.rightLine[row] =
		roadstate__behindColumn(distance, ROADSTATE_RIGHT_LINE, turn);
	roadstate_behind.leftLine[row] =
		roadstate__behindColumn(distance, ROADSTATE_LEFT_LINE, turn);

	if (high) {
		roadstate_behind.eye[row] =
			roadstate__behindRow(distance, ROADSTATE_EYE_HEIGHT, rise);
	}
}

/** ****************************************************************************
 *
 * \brief	Project the 15 rows ahead.
 *
 * \details	Port of FUN_3645_18fb: the player's tile at distance 4,
 * 			then the next 14 tiles from 12 less the eighths on, 8 a
 * 			tile.
 *
 ******************************************************************************/
static void roadstate__ahead(void) {
	int16_t distance = (int16_t)(0xc - roadstate__read(ROADSTATE_EIGHTHS));

	roadstate__projectAhead(0, 4);

	for (int16_t row = 1; row < ROADSTATE_AHEAD_ROWS; row++) {
		roadstate__projectAhead(row, distance);
		distance = (int16_t)(distance + 8);
	}
}

/** ****************************************************************************
 *
 * \brief	Project the 8 rows in the mirror.
 *
 * \details	Port of FUN_3645_1bcc: the player's tile at distance 8,
 * 			then the 7 tiles before it from the eighths plus 8 on, 8 a
 * 			tile.
 *
 ******************************************************************************/
static void roadstate__behind(void) {
	int16_t distance = roadstate__read(ROADSTATE_EIGHTHS);

	roadstate__projectBehind(0, 8);

	for (int16_t row = 1; row < ROADSTATE_BEHIND_ROWS; row++) {
		distance = (int16_t)(distance + 8);
		roadstate__projectBehind(row, distance);
	}
}

/** ****************************************************************************
 *
 * \brief	Project the rows of the frame.
 *
 * \details	First part of FUN_3645_1e9e: the pointers are set to the
 * 			player's tile before each projection and after both.
 *
 ******************************************************************************/
static void roadstate__setUp(void) {
	int16_t tile = roadstate__read(ROADSTATE_TILE);

	roadstate_point(tile);
	roadstate__ahead();
	roadstate_point(tile);
	roadstate__behind();
	roadstate_point(tile);
}

/** ****************************************************************************
 *
 * \brief	Project, draw and pace a frame of the road.
 *
 * \details	Port of FUN_3645_1e9e. The rows are projected
 * 			(roadstate__setUp), the view is drawn (FUN_3645_4b8c, not
 * 			ported yet: logged once) and the frame waits until 9 timer
 * 			ticks have passed since the projection.
 *
 ******************************************************************************/
static void roadstate__show(void) {
	roadstate__setUp();

	uint32_t until = globals_ticks + ROADSTATE_FRAME_TICKS;

	if (!roadstate__viewLogged) {
		roadstate__viewLogged = true;
		plat_log("road: the view (FUN_3645_4b8c) is not ported yet");
	}

	while ((int32_t)(globals_ticks - until) < 0) {
		dos_service();
	}
}

/** ****************************************************************************
 *
 * \brief	Turn the view while a cruise drifts off the road.
 *
 * \param	direction		1: to the left turn-off, else to the right.
 *
 * \details	Port of FUN_3645_2126: the heading changes by 3.
 *
 ******************************************************************************/
static void roadstate__drift(
	int16_t direction
) {
	int16_t heading = roadstate__read(ROADSTATE_HEADING);

	if (direction == 1) {
		heading = (int16_t)(heading + ROADSTATE_DRIFT_TURN);
	} else {
		heading = (int16_t)(heading - ROADSTATE_DRIFT_TURN);
	}

	roadstate__write(ROADSTATE_HEADING, heading);
}
