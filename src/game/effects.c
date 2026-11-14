/** ****************************************************************************
 *
 * \file	effects.c
 *
 * \brief	The sound effects on the PC speaker.
 *
 * \details	Two kinds (docs/sound.md). The blocking effects of segment
 * 			1f38 set a tone or toggle the speaker by hand and wait in a
 * 			delay loop between the changes; the game stands still while
 * 			they sound. The looping effects are short tracks that the
 * 			track player (game/music.c) plays in place of the music until
 * 			they are switched off. The original calibrates its delay loop
 * 			at the start (CalibrateDelay, 1f38:703e; InitSound,
 * 			1f38:7101); the port uses a fixed length (EFFECTS_DELAY_STEPS,
 * 			EFFECTS_CALL_STEPS), still to be compared with DOSBox.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/effects.h"

#include "dos/dos.h"
#include "game/globals.h"
#include "game/music.h"
#include "game/random.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

/**
 * The clock of the timer chip as the game has it (0x1234dc), divided by a
 * frequency to get a divisor.
 */
#define EFFECTS_PIT_HZ	1193180L

#define EFFECTS_IDLE_HZ 25000 /**< Tone of SpeakerOn, too high to hear. */

/**
 * Length of one unit of DelayLoop (3196:0002) in steps of the timer chip's
 * clock: about 6 us. The original measures the machine so that
 * DelayLoop(10) runs a fixed share of a BIOS tick; with the cost of the
 * measuring loop's call a unit comes out at 4..7 us (docs/sound.md).
 */
#define EFFECTS_DELAY_STEPS 7

/**
 * Time the original needs around one DelayLoop (the calls, the long
 * division of the frequency, Random), in steps of the timer chip's clock:
 * about 60 us, a guess for a 286. It matters for the delays of one unit.
 */
#define EFFECTS_CALL_STEPS 72

/** Port 61h bits that let the speaker sound. */
#define EFFECTS_SPEAKER_BITS (DOS_SPEAKER_GATE | DOS_SPEAKER_DATA)

/* Looping effects: offsets of the tracks in MUSIC_EFFECT_SEGMENT. */

#define EFFECTS_TRACK_PUMP	  0x00 /**< effects_pump. */
#define EFFECTS_TRACK_TIRE	  0x2c /**< effects_tireChange. */
#define EFFECTS_TRACK_76DA	  0x7c /**< effects_track76da. */
#define EFFECTS_TRACK_STICKER 0xa0 /**< effects_sticker. */
#define EFFECTS_TRACK_WHEEL	  0xc0 /**< effects_wheelSwap. */

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

static void effects__setFrequency(
	int16_t hertz
);

static void effects__speakerOn(void);

static void effects__speakerOff(void);

static void effects__delay(
	uint16_t units
);

static void effects__tone(
	int16_t	 hertz,
	uint16_t delay
);

static void effects__randomTones(
	int16_t	 lowest,
	int16_t	 highest,
	uint16_t delay,
	int16_t	 count
);

static void effects__fallingTones(
	int16_t	 from,
	int16_t	 to,
	int16_t	 step,
	uint16_t delay
);

static void effects__warble(
	int16_t	 middle,
	int16_t	 range,
	int16_t	 step,
	uint16_t delay,
	int16_t	 count
);

static void effects__noise(
	int16_t	 range,
	uint16_t bits,
	int16_t	 count
);

static void effects__noiseFalling(
	int16_t from,
	int16_t to,
	int16_t less,
	int16_t step,
	int16_t count
);

static void effects__noiseRising(
	int16_t from,
	int16_t to,
	int16_t less,
	int16_t step,
	int16_t count
);

static void effects__music(void);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Set the frequency of the speaker's tone.
 *
 * \param	hertz			The frequency.
 *
 * \details	Port of SetSpeakerFrequency (1f38:70d5): the divisor of timer
 * 			channel 2 is 1193180 / frequency, cut to 16 bits. The
 * 			original would stop with a division error at 0 Hz, which no
 * 			effect asks for; the port leaves the tone alone.
 *
 ******************************************************************************/
static void effects__setFrequency(
	int16_t hertz
) {
	if (hertz == 0) {
		return;
	}

	dos_setSpeakerDivisor((uint16_t)(EFFECTS_PIT_HZ / hertz));
}

/** ****************************************************************************
 *
 * \brief	Switch the speaker on with a tone too high to hear.
 *
 * \details	Port of SpeakerOn (1f38:7122).
 *
 ******************************************************************************/
static void effects__speakerOn(void) {
	effects__setFrequency(EFFECTS_IDLE_HZ);
	dos_setSpeakerPort((uint8_t)(dos_speakerPort() | EFFECTS_SPEAKER_BITS));
	globals_speakerOn = 1;
}

/** ****************************************************************************
 *
 * \brief	Switch the speaker off.
 *
 * \details	Port of SpeakerOff (1f38:713d).
 *
 ******************************************************************************/
static void effects__speakerOff(void) {
	dos_setSpeakerPort((uint8_t)(dos_speakerPort() & ~EFFECTS_SPEAKER_BITS));
	globals_speakerOn = 0;
}

/** ****************************************************************************
 *
 * \brief	Wait between two changes of the speaker.
 *
 * \param	units			Length in the units of the original's delay loop.
 *
 * \details	Stands for DelayLoop (3196:0002), a busy loop of units times
 * 			the factor measured at the start (DS:0x58e2) / 256 rounds.
 * 			The port waits a fixed time per unit plus the time the
 * 			original spends around the loop. The timer handler keeps
 * 			running meanwhile (dos_speakerDelay).
 *
 ******************************************************************************/
static void effects__delay(
	uint16_t units
) {
	dos_speakerDelay(
		(uint32_t)units * EFFECTS_DELAY_STEPS + EFFECTS_CALL_STEPS
	);
}

/** ****************************************************************************
 *
 * \brief	Sound a tone for a while.
 *
 * \param	hertz			The frequency.
 * \param	delay			Its length in delay units.
 *
 * \details	Port of PlayTone (1f38:714c).
 *
 ******************************************************************************/
static void effects__tone(
	int16_t	 hertz,
	uint16_t delay
) {
	effects__setFrequency(hertz);
	effects__delay(delay);
}

/** ****************************************************************************
 *
 * \brief	Sound a row of tones of random frequencies.
 *
 * \param	lowest			The lowest frequency.
 * \param	highest			The limit above the highest frequency.
 * \param	delay			Length of a tone in delay units.
 * \param	count			Number of tones.
 *
 * \details	Port of PlayRandomTones (1f38:7169).
 *
 ******************************************************************************/
static void effects__randomTones(
	int16_t	 lowest,
	int16_t	 highest,
	uint16_t delay,
	int16_t	 count
) {
	for (; count > 0; count--) {
		int16_t hertz = random_next((int16_t)(highest - lowest));

		effects__tone((int16_t)(hertz + lowest), delay);
	}
}

/** ****************************************************************************
 *
 * \brief	Sound a row of falling tones.
 *
 * \param	from			The first frequency.
 * \param	to				The lowest frequency that still sounds.
 * \param	step			How much each tone is lower than the one before.
 * \param	delay			Length of a tone in delay units.
 *
 * \details	Port of PlayFallingTones (1f38:71b2).
 *
 ******************************************************************************/
static void effects__fallingTones(
	int16_t	 from,
	int16_t	 to,
	int16_t	 step,
	uint16_t delay
) {
	for (; to <= from; from = (int16_t)(from - step)) {
		effects__tone(from, delay);
	}
}

/** ****************************************************************************
 *
 * \brief	Sound tones that rise and fall around a frequency.
 *
 * \param	middle			The frequency in the middle.
 * \param	range			How far the tones go below and above it.
 * \param	step			Difference between two tones.
 * \param	delay			Length of a tone in delay units.
 * \param	count			Number of rounds.
 *
 * \details	Port of PlayWarble (1f38:71e4). A round rises from middle -
 * 			range and then falls from middle + range, each over twice
 * 			the range.
 *
 ******************************************************************************/
static void effects__warble(
	int16_t	 middle,
	int16_t	 range,
	int16_t	 step,
	uint16_t delay,
	int16_t	 count
) {
	int16_t width = (int16_t)(range << 1);

	for (; count > 0; count--) {
		int16_t hertz = (int16_t)(middle - range);

		for (int16_t done = 0; done < width; done = (int16_t)(done + step)) {
			effects__tone(hertz, delay);
			hertz = (int16_t)(hertz + step);
		}

		hertz = (int16_t)(middle + range);

		for (int16_t done = 0; done < width; done = (int16_t)(done + step)) {
			effects__tone(hertz, delay);
			hertz = (int16_t)(hertz - step);
		}
	}
}

/** ****************************************************************************
 *
 * \brief	Make noise by moving the speaker by hand at random times.
 *
 * \param	range			Limit of the random part of the time between two
 * 							moves, in delay units.
 * \param	bits			Bits that are set in every such time.
 * \param	count			Number of moves.
 *
 * \details	Port of PlayNoise (1f38:7464): the speaker's tone is switched
 * 			off, then bit 1 of port 61h is toggled count times.
 *
 ******************************************************************************/
static void effects__noise(
	int16_t	 range,
	uint16_t bits,
	int16_t	 count
) {
	effects__speakerOff();

	for (; count > 0; count--) {
		dos_setSpeakerPort((uint8_t)(dos_speakerPort() ^ DOS_SPEAKER_DATA));
		effects__delay((uint16_t)((uint16_t)random_next(range) | bits));
	}
}

/** ****************************************************************************
 *
 * \brief	Make noise that gets higher: the times between the moves of
 * 			the speaker fall.
 *
 * \param	from			The first limit of the random times.
 * \param	to				The limit below which the noise ends.
 * \param	less			Each round's bits are its limit less this.
 * \param	step			How much the limit falls per round.
 * \param	count			Number of moves per round.
 *
 * \details	Port of PlayNoiseFalling (1f38:74b0).
 *
 ******************************************************************************/
static void effects__noiseFalling(
	int16_t from,
	int16_t to,
	int16_t less,
	int16_t step,
	int16_t count
) {
	for (; to < from; from = (int16_t)(from - step)) {
		effects__noise(from, (uint16_t)(from - less), count);
	}
}

/** ****************************************************************************
 *
 * \brief	Make noise that gets lower: the times between the moves of
 * 			the speaker rise.
 *
 * \param	from			The first limit of the random times.
 * \param	to				The limit at which the noise ends.
 * \param	less			Each round's bits are its limit less this.
 * \param	step			How much the limit rises per round.
 * \param	count			Number of moves per round.
 *
 * \details	Port of PlayNoiseRising (1f38:74fc).
 *
 ******************************************************************************/
static void effects__noiseRising(
	int16_t from,
	int16_t to,
	int16_t less,
	int16_t step,
	int16_t count
) {
	for (; from < to; from = (int16_t)(from + step)) {
		effects__noise(from, (uint16_t)(from - less), count);
	}
}

/** ****************************************************************************
 *
 * \brief	End a looping effect: the music's player takes over again.
 *
 * \details	The `PlayEffectTrack(0, 0x3e6b, 0x5858)` of the original's
 * 			looping effects when they are switched off.
 *
 ******************************************************************************/
static void effects__music(void) {
	music_playEffectTrack(0, MUSIC_TRACK_SEGMENT, MUSIC_REPEATS);
}

/** ****************************************************************************
 *
 * \brief	Sound the error beep.
 *
 * \details	Port of ErrorBeep (1f38:77ba), for wrong keys in a text
 * 			input.
 *
 ******************************************************************************/
void effects_errorBeep(void) {
	if (globals_soundOn == 0) {
		return;
	}

	effects__speakerOn();
	effects__warble(3000, 10, 10, 60, 9);
	effects__speakerOff();
}

/** ****************************************************************************
 *
 * \brief	Sound a short high warble of a random pitch.
 *
 * \details	Port of FUN_1f38_77e1 (transmission and engine work; what it
 * 			stands for is not certain).
 *
 ******************************************************************************/
void effects_warble77e1(void) {
	if (globals_soundOn == 0) {
		return;
	}

	effects__speakerOn();
	effects__warble((int16_t)(random_next(100) + 6200), 40, 10, 1, 10);
	effects__speakerOff();
}

/** ****************************************************************************
 *
 * \brief	Sound a short warble around 5200 Hz.
 *
 * \details	Port of FUN_1f38_7817 (transmission and engine work, after
 * 			the looping effect of effects_track76da; what it stands for
 * 			is not certain).
 *
 ******************************************************************************/
void effects_warble7817(void) {
	if (globals_soundOn == 0) {
		return;
	}

	effects__speakerOn();
	effects__warble(5200, 40, 10, 1, 10);
	effects__speakerOff();
}

/** ****************************************************************************
 *
 * \brief	Sound a short burst of noise.
 *
 * \details	Port of FUN_1f38_7841 (body work, parts lost in a race; what
 * 			it stands for is not certain).
 *
 ******************************************************************************/
void effects_noise7841(void) {
	if (globals_soundOn == 0) {
		return;
	}

	effects__speakerOn();
	effects__noise(200, 10, 60);
	effects__speakerOff();
}

/** ****************************************************************************
 *
 * \brief	Sound the crash of a race.
 *
 * \details	Port of CrashSound (1f38:7863): three times random tones,
 * 			noise getting higher, noise and noise getting lower. The
 * 			random numbers are drawn in the original's order (the
 * 			decompiler mixes the argument lists, see docs/sound.md).
 *
 ******************************************************************************/
void effects_crash(void) {
	if (globals_soundOn == 0) {
		return;
	}

	for (int16_t round = 3; round != 0; round--) {
		int16_t limit;

		effects__speakerOn();
		effects__randomTones(3000, 8000, 60, 10);
		limit = (int16_t)(random_next(30) + 18);
		effects__noiseFalling(200, limit, 15, 5, 2);
		limit = (int16_t)(random_next(300) + 40);
		effects__noise(limit, 2, 30);
		limit = (int16_t)(random_next(100) + 50);
		effects__noiseRising(16, limit, 15, 5, 4);
	}

	effects__speakerOff();
}

/** ****************************************************************************
 *
 * \brief	Sound the bump of two cars.
 *
 * \details	Port of BumpSound (1f38:790f).
 *
 ******************************************************************************/
void effects_bump(void) {
	if (globals_soundOn == 0) {
		return;
	}

	effects__speakerOn();
	effects__randomTones(2000, 8000, 100, 10);
	effects__noise(120, 2, 30);
	effects__speakerOn();
	effects__randomTones(4000, 9000, 100, 10);
	effects__noise(220, 2, 20);
	effects__speakerOff();
}

/** ****************************************************************************
 *
 * \brief	Sound a bolt: low falling tones.
 *
 * \details	Port of BoltSound (1f38:7973).
 *
 ******************************************************************************/
void effects_bolt(void) {
	if (globals_soundOn == 0) {
		return;
	}

	effects__speakerOn();
	effects__fallingTones(130, 90, 4, 1000);
	effects__speakerOff();
}

/** ****************************************************************************
 *
 * \brief	Start or end the sound of rolling a wheel.
 *
 * \param	enable			Not 0: start the looping effect. 0: back to the
 * 							music.
 *
 * \details	Port of WheelSwapSound (1f38:7548). It sets no vibrato: the
 * 			last one stays.
 *
 ******************************************************************************/
void effects_wheelSwap(
	int16_t enable
) {
	if (enable == 0) {
		effects__music();

		return;
	}

	music_playEffectTrack(
		EFFECTS_TRACK_WHEEL,
		MUSIC_EFFECT_SEGMENT,
		MUSIC_EFFECT_REPEATS
	);
}

/** ****************************************************************************
 *
 * \brief	Start or end the sound of putting on a sticker.
 *
 * \param	enable			Not 0: start the looping effect. 0: back to the
 * 							music.
 *
 * \details	Port of StickerSound (1f38:7573).
 *
 ******************************************************************************/
void effects_sticker(
	int16_t enable
) {
	if (enable == 0) {
		effects__music();

		return;
	}

	music_playEffectTrack(
		EFFECTS_TRACK_STICKER,
		MUSIC_EFFECT_SEGMENT,
		MUSIC_EFFECT_REPEATS
	);
	music_setEffectVibrato(1, 0, 8, 10, 1);
}

/** ****************************************************************************
 *
 * \brief	Start or end the sound of the gas pump.
 *
 * \param	enable			Not 0: start the looping effect. 0: back to the
 * 							music.
 *
 * \details	Port of PumpSound (1f38:7613).
 *
 ******************************************************************************/
void effects_pump(
	int16_t enable
) {
	if (enable == 0) {
		effects__music();

		return;
	}

	music_playEffectTrack(
		EFFECTS_TRACK_PUMP,
		MUSIC_EFFECT_SEGMENT,
		MUSIC_EFFECT_REPEATS
	);
	music_setEffectVibrato(1, 1, 8, 30, 1);
}

/** ****************************************************************************
 *
 * \brief	Start or end the sound of changing the tires.
 *
 * \param	enable			Not 0: start the looping effect. 0: back to the
 * 							music.
 *
 * \details	Port of TireChangeSound (1f38:7661).
 *
 ******************************************************************************/
void effects_tireChange(
	int16_t enable
) {
	if (enable == 0) {
		effects__music();

		return;
	}

	music_playEffectTrack(
		EFFECTS_TRACK_TIRE,
		MUSIC_EFFECT_SEGMENT,
		MUSIC_EFFECT_REPEATS
	);
	music_setEffectVibrato(1, 1, 8, 20, 1);
}

/** ****************************************************************************
 *
 * \brief	Start or end a looping effect of the transmission and engine
 * 			work.
 *
 * \param	enable			Not 0: start the looping effect. 0: back to the
 * 							music.
 *
 * \details	Port of FUN_1f38_76da (played while a part moves in or out;
 * 			what it stands for is not certain).
 *
 ******************************************************************************/
void effects_track76da(
	int16_t enable
) {
	if (enable == 0) {
		effects__music();

		return;
	}

	music_playEffectTrack(
		EFFECTS_TRACK_76DA,
		MUSIC_EFFECT_SEGMENT,
		MUSIC_EFFECT_REPEATS
	);
	music_setEffectVibrato(1, 1, 8, 14, 1);
}
