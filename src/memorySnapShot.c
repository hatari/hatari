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
char MemorySnapShot_rcsid[] = "Hatari $Id: memorySnapShot.c,v 1.7 2003-12-25 14:19:38 thothy Exp $";

#include <SDL_types.h>
#include <errno.h>

#include "main.h"
#include "blitter.h"
#include "debug.h"
#include "dialog.h"
#include "fdc.h"
#include "file.h"
#include "floppy.h"
#include "gemdos.h"
#include "ikbd.h"
#include "int.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "psg.h"
#include "reset.h"
#include "sound.h"
#include "tos.h"
#include "video.h"


#define COMPRESS_MEMORYSNAPSHOT     /* Compress snapshots to reduce disc space used */

#ifdef COMPRESS_MEMORYSNAPSHOT

#include <zlib.h>
typedef gzFile MSS_File;

#else

typedef FILE* MSS_File;

#endif


MSS_File CaptureFile;
BOOL bCaptureSave, bCaptureError;


/*-----------------------------------------------------------------------*/
/*
  Open file.
*/
MSS_File MemorySnapShot_fopen(char *pszFileName, char *pszMode)
{
#ifdef COMPRESS_MEMORYSNAPSHOT
	return gzopen(pszFileName, pszMode);
#else
	return fopen(pszFileName, pszMode);
#endif
}


/*-----------------------------------------------------------------------*/
/*
  Close file.
*/
void MemorySnapShot_fclose(MSS_File fhndl)
{
#ifdef COMPRESS_MEMORYSNAPSHOT
	gzclose(fhndl);
#else
	fclose(fhndl);
#endif
}


/*-----------------------------------------------------------------------*/
/*
  Read from file.
*/
int MemorySnapShot_fread(MSS_File fhndl, char *buf, int len)
{
#ifdef COMPRESS_MEMORYSNAPSHOT
	return gzread(fhndl, buf, len);
#else
	return fread(buf, 1, len, fhndl);
#endif
}


/*-----------------------------------------------------------------------*/
/*
  Write data to file.
*/
int MemorySnapShot_fwrite(MSS_File fhndl, char *buf, int len)
{
#ifdef COMPRESS_MEMORYSNAPSHOT
	return gzwrite(fhndl, buf, len);
#else
	return fwrite(buf, 1, len, fhndl);
#endif
}


/*-----------------------------------------------------------------------*/
/*
  Open/Create snapshot file, and set flag so 'MemorySnapShot_Store' knows
  how to handle data.
*/
BOOL MemorySnapShot_OpenFile(char *pszFileName, BOOL bSave)
{
	char szString[256];
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
		MemorySnapShot_Store(VERSION_STRING, VERSION_STRING_SIZE);
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
			sprintf(szString,"Unable to Restore Memory State.\n"
			        "File is only compatible with Hatari v%s", VersionString);
			Main_Message(szString, PROG_NAME /*, MB_OK | MB_ICONSTOP*/);
			bCaptureError = TRUE;
			return(FALSE);
		}
	}

	/* All OK */
	return(TRUE);
}


/*-----------------------------------------------------------------------*/
/*
  Close snapshot file.
*/
void MemorySnapShot_CloseFile(void)
{
	MemorySnapShot_fclose(CaptureFile);
}


/*-----------------------------------------------------------------------*/
/*
  Save/Restore data to/from file.
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
/*
  Save 'snapshot' of memory/chips/emulation variables
*/
void MemorySnapShot_Capture(char *pszFileName)
{
	/* Set to 'saving' */
	if (MemorySnapShot_OpenFile(pszFileName,TRUE))
	{
		/* Capture each files details */
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
		TOS_MemorySnapShot_Capture(TRUE);
		Video_MemorySnapShot_Capture(TRUE);
		Blitter_MemorySnapShot_Capture(TRUE);

		/* And close */
		MemorySnapShot_CloseFile();
	}

	/* Did error */
	if (bCaptureError)
		Main_Message("Unable to Save Memory State to file.", PROG_NAME /*, MB_OK | MB_ICONSTOP*/);
	else
		Main_Message("Memory State file saved.", PROG_NAME /*, MB_OK | MB_ICONINFORMATION*/);
}


/*-----------------------------------------------------------------------*/
/*
  Restore 'snapshot' of memory/chips/emulation variables
*/
void MemorySnapShot_Restore(char *pszFileName)
{
	/* Set to 'restore' */
	if (MemorySnapShot_OpenFile(pszFileName,FALSE))
	{
		/* Reset emulator to get things running */
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
		TOS_MemorySnapShot_Capture(FALSE);
		Video_MemorySnapShot_Capture(FALSE);
		Blitter_MemorySnapShot_Capture(FALSE);

		/* And close */
		MemorySnapShot_CloseFile();
	}

	/* Did error? */
	if (bCaptureError)
		Main_Message("Unable to Restore Memory State from file.", PROG_NAME /*,MB_OK | MB_ICONSTOP*/);
	else
		Main_Message("Memory State file restored.", PROG_NAME /*,MB_OK | MB_ICONINFORMATION*/);
}
