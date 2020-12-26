

                                    Hatari

                             Version 2.3.1, December 2020

                            http://hatari.tuxfamily.org/


Contents:
---------
1. License
2. What is Hatari?
3. Compiling and installing
   3.1 Installing Hatari dependencies
   3.2 Configuring and compiling
   3.3 WinUAE and "old" UAE CPU cores
   3.4 IPF support using capsimage library
   3.5 Notes for Linux distribution packagers
       3.5.1 Known distro problems
4. Running Hatari
   4.1 Known Windows (SDL) issues
5. Hatari tools and their run-time dependencies
6. Hatari source subdirectory contents
7. Contact


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
as it has been defined for IPF library version 5.1. Linking against modified
versions of the IPF library is also allowed, as long as neither the license
nor the purpose of the library (accessing .ipf or .ctr disk images) was changed.
You may copy and distribute such a system following the terms of the GNU GPL
for Hatari and the licenses of the other code concerned.


 2) What is Hatari?
 ------------------

Hatari is an Atari ST/STE/TT/Falcon emulator for Linux, FreeBSD, NetBSD, macOS,
Windows and other Systems which are supported by the SDL library. Unlike most
other open source ST emulators which try to give you a good environment for
running GEM applications, Hatari tries to emulate the hardware as close as
possible so that it is able to run most of the old Atari games and demos.


 3) Compiling and installing
 ---------------------------

To build and use Hatari, you first need to install its dependent libraries.


 3.1) Installing Hatari dependencies

Required:
- The SDL library v1.2.10 or newer (http://www.libsdl.org)

Optional:
- The zlib compression library (http://www.gzip.org/zlib/)
- The PNG image library for PNG format screenshots and to decrease
  AVI video recording file sizes (http://www.libpng.org/)
- The GNU Readline library for Hatari debugger command line editing
- The Xlib library to support Hatari Python UI window embedding on
  systems with the X window system (Linux and other unixes)
- The PortMidi library required for MIDI device support on macOS and Windows
  (http://portmedia.sourceforge.net/)
- The portaudio library for Falcon microphone handling
- The udev library for NatFeats SCSI driver media change detection
- The IPF support library (http://www.softpres.org/download)

Don't forget to also install the header files of these libraries for compiling
Hatari (some Linux distributions use separate development packages for these
header files)!

For compiling Hatari, you need a C compiler that supports the C99 standard
(preferably GNU C or Clang), and a working CMake (v3.3 or newer) installation,
see http://www.cmake.org/ for details.

On RedHat based Linux distributions, you get (most of) these with:
	sudo dnf install gcc cmake SDL2-devel zlib-devel libpng-devel \
	  readline-devel portaudio-devel

And on Debian/Ubuntu based ones with:
	sudo apt install gcc cmake libsdl2-dev zlib1g-dev libpng-dev \
	  libreadline-dev portaudio19-dev


 3.2) Configuring and compiling

CMake can generate makefiles for various flavours of "Make" (like GNU-Make)
and various IDEs like Xcode on macOS. To run CMake, you have to pass the
path to the sources of Hatari as parameter. For example, run the following
command sequence to configure the build of Hatari in a separate build directory
(assuming that the current working directory is the top of the source tree):

	mkdir -p build
	cd build
	cmake ..

Have a look at the manual of CMake for other options. Alternatively, you can
use the "cmake-gui" program to configure the sources with a graphical
application or "ccmake" to configure them with ncurses UI.

For your convenience we also ship an old-fashioned configure script which can
be used as a wrapper for running cmake. Type "./configure --help" to see the
options of this script.

Once you have successfully configured the build settings, you can compile
Hatari with:

	cmake --build . -j $(getconf _NPROCESSORS_ONLN)

If all works fine, you should get the executable "hatari" in the src/ sub-
directory of the build tree. You can then either run the executable from
there, or install the emulator system-wide by typing:

	cmake --install .

Note: This only works with CMake version 3.15 and later. On earlier versions,
you have to use the install command of the generator program instead, e.g.
"make install" if you are using the classical "make" for building Hatari.


 3.3) WinUAE and "old" UAE CPU cores

Up to version 1.9, Hatari had 2 different CPU cores : the "old" UAE CPU core
used for STF/STE and the WinUAE CPU core for better TT/Falcon emulation.
Default was to use the old UAE CPU core.

Starting with Hatari 2.0, all the STF/STE specific code from the old CPU core
was integrated into the new WinUAE CPU core. Some improvements were
also added to the new CPU Core, greatly improving cycle accuracy and low
level 680xx emulation.

Therefore, the new CPU core is now the default for all emulated machines.

Old CPU core can still be used by running "./configure --enable-old-uae-cpu"
which may be useful on weakest machines too slow to run the new CPU core
at acceptable speed.  Support for old CPU core will be removed soon though
as it's not anymore properly tested.


 3.4) IPF support using capsimage library

Hatari can use the optional capsimage library to access IPF and CTR
files. Those files are created using the Kryoflux board and allow to
record MFM exact copies of original games, including the protection.

Hatari supports version 5.1 of the library (previous support for older
version 4.2 was removed as it is not used anymore)

Refer to http://softpres.org/download and get the corresponding file
from the "User Distribution" section that matches your OS.

For version 5.1, you should have the following files in your include path :
/usr/local/include/caps/
	CapsAPI.h
	CapsFDC.h
	CapsForm.h
	CapsLibAll.h
	CapsLib.h
	CapsLibVersion.h
	ComLib.h
	CommonTypes.h

You should also copy the libcapsimage.so* files in your library path,
for example in /usr/local/lib/caps/


 3.5) Notes for Linux distribution packagers

TOS tester in tests/tosboot/ directory can be used to verify that
Hatari was built fine enough that it's able to boot all tested TOS
versions in various different HW configurations and run some GEMDOS
based tests.  For EmuTOS, use the latest released 512k version to
get best test coverage.

If Hatari package will have two application menu entries for Hatari,
one for the Python UI embedding Hatari, and another one for the plain
SDL version, the latter could open also a terminal window for Hatari
command line debugger and its console messages:
x-terminal-emulator -T "Hatari debugger, invoke with AltGr+Pause" -e hatari

tools/hatari-tos-register.sh is a minimal example of Linux init script
registering Hatari as a (binfmt_misc) handler for TOS binaries.

Alternatively one could add a mime type for TOS binaries with xdg-mime:
  http://portland.freedesktop.org/xdg-utils-1.0/xdg-mime.html
But registering handlers for mime-types seems desktop specific.


 3.5.1) Known distro problems

If Hatari is built with portaudio support, *and* either portaudio or
ALSA is configured to use pulseaudio plugin, that plugin aborts Hatari
at Falcon emulation startup, if Pulseaudio server is not running.

This is because:
- Falcon microphone emulation initializes Portaudio, which uses ALSA
- Many distributions enable (by default) pulseaudio plugin(s) for ALSA
  and/or portaudio
- Pulseaudio plugin aborts when it cannot connect to pulseaudio server

Note: Normal Hatari audio output goes through SDL, which uses
pulseaudio library directly.  That doesn't have this issue.


Old RHEL 5 and the derived CentOS v5.x Linux distributions ship
with a broken readline library:
	https://bugzilla.redhat.com/show_bug.cgi?id=499837

To get CMake readline detection and linking working on them,
you need to give these as extra arguments to the "cmake" command:
   -DCMAKE_C_FLAGS=-lncurses -DCMAKE_EXE_LINKER_FLAGS=-lncurses


 4) Running Hatari
 -----------------

For information about how to use the running emulator, please read the file
doc/manual.html. Here are just some hints for the impatient people:

* Before you can run the emulator, you need a TOS ROM image.  If one
  named as "tos.img" is neither in the data directory of the emulator
  (DATADIR variable in CMake configuration), or in the current
  directory, Hatari will ask you to select one.

  - Hatari binary packages ship unmodified EmuTOS ROM image with them
    (renamed as tos.img), but you may need an original Atari TOS ROM
    image for better compatibility.  For more information on EmuTOS,
    see doc/emutos.txt.

* While the emulator is running, you can open the configuration menu
  by pressing F12, the F11 key will toggle fullscreen/windowed mode.
  Pressing ALTGR-q will quit the emulator.


 4.1) Known Windows (SDL) issues

On Windows, Hatari console output doesn't go to console like on other
platforms.

This is because Windows SDL v1 library redirects all console output
(including help!) to stdout.txt and stderr.txt files (by default), and
SDL v2 library discards all that output.

To see Hatari help/warning/trace output, and to interact with Hatari
debugger, there are two options:
- Run Hatari with "-W" option, or
- Compile Hatari with "-mconsole" option (as last --linker flag) to
  build Hatari for the Windows console subsystem

Because these cause separate console output window to be opened (in
addition to the Hatari window), they are not enabled by default. 


 5) Hatari tools and their run-time dependencies
 -----------------------------------------------

While Hatari installs few binary tools binaries:
- hmsa (converts between MSA & ST disk images)
- gst2ascii (outputs a.out and DRI/GST debug symbol table contents)

Most of its tools are Python and shell scripts.
Their main run-time dependencies are:
- python (hatariui, hconsole, hatari_profile, atari-convert-dir)
- python & gtk3 (hatariui)
- mkdosfs (atari-hd-image)
- mtools (atari-hd-image / zip2st)
- unzip (zip2st)


 6) Hatari source subdirectory contents
 --------------------------------------

* cmake/ -- extra CMake files for configuring Hatari to build environment
* doc/ -- Hatari documentation
* etc/ -- old Hatari versions config files for obsolete HW
* python-ui/ -- external Python / Gtk UI for Hatari
* share/ -- Hatari desktop integration; icons, mimetypes
* src/ -- C-sources for Hatari emulator program
  - convert/ -- screen format conversion functions
  - cpu/ -- cycle-exact WinUAE CPU core (+FPU/MMU)
  - uae-cpu/ -- old UAE CPU core (+FPU)
  - debug/ -- builtin debugger/profiler
  - falcon/ -- Falcon emulation specific code (Videl used also for TT)
  - includes/ -- common include files
  - gui-osx/ -- builtin MacOS GUI
  - gui-sdl/ -- builtin SDL v1 / v2 GUI for Hatari
  - gui-win/ -- MS Windows console code + icon
* tests/ -- shell/python scripts & programs for testing emulator functionality
  - keymap/ -- programs showing keycodes to use in Hatari keymap files
  - natfeats/ -- test and example Atari code for using Hatari features
  - etc.
* tools/ -- shell/python scripts & programs useful with Hatari
  - debugger/ -- debug symbol conversion scripts & profile data tools
  - hconsole/ -- out-of-process Hatari control / automation tool
  - hmsa/ -- floppy image format conversion tool


 7) Contact
 ----------

If you want to contact the authors of Hatari, please have a look at the file
doc/authors.txt for the e-mail addresses or use the Hatari mailing list.

Visit the website of Hatari on Tuxfamily.org for more details:

 http://hatari.tuxfamily.org/contact.html
