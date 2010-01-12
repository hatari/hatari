/*
 * Hatari - symbols.c
 * 
 * Copyright (C) 2010 by Eero Tamminen
 * 
 * This file is distributed under the GNU Public License, version 2 or at
 * your option any later version. Read the file gpl.txt for details.
 * 
 * symbols.c - Hatari debugger symbol/address handling; parsing, sorting,
 * matching, TAB completion support etc.
 * 
 * Symbol/address file contents are identical to "nm" output i.e. composed
 * of a hexadecimal addresses followed by a space, letter indicating symbol
 * type (T = text/code, D = data, B = BSS), space and the symbol name.
 * Empty lines and lines starting with '#' are ignored.
 */
const char Symbols_fileid[] = "Hatari symbols.c : " __DATE__ " " __TIME__;

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <SDL_types.h>
#include "symbols.h"

/* how many characters the symbol name can have.
 * NOTE: change also sscanf width arg if you change this!!!
 */
#define MAX_SYM_SIZE 32

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
	fprintf(stderr, "WARNING: symbols '%s' & '%s' have the same 0x%x address\n",
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
		fprintf(stderr, "WARNING: symbol '%s' listed twice\n", name1);
	}
	return ret;
}


/**
 * Load symbols of given type and the symbol address addresses from
 * the given file and add given offset to the addresses.
 * Return symbols list or NULL for failure.
 */
symbol_list_t* Symbols_Load(const char *filename, Uint32 offset, symtype_t gettype)
{
	symbol_list_t *list;
	char symchar, buffer[80], name[MAX_SYM_SIZE+1], *buf;
	int count, line, symbols;
	symtype_t symtype;
	FILE *fp;
	
	if (!(fp = fopen(filename, "r"))) {
		fprintf(stderr, "ERROR: opening '%s' failed\n", filename);
		return NULL;
	}

	/* count content lines */
	symbols = 0;
	while (fgets(buffer, sizeof(buffer), fp)) {
		/* skip comments */
		if (*buffer == '#') {
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
		fprintf(stderr, "ERROR: no symbols/addresses in '%s'\n", filename);
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
		/* skip comments */
		if (*buffer == '#') {
			continue;
		}
		/* skip empty lines */
		for (buf = buffer; isspace(*buf); buf++);
		if (!*buf) {
			continue;
		}
		assert(count < symbols); /* file not modified in meanwhile? */
		if (sscanf(buffer, "%x %c %32[0-9A-Za-z_]s", &(list->names[count].address), &symchar, name) != 3) {
			fprintf(stderr, "ERROR: syntax error in '%s' on line %d\n", filename, line);
			continue;
		}
		switch (toupper(symchar)) {
		case 'T': symtype = SYMTYPE_TEXT; break;
		case 'D': symtype = SYMTYPE_DATA; break;
		case 'B': symtype = SYMTYPE_BSS; break;
		default:
			fprintf(stderr, "ERROR: unrecognized symbol type '%c' on line %d in '%s'\n", symchar, line, filename);
			continue;
		}
		if (gettype != SYMTYPE_ANY && gettype != symtype) {
			continue;
		}
		list->names[count].type = symtype;
		list->names[count].name = strdup(name);
		assert(list->names[count].name);
		count++;
	}

	assert(count);
	if (count < symbols) {
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
 * Helper for symbol name completion and finding their addresses.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
const symbol_t* Symbols_MatchByName(symbol_list_t* list, symtype_t symtype, const char *text, int state)
{
	const symbol_t *entry = list->names;
	static int i, len;
	
	if (!state)
	{
		/* first match */
		len = strlen(text);
		i = 0;
	}

	/* next match */
	while (i < list->count) {
		if ((symtype == SYMTYPE_ANY || entry[i].type == symtype) &&
		    strncmp(entry[i].name, text, len) == 0) {
			return &(entry[i++]);
		} else {
			i++;
		}
	}
	return NULL;
}


/**
 * Match symbols by address.
 * Return symbol name if address matches, NULL otherwise.
 */
const char* Symbols_FindByAddress(symbol_list_t* list, Uint32 addr)
{
	symbol_t *entries = list->addresses;
	/* left, right, middle */
        int l, r, m;
	Uint32 curr;

	/* bisect */
	l = 0;
	r = list->count - 1;
	do {
		m = (l+r) >> 1;
		curr = entries[m].address;
		if (curr == addr) {
			return (const char*)entries[m].name;
		}
		if (curr > addr) {
			r = m-1;
		} else {
			l = m+1;
		}
	} while (l <= r);
	return NULL;
}


/**
 * Show symbols sorted by address.
 */
void Symbols_ShowByAddress(symbol_list_t* list)
{
	symbol_t *entry;
	int i;
	if (!list) {
		fprintf(stderr, "No symbols!\n");
		return;
	}
	fprintf(stderr, "Symbols sorted by address:\n");
	for (entry = list->addresses, i = 0; i < list->count; i++, entry++) {
		fprintf(stderr, "  0x%08x: %s\n", entry->address, entry->name);
	}
}

/**
 * Show symbols sorted by name.
 */
void Symbols_ShowByName(symbol_list_t* list)
{
	symbol_t *entry;
	int i;
	if (!list) {
		fprintf(stderr, "No symbols!\n");
		return;
	}
	fprintf(stderr, "Symbols sorted by name:\n");
	for (entry = list->names, i = 0; i < list->count; i++, entry++) {
		fprintf(stderr, "  0x%08x: %s\n", entry->address, entry->name);
	}
}


/**
 * Free read symbols.
 */
void Symbols_Free(symbol_list_t* list)
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
	list->count = 0;	/* catch use of freed list */
	free(list);
}
