#!/bin/sh
#
# script to test Hatari debugger and console script features

if [ $# -ne 1 ]; then
	echo "usage: ${0##*/} <EmuTOS 512k binary>"
	exit 1
fi

etos=$1
if [ \! -f $etos ]; then
	echo "ERROR: given EmuTOS image file '$etos' doesn't exist!"
	exit 1
fi

hpath=../src
if [ \! -d $hpath ]; then
	echo "ERROR: Hatari source directory '$path' missing!"
	exit 1
fi

hatari=$hpath/hatari
if [ \! -x $hatari ]; then
	echo "ERROR: Hatari binary '$hatari' missing!"
	exit 1
fi

console=../python-ui/hatari-console.py
if [ \! -x $console ]; then
	echo "ERROR: Hatari console script '$console' missing!"
	exit 1
fi

echo "TESTING: debugger input file"
echo "============================"
$hatari --machine falcon --tos $etos --dsp emu --parse debugui/debugger.ini

echo
echo "TESTING: console input file"
echo "==========================="
PATH=$hpath:$PATH $console debugui/console.ini --exit --
