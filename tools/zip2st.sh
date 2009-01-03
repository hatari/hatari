#!/bin/sh

ZIPFILE=$1
STFILE=$2
TEMPDIR=`mktemp -d`

if [ x"$ZIPFILE" = x ]; then
	echo "Convert .ZIP files to .ST disk images."
	echo "This script requires the mtools package."
	echo "Usage:"
	echo " $0 srcname.zip [destname.st]"
	exit 1
fi

if [ x"$STFILE" = x ]; then
	STFILE=$ZIPFILE.st
fi

echo "Converting" $ZIPFILE "->" $TEMPDIR "->" $STFILE

echo
echo "1) Unzipping..."
unzip $ZIPFILE -d $TEMPDIR || exit 2

WORKINGDIR=`pwd`

echo
echo "2) Changing file names to upper case..."
for i in `find $TEMPDIR/* -depth` ; do
	cd `dirname $i`
	rename -v 'y/a-z/A-Z/' `basename $i`
done

cd $WORKINGDIR

echo
echo "3) Creating disk image..."
dd if=/dev/zero of=$STFILE bs=1024 count=720

echo
echo "4) Formating disk image..."
mformat -i $STFILE -f 720 -a ::

echo
echo "5) Copying data to disk image..."
mcopy -i $STFILE -spmv $TEMPDIR/* ::

echo
echo "6) Cleaning up temporary files..."
rm -rv $TEMPDIR

echo "Done."
