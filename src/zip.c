/*
  Hatari - zip.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Zipped disc support, uses zlib
*/
char ZIP_rcsid[] = "Hatari $Id: zip.c,v 1.11 2005-04-05 14:41:32 thothy Exp $";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

#include <zlib.h>

#include "main.h"
#include "dim.h"
#include "file.h"
#include "floppy.h"
#include "log.h"
#include "msa.h"
#include "st.h"
#include "unzip.h"
#include "zip.h"

/* #define SAVE_TO_ZIP_IMAGES */

#define ZIP_PATH_MAX  256

#define ZIP_FILE_ST   1
#define ZIP_FILE_MSA  2
#define ZIP_FILE_DIM  3


/*-----------------------------------------------------------------------*/
/*
  Does filename end with a .ZIP extension? If so, return TRUE
*/
BOOL ZIP_FileNameIsZIP(char *pszFileName)
{
  return(File_DoesFileExtensionMatch(pszFileName,".zip"));
}


/*-----------------------------------------------------------------------*/
/*
  Check if a file name contains a slash or backslash and return its position.
*/
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
  unsigned int i;
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
      return NULL;
    }

  err = unzGetGlobalInfo (uf,&gi);
  if (err!=UNZ_OK){
    printf("error %d with zipfile in unzGetGlobalInfo \n",err);
    return NULL;
  }
  
  /* allocate a file list */
  filelist = (char **)malloc(gi.number_entry*sizeof(char *));
  if (!filelist)
  {
    perror("ZIP_GetFiles");
    return NULL;
  }

  nfiles = gi.number_entry;  /* set the number of files */
  
  for(i=0;i<gi.number_entry;i++)
    {
      err = unzGetCurrentFileInfo(uf,&file_info,filename_inzip, ZIP_PATH_MAX,NULL,0,NULL,0);
      if (err!=UNZ_OK)
      {
        free(filelist);
        return NULL;
      }

      filelist[i] = (char *)malloc(strlen(filename_inzip) + 1);
      if (!filelist[i])
      {
        perror("ZIP_GetFiles");
        free(filelist);
        return NULL;
      }

      strcpy(filelist[i], filename_inzip);
      if ((i+1)<gi.number_entry)
      {
        err = unzGoToNextFile(uf);
        if (err!=UNZ_OK)
        {
          Log_Printf(LOG_ERROR, "ZIP_GetFiles: Error in ZIP-file\n");
          /* deallocate memory */
          for(;i>0;i--)  free(filelist[i]);
          free(filelist);
          return NULL;
        }
      }
    }

  unzClose(uf);

  zd = (zip_dir *)malloc(sizeof(zip_dir));
  if (!zd)
  {
    perror("ZIP_GetFiles");
    free(filelist);
    return NULL;
  }
  zd->names = filelist;
  zd->nfiles = nfiles;

  return zd;
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
    free(f_zd->names[f_zd->nfiles]);
    f_zd->names[f_zd->nfiles] = NULL;
  }
  free(f_zd->names);
  f_zd->names = NULL;
  free(f_zd);
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
  files->nfiles = 1;
  temp = (char *)malloc(4);
  if (!temp)
    return NULL;
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
			  files->names[files->nfiles] = (char *)malloc(slash+1);
			  if (!files->names[files->nfiles])
			  {
			    perror("ZIP_GetFilesDir");
			    return NULL;
			  }
			  strncpy(files->names[files->nfiles], temp, slash+1);
			  ((char *)files->names[files->nfiles])[slash+1] = '\0';
			  files->nfiles++;
			}
		    } 
		  else 
		    {
		      /* add a filename */
		      files->names[files->nfiles] = (char *)malloc(strlen(temp)+1);
		      if (!files->names[files->nfiles])
		      {
		        perror("ZIP_GetFilesDir");
		        return NULL;
		      }
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
  fentries = (struct dirent **)malloc(sizeof(struct dirent *)*files->nfiles);
  if (!fentries)
  {
    perror("ZIP_GetFilesDir");
    ZIP_FreeZipDir(files);
    return NULL;
  }
  for(i=0; i<files->nfiles; i++)
    {
      fentries[i] = (struct dirent *)malloc(sizeof(struct dirent));
      if (!fentries[i])
      {
        perror("ZIP_GetFilesDir");
        return NULL;
      }
      strcpy(fentries[i]->d_name, files->names[i]);
    }

  ZIP_FreeZipDir(files);

  return(fentries);
}


/*-----------------------------------------------------------------------*/
/*
  Check an image file in the archive, return the uncompressed length
*/
static long ZIP_CheckImageFile(unzFile uf, char *filename, int *pDiscType)
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
  if(MSA_FileNameIsMSA(filename, FALSE))
    {
      *pDiscType = ZIP_FILE_MSA;
      return( file_info.uncompressed_size );
    }

  if(ST_FileNameIsST(filename, FALSE))
    {
      *pDiscType = ZIP_FILE_ST;
      return( file_info.uncompressed_size );
    }
  
  if (DIM_FileNameIsDIM(filename, FALSE))
    {
      *pDiscType = ZIP_FILE_DIM;
      return( file_info.uncompressed_size );
    }

  fprintf(stderr, "Not an .ST, .MSA or .DIM file.\n");
  return(0);
}

/*-----------------------------------------------------------------------*/
/*
  Return the first .st, .msa or .dim file in a zip, or NULL on failure
*/
static char *ZIP_FirstFile(char *filename)
{
  zip_dir *files;
  int i;
  char *name;

  if ((files = ZIP_GetFiles(filename)) == NULL)
    return(NULL);

  name = malloc(ZIP_PATH_MAX);
  if (!name)
  {
    perror("ZIP_FirstFile");
    return NULL;
  }

  name[0] = '\0';
  for(i=files->nfiles-1;i>=0;i--)
  {
    if (MSA_FileNameIsMSA(files->names[i], FALSE)
        || ST_FileNameIsST(files->names[i], FALSE)
        || DIM_FileNameIsDIM(files->names[i], FALSE))
      strncpy(name, files->names[i], ZIP_PATH_MAX);
  }
 
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
      Log_Printf(LOG_ERROR, "ZIP_ExtractFile: could not find file in archive\n");
      return NULL;
    }
  
  err = unzGetCurrentFileInfo(uf,&file_info,filename_inzip,sizeof(filename_inzip),NULL,0,NULL,0);
  
  if (err!=UNZ_OK)
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
  if (err!=UNZ_OK)
    {
      Log_Printf(LOG_ERROR, "ZIP_ExtractFile: could not open file\n");
      return(NULL);
    }
  
  do
    {
      err = unzReadCurrentFile(uf,buf,size_buf);
      if (err<0)	
	{
	  Log_Printf(LOG_ERROR, "ZIP_ExtractFile: could not read file\n");
	  return(NULL);
	}
    } while (err>0);
  
  return buf;
}

/*-----------------------------------------------------------------------*/
/*
  Load .ZIP file into memory, return number of bytes loaded

*/
Uint8 *ZIP_ReadDisc(char *pszFileName, char *pszZipPath, long *pImageSize)
{
  uLong ImageSize=0;
  unzFile uf=NULL;
  Uint8 *buf;
  int nDiscType;
  BOOL pathAllocated=FALSE;
  Uint8 *pDiscBuffer = NULL;

  *pImageSize = 0;

  uf = unzOpen(pszFileName);
  if (uf==NULL)
    {
      printf("Cannot open %s\n", pszFileName);
      return NULL;
    }
  
  if (pszZipPath == NULL || pszZipPath[0] == 0)
    {
      if((pszZipPath = ZIP_FirstFile(pszFileName)) == NULL)
	{
	  printf("Cannot open %s\n", pszFileName);
	  unzClose(uf);
	  return NULL;
	}
      pathAllocated=TRUE;
    }

  if((ImageSize = ZIP_CheckImageFile(uf, pszZipPath, &nDiscType)) <= 0)
    {
      unzClose(uf);
      return NULL;
    }

  /* extract to buf */
  buf=ZIP_ExtractFile(uf, pszZipPath, ImageSize);
  unzCloseCurrentFile(uf);
  unzClose(uf);
  if(buf == NULL)
    {
      return NULL;  /* failed extraction, return error */
    }

  if (nDiscType == ZIP_FILE_ST)
    {
      /* ST image => return buffer directly */
      pDiscBuffer = buf;
    }
  else if (nDiscType == ZIP_FILE_MSA)
    {
      /* uncompress the MSA file */
      pDiscBuffer = MSA_UnCompress(buf, &ImageSize);
    }
  else if (nDiscType == ZIP_FILE_DIM)
    {
      /* Skip DIM header */
      ImageSize -= 32;
      pDiscBuffer = malloc(ImageSize);
      if (pDiscBuffer)
        memcpy(pDiscBuffer, buf+32, ImageSize);
      else
        perror("ZIP_ReadDisc");
    }

  /* Free the buffers */
  if (pDiscBuffer != buf)
    free(buf);
  if(pathAllocated == TRUE)
    free(pszZipPath);

  if (pDiscBuffer != NULL)
    *pImageSize = ImageSize;

  return(pDiscBuffer);
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

