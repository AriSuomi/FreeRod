/** ****************************************************************************
 *
 * \file	anim.c
 *
 * \brief	The timed event queue and the animations it steps.
 *
 * \details	The queue (DS:0x566..0x5bb) holds up to 5 events sorted by
 * 			their tick; event_wait takes the first one when its tick has
 * 			come. An event of kind 1 makes the next steps of an animation
 * 			run. Animations are lists of 6-byte steps in the data
 * 			segment (DS:0x58f0; the first step of animation N is word N
 * 			of DS:0x59f8, and word +2 of that header step holds the slot
 * 			the animation runs in, -1 when it does not run). Up to 5 run
 * 			at once, in the slots at DS:0x7568 (0x2c bytes each). Only
 * 			animations 2, 3 and 8 have steps; 1 and 4..7 end at once.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/anim.h"

#include <stdbool.h>
#include <stdint.h>

#include "game/cars.h"
#include "game/cursor.h"
#include "game/event.h"
#include "game/exe.h"
#include "game/globals.h"
#include "game/screen.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define ANIM_STEPS		  0x58f0 /**< DS offset: the steps, 6 bytes each. */
#define ANIM_STEP_SIZE	  6		 /**< Bytes per step. */
#define ANIM_STEP_SLOT	  2		 /**< Header step: s16 slot, -1: not running. */
#define ANIM_FIRST_STEP	  0x59f8 /**< DS offset: header step per animation. */
#define ANIM_PRIORITY	  0x8ef6 /**< DS offset: s16 priority of new events. */

#define ANIM_SLOTS		  5 /**< Animations running at once at most. */
#define ANIM_EVENTS		  5 /**< Events in the queue at most. */

#define ANIM_EVENT_NONE	  0 /**< Event kind: nothing (cancelled). */
#define ANIM_EVENT_STEP	  1 /**< Event kind: run steps of an animation. */

#define ANIM_POINT_SOURCE 1 /**< Point argument: source in the image. */
#define ANIM_POINT_TARGET 2 /**< Point argument: place on the surface. */
#define ANIM_POINT_BOTH	  3 /**< Point argument: both. */
#define ANIM_SHOW_AREA	  4 /**< Byte +1 of a wait: show the area. */

#define ANIM_METER_LEFT	  0x30 /**< Left meter digit's column. */
#define ANIM_METER_RIGHT  0x33 /**< Right meter digit's column. */
#define ANIM_METER_ROW	  0x6f /**< Row of the meter's flashing lines. */
#define ANIM_METER_WIDTH  2	   /**< Width of each line. */
#define ANIM_METER_LIT	  4	   /**< Color of a lit line. */

#define ANIM_PLAY_WAIT	  10 /**< Ticks per event_wait in anim_play. */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/**
 * Opcodes of the steps (byte +0; header steps hold the animation's
 * number there, which is not run).
 */
typedef enum {
	anim__OP_DRAW = 2,	   /**< Set a point, copy the area plainly. */
	anim__OP_MASKED = 4,   /**< Set a point, copy it with color 0 clear. */
	anim__OP_CHECK = 5,	   /**< The animation's own code; may end it. */
	anim__OP_END = 6,	   /**< End the animation. */
	anim__OP_JUMP = 7,	   /**< Go on at step word +2. */
	anim__OP_IMAGE = 8,	   /**< Load resource word +2 as the image. */
	anim__OP_POINT = 9,	   /**< Set a point (byte +1). */
	anim__OP_SIZE = 10,	   /**< Set width (word +2) and height (+4). */
	anim__OP_WAIT = 11,	   /**< Wait word +2 ticks, go on at step +4. */
	anim__OP_SHOW = 12,	   /**< Show the cursor and the area. */
	anim__OP_CAR_OVER = 13 /**< Draw the parked car over the frames. */
} anim__Op;

/**
 * A running animation (DS:0x7568, 0x2c bytes each). The area (+0x10) is
 * the rectangle the video table's blits get.
 */
typedef struct anim__slot_tag {
	int16_t animation;	/**< +0x00: its number, -1: slot free. */
	int16_t step;		/**< +0x02: step to go on with. */
	int16_t pending;	/**< +0x04: events of it in the queue. */
	int16_t image;		/**< +0x08: resource drawn from (a far pointer). */
	int16_t width;		/**< +0x10: width of the area. */
	int16_t height;		/**< +0x12: height of the area. */
	int16_t sourceLeft; /**< +0x14: left edge in the image. */
	int16_t sourceTop;	/**< +0x16: top edge in the image. */
	int16_t left;		/**< +0x18: left edge on the work surface. */
	int16_t top;		/**< +0x1a: top edge on the work surface. */
	bool	carOver;	/**< Port: the parked car goes over each frame. */
} anim__Slot;

/**
 * An event of the queue (DS:0x572, 0x12 bytes each).
 */
typedef struct anim__event_tag {
	int16_t earlier;  /**< +0x00: event before it (free list: next). */
	int16_t later;	  /**< +0x02: event after it. */
	int32_t time;	  /**< +0x04: tick it is due. */
	int16_t priority; /**< +0x08: of events due at once, higher first. */
	int16_t kind;	  /**< +0x0a: ANIM_EVENT_*. */
	int16_t param;	  /**< +0x0c: the animation of ANIM_EVENT_STEP. */
} anim__Event;

/*******************************************************************************
;
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

static uint16_t anim__header(
	int16_t animation
);

static int16_t anim__slotOf(
	int16_t animation
);

static void anim__run(
	int16_t slot
);

static void anim__setPoint(
	anim__Slot * pSlot,
	int8_t		 which,
	int16_t		 column,
	int16_t		 row
);

static void anim__draw(
	const anim__Slot * pSlot,
	bool			   opaque
);

static void anim__showArea(
	const anim__Slot * pSlot
);

static int16_t anim__check(
	const anim__Slot * pSlot,
	int16_t			   argument
);

static void anim__step(
	int16_t animation
);

static void anim__schedule(
	int16_t animation,
	int16_t delay
);

static int16_t anim__insert(
	int16_t delay,
	int16_t priority
);

static void anim__remove(
	int16_t event
);

static void anim__cancel(
	int16_t animation
);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/** DS:0x7568: the slots of running animations. */
static anim__Slot anim__slots[ANIM_SLOTS] = {
	{ .animation = -1 },
	{ .animation = -1 },
	{ .animation = -1 },
	{ .animation = -1 },
	{ .animation = -1 },
};

/** DS:0x572: the events, chained into the queue or the free list. */
static anim__Event anim__events[ANIM_EVENTS] = {
	{ .earlier = 1 },
	{ .earlier = 2 },
	{ .earlier = 3 },
	{ .earlier = 4 },
	{ .earlier = -1 },
};

static int16_t anim__first = -1; /**< DS:0x566: first event, -1: none. */
static int16_t anim__last = -1;	 /**< DS:0x568: last event, -1: none. */
static int16_t anim__free = 0;	 /**< DS:0x56a: first free event. */
static int16_t anim__meter;		 /**< DS:0x6c3e: meter flashes left. */

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Start an animation.
 *
 * \param	animation		Its number (1..8).
 *
 * \return	The slot it runs in, ANIM_SLOTS when all are taken (then it
 * 			does not run).
 *
 * \details	Port of FUN_1f38_1dc9. The first steps run at once, up to
 * 			the first wait.
 *
 ******************************************************************************/
int16_t anim_start(
	int16_t animation
) {
	int16_t slot;

	for (slot = 0; slot < ANIM_SLOTS; slot++) {
		anim__Slot * pSlot = &anim__slots[slot];

		if (pSlot->animation == -1) {
			int16_t first = exe_readS16(
				(uint16_t)(ANIM_FIRST_STEP + animation * 2)
			);

			exe_writeU16(
				(uint16_t)(anim__header(animation) + ANIM_STEP_SLOT),
				(uint16_t)slot
			);
			pSlot->animation = animation;
			pSlot->step = (int16_t)(first + 1);
			pSlot->pending = 0;
			pSlot->carOver = false;
			anim__run(slot);

			return slot;
		}
	}

	return slot;
}

/** ****************************************************************************
 *
 * \brief	Tell whether an animation runs.
 *
 * \param	animation		Its number.
 *
 * \return	true while it runs.
 *
 * \details	Port of FUN_1f38_1e72.
 *
 ******************************************************************************/
bool anim_isRunning(
	int16_t animation
) {
	return anim__slotOf(animation) >= 0;
}

/** ****************************************************************************
 *
 * \brief	Stop an animation.
 *
 * \param	animation		Its number.
 *
 * \details	Port of FUN_1f38_19ec. Its slot is freed; when an event of
 * 			it is pending, FUN_1000_19bc cancels it (anim__cancel, which
 * 			only looks at the first event). The original runs on with
 * 			slot -1 for an animation that does not run; the port does
 * 			nothing then.
 *
 ******************************************************************************/
void anim_stop(
	int16_t animation
) {
	int16_t slot = anim__slotOf(animation);

	if (slot < 0) {
		plat_log("anim_stop: animation %d does not run", animation);

		return;
	}

	anim__Slot * pSlot = &anim__slots[slot];

	pSlot->image = 0;
	exe_writeU16(
		(uint16_t)(anim__header(animation) + ANIM_STEP_SLOT),
		0xffff
	);
	pSlot->animation = -1;

	if (pSlot->pending != 0) {
		anim__cancel(animation);
	}
}

/** ****************************************************************************
 *
 * \brief	Run an animation to its end.
 *
 * \param	animation		Its number.
 *
 * \details	Port of FUN_1000_4fca: starts it and waits for events (10
 * 			ticks at a time, results ignored) while it runs.
 *
 ******************************************************************************/
void anim_play(
	int16_t animation
) {
	anim_start(animation);

	while (anim_isRunning(animation)) {
		event_wait(ANIM_PLAY_WAIT);
	}
}

/** ****************************************************************************
 *
 * \brief	Empty the timed event queue.
 *
 * \details	The start of FlushInput (1000:0e24): all events go into the
 * 			free list. Running animations keep their slots (an animation
 * 			waiting for an event then stands still). Not ported: the
 * 			flags DS:0x570 and DS:0x571 (0x571 is set while the queue
 * 			changes; nothing reads either in the port).
 *
 ******************************************************************************/
void anim_resetQueue(void) {
	anim__last = -1;
	anim__first = -1;
	anim__free = 0;
	globals_timedEventTime = GLOBALS_NO_EVENT;

	for (int16_t event = 0; event < ANIM_EVENTS; event++) {
		anim__events[event].earlier = (int16_t)(event + 1);
	}

	anim__events[ANIM_EVENTS - 1].earlier = -1;
}

/** ****************************************************************************
 *
 * \brief	Handle the first event of the queue when it is due.
 *
 * \details	The part of WaitEvent (1000:1417) that runs on each turn of
 * 			its loop: the event is taken from the queue (FUN_1000_0f0b)
 * 			and, for kind 1, its animation runs on (FUN_1f38_1e41).
 *
 ******************************************************************************/
void anim_timedEvent(void) {
	if ((int32_t)globals_timedEventTime > (int32_t)globals_ticks) {
		return;
	}

	int16_t event = anim__first;

	if (event < 0) {
		globals_timedEventTime = GLOBALS_NO_EVENT;

		return;
	}

	int16_t kind = anim__events[event].kind;
	int16_t animation = anim__events[event].param;

	anim__remove(event);

	if (kind == ANIM_EVENT_STEP) {
		anim__step(animation);
	}
}

/** ****************************************************************************
 *
 * \brief	Find the header step of an animation.
 *
 * \param	animation		Its number.
 *
 * \return	DS offset of the step.
 *
 ******************************************************************************/
static uint16_t anim__header(
	int16_t animation
) {
	int16_t first =
		exe_readS16((uint16_t)(ANIM_FIRST_STEP + animation * 2));

	return (uint16_t)(ANIM_STEPS + first * ANIM_STEP_SIZE);
}

/** ****************************************************************************
 *
 * \brief	Find the slot an animation runs in.
 *
 * \param	animation		Its number.
 *
 * \return	The slot, < 0 when it does not run.
 *
 ******************************************************************************/
static int16_t anim__slotOf(
	int16_t animation
) {
	return exe_readS16(
		(uint16_t)(anim__header(animation) + ANIM_STEP_SLOT)
	);
}

/** ****************************************************************************
 *
 * \brief	Run the steps of an animation up to its next wait or its end.
 *
 * \param	slot			The slot it runs in.
 *
 * \details	Port of FUN_1f38_1abd. The cursor is hidden while it runs
 * 			and shown again at a wait (unless step 12 showed it already)
 * 			or at the end. The step counter (local -0x24) also counts
 * 			jumps, as in the original: a wait without a step number to
 * 			go on with stores it.
 *
 ******************************************************************************/
static void anim__run(
	int16_t slot
) {
	anim__Slot * pSlot = &anim__slots[slot];
	bool		 hidden = true;
	int16_t		 index = pSlot->step;
	uint16_t	 stepAt = (uint16_t)(ANIM_STEPS + index * ANIM_STEP_SIZE);

	cursor_set(CURSOR_HIDE);

	while (index >= 0) {
		const uint8_t * pStep = exe_data(stepAt);
		int8_t			which = (int8_t)pStep[1];
		int16_t			first = exe_readS16((uint16_t)(stepAt + 2));
		int16_t			second = exe_readS16((uint16_t)(stepAt + 4));

		switch ((int8_t)pStep[0]) {
			case anim__OP_DRAW:
				anim__setPoint(pSlot, which, first, second);
				anim__draw(pSlot, true);
				break;

			case anim__OP_MASKED:
				anim__setPoint(pSlot, which, first, second);
				anim__draw(pSlot, false);
				break;

			case anim__OP_CHECK:
				if (anim__check(pSlot, first) == 0) {
					break;
				}

				anim_stop(pSlot->animation);
				cursor_set(CURSOR_SHOW);

				return;

			case anim__OP_END:
				anim_stop(pSlot->animation);
				cursor_set(CURSOR_SHOW);

				return;

			case anim__OP_JUMP:
				stepAt = (uint16_t)(ANIM_STEPS + first * ANIM_STEP_SIZE);
				index++;
				continue;

			case anim__OP_IMAGE:
				pSlot->image = first;
				break;

			case anim__OP_POINT:
				anim__setPoint(pSlot, which, first, second);
				break;

			case anim__OP_SIZE:
				pSlot->width = first;
				pSlot->height = second;
				break;

			case anim__OP_WAIT:
				pSlot->step = second;

				if (second == 0) {
					index++;
					pSlot->step = index;
				}

				anim__schedule(pSlot->animation, first);
				pSlot->pending++;

				if (hidden) {
					cursor_set(CURSOR_SHOW);
				}

				if (which == ANIM_SHOW_AREA && globals_drawThrough != 0) {
					anim__showArea(pSlot);
				}

				return;

			case anim__OP_SHOW:
				cursor_set(CURSOR_SHOW);
				hidden = false;

				if (globals_drawThrough != 0) {
					anim__showArea(pSlot);
				}

				break;

			case anim__OP_CAR_OVER:
				pSlot->carOver = true;
				break;

			default:
				break;
		}

		stepAt = (uint16_t)(stepAt + ANIM_STEP_SIZE);
		index++;
	}
}

/** ****************************************************************************
 *
 * \brief	Set the source and/or the place of an animation's area.
 *
 * \param	pSlot			The animation.
 * \param	which			ANIM_POINT_SOURCE, _TARGET or _BOTH; other
 * 							values set nothing.
 * \param	column			Left edge.
 * \param	row				Top edge.
 *
 * \details	Port of FUN_1f38_1a5f (near, not a function in Ghidra).
 *
 ******************************************************************************/
static void anim__setPoint(
	anim__Slot * pSlot,
	int8_t		 which,
	int16_t		 column,
	int16_t		 row
) {
	if (which == ANIM_POINT_SOURCE || which == ANIM_POINT_BOTH) {
		pSlot->sourceLeft = column;
		pSlot->sourceTop = row;
	}

	if (which == ANIM_POINT_TARGET || which == ANIM_POINT_BOTH) {
		pSlot->left = column;
		pSlot->top = row;
	}
}

/** ****************************************************************************
 *
 * \brief	Copy the area of an animation's image to the work surface.
 *
 * \param	pSlot			The animation.
 * \param	opaque			true: a plain copy (video table entry 0,
 * 							DS:0x78a2); false: color 0 stays see-through
 * 							(entry 2, DS:0x78aa).
 *
 * \details	Step 13 (the code at 1f38:1b4a) makes the original draw the
 * 			part of the parked car that overlaps the area (FUN_1f38_a198)
 * 			into every frame of the image; the port draws the car over
 * 			the area after each copy instead (cars_drawOnWorkPart), so
 * 			replacement images stay as they are.
 *
 ******************************************************************************/
static void anim__draw(
	const anim__Slot * pSlot,
	bool			   opaque
) {
	screen_drawSpritePart(
		globals_workSurface,
		pSlot->image,
		pSlot->sourceLeft,
		pSlot->sourceTop,
		pSlot->width,
		pSlot->height,
		pSlot->left,
		pSlot->top,
		opaque
	);

	if (pSlot->carOver) {
		cars_drawOnWorkPart(
			pSlot->left,
			pSlot->top,
			pSlot->width,
			pSlot->height
		);
	}
}

/** ****************************************************************************
 *
 * \brief	Copy the area of an animation to the display.
 *
 * \param	pSlot			The animation.
 *
 ******************************************************************************/
static void anim__showArea(
	const anim__Slot * pSlot
) {
	screen_copyToDisplay(
		pSlot->left,
		pSlot->top,
		(int16_t)(pSlot->left + pSlot->width - 1),
		(int16_t)(pSlot->top + pSlot->height - 1)
	);
}

/** ****************************************************************************
 *
 * \brief	Run the own code of an animation (step 5).
 *
 * \param	pSlot			The animation.
 * \param	argument		Word +2 of the step.
 *
 * \return	0 to go on, else the animation ends.
 *
 * \details	Port of FUN_1f38_1e93. Only animation 2 has code: the pump's
 * 			meter. Called with an argument, it sets the number of
 * 			flashes; without, it flashes the meter's lines (the right
 * 			one on every step, the left one on every 4th) and ends the
 * 			animation when they are used up. Every other animation ends
 * 			at once. Not ported: the pump's sound (FUN_1f38_7613 with 1
 * 			at the start, 0 at the end; Phase 5).
 *
 ******************************************************************************/
static int16_t anim__check(
	const anim__Slot * pSlot,
	int16_t			   argument
) {
	if (pSlot->animation != ANIM_GAS_METER) {
		return pSlot->animation;
	}

	if (argument != 0) {
		anim__meter = argument;

		return 0;
	}

	int16_t left = anim__meter;

	anim__meter--;

	if (left == 0) {
		return 1;
	}

	globals_lineColor = (anim__meter & 1) == 0 ? 0 : ANIM_METER_LIT;
	screen_horizontalLine(
		globals_workSurface,
		ANIM_METER_RIGHT,
		ANIM_METER_ROW,
		ANIM_METER_WIDTH
	);

	if ((anim__meter & 3) == 0) {
		globals_lineColor = (anim__meter & 4) == 0 ? 0 : ANIM_METER_LIT;
		screen_horizontalLine(
			globals_workSurface,
			ANIM_METER_LEFT,
			ANIM_METER_ROW,
			ANIM_METER_WIDTH
		);
	}

	return 0;
}

/** ****************************************************************************
 *
 * \brief	Run an animation on when its event came.
 *
 * \param	animation		Its number.
 *
 * \details	Port of FUN_1f38_1e41. The original runs slot -1 when the
 * 			animation was stopped without its event being cancelled
 * 			(anim__cancel only looks at the first event); the port
 * 			ignores the event then.
 *
 ******************************************************************************/
static void anim__step(
	int16_t animation
) {
	int16_t slot = anim__slotOf(animation);

	if (slot < 0) {
		plat_log("anim: event of animation %d, which does not run", animation);

		return;
	}

	anim__slots[slot].pending--;
	anim__run(slot);
}

/** ****************************************************************************
 *
 * \brief	Queue the next steps of an animation.
 *
 * \param	animation		Its number.
 * \param	delay			Ticks from now.
 *
 * \details	Port of FUN_1000_198a, with the priority at DS:0x8ef6. The
 * 			original writes over the queue's head when no event is free;
 * 			the port drops the event then.
 *
 ******************************************************************************/
static void anim__schedule(
	int16_t animation,
	int16_t delay
) {
	int16_t event = anim__insert(delay, exe_readS16(ANIM_PRIORITY));

	if (event < 0) {
		plat_log("anim: no free event for animation %d", animation);

		return;
	}

	anim__events[event].kind = ANIM_EVENT_STEP;
	anim__events[event].param = animation;
}

/** ****************************************************************************
 *
 * \brief	Put a free event into the queue.
 *
 * \param	delay			Ticks from now.
 * \param	priority		Of events due at the same tick, the one with
 * 							the higher priority comes first.
 *
 * \return	The event, -1 when none is free.
 *
 * \details	Port of FUN_1000_1843. The queue is searched from its end.
 * 			With delay 0 the event goes to the end of the queue, after
 * 			events that are due later.
 *
 ******************************************************************************/
static int16_t anim__insert(
	int16_t delay,
	int16_t priority
) {
	int16_t event = anim__free;

	if (event < 0) {
		return -1;
	}

	int32_t time = (int32_t)globals_ticks + delay;
	int16_t earlier = anim__last;
	int16_t later = -1;

	if (delay != 0) {
		while (earlier >= 0) {
			const anim__Event * pEarlier = &anim__events[earlier];

			if (pEarlier->time < time ||
				(pEarlier->time == time && priority <= pEarlier->priority)) {
				break;
			}

			later = earlier;
			earlier = pEarlier->earlier;
		}
	}

	anim__Event * pEvent = &anim__events[event];

	pEvent->time = time;
	pEvent->priority = priority;
	anim__free = pEvent->earlier;
	pEvent->earlier = earlier;

	if (earlier < 0) {
		anim__first = event;
		globals_timedEventTime = (uint32_t)time;
	} else {
		anim__events[earlier].later = event;
	}

	pEvent->later = later;

	if (later < 0) {
		anim__last = event;
	} else {
		anim__events[later].earlier = event;
	}

	return event;
}

/** ****************************************************************************
 *
 * \brief	Take an event out of the queue and free it.
 *
 * \param	event			The event.
 *
 * \details	Port of FUN_1000_0f0b.
 *
 ******************************************************************************/
static void anim__remove(
	int16_t event
) {
	anim__Event * pEvent = &anim__events[event];

	if (pEvent->earlier < 0) {
		anim__first = pEvent->later;

		if (anim__first < 0) {
			globals_timedEventTime = GLOBALS_NO_EVENT;
		} else {
			globals_timedEventTime =
				(uint32_t)anim__events[anim__first].time;
		}
	} else {
		anim__events[pEvent->earlier].later = pEvent->later;
	}

	if (pEvent->later < 0) {
		anim__last = pEvent->earlier;
	} else {
		anim__events[pEvent->later].earlier = pEvent->earlier;
	}

	pEvent->earlier = anim__free;
	anim__free = event;
}

/** ****************************************************************************
 *
 * \brief	Cancel the queued steps of an animation.
 *
 * \param	animation		Its number.
 *
 * \details	Port of FUN_1000_19bc. Like the original, it starts at the
 * 			first event and follows the link to the event before it,
 * 			which there is none of: only the first event is looked at.
 *
 ******************************************************************************/
static void anim__cancel(
	int16_t animation
) {
	for (int16_t event = anim__first; event >= 0;
		 event = anim__events[event].earlier) {
		anim__Event * pEvent = &anim__events[event];

		if (pEvent->kind == ANIM_EVENT_STEP && pEvent->param == animation) {
			pEvent->kind = ANIM_EVENT_NONE;
		}
	}
}
