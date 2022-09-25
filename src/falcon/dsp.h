/*
	DSP M56001 emulation
	Dummy emulation, Hatari glue

	(C) 2001-2008 ARAnyM developer team
	Adaption to Hatari (C) 2008 by Thomas Huth

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software Foundation,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA
*/

#ifndef DSP_H
#define DSP_H

#if ENABLE_DSP_EMU
# include "dsp_core.h"
#endif

#define DSP_CPU_FREQ_RATIO		2	/* Dsp Freq = 2 * Cpu Freq (32 MHz vs 16 MHz on Falcon) */

extern bool bDspEnabled;
extern bool bDspHostInterruptPending;

extern uint64_t   DSP_CyclesGlobalClockCounter;

/* Dsp commands */
extern bool DSP_ProcessIRQ(void);
extern void DSP_Init(void);
extern void DSP_UnInit(void);
extern void DSP_Reset(void);
extern void DSP_Enable(void);
extern void DSP_Disable(void);
extern void DSP_Run(int nHostCycles);

/* Save Dsp state to snapshot */
extern void DSP_MemorySnapShot_Capture(bool bSave);

/* Dsp Debugger commands */
extern void DSP_SetDebugging(bool enabled);
extern uint16_t DSP_GetPC(void);
extern uint16_t DSP_GetNextPC(uint16_t pc);
extern uint16_t DSP_GetInstrCycles(void);
extern uint32_t DSP_ReadMemory(uint16_t addr, char space, const char **mem_str);
extern uint16_t DSP_DisasmMemory(FILE *fp, uint16_t dsp_memdump_addr, uint16_t dsp_memdump_upper, char space);
extern uint16_t DSP_DisasmAddress(FILE *out, uint16_t lowerAdr, uint16_t UpperAdr);
extern void DSP_Info(FILE *fp, uint32_t dummy);
extern void DSP_DisasmRegisters(FILE *fp);
extern int DSP_GetRegisterAddress(const char *arg, uint32_t **addr, uint32_t *mask);
extern bool DSP_Disasm_SetRegister(const char *arg, uint32_t value);

/* Dsp SSI commands */
extern uint32_t DSP_SsiReadTxValue(void);
extern void DSP_SsiWriteRxValue(uint32_t value);
extern void DSP_SsiReceive_SC0(void);
extern void DSP_SsiReceive_SC1(uint32_t value);
extern void DSP_SsiReceive_SC2(uint32_t value);
extern void DSP_SsiReceive_SCK(void);
extern void DSP_SsiTransmit_SC0(void);
extern void DSP_SsiTransmit_SC1(void);
extern void DSP_SsiTransmit_SC2(uint32_t frame);
extern void DSP_SsiTransmit_SCK(void);


/* Dsp Host interface commands */
extern void DSP_HandleReadAccess(void);
extern void DSP_HandleWriteAccess(void);
extern uint8_t DSP_GetHREQ(void);
extern int DSP_ProcessIACK(void);


#endif /* DSP_H */
