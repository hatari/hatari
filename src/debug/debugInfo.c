/*
  Hatari - debuginfo.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  debuginfo.c - functions needed to show info about the atari HW & OS
   components and "lock" that info to be shown on entering the debugger.
*/
const char DebugInfo_fileid[] = "Hatari debuginfo.c : " __DATE__ " " __TIME__;

#include <stdio.h>
#include <assert.h>
#include <ctype.h>

#include "main.h"
#include "bios.h"
#include "blitter.h"
#include "configuration.h"
#include "crossbar.h"
#include "debugInfo.h"
#include "debugcpu.h"
#include "debugdsp.h"
#include "debugui.h"
#include "debug_priv.h"
#include "dsp.h"
#include "evaluate.h"
#include "file.h"
#include "gemdos.h"
#include "history.h"
#include "ioMem.h"
#include "m68000.h"
#include "psg.h"
#include "stMemory.h"
#include "tos.h"
#include "screen.h"
#include "vdi.h"
#include "video.h"
#include "videl.h"
#include "xbios.h"
#include "newcpu.h"
#include "68kDisass.h"

/* ------------------------------------------------------------------
 * TOS information
 */
#define OS_SYSBASE 0x4F2
#define OS_HEADER_SIZE 0x30

#define OS_PHYSTOP 0x42E
#define COOKIE_JAR 0x5A0

#define BASEPAGE_SIZE 0x100

#define GEM_MAGIC 0x87654321
#define GEM_MUPB_SIZE 0xC

#define RESET_MAGIC 0x31415926
#define RESET_VALID 0x426
#define RESET_VECTOR 0x42A

#define COUNTRY_SPAIN 4


/**
 * DebugInfo_GetSysbase: get and validate system base
 * If warnings is set, output warnings if no valid system base
 * return on success sysbase address (+ set rombase), on failure return zero
 */
static Uint32 DebugInfo_GetSysbase(Uint32 *rombase, bool warnings)
{
	Uint32 sysbase = STMemory_ReadLong(OS_SYSBASE);

	if ( !STMemory_CheckAreaType (sysbase, OS_HEADER_SIZE, ABFLAG_RAM | ABFLAG_ROM ) ) {
		if (warnings) {
			fprintf(stderr, "Invalid TOS sysbase RAM address (0x%x)!\n", sysbase);
		}
		return 0;
	}
	/* under TOS, sysbase = os_beg = TosAddress, but not under MiNT -> use os_beg */
	*rombase = STMemory_ReadLong(sysbase+0x08);
	if ( !STMemory_CheckAreaType (*rombase, OS_HEADER_SIZE, ABFLAG_RAM | ABFLAG_ROM ) ) {
		if (warnings) {
			fprintf(stderr, "Invalid TOS sysbase ROM address (0x%x)!\n", *rombase);
		}
		return 0;
	}
	if (*rombase != TosAddress) {
		if (warnings) {
			fprintf(stderr, "os_beg (0x%x) != TOS address (0x%x), header in RAM not set up yet?\n",
				*rombase, TosAddress);
		}
		return 0;
	}
	return sysbase;
}

/**
 * DebugInfo_CurrentBasepage: get and validate currently running program basepage.
 * if given sysbase is zero, use system sysbase.
 * If warnings is set, output warnings if no valid basepage
 * return on success basepage address, on failure return zero
 */
static Uint32 DebugInfo_CurrentBasepage(Uint32 sysbase, bool warnings)
{
	Uint32 basepage;
	Uint16 osversion, osconf;

	if (!sysbase) {
		Uint32 rombase;
		sysbase = DebugInfo_GetSysbase(&rombase, warnings);
		if (!sysbase) {
			return 0;
		}
	}
	osversion = STMemory_ReadWord(sysbase+0x02);
	if (osversion >= 0x0102) {
		basepage = STMemory_ReadLong(sysbase+0x28);
	} else {
		osconf = STMemory_ReadWord(sysbase+0x1C);
		if((osconf>>1) == COUNTRY_SPAIN) {
			basepage = 0x873C;
		} else {
			basepage = 0x602C;
		}
	}
	if ( STMemory_CheckAreaType ( basepage, 4, ABFLAG_RAM ) ) {
		return STMemory_ReadLong(basepage);
	}
	if (warnings) {
		fprintf(stderr, "Pointer 0x%06x to basepage address is invalid!\n", basepage);
	}
	return 0;
}


/**
 * GetBasepageValue: return basepage value at given offset in
 * TOS process basepage or zero if that is missing/invalid.
 */
static Uint32 GetBasepageValue(unsigned offset)
{
	Uint32 basepage = DebugInfo_CurrentBasepage(0, false);
	if (!basepage) {
		return 0;
	}
	if ( !STMemory_CheckAreaType ( basepage, BASEPAGE_SIZE, ABFLAG_RAM ) ||
	    STMemory_ReadLong(basepage) != basepage) {
		return 0;
	}
	return STMemory_ReadLong(basepage+offset);
}

/**
 * DebugInfo_GetTEXT: return current program TEXT segment address
 * or zero if basepage missing/invalid.  For virtual debugger variable.
 */
Uint32 DebugInfo_GetTEXT(void)
{
	return GetBasepageValue(0x08);
}
/**
 * DebugInfo_GetTEXTEnd: return current program TEXT segment end address
 * or zero if basepage missing/invalid.  For virtual debugger variable.
 */
Uint32 DebugInfo_GetTEXTEnd(void)
{
	Uint32 addr = GetBasepageValue(0x08);
	if (addr) {
		return addr + GetBasepageValue(0x0C) - 1;
	}
	return 0;
}
/**
 * DebugInfo_GetDATA: return current program DATA segment address
 * or zero if basepage missing/invalid.  For virtual debugger variable.
 */
Uint32 DebugInfo_GetDATA(void)
{
	return GetBasepageValue(0x010);
}
/**
 * DebugInfo_GetBSS: return current program BSS segment address
 * or zero if basepage missing/invalid.  For virtual debugger variable.
 */
Uint32 DebugInfo_GetBSS(void)
{
	return GetBasepageValue(0x18);
}
/**
 * DebugInfo_GetBASEPAGE: return current basepage address.
 */
Uint32 DebugInfo_GetBASEPAGE(void)
{
	return DebugInfo_CurrentBasepage(0, false);
}


/**
 * output nil-terminated string from any Atari memory type
 */
static Uint32 print_mem_str(Uint32 addr, Uint32 end)
{
	Uint8 chr;
	while (addr < end && (chr = STMemory_ReadByte(addr++))) {
		fputc(chr, stderr);
	}
	return addr;
}

/**
 * DebugInfo_Basepage: show TOS process basepage information
 * at given address.
 */
static void DebugInfo_Basepage(FILE *fp, Uint32 basepage)
{
	Uint8 cmdlen;
	Uint32 addr;

	if (!basepage) {
		/* default to current process basepage */
		basepage = DebugInfo_CurrentBasepage(0, true);
		if (!basepage) {
			return;
		}
	}
	fprintf(fp, "Process basepage (0x%x) information:\n", basepage);
	if ( !STMemory_CheckAreaType ( basepage, BASEPAGE_SIZE, ABFLAG_RAM ) ||
	    STMemory_ReadLong(basepage) != basepage) {
		fprintf(fp, "- address 0x%06x is invalid!\n", basepage);
		return;
	}
	fprintf(fp, "- TPA start      : 0x%06x\n", STMemory_ReadLong(basepage));
	fprintf(fp, "- TPA end +1     : 0x%06x\n", STMemory_ReadLong(basepage+0x04));
	fprintf(fp, "- Text segment   : 0x%06x\n", STMemory_ReadLong(basepage+0x08));
	fprintf(fp, "- Text size      : 0x%x\n",   STMemory_ReadLong(basepage+0x0C));
	fprintf(fp, "- Data segment   : 0x%06x\n", STMemory_ReadLong(basepage+0x10));
	fprintf(fp, "- Data size      : 0x%x\n",   STMemory_ReadLong(basepage+0x14));
	fprintf(fp, "- BSS segment    : 0x%06x\n", STMemory_ReadLong(basepage+0x18));
	fprintf(fp, "- BSS size       : 0x%x\n",   STMemory_ReadLong(basepage+0x1C));
	fprintf(fp, "- Process DTA    : 0x%06x\n", STMemory_ReadLong(basepage+0x20));
	fprintf(fp, "- Parent basepage: 0x%06x\n", STMemory_ReadLong(basepage+0x24));

	addr = STMemory_ReadLong(basepage+0x2C);
	fprintf(fp, "- Environment    : 0x%06x\n", addr);
	if ( STMemory_CheckAreaType ( addr, 4096, ABFLAG_RAM ) ) {
		Uint32 end = addr + 4096;
		while (addr < end && STMemory_ReadByte(addr)) {
			fprintf(fp, "  '");
			addr = print_mem_str(addr, end);
			addr = print_mem_str(addr, end);
			fprintf(fp, "'\n");
		}
	}
	addr = basepage+0x80;
	cmdlen = STMemory_ReadByte(addr++);
	fprintf(fp, "- Command argslen: %d (at 0x%06x)\n", cmdlen, addr);
	if (cmdlen) {
		Uint32 end = addr + cmdlen;
		fprintf(fp, "  '");
		for (;;) {
			addr = print_mem_str(addr, end);
			if (addr >= end) {
				break;
			}
			fputc(' ', fp);
		}
		fprintf(fp, "'\n");
	}
}


/**
 * DebugInfo_PrintOSHeader: output OS Header information
 */
static void DebugInfo_PrintOSHeader(FILE *fp, Uint32 sysbase)
{
	Uint32 gemblock, basepage;
	Uint16 osversion, datespec, osconf, langbits;
	const char *lang;
	static const char langs[][3] = {
		"us", "de", "fr", "uk", "es", "it", "se", "ch" /* fr */, "ch" /* de */,
		"tr", "fi", "no", "dk", "sa", "nl", "cs", "hu"
	};

	/* first more technical info */

	osversion = STMemory_ReadWord(sysbase+0x02);
	fprintf(fp, "OS base addr : 0x%06x\n", sysbase);
	fprintf(fp, "OS RAM end+1 : 0x%06x\n", STMemory_ReadLong(sysbase+0x0C));

	fprintf(fp, "Reset handler: 0x%06x\n", STMemory_ReadLong(sysbase+0x04));
	fprintf(fp, "Reset vector : 0x%06x\n", STMemory_ReadLong(RESET_VECTOR));
	fprintf(fp, "Reset valid  : 0x%x (valid=0x%x)\n", STMemory_ReadLong(RESET_VALID), RESET_MAGIC);

	gemblock = STMemory_ReadLong(sysbase+0x14);
	fprintf(fp, "GEM Memory Usage Parameter Block:\n");
	if ( STMemory_CheckAreaType ( gemblock, GEM_MUPB_SIZE, ABFLAG_RAM | ABFLAG_ROM ) ) {
		fprintf(fp, "- Block addr : 0x%06x\n", gemblock);
		fprintf(fp, "- GEM magic  : 0x%x (valid=0x%x)\n", STMemory_ReadLong(gemblock), GEM_MAGIC);
		fprintf(fp, "- GEM entry  : 0x%06x\n", STMemory_ReadLong(gemblock+4));
		fprintf(fp, "- GEM end    : 0x%06x\n", STMemory_ReadLong(gemblock+8));
	} else {
		fprintf(fp, "- is at INVALID 0x%06x address.\n", gemblock);
	}

	if (osversion >= 0x0102) {
		/* last 3 OS header fields are only available as of TOS 1.02 */
		fprintf(fp, "Memory pool  : 0x%06x\n", STMemory_ReadLong(sysbase+0x20));
		fprintf(fp, "Kbshift addr : 0x%06x\n", STMemory_ReadLong(sysbase+0x24));
	} else {
		/* TOS 1.0 */
		fprintf(fp, "Memory pool  : 0x0056FA\n");
		fprintf(fp, "Kbshift addr : 0x000E1B\n");
	}
	basepage = DebugInfo_CurrentBasepage(sysbase, true);
	if (basepage) {
		fprintf(fp, "Basepage     : 0x%06x\n", basepage);
	}

	/* and then basic TOS information */

	fputs("\n", fp);
	fprintf(fp, "TOS version  : 0x%x%s\n", osversion, bIsEmuTOS ? " (EmuTOS)" : "");
	/* Bits: 0-4 = day (1-31), 5-8 = month (1-12), 9-15 = years (since 1980) */
	datespec = STMemory_ReadWord(sysbase+0x1E);
	fprintf(fp, "Build date   : %04d-%02d-%02d\n", (datespec >> 9) + 1980,
	       (datespec & 0x1E0) >> 5, datespec & 0x1f);

	osconf = STMemory_ReadWord(sysbase+0x1C);
	langbits = osconf >> 1;
	if (langbits == 127) {
		lang = "all";
	} else if (langbits < ARRAYSIZE(langs)) {
		lang = langs[langbits];
	} else {
		lang = "unknown";
	}
	fprintf(fp, "OS config    : %s, %s (0x%x)\n", lang, osconf&1 ? "PAL":"NTSC", osconf);
	fprintf(fp, "Phystop      : %d KB\n", (STMemory_ReadLong(OS_PHYSTOP) + 511) / 1024);
}

/**
 * DebugInfo_OSHeader: display TOS OS Header and RAM one
 * if their addresses differ
 */
static void DebugInfo_OSHeader(FILE *fp, Uint32 dummy)
{
	Uint32 sysbase, rombase;

	sysbase = DebugInfo_GetSysbase(&rombase, false);
	if (!sysbase) {
		return;
	}
	fprintf(fp, "OS header information:\n");
	DebugInfo_PrintOSHeader(fp, sysbase);
	if (sysbase != rombase) {
		fprintf(fp, "\nROM TOS OS header information:\n");
		DebugInfo_PrintOSHeader(fp, rombase);
		return;
	}
}

/**
 * DebugInfo_Cookiejar: display TOS Cookiejar content
 */
static void DebugInfo_Cookiejar(FILE *fp, Uint32 dummy)
{
	int items;

	Uint32 jar = STMemory_ReadLong(COOKIE_JAR);
	if (!jar) {
		fprintf(fp, "Cookiejar is empty.\n");
		return;
	}

	fprintf(fp, "Cookiejar contents:\n");
	items = 0;
	while ( STMemory_CheckAreaType (jar, 8, ABFLAG_RAM ) && STMemory_ReadLong(jar)) {
		fprintf(fp, "%c%c%c%c = 0x%08x\n",
			STMemory_ReadByte(jar+0), STMemory_ReadByte(jar+1),
			STMemory_ReadByte(jar+2), STMemory_ReadByte(jar+3),
			STMemory_ReadLong(jar+4));
		jar += 8;
		items++;
	}
	fprintf(fp, "%d items at 0x%06x.\n", items, STMemory_ReadLong(COOKIE_JAR));
}


/* ------------------------------------------------------------------
 * CPU and DSP information wrappers
 */

/**
 * Helper to call debugcpu.c and debugdsp.c debugger commands
 */
static void DebugInfo_CallCommand(int (*func)(int, char* []), const char *command, Uint32 arg)
{
	char cmdbuffer[16], argbuffer[12];
	char *argv[] = { cmdbuffer, NULL };
	int argc = 1;

	assert(strlen(command) < sizeof(cmdbuffer));
	strcpy(cmdbuffer, command);
	if (arg) {
		sprintf(argbuffer, "$%x", arg);
		argv[argc++] = argbuffer;
	}
	func(argc, argv);
}

static void DebugInfo_CpuRegister(FILE *fp, Uint32 arg)
{
	DebugInfo_CallCommand(DebugCpu_Register, "register", arg);
}
static void DebugInfo_CpuDisAsm(FILE *fp, Uint32 arg)
{
	DebugInfo_CallCommand(DebugCpu_DisAsm, "disasm", arg);
}
static void DebugInfo_CpuMemDump(FILE *fp, Uint32 arg)
{
	DebugInfo_CallCommand(DebugCpu_MemDump, "memdump", arg);
}

#if ENABLE_DSP_EMU

static void DebugInfo_DspRegister(FILE *fp, Uint32 arg)
{
	DebugInfo_CallCommand(DebugDsp_Register, "dspreg", arg);
}
static void DebugInfo_DspDisAsm(FILE *fp, Uint32 arg)
{
	DebugInfo_CallCommand(DebugDsp_DisAsm, "dspdisasm", arg);
}

static void DebugInfo_DspMemDump(FILE *fp, Uint32 arg)
{
	char cmdbuf[] = "dspmemdump";
	char addrbuf[6], spacebuf[2] = "X";
	char *argv[] = { cmdbuf, spacebuf, addrbuf };
	spacebuf[0] = (arg>>16)&0xff;
	sprintf(addrbuf, "$%x", (Uint16)(arg&0xffff));
	DebugDsp_MemDump(3, argv);
}

/**
 * Convert arguments to Uint32 arg suitable for DSP memdump callback
 */
static Uint32 DebugInfo_DspMemArgs(int argc, char *argv[])
{
	Uint32 value;
	char space;
	if (argc != 2) {
		return 0;
	}
	space = toupper((unsigned char)argv[0][0]);
	if ((space != 'X' && space != 'Y' && space != 'P') || argv[0][1]) {
		fprintf(stderr, "ERROR: invalid DSP address space '%s'!\n", argv[0]);
		return 0;
	}
	if (!Eval_Number(argv[1], &value) || value > 0xffff) {
		fprintf(stderr, "ERROR: invalid DSP address '%s'!\n", argv[1]);
		return 0;
	}
	return ((Uint32)space<<16) | value;
}

#endif  /* ENABLE_DSP_EMU */


static void DebugInfo_RegAddr(FILE *fp, Uint32 arg)
{
	bool forDsp;
	char regname[3];
	Uint32 *reg32, regvalue, mask;
	char cmdbuf[12], addrbuf[6];
	char *argv[] = { cmdbuf, addrbuf };
	
	regname[0] = (arg>>24)&0xff;
	regname[1] = (arg>>16)&0xff;
	regname[2] = '\0';

	if (DebugCpu_GetRegisterAddress(regname, &reg32)) {
		regvalue = *reg32;
		mask = 0xffffffff;
		forDsp = false;
	} else {
		int regsize = DSP_GetRegisterAddress(regname, &reg32, &mask);
		switch (regsize) {
			/* currently regaddr supports only 32-bit Rx regs, but maybe later... */
		case 16:
			regvalue = *((Uint16*)reg32);
			break;
		case 32:
			regvalue = *reg32;
			break;
		default:
			fprintf(stderr, "ERROR: invalid address/data register '%s'!\n", regname);
			return;
		}
		forDsp = true;
	}
       	sprintf(addrbuf, "$%x", regvalue & mask);

	if ((arg & 0xff) == 'D') {
		if (forDsp) {
#if ENABLE_DSP_EMU
			strcpy(cmdbuf, "dd");
			DebugDsp_DisAsm(2, argv);
#endif
		} else {
			strcpy(cmdbuf, "d");
			DebugCpu_DisAsm(2, argv);
		}
	} else {
		if (forDsp) {
#if ENABLE_DSP_EMU
			/* use "Y" address space */
			char cmd[] = "dm"; char space[] = "y";
			char *dargv[] = { cmd, space, addrbuf };
			DebugDsp_MemDump(3, dargv);
#endif
		} else {
			strcpy(cmdbuf, "m");
			DebugCpu_MemDump(2, argv);
		}
	}
}

/**
 * Convert arguments to Uint32 arg suitable for RegAddr callback
 */
static Uint32 DebugInfo_RegAddrArgs(int argc, char *argv[])
{
	Uint32 value, *regaddr;
	if (argc != 2) {
		return 0;
	}

	if (strcmp(argv[0], "disasm") == 0) {
		value = 'D';
	} else if (strcmp(argv[0], "memdump") == 0) {
		value = 'M';
	} else {
		fprintf(stderr, "ERROR: regaddr operation can be only 'disasm' or 'memdump', not '%s'!\n", argv[0]);
		return 0;
	}

	if (strlen(argv[1]) != 2 ||
	    (!DebugCpu_GetRegisterAddress(argv[1], &regaddr) &&
	     (toupper((unsigned char)argv[1][0]) != 'R'
	      || !isdigit((unsigned char)argv[1][1]) || argv[1][2]))) {
		/* not CPU register or Rx DSP register */
		fprintf(stderr, "ERROR: invalid address/data register '%s'!\n", argv[1]);
		return 0;
	}
	
	value |= argv[1][0] << 24;
	value |= argv[1][1] << 16;
	value &= 0xffff00ff;
	return value;
}


/* ------------------------------------------------------------------
 * wrappers for command to parse debugger input file
 */

/* file name to be given before calling the Parse function,
 * needs to be set separately as it's a host pointer which
 * can be 64-bit i.e. may not fit into Uint32.
 */
static char *parse_filename;

/**
 * Parse and exec commands in the previously given debugger input file
 */
static void DebugInfo_FileParse(FILE *fp, Uint32 dummy)
{
	if (parse_filename) {
		DebugUI_ParseFile(parse_filename, true);
	} else {
		fputs("ERROR: debugger input file name to parse isn't set!\n", stderr);
	}
}

/**
 * Set which input file to parse.
 * Return true if file exists, false on error
 */
static Uint32 DebugInfo_FileArgs(int argc, char *argv[])
{
	if (argc != 1) {
		return false;
	}
	if (!File_Exists(argv[0])) {
		fprintf(stderr, "ERROR: given file '%s' doesn't exist!\n", argv[0]);
		return false;
	}
	if (parse_filename) {
		free(parse_filename);
	}
	parse_filename = strdup(argv[0]);
	return true;
}


/* ------------------------------------------------------------------
 * Debugger & readline TAB completion integration
 */

/**
 * Default information on entering the debugger
 */
static void DebugInfo_Default(FILE *fp, Uint32 dummy)
{
	int hbl, fcycles, lcycles;
        uaecptr nextpc, pc = M68000_GetPC();
	Video_GetPosition(&fcycles, &hbl, &lcycles);

	fprintf(fp, "\nCPU=$%x, VBL=%d, FrameCycles=%d, HBL=%d, LineCycles=%d, DSP=",
		pc, nVBLs, fcycles, hbl, lcycles);
	if (bDspEnabled)
		fprintf(fp, "$%x\n", DSP_GetPC());
	else
		fprintf(fp, "N/A\n");

	Disasm(fp, pc, &nextpc, 1);
}

static const struct {
	/* if overlaps with other functionality, list only for lock command */
	bool lock;
	const char *name;
	void (*func)(FILE *fp, Uint32 arg);
	/* convert args in argv into single Uint32 for func */
	Uint32 (*args)(int argc, char *argv[]);
	const char *info;
} infotable[] = {
	{ false,"aes",       AES_Info,             NULL, "Show AES vector contents (with <value>, show opcodes)" },
	{ false,"basepage",  DebugInfo_Basepage,   NULL, "Show program basepage contents at given <address>" },
	{ false,"bios",      Bios_Info,            NULL, "Show BIOS opcodes" },
	{ false,"blitter",   Blitter_Info,         NULL, "Show Blitter register contents" },
	{ false,"cookiejar", DebugInfo_Cookiejar,  NULL, "Show TOS Cookiejar contents" },
	{ false,"crossbar",  Crossbar_Info,        NULL, "Show Falcon Crossbar register contents" },
	{ true, "default",   DebugInfo_Default,    NULL, "Show default debugger entry information" },
	{ true, "disasm",    DebugInfo_CpuDisAsm,  NULL, "Disasm CPU from PC or given <address>" },
#if ENABLE_DSP_EMU
	{ false, "dsp",      DSP_Info,             NULL, "Show misc. DSP core info (stack etc)" },
	{ true, "dspdisasm", DebugInfo_DspDisAsm,  NULL, "Disasm DSP from given <address>" },
	{ true, "dspmemdump",DebugInfo_DspMemDump, DebugInfo_DspMemArgs, "Dump DSP memory from given <space> <address>" },
	{ true, "dspregs",   DebugInfo_DspRegister,NULL, "Show DSP register contents" },
#endif
	{ true, "file",      DebugInfo_FileParse, DebugInfo_FileArgs, "Parse commands from given debugger input <file>" },
	{ false,"gemdos",    GemDOS_Info,          NULL, "Show GEMDOS HDD emu information (with <value>, show opcodes)" },
	{ true, "history",   History_Show,         NULL, "Show history of last <count> instructions" },
	{ true, "memdump",   DebugInfo_CpuMemDump, NULL, "Dump CPU memory from given <address>" },
	{ false,"osheader",  DebugInfo_OSHeader,   NULL, "Show TOS OS header contents" },
	{ true, "regaddr",   DebugInfo_RegAddr, DebugInfo_RegAddrArgs, "Show <disasm|memdump> from CPU/DSP address pointed by <register>" },
	{ true, "registers", DebugInfo_CpuRegister,NULL, "Show CPU register contents" },
	{ false,"vdi",       VDI_Info,             NULL, "Show VDI vector contents (with <value>, show opcodes)" },
	{ false,"videl",     Videl_Info,           NULL, "Show Falcon Videl register contents" },
	{ false,"video",     Video_Info,           NULL, "Show Video information" },
	{ false,"xbios",     XBios_Info,           NULL, "Show XBIOS opcodes" },
	{ false,"ym",        PSG_Info,             NULL, "Show YM-2149 register contents" },
};

static int LockedFunction = 6; /* index for the "default" function */
static Uint32 LockedArgument;

/**
 * Show selected debugger session information
 * (when debugger is (again) entered)
 */
void DebugInfo_ShowSessionInfo(void)
{
	infotable[LockedFunction].func(stderr, LockedArgument);
}


/**
 * Readline match callback for info subcommand name completion.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
static char *DebugInfo_Match(const char *text, int state, bool lock)
{
	static int i, len;
	const char *name;
	
	if (!state) {
		/* first match */
		len = strlen(text);
		i = 0;
	}
	/* next match */
	while (i++ < ARRAYSIZE(infotable)) {
		if (!lock && infotable[i-1].lock) {
			continue;
		}
		name = infotable[i-1].name;
		if (strncmp(name, text, len) == 0)
			return (strdup(name));
	}
	return NULL;
}
char *DebugInfo_MatchLock(const char *text, int state)
{
	return DebugInfo_Match(text, state, true);
}
char *DebugInfo_MatchInfo(const char *text, int state)
{
	return DebugInfo_Match(text, state, false);
}


/**
 * Show requested command information.
 */
int DebugInfo_Command(int nArgc, char *psArgs[])
{
	Uint32 value;
	const char *cmd;
	bool ok, lock;
	int i, sub;

	sub = -1;
	if (nArgc > 1) {
		cmd = psArgs[1];		
		/* which subcommand? */
		for (i = 0; i < ARRAYSIZE(infotable); i++) {
			if (strcmp(cmd, infotable[i].name) == 0) {
				sub = i;
				break;
			}
		}
	}

	if (sub >= 0 && infotable[sub].args) {
		/* value needs callback specific conversion */
		value = infotable[sub].args(nArgc-2, psArgs+2);
		ok = !!value;
	} else {
		if (nArgc > 2) {
			/* value is normal number */
			ok = Eval_Number(psArgs[2], &value);
		} else {
			value = 0;
			ok = true;
		}
	}

	lock = (strcmp(psArgs[0], "lock") == 0);
	
	if (sub < 0 || !ok) {
		/* no subcommand or something wrong with value, show info */
		fprintf(stderr, "%s subcommands are:\n", psArgs[0]);
		for (i = 0; i < ARRAYSIZE(infotable); i++) {
			if (!lock && infotable[i].lock) {
				continue;
			}
			fprintf(stderr, "- %s: %s\n",
				infotable[i].name, infotable[i].info);
		}
		return DEBUGGER_CMDDONE;
	}

	if (lock) {
		/* lock given subcommand and value */
		LockedFunction = sub;
		LockedArgument = value;
		fprintf(stderr, "Locked %s output.\n", psArgs[1]);
	} else {
		/* do actual work */
		infotable[sub].func(stderr, value);
	}
	return DEBUGGER_CMDDONE;
}
