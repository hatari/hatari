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
	along with this program; if not, write to the Free Software Foundation,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA
*/

/*
	DSP memory mapping
	------------------

	The memory map is configured as follows :
	Program space P is one contiguous block of 32K dsp Words
	X and Y data space are each separate 16K dsp Word blocks.
	Both X and Y can be accessed as blocks starting at 0 or 16K.
	Program space physically overlaps both X and Y data spaces.
	Y: memory is mapped at address $0 in P memory
	X: memory is mapped at address $4000 in P memory

	The DSP external RAM is zero waitstate, but there is a penalty for
	accessing it twice or more in a single instruction, because there is only
	one external data bus. The extra access costs 2 cycles penalty.

	The internal buses are all separate (0 waitstate)


	              X:             Y:             P:
	$ffff  |--------------+--------------+--------------|
	       |   Int. I/O   |   Ext. I/O   |              |
	$ffc0  |--------------+--------------+              |
	       |              |              |              |
	       |   Reserved   |   Reserved   |   Reserved   |
	       |              |              |              |
	       |              |              |              |
	       |              |              |              |
	$8000  |--------------+--------------+--------------|
	       |              |              |              |
	       |  16k Shadow  |  16k Shadow  |              |
	       |              |              |      32K     |
	$4000  |--------------+--------------|    Program   |
	       |     16K      |     16K      |      RAM     |
	       |   External   |   External   |              |
	       |     RAM      |     RAM      |              |
	$0200  |--------------+--------------+--------------|
	       | Log table or | Sin table or |              |
	       | external mem | external mem |   Internal   |
	$0100  |--------------+--------------+    program   |
	       |  Internal X  |  Internal Y  |    memory    |
	       |    memory    |    memory    |              |
	$0000  |--------------+--------------+--------------|


	Special Note : As the Falcon DSP is a 0 waitstate access memory, I've simplified a little the cycle counting.
	If this DSP emulator code is used in another project, one should take into account the bus control register (BCR) waitstates.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>

#include "main.h"
#include "dsp_core.h"
#include "dsp_cpu.h"
#include "dsp_disasm.h"
#include "log.h"
#include "debugui.h"

#define DSP_COUNT_IPS 0		/* Count instruction per seconds */

#if DSP_COUNT_IPS
/* For counting instructions per second */
#include <SDL_timer.h>
static uint32_t start_time;
static uint32_t num_inst;
#endif

/**********************************
 *	Defines
 **********************************/

#define SIGN_PLUS  0
#define SIGN_MINUS 1

/**********************************
 *	Variables
 **********************************/

/* Length of current instruction */
static uint32_t cur_inst_len;	/* =0:jump, >0:increment */

/* Current instruction */
static uint32_t cur_inst;

/* Counts the number of access to the external memory for one instruction */
static uint16_t access_to_ext_memory;

/* DSP is in disasm mode ? */
/* If yes, stack overflow, underflow and illegal instructions messages are not displayed */
static bool isDsp_in_disasm_mode;

static char   str_disasm_memory[2][50]; 	/* Buffer for memory change text in disasm mode */
static uint16_t disasm_memory_ptr;		/* Pointer for memory change in disasm mode */

/**********************************
 *	Functions
 **********************************/

typedef void (*dsp_emul_t)(void);

static void dsp_postexecute_update_pc(void);
static void dsp_postexecute_interrupts(void);

static void dsp_setInterruptIPL(uint32_t value);

static void dsp_ccr_update_e_u_n_z(uint32_t reg0, uint32_t reg1, uint32_t reg2);

static uint32_t read_memory(int space, uint16_t address);
static inline uint32_t read_memory_p(uint16_t address);
static uint32_t read_memory_disasm(int space, uint16_t address);

static inline void write_memory(int space, uint16_t address, uint32_t value);
static void write_memory_raw(int space, uint16_t address, uint32_t value);
static void write_memory_disasm(int space, uint16_t address, uint32_t value);

static void dsp_write_reg(uint32_t numreg, uint32_t value);

static void dsp_stack_push(uint32_t curpc, uint32_t cursr, uint16_t sshOnly);
static void dsp_stack_pop(uint32_t *curpc, uint32_t *cursr);
static void dsp_compute_ssh_ssl(void);

static void opcode8h_0(void);

static void dsp_update_rn(uint32_t numreg, int16_t modifier);
static void dsp_update_rn_bitreverse(uint32_t numreg);
static void dsp_update_rn_modulo(uint32_t numreg, int16_t modifier);
static int dsp_calc_ea(uint32_t ea_mode, uint32_t *dst_addr);
static int dsp_calc_cc(uint32_t cc_code);

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
static int dsp_pm_read_accu24(int numreg, uint32_t *dest);
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
static uint16_t dsp_abs56(uint32_t *dest);
static uint16_t dsp_asl56(uint32_t *dest);
static uint16_t dsp_asr56(uint32_t *dest);
static uint16_t dsp_add56(uint32_t *source, uint32_t *dest);
static uint16_t dsp_sub56(uint32_t *source, uint32_t *dest);
static void dsp_mul56(uint32_t source1, uint32_t source2, uint32_t *dest, uint8_t signe);
static void dsp_rnd56(uint32_t *dest);

/* Instructions with parallel moves */
static void dsp_abs_a(void);
static void dsp_abs_b(void);
static void dsp_adc_x_a(void);
static void dsp_adc_x_b(void);
static void dsp_adc_y_a(void);
static void dsp_adc_y_b(void);
static void dsp_add_b_a(void);
static void dsp_add_a_b(void);
static void dsp_add_x_a(void);
static void dsp_add_x_b(void);
static void dsp_add_y_a(void);
static void dsp_add_y_b(void);
static void dsp_add_x0_a(void);
static void dsp_add_x0_b(void);
static void dsp_add_y0_a(void);
static void dsp_add_y0_b(void);
static void dsp_add_x1_a(void);
static void dsp_add_x1_b(void);
static void dsp_add_y1_a(void);
static void dsp_add_y1_b(void);
static void dsp_addl_b_a(void);
static void dsp_addl_a_b(void);
static void dsp_addr_b_a(void);
static void dsp_addr_a_b(void);
static void dsp_and_x0_a(void);
static void dsp_and_x0_b(void);
static void dsp_and_y0_a(void);
static void dsp_and_y0_b(void);
static void dsp_and_x1_a(void);
static void dsp_and_x1_b(void);
static void dsp_and_y1_a(void);
static void dsp_and_y1_b(void);
static void dsp_asl_a(void);
static void dsp_asl_b(void);
static void dsp_asr_a(void);
static void dsp_asr_b(void);
static void dsp_clr_a(void);
static void dsp_clr_b(void);
static void dsp_cmp_b_a(void);
static void dsp_cmp_a_b(void);
static void dsp_cmp_x0_a(void);
static void dsp_cmp_x0_b(void);
static void dsp_cmp_y0_a(void);
static void dsp_cmp_y0_b(void);
static void dsp_cmp_x1_a(void);
static void dsp_cmp_x1_b(void);
static void dsp_cmp_y1_a(void);
static void dsp_cmp_y1_b(void);
static void dsp_cmpm_b_a(void);
static void dsp_cmpm_a_b(void);
static void dsp_cmpm_x0_a(void);
static void dsp_cmpm_x0_b(void);
static void dsp_cmpm_y0_a(void);
static void dsp_cmpm_y0_b(void);
static void dsp_cmpm_x1_a(void);
static void dsp_cmpm_x1_b(void);
static void dsp_cmpm_y1_a(void);
static void dsp_cmpm_y1_b(void);
static void dsp_eor_x0_a(void);
static void dsp_eor_x0_b(void);
static void dsp_eor_y0_a(void);
static void dsp_eor_y0_b(void);
static void dsp_eor_x1_a(void);
static void dsp_eor_x1_b(void);
static void dsp_eor_y1_a(void);
static void dsp_eor_y1_b(void);
static void dsp_lsl_a(void);
static void dsp_lsl_b(void);
static void dsp_lsr_a(void);
static void dsp_lsr_b(void);
static void dsp_mac_p_x0_x0_a(void);
static void dsp_mac_m_x0_x0_a(void);
static void dsp_mac_p_x0_x0_b(void);
static void dsp_mac_m_x0_x0_b(void);
static void dsp_mac_p_y0_y0_a(void);
static void dsp_mac_m_y0_y0_a(void);
static void dsp_mac_p_y0_y0_b(void);
static void dsp_mac_m_y0_y0_b(void);
static void dsp_mac_p_x1_x0_a(void);
static void dsp_mac_m_x1_x0_a(void);
static void dsp_mac_p_x1_x0_b(void);
static void dsp_mac_m_x1_x0_b(void);
static void dsp_mac_p_y1_y0_a(void);
static void dsp_mac_m_y1_y0_a(void);
static void dsp_mac_p_y1_y0_b(void);
static void dsp_mac_m_y1_y0_b(void);
static void dsp_mac_p_x0_y1_a(void);
static void dsp_mac_m_x0_y1_a(void);
static void dsp_mac_p_x0_y1_b(void);
static void dsp_mac_m_x0_y1_b(void);
static void dsp_mac_p_y0_x0_a(void);
static void dsp_mac_m_y0_x0_a(void);
static void dsp_mac_p_y0_x0_b(void);
static void dsp_mac_m_y0_x0_b(void);
static void dsp_mac_p_x1_y0_a(void);
static void dsp_mac_m_x1_y0_a(void);
static void dsp_mac_p_x1_y0_b(void);
static void dsp_mac_m_x1_y0_b(void);
static void dsp_mac_p_y1_x1_a(void);
static void dsp_mac_m_y1_x1_a(void);
static void dsp_mac_p_y1_x1_b(void);
static void dsp_mac_m_y1_x1_b(void);
static void dsp_macr_p_x0_x0_a(void);
static void dsp_macr_m_x0_x0_a(void);
static void dsp_macr_p_x0_x0_b(void);
static void dsp_macr_m_x0_x0_b(void);
static void dsp_macr_p_y0_y0_a(void);
static void dsp_macr_m_y0_y0_a(void);
static void dsp_macr_p_y0_y0_b(void);
static void dsp_macr_m_y0_y0_b(void);
static void dsp_macr_p_x1_x0_a(void);
static void dsp_macr_m_x1_x0_a(void);
static void dsp_macr_p_x1_x0_b(void);
static void dsp_macr_m_x1_x0_b(void);
static void dsp_macr_p_y1_y0_a(void);
static void dsp_macr_m_y1_y0_a(void);
static void dsp_macr_p_y1_y0_b(void);
static void dsp_macr_m_y1_y0_b(void);
static void dsp_macr_p_x0_y1_a(void);
static void dsp_macr_m_x0_y1_a(void);
static void dsp_macr_p_x0_y1_b(void);
static void dsp_macr_m_x0_y1_b(void);
static void dsp_macr_p_y0_x0_a(void);
static void dsp_macr_m_y0_x0_a(void);
static void dsp_macr_p_y0_x0_b(void);
static void dsp_macr_m_y0_x0_b(void);
static void dsp_macr_p_x1_y0_a(void);
static void dsp_macr_m_x1_y0_a(void);
static void dsp_macr_p_x1_y0_b(void);
static void dsp_macr_m_x1_y0_b(void);
static void dsp_macr_p_y1_x1_a(void);
static void dsp_macr_m_y1_x1_a(void);
static void dsp_macr_p_y1_x1_b(void);
static void dsp_macr_m_y1_x1_b(void);
static void dsp_move(void);
static void dsp_mpy_p_x0_x0_a(void);
static void dsp_mpy_m_x0_x0_a(void);
static void dsp_mpy_p_x0_x0_b(void);
static void dsp_mpy_m_x0_x0_b(void);
static void dsp_mpy_p_y0_y0_a(void);
static void dsp_mpy_m_y0_y0_a(void);
static void dsp_mpy_p_y0_y0_b(void);
static void dsp_mpy_m_y0_y0_b(void);
static void dsp_mpy_p_x1_x0_a(void);
static void dsp_mpy_m_x1_x0_a(void);
static void dsp_mpy_p_x1_x0_b(void);
static void dsp_mpy_m_x1_x0_b(void);
static void dsp_mpy_p_y1_y0_a(void);
static void dsp_mpy_m_y1_y0_a(void);
static void dsp_mpy_p_y1_y0_b(void);
static void dsp_mpy_m_y1_y0_b(void);
static void dsp_mpy_p_x0_y1_a(void);
static void dsp_mpy_m_x0_y1_a(void);
static void dsp_mpy_p_x0_y1_b(void);
static void dsp_mpy_m_x0_y1_b(void);
static void dsp_mpy_p_y0_x0_a(void);
static void dsp_mpy_m_y0_x0_a(void);
static void dsp_mpy_p_y0_x0_b(void);
static void dsp_mpy_m_y0_x0_b(void);
static void dsp_mpy_p_x1_y0_a(void);
static void dsp_mpy_m_x1_y0_a(void);
static void dsp_mpy_p_x1_y0_b(void);
static void dsp_mpy_m_x1_y0_b(void);
static void dsp_mpy_p_y1_x1_a(void);
static void dsp_mpy_m_y1_x1_a(void);
static void dsp_mpy_p_y1_x1_b(void);
static void dsp_mpy_m_y1_x1_b(void);
static void dsp_mpyr_p_x0_x0_a(void);
static void dsp_mpyr_m_x0_x0_a(void);
static void dsp_mpyr_p_x0_x0_b(void);
static void dsp_mpyr_m_x0_x0_b(void);
static void dsp_mpyr_p_y0_y0_a(void);
static void dsp_mpyr_m_y0_y0_a(void);
static void dsp_mpyr_p_y0_y0_b(void);
static void dsp_mpyr_m_y0_y0_b(void);
static void dsp_mpyr_p_x1_x0_a(void);
static void dsp_mpyr_m_x1_x0_a(void);
static void dsp_mpyr_p_x1_x0_b(void);
static void dsp_mpyr_m_x1_x0_b(void);
static void dsp_mpyr_p_y1_y0_a(void);
static void dsp_mpyr_m_y1_y0_a(void);
static void dsp_mpyr_p_y1_y0_b(void);
static void dsp_mpyr_m_y1_y0_b(void);
static void dsp_mpyr_p_x0_y1_a(void);
static void dsp_mpyr_m_x0_y1_a(void);
static void dsp_mpyr_p_x0_y1_b(void);
static void dsp_mpyr_m_x0_y1_b(void);
static void dsp_mpyr_p_y0_x0_a(void);
static void dsp_mpyr_m_y0_x0_a(void);
static void dsp_mpyr_p_y0_x0_b(void);
static void dsp_mpyr_m_y0_x0_b(void);
static void dsp_mpyr_p_x1_y0_a(void);
static void dsp_mpyr_m_x1_y0_a(void);
static void dsp_mpyr_p_x1_y0_b(void);
static void dsp_mpyr_m_x1_y0_b(void);
static void dsp_mpyr_p_y1_x1_a(void);
static void dsp_mpyr_m_y1_x1_a(void);
static void dsp_mpyr_p_y1_x1_b(void);
static void dsp_mpyr_m_y1_x1_b(void);
static void dsp_neg_a(void);
static void dsp_neg_b(void);
static void dsp_not_a(void);
static void dsp_not_b(void);
static void dsp_or_x0_a(void);
static void dsp_or_x0_b(void);
static void dsp_or_y0_a(void);
static void dsp_or_y0_b(void);
static void dsp_or_x1_a(void);
static void dsp_or_x1_b(void);
static void dsp_or_y1_a(void);
static void dsp_or_y1_b(void);
static void dsp_rnd_a(void);
static void dsp_rnd_b(void);
static void dsp_rol_a(void);
static void dsp_rol_b(void);
static void dsp_ror_a(void);
static void dsp_ror_b(void);
static void dsp_sbc_x_a(void);
static void dsp_sbc_x_b(void);
static void dsp_sbc_y_a(void);
static void dsp_sbc_y_b(void);
static void dsp_sub_b_a(void);
static void dsp_sub_a_b(void);
static void dsp_sub_x_a(void);
static void dsp_sub_x_b(void);
static void dsp_sub_y_a(void);
static void dsp_sub_y_b(void);
static void dsp_sub_x0_a(void);
static void dsp_sub_x0_b(void);
static void dsp_sub_y0_a(void);
static void dsp_sub_y0_b(void);
static void dsp_sub_x1_a(void);
static void dsp_sub_x1_b(void);
static void dsp_sub_y1_a(void);
static void dsp_sub_y1_b(void);
static void dsp_subl_a(void);
static void dsp_subl_b(void);
static void dsp_subr_a(void);
static void dsp_subr_b(void);
static void dsp_tfr_b_a(void);
static void dsp_tfr_a_b(void);
static void dsp_tfr_x0_a(void);
static void dsp_tfr_x0_b(void);
static void dsp_tfr_y0_a(void);
static void dsp_tfr_y0_b(void);
static void dsp_tfr_x1_a(void);
static void dsp_tfr_x1_b(void);
static void dsp_tfr_y1_a(void);
static void dsp_tfr_y1_b(void);
static void dsp_tst_a(void);
static void dsp_tst_b(void);

static const dsp_emul_t opcodes8h[512] = {
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
	dsp_pm_0, dsp_pm_0, dsp_pm_0, dsp_pm_0, dsp_pm_0, dsp_pm_0, dsp_pm_0, dsp_pm_0,
	dsp_movep_0, dsp_movep_0, dsp_movep_1, dsp_movep_1, dsp_movep_23, dsp_movep_23, dsp_movep_23, dsp_movep_23,
	dsp_pm_0, dsp_pm_0, dsp_pm_0, dsp_pm_0, dsp_pm_0, dsp_pm_0, dsp_pm_0, dsp_pm_0,
	dsp_movep_0, dsp_movep_0, dsp_movep_1, dsp_movep_1, dsp_movep_23, dsp_movep_23, dsp_movep_23, dsp_movep_23,
	dsp_pm_0, dsp_pm_0, dsp_pm_0, dsp_pm_0, dsp_pm_0, dsp_pm_0, dsp_pm_0, dsp_pm_0,
	dsp_movep_0, dsp_movep_0, dsp_movep_1, dsp_movep_1, dsp_movep_23, dsp_movep_23, dsp_movep_23, dsp_movep_23,
	dsp_pm_0, dsp_pm_0, dsp_pm_0, dsp_pm_0, dsp_pm_0, dsp_pm_0, dsp_pm_0, dsp_pm_0,
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

static const dsp_emul_t opcodes_parmove[16] = {
	dsp_pm_0, dsp_pm_1, dsp_pm_2, dsp_pm_3, dsp_pm_4, dsp_pm_5, dsp_pm_5, dsp_pm_5,
	dsp_pm_8, dsp_pm_8, dsp_pm_8, dsp_pm_8, dsp_pm_8, dsp_pm_8, dsp_pm_8, dsp_pm_8
};

static const dsp_emul_t opcodes_alu[256] = {
	/* 0x00 - 0x3f */
	dsp_move     , dsp_tfr_b_a, dsp_addr_b_a, dsp_tst_a, dsp_undefined, dsp_cmp_b_a, dsp_subr_a, dsp_cmpm_b_a,
	dsp_undefined, dsp_tfr_a_b, dsp_addr_a_b, dsp_tst_b, dsp_undefined, dsp_cmp_a_b, dsp_subr_b, dsp_cmpm_a_b,
	dsp_add_b_a, dsp_rnd_a, dsp_addl_b_a, dsp_clr_a, dsp_sub_b_a, dsp_undefined, dsp_subl_a, dsp_not_a,
	dsp_add_a_b, dsp_rnd_b, dsp_addl_a_b, dsp_clr_b, dsp_sub_a_b, dsp_undefined, dsp_subl_b, dsp_not_b,
	dsp_add_x_a, dsp_adc_x_a, dsp_asr_a, dsp_lsr_a, dsp_sub_x_a, dsp_sbc_x_a, dsp_abs_a, dsp_ror_a,
	dsp_add_x_b, dsp_adc_x_b, dsp_asr_b, dsp_lsr_b, dsp_sub_x_b, dsp_sbc_x_b, dsp_abs_b, dsp_ror_b,
	dsp_add_y_a, dsp_adc_y_a, dsp_asl_a, dsp_lsl_a, dsp_sub_y_a, dsp_sbc_y_a, dsp_neg_a, dsp_rol_a,
	dsp_add_y_b, dsp_adc_y_b, dsp_asl_b, dsp_lsl_b, dsp_sub_y_b, dsp_sbc_y_b, dsp_neg_b, dsp_rol_b,

	/* 0x40 - 0x7f */
	dsp_add_x0_a, dsp_tfr_x0_a, dsp_or_x0_a, dsp_eor_x0_a, dsp_sub_x0_a, dsp_cmp_x0_a, dsp_and_x0_a, dsp_cmpm_x0_a,
	dsp_add_x0_b, dsp_tfr_x0_b, dsp_or_x0_b, dsp_eor_x0_b, dsp_sub_x0_b, dsp_cmp_x0_b, dsp_and_x0_b, dsp_cmpm_x0_b,
	dsp_add_y0_a, dsp_tfr_y0_a, dsp_or_y0_a, dsp_eor_y0_a, dsp_sub_y0_a, dsp_cmp_y0_a, dsp_and_y0_a, dsp_cmpm_y0_a,
	dsp_add_y0_b, dsp_tfr_y0_b, dsp_or_y0_b, dsp_eor_y0_b, dsp_sub_y0_b, dsp_cmp_y0_b, dsp_and_y0_b, dsp_cmpm_y0_b,
	dsp_add_x1_a, dsp_tfr_x1_a, dsp_or_x1_a, dsp_eor_x1_a, dsp_sub_x1_a, dsp_cmp_x1_a, dsp_and_x1_a, dsp_cmpm_x1_a,
	dsp_add_x1_b, dsp_tfr_x1_b, dsp_or_x1_b, dsp_eor_x1_b, dsp_sub_x1_b, dsp_cmp_x1_b, dsp_and_x1_b, dsp_cmpm_x1_b,
	dsp_add_y1_a, dsp_tfr_y1_a, dsp_or_y1_a, dsp_eor_y1_a, dsp_sub_y1_a, dsp_cmp_y1_a, dsp_and_y1_a, dsp_cmpm_y1_a,
	dsp_add_y1_b, dsp_tfr_y1_b, dsp_or_y1_b, dsp_eor_y1_b, dsp_sub_y1_b, dsp_cmp_y1_b, dsp_and_y1_b, dsp_cmpm_y1_b,

	/* 0x80 - 0xbf */
	dsp_mpy_p_x0_x0_a, dsp_mpyr_p_x0_x0_a, dsp_mac_p_x0_x0_a, dsp_macr_p_x0_x0_a, dsp_mpy_m_x0_x0_a, dsp_mpyr_m_x0_x0_a, dsp_mac_m_x0_x0_a, dsp_macr_m_x0_x0_a,
	dsp_mpy_p_x0_x0_b, dsp_mpyr_p_x0_x0_b, dsp_mac_p_x0_x0_b, dsp_macr_p_x0_x0_b, dsp_mpy_m_x0_x0_b, dsp_mpyr_m_x0_x0_b, dsp_mac_m_x0_x0_b, dsp_macr_m_x0_x0_b,
	dsp_mpy_p_y0_y0_a, dsp_mpyr_p_y0_y0_a, dsp_mac_p_y0_y0_a, dsp_macr_p_y0_y0_a, dsp_mpy_m_y0_y0_a, dsp_mpyr_m_y0_y0_a, dsp_mac_m_y0_y0_a, dsp_macr_m_y0_y0_a,
	dsp_mpy_p_y0_y0_b, dsp_mpyr_p_y0_y0_b, dsp_mac_p_y0_y0_b, dsp_macr_p_y0_y0_b, dsp_mpy_m_y0_y0_b, dsp_mpyr_m_y0_y0_b, dsp_mac_m_y0_y0_b, dsp_macr_m_y0_y0_b,
	dsp_mpy_p_x1_x0_a, dsp_mpyr_p_x1_x0_a, dsp_mac_p_x1_x0_a, dsp_macr_p_x1_x0_a, dsp_mpy_m_x1_x0_a, dsp_mpyr_m_x1_x0_a, dsp_mac_m_x1_x0_a, dsp_macr_m_x1_x0_a,
	dsp_mpy_p_x1_x0_b, dsp_mpyr_p_x1_x0_b, dsp_mac_p_x1_x0_b, dsp_macr_p_x1_x0_b, dsp_mpy_m_x1_x0_b, dsp_mpyr_m_x1_x0_b, dsp_mac_m_x1_x0_b, dsp_macr_m_x1_x0_b,
	dsp_mpy_p_y1_y0_a, dsp_mpyr_p_y1_y0_a, dsp_mac_p_y1_y0_a, dsp_macr_p_y1_y0_a, dsp_mpy_m_y1_y0_a, dsp_mpyr_m_y1_y0_a, dsp_mac_m_y1_y0_a, dsp_macr_m_y1_y0_a,
	dsp_mpy_p_y1_y0_b, dsp_mpyr_p_y1_y0_b, dsp_mac_p_y1_y0_b, dsp_macr_p_y1_y0_b, dsp_mpy_m_y1_y0_b, dsp_mpyr_m_y1_y0_b, dsp_mac_m_y1_y0_b, dsp_macr_m_y1_y0_b,

	/* 0xc0_m_ 0xff */
	dsp_mpy_p_x0_y1_a, dsp_mpyr_p_x0_y1_a, dsp_mac_p_x0_y1_a, dsp_macr_p_x0_y1_a, dsp_mpy_m_x0_y1_a, dsp_mpyr_m_x0_y1_a, dsp_mac_m_x0_y1_a, dsp_macr_m_x0_y1_a,
	dsp_mpy_p_x0_y1_b, dsp_mpyr_p_x0_y1_b, dsp_mac_p_x0_y1_b, dsp_macr_p_x0_y1_b, dsp_mpy_m_x0_y1_b, dsp_mpyr_m_x0_y1_b, dsp_mac_m_x0_y1_b, dsp_macr_m_x0_y1_b,
	dsp_mpy_p_y0_x0_a, dsp_mpyr_p_y0_x0_a, dsp_mac_p_y0_x0_a, dsp_macr_p_y0_x0_a, dsp_mpy_m_y0_x0_a, dsp_mpyr_m_y0_x0_a, dsp_mac_m_y0_x0_a, dsp_macr_m_y0_x0_a,
	dsp_mpy_p_y0_x0_b, dsp_mpyr_p_y0_x0_b, dsp_mac_p_y0_x0_b, dsp_macr_p_y0_x0_b, dsp_mpy_m_y0_x0_b, dsp_mpyr_m_y0_x0_b, dsp_mac_m_y0_x0_b, dsp_macr_m_y0_x0_b,
	dsp_mpy_p_x1_y0_a, dsp_mpyr_p_x1_y0_a, dsp_mac_p_x1_y0_a, dsp_macr_p_x1_y0_a, dsp_mpy_m_x1_y0_a, dsp_mpyr_m_x1_y0_a, dsp_mac_m_x1_y0_a, dsp_macr_m_x1_y0_a,
	dsp_mpy_p_x1_y0_b, dsp_mpyr_p_x1_y0_b, dsp_mac_p_x1_y0_b, dsp_macr_p_x1_y0_b, dsp_mpy_m_x1_y0_b, dsp_mpyr_m_x1_y0_b, dsp_mac_m_x1_y0_b, dsp_macr_m_x1_y0_b,
	dsp_mpy_p_y1_x1_a, dsp_mpyr_p_y1_x1_a, dsp_mac_p_y1_x1_a, dsp_macr_p_y1_x1_a, dsp_mpy_m_y1_x1_a, dsp_mpyr_m_y1_x1_a, dsp_mac_m_y1_x1_a, dsp_macr_m_y1_x1_a,
	dsp_mpy_p_y1_x1_b, dsp_mpyr_p_y1_x1_b, dsp_mac_p_y1_x1_b, dsp_macr_p_y1_x1_b, dsp_mpy_m_y1_x1_b, dsp_mpyr_m_y1_x1_b, dsp_mac_m_y1_x1_b, dsp_macr_m_y1_x1_b
};

static const int registers_tcc[16][2] = {
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

static const int registers_mask[64] = {
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

const char dsp_inter_priority_list[32] = {
	DSP_INTER_ILLEGAL, DSP_INTER_TRACE, DSP_INTER_SWI, DSP_INTER_IRQA,					/* 0 */
	DSP_INTER_IRQB, DSP_INTER_HOST_COMMAND, DSP_INTER_SSI_TRX_DATA_E, DSP_INTER_SSI_RCV_DATA,		/* 4 */
	DSP_INTER_SCI_RCV_DATA_E, DSP_INTER_SSI_TRX_DATA, DSP_INTER_SCI_TRX_DATA, DSP_INTER_SCI_RCV_DATA,	/* 8 */
	DSP_INTER_SCI_IDLE_LINE, DSP_INTER_SCI_TIMER, DSP_PRIORITY_LIST_EXIT, DSP_INTER_STACK_ERROR,		/* 12 */
	DSP_INTER_HOST_TRX_DATA, DSP_INTER_SSI_RCV_DATA_E, DSP_INTER_HOST_RCV_DATA, 0,				/* 16 */
	0, 0, 0, 0,												/* 20 */
	0, 0, 0, 0,												/* 24 */
	0, 0, 0, DSP_INTER_NMI,											/* 28 */
};

const char *dsp_interrupt_name[32] = {
	"Reset", "Stack Error", "Trace", "SWI",
	"IRQA", "IRQB", "SSI Receive Data", "SSI Receive Data with Exception",
	"SSI Transmit Data", "SSI Transmit Data with Exception", "SCI Receive Data", "SCI Receive Data with Exception",
	"SCI Transmit Data", "SCI Idle Line", "SCI Timer", "NMI",
	"Host Receive Data", "Host Transmit Data", "Host Command", "Unknown",
	"Unknown", "Unknown", "Unknown", "Unknown",
	"Unknown", "Unknown", "Unknown", "Unknown",
	"Unknown", "Unknown", "Unknown", "Illegal"
};


/**********************************
 *	Emulator kernel
 **********************************/

void dsp56k_init_cpu(void)
{
	dsp56k_disasm_init();
	isDsp_in_disasm_mode = false;
#if DSP_COUNT_IPS
	start_time = SDL_GetTicks();
	num_inst = 0;
#endif
}

/**
 * Execute one instruction in trace mode at a given PC address.
 * */
uint16_t dsp56k_execute_one_disasm_instruction(FILE *out, uint16_t pc)
{
	dsp_core_t *ptr1, *ptr2;
	static dsp_core_t dsp_core_save;
	uint16_t instruction_length;

	ptr1 = &dsp_core;
	ptr2 = &dsp_core_save;

	/* Set DSP in disasm mode */
	isDsp_in_disasm_mode = true;

	/* Save DSP context before executing instruction */
	memcpy(ptr2, ptr1, sizeof(dsp_core));

	/* execute and disasm instruction */
	dsp_core.pc = pc;

	/* Disasm instruction */
	instruction_length = dsp56k_disasm(DSP_DISASM_MODE, out) - 1;

	/* Execute instruction at address given in parameter to get the number of cycles it takes */
	dsp56k_execute_instruction();

	fprintf(out, "%s", dsp56k_getInstructionText());

	/* Restore DSP context after executing instruction */
	memcpy(ptr1, ptr2, sizeof(dsp_core));

	/* Unset DSP in disasm mode */
	isDsp_in_disasm_mode = false;

	return instruction_length;
}

void dsp56k_execute_instruction(void)
{
	uint32_t value;
	uint32_t disasm_return = 0;
	disasm_memory_ptr = 0;

	/* Initialise the number of access to the external memory for this instruction */
	access_to_ext_memory = 0;

	/* Init the indirect AGU move instruction flag */
	dsp_core.agu_move_indirect_instr = 0;

	/* Trace Interrupt at end of instruction? */
	if (dsp_core.registers[DSP_REG_SR] & (1<<DSP_SR_T)) {
		dsp_set_interrupt(DSP_INTER_TRACE, 1);
	}

	/* Decode and execute current instruction */
	cur_inst = read_memory_p(dsp_core.pc);

	/* Initialize instruction size and cycle counter */
	cur_inst_len = 1;
	dsp_core.instr_cycle = 2;

	/* Disasm current instruction ? (trace mode only) */
	if (LOG_TRACE_LEVEL(TRACE_DSP_DISASM)) {
		/* Call dsp56k_disasm only when DSP is called in trace mode */
		if (isDsp_in_disasm_mode == false) {
			LOG_TRACE_DIRECT_INIT();
			disasm_return = dsp56k_disasm(DSP_TRACE_MODE, TraceFile);

			if (disasm_return != 0 && LOG_TRACE_LEVEL(TRACE_DSP_DISASM_REG)) {
				/* DSP regs trace enabled only if DSP DISASM is enabled */
				dsp56k_disasm_reg_save();
			}
		}
	}

	if (cur_inst < 0x100000) {
		value = (cur_inst >> 11) & (BITMASK(6) << 3);
		value += (cur_inst >> 5) & BITMASK(3);
		opcodes8h[value]();
	} else {
		/* Do parallel move read */
		opcodes_parmove[(cur_inst>>20) & BITMASK(4)]();
	}

	/* Add the waitstate due to external memory access */
	/* (2 extra cycles per extra access to the external memory after the first one */
	if (access_to_ext_memory != 0) {
		value  = (access_to_ext_memory >> DSP_SPACE_X) & 1;
		value += (access_to_ext_memory >> DSP_SPACE_Y) & 1;
		value += (access_to_ext_memory >> DSP_SPACE_P) & 1;

		if (value > 1)
			dsp_core.instr_cycle += (value - 1) * 2;
	}

	/* Disasm current instruction ? (trace mode only) */
	if (LOG_TRACE_LEVEL(TRACE_DSP_DISASM)) {
		/* Display only when DSP is called in trace mode */
		if (isDsp_in_disasm_mode == false) {
			if (disasm_return != 0) {
				LOG_TRACE_DIRECT_INIT();
				fprintf(TraceFile, "%s", dsp56k_getInstructionText());

				/* DSP regs trace enabled only if DSP DISASM is enabled */
				if (LOG_TRACE_LEVEL(TRACE_DSP_DISASM_REG))
					dsp56k_disasm_reg_compare(TraceFile);

				if (LOG_TRACE_LEVEL(TRACE_DSP_DISASM_MEM)) {
					/* 1 memory change to display ? */
					if (disasm_memory_ptr == 1)
						fprintf(TraceFile, "\t%s\n", str_disasm_memory[0]);
					/* 2 memory changes to display ? */
					else if (disasm_memory_ptr == 2) {
						fprintf(TraceFile, "\t%s\n", str_disasm_memory[0]);
						fprintf(TraceFile, "\t%s\n", str_disasm_memory[1]);
					}
				}
			}
		}
	}

	/* Process the PC */
	dsp_postexecute_update_pc();

	/* Process Interrupts */
	dsp_postexecute_interrupts();

#if DSP_COUNT_IPS
	++num_inst;
	if ((num_inst & 63) == 0) {
		/* Evaluate time after <N> instructions have been executed to avoid asking too frequently */
		uint32_t cur_time = SDL_GetTicks();
		if (cur_time-start_time>1000) {
			fprintf(stderr, "Dsp: %d i/s\n", (num_inst*1000)/(cur_time-start_time));
			start_time=cur_time;
			num_inst=0;
		}
	}
#endif
}

/**********************************
 *	Update the PC
**********************************/

static void dsp_postexecute_update_pc(void)
{
	/* When running a REP, PC must stay on the current instruction */
	if (dsp_core.loop_rep) {
		/* Is PC on the instruction to repeat ? */
		if (dsp_core.pc_on_rep==0) {
			--dsp_core.registers[DSP_REG_LC];
			dsp_core.registers[DSP_REG_LC] &= BITMASK(16);

			if (dsp_core.registers[DSP_REG_LC] > 0) {
				cur_inst_len = 0;	/* Stay on this instruction */
			} else {
				dsp_core.loop_rep = 0;
				dsp_core.registers[DSP_REG_LC] = dsp_core.registers[DSP_REG_LCSAVE];
			}
		} else {
			/* Init LC at right value */
			if (dsp_core.registers[DSP_REG_LC] == 0) {
				dsp_core.registers[DSP_REG_LC] = 0x010000;
			}
			dsp_core.pc_on_rep = 0;
		}
	}

	/* Normal execution, go to next instruction */
	dsp_core.pc += cur_inst_len;

	/* When running a DO loop, we test the end of loop with the */
	/* updated PC, pointing to last instruction of the loop */
	if (dsp_core.registers[DSP_REG_SR] & (1<<DSP_SR_LF)) {

		/* Did we execute the last instruction in loop ? */
		if (dsp_core.pc == dsp_core.registers[DSP_REG_LA] + 1) {
			if (dsp_core.registers[DSP_REG_LC] == 1) {
				/* end of the loop */
				uint32_t saved_pc, saved_sr;

				dsp_stack_pop(&saved_pc, &saved_sr);
				dsp_core.registers[DSP_REG_SR] &= 0x7fff;
				dsp_core.registers[DSP_REG_SR] |= saved_sr & (1<<DSP_SR_LF);
				dsp_stack_pop(&dsp_core.registers[DSP_REG_LA], &dsp_core.registers[DSP_REG_LC]);
			} else {
				/* Loop one more time */
				--dsp_core.registers[DSP_REG_LC];
				dsp_core.registers[DSP_REG_LC] &= BITMASK(16);
				dsp_core.pc = dsp_core.registers[DSP_REG_SSH];
			}
		}
	}
}


/**********************************
 *	Interrupts
**********************************/

/* Set the status of an interrupt */
void dsp_set_interrupt(uint32_t intr, uint32_t set)
{
	if (set)
		dsp_core.interrupt_status |= (1<<intr);
	else
		dsp_core.interrupt_status &= ~(1<<intr);
}

/* Mask or unmask an interrupt */
void dsp_set_interrupt_mask(uint32_t intr, uint32_t set)
{
	if (set)
		dsp_core.interrupt_mask |= (1<<intr);
	else
		dsp_core.interrupt_mask &= ~(1<<intr);
}

static void dsp_setInterruptIPL(uint32_t value)
{
	uint32_t ipl_irqa, ipl_irqb, ipl_hi, ipl_ssi, ipl_sci;

	ipl_irqa = (value & 3);
	ipl_irqb = ((value >> 3) & 3);
	ipl_hi  = ((value >> 10) & 3);
	ipl_ssi = ((value >> 12) & 3);
	ipl_sci = ((value >> 14) & 3);

	/* Reset all masks */
	dsp_core.interrupt_enable = 0;
	dsp_core.interrupt_mask_level[0] = 0;
	dsp_core.interrupt_mask_level[1] = 0;
	dsp_core.interrupt_mask_level[2] = 0;
	dsp_core.interrupt_edgetriggered_mask = DSP_INTER_EDGE_MASK;

    /* Set masks to programmed values */
	if (ipl_irqa) {
		dsp_core.interrupt_enable |= DSP_INTER_IRQA_MASK;
		dsp_core.interrupt_mask_level[ipl_irqa-1] |= DSP_INTER_IRQA_MASK;
	}
	if (ipl_irqb) {
		dsp_core.interrupt_enable |= DSP_INTER_IRQB_MASK;
		dsp_core.interrupt_mask_level[ipl_irqb-1] |= DSP_INTER_IRQB_MASK;
	}
	if (ipl_hi) {
		dsp_core.interrupt_enable |= DSP_INTER_HOST_MASK;
		dsp_core.interrupt_mask_level[ipl_hi-1] |= DSP_INTER_HOST_MASK;
	}
	if (ipl_ssi) {
		dsp_core.interrupt_enable |= DSP_INTER_SSI_MASK;
		dsp_core.interrupt_mask_level[ipl_ssi-1] |= DSP_INTER_SSI_MASK;
	}
	if (ipl_sci) {
		dsp_core.interrupt_enable |= DSP_INTER_SCI_MASK;
		dsp_core.interrupt_mask_level[ipl_sci-1] |= DSP_INTER_SCI_MASK;
	}

	/* Add IRQA, IRQB as edge triggered, if configured */
	if (value & 0x04) {
		dsp_core.interrupt_edgetriggered_mask |= DSP_INTER_IRQA_MASK;
	}
	if (value & 0x20) {
		dsp_core.interrupt_edgetriggered_mask |= DSP_INTER_IRQB_MASK;
	}
}

static void dsp_postexecute_interrupts(void)
{
	int i;
	uint32_t interrupt, inter;
	uint32_t priority_list_start;
	uint32_t instr;
	int32_t ipl_sr;


	/* REP is not interruptible */
	if (dsp_core.loop_rep) {
		return;
	}

	/* A fast interrupt can not be interrupted. */
	if (dsp_core.interrupt_state == DSP_INTERRUPT_DISABLED) {

		switch (dsp_core.interrupt_pipeline_count) {
			case 5:
				dsp_core.interrupt_pipeline_count --;
				return;
			case 4:
				/* Prefetch interrupt instruction 1 */
				dsp_core.interrupt_save_pc = dsp_core.pc;
				dsp_core.pc = dsp_core.interrupt_instr_fetch;

				/* is it a LONG interrupt ? */
				instr = read_memory_p(dsp_core.interrupt_instr_fetch);
				if ( ((instr & 0xfff000) == 0x0d0000) || ((instr & 0xffc0ff) == 0x0bc080) ) {
					dsp_core.interrupt_state = DSP_INTERRUPT_LONG;
					dsp_stack_push(dsp_core.interrupt_save_pc, dsp_core.registers[DSP_REG_SR], 0);
					dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_LF)|(1<<DSP_SR_T)  |
											(1<<DSP_SR_S1)|(1<<DSP_SR_S0) |
											(1<<DSP_SR_I0)|(1<<DSP_SR_I1));
					dsp_core.registers[DSP_REG_SR] |= dsp_core.interrupt_IplToRaise<<DSP_SR_I0;
				}
				dsp_core.interrupt_pipeline_count --;
				return;
			case 3:
				/* Prefetch interrupt instruction 2, if first one was single word */
				if (dsp_core.pc == dsp_core.interrupt_instr_fetch+1) {
					instr = read_memory_p(dsp_core.pc);
					if ( ((instr & 0xfff000) == 0x0d0000) || ((instr & 0xffc0ff) == 0x0bc080) ) {
						dsp_core.interrupt_state = DSP_INTERRUPT_LONG;
						dsp_stack_push(dsp_core.interrupt_save_pc, dsp_core.registers[DSP_REG_SR], 0);
						dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_LF)|(1<<DSP_SR_T)  |
												(1<<DSP_SR_S1)|(1<<DSP_SR_S0) |
												(1<<DSP_SR_I0)|(1<<DSP_SR_I1));
						dsp_core.registers[DSP_REG_SR] |= dsp_core.interrupt_IplToRaise<<DSP_SR_I0;
					}
					dsp_core.interrupt_pipeline_count --;
					return;
				}
				dsp_core.interrupt_pipeline_count --;
				/* First instruction was 2 word. Fall through */
			case 2:
				/* 1 instruction executed after interrupt */
				/* before re enable interrupts */
				/* Was it a FAST interrupt ? */
				if (dsp_core.pc == dsp_core.interrupt_instr_fetch+2) {
					dsp_core.pc = dsp_core.interrupt_save_pc;
				}
				dsp_core.interrupt_pipeline_count --;
				return;
			case 1:
				/* Last instruction executed after interrupt */
				/* before re enable interrupts */
				dsp_core.interrupt_pipeline_count --;
				return;
			case 0:
				/* Re enable interrupts */
				/* All 6 instruction are done, Interrupts can be enabled again */
				dsp_core.interrupt_save_pc = -1;
				dsp_core.interrupt_instr_fetch = -1;
				dsp_core.interrupt_state = DSP_INTERRUPT_NONE;
				break;
		}
	}

	/* See if there is an interrupt pending */
	if (dsp_core.interrupt_status & DSP_INTER_NMI_MASK) {
		interrupt = dsp_core.interrupt_status & DSP_INTER_NMI_MASK;
		dsp_core.interrupt_IplToRaise = 3;
		priority_list_start = DSP_INTER_RESET;
	} else {
		interrupt = 0;
		inter = dsp_core.interrupt_status & dsp_core.interrupt_enable & dsp_core.interrupt_mask;

		/* If there is no enabled interrupt, stop here */
		if (!inter) {
			return;
		}

		/* Find out the interrupt priority level */
		ipl_sr = (dsp_core.registers[DSP_REG_SR]>>DSP_SR_I0) & BITMASK(2);
		for (i = 2; i >= ipl_sr; i--) {
			if (inter & dsp_core.interrupt_mask_level[i]) {
				dsp_core.interrupt_IplToRaise = i+1;
				interrupt = inter & dsp_core.interrupt_mask_level[i];
				break;
			}
		}
		priority_list_start = DSP_INTER_IRQA;
	}

	/* If there is no unmasked interrupt, stop here */
	if (!interrupt) {
		return;
	}

	/* Find out which interrupt is pending, using priorities */
	for (i = priority_list_start; i != DSP_PRIORITY_LIST_EXIT; i = dsp_inter_priority_list[i]) {
		if (interrupt & (1<<i)) {
			interrupt = i;
			break;
		}
	}

	LOG_TRACE(TRACE_DSP_INTERRUPT, "Dsp interrupt: %s\n", dsp_interrupt_name[interrupt]);

	/* Auto-release edge triggered interrupts (IRQB simplified for NeXT) */
	if ((1<<interrupt) & (dsp_core.interrupt_edgetriggered_mask|DSP_INTER_IRQB_MASK)) {
		dsp_set_interrupt(interrupt, 0);
	}

	/* Host command interrupt */
	if (interrupt == DSP_INTER_HOST_COMMAND) {
		interrupt = dsp_core.hostport[CPU_HOST_CVR] & BITMASK(5);

		/* Clear HC and HCP interrupt */
		dsp_core.periph[DSP_SPACE_X][DSP_HOST_HSR] &= 0xff - (1<<DSP_HOST_HSR_HCP);
		dsp_core.hostport[CPU_HOST_CVR] &= 0xff - (1<<CPU_HOST_CVR_HC);
	}

	/* Set variables for interrupt routine */
	dsp_core.interrupt_pipeline_count = 5;
	dsp_core.interrupt_state = DSP_INTERRUPT_DISABLED;

	/* Set the interrupt vector */
	dsp_core.interrupt_instr_fetch = interrupt * 2;
}

/**********************************
 *	Set/clear ccr bits
 **********************************/

/* reg0 has bits 55..48 */
/* reg1 has bits 47..24 */
/* reg2 has bits 23..0 */

static void dsp_ccr_update_e_u_n_z(uint32_t reg0, uint32_t reg1, uint32_t reg2)
{
	uint32_t scaling, value_e, value_u;

	/* Initialize SR register */
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_E) | (1<<DSP_SR_U) | (1<<DSP_SR_N) | (1<<DSP_SR_Z));

	scaling = (dsp_core.registers[DSP_REG_SR]>>DSP_SR_S0) & BITMASK(2);
	switch(scaling) {
		case 0:
			/* Extension Bit (E) */
			value_e = (reg0<<1) + (reg1>>23);
			if ((value_e != 0) && (value_e != BITMASK(9)))
				dsp_core.registers[DSP_REG_SR] |= 1 << DSP_SR_E;

			/* Unnormalized bit (U) */
			if ((reg1 & 0xc00000) == 0 || (reg1 & 0xc00000) == 0xc00000)
				dsp_core.registers[DSP_REG_SR] |= 1 << DSP_SR_U;
			break;
		case 1:
			/* Extension Bit (E) */
			if ((reg0 != 0) && (reg0 != BITMASK(8)))
				dsp_core.registers[DSP_REG_SR] |= 1 << DSP_SR_E;

			/* Unnormalized bit (U) */
			value_u = ((reg0<<1) + (reg1>>23)) & 3;
			if (value_u == 0 || value_u == 3)
				dsp_core.registers[DSP_REG_SR] |= 1 << DSP_SR_U;
			break;
		case 2:
			/* Extension Bit (E) */
			value_e = (reg0<<2) + (reg1>>22);
			if ((value_e != 0) && (value_e != BITMASK(10)))
				dsp_core.registers[DSP_REG_SR] |= 1 << DSP_SR_E;

			/* Unnormalized bit (U) */
			if ((reg1 & 0x600000) == 0 || (reg1 & 0x600000) == 0x600000)
				dsp_core.registers[DSP_REG_SR] |= 1 << DSP_SR_U;
			break;
		default:
			return;
			break;
	}

	/* Zero Flag (Z) */
	if ((reg1 == 0) && (reg2 == 0) && (reg0 == 0))
		dsp_core.registers[DSP_REG_SR] |= 1 << DSP_SR_Z;

	/* Negative Flag (N) */
	dsp_core.registers[DSP_REG_SR] |= (reg0>>4) & 0x8;
}

/**********************************
 *	Read/Write memory functions
 **********************************/

static uint32_t read_memory_disasm(int space, uint16_t address)
{
	/* Program memory space ? */
	if (space==DSP_SPACE_P) {
		return read_memory_p(address);
	}

	/* Internal RAM ? */
	if (address<0x100) {
		return dsp_core.ramint[space][address] & BITMASK(24);
	}

	/* Internal ROM? */
	if ((dsp_core.registers[DSP_REG_OMR] & (1<<DSP_OMR_DE)) &&
		(address<0x200)) {
		return dsp_core.rom[space][address] & BITMASK(24);
	}

	/* Peripheral address ? */
	if (address >= 0xffc0) {
		if ((space==DSP_SPACE_X) && (address==0xffc0+DSP_HOST_HTX)) {
			return dsp_core.dsp_host_htx;
		}
		if ((space==DSP_SPACE_X) && (address==0xffc0+DSP_SSI_TX)) {
			return dsp_core.ssi.transmit_value;
		}
		return dsp_core.periph[space][address-0xffc0] & BITMASK(24);
	}

	/* Falcon: External RAM, map X to upper 16K of matching space in Y,P */
	address &= (DSP_RAMSIZE>>1) - 1;
	if (space == DSP_SPACE_X) {
		address |= DSP_RAMSIZE>>1;
	}

	/* Falcon: External RAM, finally map X,Y to P */
	return dsp_core.ramext[address & (DSP_RAMSIZE-1)] & BITMASK(24);
}

static inline uint32_t read_memory_p(uint16_t address)
{
	/* Internal RAM ? */
	if (address < 0x200) {
		return dsp_core.ramint[DSP_SPACE_P][address] & BITMASK(24);
	}

	/* Access to the external P memory */
	access_to_ext_memory |= 1 << DSP_SPACE_P;

	/* External RAM, mask address to available ram size */
	return dsp_core.ramext[address & (DSP_RAMSIZE-1)] & BITMASK(24);
}

static uint32_t read_memory(int space, uint16_t address)
{
	uint32_t value;

	/* Program memory space ? */
	if (space == DSP_SPACE_P) {
		return read_memory_p(address);
	}

	/* Internal RAM ? */
	if (address < 0x100) {
		return dsp_core.ramint[space][address] & BITMASK(24);
	}

	/* Internal ROM ? */
	if (address < 0x200) {
		if (dsp_core.registers[DSP_REG_OMR] & (1<<DSP_OMR_DE)) {
			return dsp_core.rom[space][address] & BITMASK(24);
		}
	}

	/* Peripheral address ? */
	if (address >= 0xffc0) {
		value = dsp_core.periph[space][address-0xffc0] & BITMASK(24);
		if (space == DSP_SPACE_X) {
			if (address == 0xffc0+DSP_HOST_HRX) {
				value = dsp_core.dsp_host_rtx;
				dsp_core_hostport_dspread();
			}
			else if (address == 0xffc0+DSP_SSI_RX) {
				value = dsp_core_ssi_readRX();
			}
		}
		return value;
	}

	/* Access to external memory */
	access_to_ext_memory |= 1 << space;

	/* Falcon: External X or Y RAM access */
	address &= (DSP_RAMSIZE>>1) - 1;

	if (space == DSP_SPACE_X) {
		/* Map X to upper 16K of matching space in Y,P */
		address |= DSP_RAMSIZE>>1;
	}

	/* Falcon: External RAM, finally map X,Y to P */
	return dsp_core.ramext[address & (DSP_RAMSIZE-1)] & BITMASK(24);
}

static inline void write_memory(int space, uint16_t address, uint32_t value)
{
	if (LOG_TRACE_LEVEL(TRACE_DSP_DISASM_MEM))
		write_memory_disasm(space, address, value);
	else
		write_memory_raw(space, address, value);
}

static void write_memory_raw(int space, uint16_t address, uint32_t value)
{
	value &= BITMASK(24);

	/* Peripheral address ? */
	if (address >= 0xffc0) {
		if (space == DSP_SPACE_X) {
			switch(address-0xffc0) {
				case DSP_HOST_HTX:
					dsp_core.dsp_host_htx = value;
					dsp_core_hostport_dspwrite();
					break;
				case DSP_HOST_HCR:
					dsp_core.periph[DSP_SPACE_X][DSP_HOST_HCR] = value & 0x1f;
					/* Set HF3 and HF2 accordingly on the host side */
					dsp_core.hostport[CPU_HOST_ISR] &=
						BITMASK(8)-((1<<CPU_HOST_ISR_HF3)|(1<<CPU_HOST_ISR_HF2));
					dsp_core.hostport[CPU_HOST_ISR] |=
						dsp_core.periph[DSP_SPACE_X][DSP_HOST_HCR] & ((1<<CPU_HOST_ISR_HF3)|(1<<CPU_HOST_ISR_HF2));
					/* Handle interrupt mask */
					dsp_set_interrupt_mask(DSP_INTER_HOST_RCV_DATA, dsp_core.periph[DSP_SPACE_X][DSP_HOST_HCR]&(1<<DSP_HOST_HCR_HRIE));
					dsp_set_interrupt_mask(DSP_INTER_HOST_TRX_DATA, dsp_core.periph[DSP_SPACE_X][DSP_HOST_HCR]&(1<<DSP_HOST_HCR_HTIE));
					dsp_set_interrupt_mask(DSP_INTER_HOST_COMMAND, dsp_core.periph[DSP_SPACE_X][DSP_HOST_HCR]&(1<<DSP_HOST_HCR_HCIE));
					break;
				case DSP_HOST_HSR:
					/* Read only */
					break;
				case DSP_SSI_CRA:
					dsp_core.periph[DSP_SPACE_X][address-0xffc0] = value;
					dsp_core_ssi_configure(address-0xffc0, value);
					break;
				case DSP_SSI_CRB:
					dsp_core.periph[DSP_SPACE_X][address-0xffc0] = value;
					dsp_core_ssi_configure(address-0xffc0, value);
					dsp_set_interrupt_mask(DSP_INTER_SSI_RCV_DATA_E, dsp_core.periph[DSP_SPACE_X][DSP_SSI_CRB]&(1<<DSP_SSI_CRB_RIE));
					dsp_set_interrupt_mask(DSP_INTER_SSI_RCV_DATA, dsp_core.periph[DSP_SPACE_X][DSP_SSI_CRB]&(1<<DSP_SSI_CRB_RIE));
					dsp_set_interrupt_mask(DSP_INTER_SSI_TRX_DATA_E, dsp_core.periph[DSP_SPACE_X][DSP_SSI_CRB]&(1<<DSP_SSI_CRB_TIE));
					dsp_set_interrupt_mask(DSP_INTER_SSI_TRX_DATA, dsp_core.periph[DSP_SPACE_X][DSP_SSI_CRB]&(1<<DSP_SSI_CRB_TIE));
					break;
				case DSP_SSI_TSR:
					dsp_core_ssi_writeTSR();
					break;
				case DSP_SSI_TX:
					dsp_core_ssi_writeTX(value);
					break;
				case DSP_IPR:
					dsp_core.periph[DSP_SPACE_X][DSP_IPR] = value;
					dsp_setInterruptIPL(value);
					break;
				case DSP_PCD:
					dsp_core.periph[DSP_SPACE_X][DSP_PCD] = value;
					dsp_core_setPortCDataRegister(value);
					break;
				case DSP_PBC:
					dsp_core.periph[DSP_SPACE_X][DSP_PBC] = value;
					dsp_set_interrupt_mask(DSP_INTER_SCI_RCV_DATA_E, dsp_core.periph[DSP_SPACE_X][DSP_PBC]&(1<<11));
					dsp_set_interrupt_mask(DSP_INTER_SCI_RCV_DATA, dsp_core.periph[DSP_SPACE_X][DSP_PBC]&(1<<11));
					dsp_set_interrupt_mask(DSP_INTER_SCI_TRX_DATA, dsp_core.periph[DSP_SPACE_X][DSP_PBC]&(1<<12));
					dsp_set_interrupt_mask(DSP_INTER_SCI_IDLE_LINE, dsp_core.periph[DSP_SPACE_X][DSP_PBC]&(1<<10));
					dsp_set_interrupt_mask(DSP_INTER_SCI_TIMER, dsp_core.periph[DSP_SPACE_X][DSP_PBC]&(1<<13));
					break;
				default:
					dsp_core.periph[DSP_SPACE_X][address-0xffc0] = value;
					break;
			}
			return;
		}
		else if (space == DSP_SPACE_Y) {
			dsp_core.periph[DSP_SPACE_Y][address-0xffc0] = value;
			return;
		}
	}

	/* Internal RAM ? */
	if (address < 0x100) {
		dsp_core.ramint[space][address] = value;
		return;
	}

	/* Internal ROM ? */
	if (address < 0x200) {
		if (space != DSP_SPACE_P) {
			if (dsp_core.registers[DSP_REG_OMR] & (1<<DSP_OMR_DE)) {
				/* Can not write to ROM space */
				return;
			}
		}
		else {
			/* Space P RAM */
			dsp_core.ramint[DSP_SPACE_P][address] = value;
			return;
		}
	}

	/* Access to external memory */
	access_to_ext_memory |= 1 << space;

	/* Access to X, Y or P external RAM */

	if (space != DSP_SPACE_P) {
		address &= (DSP_RAMSIZE>>1) - 1;

		if (space == DSP_SPACE_X) {
			/* Access to the X external RAM */
			/* map X to upper 16K of matching space in Y,P */
			address |= DSP_RAMSIZE>>1;
		}
	}

	/* Falcon: External RAM, map X,Y to P */
	dsp_core.ramext[address & (DSP_RAMSIZE-1)] = value;
}

static void write_memory_disasm(int space, uint16_t address, uint32_t value)
{
	uint32_t oldvalue, curvalue;
	uint8_t space_c = 'p';

	value &= BITMASK(24);
	oldvalue = read_memory_disasm(space, address);

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

	curvalue = read_memory_disasm(space, address);
	snprintf(str_disasm_memory[disasm_memory_ptr], sizeof(str_disasm_memory[0]),
	         "Mem: %c:0x%04x  0x%06x -> 0x%06x", space_c, address, oldvalue, curvalue);
	disasm_memory_ptr ++;
}

static void dsp_write_reg(uint32_t numreg, uint32_t value)
{
	uint32_t stack_error;

	switch (numreg) {
		case DSP_REG_A:
			dsp_core.registers[DSP_REG_A0] = 0;
			dsp_core.registers[DSP_REG_A1] = value & BITMASK(24);
			dsp_core.registers[DSP_REG_A2] = value & (1<<23) ? 0xff : 0x0;
			break;
		case DSP_REG_B:
			dsp_core.registers[DSP_REG_B0] = 0;
			dsp_core.registers[DSP_REG_B1] = value & BITMASK(24);
			dsp_core.registers[DSP_REG_B2] = value & (1<<23) ? 0xff : 0x0;
			break;
		case DSP_REG_R0:
		case DSP_REG_R1:
		case DSP_REG_R2:
		case DSP_REG_R3:
		case DSP_REG_R4:
		case DSP_REG_R5:
		case DSP_REG_R6:
		case DSP_REG_R7:
		case DSP_REG_N0:
		case DSP_REG_N1:
		case DSP_REG_N2:
		case DSP_REG_N3:
		case DSP_REG_N4:
		case DSP_REG_N5:
		case DSP_REG_N6:
		case DSP_REG_N7:
		case DSP_REG_M0:
		case DSP_REG_M1:
		case DSP_REG_M2:
		case DSP_REG_M3:
		case DSP_REG_M4:
		case DSP_REG_M5:
		case DSP_REG_M6:
		case DSP_REG_M7:
			dsp_core.registers[numreg] = value & BITMASK(16);
			break;
		case DSP_REG_OMR:
			dsp_core.registers[DSP_REG_OMR] = value & 0xc7;
			break;
		case DSP_REG_SR:
			dsp_core.registers[DSP_REG_SR] = value & 0xaf7f;
			break;
		case DSP_REG_SP:
			stack_error = dsp_core.registers[DSP_REG_SP] & (3<<DSP_SP_SE);
			if ((stack_error==0) && (value & (3<<DSP_SP_SE))) {
				/* Stack underflow or overflow detected, raise interrupt */
				dsp_set_interrupt(DSP_INTER_STACK_ERROR, 1);
				dsp_core.registers[DSP_REG_SP] = value & (3<<DSP_SP_SE);
				if (!isDsp_in_disasm_mode)
					Log_Printf(LOG_WARN, "Dsp: Stack Overflow or Underflow\n");
				if (ExceptionDebugMask & EXCEPT_DSP)
					DebugUI(REASON_DSP_EXCEPTION);
			}
			else
				dsp_core.registers[DSP_REG_SP] = value & BITMASK(6);
			dsp_compute_ssh_ssl();
			break;
		case DSP_REG_SSH:
			dsp_stack_push(value, 0, 1);
			break;
		case DSP_REG_SSL:
			numreg = dsp_core.registers[DSP_REG_SP] & BITMASK(4);
			if (numreg == 0) {
				value = 0;
			}
			dsp_core.stack[1][numreg] = value & BITMASK(16);
			dsp_core.registers[DSP_REG_SSL] = value & BITMASK(16);
			break;
		default:
			dsp_core.registers[numreg] = value;
			dsp_core.registers[numreg] &= BITMASK(registers_mask[numreg]);
			break;
	}
}

/**********************************
 *	Stack push/pop
 **********************************/

static void dsp_stack_push(uint32_t curpc, uint32_t cursr, uint16_t sshOnly)
{
	uint32_t stack_error, underflow, stack;

	stack_error = dsp_core.registers[DSP_REG_SP] & (1<<DSP_SP_SE);
	underflow = dsp_core.registers[DSP_REG_SP] & (1<<DSP_SP_UF);
	stack = (dsp_core.registers[DSP_REG_SP] & BITMASK(4)) + 1;


	if ((stack_error==0) && (stack & (1<<DSP_SP_SE))) {
		/* Stack full, raise interrupt */
		dsp_set_interrupt(DSP_INTER_STACK_ERROR, 1);
		if (!isDsp_in_disasm_mode)
			Log_Printf(LOG_WARN, "Dsp: Stack Overflow\n");
		if (ExceptionDebugMask & EXCEPT_DSP)
			DebugUI(REASON_DSP_EXCEPTION);
	}

	dsp_core.registers[DSP_REG_SP] = (underflow | stack_error | stack) & BITMASK(6);
	stack &= BITMASK(4);

	if (stack) {
		/* SSH part */
		dsp_core.stack[0][stack] = curpc & BITMASK(16);
		/* SSL part, if instruction is not like "MOVEC xx, SSH"  */
		if (sshOnly == 0) {
			dsp_core.stack[1][stack] = cursr & BITMASK(16);
		}
	} else {
		dsp_core.stack[0][0] = 0;
		dsp_core.stack[1][0] = 0;
	}

	/* Update SSH and SSL registers */
	dsp_core.registers[DSP_REG_SSH] = dsp_core.stack[0][stack];
	dsp_core.registers[DSP_REG_SSL] = dsp_core.stack[1][stack];
}

static void dsp_stack_pop(uint32_t *newpc, uint32_t *newsr)
{
	uint32_t stack_error, underflow, stack;

	stack_error = dsp_core.registers[DSP_REG_SP] & (1<<DSP_SP_SE);
	underflow = dsp_core.registers[DSP_REG_SP] & (1<<DSP_SP_UF);
	stack = (dsp_core.registers[DSP_REG_SP] & BITMASK(4)) - 1;

	if ((stack_error==0) && (stack & (1<<DSP_SP_SE))) {
		/* Stack empty*/
		dsp_set_interrupt(DSP_INTER_STACK_ERROR, 1);
		if (!isDsp_in_disasm_mode)
			Log_Printf(LOG_WARN, "Dsp: Stack underflow\n");
		if (ExceptionDebugMask & EXCEPT_DSP)
			DebugUI(REASON_DSP_EXCEPTION);
	}

	dsp_core.registers[DSP_REG_SP] = (underflow | stack_error | stack) & BITMASK(6);
	stack &= BITMASK(4);
	*newpc = dsp_core.registers[DSP_REG_SSH];
	*newsr = dsp_core.registers[DSP_REG_SSL];

	dsp_core.registers[DSP_REG_SSH] = dsp_core.stack[0][stack];
	dsp_core.registers[DSP_REG_SSL] = dsp_core.stack[1][stack];
}

static void dsp_compute_ssh_ssl(void)
{
	uint32_t stack;

	stack = dsp_core.registers[DSP_REG_SP];
	stack &= BITMASK(4);
	dsp_core.registers[DSP_REG_SSH] = dsp_core.stack[0][stack];
	dsp_core.registers[DSP_REG_SSL] = dsp_core.stack[1][stack];
}

/**********************************
 *	Effective address calculation
 **********************************/

static void dsp_update_rn(uint32_t numreg, int16_t modifier)
{
	uint32_t value;
	uint16_t m_reg;

	m_reg = (uint16_t) dsp_core.registers[DSP_REG_M0+numreg];
	if (m_reg == 65535) {
		/* Linear addressing mode */
		value = dsp_core.registers[DSP_REG_R0+numreg]|0x10000;
		value += modifier;
		dsp_core.registers[DSP_REG_R0+numreg] = value & BITMASK(16);
	} else if (m_reg == 0) {
		/* Bit reversed carry update */
		dsp_update_rn_bitreverse(numreg);
	} else if (m_reg<=32767) {
		/* Modulo update */
		dsp_update_rn_modulo(numreg, modifier);
	} else {
		/* Undefined */
	}
}

static void dsp_update_rn_bitreverse(uint32_t numreg)
{
	int revbits, i;
	uint32_t value, r_reg;

	/* Check how many bits to reverse */
	value = dsp_core.registers[DSP_REG_N0+numreg];
	for (revbits=0;revbits<16;revbits++) {
		if (value & (1<<revbits)) {
			break;
		}
	}
	revbits++;

	/* Reverse Rn bits */
	r_reg = dsp_core.registers[DSP_REG_R0+numreg];
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

	dsp_core.registers[DSP_REG_R0+numreg] = value;
}

static void dsp_update_rn_modulo(uint32_t numreg, int16_t modifier)
{
	uint16_t bufsize, bufmask, modulo, abs_modifier;
	uint32_t r_reg, lobound, hibound;

	r_reg = dsp_core.registers[DSP_REG_R0+numreg]|0x10000;
	modulo = dsp_core.registers[DSP_REG_M0+numreg]+1;


	bufsize = 1;
	while (bufsize < modulo) {
		bufsize <<= 1;
	}
	bufmask = bufsize - 1;


	lobound = r_reg - (r_reg&bufmask);
	hibound = lobound + modulo - 1;


	if (modifier<0) {
		abs_modifier = -modifier;
	} else {
		abs_modifier = modifier;
	}


	if (abs_modifier>modulo) {
		if (abs_modifier&bufmask) {
			Log_Printf(LOG_WARN, "Dsp: Modulo addressing result unpredictable\n");
		} else {
			r_reg += modifier;
		}
	} else {
		r_reg += modifier;


		if (r_reg>hibound) {
			r_reg -= modulo;
		} else if (r_reg<lobound) {
			r_reg += modulo;
		}
	}

	dsp_core.registers[DSP_REG_R0+numreg] = r_reg & BITMASK(16);
}

static int dsp_calc_ea(uint32_t ea_mode, uint32_t *dst_addr)
{
	uint32_t value, numreg, curreg;

	value = (ea_mode >> 3) & BITMASK(3);
	numreg = ea_mode & BITMASK(3);
	switch (value) {
		case 0:
			/* (Rx)-Nx */
			*dst_addr = dsp_core.registers[DSP_REG_R0+numreg];
			dsp_update_rn(numreg, -dsp_core.registers[DSP_REG_N0+numreg]);
			break;
		case 1:
			/* (Rx)+Nx */
			*dst_addr = dsp_core.registers[DSP_REG_R0+numreg];
			dsp_update_rn(numreg, dsp_core.registers[DSP_REG_N0+numreg]);
			break;
		case 2:
			/* (Rx)- */
			*dst_addr = dsp_core.registers[DSP_REG_R0+numreg];
			dsp_update_rn(numreg, -1);
			break;
		case 3:
			/* (Rx)+ */
			*dst_addr = dsp_core.registers[DSP_REG_R0+numreg];
			dsp_update_rn(numreg, 1);
			break;
		case 4:
			/* (Rx) */
			*dst_addr = dsp_core.registers[DSP_REG_R0+numreg];
			break;
		case 5:
			/* (Rx+Nx) */
			dsp_core.instr_cycle += 2;
			curreg = dsp_core.registers[DSP_REG_R0+numreg];
			dsp_update_rn(numreg, dsp_core.registers[DSP_REG_N0+numreg]);
			*dst_addr = dsp_core.registers[DSP_REG_R0+numreg];
			dsp_core.registers[DSP_REG_R0+numreg] = curreg;
			break;
		case 6:
			/* aa */
			dsp_core.instr_cycle += 2;
			*dst_addr = read_memory_p(dsp_core.pc+1);
			cur_inst_len++;
			if (numreg != 0) {
				return 1; /* immediate value */
			}
			break;
		case 7:
			/* -(Rx) */
			dsp_core.instr_cycle += 2;
			dsp_update_rn(numreg, -1);
			*dst_addr = dsp_core.registers[DSP_REG_R0+numreg];
			break;
	}
	/* address */
	return 0;
}

/**********************************
 *	Condition code test
 **********************************/

static int dsp_calc_cc(uint32_t cc_code)
{
	uint16_t value1, value2, value3;

	switch (cc_code) {
		case 0:  /* CC (HS) */
			value1 = dsp_core.registers[DSP_REG_SR] & (1<<DSP_SR_C);
			return (value1==0);
		case 1: /* GE */
			value1 = (dsp_core.registers[DSP_REG_SR] >> DSP_SR_N) & 1;
			value2 = (dsp_core.registers[DSP_REG_SR] >> DSP_SR_V) & 1;
			return ((value1 ^ value2) == 0);
		case 2: /* NE */
			value1 = dsp_core.registers[DSP_REG_SR] & (1<<DSP_SR_Z);
			return (value1==0);
		case 3: /* PL */
			value1 = dsp_core.registers[DSP_REG_SR] & (1<<DSP_SR_N);
			return (value1==0);
		case 4: /* NN */
			value1 = (dsp_core.registers[DSP_REG_SR] >> DSP_SR_Z) & 1;
			value2 = (~(dsp_core.registers[DSP_REG_SR] >> DSP_SR_U)) & 1;
			value3 = (~(dsp_core.registers[DSP_REG_SR] >> DSP_SR_E)) & 1;
			return ((value1 | (value2 & value3)) == 0);
		case 5: /* EC */
			value1 = dsp_core.registers[DSP_REG_SR] & (1<<DSP_SR_E);
			return (value1==0);
		case 6: /* LC */
			value1 = dsp_core.registers[DSP_REG_SR] & (1<<DSP_SR_L);
			return (value1==0);
		case 7: /* GT */
			value1 = (dsp_core.registers[DSP_REG_SR] >> DSP_SR_N) & 1;
			value2 = (dsp_core.registers[DSP_REG_SR] >> DSP_SR_V) & 1;
			value3 = (dsp_core.registers[DSP_REG_SR] >> DSP_SR_Z) & 1;
			return ((value3 | (value1 ^ value2)) == 0);
		case 8: /* CS (LO) */
			value1 = dsp_core.registers[DSP_REG_SR] & (1<<DSP_SR_C);
			return (value1==1);
		case 9: /* LT */
			value1 = (dsp_core.registers[DSP_REG_SR] >> DSP_SR_N) & 1;
			value2 = (dsp_core.registers[DSP_REG_SR] >> DSP_SR_V) & 1;
			return ((value1 ^ value2) == 1);
		case 10: /* EQ */
			value1 = (dsp_core.registers[DSP_REG_SR] >> DSP_SR_Z) & 1;
			return (value1==1);
		case 11: /* MI */
			value1 = (dsp_core.registers[DSP_REG_SR] >> DSP_SR_N) & 1;
			return (value1==1);
		case 12: /* NR */
			value1 = (dsp_core.registers[DSP_REG_SR] >> DSP_SR_Z) & 1;
			value2 = (~(dsp_core.registers[DSP_REG_SR] >> DSP_SR_U)) & 1;
			value3 = (~(dsp_core.registers[DSP_REG_SR] >> DSP_SR_E)) & 1;
			return ((value1 | (value2 & value3)) == 1);
		case 13: /* ES */
			value1 = (dsp_core.registers[DSP_REG_SR] >> DSP_SR_E) & 1;
			return (value1==1);
		case 14: /* LS */
			value1 = (dsp_core.registers[DSP_REG_SR] >> DSP_SR_L) & 1;
			return (value1==1);
		case 15: /* LE */
			value1 = (dsp_core.registers[DSP_REG_SR] >> DSP_SR_N) & 1;
			value2 = (dsp_core.registers[DSP_REG_SR] >> DSP_SR_V) & 1;
			value3 = (dsp_core.registers[DSP_REG_SR] >> DSP_SR_Z) & 1;
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
		default:
			dsp_undefined();
			break;
	}
}

/**********************************
 *	Non-parallel moves instructions
 **********************************/

static void dsp_undefined(void)
{
	if (isDsp_in_disasm_mode == false) {
		cur_inst_len = 0;
		Log_Printf(LOG_WARN, "Dsp: 0x%04x: 0x%06x Illegal instruction\n",dsp_core.pc, cur_inst);
		/* Add some artificial CPU cycles to avoid being stuck in an infinite loop */
		dsp_core.instr_cycle += 100;
	}
	else {
		cur_inst_len = 1;
		dsp_core.instr_cycle = 0;
	}
	if (ExceptionDebugMask & EXCEPT_DSP) {
		DebugUI(REASON_DSP_EXCEPTION);
	}
}

static void dsp_andi(void)
{
	uint32_t regnum, value;

	value = (cur_inst >> 8) & BITMASK(8);
	regnum = cur_inst & BITMASK(2);
	switch(regnum) {
		case 0:
			/* mr */
			dsp_core.registers[DSP_REG_SR] &= (value<<8)|BITMASK(8);
			break;
		case 1:
			/* ccr */
			dsp_core.registers[DSP_REG_SR] &= (BITMASK(8)<<8)|value;
			break;
		case 2:
			/* omr */
			dsp_core.registers[DSP_REG_OMR] &= value;
			break;
	}
}

static void dsp_bchg_aa(void)
{
	uint32_t memspace, addr, value, newcarry, numbit;

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
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core.registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core.instr_cycle += 2;
}

static void dsp_bchg_ea(void)
{
	uint32_t memspace, addr, value, newcarry, numbit;

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
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core.registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core.instr_cycle += 2;
}

static void dsp_bchg_pp(void)
{
	uint32_t memspace, addr, value, newcarry, numbit;

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
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core.registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core.instr_cycle += 2;
}

static void dsp_bchg_reg(void)
{
	uint32_t value, numreg, newcarry, numbit;

	numreg = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	if ((numreg==DSP_REG_A) || (numreg==DSP_REG_B)) {
		dsp_pm_read_accu24(numreg, &value);
	} else {
		value = dsp_core.registers[numreg];
	}

	newcarry = (value>>numbit) & 1;
	if (newcarry) {
		value -= (1<<numbit);
	} else {
		value += (1<<numbit);
	}

	dsp_write_reg(numreg, value);

	/* Set carry */
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core.registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core.instr_cycle += 2;
}

static void dsp_bclr_aa(void)
{
	uint32_t memspace, addr, value, newcarry, numbit;

	memspace = (cur_inst>>6) & 1;
	addr = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;
	value &= 0xffffffff-(1<<numbit);
	write_memory(memspace, addr, value);

	/* Set carry */
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core.registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core.instr_cycle += 2;
}

static void dsp_bclr_ea(void)
{
	uint32_t memspace, addr, value, newcarry, numbit;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	dsp_calc_ea(value, &addr);
	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;
	value &= 0xffffffff-(1<<numbit);
	write_memory(memspace, addr, value);

	/* Set carry */
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core.registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core.instr_cycle += 2;
}

static void dsp_bclr_pp(void)
{
	uint32_t memspace, addr, value, newcarry, numbit;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	addr = 0xffc0 + value;
	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;
	value &= 0xffffffff-(1<<numbit);
	write_memory(memspace, addr, value);

	/* Set carry */
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core.registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core.instr_cycle += 2;
}

static void dsp_bclr_reg(void)
{
	uint32_t value, numreg, newcarry, numbit;

	numreg = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	if ((numreg==DSP_REG_A) || (numreg==DSP_REG_B)) {
		dsp_pm_read_accu24(numreg, &value);
	} else {
		value = dsp_core.registers[numreg];
	}

	newcarry = (value>>numbit) & 1;
	value &= 0xffffffff-(1<<numbit);

	dsp_write_reg(numreg, value);

	/* Set carry */
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core.registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core.instr_cycle += 2;
}

static void dsp_bset_aa(void)
{
	uint32_t memspace, addr, value, newcarry, numbit;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	addr = value;
	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;
	value |= (1<<numbit);
	write_memory(memspace, addr, value);

	/* Set carry */
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core.registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core.instr_cycle += 2;
}

static void dsp_bset_ea(void)
{
	uint32_t memspace, addr, value, newcarry, numbit;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	dsp_calc_ea(value, &addr);
	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;
	value |= (1<<numbit);
	write_memory(memspace, addr, value);

	/* Set carry */
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core.registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core.instr_cycle += 2;
}

static void dsp_bset_pp(void)
{
	uint32_t memspace, addr, value, newcarry, numbit;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	addr = 0xffc0 + value;
	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;
	value |= (1<<numbit);
	write_memory(memspace, addr, value);

	/* Set carry */
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core.registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core.instr_cycle += 2;
}

static void dsp_bset_reg(void)
{
	uint32_t value, numreg, newcarry, numbit;

	numreg = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	if ((numreg==DSP_REG_A) || (numreg==DSP_REG_B)) {
		dsp_pm_read_accu24(numreg, &value);
	} else {
		value = dsp_core.registers[numreg];
	}

	newcarry = (value>>numbit) & 1;
	value |= (1<<numbit);

	dsp_write_reg(numreg, value);

	/* Set carry */
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core.registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core.instr_cycle += 2;
}

static void dsp_btst_aa(void)
{
	uint32_t memspace, addr, value, newcarry, numbit;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	addr = value;
	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;

	/* Set carry */
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core.registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core.instr_cycle += 2;
}

static void dsp_btst_ea(void)
{
	uint32_t memspace, addr, value, newcarry, numbit;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	dsp_calc_ea(value, &addr);
	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;

	/* Set carry */
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core.registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core.instr_cycle += 2;
}

static void dsp_btst_pp(void)
{
	uint32_t memspace, addr, value, newcarry, numbit;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	addr = 0xffc0 + value;
	value = read_memory(memspace, addr);
	newcarry = (value>>numbit) & 1;

	/* Set carry */
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core.registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core.instr_cycle += 2;
}

static void dsp_btst_reg(void)
{
	uint32_t value, numreg, newcarry, numbit;

	numreg = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);

	if ((numreg==DSP_REG_A) || (numreg==DSP_REG_B)) {
		dsp_pm_read_accu24(numreg, &value);
	} else {
		value = dsp_core.registers[numreg];
	}

	newcarry = (value>>numbit) & 1;

	/* Set carry */
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_C);
	dsp_core.registers[DSP_REG_SR] |= newcarry<<DSP_SR_C;

	dsp_core.instr_cycle += 2;
}

static void dsp_div(void)
{
	uint32_t srcreg, destreg, source[3], dest[3];
	uint16_t newsr;

	srcreg = DSP_REG_NULL;
	switch((cur_inst>>4) & BITMASK(2)) {
		case 0:	srcreg = DSP_REG_X0;	break;
		case 1:	srcreg = DSP_REG_Y0;	break;
		case 2:	srcreg = DSP_REG_X1;	break;
		case 3:	srcreg = DSP_REG_Y1;	break;
	}
	source[2] = 0;
	source[1] = dsp_core.registers[srcreg];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	destreg = DSP_REG_A + ((cur_inst>>3) & 1);
	if (destreg == DSP_REG_A) {
		dest[0] = dsp_core.registers[DSP_REG_A2];
		dest[1] = dsp_core.registers[DSP_REG_A1];
		dest[2] = dsp_core.registers[DSP_REG_A0];
	}
	else {
		dest[0] = dsp_core.registers[DSP_REG_B2];
		dest[1] = dsp_core.registers[DSP_REG_B1];
		dest[2] = dsp_core.registers[DSP_REG_B0];
	}

	if (((dest[0]>>7) & 1) ^ ((source[1]>>23) & 1)) {
		/* D += S */
		newsr = dsp_asl56(dest);
		dsp_add56(source, dest);
	} else {
		/* D -= S */
		newsr = dsp_asl56(dest);
		dsp_sub56(source, dest);
	}

	dest[2] |= (dsp_core.registers[DSP_REG_SR]>>DSP_SR_C) & 1;

	if (destreg == DSP_REG_A) {
		dsp_core.registers[DSP_REG_A2] = dest[0];
		dsp_core.registers[DSP_REG_A1] = dest[1];
		dsp_core.registers[DSP_REG_A0] = dest[2];
	}
	else {
		dsp_core.registers[DSP_REG_B2] = dest[0];
		dsp_core.registers[DSP_REG_B1] = dest[1];
		dsp_core.registers[DSP_REG_B0] = dest[2];
	}

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_C)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= (1-((dest[0]>>7) & 1))<<DSP_SR_C;
	dsp_core.registers[DSP_REG_SR] |= newsr & (1<<DSP_SR_L);
	dsp_core.registers[DSP_REG_SR] |= newsr & (1<<DSP_SR_V);
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
	uint32_t memspace, addr;

	/* x:aa */
	/* y:aa */

	dsp_stack_push(dsp_core.registers[DSP_REG_LA], dsp_core.registers[DSP_REG_LC], 0);
	dsp_core.registers[DSP_REG_LA] = read_memory_p(dsp_core.pc+1) & BITMASK(16);
	cur_inst_len++;
	dsp_stack_push(dsp_core.pc+cur_inst_len, dsp_core.registers[DSP_REG_SR], 0);
	dsp_core.registers[DSP_REG_SR] |= (1<<DSP_SR_LF);

	memspace = (cur_inst>>6) & 1;
	addr = (cur_inst>>8) & BITMASK(6);
	dsp_core.registers[DSP_REG_LC] = read_memory(memspace, addr) & BITMASK(16);

	dsp_core.instr_cycle += 4;
}

static void dsp_do_imm(void)
{
	/* #xx */

	dsp_stack_push(dsp_core.registers[DSP_REG_LA], dsp_core.registers[DSP_REG_LC], 0);
	dsp_core.registers[DSP_REG_LA] = read_memory_p(dsp_core.pc+1) & BITMASK(16);
	cur_inst_len++;
	dsp_stack_push(dsp_core.pc+cur_inst_len, dsp_core.registers[DSP_REG_SR], 0);
	dsp_core.registers[DSP_REG_SR] |= (1<<DSP_SR_LF);

	dsp_core.registers[DSP_REG_LC] = ((cur_inst>>8) & BITMASK(8))
		+ ((cur_inst & BITMASK(4))<<8);

	dsp_core.instr_cycle += 4;
}

static void dsp_do_ea(void)
{
	uint32_t memspace, ea_mode, addr;

	/* x:ea */
	/* y:ea */

	dsp_stack_push(dsp_core.registers[DSP_REG_LA], dsp_core.registers[DSP_REG_LC], 0);
	dsp_core.registers[DSP_REG_LA] = read_memory_p(dsp_core.pc+1) & BITMASK(16);
	cur_inst_len++;
	dsp_stack_push(dsp_core.pc+cur_inst_len, dsp_core.registers[DSP_REG_SR], 0);
	dsp_core.registers[DSP_REG_SR] |= (1<<DSP_SR_LF);

	memspace = (cur_inst>>6) & 1;
	ea_mode = (cur_inst>>8) & BITMASK(6);
	dsp_calc_ea(ea_mode, &addr);
	dsp_core.registers[DSP_REG_LC] = read_memory(memspace, addr) & BITMASK(16);

	dsp_core.instr_cycle += 4;
}

static void dsp_do_reg(void)
{
	uint32_t numreg;

	/* S */

	dsp_stack_push(dsp_core.registers[DSP_REG_LA], dsp_core.registers[DSP_REG_LC], 0);
	dsp_core.registers[DSP_REG_LA] = read_memory_p(dsp_core.pc+1) & BITMASK(16);
	cur_inst_len++;

	numreg = (cur_inst>>8) & BITMASK(6);
	if ((numreg == DSP_REG_A) || (numreg == DSP_REG_B)) {
		dsp_pm_read_accu24(numreg, &dsp_core.registers[DSP_REG_LC]);
	} else {
		dsp_core.registers[DSP_REG_LC] = dsp_core.registers[numreg];
	}
	dsp_core.registers[DSP_REG_LC] &= BITMASK(16);

	dsp_stack_push(dsp_core.pc+cur_inst_len, dsp_core.registers[DSP_REG_SR], 0);
	dsp_core.registers[DSP_REG_SR] |= (1<<DSP_SR_LF);

	dsp_core.instr_cycle += 4;
}

static void dsp_enddo(void)
{
	uint32_t saved_pc, saved_sr;

	dsp_stack_pop(&saved_pc, &saved_sr);
	dsp_core.registers[DSP_REG_SR] &= 0x7f;
	dsp_core.registers[DSP_REG_SR] |= saved_sr & (1<<DSP_SR_LF);
	dsp_stack_pop(&dsp_core.registers[DSP_REG_LA], &dsp_core.registers[DSP_REG_LC]);
}

static void dsp_illegal(void)
{
	/* Raise interrupt p:0x003e */
	dsp_set_interrupt(DSP_INTER_ILLEGAL, 1);
	if (ExceptionDebugMask & EXCEPT_DSP) {
		DebugUI(REASON_DSP_EXCEPTION);
	}
}

static void dsp_jcc_imm(void)
{
	uint32_t cc_code, newpc;

	newpc = cur_inst & BITMASK(12);
	cc_code=(cur_inst>>12) & BITMASK(4);
	if (dsp_calc_cc(cc_code)) {
		dsp_core.pc = newpc;
		cur_inst_len = 0;
	}

	dsp_core.instr_cycle += 2;
}

static void dsp_jcc_ea(void)
{
	uint32_t newpc, cc_code;

	dsp_calc_ea((cur_inst >>8) & BITMASK(6), &newpc);
	cc_code=cur_inst & BITMASK(4);

	if (dsp_calc_cc(cc_code)) {
		dsp_core.pc = newpc;
		cur_inst_len = 0;
	}

	dsp_core.instr_cycle += 2;
}

static void dsp_jclr_aa(void)
{
	uint32_t memspace, addr, value, numbit, newaddr;

	memspace = (cur_inst>>6) & 1;
	addr = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core.pc+1);

	dsp_core.instr_cycle += 4;

	if ((value & (1<<numbit))==0) {
		dsp_core.pc = newaddr;
		cur_inst_len = 0;
		return;
	}
	++cur_inst_len;
}

static void dsp_jclr_ea(void)
{
	uint32_t memspace, addr, value, numbit, newaddr;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	newaddr = read_memory_p(dsp_core.pc+1);

	dsp_calc_ea(value, &addr);
	value = read_memory(memspace, addr);

	dsp_core.instr_cycle += 4;

	if ((value & (1<<numbit))==0) {
		dsp_core.pc = newaddr;
		cur_inst_len = 0;
		return;
	}
	++cur_inst_len;
}

static void dsp_jclr_pp(void)
{
	uint32_t memspace, addr, value, numbit, newaddr;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	addr = 0xffc0 + value;
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core.pc+1);

	dsp_core.instr_cycle += 4;

	if ((value & (1<<numbit))==0) {
		dsp_core.pc = newaddr;
		cur_inst_len = 0;
		return;
	}
	++cur_inst_len;
}

static void dsp_jclr_reg(void)
{
	uint32_t value, numreg, numbit, newaddr;

	numreg = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	newaddr = read_memory_p(dsp_core.pc+1);

	if ((numreg==DSP_REG_A) || (numreg==DSP_REG_B)) {
		dsp_pm_read_accu24(numreg, &value);
	} else {
		value = dsp_core.registers[numreg];
	}

	dsp_core.instr_cycle += 4;

	if ((value & (1<<numbit))==0) {
		dsp_core.pc = newaddr;
		cur_inst_len = 0;
		return;
	}
	++cur_inst_len;
}

static void dsp_jmp_ea(void)
{
	uint32_t newpc;

	dsp_calc_ea((cur_inst>>8) & BITMASK(6), &newpc);
	cur_inst_len = 0;
	dsp_core.pc = newpc;

	dsp_core.instr_cycle += 2;
}

static void dsp_jmp_imm(void)
{
	uint32_t newpc;

	newpc = cur_inst & BITMASK(12);
	cur_inst_len = 0;
	dsp_core.pc = newpc;

	dsp_core.instr_cycle += 2;
}

static void dsp_jscc_ea(void)
{
	uint32_t newpc, cc_code;

	dsp_calc_ea((cur_inst >>8) & BITMASK(6), &newpc);
	cc_code=cur_inst & BITMASK(4);

	if (dsp_calc_cc(cc_code)) {
		dsp_stack_push(dsp_core.pc+cur_inst_len, dsp_core.registers[DSP_REG_SR], 0);
		dsp_core.pc = newpc;
		cur_inst_len = 0;
	}

	dsp_core.instr_cycle += 2;
}

static void dsp_jscc_imm(void)
{
	uint32_t cc_code, newpc;

	newpc = cur_inst & BITMASK(12);
	cc_code=(cur_inst>>12) & BITMASK(4);
	if (dsp_calc_cc(cc_code)) {
		dsp_stack_push(dsp_core.pc+cur_inst_len, dsp_core.registers[DSP_REG_SR], 0);
		dsp_core.pc = newpc;
		cur_inst_len = 0;
	}

	dsp_core.instr_cycle += 2;
}

static void dsp_jsclr_aa(void)
{
	uint32_t memspace, addr, value, newpc, numbit, newaddr;

	memspace = (cur_inst>>6) & 1;
	addr = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core.pc+1);

	dsp_core.instr_cycle += 4;

	if ((value & (1<<numbit))==0) {
		dsp_stack_push(dsp_core.pc+2, dsp_core.registers[DSP_REG_SR], 0);
		newpc = newaddr;
		dsp_core.pc = newpc;
		cur_inst_len = 0;
		return;
	}
	++cur_inst_len;
}

static void dsp_jsclr_ea(void)
{
	uint32_t memspace, addr, value, newpc, numbit, newaddr;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	dsp_calc_ea(value, &addr);
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core.pc+1);

	dsp_core.instr_cycle += 4;

	if ((value & (1<<numbit))==0) {
		dsp_stack_push(dsp_core.pc+2, dsp_core.registers[DSP_REG_SR], 0);
		newpc = newaddr;
		dsp_core.pc = newpc;
		cur_inst_len = 0;
		return;
	}
	++cur_inst_len;
}

static void dsp_jsclr_pp(void)
{
	uint32_t memspace, addr, value, newpc, numbit, newaddr;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	addr = 0xffc0 + value;
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core.pc+1);

	dsp_core.instr_cycle += 4;

	if ((value & (1<<numbit))==0) {
		dsp_stack_push(dsp_core.pc+2, dsp_core.registers[DSP_REG_SR], 0);
		newpc = newaddr;
		dsp_core.pc = newpc;
		cur_inst_len = 0;
		return;
	}
	++cur_inst_len;
}

static void dsp_jsclr_reg(void)
{
	uint32_t value, numreg, newpc, numbit, newaddr;

	numreg = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	newaddr = read_memory_p(dsp_core.pc+1);

	if ((numreg==DSP_REG_A) || (numreg==DSP_REG_B)) {
		dsp_pm_read_accu24(numreg, &value);
	} else {
		value = dsp_core.registers[numreg];
	}

	dsp_core.instr_cycle += 4;

	if ((value & (1<<numbit))==0) {
		dsp_stack_push(dsp_core.pc+2, dsp_core.registers[DSP_REG_SR], 0);
		newpc = newaddr;
		dsp_core.pc = newpc;
		cur_inst_len = 0;
		return;
	}
	++cur_inst_len;
}

static void dsp_jset_aa(void)
{
	uint32_t memspace, addr, value, numbit, newpc, newaddr;

	memspace = (cur_inst>>6) & 1;
	addr = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core.pc+1);

	dsp_core.instr_cycle += 4;

	if (value & (1<<numbit)) {
		newpc = newaddr;
		dsp_core.pc = newpc;
		cur_inst_len=0;
		return;
	}
	++cur_inst_len;
}

static void dsp_jset_ea(void)
{
	uint32_t memspace, addr, value, numbit, newpc, newaddr;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	dsp_calc_ea(value, &addr);
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core.pc+1);

	dsp_core.instr_cycle += 4;

	if (value & (1<<numbit)) {
		newpc = newaddr;
		dsp_core.pc = newpc;
		cur_inst_len=0;
		return;
	}
	++cur_inst_len;
}

static void dsp_jset_pp(void)
{
	uint32_t memspace, addr, value, numbit, newpc, newaddr;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	addr = 0xffc0 + value;
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core.pc+1);

	dsp_core.instr_cycle += 4;

	if (value & (1<<numbit)) {
		newpc = newaddr;
		dsp_core.pc = newpc;
		cur_inst_len=0;
		return;
	}
	++cur_inst_len;
}

static void dsp_jset_reg(void)
{
	uint32_t value, numreg, numbit, newpc, newaddr;

	numreg = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	newaddr = read_memory_p(dsp_core.pc+1);

	if ((numreg==DSP_REG_A) || (numreg==DSP_REG_B)) {
		dsp_pm_read_accu24(numreg, &value);
	} else {
		value = dsp_core.registers[numreg];
	}

	dsp_core.instr_cycle += 4;

	if (value & (1<<numbit)) {
		newpc = newaddr;
		dsp_core.pc = newpc;
		cur_inst_len=0;
		return;
	}
	++cur_inst_len;
}

static void dsp_jsr_imm(void)
{
	uint32_t newpc;

	newpc = cur_inst & BITMASK(12);

	if (dsp_core.interrupt_state != DSP_INTERRUPT_LONG){
		dsp_stack_push(dsp_core.pc+cur_inst_len, dsp_core.registers[DSP_REG_SR], 0);
	}
	else {
		dsp_core.interrupt_state = DSP_INTERRUPT_DISABLED;
	}

	dsp_core.pc = newpc;
	cur_inst_len = 0;

	dsp_core.instr_cycle += 2;
}

static void dsp_jsr_ea(void)
{
	uint32_t newpc;

	dsp_calc_ea((cur_inst>>8) & BITMASK(6),&newpc);

	if (dsp_core.interrupt_state != DSP_INTERRUPT_LONG){
		dsp_stack_push(dsp_core.pc+cur_inst_len, dsp_core.registers[DSP_REG_SR], 0);
	}
	else {
		dsp_core.interrupt_state = DSP_INTERRUPT_DISABLED;
	}

	dsp_core.pc = newpc;
	cur_inst_len = 0;

	dsp_core.instr_cycle += 2;
}

static void dsp_jsset_aa(void)
{
	uint32_t memspace, addr, value, newpc, numbit, newaddr;

	memspace = (cur_inst>>6) & 1;
	addr = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core.pc+1);

	dsp_core.instr_cycle += 4;

	if (value & (1<<numbit)) {
		dsp_stack_push(dsp_core.pc+2, dsp_core.registers[DSP_REG_SR], 0);
		newpc = newaddr;
		dsp_core.pc = newpc;
		cur_inst_len = 0;
		return;
	}
	++cur_inst_len;
}

static void dsp_jsset_ea(void)
{
	uint32_t memspace, addr, value, newpc, numbit, newaddr;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	dsp_calc_ea(value, &addr);
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core.pc+1);

	dsp_core.instr_cycle += 4;

	if (value & (1<<numbit)) {
		dsp_stack_push(dsp_core.pc+2, dsp_core.registers[DSP_REG_SR], 0);
		newpc = newaddr;
		dsp_core.pc = newpc;
		cur_inst_len = 0;
		return;
	}
	++cur_inst_len;
}

static void dsp_jsset_pp(void)
{
	uint32_t memspace, addr, value, newpc, numbit, newaddr;

	memspace = (cur_inst>>6) & 1;
	value = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	addr = 0xffc0 + value;
	value = read_memory(memspace, addr);
	newaddr = read_memory_p(dsp_core.pc+1);

	dsp_core.instr_cycle += 4;

	if (value & (1<<numbit)) {
		dsp_stack_push(dsp_core.pc+2, dsp_core.registers[DSP_REG_SR], 0);
		newpc = newaddr;
		dsp_core.pc = newpc;
		cur_inst_len = 0;
		return;
	}
	++cur_inst_len;
}

static void dsp_jsset_reg(void)
{
	uint32_t value, numreg, newpc, numbit, newaddr;

	numreg = (cur_inst>>8) & BITMASK(6);
	numbit = cur_inst & BITMASK(5);
	newaddr = read_memory_p(dsp_core.pc+1);

	if ((numreg==DSP_REG_A) || (numreg==DSP_REG_B)) {
		dsp_pm_read_accu24(numreg, &value);
	} else {
		value = dsp_core.registers[numreg];
	}

	dsp_core.instr_cycle += 4;

	if (value & (1<<numbit)) {
		dsp_stack_push(dsp_core.pc+2, dsp_core.registers[DSP_REG_SR], 0);
		newpc = newaddr;
		dsp_core.pc = newpc;
		cur_inst_len = 0;
		return;
	}
	++cur_inst_len;
}

static void dsp_lua(void)
{
	uint32_t value, srcreg, dstreg, srcsave, srcnew;

	srcreg = (cur_inst>>8) & BITMASK(3);

	srcsave = dsp_core.registers[DSP_REG_R0+srcreg];
	dsp_calc_ea((cur_inst>>8) & BITMASK(5), &value);
	srcnew = dsp_core.registers[DSP_REG_R0+srcreg];
	dsp_core.registers[DSP_REG_R0+srcreg] = srcsave;

	if (cur_inst & (1<<3))
		dstreg = DSP_REG_N0 + (cur_inst & BITMASK(3));
	else
		dstreg = DSP_REG_R0 + (cur_inst & BITMASK(3));

	dsp_core.agu_move_indirect_instr = 1;
	dsp_write_reg(dstreg, srcnew);
	dsp_core.instr_cycle += 2;
}

static void dsp_movec_reg(void)
{
	uint32_t numreg1, numreg2, value, dummy;

	/* S1,D2 */
	/* S2,D1 */

	numreg2 = (cur_inst>>8) & BITMASK(6);
	numreg1 = cur_inst & BITMASK(6);

	dsp_core.agu_move_indirect_instr = 1;

	if (cur_inst & (1<<15)) {
		/* Write D1 */

		if ((numreg2 == DSP_REG_A) || (numreg2 == DSP_REG_B)) {
			dsp_pm_read_accu24(numreg2, &value);
		} else {
			value = dsp_core.registers[numreg2];
		}
		dsp_write_reg(numreg1, value);
	} else {
		/* Read S1 */
		if (numreg1 == DSP_REG_SSH) {
			dsp_stack_pop(&value, &dummy);
		}
		else {
			value = dsp_core.registers[numreg1];
		}

		dsp_write_reg(numreg2, value);
	}
}

static void dsp_movec_aa(void)
{
	uint32_t numreg, addr, memspace, value, dummy;

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
		dsp_core.agu_move_indirect_instr = 1;
		dsp_write_reg(numreg, value);
	} else {
		/* Read S1 */
		if (numreg == DSP_REG_SSH) {
			dsp_stack_pop(&value, &dummy);
		}
		else {
			value = dsp_core.registers[numreg];
		}
		write_memory(memspace, addr, value);
	}
}

static void dsp_movec_imm(void)
{
	uint32_t numreg, value;

	/* #xx,D1 */
	numreg = cur_inst & BITMASK(6);
	value = (cur_inst>>8) & BITMASK(8);
	dsp_core.agu_move_indirect_instr = 1;
	dsp_write_reg(numreg, value);
}

static void dsp_movec_ea(void)
{
	uint32_t numreg, addr, memspace, ea_mode, value, dummy;
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
		dsp_core.agu_move_indirect_instr = 1;
		dsp_write_reg(numreg, value);
	} else {
		/* Read S1 */
		dsp_calc_ea(ea_mode, &addr);
		if (numreg == DSP_REG_SSH) {
			dsp_stack_pop(&value, &dummy);
		}
		else {
			value = dsp_core.registers[numreg];
		}
		write_memory(memspace, addr, value);
	}
}

static void dsp_movem_aa(void)
{
	uint32_t numreg, addr, value, dummy;

	numreg = cur_inst & BITMASK(6);
	addr = (cur_inst>>8) & BITMASK(6);

	if  (cur_inst & (1<<15)) {
		/* Write D */
		value = read_memory_p(addr);
		dsp_core.agu_move_indirect_instr = 1;
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
			value = dsp_core.registers[numreg];
		}
		write_memory(DSP_SPACE_P, addr, value);
	}

	dsp_core.instr_cycle += 4;
}

static void dsp_movem_ea(void)
{
	uint32_t numreg, addr, ea_mode, value, dummy;

	numreg = cur_inst & BITMASK(6);
	ea_mode = (cur_inst>>8) & BITMASK(6);
	dsp_calc_ea(ea_mode, &addr);

	if  (cur_inst & (1<<15)) {
		/* Write D */
		value = read_memory_p(addr);
		dsp_core.agu_move_indirect_instr = 1;
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
			value = dsp_core.registers[numreg];
		}
		write_memory(DSP_SPACE_P, addr, value);
	}

	dsp_core.instr_cycle += 4;
}

static void dsp_movep_0(void)
{
	/* S,x:pp */
	/* x:pp,D */
	/* S,y:pp */
	/* y:pp,D */

	uint32_t addr, memspace, numreg, value, dummy;

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
			value = dsp_core.registers[numreg];
		}
		write_memory(memspace, addr, value);
	} else {
		/* Read pp */
		value = read_memory(memspace, addr);
		dsp_core.agu_move_indirect_instr = 1;
		dsp_write_reg(numreg, value);
	}

	dsp_core.instr_cycle += 2;
}

static void dsp_movep_1(void)
{
	/* p:ea,x:pp */
	/* x:pp,p:ea */
	/* p:ea,y:pp */
	/* y:pp,p:ea */

	uint32_t xyaddr, memspace, paddr;

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

	/* Movep is 4 cycles, but according to the motorola doc, */
	/* movep from p memory to x or y peripheral memory takes */
	/* 2 more cycles, so +4 cycles at total */
	dsp_core.instr_cycle += 4;
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

	uint32_t addr, peraddr, easpace, perspace, ea_mode;
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
			write_memory(perspace, peraddr, read_memory(easpace, addr));
		}
	} else {
		/* Read pp */
		write_memory(easpace, addr, read_memory(perspace, peraddr));
	}

	dsp_core.instr_cycle += 2;
}

static void dsp_norm(void)
{
	uint32_t cursr,cur_e, cur_euz, dest[3], numreg, rreg;
	uint16_t newsr;

	cursr = dsp_core.registers[DSP_REG_SR];
	cur_e = (cursr>>DSP_SR_E) & 1;	/* E */
	cur_euz = ~cur_e;			/* (not E) and U and (not Z) */
	cur_euz &= (cursr>>DSP_SR_U) & 1;
	cur_euz &= ~((cursr>>DSP_SR_Z) & 1);
	cur_euz &= 1;

	numreg = (cur_inst>>3) & 1;
	dest[0] = dsp_core.registers[DSP_REG_A2+numreg];
	dest[1] = dsp_core.registers[DSP_REG_A1+numreg];
	dest[2] = dsp_core.registers[DSP_REG_A0+numreg];
	rreg = DSP_REG_R0+((cur_inst>>8) & BITMASK(3));

	if (cur_euz) {
		newsr = dsp_asl56(dest);
		--dsp_core.registers[rreg];
		dsp_core.registers[rreg] &= BITMASK(16);
	} else if (cur_e) {
		newsr = dsp_asr56(dest);
		++dsp_core.registers[rreg];
		dsp_core.registers[rreg] &= BITMASK(16);
	} else {
		newsr = 0;
	}

	dsp_core.registers[DSP_REG_A2+numreg] = dest[0];
	dsp_core.registers[DSP_REG_A1+numreg] = dest[1];
	dsp_core.registers[DSP_REG_A0+numreg] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_ori(void)
{
	uint32_t regnum, value;

	value = (cur_inst >> 8) & BITMASK(8);
	regnum = cur_inst & BITMASK(2);
	switch(regnum) {
		case 0:
			/* mr */
			dsp_core.registers[DSP_REG_SR] |= value<<8;
			break;
		case 1:
			/* ccr */
			dsp_core.registers[DSP_REG_SR] |= value;
			break;
		case 2:
			/* omr */
			dsp_core.registers[DSP_REG_OMR] |= value;
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
	dsp_core.registers[DSP_REG_LCSAVE] = dsp_core.registers[DSP_REG_LC];
	dsp_core.pc_on_rep = 1;	/* Not decrement LC at first time */
	dsp_core.loop_rep = 1; 	/* We are now running rep */

	dsp_core.registers[DSP_REG_LC]=read_memory((cur_inst>>6) & 1,(cur_inst>>8) & BITMASK(6));

	dsp_core.instr_cycle += 2;
}

static void dsp_rep_imm(void)
{
	/* #xxx */

	dsp_core.registers[DSP_REG_LCSAVE] = dsp_core.registers[DSP_REG_LC];
	dsp_core.pc_on_rep = 1;	/* Not decrement LC at first time */
	dsp_core.loop_rep = 1; 	/* We are now running rep */

	dsp_core.registers[DSP_REG_LC] = ((cur_inst>>8) & BITMASK(8))
		+ ((cur_inst & BITMASK(4))<<8);

	dsp_core.instr_cycle += 2;
}

static void dsp_rep_ea(void)
{
	uint32_t value;

	/* x:ea */
	/* y:ea */

	dsp_core.registers[DSP_REG_LCSAVE] = dsp_core.registers[DSP_REG_LC];
	dsp_core.pc_on_rep = 1;	/* Not decrement LC at first time */
	dsp_core.loop_rep = 1; 	/* We are now running rep */

	dsp_calc_ea((cur_inst>>8) & BITMASK(6),&value);
	dsp_core.registers[DSP_REG_LC]= read_memory((cur_inst>>6) & 1, value);

	dsp_core.instr_cycle += 2;
}

static void dsp_rep_reg(void)
{
	uint32_t numreg;

	/* R */

	dsp_core.registers[DSP_REG_LCSAVE] = dsp_core.registers[DSP_REG_LC];
	dsp_core.pc_on_rep = 1;	/* Not decrement LC at first time */
	dsp_core.loop_rep = 1; 	/* We are now running rep */

	numreg = (cur_inst>>8) & BITMASK(6);
	if ((numreg == DSP_REG_A) || (numreg == DSP_REG_B)) {
		dsp_pm_read_accu24(numreg, &dsp_core.registers[DSP_REG_LC]);
	} else {
		dsp_core.registers[DSP_REG_LC] = dsp_core.registers[numreg];
	}
	dsp_core.registers[DSP_REG_LC] &= BITMASK(16);

	dsp_core.instr_cycle += 2;
}

static void dsp_reset(void)
{
	/* Clear the IPR register */
	write_memory(DSP_SPACE_X, 0xffc0 + DSP_IPR, 0);

	/* Software reset all on-chip peripherals */

	/* HOST_HCR x:$FFE8 : clear the full register */
	write_memory(DSP_SPACE_X, 0xffc0 + DSP_HOST_HCR, 0);

	/* HOST_ICR $0 : clear the full register */
	dsp_core_write_host(CPU_HOST_ICR, 0);

	/* HOST_CVR $1 : set the register to $12 */
	dsp_core_write_host(CPU_HOST_CVR, 0x12);

	/* HOST_ISR $2 : set the bits TRDY and TXDE 1, other bits to 0 */
	dsp_core.hostport[CPU_HOST_ISR] = (1<<CPU_HOST_ISR_TRDY)|(1<<CPU_HOST_ISR_TXDE);

	/* HOST_IVR $3 : set the register to $0f */
	dsp_core_write_host(CPU_HOST_IVR, 0x0f);

	/* SSI_CRA x:$FFEC : clear the full register */
	write_memory(DSP_SPACE_X, 0xffc0 + DSP_SSI_CRA, 0);

	/* SSI_CRB x:$FFED : clear the full register */
	write_memory(DSP_SPACE_X, 0xffc0 + DSP_SSI_CRB, 0);

	/* SSI_SR x:$FFEE : set the register to $40 */
	write_memory(DSP_SPACE_X, 0xffc0 + DSP_SSI_SR, 1<<DSP_SSI_SR_TDE);

	/* SCI_SCR x:$FFF0 : clear the full register (not used in the Falcon) */
	write_memory(DSP_SPACE_X, 0xffc0 + DSP_SCI_SCR, 0);

	/* SCI_SSR x:$FFF1 : clear the register to $3 (not used in the Falcon) */
	write_memory(DSP_SPACE_X, 0xffc0 + DSP_SCI_SSR, 3);

	/* SCI_SCCR x:$FFF2 : clear the full register (not used in the Falcon) */
	write_memory(DSP_SPACE_X, 0xffc0 + DSP_SCI_SCCR, 0);

	dsp_core.instr_cycle += 2;
}

static void dsp_rti(void)
{
	uint32_t newpc = 0, newsr = 0;

	dsp_stack_pop(&newpc, &newsr);
	dsp_core.pc = newpc;
	dsp_core.registers[DSP_REG_SR] = newsr;
	cur_inst_len = 0;

	dsp_core.instr_cycle += 2;
}

static void dsp_rts(void)
{
	uint32_t newpc = 0, newsr;

	dsp_stack_pop(&newpc, &newsr);
	dsp_core.pc = newpc;
	cur_inst_len = 0;

	dsp_core.instr_cycle += 2;
}

static void dsp_stop(void)
{
	LOG_TRACE(TRACE_DSP_STATE, "Dsp: STOP instruction\n");
}

static void dsp_swi(void)
{
	/* Raise interrupt p:0x0006 */
	dsp_core.instr_cycle += 6;
}

static void dsp_tcc(void)
{
	uint32_t cc_code, regsrc1, regdest1;
	uint32_t regsrc2, regdest2;
	uint32_t val0, val1, val2;

	cc_code = (cur_inst>>12) & BITMASK(4);

	if (dsp_calc_cc(cc_code)) {
		regsrc1 = registers_tcc[(cur_inst>>3) & BITMASK(4)][0];
		regdest1 = registers_tcc[(cur_inst>>3) & BITMASK(4)][1];

		/* Read S1 */
		if (regsrc1 == DSP_REG_A) {
			val0 = dsp_core.registers[DSP_REG_A0];
			val1 = dsp_core.registers[DSP_REG_A1];
			val2 = dsp_core.registers[DSP_REG_A2];
		}
		else if (regsrc1 == DSP_REG_B) {
			val0 = dsp_core.registers[DSP_REG_B0];
			val1 = dsp_core.registers[DSP_REG_B1];
			val2 = dsp_core.registers[DSP_REG_B2];
		}
		else {
			val0 = 0;
			val1 = dsp_core.registers[regsrc1];
			val2 = val1 & (1<<23) ? 0xff : 0x0;
		}

		/* Write D1 */
		if (regdest1 == DSP_REG_A) {
			dsp_core.registers[DSP_REG_A2] = val2;
			dsp_core.registers[DSP_REG_A1] = val1;
			dsp_core.registers[DSP_REG_A0] = val0;
		}
		else {
			dsp_core.registers[DSP_REG_B2] = val2;
			dsp_core.registers[DSP_REG_B1] = val1;
			dsp_core.registers[DSP_REG_B0] = val0;
		}

		/* S2,D2 transfer */
		if (cur_inst & (1<<16)) {
			regsrc2 = DSP_REG_R0+((cur_inst>>8) & BITMASK(3));
			regdest2 = DSP_REG_R0+(cur_inst & BITMASK(3));

			dsp_core.agu_move_indirect_instr = 1;
			dsp_write_reg(regdest2, dsp_core.registers[regsrc2]);
		}
	}
}

static void dsp_wait(void)
{
	LOG_TRACE(TRACE_DSP_STATE, "Dsp: WAIT instruction\n");
}

static int dsp_pm_read_accu24(int numreg, uint32_t *dest)
{
	uint32_t scaling, value, reg;
	int got_limited = 0;

	/* Read an accumulator, stores it limited */

	scaling = (dsp_core.registers[DSP_REG_SR]>>DSP_SR_S0) & BITMASK(2);
	reg = numreg & 1;

	value = (dsp_core.registers[DSP_REG_A2+reg]) << 24;
	value += dsp_core.registers[DSP_REG_A1+reg];

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
			value |= (dsp_core.registers[DSP_REG_A0+reg]>>23) & 1;
			break;
		/* indeterminate */
		case 3:
			break;
	}

	/* limiting ? */
	value &= BITMASK(24);

	if (dsp_core.registers[DSP_REG_A2+reg] == 0) {
		if (value <= 0x007fffff) {
			/* No limiting */
			*dest=value;
			return 0;
		}
	}

	if (dsp_core.registers[DSP_REG_A2+reg] == 0xff) {
		if (value >= 0x00800000) {
			/* No limiting */
			*dest=value;
			return 0;
		}
	}

	if (dsp_core.registers[DSP_REG_A2+reg] & (1<<7)) {
		/* Limited to maximum negative value */
		*dest=0x00800000;
		dsp_core.registers[DSP_REG_SR] |= (1<<DSP_SR_L);
		got_limited=1;
	} else {
		/* Limited to maximal positive value */
		*dest=0x007fffff;
		dsp_core.registers[DSP_REG_SR] |= (1<<DSP_SR_L);
		got_limited=1;
	}

	return got_limited;
}

static void dsp_pm_0(void)
{
	uint32_t memspace, numreg, addr, save_accu, save_xy0;
/*
	0000 100d 00mm mrrr S,x:ea	x0,D
	0000 100d 10mm mrrr S,y:ea	y0,D
*/
	memspace = (cur_inst>>15) & 1;
	numreg = (cur_inst>>16) & 1;
	dsp_calc_ea((cur_inst>>8) & BITMASK(6), &addr);

	/* Save A or B */
	dsp_pm_read_accu24(numreg, &save_accu);

	/* Save X0 or Y0 */
	save_xy0 = dsp_core.registers[DSP_REG_X0+(memspace<<1)];

	/* Execute parallel instruction */
	opcodes_alu[cur_inst & BITMASK(8)]();

	/* Move [A|B] to [x|y]:ea */
	write_memory(memspace, addr, save_accu);

	/* Move [x|y]0 to [A|B] */
	dsp_core.registers[DSP_REG_A0+numreg] = 0;
	dsp_core.registers[DSP_REG_A1+numreg] = save_xy0;
	dsp_core.registers[DSP_REG_A2+numreg] = save_xy0 & (1<<23) ? 0xff : 0x0;
}

static void dsp_pm_1(void)
{
	uint32_t memspace, numreg1, numreg2, value, xy_addr, retour, save_1, save_2;
/*
	0001 ffdf w0mm mrrr 			x:ea,D1		S2,D2
						S1,x:ea		S2,D2
						#xxxxxx,D1	S2,D2
	0001 deff w1mm mrrr 			S1,D1		y:ea,D2
						S1,D1		S2,y:ea
						S1,D1		#xxxxxx,D2
*/
	value = (cur_inst>>8) & BITMASK(6);
	retour = dsp_calc_ea(value, &xy_addr);
	memspace = (cur_inst>>14) & 1;
	numreg1 = numreg2 = DSP_REG_NULL;

	if (memspace) {
		/* Y: */
		switch((cur_inst>>16) & BITMASK(2)) {
			case 0:	numreg1 = DSP_REG_Y0;	break;
			case 1:	numreg1 = DSP_REG_Y1;	break;
			case 2:	numreg1 = DSP_REG_A;	break;
			case 3:	numreg1 = DSP_REG_B;	break;
		}
	} else {
		/* X: */
		switch((cur_inst>>18) & BITMASK(2)) {
			case 0:	numreg1 = DSP_REG_X0;	break;
			case 1:	numreg1 = DSP_REG_X1;	break;
			case 2:	numreg1 = DSP_REG_A;	break;
			case 3:	numreg1 = DSP_REG_B;	break;
		}
	}

	if (cur_inst & (1<<15)) {
		/* Write D1 */
		if (retour)
			save_1 = xy_addr;
		else
			save_1 = read_memory(memspace, xy_addr);
	} else {
		/* Read S1 */
		if ((numreg1==DSP_REG_A) || (numreg1==DSP_REG_B))
			dsp_pm_read_accu24(numreg1, &save_1);
		else
			save_1 = dsp_core.registers[numreg1];
	}

	/* S2 */
	if (memspace) {
		/* Y: */
		numreg2 = DSP_REG_A + ((cur_inst>>19) & 1);
	} else {
		/* X: */
		numreg2 = DSP_REG_A + ((cur_inst>>17) & 1);
	}
	dsp_pm_read_accu24(numreg2, &save_2);


	/* Execute parallel instruction */
	opcodes_alu[cur_inst & BITMASK(8)]();


	/* Write parallel move values */
	if (cur_inst & (1<<15)) {
		/* Write D1 */
		dsp_write_reg(numreg1, save_1);
	} else {
		/* Read S1 */
		write_memory(memspace, xy_addr, save_1);
	}

	/* S2 -> D2 */
	if (memspace) {
		/* Y: */
		numreg2 = DSP_REG_X0 + ((cur_inst>>18) & 1);
	} else {
		/* X: */
		numreg2 = DSP_REG_Y0 + ((cur_inst>>16) & 1);
	}
	dsp_core.registers[numreg2] = save_2;
}

static void dsp_pm_2(void)
{
	uint32_t dummy;
/*
	0010 0000 0000 0000 nop
	0010 0000 010m mrrr R update
	0010 00ee eeed dddd S,D
	001d dddd iiii iiii #xx,D
*/
	if ((cur_inst & 0xffff00) == 0x200000) {
		/* Execute parallel instruction */
		opcodes_alu[cur_inst & BITMASK(8)]();
		return;
	}

	if ((cur_inst & 0xffe000) == 0x204000) {
		dsp_calc_ea((cur_inst>>8) & BITMASK(5), &dummy);
		/* Execute parallel instruction */
		opcodes_alu[cur_inst & BITMASK(8)]();
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
	uint32_t srcreg, dstreg, save_reg;

	srcreg = (cur_inst >> 13) & BITMASK(5);
	dstreg = (cur_inst >> 8) & BITMASK(5);

	if ((srcreg == DSP_REG_A) || (srcreg == DSP_REG_B))
		/* Accu to register: limited 24 bits */
		dsp_pm_read_accu24(srcreg, &save_reg);
	else
		save_reg = dsp_core.registers[srcreg];

	/* Execute parallel instruction */
	opcodes_alu[cur_inst & BITMASK(8)]();

	/* Write reg */
	dsp_core.agu_move_indirect_instr = 1;
	dsp_write_reg(dstreg, save_reg);
}

static void dsp_pm_3(void)
{
	uint32_t dstreg, srcvalue;
/*
	001d dddd iiii iiii #xx,R
*/

	/* Execute parallel instruction */
	opcodes_alu[cur_inst & BITMASK(8)]();

	/* Write reg */
	dstreg = (cur_inst >> 16) & BITMASK(5);
	srcvalue = (cur_inst >> 8) & BITMASK(8);

	switch(dstreg) {
		case DSP_REG_X0:
		case DSP_REG_X1:
		case DSP_REG_Y0:
		case DSP_REG_Y1:
		case DSP_REG_A:
		case DSP_REG_B:
			srcvalue <<= 16;
			break;
	}

	dsp_core.agu_move_indirect_instr = 1;
	dsp_write_reg(dstreg, srcvalue);
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
	uint32_t value, numreg, l_addr, save_lx, save_ly;
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

	if (cur_inst & (1<<15)) {
		/* Write D */
		save_lx = read_memory(DSP_SPACE_X,l_addr);
		save_ly = read_memory(DSP_SPACE_Y,l_addr);
	}
	else {
		/* Read S */
		switch(numreg) {
			case 0:
				/* A10 */
				save_lx = dsp_core.registers[DSP_REG_A1];
				save_ly = dsp_core.registers[DSP_REG_A0];
				break;
			case 1:
				/* B10 */
				save_lx = dsp_core.registers[DSP_REG_B1];
				save_ly = dsp_core.registers[DSP_REG_B0];
				break;
			case 2:
				/* X */
				save_lx = dsp_core.registers[DSP_REG_X1];
				save_ly = dsp_core.registers[DSP_REG_X0];
				break;
			case 3:
				/* Y */
				save_lx = dsp_core.registers[DSP_REG_Y1];
				save_ly = dsp_core.registers[DSP_REG_Y0];
				break;
			case 4:
				/* A */
				if (dsp_pm_read_accu24(DSP_REG_A, &save_lx)) {
					/* Was limited, set lower part */
					save_ly = (save_lx & (1<<23) ? 0 : 0xffffff);
				} else {
					/* Not limited */
					save_ly = dsp_core.registers[DSP_REG_A0];
				}
				break;
			case 5:
				/* B */
				if (dsp_pm_read_accu24(DSP_REG_B, &save_lx)) {
					/* Was limited, set lower part */
					save_ly = (save_lx & (1<<23) ? 0 : 0xffffff);
				} else {
					/* Not limited */
					save_ly = dsp_core.registers[DSP_REG_B0];
				}
				break;
			case 6:
				/* AB */
				dsp_pm_read_accu24(DSP_REG_A, &save_lx);
				dsp_pm_read_accu24(DSP_REG_B, &save_ly);
				break;
			case 7:
				/* BA */
				dsp_pm_read_accu24(DSP_REG_B, &save_lx);
				dsp_pm_read_accu24(DSP_REG_A, &save_ly);
				break;
		}
	}

	/* Execute parallel instruction */
	opcodes_alu[cur_inst & BITMASK(8)]();


	if (cur_inst & (1<<15)) {
		/* Write D */
		switch(numreg) {
			case 0:	/* A10 */
				dsp_core.registers[DSP_REG_A1] = save_lx;
				dsp_core.registers[DSP_REG_A0] = save_ly;
				break;
			case 1:	/* B10 */
				dsp_core.registers[DSP_REG_B1] = save_lx;
				dsp_core.registers[DSP_REG_B0] = save_ly;
				break;
			case 2:	/* X */
				dsp_core.registers[DSP_REG_X1] = save_lx;
				dsp_core.registers[DSP_REG_X0] = save_ly;
				break;
			case 3:	/* Y */
				dsp_core.registers[DSP_REG_Y1] = save_lx;
				dsp_core.registers[DSP_REG_Y0] = save_ly;
				break;
			case 4:	/* A */
				dsp_core.registers[DSP_REG_A0] = save_ly;
				dsp_core.registers[DSP_REG_A1] = save_lx;
				dsp_core.registers[DSP_REG_A2] = save_lx & (1<<23) ? 0xff : 0;
				break;
			case 5:	/* B */
				dsp_core.registers[DSP_REG_B0] = save_ly;
				dsp_core.registers[DSP_REG_B1] = save_lx;
				dsp_core.registers[DSP_REG_B2] = save_lx & (1<<23) ? 0xff : 0;
				break;
			case 6:	/* AB */
				dsp_core.registers[DSP_REG_A0] = 0;
				dsp_core.registers[DSP_REG_A1] = save_lx;
				dsp_core.registers[DSP_REG_A2] = save_lx & (1<<23) ? 0xff : 0;
				dsp_core.registers[DSP_REG_B0] = 0;
				dsp_core.registers[DSP_REG_B1] = save_ly;
				dsp_core.registers[DSP_REG_B2] = save_ly & (1<<23) ? 0xff : 0;
				break;
			case 7:	/* BA */
				dsp_core.registers[DSP_REG_B0] = 0;
				dsp_core.registers[DSP_REG_B1] = save_lx;
				dsp_core.registers[DSP_REG_B2] = save_lx & (1<<23) ? 0xff : 0;
				dsp_core.registers[DSP_REG_A0] = 0;
				dsp_core.registers[DSP_REG_A1] = save_ly;
				dsp_core.registers[DSP_REG_A2] = save_ly & (1<<23) ? 0xff : 0;
				break;
		}
	}
	else {
		/* Read S */
		write_memory(DSP_SPACE_X, l_addr, save_lx);
		write_memory(DSP_SPACE_Y, l_addr, save_ly);
	}
}

static void dsp_pm_5(void)
{
	uint32_t memspace, numreg, value, xy_addr, retour;
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
		if (retour)
			value = xy_addr;
		else
			value = read_memory(memspace, xy_addr);
	}
	else {
		/* Read S */
		if ((numreg==DSP_REG_A) || (numreg==DSP_REG_B))
			dsp_pm_read_accu24(numreg, &value);
		else
			value = dsp_core.registers[numreg];
	}


	/* Execute parallel instruction */
	opcodes_alu[cur_inst & BITMASK(8)]();

	if (cur_inst & (1<<15)) {
		/* Write D */
		dsp_core.agu_move_indirect_instr = 1;
		dsp_write_reg(numreg, value);
	}
	else {
		/* Read S */
		write_memory(memspace, xy_addr, value);
	}
}

static void dsp_pm_8(void)
{
	uint32_t ea1, ea2;
	uint32_t numreg1, numreg2;
	uint32_t save_reg1, save_reg2, x_addr, y_addr;
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
		save_reg1 = read_memory(DSP_SPACE_X, x_addr);
	} else {
		/* Read S1 */
		if ((numreg1==DSP_REG_A) || (numreg1==DSP_REG_B))
			dsp_pm_read_accu24(numreg1, &save_reg1);
		else
			save_reg1 = dsp_core.registers[numreg1];
	}

	if (cur_inst & (1<<22)) {
		/* Write D2 */
		save_reg2 = read_memory(DSP_SPACE_Y, y_addr);
	} else {
		/* Read S2 */
		if ((numreg2==DSP_REG_A) || (numreg2==DSP_REG_B))
			dsp_pm_read_accu24(numreg2, &save_reg2);
		else
			save_reg2 = dsp_core.registers[numreg2];
	}


	/* Execute parallel instruction */
	opcodes_alu[cur_inst & BITMASK(8)]();

	/* Write first parallel move */
	if (cur_inst & (1<<15)) {
		/* Write D1 */
		if (numreg1 == DSP_REG_A) {
			dsp_core.registers[DSP_REG_A0] = 0x0;
			dsp_core.registers[DSP_REG_A1] = save_reg1;
			dsp_core.registers[DSP_REG_A2] = save_reg1 & (1<<23) ? 0xff : 0x0;
		}
		else if (numreg1 == DSP_REG_B) {
			dsp_core.registers[DSP_REG_B0] = 0x0;
			dsp_core.registers[DSP_REG_B1] = save_reg1;
			dsp_core.registers[DSP_REG_B2] = save_reg1 & (1<<23) ? 0xff : 0x0;
		}
		else {
			dsp_core.registers[numreg1] = save_reg1;
		}
	} else {
		/* Read S1 */
		write_memory(DSP_SPACE_X, x_addr, save_reg1);
	}

	/* Write second parallel move */
	if (cur_inst & (1<<22)) {
		/* Write D2 */
		if (numreg2 == DSP_REG_A) {
			dsp_core.registers[DSP_REG_A0] = 0x0;
			dsp_core.registers[DSP_REG_A1] = save_reg2;
			dsp_core.registers[DSP_REG_A2] = save_reg2 & (1<<23) ? 0xff : 0x0;
		}
		else if (numreg2 == DSP_REG_B) {
			dsp_core.registers[DSP_REG_B0] = 0x0;
			dsp_core.registers[DSP_REG_B1] = save_reg2;
			dsp_core.registers[DSP_REG_B2] = save_reg2 & (1<<23) ? 0xff : 0x0;
		}
		else {
			dsp_core.registers[numreg2] = save_reg2;
		}
	} else {
		/* Read S2 */
		write_memory(DSP_SPACE_Y, y_addr, save_reg2);
	}
}

/**********************************
 *	56bit arithmetic
 **********************************/

/* source,dest[0] is 55:48 */
/* source,dest[1] is 47:24 */
/* source,dest[2] is 23:00 */

static uint16_t dsp_abs56(uint32_t *dest)
{
	uint32_t zerodest[3];
	uint16_t newsr;

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

static uint16_t dsp_asl56(uint32_t *dest)
{
	uint16_t overflow, carry;

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

static uint16_t dsp_asr56(uint32_t *dest)
{
	uint16_t carry;

	/* Shift right dest 1 bit: D>>=1 */

	carry = dest[2] & 1;

	dest[2] >>= 1;
	dest[2] |= (dest[1] & 1)<<23;

	dest[1] >>= 1;
	dest[1] |= (dest[0] & 1)<<23;

	dest[0] >>= 1;
	dest[0] |= (dest[0] & (1<<6))<<1;

	return (carry<<DSP_SR_C);
}

static uint16_t dsp_add56(uint32_t *source, uint32_t *dest)
{
	uint16_t overflow, carry, flg_s, flg_d, flg_r;

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

static uint16_t dsp_sub56(uint32_t *source, uint32_t *dest)
{
	uint16_t overflow, carry, flg_s, flg_d, flg_r, dest_save;

	dest_save = dest[0];

	/* Subtract source from dest: D = D-S */
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

static void dsp_mul56(uint32_t source1, uint32_t source2, uint32_t *dest, uint8_t signe)
{
	uint32_t part[4], zerodest[3], value;

	/* Multiply: D = S1*S2 */
	if (source1 & (1<<23)) {
		signe ^= 1;
		source1 = (1<<24) - source1;
	}
	if (source2 & (1<<23)) {
		signe ^= 1;
		source2 = (1<<24) - source2;
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

static void dsp_rnd56(uint32_t *dest)
{
	uint32_t rnd_const[3];

	rnd_const[0] = 0;

	/* Scaling mode S0 */
	if (dsp_core.registers[DSP_REG_SR] & (1<<DSP_SR_S0)) {
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
	else if (dsp_core.registers[DSP_REG_SR] & (1<<DSP_SR_S1)) {
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

static void dsp_abs_a(void)
{
	uint32_t dest[3], overflowed;

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];

	overflowed = ((dest[2]==0) && (dest[1]==0) && (dest[0]==0x80));

	dsp_abs56(dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= (overflowed<<DSP_SR_L)|(overflowed<<DSP_SR_V);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);
}

static void dsp_abs_b(void)
{
	uint32_t dest[3], overflowed;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];

	overflowed = ((dest[2]==0) && (dest[1]==0) && (dest[0]==0x80));

	dsp_abs56(dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= (overflowed<<DSP_SR_L)|(overflowed<<DSP_SR_V);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);
}

static void dsp_adc_x_a(void)
{
	uint32_t source[3], dest[3], curcarry;
	uint16_t newsr;

	curcarry = (dsp_core.registers[DSP_REG_SR]>>DSP_SR_C) & 1;

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];

	source[2] = dsp_core.registers[DSP_REG_X0];
	source[1] = dsp_core.registers[DSP_REG_X1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_add56(source, dest);

	if (curcarry) {
		source[0]=0; source[1]=0; source[2]=1;
		newsr |= dsp_add56(source, dest);
	}

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_adc_x_b(void)
{
	uint32_t source[3], dest[3], curcarry;
	uint16_t newsr;

	curcarry = (dsp_core.registers[DSP_REG_SR]>>DSP_SR_C) & 1;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];

	source[2] = dsp_core.registers[DSP_REG_X0];
	source[1] = dsp_core.registers[DSP_REG_X1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_add56(source, dest);

	if (curcarry) {
		source[0]=0; source[1]=0; source[2]=1;
		newsr |= dsp_add56(source, dest);
	}

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_adc_y_a(void)
{
	uint32_t source[3], dest[3], curcarry;
	uint16_t newsr;

	curcarry = (dsp_core.registers[DSP_REG_SR]>>DSP_SR_C) & 1;

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];

	source[2] = dsp_core.registers[DSP_REG_Y0];
	source[1] = dsp_core.registers[DSP_REG_Y1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_add56(source, dest);

	if (curcarry) {
		source[0]=0; source[1]=0; source[2]=1;
		newsr |= dsp_add56(source, dest);
	}

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_adc_y_b(void)
{
	uint32_t source[3], dest[3], curcarry;
	uint16_t newsr;

	curcarry = (dsp_core.registers[DSP_REG_SR]>>DSP_SR_C) & 1;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];

	source[2] = dsp_core.registers[DSP_REG_Y0];
	source[1] = dsp_core.registers[DSP_REG_Y1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_add56(source, dest);

	if (curcarry) {
		source[0]=0; source[1]=0; source[2]=1;
		newsr |= dsp_add56(source, dest);
	}

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_add_b_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];

	source[0] = dsp_core.registers[DSP_REG_B2];
	source[1] = dsp_core.registers[DSP_REG_B1];
	source[2] = dsp_core.registers[DSP_REG_B0];

	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_add_a_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];

	source[0] = dsp_core.registers[DSP_REG_A2];
	source[1] = dsp_core.registers[DSP_REG_A1];
	source[2] = dsp_core.registers[DSP_REG_A0];

	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_add_x_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];

	source[1] = dsp_core.registers[DSP_REG_X1];
	source[2] = dsp_core.registers[DSP_REG_X0];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_add_x_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];

	source[1] = dsp_core.registers[DSP_REG_X1];
	source[2] = dsp_core.registers[DSP_REG_X0];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_add_y_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];

	source[1] = dsp_core.registers[DSP_REG_Y1];
	source[2] = dsp_core.registers[DSP_REG_Y0];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_add_y_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];

	source[1] = dsp_core.registers[DSP_REG_Y1];
	source[2] = dsp_core.registers[DSP_REG_Y0];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_add_x0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_X0];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_add_x0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_X0];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_add_y0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_Y0];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_add_y0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_Y0];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_add_x1_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_X1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_add_x1_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_X1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_add_y1_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_Y1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_add_y1_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_Y1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_addl_b_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_asl56(dest);

	source[0] = dsp_core.registers[DSP_REG_B2];
	source[1] = dsp_core.registers[DSP_REG_B1];
	source[2] = dsp_core.registers[DSP_REG_B0];
	newsr |= dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_addl_a_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_asl56(dest);

	source[0] = dsp_core.registers[DSP_REG_A2];
	source[1] = dsp_core.registers[DSP_REG_A1];
	source[2] = dsp_core.registers[DSP_REG_A0];
	newsr |= dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_addr_b_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_asr56(dest);

	source[0] = dsp_core.registers[DSP_REG_B2];
	source[1] = dsp_core.registers[DSP_REG_B1];
	source[2] = dsp_core.registers[DSP_REG_B0];
	newsr |= dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_addr_a_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_asr56(dest);

	source[0] = dsp_core.registers[DSP_REG_A2];
	source[1] = dsp_core.registers[DSP_REG_A1];
	source[2] = dsp_core.registers[DSP_REG_A0];
	newsr |= dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_and_x0_a(void)
{
	dsp_core.registers[DSP_REG_A1] &= dsp_core.registers[DSP_REG_X0];

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_A1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_A1]==0)<<DSP_SR_Z;
}

static void dsp_and_x0_b(void)
{
	dsp_core.registers[DSP_REG_B1] &= dsp_core.registers[DSP_REG_X0];

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_B1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_B1]==0)<<DSP_SR_Z;
}

static void dsp_and_y0_a(void)
{
	dsp_core.registers[DSP_REG_A1] &= dsp_core.registers[DSP_REG_Y0];

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_A1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_A1]==0)<<DSP_SR_Z;
}

static void dsp_and_y0_b(void)
{
	dsp_core.registers[DSP_REG_B1] &= dsp_core.registers[DSP_REG_Y0];

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_B1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_B1]==0)<<DSP_SR_Z;
}

static void dsp_and_x1_a(void)
{
	dsp_core.registers[DSP_REG_A1] &= dsp_core.registers[DSP_REG_X1];

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_A1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_A1]==0)<<DSP_SR_Z;
}

static void dsp_and_x1_b(void)
{
	dsp_core.registers[DSP_REG_B1] &= dsp_core.registers[DSP_REG_X1];

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_B1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_B1]==0)<<DSP_SR_Z;
}

static void dsp_and_y1_a(void)
{
	dsp_core.registers[DSP_REG_A1] &= dsp_core.registers[DSP_REG_Y1];

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_A1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_A1]==0)<<DSP_SR_Z;
}

static void dsp_and_y1_b(void)
{
	dsp_core.registers[DSP_REG_B1] &= dsp_core.registers[DSP_REG_Y1];

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_B1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_B1]==0)<<DSP_SR_Z;
}

static void dsp_asl_a(void)
{
	uint32_t dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];

	newsr = dsp_asl56(dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_C)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= newsr;

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);
}

static void dsp_asl_b(void)
{
	uint32_t dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];

	newsr = dsp_asl56(dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_C)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= newsr;

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);
}

static void dsp_asr_a(void)
{
	uint32_t dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];

	newsr = dsp_asr56(dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_C)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= newsr;

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);
}

static void dsp_asr_b(void)
{
	uint32_t dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];

	newsr = dsp_asr56(dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_C)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= newsr;

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);
}

static void dsp_clr_a(void)
{
	dsp_core.registers[DSP_REG_A2] = 0;
	dsp_core.registers[DSP_REG_A1] = 0;
	dsp_core.registers[DSP_REG_A0] = 0;

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_E)|(1<<DSP_SR_N)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= (1<<DSP_SR_U)|(1<<DSP_SR_Z);
}

static void dsp_clr_b(void)
{
	dsp_core.registers[DSP_REG_B2] = 0;
	dsp_core.registers[DSP_REG_B1] = 0;
	dsp_core.registers[DSP_REG_B0] = 0;

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_E)|(1<<DSP_SR_N)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= (1<<DSP_SR_U)|(1<<DSP_SR_Z);
}

static void dsp_cmp_b_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];

	source[0] = dsp_core.registers[DSP_REG_B2];
	source[1] = dsp_core.registers[DSP_REG_B1];
	source[2] = dsp_core.registers[DSP_REG_B0];

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_cmp_a_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];

	source[0] = dsp_core.registers[DSP_REG_A2];
	source[1] = dsp_core.registers[DSP_REG_A1];
	source[2] = dsp_core.registers[DSP_REG_A0];

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_cmp_x0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_A0];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[0] = dsp_core.registers[DSP_REG_A2];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_X0];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_cmp_x0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_X0];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_cmp_y0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_A0];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[0] = dsp_core.registers[DSP_REG_A2];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_Y0];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_cmp_y0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_Y0];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}
static void dsp_cmp_x1_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_A0];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[0] = dsp_core.registers[DSP_REG_A2];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_X1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_cmp_x1_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_X1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_cmp_y1_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_A0];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[0] = dsp_core.registers[DSP_REG_A2];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_Y1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_cmp_y1_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_Y1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_cmpm_b_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	dsp_abs56(dest);

	source[0] = dsp_core.registers[DSP_REG_B2];
	source[1] = dsp_core.registers[DSP_REG_B1];
	source[2] = dsp_core.registers[DSP_REG_B0];
	dsp_abs56(source);

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_cmpm_a_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	dsp_abs56(dest);

	source[0] = dsp_core.registers[DSP_REG_A2];
	source[1] = dsp_core.registers[DSP_REG_A1];
	source[2] = dsp_core.registers[DSP_REG_A0];
	dsp_abs56(source);

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_cmpm_x0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_A0];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[0] = dsp_core.registers[DSP_REG_A2];
	dsp_abs56(dest);

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_X0];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;
	dsp_abs56(source);

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_cmpm_x0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	dsp_abs56(dest);

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_X0];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;
	dsp_abs56(source);

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_cmpm_y0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_A0];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[0] = dsp_core.registers[DSP_REG_A2];
	dsp_abs56(dest);

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_Y0];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;
	dsp_abs56(source);

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_cmpm_y0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	dsp_abs56(dest);

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_Y0];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;
	dsp_abs56(source);

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_cmpm_x1_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_A0];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[0] = dsp_core.registers[DSP_REG_A2];
	dsp_abs56(dest);

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_X1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;
	dsp_abs56(source);

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_cmpm_x1_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	dsp_abs56(dest);

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_X1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;
	dsp_abs56(source);

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_cmpm_y1_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_A0];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[0] = dsp_core.registers[DSP_REG_A2];
	dsp_abs56(dest);

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_Y1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;
	dsp_abs56(source);

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_cmpm_y1_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	dsp_abs56(dest);

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_Y1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;
	dsp_abs56(source);

	newsr = dsp_sub56(source, dest);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_eor_x0_a(void)
{
	dsp_core.registers[DSP_REG_A1] ^= dsp_core.registers[DSP_REG_X0];
	dsp_core.registers[DSP_REG_A1] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_A1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_A1]==0)<<DSP_SR_Z;
}

static void dsp_eor_x0_b(void)
{
	dsp_core.registers[DSP_REG_B1] ^= dsp_core.registers[DSP_REG_X0];
	dsp_core.registers[DSP_REG_B1] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_B1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_B1]==0)<<DSP_SR_Z;
}

static void dsp_eor_y0_a(void)
{
	dsp_core.registers[DSP_REG_A1] ^= dsp_core.registers[DSP_REG_Y0];
	dsp_core.registers[DSP_REG_A1] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_A1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_A1]==0)<<DSP_SR_Z;
}

static void dsp_eor_y0_b(void)
{
	dsp_core.registers[DSP_REG_B1] ^= dsp_core.registers[DSP_REG_Y0];
	dsp_core.registers[DSP_REG_B1] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_B1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_B1]==0)<<DSP_SR_Z;
}

static void dsp_eor_x1_a(void)
{
	dsp_core.registers[DSP_REG_A1] ^= dsp_core.registers[DSP_REG_X1];
	dsp_core.registers[DSP_REG_A1] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_A1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_A1]==0)<<DSP_SR_Z;
}

static void dsp_eor_x1_b(void)
{
	dsp_core.registers[DSP_REG_B1] ^= dsp_core.registers[DSP_REG_X1];
	dsp_core.registers[DSP_REG_B1] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_B1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_B1]==0)<<DSP_SR_Z;
}

static void dsp_eor_y1_a(void)
{
	dsp_core.registers[DSP_REG_A1] ^= dsp_core.registers[DSP_REG_Y1];
	dsp_core.registers[DSP_REG_A1] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_A1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_A1]==0)<<DSP_SR_Z;
}

static void dsp_eor_y1_b(void)
{
	dsp_core.registers[DSP_REG_B1] ^= dsp_core.registers[DSP_REG_Y1];
	dsp_core.registers[DSP_REG_B1] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_B1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_B1]==0)<<DSP_SR_Z;
}

static void dsp_lsl_a(void)
{
	uint32_t newcarry = (dsp_core.registers[DSP_REG_A1]>>23) & 1;

	dsp_core.registers[DSP_REG_A1] <<= 1;
	dsp_core.registers[DSP_REG_A1] &= BITMASK(24);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_C)|(1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= newcarry;
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_A1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_A1]==0)<<DSP_SR_Z;
}

static void dsp_lsl_b(void)
{
	uint32_t newcarry = (dsp_core.registers[DSP_REG_B1]>>23) & 1;

	dsp_core.registers[DSP_REG_B1] <<= 1;
	dsp_core.registers[DSP_REG_B1] &= BITMASK(24);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_C)|(1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= newcarry;
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_B1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_B1]==0)<<DSP_SR_Z;
}

static void dsp_lsr_a(void)
{
	uint32_t newcarry = dsp_core.registers[DSP_REG_A1] & 1;
	dsp_core.registers[DSP_REG_A1] >>= 1;

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_C)|(1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= newcarry;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_A1]==0)<<DSP_SR_Z;
}

static void dsp_lsr_b(void)
{
	uint32_t newcarry = dsp_core.registers[DSP_REG_B1] & 1;
	dsp_core.registers[DSP_REG_B1] >>= 1;

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_C)|(1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= newcarry;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_B1]==0)<<DSP_SR_Z;
}

static void dsp_mac_p_x0_x0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_m_x0_x0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}
static void dsp_mac_p_x0_x0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_m_x0_x0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_p_y0_y0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_m_y0_y0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}
static void dsp_mac_p_y0_y0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_m_y0_y0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_p_x1_x0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_m_x1_x0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_p_x1_x0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_m_x1_x0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_p_y1_y0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_m_y1_y0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_p_y1_y0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_m_y1_y0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_p_x0_y1_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_Y1], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_m_x0_y1_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_Y1], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_p_x0_y1_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_Y1], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_m_x0_y1_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_Y1], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_p_y0_x0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_m_y0_x0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_p_y0_x0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_m_y0_x0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_p_x1_y0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_m_x1_y0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_p_x1_y0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_m_x1_y0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_p_y1_x1_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_X1], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_m_y1_x1_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_X1], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_p_y1_x1_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_X1], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_mac_m_y1_x1_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_X1], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_p_x0_x0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_m_x0_x0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}
static void dsp_macr_p_x0_x0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_m_x0_x0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_p_y0_y0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_m_y0_y0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}
static void dsp_macr_p_y0_y0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_m_y0_y0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_p_x1_x0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_m_x1_x0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_p_x1_x0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_m_x1_x0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_p_y1_y0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_m_y1_y0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_p_y1_y0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_m_y1_y0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_p_x0_y1_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_Y1], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_m_x0_y1_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_Y1], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_p_x0_y1_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_Y1], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_m_x0_y1_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_Y1], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_p_y0_x0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_m_y0_x0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_p_y0_x0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_m_y0_x0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_p_x1_y0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_m_x1_y0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_p_x1_y0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_m_x1_y0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_p_y1_x1_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_X1], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_m_y1_x1_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_X1], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_p_y1_x1_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_X1], source, SIGN_PLUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}

static void dsp_macr_m_y1_x1_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_X1], source, SIGN_MINUS);

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_add56(source, dest);

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= newsr & 0xfe;
}


static void dsp_move(void)
{
	/*	move instruction inside alu opcodes
		taken care of by parallel move dispatcher */
}

static void dsp_mpy_p_x0_x0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_m_x0_x0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_p_x0_x0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_m_x0_x0_b(void)
{
	uint32_t source[3];


	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_p_y0_y0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_m_y0_y0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_p_y0_y0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_m_y0_y0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_p_x1_x0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_m_x1_x0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_p_x1_x0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_m_x1_x0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_p_y1_y0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_m_y1_y0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_p_y1_y0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_m_y1_y0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_p_x0_y1_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_Y1], source, SIGN_PLUS);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_m_x0_y1_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_Y1], source, SIGN_MINUS);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_p_x0_y1_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_Y1], source, SIGN_PLUS);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_m_x0_y1_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_Y1], source, SIGN_MINUS);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_p_y0_x0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_m_y0_x0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_p_y0_x0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_m_y0_x0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_p_x1_y0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_m_x1_y0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_p_x1_y0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_m_x1_y0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_p_y1_x1_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_X1], source, SIGN_PLUS);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_m_y1_x1_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_X1], source, SIGN_MINUS);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_p_y1_x1_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_X1], source, SIGN_PLUS);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpy_m_y1_x1_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_X1], source, SIGN_MINUS);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_p_x0_x0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_m_x0_x0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_p_x0_x0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_m_x0_x0_b(void)
{
	uint32_t source[3];


	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_p_y0_y0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_m_y0_y0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_p_y0_y0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_m_y0_y0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_p_x1_x0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_m_x1_x0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_p_x1_x0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_m_x1_x0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_p_y1_y0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_m_y1_y0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_p_y1_y0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_m_y1_y0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_p_x0_y1_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_Y1], source, SIGN_PLUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_m_x0_y1_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_Y1], source, SIGN_MINUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_p_x0_y1_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_Y1], source, SIGN_PLUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_m_x0_y1_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_Y1], source, SIGN_MINUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_p_y0_x0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_m_y0_x0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_p_y0_x0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_X0], source, SIGN_PLUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_m_y0_x0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y0], dsp_core.registers[DSP_REG_X0], source, SIGN_MINUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_p_x1_y0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_m_x1_y0_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_p_x1_y0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_Y0], source, SIGN_PLUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_m_x1_y0_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_Y0], source, SIGN_MINUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_p_y1_x1_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_X1], source, SIGN_PLUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_m_y1_x1_a(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_X1], source, SIGN_MINUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_A2] = source[0];
	dsp_core.registers[DSP_REG_A1] = source[1];
	dsp_core.registers[DSP_REG_A0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_p_y1_x1_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_X1], source, SIGN_PLUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_mpyr_m_y1_x1_b(void)
{
	uint32_t source[3];

	dsp_mul56(dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_X1], source, SIGN_MINUS);
	dsp_rnd56(source);

	dsp_core.registers[DSP_REG_B2] = source[0];
	dsp_core.registers[DSP_REG_B1] = source[1];
	dsp_core.registers[DSP_REG_B0] = source[2];

	dsp_ccr_update_e_u_n_z(source[0], source[1], source[2]);
	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_neg_a(void)
{
	uint32_t source[3], dest[3], overflowed;

	source[0] = dsp_core.registers[DSP_REG_A2];
	source[1] = dsp_core.registers[DSP_REG_A1];
	source[2] = dsp_core.registers[DSP_REG_A0];

	overflowed = ((source[2]==0) && (source[1]==0) && (source[0]==0x80));

	dest[0] = dest[1] = dest[2] = 0;

	dsp_sub56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= (overflowed<<DSP_SR_L)|(overflowed<<DSP_SR_V);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);
}

static void dsp_neg_b(void)
{
	uint32_t source[3], dest[3], overflowed;

	source[0] = dsp_core.registers[DSP_REG_B2];
	source[1] = dsp_core.registers[DSP_REG_B1];
	source[2] = dsp_core.registers[DSP_REG_B0];

	overflowed = ((source[2]==0) && (source[1]==0) && (source[0]==0x80));

	dest[0] = dest[1] = dest[2] = 0;

	dsp_sub56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
	dsp_core.registers[DSP_REG_SR] |= (overflowed<<DSP_SR_L)|(overflowed<<DSP_SR_V);

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);
}

static void dsp_nop(void)
{
}

static void dsp_not_a(void)
{
	dsp_core.registers[DSP_REG_A1] = ~dsp_core.registers[DSP_REG_A1];
	dsp_core.registers[DSP_REG_A1] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_A1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_A1]==0)<<DSP_SR_Z;
}

static void dsp_not_b(void)
{
	dsp_core.registers[DSP_REG_B1] = ~dsp_core.registers[DSP_REG_B1];
	dsp_core.registers[DSP_REG_B1] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_B1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_B1]==0)<<DSP_SR_Z;
}

static void dsp_or_x0_a(void)
{
	dsp_core.registers[DSP_REG_A1] |= dsp_core.registers[DSP_REG_X0];
	dsp_core.registers[DSP_REG_A1] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_A1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_A1]==0)<<DSP_SR_Z;
}

static void dsp_or_x0_b(void)
{
	dsp_core.registers[DSP_REG_B1] |= dsp_core.registers[DSP_REG_X0];
	dsp_core.registers[DSP_REG_B1] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_B1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_B1]==0)<<DSP_SR_Z;
}

static void dsp_or_y0_a(void)
{
	dsp_core.registers[DSP_REG_A1] |= dsp_core.registers[DSP_REG_Y0];
	dsp_core.registers[DSP_REG_A1] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_A1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_A1]==0)<<DSP_SR_Z;
}

static void dsp_or_y0_b(void)
{
	dsp_core.registers[DSP_REG_B1] |= dsp_core.registers[DSP_REG_Y0];
	dsp_core.registers[DSP_REG_B1] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_B1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_B1]==0)<<DSP_SR_Z;
}

static void dsp_or_x1_a(void)
{
	dsp_core.registers[DSP_REG_A1] |= dsp_core.registers[DSP_REG_X1];
	dsp_core.registers[DSP_REG_A1] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_A1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_A1]==0)<<DSP_SR_Z;
}

static void dsp_or_x1_b(void)
{
	dsp_core.registers[DSP_REG_B1] |= dsp_core.registers[DSP_REG_X1];
	dsp_core.registers[DSP_REG_B1] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_B1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_B1]==0)<<DSP_SR_Z;
}

static void dsp_or_y1_a(void)
{
	dsp_core.registers[DSP_REG_A1] |= dsp_core.registers[DSP_REG_Y1];
	dsp_core.registers[DSP_REG_A1] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_A1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_A1]==0)<<DSP_SR_Z;
}

static void dsp_or_y1_b(void)
{
	dsp_core.registers[DSP_REG_B1] |= dsp_core.registers[DSP_REG_Y1];
	dsp_core.registers[DSP_REG_B1] &= BITMASK(24); /* FIXME: useless ? */

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_B1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_B1]==0)<<DSP_SR_Z;
}

static void dsp_rnd_a(void)
{
	uint32_t dest[3];

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);
}

static void dsp_rnd_b(void)
{
	uint32_t dest[3];

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];

	dsp_rnd56(dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);
}

static void dsp_rol_a(void)
{
	uint32_t newcarry;

	newcarry = (dsp_core.registers[DSP_REG_A1]>>23) & 1;

	dsp_core.registers[DSP_REG_A1] <<= 1;
	dsp_core.registers[DSP_REG_A1] |= dsp_core.registers[DSP_REG_SR] & 1;
	dsp_core.registers[DSP_REG_A1] &= BITMASK(24);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_C)|(1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= newcarry;
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_A1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_A1]==0)<<DSP_SR_Z;
}

static void dsp_rol_b(void)
{
	uint32_t newcarry;

	newcarry = (dsp_core.registers[DSP_REG_B1]>>23) & 1;

	dsp_core.registers[DSP_REG_B1] <<= 1;
	dsp_core.registers[DSP_REG_B1] |= dsp_core.registers[DSP_REG_SR] & 1;
	dsp_core.registers[DSP_REG_B1] &= BITMASK(24);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_C)|(1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= newcarry;
	dsp_core.registers[DSP_REG_SR] |= ((dsp_core.registers[DSP_REG_B1]>>23) & 1)<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_B1]==0)<<DSP_SR_Z;
}

static void dsp_ror_a(void)
{
	uint32_t newcarry;

	newcarry = dsp_core.registers[DSP_REG_A1] & 1;

	dsp_core.registers[DSP_REG_A1] >>= 1;
	dsp_core.registers[DSP_REG_A1] |= (dsp_core.registers[DSP_REG_SR] & 1)<<23;

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_C)|(1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= newcarry;
	dsp_core.registers[DSP_REG_SR] |= newcarry<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_A1]==0)<<DSP_SR_Z;
}

static void dsp_ror_b(void)
{
	uint32_t newcarry;

	newcarry = dsp_core.registers[DSP_REG_B1] & 1;

	dsp_core.registers[DSP_REG_B1] >>= 1;
	dsp_core.registers[DSP_REG_B1] |= (dsp_core.registers[DSP_REG_SR] & 1)<<23;

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_C)|(1<<DSP_SR_N)|(1<<DSP_SR_Z)|(1<<DSP_SR_V));
	dsp_core.registers[DSP_REG_SR] |= newcarry;
	dsp_core.registers[DSP_REG_SR] |= newcarry<<DSP_SR_N;
	dsp_core.registers[DSP_REG_SR] |= (dsp_core.registers[DSP_REG_B1]==0)<<DSP_SR_Z;
}

static void dsp_sbc_x_a(void)
{
	uint32_t source[3], dest[3], curcarry;
	uint16_t newsr;

	curcarry = (dsp_core.registers[DSP_REG_SR]>>(DSP_SR_C)) & 1;

	dest[2] = dsp_core.registers[DSP_REG_A0];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[0] = dsp_core.registers[DSP_REG_A2];

	source[2] = dsp_core.registers[DSP_REG_X0];
	source[1] = dsp_core.registers[DSP_REG_X1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	if (curcarry) {
		source[0]=0; source[1]=0; source[2]=1;
		newsr |= dsp_sub56(source, dest);
	}

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_sbc_x_b(void)
{
	uint32_t source[3], dest[3], curcarry;
	uint16_t newsr;

	curcarry = (dsp_core.registers[DSP_REG_SR]>>(DSP_SR_C)) & 1;

	dest[2] = dsp_core.registers[DSP_REG_B0];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[0] = dsp_core.registers[DSP_REG_B2];

	source[2] = dsp_core.registers[DSP_REG_X0];
	source[1] = dsp_core.registers[DSP_REG_X1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	if (curcarry) {
		source[0]=0; source[1]=0; source[2]=1;
		newsr |= dsp_sub56(source, dest);
	}

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_sbc_y_a(void)
{
	uint32_t source[3], dest[3], curcarry;
	uint16_t newsr;

	curcarry = (dsp_core.registers[DSP_REG_SR]>>(DSP_SR_C)) & 1;

	dest[2] = dsp_core.registers[DSP_REG_A0];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[0] = dsp_core.registers[DSP_REG_A2];

	source[2] = dsp_core.registers[DSP_REG_Y0];
	source[1] = dsp_core.registers[DSP_REG_Y1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	if (curcarry) {
		source[0]=0; source[1]=0; source[2]=1;
		newsr |= dsp_sub56(source, dest);
	}

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_sbc_y_b(void)
{
	uint32_t source[3], dest[3], curcarry;
	uint16_t newsr;

	curcarry = (dsp_core.registers[DSP_REG_SR]>>(DSP_SR_C)) & 1;

	dest[2] = dsp_core.registers[DSP_REG_B0];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[0] = dsp_core.registers[DSP_REG_B2];

	source[2] = dsp_core.registers[DSP_REG_Y0];
	source[1] = dsp_core.registers[DSP_REG_Y1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	if (curcarry) {
		source[0]=0; source[1]=0; source[2]=1;
		newsr |= dsp_sub56(source, dest);
	}

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_sub_b_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_A0];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[0] = dsp_core.registers[DSP_REG_A2];

	source[2] = dsp_core.registers[DSP_REG_B0];
	source[1] = dsp_core.registers[DSP_REG_B1];
	source[0] = dsp_core.registers[DSP_REG_B2];

	newsr = dsp_sub56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_sub_a_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_B0];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[0] = dsp_core.registers[DSP_REG_B2];

	source[2] = dsp_core.registers[DSP_REG_A0];
	source[1] = dsp_core.registers[DSP_REG_A1];
	source[0] = dsp_core.registers[DSP_REG_A2];

	newsr = dsp_sub56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_sub_x_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_A0];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[0] = dsp_core.registers[DSP_REG_A2];

	source[2] = dsp_core.registers[DSP_REG_X0];
	source[1] = dsp_core.registers[DSP_REG_X1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_sub_x_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_B0];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[0] = dsp_core.registers[DSP_REG_B2];

	source[2] = dsp_core.registers[DSP_REG_X0];
	source[1] = dsp_core.registers[DSP_REG_X1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_sub_y_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_A0];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[0] = dsp_core.registers[DSP_REG_A2];

	source[2] = dsp_core.registers[DSP_REG_Y0];
	source[1] = dsp_core.registers[DSP_REG_Y1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_sub_y_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_B0];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[0] = dsp_core.registers[DSP_REG_B2];

	source[2] = dsp_core.registers[DSP_REG_Y0];
	source[1] = dsp_core.registers[DSP_REG_Y1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_sub_x0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_A0];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[0] = dsp_core.registers[DSP_REG_A2];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_X0];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_sub_x0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_B0];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[0] = dsp_core.registers[DSP_REG_B2];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_X0];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_sub_y0_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_A0];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[0] = dsp_core.registers[DSP_REG_A2];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_Y0];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_sub_y0_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_B0];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[0] = dsp_core.registers[DSP_REG_B2];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_Y0];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_sub_x1_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_A0];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[0] = dsp_core.registers[DSP_REG_A2];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_X1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_sub_x1_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_B0];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[0] = dsp_core.registers[DSP_REG_B2];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_X1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_sub_y1_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_A0];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[0] = dsp_core.registers[DSP_REG_A2];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_Y1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_sub_y1_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[2] = dsp_core.registers[DSP_REG_B0];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[0] = dsp_core.registers[DSP_REG_B2];

	source[2] = 0;
	source[1] = dsp_core.registers[DSP_REG_Y1];
	source[0] = source[1] & (1<<23) ? 0xff : 0x0;

	newsr = dsp_sub56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_subl_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];
	newsr = dsp_asl56(dest);

	source[0] = dsp_core.registers[DSP_REG_B2];
	source[1] = dsp_core.registers[DSP_REG_B1];
	source[2] = dsp_core.registers[DSP_REG_B0];
	newsr |= dsp_sub56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_subl_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];
	newsr = dsp_asl56(dest);

	source[0] = dsp_core.registers[DSP_REG_A2];
	source[1] = dsp_core.registers[DSP_REG_A1];
	source[2] = dsp_core.registers[DSP_REG_A0];
	newsr |= dsp_sub56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_subr_a(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_A2];
	dest[1] = dsp_core.registers[DSP_REG_A1];
	dest[2] = dsp_core.registers[DSP_REG_A0];

	newsr = dsp_asr56(dest);

	source[0] = dsp_core.registers[DSP_REG_B2];
	source[1] = dsp_core.registers[DSP_REG_B1];
	source[2] = dsp_core.registers[DSP_REG_B0];

	newsr |= dsp_sub56(source, dest);

	dsp_core.registers[DSP_REG_A2] = dest[0];
	dsp_core.registers[DSP_REG_A1] = dest[1];
	dsp_core.registers[DSP_REG_A0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_subr_b(void)
{
	uint32_t source[3], dest[3];
	uint16_t newsr;

	dest[0] = dsp_core.registers[DSP_REG_B2];
	dest[1] = dsp_core.registers[DSP_REG_B1];
	dest[2] = dsp_core.registers[DSP_REG_B0];

	newsr = dsp_asr56(dest);

	source[0] = dsp_core.registers[DSP_REG_A2];
	source[1] = dsp_core.registers[DSP_REG_A1];
	source[2] = dsp_core.registers[DSP_REG_A0];

	newsr |= dsp_sub56(source, dest);

	dsp_core.registers[DSP_REG_B2] = dest[0];
	dsp_core.registers[DSP_REG_B1] = dest[1];
	dsp_core.registers[DSP_REG_B0] = dest[2];

	dsp_ccr_update_e_u_n_z(dest[0], dest[1], dest[2]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-((1<<DSP_SR_V)|(1<<DSP_SR_C));
	dsp_core.registers[DSP_REG_SR] |= newsr;
}

static void dsp_tfr_b_a(void)
{
	dsp_core.registers[DSP_REG_A0] = dsp_core.registers[DSP_REG_B0];
	dsp_core.registers[DSP_REG_A1] = dsp_core.registers[DSP_REG_B1];
	dsp_core.registers[DSP_REG_A2] = dsp_core.registers[DSP_REG_B2];
}

static void dsp_tfr_a_b(void)
{
	dsp_core.registers[DSP_REG_B0] = dsp_core.registers[DSP_REG_A0];
	dsp_core.registers[DSP_REG_B1] = dsp_core.registers[DSP_REG_A1];
	dsp_core.registers[DSP_REG_B2] = dsp_core.registers[DSP_REG_A2];
}

static void dsp_tfr_x0_a(void)
{
	dsp_write_reg(DSP_REG_A, dsp_core.registers[DSP_REG_X0]);
}

static void dsp_tfr_x0_b(void)
{
	dsp_write_reg(DSP_REG_B, dsp_core.registers[DSP_REG_X0]);
}

static void dsp_tfr_y0_a(void)
{
	dsp_write_reg(DSP_REG_A, dsp_core.registers[DSP_REG_Y0]);
}

static void dsp_tfr_y0_b(void)
{
	dsp_write_reg(DSP_REG_B, dsp_core.registers[DSP_REG_Y0]);
}

static void dsp_tfr_x1_a(void)
{
	dsp_write_reg(DSP_REG_A, dsp_core.registers[DSP_REG_X1]);
}

static void dsp_tfr_x1_b(void)
{
	dsp_write_reg(DSP_REG_B, dsp_core.registers[DSP_REG_X1]);
}

static void dsp_tfr_y1_a(void)
{
	dsp_write_reg(DSP_REG_A, dsp_core.registers[DSP_REG_Y1]);
}

static void dsp_tfr_y1_b(void)
{
	dsp_write_reg(DSP_REG_B, dsp_core.registers[DSP_REG_Y1]);
}

static void dsp_tst_a(void)
{
	dsp_ccr_update_e_u_n_z(	dsp_core.registers[DSP_REG_A2],
				dsp_core.registers[DSP_REG_A1],
				dsp_core.registers[DSP_REG_A0]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

static void dsp_tst_b(void)
{
	dsp_ccr_update_e_u_n_z(	dsp_core.registers[DSP_REG_B2],
				dsp_core.registers[DSP_REG_B1],
				dsp_core.registers[DSP_REG_B0]);

	dsp_core.registers[DSP_REG_SR] &= BITMASK(16)-(1<<DSP_SR_V);
}

/*
vim:ts=4:sw=4:
*/
