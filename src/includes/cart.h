/*
  Hatari - cart.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

/* Workspace in our 'cart_asm.s' internal program */
#define CART_OLDGEMDOS        (0xfa1004)
#define CART_VDI_OPCODE_ADDR  (0xfa1008)
#define CART_GEMDOS           (0xfa100a)

extern void Cart_LoadImage(void);
