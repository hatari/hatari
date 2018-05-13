
.global tst_addq_1
tst_addq_1:
	sub.l	d0,d0
	addq.b	#1,d0
	seq	d0
	rts

.global tst_addq_2
tst_addq_2:
	sub.l	d0,d0
	addq.w	#1,d0
	seq	d0
	rts

.global tst_addq_3
tst_addq_3:
	move.l	d3,-(sp)
	move.l	#0x55667778,d1
	addq.b	#8,d1
	scs	d0
	spl	d2
	svc	d3
	cmp.l	#0x55667780,d1
	sne	d1
	or.b	d1,d0
	or.b	d2,d0
	or.b	d3,d0
	move.l	(sp)+,d3
	rts
