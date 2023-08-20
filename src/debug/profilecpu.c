/*
 * Hatari - profilecpu.c
 * 
 * Copyright (C) 2010-2019 by Eero Tamminen
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * profilecpu.c - functions for profiling CPU and showing the results.
 */
const char Profilecpu_fileid[] = "Hatari profilecpu.c";

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include "main.h"
#include "configuration.h"
#include "clocks_timings.h"
#include "debugInfo.h"
#include "dsp.h"
#include "m68000.h"
#include "68kDisass.h"
#include "symbols.h"
#include "profile.h"
#include "profile_priv.h"
#include "debug_priv.h"
#include "stMemory.h"
#include "tos.h"
#include "video.h"


/* cartridge area */
#define CART_START	0xFA0000
#define CART_END	0xFC0000
#define CART_SIZE	(CART_END - CART_START)

#define TTRAM_START	0x01000000

/* if non-zero, output (more) warnings on suspicious:
 * - cycle/instruction counts
 * - PC switches
 * And drop to debugger on invalid current & previous PC addresses.
 *
 * NOTE: DebugUI() calls that DEBUG define enables, can cause
 * instruction count mismatch assertions because debugger invocation
 * resets the counters AND happens in middle of data collection.
 * It's best to quit after debugging the issue ('q' command).
 */
#define DEBUG 0
#if DEBUG
#include "debugui.h"
static bool skip_assert;
#endif

/* whether to track & show all cache stats for all instructions */
#define DEBUG_CACHE 0


static callinfo_t cpu_callinfo;

#define MAX_CPU_PROFILE_VALUE 0xFFFFFFFF

typedef struct {
	uint32_t count;	/* how many times this address instruction is executed */
	uint32_t cycles;	/* how many CPU cycles was taken at this address */
#if DEBUG_CACHE		  /* track also less relevant cache events */
	uint32_t i_hits;    /* how many CPU i-cache hits happened at this address */
	uint32_t d_misses;  /* how many CPU d-cache misses happened at this address */
#endif
	uint32_t i_misses;  /* how many CPU i-cache misses happened at this address */
	uint32_t d_hits;    /* how many CPU d-cache hits happened at this address */
} cpu_profile_item_t;


/* max count of hits/misses single instruction can trigger at once */
#define MAX_I_HITS   8
#define MAX_I_MISSES 8
#define MAX_D_HITS   32
#define MAX_D_MISSES 20

static struct {
	counters_t all;       /* total counts for all areas */
	cpu_profile_item_t *data; /* profile data items */
	uint32_t size;          /* number of allocated profile data items */
	profile_area_t ttram; /* TT-RAM stats */
	profile_area_t ram;   /* normal RAM stats */
	profile_area_t rom;   /* cartridge ROM stats */
	profile_area_t tos;   /* ROM TOS stats */
	int active;           /* number of active data items in all areas */
	uint32_t *sort_arr;     /* data indexes used for sorting */
	int prev_family;      /* previous instruction opcode family */
	uint64_t prev_cycles;   /* previous instruction cycles counter */
	uint32_t prev_pc;       /* previous instruction address */
	uint32_t loop_start;    /* address of last loop start */
	uint32_t loop_end;      /* address of last loop end */
	uint32_t loop_count;    /* how many times it was looped */
	uint32_t disasm_addr;   /* 'addresses' command start address */
	uint32_t i_prefetched;  /* instructions that don't incur prefetch hit/miss */
	uint32_t i_hit_counts[MAX_I_HITS];    /* I-cache hit counts */
	uint32_t d_hit_counts[MAX_D_HITS];    /* D-cache hit counts */
	uint32_t i_miss_counts[MAX_I_MISSES]; /* I-cache miss counts */
	uint32_t d_miss_counts[MAX_D_MISSES]; /* D-cache miss counts */
	bool processed;	      /* true when data is already processed */
	bool enabled;         /* true when profiling enabled */
} cpu_profile;

/* full counts for warnings that are printed without rate-limiting */
typedef struct {
	int odd;
	int address;
	int returns;
	int multireturn;
	int prevpc;
	int largevalue;
	int opfamily;
	int zerocycles;
} cpu_warnings_t;
static cpu_warnings_t cpu_warnings;

#define MAX_SHOW_COUNT	8
#define MAX_MULTI_RETURN 1


/* ------------------ CPU profile address mapping ----------------- */

/**
 * convert Atari memory address to sorting array profile data index.
 */
static inline uint32_t address2index(uint32_t pc)
{
	if (unlikely(pc & 1)) {
		if (++cpu_warnings.odd <= MAX_SHOW_COUNT) {
			fprintf(stderr, "WARNING: odd CPU profile instruction address 0x%x!\n", pc);
			if (cpu_warnings.odd == MAX_SHOW_COUNT) {
				fprintf(stderr, "Further warnings won't be shown.\n");
			}
		}
#if DEBUG
		skip_assert = true;
		DebugUI(REASON_CPU_EXCEPTION);
#endif
	}
	if (pc < STRamEnd) {
		/* most likely case, use RAM address as-is */

	} else if (pc >= TosAddress && pc < TosAddress + TosSize) {
		/* TOS, put it after RAM data */
		pc = pc - TosAddress + STRamEnd;
		if (TosAddress >= CART_END) {
			/* and after cartridge data as it's higher */
			pc += CART_SIZE;
		}
	} else if (pc >= CART_START && pc < CART_END) {
		/* ROM, put it after RAM data */
		pc = pc - CART_START + STRamEnd;
		if (TosAddress < CART_START) {
			/* and after TOS as it's higher */
			pc += TosSize;
		}
	} else if (TTmemory && pc >= TTRAM_START && pc < TTRAM_START + 1024*(unsigned)ConfigureParams.Memory.TTRamSize_KB) {
		pc += STRamEnd + TosSize + CART_SIZE - TTRAM_START;
	} else {
		if (++cpu_warnings.address <= MAX_SHOW_COUNT) {
			fprintf(stderr, "WARNING: 'invalid' CPU PC profile instruction address 0x%x!\n", pc);
			if (cpu_warnings.address == MAX_SHOW_COUNT) {
				fprintf(stderr, "Further warnings won't be shown.\n");
			}
		}
		/* extra entry at end is reserved for invalid PC values */
		pc = STRamEnd + TosSize + CART_SIZE;
#if DEBUG
		skip_assert = true;
		DebugUI(REASON_CPU_EXCEPTION);
#endif
	}
	/* CPU instructions are at even addresses, save space by halving */
	return (pc >> 1);
}

/**
 * convert sorting array profile data index to Atari memory address.
 */
static uint32_t index2address(uint32_t idx)
{
	idx <<= 1;
	/* RAM */
	if (idx < STRamEnd) {
		return idx;
	}
	idx -= STRamEnd;
	/* TOS before cartridge area? */
	if (TosAddress < CART_START) {
		/* TOS */
		if (idx < TosSize) {
			return idx + TosAddress;
		}
		idx -= TosSize;
		/* ROM */
		if (idx < CART_SIZE) {
			return idx + CART_START;
		}
		idx -= CART_SIZE;
	} else {
		/* ROM */
		if (idx < CART_SIZE) {
			return idx + CART_START;
		}
		idx -= CART_SIZE;
		/* TOS */
		if (idx < TosSize) {
			return idx + TosAddress;
		}
		idx -= TosSize;
	}
	return idx + TTRAM_START;
}

/* ------------------ CPU profile results ----------------- */

/**
 * Return true if there's profile data for given address, false otherwise
 */
bool Profile_CpuAddr_HasData(uint32_t addr)
{
	cpu_profile_item_t *item;
	uint32_t idx;

	if (!cpu_profile.data) {
		return false;
	}
	idx = address2index(addr);
	item = &(cpu_profile.data[idx]);
	if (!item->count) {
		return false;
	}
	return true;
}

/**
 * Write string containing CPU cache stats, cycles, count, count percentage
 * for given address to provided buffer.
 *
 * Return zero if there's no profiling data for given address,
 * otherwise return the number of bytes consumed from the given buffer.
 */
int Profile_CpuAddr_DataStr(char *buffer, int maxlen, uint32_t addr)
{
	cpu_profile_item_t *item;
	float percentage;
	uint32_t idx;
	int count;

	assert(buffer && maxlen > 0);
	if (!cpu_profile.data) {
		return 0;
	}
	idx = address2index(addr);
	item = &(cpu_profile.data[idx]);
	if (!item->count) {
		return 0;
	}

	if (cpu_profile.all.count) {
		percentage = 100.0 * item->count / cpu_profile.all.count;
	} else {
		percentage = 0.0;
	}
#if DEBUG_CACHE
	count = snprintf(buffer, maxlen, "%5.2f%% (%u, %u, %u, %u, %u, %u)",
			 percentage, item->count, item->cycles,
			 item->i_hits, item->i_misses,
			 item->d_hits, item->d_misses);
#else
	count = snprintf(buffer, maxlen, "%5.2f%% (%u, %u, %u, %u)",
			 percentage, item->count, item->cycles,
			 item->i_misses, item->d_hits);
#endif
	if (count >= maxlen) {
		/* truncated by (count - maxlen) amount */
		return maxlen;
	}
	return count;
}

/**
 * Show CPU profiling warning counts
 */
static void show_cpu_warnings(void)
{
	cpu_warnings_t warnings;
	memset(&warnings, 0, sizeof(warnings));
	if (memcmp(&cpu_warnings, &warnings, sizeof(warnings)) == 0) {
		return;
	}
	fprintf(stderr, "\nCPU profiling warning counts:\n");
	if (cpu_warnings.odd) {
		fprintf(stderr, "- Odd PC addresses: %d\n", cpu_warnings.odd);
	}
	if (cpu_warnings.address) {
		fprintf(stderr, "- Unmapped PC addresses: %d\n", cpu_warnings.address);
	}
	if (cpu_warnings.opfamily) {
		fprintf(stderr, "- Unrecognized (zero) opcode families: %d\n", cpu_warnings.opfamily);
	}
	if (cpu_warnings.returns) {
		fprintf(stderr, "- Subroutine calls didn't return through RTS etc: %d\n", cpu_warnings.returns);
	}
	if (cpu_warnings.multireturn > MAX_MULTI_RETURN) {
		fprintf(stderr, "- Subroutine calls returned (at max) through %d stack frames\n", cpu_warnings.multireturn);
	}
	if (cpu_warnings.prevpc) {
		fprintf(stderr, "- Undefined PC value for tracked address callers: %d\n", cpu_warnings.prevpc);
	}
	if (cpu_warnings.largevalue) {
		fprintf(stderr, "- Unexpectedly large cycles count or cache hit/miss values: %d\n", cpu_warnings.largevalue);
	}
	if (cpu_warnings.zerocycles) {
		fprintf(stderr, "- Successive instructions with zero cycles: %d\n", cpu_warnings.zerocycles);
	}
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
	fprintf(stderr, "- active instruction addresses:\n  %d (%.2f%% of all areas)\n",
		area->active,
		100.0 * area->active / cpu_profile.active);
	fprintf(stderr, "- executed instructions:\n  %"PRIu64" (%.2f%% of all areas)\n",
		area->counters.count,
		100.0 * area->counters.count / cpu_profile.all.count);
	/* CPU cache in use? */
	if (cpu_profile.all.i_misses) {
		fprintf(stderr, "- instruction cache misses:\n  %"PRIu64" (%.2f%% of all areas)\n",
			area->counters.i_misses,
			100.0 * area->counters.i_misses / cpu_profile.all.i_misses);
	}
	if (cpu_profile.all.d_hits) {
		fprintf(stderr, "- data cache hits:\n  %"PRIu64" (%.2f%% of all areas)\n",
			area->counters.d_hits,
			100.0 * area->counters.d_hits / cpu_profile.all.d_hits);
	}
	fprintf(stderr, "- used cycles:\n  %"PRIu64" (%.2f%% of all areas)\n  = %.5fs\n",
		area->counters.cycles,
		100.0 * area->counters.cycles / cpu_profile.all.cycles,
		(double)area->counters.cycles / MachineClocks.CPU_Freq_Emul);
	if (area->overflow) {
		fprintf(stderr, "  *** COUNTER OVERFLOW! ***\n");
	}
}


/**
 * show CPU area (RAM, ROM, TOS) specific statistics and error counts.
 */
void Profile_CpuShowStats(void)
{
	fprintf(stderr, "Normal RAM (0-0x%X):\n", STRamEnd);
	show_cpu_area_stats(&cpu_profile.ram);

	fprintf(stderr, "ROM TOS (0x%X-0x%X):\n", TosAddress, TosAddress + TosSize);
	show_cpu_area_stats(&cpu_profile.tos);

	fprintf(stderr, "Cartridge ROM (0x%X-%X):\n", CART_START, CART_END);
	show_cpu_area_stats(&cpu_profile.rom);

	if (TTmemory && ConfigureParams.Memory.TTRamSize_KB) {
		fprintf(stderr, "TT-RAM (0x%X-%X):\n", TTRAM_START, TTRAM_START + 1024*ConfigureParams.Memory.TTRamSize_KB);
		show_cpu_area_stats(&cpu_profile.ttram);
	}
	fprintf(stderr, "\n= %.5fs\n",
		(double)cpu_profile.all.cycles / MachineClocks.CPU_Freq_Emul);

	show_cpu_warnings();
}

/**
 * show percentage histogram of given array items
 */
static void show_histogram(const char *title, int count, uint32_t *items)
{
	const uint64_t maxval = cpu_profile.all.count;
	uint32_t value;
	int i;

	fprintf(stderr, "\n%s, number of occurrences:\n", title);
	for (i = 0; i < count; i++) {
		value = items[i];
		if (value) {
			int w, width = 50 * value / maxval+1;
			fprintf(stderr, " %2d: ", i);
			for (w = 0; w < width; w++) {
				fputc('#', stderr);
			}
			fprintf(stderr, " %.3f%%\n", 100.0 * value / maxval);
		}
	}
}

/**
 * show CPU cache usage histograms
 */
void Profile_CpuShowCaches(void)
{
	if (!(cpu_profile.all.i_misses || cpu_profile.all.d_hits)) {
		fprintf(stderr, "No instruction/data cache information.\n");
		return;
	}
	fprintf(stderr,
		"\nNote:\n"
		"- these statistics include all profiled instructions, but\n"
		"- instruction cache events happen only on prefetch/branch\n"
		"- data cache events can happen only for instructions that do memory reads\n"
		"\nAlready prefetched instructions: %.3f%% (no hits/misses)\n",
		100.0 * cpu_profile.i_prefetched / cpu_profile.all.count);

	show_histogram("Instruction cache hits per instruction",
		       ARRAY_SIZE(cpu_profile.i_hit_counts), cpu_profile.i_hit_counts);
	show_histogram("Instruction cache misses per instruction",
		       ARRAY_SIZE(cpu_profile.i_miss_counts), cpu_profile.i_miss_counts);
	show_histogram("Data cache hits per instruction",
		       ARRAY_SIZE(cpu_profile.d_hit_counts), cpu_profile.d_hit_counts);
	show_histogram("Data cache misses per instruction",
		       ARRAY_SIZE(cpu_profile.d_miss_counts), cpu_profile.d_miss_counts);
}

/**
 * Show CPU instructions which execution was profiled, in the address order,
 * starting from the given address.  Return next disassembly address.
 */
uint32_t Profile_CpuShowAddresses(uint32_t lower, uint32_t upper, FILE *out, paging_t use_paging)
{
	int oldcols[DISASM_COLUMNS], newcols[DISASM_COLUMNS];
	int show, shown, addrs, active;
	const char *symbol;
	cpu_profile_item_t *data;
	uint32_t idx, end, size;
	uaecptr nextpc, addr;

	data = cpu_profile.data;
	if (!data) {
		fprintf(stderr, "ERROR: no CPU profiling data available!\n");
		return 0;
	}

	size = cpu_profile.size;
	active = cpu_profile.active;
	if (upper) {
		end = address2index(upper);
		if (end > size) {
			end = size;
		}
	} else {
		end = size;
	}
	show = INT_MAX;
	if (use_paging == PAGING_ENABLED) {
		show = DebugUI_GetPageLines(ConfigureParams.Debugger.nDisasmLines, 0);
		if (!show) {
			show = INT_MAX;
		}
	}

	/* get/change columns */
	Disasm_GetColumns(oldcols);
	Disasm_DisableColumn(DISASM_COLUMN_HEXDUMP, oldcols, newcols);
	Disasm_SetColumns(newcols);

	fputs("# disassembly with profile data: <instructions percentage>% (<sum of instructions>, <sum of cycles>, <sum of i-cache misses>, <sum of d-cache hits>)\n", out);
	shown = 2; /* first and last printf */

	addrs = nextpc = 0;
	idx = address2index(lower);
	for (; shown < show && addrs < active && idx < end; idx++) {
		if (!data[idx].count) {
			continue;
		}
		addr = index2address(idx);
		if (addr != nextpc && nextpc) {
			fprintf(out, "[...]\n");
			shown++;
		}
		symbol = Symbols_GetByCpuAddress(addr, SYMTYPE_CODE);
		if (symbol) {
			fprintf(out, "%s:\n", symbol);
			shown++;
		}
		/* NOTE: column setup works only with 68kDisass disasm engine! */
		Disasm(out, addr, &nextpc, 1);
		shown++;
		addrs++;
	}
	if (idx < end) {
		fprintf(stderr, "Disassembled %d (of active %d) CPU addresses.\n",
			addrs, active);
	} else {
		fprintf(stderr, "Disassembled last %d (of active %d) CPU addresses, wrapping...\n",
			addrs, active);
		nextpc = 0;
	}
	/* restore disassembly columns */
	Disasm_SetColumns(oldcols);
	return nextpc;
}

/**
 * remove all disassembly columns except instruction ones.
 * data needed to restore columns is stored to "oldcols"
 */
static void leave_instruction_column(int *oldcols)
{
	int i, newcols[DISASM_COLUMNS];

	Disasm_GetColumns(oldcols);
	for (i = 0; i < DISASM_COLUMNS; i++) {
		if (i == DISASM_COLUMN_OPCODE || i == DISASM_COLUMN_OPERAND) {
			continue;
		}
		Disasm_DisableColumn(i, oldcols, newcols);
		oldcols = newcols;
	}
	Disasm_SetColumns(newcols);
}

/**
 * compare function for qsort() to sort CPU profile data by instruction cache misses.
 */
static int cmp_cpu_i_misses(const void *p1, const void *p2)
{
	uint32_t count1 = cpu_profile.data[*(const uint32_t*)p1].i_misses;
	uint32_t count2 = cpu_profile.data[*(const uint32_t*)p2].i_misses;
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
void Profile_CpuShowInstrMisses(int show)
{
	int active;
	int oldcols[DISASM_COLUMNS];
	uint32_t *sort_arr, *end, addr, nextpc;
	cpu_profile_item_t *data = cpu_profile.data;
	float percentage;
	uint32_t count;

	if (!cpu_profile.all.i_misses) {
		fprintf(stderr, "No CPU instruction cache miss information available.\n");
		return;
	}

	active = cpu_profile.active;
	sort_arr = cpu_profile.sort_arr;
	qsort(sort_arr, active, sizeof(*sort_arr), cmp_cpu_i_misses);

	leave_instruction_column(oldcols);

	fprintf(stderr, "addr:\t\ti-cache misses:\n");
	show = (show < active ? show : active);
	for (end = sort_arr + show; sort_arr < end; sort_arr++) {
		addr = index2address(*sort_arr);
		count = data[*sort_arr].i_misses;
		percentage = 100.0*count/cpu_profile.all.i_misses;
		fprintf(stderr, "0x%06x\t%5.2f%%\t%d%s\t", addr, percentage, count,
		       count == MAX_CPU_PROFILE_VALUE ? " (OVERFLOW)" : "");
		Disasm(stderr, addr, &nextpc, 1);
	}
	fprintf(stderr, "%d CPU addresses listed.\n", show);

	Disasm_SetColumns(oldcols);
}

/**
 * compare function for qsort() to sort CPU profile data by data cache hits.
 */
static int cmp_cpu_d_hits(const void *p1, const void *p2)
{
	uint32_t count1 = cpu_profile.data[*(const uint32_t*)p1].d_hits;
	uint32_t count2 = cpu_profile.data[*(const uint32_t*)p2].d_hits;
	if (count1 > count2) {
		return -1;
	}
	if (count1 < count2) {
		return 1;
	}
	return 0;
}

/**
 * Sort CPU profile data addresses by data cache hits and show the results.
 */
void Profile_CpuShowDataHits(int show)
{
	int active;
	int oldcols[DISASM_COLUMNS];
	uint32_t *sort_arr, *end, addr, nextpc;
	cpu_profile_item_t *data = cpu_profile.data;
	float percentage;
	uint32_t count;

	if (!cpu_profile.all.d_hits) {
		fprintf(stderr, "No CPU data cache hit information available.\n");
		return;
	}

	active = cpu_profile.active;
	sort_arr = cpu_profile.sort_arr;
	qsort(sort_arr, active, sizeof(*sort_arr), cmp_cpu_d_hits);

	leave_instruction_column(oldcols);

	fprintf(stderr, "addr:\t\td-cache hits:\n");
	show = (show < active ? show : active);
	for (end = sort_arr + show; sort_arr < end; sort_arr++) {
		addr = index2address(*sort_arr);
		count = data[*sort_arr].d_hits;
		percentage = 100.0*count/cpu_profile.all.d_hits;
		fprintf(stderr, "0x%06x\t%5.2f%%\t%d%s\t", addr, percentage, count,
		       count == MAX_CPU_PROFILE_VALUE ? " (OVERFLOW)" : "");
		Disasm(stderr, addr, &nextpc, 1);
	}
	fprintf(stderr, "%d CPU addresses listed.\n", show);

	Disasm_SetColumns(oldcols);
}


/**
 * compare function for qsort() to sort CPU profile data by cycles counts.
 */
static int cmp_cpu_cycles(const void *p1, const void *p2)
{
	uint32_t count1 = cpu_profile.data[*(const uint32_t*)p1].cycles;
	uint32_t count2 = cpu_profile.data[*(const uint32_t*)p2].cycles;
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
void Profile_CpuShowCycles(int show)
{
	int active;
	int oldcols[DISASM_COLUMNS];
	uint32_t *sort_arr, *end, addr, nextpc;
	cpu_profile_item_t *data = cpu_profile.data;
	float percentage;
	uint32_t count;

	if (!data) {
		fprintf(stderr, "ERROR: no CPU profiling data available!\n");
		return;
	}

	active = cpu_profile.active;
	sort_arr = cpu_profile.sort_arr;
	qsort(sort_arr, active, sizeof(*sort_arr), cmp_cpu_cycles);

	leave_instruction_column(oldcols);

	fprintf(stderr, "addr:\t\tcycles:\n");
	show = (show < active ? show : active);
	for (end = sort_arr + show; sort_arr < end; sort_arr++) {
		addr = index2address(*sort_arr);
		count = data[*sort_arr].cycles;
		percentage = 100.0*count/cpu_profile.all.cycles;
		fprintf(stderr, "0x%06x\t%5.2f%%\t%d%s\t", addr, percentage, count,
		       count == MAX_CPU_PROFILE_VALUE ? " (OVERFLOW)" : "");
		Disasm(stderr, addr, &nextpc, 1);
	}
	fprintf(stderr, "%d CPU addresses listed.\n", show);

	Disasm_SetColumns(oldcols);
}

/**
 * compare function for qsort() to sort CPU profile data by descending
 * address access counts.
 */
static int cmp_cpu_count(const void *p1, const void *p2)
{
	uint32_t count1 = cpu_profile.data[*(const uint32_t*)p1].count;
	uint32_t count2 = cpu_profile.data[*(const uint32_t*)p2].count;
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
void Profile_CpuShowCounts(int show, bool only_symbols)
{
	cpu_profile_item_t *data = cpu_profile.data;
	int symbols, matched, active;
	int oldcols[DISASM_COLUMNS];
	uint32_t *sort_arr, *end, addr, nextpc;
	const char *name;
	float percentage;
	uint32_t count;

	if (!data) {
		fprintf(stderr, "ERROR: no CPU profiling data available!\n");
		return;
	}
	active = cpu_profile.active;
	show = (show < active ? show : active);

	sort_arr = cpu_profile.sort_arr;
	qsort(sort_arr, active, sizeof(*sort_arr), cmp_cpu_count);

	if (!only_symbols) {
		leave_instruction_column(oldcols);
		fprintf(stderr, "addr:\t\tcount:\n");
		for (end = sort_arr + show; sort_arr < end; sort_arr++) {
			addr = index2address(*sort_arr);
			count = data[*sort_arr].count;
			percentage = 100.0*count/cpu_profile.all.count;
			fprintf(stderr, "0x%06x\t%5.2f%%\t%d%s\t",
			       addr, percentage, count,
			       count == MAX_CPU_PROFILE_VALUE ? " (OVERFLOW)" : "");
			Disasm(stderr, addr, &nextpc, 1);
		}
		fprintf(stderr, "%d CPU addresses listed.\n", show);
		Disasm_SetColumns(oldcols);
		return;
	}

	symbols = Symbols_CpuCodeCount();
	if (!symbols) {
		fprintf(stderr, "ERROR: no CPU symbols loaded!\n");
		return;
	}
	matched = 0;	

	leave_instruction_column(oldcols);

	fprintf(stderr, "addr:        %%:   count:  symbol:                    disassembly:\n");
	for (end = sort_arr + active; sort_arr < end; sort_arr++) {

		addr = index2address(*sort_arr);
		name = Symbols_GetByCpuAddress(addr, SYMTYPE_CODE);
		if (!name) {
			continue;
		}
		count = data[*sort_arr].count;
		percentage = 100.0*count/cpu_profile.all.count;
		fprintf(stderr, "0x%06x %6.2f %8d  %-26s %s",
		       addr, percentage, count, name,
		       count == MAX_CPU_PROFILE_VALUE ? "(OVERFLOW) " : "");
		Disasm(stderr, addr, &nextpc, 1);

		matched++;
		if (matched >= show || matched >= symbols) {
			break;
		}
	}
	fprintf(stderr, "%d CPU symbols listed.\n", matched);

	Disasm_SetColumns(oldcols);
}


static const char * addr2name(uint32_t addr, uint64_t *total)
{
	uint32_t idx = address2index(addr);
	*total = cpu_profile.data[idx].count;
	return Symbols_GetByCpuAddress(addr, SYMTYPE_CODE);
}

/**
 * Output CPU callers info to given file.
 */
void Profile_CpuShowCallers(FILE *fp)
{
	Profile_ShowCallers(fp, cpu_callinfo.sites, cpu_callinfo.site, addr2name);
}

/**
 * Save CPU profile information to given file.
 */
void Profile_CpuSave(FILE *out)
{
	uint32_t text, end;
	fputs("Field names:\tExecuted instructions, Used cycles, Instruction cache misses, Data cache hits\n", out);

	/* (Python) regexp matching disassembly address & profiling data field
	 * (for the profile post-processor), both for the (default) WinUAE
	 * CPU core disassembler output:
	 *   <addr> <code>  <ASM>     <percentage>% (<count>, <cycles>, <i-misses>, <d-hits>)
	 *   00e00cfe 4e75  rts  == $e66218   0.16% (48753, 780396, 0, 0)
	 * And for the external disassembler output:
	 *   $<addr> :  <ASM>  <percentage>% (<count>, <cycles>, <i-misses>, <d-hits>)
	 *   $e5af38 :   rts           0.00% (12, 0, 12, 0)
	 * CPU core disassembly addresses can be lower or upper case.
	 */
	fputs("Field regexp:\t^\\$?([0-9A-Fa-f]+) .*% \\(([^)]*)\\)$\n", out);

	/* some information for interpreting the addresses */
	fprintf(out, "ST_RAM:\t\t0x%06x-0x%06x\n", 0, STRamEnd);
	end = TosAddress + TosSize;
	fprintf(out, "ROM_TOS:\t0x%06x-0x%06x\n", TosAddress, end);
	fprintf(out, "CARTRIDGE:\t0x%06x-0x%06x\n", CART_START, CART_END);
	text = DebugInfo_GetTEXT();
	if (text && (text < TosAddress || text >= TTRAM_START)) {
		fprintf(out, "PROGRAM_TEXT:\t0x%06x-0x%06x\n", text, DebugInfo_GetTEXTEnd());
	}
	if (TTmemory && ConfigureParams.Memory.TTRamSize_KB) {
		end = TTRAM_START + 1024*ConfigureParams.Memory.TTRamSize_KB;
		fprintf(out, "TT_RAM:\t\t0x%08x-0x%08x\n", TTRAM_START, end);
	} else if (end < CART_END) {
		end = CART_END;
	}
	Profile_CpuShowAddresses(0, end-2, out, PAGING_DISABLED);
	Profile_CpuShowCallers(out);
}

/* ------------------ CPU profile control ----------------- */

/**
 * Free data from last profiling run, if any
 */
void Profile_CpuFree(void)
{
	Profile_FreeCallinfo(&(cpu_callinfo));
	if (cpu_profile.sort_arr) {
		free(cpu_profile.sort_arr);
		cpu_profile.sort_arr = NULL;
	}
	if (cpu_profile.data) {
		free(cpu_profile.data);
		cpu_profile.data = NULL;
		fprintf(stderr, "Freed previous CPU profile buffers.\n");
	}
}

/**
 * Initialize CPU profiling when necessary.  Return true if profiling.
 */
bool Profile_CpuStart(void)
{
	int size;

	Profile_CpuFree();
	if (!cpu_profile.enabled) {
		return false;
	}
	/* zero everything */
	memset(&cpu_profile, 0, sizeof(cpu_profile));
	memset(&cpu_warnings, 0, sizeof(cpu_warnings));
	cpu_warnings.multireturn = MAX_MULTI_RETURN;

	/* Shouldn't change within same debug session */
	size = (STRamEnd + CART_SIZE + TosSize) / 2;
	if (TTmemory && ConfigureParams.Memory.TTRamSize_KB) {
		size += ConfigureParams.Memory.TTRamSize_KB * 1024/2;
	}

	/* Add one entry for catching invalid PC values */
	cpu_profile.data = calloc(size + 1, sizeof(*cpu_profile.data));
	if (!cpu_profile.data) {
		perror("ERROR, new CPU profile buffer alloc failed");
		return false;
	}
	fprintf(stderr, "Allocated CPU profile buffer (%d MB).\n",
	       (int)sizeof(*cpu_profile.data)*size/(1024*1024));
	cpu_profile.size = size;

	Profile_AllocCallinfo(&(cpu_callinfo), Symbols_CpuCodeCount(), "CPU");

	/* reset cache stats (CPU emulation doesn't do that) */
	CpuInstruction.D_Cache_hit = 0;
	CpuInstruction.I_Cache_hit = 0;
	CpuInstruction.I_Cache_miss = 0;
	CpuInstruction.D_Cache_miss = 0;

	cpu_profile.prev_cycles = CyclesGlobalClockCounter;
	cpu_profile.prev_family = OpcodeFamily;
	cpu_profile.prev_pc = M68000_GetPC();
	if (ConfigureParams.System.bAddressSpace24) {
		cpu_profile.prev_pc &= 0xffffff;
	}
	cpu_profile.loop_start = PC_UNDEFINED;
	cpu_profile.loop_end = PC_UNDEFINED;
	cpu_profile.loop_count = 0;
	Profile_LoopReset();

	cpu_profile.disasm_addr = 0;
	cpu_profile.processed = false;
	cpu_profile.enabled = true;
	return cpu_profile.enabled;
}

/**
 * return true if pc could be next instruction for previous pc
 */
static bool is_prev_instr(uint32_t prev_pc, uint32_t pc)
{
	/* just moved to next instruction (1-2 words)? */
	if (prev_pc < pc && (pc - prev_pc) <= 10) {
		return true;
	}
	return false;
}

/**
 * return caller instruction type classification
 */
static calltype_t cpu_opcode_type(int family, uint32_t prev_pc, uint32_t pc)
{
	switch (family) {

	case i_JSR:
	case i_BSR:
		return CALL_SUBROUTINE;

	case i_RTS:
	case i_RTR:
	case i_RTD:
		return CALL_SUBRETURN;

	case i_JMP:	/* often used also for "inlined" function calls... */
	case i_Bcc:	/* both BRA & BCC */
	case i_FBcc:
	case i_DBcc:
	case i_FDBcc:
		return CALL_BRANCH;

	case i_TRAP:
	case i_TRAPV:
	case i_TRAPcc:
	case i_FTRAPcc:
	case i_STOP:
	case i_ILLG:
	case i_CHK:
	case i_CHK2:
	case i_BKPT:
		return CALL_EXCEPTION;

	case i_RTE:
		return CALL_EXCRETURN;
	}
	/* just moved to next instruction? */
	if (is_prev_instr(prev_pc, pc)) {
		return CALL_NEXT;
	}
	return CALL_UNKNOWN;
}

/**
 * Check callstack to see if return was for an earlier subroutine
 * call higher in the stack.
 *
 * This can happen when e.g. OS task switchers manipulate stack to
 * convert subroutine return (RTS) into a jump to another function.
 * I.e. one of the returns in callstack will be skipped.
 *
 * Returns number of frames to finish/end.
 */
static int returned_frames(callinfo_t *callinfo, uint32_t pc)
{
	int frames, depth;
	uint32_t return_pc;

	depth = callinfo->depth;
	for (frames = 1; --depth >= 0; frames++) {
		return_pc = callinfo->stack[depth].ret_addr;
		if (pc == return_pc) {
			return frames;
		}
	}
	return 0;
}

/**
 * If call tracking is enabled (there are symbols), collect
 * information about subroutine and other calls, and their costs.
 * 
 * Like with profile data, caller info checks need to be for previous
 * instruction, that's why "pc" argument for this function actually
 * needs to be previous PC.
 */
static void collect_calls(uint32_t pc, counters_t *counters)
{
	calltype_t flag;
	int frames, idx, family;
	uint32_t prev_pc, caller_pc;

	family = cpu_profile.prev_family;
	cpu_profile.prev_family = OpcodeFamily;

	prev_pc = cpu_callinfo.prev_pc;
	cpu_callinfo.prev_pc = pc;
	caller_pc = PC_UNDEFINED;

	/* check opcode first as return frame check can be slow with deep call stacks */
	flag = cpu_opcode_type(family, prev_pc, pc);
	if (unlikely(flag == CALL_SUBRETURN || flag == CALL_EXCRETURN)) {
		/* is address a return address for *any* of the previous subroutine calls? */
		frames = returned_frames(&cpu_callinfo, pc);
		if (frames) {
			if (unlikely(frames > cpu_warnings.multireturn)) {
				fprintf(stderr, "WARNING: subroutine call returned through %d stack frames: 0x%x -> 0x%x!\n",
					frames, prev_pc, pc);
				cpu_warnings.multireturn = frames;
			}
			/* unwind callstack & update costs */
			while (frames-- > 0) {
				caller_pc = Profile_CallEnd(&cpu_callinfo, counters);
			}
		}
	} else if (unlikely(pc == cpu_callinfo.return_pc)) {
		/* return address, but not due to return, e.g. because
		 * there was a jsr or jump to return address.  Checked
		 * only for last return
		 */
		if (++cpu_warnings.returns <= MAX_SHOW_COUNT) {
			uint32_t nextpc;
			fprintf(stderr, "WARNING: subroutine call returned 0x%x -> 0x%x, not through RTS etc!\n", prev_pc, pc);
			Disasm(stderr, prev_pc, &nextpc, 1);
			if (cpu_warnings.returns == MAX_SHOW_COUNT) {
				fprintf(stderr, "Further warnings won't be shown.\n");
			}
		}
	}

	/* address is one which we're tracking? */
	idx = Symbols_GetCpuCodeIndex(pc);
	if (unlikely(idx >= 0)) {
		/* normal subroutine / exception call? */
		if (likely(flag == CALL_SUBROUTINE || flag == CALL_EXCEPTION)) {
			if (unlikely(prev_pc == PC_UNDEFINED)) {
				/* if first profiled instruction
				 * is subroutine call, it doesn't have
				 * valid prev_pc value stored
				 */
				cpu_callinfo.return_pc = PC_UNDEFINED;
				if (++cpu_warnings.prevpc <= MAX_SHOW_COUNT) {
					fprintf(stderr, "WARNING: previous PC for tracked address 0x%d is undefined!\n", pc);
					if (cpu_warnings.prevpc == MAX_SHOW_COUNT) {
						fprintf(stderr, "Further warnings won't be shown.\n");
					}
				}
#if DEBUG
				skip_assert = true;
				DebugUI(REASON_CPU_EXCEPTION);
#endif
			} else {
				/* return is to next instruction (slow!) */
				cpu_callinfo.return_pc = Disasm_GetNextPC(prev_pc);
			}
		} else if (caller_pc != PC_UNDEFINED) {
			/* returned from function to first instruction of another symbol:
			 *	0xf384	jsr some_function
			 *	other_symbol:
			 *	0f3x8a	some_instruction
			 * -> change return instruction address to
			 *    address of what did the returned call.
			 */
			prev_pc = caller_pc;
			assert(is_prev_instr(prev_pc, pc));
			flag = CALL_NEXT;
		}
		Profile_CallStart(idx, &cpu_callinfo, prev_pc, flag, pc, counters);
	}
}

/**
 * log last loop info, if there's suitable data for one
 */
static void log_last_loop(void)
{
	unsigned len = cpu_profile.loop_end - cpu_profile.loop_start;
	if (cpu_profile.loop_count > 1 && (len < profile_loop.cpu_limit || !profile_loop.cpu_limit)) {
		fprintf(profile_loop.fp, "CPU %d 0x%06x %d %d\n", nVBLs,
			cpu_profile.loop_start, len, cpu_profile.loop_count);
	}
}

/**
 * Warning for values going out of expected range
 */
static uint32_t warn_too_large(const char *name, const int value, const int limit, const uint32_t prev_pc, const uint32_t pc)
{
	if (++cpu_warnings.largevalue <= MAX_SHOW_COUNT) {
		uint32_t nextpc;
		fprintf(stderr, "WARNING: unexpected (%d > %d) %s at 0x%x:\n", value, limit - 1, name, pc);
		Disasm(stderr, prev_pc, &nextpc, 1);
		Disasm(stderr, pc, &nextpc, 1);
		if (cpu_warnings.largevalue == MAX_SHOW_COUNT) {
			fprintf(stderr, "Further warnings will not be shown.\n");
		}
	}
#if DEBUG
	skip_assert = true;
	DebugUI(REASON_CPU_EXCEPTION);
#endif
	return limit - 1;
}

/**
 * Update CPU cycle and count statistics for PC address.
 *
 * This gets called after instruction has executed and PC
 * has advanced to next instruction.
 */
void Profile_CpuUpdate(void)
{
	counters_t *counters = &(cpu_profile.all);
	uint32_t pc, prev_pc, idx, cycles;
	cpu_profile_item_t *prev;
	uint32_t i_hits, d_hits, i_misses, d_misses;

	prev_pc = cpu_profile.prev_pc;
	/* PC may have extra bits when using 24 bit addressing, they need to be masked away as
	 * emulation itself does that too when PC value is used
	 */
	cpu_profile.prev_pc = pc = M68000_GetPC();
	if (ConfigureParams.System.bAddressSpace24) {
		cpu_profile.prev_pc &= 0xffffff;
	}
	if (unlikely(profile_loop.fp)) {
		if (pc < prev_pc) {
			if (pc == cpu_profile.loop_start && prev_pc == cpu_profile.loop_end) {
				cpu_profile.loop_count++;
			} else {
				cpu_profile.loop_start = pc;
				cpu_profile.loop_end = prev_pc;
				cpu_profile.loop_count = 1;
			}
		} else {
			if (pc > cpu_profile.loop_end) {
				log_last_loop();
				cpu_profile.loop_end = 0xffffffff;
				cpu_profile.loop_count = 0;
			}
		}
	}

	idx = address2index(prev_pc);
	assert(idx <= cpu_profile.size);
	prev = cpu_profile.data + idx;

	if (likely(prev->count < MAX_CPU_PROFILE_VALUE)) {
		prev->count++;
	}

	cycles = CyclesGlobalClockCounter - cpu_profile.prev_cycles;
	cpu_profile.prev_cycles = CyclesGlobalClockCounter;

	if (likely(prev->cycles < MAX_CPU_PROFILE_VALUE - cycles)) {
		prev->cycles += cycles;
	} else {
		prev->cycles = MAX_CPU_PROFILE_VALUE;
	}

	/* only WinUAE CPU core provides cache information */
	i_hits = CpuInstruction.I_Cache_hit;
	d_hits = CpuInstruction.D_Cache_hit;
	i_misses = CpuInstruction.I_Cache_miss;
	d_misses = CpuInstruction.D_Cache_miss;

	/* reset cache stats after reading them (for the next instruction) */
	CpuInstruction.I_Cache_hit = 0;
	CpuInstruction.D_Cache_hit = 0;
	CpuInstruction.I_Cache_miss = 0;
	CpuInstruction.D_Cache_miss = 0;

	/* tracked for every address */
# if DEBUG_CACHE
	if (likely(prev->i_hits < MAX_CPU_PROFILE_VALUE - i_hits)) {
		prev->i_hits += i_hits;
	} else {
		prev->i_hits = MAX_CPU_PROFILE_VALUE;
	}
	if (likely(prev->d_misses < MAX_CPU_PROFILE_VALUE - d_misses)) {
		prev->d_misses += d_misses;
	} else {
		prev->d_misses = MAX_CPU_PROFILE_VALUE;
	}
# endif
	if (likely(prev->i_misses < MAX_CPU_PROFILE_VALUE - i_misses)) {
		prev->i_misses += i_misses;
	} else {
		prev->i_misses = MAX_CPU_PROFILE_VALUE;
	}
	if (likely(prev->d_hits < MAX_CPU_PROFILE_VALUE - d_hits)) {
		prev->d_hits += d_hits;
	} else {
		prev->d_hits = MAX_CPU_PROFILE_VALUE;
	}

	/* tracking for histogram, check for array overflows */
	if (!(i_hits || i_misses)) {
		cpu_profile.i_prefetched++;
	}
	if (unlikely(i_hits >= MAX_I_HITS)) {
		i_hits = warn_too_large("number of CPU instruction cache hits", i_hits, MAX_I_HITS, prev_pc, pc);
	}
	cpu_profile.i_hit_counts[i_hits]++;

	if (unlikely(i_misses >= MAX_I_MISSES)) {
		i_misses = warn_too_large("number of CPU instruction cache misses", i_misses, MAX_I_MISSES, prev_pc, pc);
	}
	cpu_profile.i_miss_counts[i_misses]++;

	if (unlikely(d_hits >= MAX_D_HITS)) {
		d_hits = warn_too_large("number of CPU data cache hits", d_hits, MAX_D_HITS, prev_pc, pc);
	}
	cpu_profile.d_hit_counts[d_hits]++;

	if (unlikely(d_misses >= MAX_D_MISSES)) {
		d_misses = warn_too_large("number of CPU data cache misses", d_misses, MAX_D_MISSES, prev_pc, pc);
	}
	cpu_profile.d_miss_counts[d_misses]++;

	if (cpu_callinfo.sites) {
		collect_calls(prev_pc, counters);
	}
	/* total counters are increased after caller info is processed,
	 * otherwise cost for the instruction calling the callee
	 * doesn't get accounted to caller (but callee).
	 */
	counters->count++;
	counters->cycles += cycles;
	counters->i_misses += i_misses;
	counters->d_hits += d_hits;

#if DEBUG
	if (unlikely(OpcodeFamily == 0)) {
		if (++cpu_warnings.opfamily <= MAX_SHOW_COUNT) {
			uint32_t nextpc;
			fputs("WARNING: instruction opcode family is zero (=i_ILLG) for instruction:\n", stderr);
			Disasm(stderr, prev_pc, &nextpc, 1);
			if (cpu_warnings.opfamily == MAX_SHOW_COUNT) {
				fprintf(stderr, "Further warnings will not be shown.\n");
			}
		}
	}
	/* catch too large (and negative) cycles for other than STOP instruction */
	if (unlikely(cycles > 512 && OpcodeFamily != i_STOP)) {
		warn_too_large("cycles", cycles, 512, prev_pc, pc);
	}
#endif
}


/**
 * Helper for accounting CPU profile area item.
 */
static void update_area_item(profile_area_t *area, uint32_t addr, cpu_profile_item_t *item)
{
	uint32_t cycles = item->cycles;
	uint32_t count = item->count;

	if (!count) {
		return;
	}
	area->counters.count += count;
	area->counters.cycles += cycles;
	area->counters.i_misses += item->i_misses;
	area->counters.d_hits += item->d_hits;

	if (cycles == MAX_CPU_PROFILE_VALUE) {
		area->overflow = true;
	}
	if (addr < area->lowest) {
		area->lowest = addr;
	}
	area->highest = addr;

	area->active++;
}

/**
 * Helper for collecting CPU profile area statistics.
 */
static uint32_t update_area(profile_area_t *area, uint32_t start, uint32_t end)
{
	cpu_profile_item_t *item;
	uint32_t addr;

	memset(area, 0, sizeof(profile_area_t));
	area->lowest = end;

	item = &(cpu_profile.data[start]);
	for (addr = start; addr < end; addr++, item++) {
		update_area_item(area, addr, item);
	}
	return addr;
}

/**
 * Helper for initializing CPU profile area sorting indexes.
 */
static uint32_t* index_area(profile_area_t *area, uint32_t *sort_arr)
{
	cpu_profile_item_t *item;
	uint32_t addr;

	item = &(cpu_profile.data[area->lowest]);
	for (addr = area->lowest; addr <= area->highest; addr++, item++) {
		if (item->count) {
			*sort_arr++ = addr;
		}
	}
	return sort_arr;
}

/**
 * Stop and process the CPU profiling data; collect stats and
 * prepare for more optimal sorting.
 */
void Profile_CpuStop(void)
{
	uint32_t *sort_arr, next;
	unsigned int size, stsize;
	int active;

	if (cpu_profile.processed || !cpu_profile.enabled) {
		return;
	}

	log_last_loop();
	if (profile_loop.fp) {
		fflush(profile_loop.fp);
	}

	/* user didn't change RAM or TOS size in the meanwhile? */
	size = stsize = (STRamEnd + CART_SIZE + TosSize) / 2;
	if (TTmemory && ConfigureParams.Memory.TTRamSize_KB) {
		size += ConfigureParams.Memory.TTRamSize_KB * 1024/2;
	}
	assert(cpu_profile.size == size);

	Profile_FinalizeCalls(M68000_GetPC(),
			      &(cpu_callinfo),
			      &(cpu_profile.all),
			      Symbols_GetByCpuAddress,
			      Symbols_GetBeforeCpuAddress);

	/* find lowest and highest addresses executed etc */
	next = update_area(&cpu_profile.ram, 0, STRamEnd/2);
	if (TosAddress < CART_START) {
		next = update_area(&cpu_profile.tos, next, (STRamEnd + TosSize)/2);
		next = update_area(&cpu_profile.rom, next, stsize);
	} else {
		next = update_area(&cpu_profile.rom, next, (STRamEnd + CART_SIZE)/2);
		next = update_area(&cpu_profile.tos, next, stsize);
	}
	next = update_area(&cpu_profile.ttram, next, size);
	assert(next == size);

#if DEBUG
	if (skip_assert) {
		skip_assert = false;
	} else
#endif
	{
#if DEBUG
		if (cpu_profile.all.count != cpu_profile.ttram.counters.count + cpu_profile.ram.counters.count + cpu_profile.tos.counters.count + cpu_profile.rom.counters.count) {
			fprintf(stderr, "ERROR, instruction count mismatch:\n\t%"PRIu64" != %"PRIu64" + %"PRIu64" + %"PRIu64" + %"PRIu64"?\n",
				cpu_profile.all.count, cpu_profile.ttram.counters.count, cpu_profile.ram.counters.count,
				cpu_profile.tos.counters.count, cpu_profile.rom.counters.count);
			fprintf(stderr, "If there was debugger invocation from profiling before this, try with profiler DEBUG define disabled!!!\n");
		}
#endif
		assert(cpu_profile.all.count == cpu_profile.ttram.counters.count + cpu_profile.ram.counters.count + cpu_profile.tos.counters.count + cpu_profile.rom.counters.count);
		assert(cpu_profile.all.cycles == cpu_profile.ttram.counters.cycles + cpu_profile.ram.counters.cycles + cpu_profile.tos.counters.cycles + cpu_profile.rom.counters.cycles);
		assert(cpu_profile.all.i_misses == cpu_profile.ttram.counters.i_misses + cpu_profile.ram.counters.i_misses + cpu_profile.tos.counters.i_misses + cpu_profile.rom.counters.i_misses);
		assert(cpu_profile.all.d_hits == cpu_profile.ttram.counters.d_hits + cpu_profile.ram.counters.d_hits + cpu_profile.tos.counters.d_hits + cpu_profile.rom.counters.d_hits);
	}

	/* allocate address array for sorting */
	active = cpu_profile.ttram.active + cpu_profile.ram.active + cpu_profile.rom.active + cpu_profile.tos.active;
	sort_arr = calloc(active, sizeof(*sort_arr));

	if (!sort_arr) {
		perror("ERROR: allocating CPU profile address data");
		free(cpu_profile.data);
		cpu_profile.data = NULL;
		return;
	}
	fprintf(stderr, "Allocated CPU profile address buffer (%d KB).\n",
	       (int)sizeof(*sort_arr)*(active+512)/1024);
	cpu_profile.sort_arr = sort_arr;
	cpu_profile.active = active;

	/* and fill addresses for used instructions... */
	sort_arr = index_area(&cpu_profile.ram, sort_arr);
	sort_arr = index_area(&cpu_profile.tos, sort_arr);
	sort_arr = index_area(&cpu_profile.rom, sort_arr);
	sort_arr = index_area(&cpu_profile.ttram, sort_arr);
	assert(sort_arr == cpu_profile.sort_arr + cpu_profile.active);
	//printf("%d/%d/%d\n", area->active, sort_arr-cpu_profile.sort_arr, active);

	Profile_CpuShowStats();
	cpu_profile.processed = true;
}

/**
 * Get pointers to CPU profile enabling and disasm address variables
 * for updating them (in parser).
 */
void Profile_CpuGetPointers(bool **enabled, uint32_t **disasm_addr)
{
	*disasm_addr = &cpu_profile.disasm_addr;
	*enabled = &cpu_profile.enabled;
}

/**
 * Get callinfo & symbol search pointers for stack walking.
 */
void Profile_CpuGetCallinfo(callinfo_t **callinfo, const char* (**get_caller)(uint32_t*),
			    const char* (**get_symbol)(uint32_t, symtype_t))
{
	*callinfo = &(cpu_callinfo);
	*get_caller = Symbols_GetBeforeCpuAddress;
	*get_symbol = Symbols_GetByCpuAddress;
}
