/*
 * Hatari - profile_priv.h
 * 
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 */

#ifndef HATARI_PROFILE_PRIV_H
#define HATARI_PROFILE_PRIV_H

/* caller types */
#define CALL_UNDEFINED	0	/* = call type information not supported */
typedef enum {
	CALL_UNKNOWN	= 1,
	CALL_NEXT	= 2,
	CALL_BRANCH	= 4,
	CALL_SUBROUTINE	= 8,
	CALL_SUBRETURN	= 16,
	CALL_EXCEPTION	= 32,
	CALL_EXCRETURN	= 64,
	CALL_INTERRUPT	= 128
} calltype_t;

/* callee/caller information */
typedef struct {
	calltype_t   flags:8;	/* what kind of call it was */
	unsigned int addr:24;	/* address for the caller */
	Uint32 count;		/* number of calls */
} caller_t;

typedef struct {
	Uint32 addr;		/* called address */
	unsigned int count;	/* number of callers */
	caller_t *callers;	/* who called this address */
} callee_t;


/* CPU/DSP memory area statistics */
typedef struct {
	Uint64 all_cycles, all_count, all_misses;
	Uint64 max_cycles;	/* for overflow check (cycles > count or misses) */
	Uint32 lowest, highest;	/* active address range within memory area */
	Uint32 active;          /* number of active addresses */
} profile_area_t;

/* generic profile caller functions */
extern void Profile_ShowCallers(FILE *fp, unsigned int sites, callee_t *callsite, const char * (*addr2name)(Uint32, Uint64 *));
extern unsigned int Profile_AllocCallerInfo(const char *info, unsigned int oldcount, unsigned int count, callee_t **callsite);
extern void Profile_UpdateCaller(callee_t *callsite, Uint32 pc, Uint32 caller, calltype_t flag);

/* parser helpers */
extern void Profile_CpuGetPointers(bool **enabled, Uint32 **disasm_addr);
extern void Profile_DspGetPointers(bool **enabled, Uint32 **disasm_addr);

/* internal CPU profile results */
extern Uint32 Profile_CpuShowAddresses(Uint32 lower, Uint32 upper, FILE *out);
extern void Profile_CpuShowCounts(unsigned int show, bool only_symbols);
extern void Profile_CpuShowCycles(unsigned int show);
extern void Profile_CpuShowMisses(unsigned int show);
extern void Profile_CpuShowStats(void);
extern void Profile_CpuShowCallers(FILE *fp);
extern void Profile_CpuSave(FILE *out);

/* internal DSP profile results */
extern Uint16 Profile_DspShowAddresses(Uint32 lower, Uint32 upper, FILE *out);
extern void Profile_DspShowCounts(unsigned int show, bool only_symbols);
extern void Profile_DspShowCycles(unsigned int show);
extern void Profile_DspShowStats(void);
extern void Profile_DspShowCallers(FILE *fp);
extern void Profile_DspSave(FILE *out);

#endif  /* HATARI_PROFILE_PRIV_H */
