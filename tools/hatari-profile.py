#!/usr/bin/env python
#
# Hatari profile data processor
#
# 2013 (C) Eero Tamminen, licensed under GPL v2+

import getopt, os, re, sys

class Function:
    def __init__(self, name):
        self.zero(name)

    def zero(self, name):
        self.name = name
        self.addr = None
        self.data = [0,0,0]
        self.items = False

    def set_addr(self, addr):
        # only first one is used
        if not self.addr:
            self.addr = addr

    def add(self, strvalues):
        self.items = True
        for i in range(len(strvalues)):
            self.data[i] += int(strvalues[i])

    def show(self):
        print "%s @ 0x%x: %d, %d, %d" % (self.name, self.addr, self.data[0], self.data[1], self.data[2])


class Profile:
    # Hatari symbol and profile information processor
    def __init__(self):
        self.error = sys.stderr.write
        self.write = sys.stdout.write
        self.count = 0		# all
        self.verbose = False
        self.symbols = None	# hash
        self.profile = None	# hash
        self.sums = None	# list
        # Hatari profile format:
        # <name>: <hex>-<hex>
        # ROM TOS:	0xe00000-0xe80000
        self.r_header = re.compile("^([^:]+):[^0]*0x([0-9a-f]+)-0x([0-9a-f]+)$")
        # $<hex>  :  <ASM>  <percentage>% (<count>,<cycles>[,<misses>])
        # $e5af38 :   rts           0.00% (12, 0, 12)
        self.r_address = re.compile("^\$([0-9a-f]+) :.*% \((.*)\)$")
        # <symbol>:
        # _biostrap:
        self.r_function = re.compile("^([_a-zA-Z][_a-zA-Z0-9]+):$")
        # Hatari symbol format:
        # <address> [tTbBdD] <symbol name>
        self.r_symbol = re.compile("^([a-fA-F0-9]+) ([bBdDtT]) ([_a-zA-Z0-9]+)$")
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
                addr,kind,name = match.groups()
                if kind in ('t', 'T'):
                    addr = int(addr,16)
                    if self.verbose:
                        self.error("%d = 0x%x\n" % (addr, name))
                    if addr in self.symbols:
                        self.error("WARNING: replacing '%s' at 0x%x with '%s'\n" % (self.symbols[addr], addr, name))
                    self.symbols[addr] = name
            else:
                self.error("ERROR: unrecognized symbol line %d:\n\t'%s'\n" % (lines, line))
                unknown += 1
        self.error("%d lines with %d code symbols/addresses parsed, %d unknown.\n" % (lines, len(self.symbols), unknown))

    def _sum_values(self):
        values = [0,0,0]
        for data in self.profile.values():
            for i in range(len(data)):
                values[i] += data[i]
        self.error("Totals for instructions, cycles and misses:\n\t%s\n" % values)
        self.sums = values

    def _change_function(self, function, name):
        "store current function data and then reset it"
        if function.items:
            self.profile[function.name] = function.data
            if self.verbose:
                function.show()
        function.zero(name)

    def _check_symbols(self, function, addr):
        "if address is in new symbol (=function), change function"
        if self.symbols:
            if addr >= self.addr_text and addr < self.addr_tos:
                # non-TOS address are relative to TEXT start
                addr -= self.addr_text
            if addr in self.symbols:
                self._change_function(function, self.symbols[addr])

    def parse_profile(self, f):
        "parse profile data"
        unknown = lines = 0
        function = Function("HATARI_PROFILE_BEGIN")
        self.profile = {}
        for line in f.readlines():
            lines += 1
            line = line.strip()
            if line == "[...]":
                continue
            if line[0] == '$':
                match = self.r_address.match(line)
                if match:
                    addr, counts = match.groups()
                    addr = int(addr, 16)
                    self._check_symbols(function, addr)
                    function.set_addr(addr)
                    function.add(counts.split(','))
                else:
                    self.error("ERROR: unrecognized address line %d:\n\t'%s'\n" % (lines, line))
                    unknown += 1
                continue
            if line[-1:] == ':':
                match = self.r_function.match(line)
                if match:
                    self._change_function(function, match.group(1))
                else:
                    self.error("ERROR: unrecognized function line %d:\n\t'%s'\n" % (lines, line))
                    unknown += 1
                continue
            if not self.parse_header(line):
                self.error("WARNING: unrecognized line %d:\n\t'%s'\n" % (lines, line))
                unknown += 1
        self._change_function(function, "HATARI_PROFILE_END")
        self.error("%d lines processed with %d functions.\n" % (lines, len(self.profile)))
        if 2*unknown > lines:
            self.error("ERROR: more than half of the lines were unrecognized!\n")
        if len(self.profile) < 2:
            self.error("ERROR: less than 2 functions found!\n")
        self._sum_values()

    def _output(self, keys, field, heading):
        self.write("\n%s:\n" % heading)
        sum = self.sums[field]
        if sum == 0:
            self.write("- information missing\n")
            return
        sum = float(sum)

        idx = 0
        if not self.count:
            count = len(keys)
        for key in keys:
            if idx >= count:
                break
            value = self.profile[key][field]
            self.write("%6.2f%% %8s  %s\n" % (value*100.0/sum, value, key))
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
        self._output(keys, 0, "Executed instructions")

    def output_cycles(self):
        keys = self.profile.keys()
        keys.sort(self.cmp_cycles, None, True)
        self._output(keys, 1, "Used cycles")

    def output_misses(self):
        keys = self.profile.keys()
        keys.sort(self.cmp_misses, None, True)
        self._output(keys, 2, "Cache misses")


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
            longopts = ["cycles", "first", "instr", "misses", "output=", "symbols=", "verbose"]
            opts, rest = getopt.getopt(self.args, "cf:imo:s:v", longopts)
            del longopts
        except getopt.GetoptError as err:
            self.usage(err)

        prof = Profile()
        instr = cycles = misses = False
        for opt, arg in opts:
            self.write("%s: %s\n" % (opt, arg))
            if opt in ("-c", "--cycles"):
                cycles = True
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
            elif opt in ("-s", "--symbols"):
                self.write("\nParsing symbol information from %s...\n" % arg)
                prof.parse_symbols(self.open_file(arg))
            elif opt in ("-v", "--verbose"):
                prof.set_verbose(True)
            else:
                self.usage("unknown option '%s'" % opt)
        for arg in rest:
            self.write("\nParsing profile information from %s...\n" % arg)
            prof.parse_profile(self.open_file(arg))
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
            usage("opening given '%s' file failed:\n\t%s" % (path, err))

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
        -c		output cycles usage information
        -i		output intruction count information
        -m		output cache miss information
        -f <count>	output only first <count> items
	-o <file name>	output file name (default is stdout)
	-s <symbols>	symbol address information file
        -v		verbose parsing output

Long options for above are:
	--cycles
        --instr
        --misses
        --output
        --symbols
        --verbose

For example:
	%s -s etos512k.sym -cim -f 10 profile1.txt profile2.txt

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
