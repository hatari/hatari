/*
  Hatari

  YM File output, for use with STSound etc...
*/

#include "main.h"
#include "dialog.h"
#include "file.h"
#include "memAlloc.h"
#include "misc.h"
#include "psg.h"
#include "screen.h"
#include "sound.h"
#include "statusBar.h"
#include "view.h"
#include "ymFormat.h"

#define YM_MAX_VBLS    (50*60*8)            /* 50=1 second, 50*60=1 minute, 50*60*8=8 minutes, or 24000 */
#define YM_RECORDSIZE  (4+(YM_MAX_VBLS*NUM_PSG_SOUND_REGISTERS))  /* ~330k for 8 minutes */

BOOL bRecordingYM = FALSE;
int nYMVBLS = 0;
unsigned char *pYMWorkspace = NULL, *pYMData;


/*-----------------------------------------------------------------------*/
/*
  Start recording YM registers to workspace
*/
BOOL YMFormat_BeginRecording(char *pszYMFileName)
{
  BOOL bSaveYM=FALSE, bWasInWindowsMouse;

  /* Free any previous data, don't save */
  YMFormat_FreeRecording();

  /* Make sure we have a filename to use, ask user if not */
  if (strlen(pszYMFileName)<=0) {
    /* No, back to Windows so can show dialog */
    Screen_ReturnFromFullScreen();
    /* Back to Windows mouse */
//FIXME    bWasInWindowsMouse = View_ToggleWindowsMouse(MOUSE_WINDOWS);
    /* Ask user for filename */
    if (File_OpenSelectDlg(/*hWnd,*/pszYMFileName,FILEFILTER_YMFILE,FALSE,TRUE))
      bSaveYM = TRUE;
    /* If we were in ST mouse mode, revert back */
//    if (!bWasInWindowsMouse)
//FIXME      View_ToggleWindowsMouse(MOUSE_ST);
  }
  else
    bSaveYM = TRUE;

  /* OK to save? */
  if (bSaveYM) {
    /* Create YM workspace */
    pYMWorkspace = (unsigned char *)Memory_Alloc(YM_RECORDSIZE);
    if (pYMWorkspace) {
      /* Get workspace pointer and store 4 byte header */
      pYMData = pYMWorkspace;
      *pYMData++ = 'Y';
      *pYMData++ = 'M';
      *pYMData++ = '3';
      *pYMData++ = '!';

      bRecordingYM = TRUE;        /* Ready to record */
      nYMVBLS = 0;          /* Number of VBLs of information */

      /* Set status bar */
      StatusBar_SetIcon(STATUS_ICON_SOUND,ICONSTATE_ON);
      /* And inform user */
      Main_Message("YM Sound data recording started.",PROG_NAME /*,MB_OK|MB_ICONINFORMATION*/);
    }
    else {
      /* Failed to allocate memory, cannot record */
      bRecordingYM = FALSE;
    }
  }

  return(bSaveYM);
}


/*-----------------------------------------------------------------------*/
/*
  End recording YM registers and save as '.YM' file
*/
void YMFormat_EndRecording()
{
  /* Turn off icon */
  StatusBar_SetIcon(STATUS_ICON_SOUND,ICONSTATE_OFF);

  /* Have recorded information? */
  if (pYMWorkspace && nYMVBLS) {
    /* Convert YM to correct format(list of register 1, then register 2...) */
    if (YMFormat_ConvertToStreams()) {
      /* Save YM File */
      if (0 /*FIXME:*//*strlen(ConfigureParams.Sound.szYMCaptureFileName)>0*/ ) {
//FIXME        File_Save(/*hWnd,*/ConfigureParams.Sound.szYMCaptureFileName,pYMWorkspace,(long)(nYMVBLS*NUM_PSG_SOUND_REGISTERS)+4,FALSE);
        /* And inform user(this only happens from dialog) */
        Main_Message("YM Sound data recording stopped.",PROG_NAME /*,MB_OK|MB_ICONINFORMATION*/);
      }
    }
  }

  /* And free */
  YMFormat_FreeRecording();
}


/*-----------------------------------------------------------------------*/
/*
  Free up any resources used by YM recording
*/
void YMFormat_FreeRecording(void)
{
  /* Free workspace */
  if (pYMWorkspace)
    Memory_Free(pYMWorkspace);
  pYMWorkspace = NULL;

  /* Stop recording */
  bRecordingYM = FALSE;
}


/*-----------------------------------------------------------------------*/
/*
  Store a VBLs worth of YM registers to workspace - call each VBL
*/
void YMFormat_UpdateRecording(void)
{
  int i;

  /* Can record this VBL information? */
  if (bRecordingYM) {
    /* Copy VBL registers to workspace */
    for(i=0; i<(NUM_PSG_SOUND_REGISTERS-1); i++)
      *pYMData++ = PSGRegisters[i];
    /* Handle register '13'(PSG_REG_ENV_SHAPE) correctly - store 0xFF is did not write to this frame */
    if (bEnvelopeFreqFlag)
      *pYMData++ = PSGRegisters[PSG_REG_ENV_SHAPE];
    else
      *pYMData++ = 0xff;

    /* Increase VBL count */
    nYMVBLS++;
    /* If run out of workspace, just save */
    if (nYMVBLS>=YM_MAX_VBLS)
      YMFormat_EndRecording(NULL);
  }
}


/*-----------------------------------------------------------------------*/
/*
  Convert YM data to stream for output

  Data is:
    4 Byte header 'YM3!'
    VBL Count x 14 PSG registers
  BUT
    We need data in a register stream, eg Reg 0, VBL 1, VBL 2, VBL n and then next register...

  Convert to new workspace and return TRUE if all OK
*/
BOOL YMFormat_ConvertToStreams(void)
{
  unsigned char *pNewYMWorkspace;
  unsigned char *pYMData, *pNewYMData;
  unsigned char *pYMStream, *pNewYMStream;
  int Reg, Count;

  /* Allocate new workspace to convert data to */
  pNewYMWorkspace = (unsigned char *)Memory_Alloc(YM_RECORDSIZE);
  if (pNewYMWorkspace) {
    /* Convert data, first copy over header */
    pYMData = pYMWorkspace;
    pNewYMData = pNewYMWorkspace;
    *pNewYMData++ = *pYMData++;
    *pNewYMData++ = *pYMData++;
    *pNewYMData++ = *pYMData++;
    *pNewYMData++ = *pYMData++;

    /* Now copy over each stream */
    for(Reg=0; Reg<NUM_PSG_SOUND_REGISTERS; Reg++) {
      /* Get pointer to source / destination */
      pYMStream = pYMData + Reg;
      pNewYMStream = pNewYMData + (Reg*nYMVBLS);

      /* Copy recording VBLs worth */
      for(Count=0; Count<nYMVBLS; Count++) {
        *pNewYMStream++ = *pYMStream;
        pYMStream += NUM_PSG_SOUND_REGISTERS;
      }
    }

    /* Delete old workspace and assign new */
    Memory_Free(pYMWorkspace);
    pYMWorkspace = pNewYMWorkspace;

    return(TRUE);
  }
  else
    return(FALSE);
}
