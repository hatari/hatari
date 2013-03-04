#!/bin/sh
#
# script to convert DSP LOD file table for Hatari debugger.
#
# 2013 (C) Eero Tamminen, licenced under GPL v2+

usage ()
{
	name=${0##*/}
	echo
	echo "Usage: $name <symbol file>"
	echo
	echo "Convert P-memspace symbols in DSP LOD file"
	echo "(output by CLDLOD.TTP) to format understood"
	echo "by the Hatari debugger 'dspsymbols' command."
	echo
	echo "For example:"
	echo "  $name FOOBAR.LOD > foobar.sym"
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

# remove CR & parse symbol information at the end of the file
tr -d '\r' < $1 | awk '
BEGIN { show = 0 }
/^_SYMBOL P/ { show = 1; next }
/^_SYMBOL / { show = 0; next }
/^[_0-9a-zA-Z]+ * I [0-9A-F]+/ {
	if (show) {
		printf("%s T %s\n", $3, $1);
	}
}'
