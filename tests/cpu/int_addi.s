
.global tst_addi_1
tst_addi_1:
	sub.l	d0,d0
	addi.b	#0,d0
	sne	d0
	rts

.global tst_addi_2
tst_addi_2:
	sub.l	d0,d0
	addi.w	#0,d0
	sne	d0
	rts

.global tst_addi_3
tst_addi_3:
	sub.l	d0,d0
	addi.l	#0,d0
	sne	d0
	rts

.global tst_addi_4
tst_addi_4:
	move.l	#0x55667778,d1
	addi.b	#0x88,d1
	sne	d0
	scc	d2
	cmp.l	#0x55667700L,d1
	sne	d1
	or.b	d1,d0
	or.b	d2,d0
	rts

.global tst_addi_5
tst_addi_5:
	move.l	#0x55667788,d1
	addi.w	#0x3344,d1
	scs	d0
	cmp.l	#0x5566aacc,d1
	sne	d1
	or.b	d1,d0
	rts

.global tst_addi_6
tst_addi_6:
	move.l	#0x55667788,d1
	addi.l	#0x11223344,d1
	cmp.l	#0x6688aacc,d1
	sne	d0
	rts
