/*
	DSP M56001 emulation
	Disassembler

	(C) 2003-2008 ARAnyM developer team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "dsp_core.h"
#include "dsp_cpu.h"
#include "dsp_disasm.h"

#define DEBUG 0

/* More disasm infos, if wanted */
#define DSP_DISASM_REG_PC 0

/**********************************
 *	Defines
 **********************************/

#define BITMASK(x)	((1<<(x))-1)

/**********************************
 *	Variables
 **********************************/

/* Current instruction */
static char str_instr[50];
static char str_instr2[100];
static Uint32 cur_inst;
static Uint32 disasm_cur_inst_len;

/* Previous instruction */
static Uint32 prev_inst_pc = 0x10000;	/* Init to an invalid value */
static Uint16 isLooping = 0;

static dsp_core_t *dsp_core;

void dsp56k_disasm_init(dsp_core_t *my_dsp_core)
{
	dsp_core = my_dsp_core;
}

/**********************************
 *	Register change
 **********************************/

static Uint32 registers_save[64];
static Uint32 registers_changed[64];
#if DSP_DISASM_REG_PC
static Uint32 pc_save;
#endif

static const char *registers_name[64]={
	"","","","",
	"x0","x1","y0","y1",
	"a0","b0","a2","b2",
	"a1","b1","a","b",
	
	"r0","r1","r2","r3",
	"r4","r5","r6","r7",
	"n0","n1","n2","n3",
	"n4","n5","n6","n7",

	"m0","m1","m2","m3",
	"m4","m5","m6","m7",
	"","","","",
	"","","","",

	"","","","",
	"","","","",
	"","sr","omr","sp",
	"ssh","ssl","la","lc"
};

void dsp56k_disasm_reg_read(void)
{
	memcpy(registers_save, dsp_core->registers , sizeof(registers_save));
	memset(registers_changed, 0, sizeof(registers_changed));
#if DSP_DISASM_REG_PC
	pc_save = dsp_core->pc;
#endif
}

void dsp56k_disasm_reg_compare(void)
{
	int i;
	
	for (i=0;i<64;i++) {
		if (!registers_changed[i]) {
			continue;
		}

		switch(i) {
			case DSP_REG_X0:
			case DSP_REG_X1:
			case DSP_REG_Y0:
			case DSP_REG_Y1:
			case DSP_REG_A0:
			case DSP_REG_A1:
			case DSP_REG_B0:
			case DSP_REG_B1:
				fprintf(stderr," Reg: %s: 0x%06x -> 0x%06x\n", registers_name[i], registers_save[i] & BITMASK(24), dsp_core->registers[i]  & BITMASK(24));
				break;
			case DSP_REG_R0:
			case DSP_REG_R1:
			case DSP_REG_R2:
			case DSP_REG_R3:
			case DSP_REG_R4:
			case DSP_REG_R5:
			case DSP_REG_R6:
			case DSP_REG_R7:
			case DSP_REG_M0:
			case DSP_REG_M1:
			case DSP_REG_M2:
			case DSP_REG_M3:
			case DSP_REG_M4:
			case DSP_REG_M5:
			case DSP_REG_M6:
			case DSP_REG_M7:
			case DSP_REG_N0:
			case DSP_REG_N1:
			case DSP_REG_N2:
			case DSP_REG_N3:
			case DSP_REG_N4:
			case DSP_REG_N5:
			case DSP_REG_N6:
			case DSP_REG_N7:
			case DSP_REG_SR:
			case DSP_REG_LA:
			case DSP_REG_LC:
				fprintf(stderr," Reg: %s: 0x%04x -> 0x%04x\n", registers_name[i], registers_save[i] & BITMASK(16), dsp_core->registers[i]  & BITMASK(16));
				break;
			case DSP_REG_A2:
			case DSP_REG_B2:
			case DSP_REG_OMR:
			case DSP_REG_SP:
			case DSP_REG_SSH:
			case DSP_REG_SSL:
				fprintf(stderr," Reg: %s: 0x%02x -> 0x%02x\n", registers_name[i], registers_save[i] & BITMASK(8), dsp_core->registers[i]  & BITMASK(8));
				break;
			case DSP_REG_A:
			case DSP_REG_B:
				{
					fprintf(stderr," Reg: %s: 0x%02x:%06x:%06x -> 0x%02x:%06x:%06x\n",
						registers_name[i],
						registers_save[DSP_REG_A2+(i & 1)] & BITMASK(8),
						registers_save[DSP_REG_A1+(i & 1)] & BITMASK(24),
						registers_save[DSP_REG_A0+(i & 1)] & BITMASK(24),
						dsp_core->registers[DSP_REG_A2+(i & 1)]  & BITMASK(8),
						dsp_core->registers[DSP_REG_A1+(i & 1)]  & BITMASK(24),
						dsp_core->registers[DSP_REG_A0+(i & 1)]  & BITMASK(24)
					);
				}
				break;
		}
	}
#if DSP_DISASM_REG_PC
	if (pc_save != dsp_core->pc) {
		fprintf(stderr," Reg: pc: 0x%04x -> 0x%04x\n", pc_save, dsp_core->pc);
	}
#endif
}

/**********************************
 *	Opcode disassembler
 **********************************/

static Uint32 read_memory(Uint32 currPc);

typedef void (*dsp_emul_t)(void);

static void opcode8h_0(void);

static int dsp_calc_ea(Uint32 ea_mode, char *dest);
static void dsp_calc_cc(Uint32 cc_mode, char *dest);
static void dsp_undefined(void);

/* Instructions without parallel moves */
static void dsp_andi(void);
static void dsp_bchg_aa(void);
static void dsp_bchg_ea(void);
static void dsp_bchg_pp(void);
static void dsp_bchg_reg(void);
static void dsp_bclr_aa(void);
static void dsp_bclr_ea(void);
static void dsp_bclr_pp(void);
static void dsp_bclr_reg(void);
static void dsp_bset_aa(void);
static void dsp_bset_ea(void);
static void dsp_bset_pp(void);
static void dsp_bset_reg(void);
static void dsp_btst_aa(void);
static void dsp_btst_ea(void);
static void dsp_btst_pp(void);
static void dsp_btst_reg(void);
static void dsp_div(void);
static void dsp_enddo(void);
static void dsp_illegal(void);
static void dsp_jcc_imm(void);
static void dsp_jcc_ea(void);
static void dsp_jclr_aa(void);
static void dsp_jclr_ea(void);
static void dsp_jclr_pp(void);
static void dsp_jclr_reg(void);
static void dsp_jmp_ea(void);
static void dsp_jmp_imm(void);
static void dsp_jscc_ea(void);
static void dsp_jscc_imm(void);
static void dsp_jsclr_aa(void);
static void dsp_jsclr_ea(void);
static void dsp_jsclr_pp(void);
static void dsp_jsclr_reg(void);
static void dsp_jset_aa(void);
static void dsp_jset_ea(void);
static void dsp_jset_pp(void);
static void dsp_jset_reg(void);
static void dsp_jsr_ea(void);
static void dsp_jsr_imm(void);
static void dsp_jsset_aa(void);
static void dsp_jsset_ea(void);
static void dsp_jsset_pp(void);
static void dsp_jsset_reg(void);
static void dsp_lua(void);
static void dsp_movem_ea(void);
static void dsp_movem_aa(void);
static void dsp_nop(void);
static void dsp_norm(void);
static void dsp_ori(void);
static void dsp_reset(void);
static void dsp_rti(void);
static void dsp_rts(void);
static void dsp_stop(void);
static void dsp_swi(void);
static void dsp_tcc(void);
static void dsp_wait(void);
static void dsp_do_ea(void);
static void dsp_do_aa(void);
static void dsp_do_imm(void);
static void dsp_do_reg(void);
static void dsp_rep_aa(void);
static void dsp_rep_ea(void);
static void dsp_rep_imm(void);
static void dsp_rep_reg(void);
static void dsp_movec_aa(void);
static void dsp_movec_ea(void);
static void dsp_movec_imm(void);
static void dsp_movec_reg(void);
static void dsp_movep_0(void);
static void dsp_movep_1(void);
static void dsp_movep_23(void);

/* Parallel moves */
static void dsp_pm_class2(void);
static void dsp_pm(void);
static void dsp_pm_0(void);
static void dsp_pm_1(void);
static void dsp_pm_2(void);
static void dsp_pm_4(void);
static void dsp_pm_8(void);

/* Instructions with parallel moves */
static void dsp_abs(void);
static void dsp_adc(void);
static void dsp_add(void);
static void dsp_addl(void);
static void dsp_addr(void);
static void dsp_and(void);
static void dsp_asl(void);
static void dsp_asr(void);
static void dsp_clr(void);
static void dsp_cmp(void);
static void dsp_cmpm(void);
static void dsp_eor(void);
static void dsp_lsl(void);
static void dsp_lsr(void);
static void dsp_mac(void);
static void dsp_macr(void);
static void dsp_move(void);
static void dsp_mpy(void);
static void dsp_mpyr(void);
static void dsp_neg(void);
static void dsp_not(void);
static void dsp_or(void);
static void dsp_rnd(void);
static void dsp_rol(void);
static void dsp_ror(void);
static void dsp_sbc(void);
static void dsp_sub(void);
static void dsp_subl(void);
static void dsp_subr(void);
static void dsp_tfr(void);
static void dsp_tst(void);

static dsp_emul_t opcodes8h[512]={
	/* 0x00 - 0x3f */
	opcode8h_0, dsp_undefined, dsp_undefined, dsp_undefined, opcode8h_0, dsp_andi, dsp_undefined, dsp_ori,
	dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_andi, dsp_undefined, dsp_ori,
	dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_andi, dsp_undefined, dsp_ori,
	dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_andi, dsp_undefined, dsp_ori,
	dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined,
	dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined,
	dsp_undefined, dsp_undefined, dsp_div, dsp_div, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined,
	dsp_norm, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined,
	
	/* 0x40 - 0x7f */
	dsp_tcc, dsp_tcc, dsp_tcc, dsp_tcc, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined,
	dsp_tcc, dsp_tcc, dsp_tcc, dsp_tcc, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined,
	dsp_tcc, dsp_tcc, dsp_tcc, dsp_tcc, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined,
	dsp_tcc, dsp_tcc, dsp_tcc, dsp_tcc, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined,
	dsp_tcc, dsp_tcc, dsp_tcc, dsp_tcc, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined,
	dsp_tcc, dsp_tcc, dsp_tcc, dsp_tcc, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined,
	dsp_tcc, dsp_tcc, dsp_tcc, dsp_tcc, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined,
	dsp_tcc, dsp_tcc, dsp_tcc, dsp_tcc, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined,

	/* 0x80 - 0xbf */
	dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined,
	dsp_lua, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_movec_reg, dsp_undefined, dsp_undefined, 
	dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined,
	dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_movec_reg, dsp_undefined, dsp_undefined, 
	dsp_undefined, dsp_movec_aa, dsp_undefined, dsp_movec_aa, dsp_undefined, dsp_movec_imm, dsp_undefined, dsp_undefined,
	dsp_undefined, dsp_movec_ea, dsp_undefined, dsp_movec_ea, dsp_undefined, dsp_movec_imm, dsp_undefined, dsp_undefined,
	dsp_undefined, dsp_movec_aa, dsp_undefined, dsp_movec_aa, dsp_undefined, dsp_movec_imm, dsp_undefined, dsp_undefined,
	dsp_undefined, dsp_movec_ea, dsp_undefined, dsp_movec_ea, dsp_undefined, dsp_movec_imm, dsp_undefined, dsp_undefined,
	
	/* 0xc0 - 0xff */
	dsp_do_aa, dsp_rep_aa, dsp_do_aa, dsp_rep_aa, dsp_do_imm, dsp_rep_imm, dsp_undefined, dsp_undefined, 
	dsp_do_ea, dsp_rep_ea, dsp_do_ea, dsp_rep_ea, dsp_do_imm, dsp_rep_imm, dsp_undefined, dsp_undefined, 
	dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_do_imm, dsp_rep_imm, dsp_undefined, dsp_undefined, 
	dsp_do_reg, dsp_rep_reg, dsp_undefined, dsp_undefined, dsp_do_imm, dsp_rep_imm, dsp_undefined, dsp_undefined, 
	dsp_movem_aa, dsp_movem_aa, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, 
	dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_movem_ea, dsp_movem_ea, dsp_undefined, dsp_undefined, 
	dsp_movem_aa, dsp_movem_aa, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, 
	dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_movem_ea, dsp_movem_ea, dsp_undefined, dsp_undefined, 

	/* 0x100 - 0x13f */
	dsp_pm_class2, dsp_pm_class2, dsp_pm_class2, dsp_pm_class2, dsp_pm_class2, dsp_pm_class2, dsp_pm_class2, dsp_pm_class2,
	dsp_movep_0, dsp_movep_0, dsp_movep_1, dsp_movep_1, dsp_movep_23, dsp_movep_23, dsp_movep_23, dsp_movep_23,
	dsp_pm_class2, dsp_pm_class2, dsp_pm_class2, dsp_pm_class2, dsp_pm_class2, dsp_pm_class2, dsp_pm_class2, dsp_pm_class2,
	dsp_movep_0, dsp_movep_0, dsp_movep_1, dsp_movep_1, dsp_movep_23, dsp_movep_23, dsp_movep_23, dsp_movep_23,
	dsp_pm_class2, dsp_pm_class2, dsp_pm_class2, dsp_pm_class2, dsp_pm_class2, dsp_pm_class2, dsp_pm_class2, dsp_pm_class2,
	dsp_movep_0, dsp_movep_0, dsp_movep_1, dsp_movep_1, dsp_movep_23, dsp_movep_23, dsp_movep_23, dsp_movep_23,
	dsp_pm_class2, dsp_pm_class2, dsp_pm_class2, dsp_pm_class2, dsp_pm_class2, dsp_pm_class2, dsp_pm_class2, dsp_pm_class2,
	dsp_movep_0, dsp_movep_0, dsp_movep_1, dsp_movep_1, dsp_movep_23, dsp_movep_23, dsp_movep_23, dsp_movep_23,

	/* 0x140 - 0x17f */
	dsp_bclr_aa, dsp_bset_aa, dsp_bclr_aa, dsp_bset_aa, dsp_jclr_aa, dsp_jset_aa, dsp_jclr_aa, dsp_jset_aa,
	dsp_bclr_ea, dsp_bset_ea, dsp_bclr_ea, dsp_bset_ea, dsp_jclr_ea, dsp_jset_ea, dsp_jclr_ea, dsp_jset_ea,
	dsp_bclr_pp, dsp_bset_pp, dsp_bclr_pp, dsp_bset_pp, dsp_jclr_pp, dsp_jset_pp, dsp_jclr_pp, dsp_jset_pp,
	dsp_jclr_reg, dsp_jset_reg, dsp_bclr_reg, dsp_bset_reg, dsp_jmp_ea, dsp_jcc_ea, dsp_undefined, dsp_undefined,
	dsp_bchg_aa, dsp_btst_aa, dsp_bchg_aa, dsp_btst_aa, dsp_jsclr_aa, dsp_jsset_aa, dsp_jsclr_aa, dsp_jsset_aa,
	dsp_bchg_ea, dsp_btst_ea, dsp_bchg_ea, dsp_btst_ea, dsp_jsclr_ea, dsp_jsset_ea, dsp_jsclr_ea, dsp_jsset_ea,
	dsp_bchg_pp, dsp_btst_pp, dsp_bchg_pp, dsp_btst_pp, dsp_jsclr_pp, dsp_jsset_pp, dsp_jsclr_pp, dsp_jsset_pp,
	dsp_jsclr_reg, dsp_jsset_reg, dsp_bchg_reg, dsp_btst_reg, dsp_jsr_ea, dsp_jscc_ea, dsp_undefined, dsp_undefined,

	/* 0x180 - 0x1bf */
	dsp_jmp_imm, dsp_jmp_imm, dsp_jmp_imm, dsp_jmp_imm, dsp_jmp_imm, dsp_jmp_imm, dsp_jmp_imm, dsp_jmp_imm,
	dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, 
	dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, 
	dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, 
	dsp_jsr_imm, dsp_jsr_imm, dsp_jsr_imm, dsp_jsr_imm, dsp_jsr_imm, dsp_jsr_imm, dsp_jsr_imm, dsp_jsr_imm, 
	dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, 
	dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, 
	dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, dsp_undefined, 

	/* 0x1c0 - 0x1ff */
	dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, 
	dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, 
	dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, 
	dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, dsp_jcc_imm, 
	dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, 
	dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, 
	dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, 
	dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, dsp_jscc_imm, 
};

static dsp_emul_t opcodes_alu[256]={
	/* 0x00 - 0x3f */
	dsp_move, dsp_tfr, dsp_addr, dsp_tst, dsp_undefined, dsp_cmp, dsp_subr, dsp_cmpm,
	dsp_undefined, dsp_tfr, dsp_addr, dsp_tst, dsp_undefined, dsp_cmp, dsp_subr, dsp_cmpm,
	dsp_add, dsp_rnd, dsp_addl, dsp_clr, dsp_sub, dsp_undefined, dsp_subl, dsp_not,
	dsp_add, dsp_rnd, dsp_addl, dsp_clr, dsp_sub, dsp_undefined, dsp_subl, dsp_not,
	dsp_add, dsp_adc, dsp_asr, dsp_lsr, dsp_sub, dsp_sbc, dsp_abs, dsp_ror,
	dsp_add, dsp_adc, dsp_asr, dsp_lsr, dsp_sub, dsp_sbc, dsp_abs, dsp_ror,
	dsp_add, dsp_adc, dsp_asl, dsp_lsl, dsp_sub, dsp_sbc, dsp_neg, dsp_rol,
	dsp_add, dsp_adc, dsp_asl, dsp_lsl, dsp_sub, dsp_sbc, dsp_neg, dsp_rol,
	
	/* 0x40 - 0x7f */
	dsp_add, dsp_tfr, dsp_or, dsp_eor, dsp_sub, dsp_cmp, dsp_and, dsp_cmpm,
	dsp_add, dsp_tfr, dsp_or, dsp_eor, dsp_sub, dsp_cmp, dsp_and, dsp_cmpm,
	dsp_add, dsp_tfr, dsp_or, dsp_eor, dsp_sub, dsp_cmp, dsp_and, dsp_cmpm,
	dsp_add, dsp_tfr, dsp_or, dsp_eor, dsp_sub, dsp_cmp, dsp_and, dsp_cmpm,
	dsp_add, dsp_tfr, dsp_or, dsp_eor, dsp_sub, dsp_cmp, dsp_and, dsp_cmpm,
	dsp_add, dsp_tfr, dsp_or, dsp_eor, dsp_sub, dsp_cmp, dsp_and, dsp_cmpm,
	dsp_add, dsp_tfr, dsp_or, dsp_eor, dsp_sub, dsp_cmp, dsp_and, dsp_cmpm,
	dsp_add, dsp_tfr, dsp_or, dsp_eor, dsp_sub, dsp_cmp, dsp_and, dsp_cmpm,

	/* 0x80 - 0xbf */
	dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr, dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr,
	dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr, dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr,
	dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr, dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr,
	dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr, dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr,
	dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr, dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr,
	dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr, dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr,
	dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr, dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr,
	dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr, dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr,

	/* 0xc0 - 0xff */
	dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr, dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr,
	dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr, dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr,
	dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr, dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr,
	dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr, dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr,
	dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr, dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr,
	dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr, dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr,
	dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr, dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr,
	dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr, dsp_mpy, dsp_mpyr, dsp_mac, dsp_macr
};


static dsp_emul_t opcodes_parmove[16]={
	dsp_pm_0,
	dsp_pm_1,
	dsp_pm_2,
	dsp_pm_2,
	dsp_pm_4,
	dsp_pm_4,
	dsp_pm_4,
	dsp_pm_4,

	dsp_pm_8,
	dsp_pm_8,
	dsp_pm_8,
	dsp_pm_8,
	dsp_pm_8,
	dsp_pm_8,
	dsp_pm_8,
	dsp_pm_8
};

static int registers_tcc[16][2]={
	{DSP_REG_B,DSP_REG_A},
	{DSP_REG_A,DSP_REG_B},
	{DSP_REG_NULL,DSP_REG_NULL},
	{DSP_REG_NULL,DSP_REG_NULL},

	{DSP_REG_NULL,DSP_REG_NULL},
	{DSP_REG_NULL,DSP_REG_NULL},
	{DSP_REG_NULL,DSP_REG_NULL},
	{DSP_REG_NULL,DSP_REG_NULL},

	{DSP_REG_X0,DSP_REG_A},
	{DSP_REG_X0,DSP_REG_B},
	{DSP_REG_Y0,DSP_REG_A},
	{DSP_REG_Y0,DSP_REG_B},

	{DSP_REG_X1,DSP_REG_A},
	{DSP_REG_X1,DSP_REG_B},
	{DSP_REG_Y1,DSP_REG_A},
	{DSP_REG_Y1,DSP_REG_B}
};

static const char *registers_lmove[8]={
	"a10",
	"b10",
	"x",
	"y",
	"a",
	"b",
	"ab",
	"ba"
};

static int disasm_registers_lmove[8][2]={
	{DSP_REG_A1,DSP_REG_A0},	/* A10 */
	{DSP_REG_B1,DSP_REG_B0},	/* B10 */
	{DSP_REG_X1,DSP_REG_X0},	/* X */
	{DSP_REG_Y1,DSP_REG_Y0},	/* Y */
	{DSP_REG_A,DSP_REG_A},		/* A */
	{DSP_REG_B,DSP_REG_B},		/* B */
	{DSP_REG_A,DSP_REG_B},		/* AB */
	{DSP_REG_B,DSP_REG_A}		/* BA */
};

static const char *ea_names[9]={
	"(r%d)-n%d",	/* 000xxx */
	"(r%d)+n%d",	/* 001xxx */
	"(r%d)-",		/* 010xxx */
	"(r%d)+",		/* 011xxx */
	"(r%d)",		/* 100xxx */
	"(r%d+n%d)",	/* 101xxx */
	"0x%04x",		/* 110000 */
	"-(r%d)",		/* 111xxx */
	"0x%06x"		/* 110100 */
};

static const char *cc_name[16]={
	"cc",
	"ge",
	"ne",
	"pl",
	"nn",
	"ec",
	"lc",
	"gt",
	
	"cs",
	"lt",
	"eq",
	"mi",
	"nr",
	"es",
	"ls",
	"le"
};

static char parallelmove_name[64];

Uint16 dsp56k_disasm(void)
{
	Uint32 value;

	if (prev_inst_pc == dsp_core->pc){
		isLooping = 1;
		return 0;
	}
	prev_inst_pc = dsp_core->pc;
	isLooping = 0;

	cur_inst = read_memory(dsp_core->pc);
	disasm_cur_inst_len = 1;

	strcpy(parallelmove_name, "");

	if (cur_inst < 0x100000) {
		value = (cur_inst >> 11) & (BITMASK(6) << 3);
		value += (cur_inst >> 5) & BITMASK(3);
		opcodes8h[value]();
	} else {
		dsp_pm();
		value = cur_inst & BITMASK(8);
		opcodes_alu[value]();
	}
	return disasm_cur_inst_len;
}

/**
 * dsp56k_getInstrText : return the disasembled instructions
 */
char* dsp56k_getInstructionText(void)
{
	if (isLooping) {
		sprintf(str_instr2, "");
	}
	else if (disasm_cur_inst_len == 1) {
		sprintf(str_instr2, "%04x:  %06x         (%02d cyc)  %s\n", prev_inst_pc, cur_inst, dsp_core->instr_cycle, str_instr);
	} 
	else {
		sprintf(str_instr2, "%04x:  %06x %06x  (%02d cyc)  %s\n", prev_inst_pc, cur_inst, read_memory(prev_inst_pc + 1), dsp_core->instr_cycle, str_instr);
	}

	return str_instr2;
} 

static void dsp_pm_class2(void) {
	Uint32 value;

	dsp_pm();
	value = cur_inst & BITMASK(8);
	opcodes_alu[value]();
} 

void dsp56k_disasm_force_reg_changed(int num_dsp_reg)
{
	registers_changed[num_dsp_reg]=1;
}

static Uint32 read_memory(Uint32 currPc)
{
	Uint32 value;

	if (currPc<0x200) {
		value = dsp_core->ramint[DSP_SPACE_P][currPc];
	} else {
		value = dsp_core->ramext[currPc & (DSP_RAMSIZE-1)];
	}

	return value & BITMASK(24);
}

/**********************************
 *	Conditions code calculation
 **********************************/

static void dsp_calc_cc(Uint32 cc_mode, char *dest)
{
	strcpy(dest, cc_name[cc_mode & BITMASK(4)]);
}

/**********************************
 *	Effective address calculation
 **********************************/

static int dsp_calc_ea(Uint32 ea_mode, char *dest)
{
	int value, retour, numreg;

	value = (ea_mode >> 3) & BITMASK(3);
	numreg = ea_mode & BITMASK(3);
	retour = 0;
	switch (value) {
		case 0:
			/* (Rx)-Nx */
			sprintf(dest, ea_names[value], numreg, numreg);
			registers_changed[DSP_REG_R0+numreg]=1;
			break;
		case 1:
			/* (Rx)+Nx */
			sprintf(dest, ea_names[value], numreg, numreg);
			registers_changed[DSP_REG_R0+numreg]=1;
			break;
		case 5:
			/* (Rx+Nx) */
			sprintf(dest, ea_names[value], numreg, numreg);
			break;
		case 2:
			/* (Rx)- */
			sprintf(dest, ea_names[value], numreg);
			registers_changed[DSP_REG_R0+numreg]=1;
			break;
		case 3:
			/* (Rx)+ */
			sprintf(dest, ea_names[value], numreg);
			registers_changed[DSP_REG_R0+numreg]=1;
			break;
		case 4:
			/* (Rx) */
			sprintf(dest, ea_names[value], numreg);
			break;
		case 7:
			/* -(Rx) */
			sprintf(dest, ea_names[value], numreg);
			registers_changed[DSP_REG_R0+numreg]=1;
			break;
		case 6:
			disasm_cur_inst_len++;
			switch ((ea_mode >> 2) & 1) {
				case 0:
					/* Absolute address */
					sprintf(dest, ea_names[value], read_memory(dsp_core->pc+1));
					break;
				case 1:
					/* Immediate value */
					sprintf(dest, ea_names[8], read_memory(dsp_core->pc+1));
					retour = 1;
					break;
			}
			break;
	}
	return retour;
}

static void opcode8h_0(void)
{
	switch(cur_inst) {
		case 0x000000:
			dsp_nop();
			break;
		case 0x000004:
			dsp_rti();
			break;
		case 0x000005:
			dsp_illegal();
			break;
		case 0x000006:
			dsp_swi();
			break;
		case 0x00000c:
			dsp_rts();
			break;
		case 0x000084:
			dsp_reset();
			break;
		case 0x000086:
			dsp_wait();
			break;
		case 0x000087:
			dsp_stop();
			break;
		case 0x00008c:
			dsp_enddo();
			break;
	}
}

/**********************************
 *	Non-parallel moves instructions
 **********************************/

static void dsp_undefined(void)
{
	sprintf(str_instr," 0x%06x unknown instruction", cur_inst);
}

static void dsp_andi(void)
{
	const char *regname;

	switch(cur_inst & BITMASK(2)) {
		case 0:
			regname="mr";
			registers_changed[DSP_REG_SR]=1;
			break;
		case 1:
			regname="ccr";
			registers_changed[DSP_REG_SR]=1;
			break;
		case 2:
			regname="omr";
			registers_changed[DSP_REG_OMR]=1;
			break;
		default:
			regname="";
			break;
	}

	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," andi #0x%02x,%s",
		(cur_inst>>8) & BITMASK(8),
		regname
	);
}

static void dsp_bchg_aa(void)
{
	/* bchg #n,x:aa */
	/* bchg #n,y:aa */
	char name[16];
	Uint32 memspace, value, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	if (memspace) {
		sprintf(name,"y:0x%04x",value);
	} else {
		sprintf(name,"x:0x%04x",value);
	}
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," bchg #%d,%s", numbit, name);
}

static void dsp_bchg_ea(void)
{
	/* bchg #n,x:ea */
	/* bchg #n,y:ea */
	char name[16], addr_name[16];
	Uint32 memspace, value, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	dsp_calc_ea(value, addr_name);
	if (memspace) {
		sprintf(name,"y:%s",addr_name);
	} else {
		sprintf(name,"x:%s",addr_name);
	}
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," bchg #%d,%s", numbit, name);
}

static void dsp_bchg_pp(void)
{
	/* bchg #n,x:pp */
	/* bchg #n,y:pp */
	char name[16];
	Uint32 memspace, value, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	if (memspace) {
		sprintf(name,"y:0x%04x",value+0xffc0);
	} else {
		sprintf(name,"x:0x%04x",value+0xffc0);
	}
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," bchg #%d,%s", numbit, name);
}

static void dsp_bchg_reg(void)
{
	/* bchg #n,R */
	char name[16];
	Uint32 memspace, value, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	sprintf(name,"%s",registers_name[value]);
	registers_changed[value]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," bchg #%d,%s", numbit, name);
}

static void dsp_bclr_aa(void)
{
	/* bclr #n,x:aa */
	/* bclr #n,y:aa */
	char name[16];
	Uint32 memspace, value, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	if (memspace) {
		sprintf(name,"y:0x%04x",value);
	} else {
		sprintf(name,"x:0x%04x",value);
	}

	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," bclr #%d,%s", numbit, name);
}

static void dsp_bclr_ea(void)
{
	/* bclr #n,x:ea */
	/* bclr #n,y:ea */
	char name[16], addr_name[16];
	Uint32 memspace, value, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	dsp_calc_ea(value, addr_name);
	if (memspace) {
		sprintf(name,"y:%s",addr_name);
	} else {
		sprintf(name,"x:%s",addr_name);
	}

	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," bclr #%d,%s", numbit, name);
}

static void dsp_bclr_pp(void)
{
	/* bclr #n,x:pp */
	/* bclr #n,y:pp */
	char name[16];
	Uint32 memspace, value, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	if (memspace) {
		sprintf(name,"y:0x%04x",value+0xffc0);
	} else {
		sprintf(name,"x:0x%04x",value+0xffc0);
	}

	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," bclr #%d,%s", numbit, name);
}

static void dsp_bclr_reg(void)
{
	/* bclr #n,R */
	char name[16];
	Uint32 memspace, value, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	sprintf(name,"%s",registers_name[value]);
	registers_changed[value]=1;

	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," bclr #%d,%s", numbit, name);
}

static void dsp_bset_aa(void)
{
	/* bset #n,x:aa */
	/* bset #n,y:aa */
	char name[16];
	Uint32 memspace, value, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	if (memspace) {
		sprintf(name,"y:0x%04x",value);
	} else {
		sprintf(name,"x:0x%04x",value);
	}

	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," bset #%d,%s", numbit, name);
}

static void dsp_bset_ea(void)
{
	/* bset #n,x:ea */
	/* bset #n,y:ea */
	char name[16], addr_name[16];
	Uint32 memspace, value, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	dsp_calc_ea(value, addr_name);
	if (memspace) {
		sprintf(name,"y:%s",addr_name);
	} else {
		sprintf(name,"x:%s",addr_name);
	}

	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," bset #%d,%s", numbit, name);
}

static void dsp_bset_pp(void)
{
	/* bset #n,x:pp */
	/* bset #n,y:pp */
	char name[16];
	Uint32 memspace, value, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	if (memspace) {
		sprintf(name,"y:0x%04x",value+0xffc0);
	} else {
		sprintf(name,"x:0x%04x",value+0xffc0);
	}

	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," bset #%d,%s", numbit, name);
}

static void dsp_bset_reg(void)
{
	/* bset #n,R */
	char name[16];
	Uint32 memspace, value, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	sprintf(name,"%s",registers_name[value]);
	registers_changed[value]=1;

	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," bset #%d,%s", numbit, name);
}

static void dsp_btst_aa(void)
{
	/* btst #n,x:aa */
	/* btst #n,y:aa */
	char name[16];
	Uint32 memspace, value, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	if (memspace) {
		sprintf(name,"y:0x%04x",value);
	} else {
		sprintf(name,"x:0x%04x",value);
	}

	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," btst #%d,%s", numbit, name);
}

static void dsp_btst_ea(void)
{
	/* btst #n,x:ea */
	/* btst #n,y:ea */
	char name[16], addr_name[16];
	Uint32 memspace, value, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	dsp_calc_ea(value, addr_name);
	if (memspace) {
		sprintf(name,"y:%s",addr_name);
	} else {
		sprintf(name,"x:%s",addr_name);
	}

	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," btst #%d,%s", numbit, name);
}

static void dsp_btst_pp(void)
{
	/* btst #n,x:pp */
	/* btst #n,y:pp */
	char name[16];
	Uint32 memspace, value, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	if (memspace) {
		sprintf(name,"y:0x%04x",value+0xffc0);
	} else {
		sprintf(name,"x:0x%04x",value+0xffc0);
	}

	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," btst #%d,%s", numbit, name);
}

static void dsp_btst_reg(void)
{
	/* btst #n,R */
	char name[16];
	Uint32 memspace, value, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	sprintf(name,"%s",registers_name[value]);
	registers_changed[value]=1;

	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," btst #%d,%s", numbit, name);
}

static void dsp_div(void)
{
	Uint32 srcreg=DSP_REG_NULL, destreg;
	
	switch((cur_inst>>4) & BITMASK(2)) {
		case 0:
			srcreg = DSP_REG_X0;
				break;
		case 1:
			srcreg = DSP_REG_Y0;
				break;
		case 2:
			srcreg = DSP_REG_X1;
				break;
		case 3:
			srcreg = DSP_REG_Y1;
				break;
	}
	destreg = DSP_REG_A+((cur_inst>>3) & 1);
	registers_changed[destreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," div %s,%s", registers_name[srcreg],registers_name[destreg]);
}

static void dsp_do_aa(void)
{
	char name[16];

	disasm_cur_inst_len++;

	if (cur_inst & (1<<6)) {
		sprintf(name, "y:0x%04x", (cur_inst>>8) & BITMASK(6));
	} else {
		sprintf(name, "x:0x%04x", (cur_inst>>8) & BITMASK(6));
	}

	sprintf(str_instr," do %s,p:0x%04x",
		name,
		read_memory(dsp_core->pc+1)
	);
	registers_changed[DSP_REG_LA]=1;
	registers_changed[DSP_REG_LC]=1;
	registers_changed[DSP_REG_SR]=1;}

static void dsp_do_imm(void)
{
	disasm_cur_inst_len++;

	sprintf(str_instr," do #0x%04x,p:0x%04x",
		((cur_inst>>8) & BITMASK(8))|((cur_inst & BITMASK(4))<<8),
		read_memory(dsp_core->pc+1)
	);
	registers_changed[DSP_REG_LA]=1;
	registers_changed[DSP_REG_LC]=1;
	registers_changed[DSP_REG_SR]=1;}

static void dsp_do_ea(void)
{
	char addr_name[16], name[16];
	Uint32 ea_mode;
	
	disasm_cur_inst_len++;

	ea_mode = (cur_inst>>8) & BITMASK(6);
	dsp_calc_ea(ea_mode, addr_name);

	if (cur_inst & (1<<6)) {
		sprintf(name, "y:%s", addr_name);
	} else {
		sprintf(name, "x:%s", addr_name);
	}

	sprintf(str_instr," do %s,p:0x%04x", 
		name,
		read_memory(dsp_core->pc+1)
	);
	registers_changed[DSP_REG_LA]=1;
	registers_changed[DSP_REG_LC]=1;
	registers_changed[DSP_REG_SR]=1;}

static void dsp_do_reg(void)
{
	disasm_cur_inst_len++;

	sprintf(str_instr," do %s,p:0x%04x",
		registers_name[(cur_inst>>8) & BITMASK(6)],
		read_memory(dsp_core->pc+1)
	);
	registers_changed[DSP_REG_LA]=1;
	registers_changed[DSP_REG_LC]=1;
	registers_changed[DSP_REG_SR]=1;}

static void dsp_enddo(void)
{
	sprintf(str_instr," enddo");
}

static void dsp_illegal(void)
{
	sprintf(str_instr," illegal");
}

static void dsp_jcc_ea(void)
{
	char cond_name[16], addr_name[16];
	Uint32 cc_code=0;
	
	dsp_calc_ea((cur_inst >>8) & BITMASK(6), addr_name);
	cc_code=cur_inst & BITMASK(4);
	dsp_calc_cc(cc_code, cond_name);	

	sprintf(str_instr," j%s p:%s", cond_name, addr_name);
}

static void dsp_jcc_imm(void)
{
	char cond_name[16], addr_name[16];
	Uint32 cc_code=0;
	
	sprintf(addr_name, "0x%04x", cur_inst & BITMASK(12));
	cc_code=(cur_inst>>12) & BITMASK(4);
	dsp_calc_cc(cc_code, cond_name);	

	sprintf(str_instr," j%s p:%s", cond_name, addr_name);
}

static void dsp_jclr_aa(void)
{
	/* jclr #n,x:aa,p:xx */
	/* jclr #n,y:aa,p:xx */
	char srcname[16];
	Uint32 memspace, value, numbit;
	
	disasm_cur_inst_len++;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	if (memspace) {
		sprintf(srcname, "y:0x%04x", value);
	} else {
		sprintf(srcname, "x:0x%04x", value);
	}

	sprintf(str_instr," jclr #%d,%s,p:0x%04x",
		numbit,
		srcname,
		read_memory(dsp_core->pc+1)
	);
}

static void dsp_jclr_ea(void)
{
	/* jclr #n,x:ea,p:xx */
	/* jclr #n,y:ea,p:xx */
	char srcname[16], addr_name[16];
	Uint32 memspace, value, numbit;
	
	disasm_cur_inst_len++;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	dsp_calc_ea(value, addr_name);
	if (memspace) {
		sprintf(srcname, "y:%s", addr_name);
	} else {
		sprintf(srcname, "x:%s", addr_name);
	}

	sprintf(str_instr," jclr #%d,%s,p:0x%04x",
		numbit,
		srcname,
		read_memory(dsp_core->pc+1)
	);
}

static void dsp_jclr_pp(void)
{
	/* jclr #n,x:pp,p:xx */
	/* jclr #n,y:pp,p:xx */
	char srcname[16];
	Uint32 memspace, value, numbit;
	
	disasm_cur_inst_len++;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	value += 0xffc0;
	if (memspace) {
		sprintf(srcname, "y:0x%04x", value);
	} else {
		sprintf(srcname, "x:0x%04x", value);
	}

	sprintf(str_instr," jclr #%d,%s,p:0x%04x",
		numbit,
		srcname,
		read_memory(dsp_core->pc+1)
	);
}

static void dsp_jclr_reg(void)
{
	/* jclr #n,R,p:xx */
	char srcname[16];
	Uint32 memspace, value, numbit;
	
	disasm_cur_inst_len++;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	strcpy(srcname, registers_name[value]);

	sprintf(str_instr," jclr #%d,%s,p:0x%04x",
		numbit,
		srcname,
		read_memory(dsp_core->pc+1)
	);
}

static void dsp_jmp_imm(void)
{
	char dstname[16];

	sprintf(dstname, "0x%04x", cur_inst & BITMASK(12));

	sprintf(str_instr," jmp p:%s", dstname);
}

static void dsp_jmp_ea(void)
{
	char dstname[16];

	dsp_calc_ea((cur_inst >>8) & BITMASK(6), dstname);

	sprintf(str_instr," jmp p:%s", dstname);
}

static void dsp_jscc_ea(void)
{
	char cond_name[16], addr_name[16];
	Uint32 cc_code=0;
	
	dsp_calc_ea((cur_inst>>8) & BITMASK(6), addr_name);
	cc_code=cur_inst & BITMASK(4);
	dsp_calc_cc(cc_code, cond_name);	

	sprintf(str_instr," js%s p:%s", cond_name, addr_name);
}
	
static void dsp_jscc_imm(void)
{
	char cond_name[16], addr_name[16];
	Uint32 cc_code=0;
	
	sprintf(addr_name, "0x%04x", cur_inst & BITMASK(12));
	cc_code=(cur_inst>>12) & BITMASK(4);
	dsp_calc_cc(cc_code, cond_name);	

	sprintf(str_instr," js%s p:%s", cond_name, addr_name);
}

static void dsp_jsclr_aa(void)
{
	/* jsclr #n,x:aa,p:xx */
	/* jsclr #n,y:aa,p:xx */
	char srcname[16];
	Uint32 memspace, value, numbit;
	
	disasm_cur_inst_len++;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	if (memspace) {
		sprintf(srcname, "y:0x%04x", value);
	} else {
		sprintf(srcname, "x:0x%04x", value);
	}

	sprintf(str_instr," jsclr #%d,%s,p:0x%04x",
		numbit,
		srcname,
		read_memory(dsp_core->pc+1)
	);
}

static void dsp_jsclr_ea(void)
{
	/* jsclr #n,x:ea,p:xx */
	/* jsclr #n,y:ea,p:xx */
	char srcname[16], addr_name[16];
	Uint32 memspace, value, numbit;
	
	disasm_cur_inst_len++;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	dsp_calc_ea(value, addr_name);
	if (memspace) {
		sprintf(srcname, "y:%s", addr_name);
	} else {
		sprintf(srcname, "x:%s", addr_name);
	}

	sprintf(str_instr," jsclr #%d,%s,p:0x%04x",
		numbit,
		srcname,
		read_memory(dsp_core->pc+1)
	);
}

static void dsp_jsclr_pp(void)
{
	/* jsclr #n,x:pp,p:xx */
	/* jsclr #n,y:pp,p:xx */
	char srcname[16];
	Uint32 memspace, value, numbit;
	
	disasm_cur_inst_len++;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	value += 0xffc0;
	if (memspace) {
		sprintf(srcname, "y:0x%04x", value);
	} else {
		sprintf(srcname, "x:0x%04x", value);
	}

	sprintf(str_instr," jsclr #%d,%s,p:0x%04x",
		numbit,
		srcname,
		read_memory(dsp_core->pc+1)
	);
}

static void dsp_jsclr_reg(void)
{
	/* jsclr #n,R,p:xx */
	char srcname[16];
	Uint32 memspace, value, numbit;
	
	disasm_cur_inst_len++;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	strcpy(srcname, registers_name[value]);

	sprintf(str_instr," jsclr #%d,%s,p:0x%04x",
		numbit,
		srcname,
		read_memory(dsp_core->pc+1)
	);
}

static void dsp_jset_aa(void)
{
	/* jset #n,x:aa,p:xx */
	/* jset #n,y:aa,p:xx */
	char srcname[16];
	Uint32 memspace, value, numbit;
	
	disasm_cur_inst_len++;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	if (memspace) {
		sprintf(srcname, "y:0x%04x", value);
	} else {
		sprintf(srcname, "x:0x%04x", value);
	}

	sprintf(str_instr," jset #%d,%s,p:0x%04x",
		numbit,
		srcname,
		read_memory(dsp_core->pc+1)
	);
}

static void dsp_jset_ea(void)
{
	/* jset #n,x:ea,p:xx */
	/* jset #n,y:ea,p:xx */
	char srcname[16], addr_name[16];
	Uint32 memspace, value, numbit;
	
	disasm_cur_inst_len++;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	dsp_calc_ea(value, addr_name);
	if (memspace) {
		sprintf(srcname, "y:%s", addr_name);
	} else {
		sprintf(srcname, "x:%s", addr_name);
	}

	sprintf(str_instr," jset #%d,%s,p:0x%04x",
		numbit,
		srcname,
		read_memory(dsp_core->pc+1)
	);
}

static void dsp_jset_pp(void)
{
	/* jset #n,x:pp,p:xx */
	/* jset #n,y:pp,p:xx */
	char srcname[16];
	Uint32 memspace, value, numbit;
	
	disasm_cur_inst_len++;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	value += 0xffc0;
	if (memspace) {
		sprintf(srcname, "y:0x%04x", value);
	} else {
		sprintf(srcname, "x:0x%04x", value);
	}

	sprintf(str_instr," jset #%d,%s,p:0x%04x",
		numbit,
		srcname,
		read_memory(dsp_core->pc+1)
	);
}

static void dsp_jset_reg(void)
{
	/* jset #n,R,p:xx */
	char srcname[16];
	Uint32 memspace, value, numbit;
	
	disasm_cur_inst_len++;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	strcpy(srcname, registers_name[value]);

	sprintf(str_instr," jset #%d,%s,p:0x%04x",
		numbit,
		srcname,
		read_memory(dsp_core->pc+1)
	);
}

static void dsp_jsr_imm(void)
{
	char dstname[16];

	sprintf(dstname, "0x%04x", cur_inst & BITMASK(12));

	sprintf(str_instr," jsr p:%s", dstname);
}

static void dsp_jsr_ea(void)
{
	char dstname[16];

	dsp_calc_ea((cur_inst>>8) & BITMASK(6),dstname);

	sprintf(str_instr," jsr p:%s", dstname);
}

static void dsp_jsset_aa(void)
{
	/* jsset #n,x:aa,p:xx */
	/* jsset #n,y:aa,p:xx */
	char srcname[16];
	Uint32 memspace, value, numbit;
	
	disasm_cur_inst_len++;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	if (memspace) {
		sprintf(srcname, "y:0x%04x", value);
	} else {
		sprintf(srcname, "x:0x%04x", value);
	}

	sprintf(str_instr," jsset #%d,%s,p:0x%04x",
		numbit,
		srcname,
		read_memory(dsp_core->pc+1)
	);
}

static void dsp_jsset_ea(void)
{
	/* jsset #n,x:ea,p:xx */
	/* jsset #n,y:ea,p:xx */
	char srcname[16], addr_name[16];
	Uint32 memspace, value, numbit;
	
	disasm_cur_inst_len++;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	dsp_calc_ea(value, addr_name);
	if (memspace) {
		sprintf(srcname, "y:%s", addr_name);
	} else {
		sprintf(srcname, "x:%s", addr_name);
	}

	sprintf(str_instr," jsset #%d,%s,p:0x%04x",
		numbit,
		srcname,
		read_memory(dsp_core->pc+1)
	);
}

static void dsp_jsset_pp(void)
{
	/* jsset #n,x:pp,p:xx */
	/* jsset #n,y:pp,p:xx */
	char srcname[16];
	Uint32 memspace, value, numbit;
	
	disasm_cur_inst_len++;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	value += 0xffc0;
	if (memspace) {
		sprintf(srcname, "y:0x%04x", value);
	} else {
		sprintf(srcname, "x:0x%04x", value);
	}

	sprintf(str_instr," jsset #%d,%s,p:0x%04x",
		numbit,
		srcname,
		read_memory(dsp_core->pc+1)
	);
}

static void dsp_jsset_reg(void)
{
	/* jsset #n,r,p:xx */
	char srcname[16];
	Uint32 memspace, value, numbit;
	
	disasm_cur_inst_len++;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	strcpy(srcname, registers_name[value]);

	sprintf(str_instr," jsset #%d,%s,p:0x%04x",
		numbit,
		srcname,
		read_memory(dsp_core->pc+1)
	);
}

static void dsp_lua(void)
{
	char addr_name[16], numreg;

	dsp_calc_ea((cur_inst>>8) & BITMASK(5), addr_name);
	numreg = cur_inst & BITMASK(3);
	registers_changed[DSP_REG_R0+numreg]=1;
	
	sprintf(str_instr," lua %s,r%d", addr_name, numreg);
}

static void dsp_movec_reg(void)
{
	Uint32 numreg1, numreg2;

	/* S1,D2 */
	/* S2,D1 */

	numreg2 = (cur_inst>>8) & BITMASK(6);
	numreg1 = cur_inst & BITMASK(6);

	if (cur_inst & (1<<15)) {
		/* Write D1 */
		sprintf(str_instr," movec %s,%s", registers_name[numreg2], registers_name[numreg1]);
		registers_changed[numreg1]=1;
	} else {
		/* Read S1 */
		sprintf(str_instr," movec %s,%s", registers_name[numreg1], registers_name[numreg2]);
		registers_changed[numreg2]=1;
	}
	registers_changed[DSP_REG_SR]=1;
}

static void dsp_movec_aa(void)
{
	const char *spacename;
	char srcname[16],dstname[16];
	Uint32 numreg, addr;

	/* x:aa,D1 */
	/* S1,x:aa */
	/* y:aa,D1 */
	/* S1,y:aa */

	numreg = cur_inst & BITMASK(6);
	addr = (cur_inst>>8) & BITMASK(6);

	if (cur_inst & (1<<6)) {
		spacename="y";
	} else {
		spacename="x";
	}

	if (cur_inst & (1<<15)) {
		/* Write D1 */
		sprintf(srcname, "%s:0x%04x", spacename, addr);
		strcpy(dstname, registers_name[numreg]);
	} else {
		/* Read S1 */
		strcpy(srcname, registers_name[numreg]);
		sprintf(dstname, "%s:0x%04x", spacename, addr);
	}

	sprintf(str_instr," movec %s,%s", srcname, dstname);
	registers_changed[DSP_REG_SR]=1;
}

static void dsp_movec_imm(void)
{
	Uint32 numreg;

	/* #xx,D1 */

	numreg = cur_inst & BITMASK(6);

	registers_changed[numreg]=1;
	sprintf(str_instr," movec #0x%02x,%s", (cur_inst>>8) & BITMASK(8), registers_name[numreg]);
	registers_changed[DSP_REG_SR]=1;
}

static void dsp_movec_ea(void)
{
	const char *spacename;
	char srcname[16], dstname[16], addr_name[16];
	Uint32 numreg, ea_mode;
	int retour;

	/* x:ea,D1 */
	/* S1,x:ea */
	/* y:ea,D1 */
	/* S1,y:ea */
	/* #xxxx,D1 */

	numreg = cur_inst & BITMASK(6);
	ea_mode = (cur_inst>>8) & BITMASK(6);
	retour = dsp_calc_ea(ea_mode, addr_name);

	if (cur_inst & (1<<6)) {
		spacename="y";
	} else {
		spacename="x";
	}

	if (cur_inst & (1<<15)) {
		/* Write D1 */
		if (retour) {
			sprintf(srcname, "#%s", addr_name);
		} else {
			sprintf(srcname, "%s:%s", spacename, addr_name);
		}
		registers_changed[numreg]=1;
		strcpy(dstname, registers_name[numreg]);
	} else {
		/* Read S1 */
		strcpy(srcname, registers_name[numreg]);
		sprintf(dstname, "%s:%s", spacename, addr_name);
	}

	sprintf(str_instr," movec %s,%s", srcname, dstname);
	registers_changed[DSP_REG_SR]=1;
}

static void dsp_movem_aa(void)
{
	/* S,p:aa */
	/* p:aa,D */
	char addr_name[16], srcname[16], dstname[16];
	Uint32 numreg;

	sprintf(addr_name, "0x%04x",(cur_inst>>8) & BITMASK(6));
	numreg = cur_inst & BITMASK(6);
	if  (cur_inst & (1<<15)) {
		/* Write D */
		registers_changed[numreg]=1;
		sprintf(srcname, "p:%s", addr_name);
		strcpy(dstname, registers_name[numreg]);
	} else {
		/* Read S */
		strcpy(srcname, registers_name[numreg]);
		sprintf(dstname, "p:%s", addr_name);
	}
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," movem %s,%s", srcname, dstname);
}

static void dsp_movem_ea(void)
{
	/* S,p:ea */
	/* p:ea,D */
	char addr_name[16], srcname[16], dstname[16];
	Uint32 ea_mode, numreg;

	ea_mode = (cur_inst>>8) & BITMASK(6);
	dsp_calc_ea(ea_mode, addr_name);
	numreg = cur_inst & BITMASK(6);
	if  (cur_inst & (1<<15)) {
		/* Write D */
		registers_changed[numreg]=1;
		sprintf(srcname, "p:%s", addr_name);
		strcpy(dstname, registers_name[numreg]);
	} else {
		/* Read S */
		strcpy(srcname, registers_name[numreg]);
		sprintf(dstname, "p:%s", addr_name);
	}
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," movem %s,%s", srcname, dstname);
}

static void dsp_movep_0(void)
{
	char srcname[16]="",dstname[16]="";
	Uint32 addr, memspace, numreg;

	/* S,x:pp */
	/* x:pp,D */
	/* S,y:pp */
	/* y:pp,D */

	addr = 0xffc0 + (cur_inst & BITMASK(6));
	memspace = (cur_inst>>16) & 1;
	numreg = (cur_inst>>8) & BITMASK(6);

	if (cur_inst & (1<<15)) {
		/* Write pp */

		strcpy(srcname, registers_name[numreg]);

		if (memspace) {
			sprintf(dstname, "y:0x%04x", addr);
		} else {
			sprintf(dstname, "x:0x%04x", addr);
		}
	} else {
		/* Read pp */

		if (memspace) {
			sprintf(srcname, "y:0x%04x", addr);
		} else {
			sprintf(srcname, "x:0x%04x", addr);
		}

		registers_changed[numreg]=1;
		strcpy(dstname, registers_name[numreg]);
	}

	sprintf(str_instr," movep %s,%s", srcname, dstname);
	registers_changed[DSP_REG_SR]=1;
}

static void dsp_movep_1(void)
{
	char srcname[16]="",dstname[16]="",name[16]="";
	Uint32 addr, memspace; 

	/* p:ea,x:pp */
	/* x:pp,p:ea */
	/* p:ea,y:pp */
	/* y:pp,p:ea */

	addr = 0xffc0 + (cur_inst & BITMASK(6));
	dsp_calc_ea((cur_inst>>8) & BITMASK(6), name);
	memspace = (cur_inst>>16) & 1;

	if (cur_inst & (1<<15)) {
		/* Write pp */

		sprintf(srcname, "p:%s", name);

		if (memspace) {
			sprintf(dstname, "y:0x%04x", addr);
		} else {
			sprintf(dstname, "x:0x%04x", addr);
		}
	} else {
		/* Read pp */

		if (memspace) {
			sprintf(srcname, "y:0x%04x", addr);
		} else {
			sprintf(srcname, "x:0x%04x", addr);
		}

		sprintf(dstname, "p:%s", name);
	}

	sprintf(str_instr," movep %s,%s", srcname, dstname);
	registers_changed[DSP_REG_SR]=1;
}

static void dsp_movep_23(void)
{
	char srcname[16]="",dstname[16]="",name[16]="";
	Uint32 addr, memspace, easpace, retour; 

	/* x:ea,x:pp */
	/* y:ea,x:pp */
	/* #xxxxxx,x:pp */
	/* x:pp,x:ea */
	/* x:pp,y:ea */

	/* x:ea,y:pp */
	/* y:ea,y:pp */
	/* #xxxxxx,y:pp */
	/* y:pp,y:ea */
	/* y:pp,x:ea */

	addr = 0xffc0 + (cur_inst & BITMASK(6));
	retour = dsp_calc_ea((cur_inst>>8) & BITMASK(6), name);
	memspace = (cur_inst>>16) & 1;
	easpace = (cur_inst>>6) & 1;

	if (cur_inst & (1<<15)) {
		/* Write pp */

		if (retour) {
			sprintf(srcname, "#%s", name);
		} else {
			if (easpace) {
				sprintf(srcname, "y:%s", name);
			} else {
				sprintf(srcname, "x:%s", name);
			}
		}

		if (memspace) {
			sprintf(dstname, "y:0x%04x", addr);
		} else {
			sprintf(dstname, "x:0x%04x", addr);
		}
	} else {
		/* Read pp */

		if (memspace) {
			sprintf(srcname, "y:0x%04x", addr);
		} else {
			sprintf(srcname, "x:0x%04x", addr);
		}

		if (easpace) {
			sprintf(dstname, "y:%s", name);
		} else {
			sprintf(dstname, "x:%s", name);
		}
	}

	sprintf(str_instr," movep %s,%s", srcname, dstname);
	registers_changed[DSP_REG_SR]=1;
}

static void dsp_nop(void)
{
	sprintf(str_instr," nop");
}

static void dsp_norm(void)
{
	Uint32 srcreg, destreg;

	srcreg = DSP_REG_R0+((cur_inst>>8) & BITMASK(3));
	destreg = DSP_REG_A+((cur_inst>>3) & 1);

	registers_changed[srcreg]=1;
	registers_changed[destreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," norm %s,%s", registers_name[srcreg], registers_name[destreg]);
}

static void dsp_ori(void)
{
	const char *regname;

	switch(cur_inst & BITMASK(2)) {
		case 0:
			regname="mr";
			registers_changed[DSP_REG_SR]=1;
			break;
		case 1:
			regname="ccr";
			registers_changed[DSP_REG_SR]=1;
			break;
		case 2:
			regname="omr";
			registers_changed[DSP_REG_OMR]=1;
			break;
		default:
			regname="";
			break;
	}

	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," ori #0x%02x,%s", (cur_inst>>8) & BITMASK(8), regname);
}

static void dsp_rep_aa(void)
{
	char name[16];

	/* x:aa */
	/* y:aa */

	if (cur_inst & (1<<6)) {
		sprintf(name, "y:0x%04x",(cur_inst>>8) & BITMASK(6));
	} else {
		sprintf(name, "x:0x%04x",(cur_inst>>8) & BITMASK(6));
	}

	sprintf(str_instr," rep %s", name);
	registers_changed[DSP_REG_LC]=1;
	registers_changed[DSP_REG_SR]=1;
}

static void dsp_rep_imm(void)
{
	/* #xxx */
	sprintf(str_instr," rep #0x%02x", ((cur_inst>>8) & BITMASK(8))
		+ ((cur_inst & BITMASK(4))<<8));
	registers_changed[DSP_REG_LC]=1;
	registers_changed[DSP_REG_SR]=1;
}

static void dsp_rep_ea(void)
{
	char name[16],addr_name[16];

	/* x:ea */
	/* y:ea */

	dsp_calc_ea((cur_inst>>8) & BITMASK(6), addr_name);
	if (cur_inst & (1<<6)) {
		sprintf(name, "y:%s",addr_name);
	} else {
		sprintf(name, "x:%s",addr_name);
	}

	sprintf(str_instr," rep %s", name);
	registers_changed[DSP_REG_LC]=1;
	registers_changed[DSP_REG_SR]=1;
}

static void dsp_rep_reg(void)
{
	/* R */

	sprintf(str_instr," rep %s", registers_name[(cur_inst>>8) & BITMASK(6)]);
	registers_changed[DSP_REG_LC]=1;
	registers_changed[DSP_REG_SR]=1;
}

static void dsp_reset(void)
{
	sprintf(str_instr," reset");
}

static void dsp_rti(void)
{
	registers_changed[DSP_REG_SR]=1;
	sprintf(str_instr," rti");
}

static void dsp_rts(void)
{
	registers_changed[DSP_REG_SR]=1;
	sprintf(str_instr," rts");
}

static void dsp_stop(void)
{
	sprintf(str_instr," stop");
}
	
static void dsp_swi(void)
{
	sprintf(str_instr," swi");
}

static void dsp_tcc(void)
{
	char ccname[16];
	Uint32 src1reg, dst1reg, src2reg, dst2reg;

	dsp_calc_cc((cur_inst>>12) & BITMASK(4), ccname);
	src1reg = registers_tcc[(cur_inst>>3) & BITMASK(4)][0];
	dst1reg = registers_tcc[(cur_inst>>3) & BITMASK(4)][1];

	registers_changed[dst1reg]=1;
	if (cur_inst & (1<<16)) {
		src2reg = DSP_REG_R0+(cur_inst & BITMASK(3));
		dst2reg = DSP_REG_R0+((cur_inst>>8) & BITMASK(3));

		registers_changed[dst2reg]=1;
		sprintf(str_instr," t%s %s,%s %s,%s",
			ccname,
			registers_name[src1reg],
			registers_name[dst1reg],
			registers_name[src2reg],
			registers_name[dst2reg]
		);
	} else {
		sprintf(str_instr," t%s %s,%s",
			ccname,
			registers_name[src1reg],
			registers_name[dst1reg]
		);
	}
}

static void dsp_wait(void)
{
	sprintf(str_instr," wait");
}

/**********************************
 *	Parallel moves
 **********************************/

static void dsp_pm(void)
{
	Uint32 value;

	value = (cur_inst >> 20) & BITMASK(4);

	opcodes_parmove[value]();
}

static void dsp_pm_0(void)
{
	char space_name[16], addr_name[16];
	Uint32 memspace, numreg1, numreg2;
/*
	0000 100d 00mm mrrr S,x:ea	x0,D
	0000 100d 10mm mrrr S,y:ea	y0,D
*/
	memspace = (cur_inst>>15) & 1;
	numreg1 = DSP_REG_A+((cur_inst>>16) & 1);
	dsp_calc_ea((cur_inst>>8) & BITMASK(6), addr_name);

	if (memspace) {
		strcpy(space_name,"y");
		numreg2 = DSP_REG_Y0;
	} else {
		strcpy(space_name,"x");
		numreg2 = DSP_REG_X0;
	}

	registers_changed[numreg1]=1;

	sprintf(parallelmove_name,
		"%s,%s:%s %s,%s",
		registers_name[numreg1],
		space_name,
		addr_name,
		registers_name[numreg2],
		registers_name[numreg1]
	);
}

static void dsp_pm_1(void)
{
/*
	0001 ffdf w0mm mrrr x:ea,D1		S2,D2
						S1,x:ea		S2,D2
						#xxxxxx,D1	S2,D2
	0001 deff w1mm mrrr S1,D1		y:ea,D2
						S1,D1		S2,y:ea
						S1,D1		#xxxxxx,D2
*/

	char addr_name[16];
	Uint32 memspace, write_flag, retour, s1reg, s2reg, d1reg, d2reg;

	memspace = (cur_inst>>14) & 1;
	write_flag = (cur_inst>>15) & 1;
	retour = dsp_calc_ea((cur_inst>>8) & BITMASK(6), addr_name);

	if (memspace==DSP_SPACE_Y) {
		s2reg = d2reg = DSP_REG_Y0;
		switch((cur_inst>>16) & BITMASK(2)) {
			case 0:	s2reg = d2reg = DSP_REG_Y0;	break;
			case 1:	s2reg = d2reg = DSP_REG_Y1;	break;
			case 2:	s2reg = d2reg = DSP_REG_A;	break;
			case 3:	s2reg = d2reg = DSP_REG_B;	break;
		}

		s1reg = DSP_REG_A+((cur_inst>>19) & 1);
		d1reg = DSP_REG_X0+((cur_inst>>18) & 1);

		registers_changed[d1reg]=1;

		if (write_flag) {
			/* Write D2 */

			registers_changed[d2reg]=1;

			if (retour) {
				sprintf(parallelmove_name,"%s,%s #%s,%s",
					registers_name[s1reg],
					registers_name[d1reg],
					addr_name,
					registers_name[d2reg]
				);
			} else {
				sprintf(parallelmove_name,"%s,%s y:%s,%s",
					registers_name[s1reg],
					registers_name[d1reg],
					addr_name,
					registers_name[d2reg]
				);
			}
		} else {
			/* Read S2 */
			sprintf(parallelmove_name,"%s,%s %s,y:%s",
				registers_name[s1reg],
				registers_name[d1reg],
				registers_name[s2reg],
				addr_name
			);
		}		

	} else {
		s1reg = d1reg = DSP_REG_X0;
		switch((cur_inst>>18) & BITMASK(2)) {
			case 0:	s1reg = d1reg = DSP_REG_X0;	break;
			case 1:	s1reg = d1reg = DSP_REG_X1;	break;
			case 2:	s1reg = d1reg = DSP_REG_A;	break;
			case 3:	s1reg = d1reg = DSP_REG_B;	break;
		}

		s2reg = DSP_REG_A+((cur_inst>>17) & 1);
		d2reg = DSP_REG_Y0+((cur_inst>>16) & 1);

		registers_changed[d2reg]=1;

		if (write_flag) {
			/* Write D1 */

			registers_changed[d1reg]=1;

			if (retour) {
				sprintf(parallelmove_name,"#%s,%s %s,%s",
					addr_name,
					registers_name[d1reg],
					registers_name[s2reg],
					registers_name[d2reg]
				);
			} else {
				sprintf(parallelmove_name,"x:%s,%s %s,%s",
					addr_name,
					registers_name[d1reg],
					registers_name[s2reg],
					registers_name[d2reg]
				);
			}
		} else {
			/* Read S1 */
			sprintf(parallelmove_name,"%s,x:%s %s,%s",
				registers_name[s1reg],
				addr_name,
				registers_name[s2reg],
				registers_name[d2reg]
			);
		}		
	
	}
}

static void dsp_pm_2(void)
{
	char addr_name[16];
	Uint32 numreg1, numreg2;
/*
	0010 0000 0000 0000 nop
	0010 0000 010m mrrr R update
	0010 00ee eeed dddd S,D
	001d dddd iiii iiii #xx,D
*/
	if (((cur_inst >> 8) & 0xffff) == 0x2000) {
		return;
	}

	if (((cur_inst >> 8) & 0xffe0) == 0x2040) {
		dsp_calc_ea((cur_inst>>8) & BITMASK(5), addr_name);
		registers_changed[DSP_REG_R0+((cur_inst>>8) & BITMASK(3))]=1;
		sprintf(parallelmove_name, "%s,r%d",addr_name, (cur_inst>>8) & BITMASK(3));
		return;
	}

	if (((cur_inst >> 8) & 0xfc00) == 0x2000) {
		numreg1 = (cur_inst>>13) & BITMASK(5);
		numreg2 = (cur_inst>>8) & BITMASK(5);
		registers_changed[numreg2]=1;
		sprintf(parallelmove_name, "%s,%s", registers_name[numreg1], registers_name[numreg2]); 
		return;
	}

	numreg1 = (cur_inst>>16) & BITMASK(5);
	registers_changed[numreg1]=1;
	sprintf(parallelmove_name, "#0x%02x,%s", (cur_inst >> 8) & BITMASK(8), registers_name[numreg1]);
}

static void dsp_pm_4(void)
{
	char addr_name[16];
	Uint32 value, retour, ea_mode, memspace;
/*
	0100 l0ll w0aa aaaa l:aa,D
						S,l:aa
	0100 l0ll w1mm mrrr l:ea,D
						S,l:ea
	01dd 0ddd w0aa aaaa x:aa,D
						S,x:aa
	01dd 0ddd w1mm mrrr x:ea,D
						S,x:ea
						#xxxxxx,D
	01dd 1ddd w0aa aaaa y:aa,D
						S,y:aa
	01dd 1ddd w1mm mrrr y:ea,D
						S,y:ea
						#xxxxxx,D
*/
	value = (cur_inst>>16) & BITMASK(3);
	value |= (cur_inst>>17) & (BITMASK(2)<<3);

	ea_mode = (cur_inst>>8) & BITMASK(6);

	if ((value>>2)==0) {
		/* L: memory move */
		if (cur_inst & (1<<14)) {
			retour = dsp_calc_ea(ea_mode, addr_name);	
		} else {
			sprintf(addr_name,"0x%04x", ea_mode);
			retour = 0;
		}

		value = (cur_inst>>16) & BITMASK(2);
		value |= (cur_inst>>17) & (1<<2);

		if (cur_inst & (1<<15)) {
			/* Write D */

			registers_changed[disasm_registers_lmove[value][0]]=1;
			registers_changed[disasm_registers_lmove[value][1]]=1;
			if (retour) {
				sprintf(parallelmove_name, "#%s,%s", addr_name, registers_lmove[value]);
			} else {
				sprintf(parallelmove_name, "l:%s,%s", addr_name, registers_lmove[value]);
			}
		} else {
			/* Read S */
			sprintf(parallelmove_name, "%s,l:%s", registers_lmove[value], addr_name);
		}

		return;
	}

	memspace = (cur_inst>>19) & 1;
	if (cur_inst & (1<<14)) {
		retour = dsp_calc_ea(ea_mode, addr_name);	
	} else {
		sprintf(addr_name,"0x%04x", ea_mode);
		retour = 0;
	}

	if (memspace) {
		/* Y: */

		if (cur_inst & (1<<15)) {
			/* Write D */

			registers_changed[value]=1;
			if (retour) {
				sprintf(parallelmove_name, "#%s,%s", addr_name, registers_name[value]);
			} else {
				sprintf(parallelmove_name, "y:%s,%s", addr_name, registers_name[value]);
			}

		} else {
			/* Read S */
			sprintf(parallelmove_name, "%s,y:%s", registers_name[value], addr_name);
		}
	} else {
		/* X: */

		if (cur_inst & (1<<15)) {
			/* Write D */

			registers_changed[value]=1;

			if (retour) {
				sprintf(parallelmove_name, "#%s,%s", addr_name, registers_name[value]);
			} else {
				sprintf(parallelmove_name, "x:%s,%s", addr_name, registers_name[value]);
			}
		} else {
			/* Read S */
			sprintf(parallelmove_name, "%s,x:%s", registers_name[value], addr_name);
		}
	}
}

static void dsp_pm_8(void)
{
	char addr1_name[16], addr2_name[16];
	Uint32 ea_mode1, ea_mode2, numreg1, numreg2;
/*
	1wmm eeff WrrM MRRR x:ea,D1		y:ea,D2	
						x:ea,D1		S2,y:ea
						S1,x:ea		y:ea,D2
						S1,x:ea		S2,y:ea
*/
	numreg1 = DSP_REG_X0;
	switch((cur_inst>>18) & BITMASK(2)) {
		case 0:	numreg1 = DSP_REG_X0;	break;
		case 1:	numreg1 = DSP_REG_X1;	break;
		case 2:	numreg1 = DSP_REG_A;	break;
		case 3:	numreg1 = DSP_REG_B;	break;
	}

	numreg2 = DSP_REG_Y0;
	switch((cur_inst>>16) & BITMASK(2)) {
		case 0:	numreg2 = DSP_REG_Y0;	break;
		case 1:	numreg2 = DSP_REG_Y1;	break;
		case 2:	numreg2 = DSP_REG_A;	break;
		case 3:	numreg2 = DSP_REG_B;	break;
	}

	ea_mode1 = (cur_inst>>8) & BITMASK(5);
	if ((ea_mode1>>3) == 0) {
		ea_mode1 |= (1<<5);
	}
	ea_mode2 = (cur_inst>>13) & BITMASK(2);
	ea_mode2 |= ((cur_inst>>20) & BITMASK(2))<<3;
	if ((ea_mode1 & (1<<2))==0) {
		ea_mode2 |= 1<<2;
	}
	if ((ea_mode2>>3) == 0) {
		ea_mode2 |= (1<<5);
	}

	dsp_calc_ea(ea_mode1, addr1_name);
	dsp_calc_ea(ea_mode2, addr2_name);
	
	if (cur_inst & (1<<15)) {
		registers_changed[numreg1]=1;
		if (cur_inst & (1<<22)) {
			registers_changed[numreg2]=1;
			sprintf(parallelmove_name, "x:%s,%s y:%s,%s",
				addr1_name,
				registers_name[numreg1],
				addr2_name,
				registers_name[numreg2]
			);
		} else {
			sprintf(parallelmove_name, "x:%s,%s %s,y:%s",
				addr1_name,
				registers_name[numreg1],
				registers_name[numreg2],
				addr2_name
			);
		}
	} else {
		if (cur_inst & (1<<22)) {
			registers_changed[numreg2]=1;
			sprintf(parallelmove_name, "%s,x:%s y:%s,%s",
				registers_name[numreg1],
				addr1_name,
				addr2_name,
				registers_name[numreg2]
			);
		} else {
			sprintf(parallelmove_name, "%s,x:%s %s,y:%s",
				registers_name[numreg1],
				addr1_name,
				registers_name[numreg2],
				addr2_name
			);
		}
	}	
}


/**********************************
 *	Parallel moves ALU instructions
 **********************************/

static void dsp_abs(void)
{
	Uint32 numreg;
	
	numreg = DSP_REG_A+((cur_inst>>3) & 1);

	registers_changed[numreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," abs %s %s", registers_name[numreg], parallelmove_name);
}

static void dsp_adc(void)
{
	const char *srcname;
	Uint32 numreg;

	if (cur_inst & (1<<4)) {
		srcname="y";
	} else {
		srcname="x";
	}

	numreg=DSP_REG_A+((cur_inst>>3) & 1);
	registers_changed[numreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," adc %s,%s %s",
		srcname,
		registers_name[numreg],
		parallelmove_name
	);
}

static void dsp_add(void)
{
	const char *srcname;
	Uint32 srcreg, dstreg;
	
	srcreg = (cur_inst>>4) & BITMASK(3);
	dstreg = (cur_inst>>3) & 1;

	switch(srcreg) {
		case 1:
			srcreg = dstreg ^ 1;
			srcname = registers_name[DSP_REG_A+srcreg];
			break;
		case 2:
			srcname="x";
			break;
		case 3:
			srcname="y";
			break;
		case 4:
			srcname=registers_name[DSP_REG_X0];
			break;
		case 5:
			srcname=registers_name[DSP_REG_Y0];
			break;
		case 6:
			srcname=registers_name[DSP_REG_X1];
			break;
		case 7:
			srcname=registers_name[DSP_REG_Y1];
			break;
		default:
			srcname="";
			break;
	}

	registers_changed[DSP_REG_A+dstreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," add %s,%s %s",
		srcname,
		registers_name[DSP_REG_A+dstreg],
		parallelmove_name
	);
}

static void dsp_addl(void)
{
	Uint32 numreg;

	numreg = (cur_inst>>3) & 1;

	registers_changed[DSP_REG_A+numreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," addl %s,%s %s",
		registers_name[DSP_REG_A+(numreg ^ 1)],
		registers_name[DSP_REG_A+numreg],
		parallelmove_name
	);
}

static void dsp_addr(void)
{
	Uint32 numreg;

	numreg = (cur_inst>>3) & 1;

	registers_changed[DSP_REG_A+numreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," addr %s,%s %s",
		registers_name[DSP_REG_A+(numreg ^ 1)],
		registers_name[DSP_REG_A+numreg],
		parallelmove_name
	);
}

static void dsp_and(void)
{
	Uint32 srcreg,dstreg;

	switch((cur_inst>>4) & BITMASK(2)) {
		case 1:
			srcreg=DSP_REG_Y0;
			break;
		case 2:
			srcreg=DSP_REG_X1;
			break;
		case 3:
			srcreg=DSP_REG_Y1;
			break;
		case 0:
		default:
			srcreg=DSP_REG_X0;
	}
	dstreg = DSP_REG_A+((cur_inst>>3) & 1);

	registers_changed[dstreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," and %s,%s %s",
		registers_name[srcreg],
		registers_name[dstreg],
		parallelmove_name
	);
}

static void dsp_asl(void)
{
	Uint32 numreg;

	numreg = DSP_REG_A+((cur_inst>>3) & 1);

	registers_changed[numreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," asl %s %s",
		registers_name[numreg],
		parallelmove_name
	);
}

static void dsp_asr(void)
{
	Uint32 numreg;

	numreg = DSP_REG_A+((cur_inst>>3) & 1);

	registers_changed[numreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," asr %s %s",
		registers_name[numreg],
		parallelmove_name
	);
}

static void dsp_clr(void)
{
	Uint32 numreg;

	numreg = DSP_REG_A+((cur_inst>>3) & 1);

	registers_changed[numreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," clr %s %s",
		registers_name[numreg],
		parallelmove_name
	);
}

static void dsp_cmp(void)
{
	Uint32 srcreg, dstreg;
	
	srcreg = (cur_inst>>4) & BITMASK(3);
	dstreg = (cur_inst>>3) & 1;

	switch(srcreg) {
		case 0:
			srcreg = DSP_REG_A+(dstreg ^ 1);
			break;
		case 4:
			srcreg = DSP_REG_X0;
			break;
		case 5:
			srcreg = DSP_REG_Y0;
			break;
		case 6:
			srcreg = DSP_REG_X1;
			break;
		case 7:
			srcreg = DSP_REG_Y1;
			break;
	}

	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," cmp %s,%s %s",
		registers_name[srcreg],
		registers_name[DSP_REG_A+dstreg],
		parallelmove_name
	);
}

static void dsp_cmpm(void)
{
	Uint32 srcreg, dstreg;
	
	srcreg = (cur_inst>>4) & BITMASK(3);
	dstreg = (cur_inst>>3) & 1;

	switch(srcreg) {
		case 0:
			srcreg = DSP_REG_A+(dstreg ^ 1);
			break;
		case 4:
			srcreg = DSP_REG_X0;
			break;
		case 5:
			srcreg = DSP_REG_Y0;
			break;
		case 6:
			srcreg = DSP_REG_X1;
			break;
		case 7:
			srcreg = DSP_REG_Y1;
			break;
	}

	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," cmpm %s,%s %s",
		registers_name[srcreg],
		registers_name[DSP_REG_A+dstreg],
		parallelmove_name
	);
}

static void dsp_eor(void)
{
	Uint32 srcreg, dstreg;

	switch((cur_inst>>4) & BITMASK(2)) {
		case 1:
			srcreg=DSP_REG_Y0;
			break;
		case 2:
			srcreg=DSP_REG_X1;
			break;
		case 3:
			srcreg=DSP_REG_Y1;
			break;
		case 0:
		default:
			srcreg=DSP_REG_X0;
	}
	dstreg = DSP_REG_A+((cur_inst>>3) & 1);

	registers_changed[dstreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," eor %s,%s %s",
		registers_name[srcreg],
		registers_name[dstreg],
		parallelmove_name
	);
}

static void dsp_lsl(void)
{
	Uint32 numreg;

	numreg = DSP_REG_A+((cur_inst>>3) & 1);

	registers_changed[numreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," lsl %s %s",
		registers_name[numreg],
		parallelmove_name
	);
}

static void dsp_lsr(void)
{
	Uint32 numreg;

	numreg = DSP_REG_A+((cur_inst>>3) & 1);

	registers_changed[numreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," lsr %s %s",
		registers_name[numreg],
		parallelmove_name
	);
}

static void dsp_mac(void)
{
	const char *sign_name;
	Uint32 src1reg=DSP_REG_NULL, src2reg=DSP_REG_NULL, dstreg;

	if (cur_inst & (1<<2)) {
		sign_name="-";
	} else {
		sign_name="";
	}
	
	switch((cur_inst>>4) & BITMASK(3)) {
		case 0:
			src1reg = DSP_REG_X0;
			src2reg = DSP_REG_X0;
			break;
		case 1:
			src1reg = DSP_REG_Y0;
			src2reg = DSP_REG_Y0;
			break;
		case 2:
			src1reg = DSP_REG_X1;
			src2reg = DSP_REG_X0;
			break;
		case 3:
			src1reg = DSP_REG_Y1;
			src2reg = DSP_REG_Y0;
			break;
		case 4:
			src1reg = DSP_REG_X0;
			src2reg = DSP_REG_Y1;
			break;
		case 5:
			src1reg = DSP_REG_Y0;
			src2reg = DSP_REG_X0;
			break;
		case 6:
			src1reg = DSP_REG_X1;
			src2reg = DSP_REG_Y0;
			break;
		case 7:
			src1reg = DSP_REG_Y1;
			src2reg = DSP_REG_X1;
			break;
	}
	dstreg = (cur_inst>>3) & 1;

	registers_changed[DSP_REG_A+dstreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," mac %s%s,%s,%s %s",
		sign_name,
		registers_name[src1reg],
		registers_name[src2reg],
		registers_name[DSP_REG_A+dstreg],
		parallelmove_name
	);
}

static void dsp_macr(void)
{
	const char *sign_name;
	Uint32 src1reg=DSP_REG_NULL, src2reg=DSP_REG_NULL, dstreg;

	if (cur_inst & (1<<2)) {
		sign_name="-";
	} else {
		sign_name="";
	}
	
	switch((cur_inst>>4) & BITMASK(3)) {
		case 0:
			src1reg = DSP_REG_X0;
			src2reg = DSP_REG_X0;
			break;
		case 1:
			src1reg = DSP_REG_Y0;
			src2reg = DSP_REG_Y0;
			break;
		case 2:
			src1reg = DSP_REG_X1;
			src2reg = DSP_REG_X0;
			break;
		case 3:
			src1reg = DSP_REG_Y1;
			src2reg = DSP_REG_Y0;
			break;
		case 4:
			src1reg = DSP_REG_X0;
			src2reg = DSP_REG_Y1;
			break;
		case 5:
			src1reg = DSP_REG_Y0;
			src2reg = DSP_REG_X0;
			break;
		case 6:
			src1reg = DSP_REG_X1;
			src2reg = DSP_REG_Y0;
			break;
		case 7:
			src1reg = DSP_REG_Y1;
			src2reg = DSP_REG_X1;
			break;
	}
	dstreg = (cur_inst>>3) & 1;

	registers_changed[DSP_REG_A+dstreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," macr %s%s,%s,%s %s",
		sign_name,
		registers_name[src1reg],
		registers_name[src2reg],
		registers_name[DSP_REG_A+dstreg],
		parallelmove_name
	);
}

static void dsp_move(void)
{
	sprintf(str_instr," move %s", parallelmove_name);
}

static void dsp_mpy(void)
{
	const char *sign_name;
	Uint32 src1reg=DSP_REG_NULL, src2reg=DSP_REG_NULL, dstreg;

	if (cur_inst & (1<<2)) {
		sign_name="-";
	} else {
		sign_name="";
	}
	
	switch((cur_inst>>4) & BITMASK(3)) {
		case 0:
			src1reg = DSP_REG_X0;
			src2reg = DSP_REG_X0;
			break;
		case 1:
			src1reg = DSP_REG_Y0;
			src2reg = DSP_REG_Y0;
			break;
		case 2:
			src1reg = DSP_REG_X1;
			src2reg = DSP_REG_X0;
			break;
		case 3:
			src1reg = DSP_REG_Y1;
			src2reg = DSP_REG_Y0;
			break;
		case 4:
			src1reg = DSP_REG_X0;
			src2reg = DSP_REG_Y1;
			break;
		case 5:
			src1reg = DSP_REG_Y0;
			src2reg = DSP_REG_X0;
			break;
		case 6:
			src1reg = DSP_REG_X1;
			src2reg = DSP_REG_Y0;
			break;
		case 7:
			src1reg = DSP_REG_Y1;
			src2reg = DSP_REG_X1;
			break;
	}
	dstreg = (cur_inst>>3) & 1;

	registers_changed[DSP_REG_A+dstreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," mpy %s%s,%s,%s %s",
		sign_name,
		registers_name[src1reg],
		registers_name[src2reg],
		registers_name[DSP_REG_A+dstreg],
		parallelmove_name
	);
}

static void dsp_mpyr(void)
{
	const char *sign_name;
	Uint32 src1reg=DSP_REG_NULL, src2reg=DSP_REG_NULL, dstreg;

	if (cur_inst & (1<<2)) {
		sign_name="-";
	} else {
		sign_name="";
	}
	
	switch((cur_inst>>4) & BITMASK(3)) {
		case 0:
			src1reg = DSP_REG_X0;
			src2reg = DSP_REG_X0;
			break;
		case 1:
			src1reg = DSP_REG_Y0;
			src2reg = DSP_REG_Y0;
			break;
		case 2:
			src1reg = DSP_REG_X1;
			src2reg = DSP_REG_X0;
			break;
		case 3:
			src1reg = DSP_REG_Y1;
			src2reg = DSP_REG_Y0;
			break;
		case 4:
			src1reg = DSP_REG_X0;
			src2reg = DSP_REG_Y1;
			break;
		case 5:
			src1reg = DSP_REG_Y0;
			src2reg = DSP_REG_X0;
			break;
		case 6:
			src1reg = DSP_REG_X1;
			src2reg = DSP_REG_Y0;
			break;
		case 7:
			src1reg = DSP_REG_Y1;
			src2reg = DSP_REG_X1;
			break;
	}
	dstreg = (cur_inst>>3) & 1;

	registers_changed[DSP_REG_A+dstreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," mpyr %s%s,%s,%s %s",
		sign_name,
		registers_name[src1reg],
		registers_name[src2reg],
		registers_name[DSP_REG_A+dstreg],
		parallelmove_name
	);
}

static void dsp_neg(void)
{
	Uint32 numreg;

	numreg = DSP_REG_A+((cur_inst>>3) & 1);

	registers_changed[numreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," neg %s %s",
		registers_name[numreg],
		parallelmove_name
	);
}

static void dsp_not(void)
{
	Uint32 numreg;

	numreg = DSP_REG_A+((cur_inst>>3) & 1);

	registers_changed[numreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," not %s %s",
		registers_name[numreg],
		parallelmove_name
	);
}

static void dsp_or(void)
{
	Uint32 srcreg, dstreg;

	switch((cur_inst>>4) & BITMASK(2)) {
		case 1:
			srcreg=DSP_REG_Y0;
			break;
		case 2:
			srcreg=DSP_REG_X1;
			break;
		case 3:
			srcreg=DSP_REG_Y1;
			break;
		case 0:
		default:
			srcreg=DSP_REG_X0;
	}
	dstreg = DSP_REG_A+((cur_inst>>3) & 1);

	registers_changed[dstreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," or %s,%s %s",
		registers_name[srcreg],
		registers_name[dstreg],
		parallelmove_name
	);
}

static void dsp_rnd(void)
{
	Uint32 numreg;

	numreg = DSP_REG_A+((cur_inst>>3) & 1);

	registers_changed[numreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," rnd %s %s",
		registers_name[numreg],
		parallelmove_name
	);
}

static void dsp_rol(void)
{
	Uint32 numreg;

	numreg = DSP_REG_A+((cur_inst>>3) & 1);

	registers_changed[numreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," rol %s %s",
		registers_name[numreg],
		parallelmove_name
	);
}

static void dsp_ror(void)
{
	Uint32 numreg;

	numreg = DSP_REG_A+((cur_inst>>3) & 1);

	registers_changed[numreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," ror %s %s",
		registers_name[numreg],
		parallelmove_name
	);
}

static void dsp_sbc(void)
{
	const char *srcname;
	Uint32 numreg;

	if (cur_inst & (1<<4)) {
		srcname="y";
	} else {
		srcname="x";
	}

	numreg = DSP_REG_A+((cur_inst>>3) & 1);

	registers_changed[numreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," sbc %s,%s %s",
		srcname,
		registers_name[numreg],
		parallelmove_name
	);
}

static void dsp_sub(void)
{
	const char *srcname;
	Uint32 srcreg, dstreg;
	
	srcreg = (cur_inst>>4) & BITMASK(3);
	dstreg = (cur_inst>>3) & 1;

	switch(srcreg) {
		case 1:
			srcreg = dstreg ^ 1;
			srcname = registers_name[DSP_REG_A+srcreg];
			break;
		case 2:
			srcname="x";
			break;
		case 3:
			srcname="y";
			break;
		case 4:
			srcname=registers_name[DSP_REG_X0];
			break;
		case 5:
			srcname=registers_name[DSP_REG_Y0];
			break;
		case 6:
			srcname=registers_name[DSP_REG_X1];
			break;
		case 7:
			srcname=registers_name[DSP_REG_Y1];
			break;
		default:
			srcname="";
			break;
	}

	registers_changed[DSP_REG_A+dstreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," sub %s,%s %s",
		srcname,
		registers_name[DSP_REG_A+dstreg],
		parallelmove_name
	);
}

static void dsp_subl(void)
{
	Uint32 numreg;

	numreg = (cur_inst>>3) & 1;

	registers_changed[DSP_REG_A+numreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," subl %s,%s %s",
		registers_name[DSP_REG_A+(numreg ^ 1)],
		registers_name[DSP_REG_A+numreg],
		parallelmove_name
	);
}

static void dsp_subr(void)
{
	Uint32 numreg;

	numreg = (cur_inst>>3) & 1;

	registers_changed[DSP_REG_A+numreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," subr %s,%s %s",
		registers_name[DSP_REG_A+(numreg ^ 1)],
		registers_name[DSP_REG_A+numreg],
		parallelmove_name
	);
}

static void dsp_tfr(void)
{
	Uint32 srcreg, dstreg;
	
	srcreg = (cur_inst>>4) & BITMASK(3);
	dstreg = (cur_inst>>3) & 1;

	switch(srcreg) {
		case 4:
			srcreg = DSP_REG_X0;
			break;
		case 5:
			srcreg = DSP_REG_Y0;
			break;
		case 6:
			srcreg = DSP_REG_X1;
			break;
		case 7:
			srcreg = DSP_REG_Y1;
			break;
		case 0:
		default:
			srcreg = DSP_REG_A+(dstreg ^ 1);
			break;
	}

	registers_changed[DSP_REG_A+dstreg]=1;
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," tfr %s,%s %s",
		registers_name[srcreg],
		registers_name[DSP_REG_A+dstreg],
		parallelmove_name
	);
}

static void dsp_tst(void)
{
	registers_changed[DSP_REG_SR]=1;

	sprintf(str_instr," tst %s %s",
		registers_name[DSP_REG_A+((cur_inst>>3) & 1)],
		parallelmove_name
	);
}
