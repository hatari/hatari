/*
  Hatari
*/

enum {
  PSG_REG_CHANNEL_A_FINE,     // 0x0000
  PSG_REG_CHANNEL_A_COARSE,   // 0x0001
  PSG_REG_CHANNEL_B_FINE,     // 0x0010
  PSG_REG_CHANNEL_B_COARSE,   // 0x0011
  PSG_REG_CHANNEL_C_FINE,     // 0x0100
  PSG_REG_CHANNEL_C_COARSE,   // 0x0101
  PSG_REG_NOISE_GENERATOR,    // 0x0110
  PSG_REG_MIXER_CONTROL,      // 0x0111
  PSG_REG_CHANNEL_A_AMP,      // 0x1000
  PSG_REG_CHANNEL_B_AMP,      // 0x1001
  PSG_REG_CHANNEL_C_AMP,      // 0x1010
  PSG_REG_ENV_FINE,           // 0x1011
  PSG_REG_ENV_COARSE,         // 0x1100
  PSG_REG_ENV_SHAPE,          // 0x1101
  PSG_REG_IO_PORTA,           // 0x1110
  PSG_REG_IO_PORTB            // 0x1111
};

#define NUM_PSG_SOUND_REGISTERS    14  // Number of register, not including IO ports

extern unsigned char PSGRegisterSelect;
extern unsigned char PSGRegisters[16];

extern void PSG_Reset(void);
extern void PSG_MemorySnapShot_Capture(BOOL bSave);
extern void PSG_WriteSelectRegister(unsigned short v);
extern unsigned short PSG_ReadSelectRegister(void);
extern void PSG_WriteDataRegister(unsigned short v);
extern unsigned short PSG_ReadDataRegister(void);
