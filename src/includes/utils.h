/*
  Hatari - utils.h
  
  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_UTILS_H
#define HATARI_UTILS_H

#include <inttypes.h>
#include <stdlib.h>


#define CRC32_POLY	0x04c11db7	/* IEEE 802.3 recommendation */
extern void	crc32_reset		( uint32_t *crc );
extern void	crc32_add_byte		( uint32_t *crc , uint8_t c );

#define CRC16_POLY	0x1021		/* CCITT */
extern void	crc16_reset		( uint16_t *crc );
extern void	crc16_add_byte		( uint16_t *crc , uint8_t c );

void		Hatari_srand		( unsigned int seed );
extern int	Hatari_rand		( void );

#endif		/* HATARI_UTILS_H */
