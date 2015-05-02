/*
 * Hatari - symbols.c
 * 
 * Copyright (C) 2010-2015 by Eero Tamminen
 * 
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 * 
 * symbols.c - Hatari debugger symbol/address handling; parsing, sorting,
 * matching, TAB completion support etc.
 * 
 * Symbol/address information is read either from:
 * - A program file's DRI/GST format symbol table, or
 * - ASCII file which contents are subset of "nm" output i.e. composed of
 *   a hexadecimal addresses followed by a space, letter indicating symbol
 *   type (T = text/code, D = data, B = BSS), space and the symbol name.
 *   Empty lines and lines starting with '#' are ignored.  It's AHCC SYM
 *   output compatible.
 */
const char Symbols_fileid[] = "Hatari symbols.c : " __DATE__ " " __TIME__;

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <SDL_types.h>
#include <SDL_endian.h>
#include "main.h"
#include "file.h"
#include "options.h"
#include "symbols.h"
#include "debugui.h"
#include "debug_priv.h"
#include "debugInfo.h"
#include "evaluate.h"
#include "configuration.h"

typedef struct {
	char *name;
	Uint32 address;
	symtype_t type;
} symbol_t;

typedef struct {
	int count;		/* final symbol count */
	int symbols;		/* initial symbol count */
	symbol_t *addresses;	/* items sorted by address */
	symbol_t *names;	/* items sorted by symbol name */
} symbol_list_t;

typedef struct {
	Uint32 offset;
	Uint32 end;
} prg_section_t;


/* how many characters the symbol name can have.
 * NOTE: change also sscanf width arg if you change this!!!
 */
#define MAX_SYM_SIZE 32


/* TODO: add symbol name/address file names to configuration? */
static symbol_list_t *CpuSymbolsList;
static symbol_list_t *DspSymbolsList;

/* path for last loaded program (through GEMDOS HD emulation) */
static char *CurrentProgramPath;
static bool SymbolsAreForProgram;
static bool AutoLoadFailed;


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
 * Allocate symbol list & names for given number of items.
 * Return allocated list or NULL on failure.
 */
static symbol_list_t* symbol_list_alloc(int symbols)
{
	symbol_list_t *list;

	if (!symbols) {
		return NULL;
	}
	list = calloc(1, sizeof(symbol_list_t));
	if (list) {
		list->names = malloc(symbols * sizeof(symbol_t));
		if (!list->names) {
			free(list);
			list = NULL;
		}
	}
	return list;
}

/**
 * Free symbol list & names.
 */
static void symbol_list_free(symbol_list_t *list)
{
	if (list) {
		if (list->names) {
			free(list->names);
		}
		free(list);
	}
}

/**
 * Return symbol type identifier char
 */
static char symbol_char(int type)
{
	switch (type) {
	case SYMTYPE_TEXT: return 'T';
	case SYMTYPE_DATA: return 'D';
	case SYMTYPE_BSS:  return 'B';
	default: return '?';
	}
}

#define INVALID_SYMBOL_OFFSETS ((symbol_list_t*)1)

/**
 * Load symbols of given type and the symbol address addresses from
 * DRI/GST format symbol table, and add given offsets to the addresses:
 *	http://toshyp.atari.org/en/005005.html
 * Return symbols list or NULL for failure.
 */
static symbol_list_t* symbols_load_dri(FILE *fp, prg_section_t *sections, symtype_t gettype, Uint32 tablesize)
{
	int i, count, symbols, len, outside;
	int dtypes, locals, ofiles;
	prg_section_t *section;
	symbol_list_t *list;
	symtype_t symtype;
#define DRI_ENTRY_SIZE	14
	char name[23];
	Uint16 symid;
	Uint32 address;

	if (tablesize % DRI_ENTRY_SIZE || !tablesize) {
		fprintf(stderr, "ERROR: invalid DRI/GST symbol table size %d!\n", tablesize);
		return NULL;
	}
	symbols = tablesize / DRI_ENTRY_SIZE;
	if (!(list = symbol_list_alloc(symbols))) {
		return NULL;
	}

	outside = dtypes = ofiles = locals = count = 0;
	for (i = 1; i <= symbols; i++) {
		/* read DRI symbol table slot */
		if (fread(name, 8, 1, fp) != 1 ||
		    fread(&symid, sizeof(symid), 1, fp) != 1 ||
		    fread(&address, sizeof(address), 1, fp) != 1) {
			break;
		}
		address = SDL_SwapBE32(address);
		symid = SDL_SwapBE16(symid);

		/* GST extended DRI symbol format? */
		if ((symid & 0x0048)) {
			/* next slot is rest of name */
			i += 1;
			if (fread(name+8, 14, 1, fp) != 1) {
				break;
			}
			name[22] = '\0';
		} else {
			name[8] = '\0';
		}

		/* check section */
		switch (symid & 0xf00) {
		case 0x0200:
			symtype = SYMTYPE_TEXT;
			section = &(sections[0]);
			break;
		case 0x0400:
			symtype = SYMTYPE_DATA;
			section = &(sections[1]);
			break;
		case 0x0100:
			symtype = SYMTYPE_BSS;
			section = &(sections[2]);
			break;
		default:
			if ((symid & 0xe000) == 0xe000) {
				dtypes++;
				continue;
			}
			fprintf(stderr, "WARNING: ignoring symbol '%s' in slot %d of unknown type 0x%x.\n", name, i, symid);
			continue;
		}
		if (!(gettype & symtype)) {
			continue;
		}
		if (name[0] == '.' && name[1] == 'L') {
			locals++;
			continue;
		}
		len = strlen(name);
		if (strchr(name, '/') || (len > 2 && name[len-2] == '.' && name[len-1] == 'o')) {
			ofiles++;
			continue;
		}
		address += section->offset;
		if (address > section->end) {
			/* VBCC has 1 symbol outside of its section */
			if (++outside > 2) {
				/* potentially buggy version of VBCC vlink used */
				fprintf(stderr, "ERROR: too many invalid offsets, skipping rest of symbols!\n");
				symbol_list_free(list);
				return INVALID_SYMBOL_OFFSETS;
			}
			fprintf(stderr, "WARNING: ignoring symbol '%s' of %c type in slot %d with invalid offset 0x%x (>= 0x%x).\n",
				name, symbol_char(symtype), i, address, section->end);
			continue;
		}
		list->names[count].address = address;
		list->names[count].type = symtype;
		list->names[count].name = strdup(name);
		assert(list->names[count].name);
		count++;
	}
	if (i <= symbols) {
		perror("ERROR: reading symbol failed");
		symbol_list_free(list);
		return NULL;
	}
	if (dtypes) {
		fprintf(stderr, "NOTE: ignored %d globally defined equated values.\n", dtypes);
	}
	if (locals) {
		fprintf(stderr, "NOTE: ignored %d unnamed / local symbols (= name starts with '.L').\n", locals);
	}
	if (ofiles) {
		/* object file path names most likely get truncated and
		 * as result cause unnecessary symbol name conflicts in
		 * addition to object file addresses conflicting with
		 * first symbol in the object file.
		 */
		fprintf(stderr, "NOTE: ignored %d object file names (= name has '/' or ends in '.o').\n", ofiles);
	}
	list->symbols = symbols;
	list->count = count;
	return list;
}


/**
 * Print program header information.
 * Return false for unrecognized symbol table type.
 */
static bool symbols_print_prg_info(Uint32 tabletype, Uint32 prgflags, Uint16 relocflag)
{
	static const struct {
		Uint32 flag;
		const char *name;
	} flags[] = {
		{ 0x0001, "FASTLOAD"   },
		{ 0x0002, "TTRAMLOAD"  },
		{ 0x0004, "TTRAMMEM"   },
		{ 0x0008, "MINIMUM"    }, /* MagiC */
		{ 0x1000, "SHAREDTEXT" }
	};
	const char *info;
	int i;

	switch (tabletype) {
	case 0x4D694E54:	/* "MiNT" */
		info = "GCC/MiNT executable, GST symbol table";
		break;
	case 0x0:
		info = "TOS executable, DRI / GST symbol table";
		break;
	default:
		fprintf(stderr, "ERROR: unknown executable type 0x%x!\n", tabletype);
		return false;
	}
	fprintf(stderr, "%s, reloc=%d, program flags:", info, relocflag);
	/* bit flags */
	for (i = 0; i < ARRAYSIZE(flags); i++) {
		if (prgflags & flags[i].flag) {
			fprintf(stderr, " %s", flags[i].name);
		}
	}
	/* memory protection flags */
	switch((prgflags >> 4) & 3) {
		case 0:	info = "PRIVATE";  break;
		case 1: info = "GLOBAL";   break;
		case 2: info = "SUPER";    break;
		case 3: info = "READONLY"; break;
	}
	fprintf(stderr, " %s (0x%x)\n", info, prgflags);
	return true;
}

/**
 * Parse program header and use symbol table format specific loader
 * loader function to load the symbols.
 * Return symbols list or NULL for failure.
 */
static symbol_list_t* symbols_load_binary(FILE *fp, symtype_t gettype)
{
	Uint32 textlen, datalen, bsslen, start, tablesize, tabletype, prgflags;
	prg_section_t sections[3];
	int offset, reads = 0;
	Uint16 relocflag;
	symbol_list_t* symbols;

	/* get TEXT, DATA & BSS section sizes */
	fseek(fp, 2, SEEK_SET);
	reads += fread(&textlen, sizeof(textlen), 1, fp);
	textlen = SDL_SwapBE32(textlen);
	reads += fread(&datalen, sizeof(datalen), 1, fp);
	datalen = SDL_SwapBE32(datalen);
	reads += fread(&bsslen, sizeof(bsslen), 1, fp);
	bsslen = SDL_SwapBE32(bsslen);

	/* get symbol table size & type and check that all reads succeeded */
	reads += fread(&tablesize, sizeof(tablesize), 1, fp);
	tablesize = SDL_SwapBE32(tablesize);
	reads += fread(&tabletype, sizeof(tabletype), 1, fp);
	tabletype = SDL_SwapBE32(tabletype);

	/* get program header and whether there's reloc table */
	reads += fread(&prgflags, sizeof(prgflags), 1, fp);
	prgflags = SDL_SwapBE32(prgflags);
	reads += fread(&relocflag, sizeof(relocflag), 1, fp);
	relocflag = SDL_SwapBE32(relocflag);
	
	if (reads != 7) {
		fprintf(stderr, "ERROR: program header reading failed!\n");
		return NULL;
	}
	if (!symbols_print_prg_info(tabletype, prgflags, relocflag)) {
		return NULL;
	}
	if (!tablesize) {
		fprintf(stderr, "ERROR: symbol table missing from the program!\n");
		return NULL;
	}

	/* offsets & max sizes for running program TEXT/DATA/BSS section symbols */
	start = DebugInfo_GetTEXT();
	if (!start) {
		fprintf(stderr, "ERROR: no valid program basepage!\n");
		return NULL;
	}
	sections[0].offset = start;
	sections[0].end = start + textlen;
	if (DebugInfo_GetTEXTEnd() != sections[0].end - 1) {
		fprintf(stderr, "ERROR: given program TEXT section size differs from one in RAM!\n");
		return NULL;
	}

	start = DebugInfo_GetDATA();
	sections[1].offset = start;
	sections[1].end = start + datalen - 1;

	start = DebugInfo_GetBSS();
	sections[2].offset = start;
	sections[2].end = start + bsslen - 1;

	/* go to start of symbol table */
	offset = 0x1C + textlen + datalen;
	if (fseek(fp, offset, SEEK_SET) < 0) {
		perror("ERROR: seeking to symbol table failed");
		return NULL;
	}
	fprintf(stderr, "Trying to load symbol table at offset 0x%x...\n", offset);
	symbols = symbols_load_dri(fp, sections, gettype, tablesize);

	if (symbols == INVALID_SYMBOL_OFFSETS && fseek(fp, offset, SEEK_SET) == 0) {
		fprintf(stderr, "Re-trying with TEXT-relative BSS/DATA section offsets...\n");
		start = DebugInfo_GetTEXT();
		sections[1].offset = start;
		sections[2].offset = start;
		sections[1].end += textlen;
		sections[2].end += (textlen + datalen);
		symbols = symbols_load_dri(fp, sections, gettype, tablesize);
	}
	if (symbols == INVALID_SYMBOL_OFFSETS) {
		return NULL;
	}
	return symbols;
}

/**
 * Load symbols of given type and the symbol address addresses from
 * the given ASCII file and add given offsets to the addresses.
 * Return symbols list or NULL for failure.
 */
static symbol_list_t* symbols_load_ascii(FILE *fp, Uint32 *offsets, Uint32 maxaddr, symtype_t gettype)
{
	symbol_list_t *list;
	char symchar, buffer[128], name[MAX_SYM_SIZE+1], *buf;
	int count, line, symbols;
	Uint32 address, offset;
	symtype_t symtype;

	/* count content lines */
	symbols = 0;
	while (fgets(buffer, sizeof(buffer), fp)) {
		/* skip comments (AHCC SYM file comments start with '*') */
		if (*buffer == '#' || *buffer == '*') {
			continue;
		}
		/* skip empty lines */
		for (buf = buffer; isspace((unsigned char)*buf); buf++);
		if (!*buf) {
			continue;
		}
		symbols++;
	}
	if (!symbols) {
		fprintf(stderr, "ERROR: no symbols.\n");
	}

	fseek(fp, 0, SEEK_SET);

	/* allocate space for symbol list & names */
	if (!(list = symbol_list_alloc(symbols))) {
		return NULL;
	}

	/* read symbols */
	count = 0;
	for (line = 1; fgets(buffer, sizeof(buffer), fp); line++) {
		/* skip comments (AHCC SYM file comments start with '*') */
		if (*buffer == '#' || *buffer == '*') {
			continue;
		}
		/* skip empty lines */
		for (buf = buffer; isspace((unsigned char)*buf); buf++);
		if (!*buf) {
			continue;
		}
		assert(count < symbols); /* file not modified in meanwhile? */
		if (sscanf(buffer, "%x %c %32[0-9A-Za-z_.-]s", &address, &symchar, name) != 3) {
			fprintf(stderr, "WARNING: syntax error on line %d, skipping.\n", line);
			continue;
		}
		switch (toupper((unsigned char)symchar)) {
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
			fprintf(stderr, "WARNING: unrecognized symbol type '%c' on line %d, skipping.\n", symchar, line);
			continue;
		}
		if (!(gettype & symtype)) {
			continue;
		}
		address += offset;
		if (address > maxaddr) {
			fprintf(stderr, "WARNING: invalid address 0x%x on line %d, skipping.\n", address, line);
			continue;
		}
		list->names[count].address = address;
		list->names[count].type = symtype;
		list->names[count].name = strdup(name);
		assert(list->names[count].name);
		count++;
	}
	list->symbols = symbols;
	list->count = count;
	return list;
}

/**
 * Load symbols of given type and the symbol address addresses from
 * the given file and add given offsets to the addresses.
 * Return symbols list or NULL for failure.
 */
static symbol_list_t* Symbols_Load(const char *filename, Uint32 *offsets, Uint32 maxaddr)
{
	symbol_list_t *list;
	FILE *fp;

	if (!File_Exists(filename)) {
		fprintf(stderr, "ERROR: file '%s' doesn't exist or isn't readable!\n", filename);
		return NULL;
	}
	if (Opt_IsAtariProgram(filename)) {
		const char *last = CurrentProgramPath;
		if (!last) {
			/* "pc=text" breakpoint used as point for loading program symbols gives false hits during bootup */
			fprintf(stderr, "WARNING: no program loaded yet (through GEMDOS HD emu)!\n");
		} else if (strcmp(last, filename) != 0) {
			fprintf(stderr, "WARNING: given program doesn't match last program executed by GEMDOS HD emulation:\n\t%s\n", last);
		}
		fprintf(stderr, "Reading symbols from program '%s' symbol table...\n", filename);
		fp = fopen(filename, "rb");
		list = symbols_load_binary(fp, SYMTYPE_ALL);
		SymbolsAreForProgram = true;
	} else {
		fprintf(stderr, "Reading 'nm' style ASCII symbols from '%s'...\n", filename);
		fp = fopen(filename, "r");
		list = symbols_load_ascii(fp, offsets, maxaddr, SYMTYPE_ALL);
		SymbolsAreForProgram = false;
	}
	fclose(fp);

	if (!list) {
		fprintf(stderr, "ERROR: reading symbols from '%s' failed!\n", filename);
		return NULL;
	}

	if (list->count < list->symbols) {
		if (!list->count) {
			fprintf(stderr, "ERROR: no valid symbols in '%s', loading failed!\n", filename);
			symbol_list_free(list);
			return NULL;
		}
		/* parsed less than there were "content" lines */
		list->names = realloc(list->names, list->count * sizeof(symbol_t));
		assert(list->names);
	}

	/* copy name list to address list */
	list->addresses = malloc(list->count * sizeof(symbol_t));
	assert(list->addresses);
	memcpy(list->addresses, list->names, list->count * sizeof(symbol_t));

	/* sort both lists, with different criteria */
	qsort(list->addresses, list->count, sizeof(symbol_t), symbols_by_address);
	qsort(list->names, list->count, sizeof(symbol_t), symbols_by_name);

	fprintf(stderr, "Loaded %d symbols from '%s'.\n", list->count, filename);
	return list;
}


/**
 * Free read symbols.
 */
static void Symbols_Free(symbol_list_t* list)
{
	int i;

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
	static int i, len;
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
const char* Symbols_GetByDspAddress(Uint32 addr)
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
int Symbols_GetDspAddressIndex(Uint32 addr)
{
	return Symbols_SearchByAddress(DspSymbolsList, addr);	
}

/**
 * Return how many symbols are loaded/available
 */
int Symbols_CpuCount(void)
{
	return (CpuSymbolsList ? CpuSymbolsList->count : 0);
}
int Symbols_DspCount(void)
{
	return (DspSymbolsList ? DspSymbolsList->count : 0);
}

/* ---------------- symbol showing ------------------ */

/**
 * Show symbols from given list with paging.
 */
static void Symbols_Show(symbol_list_t* list, const char *sorttype)
{
	symbol_t *entry, *entries;
	char symchar;
	int i;
	
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
		symchar = symbol_char(entry->type);
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

/* ---------------- binary load handling ------------------ */


/**
 * Remove last opened program path.
 */
void Symbols_RemoveCurrentProgram(void)
{
	if (CurrentProgramPath) {
		free(CurrentProgramPath);
		CurrentProgramPath = NULL;

		if (SymbolsAreForProgram) {
			Symbols_Free(CpuSymbolsList);
			CpuSymbolsList = NULL;
		}
	}
	AutoLoadFailed = false;
}

/**
 * Set last opened program path.
 */
void Symbols_ChangeCurrentProgram(const char *path)
{
	if (Opt_IsAtariProgram(path)) {
		Symbols_RemoveCurrentProgram();
		CurrentProgramPath = strdup(path);
	}
}

/**
 * Load symbols for last opened program.
 */
void Symbols_LoadCurrentProgram(void)
{
	/* symbols already loaded, program path missing or previous load failed? */
	if (CpuSymbolsList || !CurrentProgramPath || AutoLoadFailed) {
		return;
	}
	CpuSymbolsList = Symbols_Load(CurrentProgramPath, NULL, 0);
	if (!CpuSymbolsList) {
		AutoLoadFailed = true;
	} else {
		AutoLoadFailed = false;
	}
}

/* ---------------- command parsing ------------------ */

/**
 * Readline match callback to list symbols subcommands.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char *Symbols_MatchCommand(const char *text, int state)
{
	static const char* subs[] = {
		"addr", "free", "name", "prg"
	};
	return DebugUI_MatchHelper(subs, ARRAYSIZE(subs), text, state);
}

const char Symbols_Description[] =
	"<filename|prg|addr|name|free> [<T offset> [<D offset> <B offset>]]\n"
	"\tLoads symbol names and their addresses from the given file.\n"
	"\tIf there were previously loaded symbols, they're replaced.\n"
	"\n"
	"\tGiving 'prg' instead of a file name, loads DRI/GST symbol table\n"
	"\tfrom the last program executed through the GEMDOS HD emulation.\n"
	"\n"
	"\tGiving either 'name' or 'addr' instead of a file name, will\n"
	"\tlist the currently loaded symbols.  Giving 'free' will remove\n"
	"\tthe loaded symbols.\n"
	"\n"
	"\tIf one base address/offset is given, its added to all addresses.\n"
	"\tIf three offsets are given (and non-zero), they're applied to\n"
	"\ttext (T), data (D) and BSS (B) symbols.  Given offsets are used\n"
	"\tonly when loading ASCII symbol files.";

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
		if ( ConfigureParams.System.bAddressSpace24 )
			maxaddr = 0x00FFFFFF;
		else
			maxaddr = 0xFFFFFFFF;
	} else {
		listtype = TYPE_NONE;
		maxaddr = 0;
	}
	if (nArgc < 2 || listtype == TYPE_NONE) {
		return DebugUI_PrintCmdHelp(psArgs[0]);
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

	if (strcmp(file, "prg") == 0) {
		file = CurrentProgramPath;
		if (!file) {
			fprintf(stderr, "ERROR: no program loaded (through GEMDOS HD emu)!\n");
			return DEBUGGER_CMDDONE;
		}
	}
	list = Symbols_Load(file, offsets, maxaddr);
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
