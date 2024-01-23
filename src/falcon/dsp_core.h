/*
	DSP M56001 emulation
	Core of DSP emulation

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

#ifndef DSP_CORE_H
#define DSP_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DSP_RAMSIZE 32768

/* Host port, CPU side */
#define CPU_HOST_ICR	0x00
#define CPU_HOST_CVR	0x01
#define CPU_HOST_ISR	0x02
#define CPU_HOST_IVR	0x03
#define CPU_HOST_TRX0	0x04
#define CPU_HOST_TRXH	0x05
#define CPU_HOST_TRXM	0x06
#define CPU_HOST_TRXL	0x07
#define CPU_HOST_RX0	0x04
#define CPU_HOST_RXH	0x05
#define CPU_HOST_RXM	0x06
#define CPU_HOST_RXL	0x07
#define CPU_HOST_TXH	0x09
#define CPU_HOST_TXM	0x0a
#define CPU_HOST_TXL	0x0b

#define CPU_HOST_ICR_RREQ	0x00
#define CPU_HOST_ICR_TREQ	0x01
#define CPU_HOST_ICR_HF0	0x03
#define CPU_HOST_ICR_HF1	0x04
#define CPU_HOST_ICR_HM0	0x05
#define CPU_HOST_ICR_HM1	0x06
#define CPU_HOST_ICR_INIT	0x07

#define CPU_HOST_CVR_HC		0x07

#define CPU_HOST_ISR_RXDF	0x00
#define CPU_HOST_ISR_TXDE	0x01
#define CPU_HOST_ISR_TRDY	0x02
#define CPU_HOST_ISR_HF2	0x03
#define CPU_HOST_ISR_HF3	0x04
#define CPU_HOST_ISR_DMA	0x06
#define CPU_HOST_ISR_HREQ	0x07

/* Host port, DSP side, DSP addresses are 0xffc0+value */
#define DSP_PBC			0x20	/* Port B control register */
#define DSP_PCC			0x21	/* Port C control register */
#define DSP_PBDDR		0x22	/* Port B data direction register */
#define DSP_PCDDR		0x23	/* Port C data direction register */
#define DSP_PBD			0x24	/* Port B data register */
#define DSP_PCD			0x25	/* Port C data register */
#define DSP_HOST_HCR		0x28	/* Host control register */
#define DSP_HOST_HSR		0x29	/* Host status register */
#define DSP_HOST_HRX		0x2b	/* Host receive register */
#define DSP_HOST_HTX		0x2b	/* Host transmit register */
#define DSP_SSI_CRA		0x2c	/* SSI control register A */
#define DSP_SSI_CRB		0x2d	/* SSI control register B */
#define DSP_SSI_SR		0x2e	/* SSI status register */
#define DSP_SSI_TSR		0x2e	/* SSI time slot register */
#define DSP_SSI_RX		0x2f	/* SSI receive register */
#define DSP_SSI_TX		0x2f	/* SSI transmit register */
#define DSP_SCI_SCR		0x30	/* SCI control register */
#define DSP_SCI_SSR		0x31	/* SCI status register */
#define DSP_SCI_SCCR		0x32	/* SCI clock control register */
#define DSP_BCR			0x3e	/* Port A bus control register */
#define DSP_IPR			0x3f	/* Interrupt priority register */

#define DSP_HOST_HCR_HRIE	0x00
#define DSP_HOST_HCR_HTIE	0x01
#define DSP_HOST_HCR_HCIE	0x02
#define DSP_HOST_HCR_HF2	0x03
#define DSP_HOST_HCR_HF3	0x04

#define DSP_HOST_HSR_HRDF	0x00
#define DSP_HOST_HSR_HTDE	0x01
#define DSP_HOST_HSR_HCP	0x02
#define DSP_HOST_HSR_HF0	0x03
#define DSP_HOST_HSR_HF1	0x04
#define DSP_HOST_HSR_DMA	0x07

#define DSP_SSI_CRA_DC0		0x8
#define DSP_SSI_CRA_DC1		0x9
#define DSP_SSI_CRA_DC2		0xa
#define DSP_SSI_CRA_DC3		0xb
#define DSP_SSI_CRA_DC4		0xc
#define DSP_SSI_CRA_WL0		0xd
#define DSP_SSI_CRA_WL1		0xe

#define DSP_SSI_CRB_OF0		0x0
#define DSP_SSI_CRB_OF1		0x1
#define DSP_SSI_CRB_SCD0	0x2
#define DSP_SSI_CRB_SCD1	0x3
#define DSP_SSI_CRB_SCD2	0x4
#define DSP_SSI_CRB_SCKD	0x5
#define DSP_SSI_CRB_SHFD	0x6
#define DSP_SSI_CRB_FSL0	0x7
#define DSP_SSI_CRB_FSL1	0x8
#define DSP_SSI_CRB_SYN		0x9
#define DSP_SSI_CRB_GCK		0xa
#define DSP_SSI_CRB_MOD		0xb
#define DSP_SSI_CRB_TE		0xc
#define DSP_SSI_CRB_RE		0xd
#define DSP_SSI_CRB_TIE		0xe
#define DSP_SSI_CRB_RIE		0xf

#define DSP_SSI_SR_IF0		0x0
#define DSP_SSI_SR_IF1		0x1
#define DSP_SSI_SR_TFS		0x2
#define DSP_SSI_SR_RFS		0x3
#define DSP_SSI_SR_TUE		0x4
#define DSP_SSI_SR_ROE		0x5
#define DSP_SSI_SR_TDE		0x6
#define DSP_SSI_SR_RDF		0x7

#define DSP_INTERRUPT_NONE      0x0
#define DSP_INTERRUPT_DISABLED  0x1
#define DSP_INTERRUPT_LONG      0x2

#define DSP_INTER_RESET				0
#define DSP_INTER_STACK_ERROR		1
#define DSP_INTER_TRACE				2
#define DSP_INTER_SWI				3
#define DSP_INTER_IRQA				4
#define DSP_INTER_IRQB				5
#define DSP_INTER_SSI_RCV_DATA		6
#define DSP_INTER_SSI_RCV_DATA_E	7
#define DSP_INTER_SSI_TRX_DATA		8
#define DSP_INTER_SSI_TRX_DATA_E	9
#define DSP_INTER_SCI_RCV_DATA		10
#define DSP_INTER_SCI_RCV_DATA_E	11
#define DSP_INTER_SCI_TRX_DATA		12
#define DSP_INTER_SCI_IDLE_LINE		13
#define DSP_INTER_SCI_TIMER			14
#define DSP_INTER_NMI				15
#define DSP_INTER_HOST_RCV_DATA		16
#define DSP_INTER_HOST_TRX_DATA		17
#define DSP_INTER_HOST_COMMAND		18
#define DSP_INTER_ILLEGAL			31

#define DSP_INTER_NMI_MASK	0x8000800F
#define DSP_INTER_IRQA_MASK	0x00000010
#define DSP_INTER_IRQB_MASK	0x00000020
#define DSP_INTER_SSI_MASK	0x000003C0
#define DSP_INTER_SCI_MASK	0x00007C00
#define DSP_INTER_HOST_MASK	0x00070000

#define DSP_INTER_EDGE_MASK	0x8004C00E


#define DSP_PRIORITY_LIST_EXIT 32
extern const char dsp_inter_priority_list[32];
extern const char *dsp_interrupt_name[32];


typedef struct dsp_core_ssi_s dsp_core_ssi_t;
typedef struct dsp_core_s dsp_core_t;
typedef struct dsp_interrupt_s dsp_interrupt_t;

struct dsp_core_ssi_s {
	uint16_t cra_word_length;
	uint32_t cra_word_mask;
	uint16_t cra_frame_rate_divider;

	uint16_t crb_src_clock;
	uint16_t crb_shifter;
	uint16_t crb_synchro;
	uint16_t crb_mode;
	uint16_t crb_te;
	uint16_t crb_re;
	uint16_t crb_tie;
	uint16_t crb_rie;

	uint32_t TX;
	uint32_t RX;
	uint32_t transmit_value;		/* DSP Transmit --> SSI */
	uint32_t received_value;		/* DSP Receive  --> SSI */
	uint16_t waitFrameTX;
	uint16_t waitFrameRX;
	uint32_t dspPlay_handshakeMode_frame;
};

struct dsp_interrupt_s {
	const uint16_t inter;
	const uint16_t vectorAddr;
	const uint16_t periph;
	const char *name;
};


struct dsp_core_s {

	/* DSP executing instructions ? */
	int running;

	/* DSP instruction Cycle counter */
	uint16_t instr_cycle;

	/* Registers */
	uint16_t pc;
	uint32_t registers[64];

	/* stack[0=ssh], stack[1=ssl] */
	uint16_t stack[2][16];

	/* External ram[] (mapped to p:) */
	uint32_t ramext[DSP_RAMSIZE];

	/* rom[0] is x:, rom[1] is y:, rom[2] is p: */
	uint32_t rom[3][512];

	/* Internal ram[0] is x:, ram[1] is y:, ram[2] is p: */
	uint32_t ramint[3][512];

	/* peripheral space, [x|y]:0xffc0-0xffff */
	uint32_t periph[2][64];
	uint32_t dsp_host_htx;
	uint32_t dsp_host_rtx;
	uint16_t dsp_host_isr_HREQ;


	/* host port, CPU side */
	uint8_t hostport[12];

	/* SSI */
	dsp_core_ssi_t ssi;

	/* Misc */
	uint32_t loop_rep;		/* executing rep ? */
	uint32_t pc_on_rep;		/* True if PC is on REP instruction */

	/* For bootstrap routine */
	uint16_t bootstrap_pos;

	/* Interruptions */
	uint16_t interrupt_state;		/* NONE, FAST or LONG interrupt */
	uint16_t interrupt_instr_fetch;		/* vector of the current interrupt */
	uint16_t interrupt_save_pc;		/* save next pc value before interrupt */
	uint16_t interrupt_IplToRaise;		/* save the IPL level to save in the SR register */
	uint16_t interrupt_pipeline_count;	/* used to prefetch correctly the 2 inter instructions */

	/* Interruptions new */
	uint32_t interrupt_status;
	uint32_t interrupt_enable;
	uint32_t interrupt_mask;
	uint32_t interrupt_mask_level[3];
	uint32_t interrupt_edgetriggered_mask;

	/* AGU pipeline simulation for indirect move ea instructions */
	uint16_t agu_move_indirect_instr;	/* is the current instruction an indirect move ? (LUA, MOVE, MOVEC, MOVEM, TCC) (0=no ; 1 = yes)*/
};


/* DSP */
extern dsp_core_t dsp_core;

/* Emulator call these to init/stop/reset DSP emulation */
extern void dsp_core_init(void (*host_interrupt)(int));
extern void dsp_core_shutdown(void);
extern void dsp_core_reset(void);

/* host port read/write by emulator, addr is 0-7, not 0xffa200-0xffa207 */
extern uint8_t dsp_core_read_host(int addr);
extern void dsp_core_write_host(int addr, uint8_t value);

/* dsp_cpu call these to read/write host port */
extern void dsp_core_hostport_dspread(void);
extern void dsp_core_hostport_dspwrite(void);

/* dsp_cpu call these to read/write/configure SSI port */
extern void dsp_core_ssi_configure(uint32_t address, uint32_t value);
extern void dsp_core_ssi_writeTX(uint32_t value);
extern void dsp_core_ssi_writeTSR(void);
extern uint32_t dsp_core_ssi_readRX(void);
extern void dsp_core_ssi_Receive_SC0(void);
extern void dsp_core_ssi_Receive_SC1(uint32_t value);
extern void dsp_core_ssi_Receive_SC2(uint32_t value);
extern void dsp_core_ssi_Receive_SCK(void);
extern void dsp_core_setPortCDataRegister(uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* DSP_CORE_H */
