/*
 * Hatari - history.c
 * 
 * Copyright (C) 2011 by Eero Tamminen
 *
 * This file is distributed under the GNU Public License, version 2 or at
 * your option any later version. Read the file gpl.txt for details.
 *
 * history.c - functions for debugger entry & breakpoint history
 */
const char History_fileid[] = "Hatari history.c : " __DATE__ " " __TIME__;

#include <assert.h>

#include "main.h"
#include "debugui.h"
#include "debug_priv.h"
#include "dsp.h"
#include "dsp_core.h"
#include "evaluate.h"
#include "history.h"
#include "m68000.h"
#include "68kDisass.h"

bool bHistoryEnabled;

#define HISTORY_ITEMS	256

typedef struct {
	bool shown:1;
	bool valid:1;
	bool for_dsp:1;
	/* reason for debugger entry/breakpoint hit */
	debug_reason_t reason:8;
	union {
		Uint16 dsp;
		Uint32 cpu;
	} pc;
} hist_item_t;

static struct {
	int idx;          /* index to current history item */
	int count;        /* how many items of history are collected */
	Uint16 dsp_pc;    /* last CPU PC position */
	Uint32 cpu_pc;    /* last DSP PC position */
	hist_item_t item[HISTORY_ITEMS];  /* ring-buffer */
} History;


/**
 * Convert debugger entry/breakpoint entry reason to a string
 */
const char* History_ReasonStr(debug_reason_t reason)
{
	switch(reason) {
	case REASON_CPU_EXCEPTION:
		return "CPU exception";
	case REASON_CPU_BREAKPOINT:
		return "CPU breakpoint";
	case REASON_DSP_BREAKPOINT:
		return "DSP breakpoint";
	case REASON_CPU_STEPS:
		return "CPU steps";
	case REASON_DSP_STEPS:
		return "DSP steps";
	case REASON_USER:
		return "User break";
	default:
		return "Unknown reason";
	}
}


/**
 * Enable/disable history collecting.
 * Clear history on disabling as data wouldn't
 * then be anymore valid.
 */
void History_Enable(bool enable)
{
	if (!enable) {
		memset(&History, 0, sizeof(History));
	}
	bHistoryEnabled = enable;
}

/**
 * Advance & initialize next history item in ring buffer
 */
static void History_Advance(void)
{
	History.idx++;
	History.idx %= HISTORY_ITEMS;
	History.item[History.idx].valid = true;
	History.item[History.idx].shown = false;
	History.item[History.idx].reason = REASON_NONE;
	History.count++;
}

/**
 * Add CPU PC to history
 */
void History_AddCpu(void)
{
	Uint32 pc = M68000_GetPC();

	History_Advance();
	History.item[History.idx].for_dsp = false;
	History.item[History.idx].pc.cpu = pc;
	History.cpu_pc = pc;
}

/**
 * Add DSP PC to history
 */
void History_AddDsp(void)
{
	Uint16 pc = DSP_GetPC();

	History_Advance();
	History.item[History.idx].for_dsp = true;
	History.item[History.idx].pc.dsp = pc;
	History.dsp_pc = pc;
}

/**
 * Flag last history entry as debugger entry point, with given reason
 */
void History_Mark(debug_reason_t reason)
{
	History.item[History.idx].reason = reason;
}

/**
 * Get previous CPU PC position
 */
Uint32 History_GetLastCpu(void) { return History.cpu_pc; }
/**
 * Get previous DSP PC position
 */
Uint32 History_GetLastDsp(void) { return History.dsp_pc; }


/**
 * Show collected CPU/DSP debugger/breakpoint history
 */
void History_Show(int count)
{
	bool show_all;
	int i;

	if (History.count > HISTORY_ITEMS) {
		History.count = HISTORY_ITEMS;
	}
	if (count > History.count) {
		count = History.count;
	}
	if (count <= 0) {
		fprintf(stderr,  "No history items to show.\n");
		return;
	}

	i = History.idx;
	if (History.item[i].shown) {
		/* even last item already shown, show all again */
		show_all = true;
	}
	i = (i + HISTORY_ITEMS - count) % HISTORY_ITEMS;

	while (count-- > 0) {
		i++;
		i %= HISTORY_ITEMS;
		if (!History.item[i].valid) {
			fprintf(stderr, "ERROR: invalid history item %d!", count);
		}
		if (History.item[i].shown && !show_all) {
			continue;
		}
		if (History.item[i].reason != REASON_NONE) {
			fprintf(stderr, "*%s*:\n", History_ReasonStr(History.item[i].reason));
		}
		if (History.item[i].for_dsp) {
			Uint16 pc = History.item[i].pc.dsp;
			DSP_DisasmAddress(pc, pc);
		} else {
			Uint32 dummy;
			Disasm(stderr, History.item[i].pc.dsp, &dummy, 1, DISASM_ENGINE_EXT);
		}
	}
}

/**
 * Command: Show collected CPU/DSP debugger/breakpoint history
 */
bool History_Parse(int nArgc, char *psArgs[])
{
	int count;

	if (nArgc != 2) {
		DebugUI_PrintCmdHelp(psArgs[0]);
		return DEBUGGER_CMDDONE;
	}

	count = atoi(psArgs[1]);
	if (count <= 0 || count > HISTORY_ITEMS) {
		/* no count -> enable or disable? */
		if (strcmp(psArgs[1], "on") == 0) {
			History_Enable(true);
			return DEBUGGER_CMDDONE;
		}
		if (strcmp(psArgs[1], "off") == 0) {
			History_Enable(false);
			return DEBUGGER_CMDDONE;
		}
		fprintf(stderr,  "History range is 1-%d!\n", HISTORY_ITEMS);
		DebugUI_PrintCmdHelp(psArgs[0]);
		return DEBUGGER_CMDDONE;
	}

	History_Show(count);
	return DEBUGGER_CMDDONE;
}
