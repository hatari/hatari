/*
  Hatari
*/

#define MAX_CYCLEPALETTES_PERLINE  (512/4)  // As 68000 clock multiple of 4 this mean we can only write to the palette this many time per scanline

// Store writes to palette by cycles per scan line, colour and index in ST
typedef struct {
  int LineCycles;                        // Number of cycles into line(MUST be div by 4)
  unsigned short int Colour;             // ST Colour value
  unsigned short int Index;              // Index into ST palette (0...15)
} CYCLEPALETTE;

extern BOOL Spec512_IsImage(void);
extern void Spec512_StartVBL(void);
extern void Spec512_StoreCyclePalette(unsigned short col, unsigned long addr);
extern void Spec512_StartFrame(void);
extern void Spec512_ScanWholeLine(void);
extern void Spec512_StartScanLine(void);
extern void Spec512_EndScanLine(void);
extern void Spec512_UpdatePaletteSpan(void);
