/*
	DSP M56001 emulation
	Instructions interpreter

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

#include "dsp_core.h"
#include "dsp_cpu.h"
#include "dsp_disasm.h"


/* More disasm infos, if wanted */
#define DSP_DISASM 1		/* Main DSP disassembler switch */
#define DSP_DISASM_INST 1	/* Instructions */
#define DSP_DISASM_REG 1	/* Registers changes */
#define DSP_DISASM_MEM 1	/* Memory changes */
#define DSP_DISASM_INTER 0	/* Interrupts */
#define DSP_DISASM_STATE 0	/* State change */

#define DSP_COUNT_IPS 0		/* Count instruction per seconds */

#if defined(DSP_DISASM) && (DSP_DISASM_MEM==1)
# define write_memory(x,y,z) write_memory_disasm(x,y,z)
#else
# define write_memory(x,y,z) write_memory_raw(x,y,z)
#endif

/**********************************
 *	Defines
 **********************************/

#define BITMASK(x)	((1<<(x))-1)

/* cycle counter wait state time when access to external memory  */
#define XY_WAITSTATE 1
#define P_WAITSTATE 1
#define XP_WAITSTATE 1   /* X Peripheral WaitState */
#define YP_WAITSTATE 1   /* Y Peripheral WaitState */

/**********************************
 *	Variables
 **********************************/

/* Instructions per second */
static Uint32 start_time;
static Uint32 num_inst;

/* Length of current instruction */
static Uint32 cur_inst_len;	/* =0:jump, >0:increment */

/* Current instruction */
static Uint32 cur_inst;

/* Parallel move temp data */
typedef union {
	Uint32 *host_pointer;
	Uint32 dsp_address;
} parmove_dest_u;

static Uint32 tmp_parmove_src[2][3];	/* What to read */
static parmove_dest_u tmp_parmove_dest[2][3];	/* Where to write */
static Uint32 tmp_parmove_start[2];		/* From where to read/write */
static Uint32 tmp_parmove_len[2];		/* How many to read/write */
static Uint32 tmp_parmove_type[2];		/* 0=register, 1=memory */
static Uint32 tmp_parmove_space[2];		/* Memory space to write to */

#if defined(DSP_DISASM) && (DSP_DISASM_MEM==1)
static char   str_disasm_memory[2][50]; /* Buffer for memory change text in disasm mode */
static Uint16 disasm_memory_ptr;		/* Pointer for memory change in disasm mode */
#endif

/**********************************
 *	Functions
 **********************************/

typedef void (*dsp_emul_t)(void);

static void dsp_postexecute_update_pc(void);
static void dsp_postexecute_interrupts(void);

static void dsp_ccr_update_e_u_n_z(Uint32 *reg0, Uint32 *reg1, Uint32 *reg2);

static inline Uint32 read_memory_p(Uint16 address);
static Uint32 read_memory(int space, Uint16 address);
static void write_memory_raw(int space, Uint16 address, Uint32 value);
#if defined(DSP_DISASM) && (DSP_DISASM_MEM==1)
static Uint32 read_memory_disasm(int space, Uint16 address);
static void write_memory_disasm(int space, Uint16 address, Uint32 value);
#endif
static void dsp_write_reg(Uint32 numreg, Uint32 value); 

static void dsp_stack_push(Uint32 curpc, Uint32 cursr, Uint16 sshOnly);
static void dsp_stack_pop(Uint32 *curpc, Uint32 *cursr);
static void dsp_compute_ssh_ssl(void);

static void opcode8h_0(void);

static void dsp_update_rn(Uint32 numreg, Sint16 modifier);
static void dsp_update_rn_bitreverse(Uint32 numreg);
static void dsp_update_rn_modulo(Uint32 numreg, Sint16 modifier);
static int dsp_calc_ea(Uint32 ea_mode, Uint32 *dst_addr);
static int dsp_calc_cc(Uint32 cc_code);

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

/* Parallel move analyzer */
static void dsp_parmove_read(void);
static void dsp_parmove_write(void);
static void dsp_pm_class2(void);

static int dsp_pm_read_accu24(int numreg, Uint32 *dest);
static void dsp_pm_writereg(int numreg, int position);

static void dsp_pm_0(void);
static void dsp_pm_1(void);
static void dsp_pm_2(void);
static void dsp_pm_2_2(void);
static void dsp_pm_3(void);
static void dsp_pm_4(void);
static void dsp_pm_4x(void);
static void dsp_pm_5(void);
static void dsp_pm_8(void);

/* 56bits arithmetic */
static Uint16 dsp_abs56(Uint32 *dest);
static Uint16 dsp_asl56(Uint32 *dest);
static Uint16 dsp_asr56(Uint32 *dest);
static Uint16 dsp_add56(Uint32 *source, Uint32 *dest);
static Uint16 dsp_sub56(Uint32 *source, Uint32 *dest);
static void dsp_mul56(Uint32 source1, Uint32 source2, Uint32 *dest, Uint8 signe);
static void dsp_rnd56(Uint32 *dest);

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

static dsp_emul_t opcodes_parmove[16]={
	dsp_pm_0,
	dsp_pm_1,
	dsp_pm_2,
	dsp_pm_3,
	dsp_pm_4,
	dsp_pm_5,
	dsp_pm_5,
	dsp_pm_5,

	dsp_pm_8,
	dsp_pm_8,
	dsp_pm_8,
	dsp_pm_8,
	dsp_pm_8,
	dsp_pm_8,
	dsp_pm_8,
	dsp_pm_8
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

static int registers_mpy[8][2]={
	{DSP_REG_X0,DSP_REG_X0},
	{DSP_REG_Y0,DSP_REG_Y0},
	{DSP_REG_X1,DSP_REG_X0},
	{DSP_REG_Y1,DSP_REG_Y0},

	{DSP_REG_X0,DSP_REG_Y1},
	{DSP_REG_Y0,DSP_REG_X0},
	{DSP_REG_X1,DSP_REG_Y0},
	{DSP_REG_Y1,DSP_REG_X1}
};

static int registers_mask[64]={
	0, 0, 0, 0,
	24, 24, 24, 24,
	24, 24, 8, 8,
	24, 24, 24, 24,
	
	16, 16, 16, 16,
	16, 16, 16, 16,
	16, 16, 16, 16,
	16, 16, 16, 16,
	
	16, 16, 16, 16,
	16, 16, 16, 16,
	0, 0, 0, 0,
	0, 0, 0, 0,

	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 16, 8, 6,
	16, 16, 16, 16
};

static dsp_interrupt_t dsp_interrupt[12] = {
	{DSP_INTER_RESET	,	0x00, 0, "Reset"},
	{DSP_INTER_ILLEGAL	,	0x3e, 0, "Illegal"},
	{DSP_INTER_STACK_ERROR	,	0x02, 0, "Stack Error"},
	{DSP_INTER_TRACE	,	0x04, 0, "Trace"},
	{DSP_INTER_SWI		,	0x06, 0, "Swi"},
	{DSP_INTER_HOST_COMMAND	,	0xff, 1, "Host Command"},
	{DSP_INTER_HOST_RCV_DATA,	0x20, 1, "Host receive"},
	{DSP_INTER_HOST_TRX_DATA,	0x22, 1, "Host transmit"},
	{DSP_INTER_SSI_RCV_DATA_E,	0x0e, 2, "SSI receive with exception"},
	{DSP_INTER_SSI_RCV_DATA	,	0x0c, 2, "SSI receive"},
	{DSP_INTER_SSI_TRX_DATA_E,	0x12, 2, "SSI transmit with exception"},
	{DSP_INTER_SSI_TRX_DATA	,	0x10, 2, "SSI tramsmit"}
};


/**********************************
 *	Emulator kernel
 **********************************/

/* Yep, feel lazy, so put it there */
static dsp_core_t *dsp_core;

void dsp56k_init_cpu(void *th_dsp_core)
{
	dsp_core = th_dsp_core;
#ifdef DSP_DISASM
	dsp56k_disasm_init(dsp_core);
#endif
	start_time = SDL_GetTicks();
	num_inst = 0;
}

void dsp56k_execute_instruction(void)
{
	Uint32 value;

#ifdef DSP_DISASM
#if DSP_DISASM_REG
	dsp56k_disasm_reg_save();
#endif
#if DSP_DISASM_INST
	dsp56k_disasm();
#endif
#if DSP_DISASM_MEM
	disasm_memory_ptr = 0;
#endif
#endif

	/* Decode and execute current instruction */
	cur_inst = read_memory_p(dsp_core->pc);
	cur_inst_len = 1;
	
	/* Initialize instruction cycle counter */
	dsp_core->instr_cycle = 2;

	if (cur_inst < 0x100000) {
		value = (cur_inst >> 11) & (BITMASK(6) << 3);
		value += (cur_inst >> 5) & BITMASK(3);
		opcodes8h[value]();
	} else {
		dsp_parmove_read();
		value = cur_inst & BITMASK(8);
		opcodes_alu[value]();
		dsp_parmove_write();
	}

	/* Process the PC */
	dsp_postexecute_update_pc();

	/* Process Interrupts */
	dsp_postexecute_interrupts();

#if DSP_COUNT_IPS
	++num_inst;
	if ((num_inst & 63) == 0) {
		/* Evaluate time after <N> instructions have been executed to avoid asking too frequently */
		Uint32 cur_time = SDL_GetTicks();
		if (cur_time-start_time>1000) {
			fprintf(stderr, "Dsp: %d i/s\n", (num_inst*1000)/(cur_time-start_time));
			start_time=cur_time;
			num_inst=0;
		}
	}
#endif

#ifdef DSP_DISASM
#if DSP_DISASM_INST
	fprintf(stderr, "%s", dsp56k_getInstructionText());
#endif
#if DSP_DISASM_REG
	dsp56k_disasm_reg_compare();
#endif
#if DSP_DISASM_MEM
	/* 1 memory change to display ? */
	if (disasm_memory_ptr == 1)
		fprintf(stderr, "\t%s\n", str_disasm_memory[0]);
	/* 2 memory changes to display ? */
	else if (disasm_memory_ptr == 2) {
		fprintf(stderr, "\t%s\n", str_disasm_memory[0]);
		fprintf(stderr, "\t%s\n", str_disasm_memory[1]);
	}
#endif
#endif
}

/**********************************
 *	Update the PC
**********************************/

static void dsp_postexecute_update_pc(void)
{
	/* When running a REP, PC must stay on the current instruction */
	if (dsp_core->loop_rep) {
		/* Is PC on the instruction to repeat ? */		
		if (dsp_core->pc_on_rep==0) {
			--dsp_core->registers[DSP_REG_LC];
			dsp_core->registers[DSP_REG_LC] &= BITMASK(16);

			if (dsp_core->registers[DSP_REG_LC] > 0) {
				cur_inst_len=0;	/* Stay on this instruction */
			} else {
				dsp_core->loop_rep = 0;
				dsp_core->registers[DSP_REG_LC] = dsp_core->registers[DSP_REG_LCSAVE];
			}
		} else {
			/* Init LC at right value */
			if (dsp_core->registers[DSP_REG_LC] == 0) {
				dsp_core->registers[DSP_REG_LC] = 0x010000;
			}
			dsp_core->pc_on_rep = 0;
		}
	}

	/* Normal execution, go to next instruction */
	dsp_core->pc += cur_inst_len;

	/* When running a DO loop, we test the end of loop with the */
	/* updated PC, pointing to last instruction of the loop */
	if (dsp_core->registers[DSP_REG_SR] & (1<<DSP_SR_LF)) {

		/* Did we execute the last instruction in loop ? */
		if (dsp_core->pc == dsp_core->registers[DSP_REG_LA]+1) {		
			--dsp_core->registers[DSP_REG_LC];
			dsp_core->registers[DSP_REG_LC] &= BITMASK(16);

			if (dsp_core->registers[DSP_REG_LC]==0) {
				/* end of loop */
				Uint32 saved_pc, saved_sr;

				dsp_stack_pop(&saved_pc, &saved_sr);
				dsp_core->registers[DSP_REG_SR] &= 0x7f;
				dsp_core->registers[DSP_REG_SR] |= saved_sr & (1<<DSP_SR_LF);
				dsp_stack_pop(&dsp_core->registers[DSP_REG_LA], &dsp_core->registers[DSP_REG_LC]);
			} else {
				/* Loop one more time */
				dsp_core->pc = dsp_core->registers[DSP_REG_SSH];
			}
		}
	}
}

/**********************************
 *	Interrupts
**********************************/

static void dsp_postexecute_interrupts(void)
{
	Uint32 index, instr, i;
	Sint32 ipl_to_raise, ipl_sr;

	/* REP is not interruptible */
	if (dsp_core->loop_rep) {
		return;
	}

	/* A fast interrupt can not be interrupted. */
	if (dsp_core->interrupt_state == DSP_INTERRUPT_DISABLED) {

		switch (dsp_core->interrupt_pipeline_count) {
			case 5:
				dsp_core->interrupt_pipeline_count --;
				return;
			case 4:
				/* Prefetch interrupt instruction 1 */
				dsp_core->interrupt_save_pc = dsp_core->pc;
				dsp_core->pc = dsp_core->interrupt_instr_fetch;

				/* is it a LONG interrupt ? */
				instr = read_memory_p(dsp_core->interrupt_instr_fetch);
				if ( ((instr & 0xfff000) == 0x0d0000) || ((instr & 0xffc0ff) == 0x0bc080) ) {
					dsp_core->interrupt_state = DSP_INTERRUPT_LONG;
					dsp_stack_push(dsp_core->interrupt_save_pc, dsp_core->registers[DSP_REG_SR], 0); 
					dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_LF)|(1<<DSP_SR_T)  |
											(1<<DSP_SR_S1)|(1<<DSP_SR_S0) |
											(1<<DSP_SR_I0)|(1<<DSP_SR_I1));
					dsp_core->registers[DSP_REG_SR] |= dsp_core->interrupt_IplToRaise<<DSP_SR_I0;
				}
				dsp_core->interrupt_pipeline_count --;
				return;
			case 3:
				/* Prefetch interrupt instruction 2 */
				if (dsp_core->pc == dsp_core->interrupt_instr_fetch+1) {
					instr = read_memory_p(dsp_core->pc);
					if ( ((instr & 0xfff000) == 0x0d0000) || ((instr & 0xffc0ff) == 0x0bc080) ) {
						dsp_core->interrupt_state = DSP_INTERRUPT_LONG;
						dsp_stack_push(dsp_core->interrupt_save_pc, dsp_core->registers[DSP_REG_SR], 0); 
						dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_LF)|(1<<DSP_SR_T)  |
												(1<<DSP_SR_S1)|(1<<DSP_SR_S0) |
												(1<<DSP_SR_I0)|(1<<DSP_SR_I1));
						dsp_core->registers[DSP_REG_SR] |= dsp_core->interrupt_IplToRaise<<DSP_SR_I0;
					}
				}
				dsp_core->interrupt_pipeline_count --;
				return;
			case 2:
				/* 1 instruction executed after interrupt */
				/* before re enable interrupts */
				/* Was it a FAST interrupt ? */
				if (dsp_core->pc == dsp_core->interrupt_instr_fetch+2) {
					dsp_core->pc = dsp_core->interrupt_save_pc;
				}
				dsp_core->interrupt_pipeline_count --;
				return;
			case 1:
				/* Last instruction executed after interrupt */
				/* before re enable interrupts */
				dsp_core->interrupt_pipeline_count --;
				return;
			case 0:
				/* Re enable interrupts */
				dsp_core->interrupt_save_pc = -1;
				dsp_core->interrupt_instr_fetch = -1;
				dsp_core->interrupt_state = DSP_INTERRUPT_NONE;
				return;
		}
	}

	/* Trace Interrupt ? */
	if (dsp_core->registers[DSP_REG_SR] & (1<<DSP_SR_T)) {
		dsp_core_add_interrupt(dsp_core, DSP_INTER_TRACE);
	}

	/* No interrupt to execute */
	if (dsp_core->interrupt_counter == 0) {
		return;
	}

	/* search for an interrupt */
	ipl_sr = (dsp_core->registers[DSP_REG_SR]>>DSP_SR_I0) & BITMASK(2);
	index = 0xffff;
	ipl_to_raise = -1;

	/* Arbitrate between all pending interrupts */
	for (i=0; i<12; i++) {
		if (dsp_core->interrupt_isPending[i] == 1) {

			/* level 3 interrupt ? */
			if (dsp_core->interrupt_ipl[i] == 3) {
				index = i;
				break;
			}

			/* level 0, 1 ,2 interrupt ? */
			/* if interrupt is masked in SR, don't process it */
			if (dsp_core->interrupt_ipl[i] < ipl_sr)
				continue;

			/* if interrupt is lower or equal than current arbitrated interrupt */
			if (dsp_core->interrupt_ipl[i] <= ipl_to_raise)
				continue;

			/* save current arbitrated interrupt */
			index = i;
			ipl_to_raise = dsp_core->interrupt_ipl[i];
		}
	}

	/* If there's no interrupt to process, return */
	if (index == 0xffff) {
		return;
	}

	/* remove this interrupt from the pending interrupts table */
	dsp_core->interrupt_isPending[index] = 0;
	dsp_core->interrupt_counter --;

	/* process arbritrated interrupt */
	ipl_to_raise = dsp_core->interrupt_ipl[index] + 1;
	if (ipl_to_raise > 3) {
		ipl_to_raise = 3;
	}

	dsp_core->interrupt_instr_fetch = dsp_interrupt[index].vectorAddr;
	dsp_core->interrupt_pipeline_count = 5;
	dsp_core->interrupt_state = DSP_INTERRUPT_DISABLED;
	dsp_core->interrupt_IplToRaise = ipl_to_raise;
	
#if DSP_DISASM_INTER
	fprintf(stderr, "Dsp: Interrupt: %s\n", dsp_interrupt[index].name);
#endif

	/* SSI receive data with exception ? */
	if (dsp_core->interrupt_instr_fetch == 0xe) {
		dsp_core->periph[DSP_SPACE_X][DSP_SSI_SR] &= 0xff-(1<<DSP_SSI_SR_ROE);
	}

	/* SSI transmit data with exception ? */
	else if (dsp_core->interrupt_instr_fetch == 0x12) {
		dsp_core->periph[DSP_SPACE_X][DSP_SSI_SR] &= 0xff-(1<<DSP_SSI_SR_TUE);
	}

	/* host command ? */
	else if (dsp_core->interrupt_instr_fetch == 0xff) {
		/* Clear HC and HCP interrupt */
		dsp_core->periph[DSP_SPACE_X][DSP_HOST_HSR] &= 0xff - (1<<DSP_HOST_HSR_HCP);
		dsp_core->hostport[CPU_HOST_CVR] &= 0xff - (1<<CPU_HOST_CVR_HC);  

		dsp_core->interrupt_instr_fetch = dsp_core->hostport[CPU_HOST_CVR] & BITMASK(5);
		dsp_core->interrupt_instr_fetch *= 2;	
	}
}

/**********************************
 *	Set/clear ccr bits
 **********************************/

/* reg0 has bits 55..48 */
/* reg1 has bits 47..24 */
/* reg2 has bits 23..0 */

static void dsp_ccr_update_e_u_n_z(Uint32 *reg0, Uint32 *reg1, Uint32 *reg2) {
	Uint32 scaling, value_e, value_u, numbits;

	int sr_extension = 1 << DSP_SR_E;
	int sr_unnormalized;
	int sr_negative = (((*reg0)>>7) & 1) << DSP_SR_N;
	int sr_zero = 1 << DSP_SR_Z;

	scaling = (dsp_core->registers[DSP_REG_SR]>>DSP_SR_S0) & BITMASK(2);
	value_e = (*reg0) & 0xff;
	numbits = 8;

	switch(scaling) {
		case 0:
			value_e <<=1;
			value_e |= ((*reg1)>>23) & 1;
			numbits=9;
			value_u = ((*reg1)>>22) & 3;
			break;
		case 1:
			value_u = ((*reg0)<<1) & 2;
			value_u |= ((*reg1)>>23) & 1;
			break;
		case 2:
			value_e <<=2;
			value_e |= ((*reg1)>>22) & 3;
			numbits=10;
			value_u = ((*reg1)>>21) & 3;
			break;
		default:
			return;
			break;
	}

	if ((value_e==0) || (value_e==(Uint32)BITMASK(numbits))) {
		sr_extension = 0;
	}

	sr_unnormalized = ((value_u==0) || (value_u==BITMASK(2))) << DSP_SR_U;
	if (((*reg2) & BITMASK(24))!=0) {
		sr_zero = 0;
	} else if (((*reg1) & BITMASK(24))!=0) {
		sr_zero = 0;
	} else if (((*reg0) & BITMASK(8))!=0) {
		sr_zero = 0;
	}

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_E) | (1<<DSP_SR_U) | (1<<DSP_SR_N) | (1<<DSP_SR_Z));

	dsp_core->registers[DSP_REG_SR] |= sr_extension;
	dsp_core->registers[DSP_REG_SR] |= sr_unnormalized;
	dsp_core->registers[DSP_REG_SR] |= sr_negative;
	dsp_core->registers[DSP_REG_SR] |= sr_zero;
}

/**********************************
 *	Read/Write memory functions
 **********************************/

#if defined(DSP_DISASM) && (DSP_DISASM_MEM==1)
static Uint32 read_memory_disasm(int space, Uint16 address)
{
	/* Internal RAM ? */
	if (address<0x100) {
		return dsp_core->ramint[space][address] & BITMASK(24);
	}

	if (space==DSP_SPACE_P) {
		return read_memory_p(address);
	}

	/* Internal ROM? */
	if ((dsp_core->registers[DSP_REG_OMR] & (1<<DSP_OMR_DE)) &&
		(address<0x200)) {
		return dsp_core->rom[space][address] & BITMASK(24);
	}

	/* Peripheral address ? */
	if (address >= 0xffc0) {
		if ((space==DSP_SPACE_X) && (address==0xffc0+DSP_HOST_HRX)) {
			return dsp_core->dsp_host_rtx;
		}
		if ((space==DSP_SPACE_X) && (address==0xffc0+DSP_SSI_TX)) {
			return dsp_core->ssi.transmit_value;
		}
		return dsp_core->periph[space][address-0xffc0] & BITMASK(24);
	}

	/* Falcon: External RAM, map X to upper 16K of matching space in Y,P */
	address &= (DSP_RAMSIZE>>1) - 1;
	if (space == DSP_SPACE_X) {
		address += DSP_RAMSIZE>>1;
	}

	/* Falcon: External RAM, finally map X,Y to P */
	return dsp_core->ramext[address & (DSP_RAMSIZE-1)] & BITMASK(24);
}
#endif

static inline Uint32 read_memory_p(Uint16 address)
{
	/* Internal RAM ? */
	if (address<0x200) {
		return dsp_core->ramint[DSP_SPACE_P][address] & BITMASK(24);
	}

	/* External RAM, mask address to available ram size */
//	dsp_core->instr_cycle += P_WAITSTATE;
	return dsp_core->ramext[address & (DSP_RAMSIZE-1)] & BITMASK(24);
}

static Uint32 read_memory(int space, Uint16 address)
{
	Uint32 value;

		/* Internal RAM ? */
	if (address < 0x100) {
		return dsp_core->ramint[space][address] & BITMASK(24);
	}

	if (space == DSP_SPACE_P) {
		return read_memory_p(address);
	}

	/* Internal ROM ? */
	if (address < 0x200) {
		if (dsp_core->registers[DSP_REG_OMR] & (1<<DSP_OMR_DE)) {
			return dsp_core->rom[space][address] & BITMASK(24);
		}
	}

	/* Peripheral address ? */
	if (address >= 0xffc0) {
		value = dsp_core->periph[space][address-0xffc0] & BITMASK(24);
		if (space == DSP_SPACE_X) {
			if (address == 0xffc0+DSP_HOST_HRX) {
				value = dsp_core->dsp_host_rtx;
				dsp_core_hostport_dspread(dsp_core);
			}	
			else if (address == 0xffc0+DSP_SSI_RX) {
				value = dsp_core_ssi_readRX(dsp_core);
			}
			dsp_core->instr_cycle += XP_WAITSTATE;
		}
		else {
			dsp_core->instr_cycle += YP_WAITSTATE;
		}
		return value;
	}

	/* 1 more cycle for external RAM access */
	dsp_core->instr_cycle += XY_WAITSTATE;
	
	/* Falcon: External RAM, map X to upper 16K of matching space in Y,P */
	address &= (DSP_RAMSIZE>>1) - 1;

	if (space == DSP_SPACE_X) {
		address += DSP_RAMSIZE>>1;
	}

	/* Falcon: External RAM, finally map X,Y to P */
	return dsp_core->ramext[address & (DSP_RAMSIZE-1)] & BITMASK(24);
}

/* Note: MACRO write_memory defined to either write_memory_raw or write_memory_disasm */
static void write_memory_raw(int space, Uint16 address, Uint32 value)
{
	value &= BITMASK(24);

	/* Peripheral address ? */
	if (address >= 0xffc0) {
		if (space == DSP_SPACE_X) {
			switch(address-0xffc0) {
				case DSP_HOST_HTX:
					dsp_core->dsp_host_htx = value;
					dsp_core_hostport_dspwrite(dsp_core);
					break;
				case DSP_HOST_HCR:
					dsp_core->periph[DSP_SPACE_X][DSP_HOST_HCR] = value;
					/* Set HF3 and HF2 accordingly on the host side */
					dsp_core->hostport[CPU_HOST_ISR] &=
						BITMASK(8)-((1<<CPU_HOST_ISR_HF3)|(1<<CPU_HOST_ISR_HF2));
					dsp_core->hostport[CPU_HOST_ISR] |=
						dsp_core->periph[DSP_SPACE_X][DSP_HOST_HCR] & ((1<<CPU_HOST_ISR_HF3)|(1<<CPU_HOST_ISR_HF2));
					break;
				case DSP_HOST_HSR:
					/* Read only */
					break;
				case DSP_SSI_CRA:
				case DSP_SSI_CRB:
					dsp_core->periph[DSP_SPACE_X][address-0xffc0] = value;
					dsp_core_ssi_configure(dsp_core, address-0xffc0, value);
					break;
				case DSP_SSI_TSR:
					dsp_core_ssi_writeTSR(dsp_core);
					break;
				case DSP_SSI_TX:
					dsp_core_ssi_writeTX(dsp_core, value);
					break;
				case DSP_IPR:
					dsp_core->periph[DSP_SPACE_X][DSP_IPR] = value;
					dsp_core_setInterruptIPL(dsp_core, value);
					break;
				case DSP_PCD:
					dsp_core->periph[DSP_SPACE_X][DSP_PCD] = value;
					dsp_core_setPortCDataRegister(dsp_core, value);
					break;
				default:
					dsp_core->periph[DSP_SPACE_X][address-0xffc0] = value;
					break;
			}
			dsp_core->instr_cycle += XP_WAITSTATE;
			return;
		} 
		else if (space == DSP_SPACE_Y) {
			dsp_core->periph[DSP_SPACE_Y][address-0xffc0] = value;
			dsp_core->instr_cycle += YP_WAITSTATE;
			return;
		}
	}
		
	/* Internal RAM ? */
	if (address < 0x100) {
		dsp_core->ramint[space][address] = value;
		return;
	}

	/* Internal ROM ? */
	if (address < 0x200) {
		if (space != DSP_SPACE_P) {
			if (dsp_core->registers[DSP_REG_OMR] & (1<<DSP_OMR_DE)) {
				/* Can not write to ROM space */
				return;
			}
		}
		else {
			/* Space P RAM */
			dsp_core->ramint[DSP_SPACE_P][address] = value;
			return;
		}
	}

	/* 1 more cycle for external RAM access */
	dsp_core->instr_cycle += XY_WAITSTATE;

	/* Falcon: External RAM, map X to upper 16K of matching space in Y,P */
	if (space != DSP_SPACE_P) {
		address &= (DSP_RAMSIZE>>1) - 1;
	} 

	if (space == DSP_SPACE_X) {
		address += DSP_RAMSIZE>>1;
	}

	/* Falcon: External RAM, map X,Y to P */
	dsp_core->ramext[address & (DSP_RAMSIZE-1)] = value;
}

#if defined(DSP_DISASM) && (DSP_DISASM_MEM==1)
static void write_memory_disasm(int space, Uint16 address, Uint32 value)
{
	Uint32 oldvalue, curvalue;
	Uint8 space_c = 'p';

	value &= BITMASK(24);

	if ((address == 0xffeb) && (space == DSP_SPACE_X) ) {
		oldvalue = dsp_core->dsp_host_htx;
	} else {
		oldvalue = read_memory_disasm(space, address);
	}

	write_memory_raw(space,address,value);

	switch(space) {
		case DSP_SPACE_X:
			space_c = 'x';
			break;
		case DSP_SPACE_Y:
			space_c = 'y';
			break;
		default:
			break;
	}

	if ((address == 0xffeb) && (space == DSP_SPACE_X) ) {
		curvalue = dsp_core->dsp_host_htx;
	} else {
		curvalue = read_memory_disasm(space, address);
	}
	sprintf(str_disasm_memory[disasm_memory_ptr],"Mem: %c:0x%04x  0x%06x -> 0x%06x", space_c, address, oldvalue, curvalue);
	disasm_memory_ptr ++;
}
#endif

static void dsp_write_reg(Uint32 numreg, Uint32 value)
{
	Uint32 stack_error;

	if ((numreg==DSP_REG_A) || (numreg==DSP_REG_B)) {
		numreg &= 1;
		dsp_core->registers[DSP_REG_A0+numreg]= 0;
		dsp_core->registers[DSP_REG_A1+numreg]= value;
		dsp_core->registers[DSP_REG_A2+numreg]= 0;
		if (value & (1<<23)) {
			dsp_core->registers[DSP_REG_A2+numreg]= 0xff;
		}
	} else {
		switch (numreg) {
			case DSP_REG_OMR:
				dsp_core->registers[DSP_REG_OMR] = value & 0xc7;
				break;
			case DSP_REG_SR:
				dsp_core->registers[DSP_REG_SR] = value & 0xaf7f;
				break;
			case DSP_REG_SP:
				stack_error = dsp_core->registers[DSP_REG_SP] & (1<<DSP_SP_SE);
				if ((stack_error==0) && (value & (1<<DSP_SP_SE))) {
					/* Stack full, raise interrupt */
					dsp_core_add_interrupt(dsp_core, DSP_INTER_STACK_ERROR);
					fprintf(stderr,"Dsp: Stack Overflow\n");
				}
				dsp_core->registers[DSP_REG_SP] = value & BITMASK(6); 
				dsp_compute_ssh_ssl();
				break;
			case DSP_REG_SSH:
				dsp_stack_push(value, 0, 1);
				break;
			case DSP_REG_SSL:
				numreg = dsp_core->registers[DSP_REG_SP] & BITMASK(4);
				if (numreg == 0) {
					value = 0;
				}
				dsp_core->stack[1][numreg] = value & BITMASK(16);
				dsp_core->registers[DSP_REG_SSL] = value & BITMASK(16);
				break;
			default:
				dsp_core->registers[numreg] = value; 
				dsp_core->registers[numreg] &= BITMASK(registers_mask[numreg]);
				break;
		}
	}
}

/**********************************
 *	Stack push/pop
 **********************************/

static void dsp_stack_push(Uint32 curpc, Uint32 cursr, Uint16 sshOnly)
{
	Uint32 stack_error, underflow, stack;

	stack_error = dsp_core->registers[DSP_REG_SP] & (1<<DSP_SP_SE);
	underflow = dsp_core->registers[DSP_REG_SP] & (1<<DSP_SP_UF);
	stack = (dsp_core->registers[DSP_REG_SP] & BITMASK(4)) + 1;


	if ((stack_error==0) && (stack & (1<<DSP_SP_SE))) {
		/* Stack full, raise interrupt */
		dsp_core_add_interrupt(dsp_core, DSP_INTER_STACK_ERROR);
		fprintf(stderr,"Dsp: Stack Overflow\n");
	}
	
	dsp_core->registers[DSP_REG_SP] = (underflow | stack_error | stack) & BITMASK(6);
	stack &= BITMASK(4);

	if (stack) {
		/* SSH part */
		dsp_core->stack[0][stack] = curpc & BITMASK(16);
		/* SSL part, if instruction is not like "MOVEC xx, SSH"  */
		if (sshOnly == 0) {
			dsp_core->stack[1][stack] = cursr & BITMASK(16);
		}
	} else {
		dsp_core->stack[0][0] = 0;
		dsp_core->stack[1][0] = 0;
	}

	/* Update SSH and SSL registers */
	dsp_core->registers[DSP_REG_SSH] = dsp_core->stack[0][stack];
	dsp_core->registers[DSP_REG_SSL] = dsp_core->stack[1][stack];
}

static void dsp_stack_pop(Uint32 *newpc, Uint32 *newsr)
{
	Uint32 stack_error, underflow, stack;

	stack_error = dsp_core->registers[DSP_REG_SP] & (1<<DSP_SP_SE);
	underflow = dsp_core->registers[DSP_REG_SP] & (1<<DSP_SP_UF);
	stack = (dsp_core->registers[DSP_REG_SP] & BITMASK(4)) - 1;

	if ((stack_error==0) && (stack & (1<<DSP_SP_SE))) {
		/* Stack empty*/
		dsp_core_add_interrupt(dsp_core, DSP_INTER_STACK_ERROR);
		fprintf(stderr,"Dsp: Stack underflow\n");
	}

	dsp_core->registers[DSP_REG_SP] = (underflow | stack_error | stack) & BITMASK(6);
	stack &= BITMASK(4);
	*newpc = dsp_core->registers[DSP_REG_SSH];
	*newsr = dsp_core->registers[DSP_REG_SSL];

	dsp_core->registers[DSP_REG_SSH] = dsp_core->stack[0][stack];
	dsp_core->registers[DSP_REG_SSL] = dsp_core->stack[1][stack];
}

static void dsp_compute_ssh_ssl(void)
{
	Uint32 stack;

	stack = dsp_core->registers[DSP_REG_SP];
	stack &= BITMASK(4);
	dsp_core->registers[DSP_REG_SSH] = dsp_core->stack[0][stack];
	dsp_core->registers[DSP_REG_SSL] = dsp_core->stack[1][stack];
}

/**********************************
 *	Effective address calculation
 **********************************/

static void dsp_update_rn(Uint32 numreg, Sint16 modifier)
{
	Sint16 value;
	Uint16 m_reg;

	m_reg = (Uint16) dsp_core->registers[DSP_REG_M0+numreg];
	if (m_reg == 0) {
		/* Bit reversed carry update */
		dsp_update_rn_bitreverse(numreg);
	} else if (m_reg<=32767) {
		/* Modulo update */
		dsp_update_rn_modulo(numreg, modifier);
	} else if (m_reg == 65535) {
		/* Linear addressing mode */
		value = (Sint16) dsp_core->registers[DSP_REG_R0+numreg];
		value += modifier;
		dsp_core->registers[DSP_REG_R0+numreg] = ((Uint32) value) & BITMASK(16);
	} else {
		/* Undefined */
	}
}

static void dsp_update_rn_bitreverse(Uint32 numreg)
{
	int revbits, i;
	Uint32 value, r_reg;

	/* Check how many bits to reverse */
	value = dsp_core->registers[DSP_REG_N0+numreg];
	for (revbits=0;revbits<16;revbits++) {
		if (value & (1<<revbits)) {
			break;
		}
	}	
	revbits++;
		
	/* Reverse Rn bits */
	r_reg = dsp_core->registers[DSP_REG_R0+numreg];
	value = r_reg & (BITMASK(16)-BITMASK(revbits));
	for (i=0;i<revbits;i++) {
		if (r_reg & (1<<i)) {
			value |= 1<<(revbits-i-1);
		}
	}

	/* Increment */
	value++;
	value &= BITMASK(revbits);

	/* Reverse Rn bits */
	r_reg &= (BITMASK(16)-BITMASK(revbits));
	r_reg |= value;

	value = r_reg & (BITMASK(16)-BITMASK(revbits));
	for (i=0;i<revbits;i++) {
		if (r_reg & (1<<i)) {
			value |= 1<<(revbits-i-1);
		}
	}

	dsp_core->registers[DSP_REG_R0+numreg] = value;
}

static void dsp_update_rn_modulo(Uint32 numreg, Sint16 modifier)
{
	Uint16 bufsize, modulo, lobound, hibound, bufmask;
	Sint16 r_reg, orig_modifier=modifier;

	modulo = dsp_core->registers[DSP_REG_M0+numreg]+1;
	bufsize = 1;
	bufmask = BITMASK(16);
	while (bufsize < modulo) {
		bufsize <<= 1;
		bufmask <<= 1;
	}
	
	lobound = dsp_core->registers[DSP_REG_R0+numreg] & bufmask;
	hibound = lobound + modulo - 1;

	r_reg = (Sint16) dsp_core->registers[DSP_REG_R0+numreg];

	if (orig_modifier>modulo) {
		while (modifier>bufsize) {
			r_reg += bufsize;
			modifier -= bufsize;
		}
		while (modifier<-bufsize) {
			r_reg -= bufsize;
			modifier += bufsize;
		}
	}

	r_reg += modifier;

	if (orig_modifier!=modulo) {
		if (r_reg>hibound) {
			r_reg -= modulo;
		} else if (r_reg<lobound) {
			r_reg += modulo;
		}	
	}

	dsp_core->registers[DSP_REG_R0+numreg] = ((Uint32) r_reg) & BITMASK(16);
}

static int dsp_calc_ea(Uint32 ea_mode, Uint32 *dst_addr)
{
	Uint32 value, numreg, curreg;

	value = (ea_mode >> 3) & BITMASK(3);
	numreg = ea_mode & BITMASK(3);
	switch (value) {
		case 0:
			/* (Rx)-Nx */
			*dst_addr = dsp_core->registers[DSP_REG_R0+numreg];
			dsp_update_rn(numreg, -dsp_core->registers[DSP_REG_N0+numreg]);
			break;
		case 1:
			/* (Rx)+Nx */
			*dst_addr = dsp_core->registers[DSP_REG_R0+numreg];
			dsp_update_rn(numreg, dsp_core->registers[DSP_REG_N0+numreg]);
			break;
		case 2:
			/* (Rx)- */
			*dst_addr = dsp_core->registers[DSP_REG_R0+numreg];
			dsp_update_rn(numreg, -1);
			break;
		case 3:
			/* (Rx)+ */
			*dst_addr = dsp_core->registers[DSP_REG_R0+numreg];
			dsp_update_rn(numreg, +1);
			break;
		case 4:
			/* (Rx) */
			*dst_addr = dsp_core->registers[DSP_REG_R0+numreg];
			break;
		case 5:
			/* (Rx+Nx) */
			dsp_core->instr_cycle += 2;
			curreg = dsp_core->registers[DSP_REG_R0+numreg];
			dsp_update_rn(numreg, dsp_core->registers[DSP_REG_N0+numreg]);
			*dst_addr = dsp_core->registers[DSP_REG_R0+numreg];
			dsp_core->registers[DSP_REG_R0+numreg] = curreg;
			break;
		case 6:
			/* aa */
			dsp_core->instr_cycle += 2;
			*dst_addr = read_memory_p(dsp_core->pc+1);
			cur_inst_len++;
			if (numreg != 0) {
				return 1; /* immediate value */
			}
			break;
		case 7:
			/* -(Rx) */
			dsp_core->instr_cycle += 2;
			dsp_update_rn(numreg, -1);
			*dst_addr = dsp_core->registers[DSP_REG_R0+numreg];
			break;
	}
	/* address */
	return 0;
}

/**********************************
 *	Condition code test
 **********************************/

static int dsp_calc_cc(Uint32 cc_code)
{
	Uint16 value1, value2, value3;

	switch (cc_code) {
		case 0:  /* CC (HS) */
			value1 = dsp_core->registers[DSP_REG_SR] & (1<<DSP_SR_C);
			return (value1==0);
		case 1: /* GE */
			value1 = (dsp_core->registers[DSP_REG_SR] >> DSP_SR_N) & 1;
			value2 = (dsp_core->registers[DSP_REG_SR] >> DSP_SR_V) & 1;
			return ((value1 ^ value2) == 0);
		case 2: /* NE */
			value1 = dsp_core->registers[DSP_REG_SR] & (1<<DSP_SR_Z);
			return (value1==0);
		case 3: /* PL */
			value1 = dsp_core->registers[DSP_REG_SR] & (1<<DSP_SR_N);
			return (value1==0);
		case 4: /* NN */
			value1 = (dsp_core->registers[DSP_REG_SR] >> DSP_SR_Z) & 1;
			value2 = (~(dsp_core->registers[DSP_REG_SR] >> DSP_SR_U)) & 1;
			value3 = (~(dsp_core->registers[DSP_REG_SR] >> DSP_SR_E)) & 1;
			return ((value1 | (value2 & value3)) == 0);
		case 5: /* EC */
			value1 = dsp_core->registers[DSP_REG_SR] & (1<<DSP_SR_E);
			return (value1==0);
		case 6: /* LC */
			value1 = dsp_core->registers[DSP_REG_SR] & (1<<DSP_SR_L);
			return (value1==0);
		case 7: /* GT */ 
			value1 = (dsp_core->registers[DSP_REG_SR] >> DSP_SR_N) & 1;
			value2 = (dsp_core->registers[DSP_REG_SR] >> DSP_SR_V) & 1;
			value3 = (dsp_core->registers[DSP_REG_SR] >> DSP_SR_Z) & 1;
			return ((value3 | (value1 ^ value2)) == 0);
		case 8: /* CS (LO) */
			value1 = dsp_core->registers[DSP_REG_SR] & (1<<DSP_SR_C);
			return (value1==1);
		case 9: /* LT */
			value1 = (dsp_core->registers[DSP_REG_SR] >> DSP_SR_N) & 1;
			value2 = (dsp_core->registers[DSP_REG_SR] >> DSP_SR_V) & 1;
			return ((value1 ^ value2) == 1);
		case 10: /* EQ */
			value1 = (dsp_core->registers[DSP_REG_SR] >> DSP_SR_Z) & 1;
			return (value1==1);
		case 11: /* MI */
			value1 = (dsp_core->registers[DSP_REG_SR] >> DSP_SR_N) & 1;
			return (value1==1);
		case 12: /* NR */
			value1 = (dsp_core->registers[DSP_REG_SR] >> DSP_SR_Z) & 1;
			value2 = (~(dsp_core->registers[DSP_REG_SR] >> DSP_SR_U)) & 1;
			value3 = (~(dsp_core->registers[DSP_REG_SR] >> DSP_SR_E)) & 1;
			return ((value1 | (value2 & value3)) == 1);
		case 13: /* ES */
			value1 = (dsp_core->registers[DSP_REG_SR] >> DSP_SR_E) & 1;
			return (value1==1);
		case 14: /* LS */
			value1 = (dsp_core->registers[DSP_REG_SR] >> DSP_SR_L) & 1;
			return (value1==1);
		case 15: /* LE */
			value1 = (dsp_core->registers[DSP_REG_SR] >> DSP_SR_N) & 1;
			value2 = (dsp_core->registers[DSP_REG_SR] >> DSP_SR_V) & 1;
			value3 = (dsp_core->registers[DSP_REG_SR] >> DSP_SR_Z) & 1;
			return ((value3 | (value1 ^ value2)) == 1);
	}
	return 0;
}

/**********************************
 *	Highbyte opcodes dispatchers
 **********************************/

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
	cur_inst_len = 0;
	fprintf(stderr, "Dsp: 0x%04x: 0x%06x unknown instruction\n",dsp_core->pc, cur_inst);
}

static void dsp_andi(void)
{
	Uint32 regnum, value;

	value = (cur_inst >> 8) & BITMASK(8);
	regnum = cur_inst & BITMASK(2);
	switch(regnum) {
		case 0:
			/* mr */
			dsp_core->registers[DSP_REG_SR] &= (value<<8)|BITMASK(8);
			break;
		case 1:
			/* ccr */
			dsp_core->registers[DSP_REG_SR] &= (BITMASK(8)<<8)|value;
			break;
		case 2:
			/* omr */
			dsp_core->registers[DSP_REG_OMR] &= value;
			break;
	}
}

static void dsp_bchg_aa(void)
{
	Uint32 memspace, addr, value, newcarry, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	addr = value;
	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;
	if (newcarry) {
		value -= (1<<numbit);
	} else {
		value += (1<<numbit);
	}
	write_memory(memspace, addr, value);

	/* Set carry */
	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core->registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core->instr_cycle += 2;
	if (addr>=0x200) {
		dsp_core->instr_cycle += XY_WAITSTATE;
	}
}

static void dsp_bchg_ea(void)
{
	Uint32 memspace, addr, value, newcarry, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	dsp_calc_ea(value, &addr);
	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;
	if (newcarry) {
		value -= (1<<numbit);
	} else {
		value += (1<<numbit);
	}
	write_memory(memspace, addr, value);

	/* Set carry */
	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core->registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core->instr_cycle += 2;
	if (addr>=0x200) {
		dsp_core->instr_cycle += XY_WAITSTATE;
	}
}

static void dsp_bchg_pp(void)
{
	Uint32 memspace, addr, value, newcarry, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	addr = 0xffc0 + value;
	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;
	if (newcarry) {
		value -= (1<<numbit);
	} else {
		value += (1<<numbit);
	}
	write_memory(memspace, addr, value);

	/* Set carry */
	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core->registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core->instr_cycle += 2;
	if (memspace == DSP_SPACE_X) {
		dsp_core->instr_cycle += XP_WAITSTATE;
	} else {
		dsp_core->instr_cycle += YP_WAITSTATE;
	}
}

static void dsp_bchg_reg(void)
{
	Uint32 value, numreg, newcarry, numbit;
	
	numreg = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	if ((numreg==DSP_REG_A) || (numreg==DSP_REG_B)) {
		dsp_pm_read_accu24(numreg, &value);
	} else {
		value = dsp_core->registers[numreg];
	}

	newcarry = (value>>numbit) & 1;
	if (newcarry) {
		value -= (1<<numbit);
	} else {
		value += (1<<numbit);
	}

	dsp_write_reg(numreg, value);

	/* Set carry */
	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core->registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core->instr_cycle += 2;
}

static void dsp_bclr_aa(void)
{
	Uint32 memspace, addr, value, newcarry, numbit;
	
	memspace = (cur_inst>>6) & 1;
	addr = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;
	value &= 0xffffffff-(1<<numbit);
	write_memory(memspace, addr, value);

	/* Set carry */
	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core->registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core->instr_cycle += 2;
	if (addr>=0x200) {
		dsp_core->instr_cycle += XY_WAITSTATE;
	}
}

static void dsp_bclr_ea(void)
{
	Uint32 memspace, addr, value, newcarry, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	dsp_calc_ea(value, &addr);
	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;
	value &= 0xffffffff-(1<<numbit);
	write_memory(memspace, addr, value);

	/* Set carry */
	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core->registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core->instr_cycle += 2;
	if (addr>=0x200) {
		dsp_core->instr_cycle += XY_WAITSTATE;
	}
}

static void dsp_bclr_pp(void)
{
	Uint32 memspace, addr, value, newcarry, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	addr = 0xffc0 + value;
	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;
	value &= 0xffffffff-(1<<numbit);
	write_memory(memspace, addr, value);

	/* Set carry */
	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core->registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core->instr_cycle += 2;
	if (memspace == DSP_SPACE_X) {
		dsp_core->instr_cycle += XP_WAITSTATE;
	} else {
		dsp_core->instr_cycle += YP_WAITSTATE;
	}	
}

static void dsp_bclr_reg(void)
{
	Uint32 value, numreg, newcarry, numbit;
	
	numreg = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	if ((numreg==DSP_REG_A) || (numreg==DSP_REG_B)) {
		dsp_pm_read_accu24(numreg, &value);
	} else {
		value = dsp_core->registers[numreg];
	}

	newcarry = (value>>numbit) & 1;
	value &= 0xffffffff-(1<<numbit);

	dsp_write_reg(numreg, value);

	/* Set carry */
	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core->registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core->instr_cycle += 2;
}

static void dsp_bset_aa(void)
{
	Uint32 memspace, addr, value, newcarry, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	addr = value;
	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;
	value |= (1<<numbit);
	write_memory(memspace, addr, value);

	/* Set carry */
	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core->registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core->instr_cycle += 2;
	if (addr>=0x200) {
		dsp_core->instr_cycle += XY_WAITSTATE;
	}
}

static void dsp_bset_ea(void)
{
	Uint32 memspace, addr, value, newcarry, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	dsp_calc_ea(value, &addr);
	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;
	value |= (1<<numbit);
	write_memory(memspace, addr, value);

	/* Set carry */
	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core->registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core->instr_cycle += 2;
	if (addr>=0x200) {
		dsp_core->instr_cycle += XY_WAITSTATE;
	}
}

static void dsp_bset_pp(void)
{
	Uint32 memspace, addr, value, newcarry, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	addr = 0xffc0 + value;
	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;
	value |= (1<<numbit);
	write_memory(memspace, addr, value);

	/* Set carry */
	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core->registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core->instr_cycle += 2;
	if (memspace == DSP_SPACE_X) {
		dsp_core->instr_cycle += XP_WAITSTATE;
	} else {
		dsp_core->instr_cycle += YP_WAITSTATE;
	}
}

static void dsp_bset_reg(void)
{
	Uint32 value, numreg, newcarry, numbit;
	
	numreg = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	if ((numreg==DSP_REG_A) || (numreg==DSP_REG_B)) {
		dsp_pm_read_accu24(numreg, &value);
	} else {
		value = dsp_core->registers[numreg];
	}

	newcarry = (value>>numbit) & 1;
	value |= (1<<numbit);

	dsp_write_reg(numreg, value);

	/* Set carry */
	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core->registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core->instr_cycle += 2;
}

static void dsp_btst_aa(void)
{
	Uint32 memspace, addr, value, newcarry, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	addr = value;
	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;

	/* Set carry */
	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core->registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core->instr_cycle += 2;
}

static void dsp_btst_ea(void)
{
	Uint32 memspace, addr, value, newcarry, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	dsp_calc_ea(value, &addr);
	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;

	/* Set carry */
	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core->registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core->instr_cycle += 2;
}

static void dsp_btst_pp(void)
{
	Uint32 memspace, addr, value, newcarry, numbit;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	addr = 0xffc0 + value;
	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;

	/* Set carry */
	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core->registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core->instr_cycle += 2;
}

static void dsp_btst_reg(void)
{
	Uint32 value, numreg, newcarry, numbit;
	
	numreg = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	if ((numreg==DSP_REG_A) || (numreg==DSP_REG_B)) {
		dsp_pm_read_accu24(numreg, &value);
	} else {
		value = dsp_core->registers[numreg];
	}

	newcarry = (value>>numbit) & 1;

	/* Set carry */
	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core->registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core->instr_cycle += 2;
}

static void dsp_div(void)
{
	Uint32 srcreg, destreg, source[3], dest[3];
	Uint16 newsr;

	srcreg = DSP_REG_NULL;
	switch((cur_inst>>4) & BITMASK(2)) {
		case 0:	srcreg = DSP_REG_X0;	break;
		case 1:	srcreg = DSP_REG_Y0;	break;
		case 2:	srcreg = DSP_REG_X1;	break;
		case 3:	srcreg = DSP_REG_Y1;	break;
	}
	destreg = DSP_REG_A+((cur_inst>>3) & 1);

	source[0] = 0;
    source[1] = dsp_core->registers[srcreg];
    if (source[1] & (1<<23)) {
        source[0] = 0xff;
    }
    source[2] = 0;

	dest[0] = dsp_core->registers[DSP_REG_A2+(destreg & 1)];
	dest[1] = dsp_core->registers[DSP_REG_A1+(destreg & 1)];
	dest[2] = dsp_core->registers[DSP_REG_A0+(destreg & 1)];

	if (((dest[0]>>7) & 1) ^ ((source[1]>>23) & 1)) {
		/* D += S */
		newsr = dsp_asl56(dest);
		dsp_add56(source, dest);
	} else {
		/* D -= S */
		newsr = dsp_asl56(dest);
		dsp_sub56(source, dest);
	}

	dest[2] |= (dsp_core->registers[DSP_REG_SR]>>DSP_SR_C) & 1;

	dsp_core->registers[DSP_REG_A2+(destreg & 1)] = dest[0];
	dsp_core->registers[DSP_REG_A1+(destreg & 1)] = dest[1];
	dsp_core->registers[DSP_REG_A0+(destreg & 1)] = dest[2];

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_C)|(1<<DSP_SR_V));
	dsp_core->registers[DSP_REG_SR] |= (1-((dest[0]>>7) & 1))<<DSP_SR_C;
	dsp_core->registers[DSP_REG_SR] |= newsr & (1<<DSP_SR_L);
	dsp_core->registers[DSP_REG_SR] |= newsr & (1<<DSP_SR_V);
}

/*
	DO instruction parameter encoding

	xxxxxxxx 00xxxxxx 0xxxxxxx	aa
	xxxxxxxx 01xxxxxx 0xxxxxxx	ea
	xxxxxxxx YYxxxxxx 1xxxxxxx	imm
	xxxxxxxx 11xxxxxx 0xxxxxxx	reg
*/

static void dsp_do_aa(void)
{
	Uint32 memspace, addr;

	/* x:aa */
	/* y:aa */

	dsp_stack_push(dsp_core->registers[DSP_REG_LA], dsp_core->registers[DSP_REG_LC], 0);
	dsp_core->registers[DSP_REG_LA] = read_memory_p(dsp_core->pc+1) & BITMASK(16);
	cur_inst_len++;
	dsp_stack_push(dsp_core->pc+cur_inst_len, dsp_core->registers[DSP_REG_SR], 0);
	dsp_core->registers[DSP_REG_SR] |= (1<<DSP_SR_LF);

	memspace = (cur_inst>>6) & 1;
	addr = (cur_inst>>8) & BITMASK(6);
	dsp_core->registers[DSP_REG_LC] = read_memory(memspace, addr) & BITMASK(16);

	dsp_core->instr_cycle += 4;
}

static void dsp_do_imm(void)
{
	/* #xx */

	dsp_stack_push(dsp_core->registers[DSP_REG_LA], dsp_core->registers[DSP_REG_LC], 0);
	dsp_core->registers[DSP_REG_LA] = read_memory_p(dsp_core->pc+1) & BITMASK(16);
	cur_inst_len++;
	dsp_stack_push(dsp_core->pc+cur_inst_len, dsp_core->registers[DSP_REG_SR], 0);
	dsp_core->registers[DSP_REG_SR] |= (1<<DSP_SR_LF);

	dsp_core->registers[DSP_REG_LC] = ((cur_inst>>8) & BITMASK(8))
		+ ((cur_inst & BITMASK(4))<<8);

	dsp_core->instr_cycle += 4;
}

static void dsp_do_ea(void)
{
	Uint32 memspace, ea_mode, addr;

	/* x:ea */
	/* y:ea */

	dsp_stack_push(dsp_core->registers[DSP_REG_LA], dsp_core->registers[DSP_REG_LC], 0);
	dsp_core->registers[DSP_REG_LA] = read_memory_p(dsp_core->pc+1) & BITMASK(16);
	cur_inst_len++;
	dsp_stack_push(dsp_core->pc+cur_inst_len, dsp_core->registers[DSP_REG_SR], 0);
	dsp_core->registers[DSP_REG_SR] |= (1<<DSP_SR_LF);

	memspace = (cur_inst>>6) & 1;
	ea_mode = (cur_inst>>8) & BITMASK(6);
	dsp_calc_ea(ea_mode, &addr);
	dsp_core->registers[DSP_REG_LC] = read_memory(memspace, addr) & BITMASK(16);

	dsp_core->instr_cycle += 4;
}

static void dsp_do_reg(void)
{
	Uint32 numreg;

	/* S */

	dsp_stack_push(dsp_core->registers[DSP_REG_LA], dsp_core->registers[DSP_REG_LC], 0);
	dsp_core->registers[DSP_REG_LA] = read_memory_p(dsp_core->pc+1) & BITMASK(16);
	cur_inst_len++;

	numreg = (cur_inst>>8) & BITMASK(6);
	if ((numreg == DSP_REG_A) || (numreg == DSP_REG_B)) {
		dsp_pm_read_accu24(numreg, &dsp_core->registers[DSP_REG_LC]); 
	} else {
		dsp_core->registers[DSP_REG_LC] = dsp_core->registers[numreg];
	}
	dsp_core->registers[DSP_REG_LC] &= BITMASK(16);

	dsp_stack_push(dsp_core->pc+cur_inst_len, dsp_core->registers[DSP_REG_SR], 0);
	dsp_core->registers[DSP_REG_SR] |= (1<<DSP_SR_LF);

	dsp_core->instr_cycle += 4;
}

static void dsp_enddo(void)
{
	Uint32 saved_pc, saved_sr;

	dsp_stack_pop(&saved_pc, &saved_sr);
	dsp_core->registers[DSP_REG_SR] &= 0x7f;
	dsp_core->registers[DSP_REG_SR] |= saved_sr & (1<<DSP_SR_LF);
	dsp_stack_pop(&dsp_core->registers[DSP_REG_LA], &dsp_core->registers[DSP_REG_LC]);
}

static void dsp_illegal(void)
{
	/* Raise interrupt p:0x003e */
	dsp_core_add_interrupt(dsp_core, DSP_INTER_ILLEGAL);
}

static void dsp_jcc_imm(void)
{
	Uint32 cc_code, newpc;

	newpc = cur_inst & BITMASK(12);
	cc_code=(cur_inst>>12) & BITMASK(4);
	if (dsp_calc_cc(cc_code)) {
		dsp_core->pc = newpc;
		cur_inst_len = 0;
	}

	dsp_core->instr_cycle += 2;
	if (newpc >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}
}

static void dsp_jcc_ea(void)
{
	Uint32 newpc, cc_code;

	dsp_calc_ea((cur_inst >>8) & BITMASK(6), &newpc);
	cc_code=cur_inst & BITMASK(4);

	if (dsp_calc_cc(cc_code)) {
		dsp_core->pc = newpc;
		cur_inst_len = 0;
	}

	dsp_core->instr_cycle += 2;
	if (newpc >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}
}

static void dsp_jclr_aa(void)
{
	Uint32 memspace, addr, value, numbit, newaddr;
	
	memspace = (cur_inst>>6) & 1;
	addr = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core->pc+1);

	dsp_core->instr_cycle += 4;
	if (newaddr >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}

	if ((value & (1<<numbit))==0) {
		dsp_core->pc = newaddr;
		cur_inst_len = 0;
		return;
	} 
	++cur_inst_len;
}

static void dsp_jclr_ea(void)
{
	Uint32 memspace, addr, value, numbit, newaddr;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	newaddr = read_memory_p(dsp_core->pc+1);
	
	dsp_calc_ea(value, &addr);
	value = read_memory(memspace, addr);

	dsp_core->instr_cycle += 4;
	if (newaddr >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}

	if ((value & (1<<numbit))==0) {
		dsp_core->pc = newaddr;
		cur_inst_len = 0;
		return;
	} 
	++cur_inst_len;
}

static void dsp_jclr_pp(void)
{
	Uint32 memspace, addr, value, numbit, newaddr;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	addr = 0xffc0 + value;
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core->pc+1);

	dsp_core->instr_cycle += 4;
	if (newaddr >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}

	if ((value & (1<<numbit))==0) {
		dsp_core->pc = newaddr;
		cur_inst_len = 0;
		return;
	} 
	++cur_inst_len;
}

static void dsp_jclr_reg(void)
{
	Uint32 value, numreg, numbit, newaddr;
	
	numreg = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	newaddr = read_memory_p(dsp_core->pc+1);

	if ((numreg==DSP_REG_A) || (numreg==DSP_REG_B)) {
		dsp_pm_read_accu24(numreg, &value);
	} else {
		value = dsp_core->registers[numreg];
	}

	dsp_core->instr_cycle += 4;
	if (newaddr >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}

	if ((value & (1<<numbit))==0) {
		dsp_core->pc = newaddr;
		cur_inst_len = 0;
		return;
	} 
	++cur_inst_len;
}

static void dsp_jmp_ea(void)
{
	Uint32 newpc;

	dsp_calc_ea((cur_inst>>8) & BITMASK(6), &newpc);
	cur_inst_len = 0;
	dsp_core->pc = newpc;

	dsp_core->instr_cycle += 2;
	if (newpc >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}
}

static void dsp_jmp_imm(void)
{
	Uint32 newpc;

	newpc = cur_inst & BITMASK(12);
	cur_inst_len = 0;
	dsp_core->pc = newpc;

	dsp_core->instr_cycle += 2;
	if (newpc >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}
}

static void dsp_jscc_ea(void)
{
	Uint32 newpc, cc_code;

	dsp_calc_ea((cur_inst >>8) & BITMASK(6), &newpc);
	cc_code=cur_inst & BITMASK(4);

	if (dsp_calc_cc(cc_code)) {
		dsp_stack_push(dsp_core->pc+cur_inst_len, dsp_core->registers[DSP_REG_SR], 0);
		dsp_core->pc = newpc;
		cur_inst_len = 0;
	} 

	dsp_core->instr_cycle += 2;
	if (newpc >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}
}

static void dsp_jscc_imm(void)
{
	Uint32 cc_code, newpc;

	newpc = cur_inst & BITMASK(12);
	cc_code=(cur_inst>>12) & BITMASK(4);
	if (dsp_calc_cc(cc_code)) {
		dsp_stack_push(dsp_core->pc+cur_inst_len, dsp_core->registers[DSP_REG_SR], 0);
		dsp_core->pc = newpc;
		cur_inst_len = 0;
	} 

	dsp_core->instr_cycle += 2;
	if (newpc >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}
}

static void dsp_jsclr_aa(void)
{
	Uint32 memspace, addr, value, newpc, numbit, newaddr;
	
	memspace = (cur_inst>>6) & 1;
	addr = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core->pc+1);
	
	dsp_core->instr_cycle += 4;
	if (newaddr >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}
	
	if ((value & (1<<numbit))==0) {
		dsp_stack_push(dsp_core->pc+2, dsp_core->registers[DSP_REG_SR], 0);
		newpc = newaddr;
		dsp_core->pc = newpc;
		cur_inst_len = 0;
		return;
	} 
	++cur_inst_len;
}

static void dsp_jsclr_ea(void)
{
	Uint32 memspace, addr, value, newpc, numbit, newaddr;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	dsp_calc_ea(value, &addr);
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core->pc+1);

	dsp_core->instr_cycle += 4;
	if (newaddr >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}
	
	if ((value & (1<<numbit))==0) {
		dsp_stack_push(dsp_core->pc+2, dsp_core->registers[DSP_REG_SR], 0);
		newpc = newaddr;
		dsp_core->pc = newpc;
		cur_inst_len = 0;
		return;
	} 
	++cur_inst_len;
}

static void dsp_jsclr_pp(void)
{
	Uint32 memspace, addr, value, newpc, numbit, newaddr;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	addr = 0xffc0 + value;
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core->pc+1);

	dsp_core->instr_cycle += 4;
	if (newaddr >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}
	
	if ((value & (1<<numbit))==0) {
		dsp_stack_push(dsp_core->pc+2, dsp_core->registers[DSP_REG_SR], 0);
		newpc = newaddr;
		dsp_core->pc = newpc;
		cur_inst_len = 0;
		return;
	} 
	++cur_inst_len;
}

static void dsp_jsclr_reg(void)
{
	Uint32 value, numreg, newpc, numbit, newaddr;
	
	numreg = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	newaddr = read_memory_p(dsp_core->pc+1);

	if ((numreg==DSP_REG_A) || (numreg==DSP_REG_B)) {
		dsp_pm_read_accu24(numreg, &value);
	} else {
		value = dsp_core->registers[numreg];
	}

	dsp_core->instr_cycle += 4;
	if (newaddr >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}
	
	if ((value & (1<<numbit))==0) {
		dsp_stack_push(dsp_core->pc+2, dsp_core->registers[DSP_REG_SR], 0);
		newpc = newaddr;
		dsp_core->pc = newpc;
		cur_inst_len = 0;
		return;
	} 
	++cur_inst_len;
}

static void dsp_jset_aa(void)
{
	Uint32 memspace, addr, value, numbit, newpc, newaddr;
	
	memspace = (cur_inst>>6) & 1;
	addr = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core->pc+1);

	dsp_core->instr_cycle += 4;
	if (newaddr >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}
	
	if (value & (1<<numbit)) {
		newpc = newaddr;
		dsp_core->pc = newpc;
		cur_inst_len=0;
		return;
	} 
	++cur_inst_len;
}

static void dsp_jset_ea(void)
{
	Uint32 memspace, addr, value, numbit, newpc, newaddr;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	dsp_calc_ea(value, &addr);
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core->pc+1);

	dsp_core->instr_cycle += 4;
	if (newaddr >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}
	
	if (value & (1<<numbit)) {
		newpc = newaddr;
		dsp_core->pc = newpc;
		cur_inst_len=0;
		return;
	} 
	++cur_inst_len;
}

static void dsp_jset_pp(void)
{
	Uint32 memspace, addr, value, numbit, newpc, newaddr;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	addr = 0xffc0 + value;
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core->pc+1);

	dsp_core->instr_cycle += 4;
	if (newaddr >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}
	
	if (value & (1<<numbit)) {
		newpc = newaddr;
		dsp_core->pc = newpc;
		cur_inst_len=0;
		return;
	} 
	++cur_inst_len;
}

static void dsp_jset_reg(void)
{
	Uint32 value, numreg, numbit, newpc, newaddr;
	
	numreg = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	newaddr = read_memory_p(dsp_core->pc+1);
	
	if ((numreg==DSP_REG_A) || (numreg==DSP_REG_B)) {
		dsp_pm_read_accu24(numreg, &value);
	} else {
		value = dsp_core->registers[numreg];
	}

	dsp_core->instr_cycle += 4;
	if (newaddr >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}
	
	if (value & (1<<numbit)) {
		newpc = newaddr;
		dsp_core->pc = newpc;
		cur_inst_len=0;
		return;
	} 
	++cur_inst_len;
}

static void dsp_jsr_imm(void)
{
	Uint32 newpc;

	newpc = cur_inst & BITMASK(12);

	if (dsp_core->interrupt_state != DSP_INTERRUPT_LONG){
		dsp_stack_push(dsp_core->pc+cur_inst_len, dsp_core->registers[DSP_REG_SR], 0);
	}
	else {
		dsp_core->interrupt_state = DSP_INTERRUPT_DISABLED;
	}

	dsp_core->pc = newpc;
	cur_inst_len = 0;

	dsp_core->instr_cycle += 2;
	if (newpc >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}
}

static void dsp_jsr_ea(void)
{
	Uint32 newpc;

	dsp_calc_ea((cur_inst>>8) & BITMASK(6),&newpc);

	if (dsp_core->interrupt_state != DSP_INTERRUPT_LONG){
		dsp_stack_push(dsp_core->pc+cur_inst_len, dsp_core->registers[DSP_REG_SR], 0);
	}
	else {
		dsp_core->interrupt_state = DSP_INTERRUPT_DISABLED;
	}

	dsp_core->pc = newpc;
	cur_inst_len = 0;

	dsp_core->instr_cycle += 2;
	if (newpc >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}
}

static void dsp_jsset_aa(void)
{
	Uint32 memspace, addr, value, newpc, numbit, newaddr;
	
	memspace = (cur_inst>>6) & 1;
	addr = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core->pc+1);
	
	dsp_core->instr_cycle += 4;
	if (newaddr >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}

	if (value & (1<<numbit)) {
		dsp_stack_push(dsp_core->pc+2, dsp_core->registers[DSP_REG_SR], 0);
		newpc = newaddr;
		dsp_core->pc = newpc;
		cur_inst_len = 0;
		return;
	} 
	++cur_inst_len;
}

static void dsp_jsset_ea(void)
{
	Uint32 memspace, addr, value, newpc, numbit, newaddr;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	dsp_calc_ea(value, &addr);
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core->pc+1);
	
	dsp_core->instr_cycle += 4;
	if (newaddr >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}

	if (value & (1<<numbit)) {
		dsp_stack_push(dsp_core->pc+2, dsp_core->registers[DSP_REG_SR], 0);
		newpc = newaddr;
		dsp_core->pc = newpc;
		cur_inst_len = 0;
		return;
	} 
	++cur_inst_len;
}

static void dsp_jsset_pp(void)
{
	Uint32 memspace, addr, value, newpc, numbit, newaddr;
	
	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	addr = 0xffc0 + value;
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core->pc+1);

	dsp_core->instr_cycle += 4;
	if (newaddr >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}

	if (value & (1<<numbit)) {
		dsp_stack_push(dsp_core->pc+2, dsp_core->registers[DSP_REG_SR], 0);
		newpc = newaddr;
		dsp_core->pc = newpc;
		cur_inst_len = 0;
		return;
	} 
	++cur_inst_len;
}

static void dsp_jsset_reg(void)
{
	Uint32 value, numreg, newpc, numbit, newaddr;
	
	numreg = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	newaddr = read_memory_p(dsp_core->pc+1);
	
	if ((numreg==DSP_REG_A) || (numreg==DSP_REG_B)) {
		dsp_pm_read_accu24(numreg, &value);
	} else {
		value = dsp_core->registers[numreg];
	}

	dsp_core->instr_cycle += 4;
	if (newaddr >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}

	if (value & (1<<numbit)) {
		dsp_stack_push(dsp_core->pc+2, dsp_core->registers[DSP_REG_SR], 0);
		newpc = newaddr;
		dsp_core->pc = newpc;
		cur_inst_len = 0;
		return;
	} 
	++cur_inst_len;
}

static void dsp_lua(void)
{
	Uint32 value, srcreg, dstreg, srcsave, srcnew;

	srcreg = (cur_inst>>8) & BITMASK(3);

	srcsave = dsp_core->registers[DSP_REG_R0+srcreg];
	dsp_calc_ea((cur_inst>>8) & BITMASK(5), &value);
	srcnew = dsp_core->registers[DSP_REG_R0+srcreg];
	dsp_core->registers[DSP_REG_R0+srcreg] = srcsave;

	dstreg = cur_inst & BITMASK(3);
	
	if (cur_inst & (1<<3)) {
		dsp_core->registers[DSP_REG_N0+dstreg] = srcnew;
	} else {
		dsp_core->registers[DSP_REG_R0+dstreg] = srcnew;
	}

	dsp_core->instr_cycle += 2;
}

static void dsp_movec_reg(void)
{
	Uint32 numreg1, numreg2, value, dummy;

	/* S1,D2 */
	/* S2,D1 */

	numreg2 = (cur_inst>>8) & BITMASK(6);
	numreg1 = cur_inst & BITMASK(6);

	if (cur_inst & (1<<15)) {
		/* Write D1 */

		if ((numreg2 == DSP_REG_A) || (numreg2 == DSP_REG_B)) {
			dsp_pm_read_accu24(numreg2, &value); 
		} else {
			value = dsp_core->registers[numreg2];
		}
		value &= BITMASK(registers_mask[numreg1]);
		dsp_write_reg(numreg1, value);
	} else {
		/* Read S1 */
		if (numreg1 == DSP_REG_SSH) {
			dsp_stack_pop(&value, &dummy);
		} 
		else {
			value = dsp_core->registers[numreg1];
		}

		if ((numreg2 == DSP_REG_A) || (numreg2 == DSP_REG_B)) {
			dsp_core->registers[DSP_REG_A2+(numreg2 & 1)] = 0;
			if (value & (1<<23)) {
				dsp_core->registers[DSP_REG_A2+(numreg2 & 1)] = 0xff;
			}
			dsp_core->registers[DSP_REG_A1+(numreg2 & 1)] = value & BITMASK(24);
			dsp_core->registers[DSP_REG_A0+(numreg2 & 1)] = 0;
		} else {
			dsp_core->registers[numreg2] = value & BITMASK(registers_mask[numreg2]);
		}
	}
}

static void dsp_movec_aa(void)
{
	Uint32 numreg, addr, memspace, value, dummy;

	/* x:aa,D1 */
	/* S1,x:aa */
	/* y:aa,D1 */
	/* S1,y:aa */

	numreg = cur_inst & BITMASK(6);
	addr = (cur_inst>>8) & BITMASK(6);
	memspace = (cur_inst>>6) & 1;

	if (cur_inst & (1<<15)) {
		/* Write D1 */
		value = read_memory(memspace, addr);
		value &= BITMASK(registers_mask[numreg]);
		dsp_write_reg(numreg, value);
	} else {
		/* Read S1 */
		if (numreg == DSP_REG_SSH) {
			dsp_stack_pop(&value, &dummy);
		} 
		else {
			value = dsp_core->registers[numreg];
		}
		write_memory(memspace, addr, value);
	}
}

static void dsp_movec_imm(void)
{
	Uint32 numreg, value;

	/* #xx,D1 */
	numreg = cur_inst & BITMASK(6);
	value = (cur_inst>>8) & BITMASK(8);
	value &= BITMASK(registers_mask[numreg]);
	dsp_write_reg(numreg, value);
}

static void dsp_movec_ea(void)
{
	Uint32 numreg, addr, memspace, ea_mode, value, dummy;
	int retour;

	/* x:ea,D1 */
	/* S1,x:ea */
	/* y:ea,D1 */
	/* S1,y:ea */
	/* #xxxx,D1 */

	numreg = cur_inst & BITMASK(6);
	ea_mode = (cur_inst>>8) & BITMASK(6);
	memspace = (cur_inst>>6) & 1;

	if (cur_inst & (1<<15)) {
		/* Write D1 */
		retour = dsp_calc_ea(ea_mode, &addr);
		if (retour) {
			value = addr;
		} else {
			value = read_memory(memspace, addr);
		}
		value &= BITMASK(registers_mask[numreg]);
		dsp_write_reg(numreg, value);
	} else {
		/* Read S1 */
		dsp_calc_ea(ea_mode, &addr);
		if (numreg == DSP_REG_SSH) {
			dsp_stack_pop(&value, &dummy);
		} 
		else {
			value = dsp_core->registers[numreg];
		}
		write_memory(memspace, addr, value);
	}
}

static void dsp_movem_aa(void)
{
	Uint32 numreg, addr, value, dummy;

	numreg = cur_inst & BITMASK(6);
	addr = (cur_inst>>8) & BITMASK(6);

	if  (cur_inst & (1<<15)) {
		/* Write D */
		value = read_memory_p(addr);
		value &= BITMASK(registers_mask[numreg]);
		dsp_write_reg(numreg, value);
	} else {
		/* Read S */
		if (numreg == DSP_REG_SSH) {
			dsp_stack_pop(&value, &dummy);
		} 
		else if ((numreg == DSP_REG_A) || (numreg == DSP_REG_B)) {
			dsp_pm_read_accu24(numreg, &value); 
		} 
		else {
			value = dsp_core->registers[numreg];
		}
		write_memory(DSP_SPACE_P, addr, value);
	}

	dsp_core->instr_cycle += 4;
	if (addr>=0x200) {
		dsp_core->instr_cycle += P_WAITSTATE;
	}
}

static void dsp_movem_ea(void)
{
	Uint32 numreg, addr, ea_mode, value, dummy;

	numreg = cur_inst & BITMASK(6);
	ea_mode = (cur_inst>>8) & BITMASK(6);
	dsp_calc_ea(ea_mode, &addr);

	if  (cur_inst & (1<<15)) {
		/* Write D */
		value = read_memory_p(addr);
		value &= BITMASK(registers_mask[numreg]);
		dsp_write_reg(numreg, value);
	} else {
		/* Read S */
		if (numreg == DSP_REG_SSH) {
			dsp_stack_pop(&value, &dummy);
		} 
		else if ((numreg == DSP_REG_A) || (numreg == DSP_REG_B)) {
			dsp_pm_read_accu24(numreg, &value); 
		} 
		else {
			value = dsp_core->registers[numreg];
		}
		write_memory(DSP_SPACE_P, addr, value);
	}

	dsp_core->instr_cycle += 4;
	if (addr>=0x200) {
		dsp_core->instr_cycle += P_WAITSTATE;
	}
}

static void dsp_movep_0(void)
{
	/* S,x:pp */
	/* x:pp,D */
	/* S,y:pp */
	/* y:pp,D */
	
	Uint32 addr, memspace, numreg, value, dummy;

	addr = 0xffc0 + (cur_inst & BITMASK(6));
	memspace = (cur_inst>>16) & 1;
	numreg = (cur_inst>>8) & BITMASK(6);

	if  (cur_inst & (1<<15)) {
		/* Write pp */
		if ((numreg == DSP_REG_A) || (numreg == DSP_REG_B)) {
			dsp_pm_read_accu24(numreg, &value); 
		}
		else if (numreg == DSP_REG_SSH) {
			dsp_stack_pop(&value, &dummy);
		}
		else {
			value = dsp_core->registers[numreg];
		}
		write_memory(memspace, addr, value);
	} else {
		/* Read pp */
		value = read_memory(memspace, addr);
		value &= BITMASK(registers_mask[numreg]);
		dsp_write_reg(numreg, value);
	}

	dsp_core->instr_cycle += 2;
}

static void dsp_movep_1(void)
{
	/* p:ea,x:pp */
	/* x:pp,p:ea */
	/* p:ea,y:pp */
	/* y:pp,p:ea */

	Uint32 xyaddr, memspace, paddr;

	xyaddr = 0xffc0 + (cur_inst & BITMASK(6));
	dsp_calc_ea((cur_inst>>8) & BITMASK(6), &paddr);
	memspace = (cur_inst>>16) & 1;

	if (cur_inst & (1<<15)) {
		/* Write pp */
		write_memory(memspace, xyaddr, read_memory_p(paddr));
	} else {
		/* Read pp */
		write_memory(DSP_SPACE_P, paddr, read_memory(memspace, xyaddr));
	}

	dsp_core->instr_cycle += 2;
}

static void dsp_movep_23(void)
{
	/* x:ea,x:pp */
	/* y:ea,x:pp */
	/* #xxxxxx,x:pp */
	/* x:pp,x:ea */
	/* x:pp,y:pp */
	/* x:ea,y:pp */
	/* y:ea,y:pp */
	/* #xxxxxx,y:pp */
	/* y:pp,y:ea */
	/* y:pp,x:ea */

	Uint32 addr, peraddr, easpace, perspace, ea_mode;
	int retour;

	peraddr = 0xffc0 + (cur_inst & BITMASK(6));
	perspace = (cur_inst>>16) & 1;
	
	ea_mode = (cur_inst>>8) & BITMASK(6);
	easpace = (cur_inst>>6) & 1;
	retour = dsp_calc_ea(ea_mode, &addr);

	if (cur_inst & (1<<15)) {
		/* Write pp */
		
		if (retour) {
			write_memory(perspace, peraddr, addr);
		} else {
			if (peraddr>=0x200) {
				dsp_core->instr_cycle += P_WAITSTATE;
			}
			write_memory(perspace, peraddr, read_memory(easpace, addr));
		}
	} else {
		/* Read pp */
		if (peraddr>=0x200) {
			dsp_core->instr_cycle += P_WAITSTATE;
		}
		write_memory(easpace, addr, read_memory(perspace, peraddr));
	}

	dsp_core->instr_cycle += 4;
}

static void dsp_norm(void)
{
	Uint32 cursr,cur_e, cur_euz, dest[3], numreg, rreg;
	Uint16 newsr;

	cursr = dsp_core->registers[DSP_REG_SR];
	cur_e = (cursr>>DSP_SR_E) & 1;	/* E */
	cur_euz = ~cur_e;			/* (not E) and U and (not Z) */
	cur_euz &= (cursr>>DSP_SR_U) & 1;
	cur_euz &= ~((cursr>>DSP_SR_Z) & 1);
	cur_euz &= 1;

	numreg = (cur_inst>>3) & 1;
	dest[0] = dsp_core->registers[DSP_REG_A2+numreg];
	dest[1] = dsp_core->registers[DSP_REG_A1+numreg];
	dest[2] = dsp_core->registers[DSP_REG_A0+numreg];
	rreg = DSP_REG_R0+((cur_inst>>8) & BITMASK(3));

	if (cur_euz) {
		newsr = dsp_asl56(dest);
		--dsp_core->registers[rreg];
		dsp_core->registers[rreg] &= BITMASK(16);
	} else if (cur_e) {
		newsr = dsp_asr56(dest);
		++dsp_core->registers[rreg];
		dsp_core->registers[rreg] &= BITMASK(16);
	} else {
		newsr = 0;
	}

	dsp_core->registers[DSP_REG_A2+numreg] = dest[0];
	dsp_core->registers[DSP_REG_A1+numreg] = dest[1];
	dsp_core->registers[DSP_REG_A0+numreg] = dest[2];

	dsp_ccr_update_e_u_n_z(&dest[0], &dest[1], &dest[2]);

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core->registers[DSP_REG_SR] |= newsr;
}

static void dsp_ori(void)
{
	Uint32 regnum, value;

	value = (cur_inst >> 8) & BITMASK(8);
	regnum = cur_inst & BITMASK(2);
	switch(regnum) {
		case 0:
			/* mr */
			dsp_core->registers[DSP_REG_SR] |= value<<8;
			break;
		case 1:
			/* ccr */
			dsp_core->registers[DSP_REG_SR] |= value;
			break;
		case 2:
			/* omr */
			dsp_core->registers[DSP_REG_OMR] |= value;
			break;
	}
}

/*
	REP instruction parameter encoding

	xxxxxxxx 00xxxxxx 0xxxxxxx	aa
	xxxxxxxx 01xxxxxx 0xxxxxxx	ea
	xxxxxxxx YYxxxxxx 1xxxxxxx	imm
	xxxxxxxx 11xxxxxx 0xxxxxxx	reg
*/

static void dsp_rep_aa(void)
{
	/* x:aa */
	/* y:aa */
	dsp_core->registers[DSP_REG_LCSAVE] = dsp_core->registers[DSP_REG_LC];
	dsp_core->pc_on_rep = 1;	/* Not decrement LC at first time */
	dsp_core->loop_rep = 1; 	/* We are now running rep */

	dsp_core->registers[DSP_REG_LC]=read_memory((cur_inst>>6) & 1,(cur_inst>>8) & BITMASK(6));

	dsp_core->instr_cycle += 2;
}

static void dsp_rep_imm(void)
{
	/* #xxx */

	dsp_core->registers[DSP_REG_LCSAVE] = dsp_core->registers[DSP_REG_LC];
	dsp_core->pc_on_rep = 1;	/* Not decrement LC at first time */
	dsp_core->loop_rep = 1; 	/* We are now running rep */

	dsp_core->registers[DSP_REG_LC] = ((cur_inst>>8) & BITMASK(8))
		+ ((cur_inst & BITMASK(4))<<8);

	dsp_core->instr_cycle += 2;
}

static void dsp_rep_ea(void)
{
	Uint32 value;

	/* x:ea */
	/* y:ea */

	dsp_core->registers[DSP_REG_LCSAVE] = dsp_core->registers[DSP_REG_LC];
	dsp_core->pc_on_rep = 1;	/* Not decrement LC at first time */
	dsp_core->loop_rep = 1; 	/* We are now running rep */

	dsp_calc_ea((cur_inst>>8) & BITMASK(6),&value);
	dsp_core->registers[DSP_REG_LC]= read_memory((cur_inst>>6) & 1, value);

	dsp_core->instr_cycle += 2;
}

static void dsp_rep_reg(void)
{
	Uint32 numreg;

	/* R */

	dsp_core->registers[DSP_REG_LCSAVE] = dsp_core->registers[DSP_REG_LC];
	dsp_core->pc_on_rep = 1;	/* Not decrement LC at first time */
	dsp_core->loop_rep = 1; 	/* We are now running rep */

	numreg = (cur_inst>>8) & BITMASK(6);
	if ((numreg == DSP_REG_A) || (numreg == DSP_REG_B)) {
		dsp_pm_read_accu24(numreg, &dsp_core->registers[DSP_REG_LC]); 
	} else {
		dsp_core->registers[DSP_REG_LC] = dsp_core->registers[numreg];
	}
	dsp_core->registers[DSP_REG_LC] &= BITMASK(16);

	dsp_core->instr_cycle += 2;
}

static void dsp_reset(void)
{
	/* Reset external peripherals */
	dsp_core->instr_cycle += 2;
}

static void dsp_rti(void)
{
	Uint32 newpc = 0, newsr = 0;

	dsp_stack_pop(&newpc, &newsr);
	dsp_core->pc = newpc;
	dsp_core->registers[DSP_REG_SR] = newsr;
	cur_inst_len = 0;

	dsp_core->instr_cycle += 2;
	if (newpc >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}
}

static void dsp_rts(void)
{
	Uint32 newpc = 0, newsr;

	dsp_stack_pop(&newpc, &newsr);
	dsp_core->pc = newpc;
	cur_inst_len = 0;

	dsp_core->instr_cycle += 2;
	if (newpc >= 0x200) {
		dsp_core->instr_cycle += 2*P_WAITSTATE;
	}
}

static void dsp_stop(void)
{
#if DSP_DISASM_STATE
	fprintf(stderr, "Dsp: STOP instruction\n");
#endif
}

static void dsp_swi(void)
{
	/* Raise interrupt p:0x0006 */
	dsp_core_add_interrupt(dsp_core, DSP_INTER_SWI);

	dsp_core->instr_cycle += 6;
}

static void dsp_tcc(void)
{
	Uint32 cc_code, regsrc1, regdest1, value;
	Uint32 regsrc2, regdest2;

	cc_code = (cur_inst>>12) & BITMASK(4);

	if (dsp_calc_cc(cc_code)) {
		regsrc1 = registers_tcc[(cur_inst>>3) & BITMASK(4)][0];
		regdest1 = registers_tcc[(cur_inst>>3) & BITMASK(4)][1];

		/* Read S1 */
		if ((regsrc1 == DSP_REG_A) || (regsrc1 == DSP_REG_B)) {
			tmp_parmove_src[0][0]=dsp_core->registers[DSP_REG_A2+(regsrc1 & 1)];
			tmp_parmove_src[0][1]=dsp_core->registers[DSP_REG_A1+(regsrc1 & 1)];
			tmp_parmove_src[0][2]=dsp_core->registers[DSP_REG_A0+(regsrc1 & 1)];
		} else {
			value = dsp_core->registers[regsrc1];
			tmp_parmove_src[0][0]=0;
			if (value & (1<<23)) {
				tmp_parmove_src[0][0]=0x0000ff;
			}
			tmp_parmove_src[0][1]=value;
			tmp_parmove_src[0][2]=0;
		}
		
		/* Write D1 */
		dsp_core->registers[DSP_REG_A2+(regdest1 & 1)]=tmp_parmove_src[0][0];
		dsp_core->registers[DSP_REG_A1+(regdest1 & 1)]=tmp_parmove_src[0][1];
		dsp_core->registers[DSP_REG_A0+(regdest1 & 1)]=tmp_parmove_src[0][2];

		/* S2,D2 transfer */
		if (cur_inst & (1<<16)) {
			regsrc2 = DSP_REG_R0+((cur_inst>>8) & BITMASK(3));
			regdest2 = DSP_REG_R0+(cur_inst & BITMASK(3));

			dsp_core->registers[regdest2] = dsp_core->registers[regsrc2];
		}
	}
}

static void dsp_wait(void)
{
#if DSP_DISASM_STATE
	fprintf(stderr, "Dsp: WAIT instruction\n");
#endif
}

/**********************************
 *	Parallel moves instructions dispatcher
 **********************************/

static void dsp_parmove_read(void)
{
	Uint32 value;

	tmp_parmove_len[0] = tmp_parmove_len[1] = 0;

	/* Calculate needed parallel moves */
	value = (cur_inst >> 20) & BITMASK(4);

	/* Do parallel move read */
	opcodes_parmove[value]();
}

static void dsp_pm_class2(void) {
	Uint32 value;

	dsp_pm_0();
	value = cur_inst & BITMASK(8);
	opcodes_alu[value]();
	dsp_parmove_write();
} 

static void dsp_parmove_write(void)
{
	Uint32 i,j;
	
	for(i=0;i<2;i++) {
		if (tmp_parmove_len[i]==0) {
			continue;
		}

		/* Do parallel move write */
		for (
			j=tmp_parmove_start[i];
			j<tmp_parmove_start[i]+tmp_parmove_len[i];
			j++
		) {
			if (tmp_parmove_type[i]) {
				/* Write to memory */
				write_memory(tmp_parmove_space[i], tmp_parmove_dest[i][j].dsp_address, tmp_parmove_src[i][j]);
			} else {
				Uint32 *dest;

				/* Write to register */
				dest=tmp_parmove_dest[i][j].host_pointer;
				*dest = tmp_parmove_src[i][j];
			}
		}
	}
}

static int dsp_pm_read_accu24(int numreg, Uint32 *dest)
{
	Uint32 scaling, value, reg;
	int got_limited=0;

	/* Read an accumulator, stores it limited */

	scaling = (dsp_core->registers[DSP_REG_SR]>>DSP_SR_S0) & BITMASK(2);
	reg = numreg & 1;

	value = (dsp_core->registers[DSP_REG_A2+reg]) << 24;
	value += dsp_core->registers[DSP_REG_A1+reg];

	switch(scaling) {
		case 0:
			/* No scaling */
			break;
		case 1:
			/* scaling down */
			value >>= 1;
			break;
		case 2:
			/* scaling up */
			value <<= 1;
			value |= (dsp_core->registers[DSP_REG_A0+reg]>>23) & 1;
			break;
		/* indeterminate */
		case 3:	
			break;
	}

	/* limiting ? */
	value &= BITMASK(24);

	if (dsp_core->registers[DSP_REG_A2+reg] == 0) {
		if (value <= 0x007fffff) {
			/* No limiting */
			*dest=value;
			return 0;
		} 
	}

	if (dsp_core->registers[DSP_REG_A2+reg] == 0xff) {
		if (value >= 0x00800000) {
			/* No limiting */
			*dest=value;
			return 0;
		} 
	}

	if (dsp_core->registers[DSP_REG_A2+reg] & (1<<7)) {
		/* Limited to maximum negative value */
		*dest=0x00800000;
		dsp_core->registers[DSP_REG_SR] |= (1<<DSP_SR_L);
		got_limited=1;
	} else {
		/* Limited to maximal positive value */
		*dest=0x007fffff;
		dsp_core->registers[DSP_REG_SR] |= (1<<DSP_SR_L);
		got_limited=1;
	}	

	return got_limited;
}

static void dsp_pm_writereg(int numreg, int position)
{
	if ((numreg == DSP_REG_A) || (numreg == DSP_REG_B)) {
		tmp_parmove_dest[position][0].host_pointer=&dsp_core->registers[DSP_REG_A2+(numreg & 1)];
		tmp_parmove_dest[position][1].host_pointer=&dsp_core->registers[DSP_REG_A1+(numreg & 1)];
		tmp_parmove_dest[position][2].host_pointer=&dsp_core->registers[DSP_REG_A0+(numreg & 1)];

		tmp_parmove_start[position]=0;
		tmp_parmove_len[position]=3;
	} else {
		tmp_parmove_dest[position][1].host_pointer=&dsp_core->registers[numreg];

		tmp_parmove_start[position]=1;
		tmp_parmove_len[position]=1;
	}
}

static void dsp_pm_0(void)
{
	Uint32 memspace, dummy, numreg, value;
/*
	0000 100d 00mm mrrr S,x:ea	x0,D
	0000 100d 10mm mrrr S,y:ea	y0,D
*/
	memspace = (cur_inst>>15) & 1;
	numreg = (cur_inst>>16) & 1;
	dsp_calc_ea((cur_inst>>8) & BITMASK(6), &dummy);

	/* [A|B] to [x|y]:ea */	
	dsp_pm_read_accu24(numreg, &tmp_parmove_src[0][1]);
	tmp_parmove_dest[0][1].dsp_address=dummy;

	tmp_parmove_start[0] = 1;
	tmp_parmove_len[0] = 1;

	tmp_parmove_type[0]=1;
	tmp_parmove_space[0]=memspace;

	/* [x|y]0 to [A|B] */
	value = dsp_core->registers[DSP_REG_X0+(memspace<<1)];
	if (value & (1<<23)) {
		tmp_parmove_src[1][0]=0x0000ff;
	} else {
		tmp_parmove_src[1][0]=0x000000;
	}
	tmp_parmove_src[1][1]=value;
	tmp_parmove_src[1][2]=0x000000;
	tmp_parmove_dest[1][0].host_pointer=&dsp_core->registers[DSP_REG_A2+numreg];
	tmp_parmove_dest[1][1].host_pointer=&dsp_core->registers[DSP_REG_A1+numreg];
	tmp_parmove_dest[1][2].host_pointer=&dsp_core->registers[DSP_REG_A0+numreg];

	tmp_parmove_start[1]=0;
	tmp_parmove_len[1]=3;
	tmp_parmove_type[1]=0;
}

static void dsp_pm_1(void)
{
	Uint32 memspace, numreg, value, xy_addr, retour;
/*
	0001 ffdf w0mm mrrr x:ea,D1		S2,D2
						S1,x:ea		S2,D2
						#xxxxxx,D1	S2,D2
	0001 deff w1mm mrrr S1,D1		y:ea,D2
						S1,D1		S2,y:ea
						S1,D1		#xxxxxx,D2
*/
	value = (cur_inst>>8) & BITMASK(6);

	retour = dsp_calc_ea(value, &xy_addr);	

	memspace = (cur_inst>>14) & 1;
	numreg = DSP_REG_NULL;

	if (memspace) {
		/* Y: */
		switch((cur_inst>>16) & BITMASK(2)) {
			case 0:	numreg = DSP_REG_Y0;	break;
			case 1:	numreg = DSP_REG_Y1;	break;
			case 2:	numreg = DSP_REG_A;		break;
			case 3:	numreg = DSP_REG_B;		break;
		}
	} else {
		/* X: */
		switch((cur_inst>>18) & BITMASK(2)) {
			case 0:	numreg = DSP_REG_X0;	break;
			case 1:	numreg = DSP_REG_X1;	break;
			case 2:	numreg = DSP_REG_A;		break;
			case 3:	numreg = DSP_REG_B;		break;
		}
	}

	if (cur_inst & (1<<15)) {
		/* Write D1 */

		if (retour) {
			value = xy_addr;
		} else {
			value = read_memory(memspace, xy_addr);
		}
		tmp_parmove_src[0][0]= 0x000000;
		if (value & (1<<23)) {
			tmp_parmove_src[0][0]= 0x0000ff;
		}
		tmp_parmove_src[0][1]= value & BITMASK(registers_mask[numreg]);
		tmp_parmove_src[0][2]= 0x000000;

		dsp_pm_writereg(numreg, 0);
		tmp_parmove_type[0]=0;
	} else {
		/* Read S1 */

		if ((numreg==DSP_REG_A) || (numreg==DSP_REG_B)) {
			dsp_pm_read_accu24(numreg, &tmp_parmove_src[0][1]);
		} else {
			tmp_parmove_src[0][1]=dsp_core->registers[numreg];
		}

		tmp_parmove_dest[0][1].dsp_address=xy_addr;

		tmp_parmove_start[0]=1;
		tmp_parmove_len[0]=1;

		tmp_parmove_type[0]=1;
		tmp_parmove_space[0]=memspace;
	}

	/* S2 */
	if (memspace) {
		/* Y: */
		numreg = DSP_REG_A + ((cur_inst>>19) & 1);
	} else {
		/* X: */
		numreg = DSP_REG_A + ((cur_inst>>17) & 1);
	}	
	dsp_pm_read_accu24(numreg, &tmp_parmove_src[1][1]);
	
	/* D2 */
	if (memspace) {
		/* Y: */
		numreg = DSP_REG_X0 + ((cur_inst>>18) & 1);
	} else {
		/* X: */
		numreg = DSP_REG_Y0 + ((cur_inst>>16) & 1);
	}	
	tmp_parmove_src[1][1] &= BITMASK(registers_mask[numreg]);
	tmp_parmove_dest[1][1].host_pointer=&dsp_core->registers[numreg];

	tmp_parmove_start[1]=1;
	tmp_parmove_len[1]=1;

	tmp_parmove_type[1]=0;
}

static void dsp_pm_2(void)
{
	Uint32 dummy;
/*
	0010 0000 0000 0000 nop
	0010 0000 010m mrrr R update
	0010 00ee eeed dddd S,D
	001d dddd iiii iiii #xx,D
*/
	if ((cur_inst & 0xffff00) == 0x200000) {
		return;
	}

	if ((cur_inst & 0xffe000) == 0x204000) {
		dsp_calc_ea((cur_inst>>8) & BITMASK(5), &dummy);
		return;
	}

	if ((cur_inst & 0xfc0000) == 0x200000) {
		dsp_pm_2_2();
		return;
	}

	dsp_pm_3();
}

static void dsp_pm_2_2(void)
{
/*
	0010 00ee eeed dddd S,D
*/
	Uint32 srcreg, dstreg;
	
	srcreg = (cur_inst >> 13) & BITMASK(5);
	dstreg = (cur_inst >> 8) & BITMASK(5);

	tmp_parmove_src[0][0]=
		tmp_parmove_src[0][1]=
		tmp_parmove_src[0][2]= 0x000000;

	if ((srcreg == DSP_REG_A) || (srcreg == DSP_REG_B)) {
		/* Accu to register or accu: limited 24 bits */
		dsp_pm_read_accu24(srcreg, &tmp_parmove_src[0][1]); 
		tmp_parmove_src[0][1] &= BITMASK(registers_mask[dstreg]);
		if (tmp_parmove_src[0][1] & (1<<23)) {
			tmp_parmove_src[0][0]=0x0000ff;
		}
	} else {
		if ((dstreg == DSP_REG_A) || (dstreg == DSP_REG_B)) {
			/* Register to accu: sign extended to 56 bits */
			tmp_parmove_src[0][1]=dsp_core->registers[srcreg] & BITMASK(registers_mask[dstreg]);
			if (tmp_parmove_src[0][1] & (1<<23)) {
				tmp_parmove_src[0][0]=0x0000ff;
			}
		} else {
			/* Register to register: n bits */
			tmp_parmove_src[0][1]=dsp_core->registers[srcreg] & BITMASK(registers_mask[dstreg]);
		}
	}

	dsp_pm_writereg(dstreg, 0);
	tmp_parmove_type[0]=0;
}

static void dsp_pm_3(void)
{
	Uint32 dest, srcvalue;
/*
	001d dddd iiii iiii #xx,R
*/
	dest = (cur_inst >> 16) & BITMASK(5);
	srcvalue = (cur_inst >> 8) & BITMASK(8);

	switch(dest) {
		case DSP_REG_X0:
		case DSP_REG_X1:
		case DSP_REG_Y0:
		case DSP_REG_Y1:
		case DSP_REG_A:
		case DSP_REG_B:
			srcvalue <<= 16;
			break;
	}

	tmp_parmove_src[0][0]=0x000000;
	if ((dest == DSP_REG_A) || (dest == DSP_REG_B)) {
		if (srcvalue & (1<<23)) {
			tmp_parmove_src[0][0]=0x0000ff;
		}
	}
	tmp_parmove_src[0][1]=srcvalue & BITMASK(registers_mask[dest]);
	tmp_parmove_src[0][2]=0x000000;

	dsp_pm_writereg(dest, 0);
	tmp_parmove_type[0]=0;
}

static void dsp_pm_4(void)
{
/*
	0100 l0ll w0aa aaaa 			l:aa,D
						S,l:aa
	0100 l0ll w1mm mrrr 			l:ea,D
						S,l:ea
	01dd 0ddd w0aa aaaa 			x:aa,D
						S,x:aa
	01dd 0ddd w1mm mrrr 			x:ea,D
						S,x:ea
						#xxxxxx,D
	01dd 1ddd w0aa aaaa 			y:aa,D
						S,y:aa
	01dd 1ddd w1mm mrrr 			y:ea,D
						S,y:ea
						#xxxxxx,D
*/
	if ((cur_inst & 0xf40000)==0x400000) {
		dsp_pm_4x();
		return;
	}

	dsp_pm_5();
}

static void dsp_pm_4x(void)
{
	Uint32 value, numreg, l_addr;
/*
	0100 l0ll w0aa aaaa 		l:aa,D
					S,l:aa
	0100 l0ll w1mm mrrr 		l:ea,D
					S,l:ea
*/
	value = (cur_inst>>8) & BITMASK(6);
	if (cur_inst & (1<<14)) {
		dsp_calc_ea(value, &l_addr);	
	} else {
		l_addr = value;
	}

	numreg = (cur_inst>>16) & BITMASK(2);
	numreg |= (cur_inst>>17) & (1<<2);

	/* 2 more cycles are needed if address is in external memory */
	if (l_addr>=0x200) {
		dsp_core->instr_cycle += 2;
	}
	
	if (cur_inst & (1<<15)) {
		/* Write D */
		tmp_parmove_src[0][1] = read_memory(DSP_SPACE_X,l_addr);

		switch(numreg) {
			case 0:
				/* A10 */
				tmp_parmove_src[1][1] = read_memory(DSP_SPACE_Y,l_addr);
				dsp_pm_writereg(DSP_REG_A1, 0);
				dsp_pm_writereg(DSP_REG_A0, 1);
				break;
			case 1:
				/* B10 */
				tmp_parmove_src[1][1] = read_memory(DSP_SPACE_Y,l_addr);
				dsp_pm_writereg(DSP_REG_B1, 0);
				dsp_pm_writereg(DSP_REG_B0, 1);
				break;
			case 2:
				/* X */
				tmp_parmove_src[1][1] = read_memory(DSP_SPACE_Y,l_addr);
				dsp_pm_writereg(DSP_REG_X1, 0);
				dsp_pm_writereg(DSP_REG_X0, 1);
				break;
			case 3:
				/* Y */
				tmp_parmove_src[1][1] = read_memory(DSP_SPACE_Y,l_addr);
				dsp_pm_writereg(DSP_REG_Y1, 0);
				dsp_pm_writereg(DSP_REG_Y0, 1);
				break;
			case 4:
				/* A */
				tmp_parmove_src[0][0] = (tmp_parmove_src[0][1] & (1<<23) ? 0xff : 0);
				tmp_parmove_src[0][2] = read_memory(DSP_SPACE_Y,l_addr);
				dsp_pm_writereg(DSP_REG_A, 0);
				break;
			case 5:
				/* B */
				tmp_parmove_src[0][0] = (tmp_parmove_src[0][1] & (1<<23) ? 0xff : 0);
				tmp_parmove_src[0][2] = read_memory(DSP_SPACE_Y,l_addr);
				dsp_pm_writereg(DSP_REG_B, 0);
				break;
			case 6:
				/* AB */
				tmp_parmove_src[0][0] = (tmp_parmove_src[0][1] & (1<<23) ? 0xff : 0);
				tmp_parmove_src[0][2] = 0;
				tmp_parmove_src[1][1] = read_memory(DSP_SPACE_Y,l_addr);
				tmp_parmove_src[1][0] = (tmp_parmove_src[1][1] & (1<<23) ? 0xff : 0);
				tmp_parmove_src[1][2] = 0;
				dsp_pm_writereg(DSP_REG_A, 0);
				dsp_pm_writereg(DSP_REG_B, 1);
				break;
			case 7:
				/* BA */
				tmp_parmove_src[0][0] = (tmp_parmove_src[0][1] & (1<<23) ? 0xff : 0);
				tmp_parmove_src[0][2] = 0;
				tmp_parmove_src[1][1] = read_memory(DSP_SPACE_Y,l_addr);
				tmp_parmove_src[1][0] = (tmp_parmove_src[1][1] & (1<<23) ? 0xff : 0);
				tmp_parmove_src[1][2] = 0;
				dsp_pm_writereg(DSP_REG_B, 0);
				dsp_pm_writereg(DSP_REG_A, 1);
				break;
		}

		tmp_parmove_type[0]=0;
		tmp_parmove_type[1]=0;
	} else {
		/* Read S */

		switch(numreg) {
			case 0:
				/* A10 */
				tmp_parmove_src[0][1] = dsp_core->registers[DSP_REG_A1];
				tmp_parmove_src[1][1] = dsp_core->registers[DSP_REG_A0];
				break;
			case 1:
				/* B10 */
				tmp_parmove_src[0][1] = dsp_core->registers[DSP_REG_B1];
				tmp_parmove_src[1][1] = dsp_core->registers[DSP_REG_B0];
				break;
			case 2:
				/* X */
				tmp_parmove_src[0][1] = dsp_core->registers[DSP_REG_X1];
				tmp_parmove_src[1][1] = dsp_core->registers[DSP_REG_X0];
				break;
			case 3:
				/* Y */
				tmp_parmove_src[0][1] = dsp_core->registers[DSP_REG_Y1];
				tmp_parmove_src[1][1] = dsp_core->registers[DSP_REG_Y0];
				break;
			case 4:
				/* A */
				if (dsp_pm_read_accu24(DSP_REG_A, &tmp_parmove_src[0][1])) {
					/* Was limited, set lower part */
					tmp_parmove_src[1][1] = (tmp_parmove_src[0][1] & (1<<23) ? 0 : 0xffffff);
				} else {
					/* Not limited */
					tmp_parmove_src[1][1] = dsp_core->registers[DSP_REG_A0];
				}
				break;
			case 5:
				/* B */
				if (dsp_pm_read_accu24(DSP_REG_B, &tmp_parmove_src[0][1])) {
					/* Was limited, set lower part */
					tmp_parmove_src[1][1] = (tmp_parmove_src[0][1] & (1<<23) ? 0 : 0xffffff);
				} else {
					/* Not limited */
					tmp_parmove_src[1][1] = dsp_core->registers[DSP_REG_B0];
				}
				break;
			case 6:
				/* AB */
				dsp_pm_read_accu24(DSP_REG_A, &tmp_parmove_src[0][1]); 
				dsp_pm_read_accu24(DSP_REG_B, &tmp_parmove_src[1][1]); 
				break;
			case 7:
				/* BA */
				dsp_pm_read_accu24(DSP_REG_B, &tmp_parmove_src[0][1]); 
				dsp_pm_read_accu24(DSP_REG_A, &tmp_parmove_src[1][1]); 
				break;
		}
				
		/* D1 */
		tmp_parmove_dest[0][1].dsp_address=l_addr;
		tmp_parmove_start[0]=1;
		tmp_parmove_len[0]=1;
		tmp_parmove_type[0]=1;
		tmp_parmove_space[0]=DSP_SPACE_X;

		/* D2 */
		tmp_parmove_dest[1][1].dsp_address=l_addr;
		tmp_parmove_start[1]=1;
		tmp_parmove_len[1]=1;
		tmp_parmove_type[1]=1;
		tmp_parmove_space[1]=DSP_SPACE_Y;
	}
}

static void dsp_pm_5(void)
{
	Uint32 memspace, numreg, value, xy_addr, retour;
/*
	01dd 0ddd w0aa aaaa 			x:aa,D
						S,x:aa
	01dd 0ddd w1mm mrrr 			x:ea,D
						S,x:ea
						#xxxxxx,D
	01dd 1ddd w0aa aaaa 			y:aa,D
						S,y:aa
	01dd 1ddd w1mm mrrr 			y:ea,D
						S,y:ea
						#xxxxxx,D
*/

	value = (cur_inst>>8) & BITMASK(6);

	if (cur_inst & (1<<14)) {
		retour = dsp_calc_ea(value, &xy_addr);	
	} else {
		xy_addr = value;
		retour = 0;
	}

	memspace = (cur_inst>>19) & 1;
	numreg = (cur_inst>>16) & BITMASK(3);
	numreg |= (cur_inst>>17) & (BITMASK(2)<<3);

	if (cur_inst & (1<<15)) {
		/* Write D */

		if (retour) {
			value = xy_addr;
		} else {
			value = read_memory(memspace, xy_addr);
		}
		tmp_parmove_src[0][1]= value & BITMASK(registers_mask[numreg]);
		tmp_parmove_src[0][2]= 0x000000;
		tmp_parmove_src[0][0]= 0x000000;
		if (value & (1<<23)) {
			tmp_parmove_src[0][0]= 0x0000ff;
		}

		dsp_pm_writereg(numreg, 0);
		tmp_parmove_type[0]=0;
	} else {
		/* Read S */

		if ((numreg==DSP_REG_A) || (numreg==DSP_REG_B)) {
			dsp_pm_read_accu24(numreg, &tmp_parmove_src[0][1]);
		} else {
			tmp_parmove_src[0][1]=dsp_core->registers[numreg];
		}

		tmp_parmove_dest[0][1].dsp_address=xy_addr;

		tmp_parmove_start[0]=1;
		tmp_parmove_len[0]=1;

		tmp_parmove_type[0]=1;
		tmp_parmove_space[0]=memspace;
	}
}

static void dsp_pm_8(void)
{
	Uint32 ea1, ea2;
	Uint32 numreg1, numreg2;
	Uint32 value, x_addr, y_addr;
/*
	1wmm eeff WrrM MRRR 			x:ea,D1		y:ea,D2	
						x:ea,D1		S2,y:ea
						S1,x:ea		y:ea,D2
						S1,x:ea		S2,y:ea
*/
	numreg1 = numreg2 = DSP_REG_NULL;

	ea1 = (cur_inst>>8) & BITMASK(5);
	if ((ea1>>3) == 0) {
		ea1 |= (1<<5);
	}
	ea2 = (cur_inst>>13) & BITMASK(2);
	ea2 |= (cur_inst>>17) & (BITMASK(2)<<3);
	if ((ea1 & (1<<2))==0) {
		ea2 |= 1<<2;
	}
	if ((ea2>>3) == 0) {
		ea2 |= (1<<5);
	}

	dsp_calc_ea(ea1, &x_addr);
	dsp_calc_ea(ea2, &y_addr);

	/* 2 more cycles are needed if X:address1 and Y:address2 are both in external memory */
	if ((x_addr>=0x200) && (y_addr>=0x200)) {
		dsp_core->instr_cycle += 2;
	}

	switch((cur_inst>>18) & BITMASK(2)) {
		case 0:	numreg1=DSP_REG_X0;	break;
		case 1:	numreg1=DSP_REG_X1;	break;
		case 2:	numreg1=DSP_REG_A;	break;
		case 3:	numreg1=DSP_REG_B;	break;
	}
	switch((cur_inst>>16) & BITMASK(2)) {
		case 0:	numreg2=DSP_REG_Y0;	break;
		case 1:	numreg2=DSP_REG_Y1;	break;
		case 2:	numreg2=DSP_REG_A;	break;
		case 3:	numreg2=DSP_REG_B;	break;
	}
	
	if (cur_inst & (1<<15)) {
		/* Write D1 */

		value = read_memory(DSP_SPACE_X, x_addr);
		tmp_parmove_src[0][0]= 0x000000;
		if (value & (1<<23)) {
			tmp_parmove_src[0][0]= 0x0000ff;
		}
		tmp_parmove_src[0][1]= value & BITMASK(registers_mask[numreg1]);
		tmp_parmove_src[0][2]= 0x000000;
		dsp_pm_writereg(numreg1, 0);
		tmp_parmove_type[0]=0;
	} else {
		/* Read S1 */

		if ((numreg1==DSP_REG_A) || (numreg1==DSP_REG_B)) {
			dsp_pm_read_accu24(numreg1, &tmp_parmove_src[0][1]);
		} else {
			tmp_parmove_src[0][1]=dsp_core->registers[numreg1];
		}
		tmp_parmove_dest[0][1].dsp_address=x_addr;
		tmp_parmove_start[0]=1;
		tmp_parmove_len[0]=1;
		tmp_parmove_type[0]=1;
		tmp_parmove_space[0]=DSP_SPACE_X;
	}

	if (cur_inst & (1<<22)) {
		/* Write D2 */

		value = read_memory(DSP_SPACE_Y, y_addr);
		tmp_parmove_src[1][0]= 0x000000;
		if (value & (1<<23)) {
			tmp_parmove_src[1][0]= 0x0000ff;
		}
		tmp_parmove_src[1][1]= value & BITMASK(registers_mask[numreg2]);
		tmp_parmove_src[1][2]= 0x000000;
		dsp_pm_writereg(numreg2, 1);
		tmp_parmove_type[1]=0;
	} else {
		/* Read S2 */
		if ((numreg2==DSP_REG_A) || (numreg2==DSP_REG_B)) {
			dsp_pm_read_accu24(numreg2, &tmp_parmove_src[1][1]);
		} else {
			tmp_parmove_src[1][1]=dsp_core->registers[numreg2];
		}

		tmp_parmove_dest[1][1].dsp_address=y_addr;
		tmp_parmove_start[1]=1;
		tmp_parmove_len[1]=1;
		tmp_parmove_type[1]=1;
		tmp_parmove_space[1]=DSP_SPACE_Y;
	}
}

/**********************************
 *	56bit arithmetic
 **********************************/

/* source,dest[0] is 55:48 */
/* source,dest[1] is 47:24 */
/* source,dest[2] is 23:00 */

static Uint16 dsp_abs56(Uint32 *dest)
{
	Uint32 zerodest[3];
	Uint16 newsr;

	/* D=|D| */

	if (dest[0] & (1<<7)) {
		zerodest[0] = zerodest[1] = zerodest[2] = 0;

		newsr = dsp_sub56(dest, zerodest);

		dest[0] = zerodest[0];
		dest[1] = zerodest[1];
		dest[2] = zerodest[2];
	} else {
		newsr = 0;
	}

	return newsr;
}

static Uint16 dsp_asl56(Uint32 *dest)
{
	Uint16 overflow, carry;

	/* Shift left dest 1 bit: D<<=1 */

	carry = (dest[0]>>7) & 1;

	dest[0] <<= 1;
	dest[0] |= (dest[1]>>23) & 1;
	dest[0] &= BITMASK(8);

	dest[1] <<= 1;
	dest[1] |= (dest[2]>>23) & 1;
	dest[1] &= BITMASK(24);
	
	dest[2] <<= 1;
	dest[2] &= BITMASK(24);

	overflow = (carry != ((dest[0]>>7) & 1));

	return (overflow<<DSP_SR_L)|(overflow<<DSP_SR_V)|(carry<<DSP_SR_C);
}

static Uint16 dsp_asr56(Uint32 *dest)
{
	Uint16 carry;

	/* Shift right dest 1 bit: D>>=1 */

	carry = dest[2] & 1;

	dest[2] >>= 1;
	dest[2] &= BITMASK(23);
	dest[2] |= (dest[1] & 1)<<23;

	dest[1] >>= 1;
	dest[1] &= BITMASK(23);
	dest[1] |= (dest[0] & 1)<<23;

	dest[0] >>= 1;
	dest[0] &= BITMASK(7);
	dest[0] |= (dest[0] & (1<<6))<<1;

	return (carry<<DSP_SR_C);
}

static Uint16 dsp_add56(Uint32 *source, Uint32 *dest)
{
	Uint16 overflow, carry, flg_s, flg_d, flg_r;

	flg_s = (source[0]>>7) & 1;
	flg_d = (dest[0]>>7) & 1;

	/* Add source to dest: D = D+S */
	dest[2] += source[2];
	dest[1] += source[1]+((dest[2]>>24) & 1);
	dest[0] += source[0]+((dest[1]>>24) & 1);

	carry = (dest[0]>>8) & 1;

	dest[2] &= BITMASK(24);
	dest[1] &= BITMASK(24);
	dest[0] &= BITMASK(8);

	flg_r = (dest[0]>>7) & 1;

	/*set overflow*/
	overflow = (flg_s ^ flg_r) & (flg_d ^ flg_r);

	return (overflow<<DSP_SR_L)|(overflow<<DSP_SR_V)|(carry<<DSP_SR_C);
}

static Uint16 dsp_sub56(Uint32 *source, Uint32 *dest)
{
	Uint16 overflow, carry, flg_s, flg_d, flg_r, dest_save;

	dest_save = dest[0];

	/* Substract source from dest: D = D-S */
	dest[2] -= source[2];
	dest[1] -= source[1]+((dest[2]>>24) & 1);
	dest[0] -= source[0]+((dest[1]>>24) & 1);

	carry = (dest[0]>>8) & 1;

	dest[2] &= BITMASK(24);
	dest[1] &= BITMASK(24);
	dest[0] &= BITMASK(8);

	flg_s = (source[0]>>7) & 1;
	flg_d = (dest_save>>7) & 1;
	flg_r = (dest[0]>>7) & 1;

	/* set overflow */
	overflow = (flg_s ^ flg_d) & (flg_r ^ flg_d);

	return (overflow<<DSP_SR_L)|(overflow<<DSP_SR_V)|(carry<<DSP_SR_C);
}

static void dsp_mul56(Uint32 source1, Uint32 source2, Uint32 *dest, Uint8 signe)
{
	Uint32 part[4], zerodest[3], value;

	/* Multiply: D = S1*S2 */
	if (source1 & (1<<23)) {
		signe ^= 1;
		source1 = (1<<24) - (source1 & BITMASK(24));
	}
	if (source2 & (1<<23)) {
		signe ^= 1;
		source2 = (1<<24) - (source2 & BITMASK(24));
	}

	/* bits 0-11 * bits 0-11 */
	part[0]=(source1 & BITMASK(12))*(source2 & BITMASK(12));
	/* bits 12-23 * bits 0-11 */
	part[1]=((source1>>12) & BITMASK(12))*(source2 & BITMASK(12));
	/* bits 0-11 * bits 12-23 */
	part[2]=(source1 & BITMASK(12))*((source2>>12)  & BITMASK(12));
	/* bits 12-23 * bits 12-23 */
	part[3]=((source1>>12) & BITMASK(12))*((source2>>12) & BITMASK(12));

	/* Calc dest 2 */
	dest[2] = part[0];
	dest[2] += (part[1] & BITMASK(12)) << 12;
	dest[2] += (part[2] & BITMASK(12)) << 12;

	/* Calc dest 1 */
	dest[1] = (part[1]>>12) & BITMASK(12);
	dest[1] += (part[2]>>12) & BITMASK(12);
	dest[1] += part[3];

	/* Calc dest 0 */
	dest[0] = 0;

	/* Add carries */
	value = (dest[2]>>24) & BITMASK(8);
	if (value) {
		dest[1] += value;
		dest[2] &= BITMASK(24);
	}
	value = (dest[1]>>24) & BITMASK(8);
	if (value) {
		dest[0] += value;
		dest[1] &= BITMASK(24);
	}

	/* Get rid of extra sign bit */
	dsp_asl56(dest);

	if (signe) {
		zerodest[0] = zerodest[1] = zerodest[2] = 0;

		dsp_sub56(dest, zerodest);

		dest[0] = zerodest[0];
		dest[1] = zerodest[1];
		dest[2] = zerodest[2];
	}
}

static void dsp_rnd56(Uint32 *dest)
{
	Uint32 rnd_const[3];

	rnd_const[0] = 0;

	/* Scaling mode S0 */
	if (dsp_core->registers[DSP_REG_SR] & (1<<DSP_SR_S0)) {
		rnd_const[1] = 1;
		rnd_const[2] = 0;
		dsp_add56(rnd_const, dest);

		if ((dest[2]==0) && ((dest[1] & 1) == 0)) {
			dest[1] &= (0xffffff - 0x3);
		}
		dest[1] &= 0xfffffe;
		dest[2]=0;
	}
	/* Scaling mode S1 */
	else if (dsp_core->registers[DSP_REG_SR] & (1<<DSP_SR_S1)) {
		rnd_const[1] = 0;
		rnd_const[2] = (1<<22);
		dsp_add56(rnd_const, dest);
   
		if ((dest[2] & 0x7fffff) == 0){
			dest[2] = 0;
		}
		dest[2] &= 0x800000;
	}
	/* No Scaling */
	else {
		rnd_const[1] = 0;
		rnd_const[2] = (1<<23);
		dsp_add56(rnd_const, dest);

		if (dest[2] == 0) {
			dest[1] &= 0xfffffe;
		}
		dest[2]=0;
	}
}

/**********************************
 *	Parallel moves instructions
 **********************************/

static void dsp_abs(void)
{
	Uint32 numreg, dest[3], overflowed;

	numreg = (cur_inst>>3) & 1;

	dest[0] = dsp_core->registers[DSP_REG_A2+numreg];
	dest[1] = dsp_core->registers[DSP_REG_A1+numreg];
	dest[2] = dsp_core->registers[DSP_REG_A0+numreg];

	overflowed = ((dest[2]==0) && (dest[1]==0) && (dest[0]==0x80));

	dsp_abs56(dest);

	dsp_core->registers[DSP_REG_A2+numreg] = dest[0];
	dsp_core->registers[DSP_REG_A1+numreg] = dest[1];
	dsp_core->registers[DSP_REG_A0+numreg] = dest[2];

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core->registers[DSP_REG_SR] |= (overflowed<<DSP_SR_L)|(overflowed<<DSP_SR_V);

	dsp_ccr_update_e_u_n_z(&dest[0], &dest[1], &dest[2]);
}

static void dsp_adc(void)
{
	Uint32 srcreg, destreg, source[3], dest[3], curcarry;
	Uint16 newsr;

	curcarry = (dsp_core->registers[DSP_REG_SR]>>DSP_SR_C) & 1;

	destreg = (cur_inst>>3) & 1;
	dest[0] = dsp_core->registers[DSP_REG_A2+destreg];
	dest[1] = dsp_core->registers[DSP_REG_A1+destreg];
	dest[2] = dsp_core->registers[DSP_REG_A0+destreg];

	srcreg = (cur_inst>>4) & 1;
	if (srcreg == 0) {	/* X */
		source[1] = dsp_core->registers[DSP_REG_X1];
		source[2] = dsp_core->registers[DSP_REG_X0];
		source[0] = 0;
		if (source[1] & (1<<23)) {
			source[0] = 0x0000ff;
		}
	}
	else {	/* Y */
		source[1] = dsp_core->registers[DSP_REG_Y1];
		source[2] = dsp_core->registers[DSP_REG_Y0];
		source[0] = 0;
		if (source[1] & (1<<23)) {
			source[0] = 0x0000ff;
		}
	}

	newsr = dsp_add56(source, dest);
	
	if (curcarry) {
		source[0]=0;
		source[1]=0;
		source[2]=1;
		newsr |= dsp_add56(source, dest);
	}

	dsp_core->registers[DSP_REG_A2+destreg] = dest[0];
	dsp_core->registers[DSP_REG_A1+destreg] = dest[1];
	dsp_core->registers[DSP_REG_A0+destreg] = dest[2];

	dsp_ccr_update_e_u_n_z(&dest[0], &dest[1], &dest[2]);

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core->registers[DSP_REG_SR] |= newsr;
}

static void dsp_add(void)
{
	Uint32 srcreg, destreg, source[3], dest[3];
	Uint16 newsr;

	destreg = (cur_inst>>3) & 1;
	dest[0] = dsp_core->registers[DSP_REG_A2+destreg];
	dest[1] = dsp_core->registers[DSP_REG_A1+destreg];
	dest[2] = dsp_core->registers[DSP_REG_A0+destreg];

	srcreg = (cur_inst>>4) & BITMASK(3);
	switch(srcreg) {
		case 1:	/* A or B */
			srcreg = destreg ^ 1;
			source[0] = dsp_core->registers[DSP_REG_A2+srcreg];
			source[1] = dsp_core->registers[DSP_REG_A1+srcreg];
			source[2] = dsp_core->registers[DSP_REG_A0+srcreg];
			break;
		case 2:	/* X */
			source[1] = dsp_core->registers[DSP_REG_X1];
			source[2] = dsp_core->registers[DSP_REG_X0];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		case 3:	/* Y */
			source[1] = dsp_core->registers[DSP_REG_Y1];
			source[2] = dsp_core->registers[DSP_REG_Y0];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		case 4:	/* X0 */
			source[2] = 0;
			source[1] = dsp_core->registers[DSP_REG_X0];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		case 5:	/* Y0 */
			source[2] = 0;
			source[1] = dsp_core->registers[DSP_REG_Y0];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		case 6:	/* X1 */
			source[2] = 0;
			source[1] = dsp_core->registers[DSP_REG_X1];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		case 7:	/* Y1 */
			source[2] = 0;
			source[1] = dsp_core->registers[DSP_REG_Y1];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		default:
			fprintf(stderr, "Dsp: source register undefined! dsp_cpu.cpp: %d\n", __LINE__);
			return;
	}

	newsr = dsp_add56(source, dest);

	dsp_core->registers[DSP_REG_A2+destreg] = dest[0];
	dsp_core->registers[DSP_REG_A1+destreg] = dest[1];
	dsp_core->registers[DSP_REG_A0+destreg] = dest[2];

	dsp_ccr_update_e_u_n_z(&dest[0], &dest[1], &dest[2]);

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core->registers[DSP_REG_SR] |= newsr;
}

static void dsp_addl(void)
{
	Uint32 numreg, source[3], dest[3];
	Uint16 newsr;

	numreg = (cur_inst>>3) & 1;

	dest[0] = dsp_core->registers[DSP_REG_A2+numreg];
	dest[1] = dsp_core->registers[DSP_REG_A1+numreg];
	dest[2] = dsp_core->registers[DSP_REG_A0+numreg];
	newsr = dsp_asl56(dest);

	source[0] = dsp_core->registers[DSP_REG_A2+(numreg ^ 1)];
	source[1] = dsp_core->registers[DSP_REG_A1+(numreg ^ 1)];
	source[2] = dsp_core->registers[DSP_REG_A0+(numreg ^ 1)];
	newsr |= dsp_add56(source, dest);

	dsp_core->registers[DSP_REG_A2+numreg] = dest[0];
	dsp_core->registers[DSP_REG_A1+numreg] = dest[1];
	dsp_core->registers[DSP_REG_A0+numreg] = dest[2];

	dsp_ccr_update_e_u_n_z(&dest[0], &dest[1], &dest[2]);

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core->registers[DSP_REG_SR] |= newsr;
}

static void dsp_addr(void)
{
	Uint32 numreg, source[3], dest[3];
	Uint16 newsr;

	numreg = (cur_inst>>3) & 1;

	dest[0] = dsp_core->registers[DSP_REG_A2+numreg];
	dest[1] = dsp_core->registers[DSP_REG_A1+numreg];
	dest[2] = dsp_core->registers[DSP_REG_A0+numreg];
	newsr = dsp_asr56(dest);

	source[0] = dsp_core->registers[DSP_REG_A2+(numreg ^ 1)];
	source[1] = dsp_core->registers[DSP_REG_A1+(numreg ^ 1)];
	source[2] = dsp_core->registers[DSP_REG_A0+(numreg ^ 1)];
	newsr |= dsp_add56(source, dest);

	dsp_core->registers[DSP_REG_A2+numreg] = dest[0];
	dsp_core->registers[DSP_REG_A1+numreg] = dest[1];
	dsp_core->registers[DSP_REG_A0+numreg] = dest[2];

	dsp_ccr_update_e_u_n_z(&dest[0], &dest[1], &dest[2]);

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core->registers[DSP_REG_SR] |= newsr;
}

static void dsp_and(void)
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
	dstreg = DSP_REG_A1+((cur_inst>>3) & 1);

	dsp_core->registers[dstreg] &= dsp_core->registers[srcreg];

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core->registers[DSP_REG_SR] |= ((dsp_core->registers[dstreg]>>23) & 1)<<DSP_SR_N;
	dsp_core->registers[DSP_REG_SR] |= (dsp_core->registers[dstreg]==0)<<DSP_SR_Z;
}

static void dsp_asl(void)
{
	Uint32 numreg, dest[3];
	Uint16 newsr;

	numreg = (cur_inst>>3) & 1;

	dest[0] = dsp_core->registers[DSP_REG_A2+numreg];
	dest[1] = dsp_core->registers[DSP_REG_A1+numreg];
	dest[2] = dsp_core->registers[DSP_REG_A0+numreg];

	newsr = dsp_asl56(dest);

	dsp_core->registers[DSP_REG_A2+numreg] = dest[0];
	dsp_core->registers[DSP_REG_A1+numreg] = dest[1];
	dsp_core->registers[DSP_REG_A0+numreg] = dest[2];

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_C)|(1<<DSP_SR_V));
	dsp_core->registers[DSP_REG_SR] |= newsr;

	dsp_ccr_update_e_u_n_z(&dest[0], &dest[1], &dest[2]);
}

static void dsp_asr(void)
{
	Uint32 numreg, newsr, dest[3];

	numreg = (cur_inst>>3) & 1;

	dest[0] = dsp_core->registers[DSP_REG_A2+numreg];
	dest[1] = dsp_core->registers[DSP_REG_A1+numreg];
	dest[2] = dsp_core->registers[DSP_REG_A0+numreg];

	newsr = dsp_asr56(dest);

	dsp_core->registers[DSP_REG_A2+numreg] = dest[0];
	dsp_core->registers[DSP_REG_A1+numreg] = dest[1];
	dsp_core->registers[DSP_REG_A0+numreg] = dest[2];

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_C)|(1<<DSP_SR_V));
	dsp_core->registers[DSP_REG_SR] |= newsr;

	dsp_ccr_update_e_u_n_z(&dest[0], &dest[1], &dest[2]);
}

static void dsp_clr(void)
{
	Uint32 numreg;

	numreg = (cur_inst>>3) & 1;

	dsp_core->registers[DSP_REG_A2+numreg]=0;
	dsp_core->registers[DSP_REG_A1+numreg]=0;
	dsp_core->registers[DSP_REG_A0+numreg]=0;

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_E)|(1<<DSP_SR_N)|(1<<DSP_SR_V));
	dsp_core->registers[DSP_REG_SR] |= (1<<DSP_SR_U)|(1<<DSP_SR_Z);
}

static void dsp_cmp(void)
{
	Uint32 srcreg, destreg, source[3], dest[3];
	Uint16 newsr;

	destreg = (cur_inst>>3) & 1;
	dest[0] = dsp_core->registers[DSP_REG_A2+destreg];
	dest[1] = dsp_core->registers[DSP_REG_A1+destreg];
	dest[2] = dsp_core->registers[DSP_REG_A0+destreg];

	srcreg = (cur_inst>>4) & BITMASK(3);
	switch(srcreg) {
		case 0:	/* A or B */
			srcreg = destreg ^ 1;
			source[0] = dsp_core->registers[DSP_REG_A2+srcreg];
			source[1] = dsp_core->registers[DSP_REG_A1+srcreg];
			source[2] = dsp_core->registers[DSP_REG_A0+srcreg];
			break;
		case 4:	/* X0 */
			source[2] = 0;
			source[1] = dsp_core->registers[DSP_REG_X0];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		case 5:	/* Y0 */
			source[2] = 0;
			source[1] = dsp_core->registers[DSP_REG_Y0];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		case 6:	/* X1 */
			source[2] = 0;
			source[1] = dsp_core->registers[DSP_REG_X1];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		case 7:	/* Y1 */
			source[2] = 0;
			source[1] = dsp_core->registers[DSP_REG_Y1];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		default:
			fprintf(stderr, "source register undefined! dsp_cpu.cpp: %d\n", __LINE__);
			return;
	}

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(&dest[0], &dest[1], &dest[2]);

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core->registers[DSP_REG_SR] |= newsr;
}

static void dsp_cmpm(void)
{
	Uint32 srcreg, destreg, source[3], dest[3];
	Uint16 newsr;

	destreg = (cur_inst>>3) & 1;
	dest[0] = dsp_core->registers[DSP_REG_A2+destreg];
	dest[1] = dsp_core->registers[DSP_REG_A1+destreg];
	dest[2] = dsp_core->registers[DSP_REG_A0+destreg];
	dsp_abs56(dest);

	srcreg = (cur_inst>>4) & BITMASK(3);
	switch(srcreg) {
		case 0:	/* A or B */
			srcreg = destreg ^ 1;
			source[0] = dsp_core->registers[DSP_REG_A2+srcreg];
			source[1] = dsp_core->registers[DSP_REG_A1+srcreg];
			source[2] = dsp_core->registers[DSP_REG_A0+srcreg];
			break;
		case 4:	/* X0 */
			source[2] = 0;
			source[1] = dsp_core->registers[DSP_REG_X0];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		case 5:	/* Y0 */
			source[2] = 0;
			source[1] = dsp_core->registers[DSP_REG_Y0];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		case 6:	/* X1 */
			source[2] = 0;
			source[1] = dsp_core->registers[DSP_REG_X1];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		case 7:	/* Y1 */
			source[2] = 0;
			source[1] = dsp_core->registers[DSP_REG_Y1];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		default:
			fprintf(stderr, "source register undefined! dsp_cpu.cpp: %d\n", __LINE__);
			return;
	}

	dsp_abs56(source);
	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(&dest[0], &dest[1], &dest[2]);

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core->registers[DSP_REG_SR] |= newsr;
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
	dstreg = DSP_REG_A1+((cur_inst>>3) & 1);

	dsp_core->registers[dstreg] ^= dsp_core->registers[srcreg];
	dsp_core->registers[dstreg] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core->registers[DSP_REG_SR] |= ((dsp_core->registers[dstreg]>>23) & 1)<<DSP_SR_N;
	dsp_core->registers[DSP_REG_SR] |= (dsp_core->registers[dstreg]==0)<<DSP_SR_Z;
}

static void dsp_lsl(void)
{
	Uint32 numreg, newcarry;

	numreg = DSP_REG_A1+((cur_inst>>3) & 1);

	newcarry = (dsp_core->registers[numreg]>>23) & 1;

	dsp_core->registers[numreg] <<= 1;
	dsp_core->registers[numreg] &= BITMASK(24);

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_C)|(1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core->registers[DSP_REG_SR] |= newcarry;
	dsp_core->registers[DSP_REG_SR] |= ((dsp_core->registers[numreg]>>23) & 1)<<DSP_SR_N;
	dsp_core->registers[DSP_REG_SR] |= (dsp_core->registers[numreg]==0)<<DSP_SR_Z;
}

static void dsp_lsr(void)
{
	Uint32 numreg, newcarry;

	numreg = DSP_REG_A1+((cur_inst>>3) & 1);

	newcarry = dsp_core->registers[numreg] & 1;

	dsp_core->registers[numreg] >>= 1;
	/*dsp_core->registers[numreg] &= BITMASK(24);*/

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_C)|(1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core->registers[DSP_REG_SR] |= newcarry;
	dsp_core->registers[DSP_REG_SR] |= (dsp_core->registers[numreg]==0)<<DSP_SR_Z;
}

static void dsp_mac(void)
{
	Uint32 srcreg1, srcreg2, destreg, value, source[3], dest[3];
	Uint16 newsr;

	value = (cur_inst>>4) & BITMASK(3);
	srcreg1 = registers_mpy[value][0];
	srcreg2 = registers_mpy[value][1];

	dsp_mul56(dsp_core->registers[srcreg1], dsp_core->registers[srcreg2], source, (cur_inst >> 2) & 1);

	destreg = (cur_inst>>3) & 1;
	dest[0] = dsp_core->registers[DSP_REG_A2+destreg];
	dest[1] = dsp_core->registers[DSP_REG_A1+destreg];
	dest[2] = dsp_core->registers[DSP_REG_A0+destreg];
	newsr = dsp_add56(source, dest);

	dsp_core->registers[DSP_REG_A2+destreg] = dest[0];
	dsp_core->registers[DSP_REG_A1+destreg] = dest[1];
	dsp_core->registers[DSP_REG_A0+destreg] = dest[2];

	dsp_ccr_update_e_u_n_z(&dest[0], &dest[1], &dest[2]);

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core->registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr(void)
{
	Uint32 srcreg1, srcreg2, destreg, value, source[3], dest[3];
	Uint16 newsr;

	value = (cur_inst>>4) & BITMASK(3);
	srcreg1 = registers_mpy[value][0];
	srcreg2 = registers_mpy[value][1];

	dsp_mul56(dsp_core->registers[srcreg1], dsp_core->registers[srcreg2], source, (cur_inst >> 2) & 1);

	destreg = (cur_inst>>3) & 1;
	dest[0] = dsp_core->registers[DSP_REG_A2+destreg];
	dest[1] = dsp_core->registers[DSP_REG_A1+destreg];
	dest[2] = dsp_core->registers[DSP_REG_A0+destreg];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core->registers[DSP_REG_A2+destreg] = dest[0];
	dsp_core->registers[DSP_REG_A1+destreg] = dest[1];
	dsp_core->registers[DSP_REG_A0+destreg] = dest[2];

	dsp_ccr_update_e_u_n_z(&dest[0], &dest[1], &dest[2]);

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core->registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_move(void)
{
	/*	move instruction inside alu opcodes
		taken care of by parallel move dispatcher */
}

static void dsp_mpy(void)
{
	Uint32 srcreg1, srcreg2, destreg, value, source[3];

	value = (cur_inst>>4) & BITMASK(3);
	srcreg1 = registers_mpy[value][0];
	srcreg2 = registers_mpy[value][1];

	dsp_mul56(dsp_core->registers[srcreg1], dsp_core->registers[srcreg2], source, (cur_inst >> 2) & 1);

	destreg = (cur_inst>>3) & 1;

	dsp_core->registers[DSP_REG_A2+destreg] = source[0];
	dsp_core->registers[DSP_REG_A1+destreg] = source[1];
	dsp_core->registers[DSP_REG_A0+destreg] = source[2];

	dsp_ccr_update_e_u_n_z(	&dsp_core->registers[DSP_REG_A2+destreg],
				&dsp_core->registers[DSP_REG_A1+destreg],
				&dsp_core->registers[DSP_REG_A0+destreg]);

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr(void)
{
	Uint32 srcreg1, srcreg2, destreg, value, dest[3], source[3];

	value = (cur_inst>>4) & BITMASK(3);
	srcreg1 = registers_mpy[value][0];
	srcreg2 = registers_mpy[value][1];

	dsp_mul56(dsp_core->registers[srcreg1], dsp_core->registers[srcreg2], source, (cur_inst >> 2) & 1);

	destreg = (cur_inst>>3) & 1;

	dest[0] = source[0];
	dest[1] = source[1];
	dest[2] = source[2];

	dsp_rnd56(dest);

	dsp_core->registers[DSP_REG_A2+destreg] = dest[0];
	dsp_core->registers[DSP_REG_A1+destreg] = dest[1];
	dsp_core->registers[DSP_REG_A0+destreg] = dest[2];

	dsp_ccr_update_e_u_n_z(&dest[0], &dest[1], &dest[2]);

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_neg(void)
{
	Uint32 srcreg, source[3], dest[3], overflowed;

	srcreg = (cur_inst>>3) & 1;
	source[0] = dsp_core->registers[DSP_REG_A2+srcreg];
	source[1] = dsp_core->registers[DSP_REG_A1+srcreg];
	source[2] = dsp_core->registers[DSP_REG_A0+srcreg];

	overflowed = ((source[2]==0) && (source[1]==0) && (source[0]==0x80));

	dest[0] = dest[1] = dest[2] = 0;

	dsp_sub56(source, dest);

	dsp_core->registers[DSP_REG_A2+srcreg] = dest[0];
	dsp_core->registers[DSP_REG_A1+srcreg] = dest[1];
	dsp_core->registers[DSP_REG_A0+srcreg] = dest[2];

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core->registers[DSP_REG_SR] |= (overflowed<<DSP_SR_L)|(overflowed<<DSP_SR_V);

	dsp_ccr_update_e_u_n_z(&dest[0], &dest[1], &dest[2]);
}

static void dsp_nop(void)
{
}

static void dsp_not(void)
{
	Uint32 dstreg;

	dstreg = DSP_REG_A1+((cur_inst>>3) & 1);

	dsp_core->registers[dstreg] = ~dsp_core->registers[dstreg];
	dsp_core->registers[dstreg] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core->registers[DSP_REG_SR] |= ((dsp_core->registers[dstreg]>>23) & 1)<<DSP_SR_N;
	dsp_core->registers[DSP_REG_SR] |= (dsp_core->registers[dstreg]==0)<<DSP_SR_Z;
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
	dstreg = DSP_REG_A1+((cur_inst>>3) & 1);

	dsp_core->registers[dstreg] |= dsp_core->registers[srcreg];
	dsp_core->registers[dstreg] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core->registers[DSP_REG_SR] |= ((dsp_core->registers[dstreg]>>23) & 1)<<DSP_SR_N;
	dsp_core->registers[DSP_REG_SR] |= (dsp_core->registers[dstreg]==0)<<DSP_SR_Z;
}

static void dsp_rnd(void)
{
	Uint32 numreg, dest[3];

	numreg = (cur_inst>>3) & 1;

	dest[0] = dsp_core->registers[DSP_REG_A2+numreg];
	dest[1] = dsp_core->registers[DSP_REG_A1+numreg];
	dest[2] = dsp_core->registers[DSP_REG_A0+numreg];

	dsp_rnd56(dest);

	dsp_core->registers[DSP_REG_A2+numreg] = dest[0];
	dsp_core->registers[DSP_REG_A1+numreg] = dest[1];
	dsp_core->registers[DSP_REG_A0+numreg] = dest[2];

	dsp_ccr_update_e_u_n_z(&dest[0], &dest[1], &dest[2]);
}

static void dsp_rol(void)
{
	Uint32 dstreg, newcarry;

	dstreg = DSP_REG_A1+((cur_inst>>3) & 1);

	newcarry = (dsp_core->registers[dstreg]>>23) & 1;

	dsp_core->registers[dstreg] <<= 1;
	dsp_core->registers[dstreg] |= newcarry;
	dsp_core->registers[dstreg] &= BITMASK(24);

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_C)|(1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core->registers[DSP_REG_SR] |= newcarry;
	dsp_core->registers[DSP_REG_SR] |= ((dsp_core->registers[dstreg]>>23) & 1)<<DSP_SR_N;
	dsp_core->registers[DSP_REG_SR] |= (dsp_core->registers[dstreg]==0)<<DSP_SR_Z;
}

static void dsp_ror(void)
{
	Uint32 dstreg, newcarry;

	dstreg = DSP_REG_A1+((cur_inst>>3) & 1);

	newcarry = dsp_core->registers[dstreg] & 1;

	dsp_core->registers[dstreg] >>= 1;
	dsp_core->registers[dstreg] |= newcarry<<23;
	dsp_core->registers[dstreg] &= BITMASK(24);

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_C)|(1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core->registers[DSP_REG_SR] |= newcarry;
	dsp_core->registers[DSP_REG_SR] |= ((dsp_core->registers[dstreg]>>23) & 1)<<DSP_SR_N;
	dsp_core->registers[DSP_REG_SR] |= (dsp_core->registers[dstreg]==0)<<DSP_SR_Z;
}

static void dsp_sbc(void)
{
	Uint32 srcreg, destreg, source[3], dest[3], curcarry;
	Uint16 newsr;

	curcarry = (dsp_core->registers[DSP_REG_SR]>>(DSP_SR_C)) & 1;

	destreg = (cur_inst>>3) & 1;
	dest[0] = dsp_core->registers[DSP_REG_A2+destreg];
	dest[1] = dsp_core->registers[DSP_REG_A1+destreg];
	dest[2] = dsp_core->registers[DSP_REG_A0+destreg];

	srcreg = (cur_inst>>4) & 1;
	if (srcreg == 0) {	/* X */
		source[1] = dsp_core->registers[DSP_REG_X1];
		source[2] = dsp_core->registers[DSP_REG_X0];
		source[0] = 0;
		if (source[1] & (1<<23)) {
			source[0] = 0x0000ff;
		}
	}
	else {	/* Y */
		source[1] = dsp_core->registers[DSP_REG_Y1];
		source[2] = dsp_core->registers[DSP_REG_Y0];
		source[0] = 0;
		if (source[1] & (1<<23)) {
			source[0] = 0x0000ff;
		}
	}

	newsr = dsp_sub56(source, dest);
	
	if (curcarry) {
		source[0]=0;
		source[1]=0;
		source[2]=1;
		newsr |= dsp_sub56(source, dest);
	}

	dsp_core->registers[DSP_REG_A2+destreg] = dest[0];
	dsp_core->registers[DSP_REG_A1+destreg] = dest[1];
	dsp_core->registers[DSP_REG_A0+destreg] = dest[2];

	dsp_ccr_update_e_u_n_z(&dest[0], &dest[1], &dest[2]);

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core->registers[DSP_REG_SR] |= newsr;
}

static void dsp_sub(void)
{
	Uint32 srcreg, destreg, source[3], dest[3];
	Uint16 newsr;

	destreg = (cur_inst>>3) & 1;
	dest[0] = dsp_core->registers[DSP_REG_A2+destreg];
	dest[1] = dsp_core->registers[DSP_REG_A1+destreg];
	dest[2] = dsp_core->registers[DSP_REG_A0+destreg];

	srcreg = (cur_inst>>4) & BITMASK(3);
	switch(srcreg) {
		case 1:	/* A or B */
			srcreg = destreg ^ 1;
			source[0] = dsp_core->registers[DSP_REG_A2+srcreg];
			source[1] = dsp_core->registers[DSP_REG_A1+srcreg];
			source[2] = dsp_core->registers[DSP_REG_A0+srcreg];
			break;
		case 2:	/* X */
			source[1] = dsp_core->registers[DSP_REG_X1];
			source[2] = dsp_core->registers[DSP_REG_X0];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		case 3:	/* Y */
			source[1] = dsp_core->registers[DSP_REG_Y1];
			source[2] = dsp_core->registers[DSP_REG_Y0];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		case 4:	/* X0 */
			source[2] = 0;
			source[1] = dsp_core->registers[DSP_REG_X0];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		case 5:	/* Y0 */
			source[2] = 0;
			source[1] = dsp_core->registers[DSP_REG_Y0];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		case 6:	/* X1 */
			source[2] = 0;
			source[1] = dsp_core->registers[DSP_REG_X1];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		case 7:	/* Y1 */
			source[2] = 0;
			source[1] = dsp_core->registers[DSP_REG_Y1];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		default:
			fprintf(stderr, "Dsp: source register undefined! dsp_cpu.cpp: %d\n", __LINE__);
			return;
	}

	newsr = dsp_sub56(source, dest);

	dsp_core->registers[DSP_REG_A2+destreg] = dest[0];
	dsp_core->registers[DSP_REG_A1+destreg] = dest[1];
	dsp_core->registers[DSP_REG_A0+destreg] = dest[2];

	dsp_ccr_update_e_u_n_z(&dest[0], &dest[1], &dest[2]);

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core->registers[DSP_REG_SR] |= newsr;
}

static void dsp_subl(void)
{
	Uint32 numreg, source[3], dest[3];
	Uint16 newsr;

	numreg = (cur_inst>>3) & 1;

	dest[0] = dsp_core->registers[DSP_REG_A2+numreg];
	dest[1] = dsp_core->registers[DSP_REG_A1+numreg];
	dest[2] = dsp_core->registers[DSP_REG_A0+numreg];
	newsr = dsp_asl56(dest);

	source[0] = dsp_core->registers[DSP_REG_A2+(numreg ^ 1)];
	source[1] = dsp_core->registers[DSP_REG_A1+(numreg ^ 1)];
	source[2] = dsp_core->registers[DSP_REG_A0+(numreg ^ 1)];
	newsr |= dsp_sub56(source, dest);

	dsp_core->registers[DSP_REG_A2+numreg] = dest[0];
	dsp_core->registers[DSP_REG_A1+numreg] = dest[1];
	dsp_core->registers[DSP_REG_A0+numreg] = dest[2];

	dsp_ccr_update_e_u_n_z(&dest[0], &dest[1], &dest[2]);

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core->registers[DSP_REG_SR] |= newsr;
}

static void dsp_subr(void)
{
	Uint32 numreg, source[3], dest[3];
	Uint16 newsr;

	numreg = (cur_inst>>3) & 1;

	dest[0] = dsp_core->registers[DSP_REG_A2+numreg];
	dest[1] = dsp_core->registers[DSP_REG_A1+numreg];
	dest[2] = dsp_core->registers[DSP_REG_A0+numreg];
	newsr = dsp_asr56(dest);

	source[0] = dsp_core->registers[DSP_REG_A2+(numreg ^ 1)];
	source[1] = dsp_core->registers[DSP_REG_A1+(numreg ^ 1)];
	source[2] = dsp_core->registers[DSP_REG_A0+(numreg ^ 1)];
	newsr |= dsp_sub56(source, dest);

	dsp_core->registers[DSP_REG_A2+numreg] = dest[0];
	dsp_core->registers[DSP_REG_A1+numreg] = dest[1];
	dsp_core->registers[DSP_REG_A0+numreg] = dest[2];

	dsp_ccr_update_e_u_n_z(&dest[0], &dest[1], &dest[2]);

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core->registers[DSP_REG_SR] |= newsr;
}

static void dsp_tfr(void)
{
	Uint32 srcreg, destreg, source[3];

	destreg = (cur_inst>>3) & 1;

	srcreg = (cur_inst>>4) & BITMASK(3);
	switch(srcreg) {
		case 0:	/* A or B */
			srcreg = destreg ^ 1;
			source[0] = dsp_core->registers[DSP_REG_A2+srcreg];
			source[1] = dsp_core->registers[DSP_REG_A1+srcreg];
			source[2] = dsp_core->registers[DSP_REG_A0+srcreg];
			break;
		case 4:	/* X0 */
			source[2] = 0;
			source[1] = dsp_core->registers[DSP_REG_X0];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		case 5:	/* Y0 */
			source[2] = 0;
			source[1] = dsp_core->registers[DSP_REG_Y0];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		case 6:	/* X1 */
			source[2] = 0;
			source[1] = dsp_core->registers[DSP_REG_X1];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		case 7:	/* Y1 */
			source[2] = 0;
			source[1] = dsp_core->registers[DSP_REG_Y1];
			source[0] = 0;
			if (source[1] & (1<<23)) {
				source[0] = 0x0000ff;
			}
			break;
		default:
			return;
	}

	dsp_core->registers[DSP_REG_A2+destreg] = source[0];
	dsp_core->registers[DSP_REG_A1+destreg] = source[1];
	dsp_core->registers[DSP_REG_A0+destreg] = source[2];
}

static void dsp_tst(void)
{
	Uint32 destreg;
	
	destreg = (cur_inst>>3) & 1;

	dsp_ccr_update_e_u_n_z(	&dsp_core->registers[DSP_REG_A2+destreg],
				&dsp_core->registers[DSP_REG_A1+destreg],
				&dsp_core->registers[DSP_REG_A0+destreg]);

	dsp_core->registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

/*
vim:ts=4:sw=4:
*/
