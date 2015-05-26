/*
  Hatari - bios.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Bios Handler (Trap #13)

  Intercept some Bios calls for tracing/debugging
*/
const char Bios__fileid[] = "Hatari bios.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "floppy.h"
#include "log.h"
#include "m68000.h"
#include "printer.h"
#include "rs232.h"
#include "stMemory.h"
#include "bios.h"


/*-----------------------------------------------------------------------*/
/**
 * BIOS Read/Write disk sector
 * Call 4
 */
static void Bios_RWabs(Uint32 Params)
{
#if ENABLE_TRACING
	Uint32 pBuffer;
	Uint16 RWFlag, Number, RecNo, Dev;

	/* Read details from stack */
	RWFlag = STMemory_ReadWord(Params);
	pBuffer = STMemory_ReadLong(Params+SIZE_WORD);
	Number = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG);
	RecNo = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_WORD);
	Dev = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_WORD+SIZE_WORD);

	LOG_TRACE(TRACE_OS_BIOS, "BIOS 0x04 Rwabs(%d,0x%x,%d,%d,%i) at PC 0x%X\n",
	          RWFlag, pBuffer, Number, RecNo, Dev,
		  M68000_GetPC());
#endif
}

/*-----------------------------------------------------------------------*/
/**
 * BIOS Set/query exception vectors
 * Call 5
 */
static void Bios_Setexe(Uint32 Params)
{
#if ENABLE_TRACING
	Uint16 vec = STMemory_ReadWord(Params);
	Uint32 addr = STMemory_ReadLong(Params+SIZE_WORD);
	struct {
		int vec;
		const char *name;
	} *vecname, vecnames[] =
	{
		{ 0x002, "BUSERROR" },
		{ 0x003, "ADDRESSERROR" },
		{ 0x004, "ILLEGALINSTRUCTION" },
		{ 0x021, "GEMDOS" },
		{ 0x022, "GEM" },
		{ 0x02D, "BIOS" },
		{ 0x02E, "XBIOS" },
		{ 0x100, "TIMER" },
		{ 0x101, "CRITICALERROR" },
		{ 0x102, "TERMINATE" },
		{ 0x000, "???" }
	};
	for (vecname = &(vecnames[0]); vecname->vec && vec != vecname->vec; vecname++)
		;
	LOG_TRACE(TRACE_OS_BIOS, "BIOS 0x05 Setexc(0x%hX VEC_%s, 0x%X) at PC 0x%X\n", vec, vecname->name, addr,
		  M68000_GetPC());
#endif
}


/*-----------------------------------------------------------------------*/

#if ENABLE_TRACING
/**
 * Map BIOS call opcode to BIOS function name
 */
static const char* Bios_Call2Name(Uint16 opcode)
{
	/* GCC uses substrings from above trace statements
	 * where they match, so having them again here
	 * wastes only a pointer & simplifies things
	 */
	static const char* names[] = {
		"Getmpb", "Bconstat","Bconin", "Bconout",
		"Rwabs",  "Setexc",  "Tickcal","Getbpb",
		"Bcostat","Mediach", "Drvmap", "Kbshift"
	};
	if (opcode < ARRAYSIZE(names) && names[opcode]) {
		return names[opcode];
	}
	return "???";
}

void Bios_Info(FILE *fp, Uint32 dummy)
{
	Uint16 opcode;
	for (opcode = 0; opcode <= 0xB; ) {
		fprintf(fp, "%02x %-9s", opcode,
			Bios_Call2Name(opcode));
		if (++opcode % 6 == 0) {
			fputs("\n", fp);
		}
	}
}
#else /* !ENABLE_TRACING */
void Bios_Info(FILE *fp, Uint32 bShowOpcodes)
{
	        fputs("Hatari isn't configured with ENABLE_TRACING\n", fp);
}
#endif /* !ENABLE_TRACING */


/*-----------------------------------------------------------------------*/
/**
 * Check Bios call and see if we need to re-direct to our own routines.
 * Return true if we've handled the exception, else return false to let
 * TOS attempt it
 */
bool Bios(void)
{
	Uint32 Params;
	Uint16 BiosCall;

	/* Get call */
	Params = Regs[REG_A7];
	BiosCall = STMemory_ReadWord(Params);
	Params += SIZE_WORD;

	/* Intercept? */
	switch(BiosCall)
	{
	case 0x0:
		LOG_TRACE(TRACE_OS_BIOS, "BIOS 0x00 Getmpb(0x%X) at PC 0x%X\n",
			  STMemory_ReadLong(Params),
			  M68000_GetPC());
		break;

	case 0x3:
		LOG_TRACE(TRACE_OS_BIOS, "BIOS 0x03 Bconout(%i, 0x%02hX) at PC 0x%X\n",
			  STMemory_ReadWord(Params),
			  STMemory_ReadWord(Params+SIZE_WORD),
			  M68000_GetPC());
		break;

	case 0x4:
		Bios_RWabs(Params);
		break;

	case 0x5:
		Bios_Setexe(Params);
		break;

	case 0x1:
	case 0x2:
	case 0x7:
	case 0x8:
	case 0x9:
	case 0xB:
		/* commands taking a single word */
		LOG_TRACE(TRACE_OS_BIOS, "BIOS 0x%02hX %s(0x%hX) at PC 0x%X\n",
			  BiosCall, Bios_Call2Name(BiosCall),
			  STMemory_ReadWord(Params),
			  M68000_GetPC());
		break;

	case 0x6:
	case 0xA:
		/* commands taking no args */
		LOG_TRACE(TRACE_OS_BIOS, "BIOS 0x%02hX %s() at PC 0x%X\n",
			  BiosCall, Bios_Call2Name(BiosCall),
			  M68000_GetPC());
		break;

	default:
		Log_Printf(LOG_WARN, "Unknown BIOS call 0x%x! at PC 0x%X\n", BiosCall,
			   M68000_GetPC());
		break;
	}
	return false;
}
