/** ****************************************************************************
 *
 * \file	main.c
 *
 * \brief	FreeRod entry point.
 *
 * \details	Phase 4 (in progress): the start of the original main
 * 			(1000:066f), the opening (logo, title, credits), the manual
 * 			question and the loading of the game data (FUN_1000_39c0),
 * 			then the resource viewer.
 *
 * 			Opening: Enter, Esc, Space or a mouse click skips a screen.
 * 			Viewer: Right/Left (or PageDown/PageUp) = next/previous
 * 			resource, Home = title, Esc = quit.
 *
 * 			Switches of the original (any case): 1, nomouse, demo, auto,
 * 			nouemem.
 *
 * 			Options:
 * 			--data DIR			folder with the original game files
 * 								(default: original)
 * 			--saves DIR			folder for saved games (default: saves)
 * 			--mod DIR			folder with replacement PNGs, e.g.
 * 								mods/hires (lib1/000.png, ...)
 * 			--viewer			skip the opening, start in the viewer
 * 			--show N			resource the viewer shows first
 * 								(default 0)
 * 			--scale N			initial window size multiplier (default 3)
 * 			--render-scale N	scene resolution multiplier (default 4)
 * 			--screenshot FILE	viewer: show one frame, save the composed
 * 								scene as BMP, and exit
 * 			--frames N			viewer: exit after N frames (for
 * 								automated runs)
 * 			--skip-opening		start at the manual question
 * 			--shot-at TICKS		with --screenshot: save the picture
 * 								when the game clock reaches TICKS (72.8
 * 								per second) instead of in the viewer
 * 			--click-at TICKS X Y	click at column X, row Y at that time
 * 								(for automated runs; up to 16, in order)
 * 			--keys-at TICKS TEXT	type TEXT from that time on, one key per
 * 								pass; "<" is Backspace, "!" is Enter, "~"
 * 								waits 40 ticks
 * 			--joystick-at TICKS X Y BUTTONS	use a test joystick and
 * 								move it from that time on: X, Y -100
 * 								(left, up) .. 100 percent, BUTTONS bit 0
 * 								button 1, bit 1 button 2 (up to 16, in
 * 								order)
 * 			--race-result FLAGS SPEED	ends every race at "Go !" with
 * 								this outcome (bits 0..7 for
 * 								DS:0x799c, 0x100 won) and the speed
 * 								at the end (mph); C notation (0x41)
 * 			--accept-bets		the opponent on the street takes every
 * 								bet (for automated runs)
 * 			--sound-test		play test tones and noise on the speaker
 * 								(about 2.5 seconds) and exit
 * 			--sound-wav FILE	record the sound as a WAV file
 * 			--sound-log FILE	write every change of the speaker (time,
 * 								divisor or port bits) to a text file
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <stdio.h> /* snprintf only; no file access or printing */
#include <stdlib.h>
#include <string.h>

#include "dos/dos.h"
#include "game/challenge.h"
#include "game/cursor.h"
#include "game/exe.h"
#include "game/game.h"
#include "game/globals.h"
#include "game/input.h"
#include "game/lib.h"
#include "game/opening.h"
#include "game/race.h"
#include "game/screen.h"
#include "game/video.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define MAIN_EXIT_CODE	5  /**< ExitGame ends the program with exit(5). */
#define MAIN_KEY_PAUSE	40 /**< Ticks "~" waits in --keys-at. */
#define MAIN_MAX_CLICKS 16 /**< Test clicks at most. */
#define MAIN_MAX_MOVES	16 /**< Test joystick moves at most. */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/**
 * Command-line options.
 */
typedef struct main__options_tag {
	const char * pDataDir;	  /**< Folder with the game files. */
	const char * pSaveDir;	  /**< Folder for saved games. */
	const char * pModDir;	  /**< Replacement folder, or NULL. */
	const char * pScreenshot; /**< Screenshot file, or NULL. */
	bool		 viewerOnly;  /**< Skip the opening. */
	int			 show;		  /**< Resource shown first. */
	int			 windowScale; /**< Window size multiplier. */
	int			 renderScale; /**< Scene resolution multiplier. */
	long		 maxFrames;	  /**< Frames to run, or -1 for no limit. */
	bool		 skipOpening; /**< Start at the manual question. */
	long		 shotAt;	  /**< Tick of the screenshot, or -1. */
	int			 clickCount;  /**< Test clicks given. */
	int			 clickNext;	  /**< Next test click to make. */
	long		 keysAt;	  /**< Tick of the test keys, or -1. */
	const char * pKeys;		  /**< Test keys still to type, or NULL. */
	int			 moveCount;	  /**< Test joystick moves given. */
	int			 moveNext;	  /**< Next test joystick move to make. */
	bool		 soundTest;	  /**< Play the test sounds and exit. */
	const char * pSoundWav;	  /**< Sound recording, or NULL. */
	const char * pSoundLog;	  /**< Log of the speaker, or NULL. */
} main__Options;

/**
 * A test click (--click-at).
 */
typedef struct main__click_tag {
	long tick;	 /**< Tick of the game clock. */
	int	 column; /**< Column of the click. */
	int	 row;	 /**< Row of the click. */
} main__Click;

/**
 * A move of the test joystick (--joystick-at).
 */
typedef struct main__joystick_move_tag {
	long	 tick;		 /**< Tick of the game clock. */
	int16_t	 horizontal; /**< -32767 (left) .. 32767 (right). */
	int16_t	 vertical;	 /**< -32767 (up) .. 32767 (down). */
	unsigned buttons;	 /**< PLAT_JOY_BUTTON_* bits held. */
} main__JoystickMove;

/*******************************************************************************
;
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

static void main__exitGame(void);

static void main__testHook(void);

static void main__soundTest(void);

static const char * main__resourceKindName(
	const lib_Resource * pResource
);

static void main__showResource(
	int id
);

static bool main__equalsIgnoringCase(
	const char * pText,
	const char * pLowerCase
);

static bool main__parseSwitch(
	const char * pArgument
);

static bool main__parseOptions(
	int				argc,
	char **			pArgv,
	main__Options * pOptions
);

static int16_t main__percentToAxis(
	int percent
);

static int main__nextResource(
	int current,
	int key
);

static void main__runViewer(
	int	 shown,
	long maxFrames
);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/**
 * The options, for main__testHook.
 */
static main__Options main__options;

/**
 * The test clicks of the --click-at options, in order.
 */
static main__Click main__clicks[MAIN_MAX_CLICKS];

/**
 * The test joystick moves of the --joystick-at options, in order.
 */
static main__JoystickMove main__moves[MAIN_MAX_MOVES];

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Program entry point.
 *
 * \param	argc			Number of arguments.
 * \param	pArgv			Arguments, see the switches and options in the file
 * 							comment.
 *
 * \return	0 on success, 1 when loading or setup fails, 2 for a bad
 * 			option.
 *
 * \details	Port of the start of main (1000:066f). Not ported: the
 * 			memory check (FUN_1000_05c6), the video adapter detection
 * 			and setup (FUN_1000_0226 and the calls after it; the port is
 * 			EGA/VGA), and the steps after OpenLibraries. The opening is
 * 			reached in the original through FUN_1000_503f and
 * 			FUN_1000_39c0, which also start the music (Phase 5).
 *
 ******************************************************************************/
int main(
	int		argc,
	char ** pArgv
) {
	main__Options options = {
		.pDataDir = "original",
		.pSaveDir = "saves",
		.pModDir = NULL,
		.pScreenshot = NULL,
		.viewerOnly = false,
		.show = 0,
		.windowScale = 3,
		.renderScale = 4,
		.maxFrames = -1,
		.skipOpening = false,
		.shotAt = -1,
		.clickCount = 0,
		.clickNext = 0,
		.keysAt = -1,
		.pKeys = NULL,
		.moveCount = 0,
		.moveNext = 0,
		.soundTest = false,
		.pSoundWav = NULL,
		.pSoundLog = NULL,
	};

	globals_mouseAllowed = 1;
	globals_demoSwitch = 0;
	globals_autoSwitch = 0;

	if (!main__parseOptions(argc, pArgv, &options)) {
		return 2;
	}

	plat_setDataDir(options.pDataDir);
	plat_setSaveDir(options.pSaveDir);

	if (!plat_init("FreeRod", options.windowScale, options.renderScale)) {
		return 1;
	}

	plat_setModDir(options.pModDir);

	if (options.moveCount > 0 && !plat_attachTestJoystick()) {
		plat_shutdown();
		return 1;
	}
	dos_setQuitHandler(main__exitGame);

	if (options.pSoundWav && !plat_soundRecord(options.pSoundWav)) {
		plat_shutdown();
		return 1;
	}

	if (options.pSoundLog && !plat_soundLog(options.pSoundLog)) {
		plat_shutdown();
		return 1;
	}

	if (options.soundTest) {
		main__soundTest();
		plat_shutdown();
		return 0;
	}

	screen_initSurfaces();
	input_init();
	video_setGamePalette();

	if (!lib_open()) {
		plat_log(
			"could not load the graphics libraries from %s",
			options.pDataDir
		);
		input_shutdown();
		plat_shutdown();
		return 1;
	}

	if (!exe_load()) {
		input_shutdown();
		plat_shutdown();
		lib_close();
		return 1;
	}

	cursor_init();
	main__options = options;

	bool testing = options.shotAt >= 0 || options.clickCount > 0 ||
		options.pKeys || options.moveCount > 0;

	if (testing) {
		dos_setServiceHook(main__testHook);
	}

	if (!options.viewerOnly) {
		game_skipOpening = options.skipOpening;
		game_run();
	}

	/* From here on the port's resource viewer, not the game. */
	dos_setServiceHook(NULL);
	plat_setCursor(0, 0, 0, 0, 0);
	input_shutdown();
	plat_selectPage(0);
	video_setGamePalette();

	if (options.show < 0 || options.show >= lib_count()) {
		options.show = 0;
	}

	main__showResource(options.show);

	int exitCode = 0;

	if (options.pScreenshot) {
		plat_present();

		bool saved = plat_saveSceneScreenshot(options.pScreenshot);

		plat_log(
			"%s %s",
			saved ? "saved" : "failed to save",
			options.pScreenshot
		);
		exitCode = saved ? 0 : 1;

	} else {
		main__runViewer(options.show, options.maxFrames);
	}

	plat_shutdown();
	lib_close();
	exe_unload();

	return exitCode;
}

/** ****************************************************************************
 *
 * \brief	End the program: give back the timer and the keyboard, close
 * 			the window and exit.
 *
 * \details	Port of ExitGame (1000:0fea), which the port also runs when
 * 			the window is closed. Not ported: the demo cleanup, the video
 * 			reset ([0x78c2] with FUN_1000_e610) and the speaker.
 *
 ******************************************************************************/
static void main__exitGame(void) {
	if (globals_timerInstalled != 0) {
		input_shutdown();
	}

	plat_shutdown();
	lib_close();
	exe_unload();
	exit(MAIN_EXIT_CODE);
}

/** ****************************************************************************
 *
 * \brief	Name of the kind of a resource, for the window title.
 *
 * \param	pResource		The resource.
 *
 * \return	"image", "sprite" or "1-plane".
 *
 ******************************************************************************/
static const char * main__resourceKindName(
	const lib_Resource * pResource
) {
	switch (pResource->flag) {
		case LIB_FLAG_IMAGE:
			return "image";

		case LIB_FLAG_SPRITE:
			return "sprite";

		default:
			return "1-plane";
	}
}

/** ****************************************************************************
 *
 * \brief	Show one resource and describe it in the window title.
 *
 * \param	id				Resource index.
 *
 * \details	Full screens are drawn at the origin, smaller resources are
 * 			centered.
 *
 ******************************************************************************/
static void main__showResource(
	int id
) {
	const lib_Resource * pResource = lib_get(id);
	plat_ImageHandle	 image = screen_resourceImage(id);

	plat_clearImages();
	plat_clearPixels();

	if (image) {
		int left = pResource->width >= PLAT_WIDTH
			? 0
			: (PLAT_WIDTH - pResource->width) / 2;
		int top = pResource->height >= PLAT_HEIGHT
			? 0
			: (PLAT_HEIGHT - pResource->height) / 2;

		plat_drawImage(image, left, top);
	}

	char title[128];

	snprintf(
		title,
		sizeof title,
		"FreeRod - %s (%d/%d)  %dx%d  %s%s",
		pResource->name,
		id,
		lib_count() - 1,
		pResource->width,
		pResource->height,
		main__resourceKindName(pResource),
		plat_imageIsReplaced(image) ? "  [replaced]" : ""
	);
	plat_setTitle(title);
}

/** ****************************************************************************
 *
 * \brief	Compare a text with a lower-case word, ignoring case.
 *
 * \param	pText			Text to check.
 * \param	pLowerCase		Word in lower case.
 *
 * \retval	true	The text is the word in any case.
 * \retval	false	It is not.
 *
 * \details	Stands in for stricmp, which is not standard C.
 *
 ******************************************************************************/
static bool main__equalsIgnoringCase(
	const char * pText,
	const char * pLowerCase
) {
	while (*pText && *pLowerCase) {
		char letter = *pText;

		if (letter >= 'A' && letter <= 'Z') {
			letter = (char)(letter - 'A' + 'a');
		}

		if (letter != *pLowerCase) {
			return false;
		}

		pText++;
		pLowerCase++;
	}

	return *pText == *pLowerCase;
}

/** ****************************************************************************
 *
 * \brief	Handle a command-line switch of the original game.
 *
 * \param	pArgument		The argument.
 *
 * \retval	true	It was a switch of the original.
 * \retval	false	It was not.
 *
 * \details	Port of the switch loop of main (1000:066f). "1" chose one
 * 			floppy drive for OpenLibraries; the port has no floppies and
 * 			ignores it. "demo" started the self-playing mode, which is
 * 			not ported (its random clicker FUN_1000_1bae is missing), so
 * 			it is ignored too and globals_demoSwitch stays 0.
 *
 ******************************************************************************/
static bool main__parseSwitch(
	const char * pArgument
) {
	if (strcmp(pArgument, "1") == 0 || main__equalsIgnoringCase(pArgument, "demo")) {
		return true;
	}

	if (main__equalsIgnoringCase(pArgument, "nomouse")) {
		globals_mouseAllowed = 0;
	} else if (main__equalsIgnoringCase(pArgument, "auto")) {
		globals_autoSwitch = 1;
	} else if (main__equalsIgnoringCase(pArgument, "nouemem")) {
		globals_noUememSwitch = 1;
	} else {
		return false;
	}

	return true;
}

/** ****************************************************************************
 *
 * \brief	Read the command-line switches and options.
 *
 * \param	argc			Number of arguments.
 * \param	pArgv			Arguments.
 * \param	pOptions		Options to update; keeps defaults for options not
 * 							given.
 *
 * \retval	true	All arguments were valid.
 * \retval	false	An option is unknown or has no value; the reason is
 * 					logged.
 *
 ******************************************************************************/
static bool main__parseOptions(
	int				argc,
	char **			pArgv,
	main__Options * pOptions
) {
	for (int index = 1; index < argc; index++) {
		const char * pOption = pArgv[index];

		if (main__parseSwitch(pOption)) {
			continue;
		}

		if (strcmp(pOption, "--skip-opening") == 0) {
			pOptions->skipOpening = true;
			continue;
		}

		if (strcmp(pOption, "--sound-test") == 0) {
			pOptions->soundTest = true;
			continue;
		}

		if (strcmp(pOption, "--accept-bets") == 0) {
			challenge_testAccept = true;
			continue;
		}

		if (strcmp(pOption, "--viewer") == 0) {
			pOptions->viewerOnly = true;
			continue;
		}

		bool hasValue = index + 1 < argc;

		if (!hasValue) {
			plat_log("unknown option or missing value: %s", pOption);
			return false;
		}

		const char * pValue = pArgv[++index];

		if (strcmp(pOption, "--data") == 0) {
			pOptions->pDataDir = pValue;
		} else if (strcmp(pOption, "--saves") == 0) {
			pOptions->pSaveDir = pValue;
		} else if (strcmp(pOption, "--mod") == 0) {
			pOptions->pModDir = pValue;
		} else if (strcmp(pOption, "--show") == 0) {
			pOptions->show = atoi(pValue);
		} else if (strcmp(pOption, "--scale") == 0) {
			pOptions->windowScale = atoi(pValue);
		} else if (strcmp(pOption, "--render-scale") == 0) {
			pOptions->renderScale = atoi(pValue);
		} else if (strcmp(pOption, "--screenshot") == 0) {
			pOptions->pScreenshot = pValue;
		} else if (strcmp(pOption, "--sound-wav") == 0) {
			pOptions->pSoundWav = pValue;
		} else if (strcmp(pOption, "--sound-log") == 0) {
			pOptions->pSoundLog = pValue;
		} else if (strcmp(pOption, "--frames") == 0) {
			pOptions->maxFrames = atol(pValue);
		} else if (strcmp(pOption, "--shot-at") == 0) {
			pOptions->shotAt = atol(pValue);
		} else if (strcmp(pOption, "--keys-at") == 0) {
			if (index + 1 >= argc) {
				plat_log("--keys-at needs TICKS TEXT");
				return false;
			}

			pOptions->keysAt = atol(pValue);
			pOptions->pKeys = pArgv[++index];
		} else if (strcmp(pOption, "--race-result") == 0) {
			if (index + 1 >= argc) {
				plat_log("--race-result needs FLAGS SPEED");
				return false;
			}

			race_testFlags = (int32_t)strtol(pValue, NULL, 0);
			race_testSpeed = (int16_t)atoi(pArgv[++index]);
		} else if (strcmp(pOption, "--click-at") == 0) {
			if (index + 2 >= argc || pOptions->clickCount == MAIN_MAX_CLICKS) {
				plat_log("--click-at needs TICKS X Y (at most 16)");
				return false;
			}

			int click = pOptions->clickCount++;

			main__clicks[click].tick = atol(pValue);
			main__clicks[click].column = atoi(pArgv[++index]);
			main__clicks[click].row = atoi(pArgv[++index]);
		} else if (strcmp(pOption, "--joystick-at") == 0) {
			if (index + 3 >= argc || pOptions->moveCount == MAIN_MAX_MOVES) {
				plat_log("--joystick-at needs TICKS X Y BUTTONS (at most 16)");
				return false;
			}

			main__JoystickMove * pMove = &main__moves[pOptions->moveCount++];

			pMove->tick = atol(pValue);
			pMove->horizontal = main__percentToAxis(atoi(pArgv[++index]));
			pMove->vertical = main__percentToAxis(atoi(pArgv[++index]));
			pMove->buttons = (unsigned)atoi(pArgv[++index]);
		} else {
			plat_log("unknown option: %s", pOption);
			return false;
		}
	}

	if (pOptions->pScreenshot && pOptions->shotAt < 0) {
		/* A screenshot of the viewer. */
		pOptions->viewerOnly = true;
	}

	return true;
}

/** ****************************************************************************
 *
 * \brief	Play test sounds on the speaker (--sound-test), to check the
 * 			platform's sound without the game.
 *
 * \details	Not in the original. A quarter second each: four tones (262,
 * 			330, 392, 523 Hz), silence, noise made by moving the speaker
 * 			by hand with the gate off (800 changes, 151..650 steps
 * 			apart), silence, a 25 kHz tone (what SpeakerOn leaves
 * 			playing: should not be heard) and silence.
 *
 ******************************************************************************/
static void main__soundTest(void) {
	static const uint16_t tones[] = { 262, 330, 392, 523 };

	uint32_t quarter = DOS_PIT_HZ / 4;

	dos_setSpeakerPort(DOS_SPEAKER_GATE | DOS_SPEAKER_DATA);

	for (size_t tone = 0; tone < sizeof tones / sizeof tones[0]; tone++) {
		dos_setSpeakerDivisor((uint16_t)(DOS_PIT_HZ / tones[tone]));
		dos_speakerDelay(quarter);
	}

	dos_setSpeakerPort(0);
	dos_speakerDelay(quarter);

	for (uint32_t change = 0; change < 800; change++) {
		dos_setSpeakerPort((change & 1) ? 0 : DOS_SPEAKER_DATA);
		dos_speakerDelay(151 + change * 37 % 500);
	}

	dos_setSpeakerPort(0);
	dos_speakerDelay(quarter);

	dos_setSpeakerDivisor((uint16_t)(DOS_PIT_HZ / 25000));
	dos_setSpeakerPort(DOS_SPEAKER_GATE | DOS_SPEAKER_DATA);
	dos_speakerDelay(quarter);
	dos_setSpeakerPort(0);
	dos_speakerDelay(quarter);

	/* Let the sound device play what is still buffered. */
	for (int wait = 0; wait < 200; wait++) {
		dos_service();
	}
}

/** ****************************************************************************
 *
 * \brief	The resource to show after a key press.
 *
 * \param	current			Resource shown now.
 * \param	key				Key code from plat_getKey.
 *
 * \return	The resource index to show.
 *
 ******************************************************************************/
static int main__nextResource(
	int current,
	int key
) {
	int count = lib_count();

	switch (key) {
		case PLAT_KEY_RIGHT:
		case PLAT_KEY_PAGE_DOWN:
			return (current + 1) % count;

		case PLAT_KEY_LEFT:
		case PLAT_KEY_PAGE_UP:
			return (current + count - 1) % count;

		case PLAT_KEY_HOME:
			return 0;

		default:
			return current;
	}
}

/** ****************************************************************************
 *
 * \brief	Run the viewer until the window closes or Esc is pressed.
 *
 * \param	shown			Resource shown at the start.
 * \param	maxFrames		Frames to run, or -1 for no limit.
 *
 ******************************************************************************/
static void main__runViewer(
	int	 shown,
	long maxFrames
) {
	long frames = 0;
	bool running = true;

	while (running && (maxFrames < 0 || frames < maxFrames)) {
		int key;

		running = plat_poll();

		while ((key = plat_getKey()) != PLAT_KEY_NONE) {
			if (key == PLAT_KEY_ESCAPE) {
				running = false;
			}

			int next = main__nextResource(shown, key);

			if (next != shown) {
				shown = next;
				main__showResource(shown);
			}
		}

		plat_present();
		frames++;
	}
}

/** ****************************************************************************
 *
 * \brief	Test aid: take the screenshot or make the click that the
 * 			options ask for when the game clock reaches their tick.
 *
 * \details	Runs from dos_service. A joystick move is seen from the next
 * 			event poll on. The click is latched like a click of
 * 			the click key at the given position. After the screenshot the
 * 			program ends with exit code 0 (1 when saving failed).
 *
 ******************************************************************************/
static void main__testHook(void) {
	int	 click = main__options.clickNext;
	bool clickDue = click < main__options.clickCount &&
		globals_ticks >= (uint32_t)main__clicks[click].tick;

	if (clickDue) {
		main__options.clickNext++;
		globals_cursorX = (int16_t)main__clicks[click].column;
		globals_cursorY = (int16_t)main__clicks[click].row;
		globals_cursorMoved = 1;
		globals_leftClickX = globals_cursorX;
		globals_leftClickY = globals_cursorY;
		globals_leftClick = 1;
	}

	int	 move = main__options.moveNext;
	bool moveDue = move < main__options.moveCount &&
		globals_ticks >= (uint32_t)main__moves[move].tick;

	if (moveDue) {
		main__options.moveNext++;
		plat_setTestJoystick(
			main__moves[move].horizontal,
			main__moves[move].vertical,
			main__moves[move].buttons
		);
	}

	bool keyDue = main__options.pKeys && main__options.pKeys[0] != '\0' &&
		globals_ticks >= (uint32_t)main__options.keysAt &&
		globals_lastKey == 0;

	if (keyDue) {
		char key = *main__options.pKeys++;

		if (key == '~') {
			main__options.keysAt = (long)globals_ticks + MAIN_KEY_PAUSE;
		} else if (key == '<') {
			dos_pressKey(PLAT_KEY_BACKSPACE);
		} else if (key == '!') {
			dos_pressKey(PLAT_KEY_ENTER);
		} else {
			dos_pressKey(key);
		}
	}

	bool shotDue = main__options.shotAt >= 0 && main__options.pScreenshot &&
		globals_ticks >= (uint32_t)main__options.shotAt;

	if (shotDue) {
		plat_present();

		bool saved = plat_saveSceneScreenshot(main__options.pScreenshot);

		plat_log(
			"%s %s",
			saved ? "saved" : "failed to save",
			main__options.pScreenshot
		);
		input_shutdown();
		plat_shutdown();
		lib_close();
		exe_unload();
		exit(saved ? 0 : 1);
	}
}

/** ****************************************************************************
 *
 * \brief	Turn a percentage of a joystick axis into its position.
 *
 * \param	percent			-100 (left, up) .. 100 (right, down); clamped.
 *
 * \return	The position, -32767 .. 32767.
 *
 ******************************************************************************/
static int16_t main__percentToAxis(
	int percent
) {
	if (percent < -100) {
		percent = -100;
	} else if (percent > 100) {
		percent = 100;
	}

	return (int16_t)(percent * 32767 / 100);
}
