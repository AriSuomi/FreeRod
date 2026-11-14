/** ****************************************************************************
 *
 * \file	input.h
 *
 * \brief	Timer, keyboard and mouse: the game's interrupt handlers and the
 * 			functions that read them.
 *
 * \details	The game reprograms the timer to 72.8 Hz and counts ticks in
 * 			globals_ticks; every delay in the game is measured in these
 * 			ticks. Its own keyboard handler turns scan codes into key codes
 * 			(globals_keyCodes), cursor directions and clicks. In the port
 * 			the handlers run from dos_service (see dos.h).
 *
 ******************************************************************************/

#ifndef GAME_INPUT_H_INCLUDED
#define GAME_INPUT_H_INCLUDED

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

#define INPUT_TIMER_DIVISOR 0x4000 /**< 1193182 / 16384 = 72.8 Hz. */

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

void input_init(void);

void input_shutdown(void);

void input_timerInterrupt(void);

void input_keyboardInterrupt(
	uint8_t scanCode
);

int8_t input_readKey(void);

void input_flush(void);

int16_t input_mouseReset(void);

void input_mouseReadButtons(void);

void input_mouseReadMotion(
	int16_t * pHorizontal,
	int16_t * pVertical
);

void input_mousePoll(void);

void input_readJoystick(void);

void input_waitTicks(
	int16_t ticks
);

void input_beginTextInput(
	int16_t numeric
);

void input_endTextInput(void);

#endif // GAME_INPUT_H_INCLUDED
