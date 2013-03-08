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
A tool for post-processing emulator HW profiling data.

In Hatari debugger you get (CPU) profiling data with the following
commands (for Falcon DSP data, prefix commands with 'dsp'):
	profile on
        continue
        ...
	profile save <file name>

Profiling information for code addresses is summed together and
assigned to functions where those addresses belong to. All addresses
between two function names (in profile file) or symbol addresses
(read from symbols files) are assumed to belong to the preceeding
function/symbol.

Tool output will contain at least:
- (deduced) call counts,
- executed instruction counts, and
- spent processor cycles.

If profile data contains other information (e.g. cache misses),
that is also shown.

Provided symbol information should be in same format as for Hatari
debugger 'symbols' command.  Note that files containing absolute
addresses and ones containing relatives addresses need to be given
with different options!


Usage: hatari-profile [options] <profile files>

Options:
	-a <symbols>	absolute symbol address information file
        -r <symbols>	TEXT (code section) relative symbols file
        -s		output profile statistics
        -t		list top functions for all profile items
        -i		add address and time information to lists
        -f <count>	list at least first <count> items
        -l <limit>      list at least items which percentage >= limit
        -p		propagate costs up in call hierarchy
	-o <file name>	statistics output file name (default is stdout)
        -g              write <profile>.dot callgraph files
        -v		verbose output

Long options for above are:
	--absolute
        --relative
        --stats
        --top
        --info
        --first
        --limit
        --propagate
        --output
        --graph
        --verbose

(Time info is shown only for cycles list.)

For example:
	hatari-profile -a etos512k.sym -st -g -f 10 prof1.txt prof2.txt

For each given profile file, output is:
- profile statistics
- a sorted list of functions, for each of the profile data items
  (calls, instructions, cycles...)
- callgraph in DOT format for each of the profile data items, in
  each profile file, saved to <filename>-<itemindex>.dot files
  (prof1-0.dot, prof1-2.dot etc)


When both -l and -f options are specified, they're combined.  Produced
lists contain at least the number of items specified for -f option,
and more if there are additional items which percentage of the total
value is larger than one given for -l option.  In callgraphs these
options just affect which nodes are highlighted unless -p option is
used.


With the -p option, costs for a function include also (estimated)
costs for everything else it calls.  Nodes which (propagated) cost
percentages of the total are below -l limit, are removed from the
callgraphs.

NOTE: Because caller information in profile file has only call counts
between functions, other costs can be propagated correctly to callers
only when there's a single parent.  If there are multiple parents,
other costs are estimated based on call counts.

Don't trust the estimated values:
- estimated total costs in lists are prefixed with '~'
- callgraphs nodes with estimated totals are diamond shaped


Call information filtering options:
        --no-calls <[bersux]+>	remove calls of given types, default = 'ux'
	--ignore-to <list>	ignore calls to these symbols

(Give --no-calls option an unknown type to see type descriptions.)

<list> is a comma separate list of symbol names, like this:
	--ignore-to _int_timerc,_int_vbl

These options affect the number of calls reported for functions and
the values that are propagated upwards from them with the -p option.

If default --no-calls type removal doesn't remove all interrupt
handler switches (switching to them gets recorded as a call by the
profiler, and those switches can happen at any time), give handler
names to --ignore-to option.  In callgraphs, one can then investigate
them separately using "no-calls '' --only <name>" options.


Callgraph visualization options:
        -e, --emph-limit <limit>  percentage limit for highlighted nodes

When -e limit is given, -f & -e options are used for deciding which
nodes to highlight, not -f & -l options.

Nodes with costs that exceed the highlight limit have red outline.
If node's own cost exceeds the limit, it has also gray background.


Callgraph filtering options to remove nodes and edges from the graph:
	--compact		only 1 arrow between nodes, instead of
        			arrow for each call site within function
	--no-intermediate	remove nodes with single parent & child
	--no-leafs		remove nodes which have either:
				- one parent and no children, or
				- one child and no parents
	--ignore <list>         no nodes for these symbols
	--ignore-from <list>	no arrows from these symbols
        --only <list>           only these symbols and their callers

By default, leaf and intermediate node removal options remove only
nodes which fall below -l option limit. But with -p option, they
remove all leaf and intermdiate nodes as their costs are propagated
to their parents, i.e. visible in rest of the graph.

Functions which are called from everywhere (like malloc), may be good
candinates for '--ignore' option when one wants a more readable graph.
One can then investigate them separately with the '--only <function>'
option.


To convert dot files e.g. to SVG, use:
	dot -Tsvg graph.dot > graph.svg

('dot' tool is in Graphviz package.)
"""
from copy import deepcopy
from bisect import bisect_right
import getopt, os, re, sys


class Output:
    "base class for error and file outputs"

    def __init__(self):
        self.error_write = sys.stderr.write
        self.write = sys.stdout.write
        self.verbose = False

    def enable_verbose(self):
        "enable verbose output"
        self.verbose = True

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
        # calltree linkage
        self.parent = {}
        self.child = {}

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
        if self.parent or self.child:
            return "%s, %d parents, %d children" % (ret, len(self.parent), len(self.child))
        return ret


class InstructionStats:
    "statistics on all instructions"
    # not changable, these are expectatations about the data fields
    # in this, FunctionStats, ProfileCallers and ProfileGraph classes
    callcount_field = 0
    instructions_field = 1
    cycles_field = 2

    def __init__(self, processor, clock, fields):
        "processor name, its speed and profile field names"
        # Calls item will be deducted from instruction values
        self.names = ["Calls"] + fields
        self.items = len(self.names)
        self.max_line = [0] * self.items
        self.max_addr = [0] * self.items
        self.max_val = [0] * self.items
        self.totals = [0] * self.items
        self.processor = processor
        self.clock = clock	# in Hz
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
        return float(data[self.cycles_field])/self.clock

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
    # profile file field name for program text/code address range
    text_area = "PROGRAM_TEXT"
    # default emulation memory address range name
    default_area = "RAM"

    def __init__(self):
        Output.__init__(self)
        self.names = None
        self.symbols = None	# (addr:symbol) dict for resolving
        self.absolute = {}	# (addr:symbol) dict of absolute symbols
        self.relative = {}	# (addr:symbol) dict of relative symbols
        self.symbols_need_sort = False
        self.symbols_sorted = None	# sorted list of symbol addresses
        # Non-overlapping memory areas that may be specified in profile file
        # (checked if instruction is before any of the symbol addresses)
        self.areas = {}		# (name:(start,end))
        # memory area format:
        # <area>: 0x<hex>-0x<hex>
        # TOS:	0xe00000-0xe80000
        self.r_area = re.compile("^([^:]+):[^0]*0x([0-9a-f]+)-0x([0-9a-f]+)$")
        # symbol file format:
        # [0x]<hex> [tTbBdD] <symbol/objectfile name>
        self.r_symbol = re.compile("^(0x)?([a-fA-F0-9]+) ([bBdDtT]) ([$]?[-_.a-zA-Z0-9]+)$")

    def parse_areas(self, fobj, parsed):
        "parse memory area lines from data"
        while True:
            parsed += 1
            line = fobj.readline()
            if not line:
                break
            if line.startswith('#'):
                continue
            match = self.r_area.match(line.strip())
            if not match:
                break
            name, start, end = match.groups()
            end = int(end, 16)
            start = int(start, 16)
            self.areas[name] = (start, end)
            if end < start:
                self.error_exit("invalid memory area '%s': 0x%x-0x%x on line %d" % (name, start, end, parsed))
            elif self.verbose:
                self.message("memory area '%s': 0x%x-0x%x" % (name, start, end))
        self._relocate_symbols()
        return line, parsed-1

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

    def _relocate_symbols(self):
        "combine absolute and relative symbols to single lookup"
        # renaming is done only at this point (after parsing memory areas)
        # to avoid addresses in names dict to be messed by relative symbols
        self.names = {}
        self.symbols = {}
        self.symbols_need_sort = True
        for addr, name in self.absolute.items():
            name = self._rename_symbol(addr, name)
            self.symbols[addr] = name
            if self.verbose:
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
                if self.verbose:
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
        self.r_caller = re.compile("^0x([0-9a-f]+) = ([0-9]+)( [a-z]+)?$")
        # whether there is any caller info
        self.present = False
        # address dicts
        self.callinfo = None	# callee : caller dict
        # parsing options
        self.removable_calltypes = "ux"
        self.ignore_to = []
        self.compact = False

    def set_ignore_to(self, lst):
        "set list of symbols to ignore calls to"
        self.ignore_to = lst

    def enable_compact(self):
        "enable: single entry between two functions"
        self.compact = True

    def remove_calls(self, types):
        "ignore calls of given type"
        alltypes = {
            'b': "branches/jumps",	# could be calls
            's': "subroutine calls",	# are calls
            'r': "subroutine returns",	# shouldn't be calls
            'e': "exceptions",		# are calls
            'x': "exception returns",	# shouldn't be calls
            'u': "unknown"		# shouldn't be calls
        }
        for letter in types:
            if letter not in alltypes:
                self.message("Valid call types are:")
                for item in alltypes.items():
                    self.message("  %c -- %s" % item)
                self.error_exit("invalid call type")
        self.removable_calltypes = types

    def parse_callers(self, fobj, parsed, line):
        "parse callee: caller call count information"
        #0x<hex>: 0x<hex> = <count>, N*[0x<hex> = <count>,][ (<symbol>)
        self.callinfo = {}
        while True:
            parsed += 1
            if not line:
                break
            if line.startswith('#'):
                line = fobj.readline()
                continue
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
                    caddr, count, flags = match.groups()
                    caddr = int(caddr, 16)
                    count = int(count, 10)
                    if flags:
                        flags = flags.strip()
                    else:
                        flags = '-'
                    callinfo[caddr] = (count, flags)
                else:
                    self.error_exit("unrecognized caller info '%s' on callee line %d\n\t'%s'" % (caller, parsed, line))
            self.callinfo[int(addr, 16)] = callinfo
            line = fobj.readline()
        return line, parsed-1

    def complete(self, profile, symbols):
        "resolve caller functions and add child/parent info to profile data"
        if not self.callinfo:
            self.present = False
            return
        self.present = True
        # go through called functions...
        for caddr, caller in self.callinfo.items():
            child = profile[caddr]
            if child.name in self.ignore_to:
                continue
            # ...and their callers
            ignore = total = 0
            for item in caller.items():
                laddr, info = item
                count, flags = info
                pname, offset = symbols.get_preceeding_symbol(laddr)
                if len(flags) > 1:
                    self.warning("caller instruction change ('%s') detected for '%s', did its code change during profiling?" % (flags, pname))
                elif flags in self.removable_calltypes:
                    ignore += count
                    continue
                # function address for the caller
                paddr = laddr - offset
                parent = profile[paddr]
                if pname != parent.name:
                    self.warning("overriding parsed function 0x%x name '%s' with resolved caller 0x%x name '%s'" % (parent.addr, parent.name, paddr, pname))
                    parent.name = pname
                # link parent and child function together
                item = (laddr, count)
                if paddr in child.parent:
                    if self.compact:
                        oldcount = child.parent[paddr][0][1]
                        child.parent[paddr] = ((paddr, oldcount + count),)
                    else:
                        child.parent[paddr] += (item,)
                else:
                    child.parent[paddr] = (item,)
                parent.child[caddr] = True
                total += count
            # validate call count
            if ignore:
                self.message("Ignoring %d call(s) to '%s'" % (ignore, child.name))
                #total += ignore
                child.data[0] -= ignore
            calls = child.data[0]
            if calls != total:
                info = (child.name, caddr, calls, total)
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
                self.error_exit("%s -> %s, single parent, but callcounts don't match: %d != %d" % (parent.name, child.name, total, calls))
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
            if not function.child:
                leafs[addr] = True
        # propagate leaf costs
        for addr in leafs.keys():
            function = profile[addr]
            self._propagate_leaf_cost(profile, addr, function.data, {})
        # convert values back to ints with rounding, and
        # verify that every non-leaf node with parents was visited
        for addr, function in profile.items():
            if function.total:
                function.total = [int(round(x)) for x in function.total]
            elif function.child and function.parent:
                for paddr, dummy in function.parent.items():
                    # other parent than it itself?
                    if paddr != addr:
                        self.warning("didn't propagate cost to:\n\t%s\n" % function)


class EmulatorProfile(Output):
    "Emulator profile data file parsing and profile information"

    def __init__(self):
        Output.__init__(self)
        self.symbols = ProfileSymbols()
        self.callers = ProfileCallers()

        # profile data format
        #
        # emulator ID line:
        # <Emulator> <processor name> profile
        #
        # processor clock speed
        self.r_clock = re.compile("^Cycles/second:\t([0-9]+)$")
        # field names
        self.r_fields = re.compile("^Field names:\t(.*)$")
        # processor disassembly format regexp is gotten from profile file
        self.r_regexp = re.compile("Field regexp:\t(.*)$")
        self.r_address = None
        # memory address information is parsed by ProfileSymbols
        #
        # this class parses symbols from disassembly itself:
        # <symbol/objectfile name>: (in disassembly)
        # _biostrap:
        self.r_function = re.compile("^([-_.a-zA-Z0-9]+):$")

        self.stats = None		# InstructionStats instance
        self.profile = None		# hash of profile (symbol:data)
        self.linenro = 0

    def enable_verbose(self):
        "set verbose output in this and member class instances"
        Output.enable_verbose(self)
        self.symbols.enable_verbose()
        self.callers.enable_verbose()

    def remove_calls(self, types):
        self.callers.remove_calls(types)

    def set_ignore_to(self, lst):
        self.callers.set_ignore_to(lst)

    def enable_compact(self):
        self.callers.enable_compact()

    def parse_symbols(self, fobj, is_relative):
        "parse symbols from given file object"
        self.symbols.parse_symbols(fobj, is_relative)

    def _get_profile_type(self, fobj):
        "get profile processor type and speed information or exit if it's unknown"
        line = fobj.readline()
        field = line.split()
        if len(field) != 3 or field[2] != "profile":
            self.error_exit("unrecognized file, line 1\n\t%s\nnot in format:\n\t<emulator> <processor> profile" % (line))
        processor = field[1]

        line = fobj.readline()
        match = self.r_clock.match(line)
        if not match:
            self.error_exit("invalid %s clock HZ information on line 2:\n\t%s" % (processor, line))
        clock = int(match.group(1))

        line = fobj.readline()
        match = self.r_fields.match(line)
        if not match:
            self.error_exit("invalid %s profile disassembly field descriptions on line 3:\n\t%s" % (processor, line))
        fields = [x.strip() for x in match.group(1).split(',')]
        self.stats = InstructionStats(processor, clock, fields)

        line = fobj.readline()
        match = self.r_regexp.match(line)
        try:
            self.r_address = re.compile(match.group(1))
        except (AttributeError, re.error) as error:
            self.error_exit("invalid %s profile disassembly regexp on line 4:\n\t%s\n%s" % (processor, line, error))
        return 4

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
            self.linenro += 1
            line = line.strip()
            if line.startswith('#'):
                pass
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
        line, self.linenro = self.symbols.parse_areas(fobj, self.linenro)
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
        if self.show_propagated and function.total:
            value = function.total[field]
        else:
            value = function.data[field]
        percentage = value * 100.0 / total
        if percentage < limit or not limit:
            return percentage
        return limit

    def _output_list(self, keys, count, limit, show_info):
        "output list for currently selected field"
        field = self.field
        stats = self.stats
        total = stats.totals[field]
        self.write("\n%s:\n" % stats.names[field])

        time = idx = 0
        for addr in keys:
            function = self.profile[addr]
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

            # show also propagated cost?
            propagated = ""
            if self.show_propagated:
                if function.total:
                    ppercent = 100.0 * function.total[field] / total
                    # first field (=call) counts are always
                    if field > 0 and function.estimated:
                        propagated = " ~%6.2f%%" % ppercent
                    else:
                        propagated = "  %6.2f%%" % ppercent
                else:
                    propagated = " " * 9

            if show_info:
                if field == stats.cycles_field:
                    time = stats.get_time(function.data)
                    info = "(0x%06x,%9.5fs)" % (addr, time)
                else:
                    info = "(0x%06x)" % addr
                self.write("%6.2f%%%s %9d  %-28s%s\n" % (percentage, propagated, value, function.name, info))
            else:
                self.write("%6.2f%%%s %9d  %s\n" % (percentage, propagated, value, function.name))

    def do_list(self, field, count, limit, show_info):
        "sort and show list for given profile data field"
        if self.stats.totals[field] == 0:
            return
        self.field = field
        keys = self.profile.keys()
        keys.sort(self._cmp_field, None, True)
        self._output_list(keys, count, limit, show_info)


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
        self.show_info = False

    def enable_totals(self):
        "enable totals list"
        self.show_totals = True

    def enable_top(self):
        "enable showing listing for top items"
        self.show_top = True

    def enable_info(self):
        "enable showing extra info for list items"
        self.show_info = True

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
            sorter = ProfileSorter(profobj.profile, profobj.stats, self.write, self.show_propagated)
            fields = range(profobj.stats.items)
            for field in fields:
                sorter.do_list(field, self.count, self.limit, self.show_info)


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
        self.profile = None
        self.count = 8
        self.nodes = None
        self.edges = None
        self.highlight = None
        self.output_enabled = False
        self.remove_intermediate = False
        self.remove_leafs = False
        self.only = []
        self.ignore = []
        self.ignore_from = []
        self.emph_limit = 0

    def enable_output(self):
        "enable output"
        self.output_enabled = True

    def disable_intermediate(self):
        "disable showing nodes which have just single parent/child"
        # TODO: move leaf/intermediate handling to ProfileCallers class?
        self.remove_intermediate = True

    def disable_leafs(self):
        "disable showing nodes which don't have children"
        self.remove_leafs = True

    def set_only(self, lst):
        "set list of only symbols to include"
        self.only = lst

    def set_ignore(self, lst):
        "set list of symbols to ignore"
        self.ignore = lst

    def set_ignore_from(self, lst):
        "set list of symbols to ignore calls from"
        self.ignore_from = lst

    def set_emph_limit(self, limit):
        "set emphatize percentage limit"
        self.emph_limit = limit

    def _remove_from_profile(self, addr):
        "remove function with given address from profile"
        profile = self.profile
        function = profile[addr]
        if self.verbose:
            self.message("removing leaf/intermediate node %s" % function)
        parents = list(function.parent.keys())
        children = list(function.child.keys())
       # remove it from items linking it
        for paddr in parents:
            if paddr != addr:
                parent = profile[paddr]
                # link parent directly to its grandchildren
                for  caddr in children:
                    if caddr != addr:
                        parent.child[caddr] = True
                # remove its parent's linkage
                del parent.child[addr]
        for caddr in children:
            if caddr != addr:
                child = profile[caddr]
                info = child.parent[addr]
                # link child directly to its grandparents
                for  paddr in parents:
                    if paddr != addr:
                        #self.message("%s: %s" % (parent.name, info))
                        if paddr in child.parent:
                            child.parent[paddr] += info
                        else:
                            child.parent[paddr] = info
                # remove its child's linkage
                del child.parent[addr]
        # remove it itself
        del profile[addr]

    def _set_reduced_profile(self, profobj):
        "get relinked copy of profile data with requested items removed from it"
        if not (self.remove_leafs or self.remove_intermediate):
            self.profile = profobj.profile
            return
        # need our own copy so that it can be manipulated freely
        self.profile = deepcopy(profobj.profile)
        while True:
            to_remove = {}
            for addr, function in self.profile.items():
                parents = len(function.parent)
                children = len(function.child)
                if self.remove_leafs and (parents + children) == 1:
                    to_remove[addr] = True
                elif self.remove_intermediate and parents == 1 and children == 1:
                    to_remove[addr] = True

            if not to_remove:
                break
            for addr in to_remove.keys():
                self._remove_from_profile(addr)

    def _filter_profile(self):
        "filter profile content to nodes and edges members based on ignore options"
        profile = self.profile
        self.nodes = {}
        self.edges = {}
        ignore_from = self.ignore_from + self.ignore
        for caddr, child in profile.items():
            if child.name in self.ignore:
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
            return True
        return False

    def _output_nodes(self, stats, field, limit):
        "output graph nodes from filtered nodes dict"
        self.highlight = {}
        total = stats.totals[field]
        for addr in self.nodes.keys():
            shape = style = ""
            function = self.profile[addr]
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

    def _output_edges(self):
        "output graph edges from filtered edges dict, after nodes is called"
        for laddr, data in self.edges.items():
            paddr, caddr, count, calls = data
            pname = self.profile[paddr].name
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
            # get potentially reduced instance copy of profile data
            self._set_reduced_profile(profobj)
            if not self._filter_profile():
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
            # limits are taken from full profile, not potentially reduced one
            sorter = ProfileSorter(profobj.profile, stats, None, False)
            if self.emph_limit:
                limit = sorter.get_combined_limit(field, self.count, self.emph_limit)
            else:
                limit = sorter.get_combined_limit(field, self.count, self.limit)
            self._output_nodes(stats, field, limit)
            self._output_edges()
            self.write(self.footer)


class Main(Output):
    "program main loop & args parsing"
    longopts = [
        "absolute=",
        "compact",
        "emph-limit=",
        "first",
        "graph",
        "ignore=",
        "ignore-to=",
        "ignore-from=",
        "info"
        "limit=",
        "no-calls=",
        "no-intermediate",
        "no-leafs",
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
            opts, rest = getopt.getopt(self.args, "a:e:f:gil:o:pr:stv", self.longopts)
        except getopt.GetoptError as err:
            self.usage(err)

        propagate = False
        prof = EmulatorProfile()
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
            # options for profile caller information parsing
            elif opt == "--compact":
                prof.enable_compact()
            elif opt == "--no-calls":
                prof.remove_calls(arg)
            elif opt == "--ignore-to":
                prof.set_ignore_to(arg.split(','))
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
            elif opt in ("-e", "--emph-limit"):
                graph.set_emph_limit(self.get_value(opt, arg, True))
            elif opt in ("-g", "--graph"):
                graph.enable_output()
            elif opt == "--ignore":
                graph.set_ignore(arg.split(','))
            elif opt == "--ignore-from":
                graph.set_ignore_from(arg.split(','))
            elif opt == "--only":
                graph.set_only(arg.split(','))
            elif opt == "--no-leafs":
                graph.disable_leafs()
            elif opt == "--no-intermediate":
                graph.disable_intermediate()
            # options specific to statistics
            elif opt in ("-i", "--info"):
                stats.enable_info()
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
            elif opt in ("-v", "--verbose"):
                prof.enable_verbose()
                stats.enable_verbose()
                graph.enable_verbose()
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
            graph.do_output(prof, arg)
            self.write("\nProfile information from '%s':\n" % arg)
            stats.do_output(prof)

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
