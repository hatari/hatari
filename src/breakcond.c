/*
  Hatari - breakcond.c

  Test building can be done in hatari/src/ with:
  gcc -I.. -Iincludes -Iuae-cpu $(sdl-config --cflags) -O -Wall breakcond.c

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
const char BreakPoint_fileid[] = "Hatari breakpoint.c : " __DATE__ " " __TIME__;

#include <ctype.h>
#include <stdlib.h>
#include "config.h"
#include "main.h"
#include "stMemory.h"

#define TEST 1	/* TODO: remove this stuff once done testing */

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
	bc_next_t next;
} bc_condition_t;

#define BC_MAX_COUNT 16

static bc_condition_t BreakConditionsCpu[BC_MAX_COUNT];
static bc_condition_t BreakConditionsDsp[BC_MAX_COUNT];
static int BreakConditionsCpuCount;
static int BreakConditionsDspCount;


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
#ifndef TEST
	case BC_SIZE_LONG:
		return STMemory_ReadLong(addr);
	case BC_SIZE_WORD:
		return STMemory_ReadWord(addr);
	case BC_SIZE_BYTE:
		return STMemory_ReadByte(addr);
#endif
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
static bool BreakCond_Match(bc_condition_t *condition, int count)
{
	Uint32 lvalue, rvalue;
	bool hit;
	int i;
	
	for (i = 0; i < count; condition++, i++) {

		lvalue = BreakCond_GetValue(&(condition->lvalue));
		rvalue = BreakCond_GetValue(&(condition->rvalue));

		hit = false;
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
		if (hit) {
			if (condition->next == BC_NEXT_NEW) {
				/* condition ends & breakpoint matches */
				return true;
			}
			continue;
		}
		/* this breakpoint failed, try next one */
		while (condition->next != BC_NEXT_NEW && i < count) {
			condition++;
			i++;
		}
	}
	return false;
}

/* ------------- breakpoint condition checking, public API ------------- */

/**
 * Return true if any of the CPU breakpoint conditions match
 */
bool BreakCond_MatchCpu(void)
{
	return BreakCond_Match(BreakConditionsCpu, BreakConditionsCpuCount);
}

/**
 * Return true if any of the DSP breakpoint conditions match
 */
bool BreakCond_MatchDsp(void)
{
	return BreakCond_Match(BreakConditionsDsp, BreakConditionsDspCount);
}

/**
 * Return count of breakpoint conditions (i.e. whether there are any)
 */
int BreakCond_ConditionCount(bool bForDsp)
{
	if (bForDsp) {
		return BreakConditionsDspCount;
	} else {
		return BreakConditionsCpuCount;
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
static bool BreakCond_CheckAddress(Uint32 addr, bool bForDsp)
{
	if (bForDsp) {
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


/**
 * Parse a breakpoint condition value.
 * Modify number of parsed arguments given as pointer.
 * Return error message on error and NULL for success.
 */
static const char *
BreakCond_ParseValue(int nArgc, const char *psArgs[],
		     int *parsed, bc_value_t *bc_value)
{
	int pos = 0, arg = 1;
	const char *value;
	Uint32 number;
	
	if (nArgc >= 3) {
		if (strcmp(psArgs[0], "(") == 0 &&
		    strcmp(psArgs[2], ")") == 0) {
			bc_value->is_indirect = true;
			pos = 1;
			arg = 3;
		}
	}
	value = psArgs[pos];

	if (isalpha(value[0])) {
		if (BreakCond_ParseRegister(value, bc_value)) {
			/* address of register */
			bc_value->type = BC_TYPE_ADDRESS;
		} else {
			*parsed += arg;
			return "invalid register name";
		}
	} else {
		/* a number */
		bc_value->type = BC_TYPE_NUMBER;
		if (value[0] == '$') {
			if (sscanf(value, "%x", &number) != 1) {
				*parsed += arg;
				return "invalid hexadecimal value";
			}
		} else {
			if (sscanf(value, "%d", &number) != 1) {
				*parsed += arg;
				return "invalid decimal value";
			}
		}
		/* suitable as emulated memory address (indirect)? */
		if (bc_value->is_indirect &&
		    !BreakCond_CheckAddress(number, bc_value->using_dsp)) {
			parsed += arg;
			return "invalid address";
		}
	}
	arg = pos;
	
#warning "TODO: handle lvalue/rvalue width and mask settings"
	fprintf(stderr, "TODO: handle lvalue/rvalue width and mask settings\n");
	
	*parsed += arg;
	return NULL;
}


/**
 * Parse given breakpoint conditions and append them to breakpoints.
 * Return error string if there was an error in parsing them, NULL on OK.
 */
static const char *
BreakCond_ParseCondition(int nArgc, const char *psArgs[],
			 int *parsed, bool bForDsp,
			 int *count, bc_condition_t *conditions)
{
	bc_condition_t condition;
	const char *error;
	char comparison;

	if (!nArgc) {
		return "condition missing";
	}
	if (*count >= BC_MAX_COUNT) {
		return "no free breakpoints/conditions left";
	}

	memset(&condition, 0, sizeof(bc_condition_t));
	if (bForDsp) {
		condition.lvalue.using_dsp = true;
		condition.rvalue.using_dsp = true;
	}
	
	error = BreakCond_ParseValue(nArgc, psArgs,
				     parsed, &(condition.lvalue));
	if (error) {
		return error;
	}

	if (*parsed >= nArgc) {
		return "breakpoint comparison missing";
	}
	comparison = psArgs[*parsed][0];
	switch (comparison) {
	case '<':
	case '>':
	case '=':
	case '!':
		condition.comparison = comparison;
		break;
	default:
		return "invalid comparison character";
	}
	if (psArgs[*parsed][1]) {
		return "trailing comparison characters";
	}
	
	if (++*parsed >= nArgc) {
		return "right side missing";
	}

	error = BreakCond_ParseValue(nArgc-*parsed, psArgs+*parsed,
				     parsed, &(condition.rvalue));
	if (error) {
		return error;
	}
	
	/* done? */
	if (*parsed == nArgc) {
		condition.next = BC_NEXT_NEW;
		conditions[(*count)++] = condition;
		return NULL;
	}
	if (strcmp(psArgs[*parsed], "&") != 0) {
		return "trailing content for breakpoint condition";
	}

	/* continue condition parsing (applied in reverse order) */
	error = BreakCond_ParseCondition(nArgc-*parsed, &(psArgs[*parsed]),
					 parsed, bForDsp, count, conditions);
	if (error) {
		return error;
	}
	conditions[(*count)-1].next = BC_NEXT_AND;
	condition.next = BC_NEXT_NEW;
	conditions[(*count)++] = condition;
	return NULL;
}

/**
 * Parse given breakpoint expression and store it.
 * Return true for success and false for failure.
 */
bool BreakCond_Parse(const char *expression, bool bForDsp)
{
	int parsed = 0, nArgs = 0;
	const char **psArgs = NULL;
	const char *error;
#warning "TODO: BreakCond_Parse()"
	/* TODO:
	 * - count breakpoint condition separators in expression
	 * - alloc space for separated output (strlen(expression) +
	 *   tokens + pointer array to tokesn and store it to new breakpoint
	 * - copy expression to new breakpoint so that it's tokenized:
	 *   + spaces are removed
	 *   + each separator is followed by terminating nil
	 *   + each new token is added to args array
	 */
	if (bForDsp) {
		error = BreakCond_ParseCondition(nArgs, psArgs,
						 &parsed, bForDsp,
						 &BreakConditionsDspCount,
						 BreakConditionsDsp);
	} else {
		error = BreakCond_ParseCondition(nArgs, psArgs,
						 &parsed, bForDsp,
						 &BreakConditionsCpuCount,
						 BreakConditionsCpu);
	}
	/* TODO:
	 * - if there was an error:
	 *   + show stored expression
	 *   + underline the erronous token (based on parsed index)
	 *   + show error
	 */
	return (!error);
}

#ifdef TEST
int main(int argc, const char *argv[])
{
	while (--argc > 0) {
		argv++;
		fprintf(stderr, "Parsing '%s'\n", *argv);
		BreakCond_Parse(*argv, false);
	}
	BreakCond_Parse("($2000) = (d0) & pc = $fff", true);
	return 0;
}
#endif
