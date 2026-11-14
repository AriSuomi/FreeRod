/** ****************************************************************************
 *
 * \file	roadview.c
 *
 * \brief	The view ahead of the 3D road (segment 3645).
 *
 * \details	A frame of the road as seen through the windshield
 * 			(FUN_3645_4b8c): the backdrop (FUN_3645_2982), then the rows
 * 			from the farthest to the nearest with their ground
 * 			(roaddraw_ground) and the objects at the roadside and
 * 			across the road (FUN_3645_3ccc), the two pictures over the
 * 			view (FUN_3645_3e3c) and the start lights of a race
 * 			(FUN_3645_3dc3); then the view is shown (roaddraw_show).
 * 			The opponent's car (FUN_3645_3f8d, step 14p) and the mirror
 * 			(FUN_3645_43fd, step 14q) are not ported yet. All
 * 			arithmetic is 16-bit, as in the original.
 *
 * 			The port takes the path of the adapters without a split
 * 			screen (see roaddraw.c): DS:0x5e3e stays 1, so everything
 * 			is drawn on the view surface DS:0x8188 (roaddraw_view), and
 * 			the switches of the EGA write mode (3645:0008, 001d) have
 * 			no part in it.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/roadview.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "game/drive.h"
#include "game/exe.h"
#include "game/road.h"
#include "game/roaddraw.h"
#include "game/roadpic.h"
#include "game/roadstate.h"
#include "game/screen.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define ROADVIEW__CLIP_BOTTOM 0x5e52 /**< s16: clip box, bottom row. */
#define ROADVIEW__LINE_WIDTHS 0x5e6a /**< s16 by row: center line width. */
#define ROADVIEW__CREST		  0x6e04 /**< s16: highest road row on screen. */
#define ROADVIEW__CREST_ROW	  0x6e06 /**< s16: row index where it is. */
#define ROADVIEW__HEADING	  0x7d3c /**< s16: curve the view is turned to. */
#define ROADVIEW__SLOPE		  0x7d3e /**< s16: slope at the player's tile. */
#define ROADVIEW__AUTO_MAY	  0x8228 /**< s16: set once "Go !" shows. */
#define ROADVIEW__RACING	  0x8aca /**< s16: 1 in a race, 0 in a cruise. */

#define ROADVIEW__FARTHEST	  14	 /**< Index of the farthest row. */
#define ROADVIEW__OBJECT_ROWS 0xb	 /**< Rows below this draw objects. */
#define ROADVIEW__TOP		  0x12	 /**< First row of the view. */
#define ROADVIEW__SKY_TOP	  0x66	 /**< Row of the stored sky. */
#define ROADVIEW__SKY_ROWS	  0x1d	 /**< Cruise: rows of sky copied. */
#define ROADVIEW__SKY_END	  0x9a	 /**< Race: row after the sky's last. */
#define ROADVIEW__SKY_ABOVE	  0x1e	 /**< Race: sky rows = horizon - this. */
#define ROADVIEW__LAND_TOP	  0xa3	 /**< Row of the stored ground. */
#define ROADVIEW__LAND_ROWS	  0x10	 /**< Cruise: rows of ground copied. */
#define ROADVIEW__LAND_RACE	  0x14	 /**< Race: rows of ground copied. */
#define ROADVIEW__LAND_AT	  0x2f	 /**< Cruise: row the ground goes to. */
#define ROADVIEW__LAND_ABOVE  0xc	 /**< Race: ground starts this above. */
#define ROADVIEW__FILL_AT	  0x3f	 /**< Cruise: first row filled. */
#define ROADVIEW__FILL_BELOW  8		 /**< Race: fill starts this below. */
#define ROADVIEW__FAR_AWAY	  0xf0	 /**< Distance of the race's horizon. */
#define ROADVIEW__GROUND	  2000	 /**< Height of the road. */
#define ROADVIEW__ROUND		  0xfff8 /**< Shifts are whole bytes. */

#define ROADVIEW__OVER_B_LEFT 0x58 /**< Column of picture 0x3ef. */
#define ROADVIEW__OVER_B_TOP  0x5f /**< Row of picture 0x3ef. */
#define ROADVIEW__LIGHT_WIDTH 0x18 /**< Width of a half of picture 0x3f5. */
#define ROADVIEW__LIGHT_ROWS  0x20 /**< Height of picture 0x3f5. */
#define ROADVIEW__LIGHT_LEFT  0x82 /**< Column it is drawn at. */

#define ROADVIEW__CROSSING	  0x0001 /**< Road flag: crossing. */
#define ROADVIEW__NOT_DRAWN	  0x0004 /**< Road flag: row not drawn. */
#define ROADVIEW__ACROSS	  0xf000 /**< Road flags: across the road. */
#define ROADVIEW__ACROSS_D	  0x1000 /**< Across: object 0xd. */
#define ROADVIEW__ACROSS_1	  0x2000 /**< Across: strip picture 1. */
#define ROADVIEW__ACROSS_C	  0x4000 /**< Across: object 0xc / strip 2. */

#define ROADVIEW__ROAD_COLOR  1	 /**< Cruise: fill below the ground. */
#define ROADVIEW__RACE_COLOR  9	 /**< Race: fill below the crest. */
#define ROADVIEW__BAR_COLOR	  10 /**< Posts and bar across the road. */

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
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

static uint16_t roadview__word(
	uint16_t index
);

static void roadview__copy(
	const roaddraw_Rect * pRect
);

static void roadview__fill(
	int16_t row,
	uint8_t color
);

static void roadview__shift(
	int16_t	  factor,
	int16_t * pWidth,
	int16_t * pLeft
);

static void roadview__backdrop(void);

static void roadview__setRow(
	int16_t distance
);

static void roadview__across(
	int16_t	 left,
	int16_t	 right,
	int16_t	 top,
	int16_t	 bottom,
	int16_t	 distance,
	bool	 clipLeft,
	uint16_t flags
);

static int16_t roadview__objects(
	int16_t distance
);

static void roadview__overlays(void);

static void roadview__startLights(void);

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Draw and show the view ahead.
 *
 * \details	Port of FUN_3645_4b8c. The clip box is set to the view and
 * 			the backdrop is drawn. Then the rows 14 to 1, farthest
 * 			first: the ground of the rows not beyond the crest of a
 * 			hill (DS:0x6e06) and, on the rows below 11 that are drawn
 * 			(road flag 4 clear), the objects. The original walks the
 * 			rows with the pointers of roaddraw_walk, one row nearer
 * 			each step; the port sets them from the row's index. Last
 * 			the pictures over the view, in a race the start lights,
 * 			and the view is shown.
 *
 * 			FUN_3645_2f48 reads a stack slot that holds this
 * 			function's SI (see docs/road.md, "Unset stack slots"): the
 * 			value the objects of an earlier row left there
 * 			(roadview__objects). Before the first such row it is the
 * 			caller's SI, taken as 0; the value only matters on rows
 * 			below 6, which rows with objects precede unless five
 * 			rows in a row are not drawn.
 *
 * 			Not ported yet: the opponent's car (FUN_3645_3f8d, on the
 * 			row of its tile and after the rows when it is on the
 * 			player's tile; step 14p) and the mirror (FUN_3645_43fd,
 * 			before the pictures; step 14q).
 *
 ******************************************************************************/
void roadview_draw(void) {
	int16_t kept = 0;

	roaddraw_clipView();
	roadview__backdrop();

	for (int16_t distance = ROADVIEW__FARTHEST; distance > 0; distance--) {
		roadview__setRow(distance);

		if (distance <= exe_readS16(ROADVIEW__CREST_ROW)) {
			roaddraw_ground(distance, kept);
		}

		uint16_t flags = roadview__word(roaddraw_walk.flags);

		if ((flags & ROADVIEW__NOT_DRAWN) == 0 &&
			distance < ROADVIEW__OBJECT_ROWS) {
			kept = roadview__objects(distance);
		}
	}

	roadview__overlays();

	if (exe_readS16(ROADVIEW__RACING) != 0) {
		roadview__startLights();
	}

	roaddraw_show();
}

/** ****************************************************************************
 *
 * \brief	Read a word of the road tables.
 *
 * \param	index			Index of road_table.
 *
 * \return	The word; 0 outside the tables.
 *
 * \details	Not part of the original, which reads whatever lies after
 * 			a table.
 *
 ******************************************************************************/
static uint16_t roadview__word(
	uint16_t index
) {
	if (index >= ROAD_WORDS) {
		return 0;
	}

	return road_table[index];
}

/** ****************************************************************************
 *
 * \brief	Copy a rectangle of the view to another place on it.
 *
 * \param	pRect			The rectangle.
 *
 * \details	Video table entry 0 (DS:0x78a2), plain copy, from the view
 * 			surface to itself. A rectangle without rows or columns is
 * 			not copied.
 *
 ******************************************************************************/
static void roadview__copy(
	const roaddraw_Rect * pRect
) {
	if (pRect->width <= 0 || pRect->height <= 0) {
		return;
	}

	plat_blit(
		roaddraw_view,
		pRect->sourceLeft,
		pRect->sourceTop,
		roaddraw_view,
		pRect->left,
		pRect->top,
		pRect->width,
		pRect->height
	);
}

/** ****************************************************************************
 *
 * \brief	Fill the view from a row down to the clip box's bottom.
 *
 * \param	row				First row filled.
 * \param	color			Color.
 *
 * \details	Port of 3645:00e7 (video table entry 14, DS:0x78da, of the
 * 			EGA): nothing when the row is below the bottom row of the
 * 			clip box; otherwise the whole rows from it to the one
 * 			before the bottom row are filled. A row above the screen
 * 			(the original then writes outside the page) starts at the
 * 			screen's top.
 *
 ******************************************************************************/
static void roadview__fill(
	int16_t row,
	uint8_t color
) {
	int16_t bottom = exe_readS16(ROADVIEW__CLIP_BOTTOM);

	if (bottom < row) {
		return;
	}

	if (row < 0) {
		row = 0;
	}

	plat_selectPage(roaddraw_view);
	plat_fillRect(0, row, PLAT_WIDTH, bottom - row, color);
}

/** ****************************************************************************
 *
 * \brief	Split the width of the backdrop by the heading.
 *
 * \param	factor			Columns the backdrop moves for a heading of 1.
 * \param	pWidth			Output: width of the part that starts at the
 * 							backdrop's left edge.
 * \param	pLeft			Output: column that part is drawn at; also the
 * 							width of the rest, drawn at column 0.
 *
 * \details	Part of FUN_3645_2982: the heading times the factor, modulo
 * 			0x140 and rounded down to 8. A heading above 0 moves the
 * 			backdrop to the right by that, otherwise to the left by
 * 			that of its magnitude.
 *
 ******************************************************************************/
static void roadview__shift(
	int16_t	  factor,
	int16_t * pWidth,
	int16_t * pLeft
) {
	int16_t heading = exe_readS16(ROADVIEW__HEADING);

	if (heading < 1) {
		int16_t magnitude = (int16_t)(heading < 0 ? -heading : heading);
		int16_t moved = (int16_t)(magnitude * factor);

		*pWidth = (int16_t)((moved % PLAT_WIDTH) & ROADVIEW__ROUND);
		*pLeft = (int16_t)(PLAT_WIDTH - *pWidth);
	} else {
		int16_t moved = (int16_t)(heading * factor);

		*pLeft = (int16_t)((moved % PLAT_WIDTH) & ROADVIEW__ROUND);
		*pWidth = (int16_t)(PLAT_WIDTH - *pLeft);
	}
}

/** ****************************************************************************
 *
 * \brief	Draw the backdrop of the view.
 *
 * \details	Port of FUN_3645_2982. The sky and the ground are stored
 * 			on the view surface below the view (roadpic_load: rows
 * 			from 0x66 and from 0xa3) and copied into it.
 *
 * 			Cruise: 0x1d rows of sky to row 0x12, 0x10 rows of ground
 * 			to row 0x2f, the rows from 0x3f filled with color 1; the
 * 			crest is row index 14.
 *
 * 			Race: the horizon is the row of the road 0xf0 eighths
 * 			ahead with the slope at the player (FUN_3645_17b1). The
 * 			sky ends 0xc rows above it (its last stored row is 0x99)
 * 			and is moved sideways by twice the heading; the ground,
 * 			0x14 rows from there clipped to the view, by four times.
 * 			Both wrap around, so each is copied in two parts. The
 * 			crest (DS:0x6e04, 0x6e06) is the highest road row of the
 * 			rows 14..1, starting from 8 rows below the horizon at row
 * 			index 14; from it down the view is filled with color 9.
 *
 ******************************************************************************/
static void roadview__backdrop(void) {
	bool		  racing = exe_readS16(ROADVIEW__RACING) != 0;
	int16_t		  horizon = 0;
	int16_t		  width = PLAT_WIDTH;
	int16_t		  left = 0;
	roaddraw_Rect rect;

	if (!racing) {
		rect.height = ROADVIEW__SKY_ROWS;
		rect.sourceTop = ROADVIEW__SKY_TOP;
	} else {
		horizon = roadstate_aheadRow(
			ROADVIEW__FAR_AWAY,
			ROADVIEW__GROUND,
			exe_readS16(ROADVIEW__SLOPE)
		);
		rect.height = (int16_t)(horizon - ROADVIEW__SKY_ABOVE);
		rect.sourceTop = (int16_t)(ROADVIEW__SKY_END - rect.height);
		roadview__shift(2, &width, &left);
	}

	rect.width = width;
	rect.sourceLeft = 0;
	rect.left = left;
	rect.top = ROADVIEW__TOP;
	roadview__copy(&rect);

	if (racing) {
		rect.width = left;
		rect.sourceLeft = width;
		rect.left = 0;
		roadview__copy(&rect);
	}

	rect.sourceTop = ROADVIEW__LAND_TOP;

	if (!racing) {
		rect.height = ROADVIEW__LAND_ROWS;
		rect.top = ROADVIEW__LAND_AT;
	} else {
		rect.height = ROADVIEW__LAND_RACE;
		rect.top = (int16_t)(horizon - ROADVIEW__LAND_ABOVE);
		roadview__shift(4, &width, &left);
	}

	rect.width = width;
	rect.sourceLeft = 0;
	rect.left = left;
	roaddraw_clipRows(&rect);
	roadview__copy(&rect);
	exe_writeU16(ROADVIEW__CREST_ROW, ROADVIEW__FARTHEST);

	if (!racing) {
		roadview__fill(ROADVIEW__FILL_AT, ROADVIEW__ROAD_COLOR);

		return;
	}

	rect.width = left;
	rect.sourceLeft = width;
	rect.left = 0;
	roadview__copy(&rect);

	int16_t crest = (int16_t)(horizon + ROADVIEW__FILL_BELOW);
	int16_t crestRow = ROADVIEW__FARTHEST;

	for (int16_t row = ROADVIEW__FARTHEST; row > 0; row--) {
		if (roadstate_ahead.road[row] < crest) {
			crest = roadstate_ahead.road[row];
			crestRow = row;
		}
	}

	exe_writeU16(ROADVIEW__CREST_ROW, (uint16_t)crestRow);
	exe_writeU16(ROADVIEW__CREST, (uint16_t)crest);
	roadview__fill(crest, ROADVIEW__RACE_COLOR);
}

/** ****************************************************************************
 *
 * \brief	Point the draw loops' pointers at a row ahead.
 *
 * \param	distance		Index of the row (1 nearest, 14 farthest).
 *
 * \details	Start of FUN_3645_4b8c and the end of its loop: the row
 * 			(DS:0x6e08..) and the one nearer (DS:0x6e10..) in the
 * 			projected rows, the center line width DS:0x5e6a[row] and
 * 			the flags of the tile that many tiles ahead.
 *
 ******************************************************************************/
static void roadview__setRow(
	int16_t distance
) {
	const roadstate_Rows * pRows = &roadstate_ahead;
	roaddraw_Row *		   pFar = &roaddraw_walk.farther;
	roaddraw_Row *		   pNear = &roaddraw_walk.nearer;
	int					   nearer = distance - 1;

	pFar->pLeft = &pRows->left[distance];
	pFar->pRight = &pRows->right[distance];
	pFar->pRoad = &pRows->road[distance];
	pFar->pLeftLine = &pRows->leftLine[distance];
	pFar->pRightLine = &pRows->rightLine[distance];
	pFar->pEye = &pRows->eye[distance];

	pNear->pLeft = &pRows->left[nearer];
	pNear->pRight = &pRows->right[nearer];
	pNear->pRoad = &pRows->road[nearer];
	pNear->pLeftLine = &pRows->leftLine[nearer];
	pNear->pRightLine = &pRows->rightLine[nearer];
	pNear->pEye = &pRows->eye[nearer];

	roaddraw_walk.lineWidth =
		(uint16_t)(ROADVIEW__LINE_WIDTHS + distance * 2);
	roaddraw_walk.left = (uint16_t)(roadstate_cursor.left + distance);
	roaddraw_walk.right = (uint16_t)(roadstate_cursor.right + distance);
	roaddraw_walk.flags = (uint16_t)(roadstate_cursor.flags + distance);
}

/** ****************************************************************************
 *
 * \brief	Draw what stands across the road on a row.
 *
 * \param	left			Column of the left edge.
 * \param	right			Column of the right edge.
 * \param	top				Row of its top.
 * \param	bottom			Row of the road.
 * \param	distance		Index of the row.
 * \param	clipLeft		Clip a strip at the left column of the box too.
 * \param	flags			Road flags & 0xf000 of the tile.
 *
 * \details	Port of FUN_3645_3ccc. As many times as the row's center
 * 			line width plus 1 (DS:0x5e6a), counting down: a post
 * 			(vertical line, color 10) that far outside each edge from
 * 			the top to the road, and a span between the edges that
 * 			many rows less 1 below the top. Then in the middle at the
 * 			top: with 0x1000 object 0xd; with 0x2000 strip picture 1;
 * 			otherwise object 0xc (0x4000) or 0xe, at a crossing strip
 * 			picture 2 (0x4000) or 3 instead.
 *
 ******************************************************************************/
static void roadview__across(
	int16_t	 left,
	int16_t	 right,
	int16_t	 top,
	int16_t	 bottom,
	int16_t	 distance,
	bool	 clipLeft,
	uint16_t flags
) {
	int16_t width = (int16_t)(exe_readS16(roaddraw_walk.lineWidth) + 1);
	int16_t middle = (int16_t)((int16_t)(left + right) >> 1);

	for (; width > 0; width--) {
		int16_t leftPost = (int16_t)(left - width);
		int16_t rightPost = (int16_t)(right + width);
		int16_t bar = (int16_t)(top + width - 1);

		roaddraw_verticalLine(ROADVIEW__BAR_COLOR, leftPost, top, bottom);
		roaddraw_verticalLine(ROADVIEW__BAR_COLOR, rightPost, top, bottom);
		roaddraw_span(left, right, bar, ROADVIEW__BAR_COLOR);
	}

	bool crossing =
		(roadview__word(roaddraw_walk.flags) & ROADVIEW__CROSSING) != 0;
	bool marked = (flags & ROADVIEW__ACROSS_C) != 0;

	if ((flags & ROADVIEW__ACROSS_D) != 0) {
		roaddraw_object(middle, top, distance, 1, 0xd);
	} else if ((flags & ROADVIEW__ACROSS_1) != 0) {
		roaddraw_strip(
			middle,
			top,
			distance,
			0,
			clipLeft,
			ROADDRAW_STRIP_LOWER
		);
	} else if (!crossing) {
		roaddraw_object(middle, top, distance, 1, marked ? 0xc : 0xe);
	} else {
		roaddraw_strip(
			middle,
			top,
			distance,
			0,
			clipLeft,
			marked ? ROADDRAW_STRIP_A : ROADDRAW_STRIP_B
		);
	}
}

/** ****************************************************************************
 *
 * \brief	Draw the objects of a row ahead.
 *
 * \param	distance		Index of the row (1 nearest).
 *
 * \return	The value of the original's SI after the row (see
 * 			roadview_draw): the road flags, or what an object computed
 * 			with it loaded last (a column of an object line, or the
 * 			road's row).
 *
 * \details	Body of the loop of FUN_3645_4b8c for a row that is drawn.
 * 			Objects are indexes of DS:0x6078, placed at the road's row
 * 			on an edge of the road or an object line, side -1 mirrored
 * 			(roaddraw_object); the bits are listed in docs/road.md.
 * 			Left side flags first (0x20, 0x100, 0x800, 0x1000, 1), then
 * 			outside a crossing the roadside objects of both sides and
 * 			object 9 of road flag 2, at a crossing the shapes of flag
 * 			0x40 (roaddraw_crossObject); then the right side flags,
 * 			what stands across the road (road flags 0xf000) and the
 * 			objects of the road flags 0x100, 0x800, 0x200, 0x400. At a
 * 			crossing the objects of the flags 0x20 and 0x100 stand on
 * 			the edge instead of the object line, and flag 1 draws
 * 			strip picture 1 halfway between eye height and the road.
 *
 ******************************************************************************/
static int16_t roadview__objects(
	int16_t distance
) {
	const roaddraw_Row * pFar = &roaddraw_walk.farther;

	uint16_t flags = roadview__word(roaddraw_walk.flags);
	uint16_t left = roadview__word(roaddraw_walk.left);
	uint16_t right = roadview__word(roaddraw_walk.right);
	bool	 crossing = (flags & ROADVIEW__CROSSING) != 0;
	int16_t	 road = *pFar->pRoad;
	int16_t	 leftEdge = *pFar->pLeft;
	int16_t	 rightEdge = *pFar->pRight;
	int16_t	 leftLine = *pFar->pLeftLine;
	int16_t	 rightLine = *pFar->pRightLine;
	int16_t	 middle = (int16_t)((int16_t)(*pFar->pEye + road) >> 1);
	int16_t	 kept = (int16_t)flags;
	int16_t	 column;

	if ((left & 0x20) != 0) {
		column = crossing ? leftEdge : leftLine;
		roaddraw_object(column, road, distance, -1, 0);
	}

	if ((left & 0x100) != 0) {
		if (crossing) {
			column = leftEdge;
		} else if ((left & 0x200) == 0) {
			column = leftLine;
		} else {
			kept = leftLine;
			column = (int16_t)(leftLine - leftEdge + leftLine);
		}

		roaddraw_object(column, road, distance, -1, 4);
	}

	if ((left & 0x800) != 0) {
		roaddraw_object(leftEdge, road, distance, 1, 3);
	}

	if ((left & 0x1000) != 0) {
		roaddraw_object(leftEdge, road, distance, 1, 6);
	}

	if ((left & 1) != 0) {
		if (!crossing) {
			roaddraw_object(leftEdge, road, distance, 1, 1);
		} else {
			roaddraw_strip(
				leftEdge,
				middle,
				distance,
				1,
				false,
				ROADDRAW_STRIP_LOWER
			);
		}
	}

	if (!crossing) {
		if ((left & 4) != 0) {
			roaddraw_object(leftEdge, road, distance, -1, 7);
		}

		if ((left & 8) != 0) {
			roaddraw_object(leftEdge, road, distance, -1, 8);
		}

		if ((left & 0x40) != 0) {
			int16_t half = (int16_t)((int16_t)(leftEdge - leftLine) >> 1);

			kept = leftLine;
			column = (int16_t)(leftLine - half);
			roaddraw_object(column, road, distance, -1, 10);
		}

		if ((left & 0x80) != 0) {
			roaddraw_object(leftLine, road, distance, -1, 0xb);
		}

		if ((right & 4) != 0) {
			roaddraw_object(rightEdge, road, distance, 1, 7);
		}

		if ((right & 8) != 0) {
			roaddraw_object(rightEdge, road, distance, 1, 8);
		}

		if ((right & 0x40) != 0) {
			roaddraw_object(rightLine, road, distance, 1, 10);
		}

		if ((right & 0x80) != 0) {
			int16_t half = (int16_t)((int16_t)(rightLine - rightEdge) >> 1);

			kept = rightLine;
			column = (int16_t)(half + rightLine);
			roaddraw_object(column, road, distance, 1, 0xb);
		}

		if ((flags & 2) != 0) {
			roaddraw_object(rightEdge, road, distance, 1, 9);
		}
	} else {
		if ((left & 0x40) != 0) {
			roaddraw_crossObject(leftEdge, road, distance, 1);
		}

		if ((right & 0x40) != 0) {
			roaddraw_crossObject(rightEdge, road, distance, -1);
		}
	}

	if ((right & 0x20) != 0) {
		column = crossing ? rightEdge : rightLine;
		roaddraw_object(column, road, distance, 1, 0);
	}

	if ((right & 0x100) != 0) {
		if (crossing) {
			column = rightEdge;
		} else if ((right & 0x200) == 0) {
			column = rightLine;
		} else {
			column = (int16_t)(rightLine * 2 - rightEdge);
		}

		roaddraw_object(column, road, distance, 1, 4);
	}

	if ((right & 0x2000) != 0) {
		roaddraw_object(rightEdge, road, distance, 1, 0x12);
	}

	if ((right & 0x4000) != 0) {
		roaddraw_object(rightEdge, road, distance, 1, 0x13);
	}

	if ((right & 0x800) != 0) {
		roaddraw_object(rightEdge, road, distance, 1, 3);
	}

	if ((right & 0x1000) != 0) {
		roaddraw_object(rightEdge, road, distance, -1, 6);
	}

	if ((right & 1) != 0) {
		if (!crossing) {
			roaddraw_object(rightEdge, road, distance, 1, 1);
		} else {
			roaddraw_strip(
				rightEdge,
				middle,
				distance,
				-1,
				false,
				ROADDRAW_STRIP_LOWER
			);
		}
	}

	if ((flags & ROADVIEW__ACROSS) != 0) {
		int16_t above = (int16_t)(road - *pFar->pEye);
		int16_t top = (int16_t)(road - (int16_t)(above * 4));

		kept = road;
		roadview__across(
			leftEdge,
			rightEdge,
			top,
			road,
			distance,
			false,
			(uint16_t)(flags & ROADVIEW__ACROSS)
		);
	}

	if ((flags & 0x100) != 0) {
		roaddraw_object(rightEdge, road, distance, 1, 5);
	}

	if ((flags & 0x800) != 0) {
		roaddraw_object(rightEdge, road, distance, 1, 0xf);
	}

	if ((flags & 0x200) != 0) {
		roaddraw_object(rightEdge, road, distance, 1, 0x10);
	}

	if ((flags & 0x400) != 0) {
		roaddraw_object(rightEdge, road, distance, 1, 0x11);
	}

	return kept;
}

/** ****************************************************************************
 *
 * \brief	Draw the two pictures that lie over the view.
 *
 * \details	Port of FUN_3645_3e3c: picture 0x3ee (DS:0x72f4) at
 * 			(0, 0x12) and 0x3ef (DS:0x8bca) at (0x58, 0x5f), with their
 * 			masks (video table entry 2, DS:0x78aa), not clipped.
 *
 ******************************************************************************/
static void roadview__overlays(void) {
	const roadpic_Picture * pOverA = roadpic_set.pOverA;
	const roadpic_Picture * pOverB = roadpic_set.pOverB;

	if (pOverA != NULL) {
		screen_drawSpritePart(
			roaddraw_view,
			pOverA->gameId,
			0,
			0,
			pOverA->width,
			pOverA->height,
			0,
			ROADVIEW__TOP,
			false
		);
	}

	if (pOverB != NULL) {
		screen_drawSpritePart(
			roaddraw_view,
			pOverB->gameId,
			0,
			0,
			pOverB->width,
			pOverB->height,
			ROADVIEW__OVER_B_LEFT,
			ROADVIEW__OVER_B_TOP,
			false
		);
	}
}

/** ****************************************************************************
 *
 * \brief	Draw the start lights of a race.
 *
 * \details	Port of FUN_3645_3dc3. While the height DS:0x7aae is above
 * 			0 (it shrinks once the car moves, roadstate_frame), that
 * 			many of the bottom rows of a half of picture 0x3f5
 * 			(DS:0x6e60), 0x18 columns wide, are drawn with their mask
 * 			at (0x82, 0x12): the right half, or the left half once
 * 			"Go !" shows (DS:0x7d66), which also sets DS:0x8228.
 *
 ******************************************************************************/
static void roadview__startLights(void) {
	const roadpic_Picture * pLights = roadpic_set.pRaceStart;
	int16_t					height = exe_readS16(ROADPIC_RACE_START);
	int16_t					sourceLeft = ROADVIEW__LIGHT_WIDTH;

	if (height <= 0) {
		return;
	}

	if (exe_readS16(DRIVE_GO) != 0) {
		exe_writeU16(ROADVIEW__AUTO_MAY, 1);
		sourceLeft = 0;
	}

	if (pLights == NULL) {
		return;
	}

	screen_drawSpritePart(
		roaddraw_view,
		pLights->gameId,
		sourceLeft,
		(int16_t)(ROADVIEW__LIGHT_ROWS - height),
		ROADVIEW__LIGHT_WIDTH,
		height,
		ROADVIEW__LIGHT_LEFT,
		ROADVIEW__TOP,
		false
	);
}
