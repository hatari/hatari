/*
 * natfeat.c - NatFeats API example
 *
 * Copyright (c) 2014 by Eero Tamminen
 * 
 * Implementation is partly based on EmuTOS,
 * Copyright (c) 2001-2003 The EmuTOS development team
 * 
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#if __GNUC__
# include <mint/osbind.h>
#else	/* VBCC etc. */
# include <tos.h>
#endif
#include "natfeats.h"

#define uint32_t unsigned long

/* NatFeats available & initialized */
static int nf_ok;

/* handles for frequently used NF features */
static long nfid_print;

/* detect and initialize native features */
int nf_init(void)
{
	void *sup = Super(0);
	nf_ok = detect_nf();
	Super(sup);

	if (nf_ok) {
		/* initialize commonly used handles */
		nfid_print = nf_id("NF_STDERR");
	} else {
		Cconws("Native Features initialization failed!\r\n");
	}
	return nf_ok;
}

/* show given string on emulator console */
long nf_print(const char *text)
{
	if (nfid_print) {
		return nf_call(nfid_print, text);
	} else {
		Cconws("NF_STDERR unavailable!\r\n");
		return 0;
	}
}

/* terminate the execution of the emulation if possible */
void nf_shutdown(void)
{
	long id;
	if(nf_ok && (id = nf_id("NF_SHUTDOWN"))) {
		void *sup = Super(0);
		/* needs to be called in supervisor mode */
		nf_call(id);
		Super(sup);
	} else {
		Cconws("NF_SHUTDOWN unavailable!\r\n");
	}
}

/* terminate emulator with given exit code */
void nf_exit(int exitval)
{
	long id;
	if(nf_ok && (id = nf_id("NF_EXIT"))) {
		nf_call(id, (uint32_t)exitval);
	} else {
		/* NF_EXIT is Hatari specific, NF_SHUTDOWN isn't */
		Cconws("NF_EXIT unavailable, trying NF_SHUTDOWN...\r\n");
		nf_shutdown();
	}
}

#ifdef TEST

/* show emulator name */
static void nf_showname(void)
{
	long id;
	if (nf_ok && (id = nf_id("NF_NAME"))) {
		int len;
		long chars;
		char buffer[64];
		id |= 0x0001;  /* name + version */
		chars = nf_call(id, buffer, sizeof(buffer)-2);
		buffer[chars++] = '\n';
		buffer[chars++] = '\0';
		nf_print(buffer);
	} else {
		Cconws("NF_NAME unavailable!\r\n");
	}
}

static int wait_key(void)
{
	while (Cconis()) {
		Cconin();
	}
	Cconws("\r\n<press key>\r\n");
	return Cconin();
}

int main()
{
	if (!nf_init()) {
		wait_key();
		return 1;
	}
	nf_print("Emulator name:\n");
	nf_showname();
	nf_print("Shutting down...\n");
	nf_exit(0);
	wait_key();
	return 0;
}

#endif
