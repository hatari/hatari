/*
  Hatari

  Misc functions
*/

#include "main.h"
#include "debug.h"
#include "errlog.h"
#include "file.h"
#include "m68000.h"
#include "memAlloc.h"
#include "misc.h"

long RandomNum;


/*-----------------------------------------------------------------------*/
/*
  Fill end of string out with spaces
*/
void Misc_PadStringWithSpaces(char *pszString,int nChars)
{
  int i;

  for(i=nChars; i>=(int)strlen(pszString); i--) {
    pszString[i] = ' ';
  }
  pszString[nChars] = '\0';
}


/*-----------------------------------------------------------------------*/
/*
  Remove any spaces from string
*/
void Misc_RemoveSpacesFromString(char *pszSrcString, char *pszDestString)
{
  int i=0,j=0;

  /* Copy string */
  while(pszSrcString[i]!='\0') {
    if (pszSrcString[i]!=' ') {  /* Copy character if not a white-space */
      pszDestString[j] = pszSrcString[i];
      j++;
    }
    i++;
  }

  pszDestString[j] = '\0';      /* Term */
}


/*-----------------------------------------------------------------------*/
/*
  Remove 'white-space' from beginning of text string
*/
void Misc_RemoveWhiteSpace(char *pszString,int Length)
{
  while( (*pszString==' ') || (*pszString=='\t') ) {
    /* Copy line left one character */
    memmove(pszString,pszString+1,Length-1);
  }
}


/*-----------------------------------------------------------------------*/
/*
  Find working directory, and store to 'szWorkingDir'
*/
void Misc_FindWorkingDirectory(void)
{
/* FIXME!!!!*/
/*
  char szSrcDrive[_MAX_DRIVE],szSrcDir[_MAX_DIR],szSrcName[_MAX_FNAME],szSrcExt[_MAX_EXT];

  // Find name of '.exe'
  GetModuleFileName(NULL,szWorkingDir,MAX_FILENAME_LENGTH);
  _splitpath(szWorkingDir,szSrcDrive,szSrcDir,szSrcName,szSrcExt);
  _makepath(szWorkingDir,szSrcDrive,szSrcDir,"","");
  // And remove trailing backslash
  if (strlen(szWorkingDir)>0) {
    if (szWorkingDir[strlen(szWorkingDir)-1]=='/')
      szWorkingDir[strlen(szWorkingDir)-1]='\0';
  }
*/
}


/*-----------------------------------------------------------------------*/
/*
  Limit integer between min/max range
*/
int Misc_LimitInt(int Value, int MinRange, int MaxRange)
{
  if (Value<MinRange)
    Value = MinRange;
  else if (Value>MaxRange)
    Value = MaxRange;

  return(Value);
}


/*-----------------------------------------------------------------------*/
/*
  Convert value to 2-digit BCD
*/
unsigned char Misc_ConvertToBCD(unsigned short int Value)
{
  return( ((Value&0xf0)>>4)*10 + (Value&0x0f) );
}


/*-----------------------------------------------------------------------*/
/*
  See own random number(must be !=0)
*/
void Misc_SeedRandom(unsigned long Seed)
{
  RandomNum = Seed;
}


/*-----------------------------------------------------------------------*/
/*
  Get mext random number
*/
long Misc_NextLongRand(long Seed)
{
  unsigned long Lo, Hi;

  Lo = 16807 * (long)(Seed & 0xffff);
  Hi = 16807 * (long)((unsigned long)Seed >> 16);
  Lo += (Hi & 0x7fff) << 16;
  if (Lo > 2147483647L) {
    Lo &= 2147483647L;
    ++Lo;
  }
  Lo += Hi >> 15;
  if (Lo > 2147483647L) {
    Lo &= 2147483647L;
    ++Lo;
  }
  return((long)Lo);
}


/*-----------------------------------------------------------------------*/
/*
  Get own random number
*/
long Misc_GetRandom(void)
{
  RandomNum = Misc_NextLongRand(RandomNum);
  return(RandomNum);
}


/*-----------------------------------------------------------------------*/
/*
  Convert Time/Date to DOS format
*/
/*
void Misc_TimeDataToDos(FILETIME *pFileTime, WORD *pFatDate, WORD *pFatTime)
{
  // Convert FILETIME to DOS format(same as GemDOS format)
  if (FileTimeToDosDateTime(pFileTime,pFatDate,pFatTime))
    return;

  // Ooops, date/time outside range so just NULL
  *pFatDate = *pFatTime = 0;
}
*/
