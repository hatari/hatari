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
	Uint32 cpu_limit;	/* max limit for profiled CPU loop size */
	Uint32 dsp_limit;	/* max limit for profiled DSP loop size */
} profile_loop_t;

extern profile_loop_t profile_loop;

typedef struct {
	Uint64 calls, count, cycles; /* common counters between CPU & DSP */
	Uint64 i_misses, d_hits;     /* CPU specific counters */
	Uint64 cycles_diffs;         /* DSP specific counter, not updated at run-time */
} counters_t;

typedef struct {
	int callee_idx;		/* index of called function */
	Uint32 ret_addr;	/* address after returning from call */
	Uint32 caller_addr;	/* remove informational caller address */
	Uint32 callee_addr;	/* remove informational callee address */
	counters_t all;		/* totals including everything called code does */
	counters_t out;		/* totals for subcalls done from callee */
} callstack_t;

/* callee/caller information */
typedef struct {
	calltype_t   flags:8;	/* what kind of call it was */
	unsigned int addr:24;	/* address for the caller */
	Uint32 calls;		/* number of calls, exclusive */
	counters_t all;		/* totals including everything called code does */
	counters_t own;		/* totals excluding called code (=sum(all-out)) */
} caller_t;

typedef struct {
	Uint32 addr;		/* called address */
	int count;		/* number of callers */
	caller_t *callers;	/* who called this address */
} callee_t;

/* impossible PC value, for unitialized PC values */
#define PC_UNDEFINED 0xFFFFFFFF

typedef struct {
	int sites;		/* number of symbol callsites */
	int count;		/* number of items allocated for stack */
	int depth;		/* how many callstack calls haven't yet returned */
	Uint32 prev_pc;		/* stored previous PC value */
	Uint32 return_pc;	/* address for last call return address (speedup) */
	callee_t *site;		/* symbol specific caller information */
	callstack_t *stack;	/* calls that will return */
} callinfo_t;


/* CPU/DSP memory area statistics */
typedef struct {
	counters_t counters;	/* counters for this area */
	Uint32 lowest, highest;	/* active address range within memory area */
	int active;             /* number of active addresses */
	bool overflow;		/* whether counters overflowed */
} profile_area_t;


/* generic profile caller/callee info functions */
extern void Profile_ShowCallers(FILE *fp, int sites, callee_t *callsite, const char * (*addr2name)(Uint32, Uint64 *));
extern void Profile_CallStart(int idx, callinfo_t *callinfo, Uint32 prev_pc, calltype_t flag, Uint32 pc, counters_t *totalcost);
extern void Profile_FinalizeCalls(callinfo_t *callinfo, counters_t *totalcost, const char* (get_symbol)(Uint32 addr));
extern Uint32 Profile_CallEnd(callinfo_t *callinfo, counters_t *totalcost);
extern int  Profile_AllocCallinfo(callinfo_t *callinfo, int count, const char *info);
extern void Profile_FreeCallinfo(callinfo_t *callinfo);
extern bool Profile_LoopReset(void);

/* parser helpers */
extern void Profile_CpuGetPointers(bool **enabled, Uint32 **disasm_addr);
extern void Profile_DspGetPointers(bool **enabled, Uint32 **disasm_addr);
extern void Profile_CpuGetCallinfo(callinfo_t **callinfo, const char* (**get_symbol)(Uint32));
extern void Profile_DspGetCallinfo(callinfo_t **callinfo, const char* (**get_symbol)(Uint32));

/* internal CPU profile results */
extern Uint32 Profile_CpuShowAddresses(Uint32 lower, Uint32 upper, FILE *out);
extern void Profile_CpuShowCounts(int show, bool only_symbols);
extern void Profile_CpuShowCycles(int show);
extern void Profile_CpuShowInstrMisses(int show);
extern void Profile_CpuShowDataHits(int show);
extern void Profile_CpuShowCaches(void);
extern void Profile_CpuShowStats(void);
extern void Profile_CpuShowCallers(FILE *fp);
extern void Profile_CpuSave(FILE *out);

/* internal DSP profile results */
extern Uint16 Profile_DspShowAddresses(Uint32 lower, Uint32 upper, FILE *out);
extern void Profile_DspShowCounts(int show, bool only_symbols);
extern void Profile_DspShowCycles(int show);
extern void Profile_DspShowStats(void);
extern void Profile_DspShowCallers(FILE *fp);
extern void Profile_DspSave(FILE *out);

#endif  /* HATARI_PROFILE_PRIV_H */
