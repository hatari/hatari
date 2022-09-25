/*
  Hatari - debugdsp.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Public DSP debugging header file.
*/

#ifndef HATARI_DEBUGDSP_H
#define HATARI_DEBUGDSP_H

#ifdef ENABLE_DSP_EMU
extern void DebugDsp_SetDebugging(void);
#else /* !ENABLE_DSP_EMU */
#define DebugDsp_SetDebugging()
#endif /* !ENABLE_DSP_EMU */
extern void DebugDsp_Check(void);
extern uint32_t DebugDsp_CallDepth(void);
extern uint32_t DebugDsp_InstrCount(void);
extern uint32_t DebugDsp_OpcodeType(void);
extern int DebugDsp_DisAsm(int nArgc, char *psArgs[]);
extern int DebugDsp_MemDump(int nArgc, char *psArgs[]);
extern int DebugDsp_Register(int nArgc, char *psArgs[]);

#endif /* HATARI_DEBUGDSP_H */
