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
	srand(seed);
}


int	Hatari_rand ( void )
{
	return rand();
}




/*-----------------------------------------------------------------------*/
/*
 * Read words and longs from memory stored in little endian order (eg intel x86 cpu)
 */
uint16_t	Mem_ReadU16_LE ( uint8_t *p )
{
	return (p[1]<<8) +p [0];
}

uint32_t	Mem_ReadU32_LE ( uint8_t *p )
{
	return (p[3]<<24) + (p[2]<<16) + (p[1]<<8) +p[0];
}


/*-----------------------------------------------------------------------*/
/*
 * Read words and longs from memory stored in big endian order (eg 680xx cpu)
 */
uint16_t	Mem_ReadU16_BE ( uint8_t *p )
{
	return (p[0]<<8) + p[1];
}

uint32_t	Mem_ReadU32_BE ( uint8_t *p )
{
	return (p[0]<<24) + (p[1]<<16) + (p[2]<<8) +p[3];
}


/*-----------------------------------------------------------------------*/
/*
 * Store words and longs in memory in little endian order (eg intel x86 cpu)
 */
void	Mem_WriteU16_LE ( uint8_t *p , uint16_t val )
{
	p[ 0 ] = val & 0xff;
	val >>= 8;
	p[ 1 ] = val & 0xff;
}

void	Mem_WriteU32_LE ( uint8_t *p , uint32_t val )
{
	p[ 0 ] = val & 0xff;
	val >>= 8;
	p[ 1 ] = val & 0xff;
	val >>= 8;
	p[ 1 ] = val & 0xff;
	val >>= 8;
	p[ 3 ] = val & 0xff;
}


/*-----------------------------------------------------------------------*/
/*
 * Store words and longs in memory in big endian order (eg 680xx cpu)
 */
void	Mem_WriteU16_BE ( uint8_t *p , uint16_t val )
{
	p[ 1 ] = val & 0xff;
	val >>= 8;
	p[ 0 ] = val & 0xff;
}

void	Mem_WriteU32_BE ( uint8_t *p , uint32_t val )
{
	p[ 3 ] = val & 0xff;
	val >>= 8;
	p[ 2 ] = val & 0xff;
	val >>= 8;
	p[ 1 ] = val & 0xff;
	val >>= 8;
	p[ 0 ] = val & 0xff;
}

