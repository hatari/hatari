/*
  Hatari - filename.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Common file name handling functions.
*/
const char FileName_fileid[] = "Hatari filename.c";

#include <assert.h>
#include <errno.h>

#include "main.h"
#include "file.h"
#include "str.h"


/**
 * Remove any '/'s from end of filenames, but keeps / intact
 */
void File_CleanFileName(char *pszFileName)
{
	int len;

	len = strlen(pszFileName);

	/* Remove end slashes from filename! But / remains! Doh! */
	while (len > 2 && pszFileName[--len] == PATHSEP)
		pszFileName[len] = '\0';
}


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


/**
 * Does filename's extension match? If so, return TRUE
 */
bool File_DoesFileExtensionMatch(const char *pszFileName, const char *pszExtension)
{
	if (strlen(pszFileName) < strlen(pszExtension))
		return false;
	/* Is matching extension? */
	if (!strcasecmp(&pszFileName[strlen(pszFileName)-strlen(pszExtension)], pszExtension))
		return true;

	/* No */
	return false;
}


/**
 * If filename's extension matches, replace it with a new extension and
 * copy the result in the new filename.
 * Return TRUE if OK
 */
bool File_ChangeFileExtension(const char *Filename_old, const char *Extension_old , char *Filename_new , const char *Extension_new)
{
	if ( strlen ( Filename_old ) >= FILENAME_MAX - strlen ( Extension_new ) )
		return false;					/* file name is already too long */

	if ( strlen ( Filename_old ) < strlen ( Extension_old ) )
		return false;

	if ( !strcasecmp ( Filename_old + strlen(Filename_old) - strlen(Extension_old) , Extension_old ) )
	{
		strcpy ( Filename_new , Filename_old );
		strcpy ( Filename_new + strlen ( Filename_new ) - strlen ( Extension_old ) , Extension_new );
		return true;
	}

	return false;
}


/**
 * Check if host filename is from root
 *
 * Return TRUE if filename is '/', else give FALSE
 */
static bool File_IsRootFileName(const char *pszFileName)
{
	if (pszFileName[0] == '\0')     /* If NULL string return! */
		return false;

	if (pszFileName[0] == PATHSEP)
		return true;

#ifdef WIN32
	if (pszFileName[1] == ':')
		return true;
#endif

#ifdef GEKKO
	if (strlen(pszFileName) > 2 && pszFileName[2] == ':')	/* sd: */
		return true;
	if (strlen(pszFileName) > 3 && pszFileName[3] == ':')	/* fat: */
		return true;
	if (strlen(pszFileName) > 4 && pszFileName[4] == ':')	/* fat3: */
		return true;
#endif

	return false;
}


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


/**
 * Check if filename end with a '/'
 *
 * Return TRUE if filename ends with '/'
 */
bool File_DoesFileNameEndWithSlash(char *pszFileName)
{
	if (pszFileName[0] == '\0')    /* If NULL string return! */
		return false;

	/* Does string end in a '/'? */
	if (pszFileName[strlen(pszFileName)-1] == PATHSEP)
		return true;

	return false;
}


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
		return NULL;
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


/**
 * Return basename of given path (pointer to file name part of same string)
 */
const char *File_Basename(const char *path)
{
	const char *basename;
	if ((basename = strrchr(path, PATHSEP)))
		return basename + 1;
	return path;
}


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
		memmove(pDir, pSrcFileName, ptr1-pSrcFileName);
		pDir[ptr1-pSrcFileName] = 0;
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
	}
	else
	{
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

/**
 * Like File_MakePath(), but puts the string into a pre-allocated buffer
 * instead of returning a freshly allocated memory buffer.
 */
int File_MakePathBuf(char *buf, size_t buflen, const char *pDir,
                     const char *pName, const char *pExt)
{
	char *tmp;

	if (!buflen)
		return -EINVAL;

	tmp = File_MakePath(pDir, pName, pExt);
	if (!tmp)
	{
		buf[0] = '\0';
		return -ENOMEM;
	}

	strncpy(buf, tmp, buflen);
	free(tmp);
	if (buf[buflen - 1])
	{
		buf[buflen - 1] = '\0';
		return -E2BIG;
	}

	return 0;
}


/**
 * Shrink a file name to a certain length and insert some dots if we cut
 * something away (useful for showing file names in a dialog).
 * Note: maxlen is the maximum length of the destination string, _not_
 * including the final '\0' byte! So the destination buffer has to be
 * at least one byte bigger than maxlen.
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


/**
 * Wrapper for File_MakeAbsoluteName() which special-cases stdin/out/err
 * named files and empty file name.  The given buffer should be opened
 * with File_Open() and closed with File_Close() if this function is used!
 * (On Linux one can use /dev/stdout etc, this is intended for other OSes)
 */
void File_MakeAbsoluteSpecialName(char *path)
{
	if (path[0] &&
	    strcmp(path, "stdin")  != 0 &&
	    strcmp(path, "stdout") != 0 &&
	    strcmp(path, "stderr") != 0)
		File_MakeAbsoluteName(path);
}


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
		if (!File_GetCWD(pTempName, FILENAME_MAX))
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
		else if (pFileName[inpos] == '.' && pFileName[inpos+1] == 0)
		{
			inpos += 1;        /* Ignore "." at the end of the path string */
			if (outpos > 1)
				pTempName[outpos - 1] = 0;   /* Remove the last slash, too */
		}
		else if (pFileName[inpos] == '.' && pFileName[inpos+1] == '.'
		         && (pFileName[inpos+2] == PATHSEP || pFileName[inpos+2] == 0))
		{
			/* Handle "../" */
			char *pSlashPos;
			inpos += 2;
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
			/* Were we already at the end of the string or is there more to come? */
			if (pFileName[inpos] == PATHSEP)
			{
				/* There was a slash after the '..', so skip slash and
				 * simply proceed with next part */
				inpos += 1;
			}
			else
			{
				/* We were at the end of the string, so let's remove the slash
				 * from the new string, too */
				if (outpos > 1)
					pTempName[outpos - 1] = 0;
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

	strcpy(pFileName, pTempName);          /* Copy back */
	free(pTempName);
}


/**
 * Remove given number of path elements from the end of the given path.
 * Leaves '/' at the end if path still has directories. Given path
 * may not be empty.
 */
void File_PathShorten(char *path, int dirs)
{
	int n = 0;
	/* ignore last char, it may or may not be '/' */
	int i = strlen(path) - 1;
#ifdef WIN32
	int len = i;
#endif
	assert(i >= 0);
	while(i > 0 && n < dirs)
	{
		if (path[--i] == PATHSEP)
			n++;
	}
#ifdef WIN32
	/* if we have only drive with root, don't shorten it more*/
	if (len == 2 && n == 0 && path[1] == ':' && path[2] == PATHSEP)
		return;
#endif
	if (path[i] == PATHSEP)
	{
		path[i+1] = '\0';
	}
	else
	{
		path[0] = PATHSEP;
		path[1] = '\0';
	}
}


/*
  If "/." or "/.." at end, remove that and in case of ".." remove
  also preceding dir (go one dir up).  Leave '/' at the end of
  the path.
*/
void File_HandleDotDirs(char *path)
{
	int len = strlen(path);
	if (len >= 2 &&
	    path[len-2] == PATHSEP &&
	    path[len-1] == '.')
	{
		/* keep in same dir */
		path[len-1] = '\0';
	}
	else if (len >= 3 &&
	    path[len-3] == PATHSEP &&
	    path[len-2] == '.' &&
	    path[len-1] == '.')
	{
		/* go one dir up */
		if (len == 3)
		{
			path[1] = 0;		/* already root */
		}
		else
		{
			char *ptr;
			path[len-3] = 0;
			ptr = strrchr(path, PATHSEP);
			if (ptr)
				*(ptr+1) = 0;
		}
	}
}
