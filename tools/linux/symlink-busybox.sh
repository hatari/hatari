#!/bin/sh
#
# script to generate m68k busybox links based on host busybox,
# on assumption that they both provide same tools (are from
# same Debian version and built/configured identically)

bb_host=/bin/busybox

if [ ! -x $bb_host ]; then
	echo "ERROR: host BusyBox '$bb_host' missing, do:"
	echo "  sudo apt install busybox"
	exit 1
fi

if [ ! -x bin/busybox ]; then
	echo "ERROR: m68k BusyBox 'bin/busybox' missing!"
	exit 1
fi

# symlink Busybox tools
for tool in $($bb_host --list-full); do
	ln -sfv /bin/busybox "$tool"
done
