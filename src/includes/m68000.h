/*
  Hatari - m68000.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_M68000_H
#define HATARI_M68000_H

extern Uint32 BusAddressLocation;
extern Uint32 BusErrorPC;
extern Uint16 BusErrorOpcode;
extern BOOL bBusErrorReadWrite;

extern void M68000_Reset(BOOL bCold);
extern void M68000_MemorySnapShot_Capture(BOOL bSave);
extern void M68000_BusError(unsigned long addr, BOOL bReadWrite);
extern void M68000_AddressError(unsigned long addr);
extern void M68000_Exception(Uint32 ExceptionVector);
extern void M68000_WaitState(void);

#endif
