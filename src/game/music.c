/** ****************************************************************************
 *
 * \file	music.c
 *
 * \brief	The track player: the music on the PC speaker.
 *
 * \details	The original's timer handler plays a track note by note: it
 * 			writes the divisor of each note to timer channel 2 and bends
 * 			it with a vibrato. The tracks, the repeat and instrument
 * 			lists and the vibrato tables are read from the image of
 * 			Sr.exe (docs/sound.md). A second player record (DS:0x79c6)
 * 			plays the looping effects of game/effects.c in place of the
 * 			music (music_playEffectTrack).
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/music.h"

#include <stdbool.h>
#include <stddef.h>

#include "dos/dos.h"
#include "game/event.h"
#include "game/exe.h"
#include "game/garage.h"
#include "game/globals.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define MUSIC_INSTRUMENTS	  0x5882 /**< DS: instrument list of the music. */
#define MUSIC_EFFECT_LIST	  0x58cc /**< DS: instrument list of effects. */
#define MUSIC_RECORDS		  0x5826 /**< DS: instrument records. */
#define MUSIC_RECORD_SIZE	  10	 /**< Bytes of an instrument record. */
#define MUSIC_VIBRATO		  0x57f6 /**< DS: vibrato table, rows of 8 words. */
#define MUSIC_VIBRATO_ROW	  16	 /**< Bytes of a row of that table. */

#define MUSIC_RECORD_POSITION 0 /**< Instrument: s16 step position. */
#define MUSIC_RECORD_ROW	  2 /**< Instrument: s16 row of the table. */
#define MUSIC_RECORD_DEPTH	  4 /**< Instrument: s16 depth in 1/1000. */
#define MUSIC_RECORD_STEPS	  6 /**< Instrument: s16 number of steps. */
#define MUSIC_RECORD_SPEED	  8 /**< Instrument: s16 ticks per step. */

#define MUSIC_LOOP_START	  4	   /**< Offset where a track restarts. */
#define MUSIC_DEPTH_UNIT	  1000 /**< Depth unit: 1/1000 of the divisor. */

#define MUSIC_MODE_MUSIC	  1	   /**< globals_musicAvailable: the music. */
#define MUSIC_MODE_EFFECT	  2	   /**< globals_musicAvailable: an effect. */
#define MUSIC_NOT_SAVED		  (-1) /**< globals_musicSaved: nothing. */

/** Port 61h bits that let the speaker sound. */
#define MUSIC_SPEAKER_BITS (DOS_SPEAKER_GATE | DOS_SPEAKER_DATA)

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/**
 * A player record (28 bytes in the original). The original's far pointers
 * into the track are offsets in one segment here, its near pointers DS
 * offsets.
 */
typedef struct music__player_tag {
	uint16_t divisor;		 /**< +0: divisor of the note, 0 = rest. */
	uint16_t ticks;			 /**< +2: ticks left of the note. */
	int16_t	 repeats;		 /**< +4: repeats done of the pattern. */
	uint8_t	 active;		 /**< +6: the player runs. */
	uint8_t	 gate;			 /**< +7: speaker on with the next note. */
	uint16_t segment;		 /**< +10: segment of the track. */
	uint16_t track;			 /**< +8: start of the track. */
	uint16_t pattern;		 /**< +0xc: start of the current pattern. */
	uint16_t position;		 /**< +0x10: read position. */
	uint16_t repeatList;	 /**< +0x14: DS: repeat list. */
	uint16_t repeatAt;		 /**< +0x16: DS: position in it. */
	uint16_t instrumentList; /**< +0x18: DS: instrument list. */
	uint16_t instrumentAt;	 /**< +0x1a: DS: position in it. */
} music__Player;

/*******************************************************************************
;
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

static void music__speakerOff(void);

static uint16_t music__trackWord(
	const music__Player * pPlayer
);

static void music__setInstrument(
	const music__Player * pPlayer
);

static void music__vibrato(
	const music__Player * pPlayer
);

static bool music__nextPattern(
	music__Player * pPlayer
);

static void music__nextNote(
	music__Player * pPlayer
);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/** DS:0x79aa: the player of the music. */
static music__Player music__music;

/** DS:0x79c6: the player of the looping effects. */
static music__Player music__effect;

/** DS:0x58d2: the current player. */
static music__Player * music__pPlayer = &music__music;

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Switch the speaker off.
 *
 * \details	The original's `out(0x61, in(0x61) & 0xfc)` with DS:0x47cf
 * 			cleared, which the player's functions repeat inline.
 *
 ******************************************************************************/
static void music__speakerOff(void) {
	dos_setSpeakerPort((uint8_t)(dos_speakerPort() & ~MUSIC_SPEAKER_BITS));
	globals_speakerOn = 0;
}

/** ****************************************************************************
 *
 * \brief	Read the word of the track at the read position.
 *
 * \param	pPlayer			The player.
 *
 * \return	The word; 0 when the position is outside the game's image
 * 			(which ends the track).
 *
 ******************************************************************************/
static uint16_t music__trackWord(
	const music__Player * pPlayer
) {
	const uint8_t * pWord = exe_far(pPlayer->segment, pPlayer->position, 2);

	if (pWord == NULL) {
		return 0;
	}

	return (uint16_t)(pWord[0] | (pWord[1] << 8));
}

/** ****************************************************************************
 *
 * \brief	Make the instrument at the player's list position the current
 * 			one.
 *
 * \param	pPlayer			The player.
 *
 * \details	The original's `[0x82c4] = *list * 10 + 0x5826`, inline in
 * 			StartTrack and TimerIsr.
 *
 ******************************************************************************/
static void music__setInstrument(
	const music__Player * pPlayer
) {
	int16_t instrument = exe_readS16(pPlayer->instrumentAt);

	globals_instrument =
		(uint16_t)(instrument * MUSIC_RECORD_SIZE + MUSIC_RECORDS);
}

/** ****************************************************************************
 *
 * \brief	Bend the note that plays by the next step of the vibrato.
 *
 * \param	pPlayer			The player.
 *
 * \details	Part of TimerIsr (1000:1fe2), run in every tick of a note but
 * 			its first: with the vibrato enabled and no rest, every
 * 			"speed" ticks the next value of the instrument's row of the
 * 			vibrato table, times the depth of the note, is added to the
 * 			divisor. An instrument without steps would divide by zero in
 * 			the original; the port leaves the note alone.
 *
 ******************************************************************************/
static void music__vibrato(
	const music__Player * pPlayer
) {
	if (globals_vibratoOn == 0 || pPlayer->divisor == 0) {
		return;
	}

	globals_vibratoTicks--;

	if (globals_vibratoTicks != 0) {
		return;
	}

	uint16_t record = globals_instrument;
	int16_t	 steps = exe_readS16((uint16_t)(record + MUSIC_RECORD_STEPS));

	if (steps == 0) {
		return;
	}

	int16_t position = exe_readS16((uint16_t)(record + MUSIC_RECORD_POSITION));
	int16_t step = (int16_t)(position % steps);
	int16_t row = exe_readS16((uint16_t)(record + MUSIC_RECORD_ROW));

	exe_writeU16(
		(uint16_t)(record + MUSIC_RECORD_POSITION),
		(uint16_t)(step + 1)
	);

	int16_t bend = exe_readS16(
		(uint16_t)(row * MUSIC_VIBRATO_ROW + step * 2 + MUSIC_VIBRATO)
	);

	globals_vibratoDivisor =
		(int16_t)(uint16_t)(bend * globals_vibratoDepth + pPlayer->divisor);
	dos_setSpeakerDivisor((uint16_t)globals_vibratoDivisor);
	globals_vibratoTicks =
		exe_readS16((uint16_t)(record + MUSIC_RECORD_SPEED));
}

/** ****************************************************************************
 *
 * \brief	Go on after the end of a pattern.
 *
 * \param	pPlayer			The player; its read position is after the
 * 							duration 0 that ends the pattern.
 *
 * \retval	true			The player reads on: the same pattern again, the
 * 							next one, or the first one of the track.
 * \retval	false			The track ended: the player stopped.
 *
 * \details	Part of TimerIsr (1000:1fe2). The word after the duration 0
 * 			is 0 at the end of the track: speaker off, player stopped and
 * 			rewound. Otherwise the instrument list moves on and the
 * 			pattern is repeated as often as the repeat list says; then
 * 			the next pattern starts, or, when the repeat list ends
 * 			(negative entry), the track starts over with both lists
 * 			rewound.
 *
 ******************************************************************************/
static bool music__nextPattern(
	music__Player * pPlayer
) {
	uint16_t mark = music__trackWord(pPlayer);

	pPlayer->position += 2;

	if (mark == 0) {
		music__speakerOff();
		pPlayer->active = 0;
		pPlayer->gate = 1;
		pPlayer->position = pPlayer->track;
		pPlayer->pattern = pPlayer->track;

		return false;
	}

	pPlayer->instrumentAt += 2;
	pPlayer->repeats++;

	if (exe_readS16(pPlayer->repeatAt) < pPlayer->repeats) {
		pPlayer->repeatAt += 2;

		if (exe_readS16(pPlayer->repeatAt) < 0) {
			pPlayer->position = (uint16_t)(pPlayer->track + MUSIC_LOOP_START);
			pPlayer->repeatAt = pPlayer->repeatList;
			pPlayer->instrumentAt = pPlayer->instrumentList;
		}

		pPlayer->pattern = pPlayer->position;
		pPlayer->repeats = 0;
	} else {
		pPlayer->position = pPlayer->pattern;
	}

	music__setInstrument(pPlayer);

	return true;
}

/** ****************************************************************************
 *
 * \brief	Start the next note of the track.
 *
 * \param	pPlayer			The player, whose note has ended.
 *
 * \details	Part of TimerIsr (1000:1fe2). Reads the duration (0: end of
 * 			the pattern, music__nextPattern) and the divisor. A rest
 * 			switches the speaker off. A note sets the vibrato's depth
 * 			(the instrument's depth times the divisor, cut to 16 bits as
 * 			in the original, by 1000), writes the divisor and, with the
 * 			sound on, switches the speaker on if it is off or a rest came
 * 			before.
 *
 ******************************************************************************/
static void music__nextNote(
	music__Player * pPlayer
) {
	for (;;) {
		pPlayer->ticks = music__trackWord(pPlayer);
		pPlayer->position += 2;

		if (pPlayer->ticks != 0) {
			break;
		}

		if (!music__nextPattern(pPlayer)) {
			return;
		}
	}

	pPlayer->divisor = music__trackWord(pPlayer);
	pPlayer->position += 2;

	if (pPlayer->divisor == 0) {
		pPlayer->gate = 1;
		music__speakerOff();

		return;
	}

	if (globals_vibratoOn != 0) {
		uint16_t record = globals_instrument;
		uint16_t depth = exe_readU16((uint16_t)(record + MUSIC_RECORD_DEPTH));
		uint16_t product = (uint16_t)(depth * pPlayer->divisor);

		globals_vibratoTicks =
			exe_readS16((uint16_t)(record + MUSIC_RECORD_SPEED));
		globals_vibratoDepth = (int16_t)(product / MUSIC_DEPTH_UNIT);
	}

	dos_setSpeakerDivisor(pPlayer->divisor);

	if (globals_soundOn != 0 &&
		(globals_soundOn != globals_speakerOn || pPlayer->gate != 0)) {
		dos_setSpeakerPort((uint8_t)(dos_speakerPort() | MUSIC_SPEAKER_BITS));
		globals_speakerOn = 1;
		pPlayer->gate = 0;
	}
}

/** ****************************************************************************
 *
 * \brief	Set up the player of the music.
 *
 * \details	Part of InitTimerAndInput (1000:1111), which fills the record
 * 			at DS:0x79aa: stopped, track 3e6b:0000, repeat list
 * 			DS:0x5858, instrument list DS:0x5882. It also sets the
 * 			instrument list of the effect player at DS:0x79c6
 * 			(DS:0x58cc).
 *
 ******************************************************************************/
void music_init(void) {
	music__music.active = 0;
	music__music.segment = MUSIC_TRACK_SEGMENT;
	music__music.track = 0;
	music__music.pattern = 0;
	music__music.position = 0;
	music__music.repeatList = MUSIC_REPEATS;
	music__music.repeatAt = MUSIC_REPEATS;
	music__music.instrumentList = MUSIC_INSTRUMENTS;
	music__music.instrumentAt = MUSIC_INSTRUMENTS;
	music__effect.instrumentList = MUSIC_EFFECT_LIST;
	music__effect.instrumentAt = MUSIC_EFFECT_LIST;
}

/** ****************************************************************************
 *
 * \brief	Run the track player for one tick of the timer.
 *
 * \details	The sound part of TimerIsr (1000:1fe2), 72.8 times a second.
 * 			With the sound switched off (Ctrl) the speaker is switched
 * 			off. An active player counts down the ticks of its note;
 * 			while they last the vibrato bends the note, at their end the
 * 			next note starts.
 *
 ******************************************************************************/
void music_timerTick(void) {
	music__Player * pPlayer = music__pPlayer;

	if (globals_soundOn == 0 && globals_speakerOn != 0) {
		music__speakerOff();
	}

	if (pPlayer->active == 0) {
		return;
	}

	pPlayer->ticks--;

	if (pPlayer->ticks != 0) {
		music__vibrato(pPlayer);

		return;
	}

	music__nextNote(pPlayer);
}

/** ****************************************************************************
 *
 * \brief	Start the current player at a word of its track.
 *
 * \param	index			Word of the track to start at (MUSIC_FIRST_WORD).
 * \param	delay			Ticks to wait before the first note, less one.
 *
 * \details	Port of StartTrack (1000:1a05). With the music off the player
 * 			is only stopped. Otherwise the speaker goes off, the read
 * 			position and the pattern start are set, the repeat count
 * 			is cleared, the instrument list is rewound (the repeat list
 * 			is not) and the vibrato is enabled.
 *
 ******************************************************************************/
void music_startTrack(
	int16_t index,
	int16_t delay
) {
	music__Player * pPlayer = music__pPlayer;

	pPlayer->active = 0;

	if (globals_musicOn == 0) {
		return;
	}

	music__speakerOff();
	pPlayer->repeats = 0;
	pPlayer->gate = 1;
	pPlayer->position = (uint16_t)(pPlayer->track + index * 2);
	pPlayer->pattern = pPlayer->position;
	pPlayer->ticks = (uint16_t)(delay + 1);
	pPlayer->instrumentAt = pPlayer->instrumentList;
	music__setInstrument(pPlayer);
	globals_vibratoOn = 1;
	pPlayer->active = 1;
}

/** ****************************************************************************
 *
 * \brief	Stop the current player.
 *
 * \details	Port of StopPlayer (1000:0d99). An active player is stopped,
 * 			the speaker goes off, the garage's music hotspot gets its
 * 			"off" picture and pending input is dropped.
 *
 ******************************************************************************/
void music_stop(void) {
	music__Player * pPlayer = music__pPlayer;

	if (pPlayer->active == 0) {
		return;
	}

	pPlayer->active = 0;
	music__speakerOff();
	pPlayer->gate = 1;
	garage_setMusicPicture(false);
	event_reset();
}

/** ****************************************************************************
 *
 * \brief	Let the current player go on where it stopped.
 *
 * \details	Port of ResumePlayer (1000:0dd8). Only with the music on, the
 * 			player stopped and its track started before (the pattern
 * 			start has left the start of the track). The garage's music
 * 			hotspot gets its "on" picture and pending input is dropped.
 *
 ******************************************************************************/
void music_resume(void) {
	music__Player * pPlayer = music__pPlayer;

	if (globals_musicOn == 0 || pPlayer->active != 0 ||
		pPlayer->pattern == pPlayer->track) {
		return;
	}

	pPlayer->active = 1;
	pPlayer->gate = 1;
	garage_setMusicPicture(true);
	event_reset();
}

/** ****************************************************************************
 *
 * \brief	Switch the music on or off (the M key).
 *
 * \details	The original has these lines inline in WaitEvent (1000:1417),
 * 			MainLoop (1000:503f) and WaitTicksOrInput (1f38:0b51).
 *
 ******************************************************************************/
void music_toggle(void) {
	if (globals_musicOn == 0) {
		globals_musicOn = 1;
		music_resume();
	} else {
		music_stop();
		globals_musicOn = 0;
	}
}

/** ****************************************************************************
 *
 * \brief	Switch the music off for a while, or on again.
 *
 * \param	pause			Not 0: switch the music off if it is on, and
 * 							remember that. 0: switch it on again if it was
 * 							switched off here.
 *
 * \details	Port of PauseMusic (1f38:7001), used around drives, races,
 * 			the gas station and the tire change.
 *
 ******************************************************************************/
void music_pause(
	int16_t pause
) {
	if (pause != 0) {
		if (globals_musicOn != 0) {
			music_stop();
			globals_musicOn = 0;
			globals_musicPaused = 1;

			return;
		}
	} else {
		if (globals_musicPaused == 0) {
			return;
		}

		globals_musicOn = 1;
		music_resume();
	}

	globals_musicPaused = 0;
}

/** ****************************************************************************
 *
 * rief	Play a looping effect track in place of the music, or go back
 * 			to the music.
 *
 * \param	offset			Offset of the track in its segment.
 * \param	segment			Segment of the track (MUSIC_TRACK_SEGMENT: the
 * 							music; MUSIC_EFFECT_SEGMENT: an effect).
 * \param	repeatList		DS offset of the repeat list of the track.
 *
 * \details	Port of PlayEffectTrack (1f38:6f0a). An effect track goes to
 * 			the effect player, which becomes the current one and starts
 * 			at once; the music flag is saved and forced on meanwhile, so
 * 			the effect also sounds with the music switched off, and the M
 * 			key is locked. The music's own track switches back: the music
 * 			flag is restored, the speaker goes off if the music is off,
 * 			and the music player, which stood still meanwhile, is the
 * 			current one again with its instrument and the vibrato on.
 *
 ******************************************************************************/
void music_playEffectTrack(
	uint16_t offset,
	uint16_t segment,
	uint16_t repeatList
) {
	if (offset == music__music.track && segment == music__music.segment) {
		if (globals_musicSaved != MUSIC_NOT_SAVED) {
			globals_musicOn = globals_musicSaved;
			globals_musicSaved = MUSIC_NOT_SAVED;
		}

		if (globals_musicOn == 0) {
			music__speakerOff();
		}

		globals_musicAvailable = MUSIC_MODE_MUSIC;
		music__pPlayer = &music__music;
		music__setInstrument(&music__music);
		globals_vibratoOn = 1;

		return;
	}

	if (globals_musicSaved == MUSIC_NOT_SAVED) {
		globals_musicSaved = globals_musicOn;
		globals_musicOn = 1;
	}

	globals_musicAvailable = MUSIC_MODE_EFFECT;
	music__effect.segment = segment;
	music__effect.track = offset;
	music__effect.pattern = offset;
	music__effect.position = offset;
	music__effect.repeatList = repeatList;
	music__effect.repeatAt = repeatList;
	music__pPlayer = &music__effect;
	music_startTrack(MUSIC_FIRST_WORD, 0);
}

/** ****************************************************************************
 *
 * rief	Set the vibrato of the effect track that was just started.
 *
 * \param	enable			Not 0: use the vibrato given here. 0: no vibrato.
 * \param	row				Row of the vibrato table.
 * \param	steps			Number of steps of the row that are used.
 * \param	depth			Depth in 1/1000 of the divisor.
 * \param	speed			Ticks per step.
 *
 * \details	Port of SetEffectVibrato (1f38:6fbd): fills instrument record
 * 			0 (DS:0x5826), the one the effect player's instrument list
 * 			names, and sets the vibrato switch.
 *
 ******************************************************************************/
void music_setEffectVibrato(
	int16_t enable,
	int16_t row,
	int16_t steps,
	int16_t depth,
	int16_t speed
) {
	if (enable != 0) {
		exe_writeU16(MUSIC_RECORDS + MUSIC_RECORD_POSITION, 0);
		exe_writeU16(MUSIC_RECORDS + MUSIC_RECORD_ROW, (uint16_t)row);
		exe_writeU16(MUSIC_RECORDS + MUSIC_RECORD_DEPTH, (uint16_t)depth);
		exe_writeU16(MUSIC_RECORDS + MUSIC_RECORD_STEPS, (uint16_t)steps);
		exe_writeU16(MUSIC_RECORDS + MUSIC_RECORD_SPEED, (uint16_t)speed);
	}

	globals_vibratoOn = enable;
}
