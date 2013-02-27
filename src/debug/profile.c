/*
 * Hatari - profile.c
 * 
 * Copyright (C) 2010-2013 by Eero Tamminen
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * profile.c - functions for profiling CPU and DSP and showing the results.
 */
const char Profile_fileid[] = "Hatari profile.c : " __DATE__ " " __TIME__;

#include <stdio.h>
#include "main.h"
#include "debug_priv.h"
#include "debugInfo.h"
#include "dsp.h"
#include "configuration.h"
#include "clocks_timings.h"
#include "m68000.h"
#include "profile.h"
#include "stMemory.h"
#include "symbols.h"
#include "68kDisass.h"
#include "tos.h"
#include "file.h"

/* if non-zero, output (more) warnings on syspicious cycle/instruction counts */
#define DEBUG 0

#define CALL_UNDEFINED	0	/* = call type information not supported */
typedef enum {
	CALL_UNKNOWN	= 1,
	CALL_NEXT	= 2,
	CALL_BRANCH	= 4,
	CALL_SUBROUTINE	= 8,
	CALL_SUBRETURN	= 16,
	CALL_EXCEPTION	= 32,
	CALL_EXCRETURN	= 64,
	CALL_INTERRUPT	= 128
} calltype_t;

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

typedef struct {
	calltype_t   flags:8;	/* what kind of call it was */
	unsigned int addr:24;	/* address for the caller */
	Uint32 count;		/* number of calls */
} caller_t;

typedef struct {
	Uint32 addr;		/* called address */
	unsigned int count;	/* number of callers */
	caller_t *callers;	/* who called this address */
} callee_t;


/* CPU/DSP memory area statistics */
typedef struct {
	Uint64 all_cycles, all_count, all_misses;
	Uint64 max_cycles;	/* for overflow check (cycles > count or misses) */
	Uint32 lowest, highest;	/* active address range within memory area */
	Uint32 active;          /* number of active addresses */
} profile_area_t;


/* This is relevant with WinUAE CPU core:
 * - the default cycle exact variant needs this define to be non-zero
 * - non-cycle exact and MMU variants need this define to be 0
 *   for cycle counts to make any sense
 */
#define USE_CYCLES_COUNTER 1

#define MAX_CPU_PROFILE_VALUE 0xFFFFFFFF

typedef struct {
	Uint32 count;	/* how many times this address is used */
	Uint32 cycles;	/* how many CPU cycles was taken at this address */
	Uint32 misses;  /* how many CPU cache misses happened at this address */
} cpu_profile_item_t;

#define MAX_MISS 4

static struct {
	Uint64 all_cycles, all_count, all_misses;
	Uint32 miss_counts[MAX_MISS];
	cpu_profile_item_t *data; /* profile data items */
	Uint32 size;          /* number of allocated profile data items */
	profile_area_t ram;   /* normal RAM stats */
	profile_area_t rom;   /* cartridge ROM stats */
	profile_area_t tos;   /* ROM TOS stats */
	Uint32 active;        /* number of active data items in all areas */
	unsigned int sites;   /* number of symbol callsites */
	callee_t *callsite;   /* symbol specific caller information */
	Uint32 *sort_arr;     /* data indexes used for sorting */
	Uint32 prev_cycles;   /* previous instruction cycles counter */
	Uint32 prev_pc;       /* previous instruction address */
	bool processed;	      /* true when data is already processed */
	bool enabled;         /* true when profiling enabled */
} cpu_profile;


#define DSP_PROFILE_ARR_SIZE 0x10000
#define MAX_DSP_PROFILE_VALUE 0xFFFFFFFFFFFFFFFFLL

typedef struct {
	Uint64 count;		/* how many times this address is used */
	Uint64 cycles;		/* how many DSP cycles was taken at this address */
	Uint16 min_cycle;
	Uint16 max_cycle;
} dsp_profile_item_t;

static struct {
	dsp_profile_item_t *data; /* profile data */
	profile_area_t ram;   /* normal RAM stats */
	unsigned int sites;   /* number of symbol callsites */
	callee_t *callsite;   /* symbol specific caller information */
	Uint16 *sort_arr;     /* data indexes used for sorting */
	Uint16 prev_pc;       /* previous PC for which the cycles are for */
	bool processed;	      /* true when data is already processed */
	bool enabled;         /* true when profiling enabled */
} dsp_profile;


/* ------------------ CPU profile address mapping ----------------- */

/**
 * convert Atari memory address to sorting array profile data index.
 */
static inline Uint32 address2index(Uint32 pc)
{
	if (unlikely(pc & 1)) {
		fprintf(stderr, "WARNING: odd CPU profile instruction address 0x%x!\n", pc);
	}
	if (pc >= TosAddress && pc < TosAddress + TosSize) {
		/* TOS, put it after RAM data */
		pc = pc - TosAddress + STRamEnd;

	} else if (pc >= 0xFA0000 && pc < 0xFC0000) {
		/* ROM, put it after RAM & TOS data */
		pc = pc - 0xFA0000 + STRamEnd + TosSize;

	} else {
		/* if in RAM, use as-is */
		if (unlikely(pc >= STRamEnd)) {
			fprintf(stderr, "WARNING: 'invalid' CPU PC profile instruction address 0x%x!\n", pc);
			/* extra entry at end is reserved for invalid PC values */
			pc = STRamEnd + TosSize + 0x20000;
		}
	}
	/* CPU instructions are at even addresses, save space by halving */
	return (pc >> 1);
}

/**
 * convert sorting array profile data index to Atari memory address.
 */
static Uint32 index2address(Uint32 idx)
{
	idx <<= 1;
	/* RAM */
	if (idx < STRamEnd) {
		return idx;
	}
	/* TOS */
	idx -= STRamEnd;
	if (idx < TosSize) {
		return idx + TosAddress;
	}
	/* ROM */
	return idx - TosSize + 0xFA0000;
}

/* ------------------ CPU/DSP caller information handling ----------------- */

/*
 * Show collected CPU/DSP callee/caller information.
 *
 * Hint: As caller info list is based on number of loaded symbols,
 * load only text symbols to save memory & make things faster...
 */
static void show_caller_info(FILE *fp, unsigned int sites, callee_t *callsite, bool forDsp)
{
	unsigned int typecount, countissues;
	unsigned int i, j, k;
	const char *name;
	caller_t *info;
	Uint64 total, countdiff;
	Uint32 addr, typeaddr;

	/* legend */
	fputs("# ", fp);
	for (k = 0; k < ARRAYSIZE(flaginfo); k++) {
		fprintf(fp, "%c = %s, ", flaginfo[k].chr, flaginfo[k].info);
	}
	fputs("\n", fp);

	countdiff = countissues = 0;
	for (i = 0; i < sites; i++, callsite++) {
		addr = callsite->addr;
		if (!addr) {
			continue;
		}
		if (forDsp) {
			total = dsp_profile.data[addr].count;
			name = Symbols_GetByDspAddress(addr);
		} else {
			Uint32 idx = address2index(addr);
			total = cpu_profile.data[idx].count;
			name = Symbols_GetByCpuAddress(addr);
		}
		fprintf(fp, "0x%x: ", callsite->addr);

		typeaddr = 0;
		info = callsite->callers;
		/* TODO: sort the information before output? */
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
			fprintf(stderr, "WARNING: callcount mismatches (%lld calls) with address instruction\n\t counts in %d cases, most likely profile start & end.\n",
				(Sint64)countdiff, countissues);
		} else {
			/* profiler bug: some (address?) mismatch in recording instruction counts and call counts */
			fprintf(stderr, "ERROR: callcount mismatches with address instruction counts\n\t(%lld in total) detected in %d cases!\n",
				(Sint64)countdiff, countissues);
		}
	}
}

/* convenience wrappers for caller information showing */
static void Profile_CpuShowCallers(FILE *fp)
{
	show_caller_info(fp, cpu_profile.sites, cpu_profile.callsite, false);
}
static void Profile_DspShowCallers(FILE *fp)
{
	show_caller_info(fp, dsp_profile.sites, dsp_profile.callsite, true);
}

/**
 * Update CPU/DSP callee / caller information, if called address contains
 * symbol address (= function, or other interesting place in code)
 */
static void update_caller_info(bool forDsp, Uint32 pc, int sites, callee_t *callsite, Uint32 caller, calltype_t flag)
{
	int i, idx, count;
	caller_t *info;

	if (forDsp) {
		idx = Symbols_GetDspAddressIndex(pc);
	} else {
		idx = Symbols_GetCpuAddressIndex(pc);
	}
	if (idx < 0) {
		return;
	}
	if (idx >= sites) {
		fprintf(stderr, "ERROR: number of symbols grew during profiling (%d -> %d)!\n", sites, idx);
		return;
	}
	callsite += idx;

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

static unsigned int alloc_caller_info(const char *info, unsigned int oldcount, unsigned int count, callee_t **callsite)
{
	if (*callsite) {
		/* free old data */
		unsigned int i;
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

/* ------------------ CPU profile results ----------------- */

/**
 * Get CPU cycles, count and count percentage for given address.
 * Return true if data was available and non-zero, false otherwise.
 */
bool Profile_CpuAddressData(Uint32 addr, float *percentage, Uint32 *count, Uint32 *cycles, Uint32 *misses)
{
	Uint32 idx;
	if (!cpu_profile.data) {
		return false;
	}
	idx = address2index(addr);
	*misses = cpu_profile.data[idx].misses;
	*cycles = cpu_profile.data[idx].cycles;
	*count = cpu_profile.data[idx].count;
	if (cpu_profile.all_count) {
		*percentage = 100.0*(*count)/cpu_profile.all_count;
	} else {
		*percentage = 0.0;
	}
	return (*count > 0);
}

/**
 * Helper to show statistics for specified CPU profile area.
 */
static void show_cpu_area_stats(profile_area_t *area)
{
	if (!area->active) {
		fprintf(stderr, "- no activity\n");
		return;
	}
	fprintf(stderr, "- active address range:\n  0x%06x-0x%06x\n",
		index2address(area->lowest),
		index2address(area->highest));
	fprintf(stderr, "- active instruction addresses:\n  %d (%.2f%% of all)\n",
		area->active,
		100.0 * area->active / cpu_profile.active);
	fprintf(stderr, "- executed instructions:\n  %llu (%.2f%% of all)\n",
		area->all_count,
		100.0 * area->all_count / cpu_profile.all_count);
#if ENABLE_WINUAE_CPU
	if (cpu_profile.all_misses) {	/* CPU cache in use? */
		fprintf(stderr, "- instruction cache misses:\n  %llu (%.2f%% of all)\n",
			area->all_misses,
			100.0 * area->all_misses / cpu_profile.all_misses);
	}
#endif
	fprintf(stderr, "- used cycles:\n  %llu (%.2f%% of all)\n  = %.5fs\n",
		area->all_cycles,
		100.0 * area->all_cycles / cpu_profile.all_cycles,
		(double)area->all_cycles / MachineClocks.CPU_Freq);
	if (area->max_cycles == MAX_CPU_PROFILE_VALUE) {
		fprintf(stderr, "  *** COUNTER OVERFLOW! ***\n");
	}
}


/**
 * show CPU area (RAM, ROM, TOS) specific statistics.
 */
static void Profile_CpuShowStats(void)
{
	fprintf(stderr, "Normal RAM (0-0x%X):\n", STRamEnd);
	show_cpu_area_stats(&cpu_profile.ram);

	fprintf(stderr, "ROM TOS (0x%X-0x%X):\n", TosAddress, TosAddress + TosSize);
	show_cpu_area_stats(&cpu_profile.tos);

	fprintf(stderr, "Cartridge ROM (0xFA0000-0xFC0000):\n");
	show_cpu_area_stats(&cpu_profile.rom);

	fprintf(stderr, "\n= %.5fs\n",
		(double)cpu_profile.all_cycles / MachineClocks.CPU_Freq);

#if ENABLE_WINUAE_CPU
	if (cpu_profile.all_misses) {	/* CPU cache in use? */
		int i;
		fprintf(stderr, "\nCache misses per instruction, number of occurrences:\n");
		for (i = 0; i < MAX_MISS; i++) {
			fprintf(stderr, "- %d: %d\n", i, cpu_profile.miss_counts[i]);
		}
	}
#endif
}

/**
 * Show first 'show' CPU instructions which execution was profiled,
 * in the address order.
 */
static void Profile_CpuShowAddresses(unsigned int show, FILE *out)
{
	int oldcols[DISASM_COLUMNS], newcols[DISASM_COLUMNS];
	unsigned int shown, idx;
	const char *symbol;
	cpu_profile_item_t *data;
	uaecptr nextpc, addr;
	Uint32 size, active;

	data = cpu_profile.data;
	if (!data) {
		fprintf(stderr, "ERROR: no CPU profiling data available!\n");
		return;
	}

	size = cpu_profile.size;
	active = cpu_profile.active;
	if (!show || show > active) {
		show = active;
	}

	/* get/change columns */
	Disasm_GetColumns(oldcols);
	Disasm_DisableColumn(DISASM_COLUMN_HEXDUMP, oldcols, newcols);
	Disasm_SetColumns(newcols);

	fputs("# disassembly with profile data: <instructions percentage>% (<sum of instructions>, <sum of cycles>, <sum of i-cache misses)\n", out);

	nextpc = 0;
	for (shown = idx = 0; shown < show && idx < size; idx++) {
		if (!data[idx].count) {
			continue;
		}
		addr = index2address(idx);
		if (addr != nextpc && nextpc) {
			fprintf(out, "[...]\n");
		}
		symbol = Symbols_GetByCpuAddress(addr);
		if (symbol) {
			fprintf(out, "%s:\n", symbol);
		}
		/* NOTE: column setup works only with UAE disasm engine! */
		Disasm(out, addr, &nextpc, 1);
		shown++;
	}
	printf("Disassembled %d (of active %d) CPU addresses.\n", show, active);

	/* restore disassembly columns */
	Disasm_SetColumns(oldcols);
}


/**
 * compare function for qsort() to sort CPU profile data by instruction cache misses.
 */
static int profile_by_cpu_misses(const void *p1, const void *p2)
{
	Uint32 count1 = cpu_profile.data[*(const Uint32*)p1].misses;
	Uint32 count2 = cpu_profile.data[*(const Uint32*)p2].misses;
	if (count1 > count2) {
		return -1;
	}
	if (count1 < count2) {
		return 1;
	}
	return 0;
}

/**
 * Sort CPU profile data addresses by instruction cache misses and show the results.
 */
static void Profile_CpuShowMisses(unsigned int show)
{
	unsigned int active;
	Uint32 *sort_arr, *end, addr;
	cpu_profile_item_t *data = cpu_profile.data;
	float percentage;
	Uint32 count;

	if (!data) {
		fprintf(stderr, "ERROR: no CPU profiling data available!\n");
		return;
	}

	active = cpu_profile.active;
	sort_arr = cpu_profile.sort_arr;
	qsort(sort_arr, active, sizeof(*sort_arr), profile_by_cpu_misses);

	printf("addr:\t\tmisses:\n");
	show = (show < active ? show : active);
	for (end = sort_arr + show; sort_arr < end; sort_arr++) {
		addr = index2address(*sort_arr);
		count = data[*sort_arr].misses;
		percentage = 100.0*count/cpu_profile.all_misses;
		printf("0x%06x\t%.2f%%\t%d%s\n", addr, percentage, count,
		       count == MAX_CPU_PROFILE_VALUE ? " (OVERFLOW)" : "");
	}
	printf("%d CPU addresses listed.\n", show);
}

/**
 * compare function for qsort() to sort CPU profile data by cycles counts.
 */
static int profile_by_cpu_cycles(const void *p1, const void *p2)
{
	Uint32 count1 = cpu_profile.data[*(const Uint32*)p1].cycles;
	Uint32 count2 = cpu_profile.data[*(const Uint32*)p2].cycles;
	if (count1 > count2) {
		return -1;
	}
	if (count1 < count2) {
		return 1;
	}
	return 0;
}

/**
 * Sort CPU profile data addresses by cycle counts and show the results.
 */
static void Profile_CpuShowCycles(unsigned int show)
{
	unsigned int active;
	Uint32 *sort_arr, *end, addr;
	cpu_profile_item_t *data = cpu_profile.data;
	float percentage;
	Uint32 count;

	if (!data) {
		fprintf(stderr, "ERROR: no CPU profiling data available!\n");
		return;
	}

	active = cpu_profile.active;
	sort_arr = cpu_profile.sort_arr;
	qsort(sort_arr, active, sizeof(*sort_arr), profile_by_cpu_cycles);

	printf("addr:\t\tcycles:\n");
	show = (show < active ? show : active);
	for (end = sort_arr + show; sort_arr < end; sort_arr++) {
		addr = index2address(*sort_arr);
		count = data[*sort_arr].cycles;
		percentage = 100.0*count/cpu_profile.all_cycles;
		printf("0x%06x\t%.2f%%\t%d%s\n", addr, percentage, count,
		       count == MAX_CPU_PROFILE_VALUE ? " (OVERFLOW)" : "");
	}
	printf("%d CPU addresses listed.\n", show);
}

/**
 * compare function for qsort() to sort CPU profile data by descending
 * address access counts.
 */
static int profile_by_cpu_count(const void *p1, const void *p2)
{
	Uint32 count1 = cpu_profile.data[*(const Uint32*)p1].count;
	Uint32 count2 = cpu_profile.data[*(const Uint32*)p2].count;
	if (count1 > count2) {
		return -1;
	}
	if (count1 < count2) {
		return 1;
	}
	return 0;
}

/**
 * Sort CPU profile data addresses by call counts and show the results.
 * If symbols are requested and symbols are loaded, show (only) addresses
 * matching a symbol.
 */
static void Profile_CpuShowCounts(unsigned int show, bool only_symbols)
{
	cpu_profile_item_t *data = cpu_profile.data;
	unsigned int symbols, matched, active;
	Uint32 *sort_arr, *end, addr;
	const char *name;
	float percentage;
	Uint32 count;

	if (!data) {
		fprintf(stderr, "ERROR: no CPU profiling data available!\n");
		return;
	}
	active = cpu_profile.active;
	show = (show < active ? show : active);

	sort_arr = cpu_profile.sort_arr;
	qsort(sort_arr, active, sizeof(*sort_arr), profile_by_cpu_count);

	if (!only_symbols) {
		printf("addr:\t\tcount:\n");
		for (end = sort_arr + show; sort_arr < end; sort_arr++) {
			addr = index2address(*sort_arr);
			count = data[*sort_arr].count;
			percentage = 100.0*count/cpu_profile.all_count;
			printf("0x%06x\t%.2f%%\t%d%s\n",
			       addr, percentage, count,
			       count == MAX_CPU_PROFILE_VALUE ? " (OVERFLOW)" : "");
		}
		printf("%d CPU addresses listed.\n", show);
		return;
	}

	symbols = Symbols_CpuCount();
	if (!symbols) {
		fprintf(stderr, "ERROR: no CPU symbols loaded!\n");
		return;
	}
	matched = 0;	

	printf("addr:\t\tcount:\t\tsymbol:\n");
	for (end = sort_arr + active; sort_arr < end; sort_arr++) {

		addr = index2address(*sort_arr);
		name = Symbols_GetByCpuAddress(addr);
		if (!name) {
			continue;
		}
		count = data[*sort_arr].count;
		percentage = 100.0*count/cpu_profile.all_count;
		printf("0x%06x\t%.2f%%\t%d\t%s%s\n",
		       addr, percentage, count, name,
		       count == MAX_CPU_PROFILE_VALUE ? " (OVERFLOW)" : "");

		matched++;
		if (matched >= show || matched >= symbols) {
			break;
		}
	}
	printf("%d CPU symbols listed.\n", matched);
}


/* ------------------ CPU profile control ----------------- */

/**
 * Initialize CPU profiling when necessary.  Return true if profiling.
 */
bool Profile_CpuStart(void)
{
	if (cpu_profile.sort_arr) {
		/* remove previous results */
		free(cpu_profile.sort_arr);
		free(cpu_profile.data);
		cpu_profile.sort_arr = NULL;
		cpu_profile.data = NULL;
		printf("Freed previous CPU profile buffers.\n");
	}
	if (!cpu_profile.enabled) {
		return false;
	}
	/* Shouldn't change within same debug session */
	cpu_profile.size = (STRamEnd + 0x20000 + TosSize) / 2;

	/* Add one entry for catching invalid PC values */
	cpu_profile.data = calloc(cpu_profile.size+1, sizeof(*cpu_profile.data));
	if (cpu_profile.data) {
		printf("Allocated CPU profile buffer (%d MB).\n",
		       (int)sizeof(*cpu_profile.data)*cpu_profile.size/(1024*1024));
	} else {
		perror("ERROR, new CPU profile buffer alloc failed");
		cpu_profile.enabled = false;
	}
	cpu_profile.sites = alloc_caller_info("CPU", cpu_profile.sites, Symbols_CpuCount(), &(cpu_profile.callsite));

	memset(cpu_profile.miss_counts, 0, sizeof(cpu_profile.miss_counts));
	cpu_profile.prev_cycles = Cycles_GetCounter(CYCLES_COUNTER_CPU);
	cpu_profile.prev_pc = M68000_GetPC();

	cpu_profile.processed = false;
	return cpu_profile.enabled;
}

static calltype_t cpu_opcode_type(Uint16 opcode, Uint32 prev_pc, Uint32 pc)
{
	/* JSR, BSR (should be most common) */
	if ((opcode & 0xffc0) == 0x4e80 ||
	    (opcode & 0xff00) == 0x6100) {
		return CALL_SUBROUTINE;
	}
	/* RTS, RTR, RTD */
	if (opcode == 0x4e75 ||
	    opcode == 0x4e77 ||
	    opcode == 0x4e74) {
		return CALL_SUBRETURN;
	}
	/* TRAP, TRAPV, STOP, ILLEGAL, CHK, BKPT */
	if ((opcode & 0xfff0) == 0x4e40 ||
	     opcode == 0x4e76 ||
	     opcode == 0x4afc ||
	     opcode == 0x4e72 ||
	    (opcode & 0xf1c0) == 0x4180 ||
	    (opcode & 0xfff8) == 0x4848) {
		return CALL_EXCEPTION;
	}
	/* RTE */
	if (opcode == 0x4e73) {
		return CALL_EXCRETURN;
	}
	/* JMP (potentially static functions), DBCC, BRA, BCC (loop labels) */
	if ((opcode & 0xffc0) == 0x4ec0 ||
	    (opcode & 0xf0f8) == 0x50c8 ||
	    (opcode & 0xff00) == 0x6000 ||
	   ((opcode & 0xf000) == 0x6000 && (opcode & 0xf00) > 0x100)) {
		return CALL_BRANCH;
	}
	/* just moved to next instruction? */
	if (prev_pc < pc && (pc - prev_pc) <= 10) {
		return CALL_NEXT;
	}
	return CALL_UNKNOWN;
}


/**
 * Update CPU cycle and count statistics for PC address.
 *
 * This gets called after instruction has executed and PC
 * has advanced to next instruction.
 */
void Profile_CpuUpdate(void)
{
#if ENABLE_WINUAE_CPU
	Uint32 misses;
#endif
	Uint32 pc, prev_pc, idx, cycles;
	cpu_profile_item_t *prev;
	Uint16 opcode;

	prev_pc = cpu_profile.prev_pc;
	cpu_profile.prev_pc = pc = M68000_GetPC();
	opcode = STMemory_ReadWord(prev_pc);
	if (cpu_profile.sites) {
		calltype_t flag = cpu_opcode_type(opcode, prev_pc, pc);
		update_caller_info(false, pc, cpu_profile.sites, cpu_profile.callsite, prev_pc, flag);
	}

	idx = address2index(prev_pc);
	assert(idx <= cpu_profile.size);
	prev = cpu_profile.data + idx;

	if (likely(prev->count < MAX_CPU_PROFILE_VALUE)) {
		prev->count++;
	}

#if USE_CYCLES_COUNTER
	/* Confusingly, with DSP enabled, cycle counter is for this instruction,
	 * without DSP enabled, it's a monotonically increasing counter.
	 */
	if (bDspEnabled) {
		cycles = Cycles_GetCounter(CYCLES_COUNTER_CPU);
	} else {
		Uint32 newcycles = Cycles_GetCounter(CYCLES_COUNTER_CPU);
		cycles = newcycles - cpu_profile.prev_cycles;
		cpu_profile.prev_cycles = newcycles;
	}
#else
	cycles = CurrentInstrCycles + nWaitStateCycles;
#endif
	/* catch too large (and negative) cycles, ignore STOP instruction */
	if (unlikely(cycles > 256 && opcode != 0x4e72)) {
		fprintf(stderr, "WARNING: cycles %d > 512 at 0x%x\n",
			cycles, prev_pc);
	}
#if DEBUG
	if (unlikely(cycles == 0)) {
		Uint32 nextpc;
		Disasm(debugOutput, prev_pc, &nextpc, 1);
	}
#endif
	if (likely(prev->cycles < MAX_CPU_PROFILE_VALUE - cycles)) {
		prev->cycles += cycles;
	} else {
		prev->cycles = MAX_CPU_PROFILE_VALUE;
	}

#if ENABLE_WINUAE_CPU
	misses = CpuInstruction.iCacheMisses;
	assert(misses < MAX_MISS);
	cpu_profile.miss_counts[misses]++;
	if (likely(prev->misses < MAX_CPU_PROFILE_VALUE - misses)) {
		prev->misses += misses;
	} else {
		prev->misses = MAX_CPU_PROFILE_VALUE;
	}
#endif
}


/**
 * Helper for collecting CPU profile area statistics.
 */
static void update_cpu_area(Uint32 addr, cpu_profile_item_t *item, profile_area_t *area)
{
	Uint32 cycles = item->cycles;
	Uint32 count = item->count;

	if (!count) {
		return;
	}
	area->all_count += count;
	area->all_misses += item->misses;
	area->all_cycles += cycles;

	if (cycles > area->max_cycles) {
		area->max_cycles = cycles;
	}

	if (addr < area->lowest) {
		area->lowest = addr;
	}
	area->highest = addr;

	area->active++;
}


/**
 * Stop and process the CPU profiling data; collect stats and
 * prepare for more optimal sorting.
 */
void Profile_CpuStop(void)
{
	cpu_profile_item_t *item;
	profile_area_t *area;
	Uint32 *sort_arr;
	Uint32 i, active;

	if (cpu_profile.processed || !cpu_profile.enabled) {
		return;
	}
	/* user didn't change RAM or TOS size in the meanwhile? */
	assert(cpu_profile.size == (STRamEnd + 0x20000 + TosSize) / 2);

	/* find lowest and highest addresses executed... */
	item = cpu_profile.data;

	/* ...for normal RAM */
	area = &cpu_profile.ram;
	memset(area, 0, sizeof(profile_area_t));
	area->lowest = cpu_profile.size;

	for (i = 0; i < STRamEnd/2; i++, item++) {
		update_cpu_area(i, item, area);
	}

	/* ...for ROM TOS */
	area = &cpu_profile.tos;
	memset(area, 0, sizeof(profile_area_t));
	area->lowest = cpu_profile.size;

	for (; i < (STRamEnd + TosSize)/2; i++, item++) {
		update_cpu_area(i, item, area);
	}

	/* ... for Cartridge ROM */
	area = &cpu_profile.rom;
	memset(area, 0, sizeof(profile_area_t));
	area->lowest = cpu_profile.size;

	for (; i < cpu_profile.size; i++, item++) {
		update_cpu_area(i, item, area);
	}

	cpu_profile.all_misses = cpu_profile.ram.all_misses + cpu_profile.rom.all_misses + cpu_profile.tos.all_misses;
	cpu_profile.all_cycles = cpu_profile.ram.all_cycles + cpu_profile.rom.all_cycles + cpu_profile.tos.all_cycles;
	cpu_profile.all_count = cpu_profile.ram.all_count + cpu_profile.rom.all_count + cpu_profile.tos.all_count;

	/* allocate address array for sorting */
	active = cpu_profile.ram.active + cpu_profile.rom.active + cpu_profile.tos.active;
	sort_arr = calloc(active, sizeof(*sort_arr));

	if (!sort_arr) {
		perror("ERROR: allocating CPU profile address data");
		free(cpu_profile.data);
		cpu_profile.data = NULL;
		return;
	}
	printf("Allocated CPU profile address buffer (%d KB).\n",
	       (int)sizeof(*sort_arr)*(active+512)/1024);
	cpu_profile.sort_arr = sort_arr;
	cpu_profile.active = active;

	/* and fill addresses for used instructions... */
	
	/* ...for normal RAM */
	area = &cpu_profile.ram;
	item = cpu_profile.data + area->lowest;
	for (i = area->lowest; i <= area->highest; i++, item++) {
		if (item->count) {
			*sort_arr++ = i;
		}
	}

	/* ...for TOS ROM */
	area = &cpu_profile.tos;
	item = cpu_profile.data + area->lowest;
	for (i = area->lowest; i <= area->highest; i++, item++) {
		if (item->count) {
			*sort_arr++ = i;
		}
	}

	/* ...for Cartridge ROM */
	area = &cpu_profile.rom;
	item = cpu_profile.data + area->lowest;
	for (i = area->lowest; i <= area->highest; i++, item++) {
		if (item->count) {
			*sort_arr++ = i;
		}
	}
	//printf("%d/%d/%d\n", area->active, sort_arr-cpu_profile.sort_arr, active);

	Profile_CpuShowStats();
	cpu_profile.processed = true;
}


/* ------------------ DSP profile results ----------------- */

/**
 * Get DSP cycles, count and count percentage for given address.
 * Return true if data was available and non-zero, false otherwise.
 */
bool Profile_DspAddressData(Uint16 addr, float *percentage, Uint64 *count, Uint64 *cycles, Uint16 *cycle_diff)
{
	dsp_profile_item_t *item;
	if (!dsp_profile.data) {
		return false;
	}
	item = dsp_profile.data + addr;

	*cycles = item->cycles;
	*count = item->count;
	if (item->max_cycle) {
		*cycle_diff = item->max_cycle - item->min_cycle;
	} else {
		*cycle_diff = 0;
	}
	if (dsp_profile.ram.all_count) {
		*percentage = 100.0*(*count)/dsp_profile.ram.all_count;
	} else {
		*percentage = 0.0;
	}
	return (*count > 0);
}

/**
 * show DSP specific profile statistics.
 */
static void Profile_DspShowStats(void)
{
	profile_area_t *area = &dsp_profile.ram;
	fprintf(stderr, "DSP profile statistics (0x0-0xFFFF):\n");
	if (!area->active) {
		fprintf(stderr, "- no activity\n");
		return;
	}
	fprintf(stderr, "- active address range:\n  0x%04x-0x%04x\n",
		area->lowest, area->highest);
	fprintf(stderr, "- active instruction addresses:\n  %d\n",
		area->active);
	fprintf(stderr, "- executed instructions:\n  %llu\n",
		area->all_count);
	/* indicates either instruction(s) that address different memory areas
	 * (they can have different access costs), or more significantly,
	 * DSP code that has changed during profiling.
	 */
	fprintf(stderr, "- sum of per instruction cycle changes\n  (can indicate code change during profiling):\n  %llu\n",
		area->all_misses);

	fprintf(stderr, "- used cycles:\n  %llu\n",
		area->all_cycles);
	if (area->max_cycles == MAX_DSP_PROFILE_VALUE) {
		fprintf(stderr, "  *** COUNTERS OVERFLOW! ***\n");
	}
	fprintf(stderr, "\n= %.5fs\n", (double)(area->all_cycles) / MachineClocks.DSP_Freq);
}

/**
 * Show first 'show' DSP instructions which execution was profiled,
 * in the address order.
 */
static void Profile_DspShowAddresses(unsigned int show, FILE *out)
{
	unsigned int shown;
	dsp_profile_item_t *data;
	Uint16 addr, nextpc;
	Uint32 size, active;
	const char *symbol;

	data = dsp_profile.data;
	if (!data) {
		fprintf(stderr, "ERROR: no DSP profiling data available!\n");
		return;
	}

	size = DSP_PROFILE_ARR_SIZE;
	active = dsp_profile.ram.active;
	if (!show || show > active) {
		show = active;
	}

	fputs("# disassembly with profile data: <instructions percentage>% (<sum of instructions>, <sum of cycles>, <max cycle difference>)\n", out);

	nextpc = 0;
	for (shown = addr = 0; shown < show && addr < size; addr++) {
		if (!data[addr].count) {
			continue;
		}
		if (addr != nextpc && nextpc) {
			fputs("[...]\n", out);
		}
		symbol = Symbols_GetByDspAddress(addr);
		if (symbol) {
			fprintf(out, "%s:\n", symbol);
		}
		nextpc = DSP_DisasmAddress(out, addr, addr);
		shown++;
	}
	printf("Disassembled %d (of active %d) DSP addresses.\n", show, active);
}

/**
 * compare function for qsort() to sort DSP profile data by descdending
 * address cycles counts.
 */
static int profile_by_dsp_cycles(const void *p1, const void *p2)
{
	Uint64 count1 = dsp_profile.data[*(const Uint16*)p1].cycles;
	Uint64 count2 = dsp_profile.data[*(const Uint16*)p2].cycles;
	if (count1 > count2) {
		return -1;
	}
	if (count1 < count2) {
		return 1;
	}
	return 0;
}

/**
 * Sort DSP profile data addresses by cycle counts and show the results.
 */
static void Profile_DspShowCycles(unsigned int show)
{
	unsigned int active;
	Uint16 *sort_arr, *end, addr;
	dsp_profile_item_t *data = dsp_profile.data;
	float percentage;
	Uint64 count;

	if (!data) {
		fprintf(stderr, "ERROR: no DSP profiling data available!\n");
		return;
	}

	active = dsp_profile.ram.active;
	sort_arr = dsp_profile.sort_arr;
	qsort(sort_arr, active, sizeof(*sort_arr), profile_by_dsp_cycles);

	printf("addr:\tcycles:\n");
	show = (show < active ? show : active);
	for (end = sort_arr + show; sort_arr < end; sort_arr++) {
		addr = *sort_arr;
		count = data[addr].cycles;
		percentage = 100.0*count/dsp_profile.ram.all_cycles;
		printf("0x%04x\t%.2f%%\t%lld%s\n", addr, percentage, count,
		       count == MAX_DSP_PROFILE_VALUE ? " (OVERFLOW)" : "");
	}
	printf("%d DSP addresses listed.\n", show);
}


/**
 * compare function for qsort() to sort DSP profile data by descdending
 * address access counts.
 */
static int profile_by_dsp_count(const void *p1, const void *p2)
{
	Uint64 count1 = dsp_profile.data[*(const Uint16*)p1].count;
	Uint64 count2 = dsp_profile.data[*(const Uint16*)p2].count;
	if (count1 > count2) {
		return -1;
	}
	if (count1 < count2) {
		return 1;
	}
	return 0;
}

/**
 * Sort DSP profile data addresses by call counts and show the results.
 * If symbols are requested and symbols are loaded, show (only) addresses
 * matching a symbol.
 */
static void Profile_DspShowCounts(unsigned int show, bool only_symbols)
{
	dsp_profile_item_t *data = dsp_profile.data;
	unsigned int symbols, matched, active;
	Uint16 *sort_arr, *end, addr;
	const char *name;
	float percentage;
	Uint64 count;

	if (!data) {
		fprintf(stderr, "ERROR: no DSP profiling data available!\n");
		return;
	}
	active = dsp_profile.ram.active;
	show = (show < active ? show : active);

	sort_arr = dsp_profile.sort_arr;
	qsort(sort_arr, active, sizeof(*sort_arr), profile_by_dsp_count);

	if (!only_symbols) {
		printf("addr:\tcount:\n");
		for (end = sort_arr + show; sort_arr < end; sort_arr++) {
			addr = *sort_arr;
			count = data[addr].count;
			percentage = 100.0*count/dsp_profile.ram.all_count;
			printf("0x%04x\t%.2f%%\t%lld%s\n",
			       addr, percentage, count,
			       count == MAX_DSP_PROFILE_VALUE ? " (OVERFLOW)" : "");
		}
		printf("%d DSP addresses listed.\n", show);
		return;
	}

	symbols = Symbols_DspCount();
	if (!symbols) {
		fprintf(stderr, "ERROR: no DSP symbols loaded!\n");
		return;
	}
	matched = 0;	

	printf("addr:\tcount:\t\tsymbol:\n");
	for (end = sort_arr + active; sort_arr < end; sort_arr++) {

		addr = *sort_arr;
		name = Symbols_GetByDspAddress(addr);
		if (!name) {
			continue;
		}
		count = data[addr].count;
		percentage = 100.0*count/dsp_profile.ram.all_count;
		printf("0x%04x\t%.2f%%\t%lld\t%s%s\n",
		       addr, percentage, count, name,
		       count == MAX_DSP_PROFILE_VALUE ? " (OVERFLOW)" : "");

		matched++;
		if (matched >= show || matched >= symbols) {
			break;
		}
	}
	printf("%d DSP symbols listed.\n", matched);
}


/* ------------------ DSP profile control ----------------- */

/**
 * Initialize DSP profiling when necessary.  Return true if profiling.
 */
bool Profile_DspStart(void)
{
	dsp_profile_item_t *item;
	unsigned int i;

	if (dsp_profile.sort_arr) {
		/* remove previous results */
		free(dsp_profile.sort_arr);
		free(dsp_profile.data);
		dsp_profile.sort_arr = NULL;
		dsp_profile.data = NULL;
		printf("Freed previous DSP profile buffers.\n");
	}
	if (!dsp_profile.enabled) {
		return false;
	}

	dsp_profile.data = calloc(DSP_PROFILE_ARR_SIZE, sizeof(*dsp_profile.data));
	if (dsp_profile.data) {
		printf("Allocated DSP profile buffer (%d KB).\n",
		       (int)sizeof(*dsp_profile.data)*DSP_PROFILE_ARR_SIZE/1024);
	} else {
		perror("ERROR, new DSP profile buffer alloc failed");
		dsp_profile.enabled = false;
	}
	item = dsp_profile.data;
	for (i = 0; i < DSP_PROFILE_ARR_SIZE; i++, item++) {
		item->min_cycle = 0xFFFF;
	}

	dsp_profile.sites = alloc_caller_info("DSP", dsp_profile.sites, Symbols_DspCount(), &(dsp_profile.callsite));

	dsp_profile.prev_pc = DSP_GetPC();

	dsp_profile.processed = false;
	return dsp_profile.enabled;
}

/**
 * Update DSP cycle and count statistics for PC address.
 *
 * This is called after instruction is executed and PC points
 * to next instruction i.e. info is for previous PC address.
 */
void Profile_DspUpdate(void)
{
	dsp_profile_item_t *prev;
	Uint16 pc, prev_pc, cycles;

	prev_pc = dsp_profile.prev_pc;
	dsp_profile.prev_pc = pc = DSP_GetPC();
	if (dsp_profile.sites) {
		update_caller_info(true, pc, dsp_profile.sites, dsp_profile.callsite, prev_pc, CALL_UNDEFINED);
	}
	prev = dsp_profile.data + prev_pc;
	if (likely(prev->count < MAX_DSP_PROFILE_VALUE)) {
		prev->count++;
	}

	cycles = DSP_GetInstrCycles();
	if (likely(prev->cycles < MAX_DSP_PROFILE_VALUE - cycles)) {
		prev->cycles += cycles;
	} else {
		prev->cycles = MAX_DSP_PROFILE_VALUE;
	}
	if (unlikely(cycles < prev->min_cycle)) {
		prev->min_cycle = cycles;
	}
	if (unlikely(cycles > prev->max_cycle)) {
		prev->max_cycle = cycles;
	}
}

/**
 * Helper for collecting DSP profile area statistics.
 */
static void update_dsp_area(Uint16 addr, dsp_profile_item_t *item, profile_area_t *area)
{
	Uint64 cycles = item->cycles;
	Uint64 count = item->count;
	Uint16 diff;

	if (!count) {
		return;
	}
	area->all_count += count;
	area->all_cycles += cycles;
	if (cycles > area->max_cycles) {
		area->max_cycles = cycles;
	}
	if (item->max_cycle) {
		diff = item->max_cycle - item->min_cycle;
	} else {
		diff = 0;
	}
	area->all_misses += diff;

	if (addr < area->lowest) {
		area->lowest = addr;
	}
	area->highest = addr;

	area->active++;
}

/**
 * Stop and process the DSP profiling data; collect stats and
 * prepare for more optimal sorting.
 */
void Profile_DspStop(void)
{
	dsp_profile_item_t *item;
	profile_area_t *area;
	Uint16 *sort_arr;
	Uint32 i;

	if (dsp_profile.processed || !dsp_profile.enabled) {
		return;
	}
	/* find lowest and highest  addresses executed */
	item = dsp_profile.data;
	area = &dsp_profile.ram;
	memset(area, 0, sizeof(profile_area_t));
	area->lowest = DSP_PROFILE_ARR_SIZE;

	for (i = 0; i < DSP_PROFILE_ARR_SIZE; i++, item++) {
		update_dsp_area(i, item, area);
	}

	/* allocate address array for sorting */
	sort_arr = calloc(dsp_profile.ram.active, sizeof(*sort_arr));

	if (!sort_arr) {
		perror("ERROR: allocating DSP profile address data");
		free(dsp_profile.data);
		dsp_profile.data = NULL;
		return;
	}
	printf("Allocated DSP profile address buffer (%d KB).\n",
	       (int)sizeof(*sort_arr)*(dsp_profile.ram.active+512)/1024);
	dsp_profile.sort_arr = sort_arr;

	/* ...and fill addresses for used instructions... */
	area = &dsp_profile.ram;
	item = dsp_profile.data + area->lowest;
	for (i = area->lowest; i <= area->highest; i++, item++) {
		if (item->count) {
			*sort_arr++ = i;
		}
	}
	//printf("%d/%d/%d\n", area->active, sort_arr-dsp_profile.sort_arr, active);

	Profile_DspShowStats();
	dsp_profile.processed = true;
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
		"addresses", "callers", "counts", "cycles", "misses", "off", "on", "stats", "symbols"
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
	  "<on|off|stats|counts|cycles|misses|symbols|callers|addresses> [show count] [file]\n"
	  "\t'on' & 'off' enable and disable profiling.  Data is collected\n"
	  "\tuntil debugger is entered again at which point you get profiling\n"
	  "\tstatistics ('stats') summary.\n"
	  "\n"
	  "\tThen you can ask for list of the PC addresses, sorted either by\n"
	  "\texecution 'counts', used 'cycles' or cache 'misses'. First can\n"
	  "\tbe limited just to named addresses with 'symbols'.\n"
	  "\n"
	  "\t'addresses' lists the profiled addresses in order, with the\n"
	  "\tinstructions (currently) residing at them.  'callers' shows\n"
	  "\t(raw) caller information for addresses which had symbol(s)\n"
	  "\tassociated with them.\n"
	  "\n"
	  "\tOptional count will limit how many items will be shown.\n"
	  "\tFor 'addresses' you can also give optional output file name.";


/**
 * Command: CPU/DSP profiling enabling, exec stats, cycle and call stats.
 * Return for succesful command and false for incorrect ones.
 */
bool Profile_Command(int nArgc, char *psArgs[], bool bForDsp)
{
	static int show = 16;
	bool *enabled;
	
	if (nArgc < 2) {
		DebugUI_PrintCmdHelp(psArgs[0]);
		return true;
	}
	if (nArgc > 2) {
		show = atoi(psArgs[2]);
	}
	
	if (bForDsp) {
		enabled = &dsp_profile.enabled;
	} else {
		enabled = &cpu_profile.enabled;
	}
	if (strcmp(psArgs[1], "on") == 0) {
		*enabled = true;
		fprintf(stderr, "Profiling enabled.\n");
		return true;
	}
	if (strcmp(psArgs[1], "off") == 0) {
		*enabled = false;
		fprintf(stderr, "Profiling disabled.\n");
		return true;
	}
	
	if (strcmp(psArgs[1], "stats") == 0) {
		if (bForDsp) {
			Profile_DspShowStats();
		} else {
			Profile_CpuShowStats();
		}
	} else if (strcmp(psArgs[1], "misses") == 0) {
		if (bForDsp) {
			fprintf(stderr, "Cache misses are recorded only for CPU, not DSP.\n");
			return false;
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
	} else if (strcmp(psArgs[1], "addresses") == 0) {
		FILE *out;
		if (nArgc > 3) {
			Uint32 freq;
			const char *proc;
			if (!(out = fopen(psArgs[3], "w"))) {
				fprintf(stderr, "ERROR: opening '%s' for writing failed!\n", psArgs[3]);
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
			fprintf(out, "Hatari %s profile\n", proc);
			fprintf(out, "Cycles/second:\t%u\n", freq);
		} else {
			out = stdout;
		}
		if (bForDsp) {
			if (out == stdout) {
				Profile_DspShowAddresses(show, out);
			} else {
				Profile_DspShowAddresses(show, out);
				Profile_DspShowCallers(out);
			}
		} else {
			if (out == stdout) {
				Profile_CpuShowAddresses(show, out);
			} else {
				Uint32 text;
				/* some information for interpreting the addresses */
				fprintf(out, "ROM_TOS:\t0x%06x-0x%06x\n", TosAddress, TosAddress + TosSize);
				text = DebugInfo_GetTEXT();
				if (text < TosAddress) {
					fprintf(out, "PROGRAM_TEXT:\t0x%06x-0x%06x\n", text, DebugInfo_GetTEXTEnd());
				}
				fprintf(out, "CARTRIDGE:\t0xfa0000-0xfc0000\n");
				Profile_CpuShowAddresses(show, out);
				Profile_CpuShowCallers(out);
			}
		}
		if (out != stdout) {
			fclose(out);
		}
	} else {
		DebugUI_PrintCmdHelp(psArgs[0]);
		return false;
	}
	return true;
}
