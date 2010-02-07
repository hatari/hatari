/* 
 * Code to test Hatari conditional breakpoints in src/debug/breakcond.c
 */
#include "main.h"
#include "dsp.h"
#include "debugcpu.h"
#include "breakcond.h"

#define BITMASK(x)      ((1<<(x))-1)

/* fake tracing flags */
Uint32 LogTraceFlags = 0;

/* fake Hatari configuration variables for number parsing */
#include "configuration.h"
CNF_PARAMS ConfigureParams;

/* fake ST RAM */
#include "stMemory.h"
Uint8 STRam[16*1024*1024];
Uint32 STRamEnd = 4*1024*1024;

/* fake memory banks */
#include "memory.h"
addrbank *mem_banks[65536];

/* fake IO memory variables */
#include "ioMem.h"
int nIoMemAccessSize;
Uint32 IoAccessBaseAddress;

/* fake CPU wrapper stuff */
#include "m68000.h"
void MakeFromSR(void) { }

/* fake AUE core registers */
#include "newcpu.h"
struct regstruct regs;
void MakeSR(void) { }
void m68k_dumpstate (FILE *f, uaecptr *nextpc) { }
void m68k_disasm (FILE *f, uaecptr addr, uaecptr *nextpc, int cnt) { }

/* fake memory snapshot */
#include "memorySnapShot.h"
void MemorySnapShot_Store(void *pData, int Size) { }

/* fake debugui.c stuff */
#include "debug_priv.h"
#include "debugui.h"
FILE *debugOutput;
void DebugUI(void) { }
void DebugUI_PrintCmdHelp(const char *psCmd) { }

/* fake Hatari video variables */
#include "video.h"
int nHBL = 20;
int nVBLs = 71;

/* fake video variables accessor */
void Video_GetPosition(int *pFrameCycles, int *pHBL, int *pLineCycles)
{
	*pFrameCycles = 2048;
	*pHBL = nHBL;
	*pFrameCycles = 508;
}


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
	return errors;
}
