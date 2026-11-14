/** ****************************************************************************
 *
 * \file	globals.c
 *
 * \brief	Global variables of the game, from its data segment.
 *
 * \details	Initial values are the ones in Sr_unpacked.exe; variables in
 * 			uninitialized memory start at 0.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/globals.h"

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
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/* Command line (main) */

int16_t globals_mouseAllowed;  /**< DS:0x262: 0 after the nomouse switch. */
int16_t globals_demoSwitch;	   /**< DS:0x8bce: demo switch. */
int16_t globals_autoSwitch;	   /**< DS:0x612: auto switch. */
int16_t globals_noUememSwitch; /**< DS:0x280: nouemem switch. */

/**
 * DS:0x8acc: speed of the machine (FUN_1000_05c6: loops counted during a
 * timer tick / 950, at least 1); below 4 is a slow machine. The port does
 * not measure and is a fast machine.
 */
int16_t globals_machineSpeed = 4;

/**
 * DS:0x82c6: 1 on a slow machine (set with globals_machineSpeed): fewer
 * things at the roadside, gentler bends, faster cruising. The port is a
 * fast machine.
 */
int16_t globals_slowMachine;

/* Timer */

uint32_t globals_ticks;			 /**< DS:0x5f8: timer ticks, 72.8 Hz. */
uint32_t globals_biosTicks;		 /**< DS:0x600: BIOS ticks, 18.2 Hz. */
uint8_t	 globals_biosCountdown;	 /**< DS:0x608: ticks to the next one. */
int16_t	 globals_timerInstalled; /**< DS:0x44: TimerIsr installed. */
uint8_t	 globals_timerBusy;		 /**< DS:0x6e0: cursor step running. */

/**
 * DS:0x610: demo playback running (from the demo switch); 99 after Esc
 * stopped it.
 */
int16_t globals_demoMode;

/* Keyboard */

/**
 * DS:0x4766: key code of each scan code (0x56 for all higher ones). 0:
 * ignored; -30..-1: cursor direction; 0x1f, 0x1e: left, right click;
 * 0x1d: sound on/off; others: stored as the last key. BeginTextInput
 * changes entries at run time.
 */
/* clang-format off */
int8_t globals_keyCodes[GLOBALS_SCAN_CODES] = {
	0x00, 0x1b, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', 0x00,
	0x00, 0x00, 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', 0x00, 0x00,
	0x0d, 0x1d, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 0x00, 0x00, -4,
	0x00, -8, 'Z', 'X', 'C', 'V', 'B', 'N', 'M', 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, -2, -4, -6, -14, -8, 0x00, -10, -10, -12, -14, -16,
	0x1f, 0x00, 0x00, 0x1f, 0x00,
};
/* clang-format on */

/**
 * DS:0x4756: steps of the direction codes: the step of code c (-16..-2,
 * even) is [16 + c] columns and [17 + c] rows.
 */
const int8_t globals_directionSteps[GLOBALS_DIRECTIONS + 1] = {
	1, 1, 0, 1, -1, 1, 1, 0, -1, 0, 1, -1, 0, -1, -1, -1, 0
};

/**
 * DS:0x47d8: direction key c (-16..-1) is held, at [16 + c].
 */
uint8_t globals_directionHeld[GLOBALS_DIRECTIONS];

int8_t	globals_lastKey;		   /**< DS:0x47d0: last key, 0 = none. */
int8_t	globals_keyDirectionX;	   /**< DS:0x472e: sum of held columns. */
int8_t	globals_keyDirectionY;	   /**< DS:0x472f: sum of held rows. */
int8_t	globals_lastDirectionX;	   /**< DS:0x4730: last direction key. */
int8_t	globals_lastDirectionY;	   /**< DS:0x4731: last direction key. */
int16_t globals_keyboardBusy;	   /**< DS:0x4740: handler changing state. */
int16_t globals_keyboardInstalled; /**< DS:0x5e0: KeyboardIsr installed. */
uint8_t globals_soundOn = 1;	   /**< DS:0x47ce: Ctrl toggles it. */
uint8_t globals_clickKeyFlag;	   /**< DS:0x47cd: click key; use unknown. */
int16_t globals_clickKeyFlag2;	   /**< DS:0x47ee: click key; use unknown. */

/* Cursor and clicks */

int16_t globals_cursorX;	  /**< DS:0x472a: cursor column. */
int16_t globals_cursorY;	  /**< DS:0x472c: cursor row. */
int16_t globals_savedCursorX; /**< DS:0x4746: column before driving. */
int16_t globals_savedCursorY; /**< DS:0x4748: row before driving. */
uint8_t globals_cursorMoved;  /**< DS:0x6c4a: cursor needs redrawing. */
int8_t	globals_cursorShown;  /**< DS:0x4733: > 0 when shown. */

const int16_t globals_cursorStepX[3] = { -1, 0, 1 }; /**< DS:0x474a. */
const int16_t globals_cursorStepY[3] = { -1, 0, 1 }; /**< DS:0x4750. */

/**
 * DS:0x4732: click sources held: bit 0 key, bit 1 joystick, bit 2 mouse.
 */
uint8_t globals_buttonsHeld;

uint8_t globals_leftClick;	 /**< DS:0x4736: left click waiting. */
uint8_t globals_rightClick;	 /**< DS:0x4737: right click waiting. */
int16_t globals_leftClickX;	 /**< DS:0x4738: cursor at the left click. */
int16_t globals_leftClickY;	 /**< DS:0x473a. */
int16_t globals_rightClickX; /**< DS:0x473c: cursor at the right click. */
int16_t globals_rightClickY; /**< DS:0x473e. */

/* Mouse and joystick */

uint8_t globals_mousePresent;	 /**< DS:0x5f2: mouse in use. */
int16_t globals_mouseLeft;		 /**< DS:0x5ee: left button held. */
int16_t globals_mouseRight;		 /**< DS:0x5f0: right button held. */
int16_t globals_mouseMickeysX;	 /**< DS:0x5e6: motion not used yet. */
int16_t globals_mouseMickeysY;	 /**< DS:0x5e8: motion not used yet. */
int16_t globals_mouseShiftX;	 /**< DS:0x5ea: mickeys >> this = steps. */
int16_t globals_mouseShiftY;	 /**< DS:0x5ec: mickeys >> this = steps. */
int16_t globals_mouseStepX;		 /**< DS:0x5e2: cursor step, columns. */
int16_t globals_mouseStepY;		 /**< DS:0x5e4: cursor step, rows. */
uint8_t globals_joystickPresent; /**< DS:0x471d: joystick found. */
uint8_t globals_joystickButtons; /**< DS:0x471c: joystick buttons. */
int16_t globals_joystickX;		 /**< DS:0x4718: X from the center. */
int16_t globals_joystickY;		 /**< DS:0x471a: Y from the center. */
int16_t globals_joystickCenterX; /**< DS:0x471e: X count centered. */
int16_t globals_joystickCenterY; /**< DS:0x4720: Y count centered. */
int16_t globals_joystickLeft;	 /**< DS:0x4722: X below: left. */
int16_t globals_joystickRight;	 /**< DS:0x4724: X above: right. */
int16_t globals_joystickUp;		 /**< DS:0x4726: Y below: up. */
int16_t globals_joystickDown;	 /**< DS:0x4728: Y above: down. */

/* Music */

int16_t	 globals_musicOn = 1;	   /**< DS:0x58d0: 'M' toggles it. */
int16_t	 globals_musicPaused;	   /**< DS:0x58d8: off by music_pause. */
int16_t	 globals_musicSaved = -1;  /**< DS:0x58d6: flag kept by an effect. */
int16_t	 globals_vibratoOn;		   /**< DS:0x72ec: vibrato enabled. */
uint16_t globals_instrument;	   /**< DS:0x82c4: DS: instrument record. */
int16_t	 globals_vibratoTicks = 1; /**< DS:0x6e2: ticks to the next step. */
int16_t	 globals_vibratoDepth;	   /**< DS:0x6c52: depth of the note. */
int16_t	 globals_vibratoDivisor;   /**< DS:0x6c54: last divisor written. */

/* Video */

/**
 * DS:0x8236: graphics adapter. The port only has the EGA/VGA path.
 */
int16_t globals_videoAdapter = GLOBALS_ADAPTER_EGA;

int globals_displaySurface; /**< DS:0x822a: surface shown (a page). */
int globals_workSurface;	/**< DS:0x822e: surface drawn on (a page). */

/**
 * DS:0x8240: drawing on the work surface is copied to the display surface.
 */
int16_t globals_drawThrough;

int16_t globals_drawLeft;		  /**< DS:0x4be: DrawResource column. */
int16_t globals_drawTop;		  /**< DS:0x4c0: DrawResource row. */
int16_t globals_drawHeight = 190; /**< DS:0x4c2: rows drawn at most. */
uint8_t globals_fillColor;		  /**< DS:0x8244: FillRect color. */
uint8_t globals_paletteShown;	  /**< DS:0x8249: not blanked. */

/* Drawing style */

uint8_t globals_textColor;		/**< DS:0x8242: color of text pixels. */
uint8_t globals_textBackground; /**< DS:0x8243: color behind text. */
uint8_t globals_lineColor;		/**< DS:0x8245: color of lines. */
uint8_t globals_borderStyle;	/**< DS:0x8246: frame style, 0..3. */

/**
 * DS:0x787e: DS offset of the style table of the adapter (EGA: DS:0x3a0):
 * 5 bytes per style: text color, text background, fill color, line color,
 * border style.
 */
uint16_t globals_styleTable = 0x3a0;

int16_t globals_textEndX; /**< DS:0x824c: column after the last text. */
int16_t globals_textY;	  /**< DS:0x824e: row of the last text. */

/* Status line */

int16_t globals_statusLeft;	 /**< DS:0x8250: left edge of the message. */
int16_t globals_statusTop;	 /**< DS:0x8252: row of the text. */
int16_t globals_statusWidth; /**< DS:0x8254: width of the message. */
int16_t globals_statusShown; /**< DS:0x8256: a message is shown. */

/* Screens and hotspots */

int16_t globals_screenDepth; /**< DS:0x8b8e: screens open. */

/**
 * DS:0x8b86: numbers of the open screens, the newest last.
 */
int16_t globals_screenStack[GLOBALS_SCREEN_DEPTH];

int16_t globals_screenId;	   /**< DS:0x8efe: screen opened last. */
int16_t globals_screenValue;   /**< DS:0x8bac: header word 4; unknown. */
uint8_t globals_screenIsPlain; /**< DS:0x8b98: header type is 3. */

/**
 * DS:0x7a8: -1 while a whole screen is drawn (elements chain to the next
 * one), 0 when single elements are drawn (each is copied to the display).
 */
int16_t globals_drawChain;

int16_t globals_boundsLeft;	  /**< DS:0x6c56: left of drawn elements. */
int16_t globals_boundsTop;	  /**< DS:0x6c58: top of drawn elements. */
int16_t globals_boundsRight;  /**< DS:0x6c5a: right of drawn elements. */
int16_t globals_boundsBottom; /**< DS:0x6c5c: bottom of drawn elements. */

uint16_t globals_hotList;	 /**< DS:0x8ba6: first hotspot, or 0. */
uint16_t globals_hotElement; /**< DS:0x8ba8: hotspot under the cursor. */
int16_t	 globals_hotId;		 /**< DS:0x8baa: its number, or 0. */

/**
 * DS:0x8b90, 0x8b92, 0x8b94, 0x8b96: rectangle around the last tested
 * point in which the hotspot stays the same (right and bottom excluded).
 */
int16_t globals_hotLeft;
int16_t globals_hotRight;
int16_t globals_hotTop;
int16_t globals_hotBottom;

uint16_t globals_inputElement; /**< DS:0x8ba2: first type 7/8 element. */
uint8_t	 globals_inputIsLeft;  /**< DS:0x8ba4: a type 7 element exists. */
uint16_t globals_listFirst;	   /**< DS:0x8b9a: first type 13 element. */
int16_t	 globals_listCount;	   /**< DS:0x8b9c: type 13 elements. */

/* Events */

int16_t globals_keyScreen;	 /**< DS:0x6c4c: screen of the key map. */
uint8_t globals_keysChanged; /**< DS:0x64e: screen keys are mapped. */

/**
 * DS:0x650: pairs of scan code and the key code it had before the screen
 * changed it, ended by a zero scan code.
 */
int8_t globals_savedKeys[GLOBALS_SAVED_KEYS];

uint32_t globals_timeout;	   /**< DS:0x5cc: tick of a timeout, or 0. */
int16_t	 globals_anyKeyEnds;   /**< DS:0x5d0: any key or click ends. */
int16_t	 globals_eventKey;	   /**< DS:0x5d2: key of result 1003. */
int16_t	 globals_messageShown; /**< DS:0x5de: a message box is open. */
int16_t	 globals_exiting;	   /**< DS:0x6a4: 99 once the demo ended. */
int16_t	 globals_paletteCycle; /**< DS:0x4bc8: cycle colors 6..8. */
uint8_t	 globals_speakerOn;	   /**< DS:0x47cf: the speaker is on. */

int16_t globals_musicAvailable = 1; /**< DS:0x58d4: M may toggle. */

int16_t globals_cursorDrawnX; /**< DS:0x6c46: column the cursor is at. */
int16_t globals_cursorDrawnY; /**< DS:0x6c48: row the cursor is at. */
uint8_t globals_lastClick;	  /**< DS:0x6c4e: left click flag, last. */

/**
 * DS:0x56c: tick of the next timed event (queue at DS:0x566, game/anim.c).
 */
uint32_t globals_timedEventTime = GLOBALS_NO_EVENT;

/* Cursor shape */

int16_t globals_cursorShape; /**< DS:0x7b08: cursor shape, 0..3. */
int16_t globals_cursorHotX;	 /**< DS:0x4742: hot spot column of it. */
int16_t globals_cursorHotY;	 /**< DS:0x4744: hot spot row of it. */

/* Random numbers */

int16_t globals_randomSeed1; /**< DS:0x6c62: first generator. */
int16_t globals_randomSeed2; /**< DS:0x6c64: second generator. */
int16_t globals_randomSeed3; /**< DS:0x6c66: third generator. */

/* Game state */

int16_t globals_hotDataLoaded; /**< DS:0x70e2: main loaded hot_data. */
uint8_t globals_numericInput;  /**< DS:0x9f2: text input takes digits only. */

int16_t globals_statusValueLeft; /**< DS:0x8258: column of the money. */
int16_t globals_moneyEndX;		 /**< DS:0x82c0: column after the money. */

/**
 * DS:0x5fc: the game's own clock; the main loop adds 0x222 for actions
 * that take time. Unit not known yet.
 */
uint32_t globals_gameTime;

uint32_t globals_biosTicksAtStart; /**< DS:0x604: BIOS ticks, new game. */

/**
 * DS:0x282: reasons to end the game: bit 0 the summer is over, bit 4 the
 * player is broke.
 */
int16_t globals_endFlags;

/**
 * DS:0x284: where the player is: 1 garage, 2 street, 3 gas station, 5
 * newspaper ads; others not known yet.
 */
int16_t globals_place;

int16_t globals_messageTicks = 350; /**< DS:0x5dc: ticks a message stays. */

/**
 * Height of the work surface (word +2 of the surface at DS:0x822e): text
 * and fills are clipped to it. The ads page lowers it to 190 while it
 * draws.
 */
int16_t globals_workHeight = 200;

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/
