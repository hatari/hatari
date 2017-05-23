/*
  Hatari - inffile.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  TOS *.INF file overloading for autostarting.
*/
const char INFFILE_fileid[] = "Hatari inffile.c : " __DATE__ " " __TIME__;

#include <SDL_endian.h>
#include <assert.h>

#include "main.h"
#include "configuration.h"
#include "inffile.h"
#include "log.h"
#include "str.h"
#include "tos.h"

static struct {
	FILE *file;          /* file pointer to contents of INF file */
	char *prgname;       /* TOS name of the program to auto start */
	const char *infname; /* name of the INF file TOS will try to match */
} TosAutoStart;

/* autostarted program name will be added befere first
 * '@' character in the INF files
 */

/* EmuDesk INF file format differs slightly from normal TOS */
static const char emudesk_inf[] =
"#E 9A 07\r\n"
"#Z 01 @\r\n"
"#W 00 00 02 06 26 0C 08 C:\\*.*@\r\n"
"#W 00 00 02 08 26 0C 00 @\r\n"
"#W 00 00 02 0A 26 0C 00 @\r\n"
"#W 00 00 02 0D 26 0C 00 @\r\n"
"#M 00 00 01 FF A DISK A@ @\r\n"
"#M 01 00 01 FF B DISK B@ @\r\n"
"#M 02 00 01 FF C DISK C@ @\r\n"
"#F FF 28 @ *.*@\r\n"
"#D FF 02 @ *.*@\r\n"
"#G 08 FF *.APP@ @\r\n"
"#G 08 FF *.PRG@ @\r\n"
"#P 08 FF *.TTP@ @\r\n"
"#F 08 FF *.TOS@ @\r\n"
"#T 00 03 03 FF   TRASH@ @\r\n";

/* TOS v1.04 works only with DESKTOP.INF from that version
 * (it crashes with newer INF after autobooted program exits),
 * later v1.x TOS versions work also with this
 */
static const char desktop_inf[] =
"#a000000\r\n"
"#b000000\r\n"
"#c7770007000600070055200505552220770557075055507703111103\r\n"
"#d\r\n"
"#Z 01 @\r\n"
"#E 18 11\r\n"
"#W 00 00 00 07 26 0C 09 C:\\*.*@\r\n"
"#W 00 00 02 0B 26 09 00 @\r\n"
"#W 00 00 0A 0F 1A 09 00 @\r\n"
"#W 00 00 0E 01 1A 09 00 @\r\n"
"#M 01 00 00 FF C HARD DISK@ @\r\n"
"#M 00 00 00 FF A FLOPPY DISK@ @\r\n"
"#M 00 01 00 FF B FLOPPY DISK@ @\r\n"
"#T 00 03 02 FF   TRASH@ @\r\n"
"#F FF 04   @ *.*@\r\n"
"#D FF 01   @ *.*@\r\n"
"#G 03 FF   *.APP@ @\r\n"
"#G 03 FF   *.PRG@ @\r\n"
"#P 03 FF   *.TTP@ @\r\n"
"#F 03 04   *.TOS@ @\r\n"
"\032\r\n";

/* TOS v2.x and newer have also different format, using
 * TOS v1.04 INF file would result in bogus resolution with TOS v4
 */
static const char newdesk_inf[] =
"#a000000\r\n"
"#b000000\r\n"
"#c7770007000600070055200505552220770557075055507703111103\r\n"
"#d\r\n"
"#Z 01 @\r\n"
"#K 4F 53 4C 00 46 42 43 57 45 58 00 00 00 00 00 00 00 00 00 00 00 00 00 52 00 00 4D 56 50 00 @\r\n"
"#E 18 01 00 06\r\n"
"#Q 41 40 43 40 43 40\r\n"
"#W 00 00 00 07 26 0C 00 C:\\*.*@\r\n"
"#W 00 00 02 0B 26 09 00 @\r\n"
"#W 00 00 0A 0F 1A 09 00 @\r\n"
"#W 00 00 0E 01 1A 09 00 @\r\n"
"#W 00 00 04 07 26 0C 00 @\r\n"
"#W 00 00 0C 0B 26 09 00 @\r\n"
"#W 00 00 08 0F 1A 09 00 @\r\n"
"#W 00 00 06 01 1A 09 00 @\r\n"
"#M 00 01 00 FF C HARD DISK@ @\r\n"
"#M 00 00 00 FF A FLOPPY DISK@ @\r\n"
"#M 01 00 00 FF B FLOPPY DISK@ @\r\n"
"#T 00 03 02 FF   TRASH@ @\r\n"
"#N FF 04 000 @ *.*@ @\r\n"
"#D FF 01 000 @ *.*@ @\r\n"
"#G 03 FF 000 *.APP@ @ @\r\n"
"#G 03 FF 000 *.PRG@ @ @\r\n"
"#Y 03 FF 000 *.GTP@ @ @\r\n"
"#P 03 FF 000 *.TTP@ @ @\r\n"
"#F 03 04 000 *.TOS@ @ @\r\n";


/*-----------------------------------------------------------------------*/
/**
 * Set name of program that will be auto started after TOS boots.
 * Supported only from TOS 1.04 forward.
 *
 * If program lacks a path, "C:\" will be added.
 *
 * Returns true if OK, false for obviously invalid path specification.
 */
bool INF_AutoStartSet(const char *name)
{
	char *prgname;
	int len = strlen(name);
	char drive = toupper(name[0]);

	if (drive >= 'A' && drive <= 'Z' && name[1] == ':')
	{
		/* full path */
		const char *ptr;
		int offset;
		prgname = malloc(len+2);
		ptr = strrchr(name, '\\');
		if (ptr)
			offset = ptr - name + 1;
		else
			offset = 2;
		/* copy/upcase path part */
		memcpy(prgname, name, offset);
		prgname[offset] = '\0';
		Str_ToUpper(prgname);

		if (name[2] != '\\')
		{
			/* NOT OK: A:DIR\NAME.PRG */
			if (ptr)
			{
				free(prgname);
				return false;
			}
			/* A:NAME.PRG -> A:\NAME.PRG */
			prgname[offset] = '\\';
			/* copy/upcase file part */
			Str_Filename2TOSname(name+offset, prgname+offset+1);
		} else {
			/* copy/upcase file part */
			Str_Filename2TOSname(name+offset, prgname+offset);
		}
	}
	else if (strchr(name, '\\'))
	{
		/* partial path not accepted */
		return false;
	}
	else
	{
		/* just program -> add path */
		prgname = malloc(3 + len + 1);
		strcpy(prgname, "C:\\");
		Str_Filename2TOSname(name, prgname+3);
	}
	if (TosAutoStart.prgname)
		free(TosAutoStart.prgname);
	TosAutoStart.prgname = prgname;
	return true;
}

/*-----------------------------------------------------------------------*/
/**
 * Trivial checks on whether autostart program drive may exist.
 *
 * Return NULL if it could, otherwise return the invalid autostart path.
 */
const char *INF_AutoStartValidate(void)
{
	char drive;
	const char *path = TosAutoStart.prgname;

	if (!path)
		return NULL;
	drive = path[0];

	if (drive == 'A')
	{
		if (ConfigureParams.DiskImage.EnableDriveA && ConfigureParams.DiskImage.szDiskFileName[0][0])
			return NULL;
	}
	else if (drive == 'B')
	{
		if (ConfigureParams.DiskImage.EnableDriveB && ConfigureParams.DiskImage.szDiskFileName[1][0])
			return NULL;
	}
	/* exact drive checking for hard drives would require:
	 *
	 * For images:
	 * - finding out what partitions each of the IDE master &
	 *   Slave, 8 ACSI, and 8 SCSI images do have, *and*
	 * - finding out which of those partitions the native Atari
	 *   harddisk driver happens to support...
	 * -> not feasible
	 *
	 * For GEMDOS HD:
	 * - If multiple partitions are specified, which ones
	 * - If not, what is the single partition drive letter
	 *
	 * So, just check that some harddisk is enabled for C: ->
	 */

	/* GEMDOS HD */
	else if (ConfigureParams.HardDisk.bUseHardDiskDirectories && ConfigureParams.HardDisk.szHardDiskDirectories[0])
	{
		return NULL;
	}
	/* IDE */
	else if (ConfigureParams.HardDisk.bUseIdeMasterHardDiskImage && ConfigureParams.HardDisk.szIdeMasterHardDiskImage[0])
	{
		return NULL;
	}
	else if (ConfigureParams.HardDisk.bUseIdeMasterHardDiskImage && ConfigureParams.HardDisk.szIdeMasterHardDiskImage[0])
	{
		return NULL;
	}
	else
	{
		/* ACSI / SCSI */
		int i;
		for (i = 0; i < MAX_ACSI_DEVS; i++)
		{
			if (ConfigureParams.Acsi[i].bUseDevice && ConfigureParams.Acsi[i].sDeviceFile[0])
				return NULL;
			if (ConfigureParams.Scsi[i].bUseDevice && ConfigureParams.Scsi[i].sDeviceFile[0])
				return NULL;
		}
	}
	/* error */
	return path;
}

/*-----------------------------------------------------------------------*/
/**
 * Create a temporary TOS INF file which will start autostart program.
 * 
 * File has TOS version specific differences, so it needs to be re-created
 * on each boot in case user changed TOS version.
 * 
 * Called at end of TOS ROM loading.
 */
void INF_AutoStartCreate(void)
{
	const char *contents, *infname, *prgname;
	int offset, size;
	FILE *fp;
#if defined(WIN32)	/* unfortunately tmpfile() needs administrative privileges on windows, so this needs special care */
	char *ptr;
#endif

	/* in case TOS didn't for some reason close it on previous boot */
	INF_AutoStartClose(TosAutoStart.file);

	prgname = TosAutoStart.prgname;
	/* autostart not enabled? */
	if (!prgname)
		return;

	/* autostart not supported? */
	if (TosVersion < 0x0104)
	{
		Log_Printf(LOG_WARN, "Only TOS versions >= 1.04 support autostarting!\n");
		return;
	}

	if (bIsEmuTOS)
	{
		if (ConfigureParams.HardDisk.bBootFromHardDisk)
			infname = "C:\\EMUDESK.INF";
		else
			infname = "A:\\EMUDESK.INF";
		size = sizeof(emudesk_inf);
		contents = emudesk_inf;
	}
	/* need to match file TOS searches first */
	else if (TosVersion >= 0x0200)
	{
		infname = "NEWDESK.INF";
		size = sizeof(newdesk_inf);
		contents = newdesk_inf;
	}
	else
	{
		infname = "DESKTOP.INF";
		size = sizeof(desktop_inf);
		contents = desktop_inf;
	}
	/* infname needs to be exactly the same string that given
	 * TOS version gives for GEMDOS to find.
	 */
	TosAutoStart.infname = infname;

	/* find where to insert the program name */
	for (offset = 0; offset < size; offset++)
	{
		if (contents[offset] == '@')
			break;
	}
	assert(offset < size);

	/* create the autostart file */
#if defined(WIN32)	/* unfortunately tmpfile() needs administrative privileges on windows, so this needs special care */
	ptr=WinTmpFile();
	if( ptr!=NULL )
		fp=fopen(ptr,"w+b");
	else
		fp=NULL;
#else
	fp = tmpfile();
#endif

	if (!(fp
	      && fwrite(contents, offset, 1, fp) == 1
	      && fwrite(prgname, strlen(prgname), 1, fp) == 1
	      && fwrite(contents+offset, size-offset-1, 1, fp) == 1
	      && fseek(fp, 0, SEEK_SET) == 0))
	{
		if (fp)
			fclose(fp);
		Log_Printf(LOG_ERROR, "Failed to create autostart file for '%s'!\n", TosAutoStart.prgname);
		return;
	}
	TosAutoStart.file = fp;
	Log_Printf(LOG_WARN, "Virtual autostart file '%s' created for '%s'.\n", infname, prgname);
}


/*-----------------------------------------------------------------------*/
/**
 * Whether autostarting needs GEMDOS
 * interception or Fopen() check enabling
 */
bool INF_AutoStarting(autostart_t t)
{
	if (t == AUTOSTART_FOPEN)
		return (bool)TosAutoStart.file;
	else
		return (bool)TosAutoStart.prgname;
}

/*-----------------------------------------------------------------------*/
/**
 * If given name matches autostart file, return its handle, NULL otherwise
 */
FILE *INF_AutoStartOpen(const char *filename)
{
	if (TosAutoStart.file && strcmp(filename, TosAutoStart.infname) == 0)
	{
		/* whether to "autostart" also exception debugging? */
		if (ConfigureParams.Log.nExceptionDebugMask & EXCEPT_AUTOSTART)
		{
			ExceptionDebugMask = ConfigureParams.Log.nExceptionDebugMask & ~EXCEPT_AUTOSTART;
			fprintf(stderr, "Exception debugging enabled (0x%x).\n", ExceptionDebugMask);
		}
		Log_Printf(LOG_WARN, "Autostart file '%s' for '%s' matched.\n", filename, TosAutoStart.prgname);
		return TosAutoStart.file;
	}
	return NULL;
}

/*-----------------------------------------------------------------------*/
/**
 * If given handle matches autostart file, close it and return true,
 * false otherwise.
 */
bool INF_AutoStartClose(FILE *fp)
{
	if (fp && fp == TosAutoStart.file)
	{
		/* Remove autostart INF file after TOS has
		 * read it enough times to do autostarting.
		 * Otherwise user may try change desktop settings
		 * and save them, but they would be lost.
		 */
		fclose(TosAutoStart.file);
		TosAutoStart.file = NULL;
		Log_Printf(LOG_WARN, "Autostart file removed.\n");
		return true;
	}
	return false;
}
