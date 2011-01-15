

                                    Hatari

                                 Version 1.4

                          http://hatari.berlios.de/


Contents:
---------
1. License
2. What is Hatari?
3. Compiling and installing
   3.1 Known problems
4. Running Hatari
5. Contact


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


 2) What is Hatari?
 ------------------

Hatari is an Atari ST/STE/TT/Falcon emulator for Linux, FreeBSD, NetBSD,
BeOS, Mac-OSX and other Systems which are supported by the SDL library.
Unlike most other open source ST emulators which try to give you a good
environment for running GEM applications, Hatari tries to emulate the hardware
as close as possible so that it is able to run most of the old Atari games
and demos.  Because of this, it may be somewhat slower than less accurate
emulators.


 3) Compiling and installing
 ---------------------------

For using Hatari, you need to have installed the following libraries:

Required:
- The SDL library v1.2.10 or newer (http://www.libsdl.org)
- The zlib compression library (http://www.gzip.org/zlib/)

Optional:
- The PNG image library for PNG format screenshots and to decrease
  AVI video recording file sizes (http://www.libpng.org/)
- The GNU Readline library for Hatari debugger command line editing
- The Xlib library to support Hatari Python UI window embedding on
  systems with the X window system (Linux and other unixes) 
- The portaudio library for Falcon microphone handling

Don't forget to also install the header files of these libraries for compiling
Hatari (some Linux distributions use separate development packages for these
header files)!

For compiling Hatari, you need a C compiler (preferably GNU C), and a working
CMake installation (see http://www.cmake.org/ for details).

CMake can generate makefiles for various flavours of "Make" (like GNU-Make)
and various IDEs like Xcode on Mac OS X. To run CMake, you've got to pass the
path to the sources of Hatari as parameter, for example run the following if
you are in the topmost directory of the Hatari source tree:
	cmake .

If you're tracking Hatari version control, it's preferable to do
the build in a separate build directory as above would overwrite
the (non-CMake) Makefiles coming with Hatari:
	mkdir -p build
	cd build
	cmake ..

Have a look at the manual of CMake for other options. Alternatively, you can
use the "cmake-gui" program to configure the sources with a graphical
application.

For your convenience we also ship an old-fashioned configure script which can
be used as a wrapper for running cmake. Type "./configure --help" to see the
options of this script.

Assuming that you've used the Makefile generator of CMake, and cmake finished
the configuration successfully, you can compile Hatari by typing "make". If all
works fine, you'll get the executable "hatari" in the src/ subdirectory of the
build tree. You can then install the emulator by typing "make install".


 3.1) Known problems

RHEL 5 and the derived CentOS v5.x Linux distributions ship
with a broken readline library:
	https://bugzilla.redhat.com/show_bug.cgi?id=499837

To get CMake readline detection and linking working on them,
you need to give these as arguments to the "cmake" command above:
   -DCMAKE_C_FLAGS=-lncurses -DCMAKE_EXE_LINKER_FLAGS=-lncurses


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

