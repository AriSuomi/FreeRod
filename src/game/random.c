/** ****************************************************************************
 *
 * \file	random.c
 *
 * \brief	The game's random number generator.
 *
 * \details	Three small linear congruential generators (the constants of
 * 			the Wichmann-Hill generator, computed in 16 bits without
 * 			overflow), combined by adding their remainders.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "game/random.h"

#include "game/globals.h"
#include "platform/platform.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define RANDOM_SEED_RANGE 30000 /**< Seeds are 1..30000. */

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

static int16_t random__step(
	int16_t seed,
	int16_t divisor,
	int16_t factor,
	int16_t borrow,
	int16_t modulus
);

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
 * \brief	Get a random number, or seed the generator.
 *
 * \param	range			> 0: the result is 0 .. range - 1. <= 0: seed the
 * 							generator from the time of day.
 *
 * \return	The random number; -1 after seeding.
 *
 * \details	Port of FUN_1f38_5eb6. Seeding takes the low 16 bits of the
 * 			time in seconds modulo 30000, plus 1, for the first
 * 			generator, and runs the second and third generator from that
 * 			value until each gives a seed of at most 30000.
 *
 ******************************************************************************/
int16_t random_next(
	int16_t range
) {
	if (range < 1) {
		int16_t lowTime = (int16_t)(plat_timeSeconds() & 0xffff);
		int16_t seed = (int16_t)(lowTime % RANDOM_SEED_RANGE + 1);

		globals_randomSeed1 = seed;
		globals_randomSeed2 = seed;
		globals_randomSeed3 = seed;

		do {
			seed = random__step(seed, 177, 171, 2, 30269);
		} while (seed > RANDOM_SEED_RANGE);

		globals_randomSeed2 = seed;
		seed = globals_randomSeed3;

		do {
			seed = random__step(seed, 178, 170, 63, 30323);
		} while (seed > RANDOM_SEED_RANGE);

		globals_randomSeed3 = seed;

		return -1;
	}

	globals_randomSeed1 =
		random__step(globals_randomSeed1, 177, 171, 2, 30269);
	globals_randomSeed2 =
		random__step(globals_randomSeed2, 176, 172, 35, 30307);
	globals_randomSeed3 =
		random__step(globals_randomSeed3, 178, 170, 63, 30323);

	int sum = globals_randomSeed1 % range + globals_randomSeed2 % range +
		globals_randomSeed3 % range;

	return (int16_t)(sum % range);
}

/** ****************************************************************************
 *
 * \brief	Advance one generator.
 *
 * \param	seed			Its current value.
 * \param	divisor			modulus / factor, rounded down.
 * \param	factor			Multiplier of the generator.
 * \param	borrow			modulus % factor.
 * \param	modulus			Modulus of the generator.
 *
 * \return	(seed * factor) % modulus, computed without leaving 16 bits.
 *
 ******************************************************************************/
static int16_t random__step(
	int16_t seed,
	int16_t divisor,
	int16_t factor,
	int16_t borrow,
	int16_t modulus
) {
	int next = (seed % divisor) * factor - (seed / divisor) * borrow;

	if (next < 0) {
		next += modulus;
	}

	return (int16_t)next;
}
