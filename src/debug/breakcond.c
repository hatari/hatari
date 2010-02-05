/*
  Hatari - breakcond.c

  Copyright (c) 2009-2010 by Eero Tamminen

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  breakcond.c - code for breakpoint conditions that can check variable
  and memory values against each other, mask them etc. before deciding
  whether the breakpoint should be triggered.  See BreakCond_Help()
  for the syntax.
*/
const char BreakCond_fileid[] = "Hatari breakcond.c : " __DATE__ " " __TIME__;

#include <ctype.h>
#include <stdlib.h>
#include "config.h"
#include "main.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "dsp.h"
#include "stMemory.h"
#include "str.h"
#include "video.h"	/* for Hatari video variable addresses */

#include "debug_priv.h"
#include "breakcond.h"
#include "debugcpu.h"
#include "evaluate.h"


/* set to 1 to enable parsing function tracing / debug output */
#define DEBUG 0

/* needs to go through long long to handle x=32 */
#define BITMASK(x)      ((Uint32)(((unsigned long long)1<<(x))-1))

#define BC_MAX_CONDITION_BREAKPOINTS 16
#define BC_MAX_CONDITIONS_PER_BREAKPOINT 4

#define BC_DEFAULT_DSP_SPACE 'P'

enum {	
	/* plain number */
	VALUE_TYPE_NUMBER     = 0,

	/* functions to call to get value */
	VALUE_TYPE_FUNCTION32 = 2,

	/* internal Hatari value variables */
	VALUE_TYPE_VAR32      = 4,

	/* size must match register size used in BreakCond_ParseRegister() */
	VALUE_TYPE_REG16      = 16,
	VALUE_TYPE_REG32      = 32
} typedef value_t;

static inline bool is_register_type(value_t vtype) {
	/* type used for CPU/DSP registers */
	return (vtype == VALUE_TYPE_REG16 || vtype == VALUE_TYPE_REG32);
}

typedef struct {
	bool is_indirect;
	char dsp_space;	/* DSP has P, X, Y address spaces, zero if not DSP */
	value_t valuetype;	/* Hatari value variable type */
	union {
		Uint32 number;
		Uint16 (*func16)(void);
		Uint32 (*func32)(void);
		Uint16 *reg16;
		Uint32 *reg32;
	} value;
	Uint32 bits;	/* CPU has 8/16/32 bit address widths */
	Uint32 mask;	/* <width mask> && <value mask> */
} bc_value_t;

typedef struct {
	bc_value_t lvalue;
	bc_value_t rvalue;
	char comparison;
	bool track;	/* track value changes */
} bc_condition_t;

typedef struct {
	char *expression;
	bc_condition_t conditions[BC_MAX_CONDITIONS_PER_BREAKPOINT];
	int ccount;
	int hits;	/* how many times breakpoint hit */
	bool once;      /* remove after hit */
	bool trace;	/* trace mode, don't break */
} bc_breakpoint_t;

static bc_breakpoint_t BreakPointsCpu[BC_MAX_CONDITION_BREAKPOINTS];
static bc_breakpoint_t BreakPointsDsp[BC_MAX_CONDITION_BREAKPOINTS];
static int BreakPointCpuCount;
static int BreakPointDspCount;

/* --------------------- memory snapshot ------------------- */

/**
 * Save/Restore snapshot of local breakpoint variables
 */
void BreakCond_MemorySnapShot_Capture(bool bSave)
{
	char tmp[256], **str;
	int i, idx, count;

	if (!bSave) {
		/* free current data before restore */
		for (i = 0; i < BreakPointCpuCount; i++) {
			free(BreakPointsCpu[i].expression);
		}
		for (i = 0; i < BreakPointDspCount; i++) {
			free(BreakPointsDsp[i].expression);
		}
	}
	
	/* save/restore arrays & counts */
	MemorySnapShot_Store(&BreakPointCpuCount, sizeof(BreakPointCpuCount));
	MemorySnapShot_Store(&BreakPointDspCount, sizeof(BreakPointDspCount));
	MemorySnapShot_Store(&BreakPointsCpu, sizeof(BreakPointsCpu));
	MemorySnapShot_Store(&BreakPointsDsp, sizeof(BreakPointsDsp));

	/* save/restore dynamically allocated strings */
	for (i = 0; i < 2*BC_MAX_CONDITION_BREAKPOINTS; i++) {
		if (i >= BC_MAX_CONDITION_BREAKPOINTS) {
			idx = i-BC_MAX_CONDITION_BREAKPOINTS;
			str = &(BreakPointsDsp[idx].expression);
			count = BreakPointDspCount;
		} else {
			idx = i;
			str = &(BreakPointsCpu[idx].expression);
			count = BreakPointCpuCount;
		}
		if (bSave) {
			/* clean + zero-terminate, copy & save */
			memset(tmp, 0, sizeof(tmp));
			if (idx < count) {
				strncpy(tmp, *str, sizeof(tmp)-1);
			}
			MemorySnapShot_Store(&tmp, sizeof(tmp));
		} else {
			MemorySnapShot_Store(&tmp, sizeof(tmp));
			if (idx < count) {
				*str = strdup(tmp);
				assert(*str);
			} else {
				*str = NULL;
			}
		}
	}
}

/* --------------------- debugging code ------------------- */

#if DEBUG
/* see parsing code for usage examples */
static int _traceIndent;
static void _spaces(void)
{
	int spaces = _traceIndent;
	while(spaces-- > 0) {
		putchar(' ');	/* fputc(' ',stdout); */
	}
}
#define ENTERFUNC(args) { _traceIndent += 2; _spaces(); printf args ; fflush(stdout); }
#define EXITFUNC(args) { _spaces(); printf args ; fflush(stdout); _traceIndent -= 2; }
#else
#define ENTERFUNC(args)
#define EXITFUNC(args)
#endif


/* ------------- breakpoint condition checking, internals ------------- */

/**
 * Return value from given DSP memory space/address
 */
static Uint32 BreakCond_ReadDspMemory(Uint32 addr, const bc_value_t *bc_value)
{
	const char *dummy;
	return DSP_ReadMemory(addr, bc_value->dsp_space, &dummy) & BITMASK(24);
}

/**
 * Return value of given size read from given ST memory address
 */
static Uint32 BreakCond_ReadSTMemory(Uint32 addr, const bc_value_t *bc_value)
{
	/* Mask to a 24 bit address. With this e.g. $ffff820a is also
	 * recognized as IO mem $ff820a (which is the same in the 68000).
	 */
	addr &= 0x00ffffff;

	switch (bc_value->bits) {
	case 32:
		return STMemory_ReadLong(addr);
	case 16:
		return STMemory_ReadWord(addr);
	case 8:
		return STMemory_ReadByte(addr);
	default:
		fprintf(stderr, "ERROR: unknown ST address size %d!\n", bc_value->bits);
		abort();
	}
}


/**
 * Return Uint32 value according to given bc_value_t specification
 */
static Uint32 BreakCond_GetValue(const bc_value_t *bc_value)
{
	Uint32 value;

	switch (bc_value->valuetype) {
	case VALUE_TYPE_NUMBER:
		value = bc_value->value.number;
		break;
	case VALUE_TYPE_FUNCTION32:
		value = bc_value->value.func32();
		break;
	case VALUE_TYPE_REG16:
		value = *(bc_value->value.reg16);
		break;
	case VALUE_TYPE_VAR32:
	case VALUE_TYPE_REG32:
		value = *(bc_value->value.reg32);
		break;
	default:
		fprintf(stderr, "ERROR: unknown condition value size/type %d!\n", bc_value->valuetype);
		abort();
	}
	if (bc_value->is_indirect) {
		if (bc_value->dsp_space) {
			value = BreakCond_ReadDspMemory(value, bc_value);
		} else {
			value = BreakCond_ReadSTMemory(value, bc_value);
		}
	}
	return (value & bc_value->mask);
}


/**
 * Return true if all of the given breakpoint's conditions match
 */
static bool BreakCond_MatchConditions(const bc_condition_t *condition, int count)
{
	Uint32 lvalue, rvalue;
	bool hit = false;
	int i;
	
	for (i = 0; i < count; condition++, i++) {

		lvalue = BreakCond_GetValue(&(condition->lvalue));
		rvalue = BreakCond_GetValue(&(condition->rvalue));

		switch (condition->comparison) {
		case '<':
			hit = (lvalue < rvalue);
			break;
		case '>':
			hit = (lvalue > rvalue);
			break;
		case '=':
			hit = (lvalue == rvalue);
			break;
		case '!':
			hit = (lvalue != rvalue);
			break;
		default:
			fprintf(stderr, "ERROR: Unknown breakpoint value comparison operator '%c'!\n",
				condition->comparison);
			abort();
		}
		if (!hit) {
			return false;
		}
	}
	/* all conditions matched */
	return true;
}


/**
 * Show values for the tracked breakpoint conditions
 */
static void BreakCond_ShowTracked(bc_condition_t *condition, int count)
{
	Uint32 addr, value;
	char sep;
	int i;
	
	sep = ' ';
	for (i = 0; i < count; condition++, i++) {
		if (!condition->track) {
			continue;
		}

		/* get the new value in address */
		value = BreakCond_GetValue(&(condition->lvalue));
		/* next monitor changes to this new value */
		condition->rvalue.value.number = value;
		
		if (condition->lvalue.is_indirect &&
		    condition->lvalue.valuetype == VALUE_TYPE_NUMBER) {
			/* simple memory address */
			addr = condition->lvalue.value.number;
			fprintf(stderr, "%c $%x = $%x", sep, addr, value);
		} else {
			/* register tms. */
			fprintf(stderr, "%c $%x", sep, value);
		}
		sep = ',';
	}
	fprintf(stderr, "\n");
}


/**
 * Return which of the given condition breakpoints match
 * or zero if none matched
 */
static int BreakCond_MatchBreakPoints(bc_breakpoint_t *bp, int count, const char *name)
{
	int i;
	
	for (i = 0; i < count; bp++, i++) {
		if (BreakCond_MatchConditions(bp->conditions, bp->ccount)) {
			BreakCond_ShowTracked(bp->conditions, bp->ccount);
			fprintf(stderr, "%d. %s breakpoint condition(s) matched %d times.\n",
				i+1, name, ++(bp->hits));
			if (bp->trace) {
				return 0;
			}
			fprintf(stderr, "  %s\n", bp->expression);
			if (bp->once) {
				BreakCond_Remove(i+1, (bp-i == BreakPointsDsp));
			}
			/* indexes for BreakCond_Remove() start from 1 */
			return i + 1;
		}
	}
	return 0;
}

/* ------------- breakpoint condition checking, public API ------------- */

/**
 * Return matched CPU breakpoint index or zero for an error.
 */
int BreakCond_MatchCpu(void)
{
	return BreakCond_MatchBreakPoints(BreakPointsCpu, BreakPointCpuCount, "CPU");
}

/**
 * Return matched DSP breakpoint index or zero for an error.
 */
int BreakCond_MatchDsp(void)
{
	return BreakCond_MatchBreakPoints(BreakPointsDsp, BreakPointDspCount, "DSP");
}

/**
 * Return number of condition breakpoints
 */
int BreakCond_BreakPointCount(bool bForDsp)
{
	if (bForDsp) {
		return BreakPointDspCount;
	} else {
		return BreakPointCpuCount;
	}
}


/* -------------- breakpoint condition parsing, internals ------------- */

/* struct for passing around breakpoint conditions parsing state */
typedef struct {
	int arg;		/* current arg */
	int argc;		/* arg count */
	const char **argv;	/* arg pointer array (+ strings) */
	const char *error;	/* error from parsing args */
} parser_state_t;


/* Hatari variable name & address array items */
typedef struct {
	const char *name;
	Uint32 *addr;
	value_t vtype;
	size_t bits;
	const char *constraints;
} var_addr_t;

/* Accessor functions for calculated Hatari values */
static Uint32 GetLineCycles(void)
{
	int dummy1, dummy2, lcycles;
	Video_GetPosition(&dummy1, &dummy2 , &lcycles);
	return lcycles;
}
static Uint32 GetFrameCycles(void)
{
	int dummy1, dummy2, fcycles;
	Video_GetPosition(&fcycles, &dummy1, &dummy2);
	return fcycles;
}

/* sorted by variable name so that this can be bisected */
static const var_addr_t hatari_vars[] = {
	{ "FrameCycles", (Uint32*)GetFrameCycles, VALUE_TYPE_FUNCTION32, 0, NULL },
	{ "HBL", (Uint32*)&nHBL, VALUE_TYPE_VAR32, sizeof(nHBL)*8, NULL },
	{ "LineCycles", (Uint32*)GetLineCycles, VALUE_TYPE_FUNCTION32, 0, "is always divisable by 4" },
	{ "VBL", (Uint32*)&nVBLs, VALUE_TYPE_VAR32, sizeof(nVBLs)*8, NULL }
};


/**
 * Readline match callback for variable name completion.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char *BreakCond_MatchVariable(const char *text, int state)
{
	static int i, len;
	const char *name;
	
	if (!state) {
		/* first match */
		len = strlen(text);
		i = 0;
	}
	/* next match */
	while (i < ARRAYSIZE(hatari_vars)) {
		name = hatari_vars[i++].name;
		if (strncasecmp(name, text, len) == 0)
			return (strdup(name));
	}
	return NULL;
}


/**
 * If given string is a Hatari variable name, set bc_value
 * fields accordingly and return true, otherwise return false.
 */
static bool BreakCond_ParseVariable(const char *name, bc_value_t *bc_value)
{
	/* left, right, middle, direction */
        int l, r, m, dir;

	ENTERFUNC(("BreakCond_ParseVariable('%s')\n", name));
	/* bisect */
	l = 0;
	r = ARRAYSIZE(hatari_vars) - 1;
	do {
		m = (l+r) >> 1;
		dir = strcasecmp(name, hatari_vars[m].name);
		if (dir == 0) {
			bc_value->value.reg32 = hatari_vars[m].addr;
			bc_value->valuetype = hatari_vars[m].vtype;
			bc_value->bits = hatari_vars[m].bits;
			assert(bc_value->bits == 32 || bc_value->valuetype !=  VALUE_TYPE_VAR32);
			EXITFUNC(("-> true\n"));
			return true;
		}
		if (dir < 0) {
			r = m-1;
		} else {
			l = m+1;
		}
	} while (l <= r);
	EXITFUNC(("-> false\n"));
	return false;
}


/**
 * Helper function to get CPU PC register value with static inline as Uint32
 */
static Uint32 GetCpuPC(void)
{
	return M68000_GetPC();
}
/**
 * Helper function to get CPU SR register value with static inline as Uint32
 */
static Uint32 GetCpuSR(void)
{
	return M68000_GetSR();
}

/**
 * If given string is register name (for DSP or CPU), set bc_value
 * fields accordingly and return true, otherwise return false.
 */
static bool BreakCond_ParseRegister(const char *regname, bc_value_t *bc_value, parser_state_t *pstate)
{
	int regsize;
	ENTERFUNC(("BreakCond_ParseRegister('%s')\n", regname));
	if (bc_value->dsp_space) {
		regsize = DSP_GetRegisterAddress(regname,
					      &(bc_value->value.reg32),
					      &(bc_value->mask));
		if (regsize) {
			if (bc_value->is_indirect && toupper(regname[0]) != 'R') {
				pstate->error = "only R0-R7 registers can be used for indirect addressing";
				EXITFUNC(("-> false (DSP)\n"));
				return false;
			}
			/* all DSP memory values are 24-bits */
			bc_value->bits = 24;
			bc_value->valuetype = regsize;
			EXITFUNC(("-> true (DSP)\n"));
			return true;
		}
		pstate->error = "invalid DSP register name";
		EXITFUNC(("-> false (DSP)\n"));
		return false;
	}
	regsize = DebugCpu_GetRegisterAddress(regname, &(bc_value->value.reg32));
	if (regsize) {
		bc_value->bits = regsize;
		bc_value->valuetype = regsize;
		EXITFUNC(("-> true (CPU)\n"));
		return true;
	}
	/* Exact UAE core 32-bit PC & 16-bit SR register values
	 * can be gotten only through AUE accessors, not directly
	 */
	if (strcasecmp(regname, "PC") == 0) {
		bc_value->bits = 32;
		bc_value->value.func32 = GetCpuPC;
		bc_value->valuetype = VALUE_TYPE_FUNCTION32;
		EXITFUNC(("-> true (CPU)\n"));
		return true;
	}
	if (strcasecmp(regname, "SR") == 0) {
		bc_value->bits = 16;
		bc_value->value.func32 = GetCpuSR;
		bc_value->valuetype = VALUE_TYPE_FUNCTION32;
		EXITFUNC(("-> true (CPU)\n"));
		return true;
	}
	pstate->error = "invalid CPU register name";
	EXITFUNC(("-> false (CPU)\n"));
	return false;
}

/**
 * If given address is valid (for DSP or CPU), return true.
 */
static bool BreakCond_CheckAddress(bc_value_t *bc_value)
{
	Uint32 highbyte, bit23, addr = bc_value->value.number;

	ENTERFUNC(("BreakCond_CheckAddress(%x)\n", addr));
	if (bc_value->dsp_space) {
		if (addr > 0xFFFF) {
			EXITFUNC(("-> false (DSP)\n"));
			return false;
		}
		EXITFUNC(("-> true (DSP)\n"));
		return true;
	}

	bit23 = (addr >> 23) & 1;
	highbyte = (addr >> 24) & 0xff;
	if ((bit23 == 0 && highbyte != 0) ||
	    (bit23 == 1 && highbyte != 0xff)) {
		fprintf(stderr, "WARNING: address 0x%x 23th bit isn't extended to bits 24-31.\n", addr);
	}
	/* use a 24-bit address */
	addr &= 0x00ffffff;
	if ((addr > STRamEnd && addr < 0xe00000) ||
	    (addr >= 0xff0000 && addr < 0xff8000)) {
		EXITFUNC(("-> false (CPU)\n"));
		return false;
	}
	EXITFUNC(("-> true (CPU)\n"));
	return true;
}


/**
 * Check for and parse a condition value address space/width modifier.
 * Modify pstate according to parsing (arg index and error string).
 * Return false for error and true for no or successfully parsed modifier.
 */
static bool BreakCond_ParseAddressModifier(parser_state_t *pstate, bc_value_t *bc_value)
{
	char mode;

	ENTERFUNC(("BreakCond_ParseAddressModifier()\n"));
	if (pstate->arg+2 > pstate->argc ||
	    strcmp(pstate->argv[pstate->arg], ".") != 0) {
		if (bc_value->dsp_space && bc_value->is_indirect) {
			pstate->error = "DSP memory addresses need to specify address space";
			EXITFUNC(("arg:%d -> false\n", pstate->arg));
			return false;
		}
		EXITFUNC(("arg:%d -> true (missing)\n", pstate->arg));
		return true;
	}
	if (!bc_value->is_indirect) {
		pstate->error = "space/width modifier makes sense only for an address (register)";
		EXITFUNC(("arg:%d -> false\n", pstate->arg));
		return false;
	}
	pstate->arg++;
	if (bc_value->dsp_space) {
		switch (pstate->argv[pstate->arg][0]) {
		case 'p':
		case 'x':
		case 'y':
			mode = toupper(pstate->argv[pstate->arg][0]);
			break;
		default:
			pstate->error = "invalid address space modifier";
			EXITFUNC(("arg:%d -> false\n", pstate->arg));
			return false;
		}
	} else {
		switch (pstate->argv[pstate->arg][0]) {
		case 'l':
			mode = 32;
			break;
		case 'w':
			mode = 16;
			break;
		case 'b':
			mode = 8;
			break;
		default:
			pstate->error = "invalid address width modifier";
			EXITFUNC(("arg:%d -> false\n", pstate->arg));
			return false;
		}
	}
	if (pstate->argv[pstate->arg][1]) {
		pstate->error = "invalid address space/width modifier";
		EXITFUNC(("arg:%d -> false\n", pstate->arg));
		return false;
	}
	if (bc_value->dsp_space) {
		bc_value->dsp_space = mode;
		EXITFUNC(("arg:%d -> space:%c, true\n", pstate->arg, mode));
	} else {
		bc_value->bits = mode;
		EXITFUNC(("arg:%d -> width:%d, true\n", pstate->arg, mode));
	}
	pstate->arg++;
	return true;
}


/**
 * Check for and parse a condition value mask.
 * Modify pstate according to parsing (arg index and error string).
 * Return false for error and true for no or successfully parsed modifier.
 */
static bool BreakCond_ParseMaskModifier(parser_state_t *pstate, bc_value_t *bc_value)
{
	ENTERFUNC(("BreakCond_ParseMaskModifier()\n"));
	if (pstate->arg+2 > pstate->argc ||
	    strcmp(pstate->argv[pstate->arg], "&") != 0) {
		EXITFUNC(("arg:%d -> true (missing)\n", pstate->arg));
		return true;
	}
	if (bc_value->valuetype == VALUE_TYPE_NUMBER &&
	    !bc_value->is_indirect) {
		fprintf(stderr, "WARNING: plain numbers shouldn't need masks.\n");
	}
	pstate->arg++;
	if (!Eval_Number(pstate->argv[pstate->arg], &(bc_value->mask))) {
		pstate->error = "invalid dec/hex/bin value";
		EXITFUNC(("arg:%d -> false\n", pstate->arg));
		return false;
	}
	if (bc_value->mask == 0 ||
	    (bc_value->valuetype == VALUE_TYPE_NUMBER && !bc_value->is_indirect &&
	     bc_value->value.number && !(bc_value->value.number & bc_value->mask))) {
		pstate->error = "mask zeroes value";
		EXITFUNC(("arg:%d -> false\n", pstate->arg));
		return false;
	}
	EXITFUNC(("arg:%d -> true (%x)\n", pstate->arg, bc_value->mask));
	pstate->arg++;
	return true;
}


/**
 * Parse a breakpoint condition value.
 * Modify pstate according to parsing (arg index and error string).
 * Return true for success and false for error.
 */
static bool BreakCond_ParseValue(parser_state_t *pstate, bc_value_t *bc_value)
{
	const char *str;
	int skip = 1;

	ENTERFUNC(("BreakCond_Value()\n"));
	if (pstate->arg >= pstate->argc) {
		pstate->error = "value missing";
		EXITFUNC(("arg:%d -> false\n", pstate->arg));
		return false;
	}
	/* parse indirection */
	if (pstate->arg+3 <= pstate->argc) {
		if (strcmp(pstate->argv[pstate->arg+0], "(") == 0 &&
		    strcmp(pstate->argv[pstate->arg+2], ")") == 0) {
			bc_value->is_indirect = true;
			pstate->arg++;
			skip = 2;
		}
	}
	
	str = pstate->argv[pstate->arg];
	/* parse direct or indirect value */
	if (isalpha(str[0])) {
		if (bc_value->is_indirect) {
			/* a valid register name? */
			if (!BreakCond_ParseRegister(str, bc_value, pstate)) {
				EXITFUNC(("arg:%d -> false\n", pstate->arg));
				return false;
			}
		} else {
			/* a valid Hatari variable or register name?
			 * variables cannot be used for ST memory indirection.
			 */
			if (!BreakCond_ParseVariable(str, bc_value) &&
			    !BreakCond_ParseRegister(str, bc_value, pstate)) {
				pstate->error = "invalid variable/register name";
				EXITFUNC(("arg:%d -> false\n", pstate->arg));
				return false;
			}
		}
	} else {
		/* a number */
		if (!Eval_Number(str, &(bc_value->value.number))) {
			pstate->error = "invalid dec/hex/bin value";
			EXITFUNC(("arg:%d -> false\n", pstate->arg));
			return false;
		}
		/* suitable as emulated memory address (indirect)? */
		if (bc_value->is_indirect &&
		    bc_value->valuetype == VALUE_TYPE_NUMBER &&
		    !BreakCond_CheckAddress(bc_value)) {
			pstate->error = "invalid address";
			EXITFUNC(("arg:%d -> false\n", pstate->arg));
			return false;
		}
	}
	pstate->arg += skip;

	/* parse modifiers */
	if (!BreakCond_ParseAddressModifier(pstate, bc_value)) {
		EXITFUNC(("arg:%d -> false\n", pstate->arg));
		return false;
	}
	if (!BreakCond_ParseMaskModifier(pstate, bc_value)) {
		EXITFUNC(("arg:%d -> false\n", pstate->arg));
		return false;
	}
	EXITFUNC(("arg:%d -> true (%s value)\n", pstate->arg,
		  (bc_value->is_indirect ? "indirect" : "direct")));
	return true;
}


/**
 * Parse a breakpoint comparison character.
 * Modify pstate according to parsing (arg index and error string).
 * Return the character or nil for an error.
 */
static char BreakCond_ParseComparison(parser_state_t *pstate)
{
	const char *comparison;
	
	ENTERFUNC(("BreakCond_ParseComparison(), arg:%d\n", pstate->arg));
	if (pstate->arg >= pstate->argc) {
		pstate->error = "breakpoint comparison missing";
		EXITFUNC(("-> false\n"));
		return false;
	}
	comparison = pstate->argv[pstate->arg];
	switch (comparison[0]) {
	case '<':
	case '>':
	case '=':
	case '!':
		break;
	default:
		pstate->error = "invalid comparison character";
		EXITFUNC(("-> false\n"));
		return false;
	}
	if (comparison[1]) {
		pstate->error = "trailing comparison character(s)";
		EXITFUNC(("-> false\n"));
		return false;
	}

	pstate->arg++;
	if (pstate->arg >= pstate->argc) {
		pstate->error = "right side missing";
		EXITFUNC(("-> false\n"));
		return false;
	}
	EXITFUNC(("-> '%c'\n", *comparison));
	return *comparison;
}


/**
 * If no value, use the other value, if that also missing, use default
 */
static void BreakCond_InheritDefault(Uint32 *value1, Uint32 value2, Uint32 defvalue)
{
	if (!*value1) {
		if (value2) {
			*value1 = value2;
		} else {
			*value1 = defvalue;
		}
	}
}

/**
 * Check & ensure that the masks and address sizes are sane
 * and allow comparison with the other side.
 * If yes, return true, otherwise false.
 */
static bool BreakCond_CrossCheckValues(parser_state_t *pstate,
				       bc_value_t *bc_value1,
				       bc_value_t *bc_value2)
{
	Uint32 mask1, mask2, defbits;
	ENTERFUNC(("BreakCond_CrossCheckValues()\n"));

	/* make sure there're valid bit widths and that masks have some value */
	if (bc_value1->dsp_space) {
		defbits = 24;
	} else {
		defbits = 32;
	}
	BreakCond_InheritDefault(&(bc_value1->bits), bc_value2->bits, defbits);
	BreakCond_InheritDefault(&(bc_value2->bits), bc_value1->bits, defbits);
	BreakCond_InheritDefault(&(bc_value1->mask), bc_value2->mask, BITMASK(bc_value1->bits));
	BreakCond_InheritDefault(&(bc_value2->mask), bc_value1->mask, BITMASK(bc_value2->bits));

	/* check first value mask & bit width */
	mask1 = BITMASK(bc_value1->bits) & bc_value1->mask;
	
	if (mask1 != bc_value1->mask) {
		fprintf(stderr, "WARNING: mask 0x%x doesn't fit into %d address/register bits.\n",
			bc_value1->mask, bc_value1->bits);
	}
	if (!bc_value1->dsp_space &&
	    bc_value1->is_indirect &&
	    (bc_value1->value.number & 1) && bc_value1->bits > 8) {
		fprintf(stderr, "WARNING: odd CPU address 0x%x given without using byte (.b) width.\n",
			bc_value1->value.number);
	}
	
	/* cross-check both values masks */
	mask2 = BITMASK(bc_value2->bits) & bc_value2->mask;

	if ((mask1 & mask2) == 0) {
		pstate->error = "values masks cancel each other";
		EXITFUNC(("-> false\n"));
		return false;
	}
	if (bc_value2->is_indirect ||
	    bc_value2->value.number == 0 ||
	    bc_value2->valuetype != VALUE_TYPE_NUMBER) {
		EXITFUNC(("-> true (no problematic direct types)\n"));
		return true;
	}
	if ((bc_value2->value.number & mask1) != bc_value2->value.number) {
		pstate->error = "number doesn't fit the other side address width&mask";
		EXITFUNC(("-> false\n"));
		return false;
	}
	EXITFUNC(("-> true\n"));
	return true;
}


/**
 * Parse given breakpoint conditions and append them to breakpoints.
 * Modify pstate according to parsing (arg index and error string).
 * Return number of added conditions or zero for failure.
 */
static int BreakCond_ParseCondition(parser_state_t *pstate, bool bForDsp,
				    bc_condition_t *conditions, int ccount)
{
	bc_condition_t condition;

	ENTERFUNC(("BreakCond_ParseCondition(...)\n"));
	if (ccount >= BC_MAX_CONDITIONS_PER_BREAKPOINT) {
		pstate->error = "max number of conditions exceeded";
		EXITFUNC(("-> 0 (no conditions free)\n"));
		return 0;
	}

	/* setup condition */
	memset(&condition, 0, sizeof(bc_condition_t));
	if (bForDsp) {
		/* used also for checking whether value is for DSP */
		condition.lvalue.dsp_space = BC_DEFAULT_DSP_SPACE;
		condition.rvalue.dsp_space = BC_DEFAULT_DSP_SPACE;
	}

	/* parse condition */
	if (!BreakCond_ParseValue(pstate, &(condition.lvalue))) {
		EXITFUNC(("-> 0\n"));
		return 0;
	}
	condition.comparison = BreakCond_ParseComparison(pstate);
	if (!condition.comparison) {
		EXITFUNC(("-> 0\n"));
		return 0;
	}
	if (!BreakCond_ParseValue(pstate, &(condition.rvalue))) {
		EXITFUNC(("-> 0\n"));
		return 0;
	}
	if (!(BreakCond_CrossCheckValues(pstate, &(condition.lvalue), &(condition.rvalue)) &&
	      BreakCond_CrossCheckValues(pstate, &(condition.rvalue), &(condition.lvalue)))) {
		EXITFUNC(("-> 0\n"));
		return 0;
	}
	/* new condition */
	conditions[ccount++] = condition;

	/* continue with next condition? */
	if (pstate->arg == pstate->argc) {
		EXITFUNC(("-> %d (conditions)\n", ccount-1));
		return ccount;
	}
	if (strcmp(pstate->argv[pstate->arg], "&&") != 0) {
		pstate->error = "trailing content for breakpoint condition";
		EXITFUNC(("-> 0\n"));
		return 0;
	}
	pstate->arg++;

	/* recurse conditions parsing */
	ccount = BreakCond_ParseCondition(pstate, bForDsp, conditions, ccount);
	if (!ccount) {
		EXITFUNC(("-> 0\n"));
		return 0;
	}
	EXITFUNC(("-> %d (conditions)\n", ccount-1));
	return ccount;
}


/**
 * Tokenize given breakpoint expression to given parser struct.
 * Return normalized expression string that corresponds to tokenization
 * or NULL on error. On error, pstate->error contains the error message
 * and pstate->arg index to invalid character (instead of to token like
 * after parsing).
 */
static char *BreakCond_TokenizeExpression(const char *expression,
					  parser_state_t *pstate)
{
	char separator[] = {
		'=', '!', '<', '>',  /* comparison operators */
		'(', ')', '.', '&',  /* other separators */
		'\0'                 /* terminator */
	};
	bool is_separated, has_comparison;
	char sep, *dst, *normalized;
	const char *src;
	int i, tokens;

	memset(pstate, 0, sizeof(parser_state_t));

	/* _minimum_ safe size for normalized expression is 2x+1 */
	normalized = malloc(2*strlen(expression)+1);
	if (!normalized) {
		pstate->error = "alloc failed";
		return NULL;
	}

	/* check characters & normalize string */
	dst = normalized;
	is_separated = false;
	has_comparison = false;
	for (src = expression; *src; src++) {
		/* discard white space in source */
		if (isspace(*src)) {
			continue;
		}
		/* separate tokens with single space in destination */
		for (i = 0; (sep = separator[i]); i++) {
			if (*src == sep) {
				if (dst > normalized) {
					/* don't separate boolean AND '&&' */
					if (*src == '&' && *(src-1) == '&') {
						dst--;
					} else {
						if (!is_separated) {
							*dst++ = ' ';
						}
					}
				}
				*dst++ = *src;
				*dst++ = ' ';
				is_separated = true;
				if (i < 4) {
					has_comparison = true;
				}
				break;
			}
		}
		/* validate & copy other characters */
		if (!sep) {
			if (!(isalnum(*src) || isblank(*src) ||
			      *src == '$' || *src == '%')) {
				pstate->error = "invalid character";
				pstate->arg = src-expression;
				free(normalized);
				return NULL;
			}
			*dst++ = tolower(*src);
			is_separated = false;
		}
	}
	if (is_separated) {
		dst--;	/* no trailing space */
	}
	*dst = '\0';

	if (!has_comparison) {
		pstate->error = "condition comparison missing";
		pstate->arg = strlen(expression)/2;
		free(normalized);
		return NULL;
	}

	/* allocate exact space for tokenized string array + strings */
	tokens = 1;
	for (dst = normalized; *dst; dst++) {
		if (*dst == ' ') {
			tokens++;
		}
	}
	pstate->argv = malloc(tokens*sizeof(char*)+strlen(normalized)+1);
	if (!pstate->argv) {
		pstate->error = "alloc failed";
		free(normalized);
		return NULL;
	}
	/* and copy/tokenize... */
	dst = (char*)(pstate->argv) + tokens*sizeof(char*);
	strcpy(dst, normalized);
	pstate->argv[0] = strtok(dst, " ");
	for (i = 1; (dst = strtok(NULL, " ")); i++) {
		pstate->argv[i] = dst;
	}
	assert(i == tokens);
	pstate->argc = tokens;
#if DEBUG
	fprintf(stderr, "args->");
	for (i = 0; i < tokens; i++) {
		fprintf(stderr, " %d: %s,", i, pstate->argv[i]);
	}
	fprintf(stderr, "\n");
#endif
	return normalized;
}


/**
 * Helper to set corrent breakpoint list and type name to given variables.
 * Return pointer to breakpoint list count
 */
static int* BreakCond_GetListInfo(bc_breakpoint_t **bp,
				  const char **name, bool bForDsp)
{
	int *bcount;
	if (bForDsp) {
		bcount = &BreakPointDspCount;
		*bp = BreakPointsDsp;
		*name = "DSP";
	} else {
		bcount = &BreakPointCpuCount;
		*bp = BreakPointsCpu;
		*name = "CPU";
	}
	return bcount;
}


/**
 * Check whether any of the breakpoint conditions is such
 * that it's intended for tracking given value changes: 
 *    inequality check for values that are the same
 * If yes, mark & change it for tracing.
 */
static void BreakCond_CheckTracking(bc_breakpoint_t *bp)
{
	bc_condition_t *condition;
	bool track = false;
	Uint32 value;
	int i;

	condition = bp->conditions;
	for (i = 0; i < bp->ccount; condition++, i++) {
		
		if (condition->comparison == '!' &&
		    memcmp(&(condition->lvalue), &(condition->rvalue), sizeof(bc_value_t)) == 0) {
			/* set current value to right side */
			value = BreakCond_GetValue(&(condition->rvalue));
			condition->rvalue.value.number = value;
			condition->rvalue.valuetype = VALUE_TYPE_NUMBER;
			condition->rvalue.is_indirect = false;
			/* and note that this is traced */
			condition->track = true;
			track = true;
		}
	}
	if (track) {
		fprintf(stderr, "-> Track value changes, show value(s) when matched.\n");
	}
}


/**
 * Parse given breakpoint expression and store it.
 * Return true for success and false for failure.
 */
static bool BreakCond_Parse(const char *expression, bool bForDsp, bool trace, bool once)
{
	parser_state_t pstate;
	bc_breakpoint_t *bp;
	const char *name;
	char *normalized;
	int *bcount;
	int ccount;

	bcount = BreakCond_GetListInfo(&bp, &name, bForDsp);
	if (*bcount >= BC_MAX_CONDITION_BREAKPOINTS) {
		fprintf(stderr, "ERROR: no free %s condition breakpoints left.\n", name);
		return false;
	}
	bp += *bcount;
	memset(bp, 0, sizeof(bc_breakpoint_t));

	normalized = BreakCond_TokenizeExpression(expression, &pstate);
	if (normalized) {
		bp->expression = normalized;
		ccount = BreakCond_ParseCondition(&pstate, bForDsp,
						  bp->conditions, 0);
		bp->ccount = ccount;
	} else {
		ccount = 0;
	}
	if (pstate.argv) {
		free(pstate.argv);
	}
	if (ccount > 0) {
		(*bcount)++;
		fprintf(stderr, "%s condition breakpoint %d with %d condition(s) added.\n",
			name, *bcount, ccount);
		BreakCond_CheckTracking(bp);
		if (trace) {
			fprintf(stderr, "-> Trace, show hits, but don't break.\n");
			bp->trace = trace;
		}
		if (once) {
			fprintf(stderr, "-> Once, delete after hit.\n");
			bp->once = once;
		}
	} else {
		if (normalized) {
			int offset, i = 0;
			char *s = normalized;
			while (*s && i < pstate.arg) {
				if (*s++ == ' ') {
					i++;
				}
			}
			offset = s - normalized;
			/* show tokenized string and point out
			 * the token where the error was encountered
			 */
			fprintf(stderr, "ERROR in tokenized string:\n'%s'\n%*c-%s\n",
				normalized, offset+2, '^', pstate.error);
			free(normalized);
			bp->expression = NULL;
		} else {
			/* show original string and point out the character
			 * where the error was encountered
			 */
			fprintf(stderr, "ERROR in parsed string:\n'%s'\n%*c-%s\n",
				expression, pstate.arg+2, '^', pstate.error);
		}
	}
	return (ccount > 0);
}


/**
 * List condition breakpoints
 */
void BreakCond_List(bool bForDsp)
{
	const char *name;
	bc_breakpoint_t *bp;
	int i, bcount;
	
	bcount = *BreakCond_GetListInfo(&bp, &name, bForDsp);
	if (!bcount) {
		fprintf(stderr, "No conditional %s breakpoints.\n", name);
		return;
	}

	fprintf(stderr, "%d conditional %s breakpoints:\n", bcount, name);
	for (i = 1; i <= bcount; bp++, i++) {
		fprintf(stderr, "%3d: %s%s%s\n", i, bp->expression,
			bp->trace?", traced":"", bp->once?", once":"");
	}
}


/**
 * Remove condition breakpoint at given position
 */
bool BreakCond_Remove(int position, bool bForDsp)
{
	const char *name;
	bc_breakpoint_t *bp;
	int *bcount, offset;
	
	bcount = BreakCond_GetListInfo(&bp, &name, bForDsp);
	if (!*bcount) {
		fprintf(stderr, "No (more) breakpoints to remove.\n");
		return false;
	}
	if (position < 1 || position > *bcount) {
		fprintf(stderr, "ERROR: No such %s breakpoint.\n", name);
		return false;
	}
	offset = position - 1;
	fprintf(stderr, "Removed %s breakpoint %d:\n  %s\n",
		name, position, bp[offset].expression);
	free(bp[offset].expression);
	bp[offset].expression = NULL;
	if (position < *bcount) {
		memmove(bp+offset, bp+position,
			(*bcount-position)*sizeof(bc_breakpoint_t));
	}
	(*bcount)--;
	return true;
}


/**
 * Remove all condition breakpoints
 */
void BreakCond_RemoveAll(bool bForDsp)
{
	while (BreakCond_Remove(1, bForDsp))
		;
}


/**
 * Return true if given CPU breakpoint has given CPU expression.
 * Used by the test code.
 */
int BreakCond_MatchCpuExpression(int position, const char *expression)
{
	if (position < 1 || position > BreakPointCpuCount) {
		return false;
	}
	if (strcmp(expression, BreakPointsCpu[position-1].expression)) {
		return false;
	}
	return true;
}


/**
 * help
 */
static void BreakCond_Help(void)
{
	Uint32 value;
	int i;
	fputs(
"  breakpoint = <condition> [ && <condition> ... ] [option]\n"
"  condition = <value>[.mode] [& <number>] <comparison> <value>[.mode]\n"
"\n"
"  where:\n"
"  	value = [(] <register-name | hatari-variable | number> [)]\n"
"  	number = [#|$|%]<digits>\n"
"  	comparison = '<' | '>' | '=' | '!'\n"
"  	addressing mode (width) = 'b' | 'w' | 'l'\n"
"  	addressing mode (space) = 'p' | 'x' | 'y'\n"
"  	option = trace | once\n"
"\n"
"  If the value is in parenthesis like in '($ff820)' or '(a0)', then\n"
"  the used value will be read from the memory address pointed by it.\n"
"\n"
"  If the value expressions on both sides of the '!' inequality comparison\n"
"  are exactly the same, then the breakpoint tracks changes to the given\n"
"  (address/register) value.  'trace' option to continue without breaking\n"
"  can be useful with this. 'once' option removes breakpoint after hit.\n"
"\n"
"  M68k addresses can have byte (b), word (w) or long (l, default) width.\n"
"  DSP addresses belong to different address spaces: P, X or Y. Note that\n"
"  on DSP only R0-R7 registers can be used for memory addressing.\n"
"\n"
"  Valid Hatari variable names (and their current values) are:\n", stderr);
	for (i = 0; i < ARRAYSIZE(hatari_vars); i++) {
		switch (hatari_vars[i].vtype) {
		case VALUE_TYPE_FUNCTION32:
			value = ((Uint32(*)(void))(hatari_vars[i].addr))();
			break;
		case VALUE_TYPE_VAR32:
			value = *(hatari_vars[i].addr);
			break;
		default:
			fprintf(stderr, "ERROR: variable '%s' has unsupported type '%d'\n",
				hatari_vars[i].name, hatari_vars[i].vtype);
			continue;
		}
		fprintf(stderr, "  - %s (%d)", hatari_vars[i].name, value);
		if (hatari_vars[i].constraints) {
			fprintf(stderr, ", %s\n", hatari_vars[i].constraints);
		} else {
			fprintf(stderr, "\n");
		}
	}
	fputs(
"\n"
"  Examples:\n"
"  	pc = $64543  &&  ($ff820).w & 3 = (a0)  &&  d0 = %1100\n"
"       ($ffff9202).w ! ($ffff9202).w trace\n"
"  	(r0).x = 1 && (r0).y = 2\n", stderr);
}


const char BreakCond_Description[] =
	"[ help | all | <index> | <condition> [trace|once] ]\n"
	"\tSet breakpoint with given condition, remove breakpoint with\n"
	"\tgiven index or list all breakpoints when no args are given.\n"
	"\tAdding 'trace' to end of condition causes breakpoint match\n"
	"\tjust to be printed, not break.  Adding 'once' will delete\n"
	"\tthe breakpoint after it's hit.  'help' outputs breakpoint\n"
	"\tcondition syntax help, 'all' removes all conditional breakpoints.";


/* ------------- breakpoint condition parsing, public API ------------ */

/**
 * Parse given DebugUI command for Dsp and act accordingly
 */
bool BreakCond_Command(const char *args, bool bForDsp)
{
	bool trace, once, ret = true;
	char *cut, *expression;
	unsigned int position;
	const char *end;
	
	if (!args) {
		BreakCond_List(bForDsp);
		return true;
	}
	expression = strdup(args);
	assert(expression);
	
	expression = Str_Trim(expression);
	/* subcommands */
	if (strncmp(expression, "help", 4) == 0) {
		BreakCond_Help();
		goto cleanup;
	}
	if (strcmp(expression, "all") == 0) {
		BreakCond_RemoveAll(bForDsp);
		goto cleanup;
	}
	/* postfix options */
	once = false;
	trace = false;
	if ((cut = Str_EndsWith(expression, "trace"))) {
		*cut = '\0';
		trace = true;
	} else {
		if ((cut = Str_EndsWith(expression, "once"))) {
			*cut = '\0';
			once = true;
		}
	}
	/* index (for removal) */
	end = expression;
	while (isdigit(*end)) {
		end++;
	}
	if (end > expression && *end == '\0' &&
	    sscanf(expression, "%u", &position) == 1) {
		ret = BreakCond_Remove(position, bForDsp);
	} else {
		/* add breakpoint? */
		ret = BreakCond_Parse(expression, bForDsp, trace, once);
	}
cleanup:
	free(expression);
	return ret;
}
