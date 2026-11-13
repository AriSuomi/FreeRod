/** ****************************************************************************
 *
 * \file	exe.c
 *
 * \brief	Initialized data of the original program, read from Sr.exe.
 *
 * \details	Sr.exe is packed with Microsoft EXEPACK. exe_load unpacks it
 * 			(as tools/Unexepack.java does), copies the data segment into a
 * 			64 KB buffer and the font into its own table, and releases the
 * 			rest.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/exe.h"

#include <stdlib.h>
#include <string.h>

#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define EXE_FILE "Sr.exe" /**< The original program. */

/**
 * Paragraph of the data segment in the load image. Ghidra loads the image
 * at segment 1000 and shows the data segment as 4e96.
 */
#define EXE_DATA_PARAGRAPH 0x3e96

#define EXE_DATA_SIZE	   0x10000L /**< A whole segment. */
#define EXE_FONT_SEGMENT   0x6a2a	/**< DS offset: segment of the font. */
#define EXE_CHECK_OFFSET   0x4aff	/**< DS offset of a known text. */
#define EXE_CHECK_TEXT	   "What"	/**< Start of that text. */
#define EXE_FIRST_GLYPH	   0x20		/**< Character of glyph 0. */

#define EXE_HEADER_SIZE	   0x1c /**< Bytes of the MZ header used. */
#define EXE_PACK_HEADER	   16	/**< Bytes of the EXEPACK header used. */
#define EXE_COMMAND_FILL   0xb0 /**< EXEPACK: repeat one byte. */
#define EXE_COMMAND_COPY   0xb2 /**< EXEPACK: copy bytes. */
#define EXE_COMMAND_MASK   0xfe /**< Command without the last-block bit. */
#define EXE_COMMAND_LAST   0x01 /**< Bit of the last block. */
#define EXE_SKIP_VARIANT   0x12 /**< Entry IP of stubs with a skip length. */
#define EXE_FONT_BYTES	   (EXE_FONT_GLYPHS * EXE_GLYPH_BYTES)

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

static uint16_t exe__fileU16(
	const uint8_t * pBytes
);

static uint8_t * exe__unpack(
	const uint8_t * pFile,
	long			fileSize,
	long *			pImageSize
);

static bool exe__decompress(
	uint8_t * pBuffer,
	long	  packedSize,
	long	  imageSize
);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

static uint8_t * exe__pData; /**< Copy of the data segment, 64 KB. */

static uint8_t * exe__pImage;	 /**< The unpacked load image, for exe_far. */
static long		 exe__imageSize; /**< Its size in bytes. */

/**
 * The font: for each character 0x20..0x7a its width in pixels and 8 rows
 * of pixels, leftmost pixel in bit 7.
 */
static uint8_t exe__font[EXE_FONT_BYTES];

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Read Sr.exe and keep its load image, data segment and font.
 *
 * \retval	true	The data is available.
 * \retval	false	Sr.exe is missing or is not the expected program; the
 * 					reason is logged.
 *
 ******************************************************************************/
bool exe_load(void) {
	size_t			fileSize;
	const uint8_t * pFile = plat_loadFile(EXE_FILE, &fileSize);

	if (!pFile) {
		plat_log("cannot read %s", EXE_FILE);
		return false;
	}

	long	  imageSize = 0;
	uint8_t * pImage = exe__unpack(pFile, (long)fileSize, &imageSize);

	plat_freeFile(pFile);

	if (!pImage) {
		plat_log("%s is not the EXEPACK-packed game program", EXE_FILE);
		return false;
	}

	long dataStart = EXE_DATA_PARAGRAPH * 16L;
	long checkEnd = dataStart + EXE_FONT_SEGMENT + 2;
	bool usable = imageSize >= checkEnd;

	if (usable) {
		const uint8_t * pCheck = pImage + dataStart + EXE_CHECK_OFFSET;

		usable = memcmp(pCheck, EXE_CHECK_TEXT, strlen(EXE_CHECK_TEXT)) == 0;
	}

	long fontStart = 0;

	if (usable) {
		const uint8_t * pSegment = pImage + dataStart + EXE_FONT_SEGMENT;

		fontStart = exe__fileU16(pSegment) * 16L;
		usable = fontStart + EXE_FONT_BYTES <= imageSize;
	}

	exe__pData = usable ? calloc(1, EXE_DATA_SIZE) : NULL;

	if (!exe__pData) {
		plat_log("%s is not the expected version of the game", EXE_FILE);
		free(pImage);
		return false;
	}

	long initialized = imageSize - dataStart;

	if (initialized > EXE_DATA_SIZE) {
		initialized = EXE_DATA_SIZE;
	}

	memcpy(exe__pData, pImage + dataStart, (size_t)initialized);
	memcpy(exe__font, pImage + fontStart, EXE_FONT_BYTES);
	exe__pImage = pImage;
	exe__imageSize = imageSize;

	return true;
}

/** ****************************************************************************
 *
 * \brief	Release the data read by exe_load.
 *
 ******************************************************************************/
void exe_unload(void) {
	free(exe__pData);
	free(exe__pImage);
	exe__pData = NULL;
	exe__pImage = NULL;
	exe__imageSize = 0;
}

/** ****************************************************************************
 *
 * \brief	Address of constant data in another segment of the game.
 *
 * \param	segment			Segment as the data segment holds it (a paragraph
 * 							of the load image; Ghidra shows it 0x1000
 * 							higher).
 * \param	offset			Offset in that segment.
 * \param	length			Bytes that will be read.
 *
 * \return	Pointer into the load image, NULL when the bytes are not all
 * 			in it. The image is as Sr.exe holds it: what the game stores
 * 			in its data segment later is only in exe_data.
 *
 ******************************************************************************/
const uint8_t * exe_far(
	uint16_t segment,
	uint16_t offset,
	uint16_t length
) {
	long start = segment * 16L + offset;

	if (exe__pImage == NULL || start + length > exe__imageSize) {
		return NULL;
	}

	return exe__pImage + start;
}

/** ****************************************************************************
 *
 * \brief	Address of a byte of the game's data segment.
 *
 * \param	offset			DS offset in Sr_unpacked.exe.
 *
 * \return	Pointer into the copy of the data segment. Offsets past the
 * 			initialized data are zero until something is stored there.
 *
 ******************************************************************************/
uint8_t * exe_data(
	uint16_t offset
) {
	return exe__pData + offset;
}

/** ****************************************************************************
 *
 * \brief	A zero-terminated text of the game's data segment.
 *
 * \param	offset			DS offset of the text.
 *
 * \return	The text.
 *
 ******************************************************************************/
const char * exe_text(
	uint16_t offset
) {
	return (const char *)exe_data(offset);
}

/** ****************************************************************************
 *
 * \brief	Read a 16-bit value of the data segment.
 *
 * \param	offset			DS offset of the value.
 *
 * \return	The value.
 *
 ******************************************************************************/
uint16_t exe_readU16(
	uint16_t offset
) {
	return exe__fileU16(exe_data(offset));
}

/** ****************************************************************************
 *
 * \brief	Read a signed 16-bit value of the data segment.
 *
 * \param	offset			DS offset of the value.
 *
 * \return	The value.
 *
 ******************************************************************************/
int16_t exe_readS16(
	uint16_t offset
) {
	return (int16_t)exe_readU16(offset);
}

/** ****************************************************************************
 *
 * \brief	Store a 16-bit value in the data segment.
 *
 * \param	offset			DS offset of the value.
 * \param	value			The value.
 *
 ******************************************************************************/
void exe_writeU16(
	uint16_t offset,
	uint16_t value
) {
	uint8_t * pBytes = exe_data(offset);

	pBytes[0] = (uint8_t)(value & 0xff);
	pBytes[1] = (uint8_t)(value >> 8);
}

/** ****************************************************************************
 *
 * \brief	The font data of a character.
 *
 * \param	character		The character.
 *
 * \return	EXE_GLYPH_BYTES bytes: the width in pixels, then 8 rows of
 * 			pixels with the leftmost pixel in bit 7. Characters outside
 * 			0x20..0x7a get the glyph of the space, as in the game's text
 * 			drawing (FUN_31a0_113a).
 *
 ******************************************************************************/
const uint8_t * exe_glyph(
	char character
) {
	int glyph = (uint8_t)character - EXE_FIRST_GLYPH;

	if (glyph < 0 || glyph >= EXE_FONT_GLYPHS) {
		glyph = 0;
	}

	return &exe__font[glyph * EXE_GLYPH_BYTES];
}

/** ****************************************************************************
 *
 * \brief	Read a little-endian 16-bit value.
 *
 * \param	pBytes			The two bytes.
 *
 * \return	The value.
 *
 ******************************************************************************/
static uint16_t exe__fileU16(
	const uint8_t * pBytes
) {
	return (uint16_t)(pBytes[0] | (pBytes[1] << 8));
}

/** ****************************************************************************
 *
 * \brief	Unpack an EXEPACK-packed program into its load image.
 *
 * \param	pFile			Contents of the .exe file.
 * \param	fileSize		Size of the file.
 * \param	pImageSize		Output: size of the load image.
 *
 * \return	The load image (to be released with free), or NULL when the
 * 			file is not an EXEPACK-packed program.
 *
 * \details	The EXEPACK header is at CS:0 of the packed image: +0 the real
 * 			IP, +2 the real CS, +6 the size of the unpacker, +8 SP, +10
 * 			SS, +12 the unpacked size in paragraphs, +14 (stubs whose
 * 			entry is at 0x12) a skip length; "RB" comes right before the
 * 			entry point. Relocations are not applied: the data the port
 * 			reads holds no far pointers except the font segment, which is
 * 			used relative to the image.
 *
 ******************************************************************************/
static uint8_t * exe__unpack(
	const uint8_t * pFile,
	long			fileSize,
	long *			pImageSize
) {
	if (fileSize < EXE_HEADER_SIZE || pFile[0] != 'M' || pFile[1] != 'Z') {
		return NULL;
	}

	long headerSize = exe__fileU16(pFile + 8) * 16L;
	long entryOffset = exe__fileU16(pFile + 0x14);
	long codeSegment = exe__fileU16(pFile + 0x16) * 16L;
	long packHeader = headerSize + codeSegment;

	if (entryOffset < 2 || packHeader + EXE_PACK_HEADER > fileSize ||
		packHeader + entryOffset > fileSize) {
		return NULL;
	}

	const uint8_t * pSignature = pFile + packHeader + entryOffset - 2;

	if (pSignature[0] != 'R' || pSignature[1] != 'B') {
		return NULL;
	}

	long imageSize = exe__fileU16(pFile + packHeader + 12) * 16L;
	long skipSize = 0;

	if (entryOffset == EXE_SKIP_VARIANT) {
		skipSize = (exe__fileU16(pFile + packHeader + 14) - 1) * 16L;
	}

	long packedSize = codeSegment - skipSize;

	if (packedSize <= 0 || imageSize <= 0) {
		return NULL;
	}

	long	  bufferSize = imageSize > packedSize ? imageSize : packedSize;
	uint8_t * pBuffer = malloc((size_t)bufferSize);

	if (!pBuffer) {
		return NULL;
	}

	memcpy(pBuffer, pFile + headerSize, (size_t)packedSize);

	if (!exe__decompress(pBuffer, packedSize, imageSize)) {
		free(pBuffer);
		return NULL;
	}

	*pImageSize = imageSize;

	return pBuffer;
}

/** ****************************************************************************
 *
 * \brief	Decompress EXEPACK data in place, from the end backwards.
 *
 * \param	pBuffer			Buffer holding the packed data, large enough for
 * 							the image.
 * \param	packedSize		Bytes of packed data.
 * \param	imageSize		Bytes of the unpacked image.
 *
 * \retval	true	The buffer holds the image.
 * \retval	false	The packed data is damaged.
 *
 * \details	After 0xFF padding the data ends with blocks read backwards: a
 * 			command byte, a 16-bit length, and for 0xB0 one byte to
 * 			repeat, for 0xB2 that many bytes to copy. Bit 0 of the command
 * 			marks the last block.
 *
 ******************************************************************************/
static bool exe__decompress(
	uint8_t * pBuffer,
	long	  packedSize,
	long	  imageSize
) {
	long source = packedSize - 1;
	long destination = imageSize - 1;

	while (source >= 0 && pBuffer[source] == 0xff) {
		source--;
	}

	for (;;) {
		if (source < 2) {
			return false;
		}

		uint8_t command = pBuffer[source];
		long	length = (pBuffer[source - 1] << 8) | pBuffer[source - 2];

		source -= 3;

		if (length > destination + 1) {
			return false;
		}

		if ((command & EXE_COMMAND_MASK) == EXE_COMMAND_FILL) {
			if (source < 0) {
				return false;
			}

			uint8_t value = pBuffer[source--];

			for (long count = 0; count < length; count++) {
				pBuffer[destination--] = value;
			}

		} else if ((command & EXE_COMMAND_MASK) == EXE_COMMAND_COPY) {
			if (length > source + 1) {
				return false;
			}

			for (long count = 0; count < length; count++) {
				pBuffer[destination--] = pBuffer[source--];
			}

		} else {
			return false;
		}

		if (command & EXE_COMMAND_LAST) {
			return true;
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Read a 32-bit value of the data segment.
 *
 * \param	offset			DS offset of the value.
 *
 * \return	The value.
 *
 ******************************************************************************/
uint32_t exe_readU32(
	uint16_t offset
) {
	uint32_t high = exe_readU16((uint16_t)(offset + 2));

	return (high << 16) | exe_readU16(offset);
}

/** ****************************************************************************
 *
 * \brief	Store a 32-bit value in the data segment.
 *
 * \param	offset			DS offset of the value.
 * \param	value			The value.
 *
 ******************************************************************************/
void exe_writeU32(
	uint16_t offset,
	uint32_t value
) {
	exe_writeU16(offset, (uint16_t)(value & 0xffff));
	exe_writeU16((uint16_t)(offset + 2), (uint16_t)(value >> 16));
}
