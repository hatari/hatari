/*
  Hatari - spec512.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

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
const char Spec512_rcsid[] = "Hatari $Id: spec512.c,v 1.18 2007-01-11 23:01:07 thothy Exp $";

#include <SDL_byteorder.h>

#include "main.h"
#include "cycles.h"
#include "int.h"
#include "screen.h"
#include "spec512.h"
#include "video.h"

/* As 68000 clock multiple of 4 this mean we can only write to the palette this many time per scanline */
#define MAX_CYCLEPALETTES_PERLINE  (512/4)

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
static BOOL bIsSpec512Display;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
static const int STRGBPalEndianTable[16] =
{
	0,2,1,3,8,10,9,11,4,6,5,7,12,14,13,15
};
#endif


/*-----------------------------------------------------------------------*/
/*
  Return TRUE if this frame is a Spectrum 512 style image(MUST be low res/non-mix)
*/
BOOL Spec512_IsImage(void)
{
	/* Normal Low res screen? */
	if (STRes == ST_LOW_RES && bIsSpec512Display)
		return TRUE;

	return FALSE;
}


/*-----------------------------------------------------------------------*/
/*
  We store every palette access in a table to perform Spectrum 512 colour effects
  This is cleared on each VBL
*/
void Spec512_StartVBL(void)
{
	/* Clear number of cycle palettes on each frame */
	memset(nCyclePalettes, 0x0, (nScanlinesPerFrame+1)*sizeof(int));

	/* Clear number of times accessed on entry in palette (used to check if is true Spectrum 512 image) */
	nPalettesAccesses = 0x0;

	/* Set as not Spectrum 512 displayed image */
	bIsSpec512Display = FALSE;
}


/*-----------------------------------------------------------------------*/
/*
  Store color into table 'CyclePalettes[]' for screen conversion according
  to cycles into frame.
*/
void Spec512_StoreCyclePalette(Uint16 col, Uint32 addr)
{
	CYCLEPALETTE *pTmpCyclePalette;
	int FrameCycles, ScanLine, nHorPos;

	CycleColour = col;
	CycleColourIndex = (addr-0xff8240)>>1;

	/* Find number of cycles into frame */
	FrameCycles = Cycles_GetCounterOnWriteAccess(CYCLES_COUNTER_VIDEO);

	/* Find scan line we are currently on and get index into cycle-palette table */
	ScanLine = FrameCycles / nCyclesPerLine;
	pTmpCyclePalette = &CyclePalettes[ (ScanLine*MAX_CYCLEPALETTES_PERLINE) + nCyclePalettes[ScanLine] ];

	nHorPos = FrameCycles % nCyclesPerLine;

	/* Temporary hack until we've got better CPU cycles emulation:
	 * The position needs to be corrected for a few positions.
	 * I think it might be related to the shifter which handles 16 pixels
	 * at a time / plane, since here we arrive every 20 cycles there are
	 * synchronizations problems. Now the very good question is : why does
	 * it happen only for the left part of the screen ???!!!!!
	 * If we add the same correction for the right part, then bad black pixels
	 * appear. This definetely requires some investigation... */
#if 0
	if (nHorPos == 104 || nHorPos == 124 || nHorPos == 144)
		nHorPos += 4;
#endif

	/* Do we have a previous entry at the same cycles? If so, 68000 have used a 'move.l' instruction so stagger writes */
	if (nCyclePalettes[ScanLine] > 0)
	{
		/* In case the ST uses a move.l or a movem.l to update colors, we need
		 * to add at least 4 cycles between each color: */
		if ((pTmpCyclePalette-1)->LineCycles >= nHorPos)
			nHorPos = (pTmpCyclePalette-1)->LineCycles + 4;
	}

	/* Store palette access */
	pTmpCyclePalette->LineCycles = nHorPos;           /* Cycles into scanline */
	pTmpCyclePalette->Colour = CycleColour;           /* Store ST/STe color RGB */
	pTmpCyclePalette->Index = CycleColourIndex;       /* And index (0...15) */

	/* Increment count (this can never overflow as you cannot write to the palette more than 'MAX_CYCLEPALETTES_PERLINE' times per scanline) */
	nCyclePalettes[ScanLine]++;

	/* Check if program wrote to palette registers multiple times on a frame. */
	/* If so it must be using a spec512 image or some kind of color cycling. */
	nPalettesAccesses++;
	if (nPalettesAccesses >= 128)
	{
		bIsSpec512Display = TRUE;
	}
}


/*-----------------------------------------------------------------------*/
/*
  Begin palette calculation for Spectrum 512 style images,
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
/*
  Scan whole line and build up palette - need to do this so when get to screen line we have
  the correct 16 colours set
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
/*
  Build up palette for this scan line and store in 'ScanLinePalettes'
*/
void Spec512_StartScanLine(void)
{
	int i;

	/* Store pointer to line of palette cycle writes */
	pCyclePalette = &CyclePalettes[nScanLine*MAX_CYCLEPALETTES_PERLINE];
	/* Ready for next scan line */
	nScanLine++;

	/* Update palette entries until we reach start of displayed screen */
	ScanLineCycleCount = 0;
	for(i=0; i<((SCREEN_START_CYCLE-16)/4); i++)  /* This '16' is as we've already added in the 'move' instruction timing */
		Spec512_UpdatePaletteSpan();              /* Update palette for this 4-cycle period */

	/* And skip for left border is not using overscan display to user */
	for (i=0; i<(STScreenLeftSkipBytes/2); i++)   /* Eg, 16 bytes = 32 pixels or 8 palette periods */
		Spec512_UpdatePaletteSpan();
}


/*-----------------------------------------------------------------------*/
/*
  Run to end of scan line looking up palettes so 'STRGBPalette' is up-to-date
*/
void Spec512_EndScanLine(void)
{
	/* Continue to reads palette until complete so have correct version for next line */
	while (ScanLineCycleCount < nCyclesPerLine)
		Spec512_UpdatePaletteSpan();
}


/*-----------------------------------------------------------------------*/
/*
  Update palette for 4-pixels span, storing to 'STRGBPalette'
*/
void Spec512_UpdatePaletteSpan(void)
{
	if( pCyclePalette->LineCycles == ScanLineCycleCount )
	{
		/* Need to update palette with new entry */
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
		STRGBPalette[STRGBPalEndianTable[pCyclePalette->Index]] = ST2RGB[pCyclePalette->Colour];
#else
		STRGBPalette[pCyclePalette->Index] = ST2RGB[pCyclePalette->Colour];
#endif

		pCyclePalette += 1;
	}
	ScanLineCycleCount += 4;      /* Next 4 cycles */
}
