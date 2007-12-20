/*
  Hatari - file.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Common file access functions.
*/
const char File_rcsid[] = "Hatari $Id: file.c,v 1.47 2007-12-20 11:41:04 thothy Exp $";

#include <config.h>

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <zlib.h>

#if HAVE_STRINGS_H
#include <strings.h>
#endif

#include "main.h"
#include "dialog.h"
#include "file.h"
#include "createBlankImage.h"
#include "zip.h"


/*-----------------------------------------------------------------------*/
/**
 * Remove any '/'s from end of filenames, but keeps / intact
 */
void File_CleanFileName(char *pszFileName)
{
	int len;

	len = strlen(pszFileName);

	/* Remove end slash from filename! But / remains! Doh! */
	if (len > 2 && pszFileName[len-1] == PATHSEP)
		pszFileName[len-1] = '\0';
}


/*-----------------------------------------------------------------------*/
/**
 * Add '/' to end of filename
 */
void File_AddSlashToEndFileName(char *pszFileName)
{
	int len;

	len = strlen(pszFileName);

	/* Check dir/filenames */
	if (len != 0)
	{
		if (pszFileName[len-1] != PATHSEP)
		{
			pszFileName[len] = PATHSEP; /* Must use end slash */
			pszFileName[len+1] = '\0';
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Does filename extension match? If so, return TRUE
 */
BOOL File_DoesFileExtensionMatch(const char *pszFileName, const char *pszExtension)
{
	if (strlen(pszFileName) < strlen(pszExtension))
		return FALSE;
	/* Is matching extension? */
	if (!strcasecmp(&pszFileName[strlen(pszFileName)-strlen(pszExtension)], pszExtension))
		return TRUE;

	/* No */
	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * Check if filename is from root
 * 
 * Return TRUE if filename is '/', else give FALSE
 */
static BOOL File_IsRootFileName(const char *pszFileName)
{
	if (pszFileName[0] == '\0')     /* If NULL string return! */
		return FALSE;

	if (pszFileName[0] == PATHSEP)
		return TRUE;

#ifdef WIN32

	if (pszFileName[1] == ':')
		return TRUE;
#endif

	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * Return string, to remove 'C:' part of filename
 */
const char *File_RemoveFileNameDrive(const char *pszFileName)
{
	if ( (pszFileName[0] != '\0') && (pszFileName[1] == ':') )
		return &pszFileName[2];
	else
		return pszFileName;
}


/*-----------------------------------------------------------------------*/
/**
 * Check if filename end with a '/'
 * 
 * Return TRUE if filename ends with '/'
 */
BOOL File_DoesFileNameEndWithSlash(char *pszFileName)
{
	if (pszFileName[0] == '\0')    /* If NULL string return! */
		return FALSE;
	
	/* Does string end in a '/'? */
	if (pszFileName[strlen(pszFileName)-1] == PATHSEP)
		return TRUE;
	
	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * Read file from disk into allocated buffer and return the buffer
 * or NULL for error.  If pFileSize is non-NULL, read file size
 * is set to that.
 */
Uint8 *File_Read(const char *pszFileName, long *pFileSize, const char * const ppszExts[])
{
	char *filepath = NULL;
	void *pFile = NULL;
	long FileSize = 0;

	/* Does the file exist? If not, see if can scan for other extensions and try these */
	if (!File_Exists(pszFileName) && ppszExts)
	{
		/* Try other extensions, if succeeds, returns correct one */
		filepath = File_FindPossibleExtFileName(pszFileName, ppszExts);
	}
	if (!filepath)
		filepath = strdup(pszFileName);

	/* Is it a gzipped file? */
	if (File_DoesFileExtensionMatch(filepath, ".gz"))
	{
		gzFile hGzFile;
		/* Open and read gzipped file */
		hGzFile = gzopen(filepath, "rb");
		if (hGzFile != NULL)
		{
			/* Find size of file: */
			do
			{
				/* Seek through the file until we hit the end... */
				gzseek(hGzFile, 1024, SEEK_CUR);
			}
			while (!gzeof(hGzFile));
			FileSize = gztell(hGzFile);
			gzrewind(hGzFile);
			/* Read in... */
			pFile = malloc(FileSize);
			if (pFile)
				FileSize = gzread(hGzFile, pFile, FileSize);

			gzclose(hGzFile);
		}
	}
	else if (File_DoesFileExtensionMatch(filepath, ".zip"))
	{
		/* It is a .ZIP file! -> Try to load the first file in the archive */
		pFile = ZIP_ReadFirstFile(filepath, &FileSize, ppszExts);
	}
	else          /* It is a normal file */
	{
		FILE *hDiskFile;
		/* Open and read normal file */
		hDiskFile = fopen(filepath, "rb");
		if (hDiskFile != NULL)
		{
			/* Find size of file: */
			fseek(hDiskFile, 0, SEEK_END);
			FileSize = ftell(hDiskFile);
			fseek(hDiskFile, 0, SEEK_SET);
			/* Read in... */
			pFile = malloc(FileSize);
			if (pFile)
				FileSize = fread(pFile, 1, FileSize, hDiskFile);

			fclose(hDiskFile);
		}
	}
	free(filepath);

	/* Store size of file we read in (or 0 if failed) */
	if (pFileSize)
		*pFileSize = FileSize;

	return pFile;        /* Return to where read in/allocated */
}


/*-----------------------------------------------------------------------*/
/**
 * Save file to disk, return FALSE if errors
 */
BOOL File_Save(const char *pszFileName, const Uint8 *pAddress, size_t Size, BOOL bQueryOverwrite)
{
	BOOL bRet = FALSE;

	/* Check if need to ask user if to overwrite */
	if (bQueryOverwrite)
	{
		/* If file exists, ask if OK to overwrite */
		if (!File_QueryOverwrite(pszFileName))
			return FALSE;
	}

	/* Normal file or gzipped file? */
	if (File_DoesFileExtensionMatch(pszFileName, ".gz"))
	{
		gzFile hGzFile;
		/* Create a gzipped file: */
		hGzFile = gzopen(pszFileName, "wb");
		if (hGzFile != NULL)
		{
			/* Write data, set success flag */
			if (gzwrite(hGzFile, pAddress, Size) == (int)Size)
				bRet = TRUE;

			gzclose(hGzFile);
		}
	}
	else
	{
		FILE *hDiskFile;
		/* Create a normal file: */
		hDiskFile = fopen(pszFileName, "wb");
		if (hDiskFile != NULL)
		{
			/* Write data, set success flag */
			if (fwrite(pAddress, 1, Size, hDiskFile) == Size)
				bRet = TRUE;

			fclose(hDiskFile);
		}
	}

	return bRet;
}


/*-----------------------------------------------------------------------*/
/**
 * Return size of file, -1 if error
 */
int File_Length(const char *pszFileName)
{
	FILE *hDiskFile;
	int FileSize;

	hDiskFile = fopen(pszFileName, "rb");
	if (hDiskFile!=NULL)
	{
		fseek(hDiskFile, 0, SEEK_END);
		FileSize = ftell(hDiskFile);
		fseek(hDiskFile, 0, SEEK_SET);
		fclose(hDiskFile);
		return FileSize;
	}

	return -1;
}


/*-----------------------------------------------------------------------*/
/**
 * Return TRUE if file exists, is readable or writable at least and is not
 * a directory. 
 */
BOOL File_Exists(const char *filename)
{
	struct stat buf;
	if (stat(filename, &buf) == 0 &&
	    (buf.st_mode & (S_IRUSR|S_IWUSR)) && !(buf.st_mode & S_IFDIR)) {
		/* file points to user readable regular file */
		return TRUE;
	}
	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * Find if file exists, and if so ask user if OK to overwrite
 */
BOOL File_QueryOverwrite(const char *pszFileName)
{
	const char *fmt;
	char *szString;
	BOOL ret = TRUE;

	/* Try and find if file exists */
	if (File_Exists(pszFileName))
	{
		fmt = "File '%s' exists, overwrite?";
		/* File does exist, are we OK to overwrite? */
		szString = malloc(strlen(pszFileName) + strlen(fmt) + 1);
		sprintf(szString, fmt, pszFileName);
		fprintf(stderr, "%s\n", szString);
		ret = DlgAlert_Query(szString);
		free(szString);
	}
	return ret;
}


/*-----------------------------------------------------------------------*/
/**
 * Try filename with various extensions and check if file exists
 * - if so, return allocated string which caller should free,
 *   otherwise return NULL
 */
char * File_FindPossibleExtFileName(const char *pszFileName, const char * const ppszExts[])
{
	char *szSrcDir, *szSrcName, *szSrcExt;
	int i;
	
	/* Allocate temporary memory for strings: */
	szSrcDir = malloc(3 * FILENAME_MAX);
	if (!szSrcDir)
	{
		perror("File_FindPossibleExtFileName");
		return FALSE;
	}
	szSrcName = szSrcDir + FILENAME_MAX;
	szSrcExt = szSrcName + FILENAME_MAX;
	
	/* Split filename into parts */
	File_SplitPath(pszFileName, szSrcDir, szSrcName, szSrcExt);

	/* Scan possible extensions */
	for (i = 0; ppszExts[i]; i++)
	{
		char *szTempFileName;

		/* Re-build with new file extension */
		szTempFileName = File_MakePath(szSrcDir, szSrcName, ppszExts[i]);
		if (szTempFileName)
		{
			/* Does this file exist? */
			if (File_Exists(szTempFileName))
			{
				free(szSrcDir);
				/* return filename without extra strings */
				return szTempFileName;
			}
			free(szTempFileName);
		}
	}
	free(szSrcDir);
	return NULL;
}


/*-----------------------------------------------------------------------*/
/**
 * Split a complete filename into path, filename and extension.
 * If pExt is NULL, don't split the extension from the file name!
 * It's safe for pSrcFileName and pDir to be the same string.
 */
void File_SplitPath(const char *pSrcFileName, char *pDir, char *pName, char *pExt)
{
	char *ptr1, *ptr2;

	/* Build pathname: */
	ptr1 = strrchr(pSrcFileName, PATHSEP);
	if (ptr1)
	{
		strcpy(pName, ptr1+1);
		memmove(pDir, pSrcFileName, ptr1-pSrcFileName+1);
		pDir[ptr1-pSrcFileName+1] = 0;
	}
	else
	{
 		strcpy(pName, pSrcFileName);
		sprintf(pDir, ".%c", PATHSEP);
	}

	/* Build the raw filename: */
	if (pExt != NULL)
	{
		ptr2 = strrchr(pName+1, '.');
		if (ptr2)
		{
			pName[ptr2-pName] = 0;
			/* Copy the file extension: */
			strcpy(pExt, ptr2+1);
		}
		else
			pExt[0] = 0;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Construct a complete filename from path, filename and extension.
 * Return the constructed filename.
 * pExt can also be NULL.
 */
char * File_MakePath(const char *pDir, const char *pName, const char *pExt)
{
	char *filepath;
	int len;

	/* dir or "." + "/" + name + "." + ext + \0 */
	len = strlen(pDir) + 2 + strlen(pName) + 1 + (pExt ? strlen(pExt) : 0) + 1;
	filepath = malloc(len);
	if (!filepath)
	{
		perror("File_MakePath");
		return NULL;
	}
	if (!pDir[0])
	{
		filepath[0] = '.';
		filepath[1] = '\0';
	} else {
		strcpy(filepath, pDir);
	}
	len = strlen(filepath);
	if (filepath[len-1] != PATHSEP)
	{
		filepath[len++] = PATHSEP;
	}
	strcpy(&filepath[len], pName);

	if (pExt != NULL && pExt[0])
	{
		len += strlen(pName);
		if (pExt[0] != '.')
			strcat(&filepath[len++], ".");
		strcat(&filepath[len], pExt);
	}
	return filepath;
}


/*-----------------------------------------------------------------------*/
/**
 * Shrink a file name to a certain length and insert some dots if we cut
 * something away (useful for showing file names in a dialog).
 */
void File_ShrinkName(char *pDestFileName, const char *pSrcFileName, int maxlen)
{
	int srclen = strlen(pSrcFileName);
	if (srclen < maxlen)
		strcpy(pDestFileName, pSrcFileName);  /* It fits! */
	else
	{
		assert(maxlen > 6);
		strncpy(pDestFileName, pSrcFileName, maxlen/2);
		if (maxlen&1)  /* even or uneven? */
			pDestFileName[maxlen/2-1] = 0;
		else
			pDestFileName[maxlen/2-2] = 0;
		strcat(pDestFileName, "...");
		strcat(pDestFileName, &pSrcFileName[strlen(pSrcFileName)-maxlen/2+1]);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Open given filename in given mode and handle "stdout" & "stderr"
 * filenames specially. Return FILE* to the opened file or NULL on error.
 */
FILE *File_Open(const char *path, const char *mode)
{
	int wr = 0, rd = 0;
	FILE *fp;

	/* special "stdout" and "stderr" files can be used
	 * for files which are written or appended
	 */
	if (strchr(mode, 'w') || strchr(mode, 'a'))
		wr = 1;
	if (strchr(mode, 'r'))
		rd = 1;
	if (strcmp(path, "stdin") == 0)
	{
		assert(rd && !wr);
		return stdin;
	}
	if (strcmp(path, "stdout") == 0)
	{
		assert(wr && !rd);
		return stdout;
	}
	if (strcmp(path, "stderr") == 0)
	{
		assert(wr && !rd);
		return stderr;
	}
	/* Open a normal log file */
	fp = fopen(path, mode);
	if (!fp)
		fprintf(stderr, "Can't open file '%s':\n  %s\n", path, strerror(errno));
	/* printf("'%s' opened in mode '%s'\n", path, mode, fp); */
	return fp;
}


/*-----------------------------------------------------------------------*/
/**
 * Close given FILE pointer and return the closed pointer
 * as NULL for the idiom "fp = File_Close(fp);"
 */
FILE *File_Close(FILE *fp)
{
	if (fp && fp != stdin && fp != stdout && fp != stderr)
	{
		fclose(fp);
	}
	return NULL;
}


/*-----------------------------------------------------------------------*/
/**
 * Wrapper for File_MakeAbsoluteName() which special-cases stdin/out/err
 * named files.  The given buffer should be opened with File_Open()
 * and closed with File_Close() if this function is used!
 * (On Linux one can use /dev/stdout etc, this is intended for other OSes)
 */
void File_MakeAbsoluteSpecialName(char *path)
{
	if (strcmp(path, "stdin")  != 0 &&
	    strcmp(path, "stdout") != 0 &&
	    strcmp(path, "stderr") != 0)
		File_MakeAbsoluteName(path);
}

/*-----------------------------------------------------------------------*/
/**
 * Create a clean absolute file name from a (possibly) relative file name.
 * I.e. filter out all occurancies of "./" and "../".
 * pFileName needs to point to a buffer of at least FILENAME_MAX bytes.
 */
void File_MakeAbsoluteName(char *pFileName)
{
	char *pTempName;
	int inpos, outpos;

#if defined (__AMIGAOS4__)
	/* This function does not work on Amiga OS */
	return;
#endif

	inpos = 0;
	pTempName = malloc(FILENAME_MAX);
	if (!pTempName)
	{
		perror("File_MakeAbsoluteName - malloc");
		return;
	}

	/* Is it already an absolute name? */
	if (File_IsRootFileName(pFileName))
	{
		outpos = 0;
	}
	else
	{
		if (!getcwd(pTempName, FILENAME_MAX))
		{
			perror("File_MakeAbsoluteName - getcwd");
			free(pTempName);
			return;
		}
		File_AddSlashToEndFileName(pTempName);
		outpos = strlen(pTempName);
	}

	/* Now filter out the relative paths "./" and "../" */
	while (pFileName[inpos] != 0 && outpos < FILENAME_MAX)
	{
		if (pFileName[inpos] == '.' && pFileName[inpos+1] == PATHSEP)
		{
			/* Ignore "./" */
			inpos += 2;
		}
		else if (pFileName[inpos] == '.' && pFileName[inpos+1] == '.'
		         && (pFileName[inpos+2] == PATHSEP || pFileName[inpos+2] == 0))
		{
			/* Handle "../" */
			char *pSlashPos;
			inpos += 3;
			pTempName[outpos - 1] = 0;
			pSlashPos = strrchr(pTempName, PATHSEP);
			if (pSlashPos)
			{
				*(pSlashPos + 1) = 0;
				outpos = strlen(pTempName);
			}
			else
			{
				pTempName[0] = PATHSEP;
				outpos = 1;
			}
		}
		else
		{
			/* Copy until next slash or end of input string */
			while (pFileName[inpos] != 0 && outpos < FILENAME_MAX)
			{
				pTempName[outpos++] = pFileName[inpos++];
				if (pFileName[inpos - 1] == PATHSEP)
					break;
			}
		}
	}

	pTempName[outpos] = 0;

	if (outpos > 2 && pTempName[outpos-1] == PATHSEP)
	{
		/* Remove trailing slash from path name */
		pTempName[outpos-1] = 0;
	}

	strcpy(pFileName, pTempName);          /* Copy back */
	free(pTempName);
}


/*-----------------------------------------------------------------------*/
/**
 * Create a valid path name from a possibly invalid name by erasing invalid
 * path parts at the end of the string.
 */
void File_MakeValidPathName(char *pPathName)
{
	struct stat dirstat;
	char *pLastSlash;

	do
	{
		/* Check for a valid path */
		if (stat(pPathName, &dirstat) == 0 && S_ISDIR(dirstat.st_mode))
		{
			break;
		}

		pLastSlash = strrchr(pPathName, PATHSEP);
		if (pLastSlash)
		{
			/* Erase the (probably invalid) part after the last slash */
			*pLastSlash = 0;
		}
		/* make sure we only shorten path... */
		else if (pPathName[0])
		{
			/* Path name seems to be completely invalid -> set to root directory */
			pPathName[0] = PATHSEP;
			pPathName[1] = 0;
		}
	}
	while (pLastSlash);
}
