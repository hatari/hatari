

                                    Hatari

                                 Version 0.40

                        http://hatari.sourceforge.net/



 1) License
 ----------

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Soft-
ware Foundation; either version 2 of the License, or (at your option) any
later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the 
 Free Software Foundation, Inc.,
 59 Temple Place, Suite 330, Boston,
 MA  02111-1307  USA


 2) What is this?
 ----------------

Hatari is an Atari ST emulator for Linux, FreeBSD, NetBSD, BeOS, Mac-OSX and
other Systems which are supported by the SDL library.
Unlike most other open source ST emulators which try to give you a good
environment for running GEM applications, Hatari tries to emulate the hardware
of a ST as close as possible so that it is able to run most of the old ST games
and demos.

Hatari started as an adaption of the free WinSTon source code to Linux.
(WinSTon is a ST emulator for Windows). But since WinSTon's CPU core is
written in i86 assembler, it was not possible to use it for Hatari as Hatari
is intended to be platform independent. So the UAE's CPU core is now being used
in Hatari instead, because this CPU core has been written in portable C and
also has some nice features like 68040 and FPU support.


 3) Compiling and running
 ------------------------

For using Hatari, you need to have installed the following libraries:

- The SDL library (http://www.libsdl.org)
- The zlib compression library (http://www.gzip.org/zlib/)

Don't forget to also install the header files of these libraries for compiling
Hatari (some Linux distributions use separate development packages for these
header files)!

For compiling Hatari, you currently need GNU-C and GNU-Make. Please note that
GNU-Make is often called "gmake" instead of "make" on non-Linux systems.
Now change to the src/ directory and adapt the Makefile to suite your mood (and
system of course). Then you can compile Hatari by typing "make" (or "gmake").
If all works fine, you'll get the executable "hatari".

Before you can use the emulator, you'll have to copy a TOS ROM to the data
directory (that can be specified in the Makefile before compiling) and rename
it to "tos.img", or use the "--tos" command line option to tell Hatari where
to find a TOS ROM.
Hatari needs a TOS ROM image because this contains the operating system
of the emulated Atari. Sorry, it is not possible to ship an image with
the Hatari package since these images are still copyrighted. But you can
easily create an image with a real ST and one of those various ROM-image
programs for the ST. Or search the internet, but don't ask me where to find
one.
Another solution is EmuTOS, which is an open-source TOS clone.
You can find EmuTOS at:
 http://emutos.sourceforge.net
It's not the best solution for playing games or running other old software
due to compatibility issues, but it's free!

When you finally have got a TOS image, try starting Hatari with the option
"--help" to find out more about its command line parameters.


 4) Keyboard shortcuts
 ---------------------

While the emulator is running, you can open the configuration menu by
pressing F12.
The F11 key will toggle fullscreen/windowed mode.
And if you started Hatari with the debugger option, you can enter
the debugger by pressing PAUSE.
Beside those keys, there are some more usefull shortcuts:

 ALTGR-g  :  Grab a screenshot.
 ALTGR-j  :  Toggle cursor-joystick emulation.
 ALTGR-m  :  (Un-)grab mouse.
 ALTGR-r  :  Reset the ST (warm).
 ALTGR-c  :  Reset the ST (cold).
 ALTGR-q  :  Quit the emulator.


 5) Floppy disk images
 ---------------------

Hatari does not use floppy disks directly but disk images due to differences
between the floppy disk controllers of the ST and the PC.
Two types of disk images are currently supported: The raw "ST" type and the
"MSA" (Magic-Shadow-Archiver) type.

The raw type (file suffix should be "*.st") is simply a sector by sector
image of a real floppy disk. You can easily create such an image with the
"dd" program which should normally be pre-installed on every Unix-like system.
Simply type something like "dd if=/dev/fd0 of=myimage.st" to create a disk
image. Of course you need access to /dev/fd0, and depending on your system
and the type of floppy disk you might have to use another device name here
(for example I use /dev/fd0u720 for 720kB disks). However, if the disk is
copy-protected or doesn't use a MSDOS compatible file system, this might
fail. So be very carefull if you're not sure about the disk format.

The other possibility is to image the disk on a real Atari ST. There are
programs like the Magic Shadow Archiver for this task. Hatari supports this
slightly compressed MSA disk images, too. Note that Hatari only supports
the "old" MSA format, there are some Magic Shadow Archiver clones (like
Jay-MSA) that create better compressed but Hatari-incompatible disk images.
However, if you have got such a MSA disk and want to use it with Hatari, you
can still run the corresponding MSA program within Hatari to extract the
incompatible disk image to a normal floppy disk image.

While *.ST and *.MSA are more or less the "standard" types of Atari disk
images, you might sometimes also find DIM or ADF images on the internet.
These currently do not work with Hatari. But since DIM images are nearly
the same as the raw ST images (they only have an additional 32 byte header)
you can easily transform the DIM images into ST images by stripping the
header from the files.
For example try something like:  dd if=input.dim of=output.st bs=32 skip=1

If you've got a disk image that has been created with the old ST emulator
PaCifiST (for DOS) or with early versions of the program Makedisk, and
the disk image does not work with Hatari, then the disk probably suffers
from the "PaCifiST bootsector bug" (Hatari will print a warning message then).
In this case, the bootsector of the disc contains some illegal data, so that
the disc even does not work on a real ST any more. However, if it is a .ST
and not a .MSA disk, you can easily fix it by using a hex-editor to change
the byte at offset $D (13) from 0 to 1 (don't forget to backup your disk image
first, since you can also easily destroy your disk image when changing a wrong
byte there). If the disk contains a bootsector program, you probably have to
adjust the boot sector check sum, too (it can be found at offset $1FE + $1FF).

Hatari also supports disk images that are compressed with (Pk-)ZIP (file suffix
must be ".zip") or GZip (file suffix must be ".st.gz" or ".msa.gz"). You can
even browse the contents of a ZIP file with Hatari's file selection dialog.
But please note that Hatari can not yet save disk image which are compressed
with ZIP. Changes to ZIPped disk images will be lost as soon as you eject the
disk or as soon as you quit the emulator.


 6) Hard disk support
 --------------------

Hatari supports two ways of emulating a ST hard drive: The low-level ACSI
hard disk emulation and a GEMDOS based drive emulation.

To use the ACSI hard disk emulation, you need a hard disk image file with a
pre-installed HD driver in it. So either try to image your old ST hard disk
or grab one from the internet. There is a HD image on the Hatari web page
for download.
Perhaps we'll also provide a tool for creating HD images one day.

With the GEMDOS based drive emulation, you can easily "mount" a folder from
the host file system to a drive of the emulated Atari.
To use the GEMDOS based drive emulation, you should use a folder on your
hard disk that only contains files and folders with valid TOS filenames.
That means that all files/folders should be written in capital letters
and their length mustn't exceed the 8+3 file name length limit. If you don't
want to rename all files to get capital letters, it is also possible to store
that folder on a FAT filesystem since those filesystems are case-insensitive.

GEMDOS drive emulation is an easy way to share files between the host system
and the emulated Atari, but it is known to be incomplete and a little bit
unstable, especially if you use it together with the ACSI hard disk emulation.
So if your programs complain that they could not find/read/write files on
the GEMDOS HD drive, you should try to copy them to a floppy disk image or
a real hard disk image!

Note that changing the HD-image or the GEMDOS HD-folder will reset the emulated
Atari since it is not possible to switch the hard disk while the emulator is
running.


 7) Contact
 ----------

If you want to contact the authors of Hatari, please have a look at the file
authors.txt for the e-mail addresses or use the Hatari mailing list.

Visit the project pages of Hatari on SourceForge.net for more details:

 http://sourceforge.net/projects/hatari/
