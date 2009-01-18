#!/bin/sh
# Script for converting .ZIP archives to Atari .ST disk images
# (for the Hatari emulator).

# tools are present?
if [ -z "$(which unzip)" ]; then
	echo "ERROR: 'unzip' missing."
	exit 2
fi
if [ -z "$(which rename)" ]; then
	echo "ERROR: 'rename' script (from 'util-linux' package) missing."
	exit 2
fi
if [ -z "$(which mformat)" ] || [ -z "$(which mcopy)" ]; then
	echo "ERROR: 'mformat' or 'mcopy' (from 'mtools' package) missing."
	exit 2
fi

# one ZIPFILE given?
if [ $# -lt 1 ] || [ -z "$1" ] || [ $# -gt 2 ]; then
	name=${0##*/}
	echo "Convert .ZIP files to .ST disk images."
	echo "This script requires the mtools package."
	echo
	echo "Usage:"
	echo " $name srcname.zip [destname.st]"
	echo
	echo "Example:"
	echo " for zip in *.zip; do $name \$zip; done"
	echo
	echo "ERROR: wrong number of argument(s)."
	exit 1
fi

ZIPFILE=$1
STFILE=$2
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

TEMPDIR=`mktemp -d` || exit 2
echo "Converting" $ZIPFILE "->" $TEMPDIR "->" $STFILE

echo
step=1
echo "$step) Unzipping..."
unzip $ZIPFILE -d $TEMPDIR || exit 2

# .zip files created with STZip sometimes have wrong access rights...
chmod -R u+rw $TEMPDIR/*

WORKINGDIR=`pwd`

echo
step=$(($step+1))
echo "$step) Changing file names to upper case..."
for i in `find $TEMPDIR/* -depth` ; do
	cd `dirname $i`
	rename -v 'y/a-z/A-Z/' `basename $i`
done

cd $WORKINGDIR

# size of reserved sectors, FATs & root dir + zip content size
size=$((24 + $(du -ks $TEMPDIR|awk '{print $1}')))

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
	dd if=/dev/zero of=$STFILE bs=1024 count=$disksize
	
	echo
	step=$(($step+1))
	echo "$step) Formating disk image..."
	case $disksize in
		360) mformat -i $STFILE -t 80 -h 1 -n 9 -a :: ;;
		400) mformat -i $STFILE -t 80 -h 1 -n 10 -a :: ;;
		800) mformat -i $STFILE -t 80 -h 2 -n 10 -a :: ;;
		*) mformat -i $STFILE -f $disksize -a :: ;;
	esac
	
	echo
	step=$(($step+1))
	echo "$step) Copying data to disk image..."
	mcopy -i $STFILE -spmv $TEMPDIR/* ::
else
	echo "ERROR: zip contents don't fit to a floppy image ($size > 2880 KB)."
fi

echo
step=$(($step+1))
echo "$step) Cleaning up temporary files..."
rm -rv $TEMPDIR

echo "Done."
