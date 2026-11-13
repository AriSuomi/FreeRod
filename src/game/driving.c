/** ****************************************************************************
 *
 * \file	driving.c
 *
 * \brief	Driving: the records of the two cars and what is set before a
 * 			drive.
 *
 * \details	Ports of FUN_1000_dcbe and FUN_1000_e0e2 (a car's driving
 * 			record from its parts), FUN_1000_cdaf (the start of a drive)
 * 			FUN_1000_cf5c (the opponent's start), FUN_1000_8e2d (a drive
 * 			from start to end; the drive itself is drive.c) and
 * 			FUN_1000_8d9c (the palette entries of a race).
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/driving.h"

#include "dos/fpu.h"
#include "game/cars.h"
#include "game/drive.h"
#include "game/exe.h"
#include "game/globals.h"
#include "game/random.h"
#include "game/state.h"
#include "game/video.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

/*
 * Tables of Sr.exe (words).
 */
#define DRIVING_TIRE_TABLE	 0x54b2 /**< DS: by tires (10) and wear / 2000. */
#define DRIVING_GEAR_SHARES	 0x54dc /**< DS: % of top speed, by gear. */
#define DRIVING_PULLS		 0x5504 /**< DS: pull by engine (10) and gear. */
#define DRIVING_ENGINE_REVS	 0x5522 /**< DS: 2 revs by engine (4 bytes). */
#define DRIVING_TOP_REVS_AT	 0x555a /**< DS: top revs by engine. */
#define DRIVING_GEARS_AT	 0x5560 /**< DS: gears by transmission. */
#define DRIVING_TIRE_GRIP	 0x5568 /**< DS: by tires. */
#define DRIVING_TABLE_ROW	 10		/**< Bytes of a row of these tables. */
#define DRIVING_WEAR_STEP	 2000	/**< Tire wear per table column. */
#define DRIVING_WEAR_RISKY	 8000	/**< Tire wear that can blow them. */
#define DRIVING_WEAR_RANGE	 2000	/**< Random part added to the wear. */
#define DRIVING_WEAR_BLOWN	 9500	/**< Above it the tires will blow. */
#define DRIVING_START		 200	/**< Where a drive starts. */
#define DRIVING_IDLE_REVS	 1500	/**< Revs at the start. */
#define DRIVING_LAST_REVS	 5700	/**< End of the last range of a gear. */
#define DRIVING_SECOND_GEAR	 1		/**< Gear record with stronger pull. */
#define DRIVING_WIDE_TIRES	 2		/**< Tires that triple that pull. */
#define DRIVING_HEAVIEST	 130	/**< Minus the model's weight class. */
#define DRIVING_AUTO_REVS	 0x14b4 /**< DS:0x6cba with an automatic. */
#define DRIVING_SKILL_REVS	 100	/**< Revs per point of the ad's +1. */
#define DRIVING_FAST_MACHINE 4		/**< DS:0x8acc from it: fast machine. */

/*
 * 8-byte reals of Sr.exe.
 */
#define DRIVING_FPU_CRUISE	0x696c /**< DS: real 0.7. */
#define DRIVING_FPU_FAST	0x6974 /**< DS: real 0.8. */
#define DRIVING_FPU_HUNDRED 0x69b8 /**< DS: real 100. */
#define DRIVING_FPU_NO_GEAR 0x69d8 /**< DS: real 0.2. */
#define DRIVING_FPU_ZERO	0x6a08 /**< DS: real 0. */
#define DRIVING_FPU_REVS	0x6a10 /**< DS: real 5700. */
#define DRIVING_FPU_WEIGHT	0x6a18 /**< DS: real 5. */
#define DRIVING_FPU_SCALE	0x6a20 /**< DS: real 50. */

/*
 * Variables in the data segment image. Driving, which is not ported yet,
 * uses them; names say what the code ported so far shows.
 */
#define DRIVING_UNKNOWN_5D6	   0x5d6  /**< s16: cleared before a drive. */
#define DRIVING_UNKNOWN_5D8	   0x5d8  /**< s16: cleared before a drive. */
#define DRIVING_UNKNOWN_5DA	   0x5da  /**< s16: cleared before a drive. */
#define DRIVING_UNKNOWN_53E6   0x53e6 /**< s16: cleared before a drive. */
#define DRIVING_UNKNOWN_54D0   0x54d0 /**< s16: cleared before a drive. */
#define DRIVING_UNKNOWN_54D2   0x54d2 /**< s16: cleared before a drive. */
#define DRIVING_LENGTH		   0x6caa /**< s16: 380 drag race, else 1020. */
#define DRIVING_RIVAL_SPEED	   0x6cac /**< s16: top speed of DS:0x7648. */
#define DRIVING_UNKNOWN_6CAE   0x6cae /**< s16: 10 slow machine, else 16. */
#define DRIVING_RIVAL_FAST	   0x6cb0 /**< s16: 0.8 of that top speed. */
#define DRIVING_RIVAL_CRUISE   0x6cb2 /**< s16: 0.7 of that top speed. */
#define DRIVING_RIVAL_SKILL	   0x6cb4 /**< s16: byte +1 of the ad record. */
#define DRIVING_RIVAL_PLACE	   0x6cb6 /**< s16: where the opponent starts. */
#define DRIVING_RIVAL_RANGE	   0x6cb8 /**< s16: range of its first gear. */
#define DRIVING_RIVAL_REVS	   0x6cba /**< s16: top revs less 100 * skill. */
#define DRIVING_UNKNOWN_70C0   0x70c0 /**< s16: read for the start place. */
#define DRIVING_RIVAL_MODEL	   0x7648 /**< s16: the opponent's car model. */
#define DRIVING_TIRES_BLOWN	   0x787c /**< u8: cleared before a drive. */
#define DRIVING_UNKNOWN_7D2E   0x7d2e /**< s16: read for the start place. */
#define DRIVING_RIVAL_AD	   0x7d84 /**< u16: DS offset of the ad record. */
#define DRIVING_RIVAL_AD_SKILL 1	  /**< Ad record: s8 (unknown; skill?). */
#define DRIVING_UNKNOWN_7E8A   0x7e8a /**< s16: 200 before a race. */
#define DRIVING_UNKNOWN_7E8C   0x7e8c /**< s16: 0 before a race. */
#define DRIVING_UNKNOWN_7E90   0x7e90 /**< s16: where the opponent starts. */
#define DRIVING_KIND		   0x7ec0 /**< s16: 0 drag race, 1 road race. */
#define DRIVING_UNKNOWN_8630   0x8630 /**< s16: -1 before a drive. */
#define DRIVING_UNKNOWN_8228   0x8228 /**< s16: cleared before a drive. */
#define DRIVING_SAVED_COLORS   0x51ac /**< 2 x u8: colors before a race. */
#define DRIVING_PAINT_TABLE	   0x50c2 /**< 2 colors per paint. */
#define DRIVING_NO_COLOR	   0xff	  /**< DS:0x51ac: nothing put aside. */
#define DRIVING_FIRST_ENTRY	   12	  /**< Palette entry of the first color. */
#define DRIVING_SECOND_ENTRY   3	  /**< Palette entry of the second. */
#define DRIVING_SIREN		   0x58dc /**< s16: FUN_1f38_7728 clears it. */
#define DRIVING_UNKNOWN_58DA   0x58da /**< s16: FUN_1f38_7741 clears it. */

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

static fpu_Real driving__constant(
	uint16_t offset
);

static void driving__setGear(
	uint16_t		   drivingAt,
	const cars_Setup * pSetup,
	int				   gear
);

static void driving__copyWord(
	uint16_t drivingAt,
	int		 to,
	int		 from
);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Make the driving record of a car from its parts.
 *
 * \param	model			-1 for the car in use; otherwise a model, as it
 * 							comes.
 * \param	drivingAt		DS offset of the driving record.
 *
 * \details	Port of FUN_1000_dcbe, and of FUN_1000_e0e2 which makes up a
 * 			car record for a model and calls it (see cars_measure for
 * 			the first half, the 8087 code shared with FUN_1000_e218).
 * 			Fills the 5 gear records, then: automatic with the first
 * 			transmission (the record then shifts at the engine's upper
 * 			revs), top revs by engine (DS:0x555a), (130 - weight class)
 * 			* 5 / 50 + the value of the tires (DS:0x5568) at +0x3e, the
 * 			number of gears (DS:0x5560) and gear 1.
 *
 ******************************************************************************/
void driving_prepare(
	int16_t	 model,
	uint16_t drivingAt
) {
	cars_Setup setup;

	cars_measure(model, &setup);

	for (int gear = 0; gear < DRIVING_GEAR_COUNT; gear++) {
		driving__setGear(drivingAt, &setup, gear);
	}

	uint16_t automatic = setup.gearbox == 0;

	exe_writeU16((uint16_t)(drivingAt + DRIVING_SHIFTING), automatic);
	exe_writeU16((uint16_t)(drivingAt + DRIVING_AUTOMATIC), automatic);

	if (automatic != 0) {
		uint16_t revsAt = (uint16_t)(DRIVING_ENGINE_REVS + setup.engine * 4);

		exe_writeU16((uint16_t)(drivingAt + DRIVING_UNKNOWN_28), 0xffff);
		exe_writeU16((uint16_t)(drivingAt + DRIVING_UNKNOWN_26), 0xffff);
		exe_writeU16(
			(uint16_t)(drivingAt + DRIVING_SHIFT_REVS),
			exe_readU16((uint16_t)(revsAt + 2))
		);
	}

	exe_writeU16(
		(uint16_t)(drivingAt + DRIVING_TOP_REVS),
		exe_readU16((uint16_t)(DRIVING_TOP_REVS_AT + setup.engine * 2))
	);

	uint16_t modelAt =
		(uint16_t)(STATE_MODELS + setup.model * STATE_MODEL_SIZE);
	uint8_t weightClass = *exe_data((uint16_t)(modelAt + STATE_MODEL_CLASS));
	int16_t tireGrip =
		exe_readS16((uint16_t)(DRIVING_TIRE_GRIP + setup.tires * 2));
	fpu_Real grip = fpu_fromInt((int16_t)(DRIVING_HEAVIEST - weightClass));

	grip = fpu_mul(grip, driving__constant(DRIVING_FPU_WEIGHT));
	grip = fpu_div(grip, driving__constant(DRIVING_FPU_SCALE));
	grip = fpu_add(grip, fpu_fromInt(tireGrip));
	exe_writeU16(
		(uint16_t)(drivingAt + DRIVING_GRIP),
		(uint16_t)fpu_toLong(grip)
	);
	exe_writeU16(
		(uint16_t)(drivingAt + DRIVING_GEARS),
		exe_readU16((uint16_t)(DRIVING_GEARS_AT + setup.gearbox * 2))
	);
	exe_writeU16((uint16_t)(drivingAt + DRIVING_GEAR_CHANGED), 1);
}

/** ****************************************************************************
 *
 * \brief	Set a car and the variables of driving to the start of a drive.
 *
 * \param	drivingAt		DS offset of the driving record.
 *
 * \details	Port of FUN_1000_cdaf. The cursor position is put aside (if
 * 			none is) and cleared. The record starts at 1500 revs in the
 * 			first range of its first gear record. The drive is 380 long
 * 			for a drag race and 1020 otherwise (DS:0x6caa). The tires of
 * 			the car in use give +0x0c (DS:0x54b2); worn 8000 or more
 * 			they blow when wear + random(2000) is above 9500, somewhere
 * 			in the second half of the drive (+0x0a). The top speed of
 * 			the model DS:0x7648 and 0.7 and 0.8 of it are stored
 * 			(DS:0x6cac, 0x6cb2, 0x6cb0). DS:0x6cae is 10 on a slow
 * 			machine (DS:0x8acc < 4, loops counted in a timer tick by
 * 			FUN_1000_05c6) and 16 otherwise; the port takes 16.
 *
 ******************************************************************************/
void driving_reset(
	uint16_t drivingAt
) {
	if (globals_savedCursorX == 0 && globals_savedCursorY == 0) {
		globals_savedCursorX = globals_cursorX;
		globals_savedCursorY = globals_cursorY;
	}

	globals_lastDirectionY = 0;
	globals_lastDirectionX = 0;
	globals_cursorY = 0;
	globals_cursorX = 0;
	globals_clickKeyFlag = 0;
	exe_writeU16((uint16_t)(drivingAt + DRIVING_WHEEL), 0);
	exe_writeU16((uint16_t)(drivingAt + DRIVING_STEERING), 0);
	exe_writeU16((uint16_t)(drivingAt + DRIVING_RANGE_INDEX), 0);
	exe_writeU16((uint16_t)(drivingAt + DRIVING_GEAR), 0);
	exe_writeU16((uint16_t)(drivingAt + DRIVING_SPEED), 0);
	exe_writeU16((uint16_t)(drivingAt + DRIVING_TIME), 0);
	exe_writeU16((uint16_t)(drivingAt + DRIVING_END_TIME), 0);
	exe_writeU16((uint16_t)(drivingAt + DRIVING_FLAGS), 0);

	if (exe_readU16((uint16_t)(drivingAt + DRIVING_AUTOMATIC)) != 0) {
		/*
		 * The third gear record: its second range ends and its third
		 * starts at the top revs.
		 */
		int thirdGear = DRIVING_GEAR_RECORDS + 2 * DRIVING_GEAR_SIZE;
		int secondRange = thirdGear + DRIVING_RANGE_SIZE;
		int thirdRange = secondRange + DRIVING_RANGE_SIZE;

		driving__copyWord(
			drivingAt,
			thirdRange + DRIVING_RANGE_FROM,
			DRIVING_TOP_REVS
		);
		driving__copyWord(
			drivingAt,
			secondRange + DRIVING_RANGE_TO,
			DRIVING_TOP_REVS
		);
		exe_writeU16((uint16_t)(drivingAt + DRIVING_SHIFTING), 1);
	}

	exe_writeU16((uint16_t)(drivingAt + DRIVING_UNKNOWN_26), 0xffff);
	exe_writeU16((uint16_t)(drivingAt + DRIVING_UNKNOWN_28), 0xffff);
	exe_writeU16((uint16_t)(drivingAt + DRIVING_REVS), DRIVING_IDLE_REVS);
	driving__copyWord(drivingAt, DRIVING_REVS_PER_MPH, DRIVING_UNKNOWN_2A);
	driving__copyWord(
		drivingAt,
		DRIVING_RANGE + DRIVING_RANGE_PULL,
		DRIVING_GEAR_RECORDS + DRIVING_RANGE_PULL
	);
	driving__copyWord(
		drivingAt,
		DRIVING_RANGE + DRIVING_RANGE_PULL2,
		DRIVING_GEAR_RECORDS + DRIVING_RANGE_PULL2
	);
	driving__copyWord(
		drivingAt,
		DRIVING_RANGE + DRIVING_RANGE_TO,
		DRIVING_GEAR_RECORDS + DRIVING_RANGE_TO
	);
	driving__copyWord(
		drivingAt,
		DRIVING_RANGE + DRIVING_RANGE_FROM,
		DRIVING_GEAR_RECORDS + DRIVING_RANGE_FROM
	);
	exe_writeU16(DRIVING_UNKNOWN_5D6, 0);
	exe_writeU16(DRIVING_UNKNOWN_5DA, 0);
	exe_writeU16(DRIVING_UNKNOWN_5D8, 0);
	exe_writeU16(DRIVING_UNKNOWN_8630, 0xffff);

	int16_t length = exe_readS16(DRIVING_KIND) == 0 ? 0x17c : 0x3fc;

	exe_writeU16(DRIVING_LENGTH, (uint16_t)length);

	uint16_t car = exe_readU16(STATE_CAR);
	uint16_t tires = exe_readU16((uint16_t)(car + STATE_CAR_TIRES));
	int16_t	 wear = exe_readS16((uint16_t)(tires + STATE_PART_KIND));
	int16_t	 part = exe_readS16((uint16_t)(tires + STATE_PART_VALUE));
	uint16_t partAt = (uint16_t)(STATE_PARTS + part * 8);
	int8_t	 kind = (int8_t)*exe_data((uint16_t)(partAt + STATE_PARTS_SUBTYPE));
	int		 column = wear / DRIVING_WEAR_STEP;
	int		 wearIndex = column * 2 + kind * DRIVING_TABLE_ROW;
	uint16_t wearAt = (uint16_t)(DRIVING_TIRE_TABLE + wearIndex);
	int16_t	 blowoutAt = 0;

	exe_writeU16(
		(uint16_t)(drivingAt + DRIVING_TIRE_WEAR),
		exe_readU16(wearAt)
	);

	if (wear >= DRIVING_WEAR_RISKY &&
		wear + random_next(DRIVING_WEAR_RANGE) > DRIVING_WEAR_BLOWN) {
		int16_t half = (int16_t)((length - DRIVING_START) >> 1);

		int16_t extra = random_next((int16_t)(half >> 1));

		blowoutAt = (int16_t)(extra + half + DRIVING_START);
	}

	exe_writeU16(
		(uint16_t)(drivingAt + DRIVING_BLOWOUT_AT),
		(uint16_t)blowoutAt
	);
	*exe_data(DRIVING_TIRES_BLOWN) = 0;
	exe_writeU16(DRIVING_UNKNOWN_54D0, 0);

	int16_t	 topSpeed = cars_maxSpeed(exe_readS16(DRIVING_RIVAL_MODEL));
	fpu_Real speed = fpu_fromInt(topSpeed);
	fpu_Real cruise = fpu_mul(speed, driving__constant(DRIVING_FPU_CRUISE));
	fpu_Real fast = fpu_mul(speed, driving__constant(DRIVING_FPU_FAST));

	exe_writeU16(DRIVING_RIVAL_SPEED, (uint16_t)topSpeed);
	exe_writeU16(DRIVING_RIVAL_CRUISE, (uint16_t)fpu_toLong(cruise));
	exe_writeU16(DRIVING_RIVAL_FAST, (uint16_t)fpu_toLong(fast));
	exe_writeU16(
		DRIVING_UNKNOWN_6CAE,
		globals_machineSpeed < DRIVING_FAST_MACHINE ? 10 : 16
	);
	exe_writeU16(DRIVING_UNKNOWN_54D2, 0);
	exe_writeU16(DRIVING_UNKNOWN_53E6, 0);
}

/** ****************************************************************************
 *
 * \brief	Set the opponent's car to the start of a race.
 *
 * \details	Port of FUN_1000_cf5c. Makes the driving record of the
 * 			opponent's model (DS:0x7648) at DS:0x79e2 and resets it. The
 * 			car starts with the revs per mph of its first gear, at 1500
 * 			revs, in the range of the first gear record that holds 1500
 * 			revs (DS:0x6cb8). DS:0x6cba is 0x14b4 with an automatic, else
 * 			the top revs less 100 times the byte +1 of the ad record
 * 			(DS:0x6cb4). The opponent starts at the mean of DS:0x70c0
 * 			and DS:0x7d2e less 200 (DS:0x7e90, DS:0x6cb6). The original
 * 			divides 1500 by the revs per mph without a check; the port
 * 			leaves the speed 0 when they are 0.
 *
 ******************************************************************************/
void driving_setupOpponent(void) {
	uint16_t rival = DRIVING_OPPONENT;
	uint16_t adAt = exe_readU16(DRIVING_RIVAL_AD);

	driving_prepare(exe_readS16(DRIVING_RIVAL_MODEL), rival);

	int16_t skill =
		(int8_t)*exe_data((uint16_t)(adAt + DRIVING_RIVAL_AD_SKILL));

	exe_writeU16(DRIVING_RIVAL_SKILL, (uint16_t)skill);
	driving_reset(rival);
	exe_writeU16((uint16_t)(rival + DRIVING_GEAR), 0);
	driving__copyWord(rival, DRIVING_REVS_PER_MPH, DRIVING_GEAR_REVS);
	exe_writeU16((uint16_t)(rival + DRIVING_REVS), DRIVING_IDLE_REVS);

	int16_t revsPerMph =
		exe_readS16((uint16_t)(rival + DRIVING_REVS_PER_MPH));
	int16_t speed = 0;

	if (revsPerMph != 0) {
		speed = (int16_t)(DRIVING_IDLE_REVS / revsPerMph);
	}

	exe_writeU16((uint16_t)(rival + DRIVING_SPEED), (uint16_t)speed);

	uint16_t range = 1;
	uint16_t rangeAt =
		(uint16_t)(rival + DRIVING_GEAR_RECORDS + DRIVING_RANGE_SIZE);
	int16_t rangeTo = exe_readS16((uint16_t)(rangeAt + DRIVING_RANGE_TO));
	int16_t rangeFrom =
		exe_readS16((uint16_t)(rangeAt + DRIVING_RANGE_FROM));

	if (rangeTo <= DRIVING_IDLE_REVS) {
		range = 2;
		rangeAt += DRIVING_RANGE_SIZE;
	} else if (rangeFrom > DRIVING_IDLE_REVS) {
		range = 0;
		rangeAt -= DRIVING_RANGE_SIZE;
	}

	exe_writeU16(DRIVING_RIVAL_RANGE, range);
	exe_writeU16((uint16_t)(rival + DRIVING_RANGE_INDEX), range);
	exe_writeU16(
		(uint16_t)(rival + DRIVING_RANGE + DRIVING_RANGE_TO),
		exe_readU16((uint16_t)(rangeAt + DRIVING_RANGE_TO))
	);
	exe_writeU16(
		(uint16_t)(rival + DRIVING_RANGE + DRIVING_RANGE_PULL),
		exe_readU16((uint16_t)(rangeAt + DRIVING_RANGE_PULL))
	);

	int16_t revs = DRIVING_AUTO_REVS;

	if (exe_readU16((uint16_t)(rival + DRIVING_SHIFTING)) == 0) {
		int16_t topRevs = exe_readS16((uint16_t)(rival + DRIVING_TOP_REVS));

		revs = (int16_t)(topRevs - DRIVING_SKILL_REVS * skill);
	}

	exe_writeU16(DRIVING_RIVAL_REVS, (uint16_t)revs);
	exe_writeU16(DRIVING_UNKNOWN_7E8A, DRIVING_START);
	exe_writeU16(DRIVING_UNKNOWN_7E8C, 0);

	int16_t first = exe_readS16(DRIVING_UNKNOWN_70C0);
	int16_t sum = (int16_t)(first + exe_readS16(DRIVING_UNKNOWN_7D2E));
	int16_t place = (int16_t)((sum >> 1) - DRIVING_START);

	exe_writeU16(DRIVING_UNKNOWN_7E90, (uint16_t)place);
	exe_writeU16(DRIVING_RIVAL_PLACE, (uint16_t)place);
}

/** ****************************************************************************
 *
 * \brief	Drive: a race, or a cruise to a place.
 *
 * \return	What the last step of the drive returned.
 *
 * \details	Port of FUN_1000_8e2d. Before a race (DS:0x8aca) the palette
 * 			is blanked (video table entry 7). DS:0x7d66 and DS:0x8228
 * 			are cleared, the car's paint is set and, for a race, the
 * 			race colors of the paint DS:0x827c
 * 			(driving_setRaceColors). After the drive (drive_run)
 * 			FUN_1f38_7741(0) and FUN_1f38_7728(0) clear DS:0x58da and
 * 			DS:0x58dc and the cursor goes back where driving_reset took
 * 			it from.
 *
 ******************************************************************************/
int16_t driving_run(void) {
	bool racing = *exe_data(DRIVE_RACING) != 0;

	if (racing) {
		video_blankPalette();
	}

	exe_writeU16(DRIVE_GO, 0);
	exe_writeU16(DRIVING_UNKNOWN_8228, 0);

	int16_t	 racePaint = exe_readS16(STATE_GARAGE_PAINT);
	uint16_t car = exe_readU16(STATE_CAR);

	cars_setPaint((int8_t)*exe_data((uint16_t)(car + STATE_CAR_PAINT)));

	if (racing) {
		driving_setRaceColors(racePaint);
	}

	int16_t result = drive_run();

	exe_writeU16(DRIVING_UNKNOWN_58DA, 0);
	exe_writeU16(DRIVING_SIREN, 0);
	globals_cursorX = globals_savedCursorX;
	globals_cursorY = globals_savedCursorY;
	globals_savedCursorX = 0;
	globals_savedCursorY = 0;

	return result;
}

/** ****************************************************************************
 *
 * \brief	Set the two palette colors of a race, or put back the ones
 * 			from before it.
 *
 * \param	paint			Paint number, 0..5; -1 puts the colors back.
 *
 * \details	Port of FUN_1000_8d9c (EGA and VGA only, as in the
 * 			original). Palette entries 12 and 3 get the pair of standard
 * 			colors of the paint (table DS:0x50c2); the first time the
 * 			colors they had are put aside in DS:0x51ac. They are stored
 * 			in the game palette, and set at once only while the palette
 * 			is shown. With -1 and nothing put aside the original sets
 * 			both entries to 0xff; the port leaves them alone.
 *
 ******************************************************************************/
void driving_setRaceColors(
	int16_t paint
) {
	uint8_t * pSaved = exe_data(DRIVING_SAVED_COLORS);
	uint8_t	  first;
	uint8_t	  second;

	if (paint == -1) {
		if (pSaved[0] == DRIVING_NO_COLOR) {
			return;
		}

		first = pSaved[0];
		second = pSaved[1];
		pSaved[0] = DRIVING_NO_COLOR;
		pSaved[1] = DRIVING_NO_COLOR;
	} else {
		const uint8_t * pPair =
			exe_data((uint16_t)(DRIVING_PAINT_TABLE + paint * 2));

		if (pSaved[0] == DRIVING_NO_COLOR) {
			pSaved[0] = video_gamePalette[DRIVING_FIRST_ENTRY];
			pSaved[1] = video_gamePalette[DRIVING_SECOND_ENTRY];
		}

		first = pPair[0];
		second = pPair[1];
	}

	if (globals_paletteShown != 0) {
		video_setPaletteColor(DRIVING_FIRST_ENTRY, first);
		video_setPaletteColor(DRIVING_SECOND_ENTRY, second);
	}

	video_gamePalette[DRIVING_FIRST_ENTRY] = first;
	video_gamePalette[DRIVING_SECOND_ENTRY] = second;
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
static fpu_Real driving__constant(
	uint16_t offset
) {
	return fpu_fromDouble(exe_data(offset));
}

/** ****************************************************************************
 *
 * \brief	Fill a gear record of a driving record.
 *
 * \param	drivingAt		DS offset of the driving record.
 * \param	pSetup			The car's parts, factor and top speed.
 * \param	gear			Gear record, 0..4.
 *
 * \details	The loop of FUN_1000_dcbe. The gear's share of the top speed
 * 			(DS:0x54dc by transmission, in percent; 0.2 mph when 0)
 * 			gives its revs per mph, 5700 / speed (+0x34). The record's
 * 			three ranges are 0 to the engine's lower revs, on to its
 * 			upper revs and on to 5700 (DS:0x5522). The pull is the
 * 			engine's for the gear (DS:0x5504) times the car's factor;
 * 			the ranges get pull / 5 (whole in gear record 0; in gear
 * 			record 1 doubled, with tires 2 tripled), pull and pull / 10
 * 			(whole in record 0), and as second value pull / (5 - gear)
 * 			(whole in record 0).
 *
 ******************************************************************************/
static void driving__setGear(
	uint16_t		   drivingAt,
	const cars_Setup * pSetup,
	int				   gear
) {
	int		 shareIndex = pSetup->gearbox * DRIVING_TABLE_ROW + gear * 2;
	int		 pullIndex = pSetup->engine * DRIVING_TABLE_ROW + gear * 2;
	int		 gearIndex = DRIVING_GEAR_RECORDS + gear * DRIVING_GEAR_SIZE;
	uint16_t shareAt = (uint16_t)(DRIVING_GEAR_SHARES + shareIndex);
	uint16_t pullAt = (uint16_t)(DRIVING_PULLS + pullIndex);
	uint16_t revsAt = (uint16_t)(DRIVING_ENGINE_REVS + pSetup->engine * 4);
	uint16_t gearAt = (uint16_t)(drivingAt + gearIndex);
	uint16_t lowRevs = exe_readU16(revsAt);
	uint16_t highRevs = exe_readU16((uint16_t)(revsAt + 2));
	fpu_Real speed = fpu_mul(fpu_fromInt(exe_readS16(shareAt)), pSetup->speed);

	speed = fpu_div(speed, driving__constant(DRIVING_FPU_HUNDRED));

	if (fpu_compare(speed, driving__constant(DRIVING_FPU_ZERO)) == 0) {
		speed = driving__constant(DRIVING_FPU_NO_GEAR);
	}

	fpu_Real revsPerMph = fpu_div(driving__constant(DRIVING_FPU_REVS), speed);

	exe_writeU16(
		(uint16_t)(drivingAt + DRIVING_GEAR_REVS + gear * 2),
		(uint16_t)fpu_toLong(revsPerMph)
	);

	uint16_t firstAt = gearAt;
	uint16_t secondAt = (uint16_t)(gearAt + DRIVING_RANGE_SIZE);
	uint16_t thirdAt = (uint16_t)(secondAt + DRIVING_RANGE_SIZE);

	exe_writeU16((uint16_t)(firstAt + DRIVING_RANGE_FROM), 0);
	exe_writeU16((uint16_t)(firstAt + DRIVING_RANGE_TO), lowRevs);
	exe_writeU16((uint16_t)(secondAt + DRIVING_RANGE_FROM), lowRevs);
	exe_writeU16((uint16_t)(secondAt + DRIVING_RANGE_TO), highRevs);
	exe_writeU16((uint16_t)(thirdAt + DRIVING_RANGE_FROM), highRevs);
	exe_writeU16((uint16_t)(thirdAt + DRIVING_RANGE_TO), DRIVING_LAST_REVS);

	fpu_Real enginePull = fpu_fromInt(exe_readS16(pullAt));
	int16_t	 pull = (int16_t)fpu_toLong(fpu_mul(enginePull, pSetup->factor));
	int16_t	 lowPull = (int16_t)(pull / (gear != 0 ? 5 : 1));
	int16_t	 doubled = 1;
	int16_t	 tripled = 1;

	if (gear == DRIVING_SECOND_GEAR) {
		if (pSetup->tires == DRIVING_WIDE_TIRES) {
			tripled = 3;
		} else {
			doubled = 2;
		}
	}

	exe_writeU16(
		(uint16_t)(firstAt + DRIVING_RANGE_PULL),
		(uint16_t)(tripled * doubled * lowPull)
	);
	exe_writeU16((uint16_t)(secondAt + DRIVING_RANGE_PULL), (uint16_t)pull);
	exe_writeU16(
		(uint16_t)(thirdAt + DRIVING_RANGE_PULL),
		(uint16_t)(pull / (gear != 0 ? 10 : 1))
	);

	int16_t second = gear != 0 ? (int16_t)(pull / (5 - gear)) : pull;

	for (int range = 0; range < DRIVING_RANGE_COUNT; range++) {
		uint16_t rangeAt = (uint16_t)(gearAt + range * DRIVING_RANGE_SIZE);

		exe_writeU16(
			(uint16_t)(rangeAt + DRIVING_RANGE_PULL2),
			(uint16_t)second
		);
	}
}

/** ****************************************************************************
 *
 * \brief	Copy a word of a driving record to another.
 *
 * \param	drivingAt		DS offset of the driving record.
 * \param	to				Offset of the word to set.
 * \param	from			Offset of the word to copy.
 *
 ******************************************************************************/
static void driving__copyWord(
	uint16_t drivingAt,
	int		 to,
	int		 from
) {
	exe_writeU16(
		(uint16_t)(drivingAt + to),
		exe_readU16((uint16_t)(drivingAt + from))
	);
}
