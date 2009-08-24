/*
  Hatari - hd6301_cpu.h
  Copyright Laurent Sallafranque 2009

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

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

struct hd6301_opcode_t {
	Uint8	op_value;			/* Opcode value */
	Uint8	op_bytes;			/* Total opcode bytes */
	void	(*op_func)(void);		/* Function that "executes" opcode */
	Uint8	op_n_cycles;			/* Number of clock cycles */
	const char *op_mnemonic;		/* Printout format string */
	void	(*op_func_disasm)(void);	/* Function that "executes" disasm opcode */
};

/* Functions */
void hd6301_init_cpu(void);
void hd6301_execute_one_instruction(void);

/* HF6301 Disasm and debug code */
void hd6301_disasm(void);


#ifdef __cplusplus
}
#endif

#endif	/* HD6301_CPU_H */

--
Ce message et  toutes les pieces jointes (ci-apres  le "message") sont
confidentiels et etablis a l'intention exclusive de ses destinataires.
Toute  utilisation ou  diffusion  non autorisee  est interdite.   Tout
message  etant  susceptible  d'alteration,  l'emetteur  decline  toute
responsabilite au titre de  ce message  s'il a  ete altere, deforme ou
falsifie.
                -----------------------------------
This message and any  attachments (the "message") are confidential and
intended  solely   for  the   addressees.  Any  unauthorised   use  or
dissemination is prohibited. As e-mails are susceptible to alteration,
the issuer shall  not be  liable for  the  message if altered, changed
or falsified.
