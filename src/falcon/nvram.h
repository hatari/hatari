/*
  Hatari - nvram.h
 
  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Atari TT and Falcon NVRAM and RTC emulation code - declarations
*/

#ifndef HATARI_NVRAM_H
#define HATARI_NVRAM_H

/* some constants to give NVRAM locations symbolic names */
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

#define NVRAM_BOOTDELAY	24
#define NVRAM_VMODE1	28
#define NVRAM_VMODE2	29

#define NVRAM_SCSI	30

/* FIXME: give better names to these (maybe byte order if there is any?) 
 * keep track on NvRam_SetChecksum()!
 */
#define NVRAM_CHKSUM1	62
#define NVRAM_CHKSUM2	63

extern void NvRam_Reset(void);
extern void NvRam_Init(void);
extern void NvRam_UnInit(void);
extern void NvRam_Select_ReadByte(void);
extern void NvRam_Select_WriteByte(void);
extern void NvRam_Data_ReadByte(void);
extern void NvRam_Data_WriteByte(void);

#endif /* HATARI_NVRAM_H */
