/*
  Hatari

  common file access
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "main.h"
#include "dialog.h"
#include "file.h"
#include "floppy.h"
#include "createBlankImage.h"
#include "memAlloc.h"
#include "misc.h"


//OPENFILENAME ofn;
char szSTFilter[256],szROMFilter[256],szAllFilesFilter[256],szMapFileFilter[256],szYMFileFilter[256],szMemoryFileFilter[256];
char szCreateDiscFileName[MAX_FILENAME_LENGTH];
BOOL bEjectDisc,bCreateBlankDisc;

/*-----------------------------------------------------------------------*/
/*
  Initialize Windows 'Open File' dialogs
*/
void File_Init(void)
{
/* FIXME */
/*
  char chReplace;    // string separator for szFilter
  int i,cbString;

  // Load '*.ST' filter
  cbString = LoadString(hInst,IDS_STRING1,szSTFilter,sizeof(szSTFilter));
  chReplace = szSTFilter[cbString - 1]; // retrieve wildcard

  for(i=0; szSTFilter[i]!='\0'; i++) {
    if (szSTFilter[i]==chReplace)
       szSTFilter[i]='\0';
  }
  // Load '*.IMG' filter
  cbString = LoadString(hInst,IDS_STRING2,szROMFilter,sizeof(szROMFilter));
  chReplace = szROMFilter[cbString - 1]; // retrieve wildcard

  for(i=0; szROMFilter[i]!='\0'; i++) {
    if (szROMFilter[i]==chReplace)
       szROMFilter[i]='\0';
  }

  // Load '*.*' filter
  cbString = LoadString(hInst,IDS_STRING3,szAllFilesFilter,sizeof(szAllFilesFilter));
  chReplace = szAllFilesFilter[cbString - 1]; // retrieve wildcard

  for(i=0; szAllFilesFilter[i]!='\0'; i++) {
    if (szAllFilesFilter[i]==chReplace)
       szAllFilesFilter[i]='\0';
  }

  // Load '*.map' filter
  cbString = LoadString(hInst,IDS_STRING4,szMapFileFilter,sizeof(szMapFileFilter));
  chReplace = szMapFileFilter[cbString - 1]; // retrieve wildcard

  for(i=0; szMapFileFilter[i]!='\0'; i++) {
    if (szMapFileFilter[i]==chReplace)
       szMapFileFilter[i]='\0';
  }

  // Load '*.ym' filter
  cbString = LoadString(hInst,IDS_STRING5,szYMFileFilter,sizeof(szYMFileFilter));
  chReplace = szYMFileFilter[cbString - 1]; // retrieve wildcard

  for(i=0; szYMFileFilter[i]!='\0'; i++) {
    if (szYMFileFilter[i]==chReplace)
       szYMFileFilter[i]='\0';
  }

  // Load '*.mem' filter
  cbString = LoadString(hInst,IDS_STRING6,szMemoryFileFilter,sizeof(szMemoryFileFilter));
  chReplace = szMemoryFileFilter[cbString - 1]; // retrieve wildcard

  for(i=0; szMemoryFileFilter[i]!='\0'; i++) {
    if (szMemoryFileFilter[i]==chReplace)
       szMemoryFileFilter[i]='\0';
  }

  Memory_Clear(&ofn,sizeof(OPENFILENAME));
  ofn.lStructSize = sizeof(OPENFILENAME);
  ofn.hInstance = hInst;
  ofn.nMaxFile = _MAX_PATH;
  ofn.nMaxFileTitle = _MAX_FNAME + _MAX_EXT;
*/
}


//-----------------------------------------------------------------------
/*
  Create 'Open File' dialog, and ask user for valid filename
*/
BOOL File_OpenDlg(/*HWND hWnd,*/ char *pFullFileName,int Drive)
{
/* FIXME */
/*
  char szSrcDrive[_MAX_DRIVE],szSrcDir[_MAX_DIR],szSrcName[_MAX_FNAME],szSrcExt[_MAX_EXT];
  char szTempFileName[MAX_FILENAME_LENGTH];
  char szTempDir[MAX_FILENAME_LENGTH],szTitleString[64];
  BOOL bRet;

  ofn.hwndOwner = hWnd;
  ofn.lpstrFilter = szSTFilter;
  ofn.lpstrFileTitle = NULL;
  // Copy filename as dialog will change this(may not be valid if cancel)
  strcpy(szTempFileName,pFullFileName);
  // Create filename and directory of previous file
  _splitpath(szTempFileName,szSrcDrive,szSrcDir,szSrcName,szSrcExt);
  
  // Filename only, save FULL path and file back here when quit
  _makepath(szTempFileName,"","",szSrcName,szSrcExt);
  ofn.lpstrFile = szTempFileName;
  // Directory only
  _makepath(szTempDir,szSrcDrive,szSrcDir,"","");
  if (strlen(szTempDir)>0)
    ofn.lpstrInitialDir = szTempDir;
  else {
    File_AddSlashToEndFileName(ConfigureParams.DiscImage.szDiscImageDirectory);
    ofn.lpstrInitialDir = ConfigureParams.DiscImage.szDiscImageDirectory;
  }
  sprintf(szTitleString,"Select Disc Image for Drive '%c'",Drive+'A');
  ofn.lpstrTitle = szTitleString;
  ofn.Flags = OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_NOVALIDATE
   | OFN_ENABLETEMPLATE | OFN_EXPLORER | OFN_ENABLEHOOK;// | OFN_SHOWHELP;
  ofn.lpTemplateName = MAKEINTRESOURCE(IDD_DIALOGBAR);
  ofn.hInstance = hInst;
  ofn.lpfnHook = File_OpenDlg_OFNHookProc;

  // Set globals for dialog intercept
  bEjectDisc = bCreateBlankDisc = FALSE;
  // Bring up dialog
  bRet = GetOpenFileName(&ofn);
  if (bRet)
    strcpy(pFullFileName,szTempFileName);

  // Did we eject the disc?
  if (bEjectDisc) {
    // Did we have a disc inserted?
    _splitpath(pFullFileName,szSrcDrive,szSrcDir,szSrcName,szSrcExt);
    if ( (strlen(szSrcName)>0) || (strlen(szSrcExt)>0) ) {
      Floppy_EjectDiscFromDrive(Drive,TRUE);
    }
    else {
      MessageBox(hWnd,"There is no disc image selected for that drive - Drive is empty.",PROG_NAME,MB_OK | MB_ICONINFORMATION);
      Floppy_EjectDiscFromDrive(Drive,FALSE);
    }
    // Blank disc filename, and return disc change
    strcpy(pFullFileName,"");
    bRet = TRUE;
  }
  if (bCreateBlankDisc) {
    // Do dialog for disc create, filename is in 'szCreateDiscFileName'
    if (CreateBlankImage_DoDialog(hWnd,Drive,szCreateDiscFileName)) {
      // Copy filename, so auto-inserts into drive
      strcpy(pFullFileName,szCreateDiscFileName);
    }
  }

  return(bRet);
*/
return TRUE;
}

//-----------------------------------------------------------------------
/*
  Create 'Open File' dialog, don't have extra buttons via hook
*/
/*
BOOL File_OpenDlg_NoExtraButtons(HWND hWnd, char *pFullFileName)
{
  char szSrcDrive[_MAX_DRIVE],szSrcDir[_MAX_DIR],szSrcName[_MAX_FNAME],szSrcExt[_MAX_EXT];
  char szTempFileName[MAX_FILENAME_LENGTH];
  char szTempDir[MAX_FILENAME_LENGTH],szTitleString[64];
  BOOL bRet;

  ofn.hwndOwner = hWnd;
  ofn.lpstrFilter = szSTFilter;
  ofn.lpstrFileTitle = NULL;
  // Copy filename as dialog will change this(may not be valid if cancel)
  strcpy(szTempFileName,pFullFileName);
  // Create filename and directory of previous file
  _splitpath(szTempFileName,szSrcDrive,szSrcDir,szSrcName,szSrcExt);
  
  // Filename only, save FULL path and file back here when quit
  _makepath(szTempFileName,"","",szSrcName,szSrcExt);
  ofn.lpstrFile = szTempFileName;
  // Directory only
  _makepath(szTempDir,szSrcDrive,szSrcDir,"","");
  if (strlen(szTempDir)>0)
    ofn.lpstrInitialDir = szTempDir;
  else {
    File_AddSlashToEndFileName(ConfigureParams.DiscImage.szDiscImageDirectory);
    ofn.lpstrInitialDir = ConfigureParams.DiscImage.szDiscImageDirectory;
  }
  sprintf(szTitleString,"Select Disc Image");
  ofn.lpstrTitle = szTitleString;
  ofn.Flags = OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_NOVALIDATE | OFN_EXPLORER;
  ofn.lpTemplateName = NULL;
  ofn.hInstance = hInst;
  ofn.lpfnHook = File_OpenDlg_OFNHookProc;

  // Bring up dialog
  bRet = GetOpenFileName(&ofn);
  if (bRet)
    strcpy(pFullFileName,szTempFileName);

  return(bRet);
}
*/

//-----------------------------------------------------------------------
/*
  Create 'Browse' dialog, and ask user for valid directory
*/
/*
BOOL File_OpenBrowseDlg(HWND hWnd, char *pFullFileName,BOOL bTosROM,BOOL bFileMustExist)
{
  char szChosenPath[MAX_PATH];
  BROWSEINFO bInfo;
  LPITEMIDLIST idList;

  bInfo.hwndOwner = hWnd;
  bInfo.pidlRoot = NULL;
  bInfo.pszDisplayName = szChosenPath;
  bInfo.lpszTitle = "Select a directory:";
  bInfo.lpfn = NULL;
  bInfo.ulFlags = 0;
  bInfo.lParam = 0;
  bInfo.iImage = 0;

  idList = SHBrowseForFolder(&bInfo);
  if (idList != NULL) {
    if (SHGetPathFromIDList(idList, szChosenPath)) {
      strcpy(pFullFileName,szChosenPath);
      return(TRUE);
    }
  }

  return(FALSE);
}
*/

//-----------------------------------------------------------------------
/*
  Create 'Open File' dialog, and ask user for TOS image filename
*/
BOOL File_OpenSelectDlg(/*HWND hWnd,*/ char *pFullFileName,int FileFilter,BOOL bFileMustExist,BOOL bSaving)
{
/* FIXME */
/*
  char szSrcDrive[_MAX_DRIVE],szSrcDir[_MAX_DIR],szSrcName[_MAX_FNAME],szSrcExt[_MAX_EXT];
  char szTempDir[MAX_FILENAME_LENGTH];

  ofn.hwndOwner = hWnd;
  switch (FileFilter) {
    case FILEFILTER_DISCFILES:
      ofn.lpstrFilter = szSTFilter;
      ofn.lpstrTitle = "Select Disc Image";
      break;
    case FILEFILTER_TOSROM:
      ofn.lpstrFilter = szROMFilter;
      ofn.lpstrTitle = "Select TOS Image";
      break;
    case FILEFILTER_MAPFILE:
      ofn.lpstrFilter = szMapFileFilter;
      ofn.lpstrTitle = "Select Keyboard Map file";
      break;
    case FILEFILTER_YMFILE:
      ofn.lpstrFilter = szYMFileFilter;
      ofn.lpstrTitle = "Select YM or WAV file";
      break;
    case FILEFILTER_MEMORYFILE:
      ofn.lpstrFilter = szMemoryFileFilter;
      ofn.lpstrTitle = "Select Memory Capture file";
      break;

    default:
      ofn.lpstrFilter = szAllFilesFilter;
      ofn.lpstrTitle = NULL;
      break;
  }
  ofn.lpstrFileTitle = NULL;
  // Create filename and directory of previous file
  _splitpath(pFullFileName,szSrcDrive,szSrcDir,szSrcName,szSrcExt);
  
  // Filename only, save FULL path and file back here when quit
  _makepath(pFullFileName,"","",szSrcName,szSrcExt);
  ofn.lpstrFile = pFullFileName;
  // Directory only
  _makepath(szTempDir,szSrcDrive,szSrcDir,"","");
  ofn.lpstrInitialDir = szTempDir;
  if (bFileMustExist)
    ofn.Flags = OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
  else
    ofn.Flags = OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
  ofn.lpTemplateName = MAKEINTRESOURCE(1536);

  if (bSaving)
    return GetSaveFileName(&ofn);
  else
    return GetOpenFileName(&ofn);
*/
}

//-----------------------------------------------------------------------
/*
  Remove any '/'s from end of filenames, but keeps / intact
*/
void File_CleanFileName(char *pszFileName)
{
  char szString[MAX_FILENAME_LENGTH];
  int i=0,j=0;

  // Remove end slash from filename! But / remains! Doh!
  if( strlen(pszFileName)>2 && pszFileName[strlen(pszFileName)-1]=='/' )
    pszFileName[strlen(pszFileName)-1]=0;
}

//-----------------------------------------------------------------------
/*
  Add '/' to end of filename
*/
void File_AddSlashToEndFileName(char *pszFileName)
{
  // Check dir/filenames
  if (strlen(pszFileName)!=0) {
    if (pszFileName[strlen(pszFileName)-1]!='/')
      strcat(pszFileName,"/");  // Must use end slash
  }
}

/*-----------------------------------------------------------------------*/
/*
  Does filename extension match? If so, return TRUE
*/
BOOL File_DoesFileExtensionMatch(char *pszFileName, char *pszExtension)
{
  if ( strlen(pszFileName) < strlen(pszExtension) )
    return(FALSE);
  /* Is matching extension? */
  if ( !strcasecmp(&pszFileName[strlen(pszFileName)-strlen(pszExtension)], pszExtension) )
    return(TRUE);

  /* No */
  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  Check if filename is from root
  
  Return TRUE if filename is '/', else give FALSE
*/
BOOL File_IsRootFileName(char *pszFileName)
{
  if (pszFileName[0]=='\0')     /* If NULL string return! */
    return(FALSE);

  if (pszFileName[0]=='/')
    return(TRUE);

  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  Return string, to remove 'C:' part of filename
*/
char *File_RemoveFileNameDrive(char *pszFileName)
{
  if ( (pszFileName[0]!='\0') && (pszFileName[1]==':') )
    return(&pszFileName[2]);
  else
    return(pszFileName);
}

//-----------------------------------------------------------------------
/*
  Return string, which is just 'C:\' or '\'
*/
char *File_GetFileNameDrive(char *pszFileName)
{
/*  if ( (pszFileName[0]!='\0') && (pszFileName[1]==':') )
    pszFileName[3] = '\0';*/
  if (pszFileName[0]=='/')
    pszFileName[1] = '\0';

  return(pszFileName);
}

//-----------------------------------------------------------------------
/*
  Check if filename end with a '/'
  
  Return TRUE if filename ends with '/'
*/
BOOL File_DoesFileNameEndWithSlash(char *pszFileName)
{
  if (pszFileName[0]=='\0')    /* If NULL string return! */
    return(FALSE);

  // Does string end in a '\'
  if (pszFileName[strlen(pszFileName)-1]=='/')
    return(TRUE);

  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  Remove any double '/'s  from end of filenames. So just the one
*/
void File_RemoveFileNameTrailingSlashes(char *pszFileName)
{
  int Length;

  /* Do have slash at end of filename? */
  Length = strlen(pszFileName);
  if (Length>=3) {
    if (pszFileName[Length-1]=='/') {     /* Yes, have one previous? */
      if (pszFileName[Length-2]=='/')
        pszFileName[Length-1] = '\0';     /* then remove it! */
    }
  }
}

//-----------------------------------------------------------------------
/*
  Return directory string from full path filename, including trailing '/'
*/
void File_GetDirectoryString(char *pszFileName, char *pszDirName)
{
fprintf(stderr,"FIXME: File_GetDirectoryString(%s,%s)\n",pszFileName,pszDirName);
/* FIXME */
/*
  char szDrive[_MAX_DRIVE],szDir[_MAX_DIR],szName[_MAX_FNAME],szExt[_MAX_EXT];

  // So, first split name into parts
  _splitpath(pszFileName,szDrive,szDir,szName,szExt);
  if (strlen(szExt)>0) {
    // Recombine, with out filename or extension
    _makepath(pszDirName,szDrive,szDir,"","");
  }
  else {
    // Was just a directory, so use as is
    strcpy(pszDirName,pszFileName);
  }
  // Make sure ends with a '/'
  File_AddSlashToEndFileName(pszDirName);
*/
}

//-----------------------------------------------------------------------
/*
  Does filename end with a .MSA extension? If so, return TRUE
*/
BOOL File_FileNameIsMSA(char *pszFileName)
{
  return(File_DoesFileExtensionMatch(pszFileName,".msa"));
}

//-----------------------------------------------------------------------
/*
  Does filename end with a .ST extension? If so, return TRUE
*/
BOOL File_FileNameIsST(char *pszFileName)
{
  return(File_DoesFileExtensionMatch(pszFileName,".st"));
}



//-----------------------------------------------------------------------
/*
  Read file from PC into memory, allocate memory for it if need to(pass Address as NULL)
  Also may pass 'unsigned long' if want to find size of file read(may pass as NULL)
*/
void *File_Read(char *pszFileName, void *pAddress, long *pFileSize, char *ppszExts[])
{
  int DiscFile;
  void *pFile=NULL;
  long FileSize=0;

  /* Does the file exist? If not, see if can scan for other extensions and try these */
  if (!File_Exists(pszFileName) && ppszExts) {
    /* Try other extensions, if suceeds correct filename is now in 'pszFileName' */
    File_FindPossibleExtFileName(pszFileName,ppszExts);
  }

  /* Open our file */
  DiscFile = open(pszFileName, O_RDONLY);
  if (DiscFile>=0) {
    /* Find size of TOS image - 192k or 256k */
    FileSize = lseek(DiscFile, 0, SEEK_END);
    lseek(DiscFile, 0, SEEK_SET);
    /* Find pointer to where to load, allocate memory if pass NULL */
    if (pAddress)
      pFile = pAddress;
    else
      pFile = Memory_Alloc(FileSize);
    /* Read in... */
    if (pFile)
      read(DiscFile,(char *)pFile,FileSize);

    close(DiscFile);
  }
  /* Store size of file we read in(or 0 if failed) */
  if (pFileSize)
    *pFileSize = FileSize;

  return(pFile);        /* Return to where read in/allocated */
}

//-----------------------------------------------------------------------
/*
  Save file to PC, return FALSE if errors
*/
BOOL File_Save(char *pszFileName, void *pAddress,long Size,BOOL bQueryOverwrite)
{
  int DiscFile;
  BOOL bRet=FALSE;

  /* Check if need to ask user if to overwrite */
  if (bQueryOverwrite) {
    /* If file exists, ask if OK to overwrite */
    if (!File_QueryOverwrite(pszFileName))
      return(FALSE);
  }

  /* Create our file */
  DiscFile = open(pszFileName, O_CREAT | O_WRONLY);
  if (DiscFile>=0) {
    /* Write data, set success flag */
    if (write(DiscFile,(char *)pAddress,Size)==Size)
      bRet = TRUE;

    close(DiscFile);
  }

  return(bRet);
}

//-----------------------------------------------------------------------
/*
  Return size of file, -1 if error
*/
int File_Length(char *pszFileName)
{
  int DiscFile;
  int FileSize;

  /* Attempt to open file(with OF_EXIST) */
  DiscFile = open(pszFileName, O_RDONLY);
  if (DiscFile>=0) {
    /* Find length */
    FileSize = lseek(DiscFile,0,SEEK_END);
    close(DiscFile);

    return(FileSize);
  }

  return(-1);
}

//-----------------------------------------------------------------------
/*
  Return TRUE if file exists
*/
BOOL File_Exists(char *pszFileName)
{
  int DiscFile;

  // Attempt to open file(with OF_EXIST)
  DiscFile = open(pszFileName, O_RDONLY);
  if (DiscFile!=-1)
    return(TRUE);
  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  Delete file, return TRUE if OK
*/
BOOL File_Delete(char *pszFileName)
{
  // Delete the file(must be closed first)
  return( remove(pszFileName) );
}

//-----------------------------------------------------------------------
/*
  Find if file exists, and if so ask user if OK to overwrite
*/
BOOL File_QueryOverwrite(/*HWND hWnd,*/char *pszFileName)
{

  char szString[MAX_FILENAME_LENGTH];

  // Try and find if file exists
  if (File_Exists(pszFileName)) {
    // File does exist, are we OK to overwrite?
    sprintf(szString,"File '%s' exists, overwrite?",pszFileName);
/* FIXME: */
//    if (MessageBox(hWnd,szString,PROG_NAME,MB_YESNO | MB_DEFBUTTON2 | MB_ICONSTOP)==IDNO)
//      return(FALSE);
  }

  return(TRUE);
}

//-----------------------------------------------------------------------
/*
  Try filename with various extensions and check if file exists - if so return correct name
*/
BOOL File_FindPossibleExtFileName(char *pszFileName,char *ppszExts[])
{
/* FIXME */
/*
  char szSrcDrive[_MAX_DRIVE],szSrcDir[_MAX_DIR],szSrcName[_MAX_FNAME],szSrcExt[_MAX_EXT];
  char szTempFileName[MAX_FILENAME_LENGTH];
  int i=0;

  // Split filename into parts
  _splitpath(pszFileName,szSrcDrive,szSrcDir,szSrcName,szSrcExt);

  // Scan possible extensions
  while(ppszExts[i]) {
    // Re-build with new file extension
    _makepath(szTempFileName,szSrcDrive,szSrcDir,szSrcName,ppszExts[i]);
    // Does this file exist?
    if (File_Exists(szTempFileName)) {
      // Copy name for return
      strcpy(pszFileName,szTempFileName);
      return(TRUE);
    }

    // Next one
    i++;
  }
*/
  // No, none of the files exist
  return(FALSE);
}
