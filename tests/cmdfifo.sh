#!/bin/sh

if [ $# -lt 1 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
	echo "Usage: $0 <hatari> ..."
	exit 1
fi

hatari=$1
shift
if [ ! -x "$hatari" ]; then
	echo "First parameter must point to valid hatari executable."
	exit 1
fi;

testdir=$(mktemp -d)
cmdfifo="$testdir/cmdfifo"

remove_temp() {
	rm -rf "$testdir"
}
trap remove_temp EXIT

mkdir -p "$testdir/.config/hatari"

cat > "$testdir/hatari.cfg" <<EOF
[Sound]
szYMCaptureFileName=$testdir/sndrec.wav

[Video]
AviRecordFile=$testdir/videorec.avi
EOF

export HATARI_TEST=cmdfifo
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy
unset TERM

HOME="$testdir" $hatari --confirm-quit false --screenshot-dir "$testdir" \
	-c "$testdir/hatari.cfg" --tos none --cmd-fifo "$cmdfifo" "$@" \
	> "$testdir/out.txt" 2>&1 &
hatari_pid=$!

# Wait until the fifo has been created by Hatari
while ! test -p "$cmdfifo" ; do
	sleep 0.1
done

# Send some commands, check output later...

echo "hatari-debug r" > "$cmdfifo"

echo "hatari-path memsave $testdir/testmem.sav" > "$cmdfifo"

echo "hatari-shortcut savemem" > "$cmdfifo"

echo "hatari-shortcut screenshot" > "$cmdfifo"

echo "hatari-shortcut recsound" > "$cmdfifo"

echo "hatari-shortcut recanim" > "$cmdfifo"

echo "hatari-option --spec512 256" > "$cmdfifo"

echo "hatari-event doubleclick" > "$cmdfifo"

echo "hatari-toggle printer" > "$cmdfifo"

echo "hatari-embed-info" > "$cmdfifo"

echo "hatari-shortcut quit" > "$cmdfifo"

wait $hatari_pid
exitstat=$?

echo "--------------- Hatari output: -------------------"
cat "$testdir/out.txt"
echo "--------------------------------------------------"

if [ $exitstat -ne 0 ]; then
	echo "ERROR: Running hatari FAILED. Status=${exitstat}"
	exit 1
fi

if [ -e "$cmdfifo" ]; then
	echo "ERROR: FIFO removal FAILED"
	exit 1
fi

if ! grep -q -i "D4.*00000000.*D5.*00000000" "$testdir/out.txt"; then
	echo "ERROR: Register dump FAILED"
	exit 1
fi

if [ ! -e "$testdir/testmem.sav" ]; then
	echo "ERROR: Memory snapshot FAILED"
	exit 1
fi

# shellcheck disable=SC2144 # there's only one match
if [ ! -e "$testdir"/grab0001.* ]; then
	echo "ERROR: Screenshot FAILED"
	exit 1
fi

if [ ! -e "$testdir/sndrec.wav" ]; then
	echo "ERROR: Sound recording FAILED"
	exit 1
fi

if [ ! -e "$testdir/videorec.avi" ]; then
	echo "ERROR: Video recording FAILED"
	exit 1
fi

echo "Test PASSED."
exit 0
