/*
  Hatari - tos.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_TOS_H
#define HATARI_TOS_H

/* Standard available ST memory configurations */
enum
{
  MEMORYSIZE_512,
  MEMORYSIZE_1024,
  MEMORYSIZE_2MB,
  MEMORYSIZE_4MB
};

/* List of TOS settings for different memory size */
typedef struct
{
  unsigned long PhysTop;    /* phys top */
  int MemoryConfig;         /* %00=128k %01=512k %10=2Mb %11=reserved. eg %1010 = 4Mb */
  unsigned long MemoryEnd;  /* Above this address causes a BusError */
} MEMORY_INFO;


extern Uint16 TosVersion;
extern Uint32 TosAddress, TosSize;
extern BOOL bTosImageLoaded;
extern unsigned int ConnectedDriveMask;

extern void TOS_MemorySnapShot_Capture(BOOL bSave);
extern int TOS_LoadImage(void);

#endif
