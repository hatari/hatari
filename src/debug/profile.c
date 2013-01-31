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
#include "m68000.h"
#include "profile.h"
#include "stMemory.h"
#include "symbols.h"
#include "68kDisass.h"
#include "tos.h"
#include "file.h"

/* This is relevant with WinUAE CPU core:
 * - the default cycle exact variant needs this define to be non-zero
 * - non-cycle exact and MMU variants need this define to be 0
 * for cycle counts to make any sense
 */
#define USE_CYCLES_COUNTER 1

/* if non-zero, output info on profiled data while profiling */
#define DEBUG 0

#define MAX_PROFILE_VALUE 0xFFFFFFFF

typedef struct {
	Uint32 count;	/* how many times this address is used */
	Uint32 cycles;	/* how many CPU cycles was taken at this address */
	Uint32 misses;  /* how many CPU cache misses happend at this address */
} profile_item_t;

typedef struct {
	unsigned long long all_cycles, all_count, all_misses;
	Uint32 max_cycles;	/* for overflow check (cycles > count or misses) */
	Uint32 lowest, highest;	/* active address range within memory area */
	Uint32 active;          /* number of active addresses */
} profile_area_t;

#define MAX_MISS 4

static struct {
	unsigned long long all_cycles, all_count, all_misses;
	Uint32 miss_counts[MAX_MISS];
	Uint32 size;          /* number of allocated profile data items */
	profile_item_t *data; /* profile data items */
	profile_area_t ram;   /* normal RAM stats */
	profile_area_t rom;   /* cartridge ROM stats */
	profile_area_t tos;   /* ROM TOS stats */
	Uint32 active;        /* number of active data items in all areas */
	Uint32 *sort_arr;     /* data indexes used for sorting */
	Uint32 prev_cycles;   /* previous instruction cycles counter */
	Uint32 prev_idx;      /* previous instruction address index */
	bool enabled;         /* true when profiling enabled */
} cpu_profile;


#define DSP_PROFILE_ARR_SIZE 0x10000

static struct {
	profile_item_t *data; /* profile data */
	profile_area_t ram;   /* normal RAM stats */
	Uint16 *sort_arr;     /* data indexes used for sorting */
	Uint16 prev_pc;       /* previous PC for which the cycles are for */
	bool enabled;         /* true when profiling enabled */
} dsp_profile;


/* ------------------ CPU profile results ----------------- */

/**
 * convert Atari memory address to sorting array profile data index.
 */
static inline Uint32 address2index(Uint32 pc)
{
	if (unlikely(pc & 1)) {
		fprintf(stderr, "WARNING: odd CPU profile instruction address 0x%x!\n", pc);
	}
	if (pc >= TosAddress && pc < TosAddress + TosSize) {
		/* TOS, put it after RAM & ROM data */
		pc = pc - TosAddress + STRamEnd + 0x20000;
	
	} else if (pc >= 0xFA0000 && pc < 0xFC0000) {
		/* ROM, put it after RAM data */
		pc = pc - 0xFA0000 + STRamEnd;

	} else {
		/* if in RAM, use as-is */
		if (unlikely(pc >= STRamEnd)) {
			fprintf(stderr, "WARNING: 'invalid' CPU PC profile instruction address 0x%x!\n", pc);
			/* extra entry at end is reserved for invalid PC values */
			pc = STRamEnd + 0x20000 + TosSize;
		}
	}
	/* CPU instructions are at even addresses, save space by halving */
	return (pc >> 1);
}


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
	*percentage = 100.0*(*count)/cpu_profile.all_count;
	return (*count > 0);
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
	/* ROM */
	idx -= STRamEnd;
	if (idx < 0x20000) {
		return idx + 0xFA0000;
	}
	/* TOS */
	return idx - 0x20000 + TosAddress;
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
		(float)area->active/cpu_profile.active*100);
	fprintf(stderr, "- executed instructions:\n  %llu (%.2f%% of all)\n",
		area->all_count,
		(float)area->all_count/cpu_profile.all_count*100);
	fprintf(stderr, "- used cycles:\n  %llu (%.2f%% of all)\n",
		area->all_cycles,
		(float)area->all_cycles/cpu_profile.all_cycles*100);
#if ENABLE_WINUAE_CPU
	if (cpu_profile.all_misses) {	/* CPU cache in use? */
		fprintf(stderr, "- instruction cache misses:\n  %llu (%.2f%% of all)\n",
			area->all_misses,
			(float)area->all_misses/cpu_profile.all_misses*100);
	}
#endif
	if (area->max_cycles == MAX_PROFILE_VALUE) {
		fprintf(stderr, "- Counters OVERFLOW!\n");
	}
}


/**
 * show CPU area (RAM, ROM, TOS) specific statistics.
 */
static void Profile_CpuShowStats(void)
{
	fprintf(stderr, "Normal RAM (0-0x%X):\n", STRamEnd);
	show_cpu_area_stats(&cpu_profile.ram);

	fprintf(stderr, "Cartridge ROM (0xFA0000-0xFC0000):\n");
	show_cpu_area_stats(&cpu_profile.rom);

	fprintf(stderr, "ROM TOS (0x%X-0x%X):\n", TosAddress, TosAddress + TosSize);
	show_cpu_area_stats(&cpu_profile.tos);

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
	profile_item_t *data;
	uaecptr nextpc, addr;
	Uint32 size, active, text;

	data = cpu_profile.data;
	if (!data) {
		fprintf(stderr, "ERROR: no CPU profiling data available!\n");
		return;
	}

	/* some information for interpreting the addresses */
	fprintf(out, "ROM TOS:\t0x%06x-0x%06x\n", TosAddress, TosAddress + TosSize);
	fprintf(out, "Normal RAM:\t0x%06x-0x%06x\n", 0, STRamEnd);
	text = DebugInfo_GetTEXT();
	if (text < TosAddress) {
		fprintf(out, "Program TEXT:\t0x%06x-0x%06x\n", text, DebugInfo_GetTEXTEnd());
	}
	fprintf(out, "Cartridge ROM:\t0xfa0000-0xfc0000\n");

	size = cpu_profile.size;
	active = cpu_profile.active;
	if (!show || show > active) {
		show = active;
	}

	/* get/change columns */
	Disasm_GetColumns(oldcols);
	Disasm_DisableColumn(DISASM_COLUMN_HEXDUMP, oldcols, newcols);
	Disasm_SetColumns(newcols);

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
	profile_item_t *data = cpu_profile.data;
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
		       count == MAX_PROFILE_VALUE ? " (OVERFLOW)" : "");
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
	profile_item_t *data = cpu_profile.data;
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
		       count == MAX_PROFILE_VALUE ? " (OVERFLOW)" : "");
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
	profile_item_t *data = cpu_profile.data;
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
			       count == MAX_PROFILE_VALUE ? " (OVERFLOW)" : "");
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
		       count == MAX_PROFILE_VALUE ? " (OVERFLOW)" : "");

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
		       (int)sizeof(*cpu_profile.data)*cpu_profile.size/1024/1024);
	} else {
		perror("ERROR, new CPU profile buffer alloc failed");
		cpu_profile.enabled = false;
	}
	memset(cpu_profile.miss_counts, 0, sizeof(cpu_profile.miss_counts));
	cpu_profile.prev_cycles = Cycles_GetCounter(CYCLES_COUNTER_CPU);
	cpu_profile.prev_idx = address2index(M68000_GetPC());

	return cpu_profile.enabled;
}

/**
 * Update CPU cycle and count statistics for PC address.
 */
void Profile_CpuUpdate(void)
{
#if DEBUG
	static Uint32 zero_cycles;
#endif
	Uint32 idx, prev_idx, cycles, misses;

	idx = address2index(M68000_GetPC());
	assert(idx <= cpu_profile.size);

	if (likely(cpu_profile.data[idx].count < MAX_PROFILE_VALUE)) {
		cpu_profile.data[idx].count++;
	}
	prev_idx = cpu_profile.prev_idx;

#if USE_CYCLES_COUNTER
	cycles = Cycles_GetCounter(CYCLES_COUNTER_CPU);
	/* cycles taken by current instruction */
	cycles -= cpu_profile.prev_cycles;
	cpu_profile.prev_cycles += cycles;

#if DEBUG
	if (cycles == 0) {
		zero_cycles++;
		if (zero_cycles % 1024 == 0) {
			fprintf(stderr, "WARNING: %d zero cycles, latest at 0x%x\n", zero_cycles, M68000_GetPC());
		}
	}
#endif
#else
	cycles = CurrentInstrCycles + nWaitStateCycles;
#endif
	if (likely(cpu_profile.data[prev_idx].cycles < MAX_PROFILE_VALUE - cycles)) {
		cpu_profile.data[prev_idx].cycles += cycles;
	} else {
		cpu_profile.data[prev_idx].cycles = MAX_PROFILE_VALUE;
	}

#if ENABLE_WINUAE_CPU
	/* TODO: should this also use prev_idx? */
	misses = CpuInstruction.iCacheMisses;
	assert(misses < MAX_MISS);
	cpu_profile.miss_counts[misses]++;
	if (likely(cpu_profile.data[idx].misses < MAX_PROFILE_VALUE - misses)) {
		cpu_profile.data[idx].misses += misses;
	} else {
		cpu_profile.data[idx].misses = MAX_PROFILE_VALUE;
	}
#endif
	cpu_profile.prev_idx = idx;
}


/**
 * Helper for collecting profile area statistics.
 */
static void update_area(Uint32 addr, profile_item_t *item, profile_area_t *area)
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
	profile_item_t *item;
	profile_area_t *area;
	Uint32 *sort_arr;
	Uint32 i, active;

	if (!cpu_profile.enabled) {
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
		update_area(i, item, area);
	}

	/* ... for Cartridge ROM */
	area = &cpu_profile.rom;
	memset(area, 0, sizeof(profile_area_t));
	area->lowest = cpu_profile.size;

	for (; i < (STRamEnd + 0x20000)/2; i++, item++) {
		update_area(i, item, area);
	}

	/* ...for ROM TOS */
	area = &cpu_profile.tos;
	memset(area, 0, sizeof(profile_area_t));
	area->lowest = cpu_profile.size;

	for (; i < cpu_profile.size; i++, item++) {
		update_area(i, item, area);
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

	/* ...for Cartridge ROM */
	area = &cpu_profile.rom;
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
	//printf("%d/%d/%d\n", area->active, sort_arr-cpu_profile.sort_arr, active);

	Profile_CpuShowStats();
	return;
}


/* ------------------ DSP profile results ----------------- */

/**
 * Get DSP cycles, count and count percentage for given address.
 * Return true if data was available and non-zero, false otherwise.
 */
bool Profile_DspAddressData(Uint16 addr, float *percentage, Uint32 *count, Uint32 *cycles)
{
	if (!dsp_profile.data) {
		return false;
	}
	*cycles = dsp_profile.data[addr].cycles;
	*count = dsp_profile.data[addr].count;
	*percentage = 100.0*(*count)/dsp_profile.ram.all_count;
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
	fprintf(stderr, "- used cycles:\n  %llu\n",
		area->all_cycles);
	if (area->max_cycles == MAX_PROFILE_VALUE) {
		fprintf(stderr, "- Counters OVERFLOW!\n");
	}
}

/**
 * Show first 'show' DSP instructions which execution was profiled,
 * in the address order.
 */
static void Profile_DspShowAddresses(unsigned int show, FILE *out)
{
	unsigned int shown;
	profile_item_t *data;
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
	Uint32 count1 = dsp_profile.data[*(const Uint16*)p1].cycles;
	Uint32 count2 = dsp_profile.data[*(const Uint16*)p2].cycles;
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
	profile_item_t *data = dsp_profile.data;
	float percentage;
	Uint32 count;

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
		printf("0x%04x\t%.2f%%\t%d%s\n", addr, percentage, count,
		       count == MAX_PROFILE_VALUE ? " (OVERFLOW)" : "");
	}
	printf("%d DSP addresses listed.\n", show);
}


/**
 * compare function for qsort() to sort DSP profile data by descdending
 * address access counts.
 */
static int profile_by_dsp_count(const void *p1, const void *p2)
{
	Uint32 count1 = dsp_profile.data[*(const Uint16*)p1].count;
	Uint32 count2 = dsp_profile.data[*(const Uint16*)p2].count;
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
	profile_item_t *data = dsp_profile.data;
	unsigned int symbols, matched, active;
	Uint16 *sort_arr, *end, addr;
	const char *name;
	float percentage;
	Uint32 count;

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
			printf("0x%04x\t%.2f%%\t%d%s\n",
			       addr, percentage, count,
			       count == MAX_PROFILE_VALUE ? " (OVERFLOW)" : "");
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
		printf("0x%04x\t%.2f%%\t%d\t%s%s\n",
		       addr, percentage, count, name,
		       count == MAX_PROFILE_VALUE ? " (OVERFLOW)" : "");

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
	/* first instruction cycles destination */
	dsp_profile.prev_pc = DSP_GetPC();
	return dsp_profile.enabled;
}

/**
 * Update DSP cycle and count statistics for PC address.
 */
void Profile_DspUpdate(void)
{
	Uint16 pc, prev_pc, cycles;

	pc = DSP_GetPC();
	if (likely(dsp_profile.data[pc].count < MAX_PROFILE_VALUE)) {
		dsp_profile.data[pc].count++;
	}

	/* cycle information at this point is for previous instruction */
	prev_pc = dsp_profile.prev_pc;
	cycles = DSP_GetInstrCycles();
	if (likely(dsp_profile.data[prev_pc].cycles < MAX_PROFILE_VALUE - cycles)) {
		dsp_profile.data[prev_pc].cycles += cycles;
	} else {
		dsp_profile.data[prev_pc].cycles = MAX_PROFILE_VALUE;
	}
	dsp_profile.prev_pc = pc;
}


/**
 * Stop and process the DSP profiling data; collect stats and
 * prepare for more optimal sorting.
 */
void Profile_DspStop(void)
{
	profile_item_t *item;
	profile_area_t *area;
	Uint16 *sort_arr;
	Uint32 i;

	if (!dsp_profile.enabled) {
		return;
	}
	/* find lowest and highest  addresses executed */
	item = dsp_profile.data;
	area = &dsp_profile.ram;
	memset(area, 0, sizeof(profile_area_t));
	area->lowest = DSP_PROFILE_ARR_SIZE;

	for (i = 0; i < DSP_PROFILE_ARR_SIZE; i++, item++) {
		update_area(i, item, area);
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
	return;
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
		"addresses", "counts", "cycles", "misses", "off", "on", "stats", "symbols"
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
	  "<on|off|stats|counts|cycles|misses|symbols|addresses> [show count] [file]\n"
	  "\t'on' & 'off' enable and disable profiling.  Data is collected\n"
	  "\tuntil debugger is entered again at which point you get profiling\n"
	  "\tstatistics ('stats') summary.  Then you can ask for list of the\n"
	  "\tPC addresses, sorted either by execution 'counts', used 'cycles'\n"
	  "\tor cache misses. First can be limited just to addresses with 'symbols'.\n"
	  "\t'addresses' lists the profiled addresses in order, with\n"
	  "\tthe instructions (currently) residing at them.\n"
	  "\tOptional count will limit on how many will be shown.\n"
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
	} else if (strcmp(psArgs[1], "addresses") == 0) {
		FILE *out;
		if (nArgc > 3) {
			if (File_Exists(psArgs[3])) {
				fprintf(stderr, "ERROR: output file already exists,\nremove it or give another name!\n");
				return false;
			}
			if (!(out = fopen(psArgs[3], "w"))) {
				fprintf(stderr, "ERROR: opening '%s' for writing failed!\n", psArgs[3]);
				perror(NULL);
				return false;
			}
			fprintf(out, "Hatari %s profile\n", bForDsp ? "DSP" : "CPU");
		} else {
			out = stdout;
		}
		if (bForDsp) {
			Profile_DspShowAddresses(show, out);
		} else {
			Profile_CpuShowAddresses(show, out);
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
