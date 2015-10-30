/*
  Hatari - macros.h
  
  Lookup tables and macros for screen conversion routines.

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_CONVERTMACROS_H
#define HATARI_CONVERTMACROS_H

/* For palette we don't go from colour '0' as the whole background
 * would change, so go from this value
 */
#define  BASECOLOUR       0x0A
#define  BASECOLOUR_LONG  0x0A0A0A0A

/* Remap tables to convert from plane format to byte-per-pixel
 * (Upper is for 4-Planes so if shifted by 2)
 */
static const Uint32 Remap_2_Planes[256] = {
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

static const Uint32 Remap_2_Planes_Upper[256] = {
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

static const Uint32 Remap_1_Plane[16] = {
  0x00000000+BASECOLOUR_LONG,  0x01000000+BASECOLOUR_LONG,  0x00010000+BASECOLOUR_LONG,  0x01010000+BASECOLOUR_LONG,  0x00000100+BASECOLOUR_LONG,  0x01000100+BASECOLOUR_LONG,  0x00010100+BASECOLOUR_LONG,  0x01010100+BASECOLOUR_LONG,
  0x00000001+BASECOLOUR_LONG,  0x01000001+BASECOLOUR_LONG,  0x00010001+BASECOLOUR_LONG,  0x01010001+BASECOLOUR_LONG,  0x00000101+BASECOLOUR_LONG,  0x01000101+BASECOLOUR_LONG,  0x00010101+BASECOLOUR_LONG,  0x01010101+BASECOLOUR_LONG,
};



/*----------------------------------------------------------------------*/
/* Macros to convert from Atari's planar mode to chunky mode
 * (1 byte per pixel). Convert by blocks of 4 pixels.
 * 16 low res pixels -> 4 planes of 16 bits
 * 16 med res pixels -> 2 planes of 16 bits
 * 16 hi  res pixels -> 1 plane of 16 bits
 */

#define LOW_BUILD_PIXELS_0 \
{ \
 ebx &= 0x0f0f0f0f; \
 ecx &= 0x0f0f0f0f; \
 eax = (ebx >> 12) | ebx; \
 edx = (ecx >> 12) | ecx; \
 ecx = Remap_2_Planes_Upper[edx & 0x00ff]; \
 ecx +=      Remap_2_Planes[eax & 0x00ff]; \
}

#define LOW_BUILD_PIXELS_1 \
{ \
 ecx = Remap_2_Planes_Upper[(edx >> 8) & 0x00ff]; \
 ecx +=      Remap_2_Planes[(eax >> 8) & 0x00ff]; \
}

#define LOW_BUILD_PIXELS_2 \
{ \
 ebx = (*edi     & 0xf0f0f0f0) >> 4; \
 ecx = (*(edi+1) & 0xf0f0f0f0) >> 4; \
 eax = (ebx >> 12) | ebx; \
 edx = (ecx >> 12) | ecx; \
 ecx = Remap_2_Planes_Upper[edx & 0x00ff]; \
 ecx +=      Remap_2_Planes[eax & 0x00ff]; \
}

#define LOW_BUILD_PIXELS_3 \
{ \
 ecx = Remap_2_Planes_Upper[(edx >> 8) & 0x00ff]; \
 ecx +=      Remap_2_Planes[(eax >> 8) & 0x00ff]; \
}


#define MED_BUILD_PIXELS_0 \
{ \
 ebx &= 0x0f0f0f0f; \
 eax = (ebx >> 12) | ebx; \
 ecx = Remap_2_Planes[eax & 0x000000ff]; \
}

#define MED_BUILD_PIXELS_1 \
{ \
 ecx = Remap_2_Planes[(eax >> 8) & 0x000000ff]; \
}

#define MED_BUILD_PIXELS_2 \
{ \
 ebx = (*edi & 0xf0f0f0f0) >> 4; \
 eax = (ebx >> 12) | ebx; \
 ecx = Remap_2_Planes[eax & 0x000000ff]; \
}

#define MED_BUILD_PIXELS_3 \
{ \
 ecx = Remap_2_Planes[(eax >> 8) & 0x000000ff]; \
}


/*----------------------------------------------------------------------*/
/* Macros to plot Atari's pixels in the emulator's buffer
 * (the buffer can be 32, 16 or 8 bits per pixel)
 */

/*
 * 32 bit screen format
 */

/* Plot Low Resolution (320xH) 32-Bit pixels */
#define PLOT_LOW_320_32BIT(offset)  \
{ \
	esi[offset]   = (Uint32)STRGBPalette[ecx & 0x00ff]; \
	esi[offset+1] = (Uint32)STRGBPalette[(ecx >> 8) & 0x00ff]; \
	esi[offset+2] = (Uint32)STRGBPalette[(ecx >> 16) & 0x00ff]; \
	esi[offset+3] = (Uint32)STRGBPalette[(ecx >> 24) & 0x00ff]; \
}

/* Plot Low Resolution (640xH) 32-Bit pixels */
#define PLOT_LOW_640_32BIT(offset) \
{ \
	esi[offset+0] = esi[offset+1] = STRGBPalette[ecx & 0x000000ff]; \
	esi[offset+2] = esi[offset+3] = STRGBPalette[(ecx >> 8) & 0x000000ff]; \
	esi[offset+4] = esi[offset+5] = STRGBPalette[(ecx >> 16) & 0x000000ff]; \
	esi[offset+6] = esi[offset+7] = STRGBPalette[(ecx >> 24) & 0x000000ff]; \
} 

/* Plot Low Resolution (640xH) 16-Bit pixels (Double on Y) */
#define PLOT_LOW_640_32BIT_DOUBLE_Y(offset) \
{ \
	ebx = STRGBPalette[ecx & 0x000000ff]; \
	esi[offset+0] = esi[offset+1] = esi[offset+Screen4BytesPerLine+0] \
	   = esi[offset+Screen4BytesPerLine+1] = ebx; \
	ebx = STRGBPalette[(ecx >> 8) & 0x000000ff]; \
	esi[offset+2] = esi[offset+3] = esi[offset+Screen4BytesPerLine+2] \
	   = esi[offset+Screen4BytesPerLine+3] = ebx; \
	ebx = STRGBPalette[(ecx >> 16) & 0x000000ff]; \
	esi[offset+4] = esi[offset+5] = esi[offset+Screen4BytesPerLine+4] \
	   = esi[offset+Screen4BytesPerLine+5] = ebx; \
	ebx = STRGBPalette[(ecx >> 24) & 0x000000ff]; \
	esi[offset+6] = esi[offset+7] = esi[offset+Screen4BytesPerLine+6] \
	   = esi[offset+Screen4BytesPerLine+7] = ebx; \
}

/* Plot Medium Resolution(640xH) 32-Bit pixels */
#define PLOT_MED_640_32BIT(offset) \
{ \
	esi[offset+0] = STRGBPalette[ecx & 0x000000ff]; \
	esi[offset+1] = STRGBPalette[(ecx >> 8) & 0x000000ff]; \
	esi[offset+2] = STRGBPalette[(ecx >> 16) & 0x000000ff]; \
	esi[offset+3] = STRGBPalette[(ecx >> 24) & 0x000000ff]; \
}

/* Plot Medium Resolution(640xH) 32-Bit pixels (Double on Y) */
#define PLOT_MED_640_32BIT_DOUBLE_Y(offset) \
{ \
	esi[offset+0+Screen4BytesPerLine]   = \
	esi[offset+0] = STRGBPalette[ecx & 0x000000ff]; \
	esi[offset+1+Screen4BytesPerLine] = \
	esi[offset+1] = STRGBPalette[(ecx >> 8) & 0x000000ff]; \
	esi[offset+2+Screen4BytesPerLine] = \
	esi[offset+2] = STRGBPalette[(ecx >> 16) & 0x000000ff]; \
	esi[offset+3+Screen4BytesPerLine] = \
	esi[offset+3] = STRGBPalette[(ecx >> 24) & 0x000000ff]; \
}


/* Plot Spectrum512 Resolution (320xH) 32-Bit pixels */
#define PLOT_SPEC512_LEFT_LOW_320_32BIT(offset)	\
{ \
	esi[offset] = STRGBPalette[ecx & 0x000000ff]; \
}

/* Plot Spectrum512 Resolution (320xH) 32-Bit pixels */
#define PLOT_SPEC512_MID_320_32BIT PLOT_LOW_320_32BIT

/* Plot Spectrum512 Resolution(320xH) 32-Bit pixels */
#define PLOT_SPEC512_END_LOW_320_32BIT(offset) \
{ \
	esi[offset]   = STRGBPalette[ecx & 0x000000ff]; \
	esi[offset+1] = STRGBPalette[(ecx >> 8) & 0x000000ff]; \
	esi[offset+2] = STRGBPalette[(ecx >> 16) & 0x000000ff]; \
}


/* Plot Spectrum512 Resolution (640xH) 32-Bit pixels */
#define PLOT_SPEC512_LEFT_LOW_640_32BIT(offset)	\
{ \
	esi[offset] = esi[offset+1] = STRGBPalette[ecx & 0x000000ff]; \
}

/* Plot Spectrum512 Resolution (640xH) 32-Bit pixels */
#define PLOT_SPEC512_MID_640_32BIT PLOT_LOW_640_32BIT

/* Plot Spectrum512 Resolution (640xH) 32-Bit pixels */
#define PLOT_SPEC512_END_LOW_640_32BIT(offset)	\
{ \
	esi[offset+0] = esi[offset+1] = STRGBPalette[ecx & 0x000000ff]; \
	esi[offset+2] = esi[offset+3] = STRGBPalette[(ecx >> 8) & 0x000000ff]; \
	esi[offset+4] = esi[offset+5] = STRGBPalette[(ecx >> 16) & 0x000000ff]; \
}

/* Plot Spectrum512 Resolution (640xH) 32-Bit pixels (Double on Y) */
#define PLOT_SPEC512_LEFT_LOW_640_32BIT_DOUBLE_Y(offset)	\
{ \
	esi[offset+Screen4BytesPerLine] = esi[offset+Screen4BytesPerLine+1] = \
	esi[offset] = esi[offset+1] = STRGBPalette[ecx & 0x000000ff]; \
}

/* Plot Spectrum512 Resolution (640xH) 32-Bit pixels (Double on Y) */
#define PLOT_SPEC512_MID_640_32BIT_DOUBLE_Y PLOT_LOW_640_32BIT_DOUBLE_Y

/* Plot Spectrum512 Resolution (640xH) 32-Bit pixels (Double on Y) */
#define PLOT_SPEC512_END_LOW_640_32BIT_DOUBLE_Y(offset)	\
{ \
	ebx = STRGBPalette[ecx & 0x000000ff]; \
	esi[offset+Screen4BytesPerLine] = esi[offset+Screen4BytesPerLine+1] \
	    = esi[offset] = esi[offset+1] = ebx; \
	ebx = STRGBPalette[(ecx >> 8) & 0x000000ff]; \
	esi[offset+2+Screen4BytesPerLine] = esi[offset+3+Screen4BytesPerLine] \
	    = esi[offset+2] = esi[offset+3] = ebx; \
	ebx = STRGBPalette[(ecx >> 16) & 0x000000ff]; \
	esi[offset+4+Screen4BytesPerLine] = esi[offset+5+Screen4BytesPerLine] \
	    = esi[offset+4] = esi[offset+5] = ebx; \
}


/* Plot Spectrum512 Medium Resolution (640xH) 32-Bit pixels */
#define PLOT_SPEC512_LEFT_MED_640_32BIT	PLOT_SPEC512_LEFT_LOW_320_32BIT

#define PLOT_SPEC512_MID_MED_640_32BIT PLOT_SPEC512_MID_320_32BIT

#define PLOT_SPEC512_END_MED_640_32BIT PLOT_SPEC512_END_LOW_320_32BIT


/* Plot Spectrum512 Medium Resolution (640xH) 32-Bit pixels (Double on Y) */
#define PLOT_SPEC512_LEFT_MED_640_32BIT_DOUBLE_Y(offset) \
{ \
	esi[offset+Screen4BytesPerLine] = esi[offset] = STRGBPalette[ecx & 0x000000ff]; \
}

#define PLOT_SPEC512_MID_MED_640_32BIT_DOUBLE_Y(offset) \
{ \
	esi[offset+0+Screen4BytesPerLine] = esi[offset+0] = STRGBPalette[ecx & 0x000000ff]; \
	esi[offset+1+Screen4BytesPerLine] = esi[offset+1] = STRGBPalette[(ecx >> 8) & 0x000000ff]; \
	esi[offset+2+Screen4BytesPerLine] = esi[offset+2] = STRGBPalette[(ecx >> 16) & 0x000000ff]; \
	esi[offset+3+Screen4BytesPerLine] = esi[offset+3] = STRGBPalette[(ecx >> 24) & 0x000000ff]; \
}

#define PLOT_SPEC512_END_MED_640_32BIT_DOUBLE_Y(offset) \
{ \
	esi[offset+0+Screen4BytesPerLine] = esi[offset+0] = STRGBPalette[ecx & 0x000000ff]; \
	esi[offset+1+Screen4BytesPerLine] = esi[offset+1] = STRGBPalette[(ecx >> 8) & 0x000000ff]; \
	esi[offset+2+Screen4BytesPerLine] = esi[offset+2] = STRGBPalette[(ecx >> 16) & 0x000000ff]; \
}



/*
 * 16 bit screen format
 */

/* Plot Low Resolution (320xH) 16-Bit pixels */
#define PLOT_LOW_320_16BIT(offset)  \
{ \
 esi[offset]   = (Uint16)STRGBPalette[ecx & 0x00ff]; \
 esi[offset+1] = (Uint16)STRGBPalette[(ecx >> 8) & 0x00ff]; \
 esi[offset+2] = (Uint16)STRGBPalette[(ecx >> 16) & 0x00ff]; \
 esi[offset+3] = (Uint16)STRGBPalette[(ecx >> 24) & 0x00ff]; \
}

/* Plot Low Resolution (320xH) 8-Bit pixels */
#define PLOT_LOW_320_8BIT(offset) \
{ \
  esi[offset] = SDL_SwapLE32(ecx + BASECOLOUR_LONG); \
}

/* Plot Low Resolution (640xH) 8-Bit pixels */
#define PLOT_LOW_640_8BIT(offset) \
{ \
  ebpp = ecx + BASECOLOUR_LONG; \
  ecx = ((ebpp & 0x0000ff00) << 8) | (ebpp & 0x000000ff); \
  esi[offset]   = SDL_SwapLE32((ecx << 8) | ecx); \
  ecx = ((ebpp & 0x00ff0000) >> 8) | (ebpp & 0xff000000); \
  esi[offset+1] = SDL_SwapLE32((ecx >> 8) | ecx); \
}

/* Plot Low Resolution (640xH) 8-Bit pixels (double on Y) */
#define PLOT_LOW_640_8BIT_DOUBLE_Y(offset)	\
{ \
  ebpp = ecx + BASECOLOUR_LONG; \
  ecx = ((ebpp & 0x0000ff00) << 8) | (ebpp & 0x000000ff); \
  esi[offset+Screen4BytesPerLine] = \
  esi[offset]   = SDL_SwapLE32((ecx << 8) | ecx); \
  ecx = ((ebpp & 0x00ff0000) >> 8) | (ebpp & 0xff000000); \
  esi[offset+1+Screen4BytesPerLine] = \
  esi[offset+1] = SDL_SwapLE32((ecx >> 8) | ecx); \
}

/* Plot Low Resolution (640xH) 16-Bit pixels */
#define PLOT_LOW_640_16BIT(offset) \
{ \
 esi[offset]   = STRGBPalette[ecx & 0x000000ff]; \
 esi[offset+1] = STRGBPalette[(ecx >> 8) & 0x000000ff]; \
 esi[offset+2] = STRGBPalette[(ecx >> 16) & 0x000000ff]; \
 esi[offset+3] = STRGBPalette[(ecx >> 24) & 0x000000ff]; \
} 

/* Plot Low Resolution (640xH) 16-Bit pixels (Double on Y) */
#define PLOT_LOW_640_16BIT_DOUBLE_Y(offset) \
{ \
 ebx = STRGBPalette[ecx & 0x000000ff]; \
 esi[offset]   = esi[offset+Screen4BytesPerLine]   = ebx; \
 ebx = STRGBPalette[(ecx >> 8) & 0x000000ff]; \
 esi[offset+1] = esi[offset+1+Screen4BytesPerLine] = ebx; \
 ebx = STRGBPalette[(ecx >> 16) & 0x000000ff]; \
 esi[offset+2] = esi[offset+2+Screen4BytesPerLine] = ebx; \
 ebx = STRGBPalette[(ecx >> 24) & 0x000000ff]; \
 esi[offset+3] = esi[offset+3+Screen4BytesPerLine] = ebx; \
}


/* Plot Medium Resolution (640xH) 8-Bit pixels */
#define PLOT_MED_640_8BIT(offset) \
{ \
  esi[offset] = SDL_SwapLE32(ecx + BASECOLOUR_LONG); \
}

/* Plot Medium Resolution (640xH) 8-Bit pixels (Double on Y) */
#define PLOT_MED_640_8BIT_DOUBLE_Y(offset) \
{ \
  esi[offset] = esi[offset+Screen4BytesPerLine] =\
  SDL_SwapLE32(ecx + BASECOLOUR_LONG); \
}

/* Plot Medium Resolution(640xH) 16-Bit pixels */
#define PLOT_MED_640_16BIT(offset) \
{ \
 esi[offset]   = (Uint16)STRGBPalette[ecx & 0x000000ff]; \
 esi[offset+1] = (Uint16)STRGBPalette[(ecx >> 8) & 0x000000ff]; \
 esi[offset+2] = (Uint16)STRGBPalette[(ecx >> 16) & 0x000000ff]; \
 esi[offset+3] = (Uint16)STRGBPalette[(ecx >> 24) & 0x000000ff]; \
}

/* Plot Medium Resolution(640xH) 16-Bit pixels (Double on Y) */
#define PLOT_MED_640_16BIT_DOUBLE_Y(offset) \
{ \
 esi[offset+Screen2BytesPerLine]   =\
 esi[offset]   = (Uint16)STRGBPalette[ecx & 0x000000ff]; \
 esi[offset+1+Screen2BytesPerLine] =\
 esi[offset+1] = (Uint16)STRGBPalette[(ecx >> 8) & 0x000000ff]; \
 esi[offset+2+Screen2BytesPerLine] =\
 esi[offset+2] = (Uint16)STRGBPalette[(ecx >> 16) & 0x000000ff]; \
 esi[offset+3+Screen2BytesPerLine] =\
 esi[offset+3] = (Uint16)STRGBPalette[(ecx >> 24) & 0x000000ff]; \
}


/* Plot Spectrum512 Resolution(320xH) 16-Bit pixels */
#define PLOT_SPEC512_LEFT_LOW_320_16BIT(offset)	\
{ \
 esi[offset] = (Uint16)STRGBPalette[ecx & 0x000000ff]; \
}

/* Plot Spectrum512 Resolution(320xH) 16-Bit pixels */
#define PLOT_SPEC512_MID_320_16BIT PLOT_LOW_640_16BIT

/* Plot Spectrum512 Resolution(320xH) 16-Bit pixels */
#define PLOT_SPEC512_END_LOW_320_16BIT(offset) \
{ \
 esi[offset]   = (Uint16)STRGBPalette[ecx & 0x000000ff]; \
 esi[offset+1] = (Uint16)STRGBPalette[(ecx >> 8) & 0x000000ff]; \
 esi[offset+2] = (Uint16)STRGBPalette[(ecx >> 16) & 0x000000ff]; \
}


/* Plot Spectrum512 Resolution (640xH) 16-Bit pixels */
#define PLOT_SPEC512_LEFT_LOW_640_16BIT(offset)	\
{ \
  esi[offset] = STRGBPalette[ecx & 0x000000ff]; \
}

/* Plot Spectrum512 Resolution (640xH) 16-Bit pixels */
#define PLOT_SPEC512_MID_640_16BIT PLOT_LOW_640_16BIT

/* Plot Spectrum512 Resolution (640xH) 16-Bit pixels */
#define PLOT_SPEC512_END_LOW_640_16BIT(offset)	\
{ \
  esi[offset]   = STRGBPalette[ecx & 0x000000ff]; \
  esi[offset+1] = STRGBPalette[(ecx >> 8) & 0x000000ff]; \
  esi[offset+2] = STRGBPalette[(ecx >> 16) & 0x000000ff]; \
}

/* Plot Spectrum512 Resolution (640xH) 16-Bit pixels (Double on Y) */
#define PLOT_SPEC512_LEFT_LOW_640_16BIT_DOUBLE_Y(offset)	\
{ \
  esi[offset+Screen4BytesPerLine] = \
  esi[offset] = STRGBPalette[ecx & 0x000000ff]; \
}

/* Plot Spectrum512 Resolution (640xH) 16-Bit pixels (Double on Y) */
#define PLOT_SPEC512_MID_640_16BIT_DOUBLE_Y PLOT_LOW_640_16BIT_DOUBLE_Y

/* Plot Spectrum512 Resolution (640xH) 16-Bit pixels (Double on Y) */
#define PLOT_SPEC512_END_LOW_640_16BIT_DOUBLE_Y(offset)	\
{ \
  ebx = STRGBPalette[ecx & 0x000000ff]; \
  esi[offset]   = esi[offset+Screen4BytesPerLine]   = ebx; \
  ebx = STRGBPalette[(ecx >> 8) & 0x000000ff]; \
  esi[offset+1] = esi[offset+1+Screen4BytesPerLine] = ebx; \
  ebx = STRGBPalette[(ecx >> 16) & 0x000000ff]; \
  esi[offset+2] = esi[offset+2+Screen4BytesPerLine] = ebx; \
}


/* Plot Spectrum512 Medium Resolution (640xH) 16-Bit pixels */
#define PLOT_SPEC512_LEFT_MED_640_16BIT	PLOT_SPEC512_LEFT_LOW_320_16BIT

#define PLOT_SPEC512_MID_MED_640_16BIT PLOT_SPEC512_MID_320_16BIT

#define PLOT_SPEC512_END_MED_640_16BIT PLOT_SPEC512_END_LOW_320_16BIT


/* Plot Spectrum512 Medium Resolution (640xH) 16-Bit pixels (Double on Y) */
#define PLOT_SPEC512_LEFT_MED_640_16BIT_DOUBLE_Y PLOT_SPEC512_LEFT_MED_640_32BIT_DOUBLE_Y

#define PLOT_SPEC512_MID_MED_640_16BIT_DOUBLE_Y PLOT_SPEC512_MID_MED_640_32BIT_DOUBLE_Y

#define PLOT_SPEC512_END_MED_640_16BIT_DOUBLE_Y PLOT_SPEC512_END_MED_640_32BIT_DOUBLE_Y



/* Get Spec512 pixels which are offset by 1 pixel */
#if defined(__i386__)    // Unaligned direct access is only supported on i86 platforms

/* on AMD XP, first one is 1/3 faster than aligned access, and
 * final pixels access ~15% faster than aligned operation below
 */
# define GET_SPEC512_OFFSET_PIXELS(pixels, x)  \
            (*(Uint32 *)(((Uint8 *)pixels) + x))
# define GET_SPEC512_OFFSET_FINAL_PIXELS(pixels) \
            (*(Uint32 *)(((Uint8 *)pixels) + 13))

#else

# define GET_SPEC512_OFFSET_PIXELS(pixels, x)  \
            (((*(Uint32 *)(((Uint8 *)pixels) + x-1)) >> 8) \
	     | ((*(Uint32 *)(((Uint8 *)pixels) + x+3)) << 24))
# define GET_SPEC512_OFFSET_FINAL_PIXELS(pixels)  \
            ((*(Uint32 *)(((Uint8 *)pixels) + 12)) >> 8)

#endif /* __i386__ */


#endif /* HATARI_CONVERTMACROS_H */
