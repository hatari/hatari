#!/bin/sh
#
# script to cleanup 'nm' output for Hatari debugger.
#
# 2013 (C) Eero Tamminen, licenced under GPL v2+

usage ()
{
	name=${0##*/}
	echo
	echo "Usage: $name <nm output file>"
	echo
	echo "Clean up from 'nm' symbol adddress output"
	echo "symbols that aren't useful with Hatari debugger"
	echo "e.g. because those (non-interesting) symbols"
	echo "are bound to multiple addresses:"
	echo "- GCC (2.x) internal symbols"
	echo "- Repeating local symbol names ('.L<number>')"
	echo "- Assignments / defines which aren't addresses"
	echo "- Weak symbols that are just aliases for other symbols"
	echo "Paths are also remove from object file names."
	echo
	echo "For example:"
	echo "  $name foobar.nm > foobar.sym"
	echo
	echo "ERROR: $1!"
	echo
	exit 1
}
if [ $# -ne 1 ]; then
	usage "incorrect number of arguments"
fi

if [ \! -f $1 ]; then
	usage "given '$1' nm output file not found"
fi

# clean symbol cruft & sort by hex address
cat $1 | grep -v -e ' [aAwW] ' -e ' t \.L' -e gcc2_compiled -e _gnu_compiled_c | sed 's%/.*/%%' | sort
