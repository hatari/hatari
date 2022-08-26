/**
 * Disassemble M68k code
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 */

#include "main.h"
#include <ctype.h>

#if HAVE_CAPSTONE_M68K
#include <capstone.h>
#endif

#include "sysdeps.h"
#include "configuration.h"
#include "m68000.h"
#include "newcpu.h"
#include "stMemory.h"
#include "debug.h"
#include "paths.h"
#include "profile.h"
#include "str.h"
#include "tos.h"
#include "68kDisass.h"
#include "disasm.h"

#define ADDRESS_ON_PC		1
#define USE_SYMBOLS			1

typedef enum {
	doptNoSpace = 1,	// no space after a comma in the operands list
	doptOpcodesSmall = 2,	// opcodes are small letters
	doptRegisterSmall = 4,	// register names are small letters
	doptStackSP = 8		// stack pointer is named "SP" instead of "A7" (except for MOVEM)
} Diss68kOptions;

// Note: doptNoBrackets and doptStackSP are not implemented anymore
static Diss68kOptions	options = doptOpcodesSmall | doptRegisterSmall | doptNoSpace;

/* all options */
static const Diss68kOptions optionsMask = doptOpcodesSmall | doptRegisterSmall | doptStackSP | doptNoSpace;

// values <0 will hide the group
static int			optionPosAddress = 0;	// current address
static int			optionPosHexdump = 12;	// 16-bit words at this address
static int			optionPosLabel = 35;	// label, if defined
static int			optionPosOpcode = 47;	// opcode
static int			optionPosOperand = 57;	// operands for the opcode
static int			optionPosComment = 82;	// comment, if defined

static int optionCPUTypeMask;

#if HAVE_CAPSTONE_M68K

static void Disass68k_ConvertA7ToSP(char *sOpBuf)
{
	char *ptr;
	int cnt;

	/* Do this twice, once for source and once for destination */
	for (cnt = 0; cnt < 2 ; cnt++)
	{
		ptr = strstr(sOpBuf, "(a7");
		if (ptr)
			memcpy(ptr + 1, "sp", 2);
	}

	if (strncmp(sOpBuf, "a7,", 3) == 0)
		memcpy(sOpBuf, "sp", 2);

	cnt = strlen(sOpBuf);
	if (cnt > 4 && strncmp(&sOpBuf[cnt - 4], ", a7", 4) == 0)
		memcpy(&sOpBuf[cnt - 2], "sp", 2);
}

static int Disass68k(csh cshandle, long addr, char *labelBuffer,
                     char *opcodeBuffer, char *operandBuffer, char *commentBuffer)
{
	const int maxsize = MAX_68000_INSTRUCTION_SIZE;
	cs_insn *insn;
	void *mem;
	char *ch;
	int size;

	labelBuffer[0] = 0;
	opcodeBuffer[0] = 0;
	operandBuffer[0] = 0;
	commentBuffer[0] = 0;

	if (!STMemory_CheckAreaType(addr, maxsize, ABFLAG_RAM | ABFLAG_ROM))
	{
		strcpy(commentBuffer, "address out of bounds");
		strcpy(opcodeBuffer, "???");
		return 2;
	}
	mem = STMemory_STAddrToPointer(addr);
	if (cs_disasm(cshandle, mem, maxsize, addr, 1, &insn) <= 0)
	{
		strcpy(commentBuffer, "unknown opcode");
		strcpy(opcodeBuffer, "DC.W");
		sprintf(operandBuffer,"$%4.4x", STMemory_ReadWord(addr));
		return 2;
	}

	strcpy(opcodeBuffer, insn->mnemonic);

	/* Instruction mnemonic in uppercase letter? */
	if (!(options & doptOpcodesSmall))
		Str_ToUpper(opcodeBuffer);

	strcpy(operandBuffer, insn->op_str);

	/* Replace "a7" with "sp"? */
	if ((options & doptStackSP) != 0)
		Disass68k_ConvertA7ToSP(operandBuffer);

	/* Operands in uppercase letters? */
	if (!(options & doptRegisterSmall))
		Str_ToUpper(operandBuffer);

	/* Remove spaces after comma? */
	if ((options & doptNoSpace) != 0)
	{
		for (ch = operandBuffer; *ch != 0; ch++)
		{
			if (ch[0] == ',' && ch[1] == ' ')
			{
				++ch;
				memmove(ch, ch + 1, strlen(ch + 1) + 1);
			}
		}
	}

	size = insn->size;
	cs_free(insn, 1);
	return size;
}

static void		Disass68kComposeStr(char *dbuf, const char *str, int position, int maxPos)
{
	int		i;
	int		len = strlen(dbuf);
	while(len < position) {
		dbuf[len++] = ' ';		/* Will give harmless warning from GCC */
	}
	for(i=0; str[i] && (!maxPos || len+i<maxPos); ++i)
		dbuf[len+i] = str[i];
	if(str[i])
		dbuf[len+i-1] = '+';
	dbuf[len+i] = 0;
}

static void Disass68k_loop (FILE *f, uaecptr addr, uaecptr *nextpc, int cnt)
{
	csh cshandle;

	if (cs_open(CS_ARCH_M68K, optionCPUTypeMask, &cshandle) != CS_ERR_OK)
	{
		fprintf(stderr, "Failed to init Capstone library!\n");
		return;
	}

	while (cnt-- > 0) {
		const int	addrWidth = 8;		// 6 on an ST (24 bit addressing), 8 on a TT (32 bit addressing)
		char	lineBuffer[1024];

		char	addressBuffer[32];
		char	hexdumpBuffer[256];
		char	labelBuffer[258];
		char	opcodeBuffer[64];
		char	operandBuffer[256];
		char	commentBuffer[258];
		int	plen, len, j;

		len = Disass68k(cshandle, addr, labelBuffer, opcodeBuffer, operandBuffer, commentBuffer);
		if (!len)
			break;

		sprintf(addressBuffer, "$%*.*x :", addrWidth,addrWidth, addr);

		hexdumpBuffer[0] = 0;
		plen = len;
		if(plen > 80 && (!strncmp(opcodeBuffer, "DC.", 3) || !strncmp(opcodeBuffer, "dc.", 3)))
			plen = ((optionPosLabel - optionPosHexdump) / 5) * 2;

		for(j=0; j<plen; j += 2)
		{
			if(j > 0)
				strcat(hexdumpBuffer, " ");
			if(j + 2 > plen)
			{
				sprintf(hexdumpBuffer+strlen(hexdumpBuffer),
					"%2.2x", STMemory_ReadWord(addr+j) >> 8);
			} else {
				sprintf(hexdumpBuffer+strlen(hexdumpBuffer),
					"%4.4x", STMemory_ReadWord(addr+j));
			}
		}

		lineBuffer[0] = 0;
		if(optionPosAddress >= 0)
			Disass68kComposeStr(lineBuffer, addressBuffer, optionPosAddress, 0);
		if(optionPosHexdump >= 0)
			Disass68kComposeStr(lineBuffer, hexdumpBuffer, optionPosHexdump, optionPosLabel);
		if(optionPosLabel >= 0)
			Disass68kComposeStr(lineBuffer, labelBuffer, optionPosLabel, 0);
		if(optionPosOpcode >= 0)
			Disass68kComposeStr(lineBuffer, opcodeBuffer, optionPosOpcode, 0);
		if(optionPosOperand >= 0)
		{
			size_t	l = strlen(lineBuffer);
			if(lineBuffer[l-1] != ' ')		// force at least one space between opcode and operand
			{
				lineBuffer[l++] = ' ';
				lineBuffer[l] = 0;
			}
			Disass68kComposeStr(lineBuffer, operandBuffer, optionPosOperand, 0);
		}
		if (optionPosComment >= 0)
		{
			if (Profile_CpuAddr_DataStr(commentBuffer, sizeof(commentBuffer), addr))
			{
				Disass68kComposeStr(lineBuffer, commentBuffer, optionPosComment+1, 0);
			}
			/* show comments only if profile data is missing */
			else if (commentBuffer[0])
			{
				Disass68kComposeStr(lineBuffer, " ;", optionPosComment, 0);
				Disass68kComposeStr(lineBuffer, commentBuffer, optionPosComment+3, 0);
			}
		}
		addr += len;
		if (f)
			fprintf(f, "%s\n", lineBuffer);
	}

	cs_close(&cshandle);

	if (nextpc)
		*nextpc = addr;
}

#endif /* HAVE_CAPSTONE_M68K */

/**
 * Calculate next PC address from given one, without output
 * @return	next PC address
 */
Uint32 Disasm_GetNextPC(Uint32 pc)
{
	char buf[256];

	uaecptr nextpc;
	sm68k_disasm(buf, NULL, pc, &nextpc, -1);
	return nextpc;
}

/**
 * Call disassembly using the selected disassembly method,
 * either internal UAE one, or the stand alone disassembler above,
 * whichever is selected in Hatari configuration
 */
void Disasm (FILE *f, uaecptr addr, uaecptr *nextpc, int cnt)
{
#if HAVE_CAPSTONE_M68K
	if (!ConfigureParams.Debugger.bDisasmUAE)
	{
		Disass68k_loop (f, addr, nextpc, cnt);
		return;
	}
#endif

	m68k_disasm_file (f, addr, nextpc, addr, cnt);
}

static void Disasm_CheckOptionEngine(void)
{
	if (ConfigureParams.Debugger.bDisasmUAE)
		fputs("WARNING: disassembly options are supported only for '--disasm ext'!\n", stderr);
}

/**
 * query disassembly output column positions.
 */
void Disasm_GetColumns(int *pos)
{
	pos[DISASM_COLUMN_ADDRESS] = optionPosAddress;
	pos[DISASM_COLUMN_HEXDUMP] = optionPosHexdump;
	pos[DISASM_COLUMN_LABEL]   = optionPosLabel;
	pos[DISASM_COLUMN_OPCODE]  = optionPosOpcode;
	pos[DISASM_COLUMN_OPERAND] = optionPosOperand;
	pos[DISASM_COLUMN_COMMENT] = optionPosComment;
}

/**
 * set disassembly output column positions.
 */
void Disasm_SetColumns(int *pos)
{
	optionPosAddress = pos[DISASM_COLUMN_ADDRESS];
	optionPosHexdump = pos[DISASM_COLUMN_HEXDUMP];
	optionPosLabel   = pos[DISASM_COLUMN_LABEL];
	optionPosOpcode  = pos[DISASM_COLUMN_OPCODE];
	optionPosOperand = pos[DISASM_COLUMN_OPERAND];
	optionPosComment = pos[DISASM_COLUMN_COMMENT];
}

/**
 * function to disable given disassembly output 'column'.
 * input is current column positions in 'oldcols' array and
 * output is new column positions/values in 'newcols' array.
 * It's safe to use same array for both.
 */
void Disasm_DisableColumn(int column, int *oldcols, int *newcols)
{
	int i, diff = 0;

	assert(column >= 0 && column < DISASM_COLUMNS);
	if (column+1 < DISASM_COLUMNS)
		diff = oldcols[column+1] - oldcols[column];

	for (i = 0; i < DISASM_COLUMNS; i++)
	{
		if (i && oldcols[i-1] > oldcols[i])
		{
			printf("WARNING: disassembly columns aren't in the expected order!\n");
			return;
		}
		if (i < column)
			newcols[i] = oldcols[i];
		else if (i > column)
			newcols[i] = oldcols[i] - diff;
		else
			newcols[column] = DISASM_COLUMN_DISABLE;
	}
}

/**
 * Get current disassembly output option flags
 * @return	current output flags
 */
int Disasm_GetOptions(void)
{
	return options;
}

/**
 * Set CPU and FPU mask used for disassembly (when changed from the UI or the options)
 */
void Disasm_SetCPUType(int CPU, int FPU, bool bMMU)
{
	switch (CPU)
	{
#ifdef HAVE_CAPSTONE_M68K
	 case 0:  optionCPUTypeMask = CS_MODE_M68K_000; break;
	 case 1:  optionCPUTypeMask = CS_MODE_M68K_010; break;
	 case 2:  optionCPUTypeMask = CS_MODE_M68K_020; break;
	 case 3:  optionCPUTypeMask = CS_MODE_M68K_030; break;
	 case 4:  optionCPUTypeMask = CS_MODE_M68K_040; break;
	 case 5:  optionCPUTypeMask = CS_MODE_M68K_060; break; /* special case: 5 => 060 */
#endif
	 default: optionCPUTypeMask = 0; break;
	}
}

/**
 * Parse disasm command line option argument
 * @return	error string (""=silent 'error') or NULL for success.
 */
const char *Disasm_ParseOption(const char *arg)
{
	if (strcasecmp(arg, "help") == 0)
	{
		const struct {
			int flag;
			const char *desc;
		} option[] = {
			{ doptNoSpace, "no space after comma in the operands list" },
			{ doptOpcodesSmall, "opcodes in small letters" },
			{ doptRegisterSmall, "register names in small letters" },
			{ doptStackSP, "stack pointer as 'SP', not 'A7'" },
			{ 0, NULL }
		};
		int i;
		fputs("Disassembly settings:\n"
		      "\tuae - use CPU core internal disassembler which has better\n"
		      "\t      instruction support\n"
		      "\text - use external disassembler which has nicer output\n"
		      "\t      and supports options below\n"
		      "\t<bitmask> - disassembly output option flags\n"
		      "Flag values:\n", stderr);
		for (i = 0; option[i].desc; i++) {
			assert(option[i].flag == (1 << i));
			fprintf(stderr, "\t%d: %s\n", option[i].flag, option[i].desc);
		}
		fprintf(stderr, "Current settings are:\n\t--disasm %s --disasm 0x%x\n",
			ConfigureParams.Debugger.bDisasmUAE ? "uae" : "ext",
			ConfigureParams.Debugger.nDisasmOptions);
		return "";
	}	
	if (strcasecmp(arg, "uae") == 0)
	{
		fputs("Selected UAE CPU core internal disassembler.\n", stderr);
		ConfigureParams.Debugger.bDisasmUAE = true;
		disasm_init();
		return NULL;
	}
	if (strcasecmp(arg, "ext") == 0)
	{
#if HAVE_CAPSTONE_M68K
		fputs("Selected external disassembler.\n", stderr);
		fprintf(stderr, "Disassembly output flags are %d.\n", options);
		ConfigureParams.Debugger.bDisasmUAE = false;
		return NULL;
#else
		return "external disassembler (capstone) not compiled into this binary";
#endif
	}
	if (isdigit((unsigned char)*arg))
	{
		char *end;
		int newopt = strtol(arg, &end, 0);
		if (*end)
		{
			return "not a number";
		}
		if ((newopt|optionsMask) != optionsMask)
		{
			return "unknown flags in the bitmask";
		}
		fprintf(stderr, "Changed CPU disassembly output flags from %d to %d.\n", options, newopt);
		ConfigureParams.Debugger.nDisasmOptions = options = newopt;
		Disasm_CheckOptionEngine();
		return NULL;
	}
	return "invalid disasm option";
}
