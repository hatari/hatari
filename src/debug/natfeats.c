/*
 * Hatari - natfeats.c
 * 
 * Copyright (C) 2012-2016 by Eero Tamminen
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * natfeats.c - Hatari Native features identification and call forwarding,
 * modeled after similar code in Aranym (written by Petr Stehlik),
 * specified here:
 * 	http://wiki.aranym.org/natfeats/proposal
 */
const char Natfeats_fileid[] = "Hatari natfeats.c : " __DATE__ " " __TIME__;

#include <stdio.h>
#include "main.h"
#include "version.h"
#include "configuration.h"
#include "stMemory.h"
#include "m68000.h"
#include "natfeats.h"
#include "debugui.h"
#include "nf_scsidrv.h"
#include "log.h"


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
 * NF_NAME - emulator name
 * Stack arguments are:
 * - pointer to buffer for emulator name, and
 * - uint32_t for its size
 * If subid is set, emulator name includes also version information
 */
static bool nf_name(Uint32 stack, Uint32 subid, Uint32 *retval)
{
	Uint32 ptr, len;
	const char *str;
	char *buf;

	ptr = STMemory_ReadLong(stack);
	len = STMemory_ReadLong(stack + SIZE_LONG);
	LOG_TRACE(TRACE_NATFEATS, "NF_NAME[%d](0x%x, %d)\n", subid, ptr, len);

	if ( !STMemory_CheckAreaType ( ptr, len, ABFLAG_RAM | ABFLAG_ROM ) ) {
		M68000_BusError(ptr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA);
		return false;
	}
	if (subid) {
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
static bool nf_version(Uint32 stack, Uint32 subid, Uint32 *retval)
{
	LOG_TRACE(TRACE_NATFEATS, "NF_VERSION() -> 0x00010000\n");
	*retval = 0x00010000;
	return true;
}

/**
 * NF_STDERR - print string to stderr
 * Stack arguments are:
 * - pointer to buffer containing the string
 */
static bool nf_stderr(Uint32 stack, Uint32 subid, Uint32 *retval)
{
	const char *str;
	Uint32 ptr;

	ptr = STMemory_ReadLong(stack);
	LOG_TRACE(TRACE_NATFEATS, "NF_STDERR(0x%x)\n", ptr);

	if ( !STMemory_CheckAreaType ( ptr, 1, ABFLAG_RAM | ABFLAG_ROM ) ) {
		M68000_BusError(ptr, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA);
		return false;
	}
	str = (const char *)STMemory_STAddrToPointer ( ptr );
	*retval = fprintf(stderr, "%s", str);
	fflush(stderr);
	return true;
}

/**
 * NF_SHUTDOWN - exit emulator normally
 * Needs to be called from supervisor mode
 */
static bool nf_shutdown(Uint32 stack, Uint32 subid, Uint32 *retval)
{
	LOG_TRACE(TRACE_NATFEATS, "NF_SHUTDOWN()\n");
	ConfigureParams.Log.bConfirmQuit = false;
	Main_RequestQuit(0);
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
static bool nf_exit(Uint32 stack, Uint32 subid, Uint32 *retval)
{
	Sint32 exitval;

	ConfigureParams.Log.bConfirmQuit = false;
	exitval = STMemory_ReadLong(stack);
	LOG_TRACE(TRACE_NATFEATS, "NF_EXIT(%d)\n", exitval);
	Main_RequestQuit(exitval);
	return true;
}

/**
 * NF_DEBUGGER - invoke debugger
 */
static bool nf_debugger(Uint32 stack, Uint32 subid, Uint32 *retval)
{
	LOG_TRACE(TRACE_NATFEATS, "NF_DEBUGGER()\n");
	DebugUI(REASON_PROGRAM);
	return true;
}

/**
 * NF_FASTFORWARD - set fast forward state
 * Stack arguments are:
 * - state 0: off, >=1: on
 */
static bool nf_fastforward(Uint32 stack, Uint32 subid, Uint32 *retval)
{
	Uint32 val;

	val = STMemory_ReadLong(stack);
	*retval = ConfigureParams.System.bFastForward;
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
static bool nf_command(Uint32 stack, Uint32 subid, Uint32 *retval)
{
	const char *buffer;
	Uint32 ptr;

	ptr = STMemory_ReadLong(stack);

	if ( !STMemory_CheckAreaType ( ptr, 1, ABFLAG_RAM | ABFLAG_ROM ) ) {
		M68000_BusError(ptr, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA);
		return false;
	}
	buffer = (const char *)STMemory_STAddrToPointer ( ptr );
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
	bool (*cb)(Uint32 stack, Uint32 subid, Uint32 *retval);
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
bool NatFeat_ID(Uint32 stack, Uint32 *retval)
{
	const char *name;
	Uint32 ptr;
	int i;

	ptr = STMemory_ReadLong(stack);
	if ( !STMemory_CheckAreaType ( ptr, FEATNAME_MAX, ABFLAG_RAM | ABFLAG_ROM ) ) {
		M68000_BusError(ptr, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA);
		return false;
	}

	name = (const char *)STMemory_STAddrToPointer ( ptr );
	LOG_TRACE(TRACE_NATFEATS, "NF ID(0x%x \"%s\")\n", ptr, name);

	for (i = 0; i < ARRAYSIZE(features); i++) {
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
bool NatFeat_Call(Uint32 stack, bool super, Uint32 *retval)
{
	Uint32 subid = STMemory_ReadLong(stack);
	unsigned int idx = MASTERID2IDX(subid);
	subid = MASKOUTMASTERID(subid);

	if (idx >= ARRAYSIZE(features)) {
		LOG_TRACE(TRACE_NATFEATS, "ERROR: invalid NF ID %d requested\n", idx);
		return true; /* undefined */
	}
	if (features[idx].super && !super) {
		LOG_TRACE(TRACE_NATFEATS, "ERROR: NF function %d called without supervisor mode\n", idx);
#ifndef WINUAE_FOR_HATARI
		M68000_Exception(8, M68000_EXC_SRC_CPU);
#else
		M68000_Exception(8, M68000_EXC_SRC_CPU);
#endif
		return false;
	}
	stack += SIZE_LONG;
	return features[idx].cb(stack, subid, retval);
}
