/*
  Hatari - ymFormat.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  YM File output, for use with STSound etc...
*/
const char YMFormat_fileid[] = "Hatari ymFormat.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "file.h"
#include "log.h"
#include "psg.h"
#include "sound.h"
#include "ymFormat.h"


#define YM_MAX_VBLS    (50*60*8)            /* 50=1 second, 50*60=1 minute, 50*60*8=8 minutes, or 24000 */
#define YM_RECORDSIZE  (4+(YM_MAX_VBLS*NUM_PSG_SOUND_REGISTERS))  /* ~330k for 8 minutes */

bool bRecordingYM = false;
static int nYMVBLS = 0;
static Uint8 *pYMData, *pYMWorkspace = NULL;
static char *pszYMFileName = NULL;

/*-----------------------------------------------------------------------*/
/**
 * Start recording YM registers to workspace
 */
bool YMFormat_BeginRecording(const char *filename)
{
	/* Free any previous data, don't save */
	bRecordingYM = false;
	YMFormat_EndRecording();

	/* Make sure we have a proper filename to use */
	if (!filename || strlen(filename) <= 0)
	{
		return false;
	}
	pszYMFileName = strdup(filename);
	if (!pszYMFileName)
	{
		return false;
	}

	/* Create YM workspace */
	pYMWorkspace = (Uint8 *)malloc(YM_RECORDSIZE);

	if (!pYMWorkspace)
	{
		/* Failed to allocate memory, cannot record */
		free(pszYMFileName);
		pszYMFileName = NULL;
		return false;
	}

	/* Get workspace pointer and store 4 byte header */
	pYMData = pYMWorkspace;
	*pYMData++ = 'Y';
	*pYMData++ = 'M';
	*pYMData++ = '3';
	*pYMData++ = '!';

	bRecordingYM = true;          /* Ready to record */
	nYMVBLS = 0;                  /* Number of VBLs of information */

	/* And inform user */
	Log_AlertDlg(LOG_INFO, "YM sound data recording has been started.");

	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * Convert YM data to stream for output
 *
 * Data is:
 *   4 Byte header 'YM3!'
 *   VBL Count x 14 PSG registers
 * BUT
 *   We need data in a register stream, eg Reg 0, VBL 1, VBL 2, VBL n and then next register...
 * 
 * Convert to new workspace and return true if all OK.
 */
static bool YMFormat_ConvertToStreams(void)
{
	Uint8 *pNewYMWorkspace;
	Uint8 *pTmpYMData, *pNewYMData;
	Uint8 *pTmpYMStream, *pNewYMStream;
	int Reg, Count;

	/* Allocate new workspace to convert data to */
	pNewYMWorkspace = (Uint8 *)malloc(YM_RECORDSIZE);
	if (pNewYMWorkspace)
	{
		/* Convert data, first copy over header */
		pTmpYMData = pYMWorkspace;
		pNewYMData = pNewYMWorkspace;
		*pNewYMData++ = *pTmpYMData++;
		*pNewYMData++ = *pTmpYMData++;
		*pNewYMData++ = *pTmpYMData++;
		*pNewYMData++ = *pTmpYMData++;

		/* Now copy over each stream */
		for(Reg=0; Reg<NUM_PSG_SOUND_REGISTERS; Reg++)
		{
			/* Get pointer to source / destination */
			pTmpYMStream = pTmpYMData + Reg;
			pNewYMStream = pNewYMData + (Reg*nYMVBLS);

			/* Copy recording VBLs worth */
			for(Count=0; Count<nYMVBLS; Count++)
			{
				*pNewYMStream++ = *pTmpYMStream;
				pTmpYMStream += NUM_PSG_SOUND_REGISTERS;
			}
		}

		/* Delete old workspace and assign new */
		free(pYMWorkspace);
		pYMWorkspace = pNewYMWorkspace;

		return true;
	}
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * End recording YM registers and save as '.YM' file
 */
void YMFormat_EndRecording(void)
{
	/* Recording, have recorded information? */
	if (bRecordingYM && pszYMFileName && pYMWorkspace && nYMVBLS)
	{
		/* Convert YM to correct format(list of register 1, then register 2...) */
		if (YMFormat_ConvertToStreams())
		{
			/* Save YM File */
			File_Save(pszYMFileName, pYMWorkspace,(size_t)(nYMVBLS*NUM_PSG_SOUND_REGISTERS)+4, false);
			/* And inform user */
			Log_AlertDlg(LOG_INFO, "YM sound data recording has been stopped.");
		}
		else
			Log_AlertDlg(LOG_ERROR, "YM sound data conversion failed!");

	}
	/* And free */
	if (pYMWorkspace)
	{
		free(pYMWorkspace);
		pYMWorkspace = NULL;
	}
	if (pszYMFileName)
	{
		free(pszYMFileName);
		pszYMFileName = NULL;
	}
	/* Stop recording */
	bRecordingYM = false;
}


/*-----------------------------------------------------------------------*/
/**
 * Store a VBLs worth of YM registers to workspace - call each VBL
 */
void YMFormat_UpdateRecording(void)
{
	int i;

	/* Can record this VBL information? */
	if (bRecordingYM)
	{
		/* Copy VBL registers to workspace */
		for(i=0; i<(NUM_PSG_SOUND_REGISTERS-1); i++)
			*pYMData++ = SoundRegs[i];
		/* Handle register '13'(PSG_REG_ENV_SHAPE) correctly - store 0xFF is did not write to this frame */
		if (bEnvelopeFreqFlag)
			*pYMData++ = SoundRegs[PSG_REG_ENV_SHAPE];
		else
			*pYMData++ = 0xff;

		/* Increase VBL count */
		nYMVBLS++;
		/* If run out of workspace, just save */
		if (nYMVBLS>=YM_MAX_VBLS)
			YMFormat_EndRecording();
	}
}

