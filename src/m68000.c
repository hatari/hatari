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
/* 2008/10/05	[NP]	Pass the 'ExceptionSource' parameter to Exception() in cpu/newcpu.c		*/
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


const char M68000_fileid[] = "Hatari m68000.c";

#include <inttypes.h>

#include "main.h"
#include "configuration.h"
#include "gemdos.h"
#include "hatari-glue.h"
#include "cycInt.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "mmu_common.h"
#include "options.h"
#include "savestate.h"
#include "stMemory.h"
#include "tos.h"
#include "falcon/crossbar.h"
#include "cart.h"
#include "cpu/cpummu.h"
#include "cpu/cpummu030.h"
#include "scc.h"

#if ENABLE_DSP_EMU
#include "dsp.h"
#endif

/* information about current CPU instruction */
cpu_instruction_t CpuInstruction;

uint32_t BusErrorAddress;	/* Stores the offending address for bus-/address errors */
bool bBusErrorReadWrite;	/* 0 for write error, 1 for read error */
int nCpuFreqShift;		/* Used to emulate higher CPU frequencies: 0=8MHz, 1=16MHz, 2=32Mhz */
int WaitStateCycles = 0;	/* Used to emulate the wait state cycles of certain IO registers */
int BusMode = BUS_MODE_CPU;	/* Used to tell which part is owning the bus (cpu, blitter, ...) */
bool CPU_IACK = false;		/* Set to true during an exception when getting the interrupt's vector number */
bool CpuRunCycleExact;		/* true if the cpu core is running in cycle exact mode (ie m68k_run_1_ce, m68k_run_2ce, ...) */
bool CpuRunFuncNoret;		/* true if the cpu core is using cpufunctbl_noret instead of cpufunctbl to execute opcode */

static bool M68000_DebuggerFlag;/* Is debugger enabled or not ? */

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
	//fprintf( stderr,"M68000_Reset in cold=%d" , bCold );

	UAE_Set_Quit_Reset ( bCold );
	set_special(SPCFLAG_MODE_CHANGE);		/* exit m68k_run_xxx() loop and check for cpu changes / reset / quit */

	BusMode = BUS_MODE_CPU;
	CPU_IACK = false;

	//fprintf( stderr,"M68000_Reset out cold=%d\n" , bCold );
}


/*-----------------------------------------------------------------------*/
/**
 * Enable/disable breakpoints in the debugger
 */
void M68000_SetDebugger(bool debug)
{
	M68000_DebuggerFlag = debug;

	if ( debug )
		M68000_SetSpecial(SPCFLAG_DEBUGGER);
	else
		M68000_UnsetSpecial(SPCFLAG_DEBUGGER);
}


/*-----------------------------------------------------------------------*/
/**
 * Restore debugger state (breakpoints)
 * This is called from CPU core after a reset, because CPU core clears regs.spcflags
 */
void M68000_RestoreDebugger(void)
{
	if ( M68000_DebuggerFlag )
		M68000_SetSpecial(SPCFLAG_DEBUGGER);
	else
		M68000_UnsetSpecial(SPCFLAG_DEBUGGER);
}



/*-----------------------------------------------------------------------*/
/**
 * Start 680x0 emulation
 */
void M68000_Start(void)
{
//fprintf (stderr, "M68000_Start\n" );

	/* Load initial memory snapshot */
	if (bLoadMemorySave)
	{
		MemorySnapShot_Restore(ConfigureParams.Memory.szMemoryCaptureFileName, false);
	}
	else if (bLoadAutoSave)
	{
		MemorySnapShot_Restore(ConfigureParams.Memory.szAutoSaveFileName, false);
	}

	UAE_Set_Quit_Reset ( false );
	m68k_go(true);
}


/*-----------------------------------------------------------------------*/
/**
 * Check whether CPU settings have been changed.
 * Possible values for WinUAE :
 *	cpu_model : 68000 , 68010, 68020, 68030, 68040, 68060
 *	cpu_compatible : 0/false (no prefetch for 68000/20/30)  1/true (prefetch opcode for 68000/20/30)
 *	cpu_cycle_exact : 0/false   1/true (most accurate, implies cpu_compatible)
 *	cpu_memory_cycle_exact : 0/false   1/true (less accurate than cpu_cycle_exact)
 *	cpu_data_cache : 0/false (don't emulate caches)   1/true (emulate instr/data caches for 68020/30/40/60)
 *	address_space_24 : 1 (68000/10 and 68030 LC for Falcon), 0 (68020/30/40/60)
 *	fpu_model : 0, 68881 (external), 68882 (external), 68040 (cpu) , 68060 (cpu)
 *	fpu_strict : true/false (more accurate rounding)
 *	fpu_mode :  0  faster but less accurate, use host's cpu/fpu with 64 bit precision)
 *		    1  most accurate but slower, use softfloat library)
 *		   -1  similar to 0 but with extended 80 bit precision, only for x86 CPU)
 *		       (TODO [NP] not in Hatari for now, require fpp_native_msvc_80bit.cpp / fpux64_80.asm / fpux86_80.asm)
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
//fprintf ( stderr,"M68000_CheckCpuSettings in\n" );
	/* WinUAE core uses cpu_model instead of cpu_level, so we've got to
	 * convert these values here: */
	switch (ConfigureParams.System.nCpuLevel) {
		case 0 : changed_prefs.cpu_model = 68000; break;
		case 1 : changed_prefs.cpu_model = 68010; break;
		case 2 : changed_prefs.cpu_model = 68020; break;
		case 3 : changed_prefs.cpu_model = 68030; break;
		case 4 : changed_prefs.cpu_model = 68040; break;
		case 5 : changed_prefs.cpu_model = 68060; break;
		default: fprintf (stderr, "M68000_CheckCpuSettings() : Error, cpu_level %d unknown\n" , ConfigureParams.System.nCpuLevel);
	}

	/* Only 68040/60 can have 'internal' FPU */
	if ( ( ConfigureParams.System.n_FPUType == FPU_CPU ) && ( changed_prefs.cpu_model < 68040 ) )
	{
		Log_Printf(LOG_WARN, "Internal FPU is supported only for 040/060, disabling FPU\n");
		ConfigureParams.System.n_FPUType = FPU_NONE;
	}
	/* 68000/10 can't have an FPU */
	if ( ( ConfigureParams.System.n_FPUType != FPU_NONE ) && ( changed_prefs.cpu_model < 68020 ) )
	{
		Log_Printf(LOG_WARN, "FPU is not supported in 68000/010 configurations, disabling FPU\n");
		ConfigureParams.System.n_FPUType = FPU_NONE;
	}

	changed_prefs.int_no_unimplemented = true;
	changed_prefs.fpu_no_unimplemented = true;
	changed_prefs.cpu_compatible = ConfigureParams.System.bCompatibleCpu;
	changed_prefs.cpu_cycle_exact = ConfigureParams.System.bCycleExactCpu;
	changed_prefs.cpu_memory_cycle_exact = ConfigureParams.System.bCycleExactCpu;
	changed_prefs.address_space_24 = ConfigureParams.System.bAddressSpace24;
	changed_prefs.fpu_model = ConfigureParams.System.n_FPUType;
	changed_prefs.fpu_strict = ConfigureParams.System.bCompatibleFPU;
	changed_prefs.fpu_mode = ( ConfigureParams.System.bSoftFloatFPU ? 1 : 0 );

	/* Update the MMU model by taking the same value as CPU model */
	/* MMU is only supported for CPU >=68030, this is later checked in custom.c fixup_cpu() */
	if ( !ConfigureParams.System.bMMU )
		changed_prefs.mmu_model = 0;				/* MMU disabled */
	else
		changed_prefs.mmu_model = changed_prefs.cpu_model;	/* MMU enabled */

	/* Set cpu speed to default values (only used in WinUAE, not in Hatari) */
	changed_prefs.m68k_speed = 0;
	changed_prefs.cpu_clock_multiplier = 2 << 8;

	/* We don't use JIT */
	changed_prefs.cachesize = 0;

	/* Always emulate instr/data caches for cpu >= 68020 */
	/* Cache emulation requires cpu_compatible or cpu_cycle_exact mode */
	if ( ( changed_prefs.cpu_model < 68020 ) ||
	     ( ( changed_prefs.cpu_compatible == false ) && ( changed_prefs.cpu_cycle_exact == false ) ) )
		changed_prefs.cpu_data_cache = false;
	else
		changed_prefs.cpu_data_cache = true;

	/* Update SPCFLAG_MODE_CHANGE flag if needed */
	check_prefs_changed_cpu();

//fprintf ( stderr, "M68000_CheckCpuSettings out\n" );
}


/**
 * Patch the cpu tables to intercept some opcodes used for Gemdos HD
 * emulation, extended VDI more or for NatFeats.
 */
void M68000_PatchCpuTables(void)
{
	if (Cart_UseBuiltinCartridge())
	{
		/* Hatari's specific illegal opcodes */
		cpufunctbl[GEMDOS_OPCODE] = OpCode_GemDos;				/* 0x0008 */
		cpufunctbl_noret[GEMDOS_OPCODE] = OpCode_GemDos_noret;			/* 0x0008 */
		cpufunctbl[PEXEC_OPCODE] = OpCode_Pexec;				/* 0x0009 */
		cpufunctbl_noret[PEXEC_OPCODE] = OpCode_Pexec_noret;			/* 0x0009 */
		cpufunctbl[SYSINIT_OPCODE] = OpCode_SysInit;				/* 0x000a */
		cpufunctbl_noret[SYSINIT_OPCODE] = OpCode_SysInit_noret;		/* 0x000a */
		cpufunctbl[VDI_OPCODE] = OpCode_VDI;					/* 0x000c */
		cpufunctbl_noret[VDI_OPCODE] = OpCode_VDI_noret;			/* 0x000c */
	}
	else
	{
		/* No built-in cartridge loaded : set same handler as 0x4afc (illegal) */
		cpufunctbl[GEMDOS_OPCODE] = cpufunctbl[0x4afc];   			/* 0x0008 */
		cpufunctbl_noret[GEMDOS_OPCODE] = cpufunctbl_noret[0x4afc];   		/* 0x0008 */
		cpufunctbl[PEXEC_OPCODE] = cpufunctbl[0x4afc];   		 	/* 0x0009*/
		cpufunctbl_noret[PEXEC_OPCODE] = cpufunctbl_noret[0x4afc];    		/* 0x0009*/
		cpufunctbl[SYSINIT_OPCODE] = cpufunctbl[0x4afc]; 		 	/* 0x000a */
		cpufunctbl_noret[SYSINIT_OPCODE] = cpufunctbl_noret[0x4afc];  		/* 0x000a */
		cpufunctbl[VDI_OPCODE] = cpufunctbl[0x4afc];      			/* 0x000c */
		cpufunctbl_noret[VDI_OPCODE] = cpufunctbl_noret[0x4afc];      		/* 0x000c */
	}

	/* Install opcodes for Native Features? */
	if (ConfigureParams.Log.bNatFeats)
	{
		/* illegal opcodes for emulators Native Features */
		cpufunctbl[NATFEAT_ID_OPCODE] = OpCode_NatFeat_ID;			/* 0x7300 */
		cpufunctbl_noret[NATFEAT_ID_OPCODE] = OpCode_NatFeat_ID_noret;		/* 0x7300 */
		cpufunctbl[NATFEAT_CALL_OPCODE] = OpCode_NatFeat_Call;			/* 0x7301 */
		cpufunctbl_noret[NATFEAT_CALL_OPCODE] = OpCode_NatFeat_Call_noret;	/* 0x7301 */
	}
	else
	{
		/* No Native Features : set same handler as 0x4afc (illegal) */
		cpufunctbl[NATFEAT_ID_OPCODE] = cpufunctbl[ 0x4afc ];			/* 0x7300 */
		cpufunctbl_noret[NATFEAT_ID_OPCODE] = cpufunctbl_noret[ 0x4afc ];	/* 0x7300 */
		cpufunctbl[NATFEAT_CALL_OPCODE] = cpufunctbl[ 0x4afc ];			/* 0x7301 */
		cpufunctbl_noret[NATFEAT_CALL_OPCODE] = cpufunctbl_noret[ 0x4afc ];	/* 0x7301 */
	}
}


/**
 * Save/Restore snapshot of CPU variables ('MemorySnapShot_Store' handles type)
 */
void M68000_MemorySnapShot_Capture(bool bSave)
{
	size_t len;
	uae_u8 chunk[ 1000 ];

	MemorySnapShot_Store(&pendingInterrupts, sizeof(pendingInterrupts));	/* for intlev() */

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
		//m68k_dumpstate_file(stderr, NULL);
	}

	MemorySnapShot_Store(&WaitStateCycles,sizeof(WaitStateCycles));
	MemorySnapShot_Store(&BusMode,sizeof(BusMode));
	MemorySnapShot_Store(&CPU_IACK,sizeof(CPU_IACK));
	MemorySnapShot_Store(&LastInstrCycles,sizeof(LastInstrCycles));
	MemorySnapShot_Store(&Pairing,sizeof(Pairing));

	/* From cpu/custom.c and cpu/events.c */
	MemorySnapShot_Store(&currcycle,sizeof(currcycle));
	MemorySnapShot_Store(&extra_cycle,sizeof(extra_cycle));

	/* From cpu/newcpu.c */
	MemorySnapShot_Store(&BusCyclePenalty,sizeof(BusCyclePenalty));
}

/*-----------------------------------------------------------------------*/
/**
 * Check whether bus error reporting should be reported or not.
 * We do not want to print messages when TOS is testing for available HW
 * or when a program just checks for the floating point co-processor.
 */
bool M68000_IsVerboseBusError(uint32_t pc, uint32_t addr)
{
	const uint32_t nTosProbeAddrs[] =
	{
		0xf00039, 0xff8900, 0xff8a00, 0xff8c83,
		0xff8e0d, 0xff8e09, 0xfffa40
	};
	const uint32_t nEmuTosProbeAddrs[] =
	{
		0xf0001d, 0xf0005d, 0xf0009d, 0xf000dd, 0xff8006, 0xff8282,
		0xff8400, 0xff8701, 0xff8901, 0xff8943, 0xff8961, 0xff8c80,
		0xff8a3c, 0xff9201, 0xfffa81, 0xfffe00
	};
	int idx;

	if (ConfigureParams.Log.nTextLogLevel == LOG_DEBUG)
		return true;

	if (ConfigureParams.System.bAddressSpace24
	    || (addr & 0xff000000) == 0xff000000)
	{
		addr &= 0xffffff;
	}

	/* Program just probing for FPU? A lot of C startup code is always
	 * doing this, so reporting bus errors here would be annoying */
	if (addr == 0xfffa42)
		return false;

	/* Always report other bus errors from normal programs */
	if (pc < TosAddress || pc > TosAddress + TosSize)
		return true;

	for (idx = 0; idx < ARRAY_SIZE(nTosProbeAddrs); idx++)
	{
		if (nTosProbeAddrs[idx] == addr)
			return false;
	}

	if (bIsEmuTOS)
	{
		for (idx = 0; idx < ARRAY_SIZE(nEmuTosProbeAddrs); idx++)
		{
			if (nEmuTosProbeAddrs[idx] == addr)
				return false;
		}
	}

	return true;
}

/**
 * BUSERROR - Access outside valid memory range.
 *   ReadWrite : BUS_ERROR_READ in case of a read or BUS_ERROR_WRITE in case of write
 *   Size : BUS_ERROR_SIZE_BYTE or BUS_ERROR_SIZE_WORD or BUS_ERROR_SIZE_LONG
 *   AccessType : BUS_ERROR_ACCESS_INSTR or BUS_ERROR_ACCESS_DATA
 *   val : value we wanted to write in case of a BUS_ERROR_WRITE
 */
void M68000_BusError ( uint32_t addr , int ReadWrite , int Size , int AccessType , uae_u32 val )
{
	LOG_TRACE(TRACE_CPU_EXCEPTION, "Bus error %s at address $%x PC=$%x.\n",
	          ReadWrite ? "reading" : "writing", addr, M68000_InstrPC);

#define WINUAE_HANDLE_BUS_ERROR
#ifdef WINUAE_HANDLE_BUS_ERROR

	bool	read , ins;
	int	size;

	if ( ReadWrite == BUS_ERROR_READ )		read = true; else read = false;
	if ( AccessType == BUS_ERROR_ACCESS_INSTR )	ins = true; else ins = false;
	if ( Size == BUS_ERROR_SIZE_BYTE )		size = sz_byte;
	else if ( Size == BUS_ERROR_SIZE_WORD )		size = sz_word;
	else						size = sz_long;
	hardware_exception2 ( addr , val , read , ins , size );
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
void M68000_Exception(uint32_t ExceptionNr , int ExceptionSource)
{
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
}




/*-----------------------------------------------------------------------*/
/**
 * Update the list of pending interrupts.
 * Level 2 (HBL) and 4 (VBL) are only cleared when the interrupt is processed,
 * but level 6 is shared between MFP and DSP and can be cleared by MFP or DSP
 * before being processed.
 * So, we need to check which IRQ are set/cleared at the same time
 * and update level 6 accordingly : level 6 = MFP_IRQ OR DSP_IRQ
 * Level 5 (SCC) is only used on Mega STE, TT and Falcon
 *
 * [NP] NOTE : temporary case for interrupts with WinUAE CPU in cycle exact mode
 * In CE mode, interrupt state should be updated on each subcycle of every opcode
 * then ipl_fetch() is called in each opcode.
 * For now, Hatari with WinUAE CPU in CE mode only evaluates the interrupt state
 * after the end of each opcode. So we need to call ipl_fetch() ourselves at the moment.
 */
void	M68000_Update_intlev ( void )
{	
	uint8_t	Level6_IRQ;

#if ENABLE_DSP_EMU
	Level6_IRQ = MFP_GetIRQ_CPU() | DSP_GetHREQ();
#else
	Level6_IRQ = MFP_GetIRQ_CPU();
#endif
	if ( Level6_IRQ == 1 )
		pendingInterrupts |= (1 << 6);
	else
		pendingInterrupts &= ~(1 << 6);

	if ( SCC_Get_Line_IRQ() == SCC_IRQ_ON )
		pendingInterrupts |= (1 << 5);
	else
		pendingInterrupts &= ~(1 << 5);

	if ( pendingInterrupts )
		doint();
	else
		M68000_UnsetSpecial ( SPCFLAG_INT | SPCFLAG_DOINT );

	/* Temporary case for WinUAE CPU hanlding IPL in CE mode */
	/* doint() will update regs.ipl_pin, so copy it into regs.ipl[0] */
	/* TODO : see ipl_fetch_next / update_ipl, we should not reset currcycle */
	/* (when counting Hatari's internal cycles) to have correct value */
	/* in regs.ipl_pin_change_evt. In the meantime we always copy regs.ipl_pin */
	/* to regs.ipl_pin_p, else ipl_fetch_next can return an incorrect ipl */
	if ( CpuRunCycleExact )
	{
		regs.ipl[0] = regs.ipl_pin;			/* See ipl_fetch() in cpu/cpu_prefetch.h */
		regs.ipl_pin_p = regs.ipl_pin;			/* See ipl_fetch_next() */
	}
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
 *
 * NOTE : this function should only be called in the context of emulating an opcode,
 * it should not be called in the context of an internal timer called by CycInt_Process()
 * because cycles would not be correctly added to CyclesGlobalClockCounter
 */
void M68000_WaitState(int WaitCycles)
{
	if ( CpuRunCycleExact )
		currcycle += ( WaitCycles * CYCLE_UNIT / 2 );	/* Add wait states immediately to the CE cycles counter */
	else
	{
		WaitStateCycles += WaitCycles;			/* Cumulate all the wait states for this instruction */
	}
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
	CyclesToNextE = 10 - Cycles_GetClockCounterImmediate() % 10;
	if ( CyclesToNextE == 10 )		/* we're already synchronised with E Clock */
		CyclesToNextE = 0;

//fprintf ( stderr , "wait eclock delay=%d clock=%"PRIu64"\n" , CyclesToNextE, Cycles_GetClockCounterImmediate() );
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
	uint64_t	Cycles;
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
//fprintf ( stderr , "M68000_Flush_All_Caches\n" );
	flush_cpu_caches(true);
	invalidate_cpu_data_caches();
}


void	M68000_Flush_Instr_Cache ( uaecptr addr , int size )
{
//fprintf ( stderr , "M68000_Flush_Instr_Cache\n" );
	/* Instruction cache for cpu >= 68020 */
	flush_cpu_caches(true);
}


void	M68000_Flush_Data_Cache ( uaecptr addr , int size )
{
//fprintf ( stderr , "M68000_Flush_Data_Cache\n" );
	/* Data cache for cpu >= 68030 */
	invalidate_cpu_data_caches();
}



/*-----------------------------------------------------------------------*/
/**
 * When running in 68000 CE mode, allow to change the "do_cycles" functions
 * in the cpu emulation depending on the blitter state.
 *  - if the blitter is not busy, we keep the 'normal' 68000 CE "do_cycles" functions
 *  - if the blitter is busy, we use a slightly slower "do_cycles" to accurately
 *    count bus accesses made by the blitter and the CPU
 *
 * This limits the overhead of emulating cycle exact blitter bus accesses when blitter is OFF.
 */
void	M68000_SetBlitter_CE ( bool state )
{
//fprintf ( stderr , "M68000_SetBlitter_CE state=%s\n" , state ? "on" : "off" );
	if ( state )
	{
		set_x_funcs_hatari_blitter ( 1 );		/* on */
	}
	else
	{
		set_x_funcs_hatari_blitter ( 0 );		/* off */
	}
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

	else if (ConfigureParams.Memory.STRamSize_KB > 8*1024)	/* ST/STE with more than 8 MB */
		return 0xff;					/* Allow 'fake' 24 bits for DMA */

	else if (ConfigureParams.Memory.STRamSize_KB > 4*1024)	/* ST/STE with more than 4 MB */
		return 0x7f;					/* Allow 'fake' 23 bits for DMA */

	else							/* ST/STE with <= 4 MB */
		return 0x3f;					/* Limit DMA range to 22 bits (same as real HW) */
}




/*-----------------------------------------------------------------------*/
/**
 * This function should be called when the cpu freq is changed, to update
 * other components that depend on it.
 *
 * For now, only Falcon mode requires some updates for the crossbar
 */
void	M68000_ChangeCpuFreq ( void )
{
	if ( Config_IsMachineFalcon() )
	{
		Crossbar_Recalculate_Clocks_Cycles();
	}
}




/*-----------------------------------------------------------------------*/
/**
 * Some CPU registers can't be read or modified directly, some additional
 * actions are required.
 */
uint16_t	M68000_GetSR ( void )
{
	MakeSR();
	return regs.sr;
}

void	M68000_SetSR ( uint16_t v )
{
	regs.sr = v;
	MakeFromSR();
}

void	M68000_SetPC ( uaecptr v )
{
	m68k_setpc ( v );
	fill_prefetch();
}

/**
 * Dump the contents of the MMU registers
 */
void M68000_MMU_Info(FILE *fp, uint32_t flags)
{
	if (!ConfigureParams.System.bMMU || ConfigureParams.System.nCpuLevel < 2)
	{
		fprintf(fp, "MMU is not enabled.\n");
		return;
	}
	else if (ConfigureParams.System.nCpuLevel <= 3) /* 68020/68030 mode? */
	{
		fprintf(fp, "MMUSR:\t0x%04x\n", mmusr_030);
		fprintf(fp, "SRP:\t0x%016" PRIx64 "\n", (uint64_t)srp_030);
		fprintf(fp, "CRP:\t0x%016" PRIx64 "\n", (uint64_t)crp_030);
		fprintf(fp, "TC:\t0x%08x\n", tc_030);
		fprintf(fp, "TT0:\t0x%08x\n", tt0_030);
		fprintf(fp, "TT1:\t0x%08x\n", tt1_030);
	}
	else	/* 68040 / 68060 mode */
	{
		fprintf(fp, "MMUSR:\t0x%04x\n", regs.mmusr);
		fprintf(fp, "SRP:\t0x%08x\n", regs.srp);
		fprintf(fp, "URP:\t0x%08x\n", regs.urp);
		fprintf(fp, "TC:\t0x%08x\n", regs.tcr);
		fprintf(fp, "DTT0:\t0x%08x\n", regs.dtt0);
		fprintf(fp, "DTT1:\t0x%08x\n", regs.dtt1);
		fprintf(fp, "ITT0:\t0x%08x\n", regs.itt0);
		fprintf(fp, "ITT0:\t0x%08x\n", regs.itt1);
		/* TODO: Also call mmu_dump_tables() here? */
	}
}
