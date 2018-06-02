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
	lea	$fa0000,a3
	cmp.l	#$abcdef42,(a3)		; Cartridge enabled?
	bne.s	no_sys_init
	dc.w	$a			; Yes, so we can call SYSINIT_OPCODE
no_sys_init:
	move	#$0700,sr		; Go to user mode
	lea	$18000,sp		; Set up USP
	suba.l	a0,a0
	pea	TEST_PRG_BASEPAGE
	pea	rom_header
	jmp	TEST_PRG_BASEPAGE+$100
