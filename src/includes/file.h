/*
  Hatari
*/

/* File types */
enum {
  FILEFILTER_DISCFILES,
  FILEFILTER_ALLFILES,
  FILEFILTER_TOSROM,
  FILEFILTER_MAPFILE,
  FILEFILTER_YMFILE,
  FILEFILTER_MEMORYFILE,
};

#ifdef __BEOS__
extern int alphasort(const void *d1, const void *d2);
extern int scandir(const char *dirname,struct dirent ***namelist, int(*select) __P((struct dirent *)), int (*dcomp) __P((const void *, const void *)));
#endif  /* __BEOS__ */

extern void File_CleanFileName(char *pszFileName);
extern void File_AddSlashToEndFileName(char *pszFileName);
extern BOOL File_DoesFileExtensionMatch(char *pszFileName, char *pszExtension);
extern BOOL File_IsRootFileName(char *pszFileName);
extern char *File_RemoveFileNameDrive(char *pszFileName);
extern BOOL File_DoesFileNameEndWithSlash(char *pszFileName);
extern void File_RemoveFileNameTrailingSlashes(char *pszFileName);
extern BOOL File_FileNameIsMSA(char *pszFileName);
extern BOOL File_FileNameIsST(char *pszFileName);
extern void *File_Read(char *pszFileName, void *pAddress, long *pFileSize, char *ppszExts[]);
extern BOOL File_Save(char *pszFileName, void *pAddress,long Size,BOOL bQueryOverwrite);
extern int File_Length(char *pszFileName);
extern BOOL File_Exists(char *pszFileName);
extern BOOL File_Delete(char *pszFileName);
extern BOOL File_QueryOverwrite(char *pszFileName);
extern BOOL File_FindPossibleExtFileName(char *pszFileName,char *ppszExts[]);
extern void File_splitpath(char *pSrcFileName, char *pDir, char *pName, char *Ext);
extern void File_makepath(char *pDestFileName, char *pDir, char *pName, char *pExt);
extern void File_ShrinkName(char *pDestFileName, char *pSrcFileName, int maxlen);
