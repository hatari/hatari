/*
  Hatari
*/

extern void Printer_Init(void);
extern void Printer_UnInit(void);
extern void Printer_CloseAllConnections(void);
extern void Printer_CloseDiscFile(void);
extern void Printer_CloseWindowsPrinter(void);
extern void Printer_ResetInternalBuffer(void);
extern void Printer_ResetCharsOnLine(void);
extern BOOL Printer_EmptyInternalBuffer(void);
extern void Printer_AddByteToInternalBuffer(unsigned char Byte);
extern BOOL Printer_TransferByteTo(unsigned char Byte);
extern void Printer_CheckIdleStatus(void);

