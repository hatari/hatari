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

remove_temp() {
  rm -rf "$testdir"
}
trap remove_temp EXIT

export HATARI_TEST=xbios
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy

HOME="$testdir" $hatari --log-level fatal --sound off --cpuclock 32 --tos none \
	--run-vbls 500 --bios-intercept on "$@" "$basedir/xbiostst.prg" \
	2> "$testdir/out.txt" << EOF
c
c
EOF
exitstat=$?
if [ $exitstat -ne 0 ]; then
	echo "Running hatari failed. Status=${exitstat}."
	cat "$testdir/out.txt"
	exit 1
fi

# Now check for expected strings:

if ! grep -q "%101010.*#42.*2a" "$testdir/out.txt"; then
	echo "Test FAILED, missing '#42':"
	cat "$testdir/out.txt"
	exit 1
fi

if ! grep -q "This is a Dbmsg test for a string with fixed size." \
   "$testdir/out.txt"; then
	echo "Test FAILED, missing Dbmsg string with fixed size:"
	cat "$testdir/out.txt"
	exit 1
fi

if ! grep -q "This is a Dbmsg test for a NUL-terminated string." \
   "$testdir/out.txt"; then
	echo "Test FAILED, missing NUL-terminated Dbmsg string:"
	cat "$testdir/out.txt"
	exit 1
fi

if ! grep -q "0x1234.*0xdeadc0de" "$testdir/out.txt"; then
	echo "Test FAILED, missing Dbmsg code 0x1234:"
	cat "$testdir/out.txt"
	exit 1
fi

echo "Test PASSED."
exit 0
