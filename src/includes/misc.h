/*
  Hatari
*/

extern void Misc_FindWindowsVersion(void);
extern void Misc_PadStringWithSpaces(char *pszString,int nChars);
extern void Misc_RemoveSpacesFromString(char *pszSrcString, char *pszDestString);
extern void Misc_RemoveWhiteSpace(char *pszString,int Length);
extern void Misc_FindWorkingDirectory(void);
extern int Misc_LimitInt(int Value, int MinRange, int MaxRange);
extern unsigned char Misc_ConvertToBCD(unsigned short int Value);
extern void Misc_SeedRandom(unsigned long Seed);
extern long Misc_GetRandom(void);
//extern void Misc_TimeDataToDos(FILETIME *pFileTime, WORD *pFatDate, WORD *pFatTime);
