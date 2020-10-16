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

typedef struct {
	char *name;
	Uint32 address;
	symtype_t type;
} symbol_t;

typedef struct {
	int symbols;		/* initial symbol count */
	int namecount;		/* final symbol count */
	int codecount;		/* TEXT symbol address count */
	int datacount;		/* DATA/BSS symbol address count */
	symbol_t *addresses;	/* TEXT + DATA/BSS items sorted by address */
	symbol_t *names;	/* all items sorted by symbol name */
	char *strtab;
} symbol_list_t;

typedef struct {
	Uint32 offset;
	Uint32 end;
} prg_section_t;


/* how many characters the symbol name can have.
 * NOTE: change also sscanf width arg if you change this!!!
 */
#define MAX_SYM_SIZE 32

/* Magic used to denote different symbol table formats */
#define SYMBOL_FORMAT_GNU  0x474E555f	/* "MiNT" */
#define SYMBOL_FORMAT_MINT 0x4D694E54	/* "GNU_" */
#define SYMBOL_FORMAT_DRI  0x0


/* TODO: add symbol name/address file names to configuration? */
static symbol_list_t *CpuSymbolsList;
static symbol_list_t *DspSymbolsList;

/* path for last loaded program (through GEMDOS HD emulation) */
static char *CurrentProgramPath;
/* whether current symbols were loaded from a program file */
static bool SymbolsAreForProgram;
/* prevent repeated failing on every debugger invocation */
static bool AutoLoadFailed;


/* ------------------ load and free functions ------------------ */

/**
 * return true if given symbol name is object/library/file name
 */
static bool is_obj_file(const char *name)
{
	int len = strlen(name);
	/* object (.a or .o) / file name? */
	if (len > 2 && ((name[len-2] == '.' && (name[len-1] == 'a' || name[len-1] == 'o')) || strchr(name, '/'))) {
		    return true;
	}
	return false;
}

/**
 * compare function for qsort() to sort according to
 * symbol type & address.  Text section symbols will
 * be sorted first.
 */
static int symbols_by_address(const void *s1, const void *s2)
{
	const symbol_t *sym1 = (const symbol_t*)s1;
	const symbol_t *sym2 = (const symbol_t*)s2;

	/* separate TEXT type addresses from others */
	if (sym1->type != sym2->type) {
		if (sym1->type == SYMTYPE_TEXT) {
			return -1;
		}
		if (sym2->type == SYMTYPE_TEXT) {
			return 1;
		}
	}
	/* then sort by address */
	if (sym1->address < sym2->address) {
		return -1;
	}
	if (sym1->address > sym2->address) {
		return 1;
	}
	return 0;
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
 * Check for duplicate addresses in symbol list
 * (called separately for TEXT & non-TEXT symbols)
 * Return number of duplicates
 */
static int symbols_check_addresses(const symbol_t *syms, int count)
{
	int i, j, dups = 0;

	for (i = 0; i < (count - 1); i++)
	{
		/* absolute symbols have values, not addresses */
		if (syms[i].type == SYMTYPE_ABS) {
			continue;
		}
		for (j = i + 1; j < count && syms[i].address == syms[j].address; j++) {
			if (syms[j].type == SYMTYPE_ABS) {
				continue;
			}
			/* ASCII symbol files contain also object file addresses,
			 * those will often have the same address as the first symbol
			 * in given object -> no point warning about them
			 */
			if (is_obj_file(syms[i].name) || is_obj_file(syms[j].name)) {
				continue;
			}
			fprintf(stderr,	"WARNING: symbols '%s' & '%s' have the same 0x%x address\n",
				syms[i].name, syms[j].name, syms[i].address);
			dups++;
			i = j;
		}
	}
	return dups;
}

/**
 * Check for duplicate names in symbol list
 * Return number of duplicates
 */
static int symbols_check_names(const symbol_t *syms, int count)
{
	int i, j, dups = 0;

	for (i = 0; i < (count - 1); i++)
	{
		for (j = i + 1; j < count && strcmp(syms[i].name, syms[j].name) == 0; j++) {
			/* this is common case for object files having different sections */
			if (syms[i].type != syms[j].type && is_obj_file(syms[i].name)) {
				continue;
			}
			fprintf(stderr,	"WARNING: addresses 0x%x & 0x%x have the same '%s' name\n",
				syms[i].address, syms[j].address, syms[i].name);
			dups++;
			i = j;
		}
	}
	return dups;
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
	case SYMTYPE_ABS:  return 'A';
	default: return '?';
	}
}

/**
 * Return true if symbol name matches internal GCC symbol name,
 * or is object / file name.
 */
static bool symbol_remove_obj(const char *name)
{
	static const char *gcc_sym[] = {
		"___gnu_compiled_c",
		"gcc2_compiled."
	};
	int i;

	if (is_obj_file(name)) {
		return true;
	}
	/* useless symbols GCC (v2) seems to add to every object? */
	for (i = 0; i < ARRAY_SIZE(gcc_sym); i++) {
		if (strcmp(name, gcc_sym[i]) == 0) {
			return true;
		}
	}
	return false;
}


/**
 * Load symbols of given type and the symbol address addresses from
 * DRI/GST format symbol table, and add given offsets to the addresses:
 *	http://toshyp.atari.org/en/005005.html
 * Return symbols list or NULL for failure.
 */
static symbol_list_t* symbols_load_dri(FILE *fp, prg_section_t *sections, symtype_t gettype, Uint32 tablesize)
{
	int i, count, symbols, invalid;
	int notypes, dtypes, locals, ofiles;
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

	invalid = dtypes = notypes = ofiles = locals = count = 0;
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
			if ((symid & 0x4000) == 0x4000) {
				symtype = SYMTYPE_ABS;
				section = NULL;
				break;
			}
			fprintf(stderr, "WARNING: ignoring symbol '%s' in slot %d of unknown type 0x%x.\n", name, i, symid);
			invalid++;
			continue;
		}
		if (!(gettype & symtype)) {
			notypes++;
			continue;
		}
		if (name[0] == '.' && name[1] == 'L') {
			locals++;
			continue;
		}
		if (symbol_remove_obj(name)) {
			ofiles++;
			continue;
		}
		if (section) {
			address += section->offset;
			if (address > section->end) {
				fprintf(stderr, "WARNING: ignoring symbol '%s' of type %c in slot %d with invalid offset 0x%x (>= 0x%x).\n",
					name, symbol_char(symtype), i, address, section->end);
				invalid++;
				continue;
			}
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
	list->symbols = symbols;
	list->namecount = count;

	if (invalid) {
		fprintf(stderr, "NOTE: ignored %d invalid symbols.\n", invalid);
	}
	if (dtypes) {
		fprintf(stderr, "NOTE: ignored %d debugging symbols.\n", dtypes);
	}
	if (notypes) {
		fprintf(stderr, "NOTE: ignored %d other unwanted symbol types.\n", notypes);
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
		fprintf(stderr, "NOTE: ignored %d object symbols (= name has '/', ends in '.[ao]' or is GCC internal).\n", ofiles);
	}
	return list;
}


/**
 * Load symbols of given type and the symbol address addresses from
 * a.out format symbol table, and add given offsets to the addresses.
 * Return symbols list or NULL for failure.
 */
static symbol_list_t* symbols_load_gnu(FILE *fp, prg_section_t *sections, symtype_t gettype, Uint32 tablesize, Uint32 stroff, Uint32 strsize)
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
	int dtypes, locals, ofiles, count, notypes, invalid, weak;
	prg_section_t *section;

	if (!(list = symbol_list_alloc(slots))) {
		return NULL;
	}

	list->strtab = (char *)malloc(tablesize + strsize);

	if (list->strtab == NULL)
	{
		symbol_list_free(list);
		return NULL;
	}

	nread = fread(list->strtab, tablesize + strsize, 1, fp);
	if (nread != 1)
	{
		perror("ERROR: reading symbols failed");
		symbol_list_free(list);
		return NULL;
	}

	p = (unsigned char *)list->strtab;
	sym = list->names;

	weak = invalid = dtypes = notypes = ofiles = locals = count = 0;
	for (i = 0; i < slots; i++)
	{
		strx = SDL_SwapBE32(*(Uint32*)p);
		p += 4;
		n_type = *p++;
		n_other = *p++;
		n_desc = SDL_SwapBE16(*(Uint16*)p);
		p += 2;
		address = SDL_SwapBE32(*(Uint32*)p);
		p += 4;
		name = dummy;
		if (!strx) {
			invalid++;
			continue;
		}
		if (strx >= strsize) {
			fprintf(stderr, "symbol name index %x out of range\n", (unsigned int)strx);
			invalid++;
			continue;
		}
		name = list->strtab + strx + stroff;

		if (n_type & N_STAB)
		{
			dtypes++;
			continue;
		}
		section = NULL;
		switch (n_type & (N_TYPE|N_EXT))
		{
		case N_UNDF:
		case N_UNDF|N_EXT:
			/* shouldn't happen here */
			weak++;
			continue;
		case N_ABS:
		case N_ABS|N_EXT:
			symtype = SYMTYPE_ABS;
			break;
		case N_TEXT:
		case N_TEXT|N_EXT:
			symtype = SYMTYPE_TEXT;
			section = &(sections[0]);
			break;
		case N_DATA:
		case N_DATA|N_EXT:
			symtype = SYMTYPE_DATA;
			section = &(sections[1]);
			break;
		case N_BSS:
		case N_BSS|N_EXT:
		case N_COMM:
		case N_COMM|N_EXT:
			symtype = SYMTYPE_BSS;
			section = &(sections[2]);
			break;
		case N_FN: /* filenames, not object addresses? */
			dtypes++;
			continue;
		case N_SIZE:
		case N_WARNING:
		case N_SETA:
		case N_SETT:
		case N_SETD:
		case N_SETB:
		case N_SETV:
			dtypes++;
			continue;
		case N_WEAKU:
		case N_WEAKT:
		case N_WEAKD:
		case N_WEAKB:
			weak++;
			continue;
		default:
			fprintf(stderr, "WARNING: ignoring symbol '%s' in slot %u of unknown type 0x%x.\n", name, (unsigned int)i, n_type);
			invalid++;
			continue;
		}
		/*
		 * the value of a common symbol is its size, not its address:
		 */
		if (((n_type & N_TYPE) == N_COMM) ||
			(((n_type & N_EXT) && (n_type & N_TYPE) == N_UNDF && address != 0)))
		{
			/* if we ever want to know a symbols size, get that here */
			fprintf(stderr, "WARNING: ignoring common symbol '%s' in slot %u.\n", name, (unsigned int)i);
			dtypes++;
			continue;
		}
		if (!(gettype & symtype)) {
			notypes++;
			continue;
		}
		if (name[0] == '.' && name[1] == 'L') {
			locals++;
			continue;
		}
		if (symbol_remove_obj(name)) {
			ofiles++;
			continue;
		}
		if (section) {
			address += sections[0].offset;	/* all GNU symbol addresses are TEXT relative */
			if (address > section->end) {
				fprintf(stderr, "WARNING: ignoring symbol '%s' of type %c in slot %u with invalid offset 0x%x (>= 0x%x).\n",
					name, symbol_char(symtype), (unsigned int)i, address, section->end);
				invalid++;
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

	if (invalid) {
		fprintf(stderr, "NOTE: ignored %d invalid symbols.\n", invalid);
	}
	if (dtypes) {
		fprintf(stderr, "NOTE: ignored %d debugging symbols.\n", dtypes);
	}
	if (weak) {
		fprintf(stderr, "NOTE: ignored %d weak / undefined symbols.\n", weak);
	}
	if (notypes) {
		fprintf(stderr, "NOTE: ignored %d other unwanted symbol types.\n", notypes);
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
		fprintf(stderr, "NOTE: ignored %d object symbols (= name has '/', ends in '.[ao]' or is GCC internal).\n", ofiles);
	}
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
	case SYMBOL_FORMAT_MINT: /* "MiNT" */
		info = "GCC/MiNT executable, GST symbol table";
		break;
	case SYMBOL_FORMAT_GNU:	 /* "GNU_" */
		info = "GCC/MiNT executable, a.out symbol table";
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
 * Return symbols list or NULL for failure.
 */
static symbol_list_t* symbols_load_binary(FILE *fp, symtype_t gettype)
{
	Uint32 textlen, datalen, bsslen, start, tablesize, tabletype, prgflags;
	prg_section_t sections[3];
	int offset, reads = 0;
	Uint16 relocflag;
	symbol_list_t* symbols;
	Uint32 symoff = 0;
	Uint32 stroff = 0;
	Uint32 strsize = 0;

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
	/*
	 * check for GNU-style symbol table in aexec header
	 */
	if (tabletype == SYMBOL_FORMAT_MINT) { /* MiNT */
		Uint32 magic1, magic2;
		Uint32 dummy;
		Uint32 a_text, a_data, a_bss, a_syms, a_entry, a_trsize, a_drsize;
		Uint32 g_tparel_pos, g_tparel_size, g_stkpos, g_symbol_format;

		reads  = fread(&magic1, sizeof(magic1), 1, fp);
		magic1 = SDL_SwapBE32(magic1);
		reads += fread(&magic2, sizeof(magic2), 1, fp);
		magic2 = SDL_SwapBE32(magic2);
		if (reads == 2 &&
			((magic1 == 0x283a001a && magic2 == 0x4efb48fa) || 	/* Original binutils: move.l 28(pc),d4; jmp 0(pc,d4.l) */
			 (magic1 == 0x203a001a && magic2 == 0x4efb08fa))) {	/* binutils >= 2.18-mint-20080209: move.l 28(pc),d0; jmp 0(pc,d0.l) */
			reads += fread(&dummy, sizeof(dummy), 1, fp);	/* skip a_info */
			reads += fread(&a_text, sizeof(a_text), 1, fp);
			a_text = SDL_SwapBE32(a_text);
			reads += fread(&a_data, sizeof(a_data), 1, fp);
			a_data = SDL_SwapBE32(a_data);
			reads += fread(&a_bss, sizeof(a_bss), 1, fp);
			a_bss = SDL_SwapBE32(a_bss);
			reads += fread(&a_syms, sizeof(a_syms), 1, fp);
			a_syms = SDL_SwapBE32(a_syms);
			reads += fread(&a_entry, sizeof(a_entry), 1, fp);
			a_entry = SDL_SwapBE32(a_entry);
			reads += fread(&a_trsize, sizeof(a_trsize), 1, fp);
			a_trsize = SDL_SwapBE32(a_trsize);
			reads += fread(&a_drsize, sizeof(a_drsize), 1, fp);
			a_drsize = SDL_SwapBE32(a_drsize);
			reads += fread(&g_tparel_pos, sizeof(g_tparel_pos), 1, fp);
			g_tparel_pos = SDL_SwapBE32(g_tparel_pos);
			reads += fread(&g_tparel_size, sizeof(g_tparel_size), 1, fp);
			g_tparel_size = SDL_SwapBE32(g_tparel_size);
			reads += fread(&g_stkpos, sizeof(g_stkpos), 1, fp);
			g_stkpos = SDL_SwapBE32(g_stkpos);
			reads += fread(&g_symbol_format, sizeof(g_symbol_format), 1, fp);
			g_symbol_format = SDL_SwapBE32(g_symbol_format);
			if (g_symbol_format == 0)
			{
				tabletype = SYMBOL_FORMAT_GNU;
			}
			if ((a_text + (256 - 28)) != textlen)
				fprintf(stderr, "warning: inconsistent text segment size %08x != %08x\n", textlen, a_text + (256 - 28));
			if (a_data != datalen)
				fprintf(stderr, "warning: inconsistent data segment size %08x != %08x\n", datalen, a_data);
			if (a_bss != bsslen)
				fprintf(stderr, "warning: inconsistent bss segment size %08x != %08x\n", bsslen, a_bss);
			/*
			 * the symbol table size in the GEMDOS header includes the string table,
			 * the symbol table size in the exec header does not.
			 */
			if (tabletype == SYMBOL_FORMAT_GNU)
			{
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
	if (DebugInfo_GetTEXTEnd() != sections[0].end) {
		fprintf(stderr, "ERROR: given program TEXT section size differs from one in RAM!\n");
		return NULL;
	}

	start = DebugInfo_GetDATA();
	sections[1].offset = start;
	sections[1].end = start + datalen;

	start = DebugInfo_GetBSS();
	sections[2].offset = start;
	sections[2].end = start + bsslen;

	if (sections[0].end != sections[1].offset) {
		fprintf(stderr, "WARNING: DATA start doesn't match TEXT start + size!\n");
	}
	if (sections[1].end != sections[2].offset) {
		fprintf(stderr, "WARNING: BSS start doesn't match DATA start + size!\n");
	}

	if (tabletype == SYMBOL_FORMAT_GNU) {
		/* go to start of symbol table */
		offset = symoff;
		if (fseek(fp, offset, SEEK_SET) < 0) {
			perror("ERROR: seeking to symbol table failed");
			return NULL;
		}
		fprintf(stderr, "Trying to load symbol table at offset 0x%x...\n", offset);
		symbols = symbols_load_gnu(fp, sections, gettype, tablesize, stroff, strsize);
	} else {
		/* go to start of symbol table */
		offset = 0x1C + textlen + datalen;
		if (fseek(fp, offset, SEEK_SET) < 0) {
			perror("ERROR: seeking to symbol table failed");
			return NULL;
		}
		fprintf(stderr, "Trying to load symbol table at offset 0x%x...\n", offset);
		symbols = symbols_load_dri(fp, sections, gettype, tablesize);
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
	int i;

	if (!list) {
		return;
	}
	assert(list->namecount);
	if (list->strtab) {
		free(list->strtab);
		list->strtab = NULL;
	} else {
		for (i = 0; i < list->namecount; i++) {
			free(list->names[i].name);
		}
	}
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
