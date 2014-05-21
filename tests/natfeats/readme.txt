
Here's example of using Native Features from VBCC:
  natfeats.c -- native features calling C-code
  natfeats.h -- header for assembly & C-code
  nf_asmv.s -- assembly helper code for VBCC / Vasm
  nf_asm.s -- assembly helper code for GCC / Gas (NOT TESTED!)

As to AHCC compiler, AHCC passes arguments partly in registers,
so for that either:
- the assembly code would need to be modified to push the args
  from regs to stacks, or
- ASM function prototypes would need to be marked to use
  C calling convention (stack)

Note also that AHCC doesn't prepend '_' to C-code symbols so
symbols exported from AHCC ASM shouldn't have those.
