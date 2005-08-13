/*
  Hatari - tos.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_TOS_H
#define HATARI_TOS_H

extern Uint16 TosVersion;
extern Uint32 TosAddress, TosSize;
extern BOOL bTosImageLoaded;
extern unsigned int ConnectedDriveMask;

extern void TOS_MemorySnapShot_Capture(BOOL bSave);
extern int TOS_LoadImage(void);

#endif
