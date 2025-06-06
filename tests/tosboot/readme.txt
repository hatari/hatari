Files
-----

Tools and data files:
Makefile      -- builds the test program and floppy & HD images
                 containing it
disk/*        -- sources, binaries and input files for test program,
                 directory is also used for GEMDOS HD emu testing
floppy/*      -- files to autostart test program from floppy
tos_tester.py -- test driver, described below

Generated test programs:
GEMDOS.PRG    -- test program GEMDOS HD
MINIMAL.PRG   -- test program for other drive types, i.e. ones where
                 output file contents are not easily accessible

Generated drive image files:
bootauto.st.gz -- floppy image with the test files and test program
                  run from AUTO/-folder, for TOS <1.04
bootdesk.st.gz -- floppy image with the test files and test program
                  run from *.INF desktop file
hd.img         -- HD image with the the test program / files
                  and a DOS partition table

Other generated files:
blank-a.st.gz  -- blank floppy image to avoid TOS disk dialogs
dummy.cfg      -- Hatari config file generated by tos_tester.py
output/*       -- Test report and screenshots, temporary output files

Other files and directories:
readme.txt -- this text file
tos/*      -- TOSDIR in Makefile points here for your TOS images

There's also "screenshot-report.sh" script to generate a HTML report
out of the screenshots saved by TOS tester which will list missing
tests and any differences in the produced screenshots.  For that, you
need "reference" directory to contain screenshots from an earlier,
successful run of TOS tester.


Usage
-----

NOTE: TOS tester works *only* with Hatari versions that are built with
raw MIDI device support, NOT with PortMidi support.  This is because
TOS tester communicates to the emulated test programs through FIFO
file given to Hatari as MIDI input/output file, whereas PortMidi
allows communication only to real MIDI devices.

If you want to test Hatari version that isn't in your PATH,
you need to give PATH for the Hatari binary you want to test,
like this:
	PATH=../../build/src:$PATH make

Before running that, tos/ subdirectory should have (symbolic links to)
TOS images you want to test, at least EmuTOS etos1024k.img image.
Or add this to above command:
	TOSDIR=<path to TOS images dir>

Alternatively, you can call the TOS tester directly and specify
the TOS images it should test:
	PATH=../../build/src:$PATH ./tos_tester.py <TOS images>

To view the produced screenshots, either use ImageMagick:
	display output/*.png

Or use the script that creates a HTML page with them and opens
browser to view it.


What TOS tester tests
---------------------

These are the HW configuration combinations that TOS tester currently
supports:

* ST, MegaST, STE, MegaSTE, TT and Falcon machine types

  - EmuTOS 512k & 1024k are tested for all the machine types
  - EmuTOS 256k & TOS v2.x are tested with all machine types,
    except TT / Falcon
  - Rest of TOSes are tested only with a single machine type

* TV, VGA, RGB and monochrome monitors and 1, 2 & 4 plane VDI modes

* Different amounts of ST-RAM from 0 (0.5 MiB) to 14 MiB

* Different amounts of TT-RAM from 0 to 1024 MiB

* With and without GEMDOS harddisk directory emulation

  Test program is started either from a floppy or an emulated GEMDOS
  HD (directory) using *.INF file, or in case of TOS v1.00 - 1.02,
  from floppy auto/-folder.  GEMDOS HD testing is done with more
  extensive gemdos.prg test program, floppy testing with minimal.prg
  test program which doesn't change the floppy content (to avoid its
  repository file update).

* ACSI, IDE and SCSI interface testing with EmuTOS

* Arbitrary boolean Hatari command line options specified
  with the "--bool" option

You can use the command line options to specify which set of these
is used and TOS tester will go through all combinations of them.

See "tos_tester.py -h" output for examples.


What to test
------------

For each Hatari release it would be good to test e.g. the following
TOS versions:
  v1.00 de, v1.02 de, v1.04 de, v1.04 us, v1.62 de, v1.62 us,
  v2.06 de, v3.06 us, v4.04, etos192k, etos1024k[1]

[1] Just the latest release of EmuTOS.

And following monitor configurations:
  ST:     tv,  mono, vdi-1, vdi-4
  STE:    rgb, mono, vdi-1, vdi-4
  TT:     vga, mono, vdi-1, vdi-4
  Falcon: rgb, mono, vga

Memory configurations:
  ST:   0.5 &  2 MB
  STE:    1 &  4 MB
  TT:     2 & 10 MB ST-RAM, 0 & 32 MB TT-RAM, MMU on/off
  Falcon: 4 & 14 MB ST-RAM, 0 & 32 MB TT-RAM, MMU on/off

And both with GEMDOS HD and just floppy.  For EmuTOS, also ACSI (with
ST/STe/TT), IDE (with Falcon) and SCSI (with Falcon/TT).

Note that it's enough to give the whole set of HW configurations to
TOS tester, it will automatically select a suitable subset of HW
combinations to test, for each given TOS versions.

Unless you're specifically testing FDC timings or memory detection
compatibility, you can use --fast option to speed up TOS bootup a lot.


Potential TODOs
---------------

If all TOS versions support it, extend GEMDOS test program to test also:
* starting another program
* file redirection

Testing a HD disk having also MiNT, with and without MMU.

Current "screenshot-report.sh" script assumes that Hatari will always
create identical screenshots for the same screen content.  This might
not be true if underlying libpng gets updated, so it would be better
to have some e.g. SDL (pygame?) program that loads two images,
compares their uncompressed content and either reports that, or shows
the difference.


Discarded ideas
---------------

Add testing of ASCI, IDE and SCSI drives with normal TOS in addition
to the GEMDOS HD and floppy tests.

This isn't very straightforward because both need different drivers
and therefore different disk images and the drivers either have issues
with e.g. EmuTOS, or don't support all machines.  Formatting and
installing the drivers requires using interactive Atari programs,
so these images cannot be automatically (re-)generated.

(EmuTOS supports ACSI, IDE and SCSI directly, without any need for
drivers. Both HDs with DOS (not Atari) partition table, and ones
without partition table at all.  That's why it can be already tested.)

-

Machine type specific test programs e.g. for:
* ST color resolution overscan
* STE blitter and overscan
* TT FPU operations, could output e.g. speed
* Falcon DSP operations

Such programs also needs to have some static screen which doesn't
automatically advance so that a screenshot can be taken of it.
Alternatively, test program could be accompanied with debugger
script(s) that stop the program at suitable point and take a
screenshot.

Tester for DMA sound output and comparison for the produced sound.

IMHO these belong more to the regular Atari tests run with "make test".
