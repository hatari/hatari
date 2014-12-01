 /*
  * UAE CPU core
  *
  * Function definitions for saving/restoring the CPU state
  */


extern void save_u64(uae_u64 data);
extern void save_u32(uae_u32 data);
extern void save_u16(uae_u16 data);
extern void save_u8(uae_u8 data);
extern uae_u64 restore_u64(void);
extern uae_u32 restore_u32(void);
extern uae_u16 restore_u16(void);
extern uae_u8 restore_u8(void);

extern void restore_fpu(void);
extern void save_fpu(void);
