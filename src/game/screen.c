/** ****************************************************************************
 *
 * \file	screen.c
 *
 * \brief	Drawing on the game's two surfaces: resources, filled
 * 			rectangles and the wipe between them.
 *
 * \details	Only the EGA/VGA versions are ported (the EGA video function
 * 			table at DS:0x5bd0). The mouse cursor, which the original hides
 * 			around each drawing (SetMouseCursor, 1f38:2d7e), is not ported
 * 			yet.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <stdlib.h>

#include "game/screen.h"

#include "dos/dos.h"
#include "game/cursor.h"
#include "game/globals.h"
#include "game/lib.h"
#include "game/text.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define SCREEN_RESOURCES	   277 /**< Resources in Lib1 and Lib2. */
#define SCREEN_DISPLAY_PAGE	   0   /**< The page shown by the platform. */
#define SCREEN_WORK_PAGE	   1   /**< The other page. */
#define SCREEN_WIPE_PASSES	   5   /**< Passes of screen_wipe. */
#define SCREEN_WIPE_TICKS	   3   /**< Ticks per pass at least. */
#define SCREEN_FRAME		   3   /**< Width of screen_frame's frame. */
#define SCREEN_BORDER_DOUBLE   2   /**< globals_borderStyle: two lines. */
#define SCREEN_BORDER_SOLID	   3   /**< globals_borderStyle: line color. */
#define SCREEN_BYTE_MASK	   ~7  /**< Rounds a column down to a byte. */
#define SCREEN_FIRST_SAVE_PAGE 2   /**< First page used for saved areas. */
#define SCREEN_SAVE_PAGES	   4   /**< Areas saved at the same time. */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/**
 * A rectangle saved with screen_saveRect.
 */
typedef struct screen__saved_tag {
	int16_t left;	/**< Left edge, a multiple of 8. */
	int16_t top;	/**< Top edge. */
	int16_t width;	/**< Width. */
	int16_t height; /**< Height. */
} screen__Saved;

/*******************************************************************************
;
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

static void screen__toDisplay(
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height
);

static void screen__waitTick(void);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/**
 * Image of each resource, created on first use.
 */
static plat_ImageHandle screen__images[SCREEN_RESOURCES];

/**
 * The saved rectangles, the newest last; entry n is kept in platform
 * page SCREEN_FIRST_SAVE_PAGE + n.
 */
static screen__Saved screen__saved[SCREEN_SAVE_PAGES];

static int screen__savedCount; /**< Used entries of screen__saved. */

/**
 * DS:0x792 and the record at DS:0x70ac: a sprite screen_slide draws over
 * each step (0: none), and where its left top corner goes.
 */
static int16_t screen__overlayId;
static int16_t screen__overlayLeft; /**< Left edge of the overlay. */
static int16_t screen__overlayTop;	/**< Top edge of the overlay. */

/**
 * DS:0x5c0c: first row of each wipe pass (stored as byte offsets 0, 80,
 * 160, 40, 120 at 40 bytes per row).
 */
static const int16_t screen__wipeFirstRows[SCREEN_WIPE_PASSES] = {
	0, 2, 4, 1, 3
};

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Set up the display and work surfaces.
 *
 * \details	Port of InitScreenSurfaces (1f38:1859), which main calls with
 * 			the two surface structures at DS:0x0 and DS:0x30 (segment
 * 			3e3e). Drawing then also goes to the display surface.
 *
 ******************************************************************************/
void screen_initSurfaces(void) {
	globals_displaySurface = SCREEN_DISPLAY_PAGE;
	globals_workSurface = SCREEN_WORK_PAGE;
	globals_drawThrough = 1;
}

/** ****************************************************************************
 *
 * \brief	Get the image of a resource, creating it on first use.
 *
 * \param	index			Resource index (lib_get), not the game's number.
 *
 * \return	Handle of the image, or 0 if the resource cannot be decoded.
 *
 * \details	Stands in for loading and decoding a resource
 * 			(FUN_1f38_683e, LibLoadAndDecode): the platform keeps the
 * 			decoded picture, so each resource is decoded once.
 *
 ******************************************************************************/
plat_ImageHandle screen_resourceImage(
	int index
) {
	if (index < 0 || index >= SCREEN_RESOURCES || index >= lib_count()) {
		return 0;
	}

	if (screen__images[index]) {
		return screen__images[index];
	}

	const lib_Resource * pResource = lib_get(index);

	size_t	  pixelCount = (size_t)pResource->width * pResource->height;
	uint8_t * pPixels = malloc(pixelCount);

	if (!pPixels) {
		return 0;
	}

	if (lib_decodePixels(index, pPixels)) {
		screen__images[index] = plat_imageCreate(
			pResource->name,
			pResource->width,
			pResource->height,
			pPixels,
			pResource->flag != LIB_FLAG_IMAGE
		);
	}

	free(pPixels);

	return screen__images[index];
}

/** ****************************************************************************
 *
 * \brief	Draw a resource on the work surface.
 *
 * \param	gameId			The game's resource number (1..9: Lib1,
 * 							1000..: Lib2).
 * \param	clearColor		>= 0: first fill 320 x globals_drawHeight with
 * 							this color; < 0: draw over what is there.
 *
 * \details	Port of DrawResource (1f38:2554). Draws at globals_drawLeft,
 * 			globals_drawTop (then resets both to 0), at most
 * 			globals_drawHeight rows, and copies the rectangle to the
 * 			display surface while globals_drawThrough is set. The blit is
 * 			a plain copy (mode 1): color 0 of a sprite is drawn too. As
 * 			with every blit of the video table, the column is rounded
 * 			down to a multiple of 8 (screen_byteColumn). Not ported:
 * 			FUN_1f38_a0b8(1) at the end (use unknown).
 *
 ******************************************************************************/
void screen_drawResource(
	int16_t gameId,
	int16_t clearColor
) {
	int				 index = lib_indexFromGameId(gameId);
	plat_ImageHandle image = screen_resourceImage(index);

	if (!image) {
		plat_log("resource %d cannot be drawn", gameId);
		return;
	}

	const lib_Resource * pResource = lib_get(index);

	if (clearColor >= 0) {
		uint8_t color = (uint8_t)clearColor;

		screen_fillRect(0, 0, PLAT_WIDTH, globals_drawHeight, color);
	}

	int16_t width = (int16_t)pResource->width;
	int16_t height = (int16_t)pResource->height;

	if (globals_drawHeight < height) {
		height = globals_drawHeight;
	}

	int16_t left = screen_byteColumn(globals_drawLeft);
	int16_t top = globals_drawTop;

	cursor_set(CURSOR_HIDE);
	plat_selectPage(globals_workSurface);
	plat_fillRect(left, top, width, height, 0);
	plat_drawImageRows(image, left, top, height);
	cursor_set(CURSOR_SHOW);

	if (globals_drawThrough != 0) {
		screen__toDisplay(left, top, width, height);
	}

	globals_drawTop = 0;
	globals_drawLeft = 0;
}

/** ****************************************************************************
 *
 * \brief	Round a column down to a byte column of the screen.
 *
 * \param	column			Column, width or source column in pixels.
 *
 * \return	column rounded down to a multiple of 8 (also below 0).
 *
 * \details	What FUN_1f38_1680 does to the columns of every blit of the
 * 			video table (an arithmetic shift by DS:0x823a, 3 in the
 * 			16-color mode, mask DS:0x823e 0xfff8).
 *
 ******************************************************************************/
int16_t screen_byteColumn(
	int16_t column
) {
	return (int16_t)(column & SCREEN_BYTE_MASK);
}

/** ****************************************************************************
 *
 * \brief	Draw a resource at a position on the work surface.
 *
 * \param	gameId			The game's resource number.
 * \param	left			Left edge.
 * \param	top				Top edge.
 * \param	clearColor		As for screen_drawResource.
 *
 * \details	Port of DrawResourceAt (1f38:2638).
 *
 ******************************************************************************/
void screen_drawResourceAt(
	int16_t gameId,
	int16_t left,
	int16_t top,
	int16_t clearColor
) {
	globals_drawLeft = left;
	globals_drawTop = top;
	screen_drawResource(gameId, clearColor);
}

/** ****************************************************************************
 *
 * \brief	Fill a rectangle on the work surface.
 *
 * \param	left			Left edge.
 * \param	top				Top edge.
 * \param	width			Width.
 * \param	height			Height.
 * \param	color			Palette index.
 *
 * \details	Port of FillRect (1f38:231a): fills with globals_fillColor
 * 			set to color for the call, and copies the rectangle to the
 * 			display surface while globals_drawThrough is set.
 *
 ******************************************************************************/
void screen_fillRect(
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height,
	uint8_t color
) {
	uint8_t savedColor = globals_fillColor;

	cursor_set(CURSOR_HIDE);
	globals_fillColor = color;
	plat_selectPage(globals_workSurface);
	plat_fillRect(left, top, width, height, globals_fillColor);
	cursor_set(CURSOR_SHOW);

	if (globals_drawThrough != 0) {
		screen__toDisplay(left, top, width, height);
	}

	globals_fillColor = savedColor;
}

/** ****************************************************************************
 *
 * \brief	Release the decoded data of a resource.
 *
 * \param	gameId			The game's resource number.
 *
 * \details	Port of LibFreeResource (1f38:655e). Does nothing in the
 * 			port: the platform keeps each decoded image for as long as it
 * 			is drawn anywhere, and decoding again would waste time.
 *
 ******************************************************************************/
void screen_freeResource(
	int16_t gameId
) {
	(void)gameId;
}

/** ****************************************************************************
 *
 * \brief	Copy one surface to another in five interlaced passes.
 *
 * \param	destination		Surface copied to, usually the display.
 * \param	source			Surface copied from, usually the work surface.
 *
 * \details	Port of EgaWipeToDisplay (31a0:1722), entry 10 of the EGA
 * 			video function table (DS:0x78ca). Each pass copies every 5th
 * 			row, starting at rows 0, 2, 4, 1, 3, and then waits until 3
 * 			ticks have passed since it started. The original cleared each
 * 			destination row just before copying it.
 *
 ******************************************************************************/
void screen_wipe(
	int destination,
	int source
) {
	for (int pass = 0; pass < SCREEN_WIPE_PASSES; pass++) {
		uint32_t passStart = globals_ticks;

		for (int row = screen__wipeFirstRows[pass]; row < PLAT_HEIGHT;
			 row += SCREEN_WIPE_PASSES) {
			plat_copyRect(source, destination, 0, row, PLAT_WIDTH, 1);
		}

		while (globals_ticks < passStart + SCREEN_WIPE_TICKS) {
			dos_service();
		}
	}

	if (destination == globals_displaySurface) {
		cursor_surfaceCopied(0, 0, PLAT_WIDTH, PLAT_HEIGHT);
	}
}

/** ****************************************************************************
 *
 * \brief	Copy an area of the work surface to the display surface.
 *
 * \param	left			Left edge.
 * \param	top				Top edge.
 * \param	right			Right edge, included.
 * \param	bottom			Bottom edge, included.
 *
 * \details	Port of FUN_1f38_b5aa (called with the work and the display
 * 			surface throughout). The edges are limited to the screen and
 * 			the area is widened to whole bytes of video memory (8 pixels).
 *
 ******************************************************************************/
void screen_copyToDisplay(
	int16_t left,
	int16_t top,
	int16_t right,
	int16_t bottom
) {
	if (left < 0) {
		left = 0;
	}

	if (right < 0) {
		right = 0;
	}

	if (right > PLAT_WIDTH - 1) {
		right = PLAT_WIDTH - 1;
	}

	if (bottom > PLAT_HEIGHT - 1) {
		bottom = PLAT_HEIGHT - 1;
	}

	int16_t alignedLeft = (int16_t)(left & SCREEN_BYTE_MASK);
	int16_t alignedRight = (int16_t)((right + 8) & SCREEN_BYTE_MASK);
	int16_t width = (int16_t)(alignedRight - alignedLeft);
	int16_t height = (int16_t)(bottom - top + 1);

	screen__toDisplay(alignedLeft, top, width, height);
}

/** ****************************************************************************
 *
 * \brief	Fill a rectangle of a surface with globals_fillColor.
 *
 * \param	surface			Surface to draw on.
 * \param	left			Left edge.
 * \param	top				Top edge.
 * \param	width			Width.
 * \param	height			Height.
 *
 * \details	Port of FUN_1f38_4302, which clips to the right and bottom
 * 			edges of the surface and draws row by row with function table
 * 			entry 12 (DS:0x78d2).
 *
 ******************************************************************************/
void screen_fill(
	int		surface,
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height
) {
	if (PLAT_WIDTH < left + width) {
		width = (int16_t)(PLAT_WIDTH - left);
	}

	if (screen_surfaceHeight(surface) < top + height) {
		height = (int16_t)(screen_surfaceHeight(surface) - top);
	}

	plat_selectPage(surface);
	plat_fillRect(left, top, width, height, globals_fillColor);
}

/** ****************************************************************************
 *
 * \brief	Draw a line between two points.
 *
 * \param	surface			Surface to draw on.
 * \param	fromLeft		Column of the first point.
 * \param	fromTop			Row of the first point.
 * \param	toLeft			Column of the second point.
 * \param	toTop			Row of the second point.
 * \param	color			Color of the line.
 *
 * \details	Port of function table entry 12 (DS:0x78d2, EGA: 31a0:23bc).
 * 			The line runs from its left point; an error value of twice
 * 			the short side less the long side decides after each pixel
 * 			if the line also moves along its short side (at 0 or more).
 * 			Not ported: the drawing mode DS:0x5c1e (taken as plain
 * 			drawing).
 *
 ******************************************************************************/
void screen_line(
	int		surface,
	int16_t fromLeft,
	int16_t fromTop,
	int16_t toLeft,
	int16_t toTop,
	uint8_t color
) {
	if (toLeft < fromLeft) {
		int16_t swapped = fromLeft;

		fromLeft = toLeft;
		toLeft = swapped;
		swapped = fromTop;
		fromTop = toTop;
		toTop = swapped;
	}

	int16_t across = (int16_t)(toLeft - fromLeft);
	int16_t down = (int16_t)(toTop - fromTop);
	int16_t rowStep = 1;

	if (down < 0) {
		down = (int16_t)-down;
		rowStep = -1;
	}

	bool	steep = down > across;
	int16_t longSide = steep ? down : across;
	int16_t shortSide = steep ? across : down;
	int16_t error = (int16_t)(2 * shortSide - longSide);

	plat_selectPage(surface);

	for (int16_t count = 0; count <= longSide; count++) {
		plat_fillRect(fromLeft, fromTop, 1, 1, color);

		bool both = error >= 0;

		if (both) {
			error = (int16_t)(error + 2 * shortSide - 2 * longSide);
		} else {
			error = (int16_t)(error + 2 * shortSide);
		}

		if (steep || both) {
			fromTop = (int16_t)(fromTop + rowStep);
		}

		if (!steep || both) {
			fromLeft++;
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Draw a vertical line with globals_lineColor.
 *
 * \param	surface			Surface to draw on.
 * \param	left			Column of the line.
 * \param	top				First row.
 * \param	height			Number of rows.
 *
 * \details	Port of FUN_1f38_439c.
 *
 ******************************************************************************/
void screen_verticalLine(
	int		surface,
	int16_t left,
	int16_t top,
	int16_t height
) {
	if (screen_surfaceHeight(surface) < height + top) {
		height = (int16_t)(screen_surfaceHeight(surface) - top);
	}

	plat_selectPage(surface);
	plat_fillRect(left, top, 1, height, globals_lineColor);
}

/** ****************************************************************************
 *
 * \brief	Draw a horizontal line with globals_lineColor.
 *
 * \param	surface			Surface to draw on.
 * \param	left			First column.
 * \param	top				Row of the line.
 * \param	width			Number of columns.
 *
 * \details	Port of FUN_1f38_43ff: screen_fill, one row high, with the
 * 			line color as the fill color.
 *
 ******************************************************************************/
void screen_horizontalLine(
	int		surface,
	int16_t left,
	int16_t top,
	int16_t width
) {
	uint8_t savedColor = globals_fillColor;

	globals_fillColor = globals_lineColor;
	screen_fill(surface, left, top, width, 1);
	globals_fillColor = savedColor;
}

/** ****************************************************************************
 *
 * \brief	Draw a 3 pixel wide frame around a rectangle.
 *
 * \param	surface			Surface to draw on.
 * \param	left			Left edge of the rectangle.
 * \param	top				Top edge of the rectangle.
 * \param	width			Width of the rectangle.
 * \param	height			Height of the rectangle.
 *
 * \details	Port of FUN_1f38_4432. The frame lies outside the rectangle
 * 			and is filled with globals_fillColor. globals_borderStyle 3
 * 			fills it with the line color instead; 1 adds a line in the
 * 			middle of the frame; 2 adds a second line next to the
 * 			rectangle.
 *
 ******************************************************************************/
void screen_frame(
	int		surface,
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height
) {
	int16_t right = (int16_t)(left + width);
	int16_t bottom = (int16_t)(top + height);
	int16_t frameTop = (int16_t)(top - SCREEN_FRAME);
	int16_t frameHeight = (int16_t)(height + 2 * SCREEN_FRAME);
	uint8_t savedColor = globals_fillColor;

	if (globals_borderStyle == SCREEN_BORDER_SOLID) {
		globals_fillColor = globals_lineColor;
	}

	screen_fill(
		surface,
		(int16_t)(left - SCREEN_FRAME),
		frameTop,
		SCREEN_FRAME,
		frameHeight
	);
	screen_fill(surface, left, bottom, width, SCREEN_FRAME);
	screen_fill(surface, right, frameTop, SCREEN_FRAME, frameHeight);
	screen_fill(surface, left, frameTop, width, SCREEN_FRAME);

	if (globals_borderStyle == SCREEN_BORDER_SOLID) {
		globals_fillColor = savedColor;
		return;
	}

	screen_box(
		surface,
		(int16_t)(left - 1),
		(int16_t)(top - 1),
		(int16_t)(width + 2),
		(int16_t)(height + 2)
	);

	if (globals_borderStyle == SCREEN_BORDER_DOUBLE) {
		screen_box(surface, left, top, width, height);
	}
}

/** ****************************************************************************
 *
 * \brief	Draw a one pixel wide box around a rectangle.
 *
 * \param	surface			Surface to draw on.
 * \param	left			Left edge of the rectangle.
 * \param	top				Top edge of the rectangle.
 * \param	width			Width of the rectangle.
 * \param	height			Height of the rectangle.
 *
 * \details	Port of FUN_1f38_45d7. The lines lie just outside the
 * 			rectangle and use globals_lineColor; the corners at the top
 * 			and bottom of the vertical lines are included.
 *
 ******************************************************************************/
void screen_box(
	int		surface,
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height
) {
	int16_t lineTop = (int16_t)(top - 1);
	int16_t lineHeight = (int16_t)(height + 2);

	screen_verticalLine(surface, (int16_t)(left - 1), lineTop, lineHeight);
	screen_horizontalLine(surface, left, (int16_t)(top + height), width);
	screen_verticalLine(surface, (int16_t)(left + width), lineTop, lineHeight);
	screen_horizontalLine(surface, left, lineTop, width);
}

/** ****************************************************************************
 *
 * \brief	Draw a text on the work surface.
 *
 * \param	left			Left edge.
 * \param	top				Top edge.
 * \param	pText			The text.
 *
 * \details	Port of FUN_1f38_2656: text_draw with the current colors, and
 * 			a copy to the display surface while globals_drawThrough is
 * 			set.
 *
 ******************************************************************************/
void screen_drawText(
	int16_t		 left,
	int16_t		 top,
	const char * pText
) {
	text_draw(globals_workSurface, left, top, pText);

	if (globals_drawThrough != 0) {
		screen_copyToDisplay(
			left,
			top,
			(int16_t)(globals_textEndX - 1),
			(int16_t)(top + TEXT_ROWS - 1)
		);
	}
}

/** ****************************************************************************
 *
 * \brief	Replace one color with another in a rectangle of a surface.
 *
 * \param	surface			Surface to draw on.
 * \param	fromColor		Palette index to replace.
 * \param	toColor			Palette index to write in its place.
 * \param	left			Left edge.
 * \param	top				Top edge.
 * \param	right			Right edge, excluded.
 * \param	bottom			Bottom edge, excluded.
 *
 * \details	Port of FUN_31a0_1508 (entry 6 of the EGA video function table,
 * 			DS:0x78ba), used by ui_list to highlight the hovered item.
 *
 ******************************************************************************/
void screen_recolor(
	int		surface,
	uint8_t fromColor,
	uint8_t toColor,
	int16_t left,
	int16_t top,
	int16_t right,
	int16_t bottom
) {
	if (left < 0) {
		left = 0;
	}

	if (top < 0) {
		top = 0;
	}

	if (right > PLAT_WIDTH) {
		right = PLAT_WIDTH;
	}

	int16_t maxBottom = screen_surfaceHeight(surface);

	if (bottom > maxBottom) {
		bottom = maxBottom;
	}

	plat_selectPage(surface);

	uint8_t * pPixels = plat_framebuffer();

	for (int16_t row = top; row < bottom; row++) {
		uint8_t * pRow = pPixels + (size_t)row * PLAT_WIDTH;

		for (int16_t col = left; col < right; col++) {
			if (pRow[col] == fromColor) {
				pRow[col] = toColor;
			}
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Copy a rectangle of the work surface to the display surface.
 *
 * \param	left			Left edge.
 * \param	top				Top edge.
 * \param	width			Width.
 * \param	height			Height.
 *
 * \details	Stands in for the blit of function table entry 0 (DS:0x78a2)
 * 			from the work to the display surface. The cursor is told, as
 * 			it is not part of the surfaces in the port.
 *
 ******************************************************************************/
static void screen__toDisplay(
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height
) {
	plat_copyRect(
		globals_workSurface,
		globals_displaySurface,
		left,
		top,
		width,
		height
	);
	cursor_surfaceCopied(left, top, width, height);
}

/** ****************************************************************************
 *
 * \brief	Draw a resource on a surface, at a position.
 *
 * \param	surface			Surface to draw on.
 * \param	gameId			The game's resource number.
 * \param	left			Left edge.
 * \param	top				Top edge.
 *
 * \details	Port of FUN_1f38_b3b4 (called with 0 as its last argument,
 * 			whose use is unknown): decodes the resource and draws it with
 * 			its mask (FUN_1f38_af50), so color 0 of a sprite stays
 * 			see-through. Nothing is copied to the display.
 *
 ******************************************************************************/
void screen_drawSprite(
	int		surface,
	int16_t gameId,
	int16_t left,
	int16_t top
) {
	plat_ImageHandle image =
		screen_resourceImage(lib_indexFromGameId(gameId));

	if (!image) {
		plat_log("resource %d cannot be drawn", gameId);
		return;
	}

	plat_selectPage(surface);
	plat_drawImage(image, left, top);
}

/** ****************************************************************************
 *
 * \brief	Save a rectangle of the work surface.
 *
 * \param	left			Left edge.
 * \param	top				Top edge.
 * \param	width			Width.
 * \param	height			Height.
 *
 * \return	Handle for screen_restoreRect, or 0 when too many rectangles
 * 			are saved.
 *
 * \details	Port of FUN_1f38_26f2 with a new buffer (FUN_1f38_9fce): the
 * 			rectangle is widened to whole bytes and clipped to the
 * 			screen, and copied from the work surface with the cursor
 * 			hidden. The original took the buffer from a stack of
 * 			temporary memory; the port uses spare platform pages as that
 * 			stack, which keeps replacement art in the saved picture.
 *
 ******************************************************************************/
int screen_saveRect(
	int16_t left,
	int16_t top,
	int16_t width,
	int16_t height
) {
	if (screen__savedCount >= SCREEN_SAVE_PAGES) {
		plat_log("too many saved screen areas");
		return 0;
	}

	int16_t right = (int16_t)((left + width + 7) & SCREEN_BYTE_MASK);
	int16_t bottom = (int16_t)(top + height);

	if (right > PLAT_WIDTH) {
		right = PLAT_WIDTH;
	}

	if (bottom > PLAT_HEIGHT) {
		bottom = PLAT_HEIGHT;
	}

	left = (int16_t)(left & SCREEN_BYTE_MASK);

	screen__Saved * pSaved = &screen__saved[screen__savedCount];
	int				page = SCREEN_FIRST_SAVE_PAGE + screen__savedCount;

	pSaved->left = left;
	pSaved->top = top;
	pSaved->width = (int16_t)(right - left);
	pSaved->height = (int16_t)(bottom - top);
	screen__savedCount++;

	cursor_set(CURSOR_HIDE);
	plat_selectPage(page);
	plat_clearPage();
	plat_copyRect(
		globals_workSurface,
		page,
		pSaved->left,
		pSaved->top,
		pSaved->width,
		pSaved->height
	);
	cursor_set(CURSOR_SHOW);

	return screen__savedCount;
}

/** ****************************************************************************
 *
 * \brief	Put back the rectangle saved last and release it.
 *
 * \param	handle			Handle from screen_saveRect; it must be the newest
 * 							one. 0 does nothing.
 * \param	toDisplay		true: also copy the rectangle to the display while
 * 							globals_drawThrough is set.
 *
 * \details	Port of FUN_1f38_27ef (work surface only) and FUN_1f38_2853
 * 			(also the display), followed by FUN_1f38_a0b8(1), which
 * 			releases the buffer.
 *
 ******************************************************************************/
void screen_restoreRect(
	int	 handle,
	bool toDisplay
) {
	if (handle == 0 || handle != screen__savedCount) {
		return;
	}

	screen__savedCount--;

	const screen__Saved * pSaved = &screen__saved[screen__savedCount];

	int page = SCREEN_FIRST_SAVE_PAGE + screen__savedCount;

	cursor_set(CURSOR_HIDE);
	plat_copyRect(
		page,
		globals_workSurface,
		pSaved->left,
		pSaved->top,
		pSaved->width,
		pSaved->height
	);
	cursor_set(CURSOR_SHOW);

	if (toDisplay && globals_drawThrough != 0) {
		screen_copyToDisplay(
			pSaved->left,
			pSaved->top,
			(int16_t)(pSaved->left + pSaved->width - 1),
			(int16_t)(pSaved->top + pSaved->height - 1)
		);
	}
}

/** ****************************************************************************
 *
 * \brief	Release the rectangle saved last without putting it back.
 *
 * \param	handle			Handle from screen_saveRect; it must be the newest
 * 							one. 0 does nothing.
 *
 * \details	Port of FUN_1f38_a0b8(1) for a buffer of FUN_1f38_26f2.
 *
 ******************************************************************************/
void screen_dropRect(
	int handle
) {
	if (handle != 0 && handle == screen__savedCount) {
		screen__savedCount--;
	}
}

/** ****************************************************************************
 *
 * \brief	The height of a surface.
 *
 * \param	surface			The surface.
 *
 * \return	globals_workHeight for the work surface, otherwise 200. Text
 * 			and fills are clipped to it (word +2 of the original's
 * 			surface structure).
 *
 ******************************************************************************/
int16_t screen_surfaceHeight(
	int surface
) {
	return surface == globals_workSurface ? globals_workHeight : PLAT_HEIGHT;
}

/** ****************************************************************************
 *
 * \brief	Forget all saved rectangles.
 *
 * \details	Port of FUN_1f38_a054, which empties the stack of temporary
 * 			memory the saved rectangles live in.
 *
 ******************************************************************************/
void screen_resetSaved(void) {
	screen__savedCount = 0;
}

/** ****************************************************************************
 *
 * \brief	Put a saved rectangle back on the work surface and keep it.
 *
 * \param	handle			Handle from screen_saveRect (any saved one).
 *
 * \details	For animations that restore the same background each step.
 *
 ******************************************************************************/
void screen_putBackRect(
	int handle
) {
	if (handle < 1 || handle > screen__savedCount) {
		return;
	}

	const screen__Saved * pSaved = &screen__saved[handle - 1];

	plat_copyRect(
		SCREEN_FIRST_SAVE_PAGE + handle - 1,
		globals_workSurface,
		pSaved->left,
		pSaved->top,
		pSaved->width,
		pSaved->height
	);
}

/** ****************************************************************************
 *
 * \brief	The platform page that holds a saved rectangle.
 *
 * \param	handle			Handle from screen_saveRect (any saved one).
 *
 * \return	The page; the rectangle lies at its place on the screen. -1
 * 			for a handle that is not saved.
 *
 * \details	Stands in for the buffer of FUN_1f38_9fce that FUN_1f38_26f2
 * 			filled: animations draw sprites into it and blit it. As in
 * 			the original, the picture stays until the next rectangle is
 * 			saved, also after screen_resetSaved.
 *
 ******************************************************************************/
int screen_savedPage(
	int handle
) {
	if (handle < 1 || handle > screen__savedCount) {
		return -1;
	}

	return SCREEN_FIRST_SAVE_PAGE + handle - 1;
}

/** ****************************************************************************
 *
 * \brief	Draw a part of a resource (a frame of a sprite sheet).
 *
 * \param	surface			Surface to draw on.
 * \param	gameId			The game's resource number.
 * \param	sourceLeft		Left edge of the part in the resource.
 * \param	sourceTop		Top edge of the part in the resource.
 * \param	width			Width of the part.
 * \param	height			Height of the part.
 * \param	left			Where its left edge goes.
 * \param	top				Where its top edge goes.
 * \param	opaque			true: color 0 of a sprite is drawn too (a plain
 * 							copy, blit mode 1); false: it stays
 * 							see-through.
 *
 * \details	Stands in for the video table's blits of a rectangle of a
 * 			decoded resource (entry 0, DS:0x78a2, and entry 2 with mask,
 * 			DS:0x78aa). Both clip with FUN_1f38_1680, which turns the
 * 			source column, the width and the target column into byte
 * 			columns (shift DS:0x823a, 3 in the 16-color mode), so each
 * 			is rounded down to a multiple of 8 (screen_byteColumn).
 * 			Nothing is copied to the display.
 *
 ******************************************************************************/
void screen_drawSpritePart(
	int		surface,
	int16_t gameId,
	int16_t sourceLeft,
	int16_t sourceTop,
	int16_t width,
	int16_t height,
	int16_t left,
	int16_t top,
	bool	opaque
) {
	screen_drawSpritePartExact(
		surface,
		gameId,
		screen_byteColumn(sourceLeft),
		sourceTop,
		screen_byteColumn(width),
		height,
		screen_byteColumn(left),
		top,
		opaque
	);
}

/** ****************************************************************************
 *
 * \brief	Draw a part of a resource at exact columns.
 *
 * \param	surface			Surface to draw on.
 * \param	gameId			The game's resource number.
 * \param	sourceLeft		Left edge of the part in the resource.
 * \param	sourceTop		Top edge of the part in the resource.
 * \param	width			Width of the part.
 * \param	height			Height of the part.
 * \param	left			Where its left edge goes.
 * \param	top				Where its top edge goes.
 * \param	opaque			As for screen_drawSpritePart.
 *
 * \details	Like screen_drawSpritePart without rounding the columns.
 * 			Stands in for blits of sprites the original shifted to the
 * 			column first (FUN_1f38_acbf) or composed into a picture
 * 			that starts on a byte column. Nothing is copied to the
 * 			display.
 *
 ******************************************************************************/
void screen_drawSpritePartExact(
	int		surface,
	int16_t gameId,
	int16_t sourceLeft,
	int16_t sourceTop,
	int16_t width,
	int16_t height,
	int16_t left,
	int16_t top,
	bool	opaque
) {
	plat_ImageHandle image =
		screen_resourceImage(lib_indexFromGameId(gameId));

	if (!image) {
		plat_log("resource %d cannot be drawn", gameId);
		return;
	}

	plat_selectPage(surface);

	if (opaque) {
		plat_fillRect(left, top, width, height, 0);
	}

	plat_drawImagePart(
		image,
		sourceLeft,
		sourceTop,
		width,
		height,
		left,
		top,
		NULL
	);
}

/** ****************************************************************************
 *
 * \brief	Move a sprite up or down over a strip of the screen.
 *
 * \param	gameId			The game's resource number of the sprite.
 * \param	sourceLeft		Left edge of the sprite in the resource.
 * \param	width			Width of the sprite (from the resource's top).
 * \param	height			Height of the sprite.
 * \param	left			Column of the sprite; rounded down to a
 * 							multiple of 8.
 * \param	fromRow			Row of the sprite's top edge at first.
 * \param	toRow			Row of the sprite's top edge at the end.
 * \param	stripTop		First row of the strip the sprite is shown in.
 * \param	stripBottom		Last row of the strip.
 *
 * \details	Port of FUN_31a0_28ec (entry 11 of the EGA video function
 * 			table, DS:0x78ce), which takes byte columns: see
 * 			screen_slideAt.
 *
 ******************************************************************************/
void screen_slide(
	int16_t gameId,
	int16_t sourceLeft,
	int16_t width,
	int16_t height,
	int16_t left,
	int16_t fromRow,
	int16_t toRow,
	int16_t stripTop,
	int16_t stripBottom
) {
	screen_slideAt(
		gameId,
		sourceLeft,
		width,
		height,
		(int16_t)(left / 8 * 8),
		fromRow,
		toRow,
		stripTop,
		stripBottom
	);
}

/** ****************************************************************************
 *
 * \brief	Set the sprite screen_slide draws over the moving sprite.
 *
 * \param	gameId			The game's resource number, or 0 for none.
 * \param	left			Column of its left edge.
 * \param	top				Row of its top edge.
 *
 * \details	Stands in for the copy at DS:0x792 and its record at
 * 			DS:0x70ac (FUN_1f38_3951 sets them, FUN_1f38_3a58 clears
 * 			them): a sprite that stays in front of the one that moves.
 *
 ******************************************************************************/
void screen_setSlideOverlay(
	int16_t gameId,
	int16_t left,
	int16_t top
) {
	screen__overlayId = gameId;
	screen__overlayLeft = left;
	screen__overlayTop = top;
}

/** ****************************************************************************
 *
 * \brief	Move a sprite up or down over a strip, at an exact column.
 *
 * \param	gameId			The game's resource number of the sprite.
 * \param	sourceLeft		Left edge of the sprite in the resource.
 * \param	width			Width of the sprite (from the resource's top).
 * \param	height			Height of the sprite.
 * \param	left			Column of the sprite.
 * \param	fromRow			Row of the sprite's top edge at first.
 * \param	toRow			Row of the sprite's top edge at the end.
 * \param	stripTop		First row of the strip the sprite is shown in.
 * \param	stripBottom		Last row of the strip.
 *
 * \details	Port of FUN_31a0_28ec for a sprite its caller shifted to the
 * 			column first (FUN_1f38_acbf): the port draws at any column.
 * 			The strip under the sprite is saved first; each step puts it
 * 			back on the work surface, draws the sprite (clipped to the
 * 			strip) and the overlay of screen_setSlideOverlay, waits a
 * 			frame, copies the strip to the display and waits another
 * 			frame. The sprite moves 2 rows per step and ends exactly at
 * 			toRow (an odd distance starts one row off). The original
 * 			waits for the vertical retrace (70 Hz); the port waits a tick
 * 			of the game clock (72.8 Hz) instead, as the car scroll does.
 * 			Without globals_drawThrough the steps are only drawn on the
 * 			work surface, without waiting or the overlay. The sprite
 * 			stays drawn at the end.
 *
 ******************************************************************************/
void screen_slideAt(
	int16_t gameId,
	int16_t sourceLeft,
	int16_t width,
	int16_t height,
	int16_t left,
	int16_t fromRow,
	int16_t toRow,
	int16_t stripTop,
	int16_t stripBottom
) {
	plat_ImageHandle image =
		screen_resourceImage(lib_indexFromGameId(gameId));

	if (!image) {
		plat_log("resource %d cannot be drawn", gameId);
		return;
	}

	plat_ImageHandle overlay = 0;

	if (screen__overlayId != 0) {
		overlay = screen_resourceImage(lib_indexFromGameId(screen__overlayId));
	}

	int16_t column = left;
	int16_t rows = (int16_t)(stripBottom - stripTop + 1);
	int16_t distance = (int16_t)(toRow - fromRow);
	int16_t step = 2;

	if (distance < 0) {
		if ((distance & 1) != 0) {
			distance--;
			fromRow++;
		}

		step = -2;
		distance = (int16_t)-distance;
	} else if ((distance & 1) != 0) {
		distance++;
		fromRow--;
	}

	int16_t	  steps = (int16_t)(distance / 2 + 1);
	int		  saved = screen_saveRect(column, stripTop, width, rows);
	const int strip[4] = { column, stripTop, width, rows };
	int16_t	  row = fromRow;

	for (int16_t count = 0; count < steps; count++) {
		screen_putBackRect(saved);
		plat_selectPage(globals_workSurface);
		plat_drawImagePart(
			image,
			sourceLeft,
			0,
			width,
			height,
			column,
			row,
			strip
		);

		if (globals_drawThrough != 0) {
			if (overlay) {
				plat_drawImage(
					overlay,
					screen__overlayLeft,
					screen__overlayTop
				);
			}

			screen__waitTick();
			screen__toDisplay(column, stripTop, width, rows);
			screen__waitTick();
		}

		row = (int16_t)(row + step);
	}

	screen_dropRect(saved);
}

/** ****************************************************************************
 *
 * \brief	Wait for the next tick of the game clock.
 *
 * \details	Stands in for a wait for the vertical retrace
 * 			(FUN_31a0_0006).
 *
 ******************************************************************************/
static void screen__waitTick(void) {
	uint32_t start = globals_ticks;

	while (globals_ticks == start) {
		dos_service();
	}
}

/** ****************************************************************************
 *
 * \brief	Wait for vertical retraces.
 *
 * \param	count			Retraces to wait for.
 *
 * \details	Port of WaitRetraces (1f38:177d). The port waits a tick of the game
 * 			clock for each retrace, as screen_slide does.
 *
 ******************************************************************************/
void screen_waitRetraces(
	int16_t count
) {
	for (int16_t index = 0; index < count; index++) {
		screen__waitTick();
	}
}
