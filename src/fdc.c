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

const char FDC_fileid[] = "Hatari fdc.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "fdc.h"
#include "hdc.h"
#include "floppy.h"
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
#include "utils.h"


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
    Word access only, but only lower byte (ff8605) is used
  (write) - Disk controller
    Set DMA sector count if ff8606 bit 4 == 1
    Set FDC's internal registers depending on bit 1/2 of ff8606 if bit 4 == 0
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
      NOTE : on ST, Lost Data is never set because the DMA always handles the data request signal.
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

  NOTE : The DMA is connected to the FDC and its Data Register, each time a DRQ
  is made by the FDC, it's handled by the DMA through its internal 16 bytes buffer.
  This means that in the case of the Atari ST the LOST_DATA bit will never be set
  in the Status Register (but data can be lost if FDC_DMA.SectorCount=0 as there
  will be no transfer between DMA and RAM)
*/

/*-----------------------------------------------------------------------*/

#define	FDC_STR_BIT_BUSY			0x01
#define	FDC_STR_BIT_INDEX			0x02		/* type I */
#define	FDC_STR_BIT_DRQ				0x02		/* type II and III */
#define	FDC_STR_BIT_TR00			0x04		/* type I */
#define	FDC_STR_BIT_LOST_DATA			0x04		/* type II and III */
#define	FDC_STR_BIT_CRC_ERROR			0x08
#define	FDC_STR_BIT_RNF				0x10
#define	FDC_STR_BIT_SPIN_UP			0x20		/* type I */
#define	FDC_STR_BIT_RECORD_TYPE			0x20		/* type II and III */
#define	FDC_STR_BIT_WPRT			0x40
#define	FDC_STR_BIT_MOTOR_ON			0x80


#define	FDC_COMMAND_BIT_VERIFY			(1<<2)		/* 0=verify after type I, 1=no verify after type I */
#define	FDC_COMMAND_BIT_HEAD_LOAD		(1<<2)		/* for type II/III 0=no extra delay, 1=add 30 ms delay to set the head */
#define	FDC_COMMAND_BIT_MOTOR_ON		(1<<3)		/* 0=enable motor test, 1=disable motor test */
#define	FDC_COMMAND_BIT_UPDATE_TRACK		(1<<4)		/* 0=don't update TR after type I, 1=update TR after type I */
#define	FDC_COMMAND_BIT_MULTIPLE_SECTOR		(1<<4)		/* 0=read/write only 1 sector, 1=read/write many sectors */



/* FDC Emulation commands used in FDC.Command */
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
	FDCEMU_CMD_READTRACK,
	FDCEMU_CMD_WRITETRACK,

	/* Other fake commands used internally */
	FDCEMU_CMD_MOTOR_STOP
};


/* FDC Emulation commands' sub-states used in FDC.CommandState */
enum
{
	FDCEMU_RUN_NULL = 0,

	/* Restore */
	FDCEMU_RUN_RESTORE_SEEKTOTRACKZERO,
	FDCEMU_RUN_RESTORE_COMPLETE,
	/* Seek */
	FDCEMU_RUN_SEEK_TOTRACK,
	FDCEMU_RUN_SEEK_COMPLETE,
	/* Step */
	FDCEMU_RUN_STEP_ONCE,
	FDCEMU_RUN_STEP_COMPLETE,
	/* Step In */
	FDCEMU_RUN_STEPIN_ONCE,
	FDCEMU_RUN_STEPIN_COMPLETE,
	/* Step Out */
	FDCEMU_RUN_STEPOUT_ONCE,
	FDCEMU_RUN_STEPOUT_COMPLETE,
	/* Read Sector */
	FDCEMU_RUN_READSECTORS_READDATA,
	FDCEMU_RUN_READSECTORS_READDATA_DMA,
	FDCEMU_RUN_READSECTORS_RNF,
	FDCEMU_RUN_READSECTORS_COMPLETE,
	/* Write Sector */
	FDCEMU_RUN_WRITESECTORS_WRITEDATA,
	FDCEMU_RUN_WRITESECTORS_WRITEDATA_DMA,
	FDCEMU_RUN_WRITESECTORS_RNF,
	FDCEMU_RUN_WRITESECTORS_COMPLETE,
	/* Read Address */
	FDCEMU_RUN_READADDRESS,
	FDCEMU_RUN_READADDRESS_DMA,
	FDCEMU_RUN_READADDRESS_COMPLETE,
	/* Read Track */
	FDCEMU_RUN_READTRACK,
	FDCEMU_RUN_READTRACK_DMA,
	FDCEMU_RUN_READTRACK_COMPLETE,
	/* Write Track */
	FDCEMU_RUN_WRITETRACK,
	FDCEMU_RUN_WRITETRACK_DMA,
	FDCEMU_RUN_WRITETRACK_COMPLETE
};



/* Standard hardware values for the FDC. This should allow to get good timings estimation */
/* when dealing with non protected disks that require a correct speed (MSA or ST images) */
/* FIXME : Those timings could be improved by taking into account the time */
/* it takes to reach the track index/sector/address field before really reading it, but this level */
/* of accuracy is not necessary for ST/MSA disk images (it would be required to emulate protections */
/* in Pasti disk images) */

#define	FDC_BITRATE_STANDARD			250000		/* read/write speed of the WD1772 in bits per sec */
#define	FDC_RPM_STANDARD			300		/* 300 RPM or 5 spins per sec */
#define	FDC_TRACK_BYTES_STANDARD		( ( FDC_BITRATE_STANDARD / 8 ) / ( FDC_RPM_STANDARD / 60 ) )	/* 6250 bytes */

#define FDC_TRANSFER_BYTES_US( n )		(  n * 8 * 1000000.L / FDC_BITRATE_STANDARD )	/* micro sec to read/write 'n' bytes in the WD1772 */

/* Delays are in micro sec */
#define	FDC_DELAY_MOTOR_ON			( 1000000.L * 6 / ( FDC_RPM_STANDARD / 60 ) )	/* 6 spins to reach correct speed */
#define	FDC_DELAY_MOTOR_OFF			( 1000000.L * 9 / ( FDC_RPM_STANDARD / 60 ) )	/* Turn off motor 9 spins after the last command */

#define	FDC_DELAY_HEAD_LOAD			( 30 * 1000 )	/* Additionnal 30 ms delay to load the head in type II/III */

#define	FDC_DELAY_RNF				( 1000000.L * 5 / ( FDC_RPM_STANDARD / 60 ) )	/* 5 spins to set RNF */

#define	FDC_DELAY_TYPE_I_PREPARE		100		/* Types I commands take at least 0.1 ms to execute */
								/* (~800 cpu cycles @ 8 Mhz). FIXME : this was not measured, it's */
								/* to avoid returning immediatly when command has no effect */
#define	FDC_DELAY_TYPE_II_PREPARE		1		/* Start Type II commands immediatly */
#define	FDC_DELAY_TYPE_III_PREPARE		1		/* Start Type III commands immediatly */
#define	FDC_DELAY_TYPE_IV_PREPARE		100		/* FIXME : this was not measured */
								
#define	FDC_DELAY_TRANSFER_DMA_16		FDC_TRANSFER_BYTES_US( DMA_DISK_TRANSFER_SIZE )

#define	FDC_DELAY_READ_ADDR_STANDARD		FDC_TRANSFER_BYTES_US( 6 )

#define	FDC_DELAY_COMMAND_COMPLETE		1		/* Number of us before going to the _COMPLETE state (~8 cpu cycles) */

#define	FDC_DELAY_COMMAND_IMMEDIATE		1		/* Number of us to go immediatly to another state */


#define	DMA_DISK_SECTOR_SIZE			512		/* Sector count at $ff8606 is for 512 bytes blocks */
#define	DMA_DISK_TRANSFER_SIZE			16		/* DMA transfers blocks of 16 bytes at a time */

#define	FDC_PHYSICAL_MAX_TRACK			90		/* Head can't go beyond 90 tracks */


#define FDC_SIDE				( (~PSGRegisters[PSG_REG_IO_PORTA]) & 0x01 )	/* Side 0 or 1 */
#define	FDC_DRIVE				FDC_FindFloppyDrive()

#define	FDC_STEP_RATE				( FDC.CR & 0x03 )	/* Keep bits 0 and 1 of the current type I command */

static int FDC_StepRate_ms[] = { 2 , 3 , 5 , 6 };		/* Controlled by bits 1 and 0 (r1/r0) in type I commands */


#define	FDC_SECTOR_SIZE_128			0		/* Sector size used in the ID fields */
#define	FDC_SECTOR_SIZE_256			1
#define	FDC_SECTOR_SIZE_512			2
#define	FDC_SECTOR_SIZE_1024			3


typedef struct {
	/* WD1772 internal registers */
	Uint8		DR;					/* Data Register */
	Uint8		TR;					/* Track Register */
	Uint8		SR;					/* Sector Register */
	Uint8		CR;					/* Command Register */
	Uint8		STR;					/* Status Register */
	int		StepDirection;				/* +1 (Step In) or -1 (Step Out) */

	/* Other variables */
	int		Command;				/* FDC emulation command currently being exceuted */
	int		CommandState;				/* Current state for the running command */
	Uint8		CommandType;				/* Type of latest FDC command (1,2,3 or 4) */

	Uint8		ID_FieldLastSector;			/* Sector number returned by Read Address (to simulate a spinning disk) */
} FDC_STRUCT;


typedef struct {
	/* DMA internal registers */
	Uint16		SectorCount;
	Uint16		BytesInSector;

	/* Variables to handle our DMA buffer */
	int		PosInBuffer;
	int		PosInBufferTransfer;
	int		BytesToTransfer;
} FDC_DMA_STRUCT;


static FDC_STRUCT	FDC;					/* All variables related to the WD1772 emulation */
static FDC_DMA_STRUCT	FDC_DMA;				/* All variables related to the DMA transfer */

static Uint8 HeadTrack[ MAX_FLOPPYDRIVES ];			/* A: and B: */

static Uint8 DMADiskWorkSpace[ FDC_TRACK_BYTES_STANDARD+1000 ];	/* Workspace used to transfer bytes between floppy and DMA */
								/* It should be large enough to contain a whole track */



static Uint16 DiskControllerStatus_ff8604rd;                    /* 0xff8604 (read) */

Uint16 DMAModeControl_ff8606wr;                                 /* 0xff8606 (write) */
static Uint16 DMAStatus_ff8606rd;                               /* 0xff8606 (read) */


static unsigned short int nReadWriteSectorsPerTrack;




/*--------------------------------------------------------------*/
/* Local functions prototypes					*/
/*--------------------------------------------------------------*/

static int FDC_DelayToCpuCycles ( int Delay_micro );
static void FDC_CRC16 ( Uint8 *buf , int nb , Uint16 *pCRC );

static void FDC_ResetDMAStatus(void);
static bool FDC_DMA_ReadFromFloppy ( void );
static bool FDC_DMA_WriteToFloppy ( void );

static int FDC_FindFloppyDrive(void);
static void FDC_Update_STR ( Uint8 DisableBits , Uint8 EnableBits );
static int FDC_UpdateMotorStop(void);
static int FDC_UpdateRestoreCmd(void);
static int FDC_UpdateSeekCmd(void);
static int FDC_UpdateStepCmd(void);
static int FDC_UpdateStepInCmd(void);
static int FDC_UpdateStepOutCmd(void);
static int FDC_UpdateReadSectorsCmd(void);
static int FDC_UpdateWriteSectorsCmd(void);
static int FDC_UpdateReadAddressCmd(void);
static int FDC_UpdateReadTrackCmd(void);
static bool FDC_ReadSectorFromFloppy ( Uint8 *buf , Uint8 Sector , int *pSectorSize );
static bool FDC_WriteSectorToFloppy ( int DMASectorsCount , Uint8 Sector , int *pSectorSize );

/*-----------------------------------------------------------------------*/
/**
 * Reset variables used in FDC
 */
void FDC_Reset(void)
{
	int	i;

	/* Clear out FDC registers */
	DiskControllerStatus_ff8604rd = 0;
	DMAStatus_ff8606rd = 0x01;
	DMAModeControl_ff8606wr = 0;

	FDC.CR = 0;
	FDC.TR = 0;
	FDC.SR = 1;
	FDC.DR = 0;
	FDC.StepDirection = 1;
	FDC.ID_FieldLastSector = 1;

	FDC.Command = FDCEMU_CMD_NULL;			/* FDC emulation command currently being executed */
	FDC.CommandState = FDCEMU_RUN_NULL;
	FDC.CommandType = 0;

	FDC_DMA.SectorCount = 0;
	FDC_ResetDMAStatus();

	for ( i=0 ; i<MAX_FLOPPYDRIVES ; i++ )
		HeadTrack[ i ] = 0;			/* Set all drives to track 0 */
}


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
 */
void FDC_MemorySnapShot_Capture(bool bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(&DiskControllerStatus_ff8604rd, sizeof(DiskControllerStatus_ff8604rd));
	MemorySnapShot_Store(&DMAStatus_ff8606rd, sizeof(DMAStatus_ff8606rd));
	MemorySnapShot_Store(&DMAModeControl_ff8606wr, sizeof(DMAModeControl_ff8606wr));
	MemorySnapShot_Store(&nReadWriteSectorsPerTrack, sizeof(nReadWriteSectorsPerTrack));



	MemorySnapShot_Store(&FDC, sizeof(FDC));
	MemorySnapShot_Store(&FDC_DMA, sizeof(FDC_DMA));
	MemorySnapShot_Store(HeadTrack, sizeof(HeadTrack));

	MemorySnapShot_Store(DMADiskWorkSpace, sizeof(DMADiskWorkSpace));
}


/*-----------------------------------------------------------------------*/
/**
 * Convert a delay in micro seconds to its equivalent of cpu cycles
 * (FIXME : for now we use a fixed 8 MHz clock, because cycInt.c requires it)
 */
static int FDC_DelayToCpuCycles ( int Delay_micro )
{
  fprintf ( stderr , "fdc state %d delay %d us %d cycles\n" , FDC.Command , Delay_micro , (int) ( ( (Sint64)MachineClocks.FDC_Freq * Delay_micro ) / 1000000 ) & -4 );
	return (int) ( ( (Sint64)MachineClocks.FDC_Freq * Delay_micro ) / 1000000 ) & -4;
}


/*-----------------------------------------------------------------------*/
/**
 * Compute the CRC16 of 'nb' bytes stored in 'buf'.
 */
static void FDC_CRC16 ( Uint8 *buf , int nb , Uint16 *pCRC )
{
	int	i;

	crc16_reset ( pCRC );
	for ( i=0 ; i<nb ; i++ )
	{
//		fprintf ( stderr , "fdc crc16 %d 0x%x\n" , i , buf[ i ] );
		crc16_add_byte ( pCRC , buf[ i ] );
	}
//	fprintf ( stderr , "fdc crc16 0x%x 0x%x\n" , *pCRC>>8 , *pCRC & 0xff );
}


/*-----------------------------------------------------------------------*/
/**
 * Reset DMA Status (RD 0xff8606)
 *
 * This is done by 'toggling' bit 8 of the DMA Mode Control register
 */
static void FDC_ResetDMAStatus(void)
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
	LOG_TRACE(TRACE_FDC, "fdc reset dma VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	DMAStatus_ff8606rd = 0;           /* Clear out */

	FDC_DMA.BytesInSector = DMA_DISK_SECTOR_SIZE;
	FDC_SetDMAStatus(false);          /* Set no error */

	FDC_DMA.PosInBuffer = 0;
	FDC_DMA.PosInBufferTransfer = 0;
	FDC_DMA.BytesToTransfer = 0;

	/* Reset HDC command status */
	HDCSectorCount = 0;			/* [NP] FIXME Don't reset hdc sector count ? */
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
		DMAStatus_ff8606rd |= (HDCSectorCount)?0x2:0;		/* HDC */
	else
		DMAStatus_ff8606rd |= (FDC_DMA.SectorCount)?0x2:0;	/* FDC */
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
 * Init some variables before starting a new DMA transfer.
 * We must store new data just after the most recent bytes that
 * were not yet transferred by the DMA (16 bytes buffer).
 * To avoid writing above the limit of DMADiskWorkSpace, we move
 * the current 16 bytes buffer at the start of DMADiskWorkSpace
 * if some bytes remain to be transferred, this way we never use
 * more than FDC_TRACK_BYTES_STANDARD in DMADiskWorkSpace.
 */

static void FDC_DMA_InitTransfer ( void )
{
	int	i;

	/* How many bytes remain in the current 16 bytes DMA buffer ? */
	if ( FDC_DMA.BytesToTransfer == 0 )				/* DMA buffer is empty */
	{
		FDC_DMA.PosInBuffer = 0;				/* Add new data at the start of DMADiskWorkSpace */
		FDC_DMA.PosInBufferTransfer = 0;
	}
	else								/* 16 bytes buffer partially filled */
	{
		for ( i=0 ; i<FDC_DMA.BytesToTransfer ; i++ )		/* Move these bytes at the start of the buffer */
			DMADiskWorkSpace[ i ] = DMADiskWorkSpace[ FDC_DMA.PosInBufferTransfer + i ];

		FDC_DMA.PosInBuffer = FDC_DMA.BytesToTransfer;		/* Add new data after the latest bytes stored in the 16 bytes buffer */
		FDC_DMA.PosInBufferTransfer = 0;
	}
}


/**
 * Transfer 16 bytes from the DMA workspace to the RAM.
 * Instead of handling a real 16 bytes buffer, this implementation moves
 * a 16 bytes window in DMADiskWorkSpace. The current position of this window
 * is stored in FDC_DMA.PosInBufferTransfer and contains the equivalent of the
 * DMA's internal 16 bytes buffer.
 *
 * Return true if there're no more bytes to transfer or false if some
 * bytes can still be tranfered by the DMA.
 *
 * NOTE : The DMA is connected to the FDC, each time a DRQ is made by the FDC,
 * it's handled by the DMA and stored in the DMA 16 bytes buffer. This means
 * FDC_STR_BIT_LOST_DATA will never be set (but data can be lost if FDC_DMA.SectorCount==0)
 */

static bool FDC_DMA_ReadFromFloppy ( void )
{
	Uint32	Address;
//fprintf ( stderr , "dma transfer read count=%d bytes=%d pos=%d\n" , FDC_DMA.SectorCount, FDC_DMA.BytesToTransfer, FDC_DMA.PosInBufferTransfer );

	if ( FDC_DMA.BytesToTransfer < DMA_DISK_TRANSFER_SIZE )
		return true;						/* There should be at least 16 bytes to start a DMA transfer */

	if ( FDC_DMA.SectorCount == 0 )
	{
		//FDC_Update_STR ( 0 , FDC_STR_BIT_LOST_DATA );		/* If DMA is OFF, data are lost -> Not on the ST */
		FDC_DMA.PosInBufferTransfer += DMA_DISK_TRANSFER_SIZE;
		FDC_DMA.BytesToTransfer -= DMA_DISK_TRANSFER_SIZE;
		return false;						/* FDC DMA is off but we still need to read all bytes from the floppy */
	}

	/* Transfer data and update DMA address */
	Address = FDC_ReadDMAAddress();
	STMemory_SafeCopy ( Address , DMADiskWorkSpace + FDC_DMA.PosInBufferTransfer , DMA_DISK_TRANSFER_SIZE , "FDC DMA data read" );
	FDC_DMA.PosInBufferTransfer += DMA_DISK_TRANSFER_SIZE;
	FDC_DMA.BytesToTransfer -= DMA_DISK_TRANSFER_SIZE;
	FDC_WriteDMAAddress ( Address + DMA_DISK_TRANSFER_SIZE );

	/* Update Sector Count */
	FDC_DMA.BytesInSector -= DMA_DISK_TRANSFER_SIZE;
	if ( FDC_DMA.BytesInSector <= 0 )
	{
		FDC_DMA.SectorCount--;
		FDC_DMA.BytesInSector = DMA_DISK_SECTOR_SIZE;
	}

	return false;							/* Transfer is not complete */
}


/**
 * Transfer 16 bytes from the RAM to disk using DMA.
 * This is used to write data to the disk with correct timings
 * by writing blocks of 16 bytes at a time.
 *
 * Return true if there're no more bytes to transfer or false if some
 * bytes can still be tranfered by the DMA.
 *
 * NOTE : in the case of the emulation in Hatari, the sector is first written
 * to the disk image and this function is just used to increment
 * DMA address at the correct pace to simulate that bytes are written from
 * blocks of 16 bytes handled by the DMA.
 */

static bool FDC_DMA_WriteToFloppy ( void )
{
	Uint32	Address;
//fprintf ( stderr , "dma transfer write count=%d bytes=%d pos=%d\n" , FDC_DMA.SectorCount, FDC_DMA.BytesToTransfer, FDC_DMA.PosInBufferTransfer );

	if ( FDC_DMA.BytesToTransfer < DMA_DISK_TRANSFER_SIZE )
		return true;						/* There should be at least 16 bytes to start a DMA transfer */

	if ( FDC_DMA.SectorCount == 0 )
	{
		//FDC_Update_STR ( 0 , FDC_STR_BIT_LOST_DATA );		/* If DMA is OFF, data are lost -> Not on the ST */
		FDC_DMA.PosInBufferTransfer += DMA_DISK_TRANSFER_SIZE;
		FDC_DMA.BytesToTransfer -= DMA_DISK_TRANSFER_SIZE;
		return false;						/* FDC DMA is off but we still need to process the whole sector */
	}

	/* Transfer data and update DMA address */
	Address = FDC_ReadDMAAddress();
	//STMemory_SafeCopy ( Address , DMADiskWorkSpace + FDC_DMA.PosInBufferTransfer , DMA_DISK_TRANSFER_SIZE , "FDC DMA data read" );
	FDC_DMA.PosInBufferTransfer += DMA_DISK_TRANSFER_SIZE;
	FDC_DMA.BytesToTransfer -= DMA_DISK_TRANSFER_SIZE;
	FDC_WriteDMAAddress ( Address + DMA_DISK_TRANSFER_SIZE );

	/* Update Sector Count */
	FDC_DMA.BytesInSector -= DMA_DISK_TRANSFER_SIZE;
	if ( FDC_DMA.BytesInSector <= 0 )
	{
		FDC_DMA.SectorCount--;
		FDC_DMA.BytesInSector = DMA_DISK_SECTOR_SIZE;
	}

	return false;							/* Transfer is not complete */

}


/*-----------------------------------------------------------------------*/
/**
 *
 */
static void FDC_UpdateDiskDrive(void)
{
	/* Set details for current selecte drive */

	if (EmulationDrives[ FDC_DRIVE ].bDiskInserted)
		Floppy_FindDiskDetails(EmulationDrives[ FDC_DRIVE ].pBuffer,EmulationDrives[ FDC_DRIVE ].nImageBytes,&nReadWriteSectorsPerTrack,NULL);
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

	/* If no disk inserted, tag as error */
	if (!EmulationDrives[ FDC_DRIVE ].bDiskInserted)
		DiskControllerStatus_ff8604rd |= 0x10;     /* RNF - Record not found, ie no disk in drive */
#endif
}


static void FDC_Update_STR ( Uint8 DisableBits , Uint8 EnableBits )
{
	DiskControllerStatus_ff8604rd &= (~DisableBits);		/* clear bits in DisableBits */
	DiskControllerStatus_ff8604rd |= EnableBits;			/* set bits in EnableBits */
fprintf ( stderr , "fdc str 0x%x\n" , DiskControllerStatus_ff8604rd );
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
static void FDC_SetReadWriteParameters(void)
{
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

	/* Is FDC active? */
	if (FDC.Command!=FDCEMU_CMD_NULL)
	{
		/* Which command are we running? */
		switch(FDC.Command)
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

		 case FDCEMU_CMD_READTRACK:
			Delay_micro = FDC_UpdateReadTrackCmd();
			break;

		 case FDCEMU_CMD_MOTOR_STOP:
			Delay_micro = FDC_UpdateMotorStop();
			break;
		}

		/* Set disk controller status (RD 0xff8604) */
		FDC_SetDiskControllerStatus();
	}

	if (FDC.Command != FDCEMU_CMD_NULL)
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
  FDC_Update_STR ( FDC_STR_BIT_BUSY , 0 );			/* Remove busy bit */

  FDC.Command = FDCEMU_CMD_MOTOR_STOP;				/* Fake command to stop the motor */
  return FDC_DELAY_MOTOR_OFF;
}



/**
 * Verify track after a type I command.
 * The FDC will read the first ID field of the current track and will
 * compare the track number in this ID field with the current Track Register.
 * If they don't match, an error is set with the RNF bit.
 * NOTE : in the case of Hatari when using ST/MSA images, the track is always the correct one,
 * so the verify will always be good.
 * This function could be improved to support other images format where logical track
 * could be different from physical track (eg Pasti)
 */

static void FDC_VerifyTrack(void)
{
  /* In the case of Hatari when using ST/MSA images, the track is always the correct one */
  FDC_Update_STR ( FDC_STR_BIT_RNF , 0 );			/* remove RNF bit */
}



/**
 * When the motor really stops (2 secs after the last command), clear all related bits in SR
 */

static int FDC_UpdateMotorStop(void)
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
	LOG_TRACE(TRACE_FDC, "fdc motor stopped VBL=%d video_cyc=%d %d@%d pc=%x\n",
		nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	FDC_Update_STR ( FDC_STR_BIT_MOTOR_ON | FDC_STR_BIT_SPIN_UP , 0 );	/* Unset motor and spinup bits */

	FDC.Command = FDCEMU_CMD_NULL;					/* Motor stopped, this is the last state */
	return 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Run 'RESTORE' command
 */
static int FDC_UpdateRestoreCmd(void)
{
	int	Delay_micro = 0;

	FDC_Update_STR ( 0 , FDC_STR_BIT_SPIN_UP );			/* at this point, spin up sequence is ok */

	/* Which command is running? */
	switch (FDC.CommandState)
	{
	 case FDCEMU_RUN_RESTORE_SEEKTOTRACKZERO:
		if ( FDC.TR == 0 )					/* Track 0 not reached after 255 attempts ? */
		{							/* (this should never happen in the case of Hatari) */
			FDC_Update_STR ( 0 , FDC_STR_BIT_RNF );
			FDC_Update_STR ( FDC_STR_BIT_TR00 , 0 );	/* Unset bit TR00 */
			/* Acknowledge interrupt, move along there's nothing more to see */
			FDC_AcknowledgeInterrupt();
			/* Set error */
			FDC_SetDMAStatus(true);				 /* DMA error */
			/* Done */
			Delay_micro = FDC_CmdCompleteCommon();
		}

		if ( HeadTrack[ FDC_DRIVE ] != 0 )		/* Are we at track zero ? */
		{
			FDC_Update_STR ( FDC_STR_BIT_TR00 , 0 );	/* Unset bit TR00 */
			FDC.TR--;					/* One less attempt */
			HeadTrack[ FDC_DRIVE ]--;		/* Move physical head */
			Delay_micro = FDC_StepRate_ms[ FDC_STEP_RATE ] * 1000;
		}
		else
		{
			FDC_Update_STR ( 0 , FDC_STR_BIT_TR00 );	/* Set bit TR00 */
			FDC.TR = 0;					/* Update Track Register to 0 */
			FDC.CommandState = FDCEMU_RUN_RESTORE_COMPLETE;
			Delay_micro = FDC_DELAY_COMMAND_COMPLETE;
		}
		break;
	 case FDCEMU_RUN_RESTORE_COMPLETE:
		/* Acknowledge interrupt, move along there's nothing more to see */
		FDC_AcknowledgeInterrupt();
		/* Set error */
		FDC_SetDMAStatus(false);          /* No DMA error */

		if ( FDC.CR & FDC_COMMAND_BIT_VERIFY )
			FDC_VerifyTrack();
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

	FDC_Update_STR ( 0 , FDC_STR_BIT_SPIN_UP );			/* at this point, spin up sequence is ok */

	/* Which command is running? */
	switch (FDC.CommandState)
	{
	 case FDCEMU_RUN_SEEK_TOTRACK:
		if ( FDC.TR == FDC.DR )					/* Are we at the selected track ? */
		{
			FDC.CommandState = FDCEMU_RUN_SEEK_COMPLETE;
			Delay_micro = FDC_DELAY_COMMAND_COMPLETE;
		}
		else
		{
			if ( FDC.DR < FDC.TR )				/* Set StepDirection to the correct value */
				FDC.StepDirection = -1;
			else
				FDC.StepDirection = 1;

			/* Move head by one track depending on FDC.StepDirection and update Track Register */
			FDC.TR += FDC.StepDirection;

			if ( ( HeadTrack[ FDC_DRIVE ] == FDC_PHYSICAL_MAX_TRACK ) && ( FDC.StepDirection == 1 ) )
				Delay_micro = FDC_DELAY_COMMAND_COMPLETE;	/* No delay if trying to go after max track */

			else if ( ( HeadTrack[ FDC_DRIVE ] == 0 ) && ( FDC.StepDirection == -1 ) )
			{
				FDC.TR = 0;				/* If we reach track 0, we stop there */
				FDC.CommandState = FDCEMU_RUN_SEEK_COMPLETE;
				Delay_micro = FDC_DELAY_COMMAND_COMPLETE;
			}

			else
			{
				HeadTrack[ FDC_DRIVE ] += FDC.StepDirection;	/* Move physical head */
				Delay_micro = FDC_StepRate_ms[ FDC_STEP_RATE ] * 1000;
			}
		}

		if ( HeadTrack[ FDC_DRIVE ] == 0 )
			FDC_Update_STR ( 0 , FDC_STR_BIT_TR00 );	/* Set bit TR00 */
		else
			FDC_Update_STR ( FDC_STR_BIT_TR00 , 0 );	/* Unset bit TR00 */

		break;
	 case FDCEMU_RUN_SEEK_COMPLETE:
		/* Acknowledge interrupt, move along there's nothing more to see */
		FDC_AcknowledgeInterrupt();
		/* Set error */
		FDC_SetDMAStatus(false);          /* No DMA error */

		if ( FDC.CR & FDC_COMMAND_BIT_VERIFY )
			FDC_VerifyTrack();
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

	FDC_Update_STR ( 0 , FDC_STR_BIT_SPIN_UP );			/* at this point, spin up sequence is ok */

	/* Which command is running? */
	switch (FDC.CommandState)
	{
	 case FDCEMU_RUN_STEP_ONCE:
		/* Move head by one track depending on FDC.StepDirection */
		if ( FDC.CR & FDC_COMMAND_BIT_UPDATE_TRACK )
			FDC.TR += FDC.StepDirection;

		if ( ( HeadTrack[ FDC_DRIVE ] == FDC_PHYSICAL_MAX_TRACK ) && ( FDC.StepDirection == 1 ) )
			Delay_micro = FDC_DELAY_COMMAND_COMPLETE;	/* No delay if trying to go after max track */

		else if ( ( HeadTrack[ FDC_DRIVE ] == 0 ) && ( FDC.StepDirection == -1 ) )
			Delay_micro = FDC_DELAY_COMMAND_COMPLETE;	/* No delay if trying to go before track 0 */

		else
		{
			HeadTrack[ FDC_DRIVE ] += FDC.StepDirection;/* Move physical head */
			Delay_micro = FDC_StepRate_ms[ FDC_STEP_RATE ] * 1000;
		}

		if ( HeadTrack[ FDC_DRIVE ] == 0 )
			FDC_Update_STR ( 0 , FDC_STR_BIT_TR00 );	/* Set bit TR00 */
		else
			FDC_Update_STR ( FDC_STR_BIT_TR00 , 0 );	/* Unset bit TR00 */

		FDC.CommandState = FDCEMU_RUN_STEP_COMPLETE;
		break;
	 case FDCEMU_RUN_STEP_COMPLETE:
		/* Acknowledge interrupt, move along there's nothing more to see */
		FDC_AcknowledgeInterrupt();
		/* Set error */
		FDC_SetDMAStatus(false);            /* No DMA error */

		if ( FDC.CR & FDC_COMMAND_BIT_VERIFY )
			FDC_VerifyTrack();
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

	FDC_Update_STR ( 0 , FDC_STR_BIT_SPIN_UP );			/* at this point, spin up sequence is ok */

	/* Which command is running? */
	switch (FDC.CommandState)
	{
	 case FDCEMU_RUN_STEPIN_ONCE:
		if ( FDC.CR & FDC_COMMAND_BIT_UPDATE_TRACK )
			FDC.TR++;

		if ( HeadTrack[ FDC_DRIVE ] == FDC_PHYSICAL_MAX_TRACK )
			Delay_micro = FDC_DELAY_COMMAND_COMPLETE;	/* No delay if trying to go after max track */
		else
		{
			HeadTrack[ FDC_DRIVE ]++;			/* Move physical head */
			Delay_micro = FDC_StepRate_ms[ FDC_STEP_RATE ] * 1000;
		}

		if ( HeadTrack[ FDC_DRIVE ] == 0 )
			FDC_Update_STR ( 0 , FDC_STR_BIT_TR00 );	/* Set bit TR00 */
		else
			FDC_Update_STR ( FDC_STR_BIT_TR00 , 0 );	/* Unset bit TR00 */

		FDC.CommandState = FDCEMU_RUN_STEPIN_COMPLETE;
		break;
	 case FDCEMU_RUN_STEPIN_COMPLETE:
		/* Acknowledge interrupt, move along there's nothing more to see */
		FDC_AcknowledgeInterrupt();
		/* Set error */
		FDC_SetDMAStatus(false);            /* No DMA error */

		if ( FDC.CR & FDC_COMMAND_BIT_VERIFY )
			FDC_VerifyTrack();
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

	FDC_Update_STR ( 0 , FDC_STR_BIT_SPIN_UP );			/* at this point, spin up sequence is ok */

	/* Which command is running? */
	switch (FDC.CommandState)
	{
	 case FDCEMU_RUN_STEPOUT_ONCE:
		if ( FDC.CR & FDC_COMMAND_BIT_UPDATE_TRACK )
			FDC.TR--;

		if ( HeadTrack[ FDC_DRIVE ] == 0 )
			Delay_micro = FDC_DELAY_COMMAND_COMPLETE;	/* No delay if trying to go before track 0 */
		else
		{
			HeadTrack[ FDC_DRIVE ]--;			/* Move physical head */
			Delay_micro = FDC_StepRate_ms[ FDC_STEP_RATE ] * 1000;
		}

		if ( HeadTrack[ FDC_DRIVE ] == 0 )
			FDC_Update_STR ( 0 , FDC_STR_BIT_TR00 );	/* Set bit TR00 */
		else
			FDC_Update_STR ( FDC_STR_BIT_TR00 , 0 );	/* Unset bit TR00 */

		FDC.CommandState = FDCEMU_RUN_STEPOUT_COMPLETE;
		break;
	 case FDCEMU_RUN_STEPOUT_COMPLETE:
		/* Acknowledge interrupt, move along there's nothing more to see */
		FDC_AcknowledgeInterrupt();
		/* Set error */
		FDC_SetDMAStatus(false);            /* No DMA error */

		if ( FDC.CR & FDC_COMMAND_BIT_VERIFY )
			FDC_VerifyTrack();
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
	int	FrameCycles, HblCounterVideo, LineCycles;
	int	SectorSize;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );


	/* Which command is running? */
	switch (FDC.CommandState)
	{
	 case FDCEMU_RUN_READSECTORS_READDATA:
		/* Read a single sector into temporary buffer (512 bytes for ST/MSA) */
		FDC_DMA_InitTransfer ();				/* Update FDC_DMA.PosInBuffer */
		if ( FDC_ReadSectorFromFloppy ( DMADiskWorkSpace + FDC_DMA.PosInBuffer , FDC.SR , &SectorSize ) )
		{
			FDC_DMA.BytesToTransfer += SectorSize;		/* 512 bytes per sector for ST/MSA disk images */
			FDC_DMA.PosInBuffer += SectorSize;
				
			FDC.CommandState = FDCEMU_RUN_READSECTORS_READDATA_DMA;
			Delay_micro = FDC_DELAY_TRANSFER_DMA_16;	/* Transfer blocks of 16 bytes from the sector we just read */
		}
		else							/* Sector FDC.SR was not found */
		{
			FDC.CommandState = FDCEMU_RUN_READSECTORS_RNF;
			Delay_micro = FDC_DELAY_RNF;
		}
		break;
	 case FDCEMU_RUN_READSECTORS_READDATA_DMA:
		if ( ! FDC_DMA_ReadFromFloppy () )
		{
			Delay_micro = FDC_DELAY_TRANSFER_DMA_16;	/* Continue transferring blocks of 16 bytes */
		}
		else							/* Sector completly transferred, check for multi bit */
		{
			if ( FDC.CR & FDC_COMMAND_BIT_MULTIPLE_SECTOR  )
			{
				FDC.SR++;				/* Try to read next sector and set RNF if not possible */
				FDC.CommandState = FDCEMU_RUN_READSECTORS_READDATA;
				Delay_micro = FDC_DELAY_COMMAND_IMMEDIATE;
			}
			else						/* Multi=0, stop here with no error */
			{
				FDC.CommandState = FDCEMU_RUN_READSECTORS_COMPLETE;
				Delay_micro = FDC_DELAY_COMMAND_COMPLETE;
			}
		}
		break;
	 case FDCEMU_RUN_READSECTORS_RNF:
		LOG_TRACE(TRACE_FDC, "fdc type II read sector=%d track=%d RNF VBL=%d video_cyc=%d %d@%d pc=%x\n",
			  FDC.SR , HeadTrack[ FDC_DRIVE ] , nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

		FDC_Update_STR ( 0 , FDC_STR_BIT_RNF );
		/* Acknowledge interrupt, move along there's nothing more to see */
		FDC_AcknowledgeInterrupt();
		/* Set error */
		FDC_SetDMAStatus(true);             /* DMA error */
		/* Done */
		Delay_micro = FDC_CmdCompleteCommon();
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
	int	FrameCycles, HblCounterVideo, LineCycles;
	int	SectorSize;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	if ( Floppy_IsWriteProtected ( FDC_DRIVE ) )
	{
		LOG_TRACE(TRACE_FDC, "fdc type II write sector=%d track=%d WPRT VBL=%d video_cyc=%d %d@%d pc=%x\n",
			  FDC.SR , HeadTrack[ FDC_DRIVE ] , nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

		FDC_Update_STR ( 0 , FDC_STR_BIT_WPRT );		/* Set WPRT bit */
		FDC_AcknowledgeInterrupt();
		/* Set error */
		FDC_SetDMAStatus(true);             /* DMA error */
		/* Done */
		Delay_micro = FDC_CmdCompleteCommon();
	}
	else
		FDC_Update_STR ( FDC_STR_BIT_WPRT , 0 );		/* Unset WPRT bit */

	
	/* Which command is running? */
	switch (FDC.CommandState)
	{
	 case FDCEMU_RUN_WRITESECTORS_WRITEDATA:
		/* Write a single sector from RAM (512 bytes for ST/MSA) */
		FDC_DMA_InitTransfer ();				/* Update FDC_DMA.PosInBuffer */
		if ( FDC_WriteSectorToFloppy ( FDC_DMA.SectorCount , FDC.SR , &SectorSize ) )
		{
			FDC_DMA.BytesToTransfer += SectorSize;		/* 512 bytes per sector for ST/MSA disk images */
			FDC_DMA.PosInBuffer += SectorSize;
				
			FDC.CommandState = FDCEMU_RUN_WRITESECTORS_WRITEDATA_DMA;
			Delay_micro = FDC_DELAY_TRANSFER_DMA_16;	/* Transfer blocks of 16 bytes from the sector we just wrote */
		}
		else							/* Sector FDC.SR was not found */
		{
			FDC.CommandState = FDCEMU_RUN_READSECTORS_RNF;
			Delay_micro = FDC_DELAY_RNF;
		}
		break;
	 case FDCEMU_RUN_WRITESECTORS_WRITEDATA_DMA:
		if ( ! FDC_DMA_WriteToFloppy () )
		{
			Delay_micro = FDC_DELAY_TRANSFER_DMA_16;	/* Continue transferring blocks of 16 bytes */
		}
		else							/* Sector completly transferred, check for multi bit */
		{
			if ( FDC.CR & FDC_COMMAND_BIT_MULTIPLE_SECTOR  )
			{
				FDC.SR++;				/* Try to write next sector and set RNF if not possible */
				FDC.CommandState = FDCEMU_RUN_WRITESECTORS_WRITEDATA;
				Delay_micro = FDC_DELAY_COMMAND_IMMEDIATE;
			}
			else						/* Multi=0, stop here with no error */
			{
				FDC.CommandState = FDCEMU_RUN_WRITESECTORS_COMPLETE;
				Delay_micro = FDC_DELAY_COMMAND_COMPLETE;
			}
		}
		break;
	 case FDCEMU_RUN_WRITESECTORS_RNF:
		LOG_TRACE(TRACE_FDC, "fdc type II write sector=%d track=%d RNF VBL=%d video_cyc=%d %d@%d pc=%x\n",
			  FDC.SR , HeadTrack[ FDC_DRIVE ] , nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

		FDC_Update_STR ( 0 , FDC_STR_BIT_RNF );
		/* Acknowledge interrupt, move along there's nothing more to see */
		FDC_AcknowledgeInterrupt();
		/* Set error */
		FDC_SetDMAStatus(true);             /* DMA error */
		/* Done */
		Delay_micro = FDC_CmdCompleteCommon();
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
	Uint16	CRC;
	Uint8	buf[ 4+6 ];
	Uint8	*p;

	if ( ! EmulationDrives[FDC_DRIVE].bDiskInserted )	/* Set RNF bit if no disk is inserted */
	{
		FDC_Update_STR ( 0 , FDC_STR_BIT_RNF );
		FDC_AcknowledgeInterrupt();
		/* Set error */
		FDC_SetDMAStatus(true);             /* DMA error */
		/* Done */
		Delay_micro = FDC_CmdCompleteCommon();
	}

	
	/* Which command is running? */
	switch (FDC.CommandState)
	{
	 case FDCEMU_RUN_READADDRESS:
		if ( FDC.ID_FieldLastSector > nReadWriteSectorsPerTrack )
			FDC.ID_FieldLastSector = 1;

		/* In the case of Hatari, only ST/MSA images are supported, so we build */
		/* a simplified ID fied based on current track/sector/side */
		p = buf;
		*p++ = 0xa1;					/* SYNC bytes and IAM byte are included in the CRC */
		*p++ = 0xa1;
		*p++ = 0xa1;
		*p++ = 0xfe;
		*p++ = HeadTrack[ FDC_DRIVE ];
		FDC.SR = HeadTrack[ FDC_DRIVE ];		/* The 1st byte of the ID field is also copied into Sector Register */
		*p++ = FDC_SIDE;
		*p++ = FDC.ID_FieldLastSector;
		*p++ = FDC_SECTOR_SIZE_512;			/* ST/MSA images are 512 bytes per sector */

		FDC_CRC16 ( buf , 8 , &CRC );

		*p++ = CRC >> 8;
		*p++ = CRC & 0xff;

		FDC.ID_FieldLastSector++;			/* Increase sector for next Read Address command */

		FDC_DMA_InitTransfer ();			/* Update FDC_DMA.PosInBuffer */
		memcpy ( DMADiskWorkSpace + FDC_DMA.PosInBuffer , buf + 4 , 6 );	/* Don't return the 3 x $A1 and $FE in the Address Field */
		FDC_DMA.BytesToTransfer += 6;			/* 6 bytes per ID field */
		FDC_DMA.PosInBuffer += 6;

		FDC.CommandState = FDCEMU_RUN_READADDRESS_DMA;
		Delay_micro = FDC_DELAY_READ_ADDR_STANDARD;
		break;
	 case FDCEMU_RUN_READADDRESS_DMA:
		FDC_DMA_ReadFromFloppy ();			/* Transfer bytes if 16 bytes or more are in the DMA buffer */

		FDC.CommandState = FDCEMU_RUN_READADDRESS_COMPLETE;
		Delay_micro = FDC_DELAY_COMMAND_COMPLETE;
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


/*-----------------------------------------------------------------------*/
/**
 * Run 'READ TRACK' command
 */
static int FDC_UpdateReadTrackCmd(void)
{
	int	Delay_micro = 0;
	Uint16	CRC;
	Uint8	*buf;
	Uint8	*buf_crc;
	int	Sector;
	int	SectorSize;
	int	i;

	if ( ! EmulationDrives[FDC_DRIVE].bDiskInserted )	/* Set RNF bit if no disk is inserted */
	{
		FDC_Update_STR ( 0 , FDC_STR_BIT_RNF );
		FDC_AcknowledgeInterrupt();
		/* Set error */
		FDC_SetDMAStatus(true);             /* DMA error */
		/* Done */
		Delay_micro = FDC_CmdCompleteCommon();
	}


	/* Which command is running? */
	switch (FDC.CommandState)
	{
	 case FDCEMU_RUN_READTRACK:

		/* Build the track data */
		FDC_DMA_InitTransfer ();					/* Update FDC_DMA.PosInBuffer */
		buf = DMADiskWorkSpace + FDC_DMA.PosInBuffer;
		for ( i=0 ; i<60 ; i++ )		*buf++ = 0x4e;		/* GAP1 */

		for ( Sector=1 ; Sector<=nReadWriteSectorsPerTrack ; Sector++ )
		{
			for ( i=0 ; i<12 ; i++ )	*buf++ = 0x00;		/* GAP2 */

			buf_crc = buf;
			for ( i=0 ; i<3 ; i++ )		*buf++ = 0xa1;		/* SYNC (write $F5) */
			*buf++ = 0xfe;						/* Index Address Mark */
			*buf++ = HeadTrack[ FDC_DRIVE ];			/* Track */
			*buf++ = FDC_SIDE;					/* Side */
			*buf++ = Sector;					/* Sector */
			*buf++ = FDC_SECTOR_SIZE_512;				/* 512 bytes/sector for ST/MSA */
			FDC_CRC16 ( buf_crc , buf - buf_crc , &CRC );
			*buf++ = CRC >> 8;					/* CRC1 (write $F7) */
			*buf++ = CRC & 0xff;					/* CRC2 */

			for ( i=0 ; i<22 ; i++ )	*buf++ = 0x4e;		/* GAP3a */
			for ( i=0 ; i<12 ; i++ )	*buf++ = 0x00;		/* GAP3b */

			buf_crc = buf;
			for ( i=0 ; i<3 ; i++ )		*buf++ = 0xa1;		/* SYNC (write $F5) */
			*buf++ = 0xfb;						/* Data Address Mark */

			if ( ! FDC_ReadSectorFromFloppy ( buf , Sector , &SectorSize ) )	/* Read a single 512 bytes sector into temporary buffer */
			{
				FDC_Update_STR ( 0 , FDC_STR_BIT_RNF );
				FDC_AcknowledgeInterrupt();
				/* Set error */
				FDC_SetDMAStatus(true);             /* DMA error */
				/* Done */
				Delay_micro = FDC_CmdCompleteCommon();
			}
			buf += SectorSize;

			FDC_CRC16 ( buf_crc , buf - buf_crc , &CRC );
			*buf++ = CRC >> 8;					/* CRC1 (write $F7) */
			*buf++ = CRC & 0xff;					/* CRC2 */

			for ( i=0 ; i<40 ; i++ )	*buf++ = 0x4e;		/* GAP4 */
		}

		while ( buf < DMADiskWorkSpace + FDC_DMA.PosInBuffer + FDC_TRACK_BYTES_STANDARD )	/* Complete the track buffer */
		       *buf++ = 0x4e;						/* GAP5 */


		/* Transfer Track data to RAM using DMA */
		FDC_DMA.BytesToTransfer += FDC_TRACK_BYTES_STANDARD;
		FDC_DMA.PosInBuffer += FDC_TRACK_BYTES_STANDARD;

		FDC.CommandState = FDCEMU_RUN_READTRACK_DMA;
		Delay_micro = FDC_DELAY_TRANSFER_DMA_16;			/* Transfer blocks of 16 bytes from the track we just read */
		break;
	 case FDCEMU_RUN_READTRACK_DMA:
		if ( ! FDC_DMA_ReadFromFloppy () )
		{
			Delay_micro = FDC_DELAY_TRANSFER_DMA_16;		/* Continue transferring blocks of 16 bytes */
		}
		else								/* Track completly transferred */
		{
			FDC.CommandState = FDCEMU_RUN_READTRACK_COMPLETE;
			Delay_micro = FDC_DELAY_COMMAND_COMPLETE;
		}
		break;
	 case FDCEMU_RUN_READTRACK_COMPLETE:
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
 * Start motor / spin up sequence if needed
 */

static int FDC_Check_MotorON ( Uint8 FDC_CR )
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	if ( ( ( FDC_CR & FDC_COMMAND_BIT_MOTOR_ON ) == 0 )				/* command wants motor on / spin up */
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
	FDC.Command = FDCEMU_CMD_RESTORE;
	FDC.CommandState = FDCEMU_RUN_RESTORE_SEEKTOTRACKZERO;

	FDC_Update_STR ( FDC_STR_BIT_INDEX | FDC_STR_BIT_CRC_ERROR | FDC_STR_BIT_RNF , FDC_STR_BIT_BUSY );

	/* The FDC will try 255 times to reach track 0 using step out signals */
	/* If track 0 signal is not detected after 255 attempts, the command is interrupted */
	/* and FDC_STR_BIT_RNF is set in the Status Register. */
	/* This will never happen in the case of Hatari, because the physical track can't go */
	/* beyond track FDC_PHYSICAL_MAX_TRACK (=90) */
	FDC.TR = 0xff;				

	FDC_SetDiskControllerStatus();
	return FDC_DELAY_TYPE_I_PREPARE;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeI_Seek(void)
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type I seek track=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  FDC.DR, nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to seek to chosen track */
	FDC.Command = FDCEMU_CMD_SEEK;
	FDC.CommandState = FDCEMU_RUN_SEEK_TOTRACK;

	FDC_Update_STR ( FDC_STR_BIT_INDEX | FDC_STR_BIT_CRC_ERROR | FDC_STR_BIT_RNF , FDC_STR_BIT_BUSY );

	FDC_SetDiskControllerStatus();
	return FDC_DELAY_TYPE_I_PREPARE;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeI_Step(void)
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type I step %d VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  FDC.StepDirection, nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to step (same direction as last seek executed, eg 'FDC.StepDirection') */
	FDC.Command = FDCEMU_CMD_STEP;
	FDC.CommandState = FDCEMU_RUN_STEP_ONCE;

	FDC_Update_STR ( FDC_STR_BIT_INDEX | FDC_STR_BIT_CRC_ERROR | FDC_STR_BIT_RNF , FDC_STR_BIT_BUSY );

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

	/* Set emulation to step in(Set 'FDC.StepDirection') */
	FDC.Command = FDCEMU_CMD_STEPIN;
	FDC.CommandState = FDCEMU_RUN_STEPIN_ONCE;
	FDC.StepDirection = 1;						/* Increment track*/

	FDC_Update_STR ( FDC_STR_BIT_INDEX | FDC_STR_BIT_CRC_ERROR | FDC_STR_BIT_RNF , FDC_STR_BIT_BUSY );

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

	/* Set emulation to step out(Set 'FDC.StepDirection') */
	FDC.Command = FDCEMU_CMD_STEPOUT;
	FDC.CommandState = FDCEMU_RUN_STEPOUT_ONCE;
	FDC.StepDirection = -1;						/* Decrement track */

	FDC_Update_STR ( FDC_STR_BIT_INDEX | FDC_STR_BIT_CRC_ERROR | FDC_STR_BIT_RNF , FDC_STR_BIT_BUSY );

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
	int	Delay_micro = 0;
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type II read sector sect=0x%x multi=%s dmasector=%d addr=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  FDC.SR, ( FDC.CR & FDC_COMMAND_BIT_MULTIPLE_SECTOR ) ? "on" : "off" , FDC_DMA.SectorCount ,
		  FDC_ReadDMAAddress(), nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to read sector(s) */
	FDC.Command = FDCEMU_CMD_READSECTORS;
	FDC.CommandState = FDCEMU_RUN_READSECTORS_READDATA;
	/* Set reading parameters */
	FDC_SetReadWriteParameters();

	FDC_Update_STR ( FDC_STR_BIT_DRQ | FDC_STR_BIT_LOST_DATA | FDC_STR_BIT_CRC_ERROR
		| FDC_STR_BIT_RNF | FDC_STR_BIT_RECORD_TYPE , FDC_STR_BIT_BUSY );

	if ( FDC.CR & FDC_COMMAND_BIT_HEAD_LOAD )
		Delay_micro = FDC_DELAY_HEAD_LOAD;
	
	FDC_SetDiskControllerStatus();
	return FDC_DELAY_TYPE_II_PREPARE + Delay_micro;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeII_WriteSector(void)
{
	int	Delay_micro = 0;
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type II write sector %d multi=%s dmasector=%d addr=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  FDC.SR, ( FDC.CR & FDC_COMMAND_BIT_MULTIPLE_SECTOR ) ? "on" : "off" , FDC_DMA.SectorCount,
		  FDC_ReadDMAAddress(), nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to write a sector(s) */
	FDC.Command = FDCEMU_CMD_WRITESECTORS;
	FDC.CommandState = FDCEMU_RUN_WRITESECTORS_WRITEDATA;
	/* Set writing parameters */
	FDC_SetReadWriteParameters();

	FDC_Update_STR ( FDC_STR_BIT_DRQ | FDC_STR_BIT_LOST_DATA | FDC_STR_BIT_CRC_ERROR
		| FDC_STR_BIT_RNF | FDC_STR_BIT_RECORD_TYPE , FDC_STR_BIT_BUSY );

	if ( FDC.CR & FDC_COMMAND_BIT_HEAD_LOAD )
		Delay_micro = FDC_DELAY_HEAD_LOAD;
	
	FDC_SetDiskControllerStatus();
	return FDC_DELAY_TYPE_II_PREPARE + Delay_micro;
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
	int	Delay_micro = 0;
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type III read address track=0x%x side=%d addr=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  HeadTrack[ FDC_DRIVE ], FDC_SIDE, FDC_ReadDMAAddress(),
		  nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to seek to track zero */
	FDC.Command = FDCEMU_CMD_READADDRESS;
	FDC.CommandState = FDCEMU_RUN_READADDRESS;

	FDC_Update_STR ( FDC_STR_BIT_DRQ | FDC_STR_BIT_LOST_DATA | FDC_STR_BIT_CRC_ERROR
		| FDC_STR_BIT_RNF | FDC_STR_BIT_RECORD_TYPE , FDC_STR_BIT_BUSY );

	if ( FDC.CR & FDC_COMMAND_BIT_HEAD_LOAD )
		Delay_micro = FDC_DELAY_HEAD_LOAD;
	
	FDC_SetDiskControllerStatus();
	return FDC_DELAY_TYPE_III_PREPARE + Delay_micro;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeIII_ReadTrack(void)
{
	int	Delay_micro = 0;
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type III read track track=0x%x side=%d addr=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  HeadTrack[ FDC_DRIVE ], FDC_SIDE, FDC_ReadDMAAddress(),
		  nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to read a single track */
	FDC.Command = FDCEMU_CMD_READTRACK;
	FDC.CommandState = FDCEMU_RUN_READTRACK;
	/* Set reading parameters */
	FDC_SetReadWriteParameters();

	FDC_Update_STR ( FDC_STR_BIT_DRQ | FDC_STR_BIT_LOST_DATA | FDC_STR_BIT_CRC_ERROR
		| FDC_STR_BIT_RNF | FDC_STR_BIT_RECORD_TYPE , FDC_STR_BIT_BUSY );

	if ( FDC.CR & FDC_COMMAND_BIT_HEAD_LOAD )
		Delay_micro = FDC_DELAY_HEAD_LOAD;

	FDC_SetDiskControllerStatus();
	return FDC_DELAY_TYPE_III_PREPARE + Delay_micro;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeIII_WriteTrack(void)
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type III write track track=0x%x side=%d addr=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  HeadTrack[ FDC_DRIVE ], FDC_SIDE, FDC_ReadDMAAddress(),
		  nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	Log_Printf(LOG_TODO, "FDC type III command 'write track' does not work yet!\n");

	/* FIXME: "Write track" should write all the sectors after extracting them from the track data */

	/* Set emulation to write a single track */
	FDC_Update_STR ( 0 , FDC_STR_BIT_RNF );				/* FIXME : Not supported yet, set RNF bit */
	FDC.Command = FDCEMU_CMD_NULL;
	FDC.CommandState = FDCEMU_RUN_NULL;
	/* Set writing parameters */
	FDC_SetReadWriteParameters();

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
	int	Delay_micro;
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type IV force int VBL=%d video_cyc=%d %d@%dpc=%x\n",
		  nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* For Type II/III commands, LOST DATA bit is never set (DRQ is always handled by the DMA) (Super Monaco GP on Superior 65) */
	FDC_Update_STR ( FDC_STR_BIT_LOST_DATA , 0 );			/* Remove LOST DATA / TR00 bit */

	/* TR00 is updated when a type I command is interrupted or when no command was running */
	if ( ( ( DiskControllerStatus_ff8604rd & FDC_STR_BIT_BUSY ) == 0 )	/* No command running */
	  || ( FDC.CommandType == 1 ) )						/* Busy command is Type I */
	{
		if ( HeadTrack[ FDC_DRIVE ] == 0 )
			FDC_Update_STR ( 0 , FDC_STR_BIT_TR00 );	/* Set bit TR00 */
	}

	FDC_Update_STR ( FDC_STR_BIT_BUSY , 0 );			/* Remove BUSY bit */

	/* Acknowledge interrupt, move along there's nothing more to see */
	if (bCauseCPUInterrupt)
		FDC_AcknowledgeInterrupt();

	/* Remove busy bit and stop the motor */
	Delay_micro = FDC_CmdCompleteCommon();

	return FDC_DELAY_TYPE_IV_PREPARE + Delay_micro;
}


/*-----------------------------------------------------------------------*/
/**
 * Execute Type I commands
 */
static int FDC_ExecuteTypeICommands(void)
{
	int	Delay_micro = 0;

	FDC.CommandType = 1;
	MFP_GPIP |= 0x20;

	/* Check Type I Command */
	switch ( FDC.CR & 0xf0 )
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

	/* Check if motor needs to be started and add possible delay */
	Delay_micro += FDC_Check_MotorON ( FDC.CR );
	
	return Delay_micro;
}


/*-----------------------------------------------------------------------*/
/**
 * Execute Type II commands
 */
static int FDC_ExecuteTypeIICommands(void)
{
	int	Delay_micro = 0;

	FDC.CommandType = 2;
	MFP_GPIP |= 0x20;

	/* Check Type II Command */
	switch ( FDC.CR & 0xf0 )
	{
	 case 0x80:             /* Read Sector multi=0*/
	 case 0x90:             /* Read Sectors multi=1 */
		Delay_micro = FDC_TypeII_ReadSector();
		break;
	 case 0xa0:             /* Write Sector multi=0 */
	 case 0xb0:             /* Write Sectors multi=1 */
		Delay_micro = FDC_TypeII_WriteSector();
		break;
	}

	/* Check if motor needs to be started and add possible delay */
	Delay_micro += FDC_Check_MotorON ( FDC.CR );

	return Delay_micro;
}


/*-----------------------------------------------------------------------*/
/**
 * Execute Type III commands
 */
static int FDC_ExecuteTypeIIICommands(void)
{
	int	Delay_micro = 0;

	FDC.CommandType = 3;
	MFP_GPIP |= 0x20;

	/* Check Type III Command */
	switch ( FDC.CR & 0xf0 )
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
	Delay_micro += FDC_Check_MotorON ( FDC.CR );

	return Delay_micro;
}


/*-----------------------------------------------------------------------*/
/**
 * Execute Type IV commands
 */
static int FDC_ExecuteTypeIVCommands(void)
{
	int	Delay_micro;

	if ( FDC.CR != 0xD8 )						/* Is an 'immediate interrupt command' ? Don't reset interrupt */
		MFP_GPIP |= 0x20;

	/* Check Type IV Command */
	if ( ( FDC.CR & 0x0c ) == 0 )					/* I3 and I2 are clear? If so we don't need a CPU interrupt */
		Delay_micro = FDC_TypeIV_ForceInterrupt(false);		/* Force Interrupt - no interrupt */
	else
		Delay_micro = FDC_TypeIV_ForceInterrupt(true);		/* Force Interrupt */

	FDC.CommandType = 4;						/* Change CommandType after interrupting the current command */
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
	if ( ( FDC.CR & 0x80 ) == 0 )					/* Type I - Restore, Seek, Step, Step-In, Step-Out */
		Delay_micro = FDC_ExecuteTypeICommands();
	else if ( ( FDC.CR & 0x40 ) == 0 )				/* Type II - Read Sector, Write Sector */
		Delay_micro = FDC_ExecuteTypeIICommands();
	else if ( ( FDC.CR & 0xf0 ) != 0xd0 )				/* Type III - Read Address, Read Track, Write Track */
		Delay_micro = FDC_ExecuteTypeIIICommands();
	else								/* Type IV - Force Interrupt */
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
		  IoMem_ReadByte(0xff8605), nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	FDC_DMA.SectorCount = IoMem_ReadByte(0xff8605);
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
		  IoMem_ReadByte(0xff8605), nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* If fdc is busy, only 'Force Interrupt' is possible */
	if ( ( DiskControllerStatus_ff8604rd & FDC_STR_BIT_BUSY )
		&& ( ( IoMem_ReadByte(0xff8605) & 0xf0 ) != 0xd0 ) )
	{
		LOG_TRACE(TRACE_FDC, "fdc write 8604 fdc busy, command=0x%x ignored VBL=%d video_cyc=%d %d@%d pc=%x\n",
			IoMem_ReadByte(0xff8605), nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());
		return;
	}

	FDC.CR = IoMem_ReadByte(0xff8605);
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
		IoMem_ReadByte(0xff8605) , nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	/* If fdc is busy, Track Register can't be modified */
	if ( DiskControllerStatus_ff8604rd & FDC_STR_BIT_BUSY )
	{
		LOG_TRACE(TRACE_FDC, "fdc write 8604 fdc busy, track=0x%x ignored VBL=%d video_cyc=%d %d@%d pc=%x\n",
			IoMem_ReadByte(0xff8605), nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());
		return;
	}

	FDC.TR = IoMem_ReadByte(0xff8605);
}


/*-----------------------------------------------------------------------*/
/**
 * Write to Sector register (WR 0xff8604)
 */
static void FDC_WriteSectorRegister(void)
{
	int FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write 8604 sector=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		IoMem_ReadByte(0xff8605) , nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	/* If fdc is busy, Sector Register can't be modified */
	if ( DiskControllerStatus_ff8604rd & FDC_STR_BIT_BUSY )
	{
		LOG_TRACE(TRACE_FDC, "fdc write 8604 fdc busy, sector=0x%x ignored VBL=%d video_cyc=%d %d@%d pc=%x\n",
			IoMem_ReadByte(0xff8605), nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());
		return;
	}

	FDC.SR = IoMem_ReadByte(0xff8605);
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
		IoMem_ReadByte(0xff8605), nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	FDC.DR = IoMem_ReadByte(0xff8605);
}


/*-----------------------------------------------------------------------*/
/**
 * Store byte in FDC registers or DMA sector count, when write to 0xff8604
 */
void FDC_DiskController_WriteWord(void)
{
	int FrameCycles, HblCounterVideo, LineCycles;

	if ( nIoMemAccessSize == SIZE_BYTE )
	{
		/* This register does not like to be accessed in byte mode on a normal ST */
		M68000_BusError(IoAccessBaseAddress, BUS_ERROR_WRITE);
		return;
	}

	M68000_WaitState(4);

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write 8604 data=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		IoMem_ReadWord(0xff8604), nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	/* Is it an ASCII HD command? */
	if ( ( DMAModeControl_ff8606wr & 0x0018 ) == 8 )
	{
		/*  Handle HDC functions */
		HDC_WriteCommandPacket();
		return;
	}

	/* Are we trying to set the SectorCount ? */
	if ( DMAModeControl_ff8606wr & 0x10 )				/* Bit 4 */
		FDC_WriteSectorCountRegister();
	else
	{
		/* Write to FDC registers */
		switch ( DMAModeControl_ff8606wr & 0x6 )
		{   /* Bits 1,2 (A1,A0) */
		 case 0x0:						/* 0 0 - Command register */
			FDC_WriteCommandRegister();
			break;
		 case 0x2:						/* 0 1 - Track register */
			FDC_WriteTrackRegister();
			break;
		 case 0x4:						/* 1 0 - Sector register */
			FDC_WriteSectorRegister();
			break;
		 case 0x6:						/* 1 1 - Data register */
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
	Sint16 DiskControllerByte = 0;					/* Used to pass back the parameter */
	int FrameCycles, HblCounterVideo, LineCycles;


	if (nIoMemAccessSize == SIZE_BYTE)
	{
		/* This register does not like to be accessed in byte mode on a normal ST */
		M68000_BusError(IoAccessBaseAddress, BUS_ERROR_READ);
		return;
	}

	M68000_WaitState(4);

	if ((DMAModeControl_ff8606wr & 0x18) == 0x08)			/* HDC status reg selected? */
	{
		/* return the HDC status reg */
		DiskControllerByte = HDCCommand.returnCode;
	}
	else if ((DMAModeControl_ff8606wr & 0x18) == 0x18)		/* HDC sector counter??? */
	{
		Log_Printf(LOG_DEBUG, "*** Read HDC sector counter???\n");
		DiskControllerByte = HDCSectorCount;
	}
	else
	{
		/* old FDC code */
		switch (DMAModeControl_ff8606wr&0x6)			/* Bits 1,2 (A1,A0) */
		{
		 case 0x0:						/* 0 0 - Status register */
			DiskControllerByte = DiskControllerStatus_ff8604rd;

			if (Floppy_IsWriteProtected(FDC_DRIVE))
				DiskControllerByte |= 0x40;

			if (EmulationDrives[FDC_DRIVE].bMediaChanged)
			{
				/* Some games apparently poll the write-protection signal to check
				 * for disk image changes (the signal seems to change when you
				 * exchange disks on a real ST). We now also simulate this behaviour
				 * here, so that these games can continue with the other disk. */
				DiskControllerByte ^= 0x40;
				EmulationDrives[FDC_DRIVE].bMediaChanged = false;
			}

			/* Reset FDC GPIP */
			MFP_GPIP |= 0x20;
			break;
		 case 0x2:						/* 0 1 - Track register */
			DiskControllerByte = FDC.TR;
			break;
		 case 0x4:						/* 1 0 - Sector register */
			DiskControllerByte = FDC.SR;
			break;
		 case 0x6:						/* 1 1 - Data register */
			DiskControllerByte = FDC.DR;
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
static bool FDC_ReadSectorFromFloppy ( Uint8 *buf , Uint8 Sector , int *pSectorSize )
{
	int FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc read sector addr=0x%x dev=%d sect=%d track=%d side=%d VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		FDC_ReadDMAAddress(), FDC_DRIVE, Sector, HeadTrack[ FDC_DRIVE ], FDC_SIDE,
		nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	/* Copy 1 sector to our workspace */
	if ( Floppy_ReadSectors ( FDC_DRIVE, buf, Sector, HeadTrack[ FDC_DRIVE ], FDC_SIDE, 1, NULL, pSectorSize ) )
		return true;

	/* Failed */
	LOG_TRACE(TRACE_FDC, "fdc read sector failed\n" );
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Write sector from RAM to floppy drive
 * We copy the bytes in chunks to simulate writing of the floppy using DMA
 * If DMASectorsCount==0, the DMA won't transfer any byte from RAM to the FDC
 * and some '0' bytes will be written to the disk.
 */
static bool FDC_WriteSectorToFloppy ( int DMASectorsCount , Uint8 Sector , int *pSectorSize )
{
	Uint8 *pBuffer;
	int FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write sector addr=0x%x dev=%d sect=%d track=%d side=%d VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		FDC_ReadDMAAddress(), FDC_DRIVE, Sector, HeadTrack[ FDC_DRIVE ], FDC_SIDE,
		nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	if ( DMASectorsCount > 0 )
		pBuffer = &STRam[ FDC_ReadDMAAddress() ];
	else
	{
		pBuffer = DMADiskWorkSpace;				/* If DMA can't transfer data, we write '0' bytes */
		memset ( pBuffer , 0 , DMA_DISK_SECTOR_SIZE );
	}
	
	/* Write 1 sector from our workspace */
	if ( Floppy_WriteSectors ( FDC_DRIVE, pBuffer, Sector, HeadTrack[ FDC_DRIVE ], FDC_SIDE, 1, NULL, pSectorSize ) )
		return true;

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
