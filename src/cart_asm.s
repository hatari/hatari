	org	$fa1000

GEMDOS_OPCODE		equ	8		; Free op-code!
RUNOLDGEMDOS_OPCODE	equ	9		; Free op-code!
VDI_OPCODE			equ	12		; Free op-code!

harddrive_boot	dc.l	hdv_boot
old_gemdos		ds.l	1
vdi_opcode		dc.w	VDI_OPCODE		;Address to call after Trap #2(VDI), causes illegal instruction

;New GemDOS vector (0x84)
new_gemdos
	dc.w	GEMDOS_OPCODE	; Returns NEG as run old vector, ZERO to return or OVERFLOW to run pexec
	bvs		pexec
	bne		go_oldgemdos
	rte

;Branch to old GemDOS
go_oldgemdos
	dc.w	RUNOLDGEMDOS_OPCODE	;Set PC to 'old_gemdos' and continue execution, WITHOUT corrupting register

;Progam Execute
pexec
	lea		8(sp),a0
	btst	#5,(sp)
	bne.s	s_ok
	move.l	usp,a0
	addq	#2,a0
s_ok:
	tst		(a0)
	bne.s	no_0
	move.l	a6,-(sp)
	move.l	a0,a6
	move.l	a6,-(sp)	;new
	bsr		find_prog
	move.l	(sp)+,a6	;new
	bsr		pexec5
	bsr		reloc
	clr.l	2(a6)
	clr.l	10(a6)
	move.l	d0,6(a6)
	move	#4,(a6)			;6 for TOS 1.02 on
	move.l	(sp)+,a6
	bra		go_oldgemdos
no_0:
	cmp		#3,(a0)
	bne.s	go_oldgemdos
	move.l	a6,-(sp)
	move.l	a0,a6
	bsr		find_prog
	bsr		pexec5
	bsr		reloc
gohome:
	move.l	(sp)+,a6
	rte
find_prog:
	move	#$002F,-(a7)	; Fgetdta
	trap	#1		; Gemdos
	addq	#2,sp
	move.l	d0,a0
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	a0,-(sp)
	move	#$17,-(sp)
	move.l	2(a6),-(sp)
	move	#$004E,-(a7)	; Fsfirst
	trap	#1		; Gemdos
	addq	#8,sp
	move.l	(sp)+,a0
	move.l	(sp)+,-(a0)
	move.l	(sp)+,-(a0)
	move.l	(sp)+,-(a0)
	move.l	(sp)+,-(a0)
	move.l	(sp)+,-(a0)
	move.l	(sp)+,-(a0)
	move.l	(sp)+,-(a0)
	move.l	(sp)+,-(a0)
	move.l	(sp)+,-(a0)
	move.l	(sp)+,-(a0)
	move.l	(sp)+,-(a0)
	tst.l	d0
	beq.s	findprog_ok
	addq	#4,sp
	bra.s	gohome
findprog_ok:
	rts
pexec5:
	move.l	10(a6),-(sp)
	move.l	6(a6),-(sp)
	clr.l	-(sp)
	move	#5,-(sp)
	move	#$004B,-(a7)	; Pexec
	trap	#1		; Gemdos
	lea		16(sp),sp
	tst.l	d0
	bmi.s	pexecerr
	rts
pexecerr:
	addq	#4,sp
	bra.s	gohome
reloc:
	movem.l	a3-a5/d6-d7,-(sp)
	move.l	d0,a5
	clr		-(sp)
	move.l	2(a6),-(sp)
	move	#$003D,-(a7)	; Fopen
	trap	#1		; Gemdos
	addq	#8,sp
	move.l	d0,d6
	move.l	a5,-(sp)
	add.l	#228,(sp)
	pea		$1c.w
	move	d6,-(sp)
	move	#$003F,-(a7)	; Fread
	trap	#1		; Gemdos
	lea		12(sp),sp
; check size!!
	move.l	a5,-(sp)
	add.l	#256,(sp)
	pea		$7fffffff
	move	d6,-(sp)
	move	#$003F,-(a7)	; Fread
	trap	#1		; Gemdos
	lea		12(sp),sp
	move	d6,-(sp)
	move	#$003E,-(a7)	; Fclose
	trap	#1		; Gemdos
	addq	#4,sp
	lea		8(a5),a4
	move.l	a5,d0
	add.l	#$100,d0
	move.l	d0,(a4)+		; text start
	move.l	230(a5),d0
	move.l	d0,(a4)+		; text length
	add.l	8(a5),d0		; data start
	move.l	d0,(a4)+
	move.l	234(a5),(a4)+	; data length
	add.l	234(a5),d0
	move.l	d0,(a4)+		; bss start
	move.l	238(a5),(a4)+	; bss length
	move.l	a5,d0
	add.l	#$80,d0
	move.l	d0,32(a5)
	move.l	24(a5),a4
	add.l	242(a5),a4		; symbol table length
	move.l	8(a5),a3
	move.l	a3,d0
	tst.w	254(a5)
	bne.s	relocdone

	;; the following part until relocdone is from a patch
	;; by the author of winston
;;; Created by TT-Digger v6.3
;;; Sun Apr 06 14:37:50 2003

	move.l	(a4),d7
	move.l	#$00000000,(a4)+
	cmp.l	#$00000000,d7
	beq.s	relocdone
	adda.l	d7,a3
	moveq	#$00,d7
LFA1192:add.l	d0,(a3)
LFA1194:move.b	(a4),d7
	move.b	#$00,(a4)+
	cmp.b	#$00,d7
	beq.s	LFA11B0
	cmp.b	#$01,d7
	bne.s	LFA11AC
	lea	$00FE(a3),a3
	bra.s	LFA1194
LFA11AC:adda	d7,a3
	bra.s	LFA1192

relocdone:
	move.l	28(a5),d0
	beq.s	cleardone
	move.l	24(a5),a0
clear:
	clr.b	(a0)+
	subq.l	#1,d0
	bne.s	clear
cleardone:
	move.l	a5,d0
	movem.l	(sp)+,a3-a5/d6-d7
	rts

;Boot from floppy (this code is not used anymore by hatari)
hdv_boot
	;Read first sector of floppy
	;Store at bodge TOS 1.00 address $167a
	;And set D0.W to zero if executable

	movem.l	d1-d7/a0-a6,-(sp)

	;Read disc sector
	move.w	#0,-(sp)			;Drive A
	move.w	#0,-(sp)			;First logical sector
	move.w	#1,-(sp)			;Read 1 sector
	move.l	#$167a,-(sp)		;Bodge address, $16da for TOS 1.02, $181C for 1.04, $185C for 1.62
	move.w	#0,-(sp)			;Read only
	move	#$0004,-(a7)	; Rwabs
	trap	#13		; Bios
	add.l	#14,sp
	tst.l	d0
	bmi		non_executable

	;Is sector executable?
	;It is if checksum is OK
	move.w	#$100,-(sp)			;256 words (512 bytes)
	move.l	#$167a,-(sp)		;Address
	bsr		calc_bootsector_checksum
	addq.l	#6,sp
	cmp.w	#$1234,d0
	bne		non_executable

	clr.w	d0					;Is executable
	movem.l	(sp)+,d1-d7/a0-a6
	rts

non_executable
	moveq.l	#4,d0				;Not valid boot sector
	movem.l	(sp)+,d1-d7/a0-a6
	rts

;Copied from ST Internals(Abacus), page 323
calc_bootsector_checksum
	link	a6,#0
	movem.l	d6-d7,-(sp)
	clr.w	d7					;Clear checksum
	bra		.loop_end			;To loop end
.loop
	move.l	8(a6),a0			;Address of buffer
	move.w	(a0),d0				;Get word
	add.w	d0,d7				;sum
	addq.l	#2,8(a6)			;Increment buffer address
.loop_end
	move.w  12(a6),d0			;Number of words
	subq.w	#1,12(a6)			;minus 1
	tst.w	d0					;All words added?
	bne		.loop				;No
	move.w	d7,d0				;Result in D0

	tst.l	(a7)+
	movem.l	(a7)+,d7
	unlk	a6
	rts

