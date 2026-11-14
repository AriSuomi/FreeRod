/** ****************************************************************************
 *
 * \file	save.c
 *
 * \brief	Saved games: the slot lists, saving and loading.
 *
 * \details	A saved game (HOTROD1.SAV .. HOTROD15.SAV) holds, in this
 * 			order: the 36 bytes of state from DS:0x7e9a (the player's
 * 			name, money, list heads and counts), the car in use and then
 * 			the stored cars, each as its 40-byte record followed by the
 * 			8-byte records of its parts (transmission, engine, manifold,
 * 			3 carburetors, tires; each slot a chain), the spare parts,
 * 			the 8 bytes at DS:0x4970, the 396 bytes of ads (DS:0x7ff8),
 * 			the game clock (u32), the BIOS ticks played (u32) and the
 * 			word DS:0x5e0c. Near pointers are written as they are; on
 * 			loading a non-zero pointer only says that a record follows,
 * 			and the records get new places in the record tables.
 * 			Numbers are little-endian, as on the PC.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/save.h"

#include <stdbool.h>
#include <stdio.h> /* snprintf only; no file access or printing */
#include <string.h>

#include "game/cars.h"
#include "game/cursor.h"
#include "game/exe.h"
#include "game/game.h"
#include "game/globals.h"
#include "game/road.h"
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

#define SAVE_SLOTS			15	   /**< Save files HOTROD1..15.SAV. */
#define SAVE_SLOT_TABLE		0x4fc2 /**< DS: u8 per slot (from 1): n or 0. */
#define SAVE_SLOT_TEXTS		0x4fd4 /**< DS: 15 offsets of slot name lines. */
#define SAVE_EMPTY_NAME		0x4607 /**< DS: name line of an empty slot. */
#define SAVE_NAME_LENGTH	15	   /**< Characters of a name line. */
#define SAVE_LIST			0x49e0 /**< DS: list count and items. */
#define SAVE_MESSAGES		0x2c02 /**< DS: list texts count from. */
#define SAVE_FILE_PREFIX	0x4ff7 /**< DS: "HOTROD" (after the ':'). */
#define SAVE_FILE_SUFFIX	0x4ffe /**< DS: ".SAV". */
#define SAVE_TYPED_NAME		0x2a2e /**< DS: name line of the save list. */
#define SAVE_ALERT_TEXT		0x291f /**< DS: text of the alert screen. */
#define SAVE_NO_NAME_TEXT	0x4ff2 /**< DS: pointer to "type a name" text. */
#define SAVE_HEADER_SIZE	0x24   /**< Bytes of state at DS:0x7e9a. */
#define SAVE_EXTRA			0x4970 /**< DS: 8 bytes saved after the parts. */
#define SAVE_EXTRA_SIZE		8	   /**< Bytes at DS:0x4970. */
#define SAVE_ADS_SIZE		0x18c  /**< Bytes of ads at DS:0x7ff8. */
#define SAVE_ROAD_SEED		0x5e0c /**< DS: word saved last (road data). */
#define SAVE_MAX_SIZE		4096   /**< Bytes a saved game has at most. */
#define SAVE_SLOT_SPARE		6	   /**< Car record: first part slot. */
#define SAVE_SLOT_FITTED	8	   /**< Car record: 5 more part slots. */
#define SAVE_FITTED_SLOTS	5	   /**< Slots from +8. */
#define SAVE_CANCEL			-4	   /**< List result: leave. */
#define SAVE_CHOSEN			-3	   /**< List result: the selected item. */
#define SAVE_ANSWER_YES		-20	   /**< Overwrite screen: yes. */
#define SAVE_ANSWER_NO		-21	   /**< Overwrite screen: no. */
#define SAVE_LOAD_SCREEN	3	   /**< List of saved games to load. */
#define SAVE_SAVE_SCREEN	5	   /**< List of slots to save in. */
#define SAVE_FAILED_SCREEN	6	   /**< Saving failed. */
#define SAVE_NONE_SCREEN	0xf	   /**< No saved games. */
#define SAVE_ALERT_SCREEN	0x1d   /**< Alert with a text. */
#define SAVE_REPLACE_SCREEN 0x1e   /**< Replace the saved game? */
#define SAVE_BAD_SCREEN		0x1f   /**< Loading failed. */
#define SAVE_BACKGROUND		0x3f7  /**< Resource behind the lists. */
#define SAVE_SAVE_PICTURE	0x3f9  /**< Resource of the save screen. */
#define SAVE_LOAD_PICTURE	0x3fa  /**< Resource of the load screen. */
#define SAVE_DISK_SAVE		0x3fb  /**< Disk picture of the save screen. */
#define SAVE_DISK			0x3fc  /**< Disk picture of both screens. */
#define SAVE_ELEMENT_SIZE	18	   /**< Bytes of an element record. */
#define SAVE_ELEMENT_STYLE	2	   /**< Element field: style. */
#define SAVE_ELEMENT_LEFT	8	   /**< Element field: left edge. */
#define SAVE_ELEMENT_TOP	10	   /**< Element field: top edge. */
#define SAVE_ELEMENT_RIGHT	12	   /**< Element field: right edge. */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/**
 * A saved game being written or read, in memory.
 */
typedef struct save__stream_tag {
	uint8_t * pData;	/**< The bytes. */
	size_t	  size;		/**< Bytes there are (reading) or room (writing). */
	size_t	  position; /**< Bytes written or read so far. */
} save__Stream;

/*******************************************************************************
;
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

static void save__fileName(
	int16_t slot,
	char *	pName,
	size_t	size
);

static bool save__isBlank(
	const char * pText
);

static void save__alert(
	const char * pText
);

static void save__unmark(
	int16_t row
);

static bool save__put(
	save__Stream *	pStream,
	const uint8_t * pBytes,
	size_t			count
);

static bool save__get(
	save__Stream * pStream,
	uint8_t *	   pBytes,
	size_t		   count
);

static bool save__putParts(
	save__Stream * pStream,
	uint16_t	   part
);

static bool save__putCars(
	save__Stream * pStream,
	uint16_t	   car,
	bool		   single
);

static bool save__getParts(
	save__Stream * pStream,
	uint8_t *	   pSlot
);

static bool save__getCars(
	save__Stream * pStream,
	uint16_t	   listAt,
	bool		   single
);

static bool save__write(
	const char * pName
);

static bool save__read(
	const char * pName,
	bool *		 pOpened
);

static uint16_t save__word(
	const uint8_t * pBytes
);

static void save__setWord(
	uint8_t * pBytes,
	uint16_t  value
);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/**
 * The saved game being written or read (too large for small stacks).
 */
static uint8_t save__buffer[SAVE_MAX_SIZE];

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Find the saved games and list them.
 *
 * \param	withEmpty		Not 0: list all 15 slots, the empty ones with
 * 							the name line "_______________".
 *
 * \details	Port of ScanSaveSlots (1000:59f6). A slot holds a game when
 * 			its file has the 36-byte header; its byte in DS:0x4fc2 is
 * 			then its number (else 0), and the name from the header (15
 * 			characters) goes into the slot's name line (table DS:0x4fd4,
 * 			offsets from DS:0x2c02). The list at DS:0x49e0 gets the name
 * 			lines.
 *
 ******************************************************************************/
void save_scanSlots(
	int16_t withEmpty
) {
	int16_t count = 0;

	for (int16_t slot = 1; slot <= SAVE_SLOTS; slot++) {
		uint16_t lineAt =
			exe_readU16((uint16_t)(SAVE_SLOT_TEXTS + (slot - 1) * 2));
		char *	  pLine = (char *)exe_data((uint16_t)(SAVE_MESSAGES + lineAt));
		uint8_t * pMark = exe_data((uint16_t)(SAVE_SLOT_TABLE + slot));
		char	  name[16];
		size_t	  size = 0;

		save__fileName(slot, name, sizeof name);

		uint8_t * pFile = plat_readSave(name, &size);

		*pMark = 0;

		if (pFile && size >= SAVE_HEADER_SIZE) {
			*pMark = (uint8_t)slot;
			count++;
			exe_writeU16((uint16_t)(SAVE_LIST + count * 2), lineAt);
			strncpy(pLine, (const char *)pFile, SAVE_NAME_LENGTH);
		}

		plat_freeFile(pFile);

		if (withEmpty != 0 && *pMark == 0) {
			count++;
			exe_writeU16((uint16_t)(SAVE_LIST + count * 2), lineAt);
			strcpy(pLine, exe_text(SAVE_EMPTY_NAME));
		}
	}

	exe_writeU16(SAVE_LIST, (uint16_t)count);
}

/** ****************************************************************************
 *
 * \brief	Let the player pick a saved game and load it.
 *
 * \return	0: a game was loaded. -1: there is none, or the player left
 * 			the list. GAME_CANCELLED (-666): the file could not be read;
 * 			when it was there, a new game has been set up instead (the
 * 			state may be partly overwritten).
 *
 * \details	Port of LoadGame (1000:5b4a). Resource 0x3f7 is drawn on the
 * 			work surface (with the disks 0x3fa, 0x3fc when there are
 * 			games) and copied to the display; without games screen 15
 * 			says so. The list is screen 3. After loading, the car paint
 * 			is set and the BIOS start time is moved back by the ticks
 * 			played; the road is laid along the saved route.
 *
 ******************************************************************************/
int16_t save_load(void) {
	cursor_set(CURSOR_HIDE_NOW);
	save_scanSlots(0);

	int16_t drawThrough = globals_drawThrough;
	int16_t count = exe_readS16(SAVE_LIST);

	globals_drawThrough = 0;
	screen_drawResource(SAVE_BACKGROUND, 0);

	if (count != 0) {
		screen_drawResourceAt(SAVE_LOAD_PICTURE, 0xf0, 100, -1);
		screen_drawResourceAt(SAVE_DISK, 0xf0, 0x8c, -1);
	}

	globals_drawThrough = drawThrough;

	if (drawThrough != 0) {
		screen_copyToDisplay(0, 0, PLAT_WIDTH, UI_STATUS_ROW);
	}

	cursor_set(CURSOR_SHOW_NOW);

	if (count == 0) {
		ui_run(SAVE_NONE_SCREEN);
		return -1;
	}

	int16_t choice =
		ui_runList(SAVE_LOAD_SCREEN, 1, NULL, SAVE_LIST, NULL, 0);

	if (choice == SAVE_CANCEL) {
		return -1;
	}

	if (choice == SAVE_CHOSEN) {
		choice = ui_listSelection();
	}

	/* The choice counts the slots that hold a game. */
	int16_t slot = 1;
	int16_t found = 0;

	while (found < choice) {
		if (*exe_data((uint16_t)(SAVE_SLOT_TABLE + slot)) != 0) {
			found++;
		}

		slot++;
	}

	char name[16];

	cursor_set(CURSOR_HIDE_NOW);
	save__fileName(
		(int8_t)*exe_data((uint16_t)(SAVE_SLOT_TABLE + slot - 1)),
		name,
		sizeof name
	);

	bool opened;

	if (!save__read(name, &opened)) {
		cursor_set(CURSOR_SHOW_NOW);
		ui_run(SAVE_BAD_SCREEN);
		cursor_set(CURSOR_HIDE);

		if (opened) {
			game_initNewGame();
		}

		cursor_set(CURSOR_SHOW);

		return GAME_CANCELLED;
	}

	cursor_set(CURSOR_SHOW_NOW);

	return 0;
}

/** ****************************************************************************
 *
 * \brief	Let the player pick a slot and save the game.
 *
 * \details	Port of FUN_1000_5eea. Resource 0x3f7 with 0x3f9, 0x3fb and
 * 			0x3fc is the background of screen 5: the 15 slots and a name
 * 			line that starts with the player's name. The first empty slot
 * 			is selected. A slot needs a name (typed, or the slot's own);
 * 			else an alert says so (text at the pointer DS:0x4ff2). When
 * 			the typed name is the name of a slot, that slot is used (the
 * 			mark of the selected row is taken away). Replacing a saved
 * 			game asks first (screen 0x1e; "No" chooses again). The name
 * 			becomes the player's name. When writing fails, screen 6 says
 * 			so and the player chooses again; after saving, the function
 * 			ends.
 *
 ******************************************************************************/
void save_save(void) {
	int16_t selection = 0;

	cursor_set(CURSOR_HIDE_NOW);
	save_scanSlots(1);

	int16_t drawThrough = globals_drawThrough;

	globals_drawThrough = 0;
	screen_drawResource(SAVE_BACKGROUND, 0);
	screen_drawResourceAt(SAVE_SAVE_PICTURE, 0x48, 0xa9, -1);
	screen_drawResourceAt(SAVE_DISK_SAVE, 0xf0, 100, -1);
	screen_drawResourceAt(SAVE_DISK, 0xf0, 0x8c, -1);
	globals_drawThrough = drawThrough;

	if (drawThrough != 0) {
		screen_copyToDisplay(0, 0, PLAT_WIDTH, UI_STATUS_ROW);
	}

	char * pTyped = (char *)exe_data(SAVE_TYPED_NAME);

	strcpy(pTyped, exe_text(STATE_PLAYER_NAME));

	for (;;) {
		int16_t slot;
		int16_t answer;

		do {
			int16_t count = exe_readS16(SAVE_LIST);
			int16_t chosen;

			for (;;) {
				int16_t firstEmpty = 1;

				while (firstEmpty <= count &&
					   *exe_data((uint16_t)(SAVE_SLOT_TABLE + firstEmpty)) !=
						   0) {
					firstEmpty++;
				}

				if (firstEmpty > count) {
					firstEmpty = count;
				}

				if (selection == 0) {
					selection = firstEmpty;
				}

				cursor_set(CURSOR_SHOW_NOW);
				chosen = ui_list(
					SAVE_SAVE_SCREEN,
					selection,
					NULL,
					SAVE_LIST,
					NULL,
					0
				);

				if (chosen == SAVE_CANCEL) {
					return;
				}

				if (chosen == SAVE_CHOSEN) {
					chosen = ui_listSelection();
				} else if (chosen < 1 || chosen > count) {
					return;
				}

				selection = chosen;

				uint16_t lineAt =
					exe_readU16((uint16_t)(SAVE_LIST + chosen * 2));
				const char * pLine =
					exe_text((uint16_t)(SAVE_MESSAGES + lineAt));

				if (!save__isBlank(pTyped) || !save__isBlank(pLine)) {
					break;
				}

				save__alert(exe_text(exe_readU16(SAVE_NO_NAME_TEXT)));
				cursor_set(CURSOR_HIDE_NOW);
			}

			/* A slot with the typed name is used instead. */
			for (int16_t index = 1; index <= SAVE_SLOTS; index++) {
				uint16_t lineAt =
					exe_readU16((uint16_t)(SAVE_LIST + index * 2));

				if (strcmp(
						exe_text((uint16_t)(SAVE_MESSAGES + lineAt)),
						pTyped
					) == 0) {
					save__unmark(chosen);
					chosen = index;
					break;
				}
			}

			slot = chosen;
			answer = 0;

			if (*exe_data((uint16_t)(SAVE_SLOT_TABLE + slot)) != 0) {
				answer = ui_runModal(SAVE_REPLACE_SCREEN);
			}

			cursor_set(CURSOR_HIDE_NOW);
			selection = slot;
		} while (answer == SAVE_ANSWER_NO);

		char name[16];

		save__fileName(slot, name, sizeof name);

		if (answer == SAVE_ANSWER_YES) {
			plat_deleteSave(name);
		}

		const char * pNewName = pTyped;

		if (save__isBlank(pTyped)) {
			uint16_t lineAt = exe_readU16((uint16_t)(SAVE_LIST + slot * 2));

			pNewName = exe_text((uint16_t)(SAVE_MESSAGES + lineAt));
		}

		strcpy((char *)exe_data(STATE_PLAYER_NAME), pNewName);

		if (save__write(name)) {
			cursor_set(CURSOR_SHOW_NOW);
			return;
		}

		cursor_set(CURSOR_SHOW_NOW);
		ui_runModal(SAVE_FAILED_SCREEN);
		cursor_set(CURSOR_HIDE_NOW);
		plat_deleteSave(name);
		save_scanSlots(1);
	}
}

/** ****************************************************************************
 *
 * \brief	The file name of a save slot.
 *
 * \param	slot			Slot, 1..15.
 * \param	pName			Output: "HOTROD<slot>.SAV".
 * \param	size			Bytes of pName.
 *
 * \details	The original puts the data drive in front (":HOTROD" at
 * 			DS:0x4ff6 and its copies); the port's platform has a folder
 * 			for saved games instead.
 *
 ******************************************************************************/
static void save__fileName(
	int16_t slot,
	char *	pName,
	size_t	size
) {
	snprintf(
		pName,
		size,
		"%s%d%s",
		exe_text(SAVE_FILE_PREFIX),
		slot,
		exe_text(SAVE_FILE_SUFFIX)
	);
}

/** ****************************************************************************
 *
 * \brief	Tell whether a name line has no name.
 *
 * \param	pText			The text.
 *
 * \retval	true	It has only spaces and underscores (or nothing).
 * \retval	false	It has another character.
 *
 * \details	Port of FUN_1000_5eb3.
 *
 ******************************************************************************/
static bool save__isBlank(
	const char * pText
) {
	for (; *pText != '\0'; pText++) {
		if (*pText != ' ' && *pText != '_') {
			return false;
		}
	}

	return true;
}

/** ****************************************************************************
 *
 * \brief	Show a text in the alert box until the player closes it.
 *
 * \param	pText			The text.
 *
 * \details	Port of FUN_1f38_239c: the text is copied to DS:0x291f, the
 * 			text of screen 0x1d.
 *
 ******************************************************************************/
static void save__alert(
	const char * pText
) {
	strcpy((char *)exe_data(SAVE_ALERT_TEXT), pText);
	ui_runModal(SAVE_ALERT_SCREEN);
}

/** ****************************************************************************
 *
 * \brief	Take the selection mark away from a row of the save list.
 *
 * \param	row				Selected item (from 1).
 *
 * \details	Part of FUN_1000_5eea (1000:6159..61e5): a box in the color
 * 			behind the text, in the style of the list's first row, is
 * 			drawn around the row on the work surface and the display.
 *
 ******************************************************************************/
static void save__unmark(
	int16_t row
) {
	uint16_t first = globals_listFirst;
	uint16_t element = (uint16_t)(first + (row - 1) * SAVE_ELEMENT_SIZE);
	int16_t	 left = exe_readS16((uint16_t)(element + SAVE_ELEMENT_LEFT));
	int16_t	 top = exe_readS16((uint16_t)(element + SAVE_ELEMENT_TOP));
	int16_t	 firstLeft = exe_readS16((uint16_t)(first + SAVE_ELEMENT_LEFT));
	int16_t	 firstRight = exe_readS16((uint16_t)(first + SAVE_ELEMENT_RIGHT));
	int16_t	 width = (int16_t)(firstRight - firstLeft);
	uint8_t	 textColor = globals_textColor;
	uint8_t	 textBackground = globals_textBackground;
	uint8_t	 fillColor = globals_fillColor;
	uint8_t	 lineColor = globals_lineColor;
	uint8_t	 borderStyle = globals_borderStyle;

	cursor_set(CURSOR_HIDE);
	ui_setStyle(exe_readS16((uint16_t)(first + SAVE_ELEMENT_STYLE)));
	globals_lineColor = globals_textBackground;
	screen_box(globals_workSurface, left, top, width, TEXT_ROWS);
	screen_box(globals_displaySurface, left, top, width, TEXT_ROWS);
	globals_textColor = textColor;
	globals_textBackground = textBackground;
	globals_fillColor = fillColor;
	globals_lineColor = lineColor;
	globals_borderStyle = borderStyle;
	cursor_set(CURSOR_SHOW);
}

/** ****************************************************************************
 *
 * \brief	Add bytes to a saved game being written.
 *
 * \param	pStream			The saved game.
 * \param	pBytes			The bytes.
 * \param	count			How many.
 *
 * \retval	true	They were added.
 * \retval	false	There is no room (the write fails, as a full disk).
 *
 ******************************************************************************/
static bool save__put(
	save__Stream *	pStream,
	const uint8_t * pBytes,
	size_t			count
) {
	if (pStream->size - pStream->position < count) {
		return false;
	}

	memcpy(pStream->pData + pStream->position, pBytes, count);
	pStream->position += count;

	return true;
}

/** ****************************************************************************
 *
 * \brief	Take bytes from a saved game being read.
 *
 * \param	pStream			The saved game.
 * \param	pBytes			Output: the bytes.
 * \param	count			How many.
 *
 * \retval	true	They were there.
 * \retval	false	The file ends before (a short read).
 *
 ******************************************************************************/
static bool save__get(
	save__Stream * pStream,
	uint8_t *	   pBytes,
	size_t		   count
) {
	if (pStream->size - pStream->position < count) {
		return false;
	}

	memcpy(pBytes, pStream->pData + pStream->position, count);
	pStream->position += count;

	return true;
}

/** ****************************************************************************
 *
 * \brief	Write a chain of part records.
 *
 * \param	pStream			The saved game.
 * \param	part			DS offset of the first part record, or 0.
 *
 * \return	false when there was no room.
 *
 * \details	Port of FUN_1000_5795: each record (8 bytes) as it is,
 * 			following the links (+6).
 *
 ******************************************************************************/
static bool save__putParts(
	save__Stream * pStream,
	uint16_t	   part
) {
	while (part != 0) {
		if (!save__put(pStream, exe_data(part), STATE_PART_SIZE)) {
			return false;
		}

		part = exe_readU16((uint16_t)(part + STATE_PART_NEXT));
	}

	return true;
}

/** ****************************************************************************
 *
 * \brief	Write a chain of car records with their parts.
 *
 * \param	pStream			The saved game.
 * \param	car				DS offset of the first car record, or 0.
 * \param	single			true: only this car (the car in use).
 *
 * \return	false when there was no room.
 *
 * \details	Port of FUN_1000_586e: the record (40 bytes), then the part
 * 			chains of the slots +6, +8..+0x10 and +0x12.
 *
 ******************************************************************************/
static bool save__putCars(
	save__Stream * pStream,
	uint16_t	   car,
	bool		   single
) {
	while (car != 0) {
		if (!save__put(pStream, exe_data(car), STATE_CAR_SIZE)) {
			return false;
		}

		uint16_t spare = exe_readU16((uint16_t)(car + SAVE_SLOT_SPARE));

		if (!save__putParts(pStream, spare)) {
			return false;
		}

		for (int slot = 0; slot < SAVE_FITTED_SLOTS; slot++) {
			uint16_t slotAt = (uint16_t)(car + SAVE_SLOT_FITTED + slot * 2);

			if (!save__putParts(pStream, exe_readU16(slotAt))) {
				return false;
			}
		}

		uint16_t tires = exe_readU16((uint16_t)(car + STATE_CAR_TIRES));

		if (!save__putParts(pStream, tires)) {
			return false;
		}

		if (single) {
			return true;
		}

		car = exe_readU16((uint16_t)(car + STATE_CAR_NEXT));
	}

	return true;
}

/** ****************************************************************************
 *
 * \brief	Read a chain of part records into new records.
 *
 * \param	pStream			The saved game.
 * \param	pSlot			In: the saved pointer (0: no chain). Out: the
 * 							first new record. A little-endian word.
 *
 * \return	false when the file ended before (or no record was left).
 *
 * \details	Port of FUN_1000_57dc. Each record is copied into a record
 * 			from the free list; a saved link (+6) that is not 0 says that
 * 			another record follows. The port also stops when no record is
 * 			left, where the original would take a broken one.
 *
 ******************************************************************************/
static bool save__getParts(
	save__Stream * pStream,
	uint8_t *	   pSlot
) {
	uint16_t last = 0;

	if (save__word(pSlot) == 0) {
		return true;
	}

	save__setWord(pSlot, 0);

	for (;;) {
		uint8_t record[STATE_PART_SIZE];

		if (!save__get(pStream, record, sizeof record) ||
			exe_readU16(STATE_FREE_PARTS) == 0) {
			return false;
		}

		uint16_t part = cars_allocPart();

		memcpy(exe_data(part), record, sizeof record);

		if (last == 0) {
			save__setWord(pSlot, part);
		} else {
			exe_writeU16((uint16_t)(last + STATE_PART_NEXT), part);
		}

		last = part;

		if (exe_readU16((uint16_t)(part + STATE_PART_NEXT)) == 0) {
			return true;
		}

		exe_writeU16((uint16_t)(part + STATE_PART_NEXT), 0);
	}
}

/** ****************************************************************************
 *
 * \brief	Read a chain of car records with their parts.
 *
 * \param	pStream			The saved game.
 * \param	listAt			DS offset of the list head (the car in use or
 * 							the stored cars); its saved value 0 means no
 * 							cars.
 * \param	single			true: only one car (the car in use).
 *
 * \return	false when the file ended before (or no record was left).
 *
 * \details	Port of FUN_1000_590f. The part chains are read into the
 * 			slots of the record before it is copied into a record from
 * 			the free list. Like the original, a single car keeps its
 * 			saved link (+0x26).
 *
 ******************************************************************************/
static bool save__getCars(
	save__Stream * pStream,
	uint16_t	   listAt,
	bool		   single
) {
	uint16_t last = 0;

	if (exe_readU16(listAt) == 0) {
		return true;
	}

	exe_writeU16(listAt, 0);

	for (;;) {
		uint8_t record[STATE_CAR_SIZE];

		if (!save__get(pStream, record, sizeof record) ||
			!save__getParts(pStream, record + SAVE_SLOT_SPARE)) {
			return false;
		}

		for (int slot = 0; slot < SAVE_FITTED_SLOTS; slot++) {
			uint8_t * pSlot = record + SAVE_SLOT_FITTED + slot * 2;

			if (!save__getParts(pStream, pSlot)) {
				return false;
			}
		}

		if (!save__getParts(pStream, record + STATE_CAR_TIRES) ||
			exe_readU16(STATE_FREE_CARS) == 0) {
			return false;
		}

		uint16_t car = cars_allocCar();

		memcpy(exe_data(car), record, sizeof record);

		if (last == 0) {
			exe_writeU16(listAt, car);
		} else {
			exe_writeU16((uint16_t)(last + STATE_CAR_NEXT), car);
		}

		last = car;

		if (exe_readU16((uint16_t)(car + STATE_CAR_NEXT)) == 0 || single) {
			return true;
		}

		exe_writeU16((uint16_t)(car + STATE_CAR_NEXT), 0);
	}
}

/** ****************************************************************************
 *
 * \brief	Write the game into a save file.
 *
 * \param	pName			File name.
 *
 * \retval	true	Saved.
 * \retval	false	Writing failed.
 *
 * \details	Part of FUN_1000_5eea (1000:6263..6401): the parts of the
 * 			file in their order (see the file comment).
 *
 ******************************************************************************/
static bool save__write(
	const char * pName
) {
	save__Stream stream = { save__buffer, sizeof save__buffer, 0 };
	uint32_t	 played = globals_biosTicks - globals_biosTicksAtStart;
	uint8_t		 clocks[8];

	for (int index = 0; index < 4; index++) {
		clocks[index] = (uint8_t)(globals_gameTime >> (index * 8));
		clocks[4 + index] = (uint8_t)(played >> (index * 8));
	}

	bool complete =
		save__put(&stream, exe_data(STATE_PLAYER_NAME), SAVE_HEADER_SIZE) &&
		save__putCars(&stream, exe_readU16(STATE_CAR), true) &&
		save__putCars(&stream, exe_readU16(STATE_STORED_CARS), false) &&
		save__putParts(&stream, exe_readU16(STATE_SPARE_PARTS)) &&
		save__put(&stream, exe_data(SAVE_EXTRA), SAVE_EXTRA_SIZE) &&
		save__put(&stream, exe_data(STATE_ADS), SAVE_ADS_SIZE) &&
		save__put(&stream, clocks, sizeof clocks) &&
		save__put(&stream, exe_data(SAVE_ROAD_SEED), 2);

	return complete && plat_writeSave(pName, stream.pData, stream.position);
}

/** ****************************************************************************
 *
 * \brief	Read a save file into the game.
 *
 * \param	pName			File name.
 * \param	pOpened			Output: whether the file was there.
 *
 * \retval	true	Loaded.
 * \retval	false	The file is missing or too short; the game state may
 * 					be partly overwritten.
 *
 * \details	Part of LoadGame (1000:5cdf..5e9b): the header, then the
 * 			record tables are reset (FUN_1000_3614(0)) and the cars and
 * 			parts read into new records, then the rest. The car paint is
 * 			set and the BIOS start time moved back by the ticks played.
 *
 ******************************************************************************/
static bool save__read(
	const char * pName,
	bool *		 pOpened
) {
	size_t	  size = 0;
	uint8_t * pFile = plat_readSave(pName, &size);

	*pOpened = pFile != NULL;

	if (!pFile) {
		return false;
	}

	save__Stream stream = { pFile, size, 0 };
	uint8_t		 clocks[8];
	bool		 complete =
		save__get(&stream, exe_data(STATE_PLAYER_NAME), SAVE_HEADER_SIZE);

	if (complete) {
		game_initRecords(0);
		complete = save__getCars(&stream, STATE_CAR, true) &&
			save__getCars(&stream, STATE_STORED_CARS, false) &&
			save__getParts(&stream, exe_data(STATE_SPARE_PARTS)) &&
			save__get(&stream, exe_data(SAVE_EXTRA), SAVE_EXTRA_SIZE) &&
			save__get(&stream, exe_data(STATE_ADS), SAVE_ADS_SIZE) &&
			save__get(&stream, clocks, sizeof clocks) &&
			save__get(&stream, exe_data(SAVE_ROAD_SEED), 2);
	}

	plat_freeFile(pFile);

	if (!complete) {
		return false;
	}

	uint32_t played = 0;

	globals_gameTime = 0;

	for (int index = 3; index >= 0; index--) {
		globals_gameTime = (globals_gameTime << 8) | clocks[index];
		played = (played << 8) | clocks[4 + index];
	}

	uint16_t car = exe_readU16(STATE_CAR);

	if (car != 0) {
		cars_setPaint((int8_t)*exe_data((uint16_t)(car + STATE_CAR_PAINT)));
	}

	road_build(exe_readS16(SAVE_ROAD_SEED));
	globals_biosTicksAtStart = globals_biosTicks - played;

	return true;
}

/** ****************************************************************************
 *
 * \brief	Read a little-endian word.
 *
 * \param	pBytes			Its two bytes.
 *
 * \return	The word.
 *
 ******************************************************************************/
static uint16_t save__word(
	const uint8_t * pBytes
) {
	return (uint16_t)(pBytes[0] | (pBytes[1] << 8));
}

/** ****************************************************************************
 *
 * \brief	Write a little-endian word.
 *
 * \param	pBytes			Its two bytes.
 * \param	value			The word.
 *
 ******************************************************************************/
static void save__setWord(
	uint8_t * pBytes,
	uint16_t  value
) {
	pBytes[0] = (uint8_t)value;
	pBytes[1] = (uint8_t)(value >> 8);
}
