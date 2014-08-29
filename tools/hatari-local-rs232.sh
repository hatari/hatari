#!/bin/sh
#
# Start two Hatari instances connected through emulated rs232 connection.
# Given arguments (if any) are passed to the invoked Hatari instances.
# Their rs232 connection goes through shared local fifo file.
#
# To run development version of Hatari, use something like:
#	PATH=../../build/src:$PATH hatari-local-rs232.sh <options>

# open fifos
for i in 1 2; do
	mkfifo rs232-$i
	if [ $? -ne 0 ]; then
		echo "ERROR: creating FIFO 'midi$i' for RS232 communication failed!"
		exit 1
	fi
done

# show full path
hatari=$(which hatari)

# pass all args to the Hatari instances
args=$*

# connect the Hatari instances with fifos
echo "$hatari --rs232-in rs232-1 --rs232-out rs232-2 $args &"
hatari --rs232-in rs232-1 --rs232-out rs232-2 $args &
echo "$hatari --rs232-in rs232-2 --rs232-out rs232-1 $args &"
hatari --rs232-in rs232-2 --rs232-out rs232-1 $args &

# Without this Hataris would deadlock as fifos
# block the process until both ends are opened.
# Hatari opens rs232 output first (for writing),
# so this needs to read it.
catpids=""
cat rs232-2 >/dev/null &
catpids="$catpids $!"
cat rs232-1 >/dev/null &
catpids="$catpids $!"

# after connections are open, fifo files can be removed
sleep 2
for i in 1 2; do
	rm rs232-$i
done
# as can the 'cat' processes
kill -9 $catpids
