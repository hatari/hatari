/*
 * Hatari - profile_priv.h
 * 
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 */

#ifndef HATARI_PROFILE_PRIV_H
#define HATARI_PROFILE_PRIV_H

typedef struct {
	char *filename;		/* where to write loop info */
	FILE *fp;		/* pointer modified by CPU & DSP code */
	uint32_t cpu_limit;	/* max limit for profiled CPU loop size */
	uint32_t dsp_limit;	/* max limit for profiled DSP loop size */
} profile_loop_t;

extern profile_loop_t profile_loop;

typedef struct {
	uint64_t calls, count, cycles; /* common counters between CPU & DSP */
	uint64_t i_misses, d_hits;     /* CPU specific counters */
	uint64_t cycles_diffs;         /* DSP specific counter, not updated at run-time */
} counters_t;

typedef struct {
	int callee_idx;		/* index of called function */
	uint32_t ret_addr;	/* address after returning from call */
	uint32_t caller_addr;	/* caller address for callstack printing */
	uint32_t callee_addr;	/* callee address for callstack printing */
	counters_t all;		/* totals including everything called code does */
	counters_t out;		/* totals for subcalls done from callee */
} callstack_t;

/* callee/caller information */
typedef struct {
	calltype_t flags;	/* what kind of call it was */
	uint32_t addr;		/* address for the caller */
	uint32_t calls;		/* number of calls, exclusive */
	counters_t all;		/* totals including everything called code does */
	counters_t own;		/* totals excluding called code (=sum(all-out)) */
} caller_t;

typedef struct {
	uint32_t addr;		/* called address */
	int count;		/* number of callers */
	caller_t *callers;	/* who called this address */
} callee_t;

/* impossible PC value, for uninitialized PC values */
#define PC_UNDEFINED 0xFFFFFFFF

typedef struct {
	int sites;		/* number of symbol callsites */
	int count;		/* number of items allocated for stack */
	int depth;		/* how many callstack calls haven't yet returned */
	uint32_t prev_pc;	/* stored previous PC value */
	uint32_t return_pc;	/* address for last call return address (speedup) */
	callee_t *site;		/* symbol specific caller information */
	callstack_t *stack;	/* calls that will return */
} callinfo_t;


/* CPU/DSP memory area statistics */
typedef struct {
	counters_t counters;      /* counters for this area */
	uint32_t lowest, highest; /* active address range within memory area */
	int active;               /* number of active addresses */
	bool overflow;            /* whether counters overflowed */
} profile_area_t;


/* generic profile caller/callee info functions */
extern void Profile_ShowCallers(FILE *fp, int sites, callee_t *callsite, const char * (*addr2name)(uint32_t, uint64_t *));
extern void Profile_CallStart(int idx, callinfo_t *callinfo, uint32_t prev_pc, calltype_t flag, uint32_t pc, counters_t *totalcost);
extern void Profile_FinalizeCalls(uint32_t pc, callinfo_t *callinfo, counters_t *totalcost,
				  const char* (get_symbol)(uint32_t, symtype_t), const char* (get_caller)(uint32_t*));
extern uint32_t Profile_CallEnd(callinfo_t *callinfo, counters_t *totalcost);
extern int  Profile_AllocCallinfo(callinfo_t *callinfo, int count, const char *info);
extern void Profile_FreeCallinfo(callinfo_t *callinfo);
extern bool Profile_LoopReset(void);

/* parser helpers */
extern void Profile_CpuGetPointers(bool **enabled, uint32_t **disasm_addr);
extern void Profile_DspGetPointers(bool **enabled, uint32_t **disasm_addr);
extern void Profile_CpuGetCallinfo(callinfo_t **callinfo, const char* (**get_caller)(uint32_t*), const char* (**get_symbol)(uint32_t, symtype_t));
extern void Profile_DspGetCallinfo(callinfo_t **callinfo, const char* (**get_caller)(uint32_t*), const char* (**get_symbol)(uint32_t, symtype_t));

typedef enum {
	PAGING_DISABLED,
	PAGING_ENABLED
} paging_t;

/* internal CPU profile results */
extern uint32_t Profile_CpuShowAddresses(uint32_t lower, uint32_t upper, FILE *out, paging_t use_paging);
extern void Profile_CpuShowCounts(int show, bool only_symbols);
extern void Profile_CpuShowCycles(int show);
extern void Profile_CpuShowInstrMisses(int show);
extern void Profile_CpuShowDataHits(int show);
extern void Profile_CpuShowCaches(void);
extern void Profile_CpuShowStats(void);
extern void Profile_CpuShowCallers(FILE *fp);
extern void Profile_CpuSave(FILE *out);

/* internal DSP profile results */
extern uint16_t Profile_DspShowAddresses(uint32_t lower, uint32_t upper, FILE *out, paging_t use_paging);
extern void Profile_DspShowCounts(int show, bool only_symbols);
extern void Profile_DspShowCycles(int show);
extern void Profile_DspShowStats(void);
extern void Profile_DspShowCallers(FILE *fp);
extern void Profile_DspSave(FILE *out);

#endif  /* HATARI_PROFILE_PRIV_H */
