#!/bin/sh

# argument checks
if [ $# -lt 1 ] || [ \! -z "$(echo $1|tr -d 0-9)" ] || [ $1 -lt 2 ] || [ $1 -gt 16 ]; then
	echo "Usage: ${0##*/} <Hatari instances, 2-16> [extra Hatari args]"
	echo
	echo "Example of running 4 Hatari instances in a MIDI ring, each using midi/ as HD:"
	echo "    ${0##*/} 4 -d midi/"
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
