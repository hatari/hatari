/*
  Hatari

  function prototypes for pritner.c / Printing interface

  cleanedup and corrected by Matthias Arndt <marndt@asmsoftware.de>
  9 Aug 2003
*/

extern void Printer_Init(void);
extern void Printer_UnInit(void);
extern void Printer_CloseAllConnections(void);
extern BOOL Printer_OpenDiscFile(void);
extern void Printer_CloseDiscFile(void);
extern void Printer_EmptyDiscFile(void);
extern void Printer_ResetInternalBuffer(void);
extern void Printer_ResetCharsOnLine(void);
extern BOOL Printer_EmptyInternalBuffer(void);
extern BOOL Printer_ValidByte(unsigned char Byte);
extern void Printer_AddByteToInternalBuffer(unsigned char Byte);
extern void Printer_AddTabToInternalBuffer(void);
extern BOOL Printer_TransferByteTo(unsigned char Byte);
extern void Printer_CheckIdleStatus(void);

