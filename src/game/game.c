/** ****************************************************************************
 *
 * \file	game.c
 *
 * \brief	The game's top level: start-up sequence and main loop.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/game.h"

#include <string.h>

#include "dos/dos.h"
#include "game/calendar.h"
#include "game/cars.h"
#include "game/cursor.h"
#include "game/drive.h"
#include "game/driving.h"
#include "game/event.h"
#include "game/exe.h"
#include "game/fame.h"
#include "game/garage.h"
#include "game/hood.h"
#include "game/music.h"
#include "game/globals.h"
#include "game/hotdata.h"
#include "game/opening.h"
#include "game/paper.h"
#include "game/question.h"
#include "game/random.h"
#include "game/road.h"
#include "game/save.h"
#include "game/screen.h"
#include "game/state.h"
#include "game/station.h"
#include "game/street.h"
#include "game/ui.h"
#include "game/video.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define GAME_LONG_WAIT		  30000	 /**< Ticks game__waitAnyKey gives up at. */
#define GAME_MESSAGE_SCREEN	  0x18	 /**< Screen shown under a message. */
#define GAME_MAX_CARS		  16	 /**< Car records. */
#define GAME_MANY_PARTS		  0x83	 /**< Parts from which there is no room. */
#define GAME_TIME_STEP		  0x222	 /**< Clock ticks an action takes. */
#define GAME_TEXT_CARS_FULL	  0x0e27 /**< Message: 16 cars. */
#define GAME_TEXT_PARTS_FULL  0x0e3f /**< Message: too many parts. */
#define GAME_TEXT_NO_CAR	  0x0efa /**< Message: no car at all. */
#define GAME_TEXT_PICK_CAR	  0x0f16 /**< Message: no car in use. */
#define GAME_TEXT_DRINK		  0x18f6 /**< Message of the drink. */
#define GAME_TEXT_NOT_RUNNING 0x0e7f /**< Message: car not complete. */
#define GAME_TEXT_LOW_GAS	  0x172e /**< Message: too little gas. */
#define GAME_TEXT_NOT_FREE	  0x199f /**< Message: no money for gas. */
#define GAME_TEXT_TANK_FULL	  0x19b1 /**< Message: the tank is full. */
#define GAME_LOW_GAS		  8		 /**< Gas needed for the street. */
#define GAME_TANK_FULL		  0x8c	 /**< Gas at which the tank is full. */
#define GAME_NAME_PICTURE	  0x3f8	 /**< Resource of the name screen. */
#define GAME_NAME_LEFT		  16	 /**< Its left edge. */
#define GAME_NAME_TOP		  6		 /**< Its top edge. */
#define GAME_NAME_SCREEN	  2		 /**< Screen with the name input. */
#define GAME_NAME_LENGTH	  15	 /**< Characters of the name. */
#define GAME_NAME_OK		  (-3)	 /**< Result of OK and Enter. */
#define GAME_NAME_OLD_GAME	  (-2)	 /**< Result of Old Game. */
#define GAME_START_MONEY	  750	 /**< Dollars in a new game. */
#define GAME_DEMO_MONEY		  10000	 /**< Dollars in the demo. */
#define GAME_UNKNOWN_AE		  10	 /**< Start value of DS:0x7eae. */
#define GAME_CLASSES		  3		 /**< Price classes of the ads. */
#define GAME_DEMO_MODEL		  24	 /**< Model of the demo's ad. */
#define GAME_DEMO_PRICE		  0x629	 /**< Value in the demo ad's +6. */
#define GAME_QUIT_SCREEN	  0x1b	 /**< The quit dialog. */
#define GAME_QUIT_END		  (-20)	 /**< Quit dialog: end the program. */
#define GAME_QUIT_SAVE		  (-3)	 /**< Quit dialog: save the game. */
#define GAME_QUIT_CANCEL	  (-4)	 /**< Quit dialog: back to the garage. */
#define GAME_QUIT_ESCAPE	  (-22)	 /**< Quit dialog: Esc. */

/* Results of garage_show: what the player chose. */
#define GAME_CHOICE_PAPER		 1	   /**< Garage: the newspaper. */
#define GAME_CHOICE_STREET		 2	   /**< Garage: hit the street. */
#define GAME_CHOICE_CALENDAR	 3	   /**< Garage: calendar popup. */
#define GAME_CHOICE_QUIT		 4	   /**< Garage: time to quit. */
#define GAME_CHOICE_SELL_PARTS	 5	   /**< Garage: spare parts shelf. */
#define GAME_CHOICE_CARS		 6	   /**< Garage: owned cars list. */
#define GAME_CHOICE_PAINT		 7	   /**< Garage: spray paint. */
#define GAME_CHOICE_TIRES		 8	   /**< Garage: change the tires. */
#define GAME_CHOICE_GEARBOX		 9	   /**< Garage: the transmission. */
#define GAME_CHOICE_HOOD		 10	   /**< Garage: under the hood. */
#define GAME_CHOICE_REAR_BUMPER	 11	   /**< Garage: rear bumper. */
#define GAME_CHOICE_FRONT_BUMPER 12	   /**< Garage: modify front bumper. */
#define GAME_CHOICE_ROOF		 13	   /**< Garage: chop or weld roof. */
#define GAME_CHOICE_STICKERS	 14	   /**< Garage: door stickers. */
#define GAME_CHOICE_TANK		 15	   /**< Garage: the gas pump. */
#define GAME_CHOICE_CAR_INFO	 16	   /**< Garage: car info clipboard. */
#define GAME_CHOICE_DRINK		 17	   /**< Garage: the drink. */
#define GAME_CHOICE_MUSIC		 (-40) /**< Garage: the M key. */

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

static void game__waitAnyKey(
	int16_t ticks
);

static void game__readPaper(void);

static bool game__garageFull(void);

static bool game__play(
	int16_t	  mode,
	int16_t * pNext,
	bool *	  pFromStart
);

static int16_t game__quitDialog(void);

static int16_t game__newGame(void);

static void game__makeAds(void);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/**
 * Port only, for testing: game_start leaves out the opening.
 */
bool game_skipOpening;

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Run the opening and the manual question and load the game data.
 *
 * \return	-1, which makes the main loop (FUN_1000_503f) show the new
 * 			game screen next.
 *
 * \details	Port of FUN_1000_39c0. After the opening the screen is
 * 			blanked, the status line is set up in rows 190..199 and the
 * 			random numbers are seeded. The question is skipped with the
 * 			demo switch. Then hot_data is loaded (unless main did it,
 * 			globals_hotDataLoaded) and the cursor is shown. The game ends
 * 			if hot_data cannot be read. Not ported: saving and restoring
 * 			the surface pointers and the EGA register writes of
 * 			FUN_1f38_0dbd, the music (FUN_1000_1a05, Phase 5), reading
 * 			resources into memory ahead of use (FUN_1f38_61ab,
 * 			FUN_1f38_6887, FUN_1f38_68ca). The road tables are made
 * 			after the question (road_init).
 *
 ******************************************************************************/
int16_t game_start(void) {
	music_startTrack(MUSIC_FIRST_WORD, MUSIC_START_DELAY);

	if (!game_skipOpening) {
		opening_show();
	}

	video_blankPalette();

	globals_drawThrough = 1;
	ui_statusInit(UI_STATUS_ROW);
	random_next(-1);
	video_blankPalette();

	if (globals_demoSwitch == 0) {
		if (question_ask() == 0) {
			dos_exit();
		}

		cursor_set(CURSOR_HIDE_NOW);
	}

	road_init();
	exe_data(STATE_PLAYER_NAME)[0] = 0;
	globals_demoMode = globals_demoSwitch;

	if (globals_hotDataLoaded == 0 && !hotdata_load()) {
		dos_exit();
	}

	cursor_set(CURSOR_SHOW_NOW);

	return -1;
}

/** ****************************************************************************
 *
 * \brief	Run the game.
 *
 * \details	Port of the main loop FUN_1000_503f: game_start, then the new
 * 			game screen or the load of a saved game until one succeeds (a
 * 			load that fails goes to the new game screen; a load that the
 * 			player leaves does too, except when it was chosen in the quit
 * 			dialog, which then goes back to the game). Then the status
 * 			line with the player's money, the newspaper (after a new game,
 * 			if there is room for a car) and the garage loop (game__play).
 * 			The quit dialog can start over from here. The end of a game is
 * 			not ported: the port returns there, as after "Quit".
 *
 ******************************************************************************/
void game_run(void) {
	int16_t next = game_start();
	bool	fromStart = true;

	for (;;) {
		int16_t previous;

		do {
			previous = next;

			if (next == GAME_NEW) {
				fromStart = true;
				next = game__newGame();
			} else {
				next = save_load();
			}

			if (next == GAME_CANCELLED) {
				next = GAME_NEW;
				fromStart = true;
			}
		} while (next != 0 && fromStart);

		ui_statusTitle(exe_text(exe_readU16(STATE_STATUS_TITLE)));

		bool room = exe_readS16(STATE_CAR_COUNT) < GAME_MAX_CARS &&
			exe_readS16(STATE_PART_COUNT) < GAME_MANY_PARTS;

		if (previous == GAME_NEW && room) {
			game__readPaper();
		}

		fromStart = false;

		if (!game__play(GARAGE_DRAW, &next, &fromStart)) {
			return;
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Show a message in a speech bubble until a key, a click or the
 * 			message time ends it.
 *
 * \param	message			Offset of the message from DS:0x2c02.
 *
 * \details	Port of FUN_1000_3ab8: screen 0x18 (one hotspot over the
 * 			whole screen) is open while the message shows for
 * 			globals_messageTicks ticks. A deadline set by the caller
 * 			(globals_timeout) is kept.
 *
 ******************************************************************************/
void game_message(
	int16_t message
) {
	uint32_t savedTimeout = globals_timeout;

	ui_open(GAME_MESSAGE_SCREEN);
	ui_message(message);
	game__waitAnyKey(globals_messageTicks);
	ui_messageClose();
	ui_close(1);
	globals_timeout = savedTimeout;
}

/** ****************************************************************************
 *
 * \brief	Drive the car in use to globals_place.
 *
 * \details	Port of FUN_1000_8d26. The click sources are cleared. With
 * 			a place (not 0) the car uses 2 units of gas (down to 0),
 * 			the palette is blanked (video table entry 7), the cursor is
 * 			hidden, DS:0x58e6 says if the car has the first
 * 			transmission (an automatic), the car is set to the start of
 * 			a drive (driving_reset) and cruises there (driving_run with
 * 			DS:0x8aca 0). The original skips the drive if a click key
 * 			came in since the sources were cleared (DS:0x47ee, set by
 * 			its keyboard interrupt); the port has none in between.
 *
 ******************************************************************************/
void game_drive(void) {
	globals_buttonsHeld = 0;
	globals_joystickButtons = 0;
	globals_mouseLeft = 0;
	globals_clickKeyFlag2 = 0;

	if (globals_place == 0) {
		return;
	}

	uint16_t gasAt = (uint16_t)(exe_readU16(STATE_CAR) + STATE_CAR_FUEL);
	int16_t	 gas = exe_readS16(gasAt);

	exe_writeU16(gasAt, gas < 3 ? 0 : (uint16_t)(gas - 2));
	video_blankPalette();
	cursor_set(CURSOR_HIDE_NOW);

	uint16_t car = exe_readU16(STATE_CAR);
	uint16_t gearbox = exe_readU16((uint16_t)(car + STATE_CAR_GEARBOX));
	int16_t	 part = exe_readS16((uint16_t)(gearbox + STATE_PART_VALUE));
	uint16_t partAt = (uint16_t)(STATE_PARTS + part * 8);
	uint8_t	 kind = *exe_data((uint16_t)(partAt + STATE_PARTS_SUBTYPE));

	exe_writeU16(DRIVE_AUTOMATIC, kind == 0);
	driving_reset(DRIVING_PLAYER);
	exe_writeU16(DRIVE_STATE, 0);
	*exe_data(DRIVE_RACING) = 0;
	driving_run();
}

/** ****************************************************************************
 *
 * \brief	Wait for a key or a click, a number of ticks at most.
 *
 * \param	ticks			Ticks to wait.
 *
 * \details	Port of FUN_1000_3a83.
 *
 ******************************************************************************/
static void game__waitAnyKey(
	int16_t ticks
) {
	globals_timeout = globals_ticks + (uint32_t)(int32_t)ticks;
	globals_anyKeyEnds = 1;
	event_wait(GAME_LONG_WAIT);
	globals_timeout = 0;
	globals_anyKeyEnds = 0;
}

/** ****************************************************************************
 *
 * \brief	Show the newspaper and, if the player picks one, a section of
 * 			ads.
 *
 * \details	Part of FUN_1000_503f: FUN_1000_3b33, then FUN_1000_442b for
 * 			the results 1 and 2.
 *
 ******************************************************************************/
static void game__readPaper(void) {
	int16_t section = paper_show();

	if (section == PAPER_AUTO_PARTS || section == PAPER_USED_CARS) {
		paper_showAds(section);
	}
}

/** ****************************************************************************
 *
 * \brief	Tell the player when there is no room for another car or part.
 *
 * \retval	true	The garage is full; a message was shown.
 * \retval	false	There is room.
 *
 * \details	Port of FUN_1000_3b0c: 16 cars, or 131 parts and more.
 *
 ******************************************************************************/
static bool game__garageFull(void) {
	if (exe_readS16(STATE_CAR_COUNT) >= GAME_MAX_CARS) {
		game_message(GAME_TEXT_CARS_FULL);
		return true;
	}

	if (exe_readS16(STATE_PART_COUNT) >= GAME_MANY_PARTS) {
		game_message(GAME_TEXT_PARTS_FULL);
		return true;
	}

	return false;
}

/** ****************************************************************************
 *
 * \brief	Play from the garage until the game ends or the player quits.
 *
 * \param	mode			How garage_show draws the garage the first time.
 * \param	pNext			Output: what game_run does next (GAME_NEW: the
 * 							new game screen; other values: load a game).
 * \param	pFromStart		Output: true when the game ended, so game_run
 * 							keeps asking until a game is started or loaded.
 *
 * \retval	true	Start over in game_run with *pNext.
 * \retval	false	The program ends ("Quit" in the quit dialog or in the
 * 					dialog after the end of the game).
 *
 * \details	Port of the loop of FUN_1000_503f around FUN_1000_6b06 (from
 * 			1000:50f9): the garage returns what the player chose, and the
 * 			choice is carried out. Ported so far: the newspaper (1), the
 * 			street (2, street_visit),
 * 			messages for the street and the tank without a car (2, 15)
 * 			and for the drink (17), and the M key (-40; the music itself
 * 			is Phase 5). The other choices are logged and do nothing.
 * 			The quit dialog (4, FUN_1000_641e) ends the program, goes back
 * 			to the garage, saves the game (unless the garage is too full)
 * 			or starts over with its result (new game, load a game). At
 * 			the end of the game (from 1000:5113) a summer that is just over
 * 			gets its message and calendar (FUN_1f38_0d62), the hall of
 * 			fame shows (FUN_1000_339f), the end flags are cleared and the
 * 			dialog of FUN_1000_315b ends the program or starts over.
 * 			Not ported: stopping the sound (FUN_1f38_7001) and clearing
 * 			DS:0x4f4a, which only the unported mode 4 of DrawCar sets.
 *
 ******************************************************************************/
static bool game__play(
	int16_t	  mode,
	int16_t * pNext,
	bool *	  pFromStart
) {
	for (;;) {
		int16_t choice = garage_show(mode);
		bool	noCar = exe_readU16(STATE_CAR) == 0;
		bool	stored = exe_readU16(STATE_STORED_CARS) != 0;

		if (choice != 0) {
			music_pause(0);
		}

		if (choice == GARAGE_GAME_END) {
			if (globals_endFlags == FAME_END_SUMMER) {
				fame_summerOver();
			}

			fame_show((int16_t)(globals_endFlags & FAME_END_WON));
			cursor_set(1);
			globals_endFlags = 0;
			*pFromStart = true;
			*pNext = fame_endDialog();

			if (*pNext == FAME_END_QUIT) {
				return false;
			}

			cursor_set(CURSOR_SHOW_NOW);
			return true;
		}

		mode = GARAGE_KEEP;

		switch (choice) {
			case GAME_CHOICE_PAPER:
				if (game__garageFull()) {
					break;
				}

				game__readPaper();
				noCar = exe_readU16(STATE_CAR) == 0;
				stored = exe_readU16(STATE_STORED_CARS) != 0;
				mode = noCar && stored ? GARAGE_DRAW : GARAGE_WIPE_IN;
				globals_gameTime += GAME_TIME_STEP;
				break;

			case GAME_CHOICE_STREET:
			case GAME_CHOICE_TANK:
				if (game__garageFull()) {
					break;
				}

				if (noCar) {
					game_message(
						stored ? GAME_TEXT_PICK_CAR : GAME_TEXT_NO_CAR
					);
					break;
				}

				uint16_t car = exe_readU16(STATE_CAR);

				if (cars_isComplete(car, 1) == 0) {
					game_message(GAME_TEXT_NOT_RUNNING);
					break;
				}

				driving_prepare(-1, DRIVING_PLAYER);

				int16_t gas = exe_readS16((uint16_t)(car + STATE_CAR_FUEL));

				if (choice == GAME_CHOICE_STREET) {
					if (gas > GAME_LOW_GAS) {
						globals_place = STREET_PLACE;
						game_drive();
						globals_gameTime += GAME_TIME_STEP;
						street_visit();
						globals_gameTime += GAME_TIME_STEP;
						mode = GARAGE_DRAW;
					} else {
						game_message(GAME_TEXT_LOW_GAS);
					}

					break;
				}

				if (gas >= GAME_TANK_FULL) {
					game_message(GAME_TEXT_TANK_FULL);
				} else if (exe_readU32(STATE_MONEY) == 0) {
					game_message(GAME_TEXT_NOT_FREE);
				} else {
					globals_place = STATION_PLACE;
					game_drive();
					globals_gameTime += GAME_TIME_STEP;
					station_visit();
					globals_gameTime += GAME_TIME_STEP;
					mode = GARAGE_DRAW;
				}

				break;

			case GAME_CHOICE_QUIT: {
				int16_t answer = game__quitDialog();

				if (answer == GAME_QUIT_END) {
					return false;
				}

				if (answer == GAME_QUIT_ESCAPE || answer == GAME_QUIT_CANCEL ||
					(answer == GAME_QUIT_SAVE && game__garageFull())) {
					break;
				}

				if (answer != GAME_QUIT_SAVE) {
					*pNext = answer;
					return true;
				}

				save_save();
				noCar = exe_readU16(STATE_CAR) == 0;
				stored = exe_readU16(STATE_STORED_CARS) != 0;
				mode = noCar && stored ? GARAGE_DRAW : GARAGE_WIPE_IN;
				break;
			}

			case GAME_CHOICE_CALENDAR:
				calendar_show(0);
				mode = GARAGE_KEEP;
				break;

			case GAME_CHOICE_SELL_PARTS:
				garage_sellParts();
				mode = GARAGE_KEEP;
				break;

			case GAME_CHOICE_CARS:
				mode = garage_ownedCars();
				break;

			case GAME_CHOICE_PAINT:
				garage_paint();
				mode = GARAGE_KEEP;
				break;

			case GAME_CHOICE_REAR_BUMPER:
				garage_modifyBody(CARS_FLAG_REAR_BUMPER);
				mode = GARAGE_KEEP;
				break;

			case GAME_CHOICE_FRONT_BUMPER:
				garage_modifyBody(CARS_FLAG_FRONT_BUMPER);
				mode = GARAGE_KEEP;
				break;

			case GAME_CHOICE_ROOF:
				garage_modifyBody(CARS_FLAG_ROOF);
				mode = GARAGE_KEEP;
				break;

			case GAME_CHOICE_STICKERS:
				garage_stickers();
				mode = GARAGE_KEEP;
				break;

			case GAME_CHOICE_TIRES:
				garage_tires();
				mode = GARAGE_KEEP;
				break;

			case GAME_CHOICE_GEARBOX:
				garage_gearbox();
				mode = GARAGE_WIPE_IN;
				break;

			case GAME_CHOICE_HOOD:
				hood_open(exe_readU16(STATE_CAR));
				mode = GARAGE_WIPE_IN;
				break;

			case GAME_CHOICE_CAR_INFO:
				garage_carInfo();
				mode = GARAGE_KEEP;
				break;

			case GAME_CHOICE_DRINK:
				game_message(GAME_TEXT_DRINK);
				break;

			case GAME_CHOICE_MUSIC:
				music_toggle();
				break;

			default:
				plat_log("garage choice %d is not ported yet", choice);
				break;
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Ask the player what to do when leaving the garage.
 *
 * \return	The result of the quit dialog (screen 0x1b): GAME_QUIT_END,
 * 			GAME_QUIT_SAVE, GAME_QUIT_CANCEL, GAME_QUIT_ESCAPE, or what
 * 			game_run does next (GAME_NEW, or a load).
 *
 * \details	Port of FUN_1000_641e: the area under the screen is saved
 * 			and put back afterwards.
 *
 ******************************************************************************/
static int16_t game__quitDialog(void) {
	return ui_runModal(GAME_QUIT_SCREEN);
}

/** ****************************************************************************
 *
 * \brief	Show the new game screen, where the player types a name, and
 * 			start a new game.
 *
 * \return	0: a new game was started. -2: the player chose "Old Game".
 * 			Other values: the input ended another way.
 *
 * \details	Port of FUN_1000_570c. Draws resource 0x3f8 (lib2/016) at
 * 			(16, 6) on a cleared screen, sets the colors of palette
 * 			entries 6 to 8 and shows the palette, then runs screen 2: an
 * 			input line for up to 15 characters (stored at DS:0x7e9a) and
 * 			the buttons "Old Game" (-2) and "OK" (-3, also Enter).
 *
 ******************************************************************************/
static int16_t game__newGame(void) {
	globals_drawThrough = 0;
	screen_drawResourceAt(GAME_NAME_PICTURE, GAME_NAME_LEFT, GAME_NAME_TOP, 0);
	globals_drawThrough = 1;
	video_setNewGameColors();
	screen_copyToDisplay(0, 0, PLAT_WIDTH - 1, UI_STATUS_ROW - 1);
	video_showPalette();

	char *	pName = (char *)exe_data(STATE_PLAYER_NAME);
	int16_t result = ui_inputScreen(GAME_NAME_SCREEN, GAME_NAME_LENGTH, pName);

	if (result != GAME_NAME_OK) {
		if (result == GAME_NAME_OLD_GAME) {
			return GAME_NAME_OLD_GAME;
		}

		if (result != 0) {
			return result;
		}
	}

	game_initNewGame();

	return 0;
}

/** ****************************************************************************
 *
 * \brief	Set up the state of a new game.
 *
 * \details	Port of FUN_1000_56a9: no cars and no parts, 750 dollars
 * 			(10000 in the demo), new classified ads, the game clock at 0.
 * 			A new route is laid for the road (road_build).
 *
 ******************************************************************************/
void game_initNewGame(void) {
	bool	 demo = globals_demoMode != 0;
	uint32_t money = demo ? GAME_DEMO_MONEY : GAME_START_MONEY;

	game_initRecords(1);
	exe_writeU32(STATE_MONEY, money);
	exe_writeU16(STATE_UNKNOWN_AE, GAME_UNKNOWN_AE);
	road_build(ROAD_NEW_ROUTE);
	game__makeAds();
	globals_gameTime = 0;
	globals_biosTicksAtStart = globals_biosTicks;
	memset(exe_data(STATE_UNKNOWN_4970), 0, 8);
}

/** ****************************************************************************
 *
 * \brief	Mark all car and part records as unused.
 *
 * \param	clearPlayer		Not 0: also clear the player's car, money and the
 * 							other state words (a new game).
 *
 * \details	Port of FUN_1000_3614. The 16 car records (40 bytes each, at
 * 			DS:0x83b0) and the 140 part records (8 bytes each, at
 * 			DS:0x866a) are chained into two lists of unused records.
 *
 ******************************************************************************/
void game_initRecords(
	int16_t clearPlayer
) {
	exe_writeU16(STATE_FREE_CARS, STATE_CAR_RECORDS);

	for (int index = 0; index < STATE_CAR_MAX; index++) {
		int	 record = STATE_CAR_RECORDS + index * STATE_CAR_SIZE;
		bool last = index == STATE_CAR_MAX - 1;
		int	 next = last ? 0 : record + STATE_CAR_SIZE;

		exe_writeU16((uint16_t)(record + STATE_CAR_NEXT), (uint16_t)next);
		exe_writeU16((uint16_t)(record + STATE_CAR_MODEL), 0xffff);
	}

	exe_writeU16(STATE_FREE_PARTS, STATE_PART_RECORDS);

	for (int index = 0; index < STATE_PART_MAX; index++) {
		int	 record = STATE_PART_RECORDS + index * STATE_PART_SIZE;
		bool last = index == STATE_PART_MAX - 1;
		int	 next = last ? 0 : record + STATE_PART_SIZE;

		exe_writeU16((uint16_t)(record + STATE_PART_NEXT), (uint16_t)next);
		exe_writeU16((uint16_t)(record + STATE_PART_VALUE), 0xffff);
	}

	exe_writeU16(STATE_PART_COUNT, 0);
	exe_writeU16(STATE_CAR_COUNT, 0);

	if (clearPlayer != 0) {
		exe_writeU16(STATE_CAR, 0);
		exe_writeU16(STATE_STORED_CARS, 0);
		exe_writeU16(STATE_SPARE_PARTS, 0);
		exe_writeU32(STATE_MONEY, 0);
		exe_writeU16(STATE_UNKNOWN_AE, GAME_UNKNOWN_AE);
	}
}

/** ****************************************************************************
 *
 * \brief	Make the classified ads of a new game.
 *
 * \details	Port of FUN_1000_5584. The 25 car models are sorted into
 * 			three lists by their class byte (below 3, 3 to 5, 6 and up),
 * 			and 7 ads are made from each list: ads 0..6, 7..13 and
 * 			14..20. In the demo the first list loses its last model, and
 * 			the ad at DS:0x4ff4 is made again for model 24 with the value
 * 			0x629 in its +6 field.
 *
 ******************************************************************************/
static void game__makeAds(void) {
	int16_t lists[GAME_CLASSES][STATE_MODEL_COUNT + 1] = { { 0 } };

	for (int16_t model = 0; model < STATE_MODEL_COUNT; model++) {
		int		record = STATE_MODELS + model * STATE_MODEL_SIZE;
		uint8_t level = *exe_data((uint16_t)(record + STATE_MODEL_CLASS));
		int		list = level < 3 ? 0 : (level < 6 ? 1 : 2);

		lists[list][++lists[list][0]] = model;
	}

	if (globals_demoMode != 0) {
		lists[0][0]--;
	}

	int16_t advert = 0;

	for (int list = 0; list < GAME_CLASSES; list++) {
		for (int count = 0; count < STATE_ADS_PER_SET; count++) {
			game_makeAd(advert++, lists[list]);
		}
	}

	*exe_data(STATE_ADS_DONE) = 0;

	if (globals_demoMode != 0) {
		int16_t demoAd = exe_readS16(STATE_DEMO_AD);
		int		record = STATE_ADS + demoAd * STATE_AD_SIZE;

		lists[0][0] = 1;
		lists[0][1] = GAME_DEMO_MODEL;
		game_makeAd(demoAd, lists[0]);
		exe_writeU16((uint16_t)(record + STATE_AD_PRICE), GAME_DEMO_PRICE);
	}
}

/** ****************************************************************************
 *
 * \brief	Make one classified ad.
 *
 * \param	advert			Index of the ad record.
 * \param	pModels			Models to choose from: the count, then the model
 * 							indexes. The chosen one is replaced by -1, so that
 * 							no model is advertised twice.
 *
 * \details	Port of FUN_1000_542e. The ad gets a random color 0..5 (5
 * 			only half as often), a random model of the list, and random
 * 			flags whose chance grows with the model's class: +15 a value
 * 			0..8, +14 bits 0..1 (only if the model's second record has
 * 			+6 and +8 above 0), bit 2 (only if its +2 is above 0) and
 * 			bit 3 (always, if the low 3 bits of the model's +5 are 2).
 * 			What the flags mean is not known yet. The random numbers are
 * 			drawn in the original's order.
 *
 ******************************************************************************/
void game_makeAd(
	int16_t	  advert,
	int16_t * pModels
) {
	int16_t color;

	do {
		color = random_next(6);
	} while (color == 5 && random_next(2) != 1);

	int		  record = STATE_ADS + advert * STATE_AD_SIZE;
	uint8_t * pAd = exe_data((uint16_t)record);

	pAd[STATE_AD_COLOR] = (uint8_t)color;
	memset(pAd + STATE_AD_CLEARED, 0, 6);

	int16_t pick;

	do {
		pick = random_next(pModels[0]);
	} while (pModels[pick + 1] == -1);

	int16_t model = pModels[pick + 1];

	exe_writeU16((uint16_t)(record + STATE_AD_CAR), (uint16_t)model);
	pModels[pick + 1] = -1;

	int				modelAt = STATE_MODELS + model * STATE_MODEL_SIZE;
	int				partsAt = STATE_MODEL_PARTS + model * STATE_MODEL_SIZE;
	const uint8_t * pModel = exe_data((uint16_t)modelAt);
	int16_t			chance =
		(int16_t)((pModel[STATE_MODEL_CLASS] >> 1) + 1);

	pAd[STATE_AD_FLAGS] = (pModel[STATE_MODEL_KIND] & 7) == 2 ? 8 : 0;
	pAd[STATE_AD_WEAR] =
		random_next(20) < chance ? (uint8_t)random_next(9) : 0;

	int16_t rear = exe_readS16((uint16_t)(partsAt + STATE_PARTS_REAR));
	int16_t front = exe_readS16((uint16_t)(partsAt + STATE_PARTS_FRONT));

	if (rear > 0 && front > 0) {
		pAd[STATE_AD_FLAGS] |= random_next(25) < chance ? 3 : 0;
	}

	if (exe_readS16((uint16_t)(partsAt + STATE_PARTS_ROOF)) > 0) {
		pAd[STATE_AD_FLAGS] |= random_next(30) < chance ? 4 : 0;
	}
}
