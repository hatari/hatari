/* 
 * Dummy stuff needed to compile debugger related test code
 */

/* fake tracing flags */
#include "log.h"
Uint64 LogTraceFlags = 0;
FILE *TraceFile;

/* fake Hatari configuration variables for number parsing */
#include "configuration.h"
CNF_PARAMS ConfigureParams;

/* fake options.c */
#include "options.h"
bool Opt_IsAtariProgram(const char *path) { return false; }

/* fake cycles stuff */
#include "cycles.h"
int CurrentInstrCycles;
int Cycles_GetCounter(int nId) { return 0; }

/* bring in gemdos defines (EMULATEDDRIVES) */
#include "gemdos.h"

/* fake ST RAM, only 24-bit support */
#include "stMemory.h"
Uint8 STRam[16*1024*1024];
Uint32 STRamEnd = 4*1024*1024;
Uint32 STMemory_ReadLong(Uint32 addr) {
	Uint32 val;
	if (addr >= STRamEnd) return 0;
	val = (STRam[addr] << 24) | (STRam[addr+1] << 16) | (STRam[addr+2] << 8) | STRam[addr+3];
	return val;
}
Uint16 STMemory_ReadWord(Uint32 addr) {
	Uint16 val;
	if (addr >= STRamEnd) return 0;
	val = (STRam[addr] << 8) | STRam[addr+1];
	return val;
}
Uint8 STMemory_ReadByte(Uint32 addr) {
	if (addr >= STRamEnd) return 0;
	return STRam[addr];
}
void STMemory_WriteByte(Uint32 addr, Uint8 val) {
	if (addr < STRamEnd)
		STRam[addr] = val;
}
void STMemory_WriteWord(Uint32 addr, Uint16 val) {
	if (addr < STRamEnd) {
		STRam[addr+0] = val >> 8;
		STRam[addr+1] = val & 0xff;
	}
}
void STMemory_WriteLong(Uint32 addr, Uint32 val) {
	if (addr < STRamEnd) {
		STRam[addr+0] = val >> 24;
		STRam[addr+1] = val >> 16 & 0xff;
		STRam[addr+2] = val >> 8 & 0xff;
		STRam[addr+3] = val & 0xff;
	}
}
bool STMemory_CheckAreaType(Uint32 addr, int size, int mem_type ) {
	if ((addr > STRamEnd && addr < 0xe00000) ||
	    (addr >= 0xff0000 && addr < 0xff8000)) {
		return false;
	}
	return true;
}

/* fake memory banks */
#include "memory.h"
addrbank *mem_banks[65536];

/* fake IO memory variables */
#include "ioMem.h"
int nIoMemAccessSize;
Uint32 IoAccessBaseAddress;

/* fake CPU wrapper stuff */
#include "m68000.h"
int nWaitStateCycles;
cpu_instruction_t CpuInstruction;
void MakeFromSR(void) { }

/* fake UAE core registers */
#include "newcpu.h"
cpuop_func *cpufunctbl[65536];
struct regstruct regs;
void MakeSR(void) { }
void m68k_dumpstate (FILE *f, uaecptr *nextpc) { }
void m68k_disasm (FILE *f, uaecptr addr, uaecptr *nextpc, int cnt) { }

/* fake memory snapshot */
#include "memorySnapShot.h"
void MemorySnapShot_Store(void *pData, int Size) { }

/* fake TOS variables */
#include "tos.h"
Uint32 TosAddress, TosSize;

/* fake debugui.c stuff */
#include "debug_priv.h"
#include "debugui.h"
FILE *debugOutput;
void DebugUI(debug_reason_t reason) { }
int DebugUI_PrintCmdHelp(const char *psCmd) { return DEBUGGER_CMDDONE; }
char *DebugUI_MatchHelper(const char **strings, int items, const char *text, int state) {
	return NULL;
}

/* fake debugInfo.c stuff */
#include "debugInfo.h"
void DebugInfo_ShowSessionInfo(void) {}
Uint32 DebugInfo_GetBASEPAGE(void) { return 0x1f34; }
Uint32 DebugInfo_GetTEXT(void)     { return 0x1234; }
Uint32 DebugInfo_GetTEXTEnd(void)  { return 0x1234; }
Uint32 DebugInfo_GetDATA(void)     { return 0x12f4; }
Uint32 DebugInfo_GetBSS(void)      { return 0x1f34; }

/* fake debugdsp.c stuff */
#include "debugdsp.h"
void DebugDsp_InitSession(void) { }
Uint32 DebugDsp_InstrCount(void) { return 0; }
Uint32 DebugDsp_OpcodeType(void) { return 0; }

/* use fake dsp.c stuff in case config.h is configured with DSP emu */
#include "dsp.h"
bool bDspEnabled;
Uint16 DSP_DisasmAddress(FILE *f, Uint16 lowerAdr, Uint16 UpperAdr) { return 0; }
Uint16 DSP_GetInstrCycles(void) { return 0; }
Uint16 DSP_GetPC(void) { return 0; }
int DSP_GetRegisterAddress(const char *arg, Uint32 **addr, Uint32 *mask)
{
	*addr = NULL; /* find if this gets used */
	*mask = 0;
	return 0;
}
Uint32 DSP_ReadMemory(Uint16 addr, char space, const char **mem_str)
{
	*mem_str = NULL; /* find if this gets used */
	return 0;
}

/* fake console redirection */
#include "console.h"
int ConOutDevice;
void Console_Check(void) { }

/* fake profiler stuff */
#include "profile.h"
const char Profile_Description[] = "";
int Profile_Command(int nArgc, char *psArgs[], bool bForDsp) { return DEBUGGER_CMDDONE; }
char *Profile_Match(const char *text, int state) { return NULL; }
bool Profile_CpuStart(void) { return false; }
void Profile_CpuUpdate(void) { }
void Profile_CpuStop(void) { }

/* fake Hatari video variables */
#include "screen.h"
#include "video.h"
int nHBL = 20;
int nVBLs = 71;

/* fake video variables accessor */
void Video_GetPosition(int *pFrameCycles, int *pHBL, int *pLineCycles)
{
	*pFrameCycles = 2048;
	*pHBL = nHBL;
	*pFrameCycles = 508;
}

/* only function needed from file.c */
#include <sys/stat.h>
#include <sys/time.h>
#include "file.h"
bool File_Exists(const char *filename)
{
	struct stat buf;
	if (stat(filename, &buf) == 0 &&
	    (buf.st_mode & (S_IRUSR|S_IWUSR)) && !(buf.st_mode & S_IFDIR))
	{
		/* file points to user readable regular file */
		return true;
	}
	return false;
}

/* fake debugger file parsing */
#include "debugui.h"
bool DebugUI_ParseFile(const char *path, bool reinit)
{
	return File_Exists(path);
}

/* fake disassembly output */
#include "68kDisass.h"
Uint32 Disasm_GetNextPC(Uint32 pc) { return pc+2; }
void Disasm (FILE *f, uaecptr addr, uaecptr *nextpc, int count) {}
void Disasm_GetColumns(int *columns) {}
void Disasm_SetColumns(int *columns) {}
void Disasm_DisableColumn(int column, int *oldcols, int *newcols) {}
