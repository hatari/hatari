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
#include "debugInfo.h"
#include "debugui.h"
#include "ioMem.h"
#include "configuration.h"


/**
 * DebugInfo_Videl : display the Videl registers values.
 */
static void DebugInfo_Videl(void)
{
	if (ConfigureParams.System.nMachineType != MACHINE_FALCON) {
		fprintf(stderr, "No Videl\n");
		return;
	}

	fprintf(stderr, "$FF8006 : monitor type                     : %02x\n", IoMem_ReadByte(0xff8006));
	fprintf(stderr, "$FF820E : offset to next line              : %04x\n", IoMem_ReadWord(0xff820e));
	fprintf(stderr, "$FF8210 : VWRAP - line width               : %04x\n", IoMem_ReadWord(0xff8210));
	fprintf(stderr, "$FF8260 : ST shift mode                    : %02x\n", IoMem_ReadByte(0xff8260));
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
static void DebugInfo_Crossbar(void)
{
	if (ConfigureParams.System.nMachineType != MACHINE_FALCON) {
		fprintf(stderr, "No Crossbar\n");
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


static const struct {
	const char *name;
	void (*func)(void);
} infotable[] = {
	{ "crossbar", DebugInfo_Crossbar },
	{ "videl",    DebugInfo_Videl }
};


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
	int i;

	if (nArgc > 1) {
		cmd = psArgs[1];
		for (i = 0; i < ARRAYSIZE(infotable); i++) {
			if (strcmp(cmd, infotable[i].name) == 0) {
				infotable[i].func();
				return DEBUGGER_CMDDONE;
			}
		}
	}
	fprintf(stderr, "Info subcommands are:\n");
	for (i = 0; i < ARRAYSIZE(infotable); i++) {
		fprintf(stderr, "- %s\n", infotable[i].name);
	}
	return DEBUGGER_CMDDONE;
}
