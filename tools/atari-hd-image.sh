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

# sfdisk/mkdosfs reside in /sbin
PATH=/sbin:$PATH
export PATH

# check tools
if [ -z $(which sfdisk) ] || [ -z $(which mkdosfs) ]; then
	echo "ERROR: either sfdisk or mkdosfs missing!"
	exit 1
fi

# check disk size
if [ $1 -lt 5 ]; then
	echo "ERROR: disk size needs to be at least 5 (MB) to work properly."
	exit 1
fi
if [ $1 -gt 256 ]; then
	echo "ERROR: EmuTOS supports only partitions up to 256 (MB)."
	exit 1
fi
# disk geometry
cylinders=$((4*$1))	# 16*32*512 is 1/4MB
diskheads=16
tracksectors=32		# same as used by mkdosfs
sectorsize=512
partsize=$(($cylinders*$diskheads*$tracksectors*$sectorsize))

# counts in sectors with correct disk geometry
sfdisk="sfdisk -uS -C $cylinders -H $diskheads -S $tracksectors"

# check optional arguments
if [ \! -z $2 ]; then
	diskfile=$2
fi
if [ \! -z $3 ]; then
	partname=$3
fi
if [ \! -z $4 ]; then
	if [ -z $(which mcopy) ]; then
		echo "ERROR: mcopy (from Mtools) missing!"
		exit 1
	fi
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
echo "$step) Creating $1MB sparse disk image..."
echo "dd if=/dev/zero of=$diskfile bs=1 count=0 seek=$((512+partsize))"
dd if=/dev/zero of=$diskfile bs=$((512+partsize)) count=1

echo
step=$(($step+1))
echo "$step) Creating DOS Master Boot Record partition table..."
echo "Add DOS MBR signature needed by sfdisk:"
echo -e "\0125\0252" | dd of=$diskfile bs=1 seek=510 count=2

echo "Add partition table to MBR with single FAT16 partition:"
clusters=$(($partsize/1024))
if [ $clusters -le 32765 ]; then
	fatbits=16
	parttype="0x4"
else
	fatbits=16
	parttype="0x6"
fi
echo "Using FAT$fatbits partition type $parttype"
echo "$sfdisk --no-reread $diskfile: ,,$parttype,*"
$sfdisk --no-reread $diskfile << EOF
,,$parttype,*
EOF
if [ $? -ne 0 ]; then
	exit 2
fi

echo
step=$(($step+1))
echo "$step) Creating Atari TOS compatible DOS partition..."
sectors=$($sfdisk -l $diskfile|awk '/\*/{print $5}')
if [ -z "$sectors" ] || [ $sectors -eq 0 ]; then
	echo "ERROR: couldn't get partition size information."
	exit 2
fi
# mkdosfs keeps the sector count below 32765 when -A is used by increasing
# the logical sector size (this is for TOS compatibility, -A guarantees
# also 2 sectors / cluster and Atari serial number etc).  Mtools barfs
# if partition size doesn't divide evenly with its track size.  Determine
# suitable cluster count & corresponding track size and align (decrease)
# the file system sector count accordingly.
tracksize=32
clustertmp=$((sectors/2))
echo "Sectors: $sectors, sectors/track: $tracksize, clusters: $clustertmp"
while [ $clustertmp -gt 32765 ]; do
	clustertmp=$((clustertmp/2))
	tracksize=$(($tracksize*2))
	echo "Doubling sector size as >32765 clusters -> $clustertmp clusters"
done
origsectors=$sectors
sectors=$(($sectors/$tracksize))
sectors=$(($sectors*$tracksize))
kilobytes=$(($sectors/2))
if [ $sectors -ne $origsectors ]; then
	echo "Align sector count with clusters/sectors/track: $origsectors -> $sectors ($kilobytes kB)"
fi
echo "mkdosfs -A -F $fatbits -n $partname -C $tmppart $kilobytes"
mkdosfs -A -n $partname -C $tmppart $kilobytes

if [ \! -z $contentdir ]; then
	echo
	step=$(($step+1))
	# copy contents of given directory to the new partition
	echo "$step) Copying the initial content to the partition..."
	echo "MTOOLS_NO_VFAT=1 mcopy -i $tmppart -spmv $contentdir/* ::"
	MTOOLS_NO_VFAT=1 mcopy -i $tmppart -spmv $contentdir/* ::
	if [ $? -ne 0 ]; then
		echo "ERROR: failed."
		exit 2
	fi
fi

echo
step=$(($step+1))
# copy the partition into disk
echo "$step) Copying the partition to disk image..."
start=$($sfdisk -l $diskfile|awk '/\*/{print $3}')
if [ -z "$sectors" ] || [ $sectors -eq 0 ]; then
	echo "ERROR: couldn't get partition start information."
	exit 2
fi
echo "dd if=$tmppart of=$diskfile bs=512 seek=$start count=$sectors"
dd if=$tmppart of=$diskfile bs=512 seek=$start count=$sectors

step=$(($step+1))
# cleanup is done by exit_cleanup() trap
