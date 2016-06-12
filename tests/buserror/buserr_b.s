; Bus error tester - byte access
; (assemble with TurboAss)

startaddr       EQU $FF8000
endaddr         EQU $FFFFFF


                movea.l 4(SP),A5
                move.l  $0C(A5),D0
                add.l   $14(SP),D0
                add.l   $1C(SP),D0
                add.l   #$0800,D0

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

                clr.l   -(SP)
                move.w  #$20,-(SP)
                trap    #1              ; Super
                addq.l  #6,SP
                move.l  D0,old_ssp

                clr.w   -(SP)
                pea     filename
                move.w  #$3C,-(SP)
                trap    #1              ; Fcreate
                addq.l  #8,SP
                move.w  D0,fhndl

                pea     welcometxt
                move.l  #welcometxtend-welcometxt-1,-(SP)
                move.w  D0,-(SP)
                move.w  #$40,-(SP)
                trap    #1              ; Fwrite(welcometxt)
                lea     12(SP),SP

                move.l  $08,oldbushandler
                move.l  #buserrcatch,$08

                movea.l #startaddr,A5

loop:
                movea.l SP,A6

                move.b  (A5),D0         ; *** Try to generate a bus error ***

                tst.w   be_region
                beq.s   cont
                lea     hex_buf+10,A0
                bsr     write_hex
                clr.w   be_region

                pea     hex_buf
                move.l  #19,-(SP)
                move.w  fhndl,-(SP)
                move.w  #$40,-(SP)
                trap    #1              ; Fwrite
                lea     12(SP),SP

cont:
                addq.l  #1,A5
                cmpa.l  #endaddr,A5
                bne.s   loop

                tst.w   be_region
                beq.s   nofinalwrite
                lea     hex_buf+10,A0
                bsr     write_hex
                pea     hex_buf
                move.l  #19,-(SP)
                move.w  fhndl,-(SP)
                move.w  #$40,-(SP)
                trap    #1              ; Fwrite
                lea     12(SP),SP

nofinalwrite:
                move.l  oldbushandler,$08

                pea     eoftxt
                move.l  #8,-(SP)
                move.w  fhndl,-(SP)
                move.w  #$40,-(SP)
                trap    #1              ; Fwrite(eoftxt)
                lea     12(SP),SP


                move.w  fhndl,-(SP)
                move.w  #$3E,-(SP)
                trap    #1              ; Fclose
                addq.l  #4,SP

                move.l  old_ssp,-(SP)
                move.w  #$20,-(SP)
                trap    #1              ; Super
                addq.l  #6,SP

                clr.w   -(SP)
                trap    #1              ; Pterm0


buserrcatch:
                movea.l A6,SP
                tst.w   be_region
                bne     cont
                move.w  #-1,be_region
                lea     hex_buf,A0
                bsr.s   write_hex
                bra     cont

write_hex:
                move.l  A5,D1
                and.b   #$0F,D1
                cmp.b   #10,D1
                bge.s   b6_10
                add.b   #'0',D1
                bra.s   b6_ok
b6_10:          add.b   #'A'-10,D1
b6_ok:          move.b  D1,6(A0)

                move.l  A5,D1
                lsr.b   #4,D1
                and.b   #$0F,D1
                cmp.b   #10,D1
                bge.s   b5_10
                add.b   #'0',D1
                bra.s   b5_ok
b5_10:          add.b   #'A'-10,D1
b5_ok:          move.b  D1,5(A0)

                move.l  A5,D1
                lsr.w   #8,D1
                and.b   #$0F,D1
                cmp.b   #10,D1
                bge.s   b4_10
                add.b   #'0',D1
                bra.s   b4_ok
b4_10:          add.b   #'A'-10,D1
b4_ok:          move.b  D1,4(A0)

                move.l  A5,D1
                move.w  #12,D2
                lsr.w   D2,D1
                and.b   #$0F,D1
                cmp.b   #10,D1
                bge.s   b3_10
                add.b   #'0',D1
                bra.s   b3_ok
b3_10:          add.b   #'A'-10,D1
b3_ok:          move.b  D1,3(A0)

                move.l  A5,D1
                move.w  #16,D2
                lsr.l   D2,D1
                and.b   #$0F,D1
                cmp.b   #10,D1
                bge.s   b2_10
                add.b   #'0',D1
                bra.s   b2_ok
b2_10:          add.b   #'A'-10,D1
b2_ok:          move.b  D1,2(A0)

                move.l  A5,D1
                move.w  #20,D2
                lsr.l   D2,D1
                and.b   #$0F,D1
                cmp.b   #10,D1
                bge.s   b1_10
                add.b   #'0',D1
                bra.s   b1_ok
b1_10:          add.b   #'A'-10,D1
b1_ok:          move.b  D1,1(A0)

                rts



                DATA
filename:
                DC.B "BUSERR_B.TXT",0

welcometxt:
                DC.B "Bus Error testing results (byte access):",13,10,13,10,0
welcometxtend:

eoftxt:
                DC.B 13,10,"EOF!",13,10,0

hex_buf:        DC.B "$000000 - $000000",13,10,0
                EVEN

be_region:      DC.W 0


                BSS
old_ssp:
                DS.L 1

oldbushandler:
                DS.L 1

fhndl:
                DS.W 1
                END
