/*
  Hatari - hatari-glue.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This file contains some code to glue the UAE CPU core to the rest of the
  emulator and Hatari's "illegal" opcodes.
*/
const char HatariGlue_fileid[] = "Hatari hatari-glue.c : " __DATE__ " " __TIME__;


#include <stdio.h>

#include "main.h"
#include "configuration.h"
#include "cycInt.h"
#include "tos.h"
#include "gemdos.h"
#include "cart.h"
#include "vdi.h"
#include "stMemory.h"
#include "ikbd.h"
#include "screen.h"
#include "video.h"
#include "psg.h"

#include "sysdeps.h"
#include "maccess.h"
#include "memory.h"
#include "newcpu.h"
#include "cpu_prefetch.h"
#include "hatari-glue.h"


struct uae_prefs currprefs, changed_prefs;

int pendingInterrupts = 0;


/**
 * Reset custom chips
 */
void customreset(void)
{
	pendingInterrupts = 0;

	/* In case the 6301 was executing a custom program from its RAM */
	/* we must turn it back to the 'normal' mode. */
	IKBD_Reset_ExeMode ();

	/* Reseting the GLUE video chip should also set freq/res register to 0 */
	Video_Reset_Glue ();

        /* Reset the YM2149 (stop any sound) */
        PSG_Reset ();
}


/**
 * Return interrupt number (1 - 7), -1 means no interrupt.
 * Note that the interrupt stays pending if it can't be executed yet
 * due to the interrupt level field in the SR.
 */
int intlev(void)
{
	/* There are only VBL and HBL autovector interrupts in the ST... */
	assert((pendingInterrupts & ~((1<<4)|(1<<2))) == 0);

	if (pendingInterrupts & (1 << 4))         /* VBL interrupt? */
	{
		if (regs.intmask < 4)
			pendingInterrupts &= ~(1 << 4);
		return 4;
	}
	else if (pendingInterrupts & (1 << 2))    /* HBL interrupt? */
	{
		if (regs.intmask < 2)
			pendingInterrupts &= ~(1 << 2);
		return 2;
	}

	return -1;
}

/**
 * Initialize 680x0 emulation
 */
int Init680x0(void)
{
	currprefs.cpu_level = changed_prefs.cpu_level = ConfigureParams.System.nCpuLevel;

	switch (currprefs.cpu_level) {
		case 0 : currprefs.cpu_model = 68000; break;
		case 1 : currprefs.cpu_model = 68010; break;
		case 2 : currprefs.cpu_model = 68020; break;
		case 3 : currprefs.cpu_model = 68030; break;
		case 4 : currprefs.cpu_model = 68040; break;
		case 5 : currprefs.cpu_model = 68060; break;
		default: fprintf (stderr, "Init680x0() : Error, cpu_level unknown\n");
	}
	
	currprefs.cpu_compatible = changed_prefs.cpu_compatible = ConfigureParams.System.bCompatibleCpu;
	currprefs.address_space_24 = changed_prefs.address_space_24 = ConfigureParams.System.bAddressSpace24;
	currprefs.cpu_cycle_exact = changed_prefs.cpu_cycle_exact = ConfigureParams.System.bCycleExactCpu;
	currprefs.fpu_model = changed_prefs.fpu_model = ConfigureParams.System.n_FPUType;
	currprefs.fpu_strict = changed_prefs.fpu_strict = ConfigureParams.System.bCompatibleFPU;
	currprefs.mmu_model = changed_prefs.mmu_model = ConfigureParams.System.bMMU;

	init_m68k();

	return true;
}


/**
 * Deinitialize 680x0 emulation
 */
void Exit680x0(void)
{
	memory_uninit();

	free(table68k);
	table68k = NULL;
}

/**
 * This function will be called at system init by the cartridge routine
 * (after gemdos init, before booting floppies).
 * The GEMDOS vector (#$84) is setup and we also initialize the connected
 * drive mask and Line-A  variables (for an extended VDI resolution) from here.
 */
unsigned long OpCode_SysInit(uae_u32 opcode)
{
	/* Add any drives mapped by TOS in the interim */
	ConnectedDriveMask |= STMemory_ReadLong(0x4c2);
	/* Initialize the connected drive mask */
	STMemory_WriteLong(0x4c2, ConnectedDriveMask);

	if (!bInitGemDOS)
	{
		/* Init on boot - see cart.c */
		GemDOS_Boot();

		/* Update LineA for extended VDI res
		 * D0: LineA base, A1: Font base
		 */
		VDI_LineA(regs.regs[0], regs.regs[9]);
	}

	m68k_incpc(2);
	regs.ir = regs.irc;
	get_word_prefetch(2);

	return 4 * CYCLE_UNIT / 2;
}


/**
 * Intercept GEMDOS calls.
 * Used for GEMDOS HD emulation (see gemdos.c).
 */
unsigned long OpCode_GemDos(uae_u32 opcode)
{
	GemDOS_OpCode();    /* handler code in gemdos.c */

	m68k_incpc(2);
	regs.ir = regs.irc;
	get_word_prefetch(2);

	return 4 * CYCLE_UNIT / 2;
}


/**
 * This is called after completion of each VDI call
 */
unsigned long OpCode_VDI(uae_u32 opcode)
{
	VDI_Complete();

	/* Set PC back to where originated from to continue instruction decoding */
	m68k_setpc(VDI_OldPC);

	get_word_prefetch (0);
	regs.ir = regs.irc;
	get_word_prefetch(2);

	return 4 * CYCLE_UNIT / 2;
}
