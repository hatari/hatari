

                                    Hatari

                                 Version 1.3.1

                          http://hatari.berlios.de/



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
 51 Franklin Street, Fifth Floor, Boston,
 MA  02110-1301, USA


 2) What is this?
 ----------------

Hatari is an Atari ST/STE/TT/Falcon emulator for Linux, FreeBSD, NetBSD,
BeOS, Mac-OSX and other Systems which are supported by the SDL library.
Unlike most other open source ST emulators which try to give you a good
environment for running GEM applications, Hatari tries to emulate the hardware
as close as possible so that it is able to run most of the old Atari games
and demos.


 3) Compiling and installing
 ---------------------------

For using Hatari, you need to have installed the following libraries:

Required:
- The SDL library (http://www.libsdl.org)
- The zlib compression library (http://www.gzip.org/zlib/)

Optional:
- The PNG image library for PNG format screenshots (http://www.libpng.org/)
- The GNU Readline library for Hatari debugger command line editing
- The Xlib library to support Hatari Python UI window embedding on
  systems with the X window system (Linux and other unixes) 

Don't forget to also install the header files of these libraries for compiling
Hatari (some Linux distributions use separate development packages for these
header files)!

For compiling Hatari, you currently need GNU-C and GNU-Make. Please note that
GNU-Make is often called "gmake" instead of "make" on non-Linux systems.
To configure the build process, you currently have two options: You can either
use the supplied configure script (type "./configure --help" to see the
options) or you can edit the file Makefile.cnf manually. Don't forget to use
some good CFLAGS for the compiler optimizations, e.g. run "configure" in the
following way:

 CFLAGS="-O3 -fomit-frame-pointer" ./configure

Then you can compile Hatari by typing "make" (or "gmake"). If all works fine,
you'll get the executable "hatari" in the src/ subdirectory.


 4) Running Hatari
 -----------------

For information about how to use the running emulator, please read the file
doc/manual.html. Here are just some hints for the impatient people:
Before you can run the emulator, you need a TOS ROM image that should be
stored as "tos.img" in the data directory of the emulator (see the variable
DATADIR in Makefile.cnf).
While the emulator is running, you can open the configuration menu by
pressing F12, the F11 key will toggle fullscreen/windowed mode.
Pressing ALTGR-q will quit the emulator.


 5) Contact
 ----------

If you want to contact the authors of Hatari, please have a look at the file
doc/authors.txt for the e-mail addresses or use the Hatari mailing list.

Visit the project pages of Hatari on Berlios.de for more details:

 http://developer.berlios.de/projects/hatari/

