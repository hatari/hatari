/*
  Hatari - hd6301_cpu.h
  Copyright Laurent Sallafranque 2009

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  hd6301_cpu.h - this is the cpu core emulation for hd 6301 processor
*/

#ifndef HD6301_CPU_H
#define HD6301_CPU_H

#ifdef __cplusplus
extern "C" {
#endif

/* Defines */
#define hd6301_REG_CCR_C	0x00
#define hd6301_REG_CCR_V	0x01
#define hd6301_REG_CCR_Z	0x02
#define hd6301_REG_CCR_N	0x03
#define hd6301_REG_CCR_I	0x04
#define hd6301_REG_CCR_H	0x05

struct hd6301_opcode_t {
	Uint8	op_value;		/* Opcode value */
	Uint8	op_bytes;		/* Total opcode bytes */
	void	(*op_func)(void);	/* Function that "executes" opcode */
	Uint8	op_n_cycles;		/* Number of clock cycles */
	const char *op_mnemonic;	/* Printout format string */
	Uint8	op_disasm;		/* For instructions disasm */
};

/* Functions */
extern void hd6301_init_cpu(void);
extern void hd6301_execute_one_instruction(void);

/* HF6301 Disasm and debug code */
extern void hd6301_disasm(void);
extern void hd6301_display_registers(void);


#ifdef __cplusplus
}
#endif

#endif	/* HD6301_CPU_H */
