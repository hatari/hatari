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
#include "crossbar.h"
#include "configuration.h"

#if ENABLE_DSP_EMU
#include "m68000.h"
#include "debugdsp.h"
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
static dsp_core_t dsp_core_save;
#endif
static bool bDspDebugging;

bool bDspEnabled = false;
bool bDspHostInterruptPending = false;


/**
 * Trigger HREQ interrupt at the host CPU.
 */
#if ENABLE_DSP_EMU
static void DSP_TriggerHostInterrupt(void)
{
	bDspHostInterruptPending = true;

	/* Note: The DSP interrupt is not wired to the MFP on a real Falcon
	 * (but to the COMBEL chip). But in Hatari we still handle it with
	 * the SPCFLAG_MFP to avoid taking care of another special flag in
	 * the CPU core! */
	M68000_SetSpecial(SPCFLAG_MFP);
}
#endif


/**
 * Initialize the DSP emulation
 */
void DSP_Init(void)
{
#if ENABLE_DSP_EMU
	if (ConfigureParams.System.nDSPType != DSP_TYPE_EMU)
		return;
	dsp_core_init(&dsp_core, DSP_TriggerHostInterrupt);
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
	if (ConfigureParams.System.nDSPType != DSP_TYPE_EMU)
		return;
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
	bDspHostInterruptPending = false;
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
	int i = nHostCycles * 2 + 2;
	int dsp_cycle = 0;

	if (dsp_core.running == 0)
		return;
	
	if (unlikely(bDspDebugging)) {
		while (i >= dsp_cycle)
		{
			DebugDsp_Check();
			dsp56k_execute_instruction();
			dsp_cycle += dsp_core.instr_cycle;
		}
	} else {
		while (i >= dsp_cycle)
		{
			dsp56k_execute_instruction();
			dsp_cycle += dsp_core.instr_cycle;
		}
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


/**
 * Disassemble DSP code between given addresses
 */
Uint32 DSP_DisasmAddress(Uint16 lowerAdr, Uint16 UpperAdr)
{
#if ENABLE_DSP_EMU
	Uint32 dsp_pc;
	dsp_core_t *ptr1, *ptr2;
	
	ptr1 = &dsp_core;
	ptr2 = &dsp_core_save;
	
	for (dsp_pc=lowerAdr; dsp_pc<=UpperAdr; dsp_pc++) {
		/* Save DSP context before executing instruction */
		memcpy(ptr2, ptr1, sizeof(dsp_core));

		/* execute and disasm instruction */
		dsp_core.pc = dsp_pc;
		dsp_pc += dsp56k_disasm() - 1;
		dsp56k_execute_instruction();
		fprintf(stderr, "%s", dsp56k_getInstructionText());

		/* Restore DSP context after executing instruction */
		memcpy(ptr1, ptr2, sizeof(dsp_core));
	}
	return dsp_pc;
#else
	return 0;
#endif
}


/**
 * Get the value from the given (16-bit) DSP memory address / space
 * exactly the same way as in dsp_cpu.c::read_memory() (except for
 * the host/transmit peripheral register values which access has
 * side-effects). Set the mem_str to suitable string for that
 * address / space.
 * Return the value at given address. For valid values AND the return
 * value with BITMASK(24).
 */
Uint32 DSP_ReadMemory(Uint16 address, char space_id, const char **mem_str)
{
#if ENABLE_DSP_EMU
	static const char *spaces[3][4] = {
		{ "X ram", "X rom", "X", "X periph" },
		{ "Y ram", "Y rom", "Y", "Y periph" },
		{ "P ram", "P ram", "P ext memory", "P ext memory" }
	};
	int idx, space;

	switch (space_id) {
	case 'X':
		space = DSP_SPACE_X;
		idx = 0;
		break;
	case 'Y':
		space = DSP_SPACE_Y;
		idx = 1;
		break;
	case 'P':
		space = DSP_SPACE_P;
		idx = 2;
		break;
	default:
		space = DSP_SPACE_X;
		idx = 0;
	}
	address &= 0xFFFF;

	/* Internal RAM ? */
	if (address < 0x100) {
		*mem_str = spaces[idx][0];
		return dsp_core.ramint[space][address];
	}

	if (space == DSP_SPACE_P) {
		/* Internal RAM ? */
		if (address < 0x200) {
			*mem_str = spaces[idx][0];
			return dsp_core.ramint[DSP_SPACE_P][address];
		}
		/* External RAM, mask address to available ram size */
		*mem_str = spaces[idx][2];
		return dsp_core.ramext[address & (DSP_RAMSIZE-1)];
	}

	/* Internal ROM ? */
	if (address < 0x200) {
		if (dsp_core.registers[DSP_REG_OMR] & (1<<DSP_OMR_DE)) {
			*mem_str = spaces[idx][1];
			return dsp_core.rom[space][address];
		}
	}

	/* Peripheral address ? */
	if (address >= 0xffc0) {
		*mem_str = spaces[idx][3];
		/* reading host/transmit regs has side-effects,
		 * so just give the memory value.
		 */
		return dsp_core.periph[space][address-0xffc0];
	}

	/* Falcon: External RAM, map X to upper 16K of matching space in Y,P */
	address &= (DSP_RAMSIZE>>1) - 1;
	if (space == DSP_SPACE_X) {
		address += DSP_RAMSIZE>>1;
	}

	/* Falcon: External RAM, finally map X,Y to P */
	*mem_str = spaces[idx][2];
	return dsp_core.ramext[address & (DSP_RAMSIZE-1)];
#endif
	return 0;
}


/**
 * Output memory values between given addresses in given DSP address space.
 */
void DSP_DisasmMemory(Uint16 dsp_memdump_addr, Uint16 dsp_memdump_upper, char space)
{
#if ENABLE_DSP_EMU
	Uint32 mem, mem2, value;
	const char *mem_str;

	for (mem = dsp_memdump_addr; mem <= dsp_memdump_upper; mem++) {
		/* special printing of host communication/transmit registers */
		if (space == 'X' && (mem == 0xffeb || mem == 0xffef)) {
			if (mem == 0xffeb) {
				fprintf(stderr,"X periph:%04x  HTX : %06x   RTX:%06x\n", 
					mem, dsp_core.dsp_host_htx, dsp_core.dsp_host_rtx);
			}
			else if (mem == 0xffef) {
				fprintf(stderr,"X periph:%04x  SSI TX : %06x   SSI RX:%06x\n", 
					mem, dsp_core.ssi.transmit_value, dsp_core.ssi.received_value);
			}
			continue;
		}
		/* special printing of X & Y external RAM values */
		if ((space == 'X' || space == 'Y') &&
		    mem >= 0x200 && mem < 0xffc0) {
			mem2 = mem & ((DSP_RAMSIZE>>1)-1);
			if (space == 'X') {
				mem2 += (DSP_RAMSIZE>>1);
			}
			fprintf(stderr,"%c:%04x (P:%04x): %06x\n", space,
				mem, mem2, dsp_core.ramext[mem2 & (DSP_RAMSIZE-1)]);
			continue;
		}
		value = DSP_ReadMemory(mem, space, &mem_str);
		fprintf(stderr,"%s:%04x  %06x\n", mem_str, mem, value);
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
 * Get given DSP register address and required bit mask.
 * Works for A0-2, B0-2, LA, LC, M0-7, N0-7, R0-7, X0-1, Y0-1, PC, SR, SP,
 * OMR, SSH & SSL registers, but note that the SP, SSH & SSL registers
 * need special handling (in DSP*SetRegister()) when they are set.
 * Return the register width in bits or zero for an error.
 */
int DSP_GetRegisterAddress(const char *regname, Uint32 **addr, Uint32 *mask)
{
#if ENABLE_DSP_EMU
#define MAX_REGNAME_LEN 4
	typedef struct {
		const char name[MAX_REGNAME_LEN];
		Uint32 *addr;
		size_t bits;
		Uint32 mask;
	} reg_addr_t;
	
	/* sorted by name so that this can be bisected */
	static const reg_addr_t registers[] = {

		/* 56-bit A register */
		{ "A0",  &dsp_core.registers[DSP_REG_A0],  32, BITMASK(24) },
		{ "A1",  &dsp_core.registers[DSP_REG_A1],  32, BITMASK(24) },
		{ "A2",  &dsp_core.registers[DSP_REG_A2],  32, BITMASK(8) },

		/* 56-bit B register */
		{ "B0",  &dsp_core.registers[DSP_REG_B0],  32, BITMASK(24) },
		{ "B1",  &dsp_core.registers[DSP_REG_B1],  32, BITMASK(24) },
		{ "B2",  &dsp_core.registers[DSP_REG_B2],  32, BITMASK(8) },

		/* 16-bit LA & LC registers */
		{ "LA",  &dsp_core.registers[DSP_REG_LA],  32, BITMASK(16) },
		{ "LC",  &dsp_core.registers[DSP_REG_LC],  32, BITMASK(16) },

		/* 16-bit M registers */
		{ "M0",  &dsp_core.registers[DSP_REG_M0],  32, BITMASK(16) },
		{ "M1",  &dsp_core.registers[DSP_REG_M1],  32, BITMASK(16) },
		{ "M2",  &dsp_core.registers[DSP_REG_M2],  32, BITMASK(16) },
		{ "M3",  &dsp_core.registers[DSP_REG_M3],  32, BITMASK(16) },
		{ "M4",  &dsp_core.registers[DSP_REG_M4],  32, BITMASK(16) },
		{ "M5",  &dsp_core.registers[DSP_REG_M5],  32, BITMASK(16) },
		{ "M6",  &dsp_core.registers[DSP_REG_M6],  32, BITMASK(16) },
		{ "M7",  &dsp_core.registers[DSP_REG_M7],  32, BITMASK(16) },

		/* 16-bit N registers */
		{ "N0",  &dsp_core.registers[DSP_REG_N0],  32, BITMASK(16) },
		{ "N1",  &dsp_core.registers[DSP_REG_N1],  32, BITMASK(16) },
		{ "N2",  &dsp_core.registers[DSP_REG_N2],  32, BITMASK(16) },
		{ "N3",  &dsp_core.registers[DSP_REG_N3],  32, BITMASK(16) },
		{ "N4",  &dsp_core.registers[DSP_REG_N4],  32, BITMASK(16) },
		{ "N5",  &dsp_core.registers[DSP_REG_N5],  32, BITMASK(16) },
		{ "N6",  &dsp_core.registers[DSP_REG_N6],  32, BITMASK(16) },
		{ "N7",  &dsp_core.registers[DSP_REG_N7],  32, BITMASK(16) },

		{ "OMR", &dsp_core.registers[DSP_REG_OMR], 32, 0x5f },

		/* 16-bit program counter */
		{ "PC",  (Uint32*)(&dsp_core.pc),  16, BITMASK(16) },

		/* 16-bit DSP R (address) registers */
		{ "R0",  &dsp_core.registers[DSP_REG_R0],  32, BITMASK(16) },
		{ "R1",  &dsp_core.registers[DSP_REG_R1],  32, BITMASK(16) },
		{ "R2",  &dsp_core.registers[DSP_REG_R2],  32, BITMASK(16) },
		{ "R3",  &dsp_core.registers[DSP_REG_R3],  32, BITMASK(16) },
		{ "R4",  &dsp_core.registers[DSP_REG_R4],  32, BITMASK(16) },
		{ "R5",  &dsp_core.registers[DSP_REG_R5],  32, BITMASK(16) },
		{ "R6",  &dsp_core.registers[DSP_REG_R6],  32, BITMASK(16) },
		{ "R7",  &dsp_core.registers[DSP_REG_R7],  32, BITMASK(16) },

		{ "SSH", &dsp_core.registers[DSP_REG_SSH], 32, BITMASK(16) },
		{ "SSL", &dsp_core.registers[DSP_REG_SSL], 32, BITMASK(16) },
		{ "SP",  &dsp_core.registers[DSP_REG_SP],  32, BITMASK(6) },

		/* 16-bit status register */
		{ "SR",  &dsp_core.registers[DSP_REG_SR],  32, 0xefff },

		/* 48-bit X register */
		{ "X0",  &dsp_core.registers[DSP_REG_X0],  32, BITMASK(24) },
		{ "X1",  &dsp_core.registers[DSP_REG_X1],  32, BITMASK(24) },

		/* 48-bit Y register */
		{ "Y0",  &dsp_core.registers[DSP_REG_Y0],  32, BITMASK(24) },
		{ "Y1",  &dsp_core.registers[DSP_REG_Y1],  32, BITMASK(24) }
	};
	/* left, right, middle, direction */
        int l, r, m, dir;
	unsigned int i, len;
	char reg[MAX_REGNAME_LEN];

	if (!bDspEnabled) {
		return 0;
	}

	for (i = 0; i < sizeof(reg) && regname[i]; i++) {
		reg[i] = toupper(regname[i]);
	}
	if (i < 2 || regname[i]) {
		/* too short or longer than any of the names */
		return 0;
	}
	len = i;
	
	/* bisect */
	l = 0;
	r = sizeof (registers) / sizeof (*registers) - 1;
	do {
		m = (l+r) >> 1;
		for (i = 0; i < len; i++) {
			dir = (int)reg[i] - registers[m].name[i];
			if (dir) {
				break;
			}
		}
		if (dir == 0) {
			*addr = registers[m].addr;
			*mask = registers[m].mask;
			return registers[m].bits;
		}
		if (dir < 0) {
			r = m-1;
		} else {
			l = m+1;
		}
	} while (l <= r);
#undef MAX_REGNAME_LEN
#endif
	return 0;
}


/**
 * Set given DSP register value, return false if unknown register given
 */
bool DSP_Disasm_SetRegister(const char *arg, Uint32 value)
{
#if ENABLE_DSP_EMU
	Uint32 *addr, mask, sp_value;
	int bits;

	/* first check registers needing special handling... */
	if (arg[0]=='S' || arg[0]=='s') {
		if (arg[1]=='P' || arg[1]=='p') {
			dsp_core.registers[DSP_REG_SP] = value & BITMASK(6);
			value &= BITMASK(4); 
			dsp_core.registers[DSP_REG_SSH] = dsp_core.stack[0][value];
			dsp_core.registers[DSP_REG_SSL] = dsp_core.stack[1][value];
			return true;
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
				return true;
			}
			if (arg[2]=='L' || arg[2]=='l') {
				if (sp_value == 0) {
					dsp_core.registers[DSP_REG_SSL] = 0;
					dsp_core.stack[1][sp_value] = 0;
				} else {
					dsp_core.registers[DSP_REG_SSL] = value & BITMASK(16);
					dsp_core.stack[1][sp_value] = value & BITMASK(16);
				}
				return true;
			}
		}
	}

	/* ...then registers where address & mask are enough */
	bits = DSP_GetRegisterAddress(arg, &addr, &mask);
	switch (bits) {
	case 32:
		*addr = value & mask;
		return true;
	case 16:
		*(Uint16*)addr = value & mask;
		return true;
	}
#endif
	return false;
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

void DSP_SsiReceive_SC0(void)
{
#if ENABLE_DSP_EMU
	dsp_core_ssi_Receive_SC0(&dsp_core);
#endif
}

void DSP_SsiTransmit_SC0(void)
{
#if ENABLE_DSP_EMU
#endif
}

void DSP_SsiReceive_SC1(Uint32 FrameCounter)
{
#if ENABLE_DSP_EMU
	dsp_core_ssi_Receive_SC1(&dsp_core, FrameCounter);
#endif
}

void DSP_SsiTransmit_SC1(void)
{
#if ENABLE_DSP_EMU
	Crossbar_DmaPlayInHandShakeMode();
#endif
}

void DSP_SsiReceive_SC2(Uint32 FrameCounter)
{
#if ENABLE_DSP_EMU
	dsp_core_ssi_Receive_SC2(&dsp_core, FrameCounter);
#endif
}

void DSP_SsiTransmit_SC2(Uint32 frame)
{
#if ENABLE_DSP_EMU
	Crossbar_DmaRecordInHandShakeMode_Frame(frame);
#endif
}

void DSP_SsiReceive_SCK(void)
{
#if ENABLE_DSP_EMU
	dsp_core_ssi_Receive_SCK(&dsp_core);
#endif
}

void DSP_SsiTransmit_SCK(void)
{
#if ENABLE_DSP_EMU
#endif
}

/**
 * Read access wrapper for ioMemTabFalcon (DSP Host port)
 */
void DSP_HandleReadAccess(void)
{
	Uint32 addr;
	Uint8 value;
	for (addr = IoAccessBaseAddress; addr < IoAccessBaseAddress+nIoMemAccessSize; addr++)
	{
#if ENABLE_DSP_EMU
		value = dsp_core_read_host(&dsp_core, addr-DSP_HW_OFFSET);
#else
		/* this value prevents TOS from hanging in the DSP init code */
		value = 0xff;
#endif

		Dprintf(("HWget_b(0x%08x)=0x%02x at 0x%08x\n", addr, value, m68k_getpc()));
		IoMem_WriteByte(addr, value);
	}
}

/**
 * Write access wrapper for ioMemTabFalcon (DSP Host port)
 */
void DSP_HandleWriteAccess(void)
{
	Uint32 addr;
	Uint8 value;
	for (addr = IoAccessBaseAddress; addr < IoAccessBaseAddress+nIoMemAccessSize; addr++)
	{
		value = IoMem_ReadByte(addr);
		Dprintf(("HWput_b(0x%08x,0x%02x) at 0x%08x\n", addr, value, m68k_getpc()));
#if ENABLE_DSP_EMU
		dsp_core_write_host(&dsp_core, addr-DSP_HW_OFFSET, value);
#endif
	}
}
