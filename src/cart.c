/*
  Hatari

  Cartridge Pexec program

  To load programs into memory, through TOS, we need to intercept GEMDOS so we can
  relocate/execute programs via GEMDOS call $4B(PExec).
  We have some 68000 assembler, located at 0xFA1000(cartridge memory), which is used as our
  new GEMDOS handler. This checks if we need to intercept the call.
  You'll notice PaCifiST has a 'cartridge' icon on screen, which contains a program called
  'Don't run me' - this is a similar routine used for loading.
  This assembler routine needs modifying according to the TOS image we have selected to ensure
  compatibility with the rest of TOS(TOS assumes the disc sector is read into a set workspace
  address)

  The assembler routine is called 'cart.s', and has been converted to a byte array and stored
  in 'cartimg.c'.
*/

#include "main.h"
#include "cart.h"
#include "decode.h"
#include "m68000.h"
#include "misc.h"
#include "stMemory.h"

#include "cartimg.c"                   /* Cartridge program used as inferface to PC system */


/*-----------------------------------------------------------------------*/
/*
  Load ST GEMDOS intercept program image into cartridge memory space
  This is used as an interface to the PC file system and for GemDOS
*/
void Cart_LoadImage(void)
{
  /* Copy 'cart.img' file into ST's cartridge memory */
  memcpy((char *)STRam+0xFA1000,cart_img,sizeof(cart_img));
}


/*-----------------------------------------------------------------------*/
/*
  Modify program loaded into cartridge memory to set where load sectors from disc image -
  this value MUST correspond with where the TOS version assumes it will be loaded

  We can find this by looking for hdv_boot function(see tos.cpp),move.l <addr>,$47A(a5)
  and then follow code for move.l #<value>,-(sp) and jsr <floprd>
*/
void Cart_WriteHdvAddress(unsigned short int HdvAddress)
{
  STMemory_WriteWord(CART_HDV_ADDR_1,HdvAddress);
  STMemory_WriteWord(CART_HDV_ADDR_2,HdvAddress);
}
