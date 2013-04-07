#!/bin/sh
#
# script to convert Devpac v3 symbol table for Hatari debugger.
#
# 2013 (C) Eero Tamminen, licenced under GPL v2+

usage ()
{
	name=${0##*/}
	echo
	echo "Usage: $name <symbol file>"
	echo
	echo "Convert Devpac v3 symbol table listing (enabled"
	echo "in Listings options) to format understood by"
	echo "the Hatari debugger 'symbols' command."
	echo
	echo "For example:"
	echo "  $name FOOBAR.SYM > foobar.syms"
	echo
	echo "ERROR: $1!"
	echo
	exit 1
}
if [ $# -ne 1 ]; then
	usage "incorrect number of arguments"
fi

if [ \! -f $1 ]; then
	usage "given '$1' symbol file not found"
fi

# parse symbol information at the end of the file
awk '
/^[0-9A-F]+  [BDT]  R  / { print $1, $2, $4; next }
/^[0-9A-F]+  F[DEF].R  / {
	if ($2 == "FD.R") type = "D";
	else if ($2 == "FE.R") type = "B";
	else if ($2 == "FF.R") type = "T";
	print $1, type, $3;
	next
}
' $1 | sort

# parse code listing
# - has problem that in Devpac output the offsets in
#   the code listing part don't take includes into
#   account i.e. they seem wrong
#   
# works by:
# - removing columns that confuse rest command line
# - removing macro etc symbols at zero address
# - removing (optional) ':' from symbol end
# - matching addresses & symbol names and print
#   them in 'nm' order
#
#cut -b 7,9-17,39- $1 | grep -v 'T 00000000 ' | tr -d : |\
#  awk '/^[TBD] [0-9A-F]+ [._a-zA-Z0-9]/ {print $2,$1,$3}'
