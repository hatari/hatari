/*
  Hatari
*/
#include <dirent.h>

typedef struct {
  char **names;
  int nfiles;
} zip_dir;

extern struct dirent **ZIP_GetFilesDir(zip_dir *files, char *dir, int *entries);
extern zip_dir *ZIP_GetFiles(char *pszFileName);
extern int ZIP_ReadDisc(char *pszFileName, char *pszZipPath, unsigned char *pBuffer);
extern BOOL ZIP_WriteDisc(char *pszFileName,unsigned char *pBuffer,int ImageSize);
extern int GZIP_ReadDisc(char *pszFileName,unsigned char *pBuffer);
extern BOOL GZIP_WriteDisc(char *pszFileName,unsigned char *pBuffer,int ImageSize);



