/*
  Hatari - zip.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Zipped disc support, uses zlib
*/
char ZIP_rcsid[] = "Hatari $Id: zip.c,v 1.7 2004-04-19 08:53:48 thothy Exp $";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

#include "zlib.h"
#include "main.h"
#include "msa.h"
#include "floppy.h"
#include "file.h"
#include "errlog.h"
#include "unzip.h"
#include "zip.h"
#include "memAlloc.h"

#define SAVE_TO_GZIP_IMAGES
/* #define SAVE_TO_ZIP_IMAGES */

#define ZIP_PATH_MAX  256

#define ZIP_FILE_ST   1
#define ZIP_FILE_MSA  2


static int Zip_FileNameHasSlash(char *fn)
{
  int i=0;
  while( fn[i] != '\0' )
    {
      if( fn[i] == '\\' || fn[i] == '/') return( i );
      i++;
    }  
  return( -1 );
}


/*-----------------------------------------------------------------------*/
/*
    Returns a list of files from a zip file. returns NULL on failure,
    returns a pointer to an array of strings if successful. Sets nfiles
    to the number of files.
*/
zip_dir *ZIP_GetFiles(char *pszFileName)
{
  int nfiles;
  int i;
  unz_global_info gi;
  int err;
  unzFile uf;
  char **filelist;
  unz_file_info file_info;
  char filename_inzip[ZIP_PATH_MAX];
  zip_dir *zd; 

  uf = unzOpen(pszFileName);
  if (uf==NULL)
    {
      printf("Cannot open %s\n", pszFileName);
      return(0);
    }

  err = unzGetGlobalInfo (uf,&gi);
  if (err!=UNZ_OK){
    printf("error %d with zipfile in unzGetGlobalInfo \n",err);
    return(NULL);
  }
  
  /* allocate a file list */
  filelist = (char **)Memory_Alloc(gi.number_entry*sizeof(char *));
  nfiles = gi.number_entry;  /* set the number of files */
  
  for(i=0;i<gi.number_entry;i++)
    {
      err = unzGetCurrentFileInfo(uf,&file_info,filename_inzip, ZIP_PATH_MAX,NULL,0,NULL,0);
      if (err!=UNZ_OK)
	{
	  Memory_Free(filelist);
	  return(NULL);
	}
      filelist[i] = (char *)Memory_Alloc(strlen(filename_inzip) + 1);
      strcpy(filelist[i], filename_inzip);
      if ((i+1)<gi.number_entry)
	{
	  err = unzGoToNextFile(uf);
	  if (err!=UNZ_OK)
	    {
	      ErrLog_File("ERROR ZIP_GetFiles with zipfile\n");
	      /* deallocate memory */
	      for(;i>0;i--)Memory_Free(filelist[i]);
	      Memory_Free(filelist);
	      return(NULL);
	    }
	}
    }

  unzClose(uf);

  zd = (zip_dir *)Memory_Alloc(sizeof(zip_dir));
  zd->names = filelist;
  zd->nfiles = nfiles;
  return( zd );
}


/*-----------------------------------------------------------------------*/
/*
  Free the memory that has been allocated for a zip_dir.
*/
void ZIP_FreeZipDir(zip_dir *f_zd)
{
  while (f_zd->nfiles > 0)
  {
    f_zd->nfiles--;
    Memory_Free(f_zd->names[f_zd->nfiles]);
    f_zd->names[f_zd->nfiles] = NULL;
  }
  Memory_Free(f_zd->names);
  f_zd->names = NULL;
  Memory_Free(f_zd);
}


/*-----------------------------------------------------------------------*/
/*
    Returns a list of files from the directory (dir) in a zip file list (zip)
    sets entries to the number of entries and returns a dirent structure, or
    NULL on failure. NOTE: only f_name is set in the dirent structures. 
*/
struct dirent **ZIP_GetFilesDir(zip_dir *zip, char *dir, int *entries)
{
  int i,j;
  zip_dir *files; 
  char *temp;
  BOOL flag;
  int slash;
  struct dirent **fentries;

  files = (zip_dir *)Memory_Alloc(sizeof(zip_dir));
  files->names = (char **)Memory_Alloc((zip->nfiles + 1) * sizeof(char *));

  /* add ".." directory */
  files->nfiles = 1;
  temp = (char *)Memory_Alloc(4);
  temp[0] = temp[1] = '.'; 
  temp[2] = '/'; temp[3] = '\0';
  files->names[0] = temp;
  
  for(i=0;i<zip->nfiles;i++)
    {
      if(strlen(zip->names[i]) > strlen(dir))
	{
	  if(strncasecmp(zip->names[i], dir, strlen(dir)) == 0)
	    {
	      temp = zip->names[i];
	      temp = (char *)(temp + strlen(dir));
	      if( temp[0] != '\0')
		{
		  if( (slash=Zip_FileNameHasSlash(temp)) > 0)
		    {
		      /* file is in a subdirectory, add this subdirectory if it doesn't exist in the list */
		      flag = FALSE;
		      for(j = files->nfiles-1;j>0;j--)
			  if(strncasecmp(temp, files->names[j], slash+1) == 0)
			    flag=TRUE;
		      if( flag == FALSE )
			{
			  files->names[files->nfiles] = (char *)Memory_Alloc(slash+1);
			  strncpy(files->names[files->nfiles], temp, slash+1);
			  ((char *)files->names[files->nfiles])[slash+1] = '\0';
			  files->nfiles++;
			}
		    } 
		  else 
		    {
		      /* add a filename */
		      files->names[files->nfiles] = (char *)Memory_Alloc(strlen(temp)+1);
		      strncpy(files->names[files->nfiles], temp, strlen(temp));
		      ((char *)files->names[files->nfiles])[strlen(temp)] = '\0';
		      files->nfiles++;
		    }
		}
	    }
	}
    }

  /* copy to a dirent structure */
  *entries = files->nfiles;
  fentries = (struct dirent **)Memory_Alloc(sizeof(struct dirent *)*files->nfiles);
  for(i=0; i<files->nfiles; i++)
    {
      fentries[i] = (struct dirent *)Memory_Alloc(sizeof(struct dirent));
      strcpy(fentries[i]->d_name, files->names[i]);
    }
  ZIP_FreeZipDir(files);
  return(fentries);
}

/*-----------------------------------------------------------------------*/
/*
  Check an image file in the archive, return the uncompressed length
*/
static long ZIP_CheckImageFile(unzFile uf, char *filename, int *ST_or_MSA)
{
  unz_file_info file_info;

  if (unzLocateFile(uf,filename, 0)!=UNZ_OK)
    {
      fprintf(stderr, "Error: File \"%s\"not found in the archive!", filename);
      return(-1);
    }

  if( unzGetCurrentFileInfo(uf,&file_info,filename, ZIP_PATH_MAX,NULL,0,NULL,0) != UNZ_OK)
    {
      fprintf(stderr, "Error with zipfile in unzGetCurrentFileInfo \n");
      return(-1);
    }
  
  /* check for a .msa or .st extention */
  if(File_FileNameIsMSA(filename))
    {
      *ST_or_MSA = ZIP_FILE_MSA;
      return( file_info.uncompressed_size );
    }

  if(File_FileNameIsST(filename))
    {
      *ST_or_MSA = ZIP_FILE_ST;
      return( file_info.uncompressed_size );
    }
  
  fprintf(stderr, "Not an .ST or .MSA file.\n");
  return(0);
}

/*-----------------------------------------------------------------------*/
/*
  Return the first .zip or .msa file in a zip, or NULL on failure
*/
static char *ZIP_FirstFile(char *filename)
{
  zip_dir *files;
  int i;
  char *name;

  if((files = ZIP_GetFiles(filename)) == NULL) return(NULL);
  name = Memory_Alloc(ZIP_PATH_MAX);

  name[0] = '\0';
  for(i=files->nfiles-1;i>=0;i--)
    if(File_FileNameIsMSA(files->names[i]) || 
       File_FileNameIsST(files->names[i]))
      strncpy(name, files->names[i], ZIP_PATH_MAX);
    
  /* free the files */
  ZIP_FreeZipDir(files);

  if(name[0] == '\0')
    return(NULL);
  return(name);
}


/*-----------------------------------------------------------------------*/
/*
  Extract a file (filename) from a ZIP-file (uf), the number of 
  bytes to uncompress is size. Returns a pointer to a buffer containing
  the uncompressed data, or NULL.
*/
static char *ZIP_ExtractFile(unzFile uf, char *filename, uLong size)
{
  int err = UNZ_OK;
  char filename_inzip[ZIP_PATH_MAX];
  void* buf;
  uInt size_buf;
  unz_file_info file_info;


  if (unzLocateFile(uf,filename, 0)!=UNZ_OK)
    {
      fprintf(stderr, "ERROR ZIP_ExtractFile could not find file in archive\n");
      ErrLog_File("ERROR ZIP_ExtractFile could not find file in archive\n");
      return NULL;
    }
  
  err = unzGetCurrentFileInfo(uf,&file_info,filename_inzip,sizeof(filename_inzip),NULL,0,NULL,0);
  
  if (err!=UNZ_OK)
    {
      fprintf(stderr, "ERROR ZIP_ExtractFile could not find file in archive\n");
      ErrLog_File("ERROR ZIP_ExtractFile could not get file info\n");
      return NULL;
    }
  
  size_buf = size;
  buf = Memory_Alloc(size_buf);
  
  err = unzOpenCurrentFile(uf);
  if (err!=UNZ_OK)
    {
      fprintf(stderr, "ERROR ZIP_ExtractFile could not find file in archive\n");
      ErrLog_File("ERROR ZIP_ExtractFile could not open file\n");
      return(NULL);
    }
  
  do
    {
      err = unzReadCurrentFile(uf,buf,size_buf);
      if (err<0)	
	{
	  fprintf(stderr, "ERROR ZIP_ExtractFile could not find file in archive\n");
	  ErrLog_File("ERROR ZIP_ExtractFile could not read file\n");
	  return(NULL);
	}
    } while (err>0);
  
  return buf;
}

/*-----------------------------------------------------------------------*/
/*
  Load .ZIP file into memory, return number of bytes loaded

*/
int ZIP_ReadDisc(char *pszFileName, char *pszZipPath, unsigned char *pBuffer)
{
  uLong ImageSize=0;
  unzFile uf=NULL;
  char *buf;
  int ST_or_MSA;
  BOOL pathAllocated=FALSE;

  uf = unzOpen(pszFileName);
  if (uf==NULL)
    {
      printf("Cannot open %s\n", pszFileName);
      return(0);
    }
  
  if (pszZipPath == NULL || pszZipPath[0] == 0)
    {
      if((pszZipPath = ZIP_FirstFile(pszFileName)) == NULL)
	{
	  printf("Cannot open %s\n", pszFileName);
	  unzClose(uf);
	  return(0);
	}
      pathAllocated=TRUE;
    }

  if((ImageSize = ZIP_CheckImageFile(uf, pszZipPath, &ST_or_MSA)) <= 0)
    {
      unzClose(uf);
      return(0);
    }

  if(ImageSize > DRIVE_BUFFER_BYTES)
    {
      ErrLog_File("ERROR ZIP_ReadDisc uncompressed .msa or .st file is larger than buffer\n");
      unzClose(uf);
      return(0);
    }

  /* extract to buf */
  buf=ZIP_ExtractFile(uf, pszZipPath, ImageSize);
  unzCloseCurrentFile(uf);
  unzClose(uf);
  if(buf == NULL)
    {
      return(0);  /* failed extraction, return error */
    }

  if(ST_or_MSA == ZIP_FILE_ST)
    {
      /* copy the ST image */
      memcpy(pBuffer, buf, (size_t)ImageSize);
    } else {
      /* uncompress the MSA file */
      ImageSize = MSA_UnCompress(buf, pBuffer);      
    }

  /* Free the buffer */
  Memory_Free(buf);
  if(pathAllocated == TRUE)
    Memory_Free(pszZipPath);

  return(ImageSize);
}


/*-----------------------------------------------------------------------*/
/*
  Save .ZIP file from memory buffer. Returns TRUE is all OK
  
  Not yet implemented.
*/
BOOL ZIP_WriteDisc(char *pszFileName,unsigned char *pBuffer,int ImageSize)
{
  return(FALSE);
}

/*-----------------------------------------------------------------------*/
/*
  Load .GZ file into memory, return number of bytes loaded

*/
int GZIP_ReadDisc(char *pszFileName,unsigned char *pBuffer)
{
  /* allocate buffer to store uncompressed bytes */
  long ImageSize=0;
  char *buf;
  gzFile in;
  int err=0;
  
  if((in = gzopen(pszFileName, "rb")) == NULL)
    {
      fprintf(stderr, "Error: could not open %s\n", pszFileName);
      return(0);
    }
  
  buf = (char *)Memory_Alloc(DRIVE_BUFFER_BYTES);
  do {
    ImageSize += err;
    err = gzread(in, (char *)(buf + ImageSize), 256);
  } while(err > 0 && ImageSize < DRIVE_BUFFER_BYTES);
  
  if(err < 0 || ImageSize >= DRIVE_BUFFER_BYTES)
    {
      fprintf(stderr, "Error: could not decompress %s\n", pszFileName);
      return(0);
    }

  gzclose(in);

  /* is it a gzipped .ST or .MSA file? */
  if(File_FileNameIsSTGZ(pszFileName))
    {
      /* copy the ST image */
      memcpy(pBuffer, buf, (size_t)ImageSize);
    } else {
      /* uncompress the MSA file */
      ImageSize = MSA_UnCompress(buf, pBuffer);      
    }

  Memory_Free(buf);
  return(ImageSize);
}

/*-----------------------------------------------------------------------*/
/*
  Save .GZ file from memory buffer. Returns TRUE is all OK
  
  Not yet implemented.
*/
BOOL GZIP_WriteDisc(char *pszFileName,unsigned char *pBuffer,int ImageSize)
{
#ifdef SAVE_TO_GZIP_IMAGES
  gzFile out;

  /* is it a gzipped .ST or .MSA file? */
  if(File_FileNameIsSTGZ(pszFileName))
    {
      if((out = gzopen(pszFileName, "wb6 ")) == NULL)
	{
	  fprintf(stderr, "Could not write to %s\n", pszFileName);
	  return(FALSE);
	}

      if(gzwrite(out, pBuffer, ImageSize) != ImageSize)
	{
	  fprintf(stderr, "Could not write to %s\n", pszFileName);
	  return(FALSE);
	}
      fprintf(stderr,"wrote %s\n", pszFileName);
      gzclose(out);
    } else {
    }

  return(TRUE);

#else
  return(FALSE);
#endif
}

