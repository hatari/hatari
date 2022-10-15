; Check whether we can crash Hatari by playing DMA sound after the end of RAM

	TEXT

	clr.l	-(sp)
	move.w	#$20,-(sp)
	trap	#1			; Super
	move.l	d0,-(sp)

	move.l	$70.w,-(sp)
	move.l	#vbl,$70.w
	stop	#$2300			; Wait for VBL

	move.b	#0,$ffff8901.w		; Stop playback
	move.b	#$43,$ffff8921.w	; Stereo, 50 kHz
	move.b	#$ff,d0
	move.b	d0,$ffff8905.w
	move.b	d0,$ffff8907.w
	move.b	d0,$ffff8911.w
	move.b	d0,$ffff8913.w

	move.b	#$3f,d0
	bsr.s	teststep

	move.b	#$df,d0
	bsr.s	teststep

	move.b	#$ef,d0
	bsr.s	teststep

	move.b	#$fb,d0
	bsr.s	teststep

	move.b	#$ff,d0
	bsr.s	teststep

	move.l	(sp)+,$70.w

	move.w	#$20,-(sp)
	trap	#1		; Super
	addq.l	#6,sp

	pea	successtxt(pc)
	move.w  #9,-(sp)
	trap    #1		; Cconws
	addq.l  #6,sp

	clr.w	-(sp)		; Pterm0
	trap	#1


teststep:
	move.b	d0,$ffff8903.w	; Frame start high
	addq	#1,d0
	move.b	d0,$ffff890f.w	; Frame end high

	move.b	#3,$ffff8901.w	; Start sound

	move.w	#60,d1
loop:
	stop	#$2300		; Wait for VBL
	dbra	d1,loop

	move.b	#0,$ffff8901.w	; Stop sound

	rts

vbl:
	rte

successtxt:
	dc.b	"SUCCESS!",13,10,0
