/*
 * Hatari - profiledsp.c
 * 
 * Copyright (C) 2010-2015 by Eero Tamminen
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * profiledsp.c - functions for profiling DSP and showing the results.
 */
const char Profiledsp_fileid[] = "Hatari profiledsp.c : " __DATE__ " " __TIME__;

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include "main.h"
#include "configuration.h"
#include "clocks_timings.h"
#include "dsp.h"
#include "profile.h"
#include "profile_priv.h"
#include "symbols.h"
/* for VBL info */
#include "screen.h"
#include "video.h"

static callinfo_t dsp_callinfo;

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
	profile_area_t ram;   /* statistics for whole memory */
	Uint16 *sort_arr;     /* data indexes used for sorting */
	Uint16 prev_pc;       /* previous PC for which the cycles are for */
	Uint16 loop_start;    /* address of last loop start */
	Uint16 loop_end;      /* address of last loop end */
	Uint32 loop_count;    /* how many times it was looped */
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
	if (dsp_profile.ram.counters.count) {
		*percentage = 100.0*(*count)/dsp_profile.ram.counters.count;
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
	fprintf(stderr, "- executed instructions:\n  %"PRIu64"\n",
		area->counters.count);
	/* indicates either instruction(s) that address different memory areas
	 * (they can have different access costs), or more significantly,
	 * DSP code that has changed during profiling.
	 */
	fprintf(stderr, "- sum of per instruction cycle changes\n"
		"  (can indicate code change during profiling):\n  %"PRIu64"\n",
		area->counters.cycles_diffs);

	fprintf(stderr, "- used cycles:\n  %"PRIu64"\n",
		area->counters.cycles);
	if (area->overflow) {
		fprintf(stderr, "  *** COUNTERS OVERFLOW! ***\n");
	}
	fprintf(stderr, "\n= %.5fs\n", (double)(area->counters.cycles) / MachineClocks.DSP_Freq);
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
		percentage = 100.0*count/dsp_profile.ram.counters.cycles;
		printf("0x%04x\t%5.2f%%\t%"PRIu64"%s\n", addr, percentage, count,
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
			percentage = 100.0*count/dsp_profile.ram.counters.count;
			printf("0x%04x\t%5.2f%%\t%"PRIu64"%s\n",
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
		percentage = 100.0*count/dsp_profile.ram.counters.count;
		printf("0x%04x\t%.2f%%\t%"PRIu64"\t%s%s\n",
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
	Profile_ShowCallers(fp, dsp_callinfo.sites, dsp_callinfo.site, addr2name);
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

	Profile_FreeCallinfo(&(dsp_callinfo));
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
	/* zero everything */
	memset(&dsp_profile, 0, sizeof(dsp_profile));

	dsp_profile.data = calloc(DSP_PROFILE_ARR_SIZE, sizeof(*dsp_profile.data));
	if (!dsp_profile.data) {
		perror("ERROR, new DSP profile buffer alloc failed");
		return false;
	}
	printf("Allocated DSP profile buffer (%d KB).\n",
	       (int)sizeof(*dsp_profile.data)*DSP_PROFILE_ARR_SIZE/1024);

	Profile_AllocCallinfo(&(dsp_callinfo), Symbols_DspCount(), "DSP");

	item = dsp_profile.data;
	for (i = 0; i < DSP_PROFILE_ARR_SIZE; i++, item++) {
		item->min_cycle = 0xFFFF;
	}
	dsp_profile.prev_pc = DSP_GetPC();

	dsp_profile.loop_start = 0xFFFF;
	dsp_profile.loop_end = 0xFFFF;
	dsp_profile.loop_count = 0;
	Profile_LoopReset();

	dsp_profile.disasm_addr = 0;
	dsp_profile.processed = false;
	dsp_profile.enabled = true;
	return dsp_profile.enabled;
}

/* return true if pc is next instruction for previous pc */
static bool is_prev_instr(Uint16 prev_pc, Uint16 pc)
{
	/* just moved to next instruction (1-2 words)? */
	if (prev_pc < pc && (pc - prev_pc) <= 4) {
		return true;
	}
	return false;
}

/* return branch type based on caller instruction type */
static calltype_t dsp_opcode_type(Uint16 prev_pc, Uint16 pc)
{
	const char *dummy;
	Uint32 opcode;

	/* 24-bit instruction opcode */
	opcode = DSP_ReadMemory(prev_pc, 'P', &dummy) & 0xFFFFFF;

	/* subroutine returns */
	if (opcode == 0xC) {	/* (just) RTS */
		return CALL_SUBRETURN;
	}
	/* unconditional subroutine calls */
	if ((opcode & 0xFFF000) == 0xD0000 ||	/* JSR   00001101 0000aaaa aaaaaaaa */
	    (opcode & 0xFFC0FF) == 0xBC080) {	/* JSR   00001011 11MMMRRR 10000000 */
		return CALL_SUBROUTINE;
	}
	/* conditional subroutine calls */
	if ((opcode & 0xFF0000) == 0xF0000 ||	/* JSCC  00001111 CCCCaaaa aaaaaaaa */
	    (opcode & 0xFFC0F0) == 0xBC0A0 ||	/* JSCC  00001011 11MMMRRR 1010CCCC */
	    (opcode & 0xFFC0A0) == 0xB4080 ||	/* JSCLR 00001011 01MMMRRR 1S0bbbbb */
	    (opcode & 0xFFC0A0) == 0xB0080 ||	/* JSCLR 00001011 00aaaaaa 1S0bbbbb */
	    (opcode & 0xFFC0A0) == 0xB8080 ||	/* JSCLR 00001011 10pppppp 1S0bbbbb */
	    (opcode & 0xFFC0E0) == 0xBC000 ||	/* JSCLR 00001011 11DDDDDD 000bbbbb */
	    (opcode & 0xFFC0A0) == 0xB40A0 ||	/* JSSET 00001011 01MMMRRR 1S1bbbbb */
	    (opcode & 0xFFC0A0) == 0xB00A0 ||	/* JSSET 00001011 00aaaaaa 1S1bbbbb */
	    (opcode & 0xFFC0A0) == 0xB80A0 ||	/* JSSET 00001011 10pppppp 1S1bbbbb */
	    (opcode & 0xFFC0E0) == 0xBC020) {	/* JSSET 00001011 11DDDDDD 001bbbbb */
		/* hopefully fairly safe heuristic:
		 * if previously executed instruction
		 * was one before current one, no
		 * subroutine call was made to next
		 * instruction, the condition just
		 * wasn't met.
		 */
		if (is_prev_instr(prev_pc, pc)) {
			return CALL_NEXT;
		}
		return CALL_SUBROUTINE;
	}
	/* exception handler returns */
	if (opcode == 0x4) {	/* (just) RTI */
		return CALL_EXCRETURN;
	}

	/* Besides CALL_UNKNOWN, rest isn't used by subroutine call
	 * cost collection.  However, it's useful info when debugging
	 * code or reading full callgraphs (because optimized code uses
	 * also jumps/branches for subroutine calls).
	 */

	/* TODO: exception invocation.
	 * Could be detected by PC going through low interrupt vector adresses,
	 * but fast-calls using JSR/RTS would need separate handling.
	 */
	if (0) {	/* TODO */
		return CALL_EXCEPTION;
	}
	/* branches */
	if ((opcode & 0xFFF000) == 0xC0000 ||	/* JMP  00001100 0000aaaa aaaaaaaa */
	    (opcode & 0xFFC0FF) == 0xAC080 ||	/* JMP  00001010 11MMMRRR 10000000 */
	    (opcode & 0xFF0000) == 0xE0000 ||	/* JCC  00001110 CCCCaaaa aaaaaaaa */
	    (opcode & 0xFFC0F0) == 0xAC0A0 ||	/* JCC  00001010 11MMMRRR 1010CCCC */
	    (opcode & 0xFFC0A0) == 0xA8080 ||	/* JCLR 00001010 10pppppp 1S0bbbbb */
	    (opcode & 0xFFC0A0) == 0xA4080 ||	/* JCLR 00001010 01MMMRRR 1S0bbbbb */
	    (opcode & 0xFFC0A0) == 0xA0080 ||	/* JCLR 00001010 00aaaaaa 1S0bbbbb */
	    (opcode & 0xFFC0E0) == 0xAC000 ||	/* JCLR 00001010 11dddddd 000bbbbb */
	    (opcode & 0xFFC0A0) == 0xA80A0 ||	/* JSET 00001010 10pppppp 1S1bbbbb */
	    (opcode & 0xFFC0A0) == 0xA40A0 ||	/* JSET 00001010 01MMMRRR 1S1bbbbb */
	    (opcode & 0xFFC0A0) == 0xA00A0 ||	/* JSET 00001010 00aaaaaa 1S1bbbbb */
	    (opcode & 0xFFC0E0) == 0xAC020 ||	/* JSET 00001010 11dddddd 001bbbbb */
	    (opcode & 0xFF00F0) == 0x600A0 ||	/* REP  00000110 iiiiiiii 1010hhhh */
	    (opcode & 0xFFC0FF) == 0x6C020 ||	/* REP  00000110 11dddddd 00100000 */
	    (opcode & 0xFFC0BF) == 0x64020 ||	/* REP  00000110 01MMMRRR 0s100000 */
	    (opcode & 0xFFC0BF) == 0x60020 ||	/* REP  00000110 00aaaaaa 0s100000 */
	    (opcode & 0xFF00F0) == 0x60080 ||	/* DO/ENDO 00000110 iiiiiiii 1000hhhh */
	    (opcode & 0xFFC0FF) == 0x6C000 ||	/* DO/ENDO 00000110 11DDDDDD 00000000 */
	    (opcode & 0xFFC0BF) == 0x64000 ||	/* DO/ENDO 00000110 01MMMRRR 0S000000 */
	    (opcode & 0xFFC0BF) == 0x60000) {	/* DO/ENDO 00000110 00aaaaaa 0S000000 */
		return CALL_BRANCH;
	}
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
static void collect_calls(Uint16 pc, counters_t *counters)
{
	calltype_t flag;
	Uint16 prev_pc;
	Uint32 caller_pc;
	int idx;

	prev_pc = dsp_callinfo.prev_pc;
	dsp_callinfo.prev_pc = pc;
	caller_pc = PC_UNDEFINED;

	/* address is return address for last subroutine call? */
	if (unlikely(pc == dsp_callinfo.return_pc) && likely(dsp_callinfo.depth)) {

		flag = dsp_opcode_type(prev_pc, pc);
		/* return address is entered either by subroutine return,
		 * or by returning from exception that interrupted
		 * the instruction at return address.
		 */
		if (likely(flag == CALL_SUBRETURN || flag == CALL_EXCRETURN)) {
			caller_pc = Profile_CallEnd(&dsp_callinfo, counters);
		}
	}

	/* address is one which we're tracking? */
	idx = Symbols_GetDspAddressIndex(pc);
	if (unlikely(idx >= 0)) {

		flag = dsp_opcode_type(prev_pc, pc);
		if (flag == CALL_SUBROUTINE) {
			dsp_callinfo.return_pc = DSP_GetNextPC(prev_pc);  /* slow! */
		} else if (caller_pc != PC_UNDEFINED) {
			/* returned from function, change return
			 * instruction address to address of
			 * what did the returned call.
			 */
			prev_pc = caller_pc;
			assert(is_prev_instr(prev_pc, pc));
			flag = CALL_NEXT;
		}
		Profile_CallStart(idx, &dsp_callinfo, prev_pc, flag, pc, counters);

	}
}

/**
 * log last loop info, if there's suitable data for one
 */
static void log_last_loop(void)
{
	unsigned len = dsp_profile.loop_end - dsp_profile.loop_start;
	if (dsp_profile.loop_count > 1 && (len < profile_loop.dsp_limit || !profile_loop.dsp_limit)) {
		fprintf(profile_loop.fp, "DSP %d 0x%04x %d %d\n", nVBLs,
			dsp_profile.loop_start, len, dsp_profile.loop_count);
		fflush(profile_loop.fp);
	}
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
	counters_t *counters;

	prev_pc = dsp_profile.prev_pc;
	dsp_profile.prev_pc = pc = DSP_GetPC();

	if (unlikely(profile_loop.fp)) {
		if (pc < prev_pc) {
			if (pc == dsp_profile.loop_start && prev_pc == dsp_profile.loop_end) {
				dsp_profile.loop_count++;
			} else {
				dsp_profile.loop_start = pc;
				dsp_profile.loop_end = prev_pc;
				dsp_profile.loop_count = 1;
			}
		} else {
			if (pc > dsp_profile.loop_end) {
				log_last_loop();
				dsp_profile.loop_end = 0xFFFF;
				dsp_profile.loop_count = 0;
			}
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

	counters = &(dsp_profile.ram.counters);
	if (dsp_callinfo.sites) {
		collect_calls(prev_pc, counters);
	}
	/* counters are increased after caller info is processed,
	 * otherwise cost for the instruction calling the callee
	 * doesn't get accounted to caller (but callee).
	 */
	counters->cycles += cycles;
	counters->count++;
}

/**
 * Helper for collecting DSP profile area statistics.
 */
static void update_area_item(profile_area_t *area, Uint16 addr, dsp_profile_item_t *item)
{
	Uint64 cycles = item->cycles;
	Uint64 count = item->count;
	Uint16 diff;

	if (!count) {
		return;
	}
	if (cycles == MAX_DSP_PROFILE_VALUE) {
		area->overflow = true;
	}
	if (item->max_cycle) {
		diff = item->max_cycle - item->min_cycle;
	} else {
		diff = 0;
	}

	area->counters.count += count;
	area->counters.cycles += cycles;
	area->counters.cycles_diffs += diff;

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
	Uint32 addr;

	if (dsp_profile.processed || !dsp_profile.enabled) {
		return;
	}

	log_last_loop();
	if (profile_loop.fp) {
		fflush(profile_loop.fp);
	}

	Profile_FinalizeCalls(&(dsp_callinfo), &(dsp_profile.ram.counters), Symbols_GetByDspAddress);

	/* find lowest and highest  addresses executed */
	area = &dsp_profile.ram;
	memset(area, 0, sizeof(profile_area_t));
	area->lowest = DSP_PROFILE_ARR_SIZE;

	item = dsp_profile.data;
	for (addr = 0; addr < DSP_PROFILE_ARR_SIZE; addr++, item++) {
		update_area_item(area, addr, item);
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
	item = &(dsp_profile.data[area->lowest]);
	for (addr = area->lowest; addr <= area->highest; addr++, item++) {
		if (item->count) {
			*sort_arr++ = addr;
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

/**
 * Get callinfo & symbol search pointers for stack walking.
 */
void Profile_DspGetCallinfo(callinfo_t **callinfo, const char* (**get_symbol)(Uint32))
{
	*callinfo = &(dsp_callinfo);
	*get_symbol = Symbols_GetByDspAddress;
}
