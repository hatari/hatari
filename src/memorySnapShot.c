/*
  Hatari - memorySnapShot.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Memory Snapshot

  This handles the saving/restoring of the emulator's state so any game or
  application can be saved and restored at any time. This is quite complicated
  as we need to store all STRam, all chip states, all emulation variables and
  then things get really complicated as we need to restore file handles
  and such like.
  To help keep things simple each file has one function which is used to
  save/restore all variables that are local to it. We use one function to
  reduce redundancy and the function 'MemorySnapShot_Store' decides if it
  should save or restore the data.
*/
const char MemorySnapShot_fileid[] = "Hatari memorySnapShot.c : " __DATE__ " " __TIME__;

#include <SDL_types.h>
#include <errno.h>

#include "main.h"
#include "blitter.h"
#include "configuration.h"
#include "debugui.h"
#include "dmaSnd.h"
#include "fdc.h"
#include "file.h"
#include "floppy.h"
#include "floppy_ipf.h"
#include "floppy_stx.h"
#include "gemdos.h"
#include "acia.h"
#include "ikbd.h"
#include "cycInt.h"
#include "cycles.h"
#include "ioMem.h"
#include "log.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "midi.h"
#include "psg.h"
#include "reset.h"
#include "sound.h"
#include "str.h"
#include "stMemory.h"
#include "tos.h"
#include "screen.h"
#include "video.h"
#include "falcon/dsp.h"
#include "falcon/crossbar.h"
#include "falcon/videl.h"
#include "statusbar.h"
#include "cart.h"


#define VERSION_STRING      "1.8.1"   /* Version number of compatible memory snapshots - Always 6 bytes (inc' NULL) */
#define SNAPSHOT_MAGIC      0xDeadBeef

#if HAVE_LIBZ
#define COMPRESS_MEMORYSNAPSHOT       /* Compress snapshots to reduce disk space used */
#endif

#ifdef COMPRESS_MEMORYSNAPSHOT

/* Remove possible conflicting mkdir declaration from cpu/sysdeps.h */
#undef mkdir
#include <zlib.h>
typedef gzFile MSS_File;

#else

typedef FILE* MSS_File;

#endif


static MSS_File CaptureFile;
static bool bCaptureSave, bCaptureError;


/*-----------------------------------------------------------------------*/
/**
 * Open file.
 */
static MSS_File MemorySnapShot_fopen(const char *pszFileName, const char *pszMode)
{
#ifdef COMPRESS_MEMORYSNAPSHOT
	return gzopen(pszFileName, pszMode);
#else
	return fopen(pszFileName, pszMode);
#endif
}


/*-----------------------------------------------------------------------*/
/**
 * Close file.
 */
static void MemorySnapShot_fclose(MSS_File fhndl)
{
#ifdef COMPRESS_MEMORYSNAPSHOT
	gzclose(fhndl);
#else
	fclose(fhndl);
#endif
}


/*-----------------------------------------------------------------------*/
/**
 * Read from file.
 */
static int MemorySnapShot_fread(MSS_File fhndl, char *buf, int len)
{
#ifdef COMPRESS_MEMORYSNAPSHOT
	return gzread(fhndl, buf, len);
#else
	return fread(buf, 1, len, fhndl);
#endif
}


/*-----------------------------------------------------------------------*/
/**
 * Write data to file.
 */
static int MemorySnapShot_fwrite(MSS_File fhndl, const char *buf, int len)
{
#ifdef COMPRESS_MEMORYSNAPSHOT
	return gzwrite(fhndl, buf, len);
#else
	return fwrite(buf, 1, len, fhndl);
#endif
}


/*-----------------------------------------------------------------------*/
/**
 * Seek into file from current position
 */
static int MemorySnapShot_fseek(MSS_File fhndl, int pos)
{
#ifdef COMPRESS_MEMORYSNAPSHOT
	return (int)gzseek(fhndl, pos, SEEK_CUR);	/* return -1 if error, new position >=0 if OK */
#else
	return fseek(fhndl, pos, SEEK_CUR);		/* return -1 if error, 0 if OK */
#endif
}


/*-----------------------------------------------------------------------*/
/**
 * Open/Create snapshot file, and set flag so 'MemorySnapShot_Store' knows
 * how to handle data.
 */
static bool MemorySnapShot_OpenFile(const char *pszFileName, bool bSave)
{
	char VersionString[] = VERSION_STRING;
#if ENABLE_WINUAE_CPU
# define CORE_VERSION 1
#else
# define CORE_VERSION 0
#endif
	Uint8 CpuCore;

	/* Set error */
	bCaptureError = false;

	/* after opening file, set bCaptureSave to indicate whether
	 * 'MemorySnapShot_Store' should load from or save to a file
	 */
	if (bSave)
	{
		if (!File_QueryOverwrite(pszFileName))
			return false;

		/* Save */
		CaptureFile = MemorySnapShot_fopen(pszFileName, "wb");
		if (!CaptureFile)
		{
			fprintf(stderr, "Failed to open save file '%s': %s\n",
			        pszFileName, strerror(errno));
			bCaptureError = true;
			return false;
		}
		bCaptureSave = true;
		/* Store version string */
		MemorySnapShot_Store(VersionString, sizeof(VersionString));
		/* Store CPU core version */
		CpuCore = CORE_VERSION;
		MemorySnapShot_Store(&CpuCore, sizeof(CpuCore));
	}
	else
	{
		/* Restore */
		CaptureFile = MemorySnapShot_fopen(pszFileName, "rb");
		if (!CaptureFile)
		{
			fprintf(stderr, "Failed to open file '%s': %s\n",
			        pszFileName, strerror(errno));
			bCaptureError = true;
			return false;
		}
		bCaptureSave = false;
		/* Restore version string */
		MemorySnapShot_Store(VersionString, sizeof(VersionString));
		/* Does match current version? */
		if (strcmp(VersionString, VERSION_STRING))
		{
			/* No, inform user and error */
			Log_AlertDlg(LOG_ERROR,
				     "Unable to restore Hatari memory state.\n"
				     "Given state file is compatible only with\n"
				     "Hatari version " VERSION_STRING ".");
			bCaptureError = true;
			return false;
		}
		/* Check CPU core version */
		MemorySnapShot_Store(&CpuCore, sizeof(CpuCore));
		if (CpuCore != CORE_VERSION)
		{
			Log_AlertDlg(LOG_ERROR,
				     "Unable to restore Hatari memory state.\n"
				     "Given state file is for different Hatari\n"
				     "CPU core version.");
			bCaptureError = true;
			return false;
		}
	}

	/* All OK */
	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * Close snapshot file.
 */
static void MemorySnapShot_CloseFile(void)
{
	MemorySnapShot_fclose(CaptureFile);
}


/*-----------------------------------------------------------------------*/
/**
 * Skip Nb bytes when reading from/writing to file.
 */
void MemorySnapShot_Skip(int Nb)
{
	int res;

	/* Check no file errors */
	if (CaptureFile != NULL)
	{
		res = MemorySnapShot_fseek(CaptureFile, Nb);

		/* Did seek OK? */
		if (res < 0)
			bCaptureError = true;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore data to/from file.
 */
void MemorySnapShot_Store(void *pData, int Size)
{
	long nBytes;

	/* Check no file errors */
	if (CaptureFile != NULL)
	{
		/* Saving or Restoring? */
		if (bCaptureSave)
			nBytes = MemorySnapShot_fwrite(CaptureFile, (char *)pData, Size);
		else
			nBytes = MemorySnapShot_fread(CaptureFile, (char *)pData, Size);

		/* Did save OK? */
		if (nBytes != Size)
			bCaptureError = true;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Save 'snapshot' of memory/chips/emulation variables
 */
void MemorySnapShot_Capture(const char *pszFileName, bool bConfirm)
{
	Uint32 magic = SNAPSHOT_MAGIC;

	/* Set to 'saving' */
	if (MemorySnapShot_OpenFile(pszFileName, true))
	{
		/* Capture each files details */
		Configuration_MemorySnapShot_Capture(true);
		TOS_MemorySnapShot_Capture(true);
		STMemory_MemorySnapShot_Capture(true);
		Cycles_MemorySnapShot_Capture(true);			/* Before fdc (for CyclesGlobalClockCounter) */
		FDC_MemorySnapShot_Capture(true);
		Floppy_MemorySnapShot_Capture(true);
		IPF_MemorySnapShot_Capture(true);			/* After fdc/floppy are saved */
		STX_MemorySnapShot_Capture(true);			/* After fdc/floppy are saved */
		GemDOS_MemorySnapShot_Capture(true);
		ACIA_MemorySnapShot_Capture(true);
		IKBD_MemorySnapShot_Capture(true);
		MIDI_MemorySnapShot_Capture(true);
		CycInt_MemorySnapShot_Capture(true);
		M68000_MemorySnapShot_Capture(true);
		MFP_MemorySnapShot_Capture(true);
		PSG_MemorySnapShot_Capture(true);
		Sound_MemorySnapShot_Capture(true);
		Video_MemorySnapShot_Capture(true);
		Blitter_MemorySnapShot_Capture(true);
		DmaSnd_MemorySnapShot_Capture(true);
		Crossbar_MemorySnapShot_Capture(true);
		VIDEL_MemorySnapShot_Capture(true);
		DSP_MemorySnapShot_Capture(true);
		DebugUI_MemorySnapShot_Capture(pszFileName, true);
		IoMem_MemorySnapShot_Capture(true);
		/* end marker */
		MemorySnapShot_Store(&magic, sizeof(magic));
		/* And close */
		MemorySnapShot_CloseFile();
	} else {
		/* just canceled? */
		if (!bCaptureError)
			return;
	}

	/* Did error */
	if (bCaptureError)
		Log_AlertDlg(LOG_ERROR, "Unable to save memory state to file.");
	else if (bConfirm)
		Log_AlertDlg(LOG_INFO, "Memory state file saved.");
}


/*-----------------------------------------------------------------------*/
/**
 * Restore 'snapshot' of memory/chips/emulation variables
 */
void MemorySnapShot_Restore(const char *pszFileName, bool bConfirm)
{
	Uint32 magic;

	/* Set to 'restore' */
	if (MemorySnapShot_OpenFile(pszFileName, false))
	{
		Configuration_MemorySnapShot_Capture(false);
		TOS_MemorySnapShot_Capture(false);

		/* Reset emulator to get things running */
		IoMem_UnInit();  IoMem_Init();
		Reset_Cold();

		/* Capture each files details */
		STMemory_MemorySnapShot_Capture(false);
		Cycles_MemorySnapShot_Capture(false);			/* Before fdc (for CyclesGlobalClockCounter) */
		FDC_MemorySnapShot_Capture(false);
		Floppy_MemorySnapShot_Capture(false);
		IPF_MemorySnapShot_Capture(false);			/* After fdc/floppy are restored, as IPF depends on them */
		STX_MemorySnapShot_Capture(false);			/* After fdc/floppy are restored, as STX depends on them */
		GemDOS_MemorySnapShot_Capture(false);
		ACIA_MemorySnapShot_Capture(false);
		IKBD_MemorySnapShot_Capture(false);			/* After ACIA */
		MIDI_MemorySnapShot_Capture(false);
		CycInt_MemorySnapShot_Capture(false);
		M68000_MemorySnapShot_Capture(false);
		MFP_MemorySnapShot_Capture(false);
		PSG_MemorySnapShot_Capture(false);
		Sound_MemorySnapShot_Capture(false);
		Video_MemorySnapShot_Capture(false);
		Blitter_MemorySnapShot_Capture(false);
		DmaSnd_MemorySnapShot_Capture(false);
		Crossbar_MemorySnapShot_Capture(false);
		VIDEL_MemorySnapShot_Capture(false);
		DSP_MemorySnapShot_Capture(false);
		DebugUI_MemorySnapShot_Capture(pszFileName, false);
		IoMem_MemorySnapShot_Capture(false);

		/* version string check catches release-to-release
		 * state changes, bCaptureError catches too short
		 * state file, this check a too long state file.
		 */
		MemorySnapShot_Store(&magic, sizeof(magic));
		if (!bCaptureError && magic != SNAPSHOT_MAGIC)
			bCaptureError = true;

		/* And close */
		MemorySnapShot_CloseFile();

		/* Apply patches for gemdos HD if needed */
		/* (we need to do it after cpu tables for all opcodes were rebuilt) */
		Cart_Patch();

		/* changes may affect also info shown in statusbar */
		Statusbar_UpdateInfo();

		if (bCaptureError)
		{
			Log_AlertDlg(LOG_ERROR, "Full memory state restore failed!\nPlease reboot emulation.");
			return;
		}
	}

	/* Did error? */
	if (bCaptureError)
		Log_AlertDlg(LOG_ERROR, "Unable to restore memory state from file.");
	else if (bConfirm)
		Log_AlertDlg(LOG_INFO, "Memory state file restored.");
}


/*-----------------------------------------------------------------------*/
/*
 * Save and restore functions required by the UAE CPU core...
 * ... don't use them in normal Hatari code!
 */
#include <savestate.h>

void save_u64(uae_u64 data)
{
	MemorySnapShot_Store(&data, 8);
}

void save_u32(uae_u32 data)
{
	MemorySnapShot_Store(&data, 4);
//printf ("s32 %x\n", data);
}

void save_u16(uae_u16 data)
{
	MemorySnapShot_Store(&data, 2);
//printf ("s16 %x\n", data);
}

void save_u8(uae_u8 data)
{
	MemorySnapShot_Store(&data, 1);
//printf ("s8 %x\n", data);
}

uae_u64 restore_u64(void)
{
	uae_u64 data;
	MemorySnapShot_Store(&data, 8);
	return data;
}

uae_u32 restore_u32(void)
{
	uae_u32 data;
	MemorySnapShot_Store(&data, 4);
//printf ("r32 %x\n", data);
	return data;
}

uae_u16 restore_u16(void)
{
	uae_u16 data;
	MemorySnapShot_Store(&data, 2);
//printf ("r16 %x\n", data);
	return data;
}

uae_u8 restore_u8(void)
{
	uae_u8 data;
	MemorySnapShot_Store(&data, 1);
//printf ("r8 %x\n", data);
	return data;
}

