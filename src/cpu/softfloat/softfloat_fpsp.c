
/*============================================================================

 This C source file is an extension to the SoftFloat IEC/IEEE Floating-point
 Arithmetic Package, Release 2a.

 Written by Andreas Grabher for Previous, NeXT Computer Emulator.
 
=============================================================================*/

#include <stdint.h>
#include <stdlib.h>

#include "softfloat.h"
#include "softfloat-specialize.h"
#include "softfloat_fpsp_tables.h"


/*----------------------------------------------------------------------------
| Algorithms for transcendental functions supported by MC68881 and MC68882 
| mathematical coprocessors. The functions are derived from FPSP library.
*----------------------------------------------------------------------------*/

#define pi_sig      LIT64(0xc90fdaa22168c235)
#define pi_sig0     LIT64(0xc90fdaa22168c234)
#define pi_sig1     LIT64(0xc4c6628b80dc1cd1)

#define pi_exp      0x4000
#define piby2_exp   0x3FFF
#define piby4_exp   0x3FFE

#define one_exp     0x3FFF
#define one_sig     LIT64(0x8000000000000000)

#define SET_PREC \
	int8_t user_rnd_mode, user_rnd_prec; \
	user_rnd_mode = status->float_rounding_mode; \
	user_rnd_prec = status->floatx80_rounding_precision; \
	status->float_rounding_mode = float_round_nearest_even; \
	status->floatx80_rounding_precision = 80

#define RESET_PREC \
	status->float_rounding_mode = user_rnd_mode; \
	status->floatx80_rounding_precision = user_rnd_prec

/*----------------------------------------------------------------------------
 | Function for compactifying extended double-precision floating point values.
 *----------------------------------------------------------------------------*/

static int32_t floatx80_make_compact(int32_t aExp, uint64_t aSig)
{
	return (aExp<<16)|(aSig>>48);
}


/*----------------------------------------------------------------------------
 | Arc cosine
 *----------------------------------------------------------------------------*/

floatx80 floatx80_acos(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	
	int32_t compact;
	floatx80 fp0, fp1, one;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF && (uint64_t) (aSig<<1)) {
		return propagateFloatx80NaNOneArg(a, status);
	}
	if (aExp == 0 && aSig == 0) {
		float_raise(float_flag_inexact, status);
		return roundAndPackFloatx80(status->floatx80_rounding_precision, 0, piby2_exp, pi_sig, 0, status);
	}

	compact = floatx80_make_compact(aExp, aSig);
	
	if (compact >= 0x3FFF8000) { // |X| >= 1
		if (aExp == one_exp && aSig == one_sig) { // |X| == 1
			if (aSign) { // X == -1
				a = packFloatx80(0, pi_exp, pi_sig);
				float_raise(float_flag_inexact, status);
				return floatx80_move(a, status);
			} else { // X == +1
				return packFloatx80(0, 0, 0);
			}
		} else { // |X| > 1
			float_raise(float_flag_invalid, status);
			return floatx80_default_nan(status);
		}
	} // |X| < 1
	
	SET_PREC;
	
	one = packFloatx80(0, one_exp, one_sig);
	fp0 = a;
	
	fp1 = floatx80_add(one, fp0, status);   // 1 + X
	fp0 = floatx80_sub(one, fp0, status);   // 1 - X
	fp0 = floatx80_div(fp0, fp1, status);   // (1-X)/(1+X)
	fp0 = floatx80_sqrt(fp0, status);       // SQRT((1-X)/(1+X))
	fp0 = floatx80_atan(fp0, status);       // ATAN(SQRT((1-X)/(1+X)))
	
	RESET_PREC;
	
	a = floatx80_add(fp0, fp0, status);     // 2 * ATAN(SQRT((1-X)/(1+X)))
	
	float_raise(float_flag_inexact, status);
	
	return a;
}

/*----------------------------------------------------------------------------
 | Arc sine
 *----------------------------------------------------------------------------*/

floatx80 floatx80_asin(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	int32_t compact;
	floatx80 fp0, fp1, fp2, one;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF && (uint64_t) (aSig<<1)) {
		return propagateFloatx80NaNOneArg(a, status);
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(aSign, 0, 0);
	}

	compact = floatx80_make_compact(aExp, aSig);
	
	if (compact >= 0x3FFF8000) { // |X| >= 1
		if (aExp == one_exp && aSig == one_sig) { // |X| == 1
			float_raise(float_flag_inexact, status);
			a = packFloatx80(aSign, piby2_exp, pi_sig);
			return floatx80_move(a, status);
		} else { // |X| > 1
			float_raise(float_flag_invalid, status);
			return floatx80_default_nan(status);
		}

	} // |X| < 1
	
	SET_PREC;
	
	one = packFloatx80(0, one_exp, one_sig);
	fp0 = a;

	fp1 = floatx80_sub(one, fp0, status);   // 1 - X
	fp2 = floatx80_add(one, fp0, status);   // 1 + X
	fp1 = floatx80_mul(fp2, fp1, status);   // (1+X)*(1-X)
	fp1 = floatx80_sqrt(fp1, status);       // SQRT((1+X)*(1-X))
	fp0 = floatx80_div(fp0, fp1, status);   // X/SQRT((1+X)*(1-X))

	RESET_PREC;

	a = floatx80_atan(fp0, status);         // ATAN(X/SQRT((1+X)*(1-X)))
	
	float_raise(float_flag_inexact, status);

	return a;
}

/*----------------------------------------------------------------------------
 | Arc tangent
 *----------------------------------------------------------------------------*/

floatx80 floatx80_atan(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	int32_t compact, tbl_index;
	floatx80 fp0, fp1, fp2, fp3, xsave;

	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		a = packFloatx80(aSign, piby2_exp, pi_sig);
		float_raise(float_flag_inexact, status);
		return floatx80_move(a, status);
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(aSign, 0, 0);
	}
	
	compact = floatx80_make_compact(aExp, aSig);
	
	SET_PREC;

	if (compact < 0x3FFB8000 || compact > 0x4002FFFF) { // |X| >= 16 or |X| < 1/16
		if (compact > 0x3FFF8000) { // |X| >= 16
			if (compact > 0x40638000) { // |X| > 2^(100)
				fp0 = packFloatx80(aSign, piby2_exp, pi_sig);
				fp1 = packFloatx80(aSign, 0x0001, one_sig);
				
				RESET_PREC;
				
				a = floatx80_sub(fp0, fp1, status);
				
				float_raise(float_flag_inexact, status);
				
				return a;
			} else {
				fp0 = a;
				fp1 = packFloatx80(1, one_exp, one_sig); // -1
				fp1 = floatx80_div(fp1, fp0, status); // X' = -1/X
				xsave = fp1;
				fp0 = floatx80_mul(fp1, fp1, status); // Y = X'*X'
				fp1 = floatx80_mul(fp0, fp0, status); // Z = Y*Y
				fp3 = float64_to_floatx80(LIT64(0xBFB70BF398539E6A), status); // C5
				fp2 = float64_to_floatx80(LIT64(0x3FBC7187962D1D7D), status); // C4
				fp3 = floatx80_mul(fp3, fp1, status); // Z*C5
				fp2 = floatx80_mul(fp2, fp1, status); // Z*C4
				fp3 = floatx80_add(fp3, float64_to_floatx80(LIT64(0xBFC24924827107B8), status), status); // C3+Z*C5
				fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3FC999999996263E), status), status); // C2+Z*C4
				fp1 = floatx80_mul(fp1, fp3, status); // Z*(C3+Z*C5)
				fp2 = floatx80_mul(fp2, fp0, status); // Y*(C2+Z*C4)
				fp1 = floatx80_add(fp1, float64_to_floatx80(LIT64(0xBFD5555555555536), status), status); // C1+Z*(C3+Z*C5)
				fp0 = floatx80_mul(fp0, xsave, status); // X'*Y
				fp1 = floatx80_add(fp1, fp2, status); // [Y*(C2+Z*C4)]+[C1+Z*(C3+Z*C5)]
				fp0 = floatx80_mul(fp0, fp1, status); // X'*Y*([B1+Z*(B3+Z*B5)]+[Y*(B2+Z*(B4+Z*B6))]) ??
				fp0 = floatx80_add(fp0, xsave, status);
				fp1 = packFloatx80(aSign, piby2_exp, pi_sig);
				
				RESET_PREC;

				a = floatx80_add(fp0, fp1, status);
				
				float_raise(float_flag_inexact, status);
				
				return a;
			}
		} else { // |X| < 1/16
			if (compact < 0x3FD78000) { // |X| < 2^(-40)
				RESET_PREC;
				
				a = floatx80_move(a, status);
				
				float_raise(float_flag_inexact, status);
				
				return a;
			} else {
				fp0 = a;
				xsave = a;
				fp0 = floatx80_mul(fp0, fp0, status); // Y = X*X
				fp1 = floatx80_mul(fp0, fp0, status); // Z = Y*Y
				fp2 = float64_to_floatx80(LIT64(0x3FB344447F876989), status); // B6
				fp3 = float64_to_floatx80(LIT64(0xBFB744EE7FAF45DB), status); // B5
				fp2 = floatx80_mul(fp2, fp1, status); // Z*B6
				fp3 = floatx80_mul(fp3, fp1, status); // Z*B5
				fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3FBC71C646940220), status), status); // B4+Z*B6
				fp3 = floatx80_add(fp3, float64_to_floatx80(LIT64(0xBFC24924921872F9), status), status); // B3+Z*B5
				fp2 = floatx80_mul(fp2, fp1, status); // Z*(B4+Z*B6)
				fp1 = floatx80_mul(fp1, fp3, status); // Z*(B3+Z*B5)
				fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3FC9999999998FA9), status), status); // B2+Z*(B4+Z*B6)
				fp1 = floatx80_add(fp1, float64_to_floatx80(LIT64(0xBFD5555555555555), status), status); // B1+Z*(B3+Z*B5)
				fp2 = floatx80_mul(fp2, fp0, status); // Y*(B2+Z*(B4+Z*B6))
				fp0 = floatx80_mul(fp0, xsave, status); // X*Y
				fp1 = floatx80_add(fp1, fp2, status); // [B1+Z*(B3+Z*B5)]+[Y*(B2+Z*(B4+Z*B6))]
				fp0 = floatx80_mul(fp0, fp1, status); // X*Y*([B1+Z*(B3+Z*B5)]+[Y*(B2+Z*(B4+Z*B6))])
				
				RESET_PREC;
				
				a = floatx80_add(fp0, xsave, status);
				
				float_raise(float_flag_inexact, status);
				
				return a;
			}
		}
	} else {
		aSig &= LIT64(0xF800000000000000);
		aSig |= LIT64(0x0400000000000000);
		xsave = packFloatx80(aSign, aExp, aSig); // F
		fp0 = a;
		fp1 = a; // X
		fp2 = packFloatx80(0, one_exp, one_sig); // 1
		fp1 = floatx80_mul(fp1, xsave, status); // X*F
		fp0 = floatx80_sub(fp0, xsave, status); // X-F
		fp1 = floatx80_add(fp1, fp2, status); // 1 + X*F
		fp0 = floatx80_div(fp0, fp1, status); // U = (X-F)/(1+X*F)
		
		tbl_index = compact;
		
		tbl_index &= 0x7FFF0000;
		tbl_index -= 0x3FFB0000;
		tbl_index >>= 1;
		tbl_index += compact&0x00007800;
		tbl_index >>= 11;
		
		fp3 = atan_tbl[tbl_index];
		
		fp3.high |= aSign ? 0x8000 : 0; // ATAN(F)
		
		fp1 = floatx80_mul(fp0, fp0, status); // V = U*U
		fp2 = float64_to_floatx80(LIT64(0xBFF6687E314987D8), status); // A3
		fp2 = floatx80_add(fp2, fp1, status); // A3+V
		fp2 = floatx80_mul(fp2, fp1, status); // V*(A3+V)
		fp1 = floatx80_mul(fp1, fp0, status); // U*V
		fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x4002AC6934A26DB3), status), status); // A2+V*(A3+V)
		fp1 = floatx80_mul(fp1, float64_to_floatx80(LIT64(0xBFC2476F4E1DA28E), status), status); // A1+U*V
		fp1 = floatx80_mul(fp1, fp2, status); // A1*U*V*(A2+V*(A3+V))
		fp0 = floatx80_add(fp0, fp1, status); // ATAN(U)
		
		RESET_PREC;
		
		a = floatx80_add(fp0, fp3, status); // ATAN(X)
		
		float_raise(float_flag_inexact, status);
		
		return a;
	}
}

/*----------------------------------------------------------------------------
 | Hyperbolic arc tangent
 *----------------------------------------------------------------------------*/

floatx80 floatx80_atanh(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	int32_t compact;
	floatx80 fp0, fp1, fp2, one;

	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF && (uint64_t) (aSig<<1)) {
		return propagateFloatx80NaNOneArg(a, status);
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(aSign, 0, 0);
	}
	
	compact = floatx80_make_compact(aExp, aSig);
	
	if (compact >= 0x3FFF8000) { // |X| >= 1
		if (aExp == one_exp && aSig == one_sig) { // |X| == 1
			float_raise(float_flag_divbyzero, status);
			return packFloatx80(aSign, 0x7FFF, floatx80_default_infinity_low);
		} else { // |X| > 1
			float_raise(float_flag_invalid, status);
			return floatx80_default_nan(status);
		}
	} // |X| < 1
	
	SET_PREC;
	
	one = packFloatx80(0, one_exp, one_sig);
	fp2 = packFloatx80(aSign, 0x3FFE, one_sig); // SIGN(X) * (1/2)
	fp0 = packFloatx80(0, aExp, aSig); // Y = |X|
	fp1 = packFloatx80(1, aExp, aSig); // -Y
	fp0 = floatx80_add(fp0, fp0, status); // 2Y
	fp1 = floatx80_add(fp1, one, status); // 1-Y
	fp0 = floatx80_div(fp0, fp1, status); // Z = 2Y/(1-Y)
	fp0 = floatx80_lognp1(fp0, status); // LOG1P(Z)
	
	RESET_PREC;
	
	a = floatx80_mul(fp0, fp2, status); // ATANH(X) = SIGN(X) * (1/2) * LOG1P(Z)
	
	float_raise(float_flag_inexact, status);
	
	return a;
}

/*----------------------------------------------------------------------------
 | Cosine
 *----------------------------------------------------------------------------*/

floatx80 floatx80_cos(floatx80 a, float_status *status)
{
	flag aSign, xSign;
	int32_t aExp, xExp;
	uint64_t aSig, xSig;
	
	int32_t compact, l, n, j;
	floatx80 fp0, fp1, fp2, fp3, fp4, fp5, x, invtwopi, twopi1, twopi2;
	float32 posneg1, twoto63;
	flag adjn, endflag;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		float_raise(float_flag_invalid, status);
		return floatx80_default_nan(status);
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(0, one_exp, one_sig);
	}
	
	adjn = 1;
	
	SET_PREC;
	
	compact = floatx80_make_compact(aExp, aSig);
	
	fp0 = a;
	
	if (compact < 0x3FD78000 || compact > 0x4004BC7E) { // 2^(-40) > |X| > 15 PI
		if (compact > 0x3FFF8000) { // |X| >= 15 PI
			// REDUCEX
			fp1 = packFloatx80(0, 0, 0);
			if (compact == 0x7FFEFFFF) {
				twopi1 = packFloatx80(aSign ^ 1, 0x7FFE, LIT64(0xC90FDAA200000000));
				twopi2 = packFloatx80(aSign ^ 1, 0x7FDC, LIT64(0x85A308D300000000));
				fp0 = floatx80_add(fp0, twopi1, status);
				fp1 = fp0;
				fp0 = floatx80_add(fp0, twopi2, status);
				fp1 = floatx80_sub(fp1, fp0, status);
				fp1 = floatx80_add(fp1, twopi2, status);
			}
		loop:
			xSign = extractFloatx80Sign(fp0);
			xExp = extractFloatx80Exp(fp0);
			xExp -= 0x3FFF;
			if (xExp <= 28) {
				l = 0;
				endflag = 1;
			} else {
				l = xExp - 27;
				endflag = 0;
			}
			invtwopi = packFloatx80(0, 0x3FFE - l, LIT64(0xA2F9836E4E44152A)); // INVTWOPI
			twopi1 = packFloatx80(0, 0x3FFF + l, LIT64(0xC90FDAA200000000));
			twopi2 = packFloatx80(0, 0x3FDD + l, LIT64(0x85A308D300000000));
			
			twoto63 = 0x5F000000;
			twoto63 |= xSign ? 0x80000000 : 0x00000000; // SIGN(INARG)*2^63 IN SGL
			
			fp2 = floatx80_mul(fp0, invtwopi, status);
			fp2 = floatx80_add(fp2, float32_to_floatx80(twoto63, status), status); // THE FRACTIONAL PART OF FP2 IS ROUNDED
			fp2 = floatx80_sub(fp2, float32_to_floatx80(twoto63, status), status); // FP2 is N
			fp4 = floatx80_mul(twopi1, fp2, status); // W = N*P1
			fp5 = floatx80_mul(twopi2, fp2, status); // w = N*P2
			fp3 = floatx80_add(fp4, fp5, status); // FP3 is P
			fp4 = floatx80_sub(fp4, fp3, status); // W-P
			fp0 = floatx80_sub(fp0, fp3, status); // FP0 is A := R - P
			fp4 = floatx80_add(fp4, fp5, status); // FP4 is p = (W-P)+w
			fp3 = fp0; // FP3 is A
			fp1 = floatx80_sub(fp1, fp4, status); // FP1 is a := r - p
			fp0 = floatx80_add(fp0, fp1, status); // FP0 is R := A+a
			
			if (endflag > 0) {
				n = floatx80_to_int32(fp2, status);
				goto sincont;
			}
			fp3 = floatx80_sub(fp3, fp0, status); // A-R
			fp1 = floatx80_add(fp1, fp3, status); // FP1 is r := (A-R)+a
			goto loop;
		} else {
			// SINSM
			fp0 = float32_to_floatx80(0x3F800000, status); // 1
			
			RESET_PREC;
			
			if (adjn) {
				// COSTINY
				a = floatx80_sub(fp0, float32_to_floatx80(0x00800000, status), status);
			} else {
				// SINTINY
				a = floatx80_move(a, status);
			}
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	} else {
		fp1 = floatx80_mul(fp0, float64_to_floatx80(LIT64(0x3FE45F306DC9C883), status), status); // X*2/PI
		
		n = floatx80_to_int32(fp1, status);
		j = 32 + n;
		
		fp0 = floatx80_sub(fp0, pi_tbl[j], status); // X-Y1
		fp0 = floatx80_sub(fp0, float32_to_floatx80(pi_tbl2[j], status), status); // FP0 IS R = (X-Y1)-Y2
		
	sincont:
		if ((n + adjn) & 1) {
			// COSPOLY
			fp0 = floatx80_mul(fp0, fp0, status); // FP0 IS S
			fp1 = floatx80_mul(fp0, fp0, status); // FP1 IS T
			fp2 = float64_to_floatx80(LIT64(0x3D2AC4D0D6011EE3), status); // B8
			fp3 = float64_to_floatx80(LIT64(0xBDA9396F9F45AC19), status); // B7
			
			xSign = extractFloatx80Sign(fp0); // X IS S
			xExp = extractFloatx80Exp(fp0);
			xSig = extractFloatx80Frac(fp0);
			
			if (((n + adjn) >> 1) & 1) {
				xSign ^= 1;
				posneg1 = 0xBF800000; // -1
			} else {
				xSign ^= 0;
				posneg1 = 0x3F800000; // 1
			} // X IS NOW R'= SGN*R
			
			fp2 = floatx80_mul(fp2, fp1, status); // TB8
			fp3 = floatx80_mul(fp3, fp1, status); // TB7
			fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3E21EED90612C972), status), status); // B6+TB8
			fp3 = floatx80_add(fp3, float64_to_floatx80(LIT64(0xBE927E4FB79D9FCF), status), status); // B5+TB7
			fp2 = floatx80_mul(fp2, fp1, status); // T(B6+TB8)
			fp3 = floatx80_mul(fp3, fp1, status); // T(B5+TB7)
			fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3EFA01A01A01D423), status), status); // B4+T(B6+TB8)
			fp4 = packFloatx80(1, 0x3FF5, LIT64(0xB60B60B60B61D438));
			fp3 = floatx80_add(fp3, fp4, status); // B3+T(B5+TB7)
			fp2 = floatx80_mul(fp2, fp1, status); // T(B4+T(B6+TB8))
			fp1 = floatx80_mul(fp1, fp3, status); // T(B3+T(B5+TB7))
			fp4 = packFloatx80(0, 0x3FFA, LIT64(0xAAAAAAAAAAAAAB5E));
			fp2 = floatx80_add(fp2, fp4, status); // B2+T(B4+T(B6+TB8))
			fp1 = floatx80_add(fp1, float32_to_floatx80(0xBF000000, status), status); // B1+T(B3+T(B5+TB7))
			fp0 = floatx80_mul(fp0, fp2, status); // S(B2+T(B4+T(B6+TB8)))
			fp0 = floatx80_add(fp0, fp1, status); // [B1+T(B3+T(B5+TB7))]+[S(B2+T(B4+T(B6+TB8)))]
			
			x = packFloatx80(xSign, xExp, xSig);
			fp0 = floatx80_mul(fp0, x, status);
			
			RESET_PREC;
			
			a = floatx80_add(fp0, float32_to_floatx80(posneg1, status), status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		} else {
			// SINPOLY
			xSign = extractFloatx80Sign(fp0); // X IS R
			xExp = extractFloatx80Exp(fp0);
			xSig = extractFloatx80Frac(fp0);
			
			xSign ^= ((n + adjn) >> 1) & 1; // X IS NOW R'= SGN*R
			
			fp0 = floatx80_mul(fp0, fp0, status); // FP0 IS S
			fp1 = floatx80_mul(fp0, fp0, status); // FP1 IS T
			fp3 = float64_to_floatx80(LIT64(0xBD6AAA77CCC994F5), status); // A7
			fp2 = float64_to_floatx80(LIT64(0x3DE612097AAE8DA1), status); // A6
			fp3 = floatx80_mul(fp3, fp1, status); // T*A7
			fp2 = floatx80_mul(fp2, fp1, status); // T*A6
			fp3 = floatx80_add(fp3, float64_to_floatx80(LIT64(0xBE5AE6452A118AE4), status), status); // A5+T*A7
			fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3EC71DE3A5341531), status), status); // A4+T*A6
			fp3 = floatx80_mul(fp3, fp1, status); // T(A5+TA7)
			fp2 = floatx80_mul(fp2, fp1, status); // T(A4+TA6)
			fp3 = floatx80_add(fp3, float64_to_floatx80(LIT64(0xBF2A01A01A018B59), status), status); // A3+T(A5+TA7)
			fp4 = packFloatx80(0, 0x3FF8, LIT64(0x88888888888859AF));
			fp2 = floatx80_add(fp2, fp4, status); // A2+T(A4+TA6)
			fp1 = floatx80_mul(fp1, fp3, status); // T(A3+T(A5+TA7))
			fp2 = floatx80_mul(fp2, fp0, status); // S(A2+T(A4+TA6))
			fp4 = packFloatx80(1, 0x3FFC, LIT64(0xAAAAAAAAAAAAAA99));
			fp1 = floatx80_add(fp1, fp4, status); // A1+T(A3+T(A5+TA7))
			fp1 = floatx80_add(fp1, fp2, status); // [A1+T(A3+T(A5+TA7))]+[S(A2+T(A4+TA6))]
			
			x = packFloatx80(xSign, xExp, xSig);
			fp0 = floatx80_mul(fp0, x, status); // R'*S
			fp0 = floatx80_mul(fp0, fp1, status); // SIN(R')-R'
			
			RESET_PREC;
			
			a = floatx80_add(fp0, x, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	}
}

/*----------------------------------------------------------------------------
 | Hyperbolic cosine
 *----------------------------------------------------------------------------*/

floatx80 floatx80_cosh(floatx80 a, float_status *status)
{
	int32_t aExp;
	uint64_t aSig;
	
	int32_t compact;
	floatx80 fp0, fp1;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		return packFloatx80(0, aExp, aSig);
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(0, one_exp, one_sig);
	}
	
	SET_PREC;
	
	compact = floatx80_make_compact(aExp, aSig);
	
	if (compact > 0x400CB167) {
		if (compact > 0x400CB2B3) {
			RESET_PREC;
			a =  roundAndPackFloatx80(status->floatx80_rounding_precision, 0, 0x8000, one_sig, 0, status);
			float_raise(float_flag_inexact, status);
			return a;
		} else {
			fp0 = packFloatx80(0, aExp, aSig);
			fp0 = floatx80_sub(fp0, float64_to_floatx80(LIT64(0x40C62D38D3D64634), status), status);
			fp0 = floatx80_sub(fp0, float64_to_floatx80(LIT64(0x3D6F90AEB1E75CC7), status), status);
			fp0 = floatx80_etox(fp0, status);
			fp1 = packFloatx80(0, 0x7FFB, one_sig);
			
			RESET_PREC;

			a = floatx80_mul(fp0, fp1, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	}
	
	fp0 = packFloatx80(0, aExp, aSig); // |X|
	fp0 = floatx80_etox(fp0, status); // EXP(|X|)
	fp0 = floatx80_mul(fp0, float32_to_floatx80(0x3F000000, status), status); // (1/2)*EXP(|X|)
	fp1 = float32_to_floatx80(0x3E800000, status); // 1/4
	fp1 = floatx80_div(fp1, fp0, status); // 1/(2*EXP(|X|))
	
	RESET_PREC;
	
	a = floatx80_add(fp0, fp1, status);
	
	float_raise(float_flag_inexact, status);
	
	return a;
}

/*----------------------------------------------------------------------------
 | e to x
 *----------------------------------------------------------------------------*/

floatx80 floatx80_etox(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	int32_t compact, n, j, k, m, m1;
	floatx80 fp0, fp1, fp2, fp3, l2, scale, adjscale;
	flag adjflag;

	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		if (aSign) return packFloatx80(0, 0, 0);
		return a;
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(0, one_exp, one_sig);
	}
	
	SET_PREC;
	
	if (aExp >= 0x3FBE) { // |X| >= 2^(-65)
		compact = floatx80_make_compact(aExp, aSig);
		
		if (compact < 0x400CB167) { // |X| < 16380 log2
			fp0 = a;
			fp1 = a;
			fp0 = floatx80_mul(fp0, float32_to_floatx80(0x42B8AA3B, status), status); // 64/log2 * X
			adjflag = 0;
			n = floatx80_to_int32(fp0, status); // int(64/log2*X)
			fp0 = int32_to_floatx80(n);
			
			j = n & 0x3F; // J = N mod 64
			m = n / 64; // NOTE: this is really arithmetic right shift by 6
			if (n < 0 && j) { // arithmetic right shift is division and round towards minus infinity
				m--;
			}
			m += 0x3FFF; // biased exponent of 2^(M)
			
		expcont1:
			fp2 = fp0; // N
			fp0 = floatx80_mul(fp0, float32_to_floatx80(0xBC317218, status), status); // N * L1, L1 = lead(-log2/64)
			l2 = packFloatx80(0, 0x3FDC, LIT64(0x82E308654361C4C6));
			fp2 = floatx80_mul(fp2, l2, status); // N * L2, L1+L2 = -log2/64
			fp0 = floatx80_add(fp0, fp1, status); // X + N*L1
			fp0 = floatx80_add(fp0, fp2, status); // R
			
			fp1 = floatx80_mul(fp0, fp0, status); // S = R*R
			fp2 = float32_to_floatx80(0x3AB60B70, status); // A5
			fp2 = floatx80_mul(fp2, fp1, status); // fp2 is S*A5
			fp3 = floatx80_mul(float32_to_floatx80(0x3C088895, status), fp1, status); // fp3 is S*A4
			fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3FA5555555554431), status), status); // fp2 is A3+S*A5
			fp3 = floatx80_add(fp3, float64_to_floatx80(LIT64(0x3FC5555555554018), status), status); // fp3 is A2+S*A4
			fp2 = floatx80_mul(fp2, fp1, status); // fp2 is S*(A3+S*A5)
			fp3 = floatx80_mul(fp3, fp1, status); // fp3 is S*(A2+S*A4)
			fp2 = floatx80_add(fp2, float32_to_floatx80(0x3F000000, status), status); // fp2 is A1+S*(A3+S*A5)
			fp3 = floatx80_mul(fp3, fp0, status); // fp3 IS R*S*(A2+S*A4)
			fp2 = floatx80_mul(fp2, fp1, status); // fp2 IS S*(A1+S*(A3+S*A5))
			fp0 = floatx80_add(fp0, fp3, status); // fp0 IS R+R*S*(A2+S*A4)
			fp0 = floatx80_add(fp0, fp2, status); // fp0 IS EXP(R) - 1
			
			fp1 = exp_tbl[j];
			fp0 = floatx80_mul(fp0, fp1, status); // 2^(J/64)*(Exp(R)-1)
			fp0 = floatx80_add(fp0, float32_to_floatx80(exp_tbl2[j], status), status); // accurate 2^(J/64)
			fp0 = floatx80_add(fp0, fp1, status); // 2^(J/64) + 2^(J/64)*(Exp(R)-1)
			
			scale = packFloatx80(0, m, one_sig);
			if (adjflag) {
				adjscale = packFloatx80(0, m1, one_sig);
				fp0 = floatx80_mul(fp0, adjscale, status);
			}
			
			RESET_PREC;
			
			a = floatx80_mul(fp0, scale, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		} else { // |X| >= 16380 log2
			if (compact > 0x400CB27C) { // |X| >= 16480 log2
				RESET_PREC;
				if (aSign) {
					a = roundAndPackFloatx80(status->floatx80_rounding_precision, 0, -0x1000, aSig, 0, status);
				} else {
					a = roundAndPackFloatx80(status->floatx80_rounding_precision, 0, 0x8000, aSig, 0, status);
				}
				float_raise(float_flag_inexact, status);
				
				return a;
			} else {
				fp0 = a;
				fp1 = a;
				fp0 = floatx80_mul(fp0, float32_to_floatx80(0x42B8AA3B, status), status); // 64/log2 * X
				adjflag = 1;
				n = floatx80_to_int32(fp0, status); // int(64/log2*X)
				fp0 = int32_to_floatx80(n);
				
				j = n & 0x3F; // J = N mod 64
				k = n / 64; // NOTE: this is really arithmetic right shift by 6
				if (n < 0 && j) { // arithmetic right shift is division and round towards minus infinity
					k--;
				}
				m1 = k / 2; // NOTE: this is really arithmetic right shift by 1
				if (k < 0 && (k & 1)) { // arithmetic right shift is division and round towards minus infinity
					m1--;
				}
				m = k - m1;
				m1 += 0x3FFF; // biased exponent of 2^(M1)
				m += 0x3FFF; // biased exponent of 2^(M)
				
				goto expcont1;
			}
		}
	} else { // |X| < 2^(-65)
		RESET_PREC;
		
		a = floatx80_add(a, float32_to_floatx80(0x3F800000, status), status); // 1 + X
		
		float_raise(float_flag_inexact, status);
		
		return a;
	}
}

/*----------------------------------------------------------------------------
 | e to x minus 1
 *----------------------------------------------------------------------------*/

floatx80 floatx80_etoxm1(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	int32_t compact, n, j, m, m1;
	floatx80 fp0, fp1, fp2, fp3, l2, sc, onebysc;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		if (aSign) return packFloatx80(aSign, one_exp, one_sig);
		return a;
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(aSign, 0, 0);
	}
	
	SET_PREC;
	
	if (aExp >= 0x3FFD) { // |X| >= 1/4
		compact = floatx80_make_compact(aExp, aSig);
		
		if (compact <= 0x4004C215) { // |X| <= 70 log2
			fp0 = a;
			fp1 = a;
			fp0 = floatx80_mul(fp0, float32_to_floatx80(0x42B8AA3B, status), status); // 64/log2 * X
			n = floatx80_to_int32(fp0, status); // int(64/log2*X)
			fp0 = int32_to_floatx80(n);
			
			j = n & 0x3F; // J = N mod 64
			m = n / 64; // NOTE: this is really arithmetic right shift by 6
			if (n < 0 && j) { // arithmetic right shift is division and round towards minus infinity
				m--;
			}
			m1 = -m;
			//m += 0x3FFF; // biased exponent of 2^(M)
			//m1 += 0x3FFF; // biased exponent of -2^(-M)
			
			fp2 = fp0; // N
			fp0 = floatx80_mul(fp0, float32_to_floatx80(0xBC317218, status), status); // N * L1, L1 = lead(-log2/64)
			l2 = packFloatx80(0, 0x3FDC, LIT64(0x82E308654361C4C6));
			fp2 = floatx80_mul(fp2, l2, status); // N * L2, L1+L2 = -log2/64
			fp0 = floatx80_add(fp0, fp1, status); // X + N*L1
			fp0 = floatx80_add(fp0, fp2, status); // R
			
			fp1 = floatx80_mul(fp0, fp0, status); // S = R*R
			fp2 = float32_to_floatx80(0x3950097B, status); // A6
			fp2 = floatx80_mul(fp2, fp1, status); // fp2 is S*A6
			fp3 = floatx80_mul(float32_to_floatx80(0x3AB60B6A, status), fp1, status); // fp3 is S*A5
			fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3F81111111174385), status), status); // fp2 IS A4+S*A6
			fp3 = floatx80_add(fp3, float64_to_floatx80(LIT64(0x3FA5555555554F5A), status), status); // fp3 is A3+S*A5
			fp2 = floatx80_mul(fp2, fp1, status); // fp2 IS S*(A4+S*A6)
			fp3 = floatx80_mul(fp3, fp1, status); // fp3 IS S*(A3+S*A5)
			fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3FC5555555555555), status), status); // fp2 IS A2+S*(A4+S*A6)
			fp3 = floatx80_add(fp3, float32_to_floatx80(0x3F000000, status), status); // fp3 IS A1+S*(A3+S*A5)
			fp2 = floatx80_mul(fp2, fp1, status); // fp2 IS S*(A2+S*(A4+S*A6))
			fp1 = floatx80_mul(fp1, fp3, status); // fp1 IS S*(A1+S*(A3+S*A5))
			fp2 = floatx80_mul(fp2, fp0, status); // fp2 IS R*S*(A2+S*(A4+S*A6))
			fp0 = floatx80_add(fp0, fp1, status); // fp0 IS R+S*(A1+S*(A3+S*A5))
			fp0 = floatx80_add(fp0, fp2, status); // fp0 IS EXP(R) - 1
			
			fp0 = floatx80_mul(fp0, exp_tbl[j], status); // 2^(J/64)*(Exp(R)-1)
			
			if (m >= 64) {
				fp1 = float32_to_floatx80(exp_tbl2[j], status);
				onebysc = packFloatx80(1, m1 + 0x3FFF, one_sig); // -2^(-M)
				fp1 = floatx80_add(fp1, onebysc, status);
				fp0 = floatx80_add(fp0, fp1, status);
				fp0 = floatx80_add(fp0, exp_tbl[j], status);
			} else if (m < -3) {
				fp0 = floatx80_add(fp0, float32_to_floatx80(exp_tbl2[j], status), status);
				fp0 = floatx80_add(fp0, exp_tbl[j], status);
				onebysc = packFloatx80(1, m1 + 0x3FFF, one_sig); // -2^(-M)
				fp0 = floatx80_add(fp0, onebysc, status);
			} else { // -3 <= m <= 63
				fp1 = exp_tbl[j];
				fp0 = floatx80_add(fp0, float32_to_floatx80(exp_tbl2[j], status), status);
				onebysc = packFloatx80(1, m1 + 0x3FFF, one_sig); // -2^(-M)
				fp1 = floatx80_add(fp1, onebysc, status);
				fp0 = floatx80_add(fp0, fp1, status);
			}
			
			sc = packFloatx80(0, m + 0x3FFF, one_sig);
			
			RESET_PREC;
			
			a = floatx80_mul(fp0, sc, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		} else { // |X| > 70 log2
			if (aSign) {
				fp0 = float32_to_floatx80(0xBF800000, status); // -1
				
				RESET_PREC;
				
				a = floatx80_add(fp0, float32_to_floatx80(0x00800000, status), status); // -1 + 2^(-126)
				
				float_raise(float_flag_inexact, status);
				
				return a;
			} else {
				RESET_PREC;
				
				return floatx80_etox(a, status);
			}
		}
	} else { // |X| < 1/4
		if (aExp >= 0x3FBE) {
			fp0 = a;
			fp0 = floatx80_mul(fp0, fp0, status); // S = X*X
			fp1 = float32_to_floatx80(0x2F30CAA8, status); // B12
			fp1 = floatx80_mul(fp1, fp0, status); // S * B12
			fp2 = float32_to_floatx80(0x310F8290, status); // B11
			fp1 = floatx80_add(fp1, float32_to_floatx80(0x32D73220, status), status); // B10
			fp2 = floatx80_mul(fp2, fp0, status);
			fp1 = floatx80_mul(fp1, fp0, status);
			fp2 = floatx80_add(fp2, float32_to_floatx80(0x3493F281, status), status); // B9
			fp1 = floatx80_add(fp1, float64_to_floatx80(LIT64(0x3EC71DE3A5774682), status), status); // B8
			fp2 = floatx80_mul(fp2, fp0, status);
			fp1 = floatx80_mul(fp1, fp0, status);
			fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3EFA01A019D7CB68), status), status); // B7
			fp1 = floatx80_add(fp1, float64_to_floatx80(LIT64(0x3F2A01A01A019DF3), status), status); // B6
			fp2 = floatx80_mul(fp2, fp0, status);
			fp1 = floatx80_mul(fp1, fp0, status);
			fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3F56C16C16C170E2), status), status); // B5
			fp1 = floatx80_add(fp1, float64_to_floatx80(LIT64(0x3F81111111111111), status), status); // B4
			fp2 = floatx80_mul(fp2, fp0, status);
			fp1 = floatx80_mul(fp1, fp0, status);
			fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3FA5555555555555), status), status); // B3
			fp3 = packFloatx80(0, 0x3FFC, LIT64(0xAAAAAAAAAAAAAAAB));
			fp1 = floatx80_add(fp1, fp3, status); // B2
			fp2 = floatx80_mul(fp2, fp0, status);
			fp1 = floatx80_mul(fp1, fp0, status);
			
			fp2 = floatx80_mul(fp2, fp0, status);
			fp1 = floatx80_mul(fp1, a, status);
			
			fp0 = floatx80_mul(fp0, float32_to_floatx80(0x3F000000, status), status); // S*B1
			fp1 = floatx80_add(fp1, fp2, status); // Q
			fp0 = floatx80_add(fp0, fp1, status); // S*B1+Q
			
			RESET_PREC;
			
			a = floatx80_add(fp0, a, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		} else { // |X| < 2^(-65)
			sc = packFloatx80(1, 1, one_sig);
			fp0 = a;

			if (aExp < 0x0033) { // |X| < 2^(-16382)
				fp0 = floatx80_mul(fp0, float64_to_floatx80(LIT64(0x48B0000000000000), status), status);
				fp0 = floatx80_add(fp0, sc, status);
				
				RESET_PREC;

				a = floatx80_mul(fp0, float64_to_floatx80(LIT64(0x3730000000000000), status), status);
			} else {
				RESET_PREC;
				
				a = floatx80_add(fp0, sc, status);
			}
			
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	}
}

/*----------------------------------------------------------------------------
 | Log base 10
 *----------------------------------------------------------------------------*/

floatx80 floatx80_log10(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	floatx80 fp0, fp1;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		if (aSign == 0)
			return a;
	}
	
	if (aExp == 0 && aSig == 0) {
		float_raise(float_flag_divbyzero, status);
		return packFloatx80(1, 0x7FFF, floatx80_default_infinity_low);
	}
	
	if (aSign) {
		float_raise(float_flag_invalid, status);
		return floatx80_default_nan(status);
	}
	
	SET_PREC;
	
	fp0 = floatx80_logn(a, status);
	fp1 = packFloatx80(0, 0x3FFD, LIT64(0xDE5BD8A937287195)); // INV_L10
	
	RESET_PREC;
	
	a = floatx80_mul(fp0, fp1, status); // LOGN(X)*INV_L10
	
	float_raise(float_flag_inexact, status);
	
	return a;
}

/*----------------------------------------------------------------------------
 | Log base 2
 *----------------------------------------------------------------------------*/

floatx80 floatx80_log2(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	floatx80 fp0, fp1;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		if (aSign == 0)
			return a;
	}
	
	if (aExp == 0) {
		if (aSig == 0) {
			float_raise(float_flag_divbyzero, status);
			return packFloatx80(1, 0x7FFF, floatx80_default_infinity_low);
		}
		normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
	}
	
	if (aSign) {
		float_raise(float_flag_invalid, status);
		return floatx80_default_nan(status);
	}
	
	SET_PREC;
	
	if (aSig == one_sig) { // X is 2^k
		RESET_PREC;
		
		a = int32_to_floatx80(aExp-0x3FFF);
	} else {
		fp0 = floatx80_logn(a, status);
		fp1 = packFloatx80(0, 0x3FFF, LIT64(0xB8AA3B295C17F0BC)); // INV_L2
		
		RESET_PREC;
		
		a = floatx80_mul(fp0, fp1, status); // LOGN(X)*INV_L2
	}
	
	float_raise(float_flag_inexact, status);
	
	return a;
}

/*----------------------------------------------------------------------------
 | Log base e
 *----------------------------------------------------------------------------*/

floatx80 floatx80_logn(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig, fSig;
	
	int32_t compact, j, k, adjk;
	floatx80 fp0, fp1, fp2, fp3, f, logof2, klog2, saveu;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		if (aSign == 0)
			return a;
	}
	
	adjk = 0;
	
	if (aExp == 0) {
		if (aSig == 0) { // zero
			float_raise(float_flag_divbyzero, status);
			return packFloatx80(1, 0x7FFF, floatx80_default_infinity_low);
		}
#if 1
		if ((aSig & one_sig) == 0) { // denormal
			normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
			adjk = -100;
			aExp += 100;
			a = packFloatx80(aSign, aExp, aSig);
		}
#else
		normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
#endif
	}
	
	if (aSign) {
		float_raise(float_flag_invalid, status);
		return floatx80_default_nan(status);
	}
	
	SET_PREC;
	
	compact = floatx80_make_compact(aExp, aSig);
	
	if (compact < 0x3FFEF07D || compact > 0x3FFF8841) { // |X| < 15/16 or |X| > 17/16
		k = aExp - 0x3FFF;
		k += adjk;
		fp1 = int32_to_floatx80(k);
		
		fSig = (aSig & LIT64(0xFE00000000000000)) | LIT64(0x0100000000000000);
		j = (fSig >> 56) & 0x7E; // DISPLACEMENT FOR 1/F
		
		f = packFloatx80(0, 0x3FFF, fSig); // F
		fp0 = packFloatx80(0, 0x3FFF, aSig); // Y
		
		fp0 = floatx80_sub(fp0, f, status); // Y-F
		
		// LP1CONT1
		fp0 = floatx80_mul(fp0, log_tbl[j], status); // FP0 IS U = (Y-F)/F
		logof2 = packFloatx80(0, 0x3FFE, LIT64(0xB17217F7D1CF79AC));
		klog2 = floatx80_mul(fp1, logof2, status); // FP1 IS K*LOG2
		fp2 = floatx80_mul(fp0, fp0, status); // FP2 IS V=U*U
		
		fp3 = fp2;
		fp1 = fp2;
		
		fp1 = floatx80_mul(fp1, float64_to_floatx80(LIT64(0x3FC2499AB5E4040B), status), status); // V*A6
		fp2 = floatx80_mul(fp2, float64_to_floatx80(LIT64(0xBFC555B5848CB7DB), status), status); // V*A5
		fp1 = floatx80_add(fp1, float64_to_floatx80(LIT64(0x3FC99999987D8730), status), status); // A4+V*A6
		fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0xBFCFFFFFFF6F7E97), status), status); // A3+V*A5
		fp1 = floatx80_mul(fp1, fp3, status); // V*(A4+V*A6)
		fp2 = floatx80_mul(fp2, fp3, status); // V*(A3+V*A5)
		fp1 = floatx80_add(fp1, float64_to_floatx80(LIT64(0x3FD55555555555A4), status), status); // A2+V*(A4+V*A6)
		fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0xBFE0000000000008), status), status); // A1+V*(A3+V*A5)
		fp1 = floatx80_mul(fp1, fp3, status); // V*(A2+V*(A4+V*A6))
		fp2 = floatx80_mul(fp2, fp3, status); // V*(A1+V*(A3+V*A5))
		fp1 = floatx80_mul(fp1, fp0, status); // U*V*(A2+V*(A4+V*A6))
		fp0 = floatx80_add(fp0, fp2, status); // U+V*(A1+V*(A3+V*A5))
		
		fp1 = floatx80_add(fp1, log_tbl[j+1], status); // LOG(F)+U*V*(A2+V*(A4+V*A6))
		fp0 = floatx80_add(fp0, fp1, status); // FP0 IS LOG(F) + LOG(1+U)
		
		RESET_PREC;
		
		a = floatx80_add(fp0, klog2, status);
		
		float_raise(float_flag_inexact, status);
		
		return a;
	} else { // |X-1| >= 1/16
		fp0 = a;
		fp1 = a;
		fp1 = floatx80_sub(fp1, float32_to_floatx80(0x3F800000, status), status); // FP1 IS X-1
		fp0 = floatx80_add(fp0, float32_to_floatx80(0x3F800000, status), status); // FP0 IS X+1
		fp1 = floatx80_add(fp1, fp1, status); // FP1 IS 2(X-1)
		
		// LP1CONT2
		fp1 = floatx80_div(fp1, fp0, status); // U
		saveu = fp1;
		fp0 = floatx80_mul(fp1, fp1, status); // FP0 IS V = U*U
		fp1 = floatx80_mul(fp0, fp0, status); // FP1 IS W = V*V
		
		fp3 = float64_to_floatx80(LIT64(0x3F175496ADD7DAD6), status); // B5
		fp2 = float64_to_floatx80(LIT64(0x3F3C71C2FE80C7E0), status); // B4
		fp3 = floatx80_mul(fp3, fp1, status); // W*B5
		fp2 = floatx80_mul(fp2, fp1, status); // W*B4
		fp3 = floatx80_add(fp3, float64_to_floatx80(LIT64(0x3F624924928BCCFF), status), status); // B3+W*B5
		fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3F899999999995EC), status), status); // B2+W*B4
		fp1 = floatx80_mul(fp1, fp3, status); // W*(B3+W*B5)
		fp2 = floatx80_mul(fp2, fp0, status); // V*(B2+W*B4)
		fp1 = floatx80_add(fp1, float64_to_floatx80(LIT64(0x3FB5555555555555), status), status); // B1+W*(B3+W*B5)
		
		fp0 = floatx80_mul(fp0, saveu, status); // FP0 IS U*V
		fp1 = floatx80_add(fp1, fp2, status); // B1+W*(B3+W*B5) + V*(B2+W*B4)
		fp0 = floatx80_mul(fp0, fp1, status); // U*V*( [B1+W*(B3+W*B5)] + [V*(B2+W*B4)] )
		
		RESET_PREC;
		
		a = floatx80_add(fp0, saveu, status);
		
		float_raise(float_flag_inexact, status);
		
		return a;
	}
}

/*----------------------------------------------------------------------------
 | Log base e of x plus 1
 *----------------------------------------------------------------------------*/

floatx80 floatx80_lognp1(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig, fSig;
	
	int32_t compact, j, k;
	floatx80 fp0, fp1, fp2, fp3, f, logof2, klog2, saveu;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		if (aSign) {
			float_raise(float_flag_invalid, status);
			return floatx80_default_nan(status);
		}
		return a;
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(aSign, 0, 0);
	}
	
	if (aSign && aExp >= one_exp) {
		if (aExp == one_exp && aSig == one_sig) {
			float_raise(float_flag_divbyzero, status);
			return packFloatx80(aSign, 0x7FFF, floatx80_default_infinity_low);
		}
		float_raise(float_flag_invalid, status);
		return floatx80_default_nan(status);
	}
	
	if (aExp < 0x3f99 || (aExp == 0x3f99 && aSig == one_sig)) { // <= min threshold
		float_raise(float_flag_inexact, status);
		return floatx80_move(a, status);
	}
	
	SET_PREC;
	
	fp0 = a; // Z
	fp1 = a;
	
	fp0 = floatx80_add(fp0, float32_to_floatx80(0x3F800000, status), status); // X = (1+Z)
	
	aExp = extractFloatx80Exp(fp0);
	aSig = extractFloatx80Frac(fp0);
	
	compact = floatx80_make_compact(aExp, aSig);
	
	if (compact < 0x3FFE8000 || compact > 0x3FFFC000) { // |X| < 1/2 or |X| > 3/2
		k = aExp - 0x3FFF;
		fp1 = int32_to_floatx80(k);
		
		fSig = (aSig & LIT64(0xFE00000000000000)) | LIT64(0x0100000000000000);
		j = (fSig >> 56) & 0x7E; // DISPLACEMENT FOR 1/F
		
		f = packFloatx80(0, 0x3FFF, fSig); // F
		fp0 = packFloatx80(0, 0x3FFF, aSig); // Y
		
		fp0 = floatx80_sub(fp0, f, status); // Y-F
		
	lp1cont1:
		// LP1CONT1
		fp0 = floatx80_mul(fp0, log_tbl[j], status); // FP0 IS U = (Y-F)/F
		logof2 = packFloatx80(0, 0x3FFE, LIT64(0xB17217F7D1CF79AC));
		klog2 = floatx80_mul(fp1, logof2, status); // FP1 IS K*LOG2
		fp2 = floatx80_mul(fp0, fp0, status); // FP2 IS V=U*U
		
		fp3 = fp2;
		fp1 = fp2;
		
		fp1 = floatx80_mul(fp1, float64_to_floatx80(LIT64(0x3FC2499AB5E4040B), status), status); // V*A6
		fp2 = floatx80_mul(fp2, float64_to_floatx80(LIT64(0xBFC555B5848CB7DB), status), status); // V*A5
		fp1 = floatx80_add(fp1, float64_to_floatx80(LIT64(0x3FC99999987D8730), status), status); // A4+V*A6
		fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0xBFCFFFFFFF6F7E97), status), status); // A3+V*A5
		fp1 = floatx80_mul(fp1, fp3, status); // V*(A4+V*A6)
		fp2 = floatx80_mul(fp2, fp3, status); // V*(A3+V*A5)
		fp1 = floatx80_add(fp1, float64_to_floatx80(LIT64(0x3FD55555555555A4), status), status); // A2+V*(A4+V*A6)
		fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0xBFE0000000000008), status), status); // A1+V*(A3+V*A5)
		fp1 = floatx80_mul(fp1, fp3, status); // V*(A2+V*(A4+V*A6))
		fp2 = floatx80_mul(fp2, fp3, status); // V*(A1+V*(A3+V*A5))
		fp1 = floatx80_mul(fp1, fp0, status); // U*V*(A2+V*(A4+V*A6))
		fp0 = floatx80_add(fp0, fp2, status); // U+V*(A1+V*(A3+V*A5))
		
		fp1 = floatx80_add(fp1, log_tbl[j+1], status); // LOG(F)+U*V*(A2+V*(A4+V*A6))
		fp0 = floatx80_add(fp0, fp1, status); // FP0 IS LOG(F) + LOG(1+U)
		
		RESET_PREC;
		
		a = floatx80_add(fp0, klog2, status);
		
		float_raise(float_flag_inexact, status);
		
		return a;
	} else if (compact < 0x3FFEF07D || compact > 0x3FFF8841) { // |X| < 1/16 or |X| > -1/16
		// LP1CARE
		fSig = (aSig & LIT64(0xFE00000000000000)) | LIT64(0x0100000000000000);
		f = packFloatx80(0, 0x3FFF, fSig); // F
		j = (fSig >> 56) & 0x7E; // DISPLACEMENT FOR 1/F

		if (compact >= 0x3FFF8000) { // 1+Z >= 1
			// KISZERO
			fp0 = floatx80_sub(float32_to_floatx80(0x3F800000, status), f, status); // 1-F
			fp0 = floatx80_add(fp0, fp1, status); // FP0 IS Y-F = (1-F)+Z
			fp1 = packFloatx80(0, 0, 0); // K = 0
		} else {
			// KISNEG
			fp0 = floatx80_sub(float32_to_floatx80(0x40000000, status), f, status); // 2-F
			fp1 = floatx80_add(fp1, fp1, status); // 2Z
			fp0 = floatx80_add(fp0, fp1, status); // FP0 IS Y-F = (2-F)+2Z
			fp1 = packFloatx80(1, one_exp, one_sig); // K = -1
		}
		goto lp1cont1;
	} else {
		// LP1ONE16
		fp1 = floatx80_add(fp1, fp1, status); // FP1 IS 2Z
		fp0 = floatx80_add(fp0, float32_to_floatx80(0x3F800000, status), status); // FP0 IS 1+X
		
		// LP1CONT2
		fp1 = floatx80_div(fp1, fp0, status); // U
		saveu = fp1;
		fp0 = floatx80_mul(fp1, fp1, status); // FP0 IS V = U*U
		fp1 = floatx80_mul(fp0, fp0, status); // FP1 IS W = V*V
		
		fp3 = float64_to_floatx80(LIT64(0x3F175496ADD7DAD6), status); // B5
		fp2 = float64_to_floatx80(LIT64(0x3F3C71C2FE80C7E0), status); // B4
		fp3 = floatx80_mul(fp3, fp1, status); // W*B5
		fp2 = floatx80_mul(fp2, fp1, status); // W*B4
		fp3 = floatx80_add(fp3, float64_to_floatx80(LIT64(0x3F624924928BCCFF), status), status); // B3+W*B5
		fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3F899999999995EC), status), status); // B2+W*B4
		fp1 = floatx80_mul(fp1, fp3, status); // W*(B3+W*B5)
		fp2 = floatx80_mul(fp2, fp0, status); // V*(B2+W*B4)
		fp1 = floatx80_add(fp1, float64_to_floatx80(LIT64(0x3FB5555555555555), status), status); // B1+W*(B3+W*B5)
		
		fp0 = floatx80_mul(fp0, saveu, status); // FP0 IS U*V
		fp1 = floatx80_add(fp1, fp2, status); // B1+W*(B3+W*B5) + V*(B2+W*B4)
		fp0 = floatx80_mul(fp0, fp1, status); // U*V*( [B1+W*(B3+W*B5)] + [V*(B2+W*B4)] )
		
		RESET_PREC;
		
		a = floatx80_add(fp0, saveu, status);
		
		float_raise(float_flag_inexact, status);
		
		return a;
	}
}

/*----------------------------------------------------------------------------
 | Sine
 *----------------------------------------------------------------------------*/

floatx80 floatx80_sin(floatx80 a, float_status *status)
{
	flag aSign, xSign;
	int32_t aExp, xExp;
	uint64_t aSig, xSig;
	
	int32_t compact, l, n, j;
	floatx80 fp0, fp1, fp2, fp3, fp4, fp5, x, invtwopi, twopi1, twopi2;
	float32 posneg1, twoto63;
	flag adjn, endflag;

	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		float_raise(float_flag_invalid, status);
		return floatx80_default_nan(status);
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(aSign, 0, 0);
	}
	
	adjn = 0;
	
	SET_PREC;
	
	compact = floatx80_make_compact(aExp, aSig);
	
	fp0 = a;
	
	if (compact < 0x3FD78000 || compact > 0x4004BC7E) { // 2^(-40) > |X| > 15 PI
		if (compact > 0x3FFF8000) { // |X| >= 15 PI
			// REDUCEX
			fp1 = packFloatx80(0, 0, 0);
			if (compact == 0x7FFEFFFF) {
				twopi1 = packFloatx80(aSign ^ 1, 0x7FFE, LIT64(0xC90FDAA200000000));
				twopi2 = packFloatx80(aSign ^ 1, 0x7FDC, LIT64(0x85A308D300000000));
				fp0 = floatx80_add(fp0, twopi1, status);
				fp1 = fp0;
				fp0 = floatx80_add(fp0, twopi2, status);
				fp1 = floatx80_sub(fp1, fp0, status);
				fp1 = floatx80_add(fp1, twopi2, status);
			}
		loop:
			xSign = extractFloatx80Sign(fp0);
			xExp = extractFloatx80Exp(fp0);
			xExp -= 0x3FFF;
			if (xExp <= 28) {
				l = 0;
				endflag = 1;
			} else {
				l = xExp - 27;
				endflag = 0;
			}
			invtwopi = packFloatx80(0, 0x3FFE - l, LIT64(0xA2F9836E4E44152A)); // INVTWOPI
			twopi1 = packFloatx80(0, 0x3FFF + l, LIT64(0xC90FDAA200000000));
			twopi2 = packFloatx80(0, 0x3FDD + l, LIT64(0x85A308D300000000));
			
			twoto63 = 0x5F000000;
			twoto63 |= xSign ? 0x80000000 : 0x00000000; // SIGN(INARG)*2^63 IN SGL
			
			fp2 = floatx80_mul(fp0, invtwopi, status);
			fp2 = floatx80_add(fp2, float32_to_floatx80(twoto63, status), status); // THE FRACTIONAL PART OF FP2 IS ROUNDED
			fp2 = floatx80_sub(fp2, float32_to_floatx80(twoto63, status), status); // FP2 is N
			fp4 = floatx80_mul(twopi1, fp2, status); // W = N*P1
			fp5 = floatx80_mul(twopi2, fp2, status); // w = N*P2
			fp3 = floatx80_add(fp4, fp5, status); // FP3 is P
			fp4 = floatx80_sub(fp4, fp3, status); // W-P
			fp0 = floatx80_sub(fp0, fp3, status); // FP0 is A := R - P
			fp4 = floatx80_add(fp4, fp5, status); // FP4 is p = (W-P)+w
			fp3 = fp0; // FP3 is A
			fp1 = floatx80_sub(fp1, fp4, status); // FP1 is a := r - p
			fp0 = floatx80_add(fp0, fp1, status); // FP0 is R := A+a
			
			if (endflag > 0) {
				n = floatx80_to_int32(fp2, status);
				goto sincont;
			}
			fp3 = floatx80_sub(fp3, fp0, status); // A-R
			fp1 = floatx80_add(fp1, fp3, status); // FP1 is r := (A-R)+a
			goto loop;
		} else {
			// SINSM
			fp0 = float32_to_floatx80(0x3F800000, status); // 1
			
			RESET_PREC;
			
			if (adjn) {
				// COSTINY
				a = floatx80_sub(fp0, float32_to_floatx80(0x00800000, status), status);
			} else {
				// SINTINY
				a = floatx80_move(a, status);
			}
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	} else {
		fp1 = floatx80_mul(fp0, float64_to_floatx80(LIT64(0x3FE45F306DC9C883), status), status); // X*2/PI
		
		n = floatx80_to_int32(fp1, status);
		j = 32 + n;
		
		fp0 = floatx80_sub(fp0, pi_tbl[j], status); // X-Y1
		fp0 = floatx80_sub(fp0, float32_to_floatx80(pi_tbl2[j], status), status); // FP0 IS R = (X-Y1)-Y2
		
	sincont:
		if ((n + adjn) & 1) {
			// COSPOLY
			fp0 = floatx80_mul(fp0, fp0, status); // FP0 IS S
			fp1 = floatx80_mul(fp0, fp0, status); // FP1 IS T
			fp2 = float64_to_floatx80(LIT64(0x3D2AC4D0D6011EE3), status); // B8
			fp3 = float64_to_floatx80(LIT64(0xBDA9396F9F45AC19), status); // B7
			
			xSign = extractFloatx80Sign(fp0); // X IS S
			xExp = extractFloatx80Exp(fp0);
			xSig = extractFloatx80Frac(fp0);
			
			if (((n + adjn) >> 1) & 1) {
				xSign ^= 1;
				posneg1 = 0xBF800000; // -1
			} else {
				xSign ^= 0;
				posneg1 = 0x3F800000; // 1
			} // X IS NOW R'= SGN*R
			
			fp2 = floatx80_mul(fp2, fp1, status); // TB8
			fp3 = floatx80_mul(fp3, fp1, status); // TB7
			fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3E21EED90612C972), status), status); // B6+TB8
			fp3 = floatx80_add(fp3, float64_to_floatx80(LIT64(0xBE927E4FB79D9FCF), status), status); // B5+TB7
			fp2 = floatx80_mul(fp2, fp1, status); // T(B6+TB8)
			fp3 = floatx80_mul(fp3, fp1, status); // T(B5+TB7)
			fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3EFA01A01A01D423), status), status); // B4+T(B6+TB8)
			fp4 = packFloatx80(1, 0x3FF5, LIT64(0xB60B60B60B61D438));
			fp3 = floatx80_add(fp3, fp4, status); // B3+T(B5+TB7)
			fp2 = floatx80_mul(fp2, fp1, status); // T(B4+T(B6+TB8))
			fp1 = floatx80_mul(fp1, fp3, status); // T(B3+T(B5+TB7))
			fp4 = packFloatx80(0, 0x3FFA, LIT64(0xAAAAAAAAAAAAAB5E));
			fp2 = floatx80_add(fp2, fp4, status); // B2+T(B4+T(B6+TB8))
			fp1 = floatx80_add(fp1, float32_to_floatx80(0xBF000000, status), status); // B1+T(B3+T(B5+TB7))
			fp0 = floatx80_mul(fp0, fp2, status); // S(B2+T(B4+T(B6+TB8)))
			fp0 = floatx80_add(fp0, fp1, status); // [B1+T(B3+T(B5+TB7))]+[S(B2+T(B4+T(B6+TB8)))]
			
			x = packFloatx80(xSign, xExp, xSig);
			fp0 = floatx80_mul(fp0, x, status);
			
			RESET_PREC;
			
			a = floatx80_add(fp0, float32_to_floatx80(posneg1, status), status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		} else {
			// SINPOLY
			xSign = extractFloatx80Sign(fp0); // X IS R
			xExp = extractFloatx80Exp(fp0);
			xSig = extractFloatx80Frac(fp0);
			
			xSign ^= ((n + adjn) >> 1) & 1; // X IS NOW R'= SGN*R
			
			fp0 = floatx80_mul(fp0, fp0, status); // FP0 IS S
			fp1 = floatx80_mul(fp0, fp0, status); // FP1 IS T
			fp3 = float64_to_floatx80(LIT64(0xBD6AAA77CCC994F5), status); // A7
			fp2 = float64_to_floatx80(LIT64(0x3DE612097AAE8DA1), status); // A6
			fp3 = floatx80_mul(fp3, fp1, status); // T*A7
			fp2 = floatx80_mul(fp2, fp1, status); // T*A6
			fp3 = floatx80_add(fp3, float64_to_floatx80(LIT64(0xBE5AE6452A118AE4), status), status); // A5+T*A7
			fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3EC71DE3A5341531), status), status); // A4+T*A6
			fp3 = floatx80_mul(fp3, fp1, status); // T(A5+TA7)
			fp2 = floatx80_mul(fp2, fp1, status); // T(A4+TA6)
			fp3 = floatx80_add(fp3, float64_to_floatx80(LIT64(0xBF2A01A01A018B59), status), status); // A3+T(A5+TA7)
			fp4 = packFloatx80(0, 0x3FF8, LIT64(0x88888888888859AF));
			fp2 = floatx80_add(fp2, fp4, status); // A2+T(A4+TA6)
			fp1 = floatx80_mul(fp1, fp3, status); // T(A3+T(A5+TA7))
			fp2 = floatx80_mul(fp2, fp0, status); // S(A2+T(A4+TA6))
			fp4 = packFloatx80(1, 0x3FFC, LIT64(0xAAAAAAAAAAAAAA99));
			fp1 = floatx80_add(fp1, fp4, status); // A1+T(A3+T(A5+TA7))
			fp1 = floatx80_add(fp1, fp2, status); // [A1+T(A3+T(A5+TA7))]+[S(A2+T(A4+TA6))]
			
			x = packFloatx80(xSign, xExp, xSig);
			fp0 = floatx80_mul(fp0, x, status); // R'*S
			fp0 = floatx80_mul(fp0, fp1, status); // SIN(R')-R'
			
			RESET_PREC;
			
			a = floatx80_add(fp0, x, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	}
}

/*----------------------------------------------------------------------------
 | Sine and cosine
 *----------------------------------------------------------------------------*/

floatx80 floatx80_sincos(floatx80 a, floatx80 *c, float_status *status)
{
	flag aSign, xSign, rSign, sSign;
	int32_t aExp, xExp, rExp, sExp;
	uint64_t aSig, rSig, sSig;
	
	int32_t compact, l, n, i, j1, j2;
	floatx80 fp0, fp1, fp2, fp3, fp4, fp5, r, s, invtwopi, twopi1, twopi2;
	float32 posneg1, twoto63;
	flag endflag;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t)(aSig << 1)) {
			*c = propagateFloatx80NaNOneArg(a, status);
			return *c;
			
		}
		float_raise(float_flag_invalid, status);
		*c = floatx80_default_nan(status);
		return *c;
		
	}
	
	if (aExp == 0 && aSig == 0) {
		*c = packFloatx80(0, one_exp, one_sig);
		return packFloatx80(aSign, 0, 0);
		
	}
	
	SET_PREC;
	
	compact = floatx80_make_compact(aExp, aSig);
	
	fp0 = a;
	
	if (compact < 0x3FD78000 || compact > 0x4004BC7E) { // 2^(-40) > |X| > 15 PI
		if (compact > 0x3FFF8000) { // |X| >= 15 PI
			// REDUCEX
			fp1 = packFloatx80(0, 0, 0);
			if (compact == 0x7FFEFFFF) {
				twopi1 = packFloatx80(aSign ^ 1, 0x7FFE, LIT64(0xC90FDAA200000000));
				twopi2 = packFloatx80(aSign ^ 1, 0x7FDC, LIT64(0x85A308D300000000));
				fp0 = floatx80_add(fp0, twopi1, status);
				fp1 = fp0;
				fp0 = floatx80_add(fp0, twopi2, status);
				fp1 = floatx80_sub(fp1, fp0, status);
				fp1 = floatx80_add(fp1, twopi2, status);
				
			}
			loop:
			xSign = extractFloatx80Sign(fp0);
			xExp = extractFloatx80Exp(fp0);
			xExp -= 0x3FFF;
			if (xExp <= 28) {
				l = 0;
				endflag = 1;
			} else {
				l = xExp - 27;
				endflag = 0;
			}
			invtwopi = packFloatx80(0, 0x3FFE - l, LIT64(0xA2F9836E4E44152A)); // INVTWOPI
			twopi1 = packFloatx80(0, 0x3FFF + l, LIT64(0xC90FDAA200000000));
			twopi2 = packFloatx80(0, 0x3FDD + l, LIT64(0x85A308D300000000));

			twoto63 = 0x5F000000;
			twoto63 |= xSign ? 0x80000000 : 0x00000000; // SIGN(INARG)*2^63 IN SGL

			fp2 = floatx80_mul(fp0, invtwopi, status);
			fp2 = floatx80_add(fp2, float32_to_floatx80(twoto63, status), status); // THE FRACTIONAL PART OF FP2 IS ROUNDED
			fp2 = floatx80_sub(fp2, float32_to_floatx80(twoto63, status), status); // FP2 is N
			fp4 = floatx80_mul(twopi1, fp2, status); // W = N*P1
			fp5 = floatx80_mul(twopi2, fp2, status); // w = N*P2
			fp3 = floatx80_add(fp4, fp5, status); // FP3 is P
			fp4 = floatx80_sub(fp4, fp3, status); // W-P
			fp0 = floatx80_sub(fp0, fp3, status); // FP0 is A := R - P
			fp4 = floatx80_add(fp4, fp5, status); // FP4 is p = (W-P)+w
			fp3 = fp0; // FP3 is A
			fp1 = floatx80_sub(fp1, fp4, status); // FP1 is a := r - p
			fp0 = floatx80_add(fp0, fp1, status); // FP0 is R := A+a
			
			if (endflag > 0) {
				n = floatx80_to_int32(fp2, status);
				goto sccont;
			}
			fp3 = floatx80_sub(fp3, fp0, status); // A-R
			fp1 = floatx80_add(fp1, fp3, status); // FP1 is r := (A-R)+a
			goto loop;
		} else {
			// SCSM
			fp0 = float32_to_floatx80(0x3F800000, status); // 1
			
			RESET_PREC;
			
			// COSTINY
			*c = floatx80_sub(fp0, float32_to_floatx80(0x00800000, status), status);
			// SINTINY
			a = floatx80_move(a, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		
		}
	} else {
		fp1 = floatx80_mul(fp0, float64_to_floatx80(LIT64(0x3FE45F306DC9C883), status), status); // X*2/PI
		
		n = floatx80_to_int32(fp1, status);
		i = 32 + n;
		
		fp0 = floatx80_sub(fp0, pi_tbl[i], status); // X-Y1
		fp0 = floatx80_sub(fp0, float32_to_floatx80(pi_tbl2[i], status), status); // FP0 IS R = (X-Y1)-Y2
		
		sccont:
		n &= 3; // k = N mod 4
		if (n & 1) {
			// NODD
			j1 = n >> 1; // j1 = (k-1)/2
			j2 = j1 ^ (n & 1); // j2 = j1 EOR (k mod 2)
			
			rSign = extractFloatx80Sign(fp0); // R
			rExp = extractFloatx80Exp(fp0);
			rSig = extractFloatx80Frac(fp0);
			rSign ^= j2;
			
			fp0 = floatx80_mul(fp0, fp0, status); // FP0 IS S = R*R
			fp1 = float64_to_floatx80(LIT64(0xBD6AAA77CCC994F5), status); // A7
			fp2 = float64_to_floatx80(LIT64(0x3D2AC4D0D6011EE3), status); // B8
			fp1 = floatx80_mul(fp1, fp0, status); // FP1 IS SA7
			fp2 = floatx80_mul(fp2, fp0, status); // FP2 IS SB8
			fp1 = floatx80_add(fp1, float64_to_floatx80(LIT64(0x3DE612097AAE8DA1), status), status); // A6+SA7
			fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0xBDA9396F9F45AC19), status), status); // B7+SB8
			fp1 = floatx80_mul(fp1, fp0, status); // S(A6+SA7)
			fp2 = floatx80_mul(fp2, fp0, status); // S(B7+SB8)
			fp1 = floatx80_add(fp1, float64_to_floatx80(LIT64(0xBE5AE6452A118AE4), status), status); // A5+S(A6+SA7)
			fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3E21EED90612C972), status), status); // B6+S(B7+SB8)
			fp1 = floatx80_mul(fp1, fp0, status); // S(A5+S(A6+SA7))
			fp2 = floatx80_mul(fp2, fp0, status); // S(B6+S(B7+SB8))
			
			sSign = extractFloatx80Sign(fp0); // S
			sExp = extractFloatx80Exp(fp0);
			sSig = extractFloatx80Frac(fp0);
			sSign ^= j1;
			posneg1 = 0x3F800000;
			posneg1 |= j1 ? 0x80000000 : 0;
			
			fp1 = floatx80_add(fp1, float64_to_floatx80(LIT64(0x3EC71DE3A5341531), status), status); // A4+S(A5+S(A6+SA7))
			fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0xBE927E4FB79D9FCF), status), status); // B5+S(B6+S(B7+SB8))
			fp1 = floatx80_mul(fp1, fp0, status); // S(A4+...)
			fp2 = floatx80_mul(fp2, fp0, status); // S(B5+...)
			fp1 = floatx80_add(fp1, float64_to_floatx80(LIT64(0xBF2A01A01A018B59), status), status); // A3+S(A4+...)
			fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3EFA01A01A01D423), status), status); // B4+S(B5+...)
			fp1 = floatx80_mul(fp1, fp0, status); // S(A3+...)
			fp2 = floatx80_mul(fp2, fp0, status); // S(B4+...)
			fp3 = packFloatx80(0, 0x3FF8, LIT64(0x88888888888859AF));
			fp4 = packFloatx80(1, 0x3FF5, LIT64(0xB60B60B60B61D438));
			fp1 = floatx80_add(fp1, fp3, status); // A2+S(A3+...)
			fp2 = floatx80_add(fp2, fp4, status); // B3+S(B4+...)
			fp1 = floatx80_mul(fp1, fp0, status); // S(A2+...)
			fp2 = floatx80_mul(fp2, fp0, status); // S(B3+...)
			fp3 = packFloatx80(1, 0x3FFC, LIT64(0xAAAAAAAAAAAAAA99));
			fp4 = packFloatx80(0, 0x3FFA, LIT64(0xAAAAAAAAAAAAAB5E));
			fp1 = floatx80_add(fp1, fp3, status); // A1+S(A2+...)
			fp2 = floatx80_add(fp2, fp4, status); // B2+S(B3+...)
			fp1 = floatx80_mul(fp1, fp0, status); // S(A1+...)
			fp0 = floatx80_mul(fp0, fp2, status); // S(B2+...)
			
			r = packFloatx80(rSign, rExp, rSig);
			fp1 = floatx80_mul(fp1, r, status); // R'S(A1+...)
			fp0 = floatx80_add(fp0, float32_to_floatx80(0xBF000000, status), status); // B1+S(B2...)
			
			s = packFloatx80(sSign, sExp, sSig);
			fp0 = floatx80_mul(fp0, s, status); // S'(B1+S(B2+...))
			
			RESET_PREC;
			
			*c = floatx80_add(fp1, r, status); // COS(X)
			
			a = floatx80_add(fp0, float32_to_floatx80(posneg1, status), status); // SIN(X)
			
			float_raise(float_flag_inexact, status);
			
			return a;
		} else {
			// NEVEN
			j1 = n >> 1; // j1 = k/2
			
			rSign = extractFloatx80Sign(fp0); // R
			rExp = extractFloatx80Exp(fp0);
			rSig = extractFloatx80Frac(fp0);
			rSign ^= j1;
			
			fp0 = floatx80_mul(fp0, fp0, status); // FP0 IS S = R*R
			fp1 = float64_to_floatx80(LIT64(0x3D2AC4D0D6011EE3), status); // B8
			fp2 = float64_to_floatx80(LIT64(0xBD6AAA77CCC994F5), status); // A7
			fp1 = floatx80_mul(fp1, fp0, status); // SB8
			fp2 = floatx80_mul(fp2, fp0, status); // SA7
			
			sSign = extractFloatx80Sign(fp0); // S
			sExp = extractFloatx80Exp(fp0);
			sSig = extractFloatx80Frac(fp0);
			sSign ^= j1;
			posneg1 = 0x3F800000;
			posneg1 |= j1 ? 0x80000000 : 0;
			
			fp1 = floatx80_add(fp1, float64_to_floatx80(LIT64(0xBDA9396F9F45AC19), status), status); // B7+SB8
			fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3DE612097AAE8DA1), status), status); // A6+SA7
			fp1 = floatx80_mul(fp1, fp0, status); // S(B7+SB8)
			fp2 = floatx80_mul(fp2, fp0, status); // S(A6+SA7)
			fp1 = floatx80_add(fp1, float64_to_floatx80(LIT64(0x3E21EED90612C972), status), status); // B6+S(B7+SB8)
			fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0xBE5AE6452A118AE4), status), status); // A5+S(A6+SA7)
			fp1 = floatx80_mul(fp1, fp0, status); // S(B6+S(B7+SB8))
			fp2 = floatx80_mul(fp2, fp0, status); // S(A5+S(A6+SA7))
			fp1 = floatx80_add(fp1, float64_to_floatx80(LIT64(0xBE927E4FB79D9FCF), status), status); // B5+S(B6+S(B7+SB8))
			fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3EC71DE3A5341531), status), status); // A4+S(A5+S(A6+SA7))
			fp1 = floatx80_mul(fp1, fp0, status); // S(B5+...)
			fp2 = floatx80_mul(fp2, fp0, status); // S(A4+...)
			fp1 = floatx80_add(fp1, float64_to_floatx80(LIT64(0x3EFA01A01A01D423), status), status); // B4+S(B5+...)
			fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0xBF2A01A01A018B59), status), status); // A3+S(A4+...)
			fp1 = floatx80_mul(fp1, fp0, status); // S(B4+...)
			fp2 = floatx80_mul(fp2, fp0, status); // S(A3+...)
			fp3 = packFloatx80(1, 0x3FF5, LIT64(0xB60B60B60B61D438));
			fp4 = packFloatx80(0, 0x3FF8, LIT64(0x88888888888859AF));
			fp1 = floatx80_add(fp1, fp3, status); // B3+S(B4+...)
			fp2 = floatx80_add(fp2, fp4, status); // A2+S(A3+...)
			fp1 = floatx80_mul(fp1, fp0, status); // S(B3+...)
			fp2 = floatx80_mul(fp2, fp0, status); // S(A2+...)
			fp3 = packFloatx80(0, 0x3FFA, LIT64(0xAAAAAAAAAAAAAB5E));
			fp4 = packFloatx80(1, 0x3FFC, LIT64(0xAAAAAAAAAAAAAA99));
			fp1 = floatx80_add(fp1, fp3, status); // B2+S(B3+...)
			fp2 = floatx80_add(fp2, fp4, status); // A1+S(A2+...)
			fp1 = floatx80_mul(fp1, fp0, status); // S(B2+...)
			fp0 = floatx80_mul(fp0, fp2, status); // S(A1+...)
			fp1 = floatx80_add(fp1, float32_to_floatx80(0xBF000000, status), status); // B1+S(B2...)
			
			r = packFloatx80(rSign, rExp, rSig);
			fp0 = floatx80_mul(fp0, r, status); // R'S(A1+...)
			
			s = packFloatx80(sSign, sExp, sSig);
			fp1 = floatx80_mul(fp1, s, status); // S'(B1+S(B2+...))
			
			RESET_PREC;
			
			*c = floatx80_add(fp1, float32_to_floatx80(posneg1, status), status); // COS(X)
			
			a = floatx80_add(fp0, r, status); // SIN(X)
			
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	}
}

/*----------------------------------------------------------------------------
 | Hyperbolic sine
 *----------------------------------------------------------------------------*/

floatx80 floatx80_sinh(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	int32_t compact;
	floatx80 fp0, fp1, fp2;
	float32 fact;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		return a;
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(aSign, 0, 0);
	}
	
	SET_PREC;
	
	compact = floatx80_make_compact(aExp, aSig);

	if (compact > 0x400CB167) {
		// SINHBIG
		if (compact > 0x400CB2B3) {
			RESET_PREC;
			
			a = roundAndPackFloatx80(status->floatx80_rounding_precision, aSign, 0x8000, aSig, 0, status);
			float_raise(float_flag_inexact, status);
			return a;
		} else {
			fp0 = floatx80_abs(a, status); // Y = |X|
			fp0 = floatx80_sub(fp0, float64_to_floatx80(LIT64(0x40C62D38D3D64634), status), status); // (|X|-16381LOG2_LEAD)
			fp0 = floatx80_sub(fp0, float64_to_floatx80(LIT64(0x3D6F90AEB1E75CC7), status), status); // |X| - 16381 LOG2, ACCURATE
			fp0 = floatx80_etox(fp0, status);
			fp2 = packFloatx80(aSign, 0x7FFB, one_sig);
			
			RESET_PREC;
			
			a = floatx80_mul(fp0, fp2, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	} else { // |X| < 16380 LOG2
		fp0 = floatx80_abs(a, status); // Y = |X|
		fp0 = floatx80_etoxm1(fp0, status); // FP0 IS Z = EXPM1(Y)
		fp1 = floatx80_add(fp0, float32_to_floatx80(0x3F800000, status), status); // 1+Z
		fp2 = fp0;
		fp0 = floatx80_div(fp0, fp1, status); // Z/(1+Z)
		fp0 = floatx80_add(fp0, fp2, status);
		
		fact = 0x3F000000;
		fact |= aSign ? 0x80000000 : 0x00000000;
		
		RESET_PREC;
		
		a = floatx80_mul(fp0, float32_to_floatx80(fact, status), status);
		
		float_raise(float_flag_inexact, status);
		
		return a;
	}
}

/*----------------------------------------------------------------------------
 | Tangent
 *----------------------------------------------------------------------------*/

floatx80 floatx80_tan(floatx80 a, float_status *status)
{
	flag aSign, xSign;
	int32_t aExp, xExp;
	uint64_t aSig, xSig;
	
	int32_t compact, l, n, j;
	floatx80 fp0, fp1, fp2, fp3, fp4, fp5, invtwopi, twopi1, twopi2;
	float32 twoto63;
	flag endflag;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		float_raise(float_flag_invalid, status);
		return floatx80_default_nan(status);
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(aSign, 0, 0);
	}
	
	SET_PREC;
	
	compact = floatx80_make_compact(aExp, aSig);
	
	fp0 = a;
	
	if (compact < 0x3FD78000 || compact > 0x4004BC7E) { // 2^(-40) > |X| > 15 PI
		if (compact > 0x3FFF8000) { // |X| >= 15 PI
			// REDUCEX
			fp1 = packFloatx80(0, 0, 0);
			if (compact == 0x7FFEFFFF) {
				twopi1 = packFloatx80(aSign ^ 1, 0x7FFE, LIT64(0xC90FDAA200000000));
				twopi2 = packFloatx80(aSign ^ 1, 0x7FDC, LIT64(0x85A308D300000000));
				fp0 = floatx80_add(fp0, twopi1, status);
				fp1 = fp0;
				fp0 = floatx80_add(fp0, twopi2, status);
				fp1 = floatx80_sub(fp1, fp0, status);
				fp1 = floatx80_add(fp1, twopi2, status);
			}
		loop:
			xSign = extractFloatx80Sign(fp0);
			xExp = extractFloatx80Exp(fp0);
			xExp -= 0x3FFF;
			if (xExp <= 28) {
				l = 0;
				endflag = 1;
			} else {
				l = xExp - 27;
				endflag = 0;
			}
			invtwopi = packFloatx80(0, 0x3FFE - l, LIT64(0xA2F9836E4E44152A)); // INVTWOPI
			twopi1 = packFloatx80(0, 0x3FFF + l, LIT64(0xC90FDAA200000000));
			twopi2 = packFloatx80(0, 0x3FDD + l, LIT64(0x85A308D300000000));
			
			twoto63 = 0x5F000000;
			twoto63 |= xSign ? 0x80000000 : 0x00000000; // SIGN(INARG)*2^63 IN SGL
			
			fp2 = floatx80_mul(fp0, invtwopi, status);
			fp2 = floatx80_add(fp2, float32_to_floatx80(twoto63, status), status); // THE FRACTIONAL PART OF FP2 IS ROUNDED
			fp2 = floatx80_sub(fp2, float32_to_floatx80(twoto63, status), status); // FP2 is N
			fp4 = floatx80_mul(twopi1, fp2, status); // W = N*P1
			fp5 = floatx80_mul(twopi2, fp2, status); // w = N*P2
			fp3 = floatx80_add(fp4, fp5, status); // FP3 is P
			fp4 = floatx80_sub(fp4, fp3, status); // W-P
			fp0 = floatx80_sub(fp0, fp3, status); // FP0 is A := R - P
			fp4 = floatx80_add(fp4, fp5, status); // FP4 is p = (W-P)+w
			fp3 = fp0; // FP3 is A
			fp1 = floatx80_sub(fp1, fp4, status); // FP1 is a := r - p
			fp0 = floatx80_add(fp0, fp1, status); // FP0 is R := A+a
			
			if (endflag > 0) {
				n = floatx80_to_int32(fp2, status);
				goto tancont;
			}
			fp3 = floatx80_sub(fp3, fp0, status); // A-R
			fp1 = floatx80_add(fp1, fp3, status); // FP1 is r := (A-R)+a
			goto loop;
		} else {
			RESET_PREC;
			
			a = floatx80_move(a, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	} else {
		fp1 = floatx80_mul(fp0, float64_to_floatx80(LIT64(0x3FE45F306DC9C883), status), status); // X*2/PI
		
		n = floatx80_to_int32(fp1, status);
		j = 32 + n;
		
		fp0 = floatx80_sub(fp0, pi_tbl[j], status); // X-Y1
		fp0 = floatx80_sub(fp0, float32_to_floatx80(pi_tbl2[j], status), status); // FP0 IS R = (X-Y1)-Y2
		
	tancont:
		if (n & 1) {
			// NODD
			fp1 = fp0; // R
			fp0 = floatx80_mul(fp0, fp0, status); // S = R*R
			fp3 = float64_to_floatx80(LIT64(0x3EA0B759F50F8688), status); // Q4
			fp2 = float64_to_floatx80(LIT64(0xBEF2BAA5A8924F04), status); // P3
			fp3 = floatx80_mul(fp3, fp0, status); // SQ4
			fp2 = floatx80_mul(fp2, fp0, status); // SP3
			fp3 = floatx80_add(fp3, float64_to_floatx80(LIT64(0xBF346F59B39BA65F), status), status); // Q3+SQ4
			fp4 = packFloatx80(0, 0x3FF6, LIT64(0xE073D3FC199C4A00));
			fp2 = floatx80_add(fp2, fp4, status); // P2+SP3
			fp3 = floatx80_mul(fp3, fp0, status); // S(Q3+SQ4)
			fp2 = floatx80_mul(fp2, fp0, status); // S(P2+SP3)
			fp4 = packFloatx80(0, 0x3FF9, LIT64(0xD23CD68415D95FA1));
			fp3 = floatx80_add(fp3, fp4, status); // Q2+S(Q3+SQ4)
			fp4 = packFloatx80(1, 0x3FFC, LIT64(0x8895A6C5FB423BCA));
			fp2 = floatx80_add(fp2, fp4, status); // P1+S(P2+SP3)
			fp3 = floatx80_mul(fp3, fp0, status); // S(Q2+S(Q3+SQ4))
			fp2 = floatx80_mul(fp2, fp0, status); // S(P1+S(P2+SP3))
			fp4 = packFloatx80(1, 0x3FFD, LIT64(0xEEF57E0DA84BC8CE));
			fp3 = floatx80_add(fp3, fp4, status); // Q1+S(Q2+S(Q3+SQ4))
			fp2 = floatx80_mul(fp2, fp1, status); // RS(P1+S(P2+SP3))
			fp0 = floatx80_mul(fp0, fp3, status); // S(Q1+S(Q2+S(Q3+SQ4)))
			fp1 = floatx80_add(fp1, fp2, status); // R+RS(P1+S(P2+SP3))
			fp0 = floatx80_add(fp0, float32_to_floatx80(0x3F800000, status), status); // 1+S(Q1+S(Q2+S(Q3+SQ4)))
			
			xSign = extractFloatx80Sign(fp1);
			xExp = extractFloatx80Exp(fp1);
			xSig = extractFloatx80Frac(fp1);
			xSign ^= 1;
			fp1 = packFloatx80(xSign, xExp, xSig);

			RESET_PREC;
			
			a = floatx80_div(fp0, fp1, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		} else {
			fp1 = floatx80_mul(fp0, fp0, status); // S = R*R
			fp3 = float64_to_floatx80(LIT64(0x3EA0B759F50F8688), status); // Q4
			fp2 = float64_to_floatx80(LIT64(0xBEF2BAA5A8924F04), status); // P3
			fp3 = floatx80_mul(fp3, fp1, status); // SQ4
			fp2 = floatx80_mul(fp2, fp1, status); // SP3
			fp3 = floatx80_add(fp3, float64_to_floatx80(LIT64(0xBF346F59B39BA65F), status), status); // Q3+SQ4
			fp4 = packFloatx80(0, 0x3FF6, LIT64(0xE073D3FC199C4A00));
			fp2 = floatx80_add(fp2, fp4, status); // P2+SP3
			fp3 = floatx80_mul(fp3, fp1, status); // S(Q3+SQ4)
			fp2 = floatx80_mul(fp2, fp1, status); // S(P2+SP3)
			fp4 = packFloatx80(0, 0x3FF9, LIT64(0xD23CD68415D95FA1));
			fp3 = floatx80_add(fp3, fp4, status); // Q2+S(Q3+SQ4)
			fp4 = packFloatx80(1, 0x3FFC, LIT64(0x8895A6C5FB423BCA));
			fp2 = floatx80_add(fp2, fp4, status); // P1+S(P2+SP3)
			fp3 = floatx80_mul(fp3, fp1, status); // S(Q2+S(Q3+SQ4))
			fp2 = floatx80_mul(fp2, fp1, status); // S(P1+S(P2+SP3))
			fp4 = packFloatx80(1, 0x3FFD, LIT64(0xEEF57E0DA84BC8CE));
			fp3 = floatx80_add(fp3, fp4, status); // Q1+S(Q2+S(Q3+SQ4))
			fp2 = floatx80_mul(fp2, fp0, status); // RS(P1+S(P2+SP3))
			fp1 = floatx80_mul(fp1, fp3, status); // S(Q1+S(Q2+S(Q3+SQ4)))
			fp0 = floatx80_add(fp0, fp2, status); // R+RS(P1+S(P2+SP3))
			fp1 = floatx80_add(fp1, float32_to_floatx80(0x3F800000, status), status); // 1+S(Q1+S(Q2+S(Q3+SQ4)))
			
			RESET_PREC;
			
			a = floatx80_div(fp0, fp1, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	}
}

/*----------------------------------------------------------------------------
 | Hyperbolic tangent
 *----------------------------------------------------------------------------*/

floatx80 floatx80_tanh(floatx80 a, float_status *status)
{
	flag aSign, vSign;
	int32_t aExp, vExp;
	uint64_t aSig, vSig;
	
	int32_t compact;
	floatx80 fp0, fp1;
	float32 sign;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		return packFloatx80(aSign, one_exp, one_sig);
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(aSign, 0, 0);
	}
	
	SET_PREC;
	
	compact = floatx80_make_compact(aExp, aSig);
	
	if (compact < 0x3FD78000 || compact > 0x3FFFDDCE) {
		// TANHBORS
		if (compact < 0x3FFF8000) {
			// TANHSM
			RESET_PREC;
			
			a = floatx80_move(a, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		} else {
			if (compact > 0x40048AA1) {
				// TANHHUGE
				sign = 0x3F800000;
				sign |= aSign ? 0x80000000 : 0x00000000;
				fp0 = float32_to_floatx80(sign, status);
				sign &= 0x80000000;
				sign ^= 0x80800000; // -SIGN(X)*EPS
				
				RESET_PREC;
				
				a = floatx80_add(fp0, float32_to_floatx80(sign, status), status);
				
				float_raise(float_flag_inexact, status);
				
				return a;
			} else {
				fp0 = packFloatx80(0, aExp+1, aSig); // Y = 2|X|
				fp0 = floatx80_etox(fp0, status); // FP0 IS EXP(Y)
				fp0 = floatx80_add(fp0, float32_to_floatx80(0x3F800000, status), status); // EXP(Y)+1
				sign = aSign ? 0x80000000 : 0x00000000;
				fp1 = floatx80_div(float32_to_floatx80(sign^0xC0000000, status), fp0, status); // -SIGN(X)*2 / [EXP(Y)+1]
				fp0 = float32_to_floatx80(sign | 0x3F800000, status); // SIGN
				
				RESET_PREC;
				
				a = floatx80_add(fp1, fp0, status);
				
				float_raise(float_flag_inexact, status);
				
				return a;
			}
		}
	} else { // 2**(-40) < |X| < (5/2)LOG2
		fp0 = packFloatx80(0, aExp+1, aSig); // Y = 2|X|
		fp0 = floatx80_etoxm1(fp0, status); // FP0 IS Z = EXPM1(Y)
		fp1 = floatx80_add(fp0, float32_to_floatx80(0x40000000, status), status); // Z+2
		
		vSign = extractFloatx80Sign(fp1);
		vExp = extractFloatx80Exp(fp1);
		vSig = extractFloatx80Frac(fp1);
		
		fp1 = packFloatx80(vSign ^ aSign, vExp, vSig);
		
		RESET_PREC;
		
		a = floatx80_div(fp0, fp1, status);
		
		float_raise(float_flag_inexact, status);
		
		return a;
	}
}

/*----------------------------------------------------------------------------
 | 10 to x
 *----------------------------------------------------------------------------*/

floatx80 floatx80_tentox(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	int32_t compact, n, j, l, m, m1;
	floatx80 fp0, fp1, fp2, fp3, adjfact, fact1, fact2;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		if (aSign) return packFloatx80(0, 0, 0);
		return a;
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(0, one_exp, one_sig);
	}
	
	SET_PREC;
	
	fp0 = a;
	
	compact = floatx80_make_compact(aExp, aSig);
	
	if (compact < 0x3FB98000 || compact > 0x400B9B07) { // |X| > 16480 LOG2/LOG10 or |X| < 2^(-70)
		if (compact > 0x3FFF8000) { // |X| > 16480
			RESET_PREC;
			
			if (aSign) {
				return roundAndPackFloatx80(status->floatx80_rounding_precision, 0, -0x1000, aSig, 0, status);
			} else {
				return roundAndPackFloatx80(status->floatx80_rounding_precision, 0, 0x8000, aSig, 0, status);
			}
		} else { // |X| < 2^(-70)
			RESET_PREC;
			
			a = floatx80_add(fp0, float32_to_floatx80(0x3F800000, status), status); // 1 + X
			
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	} else { // 2^(-70) <= |X| <= 16480 LOG 2 / LOG 10
		fp1 = fp0; // X
		fp1 = floatx80_mul(fp1, float64_to_floatx80(LIT64(0x406A934F0979A371), status), status); // X*64*LOG10/LOG2
		n = floatx80_to_int32(fp1, status); // N=INT(X*64*LOG10/LOG2)
		fp1 = int32_to_floatx80(n);

		j = n & 0x3F;
		l = n / 64; // NOTE: this is really arithmetic right shift by 6
		if (n < 0 && j) { // arithmetic right shift is division and round towards minus infinity
			l--;
		}
		m = l / 2; // NOTE: this is really arithmetic right shift by 1
		if (l < 0 && (l & 1)) { // arithmetic right shift is division and round towards minus infinity
			m--;
		}
		m1 = l - m;
		m1 += 0x3FFF; // ADJFACT IS 2^(M')

		adjfact = packFloatx80(0, m1, one_sig);
		fact1 = exp2_tbl[j];
		fact1.high += m;
		fact2.high = exp2_tbl2[j]>>16;
		fact2.high += m;
		fact2.low = (uint64_t)(exp2_tbl2[j] & 0xFFFF);
		fact2.low <<= 48;
		
		fp2 = fp1; // N
		fp1 = floatx80_mul(fp1, float64_to_floatx80(LIT64(0x3F734413509F8000), status), status); // N*(LOG2/64LOG10)_LEAD
		fp3 = packFloatx80(1, 0x3FCD, LIT64(0xC0219DC1DA994FD2));
		fp2 = floatx80_mul(fp2, fp3, status); // N*(LOG2/64LOG10)_TRAIL
		fp0 = floatx80_sub(fp0, fp1, status); // X - N L_LEAD
		fp0 = floatx80_sub(fp0, fp2, status); // X - N L_TRAIL
		fp2 = packFloatx80(0, 0x4000, LIT64(0x935D8DDDAAA8AC17)); // LOG10
		fp0 = floatx80_mul(fp0, fp2, status); // R
		
		// EXPR
		fp1 = floatx80_mul(fp0, fp0, status); // S = R*R
		fp2 = float64_to_floatx80(LIT64(0x3F56C16D6F7BD0B2), status); // A5
		fp3 = float64_to_floatx80(LIT64(0x3F811112302C712C), status); // A4
		fp2 = floatx80_mul(fp2, fp1, status); // S*A5
		fp3 = floatx80_mul(fp3, fp1, status); // S*A4
		fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3FA5555555554CC1), status), status); // A3+S*A5
		fp3 = floatx80_add(fp3, float64_to_floatx80(LIT64(0x3FC5555555554A54), status), status); // A2+S*A4
		fp2 = floatx80_mul(fp2, fp1, status); // S*(A3+S*A5)
		fp3 = floatx80_mul(fp3, fp1, status); // S*(A2+S*A4)
		fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3FE0000000000000), status), status); // A1+S*(A3+S*A5)
		fp3 = floatx80_mul(fp3, fp0, status); // R*S*(A2+S*A4)
		
		fp2 = floatx80_mul(fp2, fp1, status); // S*(A1+S*(A3+S*A5))
		fp0 = floatx80_add(fp0, fp3, status); // R+R*S*(A2+S*A4)
		fp0 = floatx80_add(fp0, fp2, status); // EXP(R) - 1
		
		fp0 = floatx80_mul(fp0, fact1, status);
		fp0 = floatx80_add(fp0, fact2, status);
		fp0 = floatx80_add(fp0, fact1, status);
		
		RESET_PREC;

		a = floatx80_mul(fp0, adjfact, status);
		
		float_raise(float_flag_inexact, status);
		
		return a;
	}
}

/*----------------------------------------------------------------------------
 | 2 to x
 *----------------------------------------------------------------------------*/

floatx80 floatx80_twotox(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	int32_t compact, n, j, l, m, m1;
	floatx80 fp0, fp1, fp2, fp3, adjfact, fact1, fact2;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		if (aSign) return packFloatx80(0, 0, 0);
		return a;
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(0, one_exp, one_sig);
	}
	
	SET_PREC;
	
	fp0 = a;
	
	compact = floatx80_make_compact(aExp, aSig);
	
	if (compact < 0x3FB98000 || compact > 0x400D80C0) { // |X| > 16480 or |X| < 2^(-70)
		if (compact > 0x3FFF8000) { // |X| > 16480
			RESET_PREC;;
			
			if (aSign) {
				return roundAndPackFloatx80(status->floatx80_rounding_precision, 0, -0x1000, aSig, 0, status);
			} else {
				return roundAndPackFloatx80(status->floatx80_rounding_precision, 0, 0x8000, aSig, 0, status);
			}
		} else { // |X| < 2^(-70)
			RESET_PREC;;
			
			a = floatx80_add(fp0, float32_to_floatx80(0x3F800000, status), status); // 1 + X
			
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	} else { // 2^(-70) <= |X| <= 16480
		fp1 = fp0; // X
		fp1 = floatx80_mul(fp1, float32_to_floatx80(0x42800000, status), status); // X * 64
		n = floatx80_to_int32(fp1, status);
		fp1 = int32_to_floatx80(n);
		j = n & 0x3F;
		l = n / 64; // NOTE: this is really arithmetic right shift by 6
		if (n < 0 && j) { // arithmetic right shift is division and round towards minus infinity
			l--;
		}
		m = l / 2; // NOTE: this is really arithmetic right shift by 1
		if (l < 0 && (l & 1)) { // arithmetic right shift is division and round towards minus infinity
			m--;
		}
		m1 = l - m;
		m1 += 0x3FFF; // ADJFACT IS 2^(M')
		
		adjfact = packFloatx80(0, m1, one_sig);
		fact1 = exp2_tbl[j];
		fact1.high += m;
		fact2.high = exp2_tbl2[j]>>16;
		fact2.high += m;
		fact2.low = (uint64_t)(exp2_tbl2[j] & 0xFFFF);
		fact2.low <<= 48;
		
		fp1 = floatx80_mul(fp1, float32_to_floatx80(0x3C800000, status), status); // (1/64)*N
		fp0 = floatx80_sub(fp0, fp1, status); // X - (1/64)*INT(64 X)
		fp2 = packFloatx80(0, 0x3FFE, LIT64(0xB17217F7D1CF79AC)); // LOG2
		fp0 = floatx80_mul(fp0, fp2, status); // R
		
		// EXPR
		fp1 = floatx80_mul(fp0, fp0, status); // S = R*R
		fp2 = float64_to_floatx80(LIT64(0x3F56C16D6F7BD0B2), status); // A5
		fp3 = float64_to_floatx80(LIT64(0x3F811112302C712C), status); // A4
		fp2 = floatx80_mul(fp2, fp1, status); // S*A5
		fp3 = floatx80_mul(fp3, fp1, status); // S*A4
		fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3FA5555555554CC1), status), status); // A3+S*A5
		fp3 = floatx80_add(fp3, float64_to_floatx80(LIT64(0x3FC5555555554A54), status), status); // A2+S*A4
		fp2 = floatx80_mul(fp2, fp1, status); // S*(A3+S*A5)
		fp3 = floatx80_mul(fp3, fp1, status); // S*(A2+S*A4)
		fp2 = floatx80_add(fp2, float64_to_floatx80(LIT64(0x3FE0000000000000), status), status); // A1+S*(A3+S*A5)
		fp3 = floatx80_mul(fp3, fp0, status); // R*S*(A2+S*A4)
		
		fp2 = floatx80_mul(fp2, fp1, status); // S*(A1+S*(A3+S*A5))
		fp0 = floatx80_add(fp0, fp3, status); // R+R*S*(A2+S*A4)
		fp0 = floatx80_add(fp0, fp2, status); // EXP(R) - 1
		
		fp0 = floatx80_mul(fp0, fact1, status);
		fp0 = floatx80_add(fp0, fact2, status);
		fp0 = floatx80_add(fp0, fact1, status);
		
		RESET_PREC;
		
		a = floatx80_mul(fp0, adjfact, status);
		
		float_raise(float_flag_inexact, status);
		
		return a;
	}
}
