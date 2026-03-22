/*
  Hatari - endianswap.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_ENDIANSWAP_H
#define HATARI_ENDIANSWAP_H

#include "config.h"

/*
 * Byte-swapping functions
 */

/* Try to use system bswap_16/bswap_32 functions. */
#if defined HAVE_BSWAP_16 && defined HAVE_BSWAP_32
# ifdef HAVE_BYTESWAP_H
#  include <byteswap.h>
# endif
#else
/* Otherwise, we'll roll our own. */
#  ifndef bswap_16
static inline uint16_t bswap_16(uint16_t x)
{
	return (((x) >> 8) | (((x) & 0xFF) << 8));
}
#    define bswap_16 bswap_16
#  endif
#  ifndef bswap_32
static inline uint32_t bswap_32(uint32_t x)
{
	return (((x) << 24) | (((x) << 8) & 0x00FF0000) |
	       (((x) >> 8) & 0x0000FF00) | ((x) >> 24));
}
#    define bswap_32 bswap_32
#  endif
#endif

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)

#define be_swap32(x) ((uint32_t)(x))
#define be_swap16(x) ((uint16_t)(x))

#define le_swap32(x) bswap_32(x)
#define le_swap16(x) bswap_16(x)

#else

#define be_swap32(x) bswap_32(x)
#define be_swap16(x) bswap_16(x)

#define le_swap32(x) ((uint32_t)(x))
#define le_swap16(x) ((uint16_t)(x))

#endif


#endif  /* HATARI_ENDIANSWAP_H */
