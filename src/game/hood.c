/** ****************************************************************************
 *
 * \file	hood.c
 *
 * \brief	Under the hood: the engine, its manifold and carburetors.
 *
 * \details	Port of FUN_1000_81ab and its helpers (FUN_1f38_34fb to
 * 			FUN_1f38_40ca, FUN_1000_7fb1). The engine bay (resource
 * 			0x42d) is built off-screen with the parts of the car (the
 * 			original keeps it at DS:0x8232; the port uses the platform
 * 			page SCREEN_BAY_PAGE, where the bay sits where it is shown,
 * 			at (0x60, 9)). Parts go in from the bottom up: engine, intake
 * 			manifold, then up to 3 carburetors, each held by bolts (9 in
 * 			all: 1 on the engine, 2 on the manifold, 2 per carburetor)
 * 			that are put in and tightened with clicks, and loosened and
 * 			taken out the same way. The engine has one more part, shown
 * 			by resource 0x446 or 0x447 (car byte +0x1f -1 or 1; what it
 * 			is, is unknown); it must show 0x446 before the engine comes
 * 			out. The original shifts sprites to their exact column
 * 			(FUN_1f38_acbf) because its surfaces have byte columns; the
 * 			port draws at any column. Not ported: the temporary memory
 * 			bookkeeping (FUN_1f38_9fce, a07e, a09e, a0b8).
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/hood.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dos/dos.h"
#include "game/anim.h"
#include "game/cars.h"
#include "game/cursor.h"
#include "game/effects.h"
#include "game/event.h"
#include "game/exe.h"
#include "game/game.h"
#include "game/garage.h"
#include "game/globals.h"
#include "game/lib.h"
#include "game/music.h"
#include "game/random.h"
#include "game/screen.h"
#include "game/state.h"
#include "game/ui.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define HOOD_SLOTS			  5		 /**< Engine, manifold, 3 carburetors. */
#define HOOD_BOLTS			  9		 /**< Bolts of all slots. */
#define HOOD_SCREEN			  0x15	 /**< Screen of the engine bay. */
#define HOOD_TUNE_SCREEN	  0x1c	 /**< Screen of the tuning gauge. */
#define HOOD_WAIT			  3000	 /**< Ticks event_wait waits at most. */
#define HOOD_TUNE			  (-3)	 /**< Screen 0x15 result: tune. */
#define HOOD_CHANGE			  (-2)	 /**< Screen 0x15 result: put in a part. */
#define HOOD_DONE			  (-1)	 /**< Screen 0x15 result: leave. */
#define HOOD_FASTENER_ID	  0xd	 /**< Hotspot of the 0x446/0x447 part. */
#define HOOD_TUNE_UP		  (-10)	 /**< Screen 0x1c result: one up. */
#define HOOD_TUNE_DOWN		  (-7)	 /**< Screen 0x1c result: one down. */
#define HOOD_TUNE_DONE		  (-1)	 /**< Screen 0x1c result: leave. */
#define HOOD_TUNE_MAX		  4		 /**< Highest setting of byte +0x20. */
#define HOOD_TUNE_MIN		  (-8)	 /**< Lowest setting of byte +0x20. */
#define HOOD_TUNE_TIME_MAX	  0x444	 /**< Clock ticks tuning costs at most. */
#define HOOD_TUNE_TIME_FACTOR 3		 /**< Clock ticks per BIOS tick tuned. */
#define HOOD_MUSIC_TUNING	  2		 /**< globals_musicAvailable meanwhile. */
#define HOOD_TUNE_TONE		  30000	 /**< Speaker divisor at setting 0. */
#define HOOD_TUNE_TONE_STEP	  0x400	 /**< Added to it per step of setting. */
#define HOOD_RANDOM_RANGE	  13	 /**< Byte +0x20 becomes 0..12 - 8. */
#define HOOD_RANDOM_OFFSET	  8		 /**< Subtracted from the random value. */
#define HOOD_BAY			  0x42d	 /**< Resource of the empty engine bay. */
#define HOOD_GAUGE			  0x42f	 /**< Resource of the tuning gauge. */
#define HOOD_NEEDLES		  0x433	 /**< Resource with the gauge's needle. */
#define HOOD_BAR			  0x4c6	 /**< Resource of the bar under the bay. */
#define HOOD_ENGINE_SMALL	  0x434	 /**< Resource of engine subtype 0. */
#define HOOD_ENGINE_MEDIUM	  0x435	 /**< Resource of engine subtype 1. */
#define HOOD_ENGINE_LARGE	  0x436	 /**< Resource of engine subtype 2. */
#define HOOD_MANIFOLD_A		  0x437	 /**< Manifold drawn behind a bolt. */
#define HOOD_MANIFOLD_B		  0x438	 /**< Manifold, first bolt spots. */
#define HOOD_MANIFOLD_SMALL	  0x438	 /**< Manifold 0 on engine 0x434. */
#define HOOD_MANIFOLD_OTHER	  0x439	 /**< Manifold 0 on other engines. */
#define HOOD_CARB_A			  0x43c	 /**< Carburetor, bolt spots from 0. */
#define HOOD_CARB_B			  0x43d	 /**< Carburetor, bolt spots from 2. */
#define HOOD_CARB_C			  0x43e	 /**< Carburetor, bolt spots from 4. */
#define HOOD_BOLT_LOOSE		  0x43f	 /**< Bolt put in (state 1). */
#define HOOD_BOLT_TURNED	  0x440	 /**< Bolt half tight (state 2). */
#define HOOD_UNDONE			  0x446	 /**< The engine's part, undone (-1). */
#define HOOD_DONE_UP		  0x447	 /**< The engine's part, done up (1). */
#define HOOD_BAY_LEFT		  0x60	 /**< Column of the bay on the screen. */
#define HOOD_BAY_TOP		  9		 /**< Row of the bay on the screen. */
#define HOOD_BAR_TOP		  0x94	 /**< Row of the bar under the bay. */
#define HOOD_BAY_ROWS		  0xaf	 /**< Rows shown with the bar. */
#define HOOD_STRIP_TOP		  0xd	 /**< First row parts slide in. */
#define HOOD_STRIP_BOTTOM	  0x93	 /**< Last row parts slide in. */
#define HOOD_RAISE			  0x14	 /**< Rows a misfit part stays up. */
#define HOOD_BOLT_SHIFT		  2		 /**< Bolts start 2 columns left. */
#define HOOD_BOLT_BACK_WIDTH  6		 /**< Columns put back under a bolt. */
#define HOOD_FASTENER_WIDTH	  0x20	 /**< Columns put back under 0x446/7. */
#define HOOD_GROUP_CARBS	  4		 /**< First bolt group of carburetors. */
#define HOOD_FIRST_CARB_SLOT  2		 /**< Slot of the first carburetor. */
#define HOOD_STATE_TIGHT	  3		 /**< Bolt state when tight. */
#define HOOD_BOLT_MISSING	  0		 /**< Bolt mode: not in. */
#define HOOD_BOLT_TIGHTEN	  1		 /**< Bolt mode: being tightened. */
#define HOOD_BOLT_LOOSEN	  2		 /**< Bolt mode: being loosened. */
#define HOOD_ELEMENT_SIZE	  18	 /**< Bytes of an element record. */
#define HOOD_ELEMENT_TEXT	  4		 /**< Element field: text offset. */
#define HOOD_ELEMENT_ID		  6		 /**< Element field: number. */
#define HOOD_ELEMENT_LEFT	  8		 /**< Element field: left edge. */
#define HOOD_ELEMENT_TOP	  10	 /**< Element field: top edge. */
#define HOOD_ELEMENT_RIGHT	  12	 /**< Element field: right edge. */
#define HOOD_ELEMENT_BOTTOM	  14	 /**< Element field: bottom edge. */
#define HOOD_ELEMENT_END	  9		 /**< Element type: end of the list. */
#define HOOD_FIXED_ELEMENTS	  3		 /**< Tune, change and done. */
#define HOOD_TIME_ENGINE	  0x666	 /**< Clock: engine in or out. */
#define HOOD_TIME_ENGINE_BAD  0x888	 /**< Clock: engine that won't fit. */
#define HOOD_TIME_MANIFOLD	  0x333	 /**< Clock: manifold in or out. */
#define HOOD_TIME_MANI_BAD	  0x444	 /**< Clock: manifold that won't fit. */
#define HOOD_TIME_CARB		  0x199	 /**< Clock: carburetor in or out. */
#define HOOD_TIME_CARB_BAD	  0x222	 /**< Clock: carburetor that won't. */
#define HOOD_TIME_BOLT		  0x36	 /**< Clock: a click on a bolt. */
#define HOOD_TIME_FASTENER	  0x6d	 /**< Clock: 0x446/0x447 changed. */
#define HOOD_TEXT_NO_CAR	  0x0efa /**< Message: no car, dummy. */
#define HOOD_TEXT_MESS		  0x0ea1 /**< Message: can't tune that mess. */
#define HOOD_TEXT_NO_ENGINE	  0x0f97 /**< Message: no spare engine. */
#define HOOD_TEXT_NO_MANI	  0x0fd7 /**< Message: no spare manifold. */
#define HOOD_TEXT_NO_CARB	  0x0fb5 /**< Message: no spare carburetor. */
#define HOOD_TEXT_NO_ROOM	  0x1875 /**< Message: where will you put it. */
#define HOOD_TEXT_NO_FIT	  0x10cd /**< Message: that won't fit. */
#define HOOD_KIND_ENGINE	  0		 /**< Part kind of engines. */
#define HOOD_KIND_CARB		  2		 /**< Part kind of carburetors. */
#define HOOD_KIND_MANIFOLD	  3		 /**< Part kind of manifolds. */
#define HOOD_PARTS_SCREEN	  4		 /**< Spare parts shelf. */
#define HOOD_PARTS_DONE		  (-4)	 /**< Screen 4 result: leave. */
#define HOOD_PARTS_USE		  (-3)	 /**< Screen 4 result: the selection. */
#define HOOD_PARTS_LIST		  0x49e0 /**< DS: list count and items. */
#define HOOD_PART_FITS		  3		 /**< Parts table: makes it fits. */

/* Port 61h: the bits that switch the speaker on. */
#define HOOD_SPEAKER_BITS (DOS_SPEAKER_GATE | DOS_SPEAKER_DATA)

/*
 * Tables in the data segment.
 */
#define HOOD_READY			 0x6fc	/**< DS: u8, 0: parts are not shown. */
#define HOOD_BLOCK_SPOTS	 0x702	/**< DS: (x, y) per engine type. */
#define HOOD_ENGINE_BOLTS	 0x70e	/**< DS: (x, y) per engine type. */
#define HOOD_FASTENER_SPOTS	 0x71a	/**< DS: (x, y), 2 per engine type. */
#define HOOD_MANIFOLD_BOLTS	 0x732	/**< DS: (x, y), 4 bolt spots. */
#define HOOD_GROUP_SPOTS	 0x742	/**< DS: (x, y) per group from 4. */
#define HOOD_CARB_BOLTS		 0x76a	/**< DS: (dx, dy), 2 per carburetor. */
#define HOOD_GROUP_SLOT		 0x77e	/**< DS: s8 slot of each group. */
#define HOOD_FIRST_GROUPS	 0x78c	/**< DS: s8 first carb group. */
#define HOOD_NEW_ELEMENT	 0x796	/**< DS: element record to insert. */
#define HOOD_ELEMENTS		 0x184c /**< DS: element records of 0x15. */
#define HOOD_TEMPLATE		 0x1948 /**< DS: its 3 fixed elements. */
#define HOOD_BOLT_TEXT		 0x2ba5 /**< DS: u16 text of bolt hotspots. */
#define HOOD_FASTENER_TEXT	 0x2ba7 /**< DS: u16 text of the 0x446 one. */
#define HOOD_MANIFOLD_FITS	 0x4978 /**< DS: s8 [engine * 5 + mani]. */
#define HOOD_CARB_FITS		 0x4988 /**< DS: s8 [manifold * 3 + carb]. */
#define HOOD_PICTURES		 0x4998 /**< DS: resource [slot * 5 + sub]. */
#define HOOD_MANIFOLD_0		 0x49a2 /**< DS: picture of manifold 0. */
#define HOOD_BOLT_PICTURES	 0x49ca /**< DS: resource per bolt state. */
#define HOOD_MANIFOLD_LIMIT	 0x5126 /**< DS: slots - 2 per manifold. */
#define HOOD_SLOT_BOLTS		 0x5130 /**< DS: bolts per slot. */
#define HOOD_SLOT_KINDS		 0x513c /**< DS: part kind per slot. */
#define HOOD_GROUP_ROWS		 0x5148 /**< DS: 6 carb groups/manifold. */
#define HOOD_GROUP_ROW_SIZE	 12		/**< Bytes of a row of groups. */
#define HOOD_GROUP_ROW_WORDS 6		/**< Words of a row of groups. */
#define HOOD_BOLT_GROUPS	 0x5186 /**< DS: group of each bolt. */
#define HOOD_CARB_GROUPS	 0x518c /**< DS: groups of bolts 4..9. */
#define HOOD_BOLT_SPOTS		 0x519a /**< DS: spot in its group. */

/*
 * Bytes of the car record.
 */
#define HOOD_CAR_BOLT  0x15 /**< + bolt (1..9): its state, 0..3. */
#define HOOD_CAR_PART  0x1f /**< s8: the 0x446/0x447 part, 0, -1, 1. */
#define HOOD_CAR_TUNED 0x20 /**< s8: setting of the tuning gauge. */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/**
 * State of FUN_1000_81ab kept between the player's actions.
 */
typedef struct hood__work_tag {
	int16_t modes[HOOD_BOLTS + 1]; /**< Bolt modes, from index 1. */
	int16_t fitted;				   /**< Slots with a part (local_122). */
	int16_t maxFitted;			   /**< Slots the manifold allows. */

	/**
	 * Its local_12e: the manifold's subtype, but bolt clicks leave a sum
	 * in it, which a manifold or carburetor that won't fit then uses.
	 */
	int16_t subtype;
} hood__Work;

/*******************************************************************************
;
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

static int8_t hood__subtype(
	uint16_t part
);

static uint8_t hood__fitsModel(
	uint16_t carAt,
	uint16_t part
);

static void hood__size(
	int16_t	  gameId,
	int16_t * pWidth,
	int16_t * pHeight
);

static void hood__fromBay(
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height
);

static int16_t hood__engineType(void);

static int16_t hood__carbSpot(
	int16_t * pLeft,
	int16_t * pTop
);

static void hood__boltSpot(
	int16_t * pLeft,
	int16_t * pTop
);

static void hood__partSpot(
	int16_t * pLeft,
	int16_t * pTop
);

static void hood__drawBolt(
	int16_t picture
);

static void hood__dropBolt(void);

static void hood__setOverlay(void);

static void hood__drawPart(
	int16_t picture,
	bool	raised
);

static void hood__erasePart(
	int16_t picture
);

static void hood__drawFastener(void);

static void hood__buildBay(
	uint16_t carAt
);

static void hood__showBay(
	bool withBar
);

static void hood__insertElement(
	int16_t count
);

static void hood__placeHotspots(void);

static void hood__speakerOn(void);

static void hood__tune(
	uint16_t carAt
);

static void hood__takeOff(
	uint16_t	 carAt,
	hood__Work * pWork,
	int16_t		 bolt
);

static void hood__turnBolt(
	uint16_t	 carAt,
	hood__Work * pWork,
	int16_t		 bolt
);

static void hood__toggleFastener(
	uint16_t	 carAt,
	hood__Work * pWork
);

static void hood__changePart(
	uint16_t	 carAt,
	hood__Work * pWork
);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

static int16_t hood__pictures[HOOD_SLOTS];	   /**< DS:0x8290: part per slot. */
static int16_t hood__boltPictures[HOOD_BOLTS]; /**< DS:0x829a: per bolt. */

static int16_t hood__fastener; /**< DS:0x82ac: 0x446, 0x447 or 0. */
static int16_t hood__group;	   /**< DS:0x82ae: group of part or bolt. */
static int16_t hood__spot;	   /**< DS:0x82b2: bolt spot in group. */

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	The subtype of a part (byte +2 of its parts table entry).
 *
 * \param	part			DS offset of the part record.
 *
 * \return	The subtype.
 *
 ******************************************************************************/
static int8_t hood__subtype(
	uint16_t part
) {
	int16_t	 index = exe_readS16((uint16_t)(part + STATE_PART_VALUE));
	uint16_t entry = (uint16_t)(STATE_PARTS + index * 8);

	return (int8_t)*exe_data((uint16_t)(entry + STATE_PARTS_SUBTYPE));
}

/** ****************************************************************************
 *
 * \brief	Check whether a part fits the car's model.
 *
 * \param	carAt			DS offset of the car record.
 * \param	part			DS offset of the part record.
 *
 * \return	The makes both have (low 4 bits): 0 when it does not fit.
 *
 ******************************************************************************/
static uint8_t hood__fitsModel(
	uint16_t carAt,
	uint16_t part
) {
	int16_t	 index = exe_readS16((uint16_t)(part + STATE_PART_VALUE));
	uint16_t entry = (uint16_t)(STATE_PARTS + index * 8);
	int16_t	 model = exe_readS16((uint16_t)(carAt + STATE_CAR_MODEL));
	uint16_t modelAt = (uint16_t)(STATE_MODELS + model * STATE_MODEL_SIZE);
	uint8_t	 makes = *exe_data((uint16_t)(modelAt + STATE_MODEL_FITTED));
	uint8_t	 fits = *exe_data((uint16_t)(entry + HOOD_PART_FITS));

	return (uint8_t)(makes & fits & 0xf);
}

/** ****************************************************************************
 *
 * \brief	The size of a resource.
 *
 * \param	gameId			The game's resource number.
 * \param	pWidth			Output: width in pixels.
 * \param	pHeight			Output: height in pixels.
 *
 * \details	Words 0 and 1 of the original's decoded resource
 * 			(FUN_1f38_683e).
 *
 ******************************************************************************/
static void hood__size(
	int16_t	  gameId,
	int16_t * pWidth,
	int16_t * pHeight
) {
	const lib_Resource * pResource = lib_get(lib_indexFromGameId(gameId));

	*pWidth = pResource ? (int16_t)pResource->width : 0;
	*pHeight = pResource ? (int16_t)pResource->height : 0;
}

/** ****************************************************************************
 *
 * \brief	Copy a rectangle of the engine bay to the work surface.
 *
 * \param	left			Left edge in the bay.
 * \param	top				Top edge in the bay.
 * \param	width			Width.
 * \param	height			Height.
 *
 * \details	Stands in for the plain blits of video table entries 0 and 1
 * 			(DS:0x78a2, DS:0x78a6) from the bay at DS:0x8232: the
 * 			rectangle lands where the bay is shown, at (0x60, 9).
 *
 ******************************************************************************/
static void hood__fromBay(
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height
) {
	if (width <= 0 || height <= 0) {
		return;
	}

	plat_copyRect(
		SCREEN_BAY_PAGE,
		globals_workSurface,
		(int16_t)(left + HOOD_BAY_LEFT),
		(int16_t)(top + HOOD_BAY_TOP),
		width,
		height
	);
}

/** ****************************************************************************
 *
 * \brief	The type of the engine in the bay.
 *
 * \return	0, 1 or 2 for engines 0x434..0x436; otherwise the slot's
 * 			resource as it is (0 without an engine).
 *
 * \details	Port of FUN_1f38_34fb.
 *
 ******************************************************************************/
static int16_t hood__engineType(void) {
	int16_t picture = hood__pictures[0];

	if (picture == HOOD_ENGINE_SMALL) {
		return 0;
	}

	if (picture == HOOD_ENGINE_MEDIUM) {
		return 1;
	}

	if (picture == HOOD_ENGINE_LARGE) {
		return 2;
	}

	return picture;
}

/** ****************************************************************************
 *
 * \brief	Where the part of a carburetor group goes.
 *
 * \param	pLeft			Output: left edge in the bay.
 * \param	pTop			Output: bottom edge in the bay.
 *
 * \return	First of its bolt spots in DS:0x76a: 0, 2 or 4 by the
 * 			carburetor in the group's slot.
 *
 * \details	Port of FUN_1f38_351c for the group hood__group (4 or
 * 			more). The original leaves the spot unset for another
 * 			picture; the port returns 0 then.
 *
 ******************************************************************************/
static int16_t hood__carbSpot(
	int16_t * pLeft,
	int16_t * pTop
) {
	int8_t slot =
		(int8_t)*exe_data((uint16_t)(HOOD_GROUP_SLOT + hood__group));
	int16_t	 picture = hood__pictures[slot];
	int16_t	 first = 0;
	uint16_t spotAt =
		(uint16_t)(HOOD_GROUP_SPOTS + (hood__group - HOOD_GROUP_CARBS) * 4);

	if (picture == HOOD_CARB_B) {
		first = 2;
	} else if (picture == HOOD_CARB_C) {
		first = 4;
	}

	*pLeft = exe_readS16(spotAt);
	*pTop = exe_readS16((uint16_t)(spotAt + 2));

	return first;
}

/** ****************************************************************************
 *
 * \brief	Where a bolt goes.
 *
 * \param	pLeft			Output: column in the bay.
 * \param	pTop			Output: bottom row in the bay.
 *
 * \details	Port of FUN_1f38_35e4 for the bolt hood__spot of the group
 * 			hood__group: group 0 is the engine's bolt (by engine type),
 * 			group 1 the manifold's (by manifold), the others a
 * 			carburetor's.
 *
 ******************************************************************************/
static void hood__boltSpot(
	int16_t * pLeft,
	int16_t * pTop
) {
	uint16_t spotAt;

	if (hood__group == 0) {
		spotAt = (uint16_t)(HOOD_ENGINE_BOLTS + hood__engineType() * 4);
	} else if (hood__group == 1) {
		int16_t index = hood__pictures[1] == HOOD_MANIFOLD_B ||
				hood__pictures[1] == HOOD_MANIFOLD_A
			? 0
			: 2;

		if (hood__spot == 1) {
			index++;
		}

		spotAt = (uint16_t)(HOOD_MANIFOLD_BOLTS + index * 4);
	} else {
		int16_t first = hood__carbSpot(pLeft, pTop);
		int16_t offsetAt =
			(int16_t)(HOOD_CARB_BOLTS + (first + hood__spot) * 4);

		*pLeft = (int16_t)(*pLeft + exe_readS16((uint16_t)offsetAt));
		*pTop = (int16_t)(*pTop + exe_readS16((uint16_t)(offsetAt + 2)));

		return;
	}

	*pLeft = exe_readS16(spotAt);
	*pTop = exe_readS16((uint16_t)(spotAt + 2));
}

/** ****************************************************************************
 *
 * \brief	Where the part of the group hood__group goes.
 *
 * \param	pLeft			Output: left edge in the bay.
 * \param	pTop			Output: top edge of an engine, otherwise the
 * 							bottom edge.
 *
 * \details	Port of FUN_1f38_3589.
 *
 ******************************************************************************/
static void hood__partSpot(
	int16_t * pLeft,
	int16_t * pTop
) {
	if (hood__group == 0) {
		uint16_t spotAt =
			(uint16_t)(HOOD_BLOCK_SPOTS + hood__engineType() * 4);

		*pLeft = exe_readS16(spotAt);
		*pTop = exe_readS16((uint16_t)(spotAt + 2));
	} else if (hood__group == 1) {
		*pLeft = 0x38;
		*pTop = 0x4c;
	} else {
		hood__carbSpot(pLeft, pTop);
	}
}

/** ****************************************************************************
 *
 * \brief	Draw a bolt in a state.
 *
 * \param	picture			Resource of the state (DS:0x49ca).
 *
 * \details	Port of FUN_1f38_3697 for the bolt hood__spot of the group
 * 			hood__group. The bay is put back under it (6 columns) and the
 * 			bolt is drawn 2 columns left of its spot; a bolt put in
 * 			(0x43f) drops in from above the strip when drawing through.
 *
 ******************************************************************************/
static void hood__drawBolt(
	int16_t picture
) {
	int16_t left;
	int16_t bottom;
	int16_t width;
	int16_t height;

	hood__boltSpot(&left, &bottom);
	cursor_set(CURSOR_HIDE_NOW);
	hood__size(picture, &width, &height);

	int16_t boltLeft = (int16_t)(left - HOOD_BOLT_SHIFT);
	int16_t boltTop = (int16_t)(bottom - height);

	hood__fromBay(boltLeft, boltTop, HOOD_BOLT_BACK_WIDTH, height);

	int16_t toRow = (int16_t)(boltTop + HOOD_BAY_TOP);
	int16_t fromRow = toRow;

	if (picture == HOOD_BOLT_LOOSE && globals_drawThrough != 0) {
		fromRow = (int16_t)(HOOD_STRIP_TOP - height);
	}

	screen_slideAt(
		picture,
		0,
		width,
		height,
		(int16_t)(boltLeft + HOOD_BAY_LEFT),
		fromRow,
		toRow,
		HOOD_STRIP_TOP,
		HOOD_STRIP_BOTTOM
	);
	cursor_set(CURSOR_SHOW_NOW);
}

/** ****************************************************************************
 *
 * \brief	Take a bolt out: the bay is put back and the bolt falls.
 *
 * \details	Port of FUN_1f38_37fc for the bolt hood__spot of the group
 * 			hood__group. When drawing through, the area is shown and
 * 			the bolt (0x43f, at a byte column near its spot) falls to
 * 			the bottom of the strip.
 *
 ******************************************************************************/
static void hood__dropBolt(void) {
	int16_t left;
	int16_t bottom;
	int16_t width;
	int16_t height;

	hood__boltSpot(&left, &bottom);
	cursor_set(CURSOR_HIDE_NOW);
	hood__size(HOOD_BOLT_LOOSE, &width, &height);
	hood__fromBay(
		(int16_t)(left - HOOD_BOLT_SHIFT),
		(int16_t)(bottom - height),
		width,
		height
	);

	int16_t shown = 8;
	int16_t column = left;

	if ((column & 7) < 2) {
		column = (int16_t)(column - 8);
		shown = 16;
	} else if ((column & 7) >= 5) {
		shown = 16;
	}

	column = (int16_t)(column & ~7);

	if (globals_drawThrough != 0) {
		column = (int16_t)(column + HOOD_BAY_LEFT);

		int16_t row = (int16_t)(bottom + HOOD_BAY_TOP - height);

		screen_copyToDisplay(
			column,
			row,
			(int16_t)(column + shown),
			(int16_t)(row + height)
		);
		effects_bolt();

		if (shown == 16) {
			column = (int16_t)(column + 8);
		}

		screen_slide(
			HOOD_BOLT_LOOSE,
			0,
			width,
			height,
			(int16_t)(column - 8),
			(int16_t)(row + 3),
			HOOD_STRIP_BOTTOM,
			HOOD_STRIP_TOP,
			HOOD_STRIP_BOTTOM
		);
	}

	cursor_set(CURSOR_SHOW_NOW);
}

/** ****************************************************************************
 *
 * \brief	Keep a manifold bolt in front of a carburetor sliding in.
 *
 * \details	Port of FUN_1f38_3951, called for carburetor 0x43c: on
 * 			manifold 0x437, the bolt beside the group's slot (bolt 2 or
 * 			3), when it is in (0x43f or 0x440), is drawn over each step
 * 			of the slide.
 *
 ******************************************************************************/
static void hood__setOverlay(void) {
	int8_t slot =
		(int8_t)*exe_data((uint16_t)(HOOD_GROUP_SLOT + hood__group));

	if (hood__pictures[1] != HOOD_MANIFOLD_A || slot == 3) {
		return;
	}

	int16_t index = slot != 2 ? 1 : 0;
	int16_t picture = hood__boltPictures[1 + index];

	if (picture != HOOD_BOLT_LOOSE && picture != HOOD_BOLT_TURNED) {
		return;
	}

	uint16_t spotAt = (uint16_t)(HOOD_MANIFOLD_BOLTS + index * 4);
	int16_t	 left = (int16_t)(exe_readS16(spotAt) - HOOD_BOLT_SHIFT);
	int16_t	 bottom = exe_readS16((uint16_t)(spotAt + 2));
	int16_t	 width;
	int16_t	 height;

	hood__size(picture, &width, &height);
	screen_setSlideOverlay(
		picture,
		(int16_t)(left + HOOD_BAY_LEFT),
		(int16_t)(bottom - height + HOOD_BAY_TOP)
	);
}

/** ****************************************************************************
 *
 * \brief	Draw the part of the group hood__group.
 *
 * \param	picture			Resource of the part.
 * \param	raised			true: one that won't fit; it stays 0x14 rows
 * 							higher and is not put into the bay.
 *
 * \details	Port of FUN_1f38_3a58. The part is put into the bay; while
 * 			DS:0x6fc is set it is also shown: it slides in from above
 * 			the strip when drawing through.
 *
 ******************************************************************************/
static void hood__drawPart(
	int16_t picture,
	bool	raised
) {
	int16_t left;
	int16_t top;
	int16_t width;
	int16_t height;
	bool	ready = *exe_data(HOOD_READY) != 0;

	hood__partSpot(&left, &top);

	if (ready) {
		cursor_set(CURSOR_HIDE_NOW);
	}

	hood__size(picture, &width, &height);

	if (picture == HOOD_CARB_A) {
		hood__setOverlay();
	}

	if (picture != HOOD_ENGINE_SMALL && picture != HOOD_ENGINE_MEDIUM &&
		picture != HOOD_ENGINE_LARGE) {
		top = (int16_t)(top - height);
	}

	if (raised) {
		top = (int16_t)(top - HOOD_RAISE);
	} else {
		screen_drawSprite(
			SCREEN_BAY_PAGE,
			picture,
			(int16_t)(left + HOOD_BAY_LEFT),
			(int16_t)(top + HOOD_BAY_TOP)
		);
	}

	if (ready) {
		int16_t fromRow = globals_drawThrough == 0
			? top
			: (int16_t)(HOOD_STRIP_TOP - height);

		screen_slideAt(
			picture,
			0,
			width,
			height,
			(int16_t)(left + HOOD_BAY_LEFT),
			fromRow,
			(int16_t)(top + HOOD_BAY_TOP),
			HOOD_STRIP_TOP,
			HOOD_STRIP_BOTTOM
		);
		cursor_set(CURSOR_SHOW_NOW);
	}

	screen_setSlideOverlay(0, 0, 0);
}

/** ****************************************************************************
 *
 * \brief	Take away a part that won't fit.
 *
 * \param	picture			Resource of the part.
 *
 * \details	Port of FUN_1f38_3c19 for the group hood__group: the bay is
 * 			put back where hood__drawPart showed it raised (8 columns
 * 			wider than the part).
 *
 ******************************************************************************/
static void hood__erasePart(
	int16_t picture
) {
	int16_t left;
	int16_t top;
	int16_t width;
	int16_t height;

	hood__partSpot(&left, &top);
	top = (int16_t)(top - HOOD_RAISE);
	cursor_set(CURSOR_HIDE);
	hood__size(picture, &width, &height);

	if (picture != HOOD_ENGINE_SMALL && picture != HOOD_ENGINE_MEDIUM &&
		picture != HOOD_ENGINE_LARGE) {
		top = (int16_t)(top - height);
	}

	hood__fromBay(left, top, (int16_t)(width + 8), height);
	cursor_set(CURSOR_SHOW);

	if (globals_drawThrough != 0) {
		int16_t screenLeft = (int16_t)(left + HOOD_BAY_LEFT);
		int16_t screenTop = (int16_t)(top + HOOD_BAY_TOP);

		screen_copyToDisplay(
			screenLeft,
			screenTop,
			(int16_t)(screenLeft + width + 8),
			(int16_t)(screenTop + height)
		);
	}
}

/** ****************************************************************************
 *
 * \brief	Draw the engine's 0x446/0x447 part in its state.
 *
 * \details	Port of FUN_1f38_3d1c. Each state has its own spot
 * 			(DS:0x71a, 2 per engine type): the bay is put back at the
 * 			other state's spot (0x20 columns, down to 4 rows above the
 * 			bay's bottom), then hood__fastener is drawn at its own.
 *
 ******************************************************************************/
static void hood__drawFastener(void) {
	int16_t type = (int16_t)(hood__engineType() * 2);
	int16_t erase = (int16_t)(type + 1);
	int16_t draw = type;
	int16_t bayWidth;
	int16_t bayHeight;

	if (hood__fastener == HOOD_UNDONE) {
		erase = type;
		draw = (int16_t)(type + 1);
	}

	cursor_set(CURSOR_HIDE);
	hood__size(HOOD_BAY, &bayWidth, &bayHeight);

	uint16_t eraseAt = (uint16_t)(HOOD_FASTENER_SPOTS + erase * 4);
	uint16_t drawAt = (uint16_t)(HOOD_FASTENER_SPOTS + draw * 4);
	int16_t	 eraseLeft = exe_readS16(eraseAt);
	int16_t	 eraseTop = exe_readS16((uint16_t)(eraseAt + 2));
	int16_t	 eraseRows = (int16_t)(bayHeight - eraseTop - 4);
	int16_t	 drawLeft = (int16_t)(exe_readS16(drawAt) + HOOD_BAY_LEFT);
	int16_t	 drawTop =
		(int16_t)(exe_readS16((uint16_t)(drawAt + 2)) + HOOD_BAY_TOP);

	hood__fromBay(eraseLeft, eraseTop, HOOD_FASTENER_WIDTH, eraseRows);
	screen_drawSprite(globals_workSurface, hood__fastener, drawLeft, drawTop);
	cursor_set(CURSOR_SHOW);

	if (globals_drawThrough == 0) {
		return;
	}

	int16_t left = (int16_t)(eraseLeft + HOOD_BAY_LEFT);
	int16_t top = (int16_t)(eraseTop + HOOD_BAY_TOP);

	screen_copyToDisplay(
		drawLeft < left ? drawLeft : left,
		drawTop < top ? drawTop : top,
		(int16_t)(HOOD_FASTENER_WIDTH + drawLeft + 0x18),
		(int16_t)(eraseRows + drawTop + 0x18)
	);
}

/** ****************************************************************************
 *
 * \brief	Build the engine bay with the parts of a car.
 *
 * \param	carAt			DS offset of the car record.
 *
 * \details	Port of FUN_1f38_3e35: the empty bay (0x42d), then the
 * 			engine, the manifold and the carburetors (from hood__pictures)
 * 			are drawn into it without showing them (DS:0x6fc is 0
 * 			meanwhile). The carburetor groups start at DS:0x78c by the
 * 			manifold's subtype; byte DS:0x78c itself is 4 for engine 0x434
 * 			and 5 otherwise.
 *
 ******************************************************************************/
static void hood__buildBay(
	uint16_t carAt
) {
	int16_t group = hood__group;

	*exe_data(HOOD_READY) = 0;
	plat_selectPage(SCREEN_BAY_PAGE);
	plat_clearPage();
	screen_drawSprite(SCREEN_BAY_PAGE, HOOD_BAY, HOOD_BAY_LEFT, HOOD_BAY_TOP);

	int16_t engine = hood__pictures[0];

	if (engine != 0) {
		hood__group = 0;
		hood__drawPart(engine, false);
		*exe_data(HOOD_FIRST_GROUPS) = engine == HOOD_ENGINE_SMALL ? 4 : 5;

		if (hood__pictures[1] != 0) {
			uint16_t manifold =
				exe_readU16((uint16_t)(carAt + STATE_CAR_INTAKE));
			int8_t carbGroup = (int8_t)*exe_data(
				(uint16_t)(HOOD_FIRST_GROUPS + hood__subtype(manifold))
			);

			hood__group = 1;
			hood__drawPart(hood__pictures[1], false);

			for (int slot = HOOD_FIRST_CARB_SLOT; slot < HOOD_SLOTS; slot++) {
				hood__group = carbGroup;
				carbGroup++;

				if (hood__pictures[slot] != 0) {
					hood__drawPart(hood__pictures[slot], false);
				}
			}
		}
	}

	*exe_data(HOOD_READY) = 1;
	hood__group = group;
}

/** ****************************************************************************
 *
 * \brief	Show the engine bay with its bolts.
 *
 * \param	withBar			true: also the bar under the bay (0x4c6).
 *
 * \details	Port of FUN_1f38_3f2d (with the bolt groups and spots of
 * 			DS:0x5186 and DS:0x519a): everything is drawn on the work
 * 			surface, then copied to the display while drawing through.
 *
 ******************************************************************************/
static void hood__showBay(
	bool withBar
) {
	int16_t group = hood__group;
	int16_t spot = hood__spot;
	int16_t drawThrough = globals_drawThrough;
	int16_t width;
	int16_t height;

	globals_drawThrough = 0;
	hood__size(HOOD_BAY, &width, &height);
	hood__fromBay(0, 0, width, height);

	for (int bolt = 0; bolt < HOOD_BOLTS; bolt++) {
		if (hood__boltPictures[bolt] == 0) {
			continue;
		}

		hood__group = exe_readS16((uint16_t)(HOOD_BOLT_GROUPS + bolt * 2));
		hood__spot = exe_readS16((uint16_t)(HOOD_BOLT_SPOTS + bolt * 2));
		hood__drawBolt(hood__boltPictures[bolt]);
	}

	if (hood__fastener != 0) {
		hood__drawFastener();
	}

	int16_t rows = height;

	if (withBar) {
		screen_drawResourceAt(HOOD_BAR, HOOD_BAY_LEFT, HOOD_BAR_TOP, -1);
		rows = HOOD_BAY_ROWS;
	}

	globals_drawThrough = drawThrough;

	if (drawThrough != 0) {
		screen_copyToDisplay(
			HOOD_BAY_LEFT,
			HOOD_BAY_TOP,
			(int16_t)(HOOD_BAY_LEFT + width - 1),
			(int16_t)(HOOD_BAY_TOP + rows - 1)
		);
	}

	hood__group = group;
	hood__spot = spot;
}

/** ****************************************************************************
 *
 * \brief	Insert the element at DS:0x796 into the list of screen 0x15.
 *
 * \param	count			Elements in the list.
 *
 * \details	Port of FUN_1f38_404b: the list stays sorted by left edge,
 * 			then top edge.
 *
 ******************************************************************************/
static void hood__insertElement(
	int16_t count
) {
	int16_t newLeft =
		exe_readS16((uint16_t)(HOOD_NEW_ELEMENT + HOOD_ELEMENT_LEFT));
	int16_t newTop =
		exe_readS16((uint16_t)(HOOD_NEW_ELEMENT + HOOD_ELEMENT_TOP));
	int16_t index = 0;

	for (; index < count; index++) {
		uint16_t at = (uint16_t)(HOOD_ELEMENTS + index * HOOD_ELEMENT_SIZE);
		int16_t	 left = exe_readS16((uint16_t)(at + HOOD_ELEMENT_LEFT));
		int16_t	 top = exe_readS16((uint16_t)(at + HOOD_ELEMENT_TOP));

		if (newLeft < left || (newLeft <= left && newTop < top)) {
			break;
		}
	}

	uint8_t * pAt =
		exe_data((uint16_t)(HOOD_ELEMENTS + index * HOOD_ELEMENT_SIZE));

	if (index < count) {
		memmove(
			pAt + HOOD_ELEMENT_SIZE,
			pAt,
			(size_t)(count - index) * HOOD_ELEMENT_SIZE
		);
	}

	memcpy(pAt, exe_data(HOOD_NEW_ELEMENT), HOOD_ELEMENT_SIZE);
}

/** ****************************************************************************
 *
 * \brief	Set up the hotspots of screen 0x15.
 *
 * \details	Port of FUN_1f38_40ca: the 3 fixed elements (tune, change,
 * 			done) from DS:0x1948, a hotspot over each bolt of a slot with
 * 			a part (numbers 1..9), and one over the 0x446/0x447 part
 * 			(0xd) when an engine is in.
 *
 ******************************************************************************/
static void hood__placeHotspots(void) {
	int16_t group = hood__group;
	int16_t spot = hood__spot;
	int16_t count = HOOD_FIXED_ELEMENTS;
	int16_t slotBolt = 0;
	int		slot = 0;

	memcpy(
		exe_data(HOOD_ELEMENTS),
		exe_data(HOOD_TEMPLATE),
		HOOD_FIXED_ELEMENTS * HOOD_ELEMENT_SIZE
	);
	exe_writeU16(
		(uint16_t)(HOOD_NEW_ELEMENT + HOOD_ELEMENT_TEXT),
		exe_readU16(HOOD_BOLT_TEXT)
	);

	for (int bolt = 0; bolt < HOOD_BOLTS; bolt++) {
		if (hood__pictures[slot] != 0) {
			int16_t left;
			int16_t bottom;

			hood__group = exe_readS16((uint16_t)(HOOD_BOLT_GROUPS + bolt * 2));
			hood__spot = exe_readS16((uint16_t)(HOOD_BOLT_SPOTS + bolt * 2));
			hood__boltSpot(&left, &bottom);
			exe_writeU16(
				(uint16_t)(HOOD_NEW_ELEMENT + HOOD_ELEMENT_ID),
				(uint16_t)(bolt + 1)
			);
			exe_writeU16(
				(uint16_t)(HOOD_NEW_ELEMENT + HOOD_ELEMENT_LEFT),
				(uint16_t)(left + 0x5e)
			);
			exe_writeU16(
				(uint16_t)(HOOD_NEW_ELEMENT + HOOD_ELEMENT_RIGHT),
				(uint16_t)(left + 99)
			);
			exe_writeU16(
				(uint16_t)(HOOD_NEW_ELEMENT + HOOD_ELEMENT_BOTTOM),
				(uint16_t)(bottom + 0xb)
			);
			exe_writeU16(
				(uint16_t)(HOOD_NEW_ELEMENT + HOOD_ELEMENT_TOP),
				(uint16_t)(bottom - 4)
			);
			hood__insertElement(count);
			count++;
		}

		slotBolt++;

		if (exe_readS16((uint16_t)(HOOD_SLOT_BOLTS + slot * 2)) == slotBolt) {
			slot++;
			slotBolt = 0;
		}
	}

	if (hood__pictures[0] != 0) {
		int16_t	 undone = hood__fastener == HOOD_UNDONE ? 1 : 0;
		int16_t	 index = (int16_t)(undone + hood__engineType() * 2);
		uint16_t spotAt = (uint16_t)(HOOD_FASTENER_SPOTS + index * 4);
		int16_t	 left = exe_readS16(spotAt);
		int16_t	 top = exe_readS16((uint16_t)(spotAt + 2));

		exe_writeU16(
			(uint16_t)(HOOD_NEW_ELEMENT + HOOD_ELEMENT_TEXT),
			exe_readU16(HOOD_FASTENER_TEXT)
		);
		exe_writeU16(
			(uint16_t)(HOOD_NEW_ELEMENT + HOOD_ELEMENT_ID),
			HOOD_FASTENER_ID
		);
		exe_writeU16(
			(uint16_t)(HOOD_NEW_ELEMENT + HOOD_ELEMENT_LEFT),
			(uint16_t)(left + 0x5b)
		);
		exe_writeU16(
			(uint16_t)(HOOD_NEW_ELEMENT + HOOD_ELEMENT_RIGHT),
			(uint16_t)(left + 0x65)
		);
		exe_writeU16(
			(uint16_t)(HOOD_NEW_ELEMENT + HOOD_ELEMENT_TOP),
			(uint16_t)(top + 2)
		);
		exe_writeU16(
			(uint16_t)(HOOD_NEW_ELEMENT + HOOD_ELEMENT_BOTTOM),
			(uint16_t)(top + 0x11)
		);
		hood__insertElement(count);
		count++;
	}

	exe_writeU16(
		(uint16_t)(HOOD_ELEMENTS + count * HOOD_ELEMENT_SIZE),
		HOOD_ELEMENT_END
	);
	hood__group = group;
	hood__spot = spot;
	event_reset();
}

/** ****************************************************************************
 *
 * \brief	Switch the speaker on when the sound is on and it is off.
 *
 * \details	Part of FUN_1000_7fb1 (1000:8032 and 80c6), which repeats it
 * 			for every setting and while it waits, so that the sound
 * 			comes back when Ctrl switches it on again.
 *
 ******************************************************************************/
static void hood__speakerOn(void) {
	if (globals_soundOn != 0 && globals_soundOn != globals_speakerOn) {
		dos_setSpeakerPort((uint8_t)(dos_speakerPort() | HOOD_SPEAKER_BITS));
		globals_speakerOn = 1;
	}
}

/** ****************************************************************************
 *
 * \brief	Tune the engine on the gauge.
 *
 * \param	carAt			DS offset of the car record.
 *
 * \details	Port of FUN_1000_7fb1. Screen 0x1c over the gauge (0x42f at
 * 			(0x60, 9)): the needle (8 x 0xb at (0x30, 0) of 0x433) is at
 * 			row 0x4b + 2 * the car's byte +0x20, which the buttons move
 * 			between -8 and 4. The time spent costs 3 clock ticks per
 * 			BIOS tick, at most 0x444. Animation 8 (columns of 0x433 at
 * 			(0xa0, 0x34), whose first step loads the image the original
 * 			takes the needle from) runs while the gauge is open. The
 * 			music stops meanwhile (StopPlayer, ResumePlayer) and the
 * 			engine sounds on the speaker (hood__speakerOn), with the
 * 			divisor 30000 + 0x400 * the setting without its sign.
 *
 ******************************************************************************/
static void hood__tune(
	uint16_t carAt
) {
	uint8_t * pTuned = exe_data((uint16_t)(carAt + HOOD_CAR_TUNED));
	uint16_t  start = (uint16_t)globals_biosTicks;
	int16_t	  choice;

	ui_open(HOOD_TUNE_SCREEN);
	screen_drawResourceAt(HOOD_GAUGE, HOOD_BAY_LEFT, HOOD_BAY_TOP, -1);
	anim_start(ANIM_GAUGE);
	music_stop();
	globals_musicAvailable = HOOD_MUSIC_TUNING;

	if (globals_soundOn != 0) {
		dos_setSpeakerPort((uint8_t)(dos_speakerPort() | HOOD_SPEAKER_BITS));
		globals_speakerOn = 1;
	}

	do {
		int16_t setting = (int8_t)*pTuned;
		int16_t row = (int16_t)(setting * 2 + 0x4b);

		hood__speakerOn();
		dos_setSpeakerDivisor(
			(uint16_t)(abs(setting) * HOOD_TUNE_TONE_STEP + HOOD_TUNE_TONE)
		);

		cursor_set(CURSOR_HIDE_NOW);
		screen_drawSpritePart(
			globals_workSurface,
			HOOD_NEEDLES,
			0x30,
			0,
			8,
			0xb,
			0xb0,
			row,
			true
		);
		screen_copyToDisplay(0xb0, row, 0xb7, (int16_t)(setting * 2 + 0x55));
		cursor_set(CURSOR_SHOW_NOW);

		for (;;) {
			choice = event_wait(0);

			if (choice != 0) {
				break;
			}

			hood__speakerOn();
		}

		if (choice == HOOD_TUNE_UP) {
			*pTuned = (uint8_t)(*pTuned + 1);

			if ((int8_t)*pTuned > HOOD_TUNE_MAX) {
				*pTuned = HOOD_TUNE_MAX;
			}
		} else if (choice == HOOD_TUNE_DOWN) {
			*pTuned = (uint8_t)(*pTuned - 1);

			if ((int8_t)*pTuned < HOOD_TUNE_MIN) {
				*pTuned = (uint8_t)HOOD_TUNE_MIN;
			}
		}
	} while (choice != HOOD_TUNE_DONE);

	dos_setSpeakerPort((uint8_t)(dos_speakerPort() & ~HOOD_SPEAKER_BITS));
	globals_speakerOn = 0;
	globals_musicAvailable = 1;
	music_resume();
	anim_stop(ANIM_GAUGE);

	uint16_t spent =
		(uint16_t)(((uint16_t)globals_biosTicks - start) *
				   HOOD_TUNE_TIME_FACTOR);

	if (spent > HOOD_TUNE_TIME_MAX) {
		spent = HOOD_TUNE_TIME_MAX;
	}

	globals_gameTime += spent;
	ui_close(1);
}

/** ****************************************************************************
 *
 * \brief	Take out the parts whose bolts are all out.
 *
 * \param	carAt			DS offset of the car record.
 * \param	pWork			In, out: the state of the screen.
 * \param	bolt			The bolt clicked (1..9), for a carburetor.
 *
 * \details	Part of FUN_1000_81ab (1000:849c..85b0), for the group
 * 			hood__group: the engine comes out when it is the only part,
 * 			its bolt is out and the 0x446/0x447 part shows 0x446; the
 * 			manifold when it is the top part and both its bolts are
 * 			out; a carburetor when both its bolts are out. Taking out the
 * 			first carburetor (3 parts in) and the manifold goes on with
 * 			the part below. The bay is built and shown again each time.
 *
 ******************************************************************************/
static void hood__takeOff(
	uint16_t	 carAt,
	hood__Work * pWork,
	int16_t		 bolt
) {
	int16_t again;

	do {
		int16_t slot = -1;

		again = 0;

		if (hood__group == 0) {
			int8_t part = (int8_t)*exe_data((uint16_t)(carAt + HOOD_CAR_PART));

			if (pWork->fitted == 1 && part == -1 &&
				pWork->modes[1] == HOOD_BOLT_MISSING) {
				slot = 0;
				*exe_data((uint16_t)(carAt + HOOD_CAR_PART)) = 0;
				hood__fastener = 0;
				globals_gameTime += HOOD_TIME_ENGINE;
			}
		} else if (hood__group == 1) {
			if (pWork->fitted == 2 && pWork->modes[2] == HOOD_BOLT_MISSING &&
				pWork->modes[3] == HOOD_BOLT_MISSING) {
				slot = 1;
				again = 1;
				globals_gameTime += HOOD_TIME_MANIFOLD;
			}
		} else {
			int16_t first = bolt;
			int16_t spotAt = (int16_t)(HOOD_BOLT_SPOTS + (first - 1) * 2);

			if (exe_readS16((uint16_t)spotAt) == 1) {
				first--;
			}

			if (pWork->modes[first] == HOOD_BOLT_MISSING &&
				pWork->modes[first + 1] == HOOD_BOLT_MISSING) {
				slot = (int16_t)((first - 4) / 2 + HOOD_FIRST_CARB_SLOT);

				if (pWork->fitted == 3) {
					again = 2;
				}

				globals_gameTime += HOOD_TIME_CARB;
			}
		}

		if (slot < 0) {
			continue;
		}

		pWork->fitted--;
		cars_removePart((uint16_t)(carAt + STATE_CAR_ENGINE + slot * 2));
		cursor_set(CURSOR_HIDE_NOW);
		hood__pictures[slot] = 0;

		if (again != 0) {
			hood__group = (int16_t)(again - 1);
		}

		hood__buildBay(carAt);
		hood__showBay(true);
		hood__placeHotspots();
		*exe_data((uint16_t)(carAt + HOOD_CAR_TUNED)) =
			(uint8_t)(random_next(HOOD_RANDOM_RANGE) - HOOD_RANDOM_OFFSET);
		cursor_set(CURSOR_SHOW_NOW);
	} while (again != 0);
}

/** ****************************************************************************
 *
 * \brief	Click on a bolt.
 *
 * \param	carAt			DS offset of the car record.
 * \param	pWork			In, out: the state of the screen.
 * \param	bolt			The bolt, 1..9.
 *
 * \details	Part of FUN_1000_81ab (1000:8406..849b, 85b3..85f0): only
 * 			for a slot with a part; each click costs time. A missing bolt
 * 			is put in loose (state 1); a bolt being tightened turns up to
 * 			3, and once tight the next clicks loosen it; at 1 it falls
 * 			out, and the parts whose bolts are all out come out
 * 			(hood__takeOff).
 *
 ******************************************************************************/
static void hood__turnBolt(
	uint16_t	 carAt,
	hood__Work * pWork,
	int16_t		 bolt
) {
	int16_t sum = 0;
	int16_t slot = 0;

	while (slot < HOOD_SLOTS) {
		uint16_t boltsAt = (uint16_t)(HOOD_SLOT_BOLTS + slot * 2);

		sum = (int16_t)(sum + exe_readS16(boltsAt));

		if (sum >= bolt) {
			break;
		}

		slot++;
	}

	pWork->subtype = sum;

	if (slot >= HOOD_SLOTS || hood__pictures[slot] == 0) {
		return;
	}

	globals_gameTime += HOOD_TIME_BOLT;

	uint8_t * pState = exe_data((uint16_t)(carAt + HOOD_CAR_BOLT + bolt));
	int16_t	  group =
		exe_readS16((uint16_t)(HOOD_BOLT_GROUPS + (bolt - 1) * 2));
	int16_t spot = exe_readS16((uint16_t)(HOOD_BOLT_SPOTS + (bolt - 1) * 2));

	if (pWork->modes[bolt] == HOOD_BOLT_LOOSEN) {
		hood__group = group;
		hood__spot = spot;
		*pState = (uint8_t)(*pState - 1);

		if ((int8_t)*pState < 2) {
			*pState = 0;
			pWork->modes[bolt] = HOOD_BOLT_MISSING;
			hood__boltPictures[bolt - 1] = 0;
			hood__dropBolt();
			effects_warble77e1();
		} else {
			int16_t picture = exe_readS16(
				(uint16_t)(HOOD_BOLT_PICTURES + (int8_t)*pState * 2)
			);

			hood__boltPictures[bolt - 1] = picture;
			hood__drawBolt(picture);
			effects_bolt();
		}

		hood__takeOff(carAt, pWork, bolt);

		return;
	}

	if (pWork->modes[bolt] == HOOD_BOLT_TIGHTEN) {
		if (*pState == HOOD_STATE_TIGHT) {
			pWork->modes[bolt] = HOOD_BOLT_LOOSEN;

			return;
		}

		*pState = (uint8_t)(*pState + 1);

		int16_t picture =
			exe_readS16((uint16_t)(HOOD_BOLT_PICTURES + (int8_t)*pState * 2));

		hood__boltPictures[bolt - 1] = picture;
		hood__group = group;
		hood__spot = spot;
		hood__drawBolt(picture);
		effects_bolt();

		return;
	}

	int16_t picture = exe_readS16((uint16_t)(HOOD_BOLT_PICTURES + 2));

	hood__group = group;
	hood__spot = spot;
	hood__boltPictures[bolt - 1] = picture;
	hood__drawBolt(picture);
	effects_warble77e1();
	pWork->modes[bolt] = HOOD_BOLT_TIGHTEN;
	*pState = 1;
}

/** ****************************************************************************
 *
 * \brief	Click on the engine's 0x446/0x447 part.
 *
 * \param	carAt			DS offset of the car record.
 * \param	pWork			In, out: the state of the screen.
 *
 * \details	Part of FUN_1000_81ab (1000:8606..8665): car byte +0x1f
 * 			changes sign (-1 shows 0x446, 1 shows 0x447), which costs
 * 			time; with 0x446 the engine may come out (hood__takeOff).
 *
 ******************************************************************************/
static void hood__toggleFastener(
	uint16_t	 carAt,
	hood__Work * pWork
) {
	uint8_t * pPart = exe_data((uint16_t)(carAt + HOOD_CAR_PART));
	int8_t	  value = (int8_t)-(int8_t)*pPart;

	*pPart = (uint8_t)value;

	if (value == 0) {
		return;
	}

	hood__fastener = value < 1 ? HOOD_UNDONE : HOOD_DONE_UP;
	hood__drawFastener();
	globals_gameTime += HOOD_TIME_FASTENER;

	if (value >= 1) {
		hood__placeHotspots();

		return;
	}

	hood__group = 0;
	hood__placeHotspots();
	hood__takeOff(carAt, pWork, HOOD_FASTENER_ID);
}

/** ****************************************************************************
 *
 * \brief	Let the player put in the next part from the spare parts.
 *
 * \param	carAt			DS offset of the car record.
 * \param	pWork			In, out: the state of the screen.
 *
 * \details	Part of FUN_1000_81ab (1000:8668..88fb). The spare parts of
 * 			the kind the next slot takes are listed (screen 4) until one
 * 			is chosen. The engine must fit the model; the manifold also
 * 			the engine (DS:0x4978), a carburetor also the manifold
 * 			(DS:0x4988), which says which carburetor groups exist
 * 			(DS:0x5148). A part that won't fit is shown raised with a
 * 			message and taken away again; it still costs time. A part
 * 			put in gets its bolts in loose.
 *
 ******************************************************************************/
static void hood__changePart(
	uint16_t	 carAt,
	hood__Work * pWork
) {
	uint16_t parts[STATE_PART_MAX + 1];
	int8_t	 percents[STATE_PART_MAX + 1];
	int16_t	 kind =
		exe_readS16((uint16_t)(HOOD_SLOT_KINDS + pWork->fitted * 2));

	garage_listSpares((int8_t)kind, parts, percents, 0);

	for (;;) {
		int16_t choice = ui_runList(
			HOOD_PARTS_SCREEN,
			1,
			NULL,
			HOOD_PARTS_LIST,
			percents,
			0
		);

		if (choice == HOOD_PARTS_DONE) {
			choice = 0;
		} else if (choice == HOOD_PARTS_USE) {
			choice = ui_listSelection();
		} else if (choice == UI_LIST_EMPTY) {
			if (kind == HOOD_KIND_ENGINE) {
				game_message(HOOD_TEXT_NO_ENGINE);
			}

			if (kind == HOOD_KIND_MANIFOLD) {
				game_message(HOOD_TEXT_NO_MANI);
			}

			if (kind == HOOD_KIND_CARB) {
				game_message(HOOD_TEXT_NO_CARB);
			}

			choice = 0;
		}

		if (choice == 0) {
			return;
		}

		if (pWork->fitted >= pWork->maxFitted) {
			game_message(HOOD_TEXT_NO_ROOM);

			return;
		}

		uint16_t part = parts[choice];
		int16_t	 slot = pWork->fitted;
		int16_t	 firstBolt = 0;
		bool	 fits = hood__fitsModel(carAt, part) != 0;

		if (pWork->fitted == 0) {
			hood__group = 0;
			firstBolt = 0;
			*exe_data((uint16_t)(carAt + HOOD_CAR_PART)) = 0xff;
			hood__fastener = HOOD_UNDONE;
			globals_gameTime += fits ? HOOD_TIME_ENGINE : HOOD_TIME_ENGINE_BAD;
		} else if (pWork->fitted == 1) {
			if (fits) {
				uint16_t engine =
					exe_readU16((uint16_t)(carAt + STATE_CAR_ENGINE));

				pWork->subtype = hood__subtype(part);

				int16_t engineType = hood__subtype(engine);
				int16_t fitsIndex =
					(int16_t)(pWork->subtype + engineType * 5);
				uint16_t fitsAt = (uint16_t)(HOOD_MANIFOLD_FITS + fitsIndex);

				fits = *exe_data(fitsAt) != 0;
			}

			hood__group = 1;
			firstBolt = 1;
			uint16_t limitAt =
				(uint16_t)(HOOD_MANIFOLD_LIMIT + hood__subtype(part) * 2);

			pWork->maxFitted = (int16_t)(exe_readS16(limitAt) + 2);
			memcpy(
				exe_data(HOOD_CARB_GROUPS),
				exe_data(
					(uint16_t)(HOOD_GROUP_ROWS +
							   pWork->subtype * HOOD_GROUP_ROW_SIZE)
				),
				HOOD_GROUP_ROW_WORDS * 2
			);
			globals_gameTime +=
				fits ? HOOD_TIME_MANIFOLD : HOOD_TIME_MANI_BAD;
		} else if (pWork->fitted >= 2 && pWork->fitted <= 4) {
			if (fits) {
				uint16_t manifold =
					exe_readU16((uint16_t)(carAt + STATE_CAR_INTAKE));

				pWork->subtype = hood__subtype(manifold);

				int16_t carbType = hood__subtype(part);
				int16_t fitsIndex =
					(int16_t)(carbType + pWork->subtype * 3);
				uint16_t fitsAt = (uint16_t)(HOOD_CARB_FITS + fitsIndex);

				fits = *exe_data(fitsAt) != 0;
			}

			globals_gameTime += fits ? HOOD_TIME_CARB : HOOD_TIME_CARB_BAD;
			firstBolt = 3;
			slot = 2;

			uint16_t carbs = exe_readU16((uint16_t)(carAt + STATE_CAR_CARBS));
			uint16_t carbs2 =
				exe_readU16((uint16_t)(carAt + STATE_CAR_CARBS + 2));

			uint16_t rowAt = (uint16_t)(HOOD_GROUP_ROWS +
										pWork->subtype * HOOD_GROUP_ROW_SIZE);

			hood__group = exe_readS16(rowAt);

			if (hood__group == 7) {
				if (carbs != 0) {
					if (carbs2 == 0) {
						hood__group = 8;
						firstBolt = 5;
						slot = 3;
					} else {
						hood__group = 9;
						firstBolt = 7;
						slot = 4;
					}
				}
			} else if (hood__group == 10 || hood__group == 12) {
				if (carbs != 0) {
					hood__group++;
					firstBolt = 5;
					slot = 3;
				}
			} else if (hood__group < 4 || hood__group > 6) {
				continue;
			}
		} else {
			continue;
		}

		int16_t picture = exe_readS16(
			(uint16_t)(HOOD_PICTURES + slot * 10 + hood__subtype(part) * 2)
		);

		hood__pictures[slot] = picture;

		if (!fits) {
			effects_track76da(1);
			hood__drawPart(picture, true);
			effects_track76da(0);
			effects_warble7817();
			game_message(HOOD_TEXT_NO_FIT);
			effects_track76da(1);
			hood__erasePart(picture);
			effects_track76da(0);
			hood__pictures[slot] = 0;

			if (slot == 0) {
				*exe_data((uint16_t)(carAt + HOOD_CAR_PART)) = 0;
				hood__fastener = 0;
			}

			continue;
		}

		cars_fitPart(
			cars_takeSparePart(part),
			(uint16_t)(carAt + STATE_CAR_ENGINE + slot * 2)
		);
		cursor_set(CURSOR_HIDE_NOW);
		effects_track76da(1);
		hood__drawPart(picture, false);
		effects_track76da(0);
		effects_warble7817();

		if (slot == 0) {
			bool	 small = picture == HOOD_ENGINE_SMALL;
			uint16_t groups = small ? 4 : 5;

			exe_writeU16(
				HOOD_MANIFOLD_0,
				small ? HOOD_MANIFOLD_SMALL : HOOD_MANIFOLD_OTHER
			);
			exe_writeU16((uint16_t)(HOOD_GROUP_ROWS + 2), groups);
			exe_writeU16(HOOD_GROUP_ROWS, groups);
			hood__fastener = HOOD_UNDONE;
			hood__drawFastener();
		}

		int16_t bolts =
			exe_readS16((uint16_t)(HOOD_SLOT_BOLTS + pWork->fitted * 2));

		for (int16_t index = (int16_t)(bolts - 1); index >= 0; index--) {
			int16_t bolt = (int16_t)(firstBolt + index);
			int16_t loose = exe_readS16((uint16_t)(HOOD_BOLT_PICTURES + 2));

			hood__spot = index;
			hood__boltPictures[bolt] = loose;
			hood__drawBolt(loose);
			effects_warble77e1();
			pWork->modes[bolt + 1] = HOOD_BOLT_TIGHTEN;
			*exe_data((uint16_t)(carAt + HOOD_CAR_BOLT + 1 + bolt)) = 1;
		}

		hood__placeHotspots();
		cursor_set(CURSOR_SHOW_NOW);
		pWork->fitted++;
		*exe_data((uint16_t)(carAt + HOOD_CAR_TUNED)) =
			(uint8_t)(random_next(HOOD_RANDOM_RANGE) - HOOD_RANDOM_OFFSET);

		return;
	}
}

/** ****************************************************************************
 *
 * \brief	Work under the hood of a car.
 *
 * \param	carAt			DS offset of the car record, or 0 (a message).
 *
 * \details	Port of FUN_1000_81ab. The bay is built and shown; for a car
 * 			other than the one in use (FUN_1000_a265) that is all.
 * 			Otherwise screen 0x15: "Tune" (-3) shows the gauge when the
 * 			car is complete, "Change" (-2) puts in the next part,
 * 			clicks on a bolt (1..9) or on the 0x446/0x447 part (0xd)
 * 			work on them, -1 leaves. Each bolt has a mode: missing (0),
 * 			being tightened (1), or being loosened (2, after it was
 * 			tight). The engine decides the picture of manifold 0
 * 			(DS:0x49a2) and the first carburetor groups (DS:0x5148), the
 * 			manifold the groups of the carburetors (DS:0x518c) and how
 * 			many fit (DS:0x5126).
 *
 ******************************************************************************/
void hood_open(
	uint16_t carAt
) {
	if (carAt == 0) {
		game_message(HOOD_TEXT_NO_CAR);

		return;
	}

	hood__Work work = { 0 };

	for (int16_t bolt = 1; bolt <= HOOD_BOLTS; bolt++) {
		int8_t state =
			(int8_t)*exe_data((uint16_t)(carAt + HOOD_CAR_BOLT + bolt));

		if (state == 0) {
			work.modes[bolt] = HOOD_BOLT_MISSING;
			hood__boltPictures[bolt - 1] = 0;
		} else {
			work.modes[bolt] =
				state == 1 ? HOOD_BOLT_TIGHTEN : HOOD_BOLT_LOOSEN;
			hood__boltPictures[bolt - 1] =
				exe_readS16((uint16_t)(HOOD_BOLT_PICTURES + state * 2));
		}
	}

	int8_t part = (int8_t)*exe_data((uint16_t)(carAt + HOOD_CAR_PART));

	hood__fastener = part == 0 ? 0 : part < 1 ? HOOD_UNDONE
											  : HOOD_DONE_UP;

	uint16_t engine = exe_readU16((uint16_t)(carAt + STATE_CAR_ENGINE));

	if (engine != 0) {
		exe_writeU16(
			HOOD_MANIFOLD_0,
			hood__subtype(engine) == 0 ? HOOD_MANIFOLD_SMALL
									   : HOOD_MANIFOLD_OTHER
		);
	}

	for (int slot = 0; slot < HOOD_SLOTS; slot++) {
		uint16_t slotPart =
			exe_readU16((uint16_t)(carAt + STATE_CAR_ENGINE + slot * 2));

		hood__pictures[slot] = slotPart == 0
			? 0
			: exe_readS16((uint16_t)(HOOD_PICTURES + slot * 10 +
									 hood__subtype(slotPart) * 2));

		if (hood__pictures[slot] != 0) {
			work.fitted++;
		}
	}

	uint16_t groups = hood__pictures[0] == HOOD_ENGINE_SMALL ? 4 : 5;

	exe_writeU16((uint16_t)(HOOD_GROUP_ROWS + 2), groups);
	exe_writeU16(HOOD_GROUP_ROWS, groups);

	uint16_t manifold = exe_readU16((uint16_t)(carAt + STATE_CAR_INTAKE));

	if (manifold == 0) {
		work.maxFitted = HOOD_SLOTS;
	} else {
		work.subtype = hood__subtype(manifold);
		work.maxFitted = (int16_t)(exe_readS16((uint16_t)(HOOD_MANIFOLD_LIMIT +
														  work.subtype * 2)) +
								   2);
		memcpy(
			exe_data(HOOD_CARB_GROUPS),
			exe_data(
				(uint16_t)(HOOD_GROUP_ROWS + work.subtype * HOOD_GROUP_ROW_SIZE)
			),
			HOOD_GROUP_ROW_WORDS * 2
		);
	}

	bool inUse = carAt == exe_readU16(STATE_CAR);

	cursor_set(CURSOR_HIDE_NOW);
	hood__buildBay(carAt);
	hood__showBay(inUse);
	cursor_set(CURSOR_SHOW_NOW);

	if (!inUse) {
		return;
	}

	hood__placeHotspots();
	ui_open(HOOD_SCREEN);

	for (;;) {
		int16_t choice;

		do {
			choice = event_wait(HOOD_WAIT);
		} while (choice == 0);

		if (choice == HOOD_TUNE) {
			if (cars_isComplete(carAt, 0) == 0) {
				game_message(HOOD_TEXT_MESS);
			} else {
				hood__tune(carAt);
				cursor_set(CURSOR_HIDE_NOW);
				hood__showBay(true);
				cursor_set(CURSOR_SHOW_NOW);
			}
		} else if (choice == HOOD_CHANGE) {
			hood__changePart(carAt, &work);
		} else if (choice == HOOD_DONE) {
			ui_close(1);

			return;
		} else if (choice == HOOD_FASTENER_ID) {
			hood__toggleFastener(carAt, &work);
		} else if (choice > 0 && choice <= HOOD_BOLTS) {
			hood__turnBolt(carAt, &work, choice);
		}
	}
}
