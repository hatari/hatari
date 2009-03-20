#!/bin/sh
# script for creating a compatible DOS HD image for Hatari
# with a single FAT16 partition of given size

# defaults for disk attributes
diskfile=hd.img   # HD image filename
partname=DOS      # partition name

if [ $# -lt 1 ]; then
	name=${0##*/}
	echo
	echo "usage: $name <size> [filename] [partition name] [directory]"
	echo
	echo "Create an ACSI/IDE harddisk image for Hatari with a single Atari"
	echo "compatible DOS partition.  Arguments are (defaults in parenthesis):"
	echo "- size: harddisk image size in megabytes, 8-256"
	echo "- filename: name for the harddisk image ($diskfile)"
	echo "- partition name: name for that single partition ($partname)"
	echo "- directory: directory for initial content copied to the image"
	echo
	echo "For example:"
	echo "- 16MB '$diskfile' HD image:"
	echo "  $name 16"
	echo "-  8MB image with 'TEST' partition having files from content/:"
	echo "  $name 8 8mb-disk.img TEST content/"
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

# check arguments
if [ $1 -lt 8 ]; then
	echo "ERROR: disk size needs to be at least 8 (MB) to work properly."
	exit 1
fi
if [ $1 -gt 256 ]; then
	echo "ERROR: EmuTOS supports only partitions up to 256 (MB)."
	exit 1
fi
disksize=$1
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
	echo
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
echo "parted $diskfile mklabel msdos mkpart primary fat16 0 $disksize set 1 boot on"
parted $diskfile mklabel msdos mkpart primary fat16 0 $disksize set 1 boot on || exit 2

echo
step=$(($step+1))
# create an Atari compatible DOS partition that fits to disk
# size is in 1/2KB sectors, mkdosfs takes in kilobytes
echo "$step) Creating Atari partition..."
sectors=$(parted $diskfile unit s print | awk '/ 1 /{print $4}' | tr -d s)
# mkdosfs keeps cluster count below 32765 when -A is used by increasing
# the sector size (this is for TOS <1.04 compatibility, -A guarantees
# also 2 sectors / cluster and Atari serial number etc).  mtools barfs
# if partition size doesn't divide evenly with track size which is by
# default 32 sectors.  Decrease file system size to make sure everything
# is evenly aligned.
tracksize=32
clustertmp=$((sectors/2))
while [ $clustertmp -gt 32765 ]; do
	clustertmp=$((clustertmp/2))
	tracksize=$(($tracksize*2))
done
origsectors=$sectors
sectors=$(($sectors/$tracksize))
sectors=$(($sectors*$tracksize))
kilobytes=$(($sectors/2))
if [ $sectors -ne $origsectors ]; then
	echo "Align sector count with track size: $origsectors -> $sectors ($kilobytes kB)"
fi
echo "mkdosfs -A -n $partname -C $tmppart $kilobytes"
mkdosfs -A -n $partname -C $tmppart $kilobytes

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
echo "dd if=$tmppart of=$diskfile bs=512 seek=$start count=$sectors"
dd if=$tmppart of=$diskfile bs=512 seek=$start count=$sectors

step=$(($step+1))
# cleanup is done by exit_cleanup() trap
