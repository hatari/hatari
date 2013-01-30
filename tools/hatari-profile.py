#!/usr/bin/env python
#
# Hatari profile data processor
#
# 2013 (C) Eero Tamminen, licensed under GPL v2+

import getopt, os, re, sys

class Instructions:
    "current function instructions state and some state of all instructions"
    def __init__(self, name):
        self.max_addr = [0,0,0]
        self.max_val = [0,0,0]
        self.zero(name)

    def zero(self, name):
        self.name = name
        self.addr = None	# just label, not real function yet
        self.data = [0,0,0]	# current function stats

    def add(self, addr, strvalues):
        # only first one is used
        if not self.addr:
            self.addr = addr
        for i in range(len(strvalues)):
            value = int(strvalues[i])
            self.data[i] += value
            if value > self.max_val[i]:
                self.max_val[i] = value
                self.max_addr[i] = addr

    def show(self):
        print "%s @ 0x%x: %d, %d, %d" % (self.name, self.addr, self.data[0], self.data[1], self.data[2])


class Profile:
    # Hatari symbol and profile information processor
    def __init__(self):
        self.error = sys.stderr.write
        self.write = sys.stdout.write
        self.count = 0			# all
        self.instructions = None	# Instructions instance
        self.verbose = False
        self.address = None		# hash
        self.symbols = None		# hash
        self.profile = None		# hash
        self.sums = None		# list
        # Hatari profile format:
        # <name>: <hex>-<hex>
        # ROM TOS:	0xe00000-0xe80000
        self.r_header = re.compile("^([^:]+):[^0]*0x([0-9a-f]+)-0x([0-9a-f]+)$")
        # $<hex>  :  <ASM>  <percentage>% (<count>, <cycles>, <misses>)
        # $e5af38 :   rts           0.00% (12, 0, 12)
        self.r_cpuaddress = re.compile("^\$([0-9a-f]+) :.*% \((.*)\)$")
        # <space>:<address> <opcodes> (<instr cycles>) <instr> <count>% (<count>, <cycles>)
        # p:0202  0aa980 000200  (07 cyc)  jclr #0,x:$ffe9,p:$0200  0.00% (6, 42)
        self.r_dspaddress = re.compile("^p:([0-9a-f]+) .*% \((.*)\)$")
        # <symbol>:
        # _biostrap:
        self.r_function = re.compile("^([_a-zA-Z][_.a-zA-Z0-9]*):$")
        # Hatari symbol format:
        # [0x]<hex> [tTbBdD] <symbol name>
        self.r_symbol = re.compile("^(0x)?([a-fA-F0-9]+) ([bBdDtT]) ([_a-zA-Z][_.a-zA-Z0-9]*)$")
        # default emulation addresses
        self.addr_text = 0
        self.addr_ram = 0
        self.addr_tos = 0xe00000
        self.addr_cartridge = 0xfa0000

    def set_output(self, f):
        "set output file"
        self.write = f.write

    def set_count(self, count):
        self.count = count

    def set_verbose(self, verbose):
        "set verbose on/off"
        self.verbose = verbose

    def parse_header(self, line):
        "parse profile header"
        match = self.r_header.match(line)
        if not match:
            return False
        name,start,end = match.groups()
        start = int(start, 16)
        name = name.split()[-1]
        if name == "TEXT":
            self.addr_text = start
        elif name == "TOS":
            self.addr_tos = start
        elif name == "RAM":
            self.addr_ram = start
        elif name == "ROM":
            self.addr_cartridge = start
        else:
            self.error("ERROR: unrecognized profile header line\n")
            return False
        if self.addr_text >= self.addr_tos or self.addr_ram >= self.addr_tos:
            self.error("ERROR: TOS address isn't higher than TEXT and RAM start addresses\n")
        return True

    def parse_symbols(self, f):
        "parse symbol file contents"
        if not self.symbols:
            self.symbols = {}
        unknown = lines = 0
        for line in f.readlines():
            lines += 1
            line = line.strip()
            if line[0] == '#':
                continue
            match = self.r_symbol.match(line)
            if match:
                dummy,addr,kind,name = match.groups()
                if kind in ('t', 'T'):
                    addr = int(addr,16)
                    if self.verbose:
                        self.error("%d = 0x%x\n" % (addr, name))
                    if addr in self.symbols:
                        # prefer function names over object names
                        if name[-2:] == ".o":
                            continue
                        self.error("WARNING: replacing '%s' at 0x%x with '%s'\n" % (self.symbols[addr], addr, name))
                    self.symbols[addr] = name
            else:
                self.error("ERROR: unrecognized symbol line %d:\n\t'%s'\n" % (lines, line))
                unknown += 1
        self.error("%d lines with %d code symbols/addresses parsed, %d unknown.\n" % (lines, len(self.symbols), unknown))

    def _sum_values(self):
        "calculate totals"
        values = [0,0,0]
        for data in self.profile.values():
            for i in range(len(data)):
                values[i] += data[i]
        self.sums = values

    def _change_function(self, function, name):
        "store current function data and then reset it"
        if function.addr:
            self.profile[function.name] = function.data
            if self.verbose:
                function.show()
        function.zero(name)

    def _check_symbols(self, function, addr):
        "if address is in new symbol (=function), change function"
        if self.symbols:
            if addr >= self.addr_text and addr < self.addr_tos:
                # non-TOS address are relative to TEXT start
                idx = addr - self.addr_text
            else:
                idx = addr
            # overrides profile data function names for same address
            if idx in self.symbols:
                name = self.symbols[idx]
                self._change_function(function, name)
                # this function needs address info in output
                self.address[name] = addr

    def _parse_profile(self, f, r_address):
        "parse profile data"
        unknown = lines = 0
        instructions = Instructions("HATARI_PROFILE_BEGIN")
        self.address = {}
        self.profile = {}
        for line in f.readlines():
            lines += 1
            line = line.strip()
            if line == "[...]":
                continue
            # symbol?
            if line[-1:] == ':':
                match = self.r_function.match(line)
                if match:
                    self._change_function(instructions, match.group(1))
                else:
                    self.error("ERROR: unrecognized function line %d:\n\t'%s'\n" % (lines, line))
                    unknown += 1
                continue
            # CPU or DSP address line?
            if line[0] in ('$', 'p'):
                match = r_address.match(line)
                if match:
                    addr, counts = match.groups()
                    addr = int(addr, 16)
                    self._check_symbols(instructions, addr)
                    instructions.add(addr, counts.split(','))
                else:
                    self.error("ERROR: unrecognized address line %d:\n\t'%s'\n" % (lines, line))
                    unknown += 1
                continue
            # header?
            if not self.parse_header(line):
                self.error("WARNING: unrecognized line %d:\n\t'%s'\n" % (lines, line))
                unknown += 1
        self._change_function(instructions, "HATARI_PROFILE_END")
        self.instructions = instructions
        self.error("%d lines processed with %d functions.\n" % (lines, len(self.profile)))
        if 2*unknown > lines:
            self.error("ERROR: more than half of the lines were unrecognized!\n")
        if len(self.profile) < 1:
            self.error("ERROR: no functions found!\n")
            return False
        self._sum_values()
        return True

    def parse_profile_cpu(self, f):
        return self._parse_profile(f, self.r_cpuaddress)

    def parse_profile_dsp(self, f):
        return self._parse_profile(f, self.r_dspaddress)

    def output_stats(self):
        "output profile statistics"
        self.write("\n")
        instr = self.instructions
        names = ("Instructions", "Cycles", "Cache misses")
        items = len(self.profile.values()[0])
        for i in range(items):
            self.write("%s:\n" % names[i])
            self.write("- max = %d, at 0x%x\n" % (instr.max_val[i], instr.max_addr[i]))
            self.write("- %d in total\n" % self.sums[i])

    def _output_list(self, keys, field, heading):
        self.write("\n%s:\n" % heading)
        sum = self.sums[field]
        if sum == 0:
            self.write("- information missing\n")
            return
        sum = float(sum)
        idx = 0
        if self.count:
            count = self.count
        else:
            count = len(keys)
        for key in keys:
            if idx >= count:
                break
            value = self.profile[key][field]
            if key in self.address:
                addr = "\t(at 0x%x)" % self.address[key]
            else:
                addr = ""
            self.write("%6.2f%% %9s  %s%s\n" % (value*100.0/sum, value, key, addr))
            idx += 1

    def cmp_instructions(self, a, b):
        return cmp(self.profile[a][0], self.profile[b][0])

    def cmp_cycles(self, a, b):
        return cmp(self.profile[a][1], self.profile[b][1])

    def cmp_misses(self, a, b):
        return cmp(self.profile[a][2], self.profile[b][2])

    def output_instructions(self):
        keys = self.profile.keys()
        keys.sort(self.cmp_instructions, None, True)
        self._output_list(keys, 0, "Executed instructions")

    def output_cycles(self):
        keys = self.profile.keys()
        keys.sort(self.cmp_cycles, None, True)
        self._output_list(keys, 1, "Used cycles")

    def output_misses(self):
        keys = self.profile.keys()
        keys.sort(self.cmp_misses, None, True)
        self._output_list(keys, 2, "Cache misses")


class Main:
    def __init__(self, argv):
        self.name = os.path.basename(argv[0])
        self.write = sys.stderr.write
        self.write("Hatari profile data processor\n")
        if len(argv) < 2:
            self.usage("argument(s) missing")
        self.args = argv[1:]

    def parse_args(self):
        try:
            longopts = ["addresses=", "dsp", "cycles", "first", "instr", "misses", "output=", "stats", "verbose"]
            opts, rest = getopt.getopt(self.args, "a:cdf:imo:sv", longopts)
            del longopts
        except getopt.GetoptError as err:
            self.usage(err)

        prof = Profile()
        dsp = stats = instr = cycles = misses = False
        for opt, arg in opts:
            #self.write("%s: %s\n" % (opt, arg))
            if opt in ("-a", "--addresses"):
                self.write("\nParsing symbol address information from %s...\n" % arg)
                prof.parse_symbols(self.open_file(arg))
            elif opt in ("-c", "--cycles"):
                cycles = True
            elif opt in ("-d", "--dsp"):
                dsp = True
            elif opt in ("-f", "--first"):
                try:
                    prof.set_count(int(arg))
                except ValueError:
                    self.usage("invalid '%s' value" % opt)                    
            elif opt in ("-i", "--instr"):
                instr = True
            elif opt in ("-m", "--misses"):
                misses = True
            elif opt in ("-o", "--output"):
                prof.set_output(self.open_file(arg))
            elif opt in ("-s", "--stats"):
                stats = True
            elif opt in ("-v", "--verbose"):
                prof.set_verbose(True)
            else:
                self.usage("unknown option '%s'" % opt)
        for arg in rest:
            self.write("\nParsing profile information from %s...\n" % arg)
            if dsp:
                fun = prof.parse_profile_dsp
            else:
                fun = prof.parse_profile_cpu
            if not fun(self.open_file(arg)):
                return
            if stats:
                prof.output_stats()
            if instr:
                prof.output_instructions()
            if cycles:
                prof.output_cycles()
            if misses:
                prof.output_misses()

    def open_file(self, path):
        try:
            return open(path)
        except IOError, err:
            self.usage("opening given '%s' file failed:\n\t%s" % (path, err))

    def usage(self, msg):
        self.write("""
Script for processing Hatari debugger profiling information produced
with the following debugger command:
	profile addresses 0 <file name>

It sums profiling information given for code addresses, to functions
where those addresses/instruction belong to.  All addresses between
two function names (in profile file) or symbol addresses (in symbols
file) are assumed to belong to the first function/symbol.

Supported (Hatari CPU and DSP) profile items are instruction counts,
used cycles and (CPU instruction) cache misses.


Usage: %s [options] <profile files>

Options:
        -d		profile data is for DSP, not CPU
	-a <symbols>	symbol address information file
        -c		output cycles usage information
        -i		output intruction count information
        -m		output cache miss information
        -f <count>	output only first <count> items
	-o <file name>	output file name (default is stdout)
        -s		output profile statistics
        -v		verbose parsing output

Long options for above are:
        --dsp
	--addresses
	--cycles
        --instr
        --misses
        --output
        --stats
        --verbose

For example:
	%s -a etos512k.sym -cims -f 10 profile1.txt profile2.txt

For each given profile file, output is a sorted list of functions, for
each of the requested profiling items (instructions, cycles, misses).

Symbol information should be in same format as for Hatari debugger
'symbols' command.  If addresses are within ROM area, they're
interpreted as absolute, otherwise, as relative to program TEXT (code)
section start address start given in the profile data file.

TODO:
- Output in Valgrind callgrind format:
        http://valgrind.org/docs/manual/cl-format.html
  for KCachegrind:
        http://kcachegrind.sourceforge.net/

ERROR: %s!
""" % (self.name, self.name, msg))
        sys.exit(1)


if __name__ == "__main__":
    Main(sys.argv).parse_args()
