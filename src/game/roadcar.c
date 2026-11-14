/** ****************************************************************************
 *
 * \file	roadcar.c
 *
 * \brief	The opponent's car on the 3D road (segment 3645).
 *
 * \details	FUN_3645_3f8d: from the distance between the two cars
 * 			it picks the picture of the opponent's car (its
 * 			position), sets whether the cars are side by side and
 * 			whether the player ran into the opponent, and draws the
 * 			picture where the opponent is, ahead in the view or behind
 * 			in the mirror. The view calls it on the row of the
 * 			opponent's tile (roadview_draw). All arithmetic is 16-bit,
 * 			as in the original.
 *
 * 			The pictures by position are those of roadpic_set
 * 			(DS:0x8ace): 0 and 1 beside, 2 just ahead, 3..8 farther
 * 			ahead, 9..12 behind.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/roadcar.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "game/exe.h"
#include "game/globals.h"
#include "game/road.h"
#include "game/roaddraw.h"
#include "game/roadpic.h"
#include "game/roadstate.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define ROADCAR__CLIP_LEFT	   0x5e4e /**< s16: clip box, left column. */
#define ROADCAR__CLIP_RIGHT	   0x5e54 /**< s16: clip box, right column. */
#define ROADCAR__UNKNOWN_5EC6  0x5ec6 /**< s16: written only (see below). */
#define ROADCAR__CREST		   0x6e04 /**< s16: highest road row on screen. */
#define ROADCAR__LAST_SPOT	   0x6e2a /**< s16: the position before. */
#define ROADCAR__HIT		   0x72f2 /**< s16: 1: ran into the opponent. */
#define ROADCAR__TILE		   0x7d30 /**< s16: tile of the player. */
#define ROADCAR__EIGHTHS	   0x7d32 /**< s16: eighths of a tile. */
#define ROADCAR__SPEED		   0x7d34 /**< s16: player's eighths a frame. */
#define ROADCAR__X			   0x7d36 /**< s16: player's X. */
#define ROADCAR__HEADING	   0x7d3c /**< s16: curve the view is turned to. */
#define ROADCAR__SLOPE		   0x7d3e /**< s16: slope at the player's tile. */
#define ROADCAR__SPOT		   0x7d7c /**< s16: the opponent's position. */
#define ROADCAR__RIVAL_TILE	   0x7e8a /**< s16: tile of the opponent. */
#define ROADCAR__RIVAL_EIGHTHS 0x7e8c /**< s16: its eighths of a tile. */
#define ROADCAR__RIVAL_SPEED   0x7e8e /**< s16: its eighths a frame. */
#define ROADCAR__RIVAL_X	   0x7e90 /**< s16: its X. */
#define ROADCAR__SIDE_BY_SIDE  0x8bc2 /**< s16: 1: the cars are beside. */

#define ROADCAR__FAST_MACHINE  4	/**< Machine speed that steps. */
#define ROADCAR__GONE		   -1	/**< Position: out of sight. */
#define ROADCAR__BESIDE_REAR   0	/**< Position: beside, its rear. */
#define ROADCAR__BESIDE		   1	/**< Position: beside, ahead. */
#define ROADCAR__JUST_AHEAD	   2	/**< Position: just ahead. */
#define ROADCAR__AHEAD		   3	/**< Position: ahead. */
#define ROADCAR__FAR_AHEAD	   8	/**< Position: farthest ahead. */
#define ROADCAR__BEHIND		   9	/**< Position: just behind. */
#define ROADCAR__FAR_BEHIND	   12	/**< Position: farthest behind. */
#define ROADCAR__OUT_OF_SIGHT  0x78 /**< Eighths ahead it is not seen. */
#define ROADCAR__CLOSE		   0xb	/**< Eighths ahead it can be hit. */
#define ROADCAR__ACROSS		   0xb4 /**< X apart the cars overlap. */
#define ROADCAR__ACROSS_SLOW   100	/**< The same to be hit, slow machine. */
#define ROADCAR__GROUND		   2000 /**< Height of the road. */

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

static int16_t roadcar__aim(
	int16_t distance,
	int16_t apart,
	int16_t across,
	int16_t current
);

static bool roadcar__step(
	int16_t * pPosition,
	int16_t	  current,
	int16_t	  apart
);

static uint16_t roadcar__word(
	uint16_t index
);

static void roadcar__blit(
	const roadpic_Picture * pPicture,
	const roaddraw_Rect *	pRect
);

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Pick the opponent's position and draw its car.
 *
 * \details	Port of FUN_3645_3f8d. The distance is the opponent's place
 * 			less the player's in eighths of a tile, the X apart the
 * 			opponent's X less the player's. The position aimed at
 * 			(roadcar__aim) is taken at once on a slow machine; on a
 * 			fast one (DS:0x8acc above 3) the position moves one step
 * 			a frame towards it (roadcar__step), and in two cases the
 * 			function ends there with nothing set or drawn.
 *
 * 			DS:0x8bc2 is set when the car is beside (0) or just ahead
 * 			(2) less than 0xb eighths; DS:0x72f2 when, besides that,
 * 			the cars are less than 0xb4 (slow machine: 100) apart
 * 			and the opponent is slower. DS:0x5ec6 gets the eighths
 * 			ahead plus 8 modulo 16 (0 up to 0x10 ahead); nothing
 * 			reads it.
 *
 * 			The car is placed with roadstate_aheadColumn and
 * 			roadstate_aheadRow 4 eighths farther (16 more beside, 8
 * 			more just ahead when it is more than 2 behind), or in the
 * 			mirror with roadstate_behindColumn and
 * 			roadstate_behindRow. Beside, the picture's rear part
 * 			shows at the left edge (0) or its right edge is at the
 * 			column, 0x11 rows lower (1); otherwise it is centered on
 * 			the column with its bottom at the row, ahead not above
 * 			the crest (DS:0x6e04), just ahead not below row 0x65.
 *
 * 			The clip box is switched to the view (positions below 9)
 * 			or the mirror when it is not already, and switched back
 * 			only when the picture is drawn; it is drawn when it lies
 * 			between the box's left and right column, clipped to its
 * 			rows (behind also to its left column), with its mask
 * 			(video table entry 2, DS:0x78aa) on the view surface.
 *
 * 			Not part of the original: a position outside 0..12 or
 * 			without a picture draws nothing (the original reads
 * 			beyond the table; only a slow machine gets such a
 * 			position, and the port is a fast one).
 *
 ******************************************************************************/
void roadcar_draw(void) {
	int16_t tile = exe_readS16(ROADCAR__RIVAL_TILE);
	int16_t distance = (int16_t)(((tile - exe_readS16(ROADCAR__TILE)) * 8 +
								  exe_readS16(ROADCAR__RIVAL_EIGHTHS)) -
								 exe_readS16(ROADCAR__EIGHTHS));
	int16_t apart =
		(int16_t)(exe_readS16(ROADCAR__RIVAL_X) - exe_readS16(ROADCAR__X));
	int16_t across = (int16_t)(apart < 0 ? -apart : apart);
	int16_t current = exe_readS16(ROADCAR__SPOT);
	int16_t position = roadcar__aim(distance, apart, across, current);

	if (globals_machineSpeed >= ROADCAR__FAST_MACHINE && position != current &&
		!roadcar__step(&position, current, apart)) {
		return;
	}

	bool close = position == ROADCAR__JUST_AHEAD && distance < ROADCAR__CLOSE;
	bool hit = false;

	exe_writeU16(
		ROADCAR__SIDE_BY_SIDE,
		position == ROADCAR__BESIDE_REAR || close ? 1 : 0
	);

	if (close && across < ROADCAR__ACROSS) {
		int16_t limit = globals_machineSpeed < ROADCAR__FAST_MACHINE
			? ROADCAR__ACROSS_SLOW
			: ROADCAR__ACROSS;

		int16_t rivalSpeed = exe_readS16(ROADCAR__RIVAL_SPEED);

		hit = across < limit && rivalSpeed < exe_readS16(ROADCAR__SPEED);
	}

	exe_writeU16(ROADCAR__HIT, hit ? 1 : 0);
	exe_writeU16(ROADCAR__LAST_SPOT, (uint16_t)current);
	exe_writeU16(ROADCAR__SPOT, (uint16_t)position);
	exe_writeU16(
		ROADCAR__UNKNOWN_5EC6,
		distance <= 0x10 ? 0 : (uint16_t)((distance + 8) & 0xf)
	);

	if (position == ROADCAR__BESIDE_REAR) {
		distance = (int16_t)(distance + 0x10);
	}

	if (distance < -2 && position == ROADCAR__JUST_AHEAD) {
		distance = (int16_t)(distance + 8);
	}

	int16_t rise = (int16_t)(exe_readS16(ROADCAR__SLOPE) -
							 (int16_t)roadcar__word(
								 (uint16_t)(ROAD_SLOPE + tile)
							 ));
	int16_t turn = (int16_t)(exe_readS16(ROADCAR__HEADING) -
							 (int16_t)roadcar__word(
								 (uint16_t)(ROAD_CURVE + tile)
							 ));
	int16_t rivalX = exe_readS16(ROADCAR__RIVAL_X);
	int16_t column;
	int16_t row;

	if (position < ROADCAR__BEHIND) {
		int16_t ahead = (int16_t)(distance + 4);

		column = roadstate_aheadColumn(ahead, rivalX, turn);
		row = roadstate_aheadRow(ahead, ROADCAR__GROUND, rise);
	} else {
		int16_t behind = (int16_t)-distance;

		column = roadstate_behindColumn(behind, rivalX, (int16_t)-turn);
		row = roadstate_behindRow(behind, ROADCAR__GROUND, (int16_t)-rise);
	}

	if (position < 0 || position >= ROADPIC_POSITIONS ||
		roadpic_set.pOpponent[position] == NULL) {
		return;
	}

	const roadpic_Picture * pPicture = roadpic_set.pOpponent[position];

	roaddraw_Rect rect = { pPicture->width, pPicture->height, 0, 0, 0, 0 };

	if (position == ROADCAR__BESIDE_REAR) {
		rect.sourceLeft = (int16_t)((rect.width - column) + 0x40);

		if (rect.sourceLeft < 0) {
			rect.sourceLeft = 0;
		}

		rect.top = row;
	} else if (position == ROADCAR__BESIDE) {
		rect.left = (int16_t)(column - rect.width);
		rect.top = (int16_t)((row - rect.height) + 0x11);
	} else {
		rect.left = (int16_t)(column - (rect.width >> 1));

		if (position < ROADCAR__BEHIND && row < exe_readS16(ROADCAR__CREST)) {
			row = exe_readS16(ROADCAR__CREST);
		}

		rect.top = (int16_t)(row - rect.height);

		if (position == ROADCAR__JUST_AHEAD && 100 < rect.top + rect.height) {
			rect.top = (int16_t)(0x65 - rect.height);
		}
	}

	bool switched = false;

	if (position < ROADCAR__BEHIND) {
		if (exe_readS16(ROADCAR__CLIP_LEFT) != 0) {
			switched = true;
			roaddraw_clipView();
		}
	} else if (exe_readS16(ROADCAR__CLIP_LEFT) == 0) {
		switched = true;
		roaddraw_clipMirror();
	}

	if (exe_readS16(ROADCAR__CLIP_LEFT) > rect.left + rect.width ||
		rect.left >= exe_readS16(ROADCAR__CLIP_RIGHT)) {
		return;
	}

	if (position < ROADCAR__BEHIND) {
		roaddraw_clipRows(&rect);
	} else {
		roaddraw_clipRowsLeft(&rect);
	}

	roadcar__blit(pPicture, &rect);

	if (switched) {
		if (exe_readS16(ROADCAR__CLIP_LEFT) == 0) {
			roaddraw_clipMirror();
		} else {
			roaddraw_clipView();
		}
	}
}

/** ****************************************************************************
 *
 * \brief	The position the distance and X apart call for.
 *
 * \param	distance		Eighths of a tile the opponent is ahead.
 * \param	apart			Opponent's X less the player's.
 * \param	across			Its magnitude (16-bit).
 * \param	current			The position now (DS:0x7d7c).
 *
 * \return	-1 out of sight (0x78 eighths ahead or more); 2 + the
 * 			eighths ahead plus 8 over 16, at most 8, from 0x10 ahead;
 * 			8 + 0x10 less the eighths over 16 more than 7 behind
 * 			(this can pass 12); up to 8 ahead or 7 behind: 2 (ahead)
 * 			or 9 (behind), keeping the side of the current position,
 * 			when less than 0xb4 apart, else 0 to the left or 2 to the
 * 			right; 9..0xf ahead: 1 more than 0xb4 to the left, else 3.
 *
 * \details	Part of FUN_3645_3f8d.
 *
 ******************************************************************************/
static int16_t roadcar__aim(
	int16_t distance,
	int16_t apart,
	int16_t across,
	int16_t current
) {
	if (distance >= ROADCAR__OUT_OF_SIGHT) {
		return ROADCAR__GONE;
	}

	if (distance >= 0x10) {
		int16_t position = (int16_t)(((distance + 8) >> 4) + 2);

		return position > ROADCAR__FAR_AHEAD ? ROADCAR__FAR_AHEAD : position;
	}

	if (distance < -7) {
		return (int16_t)(((0x10 - distance) >> 4) + ROADCAR__FAR_AHEAD);
	}

	if (distance < 9) {
		if (across < ROADCAR__ACROSS) {
			return current < ROADCAR__BEHIND ? ROADCAR__JUST_AHEAD
											 : ROADCAR__BEHIND;
		}

		return apart < 0 ? ROADCAR__BESIDE_REAR : ROADCAR__JUST_AHEAD;
	}

	return apart < -ROADCAR__ACROSS ? ROADCAR__BESIDE : ROADCAR__AHEAD;
}

/** ****************************************************************************
 *
 * \brief	Move the position one step towards the one aimed at.
 *
 * \param	pPosition		In: the position aimed at, not the current
 * 							one; out: the position taken.
 * \param	current			The position now (DS:0x7d7c).
 * \param	apart			Opponent's X less the player's.
 *
 * \retval	true			The position is taken.
 * \retval	false			Nothing is to be set or drawn: out of sight
 * 							while at 8, or beyond 12 while at 12.
 *
 * \details	Part of FUN_3645_3f8d (fast machine). Out of sight or
 * 			beyond 12 it counts up from the current position. The
 * 			position before the current one (DS:0x6e2a) is taken at
 * 			once. Between ahead (below 9) and behind (above 9) it
 * 			passes 9 or 0: from behind to ahead and from ahead (1..8)
 * 			to behind it counts down; at 0 it goes to 1 or 9; at 9
 * 			to 0 (to the left) or 2, or to 10; at 1 to 0 or 3; at 3
 * 			to 1 directly; otherwise it counts towards the aim.
 *
 ******************************************************************************/
static bool roadcar__step(
	int16_t * pPosition,
	int16_t	  current,
	int16_t	  apart
) {
	int16_t aim = *pPosition;

	if (aim < 0) {
		if (current == ROADCAR__FAR_AHEAD) {
			return false;
		}

		*pPosition = (int16_t)(current + 1);

		return true;
	}

	if (aim >= ROADPIC_POSITIONS) {
		if (current == ROADCAR__FAR_BEHIND) {
			return false;
		}

		*pPosition = (int16_t)(current + 1);

		return true;
	}

	if (aim == exe_readS16(ROADCAR__LAST_SPOT)) {
		return true;
	}

	if ((aim < ROADCAR__BEHIND && current > ROADCAR__BEHIND) ||
		(aim > ROADCAR__FAR_AHEAD && current < ROADCAR__BEHIND &&
		 current > 0)) {
		*pPosition = (int16_t)(current - 1);
	} else if (current == ROADCAR__BESIDE_REAR) {
		*pPosition = aim < ROADCAR__BEHIND ? ROADCAR__BESIDE : ROADCAR__BEHIND;
	} else if (current == ROADCAR__BEHIND) {
		if (aim >= ROADCAR__BEHIND) {
			*pPosition = ROADCAR__BEHIND + 1;
		} else {
			*pPosition =
				apart < 0 ? ROADCAR__BESIDE_REAR : ROADCAR__JUST_AHEAD;
		}
	} else if (current == ROADCAR__BESIDE) {
		*pPosition = aim < 1 ? ROADCAR__BESIDE_REAR : ROADCAR__AHEAD;
	} else if (current != ROADCAR__AHEAD || aim != ROADCAR__BESIDE) {
		*pPosition = (int16_t)(current < aim ? current + 1 : current - 1);
	}

	return true;
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
static uint16_t roadcar__word(
	uint16_t index
) {
	if (index >= ROAD_WORDS) {
		return 0;
	}

	return road_table[index];
}

/** ****************************************************************************
 *
 * \brief	Draw a part of a picture with its mask on the view.
 *
 * \param	pPicture		The picture.
 * \param	pRect			The part and where it goes.
 *
 * \details	Video table entry 2 (DS:0x78aa) on DS:0x8188: color 0
 * 			stays see-through. A rectangle without rows or columns is
 * 			not drawn.
 *
 ******************************************************************************/
static void roadcar__blit(
	const roadpic_Picture * pPicture,
	const roaddraw_Rect *	pRect
) {
	if (pRect->width <= 0 || pRect->height <= 0) {
		return;
	}

	plat_selectPage(roaddraw_view);
	plat_drawImagePart(
		pPicture->image,
		pRect->sourceLeft,
		pRect->sourceTop,
		pRect->width,
		pRect->height,
		pRect->left,
		pRect->top,
		NULL
	);
}
