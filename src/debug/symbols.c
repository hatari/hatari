/*
 * Hatari - symbols.c
 * 
 * Copyright (C) 2010-2013 by Eero Tamminen
 * 
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 * 
 * symbols.c - Hatari debugger symbol/address handling; parsing, sorting,
 * matching, TAB completion support etc.
 * 
 * Symbol/address file contents are identical to "nm" output i.e. composed
 * of a hexadecimal addresses followed by a space, letter indicating symbol
 * type (T = text/code, D = data, B = BSS), space and the symbol name.
 * Empty lines and lines starting with '#' are ignored.  AHCC SYM output
 * compatible.
 */
const char Symbols_fileid[] = "Hatari symbols.c : " __DATE__ " " __TIME__;

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <SDL_types.h>
#include "main.h"
#include "symbols.h"
#include "debugui.h"
#include "debug_priv.h"
#include "evaluate.h"

typedef struct {
	char *name;
	Uint32 address;
	symtype_t type;
} symbol_t;

typedef struct {
	unsigned int count;
	symbol_t *addresses;	/* items sorted by address */
	symbol_t *names;	/* items sorted by symbol name */
} symbol_list_t;


/* how many characters the symbol name can have.
 * NOTE: change also sscanf width arg if you change this!!!
 */
#define MAX_SYM_SIZE 32


/* TODO: add symbol name/address file names to configuration? */
static symbol_list_t *CpuSymbolsList;
static symbol_list_t *DspSymbolsList;


/* ------------------ load and free functions ------------------ */

/**
 * compare function for qsort() to sort according to symbol address
 */
static int symbols_by_address(const void *s1, const void *s2)
{
	Uint32 addr1 = ((const symbol_t*)s1)->address;
	Uint32 addr2 = ((const symbol_t*)s2)->address;

	if (addr1 < addr2) {
		return -1;
	}
	if (addr1 > addr2) {
		return 1;
	}
	fprintf(stderr, "WARNING: symbols '%s' & '%s' have the same 0x%x address.\n",
		((const symbol_t*)s1)->name, ((const symbol_t*)s2)->name, addr1);
	return 0;
}

/**
 * compare function for qsort() to sort according to symbol name
 */
static int symbols_by_name(const void *s1, const void *s2)
{
	const char* name1 = ((const symbol_t*)s1)->name;
	const char* name2 = ((const symbol_t*)s2)->name;
	int ret;

	ret = strcmp(name1, name2);
	if (!ret) {
		fprintf(stderr, "WARNING: addresses 0x%x & 0x%x have the same '%s' name.\n",
			((const symbol_t*)s1)->address, ((const symbol_t*)s2)->address, name1);
	}
	return ret;
}


/**
 * Load symbols of given type and the symbol address addresses from
 * the given file and add given offsets to the addresses.
 * Return symbols list or NULL for failure.
 */
static symbol_list_t* Symbols_Load(const char *filename, Uint32 *offsets, Uint32 maxaddr, symtype_t gettype)
{
	symbol_list_t *list;
	char symchar, buffer[128], name[MAX_SYM_SIZE+1], *buf;
	int count, line, symbols;
	Uint32 address, offset;
	symtype_t symtype;
	FILE *fp;
	
	if (!(fp = fopen(filename, "r"))) {
		fprintf(stderr, "ERROR: opening '%s' failed!\n", filename);
		return NULL;
	}

	/* count content lines */
	symbols = 0;
	while (fgets(buffer, sizeof(buffer), fp)) {
		/* skip comments (AHCC SYM file comments start with '*') */
		if (*buffer == '#' || *buffer == '*') {
			continue;
		}
		/* skip empty lines */
		for (buf = buffer; isspace(*buf); buf++);
		if (!*buf) {
			continue;
		}
		symbols++;
	}
	fseek(fp, 0, SEEK_SET);

	if (!symbols) {
		fprintf(stderr, "ERROR: no symbols/addresses in '%s'!\n", filename);
		fclose(fp);
		return NULL;
	}

	/* allocate space for symbol list */
	list = malloc(sizeof(symbol_list_t));
	assert(list);
	list->names = malloc(symbols * sizeof(symbol_t));
	assert(list->names);

	/* read symbols */
	count = 0;
	for (line = 1; fgets(buffer, sizeof(buffer), fp); line++) {
		/* skip comments (AHCC SYM file comments start with '*') */
		if (*buffer == '#' || *buffer == '*') {
			continue;
		}
		/* skip empty lines */
		for (buf = buffer; isspace(*buf); buf++);
		if (!*buf) {
			continue;
		}
		assert(count < symbols); /* file not modified in meanwhile? */
		if (sscanf(buffer, "%x %c %32[0-9A-Za-z_.-]s", &address, &symchar, name) != 3) {
			fprintf(stderr, "WARNING: syntax error in '%s' on line %d, skipping.\n", filename, line);
			continue;
		}
		switch (toupper(symchar)) {
		case 'T':
			symtype = SYMTYPE_TEXT;
			offset = offsets[0];
			break;
		case 'O':	/* AHCC type for _StkSize etc */
		case 'D':
			symtype = SYMTYPE_DATA;
			offset = offsets[1];
			break;
		case 'B':
			symtype = SYMTYPE_BSS;
			offset = offsets[2];
			break;
		default:
			fprintf(stderr, "WARNING: unrecognized symbol type '%c' on line %d in '%s', skipping.\n", symchar, line, filename);
			continue;
		}
		if (!(gettype & symtype)) {
			continue;
		}
		address += offset;
		if (address > maxaddr) {
			fprintf(stderr, "WARNING: invalid address 0x%x in '%s' on line %d, skipping.\n", address, filename, line);
			continue;
		}
		list->names[count].address = address;
		list->names[count].type = symtype;
		list->names[count].name = strdup(name);
		assert(list->names[count].name);
		count++;
	}

	if (count < symbols) {
		if (!count) {
			fprintf(stderr, "ERROR: no valid symbols in '%s', loading failed!\n", filename);
			free(list->names);
			free(list);
			fclose(fp);
			return NULL;
		}
		/* parsed less than there were "content" lines */
		list->names = realloc(list->names, count * sizeof(symbol_t));
		assert(list->names);
	}
	list->count = count;

	/* copy name list to address list */
	list->addresses = malloc(count * sizeof(symbol_t));
	assert(list->addresses);
	memcpy(list->addresses, list->names, count * sizeof(symbol_t));

	/* sort both lists, with different criteria */
	qsort(list->addresses, count, sizeof(symbol_t), symbols_by_address);
	qsort(list->names, count, sizeof(symbol_t), symbols_by_name);

	fclose(fp);
	fprintf(stderr, "Loaded %d symbols from '%s'.\n", count, filename);
	return list;
}


/**
 * Free read symbols.
 */
static void Symbols_Free(symbol_list_t* list)
{
	unsigned int i;

	if (!list) {
		return;
	}
	assert(list->count);
	for (i = 0; i < list->count; i++) {
		free(list->names[i].name);
	}
	free(list->addresses);
	free(list->names);

	/* catch use of freed list */
	list->addresses = NULL;
	list->names = NULL;
	list->count = 0;
	free(list);
}


/* ---------------- symbol name completion support ------------------ */

/**
 * Helper for symbol name completion and finding their addresses.
 * STATE = 0 -> different text from previous one.
 * Return (copy of) next name or NULL if no matches.
 */
static char* Symbols_MatchByName(symbol_list_t* list, symtype_t symtype, const char *text, int state)
{
	static unsigned int i, len;
	const symbol_t *entry;
	
	if (!list) {
		return NULL;
	}

	if (!state) {
		/* first match */
		len = strlen(text);
		i = 0;
	}

	/* next match */
	entry = list->names;
	while (i < list->count) {
		if ((entry[i].type & symtype) &&
		    strncmp(entry[i].name, text, len) == 0) {
			return strdup(entry[i++].name);
		} else {
			i++;
		}
	}
	return NULL;
}

/**
 * Readline match callbacks for CPU symbol name completion.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char* Symbols_MatchCpuAddress(const char *text, int state)
{
	return Symbols_MatchByName(CpuSymbolsList, SYMTYPE_ALL, text, state);
}
char* Symbols_MatchCpuCodeAddress(const char *text, int state)
{
	return Symbols_MatchByName(CpuSymbolsList, SYMTYPE_TEXT, text, state);
}
char* Symbols_MatchCpuDataAddress(const char *text, int state)
{
	return Symbols_MatchByName(CpuSymbolsList, SYMTYPE_DATA|SYMTYPE_BSS, text, state);
}

/**
 * Readline match callback for DSP symbol name completion.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char* Symbols_MatchDspAddress(const char *text, int state)
{
	return Symbols_MatchByName(DspSymbolsList, SYMTYPE_ALL, text, state);
}
char* Symbols_MatchDspCodeAddress(const char *text, int state)
{
	return Symbols_MatchByName(DspSymbolsList, SYMTYPE_TEXT, text, state);
}
char* Symbols_MatchDspDataAddress(const char *text, int state)
{
	return Symbols_MatchByName(DspSymbolsList, SYMTYPE_DATA|SYMTYPE_BSS, text, state);
}


/* ---------------- symbol name -> address search ------------------ */

/**
 * Search symbol of given type by name.
 * Return symbol if name matches, zero otherwise.
 */
static const symbol_t* Symbols_SearchByName(symbol_list_t* list, symtype_t symtype, const char *name)
{
	symbol_t *entries;
	/* left, right, middle */
        int l, r, m, dir;

	if (!list) {
		return NULL;
	}
	entries = list->names;

	/* bisect */
	l = 0;
	r = list->count - 1;
	do {
		m = (l+r) >> 1;
		dir = strcmp(entries[m].name, name);
		if (dir == 0 && (entries[m].type & symtype)) {
			return &(entries[m]);
		}
		if (dir > 0) {
			r = m-1;
		} else {
			l = m+1;
		}
	} while (l <= r);
	return NULL;
}

/**
 * Set given CPU symbol's address to variable and return TRUE if one was found.
 */
bool Symbols_GetCpuAddress(symtype_t symtype, const char *name, Uint32 *addr)
{
	const symbol_t *entry;
	entry = Symbols_SearchByName(CpuSymbolsList, symtype, name);
	if (entry) {
		*addr = entry->address;
		return true;
	}
	return false;
}

/**
 * Set given DSP symbol's address to variable and return TRUE if one was found.
 */
bool Symbols_GetDspAddress(symtype_t symtype, const char *name, Uint32 *addr)
{
	const symbol_t *entry;
	entry = Symbols_SearchByName(DspSymbolsList, symtype, name);
	if (entry) {
		*addr = entry->address;
		return true;
	}
	return false;
}


/* ---------------- symbol address -> name search ------------------ */

/**
 * Search symbol by address.
 * Return symbol index if address matches, -1 otherwise.
 */
static int Symbols_SearchByAddress(symbol_list_t* list, Uint32 addr)
{
	symbol_t *entries;
	/* left, right, middle */
        int l, r, m;
	Uint32 curr;

	if (!list) {
		return -1;
	}
	entries = list->addresses;

	/* bisect */
	l = 0;
	r = list->count - 1;
	do {
		m = (l+r) >> 1;
		curr = entries[m].address;
		if (curr == addr) {
			return m;
		}
		if (curr > addr) {
			r = m-1;
		} else {
			l = m+1;
		}
	} while (l <= r);
	return -1;
}

/**
 * Search CPU symbol by address.
 * Return symbol name if address matches, NULL otherwise.
 * Returned name is valid only until next Symbols_* function call.
 */
const char* Symbols_GetByCpuAddress(Uint32 addr)
{
	int idx = Symbols_SearchByAddress(CpuSymbolsList, addr);
	if (idx < 0) {
		return NULL;
	}
	return CpuSymbolsList->addresses[idx].name;
}
/**
 * Search DSP symbol by address.
 * Return symbol name if address matches, NULL otherwise.
 * Returned name is valid only until next Symbols_* function call.
 */
const char* Symbols_GetByDspAddress(Uint16 addr)
{
	int idx = Symbols_SearchByAddress(DspSymbolsList, addr);
	if (idx < 0) {
		return NULL;
	}
	return DspSymbolsList->addresses[idx].name;
}

/**
 * Search CPU symbol by address.
 * Return symbol index if address matches, -1 otherwise.
 */
int Symbols_GetCpuAddressIndex(Uint32 addr)
{
	return Symbols_SearchByAddress(CpuSymbolsList, addr);	
}

/**
 * Search DSP symbol by address.
 * Return symbol index if address matches, -1 otherwise.
 */
int Symbols_GetDspAddressIndex(Uint16 addr)
{
	return Symbols_SearchByAddress(DspSymbolsList, addr);	
}

/**
 * Return how many symbols are loaded/available
 */
unsigned int Symbols_CpuCount(void)
{
	return (CpuSymbolsList ? CpuSymbolsList->count : 0);
}
unsigned int Symbols_DspCount(void)
{
	return (DspSymbolsList ? DspSymbolsList->count : 0);
}

/* ---------------- symbol showing and command parsing ------------------ */

/**
 * Show symbols from given list with paging.
 */
static void Symbols_Show(symbol_list_t* list, const char *sorttype)
{
	symbol_t *entry, *entries;
	unsigned int i;
	char symchar;
	
	if (!list) {
		fprintf(stderr, "No symbols!\n");
		return;
	}

	if (strcmp("addr", sorttype) == 0) {
		entries = list->addresses;
	} else {
		entries = list->names;
	}
	fprintf(stderr, "%s symbols sorted by %s:\n",
		(list == CpuSymbolsList ? "CPU" : "DSP"), sorttype);

	for (entry = entries, i = 0; i < list->count; i++, entry++) {
		switch (entry->type) {
		case SYMTYPE_TEXT:
			symchar = 'T';
			break;
		case SYMTYPE_DATA:
			symchar = 'D';
			break;
		case SYMTYPE_BSS:
			symchar = 'B';
			break;
		default:
			symchar = '?';
		}
		fprintf(stderr, "0x%08x %c %s\n",
			entry->address, symchar, entry->name);
		if (i && i % 20 == 0) {
			fprintf(stderr, "--- q to exit listing, just enter to continue --- ");
			if (toupper(getchar()) == 'Q') {
				return;
			}
		}
	}
}

const char Symbols_Description[] =
	"<filename|addr|name|free> [<T offset> [<D offset> <B offset>]]\n"
	"\tLoads symbol names and their addresses from the given file.\n"
	"\tIf one base address/offset is given, its added to all addresses.\n"
	"\tIf three offsets are given (and non-zero), they're applied to\n"
	"\t text (T), data (D) and BSS (B) symbols. If there were previously\n"
	"\tloaded symbols, they're replaced.\n"
	"\n"
	"\tGiving either 'name' or 'addr' instead of a file name, will\n"
	"\tlist the currently loaded symbols.  Giving 'free' will remove\n"
	"\tthe loaded symbols.";

/**
 * Handle debugger 'symbols' command and its arguments
 */
int Symbols_Command(int nArgc, char *psArgs[])
{
	enum { TYPE_NONE, TYPE_CPU, TYPE_DSP } listtype;
	Uint32 offsets[3], maxaddr;
	symbol_list_t *list;
	const char *file;
	int i;

	if (strcmp("dspsymbols", psArgs[0]) == 0) {
		listtype = TYPE_DSP;
		maxaddr = 0xFFFF;
	} else if (strcmp("symbols", psArgs[0]) == 0) {
		listtype = TYPE_CPU;
		maxaddr = 0xFFFFFF;
	} else {
		listtype = TYPE_NONE;
		maxaddr = 0;
	}
	if (nArgc < 2 || listtype == TYPE_NONE) {
		DebugUI_PrintCmdHelp(psArgs[0]);
		return DEBUGGER_CMDDONE;
	}
	file = psArgs[1];

	/* handle special cases */
	if (strcmp(file, "name") == 0 || strcmp(file, "addr") == 0) {
		list = (listtype == TYPE_DSP ? DspSymbolsList : CpuSymbolsList);
		Symbols_Show(list, file);
		return DEBUGGER_CMDDONE;
	}
	if (strcmp(file, "free") == 0) {
		if (listtype == TYPE_DSP) {
			Symbols_Free(DspSymbolsList);
			DspSymbolsList = NULL;
		} else {
			Symbols_Free(CpuSymbolsList);
			CpuSymbolsList = NULL;
		}
		return DEBUGGER_CMDDONE;
	}

	/* get offsets */
	offsets[0] = 0;
	for (i = 0; i < ARRAYSIZE(offsets); i++) {
		if (i+2 < nArgc) {
			int dummy;
			Eval_Expression(psArgs[i+2], &(offsets[i]), &dummy, listtype==TYPE_DSP);
		} else {
			/* default to first (text) offset */
			offsets[i] = offsets[0];
		}
	}

	list = Symbols_Load(file, offsets, maxaddr, SYMTYPE_ALL);
	if (list) {
		if (listtype == TYPE_CPU) {
			Symbols_Free(CpuSymbolsList);
			CpuSymbolsList = list;
		} else {
			Symbols_Free(DspSymbolsList);
			DspSymbolsList = list;
		}
	} else {
		DebugUI_PrintCmdHelp(psArgs[0]);
	}
	return DEBUGGER_CMDDONE;
}
