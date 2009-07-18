/*
  Hatari - debugui.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  debugui.c - this is the code for the mini-debugger. When the pause button is
  pressed, the emulator is (hopefully) halted and this little CLI can be used
  (in the terminal box) for debugging tasks like memory and register dumps.
*/
const char DebugUI_fileid[] = "Hatari debugui.c : " __DATE__ " " __TIME__;

#include <ctype.h>
#include <stdio.h>

#include "config.h"

#if HAVE_LIBREADLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "main.h"
#include "change.h"
#include "configuration.h"
#include "memorySnapShot.h"
#include "file.h"
#include "reset.h"
#include "m68000.h"
#include "str.h"
#include "stMemory.h"
#include "sound.h"
#include "tos.h"
#include "options.h"
#include "debugui.h"
#include "breakcond.h"
#include "hatari-glue.h"


#define MEMDUMP_COLS   16      /* memdump, number of bytes per row */
#define MEMDUMP_ROWS   4       /* memdump, number of rows */
#define NON_PRINT_CHAR '.'     /* character to display for non-printables */
#define DISASM_INSTS   5       /* disasm - number of instructions */

static Uint32 disasm_addr;        /* disasm address */
static Uint32 memdump_addr;       /* memdump address */

static Uint16 dsp_disasm_addr;    /* DSP disasm address */
static Uint16 dsp_memdump_addr;   /* DSP memdump address */
static char dsp_mem_space = 'P';  /* X, Y, P */

static FILE *debugOutput;

static Uint32 CpuBreakPoint[16];  /* 68k breakpoints */
static int nCpuActiveBPs = 0;     /* Amount of active breakpoints */
static int nCpuActiveCBs = 0;     /* Amount of active conditional breakpoints */
static int nCpuSteps = 0;         /* Amount of steps for CPU single-stepping */

static Uint16 DspBreakPoint[16];  /* DSP breakpoints */
static int nDspActiveBPs = 0;     /* Amount of active breakpoints */
static int nDspActiveCBs = 0;     /* Amount of active conditional breakpoints */
static int nDspSteps = 0;         /* Amount of steps for DSP single-stepping */

static int DebugUI_Help(int nArgc, char *psArgv[]);
static void DebugUI_PrintCmdHelp(const char *psCmd);


/**
 * Save/Restore snapshot of debugging session variables
 */
void DebugUI_MemorySnapShot_Capture(bool bSave)
{
	MemorySnapShot_Store(&disasm_addr, sizeof(disasm_addr));
	MemorySnapShot_Store(&memdump_addr, sizeof(memdump_addr));
	MemorySnapShot_Store(&dsp_disasm_addr, sizeof(dsp_disasm_addr));
	MemorySnapShot_Store(&dsp_memdump_addr, sizeof(dsp_memdump_addr));
	MemorySnapShot_Store(&dsp_mem_space, sizeof(dsp_mem_space));
	
	MemorySnapShot_Store(&CpuBreakPoint, sizeof(CpuBreakPoint));
	MemorySnapShot_Store(&nCpuActiveBPs, sizeof(nCpuActiveBPs));
	MemorySnapShot_Store(&nCpuActiveCBs, sizeof(nCpuActiveCBs));
	MemorySnapShot_Store(&DspBreakPoint, sizeof(DspBreakPoint));
	MemorySnapShot_Store(&nDspActiveBPs, sizeof(nDspActiveBPs));
	MemorySnapShot_Store(&nDspActiveCBs, sizeof(nDspActiveCBs));
	
	BreakCond_MemorySnapShot_Capture(bSave);
}


/**
 * Get a hex adress range, eg. "fa0000-fa0100" 
 * returns -1 if not a range,
 * -2 if a range, but not a valid one.
 * 0 if OK.
 */
static int getRange(char *str, Uint32 *lower, Uint32 *upper)
{
	bool fDash = false;
	int i=0;

	while (str[i] != '\0')
	{
		if (str[i] == '-')
		{
			str[i] = ' ';
			fDash = true;
		}
		i++;
	}
	if (fDash == false)
		return -1;

	i = sscanf(str, "%x%x", lower, upper);
	if (i != 2)
		return -2;
	if (*lower > *upper)
		return -3;
	return 0;
}


/**
 * Parse a hex adress range, eg. "fa0000[-fa0100]" + show appropriate warnings
 * returns:
 * -1 if invalid address or range,
 *  0 if single address,
 * +1 if a range.
 */
static int parseRange(char *str, Uint32 *lower, Uint32 *upper)
{
	int i;

	switch (getRange(str, lower, upper))
	{
	case 0:
		return 1;
	case -1:
		/* single address, not a range */
		if (!Str_IsHex(str))
		{
			fprintf(stderr,"Invalid address '%s'!\n", str);
			return -1;
		}
		i = sscanf(str, "%x", lower);
		
		if (i == 0)
		{
			fprintf(stderr,"Invalid address '%s'!\n", str);
			return -1;
		}
		return 0;
	case -2:
		fprintf(stderr,"Invalid addresses '%s'!\n", str);
		return -1;
	case -3:
		fprintf(stderr,"Invalid range (%x > %x)!\n", *lower, *upper);
		return -1;
	}
	fprintf(stderr, "Unknown getRange() return value!\n");
	return -1;
}


/**
 * Close a log file if open, and set it to default stream.
 */
static void DebugUI_SetLogDefault(void)
{
	if (debugOutput != stderr)
	{
		if (debugOutput)
		{
			File_Close(debugOutput);
			fprintf(stderr, "Debug log closed.\n");
		}
		debugOutput = stderr;
	}
}


/**
 * Open (or close) given log file.
 */
static int DebugUI_SetLogFile(int nArgc, char *psArgs[])
{
	File_Close(debugOutput);
	debugOutput = NULL;

	if (nArgc > 1)
		debugOutput = File_Open(psArgs[1], "w");

	if (debugOutput)
		fprintf(stderr, "Debug log '%s' opened.\n", psArgs[1]);
	else
		debugOutput = stderr;

	return DEBUGGER_CMDDONE;
}


/**
 * Load a binary file to a memory address.
 */
static int DebugUI_LoadBin(int nArgc, char *psArgs[])
{
	FILE *fp;
	unsigned char c;
	unsigned long address;
	int i=0;

	if (nArgc < 3)
	{
		DebugUI_PrintCmdHelp(psArgs[0]);
		return DEBUGGER_CMDDONE;
	}

	if (sscanf(psArgs[2], "%lx", &address) != 1)
	{
		fprintf(stderr, "Invalid address!\n");
		return DEBUGGER_CMDDONE;
	}
	address &= 0x00FFFFFF;

	if ((fp = fopen(psArgs[1], "rb")) == NULL)
	{
		fprintf(stderr, "Cannot open file '%s'!\n", psArgs[1]);
		return DEBUGGER_CMDDONE;
	}

	c = fgetc(fp);
	while (!feof(fp))
	{
		i++;
		STMemory_WriteByte(address++, c);
		c = fgetc(fp);
	}
	fprintf(stderr,"  Read 0x%x bytes.\n", i);
	fclose(fp);

	return DEBUGGER_CMDDONE;
}


/**
 * Dump memory from an address to a binary file.
 */
static int DebugUI_SaveBin(int nArgc, char *psArgs[])
{
	FILE *fp;
	unsigned char c;
	unsigned long address;
	unsigned long bytes, i=0;

	if (nArgc < 4)
	{
		DebugUI_PrintCmdHelp(psArgs[0]);
		return DEBUGGER_CMDDONE;
	}

	if (sscanf(psArgs[2], "%lx", &address) != 1)
	{
		fprintf(stderr, "  Invalid address!\n");
		return DEBUGGER_CMDDONE;
	}
	address &= 0x00FFFFFF;

	if (sscanf(psArgs[3], "%lx", &bytes) != 1)
	{
		fprintf(stderr, "  Invalid length!\n");
		return DEBUGGER_CMDDONE;
	}

	if ((fp = fopen(psArgs[1], "wb")) == NULL)
	{
		fprintf(stderr,"  Cannot open file '%s'!\n", psArgs[1]);
		return DEBUGGER_CMDDONE;
	}

	while (i < bytes)
	{
		c = STMemory_ReadByte(address++);
		fputc(c, fp);
		i++;
	}
	fclose(fp);
	fprintf(stderr, "  Wrote 0x%lx bytes.\n", bytes);

	return DEBUGGER_CMDDONE;
}


#if ENABLE_DSP_EMU

#include "dsp.h"

/**
 * Command: Dump or set a DSP register
 */
static int DebugUI_DspRegister(int nArgc, char *psArgs[])
{
	int i;
	char reg[4], *assign;
	long value;
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
	/* has '=' and reg name is max. 3 letters that fit to string */
	if (!assign || assign - arg > 3+1)
		goto error_msg;

	*assign = ' ';
	if (sscanf(arg, "%s%lx", reg, &value) != 2)
		goto error_msg;

	for (i = 0; i < 3; i++)
		reg[i] = toupper(reg[i]);

	DSP_Disasm_SetRegister(reg, value);
	return DEBUGGER_CMDDONE;

	error_msg:
	fprintf(stderr,"\tError, usage: dr or dr xx=yyyy\n"
		"\tWhere: xx=A0-A2, B0-B2, X0, X1, Y0, Y1, R0-R7,\n"
		"\t       N0-N7, M0-M7, LA, LC, PC, SR, SP, OMR, SSH, SSL\n"
		"\tand yyyy is a hex value.\n");

	return DEBUGGER_CMDDONE;
}


/**
 * DSP dissassemble - arg = starting address/range, or PC.
 */
static int DebugUI_DspDisAsm(int nArgc, char *psArgs[])
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
		switch (parseRange(psArgs[1], &lower, &upper))
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
					fprintf(stderr,"Invalid address '%x'!\n", upper);
					return DEBUGGER_CMDDONE;
				}
				dsp_disasm_upper = upper;
				break;
		}

		if (lower > 0xFFFF)
		{
			fprintf(stderr,"Invalid address '%x'!\n", lower);
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
static int DebugUI_DspMemDump(int nArgc, char *psArgs[])
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
		switch (parseRange(psArgs[2], &lower, &upper))
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
				fprintf(stderr,"Invalid address '%x'!\n", upper);
				return DEBUGGER_CMDDONE;
			}
			dsp_memdump_upper = upper;
			break;
		}
		if (lower > 0xFFFF)
		{
			fprintf(stderr,"Invalid address '%x'!\n", lower);
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
 * Toggle or list DSP breakpoints.
 */
static int DebugUI_DspBreakPoint(int nArgc, char *psArgs[])
{
	int i;
	Uint16 addr;
	unsigned int nBreakPoint;

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
			DSP_DisasmAddress(addr, addr);
		}

		return DEBUGGER_CMDDONE;
	}

	/* Parse parameter as breakpoint value */
	if (sscanf(psArgs[1], "%x", &nBreakPoint) != 1
	    || nBreakPoint > 0xFFFF)
	{
		fputs("Not a valid value for a DSP breakpoint!\n", stderr);
		return DEBUGGER_CMDDONE;
	}

	/* Is the breakpoint already in the list? Then disable it! */
	for (i = 0; i < nDspActiveBPs; i++)
	{
		if (nBreakPoint == DspBreakPoint[i])
		{
			DspBreakPoint[i] = DspBreakPoint[nDspActiveBPs-1];
			nDspActiveBPs -= 1;
			fprintf(stderr, "DSP breakpoint at %x deleted.\n", nBreakPoint);
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
	DspBreakPoint[nDspActiveBPs] = nBreakPoint;
	nDspActiveBPs += 1;
	fprintf(stderr, "DSP breakpoint added at %x.\n", nBreakPoint);

	return DEBUGGER_CMDDONE;
}

/**
 * DSP wrapper for BreakCond_Command/BreakPointCount, returns DEBUGGER_END
 */
static int DebugUI_BreakCondDsp(int nArgc, char *psArgs[])
{
	BreakCond_Command((const char *)psArgs[1], true);
	nDspActiveCBs = BreakCond_BreakPointCount(true);
	return DEBUGGER_CMDDONE;
}

/**
 * Check if we hit a DSP breakpoint
 */
static void DebugUI_CheckDspBreakpoints(void)
{
	Uint16 pc = DSP_GetPC();
	int i;

	for (i = 0; i < nDspActiveBPs; i++)
	{
		if (pc == DspBreakPoint[i])
		{
			fprintf(stderr, "\nDSP breakpoint at %x ...", pc);
			DebugUI();
			break;
		}
	}
}


/**
 * This function is called after each DSP instruction when debugging is enabled.
 */
void DebugUI_DspCheck(void)
{
	if (nDspActiveBPs)
	{
		DebugUI_CheckDspBreakpoints();
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

#endif /* ENABLE_DSP_EMU */



/**
 * Dissassemble - arg = starting address, or PC.
 */
static int DebugUI_DisAsm(int nArgc, char *psArgs[])
{
	Uint32 disasm_upper = 0;
	uaecptr nextpc;

	if (nArgc > 1)
	{
		switch (parseRange(psArgs[1], &disasm_addr, &disasm_upper))
		{
		case -1:
			/* invalid value(s) */
			return DEBUGGER_CMDDONE;
		case 0:
			/* single value */
			break;
		case 1:
			/* range */
			disasm_upper &= 0x00FFFFFF;
			break;
		}
	}
	else
	{
		/* continue */
		if(!disasm_addr)
			disasm_addr = M68000_GetPC();
	}
	disasm_addr &= 0x00FFFFFF;

	/* output a single block. */
	if (!disasm_upper)
	{
		m68k_disasm(debugOutput, (uaecptr)disasm_addr, &nextpc, DISASM_INSTS);
		disasm_addr = nextpc;
		fflush(debugOutput);
		return DEBUGGER_CMDCONT;
	}

	/* output a range */
	while (disasm_addr < disasm_upper)
	{
		m68k_disasm(debugOutput, (uaecptr)disasm_addr, &nextpc, 1);
		disasm_addr = nextpc;
	}
	fflush(debugOutput);

	return DEBUGGER_CMDCONT;
}


/**
 * Set address of the named register to given argument.
 * Return register size in bits or zero for uknown register name.
 * Handles D0-7, A0-7 and also PC & SR registers, but note that both
 * PC & SR would need special handling (using UAE accessors).
 */
int DebugUI_GetCpuRegisterAddress(const char *reg, Uint32 **addr)
{
	char r0, r1;
	if (!reg[0] || !reg[1] || reg[2])
		return 0;
	
	r0 = toupper(reg[0]);
	r1 = toupper(reg[1]);

	if (r0 == 'D')  /* Data regs? */
	{
		if (r1 >= '0' && r1 <= '7')
		{
			*addr = &(Regs[REG_D0 + r1 - '0']);
			return 32;
		}
		fprintf(stderr,"\tBad data register, valid values are 0-7\n");
		return 0;
	}
	if(r0 == 'A')  /* Address regs? */
	{
		if (r1 >= '0' && r1 <= '7')
		{
			*addr = &(Regs[REG_A0 + r1 - '0']);
			return 32;
		}
		fprintf(stderr,"\tBad address register, valid values are 0-7\n");
		return 0;
	}
	if (r0 == 'P' && r1 == 'C')
	{
		*addr = &regs.pc;
		return 32;
	}
	if (r0 == 'S' && r1 == 'R')
	{
		*addr = (Uint32 *)&regs.sr;
		return 16;
	}
	return 0;
}


/**
 * Dump or set CPU registers
 */
static int DebugUI_CpuRegister(int nArgc, char *psArgs[])
{
	int i;
	char reg[3], *assign;
	long value;
	char *arg;

	/* If no parameter has been given, simply dump all registers */
	if (nArgc == 1)
	{
		uaecptr nextpc;
		/* use the UAE function instead */
		m68k_dumpstate(debugOutput, &nextpc);
		fflush(debugOutput);
		return DEBUGGER_CMDDONE;
	}

	arg =  psArgs[1];

	assign = strchr(arg, '=');
	/* has '=' and reg name is max. 2 letters that fit to string */
	if (!assign || assign - arg > 2+1)
	{
		fprintf(stderr,"\tError, usage: r or r xx=yyyy\n\tWhere: xx=A0-A7, D0-D7, PC or SR and yyyy is a hex value.\n");
		return DEBUGGER_CMDDONE;
	}

	*assign = ' ';
	if (sscanf(arg, "%s%lx", reg, &value) != 2)
	{
		fprintf(stderr,"\tError, usage: r or r xx=yyyy\n\tWhere: xx=A0-A7, D0-D7, PC or SR and yyyy is a hex value.\n");
		return DEBUGGER_CMDDONE;
	}
	
	for (i = 0; i < 2 && reg[i]; i++)
	{
		reg[i] = toupper(reg[i]);
	}
	
	/* set SR and update conditional flags for the UAE CPU core. */
	if (reg[0] == 'S' && reg[1] == 'R')
	{
		M68000_SetSR(value);
	}
	else if (reg[0] == 'P' && reg[1] == 'C')   /* set PC? */
	{
		M68000_SetPC(value);
	}
	else
	{
		Uint32 *regaddr;
		/* check&set data and address registers */
		if (DebugUI_GetCpuRegisterAddress(reg, &regaddr))
		{
			*regaddr = value;
		}
		else
		{
			fprintf(stderr, "\t Bad register!\n");
		}
	}
	return DEBUGGER_CMDDONE;
}


/**
 * Toggle or list CPU breakpoints.
 */
static int DebugUI_CpuBreakPoint(int nArgc, char *psArgs[])
{
	int i;
	uaecptr nextpc;
	unsigned int nBreakPoint;

	/* List breakpoints? */
	if (nArgc == 1)
	{
		/* No arguments - so list available breakpoints */
		if (!nCpuActiveBPs)
		{
			fputs("No CPU breakpoints set.\n", stderr);
			return DEBUGGER_CMDDONE;
		}

		fputs("Currently active CPU breakpoints:\n", stderr);
		for (i = 0; i < nCpuActiveBPs; i++)
		{
			m68k_disasm(stderr, (uaecptr)CpuBreakPoint[i], &nextpc, 1);
		}

		return DEBUGGER_CMDDONE;
	}

	/* Parse parameter as breakpoint value */
	if (sscanf(psArgs[1], "%x", &nBreakPoint) != 1
	    || (nBreakPoint > STRamEnd && nBreakPoint < 0xe00000)
	    || nBreakPoint > 0xff0000)
	{
		fputs("Not a valid value for a CPU breakpoint!\n", stderr);
		return DEBUGGER_CMDDONE;
	}

	/* Is the breakpoint already in the list? Then disable it! */
	for (i = 0; i < nCpuActiveBPs; i++)
	{
		if (nBreakPoint == CpuBreakPoint[i])
		{
			CpuBreakPoint[i] = CpuBreakPoint[nCpuActiveBPs-1];
			nCpuActiveBPs -= 1;
			fprintf(stderr, "CPU breakpoint at %x deleted.\n", nBreakPoint);
			return DEBUGGER_CMDDONE;
		}
	}

	/* Is there at least one free slot available? */
	if (nCpuActiveBPs == ARRAYSIZE(CpuBreakPoint))
	{
		fputs("No more available free CPU breakpoints!\n", stderr);
		return DEBUGGER_CMDDONE;
	}

	/* Add new breakpoint */
	CpuBreakPoint[nCpuActiveBPs] = nBreakPoint;
	nCpuActiveBPs += 1;
	fprintf(stderr, "CPU breakpoint added at %x.\n", nBreakPoint);

	return DEBUGGER_CMDDONE;
}

/**
 * CPU wrapper for BreakCond_Command/BreakPointCount, returns DEBUGGER_END
 */
static int DebugUI_BreakCondCpu(int nArgc, char *psArgs[])
{
	BreakCond_Command((const char*)psArgs[1], false);
	nCpuActiveCBs = BreakCond_BreakPointCount(false);
	return DEBUGGER_CMDDONE;
}

/**
 * Do a memory dump, args = starting address.
 */
static int DebugUI_MemDump(int nArgc, char *psArgs[])
{
	int i,j;
	char c;
	Uint32 memdump_upper = 0;

	if (nArgc > 1)
	{
		switch (parseRange(psArgs[1], &memdump_addr, &memdump_upper))
		{
		case -1:
			/* invalid value(s) */
			return DEBUGGER_CMDDONE;
		case 0:
			/* single value */
			break;
		case 1:
			/* range */
			memdump_upper &= 0x00FFFFFF;
			break;
		}
	} /* continue */
	memdump_addr &= 0x00FFFFFF;

	if (!memdump_upper)
	{
		for (j=0;j<MEMDUMP_ROWS;j++)
		{
			fprintf(debugOutput, "%6.6X: ", memdump_addr); /* print address */
			for (i = 0; i < MEMDUMP_COLS; i++)               /* print hex data */
				fprintf(debugOutput, "%2.2x ", STMemory_ReadByte(memdump_addr++));
			fprintf(debugOutput, "  ");                     /* print ASCII data */
			for (i = 0; i < MEMDUMP_COLS; i++)
			{
				c = STMemory_ReadByte(memdump_addr-MEMDUMP_COLS+i);
				if (!isprint((unsigned)c))
					c = NON_PRINT_CHAR;         /* non-printable as dots */
				fprintf(debugOutput,"%c", c);
			}
			fprintf(debugOutput, "\n");        /* newline */
		}
		fflush(debugOutput);
		return DEBUGGER_CMDCONT;
	} /* not a range */

	while (memdump_addr < memdump_upper)
	{
		fprintf(debugOutput, "%6.6X: ", memdump_addr); /* print address */
		for (i = 0; i < MEMDUMP_COLS; i++)               /* print hex data */
			fprintf(debugOutput, "%2.2x ", STMemory_ReadByte(memdump_addr++));
		fprintf(debugOutput, "  ");                     /* print ASCII data */
		for (i = 0; i < MEMDUMP_COLS; i++)
		{
			c = STMemory_ReadByte(memdump_addr-MEMDUMP_COLS+i);
			if(!isprint((unsigned)c))
				c = NON_PRINT_CHAR;             /* non-printable as dots */
			fprintf(debugOutput,"%c", c);
		}
		fprintf(debugOutput, "\n");            /* newline */
	} /* while */
	fflush(debugOutput);

	return DEBUGGER_CMDCONT;
}


/**
 * Command: Write to memory, arg = starting address, followed by bytes.
 */
static int DebugUI_MemWrite(int nArgc, char *psArgs[])
{
	int i, numBytes;
	long write_addr;
	unsigned char bytes[256]; /* store bytes */
	int d;


	if (nArgc < 3)
	{
		DebugUI_PrintCmdHelp(psArgs[0]);
		return DEBUGGER_CMDDONE;
	}

	/* Read address */
	i = sscanf(psArgs[1], "%lx", &write_addr);
	/* if next char is not valid, or it's not a valid address */
	if (i != 1)
	{
		fprintf(stderr, "Bad address! (must be hexadecimal)\n");
		return DEBUGGER_CMDDONE;
	}

	write_addr &= 0x00FFFFFF;
	numBytes = 0;

	/* get bytes data */
	for (i = 2; i < nArgc; i++)
	{
		if (sscanf(psArgs[i], "%x", &d) != 1 || d > 255)
		{
			fprintf(stderr, "Bad byte argument: '%s'!\n", psArgs[i]);
			return DEBUGGER_CMDDONE;
		}

		bytes[numBytes] = d & 0x0FF;
		numBytes++;
	}

	/* write the data */
	for (i = 0; i < numBytes; i++)
		STMemory_WriteByte(write_addr + i, bytes[i]);

	return DEBUGGER_CMDDONE;
}


/**
 * Command: Set options
 */
static int DebugUI_SetOptions(int argc, char *argv[])
{
	CNF_PARAMS current;

	/* get configuration changes */
	current = ConfigureParams;

	/* Parse and apply options */
	if (Opt_ParseParameters(argc, (const char**)argv))
	{
		ConfigureParams.Screen.bFullScreen = false;
		Change_CopyChangedParamsToConfiguration(&current, &ConfigureParams, false);
	}
	else
	{
		ConfigureParams = current;
	}

	return DEBUGGER_CMDDONE;
}


/**
 * Command: Continue emulation / single-stepping
 */
static int DebugUI_Continue(int nArgc, char *psArgv[], bool bStepDsp)
{
	const char *chip;
	int steps = 0;
	
	if (nArgc > 1)
	{
		steps = atoi(psArgv[1]);
	}
	/* at most one should be active at the same time */
	nDspSteps = 0;
	nCpuSteps = 0;
	if (steps <= 0)
	{
		fprintf(stderr,"Returning to emulation...\n------------------------------\n\n");
		return DEBUGGER_END;
	}
	if (bStepDsp)
	{
		nDspSteps = steps;
#if ENABLE_DSP_EMU
		chip = "DSP";
#else
		chip = "<NONE>";
#endif
	} else {
		nCpuSteps = steps;
		chip = "CPU";
	}
	fprintf(stderr,"Returning to emulation for %i %s instructions...\n", steps, chip);
	return DEBUGGER_END;
}

/**
 * Command: Continue emulation / single-stepping CPU wrapper
 */
static int DebugUI_CpuContinue(int nArgc, char *psArgv[])
{
	return DebugUI_Continue(nArgc, psArgv, false);
}
/**
 * Command: Continue emulation / single-stepping DSP wrapper
 */
static int DebugUI_DspContinue(int nArgc, char *psArgv[])
{
	return DebugUI_Continue(nArgc, psArgv, true);
}


/**
 * Command: Quit emulator
 */
static int DebugUI_QuitEmu(int nArgc, char *psArgv[])
{
	bQuitProgram = true;
	M68000_SetSpecial(SPCFLAG_BRK);   /* Assure that CPU core shuts down */
	return DEBUGGER_END;
}


typedef struct
{
	int (*pFunction)(int argc, char *argv[]);
	const char *sLongName;
	const char *sShortName;
	const char *sShortDesc;
	const char *sUsage;
	bool bNoParsing;
} dbgcommand_t;

dbgcommand_t commandtab[] =
{
#if ENABLE_DSP_EMU
	{ DebugUI_DspBreakPoint, "dspaddress", "da",
	  "toggle or list (traditional) DSP address breakpoints",
	  "[address]\n"
	  "\tToggle breakpoint at <address> or list all breakpoints when\n"
	  "\tno address is given.",
	  false },
	{ DebugUI_BreakCondDsp, "dspbreak", "db",
	  "set/remove/list DSP register/RAM condition breakpoints",
	  "[help | all | <breakpoint index> | <breakpoint condition>]\n"
	  "\tSet breakpoint with given condition, remove breakpoint with\n"
	  "\tgiven index or list all breakpoints when no args are given.\n"
	  "\t'help' outputs breakpoint condition syntax help, 'all' removes\n"
	  "\tall conditional breakpoints",
	  true },
	{ DebugUI_DspDisAsm, "dspdisasm", "dd",
	  "disassemble DSP code",
	  "[address]\n"
	  "\tDisassemble from DSP-PC, otherwise at given address.",
	  false },
	{ DebugUI_DspMemDump, "dspmemdump", "dm",
	  "dump DSP memory",
	  "<x|y|p> [address]\n"
	  "\tdump DSP memory at address, or continue from previous address if not\n"
	  "\tspecified.",
	  false },
	{ DebugUI_DspRegister, "dspreg", "dr",
	  "read/write DSP registers",
	  "[REG=value]"
	  "\tSet or dump contents of DSP registers.",
	  false },
	{ DebugUI_DspContinue, "dspcont", "dc",
	  "continue emulation / DSP single-stepping",
	  "[steps]\n"
	  "\tLeave debugger and continue emulation for <steps> DSP instructions\n"
	  "\tor forever if no steps have been specified.",
	  false },
#endif
	{ DebugUI_CpuBreakPoint, "address", "a",
	  "toggle or list (traditional) CPU address breakpoints",
	  "[address]\n"
	  "\tToggle breakpoint at <address> or list all breakpoints when\n"
	  "\tno address is given.",
	  false	},
	{ DebugUI_BreakCondCpu, "breakpoint", "b",
	  "set/remove/list register/RAM condition breakpoints",
	  "[help | all | <breakpoint index> | <breakpoint condition>]\n"
	  "\tSet breakpoint with given condition, remove breakpoint with\n"
	  "\tgiven index or list all breakpoints when no args are given.\n"
	  "\t'help' outputs breakpoint condition syntax help, 'all' removes\n"
	  "\tall conditional breakpoints",
	  true },
	{ DebugUI_DisAsm, "disasm", "d",
	  "disassemble from PC, or given address",
	  "[address]\n"
	  "\tIf no address is given, this command disassembles from the last\n"
	  "\tposition or from current PC if no last postition is available.",
	  false },
	{ DebugUI_CpuRegister, "cpureg", "r",
	  "dump register values or set register to value",
	  "[REG=value]\n"
	  "\tSet CPU register to value or dumps all register if no parameter\n"
	  "\thas been specified.",
	  false },
	{ DebugUI_MemDump, "memdump", "m",
	  "dump memory",
	  "[address]\n"
	  "\tdump memory at address or continue dump from previous address.",
	  false },
	{ DebugUI_MemWrite, "memwrite", "w",
	  "write bytes to memory",
	  "address byte1 [byte2 ...]\n"
	  "\tWrite bytes to a memory address, bytes are space separated.",
	  false },
	{ DebugUI_SetLogFile, "logfile", "f",
	  "open or close log file",
	  "[filename]\n"
	  "\tOpen log file, no argument closes the log file. Output of\n"
	  "\tregister & memory dumps and disassembly will be written to it.",
	  false },
	{ DebugUI_LoadBin, "loadbin", "l",
	  "load a file into memory",
	  "filename address\n"
	  "\tLoad the file <filename> into memory starting at <address>.",
	  false },
	{ DebugUI_SaveBin, "savebin", "s",
	  "save memory to a file",
	  "filename address length\n"
	  "\tSave the memory block at <address> with given <length> to\n"
	  "\tthe file <filename>.",
	  false },
	{ DebugUI_SetOptions, "setopt", "o",
	  "set Hatari command line options",
	  "[command line parameters]\n"
	  "\tSet options like command line parameters. For example to"
	  "\tenable CPU disasm tracing:  setopt --trace cpu_disasm",
	  false },
	{ DebugUI_CpuContinue, "cont", "c",
	  "continue emulation / CPU single-stepping",
	  "[steps]\n"
	  "\tLeave debugger and continue emulation for <steps> CPU instructions\n"
	  "\tor forever if no steps have been specified.",
	  false },
	{ DebugUI_QuitEmu, "quit", "q",
	  "quit emulator",
	  "\n"
	  "\tLeave debugger and quit emulator.",
	  false },
	{ DebugUI_Help, "help", "h",
	  "print help",
	  "[command]"
	  "\tPrint help text for available commands.",
	  false },
};


/**
 * Print help text for one command
 */
static void DebugUI_PrintCmdHelp(const char *psCmd)
{
	int i;

	/* Search the command ... */
	for (i = 0; i < ARRAYSIZE(commandtab); i++)
	{
		if (!strcmp(psCmd, commandtab[i].sLongName)
		    || !strcmp(psCmd, commandtab[i].sShortName))
		{
			/* ... and print help text */
			fprintf(stderr, "'%s' or '%s' - %s\n",
				commandtab[i].sLongName, commandtab[i].sShortName,
				commandtab[i].sShortDesc);
			fprintf(stderr, "Usage:  %s %s\n", commandtab[i].sShortName,
				commandtab[i].sUsage);
			return;
		}
	}

	fprintf(stderr, "Unknown command '%s'\n", psCmd);
}


/**
 * Command: Print debugger help screen.
 */
static int DebugUI_Help(int nArgc, char *psArgs[])
{
	int i;

	if (nArgc > 1)
	{
		DebugUI_PrintCmdHelp(psArgs[1]);
		return DEBUGGER_CMDDONE;
	}

	fputs("Available commands:\n", stderr);
	for (i = 0; i < ARRAYSIZE(commandtab); i++)
	{
		fprintf(stderr, " %12s (%2s) : %s\n", commandtab[i].sLongName,
			commandtab[i].sShortName, commandtab[i].sShortDesc);
	}

	fputs("Adresses may be given as a range e.g. 'fc0000-fc0100'.\n"
	      "All values in hexadecimal. 'h <command>' gives more help.\n",
	      stderr);
	return DEBUGGER_CMDDONE;
}


/**
 * Parse debug command and execute it.
 */
int DebugUI_ParseCommand(char *input)
{
	char *psArgs[64];
	const char *delim;
	static char sLastCmd[80] = { '\0' };
	int nArgc, cmd = -1;
	int i, retval;

	psArgs[0] = strtok(input, " \t");

	if (psArgs[0] == NULL)
	{
		if (strlen(sLastCmd) > 0)
			psArgs[0] = sLastCmd;
		else
			return DEBUGGER_CMDDONE;
	}

	/* Search the command ... */
	for (i = 0; i < ARRAYSIZE(commandtab); i++)
	{
		if (!strcmp(psArgs[0], commandtab[i].sLongName)
		    || !strcmp(psArgs[0], commandtab[i].sShortName))
		{
			cmd = i;
			break;
		}
	}
	if (cmd == -1)
	{
		fprintf(stderr, "Command '%s' not found.\n"
			"Use 'help' to view a list of available commands.\n",
			psArgs[0]);
		return DEBUGGER_CMDDONE;
	}

	if (commandtab[cmd].bNoParsing)
		delim = "";
	else
		delim = " \t";

	/* Separate arguments and put the pointers into psArgs */
	for (nArgc = 1; nArgc < ARRAYSIZE(psArgs); nArgc++)
	{
		psArgs[nArgc] = strtok(NULL, delim);
		if (psArgs[nArgc] == NULL)
			break;
	}

	if (!debugOutput) {
		/* make sure also calls from control.c work */
		DebugUI_SetLogDefault();
	}

	/* ... and execute the function */
	retval = commandtab[i].pFunction(nArgc, psArgs);
	/* Save commando string if it can be repeated */
	if (retval == DEBUGGER_CMDCONT)
		strncpy(sLastCmd, psArgs[0], sizeof(sLastCmd));
	else
		sLastCmd[0] = '\0';
	return retval;
}


/**
 * Read a command line from the keyboard and return a pointer to the string.
 * @return	Pointer to the string which should be deallocated free()
 *              after use. Returns NULL when error occured.
 */
static char *DebugUI_GetCommand(void)
{
	char *input;

#if HAVE_LIBREADLINE
	input = readline("> ");
	if (!input)
		return NULL;
	if (input[0] != 0)
		add_history(input);
#else
	fprintf(stderr, "> ");
	input = malloc(256);
	if (!input)
		return NULL;
	input[0] = '\0';
	if (fgets(input, 256, stdin) == NULL)
	{
		free(input);
		return NULL;
	}
#endif
	input = Str_Trim(input);

	return input;
}


/**
 * Debugger user interface main function.
 */
void DebugUI(void)
{
	int cmdret;

	/* if you want disassembly or memdumping to start/continue from
	 * specific address, you can set them here.  If disassembly
	 * address is zero, disassembling starts from PC.
	 */
#if ENABLE_DSP_EMU
	dsp_disasm_addr = 0;
	dsp_memdump_addr = 0;
	dsp_mem_space = 'P';
#endif
	memdump_addr = 0;
	disasm_addr = 0;

	fprintf(stderr, "\n----------------------------------------------------------------------"
	                "\nYou have entered debug mode. Type c to continue emulation, h for help.\n");

	do
	{
		char *psCmd;

		/* Read command from the keyboard */
		psCmd = DebugUI_GetCommand();

		if (psCmd)
		{
			/* Parse and execute the command string */
			cmdret = DebugUI_ParseCommand(psCmd);
			free(psCmd);
		}
		else
		{
			cmdret = DEBUGGER_END;
		}
	}
	while (cmdret != DEBUGGER_END);

	DebugUI_SetLogDefault();

	/* If "real-time" debugging like breakpoints has been set, we've
	 * got to tell the CPU core to call us after each instruction! */
	if (nCpuActiveBPs || nCpuActiveCBs || nCpuSteps)
		M68000_SetSpecial(SPCFLAG_DEBUGGER);
	else
		M68000_UnsetSpecial(SPCFLAG_DEBUGGER);
	/* ...and DSP core */
	if (nDspActiveBPs || nDspActiveCBs || nDspSteps)
		DSP_SetDebugging(true);
	else
		DSP_SetDebugging(false);
}


/**
 * Check if we hit a CPU breakpoint
 */
static void DebugUI_CheckCpuBreakpoints(void)
{
	Uint32 pc = M68000_GetPC();
	int i;

	for (i = 0; i < nCpuActiveBPs; i++)
	{
		if (pc == CpuBreakPoint[i])
		{
			fprintf(stderr, "\nCPU breakpoint at %x ...", pc);
			DebugUI();
			break;
		}
	}
}


/**
 * This function is called after each CPU instruction when debugging is enabled.
 */
void DebugUI_CpuCheck(void)
{
	if (nCpuActiveBPs)
	{
		DebugUI_CheckCpuBreakpoints();
	}
	if (nCpuActiveCBs)
	{
		if (BreakCond_MatchCpu())
			DebugUI();
	}
	if (nCpuSteps)
	{
		nCpuSteps -= 1;
		if (nCpuSteps == 0)
			DebugUI();
	}
}
