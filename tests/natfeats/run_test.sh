#!/bin/sh

if [ $# -lt 1 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
	echo "Usage: $0 <hatari> ..."
	exit 1;
fi

hatari=$1
shift
if [ ! -x "$hatari" ]; then
	echo "First parameter must point to valid hatari executable."
	exit 1;
fi;

basedir=$(dirname "$0")
testdir=$(mktemp -d)

export HATARI_TEST=natfeats
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy
unset TERM

echo c | HOME="$testdir" $hatari --log-level fatal --sound off --natfeats on \
	-t none --fast-forward on --run-vbls 500 "$@" "$basedir/nf_ahcc.tos" \
	2>&1 | sed -e 's/^Hatari v.*/Hatari v/' -e 's/^CPU=.*$/CPU=.../' \
		   -e 's/^00.*/00.../' -e '/^> c$/d' -e 's/^> //' \
	> "$testdir/out.txt"
exitstat=$?
if [ $exitstat -ne 0 ]; then
	echo "Running hatari failed. Status=${exitstat}."
	cat "$testdir/out.txt"
	rm -rf "$testdir"
	exit 1
fi

if ! diff -q "$basedir/test_out.txt" "$testdir/out.txt"; then
	echo "Test FAILED, output differs:"
	diff -u "$basedir/test_out.txt" "$testdir/out.txt"
	rm -rf "$testdir"
	exit 1
fi

echo "Test PASSED."
rm -rf "$testdir"
exit 0
