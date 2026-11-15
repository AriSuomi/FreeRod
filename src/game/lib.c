/** ****************************************************************************
 *
 * \file	lib.c
 *
 * \brief	Graphics libraries Lib1 and Lib2.
 *
 * \details	Loads both library files, parses their entry tables and decodes
 * 			resources to indexed pixels.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include <stdio.h> /* snprintf only; no file access or printing */
#include <stdlib.h>
#include <string.h>

#include "game/lib.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define LIB_TOTAL_RESOURCES 277	 /**< OpenLibraries requires 0x115. */
#define LIB_ENTRY_SIZE		30	 /**< Bytes per library entry. */
#define LIB_MARKER			0xBC /**< First byte of each resource. */
#define LIB_FILE_COUNT		2	 /**< Lib1 and Lib2. */

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

static uint16_t lib__readU16(
	const uint8_t * pBytes
);

static uint32_t lib__readU32(
	const uint8_t * pBytes
);

static void lib__readTokens(
	lib_Resource *	pResource,
	const uint8_t * pEntry
);

static bool lib__parseLibrary(
	int				fileIndex,
	const uint8_t * pFile,
	size_t			size
);

static int lib__unpack(
	const lib_Resource * pResource,
	uint8_t *			 pOutput,
	int					 outputSize
);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/**
 * DS:0x4eee: run lengths for the 16 per-resource tokens.
 */
static const uint8_t lib__tokenLengths[16] = {
	1, 1, 2, 3, 4, 2, 3, 5, 6, 6, 4, 7, 7, 5, 8, 8
};

/*
 * Kept unformatted: clang-format would join the two rows into one long
 * line.
 */
/* clang-format off */
/**
 * DS:0x4efe: byte values for the 16 per-resource tokens.
 */
static const uint8_t lib__tokenValues[16] = {
	0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00,
	0xff, 0x00, 0xff, 0xff, 0x00, 0xff, 0xff, 0x00
};
/* clang-format on */

/**
 * Contents of Lib1 and Lib2, from plat_loadFile.
 */
static const uint8_t * lib__files[LIB_FILE_COUNT];

static lib_Resource lib__resources[LIB_TOTAL_RESOURCES]; /**< All resources. */
static int			lib__count;							 /**< Loaded. */

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Load both graphics libraries.
 *
 * \retval	true	Both libraries were loaded.
 * \retval	false	A file is missing or corrupt; the reason is logged.
 *
 * \details	The files Lib1 and Lib2 are loaded with plat_loadFile from the
 * 			platform's data folder.
 *
 ******************************************************************************/
bool lib_open(void) {
	static const char * const fileNames[LIB_FILE_COUNT] = { "Lib1", "Lib2" };

	lib_close();

	for (int index = 0; index < LIB_FILE_COUNT; index++) {
		size_t size = 0;

		lib__files[index] = plat_loadFile(fileNames[index], &size);

		if (!lib__files[index]) {
			lib_close();
			return false;
		}

		if (!lib__parseLibrary(index, lib__files[index], size)) {
			lib_close();
			return false;
		}
	}

	if (lib__count != LIB_TOTAL_RESOURCES) {
		plat_log(
			"libraries hold %d resources, expected %d",
			lib__count,
			LIB_TOTAL_RESOURCES
		);
		lib_close();
		return false;
	}

	return true;
}

/** ****************************************************************************
 *
 * \brief	Free both graphics libraries.
 *
 * \details	Resources returned by lib_get are invalid after this call.
 *
 ******************************************************************************/
void lib_close(void) {
	for (int index = 0; index < LIB_FILE_COUNT; index++) {
		plat_freeFile(lib__files[index]);
		lib__files[index] = NULL;
	}

	lib__count = 0;
}

/** ****************************************************************************
 *
 * \brief	Number of resources in both libraries.
 *
 * \return	The number of resources, 0 when the libraries are not loaded.
 *
 ******************************************************************************/
int lib_count(void) {
	return lib__count;
}

/** ****************************************************************************
 *
 * \brief	Get a resource.
 *
 * \param	id				Resource index, 0..lib_count() - 1.
 *
 * \return	Pointer to the resource. NULL if id is out of range.
 *
 ******************************************************************************/
const lib_Resource * lib_get(
	int id
) {
	bool valid = id >= 0 && id < lib__count;

	return valid ? &lib__resources[id] : NULL;
}

/** ****************************************************************************
 *
 * \brief	Convert the game's resource number to a resource index.
 *
 * \param	gameId			Resource number used by the original code.
 *
 * \return	Resource index, or -1 for an invalid number.
 *
 * \details	Port of LibIndexFromId (1f38:6057), 16-color path: numbers
 * 			1..9 are lib1/000..008 and 1000.. are lib2/000... The CGA
 * 			remap table (DS:0x7c6) is not needed because the port always
 * 			uses the 16-color libraries.
 *
 ******************************************************************************/
int lib_indexFromGameId(
	int gameId
) {
	int	 index = gameId < 1000 ? gameId - 1 : gameId - 0x3df;
	bool valid = index >= 0 && index < lib__count;

	return valid ? index : -1;
}

/** ****************************************************************************
 *
 * \brief	Decode a resource to indexed pixels.
 *
 * \param	id				Resource index.
 * \param	pPixels			Output, width * height bytes.
 *
 * \retval	true	The resource was decoded.
 * \retval	false	The id is invalid or the data is corrupt.
 *
 * \details	Each output byte is a color index 0..15. 1-plane images give 0
 * 			or 4 (white in the game palette).
 *
 ******************************************************************************/
bool lib_decodePixels(
	int		  id,
	uint8_t * pPixels
) {
	const lib_Resource * pResource = lib_get(id);

	if (!pResource) {
		return false;
	}

	int width = pResource->width;
	int height = pResource->height;
	int bytesPerRow = (width + 7) / 8;
	int planeSize = bytesPerRow * height;
	int planeCount = pResource->flag == LIB_FLAG_MONO ? 1 : 4;

	uint8_t * pPlanar = calloc((size_t)planeSize, 4);

	if (!pPlanar) {
		return false;
	}

	int decoded = lib__unpack(pResource, pPlanar, planeSize * 4);

	if (decoded != planeSize * planeCount) {
		plat_log(
			"%s: decoded %d bytes, expected %d",
			pResource->name,
			decoded,
			planeSize * planeCount
		);
		free(pPlanar);
		return false;
	}

	/*
	 * EGA planar: all rows of plane 0, then planes 1..3. Each plane is one
	 * bit of the color index.
	 */
	for (int row = 0; row < height; row++) {
		for (int column = 0; column < width; column++) {
			int		byteOffset = row * bytesPerRow + column / 8;
			int		shift = 7 - (column & 7);
			uint8_t color = 0;

			for (int plane = 0; plane < planeCount; plane++) {
				int bits = pPlanar[plane * planeSize + byteOffset] >> shift;

				color |= (uint8_t)((bits & 1) << plane);
			}

			pPixels[row * width + column] =
				planeCount == 1 ? (uint8_t)(color * 4) : color;
		}
	}

	free(pPlanar);

	return true;
}

/** ****************************************************************************
 *
 * \brief	Read a little-endian 16-bit value.
 *
 * \param	pBytes			The two bytes to read.
 *
 * \return	The value.
 *
 ******************************************************************************/
static uint16_t lib__readU16(
	const uint8_t * pBytes
) {
	return (uint16_t)(pBytes[0] | (pBytes[1] << 8));
}

/** ****************************************************************************
 *
 * \brief	Read a little-endian 32-bit value.
 *
 * \param	pBytes			The four bytes to read.
 *
 * \return	The value.
 *
 ******************************************************************************/
static uint32_t lib__readU32(
	const uint8_t * pBytes
) {
	uint32_t low = lib__readU16(pBytes);
	uint32_t high = lib__readU16(pBytes + 2);

	return low | (high << 16);
}

/** ****************************************************************************
 *
 * \brief	Copy the token bytes of a library entry.
 *
 * \param	pResource		Resource to fill in.
 * \param	pEntry			The 30-byte library entry.
 *
 * \details	The tokens are up to 16 bytes at offset 7, zero-terminated.
 *
 ******************************************************************************/
static void lib__readTokens(
	lib_Resource *	pResource,
	const uint8_t * pEntry
) {
	const uint8_t * pTokens = pEntry + 7;

	while (pResource->tokenCount < 16 && pTokens[pResource->tokenCount]) {
		pResource->tokens[pResource->tokenCount] =
			pTokens[pResource->tokenCount];
		pResource->tokenCount++;
	}
}

/** ****************************************************************************
 *
 * \brief	Parse the entry table of one library file.
 *
 * \param	fileIndex		0 for Lib1, 1 for Lib2.
 * \param	pFile			The file contents.
 * \param	size			The file size in bytes.
 *
 * \retval	true	All entries were added to lib__resources.
 * \retval	false	The file is corrupt; the reason is logged.
 *
 ******************************************************************************/
static bool lib__parseLibrary(
	int				fileIndex,
	const uint8_t * pFile,
	size_t			size
) {
	const char * pLibraryName = fileIndex == 0 ? "lib1" : "lib2";

	if (size < 2) {
		plat_log("%s: file too small", pLibraryName);
		return false;
	}

	int	   count = lib__readU16(pFile);
	size_t dataStart = 2 + (size_t)count * LIB_ENTRY_SIZE;

	if (dataStart > size || lib__count + count > LIB_TOTAL_RESOURCES) {
		plat_log("%s: bad resource count %d", pLibraryName, count);
		return false;
	}

	for (int index = 0; index < count; index++) {
		const uint8_t * pEntry = pFile + 2 + index * LIB_ENTRY_SIZE;
		lib_Resource *	pResource = &lib__resources[lib__count];

		memset(pResource, 0, sizeof *pResource);
		pResource->width = lib__readU16(pEntry);
		pResource->height = lib__readU16(pEntry + 2);
		pResource->unpackedSize = lib__readU16(pEntry + 4);
		pResource->flag = pEntry[6];
		lib__readTokens(pResource, pEntry);

		size_t	 start = dataStart + lib__readU32(pEntry + 24);
		uint16_t packedSize = lib__readU16(pEntry + 28);
		bool	 inFile = start + packedSize <= size;

		if (!inFile || packedSize == 0 || pFile[start] != LIB_MARKER) {
			plat_log(
				"%s/%03d: bad data offset",
				pLibraryName,
				index
			);
			return false;
		}

		pResource->pPacked = pFile + start + 1;
		pResource->packedSize = (uint16_t)(packedSize - 1);
		snprintf(
			pResource->name,
			sizeof pResource->name,
			"%s/%03d",
			pLibraryName,
			index
		);
		lib__count++;
	}

	return true;
}

/** ****************************************************************************
 *
 * \brief	Unpack the run-length data of a resource.
 *
 * \param	pResource		The resource.
 * \param	pOutput			Output buffer.
 * \param	outputSize		Size of the output buffer in bytes.
 *
 * \return	The number of bytes written, or -1 if the output would
 * 			overflow or the data ends early.
 *
 * \details	Port of LibDecode (1e92:0006). For each input byte: 0x00 or
 * 			0xFF followed by a count n gives the byte n + 1 times; one of
 * 			the resource's tokens gives a run from the token tables; any
 * 			other byte is copied as is.
 *
 ******************************************************************************/
static int lib__unpack(
	const lib_Resource * pResource,
	uint8_t *			 pOutput,
	int					 outputSize
) {
	uint8_t tokenNumber[256] = { 0 };

	for (int index = 0; index < pResource->tokenCount; index++) {
		tokenNumber[pResource->tokens[index]] = (uint8_t)(index + 1);
	}

	const uint8_t * pSource = pResource->pPacked;
	int				remaining = pResource->packedSize;
	int				written = 0;

	while (remaining > 0) {
		uint8_t code = *pSource++;
		uint8_t value;
		int		runLength;

		remaining--;

		if (code == 0x00 || code == 0xff) {
			if (remaining <= 0) {
				return -1;
			}

			remaining--;
			runLength = *pSource++ + 1;
			value = code;

		} else if (tokenNumber[code]) {
			runLength = lib__tokenLengths[tokenNumber[code] - 1];
			value = lib__tokenValues[tokenNumber[code] - 1];

		} else {
			runLength = 1;
			value = code;
		}

		if (written + runLength > outputSize) {
			return -1;
		}

		memset(pOutput + written, value, (size_t)runLength);
		written += runLength;
	}

	return written;
}
