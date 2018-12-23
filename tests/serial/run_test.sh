#!/bin/sh

if [ $# -lt 1 -o "$1" = "-h" -o "$1" = "--help" ]; then
	echo "Usage: $0 <hatari> <machine>"
	exit 1;
fi

hatari=$1
if [ ! -x "$hatari" ]; then
	echo "First parameter must point to valid hatari executable."
	exit 1;
fi

machine=$2

basedir=$(dirname $0)
testdir=$(mktemp -d)

touch "$testdir"/empty.txt

export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy
unset TERM

HOME="$testdir" $hatari --log-level fatal --sound off --tos none \
	--fast-forward on  --run-vbls 500 --rs232-in "$testdir"/empty.txt \
	--rs232-out "$testdir"/serial-out.txt --machine "$machine" \
	"$basedir"/mfp_ser.tos > "$testdir/out.txt"
exitstat=$?
if [ $exitstat -ne 0 ]; then
	echo "Running hatari failed. Status=${exitstat}."
	cat "$testdir/out.txt"
	rm -rf "$testdir"
	exit 1
fi

if ! diff -q "$basedir/expected.txt" "$testdir/serial-out.txt"; then
	echo "Test FAILED, output differs:"
	diff -u "$basedir/test_out.txt" "$testdir/serial-out.txt"
	rm -rf "$testdir"
	exit 1
fi

echo "Test PASSED."
rm -rf "$testdir"
exit 0
