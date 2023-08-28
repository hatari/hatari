/*
 * Hatari - symbols.h
 * 
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 */

#ifndef HATARI_SYMBOLS_H
#define HATARI_SYMBOLS_H

typedef enum {
	SYMTYPE_TEXT = 1,
	SYMTYPE_WEAK = 2,
	SYMTYPE_CODE = (SYMTYPE_TEXT|SYMTYPE_WEAK),
	/* other types get sorted after code types */
	SYMTYPE_DATA = 4,
	SYMTYPE_BSS  = 8,
	SYMTYPE_ABS  = 16,
	SYMTYPE_ALL  = 32-1
} symtype_t;

typedef struct {
	char *name;
	uint32_t address;
	symtype_t type;
	bool name_allocated;
} symbol_t;

extern const char Symbols_Description[];

/* readline completion support functions for CPU */
extern char* Symbols_MatchCpuAddress(const char *text, int state);
extern char* Symbols_MatchCpuCodeAddress(const char *text, int state);
extern char* Symbols_MatchCpuDataAddress(const char *text, int state);
/* readline completion support functions for DSP */
extern char* Symbols_MatchDspAddress(const char *text, int state);
extern char* Symbols_MatchDspCodeAddress(const char *text, int state);
extern char* Symbols_MatchDspDataAddress(const char *text, int state);
/* symbol name -> address search */
extern bool Symbols_GetCpuAddress(symtype_t symtype, const char *name, uint32_t *addr);
extern bool Symbols_GetDspAddress(symtype_t symtype, const char *name, uint32_t *addr);
/* symbol address -> name search */
extern const char* Symbols_GetByCpuAddress(uint32_t addr, symtype_t symtype);
extern const char* Symbols_GetByDspAddress(uint32_t addr, symtype_t symtype);
extern const char* Symbols_GetBeforeCpuAddress(uint32_t *addr);
extern const char* Symbols_GetBeforeDspAddress(uint32_t *addr);
/* TEXT symbol address -> index */
extern int Symbols_GetCpuCodeIndex(uint32_t addr);
extern int Symbols_GetDspCodeIndex(uint32_t addr);
/* how many TEXT symbols are loaded */
extern int Symbols_CpuCodeCount(void);
extern int Symbols_DspCodeCount(void);
/* handlers for automatic program symbol loading */
extern void Symbols_RemoveCurrentProgram(void);
extern void Symbols_ChangeCurrentProgram(const char *path);
extern void Symbols_ShowCurrentProgramPath(FILE *fp);
extern void Symbols_LoadCurrentProgram(void);
extern void Symbols_FreeAll(void);
/* symbols/dspsymbols command parsing */
extern char *Symbols_MatchCommand(const char *text, int state);
extern int Symbols_Command(int nArgc, char *psArgs[]);

#endif
