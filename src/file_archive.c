/*
  Hatari - file_archive.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  2026/03/08	Nicolas Pomarède
    Drop old zlib's code and use libarchive instead to handle all kinds of archives (zip, rar, 7z, ...)
    zlib is used only to handle .gz files and libarchive is required for browsing inside zip/rar/7z/...
*/
const char File_Archive_fileid[] = "Hatari file_archive.c";

#include "main.h"
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>

#if HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#define ARCHIVE_READ_BLOCK 10240
#endif

#include "dim.h"
#include "file.h"
#include "floppy.h"
#include "floppy_ipf.h"
#include "floppy_stx.h"
#include "floppy_scp.h"
#include "log.h"
#include "msa.h"
#include "st.h"
#include "str.h"
#include "file_archive.h"

#ifdef QNX
#include <sys/dir.h>
#define dirent direct
#endif


#if HAVE_LIBARCHIVE

#define FILE_ARCHIVE_PATH_MAX  256


/* Possible file extensions to handle with libarchive */
static const char * const ArchiveExts[] =
{
	".zip",
	".7z",
	".rar",
	".lha",
	".lzh",
	".tar",
	".tar.gz",
	".tgz",
	".tar.bz2",
	".tbz",
	NULL
};

/* Possible disk image extensions to scan for inside an archive */
static const char * const pszDiskNameExts[] =
{
	".msa",
	".st",
	".dim",
	".ipf",
	".raw",
	".ctr",
	".stx",
	".scp",
	NULL
};



//#define DEBUGPRINT(x) fprintf x
#define DEBUGPRINT(x)



/*-----------------------------------------------------------------------*/
/**
 * Check if a file name contains a slash or backslash and return its position.
 */
static int Archive_FileNameHasSlash(const char *fn)
{
	int i=0;

	while (fn[i] != '\0')
	{
		if (fn[i] == '\\' || fn[i] == '/')
			return i;
		i++;
	}
	return -1;
}


/*-----------------------------------------------------------------------*/
/**
 * Free the memory that has been allocated for a archive_dir.
 */
void	Archive_FreeArcDir(archive_dir *pArcDir)
{
DEBUGPRINT (( stderr , "Archive_FreeArcDir %d\n" , pArcDir->nfiles ));
	while (pArcDir->nfiles > 0)
	{
		pArcDir->nfiles--;
DEBUGPRINT (( stderr , "Archive_FreeArcDir %d %p %s\n" , pArcDir->nfiles , pArcDir->names[pArcDir->nfiles] , pArcDir->names[pArcDir->nfiles] ));
		free(pArcDir->names[pArcDir->nfiles]);
		pArcDir->names[pArcDir->nfiles] = NULL;
DEBUGPRINT (( stderr , "Archive_FreeArcDir %d %p ok\n" , pArcDir->nfiles , pArcDir->names[pArcDir->nfiles] ));
	}
	free(pArcDir->names);
	pArcDir->names = NULL;
	free(pArcDir);
}


/*-----------------------------------------------------------------------*/
/**
 * Free the memory that has been allocated for fentries.
 */
static void	Archive_FreeFentries ( struct dirent **fentries, int entries )
{
	while (entries > 0)
	{
		entries--;
		free(fentries[entries]);
	}
	free(fentries);
}


/*-----------------------------------------------------------------------*/
/**
 *   Returns a list of files from the directory (dir) in an archive file list
 *   Sets *pEntries to the number of entries and returns a dirent structure, or
 *   NULL on failure. NOTE: only f_name is set in the dirent structures.
 */
struct dirent	**Archive_GetFilesDir ( const archive_dir *pArcDir, const char *dir, int *pEntries )
{
	int i,j;
	archive_dir *files;
	char *temp;
	bool flag;
	int slash;
	struct dirent **fentries;

	files = (archive_dir *)malloc(sizeof(archive_dir));
	if (!files)
	{
		perror("Archive_GetFilesDir");
		return NULL;
	}

	files->names = (char **)malloc((pArcDir->nfiles + 1) * sizeof(char *));
	if (!files->names)
	{
		perror("Archive_GetFilesDir");
		free(files);
		return NULL;
	}

	/* add ".." directory */
	files->nfiles = 0;
	temp = (char *)malloc(4);
	if (!temp)
	{
		Archive_FreeArcDir(files);
		return NULL;
	}
	temp[0] = temp[1] = '.';
	temp[2] = '/';
	temp[3] = '\0';
	files->names[0] = temp;
	files->nfiles++;

	for (i = 0; i < pArcDir->nfiles; i++)
	{
		if (strlen(pArcDir->names[i]) > strlen(dir))
		{
			if (strncasecmp(pArcDir->names[i], dir, strlen(dir)) == 0)
			{
				temp = pArcDir->names[i];
				temp = (char *)(temp + strlen(dir));
				if (temp[0] != '\0')
				{
					if ((slash=Archive_FileNameHasSlash(temp)) > 0)
					{
						/* file is in a subdirectory, add this subdirectory if it doesn't exist in the list */
						flag = false;
						for (j = files->nfiles-1; j > 0; j--)
						{
							if (strncasecmp(temp, files->names[j], slash+1) == 0)
								flag = true;
						}
						if (flag == false)
						{
							char *subdir = malloc(slash+2);
							if (!subdir)
							{
								perror("Archive_GetFilesDir");
								Archive_FreeArcDir(files);
								return NULL;
							}
							strncpy(subdir, temp, slash+1);
							subdir[slash+1] = '\0';
							files->names[files->nfiles] = subdir;
							files->nfiles++;
						}
					}
					else
					{
						/* add a filename */
						files->names[files->nfiles] = strdup(temp);
						if (!files->names[files->nfiles])
						{
							perror("Archive_GetFilesDir");
							Archive_FreeArcDir(files);
							return NULL;
						}
						files->nfiles++;
					}
				}
			}
		}
	}

	/* copy to a dirent structure */
	*pEntries = files->nfiles;
	fentries = (struct dirent **)malloc(sizeof(struct dirent *)*files->nfiles);
	if (!fentries)
	{
		perror("Archive_GetFilesDir");
		Archive_FreeArcDir(files);
		return NULL;
	}
	for (i = 0; i < files->nfiles; i++)
	{
		fentries[i] = (struct dirent *)malloc(sizeof(struct dirent));
		if (!fentries[i])
		{
			perror("Archive_GetFilesDir");
			Archive_FreeFentries(fentries, i+1);
			Archive_FreeArcDir(files);
			return NULL;
		}
		strncpy(fentries[i]->d_name, files->names[i], sizeof(fentries[i]->d_name)-1);
		fentries[i]->d_name[sizeof(fentries[i]->d_name) - 1] = 0;
	}

	Archive_FreeArcDir(files);

	return fentries;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the first matching file in an archive, or NULL on failure.
 * String buffer size is FILE_ARCHIVE_PATH_MAX
 */
static char *Archive_FirstFile(const char *filename, const char * const ppsExts[])
{
	archive_dir *files;
	int i, j;
	char *name;

	files = Archive_GetFiles ( filename );
	if (files == NULL)
		return NULL;

	name = malloc(FILE_ARCHIVE_PATH_MAX);
	if (!name)
	{
		perror("Archive_FirstFile");
		Archive_FreeArcDir(files);
		return NULL;
	}
	name[0] = '\0';

	/* Do we have to scan for a certain extension? */
	if (ppsExts)
	{
		for(i = files->nfiles-1; i >= 0; i--)
		{
			for (j = 0; ppsExts[j] != NULL; j++)
			{
				if (File_DoesFileExtensionMatch(files->names[i], ppsExts[j])
				    && strlen(files->names[i]) < FILE_ARCHIVE_PATH_MAX - 1)
				{
					strcpy(name, files->names[i]);
					break;
				}
			}
		}
	}
	else
	{
		/* There was no extension given -> use the very first name */
		if (strlen(files->names[0]) < FILE_ARCHIVE_PATH_MAX - 1)
		{
			strcpy(name, files->names[0]);
		}
	}

	/* free the files */
	Archive_FreeArcDir(files);

	if (name[0] == '\0')
	{
		free(name);
		return NULL;
	}

	return name;
}




/*-----------------------------------------------------------------------*/
/**
 * Does filename end with a supported archive extension ? If so, return true.
 */
bool Archive_FileNameIsSupported ( const char *FileName )
{
	int	i;

	for ( i = 0; ArchiveExts[i] != NULL; i++ )
	{
		if ( File_DoesFileExtensionMatch ( FileName , ArchiveExts[i] ) )
		{
			return true;
		}
	}

	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Returns a list of files from an archive file. returns NULL on failure,
 * returns a pointer to an array of strings if successful. Sets nfiles
 * to the number of files.
 */
archive_dir	*Archive_GetFiles ( const char *FileName )
{
	int		nfiles;
	char 		**filelist = NULL;
	const char	*entry_pathname;
	archive_dir	*ad = NULL;
	struct archive	*arc;
	struct archive_entry *entry;
	int		r;

	arc = archive_read_new();
	archive_read_support_filter_all ( arc );
	archive_read_support_format_all ( arc );

	r = archive_read_open_filename ( arc, FileName, ARCHIVE_READ_BLOCK );
	if ( r != ARCHIVE_OK )
	{
		Log_Printf(LOG_ERROR, "Archive_GetFiles: Cannot open %s\n", FileName);
		return NULL;
	}

	nfiles = 0;
	while ( archive_read_next_header ( arc, &entry ) == ARCHIVE_OK )
	{
		if ( nfiles == 0 )		/* allocate a file list with 1 element at start*/
			filelist = (char **) malloc ( sizeof(char *) );
		else				/* increase by 1 element */
			filelist = (char **) realloc ( filelist , sizeof(char *) * ( nfiles+1 ) );
		if ( !filelist )
		{
			perror("Archive_GetFiles malloc/realloc");
			goto cleanup;
		}

		entry_pathname = archive_entry_pathname ( entry );

		/* Alloc an extra char in case a trailing '/' must be added below for directories */
		filelist[ nfiles ] = (char *) malloc ( strlen ( entry_pathname ) + 2 );
		if ( !filelist[ nfiles ] )
		{
			perror("Archive_GetFiles");
			goto cleanup;
		}

		strcpy ( filelist[ nfiles ], entry_pathname );

		/* Some archive format (eg RAR) don't add a trailing '/' to the directoty name */
		/* We add it if needed as this is required for our functions accessing files */
		if ( archive_entry_filetype(entry) == AE_IFDIR )
			File_AddSlashToEndFileName ( filelist[ nfiles ] );

DEBUGPRINT (( stderr, "arc new : nfiles=%d path=%s size=%ld type=0x%x\n" , nfiles , filelist[ nfiles ] ,archive_entry_size(entry),archive_entry_filetype(entry)  ));
		nfiles++;
	}

	ad = (archive_dir *) malloc ( sizeof(archive_dir) );
	if (ad)
	{
		ad->names = filelist;
		ad->nfiles = nfiles;
	}
	else
	{
		perror("Archive_GetFiles");
	}

cleanup:
	r = archive_read_free(arc);
DEBUGPRINT (( stderr, "arc new : nfiles=%d filelist=%p\n" , nfiles , filelist ));

	if ( !ad && filelist )
	{
		/* deallocate memory */
		for ( ; nfiles > 0; nfiles-- )
			free ( filelist[ nfiles-1 ] );
		free ( filelist );
	}

	return ad;
}


/*-----------------------------------------------------------------------*/
/**
 * Locate a file (with its full path) inside an archive.
 * Return true if file is found, else false
 * If file is found then arc_entry_ptr will be updated to the entry associated
 * with this file
 */
static bool Arc_LocateFile ( struct archive *arc , struct archive_entry	**arc_entry_ptr , char *FileName )
{
	bool			found;

	found = false;
	while ( archive_read_next_header ( arc, arc_entry_ptr ) == ARCHIVE_OK )
	{
		if ( strcmp ( FileName , archive_entry_pathname ( *arc_entry_ptr ) ) == 0 )
		{
			found = true;
			break;
		}
	}

	return found;
}


/*-----------------------------------------------------------------------*/
/**
 * Check a floppy disk image file in the archive
 * If the filename's extensions matches a supported disk image then
 * we return the uncompressed length and we update *pImageType
 * If filename is not a supported disk image, return 0
 * In case of error, return -1
 */
static long Archive_CheckImageFile ( struct archive *arc, char *FileName, int *pImageType )
{
	struct archive_entry	*arc_entry;
	int			uncompressed_size;


DEBUGPRINT (( stderr , "Archive_CheckImageFile new file=%s\n", FileName ));

	if ( !Arc_LocateFile ( arc , &arc_entry , FileName ) )
	{
		Log_Printf(LOG_ERROR, "File \"%s\" not found in the archive!\n", FileName);
		return -1;
	}
	uncompressed_size = archive_entry_size ( arc_entry );

	*pImageType = FLOPPY_IMAGE_TYPE_NONE;

	/* check for .stx, .ipf, scp, .msa, .dim or .st extension */
	if ( STX_FileNameIsSTX(FileName, false) )
		*pImageType = FLOPPY_IMAGE_TYPE_STX;

	else if ( IPF_FileNameIsIPF(FileName, false) )
		*pImageType = FLOPPY_IMAGE_TYPE_IPF;

	else if ( SCP_FileNameIsSCP(FileName, false) )
		*pImageType = FLOPPY_IMAGE_TYPE_SCP;

	else if ( MSA_FileNameIsMSA(FileName, false) )
		*pImageType = FLOPPY_IMAGE_TYPE_MSA;

	else if ( ST_FileNameIsST(FileName, false) )
		*pImageType = FLOPPY_IMAGE_TYPE_ST;

	else if ( DIM_FileNameIsDIM(FileName, false) )
		*pImageType = FLOPPY_IMAGE_TYPE_DIM;

	/* Known extension found, return uncompressed size */
	if ( pImageType != FLOPPY_IMAGE_TYPE_NONE )
		return uncompressed_size;

	Log_Printf ( LOG_ERROR, "Not an .ST, .MSA, .DIM, .IPF, .STX or .SCP file.\n" );
	return 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Extract a file from an archive
 * The extracted file is the one in the current archive_entry
 * Returns a pointer to a buffer containing the uncompressed data, or NULL.
 */
static void *Archive_ExtractFile ( struct archive *arc, size_t size )
{
	uint8_t *	buf;
	size_t		size_buf;
	size_t		last_read;
	size_t		total_read;

	size_buf = size;
	buf = malloc ( size_buf );
	if ( !buf )
	{
		perror("Archive_ExtractFile");
		return NULL;
	}


	/* Handle the case where archive_read_data returns less than size_buf bytes */
	total_read = 0;
	while ( ( last_read = archive_read_data ( arc, buf + total_read , size_buf - total_read ) ) > 0 )
		total_read += last_read;

	if ( total_read != size_buf )
	{
		Log_Printf ( LOG_ERROR, "Archive_ExtractFile: could not read file\n" );
		free(buf);
		return NULL;
	}

	return buf;
}


/*-----------------------------------------------------------------------*/
/**
 * Load a disk image "FileName" with an optional path from an archive into memory,
 * set the number of bytes loaded into pImageSize and return the data or NULL on error.
 */
uint8_t *Archive_ReadDisk ( int Drive, const char *FileName, const char *ArchivePath, long *pImageSize, int *pImageType )
{
	struct archive	*arc;
	int		r;
	long		ImageSize = 0;
	uint8_t		*buf;
	char		*path;
	uint8_t		*pDiskBuffer = NULL;

	*pImageSize = 0;
	*pImageType = FLOPPY_IMAGE_TYPE_NONE;

	arc = archive_read_new();
	archive_read_support_filter_all ( arc );
	archive_read_support_format_all ( arc );

	r = archive_read_open_filename ( arc, FileName, ARCHIVE_READ_BLOCK );
	if ( r != ARCHIVE_OK )
	{
		Log_Printf(LOG_ERROR, "Archive_ReadDisk: Cannot open %s\n", FileName);
		return NULL;
	}

	if ( ArchivePath == NULL || ArchivePath[0] == 0 )
	{
DEBUGPRINT (( stderr , "Archive_ReadDisk new first filename=%s\n" , FileName ));
		path = Archive_FirstFile ( FileName, pszDiskNameExts );
		if ( path == NULL )
		{
			Log_Printf ( LOG_ERROR, "Cannot open %s\n", FileName );
			archive_read_free ( arc );
			return NULL;
		}
	}
	else
	{
DEBUGPRINT (( stderr , "Archive_ReadDisk news path=%s filename=%s\n" , ArchivePath , FileName ));
		path = malloc ( FILE_ARCHIVE_PATH_MAX );
		if ( path == NULL )
		{
			perror("Archive_ReadDisk");
			archive_read_free ( arc );
			return NULL;
		}
		strncpy ( path, ArchivePath, FILE_ARCHIVE_PATH_MAX - 1 );
		path[FILE_ARCHIVE_PATH_MAX-1] = '\0';
	}

	ImageSize = Archive_CheckImageFile ( arc, path, pImageType );
	if ( ImageSize <= 0 )
	{
		archive_read_free ( arc );
		free ( path );
		return NULL;
	}

	/* Extract the current archive_entry set by Archive_CheckImageFile */
	buf = Archive_ExtractFile ( arc, ImageSize );
	if ( buf == NULL )
	{
		archive_read_free ( arc );
		free ( path );
		return NULL;
	}

	archive_read_free ( arc );
	free ( path );
	path = NULL;

	if ( buf == NULL )
	{
		return NULL;			/* failed extraction, return error */
	}

	switch ( *pImageType ) {
	case FLOPPY_IMAGE_TYPE_IPF:
#ifndef HAVE_CAPSIMAGE
		Log_AlertDlg ( LOG_ERROR, "This version of Hatari was not built with IPF support, this disk image can't be handled." );
		return NULL;
#else
		/* return buffer */
		pDiskBuffer = buf;
		break;
#endif
	case FLOPPY_IMAGE_TYPE_STX:
		/* return buffer */
		pDiskBuffer = buf;
		break;
	case FLOPPY_IMAGE_TYPE_SCP:
		/* return buffer */
		pDiskBuffer = buf;
		break;
	case FLOPPY_IMAGE_TYPE_MSA:
		/* uncompress the MSA file */
		pDiskBuffer = MSA_UnCompress ( buf, (long *)&ImageSize, ImageSize );
		free ( buf );
		buf = NULL;
		break;
	case FLOPPY_IMAGE_TYPE_DIM:
		/* Skip DIM header */
		ImageSize -= 32;
		memmove ( buf, buf+32, ImageSize );
		/* return buffer */
		pDiskBuffer = buf;
		break;
	case FLOPPY_IMAGE_TYPE_ST:
		/* ST image => return buffer directly */
		pDiskBuffer = buf;
		break;
	}

	if (pDiskBuffer)
	{
		*pImageSize = ImageSize;
	}
	return pDiskBuffer;
}


/*-----------------------------------------------------------------------*/
/**
 * Load the first file from the archive FileName into memory, and return the number
 * of bytes loaded.
 * The first file must match one of the extensions defined in Exts[]
 */
uint8_t		*Archive_ReadFirstFile ( const char *FileName, long *pImageSize, const char * const Exts[] )
{
	struct archive	*arc;
	struct archive_entry *arc_entry;
	int		r;
	uint8_t		*pBuffer = NULL;
	char		*ArchivePath;
	int		uncompressed_size;


DEBUGPRINT (( stderr , "Archive_ReadFirstFile new filename=%s\n", FileName ));
	*pImageSize = 0;

	/* Locate the first file in the archive */
	ArchivePath = Archive_FirstFile ( FileName, Exts );
	if ( ArchivePath == NULL )
	{
		Log_Printf(LOG_ERROR, "Failed to locate first file in '%s'\n", FileName);
		return NULL;
	}

	arc = archive_read_new();
	archive_read_support_filter_all ( arc );
	archive_read_support_format_all ( arc );

	r = archive_read_open_filename ( arc, FileName, ARCHIVE_READ_BLOCK );
	if ( r != ARCHIVE_OK )
	{
		Log_Printf(LOG_ERROR, "Archive_ReadFirstFile: Cannot open %s\n", FileName);
		return NULL;
	}


	if ( !Arc_LocateFile ( arc , &arc_entry , ArchivePath ) )
	{
		Log_Printf(LOG_ERROR, "Can not locate '%s' in the archive!\n", ArchivePath);
		goto cleanup;
	}
	uncompressed_size = archive_entry_size ( arc_entry );

	/* Extract the current archive entry set by Archive_CheckImageFile */
	pBuffer = Archive_ExtractFile ( arc, uncompressed_size );
	if ( pBuffer )
		*pImageSize = uncompressed_size;

cleanup:
	archive_read_free ( arc );
	free(ArchivePath);

	return pBuffer;
}


#else		/* ! HAVE_LIBARCHIVE */

/* Define some empty functions to compile "hmsa" in case libarchive is not found */

bool		Archive_FileNameIsSupported ( const char *FileName )
{
	return false;
}
uint8_t 	*Archive_ReadDisk ( int Drive, const char *FileName, const char *ArchivePath, long *pImageSize, int *pImageType )
{
	return NULL;
}
struct dirent	**Archive_GetFilesDir ( const archive_dir *pArcDir, const char *dir, int *pEntries )
{
	return NULL;
}
archive_dir	*Archive_GetFiles(const char *pszFileName)
{
	return NULL;
}
void		Archive_FreeArcDir ( archive_dir *pArcDir )
{
}

#endif		/* HAVE_LIBARCHIVE */




/**
 * Save archive file from memory buffer. Returns true if all is OK.
 *
 * Not yet implemented.
 */
bool	Archive_WriteDisk ( int Drive, const char *FileName, unsigned char *pBuffer, int ImageSize)
{
	return false;
}
