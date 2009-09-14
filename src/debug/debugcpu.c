/*
  Hatari - debugcpu.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  debugcpu.c - function needed for the CPU debugging tasks like memory
  and register dumps.
*/
const char DebugCpu_fileid[] = "Hatari debugcpu.c : " __DATE__ " " __TIME__;

#include <stdio.h>

#include "config.h"

#include "main.h"
#include "breakcond.h"
#include "debugui.h"
#include "debug_priv.h"
#include "debugcpu.h"
#include "hatari-glue.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "stMemory.h"
#include "str.h"

#define MEMDUMP_COLS   16      /* memdump, number of bytes per row */
#define MEMDUMP_ROWS   4       /* memdump, number of rows */
#define NON_PRINT_CHAR '.'     /* character to display for non-printables */
#define DISASM_INSTS   5       /* disasm - number of instructions */

static Uint32 disasm_addr;        /* disasm address */
static Uint32 memdump_addr;       /* memdump address */

static Uint32 CpuBreakPoint[16];  /* 68k breakpoints */
static int nCpuActiveBPs = 0;     /* Amount of active breakpoints */
static int nCpuActiveCBs = 0;     /* Amount of active conditional breakpoints */
static int nCpuSteps = 0;         /* Amount of steps for CPU single-stepping */


/**
 * Save/Restore snapshot of CPU debugging session variables
 */
void DebugCpu_MemorySnapShot_Capture(bool bSave)
{
	MemorySnapShot_Store(&disasm_addr, sizeof(disasm_addr));
	MemorySnapShot_Store(&memdump_addr, sizeof(memdump_addr));
	
	MemorySnapShot_Store(&CpuBreakPoint, sizeof(CpuBreakPoint));
	MemorySnapShot_Store(&nCpuActiveBPs, sizeof(nCpuActiveBPs));
	MemorySnapShot_Store(&nCpuActiveCBs, sizeof(nCpuActiveCBs));
}


/**
 * Load a binary file to a memory address.
 */
static int DebugCpu_LoadBin(int nArgc, char *psArgs[])
{
	FILE *fp;
	unsigned char c;
	Uint32 address;
	int i=0;

	if (nArgc < 3)
	{
		DebugUI_PrintCmdHelp(psArgs[0]);
		return DEBUGGER_CMDDONE;
	}

	if (!Str_GetNumber(psArgs[2], &address))
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
static int DebugCpu_SaveBin(int nArgc, char *psArgs[])
{
	FILE *fp;
	unsigned char c;
	Uint32 address;
	Uint32 bytes, i = 0;

	if (nArgc < 4)
	{
		DebugUI_PrintCmdHelp(psArgs[0]);
		return DEBUGGER_CMDDONE;
	}

	if (!Str_GetNumber(psArgs[2], &address))
	{
		fprintf(stderr, "  Invalid address!\n");
		return DEBUGGER_CMDDONE;
	}
	address &= 0x00FFFFFF;

	if (!Str_GetNumber(psArgs[3], &bytes))
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
	fprintf(stderr, "  Wrote 0x%x bytes.\n", bytes);

	return DEBUGGER_CMDDONE;
}


/**
 * Dissassemble - arg = starting address, or PC.
 */
static int DebugCpu_DisAsm(int nArgc, char *psArgs[])
{
	Uint32 disasm_upper = 0;
	uaecptr nextpc;

	if (nArgc > 1)
	{
		switch (Str_ParseRange(psArgs[1], &disasm_addr, &disasm_upper))
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
 * Handles D0-7 data and A0-7 address registers, but not PC & SR
 * registers as they need to be accessed using UAE accessors.
 */
int DebugCpu_GetRegisterAddress(const char *reg, Uint32 **addr)
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
	return 0;
}


/**
 * Dump or set CPU registers
 */
static int DebugCpu_Register(int nArgc, char *psArgs[])
{
	int i;
	char reg[3], *assign;
	Uint32 value;
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

	*assign++ = '\0';
	if (!Str_GetNumber(assign, &value))
	{
		fprintf(stderr,"\tError, usage: r or r xx=yyyy\n\tWhere: xx=A0-A7, D0-D7, PC or SR and yyyy is a hex value.\n");
		return DEBUGGER_CMDDONE;
	}
	
	for (i = 0; i < 2 && arg[i]; i++)
	{
		reg[i] = toupper(arg[i]);
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
		if (DebugCpu_GetRegisterAddress(reg, &regaddr))
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
static int DebugCpu_BreakPoint(int nArgc, char *psArgs[])
{
	int i;
	uaecptr nextpc;
	Uint32 BreakAddr;

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
	if (!Str_GetNumber(psArgs[1], &BreakAddr)
	    || (BreakAddr > STRamEnd && BreakAddr < 0xe00000)
	    || BreakAddr > 0xff0000)
	{
		fputs("Not a valid value for a CPU breakpoint!\n", stderr);
		return DEBUGGER_CMDDONE;
	}

	/* Is the breakpoint already in the list? Then disable it! */
	for (i = 0; i < nCpuActiveBPs; i++)
	{
		if (BreakAddr == CpuBreakPoint[i])
		{
			CpuBreakPoint[i] = CpuBreakPoint[nCpuActiveBPs-1];
			nCpuActiveBPs -= 1;
			fprintf(stderr, "CPU breakpoint at %x deleted.\n", BreakAddr);
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
	CpuBreakPoint[nCpuActiveBPs] = BreakAddr;
	nCpuActiveBPs += 1;
	fprintf(stderr, "CPU breakpoint added at %x.\n", BreakAddr);

	return DEBUGGER_CMDDONE;
}

/**
 * CPU wrapper for BreakCond_Command/BreakPointCount, returns DEBUGGER_END
 */
static int DebugCpu_BreakCond(int nArgc, char *psArgs[])
{
	BreakCond_Command((const char*)psArgs[1], false);
	nCpuActiveCBs = BreakCond_BreakPointCount(false);
	return DEBUGGER_CMDDONE;
}

/**
 * Do a memory dump, args = starting address.
 */
static int DebugCpu_MemDump(int nArgc, char *psArgs[])
{
	int i,j;
	char c;
	Uint32 memdump_upper = 0;

	if (nArgc > 1)
	{
		switch (Str_ParseRange(psArgs[1], &memdump_addr, &memdump_upper))
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
static int DebugCpu_MemWrite(int nArgc, char *psArgs[])
{
	int i, numBytes;
	Uint32 write_addr, d;
	unsigned char bytes[256]; /* store bytes */

	if (nArgc < 3)
	{
		DebugUI_PrintCmdHelp(psArgs[0]);
		return DEBUGGER_CMDDONE;
	}

	/* Read address */
	if (!Str_GetNumber(psArgs[1], &write_addr))
	{
		fprintf(stderr, "Bad address!\n");
		return DEBUGGER_CMDDONE;
	}

	write_addr &= 0x00FFFFFF;
	numBytes = 0;

	/* get bytes data */
	for (i = 2; i < nArgc; i++)
	{
		if (!Str_GetNumber(psArgs[i], &d) || d > 255)
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
 * Command: Continue CPU emulation / single-stepping
 */
static int DebugCpu_Continue(int nArgc, char *psArgv[])
{
	int steps = 0;
	
	if (nArgc > 1)
	{
		steps = atoi(psArgv[1]);
	}
	if (steps <= 0)
	{
		nCpuSteps = 0;
		fprintf(stderr,"Returning to emulation...\n");
		return DEBUGGER_END;
	}
	nCpuSteps = steps;
	fprintf(stderr,"Returning to emulation for %i CPU instructions...\n", steps);
	return DEBUGGER_END;
}


/**
 * Check if we hit a CPU breakpoint
 */
static void DebugCpu_CheckCpuBreakpoints(void)
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
void DebugCpu_Check(void)
{
	if (nCpuActiveBPs)
	{
		DebugCpu_CheckCpuBreakpoints();
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

/**
 * Should be called before returning back emulation to tell the CPU core
 * to call us after each instruction if "real-time" debugging like
 * breakpoints has been set.
 */
void DebugCpu_SetDebugging(void)
{
	if (nCpuActiveBPs || nCpuActiveCBs || nCpuSteps)
		M68000_SetSpecial(SPCFLAG_DEBUGGER);
	else
		M68000_UnsetSpecial(SPCFLAG_DEBUGGER);
}


static const dbgcommand_t cpucommands[] =
{
	{ DebugCpu_BreakPoint, "address", "a",
	  "toggle or list (traditional) CPU address breakpoints",
	  "[address]\n"
	  "\tToggle breakpoint at <address> or list all breakpoints when\n"
	  "\tno address is given.",
	  false	},
	{ DebugCpu_BreakCond, "breakpoint", "b",
	  "set/remove/list register/RAM condition breakpoints",
	  "[help | all | <breakpoint index> | <breakpoint condition>]\n"
	  "\tSet breakpoint with given condition, remove breakpoint with\n"
	  "\tgiven index or list all breakpoints when no args are given.\n"
	  "\t'help' outputs breakpoint condition syntax help, 'all' removes\n"
	  "\tall conditional breakpoints",
	  true },
	{ DebugCpu_DisAsm, "disasm", "d",
	  "disassemble from PC, or given address",
	  "[address]\n"
	  "\tIf no address is given, this command disassembles from the last\n"
	  "\tposition or from current PC if no last postition is available.",
	  false },
	{ DebugCpu_Register, "cpureg", "r",
	  "dump register values or set register to value",
	  "[REG=value]\n"
	  "\tSet CPU register to value or dumps all register if no parameter\n"
	  "\thas been specified.",
	  false },
	{ DebugCpu_MemDump, "memdump", "m",
	  "dump memory",
	  "[address]\n"
	  "\tdump memory at address or continue dump from previous address.",
	  false },
	{ DebugCpu_MemWrite, "memwrite", "w",
	  "write bytes to memory",
	  "address byte1 [byte2 ...]\n"
	  "\tWrite bytes to a memory address, bytes are space separated\n"
	  "\thexadecimals.",
	  false },
	{ DebugCpu_LoadBin, "loadbin", "l",
	  "load a file into memory",
	  "filename address\n"
	  "\tLoad the file <filename> into memory starting at <address>.",
	  false },
	{ DebugCpu_SaveBin, "savebin", "s",
	  "save memory to a file",
	  "filename address length\n"
	  "\tSave the memory block at <address> with given <length> to\n"
	  "\tthe file <filename>.",
	  false },
	{ DebugCpu_Continue, "cont", "c",
	  "continue emulation / CPU single-stepping",
	  "[steps]\n"
	  "\tLeave debugger and continue emulation for <steps> CPU instructions\n"
	  "\tor forever if no steps have been specified.",
	  false }
};


/**
 * Should be called when debugger is entered to initialize
 * CPU debugging variables.
 * 
 * if you want disassembly or memdumping to start/continue from
 * specific address, you can set them here.  If disassembly
 * address is zero, disassembling starts from PC.
 * 
 * returns number of CPU commands and pointer to array of them.
 */
int DebugCpu_Init(const dbgcommand_t **table)
{
	memdump_addr = 0;
	disasm_addr = 0;
	
	*table = cpucommands;
	return ARRAYSIZE(cpucommands);
}
