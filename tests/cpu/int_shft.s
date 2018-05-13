
/* ASL: test that both C & X flags are set to the last bit shifted out */
.global tst_shift_1
tst_shift_1:
	movem.l	d3-d4,-(sp)
	move.l	#0,d1
	move.l	#1,d2
	move.l	#1,d3
	move.l	#0,d4
	lsr.l	d2,d3	/* should set X & C */
	scc	d0
	asl	#1,d1	/* should clear C and X */
	scs	d1
	addx.l	d4,d4
	tst.l	d4
	sne	d4
	or.b	d1,d0
	or.b	d4,d0
	movem.l	(sp)+,d3-d4
	rts

/* test that lsl is modulo 64 */
.global tst_shift_2
tst_shift_2:
	move.l	#33,d1
	move.l	#1,d2
	lsl.l	d1,d2
	sne	d0
	rts

.global tst_shift_3
tst_shift_3:
	move.l	#65,d1
	move.l	#1,d2
	lsl.l	d1,d2
	cmp.l	#2,d2
	sne	d0
	rts

/* test that zero shift does not affect X flag */
.global tst_shift_4
tst_shift_4:
	movem.l	d3-d4,-(sp)
	move.l	#0,d1
	move.l	#1,d2
	move.l	#1,d3
	move.l	#0,d4
	lsr.l	d2,d3	/* should set X & C */
	scc	d0
	lsr.l	d1,d4	/* should not affect X & clear C */
	scs	d1
	addx.l	d4,d4
	cmp.l	#1,d4
	sne	d4
	or.b	d1,d0
	or.b	d4,d0
	movem.l	(sp)+,d3-d4
	rts

/* LSL: test that both C & X flags are set to the last bit shifted out */
.global tst_shift_5
tst_shift_5:
	movem.l	d3-d4,-(sp)
	move.l	#0x55555555,d1
	move.l	#0,d2
	lsl.l	#8,d1
	scc	d0
	addx.l	d2,d2
	tst.l	d2
	seq	d2
	move.l	#0,d3
	lsl.l	#1,d1
	scs	d4
	addx.l	d3,d3
	tst.l	d3
	sne	d3
	cmp.l	#0xaaaaaa00,d1
	sne	d1
	or.b	d1,d0
	or.b	d2,d0
	or.b	d3,d0
	or.b	d4,d0
	movem.l	(sp)+,d3-d4
	rts

/* same as above, for word shifts */
.global tst_shift_6
tst_shift_6:
	movem.l	d3-d4,-(sp)
	move.l	#0x55555555,d1
	move.l	#0,d2
	lsl.w	#8,d1
	scc	d0
	addx.l	d2,d2
	cmp.l	#1,d2
	sne	d2
	move.l	#0,d3
	lsl.w	#1,d1
	scs	d4
	addx.l	d3,d3
	tst.l	d3
	sne	d3
	cmp.l	#0x5555aa00,d1
	sne	d1
	or.b	d1,d0
	or.b	d2,d0
	or.b	d3,d0
	or.b	d4,d0
	movem.l	(sp)+,d3-d4
	rts

/* same as above, for byte shifts */
.global tst_shift_7
tst_shift_7:
	movem.l	d3-d4,-(sp)
	move.l	#0x55555555,d1
	move.l	#0,d2
	lsl.b	#8,d1
	scc	d0
	addx.l	d2,d2
	cmp.l	#1,d2
	sne	d2
	moveq.l	#0,d3
	lsl.b	#1,d1
	scs	d4
	addx.l	d3,d3
	tst.l	d3
	sne	d3
	cmp.l	#0x55555500,d1
	sne	d1
	or.b	d1,d0
	or.b	d2,d0
	or.b	d3,d0
	or.b	d4,d0
	movem.l	(sp)+,d3-d4
	rts

/* ROL: test that C is set to the last bit shifted out & that X is unaffected */
.global tst_shift_8
tst_shift_8:
	movem.l	d3-d5,-(sp)
	moveq.l	#0,d5
	move.l	#0x80000000,d0
	add.l	d0,d0	/* should set X, C and V, and clear d0 */
	scc	d1
	svc	d2
	rol	#1,d0	/* should clear C and V, and leave X unaffected */
	scs	d3
	svs	d4
	addx.l	d5,d5
	cmp.l	#1,d5
	sne	d0
	or.b	d1,d0
	or.b	d2,d0
	or.b	d3,d0
	or.b	d4,d0
	movem.l	(sp)+,d3-d5
	rts
