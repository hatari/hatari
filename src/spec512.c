/*
  Hatari

  Handle storing of writes to ST palette using clock-cycle counts. We can use this to accurately any form
  of Spectrum512 style images - even down to the way the screen colours change on decompression routines in menus!

  As the 68000 has a 4-clock cycle increment we can only change palette every 4 cycles. This means that on one
  scanline(512 cycles) we have just 512/4=128 places where palette writes can take place. We keep track of
  this in a table(storing on each scanline and colour writes and the cycles on the scanline where they happen).
  When we draw the screen we simply keep a cycle-count on the line and check this with our table and update
  the 16-colour palette with each change. As the table is already ordered this makes things very simple. Speed
  is a problem, though, as the palette can change once every 4 pixels - that's a lot of processing.
*/

#include "main.h"
#include "debug.h"
#include "decode.h"
#include "int.h"
#include "screen.h"
#include "spec512.h"
#include "video.h"

CYCLEPALETTE CyclePalettes[(SCANLINES_PER_FRAME+1)*MAX_CYCLEPALETTES_PERLINE];  // 314k; 1024-bytes per line
CYCLEPALETTE *pCyclePalette;
int nCyclePalettes[(SCANLINES_PER_FRAME+1)];                  // Number of entries in above table for each scanline
int nPalettesAccess[(SCANLINES_PER_FRAME+1)];                 // Number of times accessed palette register 'x' in this scan line
unsigned short int CycleColour;
int CycleColourIndex;
int nScanLine, ScanLineCycleCount;
BOOL bIsSpec512Display;

//-----------------------------------------------------------------------
/*
  Return TRUE if this frame is a Spectrum 512 style image(MUST be low res/non-mix)
*/
BOOL Spec512_IsImage(void)
{
  // Normal Low res screen?
  if ( (STRes==ST_LOW_RES) && (bIsSpec512Display) )
    return(TRUE);

  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  We store every palette access in a table to perform Spectrum 512 colour effects
  This is cleared on each VBL
*/
void Spec512_StartVBL(void)
{
  // Clear number of cycle palettes on each frame
  memset(nCyclePalettes,0x0,(SCANLINES_PER_FRAME+1)*sizeof(int));
  // Clear number of times accessed on entry in palette(used to check if is true Spectrum 512 image)
  memset(nPalettesAccess,0x0,(SCANLINES_PER_FRAME+1)*sizeof(int));  
  // Set as not Spectrum 512 displayed image
  bIsSpec512Display = FALSE;
}

//-----------------------------------------------------------------------
/*
  Store 'bx' colour into 'CyclePalettes[]' according to cycles into frame
  Address is passed in 'ecx', eg 0xff8240
*/
void Spec512_StoreCyclePalette_Execute(void)
{
  CYCLEPALETTE *pCyclePalette;
  int FrameCycles,ScanLine;

  // Find number of cycles into frame
  FrameCycles = Int_FindFrameCycles();

  // Find scan line we are currently on and get index into cycle-palette table
  ScanLine = (FrameCycles/CYCLES_PER_LINE);
  pCyclePalette = &CyclePalettes[ (ScanLine*MAX_CYCLEPALETTES_PERLINE) + nCyclePalettes[ScanLine] ];
  // Do we have a previous entry at the same cycles? If so, 68000 have used a 'move.l' instruction so stagger writes
  if (nCyclePalettes[ScanLine]>0) {
    if ((pCyclePalette-1)->LineCycles==(FrameCycles&511))
      FrameCycles += 4;              // Colours are staggered by [4,20] when writing a long word!
  }

  // Store palette access
  pCyclePalette->LineCycles = FrameCycles&511;    // Cycles into scanline
  pCyclePalette->Colour = CycleColour&0x777;      // Store ST colour RGB
  pCyclePalette->Index = CycleColourIndex;        // And Index (0...15)
  // Increment count(this can never overflow as you cannot write to the palette more than 'MAX_CYCLEPALETTES_PERLINE' times per scanline)
  nCyclePalettes[ScanLine]++;

  // Check if program wrote to certain palette entry multiple times on a single scan-line
  // If we did then we must be using a Spectrum512 image or some kind of colour cycling...
  nPalettesAccess[ScanLine]++;
  if (nPalettesAccess[ScanLine]>=32)
    bIsSpec512Display = TRUE;
}

void Spec512_StoreCyclePalette(unsigned short col, unsigned long addr)
{
  CycleColour = col;
  CycleColourIndex = (addr-0xff8240)>>1;
  Spec512_StoreCyclePalette_Execute();    /* Store it in table for screen conversion */
}

//-----------------------------------------------------------------------
/*
  Begin palette calculation for Spectrum 512 style images, 
*/
void Spec512_StartFrame(void)
{
  int i;

  // Set so screen gets full-update when returns from Spectrum 512 display
  Screen_SetFullUpdate();

  // Set terminators on each line, so when scan during conversion we know when to stop
  for(i=0; i<(SCANLINES_PER_FRAME+1); i++) {
    pCyclePalette = &CyclePalettes[ (i*MAX_CYCLEPALETTES_PERLINE) + nCyclePalettes[i] ];
    pCyclePalette->LineCycles = -1;          // Term
  }

  // Copy first line palette, kept in 'HBLPalettes' and store to 'STRGBPalette'
  for(i=0; i<16; i++)
    STRGBPalette[i] = ST2RGB[HBLPalettes[i]&0x777];

  // Ready for first call to 'Spec512_ScanLine'
  nScanLine = 0;

  // Skip to first line(where start to draw screen from)
  for (i=0; i<(STScreenStartHorizLine+(nStartHBL-OVERSCAN_TOP)); i++)
    Spec512_ScanWholeLine();
}

//-----------------------------------------------------------------------
/*
  Scan whole line and build up palette - need to do this so when get to screen line we have
  the correct 16 colours set
*/
void Spec512_ScanWholeLine(void)
{
  // Store pointer to line of palette cycle writes
  pCyclePalette = &CyclePalettes[nScanLine*MAX_CYCLEPALETTES_PERLINE];
  // Ready for next scan line
  nScanLine++;

  // Update palette entries until we reach start of displayed screen
  ScanLineCycleCount = 0;
  Spec512_EndScanLine();                // Read whole line of palettes and update 'STRGBPalette'
}

//-----------------------------------------------------------------------
/*
  Build up palette for this scan line and store in 'ScanLinePalettes'
*/
void Spec512_StartScanLine(void)
{
  int i;

  // Store pointer to line of palette cycle writes
  pCyclePalette = &CyclePalettes[nScanLine*MAX_CYCLEPALETTES_PERLINE];
  // Ready for next scan line
  nScanLine++;

  // Update palette entries until we reach start of displayed screen
  ScanLineCycleCount = 0;
  for(i=0; i<((SCREEN_START_CYCLE-8)/4); i++)      // This '8' is as we've already added in the 'move' instruction timing
    Spec512_UpdatePaletteSpan();                   // Update palette for this 4-cycle period
  // And skip for left border is not using overscan display to user
  for (i=0; i<(STScreenLeftSkipBytes/2); i++)      // Eg, 16 bytes = 32 pixels or 8 palette periods
    Spec512_UpdatePaletteSpan();
}

//-----------------------------------------------------------------------
/*
  Run to end of scan line looking up palettes so 'STRGBPalette' is up-to-date
*/
void Spec512_EndScanLine(void)
{
  // Continue to reads palette until complete so have correct version for next line
  while(ScanLineCycleCount<CYCLES_PER_LINE)
    Spec512_UpdatePaletteSpan();
}

//-----------------------------------------------------------------------
/*
  Update palette for 4-pixels span, storing to 'STRGBPalette'
*/
void Spec512_UpdatePaletteSpan(void)
{
/* FIXME!! */
/*
  __asm {
    push  eax
    push  ebx

    mov    ebx,[pCyclePalette]
    mov    eax,[ebx]        // pCyclePalette->LineCycles
    cmp    eax,[ScanLineCycleCount]    // == ScanLineCycleCount?
    jne    no_cycle_match

    // Need to update palette with new entry
    mov    eax,4[ebx]        // pCyclePalette->Colour
    and    eax,0x777        // Colour&0x777
    mov    eax,ST2RGB[eax*4]      // Convert to PC's RGB
    movzx  ebx,WORD PTR 6[ebx]        // pCyclePalette->Index
    shl    ebx,2          // as 'long' index
    add    ebx,OFFSET [STRGBPalette]
    mov    [ebx],eax        // Store as long's for speed

    add    [pCyclePalette],8      // sizeof(CYCLEPALETTE)

no_cycle_match:
    add    [ScanLineCycleCount],4      // Next 4 cycles

    pop    ebx
    pop    eax
    ret
  }
*/
}
