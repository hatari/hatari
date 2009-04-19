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
