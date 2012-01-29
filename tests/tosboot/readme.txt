
Usage
-----

If you want to test Hatari version that isn't in your PATH,
you need to give PATH for the Hatari binary you want to test,
like this:
	PATH=../../build/src:$PATH ./tos_tester.py <TOS images>

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

* Different amounts of memory

* With and without GEMDOS harddisk directory emulation

* Arbitrary boolean Hatari command line options

You can use the command line options to specify which set of these
is used and TOS tester will go through all combinations of them.


What to test
------------

For each Hatari release it would be good to test e.g. the following
TOS versions:
  v1.00 de, v1.02 de, v1.04 de, v1.04 us, v1.62 de, v1.62 us,
  v2.06 de, v3.06 us, v4.04, etos192k, etos512k[1]

[1] Just the latest release of EmuTOS.

And following monitor configurations:

  ST:     tv,  mono, vdi-1, vdi-4
  STE:    rgb, mono, vdi-1, vdi-4
  TT:     vga, mono, vdi-1, vdi-4
  Falcon: rgb, mono, vga

And memory configurations:
  ST:   0.5 &  2 MB
  STE:    1 &  4 MB
  TT:     2 & 10 MB
  Falcon: 4 & 14 MB

In addition to testing with and without gemdos HD emulation.

This should give good enough coverage of all the possible bootup
issues.


TODO
----

Testing & examples.

Add testing of floppies, ASCI and IDE drives in addition to the GEMDOS
HD test.  They need to be tested as GEMDOS HD emulation could
influence the behaviour of the emulation quite a bit since it uses a
cartridge and trap-bending mechanisms internally and because ASCI and
IDE work with different machines.

Testing app could check several different things and report their
success e.g. through the emulated printer port to a file.
