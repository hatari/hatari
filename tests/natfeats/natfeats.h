/*
 * natfeats.h - NatFeats API header file
 *
 * Copyright (c) 2014 by Eero Tamminen
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef _NATFEAT_H
#define _NATFEAT_H

/* AHCC uses registers to pass arguments, but
 * NatFeats calls expect arguments to be in stack.
 * "cdecl" can be used to declare that arguments
 * should be passed in stack.
 */
#if __AHCC__
#define CDECL cdecl
#else
#define CDECL
#endif


/* internal ASM helper interface for natfeats.c */
long CDECL nf_id(const char *);
/* it's best not to use this directly as arguments are untyped */
long CDECL nf_call(long ID, ...);
/* call only from Supervisor mode */
int CDECL detect_nf(void);


/* natfeats.c public prototypes */

/**
 * detect & initialize native features
 * returns zero for fail
 */
extern int nf_init(void);

/**
 * returns NatFeats version
 * (upper word = major number, lower word = minor number)
 */
extern long nf_version(void);

/**
 * get emulator name
 * returns name length
 */
extern long nf_name(char *buf, long len);

/**
 * get full emulator name
 * returns name length
 */
extern long nf_fullname(char *buf, long len);

/**
 * print string to emulator console
 * returns number of chars output
 */
extern long nf_print(const char *text);

/**
 * invoke emulator debugger
 * (Hatari specific, can be used e.g. in asserts)
 */
extern long nf_debugger(void);

/**
 * set emulator fastforward mode on (1) or off (0)
 * (Hatari specific)
 * returns previous value
 */
extern long nf_fastforward(long enabled);

/**
 * terminate the execution of the emulation if possible
 * (runs in supervisor mode)
 */
extern void nf_shutdown(void);

/**
 * warm reset emulation
 * (runs in supervisor mode)
 */
extern void nf_reset(void);

/**
 * cold reset emulation
 * (runs in supervisor mode)
 */
extern void nf_reset_cold(void);

/**
 * power off emulation
 * (runs in supervisor mode)
 */
extern void nf_poweroff(void);

/**
 * terminate the execution of the emulation with exit code
 * (Hatari specific, can be used e.g. for determining test-case success)
 */
extern void nf_exit(long exitval);

#endif /* _NATFEAT_H */
