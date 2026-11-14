/** ****************************************************************************
 *
 * \file	fpu.h
 *
 * \brief	Replacement for the 8087 emulator: floating point numbers with
 * 			the precision of the 8087's registers, in integer arithmetic.
 *
 ******************************************************************************/

#ifndef DOS_FPU_H_INCLUDED
#define DOS_FPU_H_INCLUDED

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <stdint.h>

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

/**
 * A number as the 8087 holds it in a register (80-bit temporary real):
 * value = mantissa * 2^(exponent - 63). Infinities, NaNs and denormals are
 * not needed by the game and not supported.
 */
typedef struct fpu_real_tag {
	uint64_t mantissa; /**< Bit 63 set, or 0 for the number 0. */
	int32_t	 exponent; /**< Power of 2 of bit 63. */
	bool	 negative; /**< Sign; false for 0. */
} fpu_Real;

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

fpu_Real fpu_fromInt(
	int32_t value
);

fpu_Real fpu_fromSingle(
	const uint8_t * pBytes
);

fpu_Real fpu_fromDouble(
	const uint8_t * pBytes
);

fpu_Real fpu_toDouble(
	fpu_Real value
);

void fpu_toSingle(
	fpu_Real  value,
	uint8_t * pBytes
);

fpu_Real fpu_add(
	fpu_Real left,
	fpu_Real right
);

fpu_Real fpu_sub(
	fpu_Real left,
	fpu_Real right
);

fpu_Real fpu_mul(
	fpu_Real left,
	fpu_Real right
);

fpu_Real fpu_div(
	fpu_Real dividend,
	fpu_Real divisor
);

fpu_Real fpu_abs(
	fpu_Real value
);

int fpu_compare(
	fpu_Real left,
	fpu_Real right
);

int32_t fpu_toLong(
	fpu_Real value
);

#endif // DOS_FPU_H_INCLUDED
