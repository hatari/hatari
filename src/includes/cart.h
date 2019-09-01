/*
  Hatari - cart.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

/* Workspace in our 'cart_asm.s' internal program */
#define CART_VDI_OPCODE_ADDR  0xfa0024

extern void Cart_ResetImage(void);
extern void Cart_PatchCpuTables(void);

