/*
  Hatari - str.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_STR_H
#define HATARI_STR_H

#include "config.h"
#include <string.h>
#if HAVE_STRINGS_H
# include <strings.h>
#endif

/* Define this only for an old Linux system which does not store
 * pathnames in UTF-8. If this is defined, pathnames are converted
 * to the host character set as defined by the locale.
 * Do not define this for OSX, as the unicode pathnames then won't
 * be converted from the decomposed to the precomposed form.
 *
 * TODO: this should have option / be set in CMake config.
 */
/* #define USE_LOCALE_CHARSET 1 */


/* Invalid characters in paths & filenames are replaced by this,
 * a valid, but uncommon GEMDOS file name character,
 * which hopefully shouldn't cause problems in:
 * - TOS *.INF files used for autostarting
 * - GEM file selectors (TOS or replacement ones)
 * - path/file handling code of common programming languages
 */
#define INVALID_CHAR '+'

#define Str_Free(s) { free(s); s = NULL; }

extern char *Str_Trim(char *buffer);
extern char *Str_ToUpper(char *pString);
extern char *Str_ToLower(char *pString);
extern char *Str_Alloc(int len);
extern char *Str_Dup(const char *str);
extern long Str_Copy(char *pDest, const char *pSrc, long nBufLen);
extern char *Str_Trunc(char *str);
extern bool Str_IsHex(const char *str);
extern void Str_UnEscape(char *str);
extern void Str_Filename2TOSname(const char *src, char *dst);
extern void Str_Dump_Hex_Ascii ( char *p , int Len , int Width , const char *Suffix , FILE *pFile );

/* Interface of character set conversions */
extern void Str_AtariToHost(const char *source, char *dest, int destLen, char replacementChar);
extern void Str_DecomposedToPrecomposedUtf8(const char *source, char *dest);


#endif  /* HATARI_STR_H */
