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

extern void File_Init(void);
extern BOOL File_OpenDlg(char *pFullFileName,int Drive);
/*extern BOOL File_OpenDlg_NoExtraButtons(HWND hWnd, char *pFullFileName);*/
/*extern BOOL File_OpenBrowseDlg(HWND hWnd, char *pFullFileName,BOOL bTosROM,BOOL bFileMustExist);*/
extern BOOL File_OpenSelectDlg(char *pFullFileName,int FileFilter,BOOL bFileMustExist,BOOL bSaving);
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
