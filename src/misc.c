/*
  Hatari - misc.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Misc functions
*/
const char Misc_rcsid[] = "Hatari $Id: misc.c,v 1.15 2007-12-19 11:13:28 thothy Exp $";

#include <ctype.h>

#include "main.h"
#include "misc.h"


/*-----------------------------------------------------------------------*/
/**
 * Remove 'white-space' from beginning of text string
 */
void Misc_RemoveWhiteSpace(char *pszString,int Length)
{
	while ((*pszString==' ') || (*pszString=='\t'))
	{
		/* Copy line left one character */
		memmove(pszString, pszString+1, Length-1);
	}
}


/*-----------------------------------------------------------------------*/
/**
 *  Convert a string to uppercase.
 */
void Misc_strupr(char *pString)
{
	while (*pString)
	{
		*pString = toupper(*pString);
		pString++;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Limit integer between min/max range
 */
int Misc_LimitInt(int Value, int MinRange, int MaxRange)
{
	if (Value < MinRange)
		Value = MinRange;
	else if (Value > MaxRange)
		Value = MaxRange;

	return Value;
}


/*-----------------------------------------------------------------------*/
/**
 * Convert value to 2-digit BCD
 */
unsigned char Misc_ConvertToBCD(unsigned short int Value)
{
	return (((Value/10))<<4) | (Value%10);
}
