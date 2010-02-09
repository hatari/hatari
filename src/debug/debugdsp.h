/*
  Hatari - debugdsp.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Public DSP debugging header file.
*/

#ifndef HATARI_DEBUGDSP_H
#define HATARI_DEBUGDSP_H

extern void DebugDsp_Check(void);
extern int DebugDsp_DisAsm(int nArgc, char *psArgs[]);
extern int DebugDsp_MemDump(int nArgc, char *psArgs[]);
extern int DebugDsp_Register(int nArgc, char *psArgs[]);

#endif /* HATARI_DEBUGDSP_H */
