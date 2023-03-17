/*
 * Hatari - history.c
 * 
 * Copyright (C) 2011-2016,2020-2023 by Eero Tamminen
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * history.c - functions for debugger entry & breakpoint history
 */
const char History_fileid[] = "Hatari history.c";

#include <assert.h>
#include <errno.h>
#include "main.h"
#include "configuration.h"
#include "debugui.h"
#include "debug_priv.h"
#include "dsp.h"
#include "dsp_core.h"
#include "evaluate.h"
#include "file.h"
#include "history.h"
#include "m68000.h"
#include "68kDisass.h"

#define HISTORY_ITEMS_MIN 64

history_type_t HistoryTracking;

typedef struct {
	bool shown:1;
	bool valid:1;
	bool for_dsp:1;
	/* reason for debugger entry/breakpoint hit */
	debug_reason_t reason:8;
	union {
		uint16_t dsp;
		uint32_t cpu;
	} pc;
} hist_item_t;

static struct {
	unsigned idx;      /* index to current history item */
	unsigned count;    /* how many items of history are collected */
	unsigned limit;    /* ring-buffer size */
	unsigned repeats;  /* repeats for the last instruction */
	hist_item_t *item; /* ring-buffer */
} History;


/**
 * Convert debugger entry/breakpoint entry reason to a string
 */
static const char* History_ReasonStr(debug_reason_t reason)
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
	case REASON_PROGRAM:
		return "Program break";
	case REASON_USER:
		return "User break";
	default:
		return "Unknown reason";
	}
}


/**
 * Set what kind of history is collected.
 * Clear history if tracking type changes as rest of
 * data wouldn't then be anymore valid.
 */
static void History_Enable(history_type_t track, unsigned limit)
{
	const char *msg;
	if (track != HistoryTracking || limit != History.limit) {
		fprintf(stderr, "Re-allocating & zeroing history due to type/limit change.\n");
		if (History.item) {
			free(History.item);
		}
		memset(&History, 0, sizeof(History));
		History.item = calloc(limit, sizeof(History.item[0]));
		History.limit = limit;
	}
	switch (track) {
	case HISTORY_TRACK_NONE:
		msg = "disabled";
		break;
	case HISTORY_TRACK_CPU:
		msg = "enabled for CPU";
		break;
	case HISTORY_TRACK_DSP:
		msg = "enabled for DSP";
		break;
	case HISTORY_TRACK_ALL:
		msg = "enabled for CPU & DSP";
		break;
	default:
		msg = "error";
	}
	HistoryTracking = track;
	fprintf(stderr, "History tracking %s (max. %d instructions).\n", msg, limit);
}

/**
 * Advance & initialize next history item in ring buffer
 */
static void History_Advance(void)
{
	History.idx++;
	History.idx %= History.limit;
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
	uint32_t pc = M68000_GetPC();

	if (pc == History.item[History.idx].pc.cpu) {
		History.repeats++;
		return;
	} else {
		History.repeats = 0;
	}

	History_Advance();
	History.item[History.idx].for_dsp = false;
	History.item[History.idx].pc.cpu = pc;
}

/**
 * Add DSP PC to history
 */
void History_AddDsp(void)
{
	uint16_t pc = DSP_GetPC();

	if (pc == History.item[History.idx].pc.dsp) {
		History.repeats++;
		return;
	} else {
		History.repeats = 0;
	}

	History_Advance();
	History.item[History.idx].for_dsp = true;
	History.item[History.idx].pc.dsp = pc;
}

/**
 * Flag last history entry as debugger entry point, with given reason
 */
void History_Mark(debug_reason_t reason)
{
	if (History.item) {
		History.item[History.idx].reason = reason;
	}
}

/**
 * Find lowest address in history that is within range:
 *   (pc-offset) - pc
 * where 'offset' is the history disasm offset limit.
 *
 * If history has no such address, return given pc value.
 */
uint32_t History_DisasmAddr(uint32_t pc, uint32_t offset, bool for_dsp)
{
	unsigned int i, count;
	uint32_t limit, first;
	int track;

	if (!offset) {
		return pc;
	}
	track = for_dsp ? HISTORY_TRACK_DSP : HISTORY_TRACK_CPU;
	if (!(track & HistoryTracking)) {
		return pc;
	}
	count = History.count;
	if (count > History.limit) {
		count = History.limit;
	}
	if (count <= 0) {
		return pc;
	}
	first = pc;
	limit = pc - offset;
	i = History.idx + History.limit - count;
	while (count-- > 0) {
		i++;
		i %= History.limit;
		assert(History.item[i].valid);
		if (History.item[i].for_dsp != for_dsp) {
			continue;
		}
		if (for_dsp) {
			pc = History.item[i].pc.dsp;
		} else {
			pc = History.item[i].pc.cpu;
		}
		if (pc >= limit && pc < first) {
			first = pc;
		}
	}
	return first;
}

/**
 * Output collected CPU/DSP debugger/breakpoint history
 */
static uint32_t History_Output(uint32_t count, FILE *fp)
{
	bool show_all;
	uint32_t retval;
	int i;

	if (History.count > History.limit) {
		History.count = History.limit;
	}
	if (count > History.count) {
		count = History.count;
	} else {
		if (!count) {
			/* default to all */
			count = History.count;
		}
	}
	if (count <= 0) {
		fprintf(stderr, "No history items to show.\n");
		return 0;
	}
	retval = count;

	show_all = false;
	if (History.item[History.idx].shown) {
		/* even last item already shown, show all again */
		show_all = true;
	}

	i = History.idx + History.limit - count;
	while (count-- > 0) {
		i++;
		i %= History.limit;
		assert(History.item[i].valid);
		if (History.item[i].shown && !show_all) {
			continue;
		}
		History.item[i].shown = true;

		if (History.item[i].for_dsp) {
			uint16_t pc = History.item[i].pc.dsp;
			DSP_DisasmAddress(fp, pc, pc);
		} else {
			uint32_t dummy;
			Disasm(fp, History.item[i].pc.cpu, &dummy, 1);
		}
		if (History.item[i].reason != REASON_NONE) {
			fprintf(fp, "Debugger: *%s*\n", History_ReasonStr(History.item[i].reason));
		}
	}
	if (History.repeats) {
		fprintf(fp, "Last item repeated %d times.\n", History.repeats);
	}
	return retval;
}

/* History_Output() helper for "info" & "lock" commands */
void History_Show(FILE *fp, uint32_t count)
{
	History_Output(count, fp);
}

/*
 * save all history to given file
 */
static void History_Save(const char *name)
{
	uint32_t count;
	FILE *fp;

	if (File_Exists(name)) {
		fprintf(stderr, "ERROR: file '%s' already exists!\n", name);

	} else if ((fp = fopen(name, "w"))) {
		count = History_Output(0, fp);
		fprintf(stderr, "%d history items saved to '%s'.\n", count, name);
		fclose(fp);
	} else {
		fprintf(stderr, "ERROR: opening '%s' failed (%d).\n", name, errno);
	}
}

/*
 * Readline callback
 */
char *History_Match(const char *text, int state)
{
	static const char* cmds[] = { "cpu", "dsp", "off", "save" };
	return DebugUI_MatchHelper(cmds, ARRAY_SIZE(cmds), text, state);
}

/**
 * Command: Show collected CPU/DSP debugger/breakpoint history
 */
int History_Parse(int nArgc, char *psArgs[])
{
	int count, limit = 0;

	if (nArgc < 2) {
		return DebugUI_PrintCmdHelp(psArgs[0]);
	}
	if (nArgc > 2) {
		limit = atoi(psArgs[2]);
	}
	/* make sure value is valid & positive */
	if (!limit) {
		limit = History.limit;
	}
	if (limit < HISTORY_ITEMS_MIN) {
		limit = HISTORY_ITEMS_MIN;
	}
	count = atoi(psArgs[1]);

	if (count <= 0) {
		/* no count -> enable or disable? */
		if (strcmp(psArgs[1], "on") == 0) {
			History_Enable(HISTORY_TRACK_ALL, limit);
			return DEBUGGER_CMDDONE;
		}
		if (strcmp(psArgs[1], "off") == 0) {
			History_Enable(HISTORY_TRACK_NONE, limit);
			return DEBUGGER_CMDDONE;
		}
		if (strcmp(psArgs[1], "cpu") == 0) {
			History_Enable(HISTORY_TRACK_CPU, limit);
			return DEBUGGER_CMDDONE;
		}
		if (strcmp(psArgs[1], "dsp") == 0) {
			History_Enable(HISTORY_TRACK_DSP, limit);
			return DEBUGGER_CMDDONE;
		}
		if (nArgc == 3 && strcmp(psArgs[1], "save") == 0) {
			History_Save(psArgs[2]);
			return DEBUGGER_CMDDONE;
		}
		fprintf(stderr,  "History range is 1-<limit>\n");
		return DebugUI_PrintCmdHelp(psArgs[0]);
	}

	History_Show(stderr, count);
	return DEBUGGER_CMDDONE;
}
