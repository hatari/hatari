/*
  Hatari
*/

extern void M68000_Reset(BOOL bCold);
extern void M68000_MemorySnapShot_Capture(BOOL bSave);
extern void M68000_Decode_MemorySnapShot_Capture(BOOL bSave);
extern void M68000_BusError(unsigned long addr);
extern void M68000_AddressError(unsigned long addr);
extern void M68000_Exception(void);
