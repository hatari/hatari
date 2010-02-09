/*
  Hatari - debugdsp.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  debugdsp.c - function needed for the DSP debugging tasks like memory
  and register dumps.
*/
const char DebugDsp_fileid[] = "Hatari debugdsp.c : " __DATE__ " " __TIME__;

#include <stdio.h>

#include "config.h"

#include "main.h"
#include "breakcond.h"
#include "debugui.h"
#include "debug_priv.h"
#include "debugdsp.h"
#include "dsp.h"
#include "evaluate.h"
#include "memorySnapShot.h"
#include "str.h"
#include "symbols.h"

static Uint16 dsp_disasm_addr;    /* DSP disasm address */
static Uint16 dsp_memdump_addr;   /* DSP memdump address */
static char dsp_mem_space = 'P';  /* X, Y, P */

static Uint16 DspBreakPoint[16];  /* DSP breakpoints */
static int nDspActiveBPs = 0;     /* Amount of active breakpoints */
static int nDspActiveCBs = 0;     /* Amount of active conditional breakpoints */
static int nDspSteps = 0;         /* Amount of steps for DSP single-stepping */


/**
 * Save/Restore snapshot of debugging session variables
 */
void DebugDsp_MemorySnapShot_Capture(bool bSave)
{
	MemorySnapShot_Store(&dsp_disasm_addr, sizeof(dsp_disasm_addr));
	MemorySnapShot_Store(&dsp_memdump_addr, sizeof(dsp_memdump_addr));
	MemorySnapShot_Store(&dsp_mem_space, sizeof(dsp_mem_space));
	
	MemorySnapShot_Store(&nDspActiveBPs, sizeof(nDspActiveBPs));
	MemorySnapShot_Store(&nDspActiveCBs, sizeof(nDspActiveCBs));
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
		printf("DSP isn't present or initialized.\n");
		return DEBUGGER_CMDDONE;
	}

	if (nArgc == 1)
	{
		/* No parameter - dump all registers */
		DSP_DisasmRegisters();
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
 * DSP dissassemble - arg = starting address/range, or PC.
 */
int DebugDsp_DisAsm(int nArgc, char *psArgs[])
{
	Uint32 lower, upper;
	Uint16 dsp_disasm_upper = 0;

	if (!bDspEnabled)
	{
		printf("DSP isn't present or initialized.\n");
		return DEBUGGER_CMDDONE;
	}

	if (nArgc > 1)
	{
		switch (Eval_Range(psArgs[1], &lower, &upper))
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
		if ( dsp_disasm_addr < (0xFFFF - 8))
			dsp_disasm_upper = dsp_disasm_addr + 8;
		else
			dsp_disasm_upper = 0xFFFF;
	}
	printf("DSP disasm %hx-%hx:\n", dsp_disasm_addr, dsp_disasm_upper);
	dsp_disasm_addr = DSP_DisasmAddress(dsp_disasm_addr, dsp_disasm_upper);

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
	char space;

	if (!bDspEnabled)
	{
		printf("DSP isn't present or initialized.\n");
		return DEBUGGER_CMDDONE;
	}
	if (nArgc == 2)
	{
		fprintf(stderr,"Memory space or address/range missing\n");
		return DEBUGGER_CMDDONE;
	}

	if (nArgc == 3)
	{
		space = toupper(psArgs[1][0]);
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
		switch (Eval_Range(psArgs[2], &lower, &upper))
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
	} /* continue */

	if (!dsp_memdump_upper)
	{
		if ( dsp_memdump_addr < (0xFFFF - 7))
			dsp_memdump_upper = dsp_memdump_addr + 7;
		else
			dsp_memdump_upper = 0xFFFF;
	}


	printf("DSP memdump from %hx in '%c' address space\n", dsp_memdump_addr, dsp_mem_space);
	DSP_DisasmMemory(dsp_memdump_addr, dsp_memdump_upper, dsp_mem_space);
	dsp_memdump_addr = dsp_memdump_upper + 1;

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
 * Toggle or list DSP breakpoints.
 */
static int DebugDsp_BreakPoint(int nArgc, char *psArgs[])
{
	int i;
	Uint16 addr;
	Uint32 BreakAddr;
	const char *symbol;

	/* List breakpoints? */
	if (nArgc == 1)
	{
		/* No arguments - so list available breakpoints */
		if (!nDspActiveBPs)
		{
			fputs("No DSP breakpoints set.\n", stderr);
			return DEBUGGER_CMDDONE;
		}

		fputs("Currently active DSP breakpoints:\n", stderr);
		for (i = 0; i < nDspActiveBPs; i++)
		{
			addr = DspBreakPoint[i];
			symbol = Symbols_GetByDspAddress(addr);
			if (symbol)
				fprintf(stderr, "%d. '%s'\n   ", i+1, symbol);
			else
				fprintf(stderr, "%d. ", i+1);
			DSP_DisasmAddress(addr, addr);
		}

		return DEBUGGER_CMDDONE;
	}

	/* Parse parameter as breakpoint value */
	if (!Symbols_GetDspAddress(SYMTYPE_TEXT, psArgs[1], &BreakAddr))
	{
		if (!Eval_Number(psArgs[1], &BreakAddr))
		{
			fprintf(stderr, "Invalid value '%s' for a DSP breakpoint!\n", psArgs[1]);
			return DEBUGGER_CMDDONE;
		}
	}
	if (BreakAddr > 0xFFFF)
	{
		fprintf(stderr, "Invalid address 0x%x for a DSP breakpoint!\n", BreakAddr);
		return DEBUGGER_CMDDONE;
	}

	/* Is the breakpoint already in the list? Then disable it! */
	for (i = 0; i < nDspActiveBPs; i++)
	{
		if (BreakAddr == DspBreakPoint[i])
		{
			DspBreakPoint[i] = DspBreakPoint[nDspActiveBPs-1];
			nDspActiveBPs -= 1;
			symbol = Symbols_GetByDspAddress(BreakAddr);
			if (symbol)
				fprintf(stderr, "DSP breakpoint at 0x%x (%s) deleted.\n", BreakAddr, symbol);
			else
				fprintf(stderr, "DSP breakpoint at 0x%x deleted.\n", BreakAddr);
			return DEBUGGER_CMDDONE;
		}
	}

	/* Is there at least one free slot available? */
	if (nDspActiveBPs == ARRAYSIZE(DspBreakPoint))
	{
		fputs("No more available free DSP breakpoints!\n", stderr);
		return DEBUGGER_CMDDONE;
	}

	/* Add new breakpoint */
	DspBreakPoint[nDspActiveBPs] = BreakAddr;
	nDspActiveBPs += 1;
	fprintf(stderr, "DSP breakpoint added at 0x%x.\n", BreakAddr);

	return DEBUGGER_CMDDONE;
}


/**
 * DSP wrapper for BreakCond_Command/BreakPointCount, returns DEBUGGER_END
 */
static int DebugDsp_BreakCond(int nArgc, char *psArgs[])
{
	BreakCond_Command(psArgs[1], true);
	nDspActiveCBs = BreakCond_BreakPointCount(true);
	return DEBUGGER_CMDDONE;
}


/**
 * Check if we hit a DSP breakpoint
 */
static void DebugDsp_CheckBreakpoints(void)
{
	Uint16 pc = DSP_GetPC();
	int i;

	for (i = 0; i < nDspActiveBPs; i++)
	{
		if (pc == DspBreakPoint[i])
		{
			const char *symbol;
			symbol = Symbols_GetByDspAddress(pc);
			if (symbol)
				fprintf(stderr, "\nDSP breakpoint at 0x%x (%s) ...", pc, symbol);
			else
				fprintf(stderr, "\nDSP breakpoint at 0x%x ...", pc);
			DebugUI();
			break;
		}
	}
}


/**
 * This function is called after each DSP instruction when debugging is enabled.
 */
void DebugDsp_Check(void)
{
	if (nDspActiveBPs)
	{
		DebugDsp_CheckBreakpoints();
	}
	if (nDspActiveCBs)
	{
		if (BreakCond_MatchDsp())
			DebugUI();
	}
	if (nDspSteps)
	{
		nDspSteps -= 1;
		if (nDspSteps == 0)
			DebugUI();
	}
}


/**
 * Should be called before returning back emulation to tell the DSP core
 * to call us after each instruction if "real-time" debugging like
 * breakpoints has been set.
 */
void DebugDsp_SetDebugging(void)
{
	if (nDspActiveBPs || nDspActiveCBs || nDspSteps)
		DSP_SetDebugging(true);
	else
		DSP_SetDebugging(false);
}


static const dbgcommand_t dspcommands[] =
{
	{ DebugDsp_BreakPoint, "dspaddress", "da",
	  "toggle or list (traditional) DSP address breakpoints",
	  "[address]\n"
	  "\tToggle breakpoint at <address> or list all breakpoints when\n"
	  "\tno address is given. Address can also be a loaded symbol name.",
	  false },
	{ DebugDsp_BreakCond, "dspbreak", "db",
	  "set/remove/list DSP register/RAM condition breakpoints",
	  BreakCond_Description,
	  true },
	{ DebugDsp_DisAsm, "dspdisasm", "dd",
	  "disassemble DSP code",
	  "[address]\n"
	  "\tDisassemble from DSP-PC, otherwise at given address.",
	  false },
	{ DebugDsp_MemDump, "dspmemdump", "dm",
	  "dump DSP memory",
	  "<x|y|p> [address]\n"
	  "\tdump DSP memory at address, or continue from previous address if not\n"
	  "\tspecified.",
	  false },
	{ Symbols_Command, "dspsymbols", "",
	  "load DSP symbols & their addresses",
	  "<filename|addr|name> [offset]\n"
	  "\tLoads symbol names and their addresses (with optional offset)\n"
	  "\tfrom given <filename>.  If there were previously loaded symbols,\n"
	  "\tthey're replaced.  Giving either 'name' or 'addr' instead of\n"
	  "\ta file name, will list the currently loaded symbols.",
	  false },
	{ DebugDsp_Register, "dspreg", "dr",
	  "read/write DSP registers",
	  "[REG=value]"
	  "\tSet or dump contents of DSP registers.",
	  true },
	{ DebugDsp_Continue, "dspcont", "dc",
	  "continue emulation / DSP single-stepping",
	  "[steps]\n"
	  "\tLeave debugger and continue emulation for <steps> DSP instructions\n"
	  "\tor forever if no steps have been specified.",
	  false }
};


/**
 * Should be called when debugger is entered to initialize
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
	return ARRAYSIZE(dspcommands);
}
