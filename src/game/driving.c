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
 * 			from start to end; the drive itself is drive.c),
 * 			FUN_1000_8d9c (the palette entries of a race), and the
 * 			movement during a race (FUN_1000_da25, d992, d67d, d624,
 * 			d29d, d544, d1de, c407).
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/driving.h"

#include <stdbool.h>

#include "dos/fpu.h"
#include "game/cars.h"
#include "game/drive.h"
#include "game/effects.h"
#include "game/exe.h"
#include "game/globals.h"
#include "game/random.h"
#include "game/road.h"
#include "game/roadpic.h"
#include "game/roadstate.h"
#include "game/state.h"
#include "game/ui.h"
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
#define DRIVING_TOP_AUTO	 0x1518 /**< Opponent top revs on fast PC. */
#define DRIVING_SKILL_REVS	 100	/**< Revs per point of the ad's +1. */
#define DRIVING_FAST_MACHINE 4		/**< DS:0x8acc from it: fast machine. */
#define DRIVING_DRAG_END	 0x17c	/**< Place where a drag race ends. */
#define DRIVING_ROAD_END	 0x3fc	/**< Place where the other races end. */
#define DRIVING_POLICE_MODEL 0x19	/**< Police car model for chases. */
#define DRIVING_POLICE_COLOR 5		/**< Paint index for police chase. */
#define DRIVING_POLICE_CARS	 12		/**< Police pictures copied (0x18 words). */
#define DRIVING_FLAG_LOST	 0x01	/**< Race flag: crossed too late. */
#define DRIVING_FLAG_CRASH	 0x03	/**< Race flag: crashed + lost. */
#define DRIVING_FLAG_POLICE	 0x41	/**< Race flag: stopped by police. */
#define DRIVING_SCREECH_ON	 12		/**< |wheel| from it: tire screech. */
#define DRIVING_SCREECH_OFF	 10		/**< |wheel| below it: screech stops. */
#define DRIVING_LANE_LEFT	 1500	/**< Wide road left edge. */
#define DRIVING_LANE_NARROW	 2000	/**< Narrow road left edge. */
#define DRIVING_LANE_RIGHT	 3300	/**< Wide road right edge. */
#define DRIVING_LANE_PINCH	 2800	/**< Narrow road right edge. */
#define DRIVING_LANE_START	 2560	/**< Player initial lateral offset. */
#define DRIVING_LANE_MID	 2400	/**< Centerline of wide road. */
#define DRIVING_ROAD_X_START 1900	/**< Initial road X (DS:0x7d38). */

/*
 * 8-byte reals of Sr.exe.
 */
#define DRIVING_FPU_RATE	   0x6954 /**< DS: real 6.068827333333334. */
#define DRIVING_FPU_PERCENT	   0x695c /**< DS: real 100 (finish %). */
#define DRIVING_FPU_CRUISE	   0x696c /**< DS: real 0.7. */
#define DRIVING_FPU_FAST	   0x6974 /**< DS: real 0.8. */
#define DRIVING_FPU_COP_CRUISE 0x6980 /**< DS: real 0.6. */
#define DRIVING_FPU_HUNDRED	   0x69b8 /**< DS: real 100. */
#define DRIVING_FPU_NO_GEAR	   0x69d8 /**< DS: real 0.2. */
#define DRIVING_FPU_ZERO	   0x6a08 /**< DS: real 0. */
#define DRIVING_FPU_REVS	   0x6a10 /**< DS: real 5700. */
#define DRIVING_FPU_WEIGHT	   0x6a18 /**< DS: real 5. */
#define DRIVING_FPU_SCALE	   0x6a20 /**< DS: real 50. */

/*
 * Variables in the data segment image.
 */
#define DRIVING_UNKNOWN_5D6	   0x5d6  /**< s16: cleared before a drive. */
#define DRIVING_UNKNOWN_5D8	   0x5d8  /**< s16: cleared before a drive. */
#define DRIVING_UNKNOWN_5DA	   0x5da  /**< s16: cleared before a drive. */
#define DRIVING_UNKNOWN_53E6   0x53e6 /**< s16: 1 after finish recorded. */
#define DRIVING_UNKNOWN_54D0   0x54d0 /**< s16: opponent's last +0xc0. */
#define DRIVING_UNKNOWN_54D2   0x54d2 /**< s16: player ram toggle. */
#define DRIVING_RIVAL_SIDE	   0x54d4 /**< s16: opponent preferred side. */
#define DRIVING_OPP_CURVE	   0x54d6 /**< s16: 1 while opponent curves. */
#define DRIVING_BUMP_TICKS	   0x54d8 /**< s16: steps beside player. */
#define DRIVING_FORCED_BRAKE   0x54da /**< s16: forced braking steps. */
#define DRIVING_LENGTH		   0x6caa /**< s16: 380 drag race, else 1020. */
#define DRIVING_RIVAL_SPEED	   0x6cac /**< s16: top speed of DS:0x7648. */
#define DRIVING_UNKNOWN_6CAE   0x6cae /**< s16: 10 slow machine, else 16. */
#define DRIVING_RIVAL_FAST	   0x6cb0 /**< s16: 0.8 of that top speed. */
#define DRIVING_RIVAL_CRUISE   0x6cb2 /**< s16: 0.7/0.6 of that top speed. */
#define DRIVING_RIVAL_SKILL	   0x6cb4 /**< s16: byte +1 of the ad record. */
#define DRIVING_RIVAL_PLACE	   0x6cb6 /**< s16: opponent target lateral X. */
#define DRIVING_RIVAL_RANGE	   0x6cb8 /**< s16: opponent range index. */
#define DRIVING_RIVAL_REVS	   0x6cba /**< s16: top revs less 100 * skill. */
#define DRIVING_UNKNOWN_70C0   0x70c0 /**< s16: road left edge +4 ahead. */
#define DRIVING_CRASHED		   0x72f2 /**< s16: 1: ran into the opponent. */
#define DRIVING_POLICE_AT	   0x755a /**< s16: place police may appear. */
#define DRIVING_AVERAGE_SPEED  0x7644 /**< s16: average speed (mph). */
#define DRIVING_RIVAL_MODEL	   0x7648 /**< s16: the opponent's car model. */
#define DRIVING_TIRES_BLOWN	   0x787c /**< u8: cleared before a drive. */
#define DRIVING_RACE_FLAGS	   0x799c /**< u16: race outcome flags. */
#define DRIVING_WON_FLAGS	   0x7a9e /**< u16: bit 0 set when player won. */
#define DRIVING_UNKNOWN_7D2E   0x7d2e /**< s16: road right edge +4 ahead. */
#define DRIVING_PLACE		   0x7d30 /**< s16: player's place on road. */
#define DRIVING_PLAYER_SUB	   0x7d32 /**< s16: player's eighth-tile pos. */
#define DRIVING_PLAYER_STEP	   0x7d34 /**< s16: player's speed step. */
#define DRIVING_PLAYER_LAT	   0x7d36 /**< s16: player's lateral offset. */
#define DRIVING_ROAD_X		   0x7d38 /**< s16: eye height of the view. */
#define DRIVING_UNKNOWN_7D3A   0x7d3a /**< s16: cleared at road start. */
#define DRIVING_CURVE		   0x7d3c /**< s16: heading of the view. */
#define DRIVING_SLOPE		   0x7d3e /**< s16: road slope at player. */
#define DRIVING_NO_DRIVER	   0x7d40 /**< s16: 1 vs car with no driver. */
#define DRIVING_CAR_GAP		   0x7d7c /**< s16: 3645 opponent Z gap slot. */
#define DRIVING_RIVAL_ATTACK   0x7d7e /**< s16: 1 when rival may ram. */
#define DRIVING_ROAD_LEFT	   0x7d82 /**< s16: road left edge at player. */
#define DRIVING_RIVAL_AD	   0x7d84 /**< u16: DS offset of the ad record. */
#define DRIVING_RIVAL_AD_SKILL 1	  /**< Ad record: s8 skill factor. */
#define DRIVING_RIVAL_AD_CURVE 2	  /**< Ad record: s8 corner speed drop. */
#define DRIVING_UNKNOWN_7E8A   0x7e8a /**< s16: opponent's place on road. */
#define DRIVING_UNKNOWN_7E8C   0x7e8c /**< s16: opponent's eighth-tile pos. */
#define DRIVING_OPP_STEP	   0x7e8e /**< s16: opponent's speed step. */
#define DRIVING_UNKNOWN_7E90   0x7e90 /**< s16: opponent's lateral offset. */
#define DRIVING_KIND		   0x7ec0 /**< s16: 0 drag race, 1 road race. */
#define DRIVING_POLICE		   0x7f88 /**< s16: 0 none, 1..3 chase state. */
#define DRIVING_POLICE_AD	   0x80ac /**< u16: police car ad in Hot_data. */
#define DRIVING_UNKNOWN_8630   0x8630 /**< s16: -1 before a drive. */
#define DRIVING_UNKNOWN_8228   0x8228 /**< s16: cleared before a drive. */
#define DRIVING_DISTANCE	   0x82bc /**< u32: cumulative speed steps. */
#define DRIVING_COVERED		   0x82c8 /**< s16: percent covered (max 99). */
#define DRIVING_PLAYER_AHEAD   0x8632 /**< s16: 1 player beat/tied rival. */
#define DRIVING_WEAVE_READY	   0x8648 /**< s16: 1 opponent may change side. */
#define DRIVING_CARS_NEAR	   0x8bc2 /**< s16: 1 when 3645 sees overlap. */
#define DRIVING_RACE_TIME	   0x8bc4 /**< float: elapsed seconds in race. */
#define DRIVING_ROAD_RIGHT	   0x8bd0 /**< s16: road right edge at player. */
#define DRIVING_ROAD_CLEAR	   0x8236 /**< s16: -6 skips auto-rev cap. */
#define DRIVING_SAVED_COLORS   0x51ac /**< 2 x u8: colors before a race. */
#define DRIVING_PAINT_TABLE	   0x50c2 /**< 2 colors per paint. */
#define DRIVING_NO_COLOR	   0xff	  /**< DS:0x51ac: nothing put aside. */
#define DRIVING_FIRST_ENTRY	   12	  /**< Palette entry of the first color. */
#define DRIVING_SECOND_ENTRY   3	  /**< Palette entry of the second. */
#define DRIVING_SIREN		   0x58dc /**< s16: FUN_1f38_7728 sets/clears. */
#define DRIVING_UNKNOWN_58DA   0x58da /**< s16: tire screech sound flag. */
#define DRIVING_RACING		   0x8aca /**< u8: 1 in a race, 0 on demo road. */
#define DRIVING_DEMO_ROAD	   0x0284 /**< s16: demo road choice (1..3). */

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

static void driving__selectRange(
	uint16_t drivingAt,
	int16_t	 gear,
	int16_t	 revs
);

static int16_t driving__roadCurve(
	int16_t place
);

static int16_t driving__roadFlags(
	int16_t place
);

static void driving__matchGear(
	int16_t	 speed,
	uint16_t drivingAt
);

static int16_t driving__speedStep(
	int16_t speed
);

static void driving__opponentAi(void);

static void driving__advancePlayer(void);

static void driving__avoidCurve(
	int16_t curveDelta,
	int16_t midLane
);

static void driving__chaseLane(
	int16_t curveDelta,
	int16_t roadFlags,
	int16_t midLane
);

static void driving__advanceOpponent(void);

static void driving__spawnPolice(void);

static void driving__finish(void);

static void driving__updateGearStatus(void);

static bool driving__updatePolice(void);

static void driving__checkProximity(void);

static void driving__bumpSound(void);

static int16_t driving__magnitude(
	int16_t value
);

static int16_t driving__raceEnd(void);

static void driving__writeS16(
	uint16_t offset,
	int16_t	 value
);

static uint16_t driving__gearRange(
	uint16_t drivingAt,
	int16_t	 gear,
	int16_t	 range
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

	driving__selectRange(rival, 0, DRIVING_IDLE_REVS);

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
 * \brief	Step both cars, police chase, finish checks and collisions.
 *
 * \details	Port of FUN_1000_da25. Updates the dashboard gear display, checks
 * 			blown tires and screeching wheels, steps the opponent AI, checks
 * 			police spawning and pursuit, advances both cars along the road,
 * 			records race completion once either car crosses the finish, and
 * 			resolves bumper-to-bumper contact.
 *
 ******************************************************************************/
void driving_move(void) {
	driving__updateGearStatus();

	int16_t blowoutAt = exe_readS16(DRIVING_PLAYER + DRIVING_BLOWOUT_AT);

	if (blowoutAt != 0 && exe_readS16(DRIVING_PLACE) > blowoutAt) {
		*exe_data(DRIVING_TIRES_BLOWN) = 1;
	}

	int16_t wheelAbs =
		driving__magnitude(exe_readS16(DRIVING_PLAYER + DRIVING_WHEEL));

	if (exe_readS16(DRIVING_UNKNOWN_58DA) == 0) {
		if (wheelAbs >= DRIVING_SCREECH_ON) {
			driving__writeS16(DRIVING_UNKNOWN_58DA, 1);
		}
	} else if (wheelAbs < DRIVING_SCREECH_OFF) {
		driving__writeS16(DRIVING_UNKNOWN_58DA, 0);
	}

	driving__opponentAi();

	if (driving__updatePolice()) {
		drive_stopCar(0);
		return;
	}

	driving__advancePlayer();
	driving__advanceOpponent();

	int16_t length = exe_readS16(DRIVING_LENGTH);

	if (exe_readS16(DRIVING_PLACE) >= length ||
		exe_readS16(DRIVING_UNKNOWN_7E8A) >= length) {
		driving__finish();
	}

	if (exe_readU16(DRIVING_RACE_FLAGS) != 0 ||
		exe_readU16(DRIVING_WON_FLAGS) != 0) {
		if (exe_readS16(DRIVING_POLICE) >= 2) {
			if (exe_readU16(DRIVING_RACE_FLAGS) != 0) {
				uint16_t flags = exe_readU16(DRIVING_RACE_FLAGS);

				exe_writeU16(
					DRIVING_RACE_FLAGS,
					(uint16_t)(flags | DRIVING_FLAG_POLICE)
				);
			}
			driving__writeS16(DRIVING_SIREN, 0);
		}
		drive_stopCar(0);
		return;
	}

	driving__checkProximity();
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

/** ****************************************************************************
 *
 * \brief	Write a signed 16-bit integer to the data segment image.
 *
 * \param	offset			DS offset to write.
 * \param	value			Signed 16-bit value.
 *
 ******************************************************************************/
static void driving__writeS16(
	uint16_t offset,
	int16_t	 value
) {
	exe_writeU16(offset, (uint16_t)value);
}

/** ****************************************************************************
 *
 * \brief	Return the DS offset of one range inside a car's gear table.
 *
 * \param	drivingAt		DS offset of the driving record.
 * \param	gear			Gear record index.
 * \param	range			Range index within the gear record (0..2).
 *
 * \return	DS offset of that range entry.
 *
 ******************************************************************************/
static uint16_t driving__gearRange(
	uint16_t drivingAt,
	int16_t	 gear,
	int16_t	 range
) {
	uint16_t gearAt = (uint16_t)(drivingAt + DRIVING_GEAR_RECORDS);
	int16_t	 gearOffset = (int16_t)(gear * DRIVING_GEAR_SIZE);
	int16_t	 rangeOffset = (int16_t)(range * DRIVING_RANGE_SIZE);

	return (uint16_t)(gearAt + gearOffset + rangeOffset);
}

/** ****************************************************************************
 *
 * \brief	Pick the active rev range of a gear record and copy its limits.
 *
 * \param	drivingAt		DS offset of the driving record.
 * \param	gear			Gear record index (0..DRIVING_GEAR_COUNT - 1).
 * \param	revs			Engine speed in revs.
 *
 * \details	Matches the range selection shared by FUN_1000_cf5c and
 * 			FUN_1000_d29d: checks the middle range (1) against revs and picks
 * 			range 0, 1 or 2, writing DRIVING_RIVAL_RANGE, DRIVING_RANGE_INDEX,
 * 			DRIVING_RANGE_TO and DRIVING_RANGE_PULL.
 *
 ******************************************************************************/
static void driving__selectRange(
	uint16_t drivingAt,
	int16_t	 gear,
	int16_t	 revs
) {
	uint16_t rangeAt = driving__gearRange(drivingAt, gear, 1);
	int16_t	 range = 1;

	if (exe_readS16((uint16_t)(rangeAt + DRIVING_RANGE_TO)) <= revs) {
		range = 2;
		rangeAt = (uint16_t)(rangeAt + DRIVING_RANGE_SIZE);
	} else if (exe_readS16((uint16_t)(rangeAt + DRIVING_RANGE_FROM)) > revs) {
		range = 0;
		rangeAt = (uint16_t)(rangeAt - DRIVING_RANGE_SIZE);
	}

	driving__writeS16(DRIVING_RIVAL_RANGE, range);
	driving__writeS16((uint16_t)(drivingAt + DRIVING_RANGE_INDEX), range);
	driving__writeS16(
		(uint16_t)(drivingAt + DRIVING_RANGE + DRIVING_RANGE_TO),
		exe_readS16((uint16_t)(rangeAt + DRIVING_RANGE_TO))
	);
	driving__writeS16(
		(uint16_t)(drivingAt + DRIVING_RANGE + DRIVING_RANGE_PULL),
		exe_readS16((uint16_t)(rangeAt + DRIVING_RANGE_PULL))
	);
}

/** ****************************************************************************
 *
 * \brief	Curve of a road tile.
 *
 * \param	place			Road tile index.
 *
 * \return	The curve at that tile (road_table).
 *
 ******************************************************************************/
static int16_t driving__roadCurve(
	int16_t place
) {
	return (int16_t)road_table[ROAD_CURVE + place];
}

/** ****************************************************************************
 *
 * \brief	Road flags of a road tile.
 *
 * \param	place			Road tile index.
 *
 * \return	The road flags at that tile (road_table).
 *
 ******************************************************************************/
static int16_t driving__roadFlags(
	int16_t place
) {
	return (int16_t)road_table[ROAD_FLAGS + place];
}

/** ****************************************************************************
 *
 * \brief	Pick a gear and rev range matching a target speed.
 *
 * \param	speed			Target speed in mph.
 * \param	drivingAt		DS offset of the driving record.
 *
 * \details	Port of FUN_1000_d1de. Walks gears from 1 up until the middle
 * 			range ceiling exceeds ratio * speed or the top gear is reached,
 * 			then stores gear, range, pull and quantized speed.
 *
 ******************************************************************************/
static void driving__matchGear(
	int16_t	 speed,
	uint16_t drivingAt
) {
	int16_t	 gears = exe_readS16((uint16_t)(drivingAt + DRIVING_GEARS));
	int16_t	 gear = 1;
	int16_t	 range = 0;
	int16_t	 revs = 0;
	int16_t	 ratio = 0;
	uint16_t chosenAt = (uint16_t)(drivingAt + DRIVING_GEAR_RECORDS);

	for (;;) {
		ratio = exe_readS16(
			(uint16_t)(drivingAt + DRIVING_GEAR_REVS + gear * 2)
		);
		driving__writeS16((uint16_t)(drivingAt + DRIVING_REVS_PER_MPH), ratio);
		revs = (int16_t)(ratio * speed);
		driving__writeS16((uint16_t)(drivingAt + DRIVING_REVS), revs);

		uint16_t lowAt = driving__gearRange(drivingAt, gear, 0);
		uint16_t midAt = (uint16_t)(lowAt + DRIVING_RANGE_SIZE);

		if (exe_readS16((uint16_t)(midAt + DRIVING_RANGE_TO)) > revs) {
			if (revs >= exe_readS16((uint16_t)(midAt + DRIVING_RANGE_FROM))) {
				range = 1;
				chosenAt = midAt;
			} else {
				range = 0;
				chosenAt = lowAt;
			}
			break;
		}

		if (gear >= gears) {
			range = 2;
			chosenAt = (uint16_t)(midAt + DRIVING_RANGE_SIZE);
			break;
		}

		gear++;
	}

	driving__writeS16((uint16_t)(drivingAt + DRIVING_GEAR), gear);
	driving__writeS16((uint16_t)(drivingAt + DRIVING_RANGE_INDEX), range);
	driving__writeS16(DRIVING_RIVAL_RANGE, range);
	driving__writeS16(
		(uint16_t)(drivingAt + DRIVING_RANGE + DRIVING_RANGE_PULL),
		exe_readS16((uint16_t)(chosenAt + DRIVING_RANGE_PULL))
	);
	driving__writeS16(
		(uint16_t)(drivingAt + DRIVING_SPEED),
		ratio != 0 ? (int16_t)(revs / ratio) : 0
	);
}

/** ****************************************************************************
 *
 * \brief	Map speed in mph to eighth-tile movement step per road frame.
 *
 * \param	speed			Car speed in mph.
 *
 * \return	Movement step (0..16), or scaled for slow machines (< 4).
 *
 * \details	Port of FUN_1000_d544.
 *
 ******************************************************************************/
static int16_t driving__speedStep(
	int16_t speed
) {
	static const int16_t limits[] = {
		13, 27, 41, 55, 69, 83, 97, 101, 105, 109, 113, 117, 121, 125, 129
	};

	if (speed <= 0) {
		return 0;
	}

	if (globals_machineSpeed < DRIVING_FAST_MACHINE) {
		if (speed <= 71) {
			return (int16_t)((speed >> 3) + 1);
		}
		return (int16_t)((speed >> 2) - 8);
	}

	int16_t limitCount = (int16_t)(sizeof(limits) / sizeof(limits[0]));

	for (int16_t index = 0; index < limitCount; index++) {
		if (speed <= limits[index]) {
			return (int16_t)(index + 1);
		}
	}

	return 16;
}

/** ****************************************************************************
 *
 * \brief	Step the opponent's engine, shifting and speed AI.
 *
 * \details	Port of FUN_1000_d29d. Runs once per elapsed timer step since the
 * 			last call, braking on curves, rubberbanding or cruising relative to
 * 			the player, capping speed for active police chases, climbing revs
 * 			and upshifting, then decelerates after passing the finish line.
 *
 ******************************************************************************/
static void driving__opponentAi(void) {
	int16_t savedSpeed = exe_readS16(DRIVING_OPPONENT + DRIVING_SPEED);
	int16_t oppTime = exe_readS16(DRIVING_PLAYER + DRIVING_TIME);
	int16_t steps = (int16_t)(oppTime - exe_readS16(DRIVING_UNKNOWN_54D0));

	driving__writeS16(DRIVING_UNKNOWN_54D0, oppTime);

	for (int16_t step = 0; step < steps; step++) {
		int16_t oppPlace = exe_readS16(DRIVING_UNKNOWN_7E8A);

		if (driving__roadCurve(oppPlace) !=
			driving__roadCurve((int16_t)(oppPlace + 1))) {
			if (exe_readS16(DRIVING_OPP_CURVE) == 0) {
				driving__writeS16(DRIVING_OPP_CURVE, 1);

				uint16_t adAt = exe_readU16(DRIVING_RIVAL_AD);
				int16_t	 drop = (int16_t)(int8_t)*exe_data(
					 (uint16_t)(adAt + DRIVING_RIVAL_AD_CURVE)
				 );

				if (oppPlace <= exe_readS16(DRIVING_PLACE)) {
					drop = (int16_t)(drop >> 1);
				}

				int16_t targetSpeed =
					(int16_t)(exe_readS16(DRIVING_RIVAL_SPEED) - drop);
				int16_t oppSpeed =
					exe_readS16(DRIVING_OPPONENT + DRIVING_SPEED);

				if (targetSpeed < oppSpeed) {
					driving__matchGear(targetSpeed, DRIVING_OPPONENT);
				}
			}
			continue;
		}

		driving__writeS16(DRIVING_OPP_CURVE, 0);

		int16_t lead = (int16_t)(oppPlace - exe_readS16(DRIVING_PLACE));
		int16_t playerSpeed = exe_readS16(DRIVING_PLAYER + DRIVING_SPEED);
		int16_t oppSpeed = exe_readS16(DRIVING_OPPONENT + DRIVING_SPEED);
		int16_t targetSpeed = oppSpeed;
		bool	matchTarget = false;

		if (lead > 12 &&
			(exe_readS16(DRIVING_KIND) != 1 ||
			 exe_readS16(DRIVING_NO_DRIVER) == 0)) {
			if (playerSpeed >= 97 || exe_readS16(DRIVING_NO_DRIVER) != 0) {
				targetSpeed = exe_readS16(DRIVING_RIVAL_FAST);
			} else {
				targetSpeed = exe_readS16(DRIVING_RIVAL_CRUISE);
			}
			matchTarget = targetSpeed < oppSpeed;
		} else if (lead > -5 && lead < 5) {
			targetSpeed =
				(int16_t)(playerSpeed + exe_readS16(DRIVING_UNKNOWN_6CAE));
			if (exe_readS16(DRIVING_PLACE) >= 30 &&
				targetSpeed < exe_readS16(DRIVING_RIVAL_CRUISE)) {
				targetSpeed = exe_readS16(DRIVING_RIVAL_CRUISE);
			}
			matchTarget = targetSpeed < oppSpeed;
		}

		if (matchTarget) {
			driving__matchGear(targetSpeed, DRIVING_OPPONENT);
			continue;
		}

		if (exe_readS16(DRIVING_POLICE) >= 2 && playerSpeed > 50) {
			int16_t policeCap = (int16_t)(playerSpeed + 32);

			if (oppSpeed > policeCap) {
				driving__matchGear(policeCap, DRIVING_OPPONENT);
				continue;
			}
		}

		int16_t topRevs = exe_readS16(DRIVING_RIVAL_REVS);
		int16_t pull = exe_readS16(
			DRIVING_OPPONENT + DRIVING_RANGE + DRIVING_RANGE_PULL
		);
		int16_t oldRevs = exe_readS16(DRIVING_OPPONENT + DRIVING_REVS);
		int16_t revs = (int16_t)(oldRevs + pull);

		if (revs > topRevs) {
			revs = topRevs;
		}
		driving__writeS16(DRIVING_OPPONENT + DRIVING_REVS, revs);

		int16_t gear = exe_readS16(DRIVING_OPPONENT + DRIVING_GEAR);

		if (revs > exe_readS16(
					   DRIVING_OPPONENT + DRIVING_RANGE + DRIVING_RANGE_TO
				   )) {
			int16_t prevRange = exe_readS16(
				DRIVING_OPPONENT + DRIVING_RANGE_INDEX
			);
			int16_t range = (int16_t)(prevRange + 1);

			if (range < DRIVING_RANGE_COUNT) {
				uint16_t nextAt = driving__gearRange(
					DRIVING_OPPONENT,
					gear,
					range
				);

				driving__writeS16(
					DRIVING_OPPONENT + DRIVING_RANGE_INDEX,
					range
				);
				driving__writeS16(
					DRIVING_OPPONENT + DRIVING_RANGE + DRIVING_RANGE_TO,
					exe_readS16((uint16_t)(nextAt + DRIVING_RANGE_TO))
				);
				driving__writeS16(
					DRIVING_OPPONENT + DRIVING_RANGE + DRIVING_RANGE_PULL,
					exe_readS16((uint16_t)(nextAt + DRIVING_RANGE_PULL))
				);
			}
		}

		int16_t gears = exe_readS16(DRIVING_OPPONENT + DRIVING_GEARS);

		if (gear < gears &&
			((exe_readS16(DRIVING_OPPONENT + DRIVING_AUTOMATIC) != 0 &&
			  revs >= exe_readS16(DRIVING_OPPONENT + DRIVING_SHIFT_REVS)) ||
			 revs >= topRevs)) {
			gear++;
			driving__writeS16(DRIVING_OPPONENT + DRIVING_GEAR, gear);

			if (exe_readS16(DRIVING_OPPONENT + DRIVING_AUTOMATIC) == 0) {
				int16_t oppTopRevs = exe_readS16(
					DRIVING_OPPONENT + DRIVING_TOP_REVS
				);
				bool	slowPc = globals_machineSpeed < DRIVING_FAST_MACHINE;
				bool	noCap = exe_readS16(DRIVING_ROAD_CLEAR) != -6;
				int16_t nextRevs;

				if (gear < gears) {
					int16_t skill = exe_readS16(DRIVING_RIVAL_SKILL);
					int16_t skillDrop = (int16_t)(DRIVING_SKILL_REVS * skill);

					nextRevs = (int16_t)(oppTopRevs - skillDrop);
				} else if (slowPc && noCap) {
					nextRevs = DRIVING_AUTO_REVS;
				} else {
					nextRevs = DRIVING_TOP_AUTO;
				}
				driving__writeS16(DRIVING_RIVAL_REVS, nextRevs);
			}

			int16_t nextRatio = exe_readS16(
				(uint16_t)(DRIVING_OPPONENT + DRIVING_GEAR_REVS + gear * 2)
			);
			int16_t prevSpeed = exe_readS16(DRIVING_OPPONENT + DRIVING_SPEED);

			driving__writeS16(
				DRIVING_OPPONENT + DRIVING_REVS_PER_MPH,
				nextRatio
			);
			revs = (int16_t)(prevSpeed * nextRatio);
			driving__writeS16(DRIVING_OPPONENT + DRIVING_REVS, revs);
			driving__selectRange(DRIVING_OPPONENT, gear, revs);
		}

		int16_t ratio = exe_readS16(DRIVING_OPPONENT + DRIVING_REVS_PER_MPH);

		driving__writeS16(
			DRIVING_OPPONENT + DRIVING_SPEED,
			ratio != 0 ? (int16_t)(revs / ratio) : 0
		);
	}

	if (exe_readS16(DRIVING_UNKNOWN_7E8A) >= exe_readS16(DRIVING_LENGTH)) {
		int16_t brake =
			exe_readS16(DRIVING_UNKNOWN_7E8A) > exe_readS16(DRIVING_PLACE) + 5
			? 5
			: 10;
		int16_t nextSpeed =
			brake < savedSpeed ? (int16_t)(savedSpeed - brake) : 0;

		driving__writeS16(DRIVING_OPPONENT + DRIVING_SPEED, nextSpeed);
	}
}

/** ****************************************************************************
 *
 * \brief	Advance the player's road position for the current frame.
 *
 * \details	Port of FUN_1000_d624. Converts player speed to an eighth-tile
 * 			step, carries whole tiles via roadstate_advance, and stops the
 * 			car once the finish line is crossed and speed drops to zero.
 *
 ******************************************************************************/
static void driving__advancePlayer(void) {
	int16_t speed = exe_readS16(DRIVING_PLAYER + DRIVING_SPEED);
	int16_t step = speed != 0 ? driving__speedStep(speed) : 0;

	driving__writeS16(DRIVING_PLAYER_STEP, step);

	int16_t sub = (int16_t)(exe_readS16(DRIVING_PLAYER_SUB) + step);

	driving__writeS16(DRIVING_PLAYER_SUB, sub);

	if (sub >= 8) {
		roadstate_advance();
	}

	if (exe_readS16(DRIVING_PLACE) >= exe_readS16(DRIVING_LENGTH) &&
		exe_readS16(DRIVING_UNKNOWN_53E6) != 0 &&
		exe_readS16(DRIVING_PLAYER + DRIVING_SPEED) <= 0) {
		if (exe_readS16(DRIVING_PLAYER_AHEAD) != 0) {
			exe_writeU16(
				DRIVING_WON_FLAGS,
				(uint16_t)(exe_readU16(DRIVING_WON_FLAGS) | 1)
			);
		} else {
			exe_writeU16(
				DRIVING_RACE_FLAGS,
				(uint16_t)(exe_readU16(DRIVING_RACE_FLAGS) | DRIVING_FLAG_LOST)
			);
		}
		drive_stopCar(0);
	}
}

/** ****************************************************************************
 *
 * \brief	Steer the opponent toward the outside of an upcoming curve.
 *
 * \param	curveDelta		Curve delta ahead of the player (+4 minus +5).
 * \param	midLane			Center lateral position of the road ahead.
 *
 * \details	Shared curve-positioning target of FUN_1000_d67d (0xd770..0xd7a1).
 *
 ******************************************************************************/
static void driving__avoidCurve(
	int16_t curveDelta,
	int16_t midLane
) {
	if (curveDelta < 0) {
		driving__writeS16(DRIVING_RIVAL_PLACE, (int16_t)(midLane - 200));
		driving__writeS16(DRIVING_RIVAL_SIDE, 1);
	} else {
		driving__writeS16(DRIVING_RIVAL_PLACE, (int16_t)(midLane + 300));
		driving__writeS16(DRIVING_RIVAL_SIDE, 0);
	}
}

/** ****************************************************************************
 *
 * \brief	Pick the opponent's attacking lane near the player.
 *
 * \param	curveDelta		Curve delta ahead of the player (+4 minus +5).
 * \param	roadFlags		Width flags at four tiles ahead of the opponent.
 * \param	midLane			Center lateral position of the road ahead.
 *
 * \details	Shared lane choice of FUN_1000_d67d when the opponent is slightly
 * 			ahead (6 < lead <= 12) or just behind (-6 < lead <= -4).
 *
 ******************************************************************************/
static void driving__chaseLane(
	int16_t curveDelta,
	int16_t roadFlags,
	int16_t midLane
) {
	driving__writeS16(DRIVING_RIVAL_ATTACK, 1);

	if (curveDelta != 0) {
		driving__avoidCurve(curveDelta, midLane);
	} else if (roadFlags != 0) {
		driving__writeS16(DRIVING_RIVAL_PLACE, midLane);
	} else if (exe_readS16(DRIVING_RIVAL_SIDE) != 0) {
		driving__writeS16(DRIVING_RIVAL_PLACE, (int16_t)(midLane - 200));
	} else {
		driving__writeS16(DRIVING_RIVAL_PLACE, (int16_t)(midLane + 300));
	}
}

/** ****************************************************************************
 *
 * \brief	Advance the opponent along the road and steer its lane position.
 *
 * \details	Port of FUN_1000_d67d. Updates the opponent's tile progress, checks
 * 			for race-end stop when finish has been recorded, picks a target
 * 			lateral offset by lead distance and overtaking room, and smooths
 * 			DRIVING_UNKNOWN_7E90 one eighth toward the target.
 *
 ******************************************************************************/
static void driving__advanceOpponent(void) {
	int16_t speed = exe_readS16(DRIVING_OPPONENT + DRIVING_SPEED);
	int16_t step = speed != 0 ? driving__speedStep(speed) : 0;

	driving__writeS16(DRIVING_OPP_STEP, step);

	int16_t sub = (int16_t)(exe_readS16(DRIVING_UNKNOWN_7E8C) + step);

	driving__writeS16(DRIVING_UNKNOWN_7E8C, sub);

	if (sub >= 8 && step > 0) {
		driving__writeS16(
			DRIVING_UNKNOWN_7E8A,
			(int16_t)(exe_readS16(DRIVING_UNKNOWN_7E8A) + (sub >> 3))
		);
		exe_writeU16(DRIVING_UNKNOWN_7E8C, (uint16_t)(sub & 7));
	}

	if (exe_readS16(DRIVING_UNKNOWN_7E8A) >= exe_readS16(DRIVING_LENGTH) &&
		exe_readS16(DRIVING_UNKNOWN_53E6) != 0 &&
		exe_readS16(DRIVING_PLAYER + DRIVING_SPEED) <= 0) {
		if (exe_readS16(DRIVING_PLAYER_AHEAD) == 0) {
			exe_writeU16(
				DRIVING_RACE_FLAGS,
				(uint16_t)(exe_readU16(DRIVING_RACE_FLAGS) | DRIVING_FLAG_LOST)
			);
		} else {
			exe_writeU16(
				DRIVING_WON_FLAGS,
				(uint16_t)(exe_readU16(DRIVING_WON_FLAGS) | 1)
			);
		}
		drive_stopCar(0);
		return;
	}

	int16_t oppPlace = exe_readS16(DRIVING_UNKNOWN_7E8A);
	int16_t roadFlags = (int16_t)(driving__roadFlags((int16_t)(oppPlace + 4)) &
								  0xe0);
	int16_t playerPlace = exe_readS16(DRIVING_PLACE);
	int16_t lead = (int16_t)(oppPlace - playerPlace);
	int16_t curveNow = driving__roadCurve((int16_t)(playerPlace + 4));
	int16_t curveNext = driving__roadCurve((int16_t)(playerPlace + 5));
	int16_t curveDelta = (int16_t)(curveNow - curveNext);
	int16_t leftEdge = exe_readS16(DRIVING_UNKNOWN_70C0);
	int16_t rightEdge = exe_readS16(DRIVING_UNKNOWN_7D2E);
	int16_t midLane = (int16_t)((leftEdge + rightEdge) >> 1);
	int16_t closeLead = globals_machineSpeed < DRIVING_FAST_MACHINE ? 4 : 2;

	if (lead > 12) {
		driving__writeS16(DRIVING_RIVAL_PLACE, midLane);
		if (curveDelta != 0) {
			driving__writeS16(DRIVING_RIVAL_SIDE, curveDelta < 0 ? 1 : 0);
		}
		driving__writeS16(DRIVING_WEAVE_READY, 1);

	} else if (lead > 6) {
		driving__chaseLane(curveDelta, roadFlags, midLane);

	} else if (lead > closeLead) {
		if (curveDelta != 0) {
			driving__avoidCurve(curveDelta, midLane);
		} else if (roadFlags != 0) {
			driving__writeS16(DRIVING_RIVAL_PLACE, midLane);
		} else if (exe_readS16(DRIVING_WEAVE_READY) != 0) {
			int16_t side = exe_readS16(DRIVING_RIVAL_SIDE);

			driving__writeS16(
				DRIVING_RIVAL_PLACE,
				(int16_t)(midLane + (side != 0 ? 300 : -200))
			);
			driving__writeS16(DRIVING_WEAVE_READY, 0);
			driving__writeS16(DRIVING_RIVAL_SIDE, side == 0 ? 1 : 0);
		}

	} else if (lead > -4) {
		int16_t gap = exe_readS16(DRIVING_CAR_GAP);
		int16_t playerLat = exe_readS16(DRIVING_PLAYER_LAT);
		int16_t oppLat = exe_readS16(DRIVING_UNKNOWN_7E90);

		if (step > exe_readS16(DRIVING_PLAYER_STEP)) {
			if (gap >= 9) {
				int16_t passLeft = (int16_t)(playerLat - 330);
				int16_t passRight = (int16_t)(playerLat + 430);

				if (passLeft > exe_readS16(DRIVING_ROAD_LEFT) + 480) {
					driving__writeS16(DRIVING_RIVAL_SIDE, 1);
					driving__writeS16(DRIVING_RIVAL_PLACE, passLeft);
					if (playerLat - oppLat < 280) {
						driving__writeS16(
							DRIVING_UNKNOWN_7E8A,
							(int16_t)(playerPlace - 1)
						);
						driving__matchGear(
							exe_readS16(DRIVING_PLAYER + DRIVING_SPEED),
							DRIVING_OPPONENT
						);
					}
				} else if (passRight < exe_readS16(DRIVING_ROAD_RIGHT) - 30) {
					driving__writeS16(DRIVING_RIVAL_SIDE, 0);
					driving__writeS16(
						DRIVING_RIVAL_PLACE,
						(int16_t)(passRight + 200)
					);
					if (oppLat - playerLat < 580) {
						driving__writeS16(
							DRIVING_UNKNOWN_7E8A,
							(int16_t)(playerPlace - 2)
						);
						driving__matchGear(
							exe_readS16(DRIVING_PLAYER + DRIVING_SPEED),
							DRIVING_OPPONENT
						);
					}
				} else {
					driving__writeS16(
						DRIVING_UNKNOWN_7E8A,
						(int16_t)(playerPlace - 2)
					);
					driving__matchGear(
						exe_readS16(DRIVING_PLAYER + DRIVING_SPEED),
						DRIVING_OPPONENT
					);
				}
			} else if (gap == 0 || gap == 2) {
				driving__writeS16(
					DRIVING_RIVAL_PLACE,
					playerLat > oppLat
						? (int16_t)(playerLat - 330)
						: (int16_t)(playerLat + 530)
				);
			}
		} else if (gap == 0) {
			driving__writeS16(DRIVING_RIVAL_PLACE, (int16_t)(playerLat - 330));
		}

	} else if (lead > -6) {
		driving__chaseLane(curveDelta, roadFlags, midLane);

	} else {
		driving__writeS16(DRIVING_RIVAL_PLACE, midLane);
		driving__writeS16(DRIVING_WEAVE_READY, 1);
	}

	int16_t targetLat = exe_readS16(DRIVING_RIVAL_PLACE);
	int16_t oppLat = exe_readS16(DRIVING_UNKNOWN_7E90);

	if (oppLat > targetLat) {
		int16_t delta = (int16_t)((oppLat - targetLat) >> 3);

		if (oppLat - delta > targetLat) {
			driving__writeS16(DRIVING_UNKNOWN_7E90, (int16_t)(oppLat - delta));
		}
	} else {
		int16_t delta = (int16_t)((targetLat - oppLat) >> 3);

		if (delta + oppLat < targetLat) {
			driving__writeS16(DRIVING_UNKNOWN_7E90, (int16_t)(oppLat + delta));
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Replace the opponent record with the pursuing police cruiser.
 *
 * \details	Port of FUN_1000_d992. Prepares the police model (0x19), matches its
 * 			gear to the current opponent speed, switches race paint to police
 * 			colors (5), copies the pictures 0..11 of the police car over the
 * 			opponent's (DS:0x7526 to DS:0x8ace), and stores 0.6 and
 * 			0.8 of the cruiser's top speed.
 *
 ******************************************************************************/
static void driving__spawnPolice(void) {
	int16_t speed = exe_readS16(DRIVING_OPPONENT + DRIVING_SPEED);

	exe_writeU16(DRIVING_RIVAL_AD, DRIVING_POLICE_AD);
	driving_prepare(DRIVING_POLICE_MODEL, DRIVING_OPPONENT);
	driving__writeS16(
		DRIVING_RIVAL_SKILL,
		(int16_t)(int8_t)*exe_data(
			(uint16_t)(DRIVING_POLICE_AD + DRIVING_RIVAL_AD_SKILL)
		)
	);
	driving__matchGear(speed, DRIVING_OPPONENT);
	driving_setRaceColors(DRIVING_POLICE_COLOR);

	for (int position = 0; position < DRIVING_POLICE_CARS; position++) {
		roadpic_set.pOpponent[position] = roadpic_set.pPolice[position];
	}

	int16_t	 topSpeed = cars_maxSpeed(DRIVING_POLICE_MODEL);
	fpu_Real baseSpeed = fpu_fromInt(topSpeed);

	driving__writeS16(DRIVING_RIVAL_SPEED, topSpeed);
	driving__writeS16(
		DRIVING_RIVAL_CRUISE,
		(int16_t)fpu_toLong(
			fpu_mul(baseSpeed, driving__constant(DRIVING_FPU_COP_CRUISE))
		)
	);
	driving__writeS16(
		DRIVING_RIVAL_FAST,
		(int16_t)fpu_toLong(
			fpu_mul(baseSpeed, driving__constant(DRIVING_FPU_FAST))
		)
	);
}

/** ****************************************************************************
 *
 * \brief	Record race outcome statistics once a car reaches the finish line.
 *
 * \details	Port of FUN_1000_c407. Runs once per race (guarded by
 * 			DRIVING_UNKNOWN_53E6): compares eighth-tile progress to set
 * 			DRIVING_PLAYER_AHEAD, divides DRIVING_DISTANCE by player time for
 * 			DRIVING_AVERAGE_SPEED, writes elapsed seconds to DRIVING_RACE_TIME,
 * 			and stores distance covered percentage (up to 99) in
 * 			DRIVING_COVERED.
 *
 ******************************************************************************/
static void driving__finish(void) {
	if (exe_readS16(DRIVING_UNKNOWN_53E6) != 0) {
		return;
	}

	int16_t oppPlace = exe_readS16(DRIVING_UNKNOWN_7E8A);
	int16_t playerPlace = exe_readS16(DRIVING_PLACE);
	int16_t oppSub = exe_readS16(DRIVING_UNKNOWN_7E8C);
	int16_t leadEighths = (int16_t)((oppPlace - playerPlace) * 8 + oppSub);

	driving__writeS16(
		DRIVING_PLAYER_AHEAD,
		leadEighths > exe_readS16(DRIVING_PLAYER_SUB) ? 0 : 1
	);

	int16_t playerTime = exe_readS16(DRIVING_PLAYER + DRIVING_TIME);
	int16_t avgSpeed = 0;

	if (playerTime != 0) {
		int32_t totalDist = (int32_t)exe_readU32(DRIVING_DISTANCE);

		avgSpeed = (int16_t)(totalDist / (int32_t)playerTime);
	}
	driving__writeS16(DRIVING_AVERAGE_SPEED, avgSpeed);

	fpu_Real elapsed = fpu_div(
		fpu_fromInt(playerTime),
		driving__constant(DRIVING_FPU_RATE)
	);

	fpu_toSingle(elapsed, exe_data(DRIVING_RACE_TIME));

	int16_t playerDist = (int16_t)(exe_readS16(DRIVING_PLACE) - DRIVING_START);
	int16_t oppDist =
		(int16_t)(exe_readS16(DRIVING_UNKNOWN_7E8A) - DRIVING_START);
	int16_t covered = 99;

	if (oppDist != 0) {
		fpu_Real ratio = fpu_div(
			fpu_mul(
				fpu_fromInt(playerDist),
				driving__constant(DRIVING_FPU_PERCENT)
			),
			fpu_fromInt(oppDist)
		);

		covered = (int16_t)fpu_toLong(ratio);
		if (covered > 99) {
			covered = 99;
		}
	}

	driving__writeS16(DRIVING_COVERED, covered);
	driving__writeS16(DRIVING_UNKNOWN_53E6, 1);
}

/** ****************************************************************************
 *
 * \brief	Redraw the gear indicator in the status bar when it changes.
 *
 * \details	Part of FUN_1000_da25 (0xda2c..0xda5e): tracks the shown index in
 * 			DRIVING_UNKNOWN_8630, using shifting state + 4 for automatics and
 * 			DRIVING_GEAR otherwise.
 *
 ******************************************************************************/
static void driving__updateGearStatus(void) {
	int16_t shifting = exe_readS16(DRIVING_PLAYER + DRIVING_SHIFTING);
	int16_t shown = exe_readS16(DRIVING_UNKNOWN_8630);

	if (shifting != 0) {
		if (shifting != shown) {
			driving__writeS16(DRIVING_UNKNOWN_8630, shifting);
			ui_statusGear((int16_t)(shifting + 4));
		}
	} else {
		int16_t gear = exe_readS16(DRIVING_PLAYER + DRIVING_GEAR);

		if (gear != shown) {
			driving__writeS16(DRIVING_UNKNOWN_8630, gear);
			ui_statusGear(gear);
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Advance police pursuit state and detect if the player is caught.
 *
 * \return	true when the police pull in front of the player (race ended).
 *
 * \details	Part of FUN_1000_da25 (0xdaa6..0xdaf4, 0xdb51..0xdb88).
 *
 ******************************************************************************/
static bool driving__updatePolice(void) {
	switch (exe_readS16(DRIVING_POLICE)) {
		case 1:
			if (exe_readS16(DRIVING_PLACE) >= exe_readS16(DRIVING_POLICE_AT)) {
				if (exe_readS16(DRIVING_UNKNOWN_7E8A) + 5 <=
					exe_readS16(DRIVING_PLACE)) {
					driving__writeS16(DRIVING_POLICE, 2);
					driving__spawnPolice();
				} else {
					driving__writeS16(DRIVING_POLICE, 0);
				}
			}
			break;

		case 2:
			if (exe_readS16(DRIVING_UNKNOWN_7E8A) + 15 >=
				exe_readS16(DRIVING_PLACE)) {
				driving__writeS16(DRIVING_POLICE, 3);
				driving__writeS16(DRIVING_SIREN, 1);
			}
			break;

		case 3:
			if (exe_readS16(DRIVING_UNKNOWN_7E8A) - 2 >
				exe_readS16(DRIVING_PLACE)) {
				uint16_t flags = exe_readU16(DRIVING_RACE_FLAGS);

				exe_writeU16(
					DRIVING_RACE_FLAGS,
					(uint16_t)(flags | DRIVING_FLAG_POLICE)
				);
				return true;
			}
			break;

		default:
			break;
	}

	return false;
}

/** ****************************************************************************
 *
 * \brief	Handle roadside crash and side-by-side car contact.
 *
 * \details	Part of FUN_1000_da25 (0xdb8b..0xdcb8). Ends the race on a roadside
 * 			crash unless actively chased by police, and resolves player left
 * 			nudges vs opponent random side-swipes when side by side.
 *
 ******************************************************************************/
static void driving__checkProximity(void) {
	if (exe_readS16(DRIVING_CARS_NEAR) == 0 || globals_demoMode != 0 ||
		exe_readS16(DRIVING_PLACE) >= driving__raceEnd()) {
		return;
	}

	if (exe_readS16(DRIVING_CRASHED) != 0 && exe_readS16(DRIVING_POLICE) <= 1) {
		exe_writeU16(
			DRIVING_RACE_FLAGS,
			(uint16_t)(exe_readU16(DRIVING_RACE_FLAGS) | DRIVING_FLAG_CRASH)
		);
		drive_stopCar(0);
		return;
	}

	if (exe_readS16(DRIVING_CAR_GAP) != 0 ||
		exe_readS16(DRIVING_PLAYER_STEP) <= 1) {
		if (exe_readS16(DRIVING_CAR_GAP) != 0) {
			driving__writeS16(DRIVING_BUMP_TICKS, 0);
		}
		return;
	}

	int16_t ticks = (int16_t)(exe_readS16(DRIVING_BUMP_TICKS) + 1);

	driving__writeS16(DRIVING_BUMP_TICKS, ticks);

	if (exe_readS16(DRIVING_PLAYER + DRIVING_STEERING) < 0) {
		int16_t place = exe_readS16(DRIVING_PLACE);
		int16_t midLane = (int16_t)((exe_readS16(DRIVING_UNKNOWN_70C0) +
									 exe_readS16(DRIVING_UNKNOWN_7D2E)) >>
									1);

		if (driving__roadCurve(place) <
				driving__roadCurve((int16_t)(place + 1)) &&
			midLane < exe_readS16(DRIVING_PLAYER_LAT)) {
			return;
		}

		driving__bumpSound();

		if (exe_readS16(DRIVING_UNKNOWN_54D2) == 0) {
			driving__writeS16(
				DRIVING_UNKNOWN_7E8A,
				(int16_t)(exe_readS16(DRIVING_UNKNOWN_7E8A) - 1)
			);
			driving__writeS16(DRIVING_UNKNOWN_54D2, 1);
		} else {
			driving__writeS16(DRIVING_UNKNOWN_54D2, 0);
		}
		return;
	}

	if (exe_readS16(DRIVING_RIVAL_ATTACK) != 0 && ticks > 3 &&
		exe_readS16(DRIVING_POLICE) < 2) {
		uint16_t adAt = exe_readU16(DRIVING_RIVAL_AD);
		int16_t	 drop = (int16_t)(int8_t)*exe_data(
			 (uint16_t)(adAt + DRIVING_RIVAL_AD_CURVE)
		 );

		if (random_next((int16_t)((drop >> 2) + 2)) == 0) {
			driving__bumpSound();
			driving__bumpSound();
			driving__writeS16(
				DRIVING_PLAYER_LAT,
				(int16_t)(exe_readS16(DRIVING_PLAYER_LAT) + 300)
			);
			if (exe_readS16(DRIVING_PLAYER + DRIVING_SPEED) > 20) {
				driving__writeS16(DRIVING_FORCED_BRAKE, 2);
			}
			driving__writeS16(DRIVING_RIVAL_ATTACK, 0);
			driving__writeS16(
				DRIVING_PLAYER + DRIVING_WHEEL,
				(int16_t)(exe_readS16(DRIVING_PLAYER + DRIVING_WHEEL) +
						  (random_next(2) == 0 ? -15 : 15))
			);
			driving__bumpSound();
		}

		driving__writeS16(DRIVING_BUMP_TICKS, 0);
	}
}

/** ****************************************************************************
 *
 * \brief	Sound the bump of two cars (BumpSound, 1f38:790f).
 *
 ******************************************************************************/
static void driving__bumpSound(void) {
	effects_bump();
}

/** ****************************************************************************
 *
 * \brief	Signed 16-bit absolute value.
 *
 * \param	value			Signed value.
 *
 * \return	Non-negative magnitude of value.
 *
 ******************************************************************************/
static int16_t driving__magnitude(
	int16_t value
) {
	return value < 0 ? (int16_t)(-value) : value;
}

/** ****************************************************************************
 *
 * \brief	Finish line tile for the active race kind.
 *
 * \return	DRIVING_DRAG_END (0x17c) for drag races, DRIVING_ROAD_END (0x3fc)
 * 			for road races.
 *
 ******************************************************************************/
static int16_t driving__raceEnd(void) {
	return exe_readS16(DRIVING_KIND) == 0 ? DRIVING_DRAG_END
										  : DRIVING_ROAD_END;
}
