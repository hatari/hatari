/*
  Hatari - m68000.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

/* 2007/11/10	[NP]	Add pairing for lsr / dbcc (and all variants	*/
/*			working on register, not on memory).		*/
/* 2008/01/07	[NP]	Use PairingArray to store all valid pairing	*/
/*			combinations (in m68000.c)			*/
/* 2010/04/05	[NP]	Rework the pairing code to take BusCyclePenalty	*/
/*			into account when using d8(an,ix).		*/
/* 2010/05/07	[NP]	Add BusCyclePenalty to LastInstrCycles to detect*/
/*			a possible pairing between add.l (a5,d1.w),d0	*/
/*			and move.b 7(a5,d1.w),d5.			*/


#ifndef HATARI_M68000_H
#define HATARI_M68000_H

#include "cycles.h"     /* for nCyclesMainCounter */
#include "sysdeps.h"
#include "memory.h"
#define	HATARI_NO_ENUM_BITVALS	/* Don't define 'bitvals' and 'bits' in newcpu.h / readcpu.h */
#include "newcpu.h"     /* for regs */
#include "cycInt.h"
#include "log.h"

/* 68000 register defines */
enum {
  REG_D0,    /* D0.. */
  REG_D1,
  REG_D2,
  REG_D3,
  REG_D4,
  REG_D5,
  REG_D6,
  REG_D7,    /* ..D7 */
  REG_A0,    /* A0.. */
  REG_A1,
  REG_A2,
  REG_A3,
  REG_A4,
  REG_A5,
  REG_A6,
  REG_A7    /* ..A7 (also SP) */
};

/* 68000 Condition code's */
#define SR_AUX              0x0010
#define SR_NEG              0x0008
#define SR_ZERO             0x0004
#define SR_OVERFLOW         0x0002
#define SR_CARRY            0x0001

#define SR_CCODE_MASK       (SR_AUX|SR_NEG|SR_ZERO|SR_OVERFLOW|SR_CARRY)
#define SR_MASK             0xFFE0

#define SR_TRACEMODE        0x8000
#define SR_SUPERMODE        0x2000
#define SR_IPL              0x0700

#define SR_CLEAR_IPL        0xf8ff
#define SR_CLEAR_TRACEMODE  0x7fff
#define SR_CLEAR_SUPERMODE  0xdfff

/* Exception numbers most commonly used in ST */
#define  EXCEPTION_NR_BUSERROR		2
#define  EXCEPTION_NR_ADDRERROR		3
#define  EXCEPTION_NR_ILLEGALINS	4
#define  EXCEPTION_NR_DIVZERO    	5
#define  EXCEPTION_NR_CHK        	6
#define  EXCEPTION_NR_TRAPV      	7
#define  EXCEPTION_NR_TRACE      	9
#define  EXCEPTION_NR_LINE_A		10
#define  EXCEPTION_NR_LINE_F		11
#define  EXCEPTION_NR_HBLANK		26		/* Level 2 interrupt */
#define  EXCEPTION_NR_VBLANK		28		/* Level 4 interrupt */
#define  EXCEPTION_NR_MFP_DSP		30		/* Level 6 interrupt */
#define  EXCEPTION_NR_TRAP0		32
#define  EXCEPTION_NR_TRAP1		33
#define  EXCEPTION_NR_TRAP2		34
#define  EXCEPTION_NR_TRAP13		45
#define  EXCEPTION_NR_TRAP14		46


/* Size of 68000 instructions */
#define MAX_68000_INSTRUCTION_SIZE  10  /* Longest 68000 instruction is 10 bytes(6+4) */
#define MIN_68000_INSTRUCTION_SIZE  2   /* Smallest 68000 instruction is 2 bytes(ie NOP) */

/* Illegal Opcode used to help emulation. eg. free entries are 8 to 15 inc' */
#define  GEMDOS_OPCODE        8  /* Free op-code to intercept GemDOS trap */
#define  PEXEC_OPCODE         9  /* Free op-code to intercept Pexec calls */
#define  SYSINIT_OPCODE      10  /* Free op-code to initialize system (connected drives etc.) */
#define  VDI_OPCODE          12  /* Free op-code to call VDI handlers AFTER Trap#2 */

/* Illegal opcodes used for Native Features emulation.
 * See debug/natfeats.c and tests/natfeats/ for more info.
 */
#define  NATFEAT_ID_OPCODE   0x7300
#define  NATFEAT_CALL_OPCODE 0x7301


/* Ugly hacks to adapt the main code to the different CPU cores: */

#define Regs regs.regs


# define M68000_GetPC()     m68k_getpc()

# define M68000_InstrPC		regs.instruction_pc
# define M68000_CurrentOpcode	regs.opcode

# define M68000_SetSpecial(flags)   set_special(flags)
# define M68000_UnsetSpecial(flags) unset_special(flags)


/* Some define's for bus error (see newcpu.c) */
/* Bus error read/write mode */
#define BUS_ERROR_WRITE		0
#define BUS_ERROR_READ		1
/* Bus error access size */
#define BUS_ERROR_SIZE_BYTE	1
#define BUS_ERROR_SIZE_WORD	2
#define BUS_ERROR_SIZE_LONG	4
/* Bus error access type */
#define BUS_ERROR_ACCESS_INSTR	0
#define BUS_ERROR_ACCESS_DATA	1


/* Bus access mode */
#define	BUS_MODE_CPU		0			/* bus is owned by the cpu */
#define	BUS_MODE_BLITTER	1			/* bus is owned by the blitter */
#define BUS_MODE_DEBUGGER	2			/* bus is owned by debugger : special case to access RAM 0-0x7FF */
							/* without doing a bus error when not in supervisor mode */
							/* eg : this is used in some debug functions that do get_long/word/byte */

/* [NP] Notes on IACK :
 * When an interrupt happens, it's possible a similar interrupt happens again
 * between the start of the exception and the IACK sequence. In that case, we
 * might have to set pending bit twice and change the interrupt vector.
 *
 * From the 68000's doc, IACK starts after 10 cycles (12 cycles on STF due to 2 cycle
 * bus penalty) and is supposed to take 4 cycles if the interrupt takes a total of 44 cycles.
 *
 * On Atari STF, interrupts take 56 cycles instead of 44, which means it takes
 * 12 extra cycles to fetch the vector number and to handle non-aligned memory accesses.
 * From WinUAE's CE mode, we have 2 non-aligned memory accesses to wait for (ie 2+2 cycles),
 * which leaves a total of 12 cycles to fetch the vector.
 *
 * As seen with a custom program on STF that measures HBL's jitter, we get the same results with Hatari
 * in CE mode if we use 10 cycles to fetch the vector (step 3), which will also add 2 cycle penalty (step 4b)
 * This means we have at max 12+10=22 cycles after the start of the exception where some
 * changes can happen (maybe it's a little less, depending on when the interrupt
 * vector is written on the bus).
 *
 * Additionally, auto vectored interrupts (HBL and VBL) require to be in sync with E-clock,
 * which can add 0 to 8 cycles (step 3a). In that case we have between 22+0 and 22+8 cycles
 * to get another interrupt before vector is written on the bus.
 *
 * The values we use were not entirely measured on real ST hardware, they were guessed/adjusted
 * to get the correct behaviour in some games/demos relying on this.
 * These values are when running in CE mode (2 cycle precision) ; when CPU runs in prefetch
 * mode, values need to be rounded to 4).
 *
 * Interrupt steps + WinUAE cycles (measured on real A500 HW) + ST specific values :
 *
 * 1	6	idle cycles
 * 1b	2(*)	ST bus access penalty (if necessary)
 * 2	4	write PC low word
 * 3a	0-8(*)	wait for E-clock for auto vectored interrupt
 * 3	10(*)	read exception number
 * 4	4	idle cycles
 * 4b	2(*)	ST bus access penalty
 * 5	4	write SR
 * 6	4	write PC high word
 * 7	4	read exception address high word
 * 8	4	read exception address low word
 * 9	4	prefetch
 * 10	2	idle cycles
 * 10b	2(*)	ST bus access penalty
 * 11	4	prefetch
 *  TOTAL = 56
 *
 *   (*) ST specific timings
 */

/* Values for IACK sequence when running in cycle exact mode */
#define CPU_IACK_CYCLES_MFP_CE		12		/* vector sent by the MFP (TODO value not measured on real STF) */
#define CPU_IACK_CYCLES_VIDEO_CE	10		/* auto vectored for HBL/VBL (value measured on real STF) */

/* Values for IACK sequence when running in normal/prefetch mode */
#define CPU_IACK_CYCLES_START		12		/* number of cycles before starting the IACK when not using CE mode */
							/* (this should be a multiple of 4, else it will be rounded by M68000_AddCycles) */
#define CPU_IACK_CYCLES_MFP		12		/* vector sent by the MFP */
#define CPU_IACK_CYCLES_VIDEO		12		/* auto vectored for HBL/VBL */

/* Information about current CPU instruction */
typedef struct {
	/* These are provided only by WinUAE CPU core */
	int	I_Cache_miss;				/* Instruction cache for 68020/30/40/60 */
	int	I_Cache_hit;
	int	D_Cache_miss;				/* Data cache for 68030/40/60 */
	int	D_Cache_hit;

	/* TODO: move other instruction specific Hatari variables here */
} cpu_instruction_t;

extern cpu_instruction_t CpuInstruction;

extern uint32_t BusErrorAddress;
extern bool bBusErrorReadWrite;
extern int nCpuFreqShift;
extern int WaitStateCycles;
extern int BusMode;
extern bool	CPU_IACK;
extern bool	CpuRunCycleExact;
extern bool	CpuRunFuncNoret;

extern int	LastOpcodeFamily;
extern int	LastInstrCycles;
extern int	Pairing;
extern char	PairingArray[ MAX_OPCODE_FAMILY ][ MAX_OPCODE_FAMILY ];
extern const char *OpcodeName[];


/*-----------------------------------------------------------------------*/
/**
 * Add CPU cycles.
 * NOTE: All times are rounded up to nearest 4 cycles.
 */
static inline void M68000_AddCycles(int cycles)
{
	cycles = (cycles + 3) & ~3;
	nCyclesMainCounter += cycles;
	CyclesGlobalClockCounter += cycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Add CPU cycles, take cycles pairing into account. Pairing will make
 * some specific instructions take 4 cycles less when run one after the other.
 * Pairing happens when the 2 instructions are "aligned" on different bus accesses.
 * Candidates are :
 *  - 2 instructions taking 4n+2 cycles
 *  - 1 instruction taking 4n+2 cycles, followed by 1 instruction using d8(an,ix)
 *
 * Not all the candidate instructions can pair, only the opcodes listed in PairingArray.
 * On ST, when using d8(an,ix), we get an extra 2 cycle penalty for misaligned bus access.
 * The only instruction that can generate BusCyclePenalty=4 is move d8(an,ix),d8(an,ix)
 * and although it takes 4n cycles (24 for .b/.w or 32 for .l) it can pair with
 * a previous 4n+2 instruction (but it will still have 1 misaligned bus access in the end).
 *
 * Verified pairing on an STF :
 *  - lsl.w #4,d1 + move.w 0(a4,d2.w),d1		motorola=14+14=28  stf=28
 *  - lsl.w #4,d1 + move.w 0(a4,d2.w),(a4)		motorola=14+18=32  stf=32
 *  - lsl.w #4,d1 + move.w 0(a4,d2.w),0(a4,d2.w)	motorola=14+24=38  stf=40
 *  - add.l (a5,d1.w),d0 + move.b 7(a5,d1.w),d5)	motorola=20+14=34  stf=36
 *
 * d8(an,ix) timings without pairing (2 cycles penalty) :
 *  - add.l   0(a4,d2.w),a1				motorola=20  stf=24
 *  - move.w  0(a4,d2.w),d1				motorola=14  stf=16
 *  - move.w  0(a4,d2.w),(a4)				motorola=18  stf=20
 *  - move.w  0(a4,d2.w),0(a4,d2.w)			motorola=24  stf=28
 *
 * NOTE: All times are rounded up to nearest 4 cycles.
 */
static inline void M68000_AddCyclesWithPairing(int cycles)
{
	Pairing = 0;
	/* Check if number of cycles for current instr and for */
	/* the previous one is of the form 4+2n */
	/* If so, a pairing could be possible depending on the opcode */
	/* A pairing is also possible if current instr is 4n but with BusCyclePenalty > 0 */
	if ( ( PairingArray[ LastOpcodeFamily ][ OpcodeFamily ] == 1 )
	    && ( ( LastInstrCycles & 3 ) == 2 )
	    && ( ( ( cycles & 3 ) == 2 ) || ( BusCyclePenalty > 0 ) ) )
	{
		Pairing = 1;
		LOG_TRACE(TRACE_CPU_PAIRING,
		          "cpu pairing detected pc=%x family %s/%s cycles %d/%d\n",
		          m68k_getpc(), OpcodeName[LastOpcodeFamily],
		          OpcodeName[OpcodeFamily], LastInstrCycles, cycles);
	}

	/* [NP] This part is only needed to track possible pairing instructions, */
	/* we can keep it disabled most of the time */
#if 0
	if ( (LastOpcodeFamily!=OpcodeFamily) && ( Pairing == 0 )
		&& ( ( cycles & 3 ) == 2 ) && ( ( LastInstrCycles & 3 ) == 2 ) )
	{
		LOG_TRACE(TRACE_CPU_PAIRING,
		          "cpu could pair pc=%x family %s/%s cycles %d/%d\n",
		          m68k_getpc(), OpcodeName[LastOpcodeFamily],
		          OpcodeName[OpcodeFamily], LastInstrCycles, cycles);
	}
#endif

	/* Store current instr (not rounded) to check next time */
	LastInstrCycles = cycles + BusCyclePenalty;
	LastOpcodeFamily = OpcodeFamily;

	/* If pairing is true, we need to subtract 2 cycles for the	*/
	/* previous instr which was rounded to 4 cycles while it wasn't */
	/* needed (and we don't round the current one)			*/
	/* -> both instr will take 4 cycles less on the ST than if ran	*/
	/* separately.							*/
	if (Pairing == 1)
	{
		if ( ( cycles & 3 ) == 2 )		/* pairing between 4n+2 and 4n+2 instructions */
			cycles -= 2;			/* if we have a pairing, we should not count the misaligned bus access */

		else					/* this is the case of move d8(an,ix),d8(an,ix) where BusCyclePenalty=4 */
			/*do nothing */;		/* we gain 2 cycles for the pairing with 1st d8(an,ix) */
							/* and we have 1 remaining misaligned access for the 2nd d8(an,ix). So in the end, we keep */
							/* cycles unmodified as 4n cycles (eg lsl.w #4,d1 + move.w 0(a4,d2.w),0(a4,d2.w) takes 40 cycles) */
	}
	else
	{
		cycles += BusCyclePenalty;		/* >0 if d8(an,ix) was used */
		cycles = (cycles + 3) & ~3;		/* no pairing, round current instr to 4 cycles */
	}

	nCyclesMainCounter += cycles;
	CyclesGlobalClockCounter += cycles;
	BusCyclePenalty = 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Add CPU cycles when using WinUAE CPU 'cycle exact' mode.
 * In this mode, we should not round cycles to the next nearest 4 cycles
 * because all memory accesses will already be aligned to 4 cycles when
 * using CE mode.
 * The CE mode will also give the correct 'instruction pairing' for all
 * opcodes/addressing mode, without requiring tables/heuristics (in the
 * same way that it's done in real hardware)
 */
static inline void M68000_AddCycles_CE(int cycles)
{
	nCyclesMainCounter += cycles;
	CyclesGlobalClockCounter += cycles;
}



extern void M68000_Init(void);
extern void M68000_Reset(bool bCold);
extern void M68000_SetDebugger(bool debug);
extern void M68000_RestoreDebugger(void);
extern void M68000_Start(void);
extern void M68000_CheckCpuSettings(void);
extern void M68000_PatchCpuTables(void);
extern void M68000_MemorySnapShot_Capture(bool bSave);
extern bool M68000_IsVerboseBusError(uint32_t pc, uint32_t addr);
extern void M68000_BusError ( uint32_t addr , int ReadWrite , int Size , int AccessType , uae_u32 val );
extern void M68000_Exception(uint32_t ExceptionNr , int ExceptionSource);
extern void M68000_Update_intlev ( void );
extern void M68000_WaitState(int WaitCycles);
extern int M68000_WaitEClock ( void );
extern void M68000_SyncCpuBus_OnReadAccess ( void );
extern void M68000_SyncCpuBus_OnWriteAccess ( void );
extern void M68000_Flush_Instr_Cache ( uaecptr addr , int size );
extern void M68000_Flush_Data_Cache ( uaecptr addr , int size );
extern void M68000_Flush_All_Caches ( uaecptr addr , int size );
extern void M68000_SetBlitter_CE ( bool ce_mode );
extern int DMA_MaskAddressHigh ( void );
extern void M68000_ChangeCpuFreq ( void );
extern uint16_t M68000_GetSR ( void );
extern void M68000_SetSR ( uint16_t v );
extern void M68000_SetPC ( uaecptr v );
extern void M68000_MMU_Info(FILE *fp, uint32_t flags);

#endif
