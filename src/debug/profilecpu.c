/*
 * Hatari - profilecpu.c
 * 
 * Copyright (C) 2010-2015 by Eero Tamminen
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * profilecpu.c - functions for profiling CPU and showing the results.
 */
const char Profilecpu_fileid[] = "Hatari profilecpu.c : " __DATE__ " " __TIME__;

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
#include "profile.h"
#include "profile_priv.h"
#include "stMemory.h"
#include "symbols.h"
#include "tos.h"
#include "screen.h"
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

static callinfo_t cpu_callinfo;

#define MAX_CPU_PROFILE_VALUE 0xFFFFFFFF

typedef struct {
	Uint32 count;	/* how many times this address instrcution is executed */
	Uint32 cycles;	/* how many CPU cycles was taken at this address */
	Uint32 i_misses;  /* how many CPU instruction cache misses happened at this address */
	Uint32 d_hits;  /* how many CPU data cache hits happened at this address */
} cpu_profile_item_t;

#define MAX_I_HITS   8
#define MAX_I_MISSES 8
#define MAX_D_HITS   30
#define MAX_D_MISSES 20

static struct {
	counters_t all;       /* total counts for all areas */
	cpu_profile_item_t *data; /* profile data items */
	Uint32 size;          /* number of allocated profile data items */
	profile_area_t ttram; /* TT-RAM stats */
	profile_area_t ram;   /* normal RAM stats */
	profile_area_t rom;   /* cartridge ROM stats */
	profile_area_t tos;   /* ROM TOS stats */
	int active;           /* number of active data items in all areas */
	Uint32 *sort_arr;     /* data indexes used for sorting */
	int prev_family;      /* previous instruction opcode family */
	Uint64 prev_cycles;   /* previous instruction cycles counter */
	Uint32 prev_pc;       /* previous instruction address */
	Uint32 loop_start;    /* address of last loop start */
	Uint32 loop_end;      /* address of last loop end */
	Uint32 loop_count;    /* how many times it was looped */
	Uint32 disasm_addr;   /* 'addresses' command start address */
#if ENABLE_WINUAE_CPU
	Uint32 i_hit_counts[MAX_I_HITS];    /* I-cache hit counts */
	Uint32 d_hit_counts[MAX_D_HITS];    /* D-cache hit counts */
	Uint32 i_miss_counts[MAX_I_MISSES]; /* I-cache miss counts */
	Uint32 d_miss_counts[MAX_D_MISSES]; /* D-cache miss counts */
#endif
	bool processed;	      /* true when data is already processed */
	bool enabled;         /* true when profiling enabled */
} cpu_profile;

/* special hack for EmuTOS */
static Uint32 etos_switcher;


/* ------------------ CPU profile address mapping ----------------- */

/**
 * convert Atari memory address to sorting array profile data index.
 */
static inline Uint32 address2index(Uint32 pc)
{
	if (unlikely(pc & 1)) {
		fprintf(stderr, "WARNING: odd CPU profile instruction address 0x%x!\n", pc);
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
#if ENABLE_WINUAE_CPU
	} else if (TTmemory && pc >= TTRAM_START && pc < TTRAM_START + 1024*1024*(unsigned)ConfigureParams.Memory.nTTRamSize) {
		pc += STRamEnd + TosSize + CART_SIZE - TTRAM_START;
#endif
	} else {
		fprintf(stderr, "WARNING: 'invalid' CPU PC profile instruction address 0x%x!\n", pc);
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
static Uint32 index2address(Uint32 idx)
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
 * Get CPU cycles, count and count percentage for given address.
 * Return true if data was available and non-zero, false otherwise.
 */
bool Profile_CpuAddressData(Uint32 addr, float *percentage, Uint32 *count, Uint32 *cycles, Uint32 *i_misses, Uint32 *d_hits)
{
	Uint32 idx;
	if (!cpu_profile.data) {
		return false;
	}
	idx = address2index(addr);
	*i_misses = cpu_profile.data[idx].i_misses;
	*d_hits = cpu_profile.data[idx].d_hits;
	*cycles = cpu_profile.data[idx].cycles;
	*count = cpu_profile.data[idx].count;
	if (cpu_profile.all.count) {
		*percentage = 100.0*(*count)/cpu_profile.all.count;
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
	fprintf(stderr, "- executed instructions:\n  %"PRIu64" (%.2f%% of all)\n",
		area->counters.count,
		100.0 * area->counters.count / cpu_profile.all.count);
	/* CPU cache in use? */
	if (cpu_profile.all.i_misses) {
		fprintf(stderr, "- instruction cache misses:\n  %"PRIu64" (%.2f%% of all)\n",
			area->counters.i_misses,
			100.0 * area->counters.i_misses / cpu_profile.all.i_misses);
	}
	if (cpu_profile.all.d_hits) {
		fprintf(stderr, "- data cache hits:\n  %"PRIu64" (%.2f%% of all)\n",
			area->counters.d_hits,
			100.0 * area->counters.d_hits / cpu_profile.all.d_hits);
	}
	fprintf(stderr, "- used cycles:\n  %"PRIu64" (%.2f%% of all)\n  = %.5fs\n",
		area->counters.cycles,
		100.0 * area->counters.cycles / cpu_profile.all.cycles,
		(double)area->counters.cycles / MachineClocks.CPU_Freq);
	if (area->overflow) {
		fprintf(stderr, "  *** COUNTER OVERFLOW! ***\n");
	}
}


/**
 * show CPU area (RAM, ROM, TOS) specific statistics.
 */
void Profile_CpuShowStats(void)
{
	fprintf(stderr, "Normal RAM (0-0x%X):\n", STRamEnd);
	show_cpu_area_stats(&cpu_profile.ram);

	fprintf(stderr, "ROM TOS (0x%X-0x%X):\n", TosAddress, TosAddress + TosSize);
	show_cpu_area_stats(&cpu_profile.tos);

	fprintf(stderr, "Cartridge ROM (0x%X-%X):\n", CART_START, CART_END);
	show_cpu_area_stats(&cpu_profile.rom);

	if (TTmemory && ConfigureParams.Memory.nTTRamSize) {
		fprintf(stderr, "TT-RAM (0x%X-%X):\n", TTRAM_START, TTRAM_START + 1024*1024*ConfigureParams.Memory.nTTRamSize);
		show_cpu_area_stats(&cpu_profile.ttram);
	}

	fprintf(stderr, "\n= %.5fs\n",
		(double)cpu_profile.all.cycles / MachineClocks.CPU_Freq);
}

#if ENABLE_WINUAE_CPU
/**
 * show percentage histogram of given array items
 */
static void show_histogram(const char *title, int count, Uint32 *items)
{
	Uint64 maxval;
	Uint32 value;
	int i;

	fprintf(stderr, "\n%s, number of occurrencies:\n", title);
	maxval = 0;
	for (i = 0; i < count; i++) {
		maxval += items[i];
	}
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
		fprintf(stderr, "No instruction/data cache information.");
		return;
	}
	show_histogram("Instruction cache hits per instruction",
		       ARRAYSIZE(cpu_profile.i_hit_counts), cpu_profile.i_hit_counts);
	show_histogram("Instruction cache misses per instruction",
		       ARRAYSIZE(cpu_profile.i_miss_counts), cpu_profile.i_miss_counts);
	show_histogram("Data cache hits per instruction",
		       ARRAYSIZE(cpu_profile.d_hit_counts), cpu_profile.d_hit_counts);
	show_histogram("Data cache misses per instruction",
		       ARRAYSIZE(cpu_profile.d_miss_counts), cpu_profile.d_miss_counts);
}
#else
void Profile_CpuShowCaches(void) {
	fprintf(stderr, "Cache information is recorded only with WinUAE CPU.\n");
}
#endif

/**
 * Show CPU instructions which execution was profiled, in the address order,
 * starting from the given address.  Return next disassembly address.
 */
Uint32 Profile_CpuShowAddresses(Uint32 lower, Uint32 upper, FILE *out)
{
	int oldcols[DISASM_COLUMNS], newcols[DISASM_COLUMNS];
	int show, shown, active;
	const char *symbol;
	cpu_profile_item_t *data;
	Uint32 idx, end, size;
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
		show = active;
		if (end > size) {
			end = size;
		}
	} else {
		end = size;
		show = ConfigureParams.Debugger.nDisasmLines;
		if (!show || show > active) {
			show = active;
		}
	}

	/* get/change columns */
	Disasm_GetColumns(oldcols);
	Disasm_DisableColumn(DISASM_COLUMN_HEXDUMP, oldcols, newcols);
	Disasm_SetColumns(newcols);

	fputs("# disassembly with profile data: <instructions percentage>% (<sum of instructions>, <sum of cycles>, <sum of i-cache misses>, <sum of d-cache hits>)\n", out);

	nextpc = 0;
	idx = address2index(lower);
	for (shown = 0; shown < show && idx < end; idx++) {
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
		/* NOTE: column setup works only with 68kDisass disasm engine! */
		Disasm(out, addr, &nextpc, 1);
		shown++;
	}
	printf("Disassembled %d (of active %d) CPU addresses.\n", shown, active);

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

#if ENABLE_WINUAE_CPU
/**
 * compare function for qsort() to sort CPU profile data by instruction cache misses.
 */
static int cmp_cpu_i_misses(const void *p1, const void *p2)
{
	Uint32 count1 = cpu_profile.data[*(const Uint32*)p1].i_misses;
	Uint32 count2 = cpu_profile.data[*(const Uint32*)p2].i_misses;
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
	Uint32 *sort_arr, *end, addr, nextpc;
	cpu_profile_item_t *data = cpu_profile.data;
	float percentage;
	Uint32 count;

	if (!cpu_profile.all.i_misses) {
		fprintf(stderr, "No CPU instruction cache miss information available.\n");
		return;
	}

	active = cpu_profile.active;
	sort_arr = cpu_profile.sort_arr;
	qsort(sort_arr, active, sizeof(*sort_arr), cmp_cpu_i_misses);

	leave_instruction_column(oldcols);

	printf("addr:\t\ti-cache misses:\n");
	show = (show < active ? show : active);
	for (end = sort_arr + show; sort_arr < end; sort_arr++) {
		addr = index2address(*sort_arr);
		count = data[*sort_arr].i_misses;
		percentage = 100.0*count/cpu_profile.all.i_misses;
		printf("0x%06x\t%5.2f%%\t%d%s\t", addr, percentage, count,
		       count == MAX_CPU_PROFILE_VALUE ? " (OVERFLOW)" : "");
		Disasm(stdout, addr, &nextpc, 1);
	}
	printf("%d CPU addresses listed.\n", show);

	Disasm_SetColumns(oldcols);
}

/**
 * compare function for qsort() to sort CPU profile data by data cache hits.
 */
static int cmp_cpu_d_hits(const void *p1, const void *p2)
{
	Uint32 count1 = cpu_profile.data[*(const Uint32*)p1].d_hits;
	Uint32 count2 = cpu_profile.data[*(const Uint32*)p2].d_hits;
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
	Uint32 *sort_arr, *end, addr, nextpc;
	cpu_profile_item_t *data = cpu_profile.data;
	float percentage;
	Uint32 count;

	if (!cpu_profile.all.d_hits) {
		fprintf(stderr, "No CPU data cache hit information available.\n");
		return;
	}

	active = cpu_profile.active;
	sort_arr = cpu_profile.sort_arr;
	qsort(sort_arr, active, sizeof(*sort_arr), cmp_cpu_d_hits);

	leave_instruction_column(oldcols);

	printf("addr:\t\td-cache hits:\n");
	show = (show < active ? show : active);
	for (end = sort_arr + show; sort_arr < end; sort_arr++) {
		addr = index2address(*sort_arr);
		count = data[*sort_arr].d_hits;
		percentage = 100.0*count/cpu_profile.all.d_hits;
		printf("0x%06x\t%5.2f%%\t%d%s\t", addr, percentage, count,
		       count == MAX_CPU_PROFILE_VALUE ? " (OVERFLOW)" : "");
		Disasm(stdout, addr, &nextpc, 1);
	}
	printf("%d CPU addresses listed.\n", show);

	Disasm_SetColumns(oldcols);
}

#else
void Profile_CpuShowInstrMisses(int show) {
	fprintf(stderr, "Cache information is recorded only with WinUAE CPU.\n");
}
void Profile_CpuShowDataHits(int show) {
	fprintf(stderr, "Cache information is recorded only with WinUAE CPU.\n");
}
#endif


/**
 * compare function for qsort() to sort CPU profile data by cycles counts.
 */
static int cmp_cpu_cycles(const void *p1, const void *p2)
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
void Profile_CpuShowCycles(int show)
{
	int active;
	int oldcols[DISASM_COLUMNS];
	Uint32 *sort_arr, *end, addr, nextpc;
	cpu_profile_item_t *data = cpu_profile.data;
	float percentage;
	Uint32 count;

	if (!data) {
		fprintf(stderr, "ERROR: no CPU profiling data available!\n");
		return;
	}

	active = cpu_profile.active;
	sort_arr = cpu_profile.sort_arr;
	qsort(sort_arr, active, sizeof(*sort_arr), cmp_cpu_cycles);

	leave_instruction_column(oldcols);

	printf("addr:\t\tcycles:\n");
	show = (show < active ? show : active);
	for (end = sort_arr + show; sort_arr < end; sort_arr++) {
		addr = index2address(*sort_arr);
		count = data[*sort_arr].cycles;
		percentage = 100.0*count/cpu_profile.all.cycles;
		printf("0x%06x\t%5.2f%%\t%d%s\t", addr, percentage, count,
		       count == MAX_CPU_PROFILE_VALUE ? " (OVERFLOW)" : "");
		Disasm(stdout, addr, &nextpc, 1);
	}
	printf("%d CPU addresses listed.\n", show);

	Disasm_SetColumns(oldcols);
}

/**
 * compare function for qsort() to sort CPU profile data by descending
 * address access counts.
 */
static int cmp_cpu_count(const void *p1, const void *p2)
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
void Profile_CpuShowCounts(int show, bool only_symbols)
{
	cpu_profile_item_t *data = cpu_profile.data;
	int symbols, matched, active;
	int oldcols[DISASM_COLUMNS];
	Uint32 *sort_arr, *end, addr, nextpc;
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
	qsort(sort_arr, active, sizeof(*sort_arr), cmp_cpu_count);

	if (!only_symbols) {
		leave_instruction_column(oldcols);
		printf("addr:\t\tcount:\n");
		for (end = sort_arr + show; sort_arr < end; sort_arr++) {
			addr = index2address(*sort_arr);
			count = data[*sort_arr].count;
			percentage = 100.0*count/cpu_profile.all.count;
			printf("0x%06x\t%5.2f%%\t%d%s\t",
			       addr, percentage, count,
			       count == MAX_CPU_PROFILE_VALUE ? " (OVERFLOW)" : "");
			Disasm(stdout, addr, &nextpc, 1);
		}
		printf("%d CPU addresses listed.\n", show);
		Disasm_SetColumns(oldcols);
		return;
	}

	symbols = Symbols_CpuCount();
	if (!symbols) {
		fprintf(stderr, "ERROR: no CPU symbols loaded!\n");
		return;
	}
	matched = 0;	

	leave_instruction_column(oldcols);

	printf("addr:\t\tcount:\t\tsymbol:\n");
	for (end = sort_arr + active; sort_arr < end; sort_arr++) {

		addr = index2address(*sort_arr);
		name = Symbols_GetByCpuAddress(addr);
		if (!name) {
			continue;
		}
		count = data[*sort_arr].count;
		percentage = 100.0*count/cpu_profile.all.count;
		printf("0x%06x\t%5.2f%%\t%d\t%s%s\t",
		       addr, percentage, count, name,
		       count == MAX_CPU_PROFILE_VALUE ? " (OVERFLOW)" : "");
		Disasm(stdout, addr, &nextpc, 1);

		matched++;
		if (matched >= show || matched >= symbols) {
			break;
		}
	}
	printf("%d CPU symbols listed.\n", matched);

	Disasm_SetColumns(oldcols);
}


static const char * addr2name(Uint32 addr, Uint64 *total)
{
	Uint32 idx = address2index(addr);
	*total = cpu_profile.data[idx].count;
	return Symbols_GetByCpuAddress(addr);
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
	Uint32 text, end;
	fputs("Field names:\tExecuted instructions, Used cycles, Instruction cache misses, Data cache hits\n", out);
	/* (Python) regexp that matches address and all described fields from disassembly:
	 * $<hex>  :  <ASM>  <percentage>% (<count>, <cycles>, <i-misses>, <d-hits>)
	 * $e5af38 :   rts           0.00% (12, 0, 12, 0)
	 */
	fputs("Field regexp:\t^\\$([0-9a-f]+) :.*% \\((.*)\\)$\n", out);
	/* some information for interpreting the addresses */
	fprintf(out, "ST_RAM:\t\t0x%06x-0x%06x\n", 0, STRamEnd);
	end = TosAddress + TosSize;
	fprintf(out, "ROM_TOS:\t0x%06x-0x%06x\n", TosAddress, end);
	fprintf(out, "CARTRIDGE:\t0x%06x-0x%06x\n", CART_START, CART_END);
	text = DebugInfo_GetTEXT();
	if (text && (text < TosAddress || text >= TTRAM_START)) {
		fprintf(out, "PROGRAM_TEXT:\t0x%06x-0x%06x\n", text, DebugInfo_GetTEXTEnd());
	}
	if (TTmemory && ConfigureParams.Memory.nTTRamSize) {
		end = TTRAM_START + 1024*1024*ConfigureParams.Memory.nTTRamSize;
		fprintf(out, "TT_RAM:\t\t0x%08x-0x%08x\n", TTRAM_START, end);
	} else if (end < CART_END) {
		end = CART_END;
	}
	Profile_CpuShowAddresses(0, end-2, out);
	Profile_CpuShowCallers(out);
}

/* ------------------ CPU profile control ----------------- */

/**
 * Initialize CPU profiling when necessary.  Return true if profiling.
 */
bool Profile_CpuStart(void)
{
	int size;

	Profile_FreeCallinfo(&(cpu_callinfo));
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
	/* zero everything */
	memset(&cpu_profile, 0, sizeof(cpu_profile));

	/* Shouldn't change within same debug session */
	size = (STRamEnd + CART_SIZE + TosSize) / 2;
	if (TTmemory && ConfigureParams.Memory.nTTRamSize) {
		size += ConfigureParams.Memory.nTTRamSize * 1024*1024/2;
	}

	/* Add one entry for catching invalid PC values */
	cpu_profile.data = calloc(size + 1, sizeof(*cpu_profile.data));
	if (!cpu_profile.data) {
		perror("ERROR, new CPU profile buffer alloc failed");
		return false;
	}
	printf("Allocated CPU profile buffer (%d MB).\n",
	       (int)sizeof(*cpu_profile.data)*size/(1024*1024));
	cpu_profile.size = size;

	Profile_AllocCallinfo(&(cpu_callinfo), Symbols_CpuCount(), "CPU");

	/* special hack for EmuTOS */
	etos_switcher = PC_UNDEFINED;
	if (cpu_callinfo.sites && bIsEmuTOS &&
	    (!Symbols_GetCpuAddress(SYMTYPE_TEXT, "_switchto", &etos_switcher) || etos_switcher < TosAddress)) {
		etos_switcher = PC_UNDEFINED;
	}

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
static bool is_prev_instr(Uint32 prev_pc, Uint32 pc)
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
static calltype_t cpu_opcode_type(int family, Uint32 prev_pc, Uint32 pc)
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
 * If call tracking is enabled (there are symbols), collect
 * information about subroutine and other calls, and their costs.
 * 
 * Like with profile data, caller info checks need to be for previous
 * instruction, that's why "pc" argument for this function actually
 * needs to be previous PC.
 */
static void collect_calls(Uint32 pc, counters_t *counters)
{
	calltype_t flag;
	int idx, family;
	Uint32 prev_pc, caller_pc;

	family = cpu_profile.prev_family;
	cpu_profile.prev_family = OpcodeFamily;

	prev_pc = cpu_callinfo.prev_pc;
	cpu_callinfo.prev_pc = pc;
	caller_pc = PC_UNDEFINED;

	/* address is return address for last subroutine call? */
	if (unlikely(pc == cpu_callinfo.return_pc) && likely(cpu_callinfo.depth)) {

		flag = cpu_opcode_type(family, prev_pc, pc);
		/* previous address can be exception return (e.g. RTE) instead of RTS,
		 * if exception occurred right after returning from subroutine call.
		 */
		if (likely(flag == CALL_SUBRETURN || flag == CALL_EXCRETURN)) {
			caller_pc = Profile_CallEnd(&cpu_callinfo, counters);
		} else {
#if DEBUG
			/* although at return address, it didn't return yet,
			 * e.g. because there was a jsr or jump to return address
			 */
			Uint32 nextpc;
			fprintf(stderr, "WARNING: subroutine call returned 0x%x -> 0x%x, not through RTS!\n", prev_pc, pc);
			Disasm(stderr, prev_pc, &nextpc, 1);
#endif
		}
		/* next address might be another symbol, so need to fall through */
	}

	/* address is one which we're tracking? */
	idx = Symbols_GetCpuAddressIndex(pc);
	if (unlikely(idx >= 0)) {

		flag = cpu_opcode_type(family, prev_pc, pc);
		if (flag == CALL_SUBROUTINE || flag == CALL_EXCEPTION) {
			/* special HACK for for EmuTOS AES switcher which
			 * changes stack content to remove itself from call
			 * stack and uses RTS for subroutine *calls*, not
			 * for returning from them.
			 *
			 * It wouldn't be reliable to detect calls from it,
			 * so I'm making call *to* it show up as branch, to
			 * keep callstack depth correct.
			 */
			if (unlikely(pc == etos_switcher)) {
				flag = CALL_BRANCH;
			} else if (unlikely(prev_pc == PC_UNDEFINED)) {
				/* if first profiled instruction
				 * is subroutine call, it doesn't have
				 * valid prev_pc value stored
				 */
				cpu_callinfo.return_pc = PC_UNDEFINED;
				fprintf(stderr, "WARNING: previous PC for tracked address 0x%d is undefined!\n", pc);
#if DEBUG
				skip_assert = true;
				DebugUI(REASON_CPU_EXCEPTION);
#endif
			} else {
				/* slow! */
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

# if DEBUG || ENABLE_WINUAE_CPU
/**
 * Warning for values going out of expected range
 */
static Uint32 warn_too_large(const char *name, const int value, const int limit, const Uint32 prev_pc, const Uint32 pc)
{
	Uint32 nextpc;
	fprintf(stderr, "WARNING: unexpected (%d > %d) %s at 0x%x:\n", value, limit - 1, name, pc);
	Disasm(stderr, prev_pc, &nextpc, 1);
	Disasm(stderr, pc, &nextpc, 1);
#if DEBUG
	skip_assert = true;
	DebugUI(REASON_CPU_EXCEPTION);
#endif
	return limit - 1;
}
#endif

/**
 * Update CPU cycle and count statistics for PC address.
 *
 * This gets called after instruction has executed and PC
 * has advanced to next instruction.
 */
void Profile_CpuUpdate(void)
{
	counters_t *counters = &(cpu_profile.all);
	Uint32 pc, prev_pc, idx, cycles;
	cpu_profile_item_t *prev;
#if ENABLE_WINUAE_CPU
	Uint32 i_hits, d_hits, i_misses, d_misses;
#else
	const Uint32 i_misses = 0, d_hits = 0;
#endif

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

#if ENABLE_WINUAE_CPU
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
#endif

	if (cpu_callinfo.sites) {
		collect_calls(prev_pc, counters);
	}
	/* counters are increased after caller info is processed,
	 * otherwise cost for the instruction calling the callee
	 * doesn't get accounted to caller (but callee).
	 */
	counters->count++;
	counters->cycles += cycles;
	counters->i_misses += i_misses;
	counters->d_hits += d_hits;

#if DEBUG
	if (unlikely(OpcodeFamily == 0)) {
		Uint32 nextpc;
		fputs("WARNING: instruction opcode family is zero (=i_ILLG) for instruction:\n", stderr);
		Disasm(stderr, prev_pc, &nextpc, 1);
	}
	/* catch too large (and negative) cycles for other than STOP instruction */
	if (unlikely(cycles > 512 && OpcodeFamily != i_STOP)) {
		warn_too_large("cycles", cycles, 512, prev_pc, pc);
	}
# if !ENABLE_WINUAE_CPU
	{
		static Uint32 prev_cycles = 0, prev_pc2 = 0;
		if (unlikely(cycles == 0 && prev_cycles == 0)) {
			Uint32 nextpc;
			fputs("WARNING: Zero cycles for successive opcodes:\n", stderr);
			Disasm(stderr, prev_pc2, &nextpc, 1);
			Disasm(stderr, prev_pc, &nextpc, 1);
		}
		prev_cycles = cycles;
		prev_pc2 = prev_pc;
	}
# endif
#endif
}


/**
 * Helper for accounting CPU profile area item.
 */
static void update_area_item(profile_area_t *area, Uint32 addr, cpu_profile_item_t *item)
{
	Uint32 cycles = item->cycles;
	Uint32 count = item->count;

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
static Uint32 update_area(profile_area_t *area, Uint32 start, Uint32 end)
{
	cpu_profile_item_t *item;
	Uint32 addr;

	memset(area, 0, sizeof(profile_area_t));
	area->lowest = cpu_profile.size;

	item = &(cpu_profile.data[start]);
	for (addr = start; addr < end; addr++, item++) {
		update_area_item(area, addr, item);
	}
	return addr;
}

/**
 * Helper for initializing CPU profile area sorting indexes.
 */
static Uint32* index_area(profile_area_t *area, Uint32 *sort_arr)
{
	cpu_profile_item_t *item;
	Uint32 addr;

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
	Uint32 *sort_arr, next;
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
	if (TTmemory && ConfigureParams.Memory.nTTRamSize) {
		size += ConfigureParams.Memory.nTTRamSize * 1024*1024/2;
	}
	assert(cpu_profile.size == size);

	Profile_FinalizeCalls(&(cpu_callinfo), &(cpu_profile.all), Symbols_GetByCpuAddress);

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
	printf("Allocated CPU profile address buffer (%d KB).\n",
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
void Profile_CpuGetPointers(bool **enabled, Uint32 **disasm_addr)
{
	*disasm_addr = &cpu_profile.disasm_addr;
	*enabled = &cpu_profile.enabled;
}

/**
 * Get callinfo & symbol search pointers for stack walking.
 */
void Profile_CpuGetCallinfo(callinfo_t **callinfo, const char* (**get_symbol)(Uint32))
{
	*callinfo = &(cpu_callinfo);
	*get_symbol = Symbols_GetByCpuAddress;
}
