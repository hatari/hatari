#!/bin/sh
# Script for converting .ZIP archives to Atari .ST disk images
# (for the Hatari emulator).

# tools are present?
if [ -z "$(which unzip)" ]; then
	echo "ERROR: 'unzip' missing."
	exit 2
fi
if [ -z "$(which mformat)" ] || [ -z "$(which mcopy)" ]; then
	echo "ERROR: 'mformat' or 'mcopy' (from 'mtools' package) missing."
	exit 2
fi

usage ()
{
	name=${0##*/}
	echo "Convert a .zip archive file to a .st disk image."
	echo
	echo "Single intermediate directories in the zip"
	echo "file are skipped."
	echo
	echo "Usage:"
	echo " $name srcname.zip [destname.st]"
	echo
	echo "Example:"
	echo " for zip in *.zip; do $name \$zip; done"
	echo
	if [ \! -z "$1" ]; then
		echo "ERROR: $1!"
	fi
	exit 1
}

# one ZIPFILE given?
if [ $# -lt 1 ] || [ -z "$1" ] || [ $# -gt 2 ]; then
	usage "wrong number of argument(s)"
fi

ZIPFILE=$1
STFILE=$2

if [ \! -f "$ZIPFILE" ]; then
	usage "given zipfile $ZIPFILE is missing"
fi

if [ -z "$STFILE" ]; then
	# if no STFILE path given, save it to current dir (remove path)
	# and use the ZIPFILE name with the extension removed.
	# (done with POSIX shell parameter expansion)
	BASENAME=${ZIPFILE##*/}
	BASENAME=${BASENAME%.zip}
	BASENAME=${BASENAME%.ZIP}
	STFILE=$BASENAME.st
fi
if [ -f "$STFILE" ]; then
	echo "ERROR: ST file '$STFILE' already exists, remove it first. Aborting..."
	exit 1
fi

step=0
TEMPDIR=`mktemp -d` || exit 2
echo "Converting" $ZIPFILE "->" $TEMPDIR "->" $STFILE

# script exit/error handling
exit_cleanup ()
{
	if [ $? -eq 0 ]; then
		echo "$step) Cleaning up temporary files..."
	else
		echo
		echo "ERROR, cleaning up..."
	fi
	echo "rm -rv $TEMPDIR"
	rm -rv $TEMPDIR
	echo "Done."
}
trap exit_cleanup EXIT

echo
step=$(($step+1))
echo "$step) Unzipping..."
echo "unzip $ZIPFILE -d $TEMPDIR"
unzip "$ZIPFILE" -d "$TEMPDIR" || exit 2

# .zip files created with STZip sometimes have wrong access rights...
echo "chmod -R u+rw $TEMPDIR/*"
chmod -R u+rw $TEMPDIR/*

echo
step=$(($step+1))
echo "$step) Checking/skipping intermediate directories..."
ZIPDIR=$TEMPDIR
while true; do
	count=$(ls $ZIPDIR|wc -l)
	if [ $count -ne 1 ]; then
		if [ $count -eq 0 ]; then
			echo "ERROR: zip content is empty!"
			exit 1
		fi
		# more than one dir/file
		break
	fi
	dir=$(ls $ZIPDIR)
	if [ \! -d "$ZIPDIR/$dir" ]; then
		# not dir
		break
	fi
	if [ -z "$(echo $dir|grep -v -i '^auto$')" ]; then
		# don't skip AUTO dir
		break
	fi
	echo "- $dir"
	ZIPDIR=$ZIPDIR/$dir
done

# size of reserved sectors, FATs & root dir + zip content size
size=$((24 + $(du -ks $ZIPDIR|awk '{print $1}')))

# find a suitable disk size supported by mformat and Atari ST
disksize=0
for i in 360 400 720 800 1440 2880; do
	if [ $i -gt $size ]; then
		disksize=$i
		break
	fi
done

if [ $disksize -gt 0 ]; then
	echo
	step=$(($step+1))
	echo "$step) Creating $disksize KB disk image..."
	echo "dd if=/dev/zero of=$STFILE bs=1024 count=$disksize"
	dd if=/dev/zero of="$STFILE" bs=1024 count=$disksize
	
	echo
	step=$(($step+1))
	echo "$step) Formating disk image..."
	case $disksize in
		360) format="-t 80 -h 1 -n 9" ;;
		400) format="-t 80 -h 1 -n 10" ;;
		800) format="-t 80 -h 2 -n 10" ;;
		*)   format="-f $disksize" ;;
	esac
	echo "mformat -a $format -i $STFILE ::"
	mformat -a $format -i "$STFILE" ::
	
	echo
	step=$(($step+1))
	echo "$step) Copying data to disk image..."
	echo "MTOOLS_NO_VFAT=1 mcopy -i $STFILE -spmv $ZIPDIR/* ::"
	MTOOLS_NO_VFAT=1 mcopy -i "$STFILE" -spmv $ZIPDIR/* ::
else
	echo "ERROR: zip contents don't fit to a floppy image ($size > 2880 KB)."
fi

echo
step=$(($step+1))
# do cleanup in exit handler
