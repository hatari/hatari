/*
  Hatari

  Screen conversion routines. We have a number of routines to convert ST screen to PC format.
  We split these into Low,Medium and High each with 8/16-bit versions. To gain extra speed,
  as almost half of the processing time can be spent in these routines, we check for any
  changes from the previously displayed frame. AdjustLinePaletteRemap() sets a flag to
  tell the routines if we need to totally update a line(ie full update, or palette/res change)
  or if we just can do a difference check. To see how much of the screen updates each frame, simply
  enable 'TEST_SCREEN_UPDATE'
  We convert each screen 16 pixels at a time by use of a couple of look-up tables. These tables
  convert from 2-plane format to bbp and then we can add two of these together to get 4-planes. This
  keeps the tables small and thus improves speed. We then look these bbp values up as an RGB/Index value
  to copy to the screen.
*/

#include <SDL.h>

#include "main.h"
#include "screen.h"
#include "screenConvert.h"
#include "spec512.h"
#include "vdi.h"
#include "video.h"

/*#define  TEST_SCREEN_UPDATE*/  /* Enable to see partial screen update */

int ScrX,ScrY;                   /* Locals */
int ScrUpdateFlag;               /* Bit mask of how to update screen */
BOOL bScrDoubleY;                /* TRUE if double on Y */
Uint32 PixelWorkspace[4];        /* Workspace to store pixels to so can print in right order for Spec512 */

/* Remap tables to convert from plane format to byte-per-pixel (Upper is for 4-Planes so if shifted by 2) */
Uint32 Remap_2_Planes[256] = {
  0x00000000,  0x01000000,  0x00010000,  0x01010000,  0x00000100,  0x01000100,  0x00010100,  0x01010100,
  0x00000001,  0x01000001,  0x00010001,  0x01010001,  0x00000101,  0x01000101,  0x00010101,  0x01010101,
  0x02000000,  0x03000000,  0x02010000,  0x03010000,  0x02000100,  0x03000100,  0x02010100,  0x03010100,
  0x02000001,  0x03000001,  0x02010001,  0x03010001,  0x02000101,  0x03000101,  0x02010101,  0x03010101,
  0x00020000,  0x01020000,  0x00030000,  0x01030000,  0x00020100,  0x01020100,  0x00030100,  0x01030100,
  0x00020001,  0x01020001,  0x00030001,  0x01030001,  0x00020101,  0x01020101,  0x00030101,  0x01030101,
  0x02020000,  0x03020000,  0x02030000,  0x03030000,  0x02020100,  0x03020100,  0x02030100,  0x03030100,
  0x02020001,  0x03020001,  0x02030001,  0x03030001,  0x02020101,  0x03020101,  0x02030101,  0x03030101,
  0x00000200,  0x01000200,  0x00010200,  0x01010200,  0x00000300,  0x01000300,  0x00010300,  0x01010300,
  0x00000201,  0x01000201,  0x00010201,  0x01010201,  0x00000301,  0x01000301,  0x00010301,  0x01010301,
  0x02000200,  0x03000200,  0x02010200,  0x03010200,  0x02000300,  0x03000300,  0x02010300,  0x03010300,
  0x02000201,  0x03000201,  0x02010201,  0x03010201,  0x02000301,  0x03000301,  0x02010301,  0x03010301,
  0x00020200,  0x01020200,  0x00030200,  0x01030200,  0x00020300,  0x01020300,  0x00030300,  0x01030300,
  0x00020201,  0x01020201,  0x00030201,  0x01030201,  0x00020301,  0x01020301,  0x00030301,  0x01030301,
  0x02020200,  0x03020200,  0x02030200,  0x03030200,  0x02020300,  0x03020300,  0x02030300,  0x03030300,
  0x02020201,  0x03020201,  0x02030201,  0x03030201,  0x02020301,  0x03020301,  0x02030301,  0x03030301,
  0x00000002,  0x01000002,  0x00010002,  0x01010002,  0x00000102,  0x01000102,  0x00010102,  0x01010102,
  0x00000003,  0x01000003,  0x00010003,  0x01010003,  0x00000103,  0x01000103,  0x00010103,  0x01010103,
  0x02000002,  0x03000002,  0x02010002,  0x03010002,  0x02000102,  0x03000102,  0x02010102,  0x03010102,
  0x02000003,  0x03000003,  0x02010003,  0x03010003,  0x02000103,  0x03000103,  0x02010103,  0x03010103,
  0x00020002,  0x01020002,  0x00030002,  0x01030002,  0x00020102,  0x01020102,  0x00030102,  0x01030102,
  0x00020003,  0x01020003,  0x00030003,  0x01030003,  0x00020103,  0x01020103,  0x00030103,  0x01030103,
  0x02020002,  0x03020002,  0x02030002,  0x03030002,  0x02020102,  0x03020102,  0x02030102,  0x03030102,
  0x02020003,  0x03020003,  0x02030003,  0x03030003,  0x02020103,  0x03020103,  0x02030103,  0x03030103,
  0x00000202,  0x01000202,  0x00010202,  0x01010202,  0x00000302,  0x01000302,  0x00010302,  0x01010302,
  0x00000203,  0x01000203,  0x00010203,  0x01010203,  0x00000303,  0x01000303,  0x00010303,  0x01010303,
  0x02000202,  0x03000202,  0x02010202,  0x03010202,  0x02000302,  0x03000302,  0x02010302,  0x03010302,
  0x02000203,  0x03000203,  0x02010203,  0x03010203,  0x02000303,  0x03000303,  0x02010303,  0x03010303,
  0x00020202,  0x01020202,  0x00030202,  0x01030202,  0x00020302,  0x01020302,  0x00030302,  0x01030302,
  0x00020203,  0x01020203,  0x00030203,  0x01030203,  0x00020303,  0x01020303,  0x00030303,  0x01030303,
  0x02020202,  0x03020202,  0x02030202,  0x03030202,  0x02020302,  0x03020302,  0x02030302,  0x03030302,
  0x02020203,  0x03020203,  0x02030203,  0x03030203,  0x02020303,  0x03020303,  0x02030303,  0x03030303,
};

Uint32 Remap_2_Planes_Upper[256] = {
  0x00000000,  0x04000000,  0x00040000,  0x04040000,  0x00000400,  0x04000400,  0x00040400,  0x04040400,
  0x00000004,  0x04000004,  0x00040004,  0x04040004,  0x00000404,  0x04000404,  0x00040404,  0x04040404,
  0x08000000,  0x0C000000,  0x08040000,  0x0C040000,  0x08000400,  0x0C000400,  0x08040400,  0x0C040400,
  0x08000004,  0x0C000004,  0x08040004,  0x0C040004,  0x08000404,  0x0C000404,  0x08040404,  0x0C040404,
  0x00080000,  0x04080000,  0x000C0000,  0x040C0000,  0x00080400,  0x04080400,  0x000C0400,  0x040C0400,
  0x00080004,  0x04080004,  0x000C0004,  0x040C0004,  0x00080404,  0x04080404,  0x000C0404,  0x040C0404,
  0x08080000,  0x0C080000,  0x080C0000,  0x0C0C0000,  0x08080400,  0x0C080400,  0x080C0400,  0x0C0C0400,
  0x08080004,  0x0C080004,  0x080C0004,  0x0C0C0004,  0x08080404,  0x0C080404,  0x080C0404,  0x0C0C0404,
  0x00000800,  0x04000800,  0x00040800,  0x04040800,  0x00000C00,  0x04000C00,  0x00040C00,  0x04040C00,
  0x00000804,  0x04000804,  0x00040804,  0x04040804,  0x00000C04,  0x04000C04,  0x00040C04,  0x04040C04,
  0x08000800,  0x0C000800,  0x08040800,  0x0C040800,  0x08000C00,  0x0C000C00,  0x08040C00,  0x0C040C00,
  0x08000804,  0x0C000804,  0x08040804,  0x0C040804,  0x08000C04,  0x0C000C04,  0x08040C04,  0x0C040C04,
  0x00080800,  0x04080800,  0x000C0800,  0x040C0800,  0x00080C00,  0x04080C00,  0x000C0C00,  0x040C0C00,
  0x00080804,  0x04080804,  0x000C0804,  0x040C0804,  0x00080C04,  0x04080C04,  0x000C0C04,  0x040C0C04,
  0x08080800,  0x0C080800,  0x080C0800,  0x0C0C0800,  0x08080C00,  0x0C080C00,  0x080C0C00,  0x0C0C0C00,
  0x08080804,  0x0C080804,  0x080C0804,  0x0C0C0804,  0x08080C04,  0x0C080C04,  0x080C0C04,  0x0C0C0C04,
  0x00000008,  0x04000008,  0x00040008,  0x04040008,  0x00000408,  0x04000408,  0x00040408,  0x04040408,
  0x0000000C,  0x0400000C,  0x0004000C,  0x0404000C,  0x0000040C,  0x0400040C,  0x0004040C,  0x0404040C,
  0x08000008,  0x0C000008,  0x08040008,  0x0C040008,  0x08000408,  0x0C000408,  0x08040408,  0x0C040408,
  0x0800000C,  0x0C00000C,  0x0804000C,  0x0C04000C,  0x0800040C,  0x0C00040C,  0x0804040C,  0x0C04040C,
  0x00080008,  0x04080008,  0x000C0008,  0x040C0008,  0x00080408,  0x04080408,  0x000C0408,  0x040C0408,
  0x0008000C,  0x0408000C,  0x000C000C,  0x040C000C,  0x0008040C,  0x0408040C,  0x000C040C,  0x040C040C,
  0x08080008,  0x0C080008,  0x080C0008,  0x0C0C0008,  0x08080408,  0x0C080408,  0x080C0408,  0x0C0C0408,
  0x0808000C,  0x0C08000C,  0x080C000C,  0x0C0C000C,  0x0808040C,  0x0C08040C,  0x080C040C,  0x0C0C040C,
  0x00000808,  0x04000808,  0x00040808,  0x04040808,  0x00000C08,  0x04000C08,  0x00040C08,  0x04040C08,
  0x0000080C,  0x0400080C,  0x0004080C,  0x0404080C,  0x00000C0C,  0x04000C0C,  0x00040C0C,  0x04040C0C,
  0x08000808,  0x0C000808,  0x08040808,  0x0C040808,  0x08000C08,  0x0C000C08,  0x08040C08,  0x0C040C08,
  0x0800080C,  0x0C00080C,  0x0804080C,  0x0C04080C,  0x08000C0C,  0x0C000C0C,  0x08040C0C,  0x0C040C0C,
  0x00080808,  0x04080808,  0x000C0808,  0x040C0808,  0x00080C08,  0x04080C08,  0x000C0C08,  0x040C0C08,
  0x0008080C,  0x0408080C,  0x000C080C,  0x040C080C,  0x00080C0C,  0x04080C0C,  0x000C0C0C,  0x040C0C0C,
  0x08080808,  0x0C080808,  0x080C0808,  0x0C0C0808,  0x08080C08,  0x0C080C08,  0x080C0C08,  0x0C0C0C08,
  0x0808080C,  0x0C08080C,  0x080C080C,  0x0C0C080C,  0x08080C0C,  0x0C080C0C,  0x080C0C0C,  0x0C0C0C0C,
};

Uint32 Remap_1_Plane[16] = {
  0x00000000+BASECOLOUR_LONG,  0x01000000+BASECOLOUR_LONG,  0x00010000+BASECOLOUR_LONG,  0x01010000+BASECOLOUR_LONG,  0x00000100+BASECOLOUR_LONG,  0x01000100+BASECOLOUR_LONG,  0x00010100+BASECOLOUR_LONG,  0x01010100+BASECOLOUR_LONG,
  0x00000001+BASECOLOUR_LONG,  0x01000001+BASECOLOUR_LONG,  0x00010001+BASECOLOUR_LONG,  0x01010001+BASECOLOUR_LONG,  0x00000101+BASECOLOUR_LONG,  0x01000101+BASECOLOUR_LONG,  0x00010101+BASECOLOUR_LONG,  0x01010101+BASECOLOUR_LONG,
};

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
#define LSWAP(x) (((x&0x000000ff)<<24)| \
                 ((x&0x0000ff00)<<8)|  \
                 ((x&0x00ff0000)>>8)|  \
                 ((x&0xff000000)>>24))
#else
#define LSWAP(x) (x)
#endif

/*-----------------------------------------------------------------------*/
/*
  Update the STRGBPalette[] array with current colours for this raster line.

  Return 'ScrUpdateFlag', 0x80000000=Full update, 0x40000000=Update as palette changed
*/
int AdjustLinePaletteRemap(void)
{
  unsigned short *actHBLPal;
  static int endiantable[16] = {0,2,1,3,8,10,9,11,4,6,5,7,12,14,13,15};
  int i;
  int v;
  /* Copy palette and convert to RGB in display format */
  actHBLPal = pHBLPalettes + (ScrY<<4);    /* offset in palette */
  for(i=0; i<16; i++)
   {
    v=*actHBLPal;
    actHBLPal+=1;
    v=v&0x777;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    STRGBPalette[endiantable[i]] = ST2RGB[v];
#else
    STRGBPalette[i] = ST2RGB[v];
#endif
   }
  ScrUpdateFlag = HBLPaletteMasks[ScrY];
  return ScrUpdateFlag;
}


/*-----------------------------------------------------------------------*/
/*
  Run updates to palette(STRGBPalette[]) until get to screen line we are to convert from
*/
void Convert_StartFrame(void)
{
 int ecx;
 ecx=STScreenStartHorizLine;            /* Get #lines before conversion starts */
 if( ecx==0 )  return;
 ScrY=0;
 do
  {
   AdjustLinePaletteRemap();            /* Update palette */
   ++ScrY;
   --ecx;
  }
 while( ecx );
}





#define LOW_BUILD_PIXELS_0 \
{ \
 ebx &= 0x0f0f0f0f; \
 ecx &= 0x0f0f0f0f; \
 eax = ebx >> 12; \
 eax |= ebx; \
 edx = ecx >> 12; \
 edx |= ecx; \
 ebx = edx; \
 ebx &= 0x00ff; \
 ecx = Remap_2_Planes_Upper[ebx]; \
 ebx = eax; \
 ebx &= 0x00ff; \
 ecx += Remap_2_Planes[ebx]; \
}

#define LOW_BUILD_PIXELS_1 \
{ \
 ebx = edx; \
 ebx = ebx >> 8; \
 ebx &= 0x00ff; \
 ecx = Remap_2_Planes_Upper[ebx]; \
 ebx = eax; \
 ebx = ebx >> 8; \
 ebx &= 0x00ff; \
 ecx += Remap_2_Planes[ebx]; \
}

#define LOW_BUILD_PIXELS_2 \
{ \
 ebx = *edi; \
 ecx = *(edi+1); \
 ebx &= 0xf0f0f0f0; \
 ecx &= 0xf0f0f0f0; \
 ebx = ebx >> 4; \
 eax = ebx; \
 ebx = ebx >> 12; \
 eax |= ebx; \
 ecx = ecx >> 4; \
 edx = ecx; \
 ecx = ecx >> 12; \
 edx |= ecx; \
 ebx = edx; \
 ebx &= 0x00ff; \
 ecx = Remap_2_Planes_Upper[ebx]; \
 ebx = eax; \
 ebx &= 0x00ff; \
 ecx += Remap_2_Planes[ebx]; \
}

#define LOW_BUILD_PIXELS_3 \
{ \
 ebx = edx; \
 ebx = ebx >> 8; \
 ebx &= 0x00ff; \
 ecx = Remap_2_Planes_Upper[ebx]; \
 ebx = eax; \
 ebx = ebx >> 8; \
 ebx &= 0x00ff; \
 ecx += Remap_2_Planes[ebx]; \
}


#define MED_BUILD_PIXELS_0 \
{ \
 ebx &= 0x0f0f0f0f; \
 eax = ebx; \
 eax >>= 12; \
 eax |= ebx; \
 ebx = eax; \
 ebx &= 0x000000ff; \
 ecx = Remap_2_Planes[ebx]; \
}

#define MED_BUILD_PIXELS_1 \
{ \
 ebx = eax; \
 ebx >>=8; \
 ebx &= 0x000000ff; \
 ecx = Remap_2_Planes[ebx]; \
}

#define MED_BUILD_PIXELS_2 \
{ \
 ebx = *edi; \
 ebx &= 0xf0f0f0f0; \
 ebx >>= 4; \
 eax = ebx; \
 ebx >>= 12; \
 eax |= ebx; \
 ebx = eax; \
 ebx &= 0x000000ff; \
 ecx = Remap_2_Planes[ebx]; \
}

#define MED_BUILD_PIXELS_3 \
{ \
 ebx = eax; \
 ebx >>= 8; \
 ebx &= 0x000000ff; \
 ecx = Remap_2_Planes[ebx]; \
}


/* Routines to create 'ecx' pixels - MUST be called in this order */
#define HIGH_BUILD_PIXELS_0 \
{ \
 eax = ebx; \
 eax &= 0x0000000f; \
}
/*
	__asm	mov		eax,ebx \
	__asm	and		eax,0x0000000f
*/

#define HIGH_BUILD_PIXELS_1 \
{ \
 eax = ebx; \
 eax >>= 4; \
 eax &= 0x0000000f;\
}
/*
	__asm	mov		eax,ebx \
	__asm	shr		eax,4 \
	__asm	and		eax,0x0000000f
*/

#define HIGH_BUILD_PIXELS_2 \
{ \
 eax = ebx; \
 eax >>= 8; \
 eax &= 0x0000000f;\
}
/*
	__asm	mov		eax,ebx \
	__asm	shr		eax,8 \
	__asm	and		eax,0x0000000f
*/

#define HIGH_BUILD_PIXELS_3 \
{ \
 eax = ebx; \
 eax >>= 12; \
 eax &= 0x0000000f;\
}
/*
	__asm	mov		eax,ebx \
	__asm	shr		eax,12 \
	__asm	and		eax,0x0000000f
*/


/* Plot Low Resolution (320xH) 16-Bit pixels */
#define PLOT_LOW_320_16BIT(offset)  \
{ \
 ebx = ecx; \
 ebx &= 0x00ff; \
 ecx = ecx >> 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset] = (unsigned short) ebx; \
 ebx = ecx; \
 ebx &= 0x00ff; \
 ecx = ecx >> 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset+1] = (unsigned short) ebx; \
 ebx = ecx; \
 ebx &= 0x00ff; \
 ecx = ecx >> 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset+2] = (unsigned short) ebx; \
 ebx = ecx; \
 ebx &= 0x00ff; \
 ebx = STRGBPalette[ebx]; \
 esi[offset+3] = (unsigned short) ebx; \
}

/* Plot Low Resolution (640xH) 16-Bit pixels */
#define PLOT_LOW_640_16BIT(offset) \
{ \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ecx >>= 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset] = ebx; \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ecx >>= 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset+1] = ebx; \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ecx >>= 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset+2] = ebx; \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ebx = STRGBPalette[ebx]; \
 esi[offset+3] = ebx; \
} 

/* Plot Low Resolution (640xH) 16-Bit pixels (Double on Y) */
#define PLOT_LOW_640_16BIT_DOUBLE_Y(offset) \
{ \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ecx >>= 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset] = ebx; \
 esi[offset+PCScreenBytesPerLine/4] = ebx; \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ecx >>= 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset+1] = ebx; \
 esi[offset+1+PCScreenBytesPerLine/4] = ebx; \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ecx >>= 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset+2] = ebx; \
 esi[offset+2+PCScreenBytesPerLine/4] = ebx; \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ebx = STRGBPalette[ebx]; \
 esi[offset+3] = ebx; \
 esi[offset+3+PCScreenBytesPerLine/4] = ebx; \
}


/* Plot Medium Resolution(640xH) 16-Bit pixels */
#define PLOT_MED_640_16BIT(offset) \
{ \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ecx >>= 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset] = (Uint16)ebx; \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ecx >>= 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset+1] = (Uint16)ebx; \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ecx >>= 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset+2] = (Uint16)ebx; \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ebx = STRGBPalette[ebx]; \
 esi[offset+3] = (Uint16)ebx; \
}

/* Plot Medium Resolution(640xH) 16-Bit pixels (Double on Y) */
#define PLOT_MED_640_16BIT_DOUBLE_Y(offset) \
{ \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ecx >>= 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset] = (Uint16)ebx; \
 esi[offset+PCScreenBytesPerLine/2] = (Uint16)ebx; \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ecx >>= 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset+1] = (Uint16)ebx; \
 esi[offset+1+PCScreenBytesPerLine/2] = (Uint16)ebx; \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ecx >>= 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset+2] = (Uint16)ebx; \
 esi[offset+2+PCScreenBytesPerLine/2] = (Uint16)ebx; \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ebx = STRGBPalette[ebx]; \
 esi[offset+3] = (Uint16)ebx; \
 esi[offset+3+PCScreenBytesPerLine/2] = (Uint16)ebx; \
}


/* Plot High Resolution (640xH) 8-Bit pixels */
#define PLOT_HIGH_640_8BIT(offset) \
{ \
 esi[offset] = LSWAP(Remap_1_Plane[eax]); \
}


/* Plot Spectrum512 Resolution(320xH) 16-Bit pixels */
#define PLOT_SPEC512_LEFT_LOW_320_16BIT(offset)	\
{ \
 ecx &= 0x000000ff; \
 ebx = STRGBPalette[ecx]; \
 esi[offset] = (Uint16)ebx; \
}

/* Plot Spectrum512 Resolution(320xH) 16-Bit pixels */
#define PLOT_SPEC512_MID_320_16BIT(offset) \
{ \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ecx >>= 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset] = (Uint16)ebx; \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ecx >>= 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset+1] = (Uint16)ebx; \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ecx >>= 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset+2] = (Uint16)ebx; \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ebx = STRGBPalette[ebx]; \
 esi[offset+3] = (Uint16)ebx; \
}

/* Plot Spectrum512 Resolution(320xH) 16-Bit pixels */
#define PLOT_SPEC512_END_LOW_320_16BIT(offset) \
{ \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ecx >>= 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset] = (Uint16)ebx; \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ecx >>= 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset+1] = (Uint16)ebx; \
 ebx = ecx; \
 ebx &= 0x000000ff; \
 ecx >>= 8; \
 ebx = STRGBPalette[ebx]; \
 esi[offset+2] = (Uint16)ebx; \
}



/* Conversion routines */
#include "convert/low320x16.c"    /* LowRes To 320xH x 16-bit colour */
#include "convert/low640x16.c"    /* LowRes To 640xH x 16-bit colour */
#include "convert/med640x16.c"    /* MediumRes To 640xH x 16-bit colour */
#include "convert/low320x8.c"     /* LowRes To 320xH x 8-bit colour */
#include "convert/low640x8.c"     /* LowRes To 640xH x 8-bit colour */
#include "convert/med640x8.c"     /* MediumRes To 640xH x 8-bit colour */
#include "convert/high640x8.c"    /* HighRes To 640xH x 8-bit colour */
#include "convert/high640x1.c"    /* HighRes To 640xH x 1-bit colour */
#include "convert/spec320x16.c"   /* Spectrum 512 To 320xH x 16-bit colour */
#include "convert/spec640x16.c"   /* Spectrum 512 To 640xH x 16-bit colour */

#include "convert/vdi16.c"        /* VDI x 16 colour */
#include "convert/vdi4.c"         /* VDI x 4 colour */
#include "convert/vdi2.c"         /* VDI x 2 colour */
