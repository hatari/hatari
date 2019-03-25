#!/bin/sh
#
# script to generate m68k busybox links based on host busybox,
# on assumption that they both provide same tools (are from
# same Debian version and built/configured identically)

bb_host=/bin/busybox

if [ \! -x $bb_host ]; then
	echo "ERROR: host BusyBox '$bb_host' missing, do:"
	echo "  sudo apt install busybox"
	exit 1
fi

if [ \! -x bin/busybox ]; then
	echo "ERROR: m68k BusyBox 'bin/busybox' missing!"
	exit 1
fi

# tools included in Busybox
tools=$($bb_host | tr ',' '\n' | awk '
/functions:/ {
	ok=1;
	next;
}
{ if(ok) {
	print $1;
}}')

# symlink them under bin/ directory in current dir
for i in $tools; do
	ln -sfv busybox bin/$i
done
