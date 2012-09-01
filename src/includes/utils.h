/*
  Hatari - utils.h
  
  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_UTILS_H
#define HATARI_UTILS_H

#include <SDL_types.h>


#define CRC32_POLY	0x04c11db7	/* IEEE 802.3 recommandation */

extern void    crc32_reset ( Uint32 *crc );
extern void    crc32_add_byte ( Uint32 *crc , Uint8 c );

#define CRC16_POLY	0x1021		/* CCITT */

extern void    crc16_reset ( Uint16 *crc );
extern void    crc16_add_byte ( Uint16 *crc , Uint8 c );


#endif		/* HATARI_UTILS_H */
