
/* test that ADDX does clears Z only if result is nonzero */
.global tst_addx_1
tst_addx_1:
	clr.l	d0
	move.l	#1,d1
	addx	d0,d0
	seq	d0
	rts

.global tst_addx_2
tst_addx_2:
	move.l	#1,d1
	clr.l	d0
	addx	d1,d1
	seq	d0
	rts
