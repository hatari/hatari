/*
 * Hatari - profile.c
 *
 * Copyright (C) 2010-2023 by Eero Tamminen
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * profile.c - profile caller info handling and debugger parsing functions
 */
const char Profile_fileid[] = "Hatari profile.c";

#include <stdio.h>
#include <assert.h>
#include <inttypes.h>
#include "main.h"
#include "version.h"
#include "debugui.h"
#include "debug_priv.h"
#include "configuration.h"
#include "clocks_timings.h"
#include "evaluate.h"
#include "symbols.h"
#include "profile.h"
#include "profile_priv.h"
#include "m68000.h"
#include "dsp.h"

profile_loop_t profile_loop;

#define CALLER_SEPARATOR ','


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
	uint32_t calls1 = ((const caller_t*)c1)->calls;
	uint32_t calls2 = ((const caller_t*)c2)->calls;
	if (calls1 > calls2) {
		return -1;
	}
	if (calls1 < calls2) {
		return 1;
	}
	return 0;
}

/**
 * output caller counter information
 */
static bool output_counter_info(FILE *fp, counters_t *counter)
{
	if (!counter->count) {
		return false;
	}
	/* number of calls needs to be first and rest must be in the same order as
	 * they're in the profile disassembly (count of instructions, etc...).
	 */
	fprintf(fp, " %"PRIu64"/%"PRIu64"/%"PRIu64"",
		counter->calls, counter->count, counter->cycles);
	if (counter->i_misses) {
		/* these are only with specific WinUAE CPU core */
		fprintf(fp, "/%"PRIu64"/%"PRIu64"",
			counter->i_misses, counter->d_hits);
	}
	return true;
}

/**
 * output caller call counts, call type(s) and costs
 */
static void output_caller_info(FILE *fp, caller_t *info, uint32_t *typeaddr)
{
	int k, typecount;

	fprintf(fp, "0x%x = %d", info->addr, info->calls);
	if (info->flags) {	/* calltypes supported? */
		fputc(' ', fp);
		typecount = 0;
		for (k = 0; k < ARRAY_SIZE(flaginfo); k++) {
			if (info->flags & flaginfo[k].bit) {
				fputc(flaginfo[k].chr, fp);
				typecount++;
			}
		}
		if (typecount > 1) {
			*typeaddr = info->addr;
		}
	}
	if (output_counter_info(fp, &(info->all))) {
		output_counter_info(fp, &(info->own));
		if (info->calls != info->own.calls) {
			fprintf(stderr, "WARNING: mismatch between function 0x%x call count %d and own call cost %"PRIu64"!\n",
			       info->addr, info->calls, info->own.calls);
		}
	}
	fprintf(fp, "%c ", CALLER_SEPARATOR);
}

/**
 * Show collected CPU/DSP callee/caller information
 */
void Profile_ShowCallers(FILE *fp, int sites, callee_t *callsite, const char * (*addr2name)(uint32_t, uint64_t *))
{
	int i, j, countissues, countdiff;
	const char *name;
	caller_t *info;
	uint64_t total;
	uint32_t addr, typeaddr;

	/* legend */
	fputs("# <callee>: <caller1> = <calls> <types>[ <inclusive/totals>[ <exclusive/totals>]], <caller2> ..., <callee name>", fp);
	fputs("\n# types: ", fp);
	for (i = 0; i < ARRAY_SIZE(flaginfo); i++) {
		fprintf(fp, "%c = %s, ", flaginfo[i].chr, flaginfo[i].info);
	}
	fputs("\n# totals: calls/instructions/cycles/i-misses/d-hits\n", fp);

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
			if (!info->calls) {
				break;
			}
			total -= info->calls;
			output_caller_info(fp, info, &typeaddr);
		}
		/* Symbol name output here is used only for debugging
		 * profiler functionality, post-processor does not
		 * need it.
		 *
		 * Output them only if they do not include caller
		 * separator (','), because that would mess up
		 * post-processor profile data parsing.
		 *
		 * Skip also all mangled (C++) symbols as they could
		 * include that separator after demangling.
		 */
		if (name && strchr(name, CALLER_SEPARATOR) == NULL
		    && strncmp(name, "__Z", 3) != 0) {
			fprintf(fp, "%s", name);
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
			fprintf(stderr, "WARNING: different types of calls (at least) from 0x%x (to 0x%x),\n\t has its code changed during profiling?\n",
				typeaddr, callsite->addr);
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
 * add second counter values to first counters
 */
static void add_counter_costs(counters_t *dst, counters_t *src)
{
	dst->calls += src->calls;
	dst->count += src->count;
	dst->cycles += src->cycles;
	dst->i_misses += src->i_misses;
	dst->d_hits += src->d_hits;
}

/**
 * set first counter values to their difference from a reference value
 */
static void set_counter_diff(counters_t *dst, counters_t *ref)
{
	dst->calls = ref->calls - dst->calls;
	dst->count = ref->count - dst->count;
	dst->cycles = ref->cycles - dst->cycles;
	dst->i_misses = ref->i_misses - dst->i_misses;
	dst->d_hits = ref->d_hits - dst->d_hits;
}

/**
 * add called (callee) function costs to caller information
 */
static void add_callee_cost(callee_t *callsite, callstack_t *stack)
{
	caller_t *info = callsite->callers;
	counters_t owncost;
	int i;

	for (i = 0; i < callsite->count; i++, info++) {
		if (info->addr == stack->caller_addr) {
			/* own cost for callee is its child (out) costs
			 * deducted from full (all) costs
			 */
			owncost = stack->out;
			set_counter_diff(&owncost, &(stack->all));
			add_counter_costs(&(info->own), &owncost);
			add_counter_costs(&(info->all), &(stack->all));
			return;
		}
	}
	/* cost is only added for updated callers,
	 * so they should always exist
	 */
	fprintf(stderr, "ERROR: trying to add costs to non-existing 0x%x caller of 0x%x!\n",
		stack->caller_addr, callsite->addr);
	assert(0);
}

/**
 * Add new caller or updated earlier caller stats for call site
 */
static void add_caller(callee_t *callsite, uint32_t pc, uint32_t prev_pc, calltype_t flag)
{
	int i, count, oldcount;
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
	oldcount = 0;
	for (;;) {
		for (i = oldcount; i < count; i++, info++) {
			if (info->addr == prev_pc) {
				/* increment caller */
				info->flags |= flag;
				info->calls++;
				return;
			}
			if (!info->addr) {
				/* add caller to empty slot */
				info->addr = prev_pc;
				info->flags |= flag;
				info->calls = 1;
				return;
			}
		}
		oldcount = count;
		/* not enough, double caller slots */
		count *= 2;
		info = realloc(callsite->callers, count * sizeof(*info));
		if (!info) {
			fprintf(stderr, "ERROR: caller info alloc failed!\n");
			return;
		}
		callsite->callers = info;
		callsite->count = count;
		info = info + oldcount;
		memset(info, 0, oldcount * sizeof(*info));
	}
}

/**
 * Add information about the called symbol, and if it was a subroutine
 * call, add it to stack of functions which total costs are tracked.
 * callinfo.return_pc needs to be set before invoking this if the call
 * is of type CALL_SUBROUTINE.
 */
void Profile_CallStart(int idx, callinfo_t *callinfo, uint32_t prev_pc, calltype_t flag, uint32_t pc, counters_t *totalcost)
{
	callstack_t *stack;
	int count;

	if (unlikely(idx >= callinfo->sites)) {
		fprintf(stderr, "ERROR: number of symbols increased during profiling (%d > %d)!\n", idx, callinfo->sites);
		return;
	}

	add_caller(callinfo->site + idx, pc, prev_pc, flag);

	/* subroutine/exception call i.e. one which will return? */
	if (flag != CALL_SUBROUTINE && flag != CALL_EXCEPTION) {
		/* no, some other call type */
		return;
	}
	/* yes, add it to call stack */

	if (unlikely(!callinfo->count)) {
		/* initial stack alloc, can be a bit larger */
		count = 8;
		stack = calloc(count, sizeof(*stack));
		if (!stack) {
			fputs("ERROR: callstack alloc failed!\n", stderr);
			return;
		}
		callinfo->stack = stack;
		callinfo->count = count;

	} else if (unlikely(callinfo->depth+1 >= callinfo->count)) {
		/* need to alloc more stack space for new call? */
		count = callinfo->count * 2;
		stack = realloc(callinfo->stack, count * sizeof(*stack));
		if (!stack) {
			fputs("ERROR: callstack alloc failed!\n", stderr);
			return;
		}
		memset(stack + callinfo->count, 0, callinfo->count * sizeof(*stack));
		callinfo->stack = stack;
		callinfo->count = count;
	}

	/* only first instruction can be undefined */
	assert(callinfo->return_pc != PC_UNDEFINED || !callinfo->depth);

	/* called function */
	stack = &(callinfo->stack[callinfo->depth++]);

	/* store current running totals & zero subcall costs */
	stack->all = *totalcost;
	memset(&(stack->out), 0, sizeof(stack->out));

	/* set subroutine call information */
	stack->ret_addr = callinfo->return_pc;
	stack->callee_idx = idx;
	stack->caller_addr = prev_pc;
	stack->callee_addr = pc;

	/* record call to this into costs... */
	totalcost->calls++;
}

/**
 * If it really was subcall (function) return, store returned function
 * costs and update callinfo->return_pc value.  Return address of
 * the instruction which did the returned call.
 */
uint32_t Profile_CallEnd(callinfo_t *callinfo, counters_t *totalcost)
{
	callstack_t *stack;

	assert(callinfo->depth);

	/* remove call info from stack */
	callinfo->depth--;

	/* callinfo->depth points now to to-be removed item */
	stack = &(callinfo->stack[callinfo->depth]);

	if (unlikely(stack->caller_addr == PC_UNDEFINED)) {
		/* return address can be undefined only for
		 * first profiled instruction, i.e. only for
		 * function at top of stack
		 */
		assert(!callinfo->depth);
	} else {
		/* full cost is original global cost (in ->all)
		 * deducted from current global (total) cost
		 */
		set_counter_diff(&(stack->all), totalcost);
		add_callee_cost(callinfo->site + stack->callee_idx, stack);
	}

	/* if current function had a parent:
	 * - start tracking that
	 * - add full cost of current function to parent's outside costs
	 */
	if (callinfo->depth) {
		callstack_t *parent = stack - 1;
		callinfo->return_pc = parent->ret_addr;
		add_counter_costs(&(parent->out), &(stack->all));
	} else {
		callinfo->return_pc = PC_UNDEFINED;
	}

	/* where the returned function was called from */
	return stack->caller_addr;
}


/**
 * Add costs to all functions still in call stack and print their names
 *
 * Diagram of variables involved in 2 functions deep call stack:
 *
 *   (caller_addr 1)  bsr symbol1  -1->  symbol1     (callee_addr 1)
 *      (ret_addr 1)  <instr>     <-.    ...
 *                                  |    bsr symbol2 (caller_addr 2)  -2->  symbol2 (callee_addr 2)
 *                                  1    <instr>     (ret_addr 2)    <-.    ...
 *                                  |    ...                           2    ... (PC)
 *                                  '-   rts                           '-   rts
 *
 * When one wants to match callee_addr (= symbol) to caller_addr (= which
 * place in that function called further functions), it's best to traverse
 * stack from last item to top, as these items are in following call stack
 * items, not in same one.
 */
void Profile_FinalizeCalls(uint32_t pc, callinfo_t *callinfo, counters_t *totalcost,
			   const char* (*get_symbol)(uint32_t, symtype_t),
			   const char* (*get_caller)(uint32_t*))
{
	const char *sym, *caller;
	uint32_t sym_addr, caller_addr;
	int i, lines, offset;
	bool dots;
	char sign;

	if (!callinfo->depth) {
		return;
	}
	fprintf(stderr, "Finalizing costs for %d non-returned functions:\n", callinfo->depth);

	i = 0;
	dots = false;
	caller_addr = pc;
	lines = ConfigureParams.Debugger.nBacktraceLines;
	while (callinfo->depth > 0) {

		/* finalize & decrease callinfo->depth */
		Profile_CallEnd(callinfo, totalcost);
		if (++i > lines && lines > 0) {
			continue;
		}

		/* Skip showing middle part of a long callstack as messed
		 * callstacks could be thousands of frames deep...
		 */
		if (i >= 32 && callinfo->depth > 32) {
			if (!dots) {
				fprintf(stderr, "- ...\n");
				dots = true;
			}
		} else {
			sym_addr = callinfo->stack[callinfo->depth].callee_addr;
			sym = get_symbol(sym_addr, SYMTYPE_CODE);

			if (sym) {
				offset = caller_addr - sym_addr;
				sign = offset >= 0 ? '+' : '-';
				fprintf(stderr, "- %d. 0x%06x: %s %c0x%x",
					i, caller_addr, sym, sign, abs(offset));
			} else {
				fprintf(stderr, "- %d. 0x%06x", i, caller_addr);
			}

			sym_addr = caller_addr;
			caller = get_caller(&sym_addr);
			if (caller && caller != sym) {
				offset = caller_addr - sym_addr;
				fprintf(stderr, " (%s +0x%x)\n",
					caller, abs(offset));
			} else {
				fprintf(stderr, "\n");
			}
		}
		caller_addr = callinfo->stack[callinfo->depth].caller_addr;
	}
}

/**
 * Show current profile call stack, up to configured max backtrace depth
 */
static void Profile_ShowStack(bool forDsp)
{
	const char *sym, *caller;
	const char *(*get_caller)(uint32_t*);
	const char *(*get_symbol)(uint32_t, symtype_t);
	uint32_t sym_addr, caller_addr;
	int i, offset, depth, top;
	callinfo_t *callinfo;

	if (forDsp) {
		Profile_DspGetCallinfo(&callinfo, &get_caller, &get_symbol);
		caller_addr = DSP_GetPC();
	} else {
		Profile_CpuGetCallinfo(&callinfo, &get_caller, &get_symbol);
		caller_addr = M68000_GetPC();
	}
	if (!callinfo->depth) {
		fprintf(stderr, "Empty stack.\n");
		return;
	}

	depth = callinfo->depth;
	top = ConfigureParams.Debugger.nBacktraceLines;
	if (top > 0 && top < depth) {
		top = depth - top;
	} else {
		top = 0;
	}
	i = 0;
	while (depth-- > top) {
		i++;
		sym_addr = callinfo->stack[depth].callee_addr;
		offset = caller_addr - sym_addr;

		sym = get_symbol(sym_addr, SYMTYPE_CODE);
		if (sym) {
			char sign = offset >= 0 ? '+' : '-';
			fprintf(stderr, "- %d. 0x%06x: %s %c0x%x",
				i, caller_addr, sym, sign, abs(offset));
		} else {
			fprintf(stderr, "- %d. 0x%06x", i, caller_addr);
		}

		sym_addr = caller_addr;
		caller = get_caller(&sym_addr);
		if (caller && caller != sym) {
			offset = caller_addr - sym_addr;
			fprintf(stderr, " (%s +0x%x)\n",
				caller, abs(offset));
		} else {
			fprintf(stderr, "\n");
		}
		caller_addr = callinfo->stack[depth].caller_addr;
	}
}

/**
 * Allocate & set initial callinfo structure information
 */
int Profile_AllocCallinfo(callinfo_t *callinfo, int count, const char *name)
{
	callinfo->sites = count;
	if (count) {
		/* alloc & clear new data */
		callinfo->site = calloc(count, sizeof(callee_t));
		if (callinfo->site) {
			fprintf(stderr, "Allocated %s profile callsite buffer for %d symbols.\n", name, count);
			callinfo->prev_pc = callinfo->return_pc = PC_UNDEFINED;
		} else {
			fprintf(stderr, "ERROR: callsite buffer alloc failed!\n");
			callinfo->sites = 0;
		}
	}
	return callinfo->sites;
}

/**
 * Free all callinfo structure information
 */
void Profile_FreeCallinfo(callinfo_t *callinfo)
{
	int i;
	if (callinfo->sites) {
		callee_t *site = callinfo->site;
		for (i = 0; i < callinfo->sites; i++, site++) {
			if (site->callers) {
				free(site->callers);
			}
		}
		free(callinfo->site);
		if (callinfo->stack) {
			free(callinfo->stack);
		}
		memset(callinfo, 0, sizeof(*callinfo));
	}
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
		"addresses", "callers", "caches", "counts", "cycles", "d-hits", "i-misses",
		"loops", "off", "on", "save", "stack", "stats", "symbols"
	};
	return DebugUI_MatchHelper(names, ARRAY_SIZE(names), text, state);
}

const char Profile_Description[] =
	"<subcommand> [parameter]\n"
	"\n"
	"\tSubcommands:\n"
	"\t- on\n"
	"\t- off\n"
	"\t- counts [count]\n"
	"\t- cycles [count]\n"
	"\t- i-misses [count]\n"
	"\t- d-hits [count]\n"
	"\t- symbols [count]\n"
	"\t- addresses [address]\n"
	"\t- callers\n"
	"\t- caches\n"
	"\t- stack\n"
	"\t- stats\n"
	"\t- save <file>\n"
	"\t- loops <file> [CPU limit] [DSP limit]\n"
	"\n"
	"\t'on' & 'off' enable and disable profiling.  Data is collected\n"
	"\tuntil debugger is entered again at which point you get profiling\n"
	"\tstatistics ('stats') summary.\n"
	"\n"
	"\tThen you can ask for list of the PC addresses, sorted either by\n"
	"\texecution 'counts', used 'cycles', i-cache misses or d-cache hits.\n"
	"\tFirst can be limited just to named addresses with 'symbols'.\n"
	"\tOptional count will limit how many items will be shown.\n"
	"\n"
	"\t'caches' shows histogram of CPU cache usage.\n"
	"\n"
	"\t'addresses' lists the profiled addresses in order, with the\n"
	"\tinstructions (currently) residing at them.  By default this\n"
	"\tstarts from the first executed instruction, or you can\n"
	"\tspecify the starting address.\n"
	"\n"
	"\t'callers' shows (raw) caller information for addresses which\n"
	"\thad symbol(s) associated with them.  'stack' shows the current\n"
	"\tprofile stack (this is useful only with :noinit breakpoints).\n"
	"\n"
	"\tProfile address and callers information can be saved with\n"
	"\t'save' command.\n"
	"\n"
	"\tDetailed (spin) looping information can be collected by\n"
	"\tspecifying to which file it should be saved, with optional\n"
	"\tlimit(s) on how many bytes first and last instruction\n"
	"\taddress of the loop can differ (0 = no limit).";


/**
 * Save profiling information for CPU or DSP.
 */
static bool Profile_Save(const char *fname, bool bForDsp)
{
	FILE *out;
	uint32_t freq;
	const char *proc;

	if (!(out = fopen(fname, "w"))) {
		fprintf(stderr, "ERROR: opening '%s' for writing failed!\n", fname);
		perror(NULL);
		return false;
	}
	if (bForDsp) {
		freq = MachineClocks.DSP_Freq;
		proc = "DSP";
	} else {
		freq = MachineClocks.CPU_Freq_Emul;
		proc = "CPU";
	}

	fprintf(out, "Hatari %s profile (%s)\n", proc, PROG_NAME);
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
 * function CPU & DSP profiling functionality can call to
 * reset loop information log by truncating it.  Only portable
 * way to do that is re-opening it again.
 */
bool Profile_LoopReset(void)
{
	if (!profile_loop.filename) {
		return false;
	}
	if (profile_loop.fp) {
		fclose(profile_loop.fp);
	}
	profile_loop.fp = fopen(profile_loop.filename, "w");
	if (!profile_loop.fp) {
		return false;
	}
	fprintf(profile_loop.fp, "# <processor> <VBLs from boot> <address> <size> <loops>\n");
	return true;
}

/**
 * Open file common to both CPU and DSP profiling.
 */
static bool Profile_Loops(int nArgc, char *psArgs[])
{
	if (nArgc > 2) {
		/* check that the given file can be opened for writing */
		if (profile_loop.filename) {
			free(profile_loop.filename);
		}
		profile_loop.filename = strdup(psArgs[2]);
		if (Profile_LoopReset()) {
			if (nArgc > 3) {
				profile_loop.cpu_limit = atoi(psArgs[3]);
				if (nArgc > 4) {
					profile_loop.dsp_limit = atoi(psArgs[4]);
				}
			}
			fprintf(stderr, "Additional max %d (CPU) & %d (DSP) byte loop profiling enabled to:\n\t%s\n",
				profile_loop.cpu_limit, profile_loop.cpu_limit, psArgs[2]);
		} else {
			free(profile_loop.filename);
			profile_loop.filename = NULL;
			perror("ERROR: opening profile loop output file failed, disabling!");
			return false;
		}
	} else {
		if (profile_loop.fp) {
			fprintf(stderr, "Disabling loop profiling.\n");
			free(profile_loop.filename);
			profile_loop.filename = NULL;
			fclose(profile_loop.fp);
			profile_loop.fp = NULL;
		} else {
			fprintf(stderr, "ERROR: no file name for saving the loop profiling information.\n");
		}
	}
	return true;
}

/**
 * Command: CPU/DSP profiling enabling, exec stats, cycle and call stats.
 * Returns DEBUGGER_CMDDONE or DEBUGGER_CMDCONT.
 */
int Profile_Command(int nArgc, char *psArgs[], bool bForDsp)
{
	static int show = 16;
	uint32_t *disasm_addr;
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
		uint32_t lower, upper = 0;
		if (nArgc > 2) {
			if (Eval_Range(psArgs[2], &lower, &upper, false) < 0) {
				return DEBUGGER_CMDDONE;
			}
		} else {
			lower = *disasm_addr;
		}
		if (bForDsp) {
			*disasm_addr = Profile_DspShowAddresses(lower, upper, stderr, PAGING_ENABLED);
		} else {
			*disasm_addr = Profile_CpuShowAddresses(lower, upper, stderr, PAGING_ENABLED);
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
	} else if (strcmp(psArgs[1], "i-misses") == 0) {
		if (bForDsp) {
			fprintf(stderr, "Cache information is recorded only for CPU, not DSP.\n");
		} else {
			Profile_CpuShowInstrMisses(show);
		}
	} else if (strcmp(psArgs[1], "d-hits") == 0) {
		if (bForDsp) {
			fprintf(stderr, "Cache information is recorded only for CPU, not DSP.\n");
		} else {
			Profile_CpuShowDataHits(show);
		}
	} else if (strcmp(psArgs[1], "caches") == 0) {
		if (bForDsp) {
			fprintf(stderr, "Cache information is recorded only for CPU, not DSP.\n");
		} else {
			Profile_CpuShowCaches();
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
			Profile_DspShowCallers(stderr);
		} else {
			Profile_CpuShowCallers(stderr);
		}
	} else if (strcmp(psArgs[1], "stack") == 0) {
		Profile_ShowStack(bForDsp);

	} else if (strcmp(psArgs[1], "save") == 0) {
		Profile_Save(psArgs[2], bForDsp);

	} else if (strcmp(psArgs[1], "loops") == 0) {
		Profile_Loops(nArgc, psArgs);

	} else {
		DebugUI_PrintCmdHelp(psArgs[0]);
	}
	return DEBUGGER_CMDDONE;
}
