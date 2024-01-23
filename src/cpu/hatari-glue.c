/*
  Hatari - hatari-glue.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains some code to glue the UAE CPU core to the rest of the
  emulator and Hatari's "illegal" opcodes.
*/
const char HatariGlue_fileid[] = "Hatari hatari-glue.c";


#include <stdio.h>

#include "main.h"
#include "configuration.h"
#include "cycles.h"
#include "cycInt.h"
#include "tos.h"
#include "gemdos.h"
#include "natfeats.h"
#include "cart.h"
#include "vdi.h"
#include "stMemory.h"
#include "ikbd.h"
#include "video.h"
#include "psg.h"
#include "mfp.h"
#include "fdc.h"
#include "memorySnapShot.h"

#include "sysdeps.h"
#include "options_cpu.h"
#include "maccess.h"
#include "memory.h"
#include "m68000.h"
#include "newcpu.h"
#include "cpu_prefetch.h"
#include "savestate.h"
#include "hatari-glue.h"


struct uae_prefs currprefs, changed_prefs;

int pendingInterrupts = 0;


/**
 * Reset custom chips
 * In case the RESET instruction is called, we must reset all the peripherals
 * connected to the CPU's reset pin.
 */
void customreset(void)
{
	pendingInterrupts = 0;

	/* Reset the IKBD */
	IKBD_Reset ( false );

	/* Resetting the GLUE video chip should also set freq/res register to 0 */
	Video_Reset_Glue ();

	/* Reset the YM2149 (stop any sound) */
	PSG_Reset ();

	/* Reset the MFP */
	MFP_Reset_All ();

	/* Reset the FDC */
	FDC_Reset ( false );
}


/**
 * Return highest interrupt number (1 - 7), 0 means no interrupt.
 * Note that the interrupt stays pending if it can't be executed yet
 * due to the interrupt level field in the SR.
 */
int intlev(void)
{
	if ( pendingInterrupts & (1 << 6) )		/* MFP/DSP interrupt ? */
		return 6;
	else if ( pendingInterrupts & (1 << 5) )	/* SCC interrupt ? */
		return 5;
	else if ( pendingInterrupts & (1 << 4) )	/* VBL interrupt ? */
		return 4;
	else if ( pendingInterrupts & (1 << 2) )	/* HBL interrupt ? */
		return 2;

	return 0;
}


void UAE_Set_Quit_Reset ( bool hard )
{
//fprintf ( stderr , "UAE_Set_Quit_Reset %d\n" , hard );
	if ( hard )
		quit_program = UAE_RESET_HARD;
	else
		quit_program = UAE_RESET;
}


void UAE_Set_State_Save ( void )
{
//fprintf ( stderr , "UAE_Set_State_Save\n" );
	savestate_state = STATE_SAVE;
}


void UAE_Set_State_Restore ( void )
{
//fprintf ( stderr , "UAE_Set_State_Restore\n" );
	savestate_state = STATE_RESTORE;
}



/**
 * Replace WinUAE's save_state / restore_state functions with Hatari's specific ones
 */
int save_state (const TCHAR *filename, const TCHAR *description)
{
//fprintf ( stderr , "save_state in\n" );
	MemorySnapShot_Capture_Do ();
//fprintf ( stderr , "save_state out\n" );
	savestate_state = 0;
	return 0;					/* return value is not used */
}


void restore_state (const TCHAR *filename)
{
	MemorySnapShot_Restore_Do ();
}


void savestate_restore_final (void)
{
  /* Not used for now in Hatari */
}


bool savestate_restore_finish (void)
{
//fprintf ( stderr , "savestate_restore_finish in %d\n" , quit_program );
	if (!isrestore ())
		return false;
	restore_cpu_finish ();
	savestate_state = 0;
	quit_program = 0;				/* at this point, quit_program was already processed, we must reset it */
//fprintf ( stderr , "savestate_restore_finish out %d\n" , quit_program );
	return true;
}


/**
 * Initialize 680x0 emulation
 */
int Init680x0(void)
{
//fprintf ( stderr , "Init680x0 in\n" );
	init_m68k();
//fprintf ( stderr , "Init680x0 out\n" );
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
 * Execute a 'NOP' opcode (increment PC by 2 bytes and take care
 * of prefetch at the CPU level depending on the current CPU mode)
 * This is used to return from SysInit / Natfeats interception, by ignoring
 * the intercepted opcode and executing a NOP instead once the work has been done.
 */
static void	CpuDoNOP ( void )
{
	if ( !CpuRunFuncNoret )
		(*cpufunctbl[0X4E71])(0x4E71);
	else
		(*cpufunctbl_noret[0X4E71])(0x4E71);
}


/**
 * Check whether PC is currently in ROM cartridge space - used
 * to test whether our "illegal" Hatari opcodes should be handled
 * or whether they are just "normal" illegal opcodes.
 */
static bool is_cart_pc(void)
{
	uint32_t pc = M68000_GetPC();

	if (ConfigureParams.System.bAddressSpace24 || (pc >> 24) == 0xff)
	{
		pc &= 0x00ffffff;	/* Mask to 24-bit address */
	}

	return pc >= 0xfa0000 && pc < 0xfc0000;
}


/**
 * This function will be called at system init by the cartridge routine
 * (after gemdos init, before booting floppies).
 * The GEMDOS vector (#$84) is setup and we also initialize the connected
 * drive mask and Line-A variables (for an extended VDI resolution) from here.
 */
uae_u32 REGPARAM3 OpCode_SysInit(uae_u32 opcode)
{
	if (is_cart_pc())
	{
		/* Add any drives mapped by TOS in the interim */
		ConnectedDriveMask |= STMemory_ReadLong(0x4c2);
		/* Initialize the connected drive mask */
		STMemory_WriteLong(0x4c2, ConnectedDriveMask);

		/* Init on boot - see cart.c */
		GemDOS_Boot();

		/* Update LineA for extended VDI res
		 * D0: LineA base, A1: Font base
		 */
		VDI_LineA(regs.regs[0], regs.regs[9]);

		CpuDoNOP();
	}
	else if (!bUseTos)
	{
		GemDOS_Boot();
		CpuDoNOP();
	}
	else
	{
		LOG_TRACE(TRACE_OS_GEMDOS | TRACE_OS_BASE | TRACE_OS_VDI | TRACE_OS_AES,
			  "SYSINIT opcode invoked outside of cartridge space\n");
		/* illegal instruction */
		op_illg(opcode);
		fill_prefetch();
	}

	return 4 * CYCLE_UNIT / 2;
}

void REGPARAM3 OpCode_SysInit_noret(uae_u32 opcode)
{
	OpCode_SysInit(opcode);
}


/**
 * Handle illegal opcode #8 (GEMDOS_OPCODE).
 * When GEMDOS HD emulation is enabled, we use it to intercept GEMDOS
 * calls (see gemdos.c).
 */
uae_u32 REGPARAM3 OpCode_GemDos(uae_u32 opcode)
{
	if (is_cart_pc())
	{
		GemDOS_Trap();
		CpuDoNOP();
	}
	else
	{
		LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS opcode invoked outside of cartridge space\n");
		/* illegal instruction */
		op_illg(opcode);
		fill_prefetch();
	}

	return 4 * CYCLE_UNIT / 2;
}

void REGPARAM3 OpCode_GemDos_noret(uae_u32 opcode)
{
	OpCode_GemDos(opcode);
}


/**
 * Handle illegal opcode #9 (PEXEC_OPCODE).
 * When GEMDOS HD emulation is enabled, we use it to intercept the end of
 * the Pexec call (see gemdos.c).
 */
uae_u32 REGPARAM3 OpCode_Pexec(uae_u32 opcode)
{
	if (is_cart_pc())
	{
		GemDOS_PexecBpCreated();
		CpuDoNOP();
	}
	else
	{
		LOG_TRACE(TRACE_OS_GEMDOS, "PEXEC opcode invoked outside of cartridge space\n");
		/* illegal instruction */
		op_illg(opcode);
		fill_prefetch();
	}

	return 4 * CYCLE_UNIT / 2;
}

void REGPARAM3 OpCode_Pexec_noret(uae_u32 opcode)
{
	OpCode_Pexec(opcode);
}


/**
 * This is called after completion of each VDI call
 */
uae_u32 REGPARAM3 OpCode_VDI(uae_u32 opcode)
{
	/* this is valid only after VDI trap, called from cartridge code */
	if (VDI_OldPC && is_cart_pc())
	{
		VDI_Complete();

		/* Set PC back to where originated from to continue instruction decoding */
		m68k_setpc(VDI_OldPC);
		VDI_OldPC = 0;
	}
	else
	{
		LOG_TRACE(TRACE_OS_VDI, "VDI opcode invoked outside of cartridge space\n");
		/* illegal instruction */
		op_illg(opcode);
	}

	fill_prefetch();
	return 4 * CYCLE_UNIT / 2;
}

void REGPARAM3 OpCode_VDI_noret(uae_u32 opcode)
{
	OpCode_VDI(opcode);
}


/**
 * Emulator Native Features ID opcode interception.
 */
uae_u32 REGPARAM3 OpCode_NatFeat_ID(uae_u32 opcode)
{
	uint32_t stack = Regs[REG_A7] + SIZE_LONG;	/* skip return address */

	if (NatFeat_ID(stack, &(Regs[REG_D0])))
	{
		CpuDoNOP ();
	}
	return 4 * CYCLE_UNIT / 2;
}

void REGPARAM3 OpCode_NatFeat_ID_noret(uae_u32 opcode)
{
	OpCode_NatFeat_ID(opcode);
}


/**
 * Emulator Native Features call opcode interception.
 */
uae_u32 REGPARAM3 OpCode_NatFeat_Call(uae_u32 opcode)
{
	uint32_t stack = Regs[REG_A7] + SIZE_LONG;	/* skip return address */
	uint16_t SR = M68000_GetSR();
	bool super;

	super = ((SR & SR_SUPERMODE) == SR_SUPERMODE);
	if (NatFeat_Call(stack, super, &(Regs[REG_D0])))
	{
		CpuDoNOP ();
	}
	return 4 * CYCLE_UNIT / 2;
}

void REGPARAM3 OpCode_NatFeat_Call_noret(uae_u32 opcode)
{
	OpCode_NatFeat_Call(opcode);
}


TCHAR* buf_out (TCHAR *buffer, int *bufsize, const TCHAR *format, ...)
{
	va_list parms;
	int count;

	if (buffer == NULL)
	{
		return NULL;
	}

	va_start (parms, format);
	vsnprintf (buffer, (*bufsize) - 1, format, parms);
	va_end (parms);

	count = _tcslen (buffer);
	*bufsize -= count;

	return buffer + count;
}

void error_log(const TCHAR *format, ...)
{
	va_list parms;

	va_start(parms, format);
	vfprintf(stderr, format, parms);
	va_end(parms);

	if (format[strlen(format) - 1] != '\n')
	{
		fputc('\n', stderr);
	}
}
