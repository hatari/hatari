/*
  Hatari - stMemory.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  ST Memory access functions - take care of endian swaps.
*/
char STMemory_rcsid[] = "Hatari $Id: stMemory.c,v 1.5 2004-04-19 08:53:47 thothy Exp $";

#include "stMemory.h"


/*-----------------------------------------------------------------------*/
/*
  Clear section of ST's memory space.
*/
void STMemory_Clear(unsigned long StartAddress, unsigned long EndAddress)
{
  memset((void *)((unsigned long)STRam+StartAddress), 0, EndAddress-StartAddress);
}

