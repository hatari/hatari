; nf_asma.s - AHCC NatFeats ASM detection code
;
; Copyright (c) 2014 by Eero Tamminen
;
; Mostly based on code from EmuTOS,
; Copyright (c) 2001-2013 by the EmuTOS development team
;
; This file is distributed under the GPL, version 2 or at your
; option any later version.  See doc/license.txt for details.

;;
;; exported symbols
;;
;; (AHCC doesn't prepend C-symbols with _)
;;
.global	nf_id
.global	nf_call
.global	detect_nf

;;
;; variables
;;
.data

nf_version:
        dc.b  "NF_VERSION\0"
        even

;;
;; code
;;
.text

; NatFeats test
;
; Needs to be called from Supervisor mode,
; otherwise exception handler change bombs
detect_nf:
        clr.l   d0              ; assume no NatFeats available
        move.l  sp, a1
        move.l  0x10, a0        ; illegal vector
        move.l  #fail_nf, 0x10
        pea     nf_version
        sub.l   #4, sp
; Conflict with ColdFire instruction mvs.b d0,d1
        dc.w   0x7300           ; Jump to NATFEAT_ID
        tst.l   d0
        beq.s   fail_nf
        moveq   #1, d0          ; NatFeats detected

fail_nf:
        move.l  a1, sp
        move.l  a0, 0x10        ; illegal vector

        rts


; map native feature to its ID
nf_id:
        dc.w   0x7300 ; Conflict with ColdFire instruction mvs.b d0,d1
        rts

; call native feature by its ID
nf_call:
        dc.w   0x7301 ; Conflict with ColdFire instruction mvs.b d1,d1
        rts
