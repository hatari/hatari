/*
  Hatari - misc.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Misc functions
*/
char Misc_rcsid[] = "Hatari $Id: misc.c,v 1.10 2005-02-13 16:18:49 thothy Exp $";

#include <ctype.h>

#include "main.h"
#include "misc.h"


static long RandomNum;


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
   Convert a string to uppercase.
*/
void Misc_strupr(char *pString)
{
  while(*pString)
  {
    *pString = toupper(*pString);
    pString++;
  }
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
  return (((Value/10))<<4) | (Value%10);
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
static long Misc_NextLongRand(long Seed)
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

