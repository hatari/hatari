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
		/* initialize handles */
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

#ifdef TEST

/* show emulator name */
static void nf_showname(void)
{
	long id;
	if (nf_ok && (id = nf_id("NF_NAME"))) {
		long chars;
		char buffer[64];
		id |= 0x0001;  /* name + version */
		chars = nf_call(id, buffer, sizeof(buffer));
		nf_print(buffer);
		nf_print("\n");
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
	nf_shutdown();
	wait_key();
	return 0;
}

#endif
