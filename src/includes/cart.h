/*
  Hatari
*/

/* Workspace in our 'cart.s' internal program */
#define  CART_HARDDRV         (0xfa1000)
#define  CART_OLDGEMDOS       (0xfa1000+4)
#define  CART_VDI_OPCODE_ADDR (0xfa1000+8)
#define  CART_GEMDOS          (0xfa1000+10)

#define CART_HDV_ADDR_1       (0xFA1454+2)
#define CART_HDV_ADDR_2       (0xFA1486+2)

extern void Cart_LoadImage(void);
extern void Cart_WriteHdvAddress(unsigned short int HdvAddress);
