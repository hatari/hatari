; Cartridge assembler code.
; 68000 code that is used for starting programs from the emulated GEMDOS harddisk
; and for using bigger VDI resolutions

; Hatari's "illegal" (free) opcodes:
GEMDOS_OPCODE		equ	8
SYSINIT_OPCODE		equ 10
VDI_OPCODE			equ	12

; System variables:
_longframe		equ $059E


	org	$fa0000


; This is the cartridge header:
	dc.l	$ABCDEF42					; C-FLAG (magic value)
	dc.l	$00000000					; C-NEXT
	dc.l	sys_init+$08000000			; C-INIT - flag has bit 3 set = before disk boot, but after GEMDOS init
	dc.l	infoprgstart				; C-RUN
	dc.w	%0101100000000000			; C-TIME
	dc.w	%0011001000101001			; C-DATE
	dc.l	infoprgend-infoprgstart		; C-BSIZ, offset: $14
	dc.b	'HATARI.TOS',0,0			; C-NAME

	.even


old_gemdos:		ds.l	1			; has to match the CART_OLDGEMDOS define!
vdi_opcode:		dc.w	VDI_OPCODE	; Address to call after Trap #2 (VDI), causes illegal instruction

; New GemDOS vector (0x84) - for intercepting Pexec
new_gemdos:
	dc.w	GEMDOS_OPCODE	; Returns NEG as run old vector, ZERO to return or OVERFLOW to run pexec
	bvs.s	pexec
	bne.s	go_oldgemdos
	rte

; Branch to old GemDOS
go_oldgemdos:
	move.l	old_gemdos(pc),-(sp)	; Set PC to 'old_gemdos' and continue execution, WITHOUT corrupting registers!
	rts

; Progam Execute
pexec:
	move	usp,a0		; Parameters on user stack pointer?
	btst	#5,(sp)		; Check if program was in user or supervisor mode
	beq.s	p_ok
	lea 	6(sp),a0	; Parameters are on SSP
	tst.w	_longframe.w	; Do we use a CPU > 68000?
	beq.s	p_ok		; No: A0 is OK
	addq	#2,a0		; Skip 2 additional stack frame bytes on CPUs >= 68010
p_ok:
	addq	#2,a0		; Skip GEMDOS function number
	tst		(a0)		; Test pexec mode
	bne.s	no_0

	; Simulate pexec mode 0
	move.l	a6,-(sp)
	move.l	a0,a6
	bsr.s	find_prog
	bsr	load_n_reloc
	clr.l	2(a6)
	clr.l	10(a6)
	move.l	d0,6(a6)

	move.w	#48,-(sp)	; Sversion: get GEMDOS version
	trap	#1		; call GEMDOS
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
	bsr.s	load_n_reloc
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


load_n_reloc:
	movem.l	a3-a5/d6,-(sp)

	clr.w	-(sp)
	move.l	2(a6),-(sp)
	move.w	#$3d,-(sp)	; Fopen
	trap	#1		; Gemdos
	addq	#8,sp
	move.l	d0,d6		; Keep file handle in d6
	tst.l	d0
	bmi	lr_err_out

	clr.w	-(sp)		; Temporary space for prg header magic
	move.l	sp,-(sp)
	move.l	#2,-(sp)
	move	d6,-(sp)
	move	#$3f,-(sp)	; Fread
	trap	#1		; Gemdos: Read header magic
	lea 	12(sp),sp
	move.w	(sp)+,d1
	cmp.l	#2,d0
	bne	load_reloc_error

	cmp.w	#$601a,d1	; Check program header magic
	beq.s	hdr_magic_ok
	move.l	#-66,d0		; Error code: Invalid PRG format
	bra	load_reloc_error
hdr_magic_ok:
	clr.w	-(sp)
	move.w	d6,-(sp)
	move.l	#22,-(sp)	; offset of the program flags
	move.w	#66,-(sp)	; Fseek
	trap	#1		; Gemdos: Seek to program flags
	lea	10(sp),sp
	cmp.l	#22,d0
	bne	load_reloc_error

	clr.l	-(sp)		; Temporary space for program flags
	move.l	sp,-(sp)
	move.l	#4,-(sp)
	move	d6,-(sp)
	move	#$3f,-(sp)	; Fread
	trap	#1		; Gemdos: Read program flags
	lea 	12(sp),sp
	move.l	(sp)+,a3	; Program flags now in a3
	cmp.l	#4,d0
	bne	load_reloc_error

	; Let's call Pexec now to create the basepage, first try
	; Pexec(7) and if that does not work fall back to mode 5
	move.l	10(a6),-(sp)
	move.l	6(a6),-(sp)
	move.l	a3,-(sp)	; program flags in program header
	move.w	#7,-(sp)	; Create basepage wrt program flags
	move.w	#$4b,-(sp)	; Pexec (mode 7)
	trap	#1		; Gemdos
	lea	16(sp),sp
	tst.l	d0
	bpl.s	pexec_ok

	move.l	10(a6),-(sp)
	move.l	6(a6),-(sp)
	clr.l	-(sp)
	move.w	#5,-(sp)	; Create basepage
	move.w	#$4b,-(sp)	; Pexec (mode 5)
	trap	#1		; Gemdos
	lea	16(sp),sp
	tst.l	d0
	bmi	load_reloc_error

pexec_ok:
	move.l	d0,a5		; Basepage in a5

	clr.w	-(sp)
	move.w	d6,-(sp)
	clr.l	-(sp)		; Back to the start of the file
	move.w	#66,-(sp)	; Fseek
	trap	#1		; Gemdos: Seek to start of the file
	lea	10(sp),sp
	tst.l	d0
	bne	lr_err_free

	lea	256(a5),a3	; a3 points now to the program header
	move.l	a3,-(sp)
	move.l	#$1c,-(sp)
	move	d6,-(sp)
	move	#$3f,-(sp)	; Fread
	trap	#1		; Gemdos: Read full program header
	lea 	12(sp),sp
	cmp.l	#$1c,d0
	bne	lr_err_free

	lea	8(a5),a4
	move.l	a5,d0
	add.l	#$100,d0
	move.l	d0,(a4)+	; text start
	move.l	2(a3),d0
	move.l	d0,(a4)+	; text length
	add.l	8(a5),d0
	move.l	d0,(a4)+	; data start
	move.l	6(a3),(a4)+	; data length
	add.l	6(a3),d0
	move.l	d0,(a4)+	; bss start
	move.l	10(a3),(a4)+	; bss length

	add.l	10(a3),d0
	cmp.l	4(a5),d0	; is the TPA big enough?
	bhi	tpa_not_ok

	move.l	a5,d0
	add.l	#$80,d0
	move.l	d0,32(a5)	; default DTA always points to cmd line space!

	move.l	24(a5),a4
	add.l	14(a3),a4	; add symtab length => a4 points to reloc table
	move.w	26(a3),a3	; a3 is now the absflag (0 means reloc)

	pea	256(a5)
	pea	$7fffffff
	move	d6,-(sp)
	move	#$3f,-(sp)	; Fread
	trap	#1		; Gemdos
	lea	12(sp),sp

	move	d6,-(sp)
	move	#$3e,-(sp)	; Fclose
	trap	#1		; Gemdos
	addq	#4,sp

	move.w	a3,d1
	tst.w	d1		; check absflag
	bne.s	relocdone

	move.l	8(a5),a3
	move.l	a3,d0

	; Get first offset of the relocation table. Since A4 seems sometimes not
	; to be word aligned (if symbol table length is uneven), we have to read
	; byte by byte...
	move.b	(a4),d1
	clr.b	(a4)+
	lsl.w	#8,d1
	move.b	(a4),d1
	clr.b	(a4)+
	swap	d1
	move.b	(a4),d1
	clr.b	(a4)+
	lsl.w	#8,d1
	move.b	(a4),d1
	clr.b	(a4)+

	tst.l	d1
	beq.s	relocdone
	adda.l	d1,a3
	moveq	#0,d1
relloop0:
	add.l	d0,(a3)
relloop:
	move.b	(a4),d1
	clr.b	(a4)+	; Some programs like GFA-Basic expect a clear memory
	tst.b	d1
	beq.s	relocdone
	cmp.b	#1,d1
	bne.s	no254
	lea 	254(a3),a3
	bra.s	relloop
no254:
	adda.w	d1,a3
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
	movem.l	(sp)+,a3-a5/d6
	rts

tpa_not_ok:
	move.l	#-39,d0		; Error code: Not enough memory
lr_err_free:
	move.l	d0,-(sp)
	move.l	a5,-(sp)
	move.w	#$49,-(sp)	; Mfree
	trap	#1		; Release "pexeced" memory
	addq.l	#6,sp
	move.l	(sp)+,d0
load_reloc_error:
	move.w	d6,-(sp)
	move.l	d0,d6		; Save error code
	move.w	#$3e,-(sp)	; Fclose
	trap	#1		; Gemdos
	addq	#4,sp
	move.l	d6,d0		; Restore error code
lr_err_out:
	movem.l	(sp)+,a3-a5/d6
	addq	#4,sp		; Drop return address
	bra	gohome		; Abort



; This code is called during TOS' boot sequence.
; It gets a pointer to the Line-A variables and uses an illegal opcode
; to run our system initialization code in OpCode_SysInit().
sys_init:
	dc.w	$A000			; Line-A init (needed for VDI resolutions)
	dc.w	SYSINIT_OPCODE	; Illegal opcode to call OpCode_SysInit()
	rts



; This code is run when the user starts the HATARI.PRG
; in the cartridge. It simply displays some information text.
infoprgstart:
	pea 	hatarix32(pc)
	move.w	#32,-(sp)
	trap	#14				; Dosound - play some music :-)
	addq.l	#6,sp

	pea 	infotext(pc)
	move.w	#9,-(sp)
	trap	#1				; Cconws - display the information text
	addq.l	#6,sp

	move.w	#7,-(sp)
	trap	#1				; Crawcin - wait for a key
	addq.l	#2,sp

	clr.w	-(sp)
	trap	#1				; Pterm0


infotext:
	dc.b	27,'E',13,10
	dc.b	'        =========================',13,10
	dc.b	'        Hatari keyboard shortcuts',13,10
	dc.b	'        =========================',13,10
	dc.b	13,10
	dc.b	' F11 : toggle fullscreen/windowed mode',13,10
	dc.b	' F12 : activate the setup GUI of Hatari',13,10
	dc.b	13,10
	dc.b	'All other shortcuts are activated by',13,10
	dc.b	'pressing AltGr or Right-Alt or Meta key',13,10
	dc.b	'together with one of the following keys:',13,10
	dc.b	13,10
	dc.b	' a : Record animation',13,10
	dc.b	' g : Grab a screenshot',13,10
	dc.b	' i : Leave full screen & iconify window',13,10
	dc.b	' j : joystick via key joystick on/off',13,10
	dc.b	' m : mouse grab',13,10
	dc.b	' r : warm reset of the ST',13,10
	dc.b	' c : cold reset of the ST',13,10
	dc.b	' s : enable/disable sound',13,10
	dc.b	' q : quit the emulator',13,10
	dc.b	' x : toggle normal/max speed',13,10
	dc.b	' y : enable/disable sound recording',13,10
	dc.b	0


hatarix32:
	ibytes	'cart_mus.x32'


infoprgend:

	END
