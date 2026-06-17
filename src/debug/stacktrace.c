/*
 * Hatari - stacktrace.c
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * Output CPU stack backtrace by walking the A6 frame pointer chain.
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

#define STACKTRACE_MAX 256

static uint32_t stacktrace_max_depth(int cmd_max, int limit)
{
	if (cmd_max > limit) {
		return limit;
	}
	if (cmd_max > 0) {
		return cmd_max;
	}
	if (ConfigureParams.Debugger.nBacktraceLines > limit) {
		return limit;
	}
	if (ConfigureParams.Debugger.nBacktraceLines > 0) {
		return ConfigureParams.Debugger.nBacktraceLines;
	}
	return limit;
}

static uint32_t get_stack_ceiling(void)
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

static bool frame_chain_valid(uint32_t fp, uint32_t parent_fp, uint32_t ceiling)
{
	uint32_t sp = Regs[REG_A7];

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

static void print_frame(int idx, int width, uint32_t addr)
{
	uint32_t sym_addr = addr;
	const char *name = Symbols_GetBeforeCpuAddress(&sym_addr);

	fprintf(stderr, "#%-2d 0x%0*x: ", idx, width, addr);
	if (name) {
		uint32_t off = addr - sym_addr;
		if (off) {
			fprintf(stderr, "%s +0x%x\n", name, off);
		} else {
			fprintf(stderr, "%s\n", name);
		}
	} else {
		fprintf(stderr, "0x%0*x\n", width, addr);
	}
}

static int stacktrace_walk(uint32_t start_fp, uint32_t max_depth)
{
	uint32_t fp, parent_fp, ret_addr, site, stack_ceiling;
	uint32_t seen[STACKTRACE_MAX];
	int frame_index = 0;
	unsigned int depth;
	int addr_width;

	max_depth = stacktrace_max_depth(max_depth, ARRAY_SIZE(seen));

	if (ConfigureParams.System.bAddressSpace24) {
		addr_width = 6;
	} else {
		addr_width = 8;
	}

	print_frame(frame_index++, addr_width, M68000_GetPC());

	if (!start_fp) {
		fprintf(stderr, "ERROR: frame pointer is zero, cannot unwind stack.\n");
		return DEBUGGER_CMDDONE;
	}

	fp = start_fp;
	stack_ceiling = get_stack_ceiling();

	for (depth = 0; depth < max_depth; depth++) {
		if (!read_frame(fp, &parent_fp, &ret_addr)) {
			fprintf(stderr, "ERROR: invalid frame at 0x%0*x.\n", addr_width, fp);
			return DEBUGGER_CMDDONE;
		}
		if (fp_seen(fp, seen, depth)) {
			fprintf(stderr, "ERROR: frame pointer loop detected at 0x%0*x.\n", addr_width, fp);
			return DEBUGGER_CMDDONE;
		}
		seen[depth] = fp;

		site = call_site_address(ret_addr);
		print_frame(frame_index++, addr_width, site);

		if (!parent_fp) {
			break;
		}
		if (!frame_chain_valid(fp, parent_fp, stack_ceiling)) {
			fprintf(stderr, "ERROR: broken frame chain at 0x%0*x (parent 0x%0*x).\n",
			        addr_width, fp, addr_width, parent_fp);
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
                                  uint32_t *start_fp, uint32_t *max_depth)
{
	*start_fp = Regs[REG_A6];
	*max_depth = 0;

	if (nArgc >= 2 && !Eval_Number(psArgs[1], max_depth, NUM_TYPE_NORMAL)) {
		fprintf(stderr, "Invalid max depth '%s'!\n", psArgs[1]);
		return false;
	}
	if (nArgc >= 3 && !Eval_Number(psArgs[2], start_fp, NUM_TYPE_CPU)) {
		fprintf(stderr, "Invalid frame pointer '%s'!\n", psArgs[2]);
		return false;
	}
	if (nArgc > 3) {
		fprintf(stderr, "%d extra arguments!\n", nArgc - 3);
		return false;
	}
	return true;
}

int StackTrace_Command(int nArgc, char *psArgs[])
{
	uint32_t start_fp, cmd_max = 0;

	if (nArgc > 3) {
		return DebugUI_PrintCmdHelp(psArgs[0]);
	}

	if (!stacktrace_parse_args(nArgc, psArgs, &start_fp, &cmd_max)) {
		return DEBUGGER_CMDDONE;
	}

	return stacktrace_walk(start_fp, cmd_max);
}
