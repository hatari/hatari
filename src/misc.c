/*
  Hatari - misc.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Misc functions
*/
const char Misc_rcsid[] = "Hatari $Id: misc.c,v 1.16 2008-04-13 22:11:37 thothy Exp $";

#include "main.h"
#include "misc.h"


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
