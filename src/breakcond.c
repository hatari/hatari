/*
  Hatari - breakcond.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  breakcond.c - code for breakpoint conditions that can check variable
  and memory values against each other, mask them etc. before deciding
  whether the breakpoint should be triggered.  The syntax is:

  breakpoint = <expression> N*[ & <expression> ]
  expression = <value>[.width] [& <number>] <condition> <value>[.width]

  where:
  	value = [(] <register-name | number> [)]
  	number = [$]<digits>
  	condition = '<' | '>' | '=' | '!'
  	width = 'b' | 'w' | 'l'

  If the value is in parenthesis like in "($ff820)" or "(a0)", then
  the used value will be read from the memory address pointed by it.
  If value is prefixed with '$', it's a hexadecimal, otherwise it's
  decimal value.

  Example:
  	pc = $64543  &  ($ff820).w & 3 = (a0)  &  d0.l = 123
*/
const char BreakCond_fileid[] = "Hatari breakcond.c : " __DATE__ " " __TIME__;

#include <ctype.h>
#include <stdlib.h>
#include "config.h"
#include "main.h"
#include "stMemory.h"

#define BC_MAX_CONDITION_BREAKPOINTS 16
#define BC_MAX_CONDITIONS_PER_BREAKPOINT 4

/* defines for how the value needs to be accessed: */
typedef enum {
	BC_TYPE_NUMBER,		/* use as-is */
	BC_TYPE_ADDRESS		/* read from (Hatari) address as-is */
} bc_addressing_t;

/* defines for register & address widths as they need different accessors: */
typedef enum {
	BC_SIZE_BYTE,
	BC_SIZE_WORD,
	BC_SIZE_LONG
} bc_size_t;

typedef struct {
	bc_addressing_t type;
	bool is_indirect;
	bool using_dsp;
	char space;	/* DSP has P, X, Y address spaces */
	bc_size_t size;
	union {
		Uint32 number;
		/* e.g. registers have different sizes */
		Uint32 *addr32;
		Uint16 *addr16;
		Uint8 *addr8;
	} value;
	Uint32 mask;	/* <width mask> && <value mask> */
} bc_value_t;

typedef enum {
	BC_NEXT_AND,	/* next condition is ANDed to current one */
	BC_NEXT_NEW	/* next condition starts a new breakpoint */
} bc_next_t;

typedef struct {
	bc_value_t lvalue;
	bc_value_t rvalue;
	char comparison;
} bc_condition_t;

typedef struct {
	char *expression;
	bc_condition_t conditions[BC_MAX_CONDITIONS_PER_BREAKPOINT];
	int count;
} bc_breakpoint_t;

static bc_breakpoint_t BreakPointsCpu[BC_MAX_CONDITION_BREAKPOINTS];
static bc_breakpoint_t BreakPointsDsp[BC_MAX_CONDITION_BREAKPOINTS];
static int BreakPointCpuCount;
static int BreakPointDspCount;


/* ------------- breakpoint condition checking, internals ------------- */

/**
 * Return value of given size read from given DSP memory space address
 */
static Uint32 BreakCond_ReadDspMemory(Uint32 addr, bc_size_t size, char space)
{
#warning "TODO: BreakCond_ReadDspMemory()"
	fprintf(stderr, "TODO: BreakCond_ReadDspMemory()\n");
	return 0;
}

/**
 * Return value of given size read from given ST memory address
 */
static Uint32 BreakCond_ReadSTMemory(Uint32 addr, bc_size_t size)
{
	switch (size) {
	case BC_SIZE_LONG:
		return STMemory_ReadLong(addr);
	case BC_SIZE_WORD:
		return STMemory_ReadWord(addr);
	case BC_SIZE_BYTE:
		return STMemory_ReadByte(addr);
	default:
		fprintf(stderr, "ERROR: unknown ST addr value size %d!\n", size);
		abort();
	}
}


/**
 * Return Uint32 value according to given bc_value_t specification
 */
static Uint32 BreakCond_GetValue(const bc_value_t *bc_value)
{
	bc_addressing_t type = bc_value->type;
	bc_size_t size = bc_value->size;
	Uint32 value;
	
	switch (type) {
	case BC_TYPE_NUMBER:
		value = bc_value->value.number;
		break;
	case BC_TYPE_ADDRESS:
		switch (size) {
		case 4:
			value = *(bc_value->value.addr32);
			break;
		case 2:
			value = *(bc_value->value.addr16);
			break;
		case 1:
			value = *(bc_value->value.addr8);
			break;
		default:
			fprintf(stderr, "ERROR: unknown register size %d!\n", bc_value->size);
			abort();
		}
		break;
	default:
		fprintf(stderr, "ERROR: unknown breakpoint condition value type %d!\n", bc_value->type);
		abort();
	}
	if (bc_value->is_indirect) {
		if (bc_value->using_dsp) {
			value = BreakCond_ReadDspMemory(value, size, bc_value->space);
		} else {
			value = BreakCond_ReadSTMemory(value, size);
		}
	}
	return (value & bc_value->mask);
}


/**
 * Return true if any of the given breakpoint conditions match
 */
static bool BreakCond_MatchConditions(bc_condition_t *condition, int count)
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
 * Return true if any of the given condition breakpoints match
 */
static bool BreakCond_MatchBreakPoints(bc_breakpoint_t *bp, int count)
{
	int i;
	
	for (i = 0; i < count; bp++, i++) {
		if (BreakCond_MatchConditions(bp->conditions, bp->count)) {
			return true;
		}
	}
	return false;
}

/* ------------- breakpoint condition checking, public API ------------- */

/**
 * Return true if any of the CPU breakpoint/conditions match
 */
bool BreakCond_MatchCpu(void)
{
	return BreakCond_MatchBreakPoints(BreakPointsCpu, BreakPointCpuCount);
}

/**
 * Return true if any of the DSP breakpoint/conditions match
 */
bool BreakCond_MatchDsp(void)
{
	return BreakCond_MatchBreakPoints(BreakPointsDsp, BreakPointDspCount);
}

/**
 * Return number of condition breakpoints (i.e. whether there are any)
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

/**
 * If given string is register name (for DSP or CPU), set bc_value
 * fields accordingly and return true, otherwise return false.
 */
static bool BreakCond_ParseRegister(const char *regname, bc_value_t *bc_value)
{
#warning "TODO: BreakCond_ParseRegister()"
	fprintf(stderr, "TODO: BreakCond_ParseRegister()\n");
	if (bc_value->using_dsp) {
		/* check DSP register names */
		return false;
	}
	/* check CPU register names */
	return false;
}

/**
 * If given address is valid (for DSP or CPU), return true.
 */
static bool BreakCond_CheckAddress(Uint32 addr, bc_value_t *bc_value)
{
	if (bc_value->using_dsp) {
		if (addr > 0xFFFF) {
			return false;
		}
		return true;
	}
	if ((addr > STRamEnd && addr < 0xe00000) || addr > 0xff0000) {
		return false;
	}
	return false;
}


/* struct for passing around breakpoint conditions parsing state */
typedef struct {
	int arg;		/* current arg */
	int argc;		/* arg count */
	const char **args;	/* all args */
	const char *error;	/* error from parsing args */
} parser_state_t;

/**
 * Parse a breakpoint condition value.
 * Modify pstate according to parsing (arg index and error string).
 * Return true for success and false for error.
 */
bool BreakCond_ParseValue(parser_state_t *pstate, bc_value_t *bc_value)
{
	Uint32 number;
	const char *value;
	const char **args = pstate->args;
	int arg, skip = 1;

	arg = pstate->arg;
	if (pstate->argc - arg >= 3) {
		if (strcmp(args[arg], "(") == 0 &&
		    strcmp(args[arg+2], ")") == 0) {
			bc_value->is_indirect = true;
			arg += 1;
			skip = 2;
		}
	}
	pstate->arg = arg;	/* update for error return */

	value = args[arg];
	if (isalpha(value[0])) {
		if (BreakCond_ParseRegister(value, bc_value)) {
			/* address of register */
			bc_value->type = BC_TYPE_ADDRESS;
		} else {
			pstate->error = "invalid register name";
			return false;
		}
	} else {
		/* a number */
		bc_value->type = BC_TYPE_NUMBER;
		if (value[0] == '$') {
			if (sscanf(value, "%x", &number) != 1) {
				pstate->error = "invalid hexadecimal value";
				return false;
			}
		} else {
			if (sscanf(value, "%d", &number) != 1) {
				pstate->error = "invalid decimal value";
				return false;
			}
		}
		/* suitable as emulated memory address (indirect)? */
		if (bc_value->is_indirect &&
		    !BreakCond_CheckAddress(number, bc_value)) {
			pstate->error = "invalid address";
			return false;
		}
	}
	arg += skip;
	
#warning "TODO: handle lvalue/rvalue width and mask settings"
	fprintf(stderr, "TODO: handle lvalue/rvalue width and mask settings\n");
	
	pstate->arg = arg;
	return true;
}


/**
 * Parse given breakpoint conditions and append them to breakpoints.
 * Modify pstate according to parsing (arg index and error string).
 * Return true for success and false for error.
 */
bool BreakCond_ParseCondition(parser_state_t *pstate, bool bForDsp,
			      bc_condition_t *conditions, int *ccount)
{
	bc_condition_t condition;
	const char *comparison;
	int arg;

	if (pstate->arg >= pstate->argc) {
		pstate->error = "condition(s) missing";
		return false;
	}
	if (*ccount >= BC_MAX_CONDITIONS_PER_BREAKPOINT) {
		pstate->error = "no free breakpoints/conditions left";
		return false;
	}

	memset(&condition, 0, sizeof(bc_condition_t));
	if (bForDsp) {
		condition.lvalue.using_dsp = true;
		condition.rvalue.using_dsp = true;
	}
	
	if (!BreakCond_ParseValue(pstate, &(condition.lvalue))) {
		return false;
	}

	arg = pstate->arg;
	if (arg >= pstate->argc) {
		return "breakpoint comparison missing";
	}
	comparison = pstate->args[arg];
	switch (comparison[0]) {
	case '<':
	case '>':
	case '=':
	case '!':
		condition.comparison = comparison[0];
		break;
	default:
		pstate->error = "invalid comparison character";
		return false;
	}
	if (comparison[1]) {
		pstate->error = "trailing comparison character(s)";
		return false;
	}

	arg += 1;
	if (arg >= pstate->argc) {
		pstate->error = "right side missing";
		return false;
	}

	pstate->arg = arg;
	if (!BreakCond_ParseValue(pstate, &(condition.rvalue))) {
		return false;
	}
	
	/* done? */
	arg = pstate->arg;
	if (arg == pstate->argc) {
		conditions[(*ccount)++] = condition;
		return true;
	}
	if (strcmp(pstate->args[arg], "&") != 0) {
		pstate->error = "trailing content for breakpoint condition";
		return false;
	}

	/* recurse conditions parsing (conditions applied in reverse order) */
	if (!BreakCond_ParseCondition(pstate, bForDsp, conditions, ccount)) {
		return false;
	}
	conditions[(*ccount)++] = condition;
	return true;
}


/**
 * Tokenize given breakpoint expression to given parser struct.
 * Return a single string representation or NULL for an error.
 * On error, pstate->error contains the error message.
 */
static char *BreakCond_TokenizeExpression(const char *expression,
					  parser_state_t *pstate)
{
#warning "TODO: BreakCond_TokenizeExpression()"
	fprintf(stderr, "TODO: BreakCond_TokenizeExpression()\n");
	/* TODO, do instead:
	 * - count breakpoint condition separators in expression
	 * - alloc space for separated output (strlen(expression) +
	 *   tokens + pointer array to tokesn and store it to new breakpoint
	 * - copy expression to new breakpoint so that it's tokenized:
	 *   + spaces are removed
	 *   + each separator is followed by terminating nil
	 *   + each new token is added to args array
	 */
	memset(pstate, 0, sizeof(parser_state_t));
	return NULL;
}


/* ------------- breakpoint condition parsing, public API ------------ */

/**
 * Parse given breakpoint expression and store it.
 * Return true for success and false for failure.
 */
bool BreakCond_Parse(const char *expression, bool bForDsp)
{
	parser_state_t pstate;
	bc_breakpoint_t *bp;
	char *parsed;
	bool ok;

	if (bForDsp) {
		bp = &(BreakPointsDsp[BreakPointDspCount]);
	} else {
		bp = &(BreakPointsCpu[BreakPointCpuCount]);
	}
	parsed = BreakCond_TokenizeExpression(expression, &pstate);
	if (parsed) {
		bp->expression = parsed;
		ok = BreakCond_ParseCondition(&pstate, bForDsp,
					      bp->conditions, &(bp->count));
	} else {
		bp->expression = (char *)expression;
		ok = false;
	}
	if (ok) {
		if (bForDsp) {
			BreakPointDspCount++;
		} else {
			BreakPointCpuCount++;
		}
	} else {
		fprintf(stderr, bp->expression);
		/* TODO: underline the erronous pstate->arg token */
		fprintf(stderr, pstate.error);
	}
	return ok;
}


/**
 * List condition breakpoints
 */
void BreakCond_List(bool bForDsp)
{
}

/**
 * Remove condition breakpoint at given position
 */
void BreakCond_Remove(int position, bool bForDsp)
{
}


/* ---------------------------- test code ---------------------------- */

/* Test building can be done in hatari/src/ with:
 * gcc -I.. -Iincludes -Iuae-cpu $(sdl-config --cflags) -O -Wall breakcond.c
 * 
 * TODO: remove test stuff once done testing
 */
#if 1
Uint8 STRam[16*1024*1024];
Uint32 STRamEnd = 4*1024*1024;

int main(int argc, const char *argv[])
{
	const char dsptest[] = "($2000) = (d0) & pc = $fff";
	fprintf(stderr, "Parsing '%s'\n", dsptest);
	BreakCond_Parse(dsptest, true);

	/* parse cmd line args as conditional CPU breakpoints */
	while (--argc > 0) {
		argv++;
		fprintf(stderr, "Parsing '%s'\n", *argv);
		BreakCond_Parse(*argv, false);
	}
	return 0;
}
#endif
