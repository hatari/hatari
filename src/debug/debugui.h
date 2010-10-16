/*
  Hatari - debugui.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Public debugger UI header file.
*/

#ifndef HATARI_DEBUGUI_H
#define HATARI_DEBUGUI_H

/* DebugUI_ParseCommand() return values */
enum {
	DEBUGGER_END,      // Leave debugger
	DEBUGGER_CMDCONT,  // Command can continue
	DEBUGGER_CMDDONE   // Command done
};

/* Whether CPU exceptions invoke DebugUI */
extern int bExceptionDebugging;

extern void DebugUI(void);
extern void DebugUI_Init(void);
extern bool DebugUI_RemoteParse(char *input);
extern bool DebugUI_ParseFile(const char *path);
extern bool DebugUI_SetParseFile(const char *input);
extern void DebugUI_MemorySnapShot_Capture(const char *path, bool bSave);

#endif /* HATARI_DEBUGUI_H */
