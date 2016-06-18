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

/* Cycles to run for in non-hog mode */
#define NONHOG_CYCLES	(64*4)

/* BLiTTER registers, incs are signed, others unsigned */
#define REG_HT_RAM		0xff8a00	/* - 0xff8a1e */

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

#define REG_BLIT_HOP	0xff8a3a	/* halftone blit operation byte */
#define REG_BLIT_LOP	0xff8a3b	/* logical blit operation byte */
#define REG_CONTROL 	0xff8a3c
#define REG_SKEW		0xff8a3d


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
	int		pass_cycles;
	int		op_cycles;
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

static BLITTER_OP_FUNC Blitter_ComputeHOP;
static BLITTER_OP_FUNC Blitter_ComputeLOP;

/*-----------------------------------------------------------------------*/
/**
 * Count blitter cycles
 */

static void Blitter_AddCycles(int cycles)
{
	int all_cycles = cycles + WaitStateCycles;

	BlitterVars.op_cycles += all_cycles;

	nCyclesMainCounter += all_cycles >> nCpuFreqShift;
	CyclesGlobalClockCounter += all_cycles >> nCpuFreqShift;
	WaitStateCycles = 0;
}

static void Blitter_FlushCycles(void)
{
	int op_cycles = INT_CONVERT_TO_INTERNAL(BlitterVars.op_cycles >> nCpuFreqShift, INT_CPU_CYCLE);

	BlitterVars.pass_cycles += BlitterVars.op_cycles;
	BlitterVars.op_cycles = 0;

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
 *
 * When blitter stops owning the bus in favor of the cpu, this seems to always take 4 cycles
 */
static void Blitter_BusArbitration ( int RequestBusMode )
{
	int	cycles;

	if ( RequestBusMode == BUS_MODE_BLITTER )	/* Bus is requested by the blitter */
	{
//fprintf ( stderr , "blitter start pc %x %x\n" , M68000_GetPC() , M68000_InstrPC );
		if ( ConfigureParams.System.nMachineType == MACHINE_MEGA_STE )
			cycles = 8;			/* MegaSTE blitters need 4 extra cycle when requesting the bus */
		else
			cycles = 4;			/* Default case : take 4 cycles when going from cpu to blitter */

		/* Different timing for some specific cases */

		/* 'Relapse - Graphix Sound 2' by Cybernetics (overscan plasma using blitter) */
		/* $e764 : move.b  d5,(a4) + dbra d1,$fff2 : 4 cycles of the dbra can be executed while blitter starts */
		if ( STMemory_ReadLong ( M68000_InstrPC ) == 0x188551c9 )	/* PC = E764 */
			cycles -= 4;			/* 4 cycles less than default case */
	}

	else						/* Bus is requested by the cpu */
	{
		cycles = 4;				/* Always 4 cycles (even for MegaSTE) */
	}

	/* Add arbitration cycles and update BusMode */
	if ( cycles > 0 )
	{
		Blitter_AddCycles(cycles);
		Blitter_FlushCycles();
	}	
	BusMode = RequestBusMode;
}


/*-----------------------------------------------------------------------*/
/**
 * Read & Write operations
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

	Blitter_AddCycles(4);

	return value;
}

static void Blitter_WriteWord(Uint32 addr, Uint16 value)
{
	/* Call put_word only if the address doesn't point to a bus error region */
	/* (also see SysMem_wput for addr < 0x8) */
	if ( STMemory_CheckRegionBusError ( addr ) == false )
		put_word ( addr , (Uint32)(value) );
//fprintf ( stderr , "write %x %x %x\n" , addr , value , STMemory_CheckRegionBusError(addr) );

	Blitter_AddCycles(4);
}

/*-----------------------------------------------------------------------*/
/**
 * Blitter emulation - level 1
 */

static void Blitter_BeginLine(void)
{
	BlitterVars.src_words = BlitterVars.src_words_reset;
}

static void Blitter_SetState(Uint8 fxsr, Uint8 nfsr, Uint16 end_mask)
{
	BlitterState.end_mask = end_mask;
	BlitterState.have_src = false;
	BlitterState.have_dst = false;
	BlitterState.fxsr = fxsr;
	BlitterState.nfsr = nfsr;
}

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

static Uint16 Blitter_DestRead(void)
{
	if (!BlitterState.have_dst)
	{
		BlitterState.dst_word = Blitter_ReadWord(BlitterRegs.dst_addr);
		BlitterState.have_dst = true;
	}

	return BlitterState.dst_word;
}

/* LOP */

static Uint16 Blitter_LOP_0(void)
{
	return 0;
}

static Uint16 Blitter_LOP_1(void)
{
	return Blitter_ComputeHOP() & Blitter_DestRead();
}

static Uint16 Blitter_LOP_2(void)
{
	return Blitter_ComputeHOP() & ~Blitter_DestRead();
}

static Uint16 Blitter_LOP_3(void)
{
	return Blitter_ComputeHOP();
}

static Uint16 Blitter_LOP_4(void)
{
	return ~Blitter_ComputeHOP() & Blitter_DestRead();
}

static Uint16 Blitter_LOP_5(void)
{
	return Blitter_DestRead();
}

static Uint16 Blitter_LOP_6(void)
{
	return Blitter_ComputeHOP() ^ Blitter_DestRead();
}

static Uint16 Blitter_LOP_7(void)
{
	return Blitter_ComputeHOP() | Blitter_DestRead();
}

static Uint16 Blitter_LOP_8(void)
{
	return ~Blitter_ComputeHOP() & ~Blitter_DestRead();
}

static Uint16 Blitter_LOP_9(void)
{
	return ~Blitter_ComputeHOP() ^ Blitter_DestRead();
}

static Uint16 Blitter_LOP_A(void)
{
	return ~Blitter_DestRead();
}

static Uint16 Blitter_LOP_B(void)
{
	return Blitter_ComputeHOP() | ~Blitter_DestRead();
}

static Uint16 Blitter_LOP_C(void)
{
	return ~Blitter_ComputeHOP();
}

static Uint16 Blitter_LOP_D(void)
{
	return ~Blitter_ComputeHOP() | Blitter_DestRead();
}

static Uint16 Blitter_LOP_E(void)
{
	return ~Blitter_ComputeHOP() | ~Blitter_DestRead();
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

static Uint16 Blitter_ComputeMask(void)
{
	return (Blitter_ComputeLOP() & BlitterState.end_mask) |
			(Blitter_DestRead() & ~BlitterState.end_mask);
}

static void Blitter_ProcessWord(void)
{
	/* when NFSR, a read-modify-write is always performed */
	Uint16 dst_data = ((BlitterState.nfsr || BlitterState.end_mask != 0xFFFF)
							? Blitter_ComputeMask()
							: Blitter_ComputeLOP());

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
 * Note that in non-HOG mode, the blitter only runs for 64 bus cycles (2 MHz!)
 * before giving the bus back to the CPU. Due to this mode, this function must
 * be able to abort and resume the blitting at any time.
 */
static void Blitter_Start(void)
{
	/* select HOP & LOP funcs */
	Blitter_Select_HOP();
	Blitter_Select_LOP();

	/* setup vars */
	BlitterVars.pass_cycles = 0;
	BlitterVars.op_cycles = 0;
	BlitterVars.src_words_reset = BlitterVars.dst_words_reset + BlitterVars.fxsr - BlitterVars.nfsr;

	/* bus arbitration */
	Blitter_BusArbitration ( BUS_MODE_BLITTER );

	/* Busy=1, set line to high/1 and clear interrupt */
	MFP_GPIP_Set_Line_Input ( MFP_GPIP_LINE_GPU_DONE , MFP_GPIP_STATE_HIGH );

	/* Now we enter the main blitting loop */
	do
	{
		Blitter_Step();
		Blitter_FlushCycles();
	}
	while (BlitterRegs.lines > 0
	       && (BlitterVars.hog || BlitterVars.pass_cycles < NONHOG_CYCLES));

	/* bus arbitration */
	Blitter_BusArbitration ( BUS_MODE_CPU );

	BlitterRegs.ctrl = (BlitterRegs.ctrl & 0xF0) | BlitterVars.line;

	if (BlitterRegs.lines == 0)
	{
		/* We're done, clear busy and hog bits */
		BlitterRegs.ctrl &= ~(0x80|0x40);

		/* Busy=0, set line to low/0 and request interrupt */
		MFP_GPIP_Set_Line_Input ( MFP_GPIP_LINE_GPU_DONE , MFP_GPIP_STATE_LOW );
	}
	else
	{
		/* Continue blitting later */
		CycInt_AddRelativeInterrupt(NONHOG_CYCLES, INT_CPU_CYCLE, INTERRUPT_BLITTER);
	}
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
	 * - 0: Blit mode, CPU and Blitter get 64 clockcycles in turns
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
			/* Start blitting after some CPU cycles */
			CycInt_AddRelativeInterrupt((CurrentInstrCycles+WaitStateCycles)>>nCpuFreqShift,
							 INT_CPU_CYCLE, INTERRUPT_BLITTER);
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
