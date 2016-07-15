#!/bin/sh
# script for creating a compatible DOS HD image for Hatari with
# a single FAT16 partition of given size, with given contents

# defaults for disk attributes
diskfile=hd.img   # HD image filename
partname=DOS      # partition name

# no args or first arg has non-digit characters?
if [ $# -lt 1 ] || [ \! -z "$(echo $1|tr -d 0-9)" ]; then
	name=${0##*/}
	echo
	echo "usage: $name <size> [filename] [partition name] [directory]"
	echo
	echo "Create an ACSI/IDE harddisk image for Hatari with a single Atari"
	echo "compatible DOS partition.  Arguments are (defaults in parenthesis):"
	echo "- size: harddisk image size in megabytes, 8-512"
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
if [ -z $(which mkdosfs) ] || [ -z $(which python) ]; then
	echo "ERROR: either mkdosfs or python tool missing!"
	exit 1
fi

# check disk size
disksize=$1
if [ $disksize -lt 5 ]; then
	echo "ERROR: disk size needs to be at least 5 (MB) to work properly."
	exit 1
fi
if [ $disksize -gt 512 ]; then
	echo "ERROR: mkdosfs supports Atari compatible partitions only up to 512 MB."
	exit 1
fi

# check optional arguments
if [ \! -z $2 ]; then
	diskfile=$2
fi
if [ \! -z $3 ]; then
	partname=$3
fi

# check content
convertdir=""
if [ \! -z $4 ]; then
	contentdir=${4%/}
	if [ \! -d $contentdir ]; then
		echo "ERROR: given content directory doesn't exist!"
		exit 1
	fi
	contentsize=$(du -ks $contentdir | awk '{printf("%d", $1/1024)}')
	if [ $contentsize -ge $disksize ]; then
		echo "ERROR: '$contentdir' directory contents ($contentsize MB) don't fit to given image size ($disksize MB)!"
		exit 1
	fi
	# name conversion script should be in same dir as this script, or in PATH
	convert=${0%/*}/atari-convert-dir.py
	if [ \! -x $convert ]; then
		if [ -z $(which atari-convert-dir) ]; then
			echo "ERROR: $convert script for file name conversion missing!"
			exit 1
		fi
		convert=atari-convert-dir
	fi
	convertdir=$contentdir.converted
	if [ -z $(which mcopy) ]; then
		echo "ERROR: mcopy (from Mtools) missing!"
		exit 1
	fi
fi

# don't overwrite files by accident
if [ -f $diskfile ]; then
	echo "ERROR: given harddisk image already exits. Give another name or remove it:"
	echo "  rm $diskfile"
	exit 1
fi

# disk geometry
skip=0            # alignment / "padding" sectors between MBR before partition
diskheads=16
tracksectors=32	  # same as used by mkdosfs
sectorsize=512

# partition size in sectors:
# 16*32*512 is 1/4MB -> multiply by 4 to get number of required sectors
partsectors=$((4*$disksize*$diskheads*$tracksectors))

# temporary files
tmppart=$diskfile.part

# ------------------------------------------------------------------

error="premature script exit"
# script exit/error handling
exit_cleanup ()
{
	echo
	if [ -z "$error" ]; then
		echo "$step) Cleaning up..."
	else
		echo "ERROR: $error"
		echo
		echo "cleaning up..."
		if [ -f $diskfile ]; then
			echo "rm -f $diskfile"
			rm -f $diskfile
		fi
	fi
	if [ -f $tmppart ]; then
		echo "rm -f $tmppart"
		rm -f $tmppart
	fi
	if [ \! -z $convertdir ] && [ -d $convertdir ]; then
		echo "rm -f $convertdir"
		rm -f $convertdir
	fi
	echo "Done."
}
trap exit_cleanup EXIT

# ------------------------------------------------------------------

echo
step=1
echo "$step) Create DOS Master Boot Record / partition table..."
# See:
# - http://en.wikipedia.org/wiki/Master_boot_record
# - http://en.wikipedia.org/wiki/Cylinder-head-sector
# - http://en.wikipedia.org/wiki/File_Allocation_Table#Boot_Sector
# For DOS MBR, the values are little endian.
# -----------
python << EOF
#!/usr/bin/env python
mbr = bytearray(512)

def set_long(idx, value):
    mbr[idx+0] = value & 0xff
    mbr[idx+1] = value >> 8 & 0xff
    mbr[idx+2] = value >> 16 & 0xff
    mbr[idx+3] = value >> 24

def set_word(idx, value):
    mbr[idx] = value & 0xff
    mbr[idx+1] = value >> 8 & 0xff

def set_CHS(idx, values):
    c, h, s = values
    print "CHS: %3d,%3d,%3d @ $%x" % (c,h,s,idx)
    mbr[idx] = h
    mbr[idx+1] = (s & 0x3F) | ((c >> 2) & 0xC0)
    mbr[idx+2] = c & 0xFF

def LBA2CHS(lba):
    c = lba / ($tracksectors * $diskheads)
    h = (lba / $tracksectors) % $diskheads
    s = (lba % $tracksectors) + 1
    return (c,h,s)

# disk size
sectors = 1 + $skip + $partsectors
if sectors < 65536:
    set_word(0x13, sectors)
    set_long(0x20, sectors)
    parttype=0x4
else:
    set_long(0x20, sectors)
    parttype=0x6

# reserved sectors = MBR
mbr[0x0E] = 1

# CHS information
set_word(0x0B, $sectorsize)
mbr[0x0D] = 2 # sectors / cluster
set_word(0x18, $tracksectors)
set_word(0x1A, $diskheads)
  
# non-removable disk
mbr[0x15] = 0xF8
mbr[0x24] = 0x80

# partition size in sectors
partsectors = $partsectors - 1
# first partition takes all
offset = 0x1BE
mbr[offset] = 0x80 # bootable
mbr[offset+4] = parttype
# partition start & sector count in LBA
set_long(offset + 0x08, 1)
set_long(offset + 0x0C, partsectors)
# partition start & end in CHS
set_CHS(offset + 1, LBA2CHS(1))
set_CHS(offset + 5, LBA2CHS(partsectors))
# 3 last partitions are empty
for i in (1,2,3):
    offset += 0x10
    set_long(offset + 0x08, partsectors+1)
    set_long(offset + 0x0C, 0)
    set_CHS(offset + 1, LBA2CHS(partsectors+1))
    set_CHS(offset + 5, LBA2CHS(partsectors))

# MBR signature
mbr[0x1FE] = 0x55
mbr[0x1FF] = 0xAA

open("$diskfile", "wb").write(bytes(mbr))
EOF
# -----------
od -t x1 $diskfile

# ------------------------------------------------------------------

echo
step=$(($step+1))
echo "$step) Create an Atari TOS compatible DOS partition..."
# mkdosfs keeps the sector count below 32765 when -A is used by increasing
# the logical sector size (this is for TOS compatibility, -A guarantees
# also 2 sectors / cluster and Atari serial number etc).  Mtools barfs
# if partition size doesn't divide evenly with its track size.  Determine
# suitable cluster count & corresponding track size and align (decrease)
# the file system sector count accordingly.
tracksize=32
clustertmp=$((partsectors/2))
echo "Sectors: $partsectors, sectors/track: $tracksize, clusters: $clustertmp"
while [ $clustertmp -gt 32765 ]; do
	clustertmp=$((clustertmp/2))
	tracksize=$(($tracksize*2))
	echo "Doubling sector size as >32765 clusters -> $clustertmp clusters"
done
sectors=$(($partsectors/$tracksize))
sectors=$(($sectors*$tracksize))
kilobytes=$(($sectors/2))
if [ $sectors -ne $partsectors ]; then
	echo "Align sector count with clusters/sectors/track: $partsectors -> $sectors ($kilobytes kB)"
fi
echo "mkdosfs -A -F 16 -n $partname -C $tmppart $kilobytes"
mkdosfs -A -F 16 -n $partname -C $tmppart $kilobytes

# ------------------------------------------------------------------

if [ \! -z $contentdir ]; then
	echo
	step=$(($step+1))
	echo "$step) Clip/convert long file names to Atari compatible 8+3 format..."
	echo "$convert $contentdir $convertdir"
	$convert $contentdir $convertdir
	if [ $? -ne 0 ]; then
		error="conversion failed."
		exit 2
	fi

	echo
	step=$(($step+1))
	# copy contents of given directory to the new partition
	echo "$step) Copy the initial content to the partition..."
	echo "MTOOLS_NO_VFAT=1 mcopy -i $tmppart -spmv $convertdir/* ::"
	MTOOLS_NO_VFAT=1 mcopy -i $tmppart -spmv $convertdir/* ::
	if [ $? -ne 0 ]; then
		error="mcopy failed."
		exit 2
	fi
	echo "rm -rf $convertdir"
	rm -rf $convertdir
fi

# ------------------------------------------------------------------

echo
step=$(($step+1))
# copy the partition into disk
echo "$step) Copy the partition to disk image..."
echo "dd if=$tmppart of=$diskfile bs=512 seek=$((1+$skip)) count=$sectors"
dd if=$tmppart of=$diskfile bs=512 seek=$((1+$skip)) count=$sectors

step=$(($step+1))
# cleanup is done by exit_cleanup() trap
error=""
