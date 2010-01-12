/* 
 * Code to test Hatari symbol/address (re-)loading in src/debug/symbols.c
 */
#include <stdio.h>
#include <SDL_types.h>
#include <stdbool.h>
#include "symbols.h"
#include "main.h"

#define TEST_SYM_FILE "etos512.sym"

int main(int argc, const char *argv[])
{
	/* expected to fail */
	const char *fail_name[] = {
		"afoo",
		"zbar",
	};
	Uint32 fail_addr[] = {
		0x10,
		0x30,
	};
	/* expected to succeed */
	const char *success_name[] = {
		"_supexec",
		"_dos_mkdir",
		"_shel_find"
	};
	Uint32 success_addr[] = {
		0xe00dc6,
		0xe324d2,
	};

	int i, tests = 0, errors = 0;
	symbol_list_t *list = NULL;
	const char *name;
	Uint32 addr;

	list = Symbols_Load(TEST_SYM_FILE, 0, SYMTYPE_ANY);
	Symbols_ShowByAddress(list);
	fprintf(stderr, "\n");
	Symbols_ShowByName(list);
	Symbols_Free(list);

	list = Symbols_Load(TEST_SYM_FILE, 0, SYMTYPE_ANY);

	fprintf(stderr, "\nStuff that should FAIL:\n");
	for (i = 0; i < ARRAYSIZE(fail_name); i++) {
		name = fail_name[i];
		if (Symbols_MatchByName(list, SYMTYPE_ANY, name, 0)) {
			fprintf(stderr, "*** Unexpected SUCCESS from '%s' ***\n", name);
			errors++;
		} else {
			fprintf(stderr, "- '%s'\n", name);
		}
	}
	tests += i;
	for (i = 0; i < ARRAYSIZE(fail_addr); i++) {
		addr = fail_addr[i];
		name = Symbols_FindByAddress(list, addr);
		if (name) {
			fprintf(stderr, "*** Unexpected SUCCESS from 0x%08x (%s) ***\n", addr, name);
			errors++;
		} else {
			fprintf(stderr, "- 0x%08x\n", addr);
		}
	}
	tests += i;

	fprintf(stderr, "\nStuff that should SUCCEED:\n");
	for (i = 0; i < ARRAYSIZE(success_name); i++) {
		name = success_name[i];
		if (Symbols_MatchByName(list, SYMTYPE_ANY, name, 0)) {
			fprintf(stderr, "- '%s'\n", name);
		} else {
			fprintf(stderr, "*** Unexpected FAIL from '%s' ***\n", name);
			errors++;
		}
	}
	tests += i;
	for (i = 0; i < ARRAYSIZE(success_addr); i++) {
		addr = success_addr[i];
		name = Symbols_FindByAddress(list, addr);
		if (name) {
			fprintf(stderr, "- 0x%08x: %s\n", addr, name);
		} else {
			fprintf(stderr, "*** Unexpected FAIL from 0x%08x ***\n", addr);
			errors++;
		}
	}
	tests += i;
	
	if (errors) {
		fprintf(stderr, "\n***Detected %d ERRORs in %d automated tests!***\n\n",
			errors, tests);
	} else {
		fprintf(stderr, "\nFinished without any errors!\n\n");
	}
	return errors;
}
