/*
  Hatari - str.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_STR_H
#define HATARI_STR_H

#include <string.h>
#include <config.h>
#if HAVE_STRINGS_H
# include <strings.h>
#endif

extern char *Str_Trim(char *buffer);
extern char *Str_ToUpper(char *pString);
extern char *Str_ToLower(char *pString);
extern char *Str_Trunc(char *str);
extern bool Str_IsHex(const char *str);
extern bool Str_GetNumber(const char *value, Uint32 *number);
extern int Str_ParseRange(char *str, Uint32 *lower, Uint32 *upper);

#endif  /* HATARI_STR_H */
