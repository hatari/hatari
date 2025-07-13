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
#include "statusbar.h"
#include "tos.h"
#include "falcon/crossbar.h"
#include "cart.h"
#include "cpu/cpummu.h"
#include "cpu/cpummu030.h"
#include "scc.h"
#include "scu_vme.h"
#include "blitter.h"
#include "ioMem.h"

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



#define	MEGA_STE_CACHE_SIZE		8192		/* Size in 16-bits words */

struct {
	uint8_t		Valid[ MEGA_STE_CACHE_SIZE ];
	uint16_t	Tag[ MEGA_STE_CACHE_SIZE ];
	uint16_t	Value[ MEGA_STE_CACHE_SIZE ];
} MegaSTE_Cache;


static bool	MegaSTE_Cache_Is_Enabled ( void );
static bool	MegaSTE_Cache_Addr_Cacheable ( uint32_t addr , int Size , int DoWrite );
static void	MegaSTE_Cache_Addr_Convert ( uint32_t Addr , uint16_t *pLineNbr , uint16_t *pTag );
static bool	MegaSTE_Cache_Update ( uint32_t Addr , int Size , uint16_t Val , int DoWrite );
static bool	MegaSTE_Cache_Write ( uint32_t Addr , int Size , uint16_t Val );
static bool	MegaSTE_Cache_Read ( uint32_t Addr , int Size , uint16_t *pVal );


uae_u32 (*x_get_iword_megaste_save)(int);
uae_u32 (*x_get_long_megaste_save)(uaecptr);
uae_u32 (*x_get_word_megaste_save)(uaecptr);
uae_u32 (*x_get_byte_megaste_save)(uaecptr);
void (*x_put_long_megaste_save)(uaecptr,uae_u32);
void (*x_put_word_megaste_save)(uaecptr,uae_u32);
void (*x_put_byte_megaste_save)(uaecptr,uae_u32);


uae_u32	mem_access_delay_word_read_megaste_16 (uaecptr addr);
uae_u32	mem_access_delay_wordi_read_megaste_16 (uaecptr addr);
uae_u32	mem_access_delay_byte_read_megaste_16 (uaecptr addr);
void	mem_access_delay_byte_write_megaste_16 (uaecptr addr, uae_u32 v);
void	mem_access_delay_word_write_megaste_16 (uaecptr addr, uae_u32 v);
uae_u32	wait_cpu_cycle_read_megaste_16 (uaecptr addr, int mode);
void	wait_cpu_cycle_write_megaste_16 (uaecptr addr, int mode, uae_u32 v);


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
 *	cpu_data_cache : 0/false (don't emulate data cache)   1/true (emulate data cache for 30/40/60)
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

	/* 68000/010 can't have any FPU */
	if (changed_prefs.cpu_model < 68020 && ConfigureParams.System.n_FPUType != FPU_NONE)
	{
		Log_Printf(LOG_WARN, "FPU is not supported in 68000/010 configurations, disabling FPU\n");
		ConfigureParams.System.n_FPUType = FPU_NONE;
	}
	/* 68020/030 can't have 'internal' FPU */
	else if (changed_prefs.cpu_model < 68040 && ConfigureParams.System.n_FPUType == FPU_CPU)
	{
		Log_Printf(LOG_WARN, "Internal FPU is supported only for 040/060, "
		                     "using 68882 FPU instead\n");
		ConfigureParams.System.n_FPUType = FPU_68882;
	}
	/* 68040/060 can't have an external FPU */
	else if (changed_prefs.cpu_model >= 68040 &&
	         (ConfigureParams.System.n_FPUType == FPU_68881 || ConfigureParams.System.n_FPUType == FPU_68882))
	{
		Log_Printf(LOG_WARN, "68881/68882 FPU is only supported for 020/030 CPUs, "
		                     "using internal FPU instead\n");
		ConfigureParams.System.n_FPUType = FPU_CPU;
	}

	changed_prefs.int_no_unimplemented = true;
	changed_prefs.fpu_no_unimplemented = true;
	changed_prefs.cpu_data_cache = ConfigureParams.System.bCpuDataCache;
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

	/* while 020 had i-cache, only 030+ had also d-cache */
	if (changed_prefs.cpu_model < 68030 ||
	    !ConfigureParams.System.bCpuDataCache ||
	    !(changed_prefs.cpu_compatible || changed_prefs.cpu_cycle_exact))
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
	int i;

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

	/* Save/Restore MegaSTE's cache */
	for ( i=0 ; i < MEGA_STE_CACHE_SIZE ; i++ )
	{
		MemorySnapShot_Store(&MegaSTE_Cache.Valid[ i ] , sizeof(MegaSTE_Cache.Valid[ i ]) );
		MemorySnapShot_Store(&MegaSTE_Cache.Tag[ i ] , sizeof(MegaSTE_Cache.Tag[ i ]) );
		MemorySnapShot_Store(&MegaSTE_Cache.Value[ i ] , sizeof(MegaSTE_Cache.Value[ i ]) );
	}
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

	/* For the MegaSTE, a bus error will flush the external cache */
	if ( ConfigureParams.System.nMachineType == MACHINE_MEGA_STE )
		MegaSTE_Cache_Flush ();


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
 * Set/clear interrupt request for IntNr (between 1 and 7)
 *  - For STF/STE/Falcon : we update the corresponding bits in "pendingInterrupts", which is
 *    directly "connected" to the CPU core
 *  - For MegaSTE/TT : interrupts are first handled by the SCU chip which include a dedicated mask
 *    for every interrupt source. The masked result is then copied to "pendingInterrupts" and
 *    only masked interrupts will be visible to the CPU core
 */

void	M68000_SetIRQ ( int IntNr )
{
	if ( !SCU_IsEnabled() )
		pendingInterrupts |= ( 1 << IntNr );
	else
		SCU_SetIRQ_CPU ( IntNr );			/* MegaSTE / TT */
}


void	M68000_ClearIRQ ( int IntNr )
{
	if ( !SCU_IsEnabled() )
		pendingInterrupts &= ~( 1 << IntNr );
	else
		SCU_ClearIRQ_CPU ( IntNr );			/* MegaSTE / TT */
}


/*-----------------------------------------------------------------------*/
/**
 * Exception handler
 * If ExceptionNr matches level 1-7 interrupts then we call M68000_SetIRQ
 * Else we call Exception() in the CPU core in newcpu.c
 */

void	M68000_Exception(uint32_t ExceptionNr , int ExceptionSource)
{
	if ( ExceptionNr > 24 && ExceptionNr < 32 )		/* Level 1-7 interrupts */
	{
		/* In our case, this part is called for HBL, VBL and MFP/DSP interrupts */
		/* For WinUAE CPU, we must call M68000_Update_intlev after changing pendingInterrupts */
		/* (in order to call doint() and to update regs.ipl with regs.ipl_pin, else */
		/* the exception might be delayed by one instruction in do_specialties()) */
		M68000_SetIRQ ( ExceptionNr - 24 );
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
		M68000_SetIRQ ( 6 );
	else
		M68000_ClearIRQ ( 6 );

	if ( SCC_Get_Line_IRQ() == SCC_IRQ_ON )
		M68000_SetIRQ ( 5 );
	else
		M68000_ClearIRQ ( 5 );

	if ( pendingInterrupts )
		doint();
	else
		M68000_UnsetSpecial ( SPCFLAG_INT | SPCFLAG_DOINT );

	/* Temporary case for WinUAE CPU handling IPL in CE mode */
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

	/* For the MegaSTE, we also flush the external cache */
	if ( ConfigureParams.System.nMachineType == MACHINE_MEGA_STE )
		MegaSTE_Cache_Flush ();
}


void	M68000_Flush_Instr_Cache ( uaecptr addr , int size )
{
//fprintf ( stderr , "M68000_Flush_Instr_Cache\n" );
	/* Instruction cache for cpu >= 68020 */
	flush_cpu_caches(true);

	/* For the MegaSTE, we also flush the external cache */
	if ( ConfigureParams.System.nMachineType == MACHINE_MEGA_STE )
		MegaSTE_Cache_Flush ();
}


void	M68000_Flush_Data_Cache ( uaecptr addr , int size )
{
//fprintf ( stderr , "M68000_Flush_Data_Cache\n" );
	/* Data cache for cpu >= 68030 */
	invalidate_cpu_data_caches();

	/* For the MegaSTE, we also flush the external cache */
	if ( ConfigureParams.System.nMachineType == MACHINE_MEGA_STE )
		MegaSTE_Cache_Flush ();
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





/*------------------------------------------------------------------------------*/
/*										*/
/*			MegaSTE 16MHz and cache					*/
/*										*/
/* Based on MegaSTE schematics as well as documentation by Christian Zietz	*/
/*										*/
/* The MegaSTE can change its CPU clock from 8 MHz to 16 MHz, but as the RAM	*/
/* still have a "slow" speed that was designed for a 8 MHz CPU, the CPU will	*/
/* have a lot of wait states to get a slot to access RAM (accesses are shared	*/
/* between the CPU and the shifter						*/
/* As such, when the CPU runs at 16 MHz the overall performance of the MegaSTE	*/
/* is roughly the same as when the CPU runs at 8 MHz.				*/
/*										*/
/* To improve RAM accesses, an external 16 KB cache was added to the MegaSTE,	*/
/* with	faster RAM that don't add wait states.					*/
/*										*/
/* As seen on the MegaSTE schematics, the cache is made of the following	*/
/* components :									*/
/*  - 2 8KB x 8 bits 35 ns TAG RAM chips, ref MK48S74N-25 (named u004 and u005)	*/
/*  - 2 8KB x 8 bits 85 ns RAM chips, ref HM6265L (u008 and u009). One RAM chip	*/
/*    will store lower bytes, the other the upper byte for the same cache entry	*/
/*  - some PAL for additional logic on the various signals (u011, u012, ...)	*/
/*										*/
/* The cache is made of 8192 lines, each line is 1 word (2 bytes)		*/
/* When a physical address is accessed, the following bits are used to select	*/
/* an entry in the TAG RAM :							*/
/*  - bits 15-24 : tag (10 bits)						*/
/*  - bits 1-14 : line (0 to 8191)						*/
/*  - bit 0 : ignored (because the cache stores 16 bit words)			*/
/*										*/
/* - Each time a word is accessed (read or write), the corresponding tag value	*/
/*   is stored in the line entry and the 2 bytes are stored in each 8KB RAM chip*/
/* - When bytes are read, the cache will also be updated as a word / 2 bytes,	*/
/*   this is because in	the case of 8 bit accesses in RAM/ROM the bus will	*/
/*   carry the 16 bits at this RAM/ROM address (not just the 8 bits of the byte)*/
/* - When bytes are written, the cache will be updated only if it already stores*/
/*   an entry with the same tag. In that case, upper or lower byte will be	*/
/*   updated in the previously cached word.					*/
/*										*/
/* - RAM (up to 4 MB) and ROM regions can be cached				*/
/* - IO or cartridge regions can't be cached					*/
/*										*/
/*------------------------------------------------------------------------------*/


/* Uncomment the next line to check all entries after every cache update */
//#define MEGA_STE_CACHE_DEBUG_CHECK_ENTRIES




/* Update the CPU freq and cache status, depending on content of $ff8e21
 *	$ff8e21 Mega STe Cache/Processor Control
 *		BIT 0 : Cache (0 - disabled, 1 - enabled)
 *		BIT 1 : CPU Speed (0 - 8MHz, 1 - 16MHz)
 */

void	MegaSTE_CPU_Cache_Update ( uint8_t val )
{
//fprintf ( stderr , "MegaSTE_CPU_Cache_Update 0x%x\n" , val );

	/* If cache is disabled, flush all entries */
	if ( ( val & 1 ) == 0 )
		MegaSTE_Cache_Flush ();

	/* 68000 Frequency changed ? We change freq only in 68000 mode for a
	 * normal MegaSTE, if the user did not request a faster one manually */
	if (ConfigureParams.System.nCpuLevel == 0 && ConfigureParams.System.nCpuFreq <= 16)
	{
		if ((val & 0x2) != 0) {
			LOG_TRACE ( TRACE_MEM, "cpu : megaste set to 16 MHz pc=%x\n" , M68000_GetPC() );
			/* 16 Mhz bus for 68000 */
			Configuration_ChangeCpuFreq ( 16 );
			MegaSTE_CPU_Set_16Mhz ( true );
		}
		else {
			/* 8 Mhz bus for 68000 */
			LOG_TRACE ( TRACE_MEM, "cpu : megaste set to 8 MHz pc=%x\n" , M68000_GetPC() );
			Configuration_ChangeCpuFreq ( 8 );
			MegaSTE_CPU_Set_16Mhz ( false );
		}
	}

	Statusbar_UpdateInfo();			/* Update clock speed in the status bar */
}


void	MegaSTE_CPU_Cache_Reset ( void )
{
//fprintf ( stderr , "MegaSTE_CPU_Cache_Reset\n" );
	IoMem_WriteByte ( 0xff8e21 , 0 );	/* 8 MHz, no cache */
	MegaSTE_CPU_Cache_Update ( 0 );
}


void	MegaSTE_CPU_Set_16Mhz ( bool set_16 )
{
	if ( !currprefs.cpu_cycle_exact || ( currprefs.cpu_model != 68000 ) )
		return;

//fprintf ( stderr , "MegaSTE_CPU_Set_16Mhz %d\n" , set_16);

	/* Enable 16 MHz mode for 68000 CE */
	if ( set_16 && ( x_get_iword != get_wordi_ce000_megaste_16 ) )
	{
		/* save current functions */
		x_get_iword_megaste_save = x_get_iword;
		x_put_long_megaste_save = x_put_long;
		x_put_word_megaste_save = x_put_word;
		x_put_byte_megaste_save = x_put_byte;
		x_get_long_megaste_save = x_get_long;
		x_get_word_megaste_save = x_get_word;
		x_get_byte_megaste_save = x_get_byte;

		/* set mega ste specific functions */
		x_get_iword = get_wordi_ce000_megaste_16;
		x_put_long = put_long_ce000_megaste_16;
		x_put_word = put_word_ce000_megaste_16;
		x_put_byte = put_byte_ce000_megaste_16;
		x_get_long = get_long_ce000_megaste_16;
		x_get_word = get_word_ce000_megaste_16;
		x_get_byte = get_byte_ce000_megaste_16;
	}


	/* Disable 16 MHz mode, restore functions if needed */
	if ( !set_16 && ( x_get_iword == get_wordi_ce000_megaste_16 ) )
	{
		/* save current functions */
		x_get_iword = x_get_iword_megaste_save;
		x_put_long = x_put_long_megaste_save;
		x_put_word = x_put_word_megaste_save;
		x_put_byte = x_put_byte_megaste_save;
		x_get_long = x_get_long_megaste_save;
		x_get_word = x_get_word_megaste_save;
		x_get_byte = x_get_byte_megaste_save;
	}
}


/*
 * Return true if the cache is enabled, else return false
 */

static bool	MegaSTE_Cache_Is_Enabled ( void )
{
	if ( IoMem_ReadByte(0xff8e21) & 0x1 )
		return true;
	return false;
}



/*
 * Check cache consistency, useful to debug error in the cache
 * For each valid cache entry, we compare the stored value with
 * the content of the RAM for the same physical address.
 * If there's a difference then something went wrong in the cache
 */

#ifdef MEGA_STE_CACHE_DEBUG_CHECK_ENTRIES
static void	MegaSTE_Cache_Check_Entries ( const char *txt );

static void	MegaSTE_Cache_Check_Entries ( const char *txt )
{
	uint16_t	Line;
	uint16_t	Tag;
	uint32_t	Addr;

	for ( Line=0 ; Line < MEGA_STE_CACHE_SIZE ; Line++ )
		if ( MegaSTE_Cache.Valid[ Line ] )
		{
			Tag = MegaSTE_Cache.Tag[ Line ];
			Addr = ( Line << 1 ) | ( Tag << 14 );
			if ( MegaSTE_Cache.Value[ Line ] != get_word(Addr) )
				fprintf ( stderr , "mega ste cache bad %s : Line=0x%x Tag=0x%x Addr=%x Val=0x%x != 0x%x pc=%x\n" ,
					txt , Line , Tag , Addr , MegaSTE_Cache.Value[ Line ] , get_word(Addr) , M68000_GetPC() );
		}
}

#else
/* Debugging OFF : use an empty inline function */
static inline void	MegaSTE_Cache_Check_Entries ( const char *txt );

static inline void	MegaSTE_Cache_Check_Entries ( const char *txt )
{
}
#endif



/*
 * Return true if addr is part of a cacheable region, else false
 *   - RAM (up to 4MB) and ROM regions can be cached
 *   - IO or cartridge regions can't be cached
 * On a 68000 MegaSTE, only the lowest 24 bits of the address should be used
 * (except if the user forces a 32 bit setting)
 *
 * Accesses that would cause a bus error or an address error should not be cached
 */

static bool	MegaSTE_Cache_Addr_Cacheable ( uint32_t addr , int Size , int DoWrite )
{
	/* The MegaSTE uses a 68000 with only 24 bits of address, upper 8 bits */
	/* should be ignored (except if user explicitely forces 32 bits addressing) */
	if ( ConfigureParams.System.bAddressSpace24 )
		addr &= 0xFFFFFF;

	/* Word access on odd address will cause an address error */
	if ( ( Size == 2 ) && ( addr & 1 ) )
		return false;				/* no cache */

	/* Writing to bytes 0-3 in RAM will cause a bus error */
	if ( ( addr < 0x4 ) && DoWrite )
		return false;				/* no cache */

	/* Accessing RAM 0-0x7FF in user mode will cause a bus error */
	if ( ( addr < 0x800 ) && !is_super_access ( DoWrite ? false : true ) )
		return false;				/* no cache */

	/* Available RAM can be cached (up to 4MB) */
	if ( ( addr < STRamEnd ) && ( addr < 0x400000 ) )
		return true;

	/* TOS in ROM region can be cached only when reading (writing would cause a bus error) */
	if ( ( addr >= 0xE00000 ) && ( addr < 0xF00000 ) && !DoWrite )
		return true;

	/* Other regions can't be cached */
	return false;
}



/*
 * Flush the cache by setting Valid[] to 0
 *
 * Cache will be flushed  / invalidated on the following conditions :
 *   - clearing bit 0 at $ff8e21 to disable the cache
 *   - reset
 *   - use of BGACK (if DMA or Blitter are enabled)
 *   - bus error
 *
 * For performance reason we do a global 'memset' instead of a 'for loop'
 * on each individual entry
 */

void	MegaSTE_Cache_Flush ( void )
{
//fprintf ( stderr , "MegaSTE_Cache_Flush\n" );
	memset ( MegaSTE_Cache.Valid , 0 , MEGA_STE_CACHE_SIZE );
}



/*
 * Convert a cacheable address into a Line number in the cache and a Tag value
 * Addr lowest 24 bits are split into :
 *   - bits 14-23 : tag (10 bits)
 *   - bits 1-13 : line (0 to 8191)
 *   - bit 0 : ignored (because the cache stores 16 bit words)
 */

static void	MegaSTE_Cache_Addr_Convert ( uint32_t Addr , uint16_t *pLineNbr , uint16_t *pTag )
{
	*pLineNbr = ( Addr >> 1 ) & 0x1fff;
	*pTag = ( Addr >> 14 ) & 0x3ff;
}



/*
 * Update the cache for a word or byte access
 *  - if Size==2 (read/write word access) the corresponding cache entry is replaced
 *  - If Size==1 (write byte access) the corresponding cache entry is updated
 *    only if it was already associated to the same Tag value. In that case
 *    we update the lower or upper byte of the cached value depending
 *    on Addr being even or odd.
 *  - If Size==1 (read byte access) the corresponding cache entry is replaced by the
 *    corresponding word at the same address (forced to even). This is because when RAM/ROM
 *    is accessed as byte, the bus will in fact carry the whole word (16 bits) at this
 *    address and the cpu will keep only the upper or lower byte (8 bits). The word
 *    on the bus can be used to update the cache.
 *
 * Return true if value was added to the cache, else return false
 */

static bool	MegaSTE_Cache_Update ( uint32_t Addr , int Size , uint16_t Val , int DoWrite )
{
	uint16_t	Line;
	uint16_t	Tag;

	if ( !MegaSTE_Cache_Addr_Cacheable ( Addr , Size , DoWrite ) )
		return false;					/* data not cacheable */

	MegaSTE_Cache_Addr_Convert ( Addr , &Line , &Tag );

	if ( Size == 2 )					/* word access : update cache */
	{
		MegaSTE_Cache.Valid[ Line ] = 1;
		MegaSTE_Cache.Tag[ Line ] = Tag;
		MegaSTE_Cache.Value[ Line ] = Val;
//fprintf ( stderr , "update w %x %x %x : %x\n" , Addr , Line, Tag, Val );
		MegaSTE_Cache_Check_Entries ( "update w out" );
		return true;					/* cache updated */
	}

	else							/* byte access : update if already cached */
	{
		if ( MegaSTE_Cache.Valid[ Line ] && ( MegaSTE_Cache.Tag[ Line ] == Tag ) )
		{
//fprintf ( stderr , "update b %x %x %d : %x\n" , Addr , Line, Tag, Val );
			Val &= 0xff;
			if ( Addr & 1 )				/* update lower byte of cached value */
				MegaSTE_Cache.Value[ Line ] = ( MegaSTE_Cache.Value[ Line ] & 0xff00 ) | Val;
			else					/* update upper byte of cached value */
				MegaSTE_Cache.Value[ Line ] = ( MegaSTE_Cache.Value[ Line ] & 0xff ) | ( Val << 8 );
			MegaSTE_Cache_Check_Entries ( "update b out" );
			return true;				/* cache updated */
		}
	}

	return false;						/* not stored in cache */
}



static bool	MegaSTE_Cache_Write ( uint32_t Addr , int Size , uint16_t Val )
{
	return MegaSTE_Cache_Update ( Addr , Size , Val , 1 );
}



static bool	MegaSTE_Cache_Read ( uint32_t Addr , int Size , uint16_t *pVal )
{
	uint16_t	Line;
	uint16_t	Tag;

	if ( !MegaSTE_Cache_Addr_Cacheable ( Addr , Size , 0 ) )
		return false;					/* cache miss, data not cacheable */

	MegaSTE_Cache_Addr_Convert ( Addr , &Line , &Tag );
	if ( MegaSTE_Cache.Valid[ Line ] && ( MegaSTE_Cache.Tag[ Line ] == Tag ) )
	{
		*pVal = MegaSTE_Cache.Value[ Line ];		/* get the cached word */
//fprintf ( stderr , "read cache %x %x %d : %d %x\n" , Addr , Line, Tag, Size, *pVal );
		if ( Size == 1 )				/* size is byte, not word */
		{
			if ( Addr & 1 )
				*pVal = *pVal & 0xff;		/* get lower byte of word */
			else
				*pVal = ( *pVal >> 8 ) & 0xff;	/* get upper byte of word */
		}
		MegaSTE_Cache_Check_Entries ( "read out" );
		return true;					/* cache hit */
	}

	return false;						/* data not in cache -> update cache */
}


/*
 * Similar to mem_access_delay_xxx functions for 68000 CE in cpu/newcpu.c
 * but we handle a faster CPU speed of 16 MHz instead of 8 MHz (compared to the RAM speed)
 * as well as an external cache.
 *
 * When the CPU is set to 16 MHz other components are still running as if the CPU was at 8 MHz.
 * - At 8 MHz word access to standard RAM takes a total of 4 cycles from the point of view
 *   of the CPU (if there's no additional wait states)
 * - At 16 MHz word access to standard RAM takes a total of 8 cycles from the point of view
 *   of the CPU (if there's no additional wait states)
 *
 * - At 8 MHz the GSTMCU shares every 4 cycles between the CPU and the shifter
 *   (2 cycles for the CPU and 2 cycles for the shifter). If the CPU requires an access
 *   during the shifter's slot then the CPU will have to wait until its own slot.
 * - At 16 MHz, the CPU runs faster but all other components are still running at the same speed.
 *   This means that RAM accesses still require 4 cycles from the point of view of the GSTMCU,
 *   which means 8 cycles from the point of view of the 16 MHz CPU.
 *   The slots between CPU and shifter will last 4 cycles each (instead of 2) from the point
 *   of view of the CPU. If the CPU tries to access memory outside of its slot then it will
 *   have to wait.
 *
 * As can be seen from above, if the CPU runs at 16 MHz it will have nearly no benefit
 * compared to 8 MHz because most of the time the CPU will be slowed down by memory wait states.
 * This is why an external cache was added to the MegaSTE, with faster RAM that allows to take
 * advantage of the CPU 16 MHz speed.
 *
 */

uae_u32	mem_access_delay_word_read_megaste_16 (uaecptr addr)
{
	uint16_t v;
	if ( BlitterPhase )	Blitter_HOG_CPU_mem_access_before ( 1 );

	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
	case CE_MEMBANK_CHIP32:
		if ( !MegaSTE_Cache_Is_Enabled() )
			v = wait_cpu_cycle_read_megaste_16 (addr, 1);
		else
		{
			if ( MegaSTE_Cache_Read ( addr , 2 , &v ) )
			{
				x_do_cycles_post (4 * cpucycleunit, v);
				CpuInstruction.D_Cache_hit++;
			}
			else
			{
				v = wait_cpu_cycle_read_megaste_16 (addr, 1);
				MegaSTE_Cache_Update ( addr , 2 , v , 0 );
				CpuInstruction.D_Cache_miss++;
			}
		}
		break;
	case CE_MEMBANK_FAST16:
	case CE_MEMBANK_FAST32:
		if ( !MegaSTE_Cache_Is_Enabled() )
		{
			v = get_word (addr);
			x_do_cycles_post (4 * cpucycleunit, v);
		}
		else
		{
			if ( MegaSTE_Cache_Read ( addr , 2 , &v ) )
			{
				x_do_cycles_post (4 * cpucycleunit, v);
				CpuInstruction.D_Cache_hit++;
			}
			else
			{
				v = get_word (addr);
				x_do_cycles_post (4 * cpucycleunit, v);
				MegaSTE_Cache_Update ( addr , 2 , v , 0 );
				CpuInstruction.D_Cache_miss++;
			}
		}
		break;
	default:
		v = get_word (addr);
		break;
	}

	regs.db = v;
	regs.read_buffer = v;
	if ( BlitterPhase )	Blitter_HOG_CPU_mem_access_after ( 1 );
	return v;
}


uae_u32	mem_access_delay_wordi_read_megaste_16 (uaecptr addr)
{
	uint16_t v;
	if ( BlitterPhase )	Blitter_HOG_CPU_mem_access_before ( 1 );

	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
	case CE_MEMBANK_CHIP32:
		if ( !MegaSTE_Cache_Is_Enabled() )
			v = wait_cpu_cycle_read_megaste_16 (addr, 2);
		else
		{
			if ( MegaSTE_Cache_Read ( addr , 2 , &v ) )
			{
				x_do_cycles_post (4 * cpucycleunit, v);
				CpuInstruction.I_Cache_hit++;
			}
			else
			{
				v = wait_cpu_cycle_read_megaste_16 (addr, 2);
				MegaSTE_Cache_Update ( addr , 2 , v , 0 );
				CpuInstruction.I_Cache_miss++;
			}
		}
		break;
	case CE_MEMBANK_FAST16:
	case CE_MEMBANK_FAST32:
		if ( !MegaSTE_Cache_Is_Enabled() )
		{
			v = get_wordi (addr);
			x_do_cycles_post (4 * cpucycleunit, v);
		}
		else
		{
			if ( MegaSTE_Cache_Read ( addr , 2 , &v ) )
			{
				x_do_cycles_post (4 * cpucycleunit, v);
				CpuInstruction.I_Cache_hit++;
			}
			else
			{
				v = get_wordi (addr);
				x_do_cycles_post (4 * cpucycleunit, v);
				MegaSTE_Cache_Update ( addr , 2 , v , 0 );
				CpuInstruction.I_Cache_miss++;
			}
		}
		break;
	default:
		v = get_wordi (addr);
		break;
	}

	regs.db = v;
	regs.read_buffer = v;
	if ( BlitterPhase )	Blitter_HOG_CPU_mem_access_after ( 1 );
	return v;
}


uae_u32	mem_access_delay_byte_read_megaste_16 (uaecptr addr)
{
	uint16_t  v;
	if ( BlitterPhase )	Blitter_HOG_CPU_mem_access_before ( 1 );

	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
	case CE_MEMBANK_CHIP32:
		if ( !MegaSTE_Cache_Is_Enabled() )
			v = wait_cpu_cycle_read_megaste_16 (addr, 0);
		else
		{
			if ( MegaSTE_Cache_Read ( addr , 1 , &v ) )
			{
				x_do_cycles_post (4 * cpucycleunit, v);
				CpuInstruction.D_Cache_hit++;
			}
			else
			{
				v = wait_cpu_cycle_read_megaste_16 (addr, 0);

				/* Reading with get_word() could create a bus error, so we must first */
				/* check if this address can be cached without bus error */
				if ( MegaSTE_Cache_Addr_Cacheable ( addr & ~1 , 2 , 0 ) )
					MegaSTE_Cache_Update ( addr , 2 , get_word(addr & ~1) , 0 );

				CpuInstruction.D_Cache_miss++;
			}
		}
		break;
	case CE_MEMBANK_FAST16:
	case CE_MEMBANK_FAST32:
		if ( !MegaSTE_Cache_Is_Enabled() )
		{
			v = get_byte (addr);
			x_do_cycles_post (4 * cpucycleunit, v);
		}
		else
		{
			if ( MegaSTE_Cache_Read ( addr , 1 , &v ) )
			{
				x_do_cycles_post (4 * cpucycleunit, v);
				CpuInstruction.D_Cache_hit++;
			}
			else
			{
				v = get_byte (addr);
				x_do_cycles_post (4 * cpucycleunit, v);
				MegaSTE_Cache_Update ( addr , 1 , v , 0 );
				CpuInstruction.D_Cache_miss++;
			}
		}
		break;
	default:
		v = get_byte (addr);
		break;
	}

	regs.db = (v << 8) | v;
	regs.read_buffer = v;
	if ( BlitterPhase )	Blitter_HOG_CPU_mem_access_after ( 1 );
	return v;
}


void	mem_access_delay_byte_write_megaste_16 (uaecptr addr, uae_u32 v)
{
	regs.db = (v << 8)  | v;
	regs.write_buffer = v;
	if ( BlitterPhase )	Blitter_HOG_CPU_mem_access_before ( 1 );

	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
	case CE_MEMBANK_CHIP32:
		wait_cpu_cycle_write_megaste_16 (addr, 0, v);
		if ( MegaSTE_Cache_Is_Enabled() )
			MegaSTE_Cache_Write ( addr , 1 , v );

		if ( BlitterPhase )	Blitter_HOG_CPU_mem_access_after ( 1 );
		return;
	case CE_MEMBANK_FAST16:
	case CE_MEMBANK_FAST32:
		put_byte (addr, v);
		x_do_cycles_post (4 * cpucycleunit, v);
		if ( MegaSTE_Cache_Is_Enabled() )
			MegaSTE_Cache_Write ( addr , 1 , v );

		if ( BlitterPhase )	Blitter_HOG_CPU_mem_access_after ( 1 );
		return;
	}
	put_byte (addr, v);
}


void	mem_access_delay_word_write_megaste_16 (uaecptr addr, uae_u32 v)
{
	if ( BlitterPhase )	Blitter_HOG_CPU_mem_access_before ( 1 );

	regs.db = v;
	regs.write_buffer = v;
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
	case CE_MEMBANK_CHIP32:
		wait_cpu_cycle_write_megaste_16 (addr, 1, v);
		if ( MegaSTE_Cache_Is_Enabled() )
			MegaSTE_Cache_Write ( addr , 2 , v );

		if ( BlitterPhase )	Blitter_HOG_CPU_mem_access_after ( 1 );
		return;
	case CE_MEMBANK_FAST16:
	case CE_MEMBANK_FAST32:
		put_word (addr, v);
		x_do_cycles_post (4 * cpucycleunit, v);
		if ( MegaSTE_Cache_Is_Enabled() )
			MegaSTE_Cache_Write ( addr , 2 , v );

		if ( BlitterPhase )	Blitter_HOG_CPU_mem_access_after ( 1 );
		return;
	}
	put_word (addr, v);
}




/*
 * Similar to wait_cpu_cycle_read / _write functions for 68000 CE in cpu/custom.c
 * but we handle a faster CPU speed of 16 MHz instead of 8 MHz (compared to the RAM speed)
 */

uae_u32	wait_cpu_cycle_read_megaste_16 (uaecptr addr, int mode)
{
	uae_u32 v = 0;
	int ipl = regs.ipl[0];
	evt_t now = get_cycles();
	uint64_t cycle_slot;

	cycle_slot = ( CyclesGlobalClockCounter + currcycle*2/CYCLE_UNIT ) & 7;
//	fprintf ( stderr , "mem read ce %x %d %lu %lu\n" , addr , mode ,currcycle / cpucycleunit , currcycle );
	if ( cycle_slot != 0 )
	{
//		fprintf ( stderr , "mem wait read %x %d %lu %lu\n" , addr , mode , currcycle / cpucycleunit , currcycle );
		x_do_cycles ( ( 8 - cycle_slot ) * cpucycleunit);
//		fprintf ( stderr , "mem wait read after %x %d %lu %lu\n" , addr , mode , currcycle / cpucycleunit , currcycle );
	}

	switch(mode)
	{
		case -1:
		v = get_long(addr);
		break;
		case -2:
		v = get_longi(addr);
		break;
		case 1:
		v = get_word(addr);
		break;
		case 2:
		v = get_wordi(addr);
		break;
		case 0:
		v = get_byte(addr);
		break;
	}

	x_do_cycles_post (4*CYCLE_UNIT, v);

	// if IPL fetch was pending and CPU had wait states
	// Use ipl_pin value from previous cycle
	if (now == regs.ipl_evt && regs.ipl_pin_change_evt > now + cpuipldelay2) {
		regs.ipl[0] = ipl;
	}
	return v;
}


void	wait_cpu_cycle_write_megaste_16 (uaecptr addr, int mode, uae_u32 v)
{
	int ipl = regs.ipl[0];
	evt_t now = get_cycles();
	uint64_t cycle_slot;

	cycle_slot = ( CyclesGlobalClockCounter + currcycle*2/CYCLE_UNIT ) & 7;
//	fprintf ( stderr , "mem write ce %x %d %lu %lu\n" , addr , mode ,currcycle / cpucycleunit , currcycle );
	if ( cycle_slot != 0 )
	{
//		fprintf ( stderr , "mem wait write %x %d %lu %lu\n" , addr , mode , currcycle / cpucycleunit , currcycle );
		x_do_cycles ( ( 8 - cycle_slot ) * cpucycleunit);
//		fprintf ( stderr , "mem wait write after %x %d %lu %lu\n" , addr , mode , currcycle / cpucycleunit , currcycle );
	}

	if (mode > -2) {
		if (mode < 0) {
			put_long(addr, v);
		} else if (mode > 0) {
			put_word(addr, v);
		} else if (mode == 0) {
			put_byte(addr, v);
		}
	}

	x_do_cycles_post (4*CYCLE_UNIT, v);

	// if IPL fetch was pending and CPU had wait states:
	// Use ipl_pin value from previous cycle
	if (now == regs.ipl_evt) {
		regs.ipl[0] = ipl;
	}
}



/*----------------------------------------------------------------------*/
/*			MegaSTE 16MHz and  cache			*/
/*----------------------------------------------------------------------*/


