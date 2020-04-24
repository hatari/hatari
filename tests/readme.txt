Tests
=====

Here's code for testing Hatari.  Most tests are run automatically with
"make test".


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

buserror/
- "make test" tests for IO memory addresses which cause bus errors
  on real machines

cpu/
- "make test" tests for few CPU instructions

cycles/
- "make test" tests for CPU cycles

debugger/
- "make test" test code & data for Hatari debugger.
  test-scripting.sh script for manual testing of debugger scripting

gemdos/
- "make test" test code for GEMDOS APIs used by GEMDOS HD emulation

keymap/
- test programs for finding out Atari and SDL keycodes needed in
  Hatari keymap files

natfeats/
- "make test" test for Native Features emulator interface, and
   example code for different compilers / assemblers on how to use it

serial/
- "make test" tests for Hatari serial interfaces

tosboot/
- tester for automatically running all (specified) TOS versions with
  relevant Hatari configurations to afterwards verify from produced
  screenshots that they they all booted fine.  And a script that
  compares the screenshots against earlier reference screenshots

xbios/
- "make test" tests for Hatari --bios-intercept facilities
