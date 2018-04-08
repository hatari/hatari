; Test Hatari's XBIOS functions (which can be enabled with --bios-intercept).
; Assemble this code with TurboAss.

	movea.l 4(SP),A5        ; Get pointer to basepage
	move.l  $0C(A5),D0      ; Text segment length
	add.l   $14(A5),D0      ; Data segment length
	add.l   $1C(A5),D0      ; BSS segment length
	add.l   #$0800,D0       ; Space for the stack

	move.l  D0,D1
	add.l   A5,D1
	and.l   #-2,D1

	movea.l D1,SP
	move.l  D0,-(SP)
	move.l  A5,-(SP)
	clr.w   -(SP)
	move.w  #$4A,-(SP)
	trap    #1              ; Mshrink
	lea     12(SP),SP


; Test XBIOS 255 - Send Hatari control command
	pea     hatcontrol(PC)
	move.w  #255,-(SP)
	trap    #14
	addq.l  #6,SP


; Test XBIOS 11 - the Atari debugger XBIOS call
; See:
; - http://dev-docs.atariforge.org/files/Atari_Debugger_1-24-1990.pdf
; - http://toshyp.atari.org/en/004013.html#Dbmsg

	; Print string (with length encoded in msg_num), and invoke debugger
	pea     dbmsg_start(PC)
	move.w  #$F000+dbmsg_end-dbmsg_start,-(SP)
	move.w  #5,-(SP)
	move.w  #11,-(SP)
	trap    #14
	lea     10(SP),SP

	; Print NUL-terminated string
	pea     dbmsg_nul(PC)
	move.w  #$F000,-(SP)
	move.w  #5,-(SP)
	move.w  #11,-(SP)
	trap    #14
	lea     10(SP),SP

	; Print given value and invoke debugger
	move.l  #$DEADC0DE,-(SP)
	move.w  #$1234,-(SP)
	move.w  #5,-(SP)
	move.w  #11,-(SP)
	trap    #14
	lea     10(SP),SP


	clr.w   -(SP)
	trap    #1              ; Pterm0


	DATA

hatcontrol:
	DC.B "hatari-debug evaluate 23 + 19",0

dbmsg_start:
	DC.B "This is a Dbmsg test for a string with fixed size."
dbmsg_end:

dbmsg_nul:
	DC.B "This is a Dbmsg test for a NUL-terminated string.",0

	EVEN

	END
