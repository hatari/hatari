/* 
 * Code to test Hatari conditional breakpoints in src/debug/breakcond.c
 * (both matching and setting CPU and DSP breakpoints)
 */
#include "main.h"
#include "dsp.h"
#include "debugcpu.h"
#include "breakcond.h"
#include "stMemory.h"
#include "newcpu.h"

#define BITMASK(x)      ((1<<(x))-1)

/* BreakCond_Command() command strings */
#define CMD_LIST NULL
#define CMD_REMOVE_ALL "all"


static bool SetCpuRegister(const char *regname, Uint32 value)
{
	Uint32 *addr;
	
	switch (DebugCpu_GetRegisterAddress(regname, &addr)) {
	case 32:
		*addr = value;
		break;
	case 16:
		*(Uint16*)addr = value;
		break;
	default:
		fprintf(stderr, "SETUP ERROR: Register '%s' to set (to %x) is unrecognized!\n", regname, value);
		return false;
	}
	return true;
}

#if 0
static bool SetDspRegister(const char *regname, Uint32 value)
{
	Uint32 *addr, mask;

	switch (DSP_GetRegisterAddress(regname, &addr, &mask)) {
	case 32:
		*addr = value & mask;
		break;
	case 16:
		*(Uint16*)addr = value & mask;
		break;
	default:
		return false;
	}
	return true;
}
#endif

int main(int argc, const char *argv[])
{
	const char *parser_fail[] = {
		/* syntax & register name errors */
		"",
		" = ",
		" a0 d0 ",
		"gggg=a0",
		"=a=b=",
		"a0=d0=20",
		"a0=d || 0=20",
		"a0=d & 0=20",
		".w&3=2",
		"d0 = %200",
		"d0 = \"ICE!BAR",
		"pc > $200 :foobar",
		"foo().w=bar()",
		"(a0.w=d0.l)",
		"(a0&3)=20",
		"20 = (a0.w)",
		"()&=d0",
		"d0=().w",
		"&& pc = 2",
		"pc = 2 &&",
		"255 & 3 = (d0) & && 2 = 2",
		/* missing options file */
		"pc>pc :file no-such-file",		
		/* size and mask mismatches with numbers */
		"d0.w = $ffff0",
		"(a0).b & 3 < 100",
		NULL
	};
	const char *parser_pass[] = {
		/* comparisons with normal numbers + indrect addressing */
		" ($200).w > 200 ",
		" ($200).w < 200 ",
		" (200).w = $200 ",
		" (200).w ! $200 ",
		/* indirect addressing with registers */
		"(a0)=(d0)",
		"(d0).w=(a0).b",
		/* sizes + multiple conditions + spacing */
		"(a0).w&3=(d0)&&d0=1",
		" ( a 0 ) . w  &  1 = ( d 0 ) & 1 &&  d 0 = 3 ",
		"a0=1 && (d0)&2=(a0).w && ($00ff00).w&1=1",
		" ($ff820a).b = 2",
		/* variables */
		"hbl > 0 && vbl < 2000 && linecycles = 508",
		/* options */
		"($200).w ! ($200).w :trace",
		"($200).w > ($200).w :4 :lock",
		"pc>pc :file data/test.ini :once",
		NULL
	};
	/* address breakpoint + expression evalution with register */
	char addr_pass[] = "pc + ($200*16/2 & 0xffff)";

	const char *match_tests[] = {
		"a0 = d0",
		"( $200 ) . b > 200", /* byte access to avoid endianess */
		"pc < $50000 && pc > $60000",
		"pc > $50000 && pc < $54000",
#define FAILING_BC_TEST_MATCHES 4
		"pc > $50000 && pc < $60000",
		"( $200 ) . b > ( 200 ) . b",
		"d0 = d1",
		"a0 = pc",
		NULL
	};
	const char *test;
	char testidx[2] = "1";
	int i, j, tests = 0, errors = 0;
	int remaining_matches;
	bool use_dsp;

	/* first automated tests... */
	use_dsp = false;
	fprintf(stderr, "\nShould FAIL for CPU:\n");
	for (i = 0; (test = parser_fail[i]); i++) {
		fprintf(stderr, "-----------------\n- parsing '%s'\n", test);
		if (BreakCond_Command(test, use_dsp)) {
			fprintf(stderr, "***ERROR***: should have failed\n");
			errors++;
		}
	}
	tests += i;
	fprintf(stderr, "-----------------\n\n");
	BreakCond_Command(CMD_LIST, use_dsp);
	
	fprintf(stderr, "\nShould PASS for CPU:\n");
	for (i = 0; (test = parser_pass[i]); i++) {
		fprintf(stderr, "-----------------\n- parsing '%s'\n", test);
		if (!BreakCond_Command(test, use_dsp)) {
			fprintf(stderr, "***ERROR***: should have passed\n");
			errors++;
		}
	}
	tests += i;
	fprintf(stderr, "\nAddress PASS test for CPU:\n");
	if (!BreakAddr_Command(addr_pass, use_dsp)) {
		fprintf(stderr, "***ERROR***: should have passed\n");
		errors++;
	}
	tests += 1;

	fprintf(stderr, "-----------------\n\n");
	BreakCond_Command(CMD_LIST, use_dsp);
	fprintf(stderr, "\n");
	BreakCond_Command(CMD_REMOVE_ALL, use_dsp);
	BreakCond_Command(CMD_LIST, use_dsp);
	fprintf(stderr, "-----------------\n");

	/* add conditions */
	fprintf(stderr, "\nLast one(s) should match, first one(s) shouldn't:\n");
	for (i = 0; (test = match_tests[i]); i++) {
		fprintf(stderr, "-----------------\n- parsing '%s'\n", test);
		if (!BreakCond_Command(test, use_dsp)) {
			fprintf(stderr, "***ERROR***: should have passed\n");
			errors++;
		}
	}
	tests += i;
	BreakCond_Command(CMD_LIST, use_dsp);
	fprintf(stderr, "\n");
	
	/* set up registers etc */

	/* fail indirect equality checks with zerod regs */
	memset(STRam, 0, sizeof(STRam));
	STMemory_WriteByte(0, 1);
	/* !match: "( $200 ) > 200"
	 *  match: "( $200 ) . w > ( 200 ) . b"
	 */
	STMemory_WriteByte(0x200, 100);
	STMemory_WriteByte(200, 0x20);
	/*  match: "d0 = d1" */
	SetCpuRegister("d0", 4);
	SetCpuRegister("d1", 4);
	/* !match: "pc < $50000  &&  pc > $60000"
	 * !match: "pc < $50000  &&  pc > $54000"
	 *  match: "pc > $50000  &&  pc < $60000"
	 */
	regs.pc = 0x58000;
	/* !match: "d0 = a0"
	 *  match: "pc = a0"
	 */
	SetCpuRegister("a0", 0x58000);
	
	/* check matches */
	while ((i = BreakCond_MatchCpu())) {
		fprintf(stderr, "Removing matching CPU breakpoint %d...\n", i);
		for (j = 0; (test = match_tests[j]); j++) {
			if (BreakCond_MatchCpuExpression(i, test)) {
				break;
			}
		}
		if (test) {
			if (j < FAILING_BC_TEST_MATCHES) {
				fprintf(stderr, "ERROR: breakpoint should not have matched!\n");
				errors++;
			}
		} else {
			fprintf(stderr, "WARNING: canonized breakpoint form didn't match\n");
			errors++;
		}
		testidx[0] = '0' + i;
		BreakCond_Command(testidx, use_dsp); /* remove given */
	}
	remaining_matches = BreakCond_CpuBreakPointCount();
	if (remaining_matches != FAILING_BC_TEST_MATCHES) {
		fprintf(stderr, "ERROR: wrong number of breakpoints left (%d instead of %d)!\n",
			remaining_matches, FAILING_BC_TEST_MATCHES);
		errors++;
	}

	fprintf(stderr, "\nOther breakpoints didn't match, removing the rest...\n");
	BreakCond_Command(CMD_REMOVE_ALL, use_dsp);
	BreakCond_Command(CMD_LIST, use_dsp);
	fprintf(stderr, "-----------------\n");

	/* ...last parse cmd line args as DSP breakpoints */
	if (argc > 1) {
		use_dsp = true;
		fprintf(stderr, "\nCommand line DSP breakpoints:\n");
		for (argv++; --argc > 0; argv++) {
			fprintf(stderr, "-----------------\n- parsing '%s'\n", *argv);
			BreakCond_Command(*argv, use_dsp);
		}
		fprintf(stderr, "-----------------\n\n");
		BreakCond_Command("", use_dsp); /* list */

		while ((i = BreakCond_MatchDsp())) {
			fprintf(stderr, "Removing matching DSP breakpoint.\n");
			testidx[0] = '0' + i;
			BreakCond_Command(testidx, use_dsp); /* remove given */
		}

		BreakCond_Command(CMD_REMOVE_ALL, use_dsp);
		BreakCond_Command(CMD_LIST, use_dsp);
		fprintf(stderr, "-----------------\n");
	}
	if (errors) {
		fprintf(stderr, "\n***Detected %d ERRORs in %d automated tests!***\n\n",
			errors, tests);
	} else {
		fprintf(stderr, "\nFinished without any errors!\n\n");
	}
	return errors;
}
