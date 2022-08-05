/*
  Hatari - vars.c

  Copyright (c) 2016 by Eero Tamminen

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  vars.c - Hatari internal variable value and OS call number accessors
  for conditional breakpoint and evaluate commands.
*/
const char Vars_fileid[] = "Hatari vars.c";

#include <stdio.h>
#include <stdlib.h>
#include "main.h"
#include "configuration.h"
#include "stMemory.h"
#include "m68000.h"
#include "screen.h"	 /* for defines needed by video.h */
#include "video.h"	 /* for Hatari video variable addresses */
#include "hatari-glue.h" /* for currprefs */

#include "debugInfo.h"
#include "debugcpu.h"
#include "debugdsp.h"
#include "debugui.h"
#include "symbols.h"
#include "68kDisass.h"
#include "vars.h"


static Uint32 GetCycleCounter(void)
{
	/* 64-bit, so only lower 32-bits are returned */
	return CyclesGlobalClockCounter;
}

/* Accessor functions for calculated Hatari values */
static Uint32 GetLineCycles(void)
{
	int dummy1, dummy2, lcycles;
	Video_GetPosition(&dummy1, &dummy2 , &lcycles);
	return lcycles;
}
static Uint32 GetFrameCycles(void)
{
	int dummy1, dummy2, fcycles;
	Video_GetPosition(&fcycles, &dummy1, &dummy2);
	return fcycles;
}

/* helpers for TOS OS call opcode accessor functions */

static inline Uint16 getLineOpcode(Uint8 line)
{
	Uint32 pc;
	Uint16 instr;
	pc = M68000_GetPC();
	instr = STMemory_ReadWord(pc);
	/* for opcode X, Line-A = 0xA00X, Line-F = 0xF00X */
	if ((instr >> 12) == line) {
		return instr & 0xFF;
	}
	return INVALID_OPCODE;
}
static inline bool isTrap(Uint8 trap)
{
	Uint32 pc;
	Uint16 instr;
	pc = M68000_GetPC();
	instr = STMemory_ReadWord(pc);
	return (instr == (Uint16)0x4e40u + trap);
}
static inline Uint16 getControlOpcode(void)
{
	/* Control[] address from D1, opcode in Control[0] */
	return STMemory_ReadWord(STMemory_ReadLong(Regs[REG_D1]));
}
static inline Uint16 getStackOpcode(void)
{
	return STMemory_ReadWord(Regs[REG_A7]);
}

/* Actual TOS OS call opcode accessor functions */
static Uint32 GetLineAOpcode(void)
{
	return getLineOpcode(0xA);
}
static Uint32 GetLineFOpcode(void)
{
	return getLineOpcode(0xF);
}
static Uint32 GetGemdosOpcode(void)
{
	if (isTrap(1)) {
		return getStackOpcode();
	}
	return INVALID_OPCODE;
}
static Uint32 GetBiosOpcode(void)
{
	if (isTrap(13)) {
		return getStackOpcode();
	}
	return INVALID_OPCODE;
}
static Uint32 GetXbiosOpcode(void)
{
	if (isTrap(14)) {
		return getStackOpcode();
	}
	return INVALID_OPCODE;
}
Uint32 Vars_GetAesOpcode(void)
{
	if (isTrap(2)) {
		Uint16 d0 = Regs[REG_D0];
		if (d0 == 0xC8) {
			return getControlOpcode();
		} else if (d0 == 0xC9) {
			/* same as appl_yield() */
			return 0x11;
		}
	}
	return INVALID_OPCODE;
}
Uint32 Vars_GetVdiOpcode(void)
{
	if (isTrap(2)) {
		Uint16 d0 = Regs[REG_D0];
		if (d0 == 0x73) {
			return getControlOpcode();
		} else if (d0 == 0xFFFE) {
			/* -2 = vq_[v]gdos() */
			return 0xFFFE;
		}
	}
	return INVALID_OPCODE;
}

/** return 1 if PC is on Symbol, 0 otherwise
 */
static Uint32 PConSymbol(void)
{
	const char *sym;
	Uint32 pc = M68000_GetPC();
	sym = Symbols_GetByCpuAddress(pc, SYMTYPE_TEXT);
	if (sym) {
		return 1;
	}
	return 0;
}

/** return first word in OS call parameters
 */
static Uint32 GetOsCallParam(void)
{
	/* skip OS call opcode */
	return STMemory_ReadWord(Regs[REG_A7]+SIZE_WORD);
}

static Uint32 GetNextPC(void)
{
	return Disasm_GetNextPC(M68000_GetPC());
}

/* sorted by variable name so that this can be bisected */
static const var_addr_t hatari_vars[] = {
	{ "AesOpcode", (Uint32*)Vars_GetAesOpcode, VALUE_TYPE_FUNCTION32, 16, "$FFFF when not on AES trap" },
	{ "Basepage", (Uint32*)DebugInfo_GetBASEPAGE, VALUE_TYPE_FUNCTION32, 0, "invalid before Desktop is up" },
	{ "BiosOpcode", (Uint32*)GetBiosOpcode, VALUE_TYPE_FUNCTION32, 16, "$FFFF when not on BIOS trap" },
	{ "BSS", (Uint32*)DebugInfo_GetBSS, VALUE_TYPE_FUNCTION32, 0, "invalid before Desktop is up" },
	{ "CpuCallDepth", (Uint32*)DebugCpu_CallDepth, VALUE_TYPE_FUNCTION32, 0, NULL }, /* call depth for 'next subreturn' */
	{ "CpuInstr", (Uint32*)DebugCpu_InstrCount, VALUE_TYPE_FUNCTION32, 0, "CPU instructions count" },
	{ "CpuOpcodeType", (Uint32*)DebugCpu_OpcodeType, VALUE_TYPE_FUNCTION32, 0, NULL }, /* opcode type for 'next' */
	{ "CycleCounter", (Uint32*)GetCycleCounter, VALUE_TYPE_FUNCTION32, 0, "global cycles counter (lower 32 bits)" },
	{ "DATA", (Uint32*)DebugInfo_GetDATA, VALUE_TYPE_FUNCTION32, 0, "invalid before Desktop is up" },
#if ENABLE_DSP_EMU
	{ "DspCallDepth", (Uint32*)DebugDsp_CallDepth, VALUE_TYPE_FUNCTION32, 0, NULL }, /* call depth for 'dspnext subreturn' */
	{ "DspInstr", (Uint32*)DebugDsp_InstrCount, VALUE_TYPE_FUNCTION32, 0, "DSP instructions count" },
	{ "DspOpcodeType", (Uint32*)DebugDsp_OpcodeType, VALUE_TYPE_FUNCTION32, 0, NULL }, /* opcode type for 'dspnext' */
#endif
	{ "FrameCycles", (Uint32*)GetFrameCycles, VALUE_TYPE_FUNCTION32, 0, "cycles since VBL" },
	{ "GemdosOpcode", (Uint32*)GetGemdosOpcode, VALUE_TYPE_FUNCTION32, 16, "$FFFF when not on GEMDOS trap" },
	{ "HBL", (Uint32*)&nHBL, VALUE_TYPE_VAR32, sizeof(nHBL)*8, "number of HBL interrupts"  },
	{ "LineAOpcode", (Uint32*)GetLineAOpcode, VALUE_TYPE_FUNCTION32, 16, "$FFFF when not on Line-A opcode" },
	{ "LineCycles", (Uint32*)GetLineCycles, VALUE_TYPE_FUNCTION32, 0, "cycles since HBL (divisible by 4)" },
	{ "LineFOpcode", (Uint32*)GetLineFOpcode, VALUE_TYPE_FUNCTION32, 16, "$FFFF when not on Line-F opcode" },
	{ "NextPC", (Uint32*)GetNextPC, VALUE_TYPE_FUNCTION32, 0, "Next instruction address" },
	{ "OsCallParam", (Uint32*)GetOsCallParam, VALUE_TYPE_FUNCTION32, 16, "valid only on OS call opcode breakpoint" },
	{ "PConSymbol", (Uint32*)PConSymbol, VALUE_TYPE_FUNCTION32, 16, "1 if PC on symbol, 0 otherwise" },
	{ "TEXT", (Uint32*)DebugInfo_GetTEXT, VALUE_TYPE_FUNCTION32, 0, "invalid before Desktop is up" },
	{ "TEXTEnd", (Uint32*)DebugInfo_GetTEXTEnd, VALUE_TYPE_FUNCTION32, 0, "invalid before Desktop is up" },
	{ "VBL", (Uint32*)&nVBLs, VALUE_TYPE_VAR32, sizeof(nVBLs)*8, "number of VBL interrupts" },
	{ "VdiOpcode", (Uint32*)Vars_GetVdiOpcode, VALUE_TYPE_FUNCTION32, 16, "$FFFF when not on VDI trap" },
	{ "XbiosOpcode", (Uint32*)GetXbiosOpcode, VALUE_TYPE_FUNCTION32, 16, "$FFFF when not on XBIOS trap" }
};


/**
 * Readline match callback for Hatari variable and CPU variable/symbol name completion.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char *Vars_MatchCpuVariable(const char *text, int state)
{
	static int i, len;
	const char *name;

	if (!state) {
		/* first match */
		len = strlen(text);
		i = 0;
	}
	/* next match */
	while (i < ARRAY_SIZE(hatari_vars)) {
		name = hatari_vars[i++].name;
		if (strncasecmp(name, text, len) == 0)
			return (strdup(name));
	}
	/* no variable match, check all CPU symbols */
	return Symbols_MatchCpuAddress(text, state);
}


/**
 * If given string matches Hatari variable name, return its struct pointer,
 * otherwise return NULL.
 */
const var_addr_t *Vars_ParseVariable(const char *name)
{
	const var_addr_t *hvar;
	/* left, right, middle, direction */
        int l, r, m, dir;

	/* bisect */
	l = 0;
	r = ARRAY_SIZE(hatari_vars) - 1;
	do {
		m = (l+r) >> 1;
		hvar = hatari_vars + m;
		dir = strcasecmp(name, hvar->name);
		if (dir == 0) {
			return hvar;
		}
		if (dir < 0) {
			r = m-1;
		} else {
			l = m+1;
		}
	} while (l <= r);
	return NULL;
}


/**
 * Return Uint32 value from given Hatari variable struct*
 */
Uint32 Vars_GetValue(const var_addr_t *hvar)
{
	switch (hvar->vtype) {
	case VALUE_TYPE_FUNCTION32:
		return ((Uint32(*)(void))(hvar->addr))();
	case VALUE_TYPE_VAR32:
		return *(hvar->addr);
	default:
		fprintf(stderr, "ERROR: variable '%s' has unsupported type '%d'\n",
			hvar->name, hvar->vtype);
		exit(-1);
	}
}


/**
 * If given string is a Hatari variable name, set value to given
 * variable's value and return true, otherwise return false.
 */
bool Vars_GetVariableValue(const char *name, Uint32 *value)
{
	const var_addr_t *hvar;

	if (!(hvar = Vars_ParseVariable(name))) {
		return false;
	}
	*value = Vars_GetValue(hvar);
	return true;
}


/**
 * List Hatari variable names & current values
 */
int Vars_List(int nArgc, char *psArgv[])
{
	Uint32 value;
	char numstr[16];
	int i, maxlen = 0;
	for (i = 0; i < ARRAY_SIZE(hatari_vars); i++) {
		int len = strlen(hatari_vars[i].name);
		if (len > maxlen) {
			maxlen = len;
		}
	}
	fputs("Hatari debugger builtin symbols and their values are:\n", stderr);
	for (i = 0; i < ARRAY_SIZE(hatari_vars); i++) {
		const var_addr_t *hvar = &hatari_vars[i];
		if (!hvar->info) {
			/* debugger internal variables don't have descriptions */
			continue;
		}
		value = Vars_GetValue(hvar);
		if (hvar->bits == 16) {
			fprintf(stderr, " %*s:     $%04X", maxlen, hvar->name, value);
		} else {
			fprintf(stderr, " %*s: $%08X", maxlen, hvar->name, value);
		}
		sprintf(numstr, "(%d)", value);
		fprintf(stderr, " %-*s %s\n", 12, numstr, hvar->info);
	}
	fputs("Some of the variables are valid only in specific situations.\n", stderr);
	return DEBUGGER_CMDDONE;
}
