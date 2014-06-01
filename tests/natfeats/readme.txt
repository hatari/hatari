
Here are examples of using Native Features:
  natfeats.c -- C-code for calling native features
  natfeats.h -- header for assembly- & C-code
  nf_asmv.s -- assembly helper code for VBCC / Vasm
  nf_asmg.s -- assembly helper code for GCC / Gas
  Makefile* -- Makefiles for different compilers

If TEST is defined, natfeats.c includes main() and few additional
tests, with TEST undefine, you should be able to use these files
as-is in your own programs.


As to using this example code with AHCC compiler, AHCC passes
arguments partly in registers, so for that either:
- the assembly code would need to be modified to push the args
  from regs to stacks, or
- ASM function prototypes would need to be marked to use
  C calling convention (stack)

Note also that AHCC doesn't prepend '_' to C-code symbols so
symbols exported from AHCC ASM shouldn't have those.
