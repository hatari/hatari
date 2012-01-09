
Usage
-----

If you want to test Hatari version that isn't in your PATH,
you need to give PATH for the Hatari binary you want to test,
like this:
	PATH=../../build/src:$PATH ./tos-tester.py <TOS images>

To view the produced screenshots, either use ImageMagick:
	display *.png

Or use the script that creates a HTML page with them and opens
browser to view it.


What TOS tester tests
---------------------

These are the HW configurations combinations that TOS tester currently
supports:

* ST, STE, TT, Falcon machine types

  EmuTOS 512k is tested for all the machine types, EmuTOS 192/256k and
  TOS v2.x with all except Falcon, rest of TOSes are tested only with
  a single machine type.

* TV, VGA, RGB and monochrome monitors and 1 & 4 plane VDI modes

  ST:     tv,  mono, vdi-1, vdi-4
  STE:    rgb, mono, vdi-1, vdi-4
  TT:     vga, mono, vdi-1, vdi-4
  Falcon: rgb, mono, vga

* Different amounts of memory

  ST:   0.5 &  2 MB
  STE:    1 &  4 MB
  TT:     2 & 10 MB
  Falcon: 4 & 14 MB

* With and without GEMDOS harddisk directory emulation


What to test
------------

For each Hatari release it would be good to test e.g. the following
TOS versions:
  v1.00 de, v1.02 de, v1.04 de, v1.04 us, v1.62 de, v1.62 us,
  v2.06 de, v3.06 us, v4.04, etos192k, etos512k[1]

[1] Just the latest release of EmuTOS.

This should give good enough coverage of all the possible bootup
issues.


TODO
----

* Don't use timeout and screenshot, but something more robust:

With GEMDOS HD emulation, auto-run some Atari program that writes to
a predefined file, which in actuality is a FIFO that the tos-tester
listens on.  If there's no write within some long timeout, the tester
would conclude boot to have failed.

Otherwise, redirect MIDI or RS232 to a file and boot a floppy disk
with an autostarted program that writes something to the corresponding
interface.

(Both need to be tested as GEMDOS HD emulation could influence the
behaviour of the emulation quite a bit since it uses a cartridge and
trap-bending mechanisms internally.)


* Command line options to specify what exact tests are run (memory,
graphics, arbitrary Hatari option) and to specify full of range
additional values for the tests:
	--test mem,gfx --graphics vga,vdi-1,vdi-2 --memory 0,2,8 \
	--test-bool "--fast-boot" --test-bool "--compatible"
