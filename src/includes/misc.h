/*
  Hatari - misc.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_MISC_H
#define HATARI_MISC_H

extern void Misc_FindWindowsVersion(void);
extern void Misc_PadStringWithSpaces(char *pszString,int nChars);
extern void Misc_RemoveSpacesFromString(char *pszSrcString, char *pszDestString);
extern void Misc_RemoveWhiteSpace(char *pszString,int Length);
extern void Misc_FindWorkingDirectory(char *prgname);
extern int Misc_LimitInt(int Value, int MinRange, int MaxRange);
extern unsigned char Misc_ConvertToBCD(unsigned short int Value);
extern void Misc_SeedRandom(unsigned long Seed);
extern long Misc_GetRandom(void);

#endif
