 /*
  * UAE CPU core
  *
  * Function definitions for saving/restoring the CPU state
  */


void save_u32(uae_u32 data);
void save_u16(uae_u16 data);
uae_u32 restore_u32(void);
uae_u16 restore_u16(void);

void restore_fpu(void);
void save_fpu(void);
