/** ****************************************************************************
 *
 * \file	paper.c
 *
 * \brief	The newspaper: the front page and the classified ads for used
 * 			cars and auto parts.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/paper.h"

#include <stdbool.h>
#include <stdio.h> /* snprintf only; no file access or printing */
#include <string.h>

#include "game/calendar.h"
#include "game/cars.h"
#include "game/cursor.h"
#include "game/event.h"
#include "game/exe.h"
#include "game/game.h"
#include "game/globals.h"
#include "game/random.h"
#include "game/screen.h"
#include "game/state.h"
#include "game/text.h"
#include "game/ui.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define PAPER_PICTURE		 0x3fd	/**< Resource of the front page. */
#define PAPER_HEADLINES		 0x4f4e /**< DS offset: 5 headline resources. */
#define PAPER_HEADLINE		 0x4f58 /**< DS offset: headline shown now. */
#define PAPER_HEADLINE_DAY	 0x4f5a /**< DS offset: day it was chosen. */
#define PAPER_HEADLINE_TOP	 0x88	/**< Row of the headline. */
#define PAPER_DATE_TOP		 0x79	/**< Row of the date. */
#define PAPER_SCREEN		 7		/**< Screen with the paper's hotspots. */
#define PAPER_CURSOR		 1		/**< Cursor shape on the paper. */
#define PAPER_LAST_ROW		 0xbd	/**< Last row above the status line. */

#define PAPER_ADS_SIDES		 0x403 /**< Resource of the two side strips. */
#define PAPER_ADS_HEIGHT	 0xbe  /**< Rows of the ads page. */
#define PAPER_SIDE_WIDTH	 0x40  /**< Width of each side strip. */
#define PAPER_COLUMN_LEFT	 0x40  /**< Left edge of the ads column. */
#define PAPER_COLUMN_WIDTH	 0xc0  /**< Width of the ads column. */
#define PAPER_RIGHT_SIDE	 0x100 /**< Left edge of the right strip. */
#define PAPER_TEXT_LEFT		 0x44  /**< Left edge of the ad texts. */
#define PAPER_TEXT_WIDTH	 0xb8  /**< Width of the lines between ads. */
#define PAPER_ADS_STYLE		 13	   /**< Style of the ads page. */
#define PAPER_ADS_SCREEN	 8	   /**< Screen with the ads hotspots. */
#define PAPER_ADS_WAIT		 3000  /**< Ticks waited per call. */
#define PAPER_ADS_PREVIOUS	 (-1)  /**< Left strip or P: previous page. */
#define PAPER_ADS_NEXT		 (-2)  /**< Right strip or N: next page. */
#define PAPER_ADS_GARAGE	 (-3)  /**< Corner, G or Esc: garage. */
#define PAPER_LINE_ROWS		 9	   /**< Rows per text line. */
#define PAPER_ITEM_PADDING	 3	   /**< Extra rows around an ad. */
#define PAPER_CUTOFF_ROW	 0xc2  /**< Row reached when an ad was cut off. */
#define PAPER_MAX_PAGES		 12	   /**< Entries of paper__pageStarts. */

#define PAPER_LIST_BOX		 0x0f82 /**< Element 79: list box of screen 8. */
#define PAPER_ELEMENT_SIZE	 18		/**< Bytes of an element record. */
#define PAPER_ELEMENT_ID	 6		/**< Element field: hotspot number. */
#define PAPER_ELEMENT_TOP	 10		/**< Element field: top edge. */
#define PAPER_ELEMENT_BOTTOM 14		/**< Element field: bottom edge. */
#define PAPER_TYPE_BUTTON	 10		/**< Element type: button. */
#define PAPER_TYPE_HOT		 11		/**< Element type: hotspot. */
#define PAPER_TYPE_SKIPPED	 14		/**< Element type that is passed over. */

#define PAPER_PLACE_ADS		 5		/**< globals_place in the ads. */
#define PAPER_ADS_LIST		 0x49e0 /**< DS offset: count and text offsets. */
#define PAPER_MESSAGES		 0x2c02 /**< DS offset the ad texts count from. */
#define PAPER_USED_HEADER	 0xcd8	/**< Text offset: "--- USED CARS ---". */
#define PAPER_PARTS_HEADER	 0xcfd	/**< Text offset: "---- PARTS ----". */
#define PAPER_MIN_PART_PRICE 20		/**< Lowest part price shown in the ads. */
#define PAPER_START_USED	 2		/**< First item shown for used cars. */
#define PAPER_START_PARTS	 0x1c	/**< First item shown for auto parts. */
#define PAPER_MAX_ITEMS		 70		/**< Cars and parts in the ads list. */

#define PAPER_SEE_IT_BUTTON	 0x11b0 /**< Element 110: "See it" button. */
#define PAPER_BUY_TITLE		 0x2990 /**< DS offset: item title in screen 9. */
#define PAPER_BUY_PRICE		 0x29d6 /**< DS offset: price in screen 9. */
#define PAPER_PRICE_SPACE	 12		/**< Bytes at PAPER_BUY_PRICE. */
#define PAPER_PRICE_FORMAT	 0x4f80 /**< DS offset: "$%-3d ?". */
#define PAPER_BUY_SCREEN	 9		/**< Buy dialog screen. */
#define PAPER_BUY_YES		 (-20)	/**< Screen 9: "Yeah". */
#define PAPER_BUY_SEE_IT	 (-25)	/**< Screen 9: "See it". */

#define PAPER_SEE_TOP		 0x40  /**< Top of the "See it" area. */
#define PAPER_SEE_HEIGHT	 0x52  /**< Height of the "See it" area. */
#define PAPER_SEE_BOX_TOP	 0x41  /**< Top of the car picture box. */
#define PAPER_SEE_BOX_HEIGHT 0x46  /**< Height of the car picture box. */
#define PAPER_SEE_BOTTOM	 0x86  /**< Bottom row of the car in the box. */
#define PAPER_SEE_COLOR		 0x2cc /**< DS offset: fill color of the box. */
#define PAPER_SEE_BAR_TOP	 0x87  /**< Top of the title bar below the box. */
#define PAPER_SEE_BAR_HEIGHT 10	   /**< Height of the title bar. */
#define PAPER_SEE_TEXT_TOP	 0x88  /**< Row of the title in the bar. */
#define PAPER_SEE_STYLE		 11	   /**< Style of the title bar. */
#define PAPER_ANY_KEY_SCREEN 0x18  /**< Screen that waits for a click/key. */
#define PAPER_PAINTS		 5	   /**< Random paints for a bought car. */

#define PAPER_DEMO_TOPUP	 1999	/**< Money added in the demo when short. */
#define PAPER_SHORT_TEXT	 0x3a5a /**< DS offset: "No go, hot shot...". */
#define PAPER_SHORT_SPACE	 20		/**< Room after '$' in PAPER_SHORT_TEXT. */
#define PAPER_SHORT_FORMAT	 0x4f88 /**< DS offset: "%d short.". */
#define PAPER_TEXT_SHORT	 0x0e58 /**< Message: not enough money. */
#define PAPER_TEXT_BOUGHT	 0x0d22 /**< Message: "It's yours!". */
#define PAPER_CAR_TIME		 0x444	/**< Game time added when buying a car. */
#define PAPER_PART_TIME		 0x222	/**< Game time added when buying a part. */
#define PAPER_MAX_PARTS		 0x82	/**< Parts before the ads close. */

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

static int16_t paper__itemHeight(
	uint16_t list,
	int16_t	 item
);

static void paper__openAds(
	uint16_t list,
	int16_t	 startItem
);

static int16_t paper__drawAdsPage(void);

static int16_t paper__runAds(void);

static void paper__closeAds(void);

static void paper__seeCar(
	int16_t	 model,
	int8_t * pPaint
);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/**
 * DS:0x4fe: 1-based index of the first item on each page of the ads.
 */
static int16_t paper__pageStarts[PAPER_MAX_PAGES];

static int16_t	paper__pageCount;  /**< DS:0x516: pages in the ads list. */
static int16_t	paper__page;	   /**< DS:0x518: current page index. */
static uint16_t paper__adsList;	   /**< DS:0x51a: DS offset of the list. */
static uint8_t	paper__redraw;	   /**< DS:0x520: 1 to redraw the page. */
static int16_t	paper__adsStarted; /**< DS:0x536: 0 before the first draw. */
static int16_t	paper__firstItem;  /**< DS:0x7ebe: first item on the page. */
static int16_t	paper__lastItem;   /**< DS:0x7f84: last item on the page. */

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Show the front page of the newspaper and wait for a choice.
 *
 * \return	PAPER_USED_CARS (also keys C and U), PAPER_AUTO_PARTS (keys
 * 			P and A), 3 (keys G and Esc) or 4 (a click on the rest of
 * 			the page): the last two go to the garage.
 *
 * \details	Port of FUN_1000_3b33. Draws resource 0x3fd, one of 5
 * 			headlines at row 136 and the date at row 121, and copies the
 * 			page to the display. A new headline is drawn at random each
 * 			day (DS:0x4f58, day in DS:0x4f5a); if it comes out the same
 * 			as the last one, it is drawn once more. Then screen 7 runs
 * 			with cursor shape 1.
 *
 ******************************************************************************/
int16_t paper_show(void) {
	int16_t savedThrough = globals_drawThrough;

	globals_drawThrough = 0;
	screen_drawResource(PAPER_PICTURE, -1);

	int16_t day = calendar_day();

	if (day != exe_readS16(PAPER_HEADLINE_DAY)) {
		int16_t headline = random_next(5);

		if (headline == exe_readS16(PAPER_HEADLINE)) {
			headline = random_next(5);
		}

		exe_writeU16(PAPER_HEADLINE, (uint16_t)headline);
		exe_writeU16(PAPER_HEADLINE_DAY, (uint16_t)day);
	}

	int		entry = PAPER_HEADLINES + exe_readS16(PAPER_HEADLINE) * 2;
	int16_t picture = exe_readS16((uint16_t)entry);

	screen_drawResourceAt(picture, 0, PAPER_HEADLINE_TOP, -1);
	cursor_set(CURSOR_HIDE);
	calendar_drawDate(PAPER_DATE_TOP);
	cursor_set(CURSOR_SHOW);
	globals_drawThrough = savedThrough;

	if (savedThrough != 0) {
		screen_copyToDisplay(0, 0, PLAT_WIDTH - 1, PAPER_LAST_ROW);
	}

	cursor_set(PAPER_CURSOR);

	return ui_run(PAPER_SCREEN);
}

/** ****************************************************************************
 *
 * \brief	Show the classified ads and let the player buy a car or
 * 			parts.
 *
 * \param	section			PAPER_USED_CARS or PAPER_AUTO_PARTS: the section
 * 							the column opens on.
 *
 * \details	Port of ShowAds (1000:442b). Builds one list of ads at
 * 			DS:0x49e0: the "USED CARS" heading, the car models chained
 * 			from STATE_FIRST_MODEL, the "PARTS" heading, and the entries
 * 			of STATE_PARTS that cost at least 20 dollars. Clicking an ad
 * 			opens screen 9 with the item's first line and price ("See
 * 			it" is shown only for cars). Buying a car creates its record,
 * 			shows "It's yours!" and leaves the newspaper; buying a part
 * 			adds it to the spare parts and stays on the page until more
 * 			than 130 parts are held.
 *
 ******************************************************************************/
void paper_showAds(
	int16_t section
) {
	int8_t	items[PAPER_MAX_ITEMS];
	int16_t listIndex = 2;
	int		carCount = 0;
	int16_t model = exe_readS16(STATE_FIRST_MODEL);

	globals_place = PAPER_PLACE_ADS;
	exe_writeU16(PAPER_ADS_LIST + 2, PAPER_USED_HEADER);

	while (model >= 0) {
		uint16_t modelAt = (uint16_t)(STATE_MODELS + model * STATE_MODEL_SIZE);
		uint16_t textOffset =
			exe_readU16((uint16_t)(modelAt + STATE_MODEL_TEXT));

		exe_writeU16((uint16_t)(PAPER_ADS_LIST + listIndex * 2), textOffset);
		listIndex++;
		items[carCount++] = (int8_t)model;
		model = (int8_t)*exe_data((uint16_t)(modelAt + STATE_MODEL_NEXT));
	}

	int16_t firstPartEntry = (int16_t)(listIndex + 1);
	int		partOffset = carCount - firstPartEntry;
	int		itemCount = carCount;

	exe_writeU16(
		(uint16_t)(PAPER_ADS_LIST + listIndex * 2),
		PAPER_PARTS_HEADER
	);
	listIndex = firstPartEntry;

	for (int part = 0; part < STATE_PARTS_COUNT; part++) {
		uint16_t partAt = (uint16_t)(STATE_PARTS + part * 8);

		if (exe_readS16((uint16_t)(partAt + STATE_PARTS_PRICE)) >=
			PAPER_MIN_PART_PRICE) {
			uint16_t textOffset =
				exe_readU16((uint16_t)(partAt + STATE_PARTS_TEXT));

			exe_writeU16(
				(uint16_t)(PAPER_ADS_LIST + listIndex * 2),
				textOffset
			);
			listIndex++;
			items[itemCount++] = (int8_t)part;
		}
	}

	exe_writeU16(PAPER_ADS_LIST, (uint16_t)(listIndex - 1));

	int16_t startItem =
		section == PAPER_USED_CARS ? PAPER_START_USED : PAPER_START_PARTS;

	paper__openAds(PAPER_ADS_LIST, startItem);

	for (;;) {
		int16_t choice = paper__runAds();

		if (choice == PAPER_ADS_GARAGE) {
			break;
		}

		bool	 isPart = choice >= firstPartEntry;
		int16_t	 index;
		uint16_t textOffset;
		uint16_t price;

		if (!isPart) {
			index = items[choice - 2];

			uint16_t modelAt =
				(uint16_t)(STATE_MODELS + index * STATE_MODEL_SIZE);

			textOffset = exe_readU16((uint16_t)(modelAt + STATE_MODEL_TEXT));
			price = exe_readU16((uint16_t)(modelAt + STATE_MODEL_PRICE));
			exe_writeU16(PAPER_SEE_IT_BUTTON, PAPER_TYPE_BUTTON);
		} else {
			index = items[choice + partOffset];

			uint16_t partAt = (uint16_t)(STATE_PARTS + index * 8);

			textOffset = exe_readU16((uint16_t)(partAt + STATE_PARTS_TEXT));
			price = exe_readU16((uint16_t)(partAt + STATE_PARTS_PRICE));
			exe_writeU16(PAPER_SEE_IT_BUTTON, PAPER_TYPE_SKIPPED);
		}

		const char * pFirstLine =
			exe_text((uint16_t)(PAPER_MESSAGES + textOffset));

		strcpy((char *)exe_data(PAPER_BUY_TITLE), pFirstLine);
		snprintf(
			(char *)exe_data(PAPER_BUY_PRICE),
			PAPER_PRICE_SPACE,
			exe_text(PAPER_PRICE_FORMAT),
			price
		);

		int8_t paint = -1;
		bool   buy = true;

		for (;;) {
			int16_t answer = ui_run(PAPER_BUY_SCREEN);

			if (answer == PAPER_BUY_YES) {
				break;
			}

			if (answer != PAPER_BUY_SEE_IT) {
				buy = false;
				break;
			}

			paper__seeCar(index, &paint);
		}

		if (!buy) {
			continue;
		}

		int32_t money = (int32_t)exe_readU32(STATE_MONEY);
		int32_t cost = (int16_t)price;

		if (globals_demoMode != 0 && money < cost) {
			money += PAPER_DEMO_TOPUP;
			exe_writeU32(STATE_MONEY, (uint32_t)money);
		}

		if (money < cost) {
			char * pDollar = strchr((char *)exe_data(PAPER_SHORT_TEXT), '$');

			if (pDollar) {
				snprintf(
					pDollar + 1,
					PAPER_SHORT_SPACE,
					exe_text(PAPER_SHORT_FORMAT),
					(int)(price - (uint16_t)money)
				);
			}

			game_message(-PAPER_TEXT_SHORT);
			continue;
		}

		ui_addMoney((int16_t)-price);

		if (!isPart) {
			uint16_t car = cars_create(index, (int16_t)price, 0);

			if (paint == -1) {
				paint = (int8_t)random_next(PAPER_PAINTS);
			}

			*exe_data((uint16_t)(car + STATE_CAR_PAINT)) = (uint8_t)paint;
			game_message(-PAPER_TEXT_BOUGHT);
			globals_gameTime += PAPER_CAR_TIME;
			break;
		}

		cars_addSparePart(index, (int16_t)price);
		globals_gameTime += PAPER_PART_TIME;

		if (exe_readS16(STATE_PART_COUNT) > PAPER_MAX_PARTS) {
			break;
		}
	}

	paper__closeAds();

	uint16_t car = exe_readU16(STATE_CAR);

	if (car != 0) {
		cars_setPaint((int8_t)*exe_data((uint16_t)(car + STATE_CAR_PAINT)));
	}

	globals_place = 0;
}

/** ****************************************************************************
 *
 * \brief	Height of an item in the ads list, in pixel rows.
 *
 * \param	list			DS offset of the list.
 * \param	item			1-based index of the item.
 *
 * \return	Number of lines times 9, plus 3.
 *
 ******************************************************************************/
static int16_t paper__itemHeight(
	uint16_t list,
	int16_t	 item
) {
	uint16_t textOffset = exe_readU16((uint16_t)(list + item * 2));
	uint16_t countByte = (uint16_t)(PAPER_MESSAGES - 1 + textOffset);
	int		 lines = (int8_t)*exe_data(countByte);

	return (int16_t)(lines * PAPER_LINE_ROWS + PAPER_ITEM_PADDING);
}

/** ****************************************************************************
 *
 * \brief	Break the ads list into pages and open the ads screen.
 *
 * \param	list			DS offset of the list (count, then text offsets).
 * \param	startItem		1-based item whose page is shown first.
 *
 * \details	Port of FUN_1000_08b4. Records the first item of each 190-row
 * 			page in paper__pageStarts, selects the page that reaches
 * 			startItem, and opens screen 8.
 *
 ******************************************************************************/
static void paper__openAds(
	uint16_t list,
	int16_t	 startItem
) {
	paper__adsStarted = 0;
	screen_resetSaved();
	paper__adsList = list;
	paper__pageCount = 0;

	int16_t rows = PAPER_ADS_HEIGHT;
	int16_t count = exe_readS16(list);

	paper__page = 1;

	for (int16_t item = 1; item <= count; item++) {
		int16_t height = paper__itemHeight(list, item);

		rows = (int16_t)(rows + height);

		if (rows > PAPER_LAST_ROW) {
			rows = height;

			if (paper__pageCount < PAPER_MAX_PAGES) {
				paper__pageStarts[paper__pageCount] = item;
			}

			paper__pageCount++;
		}

		if (item == startItem) {
			paper__page = (int16_t)(paper__pageCount - 1);
		}
	}

	ui_setStyle(PAPER_ADS_STYLE);
	cursor_set(CURSOR_HIDE);
	paper__redraw = 1;
	ui_open(PAPER_ADS_SCREEN);
	cursor_set(CURSOR_SHOW);
}

/** ****************************************************************************
 *
 * \brief	Draw the current page of ads and set up its hotspots.
 *
 * \return	The row reached after the last drawn ad.
 *
 * \details	First half of FUN_1000_09cb (with FUN_1000_09b7). Draws the
 * 			left and right strips of resource 0x403 at the edges of the
 * 			screen and fills the 192-pixel column between them, then
 * 			draws as many ads as fit into the 190 rows above the status
 * 			line, wrapping from the end of the list back to item 1. Each
 * 			ad gets one of the hotspot records after element 79 (type 11,
 * 			id = item index, or 0 for a section heading); the unused
 * 			records get type 14.
 *
 ******************************************************************************/
static int16_t paper__drawAdsPage(void) {
	int16_t count = exe_readS16(paper__adsList);
	int		surface = globals_workSurface;

	cursor_set(CURSOR_HIDE_NOW);
	ui_setStyle(PAPER_ADS_STYLE);
	screen_drawSprite(surface, PAPER_ADS_SIDES, 0, 0);
	screen_drawSprite(
		surface,
		PAPER_ADS_SIDES,
		PAPER_RIGHT_SIDE - PAPER_SIDE_WIDTH,
		0
	);
	screen_fill(
		surface,
		PAPER_COLUMN_LEFT,
		0,
		PAPER_COLUMN_WIDTH,
		PAPER_ADS_HEIGHT
	);

	int16_t savedHeight = globals_workHeight;

	globals_workHeight = PAPER_ADS_HEIGHT;

	int16_t item = paper__adsStarted == 0
		? paper__pageStarts[paper__page]
		: paper__firstItem;

	paper__firstItem = item;

	int16_t	 row = 0;
	uint16_t element = PAPER_LIST_BOX;

	for (;;) {
		screen_horizontalLine(
			surface,
			PAPER_TEXT_LEFT,
			(int16_t)(row + 1),
			PAPER_TEXT_WIDTH
		);

		int16_t lineTop = (int16_t)(row + PAPER_ITEM_PADDING);

		if (lineTop >= PAPER_ADS_HEIGHT) {
			row = lineTop;
			break;
		}

		uint16_t nextElement = (uint16_t)(element + PAPER_ELEMENT_SIZE);
		int16_t	 textOffset =
			exe_readS16((uint16_t)(paper__adsList + item * 2));
		int16_t hotId = textOffset < PAPER_USED_HEADER ? item : 0;

		exe_writeU16(
			(uint16_t)(nextElement + PAPER_ELEMENT_TOP),
			(uint16_t)(row + 1)
		);
		exe_writeU16(nextElement, PAPER_TYPE_HOT);
		exe_writeU16(
			(uint16_t)(nextElement + PAPER_ELEMENT_ID),
			(uint16_t)hotId
		);

		const char * pLine =
			exe_text((uint16_t)(PAPER_MESSAGES + textOffset));

		text_draw(surface, PAPER_TEXT_LEFT, lineTop, pLine);

		for (;;) {
			pLine += strlen(pLine) + 1;

			int16_t left;

			if (*pLine == '+') {
				pLine++;
				left = globals_textEndX;
			} else {
				if ((uint8_t)*pLine < ' ') {
					break;
				}

				lineTop = (int16_t)(lineTop + PAPER_LINE_ROWS);

				if (lineTop > PAPER_LAST_ROW) {
					break;
				}

				left = PAPER_TEXT_LEFT;
			}

			text_draw(surface, left, lineTop, pLine);
		}

		row = (int16_t)(lineTop + PAPER_LINE_ROWS);

		int16_t bottom = (int16_t)(lineTop + PAPER_LINE_ROWS + 1);

		if (bottom > PAPER_LAST_ROW) {
			bottom = PAPER_LAST_ROW;
		}

		exe_writeU16(
			(uint16_t)(nextElement + PAPER_ELEMENT_BOTTOM),
			(uint16_t)bottom
		);

		if (item >= count) {
			item = 0;
		}

		element = nextElement;

		if (row > PAPER_LAST_ROW - 1) {
			break;
		}

		item++;

		if (item > count) {
			break;
		}
	}

	paper__lastItem = item;
	paper__adsStarted = 1;

	int16_t endRow = (int16_t)(row + 1);
	int16_t bottom = endRow > PAPER_LAST_ROW ? PAPER_LAST_ROW : endRow;

	exe_writeU16(
		(uint16_t)(element + PAPER_ELEMENT_BOTTOM),
		(uint16_t)bottom
	);

	if (bottom < PAPER_LAST_ROW) {
		screen_horizontalLine(
			surface,
			PAPER_TEXT_LEFT,
			endRow,
			PAPER_TEXT_WIDTH
		);
	}

	while (exe_readS16(
			   (uint16_t)(element + PAPER_ELEMENT_SIZE + PAPER_ELEMENT_ID)
		   ) >= 0) {
		element = (uint16_t)(element + PAPER_ELEMENT_SIZE);
		exe_writeU16(element, PAPER_TYPE_SKIPPED);
	}

	paper__redraw = 0;
	globals_workHeight = savedHeight;
	cursor_set(CURSOR_SHOW);

	if (globals_drawThrough != 0) {
		screen_copyToDisplay(0, 0, PLAT_WIDTH - 1, PAPER_LAST_ROW);
	}

	event_reset();

	return row;
}

/** ****************************************************************************
 *
 * \brief	Show the ads page, turn pages on Next/Previous, and return a
 * 			clicked ad or PAPER_ADS_GARAGE.
 *
 * \return	1-based index in the ads list, or PAPER_ADS_GARAGE.
 *
 * \details	Port of FUN_1000_09cb.
 *
 ******************************************************************************/
static int16_t paper__runAds(void) {
	int16_t count = exe_readS16(paper__adsList);
	int16_t row = 0;

	for (;;) {
		if (paper__redraw != 0) {
			row = paper__drawAdsPage();
		}

		int16_t result;

		do {
			result = event_wait(PAPER_ADS_WAIT);
		} while (result == 0);

		if (result == PAPER_ADS_GARAGE) {
			return PAPER_ADS_GARAGE;
		}

		int16_t nextFirst;

		if (result == PAPER_ADS_NEXT) {
			paper__page++;

			if (paper__page >= paper__pageCount) {
				paper__page = 0;
			}

			/*
			 * When the last ad on the page was cut off mid-item, row
			 * reached PAPER_CUTOFF_ROW and it is shown again at the top
			 * of the next page; otherwise the next page starts with the
			 * item after it.
			 */
			int16_t advance = row < PAPER_CUTOFF_ROW ? 1 : 0;

			nextFirst = (int16_t)(paper__lastItem + advance);
		} else if (result == PAPER_ADS_PREVIOUS) {
			paper__page--;

			if (paper__page < 0) {
				paper__page = (int16_t)(paper__pageCount - 1);
			}

			int16_t item = (int16_t)(paper__firstItem - 1);

			if (item < 1) {
				item = count;
			}

			int16_t remaining = PAPER_LAST_ROW;

			for (;;) {
				int16_t height = paper__itemHeight(paper__adsList, item);

				remaining = (int16_t)(remaining - height);

				if (remaining <= 0) {
					break;
				}

				item--;

				if (item < 1) {
					item = count;
				}
			}

			paper__lastItem = item;
			nextFirst = item;
		} else {
			paper__redraw = 1;
			return result;
		}

		paper__firstItem = nextFirst;
		paper__redraw = 1;
	}
}

/** ****************************************************************************
 *
 * \brief	Close the ads screen.
 *
 * \details	Port of FUN_1000_0d58.
 *
 ******************************************************************************/
static void paper__closeAds(void) {
	screen_resetSaved();
	paper__adsList = 0;
	ui_close(1);
}

/** ****************************************************************************
 *
 * \brief	Show the "See it" box for a car in the ads.
 *
 * \param	model			Index of the car model.
 * \param	pPaint			In, out: paint chosen for the car (-1 before the
 * 							first call).
 *
 * \details	Part of ShowAds (1000:472b). Saves the middle of the screen,
 * 			fills the box and the title bar below it, chooses a random
 * 			paint on first view, draws the car and its description in the
 * 			bar, and waits for a key or click before putting the saved
 * 			area back.
 *
 ******************************************************************************/
static void paper__seeCar(
	int16_t	 model,
	int8_t * pPaint
) {
	cursor_set(CURSOR_HIDE_NOW);
	screen_resetSaved();

	int saved =
		screen_saveRect(0, PAPER_SEE_TOP, PLAT_WIDTH, PAPER_SEE_HEIGHT);

	ui_setStyle(PAPER_SEE_STYLE);
	screen_fillRect(
		0,
		PAPER_SEE_BOX_TOP,
		PLAT_WIDTH,
		PAPER_SEE_BOX_HEIGHT,
		(uint8_t)exe_readS16(PAPER_SEE_COLOR)
	);
	screen_fillRect(
		0,
		PAPER_SEE_BAR_TOP,
		PLAT_WIDTH,
		PAPER_SEE_BAR_HEIGHT,
		globals_textBackground
	);

	if (*pPaint == -1) {
		*pPaint = (int8_t)random_next(PAPER_PAINTS);
		cars_setPaint(*pPaint);
	}

	uint16_t modelAt = (uint16_t)(STATE_MODELS + model * STATE_MODEL_SIZE);
	uint8_t	 kind = *exe_data((uint16_t)(modelAt + STATE_MODEL_KIND));
	uint8_t	 flags = (kind & 7) == 2 ? CARS_FLAG_BLOWER : 0;
	uint16_t fitted = exe_readU16((uint16_t)(modelAt + STATE_MODEL_FITTED));
	int		 tires = (fitted >> 4) & 3;
	uint16_t tireTable = (uint16_t)(STATE_TIRE_SPRITES + tires * 8);

	cars_draw(
		CARS_DRAW_PREVIEW,
		model,
		flags,
		0,
		0,
		PAPER_SEE_BOTTOM,
		0,
		tireTable
	);

	const char * pTitle = exe_text(PAPER_BUY_TITLE);
	int16_t		 left = (int16_t)((PLAT_WIDTH - text_width(pTitle)) / 2);

	screen_drawText(left, PAPER_SEE_TEXT_TOP, pTitle);
	cursor_set(CURSOR_SHOW_NOW);
	globals_anyKeyEnds = 1;
	ui_run(PAPER_ANY_KEY_SCREEN);
	globals_anyKeyEnds = 0;
	screen_restoreRect(saved, true);
	screen_resetSaved();
}
