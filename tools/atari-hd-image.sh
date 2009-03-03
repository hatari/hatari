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

# temporary files
tmppart=$diskfile.part

# no leftovers from previous runs?
if [ -f $diskfile ]; then
	toremove="$diskfile"
else
	toremove=""
fi
if [ -f $tmppart ]; then
	toremove="$toremove $tmppart"
fi
if [ \! -z "$toremove" ]; then
	echo "ERROR: Files from a previous run detected. To proceed, do:"
	echo "  rm $toremove"
	exit 1
fi

# create disk image
echo "Creating disk image..."
dd if=/dev/zero of=$diskfile bs=1M count=$disksize

# create DOS partition table/partition to image
echo "Creating partition table to disk image..."
parted $diskfile mklabel msdos
# create fat16 primary partition and mark it bootable
parted $diskfile mkpart primary fat16 0 $disksize
parted $diskfile set 1 boot on

# create an Atari compatible DOS partition that fits to disk
# size is in sectors, mkdosfs takes in kilobytes
echo "Creating Atari partition..."
size=$(parted $diskfile unit s print | awk '/ 1 /{print $4}' | tr -d s)
mkdosfs -A -n $partname -C $tmppart $(($size/2))

if [ \! -z $contentdir ]; then
	# copy contents of given directory to the new partition
	echo "Copying the initial content to the partition..."
	MTOOLS_NO_VFAT=1 mcopy -i $tmppart -spmv $contentdir/* ::
fi

# copy the partition into disk
echo "Copying the partition to disk image..."
start=$(parted $diskfile unit s print | awk '/ 1 /{print $2}' | tr -d s)
dd if=$tmppart of=$diskfile bs=512 seek=$start count=$size
rm $tmppart

echo "Done."
