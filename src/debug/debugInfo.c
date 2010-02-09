/*
  Hatari - debuginfo.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  debuginfo.c - functions needed to give some infos about the atari components 
                in debug mode.
*/
const char DebugInfo_fileid[] = "Hatari debuginfo.c : " __DATE__ " " __TIME__;

#include <stdio.h>
#include "main.h"
#include "configuration.h"
#include "debugInfo.h"
#include "debugcpu.h"
#include "debugdsp.h"
#include "debugui.h"
#include "dsp.h"
#include "evaluate.h"
#include "ioMem.h"
#include "m68000.h"
#include "tos.h"
#include "video.h"


/* ------------------------------------------------------------------
 * TOS information
 */
#define OS_SYSBASE 0x4F2
#define OS_HEADER_SIZE 0x30

#define COOKIE_JAR 0x5A0

#define BASEPAGE_SIZE 0x100

#define GEM_MAGIC 0x87654321
#define GEM_MUPB_SIZE 0xC

#define RESET_MAGIC 0x31415926
#define RESET_VALID 0x426
#define RESET_VECTOR 0x42A

#define COUNTRY_SPAIN 4

/**
 * DebugInfo_GetSysbase: set osversion to given argument.
 * return sysbase address on success and zero on failure.
 */
static Uint32 DebugInfo_GetSysbase(Uint16 *osversion)
{
	Uint32 sysbase = STMemory_ReadLong(OS_SYSBASE);

	if (!STMemory_ValidArea(sysbase, OS_HEADER_SIZE)) {
		fprintf(stderr, "Invalid TOS base address!\n");
		return 0;
	}
	if (sysbase != TosAddress || sysbase != STMemory_ReadLong(sysbase+0x08)) {
		fprintf(stderr, "Sysbase and os_beg address in OS header mismatch!\n");
		return 0;
	}
	*osversion = STMemory_ReadWord(sysbase+0x02);
	return sysbase;
}

/**
 * DebugInfo_CurrentBasepage: get currently running TOS program basepage
 */
static Uint32 DebugInfo_CurrentBasepage(void)
{
	Uint32 basepage, sysbase;
	Uint16 osversion, osconf;

	sysbase = DebugInfo_GetSysbase(&osversion);
	if (!sysbase) {
		return 0;
	}
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
	if (STMemory_ValidArea(basepage, 4)) {
		return STMemory_ReadLong(basepage);
	}
	fprintf(stderr, "Pointer 0x%06x to basepage address is invalid!\n", basepage);
	return 0;
}

/**
 * DebugInfo_Basepage: show TOS process basepage information
 * at given address.
 */
static void DebugInfo_Basepage(Uint32 basepage)
{
	Uint32 env, cmd;

	if (!basepage) {
		/* default to current process basepage */
		basepage = DebugInfo_CurrentBasepage();
		if (!basepage) {
			return;
		}
	}
	fprintf(stderr, "Process basepage information:\n");
	if (!STMemory_ValidArea(basepage, BASEPAGE_SIZE) ||
	    STMemory_ReadLong(basepage) != basepage) {
		fprintf(stderr, "- address 0x%06x is invalid!\n", basepage);
		return;
	}
	fprintf(stderr, "- TPA start      : 0x%06x\n", STMemory_ReadLong(basepage));
	fprintf(stderr, "- TPA end +1     : 0x%06x\n", STMemory_ReadLong(basepage+0x04));
	fprintf(stderr, "- Text segment   : 0x%06x\n", STMemory_ReadLong(basepage+0x08));
	fprintf(stderr, "- Text size      : 0x%x\n",   STMemory_ReadLong(basepage+0x0C));
	fprintf(stderr, "- Data segment   : 0x%06x\n", STMemory_ReadLong(basepage+0x10));
	fprintf(stderr, "- Data size      : 0x%x\n",   STMemory_ReadLong(basepage+0x14));
	fprintf(stderr, "- BSS segment    : 0x%06x\n", STMemory_ReadLong(basepage+0x18));
	fprintf(stderr, "- BSS size       : 0x%x\n",   STMemory_ReadLong(basepage+0x1C));
	fprintf(stderr, "- Process DTA    : 0x%06x\n", STMemory_ReadLong(basepage+0x20));
	fprintf(stderr, "- Parent basepage: 0x%06x\n", STMemory_ReadLong(basepage+0x24));

	env = STMemory_ReadLong(basepage+0x2C);
	fprintf(stderr, "- Environment    : 0x%06x\n", env);
	if (STMemory_ValidArea(env, 4096)) {
		fprintf(stderr, "'%s'\n", STRam+env);
	}
	cmd = STMemory_ReadLong(basepage+0x2C);
	fprintf(stderr, "- Command line   :\n'%s'\n", STRam+basepage+0x80);
}

/**
 * DebugInfo_OSHeader: display TOS OS Header
 */
static void DebugInfo_OSHeader(Uint32 dummy)
{
	Uint32 sysbase, gemblock, basepage;
	Uint16 osversion, osconf;

	sysbase = DebugInfo_GetSysbase(&osversion);
	if (!sysbase) {
		return;
	}
	fprintf(stderr, "OS base addr : 0x%06x\n", sysbase);
	fprintf(stderr, "OS RAM end+1 : 0x%06x\n", STMemory_ReadLong(sysbase+0x0C));
	fprintf(stderr, "TOS version  : 0x%x\n", osversion);

	fprintf(stderr, "Reset handler: 0x%06x\n", STMemory_ReadLong(sysbase+0x04));
	fprintf(stderr, "Reset vector : 0x%06x\n", STMemory_ReadLong(RESET_VECTOR));
	fprintf(stderr, "Reset valid  : 0x%x (valid=0x%x)\n", STMemory_ReadLong(RESET_VALID), RESET_MAGIC);

	gemblock = STMemory_ReadLong(sysbase+0x14);
	fprintf(stderr, "GEM Memory Usage Parameter Block:\n");
	if (STMemory_ValidArea(gemblock, GEM_MUPB_SIZE)) {
		fprintf(stderr, "- Block addr : 0x%06x\n", gemblock);
		fprintf(stderr, "- GEM magic  : 0x%x (valid=0x%x)\n", STMemory_ReadLong(gemblock), GEM_MAGIC);
		fprintf(stderr, "- GEM entry  : 0x%06x\n", STMemory_ReadLong(gemblock+4));
		fprintf(stderr, "- GEM end    : 0x%06x\n", STMemory_ReadLong(gemblock+8));
	} else {
		fprintf(stderr, "- is at INVALID 0x%06x address.\n", gemblock);
	}

	fprintf(stderr, "OS date      : 0x%x\n", STMemory_ReadLong(sysbase+0x14));
	fprintf(stderr, "OS DOS date  : 0x%x\n", STMemory_ReadLong(sysbase+0x1E));

	osconf = STMemory_ReadWord(sysbase+0x1C);
	fprintf(stderr, "OS Conf bits : lang=%d, %s\n", osconf>>1, osconf&1 ? "PAL":"NTSC");
	fprintf(stderr, "Cookie Jar   : 0x%06x\n", STMemory_ReadLong(COOKIE_JAR));

	/* last 3 OS header fields are only available as of TOS 1.02 */
	if (osversion >= 0x0102) {
		fprintf(stderr, "Memory pool  : 0x%06x\n", STMemory_ReadLong(sysbase+0x20));
		fprintf(stderr, "Kbshift addr : 0x%06x\n", STMemory_ReadLong(sysbase+0x24));
	} else {
		/* TODO: GEMDOS memory pool address for TOS 1.0? */
		fprintf(stderr, "Kbshift addr : 0x000E1B\n");
	}
	basepage = DebugInfo_CurrentBasepage();
	if (basepage) {
		fprintf(stderr, "Basepage     : 0x%06x\n", basepage);
	}
}


/* ------------------------------------------------------------------
 * Falcon HW information
 */

/**
 * DebugInfo_Videl : display the Videl registers values.
 */
static void DebugInfo_Videl(Uint32 dummy)
{
	if (ConfigureParams.System.nMachineType != MACHINE_FALCON) {
		fprintf(stderr, "Not Falcon - no Videl!\n");
		return;
	}

	fprintf(stderr, "$FF8006 : monitor type                     : %02x\n", IoMem_ReadByte(0xff8006));
	fprintf(stderr, "$FF820E : offset to next line              : %04x\n", IoMem_ReadWord(0xff820e));
	fprintf(stderr, "$FF8210 : VWRAP - line width               : %04x\n", IoMem_ReadWord(0xff8210));
	fprintf(stderr, "$FF8260 : ST shift mode                    : %02x\n", IoMem_ReadByte(0xff8260));
	fprintf(stderr, "$FF8265 : Horizontal scroll register       : %02x\n", IoMem_ReadByte(0xff8265));
	fprintf(stderr, "$FF8266 : Falcon shift mode                : %04x\n", IoMem_ReadWord(0xff8266));
	fprintf(stderr, "\n");
	fprintf(stderr, "$FF8280 : HHC - Horizontal Hold Counter    : %04x\n", IoMem_ReadWord(0xff8280));
	fprintf(stderr, "$FF8282 : HHT - Horizontal Hold Timer      : %04x\n", IoMem_ReadWord(0xff8282));
	fprintf(stderr, "$FF8284 : HBB - Horizontal Border Begin    : %04x\n", IoMem_ReadWord(0xff8284));
	fprintf(stderr, "$FF8286 : HBE - Horizontal Border End      : %04x\n", IoMem_ReadWord(0xff8286));
	fprintf(stderr, "$FF8288 : HDB - Horizontal Display Begin   : %04x\n", IoMem_ReadWord(0xff8288));
	fprintf(stderr, "$FF828A : HDE - Horizontal Display End     : %04x\n", IoMem_ReadWord(0xff828a));
	fprintf(stderr, "$FF828C : HSS - Horizontal SS              : %04x\n", IoMem_ReadWord(0xff828c));
	fprintf(stderr, "$FF828E : HFS - Horizontal FS              : %04x\n", IoMem_ReadWord(0xff828e));
	fprintf(stderr, "$FF8290 : HEE - Horizontal EE              : %04x\n", IoMem_ReadWord(0xff8290));
	fprintf(stderr, "\n");
	fprintf(stderr, "$FF82A0 : VFC - Vertical Frequency Counter : %04x\n", IoMem_ReadWord(0xff82a0));
	fprintf(stderr, "$FF82A2 : VFT - Vertical Frequency Timer   : %04x\n", IoMem_ReadWord(0xff82a2));
	fprintf(stderr, "$FF82A4 : VBB - Vertical Border Begin      : %04x\n", IoMem_ReadWord(0xff82a4));
	fprintf(stderr, "$FF82A6 : VBE - Vertical Border End        : %04x\n", IoMem_ReadWord(0xff82a6));
	fprintf(stderr, "$FF82A8 : VDB - Vertical Display Begin     : %04x\n", IoMem_ReadWord(0xff82a8));
	fprintf(stderr, "$FF82AA : VDE - Vertical Display End       : %04x\n", IoMem_ReadWord(0xff82aa));
	fprintf(stderr, "$FF82AC : VSS - Vertical SS                : %04x\n", IoMem_ReadWord(0xff82ac));
	fprintf(stderr, "\n");
	fprintf(stderr, "$FF82C0 : VCO - Video control              : %04x\n", IoMem_ReadWord(0xff82c0));
	fprintf(stderr, "$FF82C2 : VMD - Video mode                 : %04x\n", IoMem_ReadWord(0xff82c2));
	fprintf(stderr, "\n");
}

/**
 * DebugInfo_Crossbar : display the Crossbar registers values.
 */
static void DebugInfo_Crossbar(Uint32 dummy)
{
	if (ConfigureParams.System.nMachineType != MACHINE_FALCON) {
		fprintf(stderr, "Not Falcon - no Crossbar!\n");
		return;
	}

	fprintf(stderr, "$FF8900 : Sound DMA control                     : %02x\n", IoMem_ReadByte(0xff8900));
	fprintf(stderr, "$FF8901 : Sound DMA control                     : %02x\n", IoMem_ReadByte(0xff8901));
	fprintf(stderr, "$FF8903 : Frame Start High                      : %02x\n", IoMem_ReadByte(0xff8903));
	fprintf(stderr, "$FF8905 : Frame Start middle                    : %02x\n", IoMem_ReadByte(0xff8905));
	fprintf(stderr, "$FF8907 : Frame Start low                       : %02x\n", IoMem_ReadByte(0xff8907));
	fprintf(stderr, "$FF8909 : Frame Count High                      : %02x\n", IoMem_ReadByte(0xff8909));
	fprintf(stderr, "$FF890B : Frame Count middle                    : %02x\n", IoMem_ReadByte(0xff890b));
	fprintf(stderr, "$FF890D : Frame Count low                       : %02x\n", IoMem_ReadByte(0xff890d));
	fprintf(stderr, "$FF890f : Frame End High                        : %02x\n", IoMem_ReadByte(0xff890f));
	fprintf(stderr, "$FF8911 : Frame End middle                      : %02x\n", IoMem_ReadByte(0xff8911));
	fprintf(stderr, "$FF8913 : Frame End low                         : %02x\n", IoMem_ReadByte(0xff8913));
	fprintf(stderr, "\n");
	fprintf(stderr, "$FF8920 : Sound Mode Control                    : %02x\n", IoMem_ReadByte(0xff8920));
	fprintf(stderr, "$FF8921 : Sound Mode Control                    : %02x\n", IoMem_ReadByte(0xff8921));
	fprintf(stderr, "$FF8930 : DMA Crossbar Input Select Controller  : %04x\n", IoMem_ReadWord(0xff8930));
	fprintf(stderr, "$FF8932 : DMA Crossbar Output Select Controller : %04x\n", IoMem_ReadWord(0xff8932));
	fprintf(stderr, "\n");
	fprintf(stderr, "$FF8934 : External Sync Frequency Divider       : %02x\n", IoMem_ReadByte(0xff8934));
	fprintf(stderr, "$FF8935 : Internal Sync Frequency Divider       : %02x\n", IoMem_ReadByte(0xff8935));
	fprintf(stderr, "$FF8936 : Record Track select                   : %02x\n", IoMem_ReadByte(0xff8936));
	fprintf(stderr, "$FF8937 : Codec Input Source                    : %02x\n", IoMem_ReadByte(0xff8937));
	fprintf(stderr, "$FF8938 : Codec ADC Input                       : %02x\n", IoMem_ReadByte(0xff8938));
	fprintf(stderr, "$FF8939 : Gain Settings Per Channel             : %02x\n", IoMem_ReadByte(0xff8939));
	fprintf(stderr, "$FF893A : Attenuation Settings Per Channel      : %02x\n", IoMem_ReadByte(0xff893a));
	fprintf(stderr, "$FF893C : Codec Status                          : %04x\n", IoMem_ReadWord(0xff893c));
	fprintf(stderr, "$FF8940 : GPIO Data Direction                   : %04x\n", IoMem_ReadWord(0xff8940));
	fprintf(stderr, "$FF8942 : GPIO Data                             : %04x\n", IoMem_ReadWord(0xff8942));
}


/* ------------------------------------------------------------------
 * CPU information wrappers
 */

/**
 * Helper to call debugcpu.c debugger commands
 */
static void DebugInfo_CallCommand(int (*func)(int, char* []), const char *command, Uint32 arg)
{
	char cmdbuffer[12], argbuffer[32];
	char *argv[] = { argbuffer, NULL };
	int argc = 1;

	strcpy(cmdbuffer, command);
	if (arg) {
		sprintf(argbuffer, "$%x", arg);
		argv[argc++] = argbuffer;
	}
	func(argc, argv);
}

static void DebugInfo_DisAsm(Uint32 arg)
{
	if (!arg) {
		arg = M68000_GetPC();
	}
	DebugInfo_CallCommand(DebugCpu_DisAsm, "disasm", arg);
}
static void DebugInfo_MemDump(Uint32 arg)
{
	DebugInfo_CallCommand(DebugCpu_MemDump, "memdump", arg);
}
static void DebugInfo_Register(Uint32 arg)
{
	DebugInfo_CallCommand(DebugCpu_Register, "register", arg);
}


/* ------------------------------------------------------------------
 * Debugger & readline TAB completion integration
 */

/**
 * Default information on entering the debugger
 */
static void DebugInfo_Default(Uint32 dummy)
{
	int hbl, fcycles, lcycles;
	Video_GetPosition(&fcycles, &hbl, &lcycles);
	fprintf(stderr, "\nCPU=$%x, VBL=%d, FrameCycles=%d, HBL=%d, LineCycles=%d, DSP=",
		M68000_GetPC(), nVBLs, fcycles, hbl, lcycles);
	if (bDspEnabled)
		fprintf(stderr, "$%x\n", DSP_GetPC());
	else
		fprintf(stderr, "N/A\n");
}

static void (*LockedFunction)(Uint32 arg) = DebugInfo_Default;
static Uint32 LockedArgument;

static const struct {
	const char *name;
	void (*func)(Uint32 arg);
	const char *info;
} infotable[] = {
	{ "basepage", DebugInfo_Basepage, "program basepage info at <given address>" },
	{ "crossbar", DebugInfo_Crossbar, "Falcon crossbar HW register values" },
	{ "default",  DebugInfo_Default,  "Default debugger entry information" },
	{ "disasm",   DebugInfo_DisAsm,   "Disasm from PC or <given address>" },
	{ "memdump",  DebugInfo_MemDump,  "Dump memory from PC or <given address>" },
	{ "osheader", DebugInfo_OSHeader, "TOS OS header information" },
	{ "register", DebugInfo_Register, "Show register values" },
	{ "videl",    DebugInfo_Videl,    "Falcon Videl HW register values" }
};

/**
 * Show selected information
 */
void DebugInfo_ShowInfo(void)
{
	LockedFunction(LockedArgument);
}


/**
 * Readline match callback for info subcommand name completion.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char *DebugInfo_MatchCommand(const char *text, int state)
{
	static int i, len;
	const char *name;
	
	if (!state) {
		/* first match */
		len = strlen(text);
		i = 0;
	}
	/* next match */
	while (i < ARRAYSIZE(infotable)) {
		name = infotable[i++].name;
		if (strncmp(name, text, len) == 0)
			return (strdup(name));
	}
	return NULL;
}


/**
 * Show requested command information.
 */
int DebugInfo_Command(int nArgc, char *psArgs[])
{
	const char *cmd;
	Uint32 value = 0;
	bool lock = false;
	bool ok = true;
	int i;

	if (strcmp(psArgs[nArgc-1], "lock") == 0) {
		lock = true;
		nArgc--;
	}
	if (nArgc > 2) {
		ok = Eval_Number(psArgs[2], &value);
	}
	if (ok && nArgc > 1) {
		cmd = psArgs[1];		
		for (i = 0; i < ARRAYSIZE(infotable); i++) {
			if (strcmp(cmd, infotable[i].name) == 0) {
				if (lock) {
					LockedFunction = infotable[i].func;
					LockedArgument = value;
					fprintf(stderr, "Locked %s info.\n", cmd);
				} else {
					infotable[i].func(value);
				}
				return DEBUGGER_CMDDONE;
			}
		}
	}
	fprintf(stderr, "Info subcommands are:\n");
	for (i = 0; i < ARRAYSIZE(infotable); i++) {
		fprintf(stderr, "- %s: show %s\n",
			infotable[i].name, infotable[i].info);
	}
	return DEBUGGER_CMDDONE;
}
