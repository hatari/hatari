/*
  Hatari - spec512.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Handle storing of writes to ST palette using clock-cycle counts. We can use
  this to accurately any form of Spectrum512 style images - even down to the
  way the screen colours change on decompression routines in menus!

  As the 68000 has a 4-clock cycle increment we can only change palette every
  4 cycles. This means that on one scanline (512 cycles in 50Hz) we have just
  512/4=128 places where palette writes can take place. We keep track of this
  in a table (storing on each scanline and color writes and the cycles on the
  scanline where they happen). When we draw the screen we simply keep a
  cycle-count on the line and check this with our table and update the 16-color
  palette with each change. As the table is already ordered this makes things
  very simple. Speed is a problem, though, as the palette can change once every
  4 pixels - that's a lot of processing.
*/


/* 2008/01/12	[NP]	In Spec512_StoreCyclePalette, don't use Cycles_GetCounterOnWriteAccess	*/
/*			as it doesn't support movem for example. Use Cycles_GetCounter with	*/
/*			an average  correction of 8 cycles (this should be fixed).		*/
/*			In Spec512_StartScanLine, better handling of SCREENBYTES_LEFT when	*/
/*			border are present. Better alignement of pixel and color when left	*/
/*			border is removed (Rotofull in Nostalgia Demo, When Colors Are Going	*/
/*			Bang Bang by DF in Punish Your Machine).				*/
/* 2008/01/13	[NP]	In Spec512_StoreCyclePalette, if a movem was used to access several	*/
/*			color regs just before the end of a line, the value for nHorPos was not	*/
/*			checked and could take values >= 512, which means some colors in the	*/
/*			palette were overwritten next time colors were stored on the next line	*/
/*			and the palette was not set correctly, especially in the bottom of the	*/
/*			screen (Decade Demo Main Menu, Punish Your Machine Main Menu, ULM	*/
/*			Hidden Screen in Oh Crickey...).					*/
/* 2008/01/13	[NP]	One line should contain 512/4 + 1 colors slots, not 512/4. Else, if	*/
/*			a program manages to change colors every 4 cycles, the '-1' terminator	*/
/*			added by Spec512_StartFrame will in fact be written on cycle 0 in the	*/
/*			next line (this is theorical, practically no program changes the color	*/
/*			128 times per line).							*/
/*			Use nCyclesPerLine instead of 512 to check if nHorPos is too big and if	*/
/*			the colors must be stored on the next line when freq is 50 or 60Hz	*/
/*			(readme.prg by TEX (in 1987))						*/
/* 2008/01/24	[NP]	In Spec512_StartScanLine, use different values for LineStartCycle when	*/
/*			running in 50 Hz or 60 Hz (TEX Spectrum Slideshow in 60 Hz).		*/
/* 2008/12/14	[NP]	In Spec512_StoreCyclePalette, instead of approximating write position	*/
/*			by Cycles_GetCounter+8, we use different cases for movem, .l acces and	*/
/*			.w acces (similar to cycles.c). This gives correct results when using	*/
/*			"move.w d0,(a0) or move.w (a0)+,(a1)+" for example, which were shifted	*/
/*			8 or 4 pixels too late. Calibration was made using a custom program to	*/
/*			compare the results with a real STF in different cases (fix Froggies	*/
/*			Over The Fence Main Menu).						*/
/* 2008/12/21	[NP]	Use BusMode to adjust Cycles_GetCounterOnReadAccess and			*/
/*			Cycles_GetCounterOnWriteAccess depending on who is owning the		*/
/*			bus (cpu, blitter).							*/
/* 2009/07/28	[NP]	Use different timings for movem.l and movem.w				*/
/* 2014/01/02	[NP]	In Spec512_StoreCyclePalette, write occurs during the last cycles for	*/
/*			i_ADD and i_SUB (fix 'add d1,(a0)' in '4-pixel plasma' by TOS Crew).	*/


const char Spec512_fileid[] = "Hatari spec512.c : " __DATE__ " " __TIME__;

#include <SDL_endian.h>

#include "main.h"
#include "cycles.h"
#include "cycInt.h"
#include "m68000.h"
#include "ioMem.h"
#include "screen.h"
#include "spec512.h"
#include "video.h"
#include "configuration.h"


/* As 68000 clock multiple of 4 this mean we can only write to the palette this many time per scanline */
#define MAX_CYCLEPALETTES_PERLINE  ((512/4) + 1)		/* +1 for the '-1' added as a line's terminator */

/* Store writes to palette by cycles per scan line, colour and index in ST */
typedef struct
{
	int LineCycles;       /* Number of cycles into line (MUST be div by 4) */
	Uint16 Colour;        /* ST Colour value */
	Uint16 Index;         /* Index into ST palette (0...15) */
}
CYCLEPALETTE;

/* 314k; 1024-bytes per line */
static CYCLEPALETTE CyclePalettes[(MAX_SCANLINES_PER_FRAME+1)*MAX_CYCLEPALETTES_PERLINE];
static CYCLEPALETTE *pCyclePalette;
static int nCyclePalettes[(MAX_SCANLINES_PER_FRAME+1)];  /* Number of entries in above table for each scanline */
static int nPalettesAccesses;   /* Number of times accessed palette registers */
static Uint16 CycleColour;
static int CycleColourIndex;
static int nScanLine, ScanLineCycleCount;
static bool bIsSpec512Display;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
static const int STRGBPalEndianTable[16] =
{
	0,2,1,3,8,10,9,11,4,6,5,7,12,14,13,15
};
#endif


/*-----------------------------------------------------------------------*/
/**
 * Return true if this frame is a Spectrum 512 style image (can be low/med
 * res mix).
 */
bool Spec512_IsImage(void)
{
	/* Spec512 mode was triggered in low or med res ? */
	if (bIsSpec512Display)
		return true;

	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * We store every palette access in a table to perform Spectrum 512 color
 * effects. This is cleared on each VBL.
 */
void Spec512_StartVBL(void)
{
	/* Clear number of cycle palettes on each frame */
	memset(nCyclePalettes, 0x0, sizeof(nCyclePalettes));

	/* Clear number of times accessed on entry in palette (used to check if
	 * it is true Spectrum 512 image) */
	nPalettesAccesses = 0;

	/* Set as not Spectrum 512 displayed image */
	bIsSpec512Display = false;
}


/*-----------------------------------------------------------------------*/
/**
 * Store color into table 'CyclePalettes[]' for screen conversion according
 * to cycles into frame.
 */
void Spec512_StoreCyclePalette(Uint16 col, Uint32 addr)
{
	CYCLEPALETTE *pTmpCyclePalette;
	int FrameCycles, ScanLine, nHorPos;

	if (!ConfigureParams.Screen.nSpec512Threshold)
		return;

	CycleColour = col;
	CycleColourIndex = (addr-0xff8240)>>1;

	/* Find number of cycles into frame */
	/* FIXME [NP] We should use Cycles_GetCounterOnWriteAccess, but it wouldn't	*/
	/* work when using multiple accesses instructions like move.l or movem	*/
	/* To correct this, assume a delay of 8 cycles (should give a good approximation */
	/* of a move.w or movem.l for example) */
	//  FrameCycles = Cycles_GetCounterOnWriteAccess(CYCLES_COUNTER_VIDEO);
	if ( BusMode == BUS_MODE_BLITTER )
	{
		FrameCycles = Cycles_GetCounterOnWriteAccess(CYCLES_COUNTER_VIDEO);
	}
	else							/* BUS_MODE_CPU */
	{
#ifdef OLD_CYC_PAL
		FrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO) + 8;
#else
		if ( OpcodeFamily == i_MVMLE )
		{
//			FrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO) + 8;
			FrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO)
			              + (CurrentInstrCycles & ~3);
			if (nIoMemAccessSize == SIZE_LONG)	/* long access */
				FrameCycles -= 0;
			else					/* word access */
				FrameCycles -= 4;
		}
		else if ( ( OpcodeFamily == i_ADD ) || ( OpcodeFamily == i_SUB ) )
		{
			FrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO)
			              + (CurrentInstrCycles & ~3);
			FrameCycles -= 0;			/* write is made at the end, after prefetch */
		}
		else						/* default case write, then prefetch (mostly for 'move') */
		{
			FrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO)
			              + (CurrentInstrCycles & ~3);
			if (nIoMemAccessSize == SIZE_LONG)	/* long access */
				FrameCycles -= 8;
			else					/* word/byte access */
				FrameCycles -= 4;
		}
#endif
	}


	/* Find scan line we are currently on and get index into cycle-palette table */
	Video_ConvertPosition ( FrameCycles , &ScanLine , &nHorPos );	

	if (ScanLine > MAX_SCANLINES_PER_FRAME)
		return;

	pTmpCyclePalette = &CyclePalettes[ (ScanLine*MAX_CYCLEPALETTES_PERLINE) + nCyclePalettes[ScanLine] ];


	/* Do we have a previous entry at the same cycles? If so, 68000 have used a 'move.l' instruction so stagger writes */
	if (nCyclePalettes[ScanLine] > 0)
	{
		/* In case the ST uses a move.l or a movem.l to update colors, we need
		 * to add at least 4 cycles between each color: */
		if ((pTmpCyclePalette-1)->LineCycles >= nHorPos)
			nHorPos = (pTmpCyclePalette-1)->LineCycles + 4;

		if ( nHorPos >= nCyclesPerLine )	/* end of line reached, continue on the next line */
		{
			ScanLine++;
			pTmpCyclePalette = &CyclePalettes[ (ScanLine*MAX_CYCLEPALETTES_PERLINE) + nCyclePalettes[ScanLine] ];
			nHorPos = nCyclePalettes[ScanLine] * 4;	/* 4 cycles per access */
		}
	}

	/* Store palette access */
	pTmpCyclePalette->LineCycles = nHorPos;           /* Cycles into scanline */
	pTmpCyclePalette->Colour = CycleColour;           /* Store ST/STe color RGB */
	pTmpCyclePalette->Index = CycleColourIndex;       /* And index (0...15) */

	if ( 0 && LOG_TRACE_LEVEL(TRACE_VIDEO_COLOR))
	{
		int FrameCycles, HblCounterVideo, LineCycles;

		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("spec store col line %d cyc=%d col=%x idx=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d\n",
				ScanLine, nHorPos, CycleColour, CycleColourIndex, FrameCycles,
				LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}

	/* Increment count (this can never overflow as you cannot write to the palette more than 'MAX_CYCLEPALETTES_PERLINE-1' times per scanline) */
	nCyclePalettes[ScanLine]++;

	/* Check if program wrote to palette registers multiple times on a frame. */
	/* If so it must be using a spec512 image or some kind of color cycling. */
	nPalettesAccesses++;
	if (nPalettesAccesses >= ConfigureParams.Screen.nSpec512Threshold)
	{
		bIsSpec512Display = true;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Begin palette calculation for Spectrum 512 style images,
 */
void Spec512_StartFrame(void)
{
	int i;

	/* Set so screen gets full-update when returns from Spectrum 512 display */
	Screen_SetFullUpdate();

	/* Set terminators on each line, so when scan during conversion we know when to stop */
	for (i = 0; i < (nScanlinesPerFrame+1); i++)
	{
		pCyclePalette = &CyclePalettes[ (i*MAX_CYCLEPALETTES_PERLINE) + nCyclePalettes[i] ];
		pCyclePalette->LineCycles = -1;          /* Term */
	}

       /* Copy first line palette, kept in 'HBLPalettes' and store to 'STRGBPalette' */
       for (i = 0; i < 16; i++)
       {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
               STRGBPalette[STRGBPalEndianTable[i]] = ST2RGB[pHBLPalettes[i]];
#else
               STRGBPalette[i] = ST2RGB[pHBLPalettes[i]];
#endif
       }

	/* Ready for first call to 'Spec512_ScanLine' */
	nScanLine = 0;
	if (OverscanMode & OVERSCANMODE_TOP)
		nScanLine += OVERSCAN_TOP;

	/* Skip to first line(where start to draw screen from) */
	for (i = 0; i < (STScreenStartHorizLine+(nStartHBL-OVERSCAN_TOP)); i++)
		Spec512_ScanWholeLine();
}


/*-----------------------------------------------------------------------*/
/**
 * Scan whole line and build up palette - need to do this so when get to screen line we have
 * the correct 16 colours set
 */
void Spec512_ScanWholeLine(void)
{
	/* Store pointer to line of palette cycle writes */
	pCyclePalette = &CyclePalettes[nScanLine*MAX_CYCLEPALETTES_PERLINE];
	/* Ready for next scan line */
	nScanLine++;

	/* Update palette entries until we reach start of displayed screen */
	ScanLineCycleCount = 0;
	Spec512_EndScanLine();        /* Read whole line of palettes and update 'STRGBPalette' */
}


/*-----------------------------------------------------------------------*/
/**
 * Build up palette for this scan line and store in 'ScanLinePalettes'
 */
void Spec512_StartScanLine(void)
{
	int i;
	int LineStartCycle;

	/* Store pointer to line of palette cycle writes */
	pCyclePalette = &CyclePalettes[nScanLine*MAX_CYCLEPALETTES_PERLINE];
	/* Ready for next scan line */
	nScanLine++;

	if ( nScanlinesPerFrame == SCANLINES_PER_FRAME_50HZ )
		LineStartCycle = LINE_START_CYCLE_50;			/* The screen was 50 Hz */
	else
		LineStartCycle = LINE_START_CYCLE_60;			/* The screen was 60 Hz */

	/* Update palette entries until we reach start of displayed screen */
	ScanLineCycleCount = 0;
//	for(i=0; i<((SCREEN_START_CYCLE-16)/4); i++)  /* This '16' is as we've already added in the 'move' instruction timing */
#ifdef OLD_CYC_PAL
	for (i=0; i<((LineStartCycle-SCREENBYTES_LEFT*2)/4 + 6); i++)	/* [NP] '6' is required to align pixels and colors */
#else
	for (i=0; i<((LineStartCycle-SCREENBYTES_LEFT*2)/4 + 7); i++)	/* [NP] '7' is required to align pixels and colors */
#endif
		Spec512_UpdatePaletteSpan();              /* Update palette for this 4-cycle period */

	/* And skip for left border is not using overscan display to user */
	for (i=0; i<(STScreenLeftSkipBytes/2); i++)   /* Eg, 16 bytes = 32 pixels or 8 palette periods */
		Spec512_UpdatePaletteSpan();
}


/*-----------------------------------------------------------------------*/
/**
 * Run to end of scan line looking up palettes so 'STRGBPalette' is up-to-date
 */
void Spec512_EndScanLine(void)
{
	/* Continue to reads palette until complete so have correct version for next line */
	while (ScanLineCycleCount < nCyclesPerLine)
		Spec512_UpdatePaletteSpan();
}


/*-----------------------------------------------------------------------*/
/**
 * Update palette for 4-pixels span, storing to 'STRGBPalette'
 */
void Spec512_UpdatePaletteSpan(void)
{
	if (pCyclePalette->LineCycles == ScanLineCycleCount)
	{
		/* Need to update palette with new entry */
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
		STRGBPalette[STRGBPalEndianTable[pCyclePalette->Index]] = ST2RGB[pCyclePalette->Colour];
#else
		STRGBPalette[pCyclePalette->Index] = ST2RGB[pCyclePalette->Colour];
//fprintf ( stderr , "upd spec cyc %d %x %x\n" , ScanLineCycleCount , pCyclePalette->Index , pCyclePalette->Colour );
#endif
		pCyclePalette += 1;
	}
	ScanLineCycleCount += 4;      /* Next 4 cycles */
}
