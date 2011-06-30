/*
  Hatari - debugcpu.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Public CPU debugging header.
*/

#ifndef HATARI_DEBUGCPU_H
#define HATARI_DEBUGCPU_H

extern void DebugCpu_Check(void);
extern void DebugCpu_SetDebugging(void);
extern int DebugCpu_DisAsm(int nArgc, char *psArgs[]);
extern int DebugCpu_MemDump(int nArgc, char *psArgs[]);
extern int DebugCpu_Register(int nArgc, char *psArgs[]);
extern int DebugCpu_GetRegisterAddress(const char *reg, Uint32 **addr);

#endif /* HATARI_DEBUGCPU_H */
