
      *****************
      * Hatari        *
      * Version 0.05a *
      *****************


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

The reason for writing Hatari is, that there wasn't any Atari emulator on
Linux that also runs a lot of those old but funny ST games. STonX, FAST
and Osis are all more or less nice for working with - but you can hardly
run any game with them.
So I started adapting the WinSTon source code to Linux. And since WinSTon's
CPU core is written in Microsoft-C assembler :-( I was not able to use it.
Instead I adapted UAE's CPU core for Hatari.
The UAE CPU core has some nice features as 68020, FPU and JIT support
(but none of them is enabled in Hataris source at the moment).
On the other side, this core has a very strange CPU cycle time emulation,
so I'll perhaps will adapt another core one day...

Important: Hatari isn't yet very usable - it does not run very stable and
a lot of WinSTon original features are still missing in this version, e.g.
the ST-MED resolution emulation or the configuration dialogs.
But you can already use a floppy disk image (must be given as an argument on
the command line) and try out some programs... :-)
Try starting Hatari with the option "--help" to find out more about its
command line parameters.


 3) Compiling and running
 ------------------------

First, you need the SDL library, you can get it at:
 http://www.libsdl.org
Of course, you need GNU-C and (GNU) Make, too!

Change to the src/ directory and adapt the Makefile to suite your mood
(and system of course). Then compile it by typing "make".
If all works fine, you'll get the executable "hatari". You can strip it
to save some disk space, if you don't want to debug it.
Then you'll have to copy a TOS ROM to the actual directory and rename it
to "tos.img".
**** Very important: Only TOS 1.00, 1.02 and 1.04 are supported ****
**** at the moment, TOS 2.06 and other do _not_ work!           ****


That's all for the moment. If you want to help me working on Hatari,
write me an e-mail:
  thothy@gmx.net

The newest version of Hatari can be found at:
  http://sourceforge.net/projects/hatari/


May the fun without the price to be with you ;-) !
