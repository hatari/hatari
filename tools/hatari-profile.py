#!/usr/bin/env python
#
# Hatari profile data processor
#
# 2013 (C) Eero Tamminen, licensed under GPL v2+
#
# TODO: Output in Valgrind callgrind format:
#       http://valgrind.org/docs/manual/cl-format.html
# for KCachegrind:
#       http://kcachegrind.sourceforge.net/
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
	-a <symbols>	absolute symbol address information file
        -r <symbols>	TEXT (code section) relative symbols file
        -s		list profile statistics
        -t		list top functions for all profile items
        -f <count>	list at least first <count> items
        -l <limit>      list at least items which percentage >= limit
        -p		propagate costs up in call hierarchy
	-o <file name>	statistics output file name (default is stdout)
        -g              write <profile>.dot callgraph files
        -v		verbose parsing output

Long options for above are:
	--absolute
        --relative
        --stats
        --top
        --first
        --limit
        --propagate
        --output
        --graph
        --verbose


For example:
	hatari-profile -a etos512k.sym -st -g -f 10 prof1.txt prof2.txt

For each given profile file, output is:
- profile statistics
- a sorted list of functions, for each of the profile data items
  (calls, instructions, cycles...)
- callgraph in "dot" format, for each of the profile data items,
  for each profile file, saved to <name>-<itemindex>.dot files
  (prof1-0.dot, prof1-2.dot etc)


When both -l and -f options are specified, they're combined.  Produced
lists contain at least the number of items specified for -f, and more
if there are additional items which percentage of the total value is
larger than one given for -l.  In callgraphs these options just affect
which nodes are highlighted.


With the -p option, costs for a function include also costs for
everything else it calls.  Nodes which (propagated) cost percentages
of the total are below -l limit, are removed from the callgraphs.

NOTE: Because caller information in profile file has only call counts
between functions, other costs can be propagated correctly to callers
only when there's a single parent.  If there are multiple parents,
other costs are estimated based on call counts.  Don't trust the
estimated values:
- callgraphs nodes with them are diamond shaped
- in lists they're marked with '*'


Nodes with costs that exceed the highlight limit have red outline.
If node's own cost exceeds the limit, it has also gray background.


Callgraph filtering options to remove nodes and edges from the graph:
	--ignore <list>         no nodes for these symbols
	--ignore-to <list>	no arrows to these symbols
	--ignore-from <list>	no arrows from these symbols
        --only <list>           only these symbols and their callers

<list> is a comma separate list of symbol names, like this:
	--ignore-to _int_timerc,_int_vbl

Typically interrupt handler symbols are good to give for '--ignore-to'
option, as they can get called at any point.  Functions which are
called from everywhere (like malloc), may be good candinates for
'--ignore' option when one wants a more readable graph.  One can
then investigate them separately with the --only option.


To convert dot files e.g. to SVG, use:
	dot -Tsvg graph.dot > graph.svg

('dot' tool is in Graphviz package.)
"""
from bisect import bisect_right
import getopt, os, re, sys


class Output:
    "base class for error and file outputs"

    def __init__(self):
        self.error_write = sys.stderr.write
        self.write = sys.stdout.write

    def set_output(self, out):
        "set normal output data file"
        self.write = out.write

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


class FunctionStats:
    """Function (instructions) state. State is updated while profile
    data is being parsed and when function changes, instance is stored
    for later use."""

    def __init__(self, name, addr, line, items):
        "start collecting new 'name' function instructions information"
        self.name = name
        self.addr = addr
        # profile line on which function starts
        self.line = line
        # function costs items
        # field 0 is ALWAYS calls count and field 1 instructions count!
        self.data = [0] * items
        # propagated cost (from children)
        self.estimated = False	# whether totals are estimated
        self.total = None
        # calltree information
        self.parent = {}
        self.children = 0

    def has_data(self):
        "return whether function has valid data yet"
        # other values are valid only if there are instructions
        return (self.data[1] != 0)

    def name_for_address(self, addr):
        "if name but no address, use given address and return name, otherwise None"
        if self.name and not self.addr:
            self.addr = addr
            return self.name
        return None

    def rename(self, name, offset):
        "rename function with given address offset"
        self.addr -= offset
        self.name = name

    def add(self, values):
        "add list of values to current state"
        # first instruction in a function?
        data = self.data
        if not data[1]:
            # function call count is same as instruction
            # count for its first instruction
            data[0] = values[1]
        for i in range(1, len(data)):
            data[i] += values[i]

    def __repr__(self):
        "return printable current function instruction state"
        ret = "0x%x = %s: %s" % (self.addr, self.name, self.data)
        if self.total:
            if self.estimated:
                ret = "%s, total: %s (estimated)" % (ret, self.total)
            else:
                ret = "%s, total: %s" % (ret, self.total)
        if self.parent or self.children:
            return "%s, %d parents, %d children" % (ret, len(self.parent), self.children)
        return ret


class InstructionStats:
    "statistics on all instructions"
    # not changable, these are expectatations about the data fields
    # in this, FunctionStats, ProfileCallers and ProfileGraph classes
    callcount_field = 0
    instructions_field = 1

    def __init__(self, processor, hz, info):
        "function name, processor name, its speed, processor info dict"
        self.cycles_field = info["cycles_field"]
        self.names = info["fields"]
        self.items = len(self.names)
        self.max_line = [0] * self.items
        self.max_addr = [0] * self.items
        self.max_val = [0] * self.items
        self.totals = [0] * self.items
        self.processor = processor
        self.hz = hz
        self.areas = {}		# which memory area boundaries have been passed

    def change_area(self, function, name, addr):
        "switch to given area, if function not already in it, return True if switched"
        if addr > function.addr and name and name not in self.areas:
            self.areas[name] = True
            return True
        return False

    def add(self, addr, values, line):
        "add statistics for given list to current profile state"
        for i in range(1, self.items):
            value = values[i]
            if value > self.max_val[i]:
                self.max_val[i] = value
                self.max_addr[i] = addr
                self.max_line[i] = line

    def add_callcount(self, function):
        "add given function call count to statistics"
        value = function.data[0]
        if value > self.max_val[0]:
            self.max_val[0] = value
            self.max_addr[0] = function.addr
            self.max_line[0] = function.line

    def get_time(self, data):
        "return time (in seconds) spent by given data item"
        return float(data[self.cycles_field])/self.hz

    def sum_values(self, functions):
        "calculate totals for given functions data"
        if functions:
            sums = [0] * self.items
            for fun in functions:
                for i in range(self.items):
                    sums[i] += fun.data[i]
            self.totals = sums


class ProfileSymbols(Output):
    "class for handling parsing and matching symbols, memory areas and their addresses"

    def __init__(self, text_area):
        Output.__init__(self)
        self.names = None
        self.symbols = None	# (addr:symbol) dict for resolving
        self.absolute = {}	# (addr:symbol) dict of absolute symbols
        self.relative = {}	# (addr:symbol) dict of relative symbols
        self.symbols_need_sort = False
        self.symbols_sorted = None	# sorted list of symbol addresses
        self.areas = None
        # default emulation memory address range name
        # (for processor for which current data is for)
        self.default_area = None
        # TEXT area name
        self.text_area = text_area
        # memory areas:
        # <area>: 0x<hex>-0x<hex>
        # TOS:	0xe00000-0xe80000
        self.r_area = re.compile("^([^:]+):[^0]*0x([0-9a-f]+)-0x([0-9a-f]+)$")
        # symbol file format:
        # [0x]<hex> [tTbBdD] <symbol/objectfile name>
        self.r_symbol = re.compile("^(0x)?([a-fA-F0-9]+) ([bBdDtT]) ([$]?[-_.a-zA-Z0-9]+)$")

    def set_areas(self, areas, default):
        "set areas dict and default (zero-started) RAM area name"
        self.default_area = default
        self.areas = areas

    def parse_areas(self, fobj, parsed, verbose):
        "parse memory area lines from data"
        while True:
            line = fobj.readline()
            if not line:
                break
            match = self.r_area.match(line.strip())
            if not match:
                break
            parsed += 1
            name, start, end = match.groups()
            end = int(end, 16)
            start = int(start, 16)
            if name not in self.areas:
                self.warning("unrecognized memory area '%s' on line %d" % (name, parsed))
                continue
            self.areas[name] = (start, end)
            if end < start:
                self.error_exit("invalid memory area '%s': 0x%x-0x%x on line %d" % (name, start, end, parsed))
            elif verbose:
                self.message("memory area '%s': 0x%x-0x%x" % (name, start, end))
        self._relocate_symbols(verbose)
        return line, parsed

    def get_area(self, addr):
        "return memory area name + offset (used if no symbol matches)"
        for key, value in self.areas.items():
            if value[1] and addr >= value[0] and addr <= value[1]:
                return (key, addr - value[0])
        return (self.default_area, addr)

    def _check_symbol(self, addr, name, symbols):
        "return True if symbol is OK for addition"
        if addr in symbols:
            # symbol exists already for that address
            if name == symbols[addr]:
                return False
            # prefer function names over object names
            if name.endswith('.o'):
                return False
            oldname = symbols[addr]
            lendiff = abs(len(name) - len(oldname))
            minlen = min(len(name), min(oldname))
            # don't warn about object name replacements,
            # or adding/removing short prefix or postfix
            if not (oldname.endswith('.o') or
                    (lendiff < 3 and minlen > 3 and
                     (name.endswith(oldname) or oldname.endswith(name) or
                     name.startswith(oldname) or oldname.startswith(name)))):
                self.warning("replacing '%s' at 0x%x with '%s'" % (oldname, addr, name))
        return True

    def parse_symbols(self, fobj, is_relative):
        "parse symbol file contents"
        unknown = lines = 0
        if is_relative:
            symbols = self.relative
        else:
            symbols = self.absolute
        for line in fobj.readlines():
            lines += 1
            line = line.strip()
            if line.startswith('#'):
                continue
            match = self.r_symbol.match(line)
            if match:
                dummy, addr, kind, name = match.groups()
                if kind in ('t', 'T'):
                    addr = int(addr, 16)
                    if self._check_symbol(addr, name, symbols):
                        symbols[addr] = name
            else:
                self.warning("unrecognized symbol line %d:\n\t'%s'" % (lines, line))
                unknown += 1
        self.message("%d lines with %d code symbols/addresses parsed, %d unknown." % (lines, len(symbols), unknown))

    def _rename_symbol(self, addr, name):
        "return symbol name, potentially renamed if there were conflicts"
        if name in self.names:
            if addr == self.names[name]:
                return name
            # symbol with same name already exists at another address
            idx = 1
            while True:
                newname = "%s_%d" % (name, idx)
                if newname in self.names:
                    idx += 1
                    continue
                self.warning("renaming '%s' at 0x%x as '%s' to avoid clash with same symbol at 0x%x" % (name, addr, newname, self.names[name]))
                name = newname
                break
        self.names[name] = addr
        return name

    def _relocate_symbols(self, verbose):
        "combine absolute and relative symbols to single lookup"
        # renaming is done only at this point (after parsing memory areas)
        # to avoid addresses in names dict to be messed by relative symbols
        self.names = {}
        self.symbols = {}
        self.symbols_need_sort = True
        for addr, name in self.absolute.items():
            name = self._rename_symbol(addr, name)
            self.symbols[addr] = name
            if verbose:
                self.message("0x%x: %s (absolute)" % (addr, name))
        if not self.relative:
            return
        if self.text_area not in self.areas:
            self.error_exit("'%s' area range missing from profile, needed for relative symbols" % self.text_area)
        area = self.areas[self.text_area]
        for addr, name in self.relative.items():
            addr += area[0]
            # -1 used because compiler can add TEXT symbol right after end of TEXT section
            if addr < area[0] or addr-1 > area[1]:
                self.error_exit("relative symbol '%s' address 0x%x is outside of TEXT area: 0x%x-0x%x" % (name, addr, area[0], area[1]))
            if self._check_symbol(addr, name, self.symbols):
                name = self._rename_symbol(addr, name)
                self.symbols[addr] = name
                if verbose:
                    self.message("0x%x: %s (relative)" % (addr, name))

    def add_profile_symbol(self, addr, name):
        "add absolute symbol and return its name in case it got renamed"
        if self._check_symbol(addr, name, self.symbols):
            name = self._rename_symbol(addr, name)
            self.symbols[addr] = name
            self.symbols_need_sort = True
        return name

    def get_symbol(self, addr):
        "return symbol name for given address, or None"
        if addr in self.symbols:
            return self.symbols[addr]
        return None

    def get_preceeding_symbol(self, addr):
        "resolve non-function addresses to preceeding function name+offset"
        # should be called only after profile addresses has started
        if self.symbols:
            if self.symbols_need_sort:
                self.symbols_sorted = self.symbols.keys()
                self.symbols_sorted.sort()
                self.symbols_need_sort = False
            idx = bisect_right(self.symbols_sorted, addr) - 1
            if idx >= 0:
                saddr = self.symbols_sorted[idx]
                return (self.symbols[saddr], addr - saddr)
        return self.get_area(addr)


class ProfileCallers(Output):
    "profile data callee/caller information parser & handler"

    def __init__(self):
        Output.__init__(self)
        # caller info in callee line:
        # 0x<hex>: 0x<hex> = <count>, N*[0x<hex> = <count>,][ (<symbol>)
        # 0x<hex> = <count>
        self.r_caller = re.compile("^0x([0-9a-f]+) = ([0-9]+)$")
        # whether there is any caller info
        self.present = False
        # address dicts
        self.callinfo = None	# callee : caller dict

    def parse_callers(self, fobj, parsed, line):
        "parse callee: caller call count information"
        #0x<hex>: 0x<hex> = <count>, N*[0x<hex> = <count>,][ (<symbol>)
        self.callinfo = {}
        while True:
            if not line:
                break
            if not line.startswith("0x"):
                break
            callers = line.split(',')
            if len(callers) < 2:
                self.error_exit("caller info missing on callee line %d\n\t'%s'" % (parsed, line))
            if ':' not in callers[0]:
                self.error_exit("callee/caller separator ':' missing on callee line %d\n\t'%s'" % (parsed, line))
            last = callers[-1].strip()
            if len(last) and last[-1] != ')':
                self.error_exit("last item isn't empty or symbol name on callee line %d\n\t'%s'" % (parsed, line))

            addr, callers[0] = callers[0].split(':')
            callinfo = {}
            for caller in callers[:-1]:
                caller = caller.strip()
                match = self.r_caller.match(caller)
                if match:
                    caddr, count = match.groups()
                    caddr = int(caddr, 16)
                    count = int(count, 10)
                    callinfo[caddr] = count
                else:
                    self.error_exit("unrecognized caller info '%s' on callee line %d\n\t'%s'" % (caller, parsed, line))
            self.callinfo[int(addr, 16)] = callinfo
            parsed += 1
            line = fobj.readline()
        return line, parsed

    def complete(self, profile, symbols):
        "resolve caller functions and add child/parent info to profile data"
        if not self.callinfo:
            self.present = False
            return
        self.present = True
        # go through called functions
        for addr, caller in self.callinfo.items():
            child = profile[addr]
            # and their callers
            total = 0
            for item in caller.items():
                caddr, count = item
                cname, offset = symbols.get_preceeding_symbol(caddr)
                # function address for the caller
                cfaddr = caddr - offset
                parent = profile[cfaddr]
                assert(cname == parent.name)
                # link parent and child function together
                if cfaddr in child.parent:
                    child.parent[cfaddr] += (item,)
                else:
                    child.parent[cfaddr] = (item,)
                parent.children += 1
                total += count
            # validate call count
            calls = child.data[0]
            if calls != total:
                info = (child.name, addr, calls, total)
                self.warning("call count mismatch for '%s' at 0x%x, %d != %d" % info)
        self.callinfo = {}

    def _propagate_leaf_cost(self, profile, caddr, cost, track):
        """Propagate costs to function parents.  In case of call
	counts that can be done accurately based on existing call
	count information.  For other costs propagation can be done
	correctly only when there's a single parent."""
        child = profile[caddr]
        #self.message("processing '%s'" % child.name)
        parents = len(child.parent)
        if not parents:
            return
        # calls from all parents of this child,
        # float so that divisions don't lose info
        calls = float(child.data[0])

        for paddr, info in child.parent.items():
            if paddr == caddr:
                # loop within function or recursion
                continue
            parent = profile[paddr]
            if child.estimated or parents > 1:
                parent.estimated = True
            total = 0
            # add together calls from this particular parent
            for dummy, count in info:
                total += count
            if parents == 1 and total != calls:
                self.error_exit("%s -> %s, single parent, %d != %d" % (parent.name, child.name, total, calls))
            # parent's share of current propagated child costs
            ccost = [total * x / calls for x in cost]
            if parent.total:
                pcost = [sum(x) for x in zip(parent.total, ccost)]
            else:
                pcost = [sum(x) for x in zip(parent.data, ccost)]
                # propogate parent cost up only ones
                ccost = pcost
            parent.total = pcost
            if paddr not in track:
                track[paddr] = True
                self._propagate_leaf_cost(profile, paddr, ccost, track)
                del track[paddr]

    def propagate_costs(self, profile):
        "Identify leaf nodes and propagate costs from them to parents."
        leafs = {}
        for addr, function in profile.items():
            if function.children == 0:
                leafs[addr] = True
        # propagate leaf costs
        for addr in leafs.keys():
            function = profile[addr]
            self._propagate_leaf_cost(profile, addr, function.data, {})
        # verify that everything was visited and
        # convert values back to ints with rounding
        for addr, function in profile.items():
            if function.total:
                function.total = [int(round(x)) for x in function.total]
            elif function.children:
                for paddr, dummy in function.parent.items():
                    # other children than it itself?
                    if paddr != addr:
                        self.warning("didn't propagate cost to:\n\t%s\n\tParents:" % function)


class EmulatorProfile(Output):
    "Emulator profile data file parsing and profile information"

    def __init__(self, emuid, processor, symbols):
        Output.__init__(self)
        self.symbols = symbols		# ProfileSymbols instance
        self.processor = processor	# processor information dict
        self.callers = ProfileCallers()

        # profile data format
        #
        # emulator ID line:
        # <ID> <processor name> profile
        self.emuid = emuid
        # processor clock speed
        self.r_clock = re.compile("^Cycles/second:\t([0-9]+)$")
        # processor names, memory areas and their disassembly formats
        # are specified by subclasses with disasm argument
        self.r_address = None
        self.r_disasm = {}
        for key in processor.keys():
            assert(processor[key]['areas'] and processor[key]['fields'])
            self.r_disasm[key] = re.compile(processor[key]['regexp'])
        # <symbol/objectfile name>: (in disassembly)
        # _biostrap:
        self.r_function = re.compile("^([-_.a-zA-Z0-9]+):$")

        self.stats = None		# InstructionStats instance
        self.profile = None		# hash of profile (symbol:data)
        self.verbose = False
        self.linenro = 0

    def set_verbose(self, verbose):
        "set verbose on/off"
        self.verbose = verbose

    def parse_symbols(self, fobj, is_relative):
        "parse symbols from given file object"
        self.symbols.parse_symbols(fobj, is_relative)

    def _get_profile_type(self, fobj):
        "get profile processor type and speed information or exit if it's unknown"
        line = fobj.readline()
        field = line.split()
        if len(field) != 3 or field[0] != self.emuid:
            self.error_exit("unrecognized file, line 1:\n\t%smisses %s profiler identification" % (line, self.emuid))

        processor = field[1]
        if processor not in self.processor:
            self.error_exit("unrecognized profile processor type '%s' on line 1:\n\t%s" % (processor, line))
        self.symbols.set_areas(self.processor[processor]["areas"], "%s_RAM" % processor)

        line = fobj.readline()
        match = self.r_clock.match(line)
        if not match:
            self.error_exit("invalid %s clock information on line 2:\n\t%s" % (processor, line))
        info =  self.processor[processor]
        self.stats = InstructionStats(processor, int(match.group(1)), info)
        self.r_address = self.r_disasm[processor]
        return 2

    def _change_function(self, function, newname, addr):
        "store current function data and then reset to new function"
        if function.has_data():
            if not function.addr:
                name, offset = self.symbols.get_preceeding_symbol(function.addr)
                function.rename(name, offset)
            # addresses must increase in profile
            oldaddr = function.addr
            assert(oldaddr not in self.profile)
            self.stats.add_callcount(function)
            self.profile[oldaddr] = function
            if self.verbose:
                self.message(function)
        return FunctionStats(newname, addr, self.linenro, self.stats.items)

    def _check_symbols(self, function, addr):
        "if address is in new symbol (=function), change function"
        name = self.symbols.get_symbol(addr)
        if name:
            return self._change_function(function, name, addr)
        else:
            # as no better symbol, name it according to area where it moved to?
            name, offset = self.symbols.get_area(addr)
            addr -= offset
            if self.stats.change_area(function, name, addr):
                return self._change_function(function, name, addr)
        return function

    def _parse_line(self, function, addr, counts, discontinued):
        "parse given profile disassembly line match contents"
        newname = function.name_for_address(addr)
        if newname:
            # new symbol name finally got address on this profile line,
            # but it may need renaming due to symbol name clashes
            newname = self.symbols.add_profile_symbol(addr, newname)
            function.rename(newname, 0)
        elif discontinued:
            # continuation may skip to a function which name is not visible in profile file
            name, offset = self.symbols.get_preceeding_symbol(addr)
            symaddr = addr - offset
            # if changed area, preceeding symbol can be before area start,
            # so need to check both address, and name having changed
            if symaddr > function.addr and name != function.name:
                addr = symaddr
                if self.verbose:
                    self.message("DISCONTINUATION: %s at 0x%x -> %s at 0x%x" % (function.name, function.addr, name, addr))
                #if newname:
                #    self.warning("name_for_address() got name '%s' instead of '%s' from get_preceeding_symbol()" % (newname, name))
                function = self._change_function(function, name, addr)
                newname = name
        if not newname:
            function = self._check_symbols(function, addr)
        self.stats.add(addr, counts, self.linenro)
        function.add(counts)
        return function

    def _parse_disassembly(self, fobj, line):
        "parse profile disassembly"
        prev_addr = 0
        discontinued = False
        function = FunctionStats(None, 0, 0, self.stats.items)
        while True:
            if not line:
                break
            line = line.strip()
            if line == "[...]":
                # address discontinuation
                discontinued = True
            elif line.endswith(':'):
                # symbol
                match = self.r_function.match(line)
                if match:
                    function = self._change_function(function, match.group(1), 0)
                else:
                    self.error_exit("unrecognized function line %d:\n\t'%s'" % (self.linenro, line))
            else:
                # disassembly line
                match = self.r_address.match(line)
                if not match:
                    break
                addr = int(match.group(1), 16)
                if prev_addr > addr:
                    self.error_exit("memory addresses are not in order on line %d" % self.linenro)
                prev_addr = addr
                # counts[0] will be inferred call count
                counts = [0] + [int(x) for x in match.group(2).split(',')]
                function = self._parse_line(function, addr, counts, discontinued)
                discontinued = False
            # next line
            self.linenro += 1
            line = fobj.readline()
        # finish
        self._change_function(function, None, 0)
        return line

    def parse_profile(self, fobj):
        "parse profile data"
        self.profile = {}
        # header
        self.linenro = self._get_profile_type(fobj)
        # memory areas
        line, self.linenro = self.symbols.parse_areas(fobj, self.linenro, self.verbose)
        # instructions / memory addresses
        line = self._parse_disassembly(fobj, line)
        # caller information
        line, self.linenro = self.callers.parse_callers(fobj, self.linenro, line)
        # unrecognized lines
        if line:
            self.error_exit("unrecognized line %d:\n\t'%s'" % (self.linenro, line))
        # parsing info
        self.message("%d lines processed with %d functions." % (self.linenro, len(self.profile)))
        if len(self.profile) < 1:
            self.error_exit("no functions found!")
        # finish
        self.stats.sum_values(self.profile.values())
        self.callers.complete(self.profile, self.symbols)


class HatariProfile(EmulatorProfile):
    "EmulatorProfile subclass for Hatari with suitable data parsing regexps and processor information"
    def __init__(self):
        # Emulator name used as first word in profile file
        name = "Hatari"

        # name used for program code section in "areas" dicts below
        text_area = "PROGRAM_TEXT"

        # information on emulated processors
        #
        # * Non-overlapping memory areas that may be specified in profile,
        #   and their default values (zero = undefined at this stage).
        #   (Checked if instruction are before any of the symbol addresses)
        #
        # * Regexp for the processor disassembly information,
        #   its match contains 2 items:
        #   - instruction address
        #   - 3 comma separated performance values
        # 
        processors = {
            "CPU" : {
                "areas" : {
                    text_area	: (0, 0),
                    "ROM_TOS"	: (0xe00000, 0xe80000),
                    "CARTRIDGE"	: (0xfa0000, 0xfc0000)
                },
                # $<hex>  :  <ASM>  <percentage>% (<count>, <cycles>, <misses>)
                # $e5af38 :   rts           0.00% (12, 0, 12)
                "regexp" : "^\$([0-9a-f]+) :.*% \((.*)\)$",
                # First 2 fields are always function call counts and instruction
                # counts, then come ones from second "regexp" match group
                "fields" : ("Calls", "Executed instructions", "Used cycles", "Instruction cache misses"),
                "cycles_field": 2
            },
            "DSP" : {
                "areas" : {
                    text_area	: (0, 0),
                },
                # <space>:<address> <opcodes> (<instr cycles>) <instr> <count>% (<count>, <cycles>)
                # p:0202  0aa980 000200  (07 cyc)  jclr #0,x:$ffe9,p:$0200  0.00% (6, 42)
                "regexp" : "^p:([0-9a-f]+) .*% \((.*)\)$",
                # First 2 fields are always function call counts and instruction
                # counts, then come ones from second "regexp" match group
                "fields" : ("Calls", "Executed instructions", "Used cycles", "Largest cycle differences (= code changes during profiling)"),
                "cycles_field": 2
            }
        }
        symbols = ProfileSymbols(text_area)
        EmulatorProfile.__init__(self, name, processors, symbols)


class ProfileSorter:
    "profile information sorting and list output class"

    def __init__(self, profile, stats, write, propagated):
        self.profile = profile
        self.stats = stats
        self.write = write
        self.field = None
        self.show_propagated = propagated

    def _cmp_field(self, i, j):
        "compare currently selected field in profile data"
        field = self.field
        return cmp(self.profile[i].data[field], self.profile[j].data[field])

    def get_combined_limit(self, field, count, limit):
        "return percentage for given profile field that satisfies both count & limit constraint"
        if not count:
            return limit
        keys = self.profile.keys()
        if len(keys) <= count:
            return 0.0
        self.field = field
        keys.sort(self._cmp_field, None, True)
        total = self.stats.totals[field]
        function = self.profile[keys[count]]
        percentage = function.data[field] * 100.0 / total
        if percentage < limit or not limit:
            return percentage
        return limit

    def _output_list(self, keys, count, limit):
        "output list for currently selected field"
        field = self.field
        stats = self.stats
        total = stats.totals[field]
        self.write("\n%s:\n" % stats.names[field])

        time = idx = 0
        for addr in keys:
            mark = ""
            function = self.profile[addr]
            if self.show_propagated and function.total:
                value = function.total[field]
                if function.estimated and field != 0:
                    mark = "*"
            else:
                value = function.data[field]
            if not value:
                break

            percentage = 100.0 * value / total
            if count and limit:
                # if both list limits are given, both must be exceeded
                if percentage < limit and idx >= count:
                    break
            elif limit and percentage < limit:
                break
            elif count and idx >= count:
                break
            idx += 1

            if field == stats.cycles_field:
                time = stats.get_time(function.data)
                info = "(0x%06x,%9.5fs)" % (addr, time)
            else:
                info = "(0x%06x)" % addr
            self.write("%6.2f%% %9s%2s %-28s%s\n" % (percentage, value, mark, function.name, info))

    def do_list(self, field, count, limit):
        "sort and show list for given profile data field"
        if self.stats.totals[field] == 0:
            return
        self.field = field
        keys = self.profile.keys()
        keys.sort(self._cmp_field, None, True)
        self._output_list(keys, count, limit)


class ProfileOutput(Output):
    "base class for profile output options"

    def __init__(self):
        Output.__init__(self)
        # both unset so that subclasses can set defaults reasonable for them
        self.show_propagated = False
        self.limit = 0.0
        self.count = 0

    def enable_propagated(self):
        "enable showing propagated costs instead of just own costs"
        self.show_propagated = True

    def set_count(self, count):
        "set how many items to show or highlight at minimum, 0 = all/unset"
        if count < 0:
            self.error_exit("Invalid item count: %d" % count)
        self.count = count

    def set_limit(self, limit):
        "set percentage is shown or highlighted at minimum, 0.0 = all/unset"
        if limit < 0.0 or limit > 100.0:
            self.error_exit("Invalid percentage: %d" % limit)
        self.limit = limit


class ProfileStats(ProfileOutput):
    "profile information statistics output"

    def __init__(self):
        ProfileOutput.__init__(self)
        self.limit = 1.0
        self.sorter = None
        self.show_totals = False
        self.show_top = False

    def enable_totals(self):
        "enable totals list"
        self.show_totals = True

    def enable_top(self):
        "enable showing listing for top items"
        self.show_top = True

    def output_totals(self, profobj):
        "output profile statistics"
        stats = profobj.stats
        time = stats.get_time(stats.totals)
        self.write("\nTime spent in profile = %.5fs.\n\n" % time)

        symbols = profobj.symbols
        items = len(stats.totals)
        for i in range(items):
            if not stats.totals[i]:
                continue
            addr = stats.max_addr[i]
            name, offset = symbols.get_preceeding_symbol(addr)
            if name:
                if offset:
                    name = " in %s+%d" % (name, offset)
                else:
                    name = " in %s" % name
            self.write("%s:\n" % stats.names[i])
            info = (stats.max_val[i], name, addr, stats.max_line[i])
            self.write("- max = %d,%s at 0x%x, on line %d\n" % info)
            self.write("- %d in total\n" % stats.totals[i])

    def do_output(self, profobj):
        "output enabled lists"
        if self.show_totals:
            self.output_totals(profobj)
        if self.show_top:
            if self.show_propagated:
                self.write("\nPropagated costs which could only be estimated\n(i.e. are unreliable) are marked with '*'.\n")
            sorter = ProfileSorter(profobj.profile, profobj.stats, self.write, self.show_propagated)
            fields = range(profobj.stats.items)
            for field in fields:
                sorter.do_list(field, self.count, self.limit)


class ProfileGraph(ProfileOutput):
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
        ProfileOutput.__init__(self)
        self.count = 8
        self.nodes = None
        self.edges = None
        self.highlight = None
        self.output_enabled = False
        self.only = []
        self.ignore = []
        self.ignore_from = []
        self.ignore_to = []

    def enable_output(self):
        "enable output"
        self.output_enabled = True

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

    def _filter_profile(self, profobj, field):
        "filter profile content to nodes and edges members"
        profile = profobj.profile
        self.nodes = {}
        self.edges = {}
        ignore_to = self.ignore_to + self.ignore
        ignore_from = self.ignore_from + self.ignore
        for caddr, child in profile.items():
            if child.name in ignore_to:
                continue
            for paddr, info in child.parent.items():
                parent = profile[paddr]
                # no recursion
                # if caddr == paddr:
                #    continue
                if self.only:
                    if not (child.name in self.only or parent.name in self.only):
                        continue
                # child end for edges
                self.nodes[caddr] = True
                if parent.name in ignore_from:
                    continue
                # parent end for edges
                self.nodes[paddr] = True
                # total calls count for child
                calls = profile[caddr].data[0]
                # calls to child done from different locations in parent
                for laddr, count in info:
                    self.edges[laddr] = (paddr, caddr, count, calls)
        if self.nodes:
            return profile
        return None

    def _output_nodes(self, profile, stats, field, limit):
        "output graph nodes from filtered nodes dict"
        self.highlight = {}
        total = stats.totals[field]
        for addr in self.nodes.keys():
            shape = style = ""
            function = profile[addr]
            if self.show_propagated and function.total:
                values = function.total
                if function.estimated:
                    shape = " shape=diamond"
            else:
                values = function.data
            count = values[field]
            percentage = 100.0 * count / total
            if percentage >= limit:
                self.highlight[addr] = True
                style = " color=red"
            ownpercentage = 100.0 * function.data[field] / total
            if ownpercentage >= limit:
                style = "%s style=filled fillcolor=lightgray" % style
            name = function.name
            if field == 0:
                # calls aren't estimated so they don't need different shapes
                self.write("N_%X [label=\"%.2f%%\\n%s\\n%d calls\"%s];\n" % (addr, percentage, name, count, style))
                continue
            calls = values[0]
            if field == stats.cycles_field:
                time = stats.get_time(values)
                self.write("N_%X [label=\"%.2f%%\\n%.5fs\\n%s\\n(%d calls)\"%s%s];\n" % (addr, percentage, time, name, calls, style, shape))
            else:
                self.write("N_%X [label=\"%.2f%%\\n%d\\n%s\\n(%d calls)\"%s%s];\n" % (addr, percentage, count, name, calls, style, shape))

    def _output_edges(self, profile):
        "output graph edges from filtered edges dict, after nodes is called"
        for laddr, data in self.edges.items():
            paddr, caddr, count, calls = data
            pname = profile[paddr].name
            offset = laddr - paddr
            style = ""
            if caddr in self.highlight or paddr in self.highlight:
                style = " color=red"
            if offset:
                label = "%s+%d\\n($%x)" % (pname, offset, laddr)
            else:
                label = pname
            if count != calls:
                percentage = 100.0 * count / calls
                label = "%s\\n%d calls\\n=%.2f%%" % (label, count, percentage)
            self.write("N_%X -> N_%X [label=\"%s\"%s];\n" % (paddr, caddr, label, style))

    def do_output(self, profobj, fname):
        "output graphs for given profile data"
        if not (self.output_enabled and profobj.callers.present):
            return
        stats = profobj.stats
        basename = os.path.splitext(fname)[0]
        for field in range(profobj.stats.items):
            if not stats.totals[field]:
                continue
            profile = self._filter_profile(profobj, field)
            if not profile:
                continue
            dotname = "%s-%d.dot" % (basename, field)
            self.message("\nGenerating '%s' callgraph DOT file..." % dotname)
            try:
                self.set_output(open(dotname, "w"))
            except IOError, err:
                self.warning(err)
                continue
            name = stats.names[field]
            title = "%s, for %s" % (name, fname)
            if self.show_propagated:
                title += "\\n(Nodes which propagated costs could only be estimated (i.e. are unreliable) have diamond shape)"
            self.write(self.header % title)
            sorter = ProfileSorter(profile, stats, None, False)
            limit = sorter.get_combined_limit(field, self.count, self.limit)
            self._output_nodes(profile, stats, field, limit)
            self._output_edges(profile)
            self.write(self.footer)


class Main(Output):
    "program main loop & args parsing"
    longopts = [
        "absolute=",
        "first",
        "graph",
        "ignore=",
        "ignore-to=",
        "ignore-from=",
        "limit=",
        "only=",
        "output=",
        "propagate",
        "relative=",
        "stats",
        "top",
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
            opts, rest = getopt.getopt(self.args, "a:e:f:gl:o:pr:stv", self.longopts)
        except getopt.GetoptError as err:
            self.usage(err)

        propagate = False
        prof = HatariProfile()
        graph = ProfileGraph()
        stats = ProfileStats()
        for opt, arg in opts:
            #self.message("%s: %s" % (opt, arg))
            # options for profile symbol parsing
            if opt in ("-a", "--absolute"):
                self.message("\nParsing absolute symbol address information from %s..." % arg)
                prof.parse_symbols(self.open_file(arg, "r"), False)
            elif opt in ("-r", "--relative"):
                self.message("\nParsing TEXT relative symbol address information from %s..." % arg)
                prof.parse_symbols(self.open_file(arg, "r"), True)
            # options for both graphs & statistics
            elif opt in ("-f", "--first"):
                count = self.get_value(opt, arg, False)
                graph.set_count(count)
                stats.set_count(count)
            elif opt in ("-l", "--limit"):
                limit = self.get_value(opt, arg, True)
                graph.set_limit(limit)
                stats.set_limit(limit)
            elif opt in ("-p", "--propagate"):
                graph.enable_propagated()
                stats.enable_propagated()
                propagate = True
            # options specific to graphs
            elif opt in ("-g", "--graph"):
                graph.enable_output()
            elif opt == "--ignore":
                graph.set_ignore(arg.split(','))
            elif opt == "--ignore-from":
                graph.set_ignore_from(arg.split(','))
            elif opt == "--ignore-to":
                graph.set_ignore_to(arg.split(','))
            elif opt == "--only":
                graph.set_only(arg.split(','))
            # options specific to statistics
            elif opt in ("-s", "--stats"):
                stats.enable_totals()
            elif opt in ("-t", "--top"):
                stats.enable_top()
            # options for every class
            elif opt in ("-o", "--output"):
                out = self.open_file(arg, "w")
                self.message("\nSet output to go to '%s'." % arg)
                self.set_output(out)
                prof.set_output(out)
                stats.set_output(out)
            # options specific for profile parsing
            elif opt in ("-v", "--verbose"):
                prof.set_verbose(True)
            else:
                self.usage("unknown option '%s' with value '%s'" % (opt, arg))
        for arg in rest:
            self.message("\nParsing profile information from %s..." % arg)
            prof.parse_profile(self.open_file(arg, "r"))
            if propagate:
                # TODO: do this step automatically in callers.complete()
                # after this functionality is rewritten faster
                self.message("Propagating costs in call tree (SLOW, takes exponential time)...")
                prof.callers.propagate_costs(prof.profile)
            self.write("\nProfile information from '%s':\n" % arg)
            stats.do_output(prof)
            graph.do_output(prof, arg)

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
