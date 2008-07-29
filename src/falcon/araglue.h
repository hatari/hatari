// defines to get Aranym code running in Hatari... they should be removed later

#ifndef ARAGLUE_H
#define ARAGLUE_H

#include <SDL_types.h>

#define uint32 Uint32
#define uint16 Uint16
#define uint8 Uint8
#define int32 Sint32
#define int16 Sint16
#define int8 Sint8

#define bug(args...) printf(args);printf("\n")
#define DUNUSED(x)

#if DEBUG
#define D(x) x
#else
#define D(x)
#endif

#endif /* ARAGLUE_H */
