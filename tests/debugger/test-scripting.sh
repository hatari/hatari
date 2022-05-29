#!/bin/sh
#
# script to test Hatari debugger and console script features

if [ $# -ne 1 ]; then
	echo "usage: ${0##*/} <EmuTOS 512k binary>"
	exit 1
fi

etos=$1
if [ ! -f "$etos" ]; then
	echo "ERROR: given EmuTOS image file '$etos' doesn't exist!"
	exit 1
fi

hpath=../../build/src
if [ ! -d $hpath ]; then
	echo "ERROR: Hatari source directory '$hpath' missing!"
	exit 1
fi

hatari=$hpath/hatari
if [ ! -x $hatari ]; then
	echo "ERROR: Hatari binary '$hatari' missing!"
	exit 1
fi

console=../../tools/hconsole/hconsole.py
if [ ! -x $console ]; then
	echo "ERROR: Hatari console script '$console' missing!"
	exit 1
fi

# Enable extra GCC/LLVM AddressSanitizer options in case Hatari's compiled with it:
#   https://github.com/google/sanitizers/wiki/AddressSanitizerFlags
export ASAN_OPTIONS="detect_stack_use_after_return=1,abort_on_error=1,debug=1"

echo "TESTING: debugger input file"
echo "============================"
cmd="$hatari --sound off --machine falcon --tos $etos --dsp emu --parse data/debugger.ini"
echo "$cmd"
$cmd

echo
echo "TESTING: console input file"
echo "==========================="
PATH=$hpath:$PATH $console data/console.ini --exit --
