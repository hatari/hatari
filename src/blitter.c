/**
 * Hatari - Blitter emulation.
 * This file has been taken from STonX.
 *
 * Original information text follows:
 *
 *
 * This file is part of STonX, the Atari ST Emulator for Unix/X
 * ============================================================
 * STonX is free software and comes with NO WARRANTY - read the file
 * COPYING for details
 *
 *  Blitter Emulator,
 *  Martin Griffiths, 1995/96.
 *  
 *  Here lies the Atari Blitter Emulator - The 'Blitter' chip is found in  
 *  the STE/MegaSTE and provides a very fast BitBlit in hardware.
 *
 *  The hardware registers for this chip lie at addresses $ff8a00 - $ff8a3c,
 *  There seems to be a mirror for $ff8a30 used in TOS 1.02 at $ff7f30.
 *  
 */

#include <SDL_types.h>
#include <stdio.h>
#include <stdlib.h>

#include "blitter.h"
#include "hatari-glue.h"
#include "main.h"
#include "stMemory.h"


/* The following typedefs are needed for STonX's code */
typedef Uint16  UW;
typedef Uint8 UB;
typedef char  B;

/* The following functions are needed for STonX's code: */
#define MEM(a) (a)
#define ADDR(a) (a)

#define LM_W(a) ((short)STMemory_ReadWord(a))
#define LM_UW(a) STMemory_ReadWord(a)
#define LM_UL(a) STMemory_ReadLong(a)

#define SM_B(a,v) STMemory_WriteByte(a,v)
#define SM_UW(a,v) STMemory_WriteWord(a,v)
#define SM_UL(a,v) STMemory_WriteLong(a,v)

#if 0
#define SHOWPARAMS  \
{  \
  fprintf(stderr,"Source Address:%X\n",source_addr);  \
  fprintf(stderr,"  Dest Address:%X\n",dest_addr);  \
  fprintf(stderr,"       X count:%X\n",x_count);  \
  fprintf(stderr,"       Y count:%X\n",y_count);  \
  fprintf(stderr,"  Source X inc:%X\n",source_x_inc);  \
  fprintf(stderr,"    Dest X inc:%X\n",dest_x_inc);  \
  fprintf(stderr,"  Source Y inc:%X\n",source_y_inc);  \
  fprintf(stderr,"    Dest Y inc:%X\n",dest_y_inc);  \
  fprintf(stderr,"HOP:%2X    OP:%X\n",hop,op);  \
  fprintf(stderr,"   source SKEW:%X\n",skewreg);  \
  fprintf(stderr,"     endmask 1:%X\n",end_mask_1); \
  fprintf(stderr,"     endmask 2:%X\n",end_mask_2); \
  fprintf(stderr,"     endmask 3:%X\n",end_mask_3); \
  fprintf(stderr,"       linenum:%X\n",line_num);  \
  if (NFSR) fprintf(stderr,"NFSR is Set!\n");  \
  if (FXSR) fprintf(stderr,"FXSR is Set!\n");  \
}
#endif


static UW halftone_ram[16];
static UW end_mask_1,end_mask_2,end_mask_3;
static UB NFSR,FXSR; 
static UW x_count,y_count;
static UB hop,op,line_num,skewreg;
static unsigned int dest_addr_reg=0;
static int halftone_curroffset,halftone_direction;
static int source_x_inc, source_y_inc, dest_x_inc, dest_y_inc;
static int blit_flag = FALSE;


void load_halftone_ram(void)
{
  halftone_ram[0] = LM_UW(MEM(0xff8a00)); 
  halftone_ram[1] = LM_UW(MEM(0xff8a02)); 
  halftone_ram[2] = LM_UW(MEM(0xff8a04)); 
  halftone_ram[3] = LM_UW(MEM(0xff8a06)); 
  halftone_ram[4] = LM_UW(MEM(0xff8a08)); 
  halftone_ram[5] = LM_UW(MEM(0xff8a0a)); 
  halftone_ram[6] = LM_UW(MEM(0xff8a0c)); 
  halftone_ram[7] = LM_UW(MEM(0xff8a0e)); 
  halftone_ram[8] = LM_UW(MEM(0xff8a10)); 
  halftone_ram[9] = LM_UW(MEM(0xff8a12)); 
  halftone_ram[10] = LM_UW(MEM(0xff8a14));  
  halftone_ram[11] = LM_UW(MEM(0xff8a16));  
  halftone_ram[12] = LM_UW(MEM(0xff8a18));  
  halftone_ram[13] = LM_UW(MEM(0xff8a1a));  
  halftone_ram[14] = LM_UW(MEM(0xff8a1c));  
  halftone_ram[15] = LM_UW(MEM(0xff8a1e));  
  if (line_num & 0x20)          
    halftone_curroffset = skewreg & 15;     
  else              
    halftone_curroffset = line_num & 15;    
  if (dest_y_inc >= 0)          
    halftone_direction = 1;       
  else              
    halftone_direction = -1;      

}


#define HOP_OPS(_fn_name,_op,_do_source_shift,_get_source_data,_shifted_hopd_data, _do_halftone_inc) \
static void _fn_name (void)  \
{  \
  register int source_addr  = LM_UL(MEM(0xff8a24));   \
  register int dest_addr = dest_addr_reg;  \
  register unsigned int skew = (unsigned int) skewreg & 15;  \
  register unsigned int source_buffer=0;  \
  if(address_space_24)  \
    { source_addr&=0x0ffffff; dest_addr&=0x0ffffff; }  \
  source_x_inc = (int) LM_W(MEM(0xff8a20));  \
  source_y_inc = (int) LM_W(MEM(0xff8a22));  \
  dest_x_inc = (int) LM_W(MEM(0xff8a2e));  \
  dest_y_inc = (int) LM_W(MEM(0xff8a30));  \
  if (hop & 1) load_halftone_ram();  \
  do  \
  {  \
    register UW x,dst_data,opd_data;  \
    if (FXSR)  \
    {  \
      _do_source_shift;  \
      _get_source_data;  \
      source_addr += source_x_inc;  \
    }  \
    _do_source_shift;  \
    _get_source_data;  \
    dst_data = LM_UW(ADDR(dest_addr));  \
    opd_data =  _shifted_hopd_data;  \
    SM_UW(ADDR(dest_addr),(dst_data & ~end_mask_1) | (_op & end_mask_1));  \
    for(x=0 ; x<x_count-2 ; x++)  \
    {  \
      source_addr += source_x_inc;  \
      dest_addr += dest_x_inc;  \
      _do_source_shift;  \
      _get_source_data;  \
      dst_data = LM_UW(ADDR(dest_addr));  \
      opd_data = _shifted_hopd_data;  \
      SM_UW(ADDR(dest_addr),(dst_data & ~end_mask_2) | (_op & end_mask_2));  \
    }  \
    if (x_count >= 2)  \
    {  \
      dest_addr += dest_x_inc;  \
      _do_source_shift;  \
      if ( (!NFSR) || ((~(0xffff>>skew)) > end_mask_3) )  \
      {  \
        source_addr += source_x_inc;  \
        _get_source_data;  \
      }  \
      dst_data = LM_UW(ADDR(dest_addr));  \
      opd_data = _shifted_hopd_data;  \
      SM_UW(ADDR(dest_addr),(((UW)dst_data) & ~end_mask_3) | (_op & end_mask_3));  \
    }  \
    source_addr += source_y_inc;  \
    dest_addr += dest_y_inc;  \
    _do_halftone_inc;  \
  } while (--y_count > 0);  \
  SM_UL(MEM(0xff8a24), source_addr);  \
  dest_addr_reg = dest_addr;  \
}


HOP_OPS(_HOP_0_OP_00_N,(0), source_buffer >>=16,;, 0xffff, ;)
HOP_OPS(_HOP_0_OP_01_N,(opd_data & dst_data) ,source_buffer >>=16,; , 0xffff, ;)
HOP_OPS(_HOP_0_OP_02_N,(opd_data & ~dst_data) ,source_buffer >>=16,; , 0xffff,;)   
HOP_OPS(_HOP_0_OP_03_N,(opd_data) ,source_buffer >>=16,; , 0xffff,;)
HOP_OPS(_HOP_0_OP_04_N,(~opd_data & dst_data) ,source_buffer >>=16,;, 0xffff,;)
HOP_OPS(_HOP_0_OP_05_N,(dst_data) ,source_buffer >>=16,;, 0xffff, ;)
HOP_OPS(_HOP_0_OP_06_N,(opd_data ^ dst_data) ,source_buffer >>=16,;, 0xffff, ;)
HOP_OPS(_HOP_0_OP_07_N,(opd_data | dst_data) ,source_buffer >>=16,; , 0xffff, ;)
HOP_OPS(_HOP_0_OP_08_N,(~opd_data & ~dst_data) ,source_buffer >>=16,;, 0xffff, ;)
HOP_OPS(_HOP_0_OP_09_N,(~opd_data ^ dst_data) ,source_buffer >>=16,;, 0xffff, ;)
HOP_OPS(_HOP_0_OP_10_N,(~dst_data) ,source_buffer >>=16,;, 0xffff, ;)
HOP_OPS(_HOP_0_OP_11_N,(opd_data | ~dst_data) ,source_buffer >>=16,;, 0xffff, ;)
HOP_OPS(_HOP_0_OP_12_N,(~opd_data) ,source_buffer >>=16,;, 0xffff, ;)
HOP_OPS(_HOP_0_OP_13_N,(~opd_data | dst_data) ,source_buffer >>=16,;, 0xffff, ;)   
HOP_OPS(_HOP_0_OP_14_N,(~opd_data | ~dst_data) ,source_buffer >>=16,;, 0xffff, ;)
HOP_OPS(_HOP_0_OP_15_N,(0xffff) ,source_buffer >>=16,;, 0xffff, ;)

HOP_OPS(_HOP_1_OP_00_N,(0) ,source_buffer >>=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_01_N,(opd_data & dst_data) ,source_buffer >>=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_02_N,(opd_data & ~dst_data) ,source_buffer >>=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_03_N,(opd_data) ,source_buffer >>=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_04_N,(~opd_data & dst_data) ,source_buffer >>=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_05_N,(dst_data) ,source_buffer >>=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_06_N,(opd_data ^ dst_data) ,source_buffer >>=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_07_N,(opd_data | dst_data) ,source_buffer >>=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_08_N,(~opd_data & ~dst_data) ,source_buffer >>=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_09_N,(~opd_data ^ dst_data) ,source_buffer >>=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_10_N,(~dst_data) ,source_buffer >>=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_11_N,(opd_data | ~dst_data) ,source_buffer >>=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_12_N,(~opd_data) ,source_buffer >>=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_13_N,(~opd_data | dst_data) ,source_buffer >>=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_14_N,(~opd_data | ~dst_data) ,source_buffer >>=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_15_N,(0xffff) ,source_buffer >>=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )

HOP_OPS(_HOP_2_OP_00_N,(0) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_01_N,(opd_data & dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_02_N,(opd_data & ~dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_03_N,(opd_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_04_N,(~opd_data & dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_05_N,(dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_06_N,(opd_data ^ dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_07_N,(opd_data | dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_08_N,(~opd_data & ~dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_09_N,(~opd_data ^ dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_10_N,(~dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_11_N,(opd_data | ~dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_12_N,(~opd_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_13_N,(~opd_data | dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_14_N,(~opd_data | ~dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_15_N,(0xffff) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew),;)

HOP_OPS(_HOP_3_OP_00_N,(0) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_01_N,(opd_data & dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_02_N,(opd_data & ~dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_03_N,(opd_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_04_N,(~opd_data & dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_05_N,(dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_06_N,(opd_data ^ dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_07_N,(opd_data | dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_08_N,(~opd_data & ~dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_09_N,(~opd_data ^ dst_data) , source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_10_N,(~dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_11_N,(opd_data | ~dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_12_N,(~opd_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_13_N,(~opd_data | dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_14_N,(~opd_data | ~dst_data) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15) 
HOP_OPS(_HOP_3_OP_15_N,(0xffff) ,source_buffer >>=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) << 16) ,(source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)


HOP_OPS(_HOP_0_OP_00_P,(0) ,source_buffer <<=16,;, 0xffff,;)
HOP_OPS(_HOP_0_OP_01_P,(opd_data & dst_data) ,source_buffer <<=16,;, 0xffff,;)  
HOP_OPS(_HOP_0_OP_02_P,(opd_data & ~dst_data) ,source_buffer <<=16,;, 0xffff,;) 
HOP_OPS(_HOP_0_OP_03_P,(opd_data) ,source_buffer <<=16,;, 0xffff,;) 
HOP_OPS(_HOP_0_OP_04_P,(~opd_data & dst_data) ,source_buffer <<=16,;, 0xffff,;) 
HOP_OPS(_HOP_0_OP_05_P,(dst_data) ,source_buffer <<=16,;, 0xffff,;) 
HOP_OPS(_HOP_0_OP_06_P,(opd_data ^ dst_data) ,source_buffer <<=16,;, 0xffff,;)  
HOP_OPS(_HOP_0_OP_07_P,(opd_data | dst_data) ,source_buffer <<=16,;, 0xffff,;)  
HOP_OPS(_HOP_0_OP_08_P,(~opd_data & ~dst_data) ,source_buffer <<=16,;, 0xffff,;)  
HOP_OPS(_HOP_0_OP_09_P,(~opd_data ^ dst_data) ,source_buffer <<=16,;, 0xffff,;) 
HOP_OPS(_HOP_0_OP_10_P,(~dst_data) ,source_buffer <<=16,;, 0xffff,;)  
HOP_OPS(_HOP_0_OP_11_P,(opd_data | ~dst_data) ,source_buffer <<=16,;, 0xffff,;) 
HOP_OPS(_HOP_0_OP_12_P,(~opd_data) ,source_buffer <<=16,;, 0xffff,;)  
HOP_OPS(_HOP_0_OP_13_P,(~opd_data | dst_data) ,source_buffer <<=16,;, 0xffff,;) 
HOP_OPS(_HOP_0_OP_14_P,(~opd_data | ~dst_data) ,source_buffer <<=16,;, 0xffff,;)  
HOP_OPS(_HOP_0_OP_15_P,(0xffff) ,source_buffer <<=16,;, 0xffff,;) 

HOP_OPS(_HOP_1_OP_00_P,(0) ,source_buffer <<=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_01_P,(opd_data & dst_data) ,source_buffer <<=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_02_P,(opd_data & ~dst_data) ,source_buffer <<=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_03_P,(opd_data) ,source_buffer <<=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_04_P,(~opd_data & dst_data) ,source_buffer <<=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_05_P,(dst_data) ,source_buffer <<=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_06_P,(opd_data ^ dst_data) ,source_buffer <<=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_07_P,(opd_data | dst_data) ,source_buffer <<=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_08_P,(~opd_data & ~dst_data) ,source_buffer <<=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )\
HOP_OPS(_HOP_1_OP_09_P,(~opd_data ^ dst_data) ,source_buffer <<=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_10_P,(~dst_data) ,source_buffer <<=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_11_P,(opd_data | ~dst_data) ,source_buffer <<=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_12_P,(~opd_data) ,source_buffer <<=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_13_P,(~opd_data | dst_data) ,source_buffer <<=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_14_P,(~opd_data | ~dst_data) ,source_buffer <<=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_15_P,(0xffff) ,source_buffer <<=16,;,halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )

HOP_OPS(_HOP_2_OP_00_P,(0) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_01_P,(opd_data & dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_02_P,(opd_data & ~dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_03_P,(opd_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_04_P,(~opd_data & dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_05_P,(dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_06_P,(opd_data ^ dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_07_P,(opd_data | dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_08_P,(~opd_data & ~dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_09_P,(~opd_data ^ dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_10_P,(~dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_11_P,(opd_data | ~dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ) , (source_buffer >> skew),;)

HOP_OPS(_HOP_2_OP_12_P,(~opd_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_13_P,(~opd_data | dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_14_P,(~opd_data | ~dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_15_P,(0xffff) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ) , (source_buffer >> skew),;)

HOP_OPS(_HOP_3_OP_00_P,(0) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ), (source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_01_P,(opd_data & dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ), (source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_02_P,(opd_data & ~dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ), (source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_03_P,(opd_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ), (source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_04_P,(~opd_data & dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ), (source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_05_P,(dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ), (source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_06_P,(opd_data ^ dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ), (source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_07_P,(opd_data | dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ), (source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_08_P,(~opd_data & ~dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ), (source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_09_P,(~opd_data ^ dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ), (source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_10_P,(~dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ), (source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_11_P,(opd_data | ~dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ), (source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_12_P,(~opd_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ), (source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_13_P,(~opd_data | dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ), (source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_14_P,(~opd_data | ~dst_data) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ), (source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_15_P,(0xffff) ,source_buffer <<=16,source_buffer |= ((unsigned int) LM_UW(ADDR(source_addr)) ), (source_buffer >> skew) & halftone_ram[halftone_curroffset],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 

static void (*do_hop_op_N[4][16])(void) =
{
  { _HOP_0_OP_00_N, _HOP_0_OP_01_N, _HOP_0_OP_02_N, _HOP_0_OP_03_N, _HOP_0_OP_04_N, _HOP_0_OP_05_N, _HOP_0_OP_06_N, _HOP_0_OP_07_N, _HOP_0_OP_08_N, _HOP_0_OP_09_N, _HOP_0_OP_10_N, _HOP_0_OP_11_N, _HOP_0_OP_12_N, _HOP_0_OP_13_N, _HOP_0_OP_14_N, _HOP_0_OP_15_N,},
  { _HOP_1_OP_00_N, _HOP_1_OP_01_N, _HOP_1_OP_02_N, _HOP_1_OP_03_N, _HOP_1_OP_04_N, _HOP_1_OP_05_N, _HOP_1_OP_06_N, _HOP_1_OP_07_N, _HOP_1_OP_08_N, _HOP_1_OP_09_N, _HOP_1_OP_10_N, _HOP_1_OP_11_N, _HOP_1_OP_12_N, _HOP_1_OP_13_N, _HOP_1_OP_14_N, _HOP_1_OP_15_N,},
  { _HOP_2_OP_00_N, _HOP_2_OP_01_N, _HOP_2_OP_02_N, _HOP_2_OP_03_N, _HOP_2_OP_04_N, _HOP_2_OP_05_N, _HOP_2_OP_06_N, _HOP_2_OP_07_N, _HOP_2_OP_08_N, _HOP_2_OP_09_N, _HOP_2_OP_10_N, _HOP_2_OP_11_N, _HOP_2_OP_12_N, _HOP_2_OP_13_N, _HOP_2_OP_14_N, _HOP_2_OP_15_N,},
  { _HOP_3_OP_00_N, _HOP_3_OP_01_N, _HOP_3_OP_02_N, _HOP_3_OP_03_N, _HOP_3_OP_04_N, _HOP_3_OP_05_N, _HOP_3_OP_06_N, _HOP_3_OP_07_N, _HOP_3_OP_08_N, _HOP_3_OP_09_N, _HOP_3_OP_10_N, _HOP_3_OP_11_N, _HOP_3_OP_12_N, _HOP_3_OP_13_N, _HOP_3_OP_14_N, _HOP_3_OP_15_N,}
};

static void (*do_hop_op_P[4][16])(void) =
{
  { _HOP_0_OP_00_P, _HOP_0_OP_01_P, _HOP_0_OP_02_P, _HOP_0_OP_03_P, _HOP_0_OP_04_P, _HOP_0_OP_05_P, _HOP_0_OP_06_P, _HOP_0_OP_07_P, _HOP_0_OP_08_P, _HOP_0_OP_09_P, _HOP_0_OP_10_P, _HOP_0_OP_11_P, _HOP_0_OP_12_P, _HOP_0_OP_13_P, _HOP_0_OP_14_P, _HOP_0_OP_15_P,},
  { _HOP_1_OP_00_P, _HOP_1_OP_01_P, _HOP_1_OP_02_P, _HOP_1_OP_03_P, _HOP_1_OP_04_P, _HOP_1_OP_05_P, _HOP_1_OP_06_P, _HOP_1_OP_07_P, _HOP_1_OP_08_P, _HOP_1_OP_09_P, _HOP_1_OP_10_P, _HOP_1_OP_11_P, _HOP_1_OP_12_P, _HOP_1_OP_13_P, _HOP_1_OP_14_P, _HOP_1_OP_15_P,},
  { _HOP_2_OP_00_P, _HOP_2_OP_01_P, _HOP_2_OP_02_P, _HOP_2_OP_03_P, _HOP_2_OP_04_P, _HOP_2_OP_05_P, _HOP_2_OP_06_P, _HOP_2_OP_07_P, _HOP_2_OP_08_P, _HOP_2_OP_09_P, _HOP_2_OP_10_P, _HOP_2_OP_11_P, _HOP_2_OP_12_P, _HOP_2_OP_13_P, _HOP_2_OP_14_P, _HOP_2_OP_15_P,},
  { _HOP_3_OP_00_P, _HOP_3_OP_01_P, _HOP_3_OP_02_P, _HOP_3_OP_03_P, _HOP_3_OP_04_P, _HOP_3_OP_05_P, _HOP_3_OP_06_P, _HOP_3_OP_07_P, _HOP_3_OP_08_P, _HOP_3_OP_09_P, _HOP_3_OP_10_P, _HOP_3_OP_11_P, _HOP_3_OP_12_P, _HOP_3_OP_13_P, _HOP_3_OP_14_P, _HOP_3_OP_15_P,}
};



Uint16 LOAD_W_ff8a28(void)
{
  return end_mask_1;
}

Uint16 LOAD_W_ff8a2a(void)
{
  return end_mask_2;
}

Uint16 LOAD_W_ff8a2c(void)
{
  return end_mask_3;
}

Uint32 LOAD_L_ff8a32(void)
{
  return dest_addr_reg;
}

Uint16 LOAD_W_ff8a36(void)
{
  return x_count;
}

Uint16 LOAD_W_ff8a38(void)
{
  return y_count;
}

Sint8 LOAD_B_ff8a3a(void)
{
  return (Uint8)hop;
}

Sint8 LOAD_B_ff8a3b(void)
{
  return (Uint8)op;
}

Sint8 LOAD_B_ff8a3c(void)
{
  if (blit_flag)
  {
    Do_Blit();
    blit_flag = FALSE;
  }

  return (Uint8)(line_num & 0x3f);
}

Sint8 LOAD_B_ff8a3d(void)
{
  return (Uint8)skewreg;
}


void STORE_W_ff8a28(Uint16 v)
{
  end_mask_1 = v;
}

void STORE_W_ff8a2a(Uint16 v)
{
  end_mask_2 = v;
}

void STORE_W_ff8a2c(Uint16 v)
{
  end_mask_3 = v;
}


void STORE_L_ff8a32(Uint32 v)
{ 
  dest_addr_reg = (v & 0x0fffffe);
}


void STORE_W_ff8a36(Uint16 v)
{
  x_count = v;
}

void STORE_W_ff8a38(Uint16 v)
{
  y_count = v;
}

void STORE_B_ff8a3a(Sint8 v)
{
  hop = v & 3;                  /* h/ware reg masks out the top 6 bits! */
}

void STORE_B_ff8a3b(Sint8 v)
{
  op = v & 15;                  /* h/ware reg masks out the top 4 bits! */  
}

void STORE_B_ff8a3c(Sint8 v)
{ 
  if((y_count !=0) && (v & 0x80))   /* Busy bit set and lines to blit? */
    blit_flag = TRUE; 
  line_num   = (UB) v;  
}

void STORE_B_ff8a3d(Sint8 v)
{ 
  NFSR = (v & 0x40) != 0;         
  FXSR = (v & 0x80) != 0;         
  skewreg = (unsigned char) v & 0xcf;   /* h/ware reg mask %11001111 !*/  
}

void Do_Blit(void)
{   
  if (LM_W(MEM(0xff8a20)) < 0)          /* source_x_inc < 0 */
    do_hop_op_N[hop][op]();
  else
    do_hop_op_P[hop][op]();
}
