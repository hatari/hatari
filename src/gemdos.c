/*
  Hatari - gemdos.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  GEMDOS intercept routines.
  These are used mainly for hard drive redirection of high level file routines.

  Host file names are handled case insensitively, so files on GEMDOS
  drive emulation directories may be either in lower or upper case.

  Too long file and directory names and names with invalid characters
  are converted to TOS compatible 8+3 names, but matching them back to
  host names is slower and may match several such filenames (of which
  first one will be returned), so using them should be avoided.

  Known bugs / things to fix:
  * Host file names are in many places limited to 255 chars (same as
    on TOS). They should be dynamically allocated instead
  * GEMDOS Ddelete() implementation uses rmdir() which cannot remove
    a dir with files in it (a TOS/unix difference)
*/
const char Gemdos_fileid[] = "Hatari gemdos.c";

#include <config.h>

#include <sys/stat.h>
#if HAVE_STATVFS
#include <sys/statvfs.h>
#endif
#include <sys/types.h>
#if HAVE_UTIME_H
#include <utime.h>
#elif HAVE_SYS_UTIME_H
#include <sys/utime.h>
#endif
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>

#include "main.h"
#include "cart.h"
#include "configuration.h"
#include "file.h"
#include "floppy.h"
#include "ide.h"
#include "inffile.h"
#include "hdc.h"
#include "ncr5380.h"
#include "gemdos.h"
#include "gemdos_defines.h"
#include "log.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "printer.h"
#include "statusbar.h"
#include "scandir.h"
#include "stMemory.h"
#include "str.h"
#include "tos.h"
#include "hatari-glue.h"
#include "maccess.h"
#include "symbols.h"

/* Maximum supported length of a GEMDOS path: */
#define MAX_GEMDOS_PATH 256

#define BASEPAGE_SIZE (0x80+0x80)  /* info + command line */
#define BASEPAGE_OFFSET_DTA 0x20
#define BASEPAGE_OFFSET_PARENT 0x24

/* Have we re-directed GemDOS vector to our own routines yet? */
bool bInitGemDOS;

/* structure with all the drive-specific data for our emulated drives,
 * used by GEMDOS_EMU_ON macro
 */
EMULATEDDRIVE **emudrives = NULL;

#define  ISHARDDRIVE(Drive)  (Drive!=-1)

/*
  Disk Transfer Address (DTA)
*/
#define TOS_NAMELEN  14

typedef struct {
  /* GEMDOS internals */
  uint8_t index[2];
  uint8_t magic[4];
  char dta_pat[TOS_NAMELEN]; /* unused */
  char dta_sattrib;          /* unused */
  /* TOS API */
  char dta_attrib;
  uint8_t dta_time[2];
  uint8_t dta_date[2];
  uint8_t dta_size[4];
  char dta_name[TOS_NAMELEN];
} DTA;

/* PopulateDTA() return values */
typedef enum {
	DTA_ERR = -1,
	DTA_OK = 0,
	DTA_SKIP = 1
} dta_ret_t;

#define DTA_MAGIC_NUMBER  0x12983476
#define DTA_CACHE_INC     256      /* DTA cache initial and increment size (grows on demand) */
#define DTA_CACHE_MAX     4096     /* max DTA cache size (multiple of DTA_CACHE_INC) */

#define  BASE_FILEHANDLE     64    /* Our emulation handles - MUST not be valid TOS ones, but MUST be <256 */
#define  MAX_FILE_HANDLES    64    /* We can allow 64 files open at once */

/*
   DateTime structure used by TOS call $57 f_dtatime
   Changed to fix potential problem with alignment.
*/
typedef struct {
  uint16_t timeword;
  uint16_t dateword;
} DATETIME;

#define UNFORCED_HANDLE -1
static struct {
	int Handle;
	uint32_t Basepage;
} ForcedHandles[5]; /* (standard) handles aliased to emulated handles */

typedef struct
{
	bool bUsed;
	bool bReadOnly;
	char szMode[4];     /* enough for all used fopen() modes: rb/rb+/wb+ */
	uint32_t Basepage;
	FILE *FileHandle;
	/* TODO: host path might not fit into this */
	char szActualName[MAX_GEMDOS_PATH];        /* used by F_DATIME (0x57) */
} FILE_HANDLE;

typedef struct
{
	bool bUsed;
	uint32_t addr;                        /* ST-RAM DTA address for matching reused entries */
	int  nentries;                      /* number of entries in fs directory */
	int  centry;                        /* current entry # */
	struct dirent **found;              /* legal files */
	char path[MAX_GEMDOS_PATH];                /* sfirst path */
} INTERNAL_DTA;

static FILE_HANDLE  FileHandles[MAX_FILE_HANDLES];
static INTERNAL_DTA *InternalDTAs;
static int DTACount;        /* Current DTA cache size */
static uint16_t DTAIndex;     /* Circular index into above */
static uint16_t CurrentDrive; /* Current drive (0=A,1=B,2=C etc...) */
static uint32_t act_pd;       /* Used to get a pointer to the current basepage */
static uint16_t nAttrSFirst;  /* File attribute for SFirst/Snext */
static uint32_t CallingPC;    /* Program counter from caller */

static uint32_t nSavedPexecParams;

#if defined(WIN32) && !defined(mkdir)
#define mkdir(name,mode) mkdir(name)
#endif  /* WIN32 */

#ifndef S_IRGRP
#define S_IRGRP 0
#define S_IROTH 0
#endif

/* set to 1 if you want to see debug output from pattern matching */
#define DEBUG_PATTERN_MATCH 0


/*-------------------------------------------------------*/
/**
 * Routine to convert time and date to GEMDOS format.
 * Originally from the STonX emulator. (cheers!)
 */
static void GemDOS_DateTime2Tos(time_t t, DATETIME *DateTime, const char *fname)
{
	struct tm *x;

	/* localtime takes DST into account */
	x = localtime(&t);

	if (x == NULL)
	{
		Log_Printf(LOG_WARN, "'%s' timestamp is invalid for (Windows?) localtime(), defaulting to TOS epoch!",  fname);
		DateTime->dateword = 1|(1<<5);	/* 1980-01-01 */
		DateTime->timeword = 0;
		return;
	}
	/* Bits: 0-4 = secs/2, 5-10 = mins, 11-15 = hours (24-hour format) */
	DateTime->timeword = (x->tm_sec>>1)|(x->tm_min<<5)|(x->tm_hour<<11);
	
	/* Bits: 0-4 = day (1-31), 5-8 = month (1-12), 9-15 = years (since 1980) */
	DateTime->dateword = x->tm_mday | ((x->tm_mon+1)<<5)
		| (((x->tm_year-80 > 0) ? x->tm_year-80 : 0) << 9);
}

/*-----------------------------------------------------------------------*/
/**
 * Populate a DATETIME structure with file info.  Handle needs to be
 * validated before calling.  Return true on success.
 */
static bool GemDOS_GetFileInformation(int Handle, DATETIME *DateTime)
{
	const char *fname = FileHandles[Handle].szActualName;
	struct stat fstat;

	if (stat(fname, &fstat) == 0)
	{
		GemDOS_DateTime2Tos(fstat.st_mtime, DateTime, fname);
		return true;
	}
	return false;
}

/*-----------------------------------------------------------------------*/
/**
 * Set given file date/time from given DATETIME.  Handle needs to be
 * validated before calling.  Return true on success.
 */
static bool GemDOS_SetFileInformation(int Handle, DATETIME *DateTime)
{
	const char *filename;
	struct utimbuf timebuf;
	struct stat filestat;
	struct tm timespec;

	/* make sure Hatari itself doesn't need to write/modify
	 * the file after it's modification time is changed.
	 */
	fflush(FileHandles[Handle].FileHandle);

	/* use host modification times instead of Atari ones? */
	if (ConfigureParams.HardDisk.bGemdosHostTime)
		return true;

	filename = FileHandles[Handle].szActualName;
	
	/* Bits: 0-4 = secs/2, 5-10 = mins, 11-15 = hours (24-hour format) */
	timespec.tm_sec  = (DateTime->timeword & 0x1F) << 1;
	timespec.tm_min  = (DateTime->timeword & 0x7E0) >> 5;
	timespec.tm_hour = (DateTime->timeword & 0xF800) >> 11;
	/* Bits: 0-4 = day (1-31), 5-8 = month (1-12), 9-15 = years (since 1980) */
	timespec.tm_mday = (DateTime->dateword & 0x1F);
	timespec.tm_mon  = ((DateTime->dateword & 0x1E0) >> 5) - 1;
	timespec.tm_year = ((DateTime->dateword & 0xFE00) >> 9) + 80;
	/* check whether DST should be taken into account */
	timespec.tm_isdst = -1;

	/* set new modification time */
	timebuf.modtime = mktime(&timespec);

	/* but keep previous access time */
	if (stat(filename, &filestat) != 0)
		return false;
	timebuf.actime = filestat.st_atime;

	if (utime(filename, &timebuf) != 0)
		return false;
	// fprintf(stderr, "set date '%s' for %s\n", asctime(&timespec), name);
	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * Convert from FindFirstFile/FindNextFile attribute to GemDOS format
 */
static uint8_t GemDOS_ConvertAttribute(mode_t mode, const char *path)
{
	uint8_t Attrib = 0;

	/* Directory attribute */
	if (S_ISDIR(mode))
		Attrib |= GEMDOS_FILE_ATTRIB_SUBDIRECTORY;

	/* Read-only attribute */
	if (!(mode & S_IWUSR) || access(path, W_OK) != 0)
		Attrib |= GEMDOS_FILE_ATTRIB_READONLY;

	/* TODO, Other attributes:
	 * - GEMDOS_FILE_ATTRIB_HIDDEN (file not visible on desktop/fsel)
	 * - GEMDOS_FILE_ATTRIB_ARCHIVE (file written after being backed up)
	 * ?
	 */
	return Attrib;
}


/*-----------------------------------------------------------------------*/
/**
 * Populate the DTA buffer with file info.
 * @return   DTA_OK if entry is ok, DTA_SKIP if it should be skipped, DTA_ERR on errors
 */
static dta_ret_t PopulateDTA(char *path, struct dirent *file, DTA *pDTA, uint32_t DTA_Gemdos)
{
	/* TODO: host file path can be longer than MAX_GEMDOS_PATH */
	char tempstr[MAX_GEMDOS_PATH];
	struct stat filestat;
	DATETIME DateTime;
	int nFileAttr, nAttrMask;

	if (snprintf(tempstr, sizeof(tempstr), "%s%c%s",
	             path, PATHSEP, file->d_name) >= (int)sizeof(tempstr))
	{
		Log_Printf(LOG_ERROR, "PopulateDTA: path is too long.\n");
		return DTA_ERR;
	}

	if (stat(tempstr, &filestat) != 0)
	{
		/* skip file if it doesn't exist, otherwise return an error */
		dta_ret_t ret = (errno == ENOENT ? DTA_SKIP : DTA_ERR);
		perror(tempstr);
		return ret;
	}

	if (!pDTA)
		return DTA_ERR;   /* no DTA pointer set */

	/* Check file attributes (check is done according to the Profibuch) */
	nFileAttr = GemDOS_ConvertAttribute(filestat.st_mode, tempstr);
	nAttrMask = nAttrSFirst|GEMDOS_FILE_ATTRIB_WRITECLOSE|GEMDOS_FILE_ATTRIB_READONLY;
	if (nFileAttr != 0 && !(nAttrMask & nFileAttr))
		return DTA_SKIP;

	GemDOS_DateTime2Tos(filestat.st_mtime, &DateTime, tempstr);

	/* Atari memory modified directly through pDTA members -> flush the data cache */
	M68000_Flush_Data_Cache(DTA_Gemdos, sizeof(DTA));

	/* convert to atari-style uppercase */
	Str_Filename2TOSname(file->d_name, pDTA->dta_name);
#if DEBUG_PATTERN_MATCH
	fprintf(stderr, "DEBUG: GEMDOS: host: %s -> GEMDOS: %s\n",
		file->d_name, pDTA->dta_name);
#endif
	do_put_mem_long(pDTA->dta_size, filestat.st_size);
	do_put_mem_word(pDTA->dta_time, DateTime.timeword);
	do_put_mem_word(pDTA->dta_date, DateTime.dateword);
	pDTA->dta_attrib = nFileAttr;

	return DTA_OK;
}


/*-----------------------------------------------------------------------*/
/**
 * Clear given DTA cache structure.
 */
static void ClearInternalDTA(int idx)
{
	int i;

	/* clear the old DTA structure */
	if (InternalDTAs[idx].found != NULL)
	{
		for (i = 0; i < InternalDTAs[idx].nentries; i++)
			free(InternalDTAs[idx].found[i]);
		free(InternalDTAs[idx].found);
		InternalDTAs[idx].found = NULL;
	}
	InternalDTAs[idx].nentries = 0;
	InternalDTAs[idx].bUsed = false;
}

/*-----------------------------------------------------------------------*/
/**
 * Clear all DTA cache structures.
 *
 * If there are no DTA structures yet, allocate default amount
 */
static void GemDOS_ClearAllInternalDTAs(void)
{
	int i;
	if (!InternalDTAs)
	{
		DTACount = DTA_CACHE_INC;
		InternalDTAs = calloc(DTACount, sizeof(*InternalDTAs));
		assert(InternalDTAs);
	}
	for(i = 0; i < DTACount; i++)
	{
		ClearInternalDTA(i);
	}
	DTAIndex = 0;
}

/**
 * Free all DTA cache structures
 */
static void GemDOS_FreeAllInternalDTAs(void)
{
	int i;
	for(i = 0; i < DTACount; i++)
	{
		ClearInternalDTA(i);
	}
	if (InternalDTAs)
	{
		free(InternalDTAs);
		InternalDTAs = NULL;
		DTACount = 0;
	}
	DTAIndex = 0;
}

/*-----------------------------------------------------------------------*/
/**
 * Match a TOS file name to a dir mask.
 */
static bool fsfirst_match(const char *pat, const char *name)
{
	const char *dot, *p=pat, *n=name;

	if (name[0] == '.')
		return false;           /* skip .* files */

	dot = strrchr(name, '.');	/* '*' matches everything except last dot in name */
	if (dot && p[0] == '*' && p[1] == 0)
		return false;		/* plain '*' must not match anything with extension */

	while (*n)
	{
		if (*p=='*')
		{
			while (*n && n != dot)
				n++;
			p++;
		}
		else if (*p=='?' && *n)
		{
			n++;
			p++;
		}
		else if (toupper((unsigned char)*p++) != toupper((unsigned char)*n++))
			return false;
	}

	/* printf("'%s': '%s' -> '%s' : '%s' -> %d\n", name, pat, n, p); */

	/* The traversed name matches the pattern, if pattern also
	 * ends here, or with '*'. '*' for extension matches also
	 * filenames without extension, so pattern ending with
	 * '.*' will also be a match.
	 */
	return (
		(p[0] == 0) ||
		(p[0] == '*' && p[1] == 0) ||
		(p[0] == '.' && p[1] == '*' && p[2] == 0)
	       );
}


/*-----------------------------------------------------------------------*/
/**
 * Parse directory from sfirst mask
 * - e.g.: input:  "hdemudir/auto/mask*.*" outputs: "hdemudir/auto"
 */
static void fsfirst_dirname(const char *string, char *newstr)
{
	int i=0;

	strcpy(newstr, string);

	/* convert to front slashes and go to end of string. */
	while (newstr[i] != '\0')
	{
		if (newstr[i] == '\\')
			newstr[i] = PATHSEP;
		i++;
	}
	/* find last slash and terminate string */
	while (i && newstr[i] != PATHSEP)
		i--;
	newstr[i] = '\0';
}

/*-----------------------------------------------------------------------*/
/**
 * Close given internal file handle if it's still in use
 * and (always) reset handle variables
 */
static void GemDOS_CloseFileHandle(int i)
{
	if (FileHandles[i].bUsed)
		fclose(FileHandles[i].FileHandle);
	FileHandles[i].FileHandle = NULL;
	FileHandles[i].Basepage = 0;
	FileHandles[i].bUsed = false;
}

/**
 * Un-force given file handle
 */
static void GemDOS_UnforceFileHandle(int i)
{
	ForcedHandles[i].Handle = UNFORCED_HANDLE;
	ForcedHandles[i].Basepage = 0;
}

/**
 * Clear & un-force all file handles
 */
static void GemDOS_ClearAllFileHandles(void)
{
	int i;

	for(i = 0; i < ARRAY_SIZE(FileHandles); i++)
	{
		GemDOS_CloseFileHandle(i);
	}
	for(i = 0; i < ARRAY_SIZE(ForcedHandles); i++)
	{
		GemDOS_UnforceFileHandle(i);
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Initialize GemDOS/PC file system
 */
void GemDOS_Init(void)
{
	bInitGemDOS = false;
}

/*-----------------------------------------------------------------------*/
/**
 * Initialize GemDOS drives current paths (to drive root)
 */
static void GemDOS_InitCurPaths(void)
{
	int i;

	if (emudrives)
	{
		for (i = 0; i < MAX_HARDDRIVES; i++)
		{
			if (emudrives[i])
			{
				/* Initialize current directory to the root of the drive */
				strcpy(emudrives[i]->fs_currpath, emudrives[i]->hd_emulation_dir);
				File_AddSlashToEndFileName(emudrives[i]->fs_currpath);
			}
		}
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Reset GemDOS file system
 */
void GemDOS_Reset(void)
{
	GemDOS_Init();
	GemDOS_InitCurPaths();

	/* Reset */
	act_pd = 0;
	CurrentDrive = nBootDrive;
	Symbols_RemoveCurrentProgram();
	INF_CreateOverride();
}

/*-----------------------------------------------------------------------*/
/**
 * Routine to check the Host OS HDD path for a Drive letter sub folder
 */
static bool GEMDOS_DoesHostDriveFolderExist(char* lpstrPath, int iDrive)
{
	bool bExist = false;

	Log_Printf(LOG_DEBUG, "Checking GEMDOS %c: HDD: %s\n", 'A'+iDrive, lpstrPath);

	if (access(lpstrPath, F_OK) != 0 )
	{
		/* Try lower case drive letter instead */
		int	iIndex = strlen(lpstrPath)-1;
		lpstrPath[iIndex] = tolower((unsigned char)lpstrPath[iIndex]);
	}

	/* Check if it's a HDD identifier (or other emulated device)
	 * and if the file/folder is accessible (security basis) */
	if (iDrive > 1 && access(lpstrPath, F_OK) == 0 )
	{
		struct stat status;
		if (stat(lpstrPath, &status) == 0 && (status.st_mode & S_IFDIR) != 0)
		{
			bExist = true;
		}
		else
		{
			Log_Printf(LOG_WARN, "Not suitable as GEMDOS HDD dir: %s\n", lpstrPath);
		}
	}

	return bExist;
}


/**
 * Determine upper limit of partitions that should be emulated.
 *
 * @return true if multiple GEMDOS partitions should be emulated, false otherwise
 */
static bool GemDOS_DetermineMaxPartitions(int *pnMaxDrives)
{
	struct dirent **files;
	int count, i, last;
	char letter;
	bool bMultiPartitions;

	*pnMaxDrives = 0;

	/* Scan through the main directory to see whether there are just single
	 * letter sub-folders there (then use multi-partition mode) or if
	 * arbitrary sub-folders are there (then use single-partition mode)
	 */
	count = scandir(ConfigureParams.HardDisk.szHardDiskDirectories[0], &files, 0, alphasort);
	if (count < 0)
	{
		Log_Printf(LOG_ERROR, "GEMDOS hard disk emulation failed:\n "
			   "Can not access '%s'.\n", ConfigureParams.HardDisk.szHardDiskDirectories[0]);
		return false;
	}
	else if (count <= 2)
	{
		/* Empty directory Only "." and ".."), assume single partition mode */
		last = 1;
		bMultiPartitions = false;
	}
	else
	{
		bMultiPartitions = true;
		/* Check all files in the directory */
		last = 0;
		for (i = 0; i < count; i++)
		{
			letter = toupper((unsigned char)files[i]->d_name[0]);
			if (!letter || letter == '.')
			{
				/* Ignore hidden files like "." and ".." */
				continue;
			}
			
			if (letter < 'C' || letter > 'Z' || files[i]->d_name[1])
			{
				/* folder with name other than C-Z...
				 * (until Z under MultiTOS, to P otherwise)
				 * ... so use single partition mode! */
				last = 1;
				bMultiPartitions = false;
				break;
			}

			/* alphasort isn't case insensitive */
			letter = letter - 'C' + 1;
			if (letter > last)
				last = letter;
		}
	}

	if (last > MAX_HARDDRIVES)
		*pnMaxDrives = MAX_HARDDRIVES;
	else
		*pnMaxDrives = last;

	/* Free file list */
	for (i = 0; i < count; i++)
		free(files[i]);
	free(files);

	return bMultiPartitions;
}

/*-----------------------------------------------------------------------*/
/**
 * Initialize a GEMDOS drive.
 * Supports up to MAX_HARDDRIVES HDD units.
 */
void GemDOS_InitDrives(void)
{
	int i;
	int nMaxDrives;
	int DriveNumber;
	int SkipPartitions;
	int ImagePartitions;
	bool bMultiPartitions;

	GemDOS_ClearAllFileHandles();
	GemDOS_ClearAllInternalDTAs();

	bMultiPartitions = GemDOS_DetermineMaxPartitions(&nMaxDrives);

	/* initialize data for harddrive emulation: */
	if (nMaxDrives > 0 && !emudrives)
	{
		emudrives = calloc(MAX_HARDDRIVES, sizeof(EMULATEDDRIVE *));
		if (!emudrives)
		{
			perror("GemDOS_InitDrives");
			return;
		}
	}

	ImagePartitions = nAcsiPartitions + nScsiPartitions + nIDEPartitions;
	if (ConfigureParams.HardDisk.nGemdosDrive == DRIVE_SKIP)
		SkipPartitions = ImagePartitions;
	else
		SkipPartitions = ConfigureParams.HardDisk.nGemdosDrive;

	Log_Printf(LOG_DEBUG, "ACSI: %d, SCSI: %d, IDE: %d - GEMDOS skipping %d partitions.\n",
		   nAcsiPartitions, nScsiPartitions, nIDEPartitions, SkipPartitions);

	/* Now initialize all available drives */
	for(i = 0; i < nMaxDrives; i++)
	{
		/* If single partition mode, skip to specified / first free drive */
		if (!bMultiPartitions)
		{
			i += SkipPartitions;
		}

		/* Allocate emudrives entry for this drive */
		emudrives[i] = malloc(sizeof(EMULATEDDRIVE));
		if (!emudrives[i])
		{
			perror("GemDOS_InitDrives");
			continue;
		}

		/* set emulation directory string */
		strcpy(emudrives[i]->hd_emulation_dir, ConfigureParams.HardDisk.szHardDiskDirectories[0]);

		/* remove trailing slash, if any in the directory name */
		File_CleanFileName(emudrives[i]->hd_emulation_dir);

		/* Add requisite folder ID */
		if (bMultiPartitions)
		{
			char sDriveLetter[] = { PATHSEP, (char)('C' + i), '\0' };
			strcat(emudrives[i]->hd_emulation_dir, sDriveLetter);
		}
		/* drive number (C: = 2, D: = 3, etc.) */
		DriveNumber = 2 + i;

		// Check host file system to see if the drive folder for THIS
		// drive letter/number exists...
		if (GEMDOS_DoesHostDriveFolderExist(emudrives[i]->hd_emulation_dir, DriveNumber))
		{
			/* map drive */
			Log_Printf(LOG_INFO, "GEMDOS HDD emulation, %c: <-> %s.\n",
				   'A'+DriveNumber, emudrives[i]->hd_emulation_dir);
			emudrives[i]->drive_number = DriveNumber;
			nNumDrives = i + 3;

			/* This letter may already be allocated to the one supported physical disk images
			 * (depends on how well Atari HD driver and Hatari interpretation of partition
			 *  table(s) match each other).
			 */
			if (i < ImagePartitions)
				Log_Printf(LOG_WARN, "GEMDOS HD drive %c: (may) override ACSI/SCSI/IDE image partitions!\n", 'A'+DriveNumber);
		}
		else
		{
			free(emudrives[i]);	// Deallocate Memory (save space)
			emudrives[i] = NULL;
		}
	}

	/* Set current paths in case Atari -> host GEMDOS path mapping
	 * is needed before TOS boots GEMDOS up (at which point they're
	 * also initialized), like happens with autostart INF file
	 * handling.
	 */
	GemDOS_InitCurPaths();
}


/*-----------------------------------------------------------------------*/
/**
 * Un-init GEMDOS drives
 */
void GemDOS_UnInitDrives(void)
{
	int i;

	GemDOS_FreeAllInternalDTAs();

	GemDOS_Reset();        /* Close all open files on emulated drive */

	if (GEMDOS_EMU_ON)
	{
		for(i = 0; i < MAX_HARDDRIVES; i++)
		{
			if (emudrives[i])
			{
				free(emudrives[i]);    /* Release memory */
				emudrives[i] = NULL;
				nNumDrives -= 1;
			}
		}

		free(emudrives);
		emudrives = NULL;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Save file handle info.  If handle is used, save valid file modification
 * timestamp and file position, otherwise dummies.
 */
static void save_file_handle_info(FILE_HANDLE *handle)
{
	struct stat fstat;
	time_t mtime = 0;
	off_t offset = 0;

	MemorySnapShot_Store(&handle->bUsed, sizeof(handle->bUsed));
	MemorySnapShot_Store(&handle->szMode, sizeof(handle->szMode));
	MemorySnapShot_Store(&handle->Basepage, sizeof(handle->Basepage));
	MemorySnapShot_Store(&handle->szActualName, sizeof(handle->szActualName));
	if (handle->bUsed)
	{
		offset = ftello(handle->FileHandle);
		if (stat(handle->szActualName, &fstat) == 0)
			mtime = fstat.st_mtime; /* modification time */
	}
	MemorySnapShot_Store(&mtime, sizeof(mtime));
	MemorySnapShot_Store(&offset, sizeof(offset));
}

/*-----------------------------------------------------------------------*/
/**
 * Restore saved file handle info.  If handle is used, open file, validate
 * that file modification timestamp matches, then seek to saved position.
 * Restoring order must match one used in save_file_handle_info().
 */
static void restore_file_handle_info(int i, FILE_HANDLE *handle)
{
	struct stat fstat;
	time_t mtime;
	off_t offset;
	FILE *fp;

	if (handle->bUsed)
		fclose(handle->FileHandle);

	/* read all to proceed correctly in snapshot */
	MemorySnapShot_Store(&handle->bUsed, sizeof(handle->bUsed));
	MemorySnapShot_Store(&handle->szMode, sizeof(handle->szMode));
	MemorySnapShot_Store(&handle->Basepage, sizeof(handle->Basepage));
	MemorySnapShot_Store(&handle->szActualName, sizeof(handle->szActualName));
	MemorySnapShot_Store(&mtime, sizeof(mtime));
	MemorySnapShot_Store(&offset, sizeof(offset));
	handle->FileHandle = NULL;

	if (!handle->bUsed)
		return;

	if (stat(handle->szActualName, &fstat) != 0)
	{
		handle->bUsed = false;
		Log_Printf(LOG_WARN, "GEMDOS handle %d cannot be restored, file missing: %s\n",
			   i, handle->szActualName);
		return;
	}
	/* assumes time_t is primitive type (unsigned long on Linux) */
	if (fstat.st_mtime != mtime)
	{
		Log_Printf(LOG_WARN, "restored GEMDOS handle %d points to a file that has been modified in meanwhile: %s\n",
			   i, handle->szActualName);
	}
	fp = fopen(handle->szActualName, handle->szMode);
	if (fp == NULL || fseeko(fp, offset, SEEK_SET) != 0)
	{
		handle->bUsed = false;
		Log_Printf(LOG_WARN, "GEMDOS '%s' handle %d cannot be restored, seek to saved offset %"PRId64" failed for: %s\n",
			   handle->szMode, i, (int64_t)offset, handle->szActualName);
		if (fp)
			fclose(fp);
		return;
	}
	/* used only for warnings, ignore those after restore */
	handle->bReadOnly = false;
	handle->FileHandle = fp;
}

/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
 */
void GemDOS_MemorySnapShot_Capture(bool bSave)
{
	FILE_HANDLE *finfo;
	int i, handles = ARRAY_SIZE(FileHandles);
	bool bEmudrivesAvailable;

	/* Save/Restore the emudrives structure */
	bEmudrivesAvailable = (emudrives != NULL);
	MemorySnapShot_Store(&bEmudrivesAvailable, sizeof(bEmudrivesAvailable));
	if (bEmudrivesAvailable)
	{
		if (!emudrives)
		{
			/* As memory snapshot contained emulated drive(s),
			 * but currently there are none allocated yet...
			 * let's do it now!
			 */
			GemDOS_InitDrives();
		}

		for(i = 0; i < MAX_HARDDRIVES; i++)
		{
			int bDummyDrive = false;
			if (!emudrives[i])
			{
				/* Allocate a dummy drive */
				emudrives[i] = malloc(sizeof(EMULATEDDRIVE));
				if (!emudrives[i])
				{
					perror("GemDOS_MemorySnapShot_Capture");
					continue;
				}
				memset(emudrives[i], 0, sizeof(EMULATEDDRIVE));
				bDummyDrive = true;
			}
			MemorySnapShot_Store(emudrives[i]->hd_emulation_dir,
			                     sizeof(emudrives[i]->hd_emulation_dir));
			MemorySnapShot_Store(emudrives[i]->fs_currpath,
			                     sizeof(emudrives[i]->fs_currpath));
			MemorySnapShot_Store(&emudrives[i]->drive_number,
			                     sizeof(emudrives[i]->drive_number));
			if (bDummyDrive)
			{
				free(emudrives[i]);
				emudrives[i] = NULL;
			}
		}
	}

	/* misc information */
	MemorySnapShot_Store(&bInitGemDOS,sizeof(bInitGemDOS));
	MemorySnapShot_Store(&act_pd, sizeof(act_pd));
	MemorySnapShot_Store(&CurrentDrive, sizeof(CurrentDrive));

	MemorySnapShot_Store(&nSavedPexecParams, sizeof(nSavedPexecParams));

	/* File handle related information */
	MemorySnapShot_Store(&ForcedHandles, sizeof(ForcedHandles));
	if (bSave)
	{
		MemorySnapShot_Store(&handles, sizeof(handles));

		for (finfo = FileHandles, i = 0; i < handles; i++, finfo++)
			save_file_handle_info(finfo);
	}
	else
	{
		int saved_handles;
		MemorySnapShot_Store(&saved_handles, sizeof(saved_handles));
		assert(saved_handles == handles);

		for (finfo = FileHandles, i = 0; i < handles; i++, finfo++)
			restore_file_handle_info(i, finfo);

		/* DTA file name cache isn't valid anymore */
		GemDOS_ClearAllInternalDTAs();
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Return free PC file handle table index, or -1 if error
 */
static int GemDOS_FindFreeFileHandle(void)
{
	int i;

	/* Scan our file list for free slot */
	for(i = 0; i < ARRAY_SIZE(FileHandles); i++)
	{
		if (!FileHandles[i].bUsed)
			return i;
	}

	/* Cannot open any more files, return error */
	return -1;
}

/*-----------------------------------------------------------------------*/
/**
 * Check whether given basepage matches current program basepage
 * or basepage for its parents.  If yes, return true, otherwise false.
 */
static bool GemDOS_BasepageMatches(uint32_t checkbase)
{
	int maxparents = 12; /* prevent basepage parent loops */
	uint32_t basepage = STMemory_ReadLong(act_pd);
	while (maxparents-- > 0 && STMemory_CheckAreaType(basepage, BASEPAGE_SIZE, ABFLAG_RAM))
	{
		if (basepage == checkbase)
			return true;
		basepage = STMemory_ReadLong(basepage + BASEPAGE_OFFSET_PARENT);
	}
	return false;
}

/**
 * Check whether TOS handle is within our table range, or aliased,
 * return (positive) internal Handle if yes, (negative) -1 for error.
 */
static int GemDOS_GetValidFileHandle(int Handle)
{
	int Forced = -1;

	/* Has handle been aliased with Fforce()? */
	if (Handle >= 0 && Handle < ARRAY_SIZE(ForcedHandles)
	    && ForcedHandles[Handle].Handle != UNFORCED_HANDLE)
	{
		if (GemDOS_BasepageMatches(ForcedHandles[Handle].Basepage))
		{
			Forced = Handle;
			Handle = ForcedHandles[Handle].Handle;
		}
		else
		{
			Log_Printf(LOG_WARN, "Removing (stale?) %d->%d file handle redirection.",
				   Handle, ForcedHandles[Handle].Handle);
			GemDOS_UnforceFileHandle(Handle);
			return -1;
		}
	}
	else
	{
		Handle -= BASE_FILEHANDLE;
	}
	/* handle is valid for current program and in our handle table? */
	if (Handle >= 0 && Handle < ARRAY_SIZE(FileHandles)
	    && FileHandles[Handle].bUsed)
	{
		uint32_t current = STMemory_ReadLong(act_pd);
		if (FileHandles[Handle].Basepage == current || Forced >= 0)
			return Handle;
		/* A potential bug in Atari program or Hatari GEMDOS emulation */
		Log_Printf(LOG_WARN, "program (basebase 0x%x) accessing another program (basepage 0x%x) file handle %d.",
			     current, FileHandles[Handle].Basepage, Handle);
		return Handle;
	}
	/* invalid handle */
	return -1;
}

/*-----------------------------------------------------------------------*/
/**
 * Find drive letter from a filename, eg C,D... and return as drive ID(C:2, D:3...)
 * returns the current drive number if no drive is specified.  For special
 * devices (CON:, AUX:, PRN:), returns an invalid drive number.
 */
static int GemDOS_FindDriveNumber(char *pszFileName)
{
	/* Does have 'A:' or 'C:' etc.. at start of string? */
	if (pszFileName[0] != '\0' && pszFileName[1] == ':')
	{
		char letter = toupper((unsigned char)pszFileName[0]);
		if (letter >= 'A' && letter <= 'Z')
			return (letter-'A');
	}
	else if (strlen(pszFileName) == 4 && pszFileName[3] == ':')
	{
		/* ':' can be used only as drive indicator, not otherwise,
		 * so no need to check even special device name.
		 */
		return 0;
	}
	return CurrentDrive;
}


/**
 * Return true if drive ID (C:2, D:3 etc...) matches emulated hard-drive
 */
bool GemDOS_IsDriveEmulated(int drive)
{
	drive -= 2;
	if (drive < 0 || drive >= MAX_HARDDRIVES)
		return false;
	if (!(emudrives && emudrives[drive]))
		return false;
	assert(emudrives[drive]->drive_number == drive+2);
	return true;
}

/*-----------------------------------------------------------------------*/
/**
 * Return drive ID(C:2, D:3 etc...) or -1 if not one of our emulation hard-drives
 */
static int GemDOS_FileName2HardDriveID(char *pszFileName)
{
	/* Do we even have a hard-drive? */
	if (GEMDOS_EMU_ON)
	{
		int DriveNumber;

		/* Find drive letter (as number) */
		DriveNumber = GemDOS_FindDriveNumber(pszFileName);
		if (GemDOS_IsDriveEmulated(DriveNumber))
			return DriveNumber;
	}

	/* Not a high-level redirected drive, let TOS handle it */
	return -1;
}


/*-----------------------------------------------------------------------*/
/**
 * Check whether a file in given path matches given case-insensitive pattern.
 * Return first matched name which caller needs to free, or NULL for no match.
 */
static char* match_host_dir_entry(const char *path, const char *name, bool pattern)
{
#define MAX_UTF8_NAME_LEN (3*(8+1+3)+1) /* UTF-8 can have up to 3 bytes per character */
	struct dirent *entry;
	char *match = NULL;
	DIR *dir;
	char nameHost[MAX_UTF8_NAME_LEN];

	Str_AtariToHost(name, nameHost, MAX_UTF8_NAME_LEN, INVALID_CHAR);
	name = nameHost;
	
	dir = opendir(path);
	if (!dir)
		return NULL;

#if DEBUG_PATTERN_MATCH
	fprintf(stderr, "DEBUG: GEMDOS match '%s'%s in '%s'", name, pattern?" (pattern)":"", path);
#endif
	if (pattern)
	{
		while ((entry = readdir(dir)))
		{
			char *d_name = entry->d_name;
			Str_DecomposedToPrecomposedUtf8(d_name, d_name);   /* for OSX */
			if (fsfirst_match(name, d_name))
			{
				match = strdup(d_name);
				break;
			}
		}
	}
	else
	{
		while ((entry = readdir(dir)))
		{
			char *d_name = entry->d_name;
			Str_DecomposedToPrecomposedUtf8(d_name, d_name);   /* for OSX */
			if (strcasecmp(name, d_name) == 0)
			{
				match = strdup(d_name);
				break;
			}
		}
	}
	closedir(dir);
#if DEBUG_PATTERN_MATCH
	fprintf(stderr, "-> '%s'\n", match);
#endif
	return match;
}


static int to_same(int ch)
{
	return ch;
}

/**
 * Clip given file name to 8+3 length like TOS does,
 * return resulting name length.
 */
static int clip_to_83(char *name)
{
	int diff, len;
	char *dot;
	
	dot = strchr(name, '.');
	if (dot) {
		diff = strlen(dot) - 4;
		if (diff > 0)
		{
			Log_Printf(LOG_WARN, "have to clip %d chars from '%s' extension!\n", diff, name);
			dot[4] = '\0';
		}
		diff = dot - name - 8;
		if (diff > 0)
		{
			Log_Printf(LOG_WARN, "have to clip %d chars from '%s' base!\n", diff, name);
			memmove(name + 8, dot, strlen(dot) + 1);
		}
		return strlen(name);
	}
	len = strlen(name);
	if (len > 8)
	{
		Log_Printf(LOG_WARN, "have to clip %d chars from '%s'!\n", len - 8, name);
		name[8] = '\0';
		len = 8;
	}
	return len;
}

/*-----------------------------------------------------------------------*/
/**
 * Check whether given TOS file/dir exists in given host path.
 * If it does, add the matched host filename to the given path,
 * otherwise add the given filename as is to it.  Guarantees
 * that the resulting string doesn't exceed maxlen+1.
 * 
 * Return true if match found, false otherwise.
 */
static bool add_path_component(char *path, int maxlen, const char *origname, bool is_dir)
{
	char *tmp, *match;
	int dot, namelen, pathlen;
	int (*chr_conv)(int);
	bool modified;
	char *name = alloca(strlen(origname) + 3);

	/* append separator */
	pathlen = strlen(path);
	if (pathlen >= maxlen)
		return false;
	path[pathlen++] = PATHSEP;
	path[pathlen] = '\0';

	/* TOS clips names to 8+3 length */
	strcpy(name, origname);
	namelen = clip_to_83(name);

	/* first try exact (case insensitive) match */
	match = match_host_dir_entry(path, name, false);
	if (match)
	{
		/* use strncat so that string is always nul terminated */
		strncat(path+pathlen, match, maxlen-pathlen);
		free(match);
		return true;
	}

	/* Here comes a work-around for a bug in the file selector
	 * of TOS 1.02: When a folder name has exactly 8 characters,
	 * it appends a '.' at the end of the name...
	 */
	if (is_dir && namelen == 9 && name[8] == '.')
	{
		name[8] = '\0';
		match = match_host_dir_entry(path, name, false);
		if (match)
		{
			strncat(path+pathlen, match, maxlen-pathlen);
			free(match);
			return true;
		}
	}

	/* Assume there were invalid characters or that the host file
	 * was too long to fit into GEMDOS 8+3 filename limits.
	 * If that's the case, modify the name to a pattern that
	 * will match such host files and try again.
	 */
	modified = false;

	/* catch potentially invalid characters */
	for (tmp = name; *tmp; tmp++)
	{
		if (*tmp == INVALID_CHAR)
		{
			*tmp = '?';
			modified = true;
		}
	}

	/* catch potentially too long extension */
	for (dot = 0; name[dot] && name[dot] != '.'; dot++);
	if (namelen - dot > 3)
	{
		dot++;
		/* "emulated.too" -> "emulated.too*" */
		name[namelen++] = '*';
		name[namelen] = '\0';
		modified = true;
	}
	/* catch potentially too long part before extension */
	if (namelen > 8 && name[8] == '.')
	{
		dot++;
		/* "emulated.too*" -> "emulated*.too*" */
		memmove(name+9, name+8, namelen-7);
		namelen++;
		name[8] = '*';
		modified = true;
	}
	/* catch potentially too long part without extension */
	else if (namelen == 8 && !name[dot])
	{
		/* "emulated" -> "emulated*" */
		name[8] = '*';
		name[9] = '\0';
		namelen++;
		modified = true;
	}

	if (modified)
	{
		match = match_host_dir_entry(path, name, true);
		if (match)
		{
			strncat(path+pathlen, match, maxlen-pathlen);
			free(match);
			return true;
		}
	}

	/* not found, copy file/dirname as is */
	switch (ConfigureParams.HardDisk.nGemdosCase) {
	case GEMDOS_UPPER:
		chr_conv = toupper;
		break;
	case GEMDOS_LOWER:
		chr_conv = tolower;
		break;
	default:
		chr_conv = to_same;
	}
	tmp = name;
	while (*origname)
		*tmp++ = chr_conv(*origname++);
	*tmp = '\0';
	/* strncat(path+pathlen, name, maxlen-pathlen); */
	Str_AtariToHost(name, path+pathlen, maxlen-pathlen, INVALID_CHAR);
	return false;
}


/**
 * Join remaining path without matching. This helper is used after host
 * file name matching fails, to append the failing part of the TOS path
 * to the host path, so that it won't be a valid host path.
 *
 * Specifically, the path separators need to be converted, otherwise things
 * like Fcreate() could create files that have TOS directory names as part
 * of file names on Unix (as \ is valid filename char on Unix).  Fcreate()
 * needs to create them only when just the file name isn't found, but all
 * the directory components have.
 */
static void add_remaining_path(const char *src, char *dstpath, int dstlen)
{
	char *dst;
	int i = strlen(dstpath);

	Str_AtariToHost(src, dstpath+i, dstlen-i, INVALID_CHAR);

	for (dst = dstpath + i; *dst; dst++)
		if (*dst == '\\')
			*dst = PATHSEP;
}


/*-----------------------------------------------------------------------*/
/**
 * Use hard-drive directory, current ST directory and filename
 * to create correct path to host file system.  If given filename
 * isn't found on host file system, just append GEMDOS filename
 * to the path as is.
 * 
 * TODO: currently there are many callers which give this dest buffer of
 * MAX_GEMDOS_PATH size i.e. don't take into account that host filenames
 * can be up to FILENAME_MAX long.  Plain GEMDOS paths themselves may be
 * MAX_GEMDOS_PATH long even before host dir is prepended to it!
 * Way forward: allocate the host path here as FILENAME_MAX so that
 * it's always long enough and let callers free it. Assert if alloc
 * fails so that callers' don't need to.
 */
void GemDOS_CreateHardDriveFileName(int Drive, const char *pszFileName,
                                    char *pszDestName, int nDestNameLen)
{
	const char *s, *filename = pszFileName;
	int minlen;

	/* make sure that more convenient strncat() can be used on the
	 * destination string (it always null terminates unlike strncpy()) */
	*pszDestName = 0;

	/* Is it a valid hard drive? */
	assert(GemDOS_IsDriveEmulated(Drive));

	/* Check for valid string */
	if (filename[0] == '\0')
		return;

	/* strcat writes n+1 chars, so decrease len */
	nDestNameLen--;
	
	/* full filename with drive "C:\foo\bar" */
	if (filename[1] == ':')
	{
		strncat(pszDestName, emudrives[Drive-2]->hd_emulation_dir, nDestNameLen);
		filename += 2;
	}
	/* filename referenced from root: "\foo\bar" */
	else if (filename[0] == '\\')
	{
		strncat(pszDestName, emudrives[Drive-2]->hd_emulation_dir, nDestNameLen);
	}
	/* filename relative to current directory */
	else
	{
		strncat(pszDestName, emudrives[Drive-2]->fs_currpath, nDestNameLen);
	}

	minlen = strlen(emudrives[Drive-2]->hd_emulation_dir);
	/* this doesn't take into account possible long host filenames
	 * that will make dest name longer than pszFileName 8.3 paths,
	 * or GEMDOS paths using "../" which make it smaller.  Both
	 * should(?) be rare in paths, so this info to user should be
	 * good enough.
	 */
	if (nDestNameLen < minlen + (int)strlen(pszFileName) + 2)
	{
		Log_AlertDlg(LOG_ERROR, "Appending GEMDOS path '%s' to HDD emu host root dir doesn't fit to %d chars (current Hatari limit)!",
			     pszFileName, nDestNameLen);
		add_remaining_path(filename, pszDestName, nDestNameLen);
		return;
	}

	/* "../" handling breaks if there are extra slashes */
	File_CleanFileName(pszDestName);
	
	/* go through path directory components, advancing 'filename'
	 * pointer while parsing them.
	 */
	for (;;)
	{
		/* skip extra path separators */
		while (*filename == '\\')
			filename++;

		// fprintf(stderr, "filename: '%s', path: '%s'\n", filename, pszDestName);

		/* skip "." references to current directory */
		if (filename[0] == '.' &&
		    (filename[1] == '\\' || !filename[1]))
		{
			filename++;
			continue;
		}

		/* ".." path component -> strip last dir from dest path */
		if (filename[0] == '.' &&
		    filename[1] == '.' &&
		    (filename[2] == '\\' || !filename[2]))
		{
			char *sep = strrchr(pszDestName, PATHSEP);
			if (sep)
			{
				if (sep - pszDestName < minlen)
					Log_Printf(LOG_WARN, "GEMDOS path '%s' tried to back out of GEMDOS drive!\n", pszFileName);
				else
					*sep = '\0';
			}
			filename += 2;
			continue;
		}

		/* handle directory component */
		if ((s = strchr(filename, '\\')))
		{
			int dirlen = s - filename;
			char *dirname = alloca(dirlen + 1);
			/* copy dirname */
			strncpy(dirname, filename, dirlen);
			dirname[dirlen] = '\0';
			/* and advance filename */
			filename = s;

			if (strchr(dirname, '?') || strchr(dirname, '*'))
				Log_Printf(LOG_WARN, "GEMDOS dir name '%s' with wildcards in %s!\n", dirname, pszFileName);

			/* convert and append dirname to host path */
			if (!add_path_component(pszDestName, nDestNameLen, dirname, true))
			{
				Log_Printf(LOG_WARN, "No GEMDOS dir '%s'\n", pszDestName);
				add_remaining_path(filename, pszDestName, nDestNameLen);
				return;
			}
			continue;
		}

		/* path directory components done */
		break;
	}

	if (*filename)
	{
		/* a wildcard instead of a complete file name? */
		if (strchr(filename,'?') || strchr(filename,'*'))
		{
			int len = strlen(pszDestName);
			if (len < nDestNameLen)
			{
				pszDestName[len++] = PATHSEP;
				pszDestName[len] = '\0';
			}
			/* use strncat so that string is always nul terminated */
			/* strncat(pszDestName+len, filename, nDestNameLen-len); */
			Str_AtariToHost(filename, pszDestName+len, nDestNameLen-len, INVALID_CHAR);
		}
		else if (!add_path_component(pszDestName, nDestNameLen, filename, false))
		{
			/* It's often normal, that GEM uses this to test for
			 * existence of desktop.inf or newdesk.inf for example.
			 */
			LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS didn't find filename %s\n", pszDestName);
			return;
		}
	}
	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS: %s -> host: %s\n", pszFileName, pszDestName);
}


/**
 * GEMDOS Cconws
 * Call 0x9
 */
static bool GemDOS_Cconws(uint32_t Params)
{
	uint32_t Addr;
	char *pBuffer;

	Addr = STMemory_ReadLong(Params);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x9 Cconws(0x%X) at PC 0x%X\n",
		  Addr, CallingPC);

	/* We only intercept this call in non-TOS mode */
	if (bUseTos)
		return false;

	/* Check that write is from valid memory area */
	if ((CallingPC < TosAddress || CallingPC >= TosAddress + TosSize)
	    && !STMemory_CheckAreaType(Addr, 80 * 25, ABFLAG_RAM))
	{
		Log_Printf(LOG_WARN, "GEMDOS Cconws() failed due to invalid RAM range at 0x%x\n", Addr);
		Regs[REG_D0] = GEMDOS_ERANGE;
		return true;
	}

	pBuffer = (char *)STMemory_STAddrToPointer(Addr);
	if (fwrite(pBuffer, strnlen(pBuffer, 80 * 25), 1, stdout) < 1)
		Regs[REG_D0] = GEMDOS_ERROR;
	else
		Regs[REG_D0] = GEMDOS_EOK;

	return true;
}

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Set drive (0=A,1=B,2=C etc...)
 * Call 0xE
 */
static bool GemDOS_SetDrv(uint32_t Params)
{
	/* Read details from stack for our own use */
	CurrentDrive = STMemory_ReadWord(Params);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x0E Dsetdrv(0x%x) at PC=0x%X\n", (int)CurrentDrive,
		  CallingPC);

	/* Still re-direct to TOS */
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Dfree Free disk space.
 * Call 0x36
 */
static bool GemDOS_DFree(uint32_t Params)
{
#ifdef HAVE_STATVFS
	struct statvfs buf;
#endif
	int Drive, Total, Free;
	uint32_t Address;

	Address = STMemory_ReadLong(Params);
	Drive = STMemory_ReadWord(Params+SIZE_LONG);

	/* Note: Drive = 0 means current drive, 1 = A:, 2 = B:, 3 = C:, etc. */
	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x36 Dfree(0x%x, %i) at PC 0x%X\n", Address, Drive,
		  CallingPC);
	if (Drive == 0)
		Drive = CurrentDrive;
	else
		Drive--;

	/* is it our drive? */
	if (!GemDOS_IsDriveEmulated(Drive))
	{
		/* no, redirect to TOS */
		return false;
	}
	/* Check that write is requested to valid memory area */
	if ( !STMemory_CheckAreaType ( Address, 16, ABFLAG_RAM ) )
	{
		Log_Printf(LOG_WARN, "GEMDOS Dfree() failed due to invalid RAM range 0x%x+%i\n", Address, 16);
		Regs[REG_D0] = GEMDOS_ERANGE;
		return true;
	}

#ifdef HAVE_STATVFS
	if (statvfs(emudrives[Drive-2]->hd_emulation_dir, &buf) == 0)
	{
		Total = buf.f_blocks/1024 * buf.f_frsize;
		if (buf.f_bavail)
			Free = buf.f_bavail;	/* free for unprivileged user */
		else
			Free = buf.f_bfree;
		Free = Free/1024 * buf.f_bsize;

		/* TOS version limits based on:
		 *   http://hddriver.seimet.de/en/faq.html
		 */
		if (TosVersion >= 0x0400)
		{
			if (Total > 1024*1024)
				Total = 1024*1024;
		}
		else
		{
			if (TosVersion >= 0x0106)
			{
				if (Total > 512*1024)
					Total = 512*1024;
			}
			else
			{
				if (Total > 256*1024)
					Total = 256*1024;
			}
		}
		if (Free > Total)
			Free = Total;
	}
	else
#endif
	{
		/* fake 32MB drive with 16MB free */
		Total = 32*1024;
		Free = 16*1024;
	}
	STMemory_WriteLong(Address,  Free);             /* free clusters */
	STMemory_WriteLong(Address+SIZE_LONG, Total);   /* total clusters */

	STMemory_WriteLong(Address+SIZE_LONG*2, 512);   /* bytes per sector */
	STMemory_WriteLong(Address+SIZE_LONG*3, 2);     /* sectors per cluster (cluster = 1KB) */
	Regs[REG_D0] = GEMDOS_EOK;
	return true;
}



/*-----------------------------------------------------------------------*/
typedef enum {
	ERROR_FILE,
	ERROR_PATH
} etype_t;

/**
 * Helper to log libc errno and map it to GEMDOS error value
 */
static uint32_t errno2gemdos(const int error, const etype_t etype)
{
	LOG_TRACE(TRACE_OS_GEMDOS, "-> ERROR (errno = %d)\n", error);
	switch (error)
	{
	case ENOENT:
		if (etype == ERROR_FILE)
			return GEMDOS_EFILNF;/* File not found */
		/* fallthrough */
	case ENOTDIR:
		return GEMDOS_EPTHNF;        /* Path not found */
	case ENOTEMPTY:
	case EEXIST:
	case EPERM:
	case EACCES:
	case EROFS:
		return GEMDOS_EACCDN;        /* Access denied */
	default:
		return GEMDOS_ERROR;         /* Misc error */
	}
}

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS MkDir
 * Call 0x39
 */
static bool GemDOS_MkDir(uint32_t Params)
{
	char *pDirName, *psDirPath;
	int Drive;
	uint32_t nStrAddr = STMemory_ReadLong(Params);

	pDirName = STMemory_GetStringPointer(nStrAddr);
	if (!pDirName || !pDirName[0])
	{
		LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE, "GEMDOS 0x39 bad Dcreate(0x%X) at PC 0x%X\n",
		          nStrAddr, CallingPC);
		return false;
	}

	LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE, "GEMDOS 0x39 Dcreate(\"%s\") at PC 0x%X\n", pDirName,
		  CallingPC);

	Drive = GemDOS_FileName2HardDriveID(pDirName);

	if (!ISHARDDRIVE(Drive))
	{
		/* redirect to TOS */
		return false;
	}

	/* write protected device? */
	if (ConfigureParams.HardDisk.nWriteProtection == WRITEPROT_ON)
	{
		Log_Printf(LOG_WARN, "PREVENTED: GEMDOS Dcreate(\"%s\")\n", pDirName);
		Regs[REG_D0] = GEMDOS_EWRPRO;
		return true;
	}

	psDirPath = malloc(FILENAME_MAX);
	if (!psDirPath)
	{
		perror("GemDOS_MkDir");
		Regs[REG_D0] = GEMDOS_ENSMEM;
		return true;
	}
	
	/* Copy old directory, as if calls fails keep this one */
	GemDOS_CreateHardDriveFileName(Drive, pDirName, psDirPath, FILENAME_MAX);
	
	/* Attempt to make directory */
	if (mkdir(psDirPath, 0755) == 0)
		Regs[REG_D0] = GEMDOS_EOK;
	else
		Regs[REG_D0] = errno2gemdos(errno, ERROR_PATH);
	free(psDirPath);
	return true;
}

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS RmDir
 * Call 0x3A
 */
static bool GemDOS_RmDir(uint32_t Params)
{
	char *pDirName, *psDirPath;
	int Drive;
	uint32_t nStrAddr = STMemory_ReadLong(Params);

	pDirName = STMemory_GetStringPointer(nStrAddr);
	if (!pDirName || !pDirName[0])
	{
		LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE, "GEMDOS 0x3A bad Ddelete(0x%X) at PC 0x%X\n",
		          nStrAddr, CallingPC);
		return false;
	}

	LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE, "GEMDOS 0x3A Ddelete(\"%s\") at PC 0x%X\n", pDirName,
		  CallingPC);

	Drive = GemDOS_FileName2HardDriveID(pDirName);

	if (!ISHARDDRIVE(Drive))
	{
		/* redirect to TOS */
		return false;
	}

	/* write protected device? */
	if (ConfigureParams.HardDisk.nWriteProtection == WRITEPROT_ON)
	{
		Log_Printf(LOG_WARN, "PREVENTED: GEMDOS Ddelete(\"%s\")\n", pDirName);
		Regs[REG_D0] = GEMDOS_EWRPRO;
		return true;
	}

	psDirPath = malloc(FILENAME_MAX);
	if (!psDirPath)
	{
		perror("GemDOS_RmDir");
		Regs[REG_D0] = GEMDOS_ENSMEM;
		return true;
	}

	/* Copy old directory, as if calls fails keep this one */
	GemDOS_CreateHardDriveFileName(Drive, pDirName, psDirPath, FILENAME_MAX);

	/* Attempt to remove directory */
	if (rmdir(psDirPath) == 0)
		Regs[REG_D0] = GEMDOS_EOK;
	else
		Regs[REG_D0] = errno2gemdos(errno, ERROR_PATH);
	free(psDirPath);
	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS ChDir
 * Call 0x3B
 */
static bool GemDOS_ChDir(uint32_t Params)
{
	char *pDirName, *psTempDirPath;
	int Drive;
	uint32_t nStrAddr = STMemory_ReadLong(Params);

	pDirName = STMemory_GetStringPointer(nStrAddr);
	if (!pDirName)
	{
		LOG_TRACE(TRACE_OS_GEMDOS,
		          "GEMDOS 0x3B Dsetpath with illegal file name (0x%x) at PC 0x%X\n",
		          nStrAddr, CallingPC);
		Regs[REG_D0] = GEMDOS_EPTHNF;
		return true;
	}

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x3B Dsetpath(\"%s\") at PC 0x%X\n", pDirName,
		  CallingPC);

	Drive = GemDOS_FileName2HardDriveID(pDirName);
	if (!ISHARDDRIVE(Drive))
	{
		/* redirect to TOS */
		return false;
	}

	/* Empty string does nothing */
	if (*pDirName == '\0')
	{
		Regs[REG_D0] = GEMDOS_EOK;
		return true;
	}

	/* Allocate temporary memory for path name: */
	psTempDirPath = malloc(FILENAME_MAX);
	if (!psTempDirPath)
	{
		perror("GemDOS_ChDir");
		Regs[REG_D0] = GEMDOS_ENSMEM;
		return true;
	}

	GemDOS_CreateHardDriveFileName(Drive, pDirName, psTempDirPath, FILENAME_MAX);

	/* Remove trailing slashes (stat on Windows does not like that) */
	File_CleanFileName(psTempDirPath);

	if (access(psTempDirPath, F_OK) != 0)
	{
		/* error */
		free(psTempDirPath);
		Regs[REG_D0] = GEMDOS_EPTHNF;
		return true;
	}

	File_AddSlashToEndFileName(psTempDirPath);
	File_MakeAbsoluteName(psTempDirPath);

	/* Prevent '..' commands moving BELOW the root HDD folder */
	/* by double checking if path is valid */
	if (strncmp(psTempDirPath, emudrives[Drive-2]->hd_emulation_dir,
		    strlen(emudrives[Drive-2]->hd_emulation_dir)) == 0)
	{
		Str_Copy(emudrives[Drive-2]->fs_currpath, psTempDirPath,
		         sizeof(emudrives[Drive-2]->fs_currpath));
		Regs[REG_D0] = GEMDOS_EOK;
	}
	else
	{
		Regs[REG_D0] = GEMDOS_EPTHNF;
	}
	free(psTempDirPath);

	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * Helper to check whether given file's path is missing.
 * Returns true if missing, false if found.
 * Modifies the argument buffer.
 */
static bool GemDOS_FilePathMissing(char *szActualFileName)
{
	char *ptr = strrchr(szActualFileName, PATHSEP);
	if (ptr)
	{
		*ptr = 0;   /* Strip filename from string */
		if (!File_DirExists(szActualFileName))
			return true;
	}
	return false;
}


/*-----------------------------------------------------------------------*/
static inline bool redirect_to_TOS(void)
{
	LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE, "-> to TOS\n");
	return false;
}

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Create file
 * Call 0x3C
 */
static bool GemDOS_Create(uint32_t Params)
{
	/* TODO: host filenames might not fit into this */
	char szActualFileName[MAX_GEMDOS_PATH];
	char *pszFileName;
	int Drive, Index;
	uint32_t nStrAddr = STMemory_ReadLong(Params);
	int Mode = STMemory_ReadWord(Params + SIZE_LONG);

	pszFileName = STMemory_GetStringPointer(nStrAddr);
	if (!pszFileName || !pszFileName[0])
	{
		LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE,
		          "GEMDOS 0x3C bad Fcreate(0x%X, 0x%x) at PC 0x%X\n",
		          nStrAddr, Mode, CallingPC);
		return false;
	}

	LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE,
		  "GEMDOS 0x3C Fcreate(\"%s\", 0x%x) at PC 0x%X\n", pszFileName, Mode,
		  CallingPC);

	Drive = GemDOS_FileName2HardDriveID(pszFileName);

	if (!ISHARDDRIVE(Drive))
	{
		/* redirect to TOS */
		return redirect_to_TOS();
	}

	if (Mode == GEMDOS_FILE_ATTRIB_VOLUME_LABEL)
	{
		Log_Printf(LOG_WARN, "Warning: Hatari doesn't support GEMDOS volume"
			   " label setting\n(for '%s')\n", pszFileName);
		Regs[REG_D0] = GEMDOS_EFILNF;         /* File not found */
		return true;
	}

	/* write protected device? */
	if (ConfigureParams.HardDisk.nWriteProtection == WRITEPROT_ON)
	{
		Log_Printf(LOG_WARN, "PREVENTED: GEMDOS Fcreate(\"%s\")\n", pszFileName);
		Regs[REG_D0] = GEMDOS_EWRPRO;
		return true;
	}

	/* Now convert to hard drive filename */
	GemDOS_CreateHardDriveFileName(Drive, pszFileName,
	                            szActualFileName, sizeof(szActualFileName));

	/* Find slot to store file handle, as need to return WORD handle for ST */
	Index = GemDOS_FindFreeFileHandle();
	if (Index == -1)
	{
		/* No free handles, return error code */
		Regs[REG_D0] = GEMDOS_ENHNDL;       /* No more handles */
		return true;
	}
	
	/* truncate and open for reading & writing */
	FileHandles[Index].FileHandle = fopen(szActualFileName, "wb+");

	if (FileHandles[Index].FileHandle != NULL)
	{
		/* FIXME: implement other Mode attributes
		 * - GEMDOS_FILE_ATTRIB_HIDDEN       (FA_HIDDEN)
		 * - GEMDOS_FILE_ATTRIB_SYSTEM_FILE  (FA_SYSTEM)
		 * - GEMDOS_FILE_ATTRIB_SUBDIRECTORY (FA_DIR)
		 * - GEMDOS_FILE_ATTRIB_WRITECLOSE   (FA_ARCHIVE)
		 *   (set automatically by GemDOS >= 0.15)
		 */
		FileHandles[Index].bReadOnly = false;
		if (Mode & GEMDOS_FILE_ATTRIB_READONLY)
		{
			/* enable write warnings */
			FileHandles[Index].bReadOnly = true;
			/* after closing, file should be read-only */
			if (chmod(szActualFileName, S_IRUSR|S_IRGRP|S_IROTH))
			{
				perror("Failed to set file to read-only");
			}
		}
		/* Tag handle table entry as used in this process and return handle */
		FileHandles[Index].bUsed = true;
		strcpy(FileHandles[Index].szMode, "wb+");
		FileHandles[Index].Basepage = STMemory_ReadLong(act_pd);
		snprintf(FileHandles[Index].szActualName,
			 sizeof(FileHandles[Index].szActualName),
			 "%s", szActualFileName);

		/* Return valid ST file handle from our range (from BASE_FILEHANDLE upwards) */
		Regs[REG_D0] = Index+BASE_FILEHANDLE;
		LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE, "-> FD %d (%s)\n", Regs[REG_D0],
			  Mode & GEMDOS_FILE_ATTRIB_READONLY ? "read-only":"read/write");
		return true;
	}
	LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE, "-> ERROR (errno = %d)\n", errno);

	/* We failed to create the file, did we have required access rights? */
	if (errno == EACCES || errno == EROFS ||
	    errno == EPERM || errno == EISDIR)
	{
		Log_Printf(LOG_WARN, "GEMDOS failed to create/truncate '%s'\n",
			   szActualFileName);
		Regs[REG_D0] = GEMDOS_EACCDN;
		return true;
	}

	/* Or was path to file missing? (ST-Zip 2.6 relies on getting
	 * correct error about that during extraction of ZIP files.)
	 */
	if (errno == ENOTDIR || GemDOS_FilePathMissing(szActualFileName))
	{
		Regs[REG_D0] = GEMDOS_EPTHNF; /* Path not found */
		return true;
	}

	Regs[REG_D0] = GEMDOS_EFILNF;         /* File not found */
	return true;
}


/**
 * GEMDOS Open file
 * Call 0x3D
 */
static bool GemDOS_Open(uint32_t Params)
{
	/* TODO: host filenames might not fit into this */
	char szActualFileName[MAX_GEMDOS_PATH];
	char *pszFileName;
	const char *ModeStr, *RealMode;
	const char *Modes[] = {
		"read-only", "write-only", "read/write", "read/write"
	};
	int Drive, Index;
	FILE *OverrideHandle;
	bool bToTos = false;
	uint32_t nStrAddr = STMemory_ReadLong(Params);
	int Mode = STMemory_ReadWord(Params+SIZE_LONG) & 3;

	pszFileName = STMemory_GetStringPointer(nStrAddr);
	if (!pszFileName || !pszFileName[0])
	{
		LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE,
		          "GEMDOS 0x3D bad Fopen(0x%X, %s) at PC=0x%X\n",
		          nStrAddr, Modes[Mode], CallingPC);
		return false;
	}

	LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE,
		  "GEMDOS 0x3D Fopen(\"%s\", %s) at PC=0x%X\n",
		  pszFileName, Modes[Mode], CallingPC);

	Drive = GemDOS_FileName2HardDriveID(pszFileName);

	if (!ISHARDDRIVE(Drive))
	{
		if (INF_Overriding(AUTOSTART_FOPEN))
			bToTos = true;
		else
			return redirect_to_TOS();
	}

	/* Find slot to store file handle, as need to return WORD handle for ST  */
	Index = GemDOS_FindFreeFileHandle();
	if (Index == -1)
	{
		if (bToTos)
			return redirect_to_TOS();

		/* No free handles, return error code */
		Regs[REG_D0] = GEMDOS_ENHNDL;       /* No more handles */
		Log_Printf(LOG_WARN, "no free GEMDOS HD file handles for Fopen()");
		LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE, "-> ERROR %d\n", Regs[REG_D0]);
		return true;
	}

	if ((OverrideHandle = INF_OpenOverride(pszFileName)))
	{
		strcpy(szActualFileName, pszFileName);
		FileHandles[Index].FileHandle = OverrideHandle;
		FileHandles[Index].bReadOnly = true;
		RealMode = "read-only";
		ModeStr = "rb";
	}
	else
	{
		if (bToTos)
			return redirect_to_TOS();

		/* Convert to hard drive filename */
		GemDOS_CreateHardDriveFileName(Drive, pszFileName,
			szActualFileName, sizeof(szActualFileName));

		/* Fread/Fwrite calls succeed in all TOS versions
		 * regardless of access rights specified in Fopen().
		 * Only time when things can fail is when file is
		 * opened, if file mode doesn't allow given opening
		 * mode.  As there's no write-only file mode, access
		 * failures happen only when trying to open read-only
		 * file with (read+)write mode.
		 *
		 * Therefore only read-only & read+write modes need
		 * to be supported (ANSI-C fopen() doesn't even
		 * support write-only without truncating the file).
		 *
		 * Read-only status is used if:
		 * - Hatari write protection is enabled
		 * - File exists, but is not writable
		 * Latter is done to help cases where application
		 * needlessly requests write access, but file is
		 * on read-only media (like CD/DVD).
		 */
		if (ConfigureParams.HardDisk.nWriteProtection == WRITEPROT_ON ||
		    (access(szActualFileName, F_OK) == 0 &&
		     access(szActualFileName, W_OK) != 0))
		{
			ModeStr = "rb";
			RealMode = "read-only";
			FileHandles[Index].bReadOnly = true;
		}
		else
		{
			ModeStr = "rb+";
			RealMode = "read+write";
			FileHandles[Index].bReadOnly = (Mode == 0);
		}
		FileHandles[Index].FileHandle = fopen(szActualFileName, ModeStr);
	}

	if (FileHandles[Index].FileHandle != NULL)
	{
		/* Tag handle table entry as used in this process and return handle */
		FileHandles[Index].bUsed = true;
		strcpy(FileHandles[Index].szMode, ModeStr);
		FileHandles[Index].Basepage = STMemory_ReadLong(act_pd);
		snprintf(FileHandles[Index].szActualName,
			 sizeof(FileHandles[Index].szActualName),
			 "%s", szActualFileName);

		/* Return valid ST file handle from our range (BASE_FILEHANDLE upwards) */
		Regs[REG_D0] = Index+BASE_FILEHANDLE;
		LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE, "-> FD %d (%s -> %s)\n",
			  Regs[REG_D0], Modes[Mode], RealMode);
		return true;
	}

	if (errno == EACCES || errno == EROFS ||
	    errno == EPERM || errno == EISDIR)
	{
		Log_Printf(LOG_WARN, "GEMDOS missing %s permission to file '%s'\n",
			   Modes[Mode], szActualFileName);
		Regs[REG_D0] = GEMDOS_EACCDN;
	}
	else if (errno == ENOTDIR || GemDOS_FilePathMissing(szActualFileName))
	{
		/* Path not found */
		Regs[REG_D0] = GEMDOS_EPTHNF;
	}
	else
	{
		/* File not found / error opening */
		Regs[REG_D0] = GEMDOS_EFILNF;
	}
	LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE, "-> ERROR %d (errno = %d)\n", Regs[REG_D0], errno);
	return true;
}

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Close file
 * Call 0x3E
 */
static bool GemDOS_Close(uint32_t Params)
{
	int i, Handle;

	/* Find our handle - may belong to TOS */
	Handle = STMemory_ReadWord(Params);

	LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE,
		  "GEMDOS 0x3E Fclose(%i) at PC 0x%X\n",
		  Handle, CallingPC);

	/* Get internal handle */
	if ((Handle = GemDOS_GetValidFileHandle(Handle)) < 0)
	{
		/* no, assume it was TOS one -> redirect */
		return false;
	}
	
	/* Close file and free up handle table */
	if (INF_CloseOverride(FileHandles[Handle].FileHandle))
	{
		FileHandles[Handle].bUsed = false;
	}
	GemDOS_CloseFileHandle(Handle);

	/* unalias handle */
	for (i = 0; i < ARRAY_SIZE(ForcedHandles); i++)
	{
		if (ForcedHandles[i].Handle == Handle)
			GemDOS_UnforceFileHandle(i);
	}
	/* Return no error */
	Regs[REG_D0] = GEMDOS_EOK;
	return true;
}

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Read file
 * Call 0x3F
 */
static bool GemDOS_Read(uint32_t Params)
{
	char *pBuffer;
	off_t CurrentPos, FileSize;
	long nBytesRead, nBytesLeft;
	uint32_t Addr;
	uint32_t Size;
	int Handle;

	/* Read details from stack */
	Handle = STMemory_ReadWord(Params);
	Size = STMemory_ReadLong(Params+SIZE_WORD);
	Addr = STMemory_ReadLong(Params+SIZE_WORD+SIZE_LONG);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x3F Fread(%i, %i, 0x%x) at PC 0x%X\n",
	          Handle, Size, Addr,
		  CallingPC);

	/* Get internal handle */
	if ((Handle = GemDOS_GetValidFileHandle(Handle)) < 0)
	{
		/* assume it was TOS one -> redirect */
		return false;
	}

	/* Old TOS versions treat the Size parameter as signed */
	if (TosVersion < 0x400 && (Size & 0x80000000))
	{
		/* return -1 as original GEMDOS */
		Regs[REG_D0] = -1;
		return true;
	}
	
	/* To quick check to see where our file pointer is and how large the file is */
	CurrentPos = ftello(FileHandles[Handle].FileHandle);
	if (CurrentPos == -1L
	    || fseeko(FileHandles[Handle].FileHandle, 0, SEEK_END) != 0)
	{
		Regs[REG_D0] = GEMDOS_E_SEEK;
		return true;
	}
	FileSize = ftello(FileHandles[Handle].FileHandle);
	if (FileSize == -1L
	    || fseeko(FileHandles[Handle].FileHandle, CurrentPos, SEEK_SET) != 0)
	{
		Regs[REG_D0] = GEMDOS_E_SEEK;
		return true;
	}

	nBytesLeft = FileSize-CurrentPos;

	/* Check for bad size and End Of File */
	if (Size <= 0 || nBytesLeft <= 0)
	{
		/* return zero (bytes read) as original GEMDOS/EmuTOS */
		Regs[REG_D0] = 0;
		return true;
	}

	/* Limit to size of file to prevent errors */
	if (Size > (uint32_t)nBytesLeft)
		Size = nBytesLeft;

	/* Check that read is to valid memory area */
	if ( !STMemory_CheckAreaType ( Addr, Size, ABFLAG_RAM ) )
	{
		Log_Printf(LOG_WARN, "GEMDOS Fread() failed due to invalid RAM range 0x%x+%i\n", Addr, Size);
		Regs[REG_D0] = GEMDOS_ERANGE;
		return true;
	}

	/* Atari memory modified directly with fread() -> flush the instr/data caches */
	M68000_Flush_All_Caches(Addr, Size);

	/* And read data in */
	pBuffer = (char *)STMemory_STAddrToPointer(Addr);
	nBytesRead = fread(pBuffer, 1, Size, FileHandles[Handle].FileHandle);
	
	if (ferror(FileHandles[Handle].FileHandle))
	{
		int errnum = errno;
		Log_Printf(LOG_WARN, "GEMDOS failed to read from '%s': %s\n",
			   FileHandles[Handle].szActualName, strerror(errno));
		Regs[REG_D0] = errno2gemdos(errnum, ERROR_FILE);
		clearerr(FileHandles[Handle].FileHandle);
	}
	else
		/* Return number of bytes read */
		Regs[REG_D0] = nBytesRead;

	return true;
}

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Write file
 * Call 0x40
 */
static bool GemDOS_Write(uint32_t Params)
{
	char *pBuffer;
	long nBytesWritten;
	uint32_t Addr;
	int32_t Size;
	int Handle, fh_idx;
	FILE *fp;

	/* Read details from stack */
	Handle = STMemory_ReadWord(Params);
	Size = STMemory_ReadLong(Params+SIZE_WORD);
	Addr = STMemory_ReadLong(Params+SIZE_WORD+SIZE_LONG);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x40 Fwrite(%i, %i, 0x%x) at PC 0x%X\n",
	          Handle, Size, Addr,
		  CallingPC);

	/* Get internal handle */
	fh_idx = GemDOS_GetValidFileHandle(Handle);
	if (fh_idx >= 0)
	{
		/* write protected device? */
		if (ConfigureParams.HardDisk.nWriteProtection == WRITEPROT_ON)
		{
			Log_Printf(LOG_WARN, "PREVENTED: GEMDOS Fwrite(%d,...)\n", Handle);
			Regs[REG_D0] = GEMDOS_EWRPRO;
			return true;
		}
		fp = FileHandles[fh_idx].FileHandle;
	}
	else
	{
		if (!bUseTos && Handle == 1)
			fp = stdout;
		else if (!bUseTos && (Handle == 2 || Handle == -1))
			fp = stderr;
		else
			return false;	/* assume it was TOS one -> redirect */
	}

	/* Check that write is from valid memory area */
	if (!STMemory_CheckAreaType(Addr, Size, ABFLAG_RAM | ABFLAG_ROM))
	{
		Log_Printf(LOG_WARN, "GEMDOS Fwrite() failed due to invalid RAM range 0x%x+%i\n", Addr, Size);
		Regs[REG_D0] = GEMDOS_ERANGE;
		return true;
	}

	pBuffer = (char *)STMemory_STAddrToPointer(Addr);
	fseek(fp, 0, SEEK_CUR);
	nBytesWritten = fwrite(pBuffer, 1, Size, fp);
	if (fh_idx >= 0 && ferror(fp))
	{
		int errnum = errno;
		Log_Printf(LOG_WARN, "GEMDOS failed to write to '%s': %s\n",
			   FileHandles[fh_idx].szActualName, strerror(errno));
		Regs[REG_D0] = errno2gemdos(errnum, ERROR_FILE);
		clearerr(fp);
	}
	else
	{
		fflush(fp);
		Regs[REG_D0] = nBytesWritten;      /* OK */
	}
	if (fh_idx >= 0 && FileHandles[fh_idx].bReadOnly)
	{
		Log_Printf(LOG_WARN, "GEMDOS Fwrite() to a read-only file '%s'\n",
			   File_Basename(FileHandles[fh_idx].szActualName));
	}

	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Delete file
 * Call 0x41
 */
static bool GemDOS_FDelete(uint32_t Params)
{
	char *pszFileName, *psActualFileName;
	int Drive;
	uint32_t nStrAddr = STMemory_ReadLong(Params);

	pszFileName = STMemory_GetStringPointer(nStrAddr);
	if (!pszFileName || !pszFileName[0])
	{
		LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE, "GEMDOS 0x41 bad Fdelete(0x%X) at PC 0x%X\n",
		          nStrAddr, CallingPC);
		return false;
	}

	LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE, "GEMDOS 0x41 Fdelete(\"%s\") at PC 0x%X\n", pszFileName,
		  CallingPC);

	Drive = GemDOS_FileName2HardDriveID(pszFileName);

	if (!ISHARDDRIVE(Drive))
	{
		/* redirect to TOS */
		return false;
	}

	/* write protected device? */
	if (ConfigureParams.HardDisk.nWriteProtection == WRITEPROT_ON)
	{
		Log_Printf(LOG_WARN, "PREVENTED: GEMDOS Fdelete(\"%s\")\n", pszFileName);
		Regs[REG_D0] = GEMDOS_EWRPRO;
		return true;
	}

	psActualFileName = malloc(FILENAME_MAX);
	if (!psActualFileName)
	{
		perror("GemDOS_FDelete");
		Regs[REG_D0] = GEMDOS_ENSMEM;
		return true;
	}

	/* And convert to hard drive filename */
	GemDOS_CreateHardDriveFileName(Drive, pszFileName, psActualFileName, FILENAME_MAX);

	/* Now delete file?? */
	if (unlink(psActualFileName) == 0)
		Regs[REG_D0] = GEMDOS_EOK;          /* OK */
	else
		Regs[REG_D0] = errno2gemdos(errno, ERROR_FILE);

	free(psActualFileName);
	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS File seek
 * Call 0x42
 */
static bool GemDOS_LSeek(uint32_t Params)
{
	long Offset;
	int Handle, Mode;
	long nFileSize;
	long nOldPos, nDestPos;
	FILE *fhndl;

	/* Read details from stack */
	Offset = (int32_t)STMemory_ReadLong(Params);
	Handle = STMemory_ReadWord(Params+SIZE_LONG);
	Mode = STMemory_ReadWord(Params+SIZE_LONG+SIZE_WORD);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x42 Fseek(%li, %i, %i) at PC 0x%X\n", Offset, Handle, Mode,
		  CallingPC);

	/* get internal handle */
	if ((Handle = GemDOS_GetValidFileHandle(Handle)) < 0)
	{
		/* assume it was TOS one -> redirect */
		return false;
	}

	fhndl = FileHandles[Handle].FileHandle;

	/* Save old position in file */
	nOldPos = ftell(fhndl);

	/* Determine the size of the file */
	if (fseek(fhndl, 0L, SEEK_END) != 0 || nOldPos < 0)
	{
		Regs[REG_D0] = GEMDOS_E_SEEK;
		return true;
	}
	nFileSize = ftell(fhndl);

	switch (Mode)
	{
	 case 0: nDestPos = Offset; break; /* positive offset */
	 case 1: nDestPos = nOldPos + Offset; break;
	 case 2: nDestPos = nFileSize + Offset; break; /* negative offset */
	 default: nDestPos = -1;
	}

	if (nDestPos < 0 || nDestPos > nFileSize)
	{
		/* Restore old position and return error */
		if (fseek(fhndl, nOldPos, SEEK_SET) != 0)
			perror("GemDOS_LSeek");
		Regs[REG_D0] = GEMDOS_ERANGE;
		return true;
	}

	/* Seek to new position and return offset from start of file */
	if (fseek(fhndl, nDestPos, SEEK_SET) != 0)
		perror("GemDOS_LSeek");
	Regs[REG_D0] = ftell(fhndl);

	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Fattrib() - get or set file and directory attributes
 * Call 0x43
 */
static bool GemDOS_Fattrib(uint32_t Params)
{
	/* TODO: host filenames might not fit into this */
	char sActualFileName[MAX_GEMDOS_PATH];
	char *psFileName;
	int nDrive;
	struct stat FileStat;
	uint32_t nStrAddr = STMemory_ReadLong(Params);
	int nRwFlag = STMemory_ReadWord(Params + SIZE_LONG);
	int nAttrib = STMemory_ReadWord(Params + SIZE_LONG + SIZE_WORD);

	psFileName = STMemory_GetStringPointer(nStrAddr);
	if (!psFileName || !psFileName[0])
	{
		LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x43 bad Fattrib(0x%X, %d, 0x%x) at PC 0x%X\n",
		          nStrAddr, nRwFlag, nAttrib, CallingPC);
		return false;
	}

	nDrive = GemDOS_FileName2HardDriveID(psFileName);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x43 Fattrib(\"%s\", %d, 0x%x) at PC 0x%X\n",
	          psFileName, nRwFlag, nAttrib,
		  CallingPC);

	if (!ISHARDDRIVE(nDrive))
	{
		/* redirect to TOS */
		return false;
	}

	/* Convert to hard drive filename */
	GemDOS_CreateHardDriveFileName(nDrive, psFileName,
	                              sActualFileName, sizeof(sActualFileName));

	if (nAttrib == GEMDOS_FILE_ATTRIB_VOLUME_LABEL)
	{
		Log_Printf(LOG_WARN, "Hatari doesn't support GEMDOS volume label setting\n(for '%s')\n", sActualFileName);
		Regs[REG_D0] = GEMDOS_EFILNF;         /* File not found */
		return true;
	}
	if (stat(sActualFileName, &FileStat) != 0)
	{
		Regs[REG_D0] = GEMDOS_EFILNF;         /* File not found */
		return true;
	}
	if (nRwFlag == 0)
	{
		/* Read attributes */
		Regs[REG_D0] = GemDOS_ConvertAttribute(FileStat.st_mode, sActualFileName);
		return true;
	}

	/* prevent modifying access rights both on write & auto-protected devices */
	if (ConfigureParams.HardDisk.nWriteProtection != WRITEPROT_OFF)
	{
		Log_Printf(LOG_WARN, "PREVENTED: GEMDOS Fattrib(\"%s\",...)\n", psFileName);
		Regs[REG_D0] = GEMDOS_EWRPRO;
		return true;
	}

	if (nAttrib & GEMDOS_FILE_ATTRIB_SUBDIRECTORY)
	{
		if (!S_ISDIR(FileStat.st_mode))
		{
			/* file, not dir -> path not found */
			Regs[REG_D0] = GEMDOS_EPTHNF;
			return true;
		}
	}
	else
	{
		if (S_ISDIR(FileStat.st_mode))
		{
			/* dir, not file -> file not found */
			Regs[REG_D0] = GEMDOS_EFILNF;
			return true;
		}
	}

	if (nAttrib & GEMDOS_FILE_ATTRIB_READONLY)
	{
		/* set read-only (readable by all) */
		if (chmod(sActualFileName, S_IRUSR|S_IRGRP|S_IROTH) == 0)
		{
			Regs[REG_D0] = nAttrib;
			return true;
		}
	}
	else
	{
		/* set writable (by user, readable by all) */
		if (chmod(sActualFileName, S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH) == 0)
		{
			Regs[REG_D0] = nAttrib;
			return true;
		}
	}
	
	/* FIXME: support hidden/system/archive flags?
	 * System flag is from DOS, not used by TOS.
	 * Archive bit is cleared by backup programs
	 * and set whenever file is written to.
	 */

	Regs[REG_D0] = errno2gemdos(errno, (nAttrib & GEMDOS_FILE_ATTRIB_SUBDIRECTORY) ? ERROR_PATH : ERROR_FILE);
	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Force (file handle aliasing)
 * Call 0x46
 */
static bool GemDOS_Force(uint32_t Params)
{
	int std, own;

	/* Read details from stack */
	std = STMemory_ReadWord(Params);
        own = STMemory_ReadWord(Params+SIZE_WORD);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x46 Fforce(%d, %d) at PC 0x%X\n", std, own,
		  CallingPC);

	/* Get internal handle */
	if (std > own)
	{
		int tmp = std;
		std = own;
		own = tmp;
	}
	if ((own = GemDOS_GetValidFileHandle(own)) < 0)
	{
		/* assume it was TOS one -> let TOS handle it */
		return false;
	}
	if (std < 0 || std >= ARRAY_SIZE(ForcedHandles))
	{
		Log_Printf(LOG_WARN, "forcing of non-standard %d (> %d) handle ignored.\n", std, ARRAY_SIZE(ForcedHandles));
		return false;
	}
	/* mark given standard handle redirected by this process */
	ForcedHandles[std].Basepage = STMemory_ReadLong(act_pd);
	ForcedHandles[std].Handle = own;

	Regs[REG_D0] = GEMDOS_EOK;
	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Get Directory
 * Call 0x47
 */
static bool GemDOS_GetDir(uint32_t Params)
{
	uint32_t Address;
	uint16_t Drive;

	Address = STMemory_ReadLong(Params);
	Drive = STMemory_ReadWord(Params+SIZE_LONG);

	/* Note: Drive = 0 means current drive, 1 = A:, 2 = B:, 3 = C:, etc. */
	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x47 Dgetpath(0x%x, %i) at PC 0x%X\n", Address, (int)Drive,
		  CallingPC);
	if (Drive == 0)
		Drive = CurrentDrive;
	else
		Drive--;

	/* is it our drive? */
	if (GemDOS_IsDriveEmulated(Drive))
	{
		char path[MAX_GEMDOS_PATH];
		int i,len,c;

		*path = '\0';
		strncat(path,&emudrives[Drive-2]->fs_currpath[strlen(emudrives[Drive-2]->hd_emulation_dir)], sizeof(path)-1);

		// convert it to ST path (DOS)
		File_CleanFileName(path);
		if (path[0] == PATHSEP && path[1] == '\0')
		{
			/* Root directory is represented by empty string */
			path[0] = '\0';
		}
		len = strlen(path);
		/* Check that write is requested to valid memory area */
		if ( !STMemory_CheckAreaType ( Address, len, ABFLAG_RAM ) )
		{
			Log_Printf(LOG_WARN, "GEMDOS Dgetpath() failed due to invalid RAM range 0x%x+%i\n", Address, len);
			Regs[REG_D0] = GEMDOS_ERANGE;
			return true;
		}
		for (i = 0; i <= len; i++)
		{
			c = path[i];
			STMemory_WriteByte(Address+i, (c==PATHSEP ? '\\' : c) );
		}
		LOG_TRACE(TRACE_OS_GEMDOS, "-> '%s'\n", (char *)STMemory_STAddrToPointer(Address));

		Regs[REG_D0] = GEMDOS_EOK;          /* OK */

		return true;
	}
	/* redirect to TOS */
	return false;
}

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS PExec handler
 * Call 0x4B
 */
static int GemDOS_Pexec(uint32_t Params)
{
	int Drive, len;
	char *pszFileName;
	FILE *fh;
	char sFileName[FILENAME_MAX];
	uint8_t prgh[28];		/* Buffer for program header */
	uint32_t prgname, cmdline, env_string;
	uint16_t mode;

	/* Get Pexec parameters */
	mode = STMemory_ReadWord(Params);
	prgname = STMemory_ReadLong(Params + SIZE_WORD);
	cmdline = STMemory_ReadLong(Params + SIZE_WORD + SIZE_LONG);
	env_string = STMemory_ReadLong(Params + SIZE_WORD + SIZE_LONG + SIZE_LONG);

	if (LOG_TRACE_LEVEL(TRACE_OS_GEMDOS|TRACE_OS_BASE))
	{
		if (mode == 0 || mode == 3)
		{
			int cmdlen;
			char *str;
			const char *name, *cmd;
			name = STMemory_GetStringPointer(prgname);
			if (!name)
			{
				LOG_TRACE_PRINT("GEMDOS 0x4B bad Pexec(%i, 0x%X, ...) at PC 0x%X\n",
				                mode, prgname, CallingPC);
				return false;
			}
			cmd = (const char *)STMemory_STAddrToPointer(cmdline);
			cmdlen = *cmd++;
			str = Str_Alloc(cmdlen);
			memcpy(str, cmd, cmdlen);
			str[cmdlen] = '\0';
			LOG_TRACE_PRINT("GEMDOS 0x4B Pexec(%i, \"%s\", [%d]\"%s\", 0x%x) at PC 0x%X\n",
			                mode, name, cmdlen, str, env_string, CallingPC);
			free(str);
		}
		else
		{
			LOG_TRACE_PRINT("GEMDOS 0x4B Pexec(%i, 0x%x, 0x%x, 0x%x) at PC 0x%X\n",
			                mode, prgname, cmdline, env_string, CallingPC);
		}
	}

	/* We only have to intercept the "load" modes */
	if (mode != 0 && mode != 3)
		return false;

	pszFileName = STMemory_GetStringPointer(prgname);
	if (!pszFileName)
		return false;
	Drive = GemDOS_FileName2HardDriveID(pszFileName);

	/* Skip if it is not using our emulated drive */
	if (!ISHARDDRIVE(Drive))
		return false;

	GemDOS_CreateHardDriveFileName(Drive, pszFileName, sFileName, sizeof(sFileName));
	fh = fopen(sFileName, "rb");
	if (!fh)
	{
		Regs[REG_D0] = GEMDOS_EFILNF;
		return true;
	}
	len = fread(prgh, 1, sizeof(prgh), fh);
	fclose(fh);
	if (len != sizeof(prgh) || prgh[0] != 0x60 || prgh[1] != 0x1a
	    || prgh[2] & 0x80 || prgh[6] & 0x80 || prgh[10] & 0x80)
	{
		Regs[REG_D0] = GEMDOS_EPLFMT;
		return true;
	}
	Symbols_ChangeCurrentProgram(sFileName);

	/* Prepare stack to run "create basepage": */
	Regs[REG_A7] -= 16;
	STMemory_WriteWord(Regs[REG_A7], 0x4b);	/* Pexec number */
	STMemory_WriteWord(Regs[REG_A7] + 2, TosVersion >= 0x200 ? 7 : 5);
	STMemory_WriteLong(Regs[REG_A7] + 4, prgh[22] << 24 | prgh[23] << 16 
	                                     | prgh[24] << 8 | prgh[25]);
	STMemory_WriteLong(Regs[REG_A7] + 8, cmdline);
	STMemory_WriteLong(Regs[REG_A7] + 12, env_string);

	nSavedPexecParams = Params;

	return -1;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Search Next
 * Call 0x4F
 */
static bool GemDOS_SNext(void)
{
	struct dirent **temp;
	int ret;
	DTA *pDTA;
	uint32_t DTA_Gemdos;
	uint16_t Index;

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x4F Fsnext() at PC 0x%X\n" , CallingPC);

	/* Refresh pDTA pointer (from the current basepage) */
	DTA_Gemdos = STMemory_ReadLong(STMemory_ReadLong(act_pd) + BASEPAGE_OFFSET_DTA);

	if ( !STMemory_CheckAreaType ( DTA_Gemdos, sizeof(DTA), ABFLAG_RAM ) )
	{
		Log_Printf(LOG_WARN, "GEMDOS Fsnext() failed due to invalid DTA address 0x%x\n", DTA_Gemdos);
		Regs[REG_D0] = GEMDOS_EINTRN;    /* "internal error */
		return true;
	}
	pDTA = (DTA *)STMemory_STAddrToPointer(DTA_Gemdos);

	/* Was DTA ours or TOS? */
	if (do_get_mem_long(pDTA->magic) != DTA_MAGIC_NUMBER)
	{
		/* redirect to TOS */
		return false;
	}

	if (nAttrSFirst == GEMDOS_FILE_ATTRIB_VOLUME_LABEL)
	{
		/* Volume label was given already in Sfirst() */
		Regs[REG_D0] = GEMDOS_ENMFIL;
		return true;
	}

	/* Find index into our list of structures */
	Index = do_get_mem_word(pDTA->index);

	if (Index >= DTACount || !InternalDTAs[Index].bUsed)
	{
		/* Invalid handle, TOS returns ENMFIL
		 * (if Fsetdta() has been used by any process)
		 */
		Log_Printf(LOG_WARN, "GEMDOS Fsnext(): Invalid DTA\n");
		Regs[REG_D0] = GEMDOS_ENMFIL;
		return true;
	}

	temp = InternalDTAs[Index].found;
	do
	{
		if (InternalDTAs[Index].centry >= InternalDTAs[Index].nentries)
		{
			/* older TOS versions zero file name if there are no (further) matches */
			if (TosVersion < 0x0400)
				pDTA->dta_name[0] = 0;
			Regs[REG_D0] = GEMDOS_ENMFIL;    /* No more files */
			return true;
		}

		ret = PopulateDTA(InternalDTAs[Index].path,
				  temp[InternalDTAs[Index].centry++],
				  pDTA, DTA_Gemdos);
	} while (ret == DTA_SKIP);

	if (ret == DTA_ERR)
	{
		Log_Printf(LOG_WARN, "GEMDOS Fsnext(): Error setting DTA\n");
		Regs[REG_D0] = GEMDOS_EINTRN;
		return true;
	}

	Regs[REG_D0] = GEMDOS_EOK;
	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Find first file
 * Call 0x4E
 */
static bool GemDOS_SFirst(uint32_t Params)
{
	/* TODO: host filenames might not fit into this */
	char szActualFileName[MAX_GEMDOS_PATH];
	char *pszFileName;
	const char *dirmask;
	struct dirent **files;
	int Drive;
	DIR *fsdir;
	int i, j, count;
	DTA *pDTA;
	uint32_t DTA_Gemdos;
	uint16_t useidx;

	nAttrSFirst = STMemory_ReadWord(Params+SIZE_LONG);
	pszFileName = STMemory_GetStringPointer(STMemory_ReadLong(Params));
	if (!pszFileName)
	{
		LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x4E bad Fsfirst(0x%X, 0x%x) at PC 0x%X\n",
		          STMemory_ReadLong(Params), nAttrSFirst, CallingPC);
		return false;
	}

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x4E Fsfirst(\"%s\", 0x%x) at PC 0x%X\n", pszFileName, nAttrSFirst,
		  CallingPC);

	Drive = GemDOS_FileName2HardDriveID(pszFileName);
	if (!ISHARDDRIVE(Drive))
	{
		/* redirect to TOS */
		return false;
	}

	/* Convert to hard drive filename */
	GemDOS_CreateHardDriveFileName(Drive, pszFileName,
		                    szActualFileName, sizeof(szActualFileName));

	/* Refresh pDTA pointer (from the current basepage) */
	DTA_Gemdos = STMemory_ReadLong(STMemory_ReadLong(act_pd) + BASEPAGE_OFFSET_DTA);

	if ( !STMemory_CheckAreaType ( DTA_Gemdos, sizeof(DTA), ABFLAG_RAM ) )
	{
		Log_Printf(LOG_WARN, "GEMDOS Fsfirst() failed due to invalid DTA address 0x%x\n", DTA_Gemdos);
		Regs[REG_D0] = GEMDOS_EINTRN;    /* "internal error */
		return true;
	}

	/* Atari memory will be modified (below) directly with
	 * do_mem_* + strcpy() -> flush the data cache
	 */
	M68000_Flush_Data_Cache(DTA_Gemdos, sizeof(DTA));

	pDTA = (DTA *)STMemory_STAddrToPointer(DTA_Gemdos);

	/* re-use earlier Hatari DTA? */
	if (do_get_mem_long(pDTA->magic) == DTA_MAGIC_NUMBER)
	{
		useidx = do_get_mem_word(pDTA->index);
		if (useidx >= DTACount || InternalDTAs[useidx].addr != DTA_Gemdos)
			useidx = DTAIndex;
	}
	else
	{
		/* set our magic num on new Hatari DTA */
		do_put_mem_long(pDTA->magic, DTA_MAGIC_NUMBER);
		useidx = DTAIndex;
	}

	/* Populate DTA, set index for our use */
	do_put_mem_word(pDTA->index, useidx);

	if (InternalDTAs[useidx].bUsed == true)
	{
		ClearInternalDTA(useidx);
	}
	InternalDTAs[useidx].bUsed = true;
	InternalDTAs[useidx].addr = DTA_Gemdos;

	/* Were we looking for the volume label? */
	if (nAttrSFirst == GEMDOS_FILE_ATTRIB_VOLUME_LABEL)
	{
		/* Volume name */
		strcpy(pDTA->dta_name,"EMULATED.001");
		pDTA->dta_name[11] = '0' + Drive;
		Regs[REG_D0] = GEMDOS_EOK;          /* Got volume */
		return true;
	}

	/* open directory
	 * TODO: host path may not fit into InternalDTA
	 */
	fsfirst_dirname(szActualFileName, InternalDTAs[useidx].path);
	fsdir = opendir(InternalDTAs[useidx].path);

	if (fsdir == NULL)
	{
		Regs[REG_D0] = GEMDOS_EPTHNF;        /* Path not found */
		return true;
	}
	/* close directory */
	closedir(fsdir);

	count = scandir(InternalDTAs[useidx].path, &files, 0, alphasort);
	/* File (directory actually) not found */
	if (count < 0)
	{
		Regs[REG_D0] = GEMDOS_EFILNF;
		return true;
	}

	InternalDTAs[useidx].centry = 0;          /* current entry is 0 */
	dirmask = File_Basename(szActualFileName);/* directory mask part */
	InternalDTAs[useidx].found = files;       /* get files */

	/* count & copy the entries that match our mask and discard the rest */
	j = 0;
	for (i=0; i < count; i++)
	{
		char *d_name = files[i]->d_name;
		Str_DecomposedToPrecomposedUtf8(d_name, d_name);   /* for OSX */
		if (fsfirst_match(dirmask, d_name))
		{
			InternalDTAs[useidx].found[j] = files[i];
			j++;
		}
		else
		{
			free(files[i]);
			files[i] = NULL;
		}
	}
	InternalDTAs[useidx].nentries = j; /* set number of legal entries */

	/* No files of that match, return error code */
	if (j==0)
	{
		free(files);
		InternalDTAs[useidx].found = NULL;
		Regs[REG_D0] = GEMDOS_EFILNF;        /* File not found */
		return true;
	}

	/* Scan for first file (SNext uses no parameters) */
	GemDOS_SNext();

	/* increment DTA buffer index unless earlier one was reused */
	if (useidx != DTAIndex)
		return true;

	if (++DTAIndex >= DTACount)
	{
		if (DTACount < DTA_CACHE_MAX)
		{
			INTERNAL_DTA *pNewIntDTAs;
			/* increase DTA cache size */
			pNewIntDTAs = realloc(InternalDTAs, (DTACount + DTA_CACHE_INC) * sizeof(*InternalDTAs));
			if (pNewIntDTAs)
			{
				InternalDTAs = pNewIntDTAs;
				memset(InternalDTAs + DTACount, 0, DTA_CACHE_INC * sizeof(*InternalDTAs));
				DTACount += DTA_CACHE_INC;
			}
			else
			{
				Log_Printf(LOG_WARN, "Failed to alloc GEMDOS HD DTA entries, wrapping DTA index\n");
				DTAIndex = 0;
			}
		}
		else
		{
			Log_Printf(LOG_WARN, "Too many (%d) active GEMDOS HD DTA entries, wrapping DTA index\n", DTAIndex);
			DTAIndex = 0;
		}
	}
	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Rename
 * Call 0x56
 */
static bool GemDOS_Rename(uint32_t Params)
{
	char *pszNewFileName,*pszOldFileName;
	/* TODO: host filenames might not fit into this */
	char szNewActualFileName[MAX_GEMDOS_PATH];
	char szOldActualFileName[MAX_GEMDOS_PATH];
	int NewDrive, OldDrive;
	uint32_t nOldStrAddr = STMemory_ReadLong(Params + SIZE_WORD);
	uint32_t nNewStrAddr = STMemory_ReadLong(Params + SIZE_WORD + SIZE_LONG);

	/* Read details from stack, skip first (dummy) arg */
	pszOldFileName = STMemory_GetStringPointer(nOldStrAddr);
	pszNewFileName = STMemory_GetStringPointer(nNewStrAddr);
	if (!pszOldFileName || !pszOldFileName[0] || !pszNewFileName || !pszNewFileName[0])
	{
		LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x56 bad Frename(0x%X, 0x%X) at PC 0x%X\n",
		          nOldStrAddr, nNewStrAddr, CallingPC);
		return false;
	}

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x56 Frename(\"%s\", \"%s\") at PC 0x%X\n", pszOldFileName, pszNewFileName,
		  CallingPC);

	NewDrive = GemDOS_FileName2HardDriveID(pszNewFileName);
	OldDrive = GemDOS_FileName2HardDriveID(pszOldFileName);
	if (!(ISHARDDRIVE(NewDrive) && ISHARDDRIVE(OldDrive)))
	{
		/* redirect to TOS */
		return false;
	}

	/* write protected device? */
	if (ConfigureParams.HardDisk.nWriteProtection == WRITEPROT_ON)
	{
		Log_Printf(LOG_WARN, "PREVENTED: GEMDOS Frename(\"%s\", \"%s\")\n", pszOldFileName, pszNewFileName);
		Regs[REG_D0] = GEMDOS_EWRPRO;
		return true;
	}

	/* And convert to hard drive filenames */
	GemDOS_CreateHardDriveFileName(NewDrive, pszNewFileName,
		              szNewActualFileName, sizeof(szNewActualFileName));
	GemDOS_CreateHardDriveFileName(OldDrive, pszOldFileName,
		              szOldActualFileName, sizeof(szOldActualFileName));

	/* TOS allows renaming only when target does not exist */
	if (access(szOldActualFileName, F_OK) == 0 && access(szNewActualFileName, F_OK) == 0)
		Regs[REG_D0] = GEMDOS_EACCDN;
	else if (rename(szOldActualFileName, szNewActualFileName) == 0)
		Regs[REG_D0] = GEMDOS_EOK;
	else
		Regs[REG_D0] = errno2gemdos(errno, ERROR_FILE);
	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS GSDToF
 * Call 0x57
 */
static bool GemDOS_GSDToF(uint32_t Params)
{
	DATETIME DateTime;
	uint32_t pBuffer;
	int Handle,Flag;

	/* Read details from stack */
	pBuffer = STMemory_ReadLong(Params);
	Handle = STMemory_ReadWord(Params+SIZE_LONG);
	Flag = STMemory_ReadWord(Params+SIZE_LONG+SIZE_WORD);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x57 Fdatime(0x%x, %i, %i) at PC 0x%X\n", pBuffer,
	          Handle, Flag,
		  CallingPC);

	/* get internal handle */
	if ((Handle = GemDOS_GetValidFileHandle(Handle)) < 0)
	{
		/* No, assume was TOS -> redirect */
		return false;
	}

	if (Flag == 1)
	{
		/* write protected device? */
		if (ConfigureParams.HardDisk.nWriteProtection == WRITEPROT_ON)
		{
			Log_Printf(LOG_WARN, "PREVENTED: GEMDOS Fdatime(,%d,)\n", Handle);
			Regs[REG_D0] = GEMDOS_EWRPRO;
			return true;
		}
		DateTime.timeword = STMemory_ReadWord(pBuffer);
		DateTime.dateword = STMemory_ReadWord(pBuffer+SIZE_WORD);
		if (GemDOS_SetFileInformation(Handle, &DateTime) == true)
			Regs[REG_D0] = GEMDOS_EOK;
		else
			Regs[REG_D0] = GEMDOS_EACCDN;        /* Access denied */
		return true;
	}

	if (GemDOS_GetFileInformation(Handle, &DateTime) == true)
	{
		/* Check that write is requested to valid memory area */
		if ( STMemory_CheckAreaType ( pBuffer, 4, ABFLAG_RAM ) )
		{
			STMemory_WriteWord(pBuffer, DateTime.timeword);
			STMemory_WriteWord(pBuffer+SIZE_WORD, DateTime.dateword);
			Regs[REG_D0] = GEMDOS_EOK;
		}
		else
		{
			Log_Printf(LOG_WARN, "GEMDOS Fdatime() failed due to invalid RAM range 0x%x+%i\n", pBuffer, 4);
			Regs[REG_D0] = GEMDOS_ERANGE;
		}
	}
	else
	{
		Regs[REG_D0] = GEMDOS_ERROR; /* Generic error */
	}
	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * Do implicit file handle closing/unforcing on program termination
 */
static void GemDOS_TerminateClose(void)
{
	int i, closed, unforced;
	uint32_t current = STMemory_ReadLong(act_pd);

	closed = 0;
	for (i = 0; i < ARRAY_SIZE(FileHandles); i++)
	{
		if (FileHandles[i].Basepage == current)
		{
			GemDOS_CloseFileHandle(i);
			closed++;
		}
	}
	unforced = 0;
	for (i = 0; i < ARRAY_SIZE(ForcedHandles); i++)
	{
		if (ForcedHandles[i].Basepage == current)
		{
			GemDOS_UnforceFileHandle(i);
			unforced++;
		}
	}
	if (!(closed || unforced))
		return;
	Log_Printf(LOG_WARN, "Closing %d & unforcing %d file handle(s) remaining at program 0x%x exit.\n",
		   closed, unforced, current);
}

/**
 * GEMDOS Pterm0
 * Call 0x00
 */
static bool GemDOS_Pterm0(uint32_t Params)
{
	LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE, "GEMDOS 0x00 Pterm0() at PC 0x%X\n",
		  CallingPC);
	GemDOS_TerminateClose();
	Symbols_RemoveCurrentProgram();

	if (!bUseTos)
	{
		Main_SetQuitValue(0);
		return true;
	}

	return false;
}

/**
 * GEMDOS Ptermres
 * Call 0x31
 */
static bool GemDOS_Ptermres(uint32_t Params)
{
	LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE, "GEMDOS 0x31 Ptermres(0x%X, %hd) at PC 0x%X\n",
		  STMemory_ReadLong(Params), (int16_t)STMemory_ReadWord(Params+SIZE_WORD),
		  CallingPC);
	GemDOS_TerminateClose();
	return false;
}

/**
 * GEMDOS Pterm
 * Call 0x4c
 */
static bool GemDOS_Pterm(uint32_t Params)
{
	uint16_t nExitVal = STMemory_ReadWord(Params);

	LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE, "GEMDOS 0x4C Pterm(%hd) at PC 0x%X\n",
		  nExitVal, CallingPC);

	GemDOS_TerminateClose();
	Symbols_RemoveCurrentProgram();

	if (!bUseTos)
	{
		Main_SetQuitValue(nExitVal);
		return true;
	}

	return false;
}

/**
 * GEMDOS Super
 * Call 0x20
 */
static bool GemDOS_Super(uint32_t Params)
{
	uint32_t nParam = STMemory_ReadLong(Params);
	uint32_t nExcFrameSize, nRetAddr;
	uint16_t nSR, nVec = 0;

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x20 Super(0x%X) at PC 0x%X\n",
		  nParam, CallingPC);

	/* This call is normally fully handled by TOS - we only
	 * need to emulate it for TOS-less testing mode */
	if (bUseTos)
		return false;

	/* Get SR, return address and vector offset from stack frame */
	nSR = STMemory_ReadWord(Regs[REG_A7]);
	nRetAddr = STMemory_ReadLong(Regs[REG_A7] + SIZE_WORD);
	if (currprefs.cpu_model > 68000)
		nVec = STMemory_ReadWord(Regs[REG_A7] + SIZE_WORD + SIZE_LONG);

	if (nParam == 1)                /* Query mode? */
	{
		Regs[REG_D0] = (nSR & SR_SUPERMODE) ? -1 : 0;
		return true;
	}

	if (nParam == 0)
	{
		nParam = regs.usp;
	}

	if (currprefs.cpu_model > 68000)
		nExcFrameSize = SIZE_WORD + SIZE_LONG + SIZE_WORD;
	else
		nExcFrameSize = SIZE_WORD + SIZE_LONG;

	Regs[REG_D0] = Regs[REG_A7] + nExcFrameSize;
	Regs[REG_A7] = nParam - nExcFrameSize;

	nSR ^= SR_SUPERMODE;

	STMemory_WriteWord(Regs[REG_A7], nSR);
	STMemory_WriteLong(Regs[REG_A7] + SIZE_WORD, nRetAddr);
	STMemory_WriteWord(Regs[REG_A7] + SIZE_WORD + SIZE_LONG, nVec);

	return true;
}


/**
 * Map GEMDOS call opcodes to their names
 * 
 * Mapping is based on TOSHYP information:
 *	http://toshyp.atari.org/en/005013.html
 */
static const char* GemDOS_Opcode2Name(uint16_t opcode)
{
	static const char* names[] = {
		"Pterm0",
		"Cconin",
		"Cconout",
		"Cauxin",
		"Cauxout",
		"Cprnout",
		"Crawio",
		"Crawcin",
		"Cnecin",
		"Cconws",
		"Cconrs",
		"Cconis",
		"-", /* 0C */
		"-", /* 0D */
		"Dsetdrv",
		"-", /* 0F */
		"Cconos",
		"Cprnos",
		"Cauxis",
		"Cauxos",
		"Maddalt",
		"Srealloc", /* TOS4 */
		"-", /* 16 */
		"-", /* 17 */
		"-", /* 18 */
		"Dgetdrv",
		"Fsetdta",
		"-", /* 1B */
		"-", /* 1C */
		"-", /* 1D */
		"-", /* 1E */
		"-", /* 1F */
		"Super",
		"-", /* 21 */
		"-", /* 22 */
		"-", /* 23 */
		"-", /* 24 */
		"-", /* 25 */
		"-", /* 26 */
		"-", /* 27 */
		"-", /* 28 */
		"-", /* 29 */
		"Tgetdate",
		"Tsetdate",
		"Tgettime",
		"Tsettime",
		"-", /* 2E */
		"Fgetdta",
		"Sversion",
		"Ptermres",
		"-", /* 32 */
		"-", /* 33 */
		"-", /* 34 */
		"-", /* 35 */
		"Dfree",
		"-", /* 37 */
		"-", /* 38 */
		"Dcreate",
		"Ddelete",
		"Dsetpath",
		"Fcreate",
		"Fopen",
		"Fclose",
		"Fread",
		"Fwrite",
		"Fdelete",
		"Fseek",
		"Fattrib",
		"Mxalloc",
		"Fdup",
		"Fforce",
		"Dgetpath",
		"Malloc",
		"Mfree",
		"Mshrink",
		"Pexec",
		"Pterm",
		"-", /* 4D */
		"Fsfirst",
		"Fsnext",
		"-", /* 50 */
		"-", /* 51 */
		"-", /* 52 */
		"-", /* 53 */
		"-", /* 54 */
		"-", /* 55 */
		"Frename",
		"Fdatime",
		"-", /* 58 */
		"-", /* 59 */
		"-", /* 5A */
		"-", /* 5B */
		"Flock", /* 5C */
		"-", /* 5D */
		"-", /* 5E */
		"-", /* 5F */
		"Nversion", /* 60 */
		"-", /* 61 */
		"-", /* 62 */
		"-", /* 63 */
		"-", /* 64 */
		"-", /* 65 */
		"-", /* 66 */
		"-", /* 67 */
		"-", /* 68 */
		"-", /* 69 */
		"-", /* 6A */
		"-", /* 6B */
		"-", /* 6C */
		"-", /* 6D */
		"-", /* 6E */
		"-", /* 6F */
		"-", /* 70 */
		"-", /* 71 */
		"-", /* 72 */
		"-", /* 73 */
		"-", /* 74 */
		"-", /* 75 */
		"-", /* 76 */
		"-", /* 77 */
		"-", /* 78 */
		"-", /* 79 */
		"-", /* 7A */
		"-", /* 7B */
		"-", /* 7C */
		"-", /* 7D */
		"-", /* 7E */
		"-", /* 7F */
		"-", /* 80 */
		"-", /* 81 */
		"-", /* 82 */
		"-", /* 83 */
		"-", /* 84 */
		"-", /* 85 */
		"-", /* 86 */
		"-", /* 87 */
		"-", /* 88 */
		"-", /* 89 */
		"-", /* 8A */
		"-", /* 8B */
		"-", /* 8C */
		"-", /* 8D */
		"-", /* 8E */
		"-", /* 8F */
		"-", /* 90 */
		"-", /* 91 */
		"-", /* 92 */
		"-", /* 93 */
		"-", /* 94 */
		"-", /* 95 */
		"-", /* 96 */
		"-", /* 97 */
		"-", /* 98 */
		"-", /* 99 */
		"-", /* 9A */
		"-", /* 9B */
		"-", /* 9C */
		"-", /* 9D */
		"-", /* 9E */
		"-", /* 9F */
		"-", /* A0 */
		"-", /* A1 */
		"-", /* A2 */
		"-", /* A3 */
		"-", /* A4 */
		"-", /* A5 */
		"-", /* A6 */
		"-", /* A7 */
		"-", /* A8 */
		"-", /* A9 */
		"-", /* AA */
		"-", /* AB */
		"-", /* AC */
		"-", /* AD */
		"-", /* AE */
		"-", /* AF */
		"-", /* B0 */
		"-", /* B1 */
		"-", /* B2 */
		"-", /* B3 */
		"-", /* B4 */
		"-", /* B5 */
		"-", /* B6 */
		"-", /* B7 */
		"-", /* B8 */
		"-", /* B9 */
		"-", /* BA */
		"-", /* BB */
		"-", /* BC */
		"-", /* BD */
		"-", /* BE */
		"-", /* BF */
		"-", /* C0 */
		"-", /* C1 */
		"-", /* C2 */
		"-", /* C3 */
		"-", /* C4 */
		"-", /* C5 */
		"-", /* C6 */
		"-", /* C7 */
		"-", /* C8 */
		"-", /* C9 */
		"-", /* CA */
		"-", /* CB */
		"-", /* CC */
		"-", /* CD */
		"-", /* CE */
		"-", /* CF */
		"-", /* D0 */
		"-", /* D1 */
		"-", /* D2 */
		"-", /* D3 */
		"-", /* D4 */
		"-", /* D5 */
		"-", /* D6 */
		"-", /* D7 */
		"-", /* D8 */
		"-", /* D9 */
		"-", /* DA */
		"-", /* DB */
		"-", /* DC */
		"-", /* DD */
		"-", /* DE */
		"-", /* DF */
		"-", /* E0 */
		"-", /* E1 */
		"-", /* E2 */
		"-", /* E3 */
		"-", /* E4 */
		"-", /* E5 */
		"-", /* E6 */
		"-", /* E7 */
		"-", /* E8 */
		"-", /* E9 */
		"-", /* EA */
		"-", /* EB */
		"-", /* EC */
		"-", /* ED */
		"-", /* EE */
		"-", /* EF */
		"-", /* F0 */
		"-", /* F1 */
		"-", /* F2 */
		"-", /* F3 */
		"-", /* F4 */
		"-", /* F5 */
		"-", /* F6 */
		"-", /* F7 */
		"-", /* F8 */
		"-", /* F9 */
		"-", /* FA */
		"-", /* FB */
		"-", /* FC */
		"-", /* FD */
		"-", /* FE */
		"Syield", /* FF */
		"Fpipe", /* 100 */
		"Ffchown", /* 101 */
		"Ffchmod", /* 102 */
		"Fsync", /* 103 */
		"Fcntl", /* 104 */
		"Finstat", /* 105 */
		"Foutstat", /* 106 */
		"Fgetchar", /* 107 */
		"Fputchar", /* 108 */
		"Pwait", /* 109 */
		"Pnice", /* 10A */
		"Pgetpid", /* 10B */
		"Pgetppid", /* 10C */
		"Pgetpgrp", /* 10D */
		"Psetpgrp", /* 10E */
		"Pgetuid", /* 10F */
		"Psetuid", /* 110 */
		"Pkill", /* 111 */
		"Psignal", /* 112 */
		"Pvfork", /* 113 */
		"Pgetgid", /* 114 */
		"Psetgid", /* 115 */
		"Psigblock", /* 116 */
		"Psigsetmask", /* 117 */
		"Pusrval", /* 118 */
		"Pdomain", /* 119 */
		"Psigreturn", /* 11A */
		"Pfork", /* 11B */
		"Pwait3", /* 11C */
		"Fselect", /* 11D */
		"Prusage", /* 11E */
		"Psetlimit", /* 11F */
		"Talarm", /* 120 */
		"Pause", /* 121 */
		"Sysconf", /* 122 */
		"Psigpending", /* 123 */
		"Dpathconf", /* 124 */
		"Pmsg", /* 125 */
		"Fmidipipe", /* 126 */
		"Prenice", /* 127 */
		"Dopendir", /* 128 */
		"Dreaddir", /* 129 */
		"Drewinddir", /* 12A */
		"Dclosedir", /* 12B */
		"Fxattr", /* 12C */
		"Flink", /* 12D */
		"Fsymlink", /* 12E */
		"Freadlink", /* 12F */
		"Dcntl", /* 130 */
		"Fchown", /* 131 */
		"Fchmod", /* 132 */
		"Pumask", /* 133 */
		"Psemaphore", /* 134 */
		"Dlock", /* 135 */
		"Psigpause", /* 136 */
		"Psigaction", /* 137 */
		"Pgeteuid", /* 138 */
		"Pgetegid", /* 139 */
		"Pwaitpid", /* 13A */
		"Dgetcwd", /* 13B */
		"Salert", /* 13C */
		"Tmalarm", /* 13D */
		"Psigintr", /* 13E */
		"Suptime", /* 13F */
		"Ptrace", /* 140 */
		"Mvalidate", /* 141 */
		"Dxreaddir", /* 142 */
		"Pseteuid", /* 143 */
		"Psetegid", /* 144 */
		"Pgetauid", /* 145 */
		"Psetauid", /* 146 */
		"Pgetgroups", /* 147 */
		"Psetgroups", /* 148 */
		"Tsetitimer", /* 149 */
		"Dchroot", /* 14A; was Scookie */
		"Fstat64", /* 14B */
		"Fseek64", /* 14C */
		"Dsetkey", /* 14D */
		"Psetreuid", /* 14E */
		"Psetregid", /* 14F */
		"Sync", /* 150 */
		"Shutdown", /* 151 */
		"Dreadlabel", /* 152 */
		"Dwritelabel", /* 153 */
		"Ssystem", /* 154 */
		"Tgettimeofday", /* 155 */
		"Tsettimeofday", /* 156 */
		"Tadjtime", /* 157 */
		"Pgetpriority", /* 158 */
		"Psetpriority", /* 159 */
		"Fpoll", /* 15A */
		"Fwritev", /* 15B */
		"Freadv", /* 15C */
		"Ffstat64", /* 15D */
		"Psysctl", /* 15E */
		"Semulation", /* 15F */
		"Fsocket", /* 160 */
		"Fsocketpair", /* 161 */
		"Faccept", /* 162 */
		"Fconnect", /* 163 */
		"Fbind", /* 164 */
		"Flisten", /* 165 */
		"Frecvmsg", /* 166 */
		"Fsendmsg", /* 167 */
		"Frecvfrom", /* 168 */
		"Fsendto", /* 169 */
		"Fsetsockopt", /* 16A */
		"Fgetsockopt", /* 16B */
		"Fgetpeername", /* 16C */
		"Fgetsockname", /* 16D */
		"Fshutdown", /* 16E */
		"-", /* 16F */
		"Pshmget", /* 170 */
		"Pshmctl", /* 171 */
		"Pshmat", /* 172 */
		"Pshmdt", /* 173 */
		"Psemget", /* 174 */
		"Psemctl", /* 175 */
		"Psemop", /* 176 */
		"Psemconfig", /* 177 */
		"Pmsgget", /* 178 */
		"Pmsgctl", /* 179 */
		"Pmsgsnd", /* 17A */
		"Pmsgrcv", /* 17B */
		"-", /* 17C */
		"Maccess", /* 17D */
		"-", /* 17E */
		"-", /* 17F */
		"Fchown16", /* 180 */
		"Fchdir", /* 181 */
		"Ffdopendir", /* 182 */
		"Fdirfd" /* 183 */
	};

	if (opcode < ARRAY_SIZE(names))
		return names[opcode];
	return "-";
}


/**
 * If bShowOpcodes is true, show GEMDOS call opcode/function name table,
 * otherwise GEMDOS HDD emulation information.
 */
void GemDOS_Info(FILE *fp, uint32_t bShowOpcodes)
{
	int i, used;

	if (bShowOpcodes)
	{
		uint16_t opcode;
		/* list just normal TOS GEMDOS calls
		 *
		 * MiNT ones would need separate table as their names
		 * are much longer and 0x60 - 0xFE range is unused.
		 */
		for (opcode = 0; opcode < 0x5A; )
		{
			fprintf(fp, "%02x %-9s",
				opcode, GemDOS_Opcode2Name(opcode));
			if (++opcode % 6 == 0)
				fputs("\n", fp);
		}
		return;
	}

	if (!GEMDOS_EMU_ON)
	{
		fputs("GEMDOS HDD emulation isn't enabled!\n", fp);
		return;
	}

	/* GEMDOS vector (can be overwritten e.g. MiNT) */
	fprintf(fp, "Current GEMDOS handler: (0x84) = 0x%x\n", STMemory_ReadLong(0x0084));

	fprintf(fp, "Connected drives mask: 0x%x\n\n", ConnectedDriveMask);
	fputs("GEMDOS HDD emulation drives:\n", fp);
	for(i = 0; i < MAX_HARDDRIVES; i++)
	{
		if (!emudrives[i])
			continue;
		fprintf(fp, "- %c: %s\n  curpath: %s\n",
			'A' + emudrives[i]->drive_number,
			emudrives[i]->hd_emulation_dir,
			emudrives[i]->fs_currpath);
	}

	fputs("\nInternal Fsfirst() DTAs:\n", fp);
	for(used = i = 0; i < DTACount; i++)
	{
		int j, centry, entries;

		if (!InternalDTAs[i].bUsed)
			continue;

		fprintf(fp, "+ %d: %s\n", i, InternalDTAs[i].path);
		
		centry = InternalDTAs[i].centry;
		entries = InternalDTAs[i].nentries;
		for (j = 0; j < entries; j++)
		{
			fprintf(fp, "  - %d: %s%s\n",
				j, InternalDTAs[i].found[j]->d_name,
				j == centry ? " *" : "");
		}
		fprintf(fp, "  Fsnext entry = %d.\n", centry);
		used++;
	}
	if (!used)
		fputs("- None in use.\n", fp);

	fputs("\nOpen GEMDOS HDD file handles:\n", fp);
	for (used = i = 0; i < ARRAY_SIZE(FileHandles); i++)
	{
		if (!FileHandles[i].bUsed)
			continue;
		fprintf(fp, "- %d (0x%x): %s (%s)\n", i + BASE_FILEHANDLE,
			FileHandles[i].Basepage, FileHandles[i].szActualName,
			FileHandles[i].bReadOnly ? "ro" : "rw");
		used++;
	}
	if (!used)
		fputs("- None.\n", fp);
	fputs("\nForced GEMDOS HDD file handles:\n", fp);
	for (used = i = 0; i < ARRAY_SIZE(ForcedHandles); i++)
	{
		if (ForcedHandles[i].Handle == UNFORCED_HANDLE)
			continue;
		fprintf(fp, "- %d -> %d (0x%x)\n", i,
			ForcedHandles[i].Handle + BASE_FILEHANDLE,
			ForcedHandles[i].Basepage);
		used++;
	}
	if (!used)
		fputs("- None.\n", fp);

	fputs("\n", fp);
	Symbols_ShowCurrentProgramPath(fp);
}

/**
 * Show given DTA info
 * (works also without GEMDOS HD emu)
 */
void GemDOS_InfoDTA(FILE *fp, uint32_t dta_addr)
{
	DTA *dta;
	uint32_t magic;
	char name[TOS_NAMELEN+1];

	fprintf(fp, "DTA (0x%x):\n", dta_addr);
	if (act_pd)
	{
		uint32_t basepage = STMemory_ReadLong(act_pd);
		uint32_t dta_curr = STMemory_ReadLong(basepage + BASEPAGE_OFFSET_DTA);
		if (dta_addr != dta_curr)
		{
			fprintf(fp, "- NOTE: given DTA (0x%x) is not current program one (0x%x)\n",
				dta_addr, dta_curr);
		}
		if (dta_addr >= basepage && dta_addr + sizeof(DTA) < basepage + BASEPAGE_SIZE)
		{
			const char *msg = (dta_addr == basepage + 0x80) ? ", replacing command line" : "";
			fprintf(fp, "- NOTE: DTA (0x%x) is within current program basepage (0x%x)%s!\n",
				dta_addr, basepage, msg);
		}
	}
	if (!STMemory_CheckAreaType(dta_addr, sizeof(DTA), ABFLAG_RAM)) {
		fprintf(fp, "- ERROR: invalid memory address!\n");
		return;
	}
	dta = (DTA *)STMemory_STAddrToPointer(dta_addr);
	memcpy(name, dta->dta_name, TOS_NAMELEN);
	name[TOS_NAMELEN] = '\0';
	magic = do_get_mem_long(dta->magic);
	fprintf(fp, "- magic: 0x%08x (GEMDOS HD = 0x%08x)\n", magic, DTA_MAGIC_NUMBER);
	if (magic == DTA_MAGIC_NUMBER)
		fprintf(fp, "- index: 0x%04x\n", do_get_mem_word(dta->index));
	fprintf(fp, "- attr: 0x%x\n", dta->dta_attrib);
	fprintf(fp, "- time: 0x%04x\n", do_get_mem_word(dta->dta_time));
	fprintf(fp, "- date: 0x%04x\n", do_get_mem_word(dta->dta_date));
	fprintf(fp, "- size: %d\n", do_get_mem_long(dta->dta_size));
	fprintf(fp, "- name: '%s'\n", name);
}


/**
 * Run GEMDos call, and re-direct if need to. Used to handle hard disk emulation etc...
 * This sets the condition codes (in SR), which are used in the 'cart_asm.s' program to
 * decide if we need to run old GEM vector, or PExec or nothing.
 *
 * This method keeps the stack and other states consistent with the original ST
 * which is very important for the PExec call and maximum compatibility through-out
 */
int GemDOS_Trap(void)
{
	uint16_t GemDOSCall, CallingSReg;
	uint32_t Params;
	int Finished = false;
	uint16_t sr = M68000_GetSR();

	/* Read SReg from stack to see if parameters are on User or Super stack  */
	CallingSReg = STMemory_ReadWord(Regs[REG_A7]);
	CallingPC = STMemory_ReadLong(Regs[REG_A7] + SIZE_WORD);
	if (!(CallingSReg & SR_SUPERMODE))      /* Calling from user mode */
		Params = regs.usp;
	else
 	{
		Params = Regs[REG_A7] + SIZE_WORD + SIZE_LONG;  /* skip SR & PC pushed to super stack */
		if (currprefs.cpu_model > 68000)
			Params += SIZE_WORD;   /* Skip extra word if CPU is >=68010 */
	}

	/* Find pointer to call parameters */
	GemDOSCall = STMemory_ReadWord(Params);
	Params += SIZE_WORD;

	sr &= ~SR_OVERFLOW;

	/* Intercept call */
	switch(GemDOSCall)
	{
	 case 0x00:
		Finished = GemDOS_Pterm0(Params);
		break;
	 case 0x09:
		Finished = GemDOS_Cconws(Params);
		break;
	 case 0x0e:
		Finished = GemDOS_SetDrv(Params);
		break;
	 case 0x20:
		Finished = GemDOS_Super(Params);
		break;
	 case 0x31:
		Finished = GemDOS_Ptermres(Params);
		break;
	 case 0x36:
		Finished = GemDOS_DFree(Params);
		break;
	 case 0x39:
		Finished = GemDOS_MkDir(Params);
		break;
	 case 0x3a:
		Finished = GemDOS_RmDir(Params);
		break;
	 case 0x3b:	/* Dsetpath */
		Finished = GemDOS_ChDir(Params);
		break;
	 case 0x3c:
		Finished = GemDOS_Create(Params);
		break;
	 case 0x3d:
		Finished = GemDOS_Open(Params);
		break;
	 case 0x3e:
		Finished = GemDOS_Close(Params);
		break;
	 case 0x3f:
		Finished = GemDOS_Read(Params);
		break;
	 case 0x40:
		Finished = GemDOS_Write(Params);
		break;
	 case 0x41:
		Finished = GemDOS_FDelete(Params);
		break;
	 case 0x42:
		Finished = GemDOS_LSeek(Params);
		break;
	 case 0x43:
		Finished = GemDOS_Fattrib(Params);
		break;
	 case 0x46:
		Finished = GemDOS_Force(Params);
		break;
	 case 0x47:	/* Dgetpath */
		Finished = GemDOS_GetDir(Params);
		break;
	 case 0x4b:
		Finished = GemDOS_Pexec(Params);
		if (Finished == -1)
		{
			sr |= SR_OVERFLOW;
			Finished = true;
		}
		break;
	 case 0x4c:
		Finished = GemDOS_Pterm(Params);
		break;
	 case 0x4e:
		Finished = GemDOS_SFirst(Params);
		break;
	 case 0x4f:
		Finished = GemDOS_SNext();
		break;
	 case 0x56:
		Finished = GemDOS_Rename(Params);
		break;
	 case 0x57:
		Finished = GemDOS_GSDToF(Params);
		break;

	/* print args for other calls */

	case 0x01:	/* Conin */
	case 0x03:	/* Cauxin */
	case 0x12:	/* Cauxis */
	case 0x13:	/* Cauxos */
	case 0x0B:	/* Conis */
	case 0x10:	/* Conos */
	case 0x08:	/* Cnecin */
	case 0x11:	/* Cprnos */
	case 0x07:	/* Crawcin */
	case 0x19:	/* Dgetdrv */
	case 0x2F:	/* Fgetdta */
	case 0x30:	/* Sversion */
	case 0x2A:	/* Tgetdate */
	case 0x2C:	/* Tgettime */
		/* commands with no args */
		LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x%02hX %s() at PC 0x%X\n",
			  GemDOSCall, GemDOS_Opcode2Name(GemDOSCall),
			  CallingPC);
		break;
		
	case 0x02:	/* Cconout */
	case 0x04:	/* Cauxout */
	case 0x05:	/* Cprnout */
	case 0x06:	/* Crawio */
	case 0x2b:	/* Tsetdate */
	case 0x2d:	/* Tsettime */
	case 0x45:	/* Fdup */
		/* commands taking single word */
		LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x%02hX %s(0x%hX) at PC 0x%X\n",
			  GemDOSCall, GemDOS_Opcode2Name(GemDOSCall),
			  STMemory_ReadWord(Params),
			  CallingPC);
		break;

	case 0x0A:	/* Cconrs */
	case 0x1A:	/* Fsetdta */
	case 0x48:	/* Malloc */
	case 0x49:	/* Mfree */
		/* commands taking long/pointer */
		LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x%02hX %s(0x%X) at PC 0x%X\n",
			  GemDOSCall, GemDOS_Opcode2Name(GemDOSCall),
			  STMemory_ReadLong(Params),
			  CallingPC);
		break;

	case 0x44:	/* Mxalloc */
		/* commands taking long/pointer + word */
		LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x44 Mxalloc(0x%X, 0x%hX) at PC 0x%X\n",
			  STMemory_ReadLong(Params),
			  STMemory_ReadWord(Params+SIZE_LONG),
			  CallingPC);
		break;
	case 0x14:	/* Maddalt */
		/* commands taking 2 longs/pointers */
		LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x14 Maddalt(0x%X, 0x%X) at PC 0x%X\n",
			  STMemory_ReadLong(Params),
			  STMemory_ReadLong(Params+SIZE_LONG),
			  CallingPC);
		break;
	case 0x4A:	/* Mshrink */
		/* Mshrink's two pointers are prefixed by reserved zero word:
		 * http://toshyp.atari.org/en/00500c.html#Bindings_20for_20Mshrink
		 */
		LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x4A Mshrink(0x%X, 0x%X) at PC 0x%X\n",
			  STMemory_ReadLong(Params+SIZE_WORD),
			  STMemory_ReadLong(Params+SIZE_WORD+SIZE_LONG),
			  CallingPC);
		if (!bUseTos)
			Finished = true;
		break;

	default:
		/* rest of commands */
		LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x%02hX (%s) at PC 0x%X\n",
			  GemDOSCall, GemDOS_Opcode2Name(GemDOSCall),
			  CallingPC);
	}

	if (Finished)
	{
		sr |= SR_ZERO;
		/* visualize GemDOS emu HD access? */
		switch (GemDOSCall)
		{
		 case 0x36:
		 case 0x39:
		 case 0x3a:
		 case 0x3b:
		 case 0x3c:
		 case 0x3d:
		 case 0x3e:
		 case 0x3f:
		 case 0x40:
		 case 0x41:
		 case 0x42:
		 case 0x43:
		 case 0x47:
		 case 0x4e:
		 case 0x4f:
		 case 0x56:
			Statusbar_EnableHDLed( LED_STATE_ON );
		}
	}
	else
	{
		if (!bUseTos)
		{
			if (GemDOSCall >= 0x58)   /* Ignore optional calls */
			{
				Regs[REG_D0] = GEMDOS_EINVFN;
				M68000_SetSR(sr | SR_ZERO);
				return true;
			}
			Log_Printf(LOG_FATAL, "GEMDOS 0x%02hX %s at PC 0x%X unsupported in test mode\n",
				  GemDOSCall, GemDOS_Opcode2Name(GemDOSCall),
				  CallingPC);
			Main_SetQuitValue(1);
		}
		sr &= ~SR_ZERO;
	}

	M68000_SetSR(sr);
	return Finished;
}


/**
 * GemDOS_Boot
 * Sets up stuff for our gemdos handler
 */
void GemDOS_Boot(void)
{
	if (bInitGemDOS)
		GemDOS_Reset();

	bInitGemDOS = true;

	LOG_TRACE(TRACE_OS_GEMDOS, "Gemdos_Boot(GEMDOS_EMU_ON=%d) at PC 0x%X\n",
		  GEMDOS_EMU_ON, M68000_GetPC() );

	/* install our gemdos handler, if user has enabled either
	 * GEMDOS HD, autostarting or GEMDOS tracing
	 */
	if (!GEMDOS_EMU_ON &&
	    !INF_Overriding(AUTOSTART_INTERCEPT) &&
	    !LOG_TRACE_LEVEL(TRACE_OS_GEMDOS|TRACE_OS_BASE))
		return;

	/* Get the address of the p_run variable that points to the actual basepage */
	if (TosVersion == 0x100)
	{
		/* We have to use fix addresses on TOS 1.00 :-( */
		if ((STMemory_ReadWord(TosAddress+28)>>1) == 4)
			act_pd = 0x873c;    /* Spanish TOS is different from others! */
		else
			act_pd = 0x602c;
	}
	else
	{
		uint32_t osAddress = STMemory_ReadLong(0x4f2);
		act_pd = STMemory_ReadLong(osAddress + 0x28);
	}

	/* Save old GEMDOS handler address */
	STMemory_WriteLong(CART_OLDGEMDOS, STMemory_ReadLong(0x0084));
	/* Setup new GEMDOS handler, see "cart_asm.s" */
	STMemory_WriteLong(0x0084, CART_GEMDOS);
}


/**
 * Load and relocate a PRG file into the memory of the emulated machine.
 */
int GemDOS_LoadAndReloc(const char *psPrgName, uint32_t baseaddr, bool bFullBpSetup)
{
	long nFileSize, nRelTabIdx;
	uint8_t *prg;
	uint32_t nTextLen, nDataLen, nBssLen, nSymLen;
	uint32_t nRelOff, nCurrAddr;
	uint32_t memtop;

	prg = File_ReadAsIs(psPrgName, &nFileSize);
	if (!prg)
	{
		Log_Printf(LOG_ERROR, "Failed to load '%s'.\n", psPrgName);
		return GEMDOS_EFILNF;
	}

	/* Check program header size and magic */
	if (nFileSize < 30 || prg[0] != 0x60 || prg[1] != 0x1a)
	{
		free(prg);
		Log_Printf(LOG_ERROR, "The file '%s' is not a valid PRG.\n", psPrgName);
		return GEMDOS_EPLFMT;
	}

	nTextLen = (prg[2] << 24) | (prg[3] << 16) | (prg[4] << 8) | prg[5];
	nDataLen = (prg[6] << 24) | (prg[7] << 16) | (prg[8] << 8) | prg[9];
	nBssLen = (prg[10] << 24) | (prg[11] << 16) | (prg[12] << 8) | prg[13];
	nSymLen = (prg[14] << 24) | (prg[15] << 16) | (prg[16] << 8) | prg[17];

	if (baseaddr < 0x1000000)
		memtop = STMemory_ReadLong(0x436);
	else
		memtop = STMemory_ReadLong(0x5a4);
	if (baseaddr + 0x100 + nTextLen + nDataLen + nBssLen > memtop)
	{
		free(prg);
		Log_Printf(LOG_ERROR, "Program too large: '%s'.\n", psPrgName);
		return GEMDOS_ENSMEM;
	}

	if (!STMemory_SafeCopy(baseaddr + 0x100, prg + 28, nTextLen + nDataLen, psPrgName))
	{
		free(prg);
		return GEMDOS_EIMBA;
	}

	/* Clear BSS */
	if (!STMemory_SafeClear(baseaddr + 0x100 + nTextLen + nDataLen, nBssLen))
	{
		free(prg);
		Log_Printf(LOG_ERROR, "Failed to clear BSS for '%s'.\n", psPrgName);
		return GEMDOS_EIMBA;
	}

	/* Set up basepage */
	STMemory_WriteLong(baseaddr + 8, baseaddr + 0x100);                        /* p_tbase */
	STMemory_WriteLong(baseaddr + 12, nTextLen);                               /* p_tlen */
	STMemory_WriteLong(baseaddr + 16, baseaddr + 0x100 + nTextLen);            /* p_dbase */
	STMemory_WriteLong(baseaddr + 20, nDataLen);                               /* p_dlen */
	STMemory_WriteLong(baseaddr + 24, baseaddr + 0x100 + nTextLen + nDataLen); /* p_bbase */
	STMemory_WriteLong(baseaddr + 28, nBssLen);                                /* p_blen */
	/* In case we run without TOS, set some of the other values as good as possible, too */
	if (bFullBpSetup)
	{
		STMemory_WriteLong(baseaddr, baseaddr);                            /* p_lowtpa */
		STMemory_WriteLong(baseaddr + 4, memtop);                          /* p_hitpa */
		STMemory_WriteLong(baseaddr + 32, baseaddr + 0x80);                /* p_dta */
		STMemory_WriteLong(baseaddr + 36, baseaddr);                       /* p_parent */
		STMemory_WriteLong(baseaddr + 40, 0);                              /* p_reserved */
		/* The environment should point to an empty string - use p_reserved for that: */
		STMemory_WriteLong(baseaddr + 44, baseaddr + 40);                  /* p_env */
	}

	/* If FASTLOAD flag is not set, then also clear the heap */
	if (!(prg[25] & 1))
	{
		nCurrAddr = baseaddr + 0x100 + nTextLen + nDataLen + nBssLen;
		if (!STMemory_SafeClear(nCurrAddr, STMemory_ReadLong(baseaddr + 4) - nCurrAddr))
		{
			free(prg);
			Log_Printf(LOG_ERROR, "Failed to clear heap for '%s'.\n",
			           psPrgName);
			return GEMDOS_EIMBA;
		}
	}

	if (*(uint16_t *)&prg[26] != 0)   /* No reloc information available? */
	{
		free(prg);
		return 0;
	}

	nRelTabIdx = 0x1c + nTextLen + nDataLen;
	if (nRelTabIdx > nFileSize - 3)
	{
		free(prg);
		Log_Printf(LOG_ERROR, "Can not parse relocation table of '%s'.\n", psPrgName);
		return GEMDOS_EPLFMT;
	}
	if (nRelTabIdx + nSymLen <= nFileSize - 3U)
	{
		nRelTabIdx += nSymLen;
	}
	else
	{
		/* Original TOS ignores the error if the symbol table length
		 * is too, big, so just log a warning here instead of failing */
		Log_Printf(LOG_WARN, "Symbol table length of '%s' is too big!\n", psPrgName);
	}

	nRelOff = (prg[nRelTabIdx] << 24) | (prg[nRelTabIdx + 1] << 16)
	          | (prg[nRelTabIdx + 2] << 8) | prg[nRelTabIdx + 3];

	if (nRelOff == 0)
	{
		free(prg);
		return 0;
	}

	nCurrAddr = baseaddr + 0x100 + nRelOff;
	STMemory_WriteLong(nCurrAddr, STMemory_ReadLong(nCurrAddr) + baseaddr + 0x100);
	nRelTabIdx += 4;

	while (nRelTabIdx < nFileSize && prg[nRelTabIdx])
	{
		if (prg[nRelTabIdx] == 1)
		{
			nRelOff += 254;
			nRelTabIdx += 1;
			continue;
		}
		nRelOff += prg[nRelTabIdx];
		nCurrAddr = baseaddr + 0x100 + nRelOff;
		STMemory_WriteLong(nCurrAddr, STMemory_ReadLong(nCurrAddr) + baseaddr + 0x100);
		nRelTabIdx += 1;
	}
	free(prg);

	if (nRelTabIdx >= nFileSize)
	{
		Log_Printf(LOG_WARN, "Relocation table of '%s' is not terminated!\n", psPrgName);
	}

	return 0;
}

/**
 * This function is run after we've told TOS to create a basepage. We
 * can now load and relocate a program from our emulated HD into the
 * new TPA.
 */
void GemDOS_PexecBpCreated(void)
{
	char sFileName[FILENAME_MAX];
	char *sStFileName;
	uint32_t errcode;
	uint16_t sr = M68000_GetSR();
	uint16_t mode;
	uint32_t prgname;
	int drive;

	sr &= ~SR_OVERFLOW;

	mode = STMemory_ReadWord(nSavedPexecParams);
	prgname = STMemory_ReadLong(nSavedPexecParams + SIZE_WORD);

	sStFileName = STMemory_STAddrToPointer(prgname);
	LOG_TRACE(TRACE_OS_GEMDOS, "Basepage has been created - now loading '%s'\n",
	          sStFileName);

	drive = GemDOS_FileName2HardDriveID(sStFileName);
	if (drive >= 2)
	{
		GemDOS_CreateHardDriveFileName(drive, sStFileName, sFileName,
		                               sizeof(sFileName));
		errcode = GemDOS_LoadAndReloc(sFileName, Regs[REG_D0], false);
	}
	else
	{
		errcode = GEMDOS_EDRIVE;
	}

	if (errcode)
	{
		Regs[REG_A0] = Regs[REG_D0];
		Regs[REG_D0] = errcode;
		sr &= ~SR_ZERO;
	} else if (mode == 0)
	{
		/* Run another "just-go" Pexec call to start the program */
		STMemory_WriteWord(nSavedPexecParams, TosVersion >= 0x104 ? 6 : 4);
		STMemory_WriteLong(nSavedPexecParams + 6, Regs[REG_D0]);
		sr |= SR_OVERFLOW;
	}
	else
	{
		sr |= SR_ZERO;
	}

	M68000_SetSR(sr);
}
