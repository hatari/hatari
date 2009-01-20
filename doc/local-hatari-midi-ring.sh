#!/bin/sh

# argument checks
if [ $# -lt 1 ] || [ $1 -lt 2 ] || [ $1 -gt 16 ]; then
	echo "$# $1"
	echo "usage: ${0##*/} <MIDI ring Hataris, 2-16> [extra Hatari args]"
	exit 1
fi
count=$1
shift
args=$*

# open fifos
for i in $(seq $count); do
	mkfifo midi$i
done

# hatari command line
hatari="hatari $args"

# run MIDI ring Hatari instances
catpids=""
for i in $(seq $(($count-1))); do
	next=$(($i+1))
	echo $hatari --midi-in midi$i --midi-out midi$next &
	$hatari --midi-in midi$i --midi-out midi$next &
	# Without this Hataris would deadlock as fifos
	# block the process until both ends are opened.
	# Hatari opens midi output first (for writing),
	# so this needs to read it.
	cat midi$next >/dev/null &
	catpids="$catpids $!"
done
# and join the beginning and end of the MIDI ring
echo $hatari --midi-in midi$count --midi-out midi1 &
$hatari --midi-in midi$count --midi-out midi1 &
cat midi1 >/dev/null &
catpids="$catpids $!"

# after connections are open, fifo files can be removed
sleep 2
for i in $(seq $count); do
	rm midi$i
done
kill -9 $catpids
