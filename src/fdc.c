/*
  Hatari

  Floppy Disc Controller(FDC) emulation. We need to simulate the movement of the head of the
  floppy disc drive to accurately perform the FDC commands, such as 'Step'. The is important
  for ST demo disc images. We have to go into a lot of details - including the start up/stop
  of the drive motor.
  To help with this emulation, we keep our own internal commands which are checked each HBL
  to perform the transfer of data from our disc image into the ST RAM area by simulating the
  DMA.
*/

#include "main.h"
#include "debug.h"
#include "decode.h"
#include "dialog.h"
#include "fdc.h"
#include "hdc.h"
#include "floppy.h"
#include "ikbd.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "misc.h"
#include "psg.h"
#include "stMemory.h"


/*
  Floppy Disc Controller

Programmable Sound Generator (YM-2149)

  0xff8800(even byte)  - PSG Register Data (Read, used for parallel port)
            - PSG Register Select (Write)

  Write to bits 0-3 to select PSG register to use(then write data to 0xfff8802)
    Value    Register

    0000    Channel A Fine Tune
    0001    Channel A Coarse Tune
    0010    Channel B Fine Tune
    0011    Channel B Coarse Tune
    0100    Channel C Fine Tune
    0101    Channel C Coarse Tune
    0110    Noise Generator Control
    0111    Mixer Control - I/O enable
    1000    Channel A Amplitude
    1001    Channel B Amplitude
    1010    Channel C Amplitude
    1011    Envelope Period Fine Tune
    1100    Envelope Peroid Coarse Tune
    1101    Envelope Shape
    1110    I/O Port A Select (Write only)
    1111    I/O Port B Select

  0xfff8802(even byte)  - Bits according to 0xff8800 Register select

  1110(Register 14) - I/O Port A
    Bit 0 - Floppy side 0/1
    Bit 1 - Floppy drive 0 select
    Bit 2 - Floppy drive 1 select
    Bit 3 - RS232 Ready to send (RTS)
    Bit 4 - RS232 Data Terminal Ready (DTR)
    Bit 5 - Centronics Strobe
    Bit 6 - General Purpose Output
    Bit 7 - Reserved

ACSI DMA and Floppy Disc Controller(FDC)
  0xff8604 - information from file '1772.info.txt, by David Gahris' (register r0)
  (write) - Disk controller
  (read) - Disk controller status
    Bit 0 - Busy.  This bit is 1 when the 177x is busy.  This bit is 0 when the 177x is free for CPU commands.
    Bit 1 - Index / Data Request.  On Type I commands, this bit is high during the index pulse that occurs once
      per disk rotation.  This bit is low at all times other than the index pulse.  For Type II and III commands,
      Bit 1 high signals the CPU to handle the data register in order to maintain a continuous flow of data.
      Bit 1 is high when the data register is full during a read or when the data register is empty during a write.
      "Worst case service time" for Data Request is 23.5 cycles.
    Bit 2 - Track Zero / Lost Data.  After Type I commands, this bit is 0 if the mechanism is at track zero.
      This bit is 1 if the head is not at track zero.  After Type II or III commands, this bit is 1 if the
      CPU did not respond to Data Request (Status bit 1) in time for the 177x to maintain a continuous data flow.
      This bit is 0 if the CPU responded promptly to Data Request.
    Bit 3 - CRC Error.  This bit is high if a sector CRC on disk does not match the CRC which the 177x
      computed from the data.  The CRC polynomial is x^16+x^12+x^5+1.  If the stored CRC matches the newly
      calculated CRC, the CRC Error bit is low.  If this bit and the Record Not Found bit are set, the error
      was in an ID field.  If this bit is set but Record Not Found is clear, the error was in a data field.
    Bit 4 - Record Not Found.  This bit is set if the 177x cannot find the track, sector, or side which
      the CPU requested.  Otherwise, this bit is clear.
    Bit 5 - Spin-up / Record Type.  For Type I commands, this bit is low during the 6-revolution motor
      spin-up time.  This bit is high after spin-up.  For Type II and Type III commands, Bit 5 low
      indicates a normal data mark.  Bit 5 high indicates a deleted data mark.
    Bit 6 - Write Protect.  This bit is not used during reads.  During writes, this bit is high when the disk is write protected.
    Bit 7 - Motor On.  This bit is high when the drive motor is on, and low when the motor is off.

  0xff8606 - DMA Status(read), DMA Mode Control(write) - NOTE bits 0,9-15 are not used
    Bit 1 - FDC Pin A0 (See below)
    Bit 2 - FDC Pin A1
    Bit 3 - FDC/HDC Register Select
    Bit 4 - FDC/Sector count select
    Bit 5 - Reserved
    Bit 6 - Enable/Disable DMA
    Bit 7 - HDC/FDC
    Bit 8 - Read/Write

    A1  A0    Read        Write(bit 8==1)
    0  0    Status        Command
    0  1    Track Register    Track Register
    1  0    Sector Register    Sector Register
    1  1    Data Register    Data Register


  This handles any read/writes to the FDC and PSG. FDC commands are then sent through to read/write
  disc sector code. We have full documents on 1772 FDC, but they use r0,r1,r2,r3 etc.. which are
  not seen at first glance as Atari access them via the A0,A1 bits. Once this is understood it
  is all relativly easy as a lot of information can be ignored as we are using disc images and
  not actual discs. We do NOT support the reading of the PC's A: drive - newer PC's cannot read
  an ST single sided disc, ST discs are very old and so are dirty which gets onto the PC drive heads
  and ruins them and also support for disc sector access under Windows is... well not well documented!

  According to the documentation INTRQ is generated at the completion of each command(causes an interrupt
  in the MFP). INTRQ is reset by reading the status register OR by loading a new command. So, does this
  mean the GPIP? Or does it actually CANCEL the interrupt? Can this be done?
*/

#define ENABLE_FLOPPY_SAVING                              /* Save to floppies */

unsigned short int DiscControllerStatus_ff8604rd;         /* 0xff8604 (read) */
unsigned short int DiscControllerWord_ff8604wr;           /* 0xff8604 (write) */
unsigned short int DMAStatus_ff8606rd;                    /* 0xff8606 (read) */
unsigned short int DMAModeControl_ff8606wr,DMAModeControl_ff8606wr_prev;  /* 0xff8606 (write,store prev for 'toggle' checks) */

unsigned short int FDCCommandRegister;
short int FDCTrackRegister,FDCSectorRegister,FDCDataRegister;
short int FDCSectorCountRegister;
int FDCEmulationCommand;                                  /* FDC emulation command currently being exceuted */
int FDCEmulationRunning;                                  /* Running command under above */
int FDCStepDirection;                                     /* +Track on 'Step' command */
short int DiscControllerByte;                             /* Used to pass parameter back to assembler */
BOOL bDMAWaiting;                                         /* Is DMA waiting to copy? */
int bMotorOn;                                             /* Is motor on? */
int MotorSlowingCount;                                    /* Counter used to slow motor before stopping */

short int nReadWriteTrack;                                /* Parameters used in sector read/writes */
short int nReadWriteSector;
short int nReadWriteSide;
short int nReadWriteDev;
unsigned short int nReadWriteSectorsPerTrack;
short int nReadWriteSectors;

unsigned char DMASectorWorkSpace[NUMBYTESPERSECTOR];      /* Workspace used to copy to/from for floppy DMA */


/*-----------------------------------------------------------------------*/
/*
  Reset variables used in FDC
*/
void FDC_Reset(void)
{
  /* Clear out FDC registers */
  DiscControllerStatus_ff8604rd = 0;
  DiscControllerWord_ff8604wr = 0;
  DMAStatus_ff8606rd = 0x01;
  DMAModeControl_ff8606wr = DMAModeControl_ff8606wr_prev = 0;
  FDC_ResetDMAStatus();
  FDCCommandRegister = 0;
  FDCTrackRegister = 0;
  FDCSectorRegister = 1;
  FDCDataRegister = 0;
  FDCSectorCountRegister = 0;

  FDCEmulationCommand = FDCEMU_CMD_NULL;        /* FDC emulation command currently being exceuted */
  FDCEmulationRunning = FDCEMU_RUN_NULL;        /* Running command under above */

  FDCStepDirection = 1;                         /* +Track on 'Step' command */
  bDMAWaiting = FALSE;                          /* No DMA waiting */
  bMotorOn = FALSE;                             /* Motor off */
  MotorSlowingCount = 0;                        /* Counter for motor slowing down before stopping */
}


/*-----------------------------------------------------------------------*/
/*
  Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
*/
void FDC_MemorySnapShot_Capture(BOOL bSave)
{
  /* Save/Restore details */
  MemorySnapShot_Store(&DiscControllerStatus_ff8604rd,sizeof(DiscControllerStatus_ff8604rd));
  MemorySnapShot_Store(&DiscControllerWord_ff8604wr,sizeof(DiscControllerWord_ff8604wr));
  MemorySnapShot_Store(&DMAStatus_ff8606rd,sizeof(DMAStatus_ff8606rd));
  MemorySnapShot_Store(&DMAModeControl_ff8606wr,sizeof(DMAModeControl_ff8606wr));
  MemorySnapShot_Store(&DMAModeControl_ff8606wr_prev,sizeof(DMAModeControl_ff8606wr_prev));
  MemorySnapShot_Store(&FDCCommandRegister,sizeof(FDCCommandRegister));
  MemorySnapShot_Store(&FDCTrackRegister,sizeof(FDCTrackRegister));
  MemorySnapShot_Store(&FDCSectorRegister,sizeof(FDCSectorRegister));
  MemorySnapShot_Store(&FDCDataRegister,sizeof(FDCDataRegister));
  MemorySnapShot_Store(&FDCSectorCountRegister,sizeof(FDCSectorCountRegister));
  MemorySnapShot_Store(&FDCEmulationCommand,sizeof(FDCEmulationCommand));
  MemorySnapShot_Store(&FDCEmulationRunning,sizeof(FDCEmulationRunning));
  MemorySnapShot_Store(&FDCStepDirection,sizeof(FDCStepDirection));
  MemorySnapShot_Store(&DiscControllerByte,sizeof(DiscControllerByte));
  MemorySnapShot_Store(&bDMAWaiting,sizeof(bDMAWaiting));
  MemorySnapShot_Store(&bMotorOn,sizeof(bMotorOn));
  MemorySnapShot_Store(&MotorSlowingCount,sizeof(MotorSlowingCount));
  MemorySnapShot_Store(&nReadWriteTrack,sizeof(nReadWriteTrack));
  MemorySnapShot_Store(&nReadWriteSector,sizeof(nReadWriteSector));
  MemorySnapShot_Store(&nReadWriteSide,sizeof(nReadWriteSide));
  MemorySnapShot_Store(&nReadWriteDev,sizeof(nReadWriteDev));
  MemorySnapShot_Store(&nReadWriteSectorsPerTrack,sizeof(nReadWriteSectorsPerTrack));
  MemorySnapShot_Store(&nReadWriteSectors,sizeof(nReadWriteSectors));
  MemorySnapShot_Store(DMASectorWorkSpace,sizeof(DMASectorWorkSpace));
}


/*-----------------------------------------------------------------------*/
/*
  Turn floppy motor on
*/
void FDC_TurnMotorOn(void)
{
  bMotorOn = TRUE;                  /* Turn motor on */
  MotorSlowingCount = 0;
}


/*-----------------------------------------------------------------------*/
/*
  Turn floppy motor off(this sets a count as it takes a set amount of time for the motor to slow to a halt)
*/
void FDC_TurnMotorOff(void)
{
  MotorSlowingCount = 10;           /* Set timer so takes 'x' HBLs before turn off... */
}


/*-----------------------------------------------------------------------*/
/*
  Update floppy drive motor each HBL, to simulate slowing down and stopping for drive; needed for New Zealand Story(PP_001)
*/
void FDC_UpdateMotor(void)
{
  /* Is drive slowing down? Decrement counter */
  if (MotorSlowingCount>0) {
    MotorSlowingCount--;

    if (MotorSlowingCount==0)
      bMotorOn = FALSE;             /* Motor finally stopped */
  }
}


/*-----------------------------------------------------------------------*/
/*
  Reset DMA Status (RD 0xff8606)

  This is done by 'toggling' bit 8 of the DMA Mode Control register
*/
void FDC_ResetDMAStatus(void)
{
  DMAStatus_ff8606rd = 0;           /* Clear out */

  FDCSectorCountRegister = 0;
  FDC_SetDMAStatus(FALSE);          /* Set no error */

  HDCSectorCount = 0;
  HDCCommand.byteCount = 0;         /* Reset HDC command status */
  HDCCommand.returnCode = 0xFF;
}


/*-----------------------------------------------------------------------*/
/*
  Set DMA Status (RD 0xff8606)

  NOTE FDC Doc's are incorrect - Bit 0 is '0' on error (See TOS floprd, Ninja III etc...)
  Look like Atari(yet again) connected the hardware up differently to the spec'

  Bit 0 - _Error Status (0=Error)
  Bit 1 - _Sector Count Zero Status (0=Sector Count Zero)
  Bit 2 - _Data Request Inactive Status
*/
void FDC_SetDMAStatus(BOOL bError)
{
  DMAStatus_ff8606rd &= 0x1;        /* Clear(except for error) */

  /* Set error condition - NOTE this is incorrect in the FDC Doc's! */
  if (!bError)
    DMAStatus_ff8606rd |= 0x1;

  /* Set zero sector count */

  if (DMAModeControl_ff8606wr&0x08)         /* Get which sector count? */
    DMAStatus_ff8606rd |= (HDCSectorCount)?0:0x2;         /* HDC */
  else
    DMAStatus_ff8606rd |= (FDCSectorCountRegister)?0x2:0; /* FDC */
  /* Perhaps the DRQ should be set here */
}


/*-----------------------------------------------------------------------*/
/*
  Read DMA Status (RD 0xff8606)
*/
long FDC_ReadDMAStatus(void)
{
 return (0xffff0000|DMAStatus_ff8606rd);
}


/*-----------------------------------------------------------------------*/
/*
*/
void FDC_UpdateDiscDrive(void)
{
  /* Set details for current selecte drive */
  nReadWriteDev = FDC_FindFloppyDrive();

  if (EmulationDrives[nReadWriteDev].bDiscInserted)
    Floppy_FindDiscDetails(EmulationDrives[nReadWriteDev].pBuffer,EmulationDrives[nReadWriteDev].nImageBytes,&nReadWriteSectorsPerTrack,NULL);
}


/*-----------------------------------------------------------------------*/
/*
  When write to 0xff8606 (DMA Mode Control) check bit '8' toggle. This causes DMA status reset
*/
void FDC_CheckForDMAStatusReset(void)
{
  /* Test for 'toggle' of _read/_write bit '8' */
  if ((DMAModeControl_ff8606wr_prev ^ DMAModeControl_ff8606wr)&0x0100)
    FDC_ResetDMAStatus();
}


/*-----------------------------------------------------------------------*/
/*
  Set disc controller status (RD 0xff8604)
*/
void FDC_SetDiscControllerStatus(void)
{
  /* Update disc */
  FDC_UpdateDiscDrive();

  /* Clear out to default */
  DiscControllerStatus_ff8604rd = 0;

  /* ONLY do this if we are running a Type I command */
  if ((FDCCommandRegister&0x80)==0) {          /* Type I - Restore,Seek,Step,Step-In,Step-Out */
    if (FDCTrackRegister==0)
      DiscControllerStatus_ff8604rd |= 0x4;    /* Bit 2 - Track Zero, '0' if head is NOT at zero */
  }

  /* If no disc inserted, tag as error */
  if (!EmulationDrives[nReadWriteDev].bDiscInserted)
    DiscControllerStatus_ff8604rd |= 0x10;     /* RNF - Record not found, ie no disc in drive */
}


/*-----------------------------------------------------------------------*/
/*
  Return device for FDC, check PORTA bits 1,2(0=on,1=off)
*/
int FDC_FindFloppyDrive(void)
{
  /* Check Drive A first */
  if ((PSGRegisters[PSG_REG_IO_PORTA]&0x2)==0)
    return(0);                    /* Device 0 (A:) */
  /* If off, check Drive B */
  if ((PSGRegisters[PSG_REG_IO_PORTA]&0x4)==0)
    return(1);                    /* Device 1 (B:) */

  /* None appear to be selected so default to Drive A */
  return(0);                      /* Device 0 (A:) */
}


/*-----------------------------------------------------------------------*/
/*
  Acknowledge FDC interrupt
*/
void FDC_AcknowledgeInterrupt(void)
{
  /* Acknowledge in MFP circuit, pass bit, enable, pending */
  MFP_InputOnChannel(MFP_FDCHDC_BIT,MFP_IERB,&MFP_IPRB);
  MFP_GPIP &= ~0x20;
}


/*-----------------------------------------------------------------------*/
/*
  Copy parameters for disc sector/s read/write
*/
void FDC_SetReadWriteParameters(int nSectors)
{
  /* Copy read/write details so we can modify them */
  nReadWriteTrack = FDCTrackRegister;
  nReadWriteSector = FDCSectorRegister;
  nReadWriteSide = (~PSGRegisters[PSG_REG_IO_PORTA]) & 0x01;
  nReadWriteSectors = nSectors;
  /* Update disc */
  FDC_UpdateDiscDrive();
}


/*-----------------------------------------------------------------------*/
/*
  Update floppy drive on each HBL(approx' 512 cycles)
*/
void FDC_UpdateHBL(void)
{
  /* Do we have a DMA ready to copy? */
  if (bDMAWaiting) {
    /* Yes, copy it */
    FDC_DMADataFromFloppy();
    /* Signal done */
    bDMAWaiting = FALSE;
  }

  /* Update drive motor */
  FDC_UpdateMotor();

  /* Is FDC active? */
  if (FDCEmulationCommand!=FDCEMU_CMD_NULL) {
    /* Which command are we running? */
    switch(FDCEmulationCommand) {
      case FDCEMU_CMD_RESTORE:
        FDC_UpdateRestoreCmd();
        break;
      case FDCEMU_CMD_SEEK:
        FDC_UpdateSeekCmd();
        break;
      case FDCEMU_CMD_STEP:
        FDC_UpdateStepCmd();
        break;
      case FDCEMU_CMD_STEPIN:
        FDC_UpdateStepInCmd();
        break;
      case FDCEMU_CMD_STEPOUT:
        FDC_UpdateStepOutCmd();
        break;

      case FDCEMU_CMD_READSECTORS:
      case FDCEMU_CMD_READMULTIPLESECTORS:
        FDC_UpdateReadSectorsCmd();
        break;
      case FDCEMU_CMD_WRITESECTORS:
      case FDCEMU_CMD_WRITEMULTIPLESECTORS:
        FDC_UpdateWriteSectorsCmd();
        break;
    }

    /* Set disc controller status (RD 0xff8604) */
    FDC_SetDiscControllerStatus();
  }
}


/*-----------------------------------------------------------------------*/
/*
  Run 'RESTORE' command
*/
void FDC_UpdateRestoreCmd(void)
{
  /* Which command is running? */
  switch (FDCEmulationRunning) {
    case FDCEMU_RUN_RESTORE_SEEKTOTRACKZERO:
      /* Are we at track zero? */
      if (FDCTrackRegister>0)
        FDCTrackRegister--;             /* Move towards track zero */
      else {
        FDCTrackRegister = 0;           /* We're there */
        FDCEmulationRunning = FDCEMU_RUN_RESTORE_COMPLETE;
      }
      break;
    case FDCEMU_RUN_RESTORE_COMPLETE:
      /* Acknowledge interrupt, move along there's nothing more to see */
      FDC_AcknowledgeInterrupt();
      /* Set error */
      FDC_SetDMAStatus(FALSE);          /* No DMA error */
      /* Done */
      FDCEmulationCommand = FDCEMU_CMD_NULL;
      /* Turn motor off */
      FDC_TurnMotorOff();
      break;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Run 'SEEK' command
*/
void FDC_UpdateSeekCmd(void)
{
  /* Which command is running? */
  switch (FDCEmulationRunning) {
    case FDCEMU_RUN_SEEK_TOTRACK:
      /* Are we at the selected track? */
      if (FDCTrackRegister==FDCDataRegister)
        FDCEmulationRunning = FDCEMU_RUN_SEEK_COMPLETE;
      else {
        /* No, seek towards track */
        if (FDCDataRegister<FDCTrackRegister)
          FDCTrackRegister--;
        else
          FDCTrackRegister++;
      }
      break;
    case FDCEMU_RUN_SEEK_COMPLETE:
      /* Acknowledge interrupt, move along there's nothing more to see */
      FDC_AcknowledgeInterrupt();
      /* Set error */
      FDC_SetDMAStatus(FALSE);          /* No DMA error */
      /* Done */
      FDCEmulationCommand = FDCEMU_CMD_NULL;
      /* Turn motor off */
      FDC_TurnMotorOff();
      break;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Run 'STEP' command
*/
void FDC_UpdateStepCmd(void)
{
  /* Which command is running? */
  switch (FDCEmulationRunning) {
    case FDCEMU_RUN_STEP_ONCE:
      /* Move head by one track in same direction as last step */
      FDCTrackRegister += FDCStepDirection;
      if (FDCTrackRegister<0)             /* Limit to stop */
        FDCTrackRegister = 0;

      FDCEmulationRunning = FDCEMU_RUN_STEP_COMPLETE;
      break;
    case FDCEMU_RUN_STEP_COMPLETE:
      /* Acknowledge interrupt, move along there's nothing more to see */
      FDC_AcknowledgeInterrupt();
      /* Set error */
      FDC_SetDMAStatus(FALSE);            /* No DMA error */
      /* Done */
      FDCEmulationCommand = FDCEMU_CMD_NULL;
      /* Turn motor off */
      FDC_TurnMotorOff();
      break;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Run 'STEP IN' command
*/
void FDC_UpdateStepInCmd(void)
{
  /* Which command is running? */
  switch (FDCEmulationRunning) {
    case FDCEMU_RUN_STEPIN_ONCE:
      FDCTrackRegister++;

      FDCEmulationRunning = FDCEMU_RUN_STEPIN_COMPLETE;
      break;
    case FDCEMU_RUN_STEPIN_COMPLETE:
      /* Acknowledge interrupt, move along there's nothing more to see */
      FDC_AcknowledgeInterrupt();
      /* Set error */
      FDC_SetDMAStatus(FALSE);            /* No DMA error */
      /* Done */
      FDCEmulationCommand = FDCEMU_CMD_NULL;
      /* Turn motor off */
      FDC_TurnMotorOff();
      break;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Run 'STEP OUT' command
*/
void FDC_UpdateStepOutCmd(void)
{
  /* Which command is running? */
  switch (FDCEmulationRunning) {
    case FDCEMU_RUN_STEPOUT_ONCE:
      FDCTrackRegister--;
      if (FDCTrackRegister<0)             /* Limit to stop */
        FDCTrackRegister = 0;

      FDCEmulationRunning = FDCEMU_RUN_STEPOUT_COMPLETE;
      break;
    case FDCEMU_RUN_STEPOUT_COMPLETE:
      /* Acknowledge interrupt, move along there's nothing more to see */
      FDC_AcknowledgeInterrupt();
      /* Set error */
      FDC_SetDMAStatus(FALSE);            /* No DMA error */
      /* Done */
      FDCEmulationCommand = FDCEMU_CMD_NULL;
      /* Turn motor off */
      FDC_TurnMotorOff();
      break;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Run 'READ SECTOR/S' command
*/
void FDC_UpdateReadSectorsCmd(void)
{
  /* Which command is running? */
  switch (FDCEmulationRunning) {
    case FDCEMU_RUN_READSECTORS_READDATA:
      /* Read in a sector */
      if (FDC_ReadSectorFromFloppy()) {     /* Read a single sector through DMA */
        FDCSectorCountRegister--;           /* Decrement FDCSectorCount */
        if (FDCSectorCountRegister<=0)
          FDCSectorCountRegister = 0;

        /* Have we finished? */
        nReadWriteSectors--;
        if (nReadWriteSectors<=0)
          FDCEmulationRunning = FDCEMU_RUN_READSECTORS_COMPLETE;

        bDMAWaiting = TRUE;
      }
      else {
        /* Acknowledge interrupt, move along there's nothing more to see */
        FDC_AcknowledgeInterrupt();
        /* Set error */
        FDC_SetDMAStatus(TRUE);             /* DMA error */
        /* Done */
        FDCEmulationCommand = FDCEMU_CMD_NULL;
        /* Turn motor off */
        FDC_TurnMotorOff();
      }
      break;
    case FDCEMU_RUN_READSECTORS_COMPLETE:
      /* Acknowledge interrupt, move along there's nothing more to see */
      FDC_AcknowledgeInterrupt();
      /* Set error */
      FDC_SetDMAStatus(FALSE);              /* No DMA error */
      /* Done */
      FDCEmulationCommand = FDCEMU_CMD_NULL;
      /* Turn motor off */
      FDC_TurnMotorOff();
      break;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Run 'WRITE SECTOR/S' command
*/
void FDC_UpdateWriteSectorsCmd(void)
{
  /* Which command is running? */
  switch (FDCEmulationRunning) {
    case FDCEMU_RUN_WRITESECTORS_WRITEDATA:
      /* Write out a sector */
      if (FDC_WriteSectorFromFloppy()) {    /* Write a single sector through DMA */
        /* Decrement FDCsector count */
        FDCSectorCountRegister--;           /* Decrement FDCSectorCount */
        if (FDCSectorCountRegister<=0)
          FDCSectorCountRegister = 0;

        /* Have we finished? */
        nReadWriteSectors--;
        if (nReadWriteSectors<=0)
          FDCEmulationRunning = FDCEMU_RUN_WRITESECTORS_COMPLETE;

        /* Update DMA pointer */
        FDC_WriteDMAAddress(FDC_ReadDMAAddress()+NUMBYTESPERSECTOR);
      }
      else {
        /* Acknowledge interrupt, move along there's nothing more to see */
        FDC_AcknowledgeInterrupt();
        /* Set error */
        FDC_SetDMAStatus(TRUE);             /* DMA error */
        /* Done */
        FDCEmulationCommand = FDCEMU_CMD_NULL;
        /* Turn motor off */
        FDC_TurnMotorOff();
      }
      break;
    case FDCEMU_RUN_WRITESECTORS_COMPLETE:
      /* Acknowledge interrupt, move along there's nothing more to see */
      FDC_AcknowledgeInterrupt();
      /* Set error */
      FDC_SetDMAStatus(FALSE);              /* No DMA error */
      /* Done */
      FDCEmulationCommand = FDCEMU_CMD_NULL;
      /* Turn motor off */
      FDC_TurnMotorOff();
      break;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Type I Commands

  Restore, Seek, Step, Step-In and Step-Out
*/


/*-----------------------------------------------------------------------*/
void FDC_TypeI_Restore(void)
{
  /* Set emulation to seek to track zero */
  FDCEmulationCommand = FDCEMU_CMD_RESTORE;
  FDCEmulationRunning = FDCEMU_RUN_RESTORE_SEEKTOTRACKZERO;

  FDC_SetDiscControllerStatus();
}


/*-----------------------------------------------------------------------*/
void FDC_TypeI_Seek(void)
{
  /* Set emulation to seek to chosen track */
  FDCEmulationCommand = FDCEMU_CMD_SEEK;
  FDCEmulationRunning = FDCEMU_RUN_SEEK_TOTRACK;

  FDC_SetDiscControllerStatus();
}


/*-----------------------------------------------------------------------*/
void FDC_TypeI_Step(void)
{
  /* Set emulation to step(same direction as last seek executed, eg 'FDCStepDirection') */
  FDCEmulationCommand = FDCEMU_CMD_STEP;
  FDCEmulationRunning = FDCEMU_RUN_STEP_ONCE;

  FDC_SetDiscControllerStatus();
}


/*-----------------------------------------------------------------------*/
void FDC_TypeI_StepIn(void)
{
  /* Set emulation to step in(Set 'FDCStepDirection') */
  FDCEmulationCommand = FDCEMU_CMD_STEPIN;
  FDCEmulationRunning = FDCEMU_RUN_STEPIN_ONCE;
  FDCStepDirection = 1;                 /* Increment track*/

  FDC_SetDiscControllerStatus();
}


/*-----------------------------------------------------------------------*/
void FDC_TypeI_StepOut(void)
{
  /* Set emulation to step out(Set 'FDCStepDirection') */
  FDCEmulationCommand = FDCEMU_CMD_STEPOUT;
  FDCEmulationRunning = FDCEMU_RUN_STEPOUT_ONCE;
  FDCStepDirection = -1;                /* Decrement track */

  FDC_SetDiscControllerStatus();
}


/*-----------------------------------------------------------------------*/
/*
  Type II Commands

  Read Sector, Read Multiple Sectors, Write Sector, Write Multiple Sectors
*/


/*-----------------------------------------------------------------------*/
void FDC_TypeII_ReadSector(void)
{
  /* Set emulation to read a single sector */
  FDCEmulationCommand = FDCEMU_CMD_READSECTORS;
  FDCEmulationRunning = FDCEMU_RUN_READSECTORS_READDATA;
  /* Set reading parameters */
  FDC_SetReadWriteParameters(1);        /* Read in a single sector */

  FDC_SetDiscControllerStatus();
}


/*-----------------------------------------------------------------------*/
void FDC_TypeII_ReadMultipleSectors(void)
{
  /* Set emulation to read sectors */
  FDCEmulationCommand = FDCEMU_CMD_READMULTIPLESECTORS;
  FDCEmulationRunning = FDCEMU_RUN_READSECTORS_READDATA;
  /* Set reading parameters */
  FDC_SetReadWriteParameters(FDCSectorCountRegister);   /* Read multiple sectors */

  FDC_SetDiscControllerStatus();
}


/*-----------------------------------------------------------------------*/
void FDC_TypeII_WriteSector(void)
{
  /* Set emulation to write a single sector */
  FDCEmulationCommand = FDCEMU_CMD_WRITESECTORS;
  FDCEmulationRunning = FDCEMU_RUN_WRITESECTORS_WRITEDATA;
  /* Set writing parameters */
  FDC_SetReadWriteParameters(1);                        /* Write out a single sector */

  FDC_SetDiscControllerStatus();
}


/*-----------------------------------------------------------------------*/
void FDC_TypeII_WriteMultipleSectors(void)
{
  /* Set emulation to write sectors */
  FDCEmulationCommand = FDCEMU_CMD_WRITEMULTIPLESECTORS;
  FDCEmulationRunning = FDCEMU_RUN_WRITESECTORS_WRITEDATA;
  /* Set witing parameters */
  FDC_SetReadWriteParameters(FDCSectorCountRegister);   /* Write multiple sectors */

  FDC_SetDiscControllerStatus();
}


/*-----------------------------------------------------------------------*/
/*
  Type III Commands

  Read Address, Read Track, Write Track
*/


/*-----------------------------------------------------------------------*/
void FDC_TypeIII_ReadAddress(void)
{
}


/*-----------------------------------------------------------------------*/
void FDC_TypeIII_ReadTrack(void)
{
  /* Set emulation to read a single track */
  FDCEmulationCommand = FDCEMU_CMD_READSECTORS;
  FDCEmulationRunning = FDCEMU_RUN_READSECTORS_READDATA;
  /* Set reading parameters */
  FDC_SetReadWriteParameters(nReadWriteSectorsPerTrack);  /* Read whole track */

  FDC_SetDiscControllerStatus();
}


/*-----------------------------------------------------------------------*/
void FDC_TypeIII_WriteTrack(void)
{
  /* Set emulation to write a single track */
  FDCEmulationCommand = FDCEMU_CMD_WRITESECTORS;
  FDCEmulationRunning = FDCEMU_RUN_WRITESECTORS_WRITEDATA;
  /* Set writing parameters */
  FDC_SetReadWriteParameters(nReadWriteSectorsPerTrack);  /* Write whole track */

  FDC_SetDiscControllerStatus();
}


/*-----------------------------------------------------------------------*/
/*
  Type IV Commands

  Force Interrupt
*/


/*-----------------------------------------------------------------------*/
void FDC_TypeIV_ForceInterrupt(BOOL bCauseCPUInterrupt)
{
  /* Acknowledge interrupt, move along there's nothing more to see */
  if (bCauseCPUInterrupt)
    FDC_AcknowledgeInterrupt();

  /* Reset FDC */
  FDCEmulationCommand = FDCEMU_CMD_NULL;
  FDCEmulationRunning = FDCEMU_RUN_NULL;
}


/*-----------------------------------------------------------------------*/
/*
  Execute Type I commands
*/
void FDC_ExecuteTypeICommands(void)
{
  MFP_GPIP |= 0x20;

  /* Check Type I Command */
  switch(FDCCommandRegister&0xf0) {
    case 0x00:            /* Restore */
      FDC_TypeI_Restore();
      break;
    case 0x10:            /* Seek */
      FDC_TypeI_Seek();
      break;
    case 0x20:            /* Step */
    case 0x30:
      FDC_TypeI_Step();
      break;
    case 0x40:            /* Step-In */
    case 0x50:
      FDC_TypeI_StepIn();
      break;
    case 0x60:            /* Step-Out */
    case 0x70:
      FDC_TypeI_StepOut();
      break;
  }

  /* Signal motor on as we need to execute command */
  FDC_TurnMotorOn();
}


/*-----------------------------------------------------------------------*/
/*
  Execute Type II commands
*/
void FDC_ExecuteTypeIICommands(void)
{
  MFP_GPIP |= 0x20;

  /* Check Type II Command */
  switch(FDCCommandRegister&0xf0) {
    case 0x80:            /* Read Sector */
      FDC_TypeII_ReadSector();
      break;
    case 0x90:            /* Read Sectors */
      FDC_TypeII_ReadMultipleSectors();
      break;
    case 0xa0:            /* Write Sector */
      FDC_TypeII_WriteSector();
      break;
    case 0xb0:            /* Write Sectors */
      FDC_TypeII_WriteMultipleSectors();
      break;
  }

  /* Signal motor on as we need to execute command */
  FDC_TurnMotorOn();
}


/*-----------------------------------------------------------------------*/
/*
  Execute Type III commands
*/
void FDC_ExecuteTypeIIICommands(void)
{
  MFP_GPIP |= 0x20;

  /* Check Type III Command */
  switch(FDCCommandRegister&0xf0) {
    case 0xc0:          /* Read Address */
      FDC_TypeIII_ReadAddress();
      break;
    case 0xe0:          /* Read Track */
      FDC_TypeIII_ReadTrack();
      break;
    case 0xf0:          /* Write Track */
      FDC_TypeIII_WriteTrack();
      break;
  }

  /* Signal motor on as we need to execute command */
  FDC_TurnMotorOn();
}


/*-----------------------------------------------------------------------*/
/*
  Execute Type IV commands
*/
void FDC_ExecuteTypeIVCommands(void)
{
  if (FDCCommandRegister!=0xD8)         /* Is an 'immediate interrupt command'? don't reset interrupt */
    MFP_GPIP |= 0x20;

  /* Check Type IV Command */
  if ((FDCCommandRegister&0x0c)==0)     /* I3 and I2 are clear? If so we don't need a CPU interrupt */
    FDC_TypeIV_ForceInterrupt(FALSE);   /* Force Interrupt - no interrupt */
  else
    FDC_TypeIV_ForceInterrupt(TRUE);    /* Force Interrupt */
}


/*-----------------------------------------------------------------------*/
/*
  Find FDC command type and execute
*/
void FDC_ExecuteCommand(void)
{
  /* Check type of command and execute */
  if ((FDCCommandRegister&0x80)==0)           /* Type I - Restore,Seek,Step,Step-In,Step-Out */
    FDC_ExecuteTypeICommands();
  else if ((FDCCommandRegister&0x40)==0)      /* Type II - Read Sector, Write Sector */
    FDC_ExecuteTypeIICommands();
  else if ((FDCCommandRegister&0xf0)!=0xd0)   /* Type III - Read Address, Read Track, Write Track */
    FDC_ExecuteTypeIIICommands();
  else                                        /* Type IV - Force Interrupt */
    FDC_ExecuteTypeIVCommands();
}


/*-----------------------------------------------------------------------*/
/*
  Write to SectorCount register (WR 0xff8604)
*/
void FDC_WriteSectorCountRegister(void)
{
  FDCSectorCountRegister = DiscControllerWord_ff8604wr;
}


/*-----------------------------------------------------------------------*/
/*
  Write to Command register (WR 0xff8604)
*/
void FDC_WriteCommandRegister(void)
{
  FDCCommandRegister = DiscControllerWord_ff8604wr;
  /* And execute */
  FDC_ExecuteCommand();
}


/*-----------------------------------------------------------------------*/
/*
  Write to Track register (WR 0xff8604)
*/
void FDC_WriteTrackRegister(void)
{
  FDCTrackRegister = DiscControllerWord_ff8604wr;    /* 0...79 */
}


/*-----------------------------------------------------------------------*/
/*
  Write to Track register (WR 0xff8604)
*/
void FDC_WriteSectorRegister(void)
{
  FDCSectorRegister = DiscControllerWord_ff8604wr;  /* 1,2,3..... */
}


/*-----------------------------------------------------------------------*/
/*
  Write to Data register (WR 0xff8604)
*/
void FDC_WriteDataRegister(void)
{
  FDCDataRegister = DiscControllerWord_ff8604wr;
}


/*-----------------------------------------------------------------------*/
/*
  Store byte in FDC registers, when write to 0xff8604
*/
void FDC_WriteDiscControllerByte(void)
{
  HDC_WriteCommandPacket();           /*  Handle HDC functions */

  /* filter hdc commands */
  if ((DMAModeControl_ff8606wr & 0x0018) == 8 &&
      nPartitions == 0) return;

  /* Are we trying to set the SectorCount? */
  if (DMAModeControl_ff8606wr&0x10)         /* Bit 4 */
    FDC_WriteSectorCountRegister();
  else {  /* Write to FDC registers */
    switch(DMAModeControl_ff8606wr&0x6) {   /* Bits 1,2 (A1,A0) */
      case 0x0:                             /* 0 0 - Command register */
        FDC_WriteCommandRegister();
        break;
      case 0x2:                             /* 0 1 - Track register */
        FDC_WriteTrackRegister();
        break;
      case 0x4:                             /* 1 0 - Sector register */
        FDC_WriteSectorRegister();
        break;
      case 0x6:                             /* 1 1 - Data register */
        FDC_WriteDataRegister();
        break;
    }
  }
}


/*-----------------------------------------------------------------------*/
/*
  Read Status/FDC registers, when read from 0xff8604
  Return 'DiscControllerByte'
*/
void FDC_ReadDiscControllerStatusByte(void)
{
  /* return the HDC status reg */
  if(DMAModeControl_ff8606wr == 0x08A) {       /* HDC status reg selected */
    DiscControllerByte = HDCCommand.returnCode;
    return;
  }

  /* old FDC code */
  switch(DMAModeControl_ff8606wr&0x6) {     /* Bits 1,2 (A1,A0) */
    case 0x0:                               /* 0 0 - Status register */
      DiscControllerByte = DiscControllerStatus_ff8604rd;
      if (bMotorOn)
        DiscControllerByte |= 0x80;

      /* Reset FDC GPIP */
      MFP_GPIP |= 0x20;
      break;
    case 0x2:                               /* 0 1 - Track register */
      DiscControllerByte = FDCTrackRegister;
      break;
    case 0x4:                               /* 1 0 - Sector register */
      DiscControllerByte = FDCSectorRegister;
      break;
    case 0x6:                               /* 1 1 - Data register */
      DiscControllerByte = FDCDataRegister;
      break;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Read DMA address from ST's RAM(always up-to-date)
*/
unsigned long FDC_ReadDMAAddress(void)
{
  unsigned long Address;

  /* Build up 24-bit address from hardware registers */
  Address = ((unsigned long)STMemory_ReadByte(0xff8609)<<16) | ((unsigned long)STMemory_ReadByte(0xff860b)<<8) | (unsigned long)STMemory_ReadByte(0xff860d);

  return(Address);
}


/*-----------------------------------------------------------------------*/
/*
  Write DMA address to ST's RAM(always keep up-to-date)
*/
void FDC_WriteDMAAddress(unsigned long Address)
{
  /* Store as 24-bit address */
  STMemory_WriteByte(0xff8609,Address>>16);
  STMemory_WriteByte(0xff860b,Address>>8);
  STMemory_WriteByte(0xff860d,Address);
}


/*-----------------------------------------------------------------------*/
/*
  Read sector from floppy drive into workspace
  We copy the bytes in chunks to simulate reading of the floppy using DMA
*/
BOOL FDC_ReadSectorFromFloppy(void)
{
  /* Copy in 1 sector to our workspace */
  if (Floppy_ReadSectors(nReadWriteDev,(char *)DMASectorWorkSpace,nReadWriteSector,nReadWriteTrack,nReadWriteSide,1,NULL)) {
    /* Update reading/writing parameters */
    nReadWriteSector++;
    if (nReadWriteSector>nReadWriteSectorsPerTrack) {  /* Advance into next track? */
      nReadWriteSector = 1;
      nReadWriteTrack++;
    }
    return(TRUE);
  }

  /* Failed */
  return(FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  Write sector from workspace to floppy drive
  We copy the bytes in chunks to simulate writing of the floppy using DMA
*/
BOOL FDC_WriteSectorFromFloppy(void)
{
  unsigned long Address;

  /* Get DMA address */
  Address = FDC_ReadDMAAddress();

  /* Write out 1 sector from our workspace */
  if (Floppy_WriteSectors(nReadWriteDev,(char *)((unsigned long)STRam+Address),nReadWriteSector,nReadWriteTrack,nReadWriteSide,1,NULL)) {
    /* Update reading/writing parameters */
    nReadWriteSector++;
    if (nReadWriteSector>nReadWriteSectorsPerTrack) {  /* Advance to next track? */
      nReadWriteSector = 1;
      nReadWriteTrack++;
    }
    return(TRUE);
  }

  /* Failed */
  return(FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  Copy data from DMA workspace into ST RAM
*/
void FDC_DMADataFromFloppy(void)
{
  /* Copy data to DMA address */
  memcpy( (char *)((unsigned long)STRam+FDC_ReadDMAAddress()), DMASectorWorkSpace, NUMBYTESPERSECTOR );

  /* Update DMA pointer */
  FDC_WriteDMAAddress(FDC_ReadDMAAddress()+NUMBYTESPERSECTOR);
}


/*-----------------------------------------------------------------------*/
/*
  Write byte to 0xff8604
*/
void FDC_WriteDiscController(unsigned short v)
{
 DiscControllerWord_ff8604wr = v;
 FDC_WriteDiscControllerByte();
}


/*-----------------------------------------------------------------------*/
/*
  Read word from 0xff8604
*/
short FDC_ReadDiscControllerStatus(void)
{
 FDC_ReadDiscControllerStatusByte();    /* Read Status/Track/Sector/Data according to 'DiscControllerWord_ff8604wr' */
 return DiscControllerByte;
}


/*-----------------------------------------------------------------------*/
/*
  Write word to 0xff8606 (DMA Mode Control)

  Eg.
  $80 - Selects command/status register
  $82 - Selects track register
  $84 - Selects sector register
  $86 - Selects data regsiter
  NOTE - OR above values with $100 is transfer from memory to floppy
  Also if bit 4 is set, write to sector count register
*/
void FDC_WriteDMAModeControl(unsigned short v)
{
 DMAModeControl_ff8606wr_prev = DMAModeControl_ff8606wr;  /* Store previous to check for _read/_write toggle(DMA reset) */
 DMAModeControl_ff8606wr = v;          /* Store to DMA Mode control */
 FDC_CheckForDMAStatusReset();
}

