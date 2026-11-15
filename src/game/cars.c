/** ****************************************************************************
 *
 * \file	cars.c
 *
 * \brief	The player's cars and parts: the records and how they are made.
 *
 * \details	A car record (40 bytes, see state.h) points to the part
 * 			records fitted to it. Part records (8 bytes) hold the price
 * 			paid, a word of the part's kind, the index into the parts
 * 			table and a link. Parts that are not fitted hang in the list
 * 			at STATE_SPARE_PARTS.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/cars.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "dos/dos.h"
#include "dos/fpu.h"
#include "game/cursor.h"
#include "game/drive.h"
#include "game/effects.h"
#include "game/exe.h"
#include "game/globals.h"
#include "game/lib.h"
#include "game/random.h"
#include "game/screen.h"
#include "game/state.h"
#include "game/video.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define CARS_PAINT_TABLE	   0x50c2 /**< DS offset: 2 colors per paint. */
#define CARS_PAINT_FIRST	   6	  /**< Palette entry of the first color. */
#define CARS_CARB_COUNTS	   0x5126 /**< DS offset: carburetors per setup. */
#define CARS_CARB_SLOTS		   3	  /**< Carburetor slots of a car. */
#define CARS_CONDITION		   3	  /**< Start value of the wear bytes. */
#define CARS_START_VALUE	   0x28	  /**< Start value of the car's +0x22. */

#define CARS_FIRST_GEARBOX	   9	/**< Parts table: first transmission. */
#define CARS_FIRST_CARB		   0x16 /**< Parts table: first carburetor. */
#define CARS_FIRST_INTAKE	   0x19 /**< Parts table: first manifold. */
#define CARS_FIRST_TIRES	   0x28 /**< Parts table: first set of tires. */
#define CARS_BLOWER_SUBTYPE	   2	/**< Engine subtype with a blower. */

#define CARS_MAX_WIDTH		   264 /**< Widest car body sprite. */
#define CARS_MAX_HEIGHT		   72  /**< Tallest car body sprite. */
#define CARS_MAX_PIXELS		   (CARS_MAX_WIDTH * CARS_MAX_HEIGHT)
#define CARS_MAX_PART_PIXELS   (160 * 48) /**< Largest sub-sprite buffer. */
#define CARS_DOOR_WIDTH		   64		  /**< Width of saved door area. */
#define CARS_DOOR_HEIGHT	   25		  /**< Height of saved door area. */
#define CARS_FRAMES			   4		  /**< Tire rotation frames. */
#define CARS_SHADOW_RESOURCE   0x49d	  /**< Under-car shadow sprite. */
#define CARS_WELL_RESOURCE	   0x4d2	  /**< Behind a tire off the car. */
#define CARS_NO_BLOWER_MODEL   25		  /**< Model without a blower. */
#define CARS_OFFSETS_SIZE	   12		  /**< Bytes per model offset record. */
#define CARS_POS_SIZE		   6		  /**< Bytes per model position. */
#define CARS_STICKER_SIZE	   6		  /**< Bytes per sticker record. */
#define CARS_PLACE_CROP		   0x2a6	  /**< DS offset: crop per place. */
#define CARS_TOP_CROP		   0x2d0	  /**< DS offset: active bottom crop. */
#define CARS_STRIP_HEIGHT	   0x46		  /**< Saved background strip height. */
#define CARS_HAND_RESOURCE	   0x4c2	  /**< Hand wiping on sticker. */
#define CARS_BARRED_PLACE	   6		  /**< globals_place behind bars. */
#define CARS_GAPS			   0x539a	  /**< DS: gaps between the bars. */
#define CARS_GAP_COUNT		   6		  /**< Gaps (left, width). */
#define CARS_WIPE_STEP		   8		  /**< Columns per wipe step. */
#define CARS_WIPE_TICKS		   2		  /**< Ticks per wipe step. */
#define CARS_SPEED_WEIGHT	   0x556e	  /**< DS: weight per class. */
#define CARS_SPEED_CARBS	   0x552e	  /**< DS: carb and manifold factors. */
#define CARS_SPEED_ENGINE	   0x554c	  /**< DS: engine factors. */
#define CARS_SPEED_GEARBOX	   0x5552	  /**< DS: transmission factors. */
#define CARS_SPEED_STOCK_WEAR  0x496a	  /**< DS: wear of a stock engine. */

#define CARS_FOREGROUND_MASK   0x448			/**< Mask, 0 = foreground. */
#define CARS_FOREGROUND_NAME   "lib2/105_front" /**< Replacement name. */

#define CARS_STREET_PLACE	   2				/**< Place on the street. */
#define CARS_WAITRESS_MASK	   0x474			/**< Mask, 0 = foreground. */
#define CARS_WAITRESS_NAME	   "lib2/140_front" /**< Replacement name. */
#define CARS_WAITRESS_SHEETS   0x5a26			/**< DS: sprite sheets. */
#define CARS_WAITRESS_FRAMES   0x5a32			/**< DS: 84 frame numbers. */
#define CARS_WAITRESS_PHASES   0x5a86			/**< DS: coming and leaving. */
#define CARS_WAITRESS_STRIP	   0x100			/**< Width of the strip. */
#define CARS_WAITRESS_TOP	   0x44				/**< Top of the strip. */
#define CARS_WAITRESS_HEIGHT   0x29				/**< Height of the strip. */
#define CARS_WAITRESS_BOTTOM   0x6c				/**< Bottom of the strip. */
#define CARS_WAITRESS_CAR_Y	   0xa3				/**< Bottom of the car. */
#define CARS_WAITRESS_START_X  (-40)			/**< Start column, coming. */
#define CARS_WAITRESS_REST_X   200				/**< Resting column. */
#define CARS_WAITRESS_PATCH_W  0x30				/**< Saved patch width. */
#define CARS_WAITRESS_STEPS	   0x54				/**< Steps (0..0x53). */
#define CARS_WAITRESS_TICKS	   2				/**< Ticks per step. */

#define CARS_FPU_ROOF		   0x6988 /**< DS: real 0.09. */
#define CARS_FPU_FRONT		   0x6990 /**< DS: real 0.05. */
#define CARS_FPU_REAR		   0x6998 /**< DS: real 0.01. */
#define CARS_FPU_WEIGHT		   0x69a0 /**< DS: real 0.1. */
#define CARS_FPU_WEIGHT_SCALE  0x69a8 /**< DS: real 88. */
#define CARS_FPU_CARBS		   0x69b0 /**< DS: real 0.15. */
#define CARS_FPU_GEARBOX	   0x69b0 /**< DS: real 0.15. */
#define CARS_FPU_HUNDRED	   0x69b8 /**< DS: real 100. */
#define CARS_FPU_ENGINE		   0x69c0 /**< DS: real 0.25. */
#define CARS_FPU_TUNING_SCALE  0x69c8 /**< DS: real 8. */
#define CARS_FPU_ONE		   0x69d0 /**< DS: real 1. */
#define CARS_FPU_TUNING		   0x69d8 /**< DS: real 0.2. */
#define CARS_FPU_WORN_OUT	   0x69e0 /**< DS: real 10000. */
#define CARS_FPU_FIRST_GEARBOX 0x69e8 /**< DS: real 0.8. */
#define CARS_FPU_SPREAD		   0x69f0 /**< DS: real 0.3. */
#define CARS_FPU_BASE		   0x69f8 /**< DS: real 0.7. */
#define CARS_FPU_TOP_SPEED	   0x6a00 /**< DS: real 130. */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/
/**
 * One speed phase of a car scroll animation.
 */
typedef struct cars__phase_tag {
	int16_t count; /**< Steps in this phase, or -1 at the end. */
	int16_t step;  /**< Horizontal pixel step per frame. */
} cars__Phase;

/*******************************************************************************
;
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

static uint16_t cars__fit(
	uint16_t car,
	int		 slot,
	int16_t	 part,
	int		 kind,
	bool	 reuse
);

static bool cars__decodeResource(
	int16_t	  gameId,
	uint8_t * pPixels,
	int *	  pWidth,
	int *	  pHeight
);

static void cars__blitOverwrite(
	uint8_t *		pBody,
	int				bodyWidth,
	int				bodyHeight,
	const uint8_t * pPatch,
	int				patchWidth,
	int				patchHeight,
	int				left,
	int				top
);

static void cars__blitOverlay(
	uint8_t *		pBody,
	int				bodyWidth,
	int				bodyHeight,
	const uint8_t * pPatch,
	int				patchWidth,
	int				patchHeight,
	int				left,
	int				top
);

static void cars__blitBehind(
	uint8_t *		pBody,
	int				bodyWidth,
	int				bodyHeight,
	const uint8_t * pPatch,
	int				patchWidth,
	int				patchHeight,
	int				left,
	int				top
);

static void cars__findBounds(
	const uint8_t * pBody,
	int				bodyWidth,
	int				bodyHeight,
	int *			pLeftmost,
	int *			pRightmost
);

static void cars__build(
	int16_t			   model,
	uint8_t			   flags,
	int16_t			   sticker,
	int16_t			   hoodOpen,
	uint16_t		   tireTable,
	bool			   shadow,
	int				   frameCount,
	plat_ImageHandle * pImages,
	int16_t *		   pWidth,
	int16_t *		   pHeight
);

static void cars__scroll(
	int16_t mode,
	int16_t carLeft,
	int16_t carTop,
	int16_t bodyWidth,
	int16_t visibleRows,
	int16_t stripTop
);

static void cars__composeDoor(
	int16_t sticker,
	int		bodyWidth,
	int		bodyHeight,
	int		doorLeft,
	int		doorTop
);

static void cars__buildForeground(
	bool	build,
	int16_t stripTop
);

static void cars__saveWaitress(void);

static void cars__showFrame0(
	int16_t carLeft,
	int16_t carTop,
	int16_t bodyWidth,
	int16_t visibleRows,
	int16_t bottomY
);

static uint16_t cars__partEntry(
	uint16_t car,
	int		 slot
);

static fpu_Real cars__constant(
	uint16_t offset
);

static int cars__subtype(
	uint16_t entry
);

static fpu_Real cars__speedTerm(
	uint16_t tableAt,
	uint16_t weightAt,
	fpu_Real scale
);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

static plat_ImageHandle cars__frames[CARS_FRAMES];
static plat_ImageHandle cars__previewFrame;
static uint8_t			cars__basePixels[CARS_MAX_PIXELS];
static uint8_t			cars__framePixels[CARS_MAX_PIXELS];
static uint8_t			cars__frame0Pixels[CARS_MAX_PIXELS];
static uint8_t			cars__partPixels[CARS_MAX_PART_PIXELS];
static uint8_t			cars__cleanDoor[CARS_DOOR_WIDTH * CARS_DOOR_HEIGHT];
static int16_t			cars__bodyWidth;
static int16_t			cars__bodyHeight;
static plat_ImageHandle cars__foreground;	 /**< Over the car, or 0. */
static int16_t			cars__foregroundTop; /**< Row of cars__foreground. */
static plat_ImageHandle cars__waitressFront; /**< Over the waitress, or 0. */

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Take a car record from the list of unused ones.
 *
 * \return	DS offset of the record.
 *
 * \details	Port of FUN_1000_3fb1. The caller makes sure a record is left
 * 			(fewer than 16 cars).
 *
 ******************************************************************************/
uint16_t cars_allocCar(void) {
	uint16_t car = exe_readU16(STATE_FREE_CARS);
	uint16_t next = exe_readU16((uint16_t)(car + STATE_CAR_NEXT));
	int16_t	 count = exe_readS16(STATE_CAR_COUNT);

	exe_writeU16(STATE_FREE_CARS, next);
	exe_writeU16((uint16_t)(car + STATE_CAR_NEXT), 0);
	exe_writeU16(STATE_CAR_COUNT, (uint16_t)(count + 1));

	return car;
}

/** ****************************************************************************
 *
 * \brief	Give a car record back to the list of unused ones.
 *
 * \param	car				DS offset of the record.
 *
 * \details	Port of FUN_1000_3fd4. The model becomes -1 (unused).
 *
 ******************************************************************************/
void cars_freeCar(
	uint16_t car
) {
	int16_t count = exe_readS16(STATE_CAR_COUNT);

	exe_writeU16(
		(uint16_t)(car + STATE_CAR_NEXT),
		exe_readU16(STATE_FREE_CARS)
	);
	exe_writeU16((uint16_t)(car + STATE_CAR_MODEL), 0xffff);
	exe_writeU16(STATE_FREE_CARS, car);
	exe_writeU16(STATE_CAR_COUNT, (uint16_t)(count - 1));
}

/** ****************************************************************************
 *
 * \brief	Give a part record back to the list of unused ones.
 *
 * \param	part			DS offset of the record.
 *
 * \details	Port of FUN_1000_3c3e. The index becomes -1 (unused).
 *
 ******************************************************************************/
void cars_freePart(
	uint16_t part
) {
	int16_t count = exe_readS16(STATE_PART_COUNT);

	exe_writeU16(
		(uint16_t)(part + STATE_PART_NEXT),
		exe_readU16(STATE_FREE_PARTS)
	);
	exe_writeU16((uint16_t)(part + STATE_PART_VALUE), 0xffff);
	exe_writeU16(STATE_FREE_PARTS, part);
	exe_writeU16(STATE_PART_COUNT, (uint16_t)(count - 1));
}

/** ****************************************************************************
 *
 * \brief	The price a part record counts for.
 *
 * \param	part			DS offset of the part record.
 *
 * \return	Its price, or 100 for parts of kind 0 and subtype 0 (the
 * 			stock engine block).
 *
 * \details	Port of FUN_1000_3c5a.
 *
 ******************************************************************************/
uint16_t cars_partBasePrice(
	uint16_t part
) {
	int16_t	 index = exe_readS16((uint16_t)(part + STATE_PART_VALUE));
	uint16_t entry = (uint16_t)(STATE_PARTS + index * 8);

	if (*exe_data((uint16_t)(entry + STATE_PARTS_KIND)) == 0 &&
		*exe_data((uint16_t)(entry + STATE_PARTS_SUBTYPE)) == 0) {
		return 100;
	}

	return exe_readU16((uint16_t)(part + STATE_PART_PRICE));
}

/** ****************************************************************************
 *
 * \brief	Take a part off a car that is sold or scrapped.
 *
 * \param	part			DS offset of the part record, or 0.
 * \param	keepSpare		Not 0: put the part with the spare parts; 0:
 * 							give the record back.
 *
 * \return	The price of the part, or 0 for no part.
 *
 * \details	Port of FUN_1000_4386.
 *
 ******************************************************************************/
uint16_t cars_disposePart(
	uint16_t part,
	int16_t	 keepSpare
) {
	if (part == 0) {
		return 0;
	}

	uint16_t price = exe_readU16((uint16_t)(part + STATE_PART_PRICE));

	if (keepSpare == 0) {
		cars_freePart(part);
	} else {
		exe_writeU16(
			(uint16_t)(part + STATE_PART_NEXT),
			exe_readU16(STATE_SPARE_PARTS)
		);
		exe_writeU16(STATE_SPARE_PARTS, part);
	}

	return price;
}

/** ****************************************************************************
 *
 * \brief	Take a car apart and give its record back.
 *
 * \param	car				DS offset of the car record (not linked in any
 * 							list any more).
 * \param	keepParts		Not 0: its parts become spare parts; 0: they are
 * 							given back too.
 *
 * \return	The value of the bare body (price less the parts), at least 0.
 *
 * \details	Port of FUN_1000_43bf. The tires (slot +0x12) are always given
 * 			back.
 *
 ******************************************************************************/
int16_t cars_destroyCar(
	uint16_t car,
	int16_t	 keepParts
) {
	int16_t value = exe_readS16((uint16_t)(car + STATE_CAR_PRICE));

	value = (int16_t)(value -
					  cars_disposePart(
						  exe_readU16((uint16_t)(car + STATE_CAR_GEARBOX)),
						  keepParts
					  ));

	for (int slot = 0; slot < 5; slot++) {
		uint16_t slotAt = (uint16_t)(car + STATE_CAR_ENGINE + slot * 2);

		value = (int16_t)(value -
						  cars_disposePart(exe_readU16(slotAt), keepParts));
	}

	cars_disposePart(exe_readU16((uint16_t)(car + STATE_CAR_TIRES)), 0);
	exe_writeU16((uint16_t)(car + STATE_CAR_PRICE), (uint16_t)value);

	if (value < 0) {
		value = 0;
	}

	cars_freeCar(car);

	return value;
}

/** ****************************************************************************
 *
 * \brief	Take a part record from the list of unused ones.
 *
 * \return	DS offset of the record.
 *
 * \details	Port of FUN_1000_3c1b.
 *
 ******************************************************************************/
uint16_t cars_allocPart(void) {
	uint16_t part = exe_readU16(STATE_FREE_PARTS);
	uint16_t next = exe_readU16((uint16_t)(part + STATE_PART_NEXT));
	int16_t	 count = exe_readS16(STATE_PART_COUNT);

	exe_writeU16(STATE_FREE_PARTS, next);
	exe_writeU16((uint16_t)(part + STATE_PART_NEXT), 0);
	exe_writeU16(STATE_PART_COUNT, (uint16_t)(count + 1));

	return part;
}

/** ****************************************************************************
 *
 * \brief	Make a car of a model, with the parts the model comes with.
 *
 * \param	model			Index of the car model.
 * \param	price			Price paid, stored in the record.
 * \param	car				0: take a new car record and new part records,
 * 							and make the car the one in use (or put it in
 * 							store when the player has a car in use).
 * 							Otherwise: set this car record and its part
 * 							records up again.
 *
 * \return	DS offset of the car record.
 *
 * \details	Port of FUN_1000_4036. The model's word +4 says what is
 * 			fitted: bits 1..3 the make (0..7, three sizes of each part
 * 			kind), bits 4..5 the tires, bits 6..7 the transmission, bits
 * 			8..10 the engine, bits 11..13 the manifold (which sets the
 * 			number of carburetors, table DS:0x5126), bits 14..15 the
 * 			carburetor. The car gets a random paint 0..5 (5 half as
 * 			often) and a random value -8..4 in +0x20 (use unknown); its
 * 			wear bytes start at 3. The random numbers are drawn in the
 * 			original's order.
 *
 ******************************************************************************/
uint16_t cars_create(
	int16_t	 model,
	int16_t	 price,
	uint16_t car
) {
	bool reuse = car != 0;

	if (!reuse) {
		car = cars_allocCar();
	}

	int				modelAt = STATE_MODELS + model * STATE_MODEL_SIZE;
	const uint8_t * pModel = exe_data((uint16_t)modelAt);
	uint8_t *		pCar = exe_data(car);

	exe_writeU16((uint16_t)(car + STATE_CAR_MODEL), (uint16_t)model);
	exe_writeU16((uint16_t)(car + STATE_CAR_PRICE), (uint16_t)price);
	exe_writeU16((uint16_t)(car + STATE_CAR_FUEL), CARS_START_VALUE);
	pCar[STATE_CAR_CLASS] = pModel[STATE_MODEL_CLASS];

	int16_t paint;

	do {
		paint = random_next(6);
	} while (paint == 5 && random_next(2) != 1);

	pCar[STATE_CAR_PAINT] = (uint8_t)paint;
	pCar[STATE_CAR_UNKNOWN_1F] = 1;
	pCar[STATE_CAR_UNKNOWN_20] = (uint8_t)(random_next(13) - 8);

	uint16_t fitted = exe_readU16((uint16_t)(modelAt + STATE_MODEL_FITTED));
	int16_t	 make = (int16_t)((fitted & 0xf) >> 1);
	int16_t	 gearbox = (int16_t)((fitted >> 6) & 3);
	int16_t	 engine = (int16_t)((fitted >> 8) & 7);
	int16_t	 tires = (int16_t)((fitted >> 4) & 3);
	int16_t	 intake = (int16_t)((fitted >> 11) & 7);
	int16_t	 carb = (int16_t)(fitted >> 14);
	int16_t	 extra = gearbox == 3 ? 1 : 0;
	int16_t	 gearboxPart =
		(int16_t)(make + extra + gearbox * 3 + CARS_FIRST_GEARBOX);

	cars__fit(car, STATE_CAR_GEARBOX, gearboxPart, 1, reuse);
	cars__fit(car, STATE_CAR_ENGINE, (int16_t)(engine * 3 + make), 0, reuse);
	cars__fit(
		car,
		STATE_CAR_TIRES,
		(int16_t)(tires + CARS_FIRST_TIRES),
		4,
		reuse
	);

	uint16_t intakePart = reuse
		? exe_readU16((uint16_t)(car + STATE_CAR_INTAKE))
		: cars_allocPart();

	exe_writeU16((uint16_t)(car + STATE_CAR_INTAKE), intakePart);
	memset(pCar + STATE_CAR_CARB_WEAR, 0, 2 * CARS_CARB_SLOTS);
	cars_setPart(
		intakePart,
		(int16_t)(intake * 3 + make + CARS_FIRST_INTAKE),
		3
	);

	int16_t carbs = exe_readS16((uint16_t)(CARS_CARB_COUNTS + intake * 2));

	for (int slot = carbs; slot < CARS_CARB_SLOTS; slot++) {
		exe_writeU16((uint16_t)(car + STATE_CAR_CARBS + slot * 2), 0);
	}

	for (int slot = carbs - 1; slot >= 0; slot--) {
		cars__fit(
			car,
			STATE_CAR_CARBS + slot * 2,
			(int16_t)(carb + CARS_FIRST_CARB),
			2,
			reuse
		);
		pCar[STATE_CAR_CARB_WEAR + slot * 2] = CARS_CONDITION;
		pCar[STATE_CAR_CARB_WEAR + slot * 2 + 1] = CARS_CONDITION;
	}

	memset(pCar + STATE_CAR_WEAR, CARS_CONDITION, 5);
	pCar[STATE_CAR_UNKNOWN_24] = 0;
	pCar[STATE_CAR_FLAGS] = 0;

	if (!reuse) {
		if (exe_readU16(STATE_CAR) == 0) {
			exe_writeU16(STATE_CAR, car);
		} else {
			uint16_t stored = exe_readU16(STATE_STORED_CARS);

			exe_writeU16((uint16_t)(car + STATE_CAR_NEXT), stored);
			exe_writeU16(STATE_STORED_CARS, car);
		}
	}

	return car;
}

/** ****************************************************************************
 *
 * \brief	Fill in a part record.
 *
 * \param	record			DS offset of the part record.
 * \param	part			Index into the parts table (DS:0x4806).
 * \param	kind			Kind of part, 0..4: index of the kind word in the
 * 							table at DS:0x4966.
 *
 * \details	Part of FUN_1000_4036: the record gets the part's list price,
 * 			the kind word, the part index and no link.
 *
 ******************************************************************************/
void cars_setPart(
	uint16_t record,
	int16_t	 part,
	int		 kind
) {
	uint16_t price = exe_readU16((uint16_t)(STATE_PARTS + part * 8));
	uint16_t word = exe_readU16((uint16_t)(STATE_PART_KINDS + kind * 2));

	exe_writeU16((uint16_t)(record + STATE_PART_PRICE), price);
	exe_writeU16((uint16_t)(record + STATE_PART_KIND), word);
	exe_writeU16((uint16_t)(record + STATE_PART_VALUE), (uint16_t)part);
	exe_writeU16((uint16_t)(record + STATE_PART_NEXT), 0);
}

/** ****************************************************************************
 *
 * \brief	Add a bought part to the player's spare parts.
 *
 * \param	part			Index into the parts table (DS:0x4806).
 * \param	price			Price paid.
 *
 * \details	Port of FUN_1000_3ff0: a new part record with the price paid
 * 			and the kind word of the part's kind (byte +4 of its table
 * 			entry), put at the head of the list STATE_SPARE_PARTS.
 *
 ******************************************************************************/
void cars_addSparePart(
	int16_t part,
	int16_t price
) {
	uint16_t record = cars_allocPart();
	int		 entry = STATE_PARTS + part * 8;
	int		 kind = (int8_t)*exe_data((uint16_t)(entry + STATE_PARTS_KIND));
	uint16_t word = exe_readU16((uint16_t)(STATE_PART_KINDS + kind * 2));

	exe_writeU16((uint16_t)(record + STATE_PART_VALUE), (uint16_t)part);
	exe_writeU16((uint16_t)(record + STATE_PART_KIND), word);
	exe_writeU16((uint16_t)(record + STATE_PART_PRICE), (uint16_t)price);
	exe_writeU16(
		(uint16_t)(record + STATE_PART_NEXT),
		exe_readU16(STATE_SPARE_PARTS)
	);
	exe_writeU16(STATE_SPARE_PARTS, record);
}

/** ****************************************************************************
 *
 * \brief	Set the two palette colors of a car paint.
 *
 * \param	paint			Paint number, 0..5.
 *
 * \details	Port of FUN_1000_67cf: palette entries 6 and 7 get the pair
 * 			of standard colors of the paint (table DS:0x50c2). They are
 * 			stored in the game palette, and set at once only while the
 * 			palette is shown.
 *
 ******************************************************************************/
void cars_setPaint(
	int16_t paint
) {
	uint16_t		pairAt = (uint16_t)(CARS_PAINT_TABLE + paint * 2);
	const uint8_t * pColors = exe_data(pairAt);

	for (int index = 0; index < 2; index++) {
		int color = (int8_t)pColors[index];
		int entry = CARS_PAINT_FIRST + index;

		if (globals_paletteShown != 0) {
			video_setPaletteColor(entry, color);
		} else {
			video_gamePalette[entry] = (uint8_t)(color & 15);
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Build the body-modification and blower flags of a car record.
 *
 * \param	car				DS offset of the car record.
 *
 * \return	CARS_FLAG_* bitmask for cars_draw.
 *
 * \details	Port of FUN_1000_49a2 (1000:49a2). Reads bits 13..15 of the
 * 			word at +0x24 (chopped roof, rear bumper removed, front bumper
 * 			removed) and checks whether the fitted engine has subtype 2
 * 			(a supercharger blower).
 *
 ******************************************************************************/
uint8_t cars_drawFlags(
	uint16_t car
) {
	uint8_t	 flags = 0;
	uint16_t mods = exe_readU16((uint16_t)(car + STATE_CAR_UNKNOWN_24));

	if (mods & CARS_MOD_ROOF) {
		flags |= CARS_FLAG_ROOF;
	}

	if (mods & CARS_MOD_REAR) {
		flags |= CARS_FLAG_REAR_BUMPER;
	}

	if (mods & CARS_MOD_FRONT) {
		flags |= CARS_FLAG_FRONT_BUMPER;
	}

	uint16_t engineRecord = exe_readU16((uint16_t)(car + STATE_CAR_ENGINE));
	int16_t	 enginePart =
		exe_readS16((uint16_t)(engineRecord + STATE_PART_VALUE));
	uint16_t partEntry = (uint16_t)(STATE_PARTS + enginePart * 8);
	int8_t	 subtype =
		(int8_t)*exe_data((uint16_t)(partEntry + STATE_PARTS_SUBTYPE));

	if (subtype == CARS_BLOWER_SUBTYPE) {
		flags |= CARS_FLAG_BLOWER;
	}

	return flags;
}

/** ****************************************************************************
 *
 * \brief	Compose and draw or scroll a car on the screen.
 *
 * \param	mode			CARS_DRAW_* mode (1..8).
 * \param	model			Car model index, 0..25.
 * \param	flags			CARS_FLAG_* body modification and blower bits.
 * \param	sticker			Door sticker number (1..8), or 0 for none.
 * \param	hoodOpen		Not 0 to draw the open hood behind the body.
 * \param	bottomY			Screen row of the bottom of the car.
 * \param	foreground		Not 0: the car moves behind what mask 0x448
 * 							marks in the strip (only the gas station
 * 							passes 1).
 * \param	tireTable		DS offset of the 4 tire sprite IDs, or 0.
 *
 * \details	Port of FUN_1f38_8e48 (1f38:8e48). Modes 3, 7 and 8 draw a
 * 			single static frame; modes 1, 2, 4 and 6 build 4 tire frames
 * 			and scroll the car across the 70-row strip ending at bottomY;
 * 			mode 5 updates the door sticker in place. With CARS_PASSING
 * 			1 the car scrolls on out to the right. The original gives
 * 			the sprite mover the bottom 70 rows of mask 0x448 (FUN_1f38_
 * 			84be); the port draws cars__foreground over the car instead.
 *
 ******************************************************************************/
void cars_draw(
	int16_t	 mode,
	int16_t	 model,
	uint8_t	 flags,
	int16_t	 sticker,
	int16_t	 hoodOpen,
	int16_t	 bottomY,
	int16_t	 foreground,
	uint16_t tireTable
) {
	bool isStatic = mode == CARS_DRAW_STATIC_GARAGE ||
		mode == CARS_DRAW_PREVIEW || mode == CARS_DRAW_UPDATE_GARAGE;
	int frameCount = isStatic ? 1 : CARS_FRAMES;

	if (mode != CARS_DRAW_STICKER) {
		int16_t crop =
			exe_readS16((uint16_t)(CARS_PLACE_CROP + globals_place * 2));

		exe_writeU16(CARS_TOP_CROP, (uint16_t)crop);
	}

	int16_t stripTop = (int16_t)(bottomY - (CARS_STRIP_HEIGHT - 1));

	if (mode == CARS_DRAW_SCROLL_IN || mode == CARS_DRAW_STATIC_GARAGE ||
		mode == CARS_DRAW_DRIVE_IN) {
		plat_copyRect(
			globals_workSurface,
			CARS_BACKGROUND_PAGE,
			0,
			stripTop,
			PLAT_WIDTH,
			CARS_STRIP_HEIGHT
		);
	}

	if (mode == CARS_DRAW_DRIVE_IN &&
		(globals_place == CARS_STREET_PLACE ||
		 globals_place == CARS_BARRED_PLACE)) {
		cars__saveWaitress();
	}

	if (mode != CARS_DRAW_STICKER) {
		cars__buildForeground(foreground != 0, stripTop);
	}

	/*
	 * The original rebuilt the frames only when needed; the port rebuilds
	 * them for every mode but the sticker, so a car changed in the garage
	 * (which builds a single frame) scrolls away with all 4 tire frames.
	 */
	bool needBuild = mode != CARS_DRAW_STICKER;

	if (exe_readS16(STATE_CAR_MODIFIED) != 0) {
		needBuild = true;
		exe_writeU16(STATE_CAR_MODIFIED, 0);
	}

	int16_t bodyWidth = cars__bodyWidth;
	int16_t bodyHeight = cars__bodyHeight;

	if (needBuild) {
		plat_ImageHandle * pTarget =
			mode == CARS_DRAW_PREVIEW ? &cars__previewFrame : cars__frames;

		cars__build(
			model,
			flags,
			sticker,
			hoodOpen,
			tireTable,
			true,
			frameCount,
			pTarget,
			&bodyWidth,
			&bodyHeight
		);

		if (mode != CARS_DRAW_PREVIEW) {
			cars__bodyWidth = bodyWidth;
			cars__bodyHeight = bodyHeight;
		}
	}

	int16_t carLeft = exe_readS16(STATE_CAR_LEFT);
	int16_t carTop = (int16_t)(bottomY - bodyHeight + 1);
	int16_t topCrop = exe_readS16(CARS_TOP_CROP);
	int16_t visibleRows = (int16_t)(bodyHeight - topCrop);

	exe_writeU16(STATE_CAR_TOP, (uint16_t)carTop);
	exe_writeU16(STATE_CAR_HEIGHT, (uint16_t)visibleRows);

	const int8_t * pPos =
		(const int8_t *)exe_data((uint16_t)(STATE_MODEL_POS + model * 6));
	const uint8_t * pOffsets =
		exe_data((uint16_t)(STATE_MODEL_OFFSETS + model * CARS_OFFSETS_SIZE));

	exe_writeU16(STATE_CAR_WINDOW_X, (uint16_t)(pPos[0] + carLeft));
	exe_writeU16(STATE_CAR_WINDOW_Y, (uint16_t)(pPos[1] + carTop));
	exe_writeU16(STATE_CAR_EXHAUST1_X, (uint16_t)(int16_t)pPos[2]);
	exe_writeU16(STATE_CAR_EXHAUST1_Y, (uint16_t)(int16_t)pPos[3]);

	uint16_t hasExhaust = (pPos[2] != 0 && pPos[3] != 0) ? 1 : 0;

	exe_writeU16(STATE_CAR_HAS_EXHAUST, hasExhaust);
	exe_writeU16(STATE_CAR_EXHAUST2_X, (uint16_t)(int16_t)pPos[4]);
	exe_writeU16(STATE_CAR_EXHAUST2_Y, (uint16_t)(int16_t)pPos[5]);

	if (pPos[4] > 0) {
		exe_writeU16(
			STATE_CAR_EXHAUST2_X,
			(uint16_t)(exe_readS16(STATE_CAR_EXHAUST2_X) + carLeft)
		);
		exe_writeU16(
			STATE_CAR_EXHAUST2_Y,
			(uint16_t)(exe_readS16(STATE_CAR_EXHAUST2_Y) + carTop)
		);
		exe_writeU16(
			STATE_CAR_EXHAUST1_X,
			(uint16_t)(exe_readS16(STATE_CAR_EXHAUST1_X) + carLeft)
		);
		exe_writeU16(
			STATE_CAR_EXHAUST1_Y,
			(uint16_t)(exe_readS16(STATE_CAR_EXHAUST1_Y) + carTop)
		);
	}

	int16_t doorOffsetX = pOffsets[10];
	int16_t doorOffsetY = pOffsets[11];

	exe_writeU16(STATE_CAR_DOOR_SHIFT, (uint16_t)(doorOffsetX & 7));
	exe_writeU16(
		STATE_CAR_DOOR_X,
		(uint16_t)((doorOffsetX & ~7) + carLeft)
	);
	exe_writeU16(STATE_CAR_DOOR_Y, (uint16_t)(doorOffsetY + carTop));

	if (mode == CARS_DRAW_STICKER) {
		cars__composeDoor(
			sticker,
			bodyWidth,
			bodyHeight,
			doorOffsetX,
			doorOffsetY
		);
		cars__showFrame0(carLeft, carTop, bodyWidth, visibleRows, bottomY);
		exe_writeU16(CARS_TOP_CROP, 0);
		exe_writeU16(CARS_PASSING, 0);
		return;
	}

	if (mode == CARS_DRAW_STATIC_GARAGE) {
		plat_selectPage(globals_workSurface);
		plat_drawImageRows(cars__frames[0], carLeft, carTop, visibleRows);
		screen_copyToDisplay(0, 0, PLAT_WIDTH - 1, PLAT_HEIGHT - 1);
	} else if (mode == CARS_DRAW_PREVIEW) {
		plat_selectPage(globals_workSurface);
		plat_drawImageRows(cars__previewFrame, carLeft, carTop, visibleRows);
		screen_copyToDisplay(
			carLeft,
			carTop,
			(int16_t)(carLeft + bodyWidth - 1),
			bottomY
		);
	} else if (mode == CARS_DRAW_UPDATE_GARAGE) {
		plat_copyRect(
			CARS_BACKGROUND_PAGE,
			globals_workSurface,
			carLeft,
			carTop,
			bodyWidth,
			visibleRows
		);
		plat_selectPage(globals_workSurface);
		plat_drawImageRows(cars__frames[0], carLeft, carTop, visibleRows);
		screen_copyToDisplay(
			carLeft,
			carTop,
			(int16_t)(carLeft + bodyWidth - 1),
			bottomY
		);
	} else {
		cars__scroll(
			mode,
			carLeft,
			carTop,
			bodyWidth,
			visibleRows,
			stripTop
		);

		/*
		 * A passing car (1f38:9746): the loop of the original runs again
		 * as mode 2 with the frames already built.
		 */
		if (exe_readS16(CARS_PASSING) == 1) {
			exe_writeU16(CARS_PASSING, 2);
			cars__scroll(
				CARS_DRAW_SCROLL_RIGHT,
				carLeft,
				carTop,
				bodyWidth,
				visibleRows,
				stripTop
			);
		}
	}

	exe_writeU16(CARS_TOP_CROP, 0);
	exe_writeU16(CARS_PASSING, 0);
}

/** ****************************************************************************
 *
 * \brief	Give a car a part in one of its slots.
 *
 * \param	car				DS offset of the car record.
 * \param	slot			Offset of the slot (a near pointer) in the car
 * 							record.
 * \param	part			Index into the parts table.
 * \param	kind			Kind of part, 0..4.
 * \param	reuse			true: fill the part record the slot points to;
 * 							false: take a new part record.
 *
 * \return	DS offset of the part record.
 *
 ******************************************************************************/
static uint16_t cars__fit(
	uint16_t car,
	int		 slot,
	int16_t	 part,
	int		 kind,
	bool	 reuse
) {
	uint16_t slotAt = (uint16_t)(car + slot);
	uint16_t record = reuse ? exe_readU16(slotAt) : cars_allocPart();

	exe_writeU16(slotAt, record);
	cars_setPart(record, part, kind);

	return record;
}

/** ****************************************************************************
 *
 * \brief	Decode a library resource into a byte-per-pixel buffer.
 *
 * \param	gameId			The game's resource number.
 * \param	pPixels			Output buffer (at least width * height bytes).
 * \param	pWidth			Output: width in pixels.
 * \param	pHeight			Output: height in pixels.
 *
 * \retval	true	The resource was decoded.
 * \retval	false	The resource could not be found or decoded.
 *
 ******************************************************************************/
static bool cars__decodeResource(
	int16_t	  gameId,
	uint8_t * pPixels,
	int *	  pWidth,
	int *	  pHeight
) {
	int					 index = lib_indexFromGameId(gameId);
	const lib_Resource * pResource = lib_get(index);

	if (!pResource) {
		return false;
	}

	*pWidth = pResource->width;
	*pHeight = pResource->height;

	return lib_decodePixels(index, pPixels);
}

/** ****************************************************************************
 *
 * \brief	Copy a rectangular patch over the car body, including color 0.
 *
 * \param	pBody			Car body pixel buffer.
 * \param	bodyWidth		Width of pBody.
 * \param	bodyHeight		Height of pBody.
 * \param	pPatch			Patch pixel buffer.
 * \param	patchWidth		Width of pPatch.
 * \param	patchHeight		Height of pPatch.
 * \param	left			Destination column in pBody.
 * \param	top				Destination row in pBody.
 *
 * \details	Port of FUN_1f38_b360. Used for bumper-removed and chopped-roof
 * 			patches, where color 0 erases the original bumper or roofline.
 *
 ******************************************************************************/
static void cars__blitOverwrite(
	uint8_t *		pBody,
	int				bodyWidth,
	int				bodyHeight,
	const uint8_t * pPatch,
	int				patchWidth,
	int				patchHeight,
	int				left,
	int				top
) {
	for (int row = 0; row < patchHeight; row++) {
		int destRow = top + row;

		if (destRow < 0 || destRow >= bodyHeight) {
			continue;
		}

		for (int column = 0; column < patchWidth; column++) {
			int destColumn = left + column;

			if (destColumn >= 0 && destColumn < bodyWidth) {
				pBody[destRow * bodyWidth + destColumn] =
					pPatch[row * patchWidth + column];
			}
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Draw a transparent sprite on top of the car body.
 *
 * \param	pBody			Car body pixel buffer.
 * \param	bodyWidth		Width of pBody.
 * \param	bodyHeight		Height of pBody.
 * \param	pPatch			Sprite pixel buffer (0 is transparent).
 * \param	patchWidth		Width of pPatch.
 * \param	patchHeight		Height of pPatch.
 * \param	left			Destination column in pBody.
 * \param	top				Destination row in pBody.
 *
 * \details	Used for the supercharger blower and the door sticker, which
 * 			sit in front of the car body.
 *
 ******************************************************************************/
static void cars__blitOverlay(
	uint8_t *		pBody,
	int				bodyWidth,
	int				bodyHeight,
	const uint8_t * pPatch,
	int				patchWidth,
	int				patchHeight,
	int				left,
	int				top
) {
	for (int row = 0; row < patchHeight; row++) {
		int destRow = top + row;

		if (destRow < 0 || destRow >= bodyHeight) {
			continue;
		}

		for (int column = 0; column < patchWidth; column++) {
			int		destColumn = left + column;
			uint8_t pixel = pPatch[row * patchWidth + column];

			if (pixel != 0 && destColumn >= 0 && destColumn < bodyWidth) {
				pBody[destRow * bodyWidth + destColumn] = pixel;
			}
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Draw a sprite behind the car body where the body is color 0.
 *
 * \param	pBody			Car body pixel buffer.
 * \param	bodyWidth		Width of pBody.
 * \param	bodyHeight		Height of pBody.
 * \param	pPatch			Sprite pixel buffer (0 is transparent).
 * \param	patchWidth		Width of pPatch.
 * \param	patchHeight		Height of pPatch.
 * \param	left			Destination column in pBody.
 * \param	top				Destination row in pBody.
 *
 * \details	Port of FUN_1f38_b0f2. Used for the open hood, the tires inside
 * 			the wheel wells, and the shadow under the car.
 *
 ******************************************************************************/
static void cars__blitBehind(
	uint8_t *		pBody,
	int				bodyWidth,
	int				bodyHeight,
	const uint8_t * pPatch,
	int				patchWidth,
	int				patchHeight,
	int				left,
	int				top
) {
	for (int row = 0; row < patchHeight; row++) {
		int destRow = top + row;

		if (destRow < 0 || destRow >= bodyHeight) {
			continue;
		}

		for (int column = 0; column < patchWidth; column++) {
			int		destColumn = left + column;
			uint8_t pixel = pPatch[row * patchWidth + column];

			if (pixel != 0 && destColumn >= 0 && destColumn < bodyWidth) {
				uint8_t * pTarget = &pBody[destRow * bodyWidth + destColumn];

				if (*pTarget == 0) {
					*pTarget = pixel;
				}
			}
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Find the leftmost and rightmost non-zero columns in the outer
 * 			byte columns of a car sprite.
 *
 * \param	pBody			Car pixel buffer.
 * \param	bodyWidth		Width of pBody.
 * \param	bodyHeight		Height of pBody.
 * \param	pLeftmost		Output: first non-zero column in 0..7 (or 8).
 * \param	pRightmost		Output: last non-zero column in
 * 							bodyWidth - 8 .. bodyWidth - 1.
 *
 * \details	Port of FUN_1f38_abc2, which positions the left and right ends
 * 			of the shadow under the car.
 *
 ******************************************************************************/
static void cars__findBounds(
	const uint8_t * pBody,
	int				bodyWidth,
	int				bodyHeight,
	int *			pLeftmost,
	int *			pRightmost
) {
	int leftmost = 8;

	for (int column = 0; column < 8; column++) {
		bool found = false;

		for (int row = 0; row < bodyHeight; row++) {
			if (pBody[row * bodyWidth + column] != 0) {
				found = true;
				break;
			}
		}

		if (found) {
			leftmost = column;
			break;
		}
	}

	int rightmost = bodyWidth - 9;

	for (int column = bodyWidth - 1; column >= bodyWidth - 8; column--) {
		bool found = false;

		for (int row = 0; row < bodyHeight; row++) {
			if (pBody[row * bodyWidth + column] != 0) {
				found = true;
				break;
			}
		}

		if (found) {
			rightmost = column;
			break;
		}
	}

	*pLeftmost = leftmost;
	*pRightmost = rightmost;
}

/** ****************************************************************************
 *
 * \brief	Assemble the composite car sprite frames for a model.
 *
 * \param	model			Car model index, 0..25.
 * \param	flags			CARS_FLAG_* body modification and blower bits.
 * \param	sticker			Door sticker number (1..8), or 0 for none.
 * \param	hoodOpen		Not 0 to draw the open hood behind the body.
 * \param	tireTable		DS offset of the 4 tire sprite IDs, or 0.
 * \param	shadow			true: put the shadow under the car (param 7).
 * \param	frameCount		1 for static modes, 4 for scrolling modes.
 * \param	pImages			Output array of frameCount image handles.
 * \param	pWidth			Output: width of the car sprite.
 * \param	pHeight			Output: height of the car sprite.
 *
 * \details	Port of FUN_1f38_8866 (1f38:8866). Composites the body, bumper
 * 			and chopped-roof replacements, blower, door sticker, open hood,
 * 			front and rear tires, and under-car shadow in the original
 * 			order.
 *
 ******************************************************************************/
static void cars__build(
	int16_t			   model,
	uint8_t			   flags,
	int16_t			   sticker,
	int16_t			   hoodOpen,
	uint16_t		   tireTable,
	bool			   shadow,
	int				   frameCount,
	plat_ImageHandle * pImages,
	int16_t *		   pWidth,
	int16_t *		   pHeight
) {
	uint16_t modelAt = (uint16_t)(STATE_MODELS + model * STATE_MODEL_SIZE);
	uint16_t partsAt =
		(uint16_t)(STATE_MODEL_PARTS + model * STATE_MODEL_SIZE);
	const uint8_t * pOffsets =
		exe_data((uint16_t)(STATE_MODEL_OFFSETS + model * CARS_OFFSETS_SIZE));
	int16_t bodyId = exe_readS16((uint16_t)(modelAt + STATE_MODEL_BODY));
	int		bodyWidth = 0;
	int		bodyHeight = 0;

	if (!cars__decodeResource(
			bodyId,
			cars__basePixels,
			&bodyWidth,
			&bodyHeight
		)) {
		return;
	}

	*pWidth = (int16_t)bodyWidth;
	*pHeight = (int16_t)bodyHeight;

	int16_t carLeft = (int16_t)(((PLAT_WIDTH - bodyWidth) / 2 + 4) & ~7);

	exe_writeU16(STATE_CAR_LEFT, (uint16_t)carLeft);

	int patchWidth;
	int patchHeight;

	if (flags & CARS_FLAG_REAR_BUMPER) {
		int16_t rearId = exe_readS16((uint16_t)(partsAt + STATE_PARTS_REAR));

		if (cars__decodeResource(
				rearId,
				cars__partPixels,
				&patchWidth,
				&patchHeight
			)) {
			cars__blitOverwrite(
				cars__basePixels,
				bodyWidth,
				bodyHeight,
				cars__partPixels,
				patchWidth,
				patchHeight,
				0,
				pOffsets[7]
			);
		}
	}

	if (flags & CARS_FLAG_FRONT_BUMPER) {
		int16_t frontId = exe_readS16((uint16_t)(partsAt + STATE_PARTS_FRONT));

		if (cars__decodeResource(
				frontId,
				cars__partPixels,
				&patchWidth,
				&patchHeight
			)) {
			cars__blitOverwrite(
				cars__basePixels,
				bodyWidth,
				bodyHeight,
				cars__partPixels,
				patchWidth,
				patchHeight,
				pOffsets[8],
				pOffsets[9]
			);
		}
	}

	int roofDrop = 0;

	if (flags & CARS_FLAG_ROOF) {
		int16_t roofId = exe_readS16((uint16_t)(partsAt + STATE_PARTS_ROOF));

		if (cars__decodeResource(
				roofId,
				cars__partPixels,
				&patchWidth,
				&patchHeight
			)) {
			int totalPixels = patchWidth * patchHeight;
			int emptyPixels = 0;

			while (emptyPixels < totalPixels &&
				   cars__partPixels[emptyPixels] == 0) {
				emptyPixels++;
			}

			int chunksPerRow = patchWidth / 8;

			if (chunksPerRow > 0) {
				int emptyChunks = emptyPixels / 8;
				roofDrop = emptyChunks / chunksPerRow;
			}

			cars__blitOverwrite(
				cars__basePixels,
				bodyWidth,
				bodyHeight,
				cars__partPixels,
				patchWidth,
				patchHeight,
				pOffsets[4],
				0
			);
		}
	}

	if ((flags & CARS_FLAG_BLOWER) != 0 && model != CARS_NO_BLOWER_MODEL) {
		int16_t blowerId =
			exe_readS16((uint16_t)(partsAt + STATE_PARTS_BLOWER));

		if (cars__decodeResource(
				blowerId,
				cars__partPixels,
				&patchWidth,
				&patchHeight
			)) {
			cars__blitOverlay(
				cars__basePixels,
				bodyWidth,
				bodyHeight,
				cars__partPixels,
				patchWidth,
				patchHeight,
				pOffsets[5],
				pOffsets[6]
			);
		}
	}

	int doorLeft = pOffsets[10];
	int doorTop = pOffsets[11];

	memset(cars__cleanDoor, 0, sizeof cars__cleanDoor);

	for (int row = 0; row < CARS_DOOR_HEIGHT; row++) {
		int sourceRow = doorTop + row;

		if (sourceRow < 0 || sourceRow >= bodyHeight) {
			continue;
		}

		for (int column = 0; column < CARS_DOOR_WIDTH; column++) {
			int sourceColumn = doorLeft + column;

			if (sourceColumn >= 0 && sourceColumn < bodyWidth) {
				cars__cleanDoor[row * CARS_DOOR_WIDTH + column] =
					cars__basePixels[sourceRow * bodyWidth + sourceColumn];
			}
		}
	}

	if (sticker > 0) {
		uint16_t stickerAt =
			(uint16_t)(STATE_STICKERS + (sticker - 1) * CARS_STICKER_SIZE);
		int16_t stickerId = exe_readS16(stickerAt);
		int16_t deltaX = exe_readS16((uint16_t)(stickerAt + 2));
		int16_t deltaY = exe_readS16((uint16_t)(stickerAt + 4));

		if (cars__decodeResource(
				stickerId,
				cars__partPixels,
				&patchWidth,
				&patchHeight
			)) {
			cars__blitOverlay(
				cars__basePixels,
				bodyWidth,
				bodyHeight,
				cars__partPixels,
				patchWidth,
				patchHeight,
				doorLeft + deltaX,
				doorTop + deltaY
			);
		}
	}

	if (hoodOpen != 0) {
		int16_t hoodId = exe_readS16(STATE_GARAGE_HOOD);

		if (cars__decodeResource(
				hoodId,
				cars__partPixels,
				&patchWidth,
				&patchHeight
			)) {
			cars__blitBehind(
				cars__basePixels,
				bodyWidth,
				bodyHeight,
				cars__partPixels,
				patchWidth,
				patchHeight,
				pOffsets[2],
				pOffsets[3] - patchHeight + roofDrop + 1
			);
		}
	}

	size_t bodyBytes = (size_t)bodyWidth * (size_t)bodyHeight;

	for (int frame = 0; frame < frameCount; frame++) {
		memcpy(cars__framePixels, cars__basePixels, bodyBytes);

		if (tireTable != 0) {
			int16_t tireId = exe_readS16((uint16_t)(tireTable + frame * 2));

			if (cars__decodeResource(
					tireId,
					cars__partPixels,
					&patchWidth,
					&patchHeight
				)) {
				int tireTop = bodyHeight - patchHeight;

				cars__blitBehind(
					cars__framePixels,
					bodyWidth,
					bodyHeight,
					cars__partPixels,
					patchWidth,
					patchHeight,
					pOffsets[0],
					tireTop
				);
				cars__blitBehind(
					cars__framePixels,
					bodyWidth,
					bodyHeight,
					cars__partPixels,
					patchWidth,
					patchHeight,
					pOffsets[1],
					tireTop
				);
			}
		}

		int leftmost;
		int rightmost;

		cars__findBounds(
			cars__framePixels,
			bodyWidth,
			bodyHeight,
			&leftmost,
			&rightmost
		);

		if (shadow &&
			cars__decodeResource(
				CARS_SHADOW_RESOURCE,
				cars__partPixels,
				&patchWidth,
				&patchHeight
			)) {
			int shadowTop = bodyHeight - patchHeight;

			cars__blitBehind(
				cars__framePixels,
				bodyWidth,
				bodyHeight,
				cars__partPixels,
				patchWidth,
				patchHeight,
				leftmost,
				shadowTop
			);
			cars__blitBehind(
				cars__framePixels,
				bodyWidth,
				bodyHeight,
				cars__partPixels,
				patchWidth,
				patchHeight,
				rightmost - patchWidth + 1,
				shadowTop
			);
		}

		if (frame == 0 && pImages == cars__frames) {
			memcpy(cars__frame0Pixels, cars__framePixels, bodyBytes);
		}

		if (pImages[frame] != 0) {
			plat_imageDestroy(pImages[frame]);
		}

		pImages[frame] = plat_imageCreate(
			NULL,
			bodyWidth,
			bodyHeight,
			cars__framePixels,
			true
		);
	}
}

/** ****************************************************************************
 *
 * \brief	Scroll the car across the screen in three speed phases.
 *
 * \param	mode			CARS_DRAW_SCROLL_IN, _SCROLL_RIGHT, _DRIVE_IN or
 * 							_SCROLL_LEFT.
 * \param	carLeft			Resting left edge of the car.
 * \param	carTop			Top row of the car.
 * \param	bodyWidth		Width of the car in pixels.
 * \param	visibleRows		Rows of the car shown from its top.
 * \param	stripTop		Top row of the 70-row background strip.
 *
 * \details	Port of FUN_1f38_a7ee, FUN_1f38_a6ad, FUN_1f38_aa37 and
 * 			FUN_31a0_25d9. Each step restores the 70-row strip from
 * 			CARS_BACKGROUND_PAGE, draws the tire rotation frame matching
 * 			the horizontal position, and waits one timer tick. As in
 * 			DrawCar (1f38:971c..9773) the timer handler of a drive runs
 * 			meanwhile with DS:0x286 and DS:0x5a0e set, which makes the
 * 			sound of the car; a passing car (CARS_PASSING) keeps the
 * 			handler between its two scrolls.
 *
 ******************************************************************************/
static void cars__scroll(
	int16_t mode,
	int16_t carLeft,
	int16_t carTop,
	int16_t bodyWidth,
	int16_t visibleRows,
	int16_t stripTop
) {
	int16_t		distance;
	int16_t		column;
	cars__Phase phases[4];

	if (mode == CARS_DRAW_SCROLL_RIGHT) {
		distance = (int16_t)(PLAT_WIDTH - carLeft);
	} else {
		distance = (int16_t)(carLeft + bodyWidth);
	}

	if (mode == CARS_DRAW_SCROLL_IN || mode == CARS_DRAW_DRIVE_IN) {
		int16_t pct1 = globals_place == 1 ? 0 : 25;
		int16_t pct2 = globals_place == 1 ? 60 : 55;
		int16_t step0 = 8;
		int16_t step1 = 4;
		int16_t step2 = 2;
		int16_t count2 =
			(int16_t)((((100 - pct1 - pct2) * distance) / 100) / step2 & ~3);
		int16_t count1 = (int16_t)(((pct2 * distance) / 100) / step1 & ~1);
		int16_t span = (int16_t)(distance - count1 * step1 - count2 * step2);
		int16_t count0 = (int16_t)(span / step0 + 1);

		phases[0].count = count0;
		phases[0].step = step0;
		phases[1].count = count1;
		phases[1].step = step1;
		phases[2].count = count2;
		phases[2].step = step2;
		column = (int16_t)(carLeft - distance);
	} else {
		int16_t pct1 = mode == CARS_DRAW_SCROLL_RIGHT ? 10 : 40;
		int16_t pct2 = 60;
		int16_t step0 = mode == CARS_DRAW_SCROLL_RIGHT ? 2 : -2;
		int16_t step1 = mode == CARS_DRAW_SCROLL_RIGHT ? 4 : -4;
		int16_t step2 = mode == CARS_DRAW_SCROLL_RIGHT ? 8 : -4;
		int16_t base0 = (int16_t)(abs(((pct1 * distance) / 100) / step0) & ~3);
		int16_t count0 = (int16_t)(base0 + 1);
		int16_t raw1 = (int16_t)abs(((pct2 * distance) / 100) / step1);
		int16_t count1 = (int16_t)(raw1 & ~1);
		int16_t used = (int16_t)(abs(step0) * base0 + abs(step1) * count1);
		int16_t remaining = (int16_t)(distance - used);
		int16_t count2 = (int16_t)abs(remaining / step2);

		phases[0].count = count0;
		phases[0].step = step0;
		phases[1].count = count1;
		phases[1].step = step1;
		phases[2].count = count2;
		phases[2].step = step2;
		column = mode == CARS_DRAW_SCROLL_RIGHT
			? carLeft
			: exe_readS16(STATE_CAR_SCROLL_X);
	}

	phases[3].count = -1;
	phases[3].step = 0;

	int phaseIndex = 0;

	exe_writeU16(DRIVE_NOT_DRIVEN, 1);
	exe_writeU16(DRIVE_SCROLL_SOUND, 1);

	if (exe_readS16(CARS_PASSING) != 2) {
		drive_timerOn();
	}

	while (phaseIndex < 3 && phases[phaseIndex].count == 0) {
		phaseIndex++;
	}

	while (phases[phaseIndex].count >= 0) {
		int frame = ((uint16_t)column >> 1) & 3;

		plat_copyRect(
			CARS_BACKGROUND_PAGE,
			globals_displaySurface,
			0,
			stripTop,
			PLAT_WIDTH,
			CARS_STRIP_HEIGHT
		);
		plat_selectPage(globals_displaySurface);
		plat_drawImageRows(cars__frames[frame], column, carTop, visibleRows);

		if (globals_place == CARS_BARRED_PLACE) {
			/*
			 * ScrollCarFrame (31a0:25d9) draws the car only in the 6
			 * gaps (left, width) at DS:0x539a between the bars of place
			 * 6; the port puts the background back outside them.
			 */
			int16_t done = 0;

			for (int gap = 0; gap <= CARS_GAP_COUNT; gap++) {
				uint16_t gapAt = (uint16_t)(CARS_GAPS + gap * 4);
				int16_t	 left = gap < CARS_GAP_COUNT ? exe_readS16(gapAt)
													 : PLAT_WIDTH;

				if (left > done) {
					plat_copyRect(
						CARS_BACKGROUND_PAGE,
						globals_displaySurface,
						done,
						stripTop,
						(int16_t)(left - done),
						CARS_STRIP_HEIGHT
					);
				}

				if (gap < CARS_GAP_COUNT) {
					done = (int16_t)(left + exe_readS16((uint16_t)(gapAt + 2)));
				}
			}
		}

		if (cars__foreground != 0) {
			plat_drawImage(cars__foreground, 0, cars__foregroundTop);
		}

		cursor_surfaceCopied(0, stripTop, PLAT_WIDTH, CARS_STRIP_HEIGHT);

		uint32_t startTick = globals_ticks;

		exe_writeU16(STATE_CAR_SCROLL_X, (uint16_t)column);

		while (globals_ticks == startTick) {
			dos_service();
		}

		phases[phaseIndex].count--;

		while (phaseIndex < 3 && phases[phaseIndex].count == 0) {
			phaseIndex++;
		}

		if (phases[phaseIndex].count != -1) {
			column = (int16_t)(column + phases[phaseIndex].step);
		}
	}

	if (exe_readS16(CARS_PASSING) != 1) {
		drive_timerOff();
	}

	exe_writeU16(DRIVE_SCROLL_SOUND, 0);
	exe_writeU16(DRIVE_NOT_DRIVEN, 0);

	plat_copyRect(
		globals_displaySurface,
		globals_workSurface,
		0,
		stripTop,
		PLAT_WIDTH,
		CARS_STRIP_HEIGHT
	);
	plat_selectPage(globals_workSurface);
}

/** ****************************************************************************
 *
 * \brief	Put the clean door and a sticker into the first car frame.
 *
 * \param	sticker			Sticker number (1..8), or 0 for none.
 * \param	bodyWidth		Width of the car sprite.
 * \param	bodyHeight		Height of the car sprite.
 * \param	doorLeft		Column of the door in the car sprite.
 * \param	doorTop			Row of the door in the car sprite.
 *
 * \details	Part of FUN_1f38_8e48 (mode 5): the door saved by cars__build
 * 			goes back over the old sticker and the new sticker is drawn
 * 			over it. cars__frames[0] is made again from the pixels.
 *
 ******************************************************************************/
static void cars__composeDoor(
	int16_t sticker,
	int		bodyWidth,
	int		bodyHeight,
	int		doorLeft,
	int		doorTop
) {
	cars__blitOverwrite(
		cars__frame0Pixels,
		bodyWidth,
		bodyHeight,
		cars__cleanDoor,
		CARS_DOOR_WIDTH,
		CARS_DOOR_HEIGHT,
		doorLeft,
		doorTop
	);

	if (sticker > 0) {
		uint16_t stickerAt =
			(uint16_t)(STATE_STICKERS + (sticker - 1) * CARS_STICKER_SIZE);
		int16_t stickerId = exe_readS16(stickerAt);
		int16_t deltaX = exe_readS16((uint16_t)(stickerAt + 2));
		int16_t deltaY = exe_readS16((uint16_t)(stickerAt + 4));
		int		patchWidth;
		int		patchHeight;

		if (cars__decodeResource(
				stickerId,
				cars__partPixels,
				&patchWidth,
				&patchHeight
			)) {
			cars__blitOverlay(
				cars__frame0Pixels,
				bodyWidth,
				bodyHeight,
				cars__partPixels,
				patchWidth,
				patchHeight,
				doorLeft + deltaX,
				doorTop + deltaY
			);
		}
	}

	if (cars__frames[0] != 0) {
		plat_imageDestroy(cars__frames[0]);
	}

	cars__frames[0] = plat_imageCreate(
		NULL,
		bodyWidth,
		bodyHeight,
		cars__frame0Pixels,
		true
	);
}

/** ****************************************************************************
 *
 * \brief	Draw the first car frame over the saved background and show it.
 *
 * \param	carLeft			Left edge of the car.
 * \param	carTop			Top edge of the car.
 * \param	bodyWidth		Width of the car sprite.
 * \param	visibleRows		Rows of the car shown.
 * \param	bottomY			Bottom row of the car.
 *
 ******************************************************************************/
static void cars__showFrame0(
	int16_t carLeft,
	int16_t carTop,
	int16_t bodyWidth,
	int16_t visibleRows,
	int16_t bottomY
) {
	plat_copyRect(
		CARS_BACKGROUND_PAGE,
		globals_workSurface,
		carLeft,
		carTop,
		bodyWidth,
		visibleRows
	);
	plat_selectPage(globals_workSurface);
	plat_drawImageRows(cars__frames[0], carLeft, carTop, visibleRows);
	screen_copyToDisplay(
		carLeft,
		carTop,
		(int16_t)(carLeft + bodyWidth - 1),
		bottomY
	);
}

/** ****************************************************************************
 *
 * \brief	Take the sticker off the door of the car in the garage.
 *
 * \details	Port of the door restore in FUN_1000_6f6b (the buffer at
 * 			DS:0x294 put back at DS:0x8272, DS:0x8274): new paint takes
 * 			the sticker off. The caller clears the sticker bits.
 *
 ******************************************************************************/
void cars_restoreDoor(void) {
	cars_draw(
		CARS_DRAW_STICKER,
		exe_readS16(STATE_GARAGE_MODEL),
		0,
		0,
		0,
		(int16_t)(exe_readS16(STATE_CAR_TOP) + cars__bodyHeight - 1),
		0,
		0
	);
}

/** ****************************************************************************
 *
 * \brief	Build or drop the foreground the car moves behind.
 *
 * \param	build			true: build it; false: drop it.
 * \param	stripTop		Top row of the 70-row strip.
 *
 * \details	Stands in for the mask FUN_1f38_84be gives the sprite mover:
 * 			the bottom 70 rows of resource 0x448, column for column on
 * 			the screen, are 0 where the foreground (pumps, bucket, rag)
 * 			hides the car. The foreground image holds the original
 * 			pixels of the background strip (CARS_BACKGROUND_PAGE, read
 * 			with plat_readPixels) there and is see-through elsewhere. A
 * 			mod that replaces the background picture should also give
 * 			CARS_FOREGROUND_NAME, or the original pixels show over it.
 *
 ******************************************************************************/
static void cars__buildForeground(
	bool	build,
	int16_t stripTop
) {
	if (cars__foreground != 0) {
		plat_imageDestroy(cars__foreground);
		cars__foreground = 0;
	}

	if (!build) {
		return;
	}

	int					 index = lib_indexFromGameId(CARS_FOREGROUND_MASK);
	const lib_Resource * pMask = lib_get(index);

	if (pMask == NULL) {
		return;
	}

	int		  maskWidth = pMask->width;
	int		  maskHeight = pMask->height;
	uint8_t * pMaskPixels = calloc((size_t)maskWidth * (size_t)maskHeight, 1);
	uint8_t * pPixels = malloc((size_t)PLAT_WIDTH * CARS_STRIP_HEIGHT);

	if (pMaskPixels != NULL && pPixels != NULL &&
		lib_decodePixels(index, pMaskPixels)) {
		int firstRow = maskHeight - CARS_STRIP_HEIGHT;

		plat_readPixels(
			CARS_BACKGROUND_PAGE,
			0,
			stripTop,
			PLAT_WIDTH,
			CARS_STRIP_HEIGHT,
			pPixels
		);

		for (int row = 0; row < CARS_STRIP_HEIGHT; row++) {
			int maskRow = firstRow + row;

			for (int column = 0; column < PLAT_WIDTH; column++) {
				bool hidden = maskRow >= 0 && column < maskWidth &&
					pMaskPixels[maskRow * maskWidth + column] == 0;

				if (!hidden) {
					pPixels[row * PLAT_WIDTH + column] = PLAT_TRANSPARENT;
				}
			}
		}

		cars__foreground = plat_imageCreateOverlay(
			CARS_FOREGROUND_NAME,
			PLAT_WIDTH,
			CARS_STRIP_HEIGHT,
			pPixels
		);
		cars__foregroundTop = stripTop;
	}

	free(pMaskPixels);
	free(pPixels);
}

/** ****************************************************************************
 *
 * \brief	Draw the resting car again on the work surface.
 *
 * \details	For pictures that change behind the car (the gas station):
 * 			the frame the car stopped with is drawn at STATE_CAR_LEFT and
 * 			STATE_CAR_TOP, STATE_CAR_HEIGHT rows of it, without the
 * 			foreground (cars_drawForeground). Nothing is copied to the
 * 			display. Not in the original, whose sprite engine and saved
 * 			pictures kept the car in place.
 *
 ******************************************************************************/
void cars_drawOnWork(void) {
	int16_t carLeft = exe_readS16(STATE_CAR_LEFT);
	int		frame = ((uint16_t)carLeft >> 1) & 3;

	if (cars__frames[frame] == 0) {
		return;
	}

	plat_selectPage(globals_workSurface);
	plat_drawImageRows(
		cars__frames[frame],
		carLeft,
		exe_readS16(STATE_CAR_TOP),
		exe_readS16(STATE_CAR_HEIGHT)
	);
}

/** ****************************************************************************
 *
 * \brief	Draw the resting car again on the work surface, only inside a
 * 			rectangle.
 *
 * \param	left			Left edge of the rectangle.
 * \param	top				Top edge.
 * \param	width			Its width.
 * \param	height			Its height.
 *
 * \details	Like cars_drawOnWork, clipped. Stands in for the original's
 * 			drawing of the car into the frames of an animation that
 * 			plays behind it (the code at 1f38:1b4a, with FUN_1f38_a198).
 *
 ******************************************************************************/
void cars_drawOnWorkPart(
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height
) {
	int16_t	  carLeft = exe_readS16(STATE_CAR_LEFT);
	int		  frame = ((uint16_t)carLeft >> 1) & 3;
	const int clip[4] = { left, top, width, height };

	if (cars__frames[frame] == 0 || cars__bodyWidth <= 0) {
		return;
	}

	plat_selectPage(globals_workSurface);
	plat_drawImagePart(
		cars__frames[frame],
		0,
		0,
		cars__bodyWidth,
		exe_readS16(STATE_CAR_HEIGHT),
		carLeft,
		exe_readS16(STATE_CAR_TOP),
		clip
	);
}

/** ****************************************************************************
 *
 * \brief	Draw columns of the foreground the car moves behind.
 *
 * \param	left			First column.
 * \param	right			Last column.
 *
 * \details	Draws the foreground of the last cars_draw with foreground
 * 			set (nothing when there is none) on the work surface, in
 * 			the given columns of the strip. Nothing is copied to the
 * 			display.
 *
 ******************************************************************************/
void cars_drawForeground(
	int16_t left,
	int16_t right
) {
	if (cars__foreground == 0 || right < left) {
		return;
	}

	plat_selectPage(globals_workSurface);
	plat_drawImagePart(
		cars__foreground,
		left,
		0,
		right - left + 1,
		CARS_STRIP_HEIGHT,
		left,
		cars__foregroundTop,
		NULL
	);
}

/** ****************************************************************************
 *
 * \brief	Wipe a new sticker onto the door of the car in the garage.
 *
 * \param	sticker			Sticker number, 1..8.
 *
 * \details	Port of FUN_1000_7367 (1000:728b..7340): a hand (resource
 * 			0x4c2) moves across the door 8 columns at a time, 2 frames
 * 			each, and the new door shows behind it, with a looping
 * 			sound effect (StickerSound).
 *
 ******************************************************************************/
void cars_applySticker(
	int16_t sticker
) {
	int16_t			model = exe_readS16(STATE_GARAGE_MODEL);
	const uint8_t * pOffsets =
		exe_data((uint16_t)(STATE_MODEL_OFFSETS + model * CARS_OFFSETS_SIZE));
	int		bodyWidth = cars__bodyWidth;
	int		bodyHeight = cars__bodyHeight;
	int		doorLeft = pOffsets[10];
	int		doorTop = pOffsets[11];
	int16_t carLeft = exe_readS16(STATE_CAR_LEFT);
	int16_t carTop = exe_readS16(STATE_CAR_TOP);
	int16_t visibleRows = exe_readS16(STATE_CAR_HEIGHT);
	int16_t screenLeft = (int16_t)(carLeft + doorLeft);
	int16_t screenTop = (int16_t)(carTop + doorTop);

	cars__composeDoor(sticker, bodyWidth, bodyHeight, doorLeft, doorTop);
	effects_sticker(1);

	for (int step = 0; step < CARS_DOOR_WIDTH; step += CARS_WIPE_STEP) {
		plat_selectPage(globals_workSurface);

		uint8_t * pPixels = plat_framebuffer();

		for (int row = 0; row < CARS_DOOR_HEIGHT; row++) {
			int bodyRow = doorTop + row;
			int screenRow = screenTop + row;

			if (bodyRow >= visibleRows || screenRow >= PLAT_HEIGHT) {
				break;
			}

			for (int column = step; column < step + CARS_WIPE_STEP; column++) {
				int bodyColumn = doorLeft + column;
				int screenColumn = screenLeft + column;

				if (bodyColumn >= bodyWidth || screenColumn >= PLAT_WIDTH) {
					break;
				}

				uint8_t pixel =
					cars__frame0Pixels[bodyRow * bodyWidth + bodyColumn];

				if (pixel != 0) {
					pPixels[screenRow * PLAT_WIDTH + screenColumn] = pixel;
				}
			}
		}

		screen_copyToDisplay(
			screenLeft,
			screenTop,
			(int16_t)(screenLeft + CARS_DOOR_WIDTH - 1),
			(int16_t)(screenTop + CARS_DOOR_HEIGHT - 1)
		);

		if (step < CARS_DOOR_WIDTH - CARS_WIPE_STEP) {
			screen_drawSprite(
				globals_displaySurface,
				CARS_HAND_RESOURCE,
				(int16_t)(screenLeft + step + CARS_WIPE_STEP),
				screenTop
			);
			cursor_surfaceCopied(
				screenLeft,
				screenTop,
				CARS_DOOR_WIDTH + 2 * CARS_WIPE_STEP,
				CARS_DOOR_HEIGHT
			);
		}

		uint32_t startTick = globals_ticks;

		while (globals_ticks < startTick + CARS_WIPE_TICKS) {
			dos_service();
		}
	}

	cars__showFrame0(
		carLeft,
		carTop,
		(int16_t)bodyWidth,
		visibleRows,
		(int16_t)(carTop + bodyHeight - 1)
	);
	effects_sticker(0);
}

/** ****************************************************************************
 *
 * \brief	Save the street strip and build the foreground for the waitress.
 *
 * \details	Port of FUN_1f38_9e5d and the mask setup of FUN_1f38_84be for
 * 			resource 0x474. The clean 256 by 41 strip at (0, 0x44) on the
 * 			work surface goes to (0, 0) of CARS_BACKGROUND_PAGE (whose
 * 			car strip starts at row 0x5e), and its pixels where mask 0x474
 * 			is 0 become cars__waitressFront.
 *
 ******************************************************************************/
static void cars__saveWaitress(void) {
	plat_blit(
		globals_workSurface,
		0,
		CARS_WAITRESS_TOP,
		CARS_BACKGROUND_PAGE,
		0,
		0,
		CARS_WAITRESS_STRIP,
		CARS_WAITRESS_HEIGHT
	);

	if (cars__waitressFront != 0) {
		plat_imageDestroy(cars__waitressFront);
		cars__waitressFront = 0;
	}

	int					 index = lib_indexFromGameId(CARS_WAITRESS_MASK);
	const lib_Resource * pMask = lib_get(index);

	if (pMask == NULL) {
		return;
	}

	int		  maskWidth = pMask->width;
	int		  maskHeight = pMask->height;
	uint8_t * pMaskPixels = calloc((size_t)maskWidth * (size_t)maskHeight, 1);
	uint8_t * pPixels =
		malloc((size_t)CARS_WAITRESS_STRIP * CARS_WAITRESS_HEIGHT);

	if (pMaskPixels != NULL && pPixels != NULL &&
		lib_decodePixels(index, pMaskPixels)) {
		plat_readPixels(
			globals_workSurface,
			0,
			CARS_WAITRESS_TOP,
			CARS_WAITRESS_STRIP,
			CARS_WAITRESS_HEIGHT,
			pPixels
		);

		for (int row = 0; row < CARS_WAITRESS_HEIGHT; row++) {
			for (int column = 0; column < CARS_WAITRESS_STRIP; column++) {
				bool hidden = row < maskHeight && column < maskWidth &&
					pMaskPixels[row * maskWidth + column] == 0;

				if (!hidden) {
					pPixels[row * CARS_WAITRESS_STRIP + column] =
						PLAT_TRANSPARENT;
				}
			}
		}

		cars__waitressFront = plat_imageCreateOverlay(
			CARS_WAITRESS_NAME,
			CARS_WAITRESS_STRIP,
			CARS_WAITRESS_HEIGHT,
			pPixels
		);
	}

	free(pMaskPixels);
	free(pPixels);
}

/** ****************************************************************************
 *
 * \brief	Skate the drive-in waitress out to the car or back in.
 *
 * \param	leaving			0: coming out (resource 0x476, phases at
 * 							DS:0x5a86); 1: going back (resource 0x475,
 * 							phases at DS:0x5a96).
 * \param	here			Not 0 when an opponent's car stands on the
 * 							street in front of the strip.
 *
 * \details	Port of FUN_1f38_99d9 (with FUN_1f38_975b, FUN_1f38_985c and
 * 			FUN_1f38_990f). Across 84 steps of 2 ticks each, the frame
 * 			from DS:0x5a32 (1-based) is drawn over the saved 256 by 41
 * 			strip at row 0x44, behind mask 0x474, the bars of place 6 and
 * 			any standing opponent car. At the end the overlap with the
 * 			70-row car strip in CARS_BACKGROUND_PAGE at (200, 0x5e) is
 * 			updated (FUN_1f38_975b). The original shifts each frame to
 * 			its column (FUN_1f38_acbf), so the port draws it at the
 * 			exact column.
 *
 ******************************************************************************/
void cars_waitress(
	int16_t leaving,
	int16_t here
) {
	uint16_t sheetAt = (uint16_t)(CARS_WAITRESS_SHEETS + leaving * 6);
	int16_t	 sheetId = exe_readS16(sheetAt);
	int16_t	 sheetColumns = exe_readS16((uint16_t)(sheetAt + 2));
	int16_t	 frameCount = exe_readS16((uint16_t)(sheetAt + 4));

	const lib_Resource * pSheet = lib_get(lib_indexFromGameId(sheetId));

	if (pSheet == NULL || sheetColumns <= 0 || frameCount <= 0) {
		return;
	}

	int16_t sheetRows =
		(int16_t)((frameCount + sheetColumns - 1) / sheetColumns);
	int16_t		frameWidth = (int16_t)(pSheet->width / sheetColumns);
	int16_t		frameHeight = (int16_t)(pSheet->height / sheetRows);
	uint16_t	phasesAt = (uint16_t)(CARS_WAITRESS_PHASES + leaving * 16);
	cars__Phase phases[4];

	for (int index = 0; index < 4; index++) {
		uint16_t entryAt = (uint16_t)(phasesAt + index * 4);

		phases[index].count = exe_readS16(entryAt);
		phases[index].step = (int8_t)*exe_data((uint16_t)(entryAt + 2));
	}

	int16_t carLeft = exe_readS16(STATE_CAR_LEFT);
	int16_t carTop = exe_readS16(STATE_CAR_TOP);
	int16_t visibleRows = exe_readS16(STATE_CAR_HEIGHT);
	int		carFrame = ((uint16_t)carLeft >> 1) & 3;
	int16_t overlapRows = (int16_t)(CARS_WAITRESS_BOTTOM - carTop + 1);
	int16_t carWidth = cars__bodyWidth;

	if (overlapRows > visibleRows) {
		overlapRows = visibleRows;
	}

	if (carLeft + carWidth > CARS_WAITRESS_STRIP) {
		carWidth = (int16_t)(CARS_WAITRESS_STRIP - carLeft);
	}

	exe_writeU16(CARS_TOP_CROP, 0);

	int16_t column = leaving == 0 ? CARS_WAITRESS_START_X
								  : CARS_WAITRESS_REST_X;
	int		phaseIndex = 0;

	while (phaseIndex < 3 && phases[phaseIndex].count == 0) {
		phaseIndex++;
	}

	for (int step = 0; step < CARS_WAITRESS_STEPS; step++) {
		int16_t frame = (int16_t)((int8_t)*exe_data(
									  (uint16_t)(CARS_WAITRESS_FRAMES + step)
								  ) -
								  1);

		plat_blit(
			CARS_BACKGROUND_PAGE,
			0,
			0,
			globals_displaySurface,
			0,
			CARS_WAITRESS_TOP,
			CARS_WAITRESS_STRIP,
			CARS_WAITRESS_HEIGHT
		);
		screen_drawSpritePartExact(
			globals_displaySurface,
			sheetId,
			(int16_t)((frame % sheetColumns) * frameWidth),
			(int16_t)((frame / sheetColumns) * frameHeight),
			frameWidth,
			frameHeight,
			column,
			CARS_WAITRESS_TOP,
			false
		);
		plat_selectPage(globals_displaySurface);

		if (cars__waitressFront != 0) {
			plat_drawImage(cars__waitressFront, 0, CARS_WAITRESS_TOP);
		}

		if (globals_place == CARS_BARRED_PLACE) {
			int16_t done = 0;

			for (int gap = 0; gap <= CARS_GAP_COUNT; gap++) {
				uint16_t gapAt = (uint16_t)(CARS_GAPS + gap * 4);
				int16_t	 left = gap < CARS_GAP_COUNT ? exe_readS16(gapAt)
													 : CARS_WAITRESS_STRIP;

				if (left > done && done < CARS_WAITRESS_STRIP) {
					int16_t right = left > CARS_WAITRESS_STRIP
						? CARS_WAITRESS_STRIP
						: left;

					plat_blit(
						CARS_BACKGROUND_PAGE,
						done,
						0,
						globals_displaySurface,
						done,
						CARS_WAITRESS_TOP,
						(int16_t)(right - done),
						CARS_WAITRESS_HEIGHT
					);
				}

				if (gap < CARS_GAP_COUNT) {
					done = (int16_t)(left + exe_readS16((uint16_t)(gapAt + 2)));
				}
			}
		}

		if (step == CARS_WAITRESS_STEPS - 1) {
			int16_t stripTop =
				(int16_t)(CARS_WAITRESS_CAR_Y - (CARS_STRIP_HEIGHT - 1));

			plat_copyRect(
				globals_displaySurface,
				CARS_BACKGROUND_PAGE,
				CARS_WAITRESS_REST_X,
				stripTop,
				CARS_WAITRESS_PATCH_W,
				(int16_t)(CARS_WAITRESS_BOTTOM - stripTop + 1)
			);
		}

		if (here != 0 && overlapRows > 0 && carWidth > 0 &&
			cars__frames[carFrame] != 0) {
			plat_selectPage(globals_displaySurface);
			plat_drawImagePart(
				cars__frames[carFrame],
				0,
				0,
				carWidth,
				overlapRows,
				carLeft,
				carTop,
				NULL
			);
		}

		cursor_surfaceCopied(
			0,
			CARS_WAITRESS_TOP,
			CARS_WAITRESS_STRIP,
			CARS_WAITRESS_HEIGHT
		);

		uint32_t targetTick = globals_ticks + CARS_WAITRESS_TICKS;

		while (globals_ticks < targetTick) {
			dos_service();
		}

		phases[phaseIndex].count--;

		while (phaseIndex < 3 && phases[phaseIndex].count == 0) {
			phaseIndex++;
		}

		if (phases[phaseIndex].count != -1) {
			column = (int16_t)(column + phases[phaseIndex].step);
		}
	}

	plat_copyRect(
		globals_displaySurface,
		globals_workSurface,
		0,
		CARS_WAITRESS_TOP,
		CARS_WAITRESS_STRIP,
		CARS_WAITRESS_HEIGHT
	);
	plat_selectPage(globals_workSurface);
}

/** ****************************************************************************
 *
 * \brief	The parts table entry of the part in a car slot.
 *
 * \param	car				DS offset of the car record.
 * \param	slot			Offset of the slot in the car record.
 *
 * \return	DS offset of the parts table entry, or 0 for an empty slot.
 *
 ******************************************************************************/
static uint16_t cars__partEntry(
	uint16_t car,
	int		 slot
) {
	uint16_t part = exe_readU16((uint16_t)(car + slot));

	if (part == 0) {
		return 0;
	}

	int16_t index = exe_readS16((uint16_t)(part + STATE_PART_VALUE));

	return (uint16_t)(STATE_PARTS + index * 8);
}

/** ****************************************************************************
 *
 * \brief	Tell whether a car has all it needs to run.
 *
 * \param	car					DS offset of the car record.
 * \param	checkRunningGear	Not 0: also the transmission, the tires and
 * 								their bolts.
 *
 * \return	1 when complete, else 0.
 *
 * \details	Port of FUN_1000_7ee6: an engine and a manifold, the number
 * 			of carburetors the manifold takes, and all their bolts tight
 * 			(wear bytes 3).
 *
 ******************************************************************************/
int16_t cars_isComplete(
	uint16_t car,
	int16_t	 checkRunningGear
) {
	const uint8_t * pCar = exe_data(car);

	if (exe_readU16((uint16_t)(car + STATE_CAR_ENGINE)) == 0 ||
		(int8_t)pCar[STATE_CAR_UNKNOWN_1F] < 1 ||
		exe_readU16((uint16_t)(car + STATE_CAR_INTAKE)) == 0) {
		return 0;
	}

	uint16_t intake = cars__partEntry(car, STATE_CAR_INTAKE);
	int8_t	 setup =
		(int8_t)*exe_data((uint16_t)(intake + STATE_PARTS_SUBTYPE));
	int16_t carbs = exe_readS16((uint16_t)(CARS_CARB_COUNTS + setup * 2));

	for (int carb = 0; carb < carbs; carb++) {
		if (exe_readU16((uint16_t)(car + STATE_CAR_CARBS + carb * 2)) == 0) {
			return 0;
		}
	}

	for (int bolt = carbs * 2 + 2; bolt >= 0; bolt--) {
		if (pCar[0x16 + bolt] != CARS_CONDITION) {
			return 0;
		}
	}

	if (checkRunningGear == 0) {
		return 1;
	}

	if (exe_readU16((uint16_t)(car + STATE_CAR_GEARBOX)) == 0 ||
		exe_readU16((uint16_t)(car + STATE_CAR_TIRES)) == 0) {
		return 0;
	}

	for (int bolt = 0; bolt < 2; bolt++) {
		if (pCar[STATE_CAR_WEAR + bolt] != CARS_CONDITION) {
			return 0;
		}
	}

	return 1;
}

/** ****************************************************************************
 *
 * \brief	The parts of a car and how fast they make it.
 *
 * \param	model			-1 for the car in use; otherwise a model, as it
 * 							comes (the parts its model record lists, no
 * 							body changes, tuning 0).
 * \param	pSetup			Receives the result.
 *
 * \details	The 8087 code FUN_1000_e218 and FUN_1000_dcbe share
 * 			(constants are doubles at DS:0x6988..0x6a00), and the car
 * 			record FUN_1000_e218 and FUN_1000_e0e2 make up for a model.
 * 			The sum of 0.09 for a chopped
 * 			roof, 0.05 without front bumper and 0.01 without rear bumper
 * 			(also when the model has none to take off), 0.1 * weight / 88
 * 			(DS:0x556e by class), 0.15 * carburetors with manifold / 100
 * 			(DS:0x552e), 0.25 * engine / 100 (DS:0x554c), 0.15 *
 * 			transmission / 100 (DS:0x5552) and 0.2 * (1 - |tuning / 8|)
 * 			(car +0x20) is stored as a qword real; times (10000 - engine
 * 			wear) / 10000, stored again; times 0.8 for the first
 * 			transmission (else 1), times 0.3, plus 0.7 (the factor,
 * 			stored as a qword real), times 130 (the speed, stored as a
 * 			qword real; 130 when above).
 * 			For a model the original leaves the body bits (+0x24) of its
 * 			record on the stack unset; the port takes 0. Its engine gets
 * 			the kind word of carburetors (DS:0x496a) as wear, as in the
 * 			original. An empty part slot counts as subtype 0 (the
 * 			original reads through a null pointer).
 *
 ******************************************************************************/
void cars_measure(
	int16_t		 model,
	cars_Setup * pSetup
) {
	uint16_t mods = 0;
	int16_t	 tuning = 0;
	int16_t	 engineWear;
	int		 gearbox;
	int		 engine;
	int		 intake;
	int		 carb;
	int		 tires;

	if (model < 0) {
		uint16_t car = exe_readU16(STATE_CAR);
		uint16_t engineRecord =
			exe_readU16((uint16_t)(car + STATE_CAR_ENGINE));

		model = exe_readS16((uint16_t)(car + STATE_CAR_MODEL));
		mods = exe_readU16((uint16_t)(car + STATE_CAR_UNKNOWN_24));
		tuning = (int8_t)*exe_data((uint16_t)(car + STATE_CAR_UNKNOWN_20));
		gearbox = cars__subtype(cars__partEntry(car, STATE_CAR_GEARBOX));
		engine = cars__subtype(cars__partEntry(car, STATE_CAR_ENGINE));
		intake = cars__subtype(cars__partEntry(car, STATE_CAR_INTAKE));
		carb = cars__subtype(cars__partEntry(car, STATE_CAR_CARBS));
		tires = cars__subtype(cars__partEntry(car, STATE_CAR_TIRES));
		engineWear = engineRecord == 0
			? 0
			: exe_readS16((uint16_t)(engineRecord + STATE_PART_KIND));
	} else {
		uint16_t modelAt = (uint16_t)(STATE_MODELS + model * STATE_MODEL_SIZE);
		uint16_t fitted = exe_readU16((uint16_t)(modelAt + STATE_MODEL_FITTED));
		int		 make = (fitted & 0xf) >> 1;
		int		 gearboxType = (fitted >> 6) & 3;
		int		 extra = gearboxType == 3 ? 1 : 0;
		int		 gearboxPart =
			make + extra + gearboxType * 3 + CARS_FIRST_GEARBOX;
		int enginePart = ((fitted >> 8) & 7) * 3 + make;
		int intakePart = ((fitted >> 11) & 7) * 3 + make + CARS_FIRST_INTAKE;
		int carbPart = (fitted >> 14) + CARS_FIRST_CARB;
		int tiresPart = ((fitted >> 4) & 3) + CARS_FIRST_TIRES;

		gearbox = cars__subtype((uint16_t)(STATE_PARTS + gearboxPart * 8));
		engine = cars__subtype((uint16_t)(STATE_PARTS + enginePart * 8));
		intake = cars__subtype((uint16_t)(STATE_PARTS + intakePart * 8));
		carb = cars__subtype((uint16_t)(STATE_PARTS + carbPart * 8));
		tires = cars__subtype((uint16_t)(STATE_PARTS + tiresPart * 8));
		engineWear = exe_readS16(CARS_SPEED_STOCK_WEAR);
	}

	uint16_t partsAt = (uint16_t)(STATE_MODEL_PARTS + model * STATE_MODEL_SIZE);
	uint16_t modelAt = (uint16_t)(STATE_MODELS + model * STATE_MODEL_SIZE);
	uint8_t	 weightClass = *exe_data((uint16_t)(modelAt + STATE_MODEL_CLASS));
	int		 roof = (mods & CARS_MOD_ROOF) != 0 ||
		exe_readS16((uint16_t)(partsAt + STATE_PARTS_ROOF)) == 0;
	int front = (mods & CARS_MOD_FRONT) != 0 ||
		exe_readS16((uint16_t)(partsAt + STATE_PARTS_FRONT)) == 0;
	int rear = (mods & CARS_MOD_REAR) != 0 ||
		exe_readS16((uint16_t)(partsAt + STATE_PARTS_REAR)) == 0;
	fpu_Real hundred = cars__constant(CARS_FPU_HUNDRED);
	fpu_Real sum = fpu_mul(fpu_fromInt(roof), cars__constant(CARS_FPU_ROOF));

	sum = fpu_add(
		sum,
		fpu_mul(fpu_fromInt(front), cars__constant(CARS_FPU_FRONT))
	);
	sum = fpu_add(
		sum,
		fpu_mul(fpu_fromInt(rear), cars__constant(CARS_FPU_REAR))
	);
	sum = fpu_add(
		sum,
		cars__speedTerm(
			(uint16_t)(CARS_SPEED_WEIGHT + weightClass * 2),
			CARS_FPU_WEIGHT,
			cars__constant(CARS_FPU_WEIGHT_SCALE)
		)
	);
	sum = fpu_add(
		sum,
		cars__speedTerm(
			(uint16_t)(CARS_SPEED_CARBS + carb * 10 + intake * 2),
			CARS_FPU_CARBS,
			hundred
		)
	);
	sum = fpu_add(
		sum,
		cars__speedTerm(
			(uint16_t)(CARS_SPEED_ENGINE + engine * 2),
			CARS_FPU_ENGINE,
			hundred
		)
	);
	sum = fpu_add(
		sum,
		cars__speedTerm(
			(uint16_t)(CARS_SPEED_GEARBOX + gearbox * 2),
			CARS_FPU_GEARBOX,
			hundred
		)
	);

	fpu_Real detuned = fpu_abs(
		fpu_div(fpu_fromInt(tuning), cars__constant(CARS_FPU_TUNING_SCALE))
	);
	fpu_Real balance = fpu_mul(
		fpu_sub(cars__constant(CARS_FPU_ONE), detuned),
		cars__constant(CARS_FPU_TUNING)
	);

	sum = fpu_toDouble(fpu_add(sum, balance));

	fpu_Real wornOut = cars__constant(CARS_FPU_WORN_OUT);
	fpu_Real condition =
		fpu_div(fpu_sub(wornOut, fpu_fromInt(engineWear)), wornOut);

	sum = fpu_toDouble(fpu_mul(condition, sum));

	fpu_Real factor = cars__constant(
		gearbox == 0 ? CARS_FPU_FIRST_GEARBOX : CARS_FPU_ONE
	);

	factor = fpu_mul(fpu_mul(factor, sum), cars__constant(CARS_FPU_SPREAD));
	factor = fpu_add(factor, cars__constant(CARS_FPU_BASE));

	fpu_Real topSpeed = cars__constant(CARS_FPU_TOP_SPEED);
	fpu_Real speed = fpu_mul(factor, topSpeed);
	fpu_Real stored = fpu_toDouble(speed);

	if (fpu_compare(speed, topSpeed) > 0) {
		stored = topSpeed;
	}

	pSetup->model = model;
	pSetup->gearbox = gearbox;
	pSetup->engine = engine;
	pSetup->tires = tires;
	pSetup->factor = fpu_toDouble(factor);
	pSetup->speed = stored;
}

/** ****************************************************************************
 *
 * \brief	The top speed of a car.
 *
 * \param	model			-1 for the car in use; otherwise a model, as it
 * 							comes.
 *
 * \return	Speed in mph, at most 130.
 *
 * \details	Port of FUN_1000_e218 (see cars_measure).
 *
 ******************************************************************************/
int16_t cars_maxSpeed(
	int16_t model
) {
	cars_Setup setup;

	cars_measure(model, &setup);

	return (int16_t)fpu_toLong(setup.speed);
}

/** ****************************************************************************
 *
 * \brief	A floating point constant of Sr.exe.
 *
 * \param	offset			DS offset of the 8-byte real.
 *
 * \return	Its value.
 *
 ******************************************************************************/
static fpu_Real cars__constant(
	uint16_t offset
) {
	return fpu_fromDouble(exe_data(offset));
}

/** ****************************************************************************
 *
 * \brief	The subtype of a part in the parts table.
 *
 * \param	entry			DS offset of the parts table entry, or 0.
 *
 * \return	Its subtype byte (+2), or 0 for no entry.
 *
 ******************************************************************************/
static int cars__subtype(
	uint16_t entry
) {
	if (entry == 0) {
		return 0;
	}

	return (int8_t)*exe_data((uint16_t)(entry + STATE_PARTS_SUBTYPE));
}

/** ****************************************************************************
 *
 * \brief	One table-driven term of the top speed.
 *
 * \param	tableAt			DS offset of the s16 table value.
 * \param	weightAt		DS offset of the real it is multiplied by.
 * \param	scale			Number it is then divided by.
 *
 * \return	value * weight / scale, as FILD, FMUL, FDIV compute it.
 *
 ******************************************************************************/
static fpu_Real cars__speedTerm(
	uint16_t tableAt,
	uint16_t weightAt,
	fpu_Real scale
) {
	fpu_Real value = fpu_fromInt(exe_readS16(tableAt));

	return fpu_div(fpu_mul(value, cars__constant(weightAt)), scale);
}

/** ****************************************************************************
 *
 * \brief	Take a part out of the spare parts.
 *
 * \param	part			DS offset of a part record in the list
 * 							STATE_SPARE_PARTS.
 *
 * \return	part, no longer in the list (its link is left as it was).
 *
 * \details	Port of FUN_1000_77b7.
 *
 ******************************************************************************/
uint16_t cars_takeSparePart(
	uint16_t part
) {
	uint16_t previous = 0;
	uint16_t current = exe_readU16(STATE_SPARE_PARTS);

	while (current != 0 && current != part) {
		previous = current;
		current = exe_readU16((uint16_t)(current + STATE_PART_NEXT));
	}

	uint16_t next = exe_readU16((uint16_t)(current + STATE_PART_NEXT));

	if (previous == 0) {
		exe_writeU16(STATE_SPARE_PARTS, next);
	} else {
		exe_writeU16((uint16_t)(previous + STATE_PART_NEXT), next);
	}

	return current;
}

/** ****************************************************************************
 *
 * \brief	Fit a part into a slot of the car in use.
 *
 * \param	part			DS offset of the part record (not in a list).
 * \param	slotAt			DS offset of the slot word in the car record.
 *
 * \details	Port of FUN_1000_7741. A part in the slot goes to the spare
 * 			parts. The car's price (+0) loses the base price
 * 			(cars_partBasePrice) of the old part and gains the new one's.
 *
 ******************************************************************************/
void cars_fitPart(
	uint16_t part,
	uint16_t slotAt
) {
	uint16_t car = exe_readU16(STATE_CAR);
	uint16_t old = exe_readU16(slotAt);

	if (old != 0) {
		exe_writeU16(
			(uint16_t)(old + STATE_PART_NEXT),
			exe_readU16(STATE_SPARE_PARTS)
		);
		exe_writeU16(STATE_SPARE_PARTS, old);
		exe_writeU16(
			(uint16_t)(car + STATE_CAR_PRICE),
			(uint16_t)(exe_readU16(car) - cars_partBasePrice(old))
		);
	}

	exe_writeU16(slotAt, part);
	exe_writeU16((uint16_t)(part + STATE_PART_NEXT), 0);
	exe_writeU16(
		(uint16_t)(car + STATE_CAR_PRICE),
		(uint16_t)(exe_readU16(car) + cars_partBasePrice(part))
	);
}

/** ****************************************************************************
 *
 * \brief	Take the part out of a slot of the car in use.
 *
 * \param	slotAt			DS offset of the slot word in the car record; it
 * 							must hold a part.
 *
 * \details	Port of FUN_1000_7789: the part goes to the spare parts and
 * 			the car's price (+0) loses its base price.
 *
 ******************************************************************************/
void cars_removePart(
	uint16_t slotAt
) {
	uint16_t car = exe_readU16(STATE_CAR);
	uint16_t part = exe_readU16(slotAt);

	exe_writeU16(
		(uint16_t)(part + STATE_PART_NEXT),
		exe_readU16(STATE_SPARE_PARTS)
	);
	exe_writeU16(STATE_SPARE_PARTS, part);
	exe_writeU16(
		(uint16_t)(car + STATE_CAR_PRICE),
		(uint16_t)(exe_readU16(car) - cars_partBasePrice(part))
	);
	exe_writeU16(slotAt, 0);
}

/** ****************************************************************************
 *
 * \brief	Build the car for the tire change: without tires and shadow,
 * 			and its shadow alone.
 *
 * \param	model			Car model.
 * \param	flags			Drawing flags (cars_drawFlags).
 * \param	sticker			Door sticker number (1..8), or 0 for none.
 * \param	pCar			Output: image of the car without tires.
 * \param	pShadow			Output: image of its shadow, same size.
 * \param	pWidth			Output: width of both images.
 * \param	pHeight			Output: height of both images.
 *
 * \details	Stands in for the start of TireChangeAnimation (1f38:be93):
 * 			BuildCarSprites without tires and shadow, then the shadow
 * 			(resource 0x49d) at the car's leftmost and rightmost columns
 * 			(FindCarShadowBounds), which the original draws on the
 * 			background. The caller destroys both images.
 *
 ******************************************************************************/
void cars_buildForTires(
	int16_t			   model,
	uint8_t			   flags,
	int16_t			   sticker,
	plat_ImageHandle * pCar,
	plat_ImageHandle * pShadow,
	int16_t *		   pWidth,
	int16_t *		   pHeight
) {
	*pCar = 0;
	*pShadow = 0;
	*pWidth = 0;
	*pHeight = 0;
	cars__build(model, flags, sticker, 0, 0, false, 1, pCar, pWidth, pHeight);

	if (*pCar == 0) {
		return;
	}

	int width = *pWidth;
	int height = *pHeight;
	int leftmost;
	int rightmost;
	int shadowWidth;
	int shadowHeight;

	cars__findBounds(cars__framePixels, width, height, &leftmost, &rightmost);
	memset(cars__framePixels, 0, (size_t)width * (size_t)height);

	if (cars__decodeResource(
			CARS_SHADOW_RESOURCE,
			cars__partPixels,
			&shadowWidth,
			&shadowHeight
		)) {
		cars__blitBehind(
			cars__framePixels,
			width,
			height,
			cars__partPixels,
			shadowWidth,
			shadowHeight,
			leftmost,
			height - shadowHeight
		);
		cars__blitBehind(
			cars__framePixels,
			width,
			height,
			cars__partPixels,
			shadowWidth,
			shadowHeight,
			rightmost - shadowWidth + 1,
			height - shadowHeight
		);
	}

	*pShadow = plat_imageCreate(NULL, width, height, cars__framePixels, true);
}

/** ****************************************************************************
 *
 * \brief	Build a car with one set of tires and no shadow.
 *
 * \param	model			Car model.
 * \param	flags			Drawing flags (cars_drawFlags).
 * \param	sticker			Door sticker number (1..8), or 0 for none.
 * \param	tireTable		DS offset of the 4 tire sprite IDs.
 *
 * \return	The image (the caller destroys it), or 0.
 *
 * \details	Stands in for TireChangeAnimation's car sprite with the
 * 			tires put behind it (BlitCarBehind, 1f38:b0f2, at the tire
 * 			offsets of the model, frame 0).
 *
 ******************************************************************************/
plat_ImageHandle cars_buildWithTires(
	int16_t	 model,
	uint8_t	 flags,
	int16_t	 sticker,
	uint16_t tireTable
) {
	plat_ImageHandle image = 0;
	int16_t			 width;
	int16_t			 height;

	cars__build(
		model,
		flags,
		sticker,
		0,
		tireTable,
		false,
		1,
		&image,
		&width,
		&height
	);

	return image;
}

/** ****************************************************************************
 *
 * \brief	Make the image of a tire off the car.
 *
 * \param	tireId			Resource of the tire frame.
 * \param	well			true: put the wheel well (resource 0x4d2)
 * 							behind it, bottom aligned.
 * \param	pWidth			Output: width of the image.
 * \param	pHeight			Output: height of the image.
 *
 * \return	The image (the caller destroys it), or 0.
 *
 * \details	Stands in for WheelSwapAnimation (1f38:b82c) loading a tire
 * 			frame and drawing the well behind it with BlitCarBehind.
 * 			The original shifts each frame within its byte column; the
 * 			port draws at the exact column instead.
 *
 ******************************************************************************/
plat_ImageHandle cars_tireImage(
	int16_t	  tireId,
	bool	  well,
	int16_t * pWidth,
	int16_t * pHeight
) {
	int width;
	int height;
	int wellWidth;
	int wellHeight;

	*pWidth = 0;
	*pHeight = 0;

	if (!cars__decodeResource(tireId, cars__framePixels, &width, &height)) {
		return 0;
	}

	if (well &&
		cars__decodeResource(
			CARS_WELL_RESOURCE,
			cars__partPixels,
			&wellWidth,
			&wellHeight
		)) {
		cars__blitBehind(
			cars__framePixels,
			width,
			height,
			cars__partPixels,
			wellWidth,
			wellHeight,
			0,
			height - wellHeight
		);
	}

	*pWidth = (int16_t)width;
	*pHeight = (int16_t)height;

	return plat_imageCreate(NULL, width, height, cars__framePixels, true);
}
