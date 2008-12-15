/*
  Hatari - fdc.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_FDC_H
#define HATARI_FDC_H

/*-----------------------------------------------------------------------*/
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


/* Commands are taking the equivalent of FDC_HBL_DELAY hbl's to execute */
/* to try to simulate the speed of a real ST floppy drive */
#define FDC_DELAY_CYCLES		92160
//#define FDC_DELAY_CYCLES		1536	// 'Just Bugging Demo' by ACF requires a very fast delay (bug in the loader)


extern Sint16 FDCSectorCountRegister;
extern Uint16 DiskControllerWord_ff8604wr;
extern Uint16 DMAModeControl_ff8606wr;


extern void FDC_Reset(void);
extern void FDC_MemorySnapShot_Capture(bool bSave);
extern void FDC_ResetDMAStatus(void);
extern void FDC_SetDMAStatus(bool bError);
extern void FDC_DmaStatus_ReadWord(void);
extern int FDC_FindFloppyDrive(void);
extern void FDC_AcknowledgeInterrupt(void);
extern void FDC_GpipRead(void);
extern void FDC_InterruptHandler_Update(void);
extern void FDC_UpdateRestoreCmd(void);
extern void FDC_UpdateSeekCmd(void);
extern void FDC_UpdateStepCmd(void);
extern void FDC_UpdateStepInCmd(void);
extern void FDC_UpdateStepOutCmd(void);
extern void FDC_UpdateReadSectorsCmd(void);
extern void FDC_UpdateWriteSectorsCmd(void);
extern void FDC_UpdateReadAddressCmd(void);
extern Uint32 FDC_ReadDMAAddress(void);
extern void FDC_WriteDMAAddress(Uint32 Address);
extern bool FDC_ReadSectorFromFloppy(void);
extern bool FDC_WriteSectorFromFloppy(void);
extern void FDC_DMADataFromFloppy(void);
extern void FDC_DiskController_WriteWord(void);
extern void FDC_DiskControllerStatus_ReadWord(void);
extern void FDC_DmaModeControl_WriteWord(void);
extern void FDC_FloppyMode_WriteByte(void);
extern void FDC_FloppyMode_ReadByte(void);


#endif /* ifndef HATARI_FDC_H */
