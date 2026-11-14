/** ****************************************************************************
 *
 * \file	ui.c
 *
 * \brief	Screens made of elements (boxes, texts, buttons, hotspots) and
 * 			the status line.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/ui.h"

#include <stdbool.h>
#include <stdio.h> /* snprintf only; no file access or printing */
#include <stdlib.h>
#include <string.h>

#include "game/cursor.h"
#include "game/effects.h"
#include "game/event.h"
#include "game/exe.h"
#include "game/globals.h"
#include "game/input.h"
#include "game/screen.h"
#include "game/state.h"
#include "game/text.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define UI_ELEMENTS			 0x9f4	/**< DS: element records. */
#define UI_ELEMENT_SIZE		 18		/**< Bytes of an element record. */
#define UI_ELEMENT_LEFT		 8		/**< Element field: left edge. */
#define UI_SCREEN_TABLE		 0x2bad /**< DS: header per screen. */
#define UI_TEXTS			 0x239e /**< DS: texts offset base. */
#define UI_STYLE_SIZE		 5		/**< Bytes of a style. */
#define UI_STATUS_STYLE		 11		/**< Style of the status line. */
#define UI_STATUS_HEIGHT	 10		/**< Rows of the status line. */
#define UI_STATUS_LEFT		 0x58	/**< Default left of message. */
#define UI_STATUS_WIDTH		 0xa0	/**< Default width of message. */
#define UI_MARGIN			 3		/**< Frame around element. */
#define UI_TITLE_ROWS		 9		/**< Title rows above line. */
#define UI_FAR				 1000	/**< Larger than coordinates. */
#define UI_WAIT_TICKS		 32000	/**< Ticks ui_run waits. */
#define UI_MESSAGES			 0x2c02 /**< DS: messages count from. */
#define UI_BUBBLES			 0x4c4	/**< DS: bubble records. */
#define UI_BUBBLE_LINES		 2		/**< Bubble field: text lines. */
#define UI_BUBBLE_LEFT		 4		/**< Bubble field: left edge. */
#define UI_BUBBLE_TOP		 6		/**< Bubble field: top edge. */
#define UI_BUBBLE_WIDTH		 8		/**< Bubble field: width. */
#define UI_BUBBLE_HEIGHT	 10		/**< Bubble field: height. */
#define UI_BUBBLE_INDENT	 12		/**< Bubble field: text shift. */
#define UI_BUBBLE_DROP		 14		/**< Bubble field: text drop. */
#define UI_MESSAGE_STYLE	 14		/**< Style of message texts. */
#define UI_MESSAGE_LINE_ROWS 9		/**< Rows from line to line. */
#define UI_CARET_TICKS		 0x23	/**< Ticks per caret blink. */
#define UI_INPUT_COLUMNS	 6		/**< Columns of typed char. */
#define UI_INPUT_BLANK		 0x9f0	/**< DS: a space, for erase. */
#define UI_INPUT_UNKNOWN	 0x290c /**< DS offset: byte zeroed in input. */
#define UI_MONEY_DIGITS		 12		/**< Buffer for the money amount. */
#define UI_MONEY_WIDTH		 0x30	/**< Width given for the amount. */
#define UI_GEAR_TEXTS		 0x482	/**< DS: pointers to "0".."4","N","D". */
#define UI_GEAR_WIDTH		 8		/**< Width of the gear text. */
#define UI_FIRST_CHARACTER	 0x20	/**< Lowest character typed. */
#define UI_LAST_CHARACTER	 0x5a	/**< Highest character typed. */
#define UI_LIST_EMPTY		 0x3ea	/**< ui_list result when count is 0. */
#define UI_LIST_PERCENT_BUF	 0x7aa	/**< DS offset: "(x% worn)". */
#define UI_LIST_NEW_TEXT	 0x7b6	/**< DS offset: "NEW" text. */
#define UI_PERCENT_DIGITS	 1		/**< Where the digits go in it. */
#define UI_PERCENT_SPACE	 3		/**< Byte made a space first. */
#define UI_LIST_SEP_TEXT	 0x7be	/**< DS offset: " - " separator. */
#define UI_LIST_NO_PERCENT	 -128	/**< Percent entry meaning no wear. */
#define UI_LIST_NO_HOVER	 0x26	/**< Screen without hover highlight. */
#define UI_LIST_SCROLL_DOWN	 -10	/**< List key: scroll down one row. */
#define UI_LIST_PAGE_DOWN	 -9		/**< List key: scroll down one page. */
#define UI_LIST_PAGE_UP		 -8		/**< List key: scroll up one page. */
#define UI_LIST_SCROLL_UP	 -7		/**< List key: scroll up one row. */
#define UI_LIST_EDIT_KEY	 -6		/**< List key: edit selected name. */
#define UI_NUMBER_MAX_DIGITS 10		/**< Max digits in ui_numberInput. */

#define UI_TYPE				 0	/**< Element field: type. */
#define UI_STYLE			 2	/**< Element field: style. */
#define UI_TEXT				 4	/**< Element field: text offset. */
#define UI_ID				 6	/**< Element field: number when clicked. */
#define UI_LEFT				 8	/**< Element field: left edge. */
#define UI_TOP				 10 /**< Element field: top edge. */
#define UI_RIGHT			 12 /**< Element field: right edge, excluded. */
#define UI_BOTTOM			 14 /**< Element field: bottom edge, excluded. */
#define UI_NEXT				 16 /**< Element field: next element drawn. */

#define UI_TYPE_PLAIN		 3	/**< Header: screen without list frame. */
#define UI_TYPE_BOX			 4	/**< Box with an optional title. */
#define UI_TYPE_TEXT		 5	/**< Text at the left top corner. */
#define UI_TYPE_CENTER		 6	/**< Text centered in the element. */
#define UI_TYPE_INPUT		 7	/**< Input line, text at the left. */
#define UI_TYPE_INPUT2		 8	/**< Input line, text centered. */
#define UI_TYPE_BUTTON		 10 /**< Framed button with centered text. */
#define UI_TYPE_LIST		 12 /**< Box around a list; hotspot group. */
#define UI_TYPE_ITEM		 13 /**< List item. */
#define UI_TYPE_SKIPPED		 14 /**< Hotspot record that is passed over. */
#define UI_TYPE_LAST		 14 /**< Highest type that is drawn or chained. */
#define UI_FIRST_HOT		 10 /**< Lowest type of a hotspot record. */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/**
 * The drawing style: colors and frame style (DS:0x8242..0x8246).
 */
typedef struct ui__style_tag {
	uint8_t textColor;		/**< globals_textColor. */
	uint8_t textBackground; /**< globals_textBackground. */
	uint8_t fillColor;		/**< globals_fillColor. */
	uint8_t lineColor;		/**< globals_lineColor. */
	uint8_t borderStyle;	/**< globals_borderStyle. */
} ui__Style;

/*******************************************************************************
;
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

static int16_t ui__field(
	uint16_t element,
	int		 field
);

static const char * ui__text(
	uint16_t element
);

static uint16_t ui__elementAt(
	int16_t index
);

static ui__Style ui__getStyle(void);

static void ui__putStyle(
	const ui__Style * pStyle
);

static void ui__drawBox(
	uint16_t element
);

static void ui__drawCentered(
	uint16_t element
);

static void ui__drawButton(
	uint16_t element
);

static void ui__drawInput(
	uint16_t element
);

static bool ui__drawElement(
	uint16_t  element,
	int16_t * pStyle
);

static void ui__beep(void);

static void ui__growBounds(
	uint16_t element
);

static uint16_t ui__formatPercent(
	int16_t percent
);

static void ui__fillInputFromList(
	int16_t		   screen,
	uint16_t	   inputElement,
	char *		   pInputText,
	uint16_t	   listOffset,
	const int8_t * pPercents,
	int16_t		   selection
);

static void ui__updateListHover(
	int16_t	  screen,
	int16_t	  topItem,
	int16_t	  count,
	uint8_t	  hoverColor,
	int16_t * pHoveredRow
);

static uint16_t ui__listItem(
	int16_t row
);

static void ui__moveListSelection(
	int16_t oldRow,
	int16_t newRow,
	int16_t itemStyle,
	int16_t itemWidth,
	int16_t itemHeight
);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/**
 * DS:0x4fa: the picture saved under the speech bubble (a handle of
 * screen_saveRect), or 0.
 */
static int ui__messageSaved;

/**
 * DS:0x8ba0: 1-based index of the item selected last in ui_list.
 */
static int16_t ui__listSelection;

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Select a drawing style: colors and frame style.
 *
 * \param	style			Index into the style table (globals_styleTable).
 *
 * \details	Copies the 5 bytes of the style to DS:0x8242..0x8246, as many
 * 			functions of the original do inline.
 *
 ******************************************************************************/
void ui_setStyle(
	int16_t style
) {
	int				offset = globals_styleTable + style * UI_STYLE_SIZE;
	const uint8_t * pStyle = exe_data((uint16_t)offset);

	globals_textColor = pStyle[0];
	globals_textBackground = pStyle[1];
	globals_fillColor = pStyle[2];
	globals_lineColor = pStyle[3];
	globals_borderStyle = pStyle[4];
}

/** ****************************************************************************
 *
 * \brief	Set up and clear the status line.
 *
 * \param	row				First of its 10 rows.
 *
 * \details	Port of FUN_1f38_2022. Messages are centered in 160 columns
 * 			from column 88 unless a caller says otherwise. The status
 * 			style stays selected.
 *
 ******************************************************************************/
void ui_statusInit(
	int16_t row
) {
	globals_statusTop = (int16_t)(row + 1);
	globals_statusWidth = UI_STATUS_WIDTH;
	globals_statusLeft = UI_STATUS_LEFT;
	globals_statusShown = 0;
	ui_setStyle(UI_STATUS_STYLE);
	screen_fill(globals_workSurface, 0, row, PLAT_WIDTH, UI_STATUS_HEIGHT);

	if (globals_drawThrough != 0) {
		screen_copyToDisplay(
			0,
			row,
			PLAT_WIDTH - 1,
			(int16_t)(row + UI_STATUS_HEIGHT - 1)
		);
	}
}

/** ****************************************************************************
 *
 * \brief	Show a message in the status line.
 *
 * \param	left			Left edge of the message; negative: center it in
 * 							the default place.
 * \param	pText			The message; an empty text clears the line.
 * \param	width			Width to center in; 0: the default width.
 *
 * \details	Port of FUN_1f38_20a1. A centered text starts on a column
 * 			that is a multiple of 8. If a message is shown already, the
 * 			line is cleared from the left edge to the right screen edge
 * 			first. The drawing style is kept.
 *
 ******************************************************************************/
void ui_status(
	int16_t		 left,
	const char * pText,
	int16_t		 width
) {
	ui__Style savedStyle = ui__getStyle();
	int16_t	  textLeft = left;

	ui_setStyle(UI_STATUS_STYLE);

	if (width == 0) {
		width = globals_statusWidth;
	}

	if (left < 0) {
		left = globals_statusLeft;

		int centered = (width - text_width(pText)) / 2 + left;

		textLeft = (int16_t)(centered & ~7);
	}

	int16_t top = globals_statusTop;

	cursor_set(CURSOR_HIDE);

	if (globals_statusShown != 0) {
		screen_fill(
			globals_workSurface,
			(int16_t)(left - 1),
			top,
			(int16_t)(PLAT_WIDTH - left),
			TEXT_ROWS
		);
	}

	text_draw(globals_workSurface, textLeft, top, pText);
	globals_statusShown = pText[0];
	cursor_set(CURSOR_SHOW);

	if (globals_drawThrough != 0) {
		int16_t copyLeft = left > 8 ? (int16_t)(left - 8) : left;

		screen_copyToDisplay(
			copyLeft,
			top,
			PLAT_WIDTH,
			(int16_t)(top + TEXT_ROWS - 1)
		);
	}

	ui__putStyle(&savedStyle);
}

/** ****************************************************************************
 *
 * \brief	Draw an element and, while a screen is opened, the elements
 * 			chained to it.
 *
 * \param	element			DS offset of the first element.
 *
 * \details	Port of FUN_1f38_465e (its type switch is a jump table at
 * 			1f38:49e8). While globals_drawChain is -1 the +16 fields are
 * 			followed and the bounds of everything drawn are collected in
 * 			globals_bounds*; while it is 0 only this element is drawn and
 * 			then copied to the display with its frame (if
 * 			globals_drawThrough is set). Input lines and list items are
 * 			remembered for the functions that use them.
 *
 ******************************************************************************/
void ui_drawElements(
	uint16_t element
) {
	int16_t style = 0;
	bool	single = globals_drawChain == 0 && globals_drawThrough != 0;

	if (single) {
		cursor_set(CURSOR_HIDE);
	}

	for (;;) {
		int16_t type = ui__field(element, UI_TYPE);

		if (type < 1 || type > UI_TYPE_LAST) {
			break;
		}

		if (ui__drawElement(element, &style)) {
			ui__growBounds(element);
		}

		int16_t next = ui__field(element, UI_NEXT) & globals_drawChain;

		if (next == 0) {
			break;
		}

		element = ui__elementAt(next);
	}

	if (single) {
		cursor_set(CURSOR_SHOW);
		screen_copyToDisplay(
			(int16_t)(ui__field(element, UI_LEFT) - UI_MARGIN),
			(int16_t)(ui__field(element, UI_TOP) - UI_MARGIN),
			(int16_t)(ui__field(element, UI_RIGHT) + 2 * UI_MARGIN),
			(int16_t)(ui__field(element, UI_BOTTOM) + 2 * UI_MARGIN)
		);
	}
}

/** ****************************************************************************
 *
 * \brief	Open a screen: draw its elements and make its hotspots and
 * 			keys active.
 *
 * \param	screen			Screen number.
 *
 * \details	Port of FUN_1f38_4a66. Up to 4 screens can be open on top of
 * 			each other; opening a fifth does nothing. The drawing style
 * 			is kept. The drawn area is copied to the display while
 * 			globals_drawThrough is set.
 *
 ******************************************************************************/
void ui_open(
	int16_t screen
) {
	if (globals_screenDepth >= GLOBALS_SCREEN_DEPTH) {
		return;
	}

	cursor_set(CURSOR_HIDE);
	globals_screenStack[globals_screenDepth++] = screen;
	globals_screenId = screen;
	globals_hotBottom = -1;
	globals_hotTop = -1;
	globals_hotRight = -1;
	globals_hotLeft = -1;

	uint16_t tableEntry = (uint16_t)(UI_SCREEN_TABLE + screen * 2);
	uint16_t header = ui__elementAt(exe_readS16(tableEntry));
	uint16_t firstHot = (uint16_t)(header + UI_ELEMENT_SIZE);

	globals_screenValue = ui__field(header, UI_TEXT);
	globals_hotList =
		ui__field(firstHot, UI_TYPE) < UI_FIRST_HOT ? 0 : firstHot;
	globals_inputElement = 0;
	globals_listFirst = 0;
	globals_listCount = 0;
	globals_inputIsLeft = 0;
	globals_screenIsPlain = ui__field(header, UI_TYPE) == UI_TYPE_PLAIN;

	ui__Style savedStyle = ui__getStyle();

	globals_drawChain = -1;
	globals_boundsTop = UI_FAR;
	globals_boundsLeft = UI_FAR;
	globals_boundsBottom = -1;
	globals_boundsRight = -1;

	int16_t first = ui__field(header, UI_NEXT);

	if (first != 0) {
		ui_drawElements(ui__elementAt(first));
	}

	globals_drawChain = 0;
	ui__putStyle(&savedStyle);
	cursor_set(CURSOR_SHOW);

	if (globals_drawThrough != 0 && globals_boundsRight >= 0) {
		screen_copyToDisplay(
			(int16_t)(globals_boundsLeft - UI_MARGIN),
			(int16_t)(globals_boundsTop - UI_MARGIN),
			(int16_t)(globals_boundsRight + 2 * UI_MARGIN),
			(int16_t)(globals_boundsBottom + 2 * UI_MARGIN)
		);
	}

	event_setScreenKeys(screen);
	event_reset();
}

/** ****************************************************************************
 *
 * \brief	Close the screen opened last.
 *
 * \param	reopen			Not 0: make the screen below it active again (by
 * 							opening it once more, which also redraws it); 0: no
 * 							screen is active afterwards.
 *
 * \details	Port of FUN_1f38_4ba1. Nothing is erased; the caller redraws
 * 			what the screen covered.
 *
 ******************************************************************************/
void ui_close(
	int16_t reopen
) {
	if (globals_screenDepth == 0) {
		return;
	}

	event_restoreKeys(0);
	globals_hotList = 0;
	globals_screenDepth--;

	if (globals_screenDepth == 0 || reopen == 0) {
		globals_screenId = 0;
		globals_hotList = 0;
		globals_hotId = -1;
		globals_hotBottom = -1;
		globals_hotRight = -1;
		globals_hotTop = -1;
		globals_hotLeft = -1;
	} else {
		globals_screenDepth--;
		ui_open(globals_screenStack[globals_screenDepth]);
	}
}

/** ****************************************************************************
 *
 * \brief	Forget all open screens.
 *
 * \details	Port of FUN_1f38_4bff.
 *
 ******************************************************************************/
void ui_reset(void) {
	globals_screenValue = 0;
	globals_screenDepth = 0;
	globals_hotList = 0;
	globals_hotId = 0;
	globals_hotBottom = 0;
	globals_hotRight = 0;
	globals_hotTop = 0;
	globals_hotLeft = 0;
}

/** ****************************************************************************
 *
 * \brief	Find the hotspot of the active screen at a point.
 *
 * \param	column			Column of the point.
 * \param	row				Row of the point.
 *
 * \return	Number of the hotspot (its +6 field), or 0 when there is none.
 *
 * \details	Port of FUN_1f38_4dad. The hotspots are sorted by their left
 * 			edge; the search ends at the first one right of the point.
 * 			A type 12 record groups the records after it: when the point
 * 			is inside it they are searched, otherwise the search goes on
 * 			at the record its +6 field names. globals_hotElement and
 * 			globals_hotId get the result, and globals_hotLeft/Right/Top/
 * 			Bottom a rectangle around the point in which the result is the
 * 			same, so callers need not search again while the cursor stays
 * 			inside it.
 *
 ******************************************************************************/
int16_t ui_hitTest(
	int16_t column,
	int16_t row
) {
	uint16_t element = globals_hotList;

	if (element == 0) {
		globals_hotTop = -1;
		globals_hotLeft = -1;
		globals_hotBottom = UI_FAR;
		globals_hotRight = UI_FAR;
		globals_hotId = 0;

		return 0;
	}

	globals_hotTop = 0;
	globals_hotLeft = 0;
	globals_hotRight = PLAT_WIDTH - 1;
	globals_hotBottom = PLAT_HEIGHT - 1;

	for (;;) {
		int16_t type = ui__field(element, UI_TYPE);

		if (type < UI_FIRST_HOT) {
			break;
		}

		if (type == UI_TYPE_SKIPPED) {
			element = (uint16_t)(element + UI_ELEMENT_SIZE);
			continue;
		}

		int16_t left = ui__field(element, UI_LEFT);
		int16_t top = ui__field(element, UI_TOP);
		int16_t right = ui__field(element, UI_RIGHT);
		int16_t bottom = ui__field(element, UI_BOTTOM);
		bool	inside = false;

		if (column < left) {
			globals_hotRight = left;
			break;
		}

		if (column >= right) {
			if (globals_hotLeft < right) {
				globals_hotLeft = right;
			}
		} else if (row < top - UI_MARGIN) {
			if (top - UI_MARGIN < globals_hotBottom) {
				globals_hotBottom = (int16_t)(top - UI_MARGIN);
			}
		} else if (row < bottom) {
			if (type != UI_TYPE_LIST) {
				globals_hotLeft = left;
				globals_hotRight = right;
				globals_hotTop = top;
				globals_hotBottom = bottom;
				globals_hotElement = element;
				globals_hotId = ui__field(element, UI_ID);

				return globals_hotId;
			}

			inside = true;
		} else if (globals_hotTop < bottom) {
			globals_hotTop = bottom;
		}

		if (type != UI_TYPE_LIST || inside) {
			element = (uint16_t)(element + UI_ELEMENT_SIZE);
		} else {
			element = ui__elementAt(ui__field(element, UI_ID));
		}
	}

	globals_hotElement = 0;
	globals_hotId = 0;

	return 0;
}

/** ****************************************************************************
 *
 * \brief	Sort element records by their left edge.
 *
 * \param	first			Index of the first record.
 * \param	count			Number of records.
 *
 * \details	Port of SortElementsByLeft (1f38:4265): an exchange sort,
 * 			which ui_hitTest relies on for the hotspots.
 *
 ******************************************************************************/
void ui_sortElements(
	int16_t first,
	int16_t count
) {
	uint8_t * pFirst =
		exe_data((uint16_t)(UI_ELEMENTS + first * UI_ELEMENT_SIZE));

	for (int index = 0; index < count - 1; index++) {
		uint8_t * pLower = pFirst + index * UI_ELEMENT_SIZE;

		for (int other = index + 1; other < count; other++) {
			uint8_t *		pUpper = pFirst + other * UI_ELEMENT_SIZE;
			const uint8_t * pUpperLeft = pUpper + UI_ELEMENT_LEFT;
			const uint8_t * pLowerLeft = pLower + UI_ELEMENT_LEFT;
			int16_t			upperLeft =
				(int16_t)(pUpperLeft[0] | (pUpperLeft[1] << 8));
			int16_t lowerLeft =
				(int16_t)(pLowerLeft[0] | (pLowerLeft[1] << 8));

			if (upperLeft < lowerLeft) {
				uint8_t swapped[UI_ELEMENT_SIZE];

				memcpy(swapped, pLower, UI_ELEMENT_SIZE);
				memcpy(pLower, pUpper, UI_ELEMENT_SIZE);
				memcpy(pUpper, swapped, UI_ELEMENT_SIZE);
			}
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Open a screen, wait for a hotspot click or a key, and close it.
 *
 * \param	screen			Screen number.
 *
 * \return	The result of event_wait: a hotspot number, a negative key
 * 			result of the screen, EVENT_TIMEOUT or EVENT_KEY.
 *
 * \details	Port of FUN_1f38_4f14.
 *
 ******************************************************************************/
int16_t ui_run(
	int16_t screen
) {
	int16_t result;

	ui_open(screen);

	do {
		result = event_wait(UI_WAIT_TICKS);
	} while (result == 0);

	ui_close(1);

	return result;
}

/** ****************************************************************************
 *
 * \brief	Read a field of an element record.
 *
 * \param	element			DS offset of the element.
 * \param	field			UI_TYPE .. UI_NEXT.
 *
 * \return	The value.
 *
 ******************************************************************************/
static int16_t ui__field(
	uint16_t element,
	int		 field
) {
	return exe_readS16((uint16_t)(element + field));
}

/** ****************************************************************************
 *
 * \brief	The text of an element.
 *
 * \param	element			DS offset of the element.
 *
 * \return	The text its +4 field names.
 *
 ******************************************************************************/
static const char * ui__text(
	uint16_t element
) {
	return exe_text((uint16_t)(UI_TEXTS + ui__field(element, UI_TEXT)));
}

/** ****************************************************************************
 *
 * \brief	DS offset of an element record.
 *
 * \param	index			Index of the record.
 *
 * \return	Its DS offset.
 *
 ******************************************************************************/
static uint16_t ui__elementAt(
	int16_t index
) {
	return (uint16_t)(UI_ELEMENTS + index * UI_ELEMENT_SIZE);
}

/** ****************************************************************************
 *
 * \brief	Get the current drawing style.
 *
 * \return	The style.
 *
 ******************************************************************************/
static ui__Style ui__getStyle(void) {
	ui__Style style = {
		.textColor = globals_textColor,
		.textBackground = globals_textBackground,
		.fillColor = globals_fillColor,
		.lineColor = globals_lineColor,
		.borderStyle = globals_borderStyle,
	};

	return style;
}

/** ****************************************************************************
 *
 * \brief	Set the drawing style.
 *
 * \param	pStyle			The style.
 *
 ******************************************************************************/
static void ui__putStyle(
	const ui__Style * pStyle
) {
	globals_textColor = pStyle->textColor;
	globals_textBackground = pStyle->textBackground;
	globals_fillColor = pStyle->fillColor;
	globals_lineColor = pStyle->lineColor;
	globals_borderStyle = pStyle->borderStyle;
}

/** ****************************************************************************
 *
 * \brief	Draw a box element: filled, framed, with an optional title
 * 			above a line.
 *
 * \param	element			DS offset of the element.
 *
 * \details	Types 4 and 12 of FUN_1f38_465e (1f38:46cc).
 *
 ******************************************************************************/
static void ui__drawBox(
	uint16_t element
) {
	int16_t left = ui__field(element, UI_LEFT);
	int16_t top = ui__field(element, UI_TOP);
	int16_t width = (int16_t)(ui__field(element, UI_RIGHT) - left);
	int16_t height = (int16_t)(ui__field(element, UI_BOTTOM) - top);
	int		surface = globals_workSurface;

	screen_fill(surface, left, top, width, height);

	if (globals_borderStyle != 0) {
		screen_frame(surface, left, top, width, height);
	}

	if (ui__field(element, UI_TEXT) != 0) {
		const char * pText = ui__text(element);
		int16_t		 indent = (int16_t)((width - text_width(pText)) >> 1);

		text_draw(surface, (int16_t)(left + indent), top, pText);
		screen_horizontalLine(
			surface,
			left,
			(int16_t)(top + UI_TITLE_ROWS),
			width
		);
	}
}

/** ****************************************************************************
 *
 * \brief	Draw the text of an element in the middle of the element.
 *
 * \param	element			DS offset of the element.
 *
 * \details	Type 6 of FUN_1f38_465e (1f38:48f3), and the text of a button.
 *
 ******************************************************************************/
static void ui__drawCentered(
	uint16_t element
) {
	int16_t		 left = ui__field(element, UI_LEFT);
	int16_t		 top = ui__field(element, UI_TOP);
	int16_t		 width = (int16_t)(ui__field(element, UI_RIGHT) - left);
	int16_t		 height = (int16_t)(ui__field(element, UI_BOTTOM) - top);
	const char * pText = ui__text(element);
	int16_t		 indent = (int16_t)((width - text_width(pText)) >> 1);
	int16_t		 drop = (int16_t)((height - TEXT_ROWS) >> 1);

	text_draw(
		globals_workSurface,
		(int16_t)(left + indent),
		(int16_t)(top + drop),
		pText
	);
}

/** ****************************************************************************
 *
 * \brief	Draw a button element: filled, framed, text in the middle.
 *
 * \param	element			DS offset of the element.
 *
 * \details	Type 10 of FUN_1f38_465e (1f38:4806).
 *
 ******************************************************************************/
static void ui__drawButton(
	uint16_t element
) {
	int16_t left = ui__field(element, UI_LEFT);
	int16_t top = ui__field(element, UI_TOP);
	int16_t width = (int16_t)(ui__field(element, UI_RIGHT) - left);
	int16_t height = (int16_t)(ui__field(element, UI_BOTTOM) - top);
	int		surface = globals_workSurface;

	screen_fill(surface, left, top, width, height);
	screen_frame(surface, left, top, width, height);

	ui__drawCentered(element);
}

/** ****************************************************************************
 *
 * \brief	Draw an input line element: one text row, cleared, with its
 * 			text at the left (type 7) or centered (type 8).
 *
 * \param	element			DS offset of the element.
 *
 * \details	Types 7 and 8 of FUN_1f38_465e (1f38:4951). The first input
 * 			line of a screen is remembered in globals_inputElement.
 *
 ******************************************************************************/
static void ui__drawInput(
	uint16_t element
) {
	int16_t		 left = ui__field(element, UI_LEFT);
	int16_t		 top = ui__field(element, UI_TOP);
	int16_t		 width = (int16_t)(ui__field(element, UI_RIGHT) - left);
	int			 surface = globals_workSurface;
	const char * pText = ui__text(element);
	int16_t		 textLeft = left;

	screen_fill(surface, left, top, width, TEXT_ROWS);

	if (ui__field(element, UI_TYPE) == UI_TYPE_INPUT2) {
		textLeft = (int16_t)(left + ((width - text_width(pText)) >> 1));
	}

	text_draw(surface, textLeft, top, pText);

	if (globals_inputElement == 0) {
		globals_inputElement = element;
	}
}

/** ****************************************************************************
 *
 * \brief	Draw one element.
 *
 * \param	element			DS offset of the element.
 * \param	pStyle			In, out: the style selected last; the style of the
 * 							element is selected when it differs.
 *
 * \retval	true	The element counts for the bounds of the drawn area.
 * \retval	false	It does not (headers, hotspots and other types that are
 * 					not drawn).
 *
 * \details	The type switch of FUN_1f38_465e.
 *
 ******************************************************************************/
static bool ui__drawElement(
	uint16_t  element,
	int16_t * pStyle
) {
	int16_t type = ui__field(element, UI_TYPE);
	int16_t style = ui__field(element, UI_STYLE);
	int		surface = globals_workSurface;
	bool	drawn = type == UI_TYPE_BOX || type == UI_TYPE_LIST ||
		type == UI_TYPE_TEXT || type == UI_TYPE_CENTER ||
		type == UI_TYPE_INPUT || type == UI_TYPE_INPUT2 ||
		type == UI_TYPE_BUTTON;

	if (type == UI_TYPE_ITEM) {
		if (globals_listCount++ == 0) {
			globals_listFirst = element;
		}

		return true;
	}

	if (!drawn) {
		return false;
	}

	if (type == UI_TYPE_BOX || type == UI_TYPE_LIST) {
		/* A box without a style is only a place holder. */
		if (style <= 0) {
			return true;
		}
	}

	if (type == UI_TYPE_INPUT) {
		globals_inputIsLeft = 1;
	}

	if (style != *pStyle) {
		*pStyle = style;
		ui_setStyle(style);
	}

	int16_t left = ui__field(element, UI_LEFT);
	int16_t top = ui__field(element, UI_TOP);

	switch (type) {
		case UI_TYPE_BOX:
		case UI_TYPE_LIST:
			ui__drawBox(element);
			break;

		case UI_TYPE_TEXT:
			text_draw(surface, left, top, ui__text(element));
			break;

		case UI_TYPE_CENTER:
			ui__drawCentered(element);
			break;

		case UI_TYPE_BUTTON:
			ui__drawButton(element);
			break;

		default:
			ui__drawInput(element);
			break;
	}

	return true;
}

/** ****************************************************************************
 *
 * \brief	Extend the bounds of the drawn area by an element.
 *
 * \param	element			DS offset of the element.
 *
 * \details	Part of FUN_1f38_465e (1f38:477b).
 *
 ******************************************************************************/
static void ui__growBounds(
	uint16_t element
) {
	int16_t left = ui__field(element, UI_LEFT);
	int16_t top = ui__field(element, UI_TOP);
	int16_t right = ui__field(element, UI_RIGHT);
	int16_t bottom = ui__field(element, UI_BOTTOM);

	if (left < globals_boundsLeft) {
		globals_boundsLeft = left;
	}

	if (right > globals_boundsRight) {
		globals_boundsRight = right;
	}

	if (top < globals_boundsTop) {
		globals_boundsTop = top;
	}

	if (bottom > globals_boundsBottom) {
		globals_boundsBottom = bottom;
	}
}

/** ****************************************************************************
 *
 * \brief	Let the player type a text.
 *
 * \param	left			Left edge of the text.
 * \param	top				Top edge of the text.
 * \param	pText			In: the text shown at first. Out: the typed text.
 * 							The buffer holds maxLength characters and the
 * 							end mark.
 * \param	maxLength		Characters allowed; fewer if they would pass the
 * 							right screen edge (6 columns each).
 *
 * \return	< 0: the result of a key the active screen maps (on the name
 * 			screen -3 for Enter or OK, -2 for Old Game). 13: Enter, on
 * 			screens that do not map it.
 *
 * \details	Port of FUN_1f38_5910. The text is drawn with the current
 * 			style, with a caret that blinks every 35 ticks. The screen's
 * 			letter keys type characters while this runs (the keys that
 * 			were characters are restored), and Backspace, comma, period
 * 			and the space bar are made to type too. Characters 0x20..0x5a
 * 			are accepted; with globals_numericInput only digits, and
 * 			Enter only once something is typed. A wrong key or a full
 * 			line sounds the error beep (FUN_1f38_77ba; Phase 5). The byte
 * 			at DS:0x290c is zero while typing (use unknown).
 *
 ******************************************************************************/
int16_t ui_textInput(
	int16_t left,
	int16_t top,
	char *	pText,
	int16_t maxLength
) {
	uint8_t savedFill = globals_fillColor;
	uint8_t savedLine = globals_lineColor;
	int		surface = globals_workSurface;
	int16_t width = (int16_t)(maxLength * UI_INPUT_COLUMNS);

	if (PLAT_WIDTH <= width + left) {
		maxLength = (int16_t)((PLAT_WIDTH - left - 1) / UI_INPUT_COLUMNS);
		pText[maxLength] = '\0';
		width = (int16_t)(maxLength * UI_INPUT_COLUMNS);
	}

	globals_fillColor = globals_textBackground;
	globals_lineColor = globals_textColor;
	cursor_set(CURSOR_HIDE);
	screen_fill(surface, left, top, width, TEXT_ROWS);
	text_draw(surface, left, top, pText);

	int16_t caretTop = globals_textY;
	int16_t caretLeft = globals_textEndX;
	bool	caretShown = true;

	screen_verticalLine(surface, caretLeft, caretTop, TEXT_ROWS);
	cursor_set(CURSOR_SHOW);

	if (globals_drawThrough != 0) {
		screen_copyToDisplay(
			left,
			top,
			(int16_t)(left + width),
			(int16_t)(top + TEXT_ROWS)
		);
	}

	uint8_t * pUnknown = exe_data(UI_INPUT_UNKNOWN);
	uint8_t	  savedUnknown = *pUnknown;

	*pUnknown = 0;
	event_reset();

	int16_t keyScreen = event_restoreKeys(1);

	input_beginTextInput(globals_numericInput);

	int16_t savedAnyKey = globals_anyKeyEnds;
	int16_t length = (int16_t)strlen(pText);
	int16_t result;

	globals_anyKeyEnds = 1;
	globals_eventKey = 0;

	for (;;) {
		result = event_wait(UI_CARET_TICKS);

		if (result == 0) {
			cursor_set(CURSOR_HIDE);
			globals_lineColor =
				caretShown ? globals_textBackground : globals_textColor;
			screen_verticalLine(surface, caretLeft, caretTop, TEXT_ROWS);
			caretShown = !caretShown;
			cursor_set(CURSOR_SHOW);

			if (globals_drawThrough != 0) {
				screen_copyToDisplay(
					caretLeft,
					caretTop,
					(int16_t)(caretLeft + 1),
					(int16_t)(caretTop + TEXT_ROWS)
				);
			}

			globals_eventKey = 0;
			continue;
		}

		if (result < 0) {
			break;
		}

		int16_t key = globals_eventKey;
		bool	numeric = globals_numericInput != 0;

		globals_eventKey = 0;

		if (key == '\r') {
			result = '\r';

			if (!numeric || pText[0] != '\0') {
				break;
			}

			ui__beep();
			continue;
		}

		if (key == 0) {
			continue;
		}

		if (key != '\b') {
			if (key < UI_FIRST_CHARACTER || key > UI_LAST_CHARACTER) {
				continue;
			}

			if (numeric && (key < '0' || key > '9')) {
				ui__beep();
				continue;
			}
		}

		int16_t changedLeft = caretLeft;

		cursor_set(CURSOR_HIDE);
		globals_lineColor = globals_textBackground;
		screen_verticalLine(surface, caretLeft, caretTop, TEXT_ROWS);

		if (key == '\b' && length >= 1) {
			char removed[2] = { pText[length - 1], '\0' };

			length--;
			caretLeft = (int16_t)(caretLeft - text_width(removed));
			text_draw(surface, caretLeft, caretTop, exe_text(UI_INPUT_BLANK));
			changedLeft = caretLeft;
		} else if (key != '\b' && length < maxLength) {
			pText[length] = (char)key;
			pText[length + 1] = '\0';
			text_draw(surface, caretLeft, caretTop, pText + length);
			caretLeft = globals_textEndX;
			length++;
		} else {
			ui__beep();
		}

		globals_lineColor = globals_textColor;
		screen_verticalLine(surface, caretLeft, caretTop, TEXT_ROWS);
		caretShown = true;
		cursor_set(CURSOR_SHOW);

		if (globals_drawThrough != 0) {
			screen_copyToDisplay(
				changedLeft,
				caretTop,
				(int16_t)(changedLeft + UI_INPUT_COLUMNS),
				(int16_t)(caretTop + TEXT_ROWS)
			);
		}
	}

	pText[length] = '\0';
	cursor_set(CURSOR_HIDE);
	globals_lineColor = globals_textBackground;
	screen_verticalLine(surface, caretLeft, caretTop, TEXT_ROWS);
	cursor_set(CURSOR_SHOW);

	if (globals_drawThrough != 0) {
		screen_copyToDisplay(
			(int16_t)(caretLeft - UI_INPUT_COLUMNS),
			top,
			caretLeft,
			(int16_t)(top + TEXT_ROWS)
		);
	}

	globals_lineColor = savedLine;
	globals_fillColor = savedFill;
	*pUnknown = savedUnknown;
	event_reset();
	globals_anyKeyEnds = savedAnyKey;
	input_endTextInput();

	if (keyScreen != 0) {
		event_setScreenKeys(keyScreen);
	}

	return result;
}

/** ****************************************************************************
 *
 * \brief	Open a screen and let the player type into its input line.
 *
 * \param	screen			Screen number; it must have an input line element.
 * \param	maxLength		Characters allowed.
 * \param	pText			In: the text shown at first. Out: the typed text.
 *
 * \return	The result of ui_textInput, with positive results (Enter on
 * 			screens that do not map it) turned into 0.
 *
 * \details	Port of FUN_1f38_5e54. The text uses the style of the input
 * 			line element.
 *
 ******************************************************************************/
int16_t ui_inputScreen(
	int16_t screen,
	int16_t maxLength,
	char *	pText
) {
	ui_open(screen);

	uint16_t element = globals_inputElement;

	ui_setStyle(ui__field(element, UI_STYLE));

	int16_t result = ui_textInput(
		ui__field(element, UI_LEFT),
		ui__field(element, UI_TOP),
		pText,
		maxLength
	);

	ui_close(1);

	return result > 0 ? 0 : result;
}

/** ****************************************************************************
 *
 * \brief	Draw the title at the left of the status line and, after it,
 * 			the player's money.
 *
 * \param	pTitle			The title, normally "Bankroll: $".
 *
 * \details	Port of FUN_1f38_2213. The column after the title, rounded up
 * 			to a multiple of 8, becomes globals_statusValueLeft, where
 * 			ui_addMoney writes the amount. No amount is written when the
 * 			second character of the title is 'G' or a space. The status
 * 			style stays selected.
 *
 ******************************************************************************/
void ui_statusTitle(
	const char * pTitle
) {
	int16_t savedThrough = globals_drawThrough;
	int16_t top = globals_statusTop;

	ui_setStyle(UI_STATUS_STYLE);
	cursor_set(CURSOR_HIDE);
	screen_fill(
		globals_workSurface,
		0,
		top,
		(int16_t)(globals_statusLeft & ~7),
		TEXT_ROWS
	);
	text_draw(globals_workSurface, 0, top, pTitle);
	globals_statusValueLeft = (int16_t)((globals_textEndX + 7) & ~7);
	globals_drawThrough = 0;

	if (pTitle[1] != 'G' && pTitle[1] != ' ') {
		ui_addMoney(0);
	}

	globals_drawThrough = savedThrough;
	cursor_set(CURSOR_SHOW);

	if (globals_drawThrough != 0) {
		screen_copyToDisplay(
			0,
			top,
			globals_statusLeft,
			(int16_t)(top + TEXT_ROWS - 1)
		);
	}
}

/** ****************************************************************************
 *
 * \brief	Change the player's money and show the new amount in the
 * 			status line.
 *
 * \param	amount			Added to the money; negative to spend.
 *
 * \details	Port of FUN_1f38_21c0. The amount is written at
 * 			globals_statusValueLeft with ui_status (width 48), which
 * 			clears the rest of the status line first.
 *
 ******************************************************************************/
void ui_addMoney(
	int16_t amount
) {
	char	digits[UI_MONEY_DIGITS];
	int16_t savedShown = globals_statusShown;
	int32_t money = (int32_t)exe_readU32(STATE_MONEY) + amount;

	globals_statusShown = 1;
	exe_writeU32(STATE_MONEY, (uint32_t)money);
	snprintf(digits, sizeof digits, "%ld", (long)money);
	ui_status(globals_statusValueLeft, digits, UI_MONEY_WIDTH);
	globals_moneyEndX = globals_textEndX;
	globals_statusShown = savedShown;
}

/** ****************************************************************************
 *
 * \brief	Show a gear or shifter digit in the status line during a drive.
 *
 * \param	index			Index into DS:0x482: 0..4 gear, 5 "N", 6 "D".
 *
 * \details	Port of FUN_1f38_22e9. Writes the entry from the table of DS
 * 			text pointers at DS:0x482 using ui_status at
 * 			globals_statusValueLeft (width 8), with globals_statusShown
 * 			set to 1 while drawing so it shows over any message.
 *
 ******************************************************************************/
void ui_statusGear(
	int16_t index
) {
	int16_t savedShown = globals_statusShown;

	globals_statusShown = 1;
	ui_status(
		globals_statusValueLeft,
		exe_text(exe_readU16((uint16_t)(UI_GEAR_TEXTS + index * 2))),
		UI_GEAR_WIDTH
	);
	globals_statusShown = savedShown;
}

/** ****************************************************************************
 *
 * \brief	Sound the error beep.
 *
 * \details	ErrorBeep (1f38:77ba): a short tone on the speaker when the
 * 			sound is on.
 *
 ******************************************************************************/
static void ui__beep(void) {
	effects_errorBeep();
}

/** ****************************************************************************
 *
 * \brief	Show a message in a speech bubble.
 *
 * \param	message			Offset of the message from DS:0x2c02; negative:
 * 							the same message, but the picture under the
 * 							bubble is not kept (the bubble stays until
 * 							something is drawn over it).
 *
 * \details	Port of FUN_1f38_28a4. The byte before the message selects
 * 			one of the bubbles in the table at DS:0x4c4 (18 bytes each:
 * 			+0 resource, +2 number of text lines, +4 left, +6 top, +8
 * 			width, +10 height, +12 and +14 offset of the text). A bubble
 * 			that is still shown is removed first. The area is saved, the
 * 			bubble resource drawn, and the lines (consecutive texts)
 * 			centered in it in style 14, 9 rows apart. Sets
 * 			globals_messageShown, which makes event_wait end on any
 * 			click.
 *
 ******************************************************************************/
void ui_message(
	int16_t message
) {
	cursor_set(CURSOR_HIDE);

	if (ui__messageSaved != 0) {
		screen_restoreRect(ui__messageSaved, false);
		ui__messageSaved = 0;
	}

	bool keep = message >= 0;

	if (!keep) {
		message = (int16_t)-message;
	}

	uint16_t textAt = (uint16_t)(UI_MESSAGES + message);
	int		 kind = (int8_t)*exe_data((uint16_t)(textAt - 1)) - 1;
	uint16_t bubble = (uint16_t)(UI_BUBBLES + kind * UI_ELEMENT_SIZE);
	int16_t	 lines = exe_readS16((uint16_t)(bubble + UI_BUBBLE_LINES));
	int16_t	 left = exe_readS16((uint16_t)(bubble + UI_BUBBLE_LEFT));
	int16_t	 top = exe_readS16((uint16_t)(bubble + UI_BUBBLE_TOP));
	int16_t	 width = exe_readS16((uint16_t)(bubble + UI_BUBBLE_WIDTH));
	int16_t	 height = exe_readS16((uint16_t)(bubble + UI_BUBBLE_HEIGHT));
	int16_t	 indent = exe_readS16((uint16_t)(bubble + UI_BUBBLE_INDENT));
	int16_t	 drop = exe_readS16((uint16_t)(bubble + UI_BUBBLE_DROP));
	int		 surface = globals_workSurface;

	ui__messageSaved = screen_saveRect(left, top, width, height);
	screen_drawSprite(
		surface,
		exe_readS16(bubble),
		(int16_t)(left & 0xff),
		(int16_t)(top & 0xff)
	);
	ui_setStyle(UI_MESSAGE_STYLE);

	const char * pLine = exe_text(textAt);
	int16_t		 lineTop = (int16_t)(top + drop);

	for (; lines != 0; lines--) {
		int16_t centered = (int16_t)((width - text_width(pLine)) >> 1);

		text_draw(
			surface,
			(int16_t)(centered + left + indent),
			lineTop,
			pLine
		);
		pLine += strlen(pLine) + 1;
		lineTop = (int16_t)(lineTop + UI_MESSAGE_LINE_ROWS);
	}

	cursor_set(CURSOR_SHOW);

	if (globals_drawThrough != 0) {
		screen_copyToDisplay(
			left,
			top,
			(int16_t)(left + width - 1),
			(int16_t)(top + height - 1)
		);
	}

	if (!keep) {
		/* Give up the saved picture without putting it back. */
		screen_dropRect(ui__messageSaved);
		ui__messageSaved = 0;
	}

	globals_messageShown = 1;
}

/** ****************************************************************************
 *
 * \brief	Remove the speech bubble shown by ui_message.
 *
 * \details	Port of FUN_1f38_2adf: puts the saved picture back, also on
 * 			the display, and clears globals_messageShown.
 *
 ******************************************************************************/
void ui_messageClose(void) {
	if (ui__messageSaved != 0) {
		screen_restoreRect(ui__messageSaved, true);
		ui__messageSaved = 0;
	}

	globals_messageShown = 0;
}

/** ****************************************************************************
 *
 * \brief	Show a speech bubble at an optional override position without
 * 			making clicks dismiss it.
 *
 * \param	message			Offset of the message from DS:0x2c02.
 * \param	left			>= 0: override left edge; < 0: keep the bubble's
 * 							default left edge.
 * \param	top				>= 0: override top edge; < 0: keep the bubble's
 * 							default top edge.
 *
 * \details	Port of FUN_1f38_2a62. Calls ui_message and then clears
 * 			globals_messageShown so a modal screen can run alongside the
 * 			speech bubble.
 *
 ******************************************************************************/
void ui_bubbleAt(
	int16_t message,
	int16_t left,
	int16_t top
) {
	int16_t	 absMsg = message < 0 ? (int16_t)-message : message;
	uint16_t textAt = (uint16_t)(UI_MESSAGES + absMsg);
	int		 kind = (int8_t)*exe_data((uint16_t)(textAt - 1)) - 1;
	uint16_t bubble = (uint16_t)(UI_BUBBLES + kind * UI_ELEMENT_SIZE);
	int16_t	 savedLeft = exe_readS16((uint16_t)(bubble + UI_BUBBLE_LEFT));
	int16_t	 savedTop = exe_readS16((uint16_t)(bubble + UI_BUBBLE_TOP));

	if (left >= 0) {
		exe_writeU16((uint16_t)(bubble + UI_BUBBLE_LEFT), (uint16_t)left);
	}

	if (top >= 0) {
		exe_writeU16((uint16_t)(bubble + UI_BUBBLE_TOP), (uint16_t)top);
	}

	ui_message(message);
	exe_writeU16((uint16_t)(bubble + UI_BUBBLE_LEFT), (uint16_t)savedLeft);
	exe_writeU16((uint16_t)(bubble + UI_BUBBLE_TOP), (uint16_t)savedTop);
	globals_messageShown = 0;
}

/** ****************************************************************************
 *
 * \brief	Compute the bounding rectangle of a screen's drawn elements,
 * 			including their 3-pixel frame margin.
 *
 * \param	screen			Screen number.
 * \param	pLeft			Out: left edge, clipped to 0..320.
 * \param	pTop			Out: top edge, clipped to 0..200.
 * \param	pWidth			Out: width in pixels.
 * \param	pHeight			Out: height in pixels.
 *
 * \details	Port of FUN_1f38_4c1a.
 *
 ******************************************************************************/
void ui_bounds(
	int16_t	  screen,
	int16_t * pLeft,
	int16_t * pTop,
	int16_t * pWidth,
	int16_t * pHeight
) {
	uint16_t tableEntry = (uint16_t)(UI_SCREEN_TABLE + screen * 2);
	uint16_t element = ui__elementAt(exe_readS16(tableEntry));

	globals_boundsTop = UI_FAR;
	globals_boundsLeft = UI_FAR;
	globals_boundsBottom = 1;
	globals_boundsRight = 1;

	while (ui__field(element, UI_NEXT) != 0) {
		element = ui__elementAt(ui__field(element, UI_NEXT));
		ui__growBounds(element);
	}

	int16_t left = (int16_t)(globals_boundsLeft - UI_MARGIN);

	if (left < 0) {
		left = 0;
	}

	int16_t top = (int16_t)(globals_boundsTop - UI_MARGIN);

	if (top < 0) {
		top = 0;
	}

	int16_t right = (int16_t)(globals_boundsRight + UI_MARGIN);

	if (right > PLAT_WIDTH) {
		right = PLAT_WIDTH;
	}

	int16_t bottom = (int16_t)(globals_boundsBottom + UI_MARGIN);

	if (bottom > PLAT_HEIGHT) {
		bottom = PLAT_HEIGHT;
	}

	*pLeft = left;
	*pTop = top;
	*pWidth = (int16_t)(right - left);
	*pHeight = (int16_t)(bottom - top);
}

/** ****************************************************************************
 *
 * \brief	Save the work-surface rectangle covered by a screen.
 *
 * \param	screen			Screen number.
 *
 * \return	Handle for screen_restoreRect.
 *
 * \details	Port of FUN_1f38_4d10.
 *
 ******************************************************************************/
int ui_saveScreenRect(
	int16_t screen
) {
	int16_t left;
	int16_t top;
	int16_t width;
	int16_t height;

	ui_bounds(screen, &left, &top, &width, &height);

	return screen_saveRect(left, top, width, height);
}

/** ****************************************************************************
 *
 * \brief	Save the area under a screen, run the screen, and restore the
 * 			saved area to the work and display surfaces.
 *
 * \param	screen			Screen number.
 *
 * \return	The result of ui_run.
 *
 * \details	Port of FUN_1f38_4ec1.
 *
 ******************************************************************************/
int16_t ui_runModal(
	int16_t screen
) {
	int		saved = ui_saveScreenRect(screen);
	int16_t result = ui_run(screen);

	screen_restoreRect(saved, true);

	return result;
}

/** ****************************************************************************
 *
 * \brief	Save the area under a list screen, run ui_list, and restore the
 * 			saved area to the work and display surfaces.
 *
 * \param	screen				Screen number.
 * \param	initialSelection	1-based item selected at first.
 * \param	pIndents			Optional 1-based horizontal indents, or NULL.
 * \param	listOffset			DS offset of 1-based array ([0]=count).
 * \param	pPercents			Optional 1-based percentages, or NULL.
 * \param	extraTexts			DS offset of table for negative
 * 								entries, or 0.
 *
 * \return	UI_LIST_EMPTY (0x3ea) if the list is empty, otherwise the result
 * 			of ui_list.
 *
 * \details	Port of FUN_1f38_4f4f.
 *
 ******************************************************************************/
int16_t ui_runList(
	int16_t			screen,
	int16_t			initialSelection,
	const int16_t * pIndents,
	uint16_t		listOffset,
	const int8_t *	pPercents,
	uint16_t		extraTexts
) {
	if (exe_readS16(listOffset) == 0) {
		return UI_LIST_EMPTY;
	}

	int		saved = ui_saveScreenRect(screen);
	int16_t result = ui_list(
		screen,
		initialSelection,
		pIndents,
		listOffset,
		pPercents,
		extraTexts
	);

	screen_restoreRect(saved, true);

	return result;
}

/** ****************************************************************************
 *
 * \brief	Put a wear percentage into the text "(x% worn)" at DS:0x7aa, or
 * 			give the "new" text.
 *
 * \param	percent			0 for new, otherwise the percentage.
 *
 * \return	DS offset of the text (0x7b6 or 0x7aa).
 *
 * \details	Port of FUN_1f38_4fc1: the byte after "x" becomes a space,
 * 			the digits are written from DS:0x7ab (itoa), and their end
 * 			becomes "%"; the rest of the text stays. So one digit reads
 * 			"(5%  worn)" and three digits "(100%worn)", as in the
 * 			original.
 *
 ******************************************************************************/
static uint16_t ui__formatPercent(
	int16_t percent
) {
	if (percent == 0) {
		return UI_LIST_NEW_TEXT;
	}

	char * pText = (char *)exe_data(UI_LIST_PERCENT_BUF);
	char   digits[8];
	int	   length = snprintf(digits, sizeof digits, "%d", (int)percent);

	pText[UI_PERCENT_SPACE] = ' ';

	if (length > 0) {
		memcpy(pText + UI_PERCENT_DIGITS, digits, (size_t)length);
		pText[UI_PERCENT_DIGITS + length] = '%';
	}

	return UI_LIST_PERCENT_BUF;
}

/** ****************************************************************************
 *
 * \brief	Copy the selected list item's text (and optional percentage) into
 * 			a screen's input element and redraw it.
 *
 * \param	screen			Screen number.
 * \param	inputElement	DS offset of the input element.
 * \param	pInputText		Pointer to the input element's text buffer.
 * \param	listOffset		DS offset of 1-based item array.
 * \param	pPercents		Optional 1-based condition percentages, or NULL.
 * \param	selection		1-based selected item index.
 *
 * \details	Helper for FUN_1f38_500e.
 *
 ******************************************************************************/
static void ui__fillInputFromList(
	int16_t		   screen,
	uint16_t	   inputElement,
	char *		   pInputText,
	uint16_t	   listOffset,
	const int8_t * pPercents,
	int16_t		   selection
) {
	uint16_t textAt = (uint16_t)(listOffset + selection * 2);
	uint16_t itemText = (uint16_t)(UI_MESSAGES + exe_readU16(textAt));

	strcpy(pInputText, exe_text(itemText));

	if (screen == 5 && strcmp(pInputText, exe_text(0x4607)) == 0) {
		strcpy(pInputText, exe_text(0x7c2));
	}

	if (pPercents != NULL && pPercents[selection] != UI_LIST_NO_PERCENT) {
		strcat(pInputText, exe_text(UI_LIST_SEP_TEXT));
		strcat(
			pInputText,
			exe_text(ui__formatPercent(pPercents[selection]))
		);
	}

	ui_drawElements(inputElement);
}

/** ****************************************************************************
 *
 * \brief	Update the hover highlight in an open list screen.
 *
 * \param	screen			Screen number.
 * \param	topItem			0-based index of the first visible item.
 * \param	count			Total items in the list.
 * \param	hoverColor		Line color used to highlight the hovered item.
 * \param	pHoveredRow		In, out: 0-based visible row currently hovered,
 * 							or -1.
 *
 * \details	Part of FUN_1f38_500e (1f38:5338).
 *
 ******************************************************************************/
static void ui__updateListHover(
	int16_t	  screen,
	int16_t	  topItem,
	int16_t	  count,
	uint8_t	  hoverColor,
	int16_t * pHoveredRow
) {
	bool overValidItem =
		globals_hotId >= 1 && topItem + globals_hotId <= count;
	int16_t targetRow = overValidItem ? (int16_t)(globals_hotId - 1) : -1;

	if ((!overValidItem && *pHoveredRow < 0) || *pHoveredRow == targetRow) {
		return;
	}

	cursor_set(CURSOR_HIDE);

	int16_t dirtyLeft = UI_FAR;
	int16_t dirtyTop = UI_FAR;
	int16_t dirtyRight = -1;
	int16_t dirtyBottom = -1;

	if (overValidItem) {
		uint16_t itemElem = ui__listItem(targetRow);

		dirtyLeft = ui__field(itemElem, UI_LEFT);
		dirtyTop = ui__field(itemElem, UI_TOP);
		dirtyRight = ui__field(itemElem, UI_RIGHT);
		dirtyBottom = ui__field(itemElem, UI_BOTTOM);

		if (screen != UI_LIST_NO_HOVER) {
			screen_recolor(
				globals_workSurface,
				globals_textColor,
				hoverColor,
				dirtyLeft,
				dirtyTop,
				dirtyRight,
				(int16_t)(dirtyTop + TEXT_ROWS)
			);
		}
	}

	if (*pHoveredRow >= 0) {
		uint16_t oldElem = ui__listItem(*pHoveredRow);
		int16_t	 oldLeft = ui__field(oldElem, UI_LEFT);
		int16_t	 oldTop = ui__field(oldElem, UI_TOP);
		int16_t	 oldRight = ui__field(oldElem, UI_RIGHT);
		int16_t	 oldBottom = ui__field(oldElem, UI_BOTTOM);

		if (screen != UI_LIST_NO_HOVER) {
			screen_recolor(
				globals_workSurface,
				hoverColor,
				globals_textColor,
				oldLeft,
				oldTop,
				oldRight,
				(int16_t)(oldTop + TEXT_ROWS)
			);
		}

		if (oldLeft < dirtyLeft) {
			dirtyLeft = oldLeft;
		}

		if (oldTop < dirtyTop) {
			dirtyTop = oldTop;
		}

		if (oldRight > dirtyRight) {
			dirtyRight = oldRight;
		}

		if (oldBottom > dirtyBottom) {
			dirtyBottom = oldBottom;
		}
	}

	cursor_set(CURSOR_SHOW);

	if (globals_drawThrough != 0 && dirtyRight >= 0) {
		screen_copyToDisplay(dirtyLeft, dirtyTop, dirtyRight, dirtyBottom);
	}

	*pHoveredRow = targetRow;
}

/** ****************************************************************************
 *
 * \brief	Move the selection box from one visible list row to another.
 *
 * \param	oldRow			0-based visible row previously selected.
 * \param	newRow			0-based visible row newly selected.
 * \param	itemStyle		Style index of the list items.
 * \param	itemWidth		Width of a list item in pixels.
 * \param	itemHeight		Height of a list item in pixels.
 *
 * \details	Part of FUN_1f38_500e (1f38:5615).
 *
 ******************************************************************************/
static void ui__moveListSelection(
	int16_t oldRow,
	int16_t newRow,
	int16_t itemStyle,
	int16_t itemWidth,
	int16_t itemHeight
) {
	cursor_set(CURSOR_HIDE);
	ui_setStyle(itemStyle);

	uint16_t oldElem = ui__listItem(oldRow);
	int16_t	 oldLeft = ui__field(oldElem, UI_LEFT);
	int16_t	 oldTop = ui__field(oldElem, UI_TOP);
	uint8_t	 savedLine = globals_lineColor;

	globals_lineColor = globals_textBackground;
	screen_box(globals_workSurface, oldLeft, oldTop, itemWidth, TEXT_ROWS);
	globals_lineColor = savedLine;

	uint16_t newElem = ui__listItem(newRow);
	int16_t	 newLeft = ui__field(newElem, UI_LEFT);
	int16_t	 newTop = ui__field(newElem, UI_TOP);

	screen_box(globals_workSurface, newLeft, newTop, itemWidth, TEXT_ROWS);
	cursor_set(CURSOR_SHOW);

	if (globals_drawThrough != 0) {
		int16_t copyLeft =
			oldLeft - 1 < newLeft - 1 ? (int16_t)(oldLeft - 1)
									  : (int16_t)(newLeft - 1);
		int16_t copyTop =
			oldTop - 1 < newTop - 1 ? (int16_t)(oldTop - 1)
									: (int16_t)(newTop - 1);
		int16_t copyRight = (int16_t)(newLeft + itemWidth + 2);

		if (copyRight < oldLeft + itemWidth) {
			copyRight = (int16_t)(oldLeft + itemWidth);
		}

		int16_t copyBottom = (int16_t)(newTop + itemHeight + 2);

		if (copyBottom < oldTop + TEXT_ROWS) {
			copyBottom = (int16_t)(oldTop + TEXT_ROWS);
		}

		screen_copyToDisplay(copyLeft, copyTop, copyRight, copyBottom);
	}
}

/** ****************************************************************************
 *
 * \brief	Open a list screen and let the player scroll and select an item.
 *
 * \param	screen				Screen number.
 * \param	initialSelection	1-based item selected at first.
 * \param	pIndents			Optional 1-based horizontal indents, or NULL.
 * \param	listOffset			DS offset of 1-based array ([0]=count).
 * \param	pPercents			Optional 1-based percentages, or NULL.
 * \param	extraTexts			DS offset of table for negative
 * 								entries, or 0.
 *
 * \return	UI_LIST_EMPTY (0x3ea) if empty, otherwise negative result
 * 			of button/key. Selection read with ui_listSelection().
 *
 * \details	Port of FUN_1f38_500e.
 *
 ******************************************************************************/
int16_t ui_list(
	int16_t			screen,
	int16_t			initialSelection,
	const int16_t * pIndents,
	uint16_t		listOffset,
	const int8_t *	pPercents,
	uint16_t		extraTexts
) {
	int16_t count = exe_readS16(listOffset);

	if (count == 0) {
		return UI_LIST_EMPTY;
	}

	int16_t selectedRow = (int16_t)(initialSelection - 1);
	int16_t topItem = 0;
	int16_t hoveredRow = -1;

	ui_open(screen);

	int16_t visibleRows = globals_listCount;

	ui__listSelection = initialSelection;

	if (visibleRows < count) {
		if (count < selectedRow + visibleRows) {
			topItem = (int16_t)(count - visibleRows);
		} else {
			topItem = selectedRow;
		}
	}

	uint16_t inputElement = globals_inputElement;
	char *	 pInputText = NULL;
	int16_t	 inputMaxLen = 0;

	if (inputElement != 0) {
		pInputText = (char *)exe_data(
			(uint16_t)(UI_TEXTS + ui__field(inputElement, UI_TEXT))
		);
		inputMaxLen = (int16_t)((ui__field(inputElement, UI_RIGHT) -
								 ui__field(inputElement, UI_LEFT)) /
								UI_INPUT_COLUMNS);

		if (globals_inputIsLeft == 0) {
			ui__fillInputFromList(
				screen,
				inputElement,
				pInputText,
				listOffset,
				pPercents,
				initialSelection
			);
		}
	}

	uint16_t firstElem = globals_listFirst;
	int16_t	 listLeft = ui__field(firstElem, UI_LEFT);
	int16_t	 itemWidth =
		(int16_t)(ui__field(firstElem, UI_RIGHT) - listLeft);
	int16_t listTop = ui__field(firstElem, UI_TOP);
	int16_t itemHeight =
		(int16_t)(ui__field(firstElem, UI_BOTTOM) - listTop);
	int16_t itemStyle = ui__field(firstElem, UI_STYLE);

	memcpy(
		exe_data((uint16_t)(globals_styleTable + 9 * UI_STYLE_SIZE)),
		exe_data((uint16_t)(globals_styleTable + itemStyle * UI_STYLE_SIZE)),
		UI_STYLE_SIZE
	);

	uint8_t hoverColor = *exe_data(
		(uint16_t)(globals_styleTable + itemStyle * UI_STYLE_SIZE + 3)
	);
	int surface = globals_workSurface;

	for (;;) {
		int16_t prevTopItem = topItem;

		ui_setStyle(itemStyle);
		cursor_set(CURSOR_HIDE);

		int16_t rowsToDraw = (int16_t)(count - topItem);

		if (rowsToDraw > visibleRows) {
			rowsToDraw = visibleRows;
		}

		uint16_t rowElem = globals_listFirst;

		for (int16_t row = 0; row < rowsToDraw; row++) {
			int16_t	 itemIndex = (int16_t)(topItem + row + 1);
			uint16_t rawOffset =
				exe_readU16((uint16_t)(listOffset + itemIndex * 2));
			uint16_t textOffset;

			if (rawOffset < 0x8000) {
				textOffset = (uint16_t)(UI_MESSAGES + rawOffset);
			} else {
				int16_t negIndex = (int16_t)rawOffset;

				textOffset = exe_readU16(
					(uint16_t)(extraTexts + (-negIndex - 1) * 2)
				);
			}

			int16_t indent = pIndents != NULL ? pIndents[itemIndex] : 0;
			int16_t rowLeft = ui__field(rowElem, UI_LEFT);
			int16_t rowTop = ui__field(rowElem, UI_TOP);
			int16_t rowRight = ui__field(rowElem, UI_RIGHT);

			text_draw(
				surface,
				(int16_t)(rowLeft + indent),
				rowTop,
				exe_text(textOffset)
			);
			screen_fill(
				surface,
				(int16_t)(globals_textEndX - 1),
				globals_textY,
				(int16_t)(rowRight - globals_textEndX + 1),
				TEXT_ROWS
			);

			if (pPercents != NULL &&
				pPercents[itemIndex] != UI_LIST_NO_PERCENT) {
				uint16_t pctOffset = ui__formatPercent(pPercents[itemIndex]);
				int16_t	 pctChars =
					(int16_t)strlen(exe_text(UI_LIST_PERCENT_BUF));

				text_draw(
					surface,
					(int16_t)(rowRight - pctChars * UI_INPUT_COLUMNS),
					rowTop,
					exe_text(pctOffset)
				);
			}

			rowElem = (uint16_t)(rowElem + UI_ELEMENT_SIZE);
		}

		if (globals_screenIsPlain == 0) {
			uint16_t selElem = ui__listItem(selectedRow);

			ui_setStyle(ui__field(selElem, UI_STYLE));
			screen_box(
				surface,
				ui__field(selElem, UI_LEFT),
				ui__field(selElem, UI_TOP),
				itemWidth,
				TEXT_ROWS
			);
		}

		cursor_set(CURSOR_SHOW);

		if (globals_drawThrough != 0) {
			screen_copyToDisplay(
				(int16_t)(listLeft - 1),
				(int16_t)(listTop - 1),
				(int16_t)(listLeft + itemWidth),
				(int16_t)(listTop + visibleRows * itemHeight)
			);
		}

		int16_t result;

		for (;;) {
			ui_setStyle(itemStyle);

			while ((result = event_wait(0)) == 0) {
				ui__updateListHover(
					screen,
					topItem,
					count,
					hoverColor,
					&hoveredRow
				);
			}

			if (result < 1) {
				break;
			}

			int16_t clickedItem = (int16_t)(topItem + result);

			if (clickedItem <= count) {
				ui__listSelection = clickedItem;

				int16_t newRow = (int16_t)(clickedItem - 1);

				if (inputElement != 0 &&
					(globals_inputIsLeft == 0 || newRow == selectedRow)) {
					ui__fillInputFromList(
						screen,
						inputElement,
						pInputText,
						listOffset,
						pPercents,
						clickedItem
					);
				}

				if (globals_screenIsPlain == 0) {
					ui__moveListSelection(
						selectedRow,
						newRow,
						itemStyle,
						itemWidth,
						itemHeight
					);
				}

				selectedRow = newRow;
			}
		}

		if (result == UI_LIST_SCROLL_DOWN) {
			if (topItem + 1 < count) {
				topItem++;
			}
		} else if (result == UI_LIST_PAGE_DOWN) {
			topItem = (int16_t)(topItem + visibleRows);

			if (topItem >= count) {
				topItem = (int16_t)(count - 1);
			}
		} else if (result == UI_LIST_PAGE_UP) {
			topItem = (int16_t)(topItem - visibleRows);

			if (topItem < 0) {
				topItem = 0;
			}
		} else if (result == UI_LIST_SCROLL_UP) {
			if (topItem != 0) {
				topItem--;
			}
		} else if (result == UI_LIST_EDIT_KEY) {
			if (screen == 5 && strcmp(pInputText, exe_text(0x4607)) == 0) {
				strcpy(pInputText, exe_text(0x7c4));
			}

			ui_setStyle(ui__field(inputElement, UI_STYLE));
			ui_open(0x18);
			ui_textInput(
				ui__field(inputElement, UI_LEFT),
				ui__field(inputElement, UI_TOP),
				pInputText,
				inputMaxLen
			);
			ui_close(1);
		} else {
			ui_close(1);

			return result;
		}

		if (prevTopItem < topItem && count - topItem < visibleRows) {
			int16_t	 remaining = (int16_t)(count - topItem);
			uint16_t clearElem = ui__listItem(remaining);
			int16_t	 clearRows = (int16_t)(topItem - prevTopItem);

			if (visibleRows - remaining < clearRows) {
				clearRows = (int16_t)(visibleRows - remaining);
			}

			screen_fill(
				surface,
				ui__field(clearElem, UI_LEFT),
				ui__field(clearElem, UI_TOP),
				itemWidth,
				(int16_t)(clearRows * itemHeight)
			);
		}
	}
}

/** ****************************************************************************
 *
 * \brief	The element record of a row of the list on screen.
 *
 * \param	row				Row, 0 for the top one.
 *
 * \return	DS offset of the element record.
 *
 ******************************************************************************/
static uint16_t ui__listItem(
	int16_t row
) {
	return (uint16_t)(globals_listFirst + row * UI_ELEMENT_SIZE);
}

/** ****************************************************************************
 *
 * \brief	Get the 1-based index of the item selected last in ui_list.
 *
 * \return	1-based item index.
 *
 * \details	Port of FUN_1f38_590b.
 *
 ******************************************************************************/
int16_t ui_listSelection(void) {
	return ui__listSelection;
}

/** ****************************************************************************
 *
 * \brief	Let the player type a number into the active screen's input
 * 			line element.
 *
 * \param	pValue			Out: the parsed 16-bit integer value.
 * \param	pText			In, out: text buffer for the typed digits.
 *
 * \return	The result of ui_textInput, with positive results turned into 0.
 *
 * \details	Port of FUN_1f38_5da7.
 *
 ******************************************************************************/
int16_t ui_numberInput(
	uint16_t * pValue,
	char *	   pText
) {
	uint16_t element = globals_inputElement;

	ui_setStyle(ui__field(element, UI_STYLE));
	globals_numericInput = 1;

	int16_t width =
		(int16_t)(ui__field(element, UI_RIGHT) - ui__field(element, UI_LEFT));
	int16_t maxLength = (int16_t)(width / UI_INPUT_COLUMNS);

	if (maxLength > UI_NUMBER_MAX_DIGITS) {
		maxLength = UI_NUMBER_MAX_DIGITS;
	}

	int16_t result = ui_textInput(
		ui__field(element, UI_LEFT),
		ui__field(element, UI_TOP),
		pText,
		maxLength
	);

	globals_numericInput = 0;
	*pValue = (uint16_t)atoi(pText);

	return result > 0 ? 0 : result;
}
