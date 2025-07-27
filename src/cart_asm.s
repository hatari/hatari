; Cartridge assembler code.
; 68000 code that is used for starting programs from the emulated GEMDOS harddisk
; and for using bigger VDI resolutions

; See cartData.c for instruction to compile this file

	; Force pc relative mode
	opt	a+


; Hatari's "illegal" (free) opcodes:
GEMDOS_OPCODE		equ 8
PEXEC_OPCODE		equ 9
SYSINIT_OPCODE		equ 10
VDI_OPCODE		equ 12

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

	even

old_gemdos:		ds.l	1		; has to match the CART_OLDGEMDOS define!
vdi_opcode:		dc.w	VDI_OPCODE	; Address to call after Trap #2 (VDI), causes illegal instruction

; New GEMDOS vector (0x84)
new_gemdos:
	dc.w	GEMDOS_OPCODE	; Returns NEG as run old vector, ZERO to return or OVERFLOW to run pexec
	bvs.s	pexec
	bne.s	go_oldgemdos
do_rte:
	rte

; Branch to old GEMDOS
go_oldgemdos:
	move.l	old_gemdos(pc),-(sp)	; Set PC to 'old_gemdos' and continue execution, WITHOUT corrupting registers!
	rts

pexec:
	; GemDOS_Pexec() pushed the parameters onto the stack already
	trap	#1
	lea	16(sp),sp
	tst.l	d0
	bmi.s	do_rte
	dc.w	PEXEC_OPCODE
	bvs.s	go_oldgemdos
	beq.s	do_rte

	; If we end up here, we've got to clean up
	move.l	d0,-(sp)
	move.l	a0,-(sp)
	move.w	#73,-(sp)	; Mfree
	trap	#1
	addq.l	#6,sp
	move.l	(sp)+,d0
	rte

; This code is called during TOS' boot sequence.
; It gets a pointer to the Line-A variables and uses an illegal opcode
; to run our system initialization code in OpCode_SysInit().
sys_init:
	dc.w	$A000			; Line-A init (needed for VDI resolutions)
	dc.w	SYSINIT_OPCODE	; Illegal opcode to call OpCode_SysInit()
	bne.s	sys_init_done
	; If we provide TT RAM in Falcon mode, we have to install a _FRB
	; cookie, too. Reserve some RAM with Mxalloc() for this.
	clr.w	-(sp)			; ST-RAM only
	move.l	#65536 + 512,-(sp)	; Size
	move.w	#68,-(sp)
	trap	#1			; Mxalloc
	addq.l	#8,sp
	tst.l	d0
	beq.s	sys_init_done
	add.l	#511,d0
	and.l	#$fffffe00,d0		; Align buffer to 512 byte boundary
	move.l	$5a0.s,a0		; Get cookie jar pointer
	; Since we run this with TOS 4.x only, we can assume that the jar is
	; available and that there is at least one entry in the jar already
cj_loop:
	addq.l	#8,a0
	tst.l	(a0)
	bne.s	cj_loop
	move.l	4(a0),d1		; Jar size
	beq.s	sys_init_done
	move.l	#'_FRB',(a0)+		; Install the _FRB cookie
	move.l	d0,(a0)+
	clr.l	(a0)+			; New cookie jar end marker
	move.l	d1,(a0)+
sys_init_done:
	rts



; This code is run when the user starts the HATARI.PRG
; in the cartridge. It simply displays some information text.
infoprgstart:
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

infoprgend:

	END
