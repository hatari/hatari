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
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "main.h"
#include "sysdeps.h"
#include "newcpu.h"
#include "memorySnapShot.h"
#include "ioMem.h"
#include "dsp.h"
#include "dsp_cpu.h"
#include "dsp_disasm.h"

#define DEBUG 0
#if DEBUG
#define Dprintf(a) printf a
#else
#define Dprintf(a)
#endif


#define DSP_HW_OFFSET  0xFFA200

#if ENABLE_DSP_EMU
static dsp_core_t dsp_core;
#endif

bool bDspEnabled = false;


/**
 * Initialize the DSP emulation
 */
void DSP_Init(void)
{
#if ENABLE_DSP_EMU
	dsp_core_init(&dsp_core);
	dsp56k_init_cpu(&dsp_core);
	bDspEnabled = true;
#endif
}


/**
 * Shut down the DSP emulation
 */
void DSP_UnInit(void)
{
#if ENABLE_DSP_EMU
	dsp_core_shutdown(&dsp_core);
	bDspEnabled = false;
#endif
}


/**
 * Reset the DSP emulation
 */
void DSP_Reset(void)
{
#if ENABLE_DSP_EMU
	dsp_core_reset(&dsp_core);
#endif
}


/**
 * Save/Restore snapshot of CPU variables ('MemorySnapShot_Store' handles type)
 */
void DSP_MemorySnapShot_Capture(bool bSave)
{
#if ENABLE_DSP_EMU
	if (!bSave)
		DSP_Reset();

	MemorySnapShot_Store(&bDspEnabled, sizeof(bDspEnabled));
	MemorySnapShot_Store(&dsp_core, sizeof(dsp_core));
#endif
}


/**
 * Run DSP for certain cycles
 */
void DSP_Run(int nHostCycles)
{
#if ENABLE_DSP_EMU
	/* Cycles emulation is just a rough approximation by now.
	 * (to be tuned ...) */
	int i = 7; // nHostCycles / 2;

	while (dsp_core.running == 1 && i-- > 0)
	{
		dsp56k_execute_instruction();
	}
#endif
}


/**
 * Get DSP program counter (for disassembler)
 */
Uint16 DSP_GetPC(void)
{
#if ENABLE_DSP_EMU
	if (bDspEnabled)
		return dsp_core.pc;
	else
#endif
	return 0;
}

Uint32 DSP_DisasmAddress(Uint16 lowerAdr, Uint16 UpperAdr)
{
	Uint32 dsp_pc, save_curPC;
	
	save_curPC = dsp_core.pc;

	for (dsp_pc=lowerAdr; dsp_pc<=UpperAdr; dsp_pc++) {
		dsp_core.pc = dsp_pc;
		dsp_pc += dsp56k_disasm() - 1;
	}
	dsp_core.pc = save_curPC;
	return dsp_pc;
}

void DSP_DisasmRegisters(void)
{
	fprintf(stderr,"A: A2:%02x A1:%06x A0:%06x\n",
		dsp_core.registers[DSP_REG_A2], dsp_core.registers[DSP_REG_A1], dsp_core.registers[DSP_REG_A0]);
	fprintf(stderr,"B: B2:%02x B1:%06x B0:%06x\n",
		dsp_core.registers[DSP_REG_B2], dsp_core.registers[DSP_REG_B1], dsp_core.registers[DSP_REG_B0]);
	
	fprintf(stderr,"X: X1:%06x X0:%06x\n", dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_X0]);
	fprintf(stderr,"Y: Y1:%06x Y0:%06x\n", dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_Y0]);

	fprintf(stderr,"R0: %04x   N0: %04x   M0: %04x\n", 
		dsp_core.registers[DSP_REG_R0], dsp_core.registers[DSP_REG_M0], dsp_core.registers[DSP_REG_N0]);
	fprintf(stderr,"R1: %04x   N1: %04x   M1: %04x\n", 
		dsp_core.registers[DSP_REG_R1], dsp_core.registers[DSP_REG_M1], dsp_core.registers[DSP_REG_N1]);
	fprintf(stderr,"R2: %04x   N2: %04x   M2: %04x\n", 
		dsp_core.registers[DSP_REG_R2], dsp_core.registers[DSP_REG_M2], dsp_core.registers[DSP_REG_N2]);
	fprintf(stderr,"R3: %04x   N3: %04x   M3: %04x\n", 
		dsp_core.registers[DSP_REG_R3], dsp_core.registers[DSP_REG_M3], dsp_core.registers[DSP_REG_N3]);
	fprintf(stderr,"R4: %04x   N4: %04x   M4: %04x\n", 
		dsp_core.registers[DSP_REG_R4], dsp_core.registers[DSP_REG_M4], dsp_core.registers[DSP_REG_N4]);
	fprintf(stderr,"R5: %04x   N5: %04x   M5: %04x\n", 
		dsp_core.registers[DSP_REG_R5], dsp_core.registers[DSP_REG_M5], dsp_core.registers[DSP_REG_N5]);
	fprintf(stderr,"R6: %04x   N6: %04x   M6: %04x\n", 
		dsp_core.registers[DSP_REG_R6], dsp_core.registers[DSP_REG_M6], dsp_core.registers[DSP_REG_N6]);
	fprintf(stderr,"R7: %04x   N7: %04x   M7: %04x\n", 
		dsp_core.registers[DSP_REG_R7], dsp_core.registers[DSP_REG_M7], dsp_core.registers[DSP_REG_N7]);

	fprintf(stderr,"LA: %04x   LC: %04x\n", dsp_core.registers[DSP_REG_LA], dsp_core.registers[DSP_REG_LC]);

	fprintf(stderr,"SR: %04x  OMR: %02x\n", dsp_core.registers[DSP_REG_SR], dsp_core.registers[DSP_REG_OMR]);

	fprintf(stderr,"SP: %02x    SSH: %04x  SSL: %04x\n", 
		dsp_core.registers[DSP_REG_SP], dsp_core.registers[DSP_REG_SSH], dsp_core.registers[DSP_REG_SSL]);
}

/**
 * Read SSI transmit value
 */
Uint32 DSP_SsiReadTxValue(void)
{
#if ENABLE_DSP_EMU
	return dsp_core.ssi.transmit_value;
#else
	return 0;
#endif
}


/**
 * Signal SSI clock tick to DSP
 */
void DSP_SsiReceiveSerialClock(void)
{
#if ENABLE_DSP_EMU
	dsp_core_ssi_receive_serial_clock(&dsp_core);
#endif
}


/**
 * Hardware IO address read by CPU
 */
static Uint8 DSP_handleRead(Uint32 addr)
{
	Uint8 value;
#if ENABLE_DSP_EMU
	value = dsp_core_read_host(&dsp_core, addr-DSP_HW_OFFSET);
#else
	/* this value prevents TOS from hanging in the DSP init code */
	value = 0xff;
#endif

	Dprintf(("HWget_b(0x%08x)=0x%02x at 0x%08x\n", addr, value, m68k_getpc()));
	return value;
}

/**
 * Read access wrapper for ioMemTabFalcon
 */
void DSP_HandleReadAccess(void)
{
	Uint32 a;
	Uint8 v;
	for (a = IoAccessBaseAddress; a < IoAccessBaseAddress+nIoMemAccessSize; a++)
	{
		v = DSP_handleRead(a);
		IoMem_WriteByte(a, v);
	}
}


/**
 * Hardware IO address write by CPU
 */
static void DSP_handleWrite(Uint32 addr, Uint8 value)
{
	Dprintf(("HWput_b(0x%08x,0x%02x) at 0x%08x\n", addr, value, m68k_getpc()));
#if ENABLE_DSP_EMU
	dsp_core_write_host(&dsp_core, addr-DSP_HW_OFFSET, value);
#endif
}

/**
 * Write access wrapper for ioMemTabFalcon
 */
void DSP_HandleWriteAccess(void)
{
	Uint32 a;
	Uint8 v;
	for (a = IoAccessBaseAddress; a < IoAccessBaseAddress+nIoMemAccessSize; a++)
	{
		v = IoMem_ReadByte(a);
		DSP_handleWrite(a,v);
	}
}
