/*
  Hatari - acia.h

  This file is distributed under the GNU General Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_ACIA_H
#define HATARI_ACIA_H


void	ACIA_Init  ( ACIA_STRUCT *pACIA );

Uint8	ACIA_Read_SR ( ACIA_STRUCT *pACIA );
void	ACIA_Write_CR ( ACIA_STRUCT *pACIA , Uint8 CR );
Uint8	ACIA_Read_RDR ( ACIA_STRUCT *pACIA );
void	ACIA_Write_TDR ( ACIA_STRUCT *pACIA , Uint8 TDR );


#endif /* ifndef HATARI_ACIA_H */
