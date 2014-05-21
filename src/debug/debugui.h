/*
  Hatari - debugui.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

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

typedef enum {
	REASON_NONE,       // uninitialized
	REASON_CPU_EXCEPTION,
	REASON_DSP_EXCEPTION,
	REASON_CPU_BREAKPOINT,
	REASON_DSP_BREAKPOINT,
	REASON_CPU_STEPS,
	REASON_DSP_STEPS,
	REASON_USER        // e.g. keyboard shortcut
} debug_reason_t;

extern void DebugUI_Init(void);
extern void DebugUI(debug_reason_t reason);
extern void DebugUI_Exceptions(int nr, long pc);
extern bool DebugUI_ParseLine(const char *input);
extern bool DebugUI_SetParseFile(const char *input);
extern void DebugUI_MemorySnapShot_Capture(const char *path, bool bSave);

#endif /* HATARI_DEBUGUI_H */
