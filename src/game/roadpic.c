/** ****************************************************************************
 *
 * \file	roadpic.c
 *
 * \brief	The pictures of the 3D road (segment 3645).
 *
 * \details	Port of FUN_3645_2429, which draws the fixed pictures of a
 * 			drive on the road view and on DS:0x82b4 and keeps the
 * 			others, with the scaled copies of the cars (FUN_3645_5407,
 * 			the EGA version, with FUN_3645_52b7 and 528c). The port
 * 			takes the EGA path (DS:0x8236 = -2) for the copies, as for
 * 			everything else, but the layout of the other adapters for
 * 			the surfaces (see drive.c): the strips go to DS:0x82b4, not
 * 			below the visible rows of the screen. FUN_3645_5670 (CGA,
 * 			with 55f8) and 5885 (adapter -6) are not ported.
 *
 * 			A picture buffer becomes a roadpic_Picture: the resource's
 * 			image, or for a scaled copy a new image with the copy's
 * 			pixels, named after the resource so that a replacement PNG
 * 			is drawn scaled to the copy's size. DS:0x82b4 is the page
 * 			ROADPIC_PAGE; the port keeps the panel's saved areas, which
 * 			the original also puts on DS:0x82b4, on a page of their own
 * 			(drive.c), so the two never overlap. The memory bookkeeping
 * 			(FUN_1f38_a09e, a0b8) has no part in the port.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/roadpic.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "game/exe.h"
#include "game/globals.h"
#include "game/lib.h"
#include "game/screen.h"
#include "game/state.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

/*
 * Pictures drawn once at the start (game resource numbers).
 */
#define ROADPIC__VIEW_LOWER	 0x3ed /**< On the road view at row 0x66. */
#define ROADPIC__STRIP_A	 0x4ed /**< Cruise: on DS:0x82b4 at row 1. */
#define ROADPIC__STRIP_B	 0x4ee /**< Cruise: on DS:0x82b4 at row 0x2c. */
#define ROADPIC__BOTTOM		 0x4f3 /**< At the bottom of the road view. */
#define ROADPIC__BOTTOM_ADD	 0x3f0 /**< Cruise: drawn on ROADPIC__BOTTOM. */
#define ROADPIC__LOWER_TOP	 0x66  /**< Row of the view and the backdrop. */
#define ROADPIC__STRIP_A_TOP 1	   /**< Row of ROADPIC__STRIP_A. */
#define ROADPIC__STRIP_B_TOP 0x2c  /**< Row of ROADPIC__STRIP_B. */
#define ROADPIC__ADD_LEFT	 0x2c  /**< Place of ROADPIC__BOTTOM_ADD. */
#define ROADPIC__ADD_TOP	 0x0c  /**< Place of ROADPIC__BOTTOM_ADD. */

/*
 * Pictures kept for the drive.
 */
#define ROADPIC__OVER_A		  0x3ee /**< DS:0x72f4. */
#define ROADPIC__OVER_B		  0x3ef /**< DS:0x8bca. */
#define ROADPIC__MIRROR		  0x3f1 /**< DS:0x8184. */
#define ROADPIC__MIRROR_BACK  0x4db /**< DS:0x8b7e. */
#define ROADPIC__RACE_START	  0x3f5 /**< DS:0x6e60. */
#define ROADPIC__POLICE_SIDE  0x4d9 /**< Police car, position 1. */
#define ROADPIC__POLICE_AHEAD 0x4da /**< Police car, positions 2..6. */
#define ROADPIC__POLICE_BACK  0x4de /**< Police car, positions 9..12. */

/*
 * Tables of Sr.exe.
 */
#define ROADPIC__BACKDROPS	  0x5e62 /**< u16 by town (0 in a race). */
#define ROADPIC__CARS		  0x5e14 /**< 4 u16 pictures by car class. */
#define ROADPIC__CAR_SIZE	  8		 /**< Bytes of a ROADPIC__CARS record. */
#define ROADPIC__MASKS_AHEAD  0x5e56 /**< 3 u8 masks: positions 3..5. */
#define ROADPIC__MASKS_FAR	  0x5e5a /**< 3 u8 masks: positions 6..8. */
#define ROADPIC__MASKS_BEHIND 0x5e5e /**< 3 u8 masks: positions 10..12. */
#define ROADPIC__RACING		  0x8aca /**< u8: 1 in a race, 0 in a cruise. */
#define ROADPIC__RIVAL_MODEL  0x7648 /**< s16: the opponent's car model. */

/*
 * Positions of a car (index of roadpic_Set's tables).
 */
#define ROADPIC__AHEAD	   2 /**< Just ahead; copies 3..5 follow. */
#define ROADPIC__FAR	   6 /**< First copy of the copy 5. */
#define ROADPIC__BEHIND	   9 /**< In the mirror; copies 10..12. */
#define ROADPIC__COPIES	   3 /**< Scaled copies in each group. */

#define ROADPIC__ALIGN	   0xfff8 /**< DS:0x823e on EGA: width / 8. */
#define ROADPIC__POOL_SIZE 40	  /**< Pictures a drive keeps. */

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

roadpic_Set roadpic_set;

/** The pictures roadpic_set points to. */
static roadpic_Picture roadpic__pool[ROADPIC__POOL_SIZE];

/** Used entries of roadpic__pool. */
static int roadpic__poolCount;

/*******************************************************************************
;
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

static void roadpic__release(void);

static roadpic_Picture * roadpic__new(void);

static const roadpic_Picture * roadpic__load(
	int16_t gameId
);

static const roadpic_Picture * roadpic__scaled(
	const roadpic_Picture * pSource,
	uint8_t					mask
);

static int16_t roadpic__bitCount(
	uint8_t mask
);

static int16_t roadpic__scaledSize(
	int16_t size,
	uint8_t mask
);

static void roadpic__copyPixels(
	const uint8_t * pSource,
	int16_t			sourceWidth,
	int16_t			sourceHeight,
	uint8_t			rowMask,
	uint8_t			columnMask,
	uint8_t *		pTarget,
	int16_t			width,
	int16_t			height
);

static void roadpic__draw(
	int		page,
	int16_t gameId,
	int16_t left,
	int16_t top,
	bool	opaque,
	int16_t maxWidth,
	int16_t maxHeight
);

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Draw the fixed pictures of a drive and keep the others.
 *
 * \param	view			Surface the road is drawn on (DS:0x8188).
 *
 * \details	Port of FUN_3645_2429. Picture 0x3ed goes on the road view
 * 			at row 0x66, the backdrop DS:0x5e62[town, 0 in a race] on
 * 			DS:0x82b4 at row 0x66. In a cruise the strips 0x4ed and
 * 			0x4ee go on DS:0x82b4 at rows 1 and 0x2c. Picture 0x4f3,
 * 			in a cruise with 0x3f0 drawn on it at (0x2c, 0xc), goes on
 * 			the bottom rows of the view. Then 0x3ee, 0x3ef, 0x3f1 and
 * 			0x4db are kept. In a race also 0x3f5 (its height to
 * 			DS:0x7aae) and the opponent's car by its class (the byte
 * 			at DS:0x8df0 + model DS:0x7648 * 10) from the record
 * 			DS:0x5e14 + class * 8: +0 and +2 are positions 0 and 1,
 * 			+4 is 2 with three scaled copies (masks DS:0x5e56) as 3..5,
 * 			copy 5 scaled again (masks DS:0x5e5a) as 6..8, +6 is 9 with
 * 			three copies (masks DS:0x5e5e) as 10..12. The police car
 * 			the same way: 0 is the opponent's 0, 1 is 0x4d9, 2..6 one
 * 			copy of 0x4da (mask DS:0x5e56), 9 is 0x4de with copies 10
 * 			to 12; positions 7 and 8 are left as they were (the port:
 * 			none). Each copy uses its mask for rows and columns.
 *
 * 			The port forgets the pictures of the last drive first, so
 * 			in a cruise the race pictures are none, where the original
 * 			keeps pointers to freed memory that nothing reads.
 *
 ******************************************************************************/
void roadpic_load(
	int view
) {
	screen_resetSaved();
	roadpic__release();

	roadpic__draw(
		view,
		ROADPIC__VIEW_LOWER,
		0,
		ROADPIC__LOWER_TOP,
		true,
		PLAT_WIDTH,
		PLAT_HEIGHT
	);

	bool	racing = *exe_data(ROADPIC__RACING) != 0;
	int16_t town = racing ? 0 : globals_place;
	int16_t backdrop =
		(int16_t)exe_readU16((uint16_t)(ROADPIC__BACKDROPS + town * 2));

	roadpic__draw(
		ROADPIC_PAGE,
		backdrop,
		0,
		ROADPIC__LOWER_TOP,
		true,
		PLAT_WIDTH,
		PLAT_HEIGHT
	);

	if (!racing) {
		roadpic__draw(
			ROADPIC_PAGE,
			ROADPIC__STRIP_A,
			0,
			ROADPIC__STRIP_A_TOP,
			true,
			PLAT_WIDTH,
			PLAT_HEIGHT
		);
		roadpic__draw(
			ROADPIC_PAGE,
			ROADPIC__STRIP_B,
			0,
			ROADPIC__STRIP_B_TOP,
			true,
			PLAT_WIDTH,
			PLAT_HEIGHT
		);
	}

	const roadpic_Picture * pBottom = roadpic__load(ROADPIC__BOTTOM);

	int16_t bottomTop = (int16_t)(PLAT_HEIGHT - pBottom->height);

	roadpic__draw(
		view,
		ROADPIC__BOTTOM,
		0,
		bottomTop,
		true,
		PLAT_WIDTH,
		PLAT_HEIGHT
	);

	if (!racing) {
		roadpic__draw(
			view,
			ROADPIC__BOTTOM_ADD,
			ROADPIC__ADD_LEFT,
			(int16_t)(bottomTop + ROADPIC__ADD_TOP),
			false,
			(int16_t)(pBottom->width - ROADPIC__ADD_LEFT),
			(int16_t)(pBottom->height - ROADPIC__ADD_TOP)
		);
	}

	roadpic_set.pOverA = roadpic__load(ROADPIC__OVER_A);
	roadpic_set.pOverB = roadpic__load(ROADPIC__OVER_B);
	roadpic_set.pMirror = roadpic__load(ROADPIC__MIRROR);
	roadpic_set.pMirrorBack = roadpic__load(ROADPIC__MIRROR_BACK);

	if (!racing) {
		return;
	}

	int16_t	 model = exe_readS16(ROADPIC__RIVAL_MODEL);
	uint16_t parts = (uint16_t)(STATE_MODEL_PARTS + model * STATE_MODEL_SIZE);
	int8_t	 carClass = (int8_t)*exe_data(parts);
	uint16_t cars =
		(uint16_t)(ROADPIC__CARS + carClass * ROADPIC__CAR_SIZE);
	const roadpic_Picture ** pCar = roadpic_set.pOpponent;
	const roadpic_Picture ** pPolice = roadpic_set.pPolice;

	roadpic_set.pRaceStart = roadpic__load(ROADPIC__RACE_START);
	exe_writeU16(ROADPIC_RACE_START, (uint16_t)roadpic_set.pRaceStart->height);

	pCar[0] = roadpic__load((int16_t)exe_readU16(cars));
	pCar[1] = roadpic__load((int16_t)exe_readU16((uint16_t)(cars + 2)));
	pCar[ROADPIC__AHEAD] =
		roadpic__load((int16_t)exe_readU16((uint16_t)(cars + 4)));

	for (int copy = 0; copy < ROADPIC__COPIES; copy++) {
		uint8_t mask = *exe_data((uint16_t)(ROADPIC__MASKS_AHEAD + copy));

		pCar[ROADPIC__AHEAD + 1 + copy] =
			roadpic__scaled(pCar[ROADPIC__AHEAD], mask);
	}

	for (int copy = 0; copy < ROADPIC__COPIES; copy++) {
		uint8_t mask = *exe_data((uint16_t)(ROADPIC__MASKS_FAR + copy));

		pCar[ROADPIC__FAR + copy] =
			roadpic__scaled(pCar[ROADPIC__FAR - 1], mask);
	}

	pCar[ROADPIC__BEHIND] =
		roadpic__load((int16_t)exe_readU16((uint16_t)(cars + 6)));

	for (int copy = 0; copy < ROADPIC__COPIES; copy++) {
		uint8_t mask = *exe_data((uint16_t)(ROADPIC__MASKS_BEHIND + copy));

		pCar[ROADPIC__BEHIND + 1 + copy] =
			roadpic__scaled(pCar[ROADPIC__BEHIND], mask);
	}

	pPolice[0] = pCar[0];
	pPolice[1] = roadpic__load(ROADPIC__POLICE_SIDE);

	const roadpic_Picture * pAhead = roadpic__scaled(
		roadpic__load(ROADPIC__POLICE_AHEAD),
		*exe_data(ROADPIC__MASKS_AHEAD)
	);

	for (int position = ROADPIC__AHEAD; position < ROADPIC__FAR + 1;
		 position++) {
		pPolice[position] = pAhead;
	}

	pPolice[ROADPIC__BEHIND] = roadpic__load(ROADPIC__POLICE_BACK);

	for (int copy = 0; copy < ROADPIC__COPIES; copy++) {
		uint8_t mask = *exe_data((uint16_t)(ROADPIC__MASKS_BEHIND + copy));

		pPolice[ROADPIC__BEHIND + 1 + copy] =
			roadpic__scaled(pPolice[ROADPIC__BEHIND], mask);
	}
}

/** ****************************************************************************
 *
 * \brief	Forget the pictures of the last drive.
 *
 * \details	Not part of the original. Destroys the images of the
 * 			scaled copies; the images of resources stay (screen.c keeps
 * 			them).
 *
 ******************************************************************************/
static void roadpic__release(void) {
	for (int index = 0; index < roadpic__poolCount; index++) {
		roadpic_Picture * pPicture = &roadpic__pool[index];

		if (pPicture->pPixels != NULL) {
			plat_imageDestroy(pPicture->image);
			free(pPicture->pPixels);
		}
	}

	memset(roadpic__pool, 0, sizeof roadpic__pool);
	memset(&roadpic_set, 0, sizeof roadpic_set);
	roadpic__poolCount = 0;
}

/** ****************************************************************************
 *
 * \brief	Take an empty picture of the pool.
 *
 * \return	The picture (all 0).
 *
 * \details	Not part of the original. The pool has room for every
 * 			picture of a drive; should it run out, the last entry is
 * 			used again.
 *
 ******************************************************************************/
static roadpic_Picture * roadpic__new(void) {
	if (roadpic__poolCount < ROADPIC__POOL_SIZE) {
		roadpic__poolCount++;
	} else {
		plat_log("road: too many pictures");
	}

	roadpic_Picture * pPicture = &roadpic__pool[roadpic__poolCount - 1];

	if (pPicture->pPixels != NULL) {
		plat_imageDestroy(pPicture->image);
		free(pPicture->pPixels);
	}

	memset(pPicture, 0, sizeof *pPicture);

	return pPicture;
}

/** ****************************************************************************
 *
 * \brief	Load a resource as a picture.
 *
 * \param	gameId			The game's resource number.
 *
 * \return	The picture; without the resource it has no image and the
 * 			size 0.
 *
 * \details	Stands in for FUN_1f38_683e (load and decode).
 *
 ******************************************************************************/
static const roadpic_Picture * roadpic__load(
	int16_t gameId
) {
	roadpic_Picture * pPicture = roadpic__new();
	int				  index = lib_indexFromGameId(gameId);
	plat_ImageHandle  image = screen_resourceImage(index);

	pPicture->gameId = gameId;

	if (!image) {
		plat_log("road: picture %d cannot be loaded", gameId);

		return pPicture;
	}

	const lib_Resource * pResource = lib_get(index);

	pPicture->image = image;
	pPicture->width = (int16_t)pResource->width;
	pPicture->height = (int16_t)pResource->height;

	return pPicture;
}

/** ****************************************************************************
 *
 * \brief	Make a scaled copy of a picture.
 *
 * \param	pSource			The picture.
 * \param	mask			Rows and columns kept: a row or column is kept
 * 							where bit 0x80 >> (its index mod 8) is set.
 *
 * \return	The copy.
 *
 * \details	Port of FUN_3645_5407 (EGA) with FUN_3645_52b7, for the
 * 			same mask for rows and columns as FUN_3645_2429 passes. The
 * 			copy's size is worked out by roadpic__scaledSize, its width
 * 			rounded up to 8 (DS:0x823e); its pixels start at 0 and its
 * 			mask, if the source has one, is made from the pixels
 * 			(BuildSpriteMask), which the port's see-through color 0
 * 			stands for. The image is named after the source's resource.
 *
 ******************************************************************************/
static const roadpic_Picture * roadpic__scaled(
	const roadpic_Picture * pSource,
	uint8_t					mask
) {
	int16_t				 width = roadpic__scaledSize(pSource->width, mask);
	int16_t				 height = roadpic__scaledSize(pSource->height, mask);
	int					 index = lib_indexFromGameId(pSource->gameId);
	const lib_Resource * pResource = lib_get(index);
	roadpic_Picture *	 pCopy = roadpic__new();

	width = (int16_t)((width + 7) & ROADPIC__ALIGN);
	pCopy->gameId = pSource->gameId;
	pCopy->width = width;
	pCopy->height = height;

	if (pResource == NULL || width <= 0 || height <= 0 ||
		pSource->width <= 0 || pSource->height <= 0) {
		return pCopy;
	}

	size_t			sourceSize = (size_t)pSource->width * pSource->height;
	uint8_t *		pDecoded = NULL;
	const uint8_t * pPixels = pSource->pPixels;

	if (pPixels == NULL) {
		if (pSource->width != pResource->width ||
			pSource->height != pResource->height) {
			return pCopy;
		}

		pDecoded = malloc(sourceSize);

		if (pDecoded == NULL || !lib_decodePixels(index, pDecoded)) {
			free(pDecoded);

			return pCopy;
		}

		pPixels = pDecoded;
	}

	pCopy->pPixels = malloc((size_t)width * height);

	if (pCopy->pPixels != NULL) {
		roadpic__copyPixels(
			pPixels,
			pSource->width,
			pSource->height,
			mask,
			mask,
			pCopy->pPixels,
			width,
			height
		);
		pCopy->image = plat_imageCreate(
			pResource->name,
			width,
			height,
			pCopy->pPixels,
			pResource->flag != LIB_FLAG_IMAGE
		);
	}

	free(pDecoded);

	return pCopy;
}

/** ****************************************************************************
 *
 * \brief	Count the bits that are set.
 *
 * \param	mask			The byte.
 *
 * \return	0..8.
 *
 * \details	Port of FUN_3645_528c.
 *
 ******************************************************************************/
static int16_t roadpic__bitCount(
	uint8_t mask
) {
	int16_t count = 0;

	for (int bit = 0; bit < 8; bit++) {
		if ((mask & (0x80 >> bit)) != 0) {
			count++;
		}
	}

	return count;
}

/** ****************************************************************************
 *
 * \brief	Work out how many rows or columns a scaled copy keeps.
 *
 * \param	size			Rows or columns of the source.
 * \param	mask			Which are kept (see roadpic__scaled).
 *
 * \return	The number kept.
 *
 * \details	Part of FUN_3645_52b7: whole groups of 8 keep the number of
 * 			bits set, the rest of the group as many as are set among
 * 			its first bits from 0x80 on. 16-bit arithmetic, as there.
 *
 ******************************************************************************/
static int16_t roadpic__scaledSize(
	int16_t size,
	uint8_t mask
) {
	int16_t kept = (int16_t)(((size & 0xfff8) * roadpic__bitCount(mask)) >> 3);
	int		bit = 0x80;

	for (int rest = size & 7; rest > 0; rest--) {
		if ((mask & bit) != 0) {
			kept++;
		}

		bit >>= 1;
	}

	return kept;
}

/** ****************************************************************************
 *
 * \brief	Copy the kept rows and columns of a picture.
 *
 * \param	pSource			Pixels of the source, row by row.
 * \param	sourceWidth		Width of the source.
 * \param	sourceHeight	Height of the source.
 * \param	rowMask			Rows kept.
 * \param	columnMask		Columns kept.
 * \param	pTarget			Pixels of the copy, row by row.
 * \param	width			Width of the copy.
 * \param	height			Height of the copy.
 *
 * \details	Part of FUN_3645_5407. The original writes the kept bits of
 * 			each plane one after the other, over all bytes of a source
 * 			row (its width rounded up to 8, the rest 0), and goes on at
 * 			the next byte after each row; the copy's rows are its width
 * 			apart. The port does the same with pixels, so where a row
 * 			keeps more than the copy's width it runs into the next row
 * 			as it does there. What would go past the end is left out.
 *
 ******************************************************************************/
static void roadpic__copyPixels(
	const uint8_t * pSource,
	int16_t			sourceWidth,
	int16_t			sourceHeight,
	uint8_t			rowMask,
	uint8_t			columnMask,
	uint8_t *		pTarget,
	int16_t			width,
	int16_t			height
) {
	size_t	size = (size_t)width * (size_t)height;
	size_t	position = 0;
	int16_t paddedWidth = (int16_t)((sourceWidth + 7) & ROADPIC__ALIGN);

	memset(pTarget, 0, size);

	for (int16_t row = 0; row < sourceHeight; row++) {
		if ((rowMask & (0x80 >> (row & 7))) == 0) {
			continue;
		}

		const uint8_t * pRow = pSource + (size_t)row * (size_t)sourceWidth;

		for (int16_t column = 0; column < paddedWidth; column++) {
			if ((columnMask & (0x80 >> (column & 7))) == 0) {
				continue;
			}

			if (position < size && column < sourceWidth) {
				pTarget[position] = pRow[column];
			}

			position++;
		}

		position = (position + 7) & ~(size_t)7;
	}
}

/** ****************************************************************************
 *
 * \brief	Draw a resource whole on a page.
 *
 * \param	page			The page.
 * \param	gameId			The game's resource number.
 * \param	left			Where its left edge goes.
 * \param	top				Where its top edge goes.
 * \param	opaque			true: a plain copy (DS:0x78a2); false: color
 * 							0 stays see-through (DS:0x78aa, with mask).
 * \param	maxWidth		At most this many columns are drawn.
 * \param	maxHeight		At most this many rows are drawn.
 *
 * \details	Stands in for loading a picture (FUN_1f38_683e) and a blit
 * 			of all of it; maxWidth and maxHeight stand for the clipping
 * 			to a picture buffer drawn into.
 *
 ******************************************************************************/
static void roadpic__draw(
	int		page,
	int16_t gameId,
	int16_t left,
	int16_t top,
	bool	opaque,
	int16_t maxWidth,
	int16_t maxHeight
) {
	const lib_Resource * pResource = lib_get(lib_indexFromGameId(gameId));

	if (pResource == NULL) {
		plat_log("road: picture %d cannot be drawn", gameId);

		return;
	}

	int16_t width = (int16_t)pResource->width;
	int16_t height = (int16_t)pResource->height;

	if (width > maxWidth) {
		width = maxWidth;
	}

	if (height > maxHeight) {
		height = maxHeight;
	}

	if (width <= 0 || height <= 0) {
		return;
	}

	screen_drawSpritePart(page, gameId, 0, 0, width, height, left, top, opaque);
}
