/*
  Hatari

  ST Disc support
*/

#include "main.h"
#include "file.h"

#define SAVE_TO_ST_IMAGES

/*
    .ST FILE FORMAT
  --===============-------------------------------------------------------------

  The file format of the .ST image files used by PaCifiST is simplicity itself;
  they are just straight images of the disk in question, with sectors stored in
  the expected logical order. So, on a sector basis the images run from sector
  0 (bootsector) to however many sectors are on the disk. On a track basis the
  layout is the same as for MSA files but obviously the data is raw, no track
  header or compression or anything like that.

  TRACK 0, SIDE 0
  TRACK 0, SIDE 1
  TRACK 1, SIDE 0
  TRACK 1, SIDE 1
  TRACK 2, SIDE 0
  TRACK 2, SIDE 1
*/


/*-----------------------------------------------------------------------*/
/*
  Load .ST file into memory, return number of bytes loaded
*/
int ST_ReadDisc(char *pszFileName,unsigned char *pBuffer)
{
  void *pSTFile;
  long ImageSize=0;

  /* Just load directly into buffer, and set ImageSize accordingly (no need to free memory) */
  pSTFile = File_Read(pszFileName,pBuffer,&ImageSize,NULL);
  if (!pSTFile)
    ImageSize = 0;

  return(ImageSize);
}


/*-----------------------------------------------------------------------*/
/*
  Save .ST file from memory buffer. Returns TRUE is all OK
*/
BOOL ST_WriteDisc(char *pszFileName,unsigned char *pBuffer,int ImageSize)
{
#ifdef SAVE_TO_ST_IMAGES

  /* Just save buffer directly to file */
  return( File_Save(pszFileName, pBuffer, ImageSize, FALSE) );

#else   /*SAVE_TO_ST_IMAGES*/

  /* Oops, cannot save */
  return(FALSE);

#endif  /*SAVE_TO_ST_IMAGES*/
}
