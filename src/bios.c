/*
  Hatari

  Bios Handler (Trap #13)

  We intercept and direct some Bios calls to handle input/output to RS-232 or the printer etc...
*/

#include "main.h"
#include "debug.h"
#include "decode.h"
#include "floppy.h"
#include "m68000.h"
#include "misc.h"
#include "printer.h"
#include "rs232.h"
#include "stMemory.h"


/*-----------------------------------------------------------------------*/
/*
  BIOS Return input device status
  Call 1
*/
BOOL Bios_Bconstat(unsigned long Params)
{
  unsigned short Dev;

  Dev = STMemory_ReadWord(Params+SIZE_WORD);
  switch(Dev) {
    case 0:                                   /* PRT: Centronics */
      Regs[REG_D0] = 0;                       /* No characters ready(cannot read from printer) */
      return(TRUE);
    case 1:                                   /* AUX: RS-232 */
      if (RS232_GetStatus())
        Regs[REG_D0] = -1;                    /* Chars waiting */
      else
        Regs[REG_D0] = 0;
      return(TRUE);
  }

  return(FALSE);
}

/*-----------------------------------------------------------------------*/
/*
  BIOS Read character from device
  Call 2
*/
BOOL Bios_Bconin(unsigned long Params)
{
  unsigned short Dev;
  unsigned char Char;

  Dev = STMemory_ReadWord(Params+SIZE_WORD);
  switch(Dev) {
    case 0:                                   /* PRT: Centronics */
      Regs[REG_D0] = 0;                       /* Force NULL character(cannot read from printer) */
      return(TRUE);
    case 1:                                   /* AUX: RS-232 */
      RS232_ReadBytes(&Char,1);
      Regs[REG_D0] = Char;
      return(TRUE);
  }

  return(FALSE);
}

/*-----------------------------------------------------------------------*/
/*
  BIOS Write character to device
  Call 3
*/
BOOL Bios_Bconout(unsigned long Params)
{
  unsigned short Dev;
  unsigned char Char;

  Dev = STMemory_ReadWord(Params+SIZE_WORD);
  Char = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD);
  switch(Dev) {
    case 0:                                   /* PRT: Centronics */
      Printer_TransferByteTo(Char);
      return(TRUE);
    case 1:                                   /* AUX: RS-232 */
      RS232_TransferBytesTo(&Char,1);
      return(TRUE);
  }

  return(FALSE);
}

/*-----------------------------------------------------------------------*/
/*
  BIOS Read/Write disc sector
  Call 4
*/
BOOL Bios_RWabs(unsigned long Params)
{
#ifdef DEBUG_TO_FILE
  char *pBuffer;
  unsigned short int RWFlag,Number,RecNo,Dev;

  /* Read details from stack */
  RWFlag = STMemory_ReadWord(Params+SIZE_WORD);
  pBuffer = (char *)STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD);
  Number = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_LONG);
  RecNo = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_LONG+SIZE_WORD);  
  Dev = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_LONG+SIZE_WORD+SIZE_WORD);

  Debug_FDC("RWABS %s,%d,0x%X,%d,%d\n",EmulationDrives[Dev].szFileName,RWFlag,(char *)STRAM_ADDR(pBuffer),RecNo,Number);
#endif

  return(FALSE);
}

/*-----------------------------------------------------------------------*/
/*
  BIOS Return output device status
  Call 8
*/
BOOL Bios_Bcostat(unsigned long Params)
{
  unsigned short Dev;

  Dev = STMemory_ReadWord(Params+SIZE_WORD);
  switch(Dev) {
    case 0:                                   /* PRT: Centronics */
      Regs[REG_D0] = -1;                      /* Device ready */
      return(TRUE);
    case 1:                                   /* AUX: RS-232 */
      Regs[REG_D0] = -1;                      /* Device ready */
      return(TRUE);
  }

  return(FALSE);
}

/*-----------------------------------------------------------------------*/
/*
  BIOS Media change
  Call 9
*/
BOOL Bios_Mediach(unsigned long Params)
{
  unsigned short int Dev,ImageChanged=0;

  /* Read details from stack */
  Dev = STMemory_ReadWord(Params+SIZE_WORD);

  /* Check we are trying to access a floppy drive? */
  if (Dev<2) {
    /* Have we inserted a new disc image into this drive? */
    if (EmulationDrives[Dev].bMediaChanged) {
      EmulationDrives[Dev].bMediaChanged = FALSE;
      ImageChanged = 2;      /* We did change disc image */
    }
  }

  /* Set 0 if was not changed or 2 if was */
  Regs[REG_D0] = ImageChanged;

  return(TRUE);
}

/*-----------------------------------------------------------------------*/
/*
  Check Bios call and see if we need to re-direct to our own routines
  Return TRUE if we've handled the exception, else return FALSE to let TOS attempt it
*/
BOOL Bios(void)
{
  unsigned long Params;
  unsigned short int BiosCall;

  /* Get call */
  Params = Regs[REG_A7];
  BiosCall = STMemory_ReadWord(Params);
//  Debug_File("BIOS %d\n",BiosCall);

  /* Intercept? */
  switch(BiosCall) {
    case 0x1:
      return(Bios_Bconstat(Params));
    case 0x2:
      return(Bios_Bconin(Params));
    case 0x3:
      return(Bios_Bconout(Params));
    case 0x4:
      return(Bios_RWabs(Params));
    case 0x8:
      return(Bios_Bcostat(Params));
    case 0x9:
      return(Bios_Mediach(Params));

    default:  /* Call as normal! */
      return(FALSE);
  }
}
