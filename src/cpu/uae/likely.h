#ifndef UAE_LIKELY_H
#define UAE_LIKELY_H

#ifndef WINUAE_FOR_HATARI		/* not necessary for Hatari, already done in main.h */
#ifdef HAVE___BUILTIN_EXPECT

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#else

#define likely(x)   x
#define unlikely(x) x

#endif
#endif	/* !WINUAE_FOR_HATARI */

#endif /* UAE_LIKELY_H */
