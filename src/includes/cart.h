/*
  Hatari
*/

/* Workspace in our 'cart.s' internal program */
#define CART_HARDDRV          (0xfa1000)
#define CART_OLDGEMDOS        (0xfa1004)
#define CART_VDI_OPCODE_ADDR  (0xfa1008)
#define CART_GEMDOS           (0xfa100A)
#define CART_PEXEC_ADDR       (0xfa1018)
#define CART_HDV_ADDR_1       (0xFA1454+2)
#define CART_HDV_ADDR_2       (0xFA1486+2)
#define CART_RETURN           (0xFA1014)

/* This is set to 4, but should be 6 for TOS 1.2 on */
#define CART_PEXEC_TOS        (0xFA104D)
extern void Cart_LoadHeader(void);
extern void Cart_LoadImage(void);
extern void Cart_WriteHdvAddress(unsigned short int HdvAddress);
