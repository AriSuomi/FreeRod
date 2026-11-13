/** ****************************************************************************
 *
 * \file	lib.h
 *
 * \brief	Graphics libraries Lib1 and Lib2.
 *
 * \details	Port of OpenLibraries (1f38:6943), LibReadResource (1f38:62ba)
 * 			and LibDecode (1e92:0006). Format notes are in CLAUDE.md, "Data
 * 			formats".
 *
 * 			Resources are numbered across both files: ids
 * 			0..lib_count() - 1, Lib1 first, then Lib2.
 *
 ******************************************************************************/

#ifndef GAME_LIB_H_INCLUDED
#define GAME_LIB_H_INCLUDED

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

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/**
 * Resource kinds, from the flag byte of a library entry.
 */
enum {
	LIB_FLAG_IMAGE = 0,	 /**< Full image, 4 planes, opaque. */
	LIB_FLAG_SPRITE = 1, /**< 4 planes, color index 0 is transparent. */
	LIB_FLAG_MONO = 2,	 /**< 1 plane. */
};

/**
 * One resource in a graphics library.
 */
typedef struct lib_resource_tag {
	uint16_t		width;		  /**< Width in pixels. */
	uint16_t		height;		  /**< Height in pixels. */
	uint16_t		unpackedSize; /**< Size in bytes of all 4 planes. */
	uint8_t			flag;		  /**< LIB_FLAG_IMAGE, _SPRITE or _MONO. */
	uint8_t			tokenCount;	  /**< Number of tokens used. */
	uint8_t			tokens[16];	  /**< Bytes that stand for runs. */
	const uint8_t * pPacked;	  /**< First byte after the 0xBC marker. */
	uint16_t		packedSize;	  /**< Bytes at pPacked. */
	char			name[16];	  /**< "lib1/000", used for replacements. */
} lib_Resource;

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

bool lib_open(void);

void lib_close(void);

int lib_count(void);

const lib_Resource * lib_get(
	int id
);

int lib_indexFromGameId(
	int gameId
);

bool lib_decodePixels(
	int		  id,
	uint8_t * pPixels
);

#endif // GAME_LIB_H_INCLUDED
