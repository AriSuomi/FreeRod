/** ****************************************************************************
 *
 * \file	globals.h
 *
 * \brief	Global variables of the game, from its data segment.
 *
 * \details	Each variable names its DS offset in Sr_unpacked.exe (data
 * 			segment 4e96 in Ghidra). The original int is 16 bits wide, so
 * 			the variables use int16_t/uint16_t unless they hold bytes or
 * 			were 32-bit (far) values. Far pointers to video surfaces become
 * 			platform page numbers.
 *
 ******************************************************************************/

#ifndef GAME_GLOBALS_H_INCLUDED
#define GAME_GLOBALS_H_INCLUDED

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

#define GLOBALS_SCAN_CODES	  0x57 /**< Entries of globals_keyCodes. */
#define GLOBALS_DIRECTIONS	  16   /**< Direction codes -16..-1. */
#define GLOBALS_ADAPTER_EGA	  -2   /**< globals_videoAdapter for EGA/VGA. */
#define GLOBALS_CLICK_KEY	  0x1f /**< Key code of a left click. */
#define GLOBALS_RIGHT_KEY	  0x1e /**< Key code of a right click. */
#define GLOBALS_SOUND_KEY	  0x1d /**< Key code that toggles the sound. */
#define GLOBALS_DEMO_STOPPING 99   /**< globals_demoMode after Esc. */
#define GLOBALS_SCREEN_DEPTH  4	   /**< Screens open at the same time. */
#define GLOBALS_SAVED_KEYS	  64   /**< Bytes of globals_savedKeys. */

#define GLOBALS_NO_EVENT	  0x7fffffffUL /**< No timed event waiting. */

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

/* Command line (main) */

extern int16_t globals_mouseAllowed;
extern int16_t globals_demoSwitch;
extern int16_t globals_autoSwitch;
extern int16_t globals_noUememSwitch;
extern int16_t globals_machineSpeed;
extern int16_t globals_slowMachine;

/* Timer */

extern uint32_t globals_ticks;
extern uint32_t globals_biosTicks;
extern uint8_t	globals_biosCountdown;
extern int16_t	globals_timerInstalled;
extern uint8_t	globals_timerBusy;
extern int16_t	globals_demoMode;

/* Keyboard */

extern int8_t		globals_keyCodes[GLOBALS_SCAN_CODES];
extern const int8_t globals_directionSteps[GLOBALS_DIRECTIONS + 1];
extern uint8_t		globals_directionHeld[GLOBALS_DIRECTIONS];
extern int8_t		globals_lastKey;
extern int8_t		globals_keyDirectionX;
extern int8_t		globals_keyDirectionY;
extern int8_t		globals_lastDirectionX;
extern int8_t		globals_lastDirectionY;
extern int16_t		globals_keyboardBusy;
extern int16_t		globals_keyboardInstalled;
extern uint8_t		globals_soundOn;
extern uint8_t		globals_clickKeyFlag;
extern int16_t		globals_clickKeyFlag2;

/* Cursor and clicks */

extern int16_t		 globals_cursorX;
extern int16_t		 globals_cursorY;
extern int16_t		 globals_savedCursorX;
extern int16_t		 globals_savedCursorY;
extern uint8_t		 globals_cursorMoved;
extern int8_t		 globals_cursorShown;
extern const int16_t globals_cursorStepX[3];
extern const int16_t globals_cursorStepY[3];
extern uint8_t		 globals_buttonsHeld;
extern uint8_t		 globals_leftClick;
extern uint8_t		 globals_rightClick;
extern int16_t		 globals_leftClickX;
extern int16_t		 globals_leftClickY;
extern int16_t		 globals_rightClickX;
extern int16_t		 globals_rightClickY;

/* Mouse and joystick */

extern uint8_t globals_mousePresent;
extern int16_t globals_mouseLeft;
extern int16_t globals_mouseRight;
extern int16_t globals_mouseMickeysX;
extern int16_t globals_mouseMickeysY;
extern int16_t globals_mouseShiftX;
extern int16_t globals_mouseShiftY;
extern int16_t globals_mouseStepX;
extern int16_t globals_mouseStepY;
extern uint8_t globals_joystickPresent;
extern uint8_t globals_joystickButtons;

/* Music */

extern int16_t globals_musicOn;

/* Video */

extern int16_t globals_videoAdapter;
extern int	   globals_displaySurface;
extern int	   globals_workSurface;
extern int16_t globals_drawThrough;
extern int16_t globals_drawLeft;
extern int16_t globals_drawTop;
extern int16_t globals_drawHeight;
extern uint8_t globals_fillColor;
extern uint8_t globals_paletteShown;

/* Drawing style */

extern uint8_t	globals_textColor;
extern uint8_t	globals_textBackground;
extern uint8_t	globals_lineColor;
extern uint8_t	globals_borderStyle;
extern uint16_t globals_styleTable;
extern int16_t	globals_textEndX;
extern int16_t	globals_textY;

/* Status line */

extern int16_t globals_statusLeft;
extern int16_t globals_statusTop;
extern int16_t globals_statusWidth;
extern int16_t globals_statusShown;

/* Screens and hotspots */

extern int16_t	globals_screenDepth;
extern int16_t	globals_screenStack[GLOBALS_SCREEN_DEPTH];
extern int16_t	globals_screenId;
extern int16_t	globals_screenValue;
extern uint8_t	globals_screenIsPlain;
extern int16_t	globals_drawChain;
extern int16_t	globals_boundsLeft;
extern int16_t	globals_boundsTop;
extern int16_t	globals_boundsRight;
extern int16_t	globals_boundsBottom;
extern uint16_t globals_hotList;
extern uint16_t globals_hotElement;
extern int16_t	globals_hotId;
extern int16_t	globals_hotLeft;
extern int16_t	globals_hotRight;
extern int16_t	globals_hotTop;
extern int16_t	globals_hotBottom;
extern uint16_t globals_inputElement;
extern uint8_t	globals_inputIsLeft;
extern uint16_t globals_listFirst;
extern int16_t	globals_listCount;

/* Events */

extern int16_t	globals_keyScreen;
extern uint8_t	globals_keysChanged;
extern int8_t	globals_savedKeys[GLOBALS_SAVED_KEYS];
extern uint32_t globals_timeout;
extern int16_t	globals_anyKeyEnds;
extern int16_t	globals_eventKey;
extern int16_t	globals_messageShown;
extern int16_t	globals_exiting;
extern int16_t	globals_paletteCycle;
extern uint8_t	globals_speakerOn;
extern int16_t	globals_musicAvailable;
extern int16_t	globals_cursorDrawnX;
extern int16_t	globals_cursorDrawnY;
extern uint8_t	globals_lastClick;
extern uint32_t globals_timedEventTime;

/* Cursor shape */

extern int16_t globals_cursorShape;
extern int16_t globals_cursorHotX;
extern int16_t globals_cursorHotY;

/* Random numbers */

extern int16_t globals_randomSeed1;
extern int16_t globals_randomSeed2;
extern int16_t globals_randomSeed3;

/* Game state */

extern int16_t	globals_hotDataLoaded;
extern uint8_t	globals_numericInput;
extern int16_t	globals_statusValueLeft;
extern int16_t	globals_moneyEndX;
extern uint32_t globals_gameTime;
extern uint32_t globals_biosTicksAtStart;
extern int16_t	globals_endFlags;
extern int16_t	globals_place;
extern int16_t	globals_messageTicks;
extern int16_t	globals_workHeight;

/*******************************************************************************
;
;	F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

#endif // GAME_GLOBALS_H_INCLUDED
