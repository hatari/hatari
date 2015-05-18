/*
  Hatari - evaluate.c

  Copyright (C) 1994, 2009-2014 by Eero Tamminen

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  calculate.c - parse numbers, number ranges and expressions. Supports
  most unary and binary operations. Parenthesis are used for indirect
  ST RAM value addressing.

  Originally based on code from my Clac calculator MiNT filter version.
*/
const char Eval_fileid[] = "Hatari calculate.c : " __DATE__ " " __TIME__;

#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL_types.h>
#include <inttypes.h>
#include "breakcond.h"
#include "configuration.h"
#include "dsp.h"
#include "debugcpu.h"
#include "evaluate.h"
#include "main.h"
#include "m68000.h"
#include "stMemory.h"
#include "symbols.h"

/* define which character indicates which type of number on expression  */
#define PREFIX_BIN '%'                            /* binary decimal     */
#define PREFIX_DEC '#'                             /* normal decimal    */
#define PREFIX_HEX '$'                             /* hexadecimal       */

/* define error codes                                                   */
#define CLAC_EXP_ERR "No expression given"
#define CLAC_GEN_ERR "Syntax error"
#define CLAC_PAR_ERR "Mismatched parenthesis"
#define CLAC_DEF_ERR "Undefined result (1/0)"
#define CLAC_STK_ERR "Operation/value stack full"
#define CLAC_OVF_ERR "Overflow"
#define CLAC_OVR_ERR "Mode overflow"
#define CLAC_PRG_ERR "Internal program error"

/* define internal allocation sizes (should be enough ;-)		*/
#define PARDEPTH_MAX	16		/* max. parenth. nesting depth	*/
#define OSTACK_MAX	64		/* size of the operator stack	*/
#define VSTACK_MAX	64		/* size of the value stack	*/

/* operation with lowest precedence, used to finish calculations */
#define LOWEST_PREDECENCE '|'

/* globals + function identifier stack(s)				*/
static struct {
	const char *error;		/* global error code		*/
	bool valid;			/* value validation		*/
} id = {0, 0};

/* parenthesis and function stacks					*/
static struct {
	int idx;			/* parenthesis level		*/
	int max;			/* maximum idx			*/
	int opx[PARDEPTH_MAX + 1];	/* current op index for par	*/
	int vax[PARDEPTH_MAX + 1];	/* current val index for par	*/
} par = {0, PARDEPTH_MAX, {0}, {0}};

static struct {					/* operator stack	*/
	int idx;
	int max;
	char buf[OSTACK_MAX + 1];
} op = {0, OSTACK_MAX, ""};

static struct value_stk {			/* value stack	*/
	int idx;
	int max;
	long long buf[VSTACK_MAX + 1];
} val = {0, VSTACK_MAX, {0}};

/* -------------------------------------------------------------------- */
/* macros								*/

/* increment stack index and put value on stack (ie. PUSH)		*/
#define PUSH(stk,val) \
	if((stk).idx < (stk).max) {		\
		(stk).idx += 1;			\
		(stk).buf[(stk).idx] = (val);	\
	} else {				\
		id.error = CLAC_STK_ERR;	\
	}

/* -------------------------------------------------------------------- */
/* declare subfunctions							*/

/* parse in-between operations	*/
static void operation(long long value, char op);
/* parse unary operators	*/
static void unary (char op);
/* apply a prefix to a value */
static void apply_prefix(void);
/* juggle stacks, if possible	*/
static void eval_stack(void);
/* operator -> operator level	*/
static int get_level(int stk_offset);
/* evaluate operation		*/
static long long apply_op(char op, long long x, long long y);

/* increase parenthesis level	*/
static void open_bracket(void);
/* decrease parenthesis level	*/
static long long close_bracket(long long x);


/**
 * Parse & set an (unsigned) number, assuming it's in the configured
 * default number base unless it has a prefix:
 * - '$' / '0x' / '0h' => hexadecimal
 * - '#' / '0d' => normal decimal
 * - '%' / '0b' => binary decimal
 * - '0o' => octal decimal
 * Return how many characters were parsed or zero for error.
 */
static int getNumber(const char *str, Uint32 *number, int *nbase)
{
	char *end;
	const char *start = str;
	int base = ConfigureParams.Debugger.nNumberBase;
	unsigned long int value;

	if (!str[0]) {
		fprintf(stderr, "Value missing!\n");
		return 0;
	}
	
	/* determine correct number base */
	if (str[0] == '0') {

		/* 0x & 0h = hex, 0d = dec, 0o = oct, 0b = bin ? */
		switch(str[1]) {
		case 'b':
			base = 2;
			break;
		case 'o':
			base = 8;
			break;
		case 'd':
			base = 10;
			break;
		case 'h':
		case 'x':
			base = 16;
			break;
		default:
			str -= 2;
		}
		str += 2;
	}
	else if (!isxdigit((unsigned char)str[0])) {

		/* doesn't start with (hex) number -> is it prefix? */
		switch (*str++) {
		case PREFIX_BIN:
			base = 2;
			break;
		case PREFIX_DEC:
			base = 10;
			break;
		case PREFIX_HEX:
			base = 16;
			break;
		default:
			fprintf(stderr, "Unrecognized number prefix in '%s'!\n", start);
			return 0;
		}
	}
	*nbase = base;

	/* parse number */
	errno = 0;
	value = strtoul(str, &end, base);
	if (errno == ERANGE && value == LONG_MAX) {
		fprintf(stderr, "Overflow with value '%s'!\n", start);
		return 0;
	}
	if ((errno != 0 && value == 0) || end == str) {
		fprintf(stderr, "Invalid value '%s'!\n", start);
		return 0;
	}
	*number = value;
	return end - start;
}


/**
 * Parse unsigned register/symbol/number value and set it to "number"
 * and the number base used for parsing to "base".
 * Return how many characters were parsed or zero for error.
 */
static int getValue(const char *str, Uint32 *number, int *base, bool bForDsp)
{
	char name[64];
	const char *end;
	Uint32 mask, *addr;
	int len;

	for (end = str; *end == '_' || isalnum((unsigned char)*end); end++);

	len = end-str;
	if (len >= (int)sizeof(name)) {
		fprintf(stderr, "ERROR: symbol name at '%s' too long (%d chars)\n", str, len);
		return 0;
	}
	memcpy(name, str, len);
	name[len] = '\0';

	*base = 0; /* no base (e.g. variable) */

	/* internal Hatari variable? */
	if (BreakCond_GetHatariVariable(name, number)) {
		return len;
	}

	if (bForDsp) {
		int regsize = DSP_GetRegisterAddress(name, &addr, &mask);
		/* DSP register or symbol? */
		switch (regsize) {
		case 16:
			*number = (*((Uint16*)addr) & mask);
			return len;
		case 32:
			*number = (*addr & mask);
			return len;
		default:
			if (Symbols_GetDspAddress(SYMTYPE_ALL, name, number)) {
				return len;
			}
		}
	} else {
		/* a special case CPU register? */
		if (strcasecmp(name, "PC") == 0) {
			*number = M68000_GetPC();
			return len;
		}
		if (strcasecmp(name, "SR") == 0) {
			*number = M68000_GetSR();
			return len;
		}
		/* a normal CPU  register or symbol? */
		if (DebugCpu_GetRegisterAddress(name, &addr)) {
			*number = *addr;
			return len;
		}
		if (Symbols_GetCpuAddress(SYMTYPE_ALL, name, number)) {
			return len;
		}
	}

	/* none of above, assume it's a number */
	return getNumber(str, number, base);
}


/* Check that number string is OK and isn't followed by unrecognized
 * character (last char char is zero). If not, complain about it.
 * Return true for success and false for failure.
 */
static bool isNumberOK(const char *str, int offset, int base)
{
	const char *basestr;

	if (!offset) {
		return false;
	}
	if (!str[offset]) {
		/* no extra chars after the parsed part */
		return true;
	}
	switch (base) {
	case 0:
		fprintf(stderr, "Name '%s' contains non-alphanumeric characters!\n", str);
		return false;
	case 2:
		basestr = "binary";
		break;
	case 8:
		basestr = "octal";
		break;
	case 10:
		basestr = "decimal";
		break;
	case 16:
		basestr = "hexadecimal";
		break;
	default:
		basestr = "unknown";
	}
	fprintf(stderr, "Extra characters in %s based number '%s'!\n", basestr, str);
	return false;
}

/**
 * Parse & set an (unsigned) number, assume it's in the configured
 * default number base unless it has a suitable prefix.
 * Return true for success and false for failure.
 */
bool Eval_Number(const char *str, Uint32 *number)
{
	int offset, base;
	/* TODO: add CPU/DSP flag and use getValue() instead of getNumber()
	 * like getRange() does, so that user can use variable names in
	 * addition to numbers.
	 */
	offset = getNumber(str, number, &base);
	if (!offset)
		return false;
	else
		return isNumberOK(str, offset, base);
}


/**
 * Parse an address range, eg. "$fa0000[-$fa0100]" or "pc[-a0]" and
 * output appropriate warnings if range or values are invalid.
 * Address can also be a register/variable/symbol name.
 * returns:
 * -1 if invalid address or range,
 *  0 if single address,
 * +1 if a range.
 */
int Eval_Range(char *str1, Uint32 *lower, Uint32 *upper, bool fordsp)
{
	int offset, base, ret;
	bool fDash = false;
	char *str2 = str1;

	while (*str2) {
		if (*str2 == '-') {
			*str2++ = '\0';
			fDash = true;
			break;
		}
		str2++;
	}

	offset = getValue(str1, lower, &base, fordsp);
	if (offset == 0 || !isNumberOK(str1, offset, base)) {
		/* first number not OK */
		fprintf(stderr,"Invalid address value '%s'!\n", str1);
		ret = -1;
	} else {
		/* first number OK */
		ret = 0;
	}
	if (fDash) {
		offset = getValue(str2, upper, &base, fordsp);
		if (offset == 0 || !isNumberOK(str2, offset, base)) {
			/* second number not OK */
			fprintf(stderr, "Invalid address value '%s'!\n", str2);
			ret = -1;
		} else {
			if (*lower > *upper) {
				fprintf(stderr,"Invalid range ($%x > $%x)!\n", *lower, *upper);
				/* not a range */
				ret = -1;
			} else {
				/* second number & range OK */
				ret = 1;
			}
		}
		*--str2 = '-';
	}
	return ret;
}


/**
 * Evaluate expression. bForDsp determines which registers and symbols
 * are interpreted. Sets given value and parsing offset.
 * Return error string or NULL for success.
 */
const char* Eval_Expression(const char *in, Uint32 *out, int *erroff, bool bForDsp)
{
	/* in	 : expression to evaluate				*/
	/* out	 : final parsed value					*/
	/* value : current parsed value					*/
	/* mark	 : current character in expression			*/
	/* valid : expression validation flag, set when number parsed	*/
	/* end	 : 'expression end' flag				*/
	/* offset: character offset in expression			*/

	long long value;
	int dummy, offset = 0;
	char mark;
	
	/* Uses global variables:	*/

	par.idx = 0;			/* parenthesis stack pointer	*/
	par.opx[0] = par.vax[0] = 0;	/* additional stack pointers	*/
	op.idx = val.idx = -1;

	id.error = NULL;
	id.valid = false;		/* value validation		*/
	value = 0;

	/* parsing loop, repeated until expression ends */
	do {
		mark = in[offset];
		switch(mark) {
		case '\0':
			break;
		case ' ':
		case '\t':
			offset ++;		/* jump over white space */
			break;
		case '~':			/* prefixes */
			unary(mark);
			offset ++;
			break;
		case '>':			/* operators  */
		case '<':
			offset ++;
			/* check that it's '>>' or '<<' */
			if (in[offset] != mark)
			{
				id.error = CLAC_GEN_ERR;
				break;
			}
			operation (value, mark);
			offset ++;
			break;
		case '|':
		case '&':
		case '^':
		case '+':
		case '-':
		case '*':
		case '/':
			operation (value, mark);
			offset ++;
			break;
		case '(':
			open_bracket ();
			offset ++;
			break;
		case ')':
			value = close_bracket (value);
			offset ++;
			break;
		default:
			/* register/symbol/number value needed? */
			if (id.valid == false) {
				Uint32 tmp;
				int consumed;
				consumed = getValue(&(in[offset]), &tmp, &dummy, bForDsp);
				/* number parsed? */
				if (consumed) {
					offset += consumed;
					id.valid = true;
					value = tmp;
					break;
				}
			}
			id.error = CLAC_GEN_ERR;
		}

	/* until exit or error message					*/
	} while(mark && !id.error);

        /* result of evaluation 					*/
        if (val.idx >= 0)
		*out = val.buf[val.idx];

	/* something to return?						*/
	if (!id.error) {
		if (id.valid) {

			/* evaluate rest of the expression		*/
			operation (value, LOWEST_PREDECENCE);
			if (par.idx)			/* mismatched	*/
				id.error = CLAC_PAR_ERR;
			else				/* result out	*/
				*out = val.buf[val.idx];

		} else {
			if ((val.idx < 0) && (op.idx < 0)) {
				id.error = CLAC_EXP_ERR;
			} else			/* trailing operators	*/
				id.error = CLAC_GEN_ERR;
		}
	}

	*erroff = offset;
	if (id.error) {
		*out = 0;
		return id.error;
	}
	return NULL;
}


/* ==================================================================== */
/*			expression evaluation				*/
/* ==================================================================== */

static void operation (long long value, char oper)
{
	/* uses globals par[], id.error[], op[], val[]
	 * operation executed if the next one is on same or lower level
	 */
	/* something to calc? */
	if(id.valid == true) {
		
		/* add new items to stack */
		PUSH(op, oper);
		PUSH(val, value);
		
		/* more than 1 operator  */
		if(op.idx > par.opx[par.idx]) {

			/* but only one value */
			if(val.idx == par.vax[par.idx]) {
				apply_prefix();
			} else {
				/* evaluate all possible operations */
				eval_stack();
			}
		}
		/* next a number needed */
		id.valid = false;
	} else {
		/* pre- or post-operators instead of in-betweens? */
		unary(oper);
	}
}

/**
 * handle unary operators
 */
static void unary (char oper)
{
	/* check pre-value operators
	 * have to be parenthesised
	 */
	if(id.valid == false && op.idx < par.opx[par.idx])
	{
		switch(oper) {
		case '+':		/* not needed */
			break;
		case '-':
		case '~':
			PUSH(op, oper);
			break;
		default:
			id.error = CLAC_GEN_ERR;
		}
	}
	else
		id.error = CLAC_GEN_ERR;
}

/**
 * apply a prefix to the current value
 */
static void apply_prefix(void)
{
	long long value = val.buf[val.idx];

	op.idx--;
	switch(op.buf[op.idx]) {
	case '-':
		value = (-value);
		break;
	case '~':
		value = (~value);
		break;
	default:
		id.error = CLAC_PRG_ERR;
	}
	val.buf[val.idx] = value;
	op.buf[op.idx] = op.buf[op.idx + 1];
}

/* -------------------------------------------------------------------- */
/**
 * evaluate operators if precedence allows it
 */
/* evaluate all possible (according to order of precedence) operators	*/
static void eval_stack (void)
{
	/* uses globals par[], op[], val[]	*/

	/* # of operators >= 2 and prev. op-level >= current op-level ?	*/
	while ((op.idx > par.opx[par.idx]) && get_level (-1) >= get_level (0)) {

		/* shorten value stacks by one	*/
		/* + calculate resulting value	*/
		op.idx -= 1;
		val.idx -= 1;
		val.buf[val.idx] = apply_op(op.buf[op.idx],
			val.buf[val.idx], val.buf[val.idx + 1]);

		/* pull the just used operator out of the stack		*/
		op.buf[op.idx] = op.buf[op.idx + 1];
	}
}

/* -------------------------------------------------------------------- */
/**
 * return the precedence level of a given operator
 */
static int get_level (int offset)
{
	/* used globals par[], op[]
	 * returns operator level of: operator[stack idx + offset]
	 */
	switch(op.buf[op.idx + offset]) {
	case '|':      /* binary operations  */
	case '&':
	case '^':
		return 0;
		
	case '>':      /* bit shifting    */
	case '<':
		return 1;
		
	case '+':
	case '-':
		return 2;
		
	case '*':
	case '/':
		return 3;
		
	default:
		id.error = CLAC_PRG_ERR;
	}
	return 6;
}

/* -------------------------------------------------------------------- */
/**
 * apply operator to given values, return the result
 */
static long long apply_op (char opcode, long long value1, long long value2)
{
	/* uses global id.error[]		*/
	/* returns the result of operation	*/

	switch (opcode) {
        case '|':
		value1 |= value2;
		break;
        case '&':
		value1 &= value2;
		break;
        case '^':
		value1 ^= value2;
		break;
        case '>':
		value1 >>= value2;
        case '<':
		value1 <<= value2;
		break;
	case '+':
		value1 += value2;
		break;
	case '-':
		value1 -= value2;
		break;
	case '*':
		value1 *= value2;
		break;
	case '/':
		/* don't divide by zero */
		if (value2)
			value1 /= value2;
		else
			id.error = CLAC_DEF_ERR;
		break;
        default:
		id.error = CLAC_PRG_ERR;
	}
	return value1;				/* return result	*/
}


/* ==================================================================== */
/*			parenthesis and help				*/
/* ==================================================================== */

/**
 * open parenthesis, push values & operators to stack
 */
static void open_bracket (void)
{
	if (id.valid == false) {		/* preceded by operator	*/
		if (par.idx < PARDEPTH_MAX) {	/* not nested too deep	*/
			par.idx ++;
			par.opx[par.idx] = op.idx + 1;
			par.vax[par.idx] = val.idx + 1;
		} else
			id.error = CLAC_STK_ERR;
	} else
		id.error = CLAC_GEN_ERR;
}

/* -------------------------------------------------------------------- */
/**
 * close parenthesis, and evaluate / pop stacks
 */
/* last parsed value, last param. flag, trigonometric mode	*/
static long long close_bracket (long long value)
{
	/* returns the value of the parenthesised expression	*/

	if (id.valid) {			/* preceded by an operator	*/
		if (par.idx > 0) {	/* parenthesis has a pair	*/
			Uint32 addr;

			/* calculate the value of parenthesised exp.	*/
			operation (value, LOWEST_PREDECENCE);
			/* fetch the indirect ST RAM value */
			addr = val.buf[val.idx];
			value = STMemory_ReadLong(addr);
			fprintf(stderr, "  value in RAM at ($%x).l = $%"PRIx64"\n",
				addr, (uint64_t)value);
			/* restore state before parenthesis */
			op.idx = par.opx[par.idx] - 1;
			val.idx = par.vax[par.idx] - 1;
			par.idx --;

			/* next operator */
			id.valid = true;
		} else
			id.error = CLAC_PAR_ERR;
	} else
		id.error = CLAC_GEN_ERR;

	return value;
}
