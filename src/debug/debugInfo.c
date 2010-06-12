/*
  Hatari - debuginfo.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  debuginfo.c - functions needed to show info about the atari HW & OS
   components and "lock" that info to be shown on entering the debugger.
*/
const char DebugInfo_fileid[] = "Hatari debuginfo.c : " __DATE__ " " __TIME__;

#include <stdio.h>
#include <assert.h>
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
#include "stMemory.h"
#include "tos.h"
#include "screen.h"
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
	Uint8 cmdlen;
	Uint32 env;

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
		Uint32 end = env + 4096;
		while (env < end && *(STRam+env)) {
			fprintf(stderr, "'%s'\n", STRam+env);
			env += strlen((const char *)(STRam+env)) + 1;
		}
	}
	cmdlen = STMemory_ReadByte(basepage+0x80);
	fprintf(stderr, "- Command argslen: %d\n", cmdlen);
	if (cmdlen) {
		int offset = 0;
		while (offset < cmdlen) {
			fprintf(stderr, " '%s'", STRam+basepage+0x81+offset);
			offset += strlen((const char *)(STRam+basepage+0x81+offset)) + 1;
		}
		fprintf(stderr, "\n");
	}
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
	char matrixDMA[5], matrixDAC[5], matrixDSP[5], matrixEXT[5];
	char frqDMA[11], frqDAC[11], frqDSP[11], frqEXT[11];
	char dataSize[15];
	
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
	fprintf(stderr, "\n");
	
	/* DAC connexion */
	switch ((IoMem_ReadWord(0xff8932) >> 13) & 3) {
		case 0 : strcpy(matrixDAC, "OOXO"); break;
		case 1 : strcpy(matrixDAC, "OXOO"); break;
		case 2 : strcpy(matrixDAC, "XOOO"); break;
		case 3 : strcpy(matrixDAC, "OOOX"); break;
	}
		
	/* DMA connexion */
	switch ((IoMem_ReadWord(0xff8932) >> 1) & 3) {
		case 0 : strcpy(matrixDMA, "OOXO"); break;
		case 1 : strcpy(matrixDMA, "OXOO"); break;
		case 2 : strcpy(matrixDMA, "XOOO"); break;
		case 3 : strcpy(matrixDMA, "OOOX"); break;
	}

	/* DSP connexion */
	switch ((IoMem_ReadWord(0xff8932) >> 5) & 3) {
		case 0 : strcpy(matrixDSP, "OOXO"); break;
		case 1 : strcpy(matrixDSP, "OXOO"); break;
		case 2 : strcpy(matrixDSP, "XOOO"); break;
		case 3 : strcpy(matrixDSP, "OOOX"); break;
	}

	/* External input connexion */
	switch ((IoMem_ReadWord(0xff8932) >> 9) & 3) {
		case 0 : strcpy(matrixEXT, "OOXO"); break;
		case 1 : strcpy(matrixEXT, "OXOO"); break;
		case 2 : strcpy(matrixEXT, "XOOO"); break;
		case 3 : strcpy(matrixEXT, "OOOX"); break;
	}

	/* HandShake mode test */
	if ((IoMem_ReadWord(0xff8932) & 7) == 2) {
		matrixDMA[1] = 'H';
	}

	/* HandShake mode test */
	if ((IoMem_ReadWord(0xff8932) & 0xf) == 2) {
		matrixDSP[2] = 'H';
	}

	/* DSP Frequency */
	if ((IoMem_ReadByte(0xff8935) & 0xf) == 0) {
		strcpy(frqDSP, "(STe Freq)");
	}else {
		switch ((IoMem_ReadWord(0xff8930) >> 5) & 0x3) {
			case 0: strcpy(frqDSP, " (25 Mhz) "); break;
			case 1: strcpy(frqDSP, "(External)"); break;
			case 2: strcpy(frqDSP, " (32 Mhz) "); break;
			default:  strcpy(frqDSP, "undefined "); break;break;
		}
	}

	/* DMA Frequency */
	if ((IoMem_ReadByte(0xff8935) & 0xf) == 0) {
		strcpy(frqDMA, "(STe Freq)");
	}else {
		switch ((IoMem_ReadWord(0xff8930) >> 1) & 0x3) {
			case 0: strcpy(frqDMA, " (25 Mhz) "); break;
			case 1: strcpy(frqDMA, "(External)"); break;
			case 2: strcpy(frqDMA, " (32 Mhz) "); break;
			default:  strcpy(frqDMA, "undefined "); break;break;
		}
	}

	/* External Frequency */
	if ((IoMem_ReadByte(0xff8935) & 0xf) == 0) {
		strcpy(frqEXT, "(STe Freq)");
	}else {
		switch ((IoMem_ReadWord(0xff8930) >> 9) & 0x3) {
			case 0: strcpy(frqEXT, " (25 Mhz) "); break;
			case 1: strcpy(frqEXT, "(External)"); break;
			case 2: strcpy(frqEXT, " (32 Mhz) "); break;
			default:  strcpy(frqEXT, "undefined "); break;break;
		}
	}

	/* DAC Frequency */
	if ((IoMem_ReadByte(0xff8935) & 0xf) == 0) {
		strcpy(frqDAC, "(STe Freq)");
	}else {
		strcpy(frqDAC, " (25 Mhz) ");
	}

	/* data size */
	switch ((IoMem_ReadByte(0xff8921) >> 6) & 0x3) {
		case 0: strcpy (dataSize, "8 bits stereo"); break;
		case 1: strcpy (dataSize, "16 bits stereo"); break;
		case 2: strcpy (dataSize, "8 bits mono"); break;
		default: strcpy (dataSize, "undefined"); break;
	}

	/* Display the crossbar Matrix */
	fprintf(stderr, "           INPUT\n");
	fprintf(stderr, "External Imp  ---%c------%c------%c------%c\n", matrixDAC[0], matrixDMA[0], matrixDSP[0], matrixEXT[0]);
	fprintf(stderr, "%s       |      |      |      |    O = no connexion\n", frqEXT);
	fprintf(stderr, "                 |      |      |      |    X = connexion\n");
	fprintf(stderr, "Dsp Transmit  ---%c------%c------%c------%c    H = Handshake connexion\n", matrixDAC[1], matrixDMA[1], matrixDSP[1], matrixEXT[1]);
	fprintf(stderr, "%s       |      |      |      |\n", frqDSP);
	fprintf(stderr, "                 |      |      |      |    %s\n", dataSize);
	fprintf(stderr, "DMA PlayBack  ---%c------%c------%c------%c\n", matrixDAC[2], matrixDMA[2], matrixDSP[2], matrixEXT[2]);
	fprintf(stderr, "%s       |      |      |      |\n", frqDMA);
	fprintf(stderr, "                 |      |      |      |\n");
	fprintf(stderr, "ADC           ---%c------%c------%c------%c\n", matrixDAC[3], matrixDMA[3], matrixDSP[3], matrixEXT[3]);
	fprintf(stderr, "%s       |      |      |      |\n", frqDAC);
	fprintf(stderr, "                DAC    DMA    DSP   External     OUTPUT\n");
	fprintf(stderr, "                     Record  Record   Out\n");
	fprintf(stderr, "\n");
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

static void DebugInfo_CpuRegister(Uint32 arg)
{
	DebugInfo_CallCommand(DebugCpu_Register, "register", arg);
}
static void DebugInfo_CpuDisAsm(Uint32 arg)
{
	DebugInfo_CallCommand(DebugCpu_DisAsm, "disasm", arg);
}
static void DebugInfo_CpuMemDump(Uint32 arg)
{
	DebugInfo_CallCommand(DebugCpu_MemDump, "memdump", arg);
}

#if ENABLE_DSP_EMU

static void DebugInfo_DspRegister(Uint32 arg)
{
	DebugInfo_CallCommand(DebugDsp_Register, "dspreg", arg);
}
static void DebugInfo_DspDisAsm(Uint32 arg)
{
	DebugInfo_CallCommand(DebugDsp_DisAsm, "dspdisasm", arg);
}

static void DebugInfo_DspMemDump(Uint32 arg)
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
	space = toupper(argv[0][0]);
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


static void DebugInfo_RegAddr(Uint32 arg)
{
	bool forDsp;
	char regname[3];
	Uint32 *regvalue, mask;
	char cmdbuf[12], addrbuf[6];
	char *argv[] = { cmdbuf, addrbuf };
	
	regname[0] = (arg>>24)&0xff;
	regname[1] = (arg>>16)&0xff;
	regname[2] = '\0';

	if (DebugCpu_GetRegisterAddress(regname, &regvalue)) {
		mask = 0xffffffff;
		forDsp = false;
	} else {
		if (!DSP_GetRegisterAddress(regname, &regvalue, &mask)) {
			fprintf(stderr, "ERROR: invalid address/data register '%s'!\n", regname);
			return;
		}
		forDsp = true;
	}
       	sprintf(addrbuf, "$%x", *regvalue & mask);

	if ((arg & 0xff) == 'D') {
		strcpy(cmdbuf, "disasm");
		if (forDsp) {
#if ENABLE_DSP_EMU
			DebugDsp_DisAsm(2, argv);
#endif
		} else {
			DebugCpu_DisAsm(2, argv);
		}
	} else {
		strcpy(cmdbuf, "memdump");
		if (forDsp) {
#if ENABLE_DSP_EMU
			DebugDsp_MemDump(2, argv);
#endif
		} else {
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
	     (toupper(argv[1][0]) != 'R' || !isdigit(argv[1][1]) || argv[1][2]))) {
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

static const struct {
	/* whether callback is used only for locking */
	bool lock;
	const char *name;
	void (*func)(Uint32 arg);
	/* convert args in argv into single Uint32 for func */
	Uint32 (*args)(int argc, char *argv[]);
	const char *info;
} infotable[] = {
	{ false,"basepage",  DebugInfo_Basepage,   NULL, "Show program basepage info at given <address>" },
	{ false,"crossbar",  DebugInfo_Crossbar,   NULL, "Show Falcon crossbar HW register values" },
	{ true, "default",   DebugInfo_Default,    NULL, "Show default debugger entry information" },
	{ true, "disasm",    DebugInfo_CpuDisAsm,  NULL, "Disasm CPU from PC or given <address>" },
#if ENABLE_DSP_EMU
	{ true, "dspdisasm", DebugInfo_DspDisAsm,  NULL, "Disasm DSP from given <address>" },
	{ true, "dspmemdump",DebugInfo_DspMemDump, DebugInfo_DspMemArgs, "Dump DSP memory from given <space> <address>" },
	{ true, "dspregs",   DebugInfo_DspRegister,NULL, "Show DSP register values" },
#endif
	{ true, "memdump",   DebugInfo_CpuMemDump, NULL, "Dump CPU memory from given <address>" },
	{ false,"osheader",  DebugInfo_OSHeader,   NULL, "Show TOS OS header information" },
	{ true, "regaddr",   DebugInfo_RegAddr, DebugInfo_RegAddrArgs, "Show <disasm|memdump> from CPU/DSP address pointed by <register>" },
	{ true, "registers", DebugInfo_CpuRegister,NULL, "Show CPU register values" },
	{ false,"videl",     DebugInfo_Videl,      NULL, "Show Falcon Videl HW register values" }
};

static int LockedFunction = 2; /* index for the "default" function */
static Uint32 LockedArgument;

/**
 * Show selected debugger session information
 * (when debugger is (again) entered)
 */
void DebugInfo_ShowSessionInfo(void)
{
	infotable[LockedFunction].func(LockedArgument);
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

	if (infotable[sub].args) {
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
		infotable[sub].func(value);
	}
	return DEBUGGER_CMDDONE;
}
