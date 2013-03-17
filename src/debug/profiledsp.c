/*
 * Hatari - profiledsp.c
 * 
 * Copyright (C) 2010-2013 by Eero Tamminen
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * profiledsp.c - functions for profiling DSP and showing the results.
 */
const char Profiledsp_fileid[] = "Hatari profiledsp.c : " __DATE__ " " __TIME__;

#include <stdio.h>
#include "main.h"
#include "configuration.h"
#include "clocks_timings.h"
#include "dsp.h"
#include "profile.h"
#include "profile_priv.h"
#include "symbols.h"

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
	int sites;            /* number of symbol callsites */
	callee_t *callsite;   /* symbol specific caller information */
	Uint16 *sort_arr;     /* data indexes used for sorting */
	Uint16 prev_pc;       /* previous PC for which the cycles are for */
	Uint32 disasm_addr;   /* 'dspaddresses' command start address */
	bool processed;	      /* true when data is already processed */
	bool enabled;         /* true when profiling enabled */
} dsp_profile;


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
void Profile_DspShowStats(void)
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
 * Show DSP instructions which execution was profiled, in the address order,
 * starting from the given address.  Return next disassembly address.
 */
Uint16 Profile_DspShowAddresses(Uint32 addr, Uint32 upper, FILE *out)
{
	int show, shown, active;
	dsp_profile_item_t *data;
	Uint16 nextpc;
	Uint32 end;
	const char *symbol;

	data = dsp_profile.data;
	if (!data) {
		fprintf(stderr, "ERROR: no DSP profiling data available!\n");
		return 0;
	}

	end = DSP_PROFILE_ARR_SIZE;
	active = dsp_profile.ram.active;
	show = ConfigureParams.Debugger.nDisasmLines;
	if (upper) {
		if (upper < end) {
			end = upper;
		}
		show = active;
	} else {
		show = ConfigureParams.Debugger.nDisasmLines;
		if (!show || show > active) {
			show = active;
		}
	}

	fputs("# disassembly with profile data: <instructions percentage>% (<sum of instructions>, <sum of cycles>, <max cycle difference>)\n", out);

	nextpc = 0;
	for (shown = 0; shown < show && addr < end; addr++) {
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
	printf("Disassembled %d (of active %d) DSP addresses.\n", shown, active);
	return nextpc;
}

/**
 * compare function for qsort() to sort DSP profile data by descdending
 * address cycles counts.
 */
static int cmp_dsp_cycles(const void *p1, const void *p2)
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
void Profile_DspShowCycles(int show)
{
	int active;
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
	qsort(sort_arr, active, sizeof(*sort_arr), cmp_dsp_cycles);

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
static int cmp_dsp_count(const void *p1, const void *p2)
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
void Profile_DspShowCounts(int show, bool only_symbols)
{
	dsp_profile_item_t *data = dsp_profile.data;
	int symbols, matched, active;
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
	qsort(sort_arr, active, sizeof(*sort_arr), cmp_dsp_count);

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


static const char * addr2name(Uint32 addr, Uint64 *total)
{
	*total = dsp_profile.data[addr].count;
	return Symbols_GetByDspAddress(addr);
}

/**
 * Output DSP callers info to given file.
 */
void Profile_DspShowCallers(FILE *fp)
{
	Profile_ShowCallers(fp, dsp_profile.sites, dsp_profile.callsite, addr2name);
}

/**
 * Save DSP profile information to given file.
 */
void Profile_DspSave(FILE *out)
{
	/* Comma separated descriptions for the profile disassembly data fields.
	 * Instructions and cycles need to be first two fields!
	 */
	fputs("Field names:\tExecuted instructions, Used cycles, Largest cycle differences (= code changes during profiling)\n", out);
	/* (Python) pegexp that matches address and all describled fields from disassembly:
	 * <space>:<address> <opcodes> (<instr cycles>) <instr> <count>% (<count>, <cycles>)
	 * p:0202  0aa980 000200  (07 cyc)  jclr #0,x:$ffe9,p:$0200  0.00% (6, 42)
	 */
	fputs("Field regexp:\t^p:([0-9a-f]+) .*% \\((.*)\\)$\n", out);
	Profile_DspShowAddresses(0, DSP_PROFILE_ARR_SIZE, out);
	Profile_DspShowCallers(out);
}

/* ------------------ DSP profile control ----------------- */

/**
 * Initialize DSP profiling when necessary.  Return true if profiling.
 */
bool Profile_DspStart(void)
{
	dsp_profile_item_t *item;
	int i;

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

	dsp_profile.sites = Profile_AllocCallerInfo("DSP", dsp_profile.sites, Symbols_DspCount(), &(dsp_profile.callsite));

	dsp_profile.prev_pc = DSP_GetPC();

	dsp_profile.disasm_addr = 0;
	dsp_profile.processed = false;
	return dsp_profile.enabled;
}

/* return branch type based on caller instruction type */
static calltype_t dsp_opcode_type(Uint16 prev_pc, Uint16 pc)
{
	/* not supported (yet) */
	return CALL_UNDEFINED;
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
		int idx = Symbols_GetDspAddressIndex(pc);
		if (unlikely(idx >= 0 && idx < dsp_profile.sites)) {
			calltype_t flag = dsp_opcode_type(prev_pc, pc);
			Profile_UpdateCaller(dsp_profile.callsite + idx,
					     pc, prev_pc, flag);
		}
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

/**
 * Get pointers to DSP profile enabling and disasm address variables
 * for updating them (in parser).
 */
void Profile_DspGetPointers(bool **enabled, Uint32 **disasm_addr)
{
	*disasm_addr = &dsp_profile.disasm_addr;
	*enabled = &dsp_profile.enabled;
}
