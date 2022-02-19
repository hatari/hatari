#!/bin/sh

if [ $# -lt 2 -o "$1" = "-h" -o "$1" = "--help" ]; then
	echo "Usage: $0 <hatari> <prg> ..."
	exit 1
fi

hatari=$1
shift
if [ ! -x "$hatari" ]; then
	echo "First parameter must point to valid hatari executable."
	exit 1
fi;

prg=$1
shift

basedir=$(dirname $0)
testdir=$(mktemp -d)

export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy
unset TERM

# export LD_PRELOAD=libefence.so

export HATARI_TEST=$(basename "$prg")
HOME="$testdir" $hatari --log-level fatal --sound off --bios-intercept on \
	--fast-forward on --run-vbls 500 --frameskips 0 --tos none \
	--screenshot-dir "$testdir" "$@" "$prg" > "$testdir/out.txt" 2>&1
exitstat=$?

if [ $exitstat -ne 0 ]; then
	echo "Running hatari FAILED. Status=${exitstat}. Hatari output:"
	cat "$testdir/out.txt"
	rm -rf "$testdir"
	exit 1
fi

if [ ! -e "$testdir"/grab0007.* ]; then
	echo "Test FAILED: Screenshot has not been taken."
	cat "$testdir/out.txt"
	rm -rf "$testdir"
	exit 1
fi

echo "Test PASSED."
rm -rf "$testdir"
exit 0
