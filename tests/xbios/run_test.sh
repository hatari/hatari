#!/bin/sh

if [ $# -lt 1 -o "$1" = "-h" -o "$1" = "--help" ]; then
	echo "Usage: $0 <hatari> ..."
	exit 1;
fi

hatari=$1
shift
if [ ! -x "$hatari" ]; then
	echo "First parameter must point to valid hatari executable."
	exit 1;
fi;

basedir=$(dirname $0)
testdir=$(mktemp -d)

export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy

HOME="$testdir" $hatari --log-level fatal --sound off --fast-forward on \
	--tos none -d "$testdir" --bios-intercept $* "$basedir/xbiostst.prg" \
	2> "$testdir/out.txt" << EOF
c
c
EOF
exitstat=$?
if [ $exitstat -ne 0 ]; then
	echo "Running hatari failed. Status=${exitstat}."
	cat "$testdir/out.txt"
	rm -rf "$testdir"
	exit 1
fi

# Replace strings that might not be constant:
sed -e 's/^CPU=.*/CPU=.../' -e 's/^\$0.*/\$.../' \
    -e 's/Dbmsg: 0xF0\(.*\),\(.*\)/Dbmsg:0xF0\1/' \
    "$testdir/out.txt" > "$testdir/filtered.txt"

if ! diff -q "$basedir/test_out.txt" "$testdir/filtered.txt"; then
	echo "Test FAILED, output differs:"
	diff -u "$basedir/test_out.txt" "$testdir/filtered.txt"
	rm -rf "$testdir"
	exit 1
fi

echo "Test PASSED."
rm -rf "$testdir"
exit 0
