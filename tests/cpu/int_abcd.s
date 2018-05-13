
/* test that ABCD clears Z only if result is nonzero */
.global tst_abcd_1
tst_abcd_1:
	clr.l	d0
	move.l	#1,d1
	abcd	d0,d0
	seq	d0
	rts

.global tst_abcd_2
tst_abcd_2:
	move.l	#1,d1
	clr.l	d0
	abcd	d1,d1
	seq	d0
	rts

.global tst_abcd_3
tst_abcd_3:
	move.l	#0x033,d0
	move.l	#0x044,d1
	abcd	d1,d0
	cmp.l	#0x77,d0
	sne	d0
	rts

.global tst_abcd_4
tst_abcd_4:
	move.l	#0x155,d0
	abcd	d0,d0
	cmp.l	#0x110,d0
	sne	d0
	rts
