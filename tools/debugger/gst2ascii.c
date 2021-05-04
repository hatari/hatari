/*
 * Hatari - gst2ascii.c
 * 
 * Copyright (C) 2013-2021 by Eero Tamminen
 * 
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 * 
 * Convert DRI/GST and a.out format symbol table in a binary into ASCII symbols
 * file accepted by Hatari debugger and its profiler data post-processor.
 * This will also allow manual editing of the symbol table (removing irrelevant
 * labels or adding missing symbols for functions).
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
#include "../../src/debug/a.out.h"

typedef enum {
	SYMTYPE_TEXT = 1,
	SYMTYPE_DATA = 2,
	SYMTYPE_BSS  = 4,
	SYMTYPE_ABS  = 8
} symtype_t;

#define ARRAY_SIZE(x) (int)(sizeof(x)/sizeof(x[0]))

#include "../../src/debug/symbols-common.c"

/* ------------------ options & usage ------------------ */

#ifdef WIN32
#define PATHSEP '\\'
#else
#define PATHSEP '/'
#endif

static const char *PrgPath;

/*
 * Show program usage, given error message, and exit
 */
static void usage(const char *msg)
{
	const struct {
		const char opt;
		const char *desc;
	} OptInfo[] = {
		{ 'a', "no absolute symbols (are values, not addresses)" },
		{ 'b', "no BSS symbols" },
		{ 'd', "no DATA symbols" },
		{ 't', "no TEXT symbols" },
		{ 'l', "no local (.L*) symbols" },
		{ 'o', "no object symbols (filenames or GCC internals)" },
		{ 'n', "sort by name (not address)" },
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
		"Outputs given program (DRI/GST or a.out format) symbol table\n"
		"content in ASCII format accepted by Hatari debugger and its\n"
		"profiler data post-processor.\n"
		"\n"
		"All symbol addresses are output as TEXT relative, i.e. you need\n"
		"to give only that as section address for the Hatari debugger:\n"
		"\tsymbols <filename> TEXT\n"
		"\n"
		"Options:\n", name);
	for (i = 0; i < ARRAY_SIZE(OptInfo); i++) {
		fprintf(stderr, "\t-%c\t%s\n", OptInfo[i].opt, OptInfo[i].desc);
	}
	if (msg) {
		fprintf(stderr, "\nERROR: %s!\n", msg);
	}

	exit(msg != NULL);
}

/**
 * Sections just follow each other, so just add their sizes
 * (initially in .end fields) to successive fields and return true
 */
static bool update_sections(prg_section_t *sections)
{
	uint32_t offset;
	sections[0].offset = 0;
	offset = sections[0].end;

	sections[1].offset = offset;
	offset += sections[1].end;
	sections[1].end = offset;

	sections[2].offset = offset;
	offset += sections[2].end;
	sections[2].end = offset;
	return true;
}

/**
 * Load symbols of given type and the symbol address addresses from
 * the given file and add given offsets to the addresses.
 * Return symbols list or NULL for failure.
 */
static symbol_list_t* symbols_load(const char *filename, const symbol_opts_t *opts)
{
	symbol_list_t *list;
	uint16_t magic;
	FILE *fp;

	fprintf(stderr, "Reading symbols from program '%s' symbol table...\n", filename);
	if (!(fp = fopen(filename, "rb"))) {
		usage("opening program file failed");
	}
	if (fread(&magic, sizeof(magic), 1, fp) != 1) {
		usage("reading program file failed");
	}

	if (SDL_SwapBE16(magic) != ATARI_PROGRAM_MAGIC) {
		usage("file isn't an Atari program file");
	}
	list = symbols_load_binary(fp, opts, update_sections);
	fclose(fp);

	if (!list) {
		usage("no symbols, or reading them failed");
	}

	if (list->namecount < list->symbols) {
		if (!list->namecount) {
			usage("no valid symbols in program, symbol table loading failed");
		}
		/* parsed less than there were "content" lines */
		list->names = realloc(list->names, list->namecount * sizeof(symbol_t));
		assert(list->names);
	}

	/* copy name list to address list */
	list->addresses = malloc(list->namecount * sizeof(symbol_t));
	assert(list->addresses);
	memcpy(list->addresses, list->names, list->namecount * sizeof(symbol_t));

	/* sort both lists, with different criteria */
	qsort(list->addresses, list->namecount, sizeof(symbol_t), symbols_by_address);
	qsort(list->names, list->namecount, sizeof(symbol_t), symbols_by_name);

	/* check for duplicate addresses */
	symbols_check_addresses(list->addresses, list->namecount);

	/* check for duplicate names */
	symbols_check_names(list->names, list->namecount);

	return list;
}


/* ---------------- symbol showing & option parsing ------------------ */

/**
 * Show symbols sorted by selected option
 */
static int symbols_show(symbol_list_t* list, const symbol_opts_t *opts)
{
	symbol_t *entry, *entries;
	char symchar;
	int i;
	
	if (!list) {
		fprintf(stderr, "No symbols!\n");
		return 1;
	}
	if (opts->sort_name) {
		entries = list->names;
	} else {
		entries = list->addresses;
	}
	for (entry = entries, i = 0; i < list->namecount; i++, entry++) {
		symchar = symbol_char(entry->type);
		fprintf(stdout, "0x%08x %c %s\n",
			entry->address, symchar, entry->name);
	}

	fprintf(stderr, "%d (unignored) symbols processed.\n", list->namecount);
	fprintf(stderr, "\nLoad the listed symbols to Hatari debugger with 'symbols <filename> TEXT'.\n");
	return 0;
}

/**
 * parse program options and then call symbol load+save
 */
int main(int argc, const char *argv[])
{
	symbol_opts_t opts;
	int i;

	PrgPath = *argv;
	memset(&opts, 0, sizeof(opts));

	for (i = 1; i+1 < argc; i++) {
		if (argv[i][0] != '-') {
			break;
		}
		switch(tolower((unsigned char)argv[i][1])) {
		case 'a':
			opts.notypes |= SYMTYPE_ABS;
			break;
		case 'b':
			opts.notypes |= SYMTYPE_BSS;
			break;
		case 'd':
			opts.notypes |= SYMTYPE_DATA;
			break;
		case 't':
			opts.notypes |= SYMTYPE_TEXT;
			break;
		case 'l':
			opts.no_local = true;
			break;
		case 'o':
			opts.no_obj = true;
			break;
		case 'n':
			opts.sort_name = true;
			break;
		default:
			usage("unknown option");
		}
	}
	if (i+1 != argc) {
		usage("incorrect number of arguments");
	}
	return symbols_show(symbols_load(argv[i], &opts), &opts);
}
