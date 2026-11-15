/** ****************************************************************************
 *
 * \file	platform.c
 *
 * \brief	Platform layer implemented with SDL3 and SDL3_image.
 *
 * \details	Owns the window, the palette, the pages with their image and
 * 			pixel layers, input and time. See platform.h for the layer
 * 			model.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

/*
 * The program's own main() is used; plat_init tells SDL with SDL_SetMainReady.
 * This keeps SDL out of main.c.
 */
#define SDL_MAIN_HANDLED

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <time.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#	include <direct.h>
#else
#	include <sys/stat.h>
#endif

#include "platform/platform.h"
#include "platform/sound.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define KEY_QUEUE_SIZE 64	/**< Key events buffered between reads. */
#define MAX_IMAGES	   1024 /**< Images alive at the same time. */
#define MAX_DRAWS	   2048 /**< Draws on the image layer of a page. */
#define LOG_APP		   SDL_LOG_CATEGORY_APPLICATION
#define PAGE_PIXELS	   (PLAT_WIDTH * PLAT_HEIGHT) /**< Pixels of a page. */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/**
 * An image that can be drawn on the image layer.
 */
typedef struct plat__image_tag {
	bool		  used;			  /**< Slot in use. */
	bool		  released;		  /**< Destroyed, but still drawn. */
	char		  name[32];		  /**< Replacement lookup name. */
	int			  width;		  /**< Width in game pixels. */
	int			  height;		  /**< Height in game pixels. */
	uint8_t *	  pPixels;		  /**< Indexed original. */
	int			  seeThrough;	  /**< Index shown through, or -1. */
	SDL_Texture * pOriginal;	  /**< Built from pixels and palette. */
	unsigned	  paletteVersion; /**< Palette the texture was built with. */
	SDL_Texture * pReplacement;	  /**< From the mod folder, or NULL. */
} plat__Image;

/**
 * One image placed on the image layer.
 */
typedef struct plat__draw_tag {
	plat_ImageHandle image; /**< The image. */
	int				 left;	/**< Left edge of the image in game pixels. */
	int				 top;	/**< Top edge of the image in game pixels. */
	SDL_Rect		 clip;	/**< Visible part, in game pixels. */
} plat__Draw;

/**
 * One page of video memory.
 */
typedef struct plat__page_tag {
	plat__Draw draws[MAX_DRAWS];	/**< Image layer, in draw order. */
	int		   drawCount;			/**< Used entries of draws. */
	bool	   drawsOverflowed;		/**< Overflow already reported. */
	uint8_t	   pixels[PAGE_PIXELS]; /**< Pixel layer, row by row. */
} plat__Page;

/**
 * A key going down or up.
 */
typedef struct plat__key_event_tag {
	int	 key;	  /**< PLAT_KEY_* code or ASCII code. */
	bool pressed; /**< true: pressed (or repeated), false: released. */
} plat__KeyEvent;

/*******************************************************************************
;
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

static plat_ImageHandle plat__createImage(
	const char *	pName,
	int				width,
	int				height,
	const uint8_t * pPixels,
	int				seeThrough
);

static void plat__makeFolder(
	const char * pFolder
);

static uint8_t plat__dacTo8Bit(
	uint8_t value
);

static bool plat__createWindow(
	const char * pTitle,
	int			 windowScale
);

static bool plat__createLayerTextures(void);

static void plat__imageFreeTextures(
	plat__Image * pImage
);

static bool plat__isDrawn(
	plat_ImageHandle id
);

static void plat__freeImage(
	plat__Image * pImage
);

static plat__Image * plat__getImage(
	plat_ImageHandle id
);

static SDL_Texture * plat__loadReplacement(
	const char * pName
);

static void plat__convertToArgb(
	const uint8_t * pSource,
	int				width,
	int				height,
	int				transparentIndex,
	void *			pDestination,
	int				pitch
);

static SDL_Texture * plat__originalTexture(
	plat__Image * pImage
);

static bool plat__clipToScreen(
	SDL_Rect * pRectangle
);

static bool plat__contains(
	const SDL_Rect * pOuter,
	const SDL_Rect * pInner
);

static bool plat__unionIsRectangle(
	const SDL_Rect * pFirst,
	const SDL_Rect * pSecond
);

static void plat__dropCovered(
	plat__Page *	 pPage,
	const SDL_Rect * pArea
);

static void plat__addDraw(
	plat__Page *	   pPage,
	const plat__Draw * pDraw
);

static void plat__mergeDraw(
	plat__Page * pPage,
	int			 index
);

static void plat__showImageThrough(
	plat__Page *		pPage,
	const plat__Image * pImage,
	int					left,
	int					top,
	const SDL_Rect *	pClip
);

static void plat__uploadPixelLayer(
	const plat__Page * pPage
);

static void plat__drawImageLayer(
	const plat__Page * pPage
);

static void plat__drawCursor(void);

static void plat__renderCopy(
	SDL_Texture *	 pTexture,
	const SDL_Rect * pSource,
	const SDL_Rect * pTarget
);

static SDL_Rect plat__windowRectangle(void);

static void plat__setPointer(
	int windowX,
	int windowY
);

static void plat__pushKey(
	int	 key,
	bool pressed
);

static int plat__translateKey(
	SDL_Keycode keycode
);

static void plat__openJoystick(
	SDL_JoystickID id
);

static void plat__openFirstJoystick(void);

static void plat__closeJoystick(void);

static bool plat__isOpenJoystick(
	SDL_JoystickID id
);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

static SDL_Window *	  plat__window;		  /**< The game window. */
static SDL_Renderer * plat__renderer;	  /**< Renderer of the window. */
static SDL_Texture *  plat__pixelTexture; /**< Pixel layer, ARGB. */
static SDL_Texture *  plat__scene;		  /**< Target for both layers. */
static int			  plat__renderScale;  /**< Scene size / 320x200. */

/**
 * Video memory. Page 0 is shown.
 */
static plat__Page plat__pages[PLAT_PAGE_COUNT];

/** The source images of a copy (plat_blit), taken before it. */
static plat__Draw plat__blitDraws[MAX_DRAWS];

/**
 * The page that drawing functions change.
 */
static plat__Page * plat__target = &plat__pages[0];

static uint32_t plat__palette[256];		  /**< ARGB8888 colors. */
static unsigned plat__paletteVersion = 1; /**< Changes with the palette. */

/**
 * Slot 0 is unused so that 0 means "no image".
 */
static plat__Image plat__images[MAX_IMAGES + 1];

static char plat__modDir[512];		  /**< Replacement folder, or empty. */
static char plat__dataDir[512] = "."; /**< Folder with the game files. */
static char plat__saveDir[512] = "."; /**< Folder of the saved games. */

static plat__KeyEvent plat__keys[KEY_QUEUE_SIZE]; /**< Ring buffer. */
static int			  plat__keyHead;			  /**< Next write position. */
static int			  plat__keyTail;			  /**< Next read position. */

static int		plat__pointerLeft;	 /**< Pointer column, 0..319. */
static int		plat__pointerTop;	 /**< Pointer row, 0..199. */
static unsigned plat__buttonsHeld;	 /**< PLAT_BUTTON_* bits held now. */
static unsigned plat__buttonsPushed; /**< Pressed since the last read. */

static SDL_Joystick * plat__joystick;		/**< Joystick in use, or NULL. */
static SDL_Gamepad *  plat__gamepad;		/**< Its gamepad view, or NULL. */
static unsigned		  plat__joystickPushed; /**< Pressed since the last read. */
static SDL_JoystickID plat__testJoystick;	/**< Virtual test stick, or 0. */

static plat_ImageHandle plat__cursorImage;	/**< Cursor image, 0 = hidden. */
static int				plat__cursorSource; /**< First image row shown. */
static int				plat__cursorRows;	/**< Image rows shown. */
static int				plat__cursorLeft;	/**< Column in game pixels. */
static int				plat__cursorTop;	/**< Row in game pixels. */

/**
 * Performance counter when the timer rate was last set.
 */
static uint64_t plat__timerBaseCount;

static uint32_t plat__timerBaseTicks;		 /**< Timer ticks at the base. */
static uint32_t plat__timerNumerator = 1000; /**< Rate: ticks per ... */
static uint32_t plat__timerDenominator = 1;	 /**< ... this many seconds. */

/**
 * Standard 16-color palette (6-bit DAC values).
 */
static const uint8_t plat__standardPalette[16][3] = {
	{ 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x2a },
	{ 0x00, 0x2a, 0x00 },
	{ 0x00, 0x2a, 0x2a },
	{ 0x2a, 0x00, 0x00 },
	{ 0x2a, 0x00, 0x2a },
	{ 0x2a, 0x15, 0x00 },
	{ 0x2a, 0x2a, 0x2a },
	{ 0x15, 0x15, 0x15 },
	{ 0x15, 0x15, 0x3f },
	{ 0x15, 0x3f, 0x15 },
	{ 0x15, 0x3f, 0x3f },
	{ 0x3f, 0x15, 0x15 },
	{ 0x3f, 0x15, 0x3f },
	{ 0x3f, 0x3f, 0x15 },
	{ 0x3f, 0x3f, 0x3f },
};

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Open the window and create the layers.
 *
 * \param	pTitle			Window title.
 * \param	windowScale		Initial window size multiplier: the window is
 * 							320 * scale by 240 * scale.
 * \param	renderScale		Scene resolution multiplier, which is the most
 * 							detail replacement art can show (4: 1280x800).
 *
 * \retval	true	The platform is ready.
 * \retval	false	SDL could not be set up; the reason is logged.
 *
 ******************************************************************************/
bool plat_init(
	const char * pTitle,
	int			 windowScale,
	int			 renderScale
) {
	if (windowScale < 1) {
		windowScale = 3;
	}

	if (renderScale < 1) {
		renderScale = 4;
	}

	plat__renderScale = renderScale;

	SDL_InitFlags subsystems = SDL_INIT_VIDEO | SDL_INIT_EVENTS;

	SDL_SetMainReady();

	if (!SDL_Init(subsystems)) {
		SDL_LogError(LOG_APP, "SDL_Init: %s", SDL_GetError());
		return false;
	}

	if (SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
		plat__openFirstJoystick();
	} else {
		/* The game runs without a joystick. */
		SDL_LogWarn(LOG_APP, "no joystick support: %s", SDL_GetError());
	}

	if (!plat__createWindow(pTitle, windowScale)) {
		return false;
	}

	if (!plat__createLayerTextures()) {
		return false;
	}

	memset(plat__palette, 0, sizeof plat__palette);
	plat_setDefaultPalette();

	for (int page = PLAT_PAGE_COUNT - 1; page >= 0; page--) {
		plat_selectPage(page);
		plat_clearPage();
	}

	plat__timerBaseCount = SDL_GetPerformanceCounter();
	plat__timerBaseTicks = 0;

	/* The game runs without sound. */
	(void)plat_soundOpen();

	return true;
}

/** ****************************************************************************
 *
 * \brief	Destroy all images and close the window.
 *
 ******************************************************************************/
void plat_shutdown(void) {
	plat_soundClose();

	for (int id = 1; id <= MAX_IMAGES; id++) {
		if (plat__images[id].used) {
			plat_imageDestroy(id);
		}
	}

	if (plat__scene) {
		SDL_DestroyTexture(plat__scene);
	}

	if (plat__pixelTexture) {
		SDL_DestroyTexture(plat__pixelTexture);
	}

	if (plat__renderer) {
		SDL_DestroyRenderer(plat__renderer);
	}

	if (plat__window) {
		SDL_DestroyWindow(plat__window);
	}

	plat__scene = NULL;
	plat__pixelTexture = NULL;
	plat__renderer = NULL;
	plat__window = NULL;

	plat__closeJoystick();

	if (plat__testJoystick != 0) {
		SDL_DetachVirtualJoystick(plat__testJoystick);
		plat__testJoystick = 0;
	}

	SDL_Quit();
}

/** ****************************************************************************
 *
 * \brief	Set the window title.
 *
 * \param	pTitle			New title.
 *
 ******************************************************************************/
void plat_setTitle(
	const char * pTitle
) {
	if (plat__window) {
		SDL_SetWindowTitle(plat__window, pTitle);
	}
}

/** ****************************************************************************
 *
 * \brief	Set palette entries from VGA DAC values.
 *
 * \param	first			First palette entry.
 * \param	count			Number of entries.
 * \param	pDacColors		count * 3 bytes: red, green, blue, 0..63 each.
 *
 ******************************************************************************/
void plat_setPaletteVga(
	int				first,
	int				count,
	const uint8_t * pDacColors
) {
	for (int index = 0; index < count; index++) {
		const uint8_t * pDac = pDacColors + index * 3;

		plat_setPaletteEntry(
			first + index,
			plat__dacTo8Bit(pDac[0]),
			plat__dacTo8Bit(pDac[1]),
			plat__dacTo8Bit(pDac[2])
		);
	}
}

/** ****************************************************************************
 *
 * \brief	Set one palette entry from 8-bit values.
 *
 * \param	index			Palette entry, 0..255.
 * \param	red				Red, 0..255.
 * \param	green			Green, 0..255.
 * \param	blue			Blue, 0..255.
 *
 * \details	Original images are rebuilt with the new color the next time
 * 			they are drawn.
 *
 ******************************************************************************/
void plat_setPaletteEntry(
	int		index,
	uint8_t red,
	uint8_t green,
	uint8_t blue
) {
	if (index < 0 || index > 255) {
		return;
	}

	uint32_t color = 0xff000000u | ((uint32_t)red << 16) |
		((uint32_t)green << 8) | blue;

	if (plat__palette[index] != color) {
		plat__palette[index] = color;
		plat__paletteVersion++;
	}
}

/** ****************************************************************************
 *
 * \brief	Reset entries 0..15 to the standard 16-color palette.
 *
 ******************************************************************************/
void plat_setDefaultPalette(void) {
	plat_setPaletteVga(0, 16, &plat__standardPalette[0][0]);
}

/** ****************************************************************************
 *
 * \brief	Select the page that the drawing functions change.
 *
 * \param	page			0..PLAT_PAGE_COUNT - 1; page 0 is shown.
 *
 * \details	Affects plat_framebuffer, plat_clearPixels, plat_clearPage,
 * 			plat_fillRect, plat_drawImage, plat_drawImageRows and
 * 			plat_clearImages. Invalid pages are ignored.
 *
 ******************************************************************************/
void plat_selectPage(
	int page
) {
	if (page >= 0 && page < PLAT_PAGE_COUNT) {
		plat__target = &plat__pages[page];
	}
}

/** ****************************************************************************
 *
 * \brief	Get the pixel layer of the selected page.
 *
 * \return	PLAT_WIDTH * PLAT_HEIGHT palette indices, row by row.
 *
 ******************************************************************************/
uint8_t * plat_framebuffer(void) {
	return plat__target->pixels;
}

/** ****************************************************************************
 *
 * \brief	Fill the pixel layer of the selected page with PLAT_TRANSPARENT.
 *
 ******************************************************************************/
void plat_clearPixels(void) {
	uint8_t * pPixels = plat__target->pixels;

	memset(pPixels, PLAT_TRANSPARENT, sizeof plat__target->pixels);
}

/** ****************************************************************************
 *
 * \brief	Clear the selected page to color 0, like cleared video memory.
 *
 * \details	Removes all images and fills the pixel layer with index 0.
 *
 ******************************************************************************/
void plat_clearPage(void) {
	plat_clearImages();
	memset(plat__target->pixels, 0, sizeof plat__target->pixels);
}

/** ****************************************************************************
 *
 * \brief	Fill a rectangle of the selected page with one color.
 *
 * \param	left			Left edge in game pixels.
 * \param	top				Top edge in game pixels.
 * \param	width			Width in game pixels.
 * \param	height			Height in game pixels.
 * \param	color			Palette index.
 *
 * \details	The rectangle is clipped to the screen. It covers the images
 * 			below it; images completely inside it are removed.
 *
 ******************************************************************************/
void plat_fillRect(
	int		left,
	int		top,
	int		width,
	int		height,
	uint8_t color
) {
	SDL_Rect area = { left, top, width, height };

	if (!plat__clipToScreen(&area)) {
		return;
	}

	for (int row = area.y; row < area.y + area.h; row++) {
		uint8_t * pRow = plat__target->pixels + row * PLAT_WIDTH;

		memset(pRow + area.x, color, area.w);
	}

	plat__dropCovered(plat__target, &area);
}

/** ****************************************************************************
 *
 * rief	Copy a rectangle from one page to another.
 *
 * \param	sourcePage		Page to copy from.
 * \param	destinationPage	Page to copy to.
 * \param	left			Left edge in game pixels.
 * \param	top				Top edge in game pixels.
 * \param	width			Width in game pixels.
 * \param	height			Height in game pixels.
 *
 * \details	plat_blit to the same place.
 *
 ******************************************************************************/
void plat_copyRect(
	int sourcePage,
	int destinationPage,
	int left,
	int top,
	int width,
	int height
) {
	plat_blit(sourcePage, left, top, destinationPage, left, top, width, height);
}

/** ****************************************************************************
 *
 * rief	Copy a rectangle from one page to another place on another
 * 			page.
 *
 * \param	sourcePage		Page to copy from.
 * \param	sourceLeft		Left edge on the source in game pixels.
 * \param	sourceTop		Top edge on the source in game pixels.
 * \param	destinationPage	Page to copy to.
 * \param	left			Left edge on the destination.
 * \param	top				Top edge on the destination.
 * \param	width			Width in game pixels.
 * \param	height			Height in game pixels.
 *
 * \details	Copies the pixel layer and the visible parts of the images,
 * 			so replacement pictures stay pictures. Afterwards the rectangle
 * 			on the destination shows exactly what the source shows in its
 * 			rectangle. Both rectangles are clipped to the screen. Source
 * 			and destination may be on one page, also overlapping.
 *
 ******************************************************************************/
void plat_blit(
	int sourcePage,
	int sourceLeft,
	int sourceTop,
	int destinationPage,
	int left,
	int top,
	int width,
	int height
) {
	bool valid = sourcePage >= 0 && sourcePage < PLAT_PAGE_COUNT &&
		destinationPage >= 0 && destinationPage < PLAT_PAGE_COUNT;
	int		 shiftLeft = left - sourceLeft;
	int		 shiftTop = top - sourceTop;
	SDL_Rect area = { sourceLeft, sourceTop, width, height };

	if (!valid) {
		return;
	}

	if (!plat__clipToScreen(&area)) {
		return;
	}

	area.x += shiftLeft;
	area.y += shiftTop;

	if (!plat__clipToScreen(&area)) {
		return;
	}

	plat__Page * pSource = &plat__pages[sourcePage];
	plat__Page * pDestination = &plat__pages[destinationPage];
	int			 copied = 0;

	/*
	 * The source images are taken first: within one page, dropping and
	 * adding draws below would change the list being read.
	 */
	for (int index = 0; index < pSource->drawCount; index++) {
		plat__Draw draw = pSource->draws[index];

		draw.left += shiftLeft;
		draw.top += shiftTop;
		draw.clip.x += shiftLeft;
		draw.clip.y += shiftTop;

		if (SDL_GetRectIntersection(&draw.clip, &area, &draw.clip)) {
			plat__blitDraws[copied] = draw;
			copied++;
		}
	}

	plat__dropCovered(pDestination, &area);

	/*
	 * Rows moved down within one page are copied last row first, so
	 * overlapping rectangles keep the source's content.
	 */
	for (int step = 0; step < area.h; step++) {
		int row = shiftTop > 0 ? area.y + area.h - 1 - step : area.y + step;
		int output = row * PLAT_WIDTH + area.x;
		int input = (row - shiftTop) * PLAT_WIDTH + area.x - shiftLeft;

		memmove(
			pDestination->pixels + output,
			pSource->pixels + input,
			(size_t)area.w
		);
	}

	/*
	 * Where the source pixels are transparent, the topmost source image
	 * shows; adding the source images in their order after the older
	 * destination images reproduces that.
	 */
	for (int index = 0; index < copied; index++) {
		plat__addDraw(pDestination, &plat__blitDraws[index]);
	}
}

/** ****************************************************************************
 *
 * \brief	Read the original pixels a page shows in a rectangle.
 *
 * \param	page			Page to read.
 * \param	left			Left edge in game pixels.
 * \param	top				Top edge in game pixels.
 * \param	width			Width in game pixels.
 * \param	height			Height in game pixels.
 * \param	pPixels			Output: width * height palette indices.
 *
 * \details	Each pixel is the pixel layer's, or where that is
 * 			PLAT_TRANSPARENT the original (not replaced) pixel of the
 * 			topmost image drawn there that is not see-through, or 0.
 * 			Pixels off the screen are 0.
 *
 ******************************************************************************/
void plat_readPixels(
	int		  page,
	int		  left,
	int		  top,
	int		  width,
	int		  height,
	uint8_t * pPixels
) {
	memset(pPixels, 0, (size_t)width * (size_t)height);

	if (page < 0 || page >= PLAT_PAGE_COUNT) {
		return;
	}

	const plat__Page * pPage = &plat__pages[page];

	for (int row = 0; row < height; row++) {
		int screenRow = top + row;

		if (screenRow < 0 || screenRow >= PLAT_HEIGHT) {
			continue;
		}

		for (int column = 0; column < width; column++) {
			int		screenColumn = left + column;
			uint8_t value;

			if (screenColumn < 0 || screenColumn >= PLAT_WIDTH) {
				continue;
			}

			value = pPage->pixels[screenRow * PLAT_WIDTH + screenColumn];

			for (int index = pPage->drawCount - 1;
				 value == PLAT_TRANSPARENT && index >= 0;
				 index--) {
				const plat__Draw *	pDraw = &pPage->draws[index];
				const plat__Image * pImage = plat__getImage(pDraw->image);
				SDL_Point			point = { screenColumn, screenRow };

				if (!pImage || !SDL_PointInRect(&point, &pDraw->clip)) {
					continue;
				}

				size_t	imageRow = (size_t)(screenRow - pDraw->top);
				size_t	imageColumn = (size_t)(screenColumn - pDraw->left);
				uint8_t imagePixel =
					pImage->pPixels[imageRow * pImage->width + imageColumn];

				if (imagePixel != pImage->seeThrough) {
					value = imagePixel;
				}
			}

			pPixels[row * width + column] =
				value == PLAT_TRANSPARENT ? 0 : value;
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Set the folder searched for replacement PNGs.
 *
 * \param	pDirectory		Folder, searched as <directory>/<name>.png.
 * 							NULL turns replacements off.
 *
 * \details	Only images created after this call use the new folder.
 *
 ******************************************************************************/
void plat_setModDir(
	const char * pDirectory
) {
	if (pDirectory) {
		snprintf(plat__modDir, sizeof plat__modDir, "%s", pDirectory);
	} else {
		plat__modDir[0] = '\0';
	}
}

/** ****************************************************************************
 *
 * \brief	Set the folder that holds the original game files.
 *
 * \param	pDirectory		Folder used by plat_loadFile. NULL means the
 * 							current folder.
 *
 ******************************************************************************/
void plat_setDataDir(
	const char * pDirectory
) {
	const char * pFolder = pDirectory ? pDirectory : ".";

	snprintf(plat__dataDir, sizeof plat__dataDir, "%s", pFolder);
}

/** ****************************************************************************
 *
 * \brief	Load a game file.
 *
 * \param	pName			File name in the data folder, e.g. "Lib1".
 * \param	pSize			Output: the file size in bytes.
 *
 * \return	The file contents, read-only, to be released with
 * 			plat_freeFile. NULL if the file cannot be read; the reason is
 * 			logged.
 *
 * \details	The PC platform reads the file from the folder set with
 * 			plat_setDataDir. A platform without files may return a pointer
 * 			to data in flash instead.
 *
 ******************************************************************************/
const uint8_t * plat_loadFile(
	const char * pName,
	size_t *	 pSize
) {
	char path[640];

	snprintf(path, sizeof path, "%s/%s", plat__dataDir, pName);

	FILE * pFile = fopen(path, "rb");

	if (!pFile) {
		plat_log("cannot open %s", path);
		return NULL;
	}

	fseek(pFile, 0, SEEK_END);

	long size = ftell(pFile);

	fseek(pFile, 0, SEEK_SET);

	uint8_t * pData = size > 0 ? malloc((size_t)size) : NULL;
	size_t	  bytesRead = pData ? fread(pData, 1, (size_t)size, pFile) : 0;
	bool	  complete = pData && bytesRead == (size_t)size;

	fclose(pFile);

	if (!complete) {
		plat_log("cannot read %s", path);
		free(pData);
		return NULL;
	}

	*pSize = (size_t)size;

	return pData;
}

/** ****************************************************************************
 *
 * \brief	Release a file loaded with plat_loadFile or plat_readSave.
 *
 * \param	pData			File contents, or NULL.
 *
 ******************************************************************************/
void plat_freeFile(
	const uint8_t * pData
) {
	free((void *)pData);
}

/** ****************************************************************************
 *
 * \brief	Set the folder for saved games.
 *
 * \param	pDirectory		Folder used by plat_readSave, plat_writeSave and
 * 							plat_deleteSave. NULL means the current folder.
 *
 ******************************************************************************/
void plat_setSaveDir(
	const char * pDirectory
) {
	const char * pFolder = pDirectory ? pDirectory : ".";

	snprintf(plat__saveDir, sizeof plat__saveDir, "%s", pFolder);
}

/** ****************************************************************************
 *
 * \brief	Read a saved game file.
 *
 * \param	pName			File name, e.g. "HOTROD1.SAV".
 * \param	pSize			Output: the file size in bytes.
 *
 * \return	The file contents, to be released with plat_freeFile, or NULL
 * 			when there is no such file or it cannot be read (not logged:
 * 			most save slots are empty).
 *
 * \details	The PC platform reads the file from the folder set with
 * 			plat_setSaveDir. An empty file gives NULL too.
 *
 ******************************************************************************/
uint8_t * plat_readSave(
	const char * pName,
	size_t *	 pSize
) {
	char path[640];

	snprintf(path, sizeof path, "%s/%s", plat__saveDir, pName);

	FILE * pFile = fopen(path, "rb");

	if (!pFile) {
		return NULL;
	}

	fseek(pFile, 0, SEEK_END);

	long size = ftell(pFile);

	fseek(pFile, 0, SEEK_SET);

	uint8_t * pData = size > 0 ? malloc((size_t)size) : NULL;
	size_t	  bytesRead = pData ? fread(pData, 1, (size_t)size, pFile) : 0;
	bool	  complete = pData && bytesRead == (size_t)size;

	fclose(pFile);

	if (!complete) {
		free(pData);
		return NULL;
	}

	*pSize = (size_t)size;

	return pData;
}

/** ****************************************************************************
 *
 * \brief	Write a saved game file, replacing one of the same name.
 *
 * \param	pName			File name, e.g. "HOTROD1.SAV".
 * \param	pData			The contents.
 * \param	size			Bytes in pData.
 *
 * \retval	true	The file was written completely.
 * \retval	false	It could not be written (logged); a partly written
 * 					file is deleted.
 *
 * \details	The PC platform creates the folder set with plat_setSaveDir
 * 			when it does not exist yet.
 *
 ******************************************************************************/
bool plat_writeSave(
	const char *	pName,
	const uint8_t * pData,
	size_t			size
) {
	char path[640];

	plat__makeFolder(plat__saveDir);
	snprintf(path, sizeof path, "%s/%s", plat__saveDir, pName);

	FILE * pFile = fopen(path, "wb");

	if (!pFile) {
		plat_log("cannot create %s", path);
		return false;
	}

	size_t written = fwrite(pData, 1, size, pFile);
	bool   complete = written == size;

	if (fclose(pFile) != 0) {
		complete = false;
	}

	if (!complete) {
		plat_log("cannot write %s", path);
		remove(path);
	}

	return complete;
}

/** ****************************************************************************
 *
 * \brief	Delete a saved game file.
 *
 * \param	pName			File name, e.g. "HOTROD1.SAV". A missing file is
 * 							no error.
 *
 ******************************************************************************/
void plat_deleteSave(
	const char * pName
) {
	char path[640];

	snprintf(path, sizeof path, "%s/%s", plat__saveDir, pName);
	remove(path);
}

/** ****************************************************************************
 *
 * \brief	Create a folder if it does not exist.
 *
 * \param	pFolder			Path of the folder.
 *
 * \details	Errors (such as the folder existing already) are ignored;
 * 			writing a file into it reports a missing folder.
 *
 ******************************************************************************/
static void plat__makeFolder(
	const char * pFolder
) {
#ifdef _WIN32
	(void)_mkdir(pFolder);
#else
	(void)mkdir(pFolder, 0777);
#endif
}

/** ****************************************************************************
 *
 * \brief	Write a diagnostic message.
 *
 * \param	pFormat			printf-style format.
 * \param	...				Values for the format.
 *
 * \details	The PC platform writes to the SDL log (the console).
 *
 ******************************************************************************/
void plat_log(
	const char * pFormat,
	...
) {
	va_list arguments;

	va_start(arguments, pFormat);
	SDL_LogMessageV(LOG_APP, SDL_LOG_PRIORITY_INFO, pFormat, arguments);
	va_end(arguments);
}

/** ****************************************************************************
 *
 * \brief	Create an image from indexed pixels.
 *
 * \param	pName			Name for replacement lookup, e.g. "lib1/000".
 * \param	width			Width in game pixels.
 * \param	height			Height in game pixels.
 * \param	pPixels			width * height palette indices.
 * \param	transparent0	Index 0 is see-through.
 *
 * \return	Handle of the image, or 0 if it could not be created.
 *
 * \details	The pixels are copied. If the mod folder has <name>.png, that
 * 			picture is drawn instead of the pixels.
 *
 ******************************************************************************/
plat_ImageHandle plat_imageCreate(
	const char *	pName,
	int				width,
	int				height,
	const uint8_t * pPixels,
	bool			transparent0
) {
	return plat__createImage(
		pName,
		width,
		height,
		pPixels,
		transparent0 ? 0 : -1
	);
}

/** ****************************************************************************
 *
 * \brief	Create an image whose PLAT_TRANSPARENT pixels are see-through.
 *
 * \param	pName			Name for replacement lookup, or NULL.
 * \param	width			Width in game pixels.
 * \param	height			Height in game pixels.
 * \param	pPixels			width * height palette indices.
 *
 * \return	Handle of the image, or 0 if it could not be created.
 *
 * \details	For pictures cut out of a page by a mask, where every palette
 * 			index (also 0) must stay visible.
 *
 ******************************************************************************/
plat_ImageHandle plat_imageCreateOverlay(
	const char *	pName,
	int				width,
	int				height,
	const uint8_t * pPixels
) {
	return plat__createImage(pName, width, height, pPixels, PLAT_TRANSPARENT);
}

/** ****************************************************************************
 *
 * \brief	Create an image from indexed pixels.
 *
 * \param	pName			Name for replacement lookup, or NULL.
 * \param	width			Width in game pixels.
 * \param	height			Height in game pixels.
 * \param	pPixels			width * height palette indices.
 * \param	seeThrough		Index that is see-through, or -1 for none.
 *
 * \return	Handle of the image, or 0 if it could not be created.
 *
 ******************************************************************************/
static plat_ImageHandle plat__createImage(
	const char *	pName,
	int				width,
	int				height,
	const uint8_t * pPixels,
	int				seeThrough
) {
	size_t pixelCount = (size_t)width * (size_t)height;

	for (int id = 1; id <= MAX_IMAGES; id++) {
		plat__Image * pImage = &plat__images[id];

		if (pImage->used && pImage->released && !plat__isDrawn(id)) {
			plat__freeImage(pImage);
		}
	}

	for (int id = 1; id <= MAX_IMAGES; id++) {
		plat__Image * pImage = &plat__images[id];

		if (pImage->used) {
			continue;
		}

		memset(pImage, 0, sizeof *pImage);
		pImage->pPixels = malloc(pixelCount);

		if (!pImage->pPixels) {
			return 0;
		}

		memcpy(pImage->pPixels, pPixels, pixelCount);
		pImage->used = true;
		snprintf(pImage->name, sizeof pImage->name, "%s", pName ? pName : "");
		pImage->width = width;
		pImage->height = height;
		pImage->seeThrough = seeThrough;
		pImage->pReplacement = pName ? plat__loadReplacement(pName) : NULL;

		return id;
	}

	SDL_LogError(LOG_APP, "too many images");

	return 0;
}

/** ****************************************************************************
 *
 * \brief	Destroy an image.
 *
 * \param	id				Handle of the image.
 *
 * \details	An image still drawn on a page stays until no page draws it
 * 			(it is freed when an image is created), so the page keeps
 * 			showing it, as video memory kept a sprite's pixels, and its
 * 			handle is not given to a new image meanwhile.
 *
 ******************************************************************************/
void plat_imageDestroy(
	plat_ImageHandle id
) {
	plat__Image * pImage = plat__getImage(id);

	if (!pImage) {
		return;
	}

	if (plat__isDrawn(id)) {
		pImage->released = true;
		return;
	}

	plat__freeImage(pImage);
}

/** ****************************************************************************
 *
 * \brief	Check if an image is drawn from a replacement PNG.
 *
 * \param	id				Handle of the image.
 *
 * \retval	true	A replacement PNG was found.
 * \retval	false	The original pixels are drawn.
 *
 ******************************************************************************/
bool plat_imageIsReplaced(
	plat_ImageHandle id
) {
	const plat__Image * pImage = plat__getImage(id);

	return pImage && pImage->pReplacement;
}

/** ****************************************************************************
 *
 * \brief	Add an image to the image layer of the selected page.
 *
 * \param	image			Handle of the image.
 * \param	left			Left edge in 320x200 coordinates.
 * \param	top				Top edge in 320x200 coordinates.
 *
 * \details	The image stays on the layer until it is covered, cleared or
 * 			plat_clearImages is called. Later draws cover earlier ones.
 *
 ******************************************************************************/
void plat_drawImage(
	plat_ImageHandle image,
	int				 left,
	int				 top
) {
	const plat__Image * pImage = plat__getImage(image);

	if (pImage) {
		plat_drawImageRows(image, left, top, pImage->height);
	}
}

/** ****************************************************************************
 *
 * \brief	Add the top rows of an image to the selected page.
 *
 * \param	image			Handle of the image.
 * \param	left			Left edge in 320x200 coordinates.
 * \param	top				Top edge in 320x200 coordinates.
 * \param	rows			Number of rows shown, from the top of the image.
 *
 * \details	Like plat_drawImage, for blits whose height the game clips.
 * 			The pixel layer becomes transparent where the image is opaque,
 * 			so the image covers earlier pixel drawing.
 *
 ******************************************************************************/
void plat_drawImageRows(
	plat_ImageHandle image,
	int				 left,
	int				 top,
	int				 rows
) {
	plat__Image * pImage = plat__getImage(image);

	if (!pImage) {
		return;
	}

	if (rows > pImage->height) {
		rows = pImage->height;
	}

	plat__Draw draw = {
		.image = image,
		.left = left,
		.top = top,
		.clip = { left, top, pImage->width, rows },
	};

	if (!plat__clipToScreen(&draw.clip)) {
		return;
	}

	plat__showImageThrough(plat__target, pImage, left, top, &draw.clip);
	plat__addDraw(plat__target, &draw);
}

/** ****************************************************************************
 *
 * \brief	Add a part of an image to the selected page.
 *
 * \param	image			Handle of the image.
 * \param	sourceLeft		Left edge of the part in the image.
 * \param	sourceTop		Top edge of the part in the image.
 * \param	width			Width of the part.
 * \param	height			Height of the part.
 * \param	left			Where the part's left edge goes, in 320x200
 * 							coordinates.
 * \param	top				Where the part's top edge goes.
 * \param	pClip			Rectangle the draw is limited to as well
 * 							(left, top, width, height), or NULL.
 *
 * \details	For frames of a sprite sheet and for blits the game clips.
 * 			A replacement image is cut at the same place, scaled.
 *
 ******************************************************************************/
void plat_drawImagePart(
	plat_ImageHandle image,
	int				 sourceLeft,
	int				 sourceTop,
	int				 width,
	int				 height,
	int				 left,
	int				 top,
	const int *		 pClip
) {
	const plat__Image * pImage = plat__getImage(image);

	if (!pImage || sourceLeft < 0 || sourceTop < 0) {
		return;
	}

	if (width > pImage->width - sourceLeft) {
		width = pImage->width - sourceLeft;
	}

	if (height > pImage->height - sourceTop) {
		height = pImage->height - sourceTop;
	}

	if (width <= 0 || height <= 0) {
		return;
	}

	plat__Draw draw = {
		.image = image,
		.left = left - sourceLeft,
		.top = top - sourceTop,
		.clip = { left, top, width, height },
	};

	if (pClip) {
		SDL_Rect limit = { pClip[0], pClip[1], pClip[2], pClip[3] };
		SDL_Rect both;

		if (!SDL_GetRectIntersection(&draw.clip, &limit, &both)) {
			return;
		}

		draw.clip = both;
	}

	if (!plat__clipToScreen(&draw.clip)) {
		return;
	}

	plat__showImageThrough(
		plat__target,
		pImage,
		draw.left,
		draw.top,
		&draw.clip
	);
	plat__addDraw(plat__target, &draw);
}

/** ****************************************************************************
 *
 * \brief	Remove everything from the image layer of the selected page.
 *
 ******************************************************************************/
void plat_clearImages(void) {
	plat__target->drawCount = 0;
	plat__target->drawsOverflowed = false;
}

/** ****************************************************************************
 *
 * \brief	Set the cursor that is drawn over the picture.
 *
 * \param	image			Image holding the cursor shape; 0 hides the
 * 							cursor.
 * \param	sourceTop		First row of the image that is shown.
 * \param	rows			Number of image rows shown.
 * \param	left			Column of the cursor's left edge in game pixels.
 * \param	top				Row of the cursor's top edge in game pixels.
 *
 * \details	The cursor is not part of any page: plat_present draws it
 * 			last, over both layers of page 0. The host's own pointer is
 * 			hidden over the window while a cursor is set.
 *
 ******************************************************************************/
void plat_setCursor(
	plat_ImageHandle image,
	int				 sourceTop,
	int				 rows,
	int				 left,
	int				 top
) {
	bool shown = image != 0;

	if (shown != (plat__cursorImage != 0)) {
		if (shown) {
			SDL_HideCursor();
		} else {
			SDL_ShowCursor();
		}
	}

	plat__cursorImage = image;
	plat__cursorSource = sourceTop;
	plat__cursorRows = rows;
	plat__cursorLeft = left;
	plat__cursorTop = top;
}

/** ****************************************************************************
 *
 * \brief	The time of day.
 *
 * \return	Seconds since 1970 (PC), or any value that differs from run
 * 			to run; the game seeds its random numbers with it.
 *
 ******************************************************************************/
uint32_t plat_timeSeconds(void) {
	return (uint32_t)time(NULL);
}

/** ****************************************************************************
 *
 * \brief	Compose both layers of page 0 and show them in the window.
 *
 ******************************************************************************/
void plat_present(void) {
	SDL_SetRenderTarget(plat__renderer, plat__scene);
	SDL_SetRenderDrawColor(plat__renderer, 0, 0, 0, 255);
	SDL_RenderClear(plat__renderer);

	plat__drawImageLayer(&plat__pages[0]);

	plat__uploadPixelLayer(&plat__pages[0]);
	SDL_RenderTexture(plat__renderer, plat__pixelTexture, NULL, NULL);
	plat__drawCursor();
	SDL_SetRenderTarget(plat__renderer, NULL);

	SDL_Rect target = plat__windowRectangle();

	SDL_SetRenderDrawColor(plat__renderer, 0, 0, 0, 255);
	SDL_RenderClear(plat__renderer);
	plat__renderCopy(plat__scene, NULL, &target);
	SDL_RenderPresent(plat__renderer);
}

/** ****************************************************************************
 *
 * \brief	Handle window and input events.
 *
 * \retval	true	Keep running.
 * \retval	false	The user closed the window.
 *
 ******************************************************************************/
bool plat_poll(void) {
	SDL_Event event;

	while (SDL_PollEvent(&event)) {
		switch (event.type) {
			case SDL_EVENT_QUIT:
				return false;

			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_KEY_UP: {
				/*
				 * Without modifiers, like SDL2: letters stay lower case
				 * and keypad keys stay keypad keys with Num Lock on.
				 */
				SDL_Scancode scancode = event.key.scancode;
				SDL_Keycode	 keycode =
					SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, true);
				int	 key = plat__translateKey(keycode);
				bool pressed = event.type == SDL_EVENT_KEY_DOWN;

				if (key != PLAT_KEY_NONE) {
					plat__pushKey(key, pressed);
				}

				break;
			}

			case SDL_EVENT_MOUSE_MOTION:
				plat__setPointer((int)event.motion.x, (int)event.motion.y);
				break;

			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP: {
				unsigned button = 0;

				if (event.button.button == SDL_BUTTON_LEFT) {
					button = PLAT_BUTTON_LEFT;
				} else if (event.button.button == SDL_BUTTON_RIGHT) {
					button = PLAT_BUTTON_RIGHT;
				}

				plat__setPointer((int)event.button.x, (int)event.button.y);

				if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
					plat__buttonsHeld |= button;
					plat__buttonsPushed |= button;
				} else {
					plat__buttonsHeld &= ~button;
				}

				break;
			}

			case SDL_EVENT_JOYSTICK_ADDED:
				if (plat__joystick == NULL) {
					plat__openJoystick(event.jdevice.which);
				}

				break;

			case SDL_EVENT_JOYSTICK_REMOVED:
				if (plat__isOpenJoystick(event.jdevice.which)) {
					plat__closeJoystick();
					plat__openFirstJoystick();
				}

				break;

			case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
				if (plat__gamepad == NULL ||
					!plat__isOpenJoystick(event.gbutton.which)) {
					break;
				}

				if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
					plat__joystickPushed |= PLAT_JOY_BUTTON_1;
				} else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) {
					plat__joystickPushed |= PLAT_JOY_BUTTON_2;
				}

				break;

			case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
				if (plat__gamepad != NULL ||
					!plat__isOpenJoystick(event.jbutton.which)) {
					break;
				}

				if (event.jbutton.button == 0) {
					plat__joystickPushed |= PLAT_JOY_BUTTON_1;
				} else if (event.jbutton.button == 1) {
					plat__joystickPushed |= PLAT_JOY_BUTTON_2;
				}

				break;

			default:
				break;
		}
	}

	return true;
}

/** ****************************************************************************
 *
 * \brief	Get the next key pressed.
 *
 * \return	The key as an ASCII or PLAT_KEY_* code, or PLAT_KEY_NONE when
 * 			none is waiting.
 *
 * \details	Key releases waiting in the queue are skipped.
 *
 ******************************************************************************/
int plat_getKey(void) {
	int	 key;
	bool pressed;

	while (plat_getKeyEvent(&key, &pressed)) {
		if (pressed) {
			return key;
		}
	}

	return PLAT_KEY_NONE;
}

/** ****************************************************************************
 *
 * \brief	Get the next key going down or up.
 *
 * \param	pKey			Output: the key as an ASCII or PLAT_KEY_* code.
 * \param	pPressed		Output: true when pressed (or repeated by the
 * 							typematic), false when released.
 *
 * \retval	true	An event was read.
 * \retval	false	No event is waiting.
 *
 * \details	For code that needs releases, like the original keyboard
 * 			interrupt handler, which saw make and break scan codes.
 *
 ******************************************************************************/
bool plat_getKeyEvent(
	int *  pKey,
	bool * pPressed
) {
	if (plat__keyTail == plat__keyHead) {
		return false;
	}

	*pKey = plat__keys[plat__keyTail].key;
	*pPressed = plat__keys[plat__keyTail].pressed;
	plat__keyTail = (plat__keyTail + 1) % KEY_QUEUE_SIZE;

	return true;
}

/** ****************************************************************************
 *
 * \brief	Get the pointer position and buttons.
 *
 * \param	pLeft			Output: column under the pointer, 0..319.
 * \param	pTop			Output: row under the pointer, 0..199.
 * \param	pButtons		Output: PLAT_BUTTON_* bits of the buttons held,
 * 							plus those pressed since the last call.
 *
 * \details	A click shorter than the time between two calls is still
 * 			reported once. On a touch screen, touching is the left button.
 *
 ******************************************************************************/
void plat_getPointer(
	int *	   pLeft,
	int *	   pTop,
	unsigned * pButtons
) {
	*pLeft = plat__pointerLeft;
	*pTop = plat__pointerTop;
	*pButtons = plat__buttonsHeld | plat__buttonsPushed;
	plat__buttonsPushed = 0;
}

/** ****************************************************************************
 *
 * \brief	Get the joystick position and buttons.
 *
 * \param	pHorizontal		Output: -32768 (left) .. 32767 (right).
 * \param	pVertical		Output: -32768 (up) .. 32767 (down).
 * \param	pButtons		Output: PLAT_JOY_BUTTON_* bits of the buttons
 * 							held, plus those pressed since the last call.
 *
 * \retval	true	A joystick is connected; the outputs are set.
 * \retval	false	None is connected; the outputs are not changed.
 *
 * \details	A gamepad reports its left stick, overridden by the D-pad,
 * 			and the buttons A and B. Another joystick reports its first
 * 			two axes, overridden by its first hat, and its first two
 * 			buttons. The first device found is used; when it goes, the
 * 			next one.
 *
 ******************************************************************************/
bool plat_getJoystick(
	int16_t *  pHorizontal,
	int16_t *  pVertical,
	unsigned * pButtons
) {
	if (plat__joystick == NULL) {
		plat__joystickPushed = 0;
		return false;
	}

	int16_t	 horizontal = 0;
	int16_t	 vertical = 0;
	unsigned buttons = 0;
	bool	 left;
	bool	 right;
	bool	 up;
	bool	 down;

	if (plat__gamepad != NULL) {
		SDL_Gamepad * pPad = plat__gamepad;

		horizontal = SDL_GetGamepadAxis(pPad, SDL_GAMEPAD_AXIS_LEFTX);
		vertical = SDL_GetGamepadAxis(pPad, SDL_GAMEPAD_AXIS_LEFTY);
		left = SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
		right = SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
		up = SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_DPAD_UP);
		down = SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);

		if (SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_SOUTH)) {
			buttons |= PLAT_JOY_BUTTON_1;
		}

		if (SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_EAST)) {
			buttons |= PLAT_JOY_BUTTON_2;
		}
	} else {
		SDL_Joystick * pStick = plat__joystick;
		uint8_t		   hat = SDL_HAT_CENTERED;

		if (SDL_GetNumJoystickAxes(pStick) >= 2) {
			horizontal = SDL_GetJoystickAxis(pStick, 0);
			vertical = SDL_GetJoystickAxis(pStick, 1);
		}

		if (SDL_GetNumJoystickHats(pStick) >= 1) {
			hat = SDL_GetJoystickHat(pStick, 0);
		}

		left = (hat & SDL_HAT_LEFT) != 0;
		right = (hat & SDL_HAT_RIGHT) != 0;
		up = (hat & SDL_HAT_UP) != 0;
		down = (hat & SDL_HAT_DOWN) != 0;

		if (SDL_GetJoystickButton(pStick, 0)) {
			buttons |= PLAT_JOY_BUTTON_1;
		}

		if (SDL_GetJoystickButton(pStick, 1)) {
			buttons |= PLAT_JOY_BUTTON_2;
		}
	}

	if (left) {
		horizontal = INT16_MIN;
	} else if (right) {
		horizontal = INT16_MAX;
	}

	if (up) {
		vertical = INT16_MIN;
	} else if (down) {
		vertical = INT16_MAX;
	}

	*pHorizontal = horizontal;
	*pVertical = vertical;
	*pButtons = buttons | plat__joystickPushed;
	plat__joystickPushed = 0;

	return true;
}

/** ****************************************************************************
 *
 * \brief	Test aid: connect a virtual joystick and use it in place of
 * 			any other.
 *
 * \retval	true	The test joystick is in use, centered.
 * \retval	false	SDL could not make it; the reason is logged.
 *
 * \details	Call after plat_init. It has two axes and two buttons, set
 * 			with plat_setTestJoystick.
 *
 ******************************************************************************/
bool plat_attachTestJoystick(void) {
	SDL_VirtualJoystickDesc desc;

	SDL_INIT_INTERFACE(&desc);
	desc.type = SDL_JOYSTICK_TYPE_UNKNOWN;
	desc.naxes = 2;
	desc.nbuttons = 2;
	desc.name = "FreeRod test joystick";

	SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);

	if (id == 0) {
		SDL_LogError(LOG_APP, "test joystick: %s", SDL_GetError());
		return false;
	}

	plat__testJoystick = id;
	plat__closeJoystick();
	plat__openJoystick(id);

	return plat__joystick != NULL;
}

/** ****************************************************************************
 *
 * \brief	Test aid: move the test joystick and set its buttons.
 *
 * \param	horizontal		-32768 (left) .. 32767 (right).
 * \param	vertical		-32768 (up) .. 32767 (down).
 * \param	buttons			PLAT_JOY_BUTTON_* bits held.
 *
 * \details	Does nothing without plat_attachTestJoystick. The change is
 * 			seen after the next plat_poll.
 *
 ******************************************************************************/
void plat_setTestJoystick(
	int16_t	 horizontal,
	int16_t	 vertical,
	unsigned buttons
) {
	if (plat__testJoystick == 0 ||
		!plat__isOpenJoystick(plat__testJoystick)) {
		return;
	}

	SDL_Joystick * pStick = plat__joystick;

	SDL_SetJoystickVirtualAxis(pStick, 0, horizontal);
	SDL_SetJoystickVirtualAxis(pStick, 1, vertical);
	SDL_SetJoystickVirtualButton(
		pStick,
		0,
		(buttons & PLAT_JOY_BUTTON_1) != 0
	);
	SDL_SetJoystickVirtualButton(
		pStick,
		1,
		(buttons & PLAT_JOY_BUTTON_2) != 0
	);
}

/** ****************************************************************************
 *
 * \brief	Set the rate of the timer read with plat_timerTicks.
 *
 * \param	numerator		The timer counts numerator ticks ...
 * \param	denominator		... per denominator seconds.
 *
 * \details	The count continues from its current value at the new rate.
 * 			The DOS timer chip is numerator 1193182, denominator = its
 * 			divisor.
 *
 ******************************************************************************/
void plat_timerSetRate(
	uint32_t numerator,
	uint32_t denominator
) {
	if (numerator == 0 || denominator == 0) {
		return;
	}

	plat__timerBaseTicks = plat_timerTicks();
	plat__timerBaseCount = SDL_GetPerformanceCounter();
	plat__timerNumerator = numerator;
	plat__timerDenominator = denominator;
}

/** ****************************************************************************
 *
 * \brief	Read the timer.
 *
 * \return	Ticks counted at the rate set with plat_timerSetRate (1000 per
 * 			second until then). Wraps around after 2^32 ticks.
 *
 ******************************************************************************/
uint32_t plat_timerTicks(void) {
	uint64_t frequency = SDL_GetPerformanceFrequency();
	uint64_t elapsed = SDL_GetPerformanceCounter() - plat__timerBaseCount;

	/*
	 * Whole seconds and the rest separately, so the products cannot
	 * overflow.
	 */
	uint64_t seconds = elapsed / frequency;
	uint64_t rest = elapsed % frequency;
	uint64_t scaled = seconds * plat__timerNumerator +
		rest * plat__timerNumerator / frequency;

	return plat__timerBaseTicks + (uint32_t)(scaled / plat__timerDenominator);
}

/** ****************************************************************************
 *
 * \brief	Milliseconds since the platform was initialized.
 *
 * \return	The time in milliseconds.
 *
 ******************************************************************************/
uint32_t plat_ticksMs(void) {
	return (uint32_t)SDL_GetTicks();
}

/** ****************************************************************************
 *
 * \brief	Wait.
 *
 * \param	milliseconds	Time to wait.
 *
 ******************************************************************************/
void plat_delayMs(
	uint32_t milliseconds
) {
	SDL_Delay(milliseconds);
}

/** ****************************************************************************
 *
 * \brief	Save the pixel layer of page 0 as an 8-bit BMP (320x200).
 *
 * \param	pPath			Output file.
 *
 * \retval	true	The file was saved.
 * \retval	false	The file could not be saved.
 *
 ******************************************************************************/
bool plat_saveScreenshot(
	const char * pPath
) {
	SDL_Surface * pSurface =
		SDL_CreateSurface(PLAT_WIDTH, PLAT_HEIGHT, SDL_PIXELFORMAT_INDEX8);

	if (!pSurface) {
		return false;
	}

	SDL_Palette * pPalette = SDL_CreateSurfacePalette(pSurface);

	if (!pPalette) {
		SDL_DestroySurface(pSurface);
		return false;
	}

	SDL_Color colors[256];

	for (int index = 0; index < 256; index++) {
		colors[index].r = (uint8_t)(plat__palette[index] >> 16);
		colors[index].g = (uint8_t)(plat__palette[index] >> 8);
		colors[index].b = (uint8_t)plat__palette[index];
		colors[index].a = 255;
	}

	SDL_SetPaletteColors(pPalette, colors, 0, 256);
	SDL_LockSurface(pSurface);

	for (int row = 0; row < PLAT_HEIGHT; row++) {
		uint8_t * pOutput =
			(uint8_t *)pSurface->pixels + row * pSurface->pitch;

		memcpy(pOutput, plat__pages[0].pixels + row * PLAT_WIDTH, PLAT_WIDTH);
	}

	SDL_UnlockSurface(pSurface);

	bool saved = SDL_SaveBMP(pSurface, pPath);

	SDL_DestroySurface(pSurface);

	return saved;
}

/** ****************************************************************************
 *
 * \brief	Save the composed scene as a 32-bit BMP.
 *
 * \param	pPath			Output file.
 *
 * \retval	true	The file was saved.
 * \retval	false	The file could not be saved.
 *
 * \details	Saves the scene of the last plat_present, at render scale.
 *
 ******************************************************************************/
bool plat_saveSceneScreenshot(
	const char * pPath
) {
	SDL_SetRenderTarget(plat__renderer, plat__scene);

	SDL_Surface * pRead = SDL_RenderReadPixels(plat__renderer, NULL);

	SDL_SetRenderTarget(plat__renderer, NULL);

	if (!pRead) {
		return false;
	}

	/* The renderer may read in another format; keep the BMP 32-bit ARGB. */
	SDL_Surface * pSurface =
		SDL_ConvertSurface(pRead, SDL_PIXELFORMAT_ARGB8888);

	SDL_DestroySurface(pRead);

	if (!pSurface) {
		return false;
	}

	bool saved = SDL_SaveBMP(pSurface, pPath);

	SDL_DestroySurface(pSurface);

	return saved;
}

/** ****************************************************************************
 *
 * \brief	Convert a 6-bit VGA DAC value to 8 bits.
 *
 * \param	value			DAC value, 0..63.
 *
 * \return	The value scaled to 0..255.
 *
 ******************************************************************************/
static uint8_t plat__dacTo8Bit(
	uint8_t value
) {
	value &= 0x3f;

	return (uint8_t)((value << 2) | (value >> 4));
}

/** ****************************************************************************
 *
 * \brief	Create the window and its renderer.
 *
 * \param	pTitle			Window title.
 * \param	windowScale		Window size multiplier.
 *
 * \retval	true	The window and renderer were created.
 * \retval	false	SDL failed; the reason is logged.
 *
 ******************************************************************************/
static bool plat__createWindow(
	const char * pTitle,
	int			 windowScale
) {
	/* SDL3 centers new windows. */
	plat__window = SDL_CreateWindow(
		pTitle,
		PLAT_WIDTH * windowScale,
		240 * windowScale,
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
	);

	if (!plat__window) {
		SDL_LogError(LOG_APP, "SDL_CreateWindow: %s", SDL_GetError());
		return false;
	}

	plat__renderer = SDL_CreateRenderer(plat__window, NULL);

	if (!plat__renderer) {
		SDL_LogError(LOG_APP, "SDL_CreateRenderer: %s", SDL_GetError());
		return false;
	}

	if (!SDL_SetRenderVSync(plat__renderer, 1)) {
		SDL_LogWarn(LOG_APP, "SDL_SetRenderVSync: %s", SDL_GetError());
	}

	return true;
}

/** ****************************************************************************
 *
 * \brief	Create the pixel layer texture and the scene render target.
 *
 * \retval	true	Both textures were created.
 * \retval	false	SDL failed; the reason is logged.
 *
 ******************************************************************************/
static bool plat__createLayerTextures(void) {
	plat__pixelTexture = SDL_CreateTexture(
		plat__renderer,
		SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING,
		PLAT_WIDTH,
		PLAT_HEIGHT
	);
	plat__scene = SDL_CreateTexture(
		plat__renderer,
		SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_TARGET,
		PLAT_WIDTH * plat__renderScale,
		PLAT_HEIGHT * plat__renderScale
	);

	if (!plat__pixelTexture || !plat__scene) {
		SDL_LogError(LOG_APP, "SDL_CreateTexture: %s", SDL_GetError());
		return false;
	}

	SDL_SetTextureBlendMode(plat__pixelTexture, SDL_BLENDMODE_BLEND);
	SDL_SetTextureScaleMode(plat__pixelTexture, SDL_SCALEMODE_NEAREST);
	SDL_SetTextureScaleMode(plat__scene, SDL_SCALEMODE_LINEAR);

	return true;
}

/** ****************************************************************************
 *
 * \brief	Destroy the textures of an image.
 *
 * \param	pImage			The image.
 *
 ******************************************************************************/
static void plat__imageFreeTextures(
	plat__Image * pImage
) {
	if (pImage->pOriginal) {
		SDL_DestroyTexture(pImage->pOriginal);
		pImage->pOriginal = NULL;
	}

	if (pImage->pReplacement) {
		SDL_DestroyTexture(pImage->pReplacement);
		pImage->pReplacement = NULL;
	}
}

/** ****************************************************************************
 *
 * \brief	Look up an image by handle.
 *
 * \param	id				Handle of the image.
 *
 * \return	Pointer to the image. NULL for an invalid or destroyed handle.
 *
 ******************************************************************************/
static plat__Image * plat__getImage(
	plat_ImageHandle id
) {
	bool valid = id > 0 && id <= MAX_IMAGES && plat__images[id].used;

	return valid ? &plat__images[id] : NULL;
}

/** ****************************************************************************
 *
 * \brief	Load the replacement PNG of an image, if there is one.
 *
 * \param	pName			Name of the image, e.g. "lib1/000".
 *
 * \return	The replacement texture, or NULL if the mod folder has none.
 *
 ******************************************************************************/
static SDL_Texture * plat__loadReplacement(
	const char * pName
) {
	if (!plat__modDir[0]) {
		return NULL;
	}

	char path[640];

	snprintf(path, sizeof path, "%s/%s.png", plat__modDir, pName);

	SDL_Surface * pSurface = IMG_Load(path);

	if (!pSurface) {
		/* No replacement for this image. */
		return NULL;
	}

	SDL_Texture * pTexture =
		SDL_CreateTextureFromSurface(plat__renderer, pSurface);

	SDL_DestroySurface(pSurface);

	if (pTexture) {
		SDL_SetTextureBlendMode(pTexture, SDL_BLENDMODE_BLEND);
		SDL_SetTextureScaleMode(pTexture, SDL_SCALEMODE_LINEAR);
		SDL_Log("replacement: %s", path);
	}

	return pTexture;
}

/** ****************************************************************************
 *
 * \brief	Convert indexed pixels to ARGB rows with the current palette.
 *
 * \param	pSource				width * height palette indices.
 * \param	width				Width in pixels.
 * \param	height				Height in pixels.
 * \param	transparentIndex	Index drawn see-through, or -1.
 * \param	pDestination		First output row.
 * \param	pitch				Bytes between output rows.
 *
 ******************************************************************************/
static void plat__convertToArgb(
	const uint8_t * pSource,
	int				width,
	int				height,
	int				transparentIndex,
	void *			pDestination,
	int				pitch
) {
	for (int row = 0; row < height; row++) {
		uint8_t *		pRowStart = (uint8_t *)pDestination + row * pitch;
		uint32_t *		pOutput = (uint32_t *)pRowStart;
		const uint8_t * pInput = pSource + row * width;

		for (int column = 0; column < width; column++) {
			uint8_t index = pInput[column];
			bool	transparent = index == transparentIndex;

			pOutput[column] = transparent ? 0 : plat__palette[index];
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Get the texture of an original image.
 *
 * \param	pImage			The image.
 *
 * \return	The texture, or NULL if it could not be created.
 *
 * \details	The texture is built on first use and rebuilt after a palette
 * 			change.
 *
 ******************************************************************************/
static SDL_Texture * plat__originalTexture(
	plat__Image * pImage
) {
	bool current = pImage->paletteVersion == plat__paletteVersion;

	if (pImage->pOriginal && current) {
		return pImage->pOriginal;
	}

	if (!pImage->pOriginal) {
		pImage->pOriginal = SDL_CreateTexture(
			plat__renderer,
			SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STREAMING,
			pImage->width,
			pImage->height
		);

		if (!pImage->pOriginal) {
			return NULL;
		}

		SDL_SetTextureBlendMode(pImage->pOriginal, SDL_BLENDMODE_BLEND);
		SDL_SetTextureScaleMode(pImage->pOriginal, SDL_SCALEMODE_NEAREST);
	}

	void * pTexels;
	int	   pitch;

	if (SDL_LockTexture(pImage->pOriginal, NULL, &pTexels, &pitch)) {
		plat__convertToArgb(
			pImage->pPixels,
			pImage->width,
			pImage->height,
			pImage->seeThrough,
			pTexels,
			pitch
		);
		SDL_UnlockTexture(pImage->pOriginal);
	}

	pImage->paletteVersion = plat__paletteVersion;

	return pImage->pOriginal;
}

/** ****************************************************************************
 *
 * \brief	Clip a rectangle to the screen.
 *
 * \param	pRectangle		Rectangle in game pixels, clipped in place.
 *
 * \retval	true	Some of the rectangle is on the screen.
 * \retval	false	Nothing is left.
 *
 ******************************************************************************/
static bool plat__clipToScreen(
	SDL_Rect * pRectangle
) {
	const SDL_Rect screen = { 0, 0, PLAT_WIDTH, PLAT_HEIGHT };

	return SDL_GetRectIntersection(pRectangle, &screen, pRectangle);
}

/** ****************************************************************************
 *
 * \brief	Check if one rectangle lies completely inside another.
 *
 * \param	pOuter			The larger rectangle.
 * \param	pInner			The rectangle tested.
 *
 * \retval	true	pInner is inside pOuter.
 * \retval	false	Some of pInner is outside.
 *
 ******************************************************************************/
static bool plat__contains(
	const SDL_Rect * pOuter,
	const SDL_Rect * pInner
) {
	return pInner->x >= pOuter->x && pInner->y >= pOuter->y &&
		pInner->x + pInner->w <= pOuter->x + pOuter->w &&
		pInner->y + pInner->h <= pOuter->y + pOuter->h;
}

/** ****************************************************************************
 *
 * \brief	Check if two rectangles together form one rectangle.
 *
 * \param	pFirst			One rectangle.
 * \param	pSecond			The other rectangle.
 *
 * \retval	true	They share the columns and touch or overlap
 * 					vertically, or share the rows and touch or overlap
 * 					horizontally.
 * \retval	false	Their union is not a rectangle.
 *
 ******************************************************************************/
static bool plat__unionIsRectangle(
	const SDL_Rect * pFirst,
	const SDL_Rect * pSecond
) {
	bool sameColumns = pFirst->x == pSecond->x && pFirst->w == pSecond->w;
	bool sameRows = pFirst->y == pSecond->y && pFirst->h == pSecond->h;
	bool touchVertically = pFirst->y <= pSecond->y + pSecond->h &&
		pSecond->y <= pFirst->y + pFirst->h;
	bool touchHorizontally = pFirst->x <= pSecond->x + pSecond->w &&
		pSecond->x <= pFirst->x + pFirst->w;

	return (sameColumns && touchVertically) ||
		(sameRows && touchHorizontally);
}

/** ****************************************************************************
 *
 * \brief	Remove the images that an opaque area hides completely.
 *
 * \param	pPage			The page.
 * \param	pArea			Area that now covers everything below it.
 *
 * \details	Keeps the image layer short when the game draws over the same
 * 			place again and again.
 *
 ******************************************************************************/
static void plat__dropCovered(
	plat__Page *	 pPage,
	const SDL_Rect * pArea
) {
	int kept = 0;

	for (int index = 0; index < pPage->drawCount; index++) {
		if (!plat__contains(pArea, &pPage->draws[index].clip)) {
			pPage->draws[kept] = pPage->draws[index];
			kept++;
		}
	}

	pPage->drawCount = kept;
}

/** ****************************************************************************
 *
 * \brief	Add a draw to the image layer of a page.
 *
 * \param	pPage			The page.
 * \param	pDraw			The draw, with its clip rectangle on the screen.
 *
 * \details	An opaque image removes the draws it hides. Draws of the same
 * 			image that together form one rectangle (like the rows of a
 * 			wipe) are merged. The draw is dropped when the layer is full.
 *
 ******************************************************************************/
static void plat__addDraw(
	plat__Page *	   pPage,
	const plat__Draw * pDraw
) {
	const plat__Image * pImage = plat__getImage(pDraw->image);

	if (pImage && pImage->seeThrough < 0) {
		plat__dropCovered(pPage, &pDraw->clip);
	}

	if (pPage->drawCount == MAX_DRAWS) {
		if (!pPage->drawsOverflowed) {
			SDL_LogWarn(LOG_APP, "image layer full; draws dropped");
			pPage->drawsOverflowed = true;
		}

		return;
	}

	pPage->draws[pPage->drawCount] = *pDraw;
	pPage->drawCount++;
	plat__mergeDraw(pPage, pPage->drawCount - 1);
}

/** ****************************************************************************
 *
 * \brief	Merge a draw into an earlier draw of the same image.
 *
 * \param	pPage			The page.
 * \param	index			Draw to merge.
 *
 * \details	Moving a draw earlier puts it below the draws in between, so
 * 			the search stops at the first draw in between that overlaps
 * 			it. Merging repeats while the grown draw can merge again.
 *
 ******************************************************************************/
static void plat__mergeDraw(
	plat__Page * pPage,
	int			 index
) {
	bool merged = true;

	while (merged) {
		plat__Draw * pDraw = &pPage->draws[index];

		merged = false;

		for (int earlier = index - 1; earlier >= 0; earlier--) {
			plat__Draw * pEarlier = &pPage->draws[earlier];
			bool		 sameImage = pEarlier->image == pDraw->image &&
				pEarlier->left == pDraw->left && pEarlier->top == pDraw->top;

			if (sameImage &&
				plat__unionIsRectangle(&pEarlier->clip, &pDraw->clip)) {
				SDL_Rect * pClip = &pEarlier->clip;

				SDL_GetRectUnion(pClip, &pDraw->clip, pClip);

				int moved = pPage->drawCount - index - 1;

				memmove(pDraw, pDraw + 1, (size_t)moved * sizeof *pDraw);
				pPage->drawCount--;
				index = earlier;
				merged = true;
				break;
			}

			if (SDL_HasRectIntersection(&pEarlier->clip, &pDraw->clip)) {
				break;
			}
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Make the pixel layer transparent where an image is opaque.
 *
 * \param	pPage			The page.
 * \param	pImage			The image.
 * \param	left			Left edge of the image.
 * \param	top				Top edge of the image.
 * \param	pClip			Visible part of the image on the screen.
 *
 ******************************************************************************/
static void plat__showImageThrough(
	plat__Page *		pPage,
	const plat__Image * pImage,
	int					left,
	int					top,
	const SDL_Rect *	pClip
) {
	for (int row = pClip->y; row < pClip->y + pClip->h; row++) {
		const uint8_t * pInput = pImage->pPixels +
			(size_t)(row - top) * pImage->width + (pClip->x - left);
		int output = row * PLAT_WIDTH + pClip->x;

		for (int column = 0; column < pClip->w; column++) {
			if (pInput[column] != pImage->seeThrough) {
				pPage->pixels[output + column] = PLAT_TRANSPARENT;
			}
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Copy the pixel layer of a page into its texture.
 *
 * \param	pPage			The page.
 *
 ******************************************************************************/
static void plat__uploadPixelLayer(
	const plat__Page * pPage
) {
	void * pTexels;
	int	   pitch;

	if (!SDL_LockTexture(plat__pixelTexture, NULL, &pTexels, &pitch)) {
		return;
	}

	plat__convertToArgb(
		pPage->pixels,
		PLAT_WIDTH,
		PLAT_HEIGHT,
		PLAT_TRANSPARENT,
		pTexels,
		pitch
	);
	SDL_UnlockTexture(plat__pixelTexture);
}

/** ****************************************************************************
 *
 * \brief	Draw the image layer of a page into the scene.
 *
 * \param	pPage			The page.
 *
 * \details	Replacement pictures are scaled into the original rectangle
 * 			with linear filtering, originals with nearest neighbor. Only
 * 			the visible part of each draw is copied; for a replacement the
 * 			source rectangle is scaled by its size over the original size.
 *
 ******************************************************************************/
static void plat__drawImageLayer(
	const plat__Page * pPage
) {
	const int scale = plat__renderScale;

	for (int index = 0; index < pPage->drawCount; index++) {
		const plat__Draw * pDraw = &pPage->draws[index];
		plat__Image *	   pImage = plat__getImage(pDraw->image);

		if (!pImage) {
			/* Destroyed after being drawn. */
			continue;
		}

		SDL_Texture * pTexture = pImage->pReplacement
			? pImage->pReplacement
			: plat__originalTexture(pImage);

		if (!pTexture) {
			continue;
		}

		int textureWidth = pTexture->w;
		int textureHeight = pTexture->h;

		SDL_Rect source = {
			(pDraw->clip.x - pDraw->left) * textureWidth / pImage->width,
			(pDraw->clip.y - pDraw->top) * textureHeight / pImage->height,
			pDraw->clip.w * textureWidth / pImage->width,
			pDraw->clip.h * textureHeight / pImage->height,
		};
		SDL_Rect target = {
			pDraw->clip.x * scale,
			pDraw->clip.y * scale,
			pDraw->clip.w * scale,
			pDraw->clip.h * scale,
		};

		plat__renderCopy(pTexture, &source, &target);
	}
}

/** ****************************************************************************
 *
 * \brief	Draw the cursor set with plat_setCursor into the scene.
 *
 * \details	Uses the replacement picture of the cursor image if there is
 * 			one, like the image layer.
 *
 ******************************************************************************/
static void plat__drawCursor(void) {
	plat__Image * pImage = plat__getImage(plat__cursorImage);

	if (!pImage) {
		return;
	}

	SDL_Texture * pTexture = pImage->pReplacement
		? pImage->pReplacement
		: plat__originalTexture(pImage);

	if (!pTexture) {
		return;
	}

	int textureWidth = pTexture->w;
	int textureHeight = pTexture->h;

	const int scale = plat__renderScale;
	SDL_Rect  source = {
		0,
		plat__cursorSource * textureHeight / pImage->height,
		textureWidth,
		plat__cursorRows * textureHeight / pImage->height,
	};
	SDL_Rect target = {
		plat__cursorLeft * scale,
		plat__cursorTop * scale,
		pImage->width * scale,
		plat__cursorRows * scale,
	};

	plat__renderCopy(pTexture, &source, &target);
}

/** ****************************************************************************
 *
 * \brief	Copy part of a texture to the current render target.
 *
 * \param	pTexture		Texture to copy from.
 * \param	pSource			Part of the texture in texels, or NULL for all.
 * \param	pTarget			Where it goes in target pixels, or NULL for all.
 *
 * \details	SDL3 takes float rectangles; the layers work in whole pixels.
 *
 ******************************************************************************/
static void plat__renderCopy(
	SDL_Texture *	 pTexture,
	const SDL_Rect * pSource,
	const SDL_Rect * pTarget
) {
	SDL_FRect source;
	SDL_FRect target;

	if (pSource) {
		SDL_RectToFRect(pSource, &source);
	}

	if (pTarget) {
		SDL_RectToFRect(pTarget, &target);
	}

	SDL_RenderTexture(
		plat__renderer,
		pTexture,
		pSource ? &source : NULL,
		pTarget ? &target : NULL
	);
}

/** ****************************************************************************
 *
 * \brief	The largest 4:3 rectangle that fits the window, centered.
 *
 * \return	The rectangle in window pixels.
 *
 ******************************************************************************/
static SDL_Rect plat__windowRectangle(void) {
	int		 outputWidth;
	int		 outputHeight;
	SDL_Rect rectangle;

	SDL_GetRenderOutputSize(plat__renderer, &outputWidth, &outputHeight);

	if (outputWidth * 3 > outputHeight * 4) {
		rectangle.h = outputHeight;
		rectangle.w = outputHeight * 4 / 3;
	} else {
		rectangle.w = outputWidth;
		rectangle.h = outputWidth * 3 / 4;
	}

	rectangle.x = (outputWidth - rectangle.w) / 2;
	rectangle.y = (outputHeight - rectangle.h) / 2;

	return rectangle;
}

/** ****************************************************************************
 *
 * \brief	Set the pointer position from window coordinates.
 *
 * \param	windowX			Column in window coordinates.
 * \param	windowY			Row in window coordinates.
 *
 * \details	Maps the 4:3 picture in the window to 320x200 and clamps
 * 			positions outside it to the edge.
 *
 ******************************************************************************/
static void plat__setPointer(
	int windowX,
	int windowY
) {
	int windowWidth;
	int windowHeight;
	int outputWidth;
	int outputHeight;

	SDL_GetWindowSize(plat__window, &windowWidth, &windowHeight);
	SDL_GetRenderOutputSize(plat__renderer, &outputWidth, &outputHeight);

	SDL_Rect picture = plat__windowRectangle();

	if (windowWidth <= 0 || windowHeight <= 0 || picture.w <= 0 ||
		picture.h <= 0) {
		return;
	}

	/* Window coordinates differ from output pixels on high-DPI screens. */
	int outputX = windowX * outputWidth / windowWidth;
	int outputY = windowY * outputHeight / windowHeight;
	int left = (outputX - picture.x) * PLAT_WIDTH / picture.w;
	int top = (outputY - picture.y) * PLAT_HEIGHT / picture.h;

	plat__pointerLeft = SDL_clamp(left, 0, PLAT_WIDTH - 1);
	plat__pointerTop = SDL_clamp(top, 0, PLAT_HEIGHT - 1);
}

/** ****************************************************************************
 *
 * \brief	Add a key event to the key queue.
 *
 * \param	key				PLAT_KEY_* code or ASCII code.
 * \param	pressed			true for a press, false for a release.
 *
 * \details	The event is dropped when the queue is full.
 *
 ******************************************************************************/
static void plat__pushKey(
	int	 key,
	bool pressed
) {
	int next = (plat__keyHead + 1) % KEY_QUEUE_SIZE;

	if (next != plat__keyTail) {
		plat__keys[plat__keyHead].key = key;
		plat__keys[plat__keyHead].pressed = pressed;
		plat__keyHead = next;
	}
}

/** ****************************************************************************
 *
 * \brief	Translate an SDL keycode to a platform key code.
 *
 * \param	keycode			SDL keycode of a key.
 *
 * \return	ASCII code for printable and control keys, a PLAT_KEY_* code
 * 			for other supported keys, or PLAT_KEY_NONE for keys the game
 * 			does not use.
 *
 * \details	SDL keycodes of printable keys are their ASCII codes (letters
 * 			in lower case), so those pass through unchanged. Keypad keys
 * 			become the keys printed on them without Num Lock.
 *
 ******************************************************************************/
static int plat__translateKey(
	SDL_Keycode keycode
) {
	if (keycode > 0 && keycode < 128) {
		return (int)keycode;
	}

	if (keycode >= SDLK_F1 && keycode <= SDLK_F10) {
		return PLAT_KEY_F1 + (int)(keycode - SDLK_F1);
	}

	switch (keycode) {
		case SDLK_UP:
		case SDLK_KP_8:
			return PLAT_KEY_UP;

		case SDLK_DOWN:
		case SDLK_KP_2:
			return PLAT_KEY_DOWN;

		case SDLK_LEFT:
		case SDLK_KP_4:
			return PLAT_KEY_LEFT;

		case SDLK_RIGHT:
		case SDLK_KP_6:
			return PLAT_KEY_RIGHT;

		case SDLK_PAGEUP:
		case SDLK_KP_9:
			return PLAT_KEY_PAGE_UP;

		case SDLK_PAGEDOWN:
		case SDLK_KP_3:
			return PLAT_KEY_PAGE_DOWN;

		case SDLK_HOME:
		case SDLK_KP_7:
			return PLAT_KEY_HOME;

		case SDLK_END:
		case SDLK_KP_1:
			return PLAT_KEY_END;

		case SDLK_INSERT:
		case SDLK_KP_0:
			return PLAT_KEY_INSERT;

		case SDLK_DELETE:
		case SDLK_KP_PERIOD:
			return PLAT_KEY_DELETE;

		case SDLK_KP_ENTER:
			return PLAT_KEY_ENTER;

		case SDLK_KP_MINUS:
			return PLAT_KEY_KEYPAD_MINUS;

		case SDLK_KP_PLUS:
			return PLAT_KEY_KEYPAD_PLUS;

		case SDLK_LCTRL:
		case SDLK_RCTRL:
			return PLAT_KEY_CONTROL;

		default:
			return PLAT_KEY_NONE;
	}
}

/** ****************************************************************************
 *
 * rief	Check if a page draws an image.
 *
 * \param	id				Handle of the image.
 *
 *
etval	true	A page has a draw of the image.
 *
etval	false	No page draws it.
 *
 ******************************************************************************/
static bool plat__isDrawn(
	plat_ImageHandle id
) {
	for (int page = 0; page < PLAT_PAGE_COUNT; page++) {
		const plat__Page * pPage = &plat__pages[page];

		for (int draw = 0; draw < pPage->drawCount; draw++) {
			if (pPage->draws[draw].image == id) {
				return true;
			}
		}
	}

	return false;
}

/** ****************************************************************************
 *
 * rief	Free an image's pixels and textures and its slot.
 *
 * \param	pImage			The image.
 *
 ******************************************************************************/
static void plat__freeImage(
	plat__Image * pImage
) {
	plat__imageFreeTextures(pImage);
	free(pImage->pPixels);
	memset(pImage, 0, sizeof *pImage);
}

/** ****************************************************************************
 *
 * \brief	Open a joystick, as a gamepad when SDL knows its layout.
 *
 * \param	id				Instance of the joystick.
 *
 * \details	On failure no joystick stays open; the reason is logged.
 *
 ******************************************************************************/
static void plat__openJoystick(
	SDL_JoystickID id
) {
	if (SDL_IsGamepad(id)) {
		plat__gamepad = SDL_OpenGamepad(id);
		plat__joystick =
			plat__gamepad ? SDL_GetGamepadJoystick(plat__gamepad) : NULL;
	} else {
		plat__joystick = SDL_OpenJoystick(id);
	}

	if (plat__joystick == NULL) {
		SDL_LogWarn(LOG_APP, "joystick %u: %s", (unsigned)id, SDL_GetError());
		plat__gamepad = NULL;
		return;
	}

	plat__joystickPushed = 0;
	SDL_LogInfo(LOG_APP, "joystick: %s", SDL_GetJoystickName(plat__joystick));
}

/** ****************************************************************************
 *
 * \brief	Open the first joystick connected, if any.
 *
 * \details	The test joystick, once attached, is the only one used.
 *
 ******************************************************************************/
static void plat__openFirstJoystick(void) {
	if (plat__testJoystick != 0) {
		return;
	}

	int				 count = 0;
	SDL_JoystickID * pIds = SDL_GetJoysticks(&count);

	for (int index = 0; pIds && index < count; index++) {
		plat__openJoystick(pIds[index]);

		if (plat__joystick != NULL) {
			break;
		}
	}

	SDL_free(pIds);
}

/** ****************************************************************************
 *
 * \brief	Close the joystick in use, if any.
 *
 ******************************************************************************/
static void plat__closeJoystick(void) {
	if (plat__gamepad != NULL) {
		SDL_CloseGamepad(plat__gamepad);
	} else if (plat__joystick != NULL) {
		SDL_CloseJoystick(plat__joystick);
	}

	plat__gamepad = NULL;
	plat__joystick = NULL;
	plat__joystickPushed = 0;
}

/** ****************************************************************************
 *
 * \brief	Tell whether a joystick instance is the one in use.
 *
 * \param	id				Instance of a joystick.
 *
 * \retval	true	It is the joystick in use.
 * \retval	false	It is another one, or none is in use.
 *
 ******************************************************************************/
static bool plat__isOpenJoystick(
	SDL_JoystickID id
) {
	return plat__joystick != NULL && SDL_GetJoystickID(plat__joystick) == id;
}
