#!/bin/sh

if [ $# -lt 1 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
	echo "Usage: $0 <hatari> <machine>"
	exit 1
fi

hatari=$1
if [ ! -x "$hatari" ]; then
	echo "First parameter must point to valid hatari executable."
	exit 1
fi

sertype=$2
machine=$3

basedir=$(dirname "$0")
testdir=$(mktemp -d)

touch "$testdir"/empty.txt

export HATARI_TEST=serial
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy
unset TERM

if [ "$sertype" = "mfp" ]; then
	serparams="--rs232-in $testdir/empty.txt --rs232-out $testdir/serial-out.txt"
	testprog=mfp_ser.tos
elif [ "$sertype" = "midi" ]; then
	serparams="--midi-in $testdir/empty.txt --midi-out $testdir/serial-out.txt"
	testprog=midi_ser.tos
elif [ "$sertype" = "scc" ]; then
	serparams="--scc-b-out $testdir/serial-out.txt"
	testprog=scc_ser.tos
else
	echo "Unsupported serial type: $sertype"
	exit 1
fi

# shellcheck disable=SC2086 # word splitting of $serparams is desired here
HOME="$testdir" $hatari --log-level fatal --sound off --tos none \
	--fast-forward on  --run-vbls 500 $serparams --machine "$machine" \
	"$basedir"/"$testprog" > "$testdir/out.txt"
exitstat=$?
if [ $exitstat -ne 0 ]; then
	echo "Test FAILED. Hatari exited with status ${exitstat}."
	cat "$testdir/out.txt"
	rm -rf "$testdir"
	exit 1
fi

if ! diff -q "$basedir/expected.txt" "$testdir/serial-out.txt"; then
	echo "Test FAILED, output differs:"
	diff -u "$basedir/expected.txt" "$testdir/serial-out.txt"
	rm -rf "$testdir"
	exit 1
fi

echo "Test PASSED."
rm -rf "$testdir"
exit 0
