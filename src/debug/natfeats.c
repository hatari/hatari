/*
 * Hatari - natfeats.c
 * 
 * Copyright (C) 2012-2016, 2019-2022 by Eero Tamminen
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * natfeats.c - Hatari Native features identification and call forwarding,
 * modeled after similar code in Aranym.  See tests/natfeats/ for more info.
 */
const char Natfeats_fileid[] = "Hatari natfeats.c";

#include <stdio.h>
#include "main.h"
#include "version.h"
#include "configuration.h"
#include "stMemory.h"
#include "m68000.h"
#include "reset.h"
#include "natfeats.h"
#include "debugui.h"
#include "statusbar.h"
#include "nf_scsidrv.h"
#include "log.h"

/* maximum input string length */
#define NF_MAX_STRING 4096

/* whether to allow XBIOS(255) style
 * Hatari command line parsing with "command" NF
 */
#define NF_COMMAND 0

/* TODO:
 * - supervisor vs. user stack handling?
 * - clipboard and hostfs native features?
 */


/* ----------------------------------
 * Native Features shared with Aranym
 */

/**
 * Check whether given string address is valid
 * and whether strings is of "reasonable" length.
 *
 * Returns string length or negative value for error.
 */
static int mem_string_ok(uint32_t addr)
{
	const char *buf;
	int i;

	if (!STMemory_CheckAreaType(addr, 1, ABFLAG_RAM | ABFLAG_ROM)) {
		/* invalid starting address -> error */
		M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
		return -1;
	}
	buf = (const char *)STMemory_STAddrToPointer(addr);
	if (STMemory_CheckAreaType(addr, NF_MAX_STRING, ABFLAG_RAM | ABFLAG_ROM)) {
		/* valid area -> return length */
		for (i = 0; i < NF_MAX_STRING; i++) {
			if (!buf[i]) {
				return i;
			}
		}
		/* unterminated NF string -> error */
		M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
		return -1;
	}
	for (i = 0; i < NF_MAX_STRING; i++) {
		if (!STMemory_CheckAreaType(addr + i, 1, ABFLAG_RAM | ABFLAG_ROM)) {
			/* ends in invalid area -> error */
			M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
			return -1;
		}
		if (!buf[i]) {
			return i;
		}
	}
	assert(false); /* should never be reached */
	return -1;
}

/**
 * NF_NAME - emulator name
 * Stack arguments are:
 * - pointer to buffer for emulator name, and
 * - uint32_t for its size
 * subid == 1 -> emulator name includes also version information.
 * Returns length of the name
 */
static bool nf_name(uint32_t stack, uint32_t subid, uint32_t *retval)
{
	uint32_t ptr, len;
	const char *str;
	char *buf;

	ptr = STMemory_ReadLong(stack);
	len = STMemory_ReadLong(stack + SIZE_LONG);
	LOG_TRACE(TRACE_NATFEATS, "NF_NAME[%d](0x%x, %d)\n", subid, ptr, len);

	if ( !STMemory_CheckAreaType ( ptr, len, ABFLAG_RAM | ABFLAG_ROM ) ) {
		M68000_BusError(ptr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
		return false;
	}
	if (subid == 1) {
		str = PROG_NAME;
	} else {
		str = "Hatari";
	}
	buf = (char *)STMemory_STAddrToPointer ( ptr );
	*retval = snprintf(buf, len, "%s", str);
	return true;
}

/**
 * NF_VERSION - NativeFeatures version
 * returns version number
 */
static bool nf_version(uint32_t stack, uint32_t subid, uint32_t *retval)
{
	LOG_TRACE(TRACE_NATFEATS, "NF_VERSION() -> 0x00010000\n");
	*retval = 0x00010000;
	return true;
}

/**
 * NF_STDERR - print string to stderr
 * Stack arguments are:
 * - pointer to buffer containing the string
 * Returns printed string length
 */
static bool nf_stderr(uint32_t stack, uint32_t subid, uint32_t *retval)
{
	const char *str;
	uint32_t ptr;

	ptr = STMemory_ReadLong(stack);
	LOG_TRACE(TRACE_NATFEATS, "NF_STDERR(0x%x)\n", ptr);

	*retval = 0;
	if (subid) {
		/* unrecognized subid, nothing printed */
		return true;
	}
	if (mem_string_ok(ptr) < 0) {
		return false;
	}
	str = (const char *)STMemory_STAddrToPointer (ptr);
	*retval = fprintf(stderr, "%s", str);
	fflush(stderr);
	return true;
}

/**
 * NF_SHUTDOWN - reset or exit emulator
 *
 * Needs to be called from supervisor mode
 */
static bool nf_shutdown(uint32_t stack, uint32_t subid, uint32_t *retval)
{
	const char *msg;

	LOG_TRACE(TRACE_NATFEATS, "NF_SHUTDOWN[%d]()\n", subid);
	switch (subid) {
	case 1:	/* warm reset */
		msg = "warm reset";
		Reset_Warm();
		/* Some infos can change after 'reset' */
		Statusbar_UpdateInfo();
		break;
	case 2:	/* cold reset (clear all) */
		msg = "cold reset";
		Reset_Cold();
		/* Some infos can change after 'reset' */
		Statusbar_UpdateInfo();
		break;
	case 0: /* shutdown */
	case 3: /* poweroff */
		msg = "poweroff";
		ConfigureParams.Log.bConfirmQuit = false;
		Main_RequestQuit(0);
		break;
	default:
		/* unrecognized subid -> no-op */
		return true;
	}
	fprintf(stderr, "NatFeats: %s\n", msg);
	return true;
}

/* ----------------------------------
 * Native Features specific to Hatari
 */

/**
 * NF_EXIT - exit emulator with given exit code
 * Stack arguments are:
 * - emulator's int32_t exit value
 */
static bool nf_exit(uint32_t stack, uint32_t subid, uint32_t *retval)
{
	int32_t exitval;

	ConfigureParams.Log.bConfirmQuit = false;
	exitval = STMemory_ReadLong(stack);
	LOG_TRACE(TRACE_NATFEATS, "NF_EXIT(%d)\n", exitval);
	Main_RequestQuit(exitval);
	fprintf(stderr, "NatFeats: exit(%d)\n", exitval);
	return true;
}

/**
 * NF_DEBUGGER - invoke debugger
 */
static bool nf_debugger(uint32_t stack, uint32_t subid, uint32_t *retval)
{
	LOG_TRACE(TRACE_NATFEATS, "NF_DEBUGGER()\n");
	DebugUI(REASON_PROGRAM);
	return true;
}

/**
 * NF_FASTFORWARD - set fast forward state
 * Stack arguments are:
 * - state 0: off, >=1: on
 * Returns previous fast-forward value
 */
static bool nf_fastforward(uint32_t stack, uint32_t subid, uint32_t *retval)
{
	uint32_t val;

	*retval = ConfigureParams.System.bFastForward;
	if (subid) {
		/* unrecognized sub id -> no-op */
		val = *retval;
	} else {
		val = STMemory_ReadLong(stack);
	}
	LOG_TRACE(TRACE_NATFEATS, "NF_FASTFORWARD(%d -> %d)\n", *retval, val);
	ConfigureParams.System.bFastForward = ( val ? true : false );
	return true;
}

#if NF_COMMAND
/**
 * NF_COMMAND - execute Hatari (cli / debugger) command
 * Stack arguments are:
 * - pointer to command string
 */
static bool nf_command(uint32_t stack, uint32_t subid, uint32_t *retval)
{
	const char *buffer;
	uint32_t ptr;

	if (subid) {
		/* unrecognized sub id -> no-op */
		return true;
	}
	ptr = STMemory_ReadLong(stack);
	if (mem_string_ok(ptr) < 0) {
		return false;
	}
	buffer = (const char *)STMemory_STAddrToPointer(ptr);
	LOG_TRACE(TRACE_NATFEATS, "NF_COMMAND(0x%x \"%s\")\n", ptr, buffer);
	Control_ProcessBuffer(buffer);
	return true;
}
#endif

/* ---------------------------- */

#define FEATNAME_MAX 16

static const struct {
	const char *name;	/* feature name */
	bool super;		/* should be called only in supervisor mode */
	bool (*cb)(uint32_t stack, uint32_t subid, uint32_t *retval);
} features[] = {
#if NF_COMMAND
	{ "NF_COMMAND",  false, nf_command },
#endif
	{ "NF_NAME",     false, nf_name },
	{ "NF_VERSION",  false, nf_version },
	{ "NF_STDERR",   false, nf_stderr },
	{ "NF_SHUTDOWN", true,  nf_shutdown },
	{ "NF_EXIT",     false, nf_exit },
	{ "NF_DEBUGGER", false, nf_debugger },
	{ "NF_FASTFORWARD", false,  nf_fastforward }
#if defined(__linux__)        
        ,{ "NF_SCSIDRV",  true, nf_scsidrv }
#endif
};

/* macros from Aranym */
#define ID_SHIFT        20
#define IDX2MASTERID(idx)       (((idx)+1) << ID_SHIFT)
#define MASTERID2IDX(id)        (((id) >> ID_SHIFT)-1)
#define MASKOUTMASTERID(id)     ((id) & ((1L << ID_SHIFT)-1))


/**
 * Set retval to internal ID for requested Native Feature,
 * or zero if feature is unknown/unsupported.
 * 
 * Return true if caller is to proceed normally,
 * false if there was an exception.
 */
bool NatFeat_ID(uint32_t stack, uint32_t *retval)
{
	const char *name;
	uint32_t ptr;
	int i;

	ptr = STMemory_ReadLong(stack);
	if ( !STMemory_CheckAreaType ( ptr, FEATNAME_MAX, ABFLAG_RAM | ABFLAG_ROM ) ) {
		M68000_BusError(ptr, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
		return false;
	}

	name = (const char *)STMemory_STAddrToPointer ( ptr );
	LOG_TRACE(TRACE_NATFEATS, "NF ID(0x%x \"%s\")\n", ptr, name);

	for (i = 0; i < ARRAY_SIZE(features); i++) {
		if (strcmp(features[i].name, name) == 0) {
			*retval = IDX2MASTERID(i);
			return true;
		}
	}
	/* unknown feature */
	*retval = 0;
	return true;
}

/**
 * Do given Native Feature, if it is supported
 * and set 'retval' accordingly.
 * 
 * Return true if caller is to proceed normally,
 * false if there was an exception.
 */
bool NatFeat_Call(uint32_t stack, bool super, uint32_t *retval)
{
	uint32_t subid = STMemory_ReadLong(stack);
	unsigned int idx = MASTERID2IDX(subid);
	subid = MASKOUTMASTERID(subid);

	if (idx >= ARRAY_SIZE(features)) {
		LOG_TRACE(TRACE_NATFEATS, "ERROR: invalid NF ID %d requested\n", idx);
		return true; /* undefined */
	}
	if (features[idx].super && !super) {
		LOG_TRACE(TRACE_NATFEATS, "ERROR: NF function %d called without supervisor mode\n", idx);
		M68000_Exception(8, M68000_EXC_SRC_CPU);
		return false;
	}
	stack += SIZE_LONG;
	return features[idx].cb(stack, subid, retval);
}
