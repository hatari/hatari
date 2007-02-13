/*
  Hatari - nvram.h
 
  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Atari TT and Falcon NVRAM and RTC emulation code - declarations
*/

#ifndef HATARI_NVRAM_H
#define HATARI_NVRAM_H

/* some constants to give NVRAm locations symbolic names */
#define NVRAM_SECONDS	0
#define NVRAM_MINUTES	2
#define NVRAM_HOURS	4
#define NVRAM_DAY	7
#define NVRAM_MONTH	8
#define NVRAM_YEAR	9

/* FIXME: give better names to the OS selector cells */
#define NVRAM_OS1	14
#define NVRAM_OS2	15

#define NVRAM_LANGUAGE	20
#define NVRAM_KEYBOARDLAYOUT 21
#define NVRAM_TIMEFORMAT 22
#define NVRAM_DATESEPERATOR 23

#define NVRAM_BOOTDELAY 24
#define NVRAM_VIDEOMODE 28
#define NVRAM_MONITOR	29

#define NVRAM_SCSI	30

/* FIXME: give better names to these (maybe byte order if there is any?) 
 * keep track on NvRam_SetChecksum()!
 */
#define NVRAM_CHKSUM1	62
#define NVRAM_CHKSUM2	63

void NvRam_Reset(void);
void NvRam_Init(void);
void NvRam_UnInit(void);
void NvRam_Select_ReadByte(void);
void NvRam_Select_WriteByte(void);
void NvRam_Data_ReadByte(void);
void NvRam_Data_WriteByte(void);

#endif /* HATARI_NVRAM_H */
