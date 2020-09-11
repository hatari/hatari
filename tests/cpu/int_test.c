/*
 * CPU integer arithmetic tests.
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 */

#include <tos.h>

extern char tst_abcd_1(void);
extern char tst_abcd_2(void);
extern char tst_abcd_3(void);
extern char tst_abcd_4(void);

extern char tst_add_1(void);
extern char tst_add_2(void);
extern char tst_add_3(void);
extern char tst_add_4(void);
extern char tst_add_5(void);
extern char tst_add_6(void);
extern char tst_add_7(void);

extern char tst_addi_1(void);
extern char tst_addi_2(void);
extern char tst_addi_3(void);
extern char tst_addi_4(void);
extern char tst_addi_5(void);
extern char tst_addi_6(void);

extern char tst_addq_1(void);
extern char tst_addq_2(void);
extern char tst_addq_3(void);

extern char tst_addx_1(void);
extern char tst_addx_2(void);

extern char tst_shift_1(void);
extern char tst_shift_2(void);
extern char tst_shift_3(void);
extern char tst_shift_4(void);
extern char tst_shift_5(void);
extern char tst_shift_6(void);
extern char tst_shift_7(void);
extern char tst_shift_8(void);

struct test
{
	char *name;
	char (*testfunc)(void);
};

struct test tests[] =
{
	{ "abcd 1", tst_abcd_1 },
	{ "abcd 2", tst_abcd_2 },
	{ "abcd 3", tst_abcd_3 },
	{ "abcd 4", tst_abcd_4 },

	{ "add 1", tst_add_1 },
	{ "add 2", tst_add_2 },
	{ "add 3", tst_add_3 },
	{ "add 4", tst_add_4 },
	{ "add 5", tst_add_5 },
	{ "add 6", tst_add_6 },
	{ "add 7", tst_add_7 },

	{ "addi 1", tst_addi_1 },
	{ "addi 2", tst_addi_2 },
	{ "addi 3", tst_addi_3 },
	{ "addi 4", tst_addi_4 },
	{ "addi 5", tst_addi_5 },
	{ "addi 6", tst_addi_6 },

	{ "addq 1", tst_addq_1 },
	{ "addq 2", tst_addq_2 },
	{ "addq 3", tst_addq_3 },

	{ "addx 1", tst_addx_1 },
	{ "addx 2", tst_addx_2 },

	{ "shift 1", tst_shift_1 },
	{ "shift 2", tst_shift_2 },
	{ "shift 3", tst_shift_3 },
	{ "shift 4", tst_shift_4 },
	{ "shift 5", tst_shift_5 },
	{ "shift 6", tst_shift_6 },
	{ "shift 7", tst_shift_7 },
	{ "shift 8", tst_shift_8 },

	{ 0L, 0L }
};

int main()
{
	int failures = 0;
	int idx;

	for (idx = 0; tests[idx].name != 0L; idx++)
	{
		Cconws("Test '");
		Cconws(tests[idx].name);
		Cconws("'\t: ");
		if (tests[idx].testfunc() != 0)
		{
			Cconws("FAILED\n");
			failures++;
		}
		else
		{
			Cconws("OK\n");
		}
	}

	// Crawcin();

	return !(failures == 0);
}
