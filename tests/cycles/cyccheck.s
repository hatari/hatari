; Check CPU cycles. Note that this test only runs in 8 MHz ST color modes!

	clr.w   -(sp)
	pea     filename
	move.w  #$3C,-(sp)
	trap    #1              ; Fcreate
	addq.l  #8,sp
	move.w  d0,fhndl

	lea	tests,a4

main_loop:
	move.l  (a4)+,-(sp)
	bsr     print
	addq.l  #4,sp

	pea     septext
	bsr     print
	addq.l  #4,sp

	move.l	(a4)+,-(sp)
	bsr     run_test
	addq.l	#4,sp

	pea     numbertext
	bsr     print
	addq.l  #4,sp

	tst.l	(a4)
	bne.s	main_loop

	move.w  fhndl,-(sp)
	move.w  #$3E,-(sp)
	trap    #1              ; Fclose
	addq.l  #4,sp

;	move.w  #7,-(sp)
;	trap    #1		; Crawcin
;	addq.l  #2,sp

	clr.w   -(sp)
	trap    #1

print:
	move.l 4(sp),-(sp)
	move.w  #9,-(sp)
	trap    #1		; Cconws
	addq.l  #6,sp

	moveq   #-1,d0
	move.l  4(sp),a0
strlenloop:
	addq.l  #1,d0
	tst.b   (a0)+
	bne.s   strlenloop

	move.l  4(sp),-(sp)
	move.l  d0,-(sp)
	move.w  fhndl,-(sp)
	move.w  #$40,-(sp)
	trap    #1              ; Fwrite
	lea     12(sp),sp
out:
	rts

; *************** The tests ******************

text_nop:
	dc.b	"one nop",0
	.even
test_nop:
	nop		; 4 cycles
	rts

text_2nop:
	dc.b	"two nops",0
	.even
test_2nop:
	nop		; 4 cycles
	nop		; 4 cycles
	rts

text_lsl1:
	dc.b	"lsl #0",0
	.even
test_lsl1:
	moveq	#0,d0	; 4 cycles
	lsl.l	d0,d1	; 8 + 2 * 0 = 8 cycles
	rts

text_lsl2:
	dc.b	"lsl #6",0
	.even
test_lsl2:
	moveq	#6,d0	; 4 cycles
	lsl.l	d0,d1	; 8 + 2 * 6 = 20 cycles
	rts

text_exg_dbra1:
	dc.b	"nop+exg+dbra",0
	.even
test_exg_dbra1:
	moveq	#0,d2	 ; 4 cycles
	nop		 ; 4 cycles
	exg	d0,d1	 ; 6 cycles (pairing with the following instruction!)
	dbra	d2,out	 ; 14 cycles
	rts

text_exg_dbra2:
	dc.b	"exg+nop+dbra",0
	.even
test_exg_dbra2:
	moveq	#0,d2	 ; 4 cycles
	exg	d0,d1	 ; 6 cycles, rounded to 8 (no pairing!)
	nop		 ; 4 cycles
	dbra	d2,out	 ; 14 cycles, rounded to 16
	rts

text_exg_move1:
	dc.b	"nop+exg+move",0
	.even
test_exg_move1:
	nop		 ; 4 cycles
	exg	d0,d1	 ; 6 cycles (pairing with the following instruction!)
	move.b	-(a0),d1 ; 10 cycles
	rts

text_exg_move2:
	dc.b	"exg+nop+move",0
	.even
test_exg_move2:
	exg	d0,d1	 ; 6 cycles, will be rounded to 8 cycles (no pairing)
	nop		 ; 4 cycles
	move.b	-(a0),d1 ; 10 cycles, will be rounded to 12
	rts

text_asr_add1:
	dc.b	"nop+asr+add",0
	.even
test_asr_add1:
	moveq	#2,d0	 ; 4 cycles
	nop		 ; 4 cycles
	asr.w	d0,d1	 ; 6 + 2 * 2 = 10 cycles (not 12, thanks to pairing!)
	add.w	-(a0),d1 ; 10 cycles
	rts

text_asr_add2:
	dc.b	"asr+nop+add",0
	.even
test_asr_add2:
	moveq	#2,d0	 ; 4 cycles
	asr.w	d0,d1	 ; 6 + 2 * 2 = 10 cycles, rounded to 12
	nop		 ; 4 cycles
	add.w	-(a0),d1 ; 10 cycles, rounded to 12
	rts

text_cmp_beq1:
	dc.b	"nop+cmp+beq",0
	.even
test_cmp_beq1:
	nop		; 4 cycles
	cmp.l	d0,d0	; 6 cycles
	bra	out	; 10 cycles

text_cmp_beq2:
	dc.b	"cmp+nop+beq",0
	.even
test_cmp_beq2:
	cmp.l	d0,d0	; 6 cycles, rounded to 8
	nop		; 4 cycles
	bra	out	; 10 cycles, rounded to 12

text_sub_move1:
	dc.b	"clr+sub+move",0
	.even
test_sub_move1:
	clr.w	d2		; 4 cycles
	sub.l	(a0),d0		; 10 cycles
	move.w	0(a0,d2),d1	; 14 cycles
	rts

text_sub_move2:
	dc.b	"sub+clr+move",0
	.even
test_sub_move2:
	sub.l	(a0),d0		; 10 cycles, rounded to 12
	clr.w	d2		; 4 cycles
	move.w	0(a0,d2),d1	; 14 cycles, rounded to 16
	rts

text_move_820a:
	dc.b	"move ff820a",0
	.even
test_move_820a:
	move.b	$ffff820a.w,d0	; 12 cycles
	rts

text_move_8800:
	dc.b	"move ff8800",0
	.even
test_move_8800:
	move.b	$ffff8800.w,d0	; 12 cycles + wait state = 16 cycles
	rts

tests:
	dc.l	text_nop,	test_nop
	dc.l	text_2nop,	test_2nop
	dc.l	text_lsl1,	test_lsl1
	dc.l	text_lsl2,	test_lsl2
	dc.l	text_exg_dbra1,	test_exg_dbra1
	dc.l	text_exg_dbra2,	test_exg_dbra2
	dc.l	text_exg_move1,	test_exg_move1
	dc.l	text_exg_move2,	test_exg_move2
	dc.l	text_asr_add1,	test_asr_add1
	dc.l	text_asr_add2,	test_asr_add2
	dc.l	text_cmp_beq1,	test_cmp_beq1
	dc.l	text_cmp_beq2,	test_cmp_beq2
	dc.l	text_sub_move1,	test_sub_move1
	dc.l	text_sub_move2,	test_sub_move2
	dc.l	text_move_820a,	test_move_820a
	dc.l	text_move_8800,	test_move_8800
	dc.l	0, 0


**********************************************
*  Mini-program to determine the number of   *
*        clockcycles a routine uses          *
*             by Niclas Thisell              *
**********************************************

testinit:
	move.l	#scratch,a0
	rts

; Put routine to test on stack before calling this function
run_test:
	move.l	4(sp),d0
	movem.l	d3-d7/a3-a6,-(sp)
	move.l	d0,a5			; ; pointer to test routine in a5

	pea     0
	move.w  #$0020,-(sp)
	trap    #1
	addq.l  #6,sp
	move.l  d0,old_ssp

	move.l  $00000010,saveillegal
	move.w  #$0765,$ffff8240.w
	move.b  #2,$ffff820a.w

	move.l	$70,old_vbl
	move.l	#vblhandler,$70
loop:
	move.w	#0,got_vbl
	stop	#$2300		; Wait for VBL
	tst.w	got_vbl
	beq.s	loop
	move.l	old_vbl,$70

	move    #$2700,sr
	jsr     testinit
	move.l  a0,-(sp)
	move.w  d0,-(sp)
	lea     $ffff8209.w,a0
	moveq   #0,d0
.wait:
	move.b  (a0),d0
	beq.s   .wait
	not.w   d0
	lsr.w   d0,d0
	move.w  #128,d0
	sub.w   nopno(pc),d0
	add.w   d0,d0
	jmp     .jmpbase(pc,d0.w)
.jmpbase:
	.REPT	128
	nop
	.ENDR

	move.w  (sp)+,d0
	movea.l (sp)+,a0
;************************************
;sprite-routine or whatever here!
	jsr     (a5)
continue:
;***********************************
	move.b  $ffff8209.w,d0
	move.b  $ffff8207.w,d1
	move.b  $ffff8205.w,d2
	move.b  $ffff8209.w,d3
	sub.b   d0,d3
	cmp.b   #18,d3
	beq.s   .found_values
	addq.w  #5,nopno
	andi.w  #127,nopno
	beq     something_wrong_here
	move.w  #$0765,$ffff8240.w
	move    #$2300,sr
	bra     loop
.found_values:
	move.w  #$0770,$ffff8240.w
	move    #$2300,sr

	and.l   #$000000ff,d0
	and.l   #$000000ff,d1
	and.l   #$000000ff,d2
	lsl.w   #8,d2
	add.w   d1,d2
	lsl.l   #8,d2
	add.w   d0,d2
	sub.l   $0000044e,d2
	divu    #160,d2
	move.l  d2,d0
	mulu    #256,d0
	swap    d2
	add.w   d2,d0
	sub.w   nopno(pc),d0
	sub.w   nopno(pc),d0
	add.w   d0,d0
	sub.w   #248,d0
	bra     getouttahere
something_wrong_here:
	move.w  #10000,d0
.p:	addi.w  #$0123,$ffff8240.w
	dbra    d0,.p
	moveq   #0,d0
	bra     getouttahere
nopno:	dc.w 0

getouttahere:
	move    #$2300,sr
	move.b  #2,$ffff820a.w
	move.w  #$0777,$ffff8240.w

	lea     numbertext+5(pc),a0
	moveq   #4,d7
.decoutloop:
	divu    #10,d0
	swap    d0
	add.w   #'0',d0
	move.b  d0,-(a0)
	clr.w   d0
	swap    d0
	dbra    d7,.decoutloop

	move.l  saveillegal,$00000010

	move.l  old_ssp,-(sp)
	move.w  #$20,-(sp)
	trap    #1              ; Super
	addq.l  #6,sp

	movem.l	(sp)+,d3-d7/a3-a6

	rts

vblhandler:
	move.w	#1,got_vbl
	rte

	.data

filename:	dc.b "RESULTS.TXT",0

septext:	dc.b " :",9,0
numbertext:	dc.b "      cycles",13,10,0
	.even

	.bss
old_ssp:	ds.l 1
saveillegal:	ds.l 1
old_vbl:	ds.l 1
got_vbl:	ds.w 1
fhndl:		ds.w 1

	ds.l	16
scratch:
	ds.l	16
