/*
  Hatari - m68000.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

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


const char M68000_fileid[] = "Hatari m68000.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "gemdos.h"
#include "hatari-glue.h"
#include "int.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "options.h"
#include "savestate.h"
#include "stMemory.h"
#include "tos.h"


Uint32 BusErrorAddress;         /* Stores the offending address for bus-/address errors */
Uint32 BusErrorPC;              /* Value of the PC when bus error occurs */
bool bBusErrorReadWrite;        /* 0 for write error, 1 for read error */
int nCpuFreqShift;              /* Used to emulate higher CPU frequencies: 0=8MHz, 1=16MHz, 2=32Mhz */
int nWaitStateCycles;           /* Used to emulate the wait state cycles of certain IO registers */
int BusMode = BUS_MODE_CPU;	/* Used to tell which part is owning the bus (cpu, blitter, ...) */

int LastOpcodeFamily = -1;      /* see the enum in readcpu.h i_XXX */
int LastInstrCycles = -1;       /* number of cycles for previous instr. (not rounded to 4) */
int Pairing = 0;                /* set to 1 if the latest 2 intr paired */
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


/*-----------------------------------------------------------------------*/
/**
 * Init the pairing matrix
 * Two instructions can pair if PairingArray[ LastOpcodeFamily ][ OpcodeFamily ] == 1
 */
void M68000_InitPairing(void)
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

	PairingArray[ i_MULU ][ i_MOVEA] = 1; 
	PairingArray[ i_MULS ][ i_MOVEA] = 1; 
	PairingArray[ i_MULU ][ i_MOVE ] = 1; 
	PairingArray[ i_MULS ][ i_MOVE ] = 1; 

	PairingArray[ i_MULU ][ i_DIVU ] = 1;
	PairingArray[ i_MULU ][ i_DIVS ] = 1;
	PairingArray[ i_MULS ][ i_DIVU ] = 1;
	PairingArray[ i_MULS ][ i_DIVS ] = 1;

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
}


/*-----------------------------------------------------------------------*/
/**
 * Reset CPU 68000 variables
 */
void M68000_Reset(bool bCold)
{
	int i;

	/* Clear registers */
	if (bCold)
	{
		for (i=0; i<(16+1); i++)
			Regs[i] = 0;
	}

	/* Now directly reset the UAE CPU core: */
	m68k_reset();

	/* Init the pairing matrix */
	M68000_InitPairing();

	BusMode = BUS_MODE_CPU;
}


/*-----------------------------------------------------------------------*/
/**
 * Reset and start 680x0 emulation
 */
void M68000_Start(void)
{
	m68k_reset();

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
 * Check wether the CPU mode has been changed.
 */
void M68000_CheckCpuLevel(void)
{
	changed_prefs.cpu_level = ConfigureParams.System.nCpuLevel;
	changed_prefs.cpu_compatible = ConfigureParams.System.bCompatibleCpu;
#ifndef UAE_NEWCPU_H
	changed_prefs.cpu_cycle_exact = 0;  // TODO
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
	Uint32 savepc;

	/* Save/Restore details */
	MemorySnapShot_Store(Regs,sizeof(Regs));
	MemorySnapShot_Store(&STRamEnd,sizeof(STRamEnd));

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
#ifdef UAE_NEWCPU_H
		regs.prefetch_pc = regs.pc + 128;
#endif
	}

#ifdef UAE_NEWCPU_H
	MemorySnapShot_Store(&regs.prefetch, sizeof(regs.prefetch));  /* prefetch */
#else
	uae_u32 prefetch_dummy;
	MemorySnapShot_Store(&prefetch_dummy, sizeof(prefetch_dummy));
#endif

	if (bSave)
	{
#ifdef UAE_NEWCPU_H
		MakeSR();
#else
		MakeSR(&regs);
#endif
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
#ifdef UAE_NEWCPU_H
		MakeFromSR();
		/* set stack pointer */
		if (regs.s)
			m68k_areg(regs, 7) = regs.isp;
		else
			m68k_areg(regs, 7) = regs.usp;
#else
		MakeFromSR(&regs);
		/* set stack pointer */
		if (regs.s)
			m68k_areg(&regs, 7) = regs.isp;
		else
			m68k_areg(&regs, 7) = regs.usp;
#endif
	}

	if (bSave)
		save_fpu();
	else
		restore_fpu();
}


/*-----------------------------------------------------------------------*/
/**
 * BUSERROR - Access outside valid memory range.
 * Use bReadWrite = 0 for write errors and bReadWrite = 1 for read errors!
 */
void M68000_BusError(Uint32 addr, bool bReadWrite)
{
	/* FIXME: In prefetch mode, m68k_getpc() seems already to point to the next instruction */
	// BusErrorPC = M68000_GetPC();		/* [NP] We set BusErrorPC in m68k_run_1 */

	/* Do not print message when TOS is testing for available HW or
	 * when a program just checks for the floating point co-processor. */
	if ((BusErrorPC < TosAddress || BusErrorPC > TosAddress + TosSize)
	    && addr != 0xfffa42)
	{
		/* Print bus error message */
		fprintf(stderr, "M68000 Bus Error at address $%x.\n", addr);
	}

	if ((regs.spcflags & SPCFLAG_BUSERROR) == 0)	/* [NP] Check that the opcode has not already generated a read bus error */
	{
		BusErrorAddress = addr;				/* Store for exception frame */
		bBusErrorReadWrite = bReadWrite;
		M68000_SetSpecial(SPCFLAG_BUSERROR);		/* The exception will be done in newcpu.c */
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Exception handler
 */
void M68000_Exception(Uint32 ExceptionVector , int ExceptionSource)
{
	int exceptionNr = ExceptionVector/4;

	if ( ( ExceptionSource == M68000_EXCEPTION_SRC_INT_VIDEO )
		&& (exceptionNr>24 && exceptionNr<32) )	/* 68k autovector interrupt? */
	{
		/* Handle autovector interrupts the UAE's way
		 * (see intlev() and do_specialties() in UAE CPU core) */
		/* In our case, this part is only called for HBL and VBL interrupts */
		int intnr = exceptionNr - 24;
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
#ifdef UAE_NEWCPU_H
		Exception(exceptionNr, m68k_getpc(), ExceptionSource);
#else
		Exception(exceptionNr, &regs, m68k_getpc(&regs));
#endif

		SR = M68000_GetSR();

		/* Set Status Register so interrupt can ONLY be stopped by another interrupt
		 * of higher priority! */
		if (ExceptionSource == M68000_EXCEPTION_SRC_INT_MFP)
		{
			Uint32 MFPBaseVector = (unsigned int)(MFP_VR&0xf0)<<2;
			if ( (ExceptionVector>=MFPBaseVector) && (ExceptionVector<=(MFPBaseVector+0x3c)) )
				SR = (SR&SR_CLEAR_IPL)|0x0600; /* MFP, level 6 */
		}
		else if (ExceptionSource == M68000_EXCEPTION_SRC_INT_DSP)
		{
			SR = (SR&SR_CLEAR_IPL)|0x0600;     /* DSP, level 6 */
		}

		M68000_SetSR(SR);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * There seem to be wait states when a program accesses certain hardware
 * registers on the ST. Use this function to simulate these wait states.
 * [NP] with some instructions like CLR, we have a read then a write at the
 * same location, so we may have 2 wait states (read and write) to add
 * (nWaitStateCycles should be reset to 0 after the cycles were added).
 */
void M68000_WaitState(int nCycles)
{
	M68000_SetSpecial(SPCFLAG_EXTRA_CYCLES);

	nWaitStateCycles += nCycles;	/* add all the wait states for this instruction */
}
