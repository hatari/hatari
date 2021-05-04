/*
 * Hatari - symbols-common.c
 *
 * Copyright (C) 2010-2021 by Eero Tamminen
 * Copyright (C) 2017,2021 by Thorsten Otto
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

typedef struct {
	char *name;
	uint32_t address;
	symtype_t type;
	bool name_allocated;
} symbol_t;

typedef struct {
	int symbols;		/* initial symbol count */
	int namecount;		/* final symbol count */
	int codecount;		/* TEXT symbol address count */
	int datacount;		/* DATA/BSS symbol address count */
	symbol_t *addresses;	/* TEXT + DATA/BSS items sorted by address */
	symbol_t *names;	/* all items sorted by symbol name */
	char *strtab;		/* from a.out only */
	char *debug_strtab;	/* from pure-c debug information only */
} symbol_list_t;

typedef struct {
	uint32_t offset;
	uint32_t end;
} prg_section_t;

typedef struct {
	symtype_t notypes;
	bool no_local;
	bool no_obj;
	bool sort_name;
} symbol_opts_t;

typedef struct {
	int debug;    /* debug symbols */
	int locals;   /* unnamed / local symbols */
	int gccint;   /* GCC internal symbols */
	int files;    /* object file names */
	int weak;     /* weak (undefined) symbols */
	int invalid;  /* invalid symbol types for addresses */
	int unwanted; /* explicitly disabed symbol types */
} ignore_counts_t;

/* Magic used to denote different symbol table formats */
#define SYMBOL_FORMAT_GNU  0x474E555f	/* "MiNT" */
#define SYMBOL_FORMAT_MINT 0x4D694E54	/* "GNU_" */
#define SYMBOL_FORMAT_DRI  0x0

/* Magic identifying Atari programs */
#define ATARI_PROGRAM_MAGIC 0x601A


/* ------------------ symbol comparisons ------------------ */

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
			fprintf(stderr, "WARNING: symbols '%s' & '%s' have the same 0x%x address.\n",
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
			fprintf(stderr, "WARNING: addresses 0x%x & 0x%x have the same '%s' name.\n",
				syms[i].address, syms[j].address, syms[i].name);
			dups++;
			i = j;
		}
	}
	return dups;
}


/* ----------------- symbol list alloc / free ------------------ */

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
	int i;

	if (!list) {
		return;
	}
	assert(list->namecount);
	for (i = 0; i < list->namecount; i++) {
		if (list->names[i].name_allocated)
			free(list->names[i].name);
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
	return SDL_SwapBE32(*p32);
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

	fseek(fp, 0, SEEK_END);
	filesize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	buf = malloc(filesize);
	if (buf == NULL)
	{
		perror("");
		return 0;
	}
	nread = fread(buf, 1, filesize, fp);
	if (nread != filesize)
	{
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
		if (first_reloc != 0)
		{
			while (reloc_offset < filesize && buf[reloc_offset] != 0)
				reloc_offset++;
			reloc_offset++;
		}
		if (reloc_offset & 1)
			reloc_offset++;
		debug_offset = reloc_offset;
	}

	if (debug_offset + SIZEOF_PDB_HEADER >= filesize)
	{
		/* fprintf(stderr, "no debug information present\n"); */
		/* this is not an error */
		free(buf);
		return 1;
	}
	read_pc_debug_header(buf + debug_offset, &pdb_h);
	if (pdb_h.magic != 0x51444231UL) /* 'QDB1' (in executables) */
	{
		fprintf(stderr, "ERROR: unknown debug format 0x%08lx\n", (unsigned long)pdb_h.magic);
		free(buf);
		return 0;
	}
	if (pdb_h.size_stringtable == 0)
	{
		free(buf);
		return 0;
	}
	fprintf(stderr, "Reading symbol names from Pure-C debug information.\n");

	list->debug_strtab = (char *)malloc(pdb_h.size_stringtable);
	if (list->debug_strtab == NULL)
	{
		perror("");
		return 0;
	}

	varinfo_offset = SIZEOF_PDB_HEADER + debug_offset + pdb_h.size_fileinfos + pdb_h.size_lineinfo;
	strtable_offset = varinfo_offset + pdb_h.size_varinfo + pdb_h.size_unknown + pdb_h.size_typeinfo + pdb_h.size_structinfo;
	if (strtable_offset >= filesize || strtable_offset + pdb_h.size_stringtable > filesize)
	{
		free(buf);
		return 0;
	}
	memcpy(list->debug_strtab, buf + strtable_offset, pdb_h.size_stringtable);

	if (pdb_h.size_varinfo != 0)
	{
		int i;
		for (i = 0; i < list->namecount; i++)
		{
			uint8_t storage;
			switch (list->names[i].type)
			{
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
			if (storage != PDB_STORAGE_NONE)
			{
				uint8_t *p, *end;
				int len = (int)strlen(list->names[i].name);
				/*
				 * only need to care about possibly truncated names
				 */
				if (len != 8 && len != 22)
					continue;
				/*
				 * Fixme: slurp the infos all in, and sort them so we can do a binary search
				 */
				p = buf + varinfo_offset;
				end = p + pdb_h.size_varinfo;
				while (p < end)
				{
					struct pdb_varinfo info;

					read_varinfo(p, &info);
					if (info.storage == storage && info.value == list->names[i].address &&
					    ((storage == PDB_STORAGE_TEXT && (info.type == 7 || info.type == 8)) ||
					     ((storage == PDB_STORAGE_DATA || storage == PDB_STORAGE_BSS) && (info.type == 4 || info.type == 5 || info.type == 6))))
					{
						char *name = (char *)buf + strtable_offset + info.name_offset;
						if (strcmp(list->names[i].name, name) != 0)
						{
							if (list->names[i].name_allocated)
								free(list->names[i].name);
							list->names[i].name = list->debug_strtab + info.name_offset;
							list->names[i].name_allocated = false;
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
 * return true if given symbol name is object/library/file name
 */
static bool is_file_name(const char *name)
{
	int len = strlen(name);
	/* object (.a or .o) / file name? */
	if (len > 2 && ((name[len-2] == '.' && (name[len-1] == 'a' || name[len-1] == 'o')) || strchr(name, '/'))) {
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
 * Return true if symbol should be ignored based on its name & type
 * and given options, and increase appropiate ignore count
 */
static bool ignore_symbol(const char *name, symtype_t symtype, const symbol_opts_t *opts, ignore_counts_t *counts)
{
	if (opts->notypes & symtype) {
		counts->unwanted++;
		return true;
	}
	if (opts->no_local) {
		if (name[0] == '.' && name[1] == 'L') {
			counts->locals++;
			return true;
		}
	}
	if (opts->no_obj) {
		if (is_gcc_internal(name)) {
			counts->gccint++;
			return true;
		}
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
	if (counts->locals) {
		fprintf(stderr, "NOTE: ignored %d unnamed / local symbols ('.L*').\n", counts->locals);
	}
	if (counts->gccint) {
		fprintf(stderr, "NOTE: ignored %d GCC internal symbols.\n", counts->gccint);
	}
	if (counts->files) {
		/* object file path names most likely get truncated and
		 * as result cause unnecessary symbol name conflicts
		 * in addition to object file addresses conflicting
		 * with first symbol in the object file.
		 */
		fprintf(stderr, "NOTE: ignored %d file symbols ('*.[ao]'|'*/*').\n", counts->files);
	}
	if (counts->weak) {
		fprintf(stderr, "NOTE: ignored %d weak / undefined symbols.\n", counts->weak);
	}
	if (counts->invalid) {
		fprintf(stderr, "NOTE: ignored %d invalid symbols.\n", counts->invalid);
	}
	if (counts->unwanted) {
		fprintf(stderr, "NOTE: ignored %d other unwanted symbol types.\n", counts->unwanted);
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
static symbol_list_t* symbols_load_gnu(FILE *fp, const prg_section_t *sections, uint32_t tablesize, Uint32 stroff, Uint32 strsize, const symbol_opts_t *opts)
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

	memset(&ignore, 0, sizeof(ignore));
	count = 0;

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
			ignore.invalid++;
			continue;
		}
		if (strx >= strsize) {
			fprintf(stderr, "symbol name index %x out of range\n", (unsigned int)strx);
			ignore.invalid++;
			continue;
		}
		name = list->strtab + strx + stroff;

		if (n_type & N_STAB)
		{
			ignore.debug++;
			continue;
		}
		section = NULL;
		switch (n_type & (N_TYPE|N_EXT))
		{
		case N_UNDF:
		case N_UNDF|N_EXT:
			/* shouldn't happen here */
			ignore.weak++;
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
		case N_FN: /* filename symbol */
			ignore.files++;
			continue;
		case N_SIZE:
		case N_WARNING:
		case N_SETA:
		case N_SETT:
		case N_SETD:
		case N_SETB:
		case N_SETV:
			ignore.debug++;
			continue;
		case N_WEAKU:
		case N_WEAKT:
		case N_WEAKD:
		case N_WEAKB:
			ignore.weak++;
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
			(((n_type & N_EXT) && (n_type & N_TYPE) == N_UNDF && address != 0)))
		{
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


/* ---------- program info + symbols loading ------------- */

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
	Uint32 textlen, datalen, bsslen, tablesize, tabletype, prgflags;
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
	fprintf(stderr, "Program section sizes:\n  text: 0x%x, data: 0x%x, bss: 0x%x, symtab: 0x%x\n",
		textlen, datalen, bsslen, tablesize);

	sections[0].end = textlen;
	sections[1].end = datalen;
	sections[2].end = bsslen;
	/* add suitable offsets to section beginnings & ends, and validate them */
	if (!update_sections(sections)) {
		return NULL;
	}

	if (tabletype == SYMBOL_FORMAT_GNU) {
		/* go to start of symbol table */
		offset = symoff;
		if (fseek(fp, offset, SEEK_SET) < 0) {
			perror("ERROR: seeking to symbol table failed");
			return NULL;
		}
		fprintf(stderr, "Trying to load a.out symbol table at offset 0x%x...\n", offset);
		symbols = symbols_load_gnu(fp, sections, tablesize, stroff, strsize, opts);
	} else {
		/* go to start of symbol table */
		offset = 0x1C + textlen + datalen;
		if (fseek(fp, offset, SEEK_SET) < 0) {
			perror("ERROR: seeking to symbol table failed");
			return NULL;
		}
		fprintf(stderr, "Trying to load DRI symbol table at offset 0x%x...\n", offset);
		symbols = symbols_load_dri(fp, sections, tablesize, opts);
	}
	return symbols;
}
