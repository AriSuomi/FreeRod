/** ****************************************************************************
 *
 * \file	station.c
 *
 * \brief	Gus' Gas: filling the tank of the car in use.
 *
 * \details	Port of FUN_1000_b8a1 and its helpers (1000:b3ca to 1000:b8a1).
 * 			The car drives in beside the pump; the player takes the
 * 			nozzle, puts it into the tank and pays a dollar for every 30
 * 			units of gas. A player who waits too long gets full service
 * 			at twice the price. The pump column at (64, 100) and the
 * 			filler cap box are composed again from one saved picture of
 * 			the scene with the car (the original kept two pump columns
 * 			made by DrawCar and two saved cap boxes); the result on the
 * 			screen is the same.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/station.h"

#include <stdbool.h>
#include <stdint.h>

#include "game/anim.h"
#include "game/cars.h"
#include "game/cursor.h"
#include "game/effects.h"
#include "game/event.h"
#include "game/exe.h"
#include "game/game.h"
#include "game/globals.h"
#include "game/lib.h"
#include "game/music.h"
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

#define STATION_PICTURE	   0x442  /**< Resource: the gas station. */
#define STATION_SCREEN	   0x17	  /**< Screen with the 6 hotspots. */
#define STATION_STATUS	   0x53c2 /**< DS: status line text (empty). */
#define STATION_CAR_BOTTOM 0xa9	  /**< Row of the bottom of the car. */
#define STATION_STRIP_TOP  100	  /**< First row the car moves in. */
#define STATION_WAIT	   3000	  /**< Ticks event_wait waits. */
#define STATION_TICKS	   0x14	  /**< Ticks between idle checks. */
#define STATION_FILL_TICKS 0x4c	  /**< Ticks per fill of 30 units. */
#define STATION_PATIENCE   0x28	  /**< Idle checks before service. */

/*
 * Gas. Car +0x22 holds the gas; a fill adds 30 units, and the tank holds
 * 190 units while filling but no more than 170 when the car leaves.
 */
#define STATION_FILL	0x1e /**< Units of gas per fill. */
#define STATION_TANK	0xbe /**< Units in a full tank. */
#define STATION_MAX_GAS 0xaa /**< Units kept when leaving. */
#define STATION_PRICE	1	 /**< Dollars per fill. */
#define STATION_FINE	10	 /**< Dollars for a nozzle left out. */

/*
 * Pictures.
 */
#define STATION_PUMP_HUNG	0x4c8 /**< Pump column with the nozzle. */
#define STATION_PUMP_EMPTY	0x449 /**< Pump column without nozzle. */
#define STATION_HUNG_MASK	0x4ca /**< Mask of STATION_PUMP_HUNG. */
#define STATION_EMPTY_MASK	0x44a /**< Mask of STATION_PUMP_EMPTY. */
#define STATION_CAP			0x472 /**< Closed filler cap. */
#define STATION_NOZZLE		0x471 /**< Nozzle in the filler. */
#define STATION_SQUEEGEE	0x473 /**< Bucket without the squeegee. */
#define STATION_ATTENDANT	0x4d1 /**< The attendant's poses. */
#define STATION_PUMP_LEFT	0x40  /**< Left of the pump column. */
#define STATION_PUMP_TOP	100	  /**< Top of the pump column. */
#define STATION_PUMP_WIDTH	0x10  /**< Width of the pump column. */
#define STATION_PUMP_ROWS	0x46  /**< Height of the pump column. */
#define STATION_PUMP_PIXELS (STATION_PUMP_WIDTH * STATION_PUMP_ROWS)
#define STATION_CAP_LEFT	0x10 /**< Cap box: left of filler X. */
#define STATION_CAP_UP		5	 /**< Cap box: rows above filler Y. */
#define STATION_CAP_WIDTH	0x18 /**< Width of the cap box. */
#define STATION_CAP_ROWS	0xc	 /**< Height of the cap box. */
#define STATION_BUCKET_X	0x50 /**< Left of the bucket. */
#define STATION_BUCKET_Y	0xaa /**< Top of the bucket. */
#define STATION_BUCKET_W	0x28 /**< Width of the bucket area. */
#define STATION_BUCKET_H	0xc	 /**< Height of the bucket area. */
#define STATION_POSE_LEFT	0x60 /**< Standing pose in 0x4d1. */
#define STATION_POSE_WIDTH	0x30 /**< Width of the standing pose. */
#define STATION_POSE_ROWS	0x40 /**< Height of the standing pose. */
#define STATION_POSE_X		200	 /**< Where the attendant stands. */
#define STATION_POSE_Y		0x51 /**< Top row of the standing pose. */

/*
 * Hotspots: 6 element records from index 0xe3, numbered by the table at
 * DS:0x53b6. Type 11 makes a record a hotspot, type 14 switches it off.
 */
#define STATION_HOTSPOT_IDS	  0x53b6 /**< DS: the 6 hotspot numbers. */
#define STATION_HOTSPOTS	  6		 /**< Hotspots of the station. */
#define STATION_FIRST_HOTSPOT 0xe3	 /**< Index of the first record. */
#define STATION_ELEMENTS	  0x9f4	 /**< DS: element records. */
#define STATION_ELEMENT_SIZE  18	 /**< Bytes of an element record. */
#define STATION_ELEMENT_ID	  6		 /**< Element field: number. */
#define STATION_ELEMENT_LEFT  8		 /**< Element field: left edge. */
#define STATION_TYPE_HOT	  11	 /**< Element type: hotspot. */
#define STATION_TYPE_SKIPPED  14	 /**< Element type passed over. */
#define STATION_SPOT_BUCKET	  1		 /**< Hotspot: bucket, squeegee. */
#define STATION_SPOT_PUMP	  2		 /**< Hotspot: the pump. */
#define STATION_SPOT_GUS	  3		 /**< Hotspot: the attendant. */
#define STATION_SPOT_FILLER	  4		 /**< Hotspot: the filler cap. */
#define STATION_SPOT_EXHAUST2 5		 /**< Hotspot: second exhaust. */
#define STATION_SPOT_EXHAUST1 6		 /**< Hotspot: first exhaust. */

/*
 * What the hand (the cursor shape) holds.
 */
#define STATION_HAND_EMPTY	  1 /**< Nothing. */
#define STATION_HAND_NOZZLE	  2 /**< The nozzle. */
#define STATION_HAND_SQUEEGEE 3 /**< The squeegee. */

/*
 * Messages (offsets from DS:0x2c02).
 */
#define STATION_TEXT_GO_AWAY	  0x10f6 /**< "Get out of my face". */
#define STATION_TEXT_ATTENTION	  0x10b5 /**< "Pay attention". */
#define STATION_TEXT_NOT_FULL	  0x18dd /**< "It's not full". */
#define STATION_TEXT_FULL_SERVICE 0x19e1 /**< "You need full service". */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/**
 * State of a visit (the locals of FUN_1000_b8a1 that FUN_1000_b717 gets a
 * pointer to).
 */
typedef struct station__visit_tag {
	int16_t	 model;		   /**< Model of the car in use. */
	uint8_t	 flags;		   /**< CARS_FLAG_* of the car in use. */
	int16_t	 sticker;	   /**< Door sticker of the car. */
	uint16_t tireTable;	   /**< DS offset of the tire sprite IDs. */
	int16_t	 hand;		   /**< STATION_HAND_*: what the cursor holds. */
	int16_t	 fills;		   /**< Fills of 30 units the tank has room for. */
	bool	 selfService;  /**< false once the attendant took over. */
	bool	 nozzleInTank; /**< The nozzle is in the filler. */
	bool	 nozzleOnPump; /**< The nozzle hangs on the pump. */
	bool	 fineDue;	   /**< A nozzle left out costs STATION_FINE. */
	bool	 pumping;	   /**< Gas is flowing. */
	bool	 pumped;	   /**< Some gas was bought. */
	int16_t	 patience;	   /**< Idle checks left before full service. */
	int32_t	 price;		   /**< Dollars per fill. */
	int		 bucket;	   /**< Saved bucket area (screen_saveRect). */
	int		 scene;		   /**< Saved pump and cap area without them. */
	int16_t	 sceneLeft;	   /**< Left of the saved scene area. */
	int16_t	 sceneTop;	   /**< Top of the saved scene area. */
	int16_t	 sceneRight;   /**< Right of the saved scene area. */
	int16_t	 sceneBottom;  /**< Bottom of the saved scene area. */
} station__Visit;

/*******************************************************************************
;
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

static uint16_t station__hotspot(
	int16_t number
);

static void station__hotspotsOff(void);

static void station__hotspotOn(
	int16_t number,
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height
);

static void station__hotspotsStart(void);

static void station__hotspotsPumping(void);

static void station__hotspotsNozzle(void);

static void station__hotspotsSqueegee(void);

static void station__saveScene(
	station__Visit * pVisit
);

static plat_ImageHandle station__pumpImage(
	int16_t spriteId,
	int16_t maskId
);

static void station__drawScene(
	const station__Visit * pVisit
);

static int32_t station__money(void);

static void station__setTimeout(
	int16_t ticks
);

static void station__setHotspots(
	void (*pSetFn)(void)
);

static void station__attendantStands(
	const station__Visit * pVisit
);

static void station__leave(
	station__Visit * pVisit
);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

static bool station__fillerOn; /**< DS:0x6ca4: filler hotspot is on. */
static bool station__bucketOn; /**< DS:0x6ca6: bucket hotspot is on. */
static bool station__pumpOn;   /**< DS:0x6ca8: pump hotspot is on. */

/** Pump columns with their masks: without the nozzle, with it. */
static plat_ImageHandle station__pumps[2];

/** Pixels of a pump column while it is made. */
static uint8_t station__pumpPixels[STATION_PUMP_PIXELS];

/** Pixels of a pump column mask while it is made. */
static uint8_t station__maskPixels[STATION_PUMP_PIXELS];

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Drive into the gas station, fill the tank and drive away.
 *
 * \details	Port of FUN_1000_b8a1, which the main loop calls (with
 * 			globals_place 3) after the drive to the station. The car
 * 			drives in (DrawCar mode 4), then events of screen 0x17 are
 * 			handled: the pump (take or hang up the nozzle), the filler
 * 			(put the nozzle in or take it out), the bucket (pick up or
 * 			put back the squeegee, which does nothing else), the
 * 			attendant (a message) and the timeout, which pumps a fill
 * 			every 0x4c ticks or counts the idle checks. After 40 idle
 * 			checks the attendant comes: the price doubles and he fills
 * 			the tank. Hanging up the nozzle after buying gas (or without
 * 			money) leaves (FUN_1000_b717). Animation 2 (the pump's meter)
 * 			runs while gas flows, animation 3 (the attendant getting up)
 * 			plays before full service. The pump's sound (effects_pump)
 * 			starts and stops with animation 2.
 *
 ******************************************************************************/
void station_visit(void) {
	uint16_t		 car = exe_readU16(STATE_CAR);
	station__Visit	 visit = { 0 };
	station__Visit * pVisit = &visit;

	ui_status(-1, exe_text(STATION_STATUS), 0);
	globals_place = STATION_PLACE;
	screen_drawResource(STATION_PICTURE, -1);
	ui_statusTitle(exe_text(exe_readU16(STATE_STATUS_TITLE)));
	globals_drawThrough = 1;
	video_showPalette();
	pVisit->hand = STATION_HAND_EMPTY;
	cursor_set(STATION_HAND_EMPTY);

	uint16_t mods = exe_readU16((uint16_t)(car + STATE_CAR_UNKNOWN_24));
	uint16_t tireRecord = exe_readU16((uint16_t)(car + STATE_CAR_TIRES));
	int16_t	 tirePart = exe_readS16((uint16_t)(tireRecord + STATE_PART_VALUE));
	uint16_t tireEntry = (uint16_t)(STATE_PARTS + tirePart * 8);
	int16_t	 tires =
		(int8_t)*exe_data((uint16_t)(tireEntry + STATE_PARTS_SUBTYPE));

	pVisit->model = exe_readS16((uint16_t)(car + STATE_CAR_MODEL));
	pVisit->flags = cars_drawFlags(car);
	pVisit->sticker = (int16_t)((mods >> 8) & 0x1f);
	pVisit->tireTable = (uint16_t)(STATE_TIRE_SPRITES + tires * 8);

	/*
	 * DrawCar hides the cursor while the car moves and shows it again at
	 * its end; the port's cars_draw leaves that to its callers.
	 */
	cursor_set(CURSOR_HIDE_NOW);
	cars_draw(
		CARS_DRAW_DRIVE_IN,
		pVisit->model,
		pVisit->flags,
		pVisit->sticker,
		0,
		STATION_CAR_BOTTOM,
		1,
		pVisit->tireTable
	);
	cursor_set(CURSOR_SHOW_NOW);
	music_pause(0);

	pVisit->bucket = screen_saveRect(
		STATION_BUCKET_X,
		STATION_BUCKET_Y,
		STATION_BUCKET_W,
		STATION_BUCKET_H
	);
	pVisit->nozzleOnPump = true;
	pVisit->nozzleInTank = false;
	station__saveScene(pVisit);
	station__drawScene(pVisit);

	pVisit->price = STATION_PRICE;
	pVisit->pumping = false;
	pVisit->patience = STATION_PATIENCE;
	pVisit->pumped = false;
	pVisit->selfService = true;
	pVisit->fineDue = true;
	pVisit->fills = (int16_t)((STATION_TANK -
							   exe_readS16((uint16_t)(car + STATE_CAR_FUEL))) /
							  STATION_FILL);

	station__hotspotsStart();
	ui_open(STATION_SCREEN);
	cursor_set(CURSOR_SHOW_NOW);
	station__setTimeout(STATION_TICKS);

	for (;;) {
		int16_t event;

		do {
			event = event_wait(STATION_WAIT);
		} while (event == 0);

		bool canPay = station__money() >= pVisit->price;

		switch (event) {
			case STATION_SPOT_GUS:
				game_message(STATION_TEXT_GO_AWAY);
				break;

			case STATION_SPOT_BUCKET:
				if (!station__bucketOn) {
					break;
				}

				if (pVisit->hand == STATION_HAND_EMPTY) {
					pVisit->hand = STATION_HAND_SQUEEGEE;
					cursor_set(STATION_HAND_SQUEEGEE);
					screen_drawResourceAt(
						STATION_SQUEEGEE,
						STATION_BUCKET_X,
						STATION_BUCKET_Y,
						-1
					);
					station__setHotspots(station__hotspotsSqueegee);
				} else {
					pVisit->hand = STATION_HAND_EMPTY;
					cursor_set(STATION_HAND_EMPTY);
					screen_putBackRect(pVisit->bucket);
					screen_copyToDisplay(
						STATION_BUCKET_X,
						STATION_BUCKET_Y,
						STATION_BUCKET_X + STATION_BUCKET_W - 1,
						STATION_BUCKET_Y + STATION_BUCKET_H - 1
					);
					station__setHotspots(station__hotspotsStart);
				}

				station__setTimeout(STATION_TICKS);
				break;

			case STATION_SPOT_PUMP:
				if (!station__pumpOn) {
					break;
				}

				if (pVisit->hand == STATION_HAND_EMPTY) {
					pVisit->hand = STATION_HAND_NOZZLE;
					cursor_set(STATION_HAND_NOZZLE);
					pVisit->nozzleOnPump = false;
					station__drawScene(pVisit);
					station__setHotspots(station__hotspotsNozzle);
				} else {
					if (!canPay || pVisit->pumped) {
						pVisit->fineDue = false;
						station__leave(pVisit);
						return;
					}

					pVisit->hand = STATION_HAND_EMPTY;
					cursor_set(STATION_HAND_EMPTY);
					pVisit->nozzleOnPump = true;
					station__drawScene(pVisit);
					station__setHotspots(station__hotspotsStart);
				}

				station__setTimeout(STATION_TICKS);
				pVisit->pumping = false;
				break;

			case STATION_SPOT_FILLER:
				if (!station__fillerOn) {
					break;
				}

				if (pVisit->hand == STATION_HAND_EMPTY) {
					if (anim_isRunning(ANIM_GAS_METER)) {
						effects_pump(0);
						anim_stop(ANIM_GAS_METER);
					}

					pVisit->nozzleInTank = false;
					station__drawScene(pVisit);
					pVisit->hand = STATION_HAND_NOZZLE;
					cursor_set(STATION_HAND_NOZZLE);

					if (pVisit->fills > 0 && canPay) {
						game_message(STATION_TEXT_NOT_FULL);
					}

					station__setHotspots(station__hotspotsNozzle);
					station__setTimeout(STATION_TICKS);
					pVisit->pumping = false;
					break;
				}

				if (pVisit->fills == 0 || !canPay) {
					game_message(STATION_TEXT_ATTENTION);
					break;
				}

				pVisit->nozzleInTank = true;
				station__drawScene(pVisit);
				pVisit->hand = STATION_HAND_EMPTY;
				cursor_set(STATION_HAND_EMPTY);
				anim_start(ANIM_GAS_METER);
				effects_pump(1);
				station__setHotspots(station__hotspotsPumping);
				station__setTimeout(STATION_FILL_TICKS);
				pVisit->pumping = true;
				break;

			case EVENT_TIMEOUT:
				if (pVisit->hand == STATION_HAND_SQUEEGEE) {
					station__setTimeout(STATION_TICKS);
					break;
				}

				if (!pVisit->pumping) {
					pVisit->patience--;

					if (pVisit->patience > 0) {
						station__setTimeout(STATION_TICKS);
						break;
					}

					station__setHotspots(station__hotspotsOff);
					pVisit->price <<= 1;
					anim_play(ANIM_ATTENDANT);
					station__attendantStands(pVisit);
					game_message(STATION_TEXT_FULL_SERVICE);
					pVisit->selfService = false;

					if (pVisit->pumped) {
						station__leave(pVisit);
						return;
					}

					pVisit->fineDue = false;

					if (pVisit->hand == STATION_HAND_EMPTY) {
						pVisit->nozzleOnPump = false;
						station__drawScene(pVisit);
					}

					pVisit->nozzleInTank = true;
					station__drawScene(pVisit);
					pVisit->hand = STATION_HAND_EMPTY;
					cursor_set(STATION_HAND_EMPTY);
					anim_start(ANIM_GAS_METER);
					effects_pump(1);
					pVisit->pumping = true;
					station__setTimeout(STATION_FILL_TICKS);
					break;
				}

				pVisit->fills--;

				uint16_t gasAt = (uint16_t)(car + STATE_CAR_FUEL);
				int32_t	 money = station__money();

				exe_writeU16(
					gasAt,
					(uint16_t)(exe_readS16(gasAt) + STATION_FILL)
				);
				pVisit->pumped = true;
				ui_addMoney(
					(int16_t)-(money < pVisit->price ? money : pVisit->price)
				);

				if (pVisit->fills == 0 || station__money() < pVisit->price) {
					if (anim_isRunning(ANIM_GAS_METER)) {
						effects_pump(0);
						anim_stop(ANIM_GAS_METER);
					}

					if (!pVisit->selfService) {
						station__leave(pVisit);
						return;
					}

					station__setTimeout(STATION_TICKS);
					pVisit->pumping = false;
					break;
				}

				station__setTimeout(STATION_FILL_TICKS);
				break;

			default:
				break;
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Find a hotspot of the station by its number.
 *
 * \param	number			The number in the record's +6 field.
 *
 * \return	DS offset of the element record, or 0 when there is none.
 *
 * \details	Port of FUN_1000_b3ca, which returns the record's index.
 *
 ******************************************************************************/
static uint16_t station__hotspot(
	int16_t number
) {
	for (int index = 0; index < STATION_HOTSPOTS; index++) {
		int element = STATION_ELEMENTS +
			(STATION_FIRST_HOTSPOT + index) * STATION_ELEMENT_SIZE;

		if (exe_readS16((uint16_t)(element + STATION_ELEMENT_ID)) == number) {
			return (uint16_t)element;
		}
	}

	return 0;
}

/** ****************************************************************************
 *
 * \brief	Switch off all hotspots of the station.
 *
 * \details	Port of FUN_1000_b456 (also FUN_1000_b617, which only calls
 * 			it).
 *
 ******************************************************************************/
static void station__hotspotsOff(void) {
	station__bucketOn = false;
	station__pumpOn = false;
	station__fillerOn = false;

	for (int index = 0; index < STATION_HOTSPOTS; index++) {
		uint16_t element = station__hotspot(
			exe_readS16((uint16_t)(STATION_HOTSPOT_IDS + index * 2))
		);

		if (element != 0) {
			exe_writeU16(element, STATION_TYPE_SKIPPED);
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Place a hotspot of the station and switch it on.
 *
 * \param	number			Number of the hotspot.
 * \param	left			Left edge.
 * \param	top				Top edge.
 * \param	width			Width.
 * \param	height			Height.
 *
 * \details	Port of FUN_1000_b49c.
 *
 ******************************************************************************/
static void station__hotspotOn(
	int16_t number,
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height
) {
	uint16_t element = station__hotspot(number);

	if (element == 0) {
		return;
	}

	uint16_t box = (uint16_t)(element + STATION_ELEMENT_LEFT);

	exe_writeU16((uint16_t)(box + 0), (uint16_t)left);
	exe_writeU16((uint16_t)(box + 2), (uint16_t)top);
	exe_writeU16((uint16_t)(box + 4), (uint16_t)(left + width - 1));
	exe_writeU16((uint16_t)(box + 6), (uint16_t)(top + height - 1));
	exe_writeU16(element, STATION_TYPE_HOT);
}

/** ****************************************************************************
 *
 * \brief	Hotspots with empty hands: the pump and the bucket.
 *
 * \details	Port of FUN_1000_b4de. ui_sortElements stands in for
 * 			FUN_1000_b425.
 *
 ******************************************************************************/
static void station__hotspotsStart(void) {
	station__hotspotsOff();
	station__hotspotOn(STATION_SPOT_PUMP, 0x3f, 100, 0xc, 0x17);
	station__hotspotOn(STATION_SPOT_BUCKET, 0x54, 0xa8, 0x1f, 0xf);
	ui_sortElements(STATION_FIRST_HOTSPOT, STATION_HOTSPOTS);
	station__bucketOn = true;
	station__pumpOn = true;
}

/** ****************************************************************************
 *
 * \brief	Hotspot while gas flows: the filler.
 *
 * \details	Port of FUN_1000_b525.
 *
 ******************************************************************************/
static void station__hotspotsPumping(void) {
	station__hotspotsOff();
	station__hotspotOn(
		STATION_SPOT_FILLER,
		(int16_t)(exe_readS16(STATE_CAR_WINDOW_X) - 10),
		(int16_t)(exe_readS16(STATE_CAR_WINDOW_Y) - 5),
		0x12,
		0xc
	);
	ui_sortElements(STATION_FIRST_HOTSPOT, STATION_HOTSPOTS);
	station__fillerOn = true;
}

/** ****************************************************************************
 *
 * \brief	Hotspots while holding the nozzle: the pump and the filler.
 *
 * \details	Port of FUN_1000_b552.
 *
 ******************************************************************************/
static void station__hotspotsNozzle(void) {
	station__hotspotsOff();
	station__hotspotOn(STATION_SPOT_PUMP, 0x34, 100, 0x10, 0x17);
	station__hotspotOn(
		STATION_SPOT_FILLER,
		(int16_t)(exe_readS16(STATE_CAR_WINDOW_X) - 1),
		(int16_t)(exe_readS16(STATE_CAR_WINDOW_Y) - 1),
		10,
		9
	);
	ui_sortElements(STATION_FIRST_HOTSPOT, STATION_HOTSPOTS);
	station__pumpOn = true;
	station__fillerOn = true;
}

/** ****************************************************************************
 *
 * \brief	Hotspots while holding the squeegee: the exhausts, the bucket
 * 			and the attendant.
 *
 * \details	Port of FUN_1000_b59b. Only the bucket does something; the
 * 			exhausts are hotspots without an action.
 *
 ******************************************************************************/
static void station__hotspotsSqueegee(void) {
	station__hotspotsOff();

	if (exe_readS16(STATE_CAR_HAS_EXHAUST) != 0) {
		station__hotspotOn(
			STATION_SPOT_EXHAUST1,
			exe_readS16(STATE_CAR_EXHAUST1_X),
			exe_readS16(STATE_CAR_EXHAUST1_Y),
			0x14,
			0x13
		);
	}

	station__hotspotOn(
		STATION_SPOT_EXHAUST2,
		exe_readS16(STATE_CAR_EXHAUST2_X),
		exe_readS16(STATE_CAR_EXHAUST2_Y),
		0x16,
		0x13
	);
	station__hotspotOn(STATION_SPOT_BUCKET, 0x3e, 0xa5, 0x35, 0x12);
	station__hotspotOn(STATION_SPOT_GUS, 0xc4, 0x5a, 0x10, 0xe);
	ui_sortElements(STATION_FIRST_HOTSPOT, STATION_HOTSPOTS);
	station__bucketOn = true;
}

/** ****************************************************************************
 *
 * \brief	Save the scene around the pump column and the filler cap.
 *
 * \param	pVisit			The visit; gets the saved area.
 *
 * \details	The port's stand-in for the two pump columns DrawCar builds
 * 			at the station (FUN_1f38_872e into DS:0x28c and DS:0x290)
 * 			and the two cap boxes FUN_1000_b8a1 saves: one area covering
 * 			both, saved with the whole car (also behind the pump column,
 * 			which the foreground hides while the car moves) but without
 * 			foreground, pump and cap sprites. As in FUN_1f38_872e, the
 * 			pump column sprite lies opaque under the car, so its floor
 * 			shows where the car is see-through. The pump columns get their
 * 			masks (0x4ca, 0x44a) as in FUN_1f38_872e.
 *
 ******************************************************************************/
static void station__saveScene(
	station__Visit * pVisit
) {
	station__pumps[0] =
		station__pumpImage(STATION_PUMP_EMPTY, STATION_EMPTY_MASK);
	station__pumps[1] =
		station__pumpImage(STATION_PUMP_HUNG, STATION_HUNG_MASK);
	cursor_set(CURSOR_HIDE);
	screen_drawSpritePart(
		globals_workSurface,
		STATION_PUMP_EMPTY,
		0,
		0,
		STATION_PUMP_WIDTH,
		STATION_PUMP_ROWS,
		STATION_PUMP_LEFT,
		STATION_PUMP_TOP,
		true
	);
	cars_drawOnWork();

	int16_t capLeft =
		(int16_t)(exe_readS16(STATE_CAR_WINDOW_X) - STATION_CAP_LEFT);
	int16_t capTop =
		(int16_t)(exe_readS16(STATE_CAR_WINDOW_Y) - STATION_CAP_UP);
	int16_t left = STATION_PUMP_LEFT;
	int16_t top = STATION_PUMP_TOP;
	int16_t right = STATION_PUMP_LEFT + STATION_PUMP_WIDTH - 1;
	int16_t bottom = STATION_PUMP_TOP + STATION_PUMP_ROWS - 1;

	if (capLeft < left) {
		left = capLeft;
	}

	if (capTop < top) {
		top = capTop;
	}

	if (capLeft + STATION_CAP_WIDTH - 1 > right) {
		right = (int16_t)(capLeft + STATION_CAP_WIDTH - 1);
	}

	if (capTop + STATION_CAP_ROWS - 1 > bottom) {
		bottom = (int16_t)(capTop + STATION_CAP_ROWS - 1);
	}

	left = (int16_t)(left & ~7);
	right = (int16_t)(((right + 8) & ~7) - 1);
	pVisit->sceneLeft = left;
	pVisit->sceneTop = top;
	pVisit->sceneRight = right;
	pVisit->sceneBottom = bottom;
	pVisit->scene = screen_saveRect(
		left,
		top,
		(int16_t)(right - left + 1),
		(int16_t)(bottom - top + 1)
	);
	cars_drawForeground(0, PLAT_WIDTH - 1);
	cursor_set(CURSOR_SHOW);
}

/** ****************************************************************************
 *
 * \brief	Make a pump column image that is see-through outside its mask.
 *
 * \param	spriteId		The pump column resource.
 * \param	maskId			Its mask resource (0 where the pump is).
 *
 * \return	The image, or 0 when it could not be made.
 *
 * \details	As FUN_1f38_872e loads the two resources into one sprite
 * 			with a mask. The image has the sprite's name, so a
 * 			replacement of the sprite is used.
 *
 ******************************************************************************/
static plat_ImageHandle station__pumpImage(
	int16_t spriteId,
	int16_t maskId
) {
	int					 spriteIndex = lib_indexFromGameId(spriteId);
	int					 maskIndex = lib_indexFromGameId(maskId);
	const lib_Resource * pSprite = lib_get(spriteIndex);
	const lib_Resource * pMask = lib_get(maskIndex);

	if (pSprite == NULL || pMask == NULL ||
		pSprite->width != pMask->width || pSprite->height != pMask->height ||
		pSprite->width * pSprite->height > STATION_PUMP_PIXELS ||
		!lib_decodePixels(spriteIndex, station__pumpPixels) ||
		!lib_decodePixels(maskIndex, station__maskPixels)) {
		return 0;
	}

	int pixelCount = pSprite->width * pSprite->height;

	for (int index = 0; index < pixelCount; index++) {
		if (station__maskPixels[index] != 0) {
			station__pumpPixels[index] = PLAT_TRANSPARENT;
		}
	}

	return plat_imageCreateOverlay(
		pSprite->name,
		pSprite->width,
		pSprite->height,
		station__pumpPixels
	);
}

/** ****************************************************************************
 *
 * \brief	Draw the pump column and the filler cap as the visit has them.
 *
 * \param	pVisit			The visit.
 *
 * \details	Stands in for RestoreRectToDisplay of the pump columns at
 * 			DS:0x28c (with the nozzle) and DS:0x290 (without), and for
 * 			FUN_1000_b61b, which puts the cap box back and draws the cap
 * 			(0x472) or the nozzle (0x471) in it, its right edge 8 columns
 * 			right of the filler and its bottom 7 rows below it, at the
 * 			exact column (FUN_1f38_af50 shifts it first). The cap box
 * 			clips the sprite. The foreground goes back in front of
 * 			the car outside the pump column; the pump column has its
 * 			mask, so the car shows behind the hose.
 *
 ******************************************************************************/
static void station__drawScene(
	const station__Visit * pVisit
) {
	int16_t fillerX = exe_readS16(STATE_CAR_WINDOW_X);
	int16_t fillerY = exe_readS16(STATE_CAR_WINDOW_Y);
	int16_t capId =
		pVisit->nozzleInTank ? STATION_NOZZLE : STATION_CAP;
	const lib_Resource * pCap = lib_get(lib_indexFromGameId(capId));

	plat_ImageHandle pump = station__pumps[pVisit->nozzleOnPump ? 1 : 0];

	cursor_set(CURSOR_HIDE);
	screen_putBackRect(pVisit->scene);
	cars_drawForeground(pVisit->sceneLeft, STATION_PUMP_LEFT - 1);
	cars_drawForeground(
		STATION_PUMP_LEFT + STATION_PUMP_WIDTH,
		pVisit->sceneRight
	);

	if (pump != 0) {
		plat_selectPage(globals_workSurface);
		plat_drawImage(pump, STATION_PUMP_LEFT, STATION_PUMP_TOP);
	}

	if (pCap != NULL) {
		int16_t width = (int16_t)pCap->width;
		int16_t height = (int16_t)pCap->height;
		int16_t sourceLeft = 0;
		int16_t sourceTop = 0;

		if (width > STATION_CAP_WIDTH) {
			sourceLeft = (int16_t)(width - STATION_CAP_WIDTH);
			width = STATION_CAP_WIDTH;
		}

		if (height > STATION_CAP_ROWS) {
			sourceTop = (int16_t)(height - STATION_CAP_ROWS);
			height = STATION_CAP_ROWS;
		}

		screen_drawSpritePartExact(
			globals_workSurface,
			capId,
			sourceLeft,
			sourceTop,
			width,
			height,
			(int16_t)(fillerX + 8 - width),
			(int16_t)(fillerY + 7 - height),
			false
		);
	}

	cursor_set(CURSOR_SHOW);
	screen_copyToDisplay(
		pVisit->sceneLeft,
		pVisit->sceneTop,
		pVisit->sceneRight,
		pVisit->sceneBottom
	);
}

/** ****************************************************************************
 *
 * \brief	The player's money.
 *
 * \return	The money (DS:0x7eaa, 32 bits, signed).
 *
 ******************************************************************************/
static int32_t station__money(void) {
	return (int32_t)exe_readU32(STATE_MONEY);
}

/** ****************************************************************************
 *
 * \brief	Set the deadline of the next EVENT_TIMEOUT.
 *
 * \param	ticks			Ticks from now.
 *
 ******************************************************************************/
static void station__setTimeout(
	int16_t ticks
) {
	globals_timeout = globals_ticks + (uint32_t)ticks;
}

/** ****************************************************************************
 *
 * \brief	Change the hotspots of the open screen.
 *
 * \param	pSetFn			Function that sets the new hotspots.
 *
 * \details	The original's CloseScreen(1), the hotspot function and
 * 			OpenScreen(0x17).
 *
 ******************************************************************************/
static void station__setHotspots(
	void (*pSetFn)(void)
) {
	ui_close(1);
	pSetFn();
	ui_open(STATION_SCREEN);
}

/** ****************************************************************************
 *
 * \brief	Show the attendant standing behind the car.
 *
 * \param	pVisit			The visit.
 *
 * \details	Port only, after animation 3 (the attendant getting up): its
 * 			last picture (the standing pose, columns 0x60.. of 0x4d1 at
 * 			(200, 81), as in FUN_1000_b717) also goes into the
 * 			background behind the car, so that redrawing the car keeps
 * 			him, and the car, the pump and the filler are drawn again in
 * 			front of it.
 *
 ******************************************************************************/
static void station__attendantStands(
	const station__Visit * pVisit
) {
	cursor_set(CURSOR_HIDE);
	screen_drawSpritePart(
		CARS_BACKGROUND_PAGE,
		STATION_ATTENDANT,
		STATION_POSE_LEFT,
		0,
		STATION_POSE_WIDTH,
		STATION_POSE_ROWS,
		STATION_POSE_X,
		STATION_POSE_Y,
		true
	);
	plat_copyRect(
		CARS_BACKGROUND_PAGE,
		globals_workSurface,
		STATION_POSE_X,
		STATION_POSE_Y,
		STATION_POSE_WIDTH,
		STATION_POSE_ROWS
	);
	cars_drawOnWork();
	cars_drawForeground(0, PLAT_WIDTH - 1);
	cursor_set(CURSOR_SHOW);
	screen_copyToDisplay(
		STATION_POSE_X,
		STATION_POSE_Y,
		STATION_POSE_X + STATION_POSE_WIDTH - 1,
		STATION_POSE_Y + STATION_POSE_ROWS - 1
	);
	station__drawScene(pVisit);
}

/** ****************************************************************************
 *
 * \brief	Put the nozzle back, pay a fine if due and drive away.
 *
 * \param	pVisit			The visit.
 *
 * \details	Port of FUN_1000_b717. A nozzle left in the tank or off the
 * 			pump while fineDue is set costs 10 dollars (or all the money
 * 			when there is less). After full service the attendant's
 * 			standing pose (columns 0x60.. of 0x4d1) goes into the
 * 			background the car moves over, from row 100 down. The gas is
 * 			cut to 170 units, the car drives off to the right, the saved
 * 			areas are dropped (ResetTempMem) and the drive home follows
 * 			(game_drive with globals_place 1).
 *
 ******************************************************************************/
static void station__leave(
	station__Visit * pVisit
) {
	uint16_t car = exe_readU16(STATE_CAR);

	if (pVisit->hand != STATION_HAND_EMPTY) {
		cursor_set(STATION_HAND_EMPTY);
	}

	for (int step = 0; step < 2; step++) {
		bool nozzleOut = false;

		if (step == 0) {
			nozzleOut = pVisit->nozzleInTank;
		} else {
			nozzleOut = !pVisit->nozzleOnPump;
		}

		if (!nozzleOut) {
			continue;
		}

		if (step == 0) {
			pVisit->nozzleInTank = false;
		} else {
			pVisit->nozzleOnPump = true;
		}

		station__drawScene(pVisit);

		if (pVisit->fineDue) {
			int32_t money = station__money();

			ui_addMoney(
				(int16_t)-(money < STATION_FINE ? money : STATION_FINE)
			);

			if (step == 0) {
				pVisit->fineDue = false;
			}
		}
	}

	if (!pVisit->selfService) {
		int16_t skipped = STATION_STRIP_TOP - STATION_POSE_Y;

		screen_drawSpritePart(
			CARS_BACKGROUND_PAGE,
			STATION_ATTENDANT,
			STATION_POSE_LEFT,
			skipped,
			STATION_POSE_WIDTH,
			(int16_t)(STATION_POSE_ROWS - skipped),
			STATION_POSE_X,
			STATION_STRIP_TOP,
			true
		);
	}

	uint16_t gasAt = (uint16_t)(car + STATE_CAR_FUEL);

	if (exe_readS16(gasAt) > STATION_MAX_GAS) {
		exe_writeU16(gasAt, STATION_MAX_GAS);
	}

	cursor_set(CURSOR_HIDE_NOW);
	cars_draw(
		CARS_DRAW_SCROLL_RIGHT,
		pVisit->model,
		pVisit->flags,
		pVisit->sticker,
		0,
		STATION_CAR_BOTTOM,
		1,
		pVisit->tireTable
	);
	cursor_set(CURSOR_SHOW_NOW);
	ui_close(1);
	screen_resetSaved();

	for (int index = 0; index < 2; index++) {
		if (station__pumps[index] != 0) {
			plat_imageDestroy(station__pumps[index]);
			station__pumps[index] = 0;
		}
	}

	globals_place = 1;
	game_drive();
}
