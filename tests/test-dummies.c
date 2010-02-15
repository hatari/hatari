/* 
 * Dummy stuff needed to compile debugger related test code
 */

/* fake tracing flags */
#include "log.h"
Uint32 LogTraceFlags = 0;

/* fake Hatari configuration variables for number parsing */
#include "configuration.h"
CNF_PARAMS ConfigureParams;

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
void MakeFromSR(void) { }

/* fake AUE core registers */
#include "newcpu.h"
struct regstruct regs;
void MakeSR(void) { }
void m68k_dumpstate (FILE *f, uaecptr *nextpc) { }
void m68k_disasm (FILE *f, uaecptr addr, uaecptr *nextpc, int cnt) { }

/* fake memory snapshot */
#include "memorySnapShot.h"
void MemorySnapShot_Store(void *pData, int Size) { }

/* fake debugui.c stuff */
#include "debug_priv.h"
#include "debugui.h"
FILE *debugOutput;
void DebugUI(void) { }
void DebugUI_PrintCmdHelp(const char *psCmd) { }

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
