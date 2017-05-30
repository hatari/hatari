/*
  Hatari - inffile.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  TOS *.INF file overloading for autostarting.
*/
const char INFFILE_fileid[] = "Hatari inffile.c : " __DATE__ " " __TIME__;

#include <SDL_endian.h>
#include <assert.h>
#include <errno.h>

#include "main.h"
#include "configuration.h"
#include "inffile.h"
#include "options.h"
#include "log.h"
#include "str.h"
#include "tos.h"

#define INF_DEBUG  0         /* doesn't remove virtual INF file after use */
#define ETOS_OWN_INF 1       /* use EmuTOS specific INF file contents */

static struct {
	FILE *file;          /* file pointer to contents of INF file */
	char *prgname;       /* TOS name of the program to auto start */
	const char *infname; /* name of the INF file TOS will try to match */
	int reso;            /* resolution setting for #E line */
/* for validation */
	int reso_id;
	const char *reso_str;
	int prgname_id;
} TosAutoStart;


/* autostarted program name will be added before the first
 * '@' character in the INF files #Z line
 * (first value is 00: TOS, 01: GEM).
 *
 * Resolution is specified in the second hex value in #E line.
 *
 * TOS versions expect both of these to be within certain
 * number of bytes from the beginning of the file, and there
 * are also TOS version specific limits on the INF file sizes.
 *
 * More documentation on the DESKTOP.INF file content:
 * http://st-news.com/issues/st-news-volume-2-issue-6/education/the-desktopinf-file/
 */

/* EmuDesk INF file format differs slightly from normal TOS */
static const char emudesk_inf[] =
"#Z 01 @\r\n"
"#E 9A 07\r\n"
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
 * later v1.x TOS versions work also with this.
 *
 * Trailing spaces on #d line are significant for TOS parsing.
 */
static const char desktop_inf[] =
"#a000000\r\n"
"#b000000\r\n"
"#c7770007000600070055200505552220770557075055507703111103\r\n"
"#d                                             \r\n"
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
bool INF_AutoStartSet(const char *name, int opt_id)
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
	TosAutoStart.prgname_id = opt_id;
	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * Set specified resolution when autostarting.
 *
 *   0: no override
 * 1-3: ST/STE resolutions:
 *      - ST low, med, high
 * 4-6: TT/Falcon resolutions:
 *      - TT med, high, low
 *      - Falcon 80 cols, N/A, 40 cols
 *
 * Return true for success, false otherwise.
 */
bool INF_AutoStartSetResolution(const char *str, int opt_id)
{
	int reso = atoi(str);
	if (reso < 1 || reso > 6)
		return false;

	TosAutoStart.reso = reso;
	TosAutoStart.reso_id = opt_id;
	TosAutoStart.reso_str = str;
	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * Validate autostart options against Hatari settings:
 * - program drive
 *
 * If there's a problem, return problematic option ID
 * and set val & err strings, otherwise just return zero.
 */
int INF_AutoStartValidate(const char **val, const char **err)
{
	const char *path = TosAutoStart.prgname;
	char drive;

	if (!path)
		return 0;

	/* validate autostart program drive */
	drive = path[0];

	if (drive == 'A')
	{
		if (ConfigureParams.DiskImage.EnableDriveA && ConfigureParams.DiskImage.szDiskFileName[0][0])
			return 0;
	}
	else if (drive == 'B')
	{
		if (ConfigureParams.DiskImage.EnableDriveB && ConfigureParams.DiskImage.szDiskFileName[1][0])
			return 0;
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
		return 0;
	}
	/* IDE */
	else if (ConfigureParams.HardDisk.bUseIdeMasterHardDiskImage && ConfigureParams.HardDisk.szIdeMasterHardDiskImage[0])
	{
		return 0;
	}
	else if (ConfigureParams.HardDisk.bUseIdeMasterHardDiskImage && ConfigureParams.HardDisk.szIdeMasterHardDiskImage[0])
	{
		return 0;
	}
	else
	{
		/* ACSI / SCSI */
		int i;
		for (i = 0; i < MAX_ACSI_DEVS; i++)
		{
			if (ConfigureParams.Acsi[i].bUseDevice && ConfigureParams.Acsi[i].sDeviceFile[0])
				return 0;
			if (ConfigureParams.Scsi[i].bUseDevice && ConfigureParams.Scsi[i].sDeviceFile[0])
				return 0;
		}
	}
	/* error */
	*val = TosAutoStart.prgname;
	*err = "Required autostart drive isn't enabled";
	return TosAutoStart.prgname_id;
}


/**
 * Resolution needs to be validated later, here, because we don't
 * know the final machine type when options are parsed, as it can
 * change later when TOS is loaded.
 *
 * If there's a problem, return problematic option ID
 * and set val & err strings, otherwise just return zero.
 */
static int INF_AutoStartValidateResolution(const char **val, const char **err)
{
	int monitor = ConfigureParams.Screen.nMonitorType;
	int extra = 0;

	/* validate resolution */
	if (!TosAutoStart.reso)
		return 0;

	*val = TosAutoStart.reso_str;

	switch(ConfigureParams.System.nMachineType)
	{
	case MACHINE_STE:
	case MACHINE_MEGA_STE:
		/* blitter bit */
		extra = 0x10;
	case MACHINE_ST:
	case MACHINE_MEGA_ST:
		if (monitor == MONITOR_TYPE_MONO && TosAutoStart.reso != 3)
		{
			TosAutoStart.reso = 3;
			Log_Printf(LOG_WARN, "With mono monitor, TOS can use only resolution %d, correcting.\n", TosAutoStart.reso);
		}
		else if (TosAutoStart.reso > 2)
		{
			*err = "invalid ST/STE color resolution";
			return TosAutoStart.reso_id;
		}
		TosAutoStart.reso_id |= extra;
		break;

	case MACHINE_FALCON:
		if (monitor == MONITOR_TYPE_MONO && TosAutoStart.reso != 3)
		{
			TosAutoStart.reso = 3;
			Log_Printf(LOG_WARN, "With mono monitor, TOS can use only resolution %d, correcting.\n", TosAutoStart.reso);
		}
		else if (TosAutoStart.reso == 5)
		{
			*err = "TT-mono is invalid Falcon resolution";
			return TosAutoStart.reso_id;
		}
		else
		{
			Log_Printf(LOG_WARN, "TOS resolution setting doesn't work with Falcon (yet)\n");
		}
		extra = 0x10;
		/* TODO:
		 * Falcon resolution setting doesn't have effect,
		 * seems that #E Falcon settings in columns 6 & 7
		 * are also needed:
		 * - line doubling / interlace
		 * - ST compat, RGB/VGA, columns & #colors
		 */
		break;

	case MACHINE_TT:
		if (monitor == MONITOR_TYPE_MONO && TosAutoStart.reso != 5)
		{
			TosAutoStart.reso = 5;
			Log_Printf(LOG_WARN, "With mono monitor, TOS can use only resolution %d, correcting.\n", TosAutoStart.reso);
		}
		TosAutoStart.reso_id |= extra;
		break;
	}
	Log_Printf(LOG_INFO, "Resulting INF file resolution: %d.\n", TosAutoStart.reso);

	if (bIsEmuTOS)
		Log_Printf(LOG_WARN, "TOS resolution setting doesn't work with EmuTOS (yet)\n");
	return 0;
}

/*-----------------------------------------------------------------------*/
/**
 * Skip rest of INF file line and return index after its end.
 */
static int skip_line(const char *contents, int offset, int size)
{
	char chr;
	for (; offset < size; offset++)
	{
		chr = contents[offset];
		if (chr == '\r' || chr == '\n')
		{
			chr = contents[++offset];
			if (chr == '\r' || chr == '\n')
				offset++;
			return offset;
		}
	}
	Log_Printf(LOG_WARN, "Malformed INF file '%s' as input, autostart likely to fail!\n", TosAutoStart.prgname);
	return offset;
}

/**
 * Return INF file autostart line format suitable for given
 * program type, based on program name extension.
 */
static const char *prg_format(const char *prgname)
{
	const char *ext;
	int size;

	size = strlen(prgname);
	if (size > 4)
		ext = prgname + size - 4;
	else
		ext = prgname;

	if (strcmp(ext, ".TTP") == 0 ||strcmp(ext, ".TOS") == 0)
		return "#Z 00 %s@\r\n"; /* TOS program */
	else
		return "#Z 01 %s@\r\n"; /* GEM program */
}


/*-----------------------------------------------------------------------*/
/**
 * Create a temporary TOS INF file which will start autostart program.
 *
 * File has TOS version specific differences, so it needs to be re-created
 * on each boot in case user changed TOS version.
 *
 * Called at end of TOS ROM loading, returns zero for OK, non-zero for error.
 */
int INF_AutoStartCreate(void)
{
	const char *contents, *infname, *prgname, *format;
	int offset, size, off_prg, off_rez;
	const char *err, *val;
	int opt_id;
	FILE *fp;
#if defined(WIN32)	/* unfortunately tmpfile() needs administrative privileges on windows, so this needs special care */
	char *ptr;
#endif

	if ((opt_id = INF_AutoStartValidateResolution(&val, &err)))
	{
		Opt_ShowError(opt_id, val, err);
		return -3;
	}

	/* in case TOS didn't for some reason close it on previous boot */
	INF_AutoStartClose(TosAutoStart.file);

	prgname = TosAutoStart.prgname;
	/* autostart not enabled? */
	if (!prgname)
		return 0;

	/* autostart not supported? */
	if (TosVersion < 0x0104)
	{
		Log_Printf(LOG_WARN, "Only TOS versions >= 1.04 support autostarting!\n");
		return 0;
	}

	if (bIsEmuTOS)
	{
		if (ConfigureParams.HardDisk.bBootFromHardDisk)
			infname = "C:\\EMUDESK.INF";
		else
			infname = "A:\\EMUDESK.INF";
#if ETOS_OWN_INF
		size = sizeof(emudesk_inf);
		contents = emudesk_inf;
#else
		size = sizeof(desktop_inf);
		contents = desktop_inf;
#endif
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

	/* create the autostart file */
#if defined(WIN32)	/* unfortunately tmpfile() needs administrative privileges on windows, so this needs special care */
	ptr = WinTmpFile();
	if (ptr != NULL)
		fp = fopen(ptr,"w+b");
	else
		fp = NULL;
#else
# if INF_DEBUG
	{
		const char *debugfile = "/tmp/hatari-desktop-inf.txt";
		fprintf(stderr, "autostart: '%s'\n", debugfile);
		fp = fopen(debugfile, "w+b");
	}
# else
	fp = tmpfile();
# endif
#endif
	if (!fp)
	{
		Log_Printf(LOG_ERROR, "Failed to create autostart file for '%s': %s!\n",
			   TosAutoStart.prgname, strerror(errno));
		return 0;
	}

	format = prg_format(prgname);

	/* find where to insert the program name and resolution */
	off_prg = off_rez = 0;
	for (offset = 0; offset < size-7; offset++)
	{
		if (contents[offset] != '#')
			continue;

		if (contents[offset+1] == 'Z')
		{
			fwrite(contents+off_prg, offset-off_prg, 1, fp);
			if (!off_prg)
				fprintf(fp, format, prgname);
			offset = skip_line(contents, offset, size-1);
			off_prg = offset;
		}
		if (contents[offset+1] == 'E')
		{
			/* INF file with autostart line missing?
			 * -> add one
			 *
			 * Assumes #Z is before #E as it seems to normally be,
			 * and should be in above static INF file contents.
			 */
			fwrite(contents+off_prg, offset-off_prg, 1, fp);
			if (!off_prg)
				fprintf(fp, format, prgname);
			/* #E line start */
			fwrite(contents+offset, 6, 1, fp);
			/* requested resolution, or default? */
			if (TosAutoStart.reso)
				fprintf(fp, "%02x", TosAutoStart.reso);
			else
				fwrite(contents+offset+6, 2, 1, fp);
			/* rest of #E */
			offset += 8;
			off_rez = offset;
			break;
		}
	}
	if (!(off_rez && off_prg))
	{
		fclose(fp);
		Log_Printf(LOG_ERROR, "Autostarting disabled, '%s' is not a valid INF file!\n", infname);
		return 0;
	}
	/* write rest of INF file & seek back to start */
	if (!(fwrite(contents+offset, size-offset-1, 1, fp) && fseek(fp, 0, SEEK_SET) == 0))
	{
		fclose(fp);
		Log_Printf(LOG_ERROR, "Autostart '%s' file writing failed!\n", TosAutoStart.prgname);
		return 0;
	}
	TosAutoStart.file = fp;
	Log_Printf(LOG_WARN, "Virtual autostart file '%s' created for '%s'.\n", infname, prgname);
	return 0;
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
