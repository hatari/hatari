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
#include "gemdos.h"
#include "file.h"
#include "log.h"
#include "str.h"
#include "tos.h"
#include "vdi.h"

#define INF_DEBUG  0         /* debug output + leaves virtual INF file behind */

static struct {
	FILE *file;          /* file pointer to contents of INF file */
	char *prgname;       /* TOS name of the program to auto start */
	const char *infname; /* name of the INF file TOS will try to match */
	int reso;            /* resolution setting value request for #E line */
/* for validation */
	int reso_id;
	const char *reso_str;
	int prgname_id;
} TosAutoStart;


/* autostarted program name will be added before the first
 * '@' character in the INF files #Z line
 * (first value is 00: TOS, 01: GEM).
 *
 * Resolution is specified in the 2nd hex value in #E line
 * in normal TOS and EmuTOS <= 0.9.6, and in 4th hex value
 * in EmuTOS >= 0.9.7.  Hatari supports only latter EmuTOS
 * versions.
 *
 * TOS versions expect both of these to be within certain
 * number of bytes from the beginning of the file, and there
 * are also TOS version specific limits on the INF file sizes.
 *
 * More documentation on the DESKTOP.INF file content:
 * http://st-news.com/issues/st-news-volume-2-issue-6/education/the-desktopinf-file/
 */

/* EmuDesk INF file format and values differ from normal TOS */
static const char emudesk_inf[] =
"#R 01\r\n"
"#Z 01 @\r\n"
"#E 1A 61 FF 00 00\r\n"
"#W 00 00 02 06 26 0C 00 C:\\*.*@\r\n"
"#W 00 00 02 08 26 0C 00 @\r\n"
"#W 00 00 02 0A 26 0C 00 @\r\n"
"#W 00 00 02 0D 26 0C 00 @\r\n"
"#W 00 00 00 00 14 0B 00 @\r\n"
"#W 00 00 00 00 14 0B 00 @\r\n"
"#W 00 00 00 00 14 0B 00 @\r\n"
"#M 00 00 01 FF A DISK A@ @\r\n"
"#M 01 00 01 FF B DISK B@ @\r\n"
"#M 02 00 00 FF C DISK C@ @\r\n"
"#F FF 07 @ *.*@ 000 @\r\n"
"#N FF 07 @ *.*@ 000 @\r\n"
"#D FF 02 @ *.*@\r\n"
"#Y 06 FF *.GTP@ @ 000 @\r\n"
"#G 06 FF *.APP@ @ 000 @\r\n"
"#G 06 FF *.PRG@ @ 000 @\r\n"
"#P 06 FF *.TTP@ @ 000 @\r\n"
"#F 06 FF *.TOS@ @ 000 @\r\n"
"#T 00 03 03 FF   TRASH@ @\r\n";

/* TOS v1.04 works only with DESKTOP.INF from that version
 * (it crashes with newer INF after autobooted program exits),
 * later v1.x TOS versions work also with this.
 *
 * Trailing spaces are significant for TOS parsing.
 */
static const char desktop_inf[] =
"#a000000\r\n"
"#b000000\r\n"
"#c7770007000600070055200505552220770557075055507703111103\r\n"
"#d                                             \r\n"
"#Z 01 @ \r\n"
"#E 18 11 \r\n"
"#W 00 00 00 07 26 0C 09 C:\\*.*@\r\n"
"#W 00 00 02 0B 26 09 00 @\r\n"
"#W 00 00 0A 0F 1A 09 00 @\r\n"
"#W 00 00 0E 01 1A 09 00 @\r\n"
"#M 01 00 00 FF C HARD DISK@ @ \r\n"
"#M 00 00 00 FF A FLOPPY DISK@ @ \r\n"
"#M 00 01 00 FF B FLOPPY DISK@ @ \r\n"
"#T 00 03 02 FF   TRASH@ @ \r\n"
"#F FF 04   @ *.*@ \r\n"
"#D FF 01   @ *.*@ \r\n"
"#G 03 FF   *.APP@ @ \r\n"
"#G 03 FF   *.PRG@ @ \r\n"
"#P 03 FF   *.TTP@ @ \r\n"
"#F 03 04   *.TOS@ @ \r\n"
"\032";

/* TOS v2.x and newer have also different format, using
 * TOS v1.04 INF file would result in bogus resolution with TOS v4
 */
static const char newdesk_inf[] =
"#a000000\r\n"
"#b000000\r\n"
"#c7770007000600070055200505552220770557075055507703111103\r\n"
"#d                                             \r\n"
"#Z 01 @ \r\n"
"#K 4F 53 4C 00 46 42 43 57 45 58 00 00 00 00 00 00 00 00 00 00 00 00 00 52 00 00 4D 56 50 00 @\r\n"
"#E 18 01 00 06 \r\n"
"#Q 41 40 43 40 43 40 \r\n"
"#W 00 00 00 07 26 0C 00 C:\\*.*@\r\n"
"#W 00 00 02 0B 26 09 00 @\r\n"
"#W 00 00 0A 0F 1A 09 00 @\r\n"
"#W 00 00 0E 01 1A 09 00 @\r\n"
"#W 00 00 04 07 26 0C 00 @\r\n"
"#W 00 00 0C 0B 26 09 00 @\r\n"
"#W 00 00 08 0F 1A 09 00 @\r\n"
"#W 00 00 06 01 1A 09 00 @\r\n"
"#N FF 04 000 @ *.*@ @ \r\n"
"#D FF 01 000 @ *.*@ @ \r\n"
"#G 03 FF 000 *.APP@ @ @ \r\n"
"#G 03 FF 000 *.PRG@ @ @ \r\n"
"#Y 03 FF 000 *.GTP@ @ @ \r\n"
"#P 03 FF 000 *.TTP@ @ @ \r\n"
"#F 03 04 000 *.TOS@ @ @ \r\n"
"#M 00 01 00 FF C HARD DISK@ @ \r\n"
"#M 00 00 00 FF A FLOPPY DISK@ @ \r\n"
"#M 01 00 00 FF B FLOPPY DISK@ @ \r\n"
"#T 00 03 02 FF   TRASH@ @ \r\n";


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
 * 1-3: ST/STE resolutions
 * 4-6: TT/Falcon resolutions
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
 * Resolution settings are:
 *   0: no override
 * 1-3: ST/STE resolutions:
 *      - ST low, med, high
 * 4-6: TT/Falcon resolutions:
 *      - TT med, high, low
 *      - Falcon 80 cols, N/A, 40 cols
 *
 * If there's a problem, return problematic option ID
 * and set val & err strings, otherwise just return zero.
 */
static int INF_AutoStartValidateResolution(int *set_res, const char **val, const char **err)
{
	int res = TosAutoStart.reso;
	*set_res = 0;

	/* VDI resolution overrides TOS resolution setting */
	if (bUseVDIRes)
	{
		int newres = VDIRes + 1;
		if (res != newres)
		{
			if (res)
				Log_Printf(LOG_WARN, "Overriding TOS resolution %d with VDI resolution %d.\n",
					   res, newres);
			res = newres;
		}
	}
	else
	{
		int monitor = ConfigureParams.Screen.nMonitorType;

		/* validate given TOS resolution */
		if (!res)
			return 0;

		*val = TosAutoStart.reso_str;

		switch(ConfigureParams.System.nMachineType)
		{
		case MACHINE_STE:
		case MACHINE_MEGA_STE:
		case MACHINE_ST:
		case MACHINE_MEGA_ST:
			if (monitor == MONITOR_TYPE_MONO && res != 3)
			{
				res = 3;
				Log_Printf(LOG_WARN, "With mono monitor, TOS can use only resolution %d, correcting.\n", res);
			}
			else if (res > 2)
			{
				*err = "invalid ST/STE color resolution";
				return TosAutoStart.reso_id;
			}
			break;

		case MACHINE_TT:
			if (monitor == MONITOR_TYPE_MONO && res != 5)
			{
				res = 5;
				Log_Printf(LOG_WARN, "With mono monitor, TOS can use only resolution %d, correcting.\n", res);
			}
			else if (res == 5)
			{
				*err = "invalid TT color resolution";
				return TosAutoStart.reso_id;
			}
			break;

		case MACHINE_FALCON:
			if (monitor == MONITOR_TYPE_MONO && res != 3)
			{
				res = 3;
				Log_Printf(LOG_WARN, "With mono monitor, TOS can use only resolution %d, correcting.\n", res);
			}
			else if (res == 5)
			{
				*err = "TT-mono is invalid Falcon resolution";
				return TosAutoStart.reso_id;
			}
			else
			{
				Log_Printf(LOG_WARN, "TOS resolution setting doesn't work with Falcon (yet)\n");
			}
			/* TODO:
			 * Falcon resolution setting doesn't have effect,
			 * seems that #E Falcon settings in columns 6 & 7
			 * are also needed:
			 * - line doubling / interlace
			 * - ST compat, RGB/VGA, columns & #colors
			 */
			break;
		}
	}

	if (bIsEmuTOS)
	{
		/* map values 0-6: N/A, ST low, med, high, TT med, high, low */
		unsigned char map[] = { 0, 0, 1, 2, 4, 6, 7 };
		res = map[res];
		Log_Printf(LOG_INFO, "Remapped resolution for EmuTOS.\n");
	}
	else if (TosVersion >= 0x0160)
	{
		switch(ConfigureParams.System.nMachineType)
		{
		case MACHINE_STE:
		case MACHINE_MEGA_STE:
		case MACHINE_FALCON:
			/* enable blitter */
			res |= 0x10;
			break;
		default:
			break;
		}
	}

	Log_Printf(LOG_INFO, "Resulting INF file resolution: %02x -> %02x.\n", TosAutoStart.reso, res);
	*set_res = res;
	return 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Get suitable Atari desktop configuration file for current TOS version,
 * either by loading existing file, or creating default one if there isn't
 * a pre-existing one.
 *
 * Return INF file contents and set its name & size to args.
 */
static char *get_inf_file(const char **set_infname, int *set_size, int *res_col)
{
	char *hostname;
	const char *contents, *infname;
	Uint8 *host_content;
	long host_size;
	int size;

	/* default position of the 2 digit hex code for resolution on #E line */
	*res_col = 6;

	/* infname needs to be exactly the same string that given
	 * TOS version gives for GEMDOS to find.
	 */
	if (bIsEmuTOS)
	{
		if (ConfigureParams.HardDisk.bBootFromHardDisk)
			infname = "C:\\EMUDESK.INF";
		else
			infname = "A:\\EMUDESK.INF";
		size = sizeof(emudesk_inf);
		contents = emudesk_inf;
		*res_col = 12;
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
	*set_infname = infname;
	*set_size = size;

	/* Existing INF can be modified only through GEMDOS hard disk,
	 * i.e. boot needs to be from C:, which needs to be GEMDOS HD
	 */
	if (!(ConfigureParams.HardDisk.bBootFromHardDisk && GemDOS_IsDriveEmulated(2)))
	{
#if INF_DEBUG
	fprintf(stderr, "No GEMDOS HD boot drive, using builtin INF autostart file.\n");
#endif
		return strdup(contents);
	}

	hostname = malloc(FILENAME_MAX);
	assert(hostname);

	/* convert to host file name, and read that */
	GemDOS_CreateHardDriveFileName(2, infname, hostname, FILENAME_MAX);
#if INF_DEBUG
	GemDOS_Info(stderr, 0);
	fprintf(stderr, "\nChecking for existing INF file '%s' -> '%s'...\n", infname, hostname);
#endif
	host_content = File_Read(hostname, &host_size, NULL);

	if (host_content)
	{
		Log_Printf(LOG_INFO, "Going to modify '%s'.\n", hostname);
		free(hostname);
		*set_size = host_size;
		return (char *)host_content;
	}
	Log_Printf(LOG_INFO, "Using builtin '%s'.\n", infname);
	free(hostname);
	return strdup(contents);
}


/*-----------------------------------------------------------------------*/
/**
 * Skip rest of INF file line.
 * Return index after its end, or zero for error.
 */
static int skip_line(const char *contents, int offset, int size)
{
	int orig = offset;
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
	Log_Printf(LOG_WARN, "Malformed INF file '%s', no line end at offsets %d-%d!\n",
		   TosAutoStart.prgname, orig, offset);
	return 0;
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
		return "#Z 00 %s@ \r\n"; /* TOS program */
	else
		return "#Z 01 %s@ \r\n"; /* GEM program */
}

/**
 * Create modified, temporary INF file that contains the required
 * autostart and resolution information.
 *
 * Return FILE* pointer to it.
 */
static FILE* write_inf_file(const char *contents, int size, int res, int res_col)
{
	const char *format, *infname, *prgname;
	int offset, off_prg, off_rez, endcheck;
	FILE *fp;

#if defined(WIN32)	/* unfortunately tmpfile() needs administrative privileges on windows, so this needs special care */
	char *ptr = WinTmpFile();
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
	prgname = TosAutoStart.prgname;
	infname = TosAutoStart.infname;

	if (!fp)
	{
		Log_Printf(LOG_ERROR, "Failed to create virtual INF file '%s': %s!\n",
			   infname, strerror(errno));
		return NULL;
	}

	if (prgname)
		format = prg_format(prgname);

	/* need to fit at least 2 res digits + \r\n */
	endcheck = size-res_col-2-2;
	/* positions after prog info & resolution info */
	off_prg = off_rez = 0;
	/* find where to insert the program name and resolution */
	for (offset = 0; offset < endcheck; offset++)
	{
		if (contents[offset] != '#')
			continue;

		/* replace autostart line only when requested */
		if (prgname && contents[offset+1] == 'Z')
		{
			fwrite(contents+off_prg, offset-off_prg, 1, fp);
			/* write only first #Z line, skip rest */
			if (!off_prg)
				fprintf(fp, format, prgname);
			offset = skip_line(contents, offset, size-1);
			if (!offset)
				break;
			off_prg = offset;
		}
		/* resolution line always written */
		if (contents[offset+1] == 'E')
		{
			fwrite(contents+off_prg, offset-off_prg, 1, fp);
			/* INF file with autostart line missing?
			 *
			 * It's assumed that #Z is always before #E,
			 * if it exits. So write one when requested,
			 * if it hasn't been written yet.
			 */
			if (prgname && !off_prg)
			{
				off_prg = offset;
				fprintf(fp, format, prgname);
			}
			/* write #E line start */
			fwrite(contents+offset, res_col, 1, fp);
			/* write requested resolution, or default?
			 * 
			 * (TosAutoStart.reso tells if there's request,
			 * 'res' tells the actual value to use)
			 */
			if (TosAutoStart.reso)
				fprintf(fp, "%02x", res);
			else
				fwrite(contents+offset+res_col, 2, 1, fp);
			/* set point to rest of #E */
			offset += res_col + 2;
			off_rez = offset;
			break;
		}
	}
	if (!off_rez)
	{
		fclose(fp);
		Log_Printf(LOG_ERROR, "'%s' not a valid INF file, #E resolution line missing -> autostarting / resolution setting not possible!\n", infname);
		return NULL;
	}
	/* write rest of INF file & seek back to start */
	if (!(fwrite(contents+offset, size-offset-1, 1, fp) && fseek(fp, 0, SEEK_SET) == 0))
	{
		fclose(fp);
		Log_Printf(LOG_ERROR, "Virtual '%s' file writing failed!\n", infname);
		return NULL;
	}
	if (prgname)
		Log_Printf(LOG_WARN, "Virtual '%s' autostart file created for '%s'.\n", infname, prgname);
	else
		Log_Printf(LOG_WARN, "Virtual '%s' resolution override file created.\n", infname);
	return fp;
}


/*-----------------------------------------------------------------------*/
/**
 * Create a temporary TOS INF file for autostarting and resolution selection.
 *
 * File has TOS version specific differences, so it needs to be re-created
 * on each boot in case user changed TOS version.
 *
 * Called at end of TOS ROM loading.
 */
void INF_AutoStartCreate(void)
{
	char *contents;
	const char *err, *val;
	int size, res, res_col, opt_id;

	if ((opt_id = INF_AutoStartValidateResolution(&res, &val, &err)))
	{
		Opt_ShowError(opt_id, val, err);
		bQuitProgram = true;
		return;
	}

	/* in case TOS didn't for some reason close it on previous boot */
	INF_AutoStartClose(TosAutoStart.file);

	/* INF overriding needed? */
	if (!(TosAutoStart.prgname || TosAutoStart.reso))
		return;

	/* GEMDOS HD / INF overriding not supported? */
	if (TosVersion < 0x0104)
	{
		Log_Printf(LOG_WARN, "Only TOS versions >= 1.04 support autostarting & resolution overriding!\n");
		return;
	}

	contents = get_inf_file(&TosAutoStart.infname, &size, &res_col);
	if (contents)
	{
		TosAutoStart.file = write_inf_file(contents, size, res, res_col);
		free(contents);
	}
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
	return (((bool)TosAutoStart.prgname) || ((bool)TosAutoStart.reso));
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
		Log_Printf(LOG_WARN, "Virtual INF file '%s' matched.\n", filename);
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
		Log_Printf(LOG_WARN, "Virtual INF file removed.\n");
		return true;
	}
	return false;
}
