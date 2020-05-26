#!/bin/sh

if [ $# -lt 3 -o "$1" = "-h" -o "$1" = "--help" ]; then
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

basedir=$(dirname $0)
testdir=$(mktemp -d)
cmdfifo="$testdir/cmdfifo"

export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy
unset TERM

HOME="$testdir" $hatari --log-level fatal --sound off --fast-forward on -z 1 \
	--max-width 416 --confirm-quit false --tos none --statusbar off \
	--cmd-fifo "$cmdfifo" --screenshot-dir "$testdir" $* "$prg" \
	> "$testdir/out.txt" 2>&1 &

# Wait until the fifo has been created by Hatari
while ! test -p "$cmdfifo" ; do
	sleep 0.1
done

# Wait until the program is ready, i.e. it is writing to 0xFFFF820A
while ! grep -q -i FFFF820A "$testdir/out.txt" ; do
	echo "hatari-debug r" > "$cmdfifo"
	sleep 0.1
done

echo "hatari-shortcut screenshot" > "$cmdfifo"

while ! grep -q "Screen dump saved" "$testdir/out.txt" ; do
	sleep 0.1
done

echo "hatari-shortcut quit" > "$cmdfifo"

ref_signature=$($identify -format "%#" "$refpng")
tst_signature=$($identify -format "%#" "$testdir"/grab0001.*)

wait $hatari_pid
exitstat=$?
if [ $exitstat -ne 0 ]; then
	echo "Running hatari FAILED. Status=${exitstat}. Hatari output:"
	cat "$testdir/out.txt"
	rm -rf "$testdir"
	exit 1
fi

if [ "$ref_signature" != "$tst_signature" ]; then
	echo "Test FAILED, screenshot differs! Hatari output:"
	cat "$testdir/out.txt"
	rm -rf "$testdir"
	exit 1
fi

echo "Test PASSED."
rm -rf "$testdir"
exit 0
