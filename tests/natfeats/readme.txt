Contents:
- What is NatFeats
- Test / example files


WHAT IS NATFEATS

NatFeats is Native Features interface for Atari programs for calling
emulator functionality.  It originates from Aranym, and is specified
here:
  https://github.com/aranym/aranym/wiki/natfeats-proposal

These features range from simple things like:
- Toggling Hatari fast-forward mode on/off
- Outputting strings (NF_STDERR) and invoking debugger (NF_DEBUGGER)
  (which together can be used to implement asserts)
- Exiting emulator with return value (NF_EXIT)

To complex device drivers.

Individual feature APIs are (partially) documented here:
	https://github.com/aranym/aranym/wiki/natfeats-nfapi

What features are implemented by emulators can differ, so application
needs to check for availability of each of the features before using
them.  Ready-to-use example code for accessing them is listed below.


TEST / EXAMPLE FILES

Here are examples of using the simpler Native Features:
  natfeats.c -- C-code for calling native features
  natfeats.h -- header for assembly- & C-code
  nf_asma.s -- assembly helper code for AHCC
  nf_asmg.s -- assembly helper code for GCC / Gas
  nf_asmv.s -- assembly helper code for VBCC / Vasm
  Makefile* -- Makefiles for GCC & VBCC
  nf_ahcc.prj -- AHCC project file

They're also used in Hatari's automated tests.

If TEST is defined, natfeats.c includes main() and few additional
tests, with TEST undefined, you should be able to use these files
as-is in your own programs.
