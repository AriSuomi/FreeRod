/** ****************************************************************************
 *
 * \file	race.c
 *
 * \brief	After a race on the street: the result, the money, the car.
 *
 * \details	Port of FUN_1000_c613 and the functions it calls (FUN_1000_bed4,
 * 			bf94, c1d8, c497, c52c, c5ac, 4ffa, FUN_1f38_7728). The race
 * 			itself is driving (FUN_1000_d031 > 8e2d > FUN_1f38_7b22), which
 * 			is not ported: it leaves the outcome in DS:0x799c (lost,
 * 			crashed, broken parts, police, jumped the light) and DS:0x7a9e
 * 			(bit 0: won), the speed at the end in DS:0x78e0 and the times
 * 			in DS:0x8bc4, 0x7644 and 0x82c8. Until driving is ported, the
 * 			test option --race-result gives the outcome (race_standIn).
 * 			Not ported: the sounds (FUN_1f38_7841, 7863) and the car
 * 			drawn by the driving code on the crash picture
 * 			(FUN_1f38_1094).
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/race.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dos/fpu.h"
#include "game/cars.h"
#include "game/cursor.h"
#include "game/driving.h"
#include "game/event.h"
#include "game/exe.h"
#include "game/game.h"
#include "game/garage.h"
#include "game/globals.h"
#include "game/input.h"
#include "game/lib.h"
#include "game/random.h"
#include "game/screen.h"
#include "game/state.h"
#include "game/text.h"
#include "game/ui.h"
#include "game/video.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

/*
 * The outcome of driving (DS:0x799c).
 */
#define RACE_FLAGS	 0x799c /**< u16: what went wrong; 0 if nothing. */
#define RACE_LOST	 0x01	/**< The opponent was first. */
#define RACE_ENGINE	 0x04	/**< The engine blew. */
#define RACE_TIRES	 0x08	/**< The tires blew. */
#define RACE_GEARBOX 0x10	/**< The transmission dropped. */
#define RACE_POLICE	 0x40	/**< Stopped by the police. */
#define RACE_JUMPED	 0x81	/**< The whole value: jumped the light. */

/*
 * Flag values that mean the car broke down (engine, transmission or both),
 * not a crash.
 */
#define RACE_BROKE_ENGINE  0x05
#define RACE_BROKE_GEARBOX 0x11
#define RACE_BROKE_BOTH	   0x15

/*
 * Other DS variables written by driving (or cleared here for it).
 */
#define RACE_WON_FLAGS	 0x7a9e	  /**< u16: bit 0, the player won. */
#define RACE_SPEED		 0x78e0	  /**< s16: speed (mph) at the end. */
#define RACE_TIRES_BLOWN 0x787c	  /**< u8: the tires blew (DS:0x78ea). */
#define RACE_TIME		 0x8bc4	  /**< float: seconds driven. */
#define RACE_AVERAGE	 0x7644	  /**< s16: average speed (mph). */
#define RACE_COVERED	 0x82c8	  /**< s16: percent of the distance. */
#define RACE_CLEARED_A	 0x53e6	  /**< s16: cleared (driving; unknown). */
#define RACE_CLEARED_B	 0x82be	  /**< s16: cleared (driving; unknown). */
#define RACE_CLEARED_C	 0x82bc	  /**< s16: cleared (driving; unknown). */
#define RACE_VIEW_SET	 0x7900	  /**< s16: unknown; if set ... */
#define RACE_VIEW		 0x7904	  /**< s16: ... this becomes 1. */
#define RACE_SIREN		 0x58dc	  /**< s16: set by FUN_1f38_7728. */
#define RACE_CAR_VIEW	 0x82c2	  /**< s16: car FUN_1f38_1094 draws. */
#define RACE_UNKNOWN_254 0x254	  /**< s16: 0: crash picture 1 row lower. */
#define RACE_SALE_FACTOR 0x6964	  /**< 8-byte real 0.4: sale factor. */
#define RACE_PART_USE	 2		  /**< Part record: word that grows with \
									 driving (STATE_PART_KIND). */
#define RACE_USE_MAX	   0x26ac /**< Most RACE_PART_USE keeps. */
#define RACE_CAR_WEAR_FROM 0x14	  /**< Car record: first wear byte. */
#define RACE_ENGINE_WEAR   0x16	  /**< Car record: engine wear bytes ... */
#define RACE_ENGINE_WEAR_N 10	  /**< ... 0x16..0x1f cleared. */

/*
 * The bet and the opponent (set by challenge.c and the street).
 */
#define RACE_KIND		0x7ec0 /**< s16: 0 drag race, 1 road race. */
#define RACE_BET		0x79a2 /**< s16: the bet taken. */
#define RACE_STAKE		0x79a4 /**< u16: dollars bet. */
#define RACE_BET_PINK	5	   /**< Bet: pink slips. */
#define RACE_NO_DRIVER	0x7d40 /**< s16: the car has no driver. */
#define RACE_AD			0x7d84 /**< u16: DS offset of the opponent's ad. */
#define RACE_AD_INDEX	0x7f82 /**< s16: index of the opponent's ad. */
#define RACE_MODEL		0x7648 /**< s16: the opponent's car model. */
#define RACE_MODEL_AT	0x7d7a /**< u16: DS offset of that model. */
#define RACE_AD_PAINT	10	   /**< Ad record: u8 paint. */
#define RACE_AD_STATE	11	   /**< Ad record: s8, -1 gone. */
#define RACE_AD_BODY	14	   /**< Ad record: u8, bits 0..2 body mods. */
#define RACE_AD_STICKER 15	   /**< Ad record: u8, bits 0..4 sticker. */
#define RACE_AD_NEW_CAR 16	   /**< Ad record: u8, got a new car. */
#define RACE_GONE		0xff   /**< RACE_AD_STATE: gone. */

/*
 * Gas a race uses.
 */
#define RACE_GAS_DRAG 5
#define RACE_GAS_ROAD 15

/*
 * Screens.
 */
#define RACE_SCREEN		   1	  /**< Open while the result shows. */
#define RACE_TIMES_SCREEN  0x21	  /**< The times of the race. */
#define RACE_STATUS_SCREEN 0x18	  /**< One hotspot over the whole screen. */
#define RACE_PARTS_SCREEN  0x19	  /**< Tow: pay for parts, or sell. */
#define RACE_FIX_SCREEN	   0x1a	  /**< Tow: pay to get it running, or sell. */
#define RACE_KEY_MAPS	   0x579f /**< u16[]: key map of each screen. */
#define RACE_PAY_COLOR_A   0x1b40 /**< u16: color of the pay button ... */
#define RACE_PAY_COLOR_B   0x1ac2 /**< ... and of its text. */
#define RACE_BROKE_COLOR   0xe	  /**< The colors when the player is broke. */
#define RACE_CAN_PAY_COLOR 10	  /**< The colors otherwise. */
#define RACE_PAY		   1	  /**< Tow screen event: pay. */
#define RACE_SELL		   2	  /**< Tow screen event: sell the car. */
#define RACE_TOW_WAIT	   3000	  /**< Ticks event_wait waits. */

/*
 * Times and texts (DS offsets).
 */
#define RACE_LINE_1			0x2b3f /**< char[51]: first line of the times. */
#define RACE_LINE_1_SIZE	0x33
#define RACE_LINE_2			0x2b72 /**< char[144]: second line. */
#define RACE_LINE_2_SIZE	0x90
#define RACE_FMT_WON		0x53e8 /**< "Your winning time %2.1f sec., ..." */
#define RACE_FMT_LOST		0x542d /**< "In %2.1f sec you covered only ..." */
#define RACE_FMT_LOST_2		0x5451 /**< "of the distance. Your avg. ..." */
#define RACE_TEXT_CONGRAT	0x541b /**< "Congratulations !" */
#define RACE_CONGRAT_SIZE	18
#define RACE_TIME_SPEC		"%2.1f" /**< The 8087 number in the formats. */
#define RACE_FORMAT_MAX		80

#define RACE_TIRES_LINE		0x2aea /**< char[13]: "Plus tires !" or "". */
#define RACE_TEXT_TIRES		0x54a4 /**< "Plus tires !" */
#define RACE_TIRES_SIZE		13
#define RACE_TEXT_PARTS		0x2ab2 /**< "$123 for parts and ..." */
#define RACE_TEXT_FIX		0x2b09 /**< "Damn. It'll cost $123 ..." */
#define RACE_TEXT_JUNK		0x4115 /**< "Lucky you weren't killed ..." */
#define RACE_FMT_FINE		0x547b /**< "You have been fined $%2d ..." */
#define RACE_TEXT_JAILED	0x54a1 /**< Status after the sentence. */
#define RACE_TEXT_PAID		0x54a3 /**< Status after the fine. */
#define RACE_TEXT_MAX		40	   /**< Bytes of the fine text. */
#define RACE_MODEL_STOPS	0x53c4 /**< Characters that end a model name. */
#define RACE_MODEL_SKIP		7	   /**< Characters always kept of it. */
#define RACE_WIN_MESSAGES	0x53c8 /**< u16[6]: message for each bet. */
#define RACE_CRASH_FRAMES	0x53d4 /**< 3 x (resource, left, top). */
#define RACE_CRASH_FRAME_N	3
#define RACE_CRASH_FRAME_SZ 6

/*
 * Messages (offsets from DS:0x2c02).
 */
#define RACE_MSG_LUCK		 0x14bd	 /**< "Beginner's luck, punk!" */
#define RACE_MSG_GOOD		 0x14d6	 /**< "You ran a good race." */
#define RACE_MSG_DEBTS		 0x1793	 /**< "You've been sentenced for debts" */
#define RACE_MSG_JUMPED		 -0x134e /**< "Ya jumped the light" (not kept) */
#define RACE_MSG_WALK		 0x143a	 /**< "Take a walk, sucker." */
#define RACE_MSG_LOSE		 0x1427	 /**< "You lose, sucker." */
#define RACE_MSG_CLOSE		 0x1450	 /**< "Close, but no cigar punk!" */
#define RACE_MSG_CHOKE		 0x146c	 /**< "I knew you'd choke ..." */
#define RACE_MSG_PINK_TIRES	 0x16f6	 /**< "Blew your tires again?" */
#define RACE_MSG_PINK_LOST	 0x1658	 /**< "You lose. Take a walk, sucker." */
#define RACE_MSG_SLOW_TIRES	 0x1590	 /**< "Lucky you're so slow ..." */
#define RACE_MSG_SLOW		 0x1561	 /**< "Lucky you're so slow ..." */
#define RACE_MSG_JUNK		 0x1513	 /**< "Lucky you weren't killed ..." */
#define RACE_MSG_PINK_GEAR	 0x1678	 /**< "You just dropped your trans..." */
#define RACE_MSG_GEARBOX	 0x1608	 /**< "... transmission, klutz!" */
#define RACE_MSG_PINK_ENGINE 0x16bd	 /**< "Blew your engine again?" */
#define RACE_MSG_ENGINE		 0x1634	 /**< "Blew your engine again, huh" */

/*
 * Speeds (mph) at the end of a race.
 */
#define RACE_FINE_HIGH	 0x51 /**< From here the fine is RACE_FINE_3. */
#define RACE_FINE_MID	 0x33 /**< From here RACE_FINE_2, else _1. */
#define RACE_FINE_1		 20
#define RACE_FINE_2		 50
#define RACE_FINE_3		 75
#define RACE_SLOW_CRASH	 0x1f /**< Below: only a scratch. */
#define RACE_MID_CRASH	 0x3d /**< Below: RACE_FIX_SCREEN. */
#define RACE_BAD_CRASH	 0x5b /**< Below: RACE_PARTS_SCREEN; else junk. */
#define RACE_FAST_CRASH	 0x5a /**< Above: the crash is animated. */
#define RACE_FIX_RANGE	 0x14 /**< Random part of the cost to fix. */
#define RACE_PARTS_RANGE 0x28 /**< Random part of the cost of parts. */
#define RACE_TOW_BASE	 100  /**< Fixed part of both. */
#define RACE_JUNK_BASE	 0x13 /**< Junk value: body value / 10 + this. */

/*
 * Pictures and timing.
 */
#define RACE_TICKET			0x456  /**< Resource: the ticket. */
#define RACE_TICKET_BACK	0x4ef  /**< Resource: behind the ticket. */
#define RACE_TICKET_TOP		0x14   /**< Row of the ticket. */
#define RACE_TICKET_BACK_Y	0x45   /**< Row of the picture behind it. */
#define RACE_TICKET_STYLE	13	   /**< Drawing style of its texts. */
#define RACE_TICKET_TEXT_X	0x5e   /**< Column of its texts. */
#define RACE_TICKET_NAME_Y	0x48   /**< Row of the player's name. */
#define RACE_TICKET_MODEL_Y 99	   /**< Row of the car model. */
#define RACE_TICKET_MARGIN	8	   /**< Ticket width less text width. */
#define RACE_CRASH_TOP		0x4eb  /**< Resource: the crash scene. */
#define RACE_CRASH_BOTTOM	0x4ec  /**< Resource: below it. */
#define RACE_CRASH_SPLIT	0x59   /**< Row the scene is drawn in 2 parts. */
#define RACE_CRASH_Y		0xb	   /**< Row of the crash scene. */
#define RACE_CRASH_BOTTOM_Y 0x77   /**< Row of the part below it. */
#define RACE_CRASH_WAIT		20	   /**< Retraces before the first frame. */
#define RACE_CRASH_STEP		8	   /**< Fewer retraces each frame. */
#define RACE_CRASH_KEY		100	   /**< Ticks the crash picture stays. */
#define RACE_FINE_KEY		250	   /**< Ticks the fine stays. */
#define RACE_BUBBLE_TOP		0x49   /**< Row of the breakdown bubbles. */
#define RACE_TIRE_PRICE		0x4946 /**< s16: price of new tires. */
#define RACE_TIRE_PART		0x28   /**< Parts table index: new tires. */
#define RACE_TIRE_KIND		4	   /**< Parts table kind: tires. */
#define RACE_TIRE_COST		0x32   /**< Taken off the money for them. */
#define RACE_LIST			0x49e0 /**< s16: count of the spare list. */
#define RACE_MESSAGES		0x2c02 /**< DS: messages count from. */

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

static void race__limitUse(
	uint16_t slotAt
);

static int16_t race__won(
	int16_t	 bet,
	uint16_t stake
);

static void race__takeCar(void);

static int16_t race__fined(void);

static int16_t race__crashed(
	int16_t bet
);

static int16_t race__tow(
	int16_t	 screen,
	uint16_t textAt,
	int16_t	 range,
	bool	 tires
);

static void race__putAmount(
	uint16_t textAt,
	int16_t	 amount
);

static int16_t race__newTires(void);

static void race__ticket(void);

static void race__drawClipped(
	int16_t		 top,
	const char * pText,
	int			 maxLength,
	int16_t		 limit
);

static void race__crashPicture(void);

static void race__showTimes(void);

static void race__printTime(
	uint16_t lineAt,
	int		 size,
	uint16_t formatAt,
	int16_t	 number
);

static void race__engine(
	int16_t message
);

static void race__gearbox(
	int16_t message
);

static void race__bubble(
	int16_t message
);

static int16_t race__width(
	uint16_t gameId
);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/**
 * Test outcome (--race-result): bits 0..7 for DS:0x799c, bit 8 for bit 0 of
 * DS:0x7a9e; RACE_NO_TEST when not given.
 */
int32_t race_testFlags = RACE_NO_TEST;

int16_t race_testSpeed; /**< Test speed at the end (DS:0x78e0). */

/** Part records found by garage_listSpares (index 1 is the first). */
static uint16_t race__parts[STATE_PART_MAX + 1];

/** Their wear, which race__newTires does not use. */
static int8_t race__percents[STATE_PART_MAX + 1];

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Stand in for driving: give the outcome of the race.
 *
 * \retval	true			An outcome was given with --race-result and is
 * 							in DS:0x799c, 0x7a9e and 0x78e0.
 * \retval	false			No outcome: the caller keeps the race out.
 *
 * \details	Not part of the original. The cars' movement in a drive
 * 			(FUN_1000_da25) is not ported yet, so drive.c ends a race
 * 			with the outcome given here.
 *
 ******************************************************************************/
bool race_standIn(void) {
	if (race_testFlags == RACE_NO_TEST) {
		return false;
	}

	exe_writeU16(RACE_FLAGS, (uint16_t)(race_testFlags & 0xff));
	exe_writeU16(RACE_WON_FLAGS, (uint16_t)((race_testFlags >> 8) & 1));
	exe_writeU16(RACE_SPEED, (uint16_t)race_testSpeed);
	plat_log(
		"driving is not ported: outcome 0x%x at %d mph (--race-result)",
		(unsigned)race_testFlags,
		race_testSpeed
	);

	return true;
}

/** ****************************************************************************
 *
 * \brief	Show the result of a race and settle the bet.
 *
 * \return	RACE_HOME, RACE_TOWED, RACE_STREET, RACE_JAILED, or (after the
 * 			tow screen) its event number.
 *
 * \details	Port of FUN_1000_c613. Screen 1 is open while the result
 * 			shows. The race uses gas (5 for a drag race, 15 for a road
 * 			race) and the use words of the transmission, engine and
 * 			tires are kept at most 0x26ac. Then, in this order: a win
 * 			(the bet, the opponent's car for pink slips), the police
 * 			(a fine), jumping the light, a plain loss, a crash, a
 * 			breakdown. After a loss the stake is paid and a car bet for
 * 			pink slips is gone.
 *
 ******************************************************************************/
int16_t race_result(void) {
	int16_t	 bet = exe_readS16(RACE_BET);
	uint16_t stake = exe_readU16(RACE_STAKE);
	uint16_t flags = exe_readU16(RACE_FLAGS);
	bool	 engine = (flags & RACE_ENGINE) != 0;
	bool	 gearbox = (flags & RACE_GEARBOX) != 0;
	bool	 broke = flags == RACE_BROKE_ENGINE;
	uint16_t carAt = exe_readU16(STATE_CAR);

	broke = broke || flags == RACE_BROKE_GEARBOX;
	broke = broke || flags == RACE_BROKE_BOTH;
	race__limitUse((uint16_t)(carAt + STATE_CAR_GEARBOX));
	race__limitUse((uint16_t)(carAt + STATE_CAR_ENGINE));
	race__limitUse((uint16_t)(carAt + STATE_CAR_TIRES));
	exe_writeU16(RACE_WON, 0);
	exe_writeU16(RACE_CLEARED_A, 0);
	exe_writeU16(RACE_CLEARED_B, 0);
	exe_writeU16(RACE_CLEARED_C, 0);
	globals_clickKeyFlag = 0;
	globals_buttonsHeld = 0;

	if (exe_readS16(RACE_VIEW_SET) != 0) {
		exe_writeU16(RACE_VIEW, 1);
	}

	if (*exe_data(RACE_TIRES_BLOWN) != 0) {
		exe_writeU16(RACE_FLAGS, exe_readU16(RACE_FLAGS) | RACE_TIRES);
	}

	ui_open(RACE_SCREEN);

	int16_t gas = exe_readS16(RACE_KIND) == 0 ? RACE_GAS_DRAG : RACE_GAS_ROAD;
	int16_t left = exe_readS16((uint16_t)(carAt + STATE_CAR_FUEL));

	exe_writeU16(
		(uint16_t)(carAt + STATE_CAR_FUEL),
		(uint16_t)(gas < left ? left - gas : 0)
	);
	video_showPalette();

	if ((exe_readU16(RACE_WON_FLAGS) & 1) != 0) {
		return race__won(bet, stake);
	}

	if ((exe_readU16(RACE_FLAGS) & RACE_POLICE) != 0) {
		return race__fined();
	}

	if (exe_readU16(RACE_FLAGS) == RACE_JUMPED) {
		cursor_set(CURSOR_SHOW_NOW);
		game_message(RACE_MSG_JUMPED);
		ui_close(1);

		return RACE_STREET;
	}

	if (bet == RACE_BET_PINK) {
		cars_destroyCar(exe_readU16(STATE_CAR), 0);
		exe_writeU16(STATE_CAR, 0);
	}

	ui_addMoney((int16_t)-stake);

	if (exe_readU16(RACE_FLAGS) == RACE_LOST) {
		race__showTimes();
		cursor_set(CURSOR_SHOW_NOW);

		int16_t message = bet == RACE_BET_PINK ? RACE_MSG_WALK : RACE_MSG_LOSE;

		if (exe_readS16(RACE_NO_DRIVER) != 0) {
			message = exe_readS16(RACE_KIND) == 0 ? RACE_MSG_CLOSE
												  : RACE_MSG_CHOKE;
		}

		game_message(message);
		ui_close(1);

		return bet == RACE_BET_PINK ? RACE_TOWED : RACE_STREET;
	}

	if (!broke) {
		return race__crashed(bet);
	}

	cursor_set(CURSOR_SHOW_NOW);

	if (engine) {
		race__engine(
			bet == RACE_BET_PINK ? RACE_MSG_PINK_ENGINE : RACE_MSG_ENGINE
		);
	} else if (gearbox) {
		race__gearbox(
			bet == RACE_BET_PINK ? RACE_MSG_PINK_GEAR : RACE_MSG_GEARBOX
		);
	}

	ui_close(1);

	return RACE_TOWED;
}

/** ****************************************************************************
 *
 * \brief	Keep the use word of a part at most RACE_USE_MAX.
 *
 * \param	slotAt			DS offset of the car record's slot.
 *
 * \details	Part of FUN_1000_c613. An empty slot reads and, if needed,
 * 			writes DS:0x2 as the original does.
 *
 ******************************************************************************/
static void race__limitUse(
	uint16_t slotAt
) {
	uint16_t useAt = (uint16_t)(exe_readU16(slotAt) + RACE_PART_USE);

	if (exe_readS16(useAt) > RACE_USE_MAX) {
		exe_writeU16(useAt, RACE_USE_MAX);
	}
}

/** ****************************************************************************
 *
 * \brief	The player won: the times, the bet, the opponent's car.
 *
 * \param	bet				The bet taken.
 * \param	stake			Dollars bet.
 *
 * \return	RACE_TOWED when the tires blew and could not be paid for,
 * 			else RACE_HOME for pink slips and RACE_STREET otherwise.
 *
 * \details	Part of FUN_1000_c613. The opponent's message comes from the
 * 			table DS:0x53c8 by bet; the car without a driver has its own.
 *
 ******************************************************************************/
static int16_t race__won(
	int16_t	 bet,
	uint16_t stake
) {
	exe_writeU16(RACE_WON, 1);
	race__showTimes();
	cursor_set(CURSOR_SHOW_NOW);

	int16_t message;

	if (exe_readS16(RACE_NO_DRIVER) == 0) {
		message = exe_readS16((uint16_t)(RACE_WIN_MESSAGES + bet * 2));
	} else {
		message = exe_readS16(RACE_KIND) == 0 ? RACE_MSG_LUCK : RACE_MSG_GOOD;
	}

	game_message(message);
	ui_addMoney((int16_t)stake);

	if (bet == RACE_BET_PINK) {
		race__takeCar();
	}

	ui_close(1);

	if ((exe_readU16(RACE_FLAGS) & RACE_TIRES) != 0 && race__newTires() != 0) {
		return RACE_TOWED;
	}

	return bet == RACE_BET_PINK ? RACE_HOME : RACE_STREET;
}

/** ****************************************************************************
 *
 * \brief	Take the opponent's car after winning pink slips.
 *
 * \details	Part of FUN_1000_c613. The car is made like the opponent's
 * 			(its paint, sticker and body modifications from the ad;
 * 			tires used 0..499) and put in store; it becomes the car in
 * 			use when its class is above the player's car. The opponent
 * 			gets a new ad with a cheaper model once (not in the demo's
 * 			ad, not the car without a driver); otherwise the opponent
 * 			is gone.
 *
 ******************************************************************************/
static void race__takeCar(void) {
	uint16_t adAt = exe_readU16(RACE_AD);
	uint16_t modelAt = exe_readU16(RACE_MODEL_AT);
	uint16_t wonAt = cars_create(
		exe_readS16(RACE_MODEL),
		exe_readS16(modelAt),
		0
	);
	uint16_t  tiresAt = exe_readU16((uint16_t)(wonAt + STATE_CAR_TIRES));
	uint8_t * pAd = exe_data(adAt);
	uint8_t	  mods = pAd[RACE_AD_STICKER] & 0x1f;

	exe_writeU16(
		(uint16_t)(tiresAt + RACE_PART_USE),
		(uint16_t)random_next(500)
	);

	if ((pAd[RACE_AD_BODY] & 4) != 0) {
		mods |= 0x20;
	}

	if ((pAd[RACE_AD_BODY] & 2) != 0) {
		mods |= 0x40;
	}

	if ((pAd[RACE_AD_BODY] & 1) != 0) {
		mods |= 0x80;
	}

	*exe_data((uint16_t)(wonAt + STATE_CAR_FLAGS)) = mods;
	*exe_data((uint16_t)(wonAt + STATE_CAR_PAINT)) = pAd[RACE_AD_PAINT];

	uint16_t carAt = exe_readU16(STATE_CAR);

	uint8_t ownClass = *exe_data((uint16_t)(carAt + STATE_CAR_CLASS));
	uint8_t wonClass = *exe_data((uint16_t)(wonAt + STATE_CAR_CLASS));

	if (ownClass < wonClass) {
		uint16_t storedAt = exe_readU16(STATE_STORED_CARS);

		exe_writeU16(
			(uint16_t)(carAt + STATE_CAR_NEXT),
			exe_readU16((uint16_t)(storedAt + STATE_CAR_NEXT))
		);
		exe_writeU16(STATE_STORED_CARS, carAt);
		exe_writeU16(STATE_CAR, wonAt);
		driving_prepare(-1, DRIVING_PLAYER);
	}

	bool demoAd = exe_readS16(RACE_AD_INDEX) == exe_readS16(STATE_DEMO_AD);

	if (globals_demoMode != 0 && demoAd) {
		return;
	}

	if (pAd[RACE_AD_NEW_CAR] == 0 && exe_readS16(RACE_NO_DRIVER) == 0) {
		uint8_t level = *exe_data((uint16_t)(modelAt + STATE_MODEL_CLASS));
		int16_t models[STATE_MODEL_COUNT + 1];
		int16_t count = 0;

		for (int16_t model = 0; model < STATE_MODEL_COUNT; model++) {
			uint16_t recordAt =
				(uint16_t)(STATE_MODELS + model * STATE_MODEL_SIZE);

			if (*exe_data((uint16_t)(recordAt + STATE_MODEL_CLASS)) < level) {
				models[++count] = model;
			}
		}

		if (count > 0) {
			models[0] = count;
			game_makeAd(exe_readS16(RACE_AD_INDEX), models);
			pAd[RACE_AD_NEW_CAR] = 1;

			return;
		}
	}

	pAd[RACE_AD_STATE] = RACE_GONE;
}

/** ****************************************************************************
 *
 * \brief	Stopped by the police: the ticket and the fine.
 *
 * \return	RACE_STREET, or RACE_JAILED when the money does not cover the
 * 			fine.
 *
 * \details	Part of FUN_1000_c613. The fine is $20, $50 from 51 mph or
 * 			$75 from 81 mph, plus what blown tires could not be paid
 * 			with. A player who cannot pay loses all money.
 *
 ******************************************************************************/
static int16_t race__fined(void) {
	exe_writeU16(RACE_SIREN, 0);
	race__ticket();

	int16_t fine = 0;

	if ((exe_readU16(RACE_FLAGS) & RACE_TIRES) != 0) {
		fine = race__newTires();
	}

	int16_t speed = exe_readS16(RACE_SPEED);

	if (speed >= RACE_FINE_HIGH) {
		fine += RACE_FINE_3;
	} else if (speed >= RACE_FINE_MID) {
		fine += RACE_FINE_2;
	} else {
		fine += RACE_FINE_1;
	}

	char text[RACE_TEXT_MAX];

	ui_open(RACE_STATUS_SCREEN);
	event_wait(0);
	snprintf(text, sizeof(text), exe_text(RACE_FMT_FINE), fine);
	ui_status(-1, text, (int16_t)(PLAT_WIDTH - globals_statusLeft));
	event_waitAnyKey(RACE_FINE_KEY);

	if ((int32_t)exe_readU32(STATE_MONEY) < fine) {
		game_message(RACE_MSG_DEBTS);
		ui_close(1);
		ui_close(1);
		ui_addMoney((int16_t)-exe_readS16(STATE_MONEY));
		ui_status(
			-1,
			exe_text(RACE_TEXT_JAILED),
			(int16_t)(PLAT_WIDTH - globals_statusLeft)
		);

		return RACE_JAILED;
	}

	ui_addMoney((int16_t)-fine);

	uint32_t savedTimeout = globals_timeout;

	event_waitAnyKey(globals_messageTicks);
	globals_timeout = savedTimeout;
	ui_close(1);
	ui_status(
		-1,
		exe_text(RACE_TEXT_PAID),
		(int16_t)(PLAT_WIDTH - globals_statusLeft)
	);
	ui_close(1);

	return RACE_STREET;
}

/** ****************************************************************************
 *
 * \brief	The player crashed (or lost with blown tires).
 *
 * \param	bet				The bet taken.
 *
 * \return	RACE_HOME, RACE_TOWED, or what race__tow returns.
 *
 * \details	Part of FUN_1000_c613. After the crash picture: a car bet
 * 			for pink slips is already gone; below 31 mph only the tires
 * 			may need replacing; up to 60 mph the car can be fixed for
 * 			$100..119, up to 90 mph the parts cost $100..139, both
 * 			paid or the car sold as it is; from 91 mph the car is junk
 * 			and sold for its body value / 10 + 19.
 *
 ******************************************************************************/
static int16_t race__crashed(
	int16_t bet
) {
	race__crashPicture();

	bool tires = (exe_readU16(RACE_FLAGS) & RACE_TIRES) != 0;

	if (bet == RACE_BET_PINK) {
		if (tires) {
			game_message(RACE_MSG_PINK_TIRES);
			race__newTires();
		} else {
			game_message(RACE_MSG_PINK_LOST);
		}

		ui_close(1);

		return RACE_TOWED;
	}

	int16_t speed = exe_readS16(RACE_SPEED);

	if (speed >= RACE_BAD_CRASH) {
		int16_t value = cars_destroyCar(exe_readU16(STATE_CAR), 0);
		int16_t junk = (int16_t)(value / 10 + RACE_JUNK_BASE);

		exe_writeU16(STATE_CAR, 0);
		ui_addMoney(junk);
		race__putAmount(RACE_TEXT_JUNK, junk);
		input_flush();
		game_message(RACE_MSG_JUNK);
		ui_close(1);

		return RACE_TOWED;
	}

	if (speed >= RACE_MID_CRASH) {
		return race__tow(
			RACE_PARTS_SCREEN,
			RACE_TEXT_PARTS,
			RACE_PARTS_RANGE,
			tires
		);
	}

	if (speed >= RACE_SLOW_CRASH) {
		return race__tow(RACE_FIX_SCREEN, RACE_TEXT_FIX, RACE_FIX_RANGE, tires);
	}

	if (tires) {
		game_message(RACE_MSG_SLOW_TIRES);
		race__newTires();
		ui_close(1);

		return RACE_TOWED;
	}

	game_message(RACE_MSG_SLOW);
	ui_close(1);

	return RACE_HOME;
}

/** ****************************************************************************
 *
 * \brief	The tow truck: pay for the repair or sell the car as it is.
 *
 * \param	screen			RACE_PARTS_SCREEN or RACE_FIX_SCREEN.
 * \param	textAt			DS offset of its text with the cost.
 * \param	range			Random part of the cost.
 * \param	tires			The tires blew too ("Plus tires !").
 *
 * \return	RACE_HOME after paying, RACE_TOWED after selling, else the
 * 			event of the screen (screen 1 stays open then).
 *
 * \details	Part of FUN_1000_c613. A player who cannot pay sees the pay
 * 			button in color 14 and its keys changed in the screen's key
 * 			map. The car sells for 0.4 times its price (8087 code,
 * 			rounded toward 0).
 *
 ******************************************************************************/
static int16_t race__tow(
	int16_t	 screen,
	uint16_t textAt,
	int16_t	 range,
	bool	 tires
) {
	int16_t cost = (int16_t)(random_next(range) + RACE_TOW_BASE);

	race__putAmount(textAt, cost);

	if (tires) {
		memcpy(
			exe_data(RACE_TIRES_LINE),
			exe_data(RACE_TEXT_TIRES),
			RACE_TIRES_SIZE
		);
	} else {
		*exe_data(RACE_TIRES_LINE) = 0;
	}

	uint8_t * pKeys =
		exe_data(exe_readU16((uint16_t)(RACE_KEY_MAPS + screen * 2)));

	if ((int32_t)exe_readU32(STATE_MONEY) < cost) {
		exe_writeU16(RACE_PAY_COLOR_A, RACE_BROKE_COLOR);
		exe_writeU16(RACE_PAY_COLOR_B, RACE_BROKE_COLOR);
		pKeys[1] = 0xd0;
		pKeys[2] = 0x01;
		pKeys[3] = 0xd0;
	} else {
		exe_writeU16(RACE_PAY_COLOR_A, RACE_CAN_PAY_COLOR);
		exe_writeU16(RACE_PAY_COLOR_B, RACE_CAN_PAY_COLOR);
		pKeys[1] = 0xcf;
		pKeys[2] = 0x21;
		pKeys[3] = 0xcf;
	}

	ui_open(screen);
	input_flush();

	int16_t event;

	do {
		event = event_wait(RACE_TOW_WAIT);
	} while (event == 0);

	ui_close(1);

	if (event == RACE_PAY) {
		ui_addMoney((int16_t)-cost);

		if (tires) {
			race__newTires();
		}

		ui_close(1);

		return RACE_HOME;
	}

	if (event != RACE_SELL) {
		return event;
	}

	uint16_t carAt = exe_readU16(STATE_CAR);
	fpu_Real price = fpu_fromInt(exe_readS16(carAt));
	fpu_Real sale = fpu_mul(price, fpu_fromDouble(exe_data(RACE_SALE_FACTOR)));

	ui_addMoney((int16_t)fpu_toLong(sale));
	exe_writeU16(STATE_CAR, 0);
	ui_close(1);

	return RACE_TOWED;
}

/** ****************************************************************************
 *
 * \brief	Write an amount after the first "$" from a text on.
 *
 * \param	textAt			DS offset to search from (the "$" may be in
 * 							one of the texts that follow).
 * \param	amount			The dollars.
 *
 * \details	Part of FUN_1000_c613: the two characters after the first
 * 			digit become spaces, the digits are written after the "$"
 * 			and followed by a space (itoa, then its end replaced).
 *
 ******************************************************************************/
static void race__putAmount(
	uint16_t textAt,
	int16_t	 amount
) {
	uint8_t * pDollar = exe_data(textAt);

	while (*pDollar != '$') {
		pDollar++;
	}

	char digits[8];
	int	 length = snprintf(digits, sizeof(digits), "%d", amount);

	pDollar[2] = ' ';
	pDollar[3] = ' ';
	memcpy(pDollar + 1, digits, (size_t)length);
	pDollar[1 + length] = ' ';
}

/** ****************************************************************************
 *
 * \brief	Replace blown tires.
 *
 * \return	Dollars the player could not pay for new tires, else 0.
 *
 * \details	Port of FUN_1000_bed4. The tires are given back; spare tires
 * 			go on, else new ones are bought for $50 (taken off the money
 * 			even below 0, which is then set to 0).
 *
 ******************************************************************************/
static int16_t race__newTires(void) {
	uint16_t carAt = exe_readU16(STATE_CAR);
	int16_t	 owed = 0;

	if (carAt == 0) {
		return 0;
	}

	uint16_t slotAt = (uint16_t)(carAt + STATE_CAR_TIRES);
	uint16_t price = cars_disposePart(exe_readU16(slotAt), 0);

	exe_writeU16(carAt, (uint16_t)(exe_readU16(carAt) - price));
	exe_writeU16(slotAt, 0);
	garage_listSpares(RACE_TIRE_KIND, race__parts, race__percents, 0);

	uint16_t partAt;

	if (exe_readS16(RACE_LIST) == 0) {
		int32_t money = (int32_t)exe_readU32(STATE_MONEY) - RACE_TIRE_COST;

		if (money < 0) {
			owed = (int16_t)-(int16_t)(uint16_t)money;
			money = 0;
		}

		exe_writeU32(STATE_MONEY, (uint32_t)money);
		cars_addSparePart(RACE_TIRE_PART, exe_readS16(RACE_TIRE_PRICE));
		partAt = exe_readU16(STATE_SPARE_PARTS);
		exe_writeU16(
			STATE_SPARE_PARTS,
			exe_readU16((uint16_t)(partAt + STATE_PART_NEXT))
		);
	} else {
		partAt = cars_takeSparePart(race__parts[1]);
	}

	cars_fitPart(partAt, slotAt);

	return owed;
}

/** ****************************************************************************
 *
 * \brief	Show the ticket with the player's name and car.
 *
 * \details	Port of FUN_1000_bf94. Both texts are cut to the width of the
 * 			ticket less 8; the car's model name also after the first
 * 			character of DS:0x53c4 that follows its first 7.
 *
 ******************************************************************************/
static void race__ticket(void) {
	int16_t ticketWidth = race__width(RACE_TICKET);
	int16_t backWidth = race__width(RACE_TICKET_BACK);
	int		backIndex = lib_indexFromGameId(RACE_TICKET_BACK);
	int		ticketIndex = lib_indexFromGameId(RACE_TICKET);

	screen_resetSaved();
	screen_drawSpritePart(
		globals_workSurface,
		RACE_TICKET_BACK,
		0,
		0,
		backWidth,
		(int16_t)lib_get(backIndex)->height,
		0,
		RACE_TICKET_BACK_Y,
		false
	);
	screen_drawSpritePart(
		globals_workSurface,
		RACE_TICKET,
		0,
		0,
		ticketWidth,
		(int16_t)lib_get(ticketIndex)->height,
		backWidth,
		RACE_TICKET_TOP,
		true
	);

	int16_t limit = (int16_t)(ticketWidth - RACE_TICKET_MARGIN);

	globals_drawThrough = 0;
	ui_setStyle(RACE_TICKET_STYLE);
	race__drawClipped(
		RACE_TICKET_NAME_Y,
		exe_text(STATE_PLAYER_NAME),
		RACE_FORMAT_MAX,
		limit
	);

	uint16_t carAt = exe_readU16(STATE_CAR);
	int16_t	 model = exe_readS16((uint16_t)(carAt + STATE_CAR_MODEL));
	uint16_t textAt = exe_readU16(
		(uint16_t)(STATE_MODELS + model * STATE_MODEL_SIZE + STATE_MODEL_TEXT)
	);
	const char * pModel = exe_text((uint16_t)(RACE_MESSAGES + textAt));
	size_t		 kept =
		strcspn(pModel + RACE_MODEL_SKIP, exe_text(RACE_MODEL_STOPS));

	race__drawClipped(
		RACE_TICKET_MODEL_Y,
		pModel,
		(int)kept + RACE_MODEL_SKIP,
		limit
	);
	screen_copyToDisplay(0, 0, PLAT_WIDTH - 1, PLAT_HEIGHT - 1);
	globals_drawThrough = 1;
	cursor_set(CURSOR_SHOW_NOW);
	input_flush();
}

/** ****************************************************************************
 *
 * \brief	Draw a text of the ticket, cut to a width.
 *
 * \param	top				Row of the text.
 * \param	pText			The text.
 * \param	maxLength		Characters drawn at most.
 * \param	limit			Characters are added while the width is below
 * 							it; if it is reached the last one is dropped.
 *
 * \details	Part of FUN_1000_bf94.
 *
 ******************************************************************************/
static void race__drawClipped(
	int16_t		 top,
	const char * pText,
	int			 maxLength,
	int16_t		 limit
) {
	char	text[RACE_FORMAT_MAX];
	int		length = 0;
	int16_t width = 0;

	int most = (int)sizeof(text) - 1;

	while (pText[length] != 0 && width < limit && length < most) {
		char character[2] = { pText[length], 0 };

		width = (int16_t)(width + text_width(character));
		length++;
	}

	if (width >= limit) {
		length--;
	}

	if (length > maxLength) {
		length = maxLength;
	}

	memcpy(text, pText, (size_t)length);
	text[length] = 0;
	text_draw(globals_workSurface, RACE_TICKET_TEXT_X, top, text);
}

/** ****************************************************************************
 *
 * \brief	Show the crash.
 *
 * \details	Port of FUN_1000_c1d8. The scene is drawn in two parts, one
 * 			row apart on EGA when DS:0x254 is 0. Above 90 mph three more
 * 			pictures follow (table DS:0x53d4), 20, 12 and 4 retraces
 * 			apart. Not ported: the crash sound (FUN_1f38_7863) and the
 * 			car drawn by the driving code (FUN_1f38_1094, only logged).
 *
 ******************************************************************************/
static void race__crashPicture(void) {
	bool	tall = globals_videoAdapter == GLOBALS_ADAPTER_EGA;
	int16_t sceneWidth = race__width(RACE_CRASH_TOP);
	int16_t sceneHeight =
		(int16_t)lib_get(lib_indexFromGameId(RACE_CRASH_TOP))->height;
	int16_t bottomHeight =
		(int16_t)lib_get(lib_indexFromGameId(RACE_CRASH_BOTTOM))->height;

	tall = tall && exe_readS16(RACE_UNKNOWN_254) == 0;
	screen_resetSaved();
	screen_drawSpritePart(
		globals_workSurface,
		RACE_CRASH_TOP,
		0,
		0,
		sceneWidth,
		RACE_CRASH_SPLIT + 1,
		0,
		RACE_CRASH_Y,
		false
	);
	screen_drawSpritePart(
		globals_workSurface,
		RACE_CRASH_TOP,
		0,
		RACE_CRASH_SPLIT,
		sceneWidth,
		(int16_t)(sceneHeight - RACE_CRASH_SPLIT),
		0,
		(int16_t)(RACE_CRASH_SPLIT + tall + RACE_CRASH_Y),
		false
	);
	screen_drawSpritePart(
		globals_workSurface,
		RACE_CRASH_BOTTOM,
		0,
		0,
		race__width(RACE_CRASH_BOTTOM),
		bottomHeight,
		0,
		(int16_t)(RACE_CRASH_BOTTOM_Y + tall),
		true
	);
	plat_log(
		"FUN_1f38_1094 (car %d of the driving code) is not ported yet",
		exe_readS16(RACE_CAR_VIEW)
	);
	screen_copyToDisplay(0, 0, PLAT_WIDTH - 1, PLAT_HEIGHT - 1);

	if (exe_readS16(RACE_SPEED) > RACE_FAST_CRASH) {
		for (int frame = 0; frame < RACE_CRASH_FRAME_N; frame++) {
			uint16_t frameAt =
				(uint16_t)(RACE_CRASH_FRAMES + frame * RACE_CRASH_FRAME_SZ);
			int16_t gameId = exe_readS16(frameAt);

			screen_waitRetraces(
				(int16_t)(RACE_CRASH_WAIT - frame * RACE_CRASH_STEP)
			);
			screen_drawSpritePart(
				globals_workSurface,
				gameId,
				0,
				0,
				race__width((uint16_t)gameId),
				(int16_t)lib_get(lib_indexFromGameId(gameId))->height,
				exe_readS16((uint16_t)(frameAt + 2)),
				exe_readS16((uint16_t)(frameAt + 4)),
				false
			);
			screen_copyToDisplay(0, 0, PLAT_WIDTH - 1, PLAT_HEIGHT - 1);
		}
	}

	cursor_set(CURSOR_SHOW_NOW);
	video_showPalette();
	input_flush();
	event_waitAnyKey(RACE_CRASH_KEY);
	input_flush();
	screen_resetSaved();
}

/** ****************************************************************************
 *
 * \brief	Show the times of the race.
 *
 * \details	Port of FUN_1000_c497: the two lines of screen 0x21 are
 * 			written, the screen opened (it draws them) and closed again.
 *
 ******************************************************************************/
static void race__showTimes(void) {
	if (exe_readS16(RACE_WON) != 0) {
		race__printTime(
			RACE_LINE_1,
			RACE_LINE_1_SIZE,
			RACE_FMT_WON,
			exe_readS16(RACE_AVERAGE)
		);
		memcpy(
			exe_data(RACE_LINE_2),
			exe_data(RACE_TEXT_CONGRAT),
			RACE_CONGRAT_SIZE
		);
	} else {
		race__printTime(
			RACE_LINE_1,
			RACE_LINE_1_SIZE,
			RACE_FMT_LOST,
			exe_readS16(RACE_COVERED)
		);
		snprintf(
			(char *)exe_data(RACE_LINE_2),
			RACE_LINE_2_SIZE,
			exe_text(RACE_FMT_LOST_2),
			exe_readS16(RACE_AVERAGE)
		);
	}

	ui_open(RACE_TIMES_SCREEN);
	ui_close(1);
}

/** ****************************************************************************
 *
 * \brief	Write a line with the time of the race and a number.
 *
 * \param	lineAt			DS offset of the line.
 * \param	size			Its size in bytes.
 * \param	formatAt		DS offset of the format: "%2.1f", then "%2d".
 * \param	number			The number for "%2d".
 *
 * \details	Part of FUN_1000_c497. The time is the float at DS:0x8bc4;
 * 			"%2.1f" becomes the time in tenths, rounded half up (the
 * 			original's sprintf rounds the exact binary value, which can
 * 			differ on a tie).
 *
 ******************************************************************************/
static void race__printTime(
	uint16_t lineAt,
	int		 size,
	uint16_t formatAt,
	int16_t	 number
) {
	const char * pFormat = exe_text(formatAt);
	const char * pSpec = strstr(pFormat, RACE_TIME_SPEC);
	char		 format[RACE_FORMAT_MAX];

	if (!pSpec) {
		plat_log("format at DS:0x%x has no time", formatAt);
		return;
	}

	fpu_Real time = fpu_fromSingle(exe_data(RACE_TIME));
	fpu_Real half = fpu_div(fpu_fromInt(1), fpu_fromInt(2));
	long	 tenths = fpu_toLong(fpu_add(fpu_mul(time, fpu_fromInt(10)), half));

	snprintf(
		format,
		sizeof(format),
		"%.*s%%ld.%%ld%s",
		(int)(pSpec - pFormat),
		pFormat,
		pSpec + strlen(RACE_TIME_SPEC)
	);
	snprintf(
		(char *)exe_data(lineAt),
		(size_t)size,
		format,
		tenths / 10,
		tenths % 10,
		number
	);
}

/** ****************************************************************************
 *
 * \brief	The engine blew.
 *
 * \param	message			The message in the bubble.
 *
 * \details	Port of FUN_1000_c52c. The engine, manifold and carburetors
 * 			are given back (the car's price drops by theirs) and the
 * 			engine's wear bytes (+0x16..0x1f) cleared. The sound
 * 			(FUN_1f38_7841) is not ported.
 *
 ******************************************************************************/
static void race__engine(
	int16_t message
) {
	uint16_t carAt = exe_readU16(STATE_CAR);

	if (carAt != 0) {
		for (int slot = STATE_CAR_ENGINE; slot < STATE_CAR_TIRES;
			 slot += 2) {
			uint16_t slotAt = (uint16_t)(carAt + slot);
			uint16_t price = cars_disposePart(exe_readU16(slotAt), 0);

			exe_writeU16(carAt, (uint16_t)(exe_readU16(carAt) - price));
			exe_writeU16(slotAt, 0);
		}

		memset(
			exe_data((uint16_t)(carAt + RACE_ENGINE_WEAR)),
			0,
			RACE_ENGINE_WEAR_N
		);
	}

	input_flush();
	race__bubble(message);
}

/** ****************************************************************************
 *
 * \brief	The transmission dropped.
 *
 * \param	message			The message in the bubble.
 *
 * \details	Port of FUN_1000_c5ac. The transmission is given back (the
 * 			car's price drops by its price) and its wear bytes
 * 			(+0x14..0x15) cleared. The sound (FUN_1f38_7841) is not
 * 			ported.
 *
 ******************************************************************************/
static void race__gearbox(
	int16_t message
) {
	uint16_t carAt = exe_readU16(STATE_CAR);

	if (carAt != 0) {
		uint16_t slotAt = (uint16_t)(carAt + STATE_CAR_GEARBOX);
		uint16_t price = cars_disposePart(exe_readU16(slotAt), 0);

		exe_writeU16(carAt, (uint16_t)(exe_readU16(carAt) - price));
		exe_writeU16(slotAt, 0);
		exe_writeU16((uint16_t)(carAt + RACE_CAR_WEAR_FROM), 0);
	}

	input_flush();
	race__bubble(message);
}

/** ****************************************************************************
 *
 * \brief	Show a message in its bubble at row 0x49 until a key.
 *
 * \param	message			Offset of the message from DS:0x2c02.
 *
 * \details	Port of FUN_1000_4ffa: the bubble is not kept, the deadline
 * 			of the caller (globals_timeout) is.
 *
 ******************************************************************************/
static void race__bubble(
	int16_t message
) {
	uint32_t savedTimeout = globals_timeout;

	ui_bubbleAt((int16_t)-message, -1, RACE_BUBBLE_TOP);
	event_waitAnyKey(globals_messageTicks);
	ui_messageClose();
	globals_timeout = savedTimeout;
}

/** ****************************************************************************
 *
 * \brief	Width of a resource.
 *
 * \param	gameId			The game's resource number.
 *
 * \return	Its width in pixels (word +0 of the decoded resource).
 *
 ******************************************************************************/
static int16_t race__width(
	uint16_t gameId
) {
	return (int16_t)lib_get(lib_indexFromGameId((int16_t)gameId))->width;
}
