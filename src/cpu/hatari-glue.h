/*
  Hatari - hatari-glue.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_GLUE_H
#define HATARI_GLUE_H

#include "sysdeps.h"
#include "options_cpu.h"
#include "cycles.h"

extern int pendingInterrupts;

extern void customreset(void);
extern int intlev (void);
extern void UAE_Set_Quit_Reset ( bool hard );
extern void UAE_Set_State_Save ( void );
extern void UAE_Set_State_Restore ( void );
extern int Init680x0(void);
extern void Exit680x0(void);

extern uae_u32 extra_cycle;				/* From cpu/custom.c */

extern uae_u32 REGPARAM3 OpCode_GemDos(uae_u32 opcode);
extern    void REGPARAM3 OpCode_GemDos_noret(uae_u32 opcode);
extern uae_u32 REGPARAM3 OpCode_Pexec(uae_u32 opcode);
extern    void REGPARAM3 OpCode_Pexec_noret(uae_u32 opcode);
extern uae_u32 REGPARAM3 OpCode_SysInit(uae_u32 opcode);
extern    void REGPARAM3 OpCode_SysInit_noret(uae_u32 opcode);
extern uae_u32 REGPARAM3 OpCode_VDI(uae_u32 opcode);
extern    void REGPARAM3 OpCode_VDI_noret(uae_u32 opcode);
extern uae_u32 REGPARAM3 OpCode_NatFeat_ID(uae_u32 opcode);
extern    void REGPARAM3 OpCode_NatFeat_ID_noret(uae_u32 opcode);
extern uae_u32 REGPARAM3 OpCode_NatFeat_Call(uae_u32 opcode);
extern    void REGPARAM3 OpCode_NatFeat_Call_noret(uae_u32 opcode);

#endif /* HATARI_GLUE_H */
