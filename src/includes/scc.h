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

#define	SCC_IRQ_ON				0	/* O/low sets IRQ line */
#define	SCC_IRQ_OFF				1	/* 1/high clears IRQ line */


bool SCC_IsAvailable(CNF_PARAMS *cnf);
void SCC_Check_Lan_IsEnabled ( void );
void SCC_Init(void);
void SCC_UnInit(void);
void SCC_MemorySnapShot_Capture(bool bSave);
void SCC_Reset(void);

void SCC_InterruptHandler_BRG_A(void);
void SCC_InterruptHandler_BRG_B(void);
void SCC_InterruptHandler_TX_RX_A(void);
void SCC_InterruptHandler_TX_RX_B(void);
void SCC_InterruptHandler_RX_A(void);
void SCC_InterruptHandler_RX_B(void);

int	SCC_Get_Line_IRQ ( void );
int	SCC_Process_IACK ( void );

void SCC_IRQ(void);
int SCC_doInterrupt(void);

void SCC_IoMem_ReadByte(void);
void SCC_IoMem_WriteByte(void);
void SCC_Info(FILE *fp, uint32_t dummy);


#endif /* SCC_H */
