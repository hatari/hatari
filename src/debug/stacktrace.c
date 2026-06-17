/*
 * Hatari - stacktrace.c
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * Stack backtrace by walking the A6 frame pointer chain.
 */
const char StackTrace_fileid[] = "Hatari stacktrace.c";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "config.h"

#include "main.h"
#include "configuration.h"
#include "debugui.h"
#include "debug_priv.h"
#include "debugInfo.h"
#include "evaluate.h"
#include "m68000.h"
#include "stMemory.h"
#include "symbols.h"
#include "stacktrace.h"

#define STACKTRACE_DEFAULT_MAX 256

static bool arg_is_depth_only(const char *arg)
{
	if (!arg || !*arg) {
		return false;
	}
	while (*arg) {
		if (!isdigit((unsigned char)*arg)) {
			return false;
		}
		arg++;
	}
	return true;
}

static int stacktrace_max_depth(int cmd_max)
{
	if (cmd_max > 0) {
		return cmd_max;
	}
	if (ConfigureParams.Debugger.nBacktraceLines > 0) {
		return ConfigureParams.Debugger.nBacktraceLines;
	}
	return STACKTRACE_DEFAULT_MAX;
}

static uint32_t stack_ceiling(void)
{
	uint32_t basepage = DebugInfo_GetBASEPAGE();

	if (!basepage || !STMemory_CheckAreaType(basepage, 8, ABFLAG_RAM)) {
		return 0;
	}
	if (STMemory_ReadLong(basepage) != basepage) {
		return 0;
	}
	return STMemory_ReadLong(basepage + 4);
}

static bool read_frame(uint32_t fp, uint32_t *parent_fp, uint32_t *ret_addr)
{
	if (fp & 1) {
		return false;
	}
	if (!STMemory_CheckAreaType(fp, 2 * SIZE_LONG, ABFLAG_RAM)) {
		return false;
	}
	*parent_fp = STMemory_ReadLong(fp);
	*ret_addr = STMemory_ReadLong(fp + SIZE_LONG);
	return true;
}

static bool frame_chain_valid(uint32_t fp, uint32_t parent_fp)
{
	uint32_t sp = Regs[REG_A7];
	uint32_t ceiling = stack_ceiling();

	if (!parent_fp || parent_fp <= fp) {
		return false;
	}
	if (fp < sp) {
		return false;
	}
	if (ceiling && parent_fp > ceiling) {
		return false;
	}
	return true;
}

static uint32_t call_site_address(uint32_t ret_addr)
{
	if (ret_addr >= 2) {
		return ret_addr - 2;
	}
	return ret_addr;
}

static bool fp_seen(uint32_t fp, const uint32_t *seen, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		if (seen[i] == fp) {
			return true;
		}
	}
	return false;
}

static void print_frame(int index, uint32_t addr)
{
	fprintf(stderr, "#%-2d 0x%06x  ", index, addr);
	Symbols_PrintAddress(stderr, addr, false);
	fputc('\n', stderr);
}

static int stacktrace_walk(uint32_t start_fp, int max_depth)
{
	uint32_t fp, parent_fp, ret_addr, site;
	uint32_t seen[STACKTRACE_DEFAULT_MAX];
	int depth = 0;
	int seen_count = 0;
	int frame_index = 0;

	print_frame(frame_index++, M68000_GetPC());

	if (!start_fp) {
		fprintf(stderr, "ERROR: frame pointer is zero, cannot unwind stack.\n");
		return DEBUGGER_CMDDONE;
	}

	fp = start_fp;
	while (depth < max_depth) {
		if (!read_frame(fp, &parent_fp, &ret_addr)) {
			fprintf(stderr, "ERROR: invalid frame at 0x%06x.\n", fp);
			return DEBUGGER_CMDDONE;
		}
		if (fp_seen(fp, seen, seen_count)) {
			fprintf(stderr, "ERROR: frame pointer loop detected at 0x%06x.\n", fp);
			return DEBUGGER_CMDDONE;
		}
		seen[seen_count++] = fp;

		site = call_site_address(ret_addr);
		print_frame(frame_index++, site);

		depth++;
		if (!parent_fp) {
			break;
		}
		if (!frame_chain_valid(fp, parent_fp)) {
			fprintf(stderr, "ERROR: broken frame chain at 0x%06x (parent 0x%06x).\n",
			        fp, parent_fp);
			return DEBUGGER_CMDDONE;
		}
		if (fp_seen(parent_fp, seen, seen_count)) {
			fprintf(stderr, "ERROR: frame pointer loop detected at 0x%06x.\n", parent_fp);
			return DEBUGGER_CMDDONE;
		}
		fp = parent_fp;
	}

	if (depth >= max_depth) {
		fprintf(stderr, "(stack trace truncated at %d frames)\n", max_depth);
	}
	return DEBUGGER_CMDDONE;
}

/**
 * Parse optional [fp] [max] arguments for bt command.
 */
static bool stacktrace_parse_args(int nArgc, char *psArgs[],
                                  uint32_t *start_fp, int *max_depth)
{
	int dummy;
	const char *err;

	*start_fp = Regs[REG_A6];
	*max_depth = 0;

	if (nArgc < 2) {
		return true;
	}

	if (nArgc >= 3) {
		if (!Eval_Number(psArgs[2], (uint32_t *)max_depth, NUM_TYPE_NORMAL)) {
			fprintf(stderr, "Invalid max depth '%s'!\n", psArgs[2]);
			return false;
		}
		err = Eval_Expression(psArgs[1], start_fp, &dummy, false);
		if (err) {
			fprintf(stderr, "%s\n", err);
			return false;
		}
		return true;
	}

	if (arg_is_depth_only(psArgs[1])) {
		if (!Eval_Number(psArgs[1], (uint32_t *)max_depth, NUM_TYPE_NORMAL)) {
			fprintf(stderr, "Invalid max depth '%s'!\n", psArgs[1]);
			return false;
		}
		return true;
	}

	err = Eval_Expression(psArgs[1], start_fp, &dummy, false);
	if (err) {
		fprintf(stderr, "%s\n", err);
		return false;
	}
	return true;
}

int StackTrace_Command(int nArgc, char *psArgs[])
{
	uint32_t start_fp;
	int cmd_max = 0;
	int max_depth;

	if (nArgc > 3) {
		return DebugUI_PrintCmdHelp(psArgs[0]);
	}

	if (!stacktrace_parse_args(nArgc, psArgs, &start_fp, &cmd_max)) {
		return DEBUGGER_CMDDONE;
	}

	max_depth = stacktrace_max_depth(cmd_max);
	return stacktrace_walk(start_fp, max_depth);
}
