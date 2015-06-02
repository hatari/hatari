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

  Bugs/things to fix:
  * Host filenames are in many places limited to 255 chars (same as
    on TOS), FILENAME_MAX should be used if that's a problem.
  * rmdir routine, can't remove dir with files in it. (another tos/unix difference)
  * Fix bugs, there are probably a few lurking around in here..
*/
const char Gemdos_fileid[] = "Hatari gemdos.c : " __DATE__ " " __TIME__;

#include <config.h>

#include <sys/stat.h>
#if HAVE_STATVFS
#include <sys/statvfs.h>
#endif
#include <sys/types.h>
#include <utime.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>

#include "main.h"
#include "cart.h"
#include "tos.h"
#include "configuration.h"
#include "file.h"
#include "floppy.h"
#include "ide.h"
#include "hdc.h"
#include "gemdos.h"
#include "gemdos_defines.h"
#include "log.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "printer.h"
#include "rs232.h"
#include "statusbar.h"
#include "scandir.h"
#include "stMemory.h"
#include "str.h"
#include "hatari-glue.h"
#include "maccess.h"
#include "symbols.h"

/* Maximum supported length of a GEMDOS path: */
#define MAX_GEMDOS_PATH 256

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
  Uint8 index[2];
  Uint8 magic[4];
  char dta_pat[TOS_NAMELEN];
  char dta_sattrib;
  char dta_attrib;
  Uint8 dta_time[2];
  Uint8 dta_date[2];
  Uint8 dta_size[4];
  char dta_name[TOS_NAMELEN];
} DTA;

#define DTA_MAGIC_NUMBER  0x12983476
#define MAX_DTAS_FILES    256      /* Must be ^2 */
#define CALL_PEXEC_ROUTINE 3       /* Call our cartridge pexec routine */

#define  BASE_FILEHANDLE     64    /* Our emulation handles - MUST not be valid TOS ones, but MUST be <256 */
#define  MAX_FILE_HANDLES    32    /* We can allow 32 files open at once */

/*
   DateTime structure used by TOS call $57 f_dtatime
   Changed to fix potential problem with alignment.
*/
typedef struct {
  Uint16 timeword;
  Uint16 dateword;
} DATETIME;

#define UNFORCED_HANDLE -1
static struct {
	int Handle;
	Uint32 Basepage;
} ForcedHandles[5]; /* (standard) handles aliased to emulated handles */

typedef struct
{
	bool bUsed;
	Uint32 Basepage;
	FILE *FileHandle;
	/* TODO: host path might not fit into this */
	char szActualName[MAX_GEMDOS_PATH];        /* used by F_DATIME (0x57) */
} FILE_HANDLE;

typedef struct
{
	bool bUsed;
	int  nentries;                      /* number of entries in fs directory */
	int  centry;                        /* current entry # */
	struct dirent **found;              /* legal files */
	char path[MAX_GEMDOS_PATH];                /* sfirst path */
} INTERNAL_DTA;

static FILE_HANDLE  FileHandles[MAX_FILE_HANDLES];
static INTERNAL_DTA InternalDTAs[MAX_DTAS_FILES];
static int DTAIndex;        /* Circular index into above */
static Uint32 DTA_Gemdos;   /* DTA address in ST memory space */
static DTA *pDTA;           /* Our GEMDOS hard drive Disk Transfer Address structure */
			    /* This a direct pointer to DTA_Gemdos using STMemory_STAddrToPointer() */
static Uint16 CurrentDrive; /* Current drive (0=A,1=B,2=C etc...) */
static Uint32 act_pd;       /* Used to get a pointer to the current basepage */
static Uint16 nAttrSFirst;  /* File attribute for SFirst/Snext */

/* last program opened by GEMDOS emulation */
static bool PexecCalled;

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
		Log_Printf(LOG_WARN, "WARNING: '%s' timestamp is invalid for (Windows?) localtime(), defaulting to TOS epoch!",  fname);
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
static Uint8 GemDOS_ConvertAttribute(mode_t mode)
{
	Uint8 Attrib = 0;

	/* Directory attribute */
	if (S_ISDIR(mode))
		Attrib |= GEMDOS_FILE_ATTRIB_SUBDIRECTORY;

	/* Read-only attribute */
	if (!(mode & S_IWUSR))
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
 * @return   0 if entry is ok, 1 if entry should be skipped, < 0 for errors.
 */
static int PopulateDTA(char *path, struct dirent *file)
{
	/* TODO: host file path can be longer than MAX_GEMDOS_PATH */
	char tempstr[MAX_GEMDOS_PATH];
	struct stat filestat;
	DATETIME DateTime;
	int nFileAttr, nAttrMask;

	snprintf(tempstr, sizeof(tempstr), "%s%c%s", path, PATHSEP, file->d_name);

	if (stat(tempstr, &filestat) != 0)
	{
		perror(tempstr);
		return -1;   /* return on error */
	}

	if (!pDTA)
		return -2;   /* no DTA pointer set */

	/* Check file attributes (check is done according to the Profibuch) */
	nFileAttr = GemDOS_ConvertAttribute(filestat.st_mode);
	nAttrMask = nAttrSFirst|GEMDOS_FILE_ATTRIB_WRITECLOSE|GEMDOS_FILE_ATTRIB_READONLY;
	if (nFileAttr != 0 && !(nAttrMask & nFileAttr))
		return 1;

	GemDOS_DateTime2Tos(filestat.st_mtime, &DateTime, tempstr);

	/* Atari memory modified directly through pDTA members -> flush the data cache */
	M68000_Flush_DCache(DTA_Gemdos, sizeof(DTA));

	/* convert to atari-style uppercase */
	Str_Filename2TOSname(file->d_name, pDTA->dta_name);
#if DEBUG_PATTERN_MATCH
	fprintf(stderr, "GEMDOS: host: %s -> GEMDOS: %s\n",
		file->d_name, pDTA->dta_name);
#endif
	do_put_mem_long(pDTA->dta_size, filestat.st_size);
	do_put_mem_word(pDTA->dta_time, DateTime.timeword);
	do_put_mem_word(pDTA->dta_date, DateTime.dateword);
	pDTA->dta_attrib = nFileAttr;

	return 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Clear a used DTA structure.
 */
static void ClearInternalDTA(void)
{
	int i;

	/* clear the old DTA structure */
	if (InternalDTAs[DTAIndex].found != NULL)
	{
		for (i=0; i < InternalDTAs[DTAIndex].nentries; i++)
			free(InternalDTAs[DTAIndex].found[i]);
		free(InternalDTAs[DTAIndex].found);
		InternalDTAs[DTAIndex].found = NULL;
	}
	InternalDTAs[DTAIndex].nentries = 0;
	InternalDTAs[DTAIndex].bUsed = false;
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
	if (strcmp(pat,"*.*")==0)
		return true;            /* match everything */
	if (strcasecmp(pat,name)==0)
		return true;            /* exact case insensitive match */

	dot = strrchr(name, '.');	/* '*' matches everything except _last_ '.' */
	while (*n)
	{
		if (*p=='*')
		{
			while (*n && n != dot)
				n++;
			p++;
		}
		else
		{
			if (*p=='?' && *n)
			{
				n++;
				p++;
			}
			else
			{
				if (toupper((unsigned char)*p++) != toupper((unsigned char)*n++))
					return false;
			}
		}
	}

	/* The name matches the pattern if it ends here, too */
	return (*p == 0 || (*p == '*' && *(p+1) == 0));
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
 * Return directory mask part from the given string
 */
static const char* fsfirst_dirmask(const char *string)
{
	const char *lastsep;

	lastsep = strrchr(string, PATHSEP);
	if (lastsep)
		return lastsep + 1;
	else
		return string;
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

/*-----------------------------------------------------------------------*/

/**
 * If program was executed, store path to it
 * (should be called only by Fopen)
 */
static void GemDOS_UpdateCurrentProgram(int Handle)
{
	/* only first Fopen after Pexec needs to be handled */
	if (!PexecCalled)
		return;
	PexecCalled = false;

	/* store program path */
	Symbols_ChangeCurrentProgram(FileHandles[Handle].szActualName);
}

/*-----------------------------------------------------------------------*/
/**
 * Initialize GemDOS/PC file system
 */
void GemDOS_Init(void)
{
	int i;
	bInitGemDOS = false;

	/* Clear handles structure */
	memset(FileHandles, 0, sizeof(FileHandles));
	for(i = 0; i < ARRAYSIZE(ForcedHandles); i++)
	{
		GemDOS_UnforceFileHandle(i);
	}
	/* Clear DTAs */
	for(i = 0; i < ARRAYSIZE(InternalDTAs); i++)
	{
		InternalDTAs[i].bUsed = false;
		InternalDTAs[i].nentries = 0;
		InternalDTAs[i].found = NULL;
	}
	DTAIndex = 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Reset GemDOS file system
 */
void GemDOS_Reset(void)
{
	int i;

	/* Init file handles table */
	for (i = 0; i < ARRAYSIZE(FileHandles); i++)
	{
		GemDOS_CloseFileHandle(i);
	}
	for(i = 0; i < ARRAYSIZE(ForcedHandles); i++)
	{
		GemDOS_UnforceFileHandle(i);
	}
	for (DTAIndex = 0; DTAIndex < MAX_DTAS_FILES; DTAIndex++)
	{
		ClearInternalDTA();
	}
	DTAIndex = 0;

	/* Reset */
	bInitGemDOS = false;
	CurrentDrive = nBootDrive;
	Symbols_RemoveCurrentProgram();
	DTA_Gemdos = 0x0;
	pDTA = NULL;
}

/*-----------------------------------------------------------------------*/
/**
 * Routine to check the Host OS HDD path for a Drive letter sub folder
 */
static bool GEMDOS_DoesHostDriveFolderExist(char* lpstrPath, int iDrive)
{
	bool bExist = false;

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
	 * arbitrary sub-folders are there (then use single-partition mode */
	count = scandir(ConfigureParams.HardDisk.szHardDiskDirectories[0], &files, 0, alphasort);
	if (count < 0)
	{
		Log_Printf(LOG_ERROR, "Error: GEMDOS hard disk emulation failed:\n "
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

	bMultiPartitions = GemDOS_DetermineMaxPartitions(&nMaxDrives);

	/* intialize data for harddrive emulation: */
	if (nMaxDrives > 0 && !emudrives)
	{
		emudrives = malloc(MAX_HARDDRIVES * sizeof(EMULATEDDRIVE *));
		if (!emudrives)
		{
			perror("GemDOS_InitDrives");
			return;
		}
		memset(emudrives, 0, MAX_HARDDRIVES * sizeof(EMULATEDDRIVE *));
	}

	ImagePartitions = nAcsiPartitions + nIDEPartitions;
	if (ConfigureParams.HardDisk.nGemdosDrive == DRIVE_SKIP)
		SkipPartitions = ImagePartitions;
	else
		SkipPartitions = ConfigureParams.HardDisk.nGemdosDrive;

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

		/* Add Requisit Folder ID */
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
			/* initialize current directory string, too (initially the same as hd_emulation_dir) */
			strcpy(emudrives[i]->fs_currpath, emudrives[i]->hd_emulation_dir);
			File_AddSlashToEndFileName(emudrives[i]->fs_currpath);    /* Needs trailing slash! */

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
				Log_Printf(LOG_WARN, "WARNING: GEMDOS HD drive %c: (may) override ACSI/IDE image partitions!\n", 'A'+DriveNumber);
		}
		else
		{
			free(emudrives[i]);	// Deallocate Memory (save space)
			emudrives[i] = NULL;
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Un-init the GEMDOS drive
 */
void GemDOS_UnInitDrives(void)
{
	int i;

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
 * Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
 */
void GemDOS_MemorySnapShot_Capture(bool bSave)
{
	int i;
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

	/* Save/Restore details */
	MemorySnapShot_Store(&DTAIndex,sizeof(DTAIndex));
	MemorySnapShot_Store(&bInitGemDOS,sizeof(bInitGemDOS));
	MemorySnapShot_Store(&act_pd, sizeof(act_pd));
	if (bSave)
	{
		/* Store the value in ST memory space */
		MemorySnapShot_Store ( &DTA_Gemdos , sizeof(DTA_Gemdos) );
	}
	else
	{
		/* Restore the value in ST memory space and update pDTA */
		MemorySnapShot_Store ( &DTA_Gemdos , sizeof(DTA_Gemdos) );
		if ( DTA_Gemdos == 0x0 )
			pDTA = NULL;
		else
			pDTA = (DTA *)STMemory_STAddrToPointer( DTA_Gemdos );
	}
	MemorySnapShot_Store(&CurrentDrive,sizeof(CurrentDrive));
	/* Don't save file handles as files may have changed which makes
	 * it impossible to get a valid handle back
	 */
	if (!bSave)
	{
		/* Clear file handles  */
		for(i = 0; i < ARRAYSIZE(FileHandles); i++)
		{
			GemDOS_CloseFileHandle(i);
		}
		for(i = 0; i < ARRAYSIZE(ForcedHandles); i++)
		{
			GemDOS_UnforceFileHandle(i);
		}
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
	for(i = 0; i < ARRAYSIZE(FileHandles); i++)
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
static bool GemDOS_BasepageMatches(Uint32 checkbase)
{
	int maxparents = 12; /* prevent basepage parent loops */
	Uint32 basepage = STMemory_ReadLong(act_pd);
	while (maxparents-- > 0 && STMemory_CheckAreaType ( basepage, 0x100, ABFLAG_RAM ) )
	{
		if (basepage == checkbase)
			return true;
		basepage = STMemory_ReadLong(basepage + 0x24);	/* parent */
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
	if (Handle >= 0 && Handle < ARRAYSIZE(ForcedHandles)
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
	if (Handle >= 0 && Handle < ARRAYSIZE(FileHandles)
	    && FileHandles[Handle].bUsed)
	{
		Uint32 current = STMemory_ReadLong(act_pd);
		if (FileHandles[Handle].Basepage == current || Forced >= 0)
			return Handle;
		/* bug in Atari program or in Hatari GEMDOS emu */
		Log_Printf(LOG_WARN, "PREVENTED: program 0x%x accessing program 0x%x file handle %d.",
			     current, FileHandles[Handle].Basepage, Handle);
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
	fprintf(stderr, "GEMDOS match '%s'%s in '%s'", name, pattern?" (pattern)":"", path);
#endif
	if (pattern)
	{
		while ((entry = readdir(dir)))
		{
			Str_DecomposedToPrecomposedUtf8(entry->d_name, entry->d_name);   /* for OSX */
			if (fsfirst_match(name, entry->d_name))
			{
				match = strdup(entry->d_name);
				break;
			}
		}
	}
	else
	{
		while ((entry = readdir(dir)))
		{
			Str_DecomposedToPrecomposedUtf8(entry->d_name, entry->d_name);   /* for OSX */
			if (strcasecmp(name, entry->d_name) == 0)
			{
				match = strdup(entry->d_name);
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
			Log_Printf(LOG_WARN, "WARNING: have to clip %d chars from '%s' extension!\n", diff, name);
			dot[4] = '\0';
		}
		diff = dot - name - 8;
		if (diff > 0)
		{
			Log_Printf(LOG_WARN, "WARNING: have to clip %d chars from '%s' base!\n", diff, name);
			memmove(name + 8, dot, strlen(dot) + 1);
		}
		return strlen(name);
	}
	len = strlen(name);
	if (len > 8)
	{
		Log_Printf(LOG_WARN, "WARNING: have to clip %d chars from '%s'!\n", len - 8, name);
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
	char *tmp, *match, name[strlen(origname) + 3];
	int dot, namelen, pathlen;
	int (*chr_conv)(int);
	bool modified;

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
			char dirname[dirlen+1];
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


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Set drive (0=A,1=B,2=C etc...)
 * Call 0xE
 */
static bool GemDOS_SetDrv(Uint32 Params)
{
	/* Read details from stack for our own use */
	CurrentDrive = STMemory_ReadWord(Params);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x0E Dsetdrv(0x%x) at PC=0x%X\n", (int)CurrentDrive,
		  M68000_GetPC());

	/* Still re-direct to TOS */
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Set Disk Transfer Address (DTA)
 * Call 0x1A
 */
static bool GemDOS_SetDTA(Uint32 Params)
{
	/*  Look up on stack to find where DTA is */
	DTA_Gemdos = STMemory_ReadLong(Params);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x1A Fsetdta(0x%x) at PC 0x%X\n", DTA_Gemdos,
		  M68000_GetPC());

	if ( STMemory_CheckAreaType ( DTA_Gemdos, sizeof(DTA), ABFLAG_RAM ) )
	{
		/* Store as PC pointer */
		pDTA = (DTA *)STMemory_STAddrToPointer(DTA_Gemdos);
	}
	else
	{
		Log_Printf(LOG_WARN, "GEMDOS Fsetdta() failed due to invalid DTA address 0x%x\n", DTA_Gemdos);
		DTA_Gemdos = 0x0;
		pDTA = NULL;
	}
	/* redirect to TOS */
	return false;
}

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Dfree Free disk space.
 * Call 0x36
 */
static bool GemDOS_DFree(Uint32 Params)
{
#ifdef HAVE_STATVFS
	struct statvfs buf;
#endif
	int Drive, Total, Free;
	Uint32 Address;

	Address = STMemory_ReadLong(Params);
	Drive = STMemory_ReadWord(Params+SIZE_LONG);

	/* Note: Drive = 0 means current drive, 1 = A:, 2 = B:, 3 = C:, etc. */
	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x36 Dfree(0x%x, %i) at PC 0x%X\n", Address, Drive,
		  M68000_GetPC());
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
/**
 * Helper to map Unix errno to GEMDOS error value
 */
typedef enum {
	ERROR_FILE,
	ERROR_PATH
} etype_t;

static Uint32 errno2gemdos(const int error, const etype_t etype)
{
	LOG_TRACE(TRACE_OS_GEMDOS, "-> ERROR (errno = %d)\n", error);
	switch (error)
	{
	case ENOENT:
		if (etype == ERROR_FILE)
			return GEMDOS_EFILNF;/* File not found */
	case ENOTDIR:
		return GEMDOS_EPTHNF;        /* Path not found */
	case ENOTEMPTY:
	case EEXIST:
	case EPERM:
	case EACCES:
	case EROFS:
		return GEMDOS_EACCDN;        /* Acess denied */
	default:
		return GEMDOS_ERROR;         /* Misc error */
	}
}

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS MkDir
 * Call 0x39
 */
static bool GemDOS_MkDir(Uint32 Params)
{
	char *pDirName, *psDirPath;
	int Drive;

	/* Find directory to make */
	pDirName = (char *)STMemory_STAddrToPointer(STMemory_ReadLong(Params));

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x39 Dcreate(\"%s\") at PC 0x%X\n", pDirName,
		  M68000_GetPC());

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
static bool GemDOS_RmDir(Uint32 Params)
{
	char *pDirName, *psDirPath;
	int Drive;

	/* Find directory to make */
	pDirName = (char *)STMemory_STAddrToPointer(STMemory_ReadLong(Params));

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x3A Ddelete(\"%s\") at PC 0x%X\n", pDirName,
		  M68000_GetPC());

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
static bool GemDOS_ChDir(Uint32 Params)
{
	char *pDirName, *psTempDirPath;
	struct stat buf;
	int Drive;

	/* Find new directory */
	pDirName = (char *)STMemory_STAddrToPointer(STMemory_ReadLong(Params));

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x3B Dsetpath(\"%s\") at PC 0x%X\n", pDirName,
		  M68000_GetPC());

	Drive = GemDOS_FileName2HardDriveID(pDirName);

	if (!ISHARDDRIVE(Drive))
	{
		/* redirect to TOS */
		return false;
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

	if (stat(psTempDirPath, &buf))
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
		strcpy(emudrives[Drive-2]->fs_currpath, psTempDirPath);
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
/**
 * GEMDOS Create file
 * Call 0x3C
 */
static bool GemDOS_Create(Uint32 Params)
{
	/* TODO: host filenames might not fit into this */
	char szActualFileName[MAX_GEMDOS_PATH];
	char *pszFileName;
	int Drive,Index, Mode;

	/* Find filename */
	pszFileName = (char *)STMemory_STAddrToPointer(STMemory_ReadLong(Params));
	Mode = STMemory_ReadWord(Params+SIZE_LONG);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x3C Fcreate(\"%s\", 0x%x) at PC 0x%X\n", pszFileName, Mode,
		  M68000_GetPC());

	Drive = GemDOS_FileName2HardDriveID(pszFileName);

	if (!ISHARDDRIVE(Drive))
	{
		/* redirect to TOS */
		return false;
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
		if (Mode & GEMDOS_FILE_ATTRIB_READONLY)
		{
			/* after closing, file should be read-only */
			chmod(szActualFileName, S_IRUSR|S_IRGRP|S_IROTH);
		}
		/* Tag handle table entry as used in this process and return handle */
		FileHandles[Index].bUsed = true;
		FileHandles[Index].Basepage = STMemory_ReadLong(act_pd);
		snprintf(FileHandles[Index].szActualName,
			 sizeof(FileHandles[Index].szActualName),
			 "%s", szActualFileName);

		/* Return valid ST file handle from our range (from BASE_FILEHANDLE upwards) */
		Regs[REG_D0] = Index+BASE_FILEHANDLE;
		LOG_TRACE(TRACE_OS_GEMDOS, "-> FD %d (%s)\n", Regs[REG_D0],
			  Mode & GEMDOS_FILE_ATTRIB_READONLY ? "read-only":"read/write");
		return true;
	}
	LOG_TRACE(TRACE_OS_GEMDOS, "-> ERROR (errno = %d)\n", errno);

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


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Open file
 * Call 0x3D
 */
static bool GemDOS_Open(Uint32 Params)
{
	/* TODO: host filenames might not fit into this */
	char szActualFileName[MAX_GEMDOS_PATH];
	char *pszFileName;
	const char *ModeStr, *RealMode;
	const char *Modes[] = {
		"read-only", "write-only", "read/write", "read/write"
	};
	int Drive, Index, Mode;
	FILE *AutostartHandle;

	/* Find filename */
	pszFileName = (char *)STMemory_STAddrToPointer(STMemory_ReadLong(Params));
	Mode = STMemory_ReadWord(Params+SIZE_LONG);
	Mode &= 3;

	LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE,
		  "GEMDOS 0x3D Fopen(\"%s\", %s) at PC=0x%X\n",
		  pszFileName, Modes[Mode], M68000_GetPC());

	Drive = GemDOS_FileName2HardDriveID(pszFileName);

	if (!ISHARDDRIVE(Drive))
	{
		/* redirect to TOS */
		LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE, "-> to TOS\n");
		return false;
	}

	/* Find slot to store file handle, as need to return WORD handle for ST  */
	Index = GemDOS_FindFreeFileHandle();
	if (Index == -1)
	{
		/* No free handles, return error code */
		Regs[REG_D0] = GEMDOS_ENHNDL;       /* No more handles */
		return true;
	}

	if ((AutostartHandle = TOS_AutoStartOpen(pszFileName)))
	{
		strcpy(szActualFileName, pszFileName);
		FileHandles[Index].FileHandle = AutostartHandle;
		RealMode = "read-only";
	}
	else
	{
		struct stat FileStat;

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
		 * - requested by Atari program
		 * - Hatari write protection is enabled
		 * - File itself is read-only
		 * Latter is done to help cases where application
		 * needlessly requests write access, but file is
		 * on read-only media (like CD/DVD).
		 */
		if (Mode == 0 ||
		    ConfigureParams.HardDisk.nWriteProtection == WRITEPROT_ON ||
		    (stat(szActualFileName, &FileStat) == 0 && !(FileStat.st_mode & S_IWUSR)))
		{
			ModeStr = "rb";
			RealMode = "read-only";
		}
		else
		{
			ModeStr = "rb+";
			RealMode = "read+write";
		}
		FileHandles[Index].FileHandle = fopen(szActualFileName, ModeStr);
	}

	if (FileHandles[Index].FileHandle != NULL)
	{
		/* Tag handle table entry as used in this process and return handle */
		FileHandles[Index].bUsed = true;
		FileHandles[Index].Basepage = STMemory_ReadLong(act_pd);
		snprintf(FileHandles[Index].szActualName,
			 sizeof(FileHandles[Index].szActualName),
			 "%s", szActualFileName);

		GemDOS_UpdateCurrentProgram(Index);

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
static bool GemDOS_Close(Uint32 Params)
{
	int i, Handle;

	/* Find our handle - may belong to TOS */
	Handle = STMemory_ReadWord(Params);

	LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE,
		  "GEMDOS 0x3E Fclose(%i) at PC 0x%X\n",
		  Handle, M68000_GetPC());

	/* Get internal handle */
	if ((Handle = GemDOS_GetValidFileHandle(Handle)) < 0)
	{
		/* no, assume it was TOS one -> redirect */
		return false;
	}
	
	/* Close file and free up handle table */
	if (TOS_AutoStartClose(FileHandles[Handle].FileHandle))
	{
		FileHandles[Handle].bUsed = false;
	}
	GemDOS_CloseFileHandle(Handle);

	/* unalias handle */
	for (i = 0; i < ARRAYSIZE(ForcedHandles); i++)
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
static bool GemDOS_Read(Uint32 Params)
{
	char *pBuffer;
	off_t CurrentPos, FileSize;
	long nBytesRead, nBytesLeft;
	Uint32 Addr;
	Uint32 Size;
	int Handle;

	/* Read details from stack */
	Handle = STMemory_ReadWord(Params);
	Size = STMemory_ReadLong(Params+SIZE_WORD);
	Addr = STMemory_ReadLong(Params+SIZE_WORD+SIZE_LONG);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x3F Fread(%i, %i, 0x%x) at PC 0x%X\n",
	          Handle, Size, Addr,
		  M68000_GetPC());

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
	if (Size > (Uint32)nBytesLeft)
		Size = nBytesLeft;

	/* Check that read is to valid memory area */
	if ( !STMemory_CheckAreaType ( Addr, Size, ABFLAG_RAM ) )
	{
		Log_Printf(LOG_WARN, "GEMDOS Fread() failed due to invalid RAM range 0x%x+%i\n", Addr, Size);
		Regs[REG_D0] = GEMDOS_ERANGE;
		return true;
	}

	/* Atari memory modified directly with fread() -> flush the data cache */
	M68000_Flush_DCache(Addr, Size);

	/* And read data in */
	pBuffer = (char *)STMemory_STAddrToPointer(Addr);
	nBytesRead = fread(pBuffer, 1, Size, FileHandles[Handle].FileHandle);
	
	if (ferror(FileHandles[Handle].FileHandle))
	{
		Log_Printf(LOG_WARN, "GEMDOS failed to read from '%s'\n",
			   FileHandles[Handle].szActualName );
		Regs[REG_D0] = errno2gemdos(errno, ERROR_FILE);
	} else
		/* Return number of bytes read */
		Regs[REG_D0] = nBytesRead;

	return true;
}

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Write file
 * Call 0x40
 */
static bool GemDOS_Write(Uint32 Params)
{
	char *pBuffer;
	long nBytesWritten;
	Uint32 Addr;
	Sint32 Size;
	int Handle;
	FILE *fp;

	/* Read details from stack */
	Handle = STMemory_ReadWord(Params);
	Size = STMemory_ReadLong(Params+SIZE_WORD);
	Addr = STMemory_ReadLong(Params+SIZE_WORD+SIZE_LONG);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x40 Fwrite(%i, %i, 0x%x) at PC 0x%X\n",
	          Handle, Size, Addr,
		  M68000_GetPC());

	/* Get internal handle */
	if ((Handle = GemDOS_GetValidFileHandle(Handle)) < 0)
	{
		/* assume it was TOS one -> redirect */
		return false;
	}

	/* write protected device? */
	if (ConfigureParams.HardDisk.nWriteProtection == WRITEPROT_ON)
	{
		Log_Printf(LOG_WARN, "PREVENTED: GEMDOS Fwrite(%d,...)\n", Handle);
		Regs[REG_D0] = GEMDOS_EWRPRO;
		return true;
	}

	/* Check that write is from valid memory area */
	if ( !STMemory_CheckAreaType ( Addr, Size, ABFLAG_RAM ) )
	{
		Log_Printf(LOG_WARN, "GEMDOS Fwrite() failed due to invalid RAM range 0x%x+%i\n", Addr, Size);
		Regs[REG_D0] = GEMDOS_ERANGE;
		return true;
	}

	pBuffer = (char *)STMemory_STAddrToPointer(Addr);
	fp = FileHandles[Handle].FileHandle;
	nBytesWritten = fwrite(pBuffer, 1, Size, fp);
	if (ferror(fp))
	{
		Log_Printf(LOG_WARN, "GEMDOS failed to write to '%s'\n",
			   FileHandles[Handle].szActualName );
		Regs[REG_D0] = errno2gemdos(errno, ERROR_FILE);
	}
	else
	{
		fflush(fp);
		Regs[REG_D0] = nBytesWritten;      /* OK */
	}
	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Delete file
 * Call 0x41
 */
static bool GemDOS_FDelete(Uint32 Params)
{
	char *pszFileName, *psActualFileName;
	int Drive;

	/* Find filename */
	pszFileName = (char *)STMemory_STAddrToPointer(STMemory_ReadLong(Params));

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x41 Fdelete(\"%s\") at PC 0x%X\n", pszFileName,
		  M68000_GetPC());

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
static bool GemDOS_LSeek(Uint32 Params)
{
	long Offset;
	int Handle, Mode;
	long nFileSize;
	long nOldPos, nDestPos;
	FILE *fhndl;

	/* Read details from stack */
	Offset = (Sint32)STMemory_ReadLong(Params);
	Handle = STMemory_ReadWord(Params+SIZE_LONG);
	Mode = STMemory_ReadWord(Params+SIZE_LONG+SIZE_WORD);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x42 Fseek(%li, %i, %i) at PC 0x%X\n", Offset, Handle, Mode,
		  M68000_GetPC());

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
	if (fseek(fhndl, 0L, SEEK_END) != 0)
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
static bool GemDOS_Fattrib(Uint32 Params)
{
	/* TODO: host filenames might not fit into this */
	char sActualFileName[MAX_GEMDOS_PATH];
	char *psFileName;
	int nDrive;
	int nRwFlag, nAttrib;
	struct stat FileStat;

	/* Find filename */
	psFileName = (char *)STMemory_STAddrToPointer(STMemory_ReadLong(Params));
	nDrive = GemDOS_FileName2HardDriveID(psFileName);

	nRwFlag = STMemory_ReadWord(Params+SIZE_LONG);
	nAttrib = STMemory_ReadWord(Params+SIZE_LONG+SIZE_WORD);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x43 Fattrib(\"%s\", %d, 0x%x) at PC 0x%X\n",
	          psFileName, nRwFlag, nAttrib,
		  M68000_GetPC());

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
		Log_Printf(LOG_WARN, "Warning: Hatari doesn't support GEMDOS volume label setting\n(for '%s')\n", sActualFileName);
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
		Regs[REG_D0] = GemDOS_ConvertAttribute(FileStat.st_mode);
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
static bool GemDOS_Force(Uint32 Params)
{
	int std, own;

	/* Read details from stack */
	std = STMemory_ReadWord(Params);
        own = STMemory_ReadWord(Params+SIZE_WORD);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x46 Fforce(%d, %d) at PC 0x%X\n", std, own,
		  M68000_GetPC());

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
	if (std < 0 || std >= ARRAYSIZE(ForcedHandles))
	{
		Log_Printf(LOG_WARN, "Warning: forcing of non-standard %d (> %d) handle ignored.\n", std, ARRAYSIZE(ForcedHandles));
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
static bool GemDOS_GetDir(Uint32 Params)
{
	Uint32 Address;
	Uint16 Drive;

	Address = STMemory_ReadLong(Params);
	Drive = STMemory_ReadWord(Params+SIZE_LONG);

	/* Note: Drive = 0 means current drive, 1 = A:, 2 = B:, 3 = C:, etc. */
	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x47 Dgetpath(0x%x, %i) at PC 0x%X\n", Address, (int)Drive,
		  M68000_GetPC());
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
static int GemDOS_Pexec(Uint32 Params)
{
	int Drive;
	Uint16 Mode;
	char *pszFileName;

	/* Find PExec mode */
	Mode = STMemory_ReadWord(Params);

	if (LOG_TRACE_LEVEL(TRACE_OS_GEMDOS|TRACE_OS_BASE))
	{
		Uint32 fname, cmdline, env_string;
		fname = STMemory_ReadLong(Params+SIZE_WORD);
		cmdline = STMemory_ReadLong(Params+SIZE_WORD+SIZE_LONG);
		env_string = STMemory_ReadLong(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG);
		if (Mode == 0 || Mode == 3)
		{
			int cmdlen;
			char *str;
			const char *name, *cmd;
			name = (const char *)STMemory_STAddrToPointer(fname);
			cmd = (const char *)STMemory_STAddrToPointer(cmdline);
			cmdlen = *cmd++;
			str = malloc(cmdlen+1);
			memcpy(str, cmd, cmdlen);
			str[cmdlen] = '\0';
			LOG_TRACE_PRINT ( "GEMDOS 0x4B Pexec(%i, \"%s\", [%d]\"%s\", 0x%x) at PC 0x%X\n", Mode, name, cmdlen, str, env_string,
				M68000_GetPC());
			free(str);
		}
		else
		{
			LOG_TRACE_PRINT ( "GEMDOS 0x4B Pexec(%i, 0x%x, 0x%x, 0x%x) at PC 0x%X\n", Mode, fname, cmdline, env_string,
				M68000_GetPC());
		}
	}

	/* Re-direct as needed */
	switch(Mode)
	{
	 case 0:      /* Load and go */
	 case 3:      /* Load, don't go */
		pszFileName = (char *)STMemory_STAddrToPointer(STMemory_ReadLong(Params+SIZE_WORD));
		Drive = GemDOS_FileName2HardDriveID(pszFileName);
		
		/* If not using A: or B:, use my own routines to load */
		if (ISHARDDRIVE(Drive))
		{
			/* Redirect to cart' routine at address 0xFA1000 */
			PexecCalled = true;
			return CALL_PEXEC_ROUTINE;
		}
		return false;
	 case 4:      /* Just go */
		return false;
	 case 5:      /* Create basepage */
		return false;
	 case 6:
		return false;
	}

	/* Default: Still re-direct to TOS */
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Search Next
 * Call 0x4F
 */
static bool GemDOS_SNext(void)
{
	struct dirent **temp;
	int Index;
	int ret;

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x4F Fsnext() at PC 0x%X\n" , M68000_GetPC());

	/* Refresh pDTA pointer (from the current basepage) */
	DTA_Gemdos = STMemory_ReadLong(STMemory_ReadLong(act_pd)+32);

	if ( !STMemory_CheckAreaType ( DTA_Gemdos, sizeof(DTA), ABFLAG_RAM ) )
	{
		Log_Printf(LOG_WARN, "GEMDOS Fsnext() failed due to invalid DTA address 0x%x\n", DTA_Gemdos);
		DTA_Gemdos = 0x0;
		pDTA = NULL;
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

	/* Find index into our list of structures */
	Index = do_get_mem_word(pDTA->index) & (MAX_DTAS_FILES-1);

	if (nAttrSFirst == GEMDOS_FILE_ATTRIB_VOLUME_LABEL)
	{
		/* Volume label was given already in Sfirst() */
		Regs[REG_D0] = GEMDOS_ENMFIL;
		return true;
	}

	temp = InternalDTAs[Index].found;
	do
	{
		if (InternalDTAs[Index].centry >= InternalDTAs[Index].nentries)
		{
			Regs[REG_D0] = GEMDOS_ENMFIL;    /* No more files */
			return true;
		}

		ret = PopulateDTA(InternalDTAs[Index].path,
				  temp[InternalDTAs[Index].centry++]);
	} while (ret == 1);

	if (ret < 0)
	{
		Log_Printf(LOG_WARN, "GEMDOS Fsnext(): Error setting DTA.\n");
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
static bool GemDOS_SFirst(Uint32 Params)
{
	/* TODO: host filenames might not fit into this */
	char szActualFileName[MAX_GEMDOS_PATH];
	char *pszFileName;
	const char *dirmask;
	struct dirent **files;
	int Drive;
	DIR *fsdir;
	int i,j,count;

	/* Find filename to search for */
	pszFileName = (char *)STMemory_STAddrToPointer(STMemory_ReadLong(Params));
	nAttrSFirst = STMemory_ReadWord(Params+SIZE_LONG);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x4E Fsfirst(\"%s\", 0x%x) at PC 0x%X\n", pszFileName, nAttrSFirst,
		  M68000_GetPC());

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
	DTA_Gemdos = STMemory_ReadLong(STMemory_ReadLong(act_pd)+32);

	if ( !STMemory_CheckAreaType ( DTA_Gemdos, sizeof(DTA), ABFLAG_RAM ) )
	{
		Log_Printf(LOG_WARN, "GEMDOS Fsfirst() failed due to invalid DTA address 0x%x\n", DTA_Gemdos);
		DTA_Gemdos = 0x0;
		pDTA = NULL;
		Regs[REG_D0] = GEMDOS_EINTRN;    /* "internal error */
		return true;
	}

	/* Atari memory modified directly with do_mem_* + strcpy() -> flush the data cache */
	M68000_Flush_DCache(DTA_Gemdos, sizeof(DTA));

	pDTA = (DTA *)STMemory_STAddrToPointer(DTA_Gemdos);

	/* Populate DTA, set index for our use */
	do_put_mem_word(pDTA->index, DTAIndex);
	/* set our dta magic num */
	do_put_mem_long(pDTA->magic, DTA_MAGIC_NUMBER);

	if (InternalDTAs[DTAIndex].bUsed == true)
		ClearInternalDTA();
	InternalDTAs[DTAIndex].bUsed = true;

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
	fsfirst_dirname(szActualFileName, InternalDTAs[DTAIndex].path);
	fsdir = opendir(InternalDTAs[DTAIndex].path);

	if (fsdir == NULL)
	{
		Regs[REG_D0] = GEMDOS_EPTHNF;        /* Path not found */
		return true;
	}
	/* close directory */
	closedir(fsdir);

	count = scandir(InternalDTAs[DTAIndex].path, &files, 0, alphasort);
	/* File (directory actually) not found */
	if (count < 0)
	{
		Regs[REG_D0] = GEMDOS_EFILNF;
		return true;
	}

	InternalDTAs[DTAIndex].centry = 0;          /* current entry is 0 */
	dirmask = fsfirst_dirmask(szActualFileName);/* directory mask part */
	InternalDTAs[DTAIndex].found = files;       /* get files */

	/* count & copy the entries that match our mask and discard the rest */
	j = 0;
	for (i=0; i < count; i++)
	{
		Str_DecomposedToPrecomposedUtf8(files[i]->d_name, files[i]->d_name);   /* for OSX */
		if (fsfirst_match(dirmask, files[i]->d_name))
		{
			InternalDTAs[DTAIndex].found[j] = files[i];
			j++;
		}
		else
		{
			free(files[i]);
			files[i] = NULL;
		}
	}
	InternalDTAs[DTAIndex].nentries = j; /* set number of legal entries */

	/* No files of that match, return error code */
	if (j==0)
	{
		free(files);
		InternalDTAs[DTAIndex].found = NULL;
		Regs[REG_D0] = GEMDOS_EFILNF;        /* File not found */
		return true;
	}

	/* Scan for first file (SNext uses no parameters) */
	GemDOS_SNext();
	/* increment DTA index */
	DTAIndex++;
	DTAIndex &= (MAX_DTAS_FILES-1);

	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Rename
 * Call 0x56
 */
static bool GemDOS_Rename(Uint32 Params)
{
	char *pszNewFileName,*pszOldFileName;
	/* TODO: host filenames might not fit into this */
	char szNewActualFileName[MAX_GEMDOS_PATH];
	char szOldActualFileName[MAX_GEMDOS_PATH];
	int NewDrive, OldDrive;

	/* Read details from stack, skip first (dummy) arg */
	pszOldFileName = (char *)STMemory_STAddrToPointer(STMemory_ReadLong(Params+SIZE_WORD));
	pszNewFileName = (char *)STMemory_STAddrToPointer(STMemory_ReadLong(Params+SIZE_WORD+SIZE_LONG));

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x56 Frename(\"%s\", \"%s\") at PC 0x%X\n", pszOldFileName, pszNewFileName,
		  M68000_GetPC());

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

	/* Rename files */
	if (rename(szOldActualFileName,szNewActualFileName) == 0)
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
static bool GemDOS_GSDToF(Uint32 Params)
{
	DATETIME DateTime;
	Uint32 pBuffer;
	int Handle,Flag;

	/* Read details from stack */
	pBuffer = STMemory_ReadLong(Params);
	Handle = STMemory_ReadWord(Params+SIZE_LONG);
	Flag = STMemory_ReadWord(Params+SIZE_LONG+SIZE_WORD);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x57 Fdatime(0x%x, %i, %i) at PC 0x%X\n", pBuffer,
	          Handle, Flag,
		  M68000_GetPC());

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
	Uint32 current = STMemory_ReadLong(act_pd);

	closed = 0;
	for (i = 0; i < ARRAYSIZE(FileHandles); i++)
	{
		if (FileHandles[i].Basepage == current)
		{
			GemDOS_CloseFileHandle(i);
			closed++;
		}
	}
	unforced = 0;
	for (i = 0; i < ARRAYSIZE(ForcedHandles); i++)
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
static bool GemDOS_Pterm0(Uint32 Params)
{
	LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE, "GEMDOS 0x00 Pterm0() at PC 0x%X\n",
		  M68000_GetPC());
	GemDOS_TerminateClose();
	Symbols_RemoveCurrentProgram();
	return false;
}

/**
 * GEMDOS Ptermres
 * Call 0x31
 */
static bool GemDOS_Ptermres(Uint32 Params)
{
	LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE, "GEMDOS 0x31 Ptermres(0x%X, %hd) at PC 0x%X\n",
		  STMemory_ReadLong(Params), (Sint16)STMemory_ReadWord(Params+SIZE_WORD),
		  M68000_GetPC());
	GemDOS_TerminateClose();
	return false;
}

/**
 * GEMDOS Pterm
 * Call 0x4c
 */
static bool GemDOS_Pterm(Uint32 Params)
{
	LOG_TRACE(TRACE_OS_GEMDOS|TRACE_OS_BASE, "GEMDOS 0x4C Pterm(%hd) at PC 0x%X\n",
		  (Sint16)STMemory_ReadWord(Params),
		  M68000_GetPC());
	GemDOS_TerminateClose();
	Symbols_RemoveCurrentProgram();
	return false;
}


#if ENABLE_TRACING
/*-----------------------------------------------------------------------*/
/**
 * Map GEMDOS call opcodes to their names
 * 
 * Mapping is based on TOSHYP information:
 *	http://toshyp.atari.org/en/005013.html
 */
static const char* GemDOS_Opcode2Name(Uint16 opcode)
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
		"Fdatime"
	};
	if (opcode < ARRAYSIZE(names))
		return names[opcode];
	return "MiNT?";
}

/**
 * If bShowOpcodes is true, show GEMDOS call opcode/function name table,
 * otherwise GEMDOS HDD emulation information.
 */
void GemDOS_Info(FILE *fp, Uint32 bShowOpcodes)
{
	int i, used;

	if (bShowOpcodes)
	{
		Uint16 opcode;
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

	/* GEMDOS vector set by Hatari can be overwritten e.g. MiNT */
	fprintf(fp, "Current GEMDOS handler: (0x84) = 0x%x, emu one = 0x%x\n", STMemory_ReadLong(0x0084), CART_GEMDOS);
	fprintf(fp, "Stored GEMDOS handler: (0x%x) = 0x%x\n\n", CART_OLDGEMDOS, STMemory_ReadLong(CART_OLDGEMDOS));

	fprintf(fp, "Connected drives mask: 0x%x\n\n", ConnectedDriveMask);
	fputs("GEMDOS HDD emulation drives:\n", fp);
	for(i = 0; i < MAX_HARDDRIVES; i++)
	{
		if (!emudrives[i])
			continue;
		fprintf(fp, "- %c: %s\n",
			'A' + emudrives[i]->drive_number,
			emudrives[i]->hd_emulation_dir);
	}

	fputs("\nInternal Fsfirst() DTAs:\n", fp);
	for(used = i = 0; i < ARRAYSIZE(InternalDTAs); i++)
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
	for (used = i = 0; i < ARRAYSIZE(FileHandles); i++)
	{
		if (!FileHandles[i].bUsed)
			continue;
		fprintf(fp, "- %d (0x%x): %s\n", i + BASE_FILEHANDLE,
			FileHandles[i].Basepage, FileHandles[i].szActualName);
		used++;
	}
	if (!used)
		fputs("- None.\n", fp);
	fputs("\nForced GEMDOS HDD file handles:\n", fp);
	for (used = i = 0; i < ARRAYSIZE(ForcedHandles); i++)
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
}

#else /* !ENABLE_TRACING */
void GemDOS_Info(FILE *fp, Uint32 bShowOpcodes)
{
	fputs("Hatari isn't configured with ENABLE_TRACING\n", fp);
}
#endif /* !ENABLE_TRACING */


/**
 * Run GEMDos call, and re-direct if need to. Used to handle hard disk emulation etc...
 * This sets the condition codes (in SR), which are used in the 'cart_asm.s' program to
 * decide if we need to run old GEM vector, or PExec or nothing.
 *
 * This method keeps the stack and other states consistent with the original ST
 * which is very important for the PExec call and maximum compatibility through-out
 */
void GemDOS_OpCode(void)
{
	Uint16 GemDOSCall, CallingSReg;
	Uint32 Params;
	int Finished;
	Uint16 SR;

	SR = M68000_GetSR();

	/* Read SReg from stack to see if parameters are on User or Super stack  */
	CallingSReg = STMemory_ReadWord(Regs[REG_A7]);
	if ((CallingSReg&SR_SUPERMODE)==0)      /* Calling from user mode */
		Params = regs.usp;
	else
	{
		Params = Regs[REG_A7]+SIZE_WORD+SIZE_LONG;  /* skip SR & PC pushed to super stack */
		if (currprefs.cpu_level > 0)
			Params += SIZE_WORD;   /* Skip extra word whe CPU is >=68010 */
	}

	/* Default to run TOS GemDos (SR_NEG run Gemdos, SR_ZERO already done, SR_OVERFLOW run own 'Pexec' */
	Finished = false;
	SR &= SR_CLEAR_OVERFLOW;
	SR &= SR_CLEAR_ZERO;
	SR |= SR_NEG;

	/* Find pointer to call parameters */
	GemDOSCall = STMemory_ReadWord(Params);
	Params += SIZE_WORD;

	/* Intercept call */
	switch(GemDOSCall)
	{
	 case 0x00:
		Finished = GemDOS_Pterm0(Params);
		break;
	 case 0x0e:
		Finished = GemDOS_SetDrv(Params);
		break;
	 case 0x1a:
		Finished = GemDOS_SetDTA(Params);
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
		/* Either false or CALL_PEXEC_ROUTINE */
		Finished = GemDOS_Pexec(Params);
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
			  M68000_GetPC());
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
			  M68000_GetPC());
		break;

	case 0x09:	/* Cconws */
	case 0x0A:	/* Cconrs */
	case 0x20:	/* Super */
	case 0x48:	/* Malloc */
	case 0x49:	/* Mfree */
		/* commands taking long/pointer */
		LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x%02hX %s(0x%X) at PC 0x%X\n",
			  GemDOSCall, GemDOS_Opcode2Name(GemDOSCall),
			  STMemory_ReadLong(Params),
			  M68000_GetPC());
		break;

	case 0x44:	/* Mxalloc */
		/* commands taking long/pointer + word */
		LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x44 Mxalloc(0x%X, 0x%hX) at PC 0x%X\n",
			  STMemory_ReadLong(Params),
			  STMemory_ReadWord(Params+SIZE_LONG),
			  M68000_GetPC());
		break;
	case 0x14:	/* Maddalt */
		/* commands taking 2 longs/pointers */
		LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x14 Maddalt(0x%X, 0x%X) at PC 0x%X\n",
			  STMemory_ReadLong(Params),
			  STMemory_ReadLong(Params+SIZE_LONG),
			  M68000_GetPC());
	case 0x4A:	/* Mshrink */
		/* Mshrink's two pointers are prefixed by reserved zero word:
		 * http://toshyp.atari.org/en/00500c.html#Bindings_20for_20Mshrink
		 */
		LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x4A Mshrink(0x%X, 0x%X) at PC 0x%X\n",
			  STMemory_ReadLong(Params+SIZE_WORD),
			  STMemory_ReadLong(Params+SIZE_WORD+SIZE_LONG),
			  M68000_GetPC());
		break;

	default:
		/* rest of commands */
		LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS 0x%02hX (%s) at PC 0x%X\n",
			  GemDOSCall, GemDOS_Opcode2Name(GemDOSCall),
			  M68000_GetPC());
	}

	switch(Finished)
	{
	 case true:
		/* skip over branch to pexec to RTE */
		SR |= SR_ZERO;
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
		break;
	 case CALL_PEXEC_ROUTINE:
		/* branch to pexec, then redirect to old gemdos. */
		SR |= SR_OVERFLOW;
		break;
	}

	M68000_SetSR(SR);   /* update the flags in the SR register */
}


/*-----------------------------------------------------------------------*/
/**
 * GemDOS_Boot - routine called on the first occurrence of the gemdos opcode.
 * (this should be in the cartridge bootrom)
 * Sets up our gemdos handler (or, if we don't need one, just turn off keyclicks)
 */
void GemDOS_Boot(void)
{
	bInitGemDOS = true;

	LOG_TRACE(TRACE_OS_GEMDOS, "Gemdos_Boot() at PC 0x%X\n", M68000_GetPC() );

	/* install our gemdos handler, if -e or --harddrive option used,
	 * or user wants to do GEMDOS tracing
	 */
	if (!GEMDOS_EMU_ON && !(LogTraceFlags & (TRACE_OS_GEMDOS|TRACE_OS_BASE)))
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
		act_pd = STMemory_ReadLong(TosAddress + 0x28);
	}

	/* Save old GEMDOS handler address */
	STMemory_WriteLong(CART_OLDGEMDOS, STMemory_ReadLong(0x0084));
	/* Setup new GEMDOS handler, see "cart_asm.s" */
	STMemory_WriteLong(0x0084, CART_GEMDOS);
}
