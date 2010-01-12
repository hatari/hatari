#!/bin/sh
#
# script to convert AHCC "linker -p" output for Hatari debugger.

usage ()
{
	name=${0##*/}
	echo
	echo "usage: $name <map file>"
	echo
	echo "convert AHCC 'linker -p' symbol address map output to 'nm'"
	echo "format understood by the Hatari debugger 'symbols' command."
	echo
	echo "For example:"
	echo "  $name etos512.map > etos512.sym"
	echo
	echo "ERROR: $1!"
	echo
	exit 1
}
if [ $# -ne 1 ]; then
	usage "incorrect number of arguments"
fi

if [ \! -f $1 ]; then
	usage "given '$1' address map file not found"
fi

# output only lines that have both address & symbol name,
# remove "[ size]" stuff that confuses awk field parsing,
# and convert those with awk to the "nm" format:
#   <address> <type> <symbol name>
egrep ' (TEXT|DATA|BSS) ' $1 | egrep -v '(TEXT|DATA|BSS)[[:space:]]*$' |\
sed 's/\[[^]]*\]//' | awk '
/^ .* TEXT / { print $1, "T", $4 }
/^ .* DATA / { print $1, "D", $4 }
/^ .* BSS /  { print $1, "B", $4 }'
