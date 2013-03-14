/*
 * Hatari - profilecpu.c
 * 
 * Copyright (C) 2010-2013 by Eero Tamminen
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * profilecpu.c - functions for profiling CPU and showing the results.
 */
const char Profilecpu_fileid[] = "Hatari profilecpu.c : " __DATE__ " " __TIME__;

#include <stdio.h>
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

/* if non-zero, output (more) warnings on syspicious cycle/instruction counts */
#define DEBUG 0

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
	Uint32 disasm_addr;   /* 'addresses' command start address */
	bool processed;	      /* true when data is already processed */
	bool enabled;         /* true when profiling enabled */
} cpu_profile;


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
void Profile_CpuShowStats(void)
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
 * Show CPU instructions which execution was profiled, in the address order,
 * starting from the given address.  Return next disassembly address.
 */
Uint32 Profile_CpuShowAddresses(Uint32 lower, Uint32 upper, FILE *out)
{
	int oldcols[DISASM_COLUMNS], newcols[DISASM_COLUMNS];
	unsigned int idx, show, shown;
	const char *symbol;
	cpu_profile_item_t *data;
	Uint32 size, end, active;
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

	fputs("# disassembly with profile data: <instructions percentage>% (<sum of instructions>, <sum of cycles>, <sum of i-cache misses)\n", out);

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
		/* NOTE: column setup works only with UAE disasm engine! */
		Disasm(out, addr, &nextpc, 1);
		shown++;
	}
	printf("Disassembled %d (of active %d) CPU addresses.\n", shown, active);

	/* restore disassembly columns */
	Disasm_SetColumns(oldcols);
	return nextpc;
}


/**
 * compare function for qsort() to sort CPU profile data by instruction cache misses.
 */
static int cmp_cpu_misses(const void *p1, const void *p2)
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
void Profile_CpuShowMisses(unsigned int show)
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
	qsort(sort_arr, active, sizeof(*sort_arr), cmp_cpu_misses);

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
void Profile_CpuShowCycles(unsigned int show)
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
	qsort(sort_arr, active, sizeof(*sort_arr), cmp_cpu_cycles);

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
void Profile_CpuShowCounts(unsigned int show, bool only_symbols)
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
	qsort(sort_arr, active, sizeof(*sort_arr), cmp_cpu_count);

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
	Profile_ShowCallers(fp, cpu_profile.sites, cpu_profile.callsite, addr2name);
}

/**
 * Save CPU profile information to given file.
 */
void Profile_CpuSave(FILE *out)
{
	Uint32 text;
	fputs("Field names:\tExecuted instructions, Used cycles, Instruction cache misses\n", out);
	/* (Python) pegexp that matches address and all describled fields from disassembly:
	 * $<hex>  :  <ASM>  <percentage>% (<count>, <cycles>, <misses>)
	 * $e5af38 :   rts           0.00% (12, 0, 12)
	 */
	fputs("Field regexp:\t^\\$([0-9a-f]+) :.*% \\((.*)\\)$\n", out);
	/* some information for interpreting the addresses */
	fprintf(out, "ROM_TOS:\t0x%06x-0x%06x\n", TosAddress, TosAddress + TosSize);
	text = DebugInfo_GetTEXT();
	if (text < TosAddress) {
		fprintf(out, "PROGRAM_TEXT:\t0x%06x-0x%06x\n", text, DebugInfo_GetTEXTEnd());
	}
	fprintf(out, "CARTRIDGE:\t0xfa0000-0xfc0000\n");
	Profile_CpuShowAddresses(0, 0xFC0000-2, out);
	Profile_CpuShowCallers(out);
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
	cpu_profile.sites = Profile_AllocCallerInfo("CPU", cpu_profile.sites, Symbols_CpuCount(), &(cpu_profile.callsite));

	memset(cpu_profile.miss_counts, 0, sizeof(cpu_profile.miss_counts));
	cpu_profile.prev_cycles = Cycles_GetCounter(CYCLES_COUNTER_CPU);
	cpu_profile.prev_pc = M68000_GetPC();

	cpu_profile.disasm_addr = 0;
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
		Profile_UpdateCaller(Symbols_GetCpuAddressIndex(pc),
				     cpu_profile.sites, cpu_profile.callsite,
				     pc, prev_pc, flag);
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

/**
 * Get pointers to CPU profile enabling and disasm address variables
 * for updating them (in parser).
 */
void Profile_CpuGetPointers(bool **enabled, Uint32 **disasm_addr)
{
	*disasm_addr = &cpu_profile.disasm_addr;
	*enabled = &cpu_profile.enabled;
}
