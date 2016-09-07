/*
  Hatari - m68000.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  These routines originally (in WinSTon) handled exceptions as well as some
  few OpCode's such as Line-F and Line-A. In Hatari it has mainly become a
  wrapper between the WinSTon sources and the UAE CPU code.
*/

/* 2007/03/xx	[NP]	Possibility to add several wait states for the same instruction in		*/
/*			M68000_WaitState (e.g. clr.b $fa1b.w in Decade Demo Menu).			*/
/* 2007/04/14	[NP]	Add support for instruction pairing in M68000_AddCycles, using OpcodeFamily and	*/
/*			LastOpcodeFamily (No Cooper Loader, Oh Crickey ... Hidden Screen).		*/
/* 2007/04/24	[NP]	Add pairing for BCLR/Bcc.							*/
/* 2007/09/29	[NP]	Use the new int.c and INT_CONVERT_TO_INTERNAL.					*/
/* 2007/11/26	[NP]	We set BusErrorPC in m68k_run_1 instead of M68000_BusError, else the BusErrorPC	*/
/*			will not point to the opcode that generated the bus error.			*/
/*			In M68000_BusError, if we have 'move.l $0,$24', we need to generate a bus error	*/
/*			for the read, not for the write that should occur after (TransBeauce 2 Demo).	*/
/* 2008/01/07	[NP]	Function 'M68000_InitPairing' and 'PairingArray' as a lookup table for fast	*/
/*			determination of valid pairing combinations (replace lots of 'if' tests in	*/
/*			m68000.h).									*/
/* 2008/01/25	[NP]	Add pairing for LSR/MOVE (and all other bit shifting instr) (Anomaly Demo Intro)*/
/* 2008/02/02	[NP]	Add pairing for CMP/Bcc (Level 16 Fullscreen (1988)).				*/
/* 2008/02/08	[NP]	Add pairing for LSL/LEA (and all other bit shifting instr) (TVI 2 - The Year	*/
/*			After Demo).									*/
/* 2008/02/11	[NP]	Add pairing for MULS/MOVEA (Delirious Demo IV Loader).				*/
/* 2008/01/25	[NP]	Add pairing for LSR/MOVEA (and all other bit shifting instr) (Decade Demo Reset)*/
/* 2008/02/16	[NP]	Add pairing for MULS/DIVS (fixes e605 demo part 3).				*/
/* 2008/03/08	[NP]	In M68000_Exception, we need to know if the exception was triggered by an MFP	*/
/*			interrupt or by a video interrupt. In the case MFP vector base was changed in	*/
/*			fffa17 to an other value than the default $40, testing exceptionNr is not enough*/
/*			to correctly process the exception. For example, if vector base is set to $10	*/
/*			then MFP Timer A will call vector stored at address $74, which would be wrongly	*/
/*			interpreted as a level 5 int (which doesn't exist on Atari and will cause an	*/
/*			assert to fail in intlevel()). We use InterruptSource to correctly recognize the*/
/*			MFP interrupts (fix 'Toki' end part fullscreen which sets vector base to $10).	*/
/* 2008/04/14	[NP]	Add pairing for BTST/Bcc (eg btst #7,d0 + bne.s label  (with branch taken)).	*/
/* 2008/04/15	[NP]	As tested on a real STF :							*/
/*				- MUL/DIV can pair (but DIV/MUL can't)					*/
/*					(eg mulu d0,d0 + divs d1,d1 with d0=0 and d1=1)			*/
/*				- MUL/MOVE can pair, but not DIV/MOVE					*/
/*				- EXG/MOVE can pair (eg exg d3,d4 + move.l 0(a3,d1.w),a4)		*/
/*				- MOVE/DBcc can't pair							*/
/* 2008/04/16	[NP]	Functions 'M68000_InitPairing_BitShift' to ease code maintenance.		*/
/*			Tested on STF : add pairing between bit shift instr and ADD/SUB/OR/AND/EOR/NOT	*/
/*			CLR/NEG (certainly some more possible, haven't tested everything)		*/
/*			(fixes lsr.w #4,d4 + add.b $f0(a4,d4),d7 used in Zoolook part of ULM New Year).	*/
/* 2008/07/08	[NP]	Add pairing between bit shift instr and ADDX/SUBX/ABCD/SBCD (fixes lsl.l #1,d0	*/
/*			+ abcd d1,d1 used in Dragonnels - Rainbow Wall).				*/
/* 2008/10/05	[NP]	Pass the 'ExceptionSource' parameter to Exception() in uae-cpu/newcpu.c		*/
/* 2010/05/07	[NP]	Add pairing for ADD/MOVE ; such pairing should only be possible when combined	*/
/*			with d8(an,ix) address mode (eg: add.l (a5,d1.w),d0 + move.b 7(a5,d1.w),d5)	*/
/*			(fixes Sommarhack 2010 Invitation by DHS).					*/
/* 2010/11/07	[NP]	Add pairing between bit shift instr and JMP (fixes lsl.w #2,d0 + jmp 2(pc,d0)	*/
/*			used in Fullparts by Hemoroids).						*/
/* 2011/12/11	[NP]	Add pairing between MUL and JSR (fixes muls #52,d2 + jsr 0(a1,d2.w) used in	*/
/*			Lemmings Compilation 40's Intro).						*/
/* 2014/05/07	[NP]	In M68000_WaitEClock, use CyclesGlobalClockCounter instead of the VBL video	*/
/*			counter (else for a given position in a VBL we would always get the same value	*/
/*			for the E clock).								*/
/* 2015/02/01	[NP]	When using the new WinUAE's cpu, don't handle MFP/DSP interrupts by calling	*/
/*			directly Exception(), we must set bit 6 in pendingInterrupts and use the IACK	*/
/*			sequence to get the exception's vector number.					*/
/* 2015/02/05	[NP]	For the new WinUAE's cpu, don't use ExceptionSource anymore when calling	*/
/*			Exception().									*/
/* 2015/02/11	[NP]	Replace BusErrorPC by regs.instruction_pc, to get similar code to WinUAE's cpu  */
/* 2015/10/08	[NP]	Add M68000_AddCycles_CE() to handle cycles when running with WinUAE's cpu in	*/
/*			'cycle exact' mode. In that case, instruction pairing don't have to be handled	*/
/*			with some tables/heuristics anymore.						*/


const char M68000_fileid[] = "Hatari m68000.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "gemdos.h"
#include "hatari-glue.h"
#include "cycInt.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "options.h"
#include "savestate.h"
#include "stMemory.h"
#include "tos.h"

#if ENABLE_DSP_EMU
#include "dsp.h"
#endif

#if ENABLE_WINUAE_CPU
#include "mmu_common.h"
#endif

/* information about current CPU instruction */
cpu_instruction_t CpuInstruction;

Uint32 BusErrorAddress;		/* Stores the offending address for bus-/address errors */
bool bBusErrorReadWrite;	/* 0 for write error, 1 for read error */
int nCpuFreqShift;		/* Used to emulate higher CPU frequencies: 0=8MHz, 1=16MHz, 2=32Mhz */
int WaitStateCycles = 0;	/* Used to emulate the wait state cycles of certain IO registers */
int BusMode = BUS_MODE_CPU;	/* Used to tell which part is owning the bus (cpu, blitter, ...) */
bool CPU_IACK = false;		/* Set to true during an exception when getting the interrupt's vector number */

int LastOpcodeFamily = i_NOP;	/* see the enum in readcpu.h i_XXX */
int LastInstrCycles = 0;	/* number of cycles for previous instr. (not rounded to 4) */
int Pairing = 0;		/* set to 1 if the latest 2 intr paired */
char PairingArray[ MAX_OPCODE_FAMILY ][ MAX_OPCODE_FAMILY ];


/* to convert the enum from OpcodeFamily to a readable value for pairing's debug */
const char *OpcodeName[] = { "ILLG",
	"OR","AND","EOR","ORSR","ANDSR","EORSR",
	"SUB","SUBA","SUBX","SBCD",
	"ADD","ADDA","ADDX","ABCD",
	"NEG","NEGX","NBCD","CLR","NOT","TST",
	"BTST","BCHG","BCLR","BSET",
	"CMP","CMPM","CMPA",
	"MVPRM","MVPMR","MOVE","MOVEA","MVSR2","MV2SR",
	"SWAP","EXG","EXT","MVMEL","MVMLE",
	"TRAP","MVR2USP","MVUSP2R","RESET","NOP","STOP","RTE","RTD",
	"LINK","UNLK",
	"RTS","TRAPV","RTR",
	"JSR","JMP","BSR","Bcc",
	"LEA","PEA","DBcc","Scc",
	"DIVU","DIVS","MULU","MULS",
	"ASR","ASL","LSR","LSL","ROL","ROR","ROXL","ROXR",
	"ASRW","ASLW","LSRW","LSLW","ROLW","RORW","ROXLW","ROXRW",
	"CHK","CHK2",
	"MOVEC2","MOVE2C","CAS","CAS2","DIVL","MULL",
	"BFTST","BFEXTU","BFCHG","BFEXTS","BFCLR","BFFFO","BFSET","BFINS",
	"PACK","UNPK","TAS","BKPT","CALLM","RTM","TRAPcc","MOVES",
	"FPP","FDBcc","FScc","FTRAPcc","FBcc","FSAVE","FRESTORE",
	"CINVL","CINVP","CINVA","CPUSHL","CPUSHP","CPUSHA","MOVE16",
	"MMUOP"
};


/*-----------------------------------------------------------------------*/
/**
 * Add pairing between all the bit shifting instructions and a given Opcode
 */

static void M68000_InitPairing_BitShift ( int OpCode )
{
	PairingArray[  i_ASR ][ OpCode ] = 1; 
	PairingArray[  i_ASL ][ OpCode ] = 1; 
	PairingArray[  i_LSR ][ OpCode ] = 1; 
	PairingArray[  i_LSL ][ OpCode ] = 1; 
	PairingArray[  i_ROL ][ OpCode ] = 1; 
	PairingArray[  i_ROR ][ OpCode ] = 1; 
	PairingArray[ i_ROXR ][ OpCode ] = 1; 
	PairingArray[ i_ROXL ][ OpCode ] = 1; 
}


/**
 * Init the pairing matrix
 * Two instructions can pair if PairingArray[ LastOpcodeFamily ][ OpcodeFamily ] == 1
 */
static void M68000_InitPairing(void)
{
	/* First, clear the matrix (pairing is false) */
	memset(PairingArray , 0 , MAX_OPCODE_FAMILY * MAX_OPCODE_FAMILY);

	/* Set all valid pairing combinations to 1 */
	PairingArray[  i_EXG ][ i_DBcc ] = 1;
	PairingArray[  i_EXG ][ i_MOVE ] = 1;
	PairingArray[  i_EXG ][ i_MOVEA] = 1;

	PairingArray[ i_CMPA ][  i_Bcc ] = 1;
	PairingArray[  i_CMP ][  i_Bcc ] = 1;

	M68000_InitPairing_BitShift ( i_DBcc );
	M68000_InitPairing_BitShift ( i_MOVE );
	M68000_InitPairing_BitShift ( i_MOVEA );
	M68000_InitPairing_BitShift ( i_LEA );
	M68000_InitPairing_BitShift ( i_JMP );

	PairingArray[ i_MULU ][ i_MOVEA] = 1; 
	PairingArray[ i_MULS ][ i_MOVEA] = 1; 
	PairingArray[ i_MULU ][ i_MOVE ] = 1; 
	PairingArray[ i_MULS ][ i_MOVE ] = 1; 

	PairingArray[ i_MULU ][ i_DIVU ] = 1;
	PairingArray[ i_MULU ][ i_DIVS ] = 1;
	PairingArray[ i_MULS ][ i_DIVU ] = 1;
	PairingArray[ i_MULS ][ i_DIVS ] = 1;

	PairingArray[ i_MULU ][ i_JSR ] = 1;
	PairingArray[ i_MULS ][ i_JSR ] = 1;

	PairingArray[ i_BTST ][  i_Bcc ] = 1;

	M68000_InitPairing_BitShift ( i_ADD );
	M68000_InitPairing_BitShift ( i_SUB );
	M68000_InitPairing_BitShift ( i_OR );
	M68000_InitPairing_BitShift ( i_AND );
	M68000_InitPairing_BitShift ( i_EOR );
	M68000_InitPairing_BitShift ( i_NOT );
	M68000_InitPairing_BitShift ( i_CLR );
	M68000_InitPairing_BitShift ( i_NEG );
	M68000_InitPairing_BitShift ( i_ADDX );
	M68000_InitPairing_BitShift ( i_SUBX );
	M68000_InitPairing_BitShift ( i_ABCD );
	M68000_InitPairing_BitShift ( i_SBCD );

	PairingArray[ i_ADD ][ i_MOVE ] = 1;		/* when using xx(an,dn) addr mode */
	PairingArray[ i_SUB ][ i_MOVE ] = 1;

	PairingArray[ i_ABCD ][ i_DBcc ] = 1;
	PairingArray[ i_SBCD ][ i_DBcc ] = 1;
}


/**
 * One-time CPU initialization.
 */
void M68000_Init(void)
{
	/* Init UAE CPU core */
	Init680x0();

	/* Init the pairing matrix */
	M68000_InitPairing();
}


/*-----------------------------------------------------------------------*/
/**
 * Reset CPU 68000 variables
 */
void M68000_Reset(bool bCold)
{
#if ENABLE_WINUAE_CPU
	int spcFlags = regs.spcflags & (SPCFLAG_MODE_CHANGE | SPCFLAG_BRK);
	if (bCold)
	{
		memset(&regs, 0, sizeof(regs));
	}
	/* Now reset the WINUAE CPU core */
	m68k_reset();

        /* On Hatari, when we change cpu settings, we call m68k_reset() during m68k_run_xx(), */
	/* so we must keep the value of bits SPCFLAG_MODE_CHANGE and SPCFLAG_BRK to exit m68k_run_xx() */
	/* and choose a new m68k_run_xx() function */
	/* [NP] TODO : don't force a reset when changing cpu settings and use common code with WinUAE ? */
        regs.spcflags |= spcFlags;

#else /* UAE CPU core */
	if (bCold)
	{
		/* Clear registers */
		memset(&regs, 0, sizeof(regs));
	}
	/* Now reset the UAE CPU core */
	m68k_reset();
#endif
	BusMode = BUS_MODE_CPU;
	CPU_IACK = false;
}


/*-----------------------------------------------------------------------*/
/**
 * Start 680x0 emulation
 */
void M68000_Start(void)
{
	/* Load initial memory snapshot */
	if (bLoadMemorySave)
	{
		MemorySnapShot_Restore(ConfigureParams.Memory.szMemoryCaptureFileName, false);
	}
	else if (bLoadAutoSave)
	{
		MemorySnapShot_Restore(ConfigureParams.Memory.szAutoSaveFileName, false);
	}

	m68k_go(true);
}


/*-----------------------------------------------------------------------*/
/**
 * Check whether CPU settings have been changed.
 * Possible values for WinUAE :
 *	cpu_model : 68000 , 68010, 68020, 68030, 68040, 68060
 *	cpu_level : not used anymore
 *	cpu_compatible : 0/false (no prefetch for 68000/20/30)  1/true (prefetch opcode for 68000/20/30)
 *	cpu_cycle_exact : 0/false   1/true (most accurate, implies cpu_compatible)
 *	cpu_memory_cycle_exact : 0/false   1/true (most accurate, implies cpu_compatible)
 *	address_space_24 : 1 (68000/10 and 68030 LC for Falcon), 0 (68020/30/40/60)
 *	fpu_model : 0, 68881 (external), 68882 (external), 68040 (cpu) , 68060 (cpu)
 *	fpu_strict : true/false (more accurate rounding)
 *	mmu_model : 0, 68030, 68040, 68060
 *
 *	m68k_speed : -1=don't adjust cycle  >=0 use m68k_speed_throttle to precisely adjust cycles
 *	m68k_speed_throttle : if not 0, used to set cycles_mult. In Hatari, set it to 0
 *	cpu_frequency : in CE mode, fine control of cpu freq, set it to freq/2. Not used in Hatari, set it to 0.
 *	cpu_clock_multiplier : used to speed up/slow down clock by multiple of 2 in CE mode. In Hatari
 *			we use nCpuFreqShift, so this should always be set to 2<<8 = 512 to get the same
 *			cpucycleunit as in non CE mode.
 *	cachesize : size of cache in MB when using JIT. Not used in Hatari at the moment, set it to 0
 */
void M68000_CheckCpuSettings(void)
{
	if (ConfigureParams.System.nCpuFreq < 12)
	{
		ConfigureParams.System.nCpuFreq = 8;
		nCpuFreqShift = 0;
	}
	else if (ConfigureParams.System.nCpuFreq > 26)
	{
		ConfigureParams.System.nCpuFreq = 32;
		nCpuFreqShift = 2;
	}
	else
	{
		ConfigureParams.System.nCpuFreq = 16;
		nCpuFreqShift = 1;
	}
	changed_prefs.cpu_level = ConfigureParams.System.nCpuLevel;
	changed_prefs.cpu_compatible = ConfigureParams.System.bCompatibleCpu;

#if ENABLE_WINUAE_CPU
	/* WinUAE core uses cpu_model instead of cpu_level, so we've got to
	 * convert these values here: */
	switch (changed_prefs.cpu_level) {
		case 0 : changed_prefs.cpu_model = 68000; break;
		case 1 : changed_prefs.cpu_model = 68010; break;
		case 2 : changed_prefs.cpu_model = 68020; break;
		case 3 : changed_prefs.cpu_model = 68030; break;
		case 4 : changed_prefs.cpu_model = 68040; break;
		case 5 : changed_prefs.cpu_model = 68060; break;
		default: fprintf (stderr, "Init680x0() : Error, cpu_level unknown\n");
	}
	currprefs.cpu_level = changed_prefs.cpu_level;

	changed_prefs.address_space_24 = ConfigureParams.System.bAddressSpace24;
	changed_prefs.cpu_cycle_exact = ConfigureParams.System.bCycleExactCpu;
	changed_prefs.cpu_memory_cycle_exact = ConfigureParams.System.bCycleExactCpu;
	changed_prefs.fpu_model = ConfigureParams.System.n_FPUType;
	changed_prefs.fpu_strict = ConfigureParams.System.bCompatibleFPU;

	/* Update the MMU model by taking the same value as CPU model */
	/* MMU is only supported for CPU >=68030, this is later checked in custom.c fixup_cpu() */
	if ( !ConfigureParams.System.bMMU )
		changed_prefs.mmu_model = 0;				/* MMU disabled */
	else
		changed_prefs.mmu_model = changed_prefs.cpu_model;	/* MMU enabled */

	/* Set cpu speed to default values (only use in WinUAE, not in Hatari) */
	currprefs.m68k_speed = changed_prefs.m68k_speed = 0;
	currprefs.cpu_clock_multiplier = changed_prefs.cpu_clock_multiplier = 2 << 8;

	/* We don't use JIT */
	currprefs.cachesize = changed_prefs.cachesize = 0;
#else
	if (ConfigureParams.System.nCpuLevel > 4)
		ConfigureParams.System.nCpuLevel = 4;

	changed_prefs.cpu_cycle_exact = 0;				/* With old UAE CPU, cycle_exact is always false */
#endif

	if (table68k)
		check_prefs_changed_cpu();
}


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of CPU variables ('MemorySnapShot_Store' handles type)
 */
void M68000_MemorySnapShot_Capture(bool bSave)
{
#if ENABLE_WINUAE_CPU
	int len;
	uae_u8 chunk[ 1000 ];

	if (bSave)
	{
		//m68k_dumpstate_file(stderr, NULL);
		save_cpu (&len,chunk);
		//printf ( "save cpu done\n"  );
		save_cpu_extra (&len,chunk);
		//printf ( "save cpux done\n" );
		save_fpu (&len,chunk);
		//printf ( "save fpu done\n"  );
		save_mmu (&len,chunk);
		//printf ( "save mmu done\n"  );
		//m68k_dumpstate_file(stderr, NULL);
	}
	else
	{
		//m68k_dumpstate_file(stderr, NULL);
		restore_cpu (chunk);
		//printf ( "restore cpu done\n" );
		restore_cpu_extra (chunk);
		//printf ( "restore cpux done\n" );
		restore_fpu (chunk);
		//printf ( "restore fpu done\n"  );
		restore_mmu (chunk);
		//printf ( "restore mmu done\n"  );

		restore_cpu_finish ();
		if ( regs.s )	regs.regs[15] = regs.isp;
		else		regs.regs[15] = regs.usp;
		//m68k_dumpstate_file(stderr, NULL);
	}

#else /* UAE CPU core */
	Uint32 savepc;

	/* For the UAE CPU core: */
	MemorySnapShot_Store(&currprefs.address_space_24,
	                     sizeof(currprefs.address_space_24));
	MemorySnapShot_Store(&regs.regs[0], sizeof(regs.regs));       /* D0-D7 A0-A6 */

	if (bSave)
	{
		savepc = M68000_GetPC();
		MemorySnapShot_Store(&savepc, sizeof(savepc));            /* PC */
	}
	else
	{
		MemorySnapShot_Store(&savepc, sizeof(savepc));            /* PC */
		regs.pc = savepc;
		regs.prefetch_pc = regs.pc + 128;
	}

	MemorySnapShot_Store(&regs.prefetch, sizeof(regs.prefetch));  /* prefetch */

	if (bSave)
	{
		MakeSR();
		if (regs.s)
		{
			MemorySnapShot_Store(&regs.usp, sizeof(regs.usp));    /* USP */
			MemorySnapShot_Store(&regs.regs[15], sizeof(regs.regs[15]));  /* ISP */
		}
		else
		{
			MemorySnapShot_Store(&regs.regs[15], sizeof(regs.regs[15]));  /* USP */
			MemorySnapShot_Store(&regs.isp, sizeof(regs.isp));    /* ISP */
		}
		MemorySnapShot_Store(&regs.sr, sizeof(regs.sr));          /* SR/CCR */
	}
	else
	{
		MemorySnapShot_Store(&regs.usp, sizeof(regs.usp));
		MemorySnapShot_Store(&regs.isp, sizeof(regs.isp));
		MemorySnapShot_Store(&regs.sr, sizeof(regs.sr));
	}
	MemorySnapShot_Store(&regs.opcode, sizeof(regs.opcode));
	MemorySnapShot_Store(&regs.instruction_pc, sizeof(regs.instruction_pc));
	MemorySnapShot_Store(&regs.stopped, sizeof(regs.stopped));
	MemorySnapShot_Store(&regs.dfc, sizeof(regs.dfc));            /* DFC */
	MemorySnapShot_Store(&regs.sfc, sizeof(regs.sfc));            /* SFC */
	MemorySnapShot_Store(&regs.vbr, sizeof(regs.vbr));            /* VBR */
	MemorySnapShot_Store(&caar, sizeof(caar));                    /* CAAR */
	MemorySnapShot_Store(&cacr, sizeof(cacr));                    /* CACR */
	MemorySnapShot_Store(&regs.msp, sizeof(regs.msp));            /* MSP */

	if (!bSave)
	{
		M68000_SetPC(regs.pc);
		/* MakeFromSR() must not swap stack pointer */
		regs.s = (regs.sr >> 13) & 1;
		MakeFromSR();
		/* set stack pointer */
		if (regs.s)
			m68k_areg(regs, 7) = regs.isp;
		else
			m68k_areg(regs, 7) = regs.usp;
	}

	if (bSave)
		save_fpu();
	else
		restore_fpu();
#endif
}


/*-----------------------------------------------------------------------*/
/**
 * BUSERROR - Access outside valid memory range.
 * Use bRead = 0 for write errors and bRead = 1 for read errors!
 */
void M68000_BusError ( Uint32 addr , int ReadWrite , int Size , int AccessType )
{
	Uint32 InstrPC = M68000_InstrPC;

	/* Do not print message when TOS is testing for available HW or
	 * when a program just checks for the floating point co-processor. */
	if ((InstrPC < TosAddress || InstrPC > TosAddress + TosSize)
	    && addr != 0xfffa42)
	{
		/* Print bus error message */
		fprintf(stderr, "M68000 Bus Error %s at address $%x PC=$%x.\n",
			ReadWrite ? "reading" : "writing", addr, InstrPC);
	}

#ifndef ENABLE_WINUAE_CPU
	if ((regs.spcflags & SPCFLAG_BUSERROR) == 0)		/* [NP] Check that the opcode has not already generated a read bus error */
	{
		BusErrorAddress = addr;				/* Store for exception frame */
		bBusErrorReadWrite = ReadWrite;
		M68000_SetSpecial(SPCFLAG_BUSERROR);		/* The exception will be done in newcpu.c */
	}

#else
	/* With WinUAE's cpu, on a bus error instruction will be correctly aborted before completing, */
	/* so we don't need to check if the opcode already generated a bus error or not */
	exception2 ( addr , ReadWrite , Size , AccessType );
#endif
}


/*-----------------------------------------------------------------------*/
/**
 * Exception handler
 */
void M68000_Exception(Uint32 ExceptionNr , int ExceptionSource)
{
#ifndef WINUAE_FOR_HATARI
	if ((ExceptionSource == M68000_EXC_SRC_AUTOVEC)
		&& (ExceptionNr>24 && ExceptionNr<32))		/* 68k autovector interrupt? */
	{
		/* Handle autovector interrupts the UAE's way
		 * (see intlev() and do_specialties() in UAE CPU core) */
		/* In our case, this part is only called for HBL and VBL interrupts */
		int intnr = ExceptionNr - 24;
		pendingInterrupts |= (1 << intnr);
		M68000_SetSpecial(SPCFLAG_INT);
	}

	else							/* MFP or direct CPU exceptions */
	{
		Uint16 SR;

		/* Was the CPU stopped, i.e. by a STOP instruction? */
		if (regs.spcflags & SPCFLAG_STOP)
		{
			regs.stopped = 0;
			M68000_UnsetSpecial(SPCFLAG_STOP);    /* All is go,go,go! */
		}

		/* 68k exceptions are handled by Exception() of the UAE CPU core */
		Exception(ExceptionNr, m68k_getpc(), ExceptionSource);

		/* Set Status Register so interrupt can ONLY be stopped by another interrupt
		 * of higher priority */
		if ( (ExceptionSource == M68000_EXC_SRC_INT_MFP)
		  || (ExceptionSource == M68000_EXC_SRC_INT_DSP) )
		{
			SR = M68000_GetSR();
			SR = (SR&SR_CLEAR_IPL)|0x0600;		/* MFP or DSP, level 6 */
			M68000_SetSR(SR);
		}
	}

#else
	if ( ExceptionNr > 24 && ExceptionNr < 32 )		/* Level 1-7 interrupts */
	{
		/* In our case, this part is called for HBL, VBL and MFP/DSP interrupts */
		/* For WinUAE CPU, we must call M68000_Update_intlev after changing pendingInterrupts */
		/* (in order to call doint() and to update regs.ipl with regs.ipl_pin, else */
		/* the exception might be delayed by one instruction in do_specialties()) */
		pendingInterrupts |= (1 << ( ExceptionNr - 24 ));
		M68000_Update_intlev();
	}

	else							/* direct CPU exceptions */
	{
		Exception(ExceptionNr);
	}
#endif
}




/*-----------------------------------------------------------------------*/
/**
 * Update the list of pending interrupts.
 * Level 2 (HBL) and 4 (VBL) are only cleared when the interrupt is processed,
 * but level 6 is shared between MFP and DSP and can be cleared by MFP or DSP
 * before being processed.
 * So, we need to check which IRQ are set/cleared at the same time
 * and update level 6 accordingly : level 6 = MFP_IRQ OR DSP_IRQ
 *
 * [NP] NOTE : temporary case for interrupts with WinUAE CPU in cycle exact mode
 * In CE mode, interrupt state should be updated on each subcycle of every opcode
 * then ipl_fetch() is called in each opcode.
 * For now, Hatari with WinUAE CPU in CE mode only evaluates the interrupt state
 * after the end of each opcode. So we need to call ipl_fetch() ourselves at the moment.
 */
void	M68000_Update_intlev ( void )
{	
#ifdef WINUAE_FOR_HATARI
	Uint8	Level6_IRQ;

#if ENABLE_DSP_EMU
	Level6_IRQ = MFP_GetIRQ_CPU() | DSP_GetHREQ();
#else
	Level6_IRQ = MFP_GetIRQ_CPU();
#endif
	if ( Level6_IRQ == 1 )
		pendingInterrupts |= (1 << 6);
	else
		pendingInterrupts &= ~(1 << 6);

	if ( pendingInterrupts )
		doint();
	else
		M68000_UnsetSpecial ( SPCFLAG_INT | SPCFLAG_DOINT );

	/* Temporary case for WinUAE CPU in CE mode */
	/* doint() will update regs.ipl_pin, so copy it into regs.ipl */
	if ( ConfigureParams.System.bCycleExactCpu )
		regs.ipl = regs.ipl_pin;			/* See ipl_fetch() in cpu/cpu_prefetch.h */
#endif
}




/*-----------------------------------------------------------------------*/
/**
 * There are some wait states when accessing certain hardware registers on the ST.
 * This function simulates these wait states and add the corresponding cycles.
 *
 * [NP] with some instructions like CLR, we have a read then a write at the
 * same location, so we may have 2 wait states (read and write) to add
 * (WaitStateCycles should be reset to 0 after all the cycles were added
 * in run_xx() in newcpu.c).
 *
 * - When CPU runs in cycle exact mode, wait states are added immediately.
 * - For other less precise modes, all the wait states are cumulated and added
 *   after the instruction was processed.
 */
void M68000_WaitState(int WaitCycles)
{
#ifndef WINUAE_FOR_HATARI
	WaitStateCycles += WaitCycles;				/* Cumulate all the wait states for this instruction */

#else
	if ( ConfigureParams.System.bCycleExactCpu )
		currcycle += ( WaitCycles * CYCLE_UNIT / 2 );	/* Add wait states immediately to the CE cycles counter */
	else
	{
		WaitStateCycles += WaitCycles;			/* Cumulate all the wait states for this instruction */
	}
#endif
}



/*-----------------------------------------------------------------------*/
/**
 * Some components (HBL/VBL interrupts, access to the ACIA) require an
 * extra delay to be synchronized with the E Clock.
 * E Clock's frequency is 1/10th of the CPU, ie 0.8 MHz in an STF/STE
 * This delay is a multiple of 2 and will follow the pattern [ 0 8 6 4 2 ]
 */
int	M68000_WaitEClock ( void )
{
	int	CyclesToNextE;

	/* We must wait for the next multiple of 10 cycles to be synchronised with E Clock */
	CyclesToNextE = 10 - CyclesGlobalClockCounter % 10;
	if ( CyclesToNextE == 10 )		/* we're already synchronised with E Clock */
		CyclesToNextE = 0;
	return CyclesToNextE;
}




/*-----------------------------------------------------------------------*/
/**
 * Some hardware registers can only be accessed on a 4 cycles boundary
 * (shifter color regs and shifter res reg).
 * An extra delay should be added when needed if current cycle
 * count is not multiple of 4.
 */
static void	M68000_SyncCpuBus ( bool read )
{
	Uint64	Cycles;
	int	CyclesToNextBus;

	if ( read )
		Cycles = Cycles_GetClockCounterOnReadAccess();
	else
		Cycles = Cycles_GetClockCounterOnWriteAccess();

	CyclesToNextBus = Cycles & 3;
//fprintf ( stderr , "sync bus %lld %d\n" , Cycles, CyclesToNextBus );
	if ( CyclesToNextBus != 0 )
	{
//fprintf ( stderr , "sync bus wait %lld %d\n" ,Cycles, 4-CyclesToNextBus );
		M68000_WaitState ( 4 - CyclesToNextBus );
	}
}


void	M68000_SyncCpuBus_OnReadAccess ( void )
{
	M68000_SyncCpuBus ( true );
}


void	M68000_SyncCpuBus_OnWriteAccess ( void )
{
	M68000_SyncCpuBus ( false );
}




/*-----------------------------------------------------------------------*/
/**
 * In case we modified the memory by accessing it directly (and bypassing
 * the CPU's cache mechanism), we need to flush the instruction and data
 * caches to force an update of the caches on the next accesses.
 *
 * [NP] NOTE : for now, flush_instr_caches and flush_dcache flush
 * the whole caches, not just 'addr'
 */
void	M68000_Flush_All_Caches ( uaecptr addr , int size )
{
#ifdef WINUAE_FOR_HATARI
	M68000_Flush_Instr_Cache ( addr , size );
	M68000_Flush_Data_Cache ( addr , size );
#endif
}


void	M68000_Flush_Instr_Cache ( uaecptr addr , int size )
{
#ifdef WINUAE_FOR_HATARI
	/* Instruction cache for cpu >= 68020 */
	flush_instr_cache ( addr , size );
#endif
}


void	M68000_Flush_Data_Cache ( uaecptr addr , int size )
{
#ifdef WINUAE_FOR_HATARI
	/* Data cache for cpu >= 68030 is only emulated with WinUAE */
	flush_dcache ( addr , size );
#endif
}



/*-----------------------------------------------------------------------*/
/**
 * On real STF/STE hardware, DMA accesses are restricted to 4 MB (video addresses,
 * FDC, STE DMA sound) in the range 0 - $3fffff (22 bits of address)
 * When STF/STE are expanded beyond 4 MB, some special '_FRB' cookies variables
 * need to be set in TOS to allocate an intermediate buffer in lower 4 MB
 * that will be used to transfer data in RAM between 4 MB and 16 MB.
 * This buffer is needed because real HW can't access RAM beyond 4 MB.
 *
 * In Hatari, we allow DMA addresses to use 24 bits when RAM size is set
 * to 8 or 16 MB. This way any program / TOS version can make use of extra
 * RAM beyond 4 MB without requiring an intermediate buffer.
 *
 * But it should be noted that programs using 24 bits of DMA addresses would
 * not work on real HW ; this is just to make RAM expansion more transparent
 * under emulation.
 *
 * We return a mask for bits 16-23 :
 *  - 0x3f for compatibility with real HW and limit of 4 MB
 *  - 0xff to allow DMA addresses beyond 4 MB (or for Falcon / TT)
 */
int	DMA_MaskAddressHigh ( void )
{
	if (Config_IsMachineTT() || Config_IsMachineFalcon())
		return 0xff;					/* Falcon / TT can access 24 bits with DMA */

	else if ( ConfigureParams.Memory.nMemorySize > 4 )	/* ST/STE with more than 4 MB */
		return 0xff;					/* Allow 'fake' 24 bits for DMA */

	else							/* ST/STE with <= 4 MB */
		return 0x3f;					/* Limit DMA range to 22 bits (same as real HW) */
}
