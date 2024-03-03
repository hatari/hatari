/*
  Hatari - breakcond.c

  Copyright (c) 2009-2024 by Eero Tamminen

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  breakcond.c - code for breakpoint conditions that can check variable
  and memory values against each other, mask them etc. before deciding
  whether the breakpoint should be triggered.  See BreakCond_Help()
  for the syntax.
*/
const char BreakCond_fileid[] = "Hatari breakcond.c";

#include <ctype.h>
#include <stdlib.h>
#include "config.h"
#include "main.h"
#include "file.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "dsp.h"
#include "stMemory.h"
#include "str.h"
#include "vars.h"

#include "debug_priv.h"
#include "breakcond.h"
#include "debugcpu.h"
#include "debugdsp.h"
#include "debugInfo.h"
#include "debugui.h"
#include "evaluate.h"
#include "history.h"
#include "symbols.h"
#include "68kDisass.h"


/* set to 1 to enable parsing function tracing / debug output */
#define DEBUG 0

/* needs to go through long long to handle x=32 */
#define BITMASK(x)      ((uint32_t)(((unsigned long long)1<<(x))-1))

#define BC_DEFAULT_DSP_SPACE 'P'

typedef struct {
	bool is_indirect;
	char dsp_space;	/* DSP has P, X, Y address spaces, zero if not DSP */
	value_t valuetype;	/* Hatari value variable type */
	union {
		uint32_t number;
		uint16_t (*func16)(void);
		uint32_t (*func32)(void);
		uint16_t *reg16;
		uint32_t *reg32;
	} value;
	uint32_t bits;	/* CPU has 8/16/32 bit address widths */
	uint32_t mask;	/* <width mask> && <value mask> */
} bc_value_t;

typedef struct {
	bc_value_t lvalue;
	bc_value_t rvalue;
	char comparison;
	bool track;	/* track value changes */
} bc_condition_t;

typedef struct {
	info_func_t info;  /* pointer to specified ":info" function */
	char *filename;	/* file where to read commands to do on hit */
	int skip;	/* how many times to hit before breaking */
	bool once;	/* remove after hit&break */
	bool quiet;	/* set / hit breakpoint quietly */
	bool trace;	/* trace mode, don't break */
	bool noinit;	/* prevent debugger inits on break */
	bool lock;	/* tracing + show locked info */
	bool deleted;   /* delayed delete flag */
} bc_options_t;

typedef struct {
	char *expression;
	bc_options_t options;
	bc_condition_t *conditions;
	int ccount;	/* condition count */
	int hits;	/* how many times breakpoint hit */
} bc_breakpoint_t;

typedef struct {
	bc_breakpoint_t *breakpoint;
	bc_breakpoint_t *breakpoint2delete;	/* delayed delete of old alloc */
	const char *name;
	int count;
	int allocated;
	bool delayed_change;
	const debug_reason_t reason;
} bc_breakpoints_t;

static bc_breakpoints_t CpuBreakPoints = {
	.name = "CPU",
	.reason = REASON_CPU_BREAKPOINT
};
static bc_breakpoints_t DspBreakPoints = {
	.name = "DSP",
	.reason = REASON_DSP_BREAKPOINT
};


/* forward declarations */
static void BreakCond_DoDelayedActions(bc_breakpoints_t *bps);
static bool BreakCond_Remove(bc_breakpoints_t *bps, int position);
static void BreakCond_Print(bc_breakpoint_t *bp);


/**
 * Save breakpoints as debugger input file
 * return true for success, false for failure
 */
bool BreakCond_Save(const char *filename)
{
	FILE *fp;
	int i;

	if (!(CpuBreakPoints.count || DspBreakPoints.count)) {
		if (File_Exists(filename)) {
			if (remove(filename)) {
				perror("ERROR");
				return false;
			}
		}
		return true;
	}

	fprintf(stderr, "Saving breakpoints to '%s'...\n", filename);
	fp = fopen(filename, "w");
	if (!fp) {
		perror("ERROR");
		return false;
	}
	/* save conditional breakpoints as debugger input file */
	for (i = 0; i < CpuBreakPoints.count; i++) {
		fprintf(fp, "b %s\n", CpuBreakPoints.breakpoint[i].expression);
	}
	for (i = 0; i < DspBreakPoints.count; i++) {
		fprintf(fp, "db %s\n", DspBreakPoints.breakpoint[i].expression);
	}
	fclose(fp);
	return true;
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
static uint32_t BreakCond_ReadDspMemory(uint32_t addr, const bc_value_t *bc_value)
{
	const char *dummy;
	return DSP_ReadMemory(addr, bc_value->dsp_space, &dummy) & BITMASK(24);
}

/**
 * Return value of given size read from given ST memory address
 */
static uint32_t BreakCond_ReadSTMemory(uint32_t addr, const bc_value_t *bc_value)
{
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
 * Return uint32_t value according to given bc_value_t specification
 */
static uint32_t BreakCond_GetValue(const bc_value_t *bc_value)
{
	uint32_t value;

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
 * Show & update rvalue for a tracked breakpoint condition to lvalue
 */
static void BreakCond_UpdateTracked(bc_condition_t *condition, uint32_t value)
{
	uint32_t addr;

	/* next monitor changes to this new value */
	condition->rvalue.value.number = value;

	if (condition->lvalue.is_indirect &&
	    condition->lvalue.valuetype == VALUE_TYPE_NUMBER) {
		/* simple memory address */
		addr = condition->lvalue.value.number;
		fprintf(stderr, "  $%x = $%x\n", addr, value);
	} else {
		/* register tms. */
		fprintf(stderr, "  $%x\n", value);
	}
}


/**
 * Return true if all of the given breakpoint's conditions match
 */
static bool BreakCond_MatchConditions(bc_condition_t *condition, int count)
{
	uint32_t lvalue, rvalue;
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
		if (likely(!hit)) {
			return false;
		}
		if (condition->track) {
			BreakCond_UpdateTracked(condition, lvalue);
		}
	}
	/* all conditions matched */
	return true;
}


/**
 * Check and show which breakpoints' conditions matched
 * @return	true if (non-tracing) breakpoint was hit,
 *		or false if none matched
 */
static bool BreakCond_MatchBreakPoints(bc_breakpoints_t *bps)
{
	bc_breakpoint_t *bp;
	bool changes = false;
	bool hit = false;
	int i;

	/* array should not be changed while it's being traversed */
	assert(likely(!bps->delayed_change));
	bps->delayed_change = true;

	bp = bps->breakpoint;
	for (i = 0; i < bps->count; bp++, i++) {

		if (BreakCond_MatchConditions(bp->conditions, bp->ccount)) {
			bp->hits++;
			if (bp->options.skip) {
				if (bp->hits % bp->options.skip) {
					/* check next */
					continue;
				}
			}
			if (!bp->options.quiet) {
				fprintf(stderr, "%d. %s breakpoint condition(s) matched %d times.\n",
					i+1, bps->name, bp->hits);
				BreakCond_Print(bp);
			}
			History_Mark(bps->reason);

			if (bp->options.info || bp->options.lock || bp->options.filename) {
				bool reinit = !bp->options.noinit;

				if (reinit) {
					DebugCpu_InitSession();
					DebugDsp_InitSession();
				}
				if (bp->options.info) {
					bp->options.info(stderr, 0);
				}
				if (bp->options.lock) {
					DebugInfo_ShowSessionInfo();
				}
				if (bp->options.filename) {
					bool verbose = !bp->options.quiet;
					DebugUI_ParseFile(bp->options.filename, reinit, verbose);
					changes = true;
				}
			}
			if (bp->options.once) {
				BreakCond_Remove(bps, i+1);
				changes = true;
			}
			if (!bp->options.trace) {
				/* index for current hit, they start from 1 */
				hit = true;
			}
			/* continue checking breakpoints to make sure all relevant actions get performed */
		}
	}
	bps->delayed_change = false;
	if (unlikely(changes)) {
		BreakCond_DoDelayedActions(bps);
	}
	return hit;
}

/* ------------- breakpoint condition checking, public API ------------- */

/**
 * Return true if there were CPU breakpoint hits, false otherwise.
 */
bool BreakCond_MatchCpu(void)
{
	return BreakCond_MatchBreakPoints(&CpuBreakPoints);
}

/**
 * Return true if there were DSP breakpoint hits, false otherwise.
 */
bool BreakCond_MatchDsp(void)
{
	return BreakCond_MatchBreakPoints(&DspBreakPoints);
}

/**
 * Return number of CPU condition breakpoints
 */
int BreakCond_CpuBreakPointCount(void)
{
	return CpuBreakPoints.count;
}

/**
 * Return number of DSP condition breakpoints
 */
int BreakCond_DspBreakPointCount(void)
{
	return DspBreakPoints.count;
}


/* -------------- breakpoint condition parsing, internals ------------- */

/* struct for passing around breakpoint conditions parsing state */
typedef struct {
	int arg;		/* current arg */
	int argc;		/* arg count */
	const char **argv;	/* arg pointer array (+ strings) */
	const char *error;	/* error from parsing args */
} parser_state_t;


/**
 * If given string is a Hatari variable name, set bc_value
 * fields accordingly and return true, otherwise return false.
 */
static bool BreakCond_ParseVariable(const char *name, bc_value_t *bc_value)
{
	const var_addr_t *hvar;

	ENTERFUNC(("BreakCond_ParseVariable('%s')\n", name));
	hvar = Vars_ParseVariable(name);
	if (hvar) {
		bc_value->value.reg32 = hvar->addr;
		bc_value->valuetype = hvar->vtype;
		bc_value->bits = hvar->bits;
		assert(bc_value->bits == 32 || bc_value->valuetype !=  VALUE_TYPE_VAR32);
		EXITFUNC(("-> true\n"));
		return true;
	}
	EXITFUNC(("-> false\n"));
	return false;
}


/**
 * If given string matches a suitable symbol, set bc_value
 * fields accordingly and return true, otherwise return false.
 */
static bool BreakCond_ParseSymbol(const char *name, bc_value_t *bc_value)
{
	symtype_t symtype;
	uint32_t addr;

	ENTERFUNC(("BreakCond_ParseSymbol('%s')\n", name));
	if (bc_value->is_indirect) {
		/* indirect use of address makes sense only for data */
		symtype = SYMTYPE_DATA|SYMTYPE_BSS;
	} else {
		/* direct value can be compared for anything */
		symtype = SYMTYPE_ALL;
	}

	if (bc_value->dsp_space) {
		if (!Symbols_GetDspAddress(symtype, name, &addr)) {
			EXITFUNC(("-> false (DSP)\n"));
			return false;
		}
		/* all DSP memory values are 24-bits */
		bc_value->bits = 24;
		bc_value->value.number = addr;
		bc_value->valuetype = VALUE_TYPE_NUMBER;
		EXITFUNC(("-> true (DSP)\n"));
		return true;
	}

	if (!Symbols_GetCpuAddress(symtype, name, &addr)) {
		EXITFUNC(("-> false (CPU)\n"));
		return false;
	}
	if (addr & 1) {
		/* only bytes can be at odd addresses */
		bc_value->bits = 8;
	} else {
		bc_value->bits = 32;
	}
	bc_value->value.number = addr;
	bc_value->valuetype = VALUE_TYPE_NUMBER;
	EXITFUNC(("-> true (CPU)\n"));
	return true;
}


/**
 * Helper function to get CPU PC register value with static inline as uint32_t
 */
static uint32_t GetCpuPC(void)
{
	return M68000_GetPC();
}
/**
 * Helper function to get CPU SR register value with static inline as uint32_t
 */
static uint32_t GetCpuSR(void)
{
	return M68000_GetSR();
}

/**
 * If given string is register name (for DSP or CPU), set bc_value
 * fields accordingly and return true, otherwise return false.
 */
static bool BreakCond_ParseRegister(const char *regname, bc_value_t *bc_value)
{
	int regsize;
	ENTERFUNC(("BreakCond_ParseRegister('%s')\n", regname));
	if (bc_value->dsp_space) {
		regsize = DSP_GetRegisterAddress(regname,
					      &(bc_value->value.reg32),
					      &(bc_value->mask));
		if (regsize) {
			if (bc_value->is_indirect
			    && toupper((unsigned char)regname[0]) != 'R') {
				fprintf(stderr, "ERROR: only R0-R7 DSP registers can be used for indirect addressing!\n");
				EXITFUNC(("-> false (DSP)\n"));
				return false;
			}
			/* all DSP memory values are 24-bits */
			bc_value->bits = 24;
			bc_value->valuetype = regsize;
			EXITFUNC(("-> true (DSP)\n"));
			return true;
		}
		EXITFUNC(("-> false (DSP)\n"));
		return false;
	}
	regsize = DebugCpu_GetRegisterAddress(regname, &(bc_value->value.reg32));
	if (regsize) {
		bc_value->bits = regsize;
		/* valuetypes for registers are 16 & 32 */
		bc_value->valuetype = regsize;
		EXITFUNC(("-> true (CPU)\n"));
		return true;
	}
	/* Exact UAE core 32-bit PC & 16-bit SR register values
	 * can be gotten only through UAE accessors, not directly
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
	EXITFUNC(("-> false (CPU)\n"));
	return false;
}

/**
 * If given address is valid (for DSP or CPU), return true.
 */
static bool BreakCond_CheckAddress(bc_value_t *bc_value)
{
	uint32_t addr = bc_value->value.number;
	int size = bc_value->bits >> 8;

	ENTERFUNC(("BreakCond_CheckAddress(%x)\n", addr));
	if (bc_value->dsp_space) {
		if (addr+size > 0xFFFF) {
			EXITFUNC(("-> false (DSP)\n"));
			return false;
		}
		EXITFUNC(("-> true (DSP)\n"));
		return true;
	}
	if (!STMemory_CheckAreaType(addr, size, ABFLAG_RAM | ABFLAG_ROM | ABFLAG_IO)) {
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
		pstate->error = "space/width modifier can be used only with an (address) expression\n"
				"(note that you can use a mask instead of width, for example: 'd0 & 0xff')";
		EXITFUNC(("arg:%d -> false\n", pstate->arg));
		return false;
	}
	pstate->arg++;
	if (bc_value->dsp_space) {
		switch (pstate->argv[pstate->arg][0]) {
		case 'p':
		case 'x':
		case 'y':
			mode = toupper((unsigned char)pstate->argv[pstate->arg][0]);
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
	if (isalpha((unsigned char)*str) || *str == '_') {
		/* parse direct or indirect variable/register/symbol name */
		if (bc_value->is_indirect) {
			/* a valid register or data symbol name? */
			if (!BreakCond_ParseRegister(str, bc_value) &&
			    !BreakCond_ParseSymbol(str, bc_value)) {
				pstate->error = "invalid register/symbol name for indirection";
				EXITFUNC(("arg:%d -> false\n", pstate->arg));
				return false;
			}
		} else {
			/* a valid Hatari variable or register name?
			 * variables cannot be used for ST memory indirection.
			 */
			if (!BreakCond_ParseVariable(str, bc_value) &&
			    !BreakCond_ParseRegister(str, bc_value) &&
			    !BreakCond_ParseSymbol(str, bc_value)) {
				pstate->error = "invalid variable/register/symbol name";
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
	}
	/* memory address (indirect value) -> OK as address? */
	if (bc_value->is_indirect &&
	    bc_value->valuetype == VALUE_TYPE_NUMBER &&
	    !BreakCond_CheckAddress(bc_value)) {
		pstate->error = "invalid address";
		EXITFUNC(("arg:%d -> false\n", pstate->arg));
		return false;
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
static void BreakCond_InheritDefault(uint32_t *value1, uint32_t value2, uint32_t defvalue)
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
	uint32_t mask1, mask2, defbits;
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
				    bc_breakpoint_t *bp, int ccount)
{
	bc_condition_t condition;

	ENTERFUNC(("BreakCond_ParseCondition(...)\n"));

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
	/* copy new condition */
	ccount += 1;
	bp->conditions = realloc(bp->conditions, sizeof(bc_condition_t)*(ccount));
	if (!bp->conditions) {
		pstate->error = "failed to allocate space for breakpoint condition";
		EXITFUNC(("-> 0\n"));
		return 0;
	}
	bp->conditions[ccount-1] = condition;

	/* continue with next condition? */
	if (pstate->arg == pstate->argc) {
		EXITFUNC(("-> %d conditions (all args parsed)\n", ccount));
		return ccount;
	}
	if (strcmp(pstate->argv[pstate->arg], "&&") != 0) {
		pstate->error = "trailing content for breakpoint condition";
		EXITFUNC(("-> 0\n"));
		return 0;
	}
	pstate->arg++;

	/* recurse conditions parsing */
	ccount = BreakCond_ParseCondition(pstate, bForDsp, bp, ccount);
	if (!ccount) {
		EXITFUNC(("-> 0\n"));
		return 0;
	}
	EXITFUNC(("-> %d conditions (recursed)\n", ccount));
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
		if (isspace((unsigned char)*src)) {
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
			/* variable/register/symbol or number prefix? */
			if (!(isalnum((unsigned char)*src) || *src == '_' ||
			      *src == '$' || *src == '#' || *src == '%')) {
				pstate->error = "invalid character";
				pstate->arg = src-expression;
				free(normalized);
				return NULL;
			}
			*dst++ = *src;
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
 * Select current breakpoints struct and provide name for it.
 * Make sure there's always space for at least one additional breakpoint.
 * Return pointer to the breakpoints struct
 */
static bc_breakpoints_t* BreakCond_GetListInfo(bool bForDsp)
{
	bc_breakpoints_t *bps;
	if (bForDsp) {
		bps = &DspBreakPoints;
	} else {
		bps = &CpuBreakPoints;
	}
	/* allocate (more) space for breakpoints when needed */
	if (bps->count + 1 >= bps->allocated) {
		if (!bps->allocated) {
			/* initial count of available breakpoints */
			bps->allocated = 16;
		} else {
			bps->allocated *= 2;
		}
		if (bps->delayed_change) {
			if(bps->breakpoint2delete) {
				/* getting second re-alloc within same breakpoint handler is really
				 * unlikely, this would require adding dozens of new breakpoints.
				 */
				fprintf(stderr, "ERROR: too many new breakpoints added within single breakpoint hit!\n");
				abort();
			}
			bps->breakpoint2delete = bps->breakpoint;
			bps->breakpoint = malloc(bps->allocated * sizeof(bc_breakpoint_t));
		} else {
			bps->breakpoint = realloc(bps->breakpoint, bps->allocated * sizeof(bc_breakpoint_t));
		}
		assert(bps->breakpoint);
	}
	return bps;
}


/**
 * Check whether any of the breakpoint conditions is such that it's
 * intended for tracking given value changes (inequality comparison
 * on identical values) or for retrieving the current value to break
 * on next value change (other comparisons on identical values).
 *
 * On former case, mark it for tracking, on other cases, just
 * retrieve the value.
 */
static void BreakCond_CheckTracking(bc_breakpoint_t *bp)
{
	bc_condition_t *condition;
	bool track = false;
	uint32_t value;
	int i;

	condition = bp->conditions;
	for (i = 0; i < bp->ccount; condition++, i++) {

		if (memcmp(&(condition->lvalue), &(condition->rvalue), sizeof(bc_value_t)) == 0) {
			/* set current value to right side */
			value = BreakCond_GetValue(&(condition->rvalue));
			condition->rvalue.value.number = value;
			condition->rvalue.valuetype = VALUE_TYPE_NUMBER;
			condition->rvalue.is_indirect = false;
			/* track those changes */
			if (condition->comparison != '=') {
				condition->track = true;
				track = true;
			} else {
				fprintf(stderr, "\t%d. condition: %c $%x\n",
					i+1, condition->comparison, value);
			}
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
static bool BreakCond_Parse(const char *expression, bc_options_t *options, bool bForDsp)
{
	parser_state_t pstate;
	bc_breakpoints_t *bps;
	bc_breakpoint_t *bp;
	char *normalized;
	int ccount;

	bps = BreakCond_GetListInfo(bForDsp);

	bp = bps->breakpoint + bps->count;
	memset(bp, 0, sizeof(bc_breakpoint_t));

	normalized = BreakCond_TokenizeExpression(expression, &pstate);
	if (normalized) {
		bp->expression = normalized;
		ccount = BreakCond_ParseCondition(&pstate, bForDsp, bp, 0);
		/* fail? */
		if (!ccount) {
			bp->expression = NULL;
			if (bp->conditions) {
				/* free what was allocated by ParseCondition */
				free(bp->conditions);
				bp->conditions = NULL;
			}
		}
		bp->ccount = ccount;
	} else {
		ccount = 0;
	}
	if (pstate.argv) {
		free(pstate.argv);
	}
	if (ccount > 0) {
		bps->count++;
		if (!options->quiet) {
			fprintf(stderr, "%s condition breakpoint %d with %d condition(s) added:\n\t%s\n",
				bps->name, bps->count, ccount, bp->expression);
			if (options->skip) {
				fprintf(stderr, "-> Break only on every %d hit.\n", options->skip);
			}
			if (options->once) {
				fprintf(stderr, "-> Break only once, and delete breakpoint afterwards.\n");
			}
			if (options->trace) {
				fprintf(stderr, "-> Trace (just show breakpoint info, instead of dropping to debugger).\n");
				/* all of these options enable also trace option */
				if (options->info) {
					fprintf(stderr, "-> Call selected info command.\n");
				}
				if (options->lock) {
					fprintf(stderr, "-> Call locked info command.\n");
				}
				if (options->noinit) {
					fprintf(stderr, "-> Skip debugger initialization on hit.\n");
				}
			}
			if (options->filename) {
				fprintf(stderr, "-> Execute debugger commands from '%s' file on hit.\n", options->filename);
			}
		}
		BreakCond_CheckTracking(bp);

		bp->options.quiet = options->quiet;
		bp->options.skip = options->skip;
		bp->options.once = options->once;
		bp->options.trace = options->trace;
		bp->options.info = options->info;
		bp->options.lock = options->lock;
		bp->options.noinit = options->noinit;
		if (options->filename) {
			bp->options.filename = strdup(options->filename);
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
 * print single breakpoint
 */
static void BreakCond_Print(bc_breakpoint_t *bp)
{
	fprintf(stderr, "\t%s", bp->expression);
	if (bp->options.skip) {
		fprintf(stderr, " :%d", bp->options.skip);
	}
	if (bp->options.once) {
		fprintf(stderr, " :once");
	}
	if (bp->options.quiet) {
		fprintf(stderr, " :quiet");
	}
	if (bp->options.trace) {
		fprintf(stderr, " :trace");
		if (bp->options.info) {
			fprintf(stderr, " :info");
		}
		if (bp->options.lock) {
			fprintf(stderr, " :lock");
		}
		if (bp->options.noinit) {
			fprintf(stderr, " :noinit");
		}
	}
	if (bp->options.filename) {
		fprintf(stderr, " :file %s", bp->options.filename);
	}
	if (bp->options.deleted) {
		fprintf(stderr, " (deleted)");
	}
	fprintf(stderr, "\n");
}

/**
 * List condition breakpoints
 */
static void BreakCond_List(bc_breakpoints_t *bps)
{
	bc_breakpoint_t *bp;
	int i;

	if (!bps->count) {
		fprintf(stderr, "No conditional %s breakpoints.\n", bps->name);
		return;
	}
	fprintf(stderr, "%d conditional %s breakpoints:\n", bps->count, bps->name);
	bp = bps->breakpoint;
	for (i = 1; i <= bps->count; bp++, i++) {
		fprintf(stderr, "%4d:", i);
		BreakCond_Print(bp);
	}
}


/**
 * Remove condition breakpoint at given position
 */
static bool BreakCond_Remove(bc_breakpoints_t *bps, int position)
{
	bc_breakpoint_t *bp;

	if (!bps->count) {
		fprintf(stderr, "No (more) %s breakpoints to remove.\n", bps->name);
		return false;
	}
	if (position < 1 || position > bps->count) {
		fprintf(stderr, "ERROR: No such %s breakpoint.\n", bps->name);
		return false;
	}
	bp = bps->breakpoint + (position - 1);
	if (bps->delayed_change) {
		bp->options.deleted = true;
		return true;
	}
	if (!bp->options.quiet) {
		fprintf(stderr, "Removed %s breakpoint %d:\n", bps->name, position);
		BreakCond_Print(bp);
	}
	free(bp->expression);
	free(bp->conditions);
	bp->expression = NULL;
	bp->conditions = NULL;

	if (bp->options.filename) {
		free(bp->options.filename);
	}

	if (position < bps->count) {
		memmove(bp, bp + 1, (bps->count - position) * sizeof(bc_breakpoint_t));
	}
	bps->count--;
	return true;
}


/**
 * Remove all conditional breakpoints
 */
static void BreakCond_RemoveAll(bc_breakpoints_t *bps)
{
	bool removed;
	int i;

	for (i = bps->count; i > 0; i--) {
		removed = BreakCond_Remove(bps, i);
		ASSERT_VARIABLE(removed);
	}
	fprintf(stderr, "%s breakpoints: %d\n", bps->name, bps->count);
}

/**
 * Do delayed breakpoint actions, remove breakpoints and old array alloc
 */
static void BreakCond_DoDelayedActions(bc_breakpoints_t *bps)
{
	bc_options_t *options;
	bool removed;
	int i;

	assert(!bps->delayed_change);
	if (bps->breakpoint2delete) {
		free(bps->breakpoint2delete);
		bps->breakpoint2delete = NULL;
	}
	for (i = bps->count; i > 0; i--) {
		options = &(bps->breakpoint[i-1].options);
		if (options->deleted) {
			options->deleted = false;
			removed = BreakCond_Remove(bps, i);
			ASSERT_VARIABLE(removed);
		}
	}
}


/**
 * Return true if given CPU breakpoint has given CPU expression.
 * Used by the test code.
 */
int BreakCond_MatchCpuExpression(int position, const char *expression)
{
	if (position < 1 || position > CpuBreakPoints.count) {
		return false;
	}
	if (strcmp(expression, CpuBreakPoints.breakpoint[position-1].expression)) {
		return false;
	}
	return true;
}


/* ------------- breakpoint condition parsing, public API ------------ */

static const char BreakCond_Help[] =
"  condition = <value>[.mode] [& <mask>] <comparison> <value>[.mode]\n"
"\n"
"  where:\n"
"  	value = [(] <register/symbol/variable name | number> [)]\n"
"  	number/mask = [#|$|%]<digits>\n"
"  	comparison = '<' | '>' | '=' | '!'\n"
"  	addressing mode (width) = 'b' | 'w' | 'l'\n"
"  	addressing mode (space) = 'p' | 'x' | 'y'\n"
"\n"
"  If the value is in parenthesis like in '($ff820)' or '(a0)', then\n"
"  the used value will be read from the memory address pointed by it.\n"
"\n"
"  If the parsed value expressions on both sides of it are exactly\n"
"  the same, right side is replaced with its current value.  For\n"
"  inequality ('!') comparison, the breakpoint will additionally track\n"
"  all further changes for the given address/register expression value.\n"
"  (This is useful for tracking register and memory value changes.)\n"
"\n"
"  M68k addresses can have byte (b), word (w) or long (l, default) width.\n"
"  DSP addresses belong to different address spaces: P, X or Y. Note that\n"
"  on DSP only R0-R7 registers can be used for memory addressing.\n"
"\n"
"  Examples:\n"
"  	pc = $64543  &&  ($ff820).w & 3 = (a0)  &&  d0 = %1100\n"
"       ($ffff9202).w ! ($ffff9202).w :trace\n"
"  	(r0).x = 1 && (r0).y = 2\n"
"\n"
"  For breakpoint options, see 'help b'.\n";


const char BreakCond_Description[] =
	"<condition> [&& <condition> ...] [:<option>] | <index> | help | all\n"
	"\n"
	"\tSet breakpoint with given <conditions>, remove breakpoint with\n"
	"\tgiven <index>, remove all breakpoints with 'all' or output\n"
	"\tbreakpoint condition syntax with 'help'.  Without arguments,\n"
	"\tlists currently active breakpoints.\n"
	"\n"
	"\tMultiple breakpoint action options can be specified after\n"
	"\tthe breakpoint condition(s):\n"
	"\t- 'trace', print the breakpoint match without stopping\n"
	"\t- 'info <name>', call indicated info functionality (enables 'trace')\n"
	"\t- 'lock', print the locked debugger entry info (enables 'trace')\n"
	"\t- 'noinit', no debugger inits on hit, useful for stack tracing\n"
	"\t- 'file <file>', execute debugger commands from given <file>\n"
	"\t- 'once', delete the breakpoint after it's hit\n"
	"\t- 'quiet', set / hit breakpoint quietly\n"
	"\t- '<count>', break only on every <count> hit";

/**
 * Parse options for the given breakpoint command.
 * Return true for success and false for failure.
 */
static bool BreakCond_Options(char *str, bc_options_t *options, char marker)
{
	char *option, *next, *filename, *info;
	int skip;

	memset(options, 0, sizeof(*options));

	option = strchr(str, marker);
	if (option) {
		/* end breakcond command at options */
		*option = 0;
	}
	for (next = option; option; option = next) {

		char splitter[3] = {' ', marker, '\0'};
		/* skip marker + end & trim this option */
		option = next + 1;
		next = strstr(option, splitter);
		if (next) {
			next++; /* skip space to next marker */
			*next = 0;
		}
		option = Str_Trim(option);

		if (strcmp(option, "once") == 0) {
			options->once = true;
		} else if (strcmp(option, "quiet") == 0) {
			options->quiet = true;
		} else if (strcmp(option, "trace") == 0) {
			options->trace = true;
		} else if (strcmp(option, "lock") == 0) {
			options->trace = true;
			options->lock = true;
		} else if (strcmp(option, "noinit") == 0) {
			options->trace = true;
			options->noinit = true;
		} else if (strncmp(option, "info ", 5) == 0) {
			options->trace = true;
			info = Str_Trim(option+4);
			options->info = DebugInfo_GetInfoFunc(info);
			if (!options->info) {
				fprintf(stderr, "ERROR: no info for '%s'!\n", info);
				return false;
			}
		} else if (strncmp(option, "file ", 5) == 0) {
			filename = Str_Trim(option+4);
			if (!File_Exists(filename)) {
				fprintf(stderr, "ERROR: given file '%s' doesn't exist!\n", filename);
				fprintf(stderr, "(if you use 'cd' command, do it before setting breakpoints)\n");
				return false;
			}
			options->filename = filename;
		} else if (isdigit((unsigned char)*option)) {
			/* :<value> */
			skip = atoi(option);
			if (skip < 2) {
				fprintf(stderr, "ERROR: invalid breakpoint skip count '%s'!\n", option);
				return false;
			}
			options->skip = skip;
		} else {
			fprintf(stderr, "ERROR: unrecognized breakpoint option '%s'!\n", option);
			return false;
		}
	}
	return true;
}

/**
 * Parse given command expression to set/remove/list
 * conditional breakpoints for CPU or DSP.
 * Return true for success and false for failure.
 */
bool BreakCond_Command(const char *args, bool bForDsp)
{
	bc_breakpoints_t *bps;
	char *expression, *argscopy;
	unsigned int position;
	bc_options_t options;
	const char *end;
	bool ret = true;

	bps = BreakCond_GetListInfo(bForDsp);
	if (!args) {
		BreakCond_List(bps);
		return true;
	}
	argscopy = strdup(args);
	assert(argscopy);

	expression = Str_Trim(argscopy);

	/* subcommands? */
	if (strncmp(expression, "help", 4) == 0) {
		fputs(BreakCond_Help, stderr);
		goto cleanup;
	}
	if (strcmp(expression, "all") == 0) {
		BreakCond_RemoveAll(bps);
		goto cleanup;
	}

	if (bForDsp && !bDspEnabled) {
		ret = false;
		fprintf(stderr, "ERROR: DSP not enabled!\n");
		goto cleanup;
	}

	/* postfix options? */
	if (!BreakCond_Options(expression, &options, ':')) {
		ret = false;
		goto cleanup;
	}

	/* index (for breakcond removal)? */
	end = expression;
	while (isdigit((unsigned char)*end)) {
		end++;
	}
	if (end > expression && *end == '\0' &&
	    sscanf(expression, "%u", &position) == 1) {
		ret = BreakCond_Remove(bps, position);
	} else {
		/* add breakpoint? */
		ret = BreakCond_Parse(expression, &options, bForDsp);
	}
cleanup:
	free(argscopy);
	return ret;
}


const char BreakAddr_Description[] =
	"<address> [:<option>]\n"
	"\tCreate conditional breakpoint for given PC <address>.\n"
	"\n"
	"\tBreakpoint action option alternatives:\n"
	"\t- 'trace', print the breakpoint match without stopping\n"
	"\t- 'lock', print the debugger entry info without stopping\n"
	"\t- 'once', delete the breakpoint after it's hit\n"
	"\t- 'quiet', set / hit breakpoint quietly\n"
	"\t- '<count>', break only on every <count> hit\n"
	"\n"
	"\tUse conditional breakpoint commands to manage the created\n"
	"\tbreakpoints.";

/**
 * Set CPU & DSP program counter address breakpoints by converting
 * them to conditional breakpoints.
 * Return true for success and false for failure.
 */
bool BreakAddr_Command(char *args, bool bForDsp)
{
	const char *errstr, *expression = (const char *)args;
	char *cut, command[32];
	uint32_t addr;
	int offset;

	if (!args) {
		if (bForDsp) {
			DebugUI_PrintCmdHelp("dspaddress");
		} else {
			DebugUI_PrintCmdHelp("address");
		}
		return true;
	}

	/* split options */
	if ((cut = strchr(args, ':'))) {
		*cut = '\0';
		cut = Str_Trim(cut+1);
		if (strlen(cut) > 5) {
			cut[5] = '\0';
		}
	}

	/* evaluate address expression */
	errstr = Eval_Expression(expression, &addr, &offset, bForDsp);
	if (errstr) {
		fprintf(stderr, "ERROR in the address expression:\n'%s'\n%*c-%s\n",
			expression, offset+2, '^', errstr);
		return false;
	}

	/* add the address breakpoint with optional option */
	sprintf(command, "pc=$%x %c%s", addr, cut?':':' ', cut?cut:"");
	if (!BreakCond_Command(command, bForDsp)) {
		return false;
	}

	/* on success, show on what instruction it was added */
	if (bForDsp) {
		DSP_DisasmAddress(stderr, addr, addr);
	} else {
		uaecptr dummy;
		Disasm(stderr, (uaecptr)addr, &dummy, 1);
	}
	return true;
}
