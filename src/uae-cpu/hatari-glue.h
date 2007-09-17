/*
  Hatari - hatari-glue.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_GLUE_H
#define HATARI_GLUE_H

#include "sysdeps.h"
#include "options_cpu.h"

extern int pendingInterrupts;

int Init680x0(void);
void Exit680x0(void);
void Start680x0(void);
void customreset(void);
int intlev (void);

unsigned long OpCode_GemDos(uae_u32 opcode);
unsigned long OpCode_SysInit(uae_u32 opcode);
unsigned long OpCode_VDI(uae_u32 opcode);


#define write_log printf


#endif /* HATARI_GLUE_H */
