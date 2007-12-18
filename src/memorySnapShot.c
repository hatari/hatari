/*
  Hatari - memorySnapShot.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

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
const char MemorySnapShot_rcsid[] = "Hatari $Id: memorySnapShot.c,v 1.33 2007-12-18 20:35:05 thothy Exp $";

#include <config.h>

#include <SDL_types.h>
#include <string.h>
#include <errno.h>

#if HAVE_STRINGS_H
#include <strings.h>
#endif

#include "main.h"
#include "blitter.h"
#include "configuration.h"
#include "dmaSnd.h"
#include "fdc.h"
#include "file.h"
#include "floppy.h"
#include "gemdos.h"
#include "ikbd.h"
#include "int.h"
#include "ioMem.h"
#include "log.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "psg.h"
#include "reset.h"
#include "sound.h"
#include "tos.h"
#include "video.h"


#define VERSION_STRING      "0.97 "   /* Version number of compatible memory snapshots - Always 6 bytes (inc' NULL) */
#define VERSION_STRING_SIZE    6      /* Size of above (inc' NULL) */


#define COMPRESS_MEMORYSNAPSHOT       /* Compress snapshots to reduce disk space used */

#ifdef COMPRESS_MEMORYSNAPSHOT

#include <zlib.h>
typedef gzFile MSS_File;

#else

typedef FILE* MSS_File;

#endif


static MSS_File CaptureFile;
static BOOL bCaptureSave, bCaptureError;


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
 * Open/Create snapshot file, and set flag so 'MemorySnapShot_Store' knows
 * how to handle data.
 */
static BOOL MemorySnapShot_OpenFile(const char *pszFileName, BOOL bSave)
{
	char VersionString[VERSION_STRING_SIZE];

	/* Set error */
	bCaptureError = FALSE;

	/* Open file, set flag so 'MemorySnapShot_Store' can load to/save from file */
	if (bSave)
	{
		/* Save */
		CaptureFile = MemorySnapShot_fopen(pszFileName, "wb");
		if (!CaptureFile)
		{
			fprintf(stderr, "Failed to open save file '%s': %s\n",
			        pszFileName, strerror(errno));
			bCaptureError = TRUE;
			return(FALSE);
		}
		bCaptureSave = TRUE;
		/* Store version string */
		strcpy(VersionString, VERSION_STRING);
		MemorySnapShot_Store(VersionString, VERSION_STRING_SIZE);
	}
	else
	{
		/* Restore */
		CaptureFile = MemorySnapShot_fopen(pszFileName, "rb");
		if (!CaptureFile)
		{
			fprintf(stderr, "Failed to open file '%s': %s\n",
			        pszFileName, strerror(errno));
			bCaptureError = TRUE;
			return(FALSE);
		}
		bCaptureSave = FALSE;
		/* Restore version string */
		MemorySnapShot_Store(VersionString, VERSION_STRING_SIZE);
		/* Does match current version? */
		if (strcasecmp(VersionString, VERSION_STRING))
		{
			/* No, inform user and error */
			Log_AlertDlg(LOG_WARN, "Unable to Restore Memory State.\nFile is "
			                       "only compatible with Hatari v%s", VersionString);
			bCaptureError = TRUE;
			return(FALSE);
		}
	}

	/* All OK */
	return(TRUE);
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
			bCaptureError = TRUE;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Save 'snapshot' of memory/chips/emulation variables
 */
void MemorySnapShot_Capture(const char *pszFileName)
{
	/* Set to 'saving' */
	if (MemorySnapShot_OpenFile(pszFileName,TRUE))
	{
		/* Capture each files details */
		Configuration_MemorySnapShot_Capture(TRUE);
		TOS_MemorySnapShot_Capture(TRUE);
		Main_MemorySnapShot_Capture(TRUE);
		FDC_MemorySnapShot_Capture(TRUE);
		Floppy_MemorySnapShot_Capture(TRUE);
		GemDOS_MemorySnapShot_Capture(TRUE);
		IKBD_MemorySnapShot_Capture(TRUE);
		Int_MemorySnapShot_Capture(TRUE);
		M68000_MemorySnapShot_Capture(TRUE);
		MFP_MemorySnapShot_Capture(TRUE);
		PSG_MemorySnapShot_Capture(TRUE);
		Sound_MemorySnapShot_Capture(TRUE);
		Video_MemorySnapShot_Capture(TRUE);
		Blitter_MemorySnapShot_Capture(TRUE);
		DmaSnd_MemorySnapShot_Capture(TRUE);

		/* And close */
		MemorySnapShot_CloseFile();
	}

	/* Did error */
	if (bCaptureError)
		Log_AlertDlg(LOG_ERROR, "Unable to save memory state to file.");
	else
		Log_AlertDlg(LOG_INFO, "Memory state file saved.");
}


/*-----------------------------------------------------------------------*/
/**
 * Restore 'snapshot' of memory/chips/emulation variables
 */
void MemorySnapShot_Restore(const char *pszFileName)
{
	/* Set to 'restore' */
	if (MemorySnapShot_OpenFile(pszFileName,FALSE))
	{
		Configuration_MemorySnapShot_Capture(FALSE);
		TOS_MemorySnapShot_Capture(FALSE);

		/* Reset emulator to get things running */
		IoMem_UnInit();  IoMem_Init();
		Reset_Cold();

		/* Capture each files details */
		Main_MemorySnapShot_Capture(FALSE);
		FDC_MemorySnapShot_Capture(FALSE);
		Floppy_MemorySnapShot_Capture(FALSE);
		GemDOS_MemorySnapShot_Capture(FALSE);
		IKBD_MemorySnapShot_Capture(FALSE);
		Int_MemorySnapShot_Capture(FALSE);
		M68000_MemorySnapShot_Capture(FALSE);
		MFP_MemorySnapShot_Capture(FALSE);
		PSG_MemorySnapShot_Capture(FALSE);
		Sound_MemorySnapShot_Capture(FALSE);
		Video_MemorySnapShot_Capture(FALSE);
		Blitter_MemorySnapShot_Capture(FALSE);
		DmaSnd_MemorySnapShot_Capture(FALSE);

		/* And close */
		MemorySnapShot_CloseFile();
	}

	/* Did error? */
	if (bCaptureError)
		Log_AlertDlg(LOG_ERROR, "Unable to restore memory state from file.");
	else
		Log_AlertDlg(LOG_INFO, "Memory state file restored.");
}
