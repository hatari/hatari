/*
  Hatari - fdc.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Floppy Disk Controller(FDC) emulation.
  All commands are emulated with good timings estimation, as many programs
  (demo or cracked games) rely on accurate FDC timings and DMA transfer by blocks
  of 16 bytes.
  The behaviour of all FDC's registers matches the official docs and should not
  cause programs to fail when accessing the FDC (especially for Status Register).
  As Hatari only handles ST/MSA disk images that only support 512 bytes sectors as
  well as a fixed number of sectors per track, a few parts of the FDC emulation are
  simplified and would need to be changed to handle more complex disk images (Pasti).
*/

const char FDC_fileid[] = "Hatari fdc.c";

#include <inttypes.h>

#include "main.h"
#include "configuration.h"
#include "fdc.h"
#include "hdc.h"
#include "floppy.h"
#include "floppy_ipf.h"
#include "floppy_stx.h"
#include "ioMem.h"
#include "log.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "psg.h"
#include "stMemory.h"
#include "video.h"
#include "clocks_timings.h"
#include "utils.h"
#include "statusbar.h"


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
    1100    Envelope Period Coarse Tune
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
      After a type I command, this bit is constantly updated an give the current value of the WPT signal.
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

  0xff860e - DD/HD mode selection - NOTE bits 2-15 are not used
    This register is only available on MegaSTE, TT and Falcon
    Bit 0 - FDC Frequency  0=8 MHz  1=16 MHz
    Bit 1 - Density  0=DD  1=HD

  0xff9200 - DIP switches setting - NOTE bits 0-7 are used by joypads on STE and Falcon
    Bit 8-15 of this register are only used on MegaSTE, TT and Falcon and reflect
      the state of the 8 DIP switches (or soldering) on the motherboard
    TT and Falcon were produced with HD drives. But MegaSTE were first produced with
      DD drives then later with HD drives
    Bit 8-13 - Not used or machine dependent
    Bit 14   - Floppy Drive model    0=HD drive  1=DD drive
    Bit 15   - Disable DMA sound     0=disable   1=don't disable

  NOTE [NP] : The DMA is connected to the FDC and its Data Register, each time a DRQ
  is made by the FDC, it's handled by the DMA through its internal 16 bytes buffer.
  This means that in the case of the Atari ST the LOST_DATA bit will never be set
  in the Status Register (but data can be lost if FDC_DMA.SectorCount=0 as there
  will be no transfer between DMA and RAM).
  So, "read sector" and "write sector" type II command will never set LOST_DATA, but
  strangely on a real STF the "read track" type III command will set LOST_DATA when
  it succeeds (maybe it depends on the size of the track ?)
  (eg Super Monaco GP on Superior 65 : loader fails if LOST_DATA is set when
  there're not enough DMA sectors to transfer bytes with read sectors)

  NOTE [NP] : All commands that require to read data from the floppy (verify sequence,
  searching next sector id, ...) will not fail if the drive is OFF or empty. They will
  wait forever until the drive is enabled again or a floppy is inserted ; at this point
  the command will complete as usual, even after several seconds/minutes of delay.

  NOTE [NP] : Although the doc says a new type I/II/III command can't be started while
  the busy bit is set, it's in fact possible to do it under certain conditions. As seen
  in the loader of 'The Overdrive Demos' by Phalanx, the 'restore' command should be
  replaced by a 'seek' command when it occurs in less than 900 cycles.
  A possible explanation from this could be seen in the WD1772's documentation, where
  the specific type I command is in fact checked after the 'prepare + spinup' sequence
  in the state machine diagram.
  Similarly, we can guess that a type II command can be replaced by another type II as long
  as the 'prepare + spinup + head settle' sequence is not over (this was not tested on real HW)

  NOTE [NP] : As verified on a real STF, when reading DMA status at $ff8606 or DMA sector
  count at $ff8604, the unused bits are not set to 0, but they contain the value from the latest
  read/write made at $ff8604 when accessing FDC or HDC registers. Moreover, it's not possible to
  read DMA sector count, so we return the lowest 8 bits from the latest access at $ff8604.


  Cycles and wait states :
  ------------------------
  As verified on a real STF, reading or writing to $ff8604 or $ff8606 can add some 4 cycles
  wait state.
	lea	$ffff8604,a3
	lea	$ffff8606,a4

	move.w  (a4),d0		; dma status			motorola=8	stf=8

	move.w  #$90,(a4)	; dma ctrl sector count		motorola=12	stf=12+4
	move.w  (a3),d0		; read sector count / fdc reg	motorola=8	stf=8
	move.w  #1,(a3)		; write sector count		motorola=12	stf=12+4

	move.w  #$80,(a4)	; dma ctrl status/cmd reg	motorola=12	stf=12+4
	move.w  (a3),d0		; read fdc reg			motorola=8	stf=8+4
	move.w  #$d0,(a3)	; write fdc reg			motorola=12	stf=12+4

  -> All accesses take 4 extra cycles, except reading DMA status and reading DMA sector count
  (which can't really be read, see note above)


  Detecting disk changes :
  ------------------------
  3'1/2 floppy drives include a 'DSKCHG' signal on pin 34 to detect when a disk was changed.
  Unfortunately on ST, this signal is not connected. Nevertheless, it's possible to detect
  a disk was inserted or ejected by looking at the 'WPT' signal which tells if a disk is write
  protected or not (but this method has some limitations and doesn't work in all cases).

  The following values of the WPT signal were measured with a custom program when ejecting/inserting
  a floppy (tested on a 520 STF with a single sided drive and with a double sided drive) :
    - floppy with write protection OFF (write possible), WPT=0 :
	eject   start=0 -> end=1
	insert  start=1 -> end=0
    - floppy with write protection ON (write not possible), WPT=1 :
	eject   start=1 -> end=1
	insert  start=1 -> end=1

  As can be seen, when a disk is write protected (WPT=1), it is not possible to detect the
  transition between inserting and ejecting, WPT will always be 1.

  The TOS monitors the changes on the WPT signal to determine if a floppy was ejected or inserted.
  On TOS 1.02fr, the code is located between $fc1bc4 and $fc1ebc. Every 8 VBL, one floppy drive is checked
  to see if the WPT signal changed. When 1 drive is connected, this means a floppy change should keep the
  WPT signal during at least 8 VBLs. When 2 drive are connected, each drive is checked every 16 VBLs, so
  the WPT signal should be kept for at least 16 VBLs.

  During these transition phases between "ejected" and "inserted", we force the WPT signal to 1,
  depending on which transition we're emulating (see Floppy_DriveTransitionUpdateState()) :
    - Ejecting : WPT will be X, then 1
    - Inserting : WPT will be 1, then X

  NOTE : the TT machines have support for "real" disk change by the mean of the Disk Change (DC)
  signal as output on the pin 34 of the internal floppy drive. The DC signal is then inverted
  and connected to GPIP4 on the TT MFP. See FDC_Drive_Get_DC_signal for more details.
  The DC signal is only available for the internal floppy drive, not the external one.
  Note also that TOS 3 (for TT) doesn't use this signal, it only uses the above method
  monitoring the write protect signal.

*/

/*-----------------------------------------------------------------------*/

/* Status register */
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



/* FDC Emulation commands used in FDC.Command */
enum
{
	FDCEMU_CMD_NULL = 0,
	/* Type I */
	FDCEMU_CMD_RESTORE,
	FDCEMU_CMD_SEEK,
	FDCEMU_CMD_STEP,					/* Also used for STEP IN and STEP OUT */
	/* Type II */
	FDCEMU_CMD_READSECTORS,
	FDCEMU_CMD_WRITESECTORS,
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
	FDCEMU_RUN_RESTORE_SEEKTOTRACKZERO_SPIN_UP,
	FDCEMU_RUN_RESTORE_SEEKTOTRACKZERO_MOTOR_ON,
	FDCEMU_RUN_RESTORE_SEEKTOTRACKZERO_LOOP,
	FDCEMU_RUN_RESTORE_VERIFY,
	FDCEMU_RUN_RESTORE_VERIFY_HEAD_OK,
	FDCEMU_RUN_RESTORE_VERIFY_NEXT_SECTOR_HEADER,
	FDCEMU_RUN_RESTORE_VERIFY_CHECK_SECTOR_HEADER,
	FDCEMU_RUN_RESTORE_COMPLETE,
	/* Seek */
	FDCEMU_RUN_SEEK_TOTRACK,
	FDCEMU_RUN_SEEK_TOTRACK_SPIN_UP,
	FDCEMU_RUN_SEEK_TOTRACK_MOTOR_ON,
	FDCEMU_RUN_SEEK_VERIFY,
	FDCEMU_RUN_SEEK_VERIFY_HEAD_OK,
	FDCEMU_RUN_SEEK_VERIFY_NEXT_SECTOR_HEADER,
	FDCEMU_RUN_SEEK_VERIFY_CHECK_SECTOR_HEADER,
	FDCEMU_RUN_SEEK_COMPLETE,
	/* Step / Step In / Step Out */
	FDCEMU_RUN_STEP_ONCE,
	FDCEMU_RUN_STEP_ONCE_SPIN_UP,
	FDCEMU_RUN_STEP_ONCE_MOTOR_ON,
	FDCEMU_RUN_STEP_VERIFY,
	FDCEMU_RUN_STEP_VERIFY_HEAD_OK,
	FDCEMU_RUN_STEP_VERIFY_NEXT_SECTOR_HEADER,
	FDCEMU_RUN_STEP_VERIFY_CHECK_SECTOR_HEADER,
	FDCEMU_RUN_STEP_COMPLETE,
	/* Read Sector */
	FDCEMU_RUN_READSECTORS_READDATA,
	FDCEMU_RUN_READSECTORS_READDATA_SPIN_UP,
	FDCEMU_RUN_READSECTORS_READDATA_HEAD_LOAD,
	FDCEMU_RUN_READSECTORS_READDATA_MOTOR_ON,
	FDCEMU_RUN_READSECTORS_READDATA_NEXT_SECTOR_HEADER,
	FDCEMU_RUN_READSECTORS_READDATA_CHECK_SECTOR_HEADER,
	FDCEMU_RUN_READSECTORS_READDATA_TRANSFER_START,
	FDCEMU_RUN_READSECTORS_READDATA_TRANSFER_LOOP,
	FDCEMU_RUN_READSECTORS_CRC,
	FDCEMU_RUN_READSECTORS_MULTI,
	FDCEMU_RUN_READSECTORS_RNF,
	FDCEMU_RUN_READSECTORS_COMPLETE,
	/* Write Sector */
	FDCEMU_RUN_WRITESECTORS_WRITEDATA,
	FDCEMU_RUN_WRITESECTORS_WRITEDATA_SPIN_UP,
	FDCEMU_RUN_WRITESECTORS_WRITEDATA_HEAD_LOAD,
	FDCEMU_RUN_WRITESECTORS_WRITEDATA_MOTOR_ON,
	FDCEMU_RUN_WRITESECTORS_WRITEDATA_NEXT_SECTOR_HEADER,
	FDCEMU_RUN_WRITESECTORS_WRITEDATA_CHECK_SECTOR_HEADER,
	FDCEMU_RUN_WRITESECTORS_WRITEDATA_TRANSFER_START,
	FDCEMU_RUN_WRITESECTORS_WRITEDATA_TRANSFER_LOOP,
	FDCEMU_RUN_WRITESECTORS_CRC,
	FDCEMU_RUN_WRITESECTORS_MULTI,
	FDCEMU_RUN_WRITESECTORS_RNF,
	FDCEMU_RUN_WRITESECTORS_COMPLETE,
	/* Read Address */
	FDCEMU_RUN_READADDRESS,
	FDCEMU_RUN_READADDRESS_SPIN_UP,
	FDCEMU_RUN_READADDRESS_HEAD_LOAD,
	FDCEMU_RUN_READADDRESS_MOTOR_ON,
	FDCEMU_RUN_READADDRESS_NEXT_SECTOR_HEADER,
	FDCEMU_RUN_READADDRESS_TRANSFER_START,
	FDCEMU_RUN_READADDRESS_TRANSFER_LOOP,
	FDCEMU_RUN_READADDRESS_RNF,
	FDCEMU_RUN_READADDRESS_COMPLETE,
	/* Read Track */
	FDCEMU_RUN_READTRACK,
	FDCEMU_RUN_READTRACK_SPIN_UP,
	FDCEMU_RUN_READTRACK_HEAD_LOAD,
	FDCEMU_RUN_READTRACK_MOTOR_ON,
	FDCEMU_RUN_READTRACK_INDEX,
	FDCEMU_RUN_READTRACK_TRANSFER_LOOP,
	FDCEMU_RUN_READTRACK_COMPLETE,
	/* Write Track */
	FDCEMU_RUN_WRITETRACK,
	FDCEMU_RUN_WRITETRACK_SPIN_UP,
	FDCEMU_RUN_WRITETRACK_HEAD_LOAD,
	FDCEMU_RUN_WRITETRACK_MOTOR_ON,
	FDCEMU_RUN_WRITETRACK_INDEX,
	FDCEMU_RUN_WRITETRACK_TRANSFER_LOOP,
	FDCEMU_RUN_WRITETRACK_COMPLETE,

	/*  Motor Stop */
	FDCEMU_RUN_MOTOR_STOP,
	FDCEMU_RUN_MOTOR_STOP_WAIT,
	FDCEMU_RUN_MOTOR_STOP_COMPLETE
};



/*
 * Standard hardware values for the FDC. This should allow to get very good timings' emulation
 * when dealing with non protected disks that still require a correct speed (MSA or ST images)
 *
 * - WD1772's datasheet is based on a reference clock of 8 MHz, so delays expressed in milli-seconds
 *   will be slightly different for the Atari ST, whose FDC's clock is around 8.021247 MHz (but this is
 *   not really noticeable in practice, less than 0.3 %)
 * - DD MFM encoding defines a standard signal of 4 micro sec per bit (a possible variation of +/- 10 %
 *   should still be possible). This means the WD1772 will read/write at 250 kbits/sec.
 *   Taking 4 us per bit means 32 us for a full byte, and with a 8 MHz clock, 256 cycles per byte.
 * - The floppy drives used in the Atari ST are spinning at 300 RPM. Variations are possible, as long
 *   as it keeps the duration of an MFM bit in the required 4 us +/- 10 % (in practice, ST drives are often
 *   at 299-301 RPM)
 * - When FDC runs at 8 MHz, the 250 kbits/s and 300 RPM give 6250 bytes for a standard track
 * - When FDC runs at 8.021247 MHz (Atari ST), the 250.664 kbit/s and 300 RPM give 6267 bytes per track
 *
 * Notes on timings required for precise emulation :
 * For a standard floppy recorded with a constant speed, the FDC will take 32 microsec
 * to read/write 1 byte on the floppy. On STF with a 8 MHz CPU clock, this means one byte can be
 * transferred every 256 cpu cycles. So, to get some correct timings as required by some games' protection
 * we must update the emulated FDC's state every 256 cycles (it could be less frequently and still work,
 * due to the 16 bytes DMA FIFO that will transfer data only 16 bytes at a time, every 256*16=4096 cycles)
 */


#define	FDC_CLOCK_STANDARD			(8000000.L)	/* In the WD1772's datasheet, all timings are related to a reference clock of 8 MHz */
#define FDC_DELAY_CYCLE_MFM_BYTE		( 4 * 8 * 8 )	/* 4 us per bit, 8 bits per byte, 8 MHz clock -> 256 cycles */
#define	FDC_BITRATE_STANDARD			250000		/* read/write speed of the WD1772 in bits per sec */
#define	FDC_RPM_STANDARD			300		/* 300 RPM or 5 spins per sec */
//#define	FDC_TRACK_BYTES_STANDARD		( ( FDC_BITRATE_STANDARD / 8 ) / ( FDC_RPM_STANDARD / 60 ) )	/* 6250 bytes */
#define FDC_TRACK_BYTES_STANDARD	6268

#define FDC_TRANSFER_BYTES_US( n )		(  ( n ) * 8 * 1000000.L / FDC_BITRATE_STANDARD )	/* micro sec to read/write 'n' bytes in the WD1772 */

#define	FDC_DELAY_IP_SPIN_UP			6		/* 6 index pulses to reach correct speed during spin up */
#define	FDC_DELAY_IP_MOTOR_OFF			9		/* Turn off motor 9 index pulses after the last command */
#define	FDC_DELAY_IP_ADDRESS_ID			5		/* 5 index pulses max when looking for next sector's address id field */


/* Delays are in micro sec */
#define	FDC_DELAY_US_HEAD_LOAD			( 15 * 1000 )	/* Additional 15 ms delay to load the head in type II/III */

/* Index pulse signal remains high during 3.71 ms on each rotation ([NP] tested on my STF, can vary between 1.5 and 4 ms depending on the drive) */
#define	FDC_DELAY_US_INDEX_PULSE_LENGTH		( 3.71 * 1000 )


/* Internal delays to process commands are in fdc cycles for a 8 MHz clock */
#define	FDC_DELAY_CYCLE_TYPE_I_PREPARE		(90*8)		/* Types I commands take at least 0.09 ms to execute */
								/* (~740 cpu cycles @ 8 Mhz). [NP] : this was measured on a 520 STF */
								/* and avoid returning immediately when command has no effect */
#define	FDC_DELAY_CYCLE_TYPE_II_PREPARE		(1*8) // 65	/* Start Type II commands immediately */
#define	FDC_DELAY_CYCLE_TYPE_III_PREPARE	(1*8)		/* Start Type III commands immediately */
#define	FDC_DELAY_CYCLE_TYPE_IV_PREPARE		(100*8)		/* FIXME [NP] : this was not measured */
#define	FDC_DELAY_CYCLE_COMMAND_COMPLETE	(1*8)		/* Number of cycles before going to the _COMPLETE state (~8 cpu cycles) */
#define	FDC_DELAY_CYCLE_COMMAND_IMMEDIATE	(0)		/* Number of cycles to go immediately to another state */

/* When the drive is switched off or if there's no floppy, some commands will wait forever */
/* as they can't find the next index pulse. Instead of continuously testing if a valid drive */
/* or floppy becomes available (which would slow down emulation), we only test every 50000 FDC cycles, */
/* which shouldn't give any noticeable emulation error */
#define	FDC_DELAY_CYCLE_WAIT_NO_DRIVE_FLOPPY	50000

/* Update the floppy's angular position on a regular basis to detect the index pulses */
#define	FDC_DELAY_CYCLE_REFRESH_INDEX_PULSE	500


#define	FDC_DMA_SECTOR_SIZE			512		/* Sector count at $ff8606 is for 512 bytes blocks */
#define	FDC_DMA_FIFO_SIZE			16		/* DMA transfers blocks of 16 bytes at a time */

#define	FDC_PHYSICAL_MAX_TRACK			90		/* Head can't go beyond 90 tracks */


#define	FDC_STEP_RATE				( FDC.CR & 0x03 )	/* Bits 0 and 1 of the current type I command */

int FDC_StepRate_ms[] = { 6 , 12 , 2 , 3 };			/* Controlled by bits 1 and 0 (r1/r0) in type I commands */




#define	FDC_FAST_FDC_FACTOR			10		/* Divide all delays by this value when --fastfdc is used */

/* Standard ST floppies are double density ; to simulate HD or ED floppies, we use */
/* a density factor to have x2 or x4 more bytes during 1 FDC cycle */
#define	FDC_DENSITY_FACTOR_DD			1
#define	FDC_DENSITY_FACTOR_HD			2		/* For a HD disk, we get x2 bytes than DD */
#define	FDC_DENSITY_FACTOR_ED			4		/* For a ED disk, we get x4 bytes than DD */


#define	FDC_EMULATION_MODE_INTERNAL		1		/* Use fdc.c to handle emulation (ST, MSA, DIM and STX images) */
#define	FDC_EMULATION_MODE_IPF			2		/* Use floppy_ipf.c to handle emulation (IPF, CTR images) */


typedef struct {
	/* WD1772 internal registers */
	uint8_t		DR;					/* Data Register */
	uint8_t		TR;					/* Track Register */
	uint8_t		SR;					/* Sector Register */
	uint8_t		CR;					/* Command Register */
	uint8_t		STR;					/* Status Register */
	int		StepDirection;				/* +1 (Step In) or -1 (Step Out) */

	uint8_t		SideSignal;				/* Side 0 or 1 */
	int		DriveSelSignal;				/* 0 or 1 for drive A or B ; or -1 if no drive selected */
	uint8_t		IRQ_Signal;				/* 0 if IRQ is not set, else value depends on the source of the IRQ */

	uint16_t		DensityMode;				/* bits 0 and 1 of $ff860e */
								/* 0x00 : FDC operates in DD mode */
								/* 0x03 : FDC operates in HD mode */

	/* Other variables */
	int		Command;				/* FDC emulation command currently being executed */
	int		CommandState;				/* Current state for the running command */
	uint8_t		CommandType;				/* Type of latest FDC command (1,2,3 or 4) */
	bool		ReplaceCommandPossible;			/* true if the current command can be replaced by another one (see notes) */

	uint8_t		Status_Temp;				/* Temporary content of the status register */
	bool		StatusTypeI;				/* When true, STR will report the status of a type I command */
	int		IndexPulse_Counter;			/* To count the number of rotations when motor is ON */
	uint8_t		NextSector_ID_Field_TR;			/* Track value in the next ID Field after a call to FDC_NextSectorID_FdcCycles_ST() */
	uint8_t		NextSector_ID_Field_SR;			/* Sector value in the next ID Field after a call to FDC_NextSectorID_FdcCycles_ST() */
	uint8_t		NextSector_ID_Field_LEN;		/* Sector's length in the next ID Field after a call to FDC_NextSectorID_FdcCycles_ST() */
	uint8_t		NextSector_ID_Field_CRC_OK;		/* CRC OK or not in the next ID Field after a call to FDC_NextSectorID_FdcCycles_ST() */
	uint8_t		InterruptCond;				/* For a type IV force interrupt, contains the condition on the lower 4 bits */

	int		EmulationMode;				/* FDC_EMULATION_MODE_INTERNAL or FDC_EMULATION_MODE_IPF */
} FDC_STRUCT;


typedef struct {
	/* DMA internal registers */
	uint16_t		Status;
	uint16_t		Mode;
	uint16_t		SectorCount;
	int16_t		BytesInSector;

	uint8_t		FIFO[ FDC_DMA_FIFO_SIZE ];
	int		FIFO_Size;				/* Between 0 and FDC_DMA_FIFO_SIZE */

	uint32_t		Address;

	uint16_t		ff8604_recent_val;			/* Most recent value read/written at $ff8604 in fdc/hdc mode (bit4=0 in $ff8606) */

	/* Variables to handle our DMA buffer */
	int		PosInBuffer;
	int		PosInBufferTransfer;			/* FIXME REMOVE */
	int		BytesToTransfer;
} FDC_DMA_STRUCT;


typedef struct {
	bool		Enabled;
	bool		DiskInserted;
	int		RPM;					/* Rotation Per Minutes * 1000 */
	int		FloppyDensity;				/* Density of the inserted floppy for current track/side */
								/* 1 for DD (720 kB), 2 for HD (1.4 MB), 4 for ED (2.8 MB) */
	uint8_t		HeadTrack;				/* Current position of the head */
//	uint8_t		Motor;					/* State of the drive's motor : 0=OFF 1=ON */
	uint8_t		NumberOfHeads;				/* 1 for single sided drive, 2 for double sided */
	uint8_t		DiskChange_signal;			/* 0=disk ejected 1=disk inserted and step pulse received */
								/* This signal is available on pin 34 of compatible drives */
								/* and connected to the 2nd MFP of the TT */

	uint64_t		IndexPulse_Time;			/* CyclesGlobalClockCounter value last time we had an index pulse with motor ON */
} FDC_DRIVE_STRUCT;


/**
 * Bytes to transfer with type II/III commands are stored in this buffer
 * which associates a specific delay to each byte. This allows to
 * have a common method to transfer data from ST/MSA disk images (with fixed
 * timing), as well as data from STX disk images (with possible timing variations)
 */
typedef struct {
	int		Size;
	int		PosRead;

	struct {
		uint8_t		Byte;
		uint16_t		Timing;
	} Data [ FDC_TRACK_BYTES_STANDARD*4+1000 ];
} FDC_BUFFER_STRUCT;


static FDC_STRUCT	FDC;					/* All variables related to the WD1772 emulation */
static FDC_DMA_STRUCT	FDC_DMA;				/* All variables related to the DMA transfer */
static FDC_DRIVE_STRUCT	FDC_DRIVES[ MAX_FLOPPYDRIVES ];		/* A: and B: */
static FDC_BUFFER_STRUCT	FDC_BUFFER;			/* Buffer of Timing/Byte to transfer with the FDC */

static uint8_t DMADiskWorkSpace[ FDC_TRACK_BYTES_STANDARD*4+1000 ];/* Workspace used to transfer bytes between floppy and DMA */
								/* It should be large enough to contain a whole track */
								/* We use a x4 factor when we need to simulate HD and ED too */



/*--------------------------------------------------------------*/
/* Local functions prototypes					*/
/*--------------------------------------------------------------*/

static uint32_t	FDC_DelayToFdcCycles ( uint32_t Delay_micro );
static uint32_t	FDC_FdcCyclesToCpuCycles ( uint32_t FdcCycles );
static uint32_t	FDC_CpuCyclesToFdcCycles ( uint32_t CpuCycles );
static void	FDC_StartTimer_FdcCycles ( int FdcCycles , int InternalCycleOffset );
static int	FDC_TransferByte_FdcCycles ( int NbBytes );
static void	FDC_CRC16 ( uint8_t *buf , int nb , uint16_t *pCRC );

static void	FDC_ResetDMA ( void );

static int	FDC_GetEmulationMode ( void );
static void	FDC_Drive_Connect_DC_signal_GPIP ( int Drive );
static void	FDC_Drive_Set_DC_signal ( int Drive , uint8_t val );
static int	FDC_GetSectorsPerTrack ( int Drive , int Track , int Side );
static int	FDC_GetSidesPerDisk ( int Drive , int Track );
static int	FDC_GetTracksPerDisk ( int Drive );
static int	FDC_ComputeFloppyDensity ( uint8_t Drive , uint8_t Track , uint8_t Side );
static void	FDC_UpdateFloppyDensity ( uint8_t Drive , uint8_t Track , uint8_t Side );

static uint32_t	FDC_GetCyclesPerRev_FdcCycles ( int Drive );
static void	FDC_IndexPulse_Update ( void );
static void	FDC_IndexPulse_Init ( int Drive );

static void	FDC_Update_STR ( uint8_t DisableBits , uint8_t EnableBits );
static int	FDC_CmdCompleteCommon ( bool DoInt );
static bool	FDC_VerifyTrack ( void );
static int	FDC_UpdateMotorStop ( void );
static int	FDC_UpdateRestoreCmd ( void );
static int	FDC_UpdateSeekCmd ( void );
static int	FDC_UpdateStepCmd ( void );
static int	FDC_UpdateReadSectorsCmd ( void );
static int	FDC_UpdateWriteSectorsCmd ( void );
static int	FDC_UpdateReadAddressCmd ( void );
static int	FDC_UpdateReadTrackCmd ( void );
static int	FDC_UpdateWriteTrackCmd ( void );

static bool	FDC_Set_MotorON ( uint8_t FDC_CR );
static int	FDC_TypeI_Restore ( void );
static int	FDC_TypeI_Seek ( void );
static int	FDC_TypeI_Step ( void );
static int	FDC_TypeI_StepIn ( void );
static int	FDC_TypeI_StepOut ( void );
static int	FDC_TypeII_ReadSector ( void );
static int	FDC_TypeII_WriteSector(void);
static int	FDC_TypeIII_ReadAddress ( void );
static int	FDC_TypeIII_ReadTrack ( void );
static int	FDC_TypeIII_WriteTrack ( void );
static int	FDC_TypeIV_ForceInterrupt ( void );

static int	FDC_ExecuteTypeICommands ( void );
static int	FDC_ExecuteTypeIICommands ( void );
static int	FDC_ExecuteTypeIIICommands ( void );
static int	FDC_ExecuteTypeIVCommands ( void );
static void	FDC_ExecuteCommand ( void );

static void	FDC_WriteSectorCountRegister ( void );
static void	FDC_WriteCommandRegister ( void );
static void	FDC_WriteTrackRegister ( void );
static void	FDC_WriteSectorRegister ( void );
static void	FDC_WriteDataRegister ( void );

static int	FDC_NextSectorID_FdcCycles_ST ( uint8_t Drive , uint8_t NumberOfHeads , uint8_t Track , uint8_t Side );
static uint8_t	FDC_NextSectorID_TR_ST ( void );
static uint8_t	FDC_NextSectorID_SR_ST ( void );
static uint8_t	FDC_NextSectorID_LEN_ST ( void );
static uint8_t	FDC_NextSectorID_CRC_OK_ST ( void );
static uint8_t	FDC_ReadSector_ST ( uint8_t Drive , uint8_t Track , uint8_t Sector , uint8_t Side , int *pSectorSize );
static uint8_t	FDC_WriteSector_ST ( uint8_t Drive , uint8_t Track , uint8_t Sector , uint8_t Side , int SectorSize );
static uint8_t	FDC_ReadAddress_ST ( uint8_t Drive , uint8_t Track , uint8_t Sector , uint8_t Side );
static uint8_t	FDC_ReadTrack_ST ( uint8_t Drive , uint8_t Track , uint8_t Side );
static uint8_t	FDC_WriteTrack_ST ( uint8_t Drive , uint8_t Track , uint8_t Side , int TrackSize );


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
 */
void	FDC_MemorySnapShot_Capture(bool bSave)
{
	MemorySnapShot_Store(&FDC, sizeof(FDC));
	MemorySnapShot_Store(&FDC_DMA, sizeof(FDC_DMA));
	MemorySnapShot_Store(&FDC_DRIVES, sizeof(FDC_DRIVES));
	MemorySnapShot_Store(&FDC_BUFFER, sizeof(FDC_BUFFER_STRUCT));

	MemorySnapShot_Store(DMADiskWorkSpace, sizeof(DMADiskWorkSpace));
}


/*-----------------------------------------------------------------------*/
/**
 * Change the color of the drive's led color in the statusbar, depending
 * on the state of the busy bit in STR
 */
void	FDC_Drive_Set_BusyLed ( uint8_t STR )
{
//fprintf ( stderr ,"fdc led %d %x\n" , FDC.DriveSelSignal , STR );
	if ( FDC.DriveSelSignal < 0 )
		return;						/* no drive selected */

	if ( STR & FDC_STR_BIT_BUSY )
		Statusbar_SetFloppyLed ( FDC.DriveSelSignal , LED_STATE_ON_BUSY );
	else
		Statusbar_SetFloppyLed ( FDC.DriveSelSignal , LED_STATE_ON );
}


/*-----------------------------------------------------------------------*/
/**
 * Return a small text + length with the current values of the FDC's registers
 * This text is displayed in the statusbar and it looks like :
 * CC:xx HH:TT:SS:s
 *   CC=command in 2 letters
 *   xx=command in hexa
 *   HH=physical head's position
 *   TT=track register
 *   SS=sector register
 *   s=side
 */
int	FDC_Get_Statusbar_Text ( char *text, size_t maxlen )
{
	uint8_t	Command , Head , Track , Sector , Side;
	char	CommandText[ 3 ];
	size_t	written;
	int	Drive;

	Drive = FDC.DriveSelSignal;
	if ( Drive < 0 )					/* If no drive enabled, use drive O for Head */
		Drive = 0;

	if ( FDC_GetEmulationMode() == FDC_EMULATION_MODE_INTERNAL )
	{
		Command = FDC.CR;
		Head	= FDC_DRIVES[ Drive ].HeadTrack;
		Track	= FDC.TR;
		Sector	= FDC.SR;
		Side	= FDC.SideSignal;
	}
	else							/* FDC_EMULATION_MODE_IPF */
	{
		IPF_FDC_StatusBar ( &Command , &Head , &Track , &Sector , &Side );
	}

	if      ( ( Command & 0xf0 ) == 0x00 )	strcpy ( CommandText , "RE" );		/* Restore */
	else if ( ( Command & 0xf0 ) == 0x10 )	strcpy ( CommandText , "SE" );		/* Seek */
	else if ( ( Command & 0xe0 ) == 0x20 )	strcpy ( CommandText , "ST" );		/* Step */
	else if ( ( Command & 0xe0 ) == 0x40 )	strcpy ( CommandText , "SI" );		/* Step In */
	else if ( ( Command & 0xe0 ) == 0x60 )	strcpy ( CommandText , "SO" );		/* Step Out */
	else if ( ( Command & 0xe0 ) == 0x80 )	strcpy ( CommandText , "RS" );		/* Read Sector */
	else if ( ( Command & 0xe0 ) == 0xa0 )	strcpy ( CommandText , "WS" );		/* Write Sector */
	else if ( ( Command & 0xf0 ) == 0xc0 )	strcpy ( CommandText , "RA" );		/* Read Address */
	else if ( ( Command & 0xf0 ) == 0xe0 )	strcpy ( CommandText , "RT" );		/* Read Track */
	else if ( ( Command & 0xf0 ) == 0xf0 )	strcpy ( CommandText , "WT" );		/* Write Track */
	else					strcpy ( CommandText , "FI" );		/* Force Int */

	written = snprintf ( text, maxlen, "%s:%02X %02X:%02X:%02X:%d" , CommandText , Command , Head , Track , Sector , Side );
	assert(written < maxlen); /* more space needs to be allocated */
	return written;
}


/*-----------------------------------------------------------------------*/
/**
 * Convert a delay in micro seconds to its equivalent of fdc cycles
 * (delays in the WD1772 specs are relative to a 8 MHz reference clock)
 */
static uint32_t	FDC_DelayToFdcCycles ( uint32_t Delay_micro )
{
	uint32_t	FdcCycles;

	FdcCycles = (uint32_t) ( ( (uint64_t) FDC_CLOCK_STANDARD * Delay_micro ) / 1000000 );

//fprintf ( stderr , "fdc state %d delay %d us %d fdc cycles\n" , FDC.Command , Delay_micro , FdcCycles );
	return FdcCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Convert a number of fdc cycles at freq MachineClocks.FDC_Freq to a number
 * of cpu cycles at freq MachineClocks.CPU_Freq
 * TODO : we use a fixed 8 MHz clock to convert cycles for our internal timers
 * in cycInt.c. This should be replaced some days by using MachineClocks.CPU_Freq.
 * (for Falcon, we multiply cycles by 2 to simulate a freq in the 8 MHz range)
 */
static uint32_t	FDC_FdcCyclesToCpuCycles ( uint32_t FdcCycles )
{
	uint32_t	CpuCycles;

	/* Our conversion expects FDC_Freq to be nearly the same as CPU_Freq (8 Mhz) */
	/* but the Falcon uses a 16 MHz clock for the Ajax FDC */
	/* FIXME : as stated above, this should be handled better, without involving 8 MHz CPU_Freq */
	if (Config_IsMachineFalcon())
		FdcCycles *= 2;					/* correct delays for a 8 MHz FDC_Freq clock instead of 16 */

//	CpuCycles = rint ( ( (uint64_t)FdcCycles * MachineClocks.CPU_Freq ) / MachineClocks.FDC_Freq );
	CpuCycles = rint ( ( (uint64_t)FdcCycles * 8021247.L ) / MachineClocks.FDC_Freq );
	CpuCycles <<= nCpuFreqShift;				/* Convert to x1 or x2 or x4 cpu speed */

//fprintf ( stderr , "fdc command %d machine %d fdc cycles %d cpu cycles %d\n" , FDC.Command , ConfigureParams.System.nMachineType , FdcCycles , CpuCycles );
//if ( Delay==4104) Delay=4166;		// 4166 : decade demo
	return CpuCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Convert a number of cpu cycles at freq MachineClocks.CPU_Freq to a number
 * of fdc cycles at freq MachineClocks.FDC_Freq (this is the opposite
 * of FDC_FdcCyclesToCpuCycles)
 * TODO : we use a fixed 8 MHz clock to convert cycles for our internal timers
 * in cycInt.c. This should be replaced some days by using MachineClocks.CPU_Freq.
 */
static uint32_t	FDC_CpuCyclesToFdcCycles ( uint32_t CpuCycles )
{
	int	FdcCycles;


	CpuCycles >>= nCpuFreqShift;				/* Compensate for x2 or x4 cpu speed */

//	FdcCycles = rint ( ( (uint64_t)CpuCycles * MachineClocks.FDC_Freq ) / MachineClocks.CPU_Freq );
	FdcCycles = rint ( ( (uint64_t)CpuCycles * MachineClocks.FDC_Freq ) / 8021247.L );

	/* Our conversion expects FDC_Freq to be nearly the same as CPU_Freq (8 Mhz) */
	/* but the Falcon uses a 16 MHz clock for the Ajax FDC */
	/* FIXME : as stated above, this should be handled better, without involving 8 MHz CPU_Freq */
	if (Config_IsMachineFalcon())
		FdcCycles /= 2;					/* correct delays for a 8 MHz FDC_Freq clock instead of 16 */

//fprintf ( stderr , "fdc state %d delay %d cpu cycles %d fdc cycles\n" , FDC.Command , CpuCycles , FdcCycles );
	return FdcCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Start an internal timer to handle the FDC's events.
 * If "fast floppy" mode is used, we speed up the timer by dividing
 * the number of cycles by a fixed number.
 */
static void	FDC_StartTimer_FdcCycles ( int FdcCycles , int InternalCycleOffset )
{
//fprintf ( stderr , "fdc start timer %d cycles\n" , FdcCycles );

	if ( ( ConfigureParams.DiskImage.FastFloppy ) && ( FdcCycles > FDC_FAST_FDC_FACTOR ) )
		FdcCycles /= FDC_FAST_FDC_FACTOR;

	CycInt_AddRelativeInterruptWithOffset ( FDC_FdcCyclesToCpuCycles ( FdcCycles ) , INT_CPU_CYCLE , INTERRUPT_FDC , InternalCycleOffset );
}


/*-----------------------------------------------------------------------*/
/**
 * Return the number of FDC cycles required to read/write 'nb' bytes
 * This function will always be called when FDC.DriveSelSignal >= 0, as
 * there's no case where we transfer bytes if no drive is enabled.
 * We take "FloppyDensity" into account to simulate faster transfer for HD/ED floppies.
 *
 * 2015/10/23 [NP] As seen in the 'Bird Mad Girl Show' demo, it's possible to get
 * FDC.DriveSelSignal < 0 once a transfer was started (for example, read sector
 * will complete successfully). So we use DD by default in that case.
 */
static int	FDC_TransferByte_FdcCycles ( int NbBytes )
{
//fprintf ( stderr , "fdc state %d transfer %d bytes\n" , FDC.Command , NbBytes );
	if ( FDC.DriveSelSignal < 0 )
	{
		/* Drive was unselected during the transfer : assume DD for the rest of the bytes */
		return ( NbBytes * FDC_DELAY_CYCLE_MFM_BYTE ) / FDC_DENSITY_FACTOR_DD;
	}

	return ( NbBytes * FDC_DELAY_CYCLE_MFM_BYTE ) / FDC_GetFloppyDensity ( FDC.DriveSelSignal );
}


/*-----------------------------------------------------------------------*/
/**
 * Compute the CRC16 of 'nb' bytes stored in 'buf'.
 */
static void FDC_CRC16 ( uint8_t *buf , int nb , uint16_t *pCRC )
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
 * Init variables used in FDC and DMA emulation
 */
void FDC_Init ( void )
{
	int	i;

        LOG_TRACE ( TRACE_FDC , "fdc init\n" );

	for ( i=0 ; i<MAX_FLOPPYDRIVES ; i++ )
	{
		FDC_DRIVES[ i ].Enabled = true;
		FDC_DRIVES[ i ].DiskInserted = false;
		FDC_DRIVES[ i ].RPM = FDC_RPM_STANDARD * 1000;
		FDC_DRIVES[ i ].FloppyDensity = FDC_DENSITY_FACTOR_DD;
		FDC_DRIVES[ i ].HeadTrack = 0;			/* Set all drives to track 0 */
		FDC_DRIVES[ i ].NumberOfHeads = 2;		/* Double sided drive */
		FDC_DRIVES[ i ].IndexPulse_Time = 0;
		FDC_Drive_Set_DC_signal ( i , 0 );
	}

	FDC_Buffer_Reset();

	FDC.EmulationMode = FDC_EMULATION_MODE_INTERNAL;
}


/*-----------------------------------------------------------------------*/
/**
 * Reset variables used in FDC and DMA emulation
 */

/* This function is called after a hardware reset of the FDC.
 * Cold reset is when the computer is turned off/on.
 * Warm reset is when the reset button is pressed or the 68000
 * RESET instruction is used.
 * On warm reset, TR and DR should not be reset.
 * STR is set to 0 and SR is set to 1 (verified on a real STF)
 */
void FDC_Reset ( bool bCold )
{
	int	i;

	LOG_TRACE ( TRACE_FDC , "fdc reset mode=%s\n" , bCold?"cold":"warm" );

	/* Clear out FDC registers */
	FDC.CR = 0;
	FDC.STR = 0;
	FDC.SR = 1;
	FDC.StatusTypeI = false;

	/* On cold reset, TR and DR should be reset */
	/* On warm reset, TR and DR value should be kept */
	if ( bCold )
	{
		FDC.TR = 0;
		FDC.DR = 0;
		FDC_DMA.ff8604_recent_val = 0;		/* Only set to 0 on cold reset */
	}
	FDC.StepDirection = 1;

	FDC.Command = FDCEMU_CMD_NULL;			/* FDC emulation command currently being executed */
	FDC.CommandState = FDCEMU_RUN_NULL;
	FDC.CommandType = 0;
	FDC.InterruptCond = 0;
	FDC.IRQ_Signal = 0;
	FDC_ClearIRQ();					/* Propagate IRQ signal to MFP GPIP5 */

	FDC.IndexPulse_Counter = 0;
	for ( i=0 ; i<MAX_FLOPPYDRIVES ; i++ )
	{
		FDC_DRIVES[ i ].IndexPulse_Time = 0;	/* Current IP's locations are lost after a reset (motor is now OFF) */
		FDC_Drive_Set_DC_signal ( i , 0 );
	}

	FDC_DMA.Status = 1;				/* no DMA error and SectorCount=0 */
	FDC_DMA.Mode = 0;

	FDC_ResetDMA();

	FDC_Buffer_Reset();

	/* Also reset IPF emulation */
	IPF_Reset( bCold );
}


/*-----------------------------------------------------------------------*/
/**
 * Reset DMA (clear internal 16 bytes buffer)
 *
 * This is done by 'toggling' bit 8 of the DMA Mode Control register
 * This will empty the FIFOs and reset Sector Count to 0
 */
static void FDC_ResetDMA ( void )
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
	LOG_TRACE(TRACE_FDC, "fdc reset dma VBL=%d video_cyc=%d %d@%d pc=%x\n",
		nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Empty FIFO */
	FDC_DMA.FIFO_Size = 0;

	/* Reset bytes count for current DMA sector */
	FDC_DMA.BytesInSector = FDC_DMA_SECTOR_SIZE;
	FDC_DMA.SectorCount = 0;			/* After a reset, sector count is 0 (verified on a real STF) */

	/* Reset internal variables used to handle DMA transfer */
	FDC_DMA.PosInBuffer = 0;
	FDC_DMA.PosInBufferTransfer = 0;
	FDC_DMA.BytesToTransfer = 0;

	/* Reset HDC command status */
	HDC_ResetCommandStatus();
}


/*-----------------------------------------------------------------------*/
/**
 * Set DMA Status at $ff8606
 *
 * Bit 0 - Error Status (0=Error 1=No error)
 * Bit 1 - Sector Count Zero Status (0=Sector Count Zero)
 * Bit 2 - Data Request signal from the FDC
 */
void FDC_SetDMAStatus ( bool bError )
{
	/* Set error bit */
	if (!bError)
		FDC_DMA.Status |= 0x1;					/* No Error, set bit 0 */
	else
		FDC_DMA.Status &= ~0x1;					/* Error, clear bit 0 */
}


/*-----------------------------------------------------------------------*/
/**
 * Return the value of bit 8 in the FDC's DMA mode control register.
 * 0=dma read  0x100=dma write
 */
int	FDC_DMA_GetModeControl_R_WR ( void )
{
	return FDC_DMA.Mode & 0x100;
}

int FDC_DMA_GetMode(void)
{
	return FDC_DMA.Mode;
}


/*-----------------------------------------------------------------------*/
/**
 * Add a byte to the DMA's FIFO buffer (read from disk).
 * If the buffer is full and DMA is ON, write the FIFO's 16 bytes to DMA's address.
 *
 * NOTE [NP] : the DMA is connected to the FDC, each time a DRQ is made by the FDC,
 * it's handled by the DMA and stored in the DMA's 16 bytes buffer. This means
 * FDC_STR_BIT_LOST_DATA will never be set (but data can be lost if FDC_DMA.SectorCount==0)
 *
 * NOTE [NP] : as seen on a real STF, the unused bits when reading DMA Status at $ff8606
 * are also changed by the DMA operations (this might not be complete, but seems quite reproducible) :
 *  - reading a byte from the FDC to the DMA will change unused bits in the lowest byte at $ff8604
 *  - transferring the 16 byte DMA buffer to RAM will change the unused bits in the 2 bytes at $ff8604
 *
 * In all cases, the byte read from the FDC is transferred to the DMA, even if DMA sector count is 0, so
 * we must always update lowest byte of ff8604_recent_val.
 * DMA FIFO is transferred only when DMA sector count is >0, so high byte of ff8604_recent_val will be
 * updated only in that case.
 */
void	FDC_DMA_FIFO_Push ( uint8_t Byte )
{
	uint32_t	Address;

//fprintf ( stderr , "dma push pos=%d byte=%x %lld\n" , FDC_DMA.FIFO_Size , Byte , CyclesGlobalClockCounter );

	/* Store the byte that was just read from FDC's Data Register */
	FDC_DMA.ff8604_recent_val = ( FDC_DMA.ff8604_recent_val & 0xff00 ) | Byte;

	if ( FDC_DMA.SectorCount == 0 )
	{
		//FDC_Update_STR ( 0 , FDC_STR_BIT_LOST_DATA );		/* If DMA is OFF, data are lost -> Not on the ST */
		FDC_SetDMAStatus ( true );				/* Set DMA error (bit 0) */
		return;
	}

	FDC_SetDMAStatus ( false );					/* No DMA error (bit 0) */

	FDC_DMA.FIFO [ FDC_DMA.FIFO_Size++ ] = Byte;

	if ( FDC_DMA.FIFO_Size < FDC_DMA_FIFO_SIZE )			/* FIFO is not full yet */
		return;

	/* FIFO full : transfer data from FIFO to RAM and update DMA address */
	Address = FDC_GetDMAAddress();
	STMemory_SafeCopy ( Address , FDC_DMA.FIFO , FDC_DMA_FIFO_SIZE , "FDC DMA push to fifo" );
	FDC_WriteDMAAddress ( Address + FDC_DMA_FIFO_SIZE );
	FDC_DMA.FIFO_Size = 0;						/* FIFO is now empty again */
	/* When the FIFO transfers data to RAM it takes 4 cycles per word and the CPU is stalled during this time */
	M68000_AddCycles_CE ( 4 * FDC_DMA_FIFO_SIZE / 2 );		/* 32 cycles */

	/* Store the last word that was just transferred by the DMA */
	FDC_DMA.ff8604_recent_val = ( FDC_DMA.FIFO [ FDC_DMA_FIFO_SIZE-2 ] << 8 ) | FDC_DMA.FIFO [ FDC_DMA_FIFO_SIZE-1 ];

	/* Update Sector Count */
	FDC_DMA.BytesInSector -= FDC_DMA_FIFO_SIZE;
	if ( FDC_DMA.BytesInSector <= 0 )
	{
		FDC_DMA.SectorCount--;
		FDC_DMA.BytesInSector = FDC_DMA_SECTOR_SIZE;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Get a byte from the DMA's FIFO buffer (write to disk).
 * If the buffer is empty and DMA is ON, load 16 bytes in the FIFO from DMA's address.
 *
 * NOTE [NP] : on a real ST, there're two 16 byte DMA FIFO, this allows to refill one FIFO
 * while the other FIFO is used to transfer data to the FDC. We don't emulate this at the
 * moment, as it doesn't cause any problem (when a DMA is set to write mode, we would need
 * to prefill 32 bytes instead of 16 bytes as we do now)
 *
 * NOTE [NP] : as with FDC_DMA_FIFO_Push, this also changes the unused bits at $ff8606
 */
uint8_t	FDC_DMA_FIFO_Pull ( void )
{
	uint32_t	Address;
	uint8_t	Byte;

//fprintf ( stderr , "fifo pull %d %d %d\n" , FDC_DMA.BytesToTransfer , FDC_DMA.BytesInSector , FDC_DMA.SectorCount );
	if ( FDC_DMA.SectorCount == 0 )
	{
		//FDC_Update_STR ( 0 , FDC_STR_BIT_LOST_DATA );		/* If DMA is OFF, data are lost -> Not on the ST */
		FDC_SetDMAStatus ( true );				/* Set DMA error (bit 0) */
		return 0;						/* Write a '0' byte when dma is off */
	}

	FDC_SetDMAStatus ( false );					/* No DMA error (bit 0) */

	if ( FDC_DMA.FIFO_Size > 0 )					/* FIFO is not empty yet */
		Byte = FDC_DMA.FIFO [ FDC_DMA_FIFO_SIZE - ( FDC_DMA.FIFO_Size-- ) ];	/* return byte at pos 0, 1, .., 15 */

	else
	{
		/* FIFO empty : transfer data from RAM to FIFO and update DMA address */
		Address = FDC_GetDMAAddress();
		memcpy ( FDC_DMA.FIFO , &STRam[ Address ] , FDC_DMA_FIFO_SIZE );/* TODO : check we read from a valid RAM location ? */
		FDC_WriteDMAAddress ( Address + FDC_DMA_FIFO_SIZE );
		FDC_DMA.FIFO_Size = FDC_DMA_FIFO_SIZE - 1;			/* FIFO is now full again (minus the byte we will return below) */
		/* When the FIFO reads data from RAM it takes 4 cycles per word and the CPU is stalled during this time */
		M68000_AddCycles_CE ( 4 * FDC_DMA_FIFO_SIZE / 2 );		/* 32 cycles */

		/* Store the last word that was just transferred by the DMA */
		FDC_DMA.ff8604_recent_val = ( FDC_DMA.FIFO [ FDC_DMA_FIFO_SIZE-2 ] << 8 ) | FDC_DMA.FIFO [ FDC_DMA_FIFO_SIZE-1 ];

		/* Update Sector Count */
		FDC_DMA.BytesInSector -= FDC_DMA_FIFO_SIZE;
		if ( FDC_DMA.BytesInSector < 0 )
		{
			FDC_DMA.SectorCount--;
			FDC_DMA.BytesInSector = FDC_DMA_SECTOR_SIZE;
		}

		Byte = FDC_DMA.FIFO [ 0 ];				/* return the 1st byte we just transferred in the FIFO */
	}

	/* Store the byte that will be written to FDC's Data Register */
	FDC_DMA.ff8604_recent_val = ( FDC_DMA.ff8604_recent_val & 0xff00 ) | Byte;

	return Byte;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the value of FDC_DMA.SectorCount (used in floppy_ipf.c)
 */
int	FDC_DMA_GetSectorCount ( void )
{
	return FDC_DMA.SectorCount;
}


/*-----------------------------------------------------------------------*/
/**
 * Reset the buffer used to transfer data between the FDC and the DMA
 */
void	FDC_Buffer_Reset ( void )
{
	FDC_BUFFER.Size = 0;
	FDC_BUFFER.PosRead = 0;	
}


/*-----------------------------------------------------------------------*/
/**
 * Add a byte to the FDC transfer buffer, using a specific timing
 */
void	FDC_Buffer_Add_Timing ( uint8_t Byte , uint16_t Timing )
{
	FDC_BUFFER.Data[ FDC_BUFFER.Size ].Byte = Byte;
	FDC_BUFFER.Data[ FDC_BUFFER.Size ].Timing = Timing;
	FDC_BUFFER.Size++;
}


/*-----------------------------------------------------------------------*/
/**
 * Add a byte to the FDC transfer buffer, using a default timing
 */
void	FDC_Buffer_Add ( uint8_t Byte )
{
	FDC_Buffer_Add_Timing ( Byte , FDC_TransferByte_FdcCycles ( 1 ) );
}


/*-----------------------------------------------------------------------*/
/**
 * Return the timing needed to transfer the Byte at the current position
 */
uint16_t	FDC_Buffer_Read_Timing ( void )
{
//fprintf ( stderr , "read timing %d %x\n" , FDC_BUFFER.PosRead , FDC_BUFFER.Data[ FDC_BUFFER.PosRead ].Timing );
	return FDC_BUFFER.Data[ FDC_BUFFER.PosRead ].Timing;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the Byte at the current position and increment position
 */
uint8_t	FDC_Buffer_Read_Byte ( void )
{
//fprintf ( stderr , "read byte %d %x\n" , FDC_BUFFER.PosRead , FDC_BUFFER.Data[ FDC_BUFFER.PosRead ].Byte );
	return FDC_BUFFER.Data[ FDC_BUFFER.PosRead++ ].Byte;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the Byte at a given position
 */
uint8_t	FDC_Buffer_Read_Byte_pos ( int pos )
{
//fprintf ( stderr , "read byte pos %d %x\n" , pos , FDC_BUFFER.Data[ pos ].Byte );
	return FDC_BUFFER.Data[ pos ].Byte;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the number of bytes stored in FDC_BUFFER
 */
int	FDC_Buffer_Get_Size ( void )
{
	return FDC_BUFFER.Size;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the mode to handle a read/write in $ff86xx
 * Depending on the images inserted in each floppy drive and on the
 * selected drive, we must choose which fdc emulation should be used.
 * Possible emulation methods are 'internal' or 'ipf'.
 * To avoid mixing emulation methods on both drives when possible (which
 * could lead to inconstancies when precise timings are required), we also
 * use the IPF mode for an empty drive if the other drive contains an IPF
 * image.
 * If no drive is selected, we must use the previous mode (before drives were
 * unselected), not the internal one : in case some commands are sent when
 * drives are deselected and the drive was in IPF mode, we must send
 * the command to IPF to ensure no command are lost if the drive is selected again
 * (eg : D0 command in "Saint Dragon" IPF)
 */
static int FDC_GetEmulationMode ( void )
{
	int	Mode;

	Mode = FDC.EmulationMode;				/* Default to previous mode if no drive is selected */

	/* Check drive 1 first */
	if ( ( PSGRegisters[PSG_REG_IO_PORTA] & 0x04 ) == 0 )
	{
		if ( EmulationDrives[ 1 ].ImageType == FLOPPY_IMAGE_TYPE_IPF )
			Mode = FDC_EMULATION_MODE_IPF;
		else if ( ( EmulationDrives[ 1 ].ImageType == FLOPPY_IMAGE_TYPE_NONE )		/* Drive 1 is empty */
			&& ( EmulationDrives[ 0 ].ImageType == FLOPPY_IMAGE_TYPE_IPF ) )	/* Drive 0 is an IPF image */
			Mode = FDC_EMULATION_MODE_IPF;						/* Use IPF for drive 1 too */
		else
			Mode = FDC_EMULATION_MODE_INTERNAL;
	}

	/* If both drive 0 and drive 1 are enabled, we keep only drive 0 to choose emulation's mode */
	if ( ( PSGRegisters[PSG_REG_IO_PORTA] & 0x02 ) == 0 )
	{
		if ( EmulationDrives[ 0 ].ImageType == FLOPPY_IMAGE_TYPE_IPF )
			Mode = FDC_EMULATION_MODE_IPF;
		else if ( ( EmulationDrives[ 0 ].ImageType == FLOPPY_IMAGE_TYPE_NONE )		/* Drive 0 is empty */
			&& ( EmulationDrives[ 1 ].ImageType == FLOPPY_IMAGE_TYPE_IPF ) )	/* Drive 1 is an IPF image */
			Mode = FDC_EMULATION_MODE_IPF;						/* Use IPF for drive 0 too */
		else
			Mode = FDC_EMULATION_MODE_INTERNAL;
	}

	FDC.EmulationMode = Mode;
//fprintf ( stderr , "emul mode %x %d\n" , PSGRegisters[PSG_REG_IO_PORTA] & 0x06 , Mode );
//	return FDC_EMULATION_MODE_INTERNAL;
//	return FDC_EMULATION_MODE_IPF;
	return Mode;
}


/*-----------------------------------------------------------------------*/
/**
 * Update the FDC's internal variables on a regular basis.
 * To get correct accuracy, this should be called every 200-500 FDC cycles
 * So far, we only need to update the index position for the valid
 * drive/floppy ; updating every 500 cycles is enough for this case.
 */
static void FDC_UpdateAll(void)
{
	FDC_IndexPulse_Update ();
}


/*-----------------------------------------------------------------------*/
/**
 * This function is used to enable/disable a drive when
 * using the UI or command line parameters
 */
void	FDC_Drive_Set_Enable ( int Drive , bool value )
{
	LOG_TRACE ( TRACE_FDC , "fdc enable drive=%d %s\n" , Drive , value?"on":"off" );

	if ( ( Drive >= 0 ) && ( Drive < MAX_FLOPPYDRIVES ) )
		FDC_DRIVES[ Drive ].Enabled = value;

	/* Also forward change to IPF emulation */
	IPF_Drive_Set_Enable ( Drive , value );
}


/*-----------------------------------------------------------------------*/
/**
 * This function is used to choose single sided or double sided for a drive
 * when using the UI or command line parameters
 */
void	FDC_Drive_Set_NumberOfHeads ( int Drive , int NbrHeads )
{
	LOG_TRACE ( TRACE_FDC , "fdc set nbr heads drive=%d %d\n" , Drive , NbrHeads );

	if ( ( Drive >= 0 ) && ( Drive < MAX_FLOPPYDRIVES ) )
		FDC_DRIVES[ Drive ].NumberOfHeads = NbrHeads;

	/* Also forward change to IPF emulation */
	IPF_Drive_Set_DoubleSided ( Drive , NbrHeads==2 ? true : false );
}


/*-----------------------------------------------------------------------*/
/**
 * Get the value of the Disk Change (DC) signal available on pin 34 of some
 * floppy drives (used in TT emulation) and update the TT GPIP register.
 *
 * This signal is active low unless a disk is inserted and a STEP pulse is received
 * and the drive is selected :
 *  0 = drive is selected and a disk was ejected (ie drive is empty)
 *  1 = drive is not selected or a disk was inserted and a step pulse received
 *
 * DC signal was only available on the TT machines and in that case it's connected
 * to the TT MFP on GPIP4 (the signal is inverted before going to GPIP4)
 */
static void	FDC_Drive_Connect_DC_signal_GPIP ( int Drive )
{
	uint8_t	state;

	if ( FDC.DriveSelSignal != Drive )
		state = 1;						/* drive is not selected */
	else
		state = FDC_DRIVES[ Drive ].DiskChange_signal;

	/* DC signal is inverted before going into GPIP4 */
	if ( state == 1 )
		state = MFP_GPIP_STATE_LOW;
	else
		state = MFP_GPIP_STATE_HIGH;

	MFP_GPIP_Set_Line_Input ( pMFP_TT , MFP_TT_GPIP_LINE_DC , state );
}


/*-----------------------------------------------------------------------*/
/**
 * Update the DC signal for a drive and update GPIP for TT machines
 *  - set DC=0 when disk is ejected
 *  - set DC=1 when a step pulse (Type I command) is received and a floppy
 *    is inserted and the drive is selected
 */
static void	FDC_Drive_Set_DC_signal ( int Drive , uint8_t val )
{
	FDC_DRIVES[ Drive ].DiskChange_signal = val;

	if ( Config_IsMachineTT() && ( Drive == 0 ) )
		FDC_Drive_Connect_DC_signal_GPIP ( Drive );
}


/*-----------------------------------------------------------------------*/
/**
 * This function is called when a floppy is inserted in a drive
 * using the UI or command line parameters
 */
void	FDC_InsertFloppy ( int Drive )
{
	LOG_TRACE ( TRACE_FDC , "fdc insert drive=%d\n" , Drive );

	if ( ( Drive >= 0 ) && ( Drive < MAX_FLOPPYDRIVES ) )
	{
		FDC_DRIVES[ Drive ].DiskInserted = true;
		if ( ( FDC.STR & FDC_STR_BIT_MOTOR_ON ) != 0 )		/* If we insert a floppy while motor is already on, we must */
			FDC_IndexPulse_Init ( Drive );			/* init the index pulse's position */
		else
			FDC_DRIVES[ Drive ].IndexPulse_Time = 0;	/* Index pulse's position not known yet */

		/* Update floppy's density for this drive */
		FDC_UpdateFloppyDensity ( Drive , FDC_DRIVES[ Drive ].HeadTrack , FDC.SideSignal );
	}
}


/*-----------------------------------------------------------------------*/
/**
 * This function is called when a floppy is ejected from a drive
 * using the UI or command line parameters
 */
void	FDC_EjectFloppy ( int Drive )
{
	LOG_TRACE ( TRACE_FDC , "fdc eject drive=%d\n" , Drive );

	if ( ( Drive >= 0 ) && ( Drive < MAX_FLOPPYDRIVES ) )
	{
		FDC_DRIVES[ Drive ].DiskInserted = false;
		FDC_DRIVES[ Drive ].IndexPulse_Time = 0;		/* Stop counting index pulses on an empty drive */

		/* Set the Disk Change signal to "ejected" */
		FDC_Drive_Set_DC_signal ( Drive , FDC_DC_SIGNAL_EJECTED );
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Handle a write in the IO_PORTA register $E through $ff8802. Only bits
 * 0-2 are available here, others are masked to 0.
 * bit 0 : side select
 * bit 1-2 : drive select
 *
 * - For internal FDC emulation, we init index pulse if the active drive
 *   changed
 * - We also forward the change to IPF emulation, as it doesn't have direct access
 *   to this IO_PORTA register.
 *
 * If both drives are selected, we keep only drive 0
 */
void	FDC_SetDriveSide ( uint8_t io_porta_old , uint8_t io_porta_new )
{
	int	Side;
	int	Drive;

	if ( io_porta_old == io_porta_new )
		return;							/* No change */

	Side = ( (~io_porta_new) & 0x01 );				/* Side 0 or 1 */

	Drive = -1;							/* By default, don't select any drive */

	/* Check drive 1 first */
	if ( ( io_porta_new & 0x04 ) == 0 )
		Drive = 1;						/* Select drive 1 */

	/* If both drive 0 and drive 1 are enabled, we keep only drive 0 as newdrive */
	if ( ( io_porta_new & 0x02 ) == 0 )
		Drive = 0;						/* Select drive 0 (and un-select drive 1 if set above) */

	LOG_TRACE(TRACE_FDC, "fdc change drive/side io_porta_old=0x%x io_porta_new=0x%x side %d->%d drive %d->%d VBL=%d HBL=%d\n" ,
		  io_porta_old , io_porta_new , FDC.SideSignal , Side , FDC.DriveSelSignal , Drive , nVBLs , nHBL );

	if ( FDC.DriveSelSignal != Drive )
	{
		if ( FDC.DriveSelSignal >= 0 )					/* A drive was previously enabled */
			FDC_DRIVES[ FDC.DriveSelSignal ].IndexPulse_Time = 0;	/* Stop counting index pulses on the previous drive */

		if ( Drive >= 0 )						/* A new drive is enabled */
		{
			if ( ( FDC_DRIVES[ Drive ].DiskInserted )		/* If there's a disk in the new drive and motor is already */
			  && ( ( FDC.STR & FDC_STR_BIT_MOTOR_ON ) != 0 ) )	/* on, we must init the index pulse's position */
				FDC_IndexPulse_Init ( Drive );
			else
				FDC_DRIVES[ Drive ].IndexPulse_Time = 0;	/* Index pulse's position not known yet */
		}
	}

	FDC.SideSignal = Side;
	FDC.DriveSelSignal = Drive;

	/* Update floppy's density for this drive */
	if ( Drive >= 0 )
		FDC_UpdateFloppyDensity ( Drive , FDC_DRIVES[ Drive ].HeadTrack , FDC.SideSignal );

	/* Also forward change to IPF emulation */
	IPF_SetDriveSide ( io_porta_old , io_porta_new );
}


/*-----------------------------------------------------------------------*/
/**
 * Return the number of sectors for track/side for the current floppy in a drive
 * TODO [NP] : this function calls Floppy_FindDiskDetails which handles only ST/MSA
 * disk images so far, so this implies all tracks have in fact the same number
 * of sectors (we don't use Track and Side for now)
 * Drive should be a valid drive (0 or 1)
 */
static int FDC_GetSectorsPerTrack ( int Drive , int Track , int Side )
{
	uint16_t	SectorsPerTrack;

	if (EmulationDrives[ Drive ].bDiskInserted)
	{
		Floppy_FindDiskDetails ( EmulationDrives[ Drive ].pBuffer , EmulationDrives[ Drive ].nImageBytes , &SectorsPerTrack , NULL );
		return SectorsPerTrack;
	}
	else
		return 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the number of sides for a track for the current floppy in a drive
 * Drive should be a valid drive (0 or 1)
 */
static int FDC_GetSidesPerDisk ( int Drive , int Track )
{
	uint16_t	SidesPerDisk;

	if (EmulationDrives[ Drive ].bDiskInserted)
	{
		Floppy_FindDiskDetails ( EmulationDrives[ Drive ].pBuffer , EmulationDrives[ Drive ].nImageBytes , NULL , &SidesPerDisk );
		return SidesPerDisk;					/* 1 or 2 */
	}
	else
		return 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the number of tracks for the current floppy in a drive
 * For ST/MSA, this assumes both sides have the same number of tracks
 * Drive should be a valid drive (0 or 1)
 */
static int FDC_GetTracksPerDisk ( int Drive )
{
	uint16_t	SectorsPerTrack;
	uint16_t	SidesPerDisk;

	if (EmulationDrives[ Drive ].bDiskInserted)
	{
		Floppy_FindDiskDetails ( EmulationDrives[ Drive ].pBuffer , EmulationDrives[ Drive ].nImageBytes , &SectorsPerTrack , &SidesPerDisk );
		return ( ( EmulationDrives[Drive].nImageBytes / NUMBYTESPERSECTOR ) / SectorsPerTrack ) / SidesPerDisk;
	}
	else
		return 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the number of bytes in a raw track
 * For ST/MSA disk images, we consider all the tracks have the same size.
 * To simulate HD/ED floppies, we multiply the size by a density factor (x2 or x4).
 * Drive should be a valid drive (0 or 1)
 */
int	FDC_GetBytesPerTrack ( uint8_t Drive , uint8_t Track , uint8_t Side )
{
	int	SectorsPerTrack;
	int	TrackSize;

	if ( EmulationDrives[ Drive ].bDiskInserted )
	{
		/* If the inserted disk is an STX, we use the supplied track length */
		if ( EmulationDrives[Drive].ImageType == FLOPPY_IMAGE_TYPE_STX )
			return FDC_GetBytesPerTrack_STX ( Drive , Track , Side );

		SectorsPerTrack = FDC_GetSectorsPerTrack ( Drive , FDC_DRIVES[ Drive ].HeadTrack , FDC.SideSignal );
		if ( SectorsPerTrack >= 36 )
			TrackSize =  FDC_TRACK_BYTES_STANDARD * 4;	/* Simulate a ED disk, 36 sectors or more */
		else if ( SectorsPerTrack >= 18 )
			TrackSize = FDC_TRACK_BYTES_STANDARD * 2;	/* Simulate a HD disk, between 18 and 36 sectors */
		else
			TrackSize = FDC_TRACK_BYTES_STANDARD;		/* Normal DD disk */
	}
	else
		TrackSize = FDC_TRACK_BYTES_STANDARD;			/* No disk, default to DD disk */

	return TrackSize;
}


/*-----------------------------------------------------------------------*/
/**
 * Return a density factor for the current track/side of the floppy in a drive
 * A DD track is usually FDC_TRACK_BYTES_STANDARD bytes, a HD track is
 * twice that number and an ED track is 4 times that number.
 * As number of bytes can vary slightly depending on mastering process and RPM
 * speed, we don't use x1, x2 and x4 directly but we use a margin such as x1.5
 * to choose between DD and HD and x3 to choose between HD and ED
 *
 * We return a factor of x1, x2 or x4 for DD, HD or ED
 * Drive should be a valid drive (0 or 1)
 */
static int FDC_ComputeFloppyDensity ( uint8_t Drive , uint8_t Track , uint8_t Side )
{
	int	TrackSize;

	TrackSize = FDC_GetBytesPerTrack ( Drive , Track , Side );

	if ( TrackSize > 3 * FDC_TRACK_BYTES_STANDARD )
		return FDC_DENSITY_FACTOR_ED;				/* Simulate a ED disk */
	else if ( TrackSize > 1.5 * FDC_TRACK_BYTES_STANDARD )
		return FDC_DENSITY_FACTOR_HD;				/* Simulate a HD disk */
	else
		return FDC_DENSITY_FACTOR_DD;				/* Normal DD disk */
}


/*-----------------------------------------------------------------------*/
/**
 * Update the density value for the inserted floppy depending on the density
 * of the current track/side
 * This function should be called each time track/side are changed and before
 * reading/writing bytes.
 */
static void FDC_UpdateFloppyDensity ( uint8_t Drive , uint8_t Track , uint8_t Side )
{
	FDC_DRIVES[ Drive ].FloppyDensity = FDC_ComputeFloppyDensity ( Drive , Track , Side );
//fprintf ( stderr , "fdc update density drive=%d track=0x%x side=%d density=%d\n" , Drive , Track, Side , FDC_DRIVES[ Drive ].FloppyDensity );
}


/*-----------------------------------------------------------------------*/
/**
 * Return the latest Density value set for a drive
 */
int	FDC_GetFloppyDensity ( uint8_t Drive )
{
	return FDC_DRIVES[ Drive ].FloppyDensity;
}


/*-----------------------------------------------------------------------*/
/**
 * Check if the emulated machine can access the floppy in Drive depending
 * on its density (DD/HD/ED)
 * - For MegaSTE, TT and Falcon, we use the content of $ff860e to compare the
 *   floppy's density with the config of the FDC. If density doesn't match,
 *   the FDC command will abort with RNF error
 * - For other machines (STF, STE) we consider as a convenience that any
 *   DD/HD/ED floppy can be accessed, even if this is not the case on real HW
 *
 * Return true if the floppy in Drive can be accessed, else false
 */
int	FDC_MachineHandleDensity ( uint8_t Drive )
{
	bool		res;

	if ( Config_IsMachineMegaSTE() || Config_IsMachineTT() || Config_IsMachineFalcon() )
	{
		if ( FDC_DRIVES[ Drive ].FloppyDensity == FDC_DENSITY_FACTOR_DD )
		{
			if ( ( FDC.DensityMode & 0x03 ) == 0x00 )	/* FDC is in DD mode ? */
				res = true;
			else
				res = false;
		}
		else							/* HD and ED */
		{
			if ( ( FDC.DensityMode & 0x03 ) == 0x03 )	/* FDC is in HD mode ? */
				res = true;
			else
				res = false;
		}
	}

	else								/* STF, STE */
		res = true;

	if ( !res )
		LOG_TRACE(TRACE_FDC, "fdc handle density failed, drive=%d drive_floppy_density=%d, fdc_mode=%d VBL=%d HBL=%d\n" ,
			Drive , FDC_DRIVES[ Drive ].FloppyDensity , FDC.DensityMode , nVBLs , nHBL );

	return res;
}


/*-----------------------------------------------------------------------*/
/**
 * Get the number of FDC cycles for one revolution of the floppy
 * RPM is already multiplied by 1000 to simulate non-integer values
 * (for Falcon, we divide cycles by 2 to simulate a FDC freq in the 8 MHz range)
 * For STX image, the number of cycles depends on drive/track/side.
 * Drive should be a valid drive (0 or 1)
 */
static uint32_t	FDC_GetCyclesPerRev_FdcCycles ( int Drive )
{
	uint32_t	FdcCyclesPerRev;

	assert(Drive == 0 || Drive == 1);

	/* If the inserted disk is an STX, we use the supplied track length to compute cycles per rev */
	if ( EmulationDrives[Drive].ImageType == FLOPPY_IMAGE_TYPE_STX )
		return FDC_GetCyclesPerRev_FdcCycles_STX ( Drive , FDC_DRIVES[ Drive ].HeadTrack , FDC.SideSignal );

	/* Assume a standard length for all tracks for ST/MSA images */
	FdcCyclesPerRev = (uint64_t)(MachineClocks.FDC_Freq * 1000.L) / ( FDC_DRIVES[ Drive ].RPM / 60 );

	/* Our conversion expects FDC_Freq to be nearly the same as CPU_Freq (8 Mhz) */
	/* but the Falcon uses a 16 MHz clock for the Ajax FDC */
	/* FIXME : this should be handled better, without involving 8 MHz CPU_Freq */
	if (Config_IsMachineFalcon())
		FdcCyclesPerRev /= 2;					/* correct delays for a 8 MHz FDC_Freq clock instead of 16 */

	return FdcCyclesPerRev;
}



/*-----------------------------------------------------------------------*/
/**
 * If some valid drive/floppy are available and the motor signal is on,
 * update the current angular position for the drive and check if
 * a new index pulse was reached. Increase Index Pulse counter in that case.
 *
 * This function should be called at least every 500 FDC cycles when motor
 * is ON to get good accuracy.
 *
 * [NP] TODO : should we have 2 different Index Pulses for each side or do they
 * happen at the same time ?
 */
static void FDC_IndexPulse_Update(void)
{
	uint32_t	FdcCyclesPerRev;
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

//fprintf ( stderr , "update index drive=%d side=%d counter=%d VBL=%d HBL=%d\n" , FDC.DriveSelSignal , FDC.SideSignal , FDC.IndexPulse_Counter , nVBLs , nHBL );

	if ( ( FDC.STR & FDC_STR_BIT_MOTOR_ON ) == 0 )
		return;							/* Motor is OFF, nothing to update */

	if ( ( FDC.DriveSelSignal < 0 ) || ( !FDC_DRIVES[ FDC.DriveSelSignal ].Enabled )
		|| ( !FDC_DRIVES[ FDC.DriveSelSignal ].DiskInserted ) )
		return;							/* No valid drive/floppy, nothing to update */

	if ( FDC_DRIVES[ FDC.DriveSelSignal ].IndexPulse_Time == 0 )	/* No reference Index Pulse for this drive */
		FDC_IndexPulse_Init ( FDC.DriveSelSignal );		/* (could be the case after a 'reset') */

	FdcCyclesPerRev = FDC_GetCyclesPerRev_FdcCycles ( FDC.DriveSelSignal );

	if ( CyclesGlobalClockCounter - FDC_DRIVES[ FDC.DriveSelSignal ].IndexPulse_Time >= FDC_FdcCyclesToCpuCycles ( FdcCyclesPerRev ) )
	{
		/* Store new position of the most recent Index Pulse */
		FDC_DRIVES[ FDC.DriveSelSignal ].IndexPulse_Time += FDC_FdcCyclesToCpuCycles ( FdcCyclesPerRev );
		FDC.IndexPulse_Counter++;
		LOG_TRACE(TRACE_FDC, "fdc update index drive=%d side=%d counter=%d ip_time=%"PRIu64" VBL=%d HBL=%d\n" ,
			FDC.DriveSelSignal , FDC.SideSignal , FDC.IndexPulse_Counter ,
			FDC_DRIVES[ FDC.DriveSelSignal ].IndexPulse_Time , nVBLs , nHBL );

		if ( FDC.InterruptCond & FDC_INTERRUPT_COND_IP )	/* Do we have a "force int on index pulse" command running ? */
		{
			LOG_TRACE(TRACE_FDC, "fdc type IV force int on index, set irq VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
				nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());
			FDC_SetIRQ ( FDC_IRQ_SOURCE_INDEX );
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * When motor is started, the position of the next index pulse will be random,
 * as we don't know how much the floppy rotated when the motor was stopped or
 * the floppy was inserted.
 * We compute a random position in the "past" (less than one revolution)
 * and use it as a reference to detect the next index pulse.
 *
 */
static void	FDC_IndexPulse_Init ( int Drive )
{
	uint32_t	FdcCyclesPerRev;
	uint64_t	IndexPulse_Time;

	FdcCyclesPerRev = FDC_GetCyclesPerRev_FdcCycles ( Drive );
	IndexPulse_Time = CyclesGlobalClockCounter - Hatari_rand() % FDC_FdcCyclesToCpuCycles ( FdcCyclesPerRev );
	if ( IndexPulse_Time <= 0 )					/* Should not happen (only if FDC_IndexPulse_Init is */
		IndexPulse_Time = 1;					/* called just after emulation starts) */
	FDC_DRIVES[ Drive ].IndexPulse_Time = IndexPulse_Time;

	LOG_TRACE(TRACE_FDC, "fdc init index drive=%d side=%d counter=%d ip_time=%"PRIu64" VBL=%d HBL=%d\n" ,
		  Drive , FDC.SideSignal , FDC.IndexPulse_Counter ,
		  FDC_DRIVES[ Drive ].IndexPulse_Time , nVBLs , nHBL );
}


/*-----------------------------------------------------------------------*/
/**
 * Return the number of FDC cycles since the previous index pulse signal
 * for the current drive.
 * If there's no available drive/floppy (i.e. no index), we return -1
 */
int	FDC_IndexPulse_GetCurrentPos_FdcCycles ( uint32_t *pFdcCyclesPerRev )
{
	uint32_t	FdcCyclesPerRev;
	uint32_t	CpuCyclesSinceIndex;

	if ( ( FDC.DriveSelSignal < 0 ) || ( FDC_DRIVES[ FDC.DriveSelSignal ].IndexPulse_Time == 0 ) )
		return -1;

	FdcCyclesPerRev = FDC_GetCyclesPerRev_FdcCycles ( FDC.DriveSelSignal );
	CpuCyclesSinceIndex = CyclesGlobalClockCounter - FDC_DRIVES[ FDC.DriveSelSignal ].IndexPulse_Time;

	if ( pFdcCyclesPerRev )
		*pFdcCyclesPerRev = FdcCyclesPerRev;

//fprintf ( stderr , "current pos %d %lld %d %lld\n" , FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].IndexPulse_Time ,
//      FdcCyclesPerRev , CyclesGlobalClockCounter - FDC_DRIVES[ FDC.DriveSelSignal ].IndexPulse_Time );

	return FDC_CpuCyclesToFdcCycles ( CpuCyclesSinceIndex );
}


/*-----------------------------------------------------------------------*/
/**
 * Return the current position in the track relative to the index pulse.
 * For standard floppy, this is a number of bytes in the range [0,6250[
 * If there's no available drive/floppy and no index, we return -1
 * To simulate HD/ED floppies, we multiply the number of bytes by a density factor.
 */
int	FDC_IndexPulse_GetCurrentPos_NbBytes ( void )
{
	int	FdcCyclesSinceIndex;

	FdcCyclesSinceIndex = FDC_IndexPulse_GetCurrentPos_FdcCycles ( NULL );
	if ( FdcCyclesSinceIndex < 0 )					/* No drive/floppy available at the moment */
		return -1;
//fprintf ( stderr , "fdc index current pos new=%d\n" , FdcCyclesSinceIndex / FDC_DELAY_CYCLE_MFM_BYTE );

	return FdcCyclesSinceIndex * FDC_GetFloppyDensity ( FDC.DriveSelSignal ) / FDC_DELAY_CYCLE_MFM_BYTE;
}



/*-----------------------------------------------------------------------*/
/**
 * Return the current state of the index pulse signal.
 * The signal goes to 1 when reaching the index pulse location and remain
 * to 1 during 1.5 ms (approx 46 bytes).
 * During the rest of the track, the signal will be 0 (it will also be 0
 * if the drive if OFF or empty)
 */
int	FDC_IndexPulse_GetState ( void )
{
	int	state;
	int	FdcCyclesSinceIndex;

	FdcCyclesSinceIndex = FDC_IndexPulse_GetCurrentPos_FdcCycles ( NULL );

	state = 0;
	if ( ( FdcCyclesSinceIndex >= 0 )				/* We have a valid drive/floppy */
	  && ( (uint32_t)FdcCyclesSinceIndex < FDC_DelayToFdcCycles ( FDC_DELAY_US_INDEX_PULSE_LENGTH ) ) )
		state = 1;

//fprintf ( stderr , "fdc index state 2 pos pos=%d state=%d\n" , FdcCyclesSinceIndex , state );
	return state;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the number of FDC cycles before reaching the next index pulse signal.
 * If there's no available drive/floppy and no index, we return -1
 */
int	FDC_NextIndexPulse_FdcCycles ( void )
{
	uint32_t	FdcCyclesPerRev;
	int	FdcCyclesSinceIndex;
	int	res;

	FdcCyclesSinceIndex = FDC_IndexPulse_GetCurrentPos_FdcCycles ( &FdcCyclesPerRev );
	if ( FdcCyclesSinceIndex < 0 )					/* No drive/floppy available at the moment */
		return -1;

	res = FdcCyclesPerRev - FdcCyclesSinceIndex;

	/* If the next IP is in 0 or 1 cycle, we consider this is a rounding error */
	/* and we wait for one full revolution (this can happen in Force Int on Index Pulse */
	/* when we call FDC_NextIndexPulse_FdcCycles in a loop) */
	if ( res <= 1 )
		res = FdcCyclesPerRev;					// TODO : 0 should be allowed
	
//fprintf ( stderr , "fdc next index current pos new=%d\n" , res );
	return res;
}


/*-----------------------------------------------------------------------*/
/**
 * Set the IRQ signal
 * This is called either on command completion, or when an index pulse
 * is received or when the "force interrupt immediate" command is used.
 * This function can also be called from the HDC emulation or from another
 * FDC emulation module (IPF for example)
 *
 * NOTE : although high/1 on the IRQ pin of the FDC means an interrupt is
 * requested, this signal is inverted before going into MFP's GPIP5.
 * So, we must set the line to low/0 to request an interrupt.
 */
void FDC_SetIRQ ( uint8_t IRQ_Source )
{
	if ( FDC.IRQ_Signal != 0 )					/* Don't set MFP's IRQ again if already set */
	{
	  LOG_TRACE(TRACE_FDC, "fdc set irq, irq 0x%x already set VBL=%d HBL=%d\n" , FDC.IRQ_Signal , nVBLs , nHBL );
	}

	else
	{
		/* Acknowledge in MFP circuit, pass bit, enable, pending */
		MFP_GPIP_Set_Line_Input ( pMFP_Main , MFP_GPIP_LINE_FDC_HDC , MFP_GPIP_STATE_LOW );
		LOG_TRACE(TRACE_FDC, "fdc set irq 0x%x source 0x%x VBL=%d HBL=%d\n" , FDC.IRQ_Signal , IRQ_Source , nVBLs , nHBL );
	}

	/* If IRQ comes from HDC or other sources (IPF), we don't need */
	/* to handle the forced IRQ case used in FDC */
	if ( IRQ_Source == FDC_IRQ_SOURCE_HDC )
		FDC.IRQ_Signal = FDC_IRQ_SOURCE_HDC;

	else if ( IRQ_Source == FDC_IRQ_SOURCE_OTHER )
		FDC.IRQ_Signal = FDC_IRQ_SOURCE_OTHER;

	else								/* IRQ comes from FDC */
	{
		FDC.IRQ_Signal &= ~( FDC_IRQ_SOURCE_HDC | FDC_IRQ_SOURCE_OTHER );
		FDC.IRQ_Signal |= IRQ_Source;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Reset the IRQ signal ; in case the source of the interrupt is also
 * a "force interrupt immediate" command, the IRQ signal should not be cleared
 * (only command 0xD0 or any new command followed by a read of status reg
 * can clear the forced IRQ)
 *
 * NOTE : although low/0 on the IRQ pin of the FDC means interrupt is
 * cleared, this signal is inverted before going into MFP's GPIP5.
 * So, we must set the line to high/1 to clear interrupt request.
 */
void FDC_ClearIRQ ( void )
{
	if ( ( FDC.IRQ_Signal & FDC_IRQ_SOURCE_FORCED ) == 0 )
	{
		FDC.IRQ_Signal = 0;
		MFP_GPIP_Set_Line_Input ( pMFP_Main , MFP_GPIP_LINE_FDC_HDC , MFP_GPIP_STATE_HIGH );
		LOG_TRACE(TRACE_FDC, "fdc clear irq VBL=%d HBL=%d\n" , nVBLs , nHBL );
	}

	else
	{
		FDC.IRQ_Signal &= FDC_IRQ_SOURCE_FORCED;		/* Clear all sources except 'forced irq' and keep IRQ set in MFP */
		LOG_TRACE(TRACE_FDC, "fdc clear irq not done, irq forced VBL=%d HBL=%d\n" , nVBLs , nHBL );
	}
}

void FDC_ClearHdcIRQ(void)
{
	FDC.IRQ_Signal &= ~FDC_IRQ_SOURCE_HDC;
	if (FDC.IRQ_Signal == 0)
	{
		MFP_GPIP_Set_Line_Input ( pMFP_Main , MFP_GPIP_LINE_FDC_HDC , MFP_GPIP_STATE_HIGH );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle the current FDC command.
 * We use a timer to go from one state to another to emulate the different
 * phases of an FDC command.
 * When the command completes (success or failure), FDC.Command will be
 * set to FDCEMU_CMD_NULL. Until then, this function will be called to
 * handle each state of the command and the corresponding delay in micro
 * seconds.
 * This handler is called after a first delay corresponding to the prepare
 * delay and the eventual motor on delay.
 * Once we reach this point, the current command can not be replaced by
 * another command (except 'Force Interrupt')
 */
void FDC_InterruptHandler_Update ( void )
{
	int	FdcCycles = 0;
	int	PendingCyclesOver;

	/* Number of internal cycles we went over for this timer ( <= 0 ) */
	/* Used to restart the next timer and keep a constant rate (important for DMA transfers) */
	PendingCyclesOver = -PendingInterruptCount;			/* >= 0 */

//fprintf ( stderr , "fdc int handler %lld delay %d\n" , CyclesGlobalClockCounter, PendingCyclesOver );

	CycInt_AcknowledgeInterrupt();

	do								/* We loop as long as FdcCycles == 0 (immediate change of state) */
	{
		/* Update FDC's internal variables */
		FDC_UpdateAll ();

		/* Is FDC active? */
		if (FDC.Command!=FDCEMU_CMD_NULL)
		{
			/* Which command are we running ? */
			switch(FDC.Command)
			{
			case FDCEMU_CMD_RESTORE:
				FdcCycles = FDC_UpdateRestoreCmd();
				break;
			case FDCEMU_CMD_SEEK:
				FdcCycles = FDC_UpdateSeekCmd();
				break;
			case FDCEMU_CMD_STEP:
				FdcCycles = FDC_UpdateStepCmd();
				break;

			case FDCEMU_CMD_READSECTORS:
				FdcCycles = FDC_UpdateReadSectorsCmd();
				break;
			case FDCEMU_CMD_WRITESECTORS:
				FdcCycles = FDC_UpdateWriteSectorsCmd();
				break;

			case FDCEMU_CMD_READADDRESS:
				FdcCycles = FDC_UpdateReadAddressCmd();
				break;

			case FDCEMU_CMD_READTRACK:
				FdcCycles = FDC_UpdateReadTrackCmd();
				break;

			case FDCEMU_CMD_WRITETRACK:
				FdcCycles = FDC_UpdateWriteTrackCmd();
				break;

			case FDCEMU_CMD_MOTOR_STOP:
				FdcCycles = FDC_UpdateMotorStop();
				break;
			}
		}
	}
	while ( ( FDC.Command != FDCEMU_CMD_NULL ) && ( FdcCycles == 0 ) );

	if ( FDC.Command != FDCEMU_CMD_NULL )
	{
		FDC_StartTimer_FdcCycles ( FdcCycles , -PendingCyclesOver );
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Return the type of a command, based on the upper bits of CR
 */
uint8_t FDC_GetCmdType ( uint8_t CR )
{
	if ( ( CR & 0x80 ) == 0 )					/* Type I - Restore, Seek, Step, Step-In, Step-Out */
		return 1;
	else if ( ( CR & 0x40 ) == 0 )					/* Type II - Read Sector, Write Sector */
		return 2;
	else if ( ( CR & 0xf0 ) != 0xd0 )				/* Type III - Read Address, Read Track, Write Track */
		return 3;
	else								/* Type IV - Force Interrupt */
		return 4;
}


/*-----------------------------------------------------------------------*/
/**
 * Update the FDC's Status Register.
 * All bits in DisableBits are cleared in STR, then all bits in EnableBits
 * are set in STR.
 */
static void FDC_Update_STR ( uint8_t DisableBits , uint8_t EnableBits )
{
	FDC.STR &= (~DisableBits);					/* Clear bits in DisableBits */
	FDC.STR |= EnableBits;						/* Set bits in EnableBits */

	FDC_Drive_Set_BusyLed ( FDC.STR );
//fprintf ( stderr , "fdc str 0x%x\n" , FDC.STR );
}


/*-----------------------------------------------------------------------*/
/**
 * Common to all commands once they're completed :
 * - remove busy bit
 * - set interrupt if necessary
 * - stop motor after 2 sec
 */
static int FDC_CmdCompleteCommon ( bool DoInt )
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
	LOG_TRACE(TRACE_FDC, "fdc complete command VBL=%d video_cyc=%d %d@%d pc=%x\n",
		nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	FDC_Update_STR ( FDC_STR_BIT_BUSY , 0 );			/* Remove busy bit */

	if ( DoInt )
		FDC_SetIRQ ( FDC_IRQ_SOURCE_COMPLETE );

	FDC.Command = FDCEMU_CMD_MOTOR_STOP;				/* Fake command to stop the motor */
	FDC.CommandState = FDCEMU_RUN_MOTOR_STOP;
	return FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
}


/*-----------------------------------------------------------------------*/
/**
 * Verify track after a type I command.
 * The FDC will read the first ID field of the current track and will
 * compare the track number in this ID field with the current Track Register.
 * If they don't match, we try again with the next ID field until we
 * reach 5 revolutions, in which case we set RNF.
 *
 * NOTE [NP] : in the case of Hatari when using ST/MSA images, the track is always the correct one,
 * so the verify will always be good (except if no disk is inserted or the physical head is
 * not on the same track as FDC.TR)
 * For STX images, verify track might fail on purpose with some protection
 */
static bool FDC_VerifyTrack ( void )
{
	int	FrameCycles, HblCounterVideo, LineCycles;
	uint8_t	Next_TR;
	uint8_t	Next_CRC_OK;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	/* Return false if no drive selected, or drive not enabled, or no disk in drive */
	if ( ( FDC.DriveSelSignal < 0 ) || ( !FDC_DRIVES[ FDC.DriveSelSignal ].Enabled )
		|| ( !FDC_DRIVES[ FDC.DriveSelSignal ].DiskInserted ) )
	{
		LOG_TRACE(TRACE_FDC, "fdc type I verify track failed disabled/empty drive=%d VBL=%d video_cyc=%d %d@%d pc=%x\n",
			FDC.DriveSelSignal , nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());
		return false;
	}

	/* Check if the current ID Field is the one we're looking for (same track and correct CRC) */
	if ( EmulationDrives[ FDC.DriveSelSignal ].ImageType == FLOPPY_IMAGE_TYPE_STX )
	{
		Next_TR = FDC_NextSectorID_TR_STX ();
		Next_CRC_OK = FDC_NextSectorID_CRC_OK_STX ();
	}
	else
	{
		Next_TR = FDC_NextSectorID_TR_ST ();
		Next_CRC_OK = FDC_NextSectorID_CRC_OK_ST ();
	}

	/* ST/MSA image will always be correct, only STX can fail depending on some protections */
	if ( ( Next_TR != FDC.TR ) || ( Next_CRC_OK == 0 ) )
	{
		LOG_TRACE(TRACE_FDC, "fdc type I verify track failed ID_TR=0x%x TR=0x%x crc_ok=%d head=0x%x drive=%d VBL=%d video_cyc=%d %d@%d pc=%x\n",
			Next_TR , FDC.TR , Next_CRC_OK , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.DriveSelSignal ,
			nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

		return false;
	}

	/* If disk image has only one side or drive is single sided and we're trying to verify on 2nd side, then return false */
	if ( ( FDC.SideSignal == 1  )
	  && ( ( FDC_GetSidesPerDisk ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack ) != 2 )
	    || ( FDC_DRIVES[ FDC.DriveSelSignal ].NumberOfHeads == 1 ) ) )
	{
		LOG_TRACE(TRACE_FDC, "fdc type I verify track failed TR=0x%x head=0x%x side=1 doesn't exist drive=%d VBL=%d video_cyc=%d %d@%d pc=%x\n",
			FDC.TR , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.DriveSelSignal ,
			nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

		return false;
	}

	/* The track is the correct one */
	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * Run the 'motor stop' sequence : wait for 9 revolutions (1.8 sec)
 * and stop the motor.
 * We clear motor bit, but spinup bit remains to 1 (verified on a real STF)
 */
static int FDC_UpdateMotorStop ( void )
{
	int	FdcCycles = 0;
	int	FrameCycles, HblCounterVideo, LineCycles;

	/* Which command is running? */
	switch (FDC.CommandState)
	{
	 case FDCEMU_RUN_MOTOR_STOP:
		FDC.IndexPulse_Counter = 0;
		FDC.CommandState = FDCEMU_RUN_MOTOR_STOP_WAIT;
		/* Fall through to next state */
	 case FDCEMU_RUN_MOTOR_STOP_WAIT:
		if ( FDC.IndexPulse_Counter < FDC_DELAY_IP_MOTOR_OFF )
		{
			FdcCycles = FDC_DELAY_CYCLE_REFRESH_INDEX_PULSE;	/* Wait for the correct number of IP */
			break;
		}
		/* If IndexPulse_Counter reached, we fall through directly to the _COMPLETE state */
	 case FDCEMU_RUN_MOTOR_STOP_COMPLETE:
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE(TRACE_FDC, "fdc motor stopped VBL=%d video_cyc=%d %d@%d pc=%x\n",
			nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

		FDC.IndexPulse_Counter = 0;
		if ( FDC.DriveSelSignal >= 0 )                                  /* A drive was previously enabled */
			FDC_DRIVES[ FDC.DriveSelSignal ].IndexPulse_Time = 0;   /* Stop counting index pulses on the drive */

		FDC_Update_STR ( FDC_STR_BIT_MOTOR_ON , 0 );		/* Unset motor bit and keep spin up bit */
		FDC.Command = FDCEMU_CMD_NULL;				/* Motor stopped, this is the last state */
		FdcCycles = 0;
		break;
	}
	return FdcCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Run 'RESTORE' command
 */
static int FDC_UpdateRestoreCmd ( void )
{
	int	FdcCycles = 0;
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	/* Which command is running? */
	switch (FDC.CommandState)
	{
	 case FDCEMU_RUN_RESTORE_SEEKTOTRACKZERO:
		if ( FDC_Set_MotorON ( FDC.CR ) )
		{
			FDC.CommandState = FDCEMU_RUN_RESTORE_SEEKTOTRACKZERO_SPIN_UP;
			FdcCycles = FDC_DELAY_CYCLE_REFRESH_INDEX_PULSE;	/* Spin up needed */
		}
		else
		{
			FDC.CommandState = FDCEMU_RUN_RESTORE_SEEKTOTRACKZERO_MOTOR_ON;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;		/* No spin up needed */
		}
		break;
	 case FDCEMU_RUN_RESTORE_SEEKTOTRACKZERO_SPIN_UP:
		if ( FDC.IndexPulse_Counter < FDC_DELAY_IP_SPIN_UP )
		{
			FdcCycles = FDC_DELAY_CYCLE_REFRESH_INDEX_PULSE;	/* Wait for the correct number of IP */
			break;
		}
		/* If IndexPulse_Counter reached, we fall through directly to the _MOTOR_ON state */
	 case FDCEMU_RUN_RESTORE_SEEKTOTRACKZERO_MOTOR_ON:
		FDC_Update_STR ( 0 , FDC_STR_BIT_SPIN_UP );		/* At this point, spin up sequence is ok */
		FDC.ReplaceCommandPossible = false;

		/* The FDC will try 255 times to reach track 0 using step out signals */
		/* If track 0 signal is not detected after 255 attempts, the command is interrupted */
		/* and FDC_STR_BIT_RNF is set in the Status Register. */
		/* This can happen if no drive is selected or if the selected drive is disabled */
		/* TR should be set to 255 once the spin-up sequence is made and the command */
		/* can't be interrupted anymore by another command (else TR value will be wrong */
		/* for other type I commands) */
		FDC.TR = 0xff;				
		FDC.CommandState = FDCEMU_RUN_RESTORE_SEEKTOTRACKZERO_LOOP;
		/* Fall through to the _LOOP state */
	 case FDCEMU_RUN_RESTORE_SEEKTOTRACKZERO_LOOP:
		if ( FDC.TR == 0 )					/* Track 0 not reached after 255 attempts ? */
		{							/* (this can happen if the drive is disabled) */
			FDC_Update_STR ( 0 , FDC_STR_BIT_RNF );
			FDC_Update_STR ( FDC_STR_BIT_TR00 , 0 );	/* Unset bit TR00 */
			FdcCycles = FDC_CmdCompleteCommon( true );
			break;
		}

		if ( ( FDC.DriveSelSignal < 0 ) || ( !FDC_DRIVES[ FDC.DriveSelSignal ].Enabled )
			|| ( FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack != 0 ) )	/* Are we at track zero ? */
		{
			FDC_Update_STR ( FDC_STR_BIT_TR00 , 0 );	/* Unset bit TR00 */
			FDC.TR--;					/* One less attempt */
			if ( ( FDC.DriveSelSignal >= 0 ) && ( FDC_DRIVES[ FDC.DriveSelSignal ].Enabled ) )
			{
				FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack--;	/* Move physical head only if an enabled drive is selected */
				FDC_UpdateFloppyDensity ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal );
			}
			FdcCycles = FDC_DelayToFdcCycles ( FDC_StepRate_ms[ FDC_STEP_RATE ] * 1000 );
		}
		else							/* Drive is enabled and head is at track 0 */
		{
			FDC_Update_STR ( 0 , FDC_STR_BIT_TR00 );	/* Set bit TR00 */
			FDC.TR = 0;					/* Update Track Register to 0 */
			FDC.CommandState = FDCEMU_RUN_RESTORE_VERIFY;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
		}
		break;
	 case FDCEMU_RUN_RESTORE_VERIFY:
		if ( FDC.CR & FDC_COMMAND_BIT_VERIFY )
		{
			FDC.CommandState = FDCEMU_RUN_RESTORE_VERIFY_HEAD_OK;
			FdcCycles = FDC_DelayToFdcCycles ( FDC_DELAY_US_HEAD_LOAD );	/* Head settle delay */
		}
		else
		{
			FDC.CommandState = FDCEMU_RUN_RESTORE_COMPLETE;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_COMPLETE;
		}
		break;
	 case FDCEMU_RUN_RESTORE_VERIFY_HEAD_OK:
		FDC.IndexPulse_Counter = 0;
		/* Head OK, fall through and look for sector header */
	 case FDCEMU_RUN_RESTORE_VERIFY_NEXT_SECTOR_HEADER:
		/* If 'verify' doesn't succeed after 5 revolutions, we abort with RNF */
		if ( FDC.IndexPulse_Counter >= FDC_DELAY_IP_ADDRESS_ID )
		{
			LOG_TRACE(TRACE_FDC, "fdc type I restore track=%d drive=%d verify RNF VBL=%d video_cyc=%d %d@%d pc=%x\n",
				FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.DriveSelSignal , nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

			FDC_Update_STR ( 0 , FDC_STR_BIT_RNF );			/* Set RNF bit */
			FDC.CommandState = FDCEMU_RUN_RESTORE_COMPLETE;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_COMPLETE;
			break;
		}

		if ( FDC.DriveSelSignal < 0 )					/* No drive selected */
			FdcCycles = -1;
		else if ( EmulationDrives[ FDC.DriveSelSignal ].ImageType == FLOPPY_IMAGE_TYPE_STX )
			FdcCycles = FDC_NextSectorID_FdcCycles_STX ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].NumberOfHeads ,
					FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal );
		else
			FdcCycles = FDC_NextSectorID_FdcCycles_ST ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].NumberOfHeads ,
					FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal );
		if ( FdcCycles < 0 )
		{
			FDC.CommandState = FDCEMU_RUN_RESTORE_VERIFY_NEXT_SECTOR_HEADER;
			FdcCycles = FDC_DELAY_CYCLE_WAIT_NO_DRIVE_FLOPPY;	/* Wait for a valid drive/floppy */
		}
		else
		{
			/* Read bytes to reach the next sector's ID field and skip 10 more bytes to read the whole ID field */
			FdcCycles += FDC_TransferByte_FdcCycles ( 10 );		/* Add delay to read 3xA1, FE, ID field */
			FDC.CommandState = FDCEMU_RUN_RESTORE_VERIFY_CHECK_SECTOR_HEADER;
		}
		break;
	 case FDCEMU_RUN_RESTORE_VERIFY_CHECK_SECTOR_HEADER:
		/* Check if the current ID Field matches the track number */
		if ( FDC_VerifyTrack () )
		{
			FDC_Update_STR ( FDC_STR_BIT_RNF , 0 );			/* Track is correct, remove RNF bit */
			FDC.CommandState = FDCEMU_RUN_RESTORE_COMPLETE;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_COMPLETE;
		}
		else
		{
			/* Verify failed with this ID field ; check the next one */
			FDC.CommandState = FDCEMU_RUN_RESTORE_VERIFY_NEXT_SECTOR_HEADER;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
		}
		break;
	 case FDCEMU_RUN_RESTORE_COMPLETE:
		FdcCycles = FDC_CmdCompleteCommon( true );
		break;
	}

	return FdcCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Run 'SEEK' command
 */
static int FDC_UpdateSeekCmd ( void )
{
	int	FdcCycles = 0;
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	/* Which command is running? */
	switch (FDC.CommandState)
	{
	 case FDCEMU_RUN_SEEK_TOTRACK:
		if ( FDC_Set_MotorON ( FDC.CR ) )
		{
			FDC.CommandState = FDCEMU_RUN_SEEK_TOTRACK_SPIN_UP;
			FdcCycles = FDC_DELAY_CYCLE_REFRESH_INDEX_PULSE;	/* Spin up needed */
		}
		else
		{
			FDC.CommandState = FDCEMU_RUN_SEEK_TOTRACK_MOTOR_ON;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;		/* No spin up needed */
		}
		break;
	 case FDCEMU_RUN_SEEK_TOTRACK_SPIN_UP:
		if ( FDC.IndexPulse_Counter < FDC_DELAY_IP_SPIN_UP )
		{
			FdcCycles = FDC_DELAY_CYCLE_REFRESH_INDEX_PULSE;	/* Wait for the correct number of IP */
			break;
		}
		/* If IndexPulse_Counter reached, we fall through directly to the _MOTOR_ON state */
	 case FDCEMU_RUN_SEEK_TOTRACK_MOTOR_ON:
		FDC_Update_STR ( 0 , FDC_STR_BIT_SPIN_UP );		/* At this point, spin up sequence is ok */
		FDC.ReplaceCommandPossible = false;

		if ( FDC.TR == FDC.DR )					/* Are we at the selected track ? */
		{
			FDC.CommandState = FDCEMU_RUN_SEEK_VERIFY;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
		}
		else
		{
			if ( FDC.DR < FDC.TR )				/* Set StepDirection to the correct value */
				FDC.StepDirection = -1;
			else
				FDC.StepDirection = 1;

			/* Move head by one track depending on FDC.StepDirection and update Track Register */
			FDC.TR += FDC.StepDirection;

			FdcCycles = FDC_DelayToFdcCycles ( FDC_StepRate_ms[ FDC_STEP_RATE ] * 1000 );
			FDC_Update_STR ( FDC_STR_BIT_TR00 , 0 );	/* By default, unset bit TR00 */

			/* Check / move physical head only if an enabled drive is selected */
			if ( ( FDC.DriveSelSignal >= 0 ) && ( FDC_DRIVES[ FDC.DriveSelSignal ].Enabled ) )
			{
				if ( ( FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack == FDC_PHYSICAL_MAX_TRACK ) && ( FDC.StepDirection == 1 ) )
				{
					FDC.CommandState = FDCEMU_RUN_SEEK_VERIFY;
					FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;	/* No delay if trying to go after max track */
				}

				else if ( ( FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack == 0 ) && ( FDC.StepDirection == -1 ) )
				{
					FDC.TR = 0;			/* If we reach track 0, we stop there */
					FDC.CommandState = FDCEMU_RUN_SEEK_VERIFY;
					FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
				}

				else
				{
					FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack += FDC.StepDirection;	/* Move physical head */
					FDC_UpdateFloppyDensity ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal );
				}

				if ( FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack == 0 )
					FDC_Update_STR ( 0 , FDC_STR_BIT_TR00 );	/* Set bit TR00 */
			}
		}

		break;
	 case FDCEMU_RUN_SEEK_VERIFY:
		if ( FDC.CR & FDC_COMMAND_BIT_VERIFY )
		{
			FDC.CommandState = FDCEMU_RUN_SEEK_VERIFY_HEAD_OK;
			FdcCycles = FDC_DelayToFdcCycles ( FDC_DELAY_US_HEAD_LOAD );	/* Head settle delay */
		}
		else
		{
			FDC.CommandState = FDCEMU_RUN_SEEK_COMPLETE;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_COMPLETE;
		}
		break;
	 case FDCEMU_RUN_SEEK_VERIFY_HEAD_OK:
		FDC.IndexPulse_Counter = 0;
		/* Head OK, fall through and look for sector header */
	 case FDCEMU_RUN_SEEK_VERIFY_NEXT_SECTOR_HEADER:
		/* If 'verify' doesn't succeed after 5 revolutions, we abort with RNF */
		if ( FDC.IndexPulse_Counter >= FDC_DELAY_IP_ADDRESS_ID )
		{
			LOG_TRACE(TRACE_FDC, "fdc type I seek track=%d drive=%d verify RNF VBL=%d video_cyc=%d %d@%d pc=%x\n",
				FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.DriveSelSignal , nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

			FDC_Update_STR ( 0 , FDC_STR_BIT_RNF );			/* Set RNF bit */
			FDC.CommandState = FDCEMU_RUN_SEEK_COMPLETE;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_COMPLETE;
			break;
		}

		if ( FDC.DriveSelSignal < 0 )					/* No drive selected */
			FdcCycles = -1;
		else if ( EmulationDrives[ FDC.DriveSelSignal ].ImageType == FLOPPY_IMAGE_TYPE_STX )
			FdcCycles = FDC_NextSectorID_FdcCycles_STX ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].NumberOfHeads ,
					FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal );
		else
			FdcCycles = FDC_NextSectorID_FdcCycles_ST ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].NumberOfHeads ,
					FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal );
		if ( FdcCycles < 0 )
		{
			FDC.CommandState = FDCEMU_RUN_SEEK_VERIFY_NEXT_SECTOR_HEADER;
			FdcCycles = FDC_DELAY_CYCLE_WAIT_NO_DRIVE_FLOPPY;	/* Wait for a valid drive/floppy */
		}
		else
		{
			/* Read bytes to reach the next sector's ID field and skip 10 more bytes to read the whole ID field */
			FdcCycles += FDC_TransferByte_FdcCycles ( 10 );		/* Add delay to read 3xA1, FE, ID field */
			FDC.CommandState = FDCEMU_RUN_SEEK_VERIFY_CHECK_SECTOR_HEADER;
		}
		break;
	 case FDCEMU_RUN_SEEK_VERIFY_CHECK_SECTOR_HEADER:
		/* Check if the current ID Field matches the track number */
		if ( FDC_VerifyTrack () )
		{
			FDC_Update_STR ( FDC_STR_BIT_RNF , 0 );			/* Track is correct, remove RNF bit */
			FDC.CommandState = FDCEMU_RUN_SEEK_COMPLETE;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_COMPLETE;
		}
		else
		{
			/* Verify failed with this ID field ; check the next one */
			FDC.CommandState = FDCEMU_RUN_SEEK_VERIFY_NEXT_SECTOR_HEADER;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
		}
		break;
	 case FDCEMU_RUN_SEEK_COMPLETE:
		FdcCycles = FDC_CmdCompleteCommon( true );
		break;
	}

	return FdcCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Run 'STEP' command
 */
static int FDC_UpdateStepCmd ( void )
{
	int	FdcCycles = 0;
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	/* Which command is running? */
	switch (FDC.CommandState)
	{
	 case FDCEMU_RUN_STEP_ONCE:
		if ( FDC_Set_MotorON ( FDC.CR ) )
		{
			FDC.CommandState = FDCEMU_RUN_STEP_ONCE_SPIN_UP;
			FdcCycles = FDC_DELAY_CYCLE_REFRESH_INDEX_PULSE;	/* Spin up needed */
		}
		else
		{
			FDC.CommandState = FDCEMU_RUN_STEP_ONCE_MOTOR_ON;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;		/* No spin up needed */
		}
		break;
	 case FDCEMU_RUN_STEP_ONCE_SPIN_UP:
		if ( FDC.IndexPulse_Counter < FDC_DELAY_IP_SPIN_UP )
		{
			FdcCycles = FDC_DELAY_CYCLE_REFRESH_INDEX_PULSE;	/* Wait for the correct number of IP */
			break;
		}
		/* If IndexPulse_Counter reached, we fall through directly to the _MOTOR_ON state */
	 case FDCEMU_RUN_STEP_ONCE_MOTOR_ON:
		FDC_Update_STR ( 0 , FDC_STR_BIT_SPIN_UP );		/* At this point, spin up sequence is ok */
		FDC.ReplaceCommandPossible = false;

		/* Move head by one track depending on FDC.StepDirection */
		if ( FDC.CR & FDC_COMMAND_BIT_UPDATE_TRACK )
			FDC.TR += FDC.StepDirection;			/* Update Track Register */

		FdcCycles = FDC_DelayToFdcCycles ( FDC_StepRate_ms[ FDC_STEP_RATE ] * 1000 );
		FDC_Update_STR ( FDC_STR_BIT_TR00 , 0 );		/* By default, unset bit TR00 */

		/* Check / move physical head only if an enabled drive is selected */
		if ( ( FDC.DriveSelSignal >= 0 ) && ( FDC_DRIVES[ FDC.DriveSelSignal ].Enabled ) )
		{
			if ( ( FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack == FDC_PHYSICAL_MAX_TRACK ) && ( FDC.StepDirection == 1 ) )
				FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;	/* No delay if trying to go after max track */

			else if ( ( FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack == 0 ) && ( FDC.StepDirection == -1 ) )
				FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;	/* No delay if trying to go before track 0 */

			else
			{
				FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack += FDC.StepDirection;	/* Move physical head */
				FDC_UpdateFloppyDensity ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal );
			}

			if ( FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack == 0 )
				FDC_Update_STR ( 0 , FDC_STR_BIT_TR00 );	/* Set bit TR00 */
		}

		FDC.CommandState = FDCEMU_RUN_STEP_VERIFY;
		break;
	 case FDCEMU_RUN_STEP_VERIFY:
		if ( FDC.CR & FDC_COMMAND_BIT_VERIFY )
		{
			FDC.CommandState = FDCEMU_RUN_STEP_VERIFY_HEAD_OK;
			FdcCycles = FDC_DelayToFdcCycles ( FDC_DELAY_US_HEAD_LOAD );	/* Head settle delay */
		}
		else
		{
			FDC.CommandState = FDCEMU_RUN_STEP_COMPLETE;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_COMPLETE;
		}
		break;
	 case FDCEMU_RUN_STEP_VERIFY_HEAD_OK:
		FDC.IndexPulse_Counter = 0;
		/* Head OK, fall through and look for sector header */
	 case FDCEMU_RUN_STEP_VERIFY_NEXT_SECTOR_HEADER:
		/* If 'verify' doesn't succeed after 5 revolutions, we abort with RNF */
		if ( FDC.IndexPulse_Counter >= FDC_DELAY_IP_ADDRESS_ID )
		{
			LOG_TRACE(TRACE_FDC, "fdc type I step track=%d drive=%d verify RNF VBL=%d video_cyc=%d %d@%d pc=%x\n",
				FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.DriveSelSignal , nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

			FDC_Update_STR ( 0 , FDC_STR_BIT_RNF );			/* Set RNF bit */
			FDC.CommandState = FDCEMU_RUN_STEP_COMPLETE;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_COMPLETE;
			break;
		}

		if ( FDC.DriveSelSignal < 0 )					/* No drive selected */
			FdcCycles = -1;
		else if ( EmulationDrives[ FDC.DriveSelSignal ].ImageType == FLOPPY_IMAGE_TYPE_STX )
			FdcCycles = FDC_NextSectorID_FdcCycles_STX ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].NumberOfHeads ,
					FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal );
		else
			FdcCycles = FDC_NextSectorID_FdcCycles_ST ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].NumberOfHeads ,
					FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal );
		if ( FdcCycles < 0 )
		{
			FDC.CommandState = FDCEMU_RUN_STEP_VERIFY_NEXT_SECTOR_HEADER;
			FdcCycles = FDC_DELAY_CYCLE_WAIT_NO_DRIVE_FLOPPY;	/* Wait for a valid drive/floppy */
		}
		else
		{
			/* Read bytes to reach the next sector's ID field and skip 10 more bytes to read the whole ID field */
			FdcCycles += FDC_TransferByte_FdcCycles ( 10 );		/* Add delay to read 3xA1, FE, ID field */
			FDC.CommandState = FDCEMU_RUN_STEP_VERIFY_CHECK_SECTOR_HEADER;
		}
		break;
	 case FDCEMU_RUN_STEP_VERIFY_CHECK_SECTOR_HEADER:
		/* Check if the current ID Field matches the track number */
		if ( FDC_VerifyTrack () )
		{
			FDC_Update_STR ( FDC_STR_BIT_RNF , 0 );			/* Track is correct, remove RNF bit */
			FDC.CommandState = FDCEMU_RUN_STEP_COMPLETE;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_COMPLETE;
		}
		else
		{
			/* Verify failed with this ID field ; check the next one */
			FDC.CommandState = FDCEMU_RUN_STEP_VERIFY_NEXT_SECTOR_HEADER;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
		}
		break;
	 case FDCEMU_RUN_STEP_COMPLETE:
		FdcCycles = FDC_CmdCompleteCommon( true );
		break;
	}

	return FdcCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Run 'READ SECTOR/S' command
 */
static int FDC_UpdateReadSectorsCmd ( void )
{
	int	FdcCycles = 0;
	int	SectorSize;
	int	FrameCycles, HblCounterVideo, LineCycles;
	uint8_t	Next_TR;
	uint8_t	Next_SR;
	uint8_t	Next_CRC_OK;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );


	/* Which command is running? */
	switch (FDC.CommandState)
	{
	 case FDCEMU_RUN_READSECTORS_READDATA:
		if ( FDC_Set_MotorON ( FDC.CR ) )
		{
			FDC.CommandState = FDCEMU_RUN_READSECTORS_READDATA_SPIN_UP;
			FdcCycles = FDC_DELAY_CYCLE_REFRESH_INDEX_PULSE;	/* Spin up needed */
		}
		else
		{
			FDC.CommandState = FDCEMU_RUN_READSECTORS_READDATA_HEAD_LOAD;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;		/* No spin up needed */
		}
		break;
	 case FDCEMU_RUN_READSECTORS_READDATA_SPIN_UP:
		if ( FDC.IndexPulse_Counter < FDC_DELAY_IP_SPIN_UP )
		{
			FdcCycles = FDC_DELAY_CYCLE_REFRESH_INDEX_PULSE;	/* Wait for the correct number of IP */
			break;
		}
		/* If IndexPulse_Counter reached, we fall through directly to the _HEAD_LOAD state */
	 case FDCEMU_RUN_READSECTORS_READDATA_HEAD_LOAD:
		if ( FDC.CR & FDC_COMMAND_BIT_HEAD_LOAD )
		{
			FDC.CommandState = FDCEMU_RUN_READSECTORS_READDATA_MOTOR_ON;
			FdcCycles = FDC_DelayToFdcCycles ( FDC_DELAY_US_HEAD_LOAD );	/* Head settle delay */
			break;
		}
		/* If there's no head settle, we fall through directly to the _MOTOR_ON state */
	 case FDCEMU_RUN_READSECTORS_READDATA_MOTOR_ON:
		FDC.ReplaceCommandPossible = false;
		FDC.IndexPulse_Counter = 0;
		FDC.CommandState = FDCEMU_RUN_READSECTORS_READDATA_NEXT_SECTOR_HEADER;
		FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
		break;
	 case FDCEMU_RUN_READSECTORS_READDATA_NEXT_SECTOR_HEADER:
		/* If we're looking for sector FDC.SR for more than 5 revolutions, we abort with RNF */
		if ( FDC.IndexPulse_Counter >= FDC_DELAY_IP_ADDRESS_ID )
		{
			FDC.CommandState = FDCEMU_RUN_READSECTORS_RNF;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
			break;
		}

		if ( FDC.DriveSelSignal < 0 )					/* No drive selected */
			FdcCycles = -1;
		else if ( EmulationDrives[ FDC.DriveSelSignal ].ImageType == FLOPPY_IMAGE_TYPE_STX )
			FdcCycles = FDC_NextSectorID_FdcCycles_STX ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].NumberOfHeads ,
					FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal );
		else
			FdcCycles = FDC_NextSectorID_FdcCycles_ST ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].NumberOfHeads ,
					FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal );
		if ( FdcCycles < 0 )
		{
			FdcCycles = FDC_DELAY_CYCLE_WAIT_NO_DRIVE_FLOPPY;	/* Wait for a valid drive/floppy */
		}
		else
		{
			/* Read bytes to reach the next sector's ID field and skip 10 more bytes to read the whole ID field */
			FdcCycles += FDC_TransferByte_FdcCycles ( 10 );		/* Add delay to read 3xA1, FE, TR, SIDE, SR, LEN, CRC1, CRC2 */
			FDC.CommandState = FDCEMU_RUN_READSECTORS_READDATA_CHECK_SECTOR_HEADER;
		}
		break;
	 case FDCEMU_RUN_READSECTORS_READDATA_CHECK_SECTOR_HEADER:
		/* Check if the current ID Field is the one we're looking for (same track/sector and correct CRC) */
		if ( EmulationDrives[ FDC.DriveSelSignal ].ImageType == FLOPPY_IMAGE_TYPE_STX )
		{
			Next_TR = FDC_NextSectorID_TR_STX ();
			Next_SR = FDC_NextSectorID_SR_STX ();
			Next_CRC_OK = FDC_NextSectorID_CRC_OK_STX ();
		}
		else
		{
			Next_TR = FDC_NextSectorID_TR_ST ();
			Next_SR = FDC_NextSectorID_SR_ST ();
			Next_CRC_OK = FDC_NextSectorID_CRC_OK_ST ();
		}
		if ( ( Next_TR == FDC.TR ) && ( Next_SR == FDC.SR ) && ( Next_CRC_OK ) )
		{
			FDC.CommandState = FDCEMU_RUN_READSECTORS_READDATA_TRANSFER_START;
			/* Read bytes to reach the sector's data : GAP3a + GAP3b + 3xA1 + FB */
			FdcCycles = FDC_TransferByte_FdcCycles ( FDC_TRACK_LAYOUT_STANDARD_GAP3a + FDC_TRACK_LAYOUT_STANDARD_GAP3b + 3 + 1 );
		}
		else
		{
			/* This is not the ID field we're looking for ; check the next one */
			FDC.CommandState = FDCEMU_RUN_READSECTORS_READDATA_NEXT_SECTOR_HEADER;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
		}
		break;
	 case FDCEMU_RUN_READSECTORS_READDATA_TRANSFER_START:
		/* Read a single sector into temporary buffer (512 bytes for ST/MSA) */
		FDC_Buffer_Reset();

		if ( EmulationDrives[ FDC.DriveSelSignal ].ImageType == FLOPPY_IMAGE_TYPE_STX )
			FDC.Status_Temp = FDC_ReadSector_STX ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack ,
				FDC.SR , FDC.SideSignal , &SectorSize );
		else
			FDC.Status_Temp = FDC_ReadSector_ST ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack ,
				FDC.SR , FDC.SideSignal , &SectorSize );

		if ( FDC.Status_Temp & FDC_STR_BIT_RNF )		/* Sector FDC.SR was not found */
		{
			FDC.CommandState = FDCEMU_RUN_READSECTORS_RNF;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
		}
		else
		{
			if ( FDC.Status_Temp & FDC_STR_BIT_RECORD_TYPE )
				FDC_Update_STR ( 0 , FDC_STR_BIT_RECORD_TYPE );
			else
				FDC_Update_STR ( FDC_STR_BIT_RECORD_TYPE , 0 );

			FDC.CommandState = FDCEMU_RUN_READSECTORS_READDATA_TRANSFER_LOOP;
			FdcCycles = FDC_Buffer_Read_Timing ();		/* Delay to transfer the first byte */
		}
		break;
	 case FDCEMU_RUN_READSECTORS_READDATA_TRANSFER_LOOP:
		/* Transfer the sector 1 byte at a time using DMA */
		FDC_DMA_FIFO_Push ( FDC_Buffer_Read_Byte () );		/* Add 1 byte to the DMA FIFO */
		if ( FDC_BUFFER.PosRead < FDC_Buffer_Get_Size () )
		{
			FdcCycles = FDC_Buffer_Read_Timing ();		/* Delay to transfer the next byte */
		}
		else							/* Sector transferred, check the CRC */
		{
			FDC.CommandState = FDCEMU_RUN_READSECTORS_CRC;
			FdcCycles = FDC_TransferByte_FdcCycles ( 2 );	/* Read 2 bytes for CRC */
		}
		break;
	 case FDCEMU_RUN_READSECTORS_CRC:
		/* Sector completely transferred, CRC is always good for ST/MSA, but not always for STX */
		if ( FDC.Status_Temp & FDC_STR_BIT_CRC_ERROR )
		{
			LOG_TRACE(TRACE_FDC, "fdc type II read sector=%d track=0x%x side=%d drive=%d CRC VBL=%d video_cyc=%d %d@%d pc=%x\n",
				  FDC.SR , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal , FDC.DriveSelSignal , nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

			FDC_Update_STR ( 0 , FDC_STR_BIT_CRC_ERROR );
			FdcCycles = FDC_CmdCompleteCommon( true );
		}
		else
		{
			FDC.CommandState = FDCEMU_RUN_READSECTORS_MULTI;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
		}
		break;
	 case FDCEMU_RUN_READSECTORS_MULTI:
		/* Check for multi bit */
		if ( FDC.CR & FDC_COMMAND_BIT_MULTIPLE_SECTOR  )
		{
			FDC.SR++;					/* Try to read next sector and set RNF if not possible */
			FDC.CommandState = FDCEMU_RUN_READSECTORS_READDATA_MOTOR_ON;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
			LOG_TRACE(TRACE_FDC, "fdc type II read sector with multi sector=0x%x track=0x%x side=%d drive=%d addr=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
				FDC.SR, FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal , FDC.DriveSelSignal ,
				FDC_GetDMAAddress(), nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());
		}
		else							/* Multi=0, stop here with no error */
		{
			FDC.CommandState = FDCEMU_RUN_READSECTORS_COMPLETE;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_COMPLETE;
		}
		break;
	 case FDCEMU_RUN_READSECTORS_RNF:
		LOG_TRACE(TRACE_FDC, "fdc type II read sector=%d track=0x%x side=%d drive=%d RNF VBL=%d video_cyc=%d %d@%d pc=%x\n",
			  FDC.SR , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal , FDC.DriveSelSignal , nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

		FDC_Update_STR ( 0 , FDC_STR_BIT_RNF );
		FdcCycles = FDC_CmdCompleteCommon( true );
		break;
	 case FDCEMU_RUN_READSECTORS_COMPLETE:
		FdcCycles = FDC_CmdCompleteCommon( true );
		break;
	}

	return FdcCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Run 'WRITE SECTOR/S' command
 */
static int FDC_UpdateWriteSectorsCmd ( void )
{
	int	FdcCycles = 0;
	int	FrameCycles, HblCounterVideo, LineCycles;
	uint8_t	Next_TR;
	uint8_t	Next_SR;
	uint8_t	Next_LEN;
	uint8_t	Next_CRC_OK;
	uint8_t	Byte;
	uint8_t	Status;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	/* Stop now if disk is write protected */
	if ( ( FDC.DriveSelSignal >= 0 ) && ( FDC_DRIVES[ FDC.DriveSelSignal ].Enabled )
		&& ( FDC_DRIVES[ FDC.DriveSelSignal ].DiskInserted )
		&& ( Floppy_IsWriteProtected ( FDC.DriveSelSignal ) ) )
	{
		LOG_TRACE(TRACE_FDC, "fdc type II write sector=%d track=0x%x side=%d drive=%d WPRT VBL=%d video_cyc=%d %d@%d pc=%x\n",
			  FDC.SR , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal , FDC.DriveSelSignal , nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

		FDC_Update_STR ( 0 , FDC_STR_BIT_WPRT );		/* Set WPRT bit */
		FdcCycles = FDC_CmdCompleteCommon( true );
	}
	else
		FDC_Update_STR ( FDC_STR_BIT_WPRT , 0 );		/* Unset WPRT bit */

	
	/* Which command is running? */
	switch (FDC.CommandState)
	{
	 case FDCEMU_RUN_WRITESECTORS_WRITEDATA:
		if ( FDC_Set_MotorON ( FDC.CR ) )
		{
			FDC.CommandState = FDCEMU_RUN_WRITESECTORS_WRITEDATA_SPIN_UP;
			FdcCycles = FDC_DELAY_CYCLE_REFRESH_INDEX_PULSE;	/* Spin up needed */
		}
		else
		{
			FDC.CommandState = FDCEMU_RUN_WRITESECTORS_WRITEDATA_HEAD_LOAD;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;		/* No spin up needed */
		}
		break;
	 case FDCEMU_RUN_WRITESECTORS_WRITEDATA_SPIN_UP:
		if ( FDC.IndexPulse_Counter < FDC_DELAY_IP_SPIN_UP )
		{
			FdcCycles = FDC_DELAY_CYCLE_REFRESH_INDEX_PULSE;	/* Wait for the correct number of IP */
			break;
		}
		/* If IndexPulse_Counter reached, we fall through directly to the _HEAD_LOAD state */
	 case FDCEMU_RUN_WRITESECTORS_WRITEDATA_HEAD_LOAD:
		if ( FDC.CR & FDC_COMMAND_BIT_HEAD_LOAD )
		{
			FDC.CommandState = FDCEMU_RUN_WRITESECTORS_WRITEDATA_MOTOR_ON;
			FdcCycles = FDC_DelayToFdcCycles ( FDC_DELAY_US_HEAD_LOAD );	/* Head settle delay */
			break;
		}
		/* If there's no head settle, we fall through directly to the _MOTOR_ON state */
	 case FDCEMU_RUN_WRITESECTORS_WRITEDATA_MOTOR_ON:
		FDC.ReplaceCommandPossible = false;
		FDC.IndexPulse_Counter = 0;
		FDC.CommandState = FDCEMU_RUN_WRITESECTORS_WRITEDATA_NEXT_SECTOR_HEADER;
		FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
		break;
	 case FDCEMU_RUN_WRITESECTORS_WRITEDATA_NEXT_SECTOR_HEADER:
		/* If we're looking for sector FDC.SR for more than 5 revolutions, we abort with RNF */
		if ( FDC.IndexPulse_Counter >= FDC_DELAY_IP_ADDRESS_ID )
		{
			FDC.CommandState = FDCEMU_RUN_WRITESECTORS_RNF;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
			break;
		}

		if ( FDC.DriveSelSignal < 0 )					/* No drive selected */
			FdcCycles = -1;
		else if ( EmulationDrives[ FDC.DriveSelSignal ].ImageType == FLOPPY_IMAGE_TYPE_STX )
			FdcCycles = FDC_NextSectorID_FdcCycles_STX ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].NumberOfHeads ,
					FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal );
		else
			FdcCycles = FDC_NextSectorID_FdcCycles_ST ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].NumberOfHeads ,
					FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal );
		if ( FdcCycles < 0 )
		{
			FdcCycles = FDC_DELAY_CYCLE_WAIT_NO_DRIVE_FLOPPY;	/* Wait for a valid drive/floppy */
		}
		else
		{
			/* Read bytes to reach the next sector's ID field and skip 10 more bytes to read the whole ID field */
			FdcCycles += FDC_TransferByte_FdcCycles ( 10 );		/* Add delay to read 3xA1, FE, TR, SIDE, SR, LEN, CRC1, CRC2 */
			FDC.CommandState = FDCEMU_RUN_WRITESECTORS_WRITEDATA_CHECK_SECTOR_HEADER;
		}
		break;
	 case FDCEMU_RUN_WRITESECTORS_WRITEDATA_CHECK_SECTOR_HEADER:
		/* Check if the current ID Field is the one we're looking for (same track/sector and correct CRC) */
		if ( EmulationDrives[ FDC.DriveSelSignal ].ImageType == FLOPPY_IMAGE_TYPE_STX )
		{
			Next_TR = FDC_NextSectorID_TR_STX ();
			Next_SR = FDC_NextSectorID_SR_STX ();
			Next_CRC_OK = FDC_NextSectorID_CRC_OK_STX ();
		}
		else
		{
			Next_TR = FDC_NextSectorID_TR_ST ();
			Next_SR = FDC_NextSectorID_SR_ST ();
			Next_CRC_OK = FDC_NextSectorID_CRC_OK_ST ();
		}
		if ( ( Next_TR == FDC.TR ) && ( Next_SR == FDC.SR ) && ( Next_CRC_OK ) )
		{
			FDC.CommandState = FDCEMU_RUN_WRITESECTORS_WRITEDATA_TRANSFER_START;
			/* Read bytes to reach the sector's data : GAP3a + GAP3b + 3xA1 + FB */
			FdcCycles = FDC_TransferByte_FdcCycles ( FDC_TRACK_LAYOUT_STANDARD_GAP3a + FDC_TRACK_LAYOUT_STANDARD_GAP3b + 3 + 1 );
		}
		else
		{
			/* This is not the ID field we're looking for ; check the next one */
			FDC.CommandState = FDCEMU_RUN_WRITESECTORS_WRITEDATA_NEXT_SECTOR_HEADER;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
		}
		break;
	 case FDCEMU_RUN_WRITESECTORS_WRITEDATA_TRANSFER_START:
		/* Write a single sector from RAM (512 bytes for ST/MSA) */
		if ( EmulationDrives[ FDC.DriveSelSignal ].ImageType == FLOPPY_IMAGE_TYPE_STX )
			Next_LEN = FDC_NextSectorID_LEN_STX ();
		else
			Next_LEN = FDC_NextSectorID_LEN_ST ();

		FDC_Buffer_Reset();
		FDC_DMA.BytesToTransfer = 128 << ( Next_LEN & FDC_SECTOR_SIZE_MASK );

		FDC.CommandState = FDCEMU_RUN_WRITESECTORS_WRITEDATA_TRANSFER_LOOP;
		FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
		break;
	 case FDCEMU_RUN_WRITESECTORS_WRITEDATA_TRANSFER_LOOP:
		/* Transfer the sector 1 byte at a time using DMA */
		if ( FDC_DMA.BytesToTransfer-- > 0 )
		{
			Byte = FDC_DMA_FIFO_Pull ();			/* Get 1 byte from the DMA FIFO */
//fprintf ( stderr , "byte %d %x\n" , FDC_DMA.BytesToTransfer , Byte );
			FDC_Buffer_Add ( Byte );
			FdcCycles = FDC_TransferByte_FdcCycles ( 1 );
		}
		else							/* Sector transferred, add the CRC */
		{
			FDC.CommandState = FDCEMU_RUN_WRITESECTORS_CRC;
			FdcCycles = FDC_TransferByte_FdcCycles ( 2 );	/* Write 2 bytes for CRC */
		}
		break;
	 case FDCEMU_RUN_WRITESECTORS_CRC:
		/* Sector completely transferred, CRC is always good for ST/MSA */
		/* This is where we save the buffer to the disk image */

		if ( EmulationDrives[ FDC.DriveSelSignal ].ImageType == FLOPPY_IMAGE_TYPE_STX )
			Status = FDC_WriteSector_STX ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack ,
				FDC.SR , FDC.SideSignal , FDC_Buffer_Get_Size () );
		else
			Status = FDC_WriteSector_ST ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack ,
				FDC.SR , FDC.SideSignal , FDC_Buffer_Get_Size () );

		if ( Status & FDC_STR_BIT_RNF )				/* Sector FDC.SR was not correctly written */
		{
			FDC.CommandState = FDCEMU_RUN_WRITESECTORS_RNF;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
		}
		else
		{
			FDC.CommandState = FDCEMU_RUN_WRITESECTORS_MULTI;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
		}
		break;
	 case FDCEMU_RUN_WRITESECTORS_MULTI:
		/* Check for multi bit */
		if ( FDC.CR & FDC_COMMAND_BIT_MULTIPLE_SECTOR  )
		{
			FDC.SR++;					/* Try to write next sector and set RNF if not possible */
			FDC.CommandState = FDCEMU_RUN_WRITESECTORS_WRITEDATA_MOTOR_ON;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
			LOG_TRACE(TRACE_FDC, "fdc type II write sector with multi sector=0x%x track=0x%x side=%d drive=%d addr=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
				FDC.SR, FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal , FDC.DriveSelSignal ,
				FDC_GetDMAAddress(), nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());
		}
		else							/* Multi=0, stop here with no error */
		{
			FDC.CommandState = FDCEMU_RUN_WRITESECTORS_COMPLETE;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_COMPLETE;
		}
		break;
	 case FDCEMU_RUN_WRITESECTORS_RNF:
		LOG_TRACE(TRACE_FDC, "fdc type II write sector=%d track=0x%x side=%d drive=%d RNF VBL=%d video_cyc=%d %d@%d pc=%x\n",
			  FDC.SR , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal , FDC.DriveSelSignal , nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

		FDC_Update_STR ( 0 , FDC_STR_BIT_RNF );
		FdcCycles = FDC_CmdCompleteCommon( true );
		break;
	 case FDCEMU_RUN_WRITESECTORS_COMPLETE:
		FdcCycles = FDC_CmdCompleteCommon( true );
		break;
	}

	return FdcCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Run 'READ ADDRESS' command
 */
static int FDC_UpdateReadAddressCmd ( void )
{
	int	FdcCycles = 0;
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	/* Which command is running? */
	switch (FDC.CommandState)
	{
	 case FDCEMU_RUN_READADDRESS:
		if ( FDC_Set_MotorON ( FDC.CR ) )
		{
			FDC.CommandState = FDCEMU_RUN_READADDRESS_SPIN_UP;
			FdcCycles = FDC_DELAY_CYCLE_REFRESH_INDEX_PULSE;	/* Spin up needed */
		}
		else
		{
			FDC.CommandState = FDCEMU_RUN_READADDRESS_HEAD_LOAD;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;		/* No spin up needed */
		}
		break;
	 case FDCEMU_RUN_READADDRESS_SPIN_UP:
		if ( FDC.IndexPulse_Counter < FDC_DELAY_IP_SPIN_UP )
		{
			FdcCycles = FDC_DELAY_CYCLE_REFRESH_INDEX_PULSE;	/* Wait for the correct number of IP */
			break;
		}
		/* If IndexPulse_Counter reached, we fall through directly to the _HEAD_LOAD state */
	 case FDCEMU_RUN_READADDRESS_HEAD_LOAD:
		FDC.ReplaceCommandPossible = false;
		if ( FDC.CR & FDC_COMMAND_BIT_HEAD_LOAD )
		{
			FDC.CommandState = FDCEMU_RUN_READADDRESS_MOTOR_ON;
			FdcCycles = FDC_DelayToFdcCycles ( FDC_DELAY_US_HEAD_LOAD );	/* Head settle delay */
			break;
		}
		/* If there's no head settle, we fall through directly to the _MOTOR_ON state */
	 case FDCEMU_RUN_READADDRESS_MOTOR_ON:
		FDC.ReplaceCommandPossible = false;
		FDC.IndexPulse_Counter = 0;
		FDC.CommandState = FDCEMU_RUN_READADDRESS_NEXT_SECTOR_HEADER;
		FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
		break;
	 case FDCEMU_RUN_READADDRESS_NEXT_SECTOR_HEADER:
		/* If we don't find a sector header after more than 5 revolutions, we abort with RNF */
		if ( FDC.IndexPulse_Counter >= FDC_DELAY_IP_ADDRESS_ID )
		{
			FDC.CommandState = FDCEMU_RUN_READADDRESS_RNF;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
			break;
		}

		if ( FDC.DriveSelSignal < 0 )					/* No drive selected */
			FdcCycles = -1;
		else if ( EmulationDrives[ FDC.DriveSelSignal ].ImageType == FLOPPY_IMAGE_TYPE_STX )
			FdcCycles = FDC_NextSectorID_FdcCycles_STX ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].NumberOfHeads ,
					FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal );
		else
			FdcCycles = FDC_NextSectorID_FdcCycles_ST ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].NumberOfHeads ,
					FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal );
		if ( FdcCycles < 0 )
		{
			FdcCycles = FDC_DELAY_CYCLE_WAIT_NO_DRIVE_FLOPPY;	/* Wait for a valid drive/floppy */
		}
		else
		{
			/* Read bytes to reach the next sector's ID field */
			FdcCycles += FDC_TransferByte_FdcCycles ( 4 );		/* Add delay to read 3xA1, FE */
			FDC.CommandState = FDCEMU_RUN_READADDRESS_TRANSFER_START;
		}
		break;
	 case FDCEMU_RUN_READADDRESS_TRANSFER_START:
		/* Read the ID field into buffer */
		FDC_Buffer_Reset();

		if ( EmulationDrives[ FDC.DriveSelSignal ].ImageType == FLOPPY_IMAGE_TYPE_STX )
			FDC.Status_Temp = FDC_ReadAddress_STX ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack ,
				     FDC_NextSectorID_SR_STX () , FDC.SideSignal );
		else
			FDC.Status_Temp = FDC_ReadAddress_ST ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack ,
				     FDC_NextSectorID_SR_ST () , FDC.SideSignal );

		FDC.SR = FDC_Buffer_Read_Byte_pos ( 0 );		/* The 1st byte of the ID field is also copied into Sector Register */

		FDC.CommandState = FDCEMU_RUN_READADDRESS_TRANSFER_LOOP;
		FdcCycles = FDC_Buffer_Read_Timing ();			/* Delay to transfer the first byte */
		break;
	 case FDCEMU_RUN_READADDRESS_TRANSFER_LOOP:
		/* Transfer the ID field 1 byte at a time using DMA */
		FDC_DMA_FIFO_Push ( FDC_Buffer_Read_Byte () );		/* Add 1 byte to the DMA FIFO */
		if ( FDC_BUFFER.PosRead < FDC_Buffer_Get_Size () )
		{
			FdcCycles = FDC_Buffer_Read_Timing ();		/* Delay to transfer the next byte */
		}
		else
		{
			FDC.CommandState = FDCEMU_RUN_READADDRESS_COMPLETE;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_COMPLETE;
		}
		break;
	 case FDCEMU_RUN_READADDRESS_RNF:
		LOG_TRACE(TRACE_FDC, "fdc type III read address track=0x%x side=%d drive=%d RNF VBL=%d video_cyc=%d %d@%d pc=%x\n",
			  FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal , FDC.DriveSelSignal , nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

		FDC_Update_STR ( 0 , FDC_STR_BIT_RNF );
		FdcCycles = FDC_CmdCompleteCommon( true );
		break;
	 case FDCEMU_RUN_READADDRESS_COMPLETE:
		FdcCycles = FDC_CmdCompleteCommon( true );
		break;
	}

	return FdcCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Run 'READ TRACK' command
 */
static int FDC_UpdateReadTrackCmd ( void )
{
	int	FdcCycles = 0;
	int	i;
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	/* Which command is running? */
	switch (FDC.CommandState)
	{
	 case FDCEMU_RUN_READTRACK:
		if ( FDC_Set_MotorON ( FDC.CR ) )
		{
			FDC.CommandState = FDCEMU_RUN_READTRACK_SPIN_UP;
			FdcCycles = FDC_DELAY_CYCLE_REFRESH_INDEX_PULSE;	/* Spin up needed */
		}
		else
		{
			FDC.CommandState = FDCEMU_RUN_READTRACK_HEAD_LOAD;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;		/* No spin up needed */
		}
		break;
	 case FDCEMU_RUN_READTRACK_SPIN_UP:
		if ( FDC.IndexPulse_Counter < FDC_DELAY_IP_SPIN_UP )
		{
			FdcCycles = FDC_DELAY_CYCLE_REFRESH_INDEX_PULSE;	/* Wait for the correct number of IP */
			break;
		}
		/* If IndexPulse_Counter reached, we fall through directly to the _HEAD_LOAD state */
	 case FDCEMU_RUN_READTRACK_HEAD_LOAD:
		FDC.ReplaceCommandPossible = false;
		if ( FDC.CR & FDC_COMMAND_BIT_HEAD_LOAD )
		{
			FDC.CommandState = FDCEMU_RUN_READTRACK_MOTOR_ON;
			FdcCycles = FDC_DelayToFdcCycles ( FDC_DELAY_US_HEAD_LOAD );	/* Head settle delay */
			break;
		}
		/* If there's no head settle, we fall through directly to the _MOTOR_ON state */
	 case FDCEMU_RUN_READTRACK_MOTOR_ON:
		FdcCycles = FDC_NextIndexPulse_FdcCycles ();		/* Wait for the next index pulse */
//fprintf ( stderr , "read tr idx=%d %d\n" , FDC_IndexPulse_GetState() , FdcCycles );
		if ( FdcCycles < 0 )
		{
			FdcCycles = FDC_DELAY_CYCLE_WAIT_NO_DRIVE_FLOPPY;	/* Wait for a valid drive/floppy */
		}
		else
		{
			FDC.CommandState = FDCEMU_RUN_READTRACK_INDEX;
		}
		break;
	 case FDCEMU_RUN_READTRACK_INDEX:
		/* At this point, we have a valid drive/floppy, build the track data */
		FDC_Buffer_Reset();

		if ( ( ( FDC.SideSignal == 1 )				/* Try to read side 1 on a disk that doesn't have 2 sides or drive is single sided */
			&& ( ( FDC_GetSidesPerDisk ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack ) != 2 )
			  || ( FDC_DRIVES[ FDC.DriveSelSignal ].NumberOfHeads == 1 ) ) )
		    || ( FDC_MachineHandleDensity ( FDC.DriveSelSignal ) == false ) ) 	/* Can't handle the floppy's density */
		{
			LOG_TRACE(TRACE_FDC, "fdc type III read track drive=%d track=%d side=%d, side not found or wrong density VBL=%d video_cyc=%d %d@%d pc=%x\n",
				  FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal ,
				  nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

			for ( i=0 ; i<FDC_GetBytesPerTrack ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal ) ; i++ )
				FDC_Buffer_Add ( Hatari_rand() & 0xff ); /* Fill the track buffer with random bytes */
		}
		else if ( EmulationDrives[ FDC.DriveSelSignal ].ImageType == FLOPPY_IMAGE_TYPE_STX )
		{
			FDC.Status_Temp = FDC_ReadTrack_STX ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal );
		}
		else							/* Track/side available in the disk image */
		{
			FDC.Status_Temp = FDC_ReadTrack_ST ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal );
		}

		FDC.CommandState = FDCEMU_RUN_READTRACK_TRANSFER_LOOP;
		FdcCycles = FDC_Buffer_Read_Timing ();			/* Delay to transfer the first byte */
		break;
	 case FDCEMU_RUN_READTRACK_TRANSFER_LOOP:
		/* Transfer the track 1 byte at a time using DMA */
		FDC_DMA_FIFO_Push ( FDC_Buffer_Read_Byte () );		/* Add 1 byte to the DMA FIFO */
		if ( FDC_BUFFER.PosRead < FDC_Buffer_Get_Size () )
		{
			FdcCycles = FDC_Buffer_Read_Timing ();		/* Delay to transfer the next byte */
		}
		else							/* Track completely transferred */
		{
			FDC.CommandState = FDCEMU_RUN_READTRACK_COMPLETE;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_COMPLETE;
		}
		break;
	 case FDCEMU_RUN_READTRACK_COMPLETE:
		FdcCycles = FDC_CmdCompleteCommon( true );
		break;
	}

	return FdcCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Run 'WRITE TRACK' command
 */
static int FDC_UpdateWriteTrackCmd ( void )
{
	int	FdcCycles = 0;
	int	FrameCycles, HblCounterVideo, LineCycles;
	uint8_t	Byte;
	uint8_t	Status;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
	
	/* Which command is running? */
	switch (FDC.CommandState)
	{
	 case FDCEMU_RUN_WRITETRACK:
		if ( FDC_Set_MotorON ( FDC.CR ) )
		{
			FDC.CommandState = FDCEMU_RUN_WRITETRACK_SPIN_UP;
			FdcCycles = FDC_DELAY_CYCLE_REFRESH_INDEX_PULSE;	/* Spin up needed */
		}
		else
		{
			FDC.CommandState = FDCEMU_RUN_WRITETRACK_HEAD_LOAD;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;		/* No spin up needed */
		}
		break;
	 case FDCEMU_RUN_WRITETRACK_SPIN_UP:
		if ( FDC.IndexPulse_Counter < FDC_DELAY_IP_SPIN_UP )
		{
			FdcCycles = FDC_DELAY_CYCLE_REFRESH_INDEX_PULSE;	/* Wait for the correct number of IP */
			break;
		}
		/* If IndexPulse_Counter reached, we fall through directly to the _HEAD_LOAD state */
	 case FDCEMU_RUN_WRITETRACK_HEAD_LOAD:
		FDC.ReplaceCommandPossible = false;
		if ( FDC.CR & FDC_COMMAND_BIT_HEAD_LOAD )
		{
			FDC.CommandState = FDCEMU_RUN_WRITETRACK_MOTOR_ON;
			FdcCycles = FDC_DelayToFdcCycles ( FDC_DELAY_US_HEAD_LOAD );	/* Head settle delay */
			break;
		}
		/* If there's no head settle, we fall through directly to the _MOTOR_ON state */
	 case FDCEMU_RUN_WRITETRACK_MOTOR_ON:
		FdcCycles = FDC_NextIndexPulse_FdcCycles ();		/* Wait for the next index pulse */
//fprintf ( stderr , "write tr idx=%d %d\n" , FDC_IndexPulse_GetState() , FdcCycles );
		if ( FdcCycles < 0 )
		{
			FdcCycles = FDC_DELAY_CYCLE_WAIT_NO_DRIVE_FLOPPY;	/* Wait for a valid drive/floppy */
		}
		else
		{
			FDC.CommandState = FDCEMU_RUN_WRITETRACK_INDEX;
		}
		break;
	 case FDCEMU_RUN_WRITETRACK_INDEX:
		if ( FDC_MachineHandleDensity ( FDC.DriveSelSignal ) == false )	/* Can't handle the floppy's density */
		{
			LOG_TRACE(TRACE_FDC, "fdc type III write track drive=%d track=0x%x side=%d wrong density VBL=%d video_cyc=%d %d@%d pc=%x\n",
				  FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal , nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

			FDC_Update_STR ( 0 , FDC_STR_BIT_LOST_DATA );	/* Set LOST_DATA bit */
			FdcCycles = FDC_CmdCompleteCommon( true );
			break;
		}

		/* At this point, we have a valid drive/floppy, check write protection and write the track data */
		if ( Floppy_IsWriteProtected ( FDC.DriveSelSignal ) )
		{
			LOG_TRACE(TRACE_FDC, "fdc type III write track drive=%d track=0x%x side=%d WPRT VBL=%d video_cyc=%d %d@%d pc=%x\n",
				  FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal , nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

			FDC_Update_STR ( 0 , FDC_STR_BIT_WPRT );	/* Set WPRT bit */
			FdcCycles = FDC_CmdCompleteCommon( true );
			break;
		}

		FDC_Update_STR ( FDC_STR_BIT_WPRT , 0 );		/* Unset WPRT bit */

		FDC_Buffer_Reset();
		FDC_DMA.BytesToTransfer = FDC_GetBytesPerTrack ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack , FDC.SideSignal );

		FDC.CommandState = FDCEMU_RUN_WRITETRACK_TRANSFER_LOOP;
		FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
		break;
	 case FDCEMU_RUN_WRITETRACK_TRANSFER_LOOP:
		/* Transfer the track 1 byte at a time using DMA */
		if ( FDC_DMA.BytesToTransfer-- > 0 )
		{
			Byte = FDC_DMA_FIFO_Pull ();			/* Get 1 byte from the DMA FIFO */
//fprintf ( stderr , "byte %d %x\n" , FDC_DMA.BytesToTransfer , Byte );
			FDC_Buffer_Add ( Byte );
			FdcCycles = FDC_TransferByte_FdcCycles ( 1 );
		}
		else							/* Track written */
		{
			FDC.CommandState = FDCEMU_RUN_WRITETRACK_COMPLETE;
			FdcCycles = FDC_DELAY_CYCLE_COMMAND_IMMEDIATE;
		}
		break;
	 case FDCEMU_RUN_WRITETRACK_COMPLETE:
		/* Track completely transferred */
		/* This is where we save the buffer to the disk image */
		if ( EmulationDrives[ FDC.DriveSelSignal ].ImageType == FLOPPY_IMAGE_TYPE_STX )
			Status = FDC_WriteTrack_STX ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack ,
				FDC.SideSignal , FDC_Buffer_Get_Size () );
		else
			Status = FDC_WriteTrack_ST ( FDC.DriveSelSignal , FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack ,
				FDC.SideSignal , FDC_Buffer_Get_Size () );

		if ( Status & FDC_STR_BIT_LOST_DATA )			/* Error while writing */
			FDC_Update_STR ( 0 , FDC_STR_BIT_LOST_DATA );	/* Set LOST_DATA bit */

		FdcCycles = FDC_CmdCompleteCommon( true );
		break;
	}

	return FdcCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Common to types I, II and III
 *
 * Start motor / spin up sequence if needed
 * Return true if spin up sequence is needed, else false
 */

static bool FDC_Set_MotorON ( uint8_t FDC_CR )
{
	int	FrameCycles, HblCounterVideo, LineCycles;
	bool	SpinUp;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	if ( ( ( FDC_CR & FDC_COMMAND_BIT_SPIN_UP ) == 0 )		/* Command wants motor's spin up */
	  && ( ( FDC.STR & FDC_STR_BIT_MOTOR_ON ) == 0 ) )		/* Motor on not enabled yet */
	{
		LOG_TRACE(TRACE_FDC, "fdc start motor with spinup VBL=%d video_cyc=%d %d@%d pc=%x\n",
			nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

		FDC_Update_STR ( FDC_STR_BIT_SPIN_UP , 0 );		/* Unset spin up bit */
		FDC.IndexPulse_Counter = 0;				/* Reset counter to measure the spin up sequence */
		SpinUp = true;
	}
	else								/* No spin up : don't add delay to start the motor */
	{
		LOG_TRACE(TRACE_FDC, "fdc start motor without spinup VBL=%d video_cyc=%d %d@%d pc=%x\n",
			nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

		SpinUp = false;
	}

	FDC_Update_STR ( 0 , FDC_STR_BIT_MOTOR_ON );			/* Start motor */

	if ( ( FDC.DriveSelSignal < 0 ) || ( !FDC_DRIVES[ FDC.DriveSelSignal ].Enabled )
		|| ( !FDC_DRIVES[ FDC.DriveSelSignal ].DiskInserted ) )
	{
		LOG_TRACE(TRACE_FDC, "fdc start motor : no disk/drive VBL=%d video_cyc=%d %d@%d pc=%x\n",
			nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());
	}
	else if ( FDC_DRIVES[ FDC.DriveSelSignal ].IndexPulse_Time == 0 )
		FDC_IndexPulse_Init ( FDC.DriveSelSignal );		/* Index Pulse's position is random when motor starts */
	
	return SpinUp;
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

	LOG_TRACE(TRACE_FDC, "fdc type I restore spinup=%s verify=%s steprate_ms=%d drive=%d tr=0x%x head_track=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  ( FDC.CR & FDC_COMMAND_BIT_SPIN_UP ) ? "off" : "on" ,
		  ( FDC.CR & FDC_COMMAND_BIT_VERIFY ) ? "on" : "off" ,
		  FDC_StepRate_ms[ FDC_STEP_RATE ] ,
		  FDC.DriveSelSignal , FDC.TR , FDC.DriveSelSignal >= 0 ? FDC_DRIVES[ FDC.DriveSelSignal].HeadTrack : -1 ,
		  nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to seek to track zero */
	FDC.Command = FDCEMU_CMD_RESTORE;
	FDC.CommandState = FDCEMU_RUN_RESTORE_SEEKTOTRACKZERO;

	FDC_Update_STR ( FDC_STR_BIT_INDEX | FDC_STR_BIT_CRC_ERROR | FDC_STR_BIT_RNF , FDC_STR_BIT_BUSY );

	return FDC_DELAY_CYCLE_TYPE_I_PREPARE;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeI_Seek ( void )
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type I seek dest_track=0x%x spinup=%s verify=%s steprate_ms=%d drive=%d tr=0x%x head_track=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  FDC.DR,
		  ( FDC.CR & FDC_COMMAND_BIT_SPIN_UP ) ? "off" : "on" ,
		  ( FDC.CR & FDC_COMMAND_BIT_VERIFY ) ? "on" : "off" ,
		  FDC_StepRate_ms[ FDC_STEP_RATE ] ,
		  FDC.DriveSelSignal , FDC.TR , FDC.DriveSelSignal >= 0 ? FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack : -1 ,
		  nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to seek to chosen track */
	FDC.Command = FDCEMU_CMD_SEEK;
	FDC.CommandState = FDCEMU_RUN_SEEK_TOTRACK;

	FDC_Update_STR ( FDC_STR_BIT_INDEX | FDC_STR_BIT_CRC_ERROR | FDC_STR_BIT_RNF , FDC_STR_BIT_BUSY );

	return FDC_DELAY_CYCLE_TYPE_I_PREPARE;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeI_Step ( void )
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type I step %d spinup=%s verify=%s steprate_ms=%d drive=%d tr=0x%x head_track=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  FDC.StepDirection,
		  ( FDC.CR & FDC_COMMAND_BIT_SPIN_UP ) ? "off" : "on" ,
		  ( FDC.CR & FDC_COMMAND_BIT_VERIFY ) ? "on" : "off" ,
		  FDC_StepRate_ms[ FDC_STEP_RATE ] ,
		  FDC.DriveSelSignal , FDC.TR , FDC.DriveSelSignal >= 0 ? FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack : -1 ,
		  nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to step (using same direction as latest seek executed, ie 'FDC.StepDirection') */
	FDC.Command = FDCEMU_CMD_STEP;
	FDC.CommandState = FDCEMU_RUN_STEP_ONCE;

	FDC_Update_STR ( FDC_STR_BIT_INDEX | FDC_STR_BIT_CRC_ERROR | FDC_STR_BIT_RNF , FDC_STR_BIT_BUSY );

	return FDC_DELAY_CYCLE_TYPE_I_PREPARE;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeI_StepIn(void)
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type I step in spinup=%s verify=%s steprate_ms=%d drive=%d tr=0x%x head_track=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  ( FDC.CR & FDC_COMMAND_BIT_SPIN_UP ) ? "off" : "on" ,
		  ( FDC.CR & FDC_COMMAND_BIT_VERIFY ) ? "on" : "off" ,
		  FDC_StepRate_ms[ FDC_STEP_RATE ] ,
		  FDC.DriveSelSignal , FDC.TR , FDC.DriveSelSignal >= 0 ? FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack : -1 ,
		  nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to step in (direction = +1) */
	FDC.Command = FDCEMU_CMD_STEP;
	FDC.CommandState = FDCEMU_RUN_STEP_ONCE;
	FDC.StepDirection = 1;						/* Increment track*/

	FDC_Update_STR ( FDC_STR_BIT_INDEX | FDC_STR_BIT_CRC_ERROR | FDC_STR_BIT_RNF , FDC_STR_BIT_BUSY );

	return FDC_DELAY_CYCLE_TYPE_I_PREPARE;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeI_StepOut ( void )
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type I step out spinup=%s verify=%s steprate_ms=%d drive=%d tr=0x%x head_track=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  ( FDC.CR & FDC_COMMAND_BIT_SPIN_UP ) ? "off" : "on" ,
		  ( FDC.CR & FDC_COMMAND_BIT_VERIFY ) ? "on" : "off" ,
		  FDC_StepRate_ms[ FDC_STEP_RATE ] ,
		  FDC.DriveSelSignal , FDC.TR , FDC.DriveSelSignal >= 0 ? FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack : -1 ,
		  nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to step out (direction = -1) */
	FDC.Command = FDCEMU_CMD_STEP;
	FDC.CommandState = FDCEMU_RUN_STEP_ONCE;
	FDC.StepDirection = -1;						/* Decrement track */

	FDC_Update_STR ( FDC_STR_BIT_INDEX | FDC_STR_BIT_CRC_ERROR | FDC_STR_BIT_RNF , FDC_STR_BIT_BUSY );

	return FDC_DELAY_CYCLE_TYPE_I_PREPARE;
}


/*-----------------------------------------------------------------------*/
/**
 * Type II Commands
 *
 * Read Sector, Write Sector
 */


/*-----------------------------------------------------------------------*/
static int FDC_TypeII_ReadSector ( void )
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type II read sector sector=0x%x multi=%s spinup=%s settle=%s tr=0x%x head_track=0x%x side=%d drive=%d dmasector=%d addr=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  FDC.SR, ( FDC.CR & FDC_COMMAND_BIT_MULTIPLE_SECTOR ) ? "on" : "off" ,
		  ( FDC.CR & FDC_COMMAND_BIT_SPIN_UP ) ? "off" : "on" ,
		  ( FDC.CR & FDC_COMMAND_BIT_HEAD_LOAD ) ? "on" : "off" ,
		  FDC.TR , FDC.DriveSelSignal >= 0 ? FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack : -1 ,
		  FDC.SideSignal , FDC.DriveSelSignal , FDC_DMA.SectorCount ,
		  FDC_GetDMAAddress(), nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to read sector(s) */
	FDC.Command = FDCEMU_CMD_READSECTORS;
	FDC.CommandState = FDCEMU_RUN_READSECTORS_READDATA;

	FDC_Update_STR ( FDC_STR_BIT_DRQ | FDC_STR_BIT_LOST_DATA | FDC_STR_BIT_CRC_ERROR
		| FDC_STR_BIT_RNF | FDC_STR_BIT_RECORD_TYPE | FDC_STR_BIT_WPRT , FDC_STR_BIT_BUSY );

	return FDC_DELAY_CYCLE_TYPE_II_PREPARE;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeII_WriteSector ( void )
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type II write sector sector=0x%x multi=%s spinup=%s settle=%s tr=0x%x head_track=0x%x side=%d drive=%d dmasector=%d addr=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  FDC.SR, ( FDC.CR & FDC_COMMAND_BIT_MULTIPLE_SECTOR ) ? "on" : "off" ,
		  ( FDC.CR & FDC_COMMAND_BIT_SPIN_UP ) ? "off" : "on" ,
		  ( FDC.CR & FDC_COMMAND_BIT_HEAD_LOAD ) ? "on" : "off" ,
		  FDC.TR , FDC.DriveSelSignal >= 0 ? FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack : -1 ,
		  FDC.SideSignal , FDC.DriveSelSignal , FDC_DMA.SectorCount,
		  FDC_GetDMAAddress(), nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to write a sector(s) */
	FDC.Command = FDCEMU_CMD_WRITESECTORS;
	FDC.CommandState = FDCEMU_RUN_WRITESECTORS_WRITEDATA;

	FDC_Update_STR ( FDC_STR_BIT_DRQ | FDC_STR_BIT_LOST_DATA | FDC_STR_BIT_CRC_ERROR
		| FDC_STR_BIT_RNF | FDC_STR_BIT_RECORD_TYPE , FDC_STR_BIT_BUSY );

	return FDC_DELAY_CYCLE_TYPE_II_PREPARE;
}


/*-----------------------------------------------------------------------*/
/**
 * Type III Commands
 *
 * Read Address, Read Track, Write Track
 */


/*-----------------------------------------------------------------------*/
static int FDC_TypeIII_ReadAddress ( void )
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type III read address spinup=%s settle=%s tr=0x%x head_track=0x%x side=%d drive=%d addr=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  ( FDC.CR & FDC_COMMAND_BIT_SPIN_UP ) ? "off" : "on" ,
		  ( FDC.CR & FDC_COMMAND_BIT_HEAD_LOAD ) ? "on" : "off" ,
		  FDC.TR , FDC.DriveSelSignal >= 0 ? FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack : -1 ,
		  FDC.SideSignal , FDC.DriveSelSignal , FDC_GetDMAAddress(),
		  nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to seek to track zero */
	FDC.Command = FDCEMU_CMD_READADDRESS;
	FDC.CommandState = FDCEMU_RUN_READADDRESS;

	FDC_Update_STR ( FDC_STR_BIT_DRQ | FDC_STR_BIT_LOST_DATA | FDC_STR_BIT_CRC_ERROR
		| FDC_STR_BIT_RNF | FDC_STR_BIT_RECORD_TYPE | FDC_STR_BIT_WPRT , FDC_STR_BIT_BUSY );

	return FDC_DELAY_CYCLE_TYPE_III_PREPARE;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeIII_ReadTrack ( void )
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type III read track spinup=%s settle=%s tr=0x%x head_track=0x%x side=%d drive=%d addr=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  ( FDC.CR & FDC_COMMAND_BIT_SPIN_UP ) ? "off" : "on" ,
		  ( FDC.CR & FDC_COMMAND_BIT_HEAD_LOAD ) ? "on" : "off" ,
		  FDC.TR , FDC.DriveSelSignal >= 0 ? FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack : -1 ,
		  FDC.SideSignal , FDC.DriveSelSignal , FDC_GetDMAAddress(),
		  nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to read a single track */
	FDC.Command = FDCEMU_CMD_READTRACK;
	FDC.CommandState = FDCEMU_RUN_READTRACK;

	FDC_Update_STR ( FDC_STR_BIT_DRQ | FDC_STR_BIT_LOST_DATA | FDC_STR_BIT_CRC_ERROR
		| FDC_STR_BIT_RNF | FDC_STR_BIT_RECORD_TYPE | FDC_STR_BIT_WPRT , FDC_STR_BIT_BUSY );

	return FDC_DELAY_CYCLE_TYPE_III_PREPARE;
}


/*-----------------------------------------------------------------------*/
static int FDC_TypeIII_WriteTrack ( void )
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type III write track spinup=%s settle=%s tr=0x%x head_track=0x%x side=%d drive=%d addr=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  ( FDC.CR & FDC_COMMAND_BIT_SPIN_UP ) ? "off" : "on" ,
		  ( FDC.CR & FDC_COMMAND_BIT_HEAD_LOAD ) ? "on" : "off" ,
		  FDC.TR , FDC.DriveSelSignal >= 0 ? FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack : -1 ,
		  FDC.SideSignal , FDC.DriveSelSignal , FDC_GetDMAAddress(),
		  nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* Set emulation to write a single track */
	FDC.Command = FDCEMU_CMD_WRITETRACK;
	FDC.CommandState = FDCEMU_RUN_WRITETRACK;

	FDC_Update_STR ( FDC_STR_BIT_DRQ | FDC_STR_BIT_LOST_DATA | FDC_STR_BIT_CRC_ERROR
		| FDC_STR_BIT_RNF | FDC_STR_BIT_RECORD_TYPE | FDC_STR_BIT_WPRT , FDC_STR_BIT_BUSY );

	return FDC_DELAY_CYCLE_TYPE_III_PREPARE;
}


/*-----------------------------------------------------------------------*/
/**
 * Type IV Commands
 *
 * Force Interrupt
 */


/*-----------------------------------------------------------------------*/
static int FDC_TypeIV_ForceInterrupt ( void )
{
	int	FdcCycles;
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type IV force int 0x%x irq=%d index=%d VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  FDC.CR , ( FDC.CR & 0x8 ) >> 3 , ( FDC.CR & 0x4 ) >> 2 ,
		  nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* If a command was running, just remove busy bit and keep the current content of Status reg */
	/* If FDC was idle, the content of Status reg is forced to type I */
	if ( ( FDC.STR & FDC_STR_BIT_BUSY ) == 0 )			
	{
		FDC.StatusTypeI = true;

		/* Starting a Force Int command when idle should set the motor bit and clear the spinup bit (verified on STF) */
		FDC_Update_STR ( FDC_STR_BIT_SPIN_UP , FDC_STR_BIT_MOTOR_ON );	/* Clear spinup bit and set motor bit */
	}

	/* Get the interrupt's condition and set IRQ accordingly */
	/* Most of the time a 0xD8 command is followed by a 0xD0 command and a read STR to clear the IRQ signal */
	FDC.InterruptCond = FDC.CR & 0x0f;				/* Keep the 4 lowest bits */

	if ( FDC.InterruptCond & FDC_INTERRUPT_COND_IMMEDIATE )
		FDC_SetIRQ ( FDC_IRQ_SOURCE_FORCED );
	else
		FDC_ClearIRQ ();

	/* Remove busy bit, don't change IRQ's state and stop the motor */
	FdcCycles = FDC_CmdCompleteCommon( false );

	return FDC_DELAY_CYCLE_TYPE_IV_PREPARE + FdcCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Execute Type I commands
 */
static int FDC_ExecuteTypeICommands ( void )
{
	int	FdcCycles = 0;

	FDC.CommandType = 1;
	FDC.StatusTypeI = true;

	/* Check Type I Command */
	switch ( FDC.CR & 0xf0 )
	{
	 case 0x00:             /* Restore */
		FdcCycles = FDC_TypeI_Restore();
		break;
	 case 0x10:             /* Seek */
		FdcCycles = FDC_TypeI_Seek();
		break;
	 case 0x20:             /* Step */
	 case 0x30:
		FdcCycles = FDC_TypeI_Step();
		break;
	 case 0x40:             /* Step-In */
	 case 0x50:
		FdcCycles = FDC_TypeI_StepIn();
		break;
	 case 0x60:             /* Step-Out */
	 case 0x70:
		FdcCycles = FDC_TypeI_StepOut();
		break;
	}


	/* After a "STEP" command we set the Disk Change signal to "inserted" */
	/* if the drive is selected and a floppy is inserted */
	if ( ( FDC.DriveSelSignal >= 0 ) && ( FDC_DRIVES[ FDC.DriveSelSignal ].DiskInserted == true ) )
		FDC_Drive_Set_DC_signal ( FDC.DriveSelSignal , FDC_DC_SIGNAL_INSERTED );


	return FdcCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Execute Type II commands
 */
static int FDC_ExecuteTypeIICommands ( void )
{
	int	FdcCycles = 0;

	FDC.CommandType = 2;
	FDC.StatusTypeI = false;

	/* Check Type II Command */
	switch ( FDC.CR & 0xf0 )
	{
	 case 0x80:             /* Read Sector multi=0*/
	 case 0x90:             /* Read Sectors multi=1 */
		FdcCycles = FDC_TypeII_ReadSector();
		break;
	 case 0xa0:             /* Write Sector multi=0 */
	 case 0xb0:             /* Write Sectors multi=1 */
		FdcCycles = FDC_TypeII_WriteSector();
		break;
	}

	return FdcCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Execute Type III commands
 */
static int FDC_ExecuteTypeIIICommands ( void )
{
	int	FdcCycles = 0;

	FDC.CommandType = 3;
	FDC.StatusTypeI = false;

	/* Check Type III Command */
	switch ( FDC.CR & 0xf0 )
	{
	 case 0xc0:             /* Read Address */
		FdcCycles = FDC_TypeIII_ReadAddress();
		break;
	 case 0xe0:             /* Read Track */
		FdcCycles = FDC_TypeIII_ReadTrack();
		break;
	 case 0xf0:             /* Write Track */
		FdcCycles = FDC_TypeIII_WriteTrack();
		break;
	}

	return FdcCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Execute Type IV commands
 */
static int FDC_ExecuteTypeIVCommands ( void )
{
	int	FdcCycles;

	FDC.CommandType = 4;

	FdcCycles = FDC_TypeIV_ForceInterrupt();
	
	return FdcCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Find FDC command type and execute
 *
 * NOTE [NP] : as verified on a real STF and contrary to what is written
 * in the WD1772 doc, any new command will reset the InterruptCond set by
 * a previous Dx command, not just a D0.
 * This means that a D8 command (force int) can be cancelled by a D0 command
 * or by any other command ; but in any case, IRQ will remain set until
 * status register is read or another new command is started.
 * -> 1st command clears force IRQ condition, 2nd command clears IRQ
 */
static void FDC_ExecuteCommand ( void )
{
	int	FdcCycles;
	uint8_t	Type;

	Type = FDC_GetCmdType ( FDC.CR );

	/* When a new command is started, FDC's IRQ is reset (except if "force interrupt immediate" is set) */

	/* If IRQ is forced but FDC_INTERRUPT_COND_IMMEDIATE is not set anymore, this means */
	/* the D8 command was stopped and we can clear the forced IRQ when starting a new command */
	if ( ( FDC.IRQ_Signal & FDC_IRQ_SOURCE_FORCED )
	  && ( ( FDC.InterruptCond & FDC_INTERRUPT_COND_IMMEDIATE ) == 0 ) )
		FDC.IRQ_Signal &= ~FDC_IRQ_SOURCE_FORCED;	/* Really stop the forced IRQ */

	/* Starting a new type I/II/III should clear the IRQ (except when IRQ is forced) */
	/* For type IV, this is handled in FDC_TypeIV_ForceInterrupt() */
	if ( Type != 4 )
		FDC_ClearIRQ ();
	
	/* When a new command is executed, we clear InterruptCond */
	/* (not just when the new command is D0) */
	/* InterruptCond is cleared here, but it might be set again just after */
	/* when we call FDC_ExecuteTypeIVCommands() */
	FDC.InterruptCond = 0;

	/* Check type of command and execute */
	if ( Type == 1 )						/* Type I - Restore, Seek, Step, Step-In, Step-Out */
		FdcCycles = FDC_ExecuteTypeICommands();
	else if ( Type == 2 )						/* Type II - Read Sector, Write Sector */
		FdcCycles = FDC_ExecuteTypeIICommands();
	else if ( Type == 3 )						/* Type III - Read Address, Read Track, Write Track */
		FdcCycles = FDC_ExecuteTypeIIICommands();
	else								/* Type IV - Force Interrupt */
		FdcCycles = FDC_ExecuteTypeIVCommands();

	FDC.ReplaceCommandPossible = true;				/* This new command can be replaced during the prepare+spinup phase */
	FDC_StartTimer_FdcCycles ( FdcCycles , 0 );
}


/*-----------------------------------------------------------------------*/
/**
 * Write to SectorCount register $ff8604
 */
static void FDC_WriteSectorCountRegister ( void )
{
	int FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	FDC_DMA.SectorCount = IoMem_ReadWord(0xff8604);
	if (!Config_IsMachineFalcon())
		FDC_DMA.SectorCount &= 0xff;

	LOG_TRACE(TRACE_FDC, "fdc write 8604 dma sector count=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
	          FDC_DMA.SectorCount, nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

}


/*-----------------------------------------------------------------------*/
/**
 * Write to Command register $ff8604
 */
static void FDC_WriteCommandRegister ( void )
{
	int FrameCycles, HblCounterVideo, LineCycles;
	uint8_t Type_new;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write 8604 command=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		  IoMem_ReadByte(0xff8605), nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	/* If fdc is busy, only 'Force Interrupt' is possible */
	/* [NP] : it's also possible to start a new command just after another command */
	/* was started and spinup phase was not completed yet (eg Overdrive Demos by Phalanx) (see notes at the top of the file)*/
	if ( FDC.STR & FDC_STR_BIT_BUSY )
	{
		Type_new = FDC_GetCmdType ( IoMem_ReadByte(0xff8605) );
		if ( Type_new == 4 )					/* 'Force Interrupt' command */
		{
			LOG_TRACE(TRACE_FDC, "fdc write 8604 while fdc busy, current command=0x%x interrupted by command=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
				FDC.CR , IoMem_ReadByte(0xff8605), nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());
		}

		else if ( FDC.ReplaceCommandPossible
			&& ( ( ( Type_new == 1 ) && ( FDC.CommandType == Type_new ) )		/* Replace a type I command with a type I */
			  || ( ( Type_new == 2 ) && ( FDC.CommandType == Type_new ) ) ) )	/* Replace a type II command with a type II */
		{
			LOG_TRACE(TRACE_FDC, "fdc write 8604 while fdc busy in prepare+spinup, current command=0x%x replaced by command=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
				FDC.CR , IoMem_ReadByte(0xff8605), nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());
		}

		else								/* Other cases : new command is ignored */
		{
			LOG_TRACE(TRACE_FDC, "fdc write 8604 fdc busy, command=0x%x ignored VBL=%d video_cyc=%d %d@%d pc=%x\n",
				IoMem_ReadByte(0xff8605), nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());
			return;
		}
	}

	FDC.CR = IoMem_ReadByte(0xff8605);
	FDC_ExecuteCommand();
}


/*-----------------------------------------------------------------------*/
/**
 * Write to Track register $ff8604
 */
static void FDC_WriteTrackRegister ( void )
{
	int FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write 8604 track=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		IoMem_ReadByte(0xff8605) , nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	/* [NP] Contrary to what is written in the WD1772 doc, Track Register can be changed */
	/* while the fdc is busy (the change will be ignored or not, depending on the current sub-state */
	/* in the state machine) */
	if ( FDC.STR & FDC_STR_BIT_BUSY )
	{
		LOG_TRACE(TRACE_FDC, "fdc write 8604 fdc busy, track=0x%x may be ignored VBL=%d video_cyc=%d %d@%d pc=%x\n",
			IoMem_ReadByte(0xff8605), nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());
//return;
	}

	FDC.TR = IoMem_ReadByte(0xff8605);
}


/*-----------------------------------------------------------------------*/
/**
 * Write to Sector register $ff8604
 */
static void FDC_WriteSectorRegister ( void )
{
	int FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write 8604 sector=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		IoMem_ReadByte(0xff8605) , nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	/* [NP] Contrary to what is written in the WD1772 doc, Sector Register can be changed */
	/* while the fdc is busy (but it will have no effect once the sector's header is found) */
	/* (fix Delirious Demo IV's loader, which is bugged and set SR after starting the Read Sector command) */
	if ( FDC.STR & FDC_STR_BIT_BUSY )
	{
		LOG_TRACE(TRACE_FDC, "fdc write 8604 fdc busy, sector=0x%x may be ignored VBL=%d video_cyc=%d %d@%d pc=%x\n",
			IoMem_ReadByte(0xff8605), nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());
	}

	FDC.SR = IoMem_ReadByte(0xff8605);
}


/*-----------------------------------------------------------------------*/
/**
 * Write to Data register $ff8604
 */
static void FDC_WriteDataRegister ( void )
{
	int FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write 8604 data=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		IoMem_ReadByte(0xff8605), nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	FDC.DR = IoMem_ReadByte(0xff8605);
}


/*-----------------------------------------------------------------------*/
/**
 * Store byte in FDC/HDC registers or DMA sector count, when writing to $ff8604
 * When accessing FDC/HDC registers, a copy of $ff8604 should be kept in ff8604_recent_val
 * to be used later when reading unused bits at $ff8604/$ff8606
 *
 * NOTE [NP] : add 4 cycles wait state in all cases (sector count / FDC / HDC)
 */
void FDC_DiskController_WriteWord ( void )
{
	int FrameCycles, HblCounterVideo, LineCycles;
	int EmulationMode;
	int FDC_reg;

	if ( nIoMemAccessSize == SIZE_BYTE )
	{
		/* This register does not like to be accessed in byte mode on a normal ST */
		M68000_BusError(IoAccessFullAddress, BUS_ERROR_WRITE, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
		return;
	}

	M68000_WaitState(4);

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write 8604 data=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		IoMem_ReadWord(0xff8604), nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );


	/* Are we trying to set the DMA SectorCount ? */
	if ( FDC_DMA.Mode & 0x10 )					/* Bit 4 */
	{
		FDC_WriteSectorCountRegister();
		return;
	}

	/* Store the byte that was just accessed by this write */
	FDC_DMA.ff8604_recent_val = ( FDC_DMA.ff8604_recent_val & 0xff00 ) | IoMem_ReadByte(0xff8605);
	
	if ( ( FDC_DMA.Mode & 0x0008 ) == 0x0008 )			/* Is it an ACSI (or Falcon SCSI) HDC command access ? */
	{
		/*  Handle HDC access */
		LOG_TRACE(TRACE_FDC, "fdc write 8604 hdc command addr=%x command=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n",
			  FDC_DMA.Mode & 0x7 , IoMem_ReadByte(0xff8605), nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

		HDC_WriteCommandByte(FDC_DMA.Mode & 0x7, IoMem_ReadByte(0xff8605));
		return;
	}

	else								/* It's a FDC register access */
	{
		FDC_reg = ( FDC_DMA.Mode & 0x6 ) >> 1;			/* Bits 1,2 (A0,A1) */

		EmulationMode = FDC_GetEmulationMode();
		if ( EmulationMode == FDC_EMULATION_MODE_INTERNAL )
		{
			/* Update FDC's internal variables */
			FDC_UpdateAll ();

			/* Write to FDC registers */
			switch ( FDC_reg )
			{
			 case 0x0:					/* 0 0 - Command register */
				FDC_WriteCommandRegister();
				break;
			 case 0x1:					/* 0 1 - Track register */
				FDC_WriteTrackRegister();
				break;
			 case 0x2:					/* 1 0 - Sector register */
				FDC_WriteSectorRegister();
				break;
			 case 0x3:					/* 1 1 - Data register */
				FDC_WriteDataRegister();
				break;
			}
		}
		else if ( EmulationMode == FDC_EMULATION_MODE_IPF )
		{
			IPF_FDC_WriteReg ( FDC_reg , IoMem_ReadByte(0xff8605) );
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Return FDC/HDC registers or DMA sector count when reading from $ff8604
 * - When accessing FDC/HDC registers, a copy of $ff8604 should be kept in ff8604_recent_val
 *   to be used later when reading unused bits at $ff8604/$ff8606
 * - DMA sector count can't be read, this will return ff8604_recent_val (verified on a real STF)
 *
 * NOTE [NP] : add 4 cycles wait state in that case, except when reading DMA sector count
 */
void FDC_DiskControllerStatus_ReadWord ( void )
{
	uint16_t DiskControllerByte = 0;					/* Used to pass back the parameter */
	int FrameCycles, HblCounterVideo, LineCycles;
	int ForceWPRT;
	int EmulationMode;
	int FDC_reg;

	if (nIoMemAccessSize == SIZE_BYTE && !Config_IsMachineFalcon())
	{
		/* This register does not like to be accessed in byte mode on a normal ST */
		M68000_BusError(IoAccessFullAddress, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
		return;
	}

	/* Are we trying to read the DMA SectorCount ? */
	if ( FDC_DMA.Mode & 0x10 )					/* Bit 4 */
	{
		DiskControllerByte = FDC_DMA.ff8604_recent_val;		/* As verified on real STF, DMA sector count can't be read back */
	}

	else if ( ( FDC_DMA.Mode & 0x0008) == 0x0008)			/* HDC status reg selected ? */
	{
		M68000_WaitState(4);					/* [NP] : possible, but not tested on real HW */

		/* Return the HDC status byte */
		DiskControllerByte = HDC_ReadCommandByte(FDC_DMA.Mode & 0x7);
	}

	else								/* It's a FDC register access */
	{
		M68000_WaitState(4);

		FDC_reg = ( FDC_DMA.Mode & 0x6 ) >> 1;			/* Bits 1,2 (A0,A1) */

		EmulationMode = FDC_GetEmulationMode();
		if ( EmulationMode == FDC_EMULATION_MODE_INTERNAL )
		{
			/* Update FDC's internal variables */
			FDC_UpdateAll ();

			/* Read FDC registers */
			switch ( FDC_reg )
			{
			 case 0x0:						/* 0 0 - Status register */
				/* If we report a type I status, some bits are updated in real time */
				/* depending on the corresponding signals. If this is not a type I, we return STR unmodified */
				/* [NP] Contrary to what is written in the WD1772 doc, the WPRT bit */
				/* is updated after a Type I command */
				/* (eg : Procopy or Terminators Copy 1.68 do a Restore/Seek to test WPRT) */
				if ( FDC.StatusTypeI )
				{
					/* If no drive available, FDC's input signals TR00, INDEX and WPRT are off */
					if ( ( FDC.DriveSelSignal < 0 ) || ( !FDC_DRIVES[ FDC.DriveSelSignal ].Enabled ) )
						FDC_Update_STR ( FDC_STR_BIT_TR00 | FDC_STR_BIT_INDEX | FDC_STR_BIT_WPRT , 0 );

					else
					{
						if ( FDC_DRIVES[ FDC.DriveSelSignal ].HeadTrack == 0 )
							FDC_Update_STR ( 0 , FDC_STR_BIT_TR00 );	/* Set TR00 bit2 */
						else
							FDC_Update_STR ( FDC_STR_BIT_TR00 , 0 );	/* Unset TR00 bit2 */

						if ( FDC_IndexPulse_GetState () )
							FDC_Update_STR ( 0 , FDC_STR_BIT_INDEX );	/* Set INDEX bit1 */
						else
							FDC_Update_STR ( FDC_STR_BIT_INDEX , 0 );	/* Unset INDEX bit1 */

						/* For Type I, always unset CRC ERROR  bit3 */
						FDC_Update_STR ( FDC_STR_BIT_CRC_ERROR , 0 );

						/* When there's no disk in drive, the floppy drive hardware can't see */
						/* the difference with an inserted disk that would be write protected */
						if ( ! FDC_DRIVES[ FDC.DriveSelSignal ].DiskInserted )
							FDC_Update_STR ( 0 , FDC_STR_BIT_WPRT );	/* Set WPRT bit6 */
						else if ( Floppy_IsWriteProtected ( FDC.DriveSelSignal ) )
							FDC_Update_STR ( 0 , FDC_STR_BIT_WPRT );	/* Set WPRT bit6 */
						else
							FDC_Update_STR ( FDC_STR_BIT_WPRT , 0 );	/* Unset WPRT bit6 */

						/* Temporarily change the WPRT bit if we're in a transition phase */
						/* regarding the disk in the drive (inserting or ejecting) */
						ForceWPRT = Floppy_DriveTransitionUpdateState ( FDC.DriveSelSignal );
						if ( ForceWPRT == 1 )
							FDC_Update_STR ( 0 , FDC_STR_BIT_WPRT );	/* Force setting WPRT */
						else if ( ForceWPRT == -1 )
							FDC_Update_STR ( FDC_STR_BIT_WPRT , 0 );	/* Force clearing WPRT */

						if ( ForceWPRT != 0 )
							LOG_TRACE(TRACE_FDC, "force wprt=%d VBL=%d drive=%d str=%x\n",
							  ForceWPRT==1?1:0, nVBLs, FDC.DriveSelSignal, FDC.STR );
					}
				}

				DiskControllerByte = FDC.STR;

				/* When Status Register is read, FDC's IRQ is reset (except if "force interrupt immediate" is set) */

				/* If IRQ is forced but FDC_INTERRUPT_COND_IMMEDIATE is not set anymore, this means */
				/* the D8 command was stopped and we can clear the forced IRQ while reading status register */
				if ( ( FDC.IRQ_Signal & FDC_IRQ_SOURCE_FORCED )
				  && ( ( FDC.InterruptCond & FDC_INTERRUPT_COND_IMMEDIATE ) == 0 ) )
					FDC.IRQ_Signal &= ~FDC_IRQ_SOURCE_FORCED;	/* Really stop the forced IRQ */

				FDC_ClearIRQ ();
				break;
			 case 0x1:						/* 0 1 - Track register */
				DiskControllerByte = FDC.TR;
				break;
			 case 0x2:						/* 1 0 - Sector register */
				DiskControllerByte = FDC.SR;
				break;
			 case 0x3:						/* 1 1 - Data register */
				DiskControllerByte = FDC.DR;
				break;
			}
		}
		else if ( EmulationMode == FDC_EMULATION_MODE_IPF )
		{
			DiskControllerByte = IPF_FDC_ReadReg ( FDC_reg );
			if ( ( FDC_reg == 0x0 ) && ( FDC.DriveSelSignal >= 0 ) )	/* 0 0 - Status register */
			{
				/* Temporarily change the WPRT bit if we're in a transition phase */
				/* regarding the disk in the drive (inserting or ejecting) */
				ForceWPRT = Floppy_DriveTransitionUpdateState ( FDC.DriveSelSignal );
				if ( ForceWPRT == 1 )
					DiskControllerByte |= FDC_STR_BIT_WPRT;		/* Force setting WPRT */
				if ( ForceWPRT == -1 )
					DiskControllerByte &= ~FDC_STR_BIT_WPRT;	/* Force clearing WPRT */

				if ( ForceWPRT != 0 )
					LOG_TRACE(TRACE_FDC, "force wprt=%d VBL=%d drive=%d str=%x\n", ForceWPRT==1?1:0, nVBLs, FDC.DriveSelSignal, DiskControllerByte );
			}
		}
	}


	/* Store the byte that was just returned by this read if we accessed fdc/hdc regs */
	if ( ( FDC_DMA.Mode & 0x10 ) == 0 )				/* Bit 4 */
		FDC_DMA.ff8604_recent_val = ( FDC_DMA.ff8604_recent_val & 0xff00 ) | ( DiskControllerByte & 0xff );

	IoMem_WriteWord(0xff8604, DiskControllerByte);

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc read 8604 ctrl status=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		DiskControllerByte , nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );
}


/*-----------------------------------------------------------------------*/
/**
 * Write word to $ff8606 (DMA Mode Control)
 *
 * Eg.
 * $80 - Selects command/status register
 * $82 - Selects track register
 * $84 - Selects sector register
 * $86 - Selects data register
 * NOTE - OR above values with $100 is transfer from memory to floppy
 * Also if bit 4 is set, write to DMA sector count register
 *
 * NOTE [NP] : add 4 cycles wait state in that case
 */
void FDC_DmaModeControl_WriteWord ( void )
{
	uint16_t Mode_prev;						/* Store previous write to 0xff8606 for 'toggle' checks */
	int FrameCycles, HblCounterVideo, LineCycles;


	if (nIoMemAccessSize == SIZE_BYTE)
	{
		/* This register does not like to be accessed in byte mode on a normal ST */
		M68000_BusError(IoAccessFullAddress, BUS_ERROR_WRITE, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
		return;
	}

	M68000_WaitState(4);

	Mode_prev = FDC_DMA.Mode;					/* Store previous to check for _read/_write toggle (DMA reset) */
	FDC_DMA.Mode = IoMem_ReadWord(0xff8606);			/* Store to DMA Mode control */

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write 8606 ctrl=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		FDC_DMA.Mode , nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	/* When write to 0xff8606, check bit '8' toggle. This causes DMA status reset */
	if ((Mode_prev ^ FDC_DMA.Mode) & 0x0100)
		FDC_ResetDMA();

	if ((Mode_prev & 0xc0) != 0 && (FDC_DMA.Mode & 0xc0) == 0)
		HDC_DmaTransfer();
}


/*-----------------------------------------------------------------------*/
/**
 * Read DMA Status at $ff8606
 *
 * Only bits 0-2 are used :
 *   Bit 0 - Error Status (0=Error)
 *   Bit 1 - Sector Count Zero Status (0=Sector Count Zero)
 *   Bit 2 - Data Request signal from the FDC
 *
 * NOTE [NP] : as verified on STF, bit 0 will be cleared (=error) if DMA sector count is 0
 * when we get some DRQ to process.
 *
 * NOTE [NP] : on the ST, the Data Register will always be read by the DMA when the FDC's DRQ
 * signal is set. This means bit 2 of DMA status will be '0' nearly all the time
 * (as verified on STF by constantly reading DMA Status, bit 2 can be '1' during
 * a few cycles, before the DMA read the Data Register, but for the emulation we
 * consider it's always '0')
 *
 * NOTE [NP] : unused bits 3-15 are the ones from the latest $ff8604 access (verified on real STF)
 *
 * NOTE [NP] : no 4 cycles wait state in that case
 */
void FDC_DmaStatus_ReadWord ( void )
{
	if (nIoMemAccessSize == SIZE_BYTE && !Config_IsMachineFalcon())
	{
		/* This register does not like to be accessed in byte mode on a normal ST */
		M68000_BusError(IoAccessFullAddress, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
		return;
	}

	/* Update Bit1 for zero sector count */
	if ( FDC_DMA.SectorCount != 0 )
		FDC_DMA.Status |= 0x02;
	else
		FDC_DMA.Status &= ~0x02;

	/* In the case of the ST, Bit2 / DRQ is always 0 because it's handled by the DMA and its 16 bytes buffer */

	/* Return Status with unused bits replaced by latest bits from $ff8604 */
	IoMem_WriteWord( 0xff8606 , FDC_DMA.Status | ( FDC_DMA.ff8604_recent_val & 0xfff8 ) );
}


/*-----------------------------------------------------------------------*/
/**
 * Read hi/med/low DMA address byte at $ff8609/0b/0d
 */
void	FDC_DmaAddress_ReadByte ( void )
{
	int FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc read dma address %x val=0x%02x address=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		IoAccessCurrentAddress , IoMem[ IoAccessCurrentAddress ] , FDC_GetDMAAddress() ,
		nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );
}


/*-----------------------------------------------------------------------*/
/**
 * Write hi/med/low DMA address byte at $ff8609/0b/0d
 *
 * NOTE [NP] : as described by Ijor in http://atari-forum.com/viewtopic.php?f=16&t=30289
 * the STF DMA address counter uses a ripple carry adder that will increment middle byte
 * when bit 7 of lower byte goes from 1 to 0 (same for middle/high bytes)
 * To avoid possible error with this carry, DMA address bytes should be written in the order
 * low, middle, high (as specified by Atari documentations) and not high/middle/low
 */
void	FDC_DmaAddress_WriteByte ( void )
{
	uint32_t Address;
	uint32_t Address_old;
	int FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write dma address %x val=0x%02x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		IoAccessCurrentAddress , IoMem[ IoAccessCurrentAddress ] ,
		nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	/* Build up 24-bit address from hardware registers */
	Address = ((uint32_t)STMemory_ReadByte(0xff8609)<<16) | ((uint32_t)STMemory_ReadByte(0xff860b)<<8) | (uint32_t)STMemory_ReadByte(0xff860d);

	/* On STF, DMA address uses a "ripple carry adder" which can trigger when writing to $ff860b/0d */
	if ( Config_IsMachineST() )
	{
		Address_old = FDC_GetDMAAddress();

		if ( ( Address_old & 0x80 ) && !( Address & 0x80 ) )		/* Bit 7 goes from 1 to 0 */
		{
			Address += 0x100;					/* Increase middle byte (and high byte if needed) */
//fprintf ( stderr , "fdc write dma address detect ripple carry at $ff860d old=0x%x new=0x%x\n" , Address_old , Address );
			LOG_TRACE(TRACE_FDC, "fdc write dma address detect ripple carry at $ff860d old=0x%x new=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
				Address_old , Address ,
				nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );
		}
		else if ( ( Address_old & 0x8000 ) && !( Address & 0x8000 ) )	/* Bit 15 goes from 1 to 0 */
		{
			Address += 0x10000;					/* Increase high byte */
//fprintf ( stderr , "fdc write dma address detect ripple carry at $ff860b old=0x%x new=0x%x\n" , Address_old , Address );
			LOG_TRACE(TRACE_FDC, "fdc write dma address detect ripple carry at $ff860b old=0x%x new=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
				Address_old , Address ,
				nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );
		}
	}

	/* Store new address as DMA address and update $ff8609/0b/0d */
	FDC_WriteDMAAddress ( Address );
}


/*-----------------------------------------------------------------------*/
/**
 * Get DMA address used to transfer data between FDC/HDC and RAM
 */
uint32_t FDC_GetDMAAddress(void)
{
	return FDC_DMA.Address;
}


/*-----------------------------------------------------------------------*/
/**
 * Write a new address to the FDC/HDC DMA address registers at $ff8909/0b/0d
 * As verified on real STF, DMA address high byte written at $ff8609 is masked
 * with 0x3f :
 *	move.b #$ff,$ff8609
 *	move.b $ff8609,d0  -> d0=$3f
 * DMA address must also be word-aligned, low byte at $ff860d is masked with 0xfe
 *	move.b #$ff,$ff860d
 *	move.b $ff860d,d0  -> d0=$fe
 */
void FDC_WriteDMAAddress ( uint32_t Address )
{
	int FrameCycles, HblCounterVideo, LineCycles;
	uint32_t dma_mask;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write dma address new=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		Address , nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	/* Mask DMA address : */
	/*  - DMA address must be word-aligned, bit 0 at $ff860d is always 0 */
	/*  - On STF/STE machines limited to 4MB of RAM, DMA address is also limited to $3fffff */
	dma_mask = 0xff00fffe | ( DMA_MaskAddressHigh() << 16 );		/* Force bit 0 to 0 */
	FDC_DMA.Address = Address & dma_mask;

	/* Store as 24-bit address */
	STMemory_WriteByte(0xff8609, FDC_DMA.Address>>16);
	STMemory_WriteByte(0xff860b, FDC_DMA.Address>>8);
	STMemory_WriteByte(0xff860d, FDC_DMA.Address);
}


/*-----------------------------------------------------------------------*/
/**
 * Return the number of FDC cycles to wait before reaching the next
 * sector's ID Field in the track ($A1 $A1 $A1 $FE TR SIDE SR LEN CRC1 CRC2)
 * If no ID Field is found before the end of the track, we use the 1st
 * ID Field of the track (which simulates a full spin of the floppy).
 * We also store the next sector's number into NextSector_ID_Field_SR,
 * the next track's number into NextSector_ID_Field_TR, the next sector's length
 * into NextSector_ID_Field_LEN and if the CRC is correct or not into
 * NextSector_ID_Field_CRC_OK.
 * This function assumes some 512 byte sectors stored in ascending
 * order (for ST/MSA)
 * If there's no available drive/floppy, we return -1
 */
static int	FDC_NextSectorID_FdcCycles_ST ( uint8_t Drive , uint8_t NumberOfHeads , uint8_t Track , uint8_t Side )
{
	int	CurrentPos;
	int	MaxSector;
	int	TrackPos;
	int	i;
	int	NextSector;
	int	NbBytes;

	CurrentPos = FDC_IndexPulse_GetCurrentPos_NbBytes ();
	if ( CurrentPos < 0 )						/* No drive/floppy available at the moment */
		return -1;

	if ( ( Side == 1 ) && ( NumberOfHeads == 1 ) )			/* Can't read side 1 on a single sided drive */
		return -1;

	if ( Track >= FDC_GetTracksPerDisk ( Drive ) )			/* Try to access a non existing track */
		return -1;

	if ( FDC_MachineHandleDensity ( Drive ) == false )		/* Can't handle the floppy's density */
		return -1;

	MaxSector = FDC_GetSectorsPerTrack ( Drive , Track , Side );
	TrackPos = FDC_TRACK_LAYOUT_STANDARD_GAP1;			/* Position of 1st raw sector */
	TrackPos += FDC_TRACK_LAYOUT_STANDARD_GAP2;			/* Position of ID Field in 1st raw sector */

	/* Compare CurrentPos with each sector's position in ascending order */
	for ( i=0 ; i<MaxSector ; i++ )
	{
		if ( CurrentPos < TrackPos )
			break;						/* We found the next sector */
		else
			TrackPos += FDC_TRACK_LAYOUT_STANDARD_RAW_SECTOR_512;
	}

	if ( i == MaxSector )						/* CurrentPos is after the last ID Field of this track */
	{
		/* Reach end of track (new index pulse), then go to sector 1 */
		NbBytes = FDC_GetBytesPerTrack ( Drive , Track , Side ) - CurrentPos + FDC_TRACK_LAYOUT_STANDARD_GAP1 + FDC_TRACK_LAYOUT_STANDARD_GAP2;
		NextSector = 1;
	}
	else								/* There's an ID Field before end of track */
	{
		NbBytes = TrackPos - CurrentPos;
		NextSector = i+1;
	}

//fprintf ( stderr , "fdc bytes next sector pos=%d trpos=%d nbbytes=%d maxsr=%d nextsr=%d\n" , CurrentPos, TrackPos, NbBytes, MaxSector, NextSector );
	FDC.NextSector_ID_Field_TR = Track;
	FDC.NextSector_ID_Field_SR = NextSector;
	FDC.NextSector_ID_Field_LEN = FDC_SECTOR_SIZE_512;		/* ST/MSA have 512 bytes per sector */
	FDC.NextSector_ID_Field_CRC_OK = 1;				/* CRC is always correct for ST/MSA */

	return FDC_TransferByte_FdcCycles ( NbBytes );
}


/*-----------------------------------------------------------------------*/
/**
 * Return the value of the track number in the next ID field set by
 * FDC_NextSectorID_FdcCycles_ST (for ST/MSA, it's always HeadTrack value)
 */
static uint8_t	FDC_NextSectorID_TR_ST ( void )
{
	return FDC.NextSector_ID_Field_TR;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the value of the sector number in the next ID field set by
 * FDC_NextSectorID_FdcCycles_ST.
 */
static uint8_t	FDC_NextSectorID_SR_ST ( void )
{
	return FDC.NextSector_ID_Field_SR;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the value of the sector's length in the next ID field set by
 * FDC_NextSectorID_FdcCycles_ST.
 * For ST/MSA, it's always 2 (512 bytes per sector)
 */
static uint8_t	FDC_NextSectorID_LEN_ST ( void )
{
	return FDC.NextSector_ID_Field_LEN;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the status of the CRC in the next ID field set by
 * FDC_NextSectorID_FdcCycles_ST.
 * If '0', CRC is bad, else CRC is OK
 * For ST/MSA, CRC is always OK
 */
static uint8_t	FDC_NextSectorID_CRC_OK_ST ( void )
{
	return FDC.NextSector_ID_Field_CRC_OK;
}


/*-----------------------------------------------------------------------*/
/**
 * Read a sector from a floppy image in ST format (used in type II command)
 * Each byte of the sector is added to the FDC buffer with a default timing
 * (32 microsec)
 * Return 0 if sector was read without error, or FDC_STR_BIT_RNF if an error occurred
 * (FDC_STR_BIT_CRC_ERROR and FDC_STR_BIT_RECORD_TYPE are always set 0 for ST/MSA)
 */
static uint8_t FDC_ReadSector_ST ( uint8_t Drive , uint8_t Track , uint8_t Sector , uint8_t Side , int *pSectorSize )
{
	int	FrameCycles, HblCounterVideo, LineCycles;
	int	i;
	uint8_t	*pSectorData;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc read sector addr=0x%x drive=%d track=%d sect=%d side=%d VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		FDC_GetDMAAddress(), Drive, Track, Sector, Side,
		nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	/* Get a pointer to the sector's data and convert into bytes/timings */
	if ( Floppy_ReadSectors ( Drive, &pSectorData, Sector, Track, Side, 1, NULL, pSectorSize ) )
	{
		for ( i=0 ; i<*pSectorSize ; i++ )
			FDC_Buffer_Add ( pSectorData[ i ] );
		return 0;						/* No error */
	}

	/* Failed */
	LOG_TRACE(TRACE_FDC, "fdc read sector failed\n" );
	return FDC_STR_BIT_RNF;
}


/*-----------------------------------------------------------------------*/
/**
 * Write a sector to a floppy image in ST format (used in type II command)
 * Bytes were added to the FDC Buffer with a default timing (32 microsec) ;
 * we copy only the bytes into a temporary buffer, and write this buffer
 * to the floppy image.
 * If DMASectorsCount==0, the DMA won't transfer any byte from RAM to the FDC
 * and some '0' bytes will be written to the disk.
 * Return 0 if sector was written without error, or FDC_STR_BIT_RNF if an error occurred
 */
static uint8_t FDC_WriteSector_ST ( uint8_t Drive , uint8_t Track , uint8_t Sector , uint8_t Side , int SectorSize )
{
	int	FrameCycles, HblCounterVideo, LineCycles;
	int	i;
	uint8_t	SectorData[ 1024 ];					/* max sector size for WD1772 */

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write sector addr=0x%x drive=%d track=%d sect=%d side=%d VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		FDC_GetDMAAddress(), Drive, Track, Sector, Side,
		nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	/* Get the sector's data (ignore timings) */
	for ( i=0 ; i<SectorSize ; i++ )
		SectorData[ i ] = FDC_Buffer_Read_Byte_pos ( i );

	/* Write the sector's data */
	if ( Floppy_WriteSectors ( Drive, SectorData, Sector, Track, Side, 1, NULL, NULL ) )
		return 0;						/* No error */

	/* Failed */
	LOG_TRACE(TRACE_FDC, "fdc write sector failed\n" );
	return FDC_STR_BIT_RNF;
}


/*-----------------------------------------------------------------------*/
/**
 * Read an address field from a floppy image in ST format (used in type III command)
 * As ST images don't have address field, we compute a standard one based
 * on the current track/sector/side.
 * Each byte of the ID field is added to the FDC buffer with a default timing
 * (32 microsec)
 * Always return 0 = OK (FDC_STR_BIT_CRC_ERROR and FDC_STR_BIT_RNF are
 * always set 0 for ST/MSA)
 */
static uint8_t FDC_ReadAddress_ST ( uint8_t Drive , uint8_t Track , uint8_t Sector , uint8_t Side )
{
	int	FrameCycles, HblCounterVideo, LineCycles;
	uint8_t	buf_id[ 10 ];						/* 3 SYNC + IAM + TR + SIDE + SECTOR + SIZE + CRC1 + CRC2 */
	uint8_t	*p;
	uint16_t	CRC;
	int	i;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	/* If trying to access address field on a non existing track, then return RNF */
	if ( Track >= FDC_GetTracksPerDisk ( Drive ) )
	{
		fprintf ( stderr , "fdc : read address drive=%d track=%d side=%d, but maxtrack=%d, return RNF\n" ,
			Drive , Track , Side , FDC_GetTracksPerDisk ( Drive ) );
		return STX_SECTOR_FLAG_RNF;				/* Should not happen if FDC_NextSectorID_FdcCycles_ST succeeded before */
	}

	p = buf_id;

	*p++ = 0xa1;							/* SYNC bytes and IAM byte are included in the CRC */
	*p++ = 0xa1;
	*p++ = 0xa1;
	*p++ = 0xfe;
	*p++ = Track;
	*p++ = Side;
	*p++ = Sector;
	*p++ = FDC_SECTOR_SIZE_512;					/* ST/MSA images are 512 bytes per sector */

	FDC_CRC16 ( buf_id , 8 , &CRC );

	*p++ = CRC >> 8;
	*p++ = CRC & 0xff;

	/* 6 bytes per ID field,  don't return the 3 x $A1 and $FE */
	for ( i=4 ; i<10 ; i++ )
		FDC_Buffer_Add ( buf_id[ i ] );
	
	LOG_TRACE(TRACE_FDC, "fdc read address 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x VBL=%d video_cyc=%d %d@%d pc=%x\n",
		buf_id[4] , buf_id[5] , buf_id[6] , buf_id[7] , buf_id[8] , buf_id[9] ,
		nVBLs, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC());

	return 0;							/* No error */
}


/*-----------------------------------------------------------------------*/
/**
 * Read a track from a floppy image in ST format (used in type III command)
 * As ST images don't have gaps,sync,..., we compute a standard track based
 * on the current track/side.
 * Each byte of the track is added to the FDC buffer with a default timing
 * (32 microsec)
 * Always return 0 = OK (we fill the track buffer in all cases)
 */
static uint8_t FDC_ReadTrack_ST ( uint8_t Drive , uint8_t Track , uint8_t Side )
{
	int	FrameCycles, HblCounterVideo, LineCycles;
	uint8_t	buf_id[ 10 ];						/* 3 SYNC + IAM + TR + SIDE + SECTOR + SIZE + CRC1 + CRC2 */
	uint8_t	*p;
	uint16_t	CRC;
	int	Sector;
	uint8_t	*pSectorData;
	int	SectorSize;
	int	i;
	
	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc type III read track drive=%d track=%d side=%d VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		Drive, Track, Side, nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	/* If trying to access a non existing track, then return an empty / not formatted track */
	if ( Track >= FDC_GetTracksPerDisk ( Drive ) )
	{
		fprintf ( stderr , "fdc : read track drive=%d track=%d side=%d, but maxtrack=%d, building an unformatted track\n" ,
			Drive , Track , Side , FDC_GetTracksPerDisk ( Drive ) );
		for ( i=0 ; i<FDC_GetBytesPerTrack ( Drive , Track , Side ) ; i++ )
			FDC_Buffer_Add ( Hatari_rand() & 0xff );	/* Fill the track buffer with random bytes */
		return 0;
	}

	for ( i=0 ; i<FDC_TRACK_LAYOUT_STANDARD_GAP1 ; i++ )		/* GAP1 */
		FDC_Buffer_Add ( 0x4e );

	for ( Sector=1 ; Sector <= FDC_GetSectorsPerTrack ( Drive , Track , Side ) ; Sector++ )
	{
		for ( i=0 ; i<FDC_TRACK_LAYOUT_STANDARD_GAP2 ; i++ )	/* GAP2 */
			FDC_Buffer_Add ( 0x00 );

		/* Add the ID field for the sector */
		p = buf_id;
		for ( i=0 ; i<3 ; i++ )		*p++ = 0xa1;		/* SYNC (write $F5) */
		*p++ = 0xfe;						/* Index Address Mark */
		*p++ = Track;						/* Track */
		*p++ = Side;						/* Side */
		*p++ = Sector;						/* Sector */
		*p++ = FDC_SECTOR_SIZE_512;				/* 512 bytes/sector for ST/MSA */
		FDC_CRC16 ( buf_id , 8 , &CRC );
		*p++ = CRC >> 8;					/* CRC1 (write $F7) */
		*p++ = CRC & 0xff;					/* CRC2 */

		for ( i=0 ; i<10 ; i++ )				/* Add the ID field to the track data */
			FDC_Buffer_Add ( buf_id[ i ] );

		for ( i=0 ; i<FDC_TRACK_LAYOUT_STANDARD_GAP3a ; i++ )	/* GAP3a */
			FDC_Buffer_Add ( 0x4e );
		for ( i=0 ; i<FDC_TRACK_LAYOUT_STANDARD_GAP3b ; i++ )	/* GAP3b */
			FDC_Buffer_Add ( 0x00 );

		/* Add the data for the sector + build the CRC */
		crc16_reset ( &CRC );
		for ( i=0 ; i<3 ; i++ )
		{
			FDC_Buffer_Add ( 0xa1 );			/* SYNC (write $F5) */
			crc16_add_byte ( &CRC , 0xa1 );
		}

		FDC_Buffer_Add ( 0xfb );				/* Data Address Mark */
		crc16_add_byte ( &CRC , 0xfb );

		if ( Floppy_ReadSectors ( Drive, &pSectorData, Sector, Track, Side, 1, NULL, &SectorSize ) )
		{
			for ( i=0 ; i<SectorSize ; i++ )
			{
				FDC_Buffer_Add ( pSectorData[ i ] );
				crc16_add_byte ( &CRC , pSectorData[ i ] );
			}
		}
		else
		{
			/* In case of error, we put some 0x00 bytes, but this case should */
			/* not happen with ST/MSA disk images, all sectors should be present on each track */
			for ( i=0 ; i<512 ; i++ )
			{
				FDC_Buffer_Add ( 0x00 );
				crc16_add_byte ( &CRC , 0x00 );
			}
		}

		FDC_Buffer_Add ( CRC >> 8 );				/* CRC1 (write $F7) */
		FDC_Buffer_Add ( CRC & 0xff );				/* CRC2 */

		for ( i=0 ; i<FDC_TRACK_LAYOUT_STANDARD_GAP4 ; i++ )	/* GAP4 */
			FDC_Buffer_Add ( 0x4e );
	}

	while ( FDC_Buffer_Get_Size () < FDC_GetBytesPerTrack ( Drive , Track , Side ) )	/* Complete the track buffer */
	      FDC_Buffer_Add ( 0x4e );					/* GAP5 */

	return 0;							/* No error */
}


/*-----------------------------------------------------------------------*/
/**
 * Write a track to a floppy image in ST format (used in type III command)
 * Bytes were added to the FDC Buffer with a default timing (32 microsec) ;
 * we copy only the bytes into a temporary buffer, and write this buffer
 * to the floppy image.
 * If DMASectorsCount==0, the DMA won't transfer any byte from RAM to the FDC
 * and some '0' bytes will be written to the disk.
 * Return 0 if track was written without error, or FDC_STR_BIT_LOST_DATA if an error occurred
 *
 * TODO : this is not supported for ST/MSA at the moment, so we return FDC_STR_BIT_LOST_DATA
 */
static uint8_t FDC_WriteTrack_ST ( uint8_t Drive , uint8_t Track , uint8_t Side , int TrackSize )
{
	int	FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write track not supported addr=0x%x drive=%d track=%d side=%d VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		FDC_GetDMAAddress(), Drive, Track, Side,
		nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	Log_Printf(LOG_TODO, "FDC type III command 'write track' is not supported with ST/MSA files\n");

	/* TODO : "Write track" should write all the sectors after extracting them from the track data ? */

	/* Failed */
	LOG_TRACE(TRACE_FDC, "fdc write track failed\n" );
	return FDC_STR_BIT_LOST_DATA;
}


/*-----------------------------------------------------------------------*/
/**
 * Write word to density mode register to choose between DD/HD behaviour for the FDC
 * Used on MegaSTE, TT and Falcon
 *
 *   ______________10  Density Control Register
 *                 ||
 *                 |+- FDC Frequency 0:8MHz 1:16MHz
 *                 +-- Density 0:DD 1:HD
 *
 * For DD disks:  0x00
 * For HD disks:  0x03
 */
void FDC_DensityMode_WriteWord ( void )
{
	int FrameCycles, HblCounterVideo, LineCycles;


	M68000_WaitState(4);			/* TODO : check on real HW */

	FDC.DensityMode = IoMem_ReadWord(0xff860e);

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc write 860e density=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		FDC.DensityMode , nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );
}


/*-----------------------------------------------------------------------*/
/**
 * Read word from density mode register to choose between DD/HD behaviour for the FDC
 * Used on MegaSTE, TT and Falcon
 *
 *   ______________10  Density Control Register
 *                 ||
 *                 |+- FDC Frequency 0:8MHz 1:16MHz
 *                 +-- Density 0:DD 1:HD
 *
 * For DD disks:  0x00
 * For HD disks:  0x03
 */
void FDC_DensityMode_ReadWord ( void )
{
	int FrameCycles, HblCounterVideo, LineCycles;


	M68000_WaitState(4);			/* TODO : check on real HW */

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_FDC, "fdc read 860e density=0x%x VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		FDC.DensityMode , nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );

	IoMem_WriteWord( 0xff860e , FDC.DensityMode );
}


