/*
  Hatari

  Memory Snapshot

  This handles the saving/restoring of the emulator's state so any game/application can be saved
  and restored at any time. This is quite complicated as we need to store all STRam, all chip states,
  all emulation variables and then things get really complicated as we need to restore file handles
  and such like.
  To help keep things simple each file has one function which is used to save/restore all variables
  that are local to it. We use one function to reduce redundancy and the function 'MemorySnapShot_Store'
  decides if it should save or restore the data.
*/

#include "main.h"
/*#include "compress.h"*/
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


//#define COMPRESS_MEMORYSNAPSHOT      // Compress snapshots to reduce disc space used

//HFILE CaptureFile;
//OFSTRUCT CaptureFileInfo;
BOOL bCaptureSave, bCaptureError;
BOOL bSaveMemoryState=FALSE, bRestoreMemoryState=FALSE;
char szSnapShotFileName[MAX_FILENAME_LENGTH];

//-----------------------------------------------------------------------
/*
  Check if need to save/restore emulation memory state, via flag 'bSaveMemoryState', and 'bRestoreMemoryState'
*/
void MemorySnapShot_CheckSaveRestore(void)
{
  // Is chose to save memory state, one of these two flags will be set
/*FIXME*/
/*
  if (bSaveMemoryState || bRestoreMemoryState) {
    Main_PauseEmulation();            // Hold things...    
    View_ToggleWindowsMouse(MOUSE_WINDOWS);      // Put mouse into ST mode
    View_LimitCursorToScreen();            // Free mouse from Window constraints

    // Do we need user to enter a filename?
    if (strlen(ConfigureParams.Memory.szMemoryCaptureFileName)<=0) {
      if (!File_OpenSelectDlg(hWnd,ConfigureParams.Memory.szMemoryCaptureFileName,FILEFILTER_MEMORYFILE,FALSE,bSaveMemoryState))
        bSaveMemoryState = bRestoreMemoryState = FALSE;
    }

    // Do save/load
    if (bSaveMemoryState)
      MemorySnapShot_Capture(ConfigureParams.Memory.szMemoryCaptureFileName);
    else if (bRestoreMemoryState)
      MemorySnapShot_Restore(ConfigureParams.Memory.szMemoryCaptureFileName);
    bSaveMemoryState = bRestoreMemoryState = FALSE;

    View_LimitCursorToClient();            // And limit mouse in Window
    View_ToggleWindowsMouse(MOUSE_ST);        // Put mouse into ST mode
    Main_UnPauseEmulation();            // And off we go...
  }
*/
}

//-----------------------------------------------------------------------
/*
  Get filename to save/restore to, so can compress
*/
char *MemorySnapShot_CompressBegin(char *pszFileName)
{
#ifdef COMPRESS_MEMORYSNAPSHOT
  // Create temporary filename
  sprintf(szSnapShotFileName,"%s/%s",szWorkingDir,"snapshot.mem");
  return(szSnapShotFileName);
#else  //COMPRESS_MEMORYSNAPSHOT
  return(pszFileName);
#endif  //COMPRESS_MEMORYSNAPSHOT
}

//-----------------------------------------------------------------------
/*
  Compress memory snap shot
*/
void MemorySnapShot_CompressEnd(char *pszFileName)
{
#ifdef COMPRESS_MEMORYSNAPSHOT
  // Compress file from temporary to final destination
  Compress_Pack_File(szSnapShotFileName,pszFileName);
  // And delete temporary
  File_Delete(szSnapShotFileName);
#endif  //COMPRESS_MEMORYSNAPSHOT
}

//-----------------------------------------------------------------------
/*
  Uncompress memory snap shot to temporary file
*/
char *MemorySnapShot_UnCompressBegin(char *pszFileName)
{
#ifdef COMPRESS_MEMORYSNAPSHOT
  // Uncompress to temporary file
  sprintf(szSnapShotFileName,"%s/%s",szWorkingDir,"snapshot.mem");
  Compress_UnPack_File(pszFileName,szSnapShotFileName);
  return(szSnapShotFileName);
#else  //COMPRESS_MEMORYSNAPSHOT
  return(pszFileName);
#endif  //COMPRESS_MEMORYSNAPSHOT
}

//-----------------------------------------------------------------------
/*
  Clean up after uncompression
*/
void MemorySnapShot_UnCompressEnd(void)
{
  // And delete temporary
  File_Delete(szSnapShotFileName);
}

//-----------------------------------------------------------------------
/*
  Open/Create snapshot file, and set flag so 'MemorySnapShot_Store' knows how to handle data
*/
BOOL MemorySnapShot_OpenFile(char *pszFileName,BOOL bSave)
{
/* FIXME */
/*
  char szString[256];
  char VersionString[VERSION_STRING_SIZE];

  // Set error
  bCaptureError = FALSE;

  // Open file, set flag so 'MemorySnapShot_Store' can load to/save from file
  if (bSave) {
    // Save
    CaptureFile = OpenFile(pszFileName,&CaptureFileInfo,OF_CREATE | OF_WRITE);
    if (CaptureFile==HFILE_ERROR) {
      bCaptureError = TRUE;
      return(FALSE);
    }
    bCaptureSave = TRUE;
    // Store version string
    MemorySnapShot_Store(VERSION_STRING,VERSION_STRING_SIZE);
  }
  else {
    // Restore
    CaptureFile = OpenFile(pszFileName,&CaptureFileInfo,OF_READ);
    if (CaptureFile==HFILE_ERROR) {
      bCaptureError = TRUE;
      return(FALSE);
    }
    bCaptureSave = FALSE;
    // Restore version string
    MemorySnapShot_Store(VersionString,VERSION_STRING_SIZE);
    // Does match current version?
    if (stricmp(VersionString,VERSION_STRING)) {
      // No, inform user and error
      sprintf(szString,"Unable to Restore Memory State.\nFile is only compatible with Hatari v%s",VersionString);
      Main_Message(szString,PROG_NAME,MB_OK | MB_ICONSTOP);
      bCaptureError = TRUE;
      return(FALSE);
    }
  }

  // All OK
  return(TRUE);
*/
return FALSE;
}

//-----------------------------------------------------------------------
/*
  Close snapshot file
*/
void MemorySnapShot_CloseFile(void)
{
//FIXME  _lclose(CaptureFile);
}

//-----------------------------------------------------------------------
/*
  Save/Restore data to/from file
*/
void MemorySnapShot_Store(void *pData, int Size)
{
/*FIXME*/
/*
  long nBytes;

  // Check no file errors
  if (CaptureFile!=HFILE_ERROR) {
    // Saving or Restoring?
    if (bCaptureSave)
      nBytes = _hwrite(CaptureFile,(char *)pData,Size);
    else
      nBytes = _hread(CaptureFile,(char *)pData,Size);

    // Did save OK?
    if (nBytes==HFILE_ERROR)
      bCaptureError = TRUE;
    else if (nBytes!=Size)
      bCaptureError = TRUE;
  }
*/
}

//-----------------------------------------------------------------------
/*
  Save 'snapshot' of memory/chips/emulation variables
*/
void MemorySnapShot_Capture(char *pszFileName)
{
/*FIXME*/
/*
  char *pszSnapShotFileName;

  // Wait...
  SetCursor(Cursors[CURSOR_HOURGLASS]);

  // If to be compressed, return temporary filename
  pszSnapShotFileName = MemorySnapShot_CompressBegin(pszFileName);

  // Set to 'saving'
  if (MemorySnapShot_OpenFile(pszSnapShotFileName,TRUE)) {
    // Capture each files details
    Main_MemorySnapShot_Capture(TRUE);
    FDC_MemorySnapShot_Capture(TRUE);
    Floppy_MemorySnapShot_Capture(TRUE);
    GemDOS_MemorySnapShot_Capture(TRUE);
    IKBD_MemorySnapShot_Capture(TRUE);
    Int_MemorySnapShot_Capture(TRUE);
    M68000_MemorySnapShot_Capture(TRUE);
    M68000_Decode_MemorySnapShot_Capture(TRUE);
    MFP_MemorySnapShot_Capture(TRUE);
    PSG_MemorySnapShot_Capture(TRUE);
    Sound_MemorySnapShot_Capture(TRUE);
    TOS_MemorySnapShot_Capture(TRUE);
    Video_MemorySnapShot_Capture(TRUE);

    // And close
    MemorySnapShot_CloseFile();
  }

  // And compress, if need to
  MemorySnapShot_CompressEnd(pszFileName);

  // We're back
  SetCursor(Cursors[CURSOR_ARROW]);

  // Did error
  if (bCaptureError)
    Main_Message("Unable to Save Memory State to file.",PROG_NAME,MB_OK | MB_ICONSTOP);
  else
    Main_Message("Memory State file saved.",PROG_NAME,MB_OK | MB_ICONINFORMATION);
*/
}

//-----------------------------------------------------------------------
/*
  Restore 'snapshot' of memory/chips/emulation variables
*/
void MemorySnapShot_Restore(char *pszFileName)
{
/*FIXME*/
/*
  char *pszSnapShotFileName;

  // Wait...
  SetCursor(Cursors[CURSOR_HOURGLASS]);

  // If to be uncompressed, return temporary filename
  pszSnapShotFileName = MemorySnapShot_UnCompressBegin(pszFileName);

  // Set to 'restore'
  if (MemorySnapShot_OpenFile(pszSnapShotFileName,FALSE)) {
    // Reset emulator to get things running
    Reset_Cold();

    // Capture each files details
    Main_MemorySnapShot_Capture(FALSE);
    FDC_MemorySnapShot_Capture(FALSE);
    Floppy_MemorySnapShot_Capture(FALSE);
    GemDOS_MemorySnapShot_Capture(FALSE);
    IKBD_MemorySnapShot_Capture(FALSE);
    Int_MemorySnapShot_Capture(FALSE);
    M68000_MemorySnapShot_Capture(FALSE);
    M68000_Decode_MemorySnapShot_Capture(FALSE);
    MFP_MemorySnapShot_Capture(FALSE);
    PSG_MemorySnapShot_Capture(FALSE);
    Sound_MemorySnapShot_Capture(FALSE);
    TOS_MemorySnapShot_Capture(FALSE);
    Video_MemorySnapShot_Capture(FALSE);

    // And close
    MemorySnapShot_CloseFile();
  }

  // And clean up
  MemorySnapShot_UnCompressEnd();

  // We're back
  SetCursor(Cursors[CURSOR_ARROW]);

  // Did error
  if (bCaptureError)
    Main_Message("Unable to Restore Memory State from file.",PROG_NAME,MB_OK | MB_ICONSTOP);
  else
    Main_Message("Memory State file restored.",PROG_NAME,MB_OK | MB_ICONINFORMATION);
*/
}
