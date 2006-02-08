/*
  Hatari - stMemory.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  ST Memory access functions.
*/
const char STMemory_rcsid[] = "Hatari $Id: stMemory.c,v 1.8 2006-02-08 22:49:27 eerot Exp $";

#include "stMemory.h"


Uint8 STRam[16*1024*1024];      /* This is our ST Ram, includes all TOS/hardware areas for ease */
Uint32 STRamEnd;                /* End of ST Ram, above this address is no-mans-land and hardware vectors */


/*-----------------------------------------------------------------------*/
/*
  Clear section of ST's memory space.
*/
void STMemory_Clear(Uint32 StartAddress, Uint32 EndAddress)
{
	memset(&STRam[StartAddress], 0, EndAddress-StartAddress);
}

