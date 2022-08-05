#!/bin/sh

if [ $# -lt 1 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
	echo "Usage: $0 <hatari> <testprg> ..."
	exit 1;
fi

hatari=$1
shift
if [ ! -x "$hatari" ]; then
	echo "First parameter must point to valid hatari executable."
	exit 1;
fi;

testprg=$1
shift
if [ ! -f "$testprg" ]; then
	echo "Second parameter must point to valid test PRG."
	exit 1;
fi;

testdir=$(mktemp -d)

remove_temp() {
  rm -rf "$testdir"
}
trap remove_temp EXIT

export HATARI_TEST=cpu
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy

HOME="$testdir" $hatari --log-level error --sound off --fast-forward on \
	--tos none --run-vbls 500 "$@" "$testprg" > "$testdir/out.txt" 2>&1
exitstat=$?
if [ $exitstat -ne 0 ]; then
	echo "Running hatari failed. Status=${exitstat}."
	cat "$testdir/out.txt"
	exit 1
fi

# Now check for failure strings:

if grep -qi fail "$testdir/out.txt"; then
	echo "Test FAILED:"
	cat "$testdir/out.txt"
	exit 1
fi
if grep -qi error "$testdir/out.txt"; then
	echo "Test ERROR:"
	cat "$testdir/out.txt"
	exit 1
fi

echo "Test PASSED."
exit 0
