

                            Hatari

                         Version 0.19b

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

Hatari is a "new" Atari ST emulator for Linux. It is based on two main 
sources:
- The WinSTon sourcecode written by Paul Bates. You can get it at:
  http://www.sourceforge.net/projects/winston/
- The UAE's CPU core, you can download UAE at:
  http://www.freiburg.linux.de/~uae/

Hatari is mainly an adaption of the free WinSTon source code to Linux.
(WinSTon is a ST emulator for Windows). But since WinSTon's CPU core is
written in i86 assembler, I was not able to use it for Hatari, too.
Instead I adapted UAE's CPU core for Hatari.
The UAE CPU core has some nice features like 68040 and FPU support.


 3) Compiling and running
 ------------------------

First, you need the SDL library, you can get it at:
 http://www.libsdl.org
Of course, you need GNU-C and (GNU) Make, too!

Change to the src/ directory and adapt the Makefile to suite your mood
(and system of course). Then compile it by typing "make".
If all works fine, you'll get the executable "hatari".

Then you'll have to copy a TOS ROM to the data directory (that can be
specified in the Makefile) and rename to "tos.img", or use the
"--tos" command line option to tell Hatari where to find a TOS ROM.
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
 ALTGR-r  :  Reset the ST (warm).
 ALTGR-c  :  Reset the ST (cold).
 ALTGR-q  :  Quit the emulator.


 5) Floppy disk images
 ---------------------

Not yet written...


 6) Hard disk support
 --------------------

Not yet written...


That's all for the moment. If you want to help me working on Hatari,
e-mail to:
  thothy@users.sourceforge.net


May the fun without the price to be with you ;-) !
