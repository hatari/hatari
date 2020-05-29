; A very minimalistic TOS ROM replacement, used for testing without real TOS

	org	$e00000

TEST_PRG_BASEPAGE equ $1000

rom_header:
	bra.s	start			; Branch to 0xe00030
	dc.w	$0001			; TOS version
	dc.l	start			; Reset PC value
	dc.l	rom_header		; Pointer to ROM header
	dc.l	TEST_PRG_BASEPAGE	; End of OS BSS
	dc.l	start			; Reserved
	dc.l	$0			; Unused (GEM's MUPB)
	dc.l	$03032018		; Fake date
	dc.w	$0001			; PAL flag
	dc.w	$4c63			; Fake DOS date
	dc.l	$00000880		; Fake pointer 1 (mem pool)
	dc.l	$00000870		; Fake pointer 2 (key shift)
	dc.l	$00000800		; Addr of basepage var
	dc.l	$0			; Reserved
start:
	move	#$2700,sr
	reset
	move.b	#5,$ffff8001.w		; Fake memory config
	lea	$20000,sp		; Set up SSP

	lea	unhandled_error(pc),a1
	movea.w	#8,a0			; Start with bus error handler
set_exc_loop:
	move.l	a1,(a0)+		; Set all exception handlers
	cmp.w	#$1c0,a0
	ble.s	set_exc_loop

	lea	rte_only(pc),a1
	move.l	a1,$68			; Ignore HBLs
	move.l	a1,$72			; Ignore VBLs

	lea	$fffffa00.w,a0
	move.b	#$48,17(a0)		; Configure MFP vector base

	lea	$fffffc00.w,a0
	move.b	#3,(a0)			; Reset ACIA
	move.b	#$16,(a0)		; Configure ACIA

	lea	$fa0000,a0
	cmp.l	#$abcdef42,(a0)		; Cartridge enabled?
	bne.s	no_sys_init
	dc.w	$a			; Call SYSINIT_OPCODE to init trap #1
no_sys_init:

	moveq	#0,d0
	movea.l	d0,a0
	movea.l	d0,a1
	move	#$0700,sr		; Go to user mode
	lea	$18000,sp		; Set up USP
	pea	TEST_PRG_BASEPAGE.w
	pea	rom_header(pc)
	jmp	TEST_PRG_BASEPAGE+$100.w


unhandled_err_txt:
	dc.b	"ERROR: Unhandled exception!",13,10,0
	even

unhandled_error:
	pea	unhandled_err_txt(pc)
	move.w  #9,-(sp)
	trap    #1		; Cconws
	addq.l  #6,sp

	move.w	#1,-(sp)
	move.w	#76,-(sp)
	trap	#1		; Pterm

rte_only:
	rte
