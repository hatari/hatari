/*
  Hatari - hostscreen.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_HOSTSCREEN_H
#define HATARI_HOSTSCREEN_H

#include <SDL.h>

#include "araglue.h"

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
    ( ((uint32)(address)[0] << 16) | ((uint32)(address)[1] << 8) | (uint32)(address)[2] )

#else

#define putBpp24Pixel( address, color ) \
{ \
    ((Uint8*)(address))[0] = (color) & 0xff; \
        ((Uint8*)(address))[1] = ((color) >> 8) & 0xff; \
        ((Uint8*)(address))[2] = ((color) >> 16) & 0xff; \
}

#define getBpp24Pixel( address ) \
    ( ((uint32)(address)[2] << 16) | ((uint32)(address)[1] << 8) | (uint32)(address)[0] )

#endif


extern void HostScreen_Init(void);
extern void HostScreen_UnInit(void);
extern void HostScreen_toggleFullScreen(void);
extern bool HostScreen_renderBegin(void);
extern void HostScreen_renderEnd(void);
extern void HostScreen_update1(bool forced);
extern void HostScreen_update0(void);
extern uint32 HostScreen_getBpp(void);	/* Bytes per pixel */
extern uint32 HostScreen_getBitsPerPixel(void);
extern uint32 HostScreen_getPitch(void);
extern uint32 HostScreen_getWidth(void);
extern uint32 HostScreen_getHeight(void);
extern uint8 * HostScreen_getVideoramAddress(void);
extern void HostScreen_setPaletteColor( uint8 idx, uint32 red, uint32 green, uint32 blue );
extern uint32 HostScreen_getPaletteColor( uint8 idx );
extern void HostScreen_updatePalette( uint16 colorCount );
extern uint32 HostScreen_getColor( uint32 red, uint32 green, uint32 blue );
extern void HostScreen_setWindowSize(uint32 width, uint32 height, uint32 bpp);
extern void HostScreen_bitplaneToChunky(uint16 *atariBitplaneData, uint16 bpp, uint8 colorValues[16]);

#endif
