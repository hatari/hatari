/*
  Hatari - 68kDisass.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_68KDISASS_H
#define HATARI_68KDISASS_H


#define	DISASM_ENGINE_UAE	0		/* Use UAE's internal disassembler */
#define	DISASM_ENGINE_EXT	1		/* Use external disassembler from 68kdisass.c */


void Disasm (FILE *f, uaecptr addr, uaecptr *nextpc, int cnt , int DisasmEngine);


#endif		/* HATARI_68KDISASS_H */
