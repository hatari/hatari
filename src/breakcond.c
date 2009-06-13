/*
  Hatari - breakcond.c

  Copyright (c) 2009 by Eero Tamminen

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  breakcond.c - code for breakpoint conditions that can check variable
  and memory values against each other, mask them etc. before deciding
  whether the breakpoint should be triggered.  The syntax is:

  breakpoint = <expression> [ && <expression> [ && <expression> ] ... ]
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
  	pc = $64543  &&  ($ff820).w & 3 = (a0)  &&  d0.l = 123
*/
const char BreakCond_fileid[] = "Hatari breakcond.c : " __DATE__ " " __TIME__;

#include <ctype.h>
#include <stdlib.h>
#include "config.h"
#include "main.h"
#include "stMemory.h"

/* set to 1 to enable parsing function tracing / debug output */
#define DEBUG 0

#define BC_MAX_CONDITION_BREAKPOINTS 16
#define BC_MAX_CONDITIONS_PER_BREAKPOINT 4

/* defines for how the value needs to be accessed: */
typedef enum {
	BC_TYPE_NUMBER,		/* use as-is */
	BC_TYPE_ADDRESS		/* read from (Hatari) address as-is */
} bc_addressing_t;

/* defines for register & address widths as they need different accessors: */
typedef enum {
	BC_SIZE_UNKNOWN,
	BC_SIZE_BYTE,
	BC_SIZE_WORD,
	BC_SIZE_LONG
} bc_size_t;

typedef struct {
	bc_addressing_t type;
	bool is_indirect;
	bool use_dsp;
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
	int ccount;
} bc_breakpoint_t;

static bc_breakpoint_t BreakPointsCpu[BC_MAX_CONDITION_BREAKPOINTS];
static bc_breakpoint_t BreakPointsDsp[BC_MAX_CONDITION_BREAKPOINTS];
static int BreakPointCpuCount;
static int BreakPointDspCount;


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
	bc_size_t size = bc_value->size;
	Uint32 value;
	
	switch (bc_value->type) {
	case BC_TYPE_NUMBER:
		value = bc_value->value.number;
		break;
	case BC_TYPE_ADDRESS:
		switch (size) {
		case BC_SIZE_LONG:
			value = *(bc_value->value.addr32);
			break;
		case BC_SIZE_WORD:
			value = *(bc_value->value.addr16);
			break;
		case BC_SIZE_BYTE:
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
		if (bc_value->use_dsp) {
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
		if (BreakCond_MatchConditions(bp->conditions, bp->ccount)) {
#if DEBUG
			fprintf(stderr, "Breakpoint '%s' matched.\n", bp->expression);
#endif
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
	ENTERFUNC(("BreakCond_ParseRegister('%s')\n", regname));
	if (bc_value->use_dsp) {
		/* check DSP register names */
		EXITFUNC(("-> false (DSP TODO)\n"));
		return false;
	}
	/* check CPU register names */
	if (strcmp(regname, "a0") == 0 || strcmp(regname, "d0") == 0) {
		/* DUMMY */
		extern Uint32 DummyRegister;
		bc_value->value.addr32 = &DummyRegister;
		bc_value->size = BC_SIZE_LONG;
		bc_value->mask = 0xffffffff;
		EXITFUNC(("-> true (CPU TODO)\n"));
		return true;
	}
	EXITFUNC(("-> false (CPU TODO)\n"));
	return false;
}

/**
 * If given address is valid (for DSP or CPU), return true.
 */
static bool BreakCond_CheckAddress(Uint32 addr, bc_value_t *bc_value)
{
	ENTERFUNC(("BreakCond_CheckAddress(%x)\n", addr));
	if (bc_value->use_dsp) {
		if (addr > 0xFFFF) {
			EXITFUNC(("-> false (DSP)\n"));
			return false;
		}
		EXITFUNC(("-> true (DSP)\n"));
		return true;
	}
	if ((addr > STRamEnd && addr < 0xe00000) || addr > 0xff0000) {
		EXITFUNC(("-> false (CPU)\n"));
		return false;
	}
	EXITFUNC(("-> true (CPU)\n"));
	return true;
}


/* struct for passing around breakpoint conditions parsing state */
typedef struct {
	int arg;		/* current arg */
	int argc;		/* arg count */
	const char **argv;	/* arg pointer array (+ strings) */
	const char *error;	/* error from parsing args */
} parser_state_t;


/**
 * Parse a number, decimal unless prefixed with '$' which signifies hex.
 * Modify pstate according to parsing (arg index and error string).
 * Return true for success and false for error.
 */
static bool BreakCond_ParseNumber(parser_state_t *pstate, const char *value, Uint32 *number)
{
	ENTERFUNC(("BreakCond_ParseNumber('%s'), arg:%d\n", value, pstate->arg));
	if (value[0] == '$') {
		if (sscanf(value+1, "%x", number) != 1) {
			pstate->error = "invalid hexadecimal value";
			EXITFUNC(("-> false\n"));
			return false;
		}
	} else {
		if (sscanf(value, "%u", number) != 1) {
			pstate->error = "invalid value";
			EXITFUNC(("-> false\n"));
			return false;
		}
	}
	EXITFUNC(("-> number:%x, true\n", *number));
	return true;
}


/**
 * Check for and parse a condition value size.
 * Modify pstate according to parsing (arg index and error string).
 * Return true for no or successfully parsed size and false for error.
 */
static bool BreakCond_ParseSizeModifier(parser_state_t *pstate, bc_value_t *bc_value)
{
	int size;

	ENTERFUNC(("BreakCond_ParseSizeModifier()\n"));
	if (pstate->arg+2 > pstate->argc ||
	    strcmp(pstate->argv[pstate->arg], ".") != 0) {
		EXITFUNC(("arg:%d -> true (missing)\n", pstate->arg));
		return true;
	}
	pstate->arg++;
	switch (pstate->argv[pstate->arg][0]) {
	case 'l':
		size = BC_SIZE_LONG;
		break;
	case 'w':
		size = BC_SIZE_WORD;
		break;
	case 'b':
		size = BC_SIZE_BYTE;
		break;
	default:
		pstate->error = "invalid size modifier";
		EXITFUNC(("arg:%d -> false\n", pstate->arg));
		return false;
	}
	if (pstate->argv[pstate->arg][1]) {
		pstate->error = "invalid size modifier";
		EXITFUNC(("arg:%d -> false\n", pstate->arg));
		return false;
	}
	bc_value->size = size;
	EXITFUNC(("arg:%d -> size:%d, true\n", pstate->arg, size));
	pstate->arg++;
	return true;
}


/**
 * Check for and parse a condition value mask.
 * Modify pstate according to parsing (arg index and error string).
 * Return true for no or successfully parsed mask and false for error.
 */
static bool BreakCond_ParseMaskModifier(parser_state_t *pstate, bc_value_t *bc_value)
{
	ENTERFUNC(("BreakCond_ParseMaskModifier()\n"));
	if (pstate->arg+2 > pstate->argc ||
	    strcmp(pstate->argv[pstate->arg], "&") != 0) {
		EXITFUNC(("arg:%d -> true (missing)\n", pstate->arg));
		return true;
	}
	pstate->arg++;
	if (!BreakCond_ParseNumber(pstate, pstate->argv[pstate->arg], &(bc_value->mask))) {
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
bool BreakCond_ParseValue(parser_state_t *pstate, bc_value_t *bc_value)
{
	Uint32 number;
	const char *value;
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

	/* set default modifiers, register parsing can change them later */
	bc_value->size = BC_SIZE_LONG;
	bc_value->mask = 0xffffffff;

	value = pstate->argv[pstate->arg];
	/* parse direct or indirect value */
	if (isalpha(value[0])) {
		if (BreakCond_ParseRegister(value, bc_value)) {
			/* address of register */
			bc_value->type = BC_TYPE_ADDRESS;
		} else {
			pstate->error = "invalid register name";
			EXITFUNC(("arg:%d -> false\n", pstate->arg));
			return false;
		}
	} else {
		/* a number */
		bc_value->type = BC_TYPE_NUMBER;
		if (!BreakCond_ParseNumber(pstate, value, &number)) {
			EXITFUNC(("arg:%d -> false\n", pstate->arg));
			return false;
		}
		/* suitable as emulated memory address (indirect)? */
		if (bc_value->is_indirect &&
		    !BreakCond_CheckAddress(number, bc_value)) {
			pstate->error = "invalid address";
			EXITFUNC(("arg:%d -> false\n", pstate->arg));
			return false;
		}
	}
	pstate->arg += skip;

	/* parse modifiers */
	if (!BreakCond_ParseSizeModifier(pstate, bc_value)) {
		pstate->error = "invalid value size modifier";
		EXITFUNC(("arg:%d -> false\n", pstate->arg));
		return false;
	}
	if (!BreakCond_ParseMaskModifier(pstate, bc_value)) {
		pstate->error = "invalid value mask";
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
char BreakCond_ParseComparison(parser_state_t *pstate)
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
 * Parse given breakpoint conditions and append them to breakpoints.
 * Modify pstate according to parsing (arg index and error string).
 * Return number of added conditions or zero for failure.
 */
int BreakCond_ParseCondition(parser_state_t *pstate, bool bForDsp,
			     bc_condition_t *conditions, int ccount)
{
	bc_condition_t condition;

	ENTERFUNC(("BreakCond_ParseCondition(...)\n"));
	if (ccount >= BC_MAX_CONDITIONS_PER_BREAKPOINT) {
		pstate->error = "max number of conditions exceeded";
		EXITFUNC(("-> false (no conditions free)\n"));
		return false;
	}

	/* setup condition */
	memset(&condition, 0, sizeof(bc_condition_t));
	if (bForDsp) {
		condition.lvalue.use_dsp = true;
		condition.rvalue.use_dsp = true;
	}

	/* parse condition */
	if (!BreakCond_ParseValue(pstate, &(condition.lvalue))) {
		EXITFUNC(("-> false\n"));
		return false;
	}
	condition.comparison = BreakCond_ParseComparison(pstate);
	if (!condition.comparison) {
		EXITFUNC(("-> false\n"));
		return false;
	}
	if (!BreakCond_ParseValue(pstate, &(condition.rvalue))) {
		EXITFUNC(("-> false\n"));
		return false;
	}
	
	/* new condition */
	conditions[ccount++] = condition;

	/* continue with next condition? */
	if (pstate->arg == pstate->argc) {
		EXITFUNC(("-> true (condition %d)\n", ccount-1));
		return true;
	}
	if (strcmp(pstate->argv[pstate->arg], "&&") != 0) {
		pstate->error = "trailing content for breakpoint condition";
		EXITFUNC(("-> false\n"));
		return false;
	}
	pstate->arg++;

	/* recurse conditions parsing */
	ccount = BreakCond_ParseCondition(pstate, bForDsp, conditions, ccount);
	if (!ccount) {
		EXITFUNC(("-> false\n"));
		return false;
	}
	EXITFUNC(("-> true (condition %d)\n", ccount-1));
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
			if (!(isalnum(*src) || isblank(*src) || *src == '$')) {
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


/* ------------- breakpoint condition parsing, public API ------------ */

/**
 * Parse given breakpoint expression and store it.
 * Return true for success and false for failure.
 */
bool BreakCond_Parse(const char *expression, bool bForDsp)
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
		fprintf(stderr, "%s condition breakpoint %d added.\n",
			name, *bcount);
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

	fprintf(stderr, "Conditional %s breakpoints:\n", name);
	for (i = 1; i <= bcount; bp++, i++) {
		fprintf(stderr, "%d: %s\n", i, bp->expression);
	}
}

/**
 * Remove condition breakpoint at given position
 */
void BreakCond_Remove(int position, bool bForDsp)
{
	const char *name;
	bc_breakpoint_t *bp;
	int *bcount, offset;
	
	bcount = BreakCond_GetListInfo(&bp, &name, bForDsp);
	if (position < 1 || position > *bcount) {
		fprintf(stderr, "ERROR: No such %s breakpoint.\n", name);
		return;
	}
	offset = position - 1;
	fprintf(stderr, "Removed %s breakpoint %d:\n  %s\n",
		name, position, bp[offset].expression);
	free(bp[offset].expression);
	if (position < *bcount) {
		memmove(bp+offset, bp+position,
			(*bcount-position)*sizeof(bc_breakpoint_t));
	}
	(*bcount)--;
}


/* ---------------------------- test code ---------------------------- */

/* Test building can be done in hatari/src/ with:
 * gcc -I.. -Iincludes -Iuae-cpu $(sdl-config --cflags) -O -Wall -g breakcond.c
 * 
 * TODO: remove test stuff once done testing
 */
#if 1
Uint8 STRam[16*1024*1024];
Uint32 STRamEnd = 4*1024*1024;

Uint32 DummyRegister;

int main(int argc, const char *argv[])
{
	const char *should_fail[] = {
		"",
		" = ",
		" a0 d0 ",
		"gggg=a0",
		"=a=b=",
		"a0=d0=20",
		"a0=d || 0=20",
		"a0=d & 0=20",
		".w&3=2",
		"foo().w=bar()",
		"(a0.w=d0.l)",
		"(a0&3)=20",
		"20=(a0.w)",
		"()&=d0",
		"d0=().w",
		"255 & 3 = (d0) & && 2 = 2",
		/* more than BC_MAX_CONDITIONS_PER_BREAKPOINT conditions */
		"1=1 && 2=2 && 3=3 && 4=4 && 5=5",
		NULL
	};
	const char *should_pass[] = {
		" 200 = ( $ 200 ) ",
		" ( 200 ) = $200 ",
		"a0=d0",
		"(a0)=(d0)",
		"(d0).w=(a0).b",
		"(a0).w&3=(d0)&&d0=1",
		" ( a 0 ) . w  &  1 = ( d 0 ) & 1 &&  d 0 = 3 ",
		"a0=1 && (d0)&2=(a0).w && ($00ff00).w&1=1",
		NULL
	};
	const char *test;
	bool use_dsp = false;
	int i, count, tests = 0, errors = 0;

	/* first automated tests... */
	
	fprintf(stderr, "\nShould FAIL:\n");
	for (i = 0; (test = should_fail[i]); i++) {
		fprintf(stderr, "-----------------\n- parsing '%s'\n", test);
		if (BreakCond_Parse(test, use_dsp)) {
			fprintf(stderr, "***ERROR***: should have failed\n");
			errors++;
		}
	}
	tests += i;
	fprintf(stderr, "-----------------\n\n");
	BreakCond_List(use_dsp);
	
	fprintf(stderr, "\nShould PASS:\n");
	for (i = 0; (test = should_pass[i]); i++) {
		fprintf(stderr, "-----------------\n- parsing '%s'\n", test);
		if (!BreakCond_Parse(test, use_dsp)) {
			fprintf(stderr, "***ERROR***: should have passed\n");
			errors++;
		}
	}
	tests += i;
	fprintf(stderr, "-----------------\n\n");
	BreakCond_List(use_dsp);
	fprintf(stderr, "\n");

	STRam[0] = 1; /* make indirect equality checks for zeroed RAM fail */
	if (BreakCond_MatchCpu() || BreakCond_MatchDsp()) {
		fprintf(stderr, "-> Breakpoints matched\n\n");
	}

	/* try removing everything from alternate ends */
	while ((count = BreakCond_BreakPointCount(use_dsp))) {
		BreakCond_Remove(count, use_dsp);
		BreakCond_Remove(1, use_dsp);
	}
	BreakCond_List(use_dsp);
	fprintf(stderr, "-----------------\n");

	/* ...last parse cmd line args */
	if (argc > 1) {
		fprintf(stderr, "\nCommand line breakpoints:\n");
		for (argv++; --argc > 0; argv++) {
			fprintf(stderr, "-----------------\n- parsing '%s'\n", *argv);
			BreakCond_Parse(*argv, use_dsp);
		}
		fprintf(stderr, "-----------------\n\n");
		BreakCond_List(use_dsp);

		if (BreakCond_MatchCpu() || BreakCond_MatchDsp()) {
			fprintf(stderr, "-> Breakpoints matched\n\n");
		}
	}
	if (errors) {
		fprintf(stderr, "\n***Detected %d ERRORs in %d automated tests!***\n\n",
			errors, tests);
	}
	return 0;
}
#endif
