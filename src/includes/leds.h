/*
  Hatari - leds.h
  
  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_LEDS_H
#define HATARI_LEDS_H

#include <SDL.h>
#include "main.h"

/* whether floppy light should be drawn by Leds_Show() */
extern bool bFloppyLight;

extern void Leds_ReInit(SDL_Surface *surf);
extern SDL_Rect* Leds_Show(void);
extern SDL_Rect* Leds_Hide(void);


#endif /* HATARI_LEDS_H */
