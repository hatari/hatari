/*
  Hatari

  Programmable Sound Generator (YM-2149) - PSG

  ChangeLog:

  9 Aug 2003  Matthias Arndt <marndt@asmsoftware.de>
	- added hook for printer dispatcher to PSG Port B (Reg 15)
	- added flag t odecide if last write did go to IOB
*/

#include "main.h"
#include "memAlloc.h"
#include "memorySnapShot.h"
#include "sound.h"
#include "psg.h"

/* printer.h  because Printer I/O goes thorugh PSG Register 15 */
#include "printer.h"

unsigned char PSGRegisterSelect = 0;                  /* 0xff8800 (read/write) */
unsigned char PSGRegisters[16] = { 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0 };  /* Register in PSG, see PSG_REG_xxxx */

/* boolean flag: did the last PSG write go to IOB? */
BOOL bPSG_LastWriteToIOB;

/*-----------------------------------------------------------------------*/
/*
  Reset variables used in PSG
*/
void PSG_Reset(void)
{
  PSGRegisterSelect = 0;
  Memory_Clear(PSGRegisters,sizeof(unsigned char)*16);
  bPSG_LastWriteToIOB=FALSE;
}


/*-----------------------------------------------------------------------*/
/*
  Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
*/
void PSG_MemorySnapShot_Capture(BOOL bSave)
{
  /* Save/Restore details */
  MemorySnapShot_Store(&PSGRegisterSelect,sizeof(PSGRegisterSelect));
  MemorySnapShot_Store(PSGRegisters,sizeof(PSGRegisters));
}


/*-----------------------------------------------------------------------*/
/*
  Write byte to 0xff88000, this is used as a selector for when we read/write to address 0xff8802
*/
void PSG_WriteSelectRegister(unsigned short v)
{
 PSGRegisterSelect = v & 0x0f;              /* Store register to select (value in bits 0-3) */
}


/*-----------------------------------------------------------------------*/
/*
  Read byte from 0xff8800, returns to PSG data
*/
unsigned short PSG_ReadSelectRegister(void)
{
 /* Read data last selected by register */
 return PSGRegisters[PSGRegisterSelect];    /* Return value from PSGRegisters[] */
}


/*-----------------------------------------------------------------------*/
/*
  Write byte to 0xff8802, stores according to PSG select register(write 0xff8800)
*/
void PSG_WriteDataRegister(unsigned short bl)
{
 Sound_Update();                            /* Create samples up until this point with current values */
 PSGRegisters[PSGRegisterSelect]=bl;        /* Write value to PSGRegisters[] */
 if( PSGRegisterSelect==13 )                /* Whenever 'write' to register 13, cause envelope to reset */
  {
   bEnvelopeFreqFlag = TRUE;
   bWriteEnvelopeFreq = TRUE;
  }

 /* Matthias Arndt <marndt@asmsoftware.de>    9 Aug 2003
  * Port B (Printer port) - writing here needs to be dispatched to the printer
  * STROBE (Port A bit5) does a short LOW and back to HIGH when the char is valid
  * for printing you need to write your data and you need to toggle STROBE
  * (like EmuTOS does)....therefor we print when STROBE gets low and last PSG write
  * was to Port B
  */

 if( PSGRegisterSelect==PSG_REG_IO_PORTA )
  {
    /* is STROBE  LOW?  */
    if((PSGRegisters[PSG_REG_IO_PORTA]&16)==0)
	  /* did the last write go to IOB? then we want to print something... */
	  if(bPSG_LastWriteToIOB==TRUE)
		Printer_TransferByteTo(((unsigned char) PSGRegisters[PSG_REG_IO_PORTB] & 0xff));

	bPSG_LastWriteToIOB=FALSE;
  }
 else
  if( PSGRegisterSelect==PSG_REG_IO_PORTB )
   {
     bPSG_LastWriteToIOB=TRUE;
   }
  else
     bPSG_LastWriteToIOB=FALSE;

 /* Check registers 8,9 and 10 which are 'amplitude' for each channel and store if wrote to(to check for sample playback) */
 if( PSGRegisterSelect==8 )
   bWriteChannelAAmp=TRUE;
 else if( PSGRegisterSelect==9 )
   bWriteChannelBAmp=TRUE;
 else if( PSGRegisterSelect==10 )
   bWriteChannelCAmp=TRUE;
}


/*-----------------------------------------------------------------------*/
/*
  Read byte from 0xff8802, returns 0xff
*/
unsigned short PSG_ReadDataRegister(void)
{
 return 0xff;
}
