/*
 * Hatari - natfeats.c
 * 
 * Copyright (C) 2012 by Eero Tamminen
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * natfeats.c - Hatari Native features identification and call forwarding,
 * modeleted after similar code in Aranym (written by Petr Stehlik),
 * specified here:
 * 	http://wiki.aranym.org/natfeats/proposal
 */
const char Natfeats_fileid[] = "Hatari natfeats.c : " __DATE__ " " __TIME__;

#include <stdio.h>
#include "main.h"
#include "configuration.h"
#include "stMemory.h"
#include "natfeats.h"

#define NF_DEBUG 1
#if NF_DEBUG
# define Dprintf(a) printf a
#else
# define Dprintf(a)
#endif

/* TODO:
 * - bus error / priviledge exception, not just error return
 * - supervisor vs. user stack handling?
 * - clipboard and hostfs native features?
 */


static Uint32 nf_name(Uint32 stack, Uint32 subid)
{
	Uint32 ptr, len;
	const char *str;
	char *buf;

	ptr = STMemory_ReadLong(stack);
	len = STMemory_ReadLong(stack + SIZE_LONG);
	Dprintf(("NF name[%d](0x%x, %d)\n", subid, ptr, len));

	if (!STMemory_ValidArea(ptr, len)) {
		/* TODO: bus error */
		return 0;
	}
	if (subid) {
		str = PROG_NAME;
	} else {
		str = "Hatari";
	}
	buf = (char *)STRAM_ADDR(ptr);
	return snprintf(buf, len, "%s", str);
}

static Uint32 nf_version(Uint32 stack, Uint32 subid)
{
	Dprintf(("NF version() -> 0x00010000\n"));
	return 0x00010000;
}

static Uint32 nf_stderr(Uint32 stack, Uint32 subid)
{
	const char *str;
	Uint32 ptr, ret;

	ptr = STMemory_ReadLong(stack);
	//Dprintf(("NF stderr(0x%x)\n", ptr));

	if (!STMemory_ValidArea(ptr, 1)) {
		/* TODO: bus error */
		return 0;
	}
	str = (const char *)STRAM_ADDR(ptr);
	ret = fprintf(stderr, "%s", str);
	fflush(stderr);
	return ret;
}

static Uint32 nf_shutdown(Uint32 stack, Uint32 subid)
{
	Dprintf(("NF shutdown()\n"));
	ConfigureParams.Log.bConfirmQuit = false;
	Main_RequestQuit();
	return 0;
}

/* ---------------------------- */

#define FEATNAME_MAX 16

static const struct {
	const char *name;	/* feature name */
	bool super;		/* should be called only in supervisor mode */
	Uint32 (*cb)(Uint32 stack, Uint32 subid);
} features[] = {
	{ "NF_NAME",     false, nf_name },
	{ "NF_VERSION",  false, nf_version },
	{ "NF_STDERR",   false, nf_stderr },
	{ "NF_SHUTDOWN", true,  nf_shutdown }
};

/* macros from Aranym */
#define ID_SHIFT        20
#define IDX2MASTERID(idx)       (((idx)+1) << ID_SHIFT)
#define MASTERID2IDX(id)        (((id) >> ID_SHIFT)-1)
#define MASKOUTMASTERID(id)     ((id) & ((1L << ID_SHIFT)-1))


Uint32 NatFeat_ID(Uint32 stack)
{
	const char *name;
	Uint32 ptr;
	int i;

	ptr = STMemory_ReadLong(stack);
	if (!STMemory_ValidArea(ptr, FEATNAME_MAX)) {
		/* TODO: bus error */
		return 0;
	}
	name = (const char *)STRAM_ADDR(ptr);
	Dprintf(("NF ID(0x%x)\n", ptr));
	Dprintf(("   \"%s\"\n", name));

	for (i = 0; i < ARRAYSIZE(features); i++) {
		if (strcmp(features[i].name, name) == 0) {
			return IDX2MASTERID(i);
		}
	}
	/* unknown feature */
	return 0;
}

Uint32 NatFeat_Call(Uint32 stack, bool super)
{
	Uint32 subid = STMemory_ReadLong(stack);
	unsigned int idx = MASTERID2IDX(subid);
	subid = MASKOUTMASTERID(subid);

	if (idx >= ARRAYSIZE(features)) {
		Dprintf(("ERROR: invalid NF ID %d requested\n", idx));
		return 0; /* undefined */
	}
	if (features[idx].super && !super) {
		Dprintf(("ERROR: NF function %d called without supervisor mode\n", idx));
		/* TODO: priviledge exception */
		return 0;
	}
	stack += SIZE_LONG;
	return features[idx].cb(stack, subid);
}
