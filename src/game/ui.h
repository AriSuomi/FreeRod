/** ****************************************************************************
 *
 * \file	ui.h
 *
 * \brief	Screens made of elements (boxes, texts, buttons, hotspots) and
 * 			the status line.
 *
 * \details	The game describes its menus and dialogs with 18-byte element
 * 			records at DS:0x9f4 (read from Sr.exe, see exe.h):
 *
 * 			+0	type: 1..3 screen header, 4/12 box with title, 5 text,
 * 				6 centered text, 7/8 input line, 9 end of a list, 10
 * 				button, 11 hotspot, 13 list item, 14 skipped
 * 			+2	style (index into the style table) or, for a header, 0
 * 			+4	text, as an offset from DS:0x239e; header: unknown value
 * 			+6	number returned when the element is clicked; type 12:
 * 				index of the element after its list
 * 			+8	left, +10 top, +12 right, +14 bottom (right and bottom
 * 				excluded)
 * 			+16	index of the next element to draw, 0 = none
 *
 * 			A screen number selects a header through the table at
 * 			DS:0x2bad. The header's +16 starts the chain of elements to
 * 			draw; the records after the header are the screen's hotspots
 * 			(types 10 and up), ended by a record of a lower type.
 *
 ******************************************************************************/

#ifndef GAME_UI_H_INCLUDED
#define GAME_UI_H_INCLUDED

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include <stdint.h>

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define UI_LIST_EMPTY 0x3ea /**< ui_list / ui_runList: list is empty. */

#define UI_STATUS_ROW 190 /**< First row of the status line. */

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
;	F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

void ui_setStyle(
	int16_t style
);

void ui_statusInit(
	int16_t row
);

void ui_status(
	int16_t		 left,
	const char * pText,
	int16_t		 width
);

void ui_drawElements(
	uint16_t element
);

void ui_open(
	int16_t screen
);

void ui_close(
	int16_t reopen
);

void ui_reset(void);

void ui_sortElements(
	int16_t first,
	int16_t count
);

int16_t ui_hitTest(
	int16_t column,
	int16_t row
);

int16_t ui_run(
	int16_t screen
);

int16_t ui_textInput(
	int16_t left,
	int16_t top,
	char *	pText,
	int16_t maxLength
);

int16_t ui_inputScreen(
	int16_t screen,
	int16_t maxLength,
	char *	pText
);

void ui_statusTitle(
	const char * pTitle
);

void ui_addMoney(
	int16_t amount
);

void ui_statusGear(
	int16_t index
);

void ui_message(
	int16_t message
);

void ui_bubbleAt(
	int16_t message,
	int16_t left,
	int16_t top
);

void ui_messageClose(void);

void ui_bounds(
	int16_t	  screen,
	int16_t * pLeft,
	int16_t * pTop,
	int16_t * pWidth,
	int16_t * pHeight
);

int ui_saveScreenRect(
	int16_t screen
);

int16_t ui_runModal(
	int16_t screen
);

int16_t ui_runList(
	int16_t			screen,
	int16_t			initialSelection,
	const int16_t * pIndents,
	uint16_t		listOffset,
	const int8_t *	pPercents,
	uint16_t		extraTexts
);

int16_t ui_list(
	int16_t			screen,
	int16_t			initialSelection,
	const int16_t * pIndents,
	uint16_t		listOffset,
	const int8_t *	pPercents,
	uint16_t		extraTexts
);

int16_t ui_listSelection(void);

int16_t ui_numberInput(
	uint16_t * pValue,
	char *	   pText
);

#endif // GAME_UI_H_INCLUDED
