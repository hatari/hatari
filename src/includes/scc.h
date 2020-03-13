/*
 * Hatari scc.h
 *
 * 85C30 emulation code - declarations
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 */

#ifndef SCC_H
#define SCC_H

bool SCC_IsAvailable(CNF_PARAMS *cnf);
void SCC_Init(void);
void SCC_UnInit(void);
void SCC_MemorySnapShot_Capture(bool bSave);
void SCC_Reset(void);
void SCC_IRQ(void);
int SCC_doInterrupt(void);
void SCC_IoMem_ReadByte(void);
void SCC_IoMem_WriteByte(void);
void SCC_Info(FILE *fp, Uint32 dummy);

#endif /* SCC_H */
