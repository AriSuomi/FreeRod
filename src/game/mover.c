/** ****************************************************************************
 *
 * \file	mover.c
 *
 * \brief	The sprite mover: a sprite that moves over a background in
 * 			phases, one step per frame.
 *
 * \details	The original keeps one mover at DS:0x7d18: a ring of up to 4
 * 			frames (12-byte entries at DS:0x77ba), the position, the
 * 			surface to restore the background from, up to 2 rectangles
 * 			of a surface drawn in front of the sprite, and a list of
 * 			phases (count, step right, step down). StartSpriteMover sets it
 * 			up, SetMoverPhase sets a phase, and entry 9 of the video
 * 			function table (ScrollCarFrame, DS:0x78c6) draws one step on
 * 			the hidden EGA page, advances it (StepCarScroll) and flips
 * 			the pages. It moves the car into and out of the garage, the
 * 			jack, the car and the wheels of the tire change.
 *
 * 			The port draws on the display page: each step puts the
 * 			background back over the last and the new rectangle, draws
 * 			the frame and the pictures in front of it (clipped to the
 * 			sprite), and waits one tick of the game clock for the page
 * 			flip, which waited for the vertical retrace. The original's
 * 			frames carry the sprite shifted within its byte column, so
 * 			it draws at byte-aligned columns; the port's frames are not
 * 			shifted and are drawn at the exact column.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/mover.h"

#include "game/cursor.h"
#include "game/exe.h"
#include "game/globals.h"
#include "game/state.h"
#include "dos/dos.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define MOVER_CROP		0x2d0 /**< DS: s16, bottom rows not drawn. */
#define MOVER_PHASE_END -1	  /**< Count that ends the phase list. */
#define MOVER_FAR_STEP	8	  /**< Steps of 8 and more: 4 frames on. */
#define MOVER_MID_STEP	4	  /**< Steps of 4 to 7: 2 frames on. */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/**
 * One phase of the movement (4 bytes of the original's phase list).
 */
typedef struct mover__phase_tag {
	int16_t count; /**< Steps left, or MOVER_PHASE_END. */
	int8_t	stepX; /**< Pixels to the right per step. */
	int8_t	stepY; /**< Pixels down per step. */
} mover__Phase;

/**
 * A picture drawn in front of the sprite where the sprite is.
 */
typedef struct mover__front_tag {
	mover_Frame picture; /**< The picture; image 0 is unused. */
	int16_t		left;	 /**< Its left edge on the screen. */
	int16_t		top;	 /**< Its top edge on the screen. */
	int16_t		clip[4]; /**< Limit: left, top, width, height. */
} mover__Front;

/**
 * The mover (DS:0x7d18).
 */
typedef struct mover__mover_tag {
	mover_Frame	 frames[MOVER_FRAMES];	   /**< The frame ring (DS:0x77ba). */
	int16_t		 frameCount;			   /**< Frames in the ring. */
	int16_t		 frame;					   /**< The frame drawn next (+0). */
	int			 background;			   /**< Page with the background. */
	mover__Front fronts[MOVER_FRONTS];	   /**< In front (+0x0a..+0x0c). */
	int16_t		 left;					   /**< Left edge (+0x0e). */
	int16_t		 top;					   /**< Top edge (+0x10). */
	mover__Phase phases[MOVER_PHASES + 1]; /**< Phases (DS:0x764e). */
	int16_t		 phase;					   /**< Current phase (+0x12). */
	int16_t		 drawn[4];				   /**< Last drawn; width 0: none. */
} mover__Mover;

/*******************************************************************************
;
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

static void mover__advance(void);

static bool mover__intersect(
	const int16_t * pFirst,
	const int16_t * pSecond,
	int16_t *		pResult
);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

static mover__Mover mover__mover; /**< The mover (DS:0x7d18). */

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Set up the mover.
 *
 * \param	left			Left edge of the sprite.
 * \param	bottom			Bottom row of the sprite (of the first frame).
 * \param	pFrames			The frames, in the order the mover cycles
 * 							through them.
 * \param	frameCount		Number of frames, 1..MOVER_FRAMES.
 * \param	background		Page the background is put back from, at the
 * 							same place on the screen.
 *
 * \details	Port of StartSpriteMover (1f38:a273, EGA path). The phases are empty
 * 			(mover_setPhase sets them) and nothing is in front of the
 * 			sprite (mover_setFront). The original's second surface,
 * 			which holds the background under the new position and the
 * 			pictures in front, is the background page here; the caller
 * 			keeps that page up to date. The original's flag for frame
 * 			buffers (param 4) is always 0 on EGA and is left out.
 *
 ******************************************************************************/
void mover_start(
	int16_t				left,
	int16_t				bottom,
	const mover_Frame * pFrames,
	int16_t				frameCount,
	int					background
) {
	mover__Mover * pMover = &mover__mover;

	if (frameCount < 1) {
		frameCount = 1;
	}

	if (frameCount > MOVER_FRAMES) {
		frameCount = MOVER_FRAMES;
	}

	for (int index = 0; index < MOVER_FRAMES; index++) {
		pMover->frames[index] = pFrames[index < frameCount ? index : 0];
	}

	for (int index = 0; index < MOVER_FRONTS; index++) {
		pMover->fronts[index].picture.image = 0;
	}

	pMover->frameCount = frameCount;
	pMover->frame = 0;
	pMover->background = background;
	pMover->left = left;
	pMover->top = (int16_t)(bottom - (pFrames[0].height - 1));
	pMover->phase = 0;
	pMover->phases[0].count = MOVER_PHASE_END;
	pMover->drawn[2] = 0;
}

/** ****************************************************************************
 *
 * \brief	Draw a picture in front of the moving sprite.
 *
 * \param	index			Which one, 0..MOVER_FRONTS - 1.
 * \param	pFrame			The picture; NULL or image 0 removes it.
 * \param	left			Its left edge on the screen.
 * \param	top				Its top edge on the screen.
 * \param	pClip			Rectangle it is limited to (left, top, width,
 * 							height), or NULL for none.
 *
 * \details	Stands in for the rectangles of the original's second surface
 * 			(params 10 and 11 of StartSpriteMover), which ScrollCarFrame
 * 			copies over the sprite through that surface's mask. The
 * 			original keeps a picture in that surface; the port keeps the
 * 			picture's image, so a caller that changes the picture sets
 * 			it again.
 *
 ******************************************************************************/
void mover_setFront(
	int					index,
	const mover_Frame * pFrame,
	int16_t				left,
	int16_t				top,
	const int16_t *		pClip
) {
	if (index < 0 || index >= MOVER_FRONTS) {
		return;
	}

	mover__Front * pFront = &mover__mover.fronts[index];

	if (!pFrame) {
		pFront->picture.image = 0;
		return;
	}

	pFront->picture = *pFrame;
	pFront->left = left;
	pFront->top = top;

	if (pClip) {
		for (int part = 0; part < 4; part++) {
			pFront->clip[part] = pClip[part];
		}
	} else {
		pFront->clip[0] = left;
		pFront->clip[1] = top;
		pFront->clip[2] = pFrame->width;
		pFront->clip[3] = pFrame->height;
	}
}

/** ****************************************************************************
 *
 * \brief	Change one frame of the ring.
 *
 * \param	index			The frame, 0..MOVER_FRAMES - 1.
 * \param	pFrame			The new picture.
 *
 * \details	The original changes a frame by copying another sprite's
 * 			header over the one the ring points to (WheelSwapAnimation).
 *
 ******************************************************************************/
void mover_setFrame(
	int16_t				index,
	const mover_Frame * pFrame
) {
	if (index >= 0 && index < MOVER_FRAMES) {
		mover__mover.frames[index] = *pFrame;
	}
}

/** ****************************************************************************
 *
 * \brief	Set a phase of the movement and end the list after it.
 *
 * \param	index			The phase, 0..MOVER_PHASES - 1.
 * \param	count			Steps of the phase.
 * \param	stepX			Pixels to the right per step.
 * \param	stepY			Pixels down per step.
 *
 * \details	Port of SetMoverPhase (1f38:a65d). The step of a phase
 * 			moves the sprite after each step of that phase but the last,
 * 			and after the last step of the phase before it.
 *
 ******************************************************************************/
void mover_setPhase(
	int16_t index,
	int16_t count,
	int8_t	stepX,
	int8_t	stepY
) {
	if (index < 0 || index >= MOVER_PHASES) {
		return;
	}

	mover__Phase * pPhase = &mover__mover.phases[index];

	pPhase[0].count = count;
	pPhase[0].stepX = stepX;
	pPhase[0].stepY = stepY;
	pPhase[1].count = MOVER_PHASE_END;
	pPhase[1].stepX = 0;
	pPhase[1].stepY = 0;
}

/** ****************************************************************************
 *
 * \brief	Draw the sprite at its position and move it on.
 *
 * \retval	true	A step was drawn.
 * \retval	false	The phases are over; nothing was drawn.
 *
 * \details	Port of ScrollCarFrame (31a0:25d9, entry 9 of the EGA video
 * 			function table, DS:0x78c6) with StepCarScroll. The sprite's
 * 			bottom rows given by DS:0x2d0 are left out. Not ported: the
 * 			gaps between the bars (DS:0x284 = 6, the rectangles
 * 			DS:0x539a; cars__scroll has them for cars), and the copy
 * 			between the two EGA pages at the end, which the port,
 * 			drawing on the display page, does not need.
 *
 ******************************************************************************/
bool mover_step(void) {
	mover__Mover * pMover = &mover__mover;

	if (pMover->phases[pMover->phase].count < 0) {
		return false;
	}

	const mover_Frame * pFrame = &pMover->frames[pMover->frame];
	int16_t				crop = exe_readS16(MOVER_CROP);
	int16_t				screen[4] = { 0, 0, PLAT_WIDTH, PLAT_HEIGHT };
	int16_t				sprite[4] = {
		pMover->left,
		pMover->top,
		pFrame->width,
		(int16_t)(pFrame->height - crop),
	};
	int16_t drawn[4];
	bool	visible = mover__intersect(sprite, screen, drawn);
	int		display = globals_displaySurface;

	if (pMover->drawn[2] > 0) {
		plat_copyRect(
			pMover->background,
			display,
			pMover->drawn[0],
			pMover->drawn[1],
			pMover->drawn[2],
			pMover->drawn[3]
		);
		cursor_surfaceCopied(
			pMover->drawn[0],
			pMover->drawn[1],
			pMover->drawn[2],
			pMover->drawn[3]
		);
	}

	if (visible) {
		int clip[4] = { drawn[0], drawn[1], drawn[2], drawn[3] };

		plat_copyRect(
			pMover->background,
			display,
			drawn[0],
			drawn[1],
			drawn[2],
			drawn[3]
		);
		plat_selectPage(display);
		plat_drawImagePart(
			pFrame->image,
			0,
			0,
			pFrame->width,
			sprite[3],
			pMover->left,
			pMover->top,
			clip
		);

		for (int index = 0; index < MOVER_FRONTS; index++) {
			const mover__Front * pFront = &pMover->fronts[index];
			int16_t				 limit[4];

			if (pFront->picture.image == 0 ||
				!mover__intersect(pFront->clip, drawn, limit)) {
				continue;
			}

			int frontClip[4] = { limit[0], limit[1], limit[2], limit[3] };

			plat_drawImagePart(
				pFront->picture.image,
				0,
				0,
				pFront->picture.width,
				pFront->picture.height,
				pFront->left,
				pFront->top,
				frontClip
			);
		}

		cursor_surfaceCopied(drawn[0], drawn[1], drawn[2], drawn[3]);

		for (int part = 0; part < 4; part++) {
			pMover->drawn[part] = drawn[part];
		}
	} else {
		pMover->drawn[2] = 0;
	}

	mover__advance();

	uint32_t startTick = globals_ticks;

	while (globals_ticks == startTick) {
		dos_service();
	}

	return true;
}

/** ****************************************************************************
 *
 * \brief	The top row of the sprite.
 *
 * \return	The top row (word +0x10 of the original's mover).
 *
 ******************************************************************************/
int16_t mover_top(void) {
	return mover__mover.top;
}

/** ****************************************************************************
 *
 * \brief	Move the sprite to another row.
 *
 * \param	top				The new top row.
 *
 * \details	TireChangeAnimation changes word +0x10 of the mover to let
 * 			the car rise only on every other pump of the jack.
 *
 ******************************************************************************/
void mover_setTop(
	int16_t top
) {
	mover__mover.top = top;
}

/** ****************************************************************************
 *
 * \brief	Count a step of the current phase and move the sprite on.
 *
 * \details	Port of StepCarScroll (1f38:aa37). The position before the
 * 			step goes to DS:0x8b7c. A phase whose count reaches 0 gives
 * 			way to the next one, and the next phase's step moves the
 * 			sprite unless that is the end of the list. A step of 2 or 3
 * 			pixels to the side takes the next frame of the ring, 4 to 7
 * 			the one after it, and 8 or more the fourth one on (the
 * 			links +6, +8 and +0x0a of the original's frame entries).
 * 			Not ported: the frame pointer DS:0x288, which nothing reads.
 *
 ******************************************************************************/
static void mover__advance(void) {
	mover__Mover * pMover = &mover__mover;
	mover__Phase * pPhase = &pMover->phases[pMover->phase];

	exe_writeU16(STATE_CAR_SCROLL_X, (uint16_t)pMover->left);
	pPhase->count--;

	if (pPhase->count == 0) {
		pMover->phase++;
		pPhase++;
	}

	if (pPhase->count == MOVER_PHASE_END) {
		return;
	}

	pMover->left = (int16_t)(pMover->left + pPhase->stepX);
	pMover->top = (int16_t)(pMover->top + pPhase->stepY);

	int distance = pPhase->stepX < 0 ? -pPhase->stepX : pPhase->stepX;
	int frames = 1;

	if (distance == 0) {
		return;
	}

	if (distance >= MOVER_FAR_STEP) {
		frames = 4;
	} else if (distance >= MOVER_MID_STEP) {
		frames = 2;
	}

	pMover->frame = (int16_t)((pMover->frame + frames) % pMover->frameCount);
}

/** ****************************************************************************
 *
 * \brief	Intersect two rectangles.
 *
 * \param	pFirst			Left, top, width, height.
 * \param	pSecond			Left, top, width, height.
 * \param	pResult			Output: the rectangle both cover.
 *
 * \retval	true	They overlap.
 * \retval	false	They do not overlap; pResult is not set.
 *
 * \details	Like FUN_1f38_a0fa, without its widening to whole bytes.
 *
 ******************************************************************************/
static bool mover__intersect(
	const int16_t * pFirst,
	const int16_t * pSecond,
	int16_t *		pResult
) {
	int left = pFirst[0] > pSecond[0] ? pFirst[0] : pSecond[0];
	int top = pFirst[1] > pSecond[1] ? pFirst[1] : pSecond[1];
	int firstRight = pFirst[0] + pFirst[2];
	int secondRight = pSecond[0] + pSecond[2];
	int firstBottom = pFirst[1] + pFirst[3];
	int secondBottom = pSecond[1] + pSecond[3];
	int right = firstRight < secondRight ? firstRight : secondRight;
	int bottom = firstBottom < secondBottom ? firstBottom : secondBottom;

	if (right <= left || bottom <= top) {
		return false;
	}

	pResult[0] = (int16_t)left;
	pResult[1] = (int16_t)top;
	pResult[2] = (int16_t)(right - left);
	pResult[3] = (int16_t)(bottom - top);

	return true;
}
