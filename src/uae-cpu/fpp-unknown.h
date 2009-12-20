 /*
  * UAE - The Un*x Amiga Emulator
  *
  * MC68881 emulation
  *
  * Conversion routines for hosts with unknown floating point format.
  *
  * Copyright 1996 Herman ten Brugge
  */

#include <SDL_endian.h>

typedef double	fpu_register;

typedef union {
	fpu_register val;
	uae_u32 parts[sizeof(fpu_register) / 4];
} fpu_register_parts;

enum {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	FHI		= 0,
	FLO		= 1
#else
	FHI		= 1,
	FLO		= 0
#endif
};


#ifndef HAVE_to_single
STATIC_INLINE double to_single(uae_u32 value)
{
	uae_u32 sign;
	uae_u32 expon;
	fpu_register_parts result;

	if ((value & 0x7fffffff) == 0)
		return (0.0);

	sign = (value & 0x80000000);
	expon  = ((value & 0x7F800000) >> 23) + 1023 - 127;

	result.parts[FLO] = value << 29;
	result.parts[FHI] = sign | (expon << 20) | ((value & 0x007FFFFF) >> 3);

	return result.val;
}
#endif

#ifndef HAVE_from_single
STATIC_INLINE uae_u32 from_single(double src)
{
	uae_u32 sign;
	uae_u32 expon;
	uae_u32 result;
	fpu_register_parts const *p = (fpu_register_parts const *)&src;

	if (src == 0.0)
		return 0;

	sign  = (p->parts[FHI] & 0x80000000);
	expon = (p->parts[FHI] & 0x7FF00000) >> 20;

	if (expon + 127 < 1023) {
		expon = 0;
	} else if (expon > 1023 + 127) {
		expon = 255;
	} else {
		expon = expon + 127 - 1023;
	}

	result = sign | (expon << 23) | ((p->parts[FHI] & 0x000FFFFF) << 3) | (p->parts[FLO] >> 29);

	return result;
}
#endif

#ifndef HAVE_to_exten
STATIC_INLINE double to_exten(uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3)
{
    double frac;

    if ((wrd1 & 0x7fff0000) == 0 && wrd2 == 0 && wrd3 == 0)
        return 0.0;
    frac = (double) wrd2 / 2147483648.0 +
        (double) wrd3 / 9223372036854775808.0;
    if (wrd1 & 0x80000000)
        frac = -frac;
    return ldexp (frac, ((wrd1 >> 16) & 0x7fff) - 16383);
}
#endif

#ifndef HAVE_from_exten
STATIC_INLINE void from_exten(double src, uae_u32 * wrd1, uae_u32 * wrd2, uae_u32 * wrd3)
{
    int expon;
    double frac;

    if (src == 0.0) {
        *wrd1 = 0;
        *wrd2 = 0;
        *wrd3 = 0;
        return;
    }
    if (src < 0) {
        *wrd1 = 0x80000000;
        src = -src;
    } else {
        *wrd1 = 0;
    }
    frac = frexp (src, &expon);
    frac += 0.5 / 18446744073709551616.0;
    if (frac >= 1.0) {
        frac /= 2.0;
        expon++;
    }
    *wrd1 |= (((expon + 16383 - 1) & 0x7fff) << 16);
    *wrd2 = (uae_u32) (frac * 4294967296.0);
    *wrd3 = (uae_u32) (frac * 18446744073709551616.0 - *wrd2 * 4294967296.0);
}
#endif

#ifndef HAVE_to_double
STATIC_INLINE double to_double(uae_u32 wrd1, uae_u32 wrd2)
{
	fpu_register_parts result;

	if ((wrd1 & 0x7fffffff) == 0 && wrd2 == 0)
		return 0.0;

	result.parts[FLO] = wrd2;
	result.parts[FHI] = wrd1;

	return result.val;
}
#endif

#ifndef HAVE_from_double
STATIC_INLINE void from_double(double src, uae_u32 * wrd1, uae_u32 * wrd2)
{
/*
	if (src == 0.0) {
		*wrd1 = *wrd2 = 0;
		return;
	}
*/
	fpu_register_parts const *p = (fpu_register_parts const *)&src;
	*wrd2 = p->parts[FLO];
	*wrd1 = p->parts[FHI];
}
#endif
