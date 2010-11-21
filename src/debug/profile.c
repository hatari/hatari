/*
 * Hatari - profile.c
 * 
 * Copyright (C) 2010 by Eero Tamminen
 *
 * This file is distributed under the GNU Public License, version 2 or at
 * your option any later version. Read the file gpl.txt for details.
 *
 * profile.c - functions for profiling CPU and DSP and showing the results.
 */
const char Profile_fileid[] = "Hatari profile.c : " __DATE__ " " __TIME__;

#include <stdio.h>
#include "main.h"
#include "debug_priv.h"
#include "dsp.h"
#include "m68000.h"
#include "profile.h"
#include "stMemory.h"
#include "symbols.h"
#include "tos.h"

#define MAX_PROFILE_VALUE 0xFFFFFFFF

typedef struct {
	Uint32 count;	/* how many times this address is used */
	Uint32 cycles;	/* what address this is (for sorting) */
} profile_item_t;

typedef struct {
	unsigned long long all_cycles, all_count;
	Uint32 max_cycles, max_cycles_addr;
	Uint32 max_count, max_count_addr;
	Uint32 lowest, highest;	/* active address range within memory area */
	Uint32 active;          /* number of active addresses */
} profile_area_t;

static struct {
	unsigned long long all_cycles, all_count;
	Uint32 size;          /* number of allocated profile data items */
	profile_item_t *data; /* profile data items */
	profile_area_t ram;   /* normal RAM stats */
	profile_area_t rom;   /* cartridge ROM stats */
	profile_area_t tos;   /* ROM TOS stats */
	Uint32 active;        /* number of active data items in all areas */
	Uint32 *sort_arr;     /* data indexes used for sorting */
	bool enabled;         /* true when profiling enabled */
} cpu_profile;


#define DSP_PROFILE_ARR_SIZE 0x10000

static struct {
	profile_item_t *data; /* profile data */
	profile_area_t ram;   /* normal RAM stats */
	Uint16 *sort_arr;     /* data indexes used for sorting */
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
			fprintf(stderr, "WARNING: 'invalid' CPU PC profile instruction address 0x%x, skipping!\n", pc);
			/* extra entry at end reserved for invalid PC values */
			pc = STRamEnd + 0x20000 + TosSize;
		}
	}
	/* CPU instructions are at even addresses, save space by halving */
	return (pc >> 1);
}


/**
 * Get CPU cycles & count for given address.
 * Return true if data was available and non-zero, false otherwise.
 */
bool Profile_CpuAddressData(Uint32 addr, Uint32 *count, Uint32 *cycles)
{
	Uint32 idx;
	if (!cpu_profile.data) {
		return false;
	}
	idx = address2index(addr);
	*cycles = cpu_profile.data[idx].cycles;
	*count = cpu_profile.data[idx].count;
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
	fprintf(stderr, "- address with most cycles:\n  0x%06x, %d cycles (%.2f%% of all in area)\n",
		index2address(area->max_cycles_addr),
		area->max_cycles,
		(float)area->max_cycles/area->all_cycles*100);
	fprintf(stderr, "- address with most hits:\n  0x%06x, %d hits (%.2f%% of all in area)\n",
		index2address(area->max_count_addr),
		area->max_count,
		(float)area->max_count/area->all_count*100);
	if (area->max_cycles == MAX_PROFILE_VALUE) {
		fprintf(stderr, "- Counters OVERFLOW!\n");
	}
}


/**
 * show CPU area (RAM, ROM, TOS) specific statistics.
 */
void Profile_CpuShowStats(void)
{
	fprintf(stderr, "Normal RAM (0-0x%X):\n", STRamEnd);
	show_cpu_area_stats(&cpu_profile.ram);

	fprintf(stderr, "Cartridge ROM (0xFA0000-0xFC0000):\n");
	show_cpu_area_stats(&cpu_profile.rom);

	fprintf(stderr, "ROM TOS (0x%X-0x%X):\n", TosAddress, TosAddress+TosSize);
	show_cpu_area_stats(&cpu_profile.tos);
}


/**
 * compare function for qsort() to sort CPU profile data by descdending
 * address cycles counts.
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
void Profile_CpuShowCycles(unsigned int show)
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
 * compare function for qsort() to sort CPU profile data by descdending
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
void Profile_CpuShowCounts(unsigned int show, bool only_symbols)
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
	return cpu_profile.enabled;
}


/**
 * Update CPU cycle and count statistics for PC address.
 */
void Profile_CpuUpdate(void)
{
	Uint32 idx, opcode, cycles;
	
	idx = address2index(M68000_GetPC());

	if (likely(cpu_profile.data[idx].count < MAX_PROFILE_VALUE)) {
		cpu_profile.data[idx].count++;
	}
	
	opcode = get_iword_prefetch (0);
	cycles = (*cpufunctbl[opcode])(opcode) + nWaitStateCycles;
	
	if (likely(cpu_profile.data[idx].cycles < MAX_PROFILE_VALUE - cycles)) {
			cpu_profile.data[idx].cycles += cycles;
	}
}


/**
 * Helper for collecting profile area statistics.
 */
static void update_area(Uint32 i, profile_item_t *item, profile_area_t *area)
{
	Uint32 cycles, count = item->count;
	if (!count) {
		return;
	}

	area->all_count += count;
	if (count > area->max_count) {
		area->max_count = count;
		area->max_count_addr = i;
	}

	cycles = item->cycles;
	area->all_cycles += cycles;
	if (cycles > area->max_cycles) {
		area->max_cycles = cycles;
		area->max_cycles_addr = i;
	}

	if (i < area->lowest) {
		area->lowest = i;
	}
	area->highest = i;

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

	/* find lowest and highest  addresses executed */
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

	/* ...and fill addresses for used instructions... */
	item = cpu_profile.data;
	
	/* normal RAM */
	area = &cpu_profile.ram;
	item = cpu_profile.data + area->lowest;
	for (i = area->lowest; i <= area->highest; i++, item++) {
		if (item->count) {
			*sort_arr++ = i;
		}
	}

	/* Cartridge ROM */
	area = &cpu_profile.rom;
	item = cpu_profile.data + area->lowest;
	for (i = area->lowest; i <= area->highest; i++, item++) {
		if (item->count) {
			*sort_arr++ = i;
		}
	}

	/* TOS ROM */
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
 * Get DSP cycles & count for given address.
 * Return true if data was available and non-zero, false otherwise.
 */
bool Profile_DspAddressData(Uint16 addr, Uint32 *count, Uint32 *cycles)
{
	if (!dsp_profile.data) {
		return false;
	}
	*cycles = dsp_profile.data[addr].cycles;
	*count = dsp_profile.data[addr].count;
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
	fprintf(stderr, "- used cycles:\n  %llu\n",
		area->all_cycles);
	fprintf(stderr, "- address with most cycles:\n  0x%04x, %d cycles (%.2f%% of all)\n",
		area->max_cycles_addr,
		area->max_cycles,
		(float)area->max_cycles/area->all_cycles*100);
	fprintf(stderr, "- address with most hits:\n  0x%04x, %d hits (%.2f%% of all)\n",
		area->max_count_addr,
		area->max_count,
		(float)area->max_count/area->all_count*100);
	if (area->max_cycles == MAX_PROFILE_VALUE) {
		fprintf(stderr, "- Counters OVERFLOW!\n");
	}
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
void Profile_DspShowCycles(unsigned int show)
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
void Profile_DspShowCounts(unsigned int show, bool only_symbols)
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
	return dsp_profile.enabled;
}

/**
 * Update DSP cycle and count statistics for PC address.
 */
void Profile_DspUpdate(void)
{
	Uint16 pc, cycles;

	pc = DSP_GetPC();
	if (likely(dsp_profile.data[pc].count < MAX_PROFILE_VALUE)) {
		dsp_profile.data[pc].count++;
	}

	cycles = DSP_GetInstrCycles();
	if (likely(dsp_profile.data[pc].cycles < MAX_PROFILE_VALUE - cycles)) {
		dsp_profile.data[pc].cycles += cycles;
	}
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
		"on", "off", "counts", "cycles", "symbols", "stats"
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
	  "<on|off|counts|cycles|symbols|stats> [show count]\n"
	  "\ton & off enable and disable profiling.  Data is collected\n"
	  "\tuntil debugger is entered again after which you can view\n"
	  "\tstatistics about the data or view PC addresses that took\n"
	  "\tmost cycles or functions/symbols called most often.\n"
	  "\tYou can specify how many items are shown at most.";


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
	} else if (strcmp(psArgs[1], "symbols") == 0)	{
		if (bForDsp) {
			Profile_DspShowCounts(show, true);
		} else {
			Profile_CpuShowCounts(show, true);
		}
	} else {
		DebugUI_PrintCmdHelp(psArgs[0]);
		return false;
	}
	return true;
}
