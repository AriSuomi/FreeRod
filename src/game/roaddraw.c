/** ****************************************************************************
 *
 * \file	roaddraw.c
 *
 * \brief	Drawing helpers of the 3D road (segment 3645).
 *
 * \details	The clip box (DS:0x5e4e..0x5e54) with its two settings
 * 			and the clipping of rectangles and lines, the span
 * 			(3645:0034), the page drawn to and shown (FUN_3645_224e,
 * 			FUN_3645_2288), the shapes made of spans (FUN_3645_2c0a and
 * 			the objects), the strip pictures (FUN_3645_3b79) and the
 * 			ground of a row ahead (FUN_3645_3a3b) and in the mirror
 * 			(FUN_3645_3ab4). All arithmetic is 16-bit, as in the
 * 			original.
 *
 * 			Layout (see drive.c): the port takes the path of the
 * 			adapters without a split screen. The view is drawn on the
 * 			surface DS:0x8188 (roaddraw_view) and its rectangle
 * 			DS:0x5e42 is copied to the display to show it. The strip
 * 			pictures are all on DS:0x82b4 (ROADPIC_PAGE), at the rows of
 * 			those adapters.
 *
 * 			Two values the original reads from stack slots it never
 * 			sets are kept here: the left sum of a case of
 * 			FUN_3645_2d20 (roaddraw__quadSum, what the previous call
 * 			left there) and the count of FUN_3645_2f48
 * 			(roaddraw_ground's lineCount); see docs/road.md.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/roaddraw.h"

#include "game/exe.h"
#include "game/globals.h"
#include "game/road.h"
#include "game/roadpic.h"
#include "game/screen.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define ROADDRAW__CLIP_LEFT	  0x5e4e /**< s16: clip box, left column. */
#define ROADDRAW__CLIP_TOP	  0x5e50 /**< s16: clip box, top row. */
#define ROADDRAW__CLIP_BOTTOM 0x5e52 /**< s16: clip box, bottom row. */
#define ROADDRAW__CLIP_RIGHT  0x5e54 /**< s16: clip box, right column. */
#define ROADDRAW__SHOWN		  0x5e42 /**< 6 words: rectangle shown. */
#define ROADDRAW__STRIPS	  0x5e9c /**< 7 * {column, width, height}. */
#define ROADDRAW__SHAPES	  0x6078 /**< Near pointers: shape lists. */
#define ROADDRAW__CROSS_SHAPE 0x607c /**< Shape list of FUN_3645_3b3a. */
#define ROADDRAW__RACING	  0x8aca /**< s16: 1 in a race, 0 in a cruise. */

#define ROADDRAW__STRIP_SIZE  6		/**< Bytes of a strip record. */
#define ROADDRAW__SHAPE_SIZE  6		/**< Bytes of a shape: count, far pointer. */
#define ROADDRAW__SPAN_SIZE	  4		/**< Bytes of a span. */
#define ROADDRAW__LAST_SIZE	  6		/**< Largest size of objects and strips. */
#define ROADDRAW__CROSS_LAST  7		/**< Largest size of FUN_3645_3b3a. */
#define ROADDRAW__NEAREST	  9		/**< Row index drawn at the largest size. */
#define ROADDRAW__LOWEST_ROW  0x65	/**< Last row of the view. */
#define ROADDRAW__MIRROR_LAST 0x2a	/**< Last row of the mirror. */
#define ROADDRAW__MIRROR_FROM 0xf2	/**< Left column of the mirror. */
#define ROADDRAW__MIRROR_TO	  0x13d /**< Right column of the mirror. */
#define ROADDRAW__LINED_ROWS  6		/**< Rows nearer than this get line 2. */

#define ROADDRAW__ROW_LOWER	  0x66 /**< Source row of ROADDRAW_STRIP_LOWER. */
#define ROADDRAW__ROW_A		  1	   /**< Source row of ROADDRAW_STRIP_A. */
#define ROADDRAW__ROW_B		  0x2c /**< Source row of ROADDRAW_STRIP_B. */

#define ROADDRAW__CROSSING	  0x0001 /**< Road flag: crossing. */
#define ROADDRAW__NOT_DRAWN	  0x0004 /**< Road flag: row not drawn. */
#define ROADDRAW__CENTER_LINE 0x0008 /**< Road flag: center line. */
#define ROADDRAW__LIGHT_ROAD  0x0010 /**< Road flag: road color 0xf. */
#define ROADDRAW__BAND		  0x0004 /**< Side flag: a second band. */
#define ROADDRAW__TALL_BAND	  0x0008 /**< Side flag: the band is taller. */
#define ROADDRAW__LIGHT_SIDE  0x0010 /**< Side flag: roadside color 2. */
#define ROADDRAW__SPLIT		  0x0080 /**< Side flag: split shoulder. */

#define ROADDRAW__ROAD		  1	  /**< Color of the road. */
#define ROADDRAW__ROAD_LIGHT  0xf /**< Color of the road, flag 0x10. */
#define ROADDRAW__SIDE		  3	  /**< Color of the roadside. */
#define ROADDRAW__SIDE_LIGHT  2	  /**< Color of the roadside, flag 0x10. */
#define ROADDRAW__GROUND	  4	  /**< Crossing ground, bands, line. */
#define ROADDRAW__EDGE		  2	  /**< Lines at a crossing. */
#define ROADDRAW__SHOULDER	  9	  /**< Outer shoulder; mirror's sides. */

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/**
 * Page of the road view (the surface DS:0x8188). Set by drive_run
 * before the road's pictures are loaded; the original sets it in
 * FUN_3645_0d48.
 */
int roaddraw_view = 1;

/** The pointers of the draw loops (see roaddraw_Walk). */
roaddraw_Walk roaddraw_walk;

/** Page spans and lines go to (DS:0x787a and 0x767c). */
static int roaddraw__page = 1;

/**
 * Stack slot BP-0x12 of FUN_3645_2d20: one case adds to it without
 * setting it first and so goes on from the previous call's sum.
 */
static int16_t roaddraw__quadSum;

/*******************************************************************************
;
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

static uint16_t roaddraw__word(
	uint16_t index
);

static int16_t roaddraw__clamp(
	int16_t value,
	int16_t last
);

static int16_t roaddraw__slope(
	int16_t fromColumn,
	int16_t fromRow,
	int16_t toColumn,
	int16_t toRow
);

static int16_t roaddraw__sign(
	int16_t value
);

static void roaddraw__quad(
	uint8_t color,
	int16_t fromColumn,
	int16_t toColumn,
	int16_t lowFrom,
	int16_t lowTo,
	int16_t highTo,
	int16_t highFrom
);

static void roaddraw__road(
	int16_t distance,
	int16_t lineCount
);

static void roaddraw__mirrorRoad(void);

static void roaddraw__roadside(
	int16_t fromColumn,
	int16_t toColumn,
	int16_t lowFrom,
	int16_t lowTo,
	int16_t highTo,
	int16_t highFrom,
	bool	left
);

static void roaddraw__mirrorRoadside(
	int16_t fromColumn,
	int16_t toColumn,
	int16_t lowFrom,
	int16_t lowTo,
	int16_t highTo,
	int16_t highFrom,
	bool	left
);

static void roaddraw__crossingLines(
	int16_t fromColumn,
	int16_t toColumn,
	int16_t lowFrom,
	int16_t lowTo,
	int16_t highTo,
	int16_t highFrom,
	bool	left
);

static void roaddraw__bandEnd(
	int16_t fromColumn,
	int16_t toColumn,
	int16_t row,
	int16_t endRow,
	bool	left
);

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Set the clip box to the view.
 *
 * \details	Port of FUN_3645_2410: columns 0..0x13f, rows 0x12..0x65.
 *
 ******************************************************************************/
void roaddraw_clipView(void) {
	exe_writeU16(ROADDRAW__CLIP_LEFT, 0);
	exe_writeU16(ROADDRAW__CLIP_TOP, 0x12);
	exe_writeU16(ROADDRAW__CLIP_BOTTOM, 0x65);
	exe_writeU16(ROADDRAW__CLIP_RIGHT, 0x13f);
}

/** ****************************************************************************
 *
 * \brief	Set the clip box to the mirror.
 *
 * \details	Port of FUN_3645_23f7: columns 0xf2..0x13d, rows
 * 			0x16..0x2a.
 *
 ******************************************************************************/
void roaddraw_clipMirror(void) {
	exe_writeU16(ROADDRAW__CLIP_LEFT, 0xf2);
	exe_writeU16(ROADDRAW__CLIP_TOP, 0x16);
	exe_writeU16(ROADDRAW__CLIP_RIGHT, 0x13d);
	exe_writeU16(ROADDRAW__CLIP_BOTTOM, 0x2a);
}

/** ****************************************************************************
 *
 * \brief	Clip a rectangle to the rows of the clip box.
 *
 * \param	pRect			The rectangle, changed in place.
 *
 * \details	Port of FUN_3645_22fc. The columns are not clipped; a
 * 			rectangle below the box gets a height of 0 or less.
 *
 ******************************************************************************/
void roaddraw_clipRows(
	roaddraw_Rect * pRect
) {
	int16_t top = exe_readS16(ROADDRAW__CLIP_TOP);
	int16_t bottom = exe_readS16(ROADDRAW__CLIP_BOTTOM);

	if (pRect->top < top) {
		int16_t cut = (int16_t)(top - pRect->top);

		pRect->height = (int16_t)(pRect->height - cut);
		pRect->sourceTop = (int16_t)(pRect->sourceTop + cut);
		pRect->top = top;
	}

	int16_t end = (int16_t)(pRect->top + pRect->height);

	if (end > bottom) {
		pRect->height = (int16_t)(pRect->height - (end - bottom));
	}
}

/** ****************************************************************************
 *
 * \brief	Clip a rectangle to the rows and the left column of the
 * 			clip box.
 *
 * \param	pRect			The rectangle, changed in place.
 *
 * \details	Port of FUN_3645_2331. As in the original, a cut on the
 * 			left moves the source column by the cut plus 7.
 *
 ******************************************************************************/
void roaddraw_clipRowsLeft(
	roaddraw_Rect * pRect
) {
	int16_t left = exe_readS16(ROADDRAW__CLIP_LEFT);

	roaddraw_clipRows(pRect);

	if (pRect->left < left) {
		int16_t cut = (int16_t)(left - pRect->left);

		pRect->width = (int16_t)(pRect->width - cut);
		pRect->sourceLeft = (int16_t)(pRect->sourceLeft + cut + 7);
		pRect->left = left;
	}
}

/** ****************************************************************************
 *
 * \brief	Draw a vertical line inside the clip box.
 *
 * \param	color			Color.
 * \param	column			Column of the line.
 * \param	top				First row.
 * \param	bottom			Last row.
 *
 * \details	Port of FUN_3645_2381: nothing is drawn when the line lies
 * 			outside the box; otherwise both ends are moved into its rows
 * 			and the line is drawn with the video table's line (entry
 * 			12, DS:0x78d2).
 *
 ******************************************************************************/
void roaddraw_verticalLine(
	uint8_t color,
	int16_t column,
	int16_t top,
	int16_t bottom
) {
	int16_t clipTop = exe_readS16(ROADDRAW__CLIP_TOP);
	int16_t clipBottom = exe_readS16(ROADDRAW__CLIP_BOTTOM);

	if (bottom < clipTop || top > clipBottom ||
		column < exe_readS16(ROADDRAW__CLIP_LEFT) ||
		column > exe_readS16(ROADDRAW__CLIP_RIGHT)) {
		return;
	}

	top = top < clipTop ? clipTop : top;
	top = top > clipBottom ? clipBottom : top;
	bottom = bottom < clipTop ? clipTop : bottom;
	bottom = bottom > clipBottom ? clipBottom : bottom;

	screen_line(roaddraw__page, column, top, column, bottom, color);
}

/** ****************************************************************************
 *
 * \brief	Draw a row of pixels in one color inside the clip box.
 *
 * \param	left			First column.
 * \param	right			Last column (drawn).
 * \param	row				Row.
 * \param	color			Color.
 *
 * \details	Port of 3645:0034, entry 13 of the video table (DS:0x78d6).
 * 			Nothing is drawn when the row is outside the box or the
 * 			span lies wholly left or right of it; otherwise both ends
 * 			are moved into the box, swapped if the first is then
 * 			right of the last, and all pixels from one to the other are
 * 			drawn, both ends included.
 *
 ******************************************************************************/
void roaddraw_span(
	int16_t left,
	int16_t right,
	int16_t row,
	uint8_t color
) {
	int16_t clipLeft = exe_readS16(ROADDRAW__CLIP_LEFT);
	int16_t clipRight = exe_readS16(ROADDRAW__CLIP_RIGHT);

	if (row < exe_readS16(ROADDRAW__CLIP_TOP) ||
		row > exe_readS16(ROADDRAW__CLIP_BOTTOM) || right < clipLeft ||
		left > clipRight) {
		return;
	}

	left = left > clipLeft ? left : clipLeft;
	right = right < clipRight ? right : clipRight;

	if (left > right) {
		int16_t swap = left;

		left = right;
		right = swap;
	}

	plat_selectPage(roaddraw__page);
	plat_fillRect(left, row, right - left + 1, 1, color);
}

/** ****************************************************************************
 *
 * \brief	Choose the page the road is drawn on.
 *
 * \details	Port of FUN_3645_224e for the adapters without a split
 * 			screen: spans and lines go to the view (roaddraw_view).
 *
 ******************************************************************************/
void roaddraw_selectPage(void) {
	roaddraw__page = roaddraw_view;
}

/** ****************************************************************************
 *
 * \brief	Show the view that was drawn.
 *
 * \details	Port of FUN_3645_2288 for the adapters without a split
 * 			screen: the rectangle DS:0x5e42 (0x140 by 0x52 at row 0x12)
 * 			of the view is copied to the display (video table entry 0,
 * 			DS:0x78a2, plain copy) and drawing goes on on the view.
 *
 ******************************************************************************/
void roaddraw_show(void) {
	roaddraw_Rect shown = {
		exe_readS16(ROADDRAW__SHOWN),
		exe_readS16(ROADDRAW__SHOWN + 2),
		exe_readS16(ROADDRAW__SHOWN + 4),
		exe_readS16(ROADDRAW__SHOWN + 6),
		exe_readS16(ROADDRAW__SHOWN + 8),
		exe_readS16(ROADDRAW__SHOWN + 10),
	};

	plat_blit(
		roaddraw_view,
		shown.sourceLeft,
		shown.sourceTop,
		globals_displaySurface,
		shown.left,
		shown.top,
		shown.width,
		shown.height
	);
	roaddraw__page = roaddraw_view;
}

/** ****************************************************************************
 *
 * \brief	Draw one size of a shape made of spans.
 *
 * \param	column			Column the spans are placed from.
 * \param	row				Row the spans are placed from.
 * \param	size			Size to draw.
 * \param	sizes			Number of sizes of the shape.
 * \param	table			DS offset of its sizes: {count, far pointer}.
 * \param	side			-1: mirrored to the left; otherwise as stored.
 *
 * \details	Port of FUN_3645_2c0a. Nothing is drawn when the size is
 * 			not below the number of sizes. A span is s8 column, s8 row,
 * 			s8 length and u8 color; it runs from column + its column
 * 			to that plus its length, or mirrored from column - its
 * 			column - its length to column - its column.
 *
 ******************************************************************************/
void roaddraw_shape(
	int16_t	 column,
	int16_t	 row,
	int16_t	 size,
	int16_t	 sizes,
	uint16_t table,
	int16_t	 side
) {
	if (size >= sizes) {
		return;
	}

	uint16_t entry = (uint16_t)(table + size * ROADDRAW__SHAPE_SIZE);
	int16_t	 spans = exe_readS16(entry);

	if (spans <= 0) {
		return;
	}

	const uint8_t * pSpan = exe_far(
		exe_readU16((uint16_t)(entry + 4)),
		exe_readU16((uint16_t)(entry + 2)),
		(uint16_t)(spans * ROADDRAW__SPAN_SIZE)
	);

	if (pSpan == NULL) {
		return;
	}

	for (int16_t span = 0; span < spans; span++) {
		int16_t offset = (int8_t)pSpan[0];
		int16_t top = (int16_t)((int8_t)pSpan[1] + row);
		int16_t length = (int8_t)pSpan[2];

		if (side == -1) {
			int16_t right = (int16_t)(column - offset);

			roaddraw_span((int16_t)(right - length), right, top, pSpan[3]);
		} else {
			int16_t left = (int16_t)(offset + column);

			roaddraw_span(left, (int16_t)(length + left), top, pSpan[3]);
		}

		pSpan += ROADDRAW__SPAN_SIZE;
	}
}

/** ****************************************************************************
 *
 * \brief	Draw a roadside object.
 *
 * \param	column			Column it stands at.
 * \param	row				Row it stands on (at most 0x65 is used).
 * \param	distance		Index of the row ahead (1 nearest).
 * \param	side			-1: mirrored; otherwise as stored.
 * \param	object			Index of the shape list DS:0x6078.
 *
 * \details	Port of FUN_3645_3aeb: the size is 9 - distance, limited
 * 			to 0..6.
 *
 ******************************************************************************/
void roaddraw_object(
	int16_t column,
	int16_t row,
	int16_t distance,
	int16_t side,
	int16_t object
) {
	int16_t size = roaddraw__clamp(
		(int16_t)(ROADDRAW__NEAREST - distance),
		ROADDRAW__LAST_SIZE
	);
	uint16_t list = exe_readU16((uint16_t)(ROADDRAW__SHAPES + object * 2));

	if (row > ROADDRAW__LOWEST_ROW) {
		row = ROADDRAW__LOWEST_ROW;
	}

	roaddraw_shape(
		column,
		row,
		size,
		exe_readS16(list),
		exe_readU16((uint16_t)(list + 2)),
		side
	);
}

/** ****************************************************************************
 *
 * \brief	Draw the object of a crossing (DS:0x607c).
 *
 * \param	column			Column it stands at.
 * \param	row				Row it stands on.
 * \param	distance		Index of the row ahead (1 nearest).
 * \param	side			-1: mirrored; otherwise as stored.
 *
 * \details	Port of FUN_3645_3b3a: the shape list of object 2, size
 * 			9 - distance limited to 0..7.
 *
 ******************************************************************************/
void roaddraw_crossObject(
	int16_t column,
	int16_t row,
	int16_t distance,
	int16_t side
) {
	int16_t size = roaddraw__clamp(
		(int16_t)(ROADDRAW__NEAREST - distance),
		ROADDRAW__CROSS_LAST
	);
	uint16_t list = exe_readU16(ROADDRAW__CROSS_SHAPE);

	roaddraw_shape(
		column,
		row,
		size,
		exe_readS16(list),
		exe_readU16((uint16_t)(list + 2)),
		side
	);
}

/** ****************************************************************************
 *
 * \brief	Draw a piece of a strip picture.
 *
 * \param	column			Column it is placed at.
 * \param	row				Row of its lower edge (anchor 0: its middle).
 * \param	distance		Index of the row ahead (1 nearest).
 * \param	anchor			1: column is its right edge; 0: its middle
 * 							(and row its middle); otherwise its left
 * 							edge.
 * \param	clipLeft		Clip at the left column of the box too.
 * \param	strip			ROADDRAW_STRIP_LOWER, _A or _B.
 *
 * \details	Port of FUN_3645_3b79. The size is 9 - distance, limited
 * 			to 0..6, and picks the record DS:0x5e9c {source column,
 * 			width, height}. The rectangle is clipped
 * 			(roaddraw_clipRows, roaddraw_clipRowsLeft) and copied from
 * 			DS:0x82b4 to the view (video table entry 0, plain copy).
 * 			The rows of the strips are those of the adapters without a
 * 			split screen (EGA has the two cruise strips on the screen
 * 			surface at rows 100 and 0x8f).
 *
 ******************************************************************************/
void roaddraw_strip(
	int16_t column,
	int16_t row,
	int16_t distance,
	int16_t anchor,
	bool	clipLeft,
	int16_t strip
) {
	int16_t size = roaddraw__clamp(
		(int16_t)(ROADDRAW__NEAREST - distance),
		ROADDRAW__LAST_SIZE
	);
	uint16_t	  record = (uint16_t)(ROADDRAW__STRIPS +
								  size * ROADDRAW__STRIP_SIZE);
	roaddraw_Rect rect;

	rect.width = exe_readS16((uint16_t)(record + 2));
	rect.height = exe_readS16((uint16_t)(record + 4));
	rect.sourceLeft = exe_readS16(record);

	if (strip == ROADDRAW_STRIP_LOWER) {
		rect.sourceTop = ROADDRAW__ROW_LOWER;
	} else if (strip == ROADDRAW_STRIP_A) {
		rect.sourceTop = ROADDRAW__ROW_A;
	} else {
		rect.sourceTop = ROADDRAW__ROW_B;
	}

	if (anchor == 1) {
		column = (int16_t)(column - rect.width);
	} else if (anchor == 0) {
		column = (int16_t)(column - (rect.width >> 1));
	}

	int16_t rise = anchor == 0 ? (int16_t)(rect.height >> 1) : rect.height;

	rect.top = (int16_t)(row - rise);
	rect.left = column;

	if (!clipLeft) {
		roaddraw_clipRows(&rect);
	} else {
		roaddraw_clipRowsLeft(&rect);
	}

	plat_blit(
		ROADPIC_PAGE,
		rect.sourceLeft,
		rect.sourceTop,
		roaddraw_view,
		rect.left,
		rect.top,
		rect.width,
		rect.height
	);
}

/** ****************************************************************************
 *
 * \brief	Draw the ground of a row ahead.
 *
 * \param	distance		Index of the row ahead (1 nearest).
 * \param	lineCount		Count of rows left at which the lines of a
 * 							crossing are drawn (see roaddraw__road).
 *
 * \details	Port of FUN_3645_3a3b with FUN_3645_39de and 3a0d: in a
 * 			cruise the roadside on the left and on the right, then the
 * 			road.
 *
 ******************************************************************************/
void roaddraw_ground(
	int16_t distance,
	int16_t lineCount
) {
	const roaddraw_Row * pFar = &roaddraw_walk.farther;
	const roaddraw_Row * pNear = &roaddraw_walk.nearer;

	if (exe_readS16(ROADDRAW__RACING) == 0) {
		roaddraw__roadside(
			*pNear->pLeftLine,
			*pFar->pLeftLine,
			*pNear->pRoad,
			*pFar->pRoad,
			*pFar->pEye,
			*pNear->pEye,
			true
		);
		roaddraw__roadside(
			*pFar->pRightLine,
			*pNear->pRightLine,
			*pFar->pRoad,
			*pNear->pRoad,
			*pNear->pEye,
			*pFar->pEye,
			false
		);
	}

	roaddraw__road(distance, lineCount);
}

/** ****************************************************************************
 *
 * \brief	Draw the ground of a row in the mirror.
 *
 * \details	Port of FUN_3645_3ab4 with FUN_3645_3a57 and 3a86: the
 * 			roadside on the left and on the right, then the road.
 *
 ******************************************************************************/
void roaddraw_mirrorGround(void) {
	const roaddraw_Row * pFar = &roaddraw_walk.farther;
	const roaddraw_Row * pNear = &roaddraw_walk.nearer;

	roaddraw__mirrorRoadside(
		*pNear->pLeftLine,
		*pFar->pLeftLine,
		*pNear->pRoad,
		*pFar->pRoad,
		*pFar->pEye,
		*pNear->pEye,
		true
	);
	roaddraw__mirrorRoadside(
		*pFar->pRightLine,
		*pNear->pRightLine,
		*pFar->pRoad,
		*pNear->pRoad,
		*pNear->pEye,
		*pFar->pEye,
		false
	);
	roaddraw__mirrorRoad();
}

/** ****************************************************************************
 *
 * \brief	Read a word of the road tables.
 *
 * \param	index			Index of road_table.
 *
 * \return	The word; 0 outside the tables.
 *
 * \details	Not part of the original, which reads whatever lies before
 * 			or after a table (only possible at tile 0 or the last).
 *
 ******************************************************************************/
static uint16_t roaddraw__word(
	uint16_t index
) {
	if (index >= ROAD_WORDS) {
		return 0;
	}

	return road_table[index];
}

/** ****************************************************************************
 *
 * \brief	Limit a size to 0..last.
 *
 * \param	value			The size.
 * \param	last			Largest size.
 *
 * \return	The limited size.
 *
 ******************************************************************************/
static int16_t roaddraw__clamp(
	int16_t value,
	int16_t last
) {
	if (value > last) {
		value = last;
	}

	if (value < 0) {
		value = 0;
	}

	return value;
}

/** ****************************************************************************
 *
 * \brief	Columns per row of a line, in sixteenths.
 *
 * \param	fromColumn		Column at the first point.
 * \param	fromRow			Row of the first point.
 * \param	toColumn		Column at the second point.
 * \param	toRow			Row of the second point.
 *
 * \return	(toColumn - fromColumn) * 16 / (toRow - fromRow); 0 when the
 * 			rows are the same.
 *
 * \details	Port of FUN_3645_2cfc (16-bit products, signed division).
 *
 ******************************************************************************/
static int16_t roaddraw__slope(
	int16_t fromColumn,
	int16_t fromRow,
	int16_t toColumn,
	int16_t toRow
) {
	if (toRow == fromRow) {
		return 0;
	}

	int16_t run = (int16_t)((toColumn - fromColumn) * 16);
	int16_t rise = (int16_t)(toRow - fromRow);

	return (int16_t)(run / rise);
}

/** ****************************************************************************
 *
 * \brief	Sign of a value.
 *
 * \param	value			The value.
 *
 * \return	1, 0 or -1.
 *
 * \details	Port of FUN_3645_2f2b.
 *
 ******************************************************************************/
static int16_t roaddraw__sign(
	int16_t value
) {
	if (value > 0) {
		return 1;
	}

	return value < 0 ? -1 : 0;
}

/** ****************************************************************************
 *
 * \brief	Fill the shape between two columns and two lines.
 *
 * \param	color			Color.
 * \param	fromColumn		First column.
 * \param	toColumn		Second column.
 * \param	lowFrom			Row of the low line at fromColumn.
 * \param	lowTo			Row of the low line at toColumn.
 * \param	highTo			Row of the high line at toColumn.
 * \param	highFrom		Row of the high line at fromColumn.
 *
 * \details	Port of FUN_3645_2d20. With both lines flat, the rows
 * 			highFrom..lowTo are filled between the columns. Otherwise the
 * 			rows from the higher of the high rows to the lower of the
 * 			low rows (not past the bottom of the clip box) are filled in
 * 			one of four ways, depending on how the lines slant, each
 * 			end of a span following a line in sixteenths of a column
 * 			(roaddraw__slope) or staying at a column. As in the
 * 			original, one way (high line rising, low line falling
 * 			towards toColumn) goes on from the sum the previous call
 * 			left (roaddraw__quadSum).
 *
 ******************************************************************************/
static void roaddraw__quad(
	uint8_t color,
	int16_t fromColumn,
	int16_t toColumn,
	int16_t lowFrom,
	int16_t lowTo,
	int16_t highTo,
	int16_t highFrom
) {
	if (lowFrom == lowTo && highFrom == highTo) {
		for (int16_t row = highFrom; row <= lowTo; row++) {
			roaddraw_span(fromColumn, toColumn, row, color);
		}

		return;
	}

	int16_t row = highTo < highFrom ? highTo : highFrom;
	int16_t last = lowFrom < lowTo ? lowTo : lowFrom;
	int16_t way;
	int16_t leftEnd = 0;
	int16_t rightEnd = 0;
	int16_t rightSum = 0;
	int16_t leftSum = roaddraw__quadSum;

	if (highFrom < highTo && lowTo < lowFrom) {
		way = 2;
		leftEnd = fromColumn;
		rightSum = (int16_t)(fromColumn * 16);
	} else if (highTo < highFrom && lowFrom < lowTo) {
		way = 3;
		rightEnd = toColumn;
		leftEnd = (int16_t)(toColumn * 16);
	} else {
		way = highTo < highFrom || lowTo < lowFrom ? 5 : 4;
		rightSum = (int16_t)((way == 5 ? toColumn : fromColumn) * 16);
		leftSum = rightSum;
	}

	int16_t highStep = roaddraw__slope(fromColumn, highFrom, toColumn, highTo);
	int16_t lowStep = roaddraw__slope(fromColumn, lowFrom, toColumn, lowTo);
	int16_t bottom = exe_readS16(ROADDRAW__CLIP_BOTTOM);

	for (; row <= last && row <= bottom; row++) {
		switch (way) {
			case 2:
				if (row < highTo) {
					rightSum = (int16_t)(rightSum + highStep);
					rightEnd = (int16_t)(rightSum >> 4);
				} else if (row <= lowTo) {
					rightEnd = toColumn;
				} else {
					rightSum = (int16_t)(rightSum + lowStep);
					rightEnd = (int16_t)(rightSum >> 4);
				}

				break;

			case 3:
				if (row < highFrom) {
					leftSum = (int16_t)(leftSum + highStep);
					leftEnd = (int16_t)(leftSum >> 4);
				} else if (row <= lowFrom) {
					leftEnd = fromColumn;
				} else {
					leftSum = (int16_t)(leftSum + lowStep);
					leftEnd = (int16_t)(leftSum >> 4);
				}

				break;

			case 4:
				if (row > lowFrom) {
					leftSum = (int16_t)(leftSum + lowStep);
					leftEnd = (int16_t)(leftSum >> 4);
				} else {
					leftEnd = fromColumn;
				}

				if (row < highTo) {
					rightSum = (int16_t)(rightSum + highStep);
					rightEnd = (int16_t)(rightSum >> 4);
				} else {
					rightEnd = toColumn;
				}

				break;

			default:
				if (row < highFrom) {
					leftSum = (int16_t)(leftSum + highStep);
					leftEnd = (int16_t)(leftSum >> 4);
				} else {
					leftEnd = fromColumn;
				}

				if (row > lowTo) {
					rightSum = (int16_t)(rightSum + lowStep);
					rightEnd = (int16_t)(rightSum >> 4);
				} else {
					rightEnd = toColumn;
				}

				break;
		}

		if (rightEnd < leftEnd) {
			roaddraw_span(rightEnd, leftEnd, row, color);
		} else {
			roaddraw_span(leftEnd, rightEnd, row, color);
		}
	}

	roaddraw__quadSum = leftSum;
}

/** ****************************************************************************
 *
 * \brief	Draw the road of a row ahead.
 *
 * \param	distance		Index of the row ahead (1 nearest).
 * \param	lineCount		Value of the stack slot BP-0x22, which the
 * 							original compares with the rows left but
 * 							never sets (see docs/road.md).
 *
 * \details	Port of FUN_3645_2f48. The rows from the row's road row to
 * 			the nearer one (not past row 0x65) are drawn, the edges
 * 			following lines in sixteenths of a column. Outside a
 * 			crossing: the road between the edges, color 1 (0xf with
 * 			road flag 0x10). At a crossing (road flag 1; nothing more
 * 			with road flag 4) the ground between each edge and its
 * 			object line, color 4, or split with the outer half 4 and the
 * 			inner half 9 (side flag 0x80); where the rows left equal
 * 			lineCount, below row index 6, the outer part gets a line of
 * 			color 2. With road flag 8 the center line, color 4.
 *
 ******************************************************************************/
static void roaddraw__road(
	int16_t distance,
	int16_t lineCount
) {
	const roaddraw_Row * pFar = &roaddraw_walk.farther;
	const roaddraw_Row * pNear = &roaddraw_walk.nearer;

	int16_t	 left = *pFar->pLeft;
	int16_t	 right = *pFar->pRight;
	int16_t	 leftSum = (int16_t)(left * 16);
	int16_t	 rightSum = (int16_t)(right * 16);
	int16_t	 row = *pFar->pRoad;
	int16_t	 rows = *pNear->pRoad;
	int16_t	 leftStep = roaddraw__slope(*pNear->pLeft, rows, left, row);
	int16_t	 rightStep = roaddraw__slope(*pNear->pRight, rows, right, row);
	uint16_t flags = roaddraw__word(roaddraw_walk.flags);
	bool	 crossing = (flags & ROADDRAW__CROSSING) != 0;
	int16_t	 leftLine = 0;
	int16_t	 rightLine = 0;
	int16_t	 leftLineSum = 0;
	int16_t	 rightLineSum = 0;
	int16_t	 leftLineStep = 0;
	int16_t	 rightLineStep = 0;
	int16_t	 lineWidth = 0;
	int16_t	 lineLeft = 0;

	if (crossing) {
		leftLine = *pFar->pLeftLine;
		rightLine = *pFar->pRightLine;
		leftLineSum = (int16_t)(leftLine * 16);
		rightLineSum = (int16_t)(rightLine * 16);
		leftLineStep = roaddraw__slope(*pNear->pLeftLine, rows, leftLine, row);
		rightLineStep =
			roaddraw__slope(*pNear->pRightLine, rows, rightLine, row);
	}

	if ((flags & ROADDRAW__CENTER_LINE) != 0) {
		lineWidth = exe_readS16(roaddraw_walk.lineWidth);
		lineLeft = (int16_t)(((left + right) >> 1) - (lineWidth >> 1));
	}

	if (roaddraw__sign((int16_t)(rows - row)) < 0) {
		return;
	}

	rows = (int16_t)(rows - row);

	uint16_t leftFlags = roaddraw__word(roaddraw_walk.left);
	uint16_t rightFlags = roaddraw__word(roaddraw_walk.right);

	for (; rows != 0 && row <= ROADDRAW__LOWEST_ROW; row++) {
		if (!crossing) {
			uint8_t color = (flags & ROADDRAW__LIGHT_ROAD) != 0
				? ROADDRAW__ROAD_LIGHT
				: ROADDRAW__ROAD;

			roaddraw_span(left, right, row, color);
		} else {
			if ((flags & ROADDRAW__NOT_DRAWN) != 0) {
				return;
			}

			bool lined = rows == lineCount && distance < ROADDRAW__LINED_ROWS;

			if ((leftFlags & ROADDRAW__SPLIT) == 0) {
				roaddraw_span(leftLine, left, row, ROADDRAW__GROUND);

				if (lined) {
					roaddraw_span(leftLine, left, row, ROADDRAW__EDGE);
				}
			} else {
				int16_t middle =
					(int16_t)(((left - leftLine) >> 1) + leftLine);

				roaddraw_span(leftLine, middle, row, ROADDRAW__GROUND);
				roaddraw_span(middle, left, row, ROADDRAW__SHOULDER);

				if (lined) {
					roaddraw_span(leftLine, middle, row, ROADDRAW__EDGE);
				}
			}

			if ((rightFlags & ROADDRAW__SPLIT) == 0) {
				roaddraw_span(right, rightLine, row, ROADDRAW__GROUND);

				if (lined) {
					roaddraw_span(right, rightLine, row, ROADDRAW__EDGE);
				}
			} else {
				int16_t middle =
					(int16_t)(rightLine - ((rightLine - right) >> 1));

				roaddraw_span(middle, rightLine, row, ROADDRAW__GROUND);
				roaddraw_span(right, middle, row, ROADDRAW__SHOULDER);

				if (lined) {
					roaddraw_span(middle, rightLine, row, ROADDRAW__EDGE);
				}
			}
		}

		if ((flags & ROADDRAW__CENTER_LINE) != 0) {
			roaddraw_span(
				lineLeft,
				(int16_t)(lineLeft + lineWidth),
				row,
				ROADDRAW__GROUND
			);
		}

		leftSum = (int16_t)(leftSum + leftStep);
		rightSum = (int16_t)(rightSum + rightStep);
		left = (int16_t)(leftSum >> 4);
		right = (int16_t)(rightSum >> 4);

		if (crossing) {
			leftLineSum = (int16_t)(leftLineSum + leftLineStep);
			rightLineSum = (int16_t)(rightLineSum + rightLineStep);
			leftLine = (int16_t)(leftLineSum >> 4);
			rightLine = (int16_t)(rightLineSum >> 4);
		}

		lineLeft = (int16_t)(((left + right) >> 1) - (lineWidth >> 1));
		rows--;
	}
}

/** ****************************************************************************
 *
 * \brief	Draw the road of a row in the mirror.
 *
 * \details	Port of FUN_3645_324d, the mirror's twin of
 * 			roaddraw__road: rows up to 0x2a; outside a crossing the
 * 			road in color 1 and color 9 from the mirror's left column
 * 			to the left edge and from the right edge to its right
 * 			column; at a crossing the ground as ahead, without the
 * 			lines of color 2.
 *
 ******************************************************************************/
static void roaddraw__mirrorRoad(void) {
	const roaddraw_Row * pFar = &roaddraw_walk.farther;
	const roaddraw_Row * pNear = &roaddraw_walk.nearer;

	int16_t	 left = *pFar->pLeft;
	int16_t	 right = *pFar->pRight;
	int16_t	 leftSum = (int16_t)(left * 16);
	int16_t	 rightSum = (int16_t)(right * 16);
	int16_t	 row = *pFar->pRoad;
	int16_t	 rows = *pNear->pRoad;
	int16_t	 leftStep = roaddraw__slope(*pNear->pLeft, rows, left, row);
	int16_t	 rightStep = roaddraw__slope(*pNear->pRight, rows, right, row);
	uint16_t flags = roaddraw__word(roaddraw_walk.flags);
	bool	 crossing = (flags & ROADDRAW__CROSSING) != 0;
	int16_t	 leftLine = 0;
	int16_t	 rightLine = 0;
	int16_t	 leftLineSum = 0;
	int16_t	 rightLineSum = 0;
	int16_t	 leftLineStep = 0;
	int16_t	 rightLineStep = 0;
	int16_t	 lineWidth = 0;
	int16_t	 lineLeft = 0;

	if (crossing) {
		leftLine = *pFar->pLeftLine;
		rightLine = *pFar->pRightLine;
		leftLineSum = (int16_t)(leftLine * 16);
		rightLineSum = (int16_t)(rightLine * 16);
		leftLineStep = roaddraw__slope(*pNear->pLeftLine, rows, leftLine, row);
		rightLineStep =
			roaddraw__slope(*pNear->pRightLine, rows, rightLine, row);
	}

	if ((flags & ROADDRAW__CENTER_LINE) != 0) {
		lineWidth = exe_readS16(roaddraw_walk.lineWidth);
		lineLeft = (int16_t)(((left + right) >> 1) - (lineWidth >> 1));
	}

	if (roaddraw__sign((int16_t)(rows - row)) < 0) {
		return;
	}

	rows = (int16_t)(rows - row);

	uint16_t leftFlags = roaddraw__word(roaddraw_walk.left);
	uint16_t rightFlags = roaddraw__word(roaddraw_walk.right);

	for (; rows != 0; row++) {
		int16_t from;
		int16_t to;
		uint8_t color;

		if (row > ROADDRAW__MIRROR_LAST) {
			return;
		}

		if (!crossing) {
			roaddraw_span(ROADDRAW__MIRROR_FROM, left, row, ROADDRAW__SHOULDER);
			roaddraw_span(right, ROADDRAW__MIRROR_TO, row, ROADDRAW__SHOULDER);
			color = ROADDRAW__ROAD;
			from = left;
			to = right;
		} else {
			if ((flags & ROADDRAW__NOT_DRAWN) != 0) {
				return;
			}

			if ((leftFlags & ROADDRAW__SPLIT) == 0) {
				color = ROADDRAW__GROUND;
				to = leftLine;
			} else {
				to = (int16_t)(((left - leftLine) >> 1) + leftLine);
				roaddraw_span(leftLine, to, row, ROADDRAW__GROUND);
				color = ROADDRAW__SHOULDER;
			}

			roaddraw_span(to, left, row, color);

			if ((rightFlags & ROADDRAW__SPLIT) == 0) {
				color = ROADDRAW__GROUND;
				to = rightLine;
			} else {
				to = (int16_t)(rightLine - ((rightLine - right) >> 1));
				roaddraw_span(to, rightLine, row, ROADDRAW__GROUND);
				color = ROADDRAW__SHOULDER;
			}

			from = right;
		}

		roaddraw_span(from, to, row, color);

		if ((flags & ROADDRAW__CENTER_LINE) != 0) {
			roaddraw_span(
				lineLeft,
				(int16_t)(lineLeft + lineWidth),
				row,
				ROADDRAW__GROUND
			);
		}

		leftSum = (int16_t)(leftSum + leftStep);
		rightSum = (int16_t)(rightSum + rightStep);
		left = (int16_t)(leftSum >> 4);
		right = (int16_t)(rightSum >> 4);

		if (crossing) {
			leftLineSum = (int16_t)(leftLineSum + leftLineStep);
			rightLineSum = (int16_t)(rightLineSum + rightLineStep);
			leftLine = (int16_t)(leftLineSum >> 4);
			rightLine = (int16_t)(rightLineSum >> 4);
		}

		lineLeft = (int16_t)(((left + right) >> 1) - (lineWidth >> 1));
		rows--;
	}
}

/** ****************************************************************************
 *
 * \brief	Draw the roadside of a row ahead, on one side.
 *
 * \param	fromColumn		Column of the object line, first end.
 * \param	toColumn		Column of the object line, second end.
 * \param	lowFrom			Road row at fromColumn.
 * \param	lowTo			Road row at toColumn.
 * \param	highTo			Eye row at toColumn.
 * \param	highFrom		Eye row at fromColumn.
 * \param	left			true: the left side; false: the right.
 *
 * \details	Port of FUN_3645_34da. Nothing with road flag 4. The band
 * 			along the object line, color 3 (2 with side flag 0x10); with
 * 			side flag 4 a second band of color 4 above it, half as high
 * 			(as high with side flag 8). Then the lines of a crossing
 * 			when the tile before has road flag 4, and the end of the
 * 			second band when the tile before has none.
 *
 ******************************************************************************/
static void roaddraw__roadside(
	int16_t fromColumn,
	int16_t toColumn,
	int16_t lowFrom,
	int16_t lowTo,
	int16_t highTo,
	int16_t highFrom,
	bool	left
) {
	uint16_t index = left ? roaddraw_walk.left : roaddraw_walk.right;
	uint16_t sideFlags = roaddraw__word(index);
	uint16_t before = roaddraw__word((uint16_t)(index - 1));

	if ((roaddraw__word(roaddraw_walk.flags) & ROADDRAW__NOT_DRAWN) != 0) {
		return;
	}

	roaddraw__quad(
		(sideFlags & ROADDRAW__LIGHT_SIDE) != 0 ? ROADDRAW__SIDE_LIGHT
												: ROADDRAW__SIDE,
		fromColumn,
		toColumn,
		lowFrom,
		lowTo,
		highTo,
		highFrom
	);

	bool tall = (sideFlags & ROADDRAW__TALL_BAND) != 0;

	if ((sideFlags & ROADDRAW__BAND) != 0) {
		int16_t topFrom;
		int16_t topTo;

		if (!tall) {
			topFrom = (int16_t)(highFrom - ((lowFrom - highFrom) >> 1));
			topTo = (int16_t)(highTo - ((lowTo - highTo) >> 1));
		} else {
			topFrom = (int16_t)(highFrom * 2 - lowFrom);
			topTo = (int16_t)(highTo * 2 - lowTo);
		}

		roaddraw__quad(
			ROADDRAW__GROUND,
			fromColumn,
			toColumn,
			highFrom,
			highTo,
			topTo,
			topFrom
		);
	}

	uint16_t flagsBefore = roaddraw__word((uint16_t)(roaddraw_walk.flags - 1));

	if ((flagsBefore & ROADDRAW__NOT_DRAWN) != 0) {
		roaddraw__crossingLines(
			fromColumn,
			toColumn,
			lowFrom,
			lowTo,
			highTo,
			highFrom,
			left
		);
	}

	if ((sideFlags & ROADDRAW__BAND) != 0 && (before & ROADDRAW__BAND) == 0) {
		int16_t row;

		if (!left) {
			row = !tall ? (int16_t)(highTo - ((lowTo - highTo) >> 1))
						: (int16_t)(highTo * 2 - lowTo);
			roaddraw__bandEnd(fromColumn, toColumn, row, highTo, false);
		} else {
			row = !tall ? (int16_t)(highFrom - ((lowFrom - highFrom) >> 1))
						: (int16_t)(highFrom * 2 - lowFrom);
			roaddraw__bandEnd(fromColumn, toColumn, row, highFrom, true);
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Draw the roadside of a row in the mirror, on one side.
 *
 * \param	fromColumn		Column of the object line, first end.
 * \param	toColumn		Column of the object line, second end.
 * \param	lowFrom			Road row at fromColumn.
 * \param	lowTo			Road row at toColumn.
 * \param	highTo			Eye row at toColumn.
 * \param	highFrom		Eye row at fromColumn.
 * \param	left			true: the left side; false: the right.
 *
 * \details	Port of FUN_3645_37af, the mirror's twin of
 * 			roaddraw__roadside: drawn only at a crossing (road flag 1)
 * 			and without road flag 4; the tile looked at for the lines
 * 			and the band's end is the next one; the second band's
 * 			height depends on the side (right: as high, left: half as
 * 			high), not on side flag 8.
 *
 ******************************************************************************/
static void roaddraw__mirrorRoadside(
	int16_t fromColumn,
	int16_t toColumn,
	int16_t lowFrom,
	int16_t lowTo,
	int16_t highTo,
	int16_t highFrom,
	bool	left
) {
	uint16_t flags = roaddraw__word(roaddraw_walk.flags);

	if ((flags & ROADDRAW__CROSSING) == 0) {
		return;
	}

	uint16_t index = left ? roaddraw_walk.left : roaddraw_walk.right;
	uint16_t sideFlags = roaddraw__word(index);
	uint16_t after = roaddraw__word((uint16_t)(index + 1));
	bool	 band = (sideFlags & ROADDRAW__BAND) != 0;

	if ((flags & ROADDRAW__NOT_DRAWN) != 0) {
		return;
	}

	roaddraw__quad(
		(sideFlags & ROADDRAW__LIGHT_SIDE) != 0 ? ROADDRAW__SIDE_LIGHT
												: ROADDRAW__SIDE,
		fromColumn,
		toColumn,
		lowFrom,
		lowTo,
		highTo,
		highFrom
	);

	if (band) {
		int16_t topFrom;
		int16_t topTo;

		if (!left) {
			topFrom = (int16_t)(highFrom * 2 - lowFrom);
			topTo = (int16_t)(highTo * 2 - lowTo);
		} else {
			topFrom = (int16_t)(highFrom - ((lowFrom - highFrom) >> 1));
			topTo = (int16_t)(highTo - ((lowTo - highTo) >> 1));
		}

		roaddraw__quad(
			ROADDRAW__GROUND,
			fromColumn,
			toColumn,
			highFrom,
			highTo,
			topTo,
			topFrom
		);
	}

	uint16_t flagsAfter = roaddraw__word((uint16_t)(roaddraw_walk.flags + 1));

	if ((flagsAfter & ROADDRAW__NOT_DRAWN) != 0) {
		roaddraw__crossingLines(
			fromColumn,
			toColumn,
			lowFrom,
			lowTo,
			highTo,
			highFrom,
			left
		);
	}

	if (band && (after & ROADDRAW__BAND) == 0) {
		if (!left) {
			roaddraw__bandEnd(
				fromColumn,
				toColumn,
				(int16_t)(highTo * 2 - lowTo),
				highTo,
				false
			);
		} else {
			roaddraw__bandEnd(
				fromColumn,
				toColumn,
				(int16_t)(highFrom - ((lowFrom - highFrom) >> 1)),
				highFrom,
				true
			);
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Draw the lines where a crossing street meets the roadside.
 *
 * \param	fromColumn		Column of the object line, first end.
 * \param	toColumn		Column of the object line, second end.
 * \param	lowFrom			Road row at fromColumn.
 * \param	lowTo			Road row at toColumn.
 * \param	highTo			Eye row at toColumn.
 * \param	highFrom		Eye row at fromColumn.
 * \param	left			true: the left side; false: the right.
 *
 * \details	Part of FUN_3645_34da and FUN_3645_37af (the same code in
 * 			both): a span of color 4 from the side's end to the clip
 * 			box's edge on the row above the road row, then spans of
 * 			color 2 there from the eye row down to the row above that.
 *
 ******************************************************************************/
static void roaddraw__crossingLines(
	int16_t fromColumn,
	int16_t toColumn,
	int16_t lowFrom,
	int16_t lowTo,
	int16_t highTo,
	int16_t highFrom,
	bool	left
) {
	int16_t clipLeft = exe_readS16(ROADDRAW__CLIP_LEFT);
	int16_t clipRight = exe_readS16(ROADDRAW__CLIP_RIGHT);

	if (!left) {
		roaddraw_span(
			toColumn,
			clipRight,
			(int16_t)(lowTo - 1),
			ROADDRAW__GROUND
		);

		for (int16_t row = highTo; row < lowTo - 1; row++) {
			roaddraw_span(toColumn, clipRight, row, ROADDRAW__EDGE);
		}
	} else {
		roaddraw_span(
			clipLeft,
			fromColumn,
			(int16_t)(lowFrom - 1),
			ROADDRAW__GROUND
		);

		for (int16_t row = highFrom; row < lowFrom - 1; row++) {
			roaddraw_span(clipLeft, fromColumn, row, ROADDRAW__EDGE);
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Draw the end of the second roadside band.
 *
 * \param	fromColumn		Column of the object line, first end.
 * \param	toColumn		Column of the object line, second end.
 * \param	row				First row.
 * \param	endRow			Row after the last.
 * \param	left			true: the left side; false: the right.
 *
 * \details	Part of FUN_3645_34da and FUN_3645_37af: spans of color 2,
 * 			on the right from toColumn + 1 to 2 * toColumn - fromColumn,
 * 			on the left from 2 * fromColumn - toColumn to fromColumn - 1.
 *
 ******************************************************************************/
static void roaddraw__bandEnd(
	int16_t fromColumn,
	int16_t toColumn,
	int16_t row,
	int16_t endRow,
	bool	left
) {
	for (; row < endRow; row++) {
		if (!left) {
			roaddraw_span(
				(int16_t)(toColumn + 1),
				(int16_t)(toColumn * 2 - fromColumn),
				row,
				ROADDRAW__EDGE
			);
		} else {
			roaddraw_span(
				(int16_t)(fromColumn * 2 - toColumn),
				(int16_t)(fromColumn - 1),
				row,
				ROADDRAW__EDGE
			);
		}
	}
}
