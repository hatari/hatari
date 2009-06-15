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
#if ENABLE_DSP_EMU
#include "debugui.h"
#include "dsp_cpu.h"
#include "dsp_disasm.h"
#endif

#define DEBUG 0
#if DEBUG
#define Dprintf(a) printf a
#else
#define Dprintf(a)
#endif

#define BITMASK(x)	((1<<(x))-1)

#define DSP_HW_OFFSET  0xFFA200

#if ENABLE_DSP_EMU
static dsp_core_t dsp_core;
#endif
static bool bDspDebugging;

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
		if (unlikely(bDspDebugging))
			DebugUI_DspCheck();
		dsp56k_execute_instruction();
	}
#endif
}

/**
 * Enable/disable DSP debugging mode
 */
void DSP_SetDebugging(bool enabled)
{
	bDspDebugging = enabled;
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
#if ENABLE_DSP_EMU
	Uint32 dsp_pc, save_curPC;
	
	save_curPC = dsp_core.pc;

	for (dsp_pc=lowerAdr; dsp_pc<=UpperAdr; dsp_pc++) {
		dsp_core.pc = dsp_pc;
		dsp_pc += dsp56k_disasm() - 1;
	}
	dsp_core.pc = save_curPC;
	return dsp_pc;
#else
	return 0;
#endif
}

void DSP_DisasmMemory(Uint16 dsp_memdump_addr, Uint16 dsp_memdump_upper, Uint16 space)
{
#if ENABLE_DSP_EMU
	Uint32 mem = dsp_memdump_addr, mem2;

	switch (space) {
		case 'X':
			/* Space X */
			for (; mem<=dsp_memdump_upper; mem++) {
				if (mem < 0x100) {
					/* X internal ram */
					fprintf(stderr,"X ram:%04x  %06x\n", mem, dsp_core.ramint[DSP_SPACE_X][mem]);
				}
				else if ((mem < 0x200) && (dsp_core.registers[DSP_REG_OMR] & (1<<DSP_OMR_DE))) {
					/* X internal rom */
					fprintf(stderr,"X rom:%04x  %06x\n", mem, dsp_core.rom[DSP_SPACE_X][mem]);
				}
				else if (mem >= 0xffc0) {
					/* X Peripheral memory */
					if (mem == 0xffeb) {
						fprintf(stderr,"X periph:%04x  HTX : %06x   RTX:%06x\n", 
							mem, dsp_core.dsp_host_htx, dsp_core.dsp_host_rtx);
					} 
					else if (mem == 0xffef) {
						fprintf(stderr,"X periph:%04x  SSI TX : %06x   SSI RX:%06x\n", 
							mem, dsp_core.ssi.transmit_value, dsp_core.ssi.received_value);
					}
					else {
						fprintf(stderr,"X periph:%04x  %06x\n", mem, dsp_core.periph[DSP_SPACE_X][mem-0xffc0]);
					}
				}
				else {
					mem2 = mem & ((DSP_RAMSIZE>>1)-1);
					mem2 += (DSP_RAMSIZE>>1);
					fprintf(stderr,"X:%04x (P:%04x): %06x\n", 
						mem, mem2, dsp_core.ramext[mem2 & (DSP_RAMSIZE-1)]);
				}
			}
			break;
		case 'Y':
			/* Space Y */
			for (; mem<=dsp_memdump_upper; mem++) {
				if (mem < 0x100) {
					/* Y internal ram */
					fprintf(stderr,"Y ram:%04x  %06x\n", mem, dsp_core.ramint[DSP_SPACE_Y][mem]);
				}
				else if ((mem < 0x200) && (dsp_core.registers[DSP_REG_OMR] & (1<<DSP_OMR_DE))) {
					/* Y internal rom */
					fprintf(stderr,"Y rom:%04x  %06x\n", mem, dsp_core.rom[DSP_SPACE_Y][mem]);
				}
				else if (mem >= 0xffc0) {
					/* Y Peripheral memory */
					fprintf(stderr,"Y periph:%04x  %06x\n", mem, dsp_core.periph[DSP_SPACE_Y][mem-0xffc0]);
				}
				else {
					mem2 = mem & ((DSP_RAMSIZE>>1)-1);
					fprintf(stderr,"Y:%04x (P:%04x):  %06x\n", 
						mem, mem2, dsp_core.ramext[mem2 & (DSP_RAMSIZE-1)]);
				}
			}
			break;
		case 'P':
			/* Space P */
			for (; mem<=dsp_memdump_upper; mem++) {
				if (mem < 0x200) {
					/* P internal ram */
					fprintf(stderr,"P ram:%04x  %06x\n", mem, dsp_core.ramint[DSP_SPACE_P][mem]);
				}
				else {
					fprintf(stderr,"P ext memory:%04x  %06x\n", mem, dsp_core.ramext[mem & (DSP_RAMSIZE-1)]);
				}
			}
			break;
	}
#endif
}


void DSP_DisasmRegisters(void)
{
#if ENABLE_DSP_EMU
	Uint32 i;

	fprintf(stderr,"A: A2: %02x  A1: %06x  A0: %06x\n",
		dsp_core.registers[DSP_REG_A2], dsp_core.registers[DSP_REG_A1], dsp_core.registers[DSP_REG_A0]);
	fprintf(stderr,"B: B2: %02x  B1: %06x  B0: %06x\n",
		dsp_core.registers[DSP_REG_B2], dsp_core.registers[DSP_REG_B1], dsp_core.registers[DSP_REG_B0]);
	
	fprintf(stderr,"X: X1: %06x  X0: %06x\n", dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_X0]);
	fprintf(stderr,"Y: Y1: %06x  Y0: %06x\n", dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_Y0]);

	for (i=0; i<8; i++) {
		fprintf(stderr,"R%01x: %04x   N%01x: %04x   M%01x: %04x\n", 
			i, dsp_core.registers[DSP_REG_R0+i],
			i, dsp_core.registers[DSP_REG_N0+i],
			i, dsp_core.registers[DSP_REG_M0+i]);
	}

	fprintf(stderr,"LA: %04x   LC: %04x   PC: %04x\n", dsp_core.registers[DSP_REG_LA], dsp_core.registers[DSP_REG_LC], dsp_core.pc);
	fprintf(stderr,"SR: %04x  OMR: %02x\n", dsp_core.registers[DSP_REG_SR], dsp_core.registers[DSP_REG_OMR]);
	fprintf(stderr,"SP: %02x    SSH: %04x  SSL: %04x\n", 
		dsp_core.registers[DSP_REG_SP], dsp_core.registers[DSP_REG_SSH], dsp_core.registers[DSP_REG_SSL]);
#endif
}


/**
 * Get given DSP register address (for debugging).
 * Handles A0-2, B0-2, LA, LC, M0-7, N0-7, R0-7, X0-1, Y0-1 registers
 * but not PC because it's 16-bit, nor SR, SP, OMR, SSH, SSL because
 * they need special handling by SetRegister().
 * Return the register width in bits or zero for an error.
 */
size_t DSP_GetRegisterAddress(const char *regname, Uint32 **addr)
{
#if ENABLE_DSP_EMU
	typedef struct {
		const char name[3];
		Uint32 *addr;
		size_t bits;
	} reg_addr_t;
	
	/* if this were speed critical, the array could be global to dsp.c. */
	const reg_addr_t registers[] = {
		/* sorted by name so that this can be bisected */
		{ "A0", &dsp_core.registers[DSP_REG_A0], 24 },
		{ "A1", &dsp_core.registers[DSP_REG_A1], 24 },
		{ "A2", &dsp_core.registers[DSP_REG_A2], 8 },

		{ "B0", &dsp_core.registers[DSP_REG_B0], 24 },
		{ "B1", &dsp_core.registers[DSP_REG_B1], 24 },
		{ "B2", &dsp_core.registers[DSP_REG_B2], 8 },

		{ "LA", &dsp_core.registers[DSP_REG_LA], 16 },
		{ "LC", &dsp_core.registers[DSP_REG_LC], 16 },

		{ "M0", &dsp_core.registers[DSP_REG_M0], 16 },
		{ "M1", &dsp_core.registers[DSP_REG_M1], 16 },
		{ "M2", &dsp_core.registers[DSP_REG_M2], 16 },
		{ "M3", &dsp_core.registers[DSP_REG_M3], 16 },
		{ "M4", &dsp_core.registers[DSP_REG_M4], 16 },
		{ "M5", &dsp_core.registers[DSP_REG_M5], 16 },
		{ "M6", &dsp_core.registers[DSP_REG_M6], 16 },
		{ "M7", &dsp_core.registers[DSP_REG_M7], 16 },

		{ "N0", &dsp_core.registers[DSP_REG_N0], 16 },
		{ "N1", &dsp_core.registers[DSP_REG_N1], 16 },
		{ "N2", &dsp_core.registers[DSP_REG_N2], 16 },
		{ "N3", &dsp_core.registers[DSP_REG_N3], 16 },
		{ "N4", &dsp_core.registers[DSP_REG_N4], 16 },
		{ "N5", &dsp_core.registers[DSP_REG_N5], 16 },
		{ "N6", &dsp_core.registers[DSP_REG_N6], 16 },
		{ "N7", &dsp_core.registers[DSP_REG_N7], 16 },

		{ "R0", &dsp_core.registers[DSP_REG_R0], 16 },
		{ "R1", &dsp_core.registers[DSP_REG_R1], 16 },
		{ "R2", &dsp_core.registers[DSP_REG_R2], 16 },
		{ "R3", &dsp_core.registers[DSP_REG_R3], 16 },
		{ "R4", &dsp_core.registers[DSP_REG_R4], 16 },
		{ "R5", &dsp_core.registers[DSP_REG_R5], 16 },
		{ "R6", &dsp_core.registers[DSP_REG_R6], 16 },
		{ "R7", &dsp_core.registers[DSP_REG_R7], 16 },

		{ "X0", &dsp_core.registers[DSP_REG_X0], 24 },
		{ "X1", &dsp_core.registers[DSP_REG_X1], 24 },

		{ "Y0", &dsp_core.registers[DSP_REG_Y0], 24 },
		{ "Y1", &dsp_core.registers[DSP_REG_Y1], 24 }
	};
	/* left, right, middle, direction */
        int l, r, m, dir;
	char r0, r1;

	/* right lenght & upcase */
	r0 = toupper(regname[0]);
	if (!r0 || !regname[1] || regname[2]) {
		return 0;
	}
	r1 = toupper(regname[1]);

	/* bisect */
	l = 0;
	r = sizeof (registers) / sizeof (*registers) - 1;
	do {
		m = (l+r) >> 1;
		dir = registers[m].name[0] - r0;
		if (!dir) {
			dir = registers[m].name[1] - r1;
		}
		if (dir == 0) {
fprintf(stderr, "found register: %s=%s\n", regname, registers[m].name);
			*addr = registers[m].addr;
			return registers[m].bits;
		}
		if (dir < 0) {
			r = m-1;
		} else {
			l = m+1;
		}
	} while (l <= r);
	
	return 0;
#endif
}


void DSP_Disasm_SetRegister(char *arg, Uint32 value)
{
#if ENABLE_DSP_EMU
	Uint32 sp_value;
	Uint32 *addr;
	size_t bits;
	
	bits = DSP_GetRegisterAddress(arg, &addr);
	if (bits) {
		*addr = value & BITMASK(bits);
		return;
	}

	if (arg[0]=='P' || arg[0]=='p') {
		if (arg[1]=='C' || arg[1]=='c') {
			dsp_core.pc = value & BITMASK(16);
			return;
		}
	}
		
	if (arg[0]=='O' || arg[0]=='o') {
		if (arg[1]=='M' || arg[1]=='m') {
			if (arg[2]=='R' || arg[2]=='r') {
				dsp_core.registers[DSP_REG_OMR] = value & 0x5f;
				return;
			}
		}
	}

	if (arg[0]=='S' || arg[0]=='s') {
		if (arg[1]=='R' || arg[1]=='r') {
			dsp_core.registers[DSP_REG_SR] = value & 0xefff;
			return;
		}
		if (arg[1]=='P' || arg[1]=='p') {
			dsp_core.registers[DSP_REG_SP] = value & BITMASK(6);
			value &= BITMASK(4); 
			dsp_core.registers[DSP_REG_SSH] = dsp_core.stack[0][value];
			dsp_core.registers[DSP_REG_SSL] = dsp_core.stack[1][value];
			return;
		}
		if (arg[1]=='S' || arg[1]=='s') {
			sp_value = dsp_core.registers[DSP_REG_SP] & BITMASK(4);
			if (arg[2]=='H' || arg[2]=='h') {
				if (sp_value == 0) {
					dsp_core.registers[DSP_REG_SSH] = 0;
					dsp_core.stack[0][sp_value] = 0;
				} else {
					dsp_core.registers[DSP_REG_SSH] = value & BITMASK(16);
					dsp_core.stack[0][sp_value] = value & BITMASK(16);
				}
				return;
			}
			if (arg[2]=='L' || arg[2]=='l') {
				if (sp_value == 0) {
					dsp_core.registers[DSP_REG_SSL] = 0;
					dsp_core.stack[1][sp_value] = 0;
				} else {
					dsp_core.registers[DSP_REG_SSL] = value & BITMASK(16);
					dsp_core.stack[1][sp_value] = value & BITMASK(16);
				}
				return;
			}
		}
	}

	fprintf(stderr,"\tError, usage:  reg=value  where: \n\t \
			reg=A0-A2, B0-B2, X0, X1, Y0, Y1, \n\t \
			R0-R7, N0-N7, M0-M7, LA, LC, PC \n\t \
			SR, SP, OMR, SSH, SSL \n\t \
			and value is a hex value.\n");
#endif
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
 * Write SSI receive value
 */
void DSP_SsiWriteRxValue(Uint32 value)
{
#if ENABLE_DSP_EMU
	dsp_core.ssi.received_value = value & 0xffffff;
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

void DSP_SsiReceive_SC2(Uint32 FrameCounter)
{
#if ENABLE_DSP_EMU
	dsp_core_ssi_receive_SC2(&dsp_core, FrameCounter);
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
