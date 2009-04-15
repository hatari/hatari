/*
  Hatari - hostscreen.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_HOSTSCREEN_H
#define HATARI_HOSTSCREEN_H

#include <SDL.h>

/**
 * This macro handles the endianity for 24 bit per item data
 **/
#if SDL_BYTEORDER == SDL_BIG_ENDIAN

#define putBpp24Pixel( address, color ) \
{ \
        ((Uint8*)(address))[0] = ((color) >> 16) & 0xff; \
        ((Uint8*)(address))[1] = ((color) >> 8) & 0xff; \
        ((Uint8*)(address))[2] = (color) & 0xff; \
}

#define getBpp24Pixel( address ) \
    ( ((Uint32)(address)[0] << 16) | ((Uint32)(address)[1] << 8) | (Uint32)(address)[2] )

#else

#define putBpp24Pixel( address, color ) \
{ \
    ((Uint8*)(address))[0] = (color) & 0xff; \
        ((Uint8*)(address))[1] = ((color) >> 8) & 0xff; \
        ((Uint8*)(address))[2] = ((color) >> 16) & 0xff; \
}

#define getBpp24Pixel( address ) \
    ( ((Uint32)(address)[2] << 16) | ((Uint32)(address)[1] << 8) | (Uint32)(address)[0] )

#endif


extern void HostScreen_Init(void);
extern void HostScreen_UnInit(void);
extern void HostScreen_toggleFullScreen(void);
extern bool HostScreen_renderBegin(void);
extern void HostScreen_renderEnd(void);
extern void HostScreen_update1(bool forced);
extern Uint32 HostScreen_getBpp(void);	/* Bytes per pixel */
extern Uint32 HostScreen_getPitch(void);
extern Uint32 HostScreen_getWidth(void);
extern Uint32 HostScreen_getHeight(void);
extern Uint8 * HostScreen_getVideoramAddress(void);
extern void HostScreen_setPaletteColor(Uint8 idx, Uint32 red, Uint32 green, Uint32 blue);
extern Uint32 HostScreen_getPaletteColor(Uint8 idx);
extern void HostScreen_updatePalette(Uint16 colorCount);
extern Uint32 HostScreen_getColor(Uint32 red, Uint32 green, Uint32 blue);
extern void HostScreen_setWindowSize(Uint32 width, Uint32 height, Uint32 bpp);

#endif
