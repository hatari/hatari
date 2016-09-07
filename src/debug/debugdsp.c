/*
  Hatari - debugdsp.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  debugdsp.c - function needed for the DSP debugging tasks like memory
  and register dumps.
*/
const char DebugDsp_fileid[] = "Hatari debugdsp.c : " __DATE__ " " __TIME__;

#include <stdio.h>
#include <ctype.h>

#include "config.h"

#include "main.h"
#include "breakcond.h"
#include "configuration.h"
#include "debugui.h"
#include "debug_priv.h"
#include "debugdsp.h"
#include "dsp.h"
#include "evaluate.h"
#include "history.h"
#include "log.h"
#include "memorySnapShot.h"
#include "profile.h"
#include "str.h"
#include "symbols.h"

static Uint16 dsp_disasm_addr;    /* DSP disasm address */
static Uint16 dsp_memdump_addr;   /* DSP memdump address */
static char dsp_mem_space = 'P';  /* X, Y, P */

static bool bDspProfiling;        /* Whether profiling is enabled */
static int nDspActiveCBs = 0;     /* Amount of active conditional breakpoints */
static int nDspSteps = 0;         /* Amount of steps for DSP single-stepping */


/**
 * Readline match callback to list register names usable within debugger.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
static char *DebugDsp_MatchRegister(const char *text, int state)
{
	static const char* regs[] = {
		"a0", "a1", "a2", "b0", "b1", "b2", "la", "lc",
		"m0", "m1", "m2", "m3", "m4", "m5", "m6", "m7",
		"n0", "n1", "n2", "n3", "n4", "n5", "n6", "n7",
		"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
		"omr", "pc", "sp", "sr", "ssh", "ssl",
		"x0", "x1", "y0", "y1",
	};
	return DebugUI_MatchHelper(regs, ARRAY_SIZE(regs), text, state);
}

/**
 * Command: Dump or set a DSP register
 */
int DebugDsp_Register(int nArgc, char *psArgs[])
{
	char *assign;
	Uint32 value;
	char *arg;

	if (!bDspEnabled)
	{
		fprintf(stderr, "DSP isn't present or initialized.\n");
		return DEBUGGER_CMDDONE;
	}

	if (nArgc == 1)
	{
		/* No parameter - dump all registers */
		DSP_DisasmRegisters(debugOutput);
		fflush(debugOutput);
		return DEBUGGER_CMDDONE;
	}
	arg = psArgs[1];

	assign = strchr(arg, '=');
	if (!assign)
		goto error_msg;

	*assign++ = '\0';
	if (!Eval_Number(Str_Trim(assign), &value))
		goto error_msg;

	if (DSP_Disasm_SetRegister(Str_Trim(arg), value))
	    return DEBUGGER_CMDDONE;

error_msg:
	fprintf(stderr,"\tError, usage: dr or dr xx=yyyy\n"
		"\tWhere: xx=A0-A2, B0-B2, X0, X1, Y0, Y1, R0-R7,\n"
		"\t       N0-N7, M0-M7, LA, LC, PC, SR, SP, OMR, SSH, SSL\n");

	return DEBUGGER_CMDDONE;
}


/**
 * Check whether given address matches any DSP symbol and whether
 * there's profiling information available for it.  If yes, show it.
 */
static void DebugDsp_ShowAddressInfo(Uint16 addr, FILE *fp)
{
	const char *symbol = Symbols_GetByDspAddress(addr);
	if (symbol)
		fprintf(fp, "%s:\n", symbol);
}


/**
 * DSP dissassemble - arg = starting address/range, or PC.
 */
int DebugDsp_DisAsm(int nArgc, char *psArgs[])
{
	Uint32 lower, upper;
	Uint16 dsp_disasm_upper = 0;

	if (!bDspEnabled)
	{
		fprintf(stderr, "DSP isn't present or initialized.\n");
		return DEBUGGER_CMDDONE;
	}

	if (nArgc > 1)
	{
		switch (Eval_Range(psArgs[1], &lower, &upper, true))
		{
			case -1:
				/* invalid value(s) */
				return DEBUGGER_CMDDONE;
			case 0:
				/* single value */
				break;
			case 1:
				/* range */
				if (upper > 0xFFFF)
				{
					fprintf(stderr,"Invalid address 0x%x!\n", upper);
					return DEBUGGER_CMDDONE;
				}
				dsp_disasm_upper = upper;
				break;
		}

		if (lower > 0xFFFF)
		{
			fprintf(stderr,"Invalid address 0x%x!\n", lower);
			return DEBUGGER_CMDDONE;
		}
		dsp_disasm_addr = lower;
	}
	else
	{
		/* continue */
		if(!dsp_disasm_addr)
		{
			dsp_disasm_addr = DSP_GetPC();
		}
	}
	if (!dsp_disasm_upper)
	{
		int lines = ConfigureParams.Debugger.nDisasmLines;
		if ( dsp_disasm_addr < (0xFFFF - lines))
			dsp_disasm_upper = dsp_disasm_addr + lines;
		else
			dsp_disasm_upper = 0xFFFF;
	}
	fprintf(debugOutput, "DSP disasm 0x%hx-0x%hx:\n", dsp_disasm_addr, dsp_disasm_upper);
	while (dsp_disasm_addr < dsp_disasm_upper) {
		DebugDsp_ShowAddressInfo(dsp_disasm_addr, debugOutput);
		dsp_disasm_addr = DSP_DisasmAddress(debugOutput, dsp_disasm_addr, dsp_disasm_addr);
	}
	fflush(debugOutput);

	return DEBUGGER_CMDCONT;
}


/**
 * Do a DSP memory dump, args = starting address or range.
 * <x|y|p> <address>: dump from X, Y or P, starting from given address,
 * e.g. "x 200" or "p 200-300"
 */
int DebugDsp_MemDump(int nArgc, char *psArgs[])
{
	Uint32 lower, upper;
	Uint16 dsp_memdump_upper = 0;
	char *range, space;

	if (!bDspEnabled)
	{
		fprintf(stderr, "DSP isn't present or initialized.\n");
		return DEBUGGER_CMDDONE;
	}

	switch (nArgc)
	{
		case 1:
			break;
		case 3:	/* "x $200" */
			space = psArgs[1][0];
			range = psArgs[2];
			break;
		case 2: /* "x:$200" */
			if (psArgs[1][1] == ':')
			{
				space = psArgs[1][0];
				range = psArgs[1] + 2;
				break;
			}
			/* pass-through */
		default:
			return DebugUI_PrintCmdHelp(psArgs[0]);
	}

	if (nArgc > 1)
	{
		space = toupper((unsigned char)space);
		switch (space)
		{
		case 'X':
		case 'Y':
		case 'P':
			break;
		default:
			fprintf(stderr,"Invalid DSP address space '%c'!\n", space);
			return DEBUGGER_CMDDONE;
		}
		switch (Eval_Range(range, &lower, &upper, true))
		{
		case -1:
			/* invalid value(s) */
			return DEBUGGER_CMDDONE;
		case 0:
			/* single value */
			break;
		case 1:
			/* range */
			if (upper > 0xFFFF)
			{
				fprintf(stderr,"Invalid address 0x%x!\n", upper);
				return DEBUGGER_CMDDONE;
			}
			dsp_memdump_upper = upper;
			break;
		}
		if (lower > 0xFFFF)
		{
			fprintf(stderr,"Invalid address 0x%x!\n", lower);
			return DEBUGGER_CMDDONE;
		}
		dsp_memdump_addr = lower;
		dsp_mem_space = space;
	}

	if (!dsp_memdump_upper)
	{
		int lines = ConfigureParams.Debugger.nMemdumpLines;
		if ( dsp_memdump_addr < (0xFFFF - lines))
			dsp_memdump_upper = dsp_memdump_addr + lines;
		else
			dsp_memdump_upper = 0xFFFF;
	}

	fprintf(debugOutput, "DSP memdump from 0x%hx in '%c' address space:\n", dsp_memdump_addr, dsp_mem_space);
	dsp_memdump_addr = DSP_DisasmMemory(debugOutput, dsp_memdump_addr, dsp_memdump_upper, dsp_mem_space);
	fflush(debugOutput);

	return DEBUGGER_CMDCONT;
}


/**
 * Command: Continue DSP emulation / single-stepping
 */
static int DebugDsp_Continue(int nArgc, char *psArgv[])
{
	int steps = 0;
	
	if (nArgc > 1)
	{
		steps = atoi(psArgv[1]);
	}
	if (steps <= 0)
	{
		nDspSteps = 0;
		fprintf(stderr,"Returning to emulation...\n");
		return DEBUGGER_END;
	}
	nDspSteps = steps;
	fprintf(stderr,"Returning to emulation for %i DSP instructions...\n", steps);
	return DEBUGGER_END;
}

/**
 * Command: Single-step DSP
 */
static int DebugDsp_Step(int nArgc, char *psArgv[])
{
	nDspSteps = 1;
	return DEBUGGER_END;
}


/**
 * Readline match callback to list next command opcode types.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
static char *DebugDsp_MatchNext(const char *text, int state)
{
	static const char* ntypes[] = {
		"branch", "exreturn", "return", "subcall", "subreturn"
	};
	return DebugUI_MatchHelper(ntypes, ARRAY_SIZE(ntypes), text, state);
}

/**
 * Command: Step DSP, but proceed through subroutines
 * Does this by temporary conditional breakpoint
 */
static int DebugDsp_Next(int nArgc, char *psArgv[])
{
	char command[40];
	if (nArgc > 1)
	{
		int optype;
		if(strcmp(psArgv[1], "branch") == 0)
			optype = CALL_BRANCH;
		else if(strcmp(psArgv[1], "exreturn") == 0)
			optype = CALL_EXCRETURN;
		else if(strcmp(psArgv[1], "subcall") == 0)
			optype = CALL_SUBROUTINE;
		else if (strcmp(psArgv[1], "subreturn") == 0)
			optype = CALL_SUBRETURN;
		else if (strcmp(psArgv[1], "return") == 0)
			optype = CALL_SUBRETURN | CALL_EXCRETURN;
		else
		{
			fprintf(stderr, "Unrecognized opcode type given!\n");
			return DEBUGGER_CMDDONE;
		}
		sprintf(command, "DspOpcodeType & $%x > 0 :once :quiet\n", optype);
	}
	else
	{
		Uint32 optype;
		Uint16 nextpc;

		optype = DebugDsp_OpcodeType();
		/* can this instruction be stepped normally? */
		if (optype != CALL_SUBROUTINE && optype != CALL_EXCEPTION)
		{
			nDspSteps = 1;
			return DEBUGGER_END;
		}

		nextpc = DSP_GetNextPC(DSP_GetPC());
		sprintf(command, "pc=$%x :once :quiet\n", nextpc);
	}
	/* use breakpoint, not steps */
	if (BreakCond_Command(command, true)) {
		nDspSteps = 0;
		return DEBUGGER_END;
	}
	return DEBUGGER_CMDDONE;
}

/* helper to get instruction type, slightly simpler
 * version from one in profiledsp.c
 */
Uint32 DebugDsp_OpcodeType(void)
{
	const char *dummy;
	Uint32 opcode;

	/* 24-bit instruction opcode */
	opcode = DSP_ReadMemory(DSP_GetPC(), 'P', &dummy) & 0xFFFFFF;

	/* subroutine returns */
	if (opcode == 0xC) {	/* (just) RTS */
		return CALL_SUBRETURN;
	}
	if (
	    /* unconditional subroutine calls */
	    (opcode & 0xFFF000) == 0xD0000 ||	/* JSR   00001101 0000aaaa aaaaaaaa */
	    (opcode & 0xFFC0FF) == 0xBC080 ||	/* JSR   00001011 11MMMRRR 10000000 */
	    /* conditional subroutine calls */
	    (opcode & 0xFF0000) == 0xF0000 ||	/* JSCC  00001111 CCCCaaaa aaaaaaaa */
	    (opcode & 0xFFC0F0) == 0xBC0A0 ||	/* JSCC  00001011 11MMMRRR 1010CCCC */
	    (opcode & 0xFFC0A0) == 0xB4080 ||	/* JSCLR 00001011 01MMMRRR 1S0bbbbb */
	    (opcode & 0xFFC0A0) == 0xB0080 ||	/* JSCLR 00001011 00aaaaaa 1S0bbbbb */
	    (opcode & 0xFFC0A0) == 0xB8080 ||	/* JSCLR 00001011 10pppppp 1S0bbbbb */
	    (opcode & 0xFFC0E0) == 0xBC000 ||	/* JSCLR 00001011 11DDDDDD 000bbbbb */
	    (opcode & 0xFFC0A0) == 0xB40A0 ||	/* JSSET 00001011 01MMMRRR 1S1bbbbb */
	    (opcode & 0xFFC0A0) == 0xB00A0 ||	/* JSSET 00001011 00aaaaaa 1S1bbbbb */
	    (opcode & 0xFFC0A0) == 0xB80A0 ||	/* JSSET 00001011 10pppppp 1S1bbbbb */
	    (opcode & 0xFFC0E0) == 0xBC020) {	/* JSSET 00001011 11DDDDDD 001bbbbb */
		return CALL_SUBROUTINE;
	}
	/* exception handler returns */
	if (opcode == 0x4) {	/* (just) RTI */
		return CALL_EXCRETURN;
	}
	/* branches */
	if ((opcode & 0xFFF000) == 0xC0000 ||	/* JMP  00001100 0000aaaa aaaaaaaa */
	    (opcode & 0xFFC0FF) == 0xAC080 ||	/* JMP  00001010 11MMMRRR 10000000 */
	    (opcode & 0xFF0000) == 0xE0000 ||	/* JCC  00001110 CCCCaaaa aaaaaaaa */
	    (opcode & 0xFFC0F0) == 0xAC0A0 ||	/* JCC  00001010 11MMMRRR 1010CCCC */
	    (opcode & 0xFFC0A0) == 0xA8080 ||	/* JCLR 00001010 10pppppp 1S0bbbbb */
	    (opcode & 0xFFC0A0) == 0xA4080 ||	/* JCLR 00001010 01MMMRRR 1S0bbbbb */
	    (opcode & 0xFFC0A0) == 0xA0080 ||	/* JCLR 00001010 00aaaaaa 1S0bbbbb */
	    (opcode & 0xFFC0E0) == 0xAC000 ||	/* JCLR 00001010 11dddddd 000bbbbb */
	    (opcode & 0xFFC0A0) == 0xA80A0 ||	/* JSET 00001010 10pppppp 1S1bbbbb */
	    (opcode & 0xFFC0A0) == 0xA40A0 ||	/* JSET 00001010 01MMMRRR 1S1bbbbb */
	    (opcode & 0xFFC0A0) == 0xA00A0 ||	/* JSET 00001010 00aaaaaa 1S1bbbbb */
	    (opcode & 0xFFC0E0) == 0xAC020 ||	/* JSET 00001010 11dddddd 001bbbbb */
	    (opcode & 0xFF00F0) == 0x600A0 ||	/* REP  00000110 iiiiiiii 1010hhhh */
	    (opcode & 0xFFC0FF) == 0x6C020 ||	/* REP  00000110 11dddddd 00100000 */
	    (opcode & 0xFFC0BF) == 0x64020 ||	/* REP  00000110 01MMMRRR 0s100000 */
	    (opcode & 0xFFC0BF) == 0x60020 ||	/* REP  00000110 00aaaaaa 0s100000 */
	    (opcode & 0xFF00F0) == 0x60080 ||	/* DO/ENDO 00000110 iiiiiiii 1000hhhh */
	    (opcode & 0xFFC0FF) == 0x6C000 ||	/* DO/ENDO 00000110 11DDDDDD 00000000 */
	    (opcode & 0xFFC0BF) == 0x64000 ||	/* DO/ENDO 00000110 01MMMRRR 0S000000 */
	    (opcode & 0xFFC0BF) == 0x60000) {	/* DO/ENDO 00000110 00aaaaaa 0S000000 */
		return CALL_BRANCH;
	}
	return CALL_UNKNOWN;
}


/**
 * DSP wrapper for BreakAddr_Command().
 */
static int DebugDsp_BreakAddr(int nArgc, char *psArgs[])
{
	BreakAddr_Command(psArgs[1], true);
	return DEBUGGER_CMDDONE;
}

/**
 * DSP wrapper for BreakCond_Command().
 */
static int DebugDsp_BreakCond(int nArgc, char *psArgs[])
{
	BreakCond_Command(psArgs[1], true);
	return DEBUGGER_CMDDONE;
}

/**
 * DSP wrapper for Profile_Command().
 */
static int DebugDsp_Profile(int nArgc, char *psArgs[])
{
	return Profile_Command(nArgc, psArgs, true);
}


/**
 * DSP instructions since continuing emulation
 */
static Uint32 nDspInstructions;
Uint32 DebugDsp_InstrCount(void)
{
	return nDspInstructions;
}

/**
 * This function is called after each DSP instruction when debugging is enabled.
 */
void DebugDsp_Check(void)
{
	nDspInstructions++;
	if (bDspProfiling)
	{
		Profile_DspUpdate();
	}
	if (LOG_TRACE_LEVEL((TRACE_DSP_DISASM|TRACE_DSP_SYMBOLS)))
	{
		DebugDsp_ShowAddressInfo(DSP_GetPC(), TraceFile);
	}
	if (nDspActiveCBs)
	{
		if (BreakCond_MatchDsp())
		{
			DebugUI(REASON_DSP_BREAKPOINT);
			/* make sure we don't decrease step count
			 * below, before even getting out of here
			 */
			if (nDspSteps)
				nDspSteps++;
		}
	}
	if (nDspSteps)
	{
		nDspSteps--;
		if (nDspSteps == 0)
			DebugUI(REASON_DSP_STEPS);
	}
	if (History_TrackDsp())
	{
		History_AddDsp();
	}
}


/**
 * Should be called before returning back emulation to tell the DSP core
 * to call us after each instruction if "real-time" debugging like
 * breakpoints has been set.
 */
void DebugDsp_SetDebugging(void)
{
	bDspProfiling = Profile_DspStart();
	nDspActiveCBs = BreakCond_DspBreakPointCount();

	if (nDspActiveCBs || nDspSteps || bDspProfiling || History_TrackDsp()
	    || LOG_TRACE_LEVEL((TRACE_DSP_DISASM|TRACE_DSP_SYMBOLS)))
	{
		DSP_SetDebugging(true);
		nDspInstructions = 0;
	}
	else
		DSP_SetDebugging(false);
}


static const dbgcommand_t dspcommands[] =
{
	{ NULL, NULL, "DSP commands", NULL, NULL, NULL, false },
	{ DebugDsp_BreakAddr, Symbols_MatchDspCodeAddress,
	  "dspaddress", "da",
	  "set DSP PC address breakpoints",
	  BreakAddr_Description,
	  true },
	/* currently no DSP variables, so checks that DSP symbol addresses */
	{ DebugDsp_BreakCond, Symbols_MatchDspAddress,
	  "dspbreak", "db",
	  "set/remove/list conditional DSP breakpoints",
	  BreakCond_Description,
	  true },
	{ DebugDsp_DisAsm, Symbols_MatchDspCodeAddress,
	  "dspdisasm", "dd",
	  "disassemble DSP code",
	  "[<start address>[-<end address>]]\n"
	  "\tDisassemble from DSP-PC, otherwise at given address.",
	  false },
	{ DebugDsp_MemDump, Symbols_MatchDspDataAddress,
	  "dspmemdump", "dm",
	  "dump DSP memory",
	  "[<x|y|p> <start address>[-<end address>]]\n"
	  "\tdump DSP memory from given memory space and address, or\n"
	  "\tcontinue from previous address if not specified.",
	  false },
	{ Symbols_Command, NULL,
	  "dspsymbols", "",
	  "load DSP symbols & their addresses",
	  Symbols_Description,
	  false },
	{ DebugDsp_Profile, Profile_Match,
	  "dspprofile", "dp",
	  "profile DSP code",
	  Profile_Description,
	  false },
	{ DebugDsp_Register, DebugDsp_MatchRegister,
	  "dspreg", "dr",
	  "read/write DSP registers",
	  "[REG=value]"
	  "\tSet or dump contents of DSP registers.",
	  true },
	{ DebugDsp_Step, NULL,
	  "dspstep", "ds",
	  "single-step DSP",
	  "\n"
	  "\tExecute next DSP instruction (equals 'dc 1')",
	  false },
	{ DebugDsp_Next, DebugDsp_MatchNext,
	  "dspnext", "dn",
	  "step DSP through subroutine calls / to given instruction type",
	  "[instruction type]\n"
	  "\tSame as 'dspstep' command if there are no subroutine calls.\n"
          "\tWhen there are, those calls are treated as one instruction.\n"
	  "\tIf argument is given, continues until instruction of given\n"
	  "\ttype is encountered.",
	  false },
	{ DebugDsp_Continue, NULL,
	  "dspcont", "dc",
	  "continue emulation / DSP single-stepping",
	  "[steps]\n"
	  "\tLeave debugger and continue emulation for <steps> DSP instructions\n"
	  "\tor forever if no steps have been specified.",
	  false }
};


/**
 * Should be called when debugger is first entered to initialize
 * DSP debugging variables.
 * 
 * if you want disassembly or memdumping to start/continue from
 * specific address, you can set them here.  If disassembly
 * address is zero, disassembling starts from PC.
 * 
 * returns number of DSP commands and pointer to array of them.
 */
int DebugDsp_Init(const dbgcommand_t **table)
{
	dsp_disasm_addr = 0;
	dsp_memdump_addr = 0;
	dsp_mem_space = 'P';

	*table = dspcommands;
	return ARRAY_SIZE(dspcommands);
}

/**
 * Should be called when debugger is re-entered to reset
 * relevant DSP debugging variables.
 */
void DebugDsp_InitSession(void)
{
	dsp_disasm_addr = DSP_GetPC();
	Profile_DspStop();
}
