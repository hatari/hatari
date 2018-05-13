
.global tst_add_1
tst_add_1:
	move.b	#0x80,d0
	add.b	d0,d0
	sne	d0
	rts

.global tst_add_2
tst_add_2:
	move.w	#0x8000,d0
	add.w	d0,d0
	sne	d0
	rts

.global tst_add_3
tst_add_3:
	move.l	#0x80000000,d0
	add.l	d0,d0
	sne	d0
	rts

.global tst_add_4
tst_add_4:
	move.l	#0x55667778,d1
	move.l	#0x11223388,d0
	add.b	d0,d1
	sne	d0
	scc	d2
	cmp.l	#0x55667700,d1
	sne	d1
	or.b	d1,d0
	or.b	d2,d0
	rts

.global tst_add_5
tst_add_5:
	move.l	#0x55667788,d1
	move.l	#0x11223344,d0
	add.w	d0,d1
	scs	d0
	cmp.l	#0x5566aacc,d1
	sne	d1
	or.b	d1,d0
	rts

.global tst_add_6
tst_add_6:
	move.l	#0x55667788,d1
	move.l	#0x11223344,d0
	add.l	d0,d1
	scs	d0
	cmp.l	#0x6688aacc,d1
	sne	d1
	or.b	d1,d0
	rts

.global tst_add_7
tst_add_7:
	move.l	#0x80000000,d1
	add.l	d1,d1
	scc	d0
	svc	d2
	sne	d1
	or.b	d1,d0
	or.b	d2,d0
	rts
