/*
 * natfeat.c - NatFeats API examples
 *
 * Copyright (c) 2014-2016, 2019 by Eero Tamminen
 * 
 * NF initialization & calling is based on EmuTOS code,
 * Copyright (c) 2001-2003 The EmuTOS development team
 * 
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#if __GNUC__
# include <mint/osbind.h>
#else	/* VBCC/AHCC/Pure-C */
# include <tos.h>
#endif
#include "natfeats.h"

#define MSG_NF_MISSING \
	"\r\nStart Hatari with option:\r\n\t--natfeats yes\r\n"

/* NatFeats available & initialized */
static int nf_ok;

/* handles for NF features that may be used more frequently */
static long nfid_print, nfid_debugger, nfid_fastforward;


/* API documentation is in natfeats.h header */

int nf_init(void)
{
	void *sup = (void*)Super(0);
	nf_ok = detect_nf();
	Super(sup);

	if (nf_ok) {
		/* initialize commonly used handles */
		nfid_print = nf_id("NF_STDERR");
		nfid_debugger = nf_id("NF_DEBUGGER");
		nfid_fastforward = nf_id("NF_FASTFORWARD");
	} else {
		(void)Cconws("Native Features initialization failed!\r\n");
	}
	return nf_ok;
}

long nf_version(void)
{
	long id;
	if(nf_ok && (id = nf_id("NF_VERSION"))) {
		return nf_call(id);
	} else {
		(void)Cconws("NF_VERSION unavailable!\r\n");
		return 0;
	}
}

static long getname(char *buf, long len, int subid)
{
	long id;
	if (nf_ok && (id = nf_id("NF_NAME"))) {
		return nf_call(id | subid, buf, (long)len);
	} else {
		(void)Cconws("NF_NAME unavailable!\r\n");
		return 0;
	}
}

long nf_name(char *buf, long len)
{
	return getname(buf, len, 0x0);
}
long nf_fullname(char *buf, long len)
{
	return getname(buf, len, 0x1);
}

long nf_print(const char *text)
{
	if (nfid_print) {
		return nf_call(nfid_print, text);
	} else {
		(void)Cconws("NF_STDERR unavailable!\r\n");
		return 0;
	}
}

long nf_debugger(void)
{
	if (nfid_debugger) {
		return nf_call(nfid_debugger);
	} else {
		(void)Cconws("NF_DEBUGGER unavailable!\r\n");
		return 0;
	}
}

long nf_fastforward(long enabled)
{
	if (nfid_fastforward) {
		return nf_call(nfid_fastforward, enabled);
	} else {
		(void)Cconws("NF_FASTFORWARD unavailable!\r\n");
		return 0;
	}
}

static void halt_reset(int subid)
{
	long id;
	if(nf_ok && (id = nf_id("NF_SHUTDOWN"))) {
		void *sup = (void*)Super(0);
		/* needs to be called in supervisor mode */
		nf_call(id | subid);
		Super(sup);
	} else {
		(void)Cconws("NF_SHUTDOWN unavailable!\r\n");
	}
}

void nf_shutdown(void)
{
	halt_reset(0x0);
}
void nf_reset(void)
{
	halt_reset(0x1);
}
void nf_reset_cold(void)
{
	halt_reset(0x2);
}
void nf_poweroff(void)
{
	halt_reset(0x3);
}

void nf_exit(long exitval)
{
	long id;
	if(nf_ok && (id = nf_id("NF_EXIT"))) {
		nf_call(id, exitval);
	} else {
		/* NF_EXIT is Hatari specific, NF_SHUTDOWN isn't */
		(void)Cconws("NF_EXIT unavailable, trying NF_SHUTDOWN...\r\n");
		nf_shutdown();
	}
}

#ifdef TEST

/* show emulator name */
static void nf_showname(void)
{
	long chars;
	char buffer[64];
	chars = nf_fullname(buffer, (long)(sizeof(buffer)-2));
	buffer[chars++] = '\n';
	buffer[chars++] = '\0';
	nf_print(buffer);
}

#if 1
# define nf_showversion(void)
#else
/* printf requires adding ahcstart.o & ahccstdi.lib to nf_ahcc.prj,
 * but those need features not supported by Hatari's dummy TOS
 * (used in automated tests).
 *
 * Seeing the output requires "--conout 2" Hatari option, as
 * emulated display doesn't have time to refresh before exit.
 */
#include <stdio.h>
static void nf_showversion(void)
{
	long version = nf_version();
	printf("NF API version: 0x%x\n", version);
}
#endif

static int wait_key(void)
{
	while (Cconis()) {
		Cconin();
	}
	(void)Cconws("\r\n<press key>\r\n");
	return Cconin();
}

int main()
{
	long old_ff;
	if (!nf_init()) {
		(void)Cconws(MSG_NF_MISSING);
		wait_key();
		return 1;
	}
	old_ff = nf_fastforward(1);
	nf_print("Emulator name:\n");
	nf_showname();
	nf_print(""); /* check regression b2a81850 + its fix */
	nf_print("Invoking debugger...\n");
	nf_debugger();
	nf_print("Restoring fastforward & shutting down...\n");
	nf_fastforward(old_ff);
	nf_exit(0);
	wait_key();
	return 0;
}

#endif
