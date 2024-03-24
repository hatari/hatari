#!/bin/sh
#
# script to generate m68k busybox links based on host busybox,
# on assumption that they both provide same tools (are from
# same Debian version and built/configured identically)

bb_path=usr/bin/busybox

if [ ! -x /$bb_path ]; then
	echo "ERROR: host BusyBox '/$bb_path' missing, do:"
	echo "  sudo apt install busybox"
	exit 1
fi

if [ ! -x $bb_path ]; then
	echo "ERROR: m68k BusyBox '$bb_path' missing!"
	exit 1
fi

# symlink Busybox tools
for tool in $(/$bb_path --list-full); do
	ln -sfv /$bb_path "$tool"
done
