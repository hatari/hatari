/*
 * Hatari - blitter.c
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * Blitter emulation. The 'Blitter' chip is found in the Mega-ST, STE/Mega-STE
 * and Falcon. It provides a very fast BitBlit function in hardware.
 *
 * This file has originally been taken from STonX's blitter by Martin Griffiths,
 * but it has been completely modified for better maintainability and higher compatibility.
 *
 */



/*

  NOTES [NP] : As of August 2017, the blitter code was partly rewritten to allow cycle exact bus accesses
  between the blitter and the CPU, allowing to have CPU instruction running in parallel to the blitter
  when the CPU doesn't need to access the bus.

  The internal work of the blitter regarding bus accesses was deduced from studying many cases
  on a real STE, including several demos using the blitter with overscan, some tests programs on
  www.atari-forum.com (from Cyprian Konador), as well as my own tests on an STE.

  Depending on the CPU instruction used to start the blitter, we can see that the next instruction
  can be partially or totally executed during the blit, or that it will be executed only after the blit is done.

  To understand the reason for this parallel execution, we must know the sequence used by the blitter
  until it gets the bus and start transferring data.

  Based on several examples, possible sequence when starting the blitter seems to be :
   - t+0 : write to FF8A3C is complete or blitter "restarts" itself in non-hog mode
   - t+0 : CPU can still run during 4 cycles and access bus
   - t+4 : bus arbitration takes 4 cycles (no access for CPU and blitter during this time)
   - t+8 : blitter owns the bus and starts transferring data

  In case of MegaSTE, the arbitration to grant the bus to the blitter takes 8 cycles instead of 4.
  But when bus is granted back to the CPU, it takes 4 cycles on both STE and MegaSTE.

  We can see there's a 4 cycles latency between when the busy bit is set and when the blitter asks
  for a bus grant. It's during these 4 cycles that part of the next CPU instruction can be run, if the
  current instruction that started the blitter doesn't need the bus anymore.

  For example :
   - move.b d0,(a0) + nop	: MOVE.B will do a write then a read to prefetch the next instruction
				-> NOP will run after the blit
   - bset #7,(a0) + nop		: BSET will read first then finish with a write
				-> NOP will run before the blit
   - bset #7,(a0) + mulu dx,dy	: BSET will read first then finish with a write. Then mulu will prefetch before the blit starts
				-> MULU will run in parallel to the blitter
   - bset #7,(a0) + divu dx,dy	: BSET will read first then finish with a write. Then divu will run internal cycles and finish with a prefetch
				-> all the cycles from the DIVU will run in parallel to the blitter until we reach the DIVU's prefetch

  So, by interleaving CPU instructions with some blitter transfers, it is possible to run part of those instructions
  in parallel to the blitter, thus saving CPU cycles on some costly instructions (div,mul,...)

  - Number of bus shared between CPU and blitter : as described in Atari developers documentation,
    when HOG mode is disabled the blitter will run during 64 bus accesses (read or write), then it
    will give the bus to the CPU for 64 bus accesses too. But in some cases (see below), a possible
    bug in the blitter will make it use the bus during only 63 accesses instead of 64.

  - As verified on a real STE, when the blitter owns the bus in non-hog mode, it will give back the bus to the CPU
    exactly after the 64th (or 63th) bus access, not just after writing the result of the current word transfer.
    For the emulation, this means the blitter's state must be preserved to be able to resume after any bus read
    made by the blitter (blitter's operations can have between 0 and 2 reads and 1 write).

  - Blitter doing only 63 bus accesses instead of 64 in non-hog mode : my guess is that in non-hog mode the blitter
    will always count bus accesses as soon as busy bit is set, even when it has not started to transfer its own data.
    So, if the CPU does a bus access during the 4 cycles latency between t+0 and t+4 above, then this CPU bus access
    will be counted by the blitter as a blitter bus access, thus effectively losing one bus access and doing
    only 63 bus accesses after that (before granting the bus to the CPU for the next 64 bus accesses).


  Some examples of demos requiring cycle exact blitter mode to correctly work :
   - 'Relapse - Graphix Sound 2' by Cybernetics (overscan plasma using blitter)
		This demo uses a self-calibration routine to adapt blitter code to the MegaSTE.
		$e764 : move.b  d5,(a4) + dbra d1,$fff2 : 4 cycles of the dbra can be executed while blitter starts


  From an emulation point of view, the code needed for cycle exact blitter mode requires to intercept
  bus accesses before and after they occur, as well as intercepting "do_cycle" before and after too.

  Parallel execution of the CPU is obtained by skipping as many CPU cycles as the blitter ran,
  or by skipping CPU cycles until the next CPU bus access (at which point the CPU would stall).

  When HOG mode is disabled, the CPU can also stop the blitter in case it needs more than 64 bus accesses
  (for example to handle some timing-sensitive interrupts). When CPU owns the bus, writing '0' to bit 7 of the control
  register will stop the blitter, writing '1' will resume the blitter from where it was before being stopped.
  Note that writing '0' will not end the current blitter transfer, reading 'busy' bit 7 will still return '1'.
  It's only when 'y count' reaches '0' that transfer will be complete and busy bit will be cleared.

  As measured on STE (as well as on Falcon), the blitter produces "strange" results when
  each line is only 1 word (xcount=1) and NFSR is set at the same time, depending on whether
  src X increment is >0 or <0.
  This was discussed in may 2017 in Hatari mailing list and a model still need to be found to cover all cases.
  UPDATE : as of september 2020, this behaviour should be correctly emulated (based on reverse engineering and
  Verilog implementation made by Jorge Cwik (Ijor))


  NOTE for Falcon using 32 bit TT RAM : standard Falcon can only address 24 bits of memory with the CPU,
  the Falcon's blitter is also limited to 24 bits addresses.
  Some extension boards such as Afterburner or CT2 could use extra "TT RAM" which required 32 bits addresses,
  but as normal TOS was not designed to handle 32 bits addresses (because is was not possible hardwire-wise)
  we need to "simulate" 32 bits source/dest address at $FF8A24 and $FF8A32 instead of masking 24 bits
  (see Blitter_SourceAddr_WriteLong and Blitter_DestAddr_WriteLong)
*/



const char Blitter_fileid[] = "Hatari blitter.c";

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
#include "video.h"
#include "hatari-glue.h"
#include "falcon/dsp.h"


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


/* Blitter registers */
typedef struct
{
	uint32_t	src_addr;
	uint32_t	dst_addr;
	uint32_t	x_count;
	uint32_t	y_count;
	short	src_x_incr;
	short	src_y_incr;
	short	dst_x_incr;
	short	dst_y_incr;
	uint16_t	end_mask_1;
	uint16_t	end_mask_2;
	uint16_t	end_mask_3;
	uint8_t	hop;
	uint8_t	lop;
	uint8_t	ctrl;
	uint8_t	skew;
} BLITTERREGS;

/* Blitter vars */
typedef struct
{
	uint32_t	pass_cycles;
	uint32_t	op_cycles;
	uint32_t	total_cycles;

	uint32_t	buffer;
	uint32_t	x_count_reset;
	uint8_t	hog;
	uint8_t	smudge;
	uint8_t	halftone_line;
	uint8_t	fxsr;
	uint8_t	nfsr;
	uint8_t	skew;
} BLITTERVARS;

/* Blitter state */
typedef struct
{
	uint8_t	fxsr;
	uint8_t	nfsr;
	uint8_t	have_fxsr;
	uint8_t	need_src;
	uint8_t	have_src;
	uint8_t	fetch_src;
	uint8_t	need_dst;
	uint8_t	have_dst;

	uint16_t	src_word;
	uint16_t	dst_word;
	uint16_t	bus_word;

	uint16_t	end_mask;

	uint16_t	CountBusBlitter;				/* To count bus accesses made by the blitter */
	uint16_t	CountBusCpu;					/* To count bus accesses made by the CPU */
	uint8_t	ContinueLater;					/* 0=false / 1=true */
} BLITTERSTATE;


/* Blitter logical op func */
typedef uint16_t (*BLITTER_OP_FUNC)(void);

static BLITTERREGS	BlitterRegs;
static BLITTERVARS	BlitterVars;
static BLITTERSTATE	BlitterState;
static uint16_t		BlitterHalftone[16];

static BLITTER_OP_FUNC	Blitter_ComputeHOP;
static BLITTER_OP_FUNC	Blitter_ComputeLOP;



/* To handle CPU/blitter bus sharing in non-hog mode (require cycle exact mode for CPU emulation) */
#define	BLITTER_PHASE_STOP			0		/* blitter is completely stopped */
#define	BLITTER_PHASE_PRE_START			1
#define	BLITTER_PHASE_START			2
#define	BLITTER_PHASE_RUN_TRANSFER		4		/* blitter owns the bus and transfer data */
#define	BLITTER_PHASE_COUNT_CPU_BUS		8		/* cpu owns the bus during 64 accesses */
#define	BLITTER_PHASE_IGNORE_LAST_CPU_CYCLES	16
#define	BLITTER_PHASE_PAUSE			32		/* cpu owns the bus (COUNT_CPU_BUS) and stops the blitter */

uint16_t		BlitterPhase = BLITTER_PHASE_STOP;		/* Internal state of the blitter */

static uint16_t	Blitter_CyclesBeforeStart;			/* Number of cycles after setting busy bit before calling Blitter_Start */
								/* (during this time, the CPU can still run and access the bus) */

static uint8_t	Blitter_HOG_CPU_FromBusAccess;			/* 0 or 1 (false/true) */
static uint8_t	Blitter_HOG_CPU_BlitterStartDuringBusAccess;	/* 0 or 1 (false/true) */
static uint16_t	Blitter_HOG_CPU_BusCountError;			/* 0 or 1 (false/true) */
static uint16_t	Blitter_HOG_CPU_IgnoreMaxCpuCycles;		/* Max number of blitter cycles during which the CPU might run in parallel */
								/* (unless the CPU is stalled earlier by a bus access) */


/* Number of bus accesses allocated to blitter and CPU in non-hog mode */
#define BLITTER_NONHOG_BUS_BLITTER		64		/* Can also be 63, see Blitter_HOG_CPU_BusCountError */
#define BLITTER_NONHOG_BUS_CPU			64

#define	BLITTER_CYCLES_PER_BUS_READ		4		/* The blitter takes 4 cycles to read 1 memory word on STE */
#define	BLITTER_CYCLES_PER_BUS_WRITE		4		/* The blitter takes 4 cycles to write 1 memory word on STE */


/* Return 'true' if CE mode can be enabled for blitter (ie when using 68000 CE mode) */
#define	BLITTER_RUN_CE		( CpuRunCycleExact && ( currprefs.cpu_model == 68000 ) )


/* Used to compute the blitter's usage during each VBL (for statusbar) */
static int	BlitterStatsRate;



/*-----------------------------------------------------------------------*/
/**
 * Reset all blitter variables
 */
void Blitter_Reset ( void )
{
	BlitterRegs.src_addr = 0;
	BlitterRegs.dst_addr = 0;
	BlitterRegs.x_count = 0;
	BlitterRegs.y_count = 0;
	BlitterRegs.src_x_incr = 0;
	BlitterRegs.src_y_incr = 0;
	BlitterRegs.dst_x_incr = 0;
	BlitterRegs.dst_y_incr = 0;
	BlitterRegs.end_mask_1 = 0;
	BlitterRegs.end_mask_2 = 0;
	BlitterRegs.end_mask_3 = 0;
	BlitterRegs.hop = 0;
	BlitterRegs.lop = 0;

	BlitterRegs.ctrl = 0;
	BlitterVars.hog = 0;
	BlitterVars.smudge = 0;
	BlitterVars.halftone_line = 0;

	BlitterRegs.skew = 0;
	BlitterVars.fxsr = 0;
	BlitterVars.nfsr = 0;
	BlitterVars.skew = 0;

	BlitterState.fxsr = false;
	BlitterState.nfsr = false;
	BlitterState.have_fxsr = false;
	BlitterState.need_src = false;
	BlitterState.have_src = false;
	BlitterState.fetch_src = false;
	BlitterState.need_dst = false;
	BlitterState.have_dst = false;
	BlitterState.bus_word = 0;
	BlitterState.ContinueLater = 0 ;
}



/*-----------------------------------------------------------------------*/
/**
 * Compute some stats for the blitter's usage during a period (eg one VBL)
 * Used to determine a percent per VBL and show a led in the statusbar
 */
void	Blitter_StatsUpdateRate ( int period_cycles )
{
	int percent;

	if ( period_cycles == 0 )
		percent = 0;
	else
		percent = ceil ( 100.0 * BlitterVars.total_cycles / period_cycles );

//fprintf ( stderr , "blitter %d %%\n" , percent );
	BlitterVars.total_cycles = 0;
	BlitterStatsRate = percent;
}


int	Blitter_StatsGetRate ( void )
{
	return BlitterStatsRate;
}



/*-----------------------------------------------------------------------*/
/**
 * Count blitter cycles (this assumes blitter and CPU runs at the same freq)
 */
static void Blitter_AddCycles(int cycles)
{
	int all_cycles = cycles + WaitStateCycles;

	BlitterVars.op_cycles += all_cycles;
	BlitterVars.total_cycles += all_cycles;
//fprintf ( stderr , "blitter add_cyc cyc=%d total=%d cur_cyc=%lu\n" , all_cycles , BlitterVars.op_cycles , currcycle/cpucycleunit );
//fprintf ( stderr , "blitter src %x dst %x ycount %d\n" , BlitterRegs.src_addr , BlitterRegs.dst_addr , BlitterRegs.y_count );

	nCyclesMainCounter += all_cycles;
	CyclesGlobalClockCounter += all_cycles;
	WaitStateCycles = 0;
}

static void Blitter_FlushCycles(void)
{
//fprintf ( stderr , "blitter flush_cyc cyc=%d pass=%d %d cur_cyc=%lu\n" , BlitterVars.op_cycles , BlitterVars.pass_cycles , nCyclesMainCounter , currcycle/cpucycleunit );

	if ( BLITTER_RUN_CE )					/* In CE mode, flush cycles already counted in the current cpu instruction */
	{
		M68000_AddCycles_CE ( currcycle * 2 / CYCLE_UNIT );
 		currcycle = 0;
	}

	CycInt_Process();

	/* Run DSP while blitter owns the bus */
	if (bDspEnabled) {
		DSP_Run(2 * BlitterVars.op_cycles);
	}

	BlitterVars.pass_cycles += BlitterVars.op_cycles;
	BlitterVars.op_cycles = 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Handle bus arbitration when switching between CPU and Blitter
 * When a write is made to FF8A3C to start the blitter, it will take a few cycles
 * before doing the bus arbitration. During this time the CPU will be able to
 * partially execute the next instruction in parallel to the blitter
 * (until an access to the BUS is needed by the CPU).
 *
 * Based on several examples, possible sequence when starting the blitter seems to be :
 *  - t+0 : write to FF8A3C
 *  - t+0 : CPU can still run during 4 cycles and access bus
 *  - t+4 : bus arbitration takes 4 cycles (no access for cpu and blitter during this time)
 *  - t+8 : blitter owns the bus and starts transferring data
 * (in case of MegaSTE bus arbitration takes 8 cycles instead of 4)
 *
 * When blitter stops owning the bus in favor of the cpu, this seems to always take 4 cycles
 */
static void Blitter_BusArbitration ( int RequestBusMode )
{
	int	cycles;

	if ( RequestBusMode == BUS_MODE_BLITTER )	/* Bus is requested by the blitter */
	{
		cycles = 4;				/* Default case : take 4 cycles when going from cpu to blitter */
		if ( ConfigureParams.System.nMachineType == MACHINE_MEGA_STE )
			cycles = 8;			/* MegaSTE blitter needs 4 extra cycles when requesting the bus */
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
static uint16_t Blitter_ReadWord(uint32_t addr)
{
	uint16_t value;

	value = STMemory_DMA_ReadWord ( addr );

	BlitterState.CountBusBlitter++;
	Blitter_AddCycles ( BLITTER_CYCLES_PER_BUS_READ );
	Blitter_FlushCycles();

	BlitterState.bus_word = value;
	return value;
}

static void Blitter_WriteWord(uint32_t addr, uint16_t value)
{
	BlitterState.bus_word = value;

	STMemory_DMA_WriteWord ( addr ,  value );

	BlitterState.CountBusBlitter++;
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
	if ( BlitterState.CountBusBlitter < BLITTER_NONHOG_BUS_BLITTER )
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
	  BlitterState.ContinueLater = 1; return; \
	}



/*-----------------------------------------------------------------------*/
/**
 * Blitter emulation - level 1 (lower level)
 */

static void Blitter_SourceShift(void)
{
	if (BlitterRegs.src_x_incr < 0)
		BlitterVars.buffer >>= 16;
	else
		BlitterVars.buffer <<= 16;
}

static void Blitter_SourceFetch( bool nfsr_on )
{
	uint32_t src_word;

	if ( !nfsr_on )
		src_word = (uint32_t)Blitter_ReadWord(BlitterRegs.src_addr);
	else
		src_word = (uint32_t)BlitterState.bus_word;

	if (BlitterRegs.src_x_incr < 0)
		BlitterVars.buffer |= src_word << 16;
	else
		BlitterVars.buffer |= src_word;
}

static uint16_t Blitter_SourceRead(void)
{
	return (uint16_t)(BlitterVars.buffer >> BlitterVars.skew);
}

static uint16_t Blitter_DestRead(void)
{
	return BlitterState.dst_word;
}

static uint16_t Blitter_GetHalftoneWord(void)
{
	if ( BlitterVars.smudge )
		return BlitterHalftone[Blitter_SourceRead() & 15];
	else
		return BlitterHalftone[BlitterVars.halftone_line];
}


/* HOP */

static uint16_t Blitter_HOP_0(void)
{
	return 0xFFFF;
}

static uint16_t Blitter_HOP_1(void)
{
	return Blitter_GetHalftoneWord();
}

static uint16_t Blitter_HOP_2(void)
{
	return Blitter_SourceRead();
}

static uint16_t Blitter_HOP_3(void)
{
	return Blitter_SourceRead() & Blitter_GetHalftoneWord();
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

static uint16_t Blitter_LOP_0(void)
{
	return 0;
}

static uint16_t Blitter_LOP_1(void)
{
	return Blitter_ComputeHOP() & Blitter_DestRead();
}

static uint16_t Blitter_LOP_2(void)
{
	return Blitter_ComputeHOP() & ~Blitter_DestRead();
}

static uint16_t Blitter_LOP_3(void)
{
	return Blitter_ComputeHOP();
}

static uint16_t Blitter_LOP_4(void)
{
	return ~Blitter_ComputeHOP() & Blitter_DestRead();
}

static uint16_t Blitter_LOP_5(void)
{
	return Blitter_DestRead();
}

static uint16_t Blitter_LOP_6(void)
{
	return Blitter_ComputeHOP() ^ Blitter_DestRead();
}

static uint16_t Blitter_LOP_7(void)
{
	return Blitter_ComputeHOP() | Blitter_DestRead();
}

static uint16_t Blitter_LOP_8(void)
{
	return ~Blitter_ComputeHOP() & ~Blitter_DestRead();
}

static uint16_t Blitter_LOP_9(void)
{
	return ~Blitter_ComputeHOP() ^ Blitter_DestRead();
}

static uint16_t Blitter_LOP_A(void)
{
	return ~Blitter_DestRead();
}

static uint16_t Blitter_LOP_B(void)
{
	return Blitter_ComputeHOP() | ~Blitter_DestRead();
}

static uint16_t Blitter_LOP_C(void)
{
	return ~Blitter_ComputeHOP();
}

static uint16_t Blitter_LOP_D(void)
{
	return ~Blitter_ComputeHOP() | Blitter_DestRead();
}

static uint16_t Blitter_LOP_E(void)
{
	return ~Blitter_ComputeHOP() | ~Blitter_DestRead();
}

static uint16_t Blitter_LOP_F(void)
{
	return 0xFFFF;
}

static const struct {
	BLITTER_OP_FUNC	lop_func;
	uint8_t		need_src;
	uint8_t		need_dst;
} Blitter_LOP_Table [16] =
{
	{ Blitter_LOP_0, false, false } ,
	{ Blitter_LOP_1, true,	true },
	{ Blitter_LOP_2, true,	true },
	{ Blitter_LOP_3, true,	false },
	{ Blitter_LOP_4, true,	true },
	{ Blitter_LOP_5, false,	true },
	{ Blitter_LOP_6, true,	true },
	{ Blitter_LOP_7, true,	true },
	{ Blitter_LOP_8, true,	true },
	{ Blitter_LOP_9, true,	true },
	{ Blitter_LOP_A, false,	true },
	{ Blitter_LOP_B, true,	true },
	{ Blitter_LOP_C, true,	false },
	{ Blitter_LOP_D, true,	true },
	{ Blitter_LOP_E, true,	true },
	{ Blitter_LOP_F, false,	false }
};

static void Blitter_Select_LOP(void)
{
	Blitter_ComputeLOP = Blitter_LOP_Table[BlitterRegs.lop].lop_func;
}

/* end LOP */


static void Blitter_ProcessWord(void)
{
	uint16_t	lop;
	uint16_t	dst_data;


	/* Do FXSR if needed (only if src is used) */
	if ( BlitterState.fxsr && !BlitterState.have_fxsr && BlitterState.need_src )
	{
		Blitter_SourceShift();
		Blitter_SourceFetch( false );
		BlitterRegs.src_addr += BlitterRegs.src_x_incr;		/* always increment src_addr after doing the FXSR */
		BlitterState.have_fxsr = true;
		BLITTER_CONTINUE_LATER_IF_MAX_BUS_REACHED
	}

	/* Read src if needed */
	if ( BlitterState.need_src && !BlitterState.have_src )
	{
		if ( !BlitterState.nfsr )
		{
			Blitter_SourceShift();
			Blitter_SourceFetch( false );
			BlitterState.have_src = true;
			BlitterState.fetch_src = true;
			BLITTER_CONTINUE_LATER_IF_MAX_BUS_REACHED
		}
	}

	/* Read dst if needed */
	if ( BlitterState.need_dst && !BlitterState.have_dst )
	{
		BlitterState.dst_word = Blitter_ReadWord(BlitterRegs.dst_addr);
		BlitterState.have_dst = true;
		BLITTER_CONTINUE_LATER_IF_MAX_BUS_REACHED
	}

	/* Special 'weird' case for x_count=1 and NFSR=1 */
	if ( ( BlitterVars.nfsr ) && ( BlitterRegs.x_count == 1 ) )
	{
		Blitter_SourceShift();
		Blitter_SourceFetch( true );
	}

	lop = Blitter_ComputeLOP();

	/* When mask is not all '1', a read-modify-write is always performed */
	/* NOTE : Atari's doc wrongly states that NFSR can also do a RMW, but only mask can */
	/* (cf http://www.atari-forum.com/viewtopic.php?f=16&t=38157) */
	if ( BlitterState.end_mask != 0xFFFF )
		dst_data = (lop & BlitterState.end_mask) | (Blitter_DestRead() & ~BlitterState.end_mask);
	else
		dst_data = lop;

	Blitter_WriteWord(BlitterRegs.dst_addr, dst_data);

	/* Special 'weird' case for x_count=1 and NFSR=1 */
	if ( ( BlitterVars.nfsr ) && ( BlitterRegs.x_count == 1 ) )
	{
		Blitter_SourceShift();
		Blitter_SourceFetch( true );
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Blitter emulation - level 2 (higher level)
 *
 * If BlitterState.ContinueLater==1, it means we're resuming from a previous
 * Blitter_ProcessWord() call that did not complete because we reached
 * maximum number of bus accesses. In that case, we continue from the latest
 * state, keeping the values we already have for src_word and dst_word.
 */

/*
 * Reset internal states after fully processing 1 word or when blitter is started
 */
static void Blitter_FlushWordState( bool FlushFxsr )
{
	if ( FlushFxsr )
		BlitterState.have_fxsr = false;

	BlitterState.have_src = false;
	BlitterState.fetch_src = false;
	BlitterState.have_dst = false;
}


/**
 * Process 1 word for the current x_count/y_count values.
 * Update addresses/counters/states when done
 * If too many bus accesses were made in non-hog mode, we return
 * and we resume later from the same states.
 */
static void Blitter_Step(void)
{
	bool	FirstWord;

	if ( BlitterState.ContinueLater )
		BlitterState.ContinueLater = 0;				/* Resuming, keep previous values of have_src/have_dst/have_fxsr/... */

	/* Check if this is the first word of a line */
	FirstWord = ( BlitterRegs.x_count == BlitterVars.x_count_reset );

	/* Set mask for this word (order of 'if' matters) */
	if ( FirstWord || ( BlitterVars.x_count_reset == 1 ) )		/* 1st word or single word line */
		BlitterState.end_mask = BlitterRegs.end_mask_1;
	else if ( BlitterRegs.x_count == 1 )				/* last word for non-single word line */
		BlitterState.end_mask = BlitterRegs.end_mask_3;
	else								/* middle word for non-single word line */
		BlitterState.end_mask = BlitterRegs.end_mask_2;

	/* Set internal nfsr=0 by default at the start of a new line (it will be updated if needed when xcount goes from 2 to 1) */
	if ( FirstWord )
		BlitterState.nfsr = 0;

	/* Read an extra word at the start of a line if FXSR is set */
	/* This extra word will only be read if the blitter LOP/HOP needs to read src */
	if ( FirstWord )
		BlitterState.fxsr = BlitterVars.fxsr;

	/* Check if this operation requires to read src */
	BlitterState.need_src = Blitter_LOP_Table[BlitterRegs.lop].need_src;
	/* Check if HOP uses src : bit1==1 or halftone with smudge bit */
	BlitterState.need_src = BlitterState.need_src && ( ( BlitterRegs.hop & 2 ) || ( ( BlitterRegs.hop == 1 ) && BlitterVars.smudge ) );

	/* Check if this operation requires to read dst (if mask != 0xFFFF, read dst will be forced to do a read-modify-write */
	BlitterState.need_dst = Blitter_LOP_Table[BlitterRegs.lop].need_dst || ( BlitterState.end_mask != 0xFFFF );


	/* Call main function to process the data */
	/* Read src/dst/halftone (if needed) + process + write to dst */
	Blitter_ProcessWord();
	if ( BlitterState.ContinueLater == 1 )				/* blitter did not complete due to too many bus accesses */
		return;							/* stop now and resume later */


	/* Write was done, update counters/addresses/states for next step */
	/* Take NFSR value into account (this must be checked when x_count=2, as on real blitter) */
	if ( ( BlitterRegs.x_count == 2 ) && BlitterVars.nfsr )
		BlitterState.nfsr = 1;					/* next source read will be ignored in Blitter_SourceRead() */

	/* Update source address if a word was read from src */
	if ( BlitterState.fetch_src )
	{
		/* If this was the last read of a line or if last read will be ignored, then we go to the next source line */
		if ( ( BlitterRegs.x_count == 1 ) || ( BlitterState.nfsr == 1 ) )
			BlitterRegs.src_addr += BlitterRegs.src_y_incr;
		else
			BlitterRegs.src_addr += BlitterRegs.src_x_incr;
	}

	/* Update X/Y count as well as dest address */
	if ( BlitterRegs.x_count == 1 )					/* end of line reached */
	{
		BlitterState.have_fxsr = false;
		BlitterRegs.y_count--;
		BlitterRegs.x_count = BlitterVars.x_count_reset;

		BlitterRegs.dst_addr += BlitterRegs.dst_y_incr;

		if ( BlitterRegs.dst_y_incr >= 0 )
			BlitterVars.halftone_line = ( BlitterVars.halftone_line+1 ) & 15;
		else
			BlitterVars.halftone_line = ( BlitterVars.halftone_line-1 ) & 15;
	}
	else								/* continue on the same line */
	{
		BlitterRegs.x_count--;
		BlitterRegs.dst_addr += BlitterRegs.dst_x_incr;
	}

	/* ProcessWord is complete, reset internal content of src/dst words */
	Blitter_FlushWordState ( false );
}


/*-----------------------------------------------------------------------*/
/**
 * Start/Resume the blitter
 *
 * Note that in non-hog mode, the blitter only runs for 64 bus cycles
 * before giving the bus back to the CPU. Due to this mode, this function must
 * be able to abort and resume the blitting at any time, keeping the same internal states.
 * - In cycle exact mode, the blitter will have 64 bus accesses and the cpu 64 bus accesses
 * - In non cycle exact mode, the blitter will have 64 bus accesses and the cpu
 *   will run during 64*4 = 256 cpu cycles
 */
static void Blitter_Start(void)
{
int FrameCycles, HblCounterVideo, LineCycles;
Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

//fprintf ( stderr , "blitter start %d video_cyc=%d %d@%d\n" , nCyclesMainCounter , FrameCycles , LineCycles, HblCounterVideo );
//fprintf ( stderr , "blitter start addr=%x dst=%x xcount=%d ycount=%d fxsr=%d nfsr=%d skew=%d src_x_incr=%d src_y_incr=%d\n" , BlitterRegs.src_addr ,BlitterRegs.dst_addr, BlitterRegs.x_count , BlitterRegs.y_count , BlitterVars.fxsr , BlitterVars.nfsr , BlitterVars.skew , BlitterRegs.src_x_incr , BlitterRegs.src_y_incr );

	/* Select HOP & LOP funcs */
	Blitter_Select_HOP();
	Blitter_Select_LOP();

	/* Setup vars */
	BlitterVars.pass_cycles = 0;
	BlitterVars.op_cycles = 0;
	BlitterState.CountBusBlitter = 0;
	if ( Blitter_HOG_CPU_BusCountError )
		BlitterState.CountBusBlitter++;				/* Bug in the blitter : count 1 CPU access as a blitter access */

	/* Bus arbitration */
	Blitter_BusArbitration ( BUS_MODE_BLITTER );
	BlitterPhase = BLITTER_PHASE_RUN_TRANSFER;

	/* Busy=1, set line to high/1 and clear interrupt */
	MFP_GPIP_Set_Line_Input ( pMFP_Main , MFP_GPIP_LINE_GPU_DONE , MFP_GPIP_STATE_HIGH );

	/* Now we enter the main blitting loop */
	do
	{
		Blitter_Step();
	}
	while ( BlitterRegs.y_count > 0
	       && ( BlitterVars.hog || Blitter_ContinueNonHog() ) );

	/* Bus arbitration */
	Blitter_BusArbitration ( BUS_MODE_CPU );

	BlitterRegs.ctrl = (BlitterRegs.ctrl & 0xF0) | BlitterVars.halftone_line;

	if (BlitterRegs.y_count == 0)
	{
		/* Blit complete, clear busy and hog bits */
		BlitterRegs.ctrl &= ~(0x80|0x40);

		/* Busy=0, set line to low/0 and request interrupt */
		MFP_GPIP_Set_Line_Input ( pMFP_Main , MFP_GPIP_LINE_GPU_DONE , MFP_GPIP_STATE_LOW );

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
			BlitterState.CountBusCpu = 0;		/* Reset CPU bus counter */
		}
		else
		{
			/* In non-cycle exact 68000 mode, we run the CPU for 64*4=256 cpu cycles, */
			/* which gives a good approximation */
			CycInt_AddRelativeInterrupt ( BLITTER_NONHOG_BUS_CPU*4, INT_CPU_CYCLE, INTERRUPT_BLITTER );
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
 * Check if some word or long word registers are accessed using byte
 * operations at address IoAccessCurrentAddress.
 * The blitter doesn't allow reading/writing to word registers using bytes,
 * in such case we must ignore the read/write.
 * Return true if it's a byte access.
 */
static bool Blitter_CheckAccess_Byte ( void )
{
	if ( nIoMemAccessSize == SIZE_BYTE )
	{
		if ( LOG_TRACE_LEVEL(TRACE_BLITTER) )
		{
			int FrameCycles, HblCounterVideo, LineCycles;
			Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

			LOG_TRACE_PRINT("blitter byte access at address=%x ignored video_cyc=%d %d@%d pc=%x instr_cyc=%d\n" ,
					IoAccessCurrentAddress ,
					FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
		}
		return true;
	}

  return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Read blitter halftone ram.
 */
static void Blitter_Halftone_ReadWord(int index)
{
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

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
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	IoMem_WriteWord(REG_SRC_X_INC, (uint16_t)(BlitterRegs.src_x_incr));
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter source y increment (0xff8a22).
 */
void Blitter_SourceYInc_ReadWord(void)
{
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	IoMem_WriteWord(REG_SRC_Y_INC, (uint16_t)(BlitterRegs.src_y_incr));
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter source address (0xff8a24).
 */
void Blitter_SourceAddr_ReadLong(void)
{
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	IoMem_WriteLong(REG_SRC_ADDR, BlitterRegs.src_addr);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter endmask 1.
 */
void Blitter_Endmask1_ReadWord(void)
{
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	IoMem_WriteWord(REG_END_MASK1, BlitterRegs.end_mask_1);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter endmask 2.
 */
void Blitter_Endmask2_ReadWord(void)
{
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	IoMem_WriteWord(REG_END_MASK2, BlitterRegs.end_mask_2);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter endmask 3.
 */
void Blitter_Endmask3_ReadWord(void)
{
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	IoMem_WriteWord(REG_END_MASK3, BlitterRegs.end_mask_3);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter destination x increment (0xff8a2E).
 */
void Blitter_DestXInc_ReadWord(void)
{
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	IoMem_WriteWord(REG_DST_X_INC, (uint16_t)(BlitterRegs.dst_x_incr));
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter destination y increment (0xff8a30).
 */
void Blitter_DestYInc_ReadWord(void)
{
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	IoMem_WriteWord(REG_DST_Y_INC, (uint16_t)(BlitterRegs.dst_y_incr));
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter destination address.
 */
void Blitter_DestAddr_ReadLong(void)
{
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	IoMem_WriteLong(REG_DST_ADDR, BlitterRegs.dst_addr);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter words-per-line register X count.
 */
void Blitter_WordsPerLine_ReadWord(void)
{
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	IoMem_WriteWord(REG_X_COUNT, (uint16_t)(BlitterRegs.x_count & 0xFFFF));
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter lines-per-bitblock register Y count.
 */
void Blitter_LinesPerBitblock_ReadWord(void)
{
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	IoMem_WriteWord(REG_Y_COUNT, (uint16_t)(BlitterRegs.y_count & 0xFFFF));
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
	/* busy, hog/blit, smudge, n/a, 4 bits for halftone line number */
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
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

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
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	BlitterRegs.src_x_incr = (short)(IoMem_ReadWord(REG_SRC_X_INC) & 0xFFFE);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter source y increment.
 */
void Blitter_SourceYInc_WriteWord(void)
{
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	BlitterRegs.src_y_incr = (short)(IoMem_ReadWord(REG_SRC_Y_INC) & 0xFFFE);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter source address register (0xff8a24).
 */
void Blitter_SourceAddr_WriteLong(void)
{
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

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
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	BlitterRegs.end_mask_1 = IoMem_ReadWord(REG_END_MASK1);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter endmask 2.
 */
void Blitter_Endmask2_WriteWord(void)
{
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	BlitterRegs.end_mask_2 = IoMem_ReadWord(REG_END_MASK2);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter endmask 3.
 */
void Blitter_Endmask3_WriteWord(void)
{
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	BlitterRegs.end_mask_3 = IoMem_ReadWord(REG_END_MASK3);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter destination x increment.
 */
void Blitter_DestXInc_WriteWord(void)
{
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	BlitterRegs.dst_x_incr = (short)(IoMem_ReadWord(REG_DST_X_INC) & 0xFFFE);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter source y increment.
 */
void Blitter_DestYInc_WriteWord(void)
{
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	BlitterRegs.dst_y_incr = (short)(IoMem_ReadWord(REG_DST_Y_INC) & 0xFFFE);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter destination address register.
 */
void Blitter_DestAddr_WriteLong(void)
{
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	if ( ConfigureParams.System.bAddressSpace24 == true )
		BlitterRegs.dst_addr = IoMem_ReadLong(REG_DST_ADDR) & 0x00FFFFFE;	/* Normal STF/STE */
	else
		BlitterRegs.dst_addr = IoMem_ReadLong(REG_DST_ADDR) & 0xFFFFFFFE;	/* Falcon with extra TT RAM */
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter words-per-line register X count.
 */
void Blitter_WordsPerLine_WriteWord(void)
{
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	uint32_t x_count = (uint32_t)IoMem_ReadWord(REG_X_COUNT);

	if (x_count == 0)
		x_count = 65536;

	BlitterRegs.x_count = x_count;
	BlitterVars.x_count_reset = x_count;
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter lines-per-bitblock register Y count.
 */
void Blitter_LinesPerBitblock_WriteWord(void)
{
	if ( Blitter_CheckAccess_Byte() )
		return;						/* Ignore access */

	uint32_t y_count = (uint32_t)IoMem_ReadWord(REG_Y_COUNT);

	if (y_count == 0)
		y_count = 65536;

	BlitterRegs.y_count = y_count;
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
	 * bit 7 : start/stop bit (write) - busy bit (read)
	 *	- Turn on Blitter activity and stay "1" until copy finished
	 * bit 6 : Blit-mode bit
	 *	- 0: Blit mode, CPU and Blitter get 64 bus accesses in turns
	 *	- 1: HOG Mode, Blitter reserves and hogs the bus for as long
	 *      as the copy takes, CPU and DMA get no Bus access
	 * bit 5 : Smudge mode
	 * 	Which line of the halftone pattern to start with is read from
	 *	the first source word when the copy starts
	 * bit 4 : not used
	 * bits 0-3 :
	 *	The lowest 4 bits contain the halftone pattern line number
	 */

	if (LOG_TRACE_LEVEL(TRACE_BLITTER))
	{
		int FrameCycles, HblCounterVideo, LineCycles;

		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

		LOG_TRACE_PRINT("blitter write ctrl=%02x ctrl_old=%02x video_cyc=%d %d@%d pc=%x instr_cyc=%d\n" ,
				IoMem_ReadByte(REG_CONTROL) , BlitterRegs.ctrl ,
				FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	BlitterRegs.ctrl = IoMem_ReadByte(REG_CONTROL) & 0xEF;

	BlitterVars.hog = BlitterRegs.ctrl & 0x40;
	BlitterVars.smudge = BlitterRegs.ctrl & 0x20;
	BlitterVars.halftone_line = BlitterRegs.ctrl & 0xF;

	/* Remove old pending update interrupt */
	CycInt_RemovePendingInterrupt(INTERRUPT_BLITTER);

	/* Start/Stop bit set ? */
	if (BlitterRegs.ctrl & 0x80)
	{
		if (BlitterRegs.y_count == 0)
		{
			/* Blitter transfer is already complete, clear busy and hog bits */
			BlitterRegs.ctrl &= ~(0x80|0x40);			// TODO : check on real STE, does it clear hog bit too ?
		}
		else
		{
			/* Start blitter after a small delay */
			/* In non-hog mode, the cpu can "restart" the blitter immediately (without waiting */
			/* for 64 cpu bus accesses) by setting "start/stop" bit. The cpu can also "stop" the blitter (PAUSE state) */
			/* and restart it here (blitter continues from where it was stopped)  */
			/* This means we should reset internal states only if the blitter was stopped before ; */
			/* else if the blitter is restarted we must keep the value of the previous states */
			if ( BLITTER_RUN_CE )
			{
				if ( BlitterPhase == BLITTER_PHASE_STOP )	/* Only when blitter is started (not when restarted) */
				{
					M68000_SetBlitter_CE ( true );
					Blitter_FlushWordState ( true );
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
					Blitter_FlushWordState ( true );
				}

				/* Non 68000 CE mode : start blitting after the end of current instruction */
				CycInt_AddRelativeInterrupt( CurrentInstrCycles+WaitStateCycles, INT_CPU_CYCLE, INTERRUPT_BLITTER);
			}
		}
	}

	else					/* Start/Stop bit clear */
	{
		/* If the blitter was running and "start/stop" bit is forced to 0 (to stop the blitter in non-hog mode) */
		/* we "pause" the blitter to temporarily stop bus sharing . This does not clear busy bit, it's only */
		/* when 'y count' reaches 0 that transfer will be complete and busy bit will be cleared */
		/* If blitter is already stopped, we don't do anything */
		if ( BlitterPhase == BLITTER_PHASE_COUNT_CPU_BUS )
		{
			BlitterPhase = BLITTER_PHASE_PAUSE;
		}
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
 * Handler which continues blitting after 64 bus cycles in non-CE mode
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
	MemorySnapShot_Store(&BlitterState, sizeof(BlitterState));

	MemorySnapShot_Store(&BlitterPhase, sizeof(BlitterPhase));
	MemorySnapShot_Store(&Blitter_CyclesBeforeStart, sizeof(Blitter_CyclesBeforeStart));
	MemorySnapShot_Store(&Blitter_HOG_CPU_FromBusAccess, sizeof(Blitter_HOG_CPU_FromBusAccess));
	MemorySnapShot_Store(&Blitter_HOG_CPU_BlitterStartDuringBusAccess, sizeof(Blitter_HOG_CPU_BlitterStartDuringBusAccess));
	MemorySnapShot_Store(&Blitter_HOG_CPU_BusCountError, sizeof(Blitter_HOG_CPU_BusCountError));
	MemorySnapShot_Store(&Blitter_HOG_CPU_IgnoreMaxCpuCycles, sizeof(Blitter_HOG_CPU_IgnoreMaxCpuCycles));

	if ( !bSave )
	{
		/* On restore, we set blitter specific CPU functions if needed */
		if ( BlitterPhase && BLITTER_RUN_CE )
			M68000_SetBlitter_CE ( true );
	}

}

/*-----------------------------------------------------------------------*/
/**
 * Show Blitter register values.
 */
void Blitter_Info(FILE *fp, uint32_t dummy)
{
	BLITTERREGS *regs = &BlitterRegs;

	fprintf(fp, "src addr  (0x%x): 0x%06x\n", REG_SRC_ADDR, regs->src_addr);
	fprintf(fp, "dst addr  (0x%x): 0x%06x\n", REG_DST_ADDR, regs->dst_addr);
	fprintf(fp, "x count   (0x%x): %u\n",     REG_X_COUNT, regs->x_count);
	fprintf(fp, "y count   (0x%x): %u\n",     REG_Y_COUNT, regs->y_count);
	fprintf(fp, "src X-inc (0x%x): %hd\n",    REG_SRC_X_INC, regs->src_x_incr);
	fprintf(fp, "src Y-inc (0x%x): %hd\n",    REG_SRC_Y_INC, regs->src_y_incr);
	fprintf(fp, "dst X-inc (0x%x): %hd\n",    REG_DST_X_INC, regs->dst_x_incr);
	fprintf(fp, "dst Y-inc (0x%x): %hd\n",    REG_DST_Y_INC, regs->dst_y_incr);
	fprintf(fp, "end mask1 (0x%x): 0x%04x\n", REG_END_MASK1, regs->end_mask_1);
	fprintf(fp, "end mask2 (0x%x): 0x%04x\n", REG_END_MASK2, regs->end_mask_2);
	fprintf(fp, "end mask3 (0x%x): 0x%04x\n", REG_END_MASK3, regs->end_mask_3);
	fprintf(fp, "HOP       (0x%x): 0x%02x\n", REG_BLIT_HOP, regs->hop);
	fprintf(fp, "LOP       (0x%x): 0x%02x\n", REG_BLIT_LOP, regs->lop);
	/* List control bits: busy, hog/blit, smudge, n/a, 4 bits for halftone line number ? */
	fprintf(fp, "control   (0x%x): 0x%02x\n", REG_CONTROL, regs->ctrl);
	fprintf(fp, "skew      (0x%x): 0x%02x\n", REG_SKEW, regs->skew);
	fprintf(fp, "Note: internally changed register values aren't visible to breakpoints\nor in memdump output until emulated code reads or writes them!\n");
}




/*-----------------------------------------------------------------------*/
/**
 * This is called from the CPU emulation before doing a memory access.
 */
void	Blitter_HOG_CPU_mem_access_before ( int bus_count )
{
//fprintf ( stderr , "cpu_bus before phase=%d bus=%d %x %d cur_cyc=%lu start_acces=%d\n" , BlitterPhase , BusMode , BlitterRegs.ctrl , BlitterState.CountBusCpu , currcycle/cpucycleunit , Blitter_HOG_CPU_BlitterStartDuringBusAccess );

	Blitter_HOG_CPU_FromBusAccess = 1;			/* CPU bus access in progress */

	/* NOTE [NP] It seems there's a bug in the blitter when it counts his own bus accesses : */
	/* if a CPU bus access happens during the "pre start" blitter phase, then the blitter will wrongly */
	/* count it as a blitter bus access and will do only 63 bus accesses during copy instead of 64 in non-hog mode */
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
//fprintf ( stderr , "cpu_bus after phase=%d bus=%d %x %d cur_cyc=%lu start_acces=%d\n" , BlitterPhase , BusMode , BlitterRegs.ctrl , BlitterState.CountBusCpu , currcycle/cpucycleunit,Blitter_HOG_CPU_BlitterStartDuringBusAccess );

	if ( BlitterPhase & BLITTER_PHASE_COUNT_CPU_BUS )
	{
		if ( Blitter_HOG_CPU_BlitterStartDuringBusAccess )
		{
			Blitter_HOG_CPU_BlitterStartDuringBusAccess = 0;
			return;
		}
		
		BlitterState.CountBusCpu += bus_count;
		if ( BlitterState.CountBusCpu >= BLITTER_NONHOG_BUS_CPU )
		{
			Blitter_CyclesBeforeStart = 4;
			BlitterPhase = BLITTER_PHASE_PRE_START;
			Blitter_HOG_CPU_BusCountError = 0;
		}
	}


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
	// static int cpu_skip_cycles = 0;
	// fprintf(stderr, "blitter simult phase=%d bus=%d %x cur_cyc=%lu\n", BlitterPhase, BusMode, BlitterRegs.ctrl, currcycle/cpucycleunit);

	if ( BlitterPhase & BLITTER_PHASE_IGNORE_LAST_CPU_CYCLES )
	{
		Blitter_HOG_CPU_IgnoreMaxCpuCycles -= 2;
		if ( Blitter_HOG_CPU_IgnoreMaxCpuCycles <= 0 )
			Blitter_Stop_IgnoreLastCpuCycles();	/* No more CPU in parallel, stop ignoring next CPU cycles */

		// cpu_skip_cycles += 2;
		// fprintf(stderr, "blitter cpu skip %d cycles, max skip %d\n", cpu_skip_cycles, Blitter_HOG_CPU_IgnoreMaxCpuCycles);
		return 1;					/* Skip next do_cycles() */
	}

	// cpu_skip_cycles = 0;
	return 0;						/* Don't skip next do_cycles() */
}



/*-----------------------------------------------------------------------*/
/**
 * This is called from the cpu emulation after "do_cycles()" to count
 * the number of cycles since the blitter was (re)started.
 * After Blitter_CyclesBeforeStart cycles, we go to the next phase BLITTER_PHASE_START
 * to handle bus to the blitter and to copy data.
 * We must ensure the cpu is not doing an atomic RMW bus access ; if so, blitter must be started
 * later as it can't get the bus at the moment
 */
 void	Blitter_HOG_CPU_do_cycles_after ( int cycles )
{
//fprintf ( stderr , "blitter do_cyc_after phase=%d bus=%d %x cyc=%d cur_cyc=%lu\n" , BlitterPhase , BusMode , BlitterRegs.ctrl , cycles , currcycle/cpucycleunit );

	if ( BlitterPhase == BLITTER_PHASE_PRE_START )
	{
		Blitter_CyclesBeforeStart -= cycles;
		if ( ( Blitter_CyclesBeforeStart <= 0 ) && ( cpu_bus_rmw == false ) )
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


