; 68000 code that is used for starting programs from the emulated GEMDOS harddisk


	org	$fa1000

GEMDOS_OPCODE		equ	8		; Free op-code!
VDI_OPCODE			equ	12		; Free op-code!


harddrive_boot:	dc.l	0			; unused
old_gemdos:		ds.l	1
vdi_opcode:		dc.w	VDI_OPCODE	; Address to call after Trap #2(VDI), causes illegal instruction

;New GemDOS vector (0x84)
new_gemdos:
	dc.w	GEMDOS_OPCODE	; Returns NEG as run old vector, ZERO to return or OVERFLOW to run pexec
	bvs.s	pexec
	bne.s	go_oldgemdos
	rte

;Branch to old GemDOS
go_oldgemdos:
	move.l	old_gemdos(pc),-(sp)	;Set PC to 'old_gemdos' and continue execution, WITHOUT corrupting registers!
	rts

;Progam Execute
pexec:
	lea		8(sp),a0
	btst	#5,(sp)
	bne.s	s_ok
	move.l	usp,a0
	addq	#2,a0
s_ok:
	tst		(a0)		; Test pexec mode
	bne.s	no_0

	; Simulate pexec mode 0
	move.l	a6,-(sp)
	move.l	a0,a6
	move.l	a6,-(sp)	;new
	bsr.s	find_prog
	move.l	(sp)+,a6	;new
	bsr		pexec5
	bsr		reloc
	clr.l	2(a6)
	clr.l	10(a6)
	move.l	d0,6(a6)

	move.w	#48,-(sp)	; Sversion: get GEMDOS version
	trap	#1			; call GEMDOS
	addq	#2,sp
	ror.w	#8,d0		; Major version to high, minor version to low byte
	cmp.w	#$0015,d0
	bge.s	use_gemdos_015
	move.w	#4,(a6)		; pexec mode 4 for exec. prepared program
	bra.s	mode0_ok
use_gemdos_015:
	move.w	#6,(a6)		; On GEMDOS 0.15 and higher, we can use mode 6
mode0_ok:

	move.l	(sp)+,a6
	bra.s	go_oldgemdos

no_0:
	cmp		#3,(a0)
	bne.s	go_oldgemdos

	; Simulate pexec mode 3
	move.l	a6,-(sp)
	move.l	a0,a6
	bsr.s	find_prog
	bsr.s	pexec5
	bsr.s	reloc
gohome:
	move.l	(sp)+,a6
	rte

find_prog:
	move	#$2f,-(sp)	; Fgetdta
	trap	#1		; Gemdos
	addq	#2,sp
	move.l	d0,a0
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	(a0)+,-(sp)
	move.l	a0,-(sp)
	move	#$17,-(sp)
	move.l	2(a6),-(sp)
	move	#$4e,-(sp)	; Fsfirst
	trap	#1		; Gemdos
	addq	#8,sp
	move.l	(sp)+,a0
	move.l	(sp)+,-(a0)
	move.l	(sp)+,-(a0)
	move.l	(sp)+,-(a0)
	move.l	(sp)+,-(a0)
	move.l	(sp)+,-(a0)
	move.l	(sp)+,-(a0)
	move.l	(sp)+,-(a0)
	move.l	(sp)+,-(a0)
	move.l	(sp)+,-(a0)
	move.l	(sp)+,-(a0)
	move.l	(sp)+,-(a0)
	tst.l	d0
	beq.s	findprog_ok
	addq	#4,sp
	bra.s	gohome
findprog_ok:
	rts

pexec5:
	move.l	10(a6),-(sp)
	move.l	6(a6),-(sp)
	clr.l	-(sp)
	move	#5,-(sp)
	move	#$4b,-(sp)	; Pexec
	trap	#1		; Gemdos
	lea		16(sp),sp
	tst.l	d0
	bmi.s	pexecerr
	rts
pexecerr:
	addq	#4,sp
	bra.s	gohome

reloc:
	movem.l	a3-a5/d6-d7,-(sp)
	move.l	d0,a5
	clr		-(sp)
	move.l	2(a6),-(sp)
	move	#$3d,-(sp)	; Fopen
	trap	#1		; Gemdos
	addq	#8,sp
	move.l	d0,d6
	move.l	a5,-(sp)
	add.l	#228,(sp)
	pea		$1c.w
	move	d6,-(sp)
	move	#$3f,-(sp)	; Fread
	trap	#1		; Gemdos
	lea		12(sp),sp
; check size!!
	move.l	a5,-(sp)
	add.l	#256,(sp)
	pea		$7fffffff
	move	d6,-(sp)
	move	#$3f,-(sp)	; Fread
	trap	#1		; Gemdos
	lea		12(sp),sp
	move	d6,-(sp)
	move	#$3e,-(sp)	; Fclose
	trap	#1		; Gemdos
	addq	#4,sp
	lea		8(a5),a4
	move.l	a5,d0
	add.l	#$100,d0
	move.l	d0,(a4)+		; text start
	move.l	230(a5),d0
	move.l	d0,(a4)+		; text length
	add.l	8(a5),d0		; data start
	move.l	d0,(a4)+
	move.l	234(a5),(a4)+	; data length
	add.l	234(a5),d0
	move.l	d0,(a4)+		; bss start
	move.l	238(a5),(a4)+	; bss length
	move.l	a5,d0
	add.l	#$80,d0
	move.l	d0,32(a5)
	move.l	24(a5),a4
	add.l	242(a5),a4		; symbol table length
	move.l	8(a5),a3
	move.l	a3,d0
	tst.w	254(a5)
	bne.s	relocdone

	; Get first offset of the relocation table. Since A4 seems sometimes not
	; to be word aligned (if symbol table length is uneven), we have to read
	; byte by byte...
	move.b	(a4),d7
	clr.b	(a4)+
	lsl.w	#8,d7
	move.b	(a4),d7
	clr.b	(a4)+
	swap	d7
	move.b	(a4),d7
	clr.b	(a4)+
	lsl.w	#8,d7
	move.b	(a4),d7
	clr.b	(a4)+

	tst.l	d7
	beq.s	relocdone
	adda.l	d7,a3
	moveq	#0,d7
relloop0:
	add.l	d0,(a3)
relloop:
	move.b	(a4),d7
	move.b	#$00,(a4)+		; Some programs like GFA-Basic expect a clear memory
	tst.b	d7
	beq.s	relocdone
	cmp.b	#1,d7
	bne.s	no254
	lea 	254(a3),a3
	bra.s	relloop
no254:
	adda.w	d7,a3
	bra.s	relloop0

relocdone:
	move.l	28(a5),d0
	beq.s	cleardone
	move.l	24(a5),a0
clear:
	clr.b	(a0)+
	subq.l	#1,d0
	bne.s	clear
cleardone:
	move.l	a5,d0
	movem.l	(sp)+,a3-a5/d6-d7
	rts
