/** ****************************************************************************
 *
 * \file	dos.h
 *
 * \brief	Replacements for the DOS, BIOS and hardware services the game
 * 			uses, built on the platform layer.
 *
 * \details	Interrupt handlers do not interrupt anything in the port. The
 * 			game installs its timer and keyboard handlers here, and every
 * 			loop that waited for the hardware calls dos_service, which runs
 * 			the handlers for what happened since the last call: one timer
 * 			call per elapsed tick and one keyboard call per scan code. The
 * 			game sees the same tick counts and key codes as on DOS.
 *
 ******************************************************************************/

#ifndef DOS_DOS_H_INCLUDED
#define DOS_DOS_H_INCLUDED

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

#define DOS_PIT_HZ 1193182 /**< Input clock of the timer chip (8253). */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/**
 * Timer interrupt handler (INT 8).
 */
typedef void (*dos_TimerHandler)(void);

/**
 * Keyboard interrupt handler (INT 9). Gets the byte the handler read from
 * port 60h: the scan code, plus 0x80 for a release.
 */
typedef void (*dos_KeyboardHandler)(uint8_t scanCode);

/**
 * Called when the user closes the window. Must not return.
 */
typedef void (*dos_QuitHandler)(void);

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

void dos_setTimerDivisor(
	uint16_t divisor
);

void dos_setTimerHandler(
	dos_TimerHandler pHandler
);

void dos_setKeyboardHandler(
	dos_KeyboardHandler pHandler
);

void dos_setQuitHandler(
	dos_QuitHandler pHandler
);

void dos_service(void);

void dos_exit(void);

void dos_pressKey(
	int key
);

void dos_setServiceHook(
	dos_QuitHandler pHook
);

uint16_t dos_mouseReset(void);

uint16_t dos_mouseButtons(void);

void dos_mouseMotion(
	int16_t * pHorizontal,
	int16_t * pVertical
);

void dos_mouseSetPosition(
	int16_t left,
	int16_t top
);

#endif // DOS_DOS_H_INCLUDED
