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

char *Str_Trim(char *buffer);
char *Str_ToUpper(char *pString);
char *Str_ToLower(char *pString);
char *Str_Trunc(char *str);
bool Str_IsHex(const char *str);

#endif  /* HATARI_STR_H */
