/*
 * Hatari - gst2ascii.c
 * 
 * Copyright (C) 2013-2023 by Eero Tamminen
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
# define be_swap16(x) ((uint16_t)(x))
# define be_swap32(x) ((uint32_t)(x))
#else
# include "maccess.h"
#endif
#include <assert.h>
#include "../../src/debug/a.out.h"

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
		{ 'a', "absolute symbols (are values, not addresses)" },
		{ 'b', "BSS symbols" },
		{ 'd', "DATA symbols" },
		{ 'f', "file/path symbols" },
		{ 'g', "GCC internal (object) symbols" },
		{ 'l', "local (.L*) symbols" },
		{ 's', "symbols with duplicate addresses" },
		{ 't', "TEXT symbols" },
		{ 'w', "weak symbols" },
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
		"Outputs given program symbol table content in ASCII format\n"
		"accepted by Hatari debugger and its profiler post-processor.\n"
		"\n"
		"All symbol addresses are output as TEXT relative, i.e. you need\n"
		"to give only that as section address for the Hatari debugger:\n"
		"\tsymbols <filename> TEXT\n"
		"\n"
		"Symbol type options:\n", name);

	for (i = 0; i < ARRAY_SIZE(OptInfo); i++) {
		fprintf(stderr, "\t-%c\tno %s\n", OptInfo[i].opt, OptInfo[i].desc);
	}

	fprintf(stderr,
		"\n"
		"Prefixing option letter with '+' instead of '-', keeps\n"
		"the indicated symbol type instead of dropping it.\n"
		"\n"
		"Output options:\n"
		"\t-n, +n\tSort by address (-n), or by name (+n)\n"
		"\n"
		"Defaults:\n"
		"* drop local (-l), GCC internal (-g) and duplicate (-s) symbols\n"
		"* sort symbols by address (-n)\n");

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
	int dups;

	fprintf(stderr, "Reading symbols from program '%s' symbol table...\n", filename);
	if (!(fp = fopen(filename, "rb"))) {
		usage("opening program file failed");
	}
	if (fread(&magic, sizeof(magic), 1, fp) != 1) {
		usage("reading program file failed");
	}

	if (be_swap16(magic) != ATARI_PROGRAM_MAGIC) {
		usage("file isn't an Atari program file");
	}
	list = symbols_load_binary(fp, opts, update_sections);
	fclose(fp);

	if (!list || !list->namecount) {
		usage("no valid symbols in the program, or its symbol table loading failed");
	}

	/* first sort symbols by address (with code symbols being first) */
	qsort(list->names, list->namecount, sizeof(symbol_t), symbols_by_address);

	/* remove symbols with duplicate addresses? */
	if (opts->no_dups) {
		if ((dups = symbols_trim_names(list))) {
			fprintf(stderr, "Removed %d symbols in same addresses as other symbols.\n", dups);
		}
	}

	/* copy name list to address list */
	list->addresses = malloc(list->namecount * sizeof(symbol_t));
	assert(list->addresses);
	memcpy(list->addresses, list->names, list->namecount * sizeof(symbol_t));

	/* finally, sort name list by names */
	qsort(list->names, list->namecount, sizeof(symbol_t), symbols_by_name);

	/* check for duplicate addresses? */
	if (!opts->no_dups) {
		if ((dups = symbols_check_addresses(list->addresses, list->namecount))) {
			fprintf(stderr, "%d symbols in same addresses as other symbols.\n", dups);
		}
	}

	/* check for duplicate names */
	if ((dups = symbols_check_names(list->names, list->namecount))) {
		fprintf(stderr, "%d symbol names that have multiple addresses.\n", dups);
	}

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
	int i, notype;
	bool disable;

	PrgPath = *argv;

	memset(&opts, 0, sizeof(opts));
	opts.no_gccint = true;
	opts.no_local = true;
	opts.no_dups = true;

	for (i = 1; i+1 < argc; i++) {
		if (argv[i][0] == '-') {
			disable = true;
		} else if (argv[i][0] == '+') {
			disable = false;
		} else {
			break;
		}
		notype = 0;

		switch(tolower((unsigned char)argv[i][1])) {
			/* symbol types */
		case 'a':
			notype = SYMTYPE_ABS;
			break;
		case 'b':
			notype = SYMTYPE_BSS;
			break;
		case 'd':
			notype = SYMTYPE_DATA;
			break;
		case 't':
			opts.notypes |= SYMTYPE_TEXT;
			break;
		case 'w':
			opts.notypes |= SYMTYPE_WEAK;
			break;
			/* symbol flags */
		case 'f':
			opts.no_files = disable;
			break;
		case 'g':
			opts.no_gccint = disable;
			break;
		case 'l':
			opts.no_local = disable;
			break;
		case 's':
			opts.no_dups = disable;
			break;
			/* other options */
		case 'n':
			opts.sort_name = !disable;
			break;
		default:
			usage("unknown option");
		}

		if (disable) {
			opts.notypes |= notype;
		} else {
			opts.notypes &= ~notype;
		}
	}
	if (i+1 != argc) {
		usage("incorrect number of arguments");
	}
	return symbols_show(symbols_load(argv[i], &opts), &opts);
}
