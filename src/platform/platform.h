/** ****************************************************************************
 *
 * \file	platform.h
 *
 * \brief	Platform layer: the only part of FreeRod that talks to SDL2.
 *
 * \details	It emulates what the DOS game had: a 320x200 screen with two
 * 			pages of video memory, a 256-entry palette (like the VGA DAC), a
 * 			keyboard, a mouse, a programmable timer and a millisecond clock.
 * 			Game and dos/ code use only this interface.
 *
 * 			Each page is two layers (see CLAUDE.md, "Graphics and hi-res
 * 			replacement"):
 * 			- Image layer: whole images drawn by id at 320x200
 * 			  coordinates. Each image is the original indexed picture, or a
 * 			  replacement PNG from the mod folder at any resolution.
 * 			- Pixel layer: a 320x200 indexed framebuffer for anything drawn
 * 			  pixel by pixel, on top of the image layer. PLAT_TRANSPARENT
 * 			  pixels let the image layer show through.
 *
 * 			Drawing an image makes the pixel layer transparent where the
 * 			image is opaque, and filling a rectangle hides the images below
 * 			it, so later drawing always covers earlier drawing, as in video
 * 			memory. Rectangles can be copied between pages; the copy keeps
 * 			the images, so replacement art survives it.
 *
 * 			Page 0 is shown. It is composed into a scene of
 * 			(320 * render scale) x (200 * render scale) pixels, which is then
 * 			stretched to 4:3 in the window like the original CRT.
 *
 ******************************************************************************/

#ifndef PLATFORM_H_INCLUDED
#define PLATFORM_H_INCLUDED

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define PLAT_WIDTH		  320 /**< Screen width in game pixels. */
#define PLAT_HEIGHT		  200 /**< Screen height in game pixels. */
#define PLAT_TRANSPARENT  255 /**< Pixel-layer index that shows through. */
#define PLAT_PAGE_COUNT	  11  /**< Pages of video memory; page 0 is shown. */

#define PLAT_BUTTON_LEFT  1 /**< Left mouse button, or touching. */
#define PLAT_BUTTON_RIGHT 2 /**< Right mouse button. */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/**
 * Handle of an image on the image layer. 0 means none.
 */
typedef int plat_ImageHandle;

/**
 * Key codes returned by plat_getKey. Printable keys and the control keys
 * below use their ASCII codes (letters in lower case), as DOS getch did.
 * Other keys use codes from 0x100 up. Keypad keys report the key printed
 * on them without Num Lock (keypad 8 is PLAT_KEY_UP), as DOS games saw
 * them.
 */
enum {
	PLAT_KEY_NONE = 0,				/**< No key waiting. */
	PLAT_KEY_BACKSPACE = 8,			/**< ASCII backspace. */
	PLAT_KEY_TAB = 9,				/**< ASCII tab. */
	PLAT_KEY_ENTER = 13,			/**< ASCII carriage return. */
	PLAT_KEY_ESCAPE = 27,			/**< ASCII escape. */
	PLAT_KEY_UP = 0x100,			/**< Cursor up. */
	PLAT_KEY_DOWN,					/**< Cursor down. */
	PLAT_KEY_LEFT,					/**< Cursor left. */
	PLAT_KEY_RIGHT,					/**< Cursor right. */
	PLAT_KEY_PAGE_UP,				/**< Page up. */
	PLAT_KEY_PAGE_DOWN,				/**< Page down. */
	PLAT_KEY_HOME,					/**< Home. */
	PLAT_KEY_END,					/**< End. */
	PLAT_KEY_INSERT,				/**< Insert. */
	PLAT_KEY_DELETE,				/**< Delete. */
	PLAT_KEY_F1,					/**< F1; F2..F10 follow in order. */
	PLAT_KEY_F10 = PLAT_KEY_F1 + 9, /**< F10. */
	PLAT_KEY_CONTROL,				/**< Either Ctrl key. */
	PLAT_KEY_KEYPAD_MINUS,			/**< Minus on the keypad. */
	PLAT_KEY_KEYPAD_PLUS,			/**< Plus on the keypad. */
};

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

bool plat_init(
	const char * pTitle,
	int			 windowScale,
	int			 renderScale
);

void plat_shutdown(void);

void plat_setTitle(
	const char * pTitle
);

void plat_setPaletteVga(
	int				first,
	int				count,
	const uint8_t * pDacColors
);

void plat_setPaletteEntry(
	int		index,
	uint8_t red,
	uint8_t green,
	uint8_t blue
);

void plat_setDefaultPalette(void);

void plat_selectPage(
	int page
);

uint8_t * plat_framebuffer(void);

void plat_clearPixels(void);

void plat_clearPage(void);

void plat_fillRect(
	int		left,
	int		top,
	int		width,
	int		height,
	uint8_t color
);

void plat_copyRect(
	int sourcePage,
	int destinationPage,
	int left,
	int top,
	int width,
	int height
);

void plat_blit(
	int sourcePage,
	int sourceLeft,
	int sourceTop,
	int destinationPage,
	int left,
	int top,
	int width,
	int height
);

void plat_readPixels(
	int		  page,
	int		  left,
	int		  top,
	int		  width,
	int		  height,
	uint8_t * pPixels
);

void plat_setModDir(
	const char * pDirectory
);

void plat_setDataDir(
	const char * pDirectory
);

const uint8_t * plat_loadFile(
	const char * pName,
	size_t *	 pSize
);

void plat_freeFile(
	const uint8_t * pData
);

void plat_setSaveDir(
	const char * pDirectory
);

uint8_t * plat_readSave(
	const char * pName,
	size_t *	 pSize
);

bool plat_writeSave(
	const char *	pName,
	const uint8_t * pData,
	size_t			size
);

void plat_deleteSave(
	const char * pName
);

void plat_log(
	const char * pFormat,
	...
);

plat_ImageHandle plat_imageCreate(
	const char *	pName,
	int				width,
	int				height,
	const uint8_t * pPixels,
	bool			transparent0
);

plat_ImageHandle plat_imageCreateOverlay(
	const char *	pName,
	int				width,
	int				height,
	const uint8_t * pPixels
);

void plat_imageDestroy(
	plat_ImageHandle image
);

bool plat_imageIsReplaced(
	plat_ImageHandle image
);

void plat_drawImage(
	plat_ImageHandle image,
	int				 left,
	int				 top
);

void plat_drawImageRows(
	plat_ImageHandle image,
	int				 left,
	int				 top,
	int				 rows
);

void plat_drawImagePart(
	plat_ImageHandle image,
	int				 sourceLeft,
	int				 sourceTop,
	int				 width,
	int				 height,
	int				 left,
	int				 top,
	const int *		 pClip
);

void plat_clearImages(void);

void plat_setCursor(
	plat_ImageHandle image,
	int				 sourceTop,
	int				 rows,
	int				 left,
	int				 top
);

uint32_t plat_timeSeconds(void);

void plat_present(void);

bool plat_poll(void);

int plat_getKey(void);

bool plat_getKeyEvent(
	int *  pKey,
	bool * pPressed
);

void plat_getPointer(
	int *	   pLeft,
	int *	   pTop,
	unsigned * pButtons
);

void plat_timerSetRate(
	uint32_t numerator,
	uint32_t denominator
);

uint32_t plat_timerTicks(void);

uint32_t plat_ticksMs(void);

void plat_delayMs(
	uint32_t milliseconds
);

bool plat_saveScreenshot(
	const char * pPath
);

bool plat_saveSceneScreenshot(
	const char * pPath
);

#endif // PLATFORM_H_INCLUDED
