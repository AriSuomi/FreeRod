/** ****************************************************************************
 *
 * \file	street.c
 *
 * \brief	The street: cars that drive by and opponents that stop.
 *
 * \details	Port of FUN_1000_ab8b and the helpers it uses for cars and
 * 			opponents (1000:914c to 1000:9af4, 1000:a0f8). The opponents
 * 			are the 22 records of the classified ads (DS:0x7ff8); record
 * 			21 is a car without a driver that drives by, or offers the
 * 			last race when no opponent is left. Not ported yet: the race
 * 			(driving; its result is in race.c, meeting an opponent in
 * 			challenge.c), looking under an opponent's hood
 * 			(FUN_1000_a265) and the sounds. The
 * 			outcomes of a race are here too: the sentence
 * 			(FUN_1000_b08c), the winner (FUN_1000_a544) and the event
 * 			on arriving (FUN_1000_a416).
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/street.h"

#include <stdbool.h>
#include <stdint.h>

#include "game/calendar.h"
#include "game/challenge.h"
#include "game/cars.h"
#include "game/cursor.h"
#include "game/driving.h"
#include "game/event.h"
#include "game/exe.h"
#include "game/game.h"
#include "game/globals.h"
#include "game/lib.h"
#include "game/race.h"
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

#define STREET_PICTURE	   0x445 /**< Resource: the street. */
#define STREET_SCREEN	   0x25	 /**< Screen with the street's hotspots. */
#define STREET_LIST_SCREEN 0x24	 /**< Screen of the list of opponents. */
#define STREET_WAIT		   3000	 /**< Ticks event_wait waits. */
#define STREET_CURSOR_X	   0x9e	 /**< Cursor column on arriving. */
#define STREET_CURSOR_Y	   0x80	 /**< Cursor row on arriving. */
#define STREET_SKY_ENTRY   12	 /**< Palette entry set on arriving. */
#define STREET_SKY_COLOR   13	 /**< Its standard color. */
#define STREET_CAR_BOTTOM  0xa3	 /**< Row of the bottom of a car. */
#define STREET_HOME		   1	 /**< globals_place in the garage. */
#define STREET_STRIP	   6	 /**< globals_place behind bars. */
#define STREET_ARRIVAL	   25	 /**< Percent: the event on arriving. */

/*
 * What street__challenge returns.
 */
#define STREET_RACE_NONE  0 /**< No race: the opponent drives off. */
#define STREET_RACE_BACK  1 /**< Raced; back on the empty street. */
#define STREET_RACE_LEFT  2 /**< Raced; the street is left. */

#define STREET_UNCOUNTED  1 /**< STREET_BET_EVENT of a race not counted. */
#define STREET_END_WINNER 2 /**< globals_endFlags: won the game. */
#define STREET_END_JAILED 4 /**< globals_endFlags: sentenced. */

/*
 * Ticks until the next car: after arriving, after a car, and after the
 * list of opponents was closed without a choice.
 */
#define STREET_FIRST_WAIT 0x8c
#define STREET_NEXT_WAIT  0x118
#define STREET_SHORT_WAIT 5
#define STREET_LOOK_TIME  0x111 /**< Game clock: looking under a hood. */

/*
 * What the player clicked (hotspots of screen 0x25, Bob's Drive-In).
 */
#define STREET_EVENT_HOME  1 /**< The road at the bottom: drive home. */
#define STREET_EVENT_CAR   2 /**< The opponent's car, or wait for one. */
#define STREET_EVENT_HOOD  3 /**< On the car: look under the hood. */
#define STREET_EVENT_LIST  4 /**< The man standing: list of opponents. */
#define STREET_EVENT_ORDER 5 /**< The ORDER sign: sets DS:0x6c88. */

/*
 * Element records of screen 0x25 (5 of 18 bytes at DS:0x20aa) that
 * FUN_1000_a0f8 changes: 0 the road, 1 and 3 lie on the opponent's car,
 * 2 the ORDER sign (on while the waitress is away), 4 the man standing
 * (fixed box). Elements 3 and 4 are kept sorted by their left edge.
 */
#define STREET_ELEMENTS		0x20aa /**< DS: the element records. */
#define STREET_ELEMENT_SIZE 18	   /**< Bytes of an element record. */
#define STREET_ELEMENT_TYPE 0	   /**< Element field: type. */
#define STREET_ELEMENT_ID	6	   /**< Element field: number. */
#define STREET_LEFT			8	   /**< Element field: left edge. */
#define STREET_TOP			10	   /**< Element field: top edge. */
#define STREET_RIGHT		12	   /**< Element field: right edge. */
#define STREET_BOTTOM		14	   /**< Element field: bottom edge. */
#define STREET_TYPE_HOT		11	   /**< Element type: hotspot. */
#define STREET_TYPE_SKIPPED 14	   /**< Element type passed over. */
#define STREET_FIXED_ID		4	   /**< Number of element 4. */
#define STREET_FIXED_LEFT	0xc2   /**< Box of element 4. */
#define STREET_FIXED_TOP	0x49
#define STREET_FIXED_RIGHT	0xd8
#define STREET_FIXED_BOTTOM 0x77
#define STREET_BOX_1		4  /**< Model hotspot box of element 1. */
#define STREET_BOX_3		12 /**< Model hotspot box of element 3. */
#define STREET_BOXES_SIZE	20 /**< Bytes of hotspot boxes per model. */

/*
 * Opponents: the ad records. Index 21 is the car without a driver.
 */
#define STREET_RANKED	  21	/**< Records FUN_1000_9646 looks at. */
#define STREET_LISTED	  22	/**< Records the list of opponents shows. */
#define STREET_NOBODY	  21	/**< Index of the car without a driver. */
#define STREET_AD_NAME	  6		/**< u16: the driver's name, from DS:0x2c02. */
#define STREET_AD_HOOD	  4		/**< u16: the open hood resource. */
#define STREET_AD_STATE	  11	/**< s8: -1 gone, 1 met, else not met yet. */
#define STREET_AD_RACES	  12	/**< s8: races against the player. */
#define STREET_AD_WINS	  13	/**< s8: races the opponent won against us. */
#define STREET_MET		  1		/**< STREET_AD_STATE: met. */
#define STREET_GONE		  -1	/**< STREET_AD_STATE: gone. */
#define STREET_DEMO_MODEL 25	/**< Model of the demo's ad. */
#define STREET_TABLE	  64	/**< Words of the ranking table. */
#define STREET_FEW		  9		/**< Opponents ranked when not all are. */
#define STREET_KEPT		  6		/**< Ranked opponents never replaced. */
#define STREET_NO_RANK	  32000 /**< Larger than any speed difference. */
#define STREET_STICKERS	  9		/**< Random sticker range (0 none). */
#define STREET_INDENT	  7		/**< Indent of the list of opponents. */
#define STREET_TEXT_MAX	  79	/**< Characters of the status text. */

/*
 * DS variables of the street (kept in the data segment image, as other
 * unported code uses them too).
 */
#define STREET_HERE			0x7f86 /**< s16: an opponent's car stands. */
#define STREET_WAITRESS		0x6c84 /**< s16: -1 off, 0 away, 1 here. */
#define STREET_CALLED		0x6c88 /**< s16: 1 after the ORDER sign. */
#define STREET_FIRST		0x6ca2 /**< s16: first ranking of a visit. */
#define STREET_COUNT		0x6c86 /**< s16: ranked opponents. */
#define STREET_PICKS		0x6c8a /**< s16[]: ranked opponent indexes. */
#define STREET_LAST_PICK	0x51f8 /**< s16: the last picked opponent. */
#define STREET_SEEN			0x51f0 /**< s16: cars that drove off. */
#define STREET_INDEX		0x7f82 /**< s16: index of the opponent. */
#define STREET_RECORD		0x7d84 /**< u16: DS offset of its record. */
#define STREET_MODEL		0x7648 /**< s16: its car model. */
#define STREET_MODEL_AT		0x7d7a /**< u16: DS offset of the model. */
#define STREET_NO_DRIVER	0x7d40 /**< s16: the car has no driver. */
#define STREET_CHALLENGE	0x8256 /**< s16: set before a challenge. */
#define STREET_TALLY		0x4970 /**< s16: unknown (saved). */
#define STREET_TALLY_A		0x4972 /**< s16: unknown (saved). */
#define STREET_TALLY_B		0x4974 /**< s16: unknown (saved). */
#define STREET_STATUS		0x7f32 /**< char[80]: the status text. */
#define STREET_STATUS_X		0x51ee /**< s16: its column. */
#define STREET_STATUS_W		0x8bc0 /**< s16: its width. */
#define STREET_LIST			0x49e0 /**< DS: list (count and items). */
#define STREET_KIND			0x7ec0 /**< s16: kind of race, 0 drag. */
#define STREET_BET_EVENT	0x8278 /**< s16: the last bet event. */
#define STREET_ROAD_NEXT	0x4976 /**< s16: the last race is a road race. */
#define STREET_UNKNOWN_817D 0x817d /**< u8: cleared after a loss. */
#define STREET_DRIVE_HOME	0x51ec /**< s16: 1 while driving home after \
										 a race (unknown). */

/*
 * Texts (DS offsets).
 */
#define STREET_TEXT_EMPTY	  0x532e /**< Empty status line. */
#define STREET_TEXT_LEAVE	  0x532f /**< Status text on leaving. */
#define STREET_TEXT_WAITED	  0x5331 /**< Status: the opponent waited. */
#define STREET_TEXT_MEET	  0x5332 /**< Status: meeting an opponent. */
#define STREET_TEXT_MEET_AT	  0x5333 /**< Its text in the status place. */
#define STREET_TEXT_MEET_LOOP 0x5334 /**< Status of the challenge loop. */
#define STREET_TEXT_RACE	  0x5335 /**< Status text of the race. */
#define STREET_TEXT_JOIN	  0x51f2 /**< Between name and car model. */
#define STREET_TEXT_CLEAR	  0x51f5 /**< Status before a car moves. */
#define STREET_TEXT_GONE	  0x51f6 /**< Status after a car left. */
#define STREET_TEXT_LIST	  0x51fa /**< Status of the list. */
#define STREET_TEXT_PICKED	  0x51fb /**< Status: the opponent leaves. */

/*
 * Messages (offsets from DS:0x2c02).
 */
#define STREET_MESSAGES	  0x2c02 /**< DS: messages count from. */
#define STREET_MSG_LOOKED 0x122c /**< Already looked, or no driver. */
#define STREET_MSG_NOBODY 0x198c /**< Nobody to choose. */

/*
 * The event on arriving (FUN_1000_a416): the man standing, 14 frames of
 * 40 by 50 (resource 0x4c9, 7 in a row), plays the sequence at DS:0x521e.
 */
#define STREET_FIGURE		  0x4c9	 /**< Resource: the frames. */
#define STREET_FIGURE_SCREEN  0x18	 /**< One hotspot over the screen. */
#define STREET_FIGURE_KEY	  -26	 /**< Screen key that ends it. */
#define STREET_FIGURE_FIRST	  10	 /**< Ticks before the first frame. */
#define STREET_FIGURE_STEPS	  31	 /**< Steps of the sequence. */
#define STREET_FIGURE_SEQ	  0x521e /**< DS: (number, ticks) pairs. */
#define STREET_FIGURE_FRAMES  0x525b /**< DS: s8 frame of each number. */
#define STREET_FIGURE_REST	  1		 /**< Number shown at the end. */
#define STREET_FIGURE_COLUMNS 7		 /**< Frames in a row. */
#define STREET_FIGURE_COUNT	  14	 /**< Frames; others count as 0. */
#define STREET_FIGURE_WIDTH	  0x28	 /**< Width of a frame. */
#define STREET_FIGURE_HEIGHT  0x32	 /**< Height of a frame. */
#define STREET_FIGURE_LEFT	  0xb8	 /**< Where it stands. */
#define STREET_FIGURE_TOP	  0x49
#define STREET_FIGURE_BARRED  0x18 /**< Columns left of the bar, place 6. */

/*
 * The sentence (FUN_1000_b08c): the street seen through bars. The frame
 * (resource 0x4e8) is cut in two; 7 records of 14 bytes at DS:0x5338
 * place the bars (+0 the DS variable that holds the decoded resource,
 * +2 source left, +4 source top, +6 width, +8 height, +10 left, +12
 * top).
 */
#define STREET_BARS_FRAME	0x4e8  /**< Resource: top and bottom edge. */
#define STREET_BARS_SIDE	0x4e7  /**< Resource: both sides (DS:0x82b8). */
#define STREET_BARS_BAR		0x4e9  /**< Resource: a bar (DS:0x6e64). */
#define STREET_BARS_SIDE_AT 0x82b8 /**< DS: far pointer of the sides. */
#define STREET_BARS			0x5338 /**< DS: the records. */
#define STREET_BARS_COUNT	7	   /**< Records. */
#define STREET_BARS_SIZE	14	   /**< Bytes of a record. */
#define STREET_BARS_EDGE	5	   /**< Rows of the frame's top edge. */
#define STREET_BARS_BOTTOM	0xbe   /**< Row below the bottom edge. */
#define STREET_JAIL_CHANCE	6	   /**< Percent: the event of arriving. */
#define STREET_TEXT_JAIL	0x53b2 /**< Status behind bars. */
#define STREET_TEXT_FIGURE	0x53b3 /**< Status before the event. */
#define STREET_TEXT_PASSED	0x53b4 /**< Status after a car drove by. */

/*
 * What FUN_1000_b045 changes of screen 0x25: the text of element 0
 * (offsets from DS:0x239e), the type of elements 1 to 4 and the scan
 * code of the first key of the screen (pointer at DS:0x57e9).
 */
#define STREET_ELEMENT_TEXT 4	   /**< Element field: text. */
#define STREET_ROAD_BARRED	0x54e  /**< Text of the road behind bars. */
#define STREET_ROAD_TEXT	0x55b  /**< Text of the road otherwise. */
#define STREET_KEYS			0x57e9 /**< DS: pointer to the screen's keys. */
#define STREET_KEY_BARRED	0x10   /**< Scan code (Q) behind bars. */
#define STREET_KEY_HOME		0x22   /**< Scan code (G) otherwise. */

/*
 * The winner (FUN_1000_a544). Frames of resource 6 are 6 bytes at
 * DS:0x526a: source left, source top, width, height, left + 0x50 in the
 * 80 by 44 area the scene plays in (bottom aligned). The steps of
 * resource 7 are 6 bytes at DS:0x52e2: source left, source top, width,
 * height, left, top.
 */
#define STREET_WIN_BANNER	   9 /**< Resource drawn first. */
#define STREET_WIN_BANNER_LEFT 8 /**< Where it goes. */
#define STREET_WIN_BANNER_TOP  0x14
#define STREET_WIN_BANNER_FILL 2	 /**< Its clear color. */
#define STREET_WIN_STATUS	   0xbe	 /**< Row of the status line. */
#define STREET_WIN_PICTURE	   0x408 /**< Resource: the full picture. */
#define STREET_WIN_PICTURE_AGE 0x2ee /**< Ticks it waits for a key. */
#define STREET_WIN_LEFT		   0x78	 /**< The area the scene plays in. */
#define STREET_WIN_TOP		   0x45
#define STREET_WIN_WIDTH	   0x50
#define STREET_WIN_HEIGHT	   0x2c
#define STREET_WIN_HOOD		   0x4c1  /**< Hood resource of the last car. */
#define STREET_NOBODY_HOOD	   0x4dc  /**< Hood resource it has otherwise. */
#define STREET_WIN_CAR_BOTTOM  0x98	  /**< Row of the bottom of the car. */
#define STREET_WIN_ACTOR	   6	  /**< Resource: frames of the scene. */
#define STREET_WIN_FRAMES	   0x526a /**< DS: its frame records. */
#define STREET_WIN_FRAME_SIZE  6	  /**< Bytes of a record. */
#define STREET_WIN_FRAME_COUNT 20	  /**< Frames. */
#define STREET_WIN_FRAME_BASE  0x50	  /**< Left edge the records count from. */
#define STREET_WIN_PAUSE_FRAME 13	  /**< Frame with a pause before it. */
#define STREET_WIN_PAUSE	   0x14	  /**< Retraces of that pause. */
#define STREET_WIN_FRAME_WAIT  10	  /**< Retraces before a frame. */
#define STREET_WIN_COMER	   7	  /**< Resource: what comes closer. */
#define STREET_WIN_STEPS	   0x52e2 /**< DS: its step records. */
#define STREET_WIN_STEP_COUNT  9	  /**< Steps (8 on adapter -6). */
#define STREET_WIN_STEP_WAIT   8	  /**< Retraces before a step. */
#define STREET_WIN_STEP_START  0x28	  /**< First "last" left and top. */
#define STREET_MSG_WON		   0x183c /**< Message at the end. */

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

static uint16_t street__element(
	int index
);

static void street__swapElements(void);

static void street__setHotspots(
	bool opponentHere,
	bool waitressAway
);

static uint8_t street__modelClass(
	int16_t model
);

static int16_t street__strangerChance(void);

static void street__figureFrame(
	int16_t frame,
	int		surface
);

static void street__figure(void);

static void street__setRoad(
	int16_t type,
	uint8_t scanCode
);

static void street__sentence(void);

static void street__winFrame(
	int		surface,
	int16_t frame
);

static void street__winScene(
	int		scenePage,
	int		surface,
	int16_t frame
);

static void street__win(void);

static void street__rankOpponents(
	int16_t all
);

static int16_t street__pickOpponent(
	int16_t strangerChance,
	int16_t all
);

static void street__select(
	int16_t index
);

static bool street__waitress(
	int16_t leaving,
	int16_t here,
	int16_t chance,
	int16_t goChance
);

static int16_t street__waitressChance(
	int16_t onStrip
);

static void street__trimWord(
	char * pText
);

static void street__drawCar(
	int16_t	 mode,
	int16_t	 model,
	int16_t	 hoodOpen,
	int16_t	 bottomY,
	int16_t	 foreground,
	uint16_t tireTable
);

static void street__showCar(
	int16_t index,
	int16_t mode,
	int16_t how
);

static void street__carLeaves(void);

static int16_t street__challenge(void);

static void street__listKnown(
	int16_t * pIndexes,
	int16_t * pIndents
);

static int16_t street__chooseKnown(
	int16_t mode
);

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Spend time on the street until the player drives home.
 *
 * \details	Port of FUN_1000_ab8b. On arriving the cursor is placed, the
 * 			street drawn and palette entry 12 set (FUN_1f38_180c); then
 * 			cars drive by or stop until the player drives home or the
 * 			summer ends. Meeting an opponent is in challenge.c, the
 * 			result of a race in race.c. Not ported: driving (after a
 * 			race was agreed the opponent drives off unless
 * 			--race-result gives the outcome), looking under
 * 			the hood (FUN_1000_a265, only logged; the clock still moves)
 * 			and the sound (FUN_1f38_7001).
 *
 ******************************************************************************/
void street_visit(void) {
	int16_t looks = 0;
	int16_t carMode = CARS_DRAW_DRIVE_IN;
	bool	soon = false;

	ui_status(-1, exe_text(STREET_TEXT_EMPTY), 0);
	globals_cursorX = STREET_CURSOR_X;
	globals_cursorY = STREET_CURSOR_Y;
	screen_drawResource(STREET_PICTURE, -1);
	video_setPaletteColor(STREET_SKY_ENTRY, STREET_SKY_COLOR);
	ui_statusTitle(exe_text(exe_readU16(STATE_STATUS_TITLE)));
	cursor_forget();
	cursor_set(1);
	video_showPalette();

	if (random_next(100) < STREET_ARRIVAL) {
		street__figure();
	}

	cursor_set(CURSOR_SHOW_NOW);
	exe_writeU16(STREET_HERE, 0);
	exe_writeU16(STREET_WAITRESS, (uint16_t)-1);
	exe_writeU16(STREET_CALLED, 0);
	exe_writeU16(STREET_FIRST, 1);
	globals_place = STREET_PLACE;
	globals_buttonsHeld = 0;
	globals_joystickButtons = 0;
	globals_mouseLeft = 0;

	for (;;) {
		bool here = exe_readS16(STREET_HERE) != 0;

		street__setHotspots(
			here,
			exe_readS16(STREET_WAITRESS) == 0 && exe_readS16(STREET_CALLED) == 0
		);
		ui_open(STREET_SCREEN);

		int16_t wait = carMode == CARS_DRAW_DRIVE_IN ? STREET_FIRST_WAIT
													 : STREET_NEXT_WAIT;

		globals_timeout =
			globals_ticks + (uint32_t)(soon ? STREET_SHORT_WAIT : wait);
		soon = false;

		int16_t chance =
			carMode == CARS_DRAW_DRIVE_IN ? 0 : street__strangerChance();

		calendar_checkEnd();

		if (globals_endFlags != 0) {
			return;
		}

		int16_t event;

		do {
			event = event_wait(STREET_WAIT);
		} while (event == 0);

		if (event == STREET_EVENT_HOME) {
			*exe_data(STREET_STATUS) = 0;
			ui_status(
				exe_readS16(STREET_STATUS_X),
				exe_text(STREET_TEXT_LEAVE),
				exe_readS16(STREET_STATUS_W)
			);
			ui_close(1);
			globals_place = STREET_HOME;
			game_drive();

			return;
		}

		if (event == EVENT_TIMEOUT) {
			calendar_checkEnd();

			if (globals_endFlags != 0) {
				return;
			}
		}

		if ((event == STREET_EVENT_CAR || event == EVENT_TIMEOUT) && !here) {
			if (event == STREET_EVENT_CAR) {
				globals_timeout = globals_ticks;
			}

			int16_t index = street__pickOpponent(chance, 0);

			street__showCar(index, carMode, 2);
			carMode = CARS_DRAW_SCROLL_IN;
		} else if (event == EVENT_TIMEOUT) {
			ui_status(-1, exe_text(STREET_TEXT_WAITED), 0);
			street__carLeaves();
			looks = 0;
		} else if (event == STREET_EVENT_CAR) {
			exe_writeU16(STREET_CHALLENGE, 1);
			ui_status(-1, exe_text(STREET_TEXT_MEET), 0);
			ui_status(
				exe_readS16(STREET_STATUS_X),
				exe_text(STREET_TEXT_MEET_AT),
				exe_readS16(STREET_STATUS_W)
			);

			int16_t raced = street__challenge();

			if (raced == STREET_RACE_LEFT) {
				return;
			}

			if (raced == STREET_RACE_NONE) {
				street__carLeaves();
			} else {
				carMode = CARS_DRAW_DRIVE_IN;
			}

			looks = 0;
		} else if (event == STREET_EVENT_HOOD) {
			if (here) {
				if (looks > 0 || exe_readS16(STREET_NO_DRIVER) != 0) {
					game_message(STREET_MSG_LOOKED);
					street__carLeaves();
					looks = 0;
				} else {
					plat_log(
						"looking under the hood (FUN_1000_a265) is not "
						"ported yet"
					);
					looks++;
					globals_gameTime += STREET_LOOK_TIME;
				}
			}
		} else if (event == STREET_EVENT_LIST) {
			if (street__chooseKnown(carMode) != 0) {
				looks = 0;
			} else {
				soon = true;
			}
		} else if (event == STREET_EVENT_ORDER) {
			if (exe_readS16(STREET_WAITRESS) == 0) {
				exe_writeU16(STREET_CALLED, 1);
			}
		}

		ui_close(1);
	}
}

/** ****************************************************************************
 *
 * \brief	The DS offset of an element record of screen 0x25.
 *
 * \param	index			Element 0..4.
 *
 * \return	The DS offset of the record.
 *
 ******************************************************************************/
static uint16_t street__element(
	int index
) {
	return (uint16_t)(STREET_ELEMENTS + index * STREET_ELEMENT_SIZE);
}

/** ****************************************************************************
 *
 * \brief	Swap the element records 3 and 4 of screen 0x25.
 *
 * \details	Part of FUN_1000_a0f8 (the 9-word copies through the stack).
 *
 ******************************************************************************/
static void street__swapElements(void) {
	uint8_t * pThird = exe_data(street__element(3));
	uint8_t * pFourth = exe_data(street__element(4));

	for (int offset = 0; offset < STREET_ELEMENT_SIZE; offset++) {
		uint8_t saved = pThird[offset];

		pThird[offset] = pFourth[offset];
		pFourth[offset] = saved;
	}
}

/** ****************************************************************************
 *
 * \brief	Place the hotspots of screen 0x25 for the car on the street.
 *
 * \param	opponentHere	An opponent's car stands on the street.
 * \param	waitressAway	Waitress away (DS:0x6c84 and DS:0x6c88 are 0).
 *
 * \details	Port of FUN_1000_a0f8. Element 4 gets back its place and its
 * 			fixed box. With a car, elements 1 and 3 get boxes 1 and 3 of
 * 			the model's hotspot boxes (DS:0x70e4) at the car; element 1
 * 			reaches down to the top of element 0, element 3 starts right
 * 			of element 1, and element 4 ends above element 1.
 *
 ******************************************************************************/
static void street__setHotspots(
	bool opponentHere,
	bool waitressAway
) {
	if (exe_readS16((uint16_t)(street__element(4) + STREET_ELEMENT_ID)) !=
		STREET_FIXED_ID) {
		street__swapElements();
	}

	uint16_t fixed = street__element(4);

	exe_writeU16((uint16_t)(fixed + STREET_LEFT), STREET_FIXED_LEFT);
	exe_writeU16((uint16_t)(fixed + STREET_RIGHT), STREET_FIXED_RIGHT);
	exe_writeU16((uint16_t)(fixed + STREET_TOP), STREET_FIXED_TOP);
	exe_writeU16((uint16_t)(fixed + STREET_BOTTOM), STREET_FIXED_BOTTOM);
	exe_writeU16(
		(uint16_t)(street__element(2) + STREET_ELEMENT_TYPE),
		waitressAway ? STREET_TYPE_HOT : STREET_TYPE_SKIPPED
	);

	uint16_t car = street__element(1);
	uint16_t third = street__element(3);

	if (!opponentHere) {
		exe_writeU16(
			(uint16_t)(car + STREET_ELEMENT_TYPE),
			STREET_TYPE_SKIPPED
		);
		exe_writeU16(
			(uint16_t)(third + STREET_ELEMENT_TYPE),
			STREET_TYPE_SKIPPED
		);

		return;
	}

	int16_t	 model = exe_readS16(STREET_MODEL);
	uint16_t boxesAt =
		(uint16_t)(STATE_MODEL_HOTSPOTS + model * STREET_BOXES_SIZE);
	const uint8_t * pBoxes = exe_data(boxesAt);
	int16_t			carLeft = exe_readS16(STATE_CAR_LEFT);
	int16_t			carTop = exe_readS16(STATE_CAR_TOP);
	int16_t			left = (int16_t)(pBoxes[STREET_BOX_1] + carLeft);
	int16_t			top = (int16_t)(pBoxes[STREET_BOX_1 + 1] + carTop);
	int16_t			right = (int16_t)(pBoxes[STREET_BOX_1 + 2] + left - 1);
	int16_t			bottom = (int16_t)(exe_readS16(
								   (uint16_t)(street__element(0) + STREET_TOP)
							   ) -
							   1);

	exe_writeU16((uint16_t)(car + STREET_LEFT), (uint16_t)left);
	exe_writeU16((uint16_t)(car + STREET_RIGHT), (uint16_t)right);
	exe_writeU16((uint16_t)(car + STREET_TOP), (uint16_t)top);
	exe_writeU16((uint16_t)(car + STREET_BOTTOM), (uint16_t)bottom);
	exe_writeU16((uint16_t)(car + STREET_ELEMENT_TYPE), STREET_TYPE_HOT);

	int16_t thirdLeft = (int16_t)(pBoxes[STREET_BOX_3] + carLeft);
	int16_t thirdTop = (int16_t)(pBoxes[STREET_BOX_3 + 1] + carTop);
	int16_t thirdRight = (int16_t)(pBoxes[STREET_BOX_3 + 2] + thirdLeft - 1);
	int16_t thirdBottom = (int16_t)(pBoxes[STREET_BOX_3 + 3] + thirdTop - 1);

	if (thirdLeft <= right) {
		thirdLeft = (int16_t)(right + 1);
	}

	exe_writeU16((uint16_t)(third + STREET_LEFT), (uint16_t)thirdLeft);
	exe_writeU16((uint16_t)(third + STREET_RIGHT), (uint16_t)thirdRight);
	exe_writeU16((uint16_t)(third + STREET_TOP), (uint16_t)thirdTop);
	exe_writeU16((uint16_t)(third + STREET_BOTTOM), (uint16_t)thirdBottom);
	exe_writeU16((uint16_t)(third + STREET_ELEMENT_TYPE), STREET_TYPE_HOT);

	if (top <= exe_readS16((uint16_t)(fixed + STREET_BOTTOM))) {
		exe_writeU16((uint16_t)(fixed + STREET_BOTTOM), (uint16_t)(top - 1));
	}

	if (STREET_FIXED_LEFT < thirdLeft) {
		street__swapElements();
	}
}

/** ****************************************************************************
 *
 * \brief	The class of a car model.
 *
 * \param	model			Car model index.
 *
 * \return	Byte +2 of the model record (DS:0x7d86).
 *
 ******************************************************************************/
static uint8_t street__modelClass(
	int16_t model
) {
	return *exe_data(
		(uint16_t)(STATE_MODELS + model * STATE_MODEL_SIZE + STATE_MODEL_CLASS)
	);
}

/** ****************************************************************************
 *
 * \brief	How far the player has come against the opponents.
 *
 * \return	0 no opponent is left (or the races are many enough), 1 under
 * 			3 races, 2 under 7, 3 7 and more with neither count above 5,
 * 			4 or 5 when only one of the counts is above 5.
 *
 * \details	Port of FUN_1000_9556. The counts are DS:0x4972 and DS:0x4974
 * 			(meaning unknown; their sum is the races); an opponent is
 * 			left when one of the first 21 ad records is not gone.
 *
 ******************************************************************************/
int16_t street_rank(void) {
	bool anyLeft = false;

	for (int index = 0; index < STREET_RANKED; index++) {
		uint16_t record = (uint16_t)(STATE_ADS + index * STATE_AD_SIZE);

		if ((int8_t)*exe_data((uint16_t)(record + STREET_AD_STATE)) !=
			STREET_GONE) {
			anyLeft = true;
		}
	}

	if (!anyLeft) {
		return 0;
	}

	int16_t countA = exe_readS16(STREET_TALLY_A);
	int16_t countB = exe_readS16(STREET_TALLY_B);
	int16_t total = (int16_t)(countA + countB);

	if (total < 3) {
		return 1;
	}

	if (total >= 7 && countA < 6 && countB < 6) {
		return 3;
	}

	if (total < 7 && countA < 6 && countB < 6) {
		return 2;
	}

	if (countA < 6) {
		return 4;
	}

	if (countB < 6) {
		return 5;
	}

	if ((exe_readS16(STREET_TALLY) >> 1) <= total) {
		return 0;
	}

	return 3;
}

/** ****************************************************************************
 *
 * \brief	The percent chance that the next car has no driver.
 *
 * \return	0 while the demo's car is the model, else 5, 8, 12 or (with
 * 			no opponent left) 10 or 50.
 *
 * \details	Port of FUN_1000_95f1. With no opponent left the chance is 10
 * 			when DS:0x817d is 1, else 50.
 *
 ******************************************************************************/
static int16_t street__strangerChance(void) {
	if (exe_readS16(STREET_MODEL) == STREET_DEMO_MODEL) {
		return 0;
	}

	switch (street_rank()) {
		case 0:
			return *exe_data(STATE_ADS_DONE) == 1 ? 10 : 50;

		case 1:
			return 5;

		case 2:
			return 8;

		default:
			return 12;
	}
}

/** ****************************************************************************
 *
 * \brief	Rank the opponents by how close their car's top speed is to
 * 			the player's.
 *
 * \param	all				0: rank 9 at most; else up to 21.
 *
 * \details	Port of FUN_1000_9646. The ranked indexes go to DS:0x6c8a
 * 			(count DS:0x6c86); gone opponents are left out. With all 0,
 * 			from the end down to the seventh, a ranked opponent is
 * 			replaced by the next of the first ones when the class
 * 			difference stored before it is above 1. As in the original,
 * 			each record's class difference is stored in the slot of the
 * 			next record, so the test looks at the record before the
 * 			index. In the demo the demo's ad is added twice, and on the
 * 			first ranking of a visit it is the only one.
 *
 ******************************************************************************/
static void street__rankOpponents(
	int16_t all
) {
	int16_t	 table[STREET_TABLE] = { 0 };
	uint16_t car = exe_readU16(STATE_CAR);
	int16_t	 carModel = exe_readS16((uint16_t)(car + STATE_CAR_MODEL));
	uint8_t	 carClass = street__modelClass(carModel);

	for (int index = 0; index < STREET_RANKED; index++) {
		uint16_t record = (uint16_t)(STATE_ADS + index * STATE_AD_SIZE);
		int16_t	 model = exe_readS16((uint16_t)(record + STATE_AD_CAR));
		int16_t	 own = cars_maxSpeed(-1);
		int16_t	 theirs = cars_maxSpeed(model);
		int16_t	 speedDifference = (int16_t)(theirs - own);
		int16_t	 classDifference =
			(int16_t)(street__modelClass(model) - carClass);

		table[index * 3 + 1] = (int16_t)(speedDifference < 0 ? -speedDifference
															 : speedDifference);
		table[index * 3 + 3] = (int16_t)(classDifference < 0 ? -classDifference
															 : classDifference);
		table[index * 3 + 2] =
			(int8_t)*exe_data((uint16_t)(record + STREET_AD_STATE)) ==
			STREET_GONE;
	}

	int16_t count = 0;
	int16_t limit = all == 0 ? STREET_FEW : STREET_RANKED;
	bool	found = true;

	while (found && count < limit) {
		int16_t best = STREET_NO_RANK;
		int16_t bestIndex = 0;

		found = false;

		for (int index = 0; index < STREET_RANKED; index++) {
			if (table[index * 3 + 2] == 0 && table[index * 3 + 1] < best) {
				best = table[index * 3 + 1];
				bestIndex = (int16_t)index;
				found = true;
			}
		}

		if (found) {
			exe_writeU16(
				(uint16_t)(STREET_PICKS + count * 2),
				(uint16_t)bestIndex
			);
			count++;
			table[bestIndex * 3 + 2] = 1;
		}
	}

	if (all == 0) {
		int16_t next = 0;

		for (int16_t place = count; place > STREET_KEPT; place--) {
			if (table[place * 3] > 1) {
				exe_writeU16(
					(uint16_t)(STREET_PICKS + (place - 1) * 2),
					exe_readU16((uint16_t)(STREET_PICKS + next * 2))
				);
				next++;
			}
		}
	}

	if (globals_demoMode != 0) {
		uint16_t demoAd = exe_readU16(STATE_DEMO_AD);

		exe_writeU16((uint16_t)(STREET_PICKS + count * 2), demoAd);
		count++;
		exe_writeU16((uint16_t)(STREET_PICKS + count * 2), demoAd);
		count++;

		if (exe_readS16(STREET_FIRST) != 0) {
			count = 1;
			exe_writeU16(STREET_PICKS, demoAd);
		}

		exe_writeU16(STREET_FIRST, 0);
	}

	exe_writeU16(STREET_COUNT, (uint16_t)count);
}

/** ****************************************************************************
 *
 * \brief	Pick the next car: a ranked opponent, or one without driver.
 *
 * \param	strangerChance	Percent chance of a car without a driver.
 * \param	all				Passed to street__rankOpponents.
 *
 * \return	Index of the ad record (STREET_NOBODY: no driver).
 *
 * \details	Port of FUN_1000_984e. The same opponent as last time gets
 * 			one more draw.
 *
 ******************************************************************************/
static int16_t street__pickOpponent(
	int16_t strangerChance,
	int16_t all
) {
	if (random_next(100) < strangerChance) {
		return STREET_NOBODY;
	}

	street__rankOpponents(all);

	int16_t count = exe_readS16(STREET_COUNT);

	if (count == 0) {
		return STREET_NOBODY;
	}

	int16_t pick =
		exe_readS16((uint16_t)(STREET_PICKS + random_next(count) * 2));

	if (pick == exe_readS16(STREET_LAST_PICK)) {
		pick = exe_readS16((uint16_t)(STREET_PICKS + random_next(count) * 2));
	}

	exe_writeU16(STREET_LAST_PICK, (uint16_t)pick);

	return pick;
}

/** ****************************************************************************
 *
 * \brief	Make an ad record the opponent on the street.
 *
 * \param	index			Index of the ad record.
 *
 * \details	Port of FUN_1000_98ba. Sets the opponent's record, model and
 * 			paint and what DrawCar uses (DS:0x827a body, 0x827e tires,
 * 			0x827c paint, 0x8280 hood). An opponent with a driver is
 * 			marked as met, and after enough races (record +12) may get a
 * 			new sticker, lose both bumpers or get a chopped roof; the
 * 			chances grow with the model's class.
 *
 ******************************************************************************/
static void street__select(
	int16_t index
) {
	uint16_t record = (uint16_t)(STATE_ADS + index * STATE_AD_SIZE);
	int16_t	 model = exe_readS16((uint16_t)(record + STATE_AD_CAR));
	uint16_t modelAt = (uint16_t)(STATE_MODELS + model * STATE_MODEL_SIZE);

	exe_writeU16(STREET_INDEX, (uint16_t)index);
	exe_writeU16(STREET_RECORD, record);
	exe_writeU16(STREET_MODEL, (uint16_t)model);
	exe_writeU16(STREET_MODEL_AT, modelAt);

	if (index == STREET_NOBODY) {
		exe_writeU16(STREET_NO_DRIVER, 1);
	} else {
		uint8_t * pRecord = exe_data(record);
		uint16_t  partsAt =
			(uint16_t)(STATE_MODEL_PARTS + model * STATE_MODEL_SIZE);
		int16_t chance = (int16_t)((street__modelClass(model) >> 1) + 1);
		int8_t	races = (int8_t)pRecord[STREET_AD_RACES];
		int8_t	sticker = (int8_t)pRecord[STATE_AD_WEAR];

		exe_writeU16(STREET_NO_DRIVER, 0);
		pRecord[STREET_AD_STATE] = STREET_MET;

		if (sticker == 0 && races > 5) {
			pRecord[STATE_AD_WEAR] =
				random_next(20) < chance ? (uint8_t)random_next(STREET_STICKERS)
										 : 0;
		} else if (sticker > 0 && races > 10) {
			if (random_next(40) < chance) {
				pRecord[STATE_AD_WEAR] = (uint8_t)random_next(STREET_STICKERS);
			}
		}

		if (exe_readS16((uint16_t)(partsAt + STATE_PARTS_REAR)) > 0 &&
			exe_readS16((uint16_t)(partsAt + STATE_PARTS_FRONT)) > 0 &&
			races > 8 && random_next(25) < chance) {
			pRecord[STATE_AD_FLAGS] |=
				CARS_FLAG_FRONT_BUMPER | CARS_FLAG_REAR_BUMPER;
		}

		if (exe_readS16((uint16_t)(partsAt + STATE_PARTS_ROOF)) > 0 &&
			(int8_t)pRecord[STREET_AD_RACES] > 8 && random_next(30) < chance) {
			pRecord[STATE_AD_FLAGS] |= CARS_FLAG_ROOF;
		}
	}

	int16_t paint = (int8_t)*exe_data((uint16_t)(record + STATE_AD_COLOR));

	uint16_t body = exe_readU16((uint16_t)(modelAt + STATE_MODEL_BODY));
	uint16_t fitted = exe_readU16((uint16_t)(modelAt + STATE_MODEL_FITTED));
	uint16_t hood = exe_readU16((uint16_t)(record + STREET_AD_HOOD));

	exe_writeU16(STATE_GARAGE_BODY, body);
	exe_writeU16(STATE_GARAGE_TIRES, (uint16_t)((fitted >> 4) & 3));
	exe_writeU16(STATE_GARAGE_PAINT, (uint16_t)paint);
	cars_setPaint(paint);
	exe_writeU16(STATE_GARAGE_HOOD, hood);
}

/** ****************************************************************************
 *
 * \brief	Let the waitress come or go by chance.
 *
 * \param	leaving			0: she may come (and go again at once); 1: she
 * 							may leave.
 * \param	here			Passed to cars_waitress (an opponent's car is
 * 							on the street).
 * \param	chance			Percent chance that she comes or leaves.
 * \param	goChance		Percent chance that she leaves right after
 * 							coming.
 *
 * \retval	true	The waitress came or left.
 * \retval	false	Nothing happened.
 *
 * \details	Port of FUN_1000_914c. She comes only while DS:0x6c84 is 0 and
 * 			more than 2 cars drove off (DS:0x51f0); leaving resets that
 * 			count.
 *
 ******************************************************************************/
static bool street__waitress(
	int16_t leaving,
	int16_t here,
	int16_t chance,
	int16_t goChance
) {
	bool happened = false;

	if (leaving == 0) {
		if (exe_readS16(STREET_WAITRESS) == 0 &&
			exe_readS16(STREET_SEEN) > 2 && random_next(100) < chance) {
			happened = true;
			cursor_set(CURSOR_HIDE_NOW);
			cars_waitress(0, here);
			exe_writeU16(STREET_WAITRESS, 1);

			if (random_next(100) < goChance) {
				screen_waitRetraces(100);
				cars_waitress(1, here);
				exe_writeU16(STREET_WAITRESS, 0);
				exe_writeU16(STREET_CALLED, 0);
			}
		}
	} else if (leaving == 1) {
		if (exe_readS16(STREET_WAITRESS) == 1 && random_next(100) < chance) {
			happened = true;
			cursor_set(CURSOR_HIDE_NOW);
			cars_waitress(1, here);
			exe_writeU16(STREET_WAITRESS, 0);
			exe_writeU16(STREET_CALLED, 0);
			exe_writeU16(STREET_SEEN, 0);
		}
	}

	if (happened) {
		cursor_set(CURSOR_SHOW_NOW);
	}

	return happened;
}

/** ****************************************************************************
 *
 * \brief	The percent chance that the waitress comes.
 *
 * \param	onStrip			1 when globals_place is 6, else 0.
 *
 * \return	5, plus 10 on place 6, 75 after event 5 (DS:0x6c88) and 8
 * 			times the demo value (DS:0x610).
 *
 * \details	Part of FUN_1000_92a4.
 *
 ******************************************************************************/
static int16_t street__waitressChance(
	int16_t onStrip
) {
	return (int16_t)(onStrip * 10 + exe_readS16(STREET_CALLED) * 0x4b +
					 globals_demoMode * 8 + 5);
}

/** ****************************************************************************
 *
 * \brief	Cut the last word off a text.
 *
 * \param	pText			The text; characters are removed from its end
 * 							up to and including a space.
 *
 * \details	Port of FUN_1000_9256.
 *
 ******************************************************************************/
static void street__trimWord(
	char * pText
) {
	for (;;) {
		int last = -1;

		while (pText[last + 1] != 0) {
			last++;
		}

		if (last < 0) {
			return;
		}

		bool space = pText[last] == ' ';

		pText[last] = 0;

		if (space) {
			return;
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Write who drives the car into the status line and move the
 * 			opponent's car.
 *
 * \param	mode			DrawCar mode: 4 first car of a visit, 1 a car
 * 							comes, 2 it drives off.
 * \param	model			The car model.
 * \param	hoodOpen		Passed to cars_draw.
 * \param	bottomY			Passed to cars_draw.
 * \param	foreground		Passed to cars_draw.
 * \param	tireTable		Passed to cars_draw.
 *
 * \details	Port of FUN_1000_92a4. The text is the driver's name, the
 * 			text at DS:0x51f2 and the model's name, without a final dot;
 * 			it is centered in the space after the money (on a multiple
 * 			of 8, at least 15 columns in) and loses words until it fits.
 * 			The waitress may come or go before and after the car moves.
 * 			A car that only drives by counts as gone (DS:0x7f86 0).
 *
 ******************************************************************************/
static void street__drawCar(
	int16_t	 mode,
	int16_t	 model,
	int16_t	 hoodOpen,
	int16_t	 bottomY,
	int16_t	 foreground,
	uint16_t tireTable
) {
	bool	 passing = exe_readS16(CARS_PASSING) != 0;
	uint16_t record = exe_readU16(STREET_RECORD);
	uint16_t modelAt = exe_readU16(STREET_MODEL_AT);
	char *	 pText = (char *)exe_data(STREET_STATUS);
	uint16_t name = exe_readU16((uint16_t)(record + STREET_AD_NAME));
	uint16_t modelText = exe_readU16((uint16_t)(modelAt + STATE_MODEL_TEXT));

	const char * pParts[3] = {
		exe_text((uint16_t)(STREET_MESSAGES + name)),
		exe_text(STREET_TEXT_JOIN),
		exe_text((uint16_t)(STREET_MESSAGES + modelText)),
	};
	int length = 0;

	for (int part = 0; part < 3; part++) {
		for (const char * pChar = pParts[part];
			 *pChar != 0 && length < STREET_TEXT_MAX;
			 pChar++) {
			pText[length++] = *pChar;
		}
	}

	pText[length] = 0;

	int16_t onStrip = globals_place == STREET_STRIP;
	int16_t chance = street__waitressChance(onStrip);

	if (mode == CARS_DRAW_DRIVE_IN) {
		exe_writeU16(STREET_WAITRESS, (uint16_t)-1);
	}

	exe_writeU16(STREET_HERE, (uint16_t)(mode == CARS_DRAW_SCROLL_RIGHT));

	if (mode == CARS_DRAW_SCROLL_IN &&
		!street__waitress(0, exe_readS16(STREET_HERE), chance, 10)) {
		street__waitress(1, exe_readS16(STREET_HERE), 0x14, 0);
	}

	if (length > 0 && pText[length - 1] == '.') {
		pText[length - 1] = 0;
	}

	int16_t left;
	int16_t width;

	for (;;) {
		width = text_width(pText);

		int16_t space =
			(int16_t)(-(globals_moneyEndX + width - PLAT_WIDTH));

		space = space < 1 ? 0 : (int16_t)(space >> 1);

		if (space < 15) {
			space = 15;
		}

		left = (int16_t)((space + globals_moneyEndX) & 0xfff8);

		if (left + width <= PLAT_WIDTH) {
			break;
		}

		street__trimWord(pText);
	}

	exe_writeU16(STREET_STATUS_W, (uint16_t)width);
	exe_writeU16(STREET_STATUS_X, (uint16_t)left);
	ui_status(-1, exe_text(STREET_TEXT_CLEAR), 0);
	ui_status(left, pText, width);

	const uint8_t * pRecord = exe_data(record);

	cars_draw(
		mode,
		model,
		pRecord[STATE_AD_FLAGS],
		(int8_t)pRecord[STATE_AD_WEAR],
		hoodOpen,
		bottomY,
		foreground,
		tireTable
	);

	if (passing) {
		mode = CARS_DRAW_SCROLL_RIGHT;
	}

	if (mode == CARS_DRAW_SCROLL_RIGHT) {
		exe_writeU16(STREET_SEEN, (uint16_t)(exe_readS16(STREET_SEEN) + 1));
		pText[0] = 0;
		ui_status(left, exe_text(STREET_TEXT_GONE), width);
	}

	if (mode == CARS_DRAW_DRIVE_IN) {
		exe_writeU16(STREET_WAITRESS, 0);
	}

	int16_t here = mode != CARS_DRAW_SCROLL_RIGHT;

	exe_writeU16(STREET_HERE, (uint16_t)here);

	if (!onStrip || mode == CARS_DRAW_SCROLL_RIGHT) {
		chance = street__waitressChance(onStrip);

		if (!street__waitress(0, here, chance, here ? 0 : 8)) {
			street__waitress(1, here, 0x28, 0);
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Meet the opponent and race until no race is agreed.
 *
 * \return	STREET_RACE_NONE, STREET_RACE_BACK or STREET_RACE_LEFT
 * 			(screen 0x25 closed; home, sentenced or won the game).
 *
 * \details	Part of FUN_1000_ab8b: the challenge loop. After a race
 * 			race_result the counts DS:0x4970..0x4974 and the
 * 			opponent's counters (+0xc races, +0xd wins) change, except
 * 			after bet event 1. The car without a driver offers its road
 * 			race after the player won its drag race; winning that one
 * 			wins the game. With --race-result (race_testOutcome) the outcome
 * 			is overridden after driving_run. After the result the colors
 * 			of the race are undone (driving_setRaceColors). Not ported:
 * 			the sound FUN_1f38_7001.
 *
 ******************************************************************************/
static int16_t street__challenge(void) {
	for (;;) {
		ui_status(-1, exe_text(STREET_TEXT_MEET_LOOP), 0);

		if (challenge_meet(exe_readS16(STREET_NO_DRIVER)) == 0) {
			return STREET_RACE_NONE;
		}

		*exe_data(STREET_STATUS) = 0;
		ui_status(
			exe_readS16(STREET_STATUS_X),
			exe_text(STREET_TEXT_RACE),
			exe_readS16(STREET_STATUS_W)
		);
		race_testOutcome();

		int16_t result = race_result();
		bool	won = exe_readS16(RACE_WON) != 0;
		bool	counted = exe_readS16(STREET_BET_EVENT) != STREET_UNCOUNTED;
		bool	drag = exe_readS16(STREET_KIND) == 0;

		video_blankPalette();
		driving_setRaceColors(-1);

		if (counted) {
			exe_writeU16(STREET_TALLY, exe_readU16(STREET_TALLY) + 1);
		}

		if (exe_readS16(STREET_NO_DRIVER) != 0) {
			if (!won) {
				exe_writeU16(STREET_ROAD_NEXT, 0);
				*exe_data(STREET_UNKNOWN_817D) = 0;
				exe_writeU16(STREET_TALLY_A, exe_readU16(STREET_TALLY_A) - 3);
				exe_writeU16(STREET_TALLY_B, exe_readU16(STREET_TALLY_B) - 2);
			} else if (!drag) {
				ui_close(1);
				street__win();
				globals_endFlags |= STREET_END_WINNER;

				return STREET_RACE_LEFT;
			} else {
				exe_writeU16(STREET_ROAD_NEXT, 1);
			}
		}

		if (result == RACE_JAILED) {
			ui_close(1);
			street__sentence();
			cursor_set(CURSOR_HIDE_NOW);
			globals_endFlags |= STREET_END_JAILED;

			return STREET_RACE_LEFT;
		}

		if (won && counted) {
			uint16_t tallyAt = drag ? STREET_TALLY_A : STREET_TALLY_B;

			exe_writeU16(tallyAt, exe_readU16(tallyAt) + 1);
		}

		if (counted) {
			uint8_t * pAd = exe_data(exe_readU16(STREET_RECORD));

			pAd[STREET_AD_RACES]++;

			if (!won) {
				pAd[STREET_AD_WINS]++;
			}
		}

		if (result == RACE_HOME || result == RACE_TOWED) {
			ui_close(1);
			globals_place = result == RACE_HOME ? STREET_HOME : 0;

			if (result == RACE_HOME) {
				exe_writeU16(STREET_DRIVE_HOME, 1);
			}

			game_drive();
			exe_writeU16(STREET_DRIVE_HOME, 0);

			return STREET_RACE_LEFT;
		}

		screen_drawResource(STREET_PICTURE, -1);
		ui_statusTitle(exe_text(exe_readU16(STATE_STATUS_TITLE)));
		video_showPalette();
		exe_writeU16(STREET_HERE, 0);
		exe_writeU16(STREET_WAITRESS, (uint16_t)-1);
		exe_writeU16(STREET_CALLED, 0);

		if (exe_readS16(STREET_ROAD_NEXT) == 0) {
			return STREET_RACE_BACK;
		}

		street__showCar(STREET_NOBODY, CARS_DRAW_DRIVE_IN, 0);
	}
}

/** ****************************************************************************
 *
 * \brief	Draw a frame of the figure of the event on arriving.
 *
 * \param	frame			Frame 0..13; others draw frame 0.
 * \param	surface			Surface to draw on.
 *
 * \details	Port of FUN_1000_a35c. On place 6 the original first copies
 * 			the bar in front of the figure from the display into the
 * 			frame (16 columns, 0x18 into it); the port leaves those
 * 			columns of the surface as they are instead, which looks
 * 			the same as both surfaces show the bar there.
 *
 ******************************************************************************/
static void street__figureFrame(
	int16_t frame,
	int		surface
) {
	int16_t row = 0;
	int16_t column = 0;
	int16_t width = STREET_FIGURE_WIDTH;

	if (frame < STREET_FIGURE_COUNT) {
		row = (int16_t)(frame / STREET_FIGURE_COLUMNS);
		column = (int16_t)(frame % STREET_FIGURE_COLUMNS);
	}

	if (globals_place == STREET_STRIP) {
		width = STREET_FIGURE_BARRED;
	}

	screen_drawSpritePart(
		surface,
		STREET_FIGURE,
		(int16_t)(column * STREET_FIGURE_WIDTH),
		(int16_t)(row * STREET_FIGURE_HEIGHT),
		width,
		STREET_FIGURE_HEIGHT,
		STREET_FIGURE_LEFT,
		STREET_FIGURE_TOP,
		true
	);

	if (surface == globals_displaySurface) {
		cursor_surfaceCopied(
			STREET_FIGURE_LEFT,
			STREET_FIGURE_TOP,
			width,
			STREET_FIGURE_HEIGHT
		);
	}
}

/** ****************************************************************************
 *
 * \brief	Play the event on arriving at the street.
 *
 * \details	Port of FUN_1000_a416. The man standing at the drive-in
 * 			plays the 31 steps at DS:0x521e (number of the frame, ticks
 * 			it shows) on the display; screen key -26 ends it early. At
 * 			the end the man at rest is drawn on both surfaces. Not
 * 			ported: the bookkeeping of temporary memory
 * 			(FUN_1f38_a07e).
 *
 ******************************************************************************/
static void street__figure(void) {
	uint16_t stepAt = STREET_FIGURE_SEQ;
	int16_t	 steps = 0;
	bool	 playing = true;

	ui_open(STREET_FIGURE_SCREEN);
	globals_timeout = globals_ticks + STREET_FIGURE_FIRST;

	while (playing) {
		int16_t event;

		do {
			event = event_wait(STREET_WAIT);
		} while (event == 0);

		if (event == STREET_FIGURE_KEY) {
			playing = false;
		} else if (event == EVENT_TIMEOUT) {
			if (steps >= STREET_FIGURE_STEPS) {
				playing = false;
			} else {
				uint16_t frameAt =
					(uint16_t)(STREET_FIGURE_FRAMES + *exe_data(stepAt));

				screen_waitRetraces(1);
				street__figureFrame(
					(int8_t)*exe_data(frameAt),
					globals_displaySurface
				);
				globals_timeout =
					globals_ticks + *exe_data((uint16_t)(stepAt + 1));
				stepAt += 2;
				steps++;
			}
		}
	}

	int16_t rest = (int8_t)*exe_data(STREET_FIGURE_FRAMES + STREET_FIGURE_REST);

	screen_waitRetraces(1);
	street__figureFrame(rest, globals_displaySurface);
	street__figureFrame(rest, globals_workSurface);
	ui_close(1);
}

/** ****************************************************************************
 *
 * \brief	Switch the road of screen 0x25 between the street and the
 * 			view through the bars.
 *
 * \param	type			Type of elements 1 to 4: STREET_TYPE_SKIPPED
 * 							behind bars, else STREET_TYPE_HOT.
 * \param	scanCode		Scan code of the screen's first key.
 *
 * \details	Port of FUN_1000_b045. Behind bars only the road (element
 * 			0) can be clicked, with its own text.
 *
 ******************************************************************************/
static void street__setRoad(
	int16_t type,
	uint8_t scanCode
) {
	exe_writeU16(
		(uint16_t)(street__element(0) + STREET_ELEMENT_TEXT),
		type == STREET_TYPE_SKIPPED ? STREET_ROAD_BARRED : STREET_ROAD_TEXT
	);

	for (int index = 1; index <= 4; index++) {
		exe_writeU16(
			(uint16_t)(street__element(index) + STREET_ELEMENT_TYPE),
			(uint16_t)type
		);
	}

	*exe_data(exe_readU16(STREET_KEYS)) = scanCode;
}

/** ****************************************************************************
 *
 * \brief	Watch the street through bars until the player goes on.
 *
 * \details	Port of FUN_1000_b08c (after race result 4). The street is
 * 			drawn with the frame of resource 0x4e8 (its top 5 rows at
 * 			the top, the rest above the status line) and the 7 bars of
 * 			DS:0x5338. On place 6 cars only drive by; before each one
 * 			the event of arriving plays with 6 percent. The road (or
 * 			its key) ends it. The original keeps the decoded bars in
 * 			DS:0x82b8 and DS:0x6e64; the port draws the resources.
 *
 ******************************************************************************/
static void street__sentence(void) {
	int16_t				 carMode = CARS_DRAW_DRIVE_IN;
	const lib_Resource * pFrame =
		lib_get(lib_indexFromGameId(STREET_BARS_FRAME));

	globals_drawThrough = 0;
	cursor_set(CURSOR_HIDE);
	screen_resetSaved();
	screen_drawResource(STREET_PICTURE, -1);
	video_setPaletteColor(STREET_SKY_ENTRY, STREET_SKY_COLOR);

	if (pFrame) {
		int16_t rest = (int16_t)(pFrame->height - STREET_BARS_EDGE);

		screen_drawSpritePart(
			globals_workSurface,
			STREET_BARS_FRAME,
			0,
			0,
			(int16_t)pFrame->width,
			STREET_BARS_EDGE,
			0,
			0,
			true
		);
		screen_drawSpritePart(
			globals_workSurface,
			STREET_BARS_FRAME,
			0,
			STREET_BARS_EDGE,
			(int16_t)pFrame->width,
			rest,
			0,
			(int16_t)(STREET_BARS_BOTTOM - rest),
			true
		);
	}

	for (int index = 0; index < STREET_BARS_COUNT; index++) {
		uint16_t bar = (uint16_t)(STREET_BARS + index * STREET_BARS_SIZE);
		bool	 side = exe_readU16(bar) == STREET_BARS_SIDE_AT;

		screen_drawSpritePart(
			globals_workSurface,
			side ? STREET_BARS_SIDE : STREET_BARS_BAR,
			exe_readS16((uint16_t)(bar + 2)),
			exe_readS16((uint16_t)(bar + 4)),
			exe_readS16((uint16_t)(bar + 6)),
			exe_readS16((uint16_t)(bar + 8)),
			exe_readS16((uint16_t)(bar + 10)),
			exe_readS16((uint16_t)(bar + 12)),
			true
		);
	}

	ui_status(-1, exe_text(STREET_TEXT_JAIL), 0);
	ui_statusTitle(exe_text(exe_readU16(STATE_STATUS_TITLE)));
	screen_copyToDisplay(0, 0, PLAT_WIDTH - 1, PLAT_HEIGHT - 1);
	globals_drawThrough = 1;
	video_showPalette();
	exe_writeU16(STREET_HERE, 0);
	exe_writeU16(STREET_WAITRESS, (uint16_t)-1);
	exe_writeU16(STREET_CALLED, 0);
	cursor_set(1);
	cursor_set(CURSOR_SHOW);
	globals_place = STREET_STRIP;
	street__setRoad(STREET_TYPE_SKIPPED, STREET_KEY_BARRED);

	for (;;) {
		bool first = carMode == CARS_DRAW_DRIVE_IN;

		ui_open(STREET_SCREEN);
		globals_timeout = globals_ticks +
			(uint32_t)(first ? STREET_FIRST_WAIT : STREET_NEXT_WAIT);

		int16_t chance = first ? 0 : street__strangerChance();
		int16_t event;

		do {
			event = event_wait(STREET_WAIT);
		} while (event == 0);

		if (event == STREET_EVENT_HOME) {
			break;
		}

		if (event == EVENT_TIMEOUT) {
			if (random_next(100) < STREET_JAIL_CHANCE) {
				cursor_set(CURSOR_HIDE_NOW);
				ui_status(-1, exe_text(STREET_TEXT_FIGURE), 0);
				street__figure();
				cursor_set(CURSOR_SHOW_NOW);
			}

			street__showCar(street__pickOpponent(chance, 1), carMode, 1);
			carMode = CARS_DRAW_SCROLL_IN;
			*exe_data(STREET_STATUS) = 0;
			ui_status(
				exe_readS16(STREET_STATUS_X),
				exe_text(STREET_TEXT_PASSED),
				exe_readS16(STREET_STATUS_W)
			);
		}

		ui_close(0);
	}

	ui_close(1);
	street__setRoad(STREET_TYPE_HOT, STREET_KEY_HOME);
	globals_place = STREET_HOME;
}

/** ****************************************************************************
 *
 * \brief	Draw a frame of the winner's scene.
 *
 * \param	surface			Surface to draw on (the area of the scene lies
 * 							at its place on the screen).
 * \param	frame			Frame 0..19 of DS:0x526a.
 *
 * \details	Part of FUN_1000_a544: the masked blit (video table entry 2,
 * 			DS:0x78aa) of a frame of resource 6, bottom aligned in the
 * 			area.
 *
 ******************************************************************************/
static void street__winFrame(
	int		surface,
	int16_t frame
) {
	const uint8_t * pFrame = exe_data(
		(uint16_t)(STREET_WIN_FRAMES + frame * STREET_WIN_FRAME_SIZE)
	);

	screen_drawSpritePart(
		surface,
		STREET_WIN_ACTOR,
		pFrame[0],
		pFrame[1],
		pFrame[2],
		pFrame[3],
		(int16_t)(STREET_WIN_LEFT + pFrame[4] - STREET_WIN_FRAME_BASE),
		(int16_t)(STREET_WIN_TOP + STREET_WIN_HEIGHT - pFrame[3]),
		false
	);
}

/** ****************************************************************************
 *
 * \brief	Show the winner's scene with a frame on a surface.
 *
 * \param	scenePage		Page that holds the scene without a frame.
 * \param	surface			Surface to draw on.
 * \param	frame			Frame 0..19 of DS:0x526a.
 *
 * \details	Part of FUN_1000_a544, which copies the scene to a third
 * 			buffer (FUN_1f38_ab50), draws the frame into it and blits
 * 			that; the port draws both on the surface, which nothing
 * 			shows in between.
 *
 ******************************************************************************/
static void street__winScene(
	int		scenePage,
	int		surface,
	int16_t frame
) {
	plat_copyRect(
		scenePage,
		surface,
		STREET_WIN_LEFT,
		STREET_WIN_TOP,
		STREET_WIN_WIDTH,
		STREET_WIN_HEIGHT
	);
	street__winFrame(surface, frame);

	if (surface == globals_displaySurface) {
		cursor_surfaceCopied(
			STREET_WIN_LEFT,
			STREET_WIN_TOP,
			STREET_WIN_WIDTH,
			STREET_WIN_HEIGHT
		);
	}
}

/** ****************************************************************************
 *
 * \brief	Show how the player wins the game.
 *
 * \details	Port of FUN_1000_a544 (after winning the road race against
 * 			the car without a driver). Resource 9 and the picture 0x408
 * 			are shown; then the last car drives in with hood resource
 * 			0x4c1 in place of 0x4dc and 20 frames of resource 6 play
 * 			in an area of 80 by 44 (frame 0 over the street without
 * 			the car, the others over the car; frame 19 shows only at
 * 			the end). Then resource 7 comes closer in 9 steps, each
 * 			drawn over the rectangle that holds it and the step before
 * 			(whose bottom the original takes from the width of that
 * 			step; kept, clipped to the screen), and the message 0x183c
 * 			ends it; as DS:0x8240 is 0 its bubble is drawn on the work
 * 			surface only and does not show (as the original's code
 * 			reads; not checked in DOSBox). Not ported: freeing and
 * 			preloading resources (FUN_1f38_6915, FUN_1f38_68ca,
 * 			DS:0x4f4c) and the adapter -6 path (8 steps).
 *
 ******************************************************************************/
static void street__win(void) {
	uint16_t nobody = (uint16_t)(STATE_ADS + STREET_NOBODY * STATE_AD_SIZE);
	uint16_t hoodAt = (uint16_t)(nobody + STREET_AD_HOOD);
	int16_t	 drawHeight = globals_drawHeight;

	cursor_set(CURSOR_HIDE_NOW);
	ui_statusInit(STREET_WIN_STATUS);
	screen_drawResourceAt(
		STREET_WIN_BANNER,
		STREET_WIN_BANNER_LEFT,
		STREET_WIN_BANNER_TOP,
		STREET_WIN_BANNER_FILL
	);
	globals_drawThrough = 0;
	globals_drawHeight = PLAT_HEIGHT;
	plat_copyRect(
		globals_displaySurface,
		globals_workSurface,
		0,
		0,
		PLAT_WIDTH,
		PLAT_HEIGHT
	);
	screen_drawResource(STREET_WIN_PICTURE, -1);
	globals_drawHeight = drawHeight;
	video_showPalette();
	event_waitAnyKey(STREET_WIN_PICTURE_AGE);
	screen_wipe(globals_displaySurface, globals_workSurface);
	globals_place = STREET_HOME;

	int plain = screen_saveRect(
		STREET_WIN_LEFT,
		STREET_WIN_TOP,
		STREET_WIN_WIDTH,
		STREET_WIN_HEIGHT
	);

	screen_copyToDisplay(0, 0, PLAT_WIDTH - 1, PLAT_HEIGHT - 1);
	globals_drawThrough = 0;
	video_showPalette();
	exe_writeU16(hoodAt, STREET_WIN_HOOD);
	street__select(STREET_NOBODY);

	const uint8_t * pAd = exe_data(exe_readU16(STREET_RECORD));

	cars_draw(
		CARS_DRAW_DRIVE_IN,
		exe_readS16(STREET_MODEL),
		pAd[STATE_AD_FLAGS],
		(int8_t)pAd[STATE_AD_WEAR],
		1,
		STREET_WIN_CAR_BOTTOM,
		0,
		(uint16_t)(STATE_TIRE_SPRITES + exe_readS16(STATE_GARAGE_TIRES) * 8)
	);
	exe_writeU16(hoodAt, STREET_NOBODY_HOOD);

	int scene = screen_saveRect(
		STREET_WIN_LEFT,
		STREET_WIN_TOP,
		STREET_WIN_WIDTH,
		STREET_WIN_HEIGHT
	);
	int plainPage = screen_savedPage(plain);
	int scenePage = screen_savedPage(scene);

	if (plainPage < 0 || scenePage < 0) {
		plat_log("the winner's scene has no pages to play in");
	} else {
		const uint8_t * pFirst = exe_data(STREET_WIN_FRAMES);

		street__winFrame(plainPage, 0);
		plat_copyRect(
			plainPage,
			scenePage,
			(int16_t)(STREET_WIN_LEFT + pFirst[4] - STREET_WIN_FRAME_BASE),
			(int16_t)(STREET_WIN_TOP + STREET_WIN_HEIGHT - pFirst[3]),
			pFirst[2],
			pFirst[3]
		);
		event_waitAnyKey(10);

		for (int16_t frame = 1; frame < STREET_WIN_FRAME_COUNT; frame++) {
			if (frame == STREET_WIN_PAUSE_FRAME) {
				screen_waitRetraces(STREET_WIN_PAUSE);
			}

			if (frame < STREET_WIN_FRAME_COUNT - 1) {
				screen_waitRetraces(STREET_WIN_FRAME_WAIT);
				street__winScene(scenePage, globals_displaySurface, frame);
			}

			if (frame == STREET_WIN_FRAME_COUNT - 2) {
				street__winScene(scenePage, globals_workSurface, frame);
			}
		}
	}

	screen_resetSaved();

	int16_t lastLeft = STREET_WIN_STEP_START;
	int16_t lastTop = STREET_WIN_STEP_START;
	int16_t lastWidth = -STREET_WIN_STEP_START;
	int16_t left = 0;
	int16_t top = 0;
	int16_t width = 0;
	int16_t height = 0;

	for (int step = 0; step < STREET_WIN_STEP_COUNT; step++) {
		const uint8_t * pStep =
			exe_data((uint16_t)(STREET_WIN_STEPS + step * 6));
		int16_t right = (int16_t)(pStep[2] + pStep[4]);
		int16_t bottom = (int16_t)(pStep[3] + pStep[5]);

		left = pStep[4];
		top = pStep[5];

		if (right < lastLeft + lastWidth) {
			right = (int16_t)(lastLeft + lastWidth);
		}

		if (lastLeft < left) {
			left = lastLeft;
		}

		/*
		 * The original adds the last width here, not the last height.
		 */
		if (bottom < lastTop + lastWidth) {
			bottom = (int16_t)(lastTop + lastWidth);
		}

		if (bottom > PLAT_HEIGHT) {
			bottom = PLAT_HEIGHT;
		}

		if (lastTop < top) {
			top = lastTop;
		}

		width = (int16_t)(right - left);
		height = (int16_t)(bottom - top);
		lastLeft = pStep[4];
		lastTop = pStep[5];
		lastWidth = pStep[2];
		screen_waitRetraces(STREET_WIN_STEP_WAIT);
		plat_copyRect(
			globals_workSurface,
			globals_displaySurface,
			left,
			top,
			width,
			height
		);
		screen_drawSpritePart(
			globals_displaySurface,
			STREET_WIN_COMER,
			pStep[0],
			pStep[1],
			pStep[2],
			pStep[3],
			pStep[4],
			pStep[5],
			false
		);
		cursor_surfaceCopied(left, top, width, height);
	}

	event_waitAnyKey(0x50);
	screen_waitRetraces(1);
	plat_copyRect(
		globals_workSurface,
		globals_displaySurface,
		left,
		top,
		width,
		height
	);
	cursor_surfaceCopied(left, top, width, height);
	screen_waitRetraces(1);

	if (scenePage >= 0) {
		street__winScene(
			scenePage,
			globals_displaySurface,
			STREET_WIN_FRAME_COUNT - 1
		);
		street__winScene(
			scenePage,
			globals_workSurface,
			STREET_WIN_FRAME_COUNT - 1
		);
	}

	event_waitAnyKey(0x32);
	game_message(STREET_MSG_WON);
	globals_drawThrough = 0;
	screen_resetSaved();
}

/** ****************************************************************************
 *
 * \brief	Let a car come onto the street.
 *
 * \param	index			Index of the ad record.
 * \param	mode			DrawCar mode (4 or 1).
 * \param	how				1: the car drives by; 2: a car without a
 * 							driver drives by while the player is new
 * 							(street__rank 1); 0: it stops.
 *
 * \details	Port of FUN_1000_9a27.
 *
 ******************************************************************************/
static void street__showCar(
	int16_t index,
	int16_t mode,
	int16_t how
) {
	street__select(index);

	if (how == 1) {
		exe_writeU16(CARS_PASSING, 1);
	}

	if (how == 2 && exe_readS16(STREET_INDEX) == STREET_NOBODY) {
		exe_writeU16(CARS_PASSING, street_rank() == 1);
	}

	street__drawCar(
		mode,
		exe_readS16(STREET_MODEL),
		1,
		STREET_CAR_BOTTOM,
		0,
		(uint16_t)(STATE_TIRE_SPRITES + exe_readS16(STATE_GARAGE_TIRES) * 8)
	);
}

/** ****************************************************************************
 *
 * \brief	Let the opponent's car drive off.
 *
 * \details	The call of FUN_1000_92a4 at 1000:ad7b (also in
 * 			FUN_1000_9af4).
 *
 ******************************************************************************/
static void street__carLeaves(void) {
	street__drawCar(
		CARS_DRAW_SCROLL_RIGHT,
		exe_readS16(STREET_MODEL),
		1,
		STREET_CAR_BOTTOM,
		0,
		(uint16_t)(STATE_TIRE_SPRITES + exe_readS16(STATE_GARAGE_TIRES) * 8)
	);
}

/** ****************************************************************************
 *
 * \brief	Build the list of the opponents met so far.
 *
 * \param	pIndexes		Output: ad record index of each item (0-based).
 * \param	pIndents		Output: indent of each item (1-based).
 *
 * \details	Port of FUN_1000_9a83. The list (count, then the drivers'
 * 			names) goes to DS:0x49e0, the area the garage's lists use;
 * 			the original built it on the stack. DS:0x817d becomes 1 when
 * 			no opponent is left, else 0.
 *
 ******************************************************************************/
static void street__listKnown(
	int16_t * pIndexes,
	int16_t * pIndents
) {
	*exe_data(STATE_ADS_DONE) = street_rank() == 0 ? 1 : 0;

	int16_t count = 0;

	for (int index = 0; index < STREET_LISTED; index++) {
		uint16_t record = (uint16_t)(STATE_ADS + index * STATE_AD_SIZE);

		if (*exe_data((uint16_t)(record + STREET_AD_STATE)) == STREET_MET) {
			pIndexes[count] = (int16_t)index;
			count++;
			exe_writeU16(
				(uint16_t)(STREET_LIST + count * 2),
				exe_readU16((uint16_t)(record + STREET_AD_NAME))
			);
			pIndents[count] = STREET_INDENT;
		}
	}

	exe_writeU16(STREET_LIST, (uint16_t)count);
}

/** ****************************************************************************
 *
 * \brief	Let the player call an opponent met before.
 *
 * \param	mode			DrawCar mode for the chosen opponent's car.
 *
 * \return	1 an opponent was chosen; 0 nothing chosen or nobody to
 * 			choose; otherwise the result of the list.
 *
 * \details	Port of FUN_1000_9af4. An opponent on the street drives off
 * 			first; the chosen one stops.
 *
 ******************************************************************************/
static int16_t street__chooseKnown(
	int16_t mode
) {
	int16_t indexes[STREET_LISTED];
	int16_t indents[STREET_LISTED + 1];

	ui_status(-1, exe_text(STREET_TEXT_LIST), 0);
	street__listKnown(indexes, indents);

	int16_t result =
		ui_runList(STREET_LIST_SCREEN, 1, indents, STREET_LIST, NULL, 0);

	if (result == -4) {
		return 0;
	}

	if (result == -3) {
		if (exe_readS16(STREET_HERE) != 0) {
			ui_status(-1, exe_text(STREET_TEXT_PICKED), 0);
			street__carLeaves();
		}

		street__showCar(indexes[ui_listSelection() - 1], mode, 0);

		return 1;
	}

	if (result != UI_LIST_EMPTY) {
		return result;
	}

	game_message(STREET_MSG_NOBODY);

	return 0;
}
