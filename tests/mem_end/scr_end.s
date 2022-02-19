; Check whether we can crash Hatari by setting the screen to the end of memory

	TEXT

	clr.l	-(sp)
	move.w	#$20,-(sp)
	trap	#1		; Super
	move.l	d0,-(sp)

	move.l	$70.w,-(sp)
	move.l	#vbl,$70.w
	move.b	$ffff8201.w,-(sp)
	move.b	$ffff8203.w,-(sp)

	move.b	#$ff,$ffff8203.w

	move.b	#$3f,d0
	bsr.s	teststep

	move.b	#$80,d0
	bsr.s	teststep

	move.b	#$df,d0
	bsr.s	teststep

	move.b	#$e0,d0
	bsr.s	teststep

	move.b	#$f0,d0
	bsr.s	teststep

	move.b	#$fc,d0
	bsr.s	teststep

	move.b	#$ff,d0
	bsr.s	teststep

	move.b	(sp)+,$ffff8203.w
	move.b	(sp)+,$ffff8201.w
	move.l	(sp)+,$70.w

	move.w	#$20,-(sp)
	trap	#1		; Super
	addq.l	#6,sp

	clr.w	-(sp)		; Pterm0
	trap	#1


teststep:
	move.b	d0,$ffff8201.w
	stop	#$2300		; Wait for VBL

	cmp.w   #$0001,2	; Is it Hatari faketos?
	bne.s   skipdump

	move.w	#20,-(sp)
	trap	#14		; Scrdmp
	addq.l	#2,sp  

skipdump:
	stop	#$2300
	rts

vbl:
	rte
