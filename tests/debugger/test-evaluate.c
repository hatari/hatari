/* 
 * Code to test Hatari expression evaluation in src/debug/evaluate.c
 * (including Hatari variable and CPU register values in expressions)
 */
#include <stdio.h>
#include <SDL_types.h>
#include <stdbool.h>
#include "stMemory.h"
#include "evaluate.h"
#include "m68000.h"
#include "main.h"
#include "screen.h"
#include "configuration.h"
#include "video.h"

#define VBL_VALUE 21

int main(int argc, const char *argv[])
{
	/* expected to fail */
	const char *failure[] = {
		"1+2*",
		"*1+2",
		"1+(2",
		"1)+2",
		"foo+1+bar",
	};
	/* expected to succeed, with given result */
	struct {
		const char *expression;
		Uint32 result;
	} success[] = {
		{ "1+2*3", 7 },
		{ "(2+5)*3", 9 },
		{ "d0 + 2", 12 },
		{ "VBL+10", VBL_VALUE + 10 },
		{ "~%101 & $f0f0f ^ 0x21 * 0x200", 0xF4D0A },
	};
	int i, offset, tests = 0, errors = 0;
	const char *expression, *errstr;
	Uint32 result;

	/* set values needed by above succesful calculations */
	nVBLs = VBL_VALUE;
	memset(Regs, 0, sizeof(Regs));
	Regs[REG_D0] = 10;
	memset(STRam, 0, sizeof(STRam));
	/* expressions use long access, 3*3 -> 9 */
	STMemory_WriteLong(2+5, 3);

	fprintf(stderr, "\nExpressions that should FAIL:\n");

	for (i = 0; i < ARRAY_SIZE(failure); i++) {
		expression = failure[i];
		fprintf(stderr, "- '%s'\n", expression);
		errstr = Eval_Expression(expression, &result, &offset, false);
		if (errstr) {
			fprintf(stderr, "%*c-%s\n",
				3+offset, '^', errstr);
		} else {
			fprintf(stderr, "  => %x\n  ***Unexpected SUCCESS from expression***\n",
				(Uint32)result);
			errors++;
		}
	}
	tests += i;

	fprintf(stderr, "\nExpressions that should SUCCEED with given result:\n");

	for (i = 0; i < ARRAY_SIZE(success); i++) {
		expression = success[i].expression;
		fprintf(stderr, "- '%s'\n", expression);
		errstr = Eval_Expression(expression, &result, &offset, false);
		if (errstr) {
			fprintf(stderr, "%*c-%s\n  ***Unexpected ERROR in expression***\n",
				3+offset, '^', errstr);
			errors++;
		} else if (result != success[i].result) {
			fprintf(stderr, "  => %x (not %x)\n  ***Wrong result from expression***\n",
				(Uint32)result, (Uint32)success[i].result);
			errors++;
		} else {
			fprintf(stderr, "  => 0x%x\n", (Uint32)result);
		}
	}
	tests += i;

	if (errors) {
		fprintf(stderr, "\n***Detected %d ERRORs in %d automated tests!***\n\n",
			errors, tests);
	} else {
		fprintf(stderr, "\nFinished without any errors!\n\n");
	}
	return errors;
}
