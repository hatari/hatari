/*
  Hatari - utils.h
  
  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_UTILS_H
#define HATARI_UTILS_H

#include <SDL_types.h>


#define CRC32_POLY      0x04c11db7      /* IEEE 802.3 recommandation */

void    crc32_reset ( Uint32 *crc );
void    crc32_add_byte ( Uint32 *crc , Uint8 c );


#endif		/* HATARI_UTILS_H */
