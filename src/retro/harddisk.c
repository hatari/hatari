/*
  Hatari - harddisk.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#include <string.h>
#include <stdio.h>
#include <libretro.h>

#if HAVE_STRINGS_H
#include <strings.h>
#endif

#include "main.h"
#include "main_retro.h"
#include "harddisk.h"
#include "configuration.h"
#include "file.h"
#include "floppy.h"
#include "gemdos.h"
#include "hdc.h"
#include "ide.h"
#include "log.h"
#include "ncr5380.h"
#include "reset.h"

/**
 * Insert a raw HD image into the IDE master slot and reboot so TOS
 * picks it up.
 */
void HardDisk_InsertIde(const char *path)
{
	Ide_UnInit();

	ConfigureParams.Ide[0].bUseDevice = true;
	ConfigureParams.Ide[0].nByteSwap = BYTESWAP_AUTO;
	ConfigureParams.Ide[0].nBlockSize = 512;
	strncpy(ConfigureParams.Ide[0].sDeviceFile, path,
	        sizeof(ConfigureParams.Ide[0].sDeviceFile) - 1);
	ConfigureParams.Ide[0].sDeviceFile[sizeof(ConfigureParams.Ide[0].sDeviceFile) - 1] = '\0';
	File_MakeAbsoluteName(ConfigureParams.Ide[0].sDeviceFile);

	Ide_Init();

	Log_Printf(LOG_INFO, "HardDisk (%s) inserted into IDE master slot.\n", path);

	Reset_Cold();
}

/**
 * Insert a raw HD image into ACSI slot 0 and reboot so TOS picks it up.
 */
void HardDisk_InsertAcsi(const char *path)
{
	HDC_UnInit();
	Ncr5380_UnInit();

	ConfigureParams.Acsi[0].bUseDevice = true;
	ConfigureParams.Acsi[0].nBlockSize = 512;
	ConfigureParams.Acsi[0].nScsiVersion = 1;
	strncpy(ConfigureParams.Acsi[0].sDeviceFile, path,
	        sizeof(ConfigureParams.Acsi[0].sDeviceFile) - 1);
	ConfigureParams.Acsi[0].sDeviceFile[sizeof(ConfigureParams.Acsi[0].sDeviceFile) - 1] = '\0';
	File_MakeAbsoluteName(ConfigureParams.Acsi[0].sDeviceFile);

	HDC_Init();
	Ncr5380_Init();

	Log_Printf(LOG_INFO, "HardDisk (%s) inserted into ACSI slot 0.\n", path);

	Reset_Cold();
}

/**
 * Enable GEMDOS HD directory emulation
 */
void HardDisk_SetGemdosDrive(const char *path)
{
	char dir[FILENAME_MAX];
	size_t len = strlen(path);

	if (len >= 4 && strcasecmp(path + len - 4, ".gem") == 0)
		len -= 4;
	if (len >= sizeof(dir))
		len = sizeof(dir) - 1;
	memcpy(dir, path, len);
	dir[len] = '\0';

	GemDOS_UnInitDrives();

	strncpy(ConfigureParams.HardDisk.szHardDiskDirectories[0], dir,
	        sizeof(ConfigureParams.HardDisk.szHardDiskDirectories[0]) - 1);
	ConfigureParams.HardDisk.szHardDiskDirectories[0][sizeof(ConfigureParams.HardDisk.szHardDiskDirectories[0]) - 1] = '\0';
	File_CleanFileName(ConfigureParams.HardDisk.szHardDiskDirectories[0]);
	File_MakeAbsoluteName(ConfigureParams.HardDisk.szHardDiskDirectories[0]);
	ConfigureParams.HardDisk.bUseHardDiskDirectories = true;
	ConfigureParams.HardDisk.bBootFromHardDisk = true;

	GemDOS_InitDrives();

	Log_Printf(LOG_INFO, "GEMDOS HDD emulation set to '%s'.\n",
	           ConfigureParams.HardDisk.szHardDiskDirectories[0]);

	if (retro_system_directory && retro_system_directory[0])
	{
		char boot_path[FILENAME_MAX];

		snprintf(boot_path, sizeof(boot_path), "%s%chatari%cBOOT.ST",
		         retro_system_directory, PATHSEP, PATHSEP);

		if (File_Exists(boot_path))
		{
			Floppy_SetDiskFileName(0, boot_path, NULL);
			Floppy_InsertDiskIntoDrive(0);
		}
		else
		{
			Log_Printf(LOG_WARN, "GEMDOS HDD boot floppy '%s' not found, "
			           "content may not autoboot.\n", boot_path);
		}
	}
	else
	{
		Log_Printf(LOG_WARN, "GEMDOS HDD: no system directory available, "
		           "cannot look for BOOT.ST helper floppy.\n");
	}

	Reset_Cold();
}
