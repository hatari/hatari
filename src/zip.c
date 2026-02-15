/*
  Hatari - zip.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Zipped disk support, uses zlib
*/
const char ZIP_fileid[] = "Hatari zip.c";

#include "main.h"
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#if HAVE_ZLIB_H
#include <zlib.h>
#endif
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
#include "log.h"
#include "msa.h"
#include "st.h"
#include "str.h"
#include "unzip.h"
#include "zip.h"

#ifdef QNX
#include <sys/dir.h>
#define dirent direct
#endif

/* #define SAVE_TO_ZIP_IMAGES */

#define ZIP_PATH_MAX  256

#if HAVE_LIBZ

/* Possible disk image extensions to scan for */
static const char * const pszDiskNameExts[] =
{
	".msa",
	".st",
	".dim",
	".ipf",
	".raw",
	".ctr",
	".stx",
	NULL
};



#if HAVE_LIBARCHIVE
/* Possible file extensions to handle with libarchive */
static const char * const ArchiveExts[] =
{
	".zip",
	".7z",
	".rar",
	".lha",
	".lzh",
	".tar.gz",
	".tgz",
	".tar.bz2",
	".tbz",
	NULL
};
#endif


//#define DEBUGPRINT(x) fprintf x
#define DEBUGPRINT(x)



/*-----------------------------------------------------------------------*/
/**
 * Check if a file name contains a slash or backslash and return its position.
 */
static int Zip_FileNameHasSlash(const char *fn)
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
 * Free the memory that has been allocated for a zip_dir.
 */
void ZIP_FreeZipDir(zip_dir *f_zd)
{
DEBUGPRINT (( stderr , "ZIP_FreeZipDir %d\n" , f_zd->nfiles ));
	while (f_zd->nfiles > 0)
	{
		f_zd->nfiles--;
DEBUGPRINT (( stderr , "ZIP_FreeZipDir %d %p %s\n" , f_zd->nfiles , f_zd->names[f_zd->nfiles] , f_zd->names[f_zd->nfiles] ));
		free(f_zd->names[f_zd->nfiles]);
		f_zd->names[f_zd->nfiles] = NULL;
DEBUGPRINT (( stderr , "ZIP_FreeZipDir %d %p ok\n" , f_zd->nfiles , f_zd->names[f_zd->nfiles] ));
	}
	free(f_zd->names);
	f_zd->names = NULL;
	free(f_zd);
}


/*-----------------------------------------------------------------------*/
/**
 * Free the memory that has been allocated for fentries.
 */
static void ZIP_FreeFentries(struct dirent **fentries, int entries)
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
 *   Returns a list of files from the directory (dir) in a zip file list (zip)
 *   sets entries to the number of entries and returns a dirent structure, or
 *   NULL on failure. NOTE: only f_name is set in the dirent structures.
 */
struct dirent **ZIP_GetFilesDir(const zip_dir *zip, const char *dir, int *entries)
{
	int i,j;
	zip_dir *files;
	char *temp;
	bool flag;
	int slash;
	struct dirent **fentries;

	files = (zip_dir *)malloc(sizeof(zip_dir));
	if (!files)
	{
		perror("ZIP_GetFilesDir");
		return NULL;
	}

	files->names = (char **)malloc((zip->nfiles + 1) * sizeof(char *));
	if (!files->names)
	{
		perror("ZIP_GetFilesDir");
		free(files);
		return NULL;
	}

	/* add ".." directory */
	files->nfiles = 0;
	temp = (char *)malloc(4);
	if (!temp)
	{
		ZIP_FreeZipDir(files);
		return NULL;
	}
	temp[0] = temp[1] = '.';
	temp[2] = '/';
	temp[3] = '\0';
	files->names[0] = temp;
	files->nfiles++;

	for (i = 0; i < zip->nfiles; i++)
	{
		if (strlen(zip->names[i]) > strlen(dir))
		{
			if (strncasecmp(zip->names[i], dir, strlen(dir)) == 0)
			{
				temp = zip->names[i];
				temp = (char *)(temp + strlen(dir));
				if (temp[0] != '\0')
				{
					if ((slash=Zip_FileNameHasSlash(temp)) > 0)
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
								perror("ZIP_GetFilesDir");
								ZIP_FreeZipDir(files);
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
							perror("ZIP_GetFilesDir");
							ZIP_FreeZipDir(files);
							return NULL;
						}
						files->nfiles++;
					}
				}
			}
		}
	}

	/* copy to a dirent structure */
	*entries = files->nfiles;
	fentries = (struct dirent **)malloc(sizeof(struct dirent *)*files->nfiles);
	if (!fentries)
	{
		perror("ZIP_GetFilesDir");
		ZIP_FreeZipDir(files);
		return NULL;
	}
	for (i = 0; i < files->nfiles; i++)
	{
		fentries[i] = (struct dirent *)malloc(sizeof(struct dirent));
		if (!fentries[i])
		{
			perror("ZIP_GetFilesDir");
			ZIP_FreeFentries(fentries, i+1);
			ZIP_FreeZipDir(files);
			return NULL;
		}
		strncpy(fentries[i]->d_name, files->names[i], sizeof(fentries[i]->d_name)-1);
		fentries[i]->d_name[sizeof(fentries[i]->d_name) - 1] = 0;
	}

	ZIP_FreeZipDir(files);

	return fentries;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the first matching file in a zip, or NULL on failure.
 * String buffer size is ZIP_PATH_MAX
 */
static char *ZIP_FirstFile(const char *filename, const char * const ppsExts[])
{
	zip_dir *files;
	int i, j;
	char *name;

	files = ZIP_GetFiles(filename);
	if (files == NULL)
		return NULL;

	name = malloc(ZIP_PATH_MAX);
	if (!name)
	{
		perror("ZIP_FirstFile");
		ZIP_FreeZipDir(files);
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
				    && strlen(files->names[i]) < ZIP_PATH_MAX - 1)
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
		if (strlen(files->names[0]) < ZIP_PATH_MAX - 1)
		{
			strcpy(name, files->names[0]);
		}
	}

	/* free the files */
	ZIP_FreeZipDir(files);

	if (name[0] == '\0')
	{
		free(name);
		return NULL;
	}

	return name;
}



#if HAVE_LIBARCHIVE

/*-----------------------------------------------------------------------*/
/**
 * Does filename end with a supported archive extension ? If so, return true.
 */
bool ZIP_FileNameIsZIP ( const char *FileName )
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
zip_dir *ZIP_GetFiles ( const char *FileName )
{
	int		nfiles;
	char **		filelist = NULL;
	const char *	entry_pathname;
	zip_dir *	zd = NULL;
	struct archive	*arc;
	struct archive_entry *entry;
	int		r;

	arc = archive_read_new();
	archive_read_support_filter_all ( arc );
	archive_read_support_format_all ( arc );

	r = archive_read_open_filename ( arc, FileName, ARCHIVE_READ_BLOCK );
	if ( r != ARCHIVE_OK )
	{
		Log_Printf(LOG_ERROR, "ZIP_GetFiles: Cannot open %s\n", FileName);
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
			perror("ZIP_GetFiles malloc/realloc");
			goto cleanup;
		}

		entry_pathname = archive_entry_pathname ( entry );

		/* Alloc an extra char in case a trailing '/' must be added below for directories */
		filelist[ nfiles ] = (char *) malloc ( strlen ( entry_pathname ) + 2 );
		if ( !filelist[ nfiles ] )
		{
			perror("ZIP_GetFiles");
			goto cleanup;
		}

		strcpy ( filelist[ nfiles ], entry_pathname );

		/* Some archive format (eg RAR) don't add a trailing '/' to the directoty name */
		/* We add it if needed as this is required for our functions accessing files */
		if ( archive_entry_filetype(entry) == AE_IFDIR )
			File_AddSlashToEndFileName ( filelist[ nfiles ] );

DEBUGPRINT (( stderr, "zip new : nfiles=%d path=%s size=%ld type=0x%x\n" , nfiles , filelist[ nfiles ] ,archive_entry_size(entry),archive_entry_filetype(entry)  ));
		nfiles++;
	}

	zd = (zip_dir *) malloc ( sizeof(zip_dir) );
	if (zd)
	{
		zd->names = filelist;
		zd->nfiles = nfiles;
	}
	else
	{
		perror("ZIP_GetFiles");
	}

cleanup:
	r = archive_read_free(arc);
DEBUGPRINT (( stderr, "zip new : nfiles=%d filelist=%p\n" , nfiles , filelist ));

	if ( !zd && filelist )
	{
		/* deallocate memory */
		for ( ; nfiles > 0; nfiles-- )
			free ( filelist[ nfiles-1 ] );
		free ( filelist );
	}

	return zd;
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
static long ZIP_CheckImageFile ( struct archive *arc, char *FileName, int *pImageType )
{
	struct archive_entry	*arc_entry;
	int			uncompressed_size;


DEBUGPRINT (( stderr , "ZIP_CheckImageFile new file=%s\n", FileName ));

	if ( !Arc_LocateFile ( arc , &arc_entry , FileName ) )
	{
		Log_Printf(LOG_ERROR, "File \"%s\" not found in the archive!\n", FileName);
		return -1;
	}
	uncompressed_size = archive_entry_size ( arc_entry );

	*pImageType = FLOPPY_IMAGE_TYPE_NONE;

	/* check for .stx, .ipf, .msa, .dim or .st extension */
	if ( STX_FileNameIsSTX(FileName, false) )
		*pImageType = FLOPPY_IMAGE_TYPE_STX;

	else if ( IPF_FileNameIsIPF(FileName, false) )
		*pImageType = FLOPPY_IMAGE_TYPE_IPF;

	else if ( MSA_FileNameIsMSA(FileName, false) )
		*pImageType = FLOPPY_IMAGE_TYPE_MSA;

	else if ( ST_FileNameIsST(FileName, false) )
		*pImageType = FLOPPY_IMAGE_TYPE_ST;

	else if ( DIM_FileNameIsDIM(FileName, false) )
		*pImageType = FLOPPY_IMAGE_TYPE_DIM;

	/* Known extension found, return uncompressed size */
	if ( pImageType != FLOPPY_IMAGE_TYPE_NONE )
		return uncompressed_size;

	Log_Printf ( LOG_ERROR, "Not an .ST, .MSA, .DIM, .IPF or .STX file.\n" );
	return 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Extract a file from an archive
 * The extracted file is the one in the current archive_entry
 * Returns a pointer to a buffer containing the uncompressed data, or NULL.
 */
static void *ZIP_ExtractFile ( struct archive *arc, size_t size )
{
	uint8_t *	buf;
	size_t		size_buf;
	size_t		last_read;
	size_t		total_read;

	size_buf = size;
	buf = malloc ( size_buf );
	if ( !buf )
	{
		perror("ZIP_ExtractFile");
		return NULL;
	}


	/* Handle the case where archive_read_data returns less than size_buf bytes */
	total_read = 0;
	while ( ( last_read = archive_read_data ( arc, buf + total_read , size_buf - total_read ) ) > 0 )
		total_read += last_read;

	if ( total_read != size_buf )
	{
		Log_Printf ( LOG_ERROR, "ZIP_ExtractFile: could not read file\n" );
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
uint8_t *ZIP_ReadDisk ( int Drive, const char *FileName, const char *ArchivePath, long *pImageSize, int *pImageType )
{
	struct archive	*arc;
	int		r;
	uLong		ImageSize = 0;
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
		Log_Printf(LOG_ERROR, "ZIP_GetFiles: Cannot open %s\n", FileName);
		return NULL;
	}

	if ( ArchivePath == NULL || ArchivePath[0] == 0 )
	{
DEBUGPRINT (( stderr , "ZIP_ReadDisk new first filename=%s\n" , FileName ));
		path = ZIP_FirstFile ( FileName, pszDiskNameExts );
		if ( path == NULL )
		{
			Log_Printf ( LOG_ERROR, "Cannot open %s\n", FileName );
			archive_read_free ( arc );
			return NULL;
		}
	}
	else
	{
DEBUGPRINT (( stderr , "ZIP_ReadDisk news path=%s filename=%s\n" , ArchivePath , FileName ));
		path = malloc ( ZIP_PATH_MAX );
		if ( path == NULL )
		{
			perror("ZIP_ReadDisk");
			archive_read_free ( arc );
			return NULL;
		}
		strncpy ( path, ArchivePath, ZIP_PATH_MAX - 1 );
		path[ZIP_PATH_MAX-1] = '\0';
	}

	ImageSize = ZIP_CheckImageFile ( arc, path, pImageType );
	if ( ImageSize <= 0 )
	{
		archive_read_free ( arc );
		free ( path );
		return NULL;
	}

	/* Extract the current archive_entry set by ZIP_CheckImageFile */
	buf = ZIP_ExtractFile ( arc, ImageSize );
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
uint8_t *ZIP_ReadFirstFile ( const char *FileName, long *pImageSize, const char * const Exts[] )
{
	struct archive	*arc;
	struct archive_entry *arc_entry;
	int		r;
	uint8_t		*pBuffer = NULL;
	char		*ArchivePath;
	int		uncompressed_size;


DEBUGPRINT (( stderr , "ZIP_ReadFirstFile new filename=%s\n", FileName ));
	*pImageSize = 0;

	/* Locate the first file in the archive */
	ArchivePath = ZIP_FirstFile ( FileName, Exts );
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
		Log_Printf(LOG_ERROR, "ZIP_ReadFirstFile: Cannot open %s\n", FileName);
		return NULL;
	}


	if ( !Arc_LocateFile ( arc , &arc_entry , ArchivePath ) )
	{
		Log_Printf(LOG_ERROR, "Can not locate '%s' in the archive!\n", ArchivePath);
		goto cleanup;
	}
	uncompressed_size = archive_entry_size ( arc_entry );

	/* Extract the current archive entry set by ZIP_CheckImageFile */
	pBuffer = ZIP_ExtractFile ( arc, uncompressed_size );
	if ( pBuffer )
		*pImageSize = uncompressed_size;

cleanup:
	archive_read_free ( arc );
	free(ArchivePath);

	return pBuffer;
}



#else	// ! HAVE_LIBARCHIVE


/*-----------------------------------------------------------------------*/
/**
 * Does filename end with a .ZIP extension? If so, return true.
 */
bool ZIP_FileNameIsZIP(const char *pszFileName)
{
	return File_DoesFileExtensionMatch(pszFileName,".zip");
}


/*-----------------------------------------------------------------------*/
/**
 * Returns a list of files from a zip file. returns NULL on failure,
 * returns a pointer to an array of strings if successful. Sets nfiles
 * to the number of files.
 */
zip_dir *ZIP_GetFiles(const char *pszFileName)
{
	int nfiles;
	unsigned int i;
	unz_global_info gi;
	int err;
	unzFile uf;
	char **filelist;
	unz_file_info file_info;
	char filename_inzip[ZIP_PATH_MAX];
	zip_dir *zd = NULL;

	uf = unzOpen(pszFileName);
	if (uf == NULL)
	{
		Log_Printf(LOG_ERROR, "ZIP_GetFiles: Cannot open %s\n", pszFileName);
		return NULL;
	}

	err = unzGetGlobalInfo(uf,&gi);
	if (err != UNZ_OK)
	{
		Log_Printf(LOG_ERROR, "Error %d with zipfile in unzGetGlobalInfo \n",err);
		return NULL;
	}

	/* allocate a file list */
	filelist = (char **)malloc(gi.number_entry*sizeof(char *));
	if (!filelist)
	{
		perror("ZIP_GetFiles");
		unzClose(uf);
		return NULL;
	}

	nfiles = gi.number_entry;  /* set the number of files */

	for (i = 0; i < gi.number_entry; i++)
	{
		err = unzGetCurrentFileInfo(uf, &file_info, filename_inzip, ZIP_PATH_MAX, NULL, 0, NULL, 0);
		if (err != UNZ_OK)
		{
			Log_Printf(LOG_ERROR, "ZIP_GetFiles: Error in ZIP-file\n");
			goto cleanup;
		}

		filelist[i] = (char *)malloc(strlen(filename_inzip) + 1);
DEBUGPRINT (( stderr ,  "zip old : i=%d nfiles=%d %s\n" , i , nfiles, filename_inzip ));
		if (!filelist[i])
		{
			perror("ZIP_GetFiles");
			goto cleanup;
		}

		strcpy(filelist[i], filename_inzip);
		if ((i+1) < gi.number_entry)
		{
			err = unzGoToNextFile(uf);
			if (err != UNZ_OK)
			{
				Log_Printf(LOG_ERROR, "ZIP_GetFiles: Error in ZIP-file\n");
				goto cleanup;
			}
		}
	}

	zd = (zip_dir *)malloc(sizeof(zip_dir));
	if (zd)
	{
		zd->names = filelist;
		zd->nfiles = nfiles;
	}
	else
	{
		perror("ZIP_GetFiles");
	}

cleanup:
	unzClose(uf);
	if (!zd && filelist)
	{
		/* deallocate memory */
		for (; i > 0; i--)
			free(filelist[i]);
		free(filelist);
	}

	return zd;
}


static long ZIP_CheckImageFile(unzFile uf, char *filename, int namelen, int *pImageType)
{
	unz_file_info file_info;

DEBUGPRINT (( stderr , "ZIP_CheckImageFile file=%s namelen=%d\n", filename , namelen ));

	if (unzLocateFile(uf,filename, 0) != UNZ_OK)
	{
		Log_Printf(LOG_ERROR, "File \"%s\" not found in the archive!\n", filename);
		return -1;
	}

	if (unzGetCurrentFileInfo(uf, &file_info, filename, namelen, NULL, 0, NULL, 0) != UNZ_OK)
	{
		Log_Printf(LOG_ERROR, "Error with zipfile in unzGetCurrentFileInfo\n");
		return -1;
	}

	/* check for .stx, .ipf, .msa, .dim or .st extension */
	if (STX_FileNameIsSTX(filename, false))
	{
		*pImageType = FLOPPY_IMAGE_TYPE_STX;
		return file_info.uncompressed_size;
	}

	if (IPF_FileNameIsIPF(filename, false))
	{
		*pImageType = FLOPPY_IMAGE_TYPE_IPF;
		return file_info.uncompressed_size;
	}

	if (MSA_FileNameIsMSA(filename, false))
	{
		*pImageType = FLOPPY_IMAGE_TYPE_MSA;
		return file_info.uncompressed_size;
	}

	if (ST_FileNameIsST(filename, false))
	{
		*pImageType = FLOPPY_IMAGE_TYPE_ST;
		return file_info.uncompressed_size;
	}

	if (DIM_FileNameIsDIM(filename, false))
	{
		*pImageType = FLOPPY_IMAGE_TYPE_DIM;
		return file_info.uncompressed_size;
	}

	Log_Printf(LOG_ERROR, "Not an .ST, .MSA, .DIM, .IPF or .STX file.\n");
	return 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Extract a file (filename) from a ZIP-file (uf), the number of 
 * bytes to uncompress is size. Returns a pointer to a buffer containing
 * the uncompressed data, or NULL.
 */
static void *ZIP_ExtractFile(unzFile uf, const char *filename, uLong size)
{
	int err = UNZ_OK;
	char filename_inzip[ZIP_PATH_MAX];
	void* buf;
	uInt size_buf;
	unz_file_info file_info;


	if (unzLocateFile(uf,filename, 0) != UNZ_OK)
	{
		Log_Printf(LOG_ERROR, "ZIP_ExtractFile: could not find file in archive\n");
		return NULL;
	}

	err = unzGetCurrentFileInfo(uf,&file_info,filename_inzip,sizeof(filename_inzip),NULL,0,NULL,0);

	if (err != UNZ_OK)
	{
		Log_Printf(LOG_ERROR, "ZIP_ExtractFile: could not get file info\n");
		return NULL;
	}

	size_buf = size;
	buf = malloc(size_buf);
	if (!buf)
	{
		perror("ZIP_ExtractFile");
		return NULL;
	}

	err = unzOpenCurrentFile(uf);
	if (err != UNZ_OK)
	{
		Log_Printf(LOG_ERROR, "ZIP_ExtractFile: could not open file\n");
		free(buf);
		return NULL;
	}

	do
	{
		err = unzReadCurrentFile(uf,buf,size_buf);
		if (err < 0)
		{
			Log_Printf(LOG_ERROR, "ZIP_ExtractFile: could not read file\n");
			free(buf);
			return NULL;
		}
	}
	while (err > 0);

	return buf;
}


/*-----------------------------------------------------------------------*/
/**
 * Load disk image from a .ZIP archive into memory, set the number
 * of bytes loaded into pImageSize and return the data or NULL on error.
 */
uint8_t *ZIP_ReadDisk(int Drive, const char *pszFileName, const char *pszZipPath, long *pImageSize, int *pImageType)
{
	uLong ImageSize=0;
	unzFile uf=NULL;
	uint8_t *buf;
	char *path;
	uint8_t *pDiskBuffer = NULL;

	*pImageSize = 0;
	*pImageType = FLOPPY_IMAGE_TYPE_NONE;

	uf = unzOpen(pszFileName);
	if (uf == NULL)
	{
		Log_Printf(LOG_ERROR, "Cannot open %s\n", pszFileName);
		return NULL;
	}

	if (pszZipPath == NULL || pszZipPath[0] == 0)
	{
DEBUGPRINT (( stderr , "ZIP_ReadDisk first filename=%s\n" , pszFileName ));
		path = ZIP_FirstFile(pszFileName, pszDiskNameExts);
		if (path == NULL)
		{
			Log_Printf(LOG_ERROR, "Cannot open %s\n", pszFileName);
			unzClose(uf);
			return NULL;
		}
	}
	else
	{
DEBUGPRINT (( stderr , "ZIP_ReadDisk path=%s filename=%s\n" , pszZipPath , pszFileName ));
		path = malloc(ZIP_PATH_MAX);
		if (path == NULL)
		{
			perror("ZIP_ReadDisk");
			unzClose(uf);
			return NULL;
		}
		strncpy(path, pszZipPath, ZIP_PATH_MAX - 1);
		path[ZIP_PATH_MAX-1] = '\0';
	}

	ImageSize = ZIP_CheckImageFile(uf, path, ZIP_PATH_MAX, pImageType);
	if (ImageSize <= 0)
	{
		unzClose(uf);
		free(path);
		return NULL;
	}

	/* extract to buf */
	buf = ZIP_ExtractFile(uf, path, ImageSize);

	unzCloseCurrentFile(uf);
	unzClose(uf);
	free(path);
	path = NULL;

	if (buf == NULL)
	{
		return NULL;  /* failed extraction, return error */
	}

	switch(*pImageType) {
	case FLOPPY_IMAGE_TYPE_IPF:
#ifndef HAVE_CAPSIMAGE
		Log_AlertDlg(LOG_ERROR, "This version of Hatari was not built with IPF support, this disk image can't be handled.");
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
	case FLOPPY_IMAGE_TYPE_MSA:
		/* uncompress the MSA file */
		pDiskBuffer = MSA_UnCompress(buf, (long *)&ImageSize, ImageSize);
		free(buf);
		buf = NULL;
		break;
	case FLOPPY_IMAGE_TYPE_DIM:
		/* Skip DIM header */
		ImageSize -= 32;
		memmove(buf, buf+32, ImageSize);
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
 * Load first file from a .ZIP archive into memory, and return the number
 * of bytes loaded.
 */
uint8_t *ZIP_ReadFirstFile(const char *pszFileName, long *pImageSize, const char * const ppszExts[])
{
	unzFile uf = NULL;
	uint8_t *pBuffer = NULL;
	char *pszZipPath;
	unz_file_info file_info;

DEBUGPRINT (( stderr , "ZIP_ReadFirstFile old filename=%s\n", pszFileName ));
	*pImageSize = 0;

	/* Open the ZIP file */
	uf = unzOpen(pszFileName);
	if (uf == NULL)
	{
		Log_Printf(LOG_ERROR, "Cannot open '%s'\n", pszFileName);
		return NULL;
	}

	/* Locate the first file in the ZIP archive */
	pszZipPath = ZIP_FirstFile(pszFileName, ppszExts);
	if (pszZipPath == NULL)
	{
		Log_Printf(LOG_ERROR, "Failed to locate first file in '%s'\n", pszFileName);
		unzClose(uf);
		return NULL;
	}

	if (unzLocateFile(uf, pszZipPath, 0) != UNZ_OK)
	{
		Log_Printf(LOG_ERROR, "Can not locate '%s' in the archive!\n", pszZipPath);
		goto cleanup;
	}

	/* Get file information (file size!) */
	if (unzGetCurrentFileInfo(uf, &file_info, pszZipPath, ZIP_PATH_MAX, NULL, 0, NULL, 0) != UNZ_OK)
	{
		Log_Printf(LOG_ERROR, "Error with zipfile in unzGetCurrentFileInfo.\n");
		goto cleanup;
	}

	/* Extract to buffer */
	pBuffer = ZIP_ExtractFile(uf, pszZipPath, file_info.uncompressed_size);
	if (pBuffer)
		*pImageSize = file_info.uncompressed_size;

	/* And close the file */
	unzCloseCurrentFile(uf);
cleanup:
	unzClose(uf);
	free(pszZipPath);

	return pBuffer;
}

#endif		// ! HAVE_LIBARCHIVE

#else		// ! HAVE_LIBZ

bool ZIP_FileNameIsZIP(const char *pszFileName)
{
	return false;
}
uint8_t *ZIP_ReadDisk(int Drive, const char *name, const char *path, long *size , int *pImageType)
{
	return NULL;
}
struct dirent **ZIP_GetFilesDir(const zip_dir *zip, const char *dir, int *entries)
{
	return NULL;
}
zip_dir *ZIP_GetFiles(const char *pszFileName)
{
	return NULL;
}
void ZIP_FreeZipDir(zip_dir *f_zd)
{
}

#endif  /* HAVE_LIBZ */

/**
 * Save .ZIP file from memory buffer. Returns true if all is OK.
 *
 * Not yet implemented.
 */
bool ZIP_WriteDisk(int Drive, const char *pszFileName,unsigned char *pBuffer,int ImageSize)
{
	return false;
}
