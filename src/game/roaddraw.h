/** ****************************************************************************
 *
 * \file	roaddraw.h
 *
 * \brief	Drawing helpers of the 3D road (segment 3645).
 *
 ******************************************************************************/

#ifndef GAME_ROADDRAW_H_INCLUDED
#define GAME_ROADDRAW_H_INCLUDED

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

#define ROADDRAW_STRIP_LOWER 1 /**< Strip picture on row 0x66 of DS:0x82b4. */
#define ROADDRAW_STRIP_A	 2 /**< Strip picture on row 1 of DS:0x82b4. */
#define ROADDRAW_STRIP_B	 3 /**< Strip picture on row 0x2c of DS:0x82b4. */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/** A rectangle of the video table's copy (6 words, as in the original). */
typedef struct roaddraw_rect_tag {
	int16_t width;		/**< +0: width. */
	int16_t height;		/**< +2: height. */
	int16_t sourceLeft; /**< +4: column on the source. */
	int16_t sourceTop;	/**< +6: row on the source. */
	int16_t left;		/**< +8: column on the target. */
	int16_t top;		/**< +10: row on the target. */
} roaddraw_Rect;

/**
 * Where the draw loops of a row point into the projected rows
 * (roadstate_ahead or roadstate_behind).
 */
typedef struct roaddraw_row_tag {
	const int16_t * pLeft;		/**< Column of the left edge. */
	const int16_t * pRight;		/**< Column of the right edge. */
	const int16_t * pRoad;		/**< Row of the road surface. */
	const int16_t * pLeftLine;	/**< Column of the left object line. */
	const int16_t * pRightLine; /**< Column of the right object line. */
	const int16_t * pEye;		/**< Row at eye height. */
} roaddraw_Row;

/**
 * The pointers the draw loops walk (set by FUN_3645_4b8c and
 * FUN_3645_43fd, one row nearer each step).
 */
typedef struct roaddraw_walk_tag {
	/** DS:0x6e08, 0x6e18, 0x6e1a, 0x6e0a, 0x6e0e, 0x6e0c: the row. */
	roaddraw_Row farther;

	/** DS:0x6e10, 0x6e1c, 0x6e1e, 0x6e12, 0x6e16, 0x6e14: one row nearer. */
	roaddraw_Row nearer;

	uint16_t lineWidth; /**< DS:0x6e28: DS offset of the center line width. */
	uint16_t left;		/**< DS:0x6e20: road_table index, left side flags. */
	uint16_t right;		/**< DS:0x6e24: road_table index, right side flags. */
	uint16_t flags;		/**< DS:0x8b82: road_table index, road flags. */
} roaddraw_Walk;

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

extern int			 roaddraw_view;
extern roaddraw_Walk roaddraw_walk;

/*******************************************************************************
;
;	F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

void roaddraw_clipView(void);

void roaddraw_clipMirror(void);

void roaddraw_clipRows(
	roaddraw_Rect * pRect
);

void roaddraw_clipRowsLeft(
	roaddraw_Rect * pRect
);

void roaddraw_verticalLine(
	uint8_t color,
	int16_t column,
	int16_t top,
	int16_t bottom
);

void roaddraw_span(
	int16_t left,
	int16_t right,
	int16_t row,
	uint8_t color
);

void roaddraw_selectPage(void);

void roaddraw_show(void);

void roaddraw_shape(
	int16_t	 column,
	int16_t	 row,
	int16_t	 size,
	int16_t	 sizes,
	uint16_t table,
	int16_t	 side
);

void roaddraw_object(
	int16_t column,
	int16_t row,
	int16_t distance,
	int16_t side,
	int16_t object
);

void roaddraw_crossObject(
	int16_t column,
	int16_t row,
	int16_t distance,
	int16_t side
);

void roaddraw_strip(
	int16_t column,
	int16_t row,
	int16_t distance,
	int16_t anchor,
	bool	clipLeft,
	int16_t strip
);

void roaddraw_ground(
	int16_t distance,
	int16_t lineCount
);

void roaddraw_mirrorGround(void);

#endif // GAME_ROADDRAW_H_INCLUDED
