/*
  Hatari - riebl.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_RIEBL_H
#define HATARI_RIEBL_H

#include "sysdeps.h"

#define RIEBL_ADDR_DEFAULT	0x010000	/* A24 address of the card's memory */

extern bool Riebl_IsEnabled(void);
extern void Riebl_Apply(void);
extern void Riebl_UnInit(void);
extern void Riebl_Reset(bool bCold);
extern bool Riebl_Decode(uaecptr addr, uint32_t *pOffset);
extern uint8_t Riebl_ReadByte(uint32_t offset);
extern uint16_t Riebl_ReadWord(uint32_t offset);
extern void Riebl_WriteByte(uint32_t offset, uint8_t val);
extern void Riebl_WriteWord(uint32_t offset, uint16_t val);
extern bool Riebl_GetIRQ(void);
extern int Riebl_Process_IACK(void);
extern void Riebl_Vbl(void);
extern void Riebl_MemorySnapShot_Capture(bool bSave);

#endif /* HATARI_RIEBL_H */
