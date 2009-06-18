/*
  Hatari - gemdos.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  GEMDOS intercept routines.
  These are used mainly for hard drive redirection of high level file routines.

  Now case is handled by using glob. See the function
  GemDOS_CreateHardDriveFileName for that. It also knows about symlinks.
  A filename is recognized on its eight first characters, do not try to
  push this too far, or you'll get weirdness ! (But I can even run programs
  directly from a mounted cd in lower cases, so I guess it's working well !).

  Bugs/things to fix:
  * RS232
  * rmdir routine, can't remove dir with files in it. (another tos/unix difference)
  * Fix bugs, there are probably a few lurking around in here..
*/
const char Gemdos_fileid[] = "Hatari gemdos.c : " __DATE__ " " __TIME__;

#include <config.h>

#include <sys/stat.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>

#if HAVE_GLOB_H
#include <glob.h>
#endif

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

#define ENABLE_SAVING             /* Turn on saving stuff */

/* GLOB_ONLYDIR is a GNU extension for the glob() function and not defined
 * on some systems. We should probably use something different for this
 * case, but at the moment it we simply define it as 0... */
#ifndef GLOB_ONLYDIR
# define GLOB_ONLYDIR 0
#endif

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
  Uint16 word1;
  Uint16 word2;
} DATETIME;

typedef struct
{
	bool bUsed;
	FILE *FileHandle;
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



/* Poor Windows (and maybe other systems) do not have a glob() function... */
#if !HAVE_GLOB_H

typedef struct
{
    size_t gl_pathc;    /* Count of paths matched so far  */
    char **gl_pathv;    /* List of matched pathnames.  */
    size_t gl_offs;     /* Slots to reserve in `gl_pathv'.  */
} glob_t;

static int glob(const char *pattern, int flags,
                int errfunc(const char *epath, int eerrno),
                glob_t *pglob)
{
	/* Just a quick hack to keep Hatari happy... */
	pglob->gl_pathv = malloc(1 * sizeof(void *));
	pglob->gl_pathv[0] = NULL;
	pglob->gl_pathc = 0;
	return 0;
}

static void globfree(glob_t *pglob)
{
	free(pglob->gl_pathv);
}

#endif  /* HAVE_GLOB_H */


#if defined(WIN32) && !defined(mkdir)
#define mkdir(name,mode) mkdir(name)
#endif  /* WIN32 */

#ifndef S_IRGRP
#define S_IRGRP 0
#define S_IROTH 0
#endif



/*-------------------------------------------------------*/
/**
 * Routines to convert time and date to MSDOS format.
 * Originally from the STonX emulator. (cheers!)
 */
static Uint16 GemDOS_Time2dos(time_t t)
{
	struct tm *x;

	x = localtime(&t);

	if (x == NULL)
		return 0;

	return (x->tm_sec>>1)|(x->tm_min<<5)|(x->tm_hour<<11);
}

static Uint16 GemDOS_Date2dos(time_t t)
{
	struct tm *x;

	x = localtime(&t);

	if (x == NULL)
		return 0;

	return x->tm_mday | ((x->tm_mon+1)<<5)
	       | (((x->tm_year-80 > 0) ? x->tm_year-80 : 0) << 9);
}


/*-----------------------------------------------------------------------*/
/**
 * Populate a DATETIME structure with file info
 */
static bool GemDOS_GetFileInformation(char *name, DATETIME *DateTime)
{
	struct stat filestat;
	int n;
	struct tm *x;

	n = stat(name, &filestat);
	if (n != 0)
		return FALSE;

	x = localtime(&filestat.st_mtime);
	if (x == NULL)
		return FALSE;

	DateTime->word1 = 0;
	DateTime->word2 = 0;

	DateTime->word1 |= (x->tm_mday & 0x1F);         /* 5 bits */
	DateTime->word1 |= (x->tm_mon & 0x0F)<<5;       /* 4 bits */
	DateTime->word1 |= (((x->tm_year-80>0)?x->tm_year-80:0) & 0x7F)<<9;      /* 7 bits*/

	DateTime->word2 |= (x->tm_sec & 0x1F);          /* 5 bits */
	DateTime->word2 |= (x->tm_min & 0x3F)<<5;       /* 6 bits */
	DateTime->word2 |= (x->tm_hour & 0x1F)<<11;     /* 5 bits */

	return TRUE;
}


/*-----------------------------------------------------------------------*/
/**
 * Convert from FindFirstFile/FindNextFile attribute to GemDOS format
 */
static unsigned char GemDOS_ConvertAttribute(mode_t mode)
{
	unsigned char Attrib = 0;

	/* Directory attribute */
	if (S_ISDIR(mode))
		Attrib |= GEMDOS_FILE_ATTRIB_SUBDIRECTORY;

	/* Read-only attribute */
	if (!(mode & S_IWUSR))
		Attrib |= GEMDOS_FILE_ATTRIB_READONLY;

	/* TODO: Other attributes like GEMDOS_FILE_ATTRIB_HIDDEN ? */

	return Attrib;
}


/*-----------------------------------------------------------------------*/
/**
 * Populate the DTA buffer with file info.
 * @return   0 if entry is ok, 1 if entry should be skipped, < 0 for errors.
 */
static int PopulateDTA(char *path, struct dirent *file)
{
	char tempstr[MAX_GEMDOS_PATH];
	struct stat filestat;
	int n;
	int nFileAttr;

	snprintf(tempstr, sizeof(tempstr), "%s%c%s", path, PATHSEP, file->d_name);
	n = stat(tempstr, &filestat);
	if (n != 0)
	{
		perror(tempstr);
		return -1;   /* return on error */
	}

	if (!pDTA)
		return -2;   /* no DTA pointer set */

	/* Check file attributes (check is done according to the Profibuch */
	nFileAttr = GemDOS_ConvertAttribute(filestat.st_mode);
	if (nFileAttr != 0 && !((nAttrSFirst|0x21) & nFileAttr))
		return 1;

	Str_ToUpper(file->d_name);    /* convert to atari-style uppercase */
	strncpy(pDTA->dta_name,file->d_name,TOS_NAMELEN); /* FIXME: better handling of long file names */
	do_put_mem_long(pDTA->dta_size, filestat.st_size);
	do_put_mem_word(pDTA->dta_time, GemDOS_Time2dos(filestat.st_mtime));
	do_put_mem_word(pDTA->dta_date, GemDOS_Date2dos(filestat.st_mtime));
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
	InternalDTAs[DTAIndex].bUsed = FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * Match a file to a dir mask.
 */
static int match (char *pat, char *name)
{
	char *p=pat, *n=name;

	if (name[0] == '.')
		return FALSE;                   /* no .* files */
	if (strcmp(pat,"*.*")==0)
		return TRUE;
	if (strcasecmp(pat,name)==0)
		return TRUE;

	for (;*n;)
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
					return FALSE;
			}
		}
	}

	return (*p == 0);     /* The name matches the pattern if it ends here, too */
}


/*-----------------------------------------------------------------------*/
/**
 * Parse directory from sfirst mask
 * - e.g.: input:  "hdemudir/auto/mask*.*" outputs: "hdemudir/auto"
 */
static void fsfirst_dirname(char *string, char *newstr)
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
 * Parse directory mask, e.g. "*.*"
 */
static void fsfirst_dirmask(char *string, char *newstr)
{
	int i=0;

	while (string[i] != '\0')
		i++;   /* go to end of string */
	while (i && string[i] != PATHSEP)
		i--;   /* find last slash */
	if (string[i] == PATHSEP)
		i++;
	while (string[i] != '\0')
		*newstr++ = string[i++]; /* go to end of string */
	*newstr = '\0';
}


/*-----------------------------------------------------------------------*/
/**
 * Initialize GemDOS/PC file system
 */
void GemDOS_Init(void)
{
	int i;
	bInitGemDOS = FALSE;

	/* Clear handles structure */
	memset(FileHandles, 0, sizeof(FILE_HANDLE)*MAX_FILE_HANDLES);
	/* Clear DTAs */
	for(i=0; i<MAX_DTAS_FILES; i++)
	{
		InternalDTAs[i].bUsed = FALSE;
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
		FileHandles[i].bUsed = FALSE;
	}

	for (DTAIndex = 0; DTAIndex < MAX_DTAS_FILES; DTAIndex++)
	{
		ClearInternalDTA();
	}
	DTAIndex = 0;

	/* Reset */
	bInitGemDOS = FALSE;
	CurrentDrive = nBootDrive;
	pDTA = NULL;
}

/*-----------------------------------------------------------------------*/
/**
 * Routine to check the Host OS HDD path for a Drive letter sub folder
 */
static bool GEMDOS_DoesHostDriveFolderExist(char* lpstrPath, int iDrive)
{
	bool bExist = FALSE;

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
				bExist = TRUE;
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
	bool bPresent = FALSE;

	if ((iDrive <= nNumDrives) && (iDrive > 1))
		if (emudrives[iDrive-2])
			bPresent = TRUE;

	return bPresent;
}
#endif


/**
 * Determine upper limit of partitions that should be emulated
 */
static int GemDOS_DetermineMaxPartitions(void)
{
	struct dirent **files;
	int nMaxDrives = 0, count, i;

	/* Scan through the main directory to see whether there are just single
	 * letter sub-folders there (then use multi-partition mode) or if
	 * arbitrary sub-folders are there (then use single-partition mode */
	count = scandir(ConfigureParams.HardDisk.szHardDiskDirectories[0], &files, 0, alphasort);
	if (count < 0)
	{
		perror("GemDOS_DetermineMaxPartitions");
		return 0;
	}
	else if (count <= 2)
	{
		/* Empty directory Only "." and ".."), assume single partition mode */
		nMaxDrives = 1;
	}
	else
	{
		/* Check all files in the directory */
		for (i = 0; i < count; i++)
		{
			if (strcmp(files[i]->d_name, ".") == 0 || strcmp(files[i]->d_name, "..") == 0)
			{
				/* Ignore "." and ".." */
				--nMaxDrives;
				continue;
			}
			if (strlen(files[i]->d_name) != 1 || !isalpha(files[i]->d_name[0]))
			{
				/* There is a folder with more than one letter...
				 * ... so use single partition mode! */
				nMaxDrives = 1;
				break;
			}
			nMaxDrives = toupper(files[i]->d_name[0]) - 'C' + 1;
		}
	}

	if (nMaxDrives > MAX_HARDDRIVES)
		nMaxDrives = MAX_HARDDRIVES;

	/* Free file list */
	for (i = 0; i < count; i++)
		free(files[i]);
	free(files);

	return nMaxDrives;
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

	nMaxDrives = GemDOS_DetermineMaxPartitions();

	/* Now initialize all available drives */
	for(i = 0; i < nMaxDrives; i++)
	{
		// Create the letter equivilent string identifier for this drive
		char sDriveLetter[] = { PATHSEP, (char)('C' + i), '\0' };

		emudrives[i] = malloc(sizeof(EMULATEDDRIVE));
		if (!emudrives[i])
		{
			perror("GemDOS_InitDrives");
			continue;
		}

		/* set drive number (C: = 2, D: = 3, etc.) */
		if (nMaxDrives == 1)
			emudrives[i]->hd_letter = 2 + nPartitions + i;
		else
			emudrives[i]->hd_letter = 2 + i;

		/* set emulation directory string */
		strcpy(emudrives[i]->hd_emulation_dir, ConfigureParams.HardDisk.szHardDiskDirectories[0]);

		/* remove trailing slash, if any in the directory name */
		File_CleanFileName(emudrives[i]->hd_emulation_dir);

		/* Add Requisit Folder ID */
		if (nMaxDrives > 1)
			strcat(emudrives[i]->hd_emulation_dir, sDriveLetter);

		// Check host file system to see if the drive folder for THIS
		// drive letter/number exists...
		if (GEMDOS_DoesHostDriveFolderExist(emudrives[i]->hd_emulation_dir,emudrives[i]->hd_letter))
		{
			/* initialize current directory string, too (initially the same as hd_emulation_dir) */
			strcpy(emudrives[i]->fs_currpath, emudrives[i]->hd_emulation_dir);
			File_AddSlashToEndFileName(emudrives[i]->fs_currpath);    /* Needs trailing slash! */
			 /* If the GemDos Drive letter is free then */
			if (i >= nPartitions)
			{
				Log_Printf(LOG_INFO, "Hard drive emulation, %c: <-> %s.\n",
						emudrives[i]->hd_letter + 'A', emudrives[i]->hd_emulation_dir);
				nNumDrives = i + 3;
			}
			else	/* This letter has allready been allocated to the one supported physical disk image */
			{
				Log_Printf(LOG_INFO, "Drive Letter %c is already mapped to HDD image (cannot map GEM DOS drive to %s).\n",
						emudrives[i]->hd_letter + 'A', emudrives[i]->hd_emulation_dir);
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
			MemorySnapShot_Store(&emudrives[i]->hd_letter,
			                     sizeof(emudrives[i]->hd_letter));
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
			FileHandles[i].bUsed = FALSE;
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
	bool bInvalidHandle=FALSE;

	/* Check handle was valid with our handle table */
	if ((Handle < 0) || (Handle >= MAX_FILE_HANDLES))
		bInvalidHandle = TRUE;
	else if (!FileHandles[Handle].bUsed)
		bInvalidHandle = TRUE;

	return bInvalidHandle;
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
	int DriveLetter;
	int n;

	/* Do we even have a hard-drive? */
	if (GEMDOS_EMU_ON)
	{
		/* Find drive letter (as number) */
		DriveLetter = GemDOS_FindDriveNumber(pszFileName);

		/* We've got support for multiple drives here... */
		if (DriveLetter > 1)	// If it is not a Floppy Drive
		{
			for (n=0; n<MAX_HARDDRIVES; n++)
			{
				/* Check if drive letter matches */
				if (emudrives[n] &&  DriveLetter == emudrives[n]->hd_letter)
					return DriveLetter;
			}
		}
	}

	/* Not a high-level redirected drive, let TOS handle it */
	return -1;
}


/*-----------------------------------------------------------------------*/
/**
 * Returns the length of the basename of the file passed in parameter
 *  (ie the file without extension)
 */
static int baselen(char *s)
{
	char *ext = strchr(s,'.');
	if (ext)
		return ext-s;
	return strlen(s);
}

/*-----------------------------------------------------------------------*/
/**
 * Use hard-drive directory, current ST directory and filename to create full path
 */
void GemDOS_CreateHardDriveFileName(int Drive, const char *pszFileName,
                                    char *pszDestName, int nDestNameLen)
{
	char *s,*start;

	/* Check for valid string */
	if (pszFileName[0] == '\0')
		return;

	/* Is it a valid hard drive? */
	if (Drive < 2)
		return;

	/* case full filename "C:\foo\bar" */
	s = pszDestName;
	start = NULL;

	if (pszFileName[1] == ':')
	{
		snprintf(pszDestName, nDestNameLen, "%s%s",
		         emudrives[Drive-2]->hd_emulation_dir, File_RemoveFileNameDrive(pszFileName));
	}
	/* case referenced from root:  "\foo\bar" */
	else if (pszFileName[0] == '\\')
	{
		snprintf(pszDestName, nDestNameLen, "%s%s",
		         emudrives[Drive-2]->hd_emulation_dir, pszFileName);
	}
	/* case referenced from current directory */
	else
	{
		snprintf(pszDestName, nDestNameLen, "%s%s",
		         emudrives[Drive-2]->fs_currpath, pszFileName);
		start = pszDestName + strlen(emudrives[Drive-2]->fs_currpath)-1;
	}

#ifndef _WIN32
	/* convert to front slashes. */
	while((s = strchr(s+1,'\\')))
	{
		if (!start)
		{
			start = s;
			continue;
		}
		{
			glob_t globbuf;
			char old1, old2;
			int len, found, base_len;
			unsigned int j;

			*start++ = PATHSEP;
			old1 = *start;
			*start++ = '*';
			old2 = *start;
			*start = 0;
			glob(pszDestName,GLOB_ONLYDIR,NULL,&globbuf);
			*start-- = old2;
			*start = old1;
			*s = 0;
			len = strlen(pszDestName);
			base_len = baselen(start);
			found = 0;
			// Handle long file names
			for (j=0; j<globbuf.gl_pathc; j++)
			{
				/* If we search for a file of at least 8 characters, then it might
				   be a longer filename since the ST can access only the first 8
				   characters. If not, then it's a precise match (with case). */
				if (!(base_len < 8 ? strcasecmp(globbuf.gl_pathv[j],pszDestName) :
						strncasecmp(globbuf.gl_pathv[j],pszDestName,len)))
				{
					/* we found a matching name... */
					snprintf(pszDestName, nDestNameLen, "%s%c%s",
					         globbuf.gl_pathv[j], PATHSEP, s+1);
					j = globbuf.gl_pathc;
					found = 1;
				}
				/* Here comes a work-around for a bug in the file selector
				 * of TOS 1.02: When a folder name has exactly 8 characters,
				 * it appends a '.' at the end of the name... */
				else if (base_len == 8 && pszDestName[len-1] == '.' &&
						 !strncasecmp(globbuf.gl_pathv[j],pszDestName,len-1))
				{
					/* we found a matching name... */
					snprintf(pszDestName, nDestNameLen, "%s%c%s",
					         globbuf.gl_pathv[j], PATHSEP, s+1);
					s -= 1;
					j = globbuf.gl_pathc;  /* break */
					found = 1;
				}
			}
			globfree(&globbuf);
			if (!found)
			{
				/* didn't find it. Let's try normal files (it might be a symlink) */
				*start++ = '*';
				*start = 0;
				glob(pszDestName,0,NULL,&globbuf);
				*start-- = old2;
				*start = old1;
				for (j=0; j<globbuf.gl_pathc; j++)
				{
					if (!strncasecmp(globbuf.gl_pathv[j],pszDestName,len))
					{
						/* we found a matching name... */
						snprintf(pszDestName, nDestNameLen, "%s%c%s",
						         globbuf.gl_pathv[j], PATHSEP, s+1);
						j = globbuf.gl_pathc;
						found = 1;
					}
				}
				globfree(&globbuf);
				if (!found)
				{           /* really nothing ! */
					*s = PATHSEP;
					fprintf(stderr,"no path for %s\n",pszDestName);
				}
			}
		}
		start = s;
	}
#endif
	if (!start)
		start = strrchr(pszDestName, PATHSEP); // path already converted ?

	if (start)
	{
		*start++ = PATHSEP;     /* in case there was only 1 anti slash */
		if (*start && !strchr(start,'?') && !strchr(start,'*'))
		{
			/* We have a complete name after the path, not a wildcard */
			glob_t globbuf;
			char old1,old2;
			int len, found, base_len;
			unsigned int j;

			old1 = *start;
			*start++ = '*';
			old2 = *start;
			*start = 0;
			glob(pszDestName,0,NULL,&globbuf);
			*start-- = old2;
			*start = old1;
			len = strlen(pszDestName);
			base_len = baselen(start);
			found = 0;
			for (j=0; j<globbuf.gl_pathc; j++)
			{
				/* If we search for a file of at least 8 characters, then it might
				   be a longer filename since the ST can access only the first 8
				   characters. If not, then it's a precise match (with case). */
				if (!(base_len < 8 ? strcasecmp(globbuf.gl_pathv[j],pszDestName) :
						strncasecmp(globbuf.gl_pathv[j],pszDestName,len)))
				{
					/* we found a matching name... */
					strncpy(pszDestName, globbuf.gl_pathv[j], nDestNameLen);
					j = globbuf.gl_pathc;
					found = 1;
				}
			}
#if ENABLE_TRACING
			if (!found)
			{
				/* It's often normal, the gem uses this to test for existence */
				/* of desktop.inf or newdesk.inf for example. */
				LOG_TRACE(TRACE_OS_GEMDOS, "didn't find filename %s\n", pszDestName );
			}
#endif
			globfree(&globbuf);
		}
	}
	LOG_TRACE(TRACE_OS_GEMDOS, "conv %s -> %s\n", pszFileName, pszDestName );
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Cauxin
 * Call 0x3
 */
#if 0
static bool GemDOS_Cauxin(Uint32 Params)
{
	unsigned char c;

	/* Wait here until a character is ready */
	while(!RS232_GetStatus())
		;

	/* And read character */
	RS232_ReadBytes(&c,1);
	Regs[REG_D0] = c;

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Cauxin() = 0x%x\n", (int)c);

	return TRUE;
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
	unsigned char c;

	/* Get character from the stack */
	c = STMemory_ReadWord(Params+SIZE_WORD);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Cauxout(0x%x)\n", (int)c);

	/* Send character to RS232 */
	RS232_TransferBytesTo(&c, 1);

	return TRUE;
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
	unsigned char c;

	/* Send character to printer(or file) */
	c = STMemory_ReadWord(Params+SIZE_WORD);
	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Cprnout(0x%x)\n", (int)c);
	Printer_TransferByteTo(c);
	Regs[REG_D0] = -1;                /* Printer OK */

	return TRUE;
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
	CurrentDrive = STMemory_ReadWord(Params+SIZE_WORD);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Dsetdrv(0x%x)\n", (int)CurrentDrive);

	/* Still re-direct to TOS */
	return FALSE;
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

	return TRUE;
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

	return TRUE;
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

	return TRUE;
}
#endif

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Set Disk Transfer Address (DTA)
 * Call 0x1A
 */
static bool GemDOS_SetDTA(Uint32 Params)
{
	Uint32 nDTA = STMemory_ReadLong(Params+SIZE_WORD);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fsetdta(0x%x)\n", nDTA);

	/* Look up on stack to find where DTA is! Store as PC pointer */
	pDTA = (DTA *)STRAM_ADDR(nDTA);

	return FALSE;
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

	Address = STMemory_ReadLong(Params+SIZE_WORD);
	Drive = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Dfree(0x%x, %i)\n", Address, Drive);

	/* is it our drive? */
	if ((Drive == 0 && CurrentDrive >= 2) || Drive >= 3)
	{
		/* FIXME: Report actual free drive space */

		STMemory_WriteLong(Address,  10*2048);           /* free clusters (mock 10 Mb) */
		STMemory_WriteLong(Address+SIZE_LONG, 50*2048 ); /* total clusters (mock 50 Mb) */

		STMemory_WriteLong(Address+SIZE_LONG*2, 512 );   /* bytes per sector */
		STMemory_WriteLong(Address+SIZE_LONG*3, 1 );     /* sectors per cluster */
		return TRUE;
	}
	else
		return FALSE; /* redirect to TOS */
}

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS MkDir
 * Call 0x39
 */
static bool GemDOS_MkDir(Uint32 Params)
{
	char *pDirName;
	int Drive;

	/* Find directory to make */
	pDirName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Dcreate(\"%s\")\n", pDirName);

	Drive = GemDOS_IsFileNameAHardDrive(pDirName);

	if (ISHARDDRIVE(Drive))
	{
		char *psDirPath;
		psDirPath = malloc(FILENAME_MAX);
		if (!psDirPath)
		{
			perror("GemDOS_MkDir");
			Regs[REG_D0] = GEMDOS_ENSMEM;
			return TRUE;
		}

		/* Copy old directory, as if calls fails keep this one */
		GemDOS_CreateHardDriveFileName(Drive, pDirName, psDirPath, FILENAME_MAX);

		/* Attempt to make directory */
		if (mkdir(psDirPath, 0755) == 0)
			Regs[REG_D0] = GEMDOS_EOK;
		else
			Regs[REG_D0] = GEMDOS_EACCDN;        /* Access denied */

		free(psDirPath);
		return TRUE;
	}
	return FALSE;
}

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS RmDir
 * Call 0x3A
 */
static bool GemDOS_RmDir(Uint32 Params)
{
	char *pDirName;
	int Drive;

	/* Find directory to make */
	pDirName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Ddelete(\"%s\")\n", pDirName);

	Drive = GemDOS_IsFileNameAHardDrive(pDirName);

	if (ISHARDDRIVE(Drive))
	{
		char *psDirPath;
		psDirPath = malloc(FILENAME_MAX);
		if (!psDirPath)
		{
			perror("GemDOS_RmDir");
			Regs[REG_D0] = GEMDOS_ENSMEM;
			return TRUE;
		}

		/* Copy old directory, as if calls fails keep this one */
		GemDOS_CreateHardDriveFileName(Drive, pDirName, psDirPath, FILENAME_MAX);

		/* Attempt to make directory */
		if (rmdir(psDirPath) == 0)
			Regs[REG_D0] = GEMDOS_EOK;
		else
			Regs[REG_D0] = GEMDOS_EACCDN;        /* Access denied */

		free(psDirPath);
		return TRUE;
	}
	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS ChDir
 * Call 0x3B
 */
static bool GemDOS_ChDir(Uint32 Params)
{
	char *pDirName;
	int Drive;

	/* Find new directory */
	pDirName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Dsetpath(\"%s\")\n", pDirName);

	Drive = GemDOS_IsFileNameAHardDrive(pDirName);

	if (ISHARDDRIVE(Drive))
	{
		struct stat buf;
		char *psTempDirPath;

		/* Allocate temporary memory for path name: */
		psTempDirPath = malloc(FILENAME_MAX);
		if (!psTempDirPath)
		{
			perror("GemDOS_ChDir");
			Regs[REG_D0] = GEMDOS_ENSMEM;
			return TRUE;
		}

		GemDOS_CreateHardDriveFileName(Drive, pDirName, psTempDirPath, FILENAME_MAX);

		// Remove trailing slashes (stat on Windows does not like that)
		File_CleanFileName(psTempDirPath);

		if (stat(psTempDirPath, &buf))
		{
			/* error */
			free(psTempDirPath);
			Regs[REG_D0] = GEMDOS_EPTHNF;
			return TRUE;
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

		return TRUE;
	}

	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Create file
 * Call 0x3C
 */
static bool GemDOS_Create(Uint32 Params)
{
	char szActualFileName[MAX_GEMDOS_PATH];
	char *pszFileName, *ptr;
	int Drive,Index,Mode;
	const char *rwflags;

	/* Find filename */
	pszFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
	Mode = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fcreate(\"%s\", 0x%x)\n", pszFileName, Mode);

	Drive = GemDOS_IsFileNameAHardDrive(pszFileName);

	if (!ISHARDDRIVE(Drive))
	{
		return FALSE;
	}

	if (Mode == GEMDOS_FILE_ATTRIB_VOLUME_LABEL)
	{
		fprintf(stderr, "Warning: Hatari doesn't support GEMDOS volume"
		        " label setting\n(for '%s')\n", pszFileName);
		Regs[REG_D0] = GEMDOS_EFILNF;         /* File not found */
		return TRUE;
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
		return TRUE;
	}
#ifdef ENABLE_SAVING
	/* FIXME: implement other Mode attributes
	 * - GEMDOS_FILE_ATTRIB_HIDDEN       (FA_HIDDEN)
	 * - GEMDOS_FILE_ATTRIB_SYSTEM_FILE  (FA_SYSTEM)
	 * - GEMDOS_FILE_ATTRIB_SUBDIRECTORY (FA_DIR)
	 * - GEMDOS_FILE_ATTRIB_WRITECLOSE   (FA_ARCHIVE)
	 *   (set automatically by GemDOS >= 0.15)
	 */
	if (Mode & GEMDOS_FILE_ATTRIB_READONLY)
		rwflags = "wb";
	else
		rwflags = "wb+";
	FileHandles[Index].FileHandle = fopen(szActualFileName, rwflags);

	if (FileHandles[Index].FileHandle != NULL)
	{
		/* Tag handle table entry as used and return handle */
		FileHandles[Index].bUsed = TRUE;
		Regs[REG_D0] = Index+BASE_FILEHANDLE;  /* Return valid ST file handle from range 6 to 45! (ours start from 0) */
		return TRUE;
	}

	/* We failed to create the file... now we have to return the right
	 * error code: Normally we return FILE-NOT-FOUND, but in case the
	 * directory did not exist yet, we have to return PATH-NOT-FOUND
	 * (ST-Zip 2.6 relies on that during extraction of ZIP files). */
	ptr = strrchr(szActualFileName, PATHSEP);
	if (ptr)
	{
		*ptr = 0;   /* Strip filename from string */
		if (!File_DirectoryExists(szActualFileName))
		{
			Regs[REG_D0] = GEMDOS_EPTHNF; /* Path not found */
			return TRUE;
		}
	}
#endif
	Regs[REG_D0] = GEMDOS_EFILNF;         /* File not found */
	return TRUE;
}

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Open file
 * Call 0x3D
 */
static bool GemDOS_Open(Uint32 Params)
{
	char szActualFileName[MAX_GEMDOS_PATH];
	char *pszFileName;
	const char *open_modes[] =
		{	/* convert atari modes to stdio modes */
			"rb",	/* read only */
			"rb+",	/* FIXME: should be write only, but "wb" truncates */
			"rb+"	/* read/write */
			"rb"	/* read only */
		};
	int Drive,Index,Mode;

	/* Find filename */
	pszFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
	Mode = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fopen(\"%s\", 0x%x)\n", pszFileName, Mode);

	Drive = GemDOS_IsFileNameAHardDrive(pszFileName);

	if (!ISHARDDRIVE(Drive))
	{
		return FALSE;
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
		return TRUE;
	}

	/* FIXME: Open file
	 * - fopen() modes don't allow write-only mode without truncating.
	 *   Fixing this requires using open() and file descriptors instead
	 *   of fopen() and FILE* pointers, but Windows doesn't support that
	 */
	FileHandles[Index].FileHandle =  fopen(szActualFileName, open_modes[Mode&0x03]);

	snprintf(FileHandles[Index].szActualName, sizeof(FileHandles[Index].szActualName),
		 "%s", szActualFileName);

	if (FileHandles[Index].FileHandle != NULL)
	{
		/* Tag handle table entry as used and return handle */
		FileHandles[Index].bUsed = TRUE;
		Regs[REG_D0] = Index+BASE_FILEHANDLE;  /* Return valid ST file handle from range 6 to 45! (ours start from 0) */
		return TRUE;
	}

	if (Mode != 1 && errno == EACCES)
		LOG_TRACE(TRACE_OS_GEMDOS, "Missing permission to read file '%s'\n", szActualFileName );

	Regs[REG_D0] = GEMDOS_EFILNF;     /* File not found/ error opening */
	return TRUE;
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
	Handle = STMemory_ReadWord(Params+SIZE_WORD)-BASE_FILEHANDLE;

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fclose(%i)\n", Handle);

	/* Check handle was valid */
	if (GemDOS_IsInvalidFileHandle(Handle))
	{
		/* No assume was TOS */
		return FALSE;
	}
	else
	{
		/* Close file and free up handle table */
		fclose(FileHandles[Handle].FileHandle);
		FileHandles[Handle].bUsed = FALSE;
		/* Return no error */
		Regs[REG_D0] = GEMDOS_EOK;
		return TRUE;
	}
}

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Read file
 * Call 0x3F
 */
static bool GemDOS_Read(Uint32 Params)
{
	char *pBuffer;
	unsigned long nBytesRead,Size,CurrentPos,FileSize;
	long nBytesLeft;
	int Handle;

	/* Read details from stack */
	Handle = STMemory_ReadWord(Params+SIZE_WORD)-BASE_FILEHANDLE;
	Size = STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD);
	pBuffer = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD+SIZE_LONG));

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fread(%i, %li, 0x%x)\n", 
	          Handle, Size, STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD+SIZE_LONG));

	/* Check handle was valid */
	if (GemDOS_IsInvalidFileHandle(Handle))
	{
		/* No -  assume was TOS */
		return FALSE;
	}
	else
	{

		/* To quick check to see where our file pointer is and how large the file is */
		CurrentPos = ftell(FileHandles[Handle].FileHandle);
		fseek(FileHandles[Handle].FileHandle, 0, SEEK_END);
		FileSize = ftell(FileHandles[Handle].FileHandle);
		fseek(FileHandles[Handle].FileHandle, CurrentPos, SEEK_SET);

		nBytesLeft = FileSize-CurrentPos;

		/* Check for End Of File */
		if (nBytesLeft == 0)
		{
			/* FIXME: should we return zero (bytes read) or an error? */
			Regs[REG_D0] = 0;
			return TRUE;
		}
		else
		{
			/* Limit to size of file to prevent errors */
			if (Size > FileSize)
				Size = FileSize;
			/* And read data in */
			nBytesRead = fread(pBuffer, 1, Size, FileHandles[Handle].FileHandle);

			/* Return number of bytes read */
			Regs[REG_D0] = nBytesRead;

			return TRUE;
		}
	}
}

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Write file
 * Call 0x40
 */
static bool GemDOS_Write(Uint32 Params)
{
	char *pBuffer;
	unsigned long Size,nBytesWritten;
	int Handle;

	/* Read details from stack */
	Handle = STMemory_ReadWord(Params+SIZE_WORD)-BASE_FILEHANDLE;
	Size = STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD);
	pBuffer = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD+SIZE_LONG));

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fwrite(%i, %li, 0x%x)\n", 
	          Handle, Size, STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD+SIZE_LONG));

#ifdef ENABLE_SAVING
	/* Check if handle was not invalid */
	if (!GemDOS_IsInvalidFileHandle(Handle))
	{
		nBytesWritten = fwrite(pBuffer, 1, Size, FileHandles[Handle].FileHandle);
		if (ferror(FileHandles[Handle].FileHandle))
		{
			Regs[REG_D0] = GEMDOS_EACCDN;      /* Access denied(ie read-only) */
		}
		else
		{

			Regs[REG_D0] = nBytesWritten;      /* OK */
		}
		return TRUE;
	}
#endif

	return FALSE;
}

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Delete file
 * Call 0x41
 */
static bool GemDOS_FDelete(Uint32 Params)
{
	char *pszFileName;
	int Drive;

	/* Find filename */
	pszFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fdelete(\"%s\")\n", pszFileName);

	Drive = GemDOS_IsFileNameAHardDrive(pszFileName);

#ifdef ENABLE_SAVING
	if (ISHARDDRIVE(Drive))
	{
		char *psActualFileName;
		psActualFileName = malloc(FILENAME_MAX);
		if (!psActualFileName)
		{
			perror("GemDOS_FDelete");
			Regs[REG_D0] = GEMDOS_ENSMEM;
			return TRUE;
		}

		/* And convert to hard drive filename */
		GemDOS_CreateHardDriveFileName(Drive, pszFileName, psActualFileName, FILENAME_MAX);

		/* Now delete file?? */
		if (unlink(psActualFileName) == 0)
			Regs[REG_D0] = GEMDOS_EOK;          /* OK */
		else
			Regs[REG_D0] = GEMDOS_EFILNF;       /* File not found */

		free(psActualFileName);
		return TRUE;
	}
#endif

	return FALSE;
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
	Offset = (long)STMemory_ReadLong(Params+SIZE_WORD);
	Handle = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG)-BASE_FILEHANDLE;
	Mode = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_WORD);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fseek(%li, %i, %i)\n", Offset, Handle, Mode);

	/* Check handle was valid */
	if (GemDOS_IsInvalidFileHandle(Handle))
	{
		/* No assume was TOS */
		return FALSE;
	}

	fhndl = FileHandles[Handle].FileHandle;

	/* Save old position in file */
	nOldPos = ftell(fhndl);

	/* Determine the size of the file */
	fseek(fhndl, 0L, SEEK_END);
	nFileSize = ftell(fhndl);

	switch (Mode)
	{
	 case 0: nDestPos = Offset; break;
	 case 1: nDestPos = nOldPos + Offset; break;
	 case 2: nDestPos = nFileSize - Offset; break;
	 default:
		/* Restore old position and return error */
		fseek(fhndl, nOldPos, SEEK_SET);
		Regs[REG_D0] = GEMDOS_EINVFN;
		return TRUE;
	}

	if (nDestPos < 0 || nDestPos > nFileSize)
	{
		/* Restore old position and return error */
		fseek(fhndl, nOldPos, SEEK_SET);
		Regs[REG_D0] = GEMDOS_ERANGE;
		return TRUE;
	}

	/* Seek to new position and return offset from start of file */
	fseek(fhndl, nDestPos, SEEK_SET);
	Regs[REG_D0] = ftell(fhndl);

	return TRUE;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Fattrib() - get or set file attributes
 * Call 0x43
 */
static bool GemDOS_Fattrib(Uint32 Params)
{
	char sActualFileName[MAX_GEMDOS_PATH];
	char *psFileName;
	int nDrive;
	int nRwFlag, nAttrib;
	struct stat FileStat;

	/* Find filename */
	psFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
	nDrive = GemDOS_IsFileNameAHardDrive(psFileName);

	nRwFlag = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG);
	nAttrib = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_WORD);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fattrib(\"%s\", %d, 0x%x)\n",
	          psFileName, nRwFlag, nAttrib);

	if (!ISHARDDRIVE(nDrive))
	{
		return FALSE;
	}

	/* Convert to hard drive filename */
	GemDOS_CreateHardDriveFileName(nDrive, psFileName,
	                              sActualFileName, sizeof(sActualFileName));

	if (nAttrib == GEMDOS_FILE_ATTRIB_VOLUME_LABEL)
	{
		fprintf(stderr, "Warning: Hatari doesn't support GEMDOS volume label setting\n(for '%s')\n", sActualFileName);
		Regs[REG_D0] = GEMDOS_EFILNF;         /* File not found */
		return TRUE;
	}
	if (stat(sActualFileName, &FileStat) != 0)
	{
		Regs[REG_D0] = GEMDOS_EFILNF;         /* File not found */
		return TRUE;
	}
	if (nRwFlag == 0)
	{
		/* Read attributes */
		Regs[REG_D0] = GemDOS_ConvertAttribute(FileStat.st_mode);
		return TRUE;
	}
	if (nAttrib & GEMDOS_FILE_ATTRIB_READONLY)
	{
		/* set read-only (readable by all) */
		if (chmod(sActualFileName, S_IRUSR|S_IRGRP|S_IROTH) == 0)
		{
			Regs[REG_D0] = GEMDOS_FILE_ATTRIB_READONLY;
			return TRUE;
		}
	}
	/* FIXME: support hidden/system/archive flags? */
	Regs[REG_D0] = GEMDOS_EACCDN;         /* Acces denied */
	return TRUE;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Get Directory
 * Call 0x47
 */
static int GemDOS_GetDir(Uint32 Params)
{
	Uint32 Address;
	Uint16 Drive;

	Address = STMemory_ReadLong(Params+SIZE_WORD);
	Drive = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG);
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

		strcpy(path,&emudrives[Drive-2]->fs_currpath[strlen(emudrives[Drive-2]->hd_emulation_dir)]);

		// convert it to ST path (DOS)
		len = strlen(path)-1;
		path[len] = 0;
		for (i = 0; i <= len; i++)
		{
			c = path[i];
			STMemory_WriteByte(Address+i, (c==PATHSEP ? '\\' : c) );
		}
		LOG_TRACE(TRACE_OS_GEMDOS, "GemDOS_GetDir (%d) = %s\n", Drive, path );

		Regs[REG_D0] = GEMDOS_EOK;          /* OK */

		return TRUE;
	}
	else return FALSE;
}

/*-----------------------------------------------------------------------*/
/**
 * PExec Load And Go - Redirect to cart' routine at address 0xFA1000
 *
 * If loading from hard-drive(ie drive ID 2 or more) set condition codes to run own GEMDos routines
 */
static int GemDOS_Pexec_LoadAndGo(Uint32 Params)
{
	/* add multiple disk support here too */
	/* Hard-drive? */
	char *pszFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+2*SIZE_WORD));
	int Drive = GemDOS_IsFileNameAHardDrive(pszFileName);

	if (ISHARDDRIVE(Drive))
	{
		/* If not using A: or B:, use my own routines to load */
		return CALL_PEXEC_ROUTINE;
	}
	else return FALSE;
}

/*-----------------------------------------------------------------------*/
/**
 * PExec Load But Don't Go - Redirect to cart' routine at address 0xFA1000
 */
static int GemDOS_Pexec_LoadDontGo(Uint32 Params)
{
	/* Hard-drive? */
	char *pszFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+2*SIZE_WORD));
	int Drive = GemDOS_IsFileNameAHardDrive(pszFileName);
	if (ISHARDDRIVE(Drive))
	{
		return CALL_PEXEC_ROUTINE;
	}
	else return FALSE;
}

/*-----------------------------------------------------------------------*/
/**
 * GEMDOS PExec handler
 * Call 0x4B
 */
static int GemDOS_Pexec(Uint32 Params)
{
	Uint16 Mode;
	/*char *psFileName, *psCmdLine;*/

	/* Find PExec mode */
	Mode = STMemory_ReadWord(Params+SIZE_WORD);

	/*
	psFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+2*SIZE_WORD));
	psCmdLine = (char *)STRAM_ADDR(STMemory_ReadLong(Params+2*SIZE_WORD+SIZE_LONG));
	*/

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Pexec(%i, ...)\n", Mode);

	/* Re-direct as needed */
	switch(Mode)
	{
	 case 0:      /* Load and go */
		return GemDOS_Pexec_LoadAndGo(Params);
	 case 3:      /* Load, don't go */
		return GemDOS_Pexec_LoadDontGo(Params);
	 case 4:      /* Just go */
		return FALSE;
	 case 5:      /* Create basepage */
		return FALSE;
	 case 6:
		return FALSE;
	}

	/* Default: Still re-direct to TOS */
	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Search Next
 * Call 0x4F
 */
static bool GemDOS_SNext(void)
{
	int Index;
	int ret;

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fsnext()\n");

	/* Refresh pDTA pointer (from the current basepage) */
	pDTA = (DTA *)STRAM_ADDR(STMemory_ReadLong(STMemory_ReadLong(act_pd)+32));

	/* Was DTA ours or TOS? */
	if (do_get_mem_long(pDTA->magic) == DTA_MAGIC_NUMBER)
	{
		struct dirent **temp;

		/* Find index into our list of structures */
		Index = do_get_mem_word(pDTA->index) & (MAX_DTAS_FILES-1);

		if (nAttrSFirst == 8)
		{
			Regs[REG_D0] = GEMDOS_ENMFIL;    /* No more files */
			return TRUE;
		}

		temp = InternalDTAs[Index].found;
		do
		{
			if (InternalDTAs[Index].centry >= InternalDTAs[Index].nentries)
			{
				Regs[REG_D0] = GEMDOS_ENMFIL;    /* No more files */
				return TRUE;
			}

			ret = PopulateDTA(InternalDTAs[Index].path,
					  temp[InternalDTAs[Index].centry++]);
		} while (ret == 1);

		if (ret < 0)
		{
			Log_Printf(LOG_WARN, "GemDOS_SNext: Error setting DTA.\n");
			Regs[REG_D0] = GEMDOS_EINTRN;
			return TRUE;
		}

		Regs[REG_D0] = GEMDOS_EOK;
		return TRUE;
	}

	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Find first file
 * Call 0x4E
 */
static bool GemDOS_SFirst(Uint32 Params)
{
	char szActualFileName[MAX_GEMDOS_PATH];
	char tempstr[MAX_GEMDOS_PATH];
	char *pszFileName;
	struct dirent **files;
	int Drive;
	DIR *fsdir;
	int i,j,count;

	/* Find filename to search for */
	pszFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
	nAttrSFirst = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fsfirst(\"%s\", 0x%x)\n", pszFileName, nAttrSFirst);

	/* Refresh pDTA pointer (from the current basepage) */
	pDTA = (DTA *)STRAM_ADDR(STMemory_ReadLong(STMemory_ReadLong(act_pd)+32));

	Drive = GemDOS_IsFileNameAHardDrive(pszFileName);
	if (ISHARDDRIVE(Drive))
	{

		/* And convert to hard drive filename */
		GemDOS_CreateHardDriveFileName(Drive, pszFileName,
		                    szActualFileName, sizeof(szActualFileName));

		/* Populate DTA, set index for our use */
		do_put_mem_word(pDTA->index, DTAIndex);
		/* set our dta magic num */
		do_put_mem_long(pDTA->magic, DTA_MAGIC_NUMBER);

		if (InternalDTAs[DTAIndex].bUsed == TRUE)
			ClearInternalDTA();
		InternalDTAs[DTAIndex].bUsed = TRUE;

		/* Were we looking for the volume label? */
		if (nAttrSFirst == GEMDOS_FILE_ATTRIB_VOLUME_LABEL)
		{
			/* Volume name */
			strcpy(pDTA->dta_name,"EMULATED.001");
			Regs[REG_D0] = GEMDOS_EOK;          /* Got volume */
			return TRUE;
		}

		/* open directory */
		fsfirst_dirname(szActualFileName, InternalDTAs[DTAIndex].path);
		fsdir = opendir(InternalDTAs[DTAIndex].path);

		if (fsdir == NULL)
		{
			Regs[REG_D0] = GEMDOS_EPTHNF;        /* Path not found */
			return TRUE;
		}
		/* close directory */
		closedir(fsdir);

		count = scandir(InternalDTAs[DTAIndex].path, &files, 0, alphasort);
		/* File (directory actually) not found */
		if (count < 0)
		{
			Regs[REG_D0] = GEMDOS_EFILNF;
			return TRUE;
		}

		InternalDTAs[DTAIndex].centry = 0;          /* current entry is 0 */
		fsfirst_dirmask(szActualFileName, tempstr); /* get directory mask */
		InternalDTAs[DTAIndex].found = files;       /* get files */

		/* count & copy the entries that match our mask and discard the rest */
		j = 0;
		for (i=0; i < count; i++)
		{
			if (match(tempstr, files[i]->d_name))
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
			return TRUE;
		}

		/* Scan for first file (SNext uses no parameters) */
		GemDOS_SNext();
		/* increment DTA index */
		DTAIndex++;
		DTAIndex&=(MAX_DTAS_FILES-1);

		return TRUE;
	}
	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * GEMDOS Rename
 * Call 0x56
 */
static bool GemDOS_Rename(Uint32 Params)
{
	char *pszNewFileName,*pszOldFileName;
	char szNewActualFileName[MAX_GEMDOS_PATH],szOldActualFileName[MAX_GEMDOS_PATH];
	int NewDrive, OldDrive;

	/* Read details from stack */
	pszOldFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD));
	pszNewFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD+SIZE_LONG));

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Frename(\"%s\", \"%s\")\n", pszOldFileName, pszNewFileName);

	NewDrive = GemDOS_IsFileNameAHardDrive(pszNewFileName);
	OldDrive = GemDOS_IsFileNameAHardDrive(pszOldFileName);
	if (ISHARDDRIVE(NewDrive) && ISHARDDRIVE(OldDrive))
	{
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
		return TRUE;
	}

	return FALSE;
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
	pBuffer = STMemory_ReadLong(Params+SIZE_WORD);
	Handle = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG)-BASE_FILEHANDLE;
	Flag = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_LONG);

	LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS Fdatime(0x%x, %i, %i)\n", pBuffer,
	          Handle, Flag);

	/* Check handle was valid */
	if (GemDOS_IsInvalidFileHandle(Handle))
	{
		/* No assume was TOS */
		return FALSE;
	}

	/* Set time/date stamp? Do nothing. */
	if (Flag == 1)
	{
		Regs[REG_D0] = GEMDOS_EOK;
		return TRUE;
	}

	Regs[REG_D0] = GEMDOS_ERROR;  /* Invalid parameter */

	if (GemDOS_GetFileInformation(FileHandles[Handle].szActualName, &DateTime) == TRUE)
	{
		STMemory_WriteWord(pBuffer, DateTime.word1);
		STMemory_WriteWord(pBuffer+2, DateTime.word2);
		Regs[REG_D0] = GEMDOS_EOK;
	}
	return TRUE;
}


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
	unsigned short int GemDOSCall,CallingSReg;
	Uint32 Params;
	short RunOld;
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
	RunOld = TRUE;
	SR &= SR_CLEAR_OVERFLOW;
	SR &= SR_CLEAR_ZERO;
	SR |= SR_NEG;

	/* Find pointer to call parameters */
	GemDOSCall = STMemory_ReadWord(Params);

	/* Intercept call */
	switch(GemDOSCall)
	{
	 /*
	 case 0x3:
		if (GemDOS_Cauxin(Params))
			RunOld = FALSE;
		break;
	 */
	 /*
	 case 0x4:
		if (GemDOS_Cauxout(Params))
			RunOld = FALSE;
		break;
	 */
	 /* direct printing via GEMDOS */
	 /*
	 case 0x5:
		if (GemDOS_Cprnout(Params))
			RunOld = FALSE;
		break;
	 */
	 case 0xe:
		if (GemDOS_SetDrv(Params))
			RunOld = FALSE;
		break;
	 /* Printer status  */
	 /*
	 case 0x11:
		if (GemDOS_Cprnos(Params))
			RunOld = FALSE;
		break;
	 */
	 /*
	 case 0x12:
		if (GemDOS_Cauxis(Params))
			RunOld = FALSE;
		break;
	 */
	 /*
	 case 0x13:
		if (GemDOS_Cauxos(Params))
			RunOld = FALSE;
		break;
	 */
	 case 0x1a:
		if (GemDOS_SetDTA(Params))
			RunOld = FALSE;
		break;
	 case 0x36:
		if (GemDOS_DFree(Params))
			RunOld = FALSE;
		break;
	 case 0x39:
		if (GemDOS_MkDir(Params))
			RunOld = FALSE;
		break;
	 case 0x3a:
		if (GemDOS_RmDir(Params))
			RunOld = FALSE;
		break;
	 case 0x3b:
		if (GemDOS_ChDir(Params))
			RunOld = FALSE;
		break;
	 case 0x3c:
		if (GemDOS_Create(Params))
			RunOld = FALSE;
		break;
	 case 0x3d:
		if (GemDOS_Open(Params))
			RunOld = FALSE;
		break;
	 case 0x3e:
		if (GemDOS_Close(Params))
			RunOld = FALSE;
		break;
	 case 0x3f:
		if (GemDOS_Read(Params))
			RunOld = FALSE;
		break;
	 case 0x40:
		if (GemDOS_Write(Params))
			RunOld = FALSE;
		break;
	 case 0x41:
		if (GemDOS_FDelete(Params))
			RunOld = FALSE;
		break;
	 case 0x42:
		if (GemDOS_LSeek(Params))
			RunOld = FALSE;
		break;
	 case 0x43:
		if (GemDOS_Fattrib(Params))
			RunOld = FALSE;
		break;
	 case 0x47:
		if (GemDOS_GetDir(Params))
			RunOld = FALSE;
		break;
	 case 0x4b:
		if (GemDOS_Pexec(Params) == CALL_PEXEC_ROUTINE)
			RunOld = CALL_PEXEC_ROUTINE;
		break;
	 case 0x4e:
		if (GemDOS_SFirst(Params))
			RunOld = FALSE;
		break;
	 case 0x4f:
		if (GemDOS_SNext())
			RunOld = FALSE;
		break;
	 case 0x56:
		if (GemDOS_Rename(Params))
			RunOld = FALSE;
		break;
	 case 0x57:
		if (GemDOS_GSDToF(Params))
			RunOld = FALSE;
		break;
	 default:
		LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS call 0x%X\n", GemDOSCall);
	}

	switch(RunOld)
	{
	 case FALSE:
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
	bInitGemDOS = TRUE;

	LOG_TRACE(TRACE_OS_GEMDOS, "Gemdos_Boot()\n" );

	/* install our gemdos handler, if -e or --harddrive option used */
	if (GEMDOS_EMU_ON)
	{
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
}
