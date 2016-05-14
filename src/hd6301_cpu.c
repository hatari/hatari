/*
  Hatari - hd6301_cpu.c
  Copyright Laurent Sallafranque 2009

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  hd6301_cpu.c - this is the cpu core emulation for hd 6301 processor
*/

#include "main.h"
#include "hd6301_cpu.h"


/**********************************
 *	Defines
 **********************************/
#define HD6301_DISASM 		1
#define HD6301_DISPLAY_REGS	1

/* HD6301 Disasm and debug code */
#define HD6301_DISASM_UNDEFINED		0
#define HD6301_DISASM_NONE		1
#define HD6301_DISASM_MEMORY8		2
#define HD6301_DISASM_MEMORY16		3
#define HD6301_DISASM_XIM		4

/* CCR bits for clearing */

#define HD6301_CLR_HNZVC	hd6301_reg_CCR &= 0xd0
#define HD6301_CLR_HNZC		hd6301_reg_CCR &= 0xd2
#define HD6301_CLR_NZVC		hd6301_reg_CCR &= 0xf0
#define HD6301_CLR_NZV		hd6301_reg_CCR &= 0xf1
#define HD6301_CLR_NZC		hd6301_reg_CCR &= 0xf2
#define HD6301_CLR_ZC		hd6301_reg_CCR &= 0xfa
#define HD6301_CLR_I		hd6301_reg_CCR &= 0xef
#define HD6301_CLR_Z		hd6301_reg_CCR &= 0xfb
#define HD6301_CLR_V		hd6301_reg_CCR &= 0xfd
#define HD6301_CLR_C		hd6301_reg_CCR &= 0xfe


/**********************************
 *	macros for CCR processing
 *	adapted from mame project
 **********************************/
#define HD6301_SET_Z8(a)	hd6301_reg_CCR |= (((Uint8)(a) == 0) << 1)
#define HD6301_SET_Z16(a)	hd6301_reg_CCR |= (((Uint16)(a) == 0) << 1)
#define HD6301_SET_N8(a)	hd6301_reg_CCR |= (((a) & 0x80) >> 4)
#define HD6301_SET_N16(a)	hd6301_reg_CCR |= (((a) & 0x8000) >> 12)
#define HD6301_SET_C8(a)	hd6301_reg_CCR |= (((a) & 0x100) >> 8)
#define HD6301_SET_C16(a)	hd6301_reg_CCR |= (((a) & 0x10000) >> 16)
#define HD6301_SET_V8(a,b,r)	hd6301_reg_CCR |= ((((a)^(b)^(r)^((r)>>1)) & 0x80) >> 6)
#define HD6301_SET_V16(a,b,r)	hd6301_reg_CCR |= ((((a)^(b)^(r)^((r)>>1)) & 0x8000) >> 14)
#define HD6301_SET_H(a,b,r)	hd6301_reg_CCR |= ((((a)^(b)^(r)) & 0x10) << 1)


#define HD6301_SET_NZ8(a)		{HD6301_SET_N8(a);HD6301_SET_Z8(a);}
#define HD6301_SET_NZ16(a)		{HD6301_SET_N16(a);HD6301_SET_Z16(a);}
#define HD6301_SET_FLAGS8(a,b,r)	{HD6301_SET_N8(r);HD6301_SET_Z8(r);HD6301_SET_V8(a,b,r);HD6301_SET_C8(r);}
#define HD6301_SET_FLAGS16(a,b,r)	{HD6301_SET_N16(r);HD6301_SET_Z16(r);HD6301_SET_V16(a,b,r);HD6301_SET_C16(r);}


/**********************************
 *	Functions
 **********************************/

/* HD6301 internal functions */
static Uint8 hd6301_read_memory(Uint16 addr);
static void hd6301_write_memory (Uint16 addr, Uint8 value);
static Uint16 hd6301_get_memory_ext(void);

/* HD6301 opcodes functions */
static void hd6301_undefined(void);
static void hd6301_nop(void);
static void hd6301_lsrd(void);
static void hd6301_asld(void);
static void hd6301_tap(void);
static void hd6301_tpa(void);
static void hd6301_inx(void);
static void hd6301_dex(void);
static void hd6301_clv(void);
static void hd6301_sev(void);
static void hd6301_clc(void);
static void hd6301_sec(void);
static void hd6301_cli(void);
static void hd6301_sei(void);
static void hd6301_sba(void);
static void hd6301_cba(void);
static void hd6301_tab(void);
static void hd6301_tba(void);
static void hd6301_xgdx(void);
static void hd6301_daa(void);
static void hd6301_slp(void);
static void hd6301_aba(void);
static void hd6301_bra(void);
static void hd6301_brn(void);
static void hd6301_bhi(void);
static void hd6301_bls(void);
static void hd6301_bcc(void);
static void hd6301_bcs(void);
static void hd6301_bne(void);
static void hd6301_beq(void);
static void hd6301_bvc(void);
static void hd6301_bvs(void);
static void hd6301_bpl(void);
static void hd6301_bmi(void);
static void hd6301_bge(void);
static void hd6301_blt(void);
static void hd6301_bgt(void);
static void hd6301_ble(void);
static void hd6301_tsx(void);
static void hd6301_ins(void);
static void hd6301_pula(void);
static void hd6301_pulb(void);
static void hd6301_des(void);
static void hd6301_txs(void);
static void hd6301_psha(void);
static void hd6301_pshb(void);
static void hd6301_pulx(void);
static void hd6301_rts(void);
static void hd6301_abx(void);
static void hd6301_rti(void);
static void hd6301_pshx(void);
static void hd6301_mul(void);
static void hd6301_wai(void);
static void hd6301_swi(void);
static void hd6301_nega(void);
static void hd6301_coma(void);
static void hd6301_lsra(void);
static void hd6301_rora(void);
static void hd6301_asra(void);
static void hd6301_asla(void);
static void hd6301_rola(void);
static void hd6301_deca(void);
static void hd6301_inca(void);
static void hd6301_tsta(void);
static void hd6301_clra(void);
static void hd6301_negb(void);
static void hd6301_comb(void);
static void hd6301_lsrb(void);
static void hd6301_rorb(void);
static void hd6301_asrb(void);
static void hd6301_aslb(void);
static void hd6301_rolb(void);
static void hd6301_decb(void);
static void hd6301_incb(void);
static void hd6301_tstb(void);
static void hd6301_clrb(void);
static void hd6301_neg_ind(void);
static void hd6301_aim_ind(void);
static void hd6301_oim_ind(void);
static void hd6301_com_ind(void);
static void hd6301_lsr_ind(void);
static void hd6301_eim_ind(void);
static void hd6301_ror_ind(void);
static void hd6301_asr_ind(void);
static void hd6301_asl_ind(void);
static void hd6301_rol_ind(void);
static void hd6301_dec_ind(void);
static void hd6301_tim_ind(void);
static void hd6301_inc_ind(void);
static void hd6301_tst_ind(void);
static void hd6301_jmp_ind(void);
static void hd6301_clr_ind(void);
static void hd6301_neg_ext(void);
static void hd6301_aim_dir(void);
static void hd6301_oim_dir(void);
static void hd6301_com_ext(void);
static void hd6301_lsr_ext(void);
static void hd6301_eim_dir(void);
static void hd6301_ror_ext(void);
static void hd6301_asr_ext(void);
static void hd6301_asl_ext(void);
static void hd6301_rol_ext(void);
static void hd6301_dec_ext(void);
static void hd6301_tim_dir(void);
static void hd6301_inc_ext(void);
static void hd6301_tst_ext(void);
static void hd6301_jmp_ext(void);
static void hd6301_clr_ext(void);
static void hd6301_suba_imm(void);
static void hd6301_cmpa_imm(void);
static void hd6301_sbca_imm(void);
static void hd6301_subd_imm(void);
static void hd6301_anda_imm(void);
static void hd6301_bita_imm(void);
static void hd6301_ldaa_imm(void);
static void hd6301_eora_imm(void);
static void hd6301_adca_imm(void);
static void hd6301_oraa_imm(void);
static void hd6301_adda_imm(void);
static void hd6301_cpx_imm(void);
static void hd6301_bsr(void);
static void hd6301_lds_imm(void);
static void hd6301_suba_dir(void);
static void hd6301_cmpa_dir(void);
static void hd6301_sbca_dir(void);
static void hd6301_subd_dir(void);
static void hd6301_anda_dir(void);
static void hd6301_bita_dir(void);
static void hd6301_ldaa_dir(void);
static void hd6301_staa_dir(void);
static void hd6301_eora_dir(void);
static void hd6301_adca_dir(void);
static void hd6301_oraa_dir(void);
static void hd6301_adda_dir(void);
static void hd6301_cpx_dir(void);
static void hd6301_jsr_dir(void);
static void hd6301_lds_dir(void);
static void hd6301_sts_dir(void);
static void hd6301_suba_ind(void);
static void hd6301_cmpa_ind(void);
static void hd6301_sbca_ind(void);
static void hd6301_subd_ind(void);
static void hd6301_anda_ind(void);
static void hd6301_bita_ind(void);
static void hd6301_ldaa_ind(void);
static void hd6301_staa_ind(void);
static void hd6301_eora_ind(void);
static void hd6301_adca_ind(void);
static void hd6301_oraa_ind(void);
static void hd6301_adda_ind(void);
static void hd6301_cpx_ind(void);
static void hd6301_jsr_ind(void);
static void hd6301_lds_ind(void);
static void hd6301_sts_ind(void);
static void hd6301_suba_ext(void);
static void hd6301_cmpa_ext(void);
static void hd6301_sbca_ext(void);
static void hd6301_subd_ext(void);
static void hd6301_anda_ext(void);
static void hd6301_bita_ext(void);
static void hd6301_ldaa_ext(void);
static void hd6301_staa_ext(void);
static void hd6301_eora_ext(void);
static void hd6301_adca_ext(void);
static void hd6301_oraa_ext(void);
static void hd6301_adda_ext(void);
static void hd6301_cpx_ext(void);
static void hd6301_jsr_ext(void);
static void hd6301_lds_ext(void);
static void hd6301_sts_ext(void);
static void hd6301_subb_imm(void);
static void hd6301_cmpb_imm(void);
static void hd6301_sbcb_imm(void);
static void hd6301_addd_imm(void);
static void hd6301_andb_imm(void);
static void hd6301_bitb_imm(void);
static void hd6301_ldab_imm(void);
static void hd6301_eorb_imm(void);
static void hd6301_adcb_imm(void);
static void hd6301_orab_imm(void);
static void hd6301_addb_imm(void);
static void hd6301_ldd_imm(void);
static void hd6301_ldx_imm(void);
static void hd6301_subb_dir(void);
static void hd6301_cmpb_dir(void);
static void hd6301_sbcb_dir(void);
static void hd6301_addd_dir(void);
static void hd6301_andb_dir(void);
static void hd6301_bitb_dir(void);
static void hd6301_ldab_dir(void);
static void hd6301_stab_dir(void);
static void hd6301_eorb_dir(void);
static void hd6301_adcb_dir(void);
static void hd6301_orab_dir(void);
static void hd6301_addb_dir(void);
static void hd6301_ldd_dir(void);
static void hd6301_std_dir(void);
static void hd6301_ldx_dir(void);
static void hd6301_stx_dir(void);
static void hd6301_subb_ind(void);
static void hd6301_cmpb_ind(void);
static void hd6301_sbcb_ind(void);
static void hd6301_addd_ind(void);
static void hd6301_andb_ind(void);
static void hd6301_bitb_ind(void);
static void hd6301_ldab_ind(void);
static void hd6301_stab_ind(void);
static void hd6301_eorb_ind(void);
static void hd6301_adcb_ind(void);
static void hd6301_orab_ind(void);
static void hd6301_addb_ind(void);
static void hd6301_ldd_ind(void);
static void hd6301_std_ind(void);
static void hd6301_ldx_ind(void);
static void hd6301_stx_ind(void);
static void hd6301_subb_ext(void);
static void hd6301_cmpb_ext(void);
static void hd6301_sbcb_ext(void);
static void hd6301_addd_ext(void);
static void hd6301_andb_ext(void);
static void hd6301_bitb_ext(void);
static void hd6301_ldab_ext(void);
static void hd6301_stab_ext(void);
static void hd6301_eorb_ext(void);
static void hd6301_adcb_ext(void);
static void hd6301_orab_ext(void);
static void hd6301_addb_ext(void);
static void hd6301_ldd_ext(void);
static void hd6301_std_ext(void);
static void hd6301_ldx_ext(void);
static void hd6301_stx_ext(void);


/**********************************
 *	Variables
 **********************************/
static char hd6301_str_instr[50];

static struct hd6301_opcode_t hd6301_opcode;

static struct hd6301_opcode_t hd6301_opcode_table[256] = {

	{0x00, 0, hd6301_undefined,	0,	"", 			HD6301_DISASM_UNDEFINED},
	{0x01, 1, hd6301_nop,		1,	"nop", 			HD6301_DISASM_NONE},
	{0x02, 0, hd6301_undefined,	0,	"", 			HD6301_DISASM_UNDEFINED},
	{0x03, 0, hd6301_undefined,	0,	"", 			HD6301_DISASM_UNDEFINED},
	{0x04, 1, hd6301_lsrd,		1,	"lsrd",			HD6301_DISASM_NONE},
	{0x05, 1, hd6301_asld,		1,	"asld",			HD6301_DISASM_NONE},
	{0x06, 1, hd6301_tap,		1,	"tap",			HD6301_DISASM_NONE},
	{0x07, 1, hd6301_tpa,		1,	"tpa",			HD6301_DISASM_NONE},
	{0x08, 1, hd6301_inx,		1,	"inx",			HD6301_DISASM_NONE},
	{0x09, 1, hd6301_dex,		1,	"dex",			HD6301_DISASM_NONE},
	{0x0a, 1, hd6301_clv,		1,	"clv",			HD6301_DISASM_NONE},
	{0x0b, 1, hd6301_sev,		1,	"sev",			HD6301_DISASM_NONE},
	{0x0c, 1, hd6301_clc,		1,	"clc",			HD6301_DISASM_NONE},
	{0x0d, 1, hd6301_sec,		1,	"sec",			HD6301_DISASM_NONE},
	{0x0e, 1, hd6301_cli,		1,	"cli",			HD6301_DISASM_NONE},
	{0x0f, 1, hd6301_sei,		1,	"sei",			HD6301_DISASM_NONE},

	{0x10, 1, hd6301_sba,		1,	"sba",			HD6301_DISASM_NONE},
	{0x11, 1, hd6301_cba,		1,	"cba",			HD6301_DISASM_NONE},
 	{0x12, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},
 	{0x13, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},
 	{0x14, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},
 	{0x15, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},
	{0x16, 1, hd6301_tab,		1,	"tab",			HD6301_DISASM_NONE},
	{0x17, 1, hd6301_tba,		1,	"tba",			HD6301_DISASM_NONE},
 	{0x18, 1, hd6301_xgdx,		2,	"xgdx",			HD6301_DISASM_NONE},
	{0x19, 1, hd6301_daa,		2,	"daa",			HD6301_DISASM_NONE},
 	{0x1a, 1, hd6301_slp,		4,	"slp",			HD6301_DISASM_NONE},
	{0x1b, 1, hd6301_aba,		1,	"aba",			HD6301_DISASM_NONE},
 	{0x1c, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},
 	{0x1d, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},
 	{0x1e, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},
 	{0x1f, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},

	{0x20, 0, hd6301_bra,		3,	"bra  $%02x",		HD6301_DISASM_MEMORY8},
	{0x21, 0, hd6301_brn,		3,	"brn  $%02x",		HD6301_DISASM_MEMORY8},
	{0x22, 0, hd6301_bhi,		3,	"bhi  $%02x",		HD6301_DISASM_MEMORY8},
	{0x23, 0, hd6301_bls,		3,	"bls  $%02x",		HD6301_DISASM_MEMORY8},
	{0x24, 0, hd6301_bcc,		3,	"bcc  $%02x",		HD6301_DISASM_MEMORY8},
	{0x25, 0, hd6301_bcs,		3,	"bcs  $%02x",		HD6301_DISASM_MEMORY8},
	{0x26, 0, hd6301_bne,		3,	"bne  $%02x",		HD6301_DISASM_MEMORY8},
	{0x27, 0, hd6301_beq,		3,	"beq  $%02x",		HD6301_DISASM_MEMORY8},
	{0x28, 0, hd6301_bvc,		3,	"bvc  $%02x",		HD6301_DISASM_MEMORY8},
	{0x29, 0, hd6301_bvs,		3,	"bvs  $%02x",		HD6301_DISASM_MEMORY8},
	{0x2a, 0, hd6301_bpl,		3,	"bpl  $%02x",		HD6301_DISASM_MEMORY8},
	{0x2b, 0, hd6301_bmi,		3,	"bmi  $%02x",		HD6301_DISASM_MEMORY8},
	{0x2c, 0, hd6301_bge,		3,	"bge  $%02x",		HD6301_DISASM_MEMORY8},
	{0x2d, 0, hd6301_blt,		3,	"blt  $%02x",		HD6301_DISASM_MEMORY8},
	{0x2e, 0, hd6301_bgt,		3,	"bgt  $%02x",		HD6301_DISASM_MEMORY8},
	{0x2f, 0, hd6301_ble,		3,	"ble  $%02x",		HD6301_DISASM_MEMORY8},

	{0x30, 1, hd6301_tsx,		1,	"tsx",			HD6301_DISASM_NONE},
	{0x31, 1, hd6301_ins,		1,	"ins",			HD6301_DISASM_NONE},
	{0x32, 1, hd6301_pula,		3,	"pula",			HD6301_DISASM_NONE},
	{0x33, 1, hd6301_pulb,		3,	"pulb",			HD6301_DISASM_NONE},
	{0x34, 1, hd6301_des,		1,	"des",			HD6301_DISASM_NONE},
	{0x35, 1, hd6301_txs,		1,	"txs",			HD6301_DISASM_NONE},
	{0x36, 1, hd6301_psha,		4,	"psha",			HD6301_DISASM_NONE},
	{0x37, 1, hd6301_pshb,		4,	"pshb",			HD6301_DISASM_NONE},
	{0x38, 1, hd6301_pulx,		4,	"pulx",			HD6301_DISASM_NONE},
	{0x39, 0, hd6301_rts,		5,	"rts",			HD6301_DISASM_NONE},
	{0x3a, 1, hd6301_abx,		1,	"abx",			HD6301_DISASM_NONE},
	{0x3b, 0, hd6301_rti,		10,	"rti",			HD6301_DISASM_NONE},
	{0x3c, 1, hd6301_pshx,		5 ,	"pshx",			HD6301_DISASM_NONE},
	{0x3d, 1, hd6301_mul,		7,	"mul",			HD6301_DISASM_NONE},
	{0x3e, 0, hd6301_wai,		9,	"wai",			HD6301_DISASM_NONE},
	{0x3f, 0, hd6301_swi,		12,	"swi",			HD6301_DISASM_NONE},

	{0x40, 1, hd6301_nega,		1,	"nega",			HD6301_DISASM_NONE},
	{0x41, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},
	{0x42, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},
	{0x43, 1, hd6301_coma,		1,	"coma",			HD6301_DISASM_NONE},
	{0x44, 1, hd6301_lsra,		1,	"lsra",			HD6301_DISASM_NONE},
	{0x45, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},
	{0x46, 1, hd6301_rora,		1,	"rora",			HD6301_DISASM_NONE},
	{0x47, 1, hd6301_asra,		1,	"asra",			HD6301_DISASM_NONE},
	{0x48, 1, hd6301_asla,		1,	"lsla",			HD6301_DISASM_NONE},
	{0x49, 1, hd6301_rola,		1,	"rola",			HD6301_DISASM_NONE},
	{0x4a, 1, hd6301_deca,		1,	"deca",			HD6301_DISASM_NONE},
	{0x4b, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},
	{0x4c, 1, hd6301_inca,		1,	"inca",			HD6301_DISASM_NONE},
	{0x4d, 1, hd6301_tsta,		1,	"tsta",			HD6301_DISASM_NONE},
	{0x4e, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},
	{0x4f, 1, hd6301_clra,		1,	"clra",			HD6301_DISASM_NONE},

	{0x50, 1, hd6301_negb,		1,	"negb",			HD6301_DISASM_NONE},
	{0x51, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},
	{0x52, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},
	{0x53, 1, hd6301_comb,		1,	"comb",			HD6301_DISASM_NONE},
	{0x54, 1, hd6301_lsrb,		1,	"lsrb",			HD6301_DISASM_NONE},
	{0x55, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},
	{0x56, 1, hd6301_rorb,		1,	"rorb",			HD6301_DISASM_NONE},
	{0x57, 1, hd6301_asrb,		1,	"asrb",			HD6301_DISASM_NONE},
	{0x58, 1, hd6301_aslb,		1,	"lslb",			HD6301_DISASM_NONE},
	{0x59, 1, hd6301_rolb,		1,	"rolb",			HD6301_DISASM_NONE},
	{0x5a, 1, hd6301_decb,		1,	"decb",			HD6301_DISASM_NONE},
	{0x5b, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},
	{0x5c, 1, hd6301_incb,		1,	"incb",			HD6301_DISASM_NONE},
	{0x5d, 1, hd6301_tstb,		1,	"tstb",			HD6301_DISASM_NONE},
	{0x5e, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},
	{0x5f, 1, hd6301_clrb,		1,	"clrb",			HD6301_DISASM_NONE},

	{0x60, 2, hd6301_neg_ind,	6,	"neg $%02x,x",		HD6301_DISASM_MEMORY8},
 	{0x61, 3, hd6301_aim_ind,	7,	"aim #$%02x,$%02x x",	HD6301_DISASM_XIM},
 	{0x62, 3, hd6301_oim_ind,	7,	"oim #$%02x,$%02x x",	HD6301_DISASM_XIM},
	{0x63, 2, hd6301_com_ind,	6,	"com $%02x,x",		HD6301_DISASM_MEMORY8},
	{0x64, 2, hd6301_lsr_ind,	6,	"lsr $%02x,x",		HD6301_DISASM_MEMORY8},
 	{0x65, 3, hd6301_eim_ind,	7,	"eim #$%02x,$%02x x",	HD6301_DISASM_XIM},
	{0x66, 2, hd6301_ror_ind,	6,	"ror $%02x,x",		HD6301_DISASM_MEMORY8},
	{0x67, 2, hd6301_asr_ind,	6,	"asr $%02x,x",		HD6301_DISASM_MEMORY8},
	{0x68, 2, hd6301_asl_ind,	6,	"lsl $%02x,x",		HD6301_DISASM_MEMORY8},
	{0x69, 2, hd6301_rol_ind,	6,	"rol $%02x,x",		HD6301_DISASM_MEMORY8},
	{0x6a, 2, hd6301_dec_ind,	6,	"dec $%02x,x",		HD6301_DISASM_MEMORY8},
 	{0x6b, 3, hd6301_tim_ind,	5,	"tim #$%02x,$%02x x",	HD6301_DISASM_XIM},
	{0x6c, 2, hd6301_inc_ind,	6,	"inc $%02x,x",		HD6301_DISASM_MEMORY8},
	{0x6d, 2, hd6301_tst_ind,	4,	"tst $%02x,x",		HD6301_DISASM_MEMORY8},
	{0x6e, 0, hd6301_jmp_ind,	3,	"jmp $%02x,x",		HD6301_DISASM_MEMORY8},
	{0x6f, 2, hd6301_clr_ind,	5,	"clr $%02x,x",		HD6301_DISASM_MEMORY8},

	{0x70, 3, hd6301_neg_ext,	6,	"neg $%04x",		HD6301_DISASM_MEMORY16},
 	{0x71, 3, hd6301_aim_dir,	6,	"aim #$%02x,$%02x",	HD6301_DISASM_XIM},
 	{0x72, 3, hd6301_oim_dir,	6,	"oim #$%02x,$%02x",	HD6301_DISASM_XIM},
	{0x73, 3, hd6301_com_ext,	6,	"com $%04x",		HD6301_DISASM_MEMORY16},
	{0x74, 3, hd6301_lsr_ext,	6,	"lsr $%04x",		HD6301_DISASM_MEMORY16},
 	{0x75, 3, hd6301_eim_dir,	6,	"eim #$%02x,$%02x",	HD6301_DISASM_XIM},
	{0x76, 3, hd6301_ror_ext,	6,	"ror $%04x",		HD6301_DISASM_MEMORY16},
	{0x77, 3, hd6301_asr_ext,	6,	"asr $%04x",		HD6301_DISASM_MEMORY16},
	{0x78, 3, hd6301_asl_ext,	6,	"lsl $%04x",		HD6301_DISASM_MEMORY16},
	{0x79, 3, hd6301_rol_ext,	6,	"rol $%04x",		HD6301_DISASM_MEMORY16},
	{0x7a, 3, hd6301_dec_ext,	6,	"dec $%04x",		HD6301_DISASM_MEMORY16},
 	{0x7b, 3, hd6301_tim_dir,	4,	"tim #$%02x,$%02x",	HD6301_DISASM_XIM},
	{0x7c, 3, hd6301_inc_ext,	6,	"inc $%04x",		HD6301_DISASM_MEMORY16},
	{0x7d, 3, hd6301_tst_ext,	4,	"tst $%04x",		HD6301_DISASM_MEMORY16},
	{0x7e, 0, hd6301_jmp_ext,	3,	"jmp $%04x",		HD6301_DISASM_MEMORY16},
	{0x7f, 3, hd6301_clr_ext,	5,	"clr $%04x",		HD6301_DISASM_MEMORY16},

	{0x80, 2, hd6301_suba_imm,	2,	"suba #$%02x",		HD6301_DISASM_MEMORY8},
	{0x81, 2, hd6301_cmpa_imm,	2,	"cmpa #$%02x",		HD6301_DISASM_MEMORY8},
	{0x82, 2, hd6301_sbca_imm,	2,	"sbca #$%02x",		HD6301_DISASM_MEMORY8},
	{0x83, 3, hd6301_subd_imm,	3,	"subd #$%04x",		HD6301_DISASM_MEMORY16},
	{0x84, 2, hd6301_anda_imm,	2,	"anda #$%02x",		HD6301_DISASM_MEMORY8},
	{0x85, 2, hd6301_bita_imm,	2,	"bita #$%02x",		HD6301_DISASM_MEMORY8},
	{0x86, 2, hd6301_ldaa_imm,	2,	"ldaa #$%02x",		HD6301_DISASM_MEMORY8},
	{0x87, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},
	{0x88, 2, hd6301_eora_imm,	2,	"eora #$%02x",		HD6301_DISASM_MEMORY8},
	{0x89, 2, hd6301_adca_imm,	2,	"adca #$%02x",		HD6301_DISASM_MEMORY8},
	{0x8a, 2, hd6301_oraa_imm,	2,	"oraa #$%02x",		HD6301_DISASM_MEMORY8},
	{0x8b, 2, hd6301_adda_imm,	2,	"adda #$%02x",		HD6301_DISASM_MEMORY8},
	{0x8c, 3, hd6301_cpx_imm,	3,	"cpx  #$%04x",		HD6301_DISASM_MEMORY16},
	{0x8d, 0, hd6301_bsr,		5,	"bsr  $%02x",		HD6301_DISASM_MEMORY8},
	{0x8e, 3, hd6301_lds_imm,	3,	"lds  #$%04x",		HD6301_DISASM_MEMORY16},
 	{0x8f, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},

	{0x90, 2, hd6301_suba_dir,	3,	"suba $%02x",		HD6301_DISASM_MEMORY8},
	{0x91, 2, hd6301_cmpa_dir,	3,	"cmpa $%02x",		HD6301_DISASM_MEMORY8},
	{0x92, 2, hd6301_sbca_dir,	3,	"sbca $%02x",		HD6301_DISASM_MEMORY8},
	{0x93, 2, hd6301_subd_dir,	4,	"subd $%02x",		HD6301_DISASM_MEMORY8},
	{0x94, 2, hd6301_anda_dir,	3,	"anda $%02x",		HD6301_DISASM_MEMORY8},
	{0x95, 2, hd6301_bita_dir,	3,	"bita $%02x",		HD6301_DISASM_MEMORY8},
	{0x96, 2, hd6301_ldaa_dir,	3,	"ldaa $%02x",		HD6301_DISASM_MEMORY8},
	{0x97, 2, hd6301_staa_dir,	3,	"staa $%02x",		HD6301_DISASM_MEMORY8},
	{0x98, 2, hd6301_eora_dir,	3,	"eora $%02x",		HD6301_DISASM_MEMORY8},
	{0x99, 2, hd6301_adca_dir,	3,	"adca $%02x",		HD6301_DISASM_MEMORY8},
	{0x9a, 2, hd6301_oraa_dir,	3,	"oraa $%02x",		HD6301_DISASM_MEMORY8},
	{0x9b, 2, hd6301_adda_dir,	3,	"adda $%02x",		HD6301_DISASM_MEMORY8},
	{0x9c, 2, hd6301_cpx_dir,	4,	"cpx  $%02x",		HD6301_DISASM_MEMORY8},
	{0x9d, 0, hd6301_jsr_dir,	5,	"jsr  $%02x",		HD6301_DISASM_MEMORY8},
	{0x9e, 2, hd6301_lds_dir,	4,	"lds  $%02x",		HD6301_DISASM_MEMORY8},
	{0x9f, 2, hd6301_sts_dir,	4,	"sts  $%02x",		HD6301_DISASM_MEMORY8},

	{0xa0, 2, hd6301_suba_ind,	4,	"suba $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xa1, 2, hd6301_cmpa_ind,	4,	"cmpa $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xa2, 2, hd6301_sbca_ind,	4,	"sbca $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xa3, 2, hd6301_subd_ind,	5,	"subd $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xa4, 2, hd6301_anda_ind,	4,	"anda $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xa5, 2, hd6301_bita_ind,	4,	"bita $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xa6, 2, hd6301_ldaa_ind,	4,	"ldaa $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xa7, 2, hd6301_staa_ind,	4,	"staa $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xa8, 2, hd6301_eora_ind,	4,	"eora $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xa9, 2, hd6301_adca_ind,	4,	"adca $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xaa, 2, hd6301_oraa_ind,	4,	"oraa $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xab, 2, hd6301_adda_ind,	4,	"adda $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xac, 2, hd6301_cpx_ind,	5,	"cpx  $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xad, 0, hd6301_jsr_ind,	5,	"jsr  $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xae, 2, hd6301_lds_ind,	5,	"lds  $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xaf, 2, hd6301_sts_ind,	5,	"sts  $%02x,x",		HD6301_DISASM_MEMORY8},

	{0xb0, 3, hd6301_suba_ext,	4,	"suba $%04x",		HD6301_DISASM_MEMORY16},
	{0xb1, 3, hd6301_cmpa_ext,	4,	"cmpa $%04x",		HD6301_DISASM_MEMORY16},
	{0xb2, 3, hd6301_sbca_ext,	4,	"sbca $%04x",		HD6301_DISASM_MEMORY16},
	{0xb3, 3, hd6301_subd_ext,	5,	"subd $%04x",		HD6301_DISASM_MEMORY16},
	{0xb4, 3, hd6301_anda_ext,	4,	"anda $%04x",		HD6301_DISASM_MEMORY16},
	{0xb5, 3, hd6301_bita_ext,	4,	"bita $%04x",		HD6301_DISASM_MEMORY16},
	{0xb6, 3, hd6301_ldaa_ext,	4,	"ldaa $%04x",		HD6301_DISASM_MEMORY16},
	{0xb7, 3, hd6301_staa_ext,	4,	"staa $%04x",		HD6301_DISASM_MEMORY16},
	{0xb8, 3, hd6301_eora_ext,	4,	"eora $%04x",		HD6301_DISASM_MEMORY16},
	{0xb9, 3, hd6301_adca_ext,	4,	"adca $%04x",		HD6301_DISASM_MEMORY16},
	{0xba, 3, hd6301_oraa_ext,	4,	"oraa $%04x",		HD6301_DISASM_MEMORY16},
	{0xbb, 3, hd6301_adda_ext,	4,	"adda $%04x",		HD6301_DISASM_MEMORY16},
	{0xbc, 3, hd6301_cpx_ext,	5,	"cpx  $%04x",		HD6301_DISASM_MEMORY16},
	{0xbd, 0, hd6301_jsr_ext,	6,	"jsr  $%04x",		HD6301_DISASM_MEMORY16},
	{0xbe, 3, hd6301_lds_ext,	5,	"lds  $%04x",		HD6301_DISASM_MEMORY16},
	{0xbf, 3, hd6301_sts_ext,	5,	"sts  $%04x",		HD6301_DISASM_MEMORY16},

	{0xc0, 2, hd6301_subb_imm,	2,	"subb #$%02x",		HD6301_DISASM_MEMORY8},
	{0xc1, 2, hd6301_cmpb_imm,	2,	"cmpb #$%02x",		HD6301_DISASM_MEMORY8},
	{0xc2, 2, hd6301_sbcb_imm,	2,	"sbcb #$%02x",		HD6301_DISASM_MEMORY8},
	{0xc3, 3, hd6301_addd_imm,	3,	"addd #$%04x",		HD6301_DISASM_MEMORY16},
	{0xc4, 2, hd6301_andb_imm,	2,	"andb #$%02x",		HD6301_DISASM_MEMORY8},
	{0xc5, 2, hd6301_bitb_imm,	2,	"bitb #$%02x",		HD6301_DISASM_MEMORY8},
	{0xc6, 2, hd6301_ldab_imm,	2,	"ldab #$%02x",		HD6301_DISASM_MEMORY8},
	{0xc7, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},
	{0xc8, 2, hd6301_eorb_imm,	2,	"eorb #$%02x",		HD6301_DISASM_MEMORY8},
	{0xc9, 2, hd6301_adcb_imm,	2,	"adcb #$%02x",		HD6301_DISASM_MEMORY8},
	{0xca, 2, hd6301_orab_imm,	2,	"orab #$%02x",		HD6301_DISASM_MEMORY8},
	{0xcb, 2, hd6301_addb_imm,	2,	"addb #$%02x",		HD6301_DISASM_MEMORY8},
	{0xcc, 3, hd6301_ldd_imm,	3,	"ldd  #$%04x",		HD6301_DISASM_MEMORY16},
	{0xcd, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},
	{0xce, 3, hd6301_ldx_imm,	3,	"ldx  #$%04x",		HD6301_DISASM_MEMORY16},
	{0xcf, 0, hd6301_undefined,	0,	"",			HD6301_DISASM_UNDEFINED},

	{0xd0, 2, hd6301_subb_dir,	3,	"subb $%02x",		HD6301_DISASM_MEMORY8},
	{0xd1, 2, hd6301_cmpb_dir,	3,	"cmpb $%02x",		HD6301_DISASM_MEMORY8},
	{0xd2, 2, hd6301_sbcb_dir,	3,	"sbcb $%02x",		HD6301_DISASM_MEMORY8},
	{0xd3, 2, hd6301_addd_dir,	4,	"addd $%02x",		HD6301_DISASM_MEMORY8},
	{0xd4, 2, hd6301_andb_dir,	3,	"andb $%02x",		HD6301_DISASM_MEMORY8},
	{0xd5, 2, hd6301_bitb_dir,	3,	"bitb $%02x",		HD6301_DISASM_MEMORY8},
	{0xd6, 2, hd6301_ldab_dir,	3,	"ldab $%02x",		HD6301_DISASM_MEMORY8},
	{0xd7, 2, hd6301_stab_dir,	3,	"stab $%02x",		HD6301_DISASM_MEMORY8},
	{0xd8, 2, hd6301_eorb_dir,	3,	"eorb $%02x",		HD6301_DISASM_MEMORY8},
	{0xd9, 2, hd6301_adcb_dir,	3,	"adcb $%02x",		HD6301_DISASM_MEMORY8},
	{0xda, 2, hd6301_orab_dir,	3,	"orab $%02x",		HD6301_DISASM_MEMORY8},
	{0xdb, 2, hd6301_addb_dir,	3,	"addb $%02x",		HD6301_DISASM_MEMORY8},
	{0xdc, 2, hd6301_ldd_dir,	4,	"ldd  $%02x",		HD6301_DISASM_MEMORY8},
	{0xdd, 2, hd6301_std_dir,	4,	"std  $%02x",		HD6301_DISASM_MEMORY8},
	{0xde, 2, hd6301_ldx_dir,	4,	"ldx  $%02x",		HD6301_DISASM_MEMORY8},
	{0xdf, 2, hd6301_stx_dir,	4,	"stx  $%02x",		HD6301_DISASM_MEMORY8},
	
	{0xe0, 2, hd6301_subb_ind,	4,	"subb $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xe1, 2, hd6301_cmpb_ind,	4,	"cmpb $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xe2, 2, hd6301_sbcb_ind,	4,	"sbcb $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xe3, 2, hd6301_addd_ind,	5,	"addd $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xe4, 2, hd6301_andb_ind,	4,	"andb $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xe5, 2, hd6301_bitb_ind,	4,	"bitb $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xe6, 2, hd6301_ldab_ind,	4,	"ldab $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xe7, 2, hd6301_stab_ind,	4,	"stab $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xe8, 2, hd6301_eorb_ind,	4,	"eorb $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xe9, 2, hd6301_adcb_ind,	4,	"adcb $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xea, 2, hd6301_orab_ind,	4,	"orab $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xeb, 2, hd6301_addb_ind,	4,	"addb $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xec, 2, hd6301_ldd_ind,	5,	"ldd  $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xed, 2, hd6301_std_ind,	5,	"std  $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xee, 2, hd6301_ldx_ind,	5,	"ldx  $%02x,x",		HD6301_DISASM_MEMORY8},
	{0xef, 2, hd6301_stx_ind,	5,	"stx  $%02x,x",		HD6301_DISASM_MEMORY8},

	{0xf0, 3, hd6301_subb_ext,	4,	"subb $%04x",		HD6301_DISASM_MEMORY16},
	{0xf1, 3, hd6301_cmpb_ext,	4,	"cmpb $%04x",		HD6301_DISASM_MEMORY16},
	{0xf2, 3, hd6301_sbcb_ext,	4,	"sbcb $%04x",		HD6301_DISASM_MEMORY16},
	{0xf3, 3, hd6301_addd_ext,	5,	"addd $%04x",		HD6301_DISASM_MEMORY16},
	{0xf4, 3, hd6301_andb_ext,	4,	"andb $%04x",		HD6301_DISASM_MEMORY16},
	{0xf5, 3, hd6301_bitb_ext,	4,	"bitb $%04x",		HD6301_DISASM_MEMORY16},
	{0xf6, 3, hd6301_ldab_ext,	4,	"ldab $%04x",		HD6301_DISASM_MEMORY16},
	{0xf7, 3, hd6301_stab_ext,	4,	"stab $%04x",		HD6301_DISASM_MEMORY16},
	{0xf8, 3, hd6301_eorb_ext,	4,	"eorb $%04x",		HD6301_DISASM_MEMORY16},
	{0xf9, 3, hd6301_adcb_ext,	4,	"adcb $%04x",		HD6301_DISASM_MEMORY16},
	{0xfa, 3, hd6301_orab_ext,	4,	"orab $%04x",		HD6301_DISASM_MEMORY16},
	{0xfb, 3, hd6301_addb_ext,	4,	"addb $%04x",		HD6301_DISASM_MEMORY16},
	{0xfc, 3, hd6301_ldd_ext,	5,	"ldd  $%04x",		HD6301_DISASM_MEMORY16},
	{0xfd, 3, hd6301_std_ext,	5,	"std  $%04x",		HD6301_DISASM_MEMORY16},
	{0xfe, 3, hd6301_ldx_ext,	5,	"ldx  $%04x",		HD6301_DISASM_MEMORY16},
	{0xff, 3, hd6301_stx_ext,	5,	"stx  $%04x",		HD6301_DISASM_MEMORY16}
};


/* Variables */
static Uint8	hd6301_cycles;
static Uint8	hd6301_cur_inst;

static Sint8	hd6301_reg_A; 
static Sint8	hd6301_reg_B;
static Sint16	hd6301_reg_X;
static Uint16	hd6301_reg_SP;
static Uint16	hd6301_reg_PC;
static Uint8	hd6301_reg_CCR;

//Uint8	hd6301_reg_RMCR;

static Uint8	hd6301_intREG[32];
static Uint8	hd6301_intRAM[128];
static Uint8	hd6301_intROM[4096];


/**********************************
 *	Emulator kernel
 **********************************/

/**
 * Initialise hd6301 cpu
 */
void hd6301_init_cpu(void)
{
	hd6301_reg_CCR = 0xc0;
}

/**
 * Execute 1 hd6301 instruction
 */
void hd6301_execute_one_instruction(void)
{
	hd6301_cur_inst = hd6301_read_memory(hd6301_reg_PC);

	/* Get opcode to execute */
	hd6301_opcode = hd6301_opcode_table[hd6301_cur_inst];

	/* disasm opcode ? */
#ifdef HD6301_DISASM
	hd6301_disasm();
#endif
	/* execute opcode  */
	hd6301_opcode.op_func();

#ifdef HD6301_DISPLAY_REGS
	hd6301_display_registers();
#endif

	/* Increment instruction cycles */
	hd6301_cycles += hd6301_opcode.op_n_cycles;

	/* Increment PC register */
	hd6301_reg_PC += hd6301_opcode.op_bytes;

	/* post process interrupts */

	/* post process timers */

	/* post process SCI */
}

/**
 * Read hd6301 memory (Ram, Rom, Internal registers)
 */
static Uint8 hd6301_read_memory(Uint16 addr)
{
	/* Internal registers */
	if (addr <= 0x1f) {
		return hd6301_intREG[addr];
	}

	/* Internal RAM */
	if ((addr >= 0x80) && (addr <= 0xff)) {
		return hd6301_intRAM[addr-0x80];
	}

	/* Internal ROM */
	if (addr >= 0xf000) {
		return hd6301_intROM[addr-0xf000];
	}

	fprintf(stderr, "hd6301: 0x%04x: 0x%04x illegal memory address\n", hd6301_reg_PC, addr);
	exit(-1);
}

/**
 * Write hd6301 memory (Ram, Internal registers)
 */
static void hd6301_write_memory (Uint16 addr, Uint8 value)
{
	/* Internal registers */
	if (addr <= 0x1f) {
		hd6301_intREG[addr] = value;
	}

	/* Internal RAM */
	else if ((addr >= 0x80) && (addr <= 0xff)) {
		hd6301_intRAM[addr-0x80] = value;
	}

	/* Internal ROM */
	else if (addr >= 0xf000) {
		fprintf(stderr, "hd6301: 0x%04x: attempt to write to rom\n", addr);
	}

	/* Illegal address */
	else {
		fprintf(stderr, "hd6301: 0x%04x: write to illegal address\n", addr);
		exit(-1);
	}
}

/**
 * Get extended memory (16 bits)
 */
static Uint16 hd6301_get_memory_ext(void)
{
	Uint16 addr;

	addr = hd6301_read_memory(hd6301_reg_PC+1)<<8;
	addr += hd6301_read_memory(hd6301_reg_PC+2);
	return addr;
}

/**
 * Undefined opcode
 */
static void hd6301_undefined(void)
{
	fprintf(stderr, "hd6301: 0x%04x: 0x%02x unknown instruction\n", hd6301_reg_PC, hd6301_cur_inst);
	exit(-1);  /* TODO: Trap the error correctly */
}

/**
 * NOP : no operation
 *
 * HINZVC
 * ......
 */
static void hd6301_nop(void)
{
}

/**
 * LSRD : logical Shift Right, accumulator D : D=D>>1
 *
 * HINZVC
 * ..0***
 */
static void hd6301_lsrd(void)
{
	Uint16 regD;
	Uint8  carry;

	regD = (hd6301_reg_A<<8) + hd6301_reg_B;
	carry = regD & 1;
	regD >>= 1;

	hd6301_reg_A = regD >> 8;
	hd6301_reg_B = regD;

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_Z16(regD);
	hd6301_reg_CCR |= ((0 ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * ASLD : arythmetic Shift left, accumulator D : D=D<<1
 *
 * HINZVC
 * ..****
 */
static void hd6301_asld(void)
{
	Uint16 regD;
	Uint8  carry, bitN;

	regD = (hd6301_reg_A<<8) + hd6301_reg_B;
	carry = (regD >> 15) & 1;
	regD <<= 1;

	hd6301_reg_A = regD >> 8;
	hd6301_reg_B = regD;

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_NZ16(regD);
	bitN = (hd6301_reg_CCR & 0x8) >> 3;
	hd6301_reg_CCR |= ((bitN ^ carry) == 1) << hd6301_REG_CCR_V;
}


/**
 * TAP : transfer accumulator A into register CCR : CCR=A
 *
 * HINZVC
 * ******
 */
static void hd6301_tap(void)
{
	hd6301_reg_CCR = hd6301_reg_A;
	hd6301_reg_CCR |= 0xc0;
}

/**
 * TPA : transfer register CCR into accumulator A : A=CCR
 *
 * HINZVC
 * ......
 */
static void hd6301_tpa(void)
{
	hd6301_reg_A = hd6301_reg_CCR;
}

/**
 * INX : increment register X : X=X+1
 *
 * HINZVC
 * ...*..
 */
static void hd6301_inx(void)
{
	++ hd6301_reg_X;

	HD6301_CLR_Z;
	HD6301_SET_Z16(hd6301_reg_X);
}

/**
 * DEX : decrement register X : X=X-1
 *
 * HINZVC
 * ...*..
 */
static void hd6301_dex(void)
{
	-- hd6301_reg_X;

	HD6301_CLR_Z;
	HD6301_SET_Z16(hd6301_reg_X);
}

/**
 * CLV : clear register CCR bit V : V=0
 *
 * HINZVC
 * ....0.
 */
static void hd6301_clv(void)
{
	HD6301_CLR_V;
}

/**
 * SEV : set register CCR bit V : V=1
 *
 * HINZVC
 * ....1.
 */
static void hd6301_sev(void)
{
	hd6301_reg_CCR |= 1<<hd6301_REG_CCR_V;
}

/**
 * CLC : clear register CCR bit C : C=0
 *
 * HINZVC
 * .....0
 */
static void hd6301_clc(void)
{
	HD6301_CLR_C;
}

/**
 * SEC : set register CCR bit C : C=1
 *
 * HINZVC
 * .....1
 */
static void hd6301_sec(void)
{
	hd6301_reg_CCR |= 1<<hd6301_REG_CCR_C;
}

/**
 * CLI : clear register CCR bit I : I=0
 *
 * HINZVC
 * .0....
 */
static void hd6301_cli(void)
{
	HD6301_CLR_I;
}

/**
 * SEI : set register CCR bit I : I=1
 *
 * HINZVC
 * .1....
 */
static void hd6301_sei(void)
{
	hd6301_reg_CCR |= 1<<hd6301_REG_CCR_I;
}

/**
 * SBA : subtract accumulator B from accumulator A : A=A-B
 *
 * HINZVC
 * ..****
 */
static void hd6301_sba(void)
{
	Uint16 result;

	result = hd6301_reg_A - hd6301_reg_B;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, hd6301_reg_B, result);

	hd6301_reg_A = result;
}

/**
 * CBA : compare accumulator A and accumulator B : A-B
 *
 * HINZVC
 * ..****
 */
static void hd6301_cba(void)
{
	Uint16 result;

	result = hd6301_reg_A - hd6301_reg_B;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, hd6301_reg_B, result);
}

/**
 * TAB : transfer accumulator A into accumulator B : B=A
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_tab(void)
{
	hd6301_reg_B = hd6301_reg_A;

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * TBA : transfer accumulator B into accumulator A : A=B
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_tba(void)
{
	hd6301_reg_A = hd6301_reg_B;

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * XGDX : exchange register X and accumulator D : X<->D
 *
 * HINZVC
 * ......
 */
static void hd6301_xgdx(void)
{
	Uint16 temp;

	temp = hd6301_reg_X;
	hd6301_reg_X = (hd6301_reg_A << 8) + hd6301_reg_B;
	hd6301_reg_A = temp >> 8;
	hd6301_reg_B = temp;
}

/**
 * DAA : converts binary add of BCD characters into BCD format : A=BCD(A)
 *
 * HINZVC
 * ..****
 */
static void hd6301_daa(void)
{
	/* Todo */
}

/**
 * SLP : sleep
 *
 * HINZVC
 * ......
 */
static void hd6301_slp(void)
{
	/* Todo */
}

/**
 * ABA : add accumulator A and accumulator B into accumulator A : A=A+B
 *
 * HINZVC
 * *.****
 */
static void hd6301_aba(void)
{
	Uint16 result;

	result = hd6301_reg_A + hd6301_reg_B;

	HD6301_CLR_HNZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, hd6301_reg_B, result);
	HD6301_SET_H(hd6301_reg_A, hd6301_reg_B, result);

	hd6301_reg_A = result;
}

/**
 * BRA : branch always
 *
 * HINZVC
 * ......
 */
static void hd6301_bra(void)
{
	Sint8 addr;

	addr = hd6301_read_memory(hd6301_reg_PC + 1);
	hd6301_reg_PC += addr + 2;
}

/**
 * BRN : branch never
 *
 * HINZVC
 * ......
 */
static void hd6301_brn(void)
{
	hd6301_reg_PC += 2;
}

/**
 * BHI : branch if higher : C|Z=0
 *
 * HINZVC
 * ......
 */
static void hd6301_bhi(void)
{
	Sint8 addr;
	Uint8 bitC, bitZ;

	bitC = (hd6301_reg_CCR >> hd6301_REG_CCR_C) & 1;
	bitZ = (hd6301_reg_CCR >> hd6301_REG_CCR_Z) & 1;
	addr = 2;
	if ((bitC | bitZ) == 0) {
		addr += hd6301_read_memory(hd6301_reg_PC + 1);
	}
	hd6301_reg_PC += addr;
}

/**
 * BLS : branch if lower or same : C|Z=1
 *
 * HINZVC
 * ......
 */
static void hd6301_bls(void)
{
	Sint8 addr;
	Uint8 bitC, bitZ;

	bitC = (hd6301_reg_CCR >> hd6301_REG_CCR_C) & 1;
	bitZ = (hd6301_reg_CCR >> hd6301_REG_CCR_Z) & 1;
	addr = 2;
	if ((bitC | bitZ) == 1) {
		addr += hd6301_read_memory(hd6301_reg_PC + 1);
	}
	hd6301_reg_PC += addr;
}

/**
 * BCC : branch if carry clear : C=0
 *
 * HINZVC
 * ......
 */
static void hd6301_bcc(void)
{
	Sint8 addr;
	Uint8 bitC;

	bitC = (hd6301_reg_CCR >> hd6301_REG_CCR_C) & 1;
	addr = 2;
	if (bitC == 0) {
		addr += hd6301_read_memory(hd6301_reg_PC + 1);
	}
	hd6301_reg_PC += addr;
}

/**
 * BCS : branch if carry set : C=1
 *
 * HINZVC
 * ......
 */
static void hd6301_bcs(void)
{
	Sint8 addr;
	Uint8 bitC;

	bitC = (hd6301_reg_CCR >> hd6301_REG_CCR_C) & 1;
	addr = 2;
	if (bitC == 1) {
		addr += hd6301_read_memory(hd6301_reg_PC + 1);
	}
	hd6301_reg_PC += addr;
}

/**
 * BNE : branch if not equal 0 : Z=0
 *
 * HINZVC
 * ......
 */
static void hd6301_bne(void)
{
	Sint8 addr;
	Uint8 bitZ;

	bitZ = (hd6301_reg_CCR >> hd6301_REG_CCR_Z) & 1;
	addr = 2;
	if (bitZ == 0) {
		addr += hd6301_read_memory(hd6301_reg_PC + 1);
	}
	hd6301_reg_PC += addr;
}

/**
 * BEQ : branch if equal 0 : Z=1
 *
 * HINZVC
 * ......
 */
static void hd6301_beq(void)
{
	Sint8 addr;
	Uint8 bitZ;

	bitZ = (hd6301_reg_CCR >> hd6301_REG_CCR_Z) & 1;
	addr = 2;
	if (bitZ == 1) {
		addr += hd6301_read_memory(hd6301_reg_PC + 1);
	}
	hd6301_reg_PC += addr;
}

/**
 * BVC : branch if overflow clear : V=0
 *
 * HINZVC
 * ......
 */
static void hd6301_bvc(void)
{
	Sint8 addr;
	Uint8 bitV;

	bitV = (hd6301_reg_CCR >> hd6301_REG_CCR_V) & 1;
	addr = 2;
	if (bitV == 0) {
		addr += hd6301_read_memory(hd6301_reg_PC + 1);
	}
	hd6301_reg_PC += addr;
}

/**
 * BVS : branch if overflow set : V=1
 *
 * HINZVC
 * ......
 */
static void hd6301_bvs(void)
{
	Sint8 addr;
	Uint8 bitV;

	bitV = (hd6301_reg_CCR >> hd6301_REG_CCR_V) & 1;
	addr = 2;
	if (bitV == 1) {
		addr += hd6301_read_memory(hd6301_reg_PC + 1);
	}
	hd6301_reg_PC += addr;
}

/**
 * BPL : branch if plus : N=0
 *
 * HINZVC
 * ......
 */
static void hd6301_bpl(void)
{
	Sint8 addr;
	Uint8 bitN;

	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	addr = 2;
	if (bitN == 0) {
		addr += hd6301_read_memory(hd6301_reg_PC + 1);
	}
	hd6301_reg_PC += addr;
}

/**
 * BMI : branch if minus : N=1
 *
 * HINZVC
 * ......
 */
static void hd6301_bmi(void)
{
	Sint8 addr;
	Uint8 bitN;

	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	addr = 2;
	if (bitN == 1) {
		addr += hd6301_read_memory(hd6301_reg_PC + 1);
	}
	hd6301_reg_PC += addr;
}

/**
 * BGE : branch if greater or equal to zero : N^V=0
 *
 * HINZVC
 * ......
 */
static void hd6301_bge(void)
{
	Sint8 addr;
	Uint8 bitN, bitV;

	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	bitV = (hd6301_reg_CCR >> hd6301_REG_CCR_V) & 1;
	addr = 2;
	if ((bitN ^ bitV) == 0) {
		addr += hd6301_read_memory(hd6301_reg_PC + 1);
	}
	hd6301_reg_PC += addr;
}

/**
 * BLT : branch if lower to zero : N^V=1
 *
 * HINZVC
 * ......
 */
static void hd6301_blt(void)
{
	Sint8 addr;
	Uint8 bitN, bitV;

	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	bitV = (hd6301_reg_CCR >> hd6301_REG_CCR_V) & 1;
	addr = 2;
	if ((bitN ^ bitV) == 1) {
		addr += hd6301_read_memory(hd6301_reg_PC + 1);
	}
	hd6301_reg_PC += addr;
}

/**
 * BGT : branch if greater to zero : Z|(N^V)=0
 *
 * HINZVC
 * ......
 */
static void hd6301_bgt(void)
{
	Sint8 addr;
	Uint8 bitN, bitV, bitZ;

	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	bitV = (hd6301_reg_CCR >> hd6301_REG_CCR_V) & 1;
	bitZ = (hd6301_reg_CCR >> hd6301_REG_CCR_Z) & 1;
	addr = 2;
	if ((bitZ | (bitN ^ bitV)) == 0) {
		addr += hd6301_read_memory(hd6301_reg_PC + 1);
	}
	hd6301_reg_PC += addr;
}

/**
 * BLE : branch if lower or equal to zero : Z|(N^V)=1
 *
 * HINZVC
 * ......
 */
static void hd6301_ble(void)
{
	Sint8 addr;
	Uint8 bitN, bitV, bitZ;

	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	bitV = (hd6301_reg_CCR >> hd6301_REG_CCR_V) & 1;
	bitZ = (hd6301_reg_CCR >> hd6301_REG_CCR_Z) & 1;
	addr = 2;
	if ((bitZ | (bitN ^ bitV)) == 1) {
		addr += hd6301_read_memory(hd6301_reg_PC + 1);
	}
	hd6301_reg_PC += addr;
}

/**
 * TSX : transfer stack pointer to register X : X=SP+1
 *
 * HINZVC
 * ......
 */
static void hd6301_tsx(void)
{
	hd6301_reg_X = hd6301_reg_SP + 1;
}

/**
 * INS : increment stack pointer : SP=SP+1
 *
 * HINZVC
 * ......
 */
static void hd6301_ins(void)
{
	++ hd6301_reg_SP;
}

/**
 * PULA : pull accumulator A from stack : SP=SP+1 ; A=(SP)
 *
 * HINZVC
 * ......
 */
static void hd6301_pula(void)
{
	++ hd6301_reg_SP;
	hd6301_reg_A = hd6301_read_memory(hd6301_reg_SP);
}

/**
 * PULB : pull accumulator B from stack : SP=SP+1 ; B=(SP)
 *
 * HINZVC
 * ......
 */
static void hd6301_pulb(void)
{
	++ hd6301_reg_SP;
	hd6301_reg_B = hd6301_read_memory(hd6301_reg_SP);
}

/**
 * DES : decrement stack pointer : SP=SP-1
 *
 * HINZVC
 * ......
 */
static void hd6301_des(void)
{
	-- hd6301_reg_SP;
}

/**
 * TXS : transfer register X to stack pointer : SP=X-1
 *
 * HINZVC
 * ......
 */
static void hd6301_txs(void)
{
	hd6301_reg_SP = hd6301_reg_X - 1;
}

/**
 * PSHA : push accumulator A to stack : (SP)=A ; SP=SP-1
 *
 * HINZVC
 * ......
 */
static void hd6301_psha(void)
{
	hd6301_write_memory(hd6301_reg_SP, hd6301_reg_A);
	-- hd6301_reg_SP;
}

/**
 * PSHB : push accumulator B to stack : (SP)=B ; SP=SP-1
 *
 * HINZVC
 * ......
 */
static void hd6301_pshb(void)
{
	hd6301_write_memory(hd6301_reg_SP, hd6301_reg_B);
	-- hd6301_reg_SP;
}

/**
 * PULX : pull register X from stack : SP=SP+1 ; X=(SP)
 *
 * HINZVC
 * ......
 */
static void hd6301_pulx(void)
{
	hd6301_reg_X = hd6301_read_memory(++hd6301_reg_SP)<<8;
	hd6301_reg_X += hd6301_read_memory(++hd6301_reg_SP);
}

/**
 * RTS : return from subroutine
 *
 * HINZVC
 * ......
 */
static void hd6301_rts(void)
{
	hd6301_reg_PC = hd6301_read_memory(++hd6301_reg_SP)<<8;
	hd6301_reg_PC += hd6301_read_memory(++hd6301_reg_SP);
}

/**
 * ABX : add accumulator B to register X : X=X+B
 *
 * HINZVC
 * ......
 */
static void hd6301_abx(void)
{
	hd6301_reg_X += hd6301_reg_B;
}

/**
 * RTI : return from interrupt
 *
 * HINZVC
 * ******
 */
static void hd6301_rti(void)
{
	hd6301_reg_CCR = hd6301_read_memory(++hd6301_reg_SP);
	hd6301_reg_B = hd6301_read_memory(++hd6301_reg_SP);
	hd6301_reg_A = hd6301_read_memory(++hd6301_reg_SP);
	hd6301_reg_X = hd6301_read_memory(++hd6301_reg_SP)<<8;
	hd6301_reg_X += hd6301_read_memory(++hd6301_reg_SP);
	hd6301_reg_PC = hd6301_read_memory(++hd6301_reg_SP)<<8;
	hd6301_reg_PC += hd6301_read_memory(++hd6301_reg_SP);
}

/**
 * PSHX : push register X to stack : (SP)=X ; SP=SP-1
 *
 * HINZVC
 * ......
 */
static void hd6301_pshx(void)
{
	hd6301_write_memory(hd6301_reg_SP--, hd6301_reg_X & 0xff);
	hd6301_write_memory(hd6301_reg_SP--, hd6301_reg_X >> 8);
}

/**
 * MUL : multiply unsigned : D=A*B
 *
 * HINZVC
 * .....*
 */
static void hd6301_mul(void)
{
	Uint16 regD;

	regD = hd6301_reg_B * hd6301_reg_A;
	hd6301_reg_A = regD >> 8;
	hd6301_reg_B = regD;

	HD6301_CLR_C;
	hd6301_reg_CCR |= hd6301_reg_B >> 7;
}

/**
 * WAI : wait for interrupt
 *
 * HINZVC
 * .*....
 */
static void hd6301_wai(void)
{
	/* Todo */
}

/**
 * SWI : software interrupt
 *
 * HINZVC
 * .1....
 */
static void hd6301_swi(void)
{
	hd6301_write_memory(hd6301_reg_SP--, (hd6301_reg_PC+1) & 0xff);
	hd6301_write_memory(hd6301_reg_SP--, (hd6301_reg_PC+1) >> 8);
	hd6301_write_memory(hd6301_reg_SP--, hd6301_reg_X & 0xff);
	hd6301_write_memory(hd6301_reg_SP--, hd6301_reg_X >> 8);
	hd6301_write_memory(hd6301_reg_SP--, hd6301_reg_A);
	hd6301_write_memory(hd6301_reg_SP--, hd6301_reg_B);
	hd6301_write_memory(hd6301_reg_SP--, hd6301_reg_CCR);

	hd6301_reg_PC = hd6301_read_memory(0xfffa) << 8;
	hd6301_reg_PC += hd6301_read_memory(0xfffb);

	hd6301_reg_CCR |= 1 << hd6301_REG_CCR_I;
}

/**
 * NEGA : negate accumulator A : A=0-A
 *
 * HINZVC
 * ..****
 */
static void hd6301_nega(void)
{
	Uint8 value;

	value = 0 - hd6301_reg_A;
	hd6301_reg_A = value;

	HD6301_CLR_NZVC;
	HD6301_SET_NZ8(value);
	hd6301_reg_CCR |= (value != 0x0);
	hd6301_reg_CCR |= (value == 0x80) << hd6301_REG_CCR_V;
}

/**
 * COMA : complement 1 accumulator A : A=~A
 *
 * HINZVC
 * ..**01
 */
static void hd6301_coma(void)
{
	hd6301_reg_A = ~hd6301_reg_A;

	HD6301_CLR_NZV;
	hd6301_reg_CCR |= 1;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * LSRA : logical shift right, accumulator A : A=A>>1
 *
 * HINZVC
 * ..0***
 */
static void hd6301_lsra(void)
{
	Uint8  carry;

	carry = hd6301_reg_A & 1;
	hd6301_reg_A >>= 1;

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_Z8(hd6301_reg_A);
	hd6301_reg_CCR |= ((0 ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * RORA : rotate right, accumulator A : A=A>>1 + carry<<8
 *
 * HINZVC
 * ..****
 */
static void hd6301_rora(void)
{
	Uint8  carry, result, bitN;

	carry = hd6301_reg_A & 1;
	result = (hd6301_reg_CCR & 1) << 7;
	result += hd6301_reg_A >> 1;
	hd6301_reg_A = result;

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_NZ8(hd6301_reg_A);
	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	hd6301_reg_CCR |= ((bitN ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * ASRA : arithmetic shift right, accumulator A : A=A>>1
 *
 * HINZVC
 * ..****
 */
static void hd6301_asra(void)
{
	Uint8  carry, bitN;

	carry = hd6301_reg_A & 1;
	hd6301_reg_A >>= 1;
	hd6301_reg_A |= (hd6301_reg_A & 0x40) << 1;

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_NZ8(hd6301_reg_A);
	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	hd6301_reg_CCR |= ((bitN ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * ASLA : arithmetic shift left, accumulator A : A=A<<1
 *
 * HINZVC
 * ..****
 */
static void hd6301_asla(void)
{
	Uint8  carry, bitN;

	carry = (hd6301_reg_A & 0X80) >> 7;
	hd6301_reg_A <<= 1;

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_NZ8(hd6301_reg_A);
	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	hd6301_reg_CCR |= ((bitN ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * ROLA : rotate left, accumulator A : A=A<<1 +C
 *
 * HINZVC
 * ..****
 */
static void hd6301_rola(void)
{
	Uint8  carry, result, bitN;

	carry = (hd6301_reg_A & 0x80) >> 7;
	result = hd6301_reg_CCR & 1;
	result += hd6301_reg_A << 1;
	hd6301_reg_A  = result;

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_NZ8(hd6301_reg_A);
	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	hd6301_reg_CCR |= ((bitN ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * DECA : decrement accumulator A : A=A-1
 *
 * HINZVC
 * ..***.
 */
static void hd6301_deca(void)
{
	Uint8 overflow;

	overflow = (hd6301_reg_A == (Sint8)0x80) << hd6301_REG_CCR_V;
	-- hd6301_reg_A;

	HD6301_CLR_NZV;
	hd6301_reg_CCR |= overflow;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * INCA : increment accumulator A : A=A+1
 *
 * HINZVC
 * ..***.
 */
static void hd6301_inca(void)
{
	Uint8 overflow;

	overflow = (hd6301_reg_A == 0x7f) << hd6301_REG_CCR_V;
	hd6301_reg_A ++;

	HD6301_CLR_NZV;
	hd6301_reg_CCR |= overflow;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * TSTA : test zero or minus, accumulator A : A-0
 *
 * HINZVC
 * ..**00
 */
static void hd6301_tsta(void)
{
	HD6301_CLR_NZVC;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * CLRA : clear accumulator A : A=0
 *
 * HINZVC
 * ..0100
 */
static void hd6301_clra(void)
{
	hd6301_reg_A = 0;
	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= 1 << hd6301_REG_CCR_Z;
}

/**
 * NEGB : negate accumulator B : B=0-B
 *
 * HINZVC
 * ..****
 */
static void hd6301_negb(void)
{
	Uint8 value;

	value = 0 - hd6301_reg_B;
	hd6301_reg_B = value;

	HD6301_CLR_NZVC;
	HD6301_SET_NZ8(value);
	hd6301_reg_CCR |= (value != 0x0);
	hd6301_reg_CCR |= (value == 0x80) << hd6301_REG_CCR_V;
}

/**
 * COMB : complement 1 accumulator B : B=~B
 *
 * HINZVC
 * ..**01
 */
static void hd6301_comb(void)
{
	hd6301_reg_B = ~hd6301_reg_B;
	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= 1;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * LSRB : logical shift right, accumulator B : B=B>>1
 *
 * HINZVC
 * ..0***
 */
static void hd6301_lsrb(void)
{
	Uint8  carry;

	carry = hd6301_reg_B & 1;
	hd6301_reg_B >>= 1;

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_Z8(hd6301_reg_B);
	hd6301_reg_CCR |= ((0 ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * RORB : rotate right, accumulator B : B=B>>1 + carry<<8
 *
 * HINZVC
 * ..****
 */
static void hd6301_rorb(void)
{
	Uint8  carry, result, bitN;

	carry = hd6301_reg_B & 1;
	result = (hd6301_reg_CCR & 1) << 7;
	result += hd6301_reg_B >> 1;
	hd6301_reg_B = result;

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_NZ8(hd6301_reg_B);
	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	hd6301_reg_CCR |= ((bitN ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * ASRB : arithmetic shift right, accumulator B : B=B>>1
 *
 * HINZVC
 * ..****
 */
static void hd6301_asrb(void)
{
	Uint8  carry, bitN;

	carry = hd6301_reg_B & 1;
	hd6301_reg_B >>= 1;
	hd6301_reg_B |= (hd6301_reg_B & 0x40) << 1;

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_NZ8(hd6301_reg_B);
	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	hd6301_reg_CCR |= ((bitN ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * ASLB : arithmetic shift left, accumulator B : B=B<<1
 *
 * HINZVC
 * ..****
 */
static void hd6301_aslb(void)
{
	Uint8  carry, bitN;

	carry = (hd6301_reg_B & 0x80) >> 7;
	hd6301_reg_B <<= 1;

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_NZ8(hd6301_reg_B);
	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	hd6301_reg_CCR |= ((bitN ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * ROLB : rotate left, accumulator B : B=B<<1 +C
 *
 * HINZVC
 * ..****
 */
static void hd6301_rolb(void)
{
	Uint8  carry, result, bitN;

	carry = (hd6301_reg_B & 0x80) >> 7;
	result = hd6301_reg_CCR & 1;
	result += hd6301_reg_B << 1;
	hd6301_reg_B  = result;

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_NZ8(hd6301_reg_B);
	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	hd6301_reg_CCR |= ((bitN ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * DECB : decrement accumulator B : B=B-1
 *
 * HINZVC
 * ..***.
 */
static void hd6301_decb(void)
{
	Uint8 overflow;

	overflow = (hd6301_reg_B == (Sint8)0x80) << hd6301_REG_CCR_V;
	-- hd6301_reg_B;

	HD6301_CLR_NZV;
	hd6301_reg_CCR |= overflow;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * INCB : increment accumulator B : B=B+1
 *
 * HINZVC
 * ..***.
 */
static void hd6301_incb(void)
{
	Uint8 overflow;

	overflow = (hd6301_reg_B == 0x7f) << hd6301_REG_CCR_V;
	hd6301_reg_B ++;

	HD6301_CLR_NZV;
	hd6301_reg_CCR |= overflow;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * TSTB : test zero or minus, accumulator B : B-0
 *
 * HINZVC
 * ..**00
 */
static void hd6301_tstb(void)
{
	HD6301_CLR_NZVC;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * CLRB : clear accumulator B : B=0
 *
 * HINZVC
 * ..0100
 */
static void hd6301_clrb(void)
{
	hd6301_reg_B = 0;
	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= 1 << hd6301_REG_CCR_Z;
}

/**
 * NEG_IND : negate indexed memory : M=0-M
 *
 * HINZVC
 * ..****
 */
static void hd6301_neg_ind(void)
{
	Uint8  value;
	Uint16 addr;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = -hd6301_read_memory(addr);
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= (value != 0x0);
	hd6301_reg_CCR |= (value == 0x80) << hd6301_REG_CCR_V;
	HD6301_SET_NZ8(value);
}

/**
 * AIM_IND : and immediate indexed memory : M=M&IMM
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_aim_ind(void)
{
	Uint8  value;
	Uint16 addr;

	value = hd6301_read_memory(hd6301_reg_PC+1);
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+2);
	value &= hd6301_read_memory(addr);
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(value);
}

/**
 * OIM_IND : or immediate indexed memory : M=M|IMM
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_oim_ind(void)
{
	Uint8  value;
	Uint16 addr;

	value = hd6301_read_memory(hd6301_reg_PC+1);
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+2);
	value |= hd6301_read_memory(addr);
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(value);
}

/**
 * COM_IND : complement 1 indexed memory : M=~M
 *
 * HINZVC
 * ..**01
 */
static void hd6301_com_ind(void)
{
	Uint8  value;
	Uint16 addr;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = ~hd6301_read_memory(addr);
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZV;
	hd6301_reg_CCR |= 1;
	HD6301_SET_NZ8(value);
}

/**
 * LSR_IND : logical shift right indexed memory : M=M>>1
 *
 * HINZVC
 * ..0***
 */
static void hd6301_lsr_ind(void)
{
	Uint8  value, carry;
	Uint16 addr;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);

	carry = value & 1;
	value >>= 1;
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_Z8(value);
	hd6301_reg_CCR |= ((0 ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * EIM_IND : eor immediate indexed memory : M=M^IMM
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_eim_ind(void)
{
	Uint8  value;
	Uint16 addr;

	value = hd6301_read_memory(hd6301_reg_PC+1);
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+2);
	value ^= hd6301_read_memory(addr);
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(value);
}

/**
 * ROR_IND : rotate right indexed memory : M=M>>1 + carry<<8
 *
 * HINZVC
 * ..****
 */
static void hd6301_ror_ind(void)
{
	Uint8  value, carry, result, bitN;
	Uint16 addr;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);

	carry = value & 1;
	result = (hd6301_reg_CCR & 1) << 7;
	result += value >> 1;
	hd6301_write_memory(addr, result);

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_NZ8(result);
	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	hd6301_reg_CCR |= ((bitN ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * ASR_IND : arithmetic shift right indexed memory : M=M>>1
 *
 * HINZVC
 * ..****
 */
static void hd6301_asr_ind(void)
{
	Uint8  value, carry, bitN;
	Uint16 addr;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);

	carry = value & 1;
	value >>= 1;
	value |= (value & 0x40) << 1;
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_NZ8(value);
	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	hd6301_reg_CCR |= ((bitN ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * ASL_IND : arithmetic shift left indexed memory : M=M<<1
 *
 * HINZVC
 * ..****
 */
static void hd6301_asl_ind(void)
{
	Uint8  value, carry, bitN;
	Uint16 addr;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);

	carry = (value & 0X80) >> 7;
	value <<= 1;
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_NZ8(value);
	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	hd6301_reg_CCR |= ((bitN ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * ROL_IND : rotate left indexed memory : M=M<<1 + carry
 *
 * HINZVC
 * ..****
 */
static void hd6301_rol_ind(void)
{
	Uint8  value, carry, result, bitN;
	Uint16 addr;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);

	result = hd6301_reg_CCR & 1;
	carry = (value & 0x80) >> 7;
	result += value << 1;
	hd6301_write_memory(addr, result);

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_NZ8(result);
	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	hd6301_reg_CCR |= ((bitN ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * DEC_IND : decrement indexed memory : M=M-1
 *
 * HINZVC
 * ..***.
 */
static void hd6301_dec_ind(void)
{
	Uint8  value, overflow;
	Uint16 addr;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);

	overflow = (value == 0x80) << hd6301_REG_CCR_V;
	--value;
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZV;
	hd6301_reg_CCR |= overflow;
	HD6301_SET_NZ8(value);
}

/**
 * TIM_IND : test immediate indexed memory : M&IMM
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_tim_ind(void)
{
	Uint8  value;
	Uint16 addr;

	value = hd6301_read_memory(hd6301_reg_PC+1);
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+2);
	value &= hd6301_read_memory(addr);
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(value);
}

/**
 * INC_IND : increment indexed memory : M=M+1
 *
 * HINZVC
 * ..***.
 */
static void hd6301_inc_ind(void)
{
	Uint8  value, overflow;
	Uint16 addr;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);

	overflow = (value == 0x7f) << hd6301_REG_CCR_V;
	value ++;
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZV;
	hd6301_reg_CCR |= overflow;
	HD6301_SET_NZ8(value);
}

/**
 * TST_IND : test indexed memory : M-0
 *
 * HINZVC
 * ..**00
 */
static void hd6301_tst_ind(void)
{
	Uint8  value;
	Uint16 addr;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);

	HD6301_CLR_NZVC;
	HD6301_SET_NZ8(value);
}

/**
 * JMP_IND : jump to indexed memory address : PC=M
 *
 * HINZVC
 * ......
 */
static void hd6301_jmp_ind(void)
{
	Uint8  value;
	Uint16 addr;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	hd6301_reg_PC = value;
}

/**
 * CLR_IND : clear indexed memory : M=0
 *
 * HINZVC
 * ..0100
 */
static void hd6301_clr_ind(void)
{
	Uint16 addr;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_write_memory(addr, 0);

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= 1 << hd6301_REG_CCR_Z;
}

/**
 * NEG_EXT : negate extended memory : M=0-M
 *
 * HINZVC
 * ..****
 */
static void hd6301_neg_ext(void)
{
	Uint8  value;
	Uint16 addr;

	addr = hd6301_get_memory_ext();
	value = -hd6301_read_memory(addr);
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= (value != 0x0);
	hd6301_reg_CCR |= (value == 0x80) << hd6301_REG_CCR_V;
	HD6301_SET_NZ8(value);
}

/**
 * AIM_DIR : and immediate direct memory address : M=M&IMM
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_aim_dir(void)
{
	Uint8  value;
	Uint16 addr;

	value = hd6301_read_memory(hd6301_reg_PC+1);
	addr = hd6301_read_memory(hd6301_reg_PC+2);
	value &= hd6301_read_memory(addr);
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(value);
}

/**
 * OIM_DIR : or immediate direct memory address : M=M|IMM
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_oim_dir(void)
{
	Uint8  value;
	Uint16 addr;

	value = hd6301_read_memory(hd6301_reg_PC+1);
	addr = hd6301_read_memory(hd6301_reg_PC+2);
	value |= hd6301_read_memory(addr);
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(value);
}

/**
 * COM_EXT : complement 1 extended memory : M=~M
 *
 * HINZVC
 * ..**01
 */
static void hd6301_com_ext(void)
{
	Uint8  value;
	Uint16 addr;

	addr = hd6301_get_memory_ext();
	value = ~hd6301_read_memory(addr);
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZV;
	hd6301_reg_CCR |= 1;
	HD6301_SET_NZ8(value);
}

/**
 * LSR_EXT : logical shift right extended memory : M=M>>1
 *
 * HINZVC
 * ..0***
 */
static void hd6301_lsr_ext(void)
{
	Uint8  value, carry;
	Uint16 addr;

	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr);

	carry = value & 1;
	value >>= 1;
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_Z8(value);
	hd6301_reg_CCR |= ((0 ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * EIM_DIR : eor immediate direct memory address : M=M^IMM
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_eim_dir(void)
{
	Uint8  value;
	Uint16 addr;

	value = hd6301_read_memory(hd6301_reg_PC+1);
	addr = hd6301_read_memory(hd6301_reg_PC+2);
	value ^= hd6301_read_memory(addr);
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(value);
}

/**
 * ROR_EXT : rotate right extended memory : M=M>>1 + carry<<8
 *
 * HINZVC
 * ..****
 */
static void hd6301_ror_ext(void)
{
	Uint8  value, carry, result, bitN;
	Uint16 addr;

	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr);

	result = (hd6301_reg_CCR & 1) << 7;
	carry = value & 1;
	result += value >> 1;
	hd6301_write_memory(addr, result);

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_NZ8(value);
	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	hd6301_reg_CCR |= ((bitN ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * ASR_EXT : arithmetic shift right extended memory : M=M>>1
 *
 * HINZVC
 * ..****
 */
static void hd6301_asr_ext(void)
{
	Uint8  value, carry, bitN;
	Uint16 addr;

	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr);

	carry = value & 1;
	value >>= 1;
	value |= (value & 0x40) << 1;
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_NZ8(value);
	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	hd6301_reg_CCR |= ((bitN ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * ASL_EXT : arithmetic shift left extended memory : M=M<<1
 *
 * HINZVC
 * ..****
 */
static void hd6301_asl_ext(void)
{
	Uint8  value, carry, bitN;
	Uint16 addr;

	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr);

	carry = (value & 0X80) >> 7;
	value <<= 1;
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_NZ8(value);
	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	hd6301_reg_CCR |= ((bitN ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * ROL_EXT : rotate left extended memory : M=M<<1 + carry
 *
 * HINZVC
 * ..****
 */
static void hd6301_rol_ext(void)
{
	Uint8  value, carry, result, bitN;
	Uint16 addr;

	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr);

	result = hd6301_reg_CCR & 1;
	carry = (value & 0x80) >> 7;
	result += value << 1;
	hd6301_write_memory(addr, result);

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= carry;
	HD6301_SET_NZ8(value);
	bitN = (hd6301_reg_CCR >> hd6301_REG_CCR_N) & 1;
	hd6301_reg_CCR |= ((bitN ^ carry) == 1) << hd6301_REG_CCR_V;
}

/**
 * DEC_EXT : decrement extended memory : M=M-1
 *
 * HINZVC
 * ..***.
 */
static void hd6301_dec_ext(void)
{
	Uint8  value, overflow;
	Uint16 addr;

	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr);

	overflow = (value == 0x80) << hd6301_REG_CCR_V;
	--value;
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZV;
	hd6301_reg_CCR |= overflow;
	HD6301_SET_NZ8(value);
}

/**
 * TIM_DIR : test direct memory address value : M&IMM
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_tim_dir(void)
{
	Uint8  value;
	Uint16 addr;

	value = hd6301_read_memory(hd6301_reg_PC+1);
	addr = hd6301_read_memory(hd6301_reg_PC+2);
	value &= hd6301_read_memory(addr);
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(value);
}

/**
 * INC_EXT : increment extended memory : M=M+1
 *
 * HINZVC
 * ..***.
 */
static void hd6301_inc_ext(void)
{
	Uint8  value, overflow;
	Uint16 addr;

	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr);

	overflow = (value == 0x7f) << hd6301_REG_CCR_V;
	value ++;
	hd6301_write_memory(addr, value);

	HD6301_CLR_NZV;
	hd6301_reg_CCR |= overflow;
	HD6301_SET_NZ8(value);
}

/**
 * TST_EXT : test extended memory : M-0
 *
 * HINZVC
 * ..**00
 */
static void hd6301_tst_ext(void)
{
	Uint8  value;
	Uint16 addr;

	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr);

	HD6301_CLR_NZVC;
	HD6301_SET_NZ8(value);
}

/**
 * JMP_EXT : jump to extended memory address : PC=M
 *
 * HINZVC
 * ......
 */
static void hd6301_jmp_ext(void)
{
	Uint8  value;
	Uint16 addr;

	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr);

	hd6301_reg_PC = value;
}

/**
 * CLR_EXT : clear extended memory : M=0
 *
 * HINZVC
 * ..0100
 */
static void hd6301_clr_ext(void)
{
	Uint16 addr;

	addr = hd6301_get_memory_ext();
	hd6301_write_memory(addr, 0);

	HD6301_CLR_NZVC;
	hd6301_reg_CCR |= 1 << hd6301_REG_CCR_Z;
}

/**
 * SUBA_IMM : subtract immediate value from accumulator A : A=A-M
 *
 * HINZVC
 * ..****
 */
static void hd6301_suba_imm(void)
{
	Uint8  value;
	Uint16 result;

	value = hd6301_read_memory(hd6301_reg_PC+1);
	result = hd6301_reg_A - value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, value, result);

	hd6301_reg_A = result;
}

/**
 * CMPA_IMM : compare immediate value to accumulator A : A-M
 *
 * HINZVC
 * ..****
 */
static void hd6301_cmpa_imm(void)
{
	Uint8  value;
	Uint16 result;

	value = hd6301_read_memory(hd6301_reg_PC+1);
	result = hd6301_reg_A - value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, value, result);
}

/**
 * SBCA_IMM : subtract with carry immediate value from accumulator A : A=A-M-C
 *
 * HINZVC
 * ..****
 */
static void hd6301_sbca_imm(void)
{
	Uint8  value, carry;
	Uint16 result;

	carry = hd6301_REG_CCR_C & 1;
	value = hd6301_read_memory(hd6301_reg_PC+1);
	result = hd6301_reg_A - value - carry;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, value, result);

	hd6301_reg_A = result;
}

/**
 * SUBD_IMM : subtract immediate value from accumulator D : D=D-MM
 *
 * HINZVC
 * ..****
 */
static void hd6301_subd_imm(void)
{
	Uint16 value, regD;
	Uint32 result;

	value = hd6301_read_memory(hd6301_reg_PC+1) << 8;
	value += hd6301_read_memory(hd6301_reg_PC+2);
	regD = (hd6301_reg_A << 8) + hd6301_reg_B;
	result = regD - value;

	hd6301_reg_A = (result >> 8) & 0xff;
	hd6301_reg_B = result & 0xff;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS16(regD, value, result);
}

/**
 * ANDA_IMM : and immediate value with accumulator A : A=A&M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_anda_imm(void)
{
	hd6301_reg_A &= hd6301_read_memory(hd6301_reg_PC+1);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * BITA_IMM : bit test immediate value with accumulator A : A&M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_bita_imm(void)
{
	Uint8 value;
	
	value = hd6301_reg_A & hd6301_read_memory(hd6301_reg_PC+1);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(value);
}

/**
 * LDAA_IMM : load accumulator A with immediate value : A=M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_ldaa_imm(void)
{
	hd6301_reg_A = hd6301_read_memory(hd6301_reg_PC+1);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * EORA_IMM : exclusive or immediate value with accumulator A : A=A^M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_eora_imm(void)
{
	hd6301_reg_A ^= hd6301_read_memory(hd6301_reg_PC+1);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * ADCA_IMM : add with carry immediate value to accumulator A : A=A+M+C
 *
 * HINZVC
 * *.****
 */
static void hd6301_adca_imm(void)
{
	Uint8  value, carry;
	Uint16 result;

	carry = hd6301_REG_CCR_C & 1;
	value = hd6301_read_memory(hd6301_reg_PC+1);
	result = hd6301_reg_A + value + carry;

	HD6301_CLR_HNZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, value, result);
	HD6301_SET_H(hd6301_reg_A, value, result);

	hd6301_reg_A = result;
}

/**
 * ORAA_IMM : inclusive or accumulator A with immediate value : A=A|M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_oraa_imm(void)
{
	hd6301_reg_A |= hd6301_read_memory(hd6301_reg_PC+1);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * ADDA_IMM : add immediate value with accumulator A : A=A+M
 *
 * HINZVC
 * *.****
 */
static void hd6301_adda_imm(void)
{
	Uint8  value;
	Uint16 result;

	value = hd6301_read_memory(hd6301_reg_PC+1);
	result = hd6301_reg_A + value;

	HD6301_CLR_HNZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, value, result);
	HD6301_SET_H(hd6301_reg_A, value, result);

	hd6301_reg_A = result;
}

/**
 * CPX_IMM : compare index register with immediate value : X-MM
 *
 * HINZVC
 * ..****
 */
static void hd6301_cpx_imm(void)
{
	Uint16 value;
	Uint32 result;

	value = hd6301_read_memory(hd6301_reg_PC+1) << 8;
	value += hd6301_read_memory(hd6301_reg_PC+2);
	result = hd6301_reg_X - value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS16(hd6301_reg_X, value, result);
}

/**
 * BSR : branch to subroutine
 *
 * HINZVC
 * ......
 */
static void hd6301_bsr(void)
{
	Sint8 addr;

	hd6301_write_memory(hd6301_reg_SP--, (hd6301_reg_PC + 2) & 0xff);
	hd6301_write_memory(hd6301_reg_SP--, (hd6301_reg_PC + 2) >> 8);

	addr = hd6301_read_memory(hd6301_reg_PC + 1);
	hd6301_reg_PC += addr + 2;
}

/**
 * LDS_IMM : load stack pointer with immediate value : SP=MM
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_lds_imm(void)
{
	Uint16 value;
	
	value = hd6301_read_memory(hd6301_reg_PC+1) << 8;
	value += hd6301_read_memory(hd6301_reg_PC+2);
	hd6301_reg_SP = value;

	HD6301_CLR_NZV;
	HD6301_SET_NZ16(value);
}

/**
 * SUBA_DIR : subtract direct memory address value from accumulator A : A=A-M
 *
 * HINZVC
 * ..****
 */
static void hd6301_suba_dir(void)
{
	Uint8  value;
	Uint16 result, addr;

	addr = hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	result = hd6301_reg_A - value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, value, result);

	hd6301_reg_A = result;
}

/**
 * CMPA_DIR : compare direct memory address value to accumulator A : A-M
 *
 * HINZVC
 * ..****
 */
static void hd6301_cmpa_dir(void)
{
	Uint8  value;
	Uint16 addr, result;

	addr = hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	result = hd6301_reg_A - value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, value, result);
}

/**
 * SBCA_DIR : subtract with carry direct memory address value from accumulator A : A=A-M-C
 *
 * HINZVC
 * ..****
 */
static void hd6301_sbca_dir(void)
{
	Uint8  value, carry;
	Uint16 addr, result;

	carry = hd6301_REG_CCR_C & 1;
	addr = hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	result = hd6301_reg_A - value - carry;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, value, result);

	hd6301_reg_A = result;
}

/**
 * SUBD_DIR : subtract direct memory address value from accumulator D : D=D-MM
 *
 * HINZVC
 * ..****
 */
static void hd6301_subd_dir(void)
{
	Uint16 addr, value, regD;
	Uint32 result;

	addr = hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr) << 8;
	value += hd6301_read_memory(addr+1);
	regD = (hd6301_reg_A << 8) + hd6301_reg_B;
	result = regD - value;

	hd6301_reg_A = (result >> 8) & 0xff;
	hd6301_reg_B = result & 0xff;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS16(regD, value, result);
}

/**
 * ANDA_DIR : and direct memory address value with accumulator A : A=A&M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_anda_dir(void)
{
	Uint16 addr;
	
	addr = hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_reg_A &= hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * BITA_DIR : bit test direct memory address value with accumulator A : A&M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_bita_dir(void)
{
	Uint8 value;
	Uint16 addr;
	
	addr = hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_reg_A & hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(value);
}

/**
 * LDAA_DIR : load accumulator A with direct memory address value : A=M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_ldaa_dir(void)
{
	Uint16 addr;
	
	addr = hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_reg_A = hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * STAA_DIR : store accumulator A into direct memory address value : M=A
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_staa_dir(void)
{
	Uint16 addr;

	addr = hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_write_memory(addr, hd6301_reg_A);
	
	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * EORA_DIR : exclusive or direct memory address value with accumulator A : A=A^M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_eora_dir(void)
{
	Uint16 addr;

	addr = hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_reg_A ^= hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * ADCA_DIR : add with carry direct memory address value to accumulator A : A=A+M+C
 *
 * HINZVC
 * *.****
 */
static void hd6301_adca_dir(void)
{
	Uint8  value, carry;
	Uint16 addr, result;

	carry = hd6301_REG_CCR_C & 1;
	addr = hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	result = hd6301_reg_A + value + carry;

	HD6301_CLR_HNZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, value, result);
	HD6301_SET_H(hd6301_reg_A, value, result);

	hd6301_reg_A = result;
}

/**
 * ORAA_DIR : inclusive or accumulator A with direct memory address value : A=A|M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_oraa_dir(void)
{
	Uint16 addr;
	
	addr = hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_reg_A |= hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * ADDA_DIR : add direct memory address value with accumulator A : A=A+M
 *
 * HINZVC
 * *.****
 */
static void hd6301_adda_dir(void)
{
	Uint8  value;
	Uint16 addr, result;

	addr = hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	result = hd6301_reg_A + value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, value, result);
	HD6301_SET_H(hd6301_reg_A, value, result);

	hd6301_reg_A = result;
}

/**
 * CPX_DIR : compare index register with direct memory address value : X-MM
 *
 * HINZVC
 * ..****
 */
static void hd6301_cpx_dir(void)
{
	Uint16 addr, value;
	Uint32 result;

	addr = hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr) << 8;
	value += hd6301_read_memory(addr+1);
	result = hd6301_reg_X - value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS16(hd6301_reg_X, value, result);
}

/**
 * JSR_DIR : jump to subroutine at direct memory address
 *
 * HINZVC
 * ......
 */
static void hd6301_jsr_dir(void)
{
	Uint16 addr;

	hd6301_write_memory(hd6301_reg_SP--, (hd6301_reg_PC + 2) & 0xff);
	hd6301_write_memory(hd6301_reg_SP--, (hd6301_reg_PC + 2) >> 8);

	addr = hd6301_read_memory(hd6301_reg_PC + 1);
	hd6301_reg_PC += addr + 2;
}

/**
 * LDS_DIR : load stack pointer with direct memory address value : SP=MM
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_lds_dir(void)
{
	Uint16 addr;
	
	addr = hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_reg_SP = hd6301_read_memory(addr) << 8;
	hd6301_reg_SP += hd6301_read_memory(addr+1);

	HD6301_CLR_NZV;
	HD6301_SET_NZ16(hd6301_reg_SP);
}

/**
 * STS_DIR : store stack pointer into direct memory address value : MM=SP
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_sts_dir(void)
{
	Uint16 addr;
	
	addr = hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_write_memory(addr, hd6301_reg_SP >> 8);
	hd6301_write_memory(addr+1, hd6301_reg_SP & 8);

	HD6301_CLR_NZV;
	HD6301_SET_NZ16(hd6301_reg_SP);
}

/**
 * SUBA_IND : subtract indexed memory address value from accumulator A : A=A-M
 *
 * HINZVC
 * ..****
 */
static void hd6301_suba_ind(void)
{
	Uint8  value;
	Uint16 result, addr;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	result = hd6301_reg_A - value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, value, result);

	hd6301_reg_A = result;
}

/**
 * CMPA_IND : compare indexed memory address value to accumulator A : A-M
 *
 * HINZVC
 * ..****
 */
static void hd6301_cmpa_ind(void)
{
	Uint8  value;
	Uint16 addr, result;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	result = hd6301_reg_A - value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, value, result);
}

/**
 * SBCA_IND : subtract with carry indexed memory address value from accumulator A : A=A-M-C
 *
 * HINZVC
 * ..****
 */
static void hd6301_sbca_ind(void)
{
	Uint8  value, carry;
	Uint16 addr, result;

	carry = hd6301_REG_CCR_C & 1;
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	result = hd6301_reg_A - value - carry;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, value, result);

	hd6301_reg_A = result;
}

/**
 * SUBD_IND : subtract indexed memory address value from accumulator D : D=D-MM
 *
 * HINZVC
 * ..****
 */
static void hd6301_subd_ind(void)
{
	Uint16 addr, value, regD;
	Uint32 result;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr) << 8;
	value += hd6301_read_memory(addr+1);
	regD = (hd6301_reg_A << 8) + hd6301_reg_B;
	result = regD - value;

	hd6301_reg_A = (result >> 8) & 0xff;
	hd6301_reg_B = result & 0xff;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS16(regD, value, result);
}

/**
 * ANDA_IND : and indexed memory address value with accumulator A : A=A&M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_anda_ind(void)
{
	Uint16 addr;
	
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_reg_A &= hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * BITA_IND : bit test indexed memory address value with accumulator A : A&M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_bita_ind(void)
{
	Uint8 value;
	Uint16 addr;
	
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_reg_A & hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(value);
}

/**
 * LDAA_IND : load accumulator A with indexed memory address value : A=M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_ldaa_ind(void)
{
	Uint16 addr;
	
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_reg_A = hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * STAA_IND : store accumulator A into indexed memory address value : M=A
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_staa_ind(void)
{
	Uint16 addr;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_write_memory(addr, hd6301_reg_A);
	
	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * EORA_IND : exclusive or indexed memory address value with accumulator A : A=A^M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_eora_ind(void)
{
	Uint16 addr;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_reg_A ^= hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * ADCA_IND : add with carry indexed memory address value to accumulator A : A=A+M+C
 *
 * HINZVC
 * *.****
 */
static void hd6301_adca_ind(void)
{
	Uint8  value, carry;
	Uint16 addr, result;

	carry = hd6301_REG_CCR_C & 1;
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	result = hd6301_reg_A + value + carry;

	HD6301_CLR_HNZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, value, result);
	HD6301_SET_H(hd6301_reg_A, value, result);

	hd6301_reg_A = result;
}

/**
 * ORAA_IND : inclusive or accumulator A with indexed memory address value : A=A|M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_oraa_ind(void)
{
	Uint16 addr;
	
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_reg_A |= hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * ADDA_IND : add indexed memory address value with accumulator A : A=A+M
 *
 * HINZVC
 * *.****
 */
static void hd6301_adda_ind(void)
{
	Uint8  value;
	Uint16 addr, result;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	result = hd6301_reg_A + value;

	HD6301_CLR_HNZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, value, result);
	HD6301_SET_H(hd6301_reg_A, value, result);

	hd6301_reg_A = result;
}

/**
 * CPX_IND : compare index register with indexed memory address value : X-MM
 *
 * HINZVC
 * ..****
 */
static void hd6301_cpx_ind(void)
{
	Uint16 addr, value;
	Uint32 result;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr) << 8;
	value += hd6301_read_memory(addr+1);
	result = hd6301_reg_X - value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS16(hd6301_reg_X, value, result);
}

/**
 * JSR_IND : jump to subroutine at indexed address
 *
 * HINZVC
 * ......
 */
static void hd6301_jsr_ind(void)
{
	Uint16 addr;

	hd6301_write_memory(hd6301_reg_SP--, (hd6301_reg_PC + 2) & 0xff);
	hd6301_write_memory(hd6301_reg_SP--, (hd6301_reg_PC + 2) >> 8);

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_reg_PC += addr + 2;
}

/**
 * LDS_IND : load stack pointer with indexed memory address value : SP=MM
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_lds_ind(void)
{
	Uint16 addr;
	
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_reg_SP = hd6301_read_memory(addr) << 8;
	hd6301_reg_SP += hd6301_read_memory(addr+1);

	HD6301_CLR_NZV;
	HD6301_SET_NZ16(hd6301_reg_SP);
}

/**
 * STS_IND : store stack pointer into indexed memory address value : MM=SP
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_sts_ind(void)
{
	Uint16 addr;
	
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_write_memory(addr, hd6301_reg_SP >> 8);
	hd6301_write_memory(addr+1, hd6301_reg_SP & 8);

	HD6301_CLR_NZV;
	HD6301_SET_NZ16(hd6301_reg_SP);
}

/**
 * SUBA_EXT : subtract extented memory address value from accumulator A : A=A-M
 *
 * HINZVC
 * ..****
 */
static void hd6301_suba_ext(void)
{
	Uint8  value;
	Uint16 result, addr;

	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr);
	result = hd6301_reg_A - value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, value, result);

	hd6301_reg_A = result;
}

/**
 * CMPA_EXT : compare extented memory address value to accumulator A : A-M
 *
 * HINZVC
 * ..****
 */
static void hd6301_cmpa_ext(void)
{
	Uint8  value;
	Uint16 addr, result;

	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr);
	result = hd6301_reg_A - value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, value, result);
}

/**
 * SBCA_EXT : subtract with carry extented memory address value from accumulator A : A=A-M-C
 *
 * HINZVC
 * ..****
 */
static void hd6301_sbca_ext(void)
{
	Uint8  value, carry;
	Uint16 addr, result;

	carry = hd6301_REG_CCR_C & 1;
	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr);
	result = hd6301_reg_A - value - carry;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, value, result);

	hd6301_reg_A = result;
}

/**
 * SUBD_EXT : subtract extented memory address value from accumulator D : D=D-MM
 *
 * HINZVC
 * ..****
 */
static void hd6301_subd_ext(void)
{
	Uint16 addr, value, regD;
	Uint32 result;

	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr) << 8;
	value += hd6301_read_memory(addr+1);
	regD = (hd6301_reg_A << 8) + hd6301_reg_B;
	result = regD - value;

	hd6301_reg_A = (result >> 8) & 0xff;
	hd6301_reg_B = result & 0xff;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS16(regD, value, result);
}

/**
 * ANDA_EXT : and extented memory address value with accumulator A : A=A&M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_anda_ext(void)
{
	Uint16 addr;
	
	addr = hd6301_get_memory_ext();
	hd6301_reg_A &= hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * BITA_EXT : bit test extented memory address value with accumulator A : A&M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_bita_ext(void)
{
	Uint8 value;
	Uint16 addr;
	
	addr = hd6301_get_memory_ext();
	value = hd6301_reg_A & hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(value);
}

/**
 * LDAA_EXT : load accumulator A with extented memory address value : A=M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_ldaa_ext(void)
{
	Uint16 addr;
	
	addr = hd6301_get_memory_ext();
	hd6301_reg_A = hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * STAA_EXT : store accumulator A into extented memory address value : M=A
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_staa_ext(void)
{
	Uint16 addr;

	addr = hd6301_get_memory_ext();
	hd6301_write_memory(addr, hd6301_reg_A);
	
	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * EORA_EXT : exclusive or extented memory address value with accumulator A : A=A^M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_eora_ext(void)
{
	Uint16 addr;

	addr = hd6301_get_memory_ext();
	hd6301_reg_A ^= hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * ADCA_EXT : add with carry extented memory address value to accumulator A : A=A+M+C
 *
 * HINZVC
 * *.****
 */
static void hd6301_adca_ext(void)
{
	Uint8  value, carry;
	Uint16 addr, result;

	carry = hd6301_REG_CCR_C & 1;
	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr);
	result = hd6301_reg_A + value + carry;

	HD6301_CLR_HNZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, value, result);
	HD6301_SET_H(hd6301_reg_A, value, result);

	hd6301_reg_A = result;
}

/**
 * ORAA_EXT : inclusive or accumulator A with extented memory address value : A=A|M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_oraa_ext(void)
{
	Uint16 addr;
	
	addr = hd6301_get_memory_ext();
	hd6301_reg_A |= hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_A);
}

/**
 * ADDA_EXT : add extented memory address value with accumulator A : A=A+M
 *
 * HINZVC
 * *.****
 */
static void hd6301_adda_ext(void)
{
	Uint8  value;
	Uint16 addr, result;

	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr);
	result = hd6301_reg_A + value;

	HD6301_CLR_HNZVC;
	HD6301_SET_FLAGS8(hd6301_reg_A, value, result);
	HD6301_SET_H(hd6301_reg_A, value, result);

	hd6301_reg_A = result;
}

/**
 * CPX_EXT : compare index register with extented memory address value : X-MM
 *
 * HINZVC
 * ..****
 */
static void hd6301_cpx_ext(void)
{
	Uint16 addr, value;
	Uint32 result;

	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr) << 8;
	value += hd6301_read_memory(addr+1);
	result = hd6301_reg_X - value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS16(hd6301_reg_X, value, result);
}

/**
 * JSR_EXT : jump to subroutine at extented address
 *
 * HINZVC
 * ......
 */
static void hd6301_jsr_ext(void)
{
	Uint16 addr;

	hd6301_write_memory(hd6301_reg_SP--, (hd6301_reg_PC + 2) & 0xff);
	hd6301_write_memory(hd6301_reg_SP--, (hd6301_reg_PC + 2) >> 8);

	addr = hd6301_get_memory_ext();
	hd6301_reg_PC += addr + 2;
}

/**
 * LDS_EXT : load stack pointer with extented memory address value : SP=MM
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_lds_ext(void)
{
	Uint16 addr;
	
	addr = hd6301_get_memory_ext();
	hd6301_reg_SP = hd6301_read_memory(addr) << 8;
	hd6301_reg_SP += hd6301_read_memory(addr+1);

	HD6301_CLR_NZV;
	HD6301_SET_NZ16(hd6301_reg_SP);
}

/**
 * STS_EXT : store stack pointer into extented memory address value : MM=SP
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_sts_ext(void)
{
	Uint16 addr;
	
	addr = hd6301_get_memory_ext();
	hd6301_write_memory(addr, hd6301_reg_SP >> 8);
	hd6301_write_memory(addr+1, hd6301_reg_SP & 8);

	HD6301_CLR_NZV;
	HD6301_SET_NZ16(hd6301_reg_SP);
}

/**
 * SUBB_IMM : subtract immediate value from accumulator B : B=B-M
 *
 * HINZVC
 * ..****
 */
static void hd6301_subb_imm(void)
{
	Uint8  value;
	Uint16 result;

	value = hd6301_read_memory(hd6301_reg_PC+1);
	result = hd6301_reg_B - value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_B, value, result);

	hd6301_reg_B = result;
}

/**
 * CMPB_IMM : compare immediate value to accumulator B : B-M
 *
 * HINZVC
 * ..****
 */
static void hd6301_cmpb_imm(void)
{
	Uint8  value;
	Uint16 result;

	value = hd6301_read_memory(hd6301_reg_PC+1);
	result = hd6301_reg_B - value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_B, value, result);
}

/**
 * SBCB_IMM : subtract with carry immediate value from accumulator B : B=B-M-C
 *
 * HINZVC
 * ..****
 */
static void hd6301_sbcb_imm(void)
{
	Uint8  value, carry;
	Uint16 result;

	carry = hd6301_REG_CCR_C & 1;
	value = hd6301_read_memory(hd6301_reg_PC+1);
	result = hd6301_reg_B - value - carry;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_B, value, result);

	hd6301_reg_B = result;
}

/**
 * ADDD_IMM : add immediate value from accumulator D : D=D+MM
 *
 * HINZVC
 * ..****
 */
static void hd6301_addd_imm(void)
{
	Uint16 value, regD;
	Uint32 result;

	value = hd6301_read_memory(hd6301_reg_PC+1) << 8;
	value += hd6301_read_memory(hd6301_reg_PC+2);
	regD = (hd6301_reg_A << 8) + hd6301_reg_B;
	result = regD + value;

	hd6301_reg_A = (result >> 8) & 0xff;
	hd6301_reg_B = result & 0xff;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS16(regD, value, result);
}

/**
 * ANDB_IMM : and immediate value with accumulator B : B=B&M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_andb_imm(void)
{
	hd6301_reg_B &= hd6301_read_memory(hd6301_reg_PC+1);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * BITB_IMM : bit test immediate value with accumulator B : B&M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_bitb_imm(void)
{
	Uint8 value;
	
	value = hd6301_reg_B & hd6301_read_memory(hd6301_reg_PC+1);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(value);
}

/**
 * LDAB_IMM : load accumulator B with immediate value : B=M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_ldab_imm(void)
{
	hd6301_reg_B = hd6301_read_memory(hd6301_reg_PC+1);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * EORB_IMM : exclusive or immediate value with accumulator B : B=B^M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_eorb_imm(void)
{
	hd6301_reg_B ^= hd6301_read_memory(hd6301_reg_PC+1);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * ADCB_IMM : add with carry immediate value to accumulator B : B=B+M+C
 *
 * HINZVC
 * *.****
 */
static void hd6301_adcb_imm(void)
{
	Uint8  value, carry;
	Uint16 result;

	carry = hd6301_REG_CCR_C & 1;
	value = hd6301_read_memory(hd6301_reg_PC+1);
	result = hd6301_reg_B + value + carry;

	HD6301_CLR_HNZVC;
	HD6301_SET_FLAGS8(hd6301_reg_B, value, result);
	HD6301_SET_H(hd6301_reg_B, value, result);

	hd6301_reg_B = result;
}

/**
 * ORAB_IMM : inclusive or accumulator B with immediate value : B=B|M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_orab_imm(void)
{
	hd6301_reg_B |= hd6301_read_memory(hd6301_reg_PC+1);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * ADDB_IMM : add immediate value with accumulator B : B=B+M
 *
 * HINZVC
 * ..****
 */
static void hd6301_addb_imm(void)
{
	Uint8  value;
	Uint16 result;

	value = hd6301_read_memory(hd6301_reg_PC+1);
	result = hd6301_reg_B + value;

	HD6301_CLR_HNZVC;
	HD6301_SET_FLAGS8(hd6301_reg_B, value, result);
	HD6301_SET_H(hd6301_reg_B, value, result);

	hd6301_reg_B = result;
}

/**
 * LDD_IMM : load accumulator D with immediate value : D=MM
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_ldd_imm(void)
{
	hd6301_reg_A = hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_reg_B = hd6301_read_memory(hd6301_reg_PC+2);

	HD6301_CLR_NZV;
	hd6301_reg_CCR |= ((hd6301_reg_A == 0) && (hd6301_reg_B == 0)) << hd6301_REG_CCR_Z;
	hd6301_reg_CCR |= (hd6301_reg_A >> 7) << hd6301_REG_CCR_N;
}

/**
 * LDX_IMM : load register X with immediate value : X=MM
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_ldx_imm(void)
{
	Uint16 value;
	
	value = hd6301_read_memory(hd6301_reg_PC+1) << 8;
	value += hd6301_read_memory(hd6301_reg_PC+2);
	hd6301_reg_X = value;

	HD6301_CLR_NZV;
	HD6301_SET_NZ16(hd6301_reg_X);
}

/**
 * SUBB_DIR : subtract direct memory address value from accumulator B : B=B-M
 *
 * HINZVC
 * ..****
 */
static void hd6301_subb_dir(void)
{
	Uint8  value;
	Uint16 result, addr;

	addr = hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	result = hd6301_reg_B - value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_B, value, result);

	hd6301_reg_B = result;
}

/**
 * CMPB_DIR : compare direct memory address value to accumulator B : B-M
 *
 * HINZVC
 * ..****
 */
static void hd6301_cmpb_dir(void)
{
	Uint8  value;
	Uint16 addr, result;

	addr = hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	result = hd6301_reg_B - value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_B, value, result);
}

/**
 * SBCB_DIR : subtract with carry direct memory address value from accumulator B : B=B-M-C
 *
 * HINZVC
 * ..****
 */
static void hd6301_sbcb_dir(void)
{
	Uint8  value, carry;
	Uint16 addr, result;

	carry = hd6301_REG_CCR_C & 1;
	addr = hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	result = hd6301_reg_B - value - carry;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_B, value, result);

	hd6301_reg_B = result;
}

/**
 * ADDD_DIR : add direct memory address value from accumulator D : D=D+MM
 *
 * HINZVC
 * ..****
 */
static void hd6301_addd_dir(void)
{
	Uint16 addr, value, regD;
	Uint32 result;

	addr = hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr) << 8;
	value += hd6301_read_memory(addr+1);
	regD = (hd6301_reg_A << 8) + hd6301_reg_B;
	result = regD + value;

	hd6301_reg_A = (result >> 8) & 0xff;
	hd6301_reg_B = result & 0xff;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS16(regD, value, result);
}

/**
 * ANDB_DIR : and direct memory address value with accumulator B : B=B&M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_andb_dir(void)
{
	Uint16 addr;
	
	addr = hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_reg_B &= hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * BITB_DIR : bit test direct memory address value with accumulator B : B&M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_bitb_dir(void)
{
	Uint8 value;
	Uint16 addr;
	
	addr = hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_reg_B & hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(value);
}

/**
 * LDAB_DIR : load accumulator B with direct memory address value : B=M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_ldab_dir(void)
{
	Uint16 addr;
	
	addr = hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_reg_B = hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * STAB_DIR : store accumulator B into direct memory address value : M=B
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_stab_dir(void)
{
	Uint16 addr;

	addr = hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_write_memory(addr, hd6301_reg_B);
	
	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * EORB_DIR : exclusive or direct memory address value with accumulator B : B=B^M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_eorb_dir(void)
{
	Uint16 addr;

	addr = hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_reg_B ^= hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * ADCB_DIR : add with carry direct memory address value to accumulator B : B=B+M+C
 *
 * HINZVC
 * *.****
 */
static void hd6301_adcb_dir(void)
{
	Uint8  value, carry;
	Uint16 addr, result;

	carry = hd6301_REG_CCR_C & 1;
	addr = hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	result = hd6301_reg_B + value + carry;

	HD6301_CLR_HNZVC;
	HD6301_SET_FLAGS8(hd6301_reg_B, value, result);
	HD6301_SET_H(hd6301_reg_B, value, result);

	hd6301_reg_B = result;
}

/**
 * ORAB_DIR : inclusive or accumulator B with direct memory address value : B=B|M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_orab_dir(void)
{
	Uint16 addr;
	
	addr = hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_reg_B |= hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * ADDB_DIR : add direct memory address value with accumulator B : B=B+M
 *
 * HINZVC
 * *.****
 */
static void hd6301_addb_dir(void)
{
	Uint8  value;
	Uint16 addr, result;

	addr = hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	result = hd6301_reg_B + value;

	HD6301_CLR_HNZVC;
	HD6301_SET_FLAGS8(hd6301_reg_B, value, result);
	HD6301_SET_H(hd6301_reg_B, value, result);

	hd6301_reg_B = result;
}

/**
 * LDD_DIR : load accumulator D with direct memory address value : D=MM
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_ldd_dir(void)
{
	Uint16 addr;
	
	addr = hd6301_read_memory(hd6301_reg_PC+1);

	hd6301_reg_A = hd6301_read_memory(addr);
	hd6301_reg_B = hd6301_read_memory(addr+1);

	HD6301_CLR_NZV;
	hd6301_reg_CCR |= ((hd6301_reg_A == 0) && (hd6301_reg_B == 0)) << hd6301_REG_CCR_Z;
	hd6301_reg_CCR |= (hd6301_reg_A >> 7) << hd6301_REG_CCR_N;
}

/**
 * STD_DIR : store accumulator D into direct memory address value : MM=D
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_std_dir(void)
{
	Uint16 addr;
	
	addr = hd6301_read_memory(hd6301_reg_PC+1);

	hd6301_write_memory(addr, hd6301_reg_A);
	hd6301_write_memory(addr+1, hd6301_reg_B);

	HD6301_CLR_NZV;
	hd6301_reg_CCR |= ((hd6301_reg_A == 0) && (hd6301_reg_B == 0)) << hd6301_REG_CCR_Z;
	hd6301_reg_CCR |= (hd6301_reg_A >> 7) << hd6301_REG_CCR_N;
}

/**
 * LDX_DIR : load register X with direct memory address value : X=MM
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_ldx_dir(void)
{
	Uint16 addr;
	
	addr = hd6301_read_memory(hd6301_reg_PC+1);

	hd6301_reg_X = hd6301_read_memory(addr) << 8;
	hd6301_reg_X += hd6301_read_memory(addr+1);

	HD6301_CLR_NZV;
	HD6301_SET_NZ16(hd6301_reg_X);
}

/**
 * STX_DIR : store register X into direct memory address value : MM=X
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_stx_dir(void)
{
	Uint16 addr;
	
	addr = hd6301_read_memory(hd6301_reg_PC+1);

	hd6301_write_memory(addr, hd6301_reg_X >> 8);
	hd6301_write_memory(addr+1, hd6301_reg_X & 0xff);

	HD6301_CLR_NZV;
	HD6301_SET_NZ16(hd6301_reg_X);
}

/**
 * SUBB_IND : subtract indexed memory address value from accumulator B : B=B-M
 *
 * HINZVC
 * ..****
 */
static void hd6301_subb_ind(void)
{
	Uint8  value;
	Uint16 result, addr;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	result = hd6301_reg_B - value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_B, value, result);

	hd6301_reg_B = result;
}

/**
 * CMPB_IND : compare indexed memory address value to accumulator B : B-M
 *
 * HINZVC
 * ..****
 */
static void hd6301_cmpb_ind(void)
{
	Uint8  value;
	Uint16 addr, result;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	result = hd6301_reg_B - value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_B, value, result);
}

/**
 * SBCB_IND : subtract with carry indexed memory address value from accumulator B : B=B-M-C
 *
 * HINZVC
 * ..****
 */
static void hd6301_sbcb_ind(void)
{
	Uint8  value, carry;
	Uint16 addr, result;

	carry = hd6301_REG_CCR_C & 1;
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	result = hd6301_reg_B - value - carry;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_B, value, result);

	hd6301_reg_B = result;
}

/**
 * ADDD_IND : add indexed memory address value from accumulator D : D=D+MM
 *
 * HINZVC
 * ..****
 */
static void hd6301_addd_ind(void)
{
	Uint16 addr, value, regD;
	Uint32 result;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr) << 8;
	value += hd6301_read_memory(addr+1);
	regD = (hd6301_reg_A << 8) + hd6301_reg_B;
	result = regD + value;

	hd6301_reg_A = (result >> 8) & 0xff;
	hd6301_reg_B = result & 0xff;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS16(regD, value, result);
}

/**
 * ANDB_IND : and indexed memory address value with accumulator B : B=B&M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_andb_ind(void)
{
	Uint16 addr;
	
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_reg_B &= hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * BITB_IND : bit test indexed memory address value with accumulator B : B&M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_bitb_ind(void)
{
	Uint8 value;
	Uint16 addr;
	
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_reg_B & hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(value);
}

/**
 * LDAB_IND : load accumulator B with indexed memory address value : B=M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_ldab_ind(void)
{
	Uint16 addr;
	
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_reg_B = hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * STAB_IND : store accumulator B into indexed memory address value : M=B
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_stab_ind(void)
{
	Uint16 addr;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_write_memory(addr, hd6301_reg_B);
	
	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * EORB_IND : exclusive or indexed memory address value with accumulator B : B=B^M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_eorb_ind(void)
{
	Uint16 addr;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_reg_B ^= hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * ADCB_IND : add with carry indexed memory address value to accumulator B : B=B+M+C
 *
 * HINZVC
 * *.****
 */
static void hd6301_adcb_ind(void)
{
	Uint8  value, carry;
	Uint16 addr, result;

	carry = hd6301_REG_CCR_C & 1;
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	result = hd6301_reg_B + value + carry;

	HD6301_CLR_HNZVC;
	HD6301_SET_FLAGS8(hd6301_reg_B, value, result);
	HD6301_SET_H(hd6301_reg_B, value, result);

	hd6301_reg_B = result;
}

/**
 * ORAB_IND : inclusive or accumulator B with indexed memory address value : B=B|M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_orab_ind(void)
{
	Uint16 addr;
	
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	hd6301_reg_B |= hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * ADDB_IND : add indexed memory address value with accumulator B : B=B+M
 *
 * HINZVC
 * *.****
 */
static void hd6301_addb_ind(void)
{
	Uint8  value;
	Uint16 addr, result;

	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);
	value = hd6301_read_memory(addr);
	result = hd6301_reg_B + value;

	HD6301_CLR_HNZVC;
	HD6301_SET_FLAGS8(hd6301_reg_B, value, result);
	HD6301_SET_H(hd6301_reg_B, value, result);

	hd6301_reg_B = result;
}

/**
 * LDD_IND : load accumulator D with indexed memory address value : D=MM
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_ldd_ind(void)
{
	Uint16 addr;
	
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);

	hd6301_reg_A = hd6301_read_memory(addr);
	hd6301_reg_B = hd6301_read_memory(addr+1);

	HD6301_CLR_NZV;
	hd6301_reg_CCR |= ((hd6301_reg_A == 0) && (hd6301_reg_B == 0)) << hd6301_REG_CCR_Z;
	hd6301_reg_CCR |= (hd6301_reg_A >> 7) << hd6301_REG_CCR_N;
}

/**
 * STD_IND : store accumulator D into indexed memory address value : MM=D
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_std_ind(void)
{
	Uint16 addr;
	
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);

	hd6301_write_memory(addr, hd6301_reg_A);
	hd6301_write_memory(addr+1, hd6301_reg_B);

	HD6301_CLR_NZV;
	hd6301_reg_CCR |= ((hd6301_reg_A == 0) && (hd6301_reg_B == 0)) << hd6301_REG_CCR_Z;
	hd6301_reg_CCR |= (hd6301_reg_A >> 7) << hd6301_REG_CCR_N;
}

/**
 * LDX_IND : load register X with indexed memory address value : X=MM
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_ldx_ind(void)
{
	Uint16 addr;
	
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);

	hd6301_reg_X = hd6301_read_memory(addr) << 8;
	hd6301_reg_X += hd6301_read_memory(addr+1);

	HD6301_CLR_NZV;
	HD6301_SET_NZ16(hd6301_reg_X);
}

/**
 * STX_IND : store register X into indexed memory address value : MM=X
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_stx_ind(void)
{
	Uint16 addr;
	
	addr = hd6301_reg_X + hd6301_read_memory(hd6301_reg_PC+1);

	hd6301_write_memory(addr, hd6301_reg_X >> 8);
	hd6301_write_memory(addr+1, hd6301_reg_X & 0xff);

	HD6301_CLR_NZV;
	HD6301_SET_NZ16(hd6301_reg_X);
}

/**
 * SUBB_EXT : subtract extended memory address value from accumulator B : B=B-M
 *
 * HINZVC
 * ..****
 */
static void hd6301_subb_ext(void)
{
	Uint8  value;
	Uint16 result, addr;

	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr);
	result = hd6301_reg_B - value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_B, value, result);

	hd6301_reg_B = result;
}

/**
 * CMPB_EXT : compare extended memory address value to accumulator B : B-M
 *
 * HINZVC
 * ..****
 */
static void hd6301_cmpb_ext(void)
{
	Uint8  value;
	Uint16 addr, result;

	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr);
	result = hd6301_reg_B - value;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_B, value, result);
}

/**
 * SBCB_EXT : subtract with carry extended memory address value from accumulator B : B=B-M-C
 *
 * HINZVC
 * ..****
 */
static void hd6301_sbcb_ext(void)
{
	Uint8  value, carry;
	Uint16 addr, result;

	carry = hd6301_REG_CCR_C & 1;
	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr);
	result = hd6301_reg_B - value - carry;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS8(hd6301_reg_B, value, result);

	hd6301_reg_B = result;
}

/**
 * ADDD_EXT : add extended memory address value from accumulator D : D=D+MM
 *
 * HINZVC
 * ..****
 */
static void hd6301_addd_ext(void)
{
	Uint16 addr, value, regD;
	Uint32 result;

	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr) << 8;
	value += hd6301_read_memory(addr+1);
	regD = (hd6301_reg_A << 8) + hd6301_reg_B;
	result = regD + value;

	hd6301_reg_A = (result >> 8) & 0xff;
	hd6301_reg_B = result & 0xff;

	HD6301_CLR_NZVC;
	HD6301_SET_FLAGS16(regD, value, result);
}

/**
 * ANDB_EXT : and extended memory address value with accumulator B : B=B&M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_andb_ext(void)
{
	Uint16 addr;
	
	addr = hd6301_get_memory_ext();
	hd6301_reg_B &= hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * BITB_EXT : bit test extended memory address value with accumulator B : B&M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_bitb_ext(void)
{
	Uint8 value;
	Uint16 addr;
	
	addr = hd6301_get_memory_ext();
	value = hd6301_reg_B & hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(value);
}

/**
 * LDAB_EXT : load accumulator B with extended memory address value : B=M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_ldab_ext(void)
{
	Uint16 addr;
	
	addr = hd6301_get_memory_ext();
	hd6301_reg_B = hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * STAB_EXT : store accumulator B into extended memory address value : M=B
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_stab_ext(void)
{
	Uint16 addr;

	addr = hd6301_get_memory_ext();
	hd6301_write_memory(addr, hd6301_reg_B);
	
	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * EORB_EXT : exclusive or extended memory address value with accumulator B : B=B^M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_eorb_ext(void)
{
	Uint16 addr;

	addr = hd6301_get_memory_ext();
	hd6301_reg_B ^= hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * ADCB_EXT : add with carry extended memory address value to accumulator B : B=B+M+C
 *
 * HINZVC
 * *.****
 */
static void hd6301_adcb_ext(void)
{
	Uint8  value, carry;
	Uint16 addr, result;

	carry = hd6301_REG_CCR_C & 1;
	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr);
	result = hd6301_reg_B + value + carry;

	HD6301_CLR_HNZVC;
	HD6301_SET_FLAGS8(hd6301_reg_B, value, result);
	HD6301_SET_H(hd6301_reg_B, value, result);

	hd6301_reg_B = result;
}

/**
 * ORAB_EXT : inclusive or accumulator B with extended memory address value : B=B|M
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_orab_ext(void)
{
	Uint16 addr;
	
	addr = hd6301_get_memory_ext();
	hd6301_reg_B |= hd6301_read_memory(addr);

	HD6301_CLR_NZV;
	HD6301_SET_NZ8(hd6301_reg_B);
}

/**
 * ADDB_EXT : add extended memory address value with accumulator B : B=B+M
 *
 * HINZVC
 * *.****
 */
static void hd6301_addb_ext(void)
{
	Uint8  value;
	Uint16 addr, result;

	addr = hd6301_get_memory_ext();
	value = hd6301_read_memory(addr);
	result = hd6301_reg_B + value;

	HD6301_CLR_HNZVC;
	HD6301_SET_FLAGS8(hd6301_reg_B, value, result);
	HD6301_SET_H(hd6301_reg_B, value, result);

	hd6301_reg_B = result;
}

/**
 * LDD_EXT : load accumulator D with extended memory address value : D=MM
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_ldd_ext(void)
{
	Uint16 addr;
	
	addr = hd6301_get_memory_ext();

	hd6301_reg_A = hd6301_read_memory(addr);
	hd6301_reg_B = hd6301_read_memory(addr+1);

	HD6301_CLR_NZV;
	hd6301_reg_CCR |= ((hd6301_reg_A == 0) && (hd6301_reg_B == 0)) << hd6301_REG_CCR_Z;
	hd6301_reg_CCR |= (hd6301_reg_A >> 7) << hd6301_REG_CCR_N;
}

/**
 * STD_EXT : store accumulator D into extended memory address value : MM=D
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_std_ext(void)
{
	Uint16 addr;
	
	addr = hd6301_get_memory_ext();

	hd6301_write_memory(addr, hd6301_reg_A);
	hd6301_write_memory(addr+1, hd6301_reg_B);

	HD6301_CLR_NZV;
	hd6301_reg_CCR |= ((hd6301_reg_A == 0) && (hd6301_reg_B == 0)) << hd6301_REG_CCR_Z;
	hd6301_reg_CCR |= (hd6301_reg_A >> 7) << hd6301_REG_CCR_N;
}

/**
 * LDX_EXT : load register X with extended memory address value : X=MM
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_ldx_ext(void)
{
	Uint16 addr;
	
	addr = hd6301_get_memory_ext();

	hd6301_reg_X = hd6301_read_memory(addr) << 8;
	hd6301_reg_X += hd6301_read_memory(addr+1);

	HD6301_CLR_NZV;
	HD6301_SET_NZ16(hd6301_reg_X);
}

/**
 * STX_EXT : store register X into extended memory address value : MM=X
 *
 * HINZVC
 * ..**0.
 */
static void hd6301_stx_ext(void)
{
	Uint16 addr;
	
	addr = hd6301_get_memory_ext();

	hd6301_write_memory(addr, hd6301_reg_X >> 8);
	hd6301_write_memory(addr+1, hd6301_reg_X & 0xff);

	HD6301_CLR_NZV;
	HD6301_SET_NZ16(hd6301_reg_X);
}



/**
 * hd6301_disasm : disasm hd6301 memory
 */
void hd6301_disasm(void)
{
	switch(hd6301_opcode.op_disasm) {
		case HD6301_DISASM_UNDEFINED:
			sprintf(hd6301_str_instr, "0x%02x : unknown instruction", hd6301_cur_inst);
			break;
		case HD6301_DISASM_NONE: 
			sprintf(hd6301_str_instr, hd6301_opcode.op_mnemonic, 0);
			break;
		case HD6301_DISASM_MEMORY8: 
			sprintf(hd6301_str_instr, hd6301_opcode.op_mnemonic, hd6301_read_memory(hd6301_reg_PC+1));
			break;
		case HD6301_DISASM_MEMORY16: 
			sprintf(hd6301_str_instr, hd6301_opcode.op_mnemonic, hd6301_get_memory_ext());
			break;
		case HD6301_DISASM_XIM: 
			sprintf(hd6301_str_instr, hd6301_opcode.op_mnemonic,
				hd6301_read_memory(hd6301_reg_PC+1),
				hd6301_read_memory(hd6301_reg_PC+2));
			break;
	}

	fprintf(stderr, "%02x: %s\n", hd6301_reg_PC, hd6301_str_instr);

}

/**
 * hd6301_display_registers : display hd6301 registers state
 */
void hd6301_display_registers(void)
{
	fprintf(stderr, "A:  %02x       B: %02x\n", hd6301_reg_A, hd6301_reg_B);
	fprintf(stderr, "X:  %04x   CCR: %02x\n", hd6301_reg_X, hd6301_reg_CCR);
	fprintf(stderr, "SP: %04x    PC:  %04x\n", hd6301_reg_SP, hd6301_reg_PC);
}
