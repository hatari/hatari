/*
  Hatari - file.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Common file access functions.
*/
const char File_fileid[] = "Hatari file.c";

#include "main.h"
#include <sys/types.h>
#include <sys/stat.h>
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#if HAVE_ZLIB_H
#include <zlib.h>
#endif
#if defined(WIN32) && !defined(_VCWIN_)
#include <winsock2.h>
#endif

#include "dialog.h"
#include "file.h"
#include "str.h"
#include "file_archive.h"

#ifdef HAVE_FLOCK
# include <sys/file.h>
#endif
#if defined(__APPLE__)
#include <sys/disk.h>
#endif


/**
 * Read file via zlib into a newly allocated buffer and return the buffer
 * or NULL for error. If pFileSize is non-NULL, read file size is set to that.
 */
#if HAVE_LIBZ
uint8_t *File_ZlibRead(const char *pszFileName, long *pFileSize)
{
	uint8_t *pFile = NULL;
	gzFile hGzFile;
	long nFileSize = 0;

	hGzFile = gzopen(pszFileName, "rb");
	if (hGzFile != NULL)
	{
		/* Find size of file: */
		do
		{
			/* Seek through the file until we hit the end... */
			char tmp[1024];
			if (gzread(hGzFile, tmp, sizeof(tmp)) < 0)
			{
				gzclose(hGzFile);
				fprintf(stderr, "Failed to read gzip file!\n");
					return NULL;
			}
		}
		while (!gzeof(hGzFile));
		nFileSize = gztell(hGzFile);
		gzrewind(hGzFile);

		/* Read in... */
		pFile = malloc(nFileSize);
		if (pFile)
			nFileSize = gzread(hGzFile, pFile, nFileSize);

		gzclose(hGzFile);
	}

	if (pFileSize)
		*pFileSize = nFileSize;

	return pFile;
}
#endif

/**
 * Read file from disk into allocated buffer and return the buffer
 * unmodified, or NULL for error.  If pFileSize is non-NULL, read
 * file size is set to that.
 */
uint8_t *File_ReadAsIs(const char *pszFileName, long *pFileSize)
{
	uint8_t *pFile = NULL;
	long FileSize = 0;
	FILE *hDiskFile;

	/* Open and read normal file */
	hDiskFile = fopen(pszFileName, "rb");
	if (hDiskFile != NULL)
	{
		/* Find size of file: */
		if (fseek(hDiskFile, 0, SEEK_END) == 0)
		{
			FileSize = ftell(hDiskFile);
			if (FileSize > 0 && fseek(hDiskFile, 0, SEEK_SET) == 0)
			{
				/* Read in... */
				pFile = malloc(FileSize);
				if (pFile)
					FileSize = fread(pFile, 1, FileSize, hDiskFile);
			}
		}
		fclose(hDiskFile);
	}

	/* Store size of file we read in (or 0 if failed) */
	if (pFileSize)
		*pFileSize = FileSize;

	return pFile;        /* Return to where read in/allocated */
}

/**
 * Read file from disk into allocated buffer and return the buffer or
 * NULL for error.  If file is missing, given additional compression
 * extension are checked too.  If file has one of the supported
 * compression extension, its contents are uncompressed (in case of
 * ZIP, first file in it is read).  If pFileSize is non-NULL, read
 * file size is set to that.
 */
uint8_t *File_Read(const char *pszFileName, long *pFileSize, const char * const ppszExts[])
{
	char *filepath = NULL;
	uint8_t *pFile = NULL;
	long FileSize = 0;

	/* Does the file exist? If not, see if can scan for other extensions and try these */
	if (!File_Exists(pszFileName) && ppszExts)
	{
		/* Try other extensions, if succeeds, returns correct one */
		filepath = File_FindPossibleExtFileName(pszFileName, ppszExts);
	}
	if (!filepath)
		filepath = strdup(pszFileName);

#if HAVE_LIBZ
	/* Is it a gzipped file? */
	if (File_DoesFileExtensionMatch(filepath, ".gz"))
	{
		pFile = File_ZlibRead(filepath, &FileSize);
	}
#if HAVE_LIBARCHIVE
	else if (Archive_FileNameIsSupported(filepath))
	{
		/* It's a supported archive (zip,rar,7z,...) : try to load the first file in the archive */
		pFile = Archive_ReadFirstFile(filepath, &FileSize, ppszExts);
	}
#endif
	else
#endif  /* HAVE_LIBZ */
	{
		/* It is a normal file */
		pFile = File_ReadAsIs(filepath, &FileSize);
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
 * If built with ZLib support + file name ends with *.gz, compress it first
 */
bool File_Save(const char *pszFileName, const uint8_t *pAddress, size_t Size, bool bQueryOverwrite)
{
	bool bRet = false;

	/* Check if need to ask user if to overwrite */
	if (bQueryOverwrite)
	{
		/* If file exists, ask if OK to overwrite */
		if (!File_QueryOverwrite(pszFileName))
			return false;
	}

#if HAVE_LIBZ
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
				bRet = true;

			gzclose(hGzFile);
		}
	}
	else
#endif  /* HAVE_LIBZ */
	{
		FILE *hDiskFile;
		/* Create a normal file: */
		hDiskFile = fopen(pszFileName, "wb");
		if (hDiskFile != NULL)
		{
			/* Write data, set success flag */
			if (fwrite(pAddress, 1, Size, hDiskFile) == Size)
				bRet = true;

			fclose(hDiskFile);
		}
	}

	return bRet;
}


/*-----------------------------------------------------------------------*/
/**
 * Return size of file, -1 if error
 */
off_t File_Length(const char *pszFileName)
{
	FILE *hDiskFile;
	off_t FileSize;

	hDiskFile = fopen(pszFileName, "rb");
	if (hDiskFile!=NULL)
	{
#if defined(__APPLE__)
		/* special handling for character/block devices on macOS, where the
		   seeking method doesn't determine the size */
		struct stat buf;
		unsigned long blocksize;
		unsigned long numblocks;
		int fd = fileno(hDiskFile);

		if ((fstat(fd, &buf) == 0) &&
		    (S_ISBLK(buf.st_mode) || S_ISCHR(buf.st_mode)))
		    /* both S_ISBLK() and S_ISCHR() is needed, because /dev/rdisk?
		       devices in macOS identify themselves as character devices,
		       not block devices, meanwhile /dev/disk?, which are a lot
		       slower and buffered, say they are block devices...
		       Thanks, Apple!
		       This hack allows either of them to be used by Hatari. */
		{
			if ((ioctl(fd, DKIOCGETBLOCKSIZE, &blocksize) == 0) &&
			    (ioctl(fd, DKIOCGETBLOCKCOUNT, &numblocks) == 0))
			{
				FileSize = blocksize * numblocks;
				return FileSize;
			}
		}
#endif
		fseeko(hDiskFile, 0, SEEK_END);
		FileSize = ftello(hDiskFile);
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
bool File_Exists(const char *filename)
{
	struct stat buf;
	if (stat(filename, &buf) == 0 &&
	    (buf.st_mode & (S_IRUSR|S_IWUSR)) && !S_ISDIR(buf.st_mode))
	{
		/* file points to user readable regular file */
		return true;
	}
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Return TRUE if directory exists.
 */
bool File_DirExists(const char *path)
{
	struct stat buf;
	return (stat(path, &buf) == 0 && S_ISDIR(buf.st_mode));
}


/*-----------------------------------------------------------------------*/
/**
 * Find if file exists, and if so ask user if OK to overwrite
 */
bool File_QueryOverwrite(const char *pszFileName)
{
	const char *fmt;
	char *szString;
	bool ret = true;

	/* Try and find if file exists */
	if (File_Exists(pszFileName))
	{
		fmt = "File '%s' exists, overwrite?";
		/* File does exist, are we OK to overwrite? */
		szString = malloc(strlen(pszFileName) + strlen(fmt) + 1);
		if ( !szString )
			return false;
		sprintf(szString, fmt, pszFileName);
		fprintf(stderr, "%s\n", szString);
		ret = DlgAlert_Query(szString);
		free(szString);
	}
	return ret;
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

	/* empty name signifies file that shouldn't be opened/enabled */
	if (!path || !*path)
		return NULL;

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
		fprintf(stderr, "Can't open file '%s' (wr=%i, rd=%i):\n  %s\n",
			path, wr, rd, strerror(errno));

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
 * Internal lock function for File_Lock() / File_UnLock().
 * Returns true on success, otherwise false.
 */
static bool lock_operation(FILE *fp, int cmd)
{
#ifndef HAVE_FLOCK
# define DO_LOCK	0
# define DO_UNLOCK	0
	return true;
#else
# define DO_LOCK	(LOCK_EX|LOCK_NB)
# define DO_UNLOCK	(LOCK_UN)
	/* Advantage of locking is only small bit of extra safety if
	 * one runs (e.g. accidentally) multiple Hatari instances at
	 * same time, so replacing it with no-op is no big deal.
	 *
	 * NOTE: this uses BSD file locking as it's a bit more usable than POSIX one:
	 *	http://0pointer.de/blog/projects/locking.html
	 */
	int ret, fd = fileno(fp);
	if (fd < 0)
		return false;
	ret = flock(fd, cmd);
	return (ret >= 0);
#endif
}

/*-----------------------------------------------------------------------*/
/**
 * Takes advisory, exclusive lock on given file (FILE *).
 * Returns false if locking fails (e.g. another Hatari
 * instance has already file open for writing).
 */
bool File_Lock(FILE *fp)
{
	return lock_operation(fp, DO_LOCK);
}

/*-----------------------------------------------------------------------*/
/**
 * Releases advisory, exclusive lock on given file (FILE *).
 */
void File_UnLock(FILE *fp)
{
	lock_operation(fp, DO_UNLOCK);
}


/*-----------------------------------------------------------------------*/
/**
 * Check if input is available at the specified file descriptor.
 */
bool File_InputAvailable(FILE *fp)
{
#if HAVE_SELECT
	fd_set rfds;
	struct timeval tv;
	int fh;
	int ret;

	if (!fp || (fh = fileno(fp)) == -1)
		return false;

	/* Add the file handle to the file descriptor set */
	FD_ZERO(&rfds);
	FD_SET(fh, &rfds);

	/* Return immediately */
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	/* Check if file descriptor is ready for a read */
	ret = select(fh+1, &rfds, NULL, NULL, &tv);

	if (ret > 0)
		return true;    /* Data available */
#endif

	return false;
}


/**
 * Create a valid path name from a possibly invalid name by erasing invalid
 * path parts at the end of the string.  If string doesn't contain any path
 * component, it will be pointed to the root directory.  Empty string will
 * be left as-is to prevent overwriting past allocated area.
 */
void File_MakeValidPathName(char *pPathName)
{
	struct stat dirstat;
	char *pLastSlash;

	if (!pPathName[0])
		return;		/* Avoid writing to zero-size buffers */

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
		else
		{
			/* point to root */
			pPathName[0] = PATHSEP;
			pPathName[1] = 0;
			return;
		}
	}
	while (pLastSlash);

	/* Make sure that path name ends with a slash */
	File_AddSlashToEndFileName(pPathName);
}


/**
 * A simple wrapper around getcwd()
 */
char *File_GetCWD(char *buf, size_t bufsize)
{
	return getcwd(buf, bufsize);
}


#if defined(WIN32)
static TCHAR szTempFileName[MAX_PATH];

/*-----------------------------------------------------------------------*/
/**
 * Get temporary filename for Windows
 */
static char* WinTmpFile(void)
{
	DWORD dwRetVal = 0;
	UINT uRetVal   = 0;
	TCHAR lpTempPathBuffer[MAX_PATH];

	/* Gets the temp path env string (no guarantee it's a valid path) */
	dwRetVal = GetTempPath(MAX_PATH,		/* length of the buffer */
	                       lpTempPathBuffer);	/* buffer for path */
	if (dwRetVal > MAX_PATH || (dwRetVal == 0))
	{
		fprintf(stderr, "GetTempPath failed.\n");
		return NULL;
	}

	/* Generates a temporary file name */
	uRetVal = GetTempFileName(lpTempPathBuffer,	/* directory for tmp files */
	                          TEXT("HATARI"),	/* temp file name prefix */
	                          0,			/* create unique name */
	                          szTempFileName);	/* buffer for name */
	if (uRetVal == 0)
	{
		fprintf(stderr, "GetTempFileName failed.\n");
		return NULL;
	}
	return (char*)szTempFileName;
}
#endif

/**
 * Open a temporary file. This is a wrapper for tmpfile() that can be used
 * on Windows, too. If *psFileName gets set by this function, the caller
 * should delete the file manually when it is not needed anymore.
 */
FILE *File_OpenTempFile(char **psFileName)
{
	FILE *fh;
	char *psTmpName = NULL;

	fh = tmpfile();            /* Open temporary file */

#if defined(WIN32)
	if (!fh)
	{
		/* Unfortunately tmpfile() needs administrative privileges on
		 * Windows, so if it failed, let's work around this issue. */
		psTmpName = WinTmpFile();
		fh = fopen(psTmpName, "w+b");
	}
#endif

	if (psFileName)
	{
		*psFileName = psTmpName;
	}

	return fh;
}
