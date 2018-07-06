/*
 * Hatari - NCR 5380 SCSI controller emulation
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 */
const char NCR5380_fileid[] = "Hatari ncr5380.c";

#include "main.h"
#include "configuration.h"
#include "file.h"
#include "hdc.h"
#include "ncr5380.h"

#define WITH_NCR5380 0

#if WITH_NCR5380
static SCSI_CTRLR ScsiBus;
#endif

void Ncr5380_Init(void)
{
#if WITH_NCR5380
	int i;

	memset(&ScsiBus, 0, sizeof(ScsiBus));
	ScsiBus.respbufsize = 512;
	ScsiBus.resp = malloc(ScsiBus.respbufsize);
	if (!ScsiBus.resp)
	{
		perror("HDC_Init");
		return;
	}

	for (i = 0; i < MAX_SCSI_DEVS; i++)
	{
		if (!ConfigureParams.Scsi[i].bUseDevice)
			continue;
		HDC_InitDevice(&ScsiBus.devs[i], ConfigureParams.Scsi[i].sDeviceFile);
	}
#endif
}

void Ncr5380_UnInit(void)
{
#if WITH_NCR5380
	int i;

	for (i = 0; i < MAX_SCSI_DEVS; i++)
	{
		if (!ScsiBus.devs[i].enabled)
			continue;
		File_UnLock(ScsiBus.devs[i].image_file);
		fclose(ScsiBus.devs[i].image_file);
		ScsiBus.devs[i].image_file = NULL;
		ScsiBus.devs[i].enabled = false;
	}
	free(ScsiBus.resp);
	ScsiBus.resp = NULL;
#endif
}

/**
 * Emulate external reset "pin": Clear registers etc.
 */
void Ncr5380_Reset(void)
{
}

/**
 * Write a command byte to the NCR 5380 SCSI controller
 */
void Ncr5380_WriteByte(int addr, Uint8 byte)
{
}

/**
 * Read a command byte from the NCR 5380 SCSI controller
 */
Uint8 Ncr5380_ReadByte(int addr)
{
	return 0;
}
