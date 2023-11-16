#!/usr/bin/env python3
#
# code to generate m68k instruction opcode mask & value lookup table as C format
# and a readline enabled console to get Hatari debugger breakpoint statements
# matching the given instruction + some information about what the opcode bits
# mean.
#
# m68k instruction information is based on HiSoft MC68000/68008/68010/68012
# Pocket Programming Guide.
# 
# Copyright (C) 2011 by Eero Tamminen <oak at helsinkinet fi>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

import os, sys
import readline


class Instructions:
    # o, s are the important ones (in addition to 0/1 instruction specific bits)
    bittypes = {
    's': "size (byte/word/long)",
    'i': "dynamic, not immediate data",
    'x': "source register",
    'y': "destination register",
    'r': "data / address register",
    'v': "data value",
    'o': "op-mode",
    'm': "effective address mode",
    'a': "effective address register",
    't': "instruction type / direction",
    'c': "condition / count",
    'p': "displacement",
    'b': "breakpoint / trap vector",
    }

    # if condition (!, <, >) isn't prefixed by bit type character (from above),
    # take the given value as-is, otherwise shift it so that it matches given
    # bit types (o or s) position and get mask for that bit type from the info.
    # 
    # - 'and' may match also 'abcd'
    # - 'and' & 'anda' would need exact o values to not match
    # - 'exg' would need second o!10000 condition to avoid matching 'abcd'
    # - 'or' may match also 'sbcd'
    #
    # 5432109876543210, extra condition, instruction, condition notes:
    data = """
1100yyy10000txxx - abcd
1101rrrooommmaaa - add
1101rrrooommmaaa o>010 adda
00000110ssmmmaaa - addi
0101rrr0ssmmmaaa s!11 addq
1101yyy1ss00txxx - addx
1100rrrooommmaaa !0xc0 and
00000010ssmmmaaa - andi
1110ccc1ssi00rrr - asl register
1110000111mmmaaa - asl memory
1110ccc0ssi00rrr - asr register
1110000011mmmaaa - asr memory
0110ccccpppppppp c>0001 bcc
0000rrr101mmmaaa - bchg dynamic
0000100001mmmaaa - bchg static
0000rrr110mmmaaa - bclr dynamic
0000100010mmmaaa - bclr static
0100100001001bbb - bkpt
01100000pppppppp - bra
0000rrr111mmmaaa - bset dynamic
0000100011mmmaaa - bset static
01100001pppppppp - bsr
0000rrr100mmmaaa - btst dynamic
0000100000mmmaaa - btst static
0100rrr110mmmaaa - chk
01000010ssmmmaaa - clr
1011rrrooommmaaa o<011 cmp
1011rrrooommmaaa o>010 cmpa
00001100ssmmmaaa - cmpi
1011yyy1ss001xxx - cmpm
0101cccc11001rrr - dbcc
1000rrr111mmmaaa - divs
1000rrr011mmmaaa - divu
1011rrrooommmaaa o>011 eor
00001010ssmmmaaa - eori
1100yyy1oooooxxx o<10111 exg
0100100ooo000rrr o>001 ext
0100101011111100 - illegal
0100111011mmmaaa - jmp
0100111010mmmaaa - jsr
0100rrr111mmmaaa - lea
0100111001010rrr - link
1110ccc1ssi01rrr - lsl register
1110001111mmmaaa - lsl memory
1110ccc0ssi01rrr - lsr register
1110001011mmmaaa - lsr memory
00ssyyymmmmmmxxx s>00 move
00ssyyy001mmmxxx s>01 movea
010011100110trrr - moveusp
010011100111101t - movec
01001t001smmmaaa - movem
0000rrrooo001rrr o>011 movep
0111rrr0vvvvvvvv - moveq
00001110ssmmmaaa - moves
1100rrr111mmmaaa - muls
1100rrr011mmmaaa - mulu
0100100000mmmaaa - nbcd
01000100ssmmmaaa - neg
01000000ssmmmaaa - negx
0100111001110001 - nop
01000110ssmmmaaa - not
1000rrrooommmaaa !0xc0 or
00000000ssmmmaaa - ori
0100100001mmmaaa - pea
0100111001110000 - reset
1110ccc1sst11rrr - rol register
1110011111mmmaaa m>001 rol memory
1110ccc0sst11rrr - ror register
1110011011mmmaaa m>001 ror memory
1110ccc1sst10rrr - roxl register
1110010111mmmaaa m>001 roxl memory
1110ccc0sst10rrr - roxr register
1110010011mmmaaa m>001 roxr memory
0100111001110100 - rtd
0100111001110011 - rte
0100111001110111 - rtr
0100111001110101 - rts
1000yyy10000txxx - sbcd
0101cccc11mmmaaa - scc
0100111001110010 - stop
1001rrrooommmaaa - sub
1001rrrooommmaaa o>010 suba
00000100ssmmmaaa - subi
0101rrr1ssmmmaaa - subq
1001yyy1ss00txxx - subx
0100100001000rrr - swap
0100101011mmmaaa - tas
010011100100bbbb - trap
0100111001110110 - trapv
01001010ssmmmaaa s<11 tst
0100111001011rrr - unlink
"""

    def __init__(self):
        self.items = self.parse_data()

    def show(self, name):
        # TODO: handle dynamic/static & register/memory variants
        item = None
        for i in self.items:
            if i['name'] == name:
                item = i
                break
        if not item:
            print("ERROR: unknown instruction '%s'" % name)
            return
        print()
        print("Instruction:")
        print("  %(name)s - %(info)s" % item)
        specialbits = item['mask'] != 0xffff
        if specialbits:
            print("Bits:")
            oldchar = None
            for i in item['info']:
                if i in self.bittypes and i != oldchar:
                    print("  - %c: %s" % (i, self.bittypes[i]))
                oldchar = i

        print("Hatari breakpoint:")
        print("  b  (pc).w", end=' ')
        if specialbits:
            print("& $%(mask)x" % item, end=' ')
        print("= $%(bits)x" % item, end=' ')
        if item['op'] != '-':
            print(" &&  (pc).w & $%(bmask)x %(op)c $%(bvalue)x" % item, end=' ')
        print()
        print()
        

    def get_names(self):
        return [x['name'] for x in self.items]

    def parse_data(self):
        items = []
        for line in self.data.split(os.linesep):
            line = line.split('#')[0]  # remove comment
            line.strip()
            if not line:
                continue
            splitted = line.split()
            info = splitted[0]
            bits, mask = self.parse_bits(info)
            bmask, op, bvalue = self.parse_cond(info, splitted[1])
            name = " ".join(splitted[2:])
            items.append({'bits':bits, 'mask':mask, 'info':info, 'bmask':bmask, 'op': op, 'bvalue':bvalue, 'name':name})
        return items


    def parse_bits(self, bitstr):
        "bitstr = types of bits for instruction"
        if len(bitstr) != 16:
            print("ERROR: '%s' doesn't represent 16 bits" % bitstr)
            sys.exit(1)
        bits = 0
        mask = 0
        for bit in bitstr:
            if bit in ('0','1'):
                bits = bits << 1 | int(bit)
                mask = mask << 1 | 1
            else:
                bits <<= 1
                mask <<= 1
        return bits, mask


    def parse_cond(self, bitstr, condstr):
        "bitstr = types of bits for instruction, condstr = <bit type char><condition char><bits>"
        condmask = 0
        condshift = 0
        condtype = condstr[0]

        # mask & value are given directly?
        if condtype != '-' and condtype not in self.bittypes:
            condmask = int(condstr[1:], 16)
            return condmask, condtype, condmask

        for bit in bitstr:
            if bit in ('0','1'):
                condmask <<= 1
                condshift += 1
            else:
                if bit == condtype:
                    condmask = condmask << 1 | 1
                    condshift = 0
                else:
                    condmask <<= 1
                    condshift += 1

        if not condmask:
            return 0, "-", 0
        
        # otherwise use mask & value position from bitstr string
        condbits = 0
        if condstr != "-":
            condop = condstr[1]
            for bit in condstr[2:]:
                if bit not in ('0','1'):
                    print("ERROR: '%s' value isn't a bitvector" % condstr)
                    sys.exit(1)
                condbits = condbits << 1 | int(bit)
        return condmask, condop, condbits << condshift


    def check(self):
        # sanity check for matches between different entries
        write = sys.stderr.write
        for item in self.items:
            for test in self.items:
                if item == test:
                    continue
                # no duplicate info?
                if test['info'] == item['info'] and test['bmask'] == item['bmask'] and test['op'] == item['op'] and test['bvalue'] == item['bvalue']:
                    write("ERROR: '%s' duplicates info for '%s'\n" % (item['name'], test['name']))
                # mask & bits is unique?
                if test['bits'] & item['mask'] == item['bits']:
                    oper = item['op']
                    if oper == '!' and test['bits'] & item['bmask'] == item['bvalue']:
                        continue
                    elif oper == '<' and test['bits'] & item['bmask'] >= item['bvalue']:
                        continue
                    elif oper == '>' and test['bits'] & item['bmask'] <= item['bvalue']:
                        continue
                    write("WARNING: '%s' bits & mask may match '%s'\n" % (item['name'], test['name']))
                    for it in item, test:
                        if it['bmask']:
                            write("%(name)13s: bits & %(mask)04x = %(bits)04x, bits & %(bmask)04x %(op)s %(bvalue)04x\n" % it)
                        else:
                            write("%(name)13s: bits & %(mask)04x = %(bits)04x\n" % it)


    def print_items(self):
        print("""/*
 * instruction bitmask lookup table
 * generated by instructions.py
 *
 * instruction mask, bits, info string, comparison mask, comparison operator, value to compare against, instruction name
 */
instruction_t instructions = {""")
        for item in self.items:
            if item['op']:
                print("\t{ 0x%(mask)04x, 0x%(bits)04x, \"%(info)s\", 0x%(bmask)04x, '%(op)s', 0x%(bvalue)04x, \"%(name)s\" }," % item)
            else:
                print("\t{ 0x%(mask)04x, 0x%(bits)04x, \"%(info)s\", 0x%(bmask)x, '', 0, \"%(name)s\" }," % item)
        print("};")

    def print_help(self):
        print("help_txt =")
        print("\"\tInstruction bit type identifiers:\\n\"")
        for item in self.bittypes.items():
            print("\"\t%s = %s\\n\"" % item)
        print(";")



# main loop and command line parsing with readline
class Main:
    prompt = "m68k-instructions: "
    historysize = 99

    def __init__(self):
        self.commandlist = {
            "code":  self.show_code,
            "check": self.show_checks,
            "help":  self.show_help,
            "exit":  sys.exit
        }
        self.instructions = Instructions()
        cmds = list(self.commandlist.keys()) + self.instructions.get_names()
        cmds.sort()
        self.init_readline(cmds)

    def loop(self):
        self.show_help()
        while 1:
            line = self.get_input().strip()
            if not line:
                continue
            if line in self.commandlist:
                self.commandlist[line]()
            else:
                self.instructions.show(line)

    def show_help(self):
        print("""
*************************************************************
* m68k instruction information, Hatari debugger breakpoints *
* for them and C-code table for matching them.              *
*                                                           *
* To see instructions, use the TAB key; to see instruction  *
* information, complete it and press Enter. "code" command  *
* shows the C-code, "check" shows potential instruction     *
* conflicts, "help" shows this help and "exit" exits.       *
*************************************************************
""")
    def show_code(self):
        self.instructions.print_items()
        print()
        self.instructions.print_help()

    def show_checks(self):
        self.instructions.check()

    def init_readline(self, commands):
        readline.set_history_length(self.historysize)
        readline.parse_and_bind("tab: complete")
        readline.set_completer_delims(" \t\r\n")
        readline.set_completer(self.complete)
        self.commands = commands

    def complete(self, text, state):
        idx = 0
        #print "text: '%s', state '%d'" % (text, state)
        for cmd in self.commands:
            if cmd.startswith(text):
                idx += 1
                if idx > state:
                    return cmd
    
    def get_input(self):
        try:
            rawline = input(self.prompt)
            return rawline
        except EOFError:
            return ""


if __name__ == "__main__":
    Main().loop()
