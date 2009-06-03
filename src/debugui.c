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
#include "file.h"
#include "reset.h"
#include "m68000.h"
#include "str.h"
#include "stMemory.h"
#include "sound.h"
#include "tos.h"
#include "debugui.h"

#include "hatari-glue.h"

#define MEMDUMP_COLS   16      /* memdump, number of bytes per row */
#define MEMDUMP_ROWS   4       /* memdump, number of rows */
#define NON_PRINT_CHAR '.'     /* character to display for non-printables */
#define DISASM_INSTS   5       /* disasm - number of instructions */

static unsigned long memdump_addr; /* memdump address */
static unsigned long disasm_addr;  /* disasm address */

static FILE *debugOutput;



/*-----------------------------------------------------------------------*/
/**
 * Get a hex adress range, eg. "fa0000-fa0100" 
 * returns -1 if not a range,
 * -2 if a range, but not a valid one.
 * 0 if OK.
 */
static int getRange(char *str, unsigned long *lower, unsigned long *upper)
{
	bool fDash = FALSE;
	int i=0;

	while (str[i] != '\0')
	{
		if (str[i] == '-')
		{
			str[i] = ' ';
			fDash = TRUE;
		}
		i++;
	}
	if (fDash == FALSE)
		return -1;

	i = sscanf(str, "%lx%lx", lower, upper);
	if (i != 2)
		return -2;
	if (*lower > *upper)
		return -3;
	return 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Parse a hex adress range, eg. "fa0000[-fa0100]" + show appropriate warnings
 * returns:
 * -1 if invalid address or range,
 *  0 if single address,
 * +1 if a range.
 */
static int parseRange(char *str, unsigned long *lower, unsigned long *upper)
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
		i = sscanf(str, "%lx", lower);
		
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
		fprintf(stderr,"Invalid range (%lx > %lx)!\n", *lower, *upper);
		return -1;
	}
	fprintf(stderr, "Unknown getRange() return value!\n");
	return -1;
}


/*-----------------------------------------------------------------------*/
/**
 * Open given log file.
 */
static void DebugUI_SetLogFile(const char *logpath)
{
	File_Close(debugOutput);
	debugOutput = File_Open(logpath, "w");
	if (debugOutput)
		fprintf(stderr, "Debug log '%s' opened\n", logpath);
	else
		debugOutput = stderr;
}


/*-----------------------------------------------------------------------*/
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


/*-----------------------------------------------------------------------*/
/**
 * Load a binary file to a memory address.
 */
static void DebugUI_LoadBin(char *args)
{
	FILE *fp;
	unsigned char c;
	char dummy[100];
	char filename[200];
	unsigned long address;
	int i=0;

	if (sscanf(args, "%s%s%lx", dummy, filename, &address) != 3)
	{
		fprintf(stderr, "Invalid arguments!\n");
		return;
	}
	address &= 0x00FFFFFF;
	if ((fp = fopen(filename, "rb")) == NULL)
	{
		fprintf(stderr,"Cannot open file!\n");
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
}


/*-----------------------------------------------------------------------*/
/**
 * Dump memory from an address to a binary file.
 */
static void DebugUI_SaveBin(char *args)
{
	FILE *fp;
	unsigned char c;
	char filename[200];
	char dummy[100];
	unsigned long address;
	unsigned long bytes, i=0;

	if (sscanf(args, "%s%s%lx%lx", dummy, filename, &address, &bytes) != 4)
	{
		fprintf(stderr, "  Invalid arguments!\n");
		return;
	}
	address &= 0x00FFFFFF;
	if ((fp = fopen(filename, "wb")) == NULL)
	{
		fprintf(stderr,"  Cannot open file!\n");
	}

	while (i < bytes)
	{
		c = STMemory_ReadByte(address++);
		fputc(c, fp);
		i++;
	}
	fclose(fp);
	fprintf(stderr, "  Wrote 0x%lx bytes.\n", bytes);
}


#if ENABLE_DSP_EMU

#include "dsp.h"

static Uint16 dsp_disasm_addr;  /* DSP disasm address */
static Uint16 dsp_memdump_addr; /* DSP memdump address */
static char dsp_mem_space;      /* X, Y, P */

/*-----------------------------------------------------------------------*/
/**
 * Do a DSP register dump.
 */
static void DebugUI_DspRegDump(void)
{
	if (!bDspEnabled)
	{
		printf("DSP isn't present or initialized.\n");
		return;
	}

	DSP_DisasmRegisters();
}

/*-----------------------------------------------------------------------*/
/**
 * Set a DSP register: 
 */
static void DebugUI_DspRegSet(char *arg)
{
	int i;
	char reg[4], *assign;
	long value;

	if (!bDspEnabled)
	{
		printf("DSP isn't present or initialized.\n");
		return;
	}

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
	return;

	error_msg:
	fprintf(stderr,"\tError, usage: dr or dr xx=yyyy\n"
		"\tWhere: xx=A0-A2, B0-B2, X0, X1, Y0, Y1, R0-R7,\n"
		"\t       N0-N7, M0-M7, LA, LC, PC, SR, SP, OMR, SSH, SSL\n"
		"\tand yyyy is a hex value.\n");
}

/*-----------------------------------------------------------------------*/
/**
 * DSP dissassemble - arg = starting address/range, or PC.
 */
static void DebugUI_DspDisAsm(char *arg, bool cont)
{
	unsigned long lower, upper;
	Uint16 dsp_disasm_upper = 0;

	if (!bDspEnabled)
	{
		printf("DSP isn't present or initialized.\n");
		return;
	}

	if (cont != TRUE)
	{
		switch (parseRange(arg, &lower, &upper))
		{
			case -1:
				/* invalid value(s) */
				return;
			case 0:
				/* single value */
				break;
			case 1:
				/* range */
				if (upper > 0xFFFF)
				{
					fprintf(stderr,"Invalid address '%lx'!\n", upper);
					return;
				}
				dsp_disasm_upper = upper;
				break;
		}

		if (lower > 0xFFFF)
		{
			fprintf(stderr,"Invalid address '%lx'!\n", lower);
			return;
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
}

/*-----------------------------------------------------------------------*/
/**
 * Do a DSP memory dump, args = starting address or range.
 * <x|y|p><address>: dump from X, Y or P, starting from given address,
 * e.g. "x200" or "p200-300"
 */
static void DebugUI_DspMemDump(char *arg, bool cont)
{
	unsigned long lower, upper;
	Uint16 dsp_memdump_upper = 0;
	char space;

	if (!bDspEnabled)
	{
		printf("DSP isn't present or initialized.\n");
		return;
	}

	if (cont != TRUE)
	{
		space = toupper(*arg++);
		switch (space)
		{
		case 'X':
		case 'Y':
		case 'P':
			break;
		default:
			fprintf(stderr,"Invalid DSP address space '%c'!\n", space);
			return;
		}
		switch (parseRange(arg, &lower, &upper))
		{
		case -1:
			/* invalid value(s) */
			return;
		case 0:
			/* single value */
			break;
		case 1:
			/* range */
			if (upper > 0xFFFF)
			{
				fprintf(stderr,"Invalid address '%lx'!\n", upper);
				return;
			}
			dsp_memdump_upper = upper;
			break;
		}
		if (lower > 0xFFFF)
		{
			fprintf(stderr,"Invalid address '%lx'!\n", lower);
			return;
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
}

#endif /* ENABLE_DSP_EMU */


/*-----------------------------------------------------------------------*/
/**
 * Do a register dump.
 */
static void DebugUI_RegDump(void)
{
	uaecptr nextpc;
	/* use the UAE function instead */
	m68k_dumpstate(debugOutput, &nextpc);
	fflush(debugOutput);
}


/*-----------------------------------------------------------------------*/
/**
 * Dissassemble - arg = starting address, or PC.
 */
static void DebugUI_DisAsm(char *arg, bool cont)
{
	unsigned long disasm_upper = 0;
	uaecptr nextpc;

	if (cont != TRUE)
	{
		switch (parseRange(arg, &disasm_addr, &disasm_upper))
		{
		case -1:
			/* invalid value(s) */
			return;
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
		return;
	}

	/* output a range */
	while (disasm_addr < disasm_upper)
	{
		m68k_disasm(debugOutput, (uaecptr)disasm_addr, &nextpc, 1);
		disasm_addr = nextpc;
	}
	fflush(debugOutput);
}


/*-----------------------------------------------------------------------*/
/**
 * Set a register: 
 */
static void DebugUI_RegSet(char *arg)
{
	int i;
	char reg[3], *assign;
	long value;

	assign = strchr(arg, '=');
	/* has '=' and reg name is max. 2 letters that fit to string */
	if (!assign || assign - arg > 2+1)
	{
		fprintf(stderr,"\tError, usage: r or r xx=yyyy\n\tWhere: xx=A0-A7, D0-D7, PC or SR and yyyy is a hex value.\n");
		return;
	}

	*assign = ' ';
	if (sscanf(arg, "%s%lx", reg, &value) != 2)
	{
		fprintf(stderr,"\tError, usage: r or r xx=yyyy\n\tWhere: xx=A0-A7, D0-D7, PC or SR and yyyy is a hex value.\n");
		return;
	}

	for (i = 0; i < 2; i++)
		reg[i] = toupper(reg[i]);

	/* set SR and update conditional flags for the UAE CPU core. */
	if (reg[0] == 'S' && reg[1] == 'R')
	{
		M68000_SetSR(value);
	}
	else if (reg[0] == 'P' && reg[1] == 'C')   /* set PC? */
	{
		M68000_SetPC(value);
	}
	else if (reg[0] == 'D')  /* Data regs? */
	{
		switch (reg[1])
		{
		 case '0':
			Regs[REG_D0] = value;
			break;
		 case '1':
			Regs[REG_D1] = value;
			break;
		 case '2':
			Regs[REG_D2] = value;
			break;
		 case '3':
			Regs[REG_D3] = value;
			break;
		 case '4':
			Regs[REG_D4] = value;
			break;
		 case '5':
			Regs[REG_D5] = value;
			break;
		 case '6':
			Regs[REG_D6] = value;
			break;
		 case '7':
			Regs[REG_D7] = value;
			break;

		 default:
			fprintf(stderr,"\tBad data register, valid values are 0-7\n");
			break;
		}
	}
	else if(reg[0] == 'A')  /* Address regs? */
	{
		switch( reg[1] )
		{
		 case '0':
			Regs[REG_A0] = value;
			break;
		 case '1':
			Regs[REG_A1] = value;
			break;
		 case '2':
			Regs[REG_A2] = value;
			break;
		 case '3':
			Regs[REG_A3] = value;
			break;
		 case '4':
			Regs[REG_A4] = value;
			break;
		 case '5':
			Regs[REG_A5] = value;
			break;
		 case '6':
			Regs[REG_A6] = value;
			break;
		 case '7':
			Regs[REG_A7] = value;
			break;

		 default:
			fprintf(stderr,"\tBad address register, valid values are 0-7\n");
			break;
		}
	}
	else
	{
		fprintf(stderr, "\t Bad register!\n");
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Do a memory dump, args = starting address.
 */
static void DebugUI_MemDump(char *arg, bool cont)
{
	int i,j;
	char c;
	unsigned long memdump_upper = 0;

	if (cont != TRUE)
	{
		switch (parseRange(arg, &memdump_addr, &memdump_upper))
		{
		case -1:
			/* invalid value(s) */
			return;
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
			fprintf(debugOutput, "%6.6lX: ", memdump_addr); /* print address */
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
		return;
	} /* not a range */

	while (memdump_addr < memdump_upper)
	{
		fprintf(debugOutput, "%6.6lX: ", memdump_addr); /* print address */
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
} /* end of memdump */


/*-----------------------------------------------------------------------*/
/**
 * Do a memory write, arg = starting address, followed by bytes.
 */
static void DebugUI_MemWrite(char *arg)
{
	int i, j, numBytes;
	long write_addr;
	unsigned char bytes[300]; /* store bytes */
	char temp[15];
	int d;

	numBytes = 0;
	i = 0;

	Str_Trunc(arg);
	while (arg[i] == ' ')
		i++; /* skip spaces */
	while (arg[i] != ' ')
		i++; /* skip command */
	while (arg[i] == ' ')
		i++; /* skip spaces */

	j = 0;
	while (isxdigit((unsigned)arg[i]) && j < 14) /* get address */
		temp[j++] = arg[i++];
	temp[j] = '\0';
	j = sscanf(temp, "%lx", &write_addr);

	/* if next char is not valid, or it's not a valid address */
	if ((arg[i] != '\0' && arg[i] != ' ') || (j == 0))
	{
		fprintf(stderr, "Bad address!\n");
		return;
	}

	write_addr &= 0x00FFFFFF;

	while (arg[i] == ' ')
		i++; /* skip spaces */

	/* get bytes data */
	while (arg[i] != '\0')
	{
		j = 0;
		while(isxdigit((unsigned)arg[i]) && j < 14) /* get byte */
			temp[j++] = arg[i++];
		temp[j] = '\0';

		/* if next char is not a null or a space - it's not valid. */
		if (arg[i] != '\0' && arg[i] != ' ')
		{
			fprintf(stderr, "Bad byte argument: %c\n", arg[i]);
			return;
		}

		if (temp[0] != '\0')
		{
			if (sscanf(temp,"%x", &d) != 1)
			{
				fprintf(stderr, "Bad byte argument!\n");
				return;
			}
		}

		bytes[numBytes] = (d&0x0FF);
		numBytes++;
		while (arg[i] == ' ')
			i++; /* skip any spaces */
	}

	/* write the data */
	for (i = 0; i < numBytes; i++)
		STMemory_WriteByte(write_addr + i, bytes[i]);
}


/*-----------------------------------------------------------------------*/
/**
 * Print help.
 */
static void DebugUI_Help(void)
{
	fprintf(stderr, "---- debug mode commands ----\n"
#if ENABLE_DSP_EMU
	        " dd [address] - disassemble DSP from PC, or given address. \n"
	        " dm <x|y|p>[address]- dump DSP memory at address, \n\tdm alone continues from previous address.\n"
	        " dr [REG=value] - dump DSP register values/ set register to value \n"
#endif
	        " d [address] - disassemble from PC, or given address. \n"
	        " r [REG=value] - dump register values/ set register to value \n"
	        " m [address] - dump memory at address, \n\tm alone continues from previous address.\n"
	        " w address bytes - write bytes to a memory address, bytes are space separated. \n"
	        " f [filename] - open log file, no argument closes the log file\n"
	        "   Output of reg & mem dumps and disassembly will be written to the log\n"
	        " l filename address - load a file into memory starting at address. \n"
	        " s filename address length - dump length bytes from memory to a file. \n"
	        " o [command line] - set Hatari command line options\n\n"
	        " q - quit emulator\n"
	        " c - continue emulation\n\n"
	        " Adresses may be given as a range e.g. fc0000-fc0100\nAll values in hexadecimal.\n"
	        "-----------------------------\n"
	        "\n");
}


/*-----------------------------------------------------------------------*/
/**
 * Parse and return debug command.
 */
int DebugUI_ParseCommand(char *input)
{
	char command[255], arg[255];
	static char lastcommand[2] = {0,0};
	int i, retval;
	bool noArgs;

	/* Used for 'm', 'd' 'dm' and 'dd' to continue at last pos */
	command[0] = lastcommand[0];
	command[1] = lastcommand[1];
	command[2] = 0;
	arg[0] = 0;
	i = sscanf(input, "%s%s", command, arg);
	Str_ToLower(command);

	if (i == 0)
	{
		fprintf(stderr, "  Unknown command.\n");
		return DEBUG_CMD;
	}

	lastcommand[0] = lastcommand[1] = 0;
	retval = DEBUG_CMD;                /* Default return value */
	noArgs = (i < 2);

	switch (command[0])
	{
	 case 'c':
		retval = DEBUG_QUIT;
		break;

	 case 'q':
		bQuitProgram = TRUE;
		M68000_SetSpecial(SPCFLAG_BRK);   /* Assure that CPU core shuts down */
		retval = DEBUG_QUIT;
		break;

	 case 'h':
	 case '?':
		DebugUI_Help(); /* get help */
		break;

	 case 'o':
		Change_ApplyCommandline(input+1);
		break;

	 case 'd':
		switch (command[1])
		{
#if ENABLE_DSP_EMU
			/* DSP debugging commands? */
		case 'd':
			/* No arg - disassemble at PC, otherwise at given address */
			DebugUI_DspDisAsm(arg, noArgs);
			break;
		case 'm':
			/* No arg - continue memdump, otherwise new memdump */
			DebugUI_DspMemDump(arg, noArgs);
			break;
		case 'r':
			if (noArgs)
				DebugUI_DspRegDump();  /* no arg - dump regs */
			else
				DebugUI_DspRegSet(arg);
			break;
#endif
		default:
			/* No arg - disassemble at PC, otherwise at given address */
			DebugUI_DisAsm(arg, noArgs);
		}
		lastcommand[0] = command[0];
		lastcommand[1] = command[1];
		break;

	 case 'm':
		/* No arg - continue memdump, otherwise new memdump */
		DebugUI_MemDump(arg, noArgs);
		lastcommand[0] = 'm';
		break;

	 case 'f':
		if (noArgs)
			DebugUI_SetLogDefault();
		else
			DebugUI_SetLogFile(arg);
		break;

	 case 'w':
		if (noArgs)    /* not enough args? */
			fprintf(stderr, "  Usage: w address bytes\n");
		else
			DebugUI_MemWrite(input);
		break;

	 case 'r':
		if (noArgs)
			DebugUI_RegDump();  /* no arg - dump regs */
		else
			DebugUI_RegSet(arg);
		break;

	 case 'l':
		if (noArgs)    /* not enough args? */
			fprintf(stderr,"  Usage: l filename address\n");
		else
			DebugUI_LoadBin(input);
		break;

	 case 's':
		if (noArgs)    /* not enough args? */
			fprintf(stderr,"  Usage: s filename address bytes\n");
		else
			DebugUI_SaveBin(input);
		break;

	 default:
		if (command[0])
			fprintf(stderr,"  Unknown command: '%s'\n", command);
		break;
	}

	return retval;
}

/*-----------------------------------------------------------------------*/
/**
 * Get a UI command, parse and return it.
 */
static int DebugUI_GetCommand(void)
{
	char *input;
	int retval;

#if HAVE_LIBREADLINE
	input = readline("> ");
	if (!input)
		return DEBUG_QUIT;
	if (input[0] != 0)
		add_history(input);
#else
	fprintf(stderr, "> ");
	input = malloc(256);
	if (!input)
		return DEBUG_QUIT;
	input[0] = '\0';
	if (fgets(input, 256, stdin) == NULL)
	{
		free(input);
		return DEBUG_QUIT;
	}
#endif
	retval = DebugUI_ParseCommand(input);

	free(input);
	return retval;
}


/*-----------------------------------------------------------------------*/
/**
 * Debug UI
 */
void DebugUI(void)
{
	if (!debugOutput) {
		DebugUI_SetLogDefault();
	}

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

	/* If you want registers, disassembly or memdump to be output
	 * whenever you invoke the debugger, enable suitable lines
	 * from below.
	 */
#if 0
#if ENABLE_DSP_EMU
	if (bDspEnabled)
	{
		DebugUI_DspRegDump();
		DebugUI_DspDisAsm(NULL, TRUE);
		DebugUI_DspMemDump(NULL, TRUE);
	}
#endif
	DebugUI_RegDump();
	DebugUI_DisAsm(NULL, TRUE);
	DebugUI_MemDump(NULL, TRUE);
#endif

	fprintf(stderr, "\nYou have entered debug mode. Type c to continue emulation, h for help."
	                "\n----------------------------------------------------------------------\n");
	while (DebugUI_GetCommand() != DEBUG_QUIT)
		;
	fprintf(stderr,"Returning to emulation...\n------------------------------\n\n");
	DebugUI_SetLogDefault();
}
