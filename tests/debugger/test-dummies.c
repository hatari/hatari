/* 
 * Dummy stuff needed to compile debugger related test code
 */

/* fake tracing flags */
#include "log.h"
Uint64 LogTraceFlags = 0;

/* fake Hatari configuration variables for number parsing */
#include "configuration.h"
CNF_PARAMS ConfigureParams;

/* fake cycles stuff */
#include "cycles.h"
int CurrentInstrCycles;
int Cycles_GetCounter(int nId) { return 0; }

/* fake ST RAM */
#include "stMemory.h"
Uint8 STRam[16*1024*1024];
Uint32 STRamEnd = 4*1024*1024;

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
void DebugUI_PrintCmdHelp(const char *psCmd) { }
char *DebugUI_MatchHelper(const char **strings, int items, const char *text, int state) {
	return NULL;
}

/* fake debugInfo.c stuff */
#include "debugInfo.h"
void DebugInfo_ShowSessionInfo(void) {}
Uint32 DebugInfo_GetTEXT(void) { return 0x1234; }
Uint32 DebugInfo_GetTEXTEnd(void) { return 0x1234; }
Uint32 DebugInfo_GetDATA(void) { return 0x12f4; }
Uint32 DebugInfo_GetBSS(void)  { return 0x1f34; }

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

/* fake gemdos stuff */
#include "gemdos.h"
const char *GemDOS_GetLastProgramPath(void) { return NULL; }

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
