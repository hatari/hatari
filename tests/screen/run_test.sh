#!/bin/sh

if [ $# -lt 3 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
	echo "Usage: $0 <hatari> <prg> <ref.png> ..."
	exit 1
fi

if command -v gm >/dev/null 2>&1; then
	identify="gm identify"
elif command -v identify >/dev/null 2>&1; then
	identify=identify
else
	echo "Need either 'gm' (GraphicsMagick) or 'identify' (ImageMagick)"
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

refpng=$1
shift

testdir=$(mktemp -d)

export HATARI_TEST=screen
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy
unset TERM

HOME="$testdir" $hatari --log-level fatal --sound off -z 1 --max-width 416 \
	--bios-intercept on --statusbar off --drive-led off --fast-forward on \
	--run-vbls 500 --frameskips 0 --tos none --screenshot-dir "$testdir" \
	"$@" "$prg" > "$testdir/out.txt" 2>&1
exitstat=$?

if [ $exitstat -ne 0 ]; then
	echo "Running hatari FAILED. Status=${exitstat}. Hatari output:"
	cat "$testdir/out.txt"
	rm -rf "$testdir"
	exit 1
fi

# shellcheck disable=SC2144 # there's only one match
if [ ! -e "$testdir"/grab0001.* ]; then
	echo "Test FAILED: Screenshot has not been taken."
	cat "$testdir/out.txt"
	rm -rf "$testdir"
	exit 1
fi

ref_signature=$($identify -format "%#" "$refpng")
tst_signature=$($identify -format "%#" "$testdir"/grab0001.*)

if [ "$ref_signature" != "$tst_signature" ]; then
	echo "Test FAILED, screenshot differs! Hatari output:"
	cat "$testdir/out.txt"
	rm -rf "$testdir"
	exit 1
fi

echo "Test PASSED."
rm -rf "$testdir"
exit 0
