/*
  Hatari - xbios.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  XBios Handler (Trap #14)

  Intercept and direct some XBios calls to handle the RS-232 etc.
  and to help with tracing/debugging.
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

/* whether to enable XBios(20/255) */
static bool bXBiosCommands;

void XBios_ToggleCommands(void)
{
	if (bXBiosCommands)
	{
		fprintf(stderr, "XBios 15/20/255 parsing disabled.\n");
		bXBiosCommands = false;
	}
	else
	{
		fprintf(stderr, "XBios 15/20/255 parsing enabled.\n");
		bXBiosCommands = true;
	}
}

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


#if ENABLE_TRACING
/**
 * XBIOS Floppy Read
 * Call 8
 */
static bool XBios_Floprd(Uint32 Params)
{
	Uint32 pBuffer;
	Uint16 Dev,Sector,Side,Track,Count;

	/* Read details from stack */
	pBuffer = STMemory_ReadLong(Params);
	Dev = STMemory_ReadWord(Params+SIZE_LONG+SIZE_LONG); /* skip reserved long */
	Sector = STMemory_ReadWord(Params+SIZE_LONG+SIZE_LONG+SIZE_WORD);
	Track = STMemory_ReadWord(Params+SIZE_LONG+SIZE_LONG+SIZE_WORD+SIZE_WORD);
	Side = STMemory_ReadWord(Params+SIZE_LONG+SIZE_LONG+SIZE_WORD+SIZE_WORD+SIZE_WORD);
	Count = STMemory_ReadWord(Params+SIZE_LONG+SIZE_LONG+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD);

	LOG_TRACE(TRACE_OS_XBIOS, "XBIOS 0x08 Floprd(0x%x, %d, %d, %d, %d, %d) at PC 0x%X for: %s\n",
		  pBuffer, Dev, Sector, Track, Side, Count, M68000_GetPC(),
		  Dev < MAX_FLOPPYDRIVES ? EmulationDrives[Dev].sFileName : "n/a");
	return false;
}


/**
 * XBIOS Floppy Write
 * Call 9
 */
static bool XBios_Flopwr(Uint32 Params)
{
	Uint32 pBuffer;
	Uint16 Dev,Sector,Side,Track,Count;

	/* Read details from stack */
	pBuffer = STMemory_ReadLong(Params);
	Dev = STMemory_ReadWord(Params+SIZE_LONG+SIZE_LONG); /* skip reserved long */
	Sector = STMemory_ReadWord(Params+SIZE_LONG+SIZE_LONG+SIZE_WORD);
	Track = STMemory_ReadWord(Params+SIZE_LONG+SIZE_LONG+SIZE_WORD+SIZE_WORD);
	Side = STMemory_ReadWord(Params+SIZE_LONG+SIZE_LONG+SIZE_WORD+SIZE_WORD+SIZE_WORD);
	Count = STMemory_ReadWord(Params+SIZE_LONG+SIZE_LONG+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD);

	LOG_TRACE(TRACE_OS_XBIOS, "XBIOS 0x09 Flopwr(0x%x, %d, %d, %d, %d, %d) at PC 0x%X for: %s\n",
		  pBuffer, Dev, Sector, Track, Side, Count, M68000_GetPC(),
		  Dev < MAX_FLOPPYDRIVES ? EmulationDrives[Dev].sFileName : "n/a");
	return false;
}


/**
 * XBIOS Devconnect
 * Call 139
 */
static bool XBios_Devconnect(Uint32 Params)
{
	Uint16 src,dst,clk,prescale,protocol;

	/* Read details from stack */
	src = STMemory_ReadWord(Params);
	dst = STMemory_ReadWord(Params+SIZE_WORD);
	clk = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD);
	prescale = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_WORD);
	protocol = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD);

	LOG_TRACE(TRACE_OS_XBIOS, "XBIOS 0x8B Devconnect(%hd, 0x%hx, %hd, %hd, %hd) at PC 0x%X\n",
		  src, dst, clk, prescale, protocol ,
		  M68000_GetPC() );
	return false;
}

#else /* !ENABLE_TRACING */
#define XBios_Floprd(params)     false
#define XBios_Flopwr(params)     false
#define XBios_Devconnect(params) false
#endif


/**
 * XBIOS RsConf
 * Call 15
 */
static bool XBios_Rsconf(Uint32 Params)
{
	Sint16 Baud,Ctrl,Ucr;
#if ENABLE_TRACING
	Sint16 Rsr,Tsr,Scr;
#endif

	Baud = STMemory_ReadWord(Params);
	Ctrl = STMemory_ReadWord(Params+SIZE_WORD);
	Ucr = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD);
#if ENABLE_TRACING
	Rsr = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_WORD);
	Tsr = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD);
	Scr = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD);
	LOG_TRACE(TRACE_OS_XBIOS, "XBIOS 0x0F Rsconf(%d, %d, %d, %d, %d, %d) at PC 0x%X\n",
		   Baud, Ctrl, Ucr, Rsr, Tsr, Scr,
		   M68000_GetPC());
#endif
	if (!bXBiosCommands)
		return false;
	
	if (!ConfigureParams.RS232.bEnableRS232)
		return false;

	/* Set baud rate and other configuration, if RS232 emaulation is enabled */
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


/**
 * XBIOS Scrdmp
 * Call 20
 */
static bool XBios_Scrdmp(Uint32 Params)
{
	LOG_TRACE(TRACE_OS_XBIOS, "XBIOS 0x14 Scrdmp() at PC 0x%X\n" , M68000_GetPC());

	if (!bXBiosCommands)
		return false;

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
	const char *pText;
	pText = (const char *)STMemory_STAddrToPointer(STMemory_ReadLong(Params));
	LOG_TRACE(TRACE_OS_XBIOS, "XBIOS 0x%02X HatariControl(%s) at PC 0x%X\n", HATARI_CONTROL_OPCODE, pText, M68000_GetPC());

	if (!bXBiosCommands)
		return false;

	Control_ProcessBuffer(pText);
	Regs[REG_D0] = 0;
	return true;
}


#if ENABLE_TRACING
/**
 * Map XBIOS call opcode to XBIOS function name
 *
 * Mapping is based on TOSHYP information:
 * 	http://toshyp.atari.org/en/004014.html
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
		"VsetVars",	/* TOS4 internal */
		"VsetRGB",
		"VgetRGB",
		"VcheckMode",	/* TOS4 internal (ValidMode()) */
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
		NULL,
		NULL,
		NULL,
		NULL,	/* 149 */
		"VsetMask",
		NULL,	/* 151 */
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

void XBios_Info(FILE *fp, Uint32 dummy)
{
	Uint16 opcode;
	for (opcode = 0; opcode < 168; ) {
		fprintf(fp, "%02x %-21s", opcode,
			XBios_Call2Name(opcode));
		if (++opcode % 3 == 0) {
			fputs("\n", fp);
		}
	}
}
#else /* !ENABLE_TRACING */
void XBios_Info(FILE *fp, Uint32 bShowOpcodes)
{
	        fputs("Hatari isn't configured with ENABLE_TRACING\n", fp);
}
#endif /* !ENABLE_TRACING */


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
	Params += SIZE_WORD;

	switch (XBiosCall)
	{
		/* commands with special handling */
	case 8:
		return XBios_Floprd(Params);
	case 9:
		return XBios_Flopwr(Params);
	case 15:
		return XBios_Rsconf(Params);
	case 20:
		return XBios_Scrdmp(Params);
	case 139:
		return XBios_Devconnect(Params);
	case HATARI_CONTROL_OPCODE:
		return XBios_HatariControl(Params);

	case 2:		/* Physbase */
	case 3:		/* Logbase */
	case 4:		/* Getrez */
	case 17:	/* Random */
	case 23:	/* Gettime */
	case 24:	/* Bioskeys */
	case 34:	/* Kbdvbase */
	case 37:	/* Vsync */
	case 39:	/* Puntaes */
	case 81:	/* EgetShift */
	case 89:	/* VgetMonitor */
	case 103:	/* Dsp_GetWordSize */
	case 104:	/* Dsp_Lock */
	case 105:	/* Dsp_Unlock */
	case 113:	/* Dsp_RequestUniqueAbility */
	case 114:	/* Dsp_GetProgAbility */
	case 115:	/* Dsp_FlushSubroutines */
	case 121:	/* Dsp_Hf2 */
	case 122:	/* Dsp_Hf3 */
	case 125:	/* Dsp_Hstat */
	case 128:	/* Locksnd */
	case 129:	/* Unlocksnd */
		/* commands with no args */
		LOG_TRACE(TRACE_OS_XBIOS, "XBIOS 0x%02hX %s() at PC 0x%X\n",
			  XBiosCall, XBios_Call2Name(XBiosCall),
			  M68000_GetPC());
		return false;
		
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
		/* ones taking single word */
		LOG_TRACE(TRACE_OS_XBIOS, "XBIOS 0x%02hX %s(0x%hX) at PC 0x%X\n",
			  XBiosCall, XBios_Call2Name(XBiosCall),
			  STMemory_ReadWord(Params),
			  M68000_GetPC());
		return false;

	case 6:		/* Setpalette */
	case 22:	/* Settime */
	case 32:	/* Dosound */
	case 36:	/* Ptrblt */
	case 38:	/* Supexec */
	case 48:	/* Metainit */
	case 141:	/* Buffptr */
		/* ones taking long or pointer */
		LOG_TRACE(TRACE_OS_XBIOS, "XBIOS 0x%02hX %s(0x%X) at PC 0x%X\n",
			  XBiosCall, XBios_Call2Name(XBiosCall),
			  STMemory_ReadLong(Params),
			  M68000_GetPC());
		return false;

	case 7:		/* Setcolor */
	case 21:	/* Cursconf */
	case 28:	/* Giaccess */
	case 35:	/* Kbrate */
	case 41:	/* Floprate */
	case 83:	/* EsetColor */
	case 130:	/* Soundcmd */
	case 133:	/* Settracks */
	case 137:	/* Dsptristate */
	case 135:	/* Setinterrupt */
	case 138:	/* Gpio */
		/* ones taking two words */
		LOG_TRACE(TRACE_OS_XBIOS, "XBIOS 0x%02hX %s(0x%hX, 0x%hX) at PC 0x%X\n",
			  XBiosCall, XBios_Call2Name(XBiosCall),
			  STMemory_ReadWord(Params),
			  STMemory_ReadWord(Params+SIZE_WORD),
			  M68000_GetPC());
		return false;

	case 12:	/* Midiws */
	case 13:	/* Mfpint */
	case 25:	/* Ikbdws */
		/* ones taking word length/index and pointer */
		LOG_TRACE(TRACE_OS_XBIOS, "XBIOS 0x%02hX %s(%hd, 0x%X) at PC 0x %X\n",
			  XBiosCall, XBios_Call2Name(XBiosCall),
			  STMemory_ReadWord(Params),
			  STMemory_ReadLong(Params+SIZE_WORD),
			  M68000_GetPC());
		return false;

	case 11:	/* Dbmsg */
	case 84:	/* EsetPalette */
	case 85:	/* EgetPalette */
	case 93:	/* VsetRGB */
	case 94:	/* VgetRGB */
		/* ones taking word, word and long/pointer */
		LOG_TRACE(TRACE_OS_XBIOS, "XBIOS 0x%02hX %s(0x%hX, 0x%hX, 0x%X) at PC 0x%X\n",
			  XBiosCall, XBios_Call2Name(XBiosCall),
			  STMemory_ReadWord(Params),
			  STMemory_ReadWord(Params+SIZE_WORD),
			  STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD),
			  M68000_GetPC());
		return false;

	case 106:	/* Dsp_Available */
	case 107:	/* Dsp_Reserve */
	case 111:	/* Dsp_LodToBinary */
	case 126:	/* Dsp_SetVectors */
		/* ones taking two longs/pointers */
		LOG_TRACE(TRACE_OS_XBIOS, "XBIOS 0x%02hX %s(0x%X, 0x%X) at PC 0x%X\n",
			  XBiosCall, XBios_Call2Name(XBiosCall),
			  STMemory_ReadLong(Params),
			  STMemory_ReadLong(Params+SIZE_LONG),
			  M68000_GetPC());
		return false;

	case 5:		/* Setscreen */
		if (STMemory_ReadWord(Params+SIZE_LONG+SIZE_LONG) == 3) {
			/* actually VSetscreen with extra parameter */
			LOG_TRACE(TRACE_OS_XBIOS, "XBIOS 0x%02hX VsetScreen(0x%X, 0x%X, 3, 0x%hX) at PC 0x%X\n",
				  XBiosCall, STMemory_ReadLong(Params),
				  STMemory_ReadLong(Params+SIZE_LONG),
				  STMemory_ReadWord(Params+SIZE_LONG+SIZE_LONG+SIZE_WORD),
				  M68000_GetPC());
			return false;			
		}
	case 109:	/* Dsp_ExecProg */
	case 110:	/* Dsp_ExecBoot */
	case 116:	/* Dsp_LoadSubroutine */
	case 150:	/* VsetMask */
		/* ones taking two longs/pointers and a word */
		LOG_TRACE(TRACE_OS_XBIOS, "XBIOS 0x%02hX %s(0x%X, 0x%X, 0x%hX) at PC 0x%X\n",
			  XBiosCall, XBios_Call2Name(XBiosCall),
			  STMemory_ReadLong(Params),
			  STMemory_ReadLong(Params+SIZE_LONG),
			  STMemory_ReadWord(Params+SIZE_LONG+SIZE_LONG),
			  M68000_GetPC());
		return false;

	default:  /* rest of XBios calls */
		LOG_TRACE(TRACE_OS_XBIOS, "XBIOS 0x%02hX (%s)\n",
			  XBiosCall, XBios_Call2Name(XBiosCall));
		return false;
	}
}
