/* 
 * Code to test Hatari expression evaluation in src/debug/evaluate.c
 */
#include <stdio.h>
#include <SDL_types.h>
#include <stdbool.h>
#include "evaluate.h"
#include "main.h"

int main(int argc, const char *argv[])
{
	/* expected to fail */
	const char *failure[] = {
		"1+2*",
		"*1+2",
		"1+(2",
		"1)+2",
	};
	/* expected to succeed, with given result */
	struct {
		const char *expression;
		Uint32 result;
	} success[] = {
		{ "1+2*3", 7 },
		{ "(1+2)*3", 9 },
		{ "((0x21 * 0x200) + (-5)) ^ (~%111 & $f0f0f0)", 0xF0B10B },
	};
	int i, offset, tests = 0, errors = 0;
	const char *expression, *errstr;
	Uint32 result;

	fprintf(stderr, "\nExpressions that should FAIL:\n");

	for (i = 0; i < ARRAYSIZE(failure); i++) {
		expression = failure[i];
		errstr = Eval_Expression(expression, &result, &offset, false);
		if (errstr) {
			fprintf(stderr, "- '%s'\n%*c-%s\n",
				expression, 3+offset, '^', errstr);
		} else {
			fprintf(stderr, "***Unexpected SUCCESS from expression***\n- '%s' = %x\n",
				expression, (Uint32)result);
			errors++;
		}
	}
	tests += i;

	fprintf(stderr, "\nExpressions that should SUCCEED with given result:\n");
	
	for (i = 0; i < ARRAYSIZE(success); i++) {
		expression = success[i].expression;
		errstr = Eval_Expression(expression, &result, &offset, false);
		if (errstr) {
			fprintf(stderr, "***Unexpected ERROR in expression***\n- '%s'\n%*c-%s\n",
				expression, 3+offset, '^', errstr);
			errors++;
		} else if (result != success[i].result) {
			fprintf(stderr, "***Wrong result from expression***\n- '%s' = %x (not %x)\n",
				expression, (Uint32)result, (Uint32)success[i].result);
			errors++;
		} else {
			fprintf(stderr, "- '%s' = 0x%x\n",
				expression, (Uint32)result);
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
