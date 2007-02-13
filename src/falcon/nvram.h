/*
  Hatari - nvram.h
 
  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Atari TT and Falcon NVRAM and RTC emulation code - declarations
*/

#ifndef HATARI_NVRAM_H
#define HATARI_NVRAM_H

void NvRam_Reset(void);
void NvRam_Init(void);
void NvRam_UnInit(void);
void NvRam_Select_ReadByte(void);
void NvRam_Select_WriteByte(void);
void NvRam_Data_ReadByte(void);
void NvRam_Data_WriteByte(void);

#endif /* HATARI_NVRAM_H */
