 /*
  * UAE CPU core
  *
  * Function definitions for saving/restoring the CPU state
  */


extern void save_u32(uae_u32 data);
extern void save_u16(uae_u16 data);
extern uae_u32 restore_u32(void);
extern uae_u16 restore_u16(void);

extern void restore_fpu(void);
extern void save_fpu(void);
