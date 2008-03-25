/*
 * Hatari - blitter.c
 *
 * This file is distributed under the GNU Public License, version 2 or at
 * your option any later version. Read the file gpl.txt for details.
 *
 * Blitter emulation.
 * This file has originally been taken from STonX.
 * 
 * Changes 2005-10-21:
 * - Convert types and helper functions to SDL and Hatari ones
 * - LineNum -> Control (line number is just low nibble of control register)
 * - Add defines for register addresses
 * - Fix bug with Smudge mode
 * - Add documentation
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
 *  The hardware registers for this chip lie at addresses $ff8a00 - $ff8a3c.
 */
const char Blitter_rcsid[] = "Hatari $Id: blitter.c,v 1.16 2008-03-25 18:06:36 thothy Exp $";

#include <SDL_types.h>
#include <stdio.h>
#include <stdlib.h>

#include "main.h"
#include "blitter.h"
#include "hatari-glue.h"
#include "ioMem.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "stMemory.h"

#define DEBUG 0

/* BLiTTER registers, counts and incs are signed, others unsigned */
#define REG_HT_RAM	0xff8a00	/* - 0xff8a1e */

#define REG_SRC_X_INC	0xff8a20
#define REG_SRC_Y_INC	0xff8a22
#define REG_SRC_ADDR	0xff8a24

#define REG_END_MASK1	0xff8a28
#define REG_END_MASK2	0xff8a2a
#define REG_END_MASK3	0xff8a2c

#define REG_DST_X_INC	0xff8a2e
#define REG_DST_Y_INC	0xff8a30
#define REG_DST_ADDR	0xff8a32

#define REG_X_COUNT	0xff8a36
#define REG_Y_COUNT	0xff8a38

#define REG_BLIT_HOP	0xff8a3a	/* halftone blit operation byte */
#define REG_BLIT_LOP	0xff8a3b	/* logical blit operation byte */
#define REG_CONTROL	0xff8a3c
#define REG_SKEW	0xff8a3d


static Uint16 halftone_ram[16];
static Uint16 end_mask_1, end_mask_2, end_mask_3;
static Uint16 x_count, y_count;
static Uint8 hop, op, blit_control, skewreg;
static Uint8 NFSR, FXSR; 
static Uint32 dest_addr_reg = 0;
static int halftone_curroffset, halftone_direction;
static int source_x_inc, source_y_inc, dest_x_inc, dest_y_inc;


#if DEBUG
static void show_params(Uint32 source_addr)
{
	fprintf(stderr, "Source Address:%X\n", source_addr);
	fprintf(stderr, "  Dest Address:%X\n", dest_addr_reg);
	fprintf(stderr, "       X count:%X\n", x_count);
	fprintf(stderr, "       Y count:%X\n", y_count);
	fprintf(stderr, "  Source X inc:%X\n", source_x_inc);
	fprintf(stderr, "    Dest X inc:%X\n", dest_x_inc);
	fprintf(stderr, "  Source Y inc:%X\n", source_y_inc);
	fprintf(stderr, "    Dest Y inc:%X\n", dest_y_inc);
	fprintf(stderr, "HOP:%2X    OP:%X\n", hop, op);
	fprintf(stderr, "   source SKEW:%X\n", skewreg);
	fprintf(stderr, "     endmask 1:%X\n", end_mask_1);
	fprintf(stderr, "     endmask 2:%X\n", end_mask_2);
	fprintf(stderr, "     endmask 3:%X\n", end_mask_3);
	fprintf(stderr, "  blit control:%X\n", blit_control);
	if (NFSR) fprintf(stderr, "NFSR is Set!\n");
	if (FXSR) fprintf(stderr, "FXSR is Set!\n");
}

static void show_halftones(void)
{
	int i, j;
	fprintf(stderr, "Halftone registers:\n");
	for (i = 0; i < 2; i++) {
		for (j = 0; j < 8; j++) {
			fprintf(stderr,"%4X, ", halftone_ram[i*8+j]);
		}
		fprintf(stderr, "\n");
	}
}
#endif


/* called only before halftone operations, for HOP modes 01 and 11 */
static void load_halftone_ram(Uint32 source_addr)
{
	halftone_ram[0]  = STMemory_ReadWord(REG_HT_RAM);
	halftone_ram[1]  = STMemory_ReadWord(REG_HT_RAM+2);
	halftone_ram[2]  = STMemory_ReadWord(REG_HT_RAM+4);
	halftone_ram[3]  = STMemory_ReadWord(REG_HT_RAM+6);
	halftone_ram[4]  = STMemory_ReadWord(REG_HT_RAM+8);
	halftone_ram[5]  = STMemory_ReadWord(REG_HT_RAM+10);
	halftone_ram[6]  = STMemory_ReadWord(REG_HT_RAM+12);
	halftone_ram[7]  = STMemory_ReadWord(REG_HT_RAM+14);
	halftone_ram[8]  = STMemory_ReadWord(REG_HT_RAM+16);
	halftone_ram[9]  = STMemory_ReadWord(REG_HT_RAM+18);
	halftone_ram[10] = STMemory_ReadWord(REG_HT_RAM+20);
	halftone_ram[11] = STMemory_ReadWord(REG_HT_RAM+22);
	halftone_ram[12] = STMemory_ReadWord(REG_HT_RAM+24);
	halftone_ram[13] = STMemory_ReadWord(REG_HT_RAM+26);
	halftone_ram[14] = STMemory_ReadWord(REG_HT_RAM+28);
	halftone_ram[15] = STMemory_ReadWord(REG_HT_RAM+30);

	if (!(blit_control & 0x20)) {
		/* No smudge mode: Get halftone offset from control register */
		halftone_curroffset = blit_control & 15;
	}

	if (dest_y_inc >= 0) {
		halftone_direction = 1;
	} else {
		halftone_direction = -1;
	}
#if DEBUG
	show_params(source_addr);
	show_halftones();
#endif
}


#define HOP_OPS(_fn_name,_op,_do_source_shift,_get_source_data,_shifted_hopd_data, _do_halftone_inc) \
static void _fn_name (void)  \
{  \
	Uint32 source_addr  = STMemory_ReadLong(REG_SRC_ADDR);   \
	Uint32 dest_addr = dest_addr_reg;  \
	Uint32 source_buffer = 0;  \
	Uint8 skew = skewreg & 15;  \
	/*if(address_space_24)*/  \
	{ source_addr &= 0x0fffffe; dest_addr &= 0x0fffffe; }  \
	if (op == 0 || op == 15) \
	{ \
		/* Do not increment source address for OP 0 and 15  */ \
		/* (needed for Grotesque demo by Omega for example) */ \
		source_x_inc = 0; \
		source_y_inc = 0; \
	} \
	else \
	{ \
		source_x_inc = (short) STMemory_ReadWord(REG_SRC_X_INC);  \
		source_y_inc = (short) STMemory_ReadWord(REG_SRC_Y_INC);  \
	} \
	dest_x_inc   = (short) STMemory_ReadWord(REG_DST_X_INC);  \
	dest_y_inc   = (short) STMemory_ReadWord(REG_DST_Y_INC);  \
	if (hop & 1) load_halftone_ram(source_addr);  \
	do  \
	{  \
		Uint16 x, dst_data, opd_data;  \
		if (FXSR)  \
		{  \
			_do_source_shift;  \
			_get_source_data;  \
			source_addr += source_x_inc;  \
		}  \
		_do_source_shift;  \
		_get_source_data;  \
		dst_data = STMemory_ReadWord(dest_addr);  \
		opd_data =  _shifted_hopd_data;  \
		STMemory_WriteWord(dest_addr,(dst_data & ~end_mask_1) | (_op & end_mask_1));  \
		for(x=0 ; x<x_count-2 ; x++)  \
		{  \
			source_addr += source_x_inc;  \
			dest_addr += dest_x_inc;  \
			_do_source_shift;  \
			_get_source_data;  \
			dst_data = STMemory_ReadWord(dest_addr);  \
			opd_data = _shifted_hopd_data;  \
			STMemory_WriteWord(dest_addr,(dst_data & ~end_mask_2) | (_op & end_mask_2));  \
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
			dst_data = STMemory_ReadWord(dest_addr);  \
			opd_data = _shifted_hopd_data;  \
			STMemory_WriteWord(dest_addr,(((Uint16)dst_data) & ~end_mask_3) | (_op & end_mask_3));  \
		}  \
		source_addr += source_y_inc;  \
		dest_addr += dest_y_inc;  \
		_do_halftone_inc;  \
	} while (--y_count > 0);  \
	STMemory_WriteLong(REG_SRC_ADDR, source_addr);  \
	dest_addr_reg = dest_addr;  \
}


/* In smudge mode, the halftone offset is determined by the lowest 4 bits of
 * the first source word, after it has been skewed */
#define HALFTONE_OFFSET  ((blit_control & 0x20) ? \
	((STMemory_ReadWord(source_addr) >> skew) & 15) : halftone_curroffset)


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

HOP_OPS(_HOP_1_OP_00_N,(0) ,source_buffer >>=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_01_N,(opd_data & dst_data) ,source_buffer >>=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_02_N,(opd_data & ~dst_data) ,source_buffer >>=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_03_N,(opd_data) ,source_buffer >>=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_04_N,(~opd_data & dst_data) ,source_buffer >>=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_05_N,(dst_data) ,source_buffer >>=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_06_N,(opd_data ^ dst_data) ,source_buffer >>=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_07_N,(opd_data | dst_data) ,source_buffer >>=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_08_N,(~opd_data & ~dst_data) ,source_buffer >>=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_09_N,(~opd_data ^ dst_data) ,source_buffer >>=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_10_N,(~dst_data) ,source_buffer >>=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_11_N,(opd_data | ~dst_data) ,source_buffer >>=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_12_N,(~opd_data) ,source_buffer >>=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_13_N,(~opd_data | dst_data) ,source_buffer >>=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_14_N,(~opd_data | ~dst_data) ,source_buffer >>=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_15_N,(0xffff) ,source_buffer >>=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )

HOP_OPS(_HOP_2_OP_00_N,(0) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_01_N,(opd_data & dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_02_N,(opd_data & ~dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_03_N,(opd_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_04_N,(~opd_data & dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_05_N,(dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_06_N,(opd_data ^ dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_07_N,(opd_data | dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_08_N,(~opd_data & ~dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_09_N,(~opd_data ^ dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_10_N,(~dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_11_N,(opd_data | ~dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_12_N,(~opd_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_13_N,(~opd_data | dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_14_N,(~opd_data | ~dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_15_N,(0xffff) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew),;)

HOP_OPS(_HOP_3_OP_00_N,(0) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_01_N,(opd_data & dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_02_N,(opd_data & ~dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_03_N,(opd_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_04_N,(~opd_data & dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_05_N,(dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_06_N,(opd_data ^ dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_07_N,(opd_data | dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_08_N,(~opd_data & ~dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_09_N,(~opd_data ^ dst_data) , source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_10_N,(~dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_11_N,(opd_data | ~dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_12_N,(~opd_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_13_N,(~opd_data | dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)
HOP_OPS(_HOP_3_OP_14_N,(~opd_data | ~dst_data) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15) 
HOP_OPS(_HOP_3_OP_15_N,(0xffff) ,source_buffer >>=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) << 16) ,(source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15)


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

HOP_OPS(_HOP_1_OP_00_P,(0) ,source_buffer <<=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_01_P,(opd_data & dst_data) ,source_buffer <<=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_02_P,(opd_data & ~dst_data) ,source_buffer <<=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_03_P,(opd_data) ,source_buffer <<=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_04_P,(~opd_data & dst_data) ,source_buffer <<=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_05_P,(dst_data) ,source_buffer <<=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_06_P,(opd_data ^ dst_data) ,source_buffer <<=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_07_P,(opd_data | dst_data) ,source_buffer <<=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_08_P,(~opd_data & ~dst_data) ,source_buffer <<=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )\
HOP_OPS(_HOP_1_OP_09_P,(~opd_data ^ dst_data) ,source_buffer <<=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_10_P,(~dst_data) ,source_buffer <<=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_11_P,(opd_data | ~dst_data) ,source_buffer <<=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_12_P,(~opd_data) ,source_buffer <<=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_13_P,(~opd_data | dst_data) ,source_buffer <<=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_14_P,(~opd_data | ~dst_data) ,source_buffer <<=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )
HOP_OPS(_HOP_1_OP_15_P,(0xffff) ,source_buffer <<=16,;,halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 )

HOP_OPS(_HOP_2_OP_00_P,(0) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_01_P,(opd_data & dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_02_P,(opd_data & ~dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_03_P,(opd_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_04_P,(~opd_data & dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_05_P,(dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_06_P,(opd_data ^ dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_07_P,(opd_data | dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_08_P,(~opd_data & ~dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_09_P,(~opd_data ^ dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_10_P,(~dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_11_P,(opd_data | ~dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ) , (source_buffer >> skew),;)

HOP_OPS(_HOP_2_OP_12_P,(~opd_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_13_P,(~opd_data | dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_14_P,(~opd_data | ~dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ) , (source_buffer >> skew),;)
HOP_OPS(_HOP_2_OP_15_P,(0xffff) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ) , (source_buffer >> skew),;)

HOP_OPS(_HOP_3_OP_00_P,(0) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ), (source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_01_P,(opd_data & dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ), (source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_02_P,(opd_data & ~dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ), (source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_03_P,(opd_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ), (source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_04_P,(~opd_data & dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ), (source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_05_P,(dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ), (source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_06_P,(opd_data ^ dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ), (source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_07_P,(opd_data | dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ), (source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_08_P,(~opd_data & ~dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ), (source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_09_P,(~opd_data ^ dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ), (source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_10_P,(~dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ), (source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_11_P,(opd_data | ~dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ), (source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_12_P,(~opd_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ), (source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_13_P,(~opd_data | dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ), (source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_14_P,(~opd_data | ~dst_data) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ), (source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 
HOP_OPS(_HOP_3_OP_15_P,(0xffff) ,source_buffer <<=16,source_buffer |= ((Uint32) STMemory_ReadWord(source_addr) ), (source_buffer >> skew) & halftone_ram[HALFTONE_OFFSET],halftone_curroffset=(halftone_curroffset+halftone_direction) & 15 ) 

static void (* const do_hop_op_N[4][16])(void) =
{
  { _HOP_0_OP_00_N, _HOP_0_OP_01_N, _HOP_0_OP_02_N, _HOP_0_OP_03_N, _HOP_0_OP_04_N, _HOP_0_OP_05_N, _HOP_0_OP_06_N, _HOP_0_OP_07_N, _HOP_0_OP_08_N, _HOP_0_OP_09_N, _HOP_0_OP_10_N, _HOP_0_OP_11_N, _HOP_0_OP_12_N, _HOP_0_OP_13_N, _HOP_0_OP_14_N, _HOP_0_OP_15_N,},
  { _HOP_1_OP_00_N, _HOP_1_OP_01_N, _HOP_1_OP_02_N, _HOP_1_OP_03_N, _HOP_1_OP_04_N, _HOP_1_OP_05_N, _HOP_1_OP_06_N, _HOP_1_OP_07_N, _HOP_1_OP_08_N, _HOP_1_OP_09_N, _HOP_1_OP_10_N, _HOP_1_OP_11_N, _HOP_1_OP_12_N, _HOP_1_OP_13_N, _HOP_1_OP_14_N, _HOP_1_OP_15_N,},
  { _HOP_2_OP_00_N, _HOP_2_OP_01_N, _HOP_2_OP_02_N, _HOP_2_OP_03_N, _HOP_2_OP_04_N, _HOP_2_OP_05_N, _HOP_2_OP_06_N, _HOP_2_OP_07_N, _HOP_2_OP_08_N, _HOP_2_OP_09_N, _HOP_2_OP_10_N, _HOP_2_OP_11_N, _HOP_2_OP_12_N, _HOP_2_OP_13_N, _HOP_2_OP_14_N, _HOP_2_OP_15_N,},
  { _HOP_3_OP_00_N, _HOP_3_OP_01_N, _HOP_3_OP_02_N, _HOP_3_OP_03_N, _HOP_3_OP_04_N, _HOP_3_OP_05_N, _HOP_3_OP_06_N, _HOP_3_OP_07_N, _HOP_3_OP_08_N, _HOP_3_OP_09_N, _HOP_3_OP_10_N, _HOP_3_OP_11_N, _HOP_3_OP_12_N, _HOP_3_OP_13_N, _HOP_3_OP_14_N, _HOP_3_OP_15_N,}
};

static void (* const do_hop_op_P[4][16])(void) =
{
  { _HOP_0_OP_00_P, _HOP_0_OP_01_P, _HOP_0_OP_02_P, _HOP_0_OP_03_P, _HOP_0_OP_04_P, _HOP_0_OP_05_P, _HOP_0_OP_06_P, _HOP_0_OP_07_P, _HOP_0_OP_08_P, _HOP_0_OP_09_P, _HOP_0_OP_10_P, _HOP_0_OP_11_P, _HOP_0_OP_12_P, _HOP_0_OP_13_P, _HOP_0_OP_14_P, _HOP_0_OP_15_P,},
  { _HOP_1_OP_00_P, _HOP_1_OP_01_P, _HOP_1_OP_02_P, _HOP_1_OP_03_P, _HOP_1_OP_04_P, _HOP_1_OP_05_P, _HOP_1_OP_06_P, _HOP_1_OP_07_P, _HOP_1_OP_08_P, _HOP_1_OP_09_P, _HOP_1_OP_10_P, _HOP_1_OP_11_P, _HOP_1_OP_12_P, _HOP_1_OP_13_P, _HOP_1_OP_14_P, _HOP_1_OP_15_P,},
  { _HOP_2_OP_00_P, _HOP_2_OP_01_P, _HOP_2_OP_02_P, _HOP_2_OP_03_P, _HOP_2_OP_04_P, _HOP_2_OP_05_P, _HOP_2_OP_06_P, _HOP_2_OP_07_P, _HOP_2_OP_08_P, _HOP_2_OP_09_P, _HOP_2_OP_10_P, _HOP_2_OP_11_P, _HOP_2_OP_12_P, _HOP_2_OP_13_P, _HOP_2_OP_14_P, _HOP_2_OP_15_P,},
  { _HOP_3_OP_00_P, _HOP_3_OP_01_P, _HOP_3_OP_02_P, _HOP_3_OP_03_P, _HOP_3_OP_04_P, _HOP_3_OP_05_P, _HOP_3_OP_06_P, _HOP_3_OP_07_P, _HOP_3_OP_08_P, _HOP_3_OP_09_P, _HOP_3_OP_10_P, _HOP_3_OP_11_P, _HOP_3_OP_12_P, _HOP_3_OP_13_P, _HOP_3_OP_14_P, _HOP_3_OP_15_P,}
};



/*-----------------------------------------------------------------------*/
/**
 * Do the blit.
 */
static void Do_Blit(void)
{
	if (((short)STMemory_ReadWord(REG_SRC_X_INC)) < 0)
		do_hop_op_N[hop][op]();
	else
		do_hop_op_P[hop][op]();
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter endmask 1.
 */
void Blitter_Endmask1_ReadWord(void)
{
	IoMem_WriteWord(REG_END_MASK1, end_mask_1);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter endmask 2.
 */
void Blitter_Endmask2_ReadWord(void)
{
	IoMem_WriteWord(REG_END_MASK2, end_mask_2);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter endmask 3.
 */
void Blitter_Endmask3_ReadWord(void)
{
	IoMem_WriteWord(REG_END_MASK3, end_mask_3);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter destination address.
 */
void Blitter_DestAddr_ReadLong(void)
{
	IoMem_WriteLong(REG_DST_ADDR, dest_addr_reg);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter words-per-line register.
 */
void Blitter_WordsPerLine_ReadWord(void)
{
	IoMem_WriteWord(REG_X_COUNT, x_count);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter lines-per-bitblock register.
 */
void Blitter_LinesPerBitblock_ReadWord(void)
{
	IoMem_WriteWord(REG_Y_COUNT, y_count);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter halftone operation register.
 */
void Blitter_HalftoneOp_ReadByte(void)
{
	IoMem_WriteByte(REG_BLIT_HOP, hop);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter logical operation register.
 */
void Blitter_LogOp_ReadByte(void)
{
	IoMem_WriteByte(REG_BLIT_LOP, op);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter control register.
 */
void Blitter_Control_ReadByte(void)
{
	/* we don't implement Blit mode so this can never be
	 * busy when application gets the return value
	 * %11101111
	 * busy, hog/blit, smudge, n/a, 4bits for line number
	 */
	//IoMem_WriteByte(REG_CONTROL, (blit_control & 0x6f));
	IoMem_WriteByte(REG_CONTROL, (blit_control & 0x3f));
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter skew register.
 */
void Blitter_Skew_ReadByte(void)
{
	IoMem_WriteByte(REG_SKEW, skewreg);
}


/*-----------------------------------------------------------------------*/
/**
 * Write to blitter endmask 1.
 */
void Blitter_Endmask1_WriteWord(void)
{
	end_mask_1 = IoMem_ReadWord(REG_END_MASK1);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter endmask 2.
 */
void Blitter_Endmask2_WriteWord(void)
{
	end_mask_2 = IoMem_ReadWord(REG_END_MASK2);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter endmask 3.
 */
void Blitter_Endmask3_WriteWord(void)
{
	end_mask_3 = IoMem_ReadWord(REG_END_MASK3);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter destination address register.
 */
void Blitter_DestAddr_WriteLong(void)
{
	dest_addr_reg = IoMem_ReadLong(REG_DST_ADDR) & 0x0fffffe;
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter words-per-line register.
 */
void Blitter_WordsPerLine_WriteWord(void)
{
	x_count = IoMem_ReadWord(REG_X_COUNT);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter words-per-bitblock register.
 */
void Blitter_LinesPerBitblock_WriteWord(void)
{
	y_count = IoMem_ReadWord(REG_Y_COUNT);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter halftone operation register.
 */
void Blitter_HalftoneOp_WriteByte(void)
{
	/* h/ware reg masks out the top 6 bits! */
	hop = IoMem_ReadByte(REG_BLIT_HOP) & 3;
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter logical operation register.
 */
void Blitter_LogOp_WriteByte(void)
{	
	/* h/ware reg masks out the top 4 bits! */
	op = IoMem_ReadByte(REG_BLIT_LOP) & 15;
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter control register.
 */
void Blitter_Control_WriteByte(void)
{
	/* Control register bits:
	 * 0x80: busy bit
	 * - Turn on Blitter activity and stay "1" until copy finished
	 * 0x40: Blit-mode bit
	 * - 0: Blit mode, CPU and Blitter get 64 clockcycles in turns
	 * - 1: HOG Mode, Blitter reserves and hogs the bus for as long
	 *      as the copy takes, CPU and DMA get no Bus access
	 * 0x20: Smudge mode
	 * - Which line of the halftone pattern to start with is
	 *   read from the first source word when the copy starts
	 * 0x10: not used
	 * 0x0f
	 *
	 * The lowest 4 bits contain the Halftone pattern line number
	 */
	blit_control = IoMem_ReadByte(REG_CONTROL) & 0xef;
	
	/* Lines to blit and busy bit set? */
	if((y_count !=0) && (blit_control & 0x80))
	{
		/* Needed if a program executes for example
		 * move.W #xxxx,$ff8a3c
		 */
		Blitter_Skew_WriteByte();

		/* TODO:
		 * - Emulate the shared bus mode when HOG flag is cleared 
		 * - Add proper blitter timings
		 */
		M68000_AddCycles(y_count);

		Do_Blit();
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter skew register.
 */
void Blitter_Skew_WriteByte(void)
{
	Uint8 v = IoMem_ReadByte(REG_SKEW);
	NFSR = (v & 0x40) != 0;
	FXSR = (v & 0x80) != 0;
	skewreg = v & 0xcf;        /* h/ware reg mask %11001111 !*/
}


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of Blitter variables.
 */
void Blitter_MemorySnapShot_Capture(BOOL bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(halftone_ram, sizeof(halftone_ram));
	MemorySnapShot_Store(&end_mask_1, sizeof(end_mask_1));
	MemorySnapShot_Store(&end_mask_2, sizeof(end_mask_2));
	MemorySnapShot_Store(&end_mask_3, sizeof(end_mask_3));
	MemorySnapShot_Store(&NFSR, sizeof(NFSR));
	MemorySnapShot_Store(&FXSR, sizeof(FXSR));
	MemorySnapShot_Store(&x_count, sizeof(y_count));
	MemorySnapShot_Store(&hop, sizeof(hop));
	MemorySnapShot_Store(&op, sizeof(op));
	MemorySnapShot_Store(&blit_control, sizeof(blit_control));
	MemorySnapShot_Store(&skewreg, sizeof(skewreg));
	MemorySnapShot_Store(&dest_addr_reg, sizeof(dest_addr_reg));
	MemorySnapShot_Store(&halftone_curroffset, sizeof(halftone_curroffset));
	MemorySnapShot_Store(&halftone_direction, sizeof(halftone_direction));
	MemorySnapShot_Store(&source_x_inc, sizeof(source_x_inc));
	MemorySnapShot_Store(&source_y_inc, sizeof(source_y_inc));
	MemorySnapShot_Store(&dest_x_inc, sizeof(dest_x_inc));
	MemorySnapShot_Store(&dest_y_inc, sizeof(dest_y_inc));
}
