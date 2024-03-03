/* 
 * Dummy stuff needed to compile debugger related test code
 */

/* fake tracing flags */
#include <stdio.h>
#include "log.h"

uint64_t LogTraceFlags = 0;
FILE *TraceFile;
void Log_Trace(const char *format, ...) { }
void Log_ResetMsgRepeat(void) {}

/* fake Hatari configuration variables for number parsing */
#include "configuration.h"
CNF_PARAMS ConfigureParams;

/* fake hatari-glue.c */
#include "hatari-glue.h"
struct uae_prefs currprefs;

/* fake options.c */
#include "options.h"
bool Opt_IsAtariProgram(const char *path) { return false; }

/* fake cycles stuff */
#include "cycles.h"
uint64_t CyclesGlobalClockCounter;
int Cycles_GetCounter(int nId) { return 0; }

/* bring in gemdos defines (EMULATEDDRIVES) */
#include "gemdos.h"

/* fake ST RAM, only 24-bit support */
#include "stMemory.h"
#if ENABLE_SMALL_MEM
static uint8_t _STRam[16*1024*1024];
uint8_t *STRam = _STRam;
#else
uint8_t STRam[16*1024*1024];
#endif
uint32_t STRamEnd = 4*1024*1024;
uint32_t STMemory_ReadLong(uint32_t addr) {
	uint32_t val;
	if (addr >= STRamEnd) return 0;
	val = (STRam[addr] << 24) | (STRam[addr+1] << 16) | (STRam[addr+2] << 8) | STRam[addr+3];
	return val;
}
uint16_t STMemory_ReadWord(uint32_t addr) {
	uint16_t val;
	if (addr >= STRamEnd) return 0;
	val = (STRam[addr] << 8) | STRam[addr+1];
	return val;
}
uint8_t STMemory_ReadByte(uint32_t addr) {
	if (addr >= STRamEnd) return 0;
	return STRam[addr];
}
void STMemory_WriteByte(uint32_t addr, uint8_t val) {
	if (addr < STRamEnd)
		STRam[addr] = val;
}
void STMemory_WriteWord(uint32_t addr, uint16_t val) {
	if (addr < STRamEnd) {
		STRam[addr+0] = val >> 8;
		STRam[addr+1] = val & 0xff;
	}
}
void STMemory_WriteLong(uint32_t addr, uint32_t val) {
	if (addr < STRamEnd) {
		STRam[addr+0] = val >> 24;
		STRam[addr+1] = val >> 16 & 0xff;
		STRam[addr+2] = val >> 8 & 0xff;
		STRam[addr+3] = val & 0xff;
	}
}
bool STMemory_CheckAreaType(uint32_t addr, int size, int mem_type ) {
	if ((addr > STRamEnd && addr < 0xe00000) ||
	    (addr >= 0xff0000 && addr < 0xff8000)) {
		return false;
	}
	return true;
}

/* fake CPU wrapper stuff */
#include "m68000.h"
uint16_t M68000_GetSR(void) { return 0x2700; }
void M68000_SetSR(uint16_t v) { }
void M68000_SetPC(uaecptr v) { }
void M68000_SetDebugger(bool debug) { }

/* fake UAE core registers */
#include "newcpu.h"
struct regstruct regs;
void m68k_dumpstate_file (FILE *f, uaecptr *nextpc, uaecptr prevpc) { }

/* fake debugui.c stuff */
#include "debug_priv.h"
#include "debugui.h"
FILE *debugOutput;
void DebugUI(debug_reason_t reason) { }
int DebugUI_PrintCmdHelp(const char *psCmd) { return DEBUGGER_CMDDONE; }
int DebugUI_GetPageLines(int config, int defvalue) { return 25; }
char *DebugUI_MatchHelper(const char **strings, int items, const char *text, int state)
{
	return NULL;
}

/* fake vdi.c stuff */
#include "vdi.h"
void VDI_Info(FILE *fp, uint32_t arg) { return; }

/* fake debugInfo.c stuff */
#include "debugInfo.h"
void DebugInfo_ShowSessionInfo(void) {}
uint32_t DebugInfo_GetBASEPAGE(void) { return 0x1f34; }
uint32_t DebugInfo_GetTEXT(void)     { return 0x1234; }
uint32_t DebugInfo_GetTEXTEnd(void)  { return 0x1234; }
uint32_t DebugInfo_GetDATA(void)     { return 0x12f4; }
uint32_t DebugInfo_GetBSS(void)      { return 0x1f34; }
info_func_t DebugInfo_GetInfoFunc(const char *name) {
	if (strcmp(name, "vdi") == 0) {
		return VDI_Info;
	}
	return NULL;
}

/* fake debugdsp.c stuff */
#ifdef ENABLE_DSP_EMU
#include "debugdsp.h"
void DebugDsp_InitSession(void) { }
uint32_t DebugDsp_CallDepth(void) { return 0; }
uint32_t DebugDsp_InstrCount(void) { return 0; }
uint32_t DebugDsp_OpcodeType(void) { return 0; }
#endif

/* use fake dsp.c stuff in case config.h is configured with DSP emu */
#include "dsp.h"
bool bDspEnabled;
uint16_t DSP_DisasmAddress(FILE *f, uint16_t lowerAdr, uint16_t UpperAdr) { return 0; }
uint16_t DSP_GetInstrCycles(void) { return 0; }
uint16_t DSP_GetPC(void) { return 0; }
int DSP_GetRegisterAddress(const char *arg, uint32_t **addr, uint32_t *mask)
{
	*addr = NULL; /* find if this gets used */
	*mask = 0;
	return 0;
}
uint32_t DSP_ReadMemory(uint16_t addr, char space, const char **mem_str)
{
	*mem_str = NULL; /* find if this gets used */
	return 0;
}

/* fake console redirection */
#include "console.h"
int ConOutDevices;
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
bool DebugUI_ParseFile(const char *path, bool reinit, bool verbose)
{
	return File_Exists(path);
}

/* fake disassembly output */
#include "68kDisass.h"
uint32_t Disasm_GetNextPC(uint32_t pc) { return pc+2; }
void Disasm (FILE *f, uaecptr addr, uaecptr *nextpc, int count) {}
void Disasm_GetColumns(int *columns) {}
void Disasm_SetColumns(int *columns) {}
void Disasm_DisableColumn(int column, const int *oldcols, int *newcols) {}
