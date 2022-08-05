#!/bin/sh

if [ $# -lt 1 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
	echo "Usage: $0 <hatari>"
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

export HATARI_TEST=cycles
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy

cp "$basedir/cyccheck.prg" "$testdir"
HOME="$testdir" $hatari --log-level fatal --fast-forward on --sound off \
	--run-vbls 1000 --tos none "$@" "$testdir/cyccheck.prg" \
	> "$testdir/log.txt" 2>&1
exitstat=$?
if [ $exitstat -ne 0 ]; then
	echo "Test FAILED, Hatari returned error status ${exitstat}."
	cat "$testdir/log.txt"
	exit 1
fi

if ! diff -q "$basedir/test_out.txt" "$testdir/RESULTS.TXT"; then
	echo "Test FAILED, output differs:"
	diff -u "$basedir/test_out.txt" "$testdir/RESULTS.TXT"
	exit 1
fi

echo "Test PASSED."
exit 0
