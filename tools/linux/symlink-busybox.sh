#!/bin/sh
#
# script to generate m68k busybox links.
#
# Uses user-space Qemu to query from m68k busybox what
# tools it provides.

bb_path=usr/bin/busybox

qemu=$(which qemu-m68k)
if [ -z $qemu ]; then
	echo "ERROR: install 'qemu-m68k' first!"
	echo "  sudo apt install qemu-user"
	exit 1
fi

if [ ! -x $bb_path ]; then
	echo "ERROR: m68k BusyBox '$bb_path' missing!"
	exit 1
fi

# symlink Busybox tools
for tool in $($qemu $bb_path --list-full); do
	ln -sfv /$bb_path "$tool"
done
