#!/usr/bin/env python
#
# Hatari profile data processor
#
# 2013 (C) Eero Tamminen, licensed under GPL v2+
"""
A tool for post-processing Hatari debugger profiling information
produced with the following debugger command:
	profile addresses 0 <file name>

It sums profiling information given for code addresses, to functions
where those addresses/instruction belong to.  All addresses between
two function names (in profile file) or symbol addresses (in symbols
file) are assumed to belong to the first function/symbol.

Supported (Hatari CPU and DSP) profile items are instruction counts,
used cycles and (CPU instruction) cache misses.


Usage: hatari-profile [options] <profile files>

Options:
	-a <symbols>	symbol address information file
        -c		output cycles usage information
        -i		output instruction count information
        -m		output cache miss information
        -f <count>	output only first <count> items
	-o <file name>	output file name (default is stdout)
        -s		output profile statistics
        -v		verbose parsing output

Long options for above are:
	--addresses
	--cycles
        --instr
        --misses
        --first
        --output
        --stats
        --verbose

For example:
	hatari-profile -a etos512k.sym -cims -f 10 prof1.txt prof2.txt

For each given profile file, output is a sorted list of functions, for
each of the requested profiling items (instructions, cycles, misses).

Symbol information should be in same format as for Hatari debugger
'symbols' command.  If addresses are within ROM area, they're
interpreted as absolute, otherwise, as relative to program TEXT (code)
section start address start given in the profile data file.

TODO: Output in Valgrind callgrind format:
       http://valgrind.org/docs/manual/cl-format.html
for KCachegrind:
       http://kcachegrind.sourceforge.net/
"""

import getopt, os, re, sys


class Output:
    "base class for screen and file outputs"

    def __init__(self):
        self.write = sys.stdout.write
        self.error_write = sys.stderr.write

    def set_output(self, out):
        "set normal output data file"
        self.write = out.write

    def set_error_output(self, out):
        "set error output data file"
        self.error_write = out.write

    # rest are independent of output file
    def message(self, msg):
        "show message to user"
        self.error_write("%s\n" % msg)

    def warning(self, msg):
        "show warning to user"
        self.error_write("WARNING: %s\n" % msg)

    def error_exit(self, msg):
        "show error to user + exit"
        self.error_write("ERROR: %s!\n" % msg)
        sys.exit(1)


class Instructions:
    "current function instructions state + info on all instructions"

    names = ("Executed instructions", "Used cycles", "Cache misses")

    def __init__(self, name, dsp):
        self.max_addr = [0, 0, 0]
        self.max_val = [0, 0, 0]
        self.totals = [0, 0, 0]
        self.areas = {}		# which memory area boundaries have been passed
        self.for_dsp = dsp
        self.zero(name)

    def zero(self, name):
        "start collecting new 'name' function instructions information"
        # stupid pylint, this method IS called from __init__,
        # so these are actually set "in" __init__...
        self.name = name
        self.addr = None	# just label, not real function yet
        self.data = [0, 0, 0]	# current function stats

    def add(self, addr, strvalues):
        "add strvalues string list of values for given address"
        # only first one is used
        if not self.addr:
            self.addr = addr
        for i in range(min(3, len(strvalues))):
            value = int(strvalues[i])
            self.data[i] += value
            if value > self.max_val[i]:
                self.max_val[i] = value
                self.max_addr[i] = addr

    def show(self):
        "show current function instruction state"
        print "%s @ 0x%x: %d, %d, %d" % (self.name, self.addr, self.data[0], self.data[1], self.data[2])

    def sum_values(self, values):
        "calculate totals for given instruction value sets"
        if values:
            sums = [0, 0, 0]
            items = min(len(sums), len(values[0]))
            for data in values:
                for i in range(items):
                    sums[i] += data[i]
            self.totals = sums


class Profile(Output):
    "Hatari profile parsing and information"

    # Hatari symbol and profile information processor
    def __init__(self):
        Output.__init__(self)
        self.instructions = None	# Instructions instance
        self.verbose = False
        self.address = None		# hash of (symbol:addr)
        self.symbols = None		# hash of (addr:symbol)
        self.profile = None		# hash of profile (symbol:data)
        self.callers = None		# hash of (callee:(caller:count))
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
        # caller info:
        # 0x<hex> = <count>
        self.r_caller = re.compile("^0x([0-9a-f]+) = ([0-9]+)$")
        # <symbol>:
        # _biostrap:
        self.r_function = re.compile("^([_a-zA-Z][_.a-zA-Z0-9]*):$")
        # Hatari symbol format:
        # [0x]<hex> [tTbBdD] <symbol name>
        self.r_symbol = re.compile("^(0x)?([a-fA-F0-9]+) ([bBdDtT]) ([_a-zA-Z][_.a-zA-Z0-9]*)$")
        # default emulation addresses / ranges
        self.addr_text = (0, 0)
        self.addr_ram = 0
        self.addr_tos = 0xe00000
        self.addr_cartridge = 0xfa0000

    def set_verbose(self, verbose):
        "set verbose on/off"
        self.verbose = verbose

    def parse_header(self, line):
        "parse profile header"
        # TODO: store also area ends?
        match = self.r_header.match(line)
        if not match:
            return False
        name, start, end = match.groups()
        end = int(end, 16)
        start = int(start, 16)
        name = name.split()[-1]
        if name == "TEXT":
            self.addr_text = (start, end)
        elif name == "TOS":
            self.addr_tos = start
        elif name == "RAM":
            self.addr_ram = start
        elif name == "ROM":
            self.addr_cartridge = start
        else:
            self.warning("unrecognized profile header line")
            return False
        if self.addr_text[1] < self.addr_text[0]:
            self.error_exit("invalid TEXT area range: 0x%x-0x%x" % self.addr_text)
        if self.addr_text[0] >= self.addr_tos or self.addr_ram >= self.addr_tos:
            self.error_exit("TOS address isn't higher than TEXT and RAM start addresses")
        return True

    def _get_profile_type(self, obj):
        "get profile processor type or exit if it's unknown"
        line = obj.readline()
        field = line.split()
        if len(field) != 3 or field[0] != "Hatari":
            self.error_exit("unrecognized file, line 1:\n\t%smisses Hatari profiler identification" % line)
        if field[1] == "CPU":
            return (self.r_cpuaddress, Instructions("HATARI_PROFILE_BEGIN", False))
        if field[1] == "DSP":
            return (self.r_dspaddress, Instructions("HATARI_PROFILE_BEGIN", True))
        self.error_exit("unrecognized profile processor type '%s' in line 1:\t\n%s" % (field[1], line))

    def parse_symbols(self, obj):
        "parse symbol file contents"
        # TODO: what if same symbol name is specified for multiple addresses?
        # - keep track of the names and add some post-fix to them so that
        #   they don't overwrite each others data in self.profile hash?
        if not self.symbols:
            self.symbols = {}
        unknown = lines = 0
        for line in obj.readlines():
            lines += 1
            line = line.strip()
            if line.startswith('#'):
                continue
            match = self.r_symbol.match(line)
            if match:
                dummy, addr, kind, name = match.groups()
                if kind in ('t', 'T'):
                    addr = int(addr, 16)
                    if self.verbose:
                        self.message("%d = 0x%x\n" % (addr, name))
                    if addr in self.symbols:
                        # prefer function names over object names
                        if name[-2:] == ".o":
                            continue
                        if self.symbols[addr] == name:
                            continue
                        self.warning("replacing '%s' at 0x%x with '%s'" % (self.symbols[addr], addr, name))
                    self.symbols[addr] = name
            else:
                self.warning("unrecognized symbol line %d:\n\t'%s'" % (lines, line))
                unknown += 1
        self.message("%d lines with %d code symbols/addresses parsed, %d unknown.\n" % (lines, len(self.symbols), unknown))

    def _parse_caller(self, line):
        "parse callee: caller call count information"
        #0x<hex>: 0x<hex> = <count>, N*[0x<hex> = <count>,][ (<symbol>)
        callers = line.split(',')
        if len(callers) < 2:
            self.warning("caller info missing")
            return False
        if ':' not in callers[0]:
            self.warning("callee/caller separator ':' missing")
            return False
        last = callers[-1]
        if len(last) and last[-1] != ')':
            self.warning("last item isn't empty or symbol name")
            return False

        addr, callers[0] = callers[0].split(':')
        addr = int(addr, 16)
        self.callers[addr] = {}
        for caller in callers[:-1]:
            caller = caller.strip()
            match = self.r_caller.match(caller)
            if match:
                caddr, count = match.groups()
                caddr = int(caddr, 16)
                count = int(count, 10)
                self.callers[addr][caddr] = count
            else:
                self.warning("unrecognized caller info '%s'" % caller)
                return False
        return True

    def _change_function(self, function, newname):
        "store current function data and then reset to new function"
        # contains instructions (= meaningful data)?
        if function.addr:
            oldname = function.name
            if oldname in self.profile:
                self.warning("when switching from '%s' to '%s' symbol, overriding data for former:\n\t%s -> %s" % (oldname, newname, self.profile[oldname], function.data))
            self.profile[oldname] = function.data
            if self.verbose:
                function.show()
        function.zero(newname)

    def _check_symbols(self, function, addr):
        "if address is in new symbol (=function), change function"
        if self.symbols:
            idx = addr
            if addr >= self.addr_text[0] and addr <= self.addr_text[1]:
                # within TEXT area -> relative to TEXT start
                idx -= self.addr_text[0]
            # overrides profile data function names for same address
            if idx in self.symbols:
                name = self.symbols[idx]
                self._change_function(function, name)
                # this function needs address info in output
                self.address[name] = addr
                return
        if function.for_dsp or addr in self.address:
            # not CPU code or has been already assigned
            return
        # as no better symbol, name it according to area where it moved to
        if addr < self.addr_tos:
            if addr < self.addr_text[0]:
                if "ram_start" not in function.areas:
                    function.areas["ram_start"] = True
                    name = "RAM_BEFORE_TEXT"
                else:
                    return
            elif addr > self.addr_text[1]:
                if "ram_end" not in function.areas:
                    function.areas["ram_end"] = True
                    name = "RAM_AFTER_TEXT"
                else:
                    return
            elif "text" not in function.areas:
                function.areas["text"] = True
                name = "PROGRAM_TEXT_SECTION"
            else:
                return
        elif addr < self.addr_cartridge:
            if "tos" not in function.areas:
                function.areas["tos"] = True
                name = "ROM_TOS_AREA"
            else:
                return
        elif "cart" not in function.areas:
            function.areas["cart"] = True
            name = "ROM_CARTRIDGE_AREA"
        else:
            return
        self._change_function(function, name)
        self.address[name] = addr

    def parse_profile(self, obj):
        "parse profile data"
        r_address, instructions = self._get_profile_type(obj)
        prev_addr = unknown = lines = 0
        self.address = {}
        self.profile = {}
        self.callers = {}
        for line in obj.readlines():
            lines += 1
            line = line.strip()
            # CPU or DSP address line?
            if line.startswith('$') or line.startswith('p:'):
                match = r_address.match(line)
                if match:
                    addr, counts = match.groups()
                    addr = int(addr, 16)
                    if prev_addr > addr:
                        self.error_exit("memory addresses are not in order on line %d" % lines)
                    prev_addr = addr
                    self._check_symbols(instructions, addr)
                    instructions.add(addr, counts.split(','))
                else:
                    self.error_exit("unrecognized address line %d:\n\t'%s'" % (lines, line))
                continue
            # symbol?
            if line.endswith(':'):
                match = self.r_function.match(line)
                if match:
                    self._change_function(instructions, match.group(1))
                else:
                    self.error_exit("unrecognized function line %d:\n\t'%s'" % (lines, line))
                continue
            # address discontinuation
            if line == "[...]":
                continue
            # caller information
            if line.startswith('0x'):
                if not self._parse_caller(line):
                    self.error_exit("unrecognized caller line %d:\n\t'%s'" % (lines, line))
                continue
            # header?
            if not self.parse_header(line):
                self.warning("unrecognized line %d:\n\t'%s'" % (lines, line))
                unknown += 1

        self._change_function(instructions, "HATARI_PROFILE_END")
        self.instructions = instructions
        self.message("%d lines processed with %d functions." % (lines, len(self.profile)))
        if 2*unknown > lines:
            self.error_exit("more than half of the lines were unrecognized!")
        if len(self.profile) < 1:
            self.error_exit("no functions found!")


class ProfileStats(Output):
    "profile information statistics output"

    # Hatari symbol and profile information statistics output
    def __init__(self):
        Output.__init__(self)
        self.profile = None
        self.address = None
        self.instr = None
        self.do_totals = False
        self.do_instr = False
        self.do_cycles = False
        self.do_misses = False
        self.count = 0			# all

    def set_count(self, count):
        "set how many items to show in lists (0=all)"
        self.count = count

    def show_totals(self, show):
        "dis/enable totals list"
        self.do_totals = show
    def show_instructions(self, show):
        "dis/enable instructions list"
        self.do_instr = show
    def show_cycles(self, show):
        "dis/enable cycles list"
        self.do_cycles = show
    def show_misses(self, show):
        "dis/enable cache misses list"
        self.do_misses = show

    def do_output(self):
        "output enabled lists"
        if self.do_totals:
            self.output_totals()
        if self.do_instr:
            self.output_instructions()
        if self.do_cycles:
            self.output_cycles()
        if self.do_misses:
            self.output_misses()

    def set_profile(self, prof):
        "set profiling info to use for output"
        self.profile = prof.profile
        self.address = prof.address
        self.instr = prof.instructions
        self.instr.sum_values(self.profile.values())

    def output_totals(self):
        "output profile statistics"
        self.write("\n")
        instr = self.instr
        items = len(instr.totals)
        for i in range(items):
            if not instr.totals[i]:
                continue
            self.write("%s:\n" % instr.names[i])
            self.write("- max = %d, at 0x%x\n" % (instr.max_val[i], instr.max_addr[i]))
            self.write("- %d in total\n" % instr.totals[i])

    def _output_list(self, keys, field):
        "list output functionality"
        self.write("\n%s:\n" % self.instr.names[field])
        totals = self.instr.totals
        total = totals[field]
        if total == 0:
            self.write("- information missing\n")
            return
        total = float(total)
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
                addr = "(0x%04x)" % self.address[key]
            else:
                addr = ""
            self.write("%6.2f%% %9s %-28s%s\n" % (value*100.0/total, value, key, addr))
            idx += 1

    def _cmp_instructions(self, i, j):
        "compare instruction counts"
        return cmp(self.profile[i][0], self.profile[j][0])

    def _cmp_cycles(self, i, j):
        "compare cycle counts"
        return cmp(self.profile[i][1], self.profile[j][1])

    def _cmp_misses(self, i, j):
        "compare cache miss counts"
        return cmp(self.profile[i][2], self.profile[j][2])

    def output_instructions(self):
        "output instructions usage list"
        keys = self.profile.keys()
        keys.sort(self._cmp_instructions, None, True)
        self._output_list(keys, 0)

    def output_cycles(self):
        "output cycles usage list"
        keys = self.profile.keys()
        keys.sort(self._cmp_cycles, None, True)
        self._output_list(keys, 1)

    def output_misses(self):
        "output cache misses list"
        keys = self.profile.keys()
        keys.sort(self._cmp_misses, None, True)
        self._output_list(keys, 2)


class Main(Output):
    "program main loop & args parsing"

    def __init__(self, argv):
        Output.__init__(self)
        self.name = os.path.basename(argv[0])
        self.message("Hatari profile data processor")
        if len(argv) < 2:
            self.usage("argument(s) missing")
        self.args = argv[1:]

    def parse_args(self):
        "parse & handle program arguments"
        try:
            longopts = ["addresses=", "cycles", "first", "instr", "misses", "output=", "stats", "verbose"]
            opts, rest = getopt.getopt(self.args, "a:cf:imo:sv", longopts)
            del longopts
        except getopt.GetoptError as err:
            self.usage(err)

        prof = Profile()
        stats = ProfileStats()
        for opt, arg in opts:
            #self.message("%s: %s" % (opt, arg))
            if opt in ("-a", "--addresses"):
                self.message("\nParsing symbol address information from %s..." % arg)
                prof.parse_symbols(self.open_file(arg))
            elif opt in ("-c", "--cycles"):
                stats.show_cycles(True)
            elif opt in ("-f", "--first"):
                try:
                    stats.set_count(int(arg))
                except ValueError:
                    self.usage("invalid '%s' value" % opt)                    
            elif opt in ("-i", "--instr"):
                stats.show_instructions(True)
            elif opt in ("-m", "--misses"):
                stats.show_misses(True)
            elif opt in ("-o", "--output"):
                out = self.open_file(arg)
                prof.set_output(out)
                stats.set_output(out)
            elif opt in ("-s", "--stats"):
                stats.show_totals(True)
            elif opt in ("-v", "--verbose"):
                prof.set_verbose(True)
            else:
                self.usage("unknown option '%s'" % opt)
        for arg in rest:
            self.message("\nParsing profile information from %s..." % arg)
            prof.parse_profile(self.open_file(arg))
            stats.set_profile(prof)
            stats.do_output()

    def open_file(self, path):
        try:
            return open(path)
        except IOError, err:
            self.usage("opening given '%s' file failed:\n\t%s" % (path, err))

    def usage(self, msg):
        "show program usage + error message"
        self.message(__doc__)
        self.message("ERROR: %s!" % msg)
        sys.exit(1)


if __name__ == "__main__":
    Main(sys.argv).parse_args()
