/** ****************************************************************************
 *
 * \file	drive.c
 *
 * \brief	The drive itself: dashboard, the loop of a drive and its steps.
 *
 * \details	Ports of FUN_1f38_7b22 (the drive), FUN_1000_8ea8 (a step of
 * 			it), the parts of FUN_1f38_0dbd it uses and the panel: gear
 * 			shift (FUN_1f38_12f7, 14f2), steering wheel (1130) and
 * 			needles (01d2, 0349, 0425), and the timer handler of a drive
 * 			(TimerIsr2 1000:238c with FUN_1000_1aad, 1b27, 2374), which
 * 			drives the player's car in a race. The road's state is
 * 			roadstate.c, its pictures roadpic.c, its view roadview.c; the
 * 			sounds are not ported yet: drive__notPorted stands in for
 * 			each and logs it once. FUN_1f38_1094 (drive_crashMark) draws
 * 			the wheel's mark on the picture of a crash.
 *
 * 			Layout. On EGA the original splits the screen (line compare
 * 			register, FUN_31a0_0014): the instrument panel is at the start
 * 			of video memory and shows in rows 100..199, the road is drawn
 * 			on two pages shown in turn above it. The port has no split
 * 			screen and takes the layout of the other adapters instead: the
 * 			panel is on the display in rows 100..189 and the road view is
 * 			drawn on the work page (DS:0x8188) and copied to the display
 * 			(DS:0x7678). The Tandy path (DS:0x58e4) is not ported.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/drive.h"

#include <stdbool.h>

#include "dos/dos.h"
#include "dos/fpu.h"
#include "game/calendar.h"
#include "game/driving.h"
#include "game/exe.h"
#include "game/globals.h"
#include "game/input.h"
#include "game/lib.h"
#include "game/race.h"
#include "game/random.h"
#include "game/road.h"
#include "game/roaddraw.h"
#include "game/roadpic.h"
#include "game/roadstate.h"
#include "game/screen.h"
#include "game/ui.h"
#include "game/video.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

/*
 * Pictures.
 */
#define DRIVE_DASHBOARD	   0x3f6 /**< Windshield frame and panel. */
#define DRIVE_SHIFT_SIGN   0x4d8 /**< Drawn on it with an automatic. */
#define DRIVE_SIGN_LEFT	   0x78	 /**< Where that picture goes. */
#define DRIVE_SIGN_TOP	   0x7a	 /**< Where that picture goes. */
#define DRIVE_FRAME_ROWS   0x65	 /**< Rows of the windshield frame. */
#define DRIVE_PANEL_SOURCE 100	 /**< First row of the panel in the picture. */
#define DRIVE_PANEL_TOP	   100	 /**< First row of the panel on the screen. */
#define DRIVE_PANEL_ROWS   0x5a	 /**< Rows of the panel. */
#define DRIVE_TOP_ROWS	   0x12	 /**< Rows of the frame shown at the start. */
#define DRIVE_PANEL_PAGE   9	 /**< Page that stands in for DS:0x82b4. */

/*
 * The steering wheel on the panel (columns and rows of the panel).
 */
#define DRIVE_WHEEL_IDS	   0x58e8 /**< DS: 3 pictures: ahead, left, right. */
#define DRIVE_WHEEL_SHOWN  0x350  /**< s16: which of them is shown. */
#define DRIVE_WHEEL_LEFT   0x50	  /**< Where the picture goes. */
#define DRIVE_WHEEL_TOP	   0x3f	  /**< Where the picture goes. */
#define DRIVE_WHEEL_WIDTH  0xb8	  /**< Size of the picture. */
#define DRIVE_WHEEL_HEIGHT 0x1a	  /**< Size of the picture. */
#define DRIVE_MARK_PLACES  0x31e  /**< DS: column, row by position -24..24. */
#define DRIVE_MARK_SHAPES  0x60a0 /**< DS: near pointer to count, shapes. */
#define DRIVE_MARK_REACH   2	  /**< Pixels of the mark around its place. */
#define DRIVE_MARK_SIZE	   5	  /**< Width and height of the mark. */
#define DRIVE_CRASH_LAST   0xbe	  /**< Last row of the mark after a crash. */
#define DRIVE_NO_MARK	   100	  /**< DS:0x82c2: no mark is shown. */
#define DRIVE_SHAPE_SIZE   6	  /**< Shape: s16 spans, far pointer. */
#define DRIVE_SPAN_SIZE	   4	  /**< Span: s8 column, row, length; color. */

/*
 * The gear shift on the panel. A record of DS:0x36e: s16 places, picture of
 * the gate, picture of the knob, rows of the knob, near pointer to the
 * places (s8 column, row in the gate).
 */
#define DRIVE_SHIFTERS		 0x36e	/**< DS: automatic, 3 gears, 4 gears. */
#define DRIVE_SHIFTER_SIZE	 10		/**< Bytes of a record. */
#define DRIVE_SHIFTER_GATE	 2		/**< Record: picture of the gate. */
#define DRIVE_SHIFTER_KNOB	 4		/**< Record: picture of the knob. */
#define DRIVE_SHIFTER_ROWS	 6		/**< Record: rows drawn for a knob. */
#define DRIVE_SHIFTER_PLACES 8		/**< Record: near pointer to places. */
#define DRIVE_SHIFTER		 0x6c34 /**< u16: DS offset of the record. */
#define DRIVE_GEAR_SHOWN	 0x6c36 /**< s16: place of the knob, -1 none. */
#define DRIVE_GATE_LEFT		 0x108	/**< Where the gate goes. */
#define DRIVE_GATE_TOP		 0x17	/**< Where the gate goes. */
#define DRIVE_GATE_WIDTH	 0x30	/**< Size of the gate. */
#define DRIVE_GATE_HEIGHT	 0x2b	/**< Size of the gate. */
#define DRIVE_KNOB_WIDTH	 0x20	/**< Most columns drawn for a knob. */
#define DRIVE_KNOB_MARGIN	 8		/**< Columns kept beside a knob. */
#define DRIVE_THREE_GEARS	 3		/**< DRIVING_GEARS of the second record. */

/*
 * The needles of the speedometer and the tachometer.
 */
#define DRIVE_NEEDLE_SEGMENT 0x68e4 /**< u16: segment of the needle table. */
#define DRIVE_NEEDLE_INNER	 0x106	/**< Offset of the inner ends in it. */
#define DRIVE_NEEDLE_BYTES	 0x20c	/**< Bytes of the table. */
#define DRIVE_NEEDLE_LAST	 0x82	/**< Last position of a needle. */
#define DRIVE_NEEDLE_ROW	 0x2a	/**< Row of the gauges' centers. */
#define DRIVE_SPEED_COLUMN	 0xc4	/**< Column of the speedometer's. */
#define DRIVE_REVS_COLUMN	 0x91	/**< Column of the tachometer's. */
#define DRIVE_REVS_STEP		 0x2b	/**< Revs per position of the needle. */
#define DRIVE_NEEDLE_COLOR	 0x2d2	/**< s16: color of the needles, -1 unset. */
#define DRIVE_NEEDLE_BRIGHT	 0xb	/**< That color on EGA and VGA. */
#define DRIVE_BYTE_MASK		 0xfff8 /**< Column of a byte of video memory. */
#define DRIVE_UNDER_EXTRA	 0x10	/**< Columns added to a saved area. */

/*
 * The clock an automatic shows in place of the tachometer.
 */
#define DRIVE_CLOCK_PI		 0x68ea /**< DS: real pi. */
#define DRIVE_CLOCK_HALF	 0x68f2 /**< DS: real 180, degrees of pi. */
#define DRIVE_CLOCK_HUB		 0x68fa /**< DS: real 5, where a hand starts. */
#define DRIVE_CLOCK_HOUR	 0x6902 /**< DS: real 13, the hour hand's tip. */
#define DRIVE_CLOCK_SIDE	 0x690a /**< DS: real 9, a hand's corners. */
#define DRIVE_CLOCK_MINUTE	 0x6912 /**< DS: real 18, the minute hand's tip. */
#define DRIVE_CLOCK_HOURS	 12		/**< Hours of the dial. */
#define DRIVE_CLOCK_SHIFT	 20		/**< Added to half the hour index. */
#define DRIVE_CLOCK_PER_HOUR 30		/**< Degrees of an hour. */
#define DRIVE_CLOCK_PER_MIN	 6		/**< Degrees of a minute. */
#define DRIVE_CLOCK_UP		 90		/**< Degrees of 12 o'clock. */
#define DRIVE_CLOCK_NEAR	 10		/**< Degrees from an axis: wide hand. */
#define DRIVE_CLOCK_WIDE	 8		/**< Degrees to a corner near an axis. */
#define DRIVE_CLOCK_HOUR_OFF 5		/**< Else, of the hour hand. */
#define DRIVE_CLOCK_MIN_OFF	 4		/**< Else, of the minute hand. */
#define DRIVE_CLOCK_POINTS	 4		/**< Start, tip and two corners. */

/*
 * Modes of FUN_1f38_0dbd.
 */
#define DRIVE_SURFACES_NORMAL 1 /**< Display and work surface apart. */
#define DRIVE_SURFACES_DRIVE  3 /**< Both are the screen. */

/*
 * Texts and tables of Sr.exe.
 */
#define DRIVE_TEXT_READY   0x51ae /**< DS: "Get ready !". */
#define DRIVE_TEXT_GO	   0x51ba /**< DS: "Go !". */
#define DRIVE_TEXT_BLANK   0x51bf /**< DS: 4 spaces. */
#define DRIVE_TEXT_CRUISE  0x51c4 /**< DS: "Cruising in town...". */
#define DRIVE_CRUISE_STEPS 0x51d8 /**< DS: revs a step adds, by gear. */
#define DRIVE_CRUISE_REVS  0x51e2 /**< DS: revs per mph, by gear. */
#define DRIVE_RACE_TITLE   0x49de /**< DS: near pointer, title of a race. */

/*
 * Variables in the data segment image. Names say what the code ported so
 * far shows.
 */
#define DRIVE_UNKNOWN_5D6  0x5d6  /**< s16: cleared at the first step. */
#define DRIVE_HOME		   0x51ec /**< s16: 1 driving home after a race. */
#define DRIVE_CRUISE_STEP  0x6c82 /**< s16: revs a step of a cruise adds. */
#define DRIVE_PLACE		   0x7d30 /**< s16: place on the road. */
#define DRIVE_UNKNOWN_824A 0x824a /**< u8: 100 with the split screen. */
#define DRIVE_WHEEL		   0x82c2 /**< s16: record +4 the panel shows. */
#define DRIVE_FLAGS		   0x799c /**< u16: what went wrong in a race. */
#define DRIVE_WON_FLAGS	   0x7a9e /**< u16: bit 0, the player won. */

#define DRIVE_READY_TICKS  0x6e	  /**< "Get ready !" lasts this at least. */
#define DRIVE_READY_RANGE  0x67	  /**< Random ticks added to it. */
#define DRIVE_CRUISE_TOP   0x125c /**< Most revs of a cruise. */
#define DRIVE_CRUISE_SHIFT 0x1195 /**< Revs at which a cruise shifts up. */
#define DRIVE_JUMPED_LIGHT 0x81	  /**< Flags: moved before "Go !". */
#define DRIVE_NEEDLE_STEP  1	  /**< A needle moves for more than this. */
#define DRIVE_KEY_ESCAPE   0x1b	  /**< Key that ends a cruise. */

/*
 * The timer handler of a drive. Variables in the data segment image.
 */
#define DRIVE_UNKNOWN_286  0x286  /**< s16: not 0, the car is not driven. */
#define DRIVE_UNKNOWN_60A  0x60a  /**< s16: cleared with the handler. */
#define DRIVE_STEP_WAIT	   0x609  /**< s8: BIOS ticks to the next step. */
#define DRIVE_AUTO_SHIFTS  0x6ac  /**< s16: shifts of the automatic driver. */
#define DRIVE_OVER_REVS	   0x6e6  /**< s16: steps at the top revs. */
#define DRIVE_STEP_BUSY	   0x6e8  /**< u8: a step is running. */
#define DRIVE_BUTTON_WAS   0x6e9  /**< u8: buttons held at the last step. */
#define DRIVE_FORCED_BRAKE 0x54da /**< s16: steps the brake is held. */
#define DRIVE_UNKNOWN_58DA 0x58da /**< s16: cleared with the handler. */
#define DRIVE_UNKNOWN_58DC 0x58dc /**< s16: cleared with the handler. */
#define DRIVE_SLOWED	   0x5e0e /**< s16: not 0, the car slows to 8 mph. */
#define DRIVE_CAR		   0x7eb0 /**< u16: DS offset of the player's car. */
#define DRIVE_AUTO_FROM	   0x7e8a /**< s16: the automatic driver's start. */
#define DRIVE_RACE_KIND	   0x7ec0 /**< s16: kind of race, 0 drag race. */
#define DRIVE_AUTO_MAY	   0x8228 /**< s16: the automatic driver shifts. */
#define DRIVE_DISTANCE	   0x82bc /**< u32: sum of the speed at each step. */

#define DRIVE_BIOS_EVERY   3	  /**< Countdown: BIOS tick every 4th tick. */
#define DRIVE_STEP_EVERY   3	  /**< A step every 3rd BIOS tick. */
#define DRIVE_MOUSE_SHIFT  6	  /**< globals_mouseShiftY in a drive. */
#define DRIVE_CAR_GEARBOX  6	  /**< Car: DS offset of the transmission. */
#define DRIVE_CAR_ENGINE   8	  /**< Car: DS offset of the engine. */
#define DRIVE_PART_WEAR	   2	  /**< Part: its wear. */
#define DRIVE_WORN_OUT	   0x26ab /**< A part with more wear breaks. */
#define DRIVE_WEAR_SOFT	   3	  /**< Wear of a shift up to 5000 revs. */
#define DRIVE_WEAR_HARD	   0xf	  /**< Wear of a shift above 5000 revs. */
#define DRIVE_WEAR_REVS	   5000	  /**< Above these revs the parts wear. */
#define DRIVE_WEAR_EVERY   0xf	  /**< Mask of BIOS ticks: the engine wears. */
#define DRIVE_MISS_RANGE   0xccd  /**< Range of the chance of a bad shift. */
#define DRIVE_MISS_BASE	   100	  /**< Chance of it besides wear / 100. */
#define DRIVE_OVER_STEPS   0xf	  /**< Steps at top revs less wear / 1000. */
#define DRIVE_TOP_REVS	   0x1644 /**< Most revs of the engine. */
#define DRIVE_AUTO_REVS	   0x163a /**< Most revs with an automatic. */
#define DRIVE_IDLE_REVS	   0x5dc  /**< Revs of the idling engine. */
#define DRIVE_SHIFTED_REVS 0x5e1  /**< Revs after a shift at no speed. */
#define DRIVE_NEUTRAL_REVS 0x1e	  /**< Revs per mph above idle in neutral. */
#define DRIVE_LOW_REVS	   2000	  /**< Automatic driver shifts down below. */
#define DRIVE_STALL_SPEED  4	  /**< Up to this speed the gear falls out. */
#define DRIVE_SLOW_SPEED   8	  /**< Speed DRIVE_SLOWED slows down to. */
#define DRIVE_PAST_END	   0xf	  /**< Speed lost a step past the end. */
#define DRIVE_DRAG_END	   0x17c  /**< Place where a drag race ends. */
#define DRIVE_ROAD_END	   0x3fc  /**< Place where the other races end. */
#define DRIVE_AUTO_STOP	   0x3c	  /**< Places before it without gas. */
#define DRIVE_STEER_MOST   0x18	  /**< Most steering to a side. */
#define DRIVE_BLOWN_ENGINE 0x05	  /**< Flags: the engine broke. */
#define DRIVE_BLOWN_GEARS  0x11	  /**< Flags: the transmission broke. */
#define DRIVE_CRASHED	   0x21	  /**< Flags: what FUN_3645_206b found. */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/** What the port lacks; indexes of drive__missingNames. */
typedef enum drive__missing_tag {
	DRIVE__SOUND_OFF, /**< FUN_1f38_7001. */
	DRIVE__MISSING_COUNT
} drive__Missing;

/** A rectangle of the screen. */
typedef struct drive__box_tag {
	int16_t left;	/**< Left edge. */
	int16_t top;	/**< Top edge. */
	int16_t width;	/**< Width. */
	int16_t height; /**< Height. */
} drive__Box;

/** The two ends of a needle. */
typedef struct drive__needle_tag {
	int16_t outerLeft; /**< Column of the end at the scale. */
	int16_t outerTop;  /**< Row of that end. */
	int16_t innerLeft; /**< Column of the end at the center. */
	int16_t innerTop;  /**< Row of that end. */
} drive__Needle;

/** What the player does with the car at a step of the timer handler. */
typedef struct drive__controls_tag {
	bool gas;	/**< The gas pedal is down. */
	bool brake; /**< The brake pedal is down. */
	bool steer; /**< The wheel is turned (by globals_cursorX). */
	bool shift; /**< The gear is to change. */
} drive__Controls;

/*******************************************************************************
;
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

static void drive__notPorted(
	drive__Missing which
);

static void drive__setSurfaces(
	int16_t mode,
	int		display,
	int		work
);

static void drive__drawDashboard(
	int	 display,
	int	 view,
	bool automatic
);

static void drive__drawShifter(
	int		surface,
	int16_t shifting,
	int16_t gears
);

static void drive__drawGear(
	int		 surface,
	uint16_t record
);

static void drive__drawKnob(
	int		surface,
	int16_t place,
	bool	knob
);

static void drive__drawWheel(
	int		surface,
	int16_t position,
	int16_t shown
);

static void drive__markBox(
	int16_t		 position,
	drive__Box * pBox
);

static void drive__drawMark(
	int		surface,
	int16_t left,
	int16_t top
);

static bool drive__needleEnds(
	int16_t			value,
	bool			tachometer,
	drive__Needle * pNeedle,
	drive__Box *	pBox
);

static void drive__saveUnder(
	int				   surface,
	const drive__Box * pBox
);

static void drive__putBack(
	int				   surface,
	const drive__Box * pBox
);

static void drive__drawNeedle(
	int		surface,
	int16_t value,
	bool	tachometer
);

static int16_t drive__moveNeedle(
	int		surface,
	int16_t value,
	int16_t shown,
	bool	tachometer
);

static void drive__drawClock(
	int surface
);

static void drive__drawHand(
	int		 surface,
	int16_t	 degrees,
	int16_t	 narrow,
	uint16_t tipAt
);

static void drive__handPoint(
	fpu_Real  cosine,
	fpu_Real  sine,
	uint16_t  radiusAt,
	int16_t * pLeft,
	int16_t * pTop
);

static int16_t drive__step(void);

static int16_t drive__cruise(void);

static int16_t drive__race(void);

static void drive__begin(
	uint16_t textAt
);

static int16_t drive__playerWord(
	int offset
);

static void drive__setPlayerWord(
	int		offset,
	int16_t value
);

static bool drive__needleMoved(
	int16_t value,
	int16_t shown
);

static void drive__timerOn(void);

static void drive__timerOff(void);

static void drive__resetInput(
	int16_t mouseShift
);

static void drive__timerInterrupt(void);

static void drive__driveCar(void);

static void drive__readControls(
	drive__Controls * pControls
);

static void drive__readJoystick(
	drive__Controls * pControls
);

static void drive__autoDriver(
	drive__Controls * pControls
);

static void drive__wearParts(void);

static void drive__turnEngine(
	bool gas
);

static void drive__shiftGear(
	const drive__Controls * pControls,
	bool					stalled
);

static void drive__pickRange(void);

static void drive__setSpeed(
	bool brake
);

static void drive__steer(
	bool steer
);

static uint16_t drive__wearAt(
	int part
);

static int16_t drive__magnitude(
	int16_t value
);

static int16_t drive__raceEnd(void);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/** What drive__notPorted logs, by drive__Missing. */
static const char * const drive__missingNames[DRIVE__MISSING_COUNT] = {
	"stopping the sound (FUN_1f38_7001)",
};

/** Which of them were logged already. */
static bool drive__logged[DRIVE__MISSING_COUNT];

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Drive: show the dashboard and run the steps of the drive until
 * 			it ends.
 *
 * \return	What the last step returned (not 0).
 *
 * \details	Port of FUN_1f38_7b22, with the layout of the adapters
 * 			without a split screen (see the file's block). Both
 * 			surfaces become the screen and the palette is blanked.
 * 			The dashboard picture is drawn (drive__drawDashboard),
 * 			the road's pictures are loaded (roadpic_load), the gear
 * 			shift, the gear, the steering wheel and both needles are
 * 			drawn; with an automatic (DS:0x58e6) a clock takes the
 * 			tachometer's place (drive__drawClock). The road starts
 * 			(roadstate_start).
 * 			FUN_1f38_0f53, which loads the pictures of the steering
 * 			wheel, has no part in the port (drive__drawWheel). The
 * 			top rows of the windshield frame are copied to the
 * 			display and the palette is shown. Then each pass makes a
 * 			frame of the road (roadstate_frame, which waits 9 ticks)
 * 			and runs a step (drive__step) until that returns not 0;
 * 			after a step the gear is drawn while the record's +0x1a
 * 			is set, a needle is moved when its value is more than 1
 * 			from the one shown, and the steering wheel follows the
 * 			record's +4. At the end the display is copied to the
 * 			work surface (video table entry 5; direction taken from
 * 			its use) and the surfaces are put back.
 *
 * 			The original's loop around the set-up (DS:0x58ee above 1)
 * 			never repeats and is left out, as are DS:0x8228 (compared
 * 			with a local, no effect) and the end of a demo (DS:0x610 =
 * 			99). The port's pass also calls dos_service once.
 *
 ******************************************************************************/
int16_t drive_run(void) {
	int	 display = globals_displaySurface;
	int	 view = globals_workSurface;
	bool automatic = exe_readS16(DRIVE_AUTOMATIC) != 0;

	drive__setSurfaces(DRIVE_SURFACES_DRIVE, display, view);
	screen_resetSaved();
	video_blankPalette();
	drive__drawDashboard(display, view, automatic);
	roaddraw_view = view;
	roadpic_load(view);
	drive__drawShifter(
		display,
		drive__playerWord(DRIVING_SHIFTING),
		drive__playerWord(DRIVING_GEARS)
	);
	drive__drawGear(display, DRIVING_PLAYER);
	exe_writeU16(DRIVE_WHEEL, DRIVE_NO_MARK);
	drive__drawWheel(display, 0, DRIVE_NO_MARK);
	exe_writeU16(DRIVE_WHEEL, 0);

	int16_t shownRevs = drive__playerWord(DRIVING_REVS);
	int16_t shownSpeed = drive__playerWord(DRIVING_SPEED);

	drive__drawNeedle(display, shownSpeed, false);
	shownSpeed = drive__moveNeedle(display, shownSpeed, shownSpeed, false);

	if (!automatic) {
		drive__drawNeedle(display, shownRevs, true);
		shownRevs = drive__moveNeedle(display, shownRevs, shownRevs, true);
	} else {
		drive__drawClock(display);
	}

	*exe_data(DRIVE_UNKNOWN_824A) = 0;
	roadstate_start();

	plat_copyRect(view, display, 0, 0, PLAT_WIDTH, DRIVE_TOP_ROWS);
	video_showPalette();

	int16_t result;

	for (;;) {
		roadstate_frame();
		dos_service();
		result = drive__step();

		if (result != 0) {
			break;
		}

		int16_t wheel = drive__playerWord(DRIVING_WHEEL);
		int16_t speed = drive__playerWord(DRIVING_SPEED);
		int16_t revs = drive__playerWord(DRIVING_REVS);

		if (drive__playerWord(DRIVING_GEAR_CHANGED) != 0) {
			drive__drawGear(display, DRIVING_PLAYER);
		}

		if (drive__needleMoved(speed, shownSpeed)) {
			shownSpeed = drive__moveNeedle(display, speed, shownSpeed, false);
		}

		if (!automatic && drive__needleMoved(revs, shownRevs)) {
			shownRevs = drive__moveNeedle(display, revs, shownRevs, true);
		}

		if (wheel != exe_readS16(DRIVE_WHEEL)) {
			drive__drawWheel(display, wheel, exe_readS16(DRIVE_WHEEL));
			exe_writeU16(DRIVE_WHEEL, (uint16_t)wheel);
		}
	}

	screen_resetSaved();
	plat_copyRect(display, view, 0, 0, PLAT_WIDTH, PLAT_HEIGHT);
	drive__setSurfaces(DRIVE_SURFACES_NORMAL, display, view);

	return result;
}

/** ****************************************************************************
 *
 * \brief	Stand in for a part of driving that is not ported yet.
 *
 * \param	which			The part.
 *
 * \details	Not part of the original. Logs the part the first time.
 *
 ******************************************************************************/
static void drive__notPorted(
	drive__Missing which
) {
	if (drive__logged[which]) {
		return;
	}

	drive__logged[which] = true;
	plat_log("driving: %s is not ported yet", drive__missingNames[which]);
}

/** ****************************************************************************
 *
 * \brief	Set the surfaces and the status line for a drive, or back.
 *
 * \param	mode			DRIVE_SURFACES_DRIVE or DRIVE_SURFACES_NORMAL.
 * \param	display			The screen (DS:0x2ea).
 * \param	work			The work surface (DS:0x2e6).
 *
 * \details	Port of FUN_1f38_0dbd for its modes 3 and 1. Mode 3: drawing
 * 			and the status line go straight to the screen, and in a race
 * 			(DS:0x8aca) the status line gets the title DS:0x49de. Mode
 * 			1: the display and the work surface are apart again and
 * 			drawing is copied to the display. The status line stays in
 * 			rows 190..199 (on EGA the original puts it in rows 90..99
 * 			of video memory, which the split screen shows there). Not
 * 			ported: the EGA register writes.
 *
 ******************************************************************************/
static void drive__setSurfaces(
	int16_t mode,
	int		display,
	int		work
) {
	globals_displaySurface = display;

	if (mode == DRIVE_SURFACES_DRIVE) {
		globals_workSurface = display;
		globals_drawThrough = 0;
		ui_statusInit(UI_STATUS_ROW);

		if (*exe_data(DRIVE_RACING) != 0) {
			ui_statusTitle(exe_text(exe_readU16(DRIVE_RACE_TITLE)));
		}
	} else {
		globals_workSurface = work;
		globals_drawThrough = 1;
		ui_statusInit(UI_STATUS_ROW);
	}
}

/** ****************************************************************************
 *
 * \brief	Draw the dashboard picture: the windshield frame on the road
 * 			view and the panel on the display.
 *
 * \param	display			The screen (DS:0x7678).
 * \param	view			Surface the road is drawn on (DS:0x8188).
 * \param	automatic		The car has an automatic (DS:0x58e6).
 *
 * \details	Part of FUN_1f38_7b22. Rows 0..100 of picture 0x3f6 go to
 * 			the top of the road view, its rows 100..189 to the display
 * 			in the same place. With an automatic picture 0x4d8 is drawn
 * 			into the dashboard picture at (0x78, 0x7a) first; the port
 * 			draws it on the panel afterwards, cut off at the panel's
 * 			last row.
 *
 ******************************************************************************/
static void drive__drawDashboard(
	int	 display,
	int	 view,
	bool automatic
) {
	const lib_Resource * pDashboard =
		lib_get(lib_indexFromGameId(DRIVE_DASHBOARD));
	int16_t width = PLAT_WIDTH;

	if (pDashboard != NULL) {
		width = (int16_t)pDashboard->width;
	}

	screen_drawSpritePart(
		view,
		DRIVE_DASHBOARD,
		0,
		0,
		width,
		DRIVE_FRAME_ROWS,
		0,
		0,
		true
	);
	screen_drawSpritePart(
		display,
		DRIVE_DASHBOARD,
		0,
		DRIVE_PANEL_SOURCE,
		width,
		DRIVE_PANEL_ROWS,
		0,
		DRIVE_PANEL_TOP,
		true
	);

	const lib_Resource * pSign = NULL;

	if (automatic) {
		pSign = lib_get(lib_indexFromGameId(DRIVE_SHIFT_SIGN));
	}

	if (pSign != NULL) {
		int16_t height = (int16_t)pSign->height;
		int16_t room =
			(int16_t)(DRIVE_PANEL_TOP + DRIVE_PANEL_ROWS - DRIVE_SIGN_TOP);

		screen_drawSpritePart(
			display,
			DRIVE_SHIFT_SIGN,
			0,
			0,
			(int16_t)pSign->width,
			height < room ? height : room,
			DRIVE_SIGN_LEFT,
			DRIVE_SIGN_TOP,
			true
		);
	}
}

/** ****************************************************************************

 *
 * \brief	Draw the gate of the gear shift on the panel.
 *
 * \param	surface			The screen (DS:0x7678).
 * \param	shifting		The record's +0x24: not 0 with an automatic.
 * \param	gears			The record's +0x1c: number of gears.
 *
 * \details	Port of FUN_1f38_12f7. Picks the record of DS:0x36e
 * 			(automatic, 3 gears, otherwise 4) into DS:0x6c34, draws its
 * 			gate and sets DS:0x6c36 to -1: no knob is shown. The
 * 			original also draws each place of the knob with and without
 * 			the knob into the surface DS:0x82b4 for FUN_1f38_14f2; the
 * 			port draws them from the pictures when needed
 * 			(drive__drawKnob).
 *
 ******************************************************************************/
static void drive__drawShifter(
	int		surface,
	int16_t shifting,
	int16_t gears
) {
	int16_t kind = 0;

	if (shifting == 0) {
		kind = gears == DRIVE_THREE_GEARS ? 1 : 2;
	}

	uint16_t record = (uint16_t)(DRIVE_SHIFTERS + kind * DRIVE_SHIFTER_SIZE);

	exe_writeU16(DRIVE_SHIFTER, record);
	screen_drawSpritePart(
		surface,
		exe_readS16((uint16_t)(record + DRIVE_SHIFTER_GATE)),
		0,
		0,
		DRIVE_GATE_WIDTH,
		DRIVE_GATE_HEIGHT,
		DRIVE_GATE_LEFT,
		DRIVE_PANEL_TOP + DRIVE_GATE_TOP,
		true
	);
	exe_writeU16(DRIVE_GEAR_SHOWN, (uint16_t)-1);
}

/** ****************************************************************************

 *
 * \brief	Move the knob of the gear shift to the gear in use.
 *
 * \param	surface			The screen (DS:0x7678).
 * \param	record			DS offset of the car's driving record.
 *
 * \details	Port of FUN_1f38_14f2. The knob's place is the record's
 * 			+0x18; with an automatic (+0x24 not 0) it is 0 while +0x18
 * 			is 0, otherwise +0x24 less 1. The place shown before
 * 			(DS:0x6c36) is drawn without the knob, the new one with it,
 * 			and the record's +0x1a is cleared.
 *
 ******************************************************************************/
static void drive__drawGear(
	int		 surface,
	uint16_t record
) {
	int16_t gear = exe_readS16((uint16_t)(record + DRIVING_GEAR));
	int16_t shifting = exe_readS16((uint16_t)(record + DRIVING_SHIFTING));
	int16_t place = gear;

	if (shifting != 0) {
		place = gear == 0 ? 0 : (int16_t)(shifting - 1);
	}

	int16_t shown = exe_readS16(DRIVE_GEAR_SHOWN);

	if (shown >= 0) {
		drive__drawKnob(surface, shown, false);
	}

	drive__drawKnob(surface, place, true);
	exe_writeU16(DRIVE_GEAR_SHOWN, (uint16_t)place);
	exe_writeU16((uint16_t)(record + DRIVING_GEAR_CHANGED), 0);
}

/** ****************************************************************************

 *
 * \brief	Draw a place of the gear shift's knob, with or without the
 * 			knob.
 *
 * \param	surface			The screen.
 * \param	place			Which place of the record DS:0x6c34.
 * \param	knob			true: with the knob.
 *
 * \details	Stands in for the blits of FUN_1f38_14f2 from the pictures
 * 			FUN_1f38_12f7 made: the gate from the place's column
 * 			(rounded down to 8) on, at most 0x20 columns wide and as
 * 			many rows as the record says, and the knob over it with
 * 			color 0 see-through. The original's pictures hold the gate
 * 			only as wide as the knob and 8 columns and as high as the
 * 			knob; the port draws no more than that.
 *
 ******************************************************************************/
static void drive__drawKnob(
	int		surface,
	int16_t place,
	bool	knob
) {
	uint16_t record = exe_readU16(DRIVE_SHIFTER);
	int16_t	 gateId = exe_readS16((uint16_t)(record + DRIVE_SHIFTER_GATE));
	int16_t	 knobId = exe_readS16((uint16_t)(record + DRIVE_SHIFTER_KNOB));
	uint16_t places = exe_readU16((uint16_t)(record + DRIVE_SHIFTER_PLACES));

	const uint8_t *		 pPlace = exe_data((uint16_t)(places + place * 2));
	const lib_Resource * pKnob = lib_get(lib_indexFromGameId(knobId));

	if (pKnob == NULL) {
		return;
	}

	int16_t column = (int8_t)pPlace[0];
	int16_t row = (int8_t)pPlace[1];
	int16_t aligned = (int16_t)(column & DRIVE_BYTE_MASK);
	int16_t width = (int16_t)(DRIVE_GATE_WIDTH - aligned);
	int16_t height = exe_readS16((uint16_t)(record + DRIVE_SHIFTER_ROWS));
	int16_t knobWidth = (int16_t)pKnob->width;
	int16_t knobHeight = (int16_t)pKnob->height;

	if (width > DRIVE_KNOB_WIDTH) {
		width = DRIVE_KNOB_WIDTH;
	}

	if (width > knobWidth + DRIVE_KNOB_MARGIN) {
		width = (int16_t)(knobWidth + DRIVE_KNOB_MARGIN);
	}

	if (height > knobHeight) {
		height = knobHeight;
	}

	int16_t top = (int16_t)(DRIVE_PANEL_TOP + DRIVE_GATE_TOP + row);

	screen_drawSpritePart(
		surface,
		gateId,
		aligned,
		row,
		width,
		height,
		(int16_t)(DRIVE_GATE_LEFT + aligned),
		top,
		true
	);

	if (!knob) {
		return;
	}

	if (knobWidth > aligned + width - column) {
		knobWidth = (int16_t)(aligned + width - column);
	}

	screen_drawSpritePart(
		surface,
		knobId,
		0,
		0,
		knobWidth,
		height,
		(int16_t)(DRIVE_GATE_LEFT + column),
		top,
		false
	);
}

/** ****************************************************************************

 *
 * \brief	Show the steering wheel for a position: its picture and the
 * 			mark above it.
 *
 * \param	surface			The screen (DS:0x7678).
 * \param	position		The record's +4, -24..24; 0 is straight ahead.
 * \param	shown			Position of the mark that is shown; DRIVE_NO_MARK
 * 							for none.
 *
 * \details	Port of FUN_1f38_1130 without the split screen. The picture
 * 			of the wheel (DS:0x58e8: ahead, left for a position below
 * 			0, right) is drawn when it is not the one shown (DS:0x350);
 * 			the port draws it from the resource, the original from the
 * 			copies FUN_1f38_0f53 made. Then the screen under the old
 * 			mark is put back, the screen under the new one (table
 * 			DS:0x31e) is saved and the mark is drawn (FUN_3645_3abf
 * 			between FUN_3645_23f0 and 2410, which set the rows drawing
 * 			is cut to; the mark lies inside them).
 *
 ******************************************************************************/
static void drive__drawWheel(
	int		surface,
	int16_t position,
	int16_t shown
) {
	uint16_t picture = (uint16_t)((position != 0) + (position > 0));

	if (picture != exe_readU16(DRIVE_WHEEL_SHOWN)) {
		screen_drawSpritePart(
			surface,
			exe_readS16((uint16_t)(DRIVE_WHEEL_IDS + picture * 2)),
			0,
			0,
			DRIVE_WHEEL_WIDTH,
			DRIVE_WHEEL_HEIGHT,
			DRIVE_WHEEL_LEFT,
			DRIVE_PANEL_TOP + DRIVE_WHEEL_TOP,
			true
		);
		exe_writeU16(DRIVE_WHEEL_SHOWN, picture);
	}

	drive__Box box;

	if (shown != DRIVE_NO_MARK) {
		drive__markBox(shown, &box);
		drive__putBack(surface, &box);
	}

	drive__markBox(position, &box);
	drive__saveUnder(surface, &box);
	drive__drawMark(
		surface,
		(int16_t)(box.left + DRIVE_MARK_REACH),
		(int16_t)(box.top + DRIVE_MARK_REACH)
	);
}

/** ****************************************************************************

 *
 * \brief	The rectangle of the steering wheel's mark at a position.
 *
 * \param	position		Position of the steering wheel.
 * \param	pBox			Gets the rectangle.
 *
 * \details	Part of FUN_1f38_1130: 5 by 5 pixels around the place the
 * 			table DS:0x31e gives for the position.
 *
 ******************************************************************************/
static void drive__markBox(
	int16_t		 position,
	drive__Box * pBox
) {
	const uint8_t * pPlace =
		exe_data((uint16_t)(DRIVE_MARK_PLACES + position * 2));

	pBox->left = (int16_t)(pPlace[0] - DRIVE_MARK_REACH);
	pBox->top = (int16_t)(DRIVE_PANEL_TOP + pPlace[1] - DRIVE_MARK_REACH);
	pBox->width = DRIVE_MARK_SIZE;
	pBox->height = DRIVE_MARK_SIZE;
}

/** ****************************************************************************

 *
 * \brief	Draw the mark of the steering wheel.
 *
 * \param	surface			The screen.
 * \param	left			Column of the mark's middle.
 * \param	top				Row of the mark's middle.
 *
 * \details	Port of FUN_3645_3abf and of FUN_3645_2c0a for its last
 * 			argument 1: the first shape of the list DS:0x60a0 points
 * 			to. A shape is a number of spans behind a far pointer; a
 * 			span is a row of pixels in one color (function table entry
 * 			13, DS:0x78d6; its end is taken as not drawn, which makes
 * 			the mark 5 by 5).
 *
 ******************************************************************************/
static void drive__drawMark(
	int		surface,
	int16_t left,
	int16_t top
) {
	uint16_t list = exe_readU16(DRIVE_MARK_SHAPES);

	if (exe_readS16(list) < 1) {
		return;
	}

	uint16_t shape = exe_readU16((uint16_t)(list + 2));
	int16_t	 spans = exe_readS16(shape);

	if (spans < 1) {
		return;
	}

	const uint8_t * pSpan = exe_far(
		exe_readU16((uint16_t)(shape + 4)),
		exe_readU16((uint16_t)(shape + 2)),
		(uint16_t)(spans * DRIVE_SPAN_SIZE)
	);

	if (pSpan == NULL) {
		return;
	}

	plat_selectPage(surface);

	for (int16_t span = 0; span < spans; span++) {
		plat_fillRect(
			left + (int8_t)pSpan[0],
			top + (int8_t)pSpan[1],
			(int8_t)pSpan[2],
			1,
			pSpan[3]
		);
		pSpan += DRIVE_SPAN_SIZE;
	}
}

/** ****************************************************************************
 *
 * \brief	Draw the mark of the steering wheel on the crash picture.
 *
 * \param	position		Position of the steering wheel (DS:0x82c2).
 *
 * \details	Port of FUN_1f38_1094. The mark is drawn at its place on
 * 			the panel (table DS:0x31e, DRIVE_PANEL_TOP rows down) of
 * 			the work page (DS:0x787a: segment 0xa200 on EGA), cut to
 * 			the rows DRIVE_PANEL_TOP..DRIVE_CRASH_LAST (DS:0x5e50,
 * 			DS:0x5e52); the clip box is put back to the view's
 * 			afterwards (roaddraw_clipView). The original cuts span by
 * 			span; the port leaves out a mark that is not whole inside
 * 			the rows, which no position of the wheel gives.
 *
 ******************************************************************************/
void drive_crashMark(
	int16_t position
) {
	const uint8_t * pPlace =
		exe_data((uint16_t)(DRIVE_MARK_PLACES + position * 2));
	int16_t top = (int16_t)(DRIVE_PANEL_TOP + pPlace[1]);

	if (top - DRIVE_MARK_REACH >= DRIVE_PANEL_TOP &&
		top + DRIVE_MARK_REACH <= DRIVE_CRASH_LAST) {
		drive__drawMark(globals_workSurface, pPlace[0], top);
	}

	roaddraw_clipView();
}

/** ****************************************************************************

 *
 * \brief	Where a needle lies for a value.
 *
 * \param	value			Speed in mph, or the revs.
 * \param	tachometer		true: the tachometer's needle.
 * \param	pNeedle			Gets the ends of the needle.
 * \param	pBox			Gets the rectangle around it.
 *
 * \retval	true	The needle can be drawn.
 * \retval	false	The needle table is not in the game's program.
 *
 * \details	Port of FUN_1f38_0137 (speedometer) and FUN_1f38_02ae
 * 			(tachometer, called with the revs / 0x2b), without the
 * 			split screen. The position, 0..0x82, picks the two ends
 * 			from the table in the segment DS:0x68e4 (s8 column, row
 * 			up from the gauge's center; outer ends at 0, inner ends at
 * 			0x106). The rectangle is what the callers work out.
 *
 ******************************************************************************/
static bool drive__needleEnds(
	int16_t			value,
	bool			tachometer,
	drive__Needle * pNeedle,
	drive__Box *	pBox
) {
	const uint8_t * pOuter =
		exe_far(exe_readU16(DRIVE_NEEDLE_SEGMENT), 0, DRIVE_NEEDLE_BYTES);

	if (pOuter == NULL) {
		return false;
	}

	if (tachometer) {
		value = (int16_t)(value / DRIVE_REVS_STEP);
	}

	if (value < 0) {
		value = 0;
	} else if (value > DRIVE_NEEDLE_LAST) {
		value = DRIVE_NEEDLE_LAST;
	}

	pOuter += value * 2;

	const uint8_t * pInner = pOuter + DRIVE_NEEDLE_INNER;

	int16_t column = tachometer ? DRIVE_REVS_COLUMN : DRIVE_SPEED_COLUMN;
	int16_t row = DRIVE_PANEL_TOP + DRIVE_NEEDLE_ROW;

	pNeedle->outerLeft = (int16_t)(column + (int8_t)pOuter[0]);
	pNeedle->outerTop = (int16_t)(row - (int8_t)pOuter[1]);
	pNeedle->innerLeft = (int16_t)(column + (int8_t)pInner[0]);
	pNeedle->innerTop = (int16_t)(row - (int8_t)pInner[1]);

	int16_t across = (int16_t)(pNeedle->outerLeft - pNeedle->innerLeft);
	int16_t down = (int16_t)(pNeedle->outerTop - pNeedle->innerTop);

	pBox->left = across < 0 ? pNeedle->outerLeft : pNeedle->innerLeft;
	pBox->top = down < 0 ? pNeedle->outerTop : pNeedle->innerTop;
	pBox->width = (int16_t)((across < 0 ? -across : across) + 1);
	pBox->height = (int16_t)((down < 0 ? -down : down) + 1);

	return true;
}

/** ****************************************************************************

 *
 * \brief	Save what the screen shows in a rectangle.
 *
 * \param	surface			The screen (DS:0x7678).
 * \param	pBox			The rectangle.
 *
 * \details	Port of FUN_1f38_0098: the rectangle starts at a column of
 * 			8 and its width is rounded down to 8 and 16 more. The
 * 			original copies it to one of three places of the surface
 * 			DS:0x82b4 (mark, speedometer, tachometer); the port keeps
 * 			it at its own place on a page of its own, where the three
 * 			never overlap.
 *
 ******************************************************************************/
static void drive__saveUnder(
	int				   surface,
	const drive__Box * pBox
) {
	plat_copyRect(
		surface,
		DRIVE_PANEL_PAGE,
		pBox->left & DRIVE_BYTE_MASK,
		pBox->top,
		(pBox->width & DRIVE_BYTE_MASK) + DRIVE_UNDER_EXTRA,
		pBox->height
	);
}

/** ****************************************************************************

 *
 * \brief	Put back what drive__saveUnder saved for a rectangle.
 *
 * \param	surface			The screen (DS:0x7678).
 * \param	pBox			The rectangle.
 *
 * \details	Port of FUN_1f38_000a.
 *
 ******************************************************************************/
static void drive__putBack(
	int				   surface,
	const drive__Box * pBox
) {
	plat_copyRect(
		DRIVE_PANEL_PAGE,
		surface,
		pBox->left & DRIVE_BYTE_MASK,
		pBox->top,
		(pBox->width & DRIVE_BYTE_MASK) + DRIVE_UNDER_EXTRA,
		pBox->height
	);
}

/** ****************************************************************************

 *
 * \brief	Draw a needle for the first time.
 *
 * \param	surface			The screen (DS:0x7678).
 * \param	value			Speed in mph, or the revs.
 * \param	tachometer		true: the tachometer's needle.
 *
 * \details	Port of FUN_1f38_01d2 (speedometer) and FUN_1f38_0349
 * 			(tachometer): the screen under the needle is saved and the
 * 			needle is drawn as a line in color 0xb (EGA and VGA).
 *
 ******************************************************************************/
static void drive__drawNeedle(
	int		surface,
	int16_t value,
	bool	tachometer
) {
	drive__Needle needle;
	drive__Box	  box;

	if (!drive__needleEnds(value, tachometer, &needle, &box)) {
		return;
	}

	drive__saveUnder(surface, &box);
	screen_line(
		surface,
		needle.outerLeft,
		needle.outerTop,
		needle.innerLeft,
		needle.innerTop,
		DRIVE_NEEDLE_BRIGHT
	);
}

/** ****************************************************************************

 *
 * \brief	Move a needle to a new value.
 *
 * \param	surface			The screen (DS:0x7678).
 * \param	value			Speed in mph, or the revs.
 * \param	shown			The value the needle shows.
 * \param	tachometer		true: the tachometer's needle.
 *
 * \return	value: what the needle shows now.
 *
 * \details	Port of FUN_1f38_0425. The color DS:0x2d2 is set the first
 * 			time (0xb on EGA and VGA). The screen under the old needle
 * 			is put back, the screen under the new one is saved and the
 * 			needle is drawn.
 *
 ******************************************************************************/
static int16_t drive__moveNeedle(
	int		surface,
	int16_t value,
	int16_t shown,
	bool	tachometer
) {
	drive__Needle needle;
	drive__Box	  box;

	if (exe_readS16(DRIVE_NEEDLE_COLOR) < 0) {
		exe_writeU16(DRIVE_NEEDLE_COLOR, DRIVE_NEEDLE_BRIGHT);
	}

	if (drive__needleEnds(shown, tachometer, &needle, &box)) {
		drive__putBack(surface, &box);
	}

	if (drive__needleEnds(value, tachometer, &needle, &box)) {
		drive__saveUnder(surface, &box);
		screen_line(
			surface,
			needle.outerLeft,
			needle.outerTop,
			needle.innerLeft,
			needle.innerTop,
			(uint8_t)exe_readU16(DRIVE_NEEDLE_COLOR)
		);
	}

	return value;
}

/** ****************************************************************************
 *
 * \brief	Draw the clock an automatic shows in place of the tachometer.
 *
 * \param	surface			The screen (DS:0x7678).
 *
 * \details	Port of FUN_1f38_05f2, without the split screen (rows +
 * 			100); the original draws straight to video memory (DS:0x787a,
 * 			DS:0x767c). The color DS:0x2d2 is set the first time (0xb
 * 			on EGA and VGA). The hour shown is (hour index / 2 + 20)
 * 			modulo 12, plus 1, of the time of day (TimeOfDay). The
 * 			hour hand turns 30 degrees an hour and half a degree a
 * 			minute (half the minutes, rounded down), the minute hand 6
 * 			degrees a minute; the angles are counted from 3 o'clock
 * 			against the clock. The clock is drawn once and nothing
 * 			under the hands is saved.
 *
 ******************************************************************************/
static void drive__drawClock(
	int surface
) {
	if (exe_readS16(DRIVE_NEEDLE_COLOR) < 0) {
		exe_writeU16(DRIVE_NEEDLE_COLOR, DRIVE_NEEDLE_BRIGHT);
	}

	uint16_t hourIndex;
	uint16_t minuteIndex;

	calendar_timeOfDay(&hourIndex, &minuteIndex);

	int16_t minute = (int16_t)minuteIndex;
	int16_t hour = (int16_t)(((int16_t)hourIndex >> 1) + DRIVE_CLOCK_SHIFT);

	hour = (int16_t)(hour % DRIVE_CLOCK_HOURS + 1);

	drive__drawHand(
		surface,
		(int16_t)-(DRIVE_CLOCK_PER_HOUR * hour + (minute >> 1) - DRIVE_CLOCK_UP),
		DRIVE_CLOCK_HOUR_OFF,
		DRIVE_CLOCK_HOUR
	);
	drive__drawHand(
		surface,
		(int16_t)-(DRIVE_CLOCK_PER_MIN * minute - DRIVE_CLOCK_UP),
		DRIVE_CLOCK_MIN_OFF,
		DRIVE_CLOCK_MINUTE
	);
}

/** ****************************************************************************
 *
 * \brief	Draw a hand of the clock.
 *
 * \param	surface			The screen (DS:0x7678).
 * \param	degrees			The hand's angle, from 3 o'clock against the
 * 							clock.
 * \param	narrow			Degrees from the hand to its corners when it
 * 							is not near an axis.
 * \param	tipAt			DS offset of the real with the tip's distance
 * 							from the center.
 *
 * \details	Half of FUN_1f38_05f2, which has this code twice. The hand
 * 			has four points: its start (5 from the center) and its tip
 * 			on the angle, and two corners 9 from the center, as many
 * 			degrees to either side as narrow, or 8 when the hand is
 * 			less than 10 degrees from an axis. Lines join the start
 * 			with the other three and the tip with the corners. The
 * 			angle in radians is degrees * pi / 180 on the 8087; for
 * 			the start and the tip the cosine takes it from the
 * 			register and the sine from a qword it was stored in.
 *
 ******************************************************************************/
static void drive__drawHand(
	int		 surface,
	int16_t	 degrees,
	int16_t	 narrow,
	uint16_t tipAt
) {
	fpu_Real pi = fpu_fromDouble(exe_data(DRIVE_CLOCK_PI));
	fpu_Real half = fpu_fromDouble(exe_data(DRIVE_CLOCK_HALF));
	int16_t	 fromAxis = (int16_t)(degrees % DRIVE_CLOCK_UP);

	if (fromAxis < 0) {
		fromAxis = (int16_t)-fromAxis;
	}

	int16_t spread = narrow;

	if (fromAxis < DRIVE_CLOCK_NEAR ||
		fromAxis > DRIVE_CLOCK_UP - DRIVE_CLOCK_NEAR) {
		spread = DRIVE_CLOCK_WIDE;
	}

	int16_t	 left[DRIVE_CLOCK_POINTS];
	int16_t	 top[DRIVE_CLOCK_POINTS];
	fpu_Real angle = fpu_div(fpu_mul(fpu_fromInt(degrees), pi), half);
	fpu_Real cosine = fpu_cos(angle);
	fpu_Real sine = fpu_sin(fpu_toDouble(angle));

	drive__handPoint(cosine, sine, DRIVE_CLOCK_HUB, &left[0], &top[0]);
	drive__handPoint(cosine, sine, tipAt, &left[1], &top[1]);

	angle = fpu_div(fpu_mul(fpu_fromInt(degrees + spread), pi), half);
	drive__handPoint(
		fpu_cos(angle),
		fpu_sin(angle),
		DRIVE_CLOCK_SIDE,
		&left[2],
		&top[2]
	);

	angle = fpu_div(fpu_mul(fpu_fromInt(degrees - spread), pi), half);
	drive__handPoint(
		fpu_cos(angle),
		fpu_sin(angle),
		DRIVE_CLOCK_SIDE,
		&left[3],
		&top[3]
	);

	uint8_t color = (uint8_t)exe_readU16(DRIVE_NEEDLE_COLOR);

	for (int from = 0; from < 2; from++) {
		for (int to = from + 1; to < DRIVE_CLOCK_POINTS; to++) {
			screen_line(
				surface,
				left[from],
				top[from],
				left[to],
				top[to],
				color
			);
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Work out a point of a hand of the clock.
 *
 * \param	cosine			Cosine of the point's angle.
 * \param	sine			Sine of the point's angle.
 * \param	radiusAt		DS offset of the real with the point's
 * 							distance from the center.
 * \param	pLeft			Out: the point's column.
 * \param	pTop			Out: the point's row.
 *
 * \details	Part of FUN_1f38_05f2: the products with the distance are
 * 			cut to integers (_ftol) and counted from the tachometer's
 * 			center (0x91, 0x2a), the row upwards.
 *
 ******************************************************************************/
static void drive__handPoint(
	fpu_Real  cosine,
	fpu_Real  sine,
	uint16_t  radiusAt,
	int16_t * pLeft,
	int16_t * pTop
) {
	fpu_Real radius = fpu_fromDouble(exe_data(radiusAt));
	int16_t	 across = (int16_t)fpu_toLong(fpu_mul(cosine, radius));
	int16_t	 up = (int16_t)fpu_toLong(fpu_mul(sine, radius));

	*pLeft = (int16_t)(DRIVE_REVS_COLUMN + across);
	*pTop = (int16_t)(DRIVE_PANEL_TOP - up + DRIVE_NEEDLE_ROW);
}

/** ****************************************************************************
 *
 * \brief	Run a step of the drive.
 *
 * \retval	0				The drive goes on.
 * \retval	other			The drive is over.
 *
 * \details	Port of FUN_1000_8ea8, whose two halves are drive__cruise
 * 			(DS:0x8aca is 0) and drive__race.
 *
 ******************************************************************************/
static int16_t drive__step(void) {
	if (*exe_data(DRIVE_RACING) == 0) {
		return drive__cruise();
	}

	return drive__race();
}

/** ****************************************************************************
 *
 * \brief	Run a step of a cruise to a place.
 *
 * \retval	0				The cruise goes on.
 * \retval	other			The car is there, or the player ended it.
 *
 * \details	Half of FUN_1000_8ea8. Step 0 (DS:0x79a6) starts the drive
 * 			(drive__begin) with "Cruising in town...", in the first gear
 * 			(record +0x18), and makes +0x24 2 if it was 1.
 * 			Afterwards Esc, a click key, the mouse button or a
 * 			joystick button end the cruise, as does bit 0
 * 			of the side flags (road_table) at the tile after the car's
 * 			(DS:0x7d30). Otherwise the revs rise by the
 * 			gear's step (DS:0x51d8) up to 4700, the speed is the revs
 * 			by the gear's revs per mph (DS:0x51e2), and from 4501 revs
 * 			the car shifts up while it has gears left (record +0x1c),
 * 			after which the revs stay.
 *
 ******************************************************************************/
static int16_t drive__cruise(void) {
	int16_t state = exe_readS16(DRIVE_STATE);

	if (state == 0) {
		drive__begin(DRIVE_TEXT_CRUISE);
		drive__setPlayerWord(DRIVING_GEAR_CHANGED, 1);
		drive__setPlayerWord(DRIVING_GEAR, 1);

		if (drive__playerWord(DRIVING_SHIFTING) == 1) {
			drive__setPlayerWord(DRIVING_SHIFTING, 2);
		}

		exe_writeU16(DRIVE_CRUISE_STEP, exe_readU16(DRIVE_CRUISE_STEPS));

		return 0;
	}

	if (state != 1) {
		return state;
	}

	if (globals_demoMode == 0) {
		int8_t key = input_readKey();

		if (key == DRIVE_KEY_ESCAPE || key == GLOBALS_CLICK_KEY) {
			globals_clickKeyFlag2 = 1;
		}

		if (globals_mousePresent != 0) {
			input_mouseReadButtons();
			globals_clickKeyFlag2 |= globals_mouseLeft;
		}

		if (globals_joystickPresent != 0) {
			input_readJoystick();
			globals_clickKeyFlag2 |= globals_joystickButtons;
		}
	}

	uint16_t next = (uint16_t)(exe_readS16(DRIVE_PLACE) + 1);
	bool	 there = (road_table[ROAD_LEFT + next] & 1) != 0 ||
		(road_table[ROAD_RIGHT + next] & 1) != 0;

	if (!there && globals_clickKeyFlag2 == 0) {
		int16_t gear = drive__playerWord(DRIVING_GEAR);
		int16_t step = exe_readS16(DRIVE_CRUISE_STEP);
		int16_t revs = (int16_t)(drive__playerWord(DRIVING_REVS) + step);

		if (revs > DRIVE_CRUISE_TOP) {
			revs = DRIVE_CRUISE_TOP;
		}

		drive__setPlayerWord(DRIVING_REVS, revs);

		if (gear != 0) {
			int16_t perMph =
				exe_readS16((uint16_t)(DRIVE_CRUISE_REVS + gear * 2));

			drive__setPlayerWord(DRIVING_SPEED, (int16_t)(revs / perMph));
		}

		if (revs < DRIVE_CRUISE_SHIFT) {
			return 0;
		}

		if (gear >= drive__playerWord(DRIVING_GEARS)) {
			exe_writeU16(DRIVE_CRUISE_STEP, 0);

			return 0;
		}

		gear++;
		drive__setPlayerWord(DRIVING_GEAR_CHANGED, 1);
		drive__setPlayerWord(DRIVING_GEAR, gear);
		exe_writeU16(
			DRIVE_CRUISE_STEP,
			exe_readU16((uint16_t)(DRIVE_CRUISE_STEPS + gear * 2))
		);

		int16_t perMph = exe_readS16((uint16_t)(DRIVE_CRUISE_REVS + gear * 2));
		int16_t speed = drive__playerWord(DRIVING_SPEED);

		drive__setPlayerWord(DRIVING_REVS, (int16_t)(perMph * speed));

		return 0;
	}

	globals_leftClick = 0;
	globals_buttonsHeld = 0;
	drive__timerOff();

	return 1;
}

/** ****************************************************************************
 *
 * \brief	Run a step of a race.
 *
 * \retval	0				The race goes on.
 * \retval	other			The race is over; its outcome is in DS:0x799c
 * 							and DS:0x7a9e.
 *
 * \details	Half of FUN_1000_8ea8, by DS:0x79a6. 0: the drive starts
 * 			(drive__begin) with "Get ready !", which stays for 110 to
 * 			212 ticks (DS:0x5cc). 1: a car that moves in that time
 * 			jumped the light (flags 0x81, the race is over); when the
 * 			time is up "Go !" shows (DS:0x7d66 = 1). 2: the cars move
 * 			(driving_move) until there is an outcome; once the player's
 * 			car moves "Go !" is cleared. 3: the cars move until there is
 * 			an outcome. Steps 0 and 1 return the outcome so far, so a
 * 			race with one is over at once.
 *
 * 			The player's car is driven by drive__timerInterrupt meanwhile.
 * 			With --race-result step 2 ends the race at once so test runs
 * 			reach race_result without driving.
 *
 ******************************************************************************/
static int16_t drive__race(void) {
	int16_t state = exe_readS16(DRIVE_STATE);
	int16_t speed = drive__playerWord(DRIVING_SPEED);

	switch (state) {
		case 0:
			drive__begin(DRIVE_TEXT_READY);
			globals_timeout = globals_ticks + DRIVE_READY_TICKS +
				(uint32_t)(int32_t)random_next(DRIVE_READY_RANGE);
			break;

		case 1:
			if (speed >= 1) {
				drive__timerOff();
				*exe_data(DRIVE_FLAGS) |= DRIVE_JUMPED_LIGHT;

				return 1;
			}

			if ((int32_t)globals_timeout <= (int32_t)globals_ticks) {
				exe_writeU16(DRIVE_STATE, 2);
				ui_status(-1, exe_text(DRIVE_TEXT_GO), 0);
				exe_writeU16(DRIVE_GO, 1);
			}

			break;

		case 2:
			if (race_testFlags != RACE_NO_TEST) {
				drive__timerOff();

				return 1;
			}

			driving_move();

			if (exe_readU16(DRIVE_FLAGS) != 0 ||
				exe_readU16(DRIVE_WON_FLAGS) != 0) {
				drive__timerOff();

				return 1;
			}

			if (drive__playerWord(DRIVING_SPEED) > 0) {
				ui_status(-1, exe_text(DRIVE_TEXT_BLANK), 0);
				exe_writeU16(DRIVE_STATE, 3);
			}

			return 0;

		case 3:
			if (exe_readU16(DRIVE_FLAGS) != 0 ||
				exe_readU16(DRIVE_WON_FLAGS) != 0) {
				drive__timerOff();

				return 1;
			}

			driving_move();

			return 0;

		default:
			return state;
	}

	return (int16_t)(exe_readU16(DRIVE_FLAGS) | exe_readU16(DRIVE_WON_FLAGS));
}

/** ****************************************************************************
 *
 * \brief	Start the drive at its first step.
 *
 * \param	textAt			DS offset of the text for the status line.
 *
 * \details	The start both halves of FUN_1000_8ea8 share: DS:0x5d6 is
 * 			cleared, the sound is stopped unless the car drives home
 * 			after a race (DS:0x51ec; FUN_1f38_7001, not ported), the
 * 			timer handler of a drive is installed (drive__timerOn),
 * 			waiting input is dropped, DS:0x79a6 becomes 1 and the text
 * 			is shown.
 *
 ******************************************************************************/
static void drive__begin(
	uint16_t textAt
) {
	exe_writeU16(DRIVE_UNKNOWN_5D6, 0);

	if (exe_readS16(DRIVE_HOME) == 0) {
		drive__notPorted(DRIVE__SOUND_OFF);
	}

	drive__timerOn();
	input_flush();
	exe_writeU16(DRIVE_STATE, 1);
	ui_status(-1, exe_text(textAt), 0);
}

/** ****************************************************************************
 *
 * \brief	Read a word of the player's driving record.
 *
 * \param	offset			Offset of the word (DRIVING_...).
 *
 * \return	The word.
 *
 ******************************************************************************/
static int16_t drive__playerWord(
	int offset
) {
	return exe_readS16((uint16_t)(DRIVING_PLAYER + offset));
}

/** ****************************************************************************
 *
 * \brief	Set a word of the player's driving record.
 *
 * \param	offset			Offset of the word (DRIVING_...).
 * \param	value			Its new value.
 *
 ******************************************************************************/
static void drive__setPlayerWord(
	int		offset,
	int16_t value
) {
	exe_writeU16((uint16_t)(DRIVING_PLAYER + offset), (uint16_t)value);
}

/** ****************************************************************************
 *
 * \brief	Tell if a needle has to move.
 *
 * \param	value			The value to show.
 * \param	shown			The value the needle shows.
 *
 * \retval	true			They are more than 1 apart.
 * \retval	false			The needle can stay.
 *
 * \details	The test of FUN_1f38_7b22 before FUN_1f38_0425, with the
 * 			original's 16-bit difference.
 *
 ******************************************************************************/
static bool drive__needleMoved(
	int16_t value,
	int16_t shown
) {
	int16_t difference = (int16_t)(value - shown);

	if (difference < 0) {
		difference = (int16_t)-difference;
	}

	return difference > DRIVE_NEEDLE_STEP;
}

/** ****************************************************************************
 *
 * \brief	Install the timer handler of a drive.
 *
 * \details	Port of FUN_1000_1aad. DS:0x60a is cleared, drive__timerInterrupt
 * 			takes the place of input_timerInterrupt, the mouse's rows are
 * 			divided by 64 (drive__resetInput), the steering of the
 * 			player's record (+2, +4) and the wait for the first step
 * 			(DS:0x609) are cleared. The original also sets up the speaker
 * 			(not ported; DS:0x47cf says it is on).
 *
 ******************************************************************************/
static void drive__timerOn(void) {
	exe_writeU16(DRIVE_UNKNOWN_60A, 0);
	dos_setTimerHandler(drive__timerInterrupt);
	drive__resetInput(DRIVE_MOUSE_SHIFT);
	drive__setPlayerWord(DRIVING_WHEEL, 0);
	drive__setPlayerWord(DRIVING_STEERING, 0);
	*exe_data(DRIVE_STEP_WAIT) = 0;

	if (globals_soundOn != 0) {
		globals_speakerOn = 1;
	}
}

/** ****************************************************************************
 *
 * \brief	Put the usual timer handler back after a drive.
 *
 * \details	Port of FUN_1000_1b27. DS:0x58dc, DS:0x58da and DS:0x5e0e are
 * 			cleared, the speaker is turned off (DS:0x47cf) and the mouse
 * 			gets its usual steps back (drive__resetInput).
 *
 ******************************************************************************/
static void drive__timerOff(void) {
	dos_setTimerHandler(input_timerInterrupt);
	exe_writeU16(DRIVE_UNKNOWN_58DC, 0);
	exe_writeU16(DRIVE_UNKNOWN_58DA, 0);
	exe_writeU16(DRIVE_SLOWED, 0);
	globals_speakerOn = 0;
	drive__resetInput(1);
}

/** ****************************************************************************
 *
 * \brief	Drop the mouse's motion and the clicks and directions so far.
 *
 * \param	mouseShift		New globals_mouseShiftY.
 *
 * \details	The part FUN_1000_1aad and FUN_1000_1b27 share.
 *
 ******************************************************************************/
static void drive__resetInput(
	int16_t mouseShift
) {
	if (globals_demoMode == 0 && globals_mousePresent != 0) {
		input_mousePoll();
		globals_mouseMickeysY = 0;
		globals_mouseMickeysX = 0;
		globals_mouseShiftX = 1;
		globals_mouseShiftY = mouseShift;
	}

	globals_leftClick = 0;
	globals_buttonsHeld = 0;
	globals_lastDirectionY = 0;
	globals_lastDirectionX = 0;
}

/** ****************************************************************************
 *
 * \brief	Timer interrupt handler of a drive: count a tick and, in a
 * 			race, drive the player's car.
 *
 * \details	Port of TimerIsr2 (1000:238c), run 72.8 times a second. Every
 * 			4th tick the original called the BIOS handler; every 3rd of
 * 			those (DS:0x609; 6.07 times a second), in a race (DS:0x8aca)
 * 			with DS:0x286 at 0, the car is driven (drive__driveCar)
 * 			unless that is still running (DS:0x6e8). Not ported: the
 * 			sound of the engine on the speaker, made in the other ticks.
 *
 ******************************************************************************/
static void drive__timerInterrupt(void) {
	uint8_t countdown = globals_biosCountdown;

	globals_ticks++;
	globals_biosCountdown--;

	if (countdown != 0) {
		return;
	}

	/* The original chained to the BIOS handler (time of day) here. */
	globals_biosCountdown = DRIVE_BIOS_EVERY;
	globals_biosTicks++;

	if (*exe_data(DRIVE_RACING) == 0 || exe_readS16(DRIVE_UNKNOWN_286) != 0) {
		return;
	}

	int8_t wait = (int8_t)(*exe_data(DRIVE_STEP_WAIT) - 1);

	*exe_data(DRIVE_STEP_WAIT) = (uint8_t)wait;

	if (wait > 0) {
		return;
	}

	*exe_data(DRIVE_STEP_WAIT) = DRIVE_STEP_EVERY;

	if (*exe_data(DRIVE_STEP_BUSY) != 0) {
		return;
	}

	*exe_data(DRIVE_STEP_BUSY) = 1;
	drive__driveCar();
	*exe_data(DRIVE_STEP_BUSY) = 0;
}

/** ****************************************************************************
 *
 * \brief	Drive the player's car for a step of the timer handler.
 *
 * \details	The body of TimerIsr2 (1000:238c), in its order: the controls
 * 			are read (drive__readControls; the automatic driver of a demo
 * 			or the auto switch adds its own, drive__autoDriver); the
 * 			distance (DS:0x82bc) grows by the speed and the record's time
 * 			(+0xc0) by 1; past the end of the race (DS:0x7d30 above
 * 			drive__raceEnd) the car brakes and loses 15 mph more, and it
 * 			brakes while DS:0x54da counts down; the parts wear
 * 			(drive__wearParts), the engine turns (drive__turnEngine), the
 * 			gear changes (drive__shiftGear) when the player shifts, when
 * 			the car in gear is at 4 mph or less without gas, or when an
 * 			automatic in drive (+0x24 above 1) is above its shift revs
 * 			(+0x22) with gas and has gears left or is in the first range
 * 			of a gear above the first without gas; then the range of the
 * 			gear (drive__pickRange), the speed (drive__setSpeed) and the
 * 			steering (drive__steer) follow. At the end of a step of a
 * 			race the road code checks the car (roadstate_check); a
 * 			crash stops the car with the flags 0x21.
 *
 ******************************************************************************/
static void drive__driveCar(void) {
	drive__Controls controls;

	drive__readControls(&controls);

	if (globals_demoMode != 0 || globals_autoSwitch != 0) {
		drive__autoDriver(&controls);
	}

	int16_t	 speed = drive__playerWord(DRIVING_SPEED);
	uint32_t distance = exe_readU16(DRIVE_DISTANCE) |
		(uint32_t)exe_readU16(DRIVE_DISTANCE + 2) << 16;

	distance += (uint32_t)(int32_t)speed;
	exe_writeU16(DRIVE_DISTANCE, (uint16_t)distance);
	exe_writeU16(DRIVE_DISTANCE + 2, (uint16_t)(distance >> 16));
	drive__setPlayerWord(
		DRIVING_TIME,
		(int16_t)(drive__playerWord(DRIVING_TIME) + 1)
	);

	if (drive__raceEnd() < exe_readS16(DRIVE_PLACE)) {
		controls.brake = true;
		controls.steer = false;
		controls.gas = false;

		if (speed > DRIVE_PAST_END) {
			drive__setPlayerWord(
				DRIVING_SPEED,
				(int16_t)(speed - DRIVE_PAST_END)
			);
		}
	}

	int16_t forced = exe_readS16(DRIVE_FORCED_BRAKE);

	if (forced > 0) {
		controls.brake = true;
		controls.steer = false;
		controls.gas = false;
		exe_writeU16(DRIVE_FORCED_BRAKE, (uint16_t)(forced - 1));
	}

	drive__wearParts();
	drive__turnEngine(controls.gas);

	int16_t gear = drive__playerWord(DRIVING_GEAR);
	int16_t revs = drive__playerWord(DRIVING_REVS);
	bool	stalled = drive__playerWord(DRIVING_SPEED) <= DRIVE_STALL_SPEED &&
		!controls.gas && gear >= 1;
	bool automatic = drive__playerWord(DRIVING_AUTOMATIC) != 0 &&
		drive__playerWord(DRIVING_SHIFTING) > 1;
	bool shiftUp = controls.gas &&
		drive__playerWord(DRIVING_SHIFT_REVS) < revs &&
		gear < drive__playerWord(DRIVING_GEARS);
	bool shiftDown = !controls.gas && gear > 1 &&
		drive__playerWord(DRIVING_RANGE_INDEX) == 0;

	if (controls.shift || stalled || (automatic && (shiftUp || shiftDown))) {
		drive__shiftGear(&controls, stalled);
	}

	drive__pickRange();
	drive__setSpeed(controls.brake);
	drive__steer(controls.steer);

	if (*exe_data(DRIVE_RACING) != 0 && roadstate_check() != 0) {
		drive_stopCar(DRIVE_CRASHED);
	}
}

/** ****************************************************************************
 *
 * \brief	Read what the player does with the car.
 *
 * \param	pControls		Output: gas, brake, steering and shifting.
 *
 * \details	Part of TimerIsr2 (1000:238c). Unless the keyboard handler is
 * 			at work (DS:0x4740): a held column key steers and makes
 * 			globals_cursorX -1 or 1, the amount drive__steer takes; a
 * 			held row key is the gas (up) or the brake (down). The
 * 			joystick (no demo) past its thresholds: sideways it steers
 * 			and adds 1 to that amount, up is the gas, down the brake,
 * 			both moving globals_cursorY too. The mouse (no demo): moved
 * 			down it brakes, moved sideways it steers and adds 1 to that
 * 			amount, its right button is the gas. The gear changes when a
 * 			click key or button is newly held (DS:0x4732 against
 * 			DS:0x6e9) or DS:0x47cd is set.
 *
 ******************************************************************************/
static void drive__readControls(
	drive__Controls * pControls
) {
	pControls->gas = false;
	pControls->brake = false;
	pControls->steer = false;

	if (globals_keyboardBusy == 0) {
		if (globals_keyDirectionX != 0) {
			pControls->steer = true;
			globals_cursorX = globals_keyDirectionX < 1 ? -1 : 1;
		}

		if (globals_keyDirectionY < 0) {
			pControls->gas = true;
		} else if (globals_keyDirectionY > 0) {
			pControls->brake = true;
		}

		if (globals_demoMode == 0 && globals_joystickPresent != 0) {
			drive__readJoystick(pControls);
		}

		if (globals_demoMode == 0 && globals_mousePresent != 0) {
			input_mousePoll();

			if (globals_mouseStepY > 0) {
				globals_cursorMoved = 1;
				globals_cursorY++;
				pControls->brake = true;
			}

			if (globals_mouseStepX != 0) {
				pControls->steer = true;
				globals_cursorMoved = 1;
				globals_cursorX += globals_mouseStepX < 1 ? -1 : 1;
			}

			if (globals_mouseRight != 0) {
				pControls->gas = true;
			}
		}
	}

	pControls->shift =
		globals_buttonsHeld != 0 && *exe_data(DRIVE_BUTTON_WAS) == 0;
	*exe_data(DRIVE_BUTTON_WAS) = globals_buttonsHeld;

	if (globals_clickKeyFlag != 0) {
		pControls->shift = true;
		globals_clickKeyFlag = 0;
	}
}

/** ****************************************************************************
 *
 * \brief	Add what the joystick does with the car.
 *
 * \param	pControls		The controls; gas, brake and steering are set.
 *
 * \details	Part of TimerIsr2 (1000:238c). Left or right of its
 * 			thresholds the stick steers and moves globals_cursorX by 1;
 * 			up it is the gas, down the brake, moving globals_cursorY by
 * 			1. Any of them marks the cursor as moved.
 *
 ******************************************************************************/
static void drive__readJoystick(
	drive__Controls * pControls
) {
	input_readJoystick();

	if (globals_joystickX < globals_joystickLeft) {
		pControls->steer = true;
		globals_cursorMoved = 1;
		globals_cursorX--;
	} else if (globals_joystickX > globals_joystickRight) {
		pControls->steer = true;
		globals_cursorMoved = 1;
		globals_cursorX++;
	}

	if (globals_joystickY < globals_joystickUp) {
		pControls->gas = true;
		globals_cursorMoved = 1;
		globals_cursorY--;
	} else if (globals_joystickY > globals_joystickDown) {
		pControls->brake = true;
		globals_cursorMoved = 1;
		globals_cursorY++;
	}
}

/** ****************************************************************************
 *
 * \brief	Add what the automatic driver does with the car.
 *
 * \param	pControls		The controls; gas and shifting are changed.
 *
 * \details	Part of TimerIsr2 (1000:238c), for a demo (DS:0x610) or the
 * 			auto switch (DS:0x612). While four steps of gas would stay
 * 			below the top revs, it gives gas before the place DS:0x7e8a
 * 			and after 60 places before the end of the race; otherwise,
 * 			with gears left and DS:0x8228 set, it shifts up with gas
 * 			(counted in DS:0x6ac). Below 2000 revs without gas it shifts
 * 			down from a gear above the first. An automatic in gear is
 * 			never shifted.
 *
 ******************************************************************************/
static void drive__autoDriver(
	drive__Controls * pControls
) {
	int16_t pull = drive__playerWord(DRIVING_RANGE + DRIVING_RANGE_PULL);
	int16_t revs = drive__playerWord(DRIVING_REVS);
	int16_t gear = drive__playerWord(DRIVING_GEAR);
	int16_t place = exe_readS16(DRIVE_PLACE);

	if ((int16_t)(pull * 4 + revs) < DRIVE_TOP_REVS) {
		if (place < exe_readS16(DRIVE_AUTO_FROM) ||
			place > drive__raceEnd() - DRIVE_AUTO_STOP) {
			pControls->gas = true;
		}
	} else if (gear < drive__playerWord(DRIVING_GEARS) && exe_readS16(DRIVE_AUTO_MAY) != 0) {
		pControls->shift = true;
		pControls->gas = true;
		exe_writeU16(
			DRIVE_AUTO_SHIFTS,
			(uint16_t)(exe_readU16(DRIVE_AUTO_SHIFTS) + 1)
		);
	}

	if (revs < DRIVE_LOW_REVS && gear > 1 && !pControls->gas) {
		pControls->shift = true;
	}

	if (drive__playerWord(DRIVING_AUTOMATIC) != 0 && gear > 0) {
		pControls->shift = false;
	}
}

/** ****************************************************************************
 *
 * \brief	Wear the engine and the transmission of the player's car.
 *
 * \details	Part of TimerIsr2 (1000:238c). Above 5000 revs the wear of
 * 			both parts (word +2 of the parts the car DS:0x7eb0 points to
 * 			at +8 and +6) grows by 1 a step, and the engine's by 1 more
 * 			when the BIOS tick count is a multiple of 16. An engine with
 * 			a wear above 9899 breaks (drive__stopCar, flags 5).
 *
 ******************************************************************************/
static void drive__wearParts(void) {
	uint16_t engine = drive__wearAt(DRIVE_CAR_ENGINE);
	uint16_t gearbox = drive__wearAt(DRIVE_CAR_GEARBOX);

	if (drive__playerWord(DRIVING_REVS) > DRIVE_WEAR_REVS) {
		exe_writeU16(engine, (uint16_t)(exe_readU16(engine) + 1));
		exe_writeU16(gearbox, (uint16_t)(exe_readU16(gearbox) + 1));
	}

	if ((globals_biosTicks & DRIVE_WEAR_EVERY) == 0) {
		exe_writeU16(engine, (uint16_t)(exe_readU16(engine) + 1));
	}

	if (exe_readS16(engine) > DRIVE_WORN_OUT) {
		drive_stopCar(DRIVE_BLOWN_ENGINE);
	}
}

/** ****************************************************************************
 *
 * \brief	Change the revs of the engine by the gas pedal.
 *
 * \param	gas				The gas pedal is down.
 *
 * \details	Part of TimerIsr2 (1000:238c). With gas the revs grow by the
 * 			range's pull (record +0x14) up to 5700 (5690 with an
 * 			automatic); each step at 5700 is counted (DS:0x6e6, cleared
 * 			below 5700) and with more of them than 15 less a thousandth
 * 			of the engine's wear the engine breaks (drive_stopCar,
 * 			flags 5). Without gas the revs fall by the range's second
 * 			value (+0x16) while they are above 1500, or above that value
 * 			with the car in gear.
 *
 ******************************************************************************/
static void drive__turnEngine(
	bool gas
) {
	int16_t wear = exe_readS16(drive__wearAt(DRIVE_CAR_ENGINE));
	int16_t revs = drive__playerWord(DRIVING_REVS);
	int16_t fall = drive__playerWord(DRIVING_RANGE + DRIVING_RANGE_PULL2);

	if (gas) {
		revs += drive__playerWord(DRIVING_RANGE + DRIVING_RANGE_PULL);

		if (drive__playerWord(DRIVING_AUTOMATIC) != 0 &&
			revs > DRIVE_AUTO_REVS) {
			revs = DRIVE_AUTO_REVS;
		}

		drive__setPlayerWord(DRIVING_REVS, revs);

		if (revs > DRIVE_TOP_REVS) {
			int16_t steps = (int16_t)(exe_readS16(DRIVE_OVER_REVS) + 1);

			revs = DRIVE_TOP_REVS;
			drive__setPlayerWord(DRIVING_REVS, revs);
			exe_writeU16(DRIVE_OVER_REVS, (uint16_t)steps);

			if (DRIVE_OVER_STEPS - wear / 1000 < steps) {
				drive_stopCar(DRIVE_BLOWN_ENGINE);
			}
		}
	} else if (revs > DRIVE_IDLE_REVS || (drive__playerWord(DRIVING_GEAR) > 0 && fall < revs)) {
		revs -= fall;
		drive__setPlayerWord(DRIVING_REVS, revs);
	}

	if (revs < DRIVE_TOP_REVS) {
		exe_writeU16(DRIVE_OVER_REVS, 0);
	}
}

/** ****************************************************************************
 *
 * \brief	Change the gear of the player's car.
 *
 * \param	pControls		What the player does.
 * \param	stalled			The car in gear is too slow without gas.
 *
 * \details	Part of TimerIsr2 (1000:238c). The transmission wears by 3
 * 			(15 above 5000 revs); outside a demo it breaks
 * 			(drive__stopCar, flags 0x11) with a wear above 9899, or, for
 * 			a manual one shifted with gas above 5000 revs, when a random
 * 			number below 3277 is less than 100 plus a hundredth of the
 * 			wear. The gear goes up with gas, or in a range above the
 * 			first without the brake, while there are gears left; it goes
 * 			down from a gear when the brake is down or in the first range
 * 			without gas. A stalled car goes to neutral. With an automatic
 * 			the player's shift toggles +0x24 between 1 (neutral) and 2
 * 			(drive); a stalled car and gear 0 make it 1, and 1 puts the
 * 			gear to 0. The gear is to be drawn (+0x1a), its revs per mph
 * 			(+0x34) become those in use (+0x40) and the revs follow the
 * 			speed (1505 at speed 0).
 *
 ******************************************************************************/
static void drive__shiftGear(
	const drive__Controls * pControls,
	bool					stalled
) {
	uint16_t wearAt = drive__wearAt(DRIVE_CAR_GEARBOX);
	int16_t	 revs = drive__playerWord(DRIVING_REVS);
	int16_t	 wear = exe_readS16(wearAt);
	bool	 manual = drive__playerWord(DRIVING_AUTOMATIC) == 0;

	wear += revs <= DRIVE_WEAR_REVS ? DRIVE_WEAR_SOFT : DRIVE_WEAR_HARD;
	exe_writeU16(wearAt, (uint16_t)wear);

	if (globals_demoMode == 0) {
		bool broken = wear > DRIVE_WORN_OUT;

		if (!broken && manual && pControls->gas && revs > DRIVE_WEAR_REVS) {
			broken =
				random_next(DRIVE_MISS_RANGE) < wear / 100 + DRIVE_MISS_BASE;
		}

		if (broken) {
			drive_stopCar(DRIVE_BLOWN_GEARS);
		}
	}

	int16_t range = drive__playerWord(DRIVING_RANGE_INDEX);
	int16_t gear = drive__playerWord(DRIVING_GEAR);
	bool	neutral = stalled;

	if ((!pControls->brake && (range != 0 || pControls->gas)) || gear < 1) {
		if ((pControls->gas || (range > 0 && !pControls->brake)) &&
			gear < drive__playerWord(DRIVING_GEARS)) {
			gear++;
		}
	} else {
		gear--;
	}

	if (!manual) {
		int16_t shifting = drive__playerWord(DRIVING_SHIFTING);

		if (pControls->shift) {
			shifting = shifting == 1 ? 2 : 1;
		}

		if (stalled || gear == 0) {
			shifting = 1;
		}

		drive__setPlayerWord(DRIVING_SHIFTING, shifting);
		neutral = shifting == 1;
	}

	if (neutral) {
		gear = 0;
	}

	int16_t perMph = drive__playerWord(DRIVING_GEAR_REVS + gear * 2);

	drive__setPlayerWord(DRIVING_GEAR, gear);
	drive__setPlayerWord(DRIVING_GEAR_CHANGED, 1);
	drive__setPlayerWord(DRIVING_REVS_PER_MPH, perMph);
	revs = (int16_t)(drive__playerWord(DRIVING_SPEED) * perMph);

	if (revs == 0) {
		revs = DRIVE_SHIFTED_REVS;
	}

	drive__setPlayerWord(DRIVING_REVS, revs);
}

/** ****************************************************************************
 *
 * \brief	Pick the range of the gear for the engine's revs.
 *
 * \details	Part of TimerIsr2 (1000:238c). The range is the first of the
 * 			gear's record (+0x44) whose end is not below the revs, or the
 * 			third. When it differs from the one in use (+0x42), or the
 * 			gear changed (+0x1a), its two values are copied to the
 * 			record (+0x14, +0x16).
 *
 ******************************************************************************/
static void drive__pickRange(void) {
	int16_t revs = drive__playerWord(DRIVING_REVS);
	int16_t gear = drive__playerWord(DRIVING_GEAR);
	int		ranges = DRIVING_GEAR_RECORDS + gear * DRIVING_GEAR_SIZE;
	int16_t index = 0;

	while (index < DRIVING_RANGE_COUNT - 1 &&
		   drive__playerWord(
			   ranges + index * DRIVING_RANGE_SIZE + DRIVING_RANGE_TO
		   ) < revs) {
		index++;
	}

	if (index == drive__playerWord(DRIVING_RANGE_INDEX) &&
		drive__playerWord(DRIVING_GEAR_CHANGED) == 0) {
		return;
	}

	int range = ranges + index * DRIVING_RANGE_SIZE;

	drive__setPlayerWord(
		DRIVING_RANGE + DRIVING_RANGE_PULL,
		drive__playerWord(range + DRIVING_RANGE_PULL)
	);
	drive__setPlayerWord(
		DRIVING_RANGE + DRIVING_RANGE_PULL2,
		drive__playerWord(range + DRIVING_RANGE_PULL2)
	);
	drive__setPlayerWord(DRIVING_RANGE_INDEX, index);
}

/** ****************************************************************************
 *
 * \brief	Set the speed of the player's car, or the revs from it.
 *
 * \param	brake			The brake pedal is down.
 *
 * \details	Part of TimerIsr2 (1000:238c). Braking (or above 8 mph with
 * 			DS:0x5e0e set) the speed falls by 2 and a 16th of itself and
 * 			the revs follow it (1500 at least). In neutral a moving car
 * 			loses 1 mph and the revs are 1500 plus 30 per mph.
 * 			Otherwise the speed is the revs by the revs per mph in use
 * 			(+0x40).
 *
 ******************************************************************************/
static void drive__setSpeed(
	bool brake
) {
	int16_t speed = drive__playerWord(DRIVING_SPEED);
	int16_t perMph = drive__playerWord(DRIVING_REVS_PER_MPH);
	int16_t revs;

	if (brake ||
		(exe_readS16(DRIVE_SLOWED) != 0 && speed > DRIVE_SLOW_SPEED)) {
		speed -= (speed >> 4) + 2;

		if (speed < 0) {
			speed = 0;
		}

		revs = (int16_t)(speed * perMph);

		if (revs < DRIVE_IDLE_REVS) {
			revs = DRIVE_IDLE_REVS;
		}

		drive__setPlayerWord(DRIVING_SPEED, speed);
		drive__setPlayerWord(DRIVING_REVS, revs);
	} else if (drive__playerWord(DRIVING_GEAR) == 0 && speed != 0) {
		speed--;
		revs = (int16_t)(speed * DRIVE_NEUTRAL_REVS + DRIVE_IDLE_REVS);
		drive__setPlayerWord(DRIVING_SPEED, speed);
		drive__setPlayerWord(DRIVING_REVS, revs);
	} else if (perMph != 0) {
		revs = drive__playerWord(DRIVING_REVS);
		drive__setPlayerWord(DRIVING_SPEED, (int16_t)(revs / perMph));
	}
}

/** ****************************************************************************
 *
 * \brief	Turn the steering of the player's car.
 *
 * \param	steer			The wheel is turned, by globals_cursorX.
 *
 * \details	Part of TimerIsr2 (1000:238c). The steering (record +2) is 0
 * 			unless the wheel is turned further to its side. Then it
 * 			starts from the wheel's position (+4) if that is further out
 * 			on the same side, grows by twice the amount, up to 24 to a
 * 			side, and becomes the wheel's position; the amount is
 * 			cleared. The wheel's position then goes back toward 0 by 1
 * 			and a quarter of itself.
 *
 ******************************************************************************/
static void drive__steer(
	bool steer
) {
	int16_t steering = drive__playerWord(DRIVING_STEERING);
	int16_t wheel = drive__playerWord(DRIVING_WHEEL);
	int16_t amount = globals_cursorX;
	int16_t reach = drive__magnitude(steering);

	if (!steer || drive__magnitude((int16_t)(steering + amount)) <= reach) {
		steering = 0;
	} else {
		int16_t wheelReach = drive__magnitude(wheel);
		int16_t turned = drive__magnitude((int16_t)(wheel + amount));

		if ((int16_t)(drive__magnitude(amount) + wheelReach) == turned &&
			reach < wheelReach) {
			steering = wheel;
		}

		steering += amount * 2;

		if (drive__magnitude(steering) > DRIVE_STEER_MOST) {
			steering = steering < 1 ? -DRIVE_STEER_MOST : DRIVE_STEER_MOST;
		}

		globals_cursorX = 0;
		wheel = steering;
	}

	if (drive__magnitude(wheel) > 0) {
		if (wheel < 1) {
			wheel += ((int16_t)-wheel >> 2) + 1;
		} else {
			wheel -= (wheel >> 2) + 1;
		}
	}

	drive__setPlayerWord(DRIVING_STEERING, steering);
	drive__setPlayerWord(DRIVING_WHEEL, wheel);
}

/** ****************************************************************************
 *
 * \brief	Stop the player's car: something ended its race.
 *
 * \param	flags			Bits added to the record's flags (+0xbc).
 *
 * \details	The flags and FUN_1000_2374 as TimerIsr2 calls it: DS:0x5d6,
 * 			the steering (+2, +4) and the range's values (+0x14, +0x16)
 * 			are cleared and the time so far (+0xc0) is kept in +0xbe.
 *
 ******************************************************************************/
void drive_stopCar(
	uint8_t flags
) {
	*exe_data(DRIVE_FLAGS) |= flags;
	exe_writeU16(DRIVE_UNKNOWN_5D6, 0);
	drive__setPlayerWord(DRIVING_WHEEL, 0);
	drive__setPlayerWord(DRIVING_STEERING, 0);
	drive__setPlayerWord(DRIVING_RANGE + DRIVING_RANGE_PULL2, 0);
	drive__setPlayerWord(DRIVING_RANGE + DRIVING_RANGE_PULL, 0);
	drive__setPlayerWord(DRIVING_END_TIME, drive__playerWord(DRIVING_TIME));
}

/** ****************************************************************************
 *
 * \brief	Find the wear of a part of the player's car.
 *
 * \param	part			Offset in the car of the part's DS offset
 * 							(DRIVE_CAR_ENGINE, DRIVE_CAR_GEARBOX).
 *
 * \return	DS offset of the word with the wear.
 *
 ******************************************************************************/
static uint16_t drive__wearAt(
	int part
) {
	uint16_t car = exe_readU16(DRIVE_CAR);

	return (uint16_t)(exe_readU16((uint16_t)(car + part)) + DRIVE_PART_WEAR);
}

/** ****************************************************************************
 *
 * \brief	Give a value without its sign, in 16 bits as the original.
 *
 * \param	value			The value.
 *
 * \return	Its magnitude (-32768 stays).
 *
 ******************************************************************************/
static int16_t drive__magnitude(
	int16_t value
) {
	return (int16_t)(value < 0 ? -value : value);
}

/** ****************************************************************************
 *
 * \brief	Give the place where the race ends.
 *
 * \return	0x17c for a drag race (DS:0x7ec0 is 0), 0x3fc for the others.
 *
 ******************************************************************************/
static int16_t drive__raceEnd(void) {
	return exe_readS16(DRIVE_RACE_KIND) == 0 ? DRIVE_DRAG_END : DRIVE_ROAD_END;
}
