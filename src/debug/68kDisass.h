/*
  Hatari - 68kDisass.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_68KDISASS_H
#define HATARI_68KDISASS_H

extern uint32_t Disasm_GetNextPC(uint32_t pc);
extern void Disasm (FILE *f, uaecptr addr, uaecptr *nextpc, int cnt);

enum {
	DISASM_COLUMN_ADDRESS = 0,
	DISASM_COLUMN_HEXDUMP,
	DISASM_COLUMN_LABEL,
	DISASM_COLUMN_OPCODE,
	DISASM_COLUMN_OPERAND,
	DISASM_COLUMN_COMMENT,
	DISASM_COLUMNS			/* number of columns in disassembly output */
};

#define DISASM_COLUMN_DISABLE -1

extern void Disasm_GetColumns(int *columns);
extern void Disasm_SetColumns(int *columns);
extern void Disasm_DisableColumn(int column, const int *oldcols, int *newcols);

extern const char* Disasm_ParseOption(const char *arg);
extern int Disasm_GetOptions(void);
void Disasm_Init(void);

#endif		/* HATARI_68KDISASS_H */
