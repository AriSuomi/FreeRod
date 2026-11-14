/** ****************************************************************************
 *
 * \file	sound.c
 *
 * \brief	Platform sound: the PC speaker, played through SDL3 audio.
 *
 * \details	The game had one sound device: the speaker, driven by channel 2
 * 			of the timer chip (a square wave of 1193182 Hz / divisor) and
 * 			two bits of port 61h (bit 0: the channel's gate, bit 1: the
 * 			speaker enable, which also moves the speaker by hand while the
 * 			gate is off). This file emulates exactly that.
 *
 * 			Every change (plat_soundSetDivisor, plat_soundSetSpeaker) is
 * 			stamped with the sound clock, which counts steps of the timer
 * 			chip's input clock (PLAT_SOUND_HZ per second), and queued. The
 * 			sound clock follows real time; plat_soundDelay moves it ahead
 * 			without waiting, so effects that changed the speaker faster
 * 			than the game's timer ticks keep their exact timing. The caller
 * 			then waits until real time has caught up (plat_soundAhead).
 *
 * 			SDL's audio thread renders the queue: it runs the channel's
 * 			counter step by step and outputs, per sample, the share of
 * 			the sample's time the speaker was on (so tones above the
 * 			sample rate fade instead of turning into loud false tones),
 * 			without its constant part and slightly muffled, like the small
 * 			speaker. No game code runs in that thread.
 *
 * 			Another platform (STM32) can implement the same calls by
 * 			programming a PWM timer directly and ignoring the queue.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "platform/platform.h"
#include "platform/sound.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define LOG_APP			   SDL_LOG_CATEGORY_APPLICATION
#define SOUND_SAMPLE_HZ	   48000 /**< Samples per second rendered. */
#define SOUND_QUEUE_SIZE   4096	 /**< Changes waiting to be rendered. */
#define SOUND_CHUNK		   512	 /**< Samples rendered in one go. */
#define SOUND_AMPLITUDE	   8000	 /**< Speaker on, of 32767. */
#define SOUND_WAV_HEADER   44	 /**< Bytes before the samples of a WAV. */

#define SOUND_KIND_DIVISOR 0 /**< Change of the channel's divisor. */
#define SOUND_KIND_SPEAKER 1 /**< Change of the port 61h bits. */

#define SOUND_BIT_GATE	   1 /**< Port 61h bit 0: the counter runs. */
#define SOUND_BIT_ENABLE   2 /**< Port 61h bit 1: output reaches the speaker. */

/**
 * How far behind the sound clock the renderer is kept at least, in steps
 * (5 ms), so a change is queued before its time is rendered.
 */
#define SOUND_MARGIN (PLAT_SOUND_HZ / 200)

/**
 * When a change would wait longer than this many steps (150 ms) for the
 * renderer, the clocks have drifted apart and are lined up again.
 */
#define SOUND_MAX_LEAD (PLAT_SOUND_HZ * 3 / 20)

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/**
 * A change of the speaker's state.
 */
typedef struct plat__sound_event_tag {
	uint64_t time;	/**< When, in steps of the renderer's clock. */
	uint8_t	 kind;	/**< SOUND_KIND_*. */
	uint16_t value; /**< The divisor, or the SOUND_BIT_* bits. */
} plat__SoundEvent;

/*******************************************************************************
;
;	S T A T I C   F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

static uint64_t plat__soundNow(void);

static void plat__soundPost(
	uint8_t	 kind,
	uint16_t value
);

static void plat__soundApply(
	const plat__SoundEvent * pEvent
);

static uint32_t plat__soundHalfPeriod(void);

static int16_t plat__soundSample(void);

static void SDLCALL plat__soundCallback(
	void *			  pUserData,
	SDL_AudioStream * pStream,
	int				  additional,
	int				  total
);

static void plat__soundWriteU32(
	uint8_t * pBytes,
	uint32_t  value
);

static void plat__soundFinishWav(void);

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/**
 * The audio stream, or NULL when there is no sound device. Its lock
 * protects everything below that the audio thread uses.
 */
static SDL_AudioStream * plat__soundStream;

static bool plat__soundOverflowed; /**< Queue overflow already reported. */

/**
 * The sound clock in steps since the platform started. Only the game's
 * thread uses it.
 */
static uint64_t plat__soundClock;

/**
 * Renderer's clock minus sound clock, in steps.
 */
static int64_t plat__soundOffset;

static uint64_t plat__soundLastTime; /**< Time of the newest event. */

/**
 * The queued changes, a ring buffer in time order.
 */
static plat__SoundEvent plat__soundEvents[SOUND_QUEUE_SIZE];

static int plat__soundTail;	 /**< Oldest queued event. */
static int plat__soundCount; /**< Queued events. */

static uint64_t plat__soundRendered;		/**< Steps rendered so far. */
static uint32_t plat__soundStepRest;		/**< Fraction of a step, 1/48000. */
static uint32_t plat__soundDivisor = 65536; /**< Channel 2 divisor, 2... */
static uint32_t plat__soundCounter = 32768; /**< Steps left of this half. */
static bool		plat__soundGate;			/**< Port 61h bit 0. */
static bool		plat__soundEnable;			/**< Port 61h bit 1. */
static bool		plat__soundOutput = true;	/**< Output of channel 2. */
static int32_t	plat__soundLastInput;		/**< Filter: last raw level. */
static int32_t	plat__soundHighPass;		/**< Filter: level without DC. */
static int32_t	plat__soundLowPass;			/**< Filter: muffled level. */

static FILE *	plat__pSoundWav;	 /**< Recording, or NULL. */
static uint32_t plat__soundWavBytes; /**< Sample bytes recorded. */
static FILE *	plat__pSoundLog;	 /**< Log of the changes, or NULL. */

/*******************************************************************************
;
;	F U N C T I O N   D E F I N I T I O N S
;
;-----------------------------------------------------------------------------*/

/** ****************************************************************************
 *
 * \brief	Open the sound device and start playing (silence at first).
 *
 * \retval	true	Sound is played.
 * \retval	false	No sound device; the reason is logged. The plat_sound*
 * 					calls still work and keep their timing.
 *
 ******************************************************************************/
bool plat_soundOpen(void) {
	SDL_AudioSpec format = {
		.format = SDL_AUDIO_S16,
		.channels = 1,
		.freq = SOUND_SAMPLE_HZ,
	};

	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		SDL_LogWarn(LOG_APP, "no sound: %s", SDL_GetError());
		return false;
	}

	plat__soundStream = SDL_OpenAudioDeviceStream(
		SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
		&format,
		plat__soundCallback,
		NULL
	);

	if (!plat__soundStream) {
		SDL_LogWarn(LOG_APP, "no sound: %s", SDL_GetError());
		return false;
	}

	SDL_ResumeAudioStreamDevice(plat__soundStream);

	return true;
}

/** ****************************************************************************
 *
 * \brief	Stop playing, close the sound device and finish the files of
 * 			plat_soundRecord and plat_soundLog.
 *
 ******************************************************************************/
void plat_soundClose(void) {
	if (plat__soundStream) {
		SDL_DestroyAudioStream(plat__soundStream);
		plat__soundStream = NULL;
	}

	plat__soundFinishWav();

	if (plat__pSoundLog) {
		fclose(plat__pSoundLog);
		plat__pSoundLog = NULL;
	}
}

/** ****************************************************************************
 *
 * \brief	Set the divisor of the timer chip's channel 2, the pitch of
 * 			the speaker.
 *
 * \param	divisor			The tone is PLAT_SOUND_HZ / divisor Hz; 0 means
 * 							65536.
 *
 * \details	As on the chip, the running half wave is finished at the old
 * 			pitch.
 *
 ******************************************************************************/
void plat_soundSetDivisor(
	uint16_t divisor
) {
	plat__soundPost(SOUND_KIND_DIVISOR, divisor);
}

/** ****************************************************************************
 *
 * \brief	Set the two speaker bits of port 61h.
 *
 * \param	gate			Bit 0: channel 2 counts, so it makes its square
 * 							wave. When off, its output rests high.
 * \param	enable			Bit 1: the channel's output reaches the speaker.
 * 							With the gate off this moves the speaker
 * 							directly: true pushes it out, false lets go.
 *
 ******************************************************************************/
void plat_soundSetSpeaker(
	bool gate,
	bool enable
) {
	uint16_t bits = 0;

	if (gate) {
		bits |= SOUND_BIT_GATE;
	}

	if (enable) {
		bits |= SOUND_BIT_ENABLE;
	}

	plat__soundPost(SOUND_KIND_SPEAKER, bits);
}

/** ****************************************************************************
 *
 * \brief	Move the sound clock ahead: the changes that follow happen
 * 			this much later.
 *
 * \param	steps			Time in steps of 1 / PLAT_SOUND_HZ seconds.
 *
 * \details	Does not wait. A caller that makes sound in a loop of changes
 * 			and delays must wait itself while plat_soundAhead is large,
 * 			so the sound does not run away from the game.
 *
 ******************************************************************************/
void plat_soundDelay(
	uint32_t steps
) {
	uint64_t now = plat__soundNow();

	if (plat__soundClock < now) {
		plat__soundClock = now;
	}

	plat__soundClock += steps;
}

/** ****************************************************************************
 *
 * \brief	How far the sound clock is ahead of real time.
 *
 * \return	Steps of 1 / PLAT_SOUND_HZ seconds; 0 when plat_soundDelay has
 * 			not moved it ahead or real time has caught up.
 *
 ******************************************************************************/
uint32_t plat_soundAhead(void) {
	uint64_t now = plat__soundNow();

	if (plat__soundClock <= now) {
		return 0;
	}

	return (uint32_t)(plat__soundClock - now);
}

/** ****************************************************************************
 *
 * \brief	Record the sound played from now on as a WAV file (mono, 16
 * 			bits, 48000 Hz), for tests.
 *
 * \param	pPath			File to write.
 *
 * \retval	true	Recording.
 * \retval	false	The file could not be created, or there is no sound
 * 					device (the reason is logged).
 *
 * \details	The file is complete after plat_shutdown.
 *
 ******************************************************************************/
bool plat_soundRecord(
	const char * pPath
) {
	uint8_t header[SOUND_WAV_HEADER] = { 0 };

	if (!plat__soundStream) {
		SDL_LogError(LOG_APP, "no sound device, nothing to record");
		return false;
	}

	FILE * pFile = fopen(pPath, "wb");

	if (!pFile) {
		SDL_LogError(LOG_APP, "cannot create %s", pPath);
		return false;
	}

	/* The sizes are filled in by plat__soundFinishWav. */
	fwrite(header, 1, sizeof header, pFile);

	SDL_LockAudioStream(plat__soundStream);
	plat__pSoundWav = pFile;
	plat__soundWavBytes = 0;
	SDL_UnlockAudioStream(plat__soundStream);

	return true;
}

/** ****************************************************************************
 *
 * \brief	Write every change of the speaker from now on to a text file,
 * 			to compare the port's sound with the original as numbers.
 *
 * \param	pPath			File to write.
 *
 * \retval	true	Logging.
 * \retval	false	The file could not be created (logged).
 *
 * \details	One line per change: the sound clock in steps, a tab, "D" and
 * 			the divisor or "P" and the port 61h bits (1 gate, 2 enable).
 *
 ******************************************************************************/
bool plat_soundLog(
	const char * pPath
) {
	plat__pSoundLog = fopen(pPath, "w");

	if (!plat__pSoundLog) {
		SDL_LogError(LOG_APP, "cannot create %s", pPath);
		return false;
	}

	return true;
}

/** ****************************************************************************
 *
 * \brief	Real time on the sound clock.
 *
 * \return	Steps of 1 / PLAT_SOUND_HZ seconds since the platform started.
 *
 ******************************************************************************/
static uint64_t plat__soundNow(void) {
	uint64_t nanoseconds = SDL_GetTicksNS();

	/* Whole seconds and the rest separately: no overflow. */
	uint64_t seconds = nanoseconds / SDL_NS_PER_SECOND;
	uint64_t rest = nanoseconds % SDL_NS_PER_SECOND;

	return seconds * PLAT_SOUND_HZ + rest * PLAT_SOUND_HZ / SDL_NS_PER_SECOND;
}

/** ****************************************************************************
 *
 * \brief	Stamp a change with the sound clock and queue it for the
 * 			renderer.
 *
 * \param	kind			SOUND_KIND_*.
 * \param	value			The divisor, or the SOUND_BIT_* bits.
 *
 * \details	The renderer's clock runs with the sound device, ahead of real
 * 			time by what is buffered. The offset between the two clocks
 * 			is corrected whenever a change made now would already be in
 * 			the rendered past, or would wait too long.
 *
 ******************************************************************************/
static void plat__soundPost(
	uint8_t	 kind,
	uint16_t value
) {
	uint64_t now = plat__soundNow();

	if (plat__soundClock < now) {
		plat__soundClock = now;
	}

	if (plat__pSoundLog) {
		fprintf(
			plat__pSoundLog,
			"%llu\t%c\t%u\n",
			(unsigned long long)plat__soundClock,
			kind == SOUND_KIND_DIVISOR ? 'D' : 'P',
			(unsigned)value
		);
	}

	if (!plat__soundStream) {
		return;
	}

	bool overflow = false;

	SDL_LockAudioStream(plat__soundStream);

	int64_t lead = (int64_t)now + plat__soundOffset -
		(int64_t)plat__soundRendered;

	if (lead < 0 || lead > (int64_t)SOUND_MAX_LEAD) {
		plat__soundOffset += (int64_t)SOUND_MARGIN - lead;
	}

	uint64_t time = (uint64_t)((int64_t)plat__soundClock + plat__soundOffset);

	if (time < plat__soundLastTime) {
		/* The offset went back: keep the queue in order. */
		time = plat__soundLastTime;
	}

	plat__soundLastTime = time;

	if (plat__soundCount == SOUND_QUEUE_SIZE) {
		/* Make room: the oldest change happens early. */
		plat__soundApply(&plat__soundEvents[plat__soundTail]);
		plat__soundTail = (plat__soundTail + 1) % SOUND_QUEUE_SIZE;
		plat__soundCount--;
		overflow = true;
	}

	int head = (plat__soundTail + plat__soundCount) % SOUND_QUEUE_SIZE;

	plat__soundEvents[head].time = time;
	plat__soundEvents[head].kind = kind;
	plat__soundEvents[head].value = value;
	plat__soundCount++;

	SDL_UnlockAudioStream(plat__soundStream);

	if (overflow && !plat__soundOverflowed) {
		plat__soundOverflowed = true;
		SDL_LogWarn(LOG_APP, "sound queue full, timing is off");
	}
}

/** ****************************************************************************
 *
 * \brief	Make a queued change in the emulated timer chip and port.
 *
 * \param	pEvent			The change.
 *
 * \details	Channel 2 runs in mode 3 (square wave): a new divisor counts
 * 			from the next half wave on; while the gate is off the counter
 * 			stands and the output is high; switching the gate on starts a
 * 			new wave.
 *
 ******************************************************************************/
static void plat__soundApply(
	const plat__SoundEvent * pEvent
) {
	if (pEvent->kind == SOUND_KIND_DIVISOR) {
		plat__soundDivisor = pEvent->value;

		if (plat__soundDivisor == 0) {
			plat__soundDivisor = 65536;
		}

		if (plat__soundDivisor < 2) {
			/* 1 is not allowed in mode 3. */
			plat__soundDivisor = 2;
		}

		return;
	}

	bool gate = (pEvent->value & SOUND_BIT_GATE) != 0;

	plat__soundEnable = (pEvent->value & SOUND_BIT_ENABLE) != 0;

	if (gate != plat__soundGate) {
		plat__soundGate = gate;
		plat__soundOutput = true;
		plat__soundCounter = plat__soundHalfPeriod();
	}
}

/** ****************************************************************************
 *
 * \brief	Length of the half wave channel 2 starts now.
 *
 * \return	Steps: half the divisor; with an odd divisor the high half is
 * 			one step longer than the low half.
 *
 ******************************************************************************/
static uint32_t plat__soundHalfPeriod(void) {
	if (plat__soundOutput) {
		return (plat__soundDivisor + 1) / 2;
	}

	return plat__soundDivisor / 2;
}

/** ****************************************************************************
 *
 * \brief	Render the next sample.
 *
 * \return	The sample.
 *
 * \details	Runs the emulated chip through the steps the sample covers (24
 * 			or 25), making the queued changes at their times, and counts
 * 			the steps with the speaker on. That share is the raw level.
 * 			A high-pass (about 30 Hz) removes its constant part, so a
 * 			speaker held on is silent after a click, and a low-pass
 * 			(about 5 kHz, and nothing at half the sample rate) rounds
 * 			the edges, so the 25 kHz tone the game leaves playing
 * 			between its sounds is next to silent.
 *
 ******************************************************************************/
static int16_t plat__soundSample(void) {
	plat__soundStepRest += PLAT_SOUND_HZ;

	uint32_t steps = plat__soundStepRest / SOUND_SAMPLE_HZ;
	uint32_t remaining = steps;
	uint32_t high = 0;

	plat__soundStepRest %= SOUND_SAMPLE_HZ;

	while (remaining > 0) {
		uint32_t run = remaining;

		while (plat__soundCount > 0 &&
			   plat__soundEvents[plat__soundTail].time <= plat__soundRendered) {
			plat__soundApply(&plat__soundEvents[plat__soundTail]);
			plat__soundTail = (plat__soundTail + 1) % SOUND_QUEUE_SIZE;
			plat__soundCount--;
		}

		if (plat__soundCount > 0) {
			uint64_t until = plat__soundEvents[plat__soundTail].time -
				plat__soundRendered;

			if (until < run) {
				run = (uint32_t)until;
			}
		}

		if (plat__soundGate && plat__soundCounter < run) {
			run = plat__soundCounter;
		}

		if (plat__soundEnable && plat__soundOutput) {
			high += run;
		}

		plat__soundRendered += run;
		remaining -= run;

		if (plat__soundGate) {
			plat__soundCounter -= run;

			if (plat__soundCounter == 0) {
				plat__soundOutput = !plat__soundOutput;
				plat__soundCounter = plat__soundHalfPeriod();
			}
		}
	}

	/* Levels are kept times 256 for the filters. */
	int32_t input = (int32_t)(high * (SOUND_AMPLITUDE * 256) / steps);

	int32_t lastLowPass = plat__soundLowPass;

	plat__soundHighPass = plat__soundHighPass * 255 / 256 + input -
		plat__soundLastInput;
	plat__soundLastInput = input;
	plat__soundLowPass += (plat__soundHighPass - plat__soundLowPass) / 2;

	/* The mean of two samples removes what is left near 24 kHz. */
	return (int16_t)((plat__soundLowPass + lastLowPass) / 512);
}

/** ****************************************************************************
 *
 * \brief	SDL asks for more sound: render it.
 *
 * \param	pUserData		Not used.
 * \param	pStream			The audio stream; SDL holds its lock.
 * \param	additional		Bytes needed now.
 * \param	total			Bytes wanted in the stream; not used.
 *
 * \details	Runs in SDL's audio thread.
 *
 ******************************************************************************/
static void SDLCALL plat__soundCallback(
	void *			  pUserData,
	SDL_AudioStream * pStream,
	int				  additional,
	int				  total
) {
	static int16_t samples[SOUND_CHUNK];

	int needed = additional / (int)sizeof samples[0];

	(void)pUserData;
	(void)total;

	while (needed > 0) {
		int count = needed < SOUND_CHUNK ? needed : SOUND_CHUNK;
		int bytes = count * (int)sizeof samples[0];

		for (int index = 0; index < count; index++) {
			samples[index] = plat__soundSample();
		}

		SDL_PutAudioStreamData(pStream, samples, bytes);

		if (plat__pSoundWav) {
			/* The PC is little-endian, like WAV. */
			fwrite(samples, 1, (size_t)bytes, plat__pSoundWav);
			plat__soundWavBytes += (uint32_t)bytes;
		}

		needed -= count;
	}
}

/** ****************************************************************************
 *
 * \brief	Store a 32-bit number, low byte first.
 *
 * \param	pBytes			Where to store the 4 bytes.
 * \param	value			The number.
 *
 ******************************************************************************/
static void plat__soundWriteU32(
	uint8_t * pBytes,
	uint32_t  value
) {
	for (int index = 0; index < 4; index++) {
		pBytes[index] = (uint8_t)(value >> (8 * index));
	}
}

/** ****************************************************************************
 *
 * \brief	Write the header of the recording and close it.
 *
 * \details	Call only after the audio stream is gone.
 *
 ******************************************************************************/
static void plat__soundFinishWav(void) {
	uint8_t header[SOUND_WAV_HEADER];

	if (!plat__pSoundWav) {
		return;
	}

	memcpy(header, "RIFF", 4);
	plat__soundWriteU32(header + 4, plat__soundWavBytes + 36);
	memcpy(header + 8, "WAVEfmt ", 8);
	plat__soundWriteU32(header + 16, 16);

	/* PCM (1), one channel. */
	plat__soundWriteU32(header + 20, 0x00010001);
	plat__soundWriteU32(header + 24, SOUND_SAMPLE_HZ);
	plat__soundWriteU32(header + 28, SOUND_SAMPLE_HZ * 2);

	/* 2 bytes per sample frame, 16 bits per sample. */
	plat__soundWriteU32(header + 32, 0x00100002);
	memcpy(header + 36, "data", 4);
	plat__soundWriteU32(header + 40, plat__soundWavBytes);

	fseek(plat__pSoundWav, 0, SEEK_SET);
	fwrite(header, 1, sizeof header, plat__pSoundWav);
	fclose(plat__pSoundWav);
	plat__pSoundWav = NULL;
}
