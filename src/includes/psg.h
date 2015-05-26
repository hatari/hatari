/*
  Hatari

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_PSG_H
#define HATARI_PSG_H

enum
{
  PSG_REG_CHANNEL_A_FINE,     /* 0x0000 */
  PSG_REG_CHANNEL_A_COARSE,   /* 0x0001 */
  PSG_REG_CHANNEL_B_FINE,     /* 0x0010 */
  PSG_REG_CHANNEL_B_COARSE,   /* 0x0011 */
  PSG_REG_CHANNEL_C_FINE,     /* 0x0100 */
  PSG_REG_CHANNEL_C_COARSE,   /* 0x0101 */
  PSG_REG_NOISE_GENERATOR,    /* 0x0110 */
  PSG_REG_MIXER_CONTROL,      /* 0x0111 */
  PSG_REG_CHANNEL_A_AMP,      /* 0x1000 */
  PSG_REG_CHANNEL_B_AMP,      /* 0x1001 */
  PSG_REG_CHANNEL_C_AMP,      /* 0x1010 */
  PSG_REG_ENV_FINE,           /* 0x1011 */
  PSG_REG_ENV_COARSE,         /* 0x1100 */
  PSG_REG_ENV_SHAPE,          /* 0x1101 */
  PSG_REG_IO_PORTA,           /* 0x1110 */
  PSG_REG_IO_PORTB,           /* 0x1111 */
  MAX_PSG_REGISTERS
};

#define NUM_PSG_SOUND_REGISTERS    14		/* Number of sound related registers, not including IO ports */

extern Uint8 PSGRegisters[MAX_PSG_REGISTERS];

extern void PSG_Reset(void);
extern void PSG_MemorySnapShot_Capture(bool bSave);

extern void PSG_Set_SelectRegister(Uint8 val);
extern Uint8 PSG_Get_DataRegister(void);
extern void PSG_Set_DataRegister(Uint8 val);

extern void PSG_ff8800_ReadByte(void);
extern void PSG_ff880x_ReadByte(void);
extern void PSG_ff8800_WriteByte(void);
extern void PSG_ff8801_WriteByte(void);
extern void PSG_ff8802_WriteByte(void);
extern void PSG_ff8803_WriteByte(void);

extern void PSG_Info(FILE *fp, Uint32 dummy);

#endif  /* HATARI_PSG_H */
