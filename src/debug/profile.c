/*
 * Hatari - profile.c
 * 
 * Copyright (C) 2010-2013 by Eero Tamminen
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * profile.c - profile caller info handling and debugger parsing functions
 */
const char Profile_fileid[] = "Hatari profile.c : " __DATE__ " " __TIME__;

#include <stdio.h>
#include "main.h"
#include "version.h"
#include "debugui.h"
#include "debug_priv.h"
#include "configuration.h"
#include "clocks_timings.h"
#include "evaluate.h"
#include "profile.h"
#include "profile_priv.h"
#include "symbols.h"


/* ------------------ CPU/DSP caller information handling ----------------- */

static const struct {
	char chr;
	calltype_t bit;
	const char *info;
} flaginfo[] = {
	{ 'u', CALL_UNKNOWN,	"unknown PC change" },
	{ 'n', CALL_NEXT,	"PC moved to next instruction", },
	{ 'b', CALL_BRANCH,	"branch/jump" },
	{ 's', CALL_SUBROUTINE,	"subroutine call" },
	{ 'r', CALL_SUBRETURN,	"return from subroutine" },
	{ 'e', CALL_EXCEPTION,	"exception" },
	{ 'x', CALL_EXCRETURN,	"return from exception" }
};

/**
 * compare function for qsort() to sort caller data by calls
 */
static int cmp_callers(const void *c1, const void *c2)
{
	Uint32 count1 = ((const caller_t*)c1)->count;
	Uint32 count2 = ((const caller_t*)c2)->count;
	if (count1 > count2) {
		return -1;
	}
	if (count1 < count2) {
		return 1;
	}
	return 0;
}

/*
 * Show collected CPU/DSP callee/caller information.
 *
 * Hint: As caller info list is based on number of loaded symbols,
 * load only text symbols to save memory & make things faster...
 */
void Profile_ShowCallers(FILE *fp, int sites, callee_t *callsite, const char * (*addr2name)(Uint32, Uint64 *))
{
	int typecount, countissues, countdiff;
	int i, j, k;
	const char *name;
	caller_t *info;
	Uint64 total;
	Uint32 addr, typeaddr;

	/* legend */
	fputs("# ", fp);
	for (k = 0; k < ARRAYSIZE(flaginfo); k++) {
		fprintf(fp, "%c = %s, ", flaginfo[k].chr, flaginfo[k].info);
	}
	fputs("\n", fp);

	countdiff = 0;
	countissues = 0;
	for (i = 0; i < sites; i++, callsite++) {
		addr = callsite->addr;
		if (!addr) {
			continue;
		}
		name = addr2name(addr, &total);
		fprintf(fp, "0x%x: ", callsite->addr);

		typeaddr = 0;
		info = callsite->callers;
		qsort(info, callsite->count, sizeof(*info), cmp_callers);
		for (j = 0; j < callsite->count; j++, info++) {
			if (!info->addr) {
				break;
			}
			fprintf(fp, "0x%x = %d", info->addr, info->count);
			total -= info->count;
			if (info->flags) {	/* calltypes supported? */
				fputs(" ", fp);
				typecount = 0;
				for (k = 0; k < ARRAYSIZE(flaginfo); k++) {
					if (info->flags & flaginfo[k].bit) {
						fputc(flaginfo[k].chr, fp);
						typecount++;
					}
				}
				if (typecount > 1) {
					typeaddr = info->addr;
				}
			}
			fputs(", ", fp);
		}
		if (name) {
			fprintf(fp, "(%s)", name);
		}
		fputs("\n", fp);
		if (total) {
#if DEBUG
			fprintf(stderr, "WARNING: %llu differences in call and instruction counts for '%s'!\n", total, name);
#endif
			countdiff += total;
			countissues++;
		}
		if (typeaddr) {
			fprintf(stderr, "WARNING: different types of calls (at least) from 0x%x,\n\t has its codechanged during profiling?\n", typeaddr);
		}
	}
	if (countissues) {
		if (countdiff <= 2 && countissues == countdiff) {
			fprintf(stderr, "WARNING: callcount mismatches (%d calls) with address instruction\n\t counts in %d cases, most likely profile start & end.\n",
				countdiff, countissues);
		} else {
			/* profiler bug: some (address?) mismatch in recording instruction counts and call counts */
			fprintf(stderr, "ERROR: callcount mismatches with address instruction counts\n\t(%d in total) detected in %d cases!\n",
				countdiff, countissues);
		}
	}
}

/**
 * Update CPU/DSP callee / caller information, if called address contains
 * symbol address (= function, or other interesting place in code)
 */
void Profile_UpdateCaller(callee_t *callsite, Uint32 pc, Uint32 caller, calltype_t flag)
{
	int i, count;
	caller_t *info;

	/* need to store real call addresses as symbols can change
	 * after profiling has been stopped
	 */
	info = callsite->callers;
	if (!info) {
		info = calloc(1, sizeof(*info));
		if (!info) {
			fprintf(stderr, "ERROR: caller info alloc failed!\n");
			return;
		}
		/* first call to this address, save address */
		callsite->addr = pc;
		callsite->callers = info;
		callsite->count = 1;
	}
	/* how many caller slots are currently allocated? */
	count = callsite->count;
	for (;;) {
		for (i = 0; i < count; i++, info++) {
			if (info->addr == caller) {
				info->flags |= flag;
				info->count++;
				return;
			}
			if (!info->addr) {
				/* empty slot */
				info->addr = caller;
				info->flags |= flag;
				info->count = 1;
				return;
			}
		}
		/* not enough, double caller slots */
		count *= 2;
		info = realloc(callsite->callers, count * sizeof(*info));
		if (!info) {
			fprintf(stderr, "ERROR: caller info alloc failed!\n");
			return;
		}
		memset(info + callsite->count, 0, callsite->count * sizeof(*info));
		callsite->callers = info;
		callsite->count = count;
	}
}

int Profile_AllocCallerInfo(const char *info, int oldcount, int count, callee_t **callsite)
{
	if (*callsite) {
		/* free old data */
		int i;
		callee_t *site = *callsite;
		for (i = 0; i < oldcount; i++, site++) {
			if (site->callers) {
				free(site->callers);
			}
		}
		free(*callsite);
		*callsite = NULL;
	}
	if (count) {
		/* alloc & clear new data */
		*callsite = calloc(count, sizeof(callee_t));
		if (*callsite) {
			printf("Allocated %s profile callsite buffer for %d symbols.\n", info, count);
			return count;
		}
	}
	return 0;
}


/* ------------------- command parsing ---------------------- */

/**
 * Readline match callback to list profile subcommand names.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char *Profile_Match(const char *text, int state)
{
	static const char *names[] = {
		"addresses", "callers", "counts", "cycles", "misses", "off", "on", "save", "stats", "symbols"
	};
	static int i, len;
	
	if (!state)
	{
		/* first match */
		i = 0;
		len = strlen(text);
	}
	/* next match */
	while (i < ARRAYSIZE(names)) {
		if (strncasecmp(names[i++], text, len) == 0)
			return (strdup(names[i-1]));
	}
	return NULL;
}

const char Profile_Description[] =
	  "<on|off|stats|counts|cycles|misses|symbols|callers|addresses|save> [count|address|file]\n"
	  "\t'on' & 'off' enable and disable profiling.  Data is collected\n"
	  "\tuntil debugger is entered again at which point you get profiling\n"
	  "\tstatistics ('stats') summary.\n"
	  "\n"
	  "\tThen you can ask for list of the PC addresses, sorted either by\n"
	  "\texecution 'counts', used 'cycles' or cache 'misses'. First can\n"
	  "\tbe limited just to named addresses with 'symbols'.  Optional\n"
	  "\tcount will limit how many items will be shown.\n"
	  "\n"
	  "\t'addresses' lists the profiled addresses in order, with the\n"
	  "\tinstructions (currently) residing at them.  By default this\n"
	  "\tstarts from the first executed instruction, or you can\n"
	  "\tspecify the starting address.\n"
	  "\n"
	  "\t'callers' shows (raw) caller information for addresses which\n"
	  "\thad symbol(s) associated with them.\n"
	  "\n"
	  "\tProfile information can be saved with 'save'.";


/**
 * Save profiling information for CPU or DSP.
 */
static bool Profile_Save(const char *fname, bool bForDsp)
{
	FILE *out;
	Uint32 freq;
	const char *proc, *core;
	if (!(out = fopen(fname, "w"))) {
		fprintf(stderr, "ERROR: opening '%s' for writing failed!\n", fname);
		perror(NULL);
		return false;
	}
	if (bForDsp) {
		freq = MachineClocks.DSP_Freq;
		proc = "DSP";
	} else {
		freq = MachineClocks.CPU_Freq;
		proc = "CPU";
	}
#if ENABLE_WINUAE_CPU
	core = "WinUAE";
#else
	core = "OldUAE";
#endif
	fprintf(out, "Hatari %s profile (%s, %s CPU core)\n", proc, PROG_NAME, core);
	fprintf(out, "Cycles/second:\t%u\n", freq);
	if (bForDsp) {
		Profile_DspSave(out);
	} else {
		Profile_CpuSave(out);
	}
	fclose(out);
	return true;
}

/**
 * Command: CPU/DSP profiling enabling, exec stats, cycle and call stats.
 * Return for succesful command and false for incorrect ones.
 */
int Profile_Command(int nArgc, char *psArgs[], bool bForDsp)
{
	static int show = 16;
	Uint32 *disasm_addr;
	bool *enabled;

	if (nArgc > 2) {
		show = atoi(psArgs[2]);
	}	
	if (bForDsp) {
		Profile_DspGetPointers(&enabled, &disasm_addr);
	} else {
		Profile_CpuGetPointers(&enabled, &disasm_addr);
	}

	/* continue or explicit addresses command? */
	if (nArgc < 2 || strcmp(psArgs[1], "addresses") == 0) {
		Uint32 lower, upper = 0;
		if (nArgc > 2) {
			if (Eval_Range(psArgs[2], &lower, &upper, false) < 0) {
				return DEBUGGER_CMDDONE;
			}
		} else {
			lower = *disasm_addr;
		}
		if (bForDsp) {
			*disasm_addr = Profile_DspShowAddresses(lower, upper, stdout);
		} else {
			*disasm_addr = Profile_CpuShowAddresses(lower, upper, stdout);
		}
		return DEBUGGER_CMDCONT;

	} else if (strcmp(psArgs[1], "on") == 0) {
		*enabled = true;
		fprintf(stderr, "Profiling enabled.\n");

	} else if (strcmp(psArgs[1], "off") == 0) {
		*enabled = false;
		fprintf(stderr, "Profiling disabled.\n");
	
	} else if (strcmp(psArgs[1], "stats") == 0) {
		if (bForDsp) {
			Profile_DspShowStats();
		} else {
			Profile_CpuShowStats();
		}
	} else if (strcmp(psArgs[1], "misses") == 0) {
		if (bForDsp) {
			fprintf(stderr, "Cache misses are recorded only for CPU, not DSP.\n");
		} else {
			Profile_CpuShowMisses(show);
		}
	} else if (strcmp(psArgs[1], "cycles") == 0) {
		if (bForDsp) {
			Profile_DspShowCycles(show);
		} else {
			Profile_CpuShowCycles(show);
		}
	} else if (strcmp(psArgs[1], "counts") == 0) {
		if (bForDsp) {
			Profile_DspShowCounts(show, false);
		} else {
			Profile_CpuShowCounts(show, false);
		}
	} else if (strcmp(psArgs[1], "symbols") == 0) {
		if (bForDsp) {
			Profile_DspShowCounts(show, true);
		} else {
			Profile_CpuShowCounts(show, true);
		}
	} else if (strcmp(psArgs[1], "callers") == 0) {
		if (bForDsp) {
			Profile_DspShowCallers(stdout);
		} else {
			Profile_CpuShowCallers(stdout);
		}
	} else if (strcmp(psArgs[1], "save") == 0) {
		Profile_Save(psArgs[2], bForDsp);
	} else {
		DebugUI_PrintCmdHelp(psArgs[0]);
	}
	return DEBUGGER_CMDDONE;
}
