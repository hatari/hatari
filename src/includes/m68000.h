/*
  Hatari
*/

extern void M68000_Reset(BOOL bCold);
extern void M68000_MemorySnapShot_Capture(BOOL bSave);
extern void M68000_Decode_MemorySnapShot_Capture(BOOL bSave);
extern void M68000_BusError(unsigned long addr);
extern void M68000_AddressError(unsigned long addr);
extern void M68000_Exception(void);
extern void M68000_Line_A_OpCode(void);
extern void M68000_Line_A_Trap(void);
extern void M68000_Line_F_OpCode(void);
extern void M68000_OutputHistory();
