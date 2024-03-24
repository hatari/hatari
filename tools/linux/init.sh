#!/bin/sh
#
# NOTE: this expects kernel to be configured with:
#	CONFIG_DEVTMPFS_MOUNT=y
# so that /dev is automounted after root fs is mounted.
#
# Otherwise following is needed too:
#	mount -t devtmpfs udev /dev

# mount special file systems
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t tmpfs tmpfs /run
mount -t tmpfs tmpfs /tmp

mkdir /dev/pts
mount -t devpts devpts /dev/pts

if [ -x /usr/bin/busybox ]; then
	echo "Boot took $(cut -d' ' -f1 /proc/uptime) seconds"
	# hack for running shell so that job control is enabled, see:
	# https://git.busybox.net/busybox/plain/shell/cttyhack.c
	#
	# without -c (controlling terminal) option,
	# hangs *if* 030 caches are enabled
	setsid -c cttyhack sh
else
	# minimal klibc tools
	echo "uptime & idle seconds:"
	cat /proc/uptime
	exec sh
fi
