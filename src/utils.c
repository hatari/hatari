/*
 * Hatari - utils.c
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * Utils functions :
 *	- CRC32
 *
 * This file contains various utility functions used by different parts of Hatari.
 */
const char Utils_fileid[] = "Hatari utils.c";

#include "utils.h"


/************************************************************************/
/* Functions used to compute the CRC32 of a stream of bytes.		*/
/* These functions require a pointer to an unsigned int (uint32_t) to	*/
/* store the resulting CRC.						*/
/*	crc32_reset : call this once to reset the CRC, before adding	*/
/*		some bytes.						*/
/*	crc32_add_byte : update the current CRC with a new byte.	*/
/************************************************************************/

/*--------------------------------------------------------------*/
/* Reset the crc32 value. This should be done before calling	*/
/* crc32_add_byte().						*/
/*--------------------------------------------------------------*/

void	crc32_reset ( uint32_t *crc )
{
	*crc = 0xffffffff;
}


/*--------------------------------------------------------------*/
/* Update the current value of crc with a new byte.		*/
/* Call crc32_reset() first to init the crc value.		*/
/*--------------------------------------------------------------*/

void	crc32_add_byte ( uint32_t *crc , uint8_t c )
{
	int	bit;
    
	for ( bit=0 ; bit<8; bit++ )
	{
		if ( ( c & 0x80 ) ^ ( *crc & 0x80000000 ) )
			*crc = ( *crc << 1 ) ^ CRC32_POLY;

		else
			*crc = *crc << 1;

            c <<= 1;
        }
}


/************************************************************************/
/* Functions used to compute the CRC16 of a stream of bytes.		*/
/* These functions require a pointer to an unsigned int (uint16_t) to	*/
/* store the resulting CRC.						*/
/*	crc16_reset : call this once to reset the CRC, before adding	*/
/*		some bytes.						*/
/*	crc16_add_byte : update the current CRC with a new byte.	*/
/************************************************************************/

/*--------------------------------------------------------------*/
/* Reset the crc16 value. This should be done before calling	*/
/* crc16_add_byte().						*/
/*--------------------------------------------------------------*/

void	crc16_reset ( uint16_t *crc )
{
	*crc = 0xffff;
}


/*--------------------------------------------------------------*/
/* Update the current value of crc with a new byte.		*/
/* Call crc16_reset() first to init the crc value.		*/
/*--------------------------------------------------------------*/

void	crc16_add_byte ( uint16_t *crc , uint8_t c )
{
	int	bit;

	*crc ^= ( c << 8 );
	for ( bit=0 ; bit<8; bit++ )
	{
		if ( *crc & 0x8000 )
			*crc = ( *crc << 1 ) ^ CRC16_POLY;

		else
			*crc = *crc << 1;

            c <<= 1;
        }
}




/************************************************************************/
/* Functions used to compute random numbers				*/
/*									*/
/* These can be simple wrappers around the OS calls or some customised	*/
/* routines.								*/
/************************************************************************/

void	Hatari_srand ( unsigned int seed )
{
	return srand( seed );
}


int	Hatari_rand ( void )
{
	return rand();
}



