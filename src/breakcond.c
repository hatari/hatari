/*
  Hatari - breakcond.c
  
  Test building can be done in hatari/src/ with:
  gcc -I.. -Iincludes -Iuae-cpu $(sdl-config --cflags) -O -Wall -c breakcond.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  breakcond.c - code for breakpoint conditions that can check variable
  and memory values against each other, mask them etc. before deciding
  whether the breakpoint should be triggered.  The syntax is:
 
  breakpoint = <expression> N*[ && <expression> ]
  expression = <value>[.width] [& <number>] <condition> <value>[.width]
  
  where:
  	value = [(] <register-name | address | number> [)]
  	condition = '<' | '>' | '=' | '!'
  	width = 'b' | 'w' | 'l'
  
  If the value is in parenthesis like in "(ff820)" or "(a0)", then
  the used value will be read from the memory address pointed by it.
*/
const char BreakPoint_fileid[] = "Hatari breakpoint.c : " __DATE__ " " __TIME__;

#include <ctype.h>
#include <stdlib.h>
#include "config.h"
#include "main.h"
#include "stMemory.h"


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
	bc_size_t size;
	union {
		Uint32 number;
		/* registers have different sizes */
		Uint32 *addr32;
		Uint16 *addr16;
		Uint8 *addr8;
	} value;
	Uint32 (*read_addr)(Uint32 addr, bc_size_t size);
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

static bc_condition_t BreakConditions[16];
static int BreakConditionsCount;

/* --------------- breakpoint condition checking --------------- */

/**
 * Return value read from given DSP memory address of given size
 */
static Uint32 BreakCond_ReadDspMemory(Uint32 addr, bc_size_t size)
{
#warning "TODO: BreakCond_ReadDspMemory()"
	fprintf(stderr, "TODO: BreakCond_ReadDspMemory()\n");
	return 0;
}

/**
 * Return value read from given ST memory address of given size
 */
static Uint32 BreakCond_ReadSTMemory(Uint32 addr, bc_size_t size)
{
	switch (size) {
	case BC_SIZE_BYTE:
		return STMemory_ReadByte(addr);
	case BC_SIZE_WORD:
		return STMemory_ReadWord(addr);
	case BC_SIZE_LONG:
		return STMemory_ReadLong(addr);
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
		case 1:
			value = *(bc_value->value.addr8);
			break;
		case 2:
			value = *(bc_value->value.addr16);
			break;
		case 4:
			value = *(bc_value->value.addr32);
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
		value = bc_value->read_addr(value, size);
	}
	return (value & bc_value->mask);
}


/**
 * Return true if any of the breakpoint conditions match
 */
bool BreakCond_Match(void)
{
	bc_condition_t *condition;
	Uint32 lvalue, rvalue;
	bool hit;
	int i;
	
	if (!BreakConditionsCount) {
		return false;
	}
	condition = BreakConditions;
	for (i = 0; i < BreakConditionsCount; condition++, i++) {

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
		while (condition->next != BC_NEXT_NEW &&
		       i < BreakConditionsCount) {
			condition++;
			i++;
		}
	}
	return false;
}

/* --------------- breakpoint condition parsing --------------- */

/**
 * If given string is register name (for DSP or CPU), set bc_value
 * fields accordingly and return true, otherwise return false.
 * 
 * TODO: implement!
 */
static bool BreakCond_ParseRegister(const char *str, bc_value_t *bc_value, bool bForDsp)
{
#warning "TODO: BreakCond_ParseRegister()"
	fprintf(stderr, "TODO: BreakCond_ParseRegister()\n");
	return false;
}

/**
 * If given address is valid (for DSP or CPU), return true.
 * 
 * TODO: implement!
 */
static bool BreakCond_CheckAddress(Uint32 addr, bool bForDsp)
{
#warning "TODO: BreakCond_CheckAddress()"
	fprintf(stderr, "TODO: BreakCond_CheckAddress()\n");
	return false;
}

/**
 * Parse a breakpoint condition value.
 * Return number of parsed arguments or zero for failure.
 */
int BreakCond_ParseValue(int nArgc, char *psArgs[], bc_value_t *bc_value, bool bForDsp)
{
	int pos = 0, arg = 1;
	bool indirect = false;
	Uint32 number;
	
	if (nArgc >= 3) {
		if (strcmp(psArgs[0], "(") == 0 &&
		    strcmp(psArgs[2], ")") == 0) {
			indirect = true;
			pos = 1;
			arg = 3;
		}
	}
	if (BreakCond_ParseRegister(psArgs[pos], bc_value, bForDsp)) {
		/* address of register */
		bc_value->type = BC_TYPE_ADDRESS;
	} else {
		/* a number (which can be used as emulated memory address) */
		bc_value->type = BC_TYPE_NUMBER;
		if (sscanf(psArgs[1], "%x", &number) != 1) {
			return 0;
		}
		/* suitable as emulated memory address (indirect)? */
		if (indirect && ! BreakCond_CheckAddress(number, bForDsp)) {
				return 0;
		}
	}
	
#warning "TODO: handle lvalue/rvalue width and mask settings"
	fprintf(stderr, "TODO: handle lvalue/rvalue width and mask settings\n");
	
	if (bForDsp) {
		bc_value->read_addr = BreakCond_ReadDspMemory;
	} else {
		bc_value->read_addr = BreakCond_ReadSTMemory;
	}
	bc_value->is_indirect = indirect;
	return arg;
}


/**
 * Parse given breakpoint conditions and append them to breakpoints.
 * Return true if conditions were parsed fine, otherwise return false.
 */
bool BreakCond_Parse(int nArgc, char *psArgs[], bool bForDsp)
{
	bc_condition_t condition;
	char comparison;
	int arg, parsed;
	bool ok;

	if (BreakConditionsCount >= ARRAYSIZE(BreakConditions)) {
		fprintf(stderr, "ERROR: no free breakpoints left, remove some first!\n");
		return false;
	}
	memset(&condition, 0, sizeof(bc_condition_t));

	parsed = BreakCond_ParseValue(nArgc, psArgs, &(condition.lvalue), bForDsp);
	if (!parsed) {
		fprintf(stderr, "ERROR: parsing breakpoint comparison left side failed!\n");
		return false;
	}
	arg = parsed;

	if (arg+2 <= nArgc) {
		fprintf(stderr, "ERROR: breakpoint comparison and/or right side missing!\n");
		return false;
	}
	comparison = psArgs[arg][0];
	switch (comparison) {
	case '<':
	case '>':
	case '=':
	case '!':
		condition.comparison = comparison;
		break;
	default:
		fprintf(stderr, "ERROR: invalid comparison character '%c'\n", comparison);
		return false;
	}
	if (psArgs[arg][1]) {
		fprintf(stderr, "ERROR: trailing comparison characters: '%s'\n", psArgs[arg]);
		return false;
	}
	
	parsed = BreakCond_ParseValue(nArgc+arg, &(psArgs[arg]), &(condition.rvalue), bForDsp);
	if (!parsed) {
		fprintf(stderr, "ERROR: parsing breakpoint comparison right side failed!\n");
		return false;
	}
	arg += parsed;
	
	/* done? */
	if (arg == nArgc) {
		condition.next = BC_NEXT_NEW;
		BreakConditions[BreakConditionsCount++] = condition;
		return true;
	}
	if (strcmp(psArgs[arg], "&&") != 0) {
		fprintf(stderr, "ERROR: trailing content for breakpoint condition!\n");
		return false;
	}

	/* continue condition parsing (applied in reverse order) */
	ok = BreakCond_Parse(nArgc+arg, &(psArgs[arg]), bForDsp);
	if (ok) {
		BreakConditions[BreakConditionsCount-1].next = BC_NEXT_AND;
		BreakConditions[BreakConditionsCount++] = condition;
	}
	return ok;
}
