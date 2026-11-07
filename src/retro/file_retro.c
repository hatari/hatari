/*
  Hatari - file_retro.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Common file access functions for the libretro build
*/
const char File_fileid[] = "Hatari file_retro.c";

#include <errno.h>
#include <unistd.h>

#include "main.h"
#include "file.h"
#include "filename.h"
#include "file_archive.h"
#include "vfs.h"

/**
 * Read file from disk into allocated buffer and return the buffer
 * unmodified, or NULL for error.  If pFileSize is non-NULL, read
 * file size is set to that.
 */
uint8_t *File_ReadAsIs(const char *pszFileName, long *pFileSize)
{
	uint8_t *pFile = NULL;
	size_t filesize = 0;
	VFS_FILE hVfsFile;

	hVfsFile = VFS_fopen(pszFileName, "rb");
	if (hVfsFile != NULL)
	{
		filesize = VFS_fsize(hVfsFile);
		if (filesize > 0)
		{
			pFile = malloc(filesize);
			if (pFile)
				filesize = (long)VFS_fread(pFile, 1, filesize, hVfsFile);
		}
		VFS_fclose(hVfsFile);
	}

	/* Store size of file we read in (or 0 if failed) */
	if (pFileSize)
		*pFileSize = filesize;

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
		filepath = FileName_FindPossibleExt(pszFileName, ppszExts);
	}
	if (!filepath)
		filepath = strdup(pszFileName);

	/* It is a normal file */
	pFile = File_ReadAsIs(filepath, &FileSize);

	free(filepath);

	/* Store size of file we read in (or 0 if failed) */
	if (pFileSize)
		*pFileSize = FileSize;

	return pFile;        /* Return to where read in/allocated */
}


/**
 * Save file to disk, return FALSE if errors
 */
bool File_Save(const char *pszFileName, const uint8_t *pAddress, size_t Size, bool bQueryOverwrite)
{
	VFS_FILE hVfsFile;
	bool bRet = false;

	hVfsFile = VFS_fopen(pszFileName, "wb");
	if (hVfsFile != NULL)
	{
		/* Write data, set success flag */
		if (VFS_fwrite(pAddress, 1, Size, hVfsFile) == Size)
			bRet = true;
		VFS_fclose(hVfsFile);
	}

	return bRet;
}


/**
 * Return size of file, -1 if error
 */
off_t File_Length(const char *pszFileName)
{
	int64_t len = VFS_FileLength(pszFileName);

	return (len >= 0) ? (off_t)len : (off_t)-1;
}


/**
 * Return TRUE if file exists, is readable or writable at least and is not
 * a directory.
 */
bool File_Exists(const char *filename)
{
	return VFS_FileExists(filename);
}


/**
 * Return TRUE if directory exists.
 */
bool File_DirExists(const char *path)
{
	return VFS_DirExists(path);
}


/**
 * Find if file exists, and if so ask user if OK to overwrite
 */
bool File_QueryOverwrite(const char *pszFileName)
{
	return true;  /* Query dialog not implemented in libretro */
}


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

	/* Open a normal log file */
	fp = fopen(path, mode);
	if (!fp)
		fprintf(stderr, "Can't open file '%s' (wr=%i, rd=%i):\n  %s\n",
			path, wr, rd, strerror(errno));

	return fp;
}


/**
 * Close given FILE pointer and return the closed pointer
 * as NULL for the idiom "fp = File_Close(fp);"
 */
FILE *File_Close(FILE *fp)
{
	if (fp)
		fclose(fp);

	return NULL;
}


/**
 * Takes advisory, exclusive lock on given file (FILE *).
 * Returns false if locking fails (e.g. another Hatari
 * instance has already file open for writing).
 */
bool File_Lock(FILE *fp)
{
	return true;  /* TODO: Not implemented for libretro yet */
}


/**
 * Releases advisory, exclusive lock on given file (FILE *).
 */
void File_UnLock(FILE *fp)
{
	/* TODO */
}


/**
 * Check if input is available at the specified file descriptor.
 */
bool File_InputAvailable(FILE *fp)
{
	return false;  /* TODO: Not implemented for libretro yet */
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


uint8_t *File_ZlibRead(const char *filename, long *size)
{
	return NULL;
}

bool Archive_FileNameIsSupported(const char *filename)
{
	return false;
}

uint8_t *Archive_ReadDisk(int drive, const char *fname, const char *archpath,
                          long *imagesize, int *imagetype)
{
	return NULL;
}

bool Archive_WriteDisk(int drive, const char *fname, unsigned char *buf, int size)
{
	return false;
}
