/** ****************************************************************************
 *
 * \file	garage.c
 *
 * \brief	The garage: the screen the game is played from.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/garage.h"

#include <stdbool.h>
#include <stdio.h> /* snprintf only; no file access or printing */
#include <string.h>

#include "dos/fpu.h"
#include "game/calendar.h"
#include "game/cars.h"
#include "game/cursor.h"
#include "game/effects.h"
#include "game/event.h"
#include "game/exe.h"
#include "game/game.h"
#include "game/globals.h"
#include "game/input.h"
#include "game/lib.h"
#include "game/mover.h"
#include "game/music.h"
#include "game/random.h"
#include "game/screen.h"
#include "game/state.h"
#include "game/ui.h"
#include "game/video.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define GARAGE_PICTURE			 0x408	/**< Resource of the garage. */
#define GARAGE_SCREEN			 10		/**< Screen with its hotspots. */
#define GARAGE_OLD_CAR			 0x4f46 /**< DS: car in use before a switch. */
#define GARAGE_SWITCHED_CAR		 0x4f48 /**< DS: car switched to, or 0. */
#define GARAGE_EMPTY_TEXT		 0x50b5 /**< DS offset: empty text. */
#define GARAGE_CAR_NAME			 0x29ff /**< DS: name of the car shown. */
#define GARAGE_NO_CAR_NAME		 0x5094 /**< DS: banner text without a car. */
#define GARAGE_BLINK_ENTRY		 12		/**< Palette entry that blinks. */
#define GARAGE_BLINK_OFF		 5		/**< Standard color when off. */

#define GARAGE_PARTS_SCREEN		 4	  /**< Spare parts shelf. */
#define GARAGE_CARS_SCREEN		 11	  /**< Owned cars clipboard. */
#define GARAGE_OFFER_SCREEN		 12	  /**< Asking price for a car. */
#define GARAGE_COUNTER_SCREEN	 13	  /**< Buyer's counter-offer. */
#define GARAGE_PAINT_SCREEN		 14	  /**< Spray paint dialog. */
#define GARAGE_CONFIRM_SCREEN	 16	  /**< Offer for a spare part. */
#define GARAGE_BODY_MODAL		 0x22 /**< Body change confirmation. */
#define GARAGE_STICKERS_SCREEN	 0x23 /**< Sticker sheet. */
#define GARAGE_INFO_SCREEN		 0x26 /**< Car info clipboard. */

#define GARAGE_PARTS_DONE		 (-4)	 /**< Screen 4 result: leave. */
#define GARAGE_PARTS_SELL		 (-3)	 /**< Screen 4 result: sell the part. */
#define GARAGE_CARS_DONE		 (-3)	 /**< Screen 11 result: leave. */
#define GARAGE_CARS_DRIVE		 (-1)	 /**< Screen 11 result: drive it. */
#define GARAGE_COUNTER_ACCEPT	 (-20)	 /**< Screen 13 result: take it. */
#define GARAGE_ANSWER_NO		 (-21)	 /**< Screen 16 result: keep it. */
#define GARAGE_INPUT_ESCAPE		 (-22)	 /**< Number input ended by Esc. */
#define GARAGE_HAGGLE_SOLD		 (-1)	 /**< garage__haggle: car sold. */
#define GARAGE_KEY_SELL			 0x1f	 /**< Key code: click (Space). */
#define GARAGE_KEY_CHANGE		 0x2e	 /**< Key code of "Change". */
#define GARAGE_NO_WEAR			 (-0x80) /**< Wear word of a new part. */
#define GARAGE_WORN_OUT			 10000	 /**< Wear of a worn-out part. */

#define GARAGE_KIND_ENGINE		 0 /**< Parts table kind: engine. */
#define GARAGE_KIND_GEARBOX		 1 /**< Parts table kind: transmission. */
#define GARAGE_KIND_CARB		 2 /**< Parts table kind: carburetor. */
#define GARAGE_KIND_INTAKE		 3 /**< Parts table kind: manifold. */
#define GARAGE_KIND_TIRES		 4 /**< Parts table kind: tires. */

#define GARAGE_FPU_HUNDRED		 0x691e /**< DS: real 100. */
#define GARAGE_FPU_SHARE		 0x6926 /**< DS: real 0.9. */
#define GARAGE_FPU_LIMIT		 0x692e /**< DS: real 75. */
#define GARAGE_FPU_TOO_HIGH		 0x6936 /**< DS: real 1.15. */
#define GARAGE_FPU_FIRST_OFFER	 0x693e /**< DS: real 85. */
#define GARAGE_FPU_SECOND_OFFER	 0x6946 /**< DS: real 91. */

#define GARAGE_SELLING_NAME		 0x2990 /**< DS: name of the car sold. */
#define GARAGE_INPUT_BUFFER		 0x29f8 /**< DS: typed asking price. */
#define GARAGE_COUNTER_BUBBLE	 0x0ff7 /**< Message: "How about $..". */
#define GARAGE_COUNTER_POINTER	 0x4fa4 /**< DS: pointer to its text. */
#define GARAGE_COUNTER_FORMAT	 0x4f92 /**< DS: "How about $%-d ?". */
#define GARAGE_COUNTER_SPACE	 18		/**< Bytes for that text. */
#define GARAGE_PARTS_BTN_TEXT	 0x2a3e /**< DS: screen 4 button label. */
#define GARAGE_PARTS_BTN_KEY	 0x57a7 /**< DS: pointer to its key code. */
#define GARAGE_SELL_TEXT		 0x4f5c /**< DS: "Sell it". */
#define GARAGE_SELL_TEXT_SIZE	 8		/**< Bytes copied of it. */
#define GARAGE_CHANGE_TEXT		 0x4f64 /**< DS: "Change". */
#define GARAGE_CHANGE_TEXT_SIZE	 7		/**< Bytes copied of it. */
#define GARAGE_MODAL_PART_NAME	 0x2a5f /**< DS: screen 16 part name. */
#define GARAGE_PRICE_TEXT		 0x3c0c /**< DS: "Will you take $..". */
#define GARAGE_SELL_FORMAT		 0x4f6b /**< DS: "Will you take $%-d ?". */
#define GARAGE_PRICE_SPACE		 22		/**< Bytes for that text. */
#define GARAGE_SPEED_POINTER	 0x5124 /**< DS: pointer to speed line. */
#define GARAGE_SPEED_FORMAT		 0x5108 /**< DS: speed format. */
#define GARAGE_SPEED_COLUMN		 9		/**< Where the speed goes. */
#define GARAGE_SPEED_SPACE		 10		/**< Bytes for the speed. */
#define GARAGE_FUEL_POINTER		 0x5120 /**< DS: pointer to fuel line. */
#define GARAGE_FUEL_FORMAT		 0x5111 /**< DS: " %2i gallons". */
#define GARAGE_FUEL_COLUMN		 5		/**< Where the fuel goes. */
#define GARAGE_FUEL_SPACE		 18		/**< Bytes for the fuel. */
#define GARAGE_INFO_SLOTS		 7		/**< Part slots listed. */
#define GARAGE_INFO_EXTRAS		 4		/**< Extra lines listed. */
#define GARAGE_INFO_LINES		 11		/**< Lines at most. */
#define GARAGE_INFO_INDENTS		 0x50ce /**< DS: 7 indents of info lines. */
#define GARAGE_INFO_EXTRA		 0x511e /**< DS: info extra lines. */
#define GARAGE_PARTS_LIST		 0x49e0 /**< DS: list count and items. */
#define GARAGE_STICKERS_SHEET	 0x4b0	/**< Resource of sticker sheet. */
#define GARAGE_STICKER_BASE		 30		/**< Offset from sticker key. */
#define GARAGE_STICKER_MIN_KEY	 (-38)	/**< Lowest key for stickers. */
#define GARAGE_STICKER_MAX_KEY	 (-31)	/**< Highest key for stickers. */
#define GARAGE_PARTS_SELL_TIME	 0x444	/**< Clock ticks to sell a part. */
#define GARAGE_CAR_SELL_TIME	 0x888	/**< Clock ticks to sell a car. */

#define GARAGE_FIRST_HOTSPOT	 0x74	/**< Index of the first. */
#define GARAGE_HOTSPOTS			 18		/**< Hotspots of the garage. */
#define GARAGE_ELEMENTS			 0x9f4	/**< DS: element records. */
#define GARAGE_ELEMENT_SIZE		 18		/**< Bytes of an element record. */
#define GARAGE_ELEMENT_PICTURE	 4		/**< Element field: picture. */
#define GARAGE_ELEMENT_ID		 6		/**< Element field: number. */
#define GARAGE_MUSIC_HOTSPOT	 (-40)	/**< Number of the music hotspot. */
#define GARAGE_MUSIC_ON			 0x2ba9 /**< DS: its picture, music on. */
#define GARAGE_MUSIC_OFF		 0x2bab /**< DS: its picture, music off. */
#define GARAGE_ELEMENT_LEFT		 8		/**< Element field: left edge. */
#define GARAGE_ELEMENT_TOP		 10		/**< Element field: top edge. */
#define GARAGE_ELEMENT_RIGHT	 12		/**< Element field: right edge. */
#define GARAGE_ELEMENT_BOTTOM	 14		/**< Element field: bottom edge. */
#define GARAGE_TYPE_HOT			 11		/**< Element type: hotspot. */
#define GARAGE_TYPE_SKIPPED		 14		/**< Element type passed over. */
#define GARAGE_CAR_HOTSPOTS		 0x508a /**< DS: 5 car hotspots. */
#define GARAGE_CAR_PARTS		 5		/**< Hotspots on the car. */
#define GARAGE_HOTSPOT_STRIDE	 20		/**< Bytes per model in 0x70e4. */
#define GARAGE_HOTSPOT_BOX		 4		/**< Bytes per box in 0x70e4. */
#define GARAGE_CAR_BOTTOM		 0x98	/**< Bottom row of the car. */
#define GARAGE_MESSAGES			 0x2c02 /**< DS: car names count from. */
#define GARAGE_FITTED_SLOTS		 5		/**< Gearbox, engine, etc. */
#define GARAGE_OVER_TEXT		 0x39c9 /**< DS: parts limit text. */
#define GARAGE_DIGITS			 0x50b6 /**< DS: "1234567890". */
#define GARAGE_MAX_PARTS_HELD	 0x82	/**< Max parts before warning. */

#define GARAGE_FLAG_SUMMER		 0x01 /**< globals_endFlags: summer. */
#define GARAGE_FLAG_BROKE		 0x10 /**< globals_endFlags: broke. */
#define GARAGE_MIN_MONEY		 400  /**< Money needed without a car. */
#define GARAGE_MAX_CARS			 16	  /**< Car records. */
#define GARAGE_MANY_PARTS		 0x83 /**< Parts that fill the garage. */

#define GARAGE_TEXT_NO_RACE		 0x1867 /**< Message: nothing happened. */
#define GARAGE_TEXT_CARS_FULL	 0x0d30 /**< Message: too many cars. */
#define GARAGE_TEXT_PARTS_MAX	 0x0d76 /**< Message: parts at the limit. */
#define GARAGE_TEXT_PARTS_OVER	 0x0dc7 /**< Message: parts over the limit. */
#define GARAGE_TEXT_NO_CAR		 0x0efa /**< Message: no car, dummy. */
#define GARAGE_TEXT_NO_MORE_CARS 0x0ee9 /**< Message: all cars gone. */
#define GARAGE_TEXT_NO_PARTS	 0x0f2a /**< Message: no spare parts. */
#define GARAGE_TEXT_SOLD_ALL	 0x0ed7 /**< Message: all parts gone. */
#define GARAGE_TEXT_NO_VALUE	 0x1021 /**< Message: part for nothing. */
#define GARAGE_TEXT_OFFER		 0x100a /**< Message: "Will you take..". */
#define GARAGE_TEXT_TOO_HIGH	 0x0ec0 /**< Message: must be kidding. */
#define GARAGE_TEXT_HIGH		 0x106e /**< Message: asking above price. */
#define GARAGE_TEXT_STILL_HIGH	 0x1078 /**< Message: get lost. */
#define GARAGE_TEXT_NO_DEAL		 0x1084 /**< Message: no thanks. */
#define GARAGE_TEXT_CHEAP		 0x104b /**< Message: crazy, will take it. */
#define GARAGE_TEXT_SOLD		 0x1091 /**< Message: will take it. */
#define GARAGE_TEXT_NO_TIRES	 0x0f4e /**< Message: no spare tires. */
#define GARAGE_TEXT_BROKE		 0x17ff /**< Message: broke, game over. */
#define GARAGE_TEXT_TAKE_OUT	 0x1875 /**< Message: old one still in. */
#define GARAGE_TEXT_NO_GEARBOX	 0x0f72 /**< Message: no spare gearbox. */
#define GARAGE_TEXT_NO_FIT		 0x10cd /**< Message: that won't fit. */

#define GARAGE_GEARBOX_SCREEN	 0x14	/**< Transmission panel screen. */
#define GARAGE_GEARBOX_PANEL	 0x42e	/**< Resource of the panel. */
#define GARAGE_GEARBOX_PICTURES	 0x49d2 /**< DS: picture per subtype. */
#define GARAGE_GEARBOX_PICTURE	 0x8286 /**< DS: transmission shown, or 0. */
#define GARAGE_GEARBOX_READY	 0x6fc	/**< DS: u8, 0 while drawing it. */
#define GARAGE_GEARBOX_RIGHT	 200	/**< Right edge + 1 of it. */
#define GARAGE_GEARBOX_BOTTOM	 0x7d	/**< Bottom row + 1 of it. */
#define GARAGE_GEARBOX_ASIDE	 0x18	/**< Shift of one that won't fit. */
#define GARAGE_GEARBOX_WAIT		 3000	/**< Ticks event_wait waits. */
#define GARAGE_GEARBOX_CHANGE	 (-2)	/**< Screen 0x14: "Change". */
#define GARAGE_GEARBOX_DONE		 (-1)	/**< Screen 0x14: leave. */
#define GARAGE_GEARBOX_TIME		 0x666	/**< Clock ticks: in or out. */
#define GARAGE_MISFIT_TIME		 0x888	/**< Clock ticks: did not fit. */
#define GARAGE_TIRES_TIME		 0x444	/**< Clock ticks: new tires. */
#define GARAGE_PANEL_LEFT		 0x20	/**< Left edge of the panel. */
#define GARAGE_PANEL_TOP		 10		/**< Top edge of the panel. */
#define GARAGE_PANEL_RIGHT		 0xe7	/**< Right edge of the panel. */
#define GARAGE_PANEL_BOTTOM		 0xb8	/**< Bottom edge of the panel. */
#define GARAGE_STRIP_TOP		 0xe	/**< First row of slides. */
#define GARAGE_STRIP_BOTTOM		 0x94	/**< Last row of slides. */
#define GARAGE_PART_FITS		 3		/**< Parts table: makes it fits. */
#define GARAGE_BOLTS			 0x44f	/**< Resource: bolt frames. */
#define GARAGE_BOLT_ROWS		 0x6fe	/**< DS: row - 10 of each bolt. */
#define GARAGE_BOLT_HOTSPOTS	 0x1804 /**< DS: element of bolt 0. */
#define GARAGE_BOLT_LEFT		 0xb8	/**< Column of the bolts. */
#define GARAGE_BOLT_WIDTH		 16		/**< Width of a bolt frame. */
#define GARAGE_BOLT_HEIGHT		 6		/**< Height of a bolt frame. */
#define GARAGE_BOLT_FALLING		 0x40	/**< Column of the moving frame. */
#define GARAGE_BOLT_START		 8		/**< Row a bolt drops in from. */
#define GARAGE_BOLT_TIGHT		 3		/**< State of a tight bolt. */
#define GARAGE_BOLTS_PER_GEARBOX 2		/**< Bolts of a transmission. */
#define GARAGE_BOLT_MISSING		 0		/**< Bolt mode: not there. */
#define GARAGE_BOLT_TIGHTEN		 1		/**< Bolt mode: tightening. */
#define GARAGE_BOLT_LOOSEN		 2		/**< Bolt mode: loosening. */
#define GARAGE_OFFSETS_SIZE		 12		/**< Bytes of a model's offsets. */
#define GARAGE_JACK_FRAMES		 0x5aaa /**< DS: 3 resources of the jack. */
#define GARAGE_JACK_COUNT		 3		/**< Frames of the jack. */
#define GARAGE_JACK_LEFT		 0x88	/**< Left edge of the jack. */
#define GARAGE_JACK_FLOOR		 0xba	/**< Row below the floor jack. */
#define GARAGE_JACK_DOWN		 0xb9	/**< Jack bottom on the floor. */
#define GARAGE_JACK_UP			 0x99	/**< Jack bottom under the car. */
#define GARAGE_JACK_STEPS		 0x21	/**< Steps of the jack. */
#define GARAGE_JACK_COLOR		 0x2c4	/**< DS: u16, floor (EGA). */
#define GARAGE_LIFT_STEPS		 7		/**< Steps of the car up. */
#define GARAGE_LOWER_STEPS		 9		/**< Steps of the car down. */
#define GARAGE_LIFTED_BOTTOM	 0x94	/**< Bottom of the lifted car. */
#define GARAGE_ARCH				 0x4c7	/**< Resource: inside an arch. */
#define GARAGE_ARCH_TOP			 0x78	/**< Top row of the arch. */
#define GARAGE_WHEEL_COLUMN		 0x28	/**< Width around a wheel. */
#define GARAGE_TIRE_STEPS		 8		/**< Steps of a tire down, up. */
#define GARAGE_TIRE_FLOOR		 0x9b	/**< Tire bottom on the floor. */
#define GARAGE_TIRE_WELL_TOP	 0x7d	/**< Tire top: well in or out. */
#define GARAGE_TIRE_ROLL		 2		/**< Pixels per rolling step. */
#define GARAGE_STEP_RETRACES	 3		/**< Retraces after a step. */
#define GARAGE_ROLL_RETRACES	 2		/**< Retraces after a roll. */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/**
 * What the tire change draws (sprites of TireChangeAnimation).
 */
typedef struct garage__tires_tag {
	mover_Frame jack[GARAGE_JACK_COUNT]; /**< The jack (DS:0x6cda). */
	mover_Frame car;					 /**< Car without tires (DS:0x6df6). */
	mover_Frame shadow;					 /**< The car's shadow. */
	int16_t		carLeft;				 /**< Car left edge (DS:0x6da0). */
	int16_t		jackFrame;				 /**< Jack frame on the scene page. */
} garage__Tires;

/*******************************************************************************
;
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

static void garage__draw(
	int16_t mode
);

static uint16_t garage__hotspot(
	int16_t number
);

static void garage__hideCarHotspots(void);

static void garage__placeCarHotspots(
	int16_t model,
	int16_t carLeft,
	int16_t carTop
);

static void garage__sortHotspots(void);

static bool garage__broke(void);

static void garage__warnWhenFull(void);

static int16_t garage__counterOffer(
	uint16_t * pOffer
);

static fpu_Real garage__constant(
	uint16_t offset
);

static uint16_t garage__partPrice(
	uint16_t part
);

static bool garage__askPrice(
	uint16_t * pAsked
);

static int16_t garage__haggle(
	uint16_t   price,
	uint16_t   limit,
	uint16_t * pAsked
);

static void garage__sellCar(
	const uint16_t * pCars,
	int16_t			 selection
);

static void garage__infoList(
	int16_t * pIndents,
	int8_t *  pPercents
);

static void garage__drawBolt(
	int16_t state,
	int		bolt
);

static void garage__dropBolt(
	int bolt
);

static void garage__drawGearbox(
	int16_t picture,
	bool	aside
);

static void garage__clearGearbox(void);

static void garage__showGearbox(
	int16_t	 picture,
	uint16_t carAt
);

static bool garage__changeGearbox(
	uint16_t  carAt,
	int16_t * pModes
);

static void garage__turnBolt(
	uint16_t  carAt,
	int16_t * pModes,
	int		  bolt
);

static void garage__resourceFrame(
	int16_t		  gameId,
	mover_Frame * pFrame
);

static void garage__drawFrame(
	int					page,
	const mover_Frame * pFrame,
	int16_t				left,
	int16_t				top
);

static int16_t garage__jackTop(
	const garage__Tires * pTires,
	int16_t				  frame
);

static void garage__jackScene(
	garage__Tires * pTires,
	int16_t			frame
);

static void garage__pumpJack(
	garage__Tires * pTires,
	int16_t			frame,
	const int16_t * pClip,
	int16_t			carBottom
);

static void garage__jackArea(
	const garage__Tires * pTires,
	int16_t *			  pTop,
	int16_t *			  pWidth
);

static void garage__showJack(
	const garage__Tires * pTires,
	const mover_Frame *	  pCar,
	int16_t				  carTop
);

static void garage__moveCar(
	garage__Tires *		pTires,
	const mover_Frame * pCar,
	int16_t				bottom,
	int16_t				steps,
	int8_t				step,
	int16_t				frame,
	bool				rising
);

static void garage__runMover(
	int16_t				retraces,
	const mover_Frame * pSwap
);

static void garage__tireFrames(
	uint16_t	  tireTable,
	bool		  well,
	mover_Frame * pFrames
);

static void garage__freeFrames(
	mover_Frame * pFrames,
	int			  count
);

static void garage__swapWheel(
	garage__Tires * pTires,
	int16_t			wheel,
	int16_t			other,
	uint16_t		oldTable,
	uint16_t		newTable
);

static void garage__changeTires(
	uint16_t oldTable,
	uint16_t newTable
);

static uint16_t garage__tireTable(
	uint16_t part
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
 * \brief	Show the garage and wait for the player to choose something.
 *
 * \param	mode			GARAGE_KEEP, GARAGE_DRAW, GARAGE_WIPE_IN, or
 * 							GARAGE_SWITCH_CAR after a car switch.
 *
 * \return	The number of the clicked hotspot of screen 10 (1 newspaper,
 * 			2 street, 3 calendar, 4 quit, 5 sell parts, 6 cars, 7 paint,
 * 			8 tires, 9 transmission, 10 hood, 11..13 bumpers and roof,
 * 			14 stickers, 15 tank, 16 car info, 17 drink), a key result of
 * 			that screen, or GARAGE_GAME_END when the summer is over or
 * 			the player is broke.
 *
 * \details	Port of FUN_1000_6b06. With a car in use, draws or scrolls the
 * 			car into the garage (cars_draw) and places the 5 hotspots on
 * 			its parts (garage__placeCarHotspots); without a car those 5
 * 			hotspots are switched off. Mode GARAGE_SWITCH_CAR without a
 * 			switched car (DS:0x4f48 zero) shows the message
 * 			GARAGE_TEXT_NO_RACE. The music (FUN_1f38_7001) is not ported
 * 			yet. The hotspots are sorted by their left edge, as ui_hitTest
 * 			needs them. A full garage (16 cars, or 131 parts
 * 			and more) gives a warning, and a player with no car, no car in
 * 			store and less than 400 dollars is told that the game is over.
 *
 ******************************************************************************/
int16_t garage_show(
	int16_t mode
) {
	int16_t result = 0;

	if (globals_cursorShown > 0) {
		cursor_set(CURSOR_HIDE_NOW);
	}

	calendar_checkEnd();

	if (globals_endFlags != 0) {
		if (globals_endFlags & GARAGE_FLAG_SUMMER) {
			cursor_set(CURSOR_SHOW_NOW);
		}

		return GARAGE_GAME_END;
	}

	cursor_set(0);
	ui_status(-1, exe_text(GARAGE_EMPTY_TEXT), 0);
	globals_place = 1;

	if (mode == GARAGE_SWITCH_CAR) {
		if (exe_readU16(GARAGE_SWITCHED_CAR) != 0) {
			uint16_t oldCar = exe_readU16(GARAGE_OLD_CAR);

			if (oldCar != 0) {
				int16_t oldModel =
					exe_readS16((uint16_t)(oldCar + STATE_CAR_MODEL));
				uint8_t	 oldFlags = cars_drawFlags(oldCar);
				uint16_t oldMods =
					exe_readU16((uint16_t)(oldCar + STATE_CAR_UNKNOWN_24));
				int16_t	 oldSticker = (int16_t)((oldMods >> 8) & 0x1f);
				uint16_t oldTireRecord =
					exe_readU16((uint16_t)(oldCar + STATE_CAR_TIRES));
				int16_t oldTirePart =
					exe_readS16((uint16_t)(oldTireRecord + STATE_PART_VALUE));
				uint16_t oldTireEntry =
					(uint16_t)(STATE_PARTS + oldTirePart * 8);
				int16_t oldTires = (int8_t)*exe_data(
					(uint16_t)(oldTireEntry + STATE_PARTS_SUBTYPE)
				);

				cars_draw(
					CARS_DRAW_SCROLL_LEFT,
					oldModel,
					oldFlags,
					oldSticker,
					0,
					GARAGE_CAR_BOTTOM,
					0,
					(uint16_t)(STATE_TIRE_SPRITES + oldTires * 8)
				);
				garage_clearCarName();
				ui_open(GARAGE_SCREEN);
				ui_close(1);
			}

			exe_writeU16(GARAGE_SWITCHED_CAR, 0);
			exe_writeU16(GARAGE_OLD_CAR, 0);
			garage__draw(GARAGE_DRAW);
		} else {
			cursor_set(CURSOR_SHOW_NOW);
			game_message(GARAGE_TEXT_NO_RACE);
			cursor_set(CURSOR_HIDE_NOW);
		}
	} else if (mode != GARAGE_KEEP) {
		garage__draw(mode);
	}

	if (exe_readU16(STATE_CAR) == 0) {
		garage__hideCarHotspots();
	} else {
		garage__placeCarHotspots(
			exe_readS16(STATE_GARAGE_MODEL),
			exe_readS16(STATE_CAR_LEFT),
			exe_readS16(STATE_CAR_TOP)
		);
	}

	garage__sortHotspots();
	ui_statusTitle(exe_text(exe_readU16(STATE_STATUS_TITLE)));
	cursor_set(CURSOR_SHOW_NOW);
	garage__warnWhenFull();

	if (garage__broke()) {
		game_message(GARAGE_TEXT_BROKE);
		globals_endFlags |= GARAGE_FLAG_BROKE;
	} else {
		music_pause(0);
		input_flush();
		result = ui_run(GARAGE_SCREEN);
	}

	calendar_checkEnd();

	if (globals_endFlags & GARAGE_FLAG_BROKE) {
		globals_endFlags = GARAGE_FLAG_BROKE;
	}

	if (globals_endFlags != 0) {
		result = GARAGE_GAME_END;

		if ((globals_endFlags & GARAGE_FLAG_SUMMER) == 0) {
			cursor_set(CURSOR_HIDE_NOW);
		}
	}

	return result;
}

/** ****************************************************************************
 *
 * \brief	Draw the garage and the car in use.
 *
 * \param	mode			GARAGE_DRAW: draw through to the display and
 * 							drive the car in from the left.
 * 							GARAGE_WIPE_IN: draw on the work surface and
 * 							copy the finished picture.
 *
 * \details	Part of FUN_1000_6b06 (1000:6c51). Palette entry 12 gets
 * 			color 5 (FUN_1f38_17fc). In mode GARAGE_WIPE_IN a player
 * 			without a car gets the first car in store (DS:0x7eb2) as the
 * 			car in use and switches to GARAGE_DRAW so it drives in.
 *
 ******************************************************************************/
static void garage__draw(
	int16_t mode
) {
	if (mode == GARAGE_WIPE_IN) {
		globals_drawThrough = 0;
	}

	screen_drawResource(GARAGE_PICTURE, -1);
	video_setPaletteColor(GARAGE_BLINK_ENTRY, GARAGE_BLINK_OFF);

	uint16_t stored = exe_readU16(STATE_STORED_CARS);

	if (mode == GARAGE_WIPE_IN && exe_readU16(STATE_CAR) == 0 && stored != 0) {
		uint16_t next = exe_readU16((uint16_t)(stored + STATE_CAR_NEXT));

		exe_writeU16(STATE_CAR, stored);
		exe_writeU16(STATE_STORED_CARS, next);
		screen_copyToDisplay(0, 0, PLAT_WIDTH - 1, PLAT_HEIGHT - 1);
		mode = GARAGE_DRAW;
	}

	globals_drawThrough = 1;
	video_showPalette();

	uint16_t car = exe_readU16(STATE_CAR);

	if (car == 0) {
		screen_copyToDisplay(0, 0, PLAT_WIDTH - 1, PLAT_HEIGHT - 1);
		*exe_data(GARAGE_CAR_NAME) = 0;
		return;
	}

	int16_t	 model = exe_readS16((uint16_t)(car + STATE_CAR_MODEL));
	uint16_t modelAt = (uint16_t)(STATE_MODELS + model * STATE_MODEL_SIZE);
	uint8_t	 flags = cars_drawFlags(car);
	uint16_t mods = exe_readU16((uint16_t)(car + STATE_CAR_UNKNOWN_24));
	int16_t	 sticker = (int16_t)((mods >> 8) & 0x1f);
	int16_t	 paint = (int8_t)*exe_data((uint16_t)(car + STATE_CAR_PAINT));
	uint16_t tireRecord = exe_readU16((uint16_t)(car + STATE_CAR_TIRES));
	int16_t	 tirePart =
		exe_readS16((uint16_t)(tireRecord + STATE_PART_VALUE));
	uint16_t tireEntry = (uint16_t)(STATE_PARTS + tirePart * 8);
	int16_t	 tires =
		(int8_t)*exe_data((uint16_t)(tireEntry + STATE_PARTS_SUBTYPE));

	exe_writeU16(
		STATE_GARAGE_BODY,
		exe_readU16((uint16_t)(modelAt + STATE_MODEL_BODY))
	);
	exe_writeU16(STATE_GARAGE_PAINT, (uint16_t)paint);
	exe_writeU16(STATE_GARAGE_TIRES, (uint16_t)tires);
	exe_writeU16(STATE_GARAGE_HOOD, 0);
	exe_writeU16(STATE_GARAGE_MODEL, (uint16_t)model);
	exe_writeU16(STATE_GARAGE_MODS + 0, (uint16_t)sticker);
	exe_writeU16(STATE_GARAGE_MODS + 2, (flags & CARS_FLAG_ROOF) ? 1 : 0);
	exe_writeU16(
		STATE_GARAGE_MODS + 4,
		(flags & CARS_FLAG_REAR_BUMPER) ? 1 : 0
	);
	exe_writeU16(
		STATE_GARAGE_MODS + 6,
		(flags & CARS_FLAG_FRONT_BUMPER) ? 1 : 0
	);
	exe_writeU16(STATE_GARAGE_MODS + 8, (flags & CARS_FLAG_BLOWER) ? 1 : 0);
	exe_writeU16(STATE_GARAGE_MODS + 10, 0);

	for (int slot = 0; slot < GARAGE_FITTED_SLOTS; slot++) {
		uint16_t partRecord =
			exe_readU16((uint16_t)(car + STATE_CAR_GEARBOX + slot * 2));
		uint16_t partValue = partRecord != 0
			? exe_readU16((uint16_t)(partRecord + STATE_PART_VALUE))
			: 0xffff;

		exe_writeU16((uint16_t)(STATE_GARAGE_PARTS + slot * 2), partValue);
	}

	if (mode == GARAGE_DRAW) {
		cars_setPaint(paint);
	}

	uint16_t	 nameAt = (uint16_t)(modelAt + STATE_MODEL_TEXT);
	uint16_t	 textOffset = exe_readU16(nameAt);
	const char * pCarName = exe_text((uint16_t)(GARAGE_MESSAGES + textOffset));

	strcpy((char *)exe_data(GARAGE_CAR_NAME), pCarName);

	int16_t drawMode = mode == GARAGE_WIPE_IN
		? CARS_DRAW_STATIC_GARAGE
		: CARS_DRAW_DRIVE_IN;

	cars_draw(
		drawMode,
		model,
		flags,
		sticker,
		0,
		GARAGE_CAR_BOTTOM,
		0,
		(uint16_t)(STATE_TIRE_SPRITES + tires * 8)
	);
}

/** ****************************************************************************
 *
 * \brief	Find the garage hotspot with a number.
 *
 * \param	number			The number in the hotspot's +6 field.
 *
 * \return	DS offset of the element record, or 0 when there is none.
 *
 * \details	Port of FUN_1000_6827, which searches the 18 element records
 * 			from index 0x74 and returns the index. (When nothing is found
 * 			the original returns a meaningless value; its callers only
 * 			ask for hotspots that exist.)
 *
 ******************************************************************************/
static uint16_t garage__hotspot(
	int16_t number
) {
	for (int index = 0; index < GARAGE_HOTSPOTS; index++) {
		int element = GARAGE_ELEMENTS +
			(GARAGE_FIRST_HOTSPOT + index) * GARAGE_ELEMENT_SIZE;

		if (exe_readS16((uint16_t)(element + GARAGE_ELEMENT_ID)) == number) {
			return (uint16_t)element;
		}
	}

	return 0;
}

/** ****************************************************************************
 *
 * \brief	Give the garage's music hotspot the picture for music on or off.
 *
 * \param	playing			The music plays.
 *
 * \details	Part of StopPlayer (1000:0d99) and ResumePlayer (1000:0dd8):
 * 			the picture word of hotspot -40 becomes the word at DS:0x2ba9
 * 			(playing) or DS:0x2bab (stopped).
 *
 ******************************************************************************/
void garage_setMusicPicture(
	bool playing
) {
	uint16_t element = garage__hotspot(GARAGE_MUSIC_HOTSPOT);
	uint16_t picture =
		exe_readU16(playing ? GARAGE_MUSIC_ON : GARAGE_MUSIC_OFF);

	exe_writeU16((uint16_t)(element + GARAGE_ELEMENT_PICTURE), picture);
}

/** ****************************************************************************
 *
 * \brief	Switch off the hotspots that lie on the car.
 *
 * \details	Port of FUN_1000_68b3: the hotspots numbered in the table at
 * 			DS:0x508a (bumpers, roof, transmission, hood) get type 14,
 * 			which ui_hitTest passes over.
 *
 ******************************************************************************/
static void garage__hideCarHotspots(void) {
	for (int index = 0; index < GARAGE_CAR_PARTS; index++) {
		int		 entry = GARAGE_CAR_HOTSPOTS + index * 2;
		uint16_t element = garage__hotspot(exe_readS16((uint16_t)entry));

		if (element != 0) {
			exe_writeU16(element, GARAGE_TYPE_SKIPPED);
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Position and enable the 5 hotspots on the car in the garage.
 *
 * \param	model			Car model index, 0..25.
 * \param	carLeft			Left edge of the drawn car.
 * \param	carTop			Top edge of the drawn car.
 *
 * \details	Port of PlaceCarHotspots (1000:68e8). Sets the rectangles of
 * 			the 5 hotspots listed at DS:0x508a from the model's 20-byte
 * 			table entry at DS:0x70e4.
 *
 ******************************************************************************/
static void garage__placeCarHotspots(
	int16_t model,
	int16_t carLeft,
	int16_t carTop
) {
	uint16_t modelBoxes =
		(uint16_t)(STATE_MODEL_HOTSPOTS + model * GARAGE_HOTSPOT_STRIDE);

	for (int index = 0; index < GARAGE_CAR_PARTS; index++) {
		int		 entry = GARAGE_CAR_HOTSPOTS + index * 2;
		uint16_t element = garage__hotspot(exe_readS16((uint16_t)entry));

		if (element == 0) {
			continue;
		}

		const uint8_t * pBox =
			exe_data((uint16_t)(modelBoxes + index * GARAGE_HOTSPOT_BOX));
		int16_t left = (int16_t)(pBox[0] + carLeft);
		int16_t top = (int16_t)(pBox[1] + carTop);
		int16_t right = (int16_t)(left + pBox[2]);
		int16_t bottom = (int16_t)(top + pBox[3]);

		exe_writeU16(element, GARAGE_TYPE_HOT);
		exe_writeU16(
			(uint16_t)(element + GARAGE_ELEMENT_LEFT),
			(uint16_t)left
		);
		exe_writeU16((uint16_t)(element + GARAGE_ELEMENT_TOP), (uint16_t)top);
		exe_writeU16(
			(uint16_t)(element + GARAGE_ELEMENT_RIGHT),
			(uint16_t)right
		);
		exe_writeU16(
			(uint16_t)(element + GARAGE_ELEMENT_BOTTOM),
			(uint16_t)bottom
		);
	}
}

/** ****************************************************************************
 *
 * \brief	Sort the garage's hotspots by their left edge.
 *
 * \details	Port of FUN_1000_6882, which calls FUN_1f38_4265(0x74, 18):
 * 			the 18 element records from index 0x74.
 *
 ******************************************************************************/
static void garage__sortHotspots(void) {
	ui_sortElements(GARAGE_FIRST_HOTSPOT, GARAGE_HOTSPOTS);
}

/** ****************************************************************************
 *
 * \brief	Check whether the player can no longer go on.
 *
 * \retval	true	No car in use, none in store, DS:0x7eb8 is 0 and the
 * 					money is below 400 dollars.
 * \retval	false	The game goes on.
 *
 * \details	Port of FUN_1000_6ad8.
 *
 ******************************************************************************/
static bool garage__broke(void) {
	int32_t money = (int32_t)exe_readU32(STATE_MONEY);

	return exe_readU16(STATE_CAR) == 0 &&
		exe_readU16(STATE_STORED_CARS) == 0 &&
		exe_readU16(STATE_SPARE_PARTS) == 0 && money < GARAGE_MIN_MONEY;
}

/** ****************************************************************************
 *
 * \brief	Warn when the garage holds too many cars or parts.
 *
 * \details	Part of FUN_1000_6b06 (1000:6bdc). Over the limit of parts
 * 			the number of parts above 130 is written into the second line
 * 			of message 0xdc7 before showing it.
 *
 ******************************************************************************/
static void garage__warnWhenFull(void) {
	int16_t parts = exe_readS16(STATE_PART_COUNT);

	if (exe_readS16(STATE_CAR_COUNT) >= GARAGE_MAX_CARS) {
		game_message(GARAGE_TEXT_CARS_FULL);

	} else if (parts == GARAGE_MANY_PARTS) {
		game_message(GARAGE_TEXT_PARTS_MAX);

	} else if (parts > GARAGE_MANY_PARTS) {
		char buffer[8];
		int	 over = parts - GARAGE_MAX_PARTS_HELD;
		int	 length = snprintf(buffer, sizeof buffer, "%d", over);

		if (length > 0) {
			char * pFirstLine = (char *)exe_data(GARAGE_OVER_TEXT);
			char * pSecondLine = pFirstLine + strlen(pFirstLine) + 1;
			char * pNumber = pSecondLine +
				strcspn(pSecondLine, exe_text(GARAGE_DIGITS));

			memcpy(pNumber, buffer, (size_t)length);
			pNumber[length] = ' ';
		}

		game_message(GARAGE_TEXT_PARTS_OVER);
	}
}

/** ****************************************************************************
 *
 * \brief	Clear the name of the car shown in the garage.
 *
 * \details	Port of FUN_1000_6aa6: copies the text at DS:0x5094 (the
 * 			banner without a car) to DS:0x29ff.
 *
 ******************************************************************************/
void garage_clearCarName(void) {
	strcpy((char *)exe_data(GARAGE_CAR_NAME), exe_text(GARAGE_NO_CAR_NAME));
}

/** ****************************************************************************
 *
 * \brief	Show the buyer's counter-offer and get the player's answer.
 *
 * \param	pOffer			In, out: the offer; it is rounded down to a
 * 							multiple of 5 (at least 5).
 *
 * \return	The result of screen 13: GARAGE_COUNTER_ACCEPT when the player
 * 			takes the offer.
 *
 * \details	Port of FUN_1000_4940: the offer is formatted (format at
 * 			DS:0x4f92) into the buffer DS:0x4fa4 points to, which is the
 * 			second line of message 0xff7.
 *
 ******************************************************************************/
static int16_t garage__counterOffer(
	uint16_t * pOffer
) {
	uint16_t remainder = (uint16_t)(*pOffer % 5);

	if (remainder != 0) {
		*pOffer = (uint16_t)(*pOffer - remainder);

		if (*pOffer == 0) {
			*pOffer = 5;
		}
	}

	snprintf(
		(char *)exe_data(exe_readU16(GARAGE_COUNTER_POINTER)),
		GARAGE_COUNTER_SPACE,
		exe_text(GARAGE_COUNTER_FORMAT),
		(int16_t)*pOffer
	);
	ui_bubbleAt(GARAGE_COUNTER_BUBBLE, -1, -1);

	int16_t result = ui_run(GARAGE_COUNTER_SCREEN);

	ui_messageClose();

	return result;
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
static fpu_Real garage__constant(
	uint16_t offset
) {
	return fpu_fromDouble(exe_data(offset));
}

/** ****************************************************************************
 *
 * \brief	The price a buyer pays for a spare part.
 *
 * \param	part			DS offset of the part record.
 *
 * \return	The price, a multiple of 5.
 *
 * \details	Part of FUN_1000_3c8d (1000:3db2..3ec9), with its 8087 code.
 * 			The base is the part's price (cars_partBasePrice). Engines
 * 			and transmissions (kinds 0 and 1) lose with wear: with c the
 * 			whole percent left of 10000 wear units, kept = base * c% *
 * 			0.9, and price = kept + (base - kept) * c% * 0.9.
 * 			Carburetors and manifolds (2, 3) fetch a random 70..89 % of
 * 			the base, tires (4) nothing; other kinds keep the base.
 *
 ******************************************************************************/
static uint16_t garage__partPrice(
	uint16_t part
) {
	uint16_t base = cars_partBasePrice(part);
	int16_t	 index = exe_readS16((uint16_t)(part + STATE_PART_VALUE));
	uint16_t entry = (uint16_t)(STATE_PARTS + index * 8);
	int8_t	 kind = (int8_t)*exe_data((uint16_t)(entry + STATE_PARTS_KIND));
	fpu_Real hundred = garage__constant(GARAGE_FPU_HUNDRED);
	uint16_t price = base;

	switch ((uint16_t)kind) {
		case GARAGE_KIND_ENGINE:
		case GARAGE_KIND_GEARBOX: {
			int16_t wear = exe_readS16((uint16_t)(part + STATE_PART_KIND));
			int16_t left = (int16_t)(GARAGE_WORN_OUT - wear);
			int16_t percent = (int16_t)(left / 100);

			fpu_Real rate = fpu_div(fpu_fromInt((uint16_t)percent), hundred);
			fpu_Real share = garage__constant(GARAGE_FPU_SHARE);
			uint16_t kept = (uint16_t)fpu_toLong(
				fpu_mul(fpu_mul(fpu_fromInt(base), rate), share)
			);

			uint16_t rest = (uint16_t)(base - kept);
			fpu_Real paid = fpu_mul(fpu_mul(fpu_fromInt(rest), rate), share);

			price = (uint16_t)(fpu_toLong(paid) + kept);
			break;
		}

		case GARAGE_KIND_CARB:
		case GARAGE_KIND_INTAKE: {
			int16_t percent = (int16_t)(random_next(20) + 70);

			price = (uint16_t)fpu_toLong(fpu_mul(
				fpu_div(fpu_fromInt(percent), hundred),
				fpu_fromInt(base)
			));
			break;
		}

		case GARAGE_KIND_TIRES:
			price = 0;
			break;

		default:
			break;
	}

	return (uint16_t)(price / 5 * 5);
}

/** ****************************************************************************
 *
 * \brief	Browse the spare parts shelf and sell parts.
 *
 * \details	Port of FUN_1000_3c8d. The list (screen 4) shows the spare
 * 			parts with how worn they are; its button reads "Sell it"
 * 			while it runs (texts DS:0x4f5c and DS:0x4f64 copied to
 * 			DS:0x2a3e, key byte at the pointer DS:0x57a7). A chosen part
 * 			gets a price (garage__partPrice) in a speech bubble over
 * 			screen 16 and is sold unless the player declines. The message
 * 			for an empty shelf depends on whether the list was shown
 * 			before.
 *
 ******************************************************************************/
void garage_sellParts(void) {
	uint16_t parts[STATE_PART_MAX + 1];
	int8_t	 percents[STATE_PART_MAX + 1] = { 0 };
	int16_t	 selection = 1;
	int16_t	 visits = 0;

	for (;;) {
		int16_t	 count = 0;
		uint16_t part = exe_readU16(STATE_SPARE_PARTS);

		while (part != 0) {
			int16_t	 index = exe_readS16((uint16_t)(part + STATE_PART_VALUE));
			uint16_t entry = (uint16_t)(STATE_PARTS + index * 8);
			int16_t	 wear = exe_readS16((uint16_t)(part + STATE_PART_KIND));

			count++;
			parts[count] = part;
			exe_writeU16(
				(uint16_t)(GARAGE_PARTS_LIST + count * 2),
				exe_readU16((uint16_t)(entry + STATE_PARTS_TEXT))
			);
			percents[count] = wear == GARAGE_NO_WEAR
				? (int8_t)GARAGE_NO_WEAR
				: (int8_t)(wear / 100);
			part = exe_readU16((uint16_t)(part + STATE_PART_NEXT));
		}

		exe_writeU16(GARAGE_PARTS_LIST, (uint16_t)count);
		memcpy(
			exe_data(GARAGE_PARTS_BTN_TEXT),
			exe_data(GARAGE_SELL_TEXT),
			GARAGE_SELL_TEXT_SIZE
		);
		*exe_data(exe_readU16(GARAGE_PARTS_BTN_KEY)) = GARAGE_KEY_SELL;

		int16_t choice = ui_runList(
			GARAGE_PARTS_SCREEN,
			selection < count ? selection : count,
			NULL,
			GARAGE_PARTS_LIST,
			percents,
			0
		);

		*exe_data(exe_readU16(GARAGE_PARTS_BTN_KEY)) = GARAGE_KEY_CHANGE;
		memcpy(
			exe_data(GARAGE_PARTS_BTN_TEXT),
			exe_data(GARAGE_CHANGE_TEXT),
			GARAGE_CHANGE_TEXT_SIZE
		);

		if (choice == GARAGE_PARTS_DONE) {
			return;
		}

		if (choice == UI_LIST_EMPTY) {
			game_message(
				visits != 0 ? GARAGE_TEXT_SOLD_ALL : GARAGE_TEXT_NO_PARTS
			);
			return;
		}

		selection = choice == GARAGE_PARTS_SELL ? ui_listSelection() : choice;
		part = parts[selection];

		uint16_t price = garage__partPrice(part);
		int16_t	 index = exe_readS16((uint16_t)(part + STATE_PART_VALUE));
		uint16_t entry = (uint16_t)(STATE_PARTS + index * 8);
		uint16_t text = exe_readU16((uint16_t)(entry + STATE_PARTS_TEXT));
		uint16_t nameAt = (uint16_t)(GARAGE_MESSAGES + text);
		int16_t	 message = GARAGE_TEXT_NO_VALUE;

		strcpy((char *)exe_data(GARAGE_MODAL_PART_NAME), exe_text(nameAt));

		if (price != 0) {
			message = GARAGE_TEXT_OFFER;
			snprintf(
				(char *)exe_data(GARAGE_PRICE_TEXT),
				GARAGE_PRICE_SPACE,
				exe_text(GARAGE_SELL_FORMAT),
				(int16_t)price
			);
		}

		ui_bubbleAt(message, -1, -1);

		int16_t answer = ui_runModal(GARAGE_CONFIRM_SCREEN);

		ui_messageClose();

		if (answer != GARAGE_ANSWER_NO) {
			uint16_t next = exe_readU16((uint16_t)(part + STATE_PART_NEXT));

			if (selection == 1) {
				exe_writeU16(STATE_SPARE_PARTS, next);
			} else {
				exe_writeU16(
					(uint16_t)(parts[selection - 1] + STATE_PART_NEXT),
					next
				);
			}

			cars_freePart(part);
			globals_gameTime += GARAGE_PARTS_SELL_TIME;
			ui_addMoney((int16_t)price);
		}

		visits++;
	}
}

/** ****************************************************************************
 *
 * \brief	Let the player type the asking price for a car.
 *
 * \param	pAsked			Out: the price typed, not 0.
 *
 * \retval	true	A price was typed.
 * \retval	false	The player pressed Esc.
 *
 * \details	Part of FUN_1000_49ed: FUN_1f38_5da7 until the number is not
 * 			0. The typed text stays in the input line between calls.
 *
 ******************************************************************************/
static bool garage__askPrice(
	uint16_t * pAsked
) {
	do {
		int16_t result =
			ui_numberInput(pAsked, (char *)exe_data(GARAGE_INPUT_BUFFER));

		if (result == GARAGE_INPUT_ESCAPE) {
			return false;
		}
	} while (*pAsked == 0);

	return true;
}

/** ****************************************************************************
 *
 * \brief	Haggle with the buyer of a car.
 *
 * \param	price			Price paid for the car.
 * \param	limit			The most the buyer pays.
 * \param	pAsked			Out: the price the car sells for.
 *
 * \return	GARAGE_HAGGLE_SOLD when sold at *pAsked, 0 when the player
 * 			gave up (Esc), otherwise the message that ends the deal.
 *
 * \details	Part of FUN_1000_49ed (1000:4a9e..4c86), with its 8087 code.
 * 			Asking more than price * 1.15 ends it; asking more than the
 * 			price gets one warning. While the asking price is above the
 * 			buyer's offer (at first his limit), he offers 85..89 % and
 * 			then 91..95 % of his limit (the player may take the offer),
 * 			and the player may ask again. At the end he pays when 90 % of
 * 			the asking price is not above his offer.
 *
 ******************************************************************************/
static int16_t garage__haggle(
	uint16_t   price,
	uint16_t   limit,
	uint16_t * pAsked
) {
	uint16_t offer = limit;

	if (!garage__askPrice(pAsked)) {
		return 0;
	}

	uint16_t most = (uint16_t)fpu_toLong(fpu_mul(
		fpu_fromInt((int16_t)price),
		garage__constant(GARAGE_FPU_TOO_HIGH)
	));

	if (most < *pAsked) {
		return GARAGE_TEXT_TOO_HIGH;
	}

	if (price < *pAsked) {
		game_message(GARAGE_TEXT_HIGH);

		if (!garage__askPrice(pAsked)) {
			return 0;
		}

		if (price < *pAsked) {
			return GARAGE_TEXT_STILL_HIGH;
		}
	}

	const uint16_t rounds[2] = { GARAGE_FPU_FIRST_OFFER,
								 GARAGE_FPU_SECOND_OFFER };

	for (int round = 0; round < 2; round++) {
		if (*pAsked > offer) {
			fpu_Real percent = fpu_add(
				fpu_fromInt(random_next(5)),
				garage__constant(rounds[round])
			);

			offer = (uint16_t)fpu_toLong(fpu_div(
				fpu_mul(percent, fpu_fromInt(limit)),
				garage__constant(GARAGE_FPU_HUNDRED)
			));

			if (*pAsked > offer &&
				garage__counterOffer(&offer) == GARAGE_COUNTER_ACCEPT) {
				*pAsked = offer;
			}
		}

		if (*pAsked > offer && !garage__askPrice(pAsked)) {
			return 0;
		}
	}

	fpu_Real share = fpu_mul(
		fpu_fromInt(*pAsked),
		garage__constant(GARAGE_FPU_SHARE)
	);

	if (fpu_compare(share, fpu_fromInt(offer)) > 0) {
		return GARAGE_TEXT_NO_DEAL;
	}

	return GARAGE_HAGGLE_SOLD;
}

/** ****************************************************************************
 *
 * \brief	Try to sell one of the player's cars.
 *
 * \param	pCars			The cars as listed, from index 1.
 * \param	selection		Index of the car in pCars.
 *
 * \details	Part of FUN_1000_49ed (1000:4ef6..4e1a). The buyer's limit is
 * 			75..94 % of the price paid, and screen 12 asks the price
 * 			(garage__haggle). A sold car in use drives off to the left.
 * 			The car is taken apart and its parts are given back.
 *
 ******************************************************************************/
static void garage__sellCar(
	const uint16_t * pCars,
	int16_t			 selection
) {
	uint16_t car = pCars[selection];
	uint16_t activeCar = exe_readU16(STATE_CAR);
	uint16_t price = exe_readU16((uint16_t)(car + STATE_CAR_PRICE));
	int16_t	 model = exe_readS16((uint16_t)(car + STATE_CAR_MODEL));
	uint16_t modelAt = (uint16_t)(STATE_MODELS + model * STATE_MODEL_SIZE);
	uint16_t text = exe_readU16((uint16_t)(modelAt + STATE_MODEL_TEXT));
	uint16_t nameAt = (uint16_t)(GARAGE_MESSAGES + text);
	fpu_Real percent = fpu_add(
		fpu_fromInt(random_next(20)),
		garage__constant(GARAGE_FPU_LIMIT)
	);
	uint16_t limit = (uint16_t)fpu_toLong(fpu_div(
		fpu_mul(percent, fpu_fromInt((int16_t)price)),
		garage__constant(GARAGE_FPU_HUNDRED)
	));

	strcpy((char *)exe_data(GARAGE_SELLING_NAME), exe_text(nameAt));
	*exe_data(GARAGE_INPUT_BUFFER) = '\0';

	int saved = ui_saveScreenRect(GARAGE_OFFER_SCREEN);

	ui_open(GARAGE_OFFER_SCREEN);

	uint16_t asked = 0;
	int16_t	 result = garage__haggle(price, limit, &asked);

	if (result == GARAGE_HAGGLE_SOLD) {
		bool cheap = price > (uint16_t)(asked * 2);

		game_message(cheap ? GARAGE_TEXT_CHEAP : GARAGE_TEXT_SOLD);
		globals_gameTime += GARAGE_CAR_SELL_TIME;
		ui_addMoney((int16_t)asked);

		if (selection == 1 && activeCar != 0) {
			uint16_t tireRecord =
				exe_readU16((uint16_t)(activeCar + STATE_CAR_TIRES));
			int16_t tirePart =
				exe_readS16((uint16_t)(tireRecord + STATE_PART_VALUE));
			uint16_t tireEntry = (uint16_t)(STATE_PARTS + tirePart * 8);
			int16_t	 tires =
				(int8_t)*exe_data((uint16_t)(tireEntry + STATE_PARTS_SUBTYPE));
			uint16_t mods =
				exe_readU16((uint16_t)(activeCar + STATE_CAR_UNKNOWN_24));

			screen_restoreRect(saved, true);
			saved = 0;
			cursor_set(CURSOR_HIDE_NOW);
			cars_draw(
				CARS_DRAW_SCROLL_LEFT,
				model,
				cars_drawFlags(activeCar),
				(int16_t)((mods >> 8) & 0x1f),
				0,
				GARAGE_CAR_BOTTOM,
				0,
				(uint16_t)(STATE_TIRE_SPRITES + tires * 8)
			);
			ui_close(1);
			garage_clearCarName();
			ui_open(GARAGE_SCREEN);
			cursor_set(CURSOR_SHOW_NOW);
			cars_destroyCar(activeCar, 0);
			exe_writeU16(STATE_CAR, 0);
		} else {
			uint16_t next = exe_readU16((uint16_t)(car + STATE_CAR_NEXT));
			int16_t	 firstStored = activeCar == 0 ? 1 : 2;

			if (selection == firstStored) {
				exe_writeU16(STATE_STORED_CARS, next);
			} else {
				exe_writeU16(
					(uint16_t)(pCars[selection - 1] + STATE_CAR_NEXT),
					next
				);
			}

			cars_destroyCar(car, 0);
		}
	} else if (result != 0) {
		game_message(result);
	}

	ui_close(1);

	if (saved != 0) {
		screen_restoreRect(saved, true);
	}
}

/** ****************************************************************************
 *
 * \brief	Browse the player's cars: drive one or sell one.
 *
 * \return	GARAGE_SWITCH_CAR after "Drive it", otherwise GARAGE_KEEP.
 *
 * \details	Port of FUN_1000_49ed. The list (screen 11) shows the car in
 * 			use first, then the stored cars. "Drive it" makes the chosen
 * 			car the one in use and leaves; the garage then swaps the cars
 * 			(DS:0x4f48 the new car, 0 when it was in use already, and
 * 			DS:0x4f46 the old one). Other choices sell the chosen car
 * 			(garage__sellCar) and show the list again. The message for an
 * 			empty list depends on whether the list was shown before.
 *
 ******************************************************************************/
int16_t garage_ownedCars(void) {
	uint16_t cars[GARAGE_MAX_CARS + 1];
	int16_t	 selection = 1;
	int16_t	 visits = 0;

	for (;;) {
		uint16_t activeCar = exe_readU16(STATE_CAR);
		uint16_t car = exe_readU16(STATE_STORED_CARS);
		int16_t	 count = 0;

		if (activeCar != 0) {
			count++;
			cars[count] = activeCar;
		}

		while (car != 0) {
			count++;
			cars[count] = car;
			car = exe_readU16((uint16_t)(car + STATE_CAR_NEXT));
		}

		for (int16_t index = 1; index <= count; index++) {
			int16_t model =
				exe_readS16((uint16_t)(cars[index] + STATE_CAR_MODEL));
			uint16_t modelAt =
				(uint16_t)(STATE_MODELS + model * STATE_MODEL_SIZE);

			exe_writeU16(
				(uint16_t)(GARAGE_PARTS_LIST + index * 2),
				exe_readU16((uint16_t)(modelAt + STATE_MODEL_TEXT))
			);
		}

		exe_writeU16(GARAGE_PARTS_LIST, (uint16_t)count);

		int16_t choice = ui_runList(
			GARAGE_CARS_SCREEN,
			selection < count ? selection : count,
			NULL,
			GARAGE_PARTS_LIST,
			NULL,
			0
		);

		if (choice == GARAGE_CARS_DONE) {
			return GARAGE_KEEP;
		}

		if (choice == UI_LIST_EMPTY) {
			game_message(
				visits != 0 ? GARAGE_TEXT_NO_MORE_CARS : GARAGE_TEXT_NO_CAR
			);
			return GARAGE_KEEP;
		}

		selection = ui_listSelection();

		if (choice != GARAGE_CARS_DRIVE) {
			garage__sellCar(cars, selection);
			visits++;
			continue;
		}

		uint16_t chosen = cars[selection];

		exe_writeU16(GARAGE_OLD_CAR, activeCar);

		if (chosen == activeCar) {
			exe_writeU16(GARAGE_SWITCHED_CAR, 0);

			return GARAGE_SWITCH_CAR;
		}

		uint16_t next = exe_readU16((uint16_t)(chosen + STATE_CAR_NEXT));

		if (selection == (activeCar == 0 ? 1 : 2)) {
			exe_writeU16(STATE_STORED_CARS, next);
		} else {
			exe_writeU16(
				(uint16_t)(cars[selection - 1] + STATE_CAR_NEXT),
				next
			);
		}

		if (activeCar != 0) {
			exe_writeU16(
				(uint16_t)(activeCar + STATE_CAR_NEXT),
				exe_readU16(STATE_STORED_CARS)
			);
			exe_writeU16(STATE_STORED_CARS, activeCar);
		}

		exe_writeU16(GARAGE_SWITCHED_CAR, chosen);
		exe_writeU16(STATE_CAR, chosen);

		return GARAGE_SWITCH_CAR;
	}
}

/** ****************************************************************************
 *
 * \brief	Spray paint the active car in the garage.
 *
 * \details	Port of FUN_1000_6f6b: cycles palette entry 12 across 6 car
 * 			colors, applies paint for $20, removes door stickers, and
 * 			advances time by 0x888 ticks.
 *
 ******************************************************************************/
void garage_paint(void) {
	if (exe_readU16(STATE_CAR) == 0) {
		game_message(GARAGE_TEXT_NO_CAR);
		return;
	}

	int saved = ui_saveScreenRect(GARAGE_PAINT_SCREEN);

	ui_open(GARAGE_PAINT_SCREEN);

	uint16_t car = exe_readU16(STATE_CAR);
	int16_t	 color = (int8_t)*exe_data((uint16_t)(car + STATE_CAR_PAINT));

	for (;;) {
		video_setPaletteColor(
			GARAGE_BLINK_ENTRY,
			*exe_data((uint16_t)(0x50c2 + color * 2))
		);
		video_showPalette();

		int16_t event = event_wait(3000);

		if (event == -23) {
			break;
		}

		if (event == 1) {
			color--;

			if (color < 0) {
				color = 5;
			}
		} else if (event == 2) {
			color++;

			if (color > 5) {
				color = 0;
			}
		} else if (event == 3) {
			if (exe_readU32(STATE_MONEY) < 20) {
				game_message(0x10a1);
			} else {
				ui_close(1);
				screen_restoreRect(saved, true);
				cars_restoreDoor();

				uint8_t * pMods = exe_data(
					(uint16_t)(car + STATE_CAR_UNKNOWN_24 + 1)
				);

				*pMods &= 0xe0;
				*exe_data((uint16_t)(car + STATE_CAR_PAINT)) = (uint8_t)color;
				cars_setPaint(color);
				video_setPaletteColor(GARAGE_BLINK_ENTRY, GARAGE_BLINK_OFF);
				video_showPalette();
				globals_gameTime += 0x888;
				ui_addMoney(-20);
				return;
			}
		}
	}

	ui_close(1);
	video_setPaletteColor(GARAGE_BLINK_ENTRY, GARAGE_BLINK_OFF);
	video_showPalette();
	screen_restoreRect(saved, true);
}

/** ****************************************************************************
 *
 * \brief	Chop or weld roof, or remove or install front/rear bumper.
 *
 * \param	partBit			CARS_FLAG_ROOF (4), CARS_FLAG_REAR_BUMPER (2),
 * 							or CARS_FLAG_FRONT_BUMPER (1).
 *
 * \details	Port of FUN_1000_711a: toggles mod bit in car + 0x25, asks
 * 			confirmation on screen 0x22, deducts $70 or $15, advances time
 * 			by 0x2220 or 0x888, and redraws car via CARS_DRAW_UPDATE_GARAGE.
 *
 ******************************************************************************/
void garage_modifyBody(
	uint8_t partBit
) {
	uint16_t car = exe_readU16(STATE_CAR);

	if (car == 0) {
		game_message(GARAGE_TEXT_NO_CAR);
		return;
	}

	int16_t	 model = exe_readS16((uint16_t)(car + STATE_CAR_MODEL));
	uint16_t modelParts =
		(uint16_t)(STATE_MODEL_PARTS + model * STATE_MODEL_SIZE);
	uint16_t mods = exe_readU16((uint16_t)(car + STATE_CAR_UNKNOWN_24));
	uint16_t textOffset = 0;
	int16_t	 cost = 0;
	int32_t	 timeCost = 0;

	if (partBit & CARS_FLAG_ROOF) {
		if (exe_readS16((uint16_t)(modelParts + STATE_PARTS_ROOF)) == 0) {
			game_message(0x1914);
			return;
		}

		textOffset = (mods & CARS_MOD_ROOF) ? 0x28b1 : 0x28a1;
		cost = 70;
		timeCost = 0x2220;
	} else if (partBit & CARS_FLAG_REAR_BUMPER) {
		if (exe_readS16((uint16_t)(modelParts + STATE_PARTS_REAR)) == 0) {
			game_message(0x1936);
			return;
		}

		textOffset = (mods & CARS_MOD_REAR) ? 0x28d7 : 0x28c4;
		cost = 15;
		timeCost = 0x888;
	} else if (partBit & CARS_FLAG_FRONT_BUMPER) {
		if (exe_readS16((uint16_t)(modelParts + STATE_PARTS_FRONT)) == 0) {
			game_message(0x1936);
			return;
		}

		textOffset = (mods & CARS_MOD_FRONT) ? 0x28d7 : 0x28c4;
		cost = 15;
		timeCost = 0x888;
	}

	if (exe_readU32(STATE_MONEY) < (uint32_t)cost) {
		game_message(0x1959);
		return;
	}

	int saved = ui_saveScreenRect(GARAGE_BODY_MODAL);

	exe_writeU16(0x1e38, (uint16_t)(textOffset - 0x239e));
	ui_open(GARAGE_BODY_MODAL);

	int16_t choice = 0;

	do {
		choice = event_wait(3000);
	} while (choice == 0);

	if (choice == -22) {
		ui_close(1);
		screen_restoreRect(saved, true);
		return;
	}

	if (choice != -20) {
		return;
	}

	uint8_t * pModsHigh = exe_data(
		(uint16_t)(car + STATE_CAR_UNKNOWN_24 + 1)
	);

	if (partBit & CARS_FLAG_ROOF) {
		*pModsHigh ^= 0x20;
	}

	if (partBit & CARS_FLAG_REAR_BUMPER) {
		*pModsHigh ^= 0x40;
	}

	if (partBit & CARS_FLAG_FRONT_BUMPER) {
		*pModsHigh ^= 0x80;
	}

	cursor_set(CURSOR_HIDE_NOW);
	ui_close(1);
	screen_restoreRect(saved, true);

	uint8_t	 flags = cars_drawFlags(car);
	uint16_t newMods = exe_readU16((uint16_t)(car + STATE_CAR_UNKNOWN_24));
	int16_t	 sticker = (int16_t)((newMods >> 8) & 0x1f);
	uint16_t tireRecord = exe_readU16((uint16_t)(car + STATE_CAR_TIRES));
	int16_t	 tirePart =
		exe_readS16((uint16_t)(tireRecord + STATE_PART_VALUE));
	uint16_t tireEntry = (uint16_t)(STATE_PARTS + tirePart * 8);
	int16_t	 tires =
		(int8_t)*exe_data((uint16_t)(tireEntry + STATE_PARTS_SUBTYPE));

	cars_draw(
		CARS_DRAW_UPDATE_GARAGE,
		model,
		flags,
		sticker,
		0,
		GARAGE_CAR_BOTTOM,
		0,
		(uint16_t)(STATE_TIRE_SPRITES + tires * 8)
	);

	effects_noise7841();
	cursor_set(CURSOR_SHOW_NOW);
	globals_gameTime += (uint32_t)timeCost;
	ui_addMoney(-cost);
}

/** ****************************************************************************
 *
 * \brief	Pick and wipe a decal sticker onto the active car's door.
 *
 * \details	Port of FUN_1000_7367: displays sticker sheet 0x4b0 on screen
 * 			0x23, runs hand wiping animation via cars_applySticker, deducts
 * 			$5, and advances time by 0x222 ticks.
 *
 ******************************************************************************/
void garage_stickers(void) {
	uint16_t car = exe_readU16(STATE_CAR);

	if (car == 0) {
		game_message(GARAGE_TEXT_NO_CAR);
		return;
	}

	if (exe_readU32(STATE_MONEY) < 5) {
		game_message(0x1976);
		return;
	}

	int saved = screen_saveRect(24, 11, 264, 69);

	screen_drawResourceAt(GARAGE_STICKERS_SHEET, 24, 11, -1);
	ui_open(GARAGE_STICKERS_SCREEN);

	int16_t choice = 0;

	do {
		choice = event_wait(3000);
	} while (choice == 0);

	if (choice >= GARAGE_STICKER_MIN_KEY && choice <= GARAGE_STICKER_MAX_KEY) {
		cursor_set(CURSOR_HIDE_NOW);
		ui_close(1);
		screen_restoreRect(saved, true);

		int16_t	  sticker = (int16_t)(-choice - GARAGE_STICKER_BASE);
		uint8_t * pMods = exe_data(
			(uint16_t)(car + STATE_CAR_UNKNOWN_24 + 1)
		);

		*pMods = (uint8_t)((*pMods & 0xe0) | (sticker & 0x1f));
		cars_applySticker(sticker);
		globals_gameTime += 0x222;
		ui_addMoney(-5);
		return;
	}

	ui_close(1);
	screen_restoreRect(saved, true);
}

/** ****************************************************************************
 *
 * \brief	Fill the list of the car info clipboard.
 *
 * \param	pIndents		Out: indent of each line, from index 1.
 * \param	pPercents		Out: wear of each line, from index 1.
 *
 * \details	Port of FUN_1000_78f1. The parts of the car in use (engine,
 * 			manifold, the 3 carburetors, transmission, tires; empty slots
 * 			left out) with their indents from the table DS:0x50ce, then
 * 			the 4 extra lines -1..-4 (texts through DS:0x511e).
 *
 ******************************************************************************/
static void garage__infoList(
	int16_t * pIndents,
	int8_t *  pPercents
) {
	uint16_t  car = exe_readU16(STATE_CAR);
	const int slots[GARAGE_INFO_SLOTS] = { STATE_CAR_ENGINE,
										   STATE_CAR_INTAKE,
										   STATE_CAR_CARBS,
										   STATE_CAR_CARBS + 2,
										   STATE_CAR_CARBS + 4,
										   STATE_CAR_GEARBOX,
										   STATE_CAR_TIRES };
	int16_t	  count = 0;

	for (int slot = 0; slot < GARAGE_INFO_SLOTS; slot++) {
		uint16_t part = exe_readU16((uint16_t)(car + slots[slot]));

		if (part == 0) {
			continue;
		}

		int16_t	 index = exe_readS16((uint16_t)(part + STATE_PART_VALUE));
		uint16_t entry = (uint16_t)(STATE_PARTS + index * 8);
		int16_t	 wear = exe_readS16((uint16_t)(part + STATE_PART_KIND));

		count++;
		pIndents[count] =
			exe_readS16((uint16_t)(GARAGE_INFO_INDENTS + slot * 2));
		exe_writeU16(
			(uint16_t)(GARAGE_PARTS_LIST + count * 2),
			exe_readU16((uint16_t)(entry + STATE_PARTS_TEXT))
		);
		pPercents[count] = wear == GARAGE_NO_WEAR
			? (int8_t)GARAGE_NO_WEAR
			: (int8_t)(wear / 100);
	}

	for (int16_t extra = 1; extra < GARAGE_INFO_EXTRAS + 1; extra++) {
		count++;
		exe_writeU16(
			(uint16_t)(GARAGE_PARTS_LIST + count * 2),
			(uint16_t)-extra
		);
		pPercents[count] = (int8_t)GARAGE_NO_WEAR;
		pIndents[count] = 0;
	}

	exe_writeU16(GARAGE_PARTS_LIST, (uint16_t)count);
}

/** ****************************************************************************
 *
 * \brief	Show the car info clipboard for the car in use.
 *
 * \details	Port of FUN_1000_79eb: the list of garage__infoList, with
 * 			the top speed (0 unless the car is complete) and the gallons
 * 			in the tank (car +0x22 / 10) formatted into the extra lines
 * 			(formats DS:0x5108 and DS:0x5111, lines through the pointers
 * 			DS:0x5124 and DS:0x5120).
 *
 ******************************************************************************/
void garage_carInfo(void) {
	uint16_t car = exe_readU16(STATE_CAR);

	if (car == 0) {
		game_message(GARAGE_TEXT_NO_CAR);
		return;
	}

	int16_t indents[GARAGE_INFO_LINES + 1];
	int8_t	percents[GARAGE_INFO_LINES + 1];
	int16_t speed = 0;

	garage__infoList(indents, percents);

	if (cars_isComplete(car, 1) != 0) {
		speed = cars_maxSpeed(-1);
	}

	snprintf(
		(char *)exe_data(
			(uint16_t)(exe_readU16(GARAGE_SPEED_POINTER) + GARAGE_SPEED_COLUMN)
		),
		GARAGE_SPEED_SPACE,
		exe_text(GARAGE_SPEED_FORMAT),
		speed
	);

	int16_t fuel =
		(int16_t)(exe_readS16((uint16_t)(car + STATE_CAR_FUEL)) / 10);

	snprintf(
		(char *)exe_data(
			(uint16_t)(exe_readU16(GARAGE_FUEL_POINTER) + GARAGE_FUEL_COLUMN)
		),
		GARAGE_FUEL_SPACE,
		exe_text(GARAGE_FUEL_FORMAT),
		fuel
	);

	int16_t result = ui_runList(
		GARAGE_INFO_SCREEN,
		1,
		indents,
		GARAGE_PARTS_LIST,
		percents,
		GARAGE_INFO_EXTRA
	);

	if (result == UI_LIST_EMPTY) {
		game_message(GARAGE_TEXT_NO_TIRES);
	}
}

/** ****************************************************************************
 *
 * \brief	List the spare parts of one kind.
 *
 * \param	kind			Kind of part (byte +4 of the parts table entry).
 * \param	pParts			Output: the part records, from index first + 1.
 * \param	pPercents		Output: their wear, from index first + 1.
 * \param	first			Items already in the list.
 *
 * \details	Port of FUN_1000_76a7: the list DS:0x49e0 gets the parts'
 * 			names and its count.
 *
 ******************************************************************************/
void garage_listSpares(
	int8_t	   kind,
	uint16_t * pParts,
	int8_t *   pPercents,
	int16_t	   first
) {
	int16_t count = first;

	for (uint16_t part = exe_readU16(STATE_SPARE_PARTS); part != 0;
		 part = exe_readU16((uint16_t)(part + STATE_PART_NEXT))) {
		int16_t	 index = exe_readS16((uint16_t)(part + STATE_PART_VALUE));
		uint16_t entry = (uint16_t)(STATE_PARTS + index * 8);
		int16_t	 wear = exe_readS16((uint16_t)(part + STATE_PART_KIND));

		if ((int8_t)*exe_data((uint16_t)(entry + STATE_PARTS_KIND)) != kind) {
			continue;
		}

		count++;
		pParts[count] = part;
		exe_writeU16(
			(uint16_t)(GARAGE_PARTS_LIST + count * 2),
			exe_readU16((uint16_t)(entry + STATE_PARTS_TEXT))
		);
		pPercents[count] = wear == GARAGE_NO_WEAR
			? (int8_t)GARAGE_NO_WEAR
			: (int8_t)(wear / 100);
	}

	exe_writeU16(GARAGE_PARTS_LIST, (uint16_t)count);
}

/** ****************************************************************************
 *
 * \brief	Draw a bolt of the transmission in a state.
 *
 * \param	state			0 no bolt, 1..3 loose to tight.
 * \param	bolt			Bolt, 0 or 1.
 *
 * \details	Port of FUN_1f38_3074. The bolt is a frame (16 x 6) of
 * 			resource 0x44f at column 0xb8, row DS:0x6fe[bolt] + 10. A
 * 			bolt put in (state 1) first drops in from row 8 (frame 4,
 * 			when drawing through).
 *
 ******************************************************************************/
static void garage__drawBolt(
	int16_t state,
	int		bolt
) {
	int16_t top =
		(int16_t)(exe_readS16((uint16_t)(GARAGE_BOLT_ROWS + bolt * 2)) + 10);

	cursor_set(CURSOR_HIDE);

	if (state == 1 && globals_drawThrough != 0) {
		screen_slide(
			GARAGE_BOLTS,
			GARAGE_BOLT_FALLING,
			GARAGE_BOLT_WIDTH,
			GARAGE_BOLT_HEIGHT,
			GARAGE_BOLT_LEFT,
			GARAGE_BOLT_START,
			top,
			GARAGE_STRIP_TOP,
			GARAGE_STRIP_BOTTOM
		);
	}

	screen_drawSpritePart(
		globals_workSurface,
		GARAGE_BOLTS,
		(int16_t)(state * GARAGE_BOLT_WIDTH),
		0,
		GARAGE_BOLT_WIDTH,
		GARAGE_BOLT_HEIGHT,
		GARAGE_BOLT_LEFT,
		top,
		true
	);
	cursor_set(CURSOR_SHOW);

	if (globals_drawThrough != 0) {
		screen_copyToDisplay(
			GARAGE_BOLT_LEFT,
			top,
			GARAGE_BOLT_LEFT + GARAGE_BOLT_WIDTH - 1,
			(int16_t)(top + GARAGE_BOLT_HEIGHT - 1)
		);
	}
}

/** ****************************************************************************
 *
 * \brief	Take a bolt out: the hole is shown and the bolt falls down.
 *
 * \param	bolt			Bolt, 0 or 1.
 *
 * \details	Port of FUN_1f38_31a5. The fall (frame 4 to row 0x94) is
 * 			only shown when drawing through, after the bolt's sound.
 *
 ******************************************************************************/
static void garage__dropBolt(
	int bolt
) {
	int16_t top =
		(int16_t)(exe_readS16((uint16_t)(GARAGE_BOLT_ROWS + bolt * 2)) + 10);

	cursor_set(CURSOR_HIDE);
	screen_drawSpritePart(
		globals_workSurface,
		GARAGE_BOLTS,
		0,
		0,
		GARAGE_BOLT_WIDTH,
		GARAGE_BOLT_HEIGHT,
		GARAGE_BOLT_LEFT,
		top,
		true
	);
	cursor_set(CURSOR_SHOW);

	if (globals_drawThrough == 0) {
		return;
	}

	screen_copyToDisplay(
		GARAGE_BOLT_LEFT,
		top,
		GARAGE_BOLT_LEFT + GARAGE_BOLT_WIDTH - 1,
		(int16_t)(top + GARAGE_BOLT_HEIGHT - 1)
	);
	effects_bolt();
	cursor_set(CURSOR_HIDE_NOW);
	screen_slide(
		GARAGE_BOLTS,
		GARAGE_BOLT_FALLING,
		GARAGE_BOLT_WIDTH,
		GARAGE_BOLT_HEIGHT,
		GARAGE_BOLT_LEFT,
		top,
		GARAGE_STRIP_BOTTOM,
		GARAGE_STRIP_TOP,
		GARAGE_STRIP_BOTTOM
	);
	cursor_set(CURSOR_SHOW_NOW);
}

/** ****************************************************************************
 *
 * \brief	Draw a transmission on the panel.
 *
 * \param	picture			Resource of the transmission.
 * \param	aside			true: 24 columns further left (one that does
 * 							not fit).
 *
 * \details	Port of FUN_1f38_32ce. Its right edge is at column 200 and
 * 			its bottom at row 0x7c. When drawing through it slides up
 * 			from row 0x94 (8 columns further left) and is then put in
 * 			place over the background saved before; otherwise it is only
 * 			drawn.
 *
 ******************************************************************************/
static void garage__drawGearbox(
	int16_t picture,
	bool	aside
) {
	const lib_Resource * pSprite = lib_get(lib_indexFromGameId(picture));
	int16_t				 width = (int16_t)pSprite->width;
	int16_t				 height = (int16_t)pSprite->height;
	int16_t				 left = (int16_t)(GARAGE_GEARBOX_RIGHT - width);
	int16_t				 top = (int16_t)(GARAGE_GEARBOX_BOTTOM - height);

	if (aside) {
		left = (int16_t)(left - GARAGE_GEARBOX_ASIDE);
	}

	cursor_set(CURSOR_HIDE_NOW);

	if (globals_drawThrough == 0) {
		screen_slide(
			picture,
			0,
			width,
			height,
			left,
			top,
			top,
			GARAGE_STRIP_TOP,
			GARAGE_STRIP_BOTTOM
		);
		cursor_set(CURSOR_SHOW_NOW);
		return;
	}

	int16_t slideLeft = (int16_t)(left - 8);
	int16_t savedWidth = (int16_t)(width + 8);
	int		saved = screen_saveRect(slideLeft, top, savedWidth, height);

	screen_slide(
		picture,
		0,
		width,
		height,
		slideLeft,
		GARAGE_STRIP_BOTTOM,
		top,
		GARAGE_STRIP_TOP,
		GARAGE_STRIP_BOTTOM
	);
	cursor_set(CURSOR_SHOW_NOW);
	screen_restoreRect(saved, false);
	screen_drawSprite(globals_workSurface, picture, left, top);
	screen_copyToDisplay(
		slideLeft,
		top,
		(int16_t)(left + width - 1),
		(int16_t)(top + height - 1)
	);
}

/** ****************************************************************************
 *
 * \brief	Clear the panel: the transmission and its bolts go away.
 *
 * \details	Port of FUN_1f38_3404: resource 0x42e is put back at
 * 			(0x20, 10), also on the display while drawing through.
 *
 ******************************************************************************/
static void garage__clearGearbox(void) {
	cursor_set(CURSOR_HIDE);
	screen_drawSprite(
		globals_workSurface,
		GARAGE_GEARBOX_PANEL,
		GARAGE_PANEL_LEFT,
		GARAGE_PANEL_TOP
	);
	cursor_set(CURSOR_SHOW);

	if (globals_drawThrough != 0) {
		screen_copyToDisplay(
			GARAGE_PANEL_LEFT,
			GARAGE_PANEL_TOP,
			GARAGE_PANEL_RIGHT,
			GARAGE_PANEL_BOTTOM
		);
	}
}

/** ****************************************************************************
 *
 * \brief	Draw the transmission of the car with its bolts.
 *
 * \param	picture			Resource of the transmission, or 0 for none.
 * \param	carAt			DS offset of the car record (bolts at +0x14).
 *
 * \details	Port of FUN_1f38_345b: everything is drawn on the work
 * 			surface, then the panel is copied to the display. The flag
 * 			DS:0x6fc (use unknown) is 0 meanwhile.
 *
 ******************************************************************************/
static void garage__showGearbox(
	int16_t	 picture,
	uint16_t carAt
) {
	int16_t drawThrough = globals_drawThrough;

	if (picture == 0) {
		return;
	}

	*exe_data(GARAGE_GEARBOX_READY) = 0;
	globals_drawThrough = 0;
	cursor_set(CURSOR_HIDE);
	garage__drawGearbox(picture, false);

	for (int bolt = 0; bolt < GARAGE_BOLTS_PER_GEARBOX; bolt++) {
		int8_t state =
			(int8_t)*exe_data((uint16_t)(carAt + STATE_CAR_WEAR + bolt));

		garage__drawBolt(state, bolt);
	}

	cursor_set(CURSOR_SHOW);
	globals_drawThrough = drawThrough;

	if (drawThrough != 0) {
		screen_copyToDisplay(
			GARAGE_PANEL_LEFT,
			GARAGE_PANEL_TOP,
			GARAGE_PANEL_RIGHT,
			GARAGE_PANEL_BOTTOM
		);
	}

	*exe_data(GARAGE_GEARBOX_READY) = 1;
}

/** ****************************************************************************
 *
 * \brief	Let the player put in a spare transmission.
 *
 * \param	carAt			DS offset of the car record.
 * \param	pModes			In, out: the bolt modes (see garage_gearbox).
 *
 * \retval	true	One was put in (with two loose bolts).
 * \retval	false	The player left the list, or none could be put in.
 *
 * \details	Part of FUN_1000_7aa4 (1000:7b9f..7d44). The list of spare
 * 			transmissions (screen 4) runs until one is chosen that fits
 * 			the car's model (the low 4 bits of the model's word +4 and of
 * 			byte +3 of the part's table entry share a bit). One that does
 * 			not fit is shown aside with a message, and costs time. The
 * 			old transmission must be taken out first. A looping effect
 * 			sounds while the new one moves in.
 *
 ******************************************************************************/
static bool garage__changeGearbox(
	uint16_t  carAt,
	int16_t * pModes
) {
	uint16_t parts[STATE_PART_MAX + 1];
	int8_t	 percents[STATE_PART_MAX + 1];
	uint16_t slotAt = (uint16_t)(carAt + STATE_CAR_GEARBOX);

	garage_listSpares(GARAGE_KIND_GEARBOX, parts, percents, 0);

	for (;;) {
		int16_t choice = ui_runList(
			GARAGE_PARTS_SCREEN,
			1,
			NULL,
			GARAGE_PARTS_LIST,
			percents,
			0
		);

		if (choice == GARAGE_PARTS_DONE) {
			return false;
		}

		if (choice == GARAGE_PARTS_SELL) {
			if (exe_readU16(slotAt) != 0) {
				game_message(GARAGE_TEXT_TAKE_OUT);
				return false;
			}

			choice = ui_listSelection();
		} else if (choice == UI_LIST_EMPTY) {
			game_message(GARAGE_TEXT_NO_GEARBOX);
			return false;
		}

		if (choice == 0) {
			return false;
		}

		uint16_t part = parts[choice];
		int16_t	 index = exe_readS16((uint16_t)(part + STATE_PART_VALUE));
		uint16_t entry = (uint16_t)(STATE_PARTS + index * 8);
		int8_t	 subtype =
			(int8_t)*exe_data((uint16_t)(entry + STATE_PARTS_SUBTYPE));
		int16_t	 model = exe_readS16((uint16_t)(carAt + STATE_CAR_MODEL));
		uint16_t modelAt = (uint16_t)(STATE_MODELS + model * STATE_MODEL_SIZE);
		uint8_t	 makes = *exe_data((uint16_t)(modelAt + STATE_MODEL_FITTED));
		uint8_t	 fits = *exe_data((uint16_t)(entry + GARAGE_PART_FITS));
		int16_t	 previous = exe_readS16(GARAGE_GEARBOX_PICTURE);
		int16_t	 picture = exe_readS16(
			 (uint16_t)(GARAGE_GEARBOX_PICTURES + subtype * 2)
		 );

		exe_writeU16(GARAGE_GEARBOX_PICTURE, (uint16_t)picture);

		if ((makes & fits & 0xf) == 0) {
			garage__drawGearbox(picture, true);
			game_message(GARAGE_TEXT_NO_FIT);
			garage__clearGearbox();
			globals_gameTime += GARAGE_MISFIT_TIME;
			exe_writeU16(GARAGE_GEARBOX_PICTURE, (uint16_t)previous);
			continue;
		}

		cars_fitPart(cars_takeSparePart(part), slotAt);
		cursor_set(CURSOR_HIDE_NOW);
		effects_track76da(1);
		garage__drawGearbox(picture, false);
		effects_track76da(0);
		effects_warble7817();
		globals_gameTime += GARAGE_GEARBOX_TIME;

		for (int bolt = GARAGE_BOLTS_PER_GEARBOX - 1; bolt >= 0; bolt--) {
			uint16_t hotspot =
				(uint16_t)(GARAGE_BOLT_HOTSPOTS + bolt * GARAGE_ELEMENT_SIZE);

			pModes[bolt] = GARAGE_BOLT_TIGHTEN;
			*exe_data((uint16_t)(carAt + STATE_CAR_WEAR + bolt)) = 1;
			garage__drawBolt(1, bolt);
			effects_warble77e1();
			exe_writeU16(hotspot, GARAGE_TYPE_HOT);
		}

		event_reset();
		cursor_set(CURSOR_SHOW_NOW);

		return true;
	}
}

/** ****************************************************************************
 *
 * \brief	Turn a bolt of the transmission.
 *
 * \param	carAt			DS offset of the car record.
 * \param	pModes			In, out: the bolt modes (see garage_gearbox).
 * \param	bolt			Bolt, 0 or 1.
 *
 * \details	Part of FUN_1000_7aa4 (1000:7d47..7ed9). A missing bolt is
 * 			put in loose (1); a bolt being tightened turns up to 3, and
 * 			once tight the next clicks loosen it; at 1 it falls out. With
 * 			both bolts out the transmission goes to the spare parts.
 *
 ******************************************************************************/
static void garage__turnBolt(
	uint16_t  carAt,
	int16_t * pModes,
	int		  bolt
) {
	uint8_t * pState = exe_data((uint16_t)(carAt + STATE_CAR_WEAR + bolt));

	if (pModes[bolt] == GARAGE_BOLT_LOOSEN) {
		*pState = (uint8_t)(*pState - 1);

		if ((int8_t)*pState < 2) {
			*pState = 0;
			pModes[bolt] = GARAGE_BOLT_MISSING;
			garage__dropBolt(bolt);
			effects_warble77e1();
			event_reset();
		} else {
			garage__drawBolt((int8_t)*pState, bolt);
			effects_bolt();
		}

		if (pModes[0] == GARAGE_BOLT_MISSING &&
			pModes[1] == GARAGE_BOLT_MISSING) {
			garage__clearGearbox();
			globals_gameTime += GARAGE_GEARBOX_TIME;
			exe_writeU16(GARAGE_GEARBOX_PICTURE, 0);
			*exe_data((uint16_t)(carAt + STATE_CAR_WEAR + 1)) = 0;
			*exe_data((uint16_t)(carAt + STATE_CAR_WEAR)) = 0;
			cars_removePart((uint16_t)(carAt + STATE_CAR_GEARBOX));
			event_reset();
		}

		return;
	}

	if (pModes[bolt] == GARAGE_BOLT_TIGHTEN) {
		if (*pState == GARAGE_BOLT_TIGHT) {
			pModes[bolt] = GARAGE_BOLT_LOOSEN;
		} else {
			*pState = (uint8_t)(*pState + 1);
			garage__drawBolt((int8_t)*pState, bolt);
			effects_bolt();
		}

		return;
	}

	cursor_set(CURSOR_HIDE_NOW);
	*pState = 1;
	garage__drawBolt(1, bolt);
	effects_warble77e1();
	cursor_set(CURSOR_SHOW_NOW);
	pModes[bolt] = GARAGE_BOLT_TIGHTEN;
}

/** ****************************************************************************
 *
 * \brief	Work on the transmission of the car in use.
 *
 * \details	Port of FUN_1000_7aa4. The panel (resource 0x42e at
 * 			(0x20, 10)) shows the transmission (resources by subtype from
 * 			DS:0x49d2; the one shown is kept in DS:0x8286) and its two
 * 			bolts, whose states are the car's bytes +0x14 and +0x15.
 * 			Screen 0x14: clicking a bolt (hotspots 1 and 2, elements at
 * 			DS:0x1804) turns it (garage__turnBolt), "Change" (-2) puts in
 * 			a spare transmission (garage__changeGearbox), -1 leaves. Each
 * 			bolt has a mode: missing (0), being tightened (1), or being
 * 			loosened (2, after it was tight).
 *
 ******************************************************************************/
void garage_gearbox(void) {
	uint16_t carAt = exe_readU16(STATE_CAR);

	if (carAt == 0) {
		game_message(GARAGE_TEXT_NO_CAR);
		return;
	}

	int16_t modes[GARAGE_BOLTS_PER_GEARBOX];

	for (int bolt = 0; bolt < GARAGE_BOLTS_PER_GEARBOX; bolt++) {
		int8_t state =
			(int8_t)*exe_data((uint16_t)(carAt + STATE_CAR_WEAR + bolt));
		uint16_t hotspot =
			(uint16_t)(GARAGE_BOLT_HOTSPOTS + bolt * GARAGE_ELEMENT_SIZE);

		if (state == 0) {
			modes[bolt] = GARAGE_BOLT_MISSING;
		} else if (state == 1) {
			modes[bolt] = GARAGE_BOLT_TIGHTEN;
		} else {
			modes[bolt] = GARAGE_BOLT_LOOSEN;
		}
		exe_writeU16(hotspot, GARAGE_TYPE_HOT);
	}

	uint16_t part = exe_readU16((uint16_t)(carAt + STATE_CAR_GEARBOX));
	int16_t	 picture = 0;

	if (part != 0) {
		int16_t	 index = exe_readS16((uint16_t)(part + STATE_PART_VALUE));
		uint16_t entry = (uint16_t)(STATE_PARTS + index * 8);
		int8_t	 subtype =
			(int8_t)*exe_data((uint16_t)(entry + STATE_PARTS_SUBTYPE));

		picture = exe_readS16(
			(uint16_t)(GARAGE_GEARBOX_PICTURES + subtype * 2)
		);
	}

	exe_writeU16(GARAGE_GEARBOX_PICTURE, (uint16_t)picture);
	globals_drawThrough = picture == 0 ? 1 : 0;
	screen_drawResourceAt(
		GARAGE_GEARBOX_PANEL,
		GARAGE_PANEL_LEFT,
		GARAGE_PANEL_TOP,
		-1
	);
	globals_drawThrough = 1;
	garage__showGearbox(picture, carAt);
	ui_open(GARAGE_GEARBOX_SCREEN);

	for (;;) {
		int16_t choice;

		do {
			choice = event_wait(GARAGE_GEARBOX_WAIT);
		} while (choice == 0);

		if (choice == GARAGE_GEARBOX_CHANGE) {
			garage__changeGearbox(carAt, modes);
			continue;
		}

		if (choice == GARAGE_GEARBOX_DONE) {
			ui_close(1);
			return;
		}

		bool onBolt = choice >= 1 && choice <= GARAGE_BOLTS_PER_GEARBOX;

		if (onBolt && exe_readS16(GARAGE_GEARBOX_PICTURE) != 0) {
			garage__turnBolt(carAt, modes, choice - 1);
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Get a resource as a frame for the sprite mover.
 *
 * \param	gameId			The game's resource number.
 * \param	pFrame			Output: its image and size.
 *
 ******************************************************************************/
static void garage__resourceFrame(
	int16_t		  gameId,
	mover_Frame * pFrame
) {
	int index = lib_indexFromGameId(gameId);

	pFrame->image = screen_resourceImage(index);
	pFrame->width = 0;
	pFrame->height = 0;

	if (pFrame->image != 0) {
		pFrame->width = (int16_t)lib_get(index)->width;
		pFrame->height = (int16_t)lib_get(index)->height;
	}
}

/** ****************************************************************************
 *
 * \brief	Draw a frame on a page.
 *
 * \param	page			The page.
 * \param	pFrame			The frame.
 * \param	left			Its left edge.
 * \param	top				Its top edge.
 *
 ******************************************************************************/
static void garage__drawFrame(
	int					page,
	const mover_Frame * pFrame,
	int16_t				left,
	int16_t				top
) {
	plat_selectPage(page);
	plat_drawImage(pFrame->image, left, top);
}

/** ****************************************************************************
 *
 * \brief	The top row of a jack frame standing under the car.
 *
 * \param	pTires			The tire change.
 * \param	frame			The jack frame, 0..2.
 *
 * \return	Its top row.
 *
 ******************************************************************************/
static int16_t garage__jackTop(
	const garage__Tires * pTires,
	int16_t				  frame
) {
	return (int16_t)(GARAGE_JACK_UP + 1 - pTires->jack[frame].height);
}

/** ****************************************************************************
 *
 * \brief	Put the scene of the car lift on the scene page: the garage
 * 			without the car and the jack under it.
 *
 * \param	pTires			The tire change.
 * \param	frame			The jack frame, 0..2.
 *
 * \details	The original keeps it in sprite surfaces (DS:0x6dfa, the
 * 			EGA copy DS:0x6cd4) and blits the jack into them.
 *
 ******************************************************************************/
static void garage__jackScene(
	garage__Tires * pTires,
	int16_t			frame
) {
	plat_copyRect(
		globals_workSurface,
		SCREEN_SCENE_PAGE,
		0,
		0,
		PLAT_WIDTH,
		PLAT_HEIGHT
	);
	garage__drawFrame(
		SCREEN_SCENE_PAGE,
		&pTires->jack[frame],
		GARAGE_JACK_LEFT,
		garage__jackTop(pTires, frame)
	);
	pTires->jackFrame = frame;
}

/** ****************************************************************************
 *
 * \brief	Show the next frame of the jack pumping under the car.
 *
 * \param	pTires			The tire change.
 * \param	frame			The jack frame, 0..2.
 * \param	pClip			Rectangle the jack is drawn in over the car.
 * \param	carBottom		Bottom row of the car at its next step.
 *
 * \details	The original blits the frame into the scene surface and its
 * 			mask, which the mover draws in front of the car. The port
 * 			puts the jack back on the scene page and in front of the
 * 			car, and shows the rows of the jack below the car, which the
 * 			mover does not draw.
 *
 ******************************************************************************/
static void garage__pumpJack(
	garage__Tires * pTires,
	int16_t			frame,
	const int16_t * pClip,
	int16_t			carBottom
) {
	int16_t top;
	int16_t width;

	garage__jackArea(pTires, &top, &width);

	int16_t height = (int16_t)(GARAGE_JACK_UP + 1 - top);

	plat_copyRect(
		globals_workSurface,
		SCREEN_SCENE_PAGE,
		GARAGE_JACK_LEFT,
		top,
		width,
		height
	);
	garage__drawFrame(
		SCREEN_SCENE_PAGE,
		&pTires->jack[frame],
		GARAGE_JACK_LEFT,
		garage__jackTop(pTires, frame)
	);
	mover_setFront(
		0,
		&pTires->jack[frame],
		GARAGE_JACK_LEFT,
		garage__jackTop(pTires, frame),
		pClip
	);
	pTires->jackFrame = frame;

	int16_t below = (int16_t)(carBottom + 1 > top ? carBottom + 1 : top);

	if (below <= GARAGE_JACK_UP) {
		height = (int16_t)(GARAGE_JACK_UP + 1 - below);
		plat_copyRect(
			SCREEN_SCENE_PAGE,
			globals_displaySurface,
			GARAGE_JACK_LEFT,
			below,
			width,
			height
		);
		cursor_surfaceCopied(GARAGE_JACK_LEFT, below, width, height);
	}
}

/** ****************************************************************************
 *
 * \brief	Raise or lower the car on the jack.
 *
 * \param	pTires			The tire change.
 * \param	pCar			The car with its tires.
 * \param	bottom			Bottom row of the car at the start.
 * \param	steps			Steps of the move.
 * \param	step			Rows down per step (-1: up).
 * \param	frame			The jack frame at the start, 0..2.
 * \param	rising			The jack handle at the start: true going up
 * 							(frames 0, 1, 2), false going down.
 *
 * \details	Port of the two lift loops of TireChangeAnimation
 * 			(1f38:be93): a mover with the car (StartSpriteMover,
 * 			SetMoverPhase, video table entry 9) and the jack in front of
 * 			it, 3 retraces after each step. The jack pumps 0, 1, 2, 1, 0
 * 			...; while the handle goes down the car steps back, so it
 * 			moves on every other pump.
 *
 ******************************************************************************/
static void garage__moveCar(
	garage__Tires *		pTires,
	const mover_Frame * pCar,
	int16_t				bottom,
	int16_t				steps,
	int8_t				step,
	int16_t				frame,
	bool				rising
) {
	const int16_t clip[4] = {
		pTires->carLeft,
		0,
		(int16_t)(pCar->width - 1),
		GARAGE_JACK_UP,
	};

	garage__jackScene(pTires, frame);
	mover_start(pTires->carLeft, bottom, pCar, 1, SCREEN_SCENE_PAGE);
	mover_setFront(
		0,
		&pTires->jack[frame],
		GARAGE_JACK_LEFT,
		garage__jackTop(pTires, frame),
		clip
	);
	mover_setPhase(0, steps, 0, step);

	int16_t drawnTop = mover_top();

	for (int16_t top = drawnTop; mover_step(); top = mover_top()) {
		drawnTop = top;
		screen_waitRetraces(GARAGE_STEP_RETRACES);

		if (!rising) {
			if (frame == 0) {
				frame = 1;
				rising = true;
			} else {
				frame--;
			}
		} else if (frame == GARAGE_JACK_COUNT - 1) {
			frame = 1;
			rising = false;
		} else {
			frame++;
		}

		if (!rising) {
			mover_setTop((int16_t)(mover_top() - step));
		}

		garage__pumpJack(
			pTires,
			frame,
			clip,
			(int16_t)(mover_top() + pCar->height - 1)
		);
	}

	garage__showJack(pTires, pCar, drawnTop);
}

/** ****************************************************************************
 *
 * \brief	Get the area the jack covers under the car, any frame.
 *
 * \param	pTires			The tire change.
 * \param	pTop			Output: its top row (its bottom row is
 * 							GARAGE_JACK_UP).
 * \param	pWidth			Output: its width from GARAGE_JACK_LEFT.
 *
 ******************************************************************************/
static void garage__jackArea(
	const garage__Tires * pTires,
	int16_t *			  pTop,
	int16_t *			  pWidth
) {
	*pTop = PLAT_HEIGHT;
	*pWidth = 0;

	for (int index = 0; index < GARAGE_JACK_COUNT; index++) {
		int16_t frameTop = garage__jackTop(pTires, (int16_t)index);
		int16_t width = pTires->jack[index].width;

		*pTop = frameTop < *pTop ? frameTop : *pTop;
		*pWidth = width > *pWidth ? width : *pWidth;
	}
}

/** ****************************************************************************
 *
 * \brief	Show the last jack frame of a lift with the car behind it.
 *
 * \param	pTires			The tire change.
 * \param	pCar			The car with its tires.
 * \param	carTop			Top row the car was last drawn at.
 *
 * \details	The jack's last pump comes after the mover's last step, so
 * 			nothing draws it and the frame before stays on the display,
 * 			where the next scene (the wheel swaps, the jack going down)
 * 			does not put the background back. The original leaves it
 * 			too; the port draws the jack area of the display again, so
 * 			no part of the old frame is left over.
 *
 ******************************************************************************/
static void garage__showJack(
	const garage__Tires * pTires,
	const mover_Frame *	  pCar,
	int16_t				  carTop
) {
	const mover_Frame * pJack = &pTires->jack[pTires->jackFrame];
	int16_t				top;
	int16_t				width;

	garage__jackArea(pTires, &top, &width);

	int16_t	  height = (int16_t)(GARAGE_JACK_UP + 1 - top);
	const int clip[4] = { GARAGE_JACK_LEFT, top, width, height };

	plat_copyRect(
		SCREEN_SCENE_PAGE,
		globals_displaySurface,
		GARAGE_JACK_LEFT,
		top,
		width,
		height
	);
	plat_selectPage(globals_displaySurface);
	plat_drawImagePart(
		pCar->image,
		0,
		0,
		pCar->width,
		pCar->height,
		pTires->carLeft,
		carTop,
		clip
	);
	plat_drawImagePart(
		pJack->image,
		0,
		0,
		pJack->width,
		pJack->height,
		GARAGE_JACK_LEFT,
		garage__jackTop(pTires, pTires->jackFrame),
		clip
	);
	cursor_surfaceCopied(GARAGE_JACK_LEFT, top, width, height);
}

/** ****************************************************************************
 *
 * \brief	Move the sprite mover until its phases are over.
 *
 * \param	retraces		Retraces to wait after each step.
 * \param	pSwap			Frame to put in when the sprite's top reaches
 * 							GARAGE_TIRE_WELL_TOP, or NULL.
 *
 ******************************************************************************/
static void garage__runMover(
	int16_t				retraces,
	const mover_Frame * pSwap
) {
	while (mover_step()) {
		if (pSwap && mover_top() == GARAGE_TIRE_WELL_TOP) {
			mover_setFrame(0, pSwap);
		}

		screen_waitRetraces(retraces);
	}
}

/** ****************************************************************************
 *
 * \brief	Make the frames of a tire off the car.
 *
 * \param	tireTable		DS offset of the 4 tire sprite IDs.
 * \param	well			true: with the wheel well behind them.
 * \param	pFrames			Output: 4 frames, in the order of the table.
 *
 ******************************************************************************/
static void garage__tireFrames(
	uint16_t	  tireTable,
	bool		  well,
	mover_Frame * pFrames
) {
	for (int index = 0; index < MOVER_FRAMES; index++) {
		int16_t tireId = exe_readS16((uint16_t)(tireTable + index * 2));

		pFrames[index].image = cars_tireImage(
			tireId,
			well,
			&pFrames[index].width,
			&pFrames[index].height
		);
	}
}

/** ****************************************************************************
 *
 * \brief	Destroy the images of frames.
 *
 * \param	pFrames			The frames.
 * \param	count			How many.
 *
 ******************************************************************************/
static void garage__freeFrames(
	mover_Frame * pFrames,
	int			  count
) {
	for (int index = 0; index < count; index++) {
		if (pFrames[index].image != 0) {
			plat_imageDestroy(pFrames[index].image);
			pFrames[index].image = 0;
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Change one wheel of the lifted car.
 *
 * \param	pTires			The tire change.
 * \param	wheel			Left edge of the wheel on the screen.
 * \param	other			Left edge of the other wheel.
 * \param	oldTable		DS offset of the old tire's 4 sprite IDs.
 * \param	newTable		DS offset of the new tire's 4 sprite IDs.
 *
 * \details	Port of WheelSwapAnimation (1f38:b82c): the old tire drops 8
 * 			rows out of the wheel arch (the wheel well, resource 0x4d2,
 * 			shows behind it from row GARAGE_TIRE_WELL_TOP), rolls off to
 * 			the left, 2 pixels a step, the new tire rolls in from the
 * 			left and rises into the arch; 3 retraces after each step,
 * 			2 while rolling. The car stays in front of the tire. The
 * 			original's scene surfaces (DS:0x5ab0, DS:0x5ab4) hold the
 * 			garage, the jack and the car; the port puts them on the
 * 			scene page, with the other wheel copied from the display.
 * 			A looping effect sounds while a tire rolls.
 *
 ******************************************************************************/
static void garage__swapWheel(
	garage__Tires * pTires,
	int16_t			wheel,
	int16_t			other,
	uint16_t		oldTable,
	uint16_t		newTable
) {
	const mover_Frame * pCar = &pTires->car;
	int16_t				carTop =
		(int16_t)(GARAGE_LIFTED_BOTTOM - pCar->height + 1);
	const int16_t clip[4] = {
		pTires->carLeft,
		carTop,
		pCar->width,
		pCar->height,
	};

	garage__jackScene(pTires, pTires->jackFrame);
	garage__drawFrame(SCREEN_SCENE_PAGE, pCar, pTires->carLeft, carTop);
	garage__drawFrame(
		SCREEN_SCENE_PAGE,
		&pTires->jack[pTires->jackFrame],
		GARAGE_JACK_LEFT,
		garage__jackTop(pTires, pTires->jackFrame)
	);
	plat_copyRect(
		globals_displaySurface,
		SCREEN_SCENE_PAGE,
		other,
		GARAGE_ARCH_TOP,
		GARAGE_WHEEL_COLUMN,
		GARAGE_TIRE_FLOOR + 1 - GARAGE_ARCH_TOP
	);

	mover_Frame plain[MOVER_FRAMES];
	mover_Frame rolling[MOVER_FRAMES];
	mover_Frame ring[MOVER_FRAMES];

	garage__tireFrames(oldTable, false, plain);
	garage__tireFrames(oldTable, true, rolling);
	mover_start(wheel, GARAGE_LIFTED_BOTTOM, plain, 1, SCREEN_SCENE_PAGE);
	mover_setFront(0, pCar, pTires->carLeft, carTop, clip);
	mover_setPhase(0, GARAGE_TIRE_STEPS, 0, 1);
	garage__runMover(GARAGE_STEP_RETRACES, &rolling[0]);

	/*
	 * Rolling to the left, the frames go backwards: 0, 3, 2, 1.
	 */
	ring[0] = rolling[0];

	for (int index = 1; index < MOVER_FRAMES; index++) {
		ring[MOVER_FRAMES - index] = rolling[index];
	}

	mover_start(
		wheel,
		GARAGE_TIRE_FLOOR,
		ring,
		MOVER_FRAMES,
		SCREEN_SCENE_PAGE
	);
	mover_setPhase(
		0,
		(int16_t)((wheel + ring[0].width) >> 1),
		-GARAGE_TIRE_ROLL,
		0
	);
	effects_wheelSwap(1);
	garage__runMover(GARAGE_ROLL_RETRACES, NULL);
	effects_wheelSwap(0);
	garage__freeFrames(plain, MOVER_FRAMES);
	garage__freeFrames(rolling, MOVER_FRAMES);

	garage__tireFrames(newTable, false, plain);
	garage__tireFrames(newTable, true, rolling);
	mover_start(
		(int16_t)-rolling[0].width,
		GARAGE_TIRE_FLOOR,
		rolling,
		MOVER_FRAMES,
		SCREEN_SCENE_PAGE
	);
	mover_setPhase(
		0,
		(int16_t)(((wheel + rolling[0].width) >> 1) + 1),
		GARAGE_TIRE_ROLL,
		0
	);
	effects_wheelSwap(1);
	garage__runMover(GARAGE_ROLL_RETRACES, NULL);
	effects_wheelSwap(0);

	int16_t turn = (int16_t)((wheel & 7) >> 1);

	mover_start(wheel, GARAGE_TIRE_FLOOR, &rolling[turn], 1, SCREEN_SCENE_PAGE);
	mover_setFront(0, pCar, pTires->carLeft, carTop, clip);
	mover_setPhase(0, GARAGE_TIRE_STEPS, 0, -1);
	garage__runMover(GARAGE_STEP_RETRACES, &plain[turn]);
	garage__freeFrames(plain, MOVER_FRAMES);
	garage__freeFrames(rolling, MOVER_FRAMES);
}

/** ****************************************************************************
 *
 * \brief	Show the tire change: the jack lifts the car, both wheels are
 * 			changed, the car comes down and the jack goes back.
 *
 * \param	oldTable		DS offset of the old tire's 4 sprite IDs.
 * \param	newTable		DS offset of the new tire's 4 sprite IDs.
 *
 * \details	Port of TireChangeAnimation (1f38:be93). The floor jack
 * 			(frames DS:0x5aaa) is cleared from the work surface, which
 * 			then gets the garage without the car, the car's shadow and
 * 			the inside of both wheel arches (resource 0x4c7, from
 * 			PrepareWheelArch); the display keeps the old scene. The jack
 * 			rises 0x21 rows under the car, lifts it, the rear and then
 * 			the front wheel are changed (garage__swapWheel), the car
 * 			comes down with the new tires, the jack goes back down, and
 * 			the display is copied to the work surface (video table
 * 			entry 5). The original's sprite surfaces, built for each
 * 			adapter, are the scene page in the port (SCREEN_SCENE_PAGE).
 * 			The music pauses and a looping effect sounds while the car
 * 			moves. Not ported: the sprite memory bookkeeping
 * 			(FUN_1f38_a07e, a09e, a0b8).
 *
 ******************************************************************************/
static void garage__changeTires(
	uint16_t oldTable,
	uint16_t newTable
) {
	uint16_t	  carAt = exe_readU16(STATE_CAR);
	int16_t		  model = exe_readS16((uint16_t)(carAt + STATE_CAR_MODEL));
	uint8_t		  flags = cars_drawFlags(carAt);
	uint16_t	  mods = exe_readU16((uint16_t)(carAt + STATE_CAR_UNKNOWN_24));
	int16_t		  paint = (int16_t)((mods >> 8) & 0x1f);
	int16_t		  drawThrough = globals_drawThrough;
	garage__Tires tires;

	cursor_set(CURSOR_HIDE_NOW);
	globals_drawThrough = 0;
	tires.carLeft = exe_readS16(STATE_CAR_SCROLL_X);

	for (int index = 0; index < GARAGE_JACK_COUNT; index++) {
		garage__resourceFrame(
			exe_readS16((uint16_t)(GARAGE_JACK_FRAMES + index * 2)),
			&tires.jack[index]
		);
	}

	const mover_Frame * pJack = &tires.jack[1];

	screen_fillRect(
		GARAGE_JACK_LEFT,
		(int16_t)(GARAGE_JACK_FLOOR - pJack->height),
		pJack->width,
		pJack->height,
		(uint8_t)exe_readU16(GARAGE_JACK_COLOR)
	);
	plat_copyRect(
		globals_workSurface,
		SCREEN_SCENE_PAGE,
		0,
		0,
		PLAT_WIDTH,
		PLAT_HEIGHT
	);

	cars_buildForTires(
		model,
		flags,
		paint,
		&tires.car.image,
		&tires.shadow.image,
		&tires.car.width,
		&tires.car.height
	);
	tires.shadow.width = tires.car.width;
	tires.shadow.height = tires.car.height;

	const uint8_t * pOffsets = exe_data(
		(uint16_t)(STATE_MODEL_OFFSETS + model * GARAGE_OFFSETS_SIZE)
	);
	int16_t rear = (int16_t)(tires.carLeft + pOffsets[0]);
	int16_t front = (int16_t)(tires.carLeft + pOffsets[1]);
	int16_t carTop = (int16_t)(GARAGE_CAR_BOTTOM - tires.car.height + 1);

	screen_drawResource(GARAGE_PICTURE, -1);

	/*
	 * The garage picture has the floor jack; the original's jack goes
	 * down over the column it saved before (DS:0x6d28), without it.
	 */
	plat_copyRect(
		SCREEN_SCENE_PAGE,
		globals_workSurface,
		GARAGE_JACK_LEFT,
		(int16_t)(GARAGE_JACK_FLOOR - pJack->height),
		pJack->width,
		pJack->height
	);
	garage__drawFrame(
		globals_workSurface,
		&tires.shadow,
		tires.carLeft,
		carTop
	);

	mover_Frame arch;

	garage__resourceFrame(GARAGE_ARCH, &arch);
	garage__drawFrame(globals_workSurface, &arch, front, GARAGE_ARCH_TOP);
	garage__drawFrame(globals_workSurface, &arch, rear, GARAGE_ARCH_TOP);

	mover_start(
		GARAGE_JACK_LEFT,
		GARAGE_JACK_DOWN,
		pJack,
		1,
		SCREEN_SCENE_PAGE
	);
	mover_setPhase(0, GARAGE_JACK_STEPS, 0, -1);
	garage__runMover(GARAGE_STEP_RETRACES, NULL);
	music_pause(1);

	mover_Frame withTires = tires.car;

	withTires.image = cars_buildWithTires(model, flags, paint, oldTable);
	effects_tireChange(1);
	garage__moveCar(
		&tires,
		&withTires,
		GARAGE_CAR_BOTTOM,
		GARAGE_LIFT_STEPS,
		-1,
		0,
		false
	);
	effects_tireChange(0);
	garage__freeFrames(&withTires, 1);

	garage__swapWheel(&tires, rear, front, oldTable, newTable);
	garage__swapWheel(&tires, front, rear, oldTable, newTable);

	withTires.image = cars_buildWithTires(model, flags, paint, newTable);
	effects_tireChange(1);
	garage__moveCar(
		&tires,
		&withTires,
		GARAGE_LIFTED_BOTTOM,
		GARAGE_LOWER_STEPS,
		1,
		GARAGE_JACK_COUNT - 1,
		true
	);
	effects_tireChange(0);
	music_pause(0);

	plat_copyRect(
		globals_workSurface,
		SCREEN_SCENE_PAGE,
		0,
		0,
		PLAT_WIDTH,
		PLAT_HEIGHT
	);
	garage__drawFrame(SCREEN_SCENE_PAGE, &withTires, tires.carLeft, carTop);
	mover_start(GARAGE_JACK_LEFT, GARAGE_JACK_UP, pJack, 1, SCREEN_SCENE_PAGE);
	mover_setPhase(0, GARAGE_JACK_STEPS, 0, 1);
	garage__runMover(GARAGE_STEP_RETRACES, NULL);

	plat_copyRect(
		globals_displaySurface,
		globals_workSurface,
		0,
		0,
		PLAT_WIDTH,
		PLAT_HEIGHT
	);
	exe_writeU16(STATE_CAR_SCROLL_X, (uint16_t)tires.carLeft);
	garage__freeFrames(&withTires, 1);
	garage__freeFrames(&tires.car, 1);
	garage__freeFrames(&tires.shadow, 1);
	globals_drawThrough = drawThrough;
	cursor_set(CURSOR_SHOW_NOW);
}

/** ****************************************************************************
 *
 * \brief	Put a spare set of tires on the car in use.
 *
 * \details	Port of ChangeTires (1000:7800). The list of spare tires
 * 			(screen 4) picks a set; it takes the place of the car's
 * 			tires, which go to the spare parts (the car's price follows),
 * 			the change is shown (garage__changeTires) and the clock
 * 			advances 0x444 ticks. The port then draws the car again
 * 			(CARS_DRAW_UPDATE_GARAGE), which looks the same, so that its
 * 			garage car images have the new tires.
 *
 ******************************************************************************/
void garage_tires(void) {
	uint16_t carAt = exe_readU16(STATE_CAR);

	if (carAt == 0) {
		game_message(GARAGE_TEXT_NO_CAR);
		return;
	}

	uint16_t parts[STATE_PART_MAX + 1];
	int8_t	 percents[STATE_PART_MAX + 1];

	garage_listSpares(GARAGE_KIND_TIRES, parts, percents, 0);

	int16_t choice = ui_runList(
		GARAGE_PARTS_SCREEN,
		1,
		NULL,
		GARAGE_PARTS_LIST,
		percents,
		0
	);

	if (choice == GARAGE_PARTS_DONE) {
		return;
	}

	if (choice == GARAGE_PARTS_SELL) {
		choice = ui_listSelection();
	} else if (choice == UI_LIST_EMPTY) {
		game_message(GARAGE_TEXT_NO_TIRES);
		return;
	}

	uint16_t slotAt = (uint16_t)(carAt + STATE_CAR_TIRES);
	uint16_t oldTable = garage__tireTable(exe_readU16(slotAt));
	uint16_t newTable = garage__tireTable(parts[choice]);

	cars_fitPart(cars_takeSparePart(parts[choice]), slotAt);
	garage__changeTires(oldTable, newTable);

	int16_t	 model = exe_readS16((uint16_t)(carAt + STATE_CAR_MODEL));
	uint16_t mods = exe_readU16((uint16_t)(carAt + STATE_CAR_UNKNOWN_24));

	cursor_set(CURSOR_HIDE_NOW);
	cars_draw(
		CARS_DRAW_UPDATE_GARAGE,
		model,
		cars_drawFlags(carAt),
		(int16_t)((mods >> 8) & 0x1f),
		0,
		GARAGE_CAR_BOTTOM,
		0,
		newTable
	);
	cursor_set(CURSOR_SHOW_NOW);
	globals_gameTime += GARAGE_TIRES_TIME;
}

/** ****************************************************************************
 *
 * \brief	The tire sprite table of a set of tires.
 *
 * \param	part			DS offset of the part record.
 *
 * \return	DS offset of the 4 tire sprite IDs (DS:0x5030 + subtype * 8).
 *
 ******************************************************************************/
static uint16_t garage__tireTable(
	uint16_t part
) {
	int16_t	 index = exe_readS16((uint16_t)(part + STATE_PART_VALUE));
	uint16_t entry = (uint16_t)(STATE_PARTS + index * 8);
	int16_t	 subtype =
		(int8_t)*exe_data((uint16_t)(entry + STATE_PARTS_SUBTYPE));

	return (uint16_t)(STATE_TIRE_SPRITES + subtype * 8);
}
