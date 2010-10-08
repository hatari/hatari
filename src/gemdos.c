/*
  Hatari - gemdos.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

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


/* Maximum supported length of a GEMDOS path: */
#define MAX_GEMDOS_PATH 256

/* Invalid characters in paths & filenames are replaced by this
 * (valid but very uncommon GEMDOS file name character)
 */
#define INVALID_CHAR '@'

/* Have we re-directed GemDOS vector to our own routines yet? */
bool bInitGemDOS;

/* structure with all the drive-specific data for our emulated drives,
 * used by GEMDOS_EMU_ON macro
 */
EMULATEDDRIVE **emudrives = NULL;

#define  ISHARDDRIVE(Drive)  (Drive!=-1)

/*
  Disk Tranfer Address (DTA)
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
   DateTime structure used by TOS call $57 f_dattime
   Changed to fix potential problem with alignment.
*/
typedef struct {
  Uint16 timeword;
  Uint16 dateword;
} DATETIME;

typedef struct
{
	bool bUsed;
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
static DTA *pDTA;           /* Our GEMDOS hard drive Disk Transfer Address structure */
static Uint16 CurrentDrive; /* Current drive (0=A,1=B,2=C etc...) */
static Uint32 act_pd;       /* Used to get a pointer to the current basepage */
static Uint16 nAttrSFirst;  /* File attribute for SFirst/Snext */


#if defined(WIN32) && !defined(mkdir)
#define mkdir(name,mode) mkdir(name)
#endif  /* WIN32 */

#ifndef S_IRGRP
#define S_IRGRP 0
#define S_IROTH 0
#endif



/*-------------------------------------------------------*/
/**
 * Routine to convert time and date to GEMDOS format.
 * Originally from the STonX emulator. (cheers!)
 */
static bool GemDOS_DateTime2Tos(time_t t, DATETIME *DateTime)
{
	struct tm *x;

	x = localtime(&t);

	if (x == NULL)
		return false;

	/* Bits: 0-4 = secs/2, 5-10 = mins, 11-15 = hours (24-hour format) */
	DateTime->timeword = (x->tm_sec>>1)|(x->tm_min<<5)|(x->tm_hour<<11);
	
	/* Bits: 0-4 = day (1-31), 5-8 = month (1-12), 9-15 = years (since 1980) */
	DateTime->dateword = x->tm_mday | ((x->tm_mon+1)<<5)
		| (((x->tm_year-80 > 0) ? x->tm_year-80 : 0) << 9);
	return true;
}

/*-----------------------------------------------------------------------*/
/**
 * Populate a DATETIME structure with file info.  Handle needs to be
 * validated before calling.  Return true on success.
 */
static bool GemDOS_GetFileInformation(int Handle, DATETIME *DateTime)
{
	struct stat filestat;

	if (stat(FileHandles[Handle].szActualName, &filestat) != 0)
		return false;

	return GemDOS_DateTime2Tos(filestat.st_mtime, DateTime);
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


/**
 * Convert potentially too long host filenames to 8.3 TOS filenames
 * by truncating extension and part before it, replacing invalid
 * GEMDOS file name characters with INVALID_CHAR + upcasing the result.
 * 
 * Matching them from the host file system should first try exact
 * case-insensitive match, and then with a pattern that takes into
 * account the conversion done in here.
 */
static void Convert2TOSName(char *source, char *dst)
{
	char *dot, *tmp, *src;
	int len;

	src = strdup(source); /* dup so that it can be modified */
	len = strlen(src);

	/* does filename have an extension? */
	dot = strrchr(src, '.');
	if (dot)
	{
		/* limit extension to 3 chars */
		if (src + len - dot > 3)
			dot[4] = '\0';

		/* if there are extra dots, convert them */
		for (tmp = src; tmp < dot; tmp++)
			if (*tmp == '.')
				*tmp = INVALID_CHAR;
	}

	/* does name now fit to 8 (+3) chars? */
	if (len <= 8 || (dot && len <= 12))
		strcpy(dst, src);
	else
	{
		/* name (still) too long, cut part before extension */
		strncpy(dst, src, 8);
		if (dot)
			strcpy(dst+8, dot);
		else
			dst[8] = '\0';
	}
	free(src);

	/* replace other invalid chars than '.' in filename */
	for (tmp = dst; *tmp; tmp++)
	{
		if (*tmp < 33 || *tmp > 126)
			*tmp = INVALID_CHAR;
		else
		{
			switch (*tmp)
			{
				case '*':
				case '/':
				case ':':
				case '?':
				case '\\':
				case '{':
				case '}':
					*tmp = INVALID_CHAR;
			}
		}
	}
	Str_ToUpper(dst);
	LOG_TRACE(TRACE_OS_GEMDOS, "host: %s -> GEMDOS: %s\n", source, dst);
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

	if (!GemDOS_DateTime2Tos(filestat.st_mtime, &DateTime))
		return -3;

	/* convert to atari-style uppercase */
	Convert2TOSName(file->d_name, pDTA->dta_name);

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
	const char *p=pat, *n=name;

	if (name[0] == '.')
		return false;           /* skip .* files */
	if (strcmp(pat,"*.*")==0)
		return true;            /* match everything */
	if (strcasecmp(pat,name)==0)
		return true;            /* exact case insensitive match */

	while (*n)
	{
		if (*p=='*')
		{
			while (*n && *n != '.')
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
				if (toupper(*p++) != toupper(*n++))
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
 * Initialize GemDOS/PC file system
 */
void GemDOS_Init(void)
{
	int i;
	bInitGemDOS = false;

	/* Clear handles structure */
	memset(FileHandles, 0, sizeof(FileHandles));
	/* Clear DTAs */
	for(i=0; i<MAX_DTAS_FILES; i++)
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
	for (i=0; i<MAX_FILE_HANDLES; i++)
	{
		/* Was file open? If so close it */
		if (FileHandles[i].bUsed)
			fclose(FileHandles[i].FileHandle);

		FileHandles[i].FileHandle = NULL;
		FileHandles[i].bUsed = false;
	}

	for (DTAIndex = 0; DTAIndex < MAX_DTAS_FILES; DTAIndex++)
	{
		ClearInternalDTA();
	}
	DTAIndex = 0;

	/* Reset */
	bInitGemDOS = false;
	CurrentDrive = nBootDrive;
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
		lpstrPath[iIndex] = tolower(lpstrPath[iIndex]);
	}

	/* Check the file/folder is accessible (security basis) */
	if (access(lpstrPath, F_OK) == 0 )
	{
		 /* If its a HDD identifier (or other emulated device) */
		if (iDrive > 1)
		{
			struct stat status;
			stat( lpstrPath, &status );
			if ( status.st_mode & S_IFDIR )
				bExist = true;
		}
	}

	return bExist;
}

/*-----------------------------------------------------------------------*/
/**
 * Routine to check if any emulated drive is present
 */
#if 0
static bool GEMDOS_IsHDDPresent(int iDrive)
{
	bool bPresent = false;

	if ((iDrive <= nNumDrives) && (iDrive > 1))
		if (emudrives[iDrive-2])
			bPresent = true;

	return bPresent;
}
#endif


/**
 * Determine upper limit of partitions that should be emulated.
 *
 * @return true if multiple GEMDOS partitions should be emulated, false otherwise
 */
static bool GemDOS_DetermineMaxPartitions(int *pnMaxDrives)
{
	struct dirent **files;
	int count, i;
	char letter;
	bool bMultiPartitions;

	*pnMaxDrives = 0;

	/* Scan through the main directory to see whether there are just single
	 * letter sub-folders there (then use multi-partition mode) or if
	 * arbitrary sub-folders are there (then use single-partition mode */
	count = scandir(ConfigureParams.HardDisk.szHardDiskDirectories[0], &files, 0, alphasort);
	if (count < 0)
	{
		perror("GemDOS_DetermineMaxPartitions");
		return false;
	}
	else if (count <= 2)
	{
		/* Empty directory Only "." and ".."), assume single partition mode */
		*pnMaxDrives = 1;
		bMultiPartitions = false;
	}
	else
	{
		bMultiPartitions = true;
		/* Check all files in the directory */
		for (i = 0; i < count; i++)
		{
			letter = toupper(files[i]->d_name[0]);
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
				*pnMaxDrives = 1;
				bMultiPartitions = false;
				break;
			}
			*pnMaxDrives = letter - 'C' + 1;
		}
	}

	if (*pnMaxDrives > MAX_HARDDRIVES)
		*pnMaxDrives = MAX_HARDDRIVES;

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
	bool bMultiPartitions;

	/* intialize data for harddrive emulation: */
	if (!GEMDOS_EMU_ON)
	{
		emudrives = malloc(MAX_HARDDRIVES * sizeof(EMULATEDDRIVE *));
		if (!emudrives)
		{
			perror("GemDOS_InitDrives");
			return;
		}
		memset(emudrives, 0, MAX_HARDDRIVES * sizeof(EMULATEDDRIVE *));
	}

	bMultiPartitions = GemDOS_DetermineMaxPartitions(&nMaxDrives);

	/* Now initialize all available drives */
	for(i = 0; i < nMaxDrives; i++)
	{
		// Create the letter equivilent string identifier for this drive
		char sDriveLetter[] = { PATHSEP, (char)('C' + i), '\0' };

		/* If single partition mode, skip to the right entry */
		if (!bMultiPartitions)
			i += nPartitions;

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
			strcat(emudrives[i]->hd_emulation_dir, sDriveLetter);

		/* drive number (C: = 2, D: = 3, etc.) */
		DriveNumber = 2 + i;

		// Check host file system to see if the drive folder for THIS
		// drive letter/number exists...
		if (GEMDOS_DoesHostDriveFolderExist(emudrives[i]->hd_emulation_dir, DriveNumber))
		{
			/* initialize current directory string, too (initially the same as hd_emulation_dir) */
			strcpy(emudrives[i]->fs_currpath, emudrives[i]->hd_emulation_dir);
			File_AddSlashToEndFileName(emudrives[i]->fs_currpath);    /* Needs trailing slash! */
			 /* If the GemDos Drive letter is free then */
			if (i >= nPartitions)
			{
				Log_Printf(LOG_INFO, "GEMDOS HDD emulation, %c: <-> %s.\n",
					   'A'+DriveNumber, emudrives[i]->hd_emulation_dir);
				emudrives[i]->drive_number = DriveNumber;
				nNumDrives = i + 3;
			}
			else	/* This letter has already been allocated to the one supported physical disk image */
			{
				Log_Printf(LOG_WARN, "Drive Letter %c is already mapped to HDD image (cannot map GEMDOS drive to %s).\n",
					   'A'+DriveNumber, emudrives[i]->hd_emulation_dir);
				free(emudrives[i]);
				emudrives[i] = NULL;
			}
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

	GemDOS_Reset();        /* Close all open files on emulated drive*/

	if (GEMDOS_EMU_ON)
	{
		for(i=0; i<MAX_HARDDRIVES; i++)
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
	unsigned int Addr;
	int i;
	bool bEmudrivesAvailable;

	/* Save/Restore the emudrives structure */
	bEmudrivesAvailable = (emudrives != NULL);
	MemorySnapShot_Store(&bEmudrivesAvailable, sizeof(bEmudrivesAvailable));
	if (bEmudrivesAvailable)
	{
		if (!bSave && !emudrives)
		{
			/* We're loading a memory snapshot, but the emudrives
			 * structure has not been malloc yet... let's do it now! */
			GemDOS_InitDrives();
		}

		for(i=0; i<MAX_HARDDRIVES; i++)
		{
			int bDummyDrive = false;
			if (!emudrives[i])
			{
				/* Allocate a dummy drive */
				emudrives[i] = malloc(sizeof(EMULATEDDRIVE));
				if (!emudrives[i])
					perror("GemDOS_MemorySnapShot_Capture");
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
		Addr = ((Uint8 *)pDTA - STRam);
		MemorySnapShot_Store(&Addr,sizeof(Addr));
	}
	else
	{
		MemorySnapShot_Store(&Addr,sizeof(Addr));
		pDTA = (DTA *)(STRam + Addr);
	}
	MemorySnapShot_Store(&CurrentDrive,sizeof(CurrentDrive));
	/* Don't save file handles as files may have changed which makes
	   it impossible to get a valid handle back */
	if (!bSave)
	{
		/* Clear file handles  */
		for(i=0; i<MAX_FILE_HANDLES; i++)
		{
			FileHandles[i].FileHandle = NULL;
			FileHandles[i].bUsed = false;
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
	for(i=0; i<MAX_FILE_HANDLES; i++)
	{
		if (!FileHandles[i].bUsed)
			return i;
	}

	/* Cannot open any more files, return error */
	return -1;
}

/*-----------------------------------------------------------------------*/
/**
 * Check ST handle is within our table range, return TRUE if not
 */
static bool GemDOS_IsInvalidFileHandle(int Handle)
{
	/* Check handle was valid with our handle table */
	if (Handle >= 0 && Handle < MAX_FILE_HANDLES
	    && FileHandles[Handle].bUsed)
	{
		return false;
	}
	/* invalid handle */
	return true;
}

/*-----------------------------------------------------------------------*/
/**
 * Find drive letter from a filename, eg C,D... and return as drive ID(C:2, D:3...)
 * returns the current drive number if none is specified.
 */
static int GemDOS_FindDriveNumber(char *pszFileName)
{
	/* Does have 'A:' or 'C:' etc.. at start of string? */
	if ((pszFileName[0] != '\0') && (pszFileName[1] == ':'))
	{
		if ((pszFileName[0] >= 'a') && (pszFileName[0] <= 'z'))
			return (pszFileName[0]-'a');
		else if ((pszFileName[0] >= 'A') && (pszFileName[0] <= 'Z'))
			return (pszFileName[0]-'A');
	}

	return CurrentDrive;
}

/*-----------------------------------------------------------------------*/
/**
 * Return drive ID(C:2, D:3 etc...) or -1 if not one of our emulation hard-drives
 */
static int GemDOS_IsFileNameAHardDrive(char *pszFileName)
{
	int DriveNumber;
	int n;

	/* Do we even have a hard-drive? */
	if (GEMDOS_EMU_ON)
	{
		/* Find drive letter (as number) */
		DriveNumber = GemDOS_FindDriveNumber(pszFileName);

		/* We've got support for multiple drives here... */
		if (DriveNumber > 1)	// If it is not a Floppy Drive
		{
			for (n=0; n<MAX_HARDDRIVES; n++)
			{
				/* Check if drive letter matches */
				if (emudrives[n] &&  DriveNumber == emudrives[n]->drive_number)
					return DriveNumber;
			}
		}
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
	struct dirent *entry;
	char *match = NULL;
	DIR *dir;
	
	dir = opendir(path);
	if (!dir)
		return NULL;

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS match '%s'%s in '%s'", name, pattern?" (pattern)":"", path);

	if (pattern)
	{
		while ((entry = readdir(dir)))
		{
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
			if (strcasecmp(name, entry->d_name) == 0)
			{
				match = strdup(entry->d_name);
				break;
			}
		}
	}
	closedir(dir);
	LOG_TRACE(TRACE_OS_GEMDOS, " -> '%s'\n", match);
	return match;
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
	bool modified;

	strcpy(name, origname);
	namelen = strlen(name);
	pathlen = strlen(path);

	/* append separator */
	if (pathlen >= maxlen)
		return false;
	path[pathlen++] = PATHSEP;
	path[pathlen] = '\0';
	
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
	 * Change name to a pattern that will match such host files
	 * and try again.
	 */

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
		/* "emulated.too" -> "emulated.too*" */
		name[namelen++] = '*';
		name[namelen] = '\0';
		modified = true;
	}
	/* catch potentially too long part before extension */
	if (namelen > 8 && name[8] == '.')
	{
		/* "emulated.too*" -> "emulated*.too*" */
		memmove(name+9, name+8, namelen-7);
		namelen++;
		name[8] = '*';
		modified = true;
	}
	else if (namelen == 8)
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
	strncat(path+pathlen, origname, maxlen-pathlen);
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
	int i;

	dstlen--;
	i = strlen(dstpath);
	for (dst = dstpath + i; *src && i < dstlen; dst++, src++, i++)
	{
		if (*src == '\\')
			*dst = PATHSEP;
		else
			*dst = *src;
	}
	*dst = '\0';
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
 * can be upto FILENAME_MAX long.  Plain GEMDOS paths themselves may be
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

	/* Is it a valid hard drive? */
	if (Drive < 2)
		return;

	/* Check for valid string */
	if (filename[0] == '\0')
		return;

	/* make sure that more convenient strncat() can be used
	 * on the destination string (it always null terminates
	 * unlike strncpy()).
	 */
	*pszDestName = 0;
	/* strcat writes n+1 chars, se decrease len */
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
	
	/* go through path directory components, advacing 'filename'
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
			strncat(pszDestName+len, filename, nDestNameLen-len);
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
 * GEMDOS Cauxin
 * Call 0x3
 */
#if 0
static bool GemDOS_Cauxin(Uint32 Params)
{
	Uint8 c;

	/* Wait here until a character is ready */
	while(!RS232_GetStatus())
		;

	/* And read character */
	RS232_ReadBytes(&c,1);
	Regs[REG_D0] = c;

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Cauxin() = 0x%x\n", (int)c);

	return true;
}
#endif

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Cauxout
 * Call 0x4
 */
#if 0
static bool GemDOS_Cauxout(Uint32 Params)
{
	Uint8 c;

	/* Get character from the stack */
	c = STMemory_ReadWord(Params);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Cauxout(0x%x)\n", (int)c);

	/* Send character to RS232 */
	RS232_TransferBytesTo(&c, 1);

	return true;
}
#endif

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Cprnout
 * Call 0x5
 */
#if 0
static bool GemDOS_Cprnout(Uint32 Params)
{
	Uint8 c;

	/* Send character to printer(or file) */
	c = STMemory_ReadWord(Params);
	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Cprnout(0x%x)\n", (int)c);
	Printer_TransferByteTo(c);
	Regs[REG_D0] = -1;                /* Printer OK */

	return true;
}
#endif

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Set drive (0=A,1=B,2=C etc...)
 * Call 0xE
 */
static bool GemDOS_SetDrv(Uint32 Params)
{
	/* Read details from stack for our own use */
	CurrentDrive = STMemory_ReadWord(Params);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Dsetdrv(0x%x)\n", (int)CurrentDrive);

	/* Still re-direct to TOS */
	return false;
}

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Cprnos
 * Call 0x11
 */
#if 0
static bool GemDOS_Cprnos(Uint32 Params)
{
	/* printer status depends if printing is enabled or not... */
	if (ConfigureParams.Printer.bEnablePrinting)
		Regs[REG_D0] = -1;              /* Printer OK */
	else
		Regs[REG_D0] = 0;               /* printer not ready if printing disabled */

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Cprnos() = 0x%x\n", Regs[REG_D0]);

	return true;
}
#endif

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Cauxis
 * Call 0x12
 */
#if 0
static bool GemDOS_Cauxis(Uint32 Params)
{
	/* Read our RS232 state */
	if (RS232_GetStatus())
		Regs[REG_D0] = -1;              /* Chars waiting */
	else
		Regs[REG_D0] = 0;

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Cauxis() = 0x%x\n", Regs[REG_D0]);

	return true;
}
#endif

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Cauxos
 * Call 0x13
 */
#if 0
static bool GemDOS_Cauxos(Uint32 Params)
{
	Regs[REG_D0] = -1;                /* Device ready */

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Cauxos() = 0x%x\n", Regs[REG_D0]);

	return true;
}
#endif

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Set Disk Transfer Address (DTA)
 * Call 0x1A
 */
static bool GemDOS_SetDTA(Uint32 Params)
{
	/*  Look up on stack to find where DTA is */
	Uint32 nDTA = STMemory_ReadLong(Params);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fsetdta(0x%x)\n", nDTA);

	if (STMemory_ValidArea(nDTA, sizeof(DTA)))
	{
		/* Store as PC pointer */
		pDTA = (DTA *)STRAM_ADDR(nDTA);
	}
	else
	{
		pDTA = NULL;
		Log_Printf(LOG_WARN, "GEMDOS Fsetdta() failed due to invalid DTA address 0x%x\n", nDTA);
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
	int Drive;
	Uint32 Address;

	Address = STMemory_ReadLong(Params);
	Drive = STMemory_ReadWord(Params+SIZE_LONG);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Dfree(0x%x, %i)\n", Address, Drive);

	/* is it our drive? */
	if ((Drive == 0 && CurrentDrive >= 2) || Drive >= 3)
	{
		/* Check that write is requested to valid memory area */
		if (!STMemory_ValidArea(Address, 16))
		{
			Log_Printf(LOG_WARN, "GEMDOS Dfree() failed due to invalid RAM range 0x%x+%i\n", Address, 16);
			Regs[REG_D0] = GEMDOS_ERANGE;
			return true;
		}
		/* FIXME: Report actual free drive space */
		STMemory_WriteLong(Address,  16*1024);           /* free clusters (mock 16 Mb) */
		STMemory_WriteLong(Address+SIZE_LONG, 32*1024 ); /* total clusters (mock 32 Mb) */

		STMemory_WriteLong(Address+SIZE_LONG*2, 512 );   /* bytes per sector */
		STMemory_WriteLong(Address+SIZE_LONG*3, 2 );     /* sectors per cluster */
		return true;
	}
	/* redirect to TOS */
	return false;
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
	pDirName = (char *)STRAM_ADDR(STMemory_ReadLong(Params));

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Dcreate(\"%s\")\n", pDirName);

	Drive = GemDOS_IsFileNameAHardDrive(pDirName);

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
		Regs[REG_D0] = GEMDOS_EACCDN;        /* Access denied */
	
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
	pDirName = (char *)STRAM_ADDR(STMemory_ReadLong(Params));

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Ddelete(\"%s\")\n", pDirName);

	Drive = GemDOS_IsFileNameAHardDrive(pDirName);

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
		Regs[REG_D0] = GEMDOS_EACCDN;        /* Access denied */
	
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
	pDirName = (char *)STRAM_ADDR(STMemory_ReadLong(Params));

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Dsetpath(\"%s\")\n", pDirName);

	Drive = GemDOS_IsFileNameAHardDrive(pDirName);

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

	// Remove trailing slashes (stat on Windows does not like that)
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
		if (!File_DirectoryExists(szActualFileName))
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
	int Drive,Index,Mode;

	/* Find filename */
	pszFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params));
	Mode = STMemory_ReadWord(Params+SIZE_LONG);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fcreate(\"%s\", 0x%x)\n", pszFileName, Mode);

	Drive = GemDOS_IsFileNameAHardDrive(pszFileName);

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
	if (Index==-1)
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
		/* Tag handle table entry as used and return handle */
		FileHandles[Index].bUsed = true;
		snprintf(FileHandles[Index].szActualName,
			 sizeof(FileHandles[Index].szActualName),
			 "%s", szActualFileName);

		/* Return valid ST file handle from our range (from BASE_FILEHANDLE upwards) */
		Regs[REG_D0] = Index+BASE_FILEHANDLE;
		LOG_TRACE(TRACE_OS_GEMDOS, "-> FD %d (%s)\n", Index,
			  Mode & GEMDOS_FILE_ATTRIB_READONLY ? "read-only":"read/write");
		return true;
	}

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
	const char *ModeStr;
	/* convert atari modes to stdio modes */
	struct {
		const char *mode;
		const char *desc;
	} Modes[] = {
		{ "rb",	 "read-only" },
		/* FIXME: is actually read/write as "wb" would truncate */
		{ "rb+", "write-only" },
		{ "rb+", "read/write" },
		{ "rb+", "read/write" }
	};
	int Drive, Index, Mode;

	/* Find filename */
	pszFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params));
	Mode = STMemory_ReadWord(Params+SIZE_LONG);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fopen(\"%s\", 0x%x)\n", pszFileName, Mode);

	Drive = GemDOS_IsFileNameAHardDrive(pszFileName);

	if (!ISHARDDRIVE(Drive))
	{
		/* redirect to TOS */
		return false;
	}

	/* And convert to hard drive filename */
	GemDOS_CreateHardDriveFileName(Drive, pszFileName,
	                            szActualFileName, sizeof(szActualFileName));

	/* Find slot to store file handle, as need to return WORD handle for ST  */
	Index = GemDOS_FindFreeFileHandle();
	if (Index == -1)
	{
		/* No free handles, return error code */
		Regs[REG_D0] = GEMDOS_ENHNDL;       /* No more handles */
		return true;
	}

	if (ConfigureParams.HardDisk.nWriteProtection == WRITEPROT_ON)
	{
		/* force all accesses to be read-only */
		ModeStr = Modes[0].mode;
	}
	else
	{
		/* GEMDOS mount can be written, try open in requested mode */
		ModeStr = Modes[Mode&0x03].mode;
	}

	/* FIXME: Open file
	 * - fopen() modes don't allow write-only mode without truncating
	 *   which would be needed to implement mode 1 (write-only) correctly.
	 *   Fixing this requires using open() and file descriptors instead
	 *   of fopen() and FILE* pointers, but Windows doesn't support that.
	 */
	FileHandles[Index].FileHandle =  fopen(szActualFileName, ModeStr);

	if (FileHandles[Index].FileHandle != NULL)
	{
		/* Tag handle table entry as used and return handle */
		FileHandles[Index].bUsed = true;
		snprintf(FileHandles[Index].szActualName,
			 sizeof(FileHandles[Index].szActualName),
			 "%s", szActualFileName);

		/* Return valid ST file handle from our range (BASE_FILEHANDLE upwards) */
		Regs[REG_D0] = Index+BASE_FILEHANDLE;
		LOG_TRACE(TRACE_OS_GEMDOS, "-> FD %d (%s)\n",
			  Index, Modes[Mode&0x03].desc);
		return true;
	}

	if (errno == EACCES || errno == EROFS ||
	    errno == EPERM || errno == EISDIR)
	{
		Log_Printf(LOG_WARN, "GEMDOS missing %s permission to file '%s'\n",
			   Modes[Mode&0x03].desc, szActualFileName);
		Regs[REG_D0] = GEMDOS_EACCDN;
		return true;
	}
	if (errno == ENOTDIR || GemDOS_FilePathMissing(szActualFileName))
	{
		/* Path not found */
		Regs[REG_D0] = GEMDOS_EPTHNF;
		return true;
	}
	/* File not found / error opening */
	Regs[REG_D0] = GEMDOS_EFILNF;
	return true;
}

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Close file
 * Call 0x3E
 */
static bool GemDOS_Close(Uint32 Params)
{
	int Handle;

	/* Find our handle - may belong to TOS */
	Handle = STMemory_ReadWord(Params);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fclose(%i)\n", Handle);

	Handle -= BASE_FILEHANDLE;

	/* Check handle was valid */
	if (GemDOS_IsInvalidFileHandle(Handle))
	{
		/* no, assume it was TOS one -> redirect */
		return false;
	}
	
	/* Close file and free up handle table */
	fclose(FileHandles[Handle].FileHandle);
	FileHandles[Handle].bUsed = false;

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
	long CurrentPos, FileSize, nBytesRead, nBytesLeft;
	Uint32 Addr;
	Uint32 Size;
	int Handle;

	/* Read details from stack */
	Handle = STMemory_ReadWord(Params);
	Size = STMemory_ReadLong(Params+SIZE_WORD);
	Addr = STMemory_ReadLong(Params+SIZE_WORD+SIZE_LONG);
	pBuffer = (char *)STRAM_ADDR(Addr);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fread(%i, %i, 0x%x)\n", 
	          Handle, Size, Addr);

	Handle -= BASE_FILEHANDLE;

	/* Check handle was valid */
	if (GemDOS_IsInvalidFileHandle(Handle))
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
	CurrentPos = ftell(FileHandles[Handle].FileHandle);
	fseek(FileHandles[Handle].FileHandle, 0, SEEK_END);
	FileSize = ftell(FileHandles[Handle].FileHandle);
	fseek(FileHandles[Handle].FileHandle, CurrentPos, SEEK_SET);

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
	if (!STMemory_ValidArea(Addr, Size))
	{
		Log_Printf(LOG_WARN, "GEMDOS Fread() failed due to invalid RAM range 0x%x+%i\n", Addr, Size);
		Regs[REG_D0] = GEMDOS_ERANGE;
		return true;
	}
	/* And read data in */
	nBytesRead = fread(pBuffer, 1, Size, FileHandles[Handle].FileHandle);
	
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

	/* Read details from stack */
	Handle = STMemory_ReadWord(Params);
	Size = STMemory_ReadLong(Params+SIZE_WORD);
	Addr = STMemory_ReadLong(Params+SIZE_WORD+SIZE_LONG);
	pBuffer = (char *)STRAM_ADDR(Addr);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fwrite(%i, %i, 0x%x)\n", 
	          Handle, Size, Addr);

	Handle -= BASE_FILEHANDLE;

	/* Check handle was valid */
	if (GemDOS_IsInvalidFileHandle(Handle))
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
	if (!STMemory_ValidArea(Addr, Size))
	{
		Log_Printf(LOG_WARN, "GEMDOS Fwrite() failed due to invalid RAM range 0x%x+%i\n", Addr, Size);
		Regs[REG_D0] = GEMDOS_ERANGE;
		return true;
	}

	nBytesWritten = fwrite(pBuffer, 1, Size, FileHandles[Handle].FileHandle);
	if (ferror(FileHandles[Handle].FileHandle))
	{
		Log_Printf(LOG_WARN, "GEMDOS failed to write to '%s'\n",
			   FileHandles[Handle].szActualName );
		Regs[REG_D0] = GEMDOS_EACCDN;      /* Access denied (ie read-only) */
	}
	else
	{
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
	pszFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params));

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fdelete(\"%s\")\n", pszFileName);

	Drive = GemDOS_IsFileNameAHardDrive(pszFileName);

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
		Regs[REG_D0] = GEMDOS_EFILNF;       /* File not found */

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

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fseek(%li, %i, %i)\n", Offset, Handle, Mode);

	Handle -= BASE_FILEHANDLE;

	/* Check handle was valid */
	if (GemDOS_IsInvalidFileHandle(Handle))
	{
		/* assume it was TOS one -> redirect */
		return false;
	}

	fhndl = FileHandles[Handle].FileHandle;

	/* Save old position in file */
	nOldPos = ftell(fhndl);

	/* Determine the size of the file */
	fseek(fhndl, 0L, SEEK_END);
	nFileSize = ftell(fhndl);

	switch (Mode)
	{
	 case 0: nDestPos = Offset; break; /* positive offset */
	 case 1: nDestPos = nOldPos + Offset; break;
	 case 2: nDestPos = nFileSize + Offset; break; /* negative offset */
	 default:
		/* Restore old position and return error */
		fseek(fhndl, nOldPos, SEEK_SET);
		Regs[REG_D0] = GEMDOS_EINVFN;
		return true;
	}

	if (nDestPos < 0 || nDestPos > nFileSize)
	{
		/* Restore old position and return error */
		fseek(fhndl, nOldPos, SEEK_SET);
		Regs[REG_D0] = GEMDOS_ERANGE;
		return true;
	}

	/* Seek to new position and return offset from start of file */
	fseek(fhndl, nDestPos, SEEK_SET);
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
	psFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params));
	nDrive = GemDOS_IsFileNameAHardDrive(psFileName);

	nRwFlag = STMemory_ReadWord(Params+SIZE_LONG);
	nAttrib = STMemory_ReadWord(Params+SIZE_LONG+SIZE_WORD);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fattrib(\"%s\", %d, 0x%x)\n",
	          psFileName, nRwFlag, nAttrib);

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

	/* write or auto protected device? */
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
	Regs[REG_D0] = GEMDOS_EACCDN;         /* Acces denied */
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

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Dgetpath(0x%x, %i)\n", Address, (int)Drive);

	/* is it our drive? */
	if ((Drive == 0 && CurrentDrive >= 2) || (Drive >= 3 /*&& Drive <= nNumDrives*/))
	{
		char path[MAX_GEMDOS_PATH];
		int i,len,c;

		if (Drive == 0)
			Drive = CurrentDrive;
		else
			Drive--;

		if (emudrives[Drive-2] == NULL)
		{
			return false;
		}

		*path = '\0';
		strncat(path,&emudrives[Drive-2]->fs_currpath[strlen(emudrives[Drive-2]->hd_emulation_dir)], sizeof(path)-1);

		// convert it to ST path (DOS)
		File_CleanFileName(path);
		len = strlen(path);
		/* Check that write is requested to valid memory area */
		if (!STMemory_ValidArea(Address, len))
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
		LOG_TRACE(TRACE_OS_GEMDOS, "GemDOS_GetDir (%d) = %s\n", Drive, path );

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

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Pexec(%i, ...)\n", Mode);

	/* Re-direct as needed */
	switch(Mode)
	{
	 case 0:      /* Load and go */
	 case 3:      /* Load, don't go */
		pszFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
		Drive = GemDOS_IsFileNameAHardDrive(pszFileName);
		
		/* If not using A: or B:, use my own routines to load */
		if (ISHARDDRIVE(Drive))
		{
			/* Redirect to cart' routine at address 0xFA1000 */
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
	Uint32 nDTA;
	int Index;
	int ret;

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fsnext()\n");

	/* Refresh pDTA pointer (from the current basepage) */
	nDTA = STMemory_ReadLong(STMemory_ReadLong(act_pd)+32);

	if (!STMemory_ValidArea(nDTA, sizeof(DTA)))
	{
		pDTA = NULL;
		Log_Printf(LOG_WARN, "GEMDOS Fsnext() failed due to invalid DTA address 0x%x\n", nDTA);
		Regs[REG_D0] = GEMDOS_EINTRN;    /* "internal error */
		return true;
	}
	pDTA = (DTA *)STRAM_ADDR(nDTA);

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
	Uint32 nDTA;
	int Drive;
	DIR *fsdir;
	int i,j,count;

	/* Find filename to search for */
	pszFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params));
	nAttrSFirst = STMemory_ReadWord(Params+SIZE_LONG);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fsfirst(\"%s\", 0x%x)\n", pszFileName, nAttrSFirst);

	Drive = GemDOS_IsFileNameAHardDrive(pszFileName);
	if (!ISHARDDRIVE(Drive))
	{
		/* redirect to TOS */
		return false;
	}

	/* Convert to hard drive filename */
	GemDOS_CreateHardDriveFileName(Drive, pszFileName,
		                    szActualFileName, sizeof(szActualFileName));

	/* Refresh pDTA pointer (from the current basepage) */
	nDTA = STMemory_ReadLong(STMemory_ReadLong(act_pd)+32);

	if (!STMemory_ValidArea(nDTA, sizeof(DTA)))
	{
		pDTA = NULL;
		Log_Printf(LOG_WARN, "GEMDOS Fsfirst() failed due to invalid DTA address 0x%x\n", nDTA);
		Regs[REG_D0] = GEMDOS_EINTRN;    /* "internal error */
		return true;
	}
	pDTA = (DTA *)STRAM_ADDR(nDTA);

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
	DTAIndex&=(MAX_DTAS_FILES-1);
	
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
	pszOldFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
	pszNewFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD+SIZE_LONG));

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Frename(\"%s\", \"%s\")\n", pszOldFileName, pszNewFileName);

	NewDrive = GemDOS_IsFileNameAHardDrive(pszNewFileName);
	OldDrive = GemDOS_IsFileNameAHardDrive(pszOldFileName);
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
	if ( rename(szOldActualFileName,szNewActualFileName)==0 )
		Regs[REG_D0] = GEMDOS_EOK;
	else
		Regs[REG_D0] = GEMDOS_EACCDN;        /* Access denied */
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

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fdatime(0x%x, %i, %i)\n", pBuffer,
	          Handle, Flag);

	Handle -= BASE_FILEHANDLE;

	/* Check handle was valid */
	if (GemDOS_IsInvalidFileHandle(Handle))
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
		if (STMemory_ValidArea(pBuffer, 4))
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


#if ENABLE_TRACING
/*-----------------------------------------------------------------------*/
/**
 * Map GEMDOS call opcodes to their names
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
		"", /* 0C */
		"", /* 0D */
		"Dsetdrv",
		"", /* 0F */
		"Cconos",
		"Cprnos",
		"Cauxis",
		"Cauxos",
		"Maddalt",
		"", /* 15 */
		"", /* 16 */
		"", /* 17 */
		"", /* 18 */
		"Dgetdrv",
		"Fsetdta",
		"", /* 1B */
		"", /* 1C */
		"", /* 1D */
		"", /* 1E */
		"", /* 1F */
		"Super",
		"", /* 21 */
		"", /* 22 */
		"", /* 23 */
		"", /* 24 */
		"", /* 25 */
		"", /* 26 */
		"", /* 27 */
		"", /* 28 */
		"", /* 29 */
		"Tgetdate",
		"Tsetdate",
		"Tgettime",
		"Tsettime",
		"", /* 2E */
		"Fgetdta",
		"Sversion",
		"Ptermres",
		"", /* 32 */
		"", /* 33 */
		"", /* 34 */
		"", /* 35 */
		"Dfree",
		"", /* 37 */
		"", /* 38 */
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
		"", /* 4D */
		"Fsfirst",
		"Fsnext",
		"", /* 50 */
		"", /* 51 */
		"", /* 52 */
		"", /* 53 */
		"", /* 54 */
		"", /* 55 */
		"Frename",
		"Fdatime"
	};
	if (opcode < ARRAYSIZE(names))
		return names[opcode];
	return "MiNT call?";
}
#endif


/*-----------------------------------------------------------------------*/
/**
 * Run GEMDos call, and re-direct if need to. Used to handle hard disk emulation etc...
 * This sets the condition codes (in SR), which are used in the 'cart_asm.s' program to
 * decide if we need to run old GEM vector, or PExec or nothing.
 *
 * This method keeps the stack and other states consistant with the original ST which is very important
 * for the PExec call and maximum compatibility through-out
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
		Params = Regs[REG_A7]+SIZE_WORD+SIZE_LONG;  /* super stack */
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
	 /*
	 case 0x3:
		Finished = GemDOS_Cauxin(Params);
		break;
	 */
	 /*
	 case 0x4:
		Finished = GemDOS_Cauxout(Params);
		break;
	 */
	 /* direct printing via GEMDOS */
	 /*
	 case 0x5:
		Finished = GemDOS_Cprnout(Params);
		break;
	 */
	 case 0xe:
		Finished = GemDOS_SetDrv(Params);
		break;
	 /* Printer status  */
	 /*
	 case 0x11:
		Finished = GemDOS_Cprnos(Params);
		break;
	 */
	 /*
	 case 0x12:
		Finished = GemDOS_Cauxis(Params);
		break;
	 */
	 /*
	 case 0x13:
		Finished = GemDOS_Cauxos(Params);
		break;
	 */
	 case 0x1a:
		Finished = GemDOS_SetDTA(Params);
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
	 case 0x3b:
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
	 case 0x47:
		Finished = GemDOS_GetDir(Params);
		break;
	 case 0x4b:
		/* Either false or CALL_PEXEC_ROUTINE */
		Finished = GemDOS_Pexec(Params);
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
	 default:
		LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS call 0x%X (%s)\n",
			  GemDOSCall, GemDOS_Opcode2Name(GemDOSCall));
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
			Statusbar_EnableHDLed();
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
 * GemDOS_Boot - routine called on the first occurence of the gemdos opcode.
 * (this should be in the cartridge bootrom)
 * Sets up our gemdos handler (or, if we don't need one, just turn off keyclicks)
 */
void GemDOS_Boot(void)
{
	bInitGemDOS = true;

	LOG_TRACE(TRACE_OS_GEMDOS, "Gemdos_Boot()\n" );

	/* install our gemdos handler, if -e or --harddrive option used */
	if (!GEMDOS_EMU_ON)
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

	/* Save old GEMDOS handler adress */
	STMemory_WriteLong(CART_OLDGEMDOS, STMemory_ReadLong(0x0084));
	/* Setup new GEMDOS handler, see "cart_asm.s" */
	STMemory_WriteLong(0x0084, CART_GEMDOS);
}
