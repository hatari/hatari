; Start up code for AHCC based test programs

.extern _main

	movea.l	4(SP),A5	; Get pointer to basepage
	move.l	$0C(A5),D0	; Text segment length
	add.l	$14(A5),D0	; Data segment length
	add.l	$1C(A5),D0	; BSS segment length
	add.l	#$2000,D0	; Space for the stack

	move.l	D0,D1
	add.l	A5,D1
	and.l	#-2,D1

	movea.l	D1,SP
	move.l	D0,-(SP)
	move.l	A5,-(SP)
	clr.w	-(SP)
	move.w	#$4A,-(SP)
	trap	#1		; Mshrink
	lea	12(SP),SP

	jsr	main

	move.w	D0,-(SP)
	move.w	#$4C,-(SP)
	trap	#1		; Pterm
