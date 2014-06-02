
Here are examples of using Native Features:
  natfeats.c -- C-code for calling native features
  natfeats.h -- header for assembly- & C-code
  nf_asma.s -- assembly helper code for AHCC
  nf_asmg.s -- assembly helper code for GCC / Gas
  nf_asmv.s -- assembly helper code for VBCC / Vasm
  Makefile* -- Makefiles for GCC & VBCC
  nf_ahcc.prj -- AHCC project file

If TEST is defined, natfeats.c includes main() and few additional
tests, with TEST undefined, you should be able to use these files
as-is in your own programs.
