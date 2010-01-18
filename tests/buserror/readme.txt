
	Bus error test
	==============

The programs in this directory can be used to test the IO memory addresses
which cause a bus error on a real machine. "buserr_b.prg" tests the IO memory
with byte accesses, "buserr_w.prg" tests with word accesses.

The results will be written to a file called "BUSERR_B.TXT" or "BUSERR_W.TXT".
These can be used to compare the behaviour of the emulator with a real machine.

In the "results" folder, there are sample files that have been taken on real
machines. Note that the Falcon has two bus modes (STE compatible and normal),
so there are two sets of results for the Falcon available.
