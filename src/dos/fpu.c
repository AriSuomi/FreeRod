/** ****************************************************************************
 *
 * \file	fpu.c
 *
 * \brief	Replacement for the 8087 emulator: floating point numbers with
 * 			the precision of the 8087's registers, in integer arithmetic.
 *
 * \details	The game's floating point code (prices, top speed) runs on the
 * 			8087 emulator of the Microsoft C runtime (INT 34h..3Dh): every
 * 			result is rounded to a 64-bit mantissa, to nearest or even, and
 * 			only stores to qword reals round to 53 bits. These functions give
 * 			the same results without the host's floating point (the port
 * 			has no 80-bit type, and float rounds differently). Constants come
 * 			from Sr.exe as 8-byte (qword) reals.
 *
 ******************************************************************************/

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

#include "dos/fpu.h"

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define FPU_TOP_BIT			0x8000000000000000ull /**< Bit 63. */
#define FPU_DOUBLE_DROPPED	0x7ffull			  /**< Bits a qword lacks. */
#define FPU_DOUBLE_HALF		0x400ull			  /**< Half of its last bit. */
#define FPU_DOUBLE_LAST		0x800ull			  /**< Its last bit. */
#define FPU_DOUBLE_FRACTION 52					  /**< Stored mantissa bits. */
#define FPU_DOUBLE_BIAS		1023				  /**< Exponent bias. */
#define FPU_DOUBLE_EXPONENT 0x7ff				  /**< Exponent field mask. */
#define FPU_SINGLE_FRACTION 23					  /**< Dword mantissa bits. */
#define FPU_SINGLE_BIAS		127					  /**< Dword exponent bias. */
#define FPU_SINGLE_EXPONENT 0xff				  /**< Dword exponent mask. */
#define FPU_SINGLE_SIGN		0x80000000ul		  /**< Dword sign bit. */
#define FPU_SINGLE_INF		0x7f800000ul		  /**< Dword infinity field. */
#define FPU_SINGLE_DROPPED	0xffffffffffull		  /**< Bits a dword lacks. */
#define FPU_SINGLE_HALF		0x8000000000ull		  /**< Half of its last bit. */
#define FPU_SINGLE_LAST		0x10000000000ull	  /**< Its last bit. */
#define FPU_SINGLE_SHIFT	40					  /**< Dropped dword bits. */
#define FPU_LOW_HALF		0xffffffffull		  /**< Low 32 bits. */

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

static fpu_Real fpu__zero(void);

static fpu_Real fpu__round(
	uint64_t high,
	uint64_t low,
	int32_t	 exponent,
	bool	 negative
);

static void fpu__multiply(
	uint64_t   left,
	uint64_t   right,
	uint64_t * pHigh,
	uint64_t * pLow
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
 * \brief	Load an integer (FILD).
 *
 * \param	value			The integer.
 *
 * \return	The same value; exact.
 *
 ******************************************************************************/
fpu_Real fpu_fromInt(
	int32_t value
) {
	if (value == 0) {
		return fpu__zero();
	}

	uint64_t magnitude =
		value < 0 ? (uint64_t)(-(int64_t)value) : (uint64_t)value;
	int32_t exponent = 63;

	while ((magnitude & FPU_TOP_BIT) == 0) {
		magnitude <<= 1;
		exponent--;
	}

	fpu_Real result = { magnitude, exponent, value < 0 };

	return result;
}

/** ****************************************************************************
 *
 * \brief	Load a 4-byte real (FLD dword).
 *
 * \param	pBytes			The 4 bytes of the real, lowest first (as in
 * 							Sr.exe's data segment).
 *
 * \return	The same value; exact. Denormals, infinities and NaNs are
 * 			read as 0.
 *
 ******************************************************************************/
fpu_Real fpu_fromSingle(
	const uint8_t * pBytes
) {
	uint32_t bits = 0;

	for (int index = 3; index >= 0; index--) {
		bits = (bits << 8) | pBytes[index];
	}

	int32_t exponent =
		(int32_t)((bits >> FPU_SINGLE_FRACTION) & FPU_SINGLE_EXPONENT);
	uint64_t fraction = bits & ((1ul << FPU_SINGLE_FRACTION) - 1);

	if (exponent == 0 || exponent == FPU_SINGLE_EXPONENT) {
		return fpu__zero();
	}

	fpu_Real result = {
		(fraction | (1ull << FPU_SINGLE_FRACTION)) << 40,
		exponent - FPU_SINGLE_BIAS,
		(bits & FPU_SINGLE_SIGN) != 0
	};

	return result;
}

/** ****************************************************************************
 *
 * \brief	Load an 8-byte real (FLD qword).
 *
 * \param	pBytes			The 8 bytes of the real, lowest first (as in
 * 							Sr.exe's data segment).
 *
 * \return	The same value; exact. Denormals, infinities and NaNs are
 * 			read as 0.
 *
 ******************************************************************************/
fpu_Real fpu_fromDouble(
	const uint8_t * pBytes
) {
	uint64_t bits = 0;

	for (int index = 7; index >= 0; index--) {
		bits = (bits << 8) | pBytes[index];
	}

	int32_t exponent =
		(int32_t)((bits >> FPU_DOUBLE_FRACTION) & FPU_DOUBLE_EXPONENT);
	uint64_t fraction = bits & ((1ull << FPU_DOUBLE_FRACTION) - 1);

	if (exponent == 0 || exponent == FPU_DOUBLE_EXPONENT) {
		return fpu__zero();
	}

	fpu_Real result = {
		(fraction | (1ull << FPU_DOUBLE_FRACTION)) << 11,
		exponent - FPU_DOUBLE_BIAS,
		(bits & FPU_TOP_BIT) != 0
	};

	return result;
}

/** ****************************************************************************
 *
 * \brief	Round to the precision of an 8-byte real, as a store to a qword
 * 			(FST qword) does.
 *
 * \param	value			The number.
 *
 * \return	The number with a 53-bit mantissa, rounded to nearest or
 * 			even.
 *
 ******************************************************************************/
fpu_Real fpu_toDouble(
	fpu_Real value
) {
	uint64_t dropped = value.mantissa & FPU_DOUBLE_DROPPED;
	uint64_t kept = value.mantissa & ~FPU_DOUBLE_DROPPED;

	if (dropped > FPU_DOUBLE_HALF ||
		(dropped == FPU_DOUBLE_HALF && (kept & FPU_DOUBLE_LAST) != 0)) {
		kept += FPU_DOUBLE_LAST;

		if (kept == 0) {
			kept = FPU_TOP_BIT;
			value.exponent++;
		}
	}

	value.mantissa = kept;

	return value;
}

/** ****************************************************************************
 *
 * \brief	Store a 4-byte real (FST / FSTP dword).
 *
 * \param	value			The number.
 * \param	pBytes			Out: the 4 bytes of the real, lowest first (as in
 * 							Sr.exe's data segment).
 *
 * \details	Rounds the mantissa to 24 bits to nearest or even, and writes
 * 			zero on underflow or infinity on overflow.
 *
 ******************************************************************************/
void fpu_toSingle(
	fpu_Real  value,
	uint8_t * pBytes
) {
	uint32_t bits = 0;

	if (value.mantissa != 0) {
		uint64_t dropped = value.mantissa & FPU_SINGLE_DROPPED;
		uint64_t kept = value.mantissa & ~FPU_SINGLE_DROPPED;

		if (dropped > FPU_SINGLE_HALF ||
			(dropped == FPU_SINGLE_HALF && (kept & FPU_SINGLE_LAST) != 0)) {
			kept += FPU_SINGLE_LAST;

			if (kept == 0) {
				kept = FPU_TOP_BIT;
				value.exponent++;
			}
		}

		int32_t biased = value.exponent + FPU_SINGLE_BIAS;

		if (biased >= FPU_SINGLE_EXPONENT) {
			bits = FPU_SINGLE_INF;
		} else if (biased > 0) {
			uint32_t mask = (1ul << FPU_SINGLE_FRACTION) - 1;

			bits = ((uint32_t)biased << FPU_SINGLE_FRACTION) |
				(uint32_t)((kept >> FPU_SINGLE_SHIFT) & mask);
		}

		if (value.negative && bits != 0) {
			bits |= FPU_SINGLE_SIGN;
		}
	}

	for (int index = 0; index < 4; index++) {
		pBytes[index] = (uint8_t)(bits >> (index * 8));
	}
}

/** ****************************************************************************
 *
 * \brief	Add two numbers (FADD).
 *
 * \param	left			First number.
 * \param	right			Second number.
 *
 * \return	The sum, rounded to a 64-bit mantissa.
 *
 ******************************************************************************/
fpu_Real fpu_add(
	fpu_Real left,
	fpu_Real right
) {
	if (right.mantissa == 0) {
		return left;
	}

	if (left.mantissa == 0) {
		return right;
	}

	if (right.exponent > left.exponent ||
		(right.exponent == left.exponent &&
		 right.mantissa > left.mantissa)) {
		fpu_Real larger = right;

		right = left;
		left = larger;
	}

	/*
	 * The smaller number shifted to the exponent of the larger one, in
	 * 128 bits; bits shifted out of those set bit 0 (sticky).
	 */
	int32_t	 distance = left.exponent - right.exponent;
	uint64_t high = right.mantissa;
	uint64_t low = 0;

	if (distance >= 128) {
		high = 0;
		low = 1;
	} else if (distance > 64) {
		uint64_t lost = right.mantissa << (128 - distance);

		high = 0;
		low = (right.mantissa >> (distance - 64)) | (lost != 0 ? 1 : 0);
	} else if (distance == 64) {
		high = 0;
		low = right.mantissa;
	} else if (distance > 0) {
		low = right.mantissa << (64 - distance);
		high = right.mantissa >> distance;
	}

	int32_t exponent = left.exponent;

	if (left.negative == right.negative) {
		uint64_t sumHigh = left.mantissa + high;
		uint64_t sumLow = low;

		if (sumHigh < left.mantissa) {
			sumLow = (sumLow >> 1) | (sumHigh << 63) | (sumLow & 1);
			sumHigh = (sumHigh >> 1) | FPU_TOP_BIT;
			exponent++;
		}

		return fpu__round(sumHigh, sumLow, exponent, left.negative);
	}

	uint64_t borrow = low != 0 ? 1 : 0;
	uint64_t differenceLow = 0 - low;
	uint64_t differenceHigh = left.mantissa - high - borrow;

	if (differenceHigh == 0 && differenceLow == 0) {
		return fpu__zero();
	}

	while ((differenceHigh & FPU_TOP_BIT) == 0) {
		differenceHigh = (differenceHigh << 1) | (differenceLow >> 63);
		differenceLow <<= 1;
		exponent--;
	}

	return fpu__round(differenceHigh, differenceLow, exponent, left.negative);
}

/** ****************************************************************************
 *
 * \brief	Subtract a number from another (FSUB, FSUBR).
 *
 * \param	left			Number to subtract from.
 * \param	right			Number to subtract.
 *
 * \return	left - right, rounded to a 64-bit mantissa.
 *
 ******************************************************************************/
fpu_Real fpu_sub(
	fpu_Real left,
	fpu_Real right
) {
	if (right.mantissa != 0) {
		right.negative = !right.negative;
	}

	return fpu_add(left, right);
}

/** ****************************************************************************
 *
 * \brief	Multiply two numbers (FMUL, FIMUL).
 *
 * \param	left			First number.
 * \param	right			Second number.
 *
 * \return	The product, rounded to a 64-bit mantissa.
 *
 ******************************************************************************/
fpu_Real fpu_mul(
	fpu_Real left,
	fpu_Real right
) {
	if (left.mantissa == 0 || right.mantissa == 0) {
		return fpu__zero();
	}

	uint64_t high;
	uint64_t low;
	int32_t	 exponent = left.exponent + right.exponent;

	fpu__multiply(left.mantissa, right.mantissa, &high, &low);

	if ((high & FPU_TOP_BIT) != 0) {
		exponent++;
	} else {
		high = (high << 1) | (low >> 63);
		low <<= 1;
	}

	return fpu__round(high, low, exponent, left.negative != right.negative);
}

/** ****************************************************************************
 *
 * \brief	Divide a number by another (FDIV).
 *
 * \param	dividend		Number to divide.
 * \param	divisor			Number to divide by; not 0.
 *
 * \return	The quotient, rounded to a 64-bit mantissa (0 when the divisor
 * 			is 0, which the game never divides by).
 *
 ******************************************************************************/
fpu_Real fpu_div(
	fpu_Real dividend,
	fpu_Real divisor
) {
	if (dividend.mantissa == 0 || divisor.mantissa == 0) {
		return fpu__zero();
	}

	uint64_t remainder = dividend.mantissa;
	uint64_t quotient = 1;
	int32_t	 exponent = dividend.exponent - divisor.exponent;

	/*
	 * Both mantissas have bit 63 set, so the first quotient bit is 1:
	 * either at once, or after doubling the remainder (one exponent less).
	 * Doubling may carry out of 64 bits; the subtraction then wraps to
	 * the right remainder.
	 */
	if (remainder < divisor.mantissa) {
		exponent--;
		remainder <<= 1;
	}

	remainder -= divisor.mantissa;

	for (int step = 0; step < 64; step++) {
		bool carry = (remainder & FPU_TOP_BIT) != 0;

		remainder <<= 1;
		quotient <<= 1;

		if (carry || remainder >= divisor.mantissa) {
			remainder -= divisor.mantissa;
			quotient |= 1;
		}
	}

	/* Bit 0 of quotient is now the guard bit below the 64 kept bits. */
	uint64_t low = (quotient & 1) != 0 ? FPU_TOP_BIT : 0;

	if (remainder != 0) {
		low |= 1;
	}

	return fpu__round(
		(quotient >> 1) | FPU_TOP_BIT,
		low,
		exponent,
		dividend.negative != divisor.negative
	);
}

/** ****************************************************************************
 *
 * \brief	The absolute value (FABS).
 *
 * \param	value			The number.
 *
 * \return	The number without its sign.
 *
 ******************************************************************************/
fpu_Real fpu_abs(
	fpu_Real value
) {
	value.negative = false;

	return value;
}

/** ****************************************************************************
 *
 * \brief	Compare two numbers (FCOM).
 *
 * \param	left			First number.
 * \param	right			Second number.
 *
 * \return	-1, 0 or 1 when left is less than, equal to or greater than
 * 			right.
 *
 ******************************************************************************/
int fpu_compare(
	fpu_Real left,
	fpu_Real right
) {
	if (left.mantissa == 0 && right.mantissa == 0) {
		return 0;
	}

	if (left.negative != right.negative) {
		return left.negative ? -1 : 1;
	}

	int magnitude = 0;

	if (left.mantissa == 0) {
		magnitude = -1;
	} else if (right.mantissa == 0) {
		magnitude = 1;
	} else if (left.exponent != right.exponent) {
		magnitude = left.exponent < right.exponent ? -1 : 1;
	} else if (left.mantissa != right.mantissa) {
		magnitude = left.mantissa < right.mantissa ? -1 : 1;
	}

	return left.negative ? -magnitude : magnitude;
}

/** ****************************************************************************
 *
 * \brief	Convert to a long, cutting off the fraction.
 *
 * \param	value			The number.
 *
 * \return	The integer part (INT32_MIN when it does not fit, like the
 * 			8087's integer indefinite).
 *
 * \details	Stands in for _ftol (2e16:2de9) of the C runtime, which stores
 * 			with rounding set to chop.
 *
 ******************************************************************************/
int32_t fpu_toLong(
	fpu_Real value
) {
	if (value.mantissa == 0 || value.exponent < 0) {
		return 0;
	}

	if (value.exponent > 30) {
		return INT32_MIN;
	}

	int32_t magnitude = (int32_t)(value.mantissa >> (63 - value.exponent));

	return value.negative ? -magnitude : magnitude;
}

/** ****************************************************************************
 *
 * \brief	The number 0.
 *
 * \return	0.
 *
 ******************************************************************************/
static fpu_Real fpu__zero(void) {
	fpu_Real result = { 0, 0, false };

	return result;
}

/** ****************************************************************************
 *
 * \brief	Round a 128-bit mantissa to 64 bits, to nearest or even.
 *
 * \param	high			Upper 64 bits; bit 63 set.
 * \param	low				Lower 64 bits.
 * \param	exponent		Power of 2 of bit 63 of high.
 * \param	negative		Sign of the result.
 *
 * \return	The rounded number.
 *
 ******************************************************************************/
static fpu_Real fpu__round(
	uint64_t high,
	uint64_t low,
	int32_t	 exponent,
	bool	 negative
) {
	if (low > FPU_TOP_BIT || (low == FPU_TOP_BIT && (high & 1) != 0)) {
		high++;

		if (high == 0) {
			high = FPU_TOP_BIT;
			exponent++;
		}
	}

	fpu_Real result = { high, exponent, negative };

	return result;
}

/** ****************************************************************************
 *
 * \brief	Multiply two 64-bit numbers into 128 bits.
 *
 * \param	left			First number.
 * \param	right			Second number.
 * \param	pHigh			Out: upper 64 bits of the product.
 * \param	pLow			Out: lower 64 bits of the product.
 *
 ******************************************************************************/
static void fpu__multiply(
	uint64_t   left,
	uint64_t   right,
	uint64_t * pHigh,
	uint64_t * pLow
) {
	uint64_t leftLow = left & FPU_LOW_HALF;
	uint64_t leftHigh = left >> 32;
	uint64_t rightLow = right & FPU_LOW_HALF;
	uint64_t rightHigh = right >> 32;
	uint64_t lowLow = leftLow * rightLow;
	uint64_t lowHigh = leftLow * rightHigh;
	uint64_t highLow = leftHigh * rightLow;
	uint64_t highHigh = leftHigh * rightHigh;
	uint64_t middle = (lowLow >> 32) + (lowHigh & FPU_LOW_HALF) +
		(highLow & FPU_LOW_HALF);

	*pLow = (middle << 32) | (lowLow & FPU_LOW_HALF);
	*pHigh = highHigh + (lowHigh >> 32) + (highLow >> 32) + (middle >> 32);
}
