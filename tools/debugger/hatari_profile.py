#!/usr/bin/env python3
#
# Hatari profile data processor
#
# 2013-2023 (C) Eero Tamminen, licensed under GPL v2+
#
# TODO:
#
# Current way of handling symbol conflicts while they are being
# read, causes unnecessary symbol/address conflict resolving /
# renames, and as result outputs way too much noise about it.
#
# Instead, change that to work in multiple passes:
# 1. load all symbols regardless of conflicts
# 2. silently remove extra symbols for same address if they:
#    - are object/library file names, or
#    - look like weak symbols (e.g. extra _* prefix)
# 3. remove rest of overlapping same-address symbols with warnings
#    (need to try whether using shorter or longer name is better)
# 4. rename identical symbols pointing to different addresses
#
"""
A tool for post-processing Hatari emulator HW profiling data with
symbol information.

In Hatari debugger you get (CPU) profiling data with the following
commands (for Falcon DSP data, prefix commands with 'dsp'):
	profile on
        continue
        ...
	profile save <file name>

Profiling information for code addresses is summed together and
assigned to (function) symbols to which those addresses belong to.

All costs for addresses between two symbol names (in profile file) or
symbol addresses (read from symbols files) are assumed to belong to
the function/symbol which address precedes them.

Tool output will contain at least:
- (deduced) call counts,
- executed instruction counts, and
- spent processor cycles.

If profile data contains other information (e.g. cache misses),
that is also shown.

Provided symbol information should be in the same format as for
the Hatari debugger 'symbols' command.  Note that files containing
absolute addresses and ones containing relatives addresses need to
be given with different options!


Usage: hatari-profile [options] <profile files>

Options:
	-a <symbols>	absolute symbol address information file
        -r <symbols>	TEXT (code section) relative symbols file
        -s		output profile statistics
        -t		list top functions for all profile items
        -i		add address and time information to lists
        -f <count>	list at least first <count> items
        -l <limit>      list at least items which percentage >= limit
        -p		show costs propagated up in the call hierarchy
	-o <file name>	statistics output file name (default is stdout)
        -g              write <profile>.dot callgraph files
        -k		write <profile>.cg Callgrind format output
        		(for Kcachegrind GUI)
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
        --callgrind
        --verbose

(Timing --info is shown only for cycles list.)

For example:
	hatari-profile -a etos1024k.sym -st -f 10 prof1.txt prof2.txt

For each given profile file, output is:
- profile statistics
- a sorted list of functions, for each of the profile data items
  (calls, instructions, cycles...)


When both -l and -f options are specified, they're combined.  Produced
lists contain at least the number of items specified for the -f option,
and more, if there are additional items which percentage of the total
value is larger than the one given for the -l option.  In callgraphs
these options mainly affect which nodes are highlighted.


If profiling was done after loading symbol information for the profile code,
profile ddata includes "caller" information.  Then -p option can be used
to see:
- costs also for everything else that a subroutine calls, in addition to
- subroutine's "own" cost, which excludes costs for any further subroutine
  calls that it did

(For more information on how these differ from normally shown costs,
see Profiling section in Hatari manual.)

With the -g option, callgraph are generated in DOT format for each of
the profile data items, for each of the profile files, saved to
<filename>-<itemindex>.dot file (prof1-0.dot, prof1-2.dot etc).

Nodes with subroutine costs are shown as diamonds in the callgraphs.


Call information filtering options:
        --no-calls <[bersux]+>	remove calls of given types, default = 'rux'
	--ignore-to <list>	ignore calls to these symbols
	--compact		leave only single connection between symbols
        			which are connected through single node path

(Give --no-calls option other char than [bersux] to see type descriptions.)

<list> is a comma separate list of symbol names, like this:
	--ignore-to _int_timerc,_int_vbl

If (e.g. C++) symbol names contain commas, change list separator:
	--separator ';'

These options change which calls are reported for functions, and can
affect the shape & complexity of the graph a lot.  If you e.g. want
to see just nodes with costs specific to -p option, use "--no-calls
berux" option.

If default "--no-calls" type removal doesn't remove all interrupt
handling switches [1], give handler names to "--ignore-to" option.
In callgraphs, one can then investigate them separately using
"--no-calls '' --only <name>" options.

[1] CPU being interrupted by an interrupt handler gets recorded as
    "a call" to that handler by the profiler, and because such "calls"
    can happen at any time, then can mess graphs badly

NOTE: costs shown with "-p" option include costs of exception handling
code that interrupts the function calls.  Normally effect of this
should be minimal, but by providing symbol addresses for interrupt
handlers their costs will be visible in output.


Callgraph filtering options to remove nodes and edges from the graph:
	--no-intermediate	remove nodes with single parent & child
	--no-leafs		remove nodes which have either:
				- one parent and no children,
				- one child and no parents, or
				- no children nor parents
	--no-limited		remove _all_ nodes below -l limit
	--no-orphans		remove unconnected nodes
				(regardless of their cost)
	--only-subroutines	remove non-subroutine nodes below -l limit
	--ignore <list>		no nodes for these symbols [2]
	--ignore-from <list>	no arrows from these symbols [2]
        --only <list>		only these symbols and their callers [2]

[2] symbol matching uses shell patterns:
    https://docs.python.org/3/library/fnmatch.html

NOTE: leaf and intermediate node removal options remove only nodes which
own costs fall below limit given with the "--limit" option.  So remember
specify limit with them, as it defaults to 0.0 on callgraphs!

Functions which are called from everywhere (like malloc), may be good
candidates for '--ignore' option when one wants a more readable graph.
One can then investigate them separately with the '--only <function>'
option.


Callgraph visualization options:
	--full-symbols		  do not shorten (C++) symbols
	--mark <list>		  color nodes which names contain any
				  of the listed string(s) differently
        -e, --emph-limit <limit>  percentage limit for highlighted nodes

When -e limit is given, it is used for deciding which nodes to
highlight, not -f & -l options.  Normally it should be larger than -l
option value.

Nodes with costs that exceed the highlight limit have red outline.
If node's own cost exceeds the limit, it has also gray background.


To convert dot files e.g. to SVG and PDF, use:
	dot -Tsvg graph.dot > graph.svg
	dot -Tpdf graph.dot > graph.pdf

('dot' tool is in Graphviz package.)
"""
from copy import deepcopy
from bisect import bisect_right
from fnmatch import fnmatchcase
import getopt, os, re, sys

# PC address that was undefined during profiling,
# signifies normally first called symbol in data
PC_UNDEFINED = 0xffffff

# call type identifiers and their information
CALL_STARTUP = '0'	# first called symbol, special case
CALL_SUBROUTINE = 's'
CALL_SUBRETURN = 'r'
CALL_EXCEPTION = 'e'
CALL_EXCRETURN = 'x'
CALL_BRANCH = 'b'
CALL_NEXT = 'n'
CALL_UNKNOWN = 'u'
CALLTYPES = {
    CALL_SUBROUTINE: "subroutine call",		# is call
    CALL_SUBRETURN:  "subroutine return",	# shouldn't be call
    CALL_EXCEPTION:  "exception",		# is call
    CALL_EXCRETURN:  "exception return",	# shouldn't be call
    CALL_BRANCH:     "branch/jump",		# could be call
    CALL_NEXT:       "next instruction",	# most likely loop etc label
    CALL_UNKNOWN:    "unknown"			# shouldn't be call
}


# ---------------------------------------------------------------------
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

    def set_output_file_ext(self, fname, extension):
        "open output file name with extension replacement, on success return name"
        basename = os.path.splitext(fname)[0]
        fname = "%s%s" % (basename, extension)
        try:
            self.set_output(open(fname, "w"))
            return fname
        except IOError as err:
            self.warning(err)
        return None

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


# ---------------------------------------------------------------------
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
        # cost items summed from profile for given symbol
        # field 0 is ALWAYS calls count and field 1 instructions count!
        self.cost = [0] * items
        # if this object represents a function invoked with
        # a subroutine call, it has separately calculated
        # total and "own" costs
        self.subcost = None
        self.subtotal = None
        self.callflags = ()
        # calltree linkage
        self.parent = {}   # addr: Callinfo tuple
        self.child = {}    # addr: True

    def is_subroutine(self):
        for flag in (CALL_SUBROUTINE, CALL_STARTUP):
            if flag in self.callflags:
                return True
        return False

    def has_cost(self):
        "return whether function has valid data yet"
        # other values are valid only if there are instructions
        return self.cost[1] != 0

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
        cost = self.cost
        if not cost[1]:
            # function call count is same as instruction
            # count for its first instruction
            cost[0] = values[1]
        for i in range(1, len(cost)):
            cost[i] += values[i]

    def __repr__(self):
        "return printable current function instruction state"
        ret = "0x%x = %s:" % (self.addr, self.name)
        if self.subtotal:
            ret = "%s %s, total: %s (subroutine)" % (ret, self.subcost, self.subtotal)
        else:
            ret = "%s %s" % (ret, self.cost)
        if self.parent or self.child:
            return "%s, %d parents, %d children" % (ret, len(self.parent), len(self.child))
        return ret


# ---------------------------------------------------------------------
class InstructionStats:
    "statistics on all instructions"
    # not changeable, these are expectations about the data fields
    # in this, FunctionStats, ProfileCallers and ProfileGraph classes
    CALLS_FIELD  = 0
    INSTS_FIELD  = 1 # unused
    CYCLES_FIELD = 2

    def __init__(self, processor, clock, fields):
        "processor name, its speed and profile field names"
        # Calls item will be deducted from instruction values
        self.names = ["Visits/calls"] + fields
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
        value = function.cost[0]
        if value > self.max_val[0]:
            self.max_val[0] = value
            self.max_addr[0] = function.addr
            self.max_line[0] = function.line

    def get_time(self, cost):
        "return time (in seconds) spent by given cost item"
        return float(cost[self.CYCLES_FIELD])/self.clock

    def get_time_call(self, cost, callitem):
        "return time (in seconds) spent by given cost item per call"
        return float(cost[self.CYCLES_FIELD])/callitem[self.CALLS_FIELD]/self.clock

    def sum_values(self, functions):
        "calculate totals for given functions data"
        if functions:
            sums = [0] * self.items
            for fun in functions:
                for i in range(self.items):
                    sums[i] += fun.cost[i]
            self.totals = sums


# ---------------------------------------------------------------------
class ProfileSymbols(Output):
    "class for handling parsing and matching symbols, memory areas and their addresses"
    # profile file field name for program text/code address range
    text_area = "PROGRAM_TEXT"
    # default emulation memory address range name
    default_area = "RAM"

    def __init__(self):
        Output.__init__(self)
        self.renames = 0	# total of significant renames
        self.names = None
        self.symbols = None	# (addr:symbol) dict for resolving
        self.absolute = {}	# (addr:symbol) dict of absolute symbols
        self.relative = {}	# (addr:symbol) dict of relative symbols
        self.aliases = {}       # (name:addr) dict of replaced absolute symbols
        self.rel_aliases = {}   # (name:addr) dict of replaced relative symbols
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
        # [0x]<hex> [<type>] <symbol/objectfile name>
        # Note: C++ symbols contain almost any chars
        self.r_symbol = re.compile("^(0x)?([a-fA-F0-9]+) ([aAbBdDrRtTvVwW]) ([$]?[._a-zA-Z(][^$?@;]*)$")
        # weak C++ symbols can be data members, match them to ignore
        self.r_datasym = re.compile("^.*::[_a-zA-Z][_a-zA-Z0-9]*$")

    def parse_areas(self, fobj, parsed):
        "parse memory area lines from data and post-process earlier read symbols data"
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
        self._combine_symbols()
        self._clean_aliases()
        return line, parsed-1

    def get_area(self, addr):
        "return memory area name + offset (used if no symbol matches)"
        for key, value in self.areas.items():
            if value[1] and value[0] <= addr <= value[1]:
                return (key, addr - value[0])
        return (self.default_area, addr)

    def _check_symbol(self, addr, name, symbols, aliases):
        "return True if symbol is OK for addition"
        # same address has already a symbol?
        if addr in symbols:
            # with same name?
            if name == symbols[addr]:
                return False
            # silently drop less accurate file path symbols
            if '/' in name or name.endswith('.o') or name.endswith('.a'):
                aliases[name] = addr
                return False
            oldname = symbols[addr]
            # silently override less accurate file path symbols
            if '/' in oldname or oldname.endswith('.o') or oldname.endswith('.a'):
                aliases[oldname] = addr
                return True
            # either of the names is C/C++ symbol?
            if (name.startswith('_') or '::' in name or ' ' in name or
                oldname.startswith('_') or '::' in oldname or ' ' in oldname):
                # prefer shorter names for C/C++ symbols
                if len(name) > len(oldname):
                    aliases[name] = addr
                    retval = False
                else:
                    aliases[oldname] = addr
                    retval = True
            else:
                # prefer longer names for asm symbols
                if len(name) < len(oldname):
                    aliases[name] = addr
                    retval = False
                else:
                    aliases[oldname] = addr
                    retval = True
            # count and warn only of significant renames
            lendiff = abs(len(name) - len(oldname))
            minlen = min(len(name), len(oldname))
            if not (
               (lendiff < 3 and minlen > 3 and
                (name.endswith(oldname) or oldname.endswith(name) or
                 name.startswith(oldname) or oldname.startswith(name)))):
                if self.verbose:
                    if retval:
                        self.message("0x%x: prefer '%s' over '%s'" % (addr, name, oldname))
                    else:
                        self.message("0x%x: prefer '%s' over '%s'" % (addr, oldname, name))
                self.renames += 1
            return retval
        return True

    def _clean_aliases(self):
        "remove address aliases for names that already have symbols"
        to_remove = {}
        for name, addr in self.aliases.items():
            if name in self.names:
                if addr == self.names[name]:
                    to_remove[name] = True
                    continue
                if not self.verbose:
                    continue
                self.warning("multiple addresses (0x%x & 0x%x) for symbol '%s'" % (addr, self.names[name], name))
                if addr in self.symbols:
                    self.message("- 0x%x is also address for symbol '%s'" % (addr, self.symbols[addr]))
        for name in to_remove:
            del self.aliases[name]

    def parse_symbols(self, fobj, is_relative):
        "parse symbol file contents"
        if is_relative:
            symbols = self.relative
            aliases = self.rel_aliases
        else:
            symbols = self.absolute
            aliases = self.aliases
        old_aliases = len(self.aliases)
        old_renames = self.renames

        unknown = lines = cppdata = 0
        for line in fobj.readlines():
            lines += 1
            line = line.strip()
            if line.startswith('#'):
                continue
            match = self.r_symbol.match(line)
            if match:
                dummy, addr, kind, name = match.groups()
                if kind not in ('t', 'T', 'W'):
                    continue
                if (name.startswith("typeinfo ") or
                    name.startswith("vtable ") or
                    name.startswith("VTT ")):
                    # C++ meta data, could be also in text section
                    cppdata += 1
                    continue
                match = self.r_datasym.match(name)
                if match:
                    # C++ data members
                    cppdata += 1
                    continue
                addr = int(addr, 16)
                if self._check_symbol(addr, name, symbols, aliases):
                    symbols[addr] = name
            else:
                self.warning("unrecognized symbol line %d:\n\t'%s'" % (lines, line))
                unknown += 1

        info = (lines, len(symbols), unknown)
        self.message("%d lines with %d code symbols/addresses parsed, %d unknown." % info)
        info = (len(aliases) - old_aliases, self.renames - old_renames)
        self.message("%d (new) symbols were aliased, with %d significant renames." % info)
        if cppdata:
            self.message("%d C++ data member symbols ignored." % cppdata)
            

    def _rename_symbol(self, addr, name):
        "return symbol name, potentially renamed if there were conflicts"
        if name in self.names:
            if addr == self.names[name]:
                return name
            # symbol with same name already exists at another address
            chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
            maxid = len(chars)
            idx = 1
            while True:
                # One cannot just blindly add random suffix to a mangled C++ symbol,
                # so use <len><idx chars> for most mangled symbols, and
                # ".%d" suffix for ones already using vendor extension
                if name.startswith("__Z") and '.' not in name:
                    if idx >= maxid**2:
                        self.warning("giving up on '%s' rename, at least %d duplicates" % (name, maxid))
                        return name
                    newname = "%s2%c%c" % (name, chars[idx//maxid], chars[idx%maxid])
                else:
                    newname = "%s.%d" % (name, idx)
                if newname in self.names:
                    idx += 1
                    continue
                if self.verbose:
                    self.message("renaming '%s' at 0x%x as '%s' to avoid clash with same symbol at 0x%x" % (name, addr, newname, self.names[name]))
                name = newname
                break
        self.names[name] = addr
        return name

    def _combine_symbols(self):
        "combine absolute and relative symbols to single lookup"
        # renaming is done only at this point (after parsing memory areas)
        # to avoid addresses in names dict being messed by relative symbols
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
        # relocate symbols used for resolving
        for addr, name in self.relative.items():
            addr += area[0]
            # -1 used because compiler can add TEXT symbol right after end of TEXT section
            if addr < area[0] or addr-1 > area[1]:
                self.error_exit("relative symbol '%s' address 0x%x is outside of TEXT area: 0x%x-0x%x!\nDo symbols match the profiled binary?" % (name, addr, area[0], area[1]))
            if self._check_symbol(addr, name, self.symbols, self.rel_aliases):
                name = self._rename_symbol(addr, name)
                self.symbols[addr] = name
                if self.verbose:
                    self.message("0x%x: %s (relative)" % (addr, name))
        # relocate relative address aliases
        for name, addr in self.rel_aliases.items():
            addr += area[0]
            # -1 used because compiler can add TEXT symbol right after end of TEXT section
            if addr < area[0] or addr-1 > area[1]:
                self.error_exit("relative symbol '%s' address 0x%x is outside of TEXT area: 0x%x-0x%x!\nDo symbols match the profiled binary?" % (name, addr, area[0], area[1]))
            if self.verbose:
                if name in self.aliases and self.aliases[name] != addr:
                    self.message("conflicting addresses 0x%x & 0x%x for symbol '%s'" % (addr, self.aliases[name], name))
            self.aliases[name] = addr

    def add_profile_symbol(self, addr, name):
        "add absolute symbol and return its name in case it got renamed"
        if self._check_symbol(addr, name, self.symbols, self.aliases):
            name = self._rename_symbol(addr, name)
            self.symbols[addr] = name
            self.symbols_need_sort = True
        return name

    def get_symbol(self, addr):
        "return symbol name for given address, or None"
        if addr in self.symbols:
            return self.symbols[addr]
        return None

    def get_addr(self, name):
        "return symbol address for given name, or None"
        if name in self.names:
            return self.names[name]
        if name in self.areas:
            return self.areas[name][0]
        return None

    def get_alias_addr(self, name):
        "return symbol address for given address alias, or None (for Callgrind)"
        if name in self.aliases:
            return self.aliases[name]
        return None

    def get_preceeding_symbol(self, addr):
        "resolve non-function addresses to preceding function name+offset"
        # should be called only after profile addresses has started
        if self.symbols:
            if self.symbols_need_sort:
                self.symbols_sorted = list(self.symbols.keys())
                self.symbols_sorted.sort()
                self.symbols_need_sort = False
            idx = bisect_right(self.symbols_sorted, addr) - 1
            if idx >= 0:
                saddr = self.symbols_sorted[idx]
                return (self.symbols[saddr], addr - saddr)
        return self.get_area(addr)



# ---------------------------------------------------------------------
class Callinfo:
    "class representing parsed profile data caller/callee linkage information"

    def __init__(self, addr):
        self.addr = addr
        self.calls = 0
        self.flags = "-"
        self.total = [None, None]

    def set(self, calls, flags, intotal, extotal):
        "parse & set given string arguments"
        # how many times this caller called the destination
        self.calls = int(calls, 10)
        # what kind of "call" it was?
        if flags:
            self.flags = flags.strip()
        # cost of these calls including further function calls
        self._parse_totals(0, intotal)
        # costs exclusing further function calls
        self._parse_totals(1, extotal)

    def _parse_totals(self, idx, totalstr):
        "parse '/' separate totals string into integer sequence, or leave it None"
        if totalstr:
            self.total[idx] = [int(x, 10) for x in totalstr.strip().split('/')]

    def _add_total(self, idx, newtotal):
        "add given totals to own totals"
        mytotal = self.total[idx]
        if mytotal and newtotal:
            self.total[idx] = [x + y for x, y in zip(mytotal, newtotal)]
        elif newtotal:
            self.total[idx] = newtotal

    def add(self, newinfo):
        "add counts and totals from other item to this one"
        self.calls += newinfo.calls
        for i in range(len(self.total)):
            self._add_total(i, newinfo.total[i])

    def get_full_costs(self):
        return self.total[0]

    def get_own_costs(self):
        return self.total[1]


# ---------------------------------------------------------------------
class ProfileCallers(Output):
    "profile data callee/caller information parser & handler"

    def __init__(self):
        Output.__init__(self)
        # caller info in callee line:
        # 0x<hex> = <count> <typeletter>[ inclusive/totals][ exclusive/totals]
        self.r_caller = re.compile("^0x([0-9a-f]+) = ([0-9]+)( [a-z]+)?( [0-9/]+)?( [0-9/]+)?$")
        # whether there is any caller info
        self.present = False
        # address dicts
        self.callinfo = None	# callee:caller dict of callee:callinfo dicts
        # temporary during completion
        self.symbols = None
        # parsing options
        self.removable_calltypes = "rux"
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
        for letter in types:
            if letter not in CALLTYPES:
                self.message("Valid call types are:")
                for item in CALLTYPES.items():
                    self.message("  %c -- %s" % item)
                self.error_exit("invalid call type")
        self.removable_calltypes = types

    def parse_callers(self, fobj, parsed, line):
        "parse callee: caller call count information"
        #0x<hex>: 0x<hex> = <count>[ <flags>][, <inclusive/totals>[, <exclusive/totals>]]; N*[0x<hex> = <count>...;][ <symbol>]
        self.message("parsing call info...")
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
            caddr, callers[0] = callers[0].split(':')
            last = callers[-1].strip()
            if last.startswith('0x'):
                self.error_exit("last item isn't empty or symbol name on callee line %d\n\t'%s'" % (parsed, line))

            callinfo = {}
            for callstr in callers[:-1]:
                callstr = callstr.strip()
                match = self.r_caller.match(callstr)
                if match:
                    items = match.groups()
                    paddr = int(items[0], 16)
                    # caller/parent specific callinfo for this child/callee
                    tmp = Callinfo(paddr)
                    tmp.set(*items[1:])
                    callinfo[paddr] = tmp
                else:
                    self.error_exit("unrecognized caller info '%s' on callee line %d\n\t'%s'" % (callstr, parsed, line))
            self.callinfo[int(caddr, 16)] = callinfo
            line = fobj.readline()
        return line, parsed-1

    def _complete_child(self, profile, linkage, child, caddr):
        "link parents with child based on caller info"
        flags = {}
        ignored = switches = 0
        callinfo = Callinfo(caddr)
        for laddr, info in linkage.items():
            callinfo.add(info)
            pname, offset = self.symbols.get_preceeding_symbol(laddr)
            if len(info.flags) > 1:
                self.warning("caller instruction change ('%s') detected for '%s', did its code change during profiling?" % (info.flags, pname))
            elif info.flags in self.removable_calltypes:
                ignored += info.calls
                continue
            if laddr == PC_UNDEFINED:
                flags[CALL_STARTUP] = True
                switches += 1
                continue
            flags[info.flags] = True
            # function address for the caller
            paddr = laddr - offset
            if paddr not in profile:
                # this can be either a mismatch in symbols, or few first instructions without symbol
                self.warning("parent caller 0x%x for '%s' not in profile!\nDo symbols match the profiled binary?" % (laddr, child.name))
                continue
            parent = profile[paddr]
            if pname != parent.name:
                if not parent.name.endswith('.o'):
                    self.warning("overriding parsed function 0x%x name '%s' with resolved caller 0x%x name '%s'" % (parent.addr, parent.name, paddr, pname))
                parent.name = pname
            # link parent and child function together
            if paddr in child.parent:
                if self.compact:
                    child.parent[paddr][0].add(info)
                else:
                    child.parent[paddr] += (info,)
            else:
                child.parent[paddr] = (info,)
            parent.child[caddr] = True
            switches += info.calls
        if child.subcost:
            self.warning("cost already for child: %s" % child)
        child.subcost = callinfo.get_own_costs()
        child.subtotal = callinfo.get_full_costs()
        child.callflags = tuple(flags.keys())
        return switches, ignored

    def complete(self, profile, symbols):
        "resolve caller functions and add child/parent info to profile data"
        if not self.callinfo:
            self.present = False
            return
        self.present = True
        self.symbols = symbols
        all_switches = all_ignored = 0
        # go through called (child) functions...
        for caddr, callinfo in self.callinfo.items():
            child = profile[caddr]
            if child.name in self.ignore_to:
                continue
            # ...and their callers (parents)
            switches, ignored = self._complete_child(profile, callinfo, child, caddr)
            all_switches += switches
            all_ignored += ignored
            # validate non-subroutine call counts
            if ignored:
                if self.verbose:
                    self.message("Ignoring %d switches to %s" % (ignored, child.name))
                #switches += ignored
                child.cost[0] -= ignored
            calls = child.cost[0]
            if calls != switches:
                info = (child.name, caddr, calls, switches, ignored)
                self.warning("call count mismatch for '%s' at 0x%x, %d != %d (%d ignored)" % info)
        self.callinfo = None
        self.symbols = None
        if all_ignored:
            all_switches += all_ignored
            info = (all_switches, all_ignored, list(self.removable_calltypes))
            self.message("Of all %d switches, ignored %d for type(s) %s." % info)


# ---------------------------------------------------------------------
class ProfileCallgrind(Output):
    "profile output in callgrind format"
    # Output in Valgrind callgrind format:
    #       http://valgrind.org/docs/manual/cl-format.html
    # for KCachegrind:
    #       http://kcachegrind.sourceforge.net/

    def __init__(self):
        Output.__init__(self)
        self.function = None
        self.profname = None
        self.tmpname = None
        self.missing = None

    def start_output(self, fname, emuinfo, names):
        "create callgraph file and write header to it"
        self.profname = fname
        self.tmpname = self.set_output_file_ext(fname, ".cgtmp")
        if not self.tmpname:
            self.error_exit("Temporary callgrind file '%s' creation failed" % self.tmpname)
        if emuinfo:
            self.write("creator: %s\n" % emuinfo)
        idx = 0
        shortnames = []
        for name in names:
            abr = "%s_%d" % (name.split()[-1], idx)
            self.write("event: %s %s\n" % (abr, name))
            shortnames.append(abr)
            idx += 1
        self.write("events: %s\n" % ' '.join(shortnames))
        self.write("fl=%s\n" % fname)

    def output_line(self, function, counts, linenro):
        "output given function line information"
        # this needs to be done while data is parsed because
        # line specific data isn't stored after parsing
        if function != self.function:
            self.function = function
            # first line in function, output function info
            self.write("fn=%s\n" % function.name)
            # only first function line gets the call count
            counts = [function.cost[0]] + counts[1:]
        self.write("%d %s\n" % (linenro, ' '.join([str(x) for x in counts])))

    def _output_calls(self, profile, paddr):
        # TODO: find the line in profile corresponding
        # to the caller and use that as line number,
        # instead of caller function beginning line number
        parent = profile[paddr]
        for caddr in parent.child:
            child = profile[caddr]
            if not child.subtotal:
                # costs missing, no sense in adding dep
                self.missing[caddr] = True
                continue
            total = 0
            for item in child.parent[paddr]:
                total += item.calls
            self.write("cfn=%s\n" % child.name)
            self.write("calls=%d %d\n" % (total, child.line))
            all_calls = child.cost[0]
            cost = [int(round(float(x)*total/all_calls)) for x in child.subtotal]
            counts = ' '.join([str(x) for x in cost])
            self.write("%d %s\n" % (parent.line, counts))
            if all_calls < total:
                self.warning("calls from %s to %s: %d > %d" % (parent.name, child.name, total, all_calls))

    def output_callinfo(self, profile, symbols):
        "output function calling information"
        # this can be done only after profile is parsed because
        # before that we don't have function inclusive cost counts,
        # i.e. we need to post-process the generated callgrind file
        # to add the calls and their costs
        fname = self.set_output_file_ext(self.tmpname, ".cg")
        self.message("\nGenerating callgrind file '%s'..." % fname)
        tmpfile = open(self.tmpname, 'r')
        os.remove(self.tmpname)

        items = 0
        self.missing = {}
        for line in tmpfile.readlines():
            self.write(line)
            if not line.startswith("fn="):
                continue
            name = line.strip().split('=')[1]
            paddr = symbols.get_addr(name)
            if not paddr:
                self.warning("no resolved address for symbol '%s', trying address aliases instead" % name)
                paddr = symbols.get_alias_addr(name)
            if profile[paddr].child:
                self._output_calls(profile, paddr)
            items += 1
        if self.missing:
            self.warning("%d / %d nodes missing caller information (for callgrind data)!" % (len(self.missing), items))
            self.missing = None


# ---------------------------------------------------------------------
class EmulatorProfile(Output):
    "Emulator profile data file parsing and profile information"

    def __init__(self):
        Output.__init__(self)
        self.symbols = ProfileSymbols()
        self.callers = ProfileCallers()

        # profile data format
        #
        # emulator ID line:
        # <Emulator> <processor name> profile [(info)]
        #
        self.emuinfo = None
        self.r_info = re.compile(r"^.* profile \((.*)\)$")
        # processor clock speed
        self.r_clock = re.compile(r"^Cycles/second:\t([0-9]+)$")
        # field names
        self.r_fields = re.compile(r"^Field names:\t(.*)$")
        # processor disassembly format regexp is gotten from profile file
        self.r_regexp = re.compile(r"Field regexp:\t(.*)$")
        self.r_address = None
        # memory address information is parsed by ProfileSymbols
        #
        # this class parses symbols from disassembly itself:
        # <symbol/objectfile name>: (in disassembly)
        # Note: C++ symbols contain almost any chars
        self.r_function = re.compile(r"^([._a-zA-Z(][^$?@;]*):$")

        self.stats = None		# InstructionStats instance
        self.callgrind = None		# ProfileCallgrind instance
        self.profile = None		# hash of profile (addr:data)
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

    def enable_callgrind(self):
        self.callgrind = ProfileCallgrind()

    def parse_symbols(self, fobj, is_relative):
        "parse symbols from given file object"
        self.symbols.parse_symbols(fobj, is_relative)

    def output_info(self, fname):
        "show profile file information"
        if self.emuinfo:
            info = "- %s\n" % self.emuinfo
        else:
            info = ""
        self.write("\n%s profile information from '%s':\n%s" % (self.stats.processor, fname, info))

    def _get_profile_type(self, fobj):
        "get profile processor type and speed information or exit if it's unknown"
        line = fobj.readline()
        field = line.split()
        fields = len(field)
        if fields < 3 or field[2] != "profile":
            self.error_exit("unrecognized file, line 1\n\t%s\nnot in format:\n\t<emulator> <processor> profile [(info)]" % line)
        processor = field[1]
        if fields > 3:
            match = self.r_info.match(line)
            if not match:
                self.error_exit("invalid (optional) emulator information format on line 1:\n\t%s" % line)
            self.emuinfo = match.group(1)
        else:
            self.emuinfo = None

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
        if function.has_cost():
            if not function.addr:
                name, offset = self.symbols.get_preceeding_symbol(function.addr)
                function.rename(name, offset)
            # addresses must increase in profile
            oldaddr = function.addr
            assert oldaddr not in self.profile
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
            # if changed area, preceding symbol can be before area start,
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
        self.message("parsing disassembly...")
        prev_addr = 0
        discontinued = False
        function = FunctionStats(None, -1, 0, self.stats.items)
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
                if self.callgrind:
                    self.callgrind.output_line(function, counts, self.linenro)
                discontinued = False
            # next line
            line = fobj.readline()
        # finish
        self._change_function(function, None, 0)
        return line

    def parse_profile(self, fobj, fname):
        "parse profile data"
        self.profile = {}
        # header
        self.linenro = self._get_profile_type(fobj)
        if self.callgrind:
            self.callgrind.start_output(fname, self.emuinfo, self.stats.names)
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
        if self.callgrind:
            self.callgrind.output_callinfo(self.profile, self.symbols)


# ---------------------------------------------------------------------
class ProfileSorter:
    "profile information sorting and list output class"

    def __init__(self, profile, stats, write, subcosts):
        self.profile = profile
        self.stats = stats
        self.write = write
        self.field = None
        self.show_subcosts = subcosts

    def _cmp_field(self, i):
        "return currently selected field in profile data"
        return self.profile[i].cost[self.field]

    def get_combined_limit(self, field, count, limit):
        "return percentage for given profile field that satisfies both count & limit constraint"
        if not count:
            return limit
        keys = list(self.profile.keys())
        if len(keys) <= count:
            return 0.0
        self.field = field
        keys.sort(key=self._cmp_field, reverse=True)
        total = self.stats.totals[field]
        function = self.profile[keys[count]]
        if self.show_subcosts and function.subtotal:
            value = function.subtotal[field]
        else:
            value = function.cost[field]
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
            value = function.cost[field]
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

            mark = " "
            times = ""
            values = ""
            percentages = ""
            if self.show_subcosts:
                # show also subroutine call cost
                if field > 0:
                    totals = (function.cost, function.subcost, function.subtotal)
                    if function.subcost and (
                      len(function.subcost) > field and (
                      function.cost[field] > function.subcost[field])):
                        mark = "*"
                else:
                    # subfunction and symbol call counts are same
                    totals = (function.cost, function.subtotal,)
            else:
                totals = (function.cost,)

            showtime = False
            if show_info and field == stats.CYCLES_FIELD:
                showtime = True

            for cost in totals:
                if cost and len(cost) > field:
                    percentage = 100.0 * cost[field] / total
                    percentages += "%7.2f%%" % percentage
                    if showtime:
                        time = stats.get_time(cost)
                        times += "%9.5fs" % time
                    values += "%10d" % cost[field]
                else:
                    percentages += " " * 8
                    values += " " * 10
                    if showtime:
                        times += " " * 10
            self.write("%s %s %s %s" % (percentages, times, values, mark))

            if show_info:
                costinfo = ""
                for cost in totals:
                    if cost and len(cost) > field and function.cost[0]:
                        if showtime:
                            time = stats.get_time_call(cost, function.cost)
                            costinfo += ",%8.5fs" % time
                        elif field:
                            costinfo += ", %d" % (cost[field] / function.cost[0])
                if costinfo:
                    costinfo += " / call"
                addrinfo = "0x%06x" % addr
                self.write(" %-23s (%s%s)\n" % (function.name, addrinfo, costinfo))
            else:
                self.write(" %s\n" % function.name)

    def do_list(self, field, count, limit, show_info):
        "sort and show list for given profile data field"
        if self.stats.totals[field] == 0:
            return
        self.field = field
        keys = list(self.profile.keys())
        keys.sort(key=self._cmp_field, reverse=True)
        self._output_list(keys, count, limit, show_info)


# ---------------------------------------------------------------------
class ProfileOutput(Output):
    "base class for profile output options"

    def __init__(self):
        Output.__init__(self)
        # both unset so that subclasses can set defaults reasonable for them
        self.show_subcosts = False
        self.limit = 0.0
        self.count = 0

    def enable_subcosts(self):
        "enable showing subroutine call costs instead of just own costs"
        self.show_subcosts = True

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


# ---------------------------------------------------------------------
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
        self.write("\nTime spent in profile = %.5fs.\n" % time)

        self.write("\nProfile lines with highest costs + totals:\n")
        symbols = profobj.symbols
        items = len(stats.totals)
        for i in range(items):
            if not stats.totals[i]:
                continue
            addr = stats.max_addr[i]
            name, offset = symbols.get_preceeding_symbol(addr)
            if name:
                if offset:
                    name = " in %s%+d" % (name, offset)
                else:
                    name = " in %s" % name
            self.write("* %s:\n" % stats.names[i])
            info = (stats.max_val[i], name, addr, stats.max_line[i])
            self.write("  - max = %d,%s at 0x%x, on line %d\n" % info)
            self.write("  - %d in total\n" % stats.totals[i])

    def do_output(self, profobj):
        "output enabled lists"
        if self.show_totals:
            self.output_totals(profobj)
        if self.show_top:
            sorter = ProfileSorter(profobj.profile, profobj.stats, self.write, self.show_subcosts)
            fields = range(profobj.stats.items)
            for field in fields:
                sorter.do_list(field, self.count, self.limit, self.show_info)


# ---------------------------------------------------------------------
def name_fnmatch(matches, name):
    "return True if 'name' fnmatch()es one of 'matches', False otherwise"
    if name in matches:
        return True
    for pattern in matches:
        if fnmatchcase(name, pattern):
            return True
    return False

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
        # callgraph node filtering
        self.compact = False
        self.remove_intermediate = False
        self.remove_leafs = False
        self.remove_limited = False
        self.remove_nonsubs = False
        self.remove_orphans = False
        self.full_symbols = False
        self.only = []
        self.mark = []
        self.ignore = []
        self.ignore_from = []
        self.emph_limit = 0
        # for demangled (C++ etc) symbols
        self.re_thunk = re.compile(r"(non-)?virtual ") # ...thunk to
        self.re_clone = re.compile(r" \[clone[^]]+\]") # ...isra
        self.re_args = re.compile(r"[^(+]+::[^(+]+(\(.+\))") # class::method(args)

    def _get_short_name(self, name):
        # not C++ or other demangled symbol
        if self.full_symbols or not (" " in name or "::" in name):
            return name

        # remove args from method signatures
        m = self.re_args.search(name)
        if m:
            name = name[:m.start(1)] + "()" + name[m.end(1):]

        # shorten namespaces and template args
        name = name.replace("anonymous namespace", "anon ns")
        name = name.replace("unsigned ", "u")

        # remove virtual / clone infos from signatures
        for r in (self.re_thunk, self.re_clone):
            m = r.search(name)
            if m:
                name = name[:m.start()] + name[m.end():]

        return name

    def enable_full_symbols(self):
        "do not shorten (C++) symbols"
        self.full_symbols = True

    def enable_output(self):
        "enable output"
        self.output_enabled = True

    def enable_compact(self):
        "show just single edge between nodes"
        self.compact = True

    def disable_intermediate(self):
        "disable showing nodes with just single parent & child, with single edge"
        # TODO: move leaf/intermediate handling to ProfileCallers class?
        self.remove_intermediate = True

    def disable_leafs(self):
        "disable showing nodes which don't have children"
        self.remove_leafs = True

    def disable_limited(self):
        "disable showing all nodes which are below limit"
        self.remove_limited = True

    def disable_orphans(self):
        "disable showing above-limit nodes which have no connections"
        self.remove_orphans = True

    def only_subroutines(self):
        "disable showing nodes that aren't above limit or subroutines"
        self.remove_nonsubs = True

    def set_only(self, lst):
        "set list of only symbols to include"
        self.only = lst

    def set_marked(self, lst):
        "set list of substrings for symbols to mark in graphs"
        self.mark = lst

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
            self.message("removing node %s" % function)
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

    def _set_reduced_profile(self, profobj, totals, field):
        "get relinked copy of profile data with requested items removed from it"
        # need our own copy so that it can be manipulated freely
        self.profile = deepcopy(profobj.profile)
        total = totals[field]
        to_remove = {}
        if self.ignore:
            for addr, function in self.profile.items():
                if name_fnmatch(self.ignore, function.name):
                    to_remove[addr] = True
        removed = 0
        while True:
            for addr in to_remove:
                self._remove_from_profile(addr)
            removed += len(to_remove)
            to_remove = {}
            for addr, function in self.profile.items():
                # remove everything except subroutines?
                if self.remove_nonsubs and not function.is_subroutine():
                    to_remove[addr] = True
                    continue

                parents = len(function.parent)
                children = len(function.child)
                # remove orphans regardless of their cost?
                if self.remove_orphans and (parents + children) == 0:
                    to_remove[addr] = True
                    continue

                # don't remove symbols which own costs are over the limit
                if self.show_subcosts and function.subcost and len(function.subcost) > field:
                    count = function.subcost[field]
                else:
                    count = function.cost[field]
                percentage = 100.0 * count / total
                if percentage >= self.limit:
                    continue
                if self.remove_limited:
                    to_remove[addr] = True
                    continue

                # remove leafs & intermediates
                if self.remove_leafs:
                    if (parents + children) <= 1:
                        to_remove[addr] = True
                        continue
                    # refers just to itself?
                    if children == 1 and addr == tuple(function.child.keys())[0]:
                        to_remove[addr] = True
                        continue
                    if parents == 1 and addr == tuple(function.parent.keys())[0]:
                        to_remove[addr] = True
                        continue
                if self.remove_intermediate:
                    if parents == 1 and children == 1:
                        to_remove[addr] = True
                        continue
                    # refers also to itself?
                    if children == 2:
                        for caddr in function.child:
                            if caddr == addr:
                                to_remove[addr] = True
                                break
                    if parents == 2:
                        for paddr in function.parent:
                            if paddr == addr:
                                to_remove[addr] = True
                                break
            if not to_remove:
                break
        return removed

    def _filter_profile(self):
        "filter remaining profile content to nodes and edges members based on only & ignore-from options"
        profile = self.profile
        self.nodes = {}
        self.edges = {}
        for caddr, child in profile.items():
            if not child.parent:
                self.nodes[caddr] = True
                continue
            for paddr, info in child.parent.items():
                parent = profile[paddr]
                # no recursion
                # if caddr == paddr:
                #    continue
                if self.only:
                    if not (name_fnmatch(self.only, child.name) or
                            name_fnmatch(self.only, parent.name)):
                        continue
                # child end for edges
                self.nodes[caddr] = True
                if name_fnmatch(self.ignore_from, parent.name):
                    continue
                # parent end for edges
                self.nodes[paddr] = True
                # total calls count for child
                all_calls = profile[caddr].cost[0]
                if self.compact:
                    edge = Callinfo(paddr)
                    # TODO: "pick first" not good enough?
                    edge.flags = [x.flags for x in info][0]
                    edge.calls = sum([x.calls for x in info])
                    self.edges[(paddr, caddr)] = (paddr, all_calls, edge)
                else:
                    # calls to child done from different locations in parent
                    for edge in info:
                        self.edges[(edge.addr, caddr)] = (paddr, all_calls, edge)
        return len(profile) - len(self.nodes)

    def _output_nodes(self, stats, field, limit):
        "output graph nodes from filtered nodes dict"
        self.highlight = {}
        total = stats.totals[field]
        for addr in self.nodes:
            shape = style = ""
            function = self.profile[addr]
            calls = function.cost[0]
            if self.show_subcosts and function.subtotal and len(function.subtotal) > field:
                shape = " shape=diamond"
                cost = function.subtotal
                if len(function.subcost) > field:
                    owncount = function.subcost[field]
                else:
                    owncount = 0
            else:
                cost = function.cost
                owncount = cost[field]
            count = cost[field]
            percentage = 100.0 * count / total
            if percentage >= limit:
                self.highlight[addr] = True
                style = " color=red style=bold"
            ownpercentage = 100.0 * owncount / total
            if ownpercentage >= limit:
                style = "%s style=filled fillcolor=lightgray" % style
            for substr in self.mark:
                if substr in function.name:
                    style = "%s style=filled fillcolor=green shape=square" % style
                    break
            if CALL_STARTUP in function.callflags:
                style = "%s style=filled fillcolor=green shape=square" % style
            if count != owncount:
                coststr = "%.2f%%\\n(own: %.2f%%)" % (percentage, ownpercentage)
            else:
                coststr = "%.2f%%" % percentage

            name = self._get_short_name(function.name)
            if field == 0:
                self.write("N_%X [label=\"%s\\n%s\\n%d calls\"%s%s];\n" % (addr, coststr, name, count, style, shape))
            elif field == stats.CYCLES_FIELD:
                time = stats.get_time(cost)
                self.write("N_%X [label=\"%s\\n%.5fs\\n%s\\n(%d calls)\"%s%s];\n" % (addr, coststr, time, name, calls, style, shape))
            else:
                self.write("N_%X [label=\"%s\\n%d\\n%s\\n(%d calls)\"%s%s];\n" % (addr, coststr, count, name, calls, style, shape))

    def _output_edges(self):
        "output graph edges from filtered edges dict, after nodes is called"
        for linkage, info in self.edges.items():
            laddr, caddr = linkage
            paddr, calls, edge = info
            offset = laddr - paddr
            style = ""
            if caddr in self.highlight:
                style = " color=red style=bold"

            # arrowhead/tail styles:
            #   none, normal, inv, dot, odot, invdot, invodot, tee, empty,
            #   invempty, open, halfopen, diamond, odiamond, box, obox, crow
            flags = edge.flags
            if flags in (CALL_NEXT, CALL_BRANCH):
                style += " arrowhead=dot"
            elif flags == CALL_SUBROUTINE:
                pass	# use default arrow
            elif flags == CALL_SUBRETURN:
                style += " arrowhead=inv"
            elif flags == CALL_EXCEPTION:
                style += " style=dashed"
            elif flags == CALL_EXCRETURN:
                style += " arrowhead=inv style=dashed"
            elif flags == CALL_UNKNOWN:
                style += " arrowhead=diamond style=dotted"

            pname = self._get_short_name(self.profile[paddr].name)
            if offset:
                label = "%s%+d\\n($%x)" % (pname, offset, laddr)
            else:
                label = pname
            if edge.calls != calls:
                percentage = 100.0 * edge.calls / calls
                label = "%s\\n%d calls\\n=%.2f%%" % (label, edge.calls, percentage)
            self.write("N_%X -> N_%X [label=\"%s\"%s];\n" % (paddr, caddr, label, style))

    def do_output(self, profobj, fname):
        "output graphs for given profile data"
        if not (self.output_enabled and profobj.callers.present):
            if not profobj.callers.present:
                self.warning("callgraph output enabled, but caller info is missing!")
            return
        stats = profobj.stats
        for field in range(profobj.stats.items):
            if not stats.totals[field]:
                continue
            # get potentially reduced instance copy of profile data
            removed = self._set_reduced_profile(profobj, stats.totals, field)
            filtered = self._filter_profile()
            if not self.nodes:
                continue
            dotname = self.set_output_file_ext(fname, "-%d.dot" % field)
            if not dotname:
                continue
            self.message("\nGenerating '%s' DOT callgraph file..." % dotname)
            if self.emph_limit:
                limit = self.emph_limit
            else:
                # otherwise combine both "-f" and "-l" limits
                # limits are taken from full profile, not potentially reduced one
                sorter = ProfileSorter(profobj.profile, stats, None, False)
                limit = sorter.get_combined_limit(field, self.count, self.limit)
            name = stats.names[field]
            title = "%s\\nfor %s" % (name, fname)
            if profobj.emuinfo:
                title += "\\n(%s)" % profobj.emuinfo
            title += "\\n\\nown cost emphasis (gray bg) & total cost emphasis (red) limit = %.2f%%\\n" % limit
            if self.show_subcosts:
                title += "nodes which are subroutines and have accurate total costs, have diamond shape\\n"
            if removed:
                if self.remove_limited:
                    title += "%d nodes below %.2f%% were removed\\n" % (removed, self.limit)
                else:
                    title += "%d leaf and/or intermediate nodes below %.2f%% were removed\\n" % (removed, self.limit)
            if filtered:
                title += "%d nodes were filtered out\\n" % filtered
            self.write(self.header % title)
            self._output_nodes(stats, field, limit)
            self._output_edges()
            self.write(self.footer)


# ---------------------------------------------------------------------
class Main(Output):
    "program main loop & args parsing"
    longopts = [
        "absolute=",
        "callgrind",
        "compact",
        "emph-limit=",
        "first",
        "full-symbols",
        "graph",
        "ignore=",
        "ignore-to=",
        "ignore-from=",
        "info",
        "limit=",
        "mark=",
        "no-calls=",
        "no-intermediate",
        "no-leafs",
        "no-limited",
        "no-orphans",
        "only=",
        "only-subroutines",
        "output=",
        "propagate",
        "relative=",
        "separator=",
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
            opts, rest = getopt.getopt(self.args, "a:e:f:gikl:o:pr:stv", self.longopts)
        except getopt.GetoptError as err:
            self.usage(err)

        prof = EmulatorProfile()
        graph = ProfileGraph()
        stats = ProfileStats()

        splitter = ','
        for opt, arg in opts:
            #self.message("%s: %s" % (opt, arg))
            # options for profile symbol parsing
            if opt == "--separator":
                splitter = arg
            elif opt in ("-a", "--absolute"):
                self.message("\nParsing absolute symbol address information from %s..." % arg)
                prof.parse_symbols(self.open_file(arg, "r"), False)
            elif opt in ("-r", "--relative"):
                self.message("\nParsing TEXT relative symbol address information from %s..." % arg)
                prof.parse_symbols(self.open_file(arg, "r"), True)
            # options for profile caller information parsing
            elif opt == "--no-calls":
                prof.remove_calls(arg)
            elif opt == "--ignore-to":
                prof.set_ignore_to(arg.split(splitter))
            # options for profile Callgraph info generation
            elif opt in ("-k", "--callgrind"):
                prof.enable_callgrind()
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
                graph.enable_subcosts()
                stats.enable_subcosts()
            elif opt == "--compact":
                prof.enable_compact()
                graph.enable_compact()
            # options specific to graphs
            elif opt in ("-e", "--emph-limit"):
                graph.set_emph_limit(self.get_value(opt, arg, True))
            elif opt == "--full-symbols":
                graph.enable_full_symbols()
            elif opt in ("-g", "--graph"):
                graph.enable_output()
            elif opt == "--ignore":
                graph.set_ignore(arg.split(splitter))
            elif opt == "--ignore-from":
                graph.set_ignore_from(arg.split(splitter))
            elif opt == "--only":
                graph.set_only(arg.split(splitter))
            elif opt == "--mark":
                graph.set_marked(arg.split(splitter))
            elif opt == "--no-intermediate":
                graph.disable_intermediate()
            elif opt == "--no-leafs":
                graph.disable_leafs()
            elif opt == "--no-limited":
                graph.disable_limited()
            elif opt == "--no-orphans":
                graph.disable_orphans()
            elif opt == "--only-subroutines":
                graph.only_subroutines()
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
            prof.parse_profile(self.open_file(arg, "r"), arg)
            graph.do_output(prof, arg)
            prof.output_info(arg)
            stats.do_output(prof)

    def open_file(self, path, mode):
        "open given path in given mode & return file object"
        try:
            return open(path, mode)
        except IOError as err:
            return self.usage("opening given '%s' file in mode '%s' failed:\n\t%s" % (path, mode, err))

    def get_value(self, opt, arg, tofloat):
        "return numeric value for given string"
        try:
            if tofloat:
                return float(arg)
            return int(arg)
        except ValueError:
            return self.usage("invalid '%s' numeric value: '%s'" % (opt, arg))

    def usage(self, msg):
        "show program usage + error message"
        self.message(__doc__)
        self.error_exit(msg)


# ---------------------------------------------------------------------
if __name__ == "__main__":
    Main(sys.argv).parse_args()
