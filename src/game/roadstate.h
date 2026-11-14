/** ****************************************************************************
 *
 * \file	roadstate.h
 *
 * \brief	The state of the 3D road during a drive (segment 3645).
 *
 * \details	Where the player is on the road, the view's heading and
 * 			the rows of the road projected ahead and in the mirror.
 * 			The values the rest of the game reads (tile, X, edges) stay
 * 			in the data segment image; see docs/road.md.
 *
 ******************************************************************************/

#ifndef GAME_ROADSTATE_H_INCLUDED
#define GAME_ROADSTATE_H_INCLUDED

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

#define ROADSTATE_AHEAD_ROWS  15 /**< Rows projected ahead (FUN_3645_18fb). */
#define ROADSTATE_BEHIND_ROWS 8	 /**< Rows in the mirror (FUN_3645_1bcc). */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/**
 * Where the five table pointers of FUN_3645_171d point: indexes of
 * road_table (the original's far pointers into segment 489b).
 */
typedef struct roadstate_cursor_tag {
	uint16_t left;	/**< DS:0x72ee: left side flags. */
	uint16_t right; /**< DS:0x8634: right side flags. */
	uint16_t curve; /**< DS:0x7560: curve. */
	uint16_t flags; /**< DS:0x7d62: road flags. */
	uint16_t slope; /**< DS:0x7880: slope. */
} roadstate_Cursor;

/**
 * Screen values of the projected rows, nearest first. The mirror
 * uses the first ROADSTATE_BEHIND_ROWS entries.
 */
typedef struct roadstate_rows_tag {
	int16_t road[ROADSTATE_AHEAD_ROWS];		 /**< Row of the road surface. */
	int16_t eye[ROADSTATE_AHEAD_ROWS];		 /**< Row at eye height. */
	int16_t left[ROADSTATE_AHEAD_ROWS];		 /**< Column of the left edge. */
	int16_t right[ROADSTATE_AHEAD_ROWS];	 /**< Column of the right edge. */
	int16_t leftLine[ROADSTATE_AHEAD_ROWS];	 /**< Left object line. */
	int16_t rightLine[ROADSTATE_AHEAD_ROWS]; /**< Right object line. */
} roadstate_Rows;

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

extern roadstate_Cursor roadstate_cursor;
extern roadstate_Rows	roadstate_ahead;
extern roadstate_Rows	roadstate_behind;

/*******************************************************************************
;
;	F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

void roadstate_point(
	int16_t tile
);

void roadstate_start(void);

void roadstate_frame(void);

void roadstate_advance(void);

int16_t roadstate_check(void);

#endif // GAME_ROADSTATE_H_INCLUDED
