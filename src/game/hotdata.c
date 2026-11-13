/** ****************************************************************************
 *
 * \file	hotdata.c
 *
 * \brief	Loading of hot_data (game tables and texts) and hall_dat (the
 * 			hall of fame).
 *
 * \details	Both files are copied into the data segment kept by exe.c, at
 * 			the offsets the original read them to. The meaning of the
 * 			blocks is worked out with the code that uses them.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/hotdata.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "game/exe.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define HOTDATA_FILE		 "Hot_data" /**< Tables and texts. */
#define HOTDATA_BLOCKS		 13			/**< Blocks in the file. */
#define HOTDATA_DESTINATIONS 0x4ea4		/**< DS offset: where each goes. */
#define HOTDATA_SIZES		 0x4ebe		/**< DS offset: size of each. */

#define HOTDATA_HALL_FILE	 "Hall_dat" /**< The hall of fame. */
#define HOTDATA_HALL_COUNT	 0x7646		/**< DS offset: number of entries. */
#define HOTDATA_HALL_ENTRIES 0x82ca		/**< DS offset: the entries. */
#define HOTDATA_HALL_ENTRY	 0x17		/**< Bytes of an entry. */
#define HOTDATA_HALL_MAX	 10			/**< Entries of the table. */
#define HOTDATA_HALL_MASK	 0x7f		/**< Bits kept of each byte. */

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
 * \brief	Load hot_data and the hall of fame.
 *
 * \retval	true	Loaded.
 * \retval	false	hot_data is missing or too short ("Data disc read
 * 					fail" in the original, which then ended the game).
 *
 * \details	Port of LoadHotData (1f38:6b79): 13 blocks, back to back in
 * 			the file, each read to the DS offset in the table at
 * 			DS:0x4ea4 with the size in the table at DS:0x4ebe. Then
 * 			LoadHallOfFame. The drive handling is not ported.
 *
 ******************************************************************************/
bool hotdata_load(void) {
	size_t			fileSize;
	const uint8_t * pFile = plat_loadFile(HOTDATA_FILE, &fileSize);

	if (!pFile) {
		plat_log("cannot read %s", HOTDATA_FILE);
		return false;
	}

	size_t position = 0;
	bool   complete = true;

	for (int block = 0; block < HOTDATA_BLOCKS && complete; block++) {
		uint16_t entry = (uint16_t)(block * 2);
		uint16_t destinationAt = (uint16_t)(HOTDATA_DESTINATIONS + entry);
		uint16_t sizeAt = (uint16_t)(HOTDATA_SIZES + entry);
		uint16_t destination = exe_readU16(destinationAt);
		uint16_t size = exe_readU16(sizeAt);

		complete = position + size <= fileSize &&
			(size_t)destination + size <= 0x10000;

		if (complete) {
			memcpy(exe_data(destination), pFile + position, size);
			position += size;
		}
	}

	plat_freeFile(pFile);

	if (!complete) {
		plat_log("%s: data disc read fail", HOTDATA_FILE);
		return false;
	}

	hotdata_loadHallOfFame();

	return true;
}

/** ****************************************************************************
 *
 * \brief	Load the hall of fame.
 *
 * \details	Port of LoadHallOfFame (1f38:6d09): a 16-bit count
 * 			(DS:0x7646), then that many 23-byte entries (DS:0x82ca).
 * 			The file is read from the saved games directory, where
 * 			fame_show writes it, else from the game files. Without the
 * 			file the hall of fame is empty. Afterwards bit 7
 * 			of every byte of the table is cleared (FUN_1f38_6cec). The
 * 			original ended the game on a short file; the port then keeps
 * 			the entries that are complete.
 *
 ******************************************************************************/
void hotdata_loadHallOfFame(void) {
	size_t			fileSize;
	const uint8_t * pFile = plat_readSave(HOTDATA_HALL_FILE, &fileSize);

	if (!pFile) {
		pFile = plat_loadFile(HOTDATA_HALL_FILE, &fileSize);
	}

	exe_writeU16(HOTDATA_HALL_COUNT, 0);

	if (!pFile) {
		return;
	}

	uint16_t count = 0;

	if (fileSize >= 2) {
		count = (uint16_t)(pFile[0] | (pFile[1] << 8));
	}

	if (count > HOTDATA_HALL_MAX) {
		count = HOTDATA_HALL_MAX;
	}

	if (count > (fileSize - 2) / HOTDATA_HALL_ENTRY && fileSize >= 2) {
		count = (uint16_t)((fileSize - 2) / HOTDATA_HALL_ENTRY);
	}

	uint8_t * pEntries = exe_data(HOTDATA_HALL_ENTRIES);
	size_t	  size = (size_t)count * HOTDATA_HALL_ENTRY;

	if (count > 0) {
		memcpy(pEntries, pFile + 2, size);
	}

	exe_writeU16(HOTDATA_HALL_COUNT, count);
	plat_freeFile(pFile);

	for (int index = 0; index < HOTDATA_HALL_MAX * HOTDATA_HALL_ENTRY;
		 index++) {
		pEntries[index] &= HOTDATA_HALL_MASK;
	}
}
