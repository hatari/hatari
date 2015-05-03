/*
 * Hatari - gst2ascii.c
 * 
 * Copyright (C) 2013-2015 by Eero Tamminen
 * 
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 * 
 * Convert DRI/GST symbol table in a binary into ASCII symbols file accepted
 * by Hatari debugger and its profiler data post-processor.  This will also
 * allow manual editing of the symbol table (removing irrelevant labels or
 * adding missing symbols for functions).
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#if defined(__MINT__)	/* assume MiNT/lib is always big-endian */
# define SDL_SwapBE16(x) x
# define SDL_SwapBE32(x) x
#else
# include <SDL_endian.h>
#endif
#include <assert.h>

typedef enum {
	SYMTYPE_TEXT = 1,
	SYMTYPE_DATA = 2,
	SYMTYPE_BSS  = 4
} symtype_t;

typedef struct {
	char *name;
	uint32_t address;
	symtype_t type;
} symbol_t;

typedef struct {
	int count;		/* final symbol count */
	int symbols;		/* initial symbol count */
	symbol_t *addresses;	/* items sorted by address */
	symbol_t *names;	/* items sorted by symbol name */
} symbol_list_t;

typedef struct {
	uint32_t offset;
	uint32_t end;
} prg_section_t;

/* ------------------ options & usage ------------------ */

#ifdef WIN32
#define PATHSEP '\\'
#else
#define PATHSEP '/'
#endif

#define ARRAYSIZE(x) (int)(sizeof(x)/sizeof(x[0]))

static const char *PrgPath;

static struct {
	symtype_t notypes;
	bool no_obj;
	bool no_local;
	bool sort_name;
} Options;

/*
 * Show program usage, given error message
 * return empty list
 */
static symbol_list_t* usage(const char *msg)
{
	const struct {
		const char opt;
		const char *desc;
	} OptInfo[] = {
		{ 'n', "sort by name (not address)" },
		{ 'b', "no BSS symbols" },
		{ 'd', "no DATA symbols" },
		{ 't', "no TEXT symbols" },
		{ 'l', "no local (.L*) symbols" },
		{ 'o', "no object symbols (filenames or GCC internals)" },
	};
	const char *name;
	int i;

	if ((name = strrchr(PrgPath, PATHSEP))) {
		name++;
	} else {
		name = PrgPath;
	}
	fprintf(stderr,
		"\n"
		"Usage: %s [options] <Atari program>\n"
		"\n"
		"Outputs given program DRI/GST symbol table content\n"
		"in ASCII format accepted by Hatari debugger and\n"
		"its profiler data post-processor.\n"
		"\n"
		"Options:\n", name);
	for (i = 0; i < ARRAYSIZE(OptInfo); i++) {
		fprintf(stderr, "\t-%c\t%s\n", OptInfo[i].opt, OptInfo[i].desc);
	}
	if (msg) {
		fprintf(stderr, "\nERROR: %s!\n", msg);
	}
	return NULL;
}

/* ------------------ load and free functions ------------------ */

/**
 * compare function for qsort() to sort according to symbol address
 */
static int symbols_by_address(const void *s1, const void *s2)
{
	uint32_t addr1 = ((const symbol_t*)s1)->address;
	uint32_t addr2 = ((const symbol_t*)s2)->address;

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
static symbol_list_t* symbols_load_dri(FILE *fp, prg_section_t *sections, uint32_t tablesize)
{
	int i, count, symbols, outside;
	int notypes, dtypes, locals, ofiles;
	prg_section_t *section;
	symbol_list_t *list;
	symtype_t symtype;
#define DRI_ENTRY_SIZE	14
	char name[23];
	uint16_t symid;
	uint32_t address;

	if (tablesize % DRI_ENTRY_SIZE) {
		fprintf(stderr, "ERROR: invalid DRI/GST symbol table size %d!\n", tablesize);
		return NULL;
	}
	symbols = tablesize / DRI_ENTRY_SIZE;
	if (!(list = symbol_list_alloc(symbols))) {
		return NULL;
	}

	outside = dtypes = notypes = ofiles = locals = count = 0;
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
		if (Options.notypes & symtype) {
			notypes++;
			continue;
		}
		if (Options.no_local) {
			if (name[0] == '.' && name[1] == 'L') {
				locals++;
				continue;
			}
		}
		if (Options.no_obj) {
			const char *gcc_sym[] = {
				"___gnu_compiled_c",
				"gcc2_compiled."
			};
			int j, len = strlen(name);
			/* object / file name? */
			if (len > 2 && ((name[len-2] == '.' && name[len-1] == 'o') || strchr(name, '/'))) {
				ofiles++;
				continue;
			}
			/* useless symbols GCC (v2) seems to add to every object? */
			for (j = 0; j < ARRAYSIZE(gcc_sym); j++) {
				if (strcmp(name, gcc_sym[j]) == 0) {
					ofiles++;
					j = -1;
					break;
				}
			}
			if (j < 0) {
				continue;
			}
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
	if (notypes) {
		fprintf(stderr, "NOTE: ignored %d unwanted symbol types.\n", notypes);
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
		fprintf(stderr, "NOTE: ignored %d object symbols (= name has '/', ends in '.o' or is GCC internal).\n", ofiles);
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
static symbol_list_t* symbols_load_binary(FILE *fp)
{
	Uint32 textlen, datalen, bsslen, tablesize, tabletype, prgflags;
	prg_section_t sections[3];
	int offset, reads = 0;
	Uint16 relocflag;
	symbol_list_t* symbols;

	/* get TEXT, DATA & BSS section sizes */
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

	/* symbols already have suitable offsets, so only acceptable end position needs to be calculated */
	sections[0].offset = 0;
	sections[0].end = textlen;
	sections[1].offset = 0;
	sections[1].end = datalen;
	sections[2].offset = 0;
	sections[2].end = bsslen;

	/* go to start of symbol table */
	offset = 0x1C + textlen + datalen;
	if (fseek(fp, offset, SEEK_SET) < 0) {
		perror("ERROR: seeking to symbol table failed");
		return NULL;
	}
	fprintf(stderr, "Trying to load symbol table at offset 0x%x...\n", offset);
	symbols = symbols_load_dri(fp, sections, tablesize);

	if (symbols == INVALID_SYMBOL_OFFSETS && fseek(fp, offset, SEEK_SET) == 0) {
		fprintf(stderr, "Re-trying with TEXT-relative BSS/DATA section offsets...\n");
		sections[1].end += textlen;
		sections[2].end += (textlen + datalen);
		symbols = symbols_load_dri(fp, sections, tablesize);
		if (symbols) {
			fprintf(stderr, "Load symbols without giving separate BSS/DATA offsets (they're TEXT relative).\n");
		}
	} else {
		fprintf(stderr, "Load symbols with 'symbols <filename> TEXT DATA BSS' after starting the program.\n");
	}
	if (!symbols || symbols == INVALID_SYMBOL_OFFSETS) {
		fprintf(stderr, "\n\n*** Try with 'nm -n <program>' (Atari/cross-compiler tool) instead ***\n\n");
		return NULL;
	}
	return symbols;
}

/**
 * Load symbols of given type and the symbol address addresses from
 * the given file and add given offsets to the addresses.
 * Return symbols list or NULL for failure.
 */
static symbol_list_t* symbols_load(const char *filename)
{
	symbol_list_t *list;
	uint16_t magic;
	FILE *fp;

	fprintf(stderr, "Reading symbols from program '%s' symbol table...\n", filename);
	if (!(fp = fopen(filename, "rb"))) {
		return usage("opening program file failed");
	}
	if (fread(&magic, sizeof(magic), 1, fp) != 1) {
		return usage("reading program file failed");
	}

	if (SDL_SwapBE16(magic) != 0x601A) {
		return usage("file isn't an Atari program file");
	}
	list = symbols_load_binary(fp);
	fclose(fp);

	if (!list) {
		return usage("no symbols, or reading them failed");
	}

	if (list->count < list->symbols) {
		if (!list->count) {
			return usage("no valid symbols in program, symbol table loading failed");
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
	return list;
}


/* ---------------- symbol showing & option parsing ------------------ */

/**
 * Show symbols sorted by selected option
 */
static int symbols_show(symbol_list_t* list)
{
	symbol_t *entry, *entries;
	char symchar;
	int i;
	
	if (!list) {
		fprintf(stderr, "No symbols!\n");
		return 1;
	}
	if (Options.sort_name) {
		entries = list->names;
	} else {
		entries = list->addresses;
	}
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
		fprintf(stdout, "0x%08x %c %s\n",
			entry->address, symchar, entry->name);
	}

	fprintf(stderr, "%d symbols processed.\n", list->count);
	return 0;
}

/**
 * parse program options and then call symbol load+save
 */
int main(int argc, const char *argv[])
{
	int i;

	PrgPath = *argv;
	for (i = 1; i+1 < argc; i++) {
		if (argv[i][0] != '-') {
			break;
		}
		switch(tolower((unsigned char)argv[i][1])) {
		case 'n':
			Options.sort_name = true;
		case 'b':
			Options.notypes |= SYMTYPE_BSS;
			break;
		case 'd':
			Options.notypes |= SYMTYPE_DATA;
			break;
		case 't':
			Options.notypes |= SYMTYPE_TEXT;
			break;
		case 'l':
			Options.no_local = true;
			break;
		case 'o':
			Options.no_obj = true;
			break;
		default:
			usage("unknown option");
			return 1;
		}
	}
	if (i+1 != argc) {
		usage("incorrect number of arguments");
		return 1;
	}
	return symbols_show(symbols_load(argv[i]));
}
