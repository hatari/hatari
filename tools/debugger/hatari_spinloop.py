#!/usr/bin/env python
#
# Copyright (C) 2013 by Eero Tamminen
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
"""
Usage: hatari_spinloop.py <filename>

Script for post-processing Hatari profiler looping information
produced by "profile loops <filename>" command, after profiling is
enabled with either "profile on" and/or "dspprofile on".

That Hatari command saves spinloop data for any CPU and DSP code inner
loop that spins for more than once.

This script gives counts on how many times loops were executed, how
many times they spinned at minimum and maximum, at which VBL those
happened, and what was the standard deviation of that.

Note: in some cases Hatari can list different sized spinloops for the
same loop (start) address. This can be because code changed, or outer
loop was sometimes repeated several times in succession without
repeating the inner loop.
"""

import os, sys, math


class LoopItem:
    def __init__(self, addr, size):
        self.addr = addr
        self.size = size
        self.loops = []

    def add(self, count, vbl):
        self.loops.append((count, vbl))

    def stats(self):
        "return max + its VBL, min + its VBL, count of separate loops, stddev for them, loop addr & size"
        self.loops.sort()
        count = len(self.loops)
        if count > 1:
            mean = sum([x for x,y in self.loops]) / float(count)
            mean2 = sum([(x-mean)**2 for x,y in self.loops]) / float(count-1)
        else:
            mean2 = 0.0
        return (self.loops[-1][0], self.loops[-1][1],
                self.loops[0][0], self.loops[0][1],
                count, math.sqrt(mean2), self.addr, self.size)


def output(processors, write):
    for name, data in processors.items():
        write("\n%s loop statistics\n" % name)
        sorted = []
        for item in data.values():
            sorted.append(item.stats())
        sorted.sort()
        sorted.reverse()
        write("   max:\tat VBL:\t   min:\tat VBL:\ttimes:\tstddev:\taddr:\tsize:\n")
        for item in sorted:
            write("%7d\t%7d\t" % (item[0], item[1]))
            write("%7d\t%7d\t" % (item[2], item[3]))
            write("%7d\t%7.1f\t" % (item[4], item[5]))
            write(" %06x\t%6d\n" % (item[6], item[7]))


def parse(fname):
    "parse looping information from given file object"
    processors = {}
    for line in open(fname).readlines():
        if line[0] == '#':
            continue
        name, vbl, addr, size, count = line.split()
        if name not in processors:
            processors[name] = {}
        items = processors[name]
        addr = int(addr, 16)
        size = int(size)
        ident = (addr, size)
        if ident not in items:
            items[ident] = LoopItem(addr, size)
        items[ident].add(int(count), int(vbl))
    return processors


if __name__ == "__main__":
    if len(sys.argv) != 2 or not os.path.exists(sys.argv[1]):
        print __doc__
        sys.exit(1)
    data = parse(sys.argv[1])
    output(data, sys.stdout.write)
