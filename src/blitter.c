/*
 * Hatari - blitter.c
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * Blitter emulation. The 'Blitter' chip is found in the Mega-ST, STE/Mega-STE
 * and Falcon. It provides a very fast BitBlit function in hardware.
 *
 * This file has originally been taken from STonX, but it has been completely
 * modified for better maintainability and higher compatibility.
 *
 * NOTES:
 * ----------------------------------------------------------------------------
 * Strange end mask condition ((~(0xffff>>skew)) > end_mask_1)
 *
 * """Similarly the  NFSR (aka  post-flush) bit, when set, will prevent the last
 * source read of the  line. This read  may not  be necessary  with certain
 * combinations of end masks and skews."""
 *     - doesn't mean the blitter will skip source read by itself, just a hint
 *       for developers as far as i understand it.
 * ----------------------------------------------------------------------------
 * Does smudge mode change the line register ?
 * ----------------------------------------------------------------------------
 */

const char Blitter_fileid[] = "Hatari blitter.c : " __DATE__ " " __TIME__;

#include <SDL_types.h>
#include <stdio.h>
#include <stdlib.h>

#include "main.h"
#include "blitter.h"
#include "configuration.h"
#include "dmaSnd.h"
#include "ioMem.h"
#include "m68000.h"
#include "mfp.h"
#include "memorySnapShot.h"
#include "stMemory.h"
#include "screen.h"
#include "video.h"
#include "hatari-glue.h"


/* BLiTTER registers, incs are signed, others unsigned */
#define REG_HT_RAM	0xff8a00				/* - 0xff8a1e */

#define REG_SRC_X_INC	0xff8a20
#define REG_SRC_Y_INC	0xff8a22
#define REG_SRC_ADDR	0xff8a24

#define REG_END_MASK1	0xff8a28
#define REG_END_MASK2	0xff8a2a
#define REG_END_MASK3	0xff8a2c

#define REG_DST_X_INC	0xff8a2e
#define REG_DST_Y_INC	0xff8a30
#define REG_DST_ADDR	0xff8a32

#define REG_X_COUNT 	0xff8a36
#define REG_Y_COUNT 	0xff8a38

#define REG_BLIT_HOP	0xff8a3a				/* halftone blit operation byte */
#define REG_BLIT_LOP	0xff8a3b				/* logical blit operation byte */
#define REG_CONTROL 	0xff8a3c
#define REG_SKEW	0xff8a3d


#define	BLITTER_READ_WORD_BUS_ERR	0x0000	/* This value is returned when the blitter try to read a word */
						/* in a region that would cause a bus error */
						/* [NP] FIXME : for now we return a constant, but it should depend on the bus activity */

/* Blitter registers */
typedef struct
{
	Uint32	src_addr;
	Uint32	dst_addr;
	Uint32	words;
	Uint32	lines;
	short	src_x_incr;
	short	src_y_incr;
	short	dst_x_incr;
	short	dst_y_incr;
	Uint16	end_mask_1;
	Uint16	end_mask_2;
	Uint16	end_mask_3;
	Uint8	hop;
	Uint8	lop;
	Uint8	ctrl;
	Uint8	skew;
} BLITTERREGS;

/* Blitter vars */
typedef struct
{
	int	pass_cycles;
	int	op_cycles;
	Uint32	buffer;
	Uint32	src_words_reset;
	Uint32	dst_words_reset;
	Uint32	src_words;
	Uint8	hog;
	Uint8	smudge;
	Uint8	line;
	Uint8	fxsr;
	Uint8	nfsr;
	Uint8	skew;
} BLITTERVARS;

/* Blitter state */
typedef struct
{
	Uint16	src_word;
	Uint16	dst_word;
	Uint16	end_mask;
	Uint8	have_src;
	Uint8	have_dst;
	Uint8	fxsr;
	Uint8	nfsr;
} BLITTERSTATE;


/* Blitter logical op func */
typedef Uint16 (*BLITTER_OP_FUNC)(void);

static BLITTERREGS	BlitterRegs;
static BLITTERVARS	BlitterVars;
static BLITTERSTATE	BlitterState;
static Uint16		BlitterHalftone[16];

static BLITTER_OP_FUNC	Blitter_ComputeHOP;
static BLITTER_OP_FUNC	Blitter_ComputeLOP;



/* To handle CPU/blitter bus sharing in non-hog mode (require cycle exact mode for CPU emulation) */
#define	BLITTER_PHASE_STOP			0
#define	BLITTER_PHASE_PRE_START			1
#define	BLITTER_PHASE_START			2
#define	BLITTER_PHASE_RUN_TRANSFER		4
#define	BLITTER_PHASE_COUNT_CPU_BUS		8
#define	BLITTER_PHASE_IGNORE_LAST_CPU_CYCLES	16

Uint16		BlitterPhase = BLITTER_PHASE_STOP;		/* Internal state of the blitter */

static Uint16	Blitter_CyclesBeforeStart;			/* Number of cycles after setting busy bit before calling Blitter_Start */
								/* (during this time, the CPU can still run and access the bus) */

static Uint8	Blitter_HOG_CPU_FromBusAccess;			/* 0 or 1 (false/true) */
static Uint8	Blitter_HOG_CPU_BlitterStartDuringBusAccess;	/* 0 or 1 (false/true) */
static Uint16	Blitter_HOG_CPU_BusCountError;			/* 0 or 1, subtracted from NONHOG_BUS_BLITTER to give 64 or 63 */
static Uint16	Blitter_HOG_CPU_IgnoreMaxCpuCycles;		/* Max number of blitter cycles during which the CPU might run in parallel */
								/* (unless the CPU is stalled earlier by a bus access) */


/* Cycles to run for in non-hog mode */
#define NONHOG_BUS_BLITTER	64
#define NONHOG_BUS_CPU		64

static Uint16	Blitter_CountBusBlitter;			/* To count bus accesses made by the blitter */
static Uint16	Blitter_CountBusCpu;				/* To count bus accesses made by the CPU */


#define	BLITTER_CYCLES_PER_BUS_READ		4		/* The blitter takes 4 cycles to read 1 memory word on STE */
#define	BLITTER_CYCLES_PER_BUS_WRITE		4		/* The blitter takes 4 cycles to write 1 memory word on STE */


Uint16		BlitterState_ContinueLater;			/* 0=false / 1=true  TODO move into BLITTERSTATE */

/* Return 'true' if CE mode can be enabled for blitter (ie when using 68000 CE mode) */
#define	BLITTER_RUN_CE		( ( currprefs.cpu_cycle_exact ) && ( currprefs.cpu_level == 0 ) )



/*-----------------------------------------------------------------------*/
/**
 * Count blitter cycles (this assumes blitter and CPU runs at the same freq)
 */

static void Blitter_AddCycles(int cycles)
{
	int all_cycles = cycles + WaitStateCycles;

	BlitterVars.op_cycles += all_cycles;
//fprintf ( stderr , "blitter add_cyc cyc=%d total=%d cur_cyc=%lu\n" , all_cycles , BlitterVars.op_cycles , currcycle/cpucycleunit );
//fprintf ( stderr , "blitter src %x dst %x ycount %d\n" , BlitterRegs.src_addr , BlitterRegs.dst_addr , BlitterRegs.lines );

	nCyclesMainCounter += all_cycles;
	CyclesGlobalClockCounter += all_cycles;
	WaitStateCycles = 0;
}

static void Blitter_FlushCycles(void)
{
	int op_cycles = INT_CONVERT_TO_INTERNAL(BlitterVars.op_cycles, INT_CPU_CYCLE);

//fprintf ( stderr , "blitter flush_cyc cyc=%d pass=%d %d cur_cyc=%lu\n" , BlitterVars.op_cycles , BlitterVars.pass_cycles , nCyclesMainCounter , currcycle/cpucycleunit );

	BlitterVars.pass_cycles += BlitterVars.op_cycles;
	BlitterVars.op_cycles = 0;

#if ENABLE_WINUAE_CPU
	if ( BLITTER_RUN_CE )					/* In CE mode, flush cycles already counted in the current cpu instruction */
	{
		M68000_AddCycles_CE ( currcycle * 2 / CYCLE_UNIT );
 		currcycle = 0;
	}
#endif

	PendingInterruptCount -= op_cycles;
	while (PendingInterruptCount <= 0 && PendingInterruptFunction)
		CALL_VAR(PendingInterruptFunction);
}


/*-----------------------------------------------------------------------*/
/**
 * Handle bus arbitration when switching between CPU and Blitter
 * When a write is made to FF8A3C to start the blitter, it will take a few cycles
 * before doing the bus arbitration. During this time the CPU will be able to
 * partially execute the next instruction in parallel to the blitter
 * (until an access to the BUS is needed by the CPU).
 *
 * NOTE [NP] : this is mostly handled with hardcoded cases for now, as it
 * requires cycle exact emulation to exactly know when bus is accessed
 * by the CPU to prefetch the next word.
 * More tests are needed on a real STE to have a proper model of this.
 *
 * Based on several examples, possible sequence when starting the blitter seems to be :
 *  - t+0 : write to FF8A3C
 *  - t+0 : CPU can still run during 4 cycles and access bus
 *  - t+4 : bus arbitration takes 4 cycles (no access for cpu and blitter during this time)
 *  - t+8 : blitter owns the bus and starts tranferring data
 * (in case of MegaSTE bus arbitration takes 8 cycles instead of 4)
 *
 * When blitter stops owning the bus in favor of the cpu, this seems to always take 4 cycles
 */
static void Blitter_BusArbitration ( int RequestBusMode )
{
	int	cycles;

	if ( RequestBusMode == BUS_MODE_BLITTER )	/* Bus is requested by the blitter */
	{
		if ( ConfigureParams.System.nMachineType == MACHINE_MEGA_STE )
			cycles = 8;			/* MegaSTE blitter needs 4 extra cycles when requesting the bus */
		else
			cycles = 4;			/* Default case : take 4 cycles when going from cpu to blitter */

// 		/* Different timing for some specific cases */
// 
// 		/* 'Relapse - Graphix Sound 2' by Cybernetics (overscan plasma using blitter) */
// 		/* $e764 : move.b  d5,(a4) + dbra d1,$fff2 : 4 cycles of the dbra can be executed while blitter starts */
// 		if ( STMemory_ReadLong ( M68000_InstrPC ) == 0x188551c9 )	/* PC = E764 */
// 			cycles -= 4;			/* 4 cycles less than default case */
// 

// fprintf ( stderr , "blitter bus start pc %x %x cyc=%d cur_cyc=%lu\n" , M68000_GetPC() , M68000_InstrPC , cycles , currcycle/cpucycleunit );
	}

	else						/* Bus is requested by the cpu */
	{
		cycles = 4;				/* Always 4 cycles (even for MegaSTE) */
// fprintf ( stderr , "blitter bus end pc %x %x cyc=%d\n" , M68000_GetPC() , M68000_InstrPC , cycles );
	}

	/* Add arbitration cycles and update BusMode */
	Blitter_AddCycles ( cycles );
	Blitter_FlushCycles();

	BusMode = RequestBusMode;
}



/*-----------------------------------------------------------------------*/
/**
 * Low level memory accesses to read / write a word
 * For each word access we increment the blitter's bus accesses counter.
 */
static Uint16 Blitter_ReadWord(Uint32 addr)
{
	Uint16 value;

	/* When reading from a bus error region, just return a constant */
	if ( STMemory_CheckRegionBusError ( addr ) )
		value = BLITTER_READ_WORD_BUS_ERR;
	else
		value = (Uint16)get_word ( addr );
//fprintf ( stderr , "read %x %x %x\n" , addr , value , STMemory_CheckRegionBusError(addr) );

	Blitter_CountBusBlitter++;
	Blitter_AddCycles ( BLITTER_CYCLES_PER_BUS_READ );
	Blitter_FlushCycles();

	return value;
}

static void Blitter_WriteWord(Uint32 addr, Uint16 value)
{
	/* Call put_word only if the address doesn't point to a bus error region */
	/* (also see SysMem_wput for addr < 0x8) */
	if ( STMemory_CheckRegionBusError ( addr ) == false )
		put_word ( addr , (Uint32)(value) );
//fprintf ( stderr , "write %x %x %x\n" , addr , value , STMemory_CheckRegionBusError(addr) );

	Blitter_CountBusBlitter++;
	Blitter_AddCycles ( BLITTER_CYCLES_PER_BUS_WRITE );
	Blitter_FlushCycles();
}



/*-----------------------------------------------------------------------*/
/**
 * Used to determine how long the blitter can keep the bus in non-hog mode
 * Return true if the blitter can continue its operations and return false
 * if the blitter must suspend its work and give back the bus to the CPU
 */
static bool Blitter_ContinueNonHog ( void )
{
	if ( Blitter_CountBusBlitter < NONHOG_BUS_BLITTER - Blitter_HOG_CPU_BusCountError )
		return true;
	else
		return false;
}


/* Macro to check if blitter can continue and do a 'return' if not */
#define	BLITTER_RETURN_IF_MAX_BUS_REACHED		if ( !BlitterVars.hog && !Blitter_ContinueNonHog() ) return 0;

/* Macro to suspend this transfer for now and keep src/dst to continue later */
#define	BLITTER_CONTINUE_LATER_IF_MAX_BUS_REACHED	if ( !BlitterVars.hog && !Blitter_ContinueNonHog() ) \
	{ \
	  /* fprintf ( stderr , "blitter suspended before write word have_src=%d have_dst=%d\n" , BlitterState.have_src ,BlitterState.have_dst ); */ \
	  BlitterState_ContinueLater = 1; return; \
	}



/*-----------------------------------------------------------------------*/
/**
 * Blitter emulation - level 1
 */

static void Blitter_SourceShift(void)
{
	if (BlitterRegs.src_x_incr < 0)
		BlitterVars.buffer >>= 16;
	else
		BlitterVars.buffer <<= 16;
}

static void Blitter_SourceFetch(void)
{
	Uint32 src_word = (Uint32)Blitter_ReadWord(BlitterRegs.src_addr);

	if (BlitterRegs.src_x_incr < 0)
		BlitterVars.buffer |= src_word << 16;
	else
		BlitterVars.buffer |= src_word;

	if (BlitterVars.src_words == 1)
	{
		BlitterRegs.src_addr += BlitterRegs.src_y_incr;
	}
	else
	{
		--BlitterVars.src_words;
		BlitterRegs.src_addr += BlitterRegs.src_x_incr;
	}
}

static Uint16 Blitter_SourceRead(void)
{
	if (!BlitterState.have_src)
	{
		if (BlitterState.fxsr)
		{
			Blitter_SourceShift();
			Blitter_SourceFetch();
		}

		Blitter_SourceShift();

		if (!BlitterState.nfsr)
		{
			Blitter_SourceFetch();
		}

		BlitterState.src_word = (Uint16)(BlitterVars.buffer >> BlitterVars.skew);
		BlitterState.have_src = true;
	}

	return BlitterState.src_word;
}

static Uint16 Blitter_DestRead(void)
{
	if (!BlitterState.have_dst)
	{
		BlitterState.dst_word = Blitter_ReadWord(BlitterRegs.dst_addr);
		BlitterState.have_dst = true;
	}

	return BlitterState.dst_word;
}

static Uint16 Blitter_GetHalftoneWord(void)
{
	if (BlitterVars.smudge)
		return BlitterHalftone[Blitter_SourceRead() & 15];
	else
		return BlitterHalftone[BlitterVars.line];
}

/* HOP */

static Uint16 Blitter_HOP_0(void)
{
	return 0xFFFF;
}

static Uint16 Blitter_HOP_1(void)
{
	return Blitter_GetHalftoneWord();
}

static Uint16 Blitter_HOP_2(void)
{
	return Blitter_SourceRead();
}

static Uint16 Blitter_HOP_3(void)
{
	Uint16 src;

	src = Blitter_SourceRead();
	BLITTER_RETURN_IF_MAX_BUS_REACHED;
	return src & Blitter_GetHalftoneWord();
}

static BLITTER_OP_FUNC Blitter_HOP_Table [4] =
{
	Blitter_HOP_0,
	Blitter_HOP_1,
	Blitter_HOP_2,
	Blitter_HOP_3
};

static void Blitter_Select_HOP(void)
{
	Blitter_ComputeHOP = Blitter_HOP_Table[BlitterRegs.hop];
}

/* end HOP */

/* LOP */

static Uint16 Blitter_LOP_0(void)
{
	return 0;
}

static Uint16 Blitter_LOP_1(void)
{
	Uint16	hop;

	hop = Blitter_ComputeHOP();
	BLITTER_RETURN_IF_MAX_BUS_REACHED;
	return hop & Blitter_DestRead();
}

static Uint16 Blitter_LOP_2(void)
{
	Uint16	hop;

	hop = Blitter_ComputeHOP();
	BLITTER_RETURN_IF_MAX_BUS_REACHED;
	return hop & ~Blitter_DestRead();
}

static Uint16 Blitter_LOP_3(void)
{
	return Blitter_ComputeHOP();
}

static Uint16 Blitter_LOP_4(void)
{
	Uint16	hop;

	hop = Blitter_ComputeHOP();
	BLITTER_RETURN_IF_MAX_BUS_REACHED;
	return ~hop & Blitter_DestRead();
}

static Uint16 Blitter_LOP_5(void)
{
	return Blitter_DestRead();
}

static Uint16 Blitter_LOP_6(void)
{
	Uint16	hop;

	hop = Blitter_ComputeHOP();
	BLITTER_RETURN_IF_MAX_BUS_REACHED;
	return hop ^ Blitter_DestRead();
}

static Uint16 Blitter_LOP_7(void)
{
	Uint16	hop;

	hop = Blitter_ComputeHOP();
	BLITTER_RETURN_IF_MAX_BUS_REACHED;
	return hop | Blitter_DestRead();
}

static Uint16 Blitter_LOP_8(void)
{
	Uint16	hop;

	hop = Blitter_ComputeHOP();
	BLITTER_RETURN_IF_MAX_BUS_REACHED;
	return ~hop & ~Blitter_DestRead();
}

static Uint16 Blitter_LOP_9(void)
{
	Uint16	hop;

	hop = Blitter_ComputeHOP();
	BLITTER_RETURN_IF_MAX_BUS_REACHED;
	return ~hop ^ Blitter_DestRead();
}

static Uint16 Blitter_LOP_A(void)
{
	return ~Blitter_DestRead();
}

static Uint16 Blitter_LOP_B(void)
{
	Uint16	hop;

	hop = Blitter_ComputeHOP();
	BLITTER_RETURN_IF_MAX_BUS_REACHED;
	return hop | ~Blitter_DestRead();
}

static Uint16 Blitter_LOP_C(void)
{
	return ~Blitter_ComputeHOP();
}

static Uint16 Blitter_LOP_D(void)
{
	Uint16	hop;

	hop = Blitter_ComputeHOP();
	BLITTER_RETURN_IF_MAX_BUS_REACHED;
	return ~hop | Blitter_DestRead();
}

static Uint16 Blitter_LOP_E(void)
{
	Uint16	hop;

	hop = Blitter_ComputeHOP();
	BLITTER_RETURN_IF_MAX_BUS_REACHED;
	return ~hop | ~Blitter_DestRead();
}

static Uint16 Blitter_LOP_F(void)
{
	return 0xFFFF;
}

static BLITTER_OP_FUNC Blitter_LOP_Table [16] =
{
	Blitter_LOP_0,
	Blitter_LOP_1,
	Blitter_LOP_2,
	Blitter_LOP_3,
	Blitter_LOP_4,
	Blitter_LOP_5,
	Blitter_LOP_6,
	Blitter_LOP_7,
	Blitter_LOP_8,
	Blitter_LOP_9,
	Blitter_LOP_A,
	Blitter_LOP_B,
	Blitter_LOP_C,
	Blitter_LOP_D,
	Blitter_LOP_E,
	Blitter_LOP_F
};

static void Blitter_Select_LOP(void)
{
	Blitter_ComputeLOP = Blitter_LOP_Table[BlitterRegs.lop];
}

/* end LOP */


/*-----------------------------------------------------------------------*/
/*
 * If BlitterState_ContinueLater==1, it means we're resuming from a previous
 * Blitter_ProcessWord() call that did not complete because we reached
 * maximum number of bus accesses. In that case, we continue from the latest
 * state, keeping the values we already have for src_word and dst_word.
 */

static void Blitter_BeginLine(void)
{
	if ( BlitterState_ContinueLater )				/* Resuming, don't start a new line */
		return;

	BlitterVars.src_words = BlitterVars.src_words_reset;
}

static void Blitter_SetState(Uint8 fxsr, Uint8 nfsr, Uint16 end_mask)
{
	BlitterState.fxsr = fxsr;
	BlitterState.nfsr = nfsr;
	BlitterState.end_mask = end_mask;

	if ( BlitterState_ContinueLater )
		BlitterState_ContinueLater = 0;				/* Resuming, keep previous values of have_src / have_dst */
	else
	{
		BlitterState.have_src = false;
		BlitterState.have_dst = false;
	}
}

static Uint16 Blitter_ComputeMask(Uint16 lop)
{
	return (lop & BlitterState.end_mask) | (Blitter_DestRead() & ~BlitterState.end_mask);
}

static void Blitter_ProcessWord(void)
{
	Uint16	lop;
	Uint16	dst_data;

	lop = Blitter_ComputeLOP();
	BLITTER_CONTINUE_LATER_IF_MAX_BUS_REACHED;

	/* When NFSR or mask is not all '1', a read-modify-write is always performed */
	if ( BlitterState.nfsr || ( BlitterState.end_mask != 0xFFFF ) )
	{
		dst_data = Blitter_ComputeMask( lop );
		BLITTER_CONTINUE_LATER_IF_MAX_BUS_REACHED;
	}
	else
	{
		dst_data = lop;
	}

	Blitter_WriteWord(BlitterRegs.dst_addr, dst_data);

	if (BlitterRegs.words == 1)
	{
		BlitterRegs.dst_addr += BlitterRegs.dst_y_incr;
	}
	else
	{
		--BlitterRegs.words;
		BlitterRegs.dst_addr += BlitterRegs.dst_x_incr;
	}
}

static void Blitter_EndLine(void)
{
	if ( BlitterState_ContinueLater )			/* We will continue later, don't end this line for now */
		return;

	--BlitterRegs.lines;
	BlitterRegs.words = BlitterVars.dst_words_reset;

	if (BlitterRegs.dst_y_incr >= 0)
		BlitterVars.line = (BlitterVars.line+1) & 15;
	else
		BlitterVars.line = (BlitterVars.line-1) & 15;
}

/*-----------------------------------------------------------------------*/
/**
 * Blitter emulation - level 2
 */

static void Blitter_SingleWord(void)
{
	Blitter_BeginLine();
	Blitter_SetState(BlitterVars.fxsr, BlitterVars.nfsr, BlitterRegs.end_mask_1);
	Blitter_ProcessWord();
	Blitter_EndLine();
}

static void Blitter_FirstWord(void)
{
	Blitter_BeginLine();
	Blitter_SetState(BlitterVars.fxsr, 0, BlitterRegs.end_mask_1);
	Blitter_ProcessWord();
}

static void Blitter_MiddleWord(void)
{
	Blitter_SetState(0, 0, BlitterRegs.end_mask_2);
	Blitter_ProcessWord();
}

static void Blitter_LastWord(void)
{
	Blitter_SetState(0, BlitterVars.nfsr, BlitterRegs.end_mask_3);
	Blitter_ProcessWord();
	Blitter_EndLine();
}

static void Blitter_Step(void)
{
	if (BlitterVars.dst_words_reset == 1)
	{
		Blitter_SingleWord();
	}
	else if (BlitterRegs.words == BlitterVars.dst_words_reset)
	{
		Blitter_FirstWord();
	}
	else if (BlitterRegs.words == 1)
	{
		Blitter_LastWord();
	}
	else
	{
		Blitter_MiddleWord();
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Let's do the blit.
 * Note that in non-HOG mode, the blitter only runs for 64 bus cycles
 * before giving the bus back to the CPU. Due to this mode, this function must
 * be able to abort and resume the blitting at any time.
 * - In cycle exact mode, the blitter will have 64 bus accesses and the cpu 64 bus accesses
 * - In non cycle exact mode, the blitter will have 64 bus accesses and the cpu
 *   will run during 64*4 = 256 cpu cycles
 */
static void Blitter_Start(void)
{
int FrameCycles, HblCounterVideo, LineCycles;
Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

//fprintf ( stderr , "blitter start %d video_cyc=%d %d@%d\n" , nCyclesMainCounter , FrameCycles , LineCycles, HblCounterVideo );

	/* Select HOP & LOP funcs */
	Blitter_Select_HOP();
	Blitter_Select_LOP();

	/* Setup vars */
	BlitterVars.pass_cycles = 0;
	BlitterVars.op_cycles = 0;
	BlitterVars.src_words_reset = BlitterVars.dst_words_reset + BlitterVars.fxsr - BlitterVars.nfsr;
	Blitter_CountBusBlitter = 0;

	/* Bus arbitration */
	Blitter_BusArbitration ( BUS_MODE_BLITTER );
	BlitterPhase = BLITTER_PHASE_RUN_TRANSFER;

	/* Busy=1, set line to high/1 and clear interrupt */
	MFP_GPIP_Set_Line_Input ( MFP_GPIP_LINE_GPU_DONE , MFP_GPIP_STATE_HIGH );

	/* Now we enter the main blitting loop */
	do
	{
		Blitter_Step();
	}
	while ( BlitterRegs.lines > 0
	       && ( BlitterVars.hog || Blitter_ContinueNonHog() ) );

	/* Bus arbitration */
	Blitter_BusArbitration ( BUS_MODE_CPU );

	BlitterRegs.ctrl = (BlitterRegs.ctrl & 0xF0) | BlitterVars.line;

	if (BlitterRegs.lines == 0)
	{
		/* Blit complete, clear busy and hog bits */
		BlitterRegs.ctrl &= ~(0x80|0x40);

		/* Busy=0, set line to low/0 and request interrupt */
		MFP_GPIP_Set_Line_Input ( MFP_GPIP_LINE_GPU_DONE , MFP_GPIP_STATE_LOW );

		BlitterPhase = BLITTER_PHASE_STOP;

		if ( BLITTER_RUN_CE )
		{
			/* In CE mode, we check if a CPU instruction could have ran in parallel to the blitter */
			BlitterPhase |= BLITTER_PHASE_IGNORE_LAST_CPU_CYCLES;
			Blitter_HOG_CPU_IgnoreMaxCpuCycles = BlitterVars.pass_cycles;
		}
	}
	else
	{
		/* Blit not complete yet in non-hog mode, give back the bus to the CPU */
		BlitterPhase = BLITTER_PHASE_COUNT_CPU_BUS;

		if ( BLITTER_RUN_CE )
		{
			/* Continue blitting after 64 bus accesses in 68000 CE mode + check for parallel CPU instruction */
			BlitterPhase |= BLITTER_PHASE_IGNORE_LAST_CPU_CYCLES;
			Blitter_HOG_CPU_IgnoreMaxCpuCycles = BlitterVars.pass_cycles;
			Blitter_CountBusCpu = 0;		/* Reset CPU bus counter */
		}
		else
		{
			/* In non-cycle exact 68000 mode, we run the CPU for 64*4=256 cpu cycles, */
			/* which gives a good approximation */
			CycInt_AddRelativeInterrupt ( NONHOG_BUS_CPU*4, INT_CPU_CYCLE, INTERRUPT_BLITTER );
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * This is called when no more CPU cycles should be ignored in case an
 * instruction was running in parallel to the blitter
 */
static void Blitter_Stop_IgnoreLastCpuCycles(void)
{
	BlitterPhase &= ~BLITTER_PHASE_IGNORE_LAST_CPU_CYCLES;	/* No more CPU in parallel, stop ignoring next CPU cycles */

	/* If blitter is completely OFF now, disable the cpu specific part */
	if ( BlitterPhase == BLITTER_PHASE_STOP )
		M68000_SetBlitter_CE ( false );
}


/*-----------------------------------------------------------------------*/
/**
 * Read blitter halftone ram.
 */
static void Blitter_Halftone_ReadWord(int index)
{
	IoMem_WriteWord(REG_HT_RAM + index + index, BlitterHalftone[index]);
}

void Blitter_Halftone00_ReadWord(void) { Blitter_Halftone_ReadWord(0); }
void Blitter_Halftone01_ReadWord(void) { Blitter_Halftone_ReadWord(1); }
void Blitter_Halftone02_ReadWord(void) { Blitter_Halftone_ReadWord(2); }
void Blitter_Halftone03_ReadWord(void) { Blitter_Halftone_ReadWord(3); }
void Blitter_Halftone04_ReadWord(void) { Blitter_Halftone_ReadWord(4); }
void Blitter_Halftone05_ReadWord(void) { Blitter_Halftone_ReadWord(5); }
void Blitter_Halftone06_ReadWord(void) { Blitter_Halftone_ReadWord(6); }
void Blitter_Halftone07_ReadWord(void) { Blitter_Halftone_ReadWord(7); }
void Blitter_Halftone08_ReadWord(void) { Blitter_Halftone_ReadWord(8); }
void Blitter_Halftone09_ReadWord(void) { Blitter_Halftone_ReadWord(9); }
void Blitter_Halftone10_ReadWord(void) { Blitter_Halftone_ReadWord(10); }
void Blitter_Halftone11_ReadWord(void) { Blitter_Halftone_ReadWord(11); }
void Blitter_Halftone12_ReadWord(void) { Blitter_Halftone_ReadWord(12); }
void Blitter_Halftone13_ReadWord(void) { Blitter_Halftone_ReadWord(13); }
void Blitter_Halftone14_ReadWord(void) { Blitter_Halftone_ReadWord(14); }
void Blitter_Halftone15_ReadWord(void) { Blitter_Halftone_ReadWord(15); }

/*-----------------------------------------------------------------------*/
/**
 * Read blitter source x increment (0xff8a20).
 */
void Blitter_SourceXInc_ReadWord(void)
{
	IoMem_WriteWord(REG_SRC_X_INC, (Uint16)(BlitterRegs.src_x_incr));
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter source y increment (0xff8a22).
 */
void Blitter_SourceYInc_ReadWord(void)
{
	IoMem_WriteWord(REG_SRC_Y_INC, (Uint16)(BlitterRegs.src_y_incr));
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter source address (0xff8a24).
 */
void Blitter_SourceAddr_ReadLong(void)
{
	IoMem_WriteLong(REG_SRC_ADDR, BlitterRegs.src_addr);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter endmask 1.
 */
void Blitter_Endmask1_ReadWord(void)
{
	IoMem_WriteWord(REG_END_MASK1, BlitterRegs.end_mask_1);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter endmask 2.
 */
void Blitter_Endmask2_ReadWord(void)
{
	IoMem_WriteWord(REG_END_MASK2, BlitterRegs.end_mask_2);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter endmask 3.
 */
void Blitter_Endmask3_ReadWord(void)
{
	IoMem_WriteWord(REG_END_MASK3, BlitterRegs.end_mask_3);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter destination x increment (0xff8a2E).
 */
void Blitter_DestXInc_ReadWord(void)
{
	IoMem_WriteWord(REG_DST_X_INC, (Uint16)(BlitterRegs.dst_x_incr));
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter destination y increment (0xff8a30).
 */
void Blitter_DestYInc_ReadWord(void)
{
	IoMem_WriteWord(REG_DST_Y_INC, (Uint16)(BlitterRegs.dst_y_incr));
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter destination address.
 */
void Blitter_DestAddr_ReadLong(void)
{
	IoMem_WriteLong(REG_DST_ADDR, BlitterRegs.dst_addr);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter words-per-line register.
 */
void Blitter_WordsPerLine_ReadWord(void)
{
	IoMem_WriteWord(REG_X_COUNT, (Uint16)(BlitterRegs.words & 0xFFFF));
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter lines-per-bitblock register.
 */
void Blitter_LinesPerBitblock_ReadWord(void)
{
	IoMem_WriteWord(REG_Y_COUNT, (Uint16)(BlitterRegs.lines & 0xFFFF));
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter halftone operation register.
 */
void Blitter_HalftoneOp_ReadByte(void)
{
	IoMem_WriteByte(REG_BLIT_HOP, BlitterRegs.hop);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter logical operation register.
 */
void Blitter_LogOp_ReadByte(void)
{
	IoMem_WriteByte(REG_BLIT_LOP, BlitterRegs.lop);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter control register.
 */
void Blitter_Control_ReadByte(void)
{
	/* busy, hog/blit, smudge, n/a, 4bits for line number */
	IoMem_WriteByte(REG_CONTROL, BlitterRegs.ctrl);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter skew register.
 */
void Blitter_Skew_ReadByte(void)
{
	IoMem_WriteByte(REG_SKEW, BlitterRegs.skew);
}


/*-----------------------------------------------------------------------*/
/**
 * Write to blitter halftone ram.
 */
static void Blitter_Halftone_WriteWord(int index)
{
	BlitterHalftone[index] = IoMem_ReadWord(REG_HT_RAM + index + index);
}

void Blitter_Halftone00_WriteWord(void) { Blitter_Halftone_WriteWord(0); }
void Blitter_Halftone01_WriteWord(void) { Blitter_Halftone_WriteWord(1); }
void Blitter_Halftone02_WriteWord(void) { Blitter_Halftone_WriteWord(2); }
void Blitter_Halftone03_WriteWord(void) { Blitter_Halftone_WriteWord(3); }
void Blitter_Halftone04_WriteWord(void) { Blitter_Halftone_WriteWord(4); }
void Blitter_Halftone05_WriteWord(void) { Blitter_Halftone_WriteWord(5); }
void Blitter_Halftone06_WriteWord(void) { Blitter_Halftone_WriteWord(6); }
void Blitter_Halftone07_WriteWord(void) { Blitter_Halftone_WriteWord(7); }
void Blitter_Halftone08_WriteWord(void) { Blitter_Halftone_WriteWord(8); }
void Blitter_Halftone09_WriteWord(void) { Blitter_Halftone_WriteWord(9); }
void Blitter_Halftone10_WriteWord(void) { Blitter_Halftone_WriteWord(10); }
void Blitter_Halftone11_WriteWord(void) { Blitter_Halftone_WriteWord(11); }
void Blitter_Halftone12_WriteWord(void) { Blitter_Halftone_WriteWord(12); }
void Blitter_Halftone13_WriteWord(void) { Blitter_Halftone_WriteWord(13); }
void Blitter_Halftone14_WriteWord(void) { Blitter_Halftone_WriteWord(14); }
void Blitter_Halftone15_WriteWord(void) { Blitter_Halftone_WriteWord(15); }

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter source x increment.
 */
void Blitter_SourceXInc_WriteWord(void)
{
	BlitterRegs.src_x_incr = (short)(IoMem_ReadWord(REG_SRC_X_INC) & 0xFFFE);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter source y increment.
 */
void Blitter_SourceYInc_WriteWord(void)
{
	BlitterRegs.src_y_incr = (short)(IoMem_ReadWord(REG_SRC_Y_INC) & 0xFFFE);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter source address register (0xff8a24).
 */
void Blitter_SourceAddr_WriteLong(void)
{
	if ( ConfigureParams.System.bAddressSpace24 == true )
		BlitterRegs.src_addr = IoMem_ReadLong(REG_SRC_ADDR) & 0x00FFFFFE;	/* Normal STF/STE */
	else
		BlitterRegs.src_addr = IoMem_ReadLong(REG_SRC_ADDR) & 0xFFFFFFFE;	/* Falcon with extra TT RAM */
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter endmask 1.
 */
void Blitter_Endmask1_WriteWord(void)
{
	BlitterRegs.end_mask_1 = IoMem_ReadWord(REG_END_MASK1);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter endmask 2.
 */
void Blitter_Endmask2_WriteWord(void)
{
	BlitterRegs.end_mask_2 = IoMem_ReadWord(REG_END_MASK2);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter endmask 3.
 */
void Blitter_Endmask3_WriteWord(void)
{
	BlitterRegs.end_mask_3 = IoMem_ReadWord(REG_END_MASK3);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter destination x increment.
 */
void Blitter_DestXInc_WriteWord(void)
{
	BlitterRegs.dst_x_incr = (short)(IoMem_ReadWord(REG_DST_X_INC) & 0xFFFE);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter source y increment.
 */
void Blitter_DestYInc_WriteWord(void)
{
	BlitterRegs.dst_y_incr = (short)(IoMem_ReadWord(REG_DST_Y_INC) & 0xFFFE);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter destination address register.
 */
void Blitter_DestAddr_WriteLong(void)
{
	if ( ConfigureParams.System.bAddressSpace24 == true )
		BlitterRegs.dst_addr = IoMem_ReadLong(REG_DST_ADDR) & 0x00FFFFFE;	/* Normal STF/STE */
	else
		BlitterRegs.dst_addr = IoMem_ReadLong(REG_DST_ADDR) & 0xFFFFFFFE;	/* Falcon with extra TT RAM */
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter words-per-line register.
 */
void Blitter_WordsPerLine_WriteWord(void)
{
	Uint32 words = (Uint32)IoMem_ReadWord(REG_X_COUNT);

	if (words == 0)
		words = 65536;

	BlitterRegs.words = words;
	BlitterVars.dst_words_reset = words;
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter lines-per-bitblock register.
 */
void Blitter_LinesPerBitblock_WriteWord(void)
{
	Uint32 lines = (Uint32)IoMem_ReadWord(REG_Y_COUNT);

	if (lines == 0)
		lines = 65536;

	BlitterRegs.lines = lines;
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter halftone operation register.
 */
void Blitter_HalftoneOp_WriteByte(void)
{
	/* h/ware reg masks out the top 6 bits! */
	BlitterRegs.hop = IoMem_ReadByte(REG_BLIT_HOP) & 3;
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter logical operation register.
 */
void Blitter_LogOp_WriteByte(void)
{
	/* h/ware reg masks out the top 4 bits! */
	BlitterRegs.lop = IoMem_ReadByte(REG_BLIT_LOP) & 0xF;
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter control register.
 */
void Blitter_Control_WriteByte(void)
{
	/* Control register bits:
	 * 0x80: busy bit
	 * - Turn on Blitter activity and stay "1" until copy finished
	 * 0x40: Blit-mode bit
	 * - 0: Blit mode, CPU and Blitter get 64 bus accesses in turns
	 * - 1: HOG Mode, Blitter reserves and hogs the bus for as long
	 *      as the copy takes, CPU and DMA get no Bus access
	 * 0x20: Smudge mode
	 * - Which line of the halftone pattern to start with is
	 *   read from the first source word when the copy starts
	 * 0x10: not used
	 * 0x0f
	 *
	 * The lowest 4 bits contain the Halftone pattern line number
	 */

	if (LOG_TRACE_LEVEL(TRACE_BLITTER))
	{
		int FrameCycles, HblCounterVideo, LineCycles;

		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

		LOG_TRACE_PRINT("blitter write ctrl=%x video_cyc=%d %d@%d pc=%x instr_cyc=%d\n" ,
				IoMem_ReadByte(REG_CONTROL) ,
				FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	BlitterRegs.ctrl = IoMem_ReadByte(REG_CONTROL) & 0xEF;

	BlitterVars.hog = BlitterRegs.ctrl & 0x40;
	BlitterVars.smudge = BlitterRegs.ctrl & 0x20;
	BlitterVars.line = BlitterRegs.ctrl & 0xF;

	/* Remove old pending update interrupt */
	CycInt_RemovePendingInterrupt(INTERRUPT_BLITTER);

	/* Busy bit set? */
	if (BlitterRegs.ctrl & 0x80)
	{
		if (BlitterRegs.lines == 0)
		{
			/* We're done, clear busy and hog bits */
			BlitterRegs.ctrl &= ~(0x80|0x40);
		}
		else
		{
			/* Start blitter after a small delay */
			/* In non-hog mode, the cpu can "restart" the blitter immediately (without waiting */
			/* for 64 cpu bus accesses) by setting busy bit. This means we should reset */
			/* BlitterState_ContinueLater only if the blitter was stopped before ; */
			/* else if the blitter is restarted we must keep the value of BlitterState_ContinueLater */
			if ( BLITTER_RUN_CE )
			{
				if ( BlitterPhase == BLITTER_PHASE_STOP )	/* Only when blitter is started (not when restarted) */
				{
					M68000_SetBlitter_CE ( true );
					BlitterState_ContinueLater = 0;
				}

				/* 68000 CE : 4 cycles to complete current bus write to ctrl reg + 4 cycles before blitter request the bus */
				Blitter_CyclesBeforeStart = 4 + 4;
				BlitterPhase = BLITTER_PHASE_PRE_START;
				Blitter_HOG_CPU_BusCountError = 0;
			}
			else
			{
				if ( BlitterPhase == BLITTER_PHASE_STOP )	/* Only when blitter is started (not when restarted) */
				{
					BlitterState_ContinueLater = 0;
				}

				/* Non 68000 CE mode : start blitting after the end of current instruction */
				CycInt_AddRelativeInterrupt( CurrentInstrCycles+WaitStateCycles, INT_CPU_CYCLE, INTERRUPT_BLITTER);
			}
		}
	}

	else					/* busy bit clear */
	{
		BlitterPhase = BLITTER_PHASE_STOP;
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter skew register.
 */
void Blitter_Skew_WriteByte(void)
{
	BlitterRegs.skew = IoMem_ReadByte(REG_SKEW);
	BlitterVars.fxsr = (BlitterRegs.skew & 0x80)?1:0;
	BlitterVars.nfsr = (BlitterRegs.skew & 0x40)?1:0;
	BlitterVars.skew = BlitterRegs.skew & 0xF;
}


/*-----------------------------------------------------------------------*/
/**
 * Handler which continues blitting after 64 bus cycles.
 */
void Blitter_InterruptHandler(void)
{
	CycInt_AcknowledgeInterrupt();

	if (BlitterRegs.ctrl & 0x80)
	{
		Blitter_Start();
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of Blitter variables.
 */
void Blitter_MemorySnapShot_Capture(bool bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(&BlitterRegs, sizeof(BlitterRegs));
	MemorySnapShot_Store(&BlitterVars, sizeof(BlitterVars));
	MemorySnapShot_Store(&BlitterHalftone, sizeof(BlitterHalftone));


	/* TODO : save new CE variables + call M68000_SetBlitter_CE on restore if phase!=0 */
}

/*-----------------------------------------------------------------------*/
/**
 * Show Blitter register values.
 */
void Blitter_Info(FILE *fp, Uint32 dummy)
{
	BLITTERREGS *regs = &BlitterRegs;

	fprintf(fp, "src addr:  0x%06x\n", regs->src_addr);
	fprintf(fp, "dst addr:  0x%06x\n", regs->dst_addr);
	fprintf(fp, "words:     %u\n", regs->words);
	fprintf(fp, "lines:     %u\n", regs->lines);
	fprintf(fp, "src X-inc: %hd\n", regs->src_x_incr);
	fprintf(fp, "src Y-inc: %hd\n", regs->src_y_incr);
	fprintf(fp, "dst X-inc: %hd\n", regs->dst_x_incr);
	fprintf(fp, "dst Y-inc: %hd\n", regs->dst_y_incr);
	fprintf(fp, "end mask1: 0x%04x\n", regs->end_mask_1);
	fprintf(fp, "end mask2: 0x%04x\n", regs->end_mask_2);
	fprintf(fp, "end mask3: 0x%04x\n", regs->end_mask_3);
	fprintf(fp, "HOP:       0x%02x\n", regs->hop);
	fprintf(fp, "LOP:       0x%02x\n", regs->lop);
	fprintf(fp, "control:   0x%02x\n", regs->ctrl);
	fprintf(fp, "skew:      0x%02x\n", regs->skew);
	fprintf(fp, "Note: internally changed register values aren't visible to breakpoints\nor in memdump output until emulated code reads or writes them!\n");
}


static void	Blitter_HOG_CPU ( int bus_count )
{
//fprintf ( stderr , "cpu_bus after phase=%d bus=%d %x %d cur_cyc=%lu start_acces=%d\n" , BlitterPhase , BusMode , BlitterRegs.ctrl , Blitter_CountBusCpu , currcycle/cpucycleunit,Blitter_HOG_CPU_BlitterStartDuringBusAccess );

	if ( BlitterPhase & BLITTER_PHASE_COUNT_CPU_BUS )
	{
		if ( Blitter_HOG_CPU_BlitterStartDuringBusAccess )
		{
			Blitter_HOG_CPU_BlitterStartDuringBusAccess = 0;
			return;
		}
		
		Blitter_CountBusCpu += bus_count;
		if ( Blitter_CountBusCpu >= NONHOG_BUS_CPU )
		{
			Blitter_CyclesBeforeStart = 4;
			BlitterPhase = BLITTER_PHASE_PRE_START;
			Blitter_HOG_CPU_BusCountError = 0;
		}
	}

}



/*-----------------------------------------------------------------------*/
/**
 * This is called from the CPU emulation before doing a memory access.
 */
void	Blitter_HOG_CPU_mem_access_before ( int bus_count )
{
//fprintf ( stderr , "cpu_bus before phase=%d bus=%d %x %d cur_cyc=%lu start_acces=%d\n" , BlitterPhase , BusMode , BlitterRegs.ctrl , Blitter_CountBusCpu , currcycle/cpucycleunit , Blitter_HOG_CPU_BlitterStartDuringBusAccess );

	Blitter_HOG_CPU_FromBusAccess = 1;			/* CPU bus access in progress */

	/* NOTE [NP] It seems there's a bug in the blitter when it counts his own bus accesses : */
	/* if a CPU bus access happens during the "pre start" blitter phase, then the blitter will wrongly */
	/* count it as a blitter bus access and will do only 63 bus accesses during copy instead of 64 */
	if ( BlitterPhase == BLITTER_PHASE_PRE_START )
		Blitter_HOG_CPU_BusCountError = 1;

	/* If the bus is accessed by the CPU and we're ignoring CPU cycles that occurred in parallel */
	/* during the blitter's transfer, then we disable IGNORE_LAST_CPU_CYCLES as the CPU was stalled */
	/* after this point because it couldn't access the bus (which was owned by the blitter) */
	else if ( BlitterPhase & BLITTER_PHASE_IGNORE_LAST_CPU_CYCLES )
	{
		Blitter_Stop_IgnoreLastCpuCycles();		/* No more CPU in parallel, stop ignoring next CPU cycles */
	}
}



/*-----------------------------------------------------------------------*/
/**
 * This is called from the CPU emulation after doing a memory access.
 * Here, we count the number of bus accesses made by the CPU in non-hog mode.
 * When the CPU reaches 64 accesses, we restart the blitter.
 */
void	Blitter_HOG_CPU_mem_access_after ( int bus_count )
{
	Blitter_HOG_CPU ( bus_count );

	Blitter_HOG_CPU_FromBusAccess = 0;			/* CPU bus access is done */
}




/*-----------------------------------------------------------------------*/
/**
 * This is called from the CPU emulation before "do_cycles()" to check
 * if part of an instruction was executed simultaneously to the blitter.
 * If so, we don't count those CPU cycles (as they were already counted
 * during the blitter part) and we skip the "do_cycles()"
 * We skip CPU cycles until Blitter_HOG_CPU_IgnoreMaxCpuCycles=0 or until
 * we reach a bus access (whichever comes first)
 */

int	Blitter_Check_Simultaneous_CPU ( void )
{
	static int cpu_skip_cycles = 0;
//fprintf ( stderr , "blitter simult phase=%d bus=%d %x cur_cyc=%lu\n" , BlitterPhase , BusMode , BlitterRegs.ctrl , currcycle/cpucycleunit );

	if ( BlitterPhase & BLITTER_PHASE_IGNORE_LAST_CPU_CYCLES )
	{
		Blitter_HOG_CPU_IgnoreMaxCpuCycles -= 2;
		if ( Blitter_HOG_CPU_IgnoreMaxCpuCycles <= 0 )
			Blitter_Stop_IgnoreLastCpuCycles();	/* No more CPU in parallel, stop ignoring next CPU cycles */

		cpu_skip_cycles += 2;
//fprintf ( stderr , "blitter cpu skip %d cycles, max skip %d\n" , cpu_skip_cycles , Blitter_HOG_CPU_IgnoreMaxCpuCycles );
		return 1;					/* Skip next do_cycles() */
	}

	cpu_skip_cycles = 0;
	return 0;						/* Don't skip next do_cycles() */
}



/*-----------------------------------------------------------------------*/
/**
 * This is called from the cpu emulation after "do_cycles()" to count
 * the number of cycles since the blitter was (re)started.
 * After Blitter_CyclesBeforeStart cycles, we go to the next phase BLITTER_PHASE_START
 * to handle bus to the blitter and to copy data.
 */
 void	Blitter_HOG_CPU_do_cycles_after ( int cycles )
{
//fprintf ( stderr , "blitter do_cyc_after phase=%d bus=%d %x cyc=%d cur_cyc=%lu\n" , BlitterPhase , BusMode , BlitterRegs.ctrl , cycles , currcycle/cpucycleunit );

	if ( BlitterPhase == BLITTER_PHASE_PRE_START )
	{
		Blitter_CyclesBeforeStart -= cycles;
		if ( Blitter_CyclesBeforeStart <= 0 )
		{
			/* This is specific to our cpu emulation, to avoid counting the current */
			/* bus access (during which the blitter starts) as the first cpu bus access in non-hog mode */
			if ( Blitter_HOG_CPU_FromBusAccess )
				Blitter_HOG_CPU_BlitterStartDuringBusAccess = 1;
			else
				Blitter_HOG_CPU_BlitterStartDuringBusAccess = 0;

			/* Start the main blitter part */
			BlitterPhase = BLITTER_PHASE_START;
			Blitter_Start();
		}
	}

}


