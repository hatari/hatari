#!/bin/sh
#
# minimal Linux init script for registering Hatari as binfmt_misc
# handler for TOS binaries

# name given for binftm_misc
name=TOS

# first bytes for GEMDOS programs
magic="\x60\x1a"

# what is used to "interpret" (run) these programs
runner=/usr/bin/hatari

register_binfmt ()
{
	# Mount the binfmt_misc directory if not already mounted
	if [ ! -e /proc/sys/fs/binfmt_misc/register ]; then
		(set -e; mount none /proc/sys/fs/binfmt_misc -t binfmt_misc)
	fi
	if [ -e /proc/sys/fs/binfmt_misc/$name ] ; then
		echo "WARNING: $name binfmt_misc handler already registered."
		return
	fi
	# Register runner for files starting with specified magic
	regfile=/proc/sys/fs/binfmt_misc/register
	if ! echo ":$name:M::$magic::$runner:" > $regfile ; then
		echo "ERROR: registering $name binfmt_misc handler failed!"
		exit 1
	fi
	echo "Registered $name binfmt_misc handler."
}

deregister_binfmt ()
{
	if [ -e /proc/sys/fs/binfmt_misc/$name ] ; then
		echo -1 > /proc/sys/fs/binfmt_misc/$name
		echo "De-registered $name binfmt_misc handler."
	else
		echo "WARNING: no binfmt_misc handler for $name."
	fi
}

case "$1" in
	start)
		register_binfmt
		;;
	stop)
		deregister_binfmt
		;;
	*)
		echo "Usage: ${0##*/} start|stop" >&2
		exit 1
		;;
esac
