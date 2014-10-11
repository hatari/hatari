Tests
-----

Subdirectories contains tests for Hatari and the emulated Atari machines:

buserror/
- tests for IO memory addresses which cause bus errors on real machines

debugger/
- test code & data for Hatari debugger and its scripting facilities
  (see the Makefile and tests-scripting.sh files for more info)

keymap/
- test programs for finding out Atari and SDL keycodes needed in
  Hatari keymap files

natfeats/
- test program and example code for different compilers / assemblers
  on how to use Native Features emulator interface

tosboot/
- tester for automatically running all (specified) TOS versions with
  relevant Hatari configurations to afterwards verify from produced
  screenshots that they they all booted fine.  And a script that
  compares the screenshots against earlier reference screenshots
