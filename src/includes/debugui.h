/*
  Hatari - debugui.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_DEBUGUI_H
#define HATARI_DEBUGUI_H

/* DebugUI_ParseCommand() return values */
enum {
	DEBUGGER_END,      // Leave debugger
	DEBUGGER_CMDCONT,  // Command can continue
	DEBUGGER_CMDDONE   // Command done
};

extern void DebugUI(void);
extern int DebugUI_ParseCommand(char *input);
extern int DebugUI_GetCpuRegisterAddress(const char *reg, Uint32 **addr);
extern void DebugUI_CpuCheck(void);
extern void DebugUI_DspCheck(void);
extern void DebugUI_MemorySnapShot_Capture(bool bSave);

#endif /* HATARI_DEBUGUI_H */
