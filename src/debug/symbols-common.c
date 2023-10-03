/*
 * Hatari - symbols-common.c
 *
 * Copyright (C) 2010-2023 by Eero Tamminen
 * Copyright (C) 2017,2021,2023 by Thorsten Otto
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * symbols-common.c - Hatari debugger symbol/address handling; parsing,
 * sorting, matching, TAB completion support etc.
 *
 * This code is shared between the internal debug "symbols" command
 * and the standalone "gst2asciii" tool.
 */

#include "symbols.h"

typedef struct {
	int symbols;		/* initial symbol count */
	int namecount;		/* final symbol count */
	int codecount;		/* TEXT/WEAK symbols address count */
	int datacount;		/* DATA/BSS symbol address count */
	symbol_t *addresses;	/* all address items sorted by address */
	symbol_t *names;	/* all items sorted by symbol name */
	char *strtab;		/* from a.out only */
	char *debug_strtab;	/* from pure-c debug information only */
} symbol_list_t;

typedef struct {
	uint32_t offset;
	uint32_t end;
} prg_section_t;

typedef struct {
	/* shared by debugger & gst2ascii */
	symtype_t notypes;
	bool no_files;
	bool no_gccint;
	bool no_local;
	bool no_dups;
	/* gst2ascii specific options */
	bool sort_name;
} symbol_opts_t;

typedef struct {
	int debug;    /* debug symbols */
	int files;    /* object file names */
	int gccint;   /* GCC internal symbols */
	int invalid;  /* invalid symbol types for addresses */
	int locals;   /* unnamed / local symbols */
	int notypes;  /* explicitly disabled symbol types */
	int undefined;/* undefined symbols */
} ignore_counts_t;

/* Magic used to denote different symbol table formats */
#define SYMBOL_FORMAT_GNU  0x474E555f	/* "GNU_" */
#define SYMBOL_FORMAT_MINT 0x4D694E54	/* "MiNT" */
#define SYMBOL_FORMAT_ELF  0x454c4600	/* "ELF" */
#define SYMBOL_FORMAT_DRI  0x0

/* Magic identifying Atari programs */
#define ATARI_PROGRAM_MAGIC 0x601A


/* ------- heuristic helpers for name comparisons ------- */

/**
 * return true if given symbol name is (anonymous/numbered) local one
 */
static bool is_local_symbol(const char *name)
{
	return (name[0] == '.' && name[1] == 'L');
}

/**
 * return true if given symbol name is object/library/file name
 */
static bool is_file_name(const char *name)
{
	int len = strlen(name);
	/* object (.a or .o) file name? */
	if (len > 2 && ((name[len-2] == '.' && (name[len-1] == 'a' || name[len-1] == 'o')))) {
		    return true;
	}
	/* some other file name? */
	const char *slash = strchr(name, '/');
	/* not just overloaded '/' operator? */
	if (slash && slash[1] != '(') {
		return true;
	}
	return false;
}

/**
 * Return true if symbol name matches internal GCC symbol name
 */
static bool is_gcc_internal(const char *name)
{
	static const char *gcc_sym[] = {
		"___gnu_compiled_c",
		"gcc2_compiled."
	};
	int i;
	/* useless symbols GCC (v2) seems to add to every object? */
	for (i = 0; i < ARRAY_SIZE(gcc_sym); i++) {
		if (strcmp(name, gcc_sym[i]) == 0) {
			return true;
		}
	}
	return false;
}

/**
 * Return true if symbol name seems to be C/C++ one,
 * i.e. is unlikely to be assembly one
 */
static bool is_cpp_symbol(const char *name)
{
	/* normally C symbols start with underscore */
	if (name[0] == '_') {
		return true;
	}
	/* C++ method signatures can include '::' or spaces */
	if (strchr(name, ' ') || strchr(name, ':')) {
		return true;
	}
	return false;
}


/* ------------------ symbol comparisons ------------------ */

/**
 * compare function for qsort(), to sort symbols by their
 * type, address, and finally name.
 *
 * Code symbols are sorted first, so that later phase can
 * split symbol table to separate code and data symbol lists.
 *
 * For symbols with same address, heuristics are used to sort
 * most useful name first, so that later phase can filter
 * the following, less useful names, out for that address.
 */
static int symbols_by_address(const void *s1, const void *s2)
{
	const symbol_t *sym1 = (const symbol_t*)s1;
	const symbol_t *sym2 = (const symbol_t*)s2;

	/* separate code type addresses from others */
	if ((sym1->type & SYMTYPE_CODE) && !(sym2->type & SYMTYPE_CODE)) {
		return -1;
	}
	if (!(sym1->type & SYMTYPE_CODE) && (sym2->type & SYMTYPE_CODE)) {
		return 1;
	}
	/* then sort by address */
	if (sym1->address < sym2->address) {
		return -1;
	}
	if (sym1->address > sym2->address) {
		return 1;
	}

	/* and by name when addresses are equal */
	const char *name1 = sym1->name;
	const char *name2 = sym2->name;

	/* first check for less desirable symbol names,
	 * from most useless, to somewhat useful
	 */
	bool (*sym_check[])(const char *) = {
		is_gcc_internal,
		is_local_symbol,
		is_file_name,
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(sym_check); i++) {
		bool unwanted1 = sym_check[i](name1);
		bool unwanted2 = sym_check[i](name2);
		if (!unwanted1 && unwanted2) {
			return -1;
		}
		if (unwanted1 && !unwanted2) {
			return 1;
		}
	}
	/* => both symbol names look useful */

	bool is_cpp1 = is_cpp_symbol(name1);
	bool is_cpp2 = is_cpp_symbol(name2);
	int len1 = strlen(name1);
	int len2 = strlen(name2);

	/* prefer shorter names for C/C++ symbols, as
	 * this often avoid '___' C-function prefixes,
	 * and C++ symbols can be *very* long
	 */
	if (is_cpp1 || is_cpp2) {
		return len1 - len2;
	}
	/* otherwise prefer longer symbols (e.g. ASM) */
	return len2 - len1;
}

/**
 * compare function for qsort() to sort according to
 * symbol name & address
 */
static int symbols_by_name(const void *s1, const void *s2)
{
	const symbol_t *sym1 = (const symbol_t*)s1;
	const symbol_t *sym2 = (const symbol_t*)s2;
	int ret;

	/* first by name */
	ret = strcmp(sym1->name, sym2->name);
	if (ret) {
		return ret;
	}
	/* then by address */
	return (sym1->address - sym2->address);
}

/**
 * Remove duplicate addresses from name list symbols, and trim its
 * allocation to remaining symbols.
 *
 * NOTE: symbols list *must* be *address-sorted* when this is called,
 * with the preferred symbol name being first, so this needs just to
 * remove symbols with duplicate addresses that follow it!
 *
 * Return number of removed address duplicates.
 */
static int symbols_trim_names(symbol_list_t* list)
{
	symbol_t *sym = list->names;
	int i, next, count, skip, dups = 0;

	count = list->namecount;
	for (i = 0; i < count - 1; i++) {
		if (sym[i].type == SYMTYPE_ABS) {
			/* value, not an address */
			continue;
		}

		/* count duplicates */
		for (next = i+1; next < count; next++) {
			if (sym[i].address != sym[next].address ||
			    sym[next].type == SYMTYPE_ABS) {
				break;
			}
			/* free this duplicate's name */
			if (sym[next].name_allocated) {
				free(sym[next].name);
			}
		}
		if (next == i+1) {
			continue;
		}

		/* drop counted duplicates */
		memmove(sym+i+1, sym+next, (count-next) * sizeof(symbol_t));
		skip = next - i - 1;
		count -= skip;
		dups += skip;
	}

	if (dups || list->namecount < list->symbols) {
		list->names = realloc(list->names, count * sizeof(symbol_t));
		assert(list->names);
		list->namecount = count;
	}
	return dups;
}

/**
 * Check for duplicate addresses in address-sorted symbol list
 * (called separately for code & data symbol parts)
 * Return number of duplicates
 */
static int symbols_check_addresses(const symbol_t *syms, int count)
{
	int i, j, total = 0;

	for (i = 0; i < (count - 1); i++) {
		/* absolute symbols have values, not addresses */
		if (syms[i].type == SYMTYPE_ABS) {
			continue;
		}
		bool has_dup = false;
		for (j = i + 1; j < count && syms[i].address == syms[j].address; j++) {
			if (syms[j].type == SYMTYPE_ABS) {
				continue;
			}
			if (!total) {
				fprintf(stderr, "WARNING, following symbols have same address:\n");
			}
			if (!has_dup) {
				fprintf(stderr, "- 0x%x: '%s'", syms[i].address, syms[i].name);
				has_dup = true;
			}
			fprintf(stderr, ", '%s'", syms[j].name);
			total++;
			i = j;
		}
		if (has_dup) {
			fprintf(stderr, "\n");
		}
	}
	return total;
}

/**
 * Check for duplicate names in name-sorted symbol list
 * Return number of duplicates
 */
static int symbols_check_names(const symbol_t *syms, int count)
{
	bool has_title = false;
	int i, j, dtotal = 0;

	for (i = 0; i < (count - 1); i++) {
		int dcount = 1;
		for (j = i + 1; j < count && strcmp(syms[i].name, syms[j].name) == 0; j++) {
			dtotal++;
			dcount++;
			i = j;
		}
		if (dcount > 1) {
			if (!has_title) {
				fprintf(stderr, "WARNING, following symbols have multiple addresses:\n");
				has_title = true;
			}
			fprintf(stderr, "- %s: %d\n", syms[i].name, dcount);
		}
	}
	return dtotal;
}


/* ----------------- symbol list alloc / free ------------------ */

/**
 * Allocate zeroed symbol list & names for given number of items.
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
		list->names = calloc(symbols, sizeof(symbol_t));
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
	int i;

	if (!list) {
		return;
	}
	assert(list->namecount);
	for (i = 0; i < list->namecount; i++) {
		if (list->names[i].name_allocated) {
			free(list->names[i].name);
		}
	}
	free(list->strtab);
	list->strtab = NULL;
	free(list->debug_strtab);
	list->debug_strtab = NULL;
	free(list->addresses);
	free(list->names);

	/* catch use of freed list */
	list->addresses = NULL;
	list->codecount = 0;
	list->datacount = 0;
	list->names = NULL;
	list->namecount = 0;
	free(list);
}

/**
 * Return symbol type identifier char
 */
static char symbol_char(int type)
{
	switch (type) {
	case SYMTYPE_TEXT: return 'T';
	case SYMTYPE_WEAK: return 'W';
	case SYMTYPE_DATA: return 'D';
	case SYMTYPE_BSS:  return 'B';
	case SYMTYPE_ABS:  return 'A';
	default: return '?';
	}
}


/* -------- Pure-C debug information handling --------- */

static uint32_t get_be32(const uint8_t *p)
{
	const uint32_t *p32 = (const uint32_t *)p;
	return be_swap32(*p32);
}


struct pdb_h {
	uint32_t magic;
	uint32_t size_fileinfos;
	uint32_t size_lineinfo;
	uint32_t size_varinfo;
	uint32_t size_unknown;
	uint32_t size_typeinfo;
	uint32_t size_structinfo;
	uint32_t size_stringtable;
};
#define SIZEOF_PDB_HEADER (8 * sizeof(uint32_t))

#define PDB_STORAGE_NONE     0 /* no storage; absolute value */
#define PDB_STORAGE_TEXT     4 /* in text segment */
#define PDB_STORAGE_DATA     5 /* in data segment */
#define PDB_STORAGE_BSS      6 /* in bss segment */

struct pdb_varinfo {
	int8_t type;
	uint8_t storage;
	uint32_t name_offset;
	uint32_t typeinfo_offset;
	uint32_t value;
};
#define SIZEOF_VARINFO ((size_t)14)


static void read_pc_debug_header(const uint8_t *ptr, struct pdb_h *header)
{
	header->magic = get_be32(ptr + 0);
	header->size_fileinfos = get_be32(ptr + 4);
	header->size_lineinfo = get_be32(ptr + 8);
	header->size_varinfo = get_be32(ptr + 12);
	header->size_unknown = get_be32(ptr + 16);
	header->size_typeinfo = get_be32(ptr + 20);
	header->size_structinfo = get_be32(ptr + 24);
	header->size_stringtable = get_be32(ptr + 28);
}


static void read_varinfo(const uint8_t *ptr, struct pdb_varinfo *info)
{
	info->type = ptr[0];
	info->storage = ptr[1];
	info->name_offset = get_be32(ptr + 2);
	info->typeinfo_offset = get_be32(ptr + 6);
	info->value = get_be32(ptr + 10);
}


static int read_pc_debug_names(FILE *fp, symbol_list_t *list, uint32_t offset)
{
	uint8_t *buf;
	size_t filesize;
	size_t nread;
	uint32_t reloc_offset;
	uint32_t debug_offset;
	uint32_t varinfo_offset;
	uint32_t strtable_offset;
	struct pdb_h pdb_h;

	if (fseek(fp, 0, SEEK_END) < 0)
		return 0;
	if ((filesize = ftell(fp)) < 0)
		return 0;
	if (fseek(fp, 0, SEEK_SET) < 0)
		return 0;

	buf = malloc(filesize);
	if (buf == NULL) {
		perror("");
		return 0;
	}

	nread = fread(buf, 1, filesize, fp);
	if (nread != filesize){
		perror("ERROR: reading failed");
		free(buf);
		return 0;
	}
	reloc_offset = offset;

	/*
	 * skip the TPA relocation table
	 */
	{
		uint32_t first_reloc = get_be32(buf + reloc_offset);
		reloc_offset += 4;

		if (first_reloc != 0) {
			while (reloc_offset < filesize && buf[reloc_offset] != 0)
				reloc_offset++;
			reloc_offset++;
		}
		if (reloc_offset & 1)
			reloc_offset++;
		debug_offset = reloc_offset;
	}

	if (debug_offset + SIZEOF_PDB_HEADER >= filesize) {
		/* fprintf(stderr, "no debug information present\n"); */
		/* this is not an error */
		free(buf);
		return 1;
	}
	read_pc_debug_header(buf + debug_offset, &pdb_h);
	/* 'QDB1' (in executables) */
	if (pdb_h.magic != 0x51444231UL) {
		fprintf(stderr, "ERROR: unknown debug format 0x%08lx\n", (unsigned long)pdb_h.magic);
		free(buf);
		return 0;
	}
	if (pdb_h.size_stringtable == 0) {
		free(buf);
		return 0;
	}
	fprintf(stderr, "Reading symbol names from Pure-C debug information.\n");

	list->debug_strtab = (char *)malloc(pdb_h.size_stringtable);
	if (list->debug_strtab == NULL) {
		perror("mem alloc of debug string table");
		free(buf);
		return 0;
	}

	varinfo_offset = SIZEOF_PDB_HEADER + debug_offset + pdb_h.size_fileinfos + pdb_h.size_lineinfo;
	strtable_offset = varinfo_offset + pdb_h.size_varinfo + pdb_h.size_unknown + pdb_h.size_typeinfo + pdb_h.size_structinfo;

	if (strtable_offset >= filesize || strtable_offset + pdb_h.size_stringtable > filesize) {
		free(list->debug_strtab);
		list->debug_strtab = NULL;
		free(buf);
		return 0;
	}
	memcpy(list->debug_strtab, buf + strtable_offset, pdb_h.size_stringtable);

	if (pdb_h.size_varinfo != 0) {
		int i;
		for (i = 0; i < list->namecount; i++) {
			uint8_t storage;

			switch (list->names[i].type) {
			case SYMTYPE_TEXT:
				storage = PDB_STORAGE_TEXT;
				break;
			case SYMTYPE_DATA:
				storage = PDB_STORAGE_DATA;
				break;
			case SYMTYPE_BSS:
				storage = PDB_STORAGE_BSS;
				break;
			default:
				storage = PDB_STORAGE_NONE;
				break;
			}
			if (storage != PDB_STORAGE_NONE) {
				uint8_t *p, *end;
				int len = (int)strlen(list->names[i].name);
				/*
				 * only need to care about possibly truncated names
				 */
				if (len != 8 && len != 22) {
					continue;
				}
				/*
				 * Fixme: slurp the infos all in, and sort them so we can do a binary search
				 */
				p = buf + varinfo_offset;
				end = p + pdb_h.size_varinfo;
				while (p < end) {
					struct pdb_varinfo info;

					read_varinfo(p, &info);
					if (info.storage == storage && info.value == list->names[i].address &&
					    ((storage == PDB_STORAGE_TEXT && (info.type == 7 || info.type == 8)) ||
					     ((storage == PDB_STORAGE_DATA || storage == PDB_STORAGE_BSS) &&
					      (info.type == 4 || info.type == 5 || info.type == 6)))) {

						char *name = (char *)buf + strtable_offset + info.name_offset;
						if (strcmp(list->names[i].name, name) != 0) {
							if (list->names[i].name_allocated) {
								free(list->names[i].name);
								list->names[i].name_allocated = false;
							}
							list->names[i].name = list->debug_strtab + info.name_offset;
						}
						break;
					}
					p += SIZEOF_VARINFO;
				}
			}
		}
	}

	free(buf);
	return 1;
}


/* ---------- symbol ignore count handling ------------- */

/**
 * Return true if symbol should be ignored based on its name & type
 * and given options, and increase appropriate ignore count
 */
static bool ignore_symbol(const char *name, symtype_t symtype, const symbol_opts_t *opts, ignore_counts_t *counts)
{
	if (opts->notypes & symtype) {
		counts->notypes++;
		return true;
	}
	if (opts->no_local) {
		if (is_local_symbol(name)) {
			counts->locals++;
			return true;
		}
	}
	if (opts->no_gccint) {
		if (is_gcc_internal(name)) {
			counts->gccint++;
			return true;
		}
	}
	if (opts->no_files) {
		if (is_file_name(name)) {
			counts->files++;
			return true;
		}
	}
	return false;
}

/**
 * show counts for all ignored symbol categories
 */
static void show_ignored(const ignore_counts_t *counts)
{
	if (counts->debug) {
		fprintf(stderr, "NOTE: ignored %d debugging symbols.\n", counts->debug);
	}
	if (counts->files) {
		/* object file path names most likely get truncated and
		 * as result cause unnecessary symbol name conflicts
		 * in addition to object file addresses conflicting
		 * with first symbol in the object file.
		 */
		fprintf(stderr, "NOTE: ignored %d file symbols ('*.[ao]'|'*/*').\n", counts->files);
	}
	if (counts->gccint) {
		fprintf(stderr, "NOTE: ignored %d GCC internal symbols.\n", counts->gccint);
	}
	if (counts->invalid) {
		fprintf(stderr, "NOTE: ignored %d invalid symbols.\n", counts->invalid);
	}
	if (counts->locals) {
		fprintf(stderr, "NOTE: ignored %d unnamed / local symbols ('.L*').\n", counts->locals);
	}
	if (counts->notypes) {
		fprintf(stderr, "NOTE: ignored %d symbols with unwanted types.\n", counts->notypes);
	}
	if (counts->undefined) {
		fprintf(stderr, "NOTE: ignored %d undefined symbols.\n", counts->undefined);
	}
}


/* ---------- symbol table type specific loading ------------- */

/**
 * Load symbols of given type and the symbol address addresses from
 * DRI/GST format symbol table, and add given offsets to the addresses:
 *	http://toshyp.atari.org/en/005005.html
 * Return symbols list or NULL for failure.
 */
static symbol_list_t* symbols_load_dri(FILE *fp, const prg_section_t *sections, uint32_t tablesize, const symbol_opts_t *opts)
{
	int i, count, symbols;
	ignore_counts_t ignore;
	const prg_section_t *section;
	symbol_list_t *list;
	symtype_t symtype;
#define DRI_ENTRY_SIZE	14
	char name[23];
	uint16_t symid;
	uint32_t address;
	bool use_bssdata_offset;
	uint32_t textlen = sections[0].end - sections[0].offset;

	if (tablesize % DRI_ENTRY_SIZE || !tablesize) {
		fprintf(stderr, "ERROR: invalid DRI/GST symbol table size %d!\n", tablesize);
		return NULL;
	}
	symbols = tablesize / DRI_ENTRY_SIZE;
	if (!(list = symbol_list_alloc(symbols))) {
		return NULL;
	}

	memset(&ignore, 0, sizeof(ignore));
	use_bssdata_offset = false;
	count = 0;

	for (i = 1; i <= symbols; i++) {
		/* read DRI symbol table slot */
		if (fread(name, 8, 1, fp) != 1 ||
		    fread(&symid, sizeof(symid), 1, fp) != 1 ||
		    fread(&address, sizeof(address), 1, fp) != 1) {
			break;
		}
		address = be_swap32(address);
		symid = be_swap16(symid);

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
			break;
		case 0x0400:
			symtype = SYMTYPE_DATA;
			if (address < textlen)
				use_bssdata_offset = true;
			break;
		case 0x0100:
			symtype = SYMTYPE_BSS;
			if (address < textlen)
				use_bssdata_offset = true;
			break;
		default:
			if ((symid & 0xe000) == 0xe000) {
				ignore.debug++;
				continue;
			}
			if ((symid & 0x4000) == 0x4000) {
				symtype = SYMTYPE_ABS;
				break;
			}
			fprintf(stderr, "WARNING: ignoring symbol '%s' in slot %d of unknown type 0x%x.\n", name, i, symid);
			ignore.invalid++;
			continue;
		}
		/* whether to ignore symbol based on options and its name & type */
		if (ignore_symbol(name, symtype, opts, &ignore)) {
			continue;
		}
		list->names[count].address = address;
		list->names[count].type = symtype;
		list->names[count].name = strdup(name);
		list->names[count].name_allocated = true;
		assert(list->names[count].name);
		count++;
	}
	if (i <= symbols) {
		perror("ERROR: reading symbol failed");
		symbol_list_free(list);
		return NULL;
	}

	list->symbols = symbols;
	list->namecount = count;

	/*
	 * now try to read the real names from Pure-C debug info
	 */
	read_pc_debug_names(fp, list, 28 + (sections[2].offset - sections[0].offset) + tablesize);

	/*
	 * now offset the addresses if needed, and check them
	 */
	fprintf(stderr, "Offsetting BSS/DATA symbols from %s.\n",
		use_bssdata_offset ? "their own sections" : "TEXT section");
	count = 0;
	for (i = 0; i < list->namecount; i++) {
		/* offsets are by default based on TEXT section */
		const prg_section_t *offset_section = &(sections[0]);
		symbol_t *item = &list->names[i];
		switch (item->type) {
		case SYMTYPE_TEXT:
			section = &(sections[0]);
			break;
		case SYMTYPE_DATA:
			section = &(sections[1]);
			if (use_bssdata_offset) {
				offset_section = &(sections[1]);
			}
			break;
		case SYMTYPE_BSS:
			section = &(sections[2]);
			if (use_bssdata_offset) {
				offset_section = &(sections[2]);
			}
			break;
		default:
			section = NULL;
			break;
		}
		if (section && offset_section) {
			item->address += offset_section->offset;
			if (item->address > section->end) {
				fprintf(stderr, "WARNING: ignoring %c symbol '%s' in slot %d with invalid offset 0x%x (>= 0x%x).\n",
					symbol_char(item->type), item->name, i, item->address, section->end);
				if (item->name_allocated) {
					free(item->name);
				}
				ignore.invalid++;
				continue;
			}
		}
		list->names[count] = *item;
		count++;
	}
	/*
	 * update new final count again
	 */
	list->namecount = count;

	show_ignored(&ignore);
	return list;
}


/**
 * Load symbols of given type and the symbol address addresses from
 * a.out format symbol table, and add given offsets to the addresses.
 * Return symbols list or NULL for failure.
 */
static symbol_list_t* symbols_load_gnu(FILE *fp, const prg_section_t *sections, uint32_t tablesize, uint32_t stroff, uint32_t strsize, const symbol_opts_t *opts)
{
	size_t slots = tablesize / SIZEOF_STRUCT_NLIST;
	size_t i;
	size_t strx;
	unsigned char *p;
	char *name;
	symbol_t *sym;
	symtype_t symtype;
	uint32_t address;
	uint32_t nread;
	symbol_list_t *list;
	unsigned char n_type;
	unsigned char n_other;
	unsigned short n_desc;
	static char dummy[] = "<invalid>";
	int count;
	ignore_counts_t ignore;
	const prg_section_t *section;

	if (!(list = symbol_list_alloc(slots))) {
		return NULL;
	}

	list->strtab = (char *)malloc(tablesize + strsize + 1);

	if (list->strtab == NULL) {
		symbol_list_free(list);
		return NULL;
	}

	nread = fread(list->strtab, tablesize + strsize, 1, fp);
	if (nread != 1) {
		perror("ERROR: reading symbols failed");
		symbol_list_free(list);
		return NULL;
	}
	list->strtab[tablesize + strsize] = 0;

	p = (unsigned char *)list->strtab;
	sym = list->names;

	memset(&ignore, 0, sizeof(ignore));
	count = 0;

	for (i = 0; i < slots; i++) {
		strx = get_be32(p);
		p += 4;
		n_type = *p++;
		n_other = *p++;
		n_desc = be_swap16(*(uint16_t*)p);
		p += 2;
		address = get_be32(p);
		p += 4;
		name = dummy;
		if (!strx) {
			ignore.invalid++;
			continue;
		}
		if (strx >= strsize) {
			fprintf(stderr, "symbol name index %x out of range\n", (unsigned int)strx);
			ignore.invalid++;
			continue;
		}
		name = list->strtab + strx + stroff;

		if (n_type & N_STAB) {
			ignore.debug++;
			continue;
		}

		section = NULL;
		switch (n_type & (N_TYPE|N_EXT)) {
		case N_UNDF:
		case N_UNDF|N_EXT:
		case N_WEAKU:
			/* shouldn't happen here */
			ignore.undefined++;
			continue;

		case N_ABS:
		case N_ABS|N_EXT:
			symtype = SYMTYPE_ABS;
			break;

		case N_FN: /* filename symbol */
		case N_TEXT:
		case N_TEXT|N_EXT:
			symtype = SYMTYPE_TEXT;
			section = &(sections[0]);
			break;

		case N_WEAKT:
			symtype = SYMTYPE_WEAK;
			section = &(sections[0]);
			break;

		case N_DATA:
		case N_DATA|N_EXT:
		case N_WEAKD:
			symtype = SYMTYPE_DATA;
			section = &(sections[1]);
			break;

		case N_BSS:
		case N_BSS|N_EXT:
		case N_COMM:
		case N_COMM|N_EXT:
		case N_WEAKB:
			symtype = SYMTYPE_BSS;
			section = &(sections[2]);
			break;

		case N_SIZE:
		case N_WARNING:
		case N_SETA:
		case N_SETT:
		case N_SETD:
		case N_SETB:
		case N_SETV:
			ignore.debug++;
			continue;
		default:
			fprintf(stderr, "WARNING: ignoring symbol '%s' in slot %u of unknown type 0x%x.\n", name, (unsigned int)i, n_type);
			ignore.invalid++;
			continue;
		}
		/*
		 * the value of a common symbol is its size, not its address:
		 */
		if (((n_type & N_TYPE) == N_COMM) ||
			(((n_type & N_EXT) && (n_type & N_TYPE) == N_UNDF && address != 0))) {
			/* if we ever want to know a symbols size, get that here */
			fprintf(stderr, "WARNING: ignoring common symbol '%s' in slot %u.\n", name, (unsigned int)i);
			ignore.debug++;
			continue;
		}
		/* whether to ignore symbol based on options and its name & type */
		if (ignore_symbol(name, symtype, opts, &ignore)) {
			continue;
		}
		if (section) {
			address += sections[0].offset;	/* all GNU symbol addresses are TEXT relative */
			if (address > section->end) {
				fprintf(stderr, "WARNING: ignoring symbol '%s' of type %c in slot %u with invalid offset 0x%x (>= 0x%x).\n",
					name, symbol_char(symtype), (unsigned int)i, address, section->end);
				ignore.invalid++;
				continue;
			}
		}
		sym->address = address;
		sym->type = symtype;
		sym->name = name;
		sym++;
		count++;
		(void) n_desc;
		(void) n_other;
	}
	list->symbols = slots;
	list->namecount = count;

	show_ignored(&ignore);
	return list;
}

/**
 * Load symbols of given type and the symbol address addresses from
 * ELF format symbol table, and add given offsets to the addresses.
 * Return symbols list or NULL for failure.
 */

#define SIZEOF_ELF32_SYM 16

#define ELF_ST_BIND(val)		(((unsigned int)(val)) >> 4)
#define ELF_ST_TYPE(val)		((val) & 0xF)
#define ELF_ST_INFO(bind,type)		(((bind) << 4) + ((type) & 0xF))

/* sh_type */
#define SHT_NULL	  0 		/* Section header table entry unused */
#define SHT_PROGBITS	  1 		/* Program specific (private) data */
#define SHT_SYMTAB	  2 		/* Link editing symbol table */
#define SHT_STRTAB	  3 		/* A string table */
#define SHT_RELA	  4 		/* Relocation entries with addends */
#define SHT_HASH	  5 		/* A symbol hash table */
#define SHT_DYNAMIC 	  6 		/* Information for dynamic linking */
#define SHT_NOTE	  7 		/* Information that marks file */
#define SHT_NOBITS	  8 		/* Section occupies no space in file */
#define SHT_REL 	  9 		/* Relocation entries, no addends */
#define SHT_SHLIB	  10		/* Reserved, unspecified semantics */
#define SHT_DYNSYM	  11		/* Dynamic linking symbol table */
#define SHT_INIT_ARRAY	  14		/* Array of constructors */
#define SHT_FINI_ARRAY	  15		/* Array of destructors */
#define SHT_PREINIT_ARRAY 16		/* Array of pre-constructors */
#define SHT_GROUP	  17		/* Section group */
#define SHT_SYMTAB_SHNDX  18		/* Extended section indices */

/* ST_BIND */
#define STB_LOCAL  0			/* Symbol not visible outside obj */
#define STB_GLOBAL 1			/* Symbol visible outside obj */
#define STB_WEAK   2			/* Like globals, lower precedence */
#define STB_LOOS   10			/* Start of OS-specific */
#define STB_GNU_UNIQUE	10		/* Symbol is unique in namespace */
#define STB_HIOS   12			/* End of OS-specific */
#define STB_LOPROC 13			/* Application-specific semantics */
#define STB_HIPROC 15			/* Application-specific semantics */

/* ST_TYPE */
#define STT_NOTYPE	0		/* Symbol type is unspecified */
#define STT_OBJECT	1		/* Symbol is a data object */
#define STT_FUNC	2		/* Symbol is a code object */
#define STT_SECTION	3		/* Symbol associated with a section */
#define STT_FILE	4		/* Symbol gives a file name */
#define STT_COMMON	5		/* Symbol is a common data object */
#define STT_TLS 	6		/* Symbol is thread-local data object*/
#define STT_LOOS	10		/* Start of OS-specific */
#define STT_GNU_IFUNC	10		/* Symbol is an indirect code object */
#define STT_HIOS	12		/* End of OS-specific */
#define STT_LOPROC	13		/* Application-specific semantics */
#define STT_HIPROC	15		/* Application-specific semantics */

/* special sections indexes */
#define SHN_UNDEF 0
#define SHN_LORESERVE    0xFF00         /* Begin range of reserved indices */
#define SHN_LOPROC       0xFF00         /* Begin range of appl-specific */
#define SHN_HIPROC       0xFF1F         /* End range of appl-specific */
#define SHN_LOOS         0xFF20         /* OS specific semantics, lo */
#define SHN_HIOS         0xFF3F         /* OS specific semantics, hi */
#define SHN_ABS          0xfff1         /* Associated symbol is absolute */
#define SHN_COMMON       0xfff2         /* Associated symbol is in common */
#define SHN_XINDEX	 0xFFFF		/* Section index is held elsewhere */
#define SHN_HIRESERVE    0xFFFF         /* End range of reserved indices */

/* Values for section header, sh_flags field. */
#define SHF_WRITE		 ((uint32_t)1 << 0)   /* Writable data during execution */
#define SHF_ALLOC		 ((uint32_t)1 << 1)   /* Occupies memory during execution */
#define SHF_EXECINSTR		 ((uint32_t)1 << 2)   /* Executable machine instructions */
#define SHF_MERGE		 ((uint32_t)1 << 4)   /* Might be merged */
#define SHF_STRINGS 		 ((uint32_t)1 << 5)   /* Contains nul-terminated strings */
#define SHF_INFO_LINK		 ((uint32_t)1 << 6)   /* `sh_info' contains SHT index */
#define SHF_LINK_ORDER		 ((uint32_t)1 << 7)   /* Preserve order after combining */
#define SHF_OS_NONCONFORMING	 ((uint32_t)1 << 8)   /* Non-standard OS specific handling required */
#define SHF_GROUP		 ((uint32_t)1 << 9)   /* Section is member of a group. */
#define SHF_TLS 		 ((uint32_t)1 << 10)  /* Section hold thread-local data. */
#define SHF_COMPRESSED		 ((uint32_t)1 << 11)  /* Section with compressed data */
#define SHF_MASKOS		 0x0ff00000	/* OS-specific. */
#define SHF_MASKPROC		 0xf0000000	/* Processor-specific */
#define SHF_ORDERED 		 ((uint32_t)1 << 30)  /* Special ordering requirement (Solaris). */
#define SHF_EXCLUDE 		 ((uint32_t)1 << 31)  /* Section is excluded unless referenced or allocated (Solaris).*/

struct elf_shdr {
    uint32_t sh_name;           /* Section name */
    uint32_t sh_type;           /* Type of section */
    uint32_t sh_flags;          /* Miscellaneous section attributes */
    uint32_t sh_addr;           /* Section virtual addr at execution */
    uint32_t sh_offset;         /* Section file offset */
    uint32_t sh_size;           /* Size of section in bytes */
    uint32_t sh_link;           /* Index of another section */
    uint32_t sh_info;           /* Additional section information */
    uint32_t sh_addralign;      /* Section alignment */
    uint32_t sh_entsize;        /* Entry size if section holds table */
};

static symbol_list_t* symbols_load_elf(FILE *fp, const prg_section_t *sections,
				       uint32_t tablesize, uint32_t stroff,
				       uint32_t strsize, const symbol_opts_t *opts,
				       struct elf_shdr *headers, unsigned short e_shnum)
{
	size_t slots = tablesize / SIZEOF_ELF32_SYM;
	size_t i;
	size_t strx;
	unsigned char *p;
	char *name;
	symbol_t *sym;
	symtype_t symtype;
	uint32_t address;
	uint32_t nread;
	symbol_list_t *list;
	uint32_t st_size;
	unsigned char st_info;
	unsigned char st_other;
	unsigned short st_shndx;
	static char dummy[] = "<invalid>";
	int count;
	ignore_counts_t ignore;
	const prg_section_t *section;
	unsigned char *symtab;
	struct elf_shdr *shdr;

	if (!(list = symbol_list_alloc(slots))) {
		return NULL;
	}

	list->strtab = (char *)malloc(strsize + 1);
	symtab = (unsigned char *)malloc(tablesize);

	if (list->strtab == NULL || symtab == NULL) {
		perror("");
		free(symtab);
		symbol_list_free(list);
		return NULL;
	}

	nread = fread(symtab, tablesize, 1, fp);
	if (nread != 1) {
		perror("ERROR: reading symbols failed");
		free(symtab);
		symbol_list_free(list);
		return NULL;
	}

	if (fseek(fp, stroff, SEEK_SET) < 0) {
		perror("ERROR: seeking to string table failed");
		free(symtab);
		symbol_list_free(list);
		return NULL;
	}

	nread = fread(list->strtab, strsize, 1, fp);
	if (nread != 1) {
		perror("ERROR: reading symbol names failed");
		free(symtab);
		symbol_list_free(list);
		return NULL;
	}
	list->strtab[strsize] = 0;

	p = (unsigned char *)symtab;
	sym = list->names;

	memset(&ignore, 0, sizeof(ignore));
	count = 0;

	for (i = 0; i < slots; i++) {
		strx = get_be32(p);
		p += 4;
		address = get_be32(p);
		p += 4;
		st_size = get_be32(p);
		p += 4;
		st_info = *p++;
		st_other = *p++;
		st_shndx = be_swap16(*(uint16_t*)p);
		p += 2;
		name = dummy;
		if (!strx) {
			switch (st_info) {
			/* silently ignore no-name symbols
			 * related to section names
			 */
			case ELF_ST_INFO(STB_LOCAL, STT_NOTYPE):
			case ELF_ST_INFO(STB_LOCAL, STT_SECTION):
				break;
			default:
				ignore.invalid++;
				break;
			}
			continue;
		}
		if (strx >= strsize) {
			fprintf(stderr, "symbol name index %x out of range\n", (unsigned int)strx);
			ignore.invalid++;
			continue;
		}
		name = list->strtab + strx;

		section = NULL;
		switch (st_info) {
		case ELF_ST_INFO(STB_LOCAL, STT_OBJECT):
		case ELF_ST_INFO(STB_GLOBAL, STT_OBJECT):
		case ELF_ST_INFO(STB_WEAK, STT_OBJECT):
		case ELF_ST_INFO(STB_LOCAL, STT_FUNC):
		case ELF_ST_INFO(STB_GLOBAL, STT_FUNC):
		case ELF_ST_INFO(STB_WEAK, STT_FUNC):
		case ELF_ST_INFO(STB_LOCAL, STT_COMMON):
		case ELF_ST_INFO(STB_GLOBAL, STT_COMMON):
		case ELF_ST_INFO(STB_WEAK, STT_COMMON):
		case ELF_ST_INFO(STB_GLOBAL, STT_NOTYPE):
		case ELF_ST_INFO(STB_LOCAL, STT_NOTYPE):
		case ELF_ST_INFO(STB_WEAK, STT_NOTYPE):
			switch (st_shndx) {
			case SHN_ABS:
				symtype = SYMTYPE_ABS;
				break;
			case SHN_UNDEF:
				/* shouldn't happen here */
				ignore.undefined++;
				continue;
			case SHN_COMMON:
				fprintf(stderr, "WARNING: ignoring common symbol '%s' in slot %u.\n", name, (unsigned int)i);
				ignore.debug++;
				continue;
			default:
				if (st_shndx >= e_shnum) {
					ignore.invalid++;
					continue;
				} else {
					shdr = &headers[st_shndx];

					if (shdr->sh_type == SHT_NOBITS) {
						symtype = ELF_ST_BIND(st_info) == STB_WEAK ? SYMTYPE_WEAK : SYMTYPE_BSS;
						section = &(sections[2]);

					} else if (shdr->sh_flags & SHF_EXECINSTR) {
						symtype = ELF_ST_BIND(st_info) == STB_WEAK ? SYMTYPE_WEAK : SYMTYPE_TEXT;
						section = &(sections[0]);

					} else {
						symtype = ELF_ST_BIND(st_info) == STB_WEAK ? SYMTYPE_WEAK : SYMTYPE_DATA;
						section = &(sections[1]);
					}
				}
			}
			break;

		case ELF_ST_INFO(STB_LOCAL, STT_FILE): /* filename symbol */
			ignore.debug++;
			continue;

		case ELF_ST_INFO(STB_LOCAL, STT_SECTION): /* section name */
			continue;

		default:
			fprintf(stderr, "WARNING: ignoring symbol '%s' in slot %u of unknown type 0x%x.\n", name, (unsigned int)i, st_info);
			ignore.invalid++;
			continue;
		}

		if (section) {
			address += sections[0].offset;	/* all GNU symbol addresses are TEXT relative */
			if (address > section->end) {
				fprintf(stderr, "WARNING: ignoring symbol '%s' of type %c in slot %u with invalid offset 0x%x (>= 0x%x).\n",
					name, symbol_char(symtype), (unsigned int)i, address, section->end);
				ignore.invalid++;
				continue;
			}
		}
		sym->address = address;
		sym->type = symtype;
		sym->name = name;
		sym++;
		count++;
		(void)st_other;
		(void)st_size;
	}
	list->symbols = slots;
	list->namecount = count;

	free(symtab);

	show_ignored(&ignore);
	return list;
}

/* ---------- program info + symbols loading ------------- */

/**
 * Print program header information.
 * Return false for unrecognized symbol table type.
 */
static bool symbols_print_prg_info(uint32_t tabletype, uint32_t prgflags, uint16_t relocflag)
{
	static const struct {
		uint32_t flag;
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
	case SYMBOL_FORMAT_MINT: /* "MiNT" */
		info = "GCC/MiNT executable, GST symbol table";
		break;
	case SYMBOL_FORMAT_GNU:	 /* "GNU_" */
		info = "GCC/MiNT executable, a.out symbol table";
		break;
	case SYMBOL_FORMAT_ELF:
		info = "GCC/MiNT executable, elf symbol table";
		break;
	case SYMBOL_FORMAT_DRI:
		info = "TOS executable, DRI / GST symbol table";
		break;
	default:
		fprintf(stderr, "ERROR: unknown executable type 0x%x!\n", tabletype);
		return false;
	}
	fprintf(stderr, "%s, reloc=%d, program flags:", info, relocflag);
	/* bit flags */
	for (i = 0; i < ARRAY_SIZE(flags); i++) {
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
 * Parse program header and use symbol table format specific
 * loader function to load the symbols.
 *
 * update_sections() callback is called with .end fields set
 * to sizes of corresponding sections.  It should set suitable
 * start offsets and update end end positions accordingly.
 * If that succeeds, it should return true.
 *
 * Return symbols list or NULL for failure.
 */
static symbol_list_t* symbols_load_binary(FILE *fp, const symbol_opts_t *opts,
					  bool (*update_sections)(prg_section_t*))
{
	uint32_t textlen, datalen, bsslen, tablesize, tabletype, prgflags;
	prg_section_t sections[3];
	int reads = 0;
	uint16_t relocflag;
	symbol_list_t* symbols;
	uint32_t symoff = 0;
	uint32_t stroff = 0;
	uint32_t strsize = 0;
	struct elf_shdr *headers = 0;
	uint16_t e_shnum = 0;

	/* get TEXT, DATA & BSS section sizes */
	fseek(fp, 2, SEEK_SET);
	reads += fread(&textlen, sizeof(textlen), 1, fp);
	textlen = be_swap32(textlen);
	reads += fread(&datalen, sizeof(datalen), 1, fp);
	datalen = be_swap32(datalen);
	reads += fread(&bsslen, sizeof(bsslen), 1, fp);
	bsslen = be_swap32(bsslen);

	/* get symbol table size & type and check that all reads succeeded */
	reads += fread(&tablesize, sizeof(tablesize), 1, fp);
	tablesize = be_swap32(tablesize);
	reads += fread(&tabletype, sizeof(tabletype), 1, fp);
	tabletype = be_swap32(tabletype);

	/* get program header and whether there's reloc table */
	reads += fread(&prgflags, sizeof(prgflags), 1, fp);
	prgflags = be_swap32(prgflags);
	reads += fread(&relocflag, sizeof(relocflag), 1, fp);
	relocflag = be_swap16(relocflag);

	if (reads != 7) {
		fprintf(stderr, "ERROR: program header reading failed!\n");
		return NULL;
	}
	/*
	 * check for GNU-style symbol table in aexec header
	 */
	if (tabletype == SYMBOL_FORMAT_MINT) { /* MiNT */
		uint32_t magic1, magic2;
		uint32_t dummy;
		uint32_t a_text, a_data, a_bss, a_syms, a_entry, a_trsize, a_drsize;
		uint32_t g_tparel_pos, g_tparel_size, g_stkpos, g_symbol_format;

		reads  = fread(&magic1, sizeof(magic1), 1, fp);
		magic1 = be_swap32(magic1);
		reads += fread(&magic2, sizeof(magic2), 1, fp);
		magic2 = be_swap32(magic2);
		if (reads == 2 &&
		    ((magic1 == 0x283a001a && magic2 == 0x4efb48fa) || 	/* Original binutils: move.l 28(pc),d4; jmp 0(pc,d4.l) */
		     (magic1 == 0x203a001a && magic2 == 0x4efb08fa))) {	/* binutils >= 2.18-mint-20080209: move.l 28(pc),d0; jmp 0(pc,d0.l) */
			reads += fread(&dummy, sizeof(dummy), 1, fp);	/* skip a_info */
			reads += fread(&a_text, sizeof(a_text), 1, fp);
			a_text = be_swap32(a_text);
			reads += fread(&a_data, sizeof(a_data), 1, fp);
			a_data = be_swap32(a_data);
			reads += fread(&a_bss, sizeof(a_bss), 1, fp);
			a_bss = be_swap32(a_bss);
			reads += fread(&a_syms, sizeof(a_syms), 1, fp);
			a_syms = be_swap32(a_syms);
			reads += fread(&a_entry, sizeof(a_entry), 1, fp);
			a_entry = be_swap32(a_entry);
			reads += fread(&a_trsize, sizeof(a_trsize), 1, fp);
			a_trsize = be_swap32(a_trsize);
			reads += fread(&a_drsize, sizeof(a_drsize), 1, fp);
			a_drsize = be_swap32(a_drsize);
			reads += fread(&g_tparel_pos, sizeof(g_tparel_pos), 1, fp);
			g_tparel_pos = be_swap32(g_tparel_pos);
			reads += fread(&g_tparel_size, sizeof(g_tparel_size), 1, fp);
			g_tparel_size = be_swap32(g_tparel_size);
			reads += fread(&g_stkpos, sizeof(g_stkpos), 1, fp);
			g_stkpos = be_swap32(g_stkpos);
			reads += fread(&g_symbol_format, sizeof(g_symbol_format), 1, fp);
			g_symbol_format = be_swap32(g_symbol_format);
			if (g_symbol_format == 0) {
				tabletype = SYMBOL_FORMAT_GNU;
			}
			if ((a_text + (256 - 28)) != textlen) {
				fprintf(stderr, "warning: inconsistent text segment size %08x != %08x\n", textlen, a_text + (256 - 28));
			}
			if (a_data != datalen) {
				fprintf(stderr, "warning: inconsistent data segment size %08x != %08x\n", datalen, a_data);
			}
			if (a_bss != bsslen) {
				fprintf(stderr, "warning: inconsistent bss segment size %08x != %08x\n", bsslen, a_bss);
			}
			/*
			 * the symbol table size in the GEMDOS header includes the string table,
			 * the symbol table size in the exec header does not.
			 */
			if (tabletype == SYMBOL_FORMAT_GNU) {
				strsize = tablesize - a_syms;
				tablesize = a_syms;
				stroff = a_syms;
			}

			textlen = a_text + (256 - 28);
			datalen = a_data;
			bsslen = a_bss;
			symoff = 0x100 + /* sizeof(extended exec header) */
				a_text +
				a_data +
				a_trsize +
				a_drsize;
		}
	}
	else if ((tabletype & 0xffffff00) == SYMBOL_FORMAT_ELF && (tabletype & 0xff) >= 40) { /* new MiNT+ELF */
		uint32_t magic;
		uint8_t e_ident[12];
		uint32_t dummy;
		uint32_t e_phoff, e_shoff;
		uint16_t e_type, e_machine, e_phnum, e_shentsize, e_shstrndx;
		uint16_t i;
		int strtabidx;

		/* skip to ELF program header */
		fseek(fp, (tabletype & 0xff) - 28, SEEK_CUR);
		tabletype = SYMBOL_FORMAT_ELF;
		/* symbol table size in GEMDOS header includes space of ELF headers, ignore it */
		tablesize = 0;

		reads  = fread(&magic, sizeof(magic), 1, fp);
		magic = be_swap32(magic);
		/* read rest of e_ident */
		reads += fread(e_ident, sizeof(e_ident), 1, fp);
		reads += fread(&e_type, sizeof(e_type), 1, fp);
		e_type = be_swap16(e_type);
		reads += fread(&e_machine, sizeof(e_machine), 1, fp);
		e_machine = be_swap16(e_machine);
		if (reads == 4 &&
		    magic == 0x7f454c46 && /* '\177ELF' */
		    e_ident[0] == 1 &&     /* ELFCLASS32 */
		    e_ident[1] == 2 &&     /* ELFDATA2MSB */
		    e_type == 2 &&         /* ET_EXEC */
		    e_machine == 4) {      /* EM_68K */
			reads  = fread(&dummy, sizeof(dummy), 1, fp);
			reads += fread(&dummy, sizeof(dummy), 1, fp);
			reads += fread(&e_phoff, sizeof(e_phoff), 1, fp);
			e_phoff = be_swap32(e_phoff);
			reads += fread(&e_shoff, sizeof(e_shoff), 1, fp);
			e_shoff = be_swap32(e_shoff);
			reads += fread(&dummy, sizeof(dummy), 1, fp);
			reads += fread(&dummy, sizeof(dummy), 1, fp);
			reads += fread(&e_phnum, sizeof(e_phnum), 1, fp);
			e_phnum = be_swap16(e_phnum);
			reads += fread(&e_shentsize, sizeof(e_shentsize), 1, fp);
			reads += fread(&e_shnum, sizeof(e_shnum), 1, fp);
			e_shnum = be_swap16(e_shnum);
			reads += fread(&e_shstrndx, sizeof(e_shstrndx), 1, fp);
			e_shstrndx = be_swap16(e_shstrndx);

			fseek(fp, e_shoff, SEEK_SET);
			headers = (struct elf_shdr *)malloc(sizeof(*headers) * e_shnum);
			if (headers == NULL) {
				perror("");
				return NULL;
			}

			strtabidx = -1;
			for (i = 0; i < e_shnum; i++) {
				struct elf_shdr *shdr = &headers[i];

				reads  = fread(&shdr->sh_name, 1, sizeof(shdr->sh_name), fp);
				shdr->sh_name = be_swap32(shdr->sh_name);
				reads += fread(&shdr->sh_type, 1, sizeof(shdr->sh_type), fp);
				shdr->sh_type = be_swap32(shdr->sh_type);
				reads += fread(&shdr->sh_flags, 1, sizeof(shdr->sh_flags), fp);
				shdr->sh_flags = be_swap32(shdr->sh_flags);
				reads += fread(&shdr->sh_addr, 1, sizeof(shdr->sh_addr), fp);
				shdr->sh_addr = be_swap32(shdr->sh_addr);
				reads += fread(&shdr->sh_offset, 1, sizeof(shdr->sh_offset), fp);
				shdr->sh_offset = be_swap32(shdr->sh_offset);
				reads += fread(&shdr->sh_size, 1, sizeof(shdr->sh_size), fp);
				shdr->sh_size = be_swap32(shdr->sh_size);
				reads += fread(&shdr->sh_link, 1, sizeof(shdr->sh_link), fp);
				shdr->sh_link = be_swap32(shdr->sh_link);
				reads += fread(&shdr->sh_info, 1, sizeof(shdr->sh_info), fp);
				shdr->sh_info = be_swap32(shdr->sh_info);
				reads += fread(&shdr->sh_addralign, 1, sizeof(shdr->sh_addralign), fp);
				shdr->sh_addralign = be_swap32(shdr->sh_addralign);
				reads += fread(&shdr->sh_entsize, 1, sizeof(shdr->sh_entsize), fp);
				shdr->sh_entsize = be_swap32(shdr->sh_entsize);

				if (shdr->sh_type == SHT_SYMTAB) {
					symoff = shdr->sh_offset;
					tablesize = shdr->sh_size;
					strtabidx = shdr->sh_link;
				}
			}
			if (strtabidx < 0 || strtabidx >= e_shnum ||
			    headers[strtabidx].sh_type != SHT_STRTAB) {
				tabletype = 0;
			} else {
				stroff = headers[strtabidx].sh_offset;
				strsize = headers[strtabidx].sh_size;
			}
		} else {
			tabletype = 0;
		}
		if (tabletype == 0) {
			fprintf(stderr, "ERROR: reading ELF header failed!\n");
			free(headers);
			return NULL;
		}
	} else {
		/* DRI: symbol table offset */
		symoff = 0x1C + textlen + datalen;
	}

	if (!symbols_print_prg_info(tabletype, prgflags, relocflag)) {
		free(headers);
		return NULL;
	}
	if (!tablesize) {
		fprintf(stderr, "ERROR: symbol table missing from the program!\n");
		free(headers);
		return NULL;
	}
	fprintf(stderr, "Program section sizes:\n  text: 0x%x, data: 0x%x, bss: 0x%x, symtab: 0x%x\n",
		textlen, datalen, bsslen, tablesize);

	sections[0].end = textlen;
	sections[1].end = datalen;
	sections[2].end = bsslen;
	/* add suitable offsets to section beginnings & ends, and validate them */
	if (!update_sections(sections)) {
		free(headers);
		return NULL;
	}

	/* go to start of symbol table */
	if (fseek(fp, symoff, SEEK_SET) < 0) {
		perror("ERROR: seeking to symbol table failed");
		free(headers);
		return NULL;
	}

	if (tabletype == SYMBOL_FORMAT_GNU) {
		fprintf(stderr, "Trying to load a.out symbol table at offset 0x%x...\n", symoff);
		symbols = symbols_load_gnu(fp, sections, tablesize, stroff, strsize, opts);

	} else if (tabletype == SYMBOL_FORMAT_ELF) {
		fprintf(stderr, "Trying to load ELF symbol table at offset 0x%x...\n", symoff);
		symbols = symbols_load_elf(fp, sections, tablesize, stroff, strsize, opts, headers, e_shnum);
		free(headers);

	} else {
		fprintf(stderr, "Trying to load DRI symbol table at offset 0x%x...\n", symoff);
		symbols = symbols_load_dri(fp, sections, tablesize, opts);
	}
	return symbols;
}
