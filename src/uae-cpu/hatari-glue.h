/*
  Hatari - hatari-glue.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_GLUE_H
#define HATARI_GLUE_H


#include "sysdeps.h"


extern int illegal_mem;
extern int address_space_24;
extern int cpu_level;
extern int cpu_compatible;
extern int requestedInterrupt;

int Init680x0(void);
void Exit680x0(void);
void Start680x0(void);
void customreset(void);
int intlev (void);
void check_prefs_changed_cpu(int new_level, int new_compatible);

unsigned long OpCode_TimerD(uae_u32 opcode);
unsigned long OpCode_GemDos(uae_u32 opcode);
unsigned long OpCode_OldGemDos(uae_u32 opcode);
unsigned long OpCode_SysInit(uae_u32 opcode);
unsigned long OpCode_VDI(uae_u32 opcode);


#define write_log printf


#endif /* HATARI_GLUE_H */
