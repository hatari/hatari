#!/bin/sh
#
# Start two Hatari instances connected through emulated rs232 connection.
# Given arguments (if any) are passed to the invoked Hatari instances.
# Their rs232 connection goes through shared local fifo file.

# open fifos
for i in 1 2; do
	mkfifo rs232-$i
done

# pass all args to the Hatari instances
hatari="hatari $*"

# connect the Hatari instances with fifos
echo "$hatari --rs232-in rs232-1 --rs232-out rs232-2 &"
$hatari --rs232-in rs232-1 --rs232-out rs232-2 &
echo "$hatari --rs232-in rs232-2 --rs232-out rs232-1 &"
$hatari --rs232-in rs232-2 --rs232-out rs232-1 &

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
