

                                    Hatari

                             Version 1.8, July 2014

                          http://hatari.tuxfamily.org/


Contents:
---------
1. License
2. What is Hatari?
3. Compiling and installing
   3.1 WinUAE and "old" UAE CPU cores
   3.2 IPF support using capsimage library
   3.3 Notes for Linux distribution packagers
       3.3.1 Known distro problems
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

Linking Hatari statically or dynamically with other modules is making a
combined work based on Hatari. Thus, the terms and conditions of the GNU
General Public License cover the whole combination.

In addition, as a special exception, the copyright holders of Hatari give you
permission to combine Hatari with free software programs or libraries that are
released under the GNU LGPL and with code included in the standard release
of the IPF support library (a.k.a. libcapsimage, see http://www.softpres.org/
for more information) under the Software Preservation Society Licence Agreement
as it has been defined for IPF library version 4.2 and 5.1. Linking against modified
versions of the IPF library is also allowed, as long as neither the license
nor the purpose of the library (accessing .ipf or .ctr disk images) was changed.
You may copy and distribute such a system following the terms of the GNU GPL
for Hatari and the licenses of the other code concerned.


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
- The IPF support library (http://www.softpres.org/download)

Don't forget to also install the header files of these libraries for compiling
Hatari (some Linux distributions use separate development packages for these
header files)!

For compiling Hatari, you need a C compiler (preferably GNU C), and a working
CMake (v2.8 or newer) installation, see http://www.cmake.org/ for details.

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
application or "ccmake" to configure them with ncurses UI.

For your convenience we also ship an old-fashioned configure script which can
be used as a wrapper for running cmake. Type "./configure --help" to see the
options of this script.

Assuming that you've used the Makefile generator of CMake, and cmake finished
the configuration successfully, you can compile Hatari by typing "make". If all
works fine, you'll get the executable "hatari" in the src/ subdirectory of the
build tree. You can then install the emulator by typing "make install".


 3.1) WinUAE and "old" UAE CPU cores

By default Hatari is built with the "old" UAE CPU core used in the
earlier Hatari releases, but versions starting from v1.5 support also
new & experimental WinUAE CPU core which offers more cycle accurate
030 & DSP emulation and from v1.6 onwards also working 030 MMU
emulation.

The WinUAE CPU core can be enabled by toggling the ENABLE_WINUAE_CPU
variable in the Hatari CMake configuration (e.g. with the interactive
"ccmake" program).  Alternatively, you can run "./configure
--enable-winuae-cpu", which will run cmake with the correct
parameters.

The plan is to eventually have WinUAE CPU core enabled by default and
deprecate the "old" UAE CPU core, but currently WinUAE CPU core:
- is lacking all the ST/STE specific tweaks and proper testing
  for ST/STE compatibility
- despite better emulation, it still doesn't run all the Falcon
  programs that run with the "old" core although it works better
  for most of them
- doesn't have full debugger support

It's recommended to use Hatari built with the "old" (default) UAE CPU
core for ST/STE emulation and the new WinUAE core for Falcon emulation.
And test also the old core if Falcon programs don't work with the new
one...


 3.2) IPF support using capsimage library

Hatari can use the optionnal capsimage library to access IPF and CTR
files. Those files are created using the Kryoflux board and allow to
record MFM exact copies of original games, including the protection.

Version 4.2 of the library allows to access IPF files, while the more recent
version 5.1 fixes some bugs, as well as adding support for CTR files.

Since version 5.1 is not yet available for all OSes in binary form, Hatari
still default to version 4.2 (but you can compile capsimage 5.1 sources
to build your library). You can change this by modifying "SET(CAPSIMAGE_VERSION 4)"
into cmake/FindCapsImage.cmake

Refer to http://softpres.org/download and get the corresponding file
from the "User Distribution" section that matches your OS.

For version 4.2, you should have the following files in your include path :
/usr/local/include/caps/
	capsimage.h
	fdc.h
	form.h

For version 5.1, you should have the following files in your include path :
/usr/local/include/caps5/
	CapsAPI.h
	CapsFDC.h
	CapsForm.h
	CapsLibAll.h
	CapsLib.h
	CapsLibVersion.h
	ComLib.h
	CommonTypes.h

You should also copy the libcapsimage.so* files in your library path,
for example in /usr/local/lib/caps/ or /usr/local/lib/caps5/


 3.3) Notes for Linux distribution packagers

TOS tester in tests/tosboot/ directory can be used to verify that
Hatari was built fine enough that it's able to boot all tested TOS
versions in various different HW configurations and run some GEMDOS
based tests.  For EmuTOS, use version v0.8.7 or newer, older versions
are buggy and fail the GEMDOS tests.

If Hatari package will have two application menu entries for Hatari,
one for the Python UI embedding Hatari, and another one for the plain
SDL version, the latter could open also a terminal window for Hatari
command line debugger and its console messages:
x-terminal-emulator -T "Hatari debug window, invoke debugger with AltGr+Pause" -e hatari

tools/hatari-tos-register.sh is a minimal example of Linux init script
registering Hatari as a (binfmt_misc) handler for TOS binaries.

Alternatively one could add a mime type for TOS binaries with xdg-mime:
  http://portland.freedesktop.org/xdg-utils-1.0/xdg-mime.html
But registering handlers for mime-types seems desktop specific.


 3.3.1) Known distro problems

Old RHEL 5 and the derived CentOS v5.x Linux distributions ship
with a broken readline library:
	https://bugzilla.redhat.com/show_bug.cgi?id=499837

To get CMake readline detection and linking working on them,
you need to give these as extra arguments to the "cmake" command:
   -DCMAKE_C_FLAGS=-lncurses -DCMAKE_EXE_LINKER_FLAGS=-lncurses

They also have too old Python/PyGtk version for the python based
Hatari scripts.  Here are patches for Hatari v1.5/v1.6 Python UI:
http://listengine.tuxfamily.org/lists.tuxfamily.org/hatari-devel/2012/01/msg00008.html


 4) Running Hatari
 -----------------

For information about how to use the running emulator, please read the file
doc/manual.html. Here are just some hints for the impatient people:

* Before you can run the emulator, you need a TOS ROM image.  If one
  named as "tos.img" is neither in the data directory of the emulator
  (DATADIR variable in CMake configuration), or in the current
  directory, Hatari will ask you to select one.

  - Hatari binary packages ship unmodified EmuTOS ROM image with them
    (renamed as tos.img), but you need an original Atari TOS ROM image
    for best compatibility.  For more information on EmuTOS, see
    doc/emutos.txt.

* While the emulator is running, you can open the configuration menu
  by pressing F12, the F11 key will toggle fullscreen/windowed mode.
  Pressing ALTGR-q will quit the emulator.


 5) Contact
 ----------

If you want to contact the authors of Hatari, please have a look at the file
doc/authors.txt for the e-mail addresses or use the Hatari mailing list.

Visit the website of Hatari on Tuxfamily.org for more details:

 http://hatari.tuxfamily.org/contact.html

