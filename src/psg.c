/*
  Hatari

  Programmable Sound Generator (YM-2149) - PSG
*/

#include "main.h"
#include "memAlloc.h"
#include "memorySnapShot.h"
#include "sound.h"

unsigned char PSGRegisterSelect = 0;                  /* 0xff8800 (read/write) */
unsigned char PSGRegisters[16] = { 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0 };  /* Register in PSG, see PSG_REG_xxxx */


/*-----------------------------------------------------------------------*/
/*
  Reset variables used in PSG
*/
void PSG_Reset(void)
{
  PSGRegisterSelect = 0;
  Memory_Clear(PSGRegisters,sizeof(unsigned char)*16);
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
