/* 
 * Code to test Hatari conditional breakpoints in src/debug/breakcond.c
 * 
 * TODO: Move test stuff to a separate file and add Valgrinding
 * and --fstack-protector Make test targets for it
 */
#include "main.h"
#include "m68000.h"
#include "dsp.h"
#include "stMemory.h"
#include "memorySnapShot.h"
#include "video.h"
#include "debugui.h"
#include "breakcond.h"

#define BITMASK(x)      ((1<<(x))-1)

/* fake Hatari configuration variables for str.c */
#include "configuration.h"
CNF_PARAMS ConfigureParams;

/* fake ST RAM */
Uint8 STRam[16*1024*1024];
Uint32 STRamEnd = 4*1024*1024;

/* fake Hatari variables */
int nHBL = 20;
int nVBLs = 71;

/* fake video variables accessor */
void Video_GetPosition(int *pFrameCycles, int *pHBL, int *pLineCycles)
{
	*pFrameCycles = 2048;
	*pHBL = nHBL;
	*pFrameCycles = 508;
}

/* fake UAE core registers */
struct regstruct regs;

/* dummy UAE SR register tuning function */
void MakeSR(void) { }

/* fake AUE register accessors */
int DebugCpu_GetRegisterAddress(const char *regname, Uint32 **addr)
{
	const char *regnames[] = {
		/* must be in same order as in struct above! */
		"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
		"d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7"
	};
	static Uint32 registers[ARRAYSIZE(regnames)];
	int i;
	for (i = 0; i < ARRAYSIZE(regnames); i++) {
		if (strcmp(regname, regnames[i]) == 0) {
			*addr = &(registers[i]);
			return 32;
		}
	}
	return 0;
}

static bool SetCpuRegister(const char *regname, Uint32 value)
{
	Uint32 *addr;
	
	switch (DebugUI_GetCpuRegisterAddress(regname, &addr)) {
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


/* fake DSP register accessors */
int DSP_GetRegisterAddress(const char *regname, Uint32 **addr, Uint32 *mask)
{
	const char *regnames[] = {
		"a0", "a1", "a2", "b0", "b1", "b2", "la", "lc",
		"m0", "m1", "m2", "m3", "m4", "m5", "m6", "m7",
		"n0", "n1", "n2", "n3", "n4", "n5", "n6", "n7",
		"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
		"x0", "x1", "y0", "y1", "pc", "sr", "omr",
		"sp", "ssh", "ssl"
	};
	static Uint32 registers[ARRAYSIZE(regnames)];
	int i;
	for (i = 0; i < ARRAYSIZE(regnames); i++) {
		if (strcmp(regname, regnames[i]) == 0) {
			*addr = &(registers[i]);
			switch (regname[0]) {
			case 'a':
			case 'b':
			case 'x':
			case 'y':
				*mask = BITMASK(24);
				break;
			default:
				*mask = BITMASK(16);
				break;
			}
			if (regname[0] == 'p') {
				/* PC is 16-bit */
				return 16;
			}
			return 32;
		}
	}
	fprintf(stderr, "ERROR: unrecognized DSP register '%s', valid ones are:\n", regname);
	for (i = 0; i < ARRAYSIZE(regnames); i++) {
		fprintf(stderr, "- %s\n", regnames[i]);
	}
	return 0;
}

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

Uint32 DSP_ReadMemory(Uint16 addr, char space, const char **mem_str)
{
	/* dummy */
	return 0;
}

void MemorySnapShot_Store(void *pData, int Size)
{
	/* dummy */
}


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
		"foo().w=bar()",
		"(a0.w=d0.l)",
		"(a0&3)=20",
		"20 = (a0.w)",
		"()&=d0",
		"d0=().w",
		"255 & 3 = (d0) & && 2 = 2",
		/* size and mask mismatches with numbers */
		"d0.w = $ffff0",
		"(a0).b & 3 < 100",
		/* more than BC_MAX_CONDITIONS_PER_BREAKPOINT conditions */
		"1=1 && 2=2 && 3=3 && 4=4 && 5=5",
		NULL
	};
	const char *parser_pass[] = {
		" ($200).w > 200 ",
		" ($200).w < 200 ",
		" (200).w = $200 ",
		" (200).w ! $200 ",
		"a0>d0",
		"a0<d0",
		"d0=d1",
		"d0!d1",
		"(a0)=(d0)",
		"(d0).w=(a0).b",
		"(a0).w&3=(d0)&&d0=1",
		" ( a 0 ) . w  &  1 = ( d 0 ) & 1 &&  d 0 = 3 ",
		"a0=1 && (d0)&2=(a0).w && ($00ff00).w&1=1",
		" ($ff820a).b = 2",
		"hbl > 0 && vbl < 2000 && linecycles = 508",
		NULL
	};
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
	BreakCond_List(use_dsp);
	
	fprintf(stderr, "\nShould PASS for CPU:\n");
	for (i = 0; (test = parser_pass[i]); i++) {
		fprintf(stderr, "-----------------\n- parsing '%s'\n", test);
		if (!BreakCond_Command(test, use_dsp)) {
			fprintf(stderr, "***ERROR***: should have passed\n");
			errors++;
		}
	}
	tests += i;
	fprintf(stderr, "-----------------\n\n");
	BreakCond_List(use_dsp);
	fprintf(stderr, "\n");
	BreakCond_RemoveAll(use_dsp);
	BreakCond_List(use_dsp);
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
	BreakCond_List(use_dsp);
	fprintf(stderr, "\n");
	
	/* set up registers etc */

	/* fail indirect equality checks with zerod regs */
	memset(STRam, 0, sizeof(STRam));
	STRam[0] = 1;
	/* !match: "( $200 ) > 200"
	 *  match: "( $200 ) . w > ( 200 ) . b"
	 */
	STRam[0x200] = 100;
	STRam[200] = 0x20;
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
		BreakCond_Remove(i, use_dsp);
	}
	remaining_matches = BreakCond_BreakPointCount(use_dsp);
	if (remaining_matches != FAILING_BC_TEST_MATCHES) {
		fprintf(stderr, "ERROR: wrong number of breakpoints left (%d instead of %d)!\n",
			remaining_matches, FAILING_BC_TEST_MATCHES);
		errors++;
	}

	fprintf(stderr, "\nOther breakpoints didn't match, removing the rest...\n");
	BreakCond_RemoveAll(use_dsp);
	BreakCond_List(use_dsp);
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
		BreakCond_List(use_dsp);

		while ((i = BreakCond_MatchDsp())) {
			fprintf(stderr, "Removing matching DSP breakpoint.\n");
			BreakCond_Remove(i, use_dsp);
		}

		BreakCond_RemoveAll(use_dsp);
		BreakCond_List(use_dsp);
		fprintf(stderr, "-----------------\n");
	}
	if (errors) {
		fprintf(stderr, "\n***Detected %d ERRORs in %d automated tests!***\n\n",
			errors, tests);
	} else {
		fprintf(stderr, "\nFinished without any errors!\n\n");
	}
	return 0;
}
