#!/bin/sh
# script for creating a compatible DOS HD image for Hatari
# with a single FAT16 partition of given size

if [ $# -lt 1 ]; then
	echo
	echo "usage: ${0##*/} <size> [filename] [partition name] [directory]"
	echo
	echo "Create an ACSI/IDE harddisk image for Hatari with a single Atari"
	echo "compatible DOS partition.  Arguments are (defaults in parenthesis):"
	echo "- size: harddisk image size"
	echo "- filename: name for the harddisk image (hd.img)"
	echo "- partition name: name for that single partition (DOS)"
	echo "- directory: directory for initial content copied to the image"
	echo
	exit 1
fi

# parted/mkdosfs reside in /sbin
PATH=/sbin:$PATH
export PATH

# check tools
if [ -z $(which parted) ] || [ -z $(which mkdosfs) ]; then
	echo "ERROR: either parted or mkdosfs missing!"
	exit 1
fi

# defaults for disk attributes
diskfile=hd.img   # HD image filename
partname=DOS      # partition name

# check arguments
if [ $1 -gt 0 ]; then
	disksize=$1
fi
if [ \! -z $2 ]; then
	diskfile=$2
fi
if [ \! -z $3 ]; then
	partname=$3
fi
if [ \! -z $4 ]; then
	contentdir=$4
fi

# don't overwrite files by accident
if [ -f $diskfile ]; then
	echo "ERROR: given harddisk image already exits. Give another name or remove it:"
	echo "  rm $diskfile"
	exit 1
fi

# temporary files
tmppart=$diskfile.part

# script exit/error handling
exit_cleanup ()
{
	if [ $? -eq 0 ]; then
		echo "$step) Cleaning up..."
	else
		echo
		echo "ERROR, cleaning up..."
		echo "rm -f $diskfile"
		rm -f $diskfile
	fi
	echo "rm -f $tmppart"
	rm -f $tmppart
	echo "Done."
}
trap exit_cleanup EXIT

echo
step=1
# create disk image
echo "$step) Creating disk image..."
echo "dd if=/dev/zero of=$diskfile bs=1M count=$disksize"
dd if=/dev/zero of=$diskfile bs=1M count=$disksize

echo
step=$(($step+1))
# create DOS partition table and a bootable primary FAT16 partition to image
echo "$step) Creating partition table + primary partition to the disk image..."
echo "parted $diskfile mktable msdos mkpart primary fat16 0 $disksize set 1 boot on"
parted $diskfile mktable msdos mkpart primary fat16 0 $disksize set 1 boot on

echo
step=$(($step+1))
# create an Atari compatible DOS partition that fits to disk
# size is in sectors, mkdosfs takes in kilobytes
echo "$step) Creating Atari partition..."
size=$(parted $diskfile unit s print | awk '/ 1 /{print $4}' | tr -d s)
echo "mkdosfs -A -n $partname -C $tmppart $(($size/2))"
mkdosfs -A -n $partname -C $tmppart $(($size/2))

if [ \! -z $contentdir ]; then
	echo
	step=$(($step+1))
	# copy contents of given directory to the new partition
	echo "$step) Copying the initial content to the partition..."
	echo "MTOOLS_NO_VFAT=1 mcopy -i $tmppart -spmv $contentdir/* ::"
	MTOOLS_NO_VFAT=1 mcopy -i $tmppart -spmv $contentdir/* ::
fi

echo
step=$(($step+1))
# copy the partition into disk
echo "$step) Copying the partition to disk image..."
start=$(parted $diskfile unit s print | awk '/ 1 /{print $2}' | tr -d s)
echo "dd if=$tmppart of=$diskfile bs=512 seek=$start count=$size"
dd if=$tmppart of=$diskfile bs=512 seek=$start count=$size

echo
step=$(($step+1))
# cleanup is done by the exit_handler
