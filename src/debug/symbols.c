/*
 * Hatari - symbols.c
 * 
 * Copyright (C) 2010-2019 by Eero Tamminen
 * 
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 * 
 * symbols.c - Hatari debugger symbol/address handling; parsing, sorting,
 * matching, TAB completion support etc.
 * 
 * Symbol/address information is read either from:
 * - A program file's DRI/GST or a.out format symbol table, or
 * - ASCII file which contents are subset of "nm" output i.e. composed of
 *   a hexadecimal addresses followed by a space, letter indicating symbol
 *   type (T = text/code, D = data, B = BSS), space and the symbol name.
 *   Empty lines and lines starting with '#' are ignored.  It's AHCC SYM
 *   output compatible.
 */
const char Symbols_fileid[] = "Hatari symbols.c";

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
#include "a.out.h"

#include "symbols-common.c"

/* how many characters the symbol name can have.
 * NOTE: change also sscanf width arg if you change this!!!
 */
#define MAX_SYM_SIZE 32

/* TODO: add symbol name/address file names to configuration? */
static symbol_list_t *CpuSymbolsList;
static symbol_list_t *DspSymbolsList;

/* path for last loaded program (through GEMDOS HD emulation) */
static char *CurrentProgramPath;
/* whether current symbols were loaded from a program file */
static bool SymbolsAreForProgram;
/* prevent repeated failing on every debugger invocation */
static bool AutoLoadFailed;


/**
 * Load symbols of given type and the symbol address addresses from
 * the given ASCII file and add given offsets to the addresses.
 * Return symbols list or NULL for failure.
 */
static symbol_list_t* symbols_load_ascii(FILE *fp, Uint32 *offsets, Uint32 maxaddr, symtype_t gettype)
{
	symbol_list_t *list;
	char symchar, buffer[128], name[MAX_SYM_SIZE+1], *buf;
	int count, line, symbols, weak, unknown, invalid;
	Uint32 address, offset;
	symtype_t symtype;

	/* count content lines */
	line = symbols = 0;
	while (fgets(buffer, sizeof(buffer), fp)) {
		line++;

		/* skip comments (AHCC SYM file comments start with '*') */
		if (*buffer == '#' || *buffer == '*') {
			continue;
		}
		/* skip empty lines */
		for (buf = buffer; isspace((unsigned char)*buf); buf++);
		if (!*buf) {
			continue;
		}
		if (!isxdigit((unsigned char)*buf)) {
			fprintf(stderr, "ERROR: line %d doesn't start with an address.\n", line);
			return NULL;
		}
		symbols++;
	}
	if (!symbols) {
		fprintf(stderr, "ERROR: no symbols.\n");
		return NULL;
	}

	fseek(fp, 0, SEEK_SET);

	/* allocate space for symbol list & names */
	if (!(list = symbol_list_alloc(symbols))) {
		return NULL;
	}

	/* read symbols */
	invalid = unknown = weak = count = 0;
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
		case 'R':	/* ELF 'nm' symbol type, read only */
		case 'D':
			symtype = SYMTYPE_DATA;
			offset = offsets[1];
			break;
		case 'B':
			symtype = SYMTYPE_BSS;
			offset = offsets[2];
			break;
		case 'A':
			symtype = SYMTYPE_ABS;
			offset = 0;
			break;
		case 'W':	/* ELF 'nm' symbol type, weak */
		case 'V':
			weak++;
			continue;
		default:
			fprintf(stderr, "WARNING: unrecognized symbol type '%c' on line %d, skipping.\n", symchar, line);
			unknown++;
			continue;
		}
		if (!(gettype & symtype)) {
			continue;
		}
		address += offset;
		if (address > maxaddr) {
			fprintf(stderr, "WARNING: invalid address 0x%x on line %d, skipping.\n", address, line);
			invalid++;
			continue;
		}
		list->names[count].address = address;
		list->names[count].type = symtype;
		list->names[count].name = strdup(name);
		assert(list->names[count].name);
		count++;
	}
	if (invalid) {
		fprintf(stderr, "NOTE: ignored %d symbols with invalid addresses.\n", invalid);
	}
	if (unknown) {
		fprintf(stderr, "NOTE: ignored %d symbols with unknown types.\n", unknown);
	}
	if (weak) {
		/* TODO: accept & mark them as weak and silently override them on address conflicts? */
		fprintf(stderr, "NOTE: ignored %d weak symbols.\n", weak);
	}
	list->symbols = symbols;
	list->namecount = count;
	return list;
}

/**
 * Remove full duplicates from the sorted names list
 * and trim the allocation to remaining symbols
 */
static void symbols_trim_names(symbol_list_t* list)
{
	symbol_t *sym = list->names;
	int i, next, count, dups;

	count = list->namecount;
	for (dups = i = 0; i < count - 1; i++) {
		next = i + 1;
		if (strcmp(sym[i].name, sym[next].name) == 0 &&
		    sym[i].address == sym[next].address &&
		    sym[i].type == sym[next].type) {
			/* remove duplicate */
			memmove(sym+i, sym+next, (count-next) * sizeof(symbol_t));
			count--;
			dups++;
		}
	}
	if (dups || list->namecount < list->symbols) {
		list->names = realloc(list->names, i * sizeof(symbol_t));
		assert(list->names);
		list->namecount = i;
	}
	if (dups) {
		fprintf(stderr, "WARNING: removed %d complete symbol duplicates\n", dups);
	}
}

/**
 * Separate TEXT symbols from other symbols in address list.
 */
static void symbols_trim_addresses(symbol_list_t* list)
{
	symbol_t *sym = list->addresses;
	int i;

	for (i = 0; i < list->namecount; i++) {
		if (sym[i].type != SYMTYPE_TEXT) {
			break;
		}
	}
	list->codecount = i;
	list->datacount = list->namecount - i;
}

/**
 * Set sections to match running process by adding TEXT/DATA/BSS
 * start addresses to section offsets and ends, and return true if
 * results match it.
 */
static bool update_sections(prg_section_t *sections)
{
	/* offsets & max sizes for running program TEXT/DATA/BSS section symbols */
	Uint32 start = DebugInfo_GetTEXT();
	if (!start) {
		fprintf(stderr, "ERROR: no valid program basepage!\n");
		return false;
	}
	sections[0].offset = start;
	sections[0].end += start;
	if (DebugInfo_GetTEXTEnd() != sections[0].end) {
		fprintf(stderr, "ERROR: given program TEXT section size differs from one in RAM!\n");
		return false;
	}

	start = DebugInfo_GetDATA();
	sections[1].offset = start;
	if (sections[1].offset != sections[0].end) {
		fprintf(stderr, "WARNING: DATA start doesn't match TEXT start + size!\n");
	}
	sections[1].end += start;

	start = DebugInfo_GetBSS();
	sections[2].offset = start;
	if (sections[2].offset != sections[1].end) {
		fprintf(stderr, "WARNING: BSS start doesn't match DATA start + size!\n");
	}
	sections[2].end += start;

	return true;
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
		symbol_opts_t opts;
		const char *last = CurrentProgramPath;
		if (!last) {
			/* "pc=text" breakpoint used as point for loading program symbols gives false hits during bootup */
			fprintf(stderr, "WARNING: no program loaded yet (through GEMDOS HD emu)!\n");
		} else if (strcmp(last, filename) != 0) {
			fprintf(stderr, "WARNING: given program doesn't match last program executed by GEMDOS HD emulation:\n\t%s\n", last);
		}
		fprintf(stderr, "Reading symbols from program '%s' symbol table...\n", filename);
		fp = fopen(filename, "rb");
		opts.notypes = 0;
		opts.no_obj = true;
		opts.no_local = true;
		list = symbols_load_binary(fp, &opts, update_sections);
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

	if (!list->namecount) {
		fprintf(stderr, "ERROR: no valid symbols in '%s', loading failed!\n", filename);
		symbol_list_free(list);
		return NULL;
	}

	/* sort and trim names list */
	qsort(list->names, list->namecount, sizeof(symbol_t), symbols_by_name);
	symbols_trim_names(list);

	/* copy name list to address list */
	list->addresses = malloc(list->namecount * sizeof(symbol_t));
	assert(list->addresses);
	memcpy(list->addresses, list->names, list->namecount * sizeof(symbol_t));

	/* sort address list and trim to contain just TEXT symbols */
	qsort(list->addresses, list->namecount, sizeof(symbol_t), symbols_by_address);
	symbols_trim_addresses(list);

	/* skip verbose output when symbols are auto-loaded */
	if (ConfigureParams.Debugger.bSymbolsAutoLoad) {
		fprintf(stderr, "Skipping duplicate address & symbol name checks when autoload is enabled.\n");
	} else {
		/* check for duplicate names */
		if (symbols_check_names(list->names, list->namecount)) {
			fprintf(stderr, "-> Hatari symbol expansion can match only one of the addresses for name duplicates!\n");
		}
		/* check for duplicate TEXT & other addresses */
		if (symbols_check_addresses(list->addresses, list->codecount)) {
			fprintf(stderr, "-> Hatari profile/disassembly will show only one of the TEXT symbols for given address!\n");
		}
		if (symbols_check_addresses(list->addresses + list->codecount, list->datacount)) {
			fprintf(stderr, "-> Hatari disassembly will show only one of the symbols for given address!\n");
		}
	}

	fprintf(stderr, "Loaded %d symbols (%d TEXT) from '%s'.\n",
		list->namecount, list->codecount, filename);
	return list;
}


/**
 * Free read symbols.
 */
static void Symbols_Free(symbol_list_t* list)
{
	symbol_list_free(list);
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
	while (i < list->namecount) {
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
	if (ConfigureParams.Debugger.bMatchAllSymbols) {
		return Symbols_MatchByName(CpuSymbolsList, SYMTYPE_ALL, text, state);
	} else {
		return Symbols_MatchByName(CpuSymbolsList, SYMTYPE_TEXT, text, state);
	}
}
char* Symbols_MatchCpuDataAddress(const char *text, int state)
{
	if (ConfigureParams.Debugger.bMatchAllSymbols) {
		return Symbols_MatchByName(CpuSymbolsList, SYMTYPE_ALL, text, state);
	} else {
		return Symbols_MatchByName(CpuSymbolsList, SYMTYPE_DATA|SYMTYPE_BSS, text, state);
	}
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
 * Binary search symbol of given type by name.
 * Return symbol if name matches, zero otherwise.
 */
static const symbol_t* Symbols_SearchByName(symbol_t* entries, int count, symtype_t symtype, const char *name)
{
	/* left, right, middle */
        int l, r, m, dir;

	/* bisect */
	l = 0;
	r = count - 1;
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
 * Set given symbol's address to variable and return true if one
 * was found from given list.
 */
static bool Symbols_GetAddress(symbol_list_t* list, symtype_t symtype, const char *name, Uint32 *addr)
{
	const symbol_t *entry;
	if (!(list && list->names)) {
		return false;
	}
	entry = Symbols_SearchByName(list->names, list->namecount, symtype, name);
	if (entry) {
		*addr = entry->address;
		return true;
	}
	return false;
}
bool Symbols_GetCpuAddress(symtype_t symtype, const char *name, Uint32 *addr)
{
	return Symbols_GetAddress(CpuSymbolsList, symtype, name, addr);
}
bool Symbols_GetDspAddress(symtype_t symtype, const char *name, Uint32 *addr)
{
	return Symbols_GetAddress(DspSymbolsList, symtype, name, addr);
}


/* ---------------- symbol address -> name search ------------------ */

/**
 * Binary search TEXT symbol by address in given sorted list.
 * Return index for symbol which address matches or precedes
 * the given one.
 */
static int Symbols_SearchBeforeAddress(symbol_t* entries, int count, Uint32 addr)
{
	/* left, right, middle */
        int l, r, m;
	Uint32 curr;

	/* bisect */
	l = 0;
	r = count - 1;
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
	return r;
}

static const char* Symbols_GetBeforeAddress(symbol_list_t *list, Uint32 *addr)
{
	if (!(list && list->addresses)) {
		return NULL;
	}
	int i = Symbols_SearchBeforeAddress(list->addresses, list->codecount, *addr);
	if (i >= 0) {
		*addr = list->addresses[i].address;
		return list->addresses[i].name;
	}
	return NULL;
}
const char* Symbols_GetBeforeCpuAddress(Uint32 *addr)
{
	return Symbols_GetBeforeAddress(CpuSymbolsList, addr);
}
const char* Symbols_GetBeforeDspAddress(Uint32 *addr)
{
	return Symbols_GetBeforeAddress(DspSymbolsList, addr);
}

/**
 * Binary search symbol by address in given sorted list.
 * Return symbol index if address matches, -1 otherwise.
 *
 * Performance critical, called on every instruction
 * when profiling is enabled.
 */
static int Symbols_SearchByAddress(symbol_t* entries, int count, Uint32 addr)
{
	/* left, right, middle */
        int l, r, m;
	Uint32 curr;

	/* bisect */
	l = 0;
	r = count - 1;
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
 * Search symbol in given list by type & address.
 * Return symbol name if there's a match, NULL otherwise.
 * TEXT symbols will be matched before other symbol types.
 * Returned name is valid only until next Symbols_* function call.
 */
static const char* Symbols_GetByAddress(symbol_list_t* list, Uint32 addr, symtype_t type)
{
	if (!(list && list->addresses)) {
		return NULL;
	}
	if (type & SYMTYPE_TEXT) {
		int i = Symbols_SearchByAddress(list->addresses, list->codecount, addr);
		if (i >= 0) {
			return list->addresses[i].name;
		}
	}
	if (type & ~SYMTYPE_TEXT) {
		int i = Symbols_SearchByAddress(list->addresses + list->codecount, list->datacount, addr);
		if (i >= 0) {
			return list->addresses[list->codecount + i].name;
		}
	}
	return NULL;
}
const char* Symbols_GetByCpuAddress(Uint32 addr, symtype_t type)
{
	return Symbols_GetByAddress(CpuSymbolsList, addr, type);
}
const char* Symbols_GetByDspAddress(Uint32 addr, symtype_t type)
{
	return Symbols_GetByAddress(DspSymbolsList, addr, type);
}

/**
 * Search given list for TEXT symbol by address.
 * Return symbol index if address matches, -1 otherwise.
 */
static int Symbols_GetCodeIndex(symbol_list_t* list, Uint32 addr)
{
	if (!list) {
		return -1;
	}
	return Symbols_SearchByAddress(list->addresses, list->codecount, addr);
}
int Symbols_GetCpuCodeIndex(Uint32 addr)
{
	return Symbols_GetCodeIndex(CpuSymbolsList, addr);
}
int Symbols_GetDspCodeIndex(Uint32 addr)
{
	return Symbols_GetCodeIndex(DspSymbolsList, addr);
}

/**
 * Return how many TEXT symbols are loaded/available
 */
int Symbols_CpuCodeCount(void)
{
	return (CpuSymbolsList ? CpuSymbolsList->codecount : 0);
}
int Symbols_DspCodeCount(void)
{
	return (DspSymbolsList ? DspSymbolsList->codecount : 0);
}

/* ---------------- symbol showing ------------------ */

/**
 * Show symbols from given list with paging.
 */
static void Symbols_Show(symbol_list_t* list, const char *sortcmd)
{
	symbol_t *entry, *entries;
	const char *symtype, *sorttype;
	int i, rows, count;
	char symchar;
	char line[80];
	
	if (!list) {
		fprintf(stderr, "No symbols!\n");
		return;
	}

	if (strcmp("code", sortcmd) == 0) {
		sorttype = "address";
		entries = list->addresses;
		count = list->codecount;
		symtype = " TEXT";
	} else if (strcmp("data", sortcmd) == 0) {
		sorttype = "address";
		entries = list->addresses + list->codecount;
		count = list->datacount;
		symtype = " DATA/BSS/ABS";
	} else {
		sorttype = "name";
		entries = list->names;
		count = list->namecount;
		symtype = "";
	}
	rows = DebugUI_GetPageLines(ConfigureParams.Debugger.nSymbolLines, 20);

	for (entry = entries, i = 0; i < count; i++, entry++) {
		symchar = symbol_char(entry->type);
		fprintf(stderr, "0x%08x %c %s\n",
			entry->address, symchar, entry->name);
		if ((i + 1) % rows == 0) {
			fprintf(stderr, "--- q to exit listing, just enter to continue --- ");
			if (fgets(line, sizeof(line), stdin) == NULL ||
				toupper(line[0]) == 'Q') {
				break;
			}
		}
	}
	fprintf(stderr, "%d %s%s symbols (of %d) sorted by %s.\n", i,
		(list == CpuSymbolsList ? "CPU" : "DSP"),
		symtype, count, sorttype);
}

/* ---------------- binary load handling ------------------ */

/**
 * If autoloading is enabled and program symbols are present,
 * remove them along with program path.
 *
 * Called on GEMDOS reset and when program terminates
 * (unless terminated with Ptermres()).
 */
void Symbols_RemoveCurrentProgram(void)
{
	if (CurrentProgramPath) {
		free(CurrentProgramPath);
		CurrentProgramPath = NULL;

		if (CpuSymbolsList && SymbolsAreForProgram && ConfigureParams.Debugger.bSymbolsAutoLoad) {
			Symbols_Free(CpuSymbolsList);
			fprintf(stderr, "Program exit, removing its symbols.\n");
			CpuSymbolsList = NULL;
		}
	}
	AutoLoadFailed = false;
}

/**
 * Call Symbols_RemoveCurrentProgram() and
 * set last opened program path.
 *
 * Called on first Fopen() after Pexec().
 */
void Symbols_ChangeCurrentProgram(const char *path)
{
	if (Opt_IsAtariProgram(path)) {
		Symbols_RemoveCurrentProgram();
		CurrentProgramPath = strdup(path);
	}
}

/*
 * Show currently set program path
 */
void Symbols_ShowCurrentProgramPath(FILE *fp)
{
	if (CurrentProgramPath) {
		fprintf(fp, "Current program path: %s\n", CurrentProgramPath);
	} else {
		fputs("No program has been loaded (through GEMDOS HD).\n", fp);
	}
}

/**
 * Load symbols for last opened program when symbol autoloading is enabled.
 * Called when debugger is invoked.
 */
void Symbols_LoadCurrentProgram(void)
{
	if (!ConfigureParams.Debugger.bSymbolsAutoLoad) {
		return;
	}
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
		"autoload", "code", "data", "free", "match", "name", "prg"
	};
	return DebugUI_MatchHelper(subs, ARRAY_SIZE(subs), text, state);
}

const char Symbols_Description[] =
	"<code|data|name> -- list symbols\n"
	"\tsymbols <prg|free> -- load/free symbols\n"
	"\t        <filename> [<T offset> [<D offset> <B offset>]]\n"
	"\tsymbols <autoload|match> -- toggle symbol options\n"
	"\n"
	"\t'name' command lists the currently loaded symbols, sorted by name.\n"
	"\t'code' and 'data' commands list them sorted by address; 'code' lists\n"
	"\tonly TEXT symbols, 'data' lists DATA/BSS/ABS symbols.\n"
	"\n"
	"\tBy default, symbols are loaded from the currently executing program's\n"
	"\tbinary when entering the debugger, IF program is started through\n"
	"\tGEMDOS HD, and they're freed when that program terminates.\n"
	"\n"
	"\tThat corresponds to 'prg' command which loads (DRI/GST or a.out\n"
	"\tformat) symbol table from the last program executed through\n"
	"\tthe GEMDOS HD emulation.\n"
	"\n"
	"\t'free' command removes the loaded symbols.\n"
	"\n"
	"\tIf program lacks symbols, or it's not run through the GEMDOS HD\n"
	"\temulation, user can ask symbols to be loaded from a file that's\n"
	"\tan unstripped version of the binary. Or from an ASCII symbols file\n"
	"\tproduced by the 'nm' and (Hatari) 'gst2ascii' tools.\n"
	"\n"
	"\tWith ASCII symbols files, given non-zero offset(s) are added to\n"
	"\tthe text (T), data (D) and BSS (B) symbols.  Typically one uses\n"
	"\tTEXT variable, sometimes also DATA & BSS, variables for this.\n"
	"\n"
	"\t'autoload [on|off]' command toggle/set whether debugger will load\n"
	"\tsymbols for currently executing (GEMDOS HD) program automatically\n"
	"\ton entering the debugger (i.e. replace earlier loaded symbols),\n"
	"\tand free them when program terminates.  It needs to be disabled\n"
	"\tto debug memory-resident programs used by other programs.\n"
	"\n"
	"\t'match' command toggles whether TAB completion matches all symbols,\n"
	"\tor only symbol types that should be relevant for given command.";


/**
 * Handle debugger 'symbols' command and its arguments
 */
int Symbols_Command(int nArgc, char *psArgs[])
{
	enum { TYPE_CPU, TYPE_DSP } listtype;
	Uint32 offsets[3], maxaddr;
	symbol_list_t *list;
	const char *file;
	int i;

	if (strcmp("dspsymbols", psArgs[0]) == 0) {
		listtype = TYPE_DSP;
		maxaddr = 0xFFFF;
	} else {
		listtype = TYPE_CPU;
		if ( ConfigureParams.System.bAddressSpace24 )
			maxaddr = 0x00FFFFFF;
		else
			maxaddr = 0xFFFFFFFF;
	}
	if (nArgc < 2) {
		file = "name";
	} else {
		file = psArgs[1];
	}

	/* set whether to autoload symbols on program start and
	 * discard them when program terminates with GEMDOS HD,
	 * or whether they need to be loaded manually.
	 */
	if (strcmp(file, "autoload") == 0) {
		bool value;
		if (nArgc < 3) {
			value = !ConfigureParams.Debugger.bSymbolsAutoLoad;
		} else if (strcmp(psArgs[2], "on") == 0) {
			value = true;
		} else if (strcmp(psArgs[2], "off") == 0) {
			value = false;
		} else {
			DebugUI_PrintCmdHelp(psArgs[0]);
			return DEBUGGER_CMDDONE;
		}
		fprintf(stderr, "Program symbols auto-loading AND freeing (with GEMDOS HD) is %s\n",
		        value ? "ENABLED." : "DISABLED!");
		ConfigureParams.Debugger.bSymbolsAutoLoad = value;
		return DEBUGGER_CMDDONE;
	}

	/* toggle whether all or only specific symbols types get TAB completed? */
	if (strcmp(file, "match") == 0) {
		ConfigureParams.Debugger.bMatchAllSymbols = !ConfigureParams.Debugger.bMatchAllSymbols;
		if (ConfigureParams.Debugger.bMatchAllSymbols) {
			fprintf(stderr, "Matching all symbols types.\n");
		} else {
			fprintf(stderr, "Matching only symbols (most) relevant for given command.\n");
		}
		return DEBUGGER_CMDDONE;
	}

	/* show requested symbol types in requested order? */
	if (strcmp(file, "name") == 0 || strcmp(file, "code") == 0 || strcmp(file, "data") == 0) {
		list = (listtype == TYPE_DSP ? DspSymbolsList : CpuSymbolsList);
		Symbols_Show(list, file);
		return DEBUGGER_CMDDONE;
	}

	/* free symbols? */
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
	for (i = 0; i < ARRAY_SIZE(offsets); i++) {
		if (i+2 < nArgc) {
			int dummy;
			Eval_Expression(psArgs[i+2], &(offsets[i]), &dummy, listtype==TYPE_DSP);
		} else {
			/* default to first (text) offset */
			offsets[i] = offsets[0];
		}
	}

	/* load symbols from GEMDOS HD program? */
	if (strcmp(file, "prg") == 0) {
		file = CurrentProgramPath;
		if (!file) {
			fprintf(stderr, "ERROR: no program loaded (through GEMDOS HD emu)!\n");
			return DEBUGGER_CMDDONE;
		}
	}

	/* do actual loading */
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
