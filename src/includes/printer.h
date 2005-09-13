/*
  Hatari - printer.h

  function prototypes for pritner.c / Printing interface
*/

extern void Printer_Init(void);
extern void Printer_UnInit(void);
extern void Printer_CloseAllConnections(void);
extern BOOL Printer_OpenFile(void);
extern void Printer_CloseFile(void);
extern void Printer_EmptyFile(void);
extern void Printer_ResetInternalBuffer(void);
extern void Printer_ResetCharsOnLine(void);
extern BOOL Printer_EmptyInternalBuffer(void);
extern BOOL Printer_ValidByte(unsigned char Byte);
extern void Printer_AddByteToInternalBuffer(unsigned char Byte);
extern void Printer_AddTabToInternalBuffer(void);
extern BOOL Printer_TransferByteTo(unsigned char Byte);
extern void Printer_CheckIdleStatus(void);

