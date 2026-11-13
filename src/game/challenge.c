/** ****************************************************************************
 *
 * \file	challenge.c
 *
 * \brief	Meeting an opponent on the street: the kind of race and the bet.
 *
 * \details	Port of FUN_1000_9d6d and the functions it calls (FUN_1000_9bc5,
 * 			9524, d031). The player picks a drag race or a road race
 * 			(screen 0x16), then a bet (screen 0x12 for a drag race, 0x13
 * 			for a road race for pink slips); the opponent takes it or
 * 			not. Not ported yet: driving in FUN_1000_d031 (FUN_1000_8e2d,
 * 			1b27), which the whole port lacks so far.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/challenge.h"

#include <stdbool.h>
#include <stdint.h>

#include "game/cursor.h"
#include "game/driving.h"
#include "game/event.h"
#include "game/exe.h"
#include "game/game.h"
#include "game/globals.h"
#include "game/input.h"
#include "game/random.h"
#include "game/screen.h"
#include "game/state.h"
#include "game/street.h"
#include "game/ui.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define CHALLENGE_KIND_SCREEN 0x16	/**< Screen: drag race or road race. */
#define CHALLENGE_DRAG_SCREEN 0x12	/**< Screen: the bets of a drag race. */
#define CHALLENGE_ROAD_SCREEN 0x13	/**< Screen: the bet of a road race. */
#define CHALLENGE_WAIT		  3000	/**< Ticks event_wait waits. */
#define CHALLENGE_ANSWER	  800	/**< Ticks the player has to answer. */
#define CHALLENGE_ESCAPE	  -23	/**< event_wait: Esc. */
#define CHALLENGE_TRIES		  2		/**< Bets the player may offer. */
#define CHALLENGE_REPLIES	  5		/**< Refusals of each kind of race. */
#define CHALLENGE_ACCEPTS	  3		/**< Acceptances of each kind of race. */
#define CHALLENGE_DRAG_TIME	  0x444 /**< Game clock: a drag race. */
#define CHALLENGE_ROAD_TIME	  0x777 /**< Game clock: a road race. */

/*
 * Gas the car in use needs (STATE_CAR_FUEL): for any race, for a road
 * race, and for the drag race the car without a driver offers.
 */
#define CHALLENGE_GAS_ANY  7
#define CHALLENGE_GAS_ROAD 0x11
#define CHALLENGE_GAS_LAST 0x16

/*
 * Kinds of race (DS:0x7ec0).
 */
#define CHALLENGE_DRAG 0 /**< Drag race. */
#define CHALLENGE_ROAD 1 /**< Road race. */

/*
 * Bets (DS:0x79a2): what the events of screens 0x12 and 0x13 offer.
 */
#define CHALLENGE_BET_NONE 0 /**< No money (event 1). */
#define CHALLENGE_BET_10   1 /**< $10 (event 2). */
#define CHALLENGE_BET_50   2 /**< $50 (event 3). */
#define CHALLENGE_BET_25   3 /**< $25 (event 7). */
#define CHALLENGE_BET_100  4 /**< $100 (event 8). */
#define CHALLENGE_BET_PINK 5 /**< Pink slips (event 9). */

/*
 * DS variables (kept in the data segment image, as the race and what
 * follows it read them).
 */
#define CHALLENGE_KIND		0x7ec0 /**< s16: kind of race. */
#define CHALLENGE_BET		0x79a2 /**< s16: the bet taken. */
#define CHALLENGE_STAKE		0x79a4 /**< u16: dollars bet. */
#define CHALLENGE_EVENT		0x8278 /**< s16: the last bet event. */
#define CHALLENGE_ROAD_NEXT 0x4976 /**< s16: the last race is a road race. */
#define CHALLENGE_POLICE	0x7f88 /**< s16: police in a road race. */
#define CHALLENGE_POLICE_AT 0x755a /**< s16: where they come (unknown). */
#define CHALLENGE_DRIVE_A	0x79a6 /**< s16: cleared before driving. */
#define CHALLENGE_DRIVE_B	0x8aca /**< u8: set before driving. */
#define CHALLENGE_DISTANCE	0x7d30 /**< s16: set by driving (unknown). */
#define CHALLENGE_FAR		200	   /**< DS:0x7d30 above it wears the car. */

/*
 * The opponent (set by FUN_1000_98ba on the street).
 */
#define CHALLENGE_RECORD	0x7d84 /**< u16: DS offset of its ad record. */
#define CHALLENGE_MODEL		0x7648 /**< s16: its car model. */
#define CHALLENGE_MODEL_AT	0x7d7a /**< u16: DS offset of the model. */
#define CHALLENGE_NO_DRIVER 0x7d40 /**< s16: the car has no driver. */
#define CHALLENGE_AD_LEVEL	0	   /**< Ad record: s8, row of the margins. */
#define CHALLENGE_AD_RACES	12	   /**< Ad record: s8, races against us. */
#define CHALLENGE_AD_WINS	13	   /**< Ad record: s8, probably races won. */

/*
 * Tables (DS offsets, from hot_data; see docs/formats.md).
 */
#define CHALLENGE_POWER	  0x7d68 /**< u16[3][3]: by class and engine. */
#define CHALLENGE_MARGINS 0x8bae /**< s16[][3]: by level and bet column. */
#define CHALLENGE_ODDS	  0x8dd8 /**< s16[4][3]: by bet row and power. */
#define CHALLENGE_NO	  0x51fc /**< u16[2][5]: refusals (messages). */
#define CHALLENGE_YES	  0x5210 /**< u16[2][3]: acceptances. */

/*
 * Messages (offsets from DS:0x2c02).
 */
#define CHALLENGE_MSG_AMATEUR 0x1113 /**< "I don't got time for amateurs". */
#define CHALLENGE_MSG_OK	  0x113d /**< "You're doing OK kid". */
#define CHALLENGE_MSG_DRAGS	  0x117d /**< Win more drag races. */
#define CHALLENGE_MSG_ROADS	  0x11a7 /**< Win more road races. */
#define CHALLENGE_MSG_DRAG	  0x11d1 /**< "Let's hit the drag race". */
#define CHALLENGE_MSG_PINK	  0x11f7 /**< "Get your pink slip out". */
#define CHALLENGE_MSG_CHICKEN 0x122c /**< "Chicken!" */
#define CHALLENGE_MSG_BROKE	  0x131b /**< Not enough dough for the bet. */
#define CHALLENGE_MSG_GAS	  0x1761 /**< "You're gonna hafta get more gas". */

/*
 * Tire wear added after a drive (STATE_PART_KIND of the tires) for a
 * drag or road race, by the tire subtype: 0, else other.
 */
#define CHALLENGE_WEAR_DRAG		  300
#define CHALLENGE_WEAR_ROAD		  700
#define CHALLENGE_WEAR_DRAG_OTHER 200
#define CHALLENGE_WEAR_ROAD_OTHER 400
#define CHALLENGE_WEAR_RANDOM	  300
#define CHALLENGE_WEAR_MAX		  10000

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

static int16_t challenge__wait(void);

static uint16_t challenge__answer(
	bool	yes,
	int16_t pick
);

static void challenge__betOf(
	int16_t	   event,
	int16_t *  pBet,
	uint16_t * pStake
);

static int16_t challenge__classGroup(
	int16_t model
);

static bool challenge__accepts(
	int16_t bet
);

static void challenge__prepare(void);

static int16_t challenge__begin(void);

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Meet the opponent on the street: offer a race and a bet.
 *
 * \param	noDriver		The car has no driver (DS:0x7d40).
 *
 * \return	2 when a race follows (its kind in DS:0x7ec0, the bet in
 * 			DS:0x79a2 and DS:0x79a4), else 0.
 *
 * \details	Port of FUN_1000_9d6d. The car without a driver offers a race
 * 			only when street_rank is 0 (no opponent left): a drag race,
 * 			or a road race for pink slips when DS:0x4976 is set; else it
 * 			says what the player still has to do. With a driver the
 * 			player picks the kind of race and has two tries at a bet the
 * 			opponent takes. The sprite memory bookkeeping
 * 			(FUN_1f38_a07e) is left out.
 *
 ******************************************************************************/
int16_t challenge_meet(
	int16_t noDriver
) {
	uint16_t car = exe_readU16(STATE_CAR);
	int16_t	 gas = exe_readS16((uint16_t)(car + STATE_CAR_FUEL));

	if (noDriver != 0) {
		int16_t	 rank = street_rank();
		uint16_t message = 0;

		if (rank == 0) {
			if (exe_readS16(CHALLENGE_ROAD_NEXT) == 0) {
				if (gas < CHALLENGE_GAS_LAST) {
					game_message(CHALLENGE_MSG_GAS);

					return 0;
				}

				exe_writeU16(CHALLENGE_KIND, CHALLENGE_DRAG);
				game_message(CHALLENGE_MSG_DRAG);
				exe_writeU16(CHALLENGE_BET, CHALLENGE_BET_NONE);
			} else {
				exe_writeU16(CHALLENGE_KIND, CHALLENGE_ROAD);
				game_message(CHALLENGE_MSG_PINK);
				exe_writeU16(CHALLENGE_BET, CHALLENGE_BET_PINK);
			}

			exe_writeU16(CHALLENGE_STAKE, 0);

			return challenge__begin();
		}

		if (rank == 2) {
			message = CHALLENGE_MSG_AMATEUR;
		} else if (rank == 3) {
			message = CHALLENGE_MSG_OK;
		} else if (rank == 4) {
			message = CHALLENGE_MSG_DRAGS;
		} else if (rank == 5) {
			message = CHALLENGE_MSG_ROADS;
		}

		if (message != 0) {
			game_message(message);
		}

		return 0;
	}

	if (gas < CHALLENGE_GAS_ANY) {
		game_message(CHALLENGE_MSG_GAS);

		return 0;
	}

	int saved = ui_saveScreenRect(CHALLENGE_DRAG_SCREEN);

	ui_open(CHALLENGE_KIND_SCREEN);

	int16_t event = challenge__wait();

	if (event == CHALLENGE_ESCAPE || event == EVENT_TIMEOUT) {
		game_message(CHALLENGE_MSG_CHICKEN);
	} else {
		/*
		 * Only events 1 and 2 are expected; the original keeps an
		 * uninitialized screen and DS:0x7ec0 for others.
		 */
		int16_t betScreen = CHALLENGE_DRAG_SCREEN;

		if (event == 1) {
			exe_writeU16(CHALLENGE_KIND, CHALLENGE_DRAG);
		} else if (event == 2) {
			betScreen = CHALLENGE_ROAD_SCREEN;
			exe_writeU16(CHALLENGE_KIND, CHALLENGE_ROAD);
		}

		int16_t kind = exe_readS16(CHALLENGE_KIND);

		if (kind == CHALLENGE_ROAD && gas < CHALLENGE_GAS_ROAD) {
			game_message(CHALLENGE_MSG_GAS);
		} else {
			int16_t	 lastReply = -1;
			int16_t	 bet = CHALLENGE_BET_NONE;
			uint16_t stake = 0;

			ui_close(1);
			ui_open(betScreen);

			for (int16_t tries = 0; tries < CHALLENGE_TRIES; tries++) {
				event = challenge__wait();
				exe_writeU16(CHALLENGE_EVENT, (uint16_t)event);

				if (event == CHALLENGE_ESCAPE || event == EVENT_TIMEOUT) {
					game_message(CHALLENGE_MSG_CHICKEN);

					break;
				}

				challenge__betOf(event, &bet, &stake);

				uint16_t message;

				if ((int32_t)exe_readU32(STATE_MONEY) < (int32_t)stake) {
					message = CHALLENGE_MSG_BROKE;
				} else if (challenge__accepts(bet)) {
					exe_writeU16(CHALLENGE_BET, (uint16_t)bet);
					exe_writeU16(CHALLENGE_STAKE, stake);
					ui_close(1);
					screen_restoreRect(saved, true);

					int16_t yes = random_next(CHALLENGE_ACCEPTS);

					game_message(challenge__answer(true, yes));

					return challenge__begin();
				} else {
					int16_t reply;

					do {
						reply = random_next(CHALLENGE_REPLIES);
					} while (reply == lastReply);

					message = challenge__answer(false, reply);
					lastReply = reply;
				}

				game_message(message);
			}
		}
	}

	ui_close(1);
	screen_restoreRect(saved, true);

	return 0;
}

/** ****************************************************************************
 *
 * \brief	A message the opponent answers a bet with.
 *
 * \param	yes				true: the bet is taken (DS:0x5210); false: it is not
 * 							(DS:0x51fc).
 * \param	pick			Which answer to the kind of race (DS:0x7ec0).
 *
 * \return	The message, an offset from DS:0x2c02.
 *
 * \details	Part of FUN_1000_9d6d.
 *
 ******************************************************************************/
static uint16_t challenge__answer(
	bool	yes,
	int16_t pick
) {
	int16_t	 kind = exe_readS16(CHALLENGE_KIND);
	uint16_t table = yes ? CHALLENGE_YES : CHALLENGE_NO;
	int16_t	 count = yes ? CHALLENGE_ACCEPTS : CHALLENGE_REPLIES;

	return exe_readU16((uint16_t)(table + (kind * count + pick) * 2));
}

/** ****************************************************************************
 *
 * \brief	Wait for the player's answer, 800 ticks at most.
 *
 * \return	The event (EVENT_TIMEOUT when the time ran out).
 *
 * \details	Part of FUN_1000_9d6d.
 *
 ******************************************************************************/
static int16_t challenge__wait(void) {
	int16_t event;

	globals_timeout = globals_ticks + CHALLENGE_ANSWER;

	do {
		event = event_wait(CHALLENGE_WAIT);
	} while (event == 0);

	return event;
}

/** ****************************************************************************
 *
 * \brief	The bet an event of screen 0x12 or 0x13 offers.
 *
 * \param	event			The event.
 * \param	pBet			In/out: the bet (CHALLENGE_BET_*).
 * \param	pStake			In/out: the dollars bet.
 *
 * \details	Part of FUN_1000_9d6d. Other events keep the bet before.
 *
 ******************************************************************************/
static void challenge__betOf(
	int16_t	   event,
	int16_t *  pBet,
	uint16_t * pStake
) {
	if (event == 1) {
		*pBet = CHALLENGE_BET_NONE;
		*pStake = 0;
	} else if (event == 2) {
		*pBet = CHALLENGE_BET_10;
		*pStake = 10;
	} else if (event == 3) {
		*pBet = CHALLENGE_BET_50;
		*pStake = 50;
	} else if (event == 7) {
		*pBet = CHALLENGE_BET_25;
		*pStake = 25;
	} else if (event == 8) {
		*pBet = CHALLENGE_BET_100;
		*pStake = 100;
	} else if (event == 9) {
		*pBet = CHALLENGE_BET_PINK;
		*pStake = 0;
	}
}

/** ****************************************************************************
 *
 * \brief	The class group of a car model.
 *
 * \param	model			Car model index.
 *
 * \return	0 for class 0..2, 1 for 3..6, else 2.
 *
 * \details	Port of FUN_1000_9524 (it also returns the high word of
 * 			model * 10 in DX, which is not used).
 *
 ******************************************************************************/
static int16_t challenge__classGroup(
	int16_t model
) {
	uint8_t modelClass = *exe_data(
		(uint16_t)(STATE_MODELS + model * STATE_MODEL_SIZE + STATE_MODEL_CLASS)
	);

	if (modelClass < 3) {
		return 0;
	}

	if (modelClass < 7) {
		return 1;
	}

	return 2;
}

/** ****************************************************************************
 *
 * \brief	Whether the opponent takes the bet.
 *
 * \param	bet				The bet (CHALLENGE_BET_*).
 *
 * \return	true when the opponent takes it.
 *
 * \details	Port of FUN_1000_9bc5. The power of both cars comes from the
 * 			table at DS:0x7d68 by class group and engine (the
 * 			opponent's: bits 8..10 of the model's fitted parts; the
 * 			player's: the subtype of the engine part). With the
 * 			opponent's margin (DS:0x8bae by ad record byte 0) the player
 * 			is stronger (column 0), as strong (1) or weaker (2); that
 * 			column picks the percent chance from DS:0x8dd8. It grows by a
 * 			quarter for each race the opponent won more than lost, by 6
 * 			or 4 when the car has body mods (bits 0..4 of
 * 			STATE_CAR_FLAGS; 4 with bit 0) and in the demo.
 *
 ******************************************************************************/
static bool challenge__accepts(
	int16_t bet
) {
	int16_t column = 0;
	int16_t row = 0;

	if (bet == CHALLENGE_BET_PINK) {
		column = 2;
		row = 3;
	} else if (bet == CHALLENGE_BET_10 || bet == CHALLENGE_BET_50) {
		column = 1;
		row = 1;
	} else if (bet == CHALLENGE_BET_25 || bet == CHALLENGE_BET_100) {
		column = 1;
		row = 2;
	}

	uint16_t record = exe_readU16(CHALLENGE_RECORD);
	uint16_t car = exe_readU16(STATE_CAR);
	int8_t	 level = (int8_t)*exe_data((uint16_t)(record + CHALLENGE_AD_LEVEL));
	int16_t	 theirGroup = challenge__classGroup(exe_readS16(CHALLENGE_MODEL));
	int16_t	 ourGroup = challenge__classGroup(
		 exe_readS16((uint16_t)(car + STATE_CAR_MODEL))
	 );
	uint16_t fitted = exe_readU16(
		(uint16_t)(exe_readU16(CHALLENGE_MODEL_AT) + STATE_MODEL_FITTED)
	);
	uint16_t engine = exe_readU16((uint16_t)(car + STATE_CAR_ENGINE));
	int16_t	 enginePart = exe_readS16((uint16_t)(engine + STATE_PART_VALUE));
	uint16_t engineAt =
		(uint16_t)(STATE_PARTS + enginePart * 8 + STATE_PARTS_SUBTYPE);
	int8_t	 ourEngine = (int8_t)*exe_data(engineAt);
	uint16_t theirAt = (uint16_t)(CHALLENGE_POWER + theirGroup * 6 +
								  ((fitted >> 8) & 7) * 2);
	uint16_t theirPower = exe_readU16(theirAt);
	uint16_t ourPower = exe_readU16(
		(uint16_t)(CHALLENGE_POWER + ourGroup * 6 + ourEngine * 2)
	);
	uint16_t margin =
		exe_readU16((uint16_t)(CHALLENGE_MARGINS + level * 6 + column * 2));
	int16_t strength;

	if ((uint16_t)(margin + theirPower) < ourPower) {
		strength = 0;
	} else if (ourPower < theirPower) {
		strength = 2;
	} else {
		strength = 1;
	}

	int16_t chance = exe_readS16(
		(uint16_t)(CHALLENGE_ODDS + row * 6 + strength * 2)
	);
	uint16_t mods = exe_readU16((uint16_t)(car + STATE_CAR_UNKNOWN_24));
	int16_t	 modBonus = 0;

	if ((mods & 0x1f00) != 0) {
		modBonus = (mods & 0x100) == 0 ? 6 : 4;
	}

	int8_t	races = (int8_t)*exe_data((uint16_t)(record + CHALLENGE_AD_RACES));
	int8_t	wins = (int8_t)*exe_data((uint16_t)(record + CHALLENGE_AD_WINS));
	int16_t lead = (int16_t)((int16_t)(wins * 2 - races) * chance);

	/*
	 * The original divides by 4 towards zero (absolute value, shift,
	 * sign back).
	 */
	chance = (int16_t)(chance + lead / 4 + modBonus);

	if (globals_demoMode != 0) {
		chance = (int16_t)(chance + (chance >> 2) + 10);
	}

	return random_next(100) <= chance;
}

/** ****************************************************************************
 *
 * \brief	Prepare the race after the opponent took the bet.
 *
 * \details	Port of FUN_1000_d031. Both cars are set to the start
 * 			(driving_reset, driving_setupOpponent) and the race is
 * 			driven (driving_run); FUN_1000_1b27 after it is not ported.
 * 			A road race with a driver brings the police with a
 * 			chance of 1 in 4 (DS:0x7f88, DS:0x755a). After a drive longer
 * 			than 200 (DS:0x7d30, set by driving) STATE_CAR_UNKNOWN_20 may
 * 			change and the tires wear.
 *
 ******************************************************************************/
static void challenge__prepare(void) {
	cursor_set(CURSOR_HIDE_NOW);
	cursor_set(1);
	input_flush();
	driving_reset(DRIVING_PLAYER);
	driving_setupOpponent();

	int16_t kind = exe_readS16(CHALLENGE_KIND);

	if (kind == CHALLENGE_ROAD && exe_readS16(CHALLENGE_NO_DRIVER) == 0) {
		int16_t police = random_next(4);

		exe_writeU16(CHALLENGE_POLICE, police == 0);

		if (police == 0) {
			exe_writeU16(
				CHALLENGE_POLICE_AT,
				(uint16_t)(random_next(0xff) + 0x1fe)
			);
		}
	} else {
		exe_writeU16(CHALLENGE_POLICE, 0);
	}

	exe_writeU16(CHALLENGE_DRIVE_A, 0);
	*exe_data(CHALLENGE_DRIVE_B) = 1;
	driving_run();
	globals_timeout = 0;
	input_flush();
	ui_statusTitle(exe_text(exe_readU16(STATE_STATUS_TITLE)));

	if (exe_readS16(CHALLENGE_DISTANCE) <= CHALLENGE_FAR) {
		return;
	}

	uint16_t  car = exe_readU16(STATE_CAR);
	uint8_t * pTuning = exe_data((uint16_t)(car + STATE_CAR_UNKNOWN_20));

	if (random_next(3) >= 1) {
		int8_t tuning = (int8_t)*pTuning;
		int8_t next;

		if (tuning >= 1) {
			next = tuning < random_next(4) ? (int8_t)random_next(4) : tuning;
		} else if (tuning < 0) {
			next = tuning <= -random_next(8) ? tuning : (int8_t)-random_next(8);
		} else {
			next = (int8_t)(random_next(8) - 4);
		}

		*pTuning = (uint8_t)next;
	}

	uint16_t tires = exe_readU16((uint16_t)(car + STATE_CAR_TIRES));
	int16_t	 extra = random_next(CHALLENGE_WEAR_RANDOM);
	int16_t	 tirePart = exe_readS16((uint16_t)(tires + STATE_PART_VALUE));
	int8_t	 subtype = (int8_t)*exe_data(
		  (uint16_t)(STATE_PARTS + tirePart * 8 + STATE_PARTS_SUBTYPE)
	  );
	int16_t wear;

	if (subtype == 0) {
		wear = kind == CHALLENGE_ROAD ? CHALLENGE_WEAR_ROAD
									  : CHALLENGE_WEAR_DRAG;
	} else {
		wear = kind == CHALLENGE_ROAD ? CHALLENGE_WEAR_ROAD_OTHER
									  : CHALLENGE_WEAR_DRAG_OTHER;
	}

	uint16_t wearAt = (uint16_t)(tires + STATE_PART_KIND);

	wear = (int16_t)(exe_readS16(wearAt) + wear + extra);

	if (wear > CHALLENGE_WEAR_MAX) {
		wear = CHALLENGE_WEAR_MAX;
	}

	exe_writeU16(wearAt, (uint16_t)wear);
}

/** ****************************************************************************
 *
 * \brief	Start the race the opponent took.
 *
 * \return	2.
 *
 * \details	The end of FUN_1000_9d6d (two copies): FUN_1000_d031, then
 * 			the game clock moves on by 0x444 for a drag race or 0x777
 * 			for a road race.
 *
 ******************************************************************************/
static int16_t challenge__begin(void) {
	challenge__prepare();
	globals_gameTime += exe_readS16(CHALLENGE_KIND) == CHALLENGE_DRAG
		? CHALLENGE_DRAG_TIME
		: CHALLENGE_ROAD_TIME;

	return 2;
}
