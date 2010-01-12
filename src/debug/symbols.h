/*
 * Hatari - symbols.h
 * 
 * This file is distributed under the GNU Public License, version 2 or at
 * your option any later version. Read the file gpl.txt for details.
 */

#ifndef HATARI_SYMBOLS_H
#define HATARI_SYMBOLS_H

typedef enum {
	SYMTYPE_ANY=0,
	SYMTYPE_TEXT,
	SYMTYPE_DATA,
	SYMTYPE_BSS
} symtype_t;

typedef struct {
	char *name;
	Uint32 address;
	symtype_t type;
} symbol_t;

typedef struct {
	int count;
	symbol_t *addresses;	/* items sorted by address */
	symbol_t *names;	/* items sorted by symbol name */
} symbol_list_t;

extern symbol_list_t* Symbols_Load(const char *filename, Uint32 offset, symtype_t symtype);
extern const symbol_t* Symbols_MatchByName(symbol_list_t* list, symtype_t symtype, const char *text, int state);
extern const char* Symbols_FindByAddress(symbol_list_t* list, Uint32 addr);
extern void Symbols_ShowByAddress(symbol_list_t* list);
extern void Symbols_ShowByName(symbol_list_t* list);
extern void Symbols_Free(symbol_list_t* list);

#endif
