/*
  Hatari - debugcpu.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  debugcpu.c - function needed for the CPU debugging tasks like memory
  and register dumps.
*/
const char DebugCpu_fileid[] = "Hatari debugcpu.c";

#include <stdio.h>
#include <ctype.h>
#include <limits.h>

#include "config.h"

#include "main.h"
#include "breakcond.h"
#include "configuration.h"
#include "debugui.h"
#include "debug_priv.h"
#include "debugcpu.h"
#include "evaluate.h"
#include "hatari-glue.h"
#include "history.h"
#include "log.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "profile.h"
#include "stMemory.h"
#include "str.h"
#include "symbols.h"
#include "68kDisass.h"
#include "console.h"
#include "options.h"
#include "vars.h"


#define MEMDUMP_COLS   16      /* memdump, number of bytes per row */
#define NON_PRINT_CHAR '.'     /* character to display for non-printables */

static uint32_t disasm_addr;     /* disasm address */
static uint32_t memdump_addr;    /* memdump address */
static uint32_t fake_regs[8];    /* virtual debugger "registers" */
static bool bFakeRegsUsed;     /* whether to show virtual regs */

static bool bCpuProfiling;     /* Whether CPU profiling is activated */
static int nCpuActiveCBs = 0;  /* Amount of active conditional breakpoints */
static int nCpuSteps = 0;      /* Amount of steps for CPU single-stepping */


/**
 * Load a binary file to a memory address.
 */
static int DebugCpu_LoadBin(int nArgc, char *psArgs[])
{
	FILE *fp;
	unsigned char c;
	uint32_t address;
	int i=0;

	if (nArgc < 3)
	{
		return DebugUI_PrintCmdHelp(psArgs[0]);
	}

	if (!Eval_Number(psArgs[2], &address))
	{
		fprintf(stderr, "Invalid address!\n");
		return DEBUGGER_CMDDONE;
	}

	if ((fp = fopen(psArgs[1], "rb")) == NULL)
	{
		fprintf(stderr, "Cannot open file '%s'!\n", psArgs[1]);
		return DEBUGGER_CMDDONE;
	}

	/* TODO: more efficient would be to:
	 * - check file size
	 * - verify that it fits into valid memory area
	 * - flush emulated CPU data cache
	 * - read file contents directly into memory
	 */
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
	uint32_t address;
	uint32_t bytes, i = 0;

	if (nArgc < 4)
	{
		return DebugUI_PrintCmdHelp(psArgs[0]);
	}

	if (!Eval_Number(psArgs[2], &address))
	{
		fprintf(stderr, "  Invalid address!\n");
		return DEBUGGER_CMDDONE;
	}

	if (!Eval_Number(psArgs[3], &bytes))
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
 * Disassemble - arg = starting address, or PC.
 */
int DebugCpu_DisAsm(int nArgc, char *psArgs[])
{
	uint32_t prev_addr, disasm_upper = 0, pc = M68000_GetPC();
	int shown, lines = INT_MAX;
	uaecptr nextpc;

	if (nArgc > 1)
	{
		switch (Eval_Range(psArgs[1], &disasm_addr, &disasm_upper, false))
		{
		case -1:
			/* invalid value(s) */
			return DEBUGGER_CMDDONE;
		case 0:
			/* single value */
			break;
		case 1:
			/* range */
			break;
		}
	}
	else
	{
		/* continue */
		if(!disasm_addr)
			disasm_addr = pc;
	}

	/* limit is topmost address or instruction count */
	if (!disasm_upper)
	{
		disasm_upper = 0xFFFFFFFF;
		lines = DebugUI_GetPageLines(ConfigureParams.Debugger.nDisasmLines, 8);
	}

	/* output a range */
	prev_addr = disasm_addr;
	for (shown = 0; shown < lines && disasm_addr < disasm_upper; shown++)
	{
		const char *symbol;
		if (prev_addr < pc && disasm_addr > pc)
		{
			fputs("ERROR, disassembly misaligned with PC address, correcting\n", debugOutput);
			disasm_addr = pc;
			shown++;
		}
		if (disasm_addr == pc)
		{
			fputs("(PC)\n", debugOutput);
			shown++;
		}
		prev_addr = disasm_addr;
		symbol = Symbols_GetByCpuAddress(disasm_addr, SYMTYPE_ALL);
		if (symbol)
		{
			fprintf(debugOutput, "%s:\n", symbol);
			shown++;
		}
		Disasm(debugOutput, (uaecptr)disasm_addr, &nextpc, 1);
		disasm_addr = nextpc;
	}
	fflush(debugOutput);

	return DEBUGGER_CMDCONT;
}


/**
 * Readline match callback to list register names usable within debugger.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
static char *DebugCpu_MatchRegister(const char *text, int state)
{
	static const char* regs_000[] = {
		"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
		"d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
		"isp", "usp",
		"v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7"
	};
	static const char* regs_020[] = {
		"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
		"caar", "cacr",
		"d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
		"dfc", "isp", "msp", "pc", "sfc", "sr", "usp",
		"v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
		"vbr"
	};
	if (ConfigureParams.System.nCpuLevel < 2)
		return DebugUI_MatchHelper(regs_000, ARRAY_SIZE(regs_000), text, state);
	else
		return DebugUI_MatchHelper(regs_020, ARRAY_SIZE(regs_020), text, state);
}


/**
 * Set address of the named 32-bit register to given argument.
 * Handles V0-7 fake registers, D0-7 data, A0-7 address and several
 * special registers except for PC & SR registers because they need
 * to be accessed using UAE accessors.
 *
 * Return register size in bits or zero for unknown register name.
 */
int DebugCpu_GetRegisterAddress(const char *reg, uint32_t **addr)
{
	char r0;
	int r1;

	if (!reg[0] || !reg[1])
		return 0;

	/* 3-4 letter reg? */
	if (reg[2])
	{
		if (strcasecmp(reg, "ISP") == 0)
		{
			*addr = &regs.isp;
			return 32;
		}
		if (strcasecmp(reg, "USP") == 0)
		{
			*addr = &regs.usp;
			return 32;
		}
		if (ConfigureParams.System.nCpuLevel >= 2)
		{
			static const struct {
				const char name[5];
				uint32_t *addr;
			} reg_020[] = {
				{ "CAAR", &regs.caar },
				{ "CACR", &regs.cacr },
				{ "DFC", &regs.dfc },
				{ "MSP", &regs.msp },
				{ "SFC", &regs.sfc },
				{ "VBR", &regs.vbr }
			};
			for (int i = 0; i < ARRAY_SIZE(reg_020); i++)
			{
				if (strcasecmp(reg, reg_020[i].name) == 0)
				{
					*addr = reg_020[i].addr;
					return 32;
				}
			}
		}
		return 0;
	}

	/* 2-letter reg */
	r0 = toupper((unsigned char)reg[0]);
	r1 = toupper((unsigned char)reg[1]) - '0';

	if (r0 == 'D')  /* Data regs? */
	{
		if (r1 >= 0 && r1 <= 7)
		{
			*addr = &(regs.regs[REG_D0 + r1]);
			return 32;
		}
		fprintf(stderr,"\tBad data register, valid values are 0-7\n");
		return 0;
	}
	if(r0 == 'A')  /* Address regs? */
	{
		if (r1 >= 0 && r1 <= 7)
		{
			*addr = &(regs.regs[REG_A0 + r1]);
			return 32;
		}
		fprintf(stderr,"\tBad address register, valid values are 0-7\n");
		return 0;
	}
	if(r0 == 'V')  /* Virtual regs? */
	{
		if (r1 >= 0 && r1 < ARRAY_SIZE(fake_regs))
		{
			*addr = &fake_regs[r1];
			bFakeRegsUsed = true;
			return 32;
		}
		fprintf(stderr,"\tBad virtual register, valid values are 0-7\n");
		return 0;
	}
	return 0;
}


/**
 * Dump or set CPU registers
 */
int DebugCpu_Register(int nArgc, char *psArgs[])
{
	char *arg, *assign;
	uint32_t value;

	/* If no parameter has been given, simply dump all registers */
	if (nArgc == 1)
	{
		uaecptr nextpc;
		int idx;

		/* use the UAE function instead */
		m68k_dumpstate_file(debugOutput, &nextpc, 0xffffffff);
		fflush(debugOutput);
		if (!bFakeRegsUsed)
			return DEBUGGER_CMDDONE;

		fputs("Virtual registers:\n", debugOutput);
		for (idx = 0; idx < ARRAY_SIZE(fake_regs); idx++)
		{
			if (idx && idx % 4 == 0)
				fputs("\n", debugOutput);
			fprintf(debugOutput, "  V%c %08x",
				'0' + idx, fake_regs[idx]);
		}
		fputs("\n", debugOutput);
		fflush(debugOutput);
		return DEBUGGER_CMDDONE;
	}

	arg = psArgs[1];

	assign = strchr(arg, '=');
	if (!assign)
	{
		goto error_msg;
	}

	*assign++ = '\0';
	if (!Eval_Number(Str_Trim(assign), &value))
	{
		goto error_msg;
	}

	arg = Str_Trim(arg);
	if (strlen(arg) < 2)
	{
		goto error_msg;
	}
	
	/* set SR and update conditional flags for the UAE CPU core. */
	if (strcasecmp("SR", arg) == 0)
	{
		M68000_SetSR(value);
	}
	else if (strcasecmp("PC", arg) == 0)  /* set PC */
	{
		M68000_SetPC(value);
	}
	else
	{
		uint32_t *regaddr;
		/* check&set data and address registers */
		if (DebugCpu_GetRegisterAddress(arg, &regaddr))
		{
			*regaddr = value;
		}
		else
		{
			goto error_msg;
		}
	}
	return DEBUGGER_CMDDONE;

error_msg:
	fprintf(stderr,"\tError, usage: r or r xx=yyyy\n"
		"\tWhere: xx=A0-A7, D0-D7, PC, SR, ISP, USP\n"
		"\t020+: CAAR, CACR, DFC, SFC, MSP, VBR\n"
		"\tor V0-V7 (virtual).\n");
	return DEBUGGER_CMDDONE;
}


/**
 * CPU wrapper for BreakAddr_Command().
 */
static int DebugCpu_BreakAddr(int nArgc, char *psArgs[])
{
	BreakAddr_Command(psArgs[1], false);
	return DEBUGGER_CMDDONE;
}

/**
 * CPU wrapper for BreakCond_Command().
 */
static int DebugCpu_BreakCond(int nArgc, char *psArgs[])
{
	BreakCond_Command(psArgs[1], false);
	return DEBUGGER_CMDDONE;
}

/**
 * CPU wrapper for Profile_Command().
 */
static int DebugCpu_Profile(int nArgc, char *psArgs[])
{
	return Profile_Command(nArgc, psArgs, false);
}


/**
 * Do a memory dump, args = starting address.
 */
int DebugCpu_MemDump(int nArgc, char *psArgs[])
{
	char c, mode;
	int i, arg, size;
	uint32_t value, memdump_upper = 0;

	arg = 1;
	mode = 0;
	size = 1;
	if (nArgc > 1)
		mode = tolower(psArgs[arg][0]);

	if (!mode || isdigit((unsigned char)psArgs[arg][0]) || psArgs[arg][1])
	{
		/* no args, single digit or multiple chars -> default mode */
		mode = 'b';
	}
	else if (mode == 'b')
	{
		arg += 1;
	}
	else if (mode == 'w')
	{
		arg += 1;
		size = 2;
	}
	else if (mode == 'l')
	{
		arg += 1;
		size = 4;
	}
	else
	{
		fprintf(stderr, "Invalid width mode (not b|w|l)!\n");
		return DEBUGGER_CMDDONE;
	}

	if (nArgc > arg)
	{
		switch (Eval_Range(psArgs[arg], &memdump_addr, &memdump_upper, false))
		{
		case -1:
			/* invalid value(s) */
			return DEBUGGER_CMDDONE;
		case 0:
			/* single value */
			break;
		case 1:
			/* range */
			break;
		}
		arg++;

		if (nArgc > arg)
		{
			int count = atoi(psArgs[arg]);
			if (count < 1)
			{
				fprintf(stderr, "Invalid count %d!\n", count);
				return DEBUGGER_CMDDONE;
			}
			memdump_upper = memdump_addr + count * size;
		}
	}

	if (!memdump_upper)
	{
		int lines = DebugUI_GetPageLines(ConfigureParams.Debugger.nMemdumpLines, 8);
		memdump_upper = memdump_addr + MEMDUMP_COLS * lines;
	}

	while (memdump_addr < memdump_upper)
	{
		int cols;
		uint32_t memdump_line = memdump_addr;
		fprintf(debugOutput, "%08X: ", memdump_line);
		
		/* print HEX data */
		cols = MEMDUMP_COLS/size;
		for (i = 0; i < cols && memdump_addr < memdump_upper; i++)
		{
			switch (mode)
			{
			case 'l':
				value = STMemory_ReadLong(memdump_addr);
				break;
			case 'w':
				value = STMemory_ReadWord(memdump_addr);
				break;
			case 'b':
			default:
				value = STMemory_ReadByte(memdump_addr);
				break;
			}
			fprintf(debugOutput, "%0*x ", 2*size, value);
			memdump_addr += size;
		}

		/* print ASCII data */
		fprintf(debugOutput, "  ");

		cols = i*size;
		for (i = 0; i < cols; i++)
		{
			c = STMemory_ReadByte(memdump_line + i);
			if(!isprint((unsigned)c))
				c = NON_PRINT_CHAR;             /* non-printable as dots */
			fprintf(debugOutput,"%c", c);
		}
		fprintf(debugOutput, "\n");
	}
	fflush(debugOutput);

	return DEBUGGER_CMDCONT;
}


/**
 * Command: Write to memory, optional arg for value lengths,
 * followed by starting address and the values.
 */
static int DebugCpu_MemWrite(int nArgc, char *psArgs[])
{
	int i, arg, values, max_values;
	uint32_t write_addr, d;
	union {
		uint8_t  bytes[256];
		uint16_t words[128];
		uint32_t longs[64];
	} store;
	char mode;

	if (nArgc < 3)
	{
		return DebugUI_PrintCmdHelp(psArgs[0]);
	}

	arg = 1;
	mode = tolower(psArgs[arg][0]);
	max_values = ARRAY_SIZE(store.bytes);

	if (!mode || isdigit((unsigned char)psArgs[arg][0]) || psArgs[arg][1])
	{
		/* no args, single digit or multiple chars -> default mode */
		mode = 'b';
	}
	else if (mode == 'b')
	{
		arg += 1;
	}
	else if (mode == 'w')
	{
		max_values = ARRAY_SIZE(store.words);
		arg += 1;
	}
	else if (mode == 'l')
	{
		max_values = ARRAY_SIZE(store.longs);
		arg += 1;
	}
	else
	{
		fprintf(stderr, "Invalid width mode (not b|w|l)!\n");
		return DEBUGGER_CMDDONE;
	}
	/* Read address */
	if (!Eval_Number(psArgs[arg++], &write_addr))
	{
		fprintf(stderr, "Bad address!\n");
		return DEBUGGER_CMDDONE;
	}

	if (nArgc - arg > max_values)
	{
		fprintf(stderr, "Too many values (%d) given for mode '%c' (max %d)!\n",
		       nArgc - arg, mode, max_values);
		return DEBUGGER_CMDDONE;
	}

	/* get the data */
	values = 0;
	for (i = arg; i < nArgc; i++)
	{
		if (!Eval_Number(psArgs[i], &d))
		{
			fprintf(stderr, "Bad value '%s'!\n", psArgs[i]);
			return DEBUGGER_CMDDONE;
		}
		switch(mode)
		{
		case 'b':
			if (d > 0xff)
			{
				fprintf(stderr, "Illegal byte argument: 0x%x!\n", d);
				return DEBUGGER_CMDDONE;
			}
			store.bytes[values] = (uint8_t)d;
			break;
		case 'w':
			if (d > 0xffff)
			{
				fprintf(stderr, "Illegal word argument: 0x%x!\n", d);
				return DEBUGGER_CMDDONE;
			}
			store.words[values] = (uint16_t)d;
			break;
		case 'l':
			store.longs[values] = d;
			break;
		}
		values++;
	}

	/* write the data */
	for (i = 0; i < values; i++)
	{
		switch(mode)
		{
		case 'b':
			STMemory_WriteByte(write_addr + i, store.bytes[i]);
			break;
		case 'w':
			STMemory_WriteWord(write_addr + i*2, store.words[i]);
			break;
		case 'l':
			STMemory_WriteLong(write_addr + i*4, store.longs[i]);
			break;
		}
	}
	if (values > 1)
	{
		fprintf(stderr, "Wrote %d '%c' values starting from 0x%x.\n",
			values, mode, write_addr);
	}
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
 * Command: Single-step CPU
 */
static int DebugCpu_Step(int nArgc, char *psArgv[])
{
	nCpuSteps = 1;
	return DEBUGGER_ENDCONT;
}


/**
 * Readline match callback to list next command opcode types.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
static char *DebugCpu_MatchNext(const char *text, int state)
{
	static const char* ntypes[] = {
		"branch", "exception", "exreturn", "return", "subcall", "subreturn"
	};
	return DebugUI_MatchHelper(ntypes, ARRAY_SIZE(ntypes), text, state);
}

/**
 * Variable + debugger variable function for tracking
 * subroutine call depth for "next" breakpoint
 */
static int CpuCallDepth;
uint32_t DebugCpu_CallDepth(void)
{
	return CpuCallDepth;
}
/* Depth tracking can start anywhere i.e. it can go below initial
 * value.  Start from large enough value that it should never goes
 * negative, as then DebugCpu_CallDepth() return value would wrap
 */
#define CALL_START_DEPTH 10000

/**
 * Command: Step CPU, but proceed through subroutines
 * Does this by temporary conditional breakpoint
 */
static int DebugCpu_Next(int nArgc, char *psArgv[])
{
	char command[80];
	if (nArgc > 1)
	{
		int optype;
		bool depthcheck = false;
		if(strcmp(psArgv[1], "branch") == 0)
			optype = CALL_BRANCH;
		else if(strcmp(psArgv[1], "exception") == 0)
			optype = CALL_EXCEPTION;
		else if(strcmp(psArgv[1], "exreturn") == 0)
			optype = CALL_EXCRETURN;
		else if(strcmp(psArgv[1], "subcall") == 0)
			optype = CALL_SUBROUTINE;
		else if (strcmp(psArgv[1], "subreturn") == 0)
		{
			optype = CALL_SUBRETURN;
			depthcheck = true;
		}
		else if (strcmp(psArgv[1], "return") == 0)
			optype = CALL_SUBRETURN | CALL_EXCRETURN;
		else
		{
			fprintf(stderr, "Unrecognized opcode type given!\n");
			return DEBUGGER_CMDDONE;
		}
		/* CpuOpCodeType increases call depth on subroutine calls,
		 * and decreases depth on return from them, so it must be
		 * first check to get called on every relevant instruction.
		 */
		if (depthcheck)
		{
			CpuCallDepth = CALL_START_DEPTH;
			sprintf(command, "CpuOpcodeType & $%x > 0  &&  CpuCallDepth < $%x  :once :quiet\n",
				optype, CALL_START_DEPTH);
		}
		else
			sprintf(command, "CpuOpcodeType & $%x > 0 :once :quiet\n", optype);
	}
	else
	{
		uint32_t optype, nextpc;

		optype = DebugCpu_OpcodeType();
		/* should this instruction be stepped normally, or is it
		 * - subroutine call
		 * - exception
		 * - loop branch backwards
		 */
		if (optype == CALL_SUBROUTINE ||
		    optype == CALL_EXCEPTION ||
		    (optype == CALL_BRANCH &&
		     (STMemory_ReadWord(M68000_GetPC()) & 0xf0f8) == 0x50c8 &&
		     (int16_t)STMemory_ReadWord(M68000_GetPC() + SIZE_WORD) < 0))
		{
			nextpc = Disasm_GetNextPC(M68000_GetPC());
			sprintf(command, "pc=$%x :once :quiet\n", nextpc);
		}
		else
		{
			nCpuSteps = 1;
			return DEBUGGER_ENDCONT;
		}
	}
	/* use breakpoint, not steps */
	if (BreakCond_Command(command, false))
	{
		nCpuSteps = 0;
		return DEBUGGER_ENDCONT;
	}
	return DEBUGGER_CMDDONE;
}

/* helper to get instruction type */
uint32_t DebugCpu_OpcodeType(void)
{
	/* cannot use OpcodeFamily like profiler does,
	 * as that's for previous instructions
	 */
	uint16_t opcode = STMemory_ReadWord(M68000_GetPC());

	if (opcode == 0x4e74 ||			/* RTD */
	    opcode == 0x4e75 ||			/* RTS */
	    opcode == 0x4e77)			/* RTR */
	{
		CpuCallDepth--;
		return CALL_SUBRETURN;
	}
	if (opcode == 0x4e73)			/* RTE */
	{
		return CALL_EXCRETURN;
	}
	/* NOTE: BSR needs to be matched before BRA/BCC! */
	if ((opcode & 0xff00) == 0x6100 ||	/* BSR */
	    (opcode & 0xffc0) == 0x4e80)	/* JSR */
	{
		CpuCallDepth++;
		return CALL_SUBROUTINE;
	}
	/* TODO: ftrapcc, chk2? */
	if (opcode == 0x4e72 ||			/* STOP */
	    opcode == 0x4afc ||			/* ILLEGAL */
	    opcode == 0x4e76 ||			/* TRAPV */
	    (opcode & 0xfff0) == 0x4e40 ||	/* TRAP */
	    (opcode & 0xf1c0) == 0x4180 ||	/* CHK */
	    (opcode & 0xfff8) == 0x4848)	/* BKPT */
	{
		return CALL_EXCEPTION;
	}
	/* TODO: fbcc, fdbcc */
	if ((opcode & 0xf000) == 0x6000 ||	/* BRA / BCC */
	    (opcode & 0xffc0) == 0x4ec0 ||	/* JMP */
	    (opcode & 0xf0f8) == 0x50c8)	/* DBCC */
		return CALL_BRANCH;

	return CALL_UNKNOWN;
}


/**
 * CPU instructions since continuing emulation
 */
static uint32_t nCpuInstructions;
uint32_t DebugCpu_InstrCount(void)
{
	return nCpuInstructions;
}

/**
 * This function is called after each CPU instruction when debugging is enabled.
 */
void DebugCpu_Check(void)
{
	nCpuInstructions++;
	if (bCpuProfiling)
	{
		Profile_CpuUpdate();
	}
	if (LOG_TRACE_LEVEL((TRACE_CPU_DISASM|TRACE_CPU_SYMBOLS)))
	{
		const char *symbol;
		symbol = Symbols_GetByCpuAddress(M68000_GetPC(), SYMTYPE_ALL);
		if (symbol)
			LOG_TRACE_PRINT("%s\n", symbol);
	}
	if (LOG_TRACE_LEVEL(TRACE_CPU_REGS))
	{
		uaecptr nextpc;
		LOG_TRACE_DIRECT_INIT ();
		m68k_dumpstate_file(TraceFile, &nextpc, 0xffffffff);
	}
	if (nCpuActiveCBs)
	{
		if (BreakCond_MatchCpu())
		{
			DebugUI(REASON_CPU_BREAKPOINT);
			/* make sure we don't decrease step count
			 * below, before even even getting out of here
			 */
			if (nCpuSteps)
				nCpuSteps++;
		}
	}
	if (nCpuSteps)
	{
		nCpuSteps--;
		if (nCpuSteps == 0)
			DebugUI(REASON_CPU_STEPS);
	}
	if (History_TrackCpu())
	{
		History_AddCpu();
	}
	if (ConOutDevices)
	{
		Console_Check();
	}
}

/**
 * Should be called before returning back emulation to tell the CPU core
 * to call us after each instruction if "real-time" debugging like
 * breakpoints has been set.
 */
void DebugCpu_SetDebugging(void)
{
	bCpuProfiling = Profile_CpuStart();
	nCpuActiveCBs = BreakCond_CpuBreakPointCount();

	if (nCpuActiveCBs || nCpuSteps || bCpuProfiling || History_TrackCpu()
	    || LOG_TRACE_LEVEL((TRACE_CPU_DISASM|TRACE_CPU_SYMBOLS|TRACE_CPU_REGS))
	    || ConOutDevices)
	{
		M68000_SetDebugger(true);
		nCpuInstructions = 0;
	}	
	else
		M68000_SetDebugger(false);
}


static const dbgcommand_t cpucommands[] =
{
	{ NULL, NULL, "CPU commands", NULL, NULL, NULL, false },
	/* NULL as match function will complete file names */
	{ DebugCpu_BreakAddr, Symbols_MatchCpuCodeAddress,
	  "address", "a",
	  "set CPU PC address breakpoints",
	  BreakAddr_Description,
	  true	},
	{ DebugCpu_BreakCond, Vars_MatchCpuVariable,
	  "breakpoint", "b",
	  "set/remove/list conditional CPU breakpoints",
	  BreakCond_Description,
	  true },
	{ DebugCpu_DisAsm, Symbols_MatchCpuCodeAddress,
	  "disasm", "d",
	  "disassemble from PC, or given address",
	  "[<start address>[-<end address>]]\n"
	  "\tWhen no address is given, disassemble from the last disasm\n"
	  "\taddress, or from current PC when debugger is (re-)entered.",
	  false },
	{ DebugCpu_Profile, Profile_Match,
	  "profile", "",
	  "profile CPU code",
	  Profile_Description,
	  false },
	{ DebugCpu_Register, DebugCpu_MatchRegister,
	  "cpureg", "r",
	  "dump register values or set register to value",
	  "[REG=value]\n"
	  "\tSet CPU register to given value, or dump all registers\n"
	  "\twhen no parameter is given.",
	  true },
	{ DebugCpu_MemDump, Symbols_MatchCpuDataAddress,
	  "memdump", "m",
	  "dump memory",
	  "[b|w|l] [<start address>[-<end address>| <count>]]\n"
	  "\tdump memory at address or continue dump from previous address.\n"
	  "\tBy default memory output is done as bytes, with 'w' or 'l'\n"
	  "\toption, it will be done as words/longs instead.  Output amount\n"
	  "\tcan be given either as a count or an address range.",
	  false },
	{ DebugCpu_MemWrite, Symbols_MatchCpuAddress,
	  "memwrite", "w",
	  "write bytes/words/longs to memory",
	  "[b|w|l] address value1 [value2 ...]\n"
	  "\tWrite space separate values (in current number base) to given\n"
	  "\tmemory address. By default writes are done as bytes, with\n"
	  "\t'w' or 'l' option they will be done as words/longs instead",
	  false },
	{ DebugCpu_LoadBin, NULL,
	  "loadbin", "l",
	  "load a file into memory",
	  "<filename> <address>\n"
	  "\tLoad the file <filename> into memory starting at <address>.",
	  false },
	{ DebugCpu_SaveBin, NULL,
	  "savebin", "",
	  "save memory to a file",
	  "<filename> <address> <length>\n"
	  "\tSave the memory block at <address> with given <length> to\n"
	  "\tthe file <filename>.",
	  false },
	{ Symbols_Command, Symbols_MatchCommand,
	  "symbols", "",
	  "load CPU symbols & their addresses",
	  Symbols_Description,
	  false },
	{ DebugCpu_Step, NULL,
	  "step", "s",
	  "single-step CPU",
	  "\n"
	  "\tExecute next CPU instruction (like 'c 1', but repeats on Enter).",
	  false },
	{ DebugCpu_Next, DebugCpu_MatchNext,
	  "next", "n",
	  "step CPU through subroutine calls / to given instruction type",
	  "[instruction type]\n"
	  "\tSame as 'step' command if there are no subroutine calls.\n"
          "\tWhen there are, those calls are treated as one instruction.\n"
	  "\tIf argument is given, continues until instruction of given\n"
	  "\ttype is encountered.  Repeats on Enter.",
	  false },
	{ DebugCpu_Continue, NULL,
	  "cont", "c",
	  "continue emulation / CPU single-stepping",
	  "[steps]\n"
	  "\tLeave debugger and continue emulation for <steps> CPU instructions\n"
	  "\tor forever if no steps have been specified.",
	  false }
};


/**
 * Should be called when debugger is first entered to initialize
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
	return ARRAY_SIZE(cpucommands);
}

/**
 * Should be called when debugger is re-entered to reset
 * relevant CPU debugging variables.
 */
void DebugCpu_InitSession(void)
{
#define MAX_CPU_DISASM_OFFSET 16
	disasm_addr = History_DisasmAddr(M68000_GetPC(), MAX_CPU_DISASM_OFFSET, false);
	Profile_CpuStop();
}
