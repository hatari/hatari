/*
  Hatari - inffile.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  TOS *.INF file overloading for autostarting & TOS resolution overriding.

  Most of the functions here rely on accurate TOS+machine setup info,
  so they should be called only after emulation startup has fixed those.
*/
const char INFFILE_fileid[] = "Hatari inffile.c";

#include <assert.h>
#include <errno.h>
#include <ctype.h>

#include "main.h"
#include "configuration.h"
#include "conv_st.h"
#include "event.h"
#include "inffile.h"
#include "options.h"
#include "gemdos.h"
#include "file.h"
#include "log.h"
#include "str.h"
#include "tos.h"
#include "vdi.h"

/* debug output + leaves virtual INF file behind */
#define INF_DEBUG 0

/* TOS resolution numbers used in Atari TOS INF files.
 *
 * ST & TT values are used as-is for older Atari TOS versions,
 * but resolution values for TOS4 and EmuTOS need to go through
 * a mapping table, as those use multiple INF file fields.
 */
typedef enum {
	RES_UNSET,	/* 0 */
	RES_ST_LOW,	/* 1 */
	RES_ST_MED,	/* 2 */
	RES_ST_HIGH,	/* 3 */
	RES_TT_MED,	/* 4, 640x400 / 640x480 @16 */
	RES_TT_HIGH,	/* 5 */
	RES_TT_LOW,	/* 6, 320x400 / 320x480 @256 */
	/* no TOS IDs, Falcon only */
	RES_TC_MED,	/* 7, 320x400 / 320x480 @ TC */
	RES_TC_HIGH,	/* 8, 640x400 @ TC */
	RES_TC_LOW,	/* 9, 320x200 / 320x240 @ TC */
	RES_COUNT
} res_value_t;

static struct {
	FILE *file;          /* file pointer to contents of INF file */
	char *prgname;       /* TOS name of the program to auto start */
	const char *infname; /* name of the INF file TOS will try to match */
	res_value_t reso;    /* resolution setting value request for #E line */
	int closes;          /* how many times closed i.e. when to remove */
/* for validation */
	int reso_id;
	const char *reso_str;
	int prgname_id;
} TosOverride;

/* Autostarted program name is given on #Z line (added or
 * updated in the INF file), before its first '@' character.
 * First value on that line is 00 (TOS) or 01 (GEM).
 *
 * #E line content differs between TOS versions:
 * + Atari TOS:
 *   - Resolution is specified in the 2nd hex value
 *   - Blitter enabling is 0x10 bit for that
 * + EmuTOS v0.9.7 or newer:
 *   - Resolution is specified in the 3rd & 4th hex values.
 *     For other machines than Falcon, 3rd value is always "FF"
 *   - Blitter enabling is 0x80 bit in the 2nd hex value
 * + Older EmuTOS versions (not supported!):
 *   - Resolution is in the 2nd hex value
 *
 * TOS versions expect both of these to be within certain
 * number of bytes from the beginning of the file, and there
 * are also TOS version specific limits on the INF file sizes.
 *
 * More documentation on the DESKTOP.INF file content:
 * http://st-news.com/issues/st-news-volume-2-issue-6/education/the-desktopinf-file/
 *
 * EmuTOS INF file content is documented only in the sources:
 * https://github.com/emutos/emutos/blob/master/desk/deskapp.c
 *
 * Rev 2 did some changes to icon handling, which are not relevant
 * here, so this uses rev 1 of the INF file format:
 * https://github.com/emutos/emutos/commit/7a09651070ec7f7efc157d67166eef0f0c371695
 *
 * While 512k/1024k TOS images will update found drives (+trash/printer)
 * to desktop configuration, 192k/256k TOS images use what's in INF file,
 * so default INF file needs still to specify them.
 */

/* (space-separated) 2-digit hex value resolution info locations on #E line
 *
 * EmuTOS: 3rd & 4th 2-digit hex values
 */
#define ETOS_RES_OFFSET 9
#define ETOS_RES_LEN (2*2+1)

/* TOS v4: 2nd, 5th and 6th 2-digit hex values, matching to VsetMode:
 * https://freemint.github.io/tos.hyp/en/Screen_functions.html#VsetMode
 */
#define TOS4_RES_OFFSET 6
#define TOS4_RES_LEN (5*2+4)

/* Older Atari TOS version: 2nd 2-digit hex value */
#define TOS_RES_OFFSET  6
#define TOS_RES_LEN     2

/* EmuDesk INF file format and values differ from normal TOS */
static const char emudesk_inf[] =
"#R 01\r\n"
"#E 1A E0 FF 00 60\r\n"
"#W 00 00 02 08 26 0C 00 @\r\n"
"#W 00 00 02 0A 26 0C 00 @\r\n"
"#W 00 00 02 0D 26 0C 00 @\r\n"
"#W 00 00 00 00 28 17 00 @\r\n"
"#W 00 00 00 00 28 17 00 @\r\n"
"#W 00 00 00 00 28 17 00 @\r\n"
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
"#T 00 03 03 FF   TRASH@ @\r\n"
"#O 03 03 04 FF   PRINTER@ @\r\n";

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
"#E 18 11 \r\n"
"#W 00 00 02 0B 26 09 00 @\r\n"
"#W 00 00 0A 0F 1A 09 00 @\r\n"
"#W 00 00 0E 01 1A 09 00 @\r\n"
"#M 00 00 00 FF A FLOPPY DISK@ @ \r\n"
"#M 01 00 00 FF B FLOPPY DISK@ @ \r\n"
"#M 02 00 00 FF C HARD DISK@ @ \r\n"
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
"#K 4F 53 4C 00 46 42 43 57 45 58 00 00 00 00 00 00 00 00 00 00 00 00 00 52 00 00 4D 56 50 00 @\r\n"
"#E 18 01 00 06 \r\n"
"#Q 41 40 43 40 43 40 \r\n"
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
"#M 00 00 00 FF A FLOPPY DISK@ @ \r\n"
"#M 01 00 00 FF B FLOPPY DISK@ @ \r\n"
"#M 02 00 00 FF C HARD DISK@ @ \r\n"
"#T 00 03 02 FF   TRASH@ @ \r\n";

/* TOS v4.x has longer #E line, so need separate content for it.
 */
static const char tos4desk_inf[] =
"#a000000\r\n"
"#b000000\r\n"
"#c7770007000600070055200505552220770557075055507703111103\r\n"
"#d                                             \r\n"
"#K 4F 53 4C 00 46 42 43 57 45 58 00 00 00 00 00 00 00 00 00 00 00 00 00 52 00 00 4D 56 00 00 00 @\r\n"
"#E 18 01 00 06 00 82 00 00 00 00 \r\n"
"#Q 41 70 73 70 7D 70 \r\n"
"#W 00 00 00 07 26 0C 00 @\r\n"
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
"#M 00 00 00 FF A FLOPPY DISK@ @ \r\n"
"#M 01 00 00 FF B FLOPPY DISK@ @ \r\n"
"#M 02 00 00 FF C HARD DISK@ @ \r\n"
"#C 00 01 00 FF c CARTRIDGE@ @ \r\n"
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
bool INF_SetAutoStart(const char *name, int opt_id)
{
	char *prgname;
	int len = strlen(name);
	char drive = toupper(name[0]);

	if (drive >= 'A' && drive <= 'Z' && name[1] == ':')
	{
		/* full path */
		const char *ptr;
		int offset;
		prgname = Str_Alloc(len + 1);  /* +1 for additional backslash */
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
				Log_Printf(LOG_WARN, "rejecting auto-start path that doesn't have '\\' after drive ID:\n\t%s\n", name);
				free(prgname);
				return false;
			}
			/* A:NAME.PRG -> A:\NAME.PRG */
			prgname[offset] = '\\';
			/* copy/upcase file part */
			Str_Filename_Host2Atari(name+offset, prgname+offset+1);
		} else {
			/* copy/upcase file part */
			Str_Filename_Host2Atari(name+offset, prgname+offset);
		}
	}
	else if (strchr(name, '\\'))
	{
		/* partial path not accepted */
		Log_Printf(LOG_WARN, "rejecting auto-start path starting with '\\', but without drive ID:\n\t%s\n", name);
		return false;
	}
	else
	{
		/* just program -> add path */
		prgname = Str_Alloc(3 + len);
		strcpy(prgname, "C:\\");
		Str_Filename_Host2Atari(name, prgname+3);
	}
	if (TosOverride.prgname)
		free(TosOverride.prgname);
	TosOverride.prgname = prgname;
	TosOverride.prgname_id = opt_id;
	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * Parse given string and set specified TOS resolution override.
 *
 * Return true for success, false otherwise.
 */
bool INF_SetResolution(const char *str, int opt_id)
{
	struct {
		const char *name;
		res_value_t reso;
	} resolutions[] = {
		{ "low",   RES_ST_LOW },
		{ "stlow", RES_ST_LOW },
		{ "med",   RES_ST_MED },
		{ "stmed", RES_ST_MED },
		{ "high",  RES_ST_HIGH },
		{ "sthigh",RES_ST_HIGH },
		{ "ttlow", RES_TT_LOW },
		{ "ttmed", RES_TT_MED },
		{ "tthigh",RES_TT_HIGH },
		{ "tclow", RES_TC_LOW },
		{ "tcmed", RES_TC_MED },
		{ "tchigh",RES_TC_HIGH },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(resolutions); i++)
	{
		if (strcmp(str, resolutions[i].name) == 0)
		{
			TosOverride.reso = resolutions[i].reso;
			TosOverride.reso_id = opt_id;
			TosOverride.reso_str = str;
			return true;
		}
	}
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Validate autostart options against current Hatari settings:
 * - program drive
 *
 * If there's a problem, return problematic option ID
 * and set val & err strings, otherwise just return zero.
 */
int INF_ValidateAutoStart(const char **val, const char **err)
{
	const char *path = TosOverride.prgname;
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
	 * - finding out what partitions each of the 2 IDE, 8 ACSI, and
	 *   8 SCSI images do have, *and*
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
	else if (ConfigureParams.HardDisk.bUseHardDiskDirectories && ConfigureParams.HardDisk.szHardDiskDirectories[0][0])
	{
		return 0;
	}
	/* IDE */
	else if (ConfigureParams.Ide[0].bUseDevice && ConfigureParams.Ide[0].sDeviceFile[0])
	{
		return 0;
	}
	else if (ConfigureParams.Ide[1].bUseDevice && ConfigureParams.Ide[1].sDeviceFile[0])
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
	*val = TosOverride.prgname;
	*err = "Required autostart drive isn't enabled";
	return TosOverride.prgname_id;
}


/**
 * Map VDI / HW resolution to INF file resolution value
 */
static res_value_t vdi2inf(res_value_t mode)
{
	res_value_t newres, res = TosOverride.reso;

	switch (mode)
	{
	case ST_LOW_RES:
		newres = RES_ST_LOW;
		break;
	case ST_MEDIUM_RES:
		newres = RES_ST_MED;
		break;
	case ST_HIGH_RES:
		newres = RES_ST_HIGH;
		break;
	case TT_LOW_RES:
		newres = RES_TT_LOW;
		break;
	case TT_MEDIUM_RES:
		newres = RES_TT_MED;
		break;
	case TT_HIGH_RES:
		newres = RES_TT_HIGH;
		break;
	default:
		newres = res;
	}
	if (res != newres)
	{
		if (res)
			Log_Printf(LOG_WARN, "Overriding TOS INF resolution %d with VDI resolution %d\n",
				   res, newres);
		res = newres;
	}
	return res;
}

/**
 * Map / set VDI to INF file resolution
 */
extern void INF_SetVdiMode(int vdi_res)
{
	TosOverride.reso = vdi2inf(vdi_res);
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
 * 7-8: Falcon HiColor (Truecolor)
 *      - low and "medium" TC resolution
 *
 * For older TOS versions, update resolution ID is as-is, but for
 * EmuTOS & Falcon they need to be mapped to multiple INF file fields.
 * 
 * If there's a problem, return problematic option ID
 * and set val & err strings, otherwise just return zero.
 */
static int INF_ValidateResolution(int *set_res, const char **val, const char **err)
{
#define MONO_WARN_STR "Correcting virtual INF file resolution to mono on mono monitor\n"
	int res = TosOverride.reso;
	*val = TosOverride.reso_str;
	*set_res = 0;

	if (bUseVDIRes)
	{
		/* VDI resolution overrides any TOS resolution setting */
		res = vdi2inf(VDIRes);

		switch(ConfigureParams.System.nMachineType)
		{
		case MACHINE_TT:
		case MACHINE_FALCON:
			break;
		default:
			if (res >= TT_LOW_RES)
			{
				*err = "Invalid VDI mode, only TT + Falcon support more than 4-plane modes";
				return TosOverride.reso_id;
			}
		}
	}
	else
	{
		int monitor = ConfigureParams.Screen.nMonitorType;

		/* validate given TOS resolution */
		if (!res)
			return 0;

		switch(ConfigureParams.System.nMachineType)
		{
		case MACHINE_STE:
		case MACHINE_MEGA_STE:
		case MACHINE_ST:
		case MACHINE_MEGA_ST:
			if (monitor == MONITOR_TYPE_MONO)
			{
				if (res != RES_ST_HIGH)
				{
					res = RES_ST_HIGH;
					Log_Printf(LOG_WARN, MONO_WARN_STR);
				}
			}
			else if (res >= RES_ST_HIGH)
			{
				*err = "invalid TOS resolution for ST/STE color monitor";
				return TosOverride.reso_id;
			}
			break;

		case MACHINE_TT:
			if (monitor == MONITOR_TYPE_MONO)
			{
				if (res != RES_TT_HIGH)
				{
					res = RES_TT_HIGH;
					Log_Printf(LOG_WARN, MONO_WARN_STR);
				}
			}
			else if (res == RES_TT_HIGH)
			{
				*err = "invalid TOS resolution for TT color monitor";
				return TosOverride.reso_id;
			}
			else if (res == RES_TC_LOW || res == RES_TC_MED || res == RES_TC_HIGH)
			{
				*err = "TT does not support TrueColor mode";
				return TosOverride.reso_id;

			}
			break;

		case MACHINE_FALCON:
			if (monitor == MONITOR_TYPE_MONO && res != RES_ST_HIGH)
			{
				res = RES_ST_HIGH;
				Log_Printf(LOG_WARN, MONO_WARN_STR);
			}
			else if (res == RES_TT_HIGH)
			{
				*err = "TT-mono is invalid TOS resolution for Falcon";
				return TosOverride.reso_id;
			}
			if (monitor == MONITOR_TYPE_VGA && res == RES_TC_HIGH)
			{
				*err = "TOS does not support TC high mode on VGA monitor";
				return TosOverride.reso_id;
			}
			break;
		}
	}

	Log_Printf(LOG_DEBUG, "Resulting INF file TOS resolution: 0x%02x -> 0x%02x.\n", TosOverride.reso, res);
	*set_res = res;
	return 0;
}

/*-----------------------------------------------------------------------*/
/**
 * Return resolution value blitter flag for appropriate platforms.
 * Ones that actually have blitter HW, OS support for it, and
 * do not set it themselves (in some other INF file value).
 */
static int get_blitter_bit(void)
{
	if (TosVersion >= 0x0160 && !bIsEmuTOS)
	{
		switch(ConfigureParams.System.nMachineType)
		{
		case MACHINE_STE:
		case MACHINE_MEGA_STE:
		case MACHINE_FALCON:
			/* enable blitter */
			return 0x10;
		default:
			break;
		}
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
/**
 * Get builtin INF file contents, with line added for opening
 * a window for a boot drive (if any).  Return allocated
 * (virtual) INF file content string.
 */
static char *get_builtin_inf(const char *contents)
{
	/* line to open window (for boot drive) */
	static const char *drivewin;
	int winlen, inflen, winoffset1, winoffset2, driveoffset;
	const char *winline;
	char *inf;

	assert(contents);

	if (TosVersion >= 0x200 && TosVersion != 0x300)
		/* NEWDESK.INF / EMUDESK.INF */
		drivewin = "#W 00 00 00 07 28 10 00 X:\\*.*@\r\n";
	else
		/* DESKTOP.INF */
		drivewin = "#W 00 00 00 07 28 10 09 X:\\*.*@\r\n";

	inflen = strlen(contents);
	winlen = strlen(drivewin);
	inf = Str_Alloc(inflen + winlen);

	/* drive letter offset on drive window line */
	driveoffset = strchr(drivewin, 'X') - drivewin;

	/* first copy everything until first window line */
	winline = strstr(contents, "#W");
	assert(winline);
	winoffset2 = winoffset1 = winline - contents;
	memcpy(inf, contents, winoffset1);

	/* then comes boot drive window line, if any */
	if (ConfigureParams.HardDisk.bBootFromHardDisk)
	{
		/* C:, ignore IDE/ACSI for now */
		if (GemDOS_IsDriveEmulated(2))
		{
			strcpy(inf + winoffset1, drivewin);
			inf[winoffset1 + driveoffset] = 'C';
			winoffset2 += winlen;
		}
	}
	else if (ConfigureParams.DiskImage.EnableDriveA && ConfigureParams.DiskImage.szDiskFileName[0][0])
	{
		/* A: */
		strcpy(inf + winoffset1, drivewin);
		inf[winoffset1 + driveoffset] = 'A';
		winoffset2 += winlen;
	}
	/* finally copy rest */
	strcpy(inf + winoffset2, contents + winoffset1);

	return inf;
}

/**
 * Get suitable Atari desktop configuration file for current TOS version,
 * either by loading existing file, or creating default one if there isn't
 * a pre-existing one.
 *
 * Return INF file contents and set its name & size to args.
 */
static char *get_inf_file(const char **set_infname, int *set_size)
{
	char *hostname;
	const char *contents, *infname;
	uint8_t *host_content;
	long host_size;
	int size;

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
	}

	/* need to match file TOS searches first */
	else if (TosVersion >= 0x400)
	{
		infname = "NEWDESK.INF";
		size = sizeof(tos4desk_inf);
		contents = tos4desk_inf;
	}
	else if (TosVersion >= 0x200 && TosVersion != 0x300)
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
		Log_Printf(LOG_DEBUG, "No GEMDOS HD boot drive, using builtin INF autostart file.\n");
		return get_builtin_inf(contents);
	}

	hostname = Str_Alloc(FILENAME_MAX);

	/* convert to host file name, and read that */
	GemDOS_CreateHardDriveFileName(2, infname, hostname, FILENAME_MAX);
#if INF_DEBUG
	GemDOS_Info(stderr, 0);
	fprintf(stderr, "\nChecking for existing INF file '%s' -> '%s'...\n", infname, hostname);
#endif
	host_content = File_ReadAsIs(hostname, &host_size);

	if (host_content)
	{
		Log_Printf(LOG_DEBUG, "Going to modify '%s'.\n", hostname);
		free(hostname);
		*set_size = host_size;
		return (char *)host_content;
	}
	Log_Printf(LOG_DEBUG, "Using builtin '%s'.\n", infname);
	free(hostname);
	return get_builtin_inf(contents);
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
		   TosOverride.prgname, orig, offset);
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
 * Write specified resolution to INF FILE*,
 * mapped to suitable TOS4 INF file values.
 * Return number of written characters.
 */
static int write_reso_tos4(FILE *fp, int res)
{
	/* map res_value_t to TOS4 values */
	static const uint8_t falcon[RES_COUNT][2][3] = {
		/* RGB */          /* VGA */
		{{0x0, 0x0, 0x00}, {0x0, 0x0, 0x00}}, /* N/A */
		{{0x1, 0x0, 0x82}, {0x1, 0x1, 0x92}}, /* ST-low */
		{{0x2, 0x0, 0x89}, {0x2, 0x1, 0x99}}, /* ST-med */
		{{0x3, 0x1, 0x88}, {0x3, 0x0, 0x98}}, /* ST-high */
		{{0x3, 0x1, 0x0A}, {0x4, 0x0, 0x1A}}, /* TT-med:  640x400 / 640x480 @16 */
		{{0x0, 0x0, 0x00}, {0x0, 0x0, 0x00}}, /* TT-high: N/A on Falcon */
		{{0x6, 0x1, 0x03}, {0x6, 0x0, 0x13}}, /* TT-low:  320x400 / 320x480 @256 */
		{{0x6, 0x1, 0x04}, {0x6, 0x0, 0x14}}, /* TC-med:  320x400 / 320x480 @TC */
		{{0x3, 0x1, 0x0C}, {0x0, 0x0, 0x00}}, /* TC-high: 640x400 / N/A @TC */
		{{0x1, 0x0, 0x04}, {0x6, 0x1, 0x14}}, /* TC-low:  320x200 / 320x240 @TC */
	};

	int idx = 0;
	if (ConfigureParams.Screen.nMonitorType == MONITOR_TYPE_VGA)
	       idx = 1;

	/* 2nd, 5th and 6th hex values on #E line */
	return fprintf(fp, "%02X 00 06 %02X %02X",
		       falcon[res][idx][0],
		       falcon[res][idx][1],
		       falcon[res][idx][2]);
}

/**
 * Write specified resolution to INF FILE*,
 * mapped to suitable EmuTOS INF file values.
 * Return number of written characters.
 */
static int write_reso_etos(FILE *fp, int res)
{
	/* map TOS resolution values to EmuTOS values:
	 * N/A, ST - low, med, high, TT - med, high, low, TC - N/A, N/A
	 */
	static const uint8_t remap[RES_COUNT] = { 0, 0, 1, 2, 4, 6, 7 };

	/* map res_value_t to EmuTOS on Falcon */
	static const uint8_t falcon[RES_COUNT][2][2] = {
		/* RGB */     /* VGA */
		{{0x0, 0x00}, {0x0, 0x00}}, /* N/A */
		{{0x0, 0x82}, {0x1, 0x92}}, /* ST-low */
		{{0x0, 0x89}, {0x1, 0x99}}, /* ST-med */
		{{0x1, 0x88}, {0x0, 0x98}}, /* ST-high */
		{{0x1, 0x0A}, {0x0, 0x1A}}, /* TT-med:  640x400 / 640x480 @16 */
		{{0x0, 0x00}, {0x0, 0x00}}, /* TT-high: N/A on Falcon */
		{{0x1, 0x03}, {0x0, 0x13}}, /* TT-low:  320x400 / 320x480 @256 */
		{{0x1, 0x04}, {0x0, 0x14}}, /* TC-med:  320x400 / 320x480 @TC */
		{{0x1, 0x0C}, {0x0, 0x00}}, /* TC-high: 640x400 / N/A @TC */
		{{0x0, 0x04}, {0x1, 0x14}}, /* TC-low:  320x200 / 320x240 @TC */
	};
	int idx = 0;

	if (!Config_IsMachineFalcon())
		return fprintf(fp, "FF %02X", remap[res]);

	if (ConfigureParams.Screen.nMonitorType == MONITOR_TYPE_VGA)
		idx = 1;

	/* 3rd and 4th hex values on #E line */
	return fprintf(fp, "%02X %02X",
		       falcon[res][idx][0],
		       falcon[res][idx][1]);
}

/**
 * Create modified, temporary INF file that contains the required
 * autostart and resolution information.
 *
 * Return FILE* pointer to virtual INF file.
 */
static FILE* write_inf_file(const char *contents, int size, int res)
{
	const char *infname, *prgname, *format = NULL;
	int out, offset, off_prg, off_rez, res_col, res_len, endcheck;
	FILE *fp;

#if INF_DEBUG
	{
		/* insecure file path + leaving it behind for debugging */
		const char *debugfile = "/tmp/hatari-desktop-inf.txt";
		fprintf(stderr, "Virtual INF file: '%s' = '%s' (TOS: 0x%04x)\n",
			debugfile, TosOverride.infname, TosVersion);
		fp = fopen(debugfile, "w+b");
	}
#else
	fp = File_OpenTempFile(NULL);
#endif

	prgname = TosOverride.prgname;
	infname = TosOverride.infname;

	if (!fp)
	{
		Log_Printf(LOG_ERROR, "Failed to create virtual INF file '%s': %s!\n",
			   infname, strerror(errno));
		return NULL;
	}

	if (prgname)
		format = prg_format(prgname);

	assert((res > 0 && res < RES_COUNT) || !TosOverride.reso);

	if (bIsEmuTOS)
	{
		res_col = ETOS_RES_OFFSET;
		res_len = ETOS_RES_LEN;
	}
	else if (TosVersion >= 0x400)
	{
		res_col = TOS4_RES_OFFSET;
		res_len = TOS4_RES_LEN;
	}
	else /* older Atari TOS version */
	{
		res_col = TOS_RES_OFFSET;
		res_len = TOS_RES_LEN;
	}
	/* need to fit at least resource info + \r\n */
	endcheck = size - res_col - res_len - 2;

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
			 * if it exists. So write one when requested,
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
			 * (TosOverride.reso tells if there's request,
			 * 'res' is the actual resolution ID)
			 */
			if (!TosOverride.reso)
				out = fwrite(contents+offset+res_col, 1, res_len, fp);
			else if (bIsEmuTOS)
				out = write_reso_etos(fp, res);
			else if (TosVersion >= 0x400)
				out = write_reso_tos4(fp, res);
			else
				out = fprintf(fp, "%02X", res|get_blitter_bit());
			if (out != res_len)
				Log_Printf(LOG_ERROR, "invalid resolution write size for virtual INF file (%d!=%d)!\n", out, res_len);
			/* set point to rest of #E */
			offset += res_col + res_len;
			off_rez = offset;
			break;
		}
	}
	if (!off_rez)
	{
		fclose(fp);
		Log_Printf(LOG_ERROR, "'%s' not a valid INF file, #E resolution line missing -> autostarting / resolution overriding not possible!\n", infname);
		return NULL;
	}
	/* write rest of INF file & seek back to start */
	if (!(fwrite(contents+offset, size-offset-1, 1, fp) && fseek(fp, 0, SEEK_SET) == 0))
	{
		fclose(fp);
		Log_Printf(LOG_ERROR, "Virtual '%s' INF file writing failed!\n", infname);
		return NULL;
	}
	if (prgname)
		Log_Printf(LOG_DEBUG, "Virtual '%s' autostart INF file created for '%s'\n", infname, prgname);
	else
		Log_Printf(LOG_DEBUG, "Virtual '%s' TOS resolution override INF file created\n", infname);
	return fp;
}


/*-----------------------------------------------------------------------*/
/**
 * Create a temporary TOS INF file for autostarting and resolution overriding.
 *
 * File has TOS version specific differences, so it needs to be re-created
 * on each boot in case user changed TOS version.
 *
 * Called at end of TOS ROM loading (at GEMDOS reset).
 */
void INF_CreateOverride(void)
{
	char *contents;
	const char *err, *val;
	int size, res, opt_id;

	if ((opt_id = INF_ValidateResolution(&res, &val, &err)))
	{
		Opt_ShowError(opt_id, val, err);
		bQuitProgram = true;
		return;
	}

	/* in case TOS didn't for some reason close it on previous boot */
	INF_CloseOverride(TosOverride.file);

	/* INF overriding needed? */
	if (!(TosOverride.prgname || TosOverride.reso))
		return;

	/* GEMDOS HD / INF overriding not supported? */
	if (bUseTos && TosVersion < 0x0104)
	{
		Log_Printf(LOG_WARN, "Only TOS versions >= 1.04 support autostarting & resolution overriding!\n");
		return;
	}

	contents = get_inf_file(&TosOverride.infname, &size);
	if (contents)
	{
		TosOverride.file = write_inf_file(contents, size, res);
		free(contents);
	}

	TosOverride.closes = 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Whether INF file overriding needs GEMDOS
 * interception or Fopen() check enabling
 */
bool INF_Overriding(autostart_t t)
{
	if (t == AUTOSTART_FOPEN)
		return (bool)TosOverride.file;
	return (((bool)TosOverride.prgname) || ((bool)TosOverride.reso));
}

/*-----------------------------------------------------------------------*/
/**
 * INF file (resolution/autostart) overriding; if given name matches
 * virtual INF file name, return its handle, NULL otherwise.
 *
 * Runs also user-configured actions for this event.
 */
FILE *INF_OpenOverride(const char *filename)
{
	if (TosOverride.file && strcmp(filename, TosOverride.infname) == 0)
	{
		Log_Printf(LOG_DEBUG, "Virtual INF file '%s' matched.\n", filename);
		Event_DoInfLoadActions();
		return TosOverride.file;
	}
	return NULL;
}

/*-----------------------------------------------------------------------*/
/**
 * If given handle matches virtual INF file, close it and return true,
 * false otherwise.
 */
bool INF_CloseOverride(FILE *fp)
{
	if (!(fp && fp == TosOverride.file))
		return false;

	/* Remove virtual INF file after TOS has
	 * read it enough times to do autostarting etc.
	 * Otherwise user may try change desktop settings
	 * and save them, but they would be lost.
	 *
	 * EmuTOS reads INF file twice on startup, real TOS once.
	 */
	TosOverride.closes++;
	if (bIsEmuTOS && TosOverride.closes < 2)
	{
		/* on first time just rewind file to beginning */
		fseek(TosOverride.file, 0L, SEEK_SET);
		return true;
	}

	fclose(TosOverride.file);
	Log_Printf(LOG_DEBUG, "Virtual INF file removed.\n");
	TosOverride.file = NULL;
	TosOverride.closes = 0;
	return true;
}
