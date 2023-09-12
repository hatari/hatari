Tests
=====

Here's code for testing Hatari.

Content:
* Running tests
* Building tests
* Tests files
* Test subdirectories


Running tests
-------------

Most tests are run automatically with "make test".

Individual tests can be run (with verbose output) like this:
$ ctest -V -R command-fifo

Or a group of test:
$ ctest -V -R 'serial-scc-.*'

To speed up test runs and avoid external dependencies, tests use
"--tos none", which tells Hatari to use a "fake" TOS with no features.


Building tests
--------------

Easiest way to re-build modified C-code test binaries
(when the code and AHCC are in same directory):
$ hatari-prg-args -m --cpulevel 3 --trace os_base -- ahcc_p.ttp mfp_ser.prj


Test files
----------

check-bashisms.sh -- "make test" tests for scripts POSIX shell syntax
configfile.sh -- "make test" tests for Hatari config file load / save
startup.s -- minimal startup code for tests built with AHCC

(AHCC builds smallest / simplest test binaries, but its startup code
still needs to be replaced to work with the --tos none option.)


Test subdirectories
-------------------

Test subdirectories for Hatari and the emulated Atari machines:

autostart/
- trivial AUTO/-folder binaries for (manually) slowing down startup.
  Allows testing whether problems in autostarted programs are due to
  TOS starting them too fast for all TOS facilities to be present

blitter/
- "make test" tests for different blitter combinations of xcount
  and nfsr

buserror/
- "make test" tests for IO memory addresses which cause bus errors
  on real machines

cpu/
- "make test" tests for few CPU instructions

cycles/
- "make test" tests for CPU cycles

debugger/
- "make test" test code & data for Hatari debugger.
  test-scripting.sh is script for manual testing of debugger scripting

gemdos/
- "make test" test code for GEMDOS APIs used by GEMDOS HD emulation

keymap/
- test programs for finding out Atari and SDL keycodes needed in
  Hatari keymap files

natfeats/
- "make test" test for Native Features emulator interface, and
   example code for different compilers / assemblers on how to use it

mem_end/
- "make test" tests handling of screen at end of RAM

screen/
- "make test" tests for a fullscreen demo

serial/
- "make test" tests for Hatari (MFP/SCC/MIDI) serial interfaces

tosboot/
- Tester for automatically running all (specified) TOS versions with
  relevant Hatari configurations and for checking basic device and
  GEMDOS functionality. From screenshots saved at end of each test,
  one can manually verify that they all really booted up fine. There's
  also a script for comparing the screenshots against earlier
  reference screenshots

unit/
- Few unit tests for file.c functionality

xbios/
- "make test" tests for Hatari --bios-intercept facilities
