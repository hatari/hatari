/*
  Hatari - fdc.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Floppy Disk Controller(FDC) emulation. We need to simulate the movement of
  the head of the floppy disk drive to accurately perform the FDC commands,
  such as 'Step'. The is important for ST demo disk images. We have to go
  into a lot of details - including the start up/stop of the drive motor.
  To help with this emulation, we keep our own internal commands which are
  checked each HBL to perform the transfer of data from our disk image into
  the ST RAM area by simulating the DMA.
*/

/* FIXME : after a type II read sector with multi bit 'm' on, we	*/
/* should update FDCSectorRegister to be 'max sector for current	*/
/* track'+1 and set command status to 'Record Not Found'.		*/
/* Also,  if multi bit is set and sector count is less than number	*/
/* of sectors in the track, then the FDC reads the whole track		*/
/* anyway, setting RNF at the end, but the DMA stops transferring	*/
/* data once DMA sector count $ff8604 reaches 0.			*/
/* Timings for read sector with multi bit are not good and prevent	*/
/* some programs from working (eg Super Monaco GP on Superior 65)	*/
/* because intrq bit 5 in $fffa01 seems to be cleared too late, 	*/
/* the command takes more time than a real ST to complete.		*/

const char FDC_fileid[] = "Hatari fdc.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "fdc.h"
#include "hdc.h"
#include "floppy.h"
#include "ikbd.h"
#include "ioMem.h"
#include "log.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "psg.h"
#include "stMemory.h"
#include "screen.h"
#include "video.h"
#include "clocks_timings.h"


/*
  Floppy Disk Controller

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

ACSI DMA and Floppy Disk Controller(FDC)
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


  This handles any read/writes to the FDC and PSG. FDC commands are then sent
  through to read/write disk sector code. We have full documents on 1772 FDC,
  but they use r0,r1,r2,r3 etc.. which are not seen at first glance as Atari
  access them via the A0,A1 bits. Once this is understood it is all relativly
  easy as a lot of information can be ignored as we are using disk images and
  not actual disks. We do NOT support the reading of the PC's A: drive - newer
  PC's cannot read an ST single sided disk, ST disks are very old and so are
  dirty which gets onto the PC drive heads and ruins them and also support for
  disk sector access under the various modern operating systems is not so easy
  (if possible at all).

  According to the documentation INTRQ is generated at the completion of each
  command (causes an interrupt in the MFP). INTRQ is reset by reading the status
  register OR by loading a new command. So, does this mean the GPIP? Or does it
  actually CANCEL the interrupt? Can this be done?
*/

/*-----------------------------------------------------------------------*/

#define	FDC_STR_BIT_BUSY			0x01
#define	FDC_STR_BIT_DRQ_INDEX			0x02
#define	FDC_STR_BIT_LOST_DATA_TR00		0x04
#define	FDC_STR_BIT_CRC_ERROR			0x08
#define	FDC_STR_BIT_RNF				0x10
#define	FDC_STR_BIT_SPIN_UP_RECORD_TYPE		0x20
#define	FDC_STR_BIT_WPRT			0x40
#define	FDC_STR_BIT_MOTOR_ON			0x80


#define	FDC_COMMAND_BIT_VERIFY			(1<<2)		/* 0=verify after type I, 1=no verify after type I */
#define	FDC_COMMAND_BIT_MOTOR_ON		(1<<3)		/* 0=enable motor test, 1=disable motor test */
#define	FDC_COMMAND_BIT_UPDATE_TRACK		(1<<4)		/* 0=don't update TR after type I, 1=update TR after type I */
#define	FDC_COMMAND_BIT_MULTIPLE_SECTOR		(1<<4)		/* 0=read/write only 1 sector, 1=read/write many sectors */



/* FDC Emulation commands */
enum
{
	FDCEMU_CMD_NULL = 0,
	/* Type I */
	FDCEMU_CMD_RESTORE,
	FDCEMU_CMD_SEEK,
	FDCEMU_CMD_STEP,
	FDCEMU_CMD_STEPIN,
	FDCEMU_CMD_STEPOUT,
	/* Type II */
	FDCEMU_CMD_READSECTORS,
	FDCEMU_CMD_READMULTIPLESECTORS,
	FDCEMU_CMD_WRITESECTORS,
	FDCEMU_CMD_WRITEMULTIPLESECTORS,
	/* Type III */
	FDCEMU_CMD_READADDRESS,

	/* Other states */
	FDCEMU_CMD_MOTOR_STOP
};

/* FDC Emulation commands */
#define  FDCEMU_RUN_NULL      0

/* FDC Running Restore commands */
enum
{
	FDCEMU_RUN_RESTORE_SEEKTOTRACKZERO,
	FDCEMU_RUN_RESTORE_COMPLETE
};

/* FDC Running Seek commands */
enum
{
	FDCEMU_RUN_SEEK_TOTRACK,
	FDCEMU_RUN_SEEK_COMPLETE
};

/* FDC Running Step commands */
enum
{
	FDCEMU_RUN_STEP_ONCE,
	FDCEMU_RUN_STEP_COMPLETE
};

/* FDC Running Step In commands */
enum
{
	FDCEMU_RUN_STEPIN_ONCE,
	FDCEMU_RUN_STEPIN_COMPLETE
};

/* FDC Running Step Out commands */
enum
{
	FDCEMU_RUN_STEPOUT_ONCE,
	FDCEMU_RUN_STEPOUT_COMPLETE
};

/* FDC Running Read Sector/s commands */
enum
{
	FDCEMU_RUN_READSECTORS_READDATA,
	FDCEMU_RUN_READSECTORS_COMPLETE
};

/* FDC Running write Sector/s commands */
enum
{
	FDCEMU_RUN_WRITESECTORS_WRITEDATA,
	FDCEMU_RUN_WRITESECTORS_COMPLETE
};

/* FDC Running Read Address commands */
enum
{
	FDCEMU_RUN_READADDRESS,
	FDCEMU_RUN_READADDRESS_COMPLETE
};


/* Commands are taking the equivalent of FDC_DELAY_CYCLES cpu cycles to execute */
/* to try to simulate the speed of a real ST floppy drive */
#define FDC_DELAY_CYCLES		92160
//#define FDC_DELAY_CYCLES		1536	// 'Just Bugging Demo' by ACF requires a very fast delay (bug in the loader)

/* Standard hardware values for the FDC. This should allow to get good timings estimation */
/* when dealing with non protected disks that require a correct speed (MSA or ST images) */
/* FIXME : Those timings should be improved by taking into account the 16 bytes DMA fifo and the time */
/* it takes to reach the track/sector/address before really reading it */

#define	FDC_BITRATE_STANDARD		250000			/* read/write speed in bits per sec */
#define	FDC_RPM_STANDARD		300			/* 300 RPM or 5 spins per sec */
#define	FDC_TRACK_BYTES_STANDARD	( ( FDC_BITRATE_STANDARD / 8 ) / ( FDC_RPM_STANDARD / 60 ) )	/* 6250 bytes */

#define FDC_TRANSFER_BYTES_US( n )	(  n * 8 * 1000000.L / FDC_BITRATE_STANDARD )	/* micro sec to read/write 'n' bytes */

/* Delays are in micro sec */
#define	FDC_DELAY_MOTOR_ON			( 1000000.L * 6 / ( FDC_RPM_STANDARD / 60 ) )	/* 6 spins to reach correct speed */
#define	FDC_DELAY_MOTOR_OFF			( 1000000.L * 2 )	/* Turn off motor 2 sec after the last command */


#define	FDC_DELAY_TYPE_I_PREPARE		100		/* Types I commands take at least 0.1 ms to execute */
								/* (~800 cpu cycles @ 8 Mhz). FIXME : this was not measured, it's */
								/* to avoid returning immediatly when command has no effect */
#define	FDC_DELAY_TYPE_II_PREPARE		1		/* Start Type II commands immediatly */
#define	FDC_DELAY_TYPE_III_PREPARE		1		/* Start Type III commands immediatly */
#define	FDC_DELAY_TYPE_IV_PREPARE		100		/* FIXME : this was not measured */
								
#define	FDC_DELAY_READ_SECTOR_STANDARD		FDC_TRANSFER_BYTES_US( NUMBYTESPERSECTOR )	/* 512 bytes per sector */
#define	FDC_DELAY_WRITE_SECTOR_STANDARD		FDC_TRANSFER_BYTES_US( NUMBYTESPERSECTOR )	/* 512 bytes per sector */

#define	FDC_DELAY_READ_ADDR_STANDARD		FDC_TRANSFER_BYTES_US( 6 )
#define	FDC_DELAY_READ_TRACK_STANDARD		FDC_TRANSFER_BYTES_US( FDC_TRACK_BYTES_STANDARD )
#define	FDC_DELAY_WRITE_TRACK_STANDARD		FDC_TRANSFER_BYTES_US( FDC_TRACK_BYTES_STANDARD )

#define	FDC_DELAY_COMMAND_COMPLETE		1		/* Number of us before going to the _COMPLETE state (~8 cpu cycles) */


static int FDC_StepRate_ms[] = { 2 , 3 , 5 , 6 };		/* controlled by bits 1 and 0 (r1/r0) in type I commands */


Sint16 FDCSectorCountRegister;

Uint16 DiskControllerWord_ff8604wr;                             /* 0xff8604 (write) */
static Uint16 DiskControllerStatus_ff8604rd;                    /* 0xff8604 (read) */

Uint16 DMAModeControl_ff8606wr;                                 /* 0xff8606 (write) */
static Uint16 DMAStatus_ff8606rd;                               /* 0xff8606 (read) */

static Uint16 FDCCommandRegister;
static Sint16 FDCTrackRegister, FDCSectorRegister, FDCDataRegister;
static int FDCEmulationCommand;                                 /* FDC emulation command currently being exceuted */
static int FDCEmulationRunning;                                 /* Running command under above */
static int FDCStepDirection;                                    /* +Track on 'Step' command */
static bool bDMAWaiting;                                        /* Is DMA waiting to copy? */
static int bMotorOn;                                            /* Is motor on? */ // FIXME NP use SR instead
static int MotorSlowingCount;                                   /* Counter used to slow motor before stopping */	/* FIXME NP remove */
static int FDC_StepRate;					/* Value of bits 0 and 1 for current Type I command */

static short int nReadWriteTrack;                               /* Parameters used in sector read/writes */
static short int nReadWriteSector;
static short int nReadWriteSide;
static short int nReadWriteDev;
static unsigned short int nReadWriteSectorsPerTrack;
static short int nReadWriteSectors;

static Uint8 DMASectorWorkSpace[NUMBYTESPERSECTOR];             /* Workspace used to copy to/from for floppy DMA */


static int FDC_DelayToCpuCycles ( int Delay_micro );

static void FDC_ResetDMAStatus(void);
static int  FDC_FindFloppyDrive(void);
static int FDC_UpdateMotorStop(void);
static int FDC_UpdateRestoreCmd(void);
static int FDC_UpdateSeekCmd(void);
static int FDC_UpdateStepCmd(void);
static int FDC_UpdateStepInCmd(void);
static int FDC_UpdateStepOutCmd(void);
static int FDC_UpdateReadSectorsCmd(void);
static int FDC_UpdateWriteSectorsCmd(void);
static int FDC_UpdateReadAddressCmd(void);
static bool FDC_ReadSectorFromFloppy(void);
static bool FDC_WriteSectorFromFloppy(void);

/*-----------------------------------------------------------------------*/
/**
 * Reset variables used in FDC
 */
void FDC_Reset(void)
{
	/* Clear out FDC registers */
	DiskControllerStatus_ff8604rd = 0;
	DiskControllerWord_ff8604wr = 0;
	DMAStatus_ff8606rd = 0x01;
	DMAModeControl_ff8606wr = 0;
	FDC_ResetDMAStatus();
	FDCCommandRegister = 0;
	FDCTrackRegister = 0;
	FDCSectorRegister = 1;
	FDCDataRegister = 0;
	FDCSectorCountRegister = 0;

	FDCEmulationCommand = FDCEMU_CMD_NULL;        /* FDC emulation command currently being exceuted */
	FDCEmulationRunning = FDCEMU_RUN_NULL;        /* Running command under above */

	FDCStepDirection = 1;                         /* +Track on 'Step' command */
	bDMAWaiting = false;                          /* No DMA waiting */
	bMotorOn = false;                             /* Motor off */
}


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
 */
void FDC_MemorySnapShot_Capture(bool bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(&DiskControllerStatus_ff8604rd, sizeof(DiskControllerStatus_ff8604rd));
	MemorySnapShot_Store(&DiskControllerWord_ff8604wr, sizeof(DiskControllerWord_ff8604wr));
	MemorySnapShot_Store(&DMAStatus_ff8606rd, sizeof(DMAStatus_ff8606rd));
	MemorySnapShot_Store(&DMAModeControl_ff8606wr, sizeof(DMAModeControl_ff8606wr));
	MemorySnapShot_Store(&FDCCommandRegister, sizeof(FDCCommandRegister));
	MemorySnapShot_Store(&FDCTrackRegister, sizeof(FDCTrackRegister));
	MemorySnapShot_Store(&FDCSectorRegister, sizeof(FDCSectorRegister));
	MemorySnapShot_Store(&FDCDataRegister, sizeof(FDCDataRegister));
	MemorySnapShot_Store(&FDCSectorCountRegister, sizeof(FDCSectorCountRegister));
	MemorySnapShot_Store(&FDCEmulationCommand, sizeof(FDCEmulationCommand));
	MemorySnapShot_Store(&FDCEmulationRunning, sizeof(FDCEmulationRunning));
	MemorySnapShot_Store(&FDCStepDirection, sizeof(FDCStepDirection));
	MemorySnapShot_Store(&bDMAWaiting, sizeof(bDMAWaiting));
	MemorySnapShot_Store(&bMotorOn, sizeof(bMotorOn));
	MemorySnapShot_Store(&MotorSlowingCount, sizeof(MotorSlowingCount));
	MemorySnapShot_Store(&nReadWriteTrack, sizeof(nReadWriteTrack));
	MemorySnapShot_Store(&nReadWriteSector, sizeof(nReadWriteSector));
	MemorySnapShot_Store(&nReadWriteSide, sizeof(nReadWriteSide));
	MemorySnapShot_Store(&nReadWriteDev, sizeof(nReadWriteDev));
	MemorySnapShot_Store(&nReadWriteSectorsPerTrack, sizeof(nReadWriteSectorsPerTrack));
	MemorySnapShot_Store(&nReadWriteSectors, sizeof(nReadWriteSectors));
	MemorySnapShot_Store(DMASectorWorkSpace, sizeof(DMASectorWorkSpace));
}


/*-----------------------------------------------------------------------*/
/**
 * Convert a delay in micro seconds to its equivalent of cpu cycles
 * (FIXME : for now we use a fixed 8 MHz clock, because cycInt.c requires it)
 */
static int FDC_DelayToCpuCycles ( int Delay_micro )
{
  fprintf ( stderr , "fdc state %d delay %d us %d cycles\n" , FDCEmulationCommand , Delay_micro , (int) ( ( (Sint64)MachineClocks.FDC_Freq * Delay_micro ) / 1000000 ) & -4 );
	return (int) ( ( (Sint64)MachineClocks.FDC_Freq * Delay_micro ) / 1000000 ) & -4;
}





/*-----------------------------------------------------------------------*/
/**
 * Reset DMA Status (RD 0xff8606)
 *
 * This is done by 'toggling' bit 8 of the DMA Mode Control register
 */
static void FDC_ResetDMAStatus(void)
{
	DMAStatus_ff8606rd = 0;           /* Clear out */

	FDCSectorCountRegister = 0;
	FDC_SetDMAStatus(false);          /* Set no error */

	/* Reset HDC command status */
	HDCSectorCount = 0;
	/*HDCCommand.byteCount = 0;*/  /* Not done on real ST? */
	HDCCommand.returnCode = 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Set DMA Status (RD 0xff8606)
 *
 * NOTE FDC Doc's are incorrect - Bit 0 is '0' on error (See TOS floprd, Ninja III etc...)
 * Look like Atari(yet again) connected the hardware up differently to the spec'
 *
 * Bit 0 - _Error Status (0=Error)
 * Bit 1 - _Sector Count Zero Status (0=Sector Count Zero)
 * Bit 2 - _Data Request Inactive Status
 */
void FDC_SetDMAStatus(bool bError)
{
	DMAStatus_ff8606rd &= 0x1;        /* Clear(except for error) */

	/* Set error condition - NOTE this is incorrect in the FDC Doc's! */
	if (!bError)
		DMAStatus_ff8606rd |= 0x1;

	/* Set zero sector count */

	if (DMAModeControl_ff8606wr&0x08)         /* Get which sector count? */
		DMAStatus_ff8606rd |= (HDCSectorCount)?0x2:0;         /* HDC */
	else
		DMAStatus_ff8606rd |= (FDCSectorCountRegister)?0x2:0; /* FDC */
	/* Perhaps the DRQ should be set here */
}


/*-----------------------------------------------------------------------*/
/**
 * Read DMA Status (RD 0xff8606)
 */
void FDC_DmaStatus_ReadWord(void)
{
	if (nIoMemAccessSize == SIZE_BYTE)
	{
		/* This register does not like to be accessed in byte mode on a normal ST */
		M68000_BusError(IoAccessBaseAddress, BUS_ERROR_READ);
		return;
	}

	IoMem_WriteWord(0xff8606, DMAStatus_ff8606rd);
}


/*-----------------------------------------------------------------------*/
/**
 * Copy data from DMA workspace into ST RAM
 */
static void FDC_DMADataFromFloppy(void)
{
	Uint32 Address = FDC_ReadDMAAddress();
	STMemory_SafeCopy(Address, DMASectorWorkSpace, NUMBYTESPERSECTOR, "FDC DMA data read");
	/* Update DMA pointer */
	FDC_WriteDMAAddress(Address+NUMBYTESPERSECTOR);
}


/*-----------------------------------------------------------------------*/
/**
 *
 */
static void FDC_UpdateDiskDrive(void)
{
	/* Set details for current selecte drive */
	nReadWriteDev = FDC_FindFloppyDrive();

	if (EmulationDrives[nReadWriteDev].bDiskInserted)
		Floppy_FindDiskDetails(EmulationDrives[nReadWriteDev].pBuffer,EmulationDrives[nReadWriteDev].nImageBytes,&nReadWriteSectorsPerTrack,NULL);
}


/*-----------------------------------------------------------------------*/
/**
 * Set disk controller status (RD 0xff8604)
 */
static void FDC_SetDiskControllerStatus(void)
{
	/* Update disk */
	FDC_UpdateDiskDrive();

#if 1		/* rewrite block */
	/* Clear out to default */
	//DiskControllerStatus_ff8604rd = 0;
	DiskControllerStatus_ff8604rd &= FDC_STR_BIT_MOTOR_ON;

	/* ONLY do this if we are running a Type I command */
	if ((FDCCommandRegister&0x80)==0)
	{
		/* Type I - Restore, Seek, Step, Step-In, Step-Out */
		if (FDCTrackRegister==0)
			DiskControllerStatus_ff8604rd |= 0x4;    /* Bit 2 - Track Zero, '0' if head is NOT at zero */
	}

	/* If no disk inserted, tag as error */
	if (!EmulationDrives[nReadWriteDev].bDiskInserted)
		DiskControllerStatus_ff8604rd |= 0x10;     /* RNF - Record not found, ie no disk in drive */
#endif
}


static void FDC_Update_STR ( Uint8 DisableBits , Uint8 EnableBits )
{
	DiskControllerStatus_ff8604rd &= (~DisableBits);		/* clear bits in DisableBits */
	DiskControllerStatus_ff8604rd |= EnableBits;			/* set bits in EnableBits */
}


/*-----------------------------------------------------------------------*/
/**
 * Return device for FDC, check PORTA bits 1,2(0=on,1=off)
 */
static int FDC_FindFloppyDrive(void)
{
	/* Check Drive A first */
	if ((PSGRegisters[PSG_REG_IO_PORTA]&0x2)==0)
		return 0;                     /* Device 0 (A:) */
	/* If off, check Drive B */
	if ((PSGRegisters[PSG_REG_IO_PORTA]&0x4)==0)
		return 1;                     /* Device 1 (B:) */

	/* None appear to be selected so default to Drive A */
	return 0;                         /* Device 0 (A:) */
}


/*-----------------------------------------------------------------------*/
/**
 * Acknowledge FDC interrupt
 */
void FDC_AcknowledgeInterrupt(void)
{
	/* Acknowledge in MFP circuit, pass bit, enable, pending */
	MFP_InputOnChannel(MFP_FDCHDC_BIT,MFP_IERB,&MFP_IPRB);
	MFP_GPIP &= ~0x20;
}


/*-----------------------------------------------------------------------*/
/**
 * Copy parameters for disk sector/s read/write
 */
static void FDC_SetReadWriteParameters(int nSectors)
{
	/* Copy read/write details so we can modify them */
	nReadWriteTrack = FDCTrackRegister;
	nReadWriteSector = FDCSectorRegister;
	nReadWriteSide = (~PSGRegisters[PSG_REG_IO_PORTA]) & 0x01;
	nReadWriteSectors = nSectors;
	/* Update disk */
	FDC_UpdateDiskDrive();
}


/*-----------------------------------------------------------------------*/
/**
 * ST program (or TOS) has read the MFP GPIP register to check if the FDC
 * is already done. Then we can skip the usual FDC waiting period!
 */
void FDC_GpipRead(void)
{
	static int nLastGpipBit;

	if ((MFP_GPIP & 0x20) == nLastGpipBit)
	{
#if 0
		if (!ConfigureParams.DiskImage.bSlowFloppy)
		{
			/* Restart FDC update interrupt to occur right after a few cycles */
			CycInt_RemovePendingInterrupt(INTERRUPT_FDC);
			CycInt_AddRelativeInterrupt(4, INT_CPU_CYCLE, INTERRUPT_FDC);
		}
#endif
	}
	else
	{
		nLastGpipBit = MFP_GPIP & 0x20;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Update floppy drive after a while.
 * Some games/demos (e.g. Fantasia by Dune, Alien World, ...) don't work
 * if the FDC is too fast. So we use the update "interrupt" for delayed
 * execution of the commands.
 * FIXME: We urgently need better timings here!
 */
void FDC_InterruptHandler_Update(void)
{
	int	Delay_micro = 0;

	CycInt_AcknowledgeInterrupt();

	/* Do we have a DMA ready to copy? */
	if (bDMAWaiting)
	{
		/* Yes, copy it */
		FDC_DMADataFromFloppy();
		/* Signal done */
		bDMAWaiting = false;
	}

	/* Is FDC active? */
	if (FDCEmulationCommand!=FDCEMU_CMD_NULL)
	{
		/* Which command are we running? */
		switch(FDCEmulationCommand)
		{
		 case FDCEMU_CMD_RESTORE:
			Delay_micro = FDC_UpdateRestoreCmd();
			break;
		 case FDCEMU_CMD_SEEK:
			Delay_micro = FDC_UpdateSeekCmd();
			break;
		 case FDCEMU_CMD_STEP:
			Delay_micro = FDC_UpdateStepCmd();
			break;
		 case FDCEMU_CMD_STEPIN:
			Delay_micro = FDC_UpdateStepInCmd();
			break;
		 case FDCEMU_CMD_STEPOUT:
			Delay_micro = FDC_UpdateStepOutCmd();
			break;

		 case FDCEMU_CMD_READSECTORS:
		 case FDCEMU_CMD_READMULTIPLESECTORS:
			Delay_micro = FDC_UpdateReadSectorsCmd();
			break;
		 case FDCEMU_CMD_WRITESECTORS:
		 case FDCEMU_CMD_WRITEMULTIPLESECTORS:
			Delay_micro = FDC_UpdateWriteSectorsCmd();
			break;

		 case FDCEMU_CMD_READADDRESS:
			Delay_micro = FDC_UpdateReadAddressCmd();
			break;

		 case FDCEMU_CMD_MOTOR_STOP:
			Delay_micro = FDC_UpdateMotorStop();
			break;
		}

		/* Set disk controller status (RD 0xff8604) */
		FDC_SetDiskControllerStatus();
	}

	if (FDCEmulationCommand != FDCEMU_CMD_NULL)
	{
		CycInt_AddAbsoluteInterrupt ( FDC_DelayToCpuCycles ( Delay_micro ) , INT_CPU_CYCLE , INTERRUPT_FDC );
	}
}



/*-----------------------------------------------------------------------*/
/**
 * Common to all commands once they're completed :
 * - remove busy bit
 * - stop motor after 2 sec
 */

static int FDC_CmdCompleteCommon(void)
{
  FDC_Update_STR ( FDC_STR_BIT_BUSY , 0 );			/* remove busy bit */

  FDCEmulationCommand = FDCEMU_CMD_MOTOR_STOP;			/* next state */
  return FDC_DELAY_MOTOR_OFF;
}


static int FDC_UpdateMotorStop(void)
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
	LOG_TRACE(TRACE_FDC, "fdc motor stopped VBL=%d video_cyc=%d %d@%d pc=%x\n",
		nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	FDC_Update_STR ( FDC_STR_BIT_MOTOR_ON , 0 );		/* unset motor bit */
bMotorOn = false;         /* Motor finally stopped */

	FDCEmulationCommand = FDCEMU_CMD_NULL;			/* motor stopped, this is the last state */
	return 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Run 'RESTORE' command
 */
static int FDC_UpdateRestoreCmd(void)
{
	int	Delay_micro = 0;

	/* Which command is running? */
	switch (FDCEmulationRunning)
	{
	 case FDCEMU_RUN_RESTORE_SEEKTOTRACKZERO:
		/* Are we at track zero? */
		if (FDCTrackRegister>0)
		{
			FDCTrackRegister--;             /* Move towards track zero */
			Delay_micro = FDC_StepRate_ms[ FDC_StepRate ] * 1000;
		}
		else
		{
			FDCTrackRegister = 0;           /* We're there */
			FDCEmulationRunning = FDCEMU_RUN_RESTORE_COMPLETE;
			Delay_micro = FDC_DELAY_COMMAND_COMPLETE;
		}
		break;
	 case FDCEMU_RUN_RESTORE_COMPLETE:
		/* Acknowledge interrupt, move along there's nothing more to see */
		FDC_AcknowledgeInterrupt();
		/* Set error */
		FDC_SetDMAStatus(false);          /* No DMA error */
		/* Done */
		Delay_micro = FDC_CmdCompleteCommon();
		break;
	}

	return Delay_micro;
}


/*-----------------------------------------------------------------------*/
/**
 * Run 'SEEK' command
 */
static int FDC_UpdateSeekCmd(void)
{
	int	Delay_micro = 0;

	/* Which command is running? */
	switch (FDCEmulationRunning)
	{
	 case FDCEMU_RUN_SEEK_TOTRACK:
//fprintf ( stderr , "seek %d\n" , FDCTrackRegister );
		/* Are we at the selected track? */
		if (FDCTrackRegister==FDCDataRegister)
		{
			FDCEmulationRunning = FDCEMU_RUN_SEEK_COMPLETE;
			Delay_micro = FDC_DELAY_COMMAND_COMPLETE;
		}
		else
		{
			/* No, seek towards track */
			if (FDCDataRegister<FDCTrackRegister)
				FDCTrackRegister--;
			else
				FDCTrackRegister++;
			Delay_micro = FDC_StepRate_ms[ FDC_StepRate ] * 1000;
		}
		break;
	 case FDCEMU_RUN_SEEK_COMPLETE:
//fprintf ( stderr , "seek complete %d\n" , FDCTrackRegister );
		/* Acknowledge interrupt, move along there's nothing more to see */
		FDC_AcknowledgeInterrupt();
		/* Set error */
		FDC_SetDMAStatus(false);          /* No DMA error */
		/* Done */
		Delay_micro = FDC_CmdCompleteCommon();
		break;
	}

	return Delay_micro;
}


/*-----------------------------------------------------------------------*/
/**
 * Run 'STEP' command
 */
static int FDC_UpdateStepCmd(void)
{
	int	Delay_micro = 0;

	/* Which command is running? */
	switch (FDCEmulationRunning)
	{
	 case FDCEMU_RUN_STEP_ONCE:
		/* Move head by one track in same direction as last step */
		FDCTrackRegister += FDCStepDirection;
		if (FDCTrackRegister<0)             /* Limit to stop */
		{
			FDCTrackRegister = 0;
			Delay_micro = FDC_DELAY_COMMAND_COMPLETE;	/* No delay if trying to go before track 0 */
		}
		else
			Delay_micro = FDC_StepRate_ms[ FDC_StepRate ] * 1000;

		FDCEmulationRunning = FDCEMU_RUN_STEP_COMPLETE;
		break;
	 case FDCEMU_RUN_STEP_COMPLETE:
		/* Acknowledge interrupt, move along there's nothing more to see */
		FDC_AcknowledgeInterrupt();
		/* Set error */
		FDC_SetDMAStatus(false);            /* No DMA error */
		/* Done */
		Delay_micro = FDC_CmdCompleteCommon();
		break;
	}

	return Delay_micro;
}


/*-----------------------------------------------------------------------*/
/**
 * Run 'STEP IN' command
 */
static int FDC_UpdateStepInCmd(void)
{
	int	Delay_micro = 0;

	/* Which command is running? */
	switch (FDCEmulationRunning)
	{
	 case FDCEMU_RUN_STEPIN_ONCE:
		FDCTrackRegister++;

		FDCEmulationRunning = FDCEMU_RUN_STEPIN_COMPLETE;
		Delay_micro = FDC_StepRate_ms[ FDC_StepRate ] * 1000;
		break;
	 case FDCEMU_RUN_STEPIN_COMPLETE:
		/* Acknowledge interrupt, move along there's nothing more to see */
		FDC_AcknowledgeInterrupt();
		/* Set error */
		FDC_SetDMAStatus(false);            /* No DMA error */
		/* Done */
		Delay_micro = FDC_CmdCompleteCommon();
		break;
	}

	return Delay_micro;
}


/*-----------------------------------------------------------------------*/
/**
 * Run 'STEP OUT' command
 */
static int FDC_UpdateStepOutCmd(void)
{
	int	Delay_micro = 0;

	/* Which command is running? */
	switch (FDCEmulationRunning)
	{
	 case FDCEMU_RUN_STEPOUT_ONCE:
		FDCTrackRegister--;
		if (FDCTrackRegister < 0)           /* Limit to stop */
		{
			FDCTrackRegister = 0;
			Delay_micro = FDC_DELAY_COMMAND_COMPLETE;	/* No delay if trying to go before track 0 */
		}
		else
			Delay_micro = FDC_StepRate_ms[ FDC_StepRate ] * 1000;

		FDCEmulationRunning = FDCEMU_RUN_STEPOUT_COMPLETE;
		break;
	 case FDCEMU_RUN_STEPOUT_COMPLETE:
		/* Acknowledge interrupt, move along there's nothing more to see */
		FDC_AcknowledgeInterrupt();
		/* Set error */
		FDC_SetDMAStatus(false);            /* No DMA error */
		/* Done */
		Delay_micro = FDC_CmdCompleteCommon();
		break;
	}

	return Delay_micro;
}


/*-----------------------------------------------------------------------*/
/**
 * Run 'READ SECTOR/S' command
 */
static int FDC_UpdateReadSectorsCmd(void)
{
	int	Delay_micro = 0;

	/* Which command is running? */
	switch (FDCEmulationRunning)
	{
	 case FDCEMU_RUN_READSECTORS_READDATA:
		/* Read in a sector */
		if (FDC_ReadSectorFromFloppy())         /* Read a single sector through DMA */
		{
			FDCSectorCountRegister--;           /* Decrement FDCSectorCount */
			if (FDCSectorCountRegister <= 0)
				FDCSectorCountRegister = 0;

			/* Have we finished? */
			nReadWriteSectors--;
			if (nReadWriteSectors<=0)
			{
				FDCEmulationRunning = FDCEMU_RUN_READSECTORS_COMPLETE;
				Delay_micro = FDC_DELAY_COMMAND_COMPLETE;
			}
			Delay_micro = FDC_DELAY_READ_SECTOR_STANDARD;

			bDMAWaiting = true;
		}
		else
		{
			/* Acknowledge interrupt, move along there's nothing more to see */
			FDC_AcknowledgeInterrupt();
			/* Set error */
			FDC_SetDMAStatus(true);             /* DMA error */
			/* Done */
			Delay_micro = FDC_CmdCompleteCommon();
		}
		break;
	 case FDCEMU_RUN_READSECTORS_COMPLETE:
		/* Acknowledge interrupt, move along there's nothing more to see */
		FDC_AcknowledgeInterrupt();
		/* Set error */
		FDC_SetDMAStatus(false);              /* No DMA error */
		/* Done */
		Delay_micro = FDC_CmdCompleteCommon();
		break;
	}

	return Delay_micro;
}


/*-----------------------------------------------------------------------*/
/**
 * Run 'WRITE SECTOR/S' command
 */
static int FDC_UpdateWriteSectorsCmd(void)
{
	int	Delay_micro = 0;

	/* Which command is running? */
	switch (FDCEmulationRunning)
	{
	 case FDCEMU_RUN_WRITESECTORS_WRITEDATA:
		/* Write out a sector */
		if (FDC_WriteSectorFromFloppy())        /* Write a single sector through DMA */
		{
			/* Decrement FDCsector count */
			FDCSectorCountRegister--;           /* Decrement FDCSectorCount */
			if (FDCSectorCountRegister<=0)
				FDCSectorCountRegister = 0;

			/* Have we finished? */
			nReadWriteSectors--;
			if (nReadWriteSectors<=0)
			{
				FDCEmulationRunning = FDCEMU_RUN_WRITESECTORS_COMPLETE;
				Delay_micro = FDC_DELAY_COMMAND_COMPLETE;
			}
			Delay_micro = FDC_DELAY_WRITE_SECTOR_STANDARD;

			/* Update DMA pointer */
			FDC_WriteDMAAddress(FDC_ReadDMAAddress()+NUMBYTESPERSECTOR);
		}
		else
		{
			/* Acknowledge interrupt, move along there's nothing more to see */
			FDC_AcknowledgeInterrupt();
			/* Set error */
			FDC_SetDMAStatus(true);             /* DMA error */
			/* Done */
			Delay_micro = FDC_CmdCompleteCommon();
		}
		break;
	 case FDCEMU_RUN_WRITESECTORS_COMPLETE:
		/* Acknowledge interrupt, move along there's nothing more to see */
		FDC_AcknowledgeInterrupt();
		/* Set error */
		FDC_SetDMAStatus(false);              /* No DMA error */
		/* Done */
		Delay_micro = FDC_CmdCompleteCommon();
		break;
	}

	return Delay_micro;
}


/*-----------------------------------------------------------------------*/
/**
 * Run 'READ ADDRESS' command
 */
static int FDC_UpdateReadAddressCmd(void)
{
	int	Delay_micro = 0;

	/* Which command is running? */
	switch (FDCEmulationRunning)
	{
	 case FDCEMU_RUN_READADDRESS:
		/* not implemented, just return with no error */
		FDCEmulationRunning = FDCEMU_RUN_READADDRESS_COMPLETE;
		Delay_micro = FDC_DELAY_READ_ADDR_STANDARD;
		break;
	 case FDCEMU_RUN_READADDRESS_COMPLETE:
		/* Acknowledge interrupt, move along there's nothing more to see */
		FDC_AcknowledgeInterrupt();
		/* Set error */
		FDC_SetDMAStatus(false);            /* No DMA error */
		/* Done */
		Delay_micro = FDC_CmdCompleteCommon();
		break;
	}

	return Delay_micro;
}



/**
 * Common to types I, II and III
 *
 * Start Motor if needed
 */

static int FDC_Check_MotorON ( Uint8 FDC_CR )
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

bMotorOn = true;                  /* Turn motor on */

	if ( ( ( FDC_CR & FDC_COMMAND_BIT_MOTOR_ON ) == 0 )				/* command wants motor on */
	  && ( ( DiskControllerStatus_ff8604rd & FDC_STR_BIT_MOTOR_ON ) == 0 ) )	/* motor on not enabled yet */
	{
		LOG_TRACE(TRACE_FDC, "fdc start motor VBL=%d video_cyc=%d %d@%d pc=%x\n",
			nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());
		FDC_Update_STR ( 0 , FDC_STR_BIT_MOTOR_ON );				/* set motor bit */
		return FDC_DELAY_MOTOR_ON;						/* motor's delay */
	}

	/* Other cases : set bit in STR and don't add delay */
	LOG_TRACE(TRACE_FDC, "fdc motor already on VBL=%d video_cyc=%d %d@%d pc=%x\n",
		nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());
	FDC_Update_STR ( 0 , FDC_STR_BIT_MOTOR_ON );
	return 0;
}

	
	
/*-----------------------------------------------------------------------*/
/**
 * Type I Commands
 *
 * Restore, Seek, Step, Step-In and Step-Out
 */


/*-----------------------------------------------------------------------*/
static int FDC_TypeI_Restore(void)
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type I restore VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to seek to track zero */
	FDCEmulationCommand = FDCEMU_CMD_RESTORE;
	FDCEmulationRunning = FDCEMU_RUN_RESTORE_SEEKTOTRACKZERO;

	FDC_Update_STR ( FDC_STR_BIT_DRQ_INDEX | FDC_STR_BIT_CRC_ERROR | FDC_STR_BIT_RNF , FDC_STR_BIT_BUSY );

	FDC_SetDiskControllerStatus();
	return FDC_DELAY_TYPE_I_PREPARE;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeI_Seek(void)
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type I seek track=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  FDCDataRegister, nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to seek to chosen track */
	FDCEmulationCommand = FDCEMU_CMD_SEEK;
	FDCEmulationRunning = FDCEMU_RUN_SEEK_TOTRACK;

	FDC_Update_STR ( FDC_STR_BIT_DRQ_INDEX | FDC_STR_BIT_CRC_ERROR | FDC_STR_BIT_RNF , FDC_STR_BIT_BUSY );

	FDC_SetDiskControllerStatus();
	return FDC_DELAY_TYPE_I_PREPARE;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeI_Step(void)
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type I step %d VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  FDCStepDirection, nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to step (same direction as last seek executed, eg 'FDCStepDirection') */
	FDCEmulationCommand = FDCEMU_CMD_STEP;
	FDCEmulationRunning = FDCEMU_RUN_STEP_ONCE;

	FDC_Update_STR ( FDC_STR_BIT_DRQ_INDEX | FDC_STR_BIT_CRC_ERROR | FDC_STR_BIT_RNF , FDC_STR_BIT_BUSY );

	FDC_SetDiskControllerStatus();
	return FDC_DELAY_TYPE_I_PREPARE;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeI_StepIn(void)
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type I step in VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to step in(Set 'FDCStepDirection') */
	FDCEmulationCommand = FDCEMU_CMD_STEPIN;
	FDCEmulationRunning = FDCEMU_RUN_STEPIN_ONCE;
	FDCStepDirection = 1;                 /* Increment track*/

	FDC_Update_STR ( FDC_STR_BIT_DRQ_INDEX | FDC_STR_BIT_CRC_ERROR | FDC_STR_BIT_RNF , FDC_STR_BIT_BUSY );

	FDC_SetDiskControllerStatus();
	return FDC_DELAY_TYPE_I_PREPARE;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeI_StepOut(void)
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type I step out VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to step out(Set 'FDCStepDirection') */
	FDCEmulationCommand = FDCEMU_CMD_STEPOUT;
	FDCEmulationRunning = FDCEMU_RUN_STEPOUT_ONCE;
	FDCStepDirection = -1;                /* Decrement track */

	FDC_Update_STR ( FDC_STR_BIT_DRQ_INDEX | FDC_STR_BIT_CRC_ERROR | FDC_STR_BIT_RNF , FDC_STR_BIT_BUSY );

	FDC_SetDiskControllerStatus();
	return FDC_DELAY_TYPE_I_PREPARE;
}


/*-----------------------------------------------------------------------*/
/**
 * Type II Commands
 *
 * Read Sector, Read Multiple Sectors, Write Sector, Write Multiple Sectors
 */


/*-----------------------------------------------------------------------*/
static int FDC_TypeII_ReadSector(void)
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type II read sector %d VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  FDCSectorRegister, nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to read a single sector */
	FDCEmulationCommand = FDCEMU_CMD_READSECTORS;
	FDCEmulationRunning = FDCEMU_RUN_READSECTORS_READDATA;
	/* Set reading parameters */
	FDC_SetReadWriteParameters(1);        /* Read in a single sector */

	FDC_Update_STR ( FDC_STR_BIT_DRQ_INDEX | FDC_STR_BIT_CRC_ERROR | FDC_STR_BIT_RNF , FDC_STR_BIT_BUSY );

	FDC_SetDiskControllerStatus();
	return FDC_DELAY_TYPE_II_PREPARE;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeII_ReadMultipleSectors(void)
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type II read multi sectors %d count %d VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  FDCSectorRegister, FDCSectorCountRegister, nVBLs,
		  FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to read sectors */
	FDCEmulationCommand = FDCEMU_CMD_READMULTIPLESECTORS;
	FDCEmulationRunning = FDCEMU_RUN_READSECTORS_READDATA;
	/* Set reading parameters */
	FDC_SetReadWriteParameters(FDCSectorCountRegister);   /* Read multiple sectors */

	FDC_SetDiskControllerStatus();
	return FDC_DELAY_TYPE_II_PREPARE;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeII_WriteSector(void)
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type II write sector %d VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  FDCSectorRegister, nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to write a single sector */
	FDCEmulationCommand = FDCEMU_CMD_WRITESECTORS;
	FDCEmulationRunning = FDCEMU_RUN_WRITESECTORS_WRITEDATA;
	/* Set writing parameters */
	FDC_SetReadWriteParameters(1);                        /* Write out a single sector */

	FDC_SetDiskControllerStatus();
	return FDC_DELAY_TYPE_II_PREPARE;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeII_WriteMultipleSectors(void)
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type II write multi sectors %d count %d VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  FDCSectorRegister, FDCSectorCountRegister, nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to write sectors */
	FDCEmulationCommand = FDCEMU_CMD_WRITEMULTIPLESECTORS;
	FDCEmulationRunning = FDCEMU_RUN_WRITESECTORS_WRITEDATA;
	/* Set witing parameters */
	FDC_SetReadWriteParameters(FDCSectorCountRegister);   /* Write multiple sectors */

	FDC_SetDiskControllerStatus();
	return FDC_DELAY_TYPE_II_PREPARE;
}


/*-----------------------------------------------------------------------*/
/**
 * Type III Commands
 *
 * Read Address, Read Track, Write Track
 */


/*-----------------------------------------------------------------------*/
static int FDC_TypeIII_ReadAddress(void)
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type III read address unimplemented VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	Log_Printf(LOG_TODO, "FDC type III command 'read address' is not implemented yet!\n");

	/* Set emulation to seek to track zero */
	FDCEmulationCommand = FDCEMU_CMD_READADDRESS;
	FDCEmulationRunning = FDCEMU_RUN_READADDRESS;

	FDC_SetDiskControllerStatus();
	return FDC_DELAY_TYPE_III_PREPARE;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeIII_ReadTrack(void)
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type III read track 0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  FDCTrackRegister, nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	Log_Printf(LOG_TODO, "FDC type III command 'read track' does not work yet!\n");

	/* FIXME: "Read track" should read more than only the sectors! (also sector headers, gaps, etc.) */

	/* Set emulation to read a single track */
	FDCEmulationCommand = FDCEMU_CMD_READSECTORS;
	FDCEmulationRunning = FDCEMU_RUN_READSECTORS_READDATA;
	/* Set reading parameters */
	FDC_SetReadWriteParameters(nReadWriteSectorsPerTrack);  /* Read whole track */

	FDC_SetDiskControllerStatus();
	return FDC_DELAY_TYPE_III_PREPARE;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeIII_WriteTrack(void)
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type III write track 0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  FDCTrackRegister, nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	Log_Printf(LOG_TODO, "FDC type III command 'write track' does not work yet!\n");

	/* FIXME: "Write track" not only writes the sectors! (also sector headers, gaps, etc.) */

	/* Set emulation to write a single track */
	FDCEmulationCommand = FDCEMU_CMD_WRITESECTORS;
	FDCEmulationRunning = FDCEMU_RUN_WRITESECTORS_WRITEDATA;
	/* Set writing parameters */
	FDC_SetReadWriteParameters(nReadWriteSectorsPerTrack);  /* Write whole track */

	FDC_SetDiskControllerStatus();
	return FDC_DELAY_TYPE_III_PREPARE;
}


/*-----------------------------------------------------------------------*/
/**
 * Type IV Commands
 *
 * Force Interrupt
 */


/*-----------------------------------------------------------------------*/
static int FDC_TypeIV_ForceInterrupt(bool bCauseCPUInterrupt)
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type IV force int VBL=%d video_cyc=%d %d@%dpc=%x\n",
		  nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Acknowledge interrupt, move along there's nothing more to see */
	if (bCauseCPUInterrupt)
		FDC_AcknowledgeInterrupt();

	/* Reset FDC */
	FDCEmulationCommand = FDCEMU_CMD_NULL;
	FDCEmulationRunning = FDCEMU_RUN_NULL;
	return FDC_DELAY_TYPE_IV_PREPARE;
}


/*-----------------------------------------------------------------------*/
/**
 * Execute Type I commands
 */
static int FDC_ExecuteTypeICommands(void)
{
	int	Delay_micro;

	MFP_GPIP |= 0x20;

	/* Check Type I Command */
	switch(FDCCommandRegister&0xf0)
	{
	 case 0x00:             /* Restore */
		Delay_micro = FDC_TypeI_Restore();
		break;
	 case 0x10:             /* Seek */
		Delay_micro = FDC_TypeI_Seek();
		break;
	 case 0x20:             /* Step */
	 case 0x30:
		Delay_micro = FDC_TypeI_Step();
		break;
	 case 0x40:             /* Step-In */
	 case 0x50:
		Delay_micro = FDC_TypeI_StepIn();
		break;
	 case 0x60:             /* Step-Out */
	 case 0x70:
		Delay_micro = FDC_TypeI_StepOut();
		break;
	}

	FDC_StepRate = FDCCommandRegister & 0x03;			/* keep bits 0 and 1 */

	/* Check if motor needs to be started and add possible delay */
	Delay_micro += FDC_Check_MotorON ( FDCCommandRegister );
	
	return Delay_micro;
}


/*-----------------------------------------------------------------------*/
/**
 * Execute Type II commands
 */
static int FDC_ExecuteTypeIICommands(void)
{
	int	Delay_micro;

	MFP_GPIP |= 0x20;

	/* Check Type II Command */
	switch(FDCCommandRegister&0xf0)
	{
	 case 0x80:             /* Read Sector */
		Delay_micro = FDC_TypeII_ReadSector();
		break;
	 case 0x90:             /* Read Sectors */
		Delay_micro = FDC_TypeII_ReadMultipleSectors();
		break;
	 case 0xa0:             /* Write Sector */
		Delay_micro = FDC_TypeII_WriteSector();
		break;
	 case 0xb0:             /* Write Sectors */
		Delay_micro = FDC_TypeII_WriteMultipleSectors();
		break;
	}

	/* Check if motor needs to be started and add possible delay */
	Delay_micro += FDC_Check_MotorON ( FDCCommandRegister );

	return Delay_micro;
}


/*-----------------------------------------------------------------------*/
/**
 * Execute Type III commands
 */
static int FDC_ExecuteTypeIIICommands(void)
{
	int	Delay_micro;

	MFP_GPIP |= 0x20;

	/* Check Type III Command */
	switch(FDCCommandRegister&0xf0)
	{
	 case 0xc0:             /* Read Address */
		Delay_micro = FDC_TypeIII_ReadAddress();
		break;
	 case 0xe0:             /* Read Track */
		Delay_micro = FDC_TypeIII_ReadTrack();
		break;
	 case 0xf0:             /* Write Track */
		Delay_micro = FDC_TypeIII_WriteTrack();
		break;
	}

	/* Check if motor need to be started and add possible delay */
	Delay_micro += FDC_Check_MotorON ( FDCCommandRegister );

	return Delay_micro;
}


/*-----------------------------------------------------------------------*/
/**
 * Execute Type IV commands
 */
static int FDC_ExecuteTypeIVCommands(void)
{
	int	Delay_micro;

	if (FDCCommandRegister!=0xD8)           /* Is an 'immediate interrupt command'? don't reset interrupt */
		MFP_GPIP |= 0x20;

	/* Check Type IV Command */
	if ((FDCCommandRegister&0x0c) == 0)     /* I3 and I2 are clear? If so we don't need a CPU interrupt */
		Delay_micro = FDC_TypeIV_ForceInterrupt(false);   /* Force Interrupt - no interrupt */
	else
		Delay_micro = FDC_TypeIV_ForceInterrupt(true);    /* Force Interrupt */

	return Delay_micro;
}


/*-----------------------------------------------------------------------*/
/**
 * Find FDC command type and execute
 */
static void FDC_ExecuteCommand(void)
{
	int	Delay_micro;

	/* Check type of command and execute */
	if ((FDCCommandRegister&0x80) == 0)           /* Type I - Restore,Seek,Step,Step-In,Step-Out */
		Delay_micro = FDC_ExecuteTypeICommands();
	else if ((FDCCommandRegister&0x40) == 0)      /* Type II - Read Sector, Write Sector */
		Delay_micro = FDC_ExecuteTypeIICommands();
	else if ((FDCCommandRegister&0xf0) != 0xd0)   /* Type III - Read Address, Read Track, Write Track */
		Delay_micro = FDC_ExecuteTypeIIICommands();
	else                                          /* Type IV - Force Interrupt */
		Delay_micro = FDC_ExecuteTypeIVCommands();

	CycInt_AddAbsoluteInterrupt ( FDC_DelayToCpuCycles ( Delay_micro ) , INT_CPU_CYCLE , INTERRUPT_FDC );
}


/*-----------------------------------------------------------------------*/
/**
 * Write to SectorCount register (WR 0xff8604)
 */
static void FDC_WriteSectorCountRegister(void)
{
	int FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write 8604 sector count=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  DiskControllerWord_ff8604wr, nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	FDCSectorCountRegister = DiskControllerWord_ff8604wr;
}


/*-----------------------------------------------------------------------*/
/**
 * Write to Command register (WR 0xff8604)
 */
static void FDC_WriteCommandRegister(void)
{
	int FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write 8604 command=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  DiskControllerWord_ff8604wr, nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	FDCCommandRegister = DiskControllerWord_ff8604wr;
	/* And execute */
	FDC_ExecuteCommand();
}


/*-----------------------------------------------------------------------*/
/**
 * Write to Track register (WR 0xff8604)
 */
static void FDC_WriteTrackRegister(void)
{
	int FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write 8604 track=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		DiskControllerWord_ff8604wr , nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	FDCTrackRegister = DiskControllerWord_ff8604wr & 0xff;	/* 0...79 */
}


/*-----------------------------------------------------------------------*/
/**
 * Write to Track register (WR 0xff8604)
 */
static void FDC_WriteSectorRegister(void)
{
	int FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write 8604 sector=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		DiskControllerWord_ff8604wr , nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	FDCSectorRegister = DiskControllerWord_ff8604wr & 0xff;	/* 1,2,3..... */
}


/*-----------------------------------------------------------------------*/
/**
 * Write to Data register (WR 0xff8604)
 */
static void FDC_WriteDataRegister(void)
{
	int FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write 8604 data=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		DiskControllerWord_ff8604wr , nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	FDCDataRegister = DiskControllerWord_ff8604wr & 0xff;
}


/*-----------------------------------------------------------------------*/
/**
 * Store byte in FDC registers, when write to 0xff8604
 */
void FDC_DiskController_WriteWord(void)
{
	int FrameCycles, HblCounterVideo, LineCycles;

	if (nIoMemAccessSize == SIZE_BYTE)
	{
		/* This register does not like to be accessed in byte mode on a normal ST */
		M68000_BusError(IoAccessBaseAddress, BUS_ERROR_WRITE);
		return;
	}

	M68000_WaitState(4);

	DiskControllerWord_ff8604wr = IoMem_ReadWord(0xff8604);

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write 8604 data=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		DiskControllerWord_ff8604wr , nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	/* Is it an ASCII HD command? */
	if ((DMAModeControl_ff8606wr & 0x0018) == 8)
	{
		/*  Handle HDC functions */
		HDC_WriteCommandPacket();
		return;
	}

	/* Are we trying to set the SectorCount? */
	if (DMAModeControl_ff8606wr&0x10)         /* Bit 4 */
		FDC_WriteSectorCountRegister();
	else
	{
		/* Write to FDC registers */
		switch(DMAModeControl_ff8606wr&0x6)
		{   /* Bits 1,2 (A1,A0) */
		 case 0x0:                            /* 0 0 - Command register */
			FDC_WriteCommandRegister();
			break;
		 case 0x2:                            /* 0 1 - Track register */
			FDC_WriteTrackRegister();
			break;
		 case 0x4:                            /* 1 0 - Sector register */
			FDC_WriteSectorRegister();
			break;
		 case 0x6:                            /* 1 1 - Data register */
			FDC_WriteDataRegister();
			break;
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Read Status/FDC registers, when read from 0xff8604
 * Return 'DiskControllerByte'
 */
void FDC_DiskControllerStatus_ReadWord(void)
{
	Sint16 DiskControllerByte = 0;            /* Used to pass back the parameter */
	int FrameCycles, HblCounterVideo, LineCycles;


	if (nIoMemAccessSize == SIZE_BYTE)
	{
		/* This register does not like to be accessed in byte mode on a normal ST */
		M68000_BusError(IoAccessBaseAddress, BUS_ERROR_READ);
		return;
	}

	M68000_WaitState(4);

	if ((DMAModeControl_ff8606wr & 0x18) == 0x08)     /* HDC status reg selected? */
	{
		/* return the HDC status reg */
		DiskControllerByte = HDCCommand.returnCode;
	}
	else if ((DMAModeControl_ff8606wr & 0x18) == 0x18)  /* HDC sector counter??? */
	{
		Log_Printf(LOG_DEBUG, "*** Read HDC sector counter???\n");
		DiskControllerByte = HDCSectorCount;
	}
	else
	{
		/* old FDC code */
		switch (DMAModeControl_ff8606wr&0x6)      /* Bits 1,2 (A1,A0) */
		{
		 case 0x0:                               /* 0 0 - Status register */
			DiskControllerByte = DiskControllerStatus_ff8604rd;
			if (bMotorOn)
				DiskControllerByte |= 0x80;

			if (Floppy_IsWriteProtected(nReadWriteDev))
				DiskControllerByte |= 0x40;

			if (EmulationDrives[nReadWriteDev].bMediaChanged)
			{
				/* Some games apparently poll the write-protection signal to check
				 * for disk image changes (the signal seems to change when you
				 * exchange disks on a real ST). We now also simulate this behaviour
				 * here, so that these games can continue with the other disk. */
				DiskControllerByte ^= 0x40;
				EmulationDrives[nReadWriteDev].bMediaChanged = false;
			}

			/* Reset FDC GPIP */
			MFP_GPIP |= 0x20;
			break;
		 case 0x2:                               /* 0 1 - Track register */
			DiskControllerByte = FDCTrackRegister;
			break;
		 case 0x4:                               /* 1 0 - Sector register */
			DiskControllerByte = FDCSectorRegister;
			break;
		 case 0x6:                               /* 1 1 - Data register */
			DiskControllerByte = FDCDataRegister;
			break;
		}
	}

	IoMem_WriteWord(0xff8604, DiskControllerByte);

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc read 8604 ctrl status=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		DiskControllerByte , nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );
}


/*-----------------------------------------------------------------------*/
/**
 * Read DMA address from ST's RAM (always up-to-date)
 */
Uint32 FDC_ReadDMAAddress(void)
{
	Uint32 Address;

	/* Build up 24-bit address from hardware registers */
	Address = ((Uint32)STMemory_ReadByte(0xff8609)<<16) | ((Uint32)STMemory_ReadByte(0xff860b)<<8) | (Uint32)STMemory_ReadByte(0xff860d);

	return Address;
}


/*-----------------------------------------------------------------------*/
/**
 * Write DMA address to ST's RAM(always keep up-to-date)
 */
void FDC_WriteDMAAddress(Uint32 Address)
{
	int FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write 0x%x to dma address VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		Address , nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	/* Store as 24-bit address */
	STMemory_WriteByte(0xff8609, Address>>16);
	STMemory_WriteByte(0xff860b, Address>>8);
	STMemory_WriteByte(0xff860d, Address);
}


/*-----------------------------------------------------------------------*/
/**
 * Read sector from floppy drive into workspace
 * We copy the bytes in chunks to simulate reading of the floppy using DMA
 */
static bool FDC_ReadSectorFromFloppy(void)
{
	int FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc read sector addr=0x%x dev=%d sect=%d track=%d side=%d VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		FDC_ReadDMAAddress(), nReadWriteDev, nReadWriteSector, nReadWriteTrack, nReadWriteSide,
		nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	/* Copy in 1 sector to our workspace */
	if (Floppy_ReadSectors(nReadWriteDev, DMASectorWorkSpace, nReadWriteSector, nReadWriteTrack, nReadWriteSide, 1, NULL))
	{
		/* Update reading/writing parameters */
		nReadWriteSector++;
		if (nReadWriteSector > nReadWriteSectorsPerTrack)   /* Advance into next track? */
		{
			nReadWriteSector = 1;
			nReadWriteTrack++;
		}
		return true;
	}

	/* Failed */
	LOG_TRACE(TRACE_FDC, "fdc read sector failed\n" );
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Write sector from workspace to floppy drive
 * We copy the bytes in chunks to simulate writing of the floppy using DMA
 */
static bool FDC_WriteSectorFromFloppy(void)
{
	Uint32 Address;
	int FrameCycles, HblCounterVideo, LineCycles;


	/* Get DMA address */
	Address = FDC_ReadDMAAddress();

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write sector addr=0x%x dev=%d sect=%d track=%d side=%d VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		Address, nReadWriteDev, nReadWriteSector, nReadWriteTrack, nReadWriteSide,
		nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	/* Write out 1 sector from our workspace */
	if (Floppy_WriteSectors(nReadWriteDev, &STRam[Address], nReadWriteSector, nReadWriteTrack, nReadWriteSide, 1, NULL))
	{
		/* Update reading/writing parameters */
		nReadWriteSector++;
		if (nReadWriteSector > nReadWriteSectorsPerTrack)   /* Advance to next track? */
		{
			nReadWriteSector = 1;
			nReadWriteTrack++;
		}
		return true;
	}

	/* Failed */
	LOG_TRACE(TRACE_FDC, "fdc write sector failed\n" );
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Write word to 0xff8606 (DMA Mode Control)
 *
 * Eg.
 * $80 - Selects command/status register
 * $82 - Selects track register
 * $84 - Selects sector register
 * $86 - Selects data regsiter
 * NOTE - OR above values with $100 is transfer from memory to floppy
 * Also if bit 4 is set, write to sector count register
 */
void FDC_DmaModeControl_WriteWord(void)
{
	Uint16 DMAModeControl_ff8606wr_prev;                     /* stores previous write to 0xff8606 for 'toggle' checks */
	int FrameCycles, HblCounterVideo, LineCycles;


	if (nIoMemAccessSize == SIZE_BYTE)
	{
		/* This register does not like to be accessed in byte mode on a normal ST */
		M68000_BusError(IoAccessBaseAddress, BUS_ERROR_WRITE);
		return;
	}

	DMAModeControl_ff8606wr_prev = DMAModeControl_ff8606wr;  /* Store previous to check for _read/_write toggle (DMA reset) */
	DMAModeControl_ff8606wr = IoMem_ReadWord(0xff8606);      /* Store to DMA Mode control */

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write 8606 ctrl=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		DMAModeControl_ff8606wr , nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	/* When write to 0xff8606, check bit '8' toggle. This causes DMA status reset */
	if ((DMAModeControl_ff8606wr_prev ^ DMAModeControl_ff8606wr) & 0x0100)
		FDC_ResetDMAStatus();
}


/*-----------------------------------------------------------------------*/
/**
 * Write to floppy mode/control (?) register (0xff860F).
 * Used on Falcon only!
 * FIXME: I've found hardly any documentation about this register, only
 * the following description of the bits:
 *
 *   __________54__10  Floppy Controll-Register
 *             ||  ||
 *             ||  |+- Prescaler 1
 *             ||  +-- Media detect 1
 *             |+----- Prescaler 2
 *             +------ Media detect 2
 *
 * For DD - disks:  0x00
 * For HD - disks:  0x03
 * for ED - disks:  0x30 (not supported by TOS)
 */
void FDC_FloppyMode_WriteByte(void)
{
	// printf("Write to floppy mode reg.: 0x%02x\n", IoMem_ReadByte(0xff860f));
}


/*-----------------------------------------------------------------------*/
/**
 * Read from floppy mode/control (?) register (0xff860F).
 * Used on Falcon only!
 * FIXME: I've found hardly any documentation about this register, only
 * the following description of the bits:
 *
 *   ________76543210  Floppy Controll-Register
 *           ||||||||
 *           |||||||+- Prescaler 1
 *           ||||||+-- Mode select 1
 *           |||||+--- Media detect 1
 *           ||||+---- accessed during DMA transfers (?)
 *           |||+----- Prescaler 2
 *           ||+------ Mode select 2
 *           |+------- Media detect 2
 *           +-------- Disk changed
 */
void FDC_FloppyMode_ReadByte(void)
{
	IoMem_WriteByte(0xff860f, 0x80);  // FIXME: Is this ok?
	// printf("Read from floppy mode reg.: 0x%02x\n", IoMem_ReadByte(0xff860f));
}
