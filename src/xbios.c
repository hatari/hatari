/*
  Hatari - xbios.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  XBios Handler (Trap #14)

  We intercept and direct some XBios calls to handle the RS-232 etc. and help
  with floppy debugging.
*/
const char XBios_fileid[] = "Hatari xbios.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "control.h"
#include "floppy.h"
#include "log.h"
#include "m68000.h"
#include "rs232.h"
#include "screenSnapShot.h"
#include "stMemory.h"
#include "xbios.h"


#define HATARI_CONTROL_OPCODE 255

#define XBIOS_DEBUG 0	/* for floppy read/write & rsconf */


/* List of Atari ST RS-232 baud rates */
static const int BaudRates[] =
{
	19200, /* 0 */
	9600,  /* 1 */
	4800,  /* 2 */
	3600,  /* 3 */
	2400,  /* 4 */
	2000,  /* 5 */
	1800,  /* 6 */
	1200,  /* 7 */
	600,   /* 8 */
	300,   /* 9 */
	200,   /* 10 */
	150,   /* 11 */
	134,   /* 12 */
	110,   /* 13 */
	75,    /* 14 */
	50     /* 15 */
};


/**
 * XBIOS Floppy Read
 * Call 8
 */
static bool XBios_Floprd(Uint32 Params)
{
#if XBIOS_DEBUG
	Uint32 pBuffer;
	Uint16 Dev,Sector,Side,Track,Count;

	/* Read details from stack */
	pBuffer = STMemory_ReadLong(Params+SIZE_WORD);
	Dev = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG);
	Sector = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG+SIZE_WORD);
	Track = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG+SIZE_WORD+SIZE_WORD);
	Side = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG+SIZE_WORD+SIZE_WORD+SIZE_WORD);
	Count = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD);

	LOG_TRACE(TRACE_OS_XBIOS, "XBIOS   8 Floprd %s,0x%x,%d,%d,%d,%d at addr 0x%X\n",
		  EmulationDrives[Dev].sFileName, pBuffer,
		  Side, Track, Sector, Count, M68000_GetPC());
#else
	LOG_TRACE(TRACE_OS_XBIOS, "XBIOS   8 (Floprd)\n");
#endif

	return false;
}


/**
 * XBIOS Floppy Write
 * Call 9
 */
static bool XBios_Flopwr(Uint32 Params)
{
#if XBIOS_DEBUG
	Uint32 pBuffer;
	Uint16 Dev,Sector,Side,Track,Count;

	/* Read details from stack */
	pBuffer = STMemory_ReadLong(Params+SIZE_WORD);
	Dev = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG);
	Sector = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG+SIZE_WORD);
	Track = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG+SIZE_WORD+SIZE_WORD);
	Side = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG+SIZE_WORD+SIZE_WORD+SIZE_WORD);
	Count = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD);

	LOG_TRACE(TRACE_OS_XBIOS, "XBIOS   9 Flopwr %s,0x%x,%d,%d,%d,%d at addr 0x%X\n",
		  EmulationDrives[Dev].sFileName, pBuffer,
		  Side, Track, Sector, Count, M68000_GetPC());
#else
	LOG_TRACE(TRACE_OS_XBIOS, "XBIOS   9 (Flopwr)\n");
#endif

	return false;
}


/**
 * XBIOS RsConf
 * Call 15
 */
static bool XBios_Rsconf(Uint32 Params)
{
	Sint16 Baud,Ctrl,Ucr;

	Baud = STMemory_ReadWord(Params+SIZE_WORD);
	Ctrl = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD);
	Ucr = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_WORD);
#if XBIOS_DEBUG
	Sint16 Rsr,Tsr,Scr;
	Rsr = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD);
	Tsr = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD);
	Scr = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD);
	Log_Printf(LOG_DEBUG, "XBIOS 15  Rsconf(baud,ctrl,ucr,rsr,tsr,scr: %d,%d,%d,%d,%d,%d)\n",
		   Baud, Ctrl, Ucr, Rsr, Tsr, Scr);
#else
	LOG_TRACE(TRACE_OS_XBIOS, "XBIOS  15 Rsconf(baud,ctrl,ucr...: %d,%d,%d...)\n", Baud, Ctrl, Ucr);
#endif
	/* Set baud rate and other configuration, if RS232 emaulation is enabled */
	if (ConfigureParams.RS232.bEnableRS232)
	{
		if (Baud >= 0 && Baud < ARRAYSIZE(BaudRates))
		{
			/* Convert ST baud rate index to value */
			int BaudRate = BaudRates[Baud];
			/* And set new baud rate: */
			RS232_SetBaudRate(BaudRate);
		}

		if (Ucr != -1)
		{
			RS232_HandleUCR(Ucr);
		}

		if (Ctrl != -1)
		{
			RS232_SetFlowControl(Ctrl);
		}

		return true;
	}

	return false;
}


/**
 * XBIOS Scrdmp
 * Call 20
 */
static bool XBios_Scrdmp(Uint32 Params)
{
	LOG_TRACE(TRACE_OS_XBIOS, "XBIOS  20 Scrdmp()\n");
	ScreenSnapShot_SaveScreen();

	/* Correct return code? */
	Regs[REG_D0] = 0;

	return true;
}


/**
 * XBIOS remote control interface for Hatari
 * Call 255
 */
static bool XBios_HatariControl(Uint32 Params)
{
	char *pText;
	pText = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
	LOG_TRACE(TRACE_OS_XBIOS, "XBIOS %3d HatariControl(%s)\n", HATARI_CONTROL_OPCODE, pText);
	Control_ProcessBuffer(pText);
	Regs[REG_D0] = 0;
	return true;
}


#if ENABLE_TRACING
/**
 * Map XBIOS call opcode to XBIOS function name
 */
static const char* XBios_Call2Name(Uint16 opcode)
{
	static const char* names[] = {
		"Initmous",
		"Ssbrk",
		"Physbase",
		"Logbase",
		"Getrez",
		"Setscreen",
		"Setpalette",
		"Setcolor",
		"Floprd",
		"Flopwr",
		"Flopfmt",
		"Dbmsg",
		"Midiws",
		"Mfpint",
		"Iorec",
		"Rsconf",
		"Keytbl",
		"Random",
		"Protobt",
		"Flopver",
		"Scrdmp",
		"Cursconf",
		"Settime",
		"Gettime",
		"Bioskeys",
		"Ikbdws",
		"Jdisint",
		"Jenabint",
		"Giaccess",
		"Offgibit",
		"Ongibit",
		"Xbtimer",
		"Dosound",
		"Setprt",
		"Kbdvbase",
		"Kbrate",
		"Prtblk",
		"Vsync",
		"Supexec",
		"Puntaes",
		NULL,	/* 40 */
		"Floprate",
		"DMAread",
		"DMAwrite",
		"Bconmap",
		NULL,	/* 45 */
		"NVMaccess",
		NULL,	/* 47 */
		"Metainit",
		NULL,	/* 49 */
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,	/* 63 */
		"Blitmode",
		NULL,	/* 65 */
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,	/* 79 */
		"EsetShift",
		"EgetShift",
		"EsetBank",
		"EsetColor",
		"EsetPalette",
		"EgetPalette",
		"EsetGray",
		"EsetSmear",
		"VsetMode",
		"VgetMonitor",
		"VsetSync",
		"VgetSize",
		NULL,	/* 92 */
		"VsetRGB",
		"VgetRGB",
		NULL,	/* 95 */
		"Dsp_DoBlock",
		"Dsp_BlkHandShake",
		"Dsp_BlkUnpacked",
		"Dsp_InStream",
		"Dsp_OutStream",
		"Dsp_IOStream",
		"Dsp_RemoveInterrupts",
		"Dsp_GetWordSize",
		"Dsp_Lock",
		"Dsp_Unlock",
		"Dsp_Available",
		"Dsp_Reserve",
		"Dsp_LoadProg",
		"Dsp_ExecProg",
		"Dsp_ExecBoot",
		"Dsp_LodToBinary",
		"Dsp_TriggerHC",
		"Dsp_RequestUniqueAbility",
		"Dsp_GetProgAbility",
		"Dsp_FlushSubroutines",
		"Dsp_LoadSubroutine",
		"Dsp_InqSubrAbility",
		"Dsp_RunSubroutine",
		"Dsp_Hf0",
		"Dsp_Hf1",
		"Dsp_Hf2",
		"Dsp_Hf3",
		"Dsp_BlkWords",
		"Dsp_BlkBytes",
		"Dsp_HStat",
		"Dsp_SetVectors",
		"Dsp_MultBlocks",
		"Locksnd",
		"Unlocksnd",
		"Soundcmd",
		"Setbuffer",
		"Setmode",
		"Settracks",
		"Setmontracks",
		"Setinterrupt",
		"Buffoper",
		"Dsptristate",
		"Gpio",
		"Devconnect",
		"Sndstatus",
		"Buffptr",
		NULL,	/* 142 */
		NULL,
		NULL,
		NULL,
		"VsetMask",
		NULL,	/* 147 */
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,	/* 164 */
		"WavePlay"
	};
	if (opcode < ARRAYSIZE(names) && names[opcode]) {
		return names[opcode];
	}
	return "???";
}
#endif


/**
 * Check if we need to re-direct XBios call to our own routines
 */
bool XBios(void)
{
	Uint32 Params;
	Uint16 XBiosCall;

	/* Find call */
	Params = Regs[REG_A7];
	XBiosCall = STMemory_ReadWord(Params);

	switch (XBiosCall)
	{
	case 8:
		return XBios_Floprd(Params);
	case 9:
		return XBios_Flopwr(Params);
	case 15:
		return XBios_Rsconf(Params);
	case 20:
		return XBios_Scrdmp(Params);
	
	case 1:		/* Ssbrk */
	case 14:	/* Iorec */
	case 26:	/* Jdisint */
	case 27:	/* Jenabint */
	case 29:	/* Offgibit */
	case 30:	/* Ongibit */
	case 33:	/* Setprt */
	case 44:	/* Bconmap */
	case 64:	/* Blitmode */
	case 80:	/* EsetShift */
	case 82:	/* EsetBank */
	case 86:	/* EsetGray */
	case 87:	/* EsetSmear */
	case 88:	/* VsetMode */
	case 90:	/* VsetSync */
	case 91:	/* VgetSize */
	case 102:	/* Dsp_RemoveInterrupts */
	case 112:	/* Dsp_TriggerHC */
	case 117:	/* Dsp_InqSubrAbility */
	case 118:	/* Dsp_RunSubroutine */
	case 119:	/* Dsp_Hf0 */
	case 120:	/* Dsp_Hf1 */
	case 132:	/* Setmode */
	case 134:	/* Setmontracks */
	case 136:	/* Buffoper */
	case 140:	/* Sndstatus */
		/* print arg for calls taking single word */
		LOG_TRACE(TRACE_OS_XBIOS, "XBIOS %3hd %s(0x%02x)\n",
			  XBiosCall, XBios_Call2Name(XBiosCall),
			  STMemory_ReadWord(Params+SIZE_WORD));
		return false;

	case HATARI_CONTROL_OPCODE:
		return XBios_HatariControl(Params);

	default:  /* Call as normal! */
		LOG_TRACE(TRACE_OS_XBIOS, "XBIOS %3hd (%s)\n",
			  XBiosCall, XBios_Call2Name(XBiosCall));
		return false;
	}
}
