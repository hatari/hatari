/*
 * Hatari - symbols.h
 * 
 * This file is distributed under the GNU Public License, version 2 or at
 * your option any later version. Read the file gpl.txt for details.
 */

#ifndef HATARI_SYMBOLS_H
#define HATARI_SYMBOLS_H

/* readline support functions */
extern char* Symbols_MatchCpuAddress(const char *text, int state);
extern char* Symbols_MatchDspAddress(const char *text, int state);
/* symbol name -> address search */
extern bool Symbols_GetCpuAddress(const char *name, Uint32 *addr);
extern bool Symbols_GetDspAddress(const char *name, Uint32 *addr);
/* symbol address -> name search */
extern const char* Symbols_GetByCpuAddress(Uint32 addr);
extern const char* Symbols_GetByDspAddress(Uint32 addr);
/* symbols/dspsymbols command parsing */
extern int Symbols_Command(int nArgc, char *psArgs[]);

#endif
