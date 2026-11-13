/** ****************************************************************************
 *
 * \file	video.h
 *
 * \brief	Video setup shared by game code.
 *
 ******************************************************************************/

#ifndef GAME_VIDEO_H_INCLUDED
#define GAME_VIDEO_H_INCLUDED

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

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

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

extern uint8_t video_gamePalette[16];

extern const uint8_t video_blackPalette[16];

extern const uint8_t video_creditsPalette[16];

extern const uint8_t video_questionPalette[16];

/*******************************************************************************
;
;	F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

void video_setEgaPalette(
	const uint8_t * pTable
);

void video_setGamePalette(void);

void video_blankPalette(void);

void video_showPalette(void);

void video_setPaletteColor(
	int index,
	int standardColor
);

void video_setNewGameColors(void);

#endif // GAME_VIDEO_H_INCLUDED
