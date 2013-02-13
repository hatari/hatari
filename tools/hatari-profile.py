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

Provided symbol information should be in same format as for Hatari
debugger 'symbols' command.  If addresses are within ROM area, they're
interpreted as absolute, otherwise, as relative to program TEXT (code)
section start address start given in the profile data file.


Usage: hatari-profile [options] <profile files>

Options:
	-a <symbols>	symbol address information file
        -c		list top cycles usage
        -e		list most called symbols
        -i		list top instructions usage
        -m		list top cache misses
        -s		list profile statistics
        -f <count>	list only first <count> items
        -l <limit>      list only items which percentage >= limit
	-o <file name>	statistics output file name (default is stdout)
        -g              write <profile>.dot callgraph files
        -v		verbose parsing output

Long options for above are:
	--addresses
	--cycles
        --called
        --instr
        --misses
        --stats
        --first
        --limit
        --output
        --graph
        --verbose


For example:
	hatari-profile -a etos512k.sym -cimes -g -f 10 prof1.txt prof2.txt

For each given profile file, output is:
- a sorted list of functions, for each of the requested profiling items
  (instructions, cycles, misses).
- callgraph information in "dot" format, saved to <name>.dot file
  (prof1.dot and prof2.dot in the example)


Callgraph filtering options to remove nodes and edges from the graph:
	--ignore <list>         no nodes for these symbols
	--ignore-to <list>	no arrows to these symbols
	--ignore-from <list>	no arrows from these symbols
        --only <list>           only these symbols and their callers

<list> is a comma separate list of symbol names, like this:
	--ignore-to _int_timerc,_int_vbl

Typically interrupt handler symbols are good to give for '--ignore-to'
option, as they can get called at any point.  Leaf or intermediate
functions which are called from everywhere (like malloc) can be good
candinates to give for '--ignore' option.

In callgraph --limit option affects only which items will be highlighted.


To convert dot files e.g. to SVG, use:
	dot -Tsvg graph.dot > graph.svg

('dot' tool is in Graphviz package.)


TODO: Output in Valgrind callgrind format:
       http://valgrind.org/docs/manual/cl-format.html
for KCachegrind:
       http://kcachegrind.sourceforge.net/
"""
from bisect import bisect_left, bisect_right
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


class InstructionStats:
    "current function instructions state + info on all instructions"

    names = ("Executed instructions", "Used cycles", "Cache misses")

    def __init__(self, name, dsp):
        self.max_addr = [0, 0, 0]
        self.max_val = [0, 0, 0]
        self.totals = [0, 0, 0]
        self.areas = {}		# which memory area boundaries have been passed
        self.for_dsp = dsp
        self.zero(name, None)

    def has_data(self):
        "return whether function has valid data yet"
        # other values are valid only if there are instructions
        return (self.data[0] != 0)

    def zero(self, name, addr):
        "start collecting new 'name' function instructions information"
        # stupid pylint, this method IS called from __init__,
        # so these are actually set "in" __init__...
        self.name = name
        self.addr = addr	# 0 if address isn't know at this stage
        self.data = [0, 0, 0]	# current function stats

    def add(self, addr, strvalues):
        "add strvalues string list of values to current state"
        for i in range(min(3, len(strvalues))):
            value = int(strvalues[i])
            self.data[i] += value
            if value > self.max_val[i]:
                self.max_val[i] = value
                self.max_addr[i] = addr

    def show(self):
        "show current function instruction state"
        print "0x%x = %s: %s" % (self.addr, self.name, repr(self.data))

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
        self.verbose = False
        self.stats = None		# InstructionStats instance
        self.address = None		# hash of (symbol:addr)
        self.symbols = {}		# hash of (addr:symbol)
        self.symbols_need_sort = False
        self.symbols_sorted = None	# sorted list of symbol addresses
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
        # <symbol/objectfile name>:
        # _biostrap:
        self.r_function = re.compile("^([-_.a-zA-Z0-9]+):$")
        # Hatari symbol file format:
        # [0x]<hex> [tTbBdD] <symbol/objectfile name>
        self.r_symbol = re.compile("^(0x)?([a-fA-F0-9]+) ([bBdDtT]) ([$]?[-_.a-zA-Z0-9]+)$")
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

    def _get_area(self, addr):
        "return area name + offset, used if no symbol matches"
        if addr < self.addr_tos:
            if addr < self.addr_text[0]:
                return ("RAM_BEFORE_TEXT", self.addr_ram - addr)
            elif addr > self.addr_text[1]:
                return ("RAM_AFTER_TEXT", self.addr_text[1] - addr)
            else:
                return ("PROGRAM_TEXT_SECTION", self.addr_text[0] - addr)
        elif addr < self.addr_cartridge:
            return ("ROM_TOS_AREA", self.addr_tos - addr)
        else:
            return ("ROM_CARTRIDGE_AREA", self.addr_cartridge - addr)
        return (None, 0)

    def _get_profile_type(self, obj):
        "get profile processor type or exit if it's unknown"
        line = obj.readline()
        field = line.split()
        if len(field) != 3 or field[0] != "Hatari":
            self.error_exit("unrecognized file, line 1:\n\t%smisses Hatari profiler identification" % line)
        if field[1] == "CPU":
            return (self.r_cpuaddress, InstructionStats("HATARI_PROFILE_BEGIN", False))
        if field[1] == "DSP":
            return (self.r_dspaddress, InstructionStats("HATARI_PROFILE_BEGIN", True))
        self.error_exit("unrecognized profile processor type '%s' in line 1:\t\n%s" % (field[1], line))

    def _add_symbol(self, addr, name):
        "assign given symbol name to given address"
        if addr in self.symbols:
            # prefer function names over object names
            if name.endswith('.o'):
                return
            oldname = self.symbols[addr]
            lendiff = abs(len(name) - len(oldname))
            # don't warn about object name replacements or adding/removing short prefix
            if not (oldname.endswith('.o') or (lendiff < 3 and (name.endswith(oldname) or oldname.endswith(name)))):
                self.warning("replacing '%s' at 0x%x with '%s'" % (oldname, addr, name))
        self.symbols[addr] = name
        self.symbols_need_sort = True

    def parse_symbols(self, obj):
        "parse symbol file contents"
        # TODO: what if same symbol name is specified for multiple addresses?
        # - keep track of the names and add some post-fix to them so that
        #   they don't overwrite each others data in self.profile hash?
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
                    self._add_symbol(addr, name)
                    if self.verbose:
                        self.message("0x%x = %s" % (addr, name))
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

    def _addr2relative(self, addr):
        "return absolute address converted to relative if it's within TEXT segment, for symbol lookup"
        idx = addr
        if not self.stats.for_dsp:
            if addr >= self.addr_text[0] and addr <= self.addr_text[1]:
                # within TEXT area -> relative to TEXT start
                idx -= self.addr_text[0]
        return idx

    def get_symbol(self, addr):
        "return symbol name for given address, or None"
        if self.symbols:
            idx = self._addr2relative(addr)
            # overrides profile data function names for same address
            if idx in self.symbols:
                return self.symbols[idx]
        return None

    def get_preceeding_symbol(self, addr):
        "resolve non-function addresses to preceeding function name+offset"
        if self.symbols:
            if self.symbols_need_sort:
                self.symbols_sorted = self.symbols.keys()
                self.symbols_sorted.sort()
                self.symbols_need_sort = False
            relative = self._addr2relative(addr)
            idx = bisect_left(self.symbols_sorted, relative) - 1
            if idx < 0:
                if self.symbols_sorted[0] == relative:
                    return (self.symbols[relative], 0)
            elif idx <= len(self.symbols_sorted):
                saddr = self.symbols_sorted[idx]
                return (self.symbols[saddr], relative - saddr)
        return self._get_area(addr)

    def _change_function(self, newname, addr):
        "store current function data and then reset to new function"
        function = self.stats
        if function.has_data():
            oldname = function.name
            if oldname in self.profile:
                self.warning("when switching from '%s' to '%s' symbol, overriding data for former:\n\t%s -> %s" % (oldname, newname, self.profile[oldname], function.data))
            self.profile[oldname] = function.data
            if self.verbose:
                function.show()
        function.zero(newname, addr)

    def _change_area(self, addr, area):
        "switch function to given area, if not already in it"
        if area not in self.stats.areas:
            self.stats.areas[area] = True
            self._change_function(area, addr)
            self.address[area] = addr

    def _check_symbols(self, addr):
        "if address is in new symbol (=function), change function"
        name = self.get_symbol(addr)
        if name:
            self._change_function(name, addr)
            # this function needs address info in output
            self.address[name] = addr
            return
        if self.stats.for_dsp:
            return
        # as no better symbol, name it according to area where it moved to
        area,offset = self._get_area(addr)
        if area:
            self._change_area(addr, area)

    def parse_profile(self, obj):
        "parse profile data"
        r_address, self.stats = self._get_profile_type(obj)
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
                    # TODO: refactor, don't poke stats innards
                    if not self.stats.addr:
                        # symbol matched from profile, got now address for it
                        self._add_symbol(addr, self.stats.name)
                        self.stats.addr = addr
                    self._check_symbols(addr)
                    self.stats.add(addr, counts.split(','))
                else:
                    self.error_exit("unrecognized address line %d:\n\t'%s'" % (lines, line))
                continue
            # symbol?
            if line.endswith(':'):
                match = self.r_function.match(line)
                if match:
                    self._change_function(match.group(1), None)
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

        self._change_function("HATARI_PROFILE_END", None)
        self.message("%d lines processed with %d functions." % (lines, len(self.profile)))
        if 2*unknown > lines:
            self.error_exit("more than half of the lines were unrecognized!")
        if len(self.profile) < 1:
            self.error_exit("no functions found!")
        self.stats.sum_values(self.profile.values())


class ProfileStats(Output):
    "profile information statistics output"

    # Hatari symbol and profile information statistics output
    def __init__(self):
        Output.__init__(self)
        self.profile = None
        self.address = None
        self.callers = None
        self.callcount = None
        self.totals = None
        self.do_totals = False
        self.do_called = False
        self.do_instr = False
        self.do_cycles = False
        self.do_misses = False
        self.limit = 1
        self.count = 0

    def set_count(self, count):
        "set how many items to show in lists (0=all)"
        self.count = count

    def set_limit(self, limit):
        "set smallest percentage to show in lists (0=all)"
        self.limit = limit

    def show_totals(self, show):
        "dis/enable totals list"
        self.do_totals = show
    def show_called(self, show):
        "dis/enable called list"
        self.do_called = show
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
        if self.do_called:
            self.output_called()
        if self.do_instr:
            self.output_instructions()
        if self.do_cycles:
            self.output_cycles()
        if self.do_misses:
            self.output_misses()

    def set_profile(self, prof):
        "set profiling info to use for output"
        self.profobj = prof
        self.callers = prof.callers
        self.profile = prof.profile
        self.address = prof.address
        self.totals = prof.stats

    def output_totals(self):
        "output profile statistics"
        self.write("\n")
        totals = self.totals
        items = len(totals.totals)
        for i in range(items):
            if not totals.totals[i]:
                continue
            addr = totals.max_addr[i]
            name, offset = self.profobj.get_preceeding_symbol(addr)
            if name:
                name = " in %s" % name
            self.write("%s:\n" % totals.names[i])
            self.write("- max = %d,%s at 0x%x\n" % (totals.max_val[i], name, addr))
            self.write("- %d in total\n" % totals.totals[i])

    def _output_keyval(self, key, value, total):
        "output list addr, value information"
        if not value:
            return
        percentage = 100.0 * value / total
        if percentage < self.limit:
            return
        if key in self.address:
            addr = "(0x%04x)" % self.address[key]
        else:
            addr = ""
        self.write("%6.2f%% %9s %-28s%s\n" % (percentage, value, key, addr))

    def _cmp_called(self, i, j):
        "compare calls"
        return cmp(self.callcount[i], self.callcount[j])

    def output_called(self):
        "output called list"
        keys = self.callers.keys()
        self.callcount = {}
        total = 0
        for addr in keys:
            calls = 0
            for count in self.callers[addr].values():
                calls += count
            self.callcount[addr] = calls
            total += calls
        self.write("\nCalls:\n")
        if total == 0:
            self.write("- information missing\n")
            return
        keys.sort(self._cmp_called, None, True)
        idx = 0
        total = float(total)
        if self.count:
            count = self.count
        else:
            count = len(keys)
        for key in keys:
            if idx >= count:
                break
            value = self.callcount[key]
            name = self.profobj.get_symbol(key)
            if not name:
                name = "0x%x" % key
            self._output_keyval(name, value, total)
            idx += 1

    def _output_list(self, keys, field):
        "list output functionality"
        self.write("\n%s:\n" % self.totals.names[field])
        totals = self.totals.totals
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
            self._output_keyval(key, value, total)
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


class ProfileGraph(Output):
    "profile callgraph output"

    header = """
# Convert this to SVG with:
#   dot -Tsvg -o profile.svg <this file>

digraph profile {
center=1;
ratio=compress;

# page size A4
page="11.69,8.27";
size="9.69,6.27";
margin="1.0";

# set style options
color="black";
bgcolor="white";
node [shape="ellipse"];
edge [dir="forward" arrowsize="2"];

labelloc="t";
label="%s";
"""
    footer = "}\n"

    def __init__(self):
        Output.__init__(self)
        self.profile = None
        self.write = None
        self.name = "<no name>"
        self.limit = 10.0
        self.only = []
        self.ignore = []
        self.ignore_from = []
        self.ignore_to = []

    def set_output(self, fobj, name):
        "set output file object and its name"
        self.write = fobj.write
        self.name = name

    def set_limit(self, limit):
        "set graph node emphatizing limit, as instructions percentage"
        self.limit = limit

    def set_only(self, lst):
        "set list of only symbols to include"
        self.only = lst

    def set_ignore(self, lst):
        "set list of symbols to ignore"
        self.ignore = lst

    def set_ignore_from(self, lst):
        "set list of symbols to ignore calls from"
        self.ignore_from = lst

    def set_ignore_to(self, lst):
        "set list of symbols to ignore calls to"
        self.ignore_to = lst

    def set_profile(self, profile):
        "process profile data for the callgraph"
        self.profile = profile

    def do_output(self):
        "output graph of previusly set profile data to previously set file"
        profile = self.profile
        callers = profile.callers
        if not callers:
            self.warning("callee/caller information missing")
            return False
        title = "Function call counts and used instruction percentages, %s" % self.name
        self.write(self.header % title)

        nodes = {}
        callees = {}
        # output edges
        ignore_to = self.ignore_to + self.ignore
        ignore_from = self.ignore_from + self.ignore
        for addr in callers.keys():
            total = 0
            for count in callers[addr].values():
                total += count
            name = profile.get_symbol(addr)
            if not name:
                name = "$%x" % addr
            callees[name] = total
            if name in self.ignore_to:
                continue
            for caddr, count in callers[addr].items():
                cname, offset = profile.get_preceeding_symbol(caddr)
                if cname:
                    # no recursion
                    #if cname == name:
                    #    continue
                    if offset:
                        label = "%s+%d\\n($%x)" % (cname, offset, caddr)
                    else:
                        label = cname
                else:
                    cname = "$%x" % caddr
                    label = cname
                if self.only and name not in self.only and cname not in self.only:
                    continue
                nodes[name] = True
                if cname in self.ignore_from:
                    continue
                nodes[cname] = True
                if count != total:
                    percentage = 100.0 * count / total
                    label = "%s\\n%d calls\\n=%.2f%%" % (label, count, percentage)
                self.write("N%s -> N%s [label=\"%s\"];\n" % (cname, name, label))

        values = profile.profile
        total = profile.stats.totals[0]
        # output nodes
        for name in nodes.keys():
            count = values[name][0]
            percentage = 100.0 * count / total
            if percentage >= self.limit:
                style = " color=red style=filled fillcolor=lightgray" # shape=diamond
            else:
                style = ""
            if name in callees:
                calls = callees[name]
                self.write("N%s [label=\"%.2f%%\\n%s\\n(%d calls)\"%s];\n" % (name, percentage, name, calls, style))
            else:
                self.write("N%s [label=\"%.2f%%\\n%s\"%s];\n" % (name, percentage, name, style))

        self.write(self.footer)
        return True


class Main(Output):
    "program main loop & args parsing"
    longopts = [
        "addresses=",
        "cycles",
        "called",
        "first",
        "graph",
        "ignore=",
        "ignore-to=",
        "ignore-from=",
        "instr",
        "limit=",
        "misses",
        "only=",
        "output=",
        "stats",
        "verbose"
    ]

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
            opts, rest = getopt.getopt(self.args, "a:cef:gil:mo:sv", self.longopts)
        except getopt.GetoptError as err:
            self.usage(err)

        prof = Profile()
        do_graphs = False
        graph = ProfileGraph()
        stats = ProfileStats()
        for opt, arg in opts:
            #self.message("%s: %s" % (opt, arg))
            if opt in ("-a", "--addresses"):
                self.message("\nParsing symbol address information from %s..." % arg)
                prof.parse_symbols(self.open_file(arg, "r"))
            elif opt in ("-c", "--cycles"):
                stats.show_cycles(True)
            elif opt in ("-e", "--called"):
                stats.show_called(True)
            elif opt in ("-f", "--first"):
                stats.set_count(self.get_value(opt, arg, False))
            elif opt in ("-g", "--graph"):
                do_graphs = True
            elif opt in ("-i", "--instr"):
                stats.show_instructions(True)
            elif opt in ("-m", "--misses"):
                stats.show_misses(True)
            elif opt == "--ignore":
                graph.set_ignore(arg.split(','))
            elif opt == "--ignore-from":
                graph.set_ignore_from(arg.split(','))
            elif opt == "--ignore-to":
                graph.set_ignore_to(arg.split(','))
            elif opt == "--only":
                graph.set_only(arg.split(','))
            elif opt in ("-l", "--limit"):
                limit = self.get_value(opt, arg, True)
                graph.set_limit(limit)
                stats.set_limit(limit)
            elif opt in ("-o", "--output"):
                out = self.open_file(arg, "w")
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
            prof.parse_profile(self.open_file(arg, "r"))
            stats.set_profile(prof)
            stats.do_output()
            if do_graphs:
                self.do_graph(graph, prof, arg)

    def do_graph(self, graph, profile, fname):
        "output callgraph for given profile data for given file"
        if '.' in fname:
            dotname = fname[:fname.rindex('.')]
        dotname += ".dot"
        graph.set_output(self.open_file(dotname, "w"), fname)
        graph.set_profile(profile)
        if graph.do_output():
            self.message("\nGenerated '%s' callgraph DOT file." % dotname)
        else:
            os.remove(dotname)

    def open_file(self, path, mode):
        "open given path in given mode & return file object"
        try:
            return open(path, mode)
        except IOError, err:
            self.usage("opening given '%s' file in mode '%s' failed:\n\t%s" % (path, mode, err))

    def get_value(self, opt, arg, tofloat):
        "return numeric value for given string"
        try:
            if tofloat:
                return float(arg)
            else:
                return int(arg)
        except ValueError:
            self.usage("invalid '%s' numeric value: '%s'" % (opt, arg))

    def usage(self, msg):
        "show program usage + error message"
        self.message(__doc__)
        self.message("ERROR: %s!" % msg)
        sys.exit(1)


if __name__ == "__main__":
    Main(sys.argv).parse_args()
