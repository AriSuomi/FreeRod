/** ****************************************************************************
 *
 * \file	exe.h
 *
 * \brief	Initialized data of the original program, read from Sr.exe.
 *
 * \details	The game's tables (screen layouts, texts, key maps, font) are
 * 			part of Sr.exe, which is copyrighted, so the port does not
 * 			contain them: it unpacks Sr.exe at start and keeps a copy of
 * 			the data segment. Tables and texts are read from that copy by
 * 			their DS offset, and the blocks of hot_data are loaded into it
 * 			as the original did. Variables the port has named live in
 * 			globals.c instead.
 *
 ******************************************************************************/

#ifndef GAME_EXE_H_INCLUDED
#define GAME_EXE_H_INCLUDED

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <stdint.h>

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define EXE_FONT_GLYPHS 0x5b /**< Characters 0x20..0x7a. */
#define EXE_GLYPH_BYTES 9	 /**< Width, then 8 rows of pixels. */

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

bool exe_load(void);

void exe_unload(void);

uint8_t * exe_data(
	uint16_t offset
);

const uint8_t * exe_far(
	uint16_t segment,
	uint16_t offset,
	uint16_t length
);

const char * exe_text(
	uint16_t offset
);

uint16_t exe_readU16(
	uint16_t offset
);

int16_t exe_readS16(
	uint16_t offset
);

void exe_writeU16(
	uint16_t offset,
	uint16_t value
);

uint32_t exe_readU32(
	uint16_t offset
);

void exe_writeU32(
	uint16_t offset,
	uint32_t value
);

const uint8_t * exe_glyph(
	char character
);

#endif // GAME_EXE_H_INCLUDED
