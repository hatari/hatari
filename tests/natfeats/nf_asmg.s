/*
 * nf_asm.s - GCC NatFeats ASM detection code
 *
 * Copyright (c) 2014 by Eero Tamminen
 *
 * Mostly based on code from EmuTOS,
 * Copyright (c) 2001-2013 by the EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.
 *
 * TODO: test this
 */

/*
 * public interface
 */
        .globl  _nf_id
        .globl  _nf_call
        .globl  _detect_nf

/* illegal exception vector */
        .equ vec_illegal, 0x10

/*
 * code
 */
        .text

/*
 * NatFeats test
 *
 * Needs to be called from Supervisor mode,
 * otherwise exception handler change bombs
 */
_detect_nf:
        clr.l   d0               /* assume no NatFeats available */
        move.l  sp,a1
        move.l  vec_illegal,a0
        move.l  #fail_nf,vec_illegal
        pea     nf_version_name
        sub.l   #4,sp
#ifdef __mcoldfire__
#error Conflict with instruction mvs.b d0,d1
#else
        .dc.w   0x7300              /* Jump to NATFEAT_ID */
#endif
        tst.l   d0
        beq.s   fail_nf
        moveq   #1,d0               /* NatFeats detected */

fail_nf:
        move.l  a1,sp
        move.l  a0,vec_illegal

        rts

nf_version_name:
        .ascii  "NF_VERSION\0"
        .even

/*
 * map native features to NF ID
 */
_nf_id:
        .dc.w   0x7300  /* Conflict with ColdFire instruction mvs.b d0,d1 */
        rts

/*
 * call native feature by its ID
 */
_nf_call:
        .dc.w   0x7301  /* Conflict with ColdFire instruction mvs.b d1,d1 */
        rts
