; nf_asmv.s - VBCC/Vasm NatFeats ASM detection code
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
        PUBLIC  _nf_id
	PUBLIC  _nf_call
        PUBLIC  _detect_nf

;;
;; variables
;;
        DATA

nf_version:
        dc.b  "NF_VERSION\0"
        even

;;
;; code
;;
        TEXT

; NatFeats test
;
; Needs to be called from Supervisor mode,
; otherwise exception handler change bombs
_detect_nf:
        clr.l   d0              ; assume no NatFeats available
        move.l  sp, a1
        move.l  $10, a0         ; illegal vector
        move.l  #fail_nf, $10
        pea     nf_version
        sub.l   #4, sp
; Conflict with ColdFire instruction mvs.b d0,d1
        dc.w   $7300            ; Jump to NATFEAT_ID
        tst.l   d0
        beq.s   fail_nf
        moveq   #1, d0          ; NatFeats detected

fail_nf:
        move.l  a1, sp
        move.l  a0, $10         ; illegal vector

        rts


; map native feature to its ID
_nf_id:
        dc.w   $7300  ; Conflict with ColdFire instruction mvs.b d0,d1
        rts

; call native feature by its ID
_nf_call:
        dc.w   $7301  ; Conflict with ColdFire instruction mvs.b d1,d1
        rts
