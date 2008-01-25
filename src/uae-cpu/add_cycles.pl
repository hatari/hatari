#!/usr/bin/perl -w
#
# Simple script used to parse cpuemu.c and to generate
# a new cpuemu.c where each opcode sets CurrentInstrCycles
# to the value that would be returned at the end of this
# opcode's processing.
#
# This allows to know how many cycles an instructions will take
# while handling its memory accesses (this is needed for
# accurate border removal emulation).
#
# This script is not perfect (doesn't give correct cycles
# values for variable cycles intr. like rol, lsl, movem, ...)
# but it is enough for border removal as instruction are
# often move or clr with fixed cycles count.
#
# 2007/03/07	[NP]	OK



$in_func = 0;

while ( <> )
  {
    $line = $_;

    # not in a function, we print back to stdout
    if ( $in_func == 0 )
      {
        print $line;
        $in_func = 1 if $line =~ /^{/;
      }

    # we're entering in a function ; we buffer everything
    # until we don't reach the 'return' line, then we print
    # the CurrentInstrCycles based in the numeric value in
    # the 'return' line, then we print the buffered lines.
    else
      {
	push @buf , $line;		# create a buffer for the function's body

	if ( $line =~ /return \(?(\d+)/ )
	  {
	    print "CurrentInstrCycles = $1;\n";
	    print @buf;
	    @buf = ();
	    $in_func = 0;
	  }
      }
  }

