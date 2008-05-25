/*
  Hatari

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
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
  PSG_REG_IO_PORTB            /* 0x1111 */
};

#define NUM_PSG_SOUND_REGISTERS    14   /* Number of register, not including IO ports */

extern Uint8 PSGRegisterSelect;
extern Uint8 PSGRegisters[16];

extern void PSG_Reset(void);
extern void PSG_MemorySnapShot_Capture(bool bSave);
extern void PSG_SelectRegister_WriteByte(void);
extern void PSG_SelectRegister_ReadByte(void);
extern void PSG_DataRegister_WriteByte(void);
extern void PSG_DataRegister_ReadByte(void);
extern void PSG_Void_WriteByte(void);
extern void PSG_Void_ReadByte(void);

#endif  /* HATARI_PSG_H */
