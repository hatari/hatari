/*
  Hatari

  GEMDos intercept routines. These are used mainly for Hard Drive redirection of high level
  file routines.
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "main.h"
#include "cart.h"
#include "debug.h"
#include "decode.h"
#include "dialog.h"
#include "file.h"
#include "floppy.h"
#include "gemdos.h"
#include "m68000.h"
#include "memAlloc.h"
#include "memorySnapShot.h"
#include "misc.h"
#include "printer.h"
#include "rs232.h"
#include "statusBar.h"
#include "stMemory.h"
#include "view.h"

#define ENABLE_SAVING             /* Turn on saving stuff */

#define INVALID_HANDLE_VALUE -1

#ifndef MAX_PATH
#define MAX_PATH 256
#endif

typedef struct 
{
 BOOL bUsed;
 int FileHandle;
} FILE_HANDLE;

FILE_HANDLE  FileHandles[MAX_FILE_HANDLES];
//INTERNAL_DTA InternalDTAs[MAX_DTAS_FILES];
int DTAIndex;                                 /* Circular index into above */
BOOL bInitGemDOS;                             /* Have we re-directed GemDOS vector to our own routines yet? */
DTA *pDTA;                                    /* Our GEMDOS hard drive Disc Transfer Address structure */
unsigned short int CurrentDrive;              /* Current drive (0=A,1=B,2=C etc...) */

#ifdef DEBUG_TO_FILE
/* List of GEMDos functions... */
char *pszGemDOSNames[] = {
  "Term",                 //0x00
  "Conin",                //0x01
  "ConOut",               //0x02
  "Auxiliary Input",      //0x03
  "Auxiliary Output",     //0x04
  "Printer Output",       //0x05
  "RawConIO",             //0x06
  "Direct Conin no echo", //0x07
  "Conin no echo",        //0x08
  "Print line",           //0x09
  "ReadLine",             //0x0a
  "ConStat",              //0x0b
  "",                     //0x0c
  "",                     //0x0d
  "SetDrv",               //0x0e
  "",                     //0x0f
  "Conout Stat",          //0x10
  "PrtOut Stat",          //0x11
  "Auxin Stat",           //0x12
  "AuxOut Stat",          //0x13
  "",                     //0x14
  "",                     //0x15
  "",                     //0x16
  "",                     //0x17
  "",                     //0x18
  "Current Disk",         //0x19
  "Set DTA",              //0x1a
  "",        //0x1b
  "",        //0x1c
  "",        //0x1d
  "",        //0x1e
  "",        //0x1f
  "Super",   //0x20
  "",        //0x21
  "",        //0x22
  "",        //0x23
  "",        //0x24
  "",        //0x25
  "",        //0x26
  "",        //0x27
  "",        //0x28
  "",        //0x29
  "Get Date",      //0x2a
  "Set Date",      //0x2b
  "Get Time",      //0x2c
  "Set Time",      //0x2d
  "",              //0x2e
  "Get DTA",       //0x2f
  "Get Version Number",   //0x30
  "Keep Process",         //0x31
  "",        //0x32
  "",        //0x33
  "",        //0x34
  "",        //0x35
  "Get Disk Free Space",  //0x36
  "",           //0x37
  "",           //0x38
  "MkDir",      //0x39
  "RmDir",      //0x3a
  "ChDir",      //0x3b
  "Create",     //0x3c
  "Open",       //0x3d
  "Close",      //0x3e
  "Read",       //0x3f
  "Write",      //0x40
  "UnLink",     //0x41
  "LSeek",      //0x42
  "ChMod",      //0x43
  "",           //0x44
  "Dup",        //0x45
  "Force",      //0x46
  "GetDir",     //0x47
  "Malloc",     //0x48
  "MFree",      //0x49
  "SetBlock",   //0x4a
  "Exec",       //0x4b
  "Term",       //0x4c
  "",           //0x4d
  "SFirst",     //0x4e
  "SNext",      //0x4f
  "",           //0x50
  "",           //0x51
  "",           //0x52
  "",           //0x53
  "",           //0x54
  "",           //0x55
  "Rename",     //0x56
  "GSDTof"      //0x57
};
#endif



/* Convert a string to uppercase */
void strupr(char *string)
{
 int i;
 for(i=0; i<strlen(string); i++)
   string[i] = toupper(string[i]);
}


//-----------------------------------------------------------------------
/*
  Initialize GemDOS/PC file system
*/
void GemDOS_Init(void)
{
  // Clear handles structure
  Memory_Clear(FileHandles,sizeof(FILE_HANDLE)*MAX_FILE_HANDLES);
}

//-----------------------------------------------------------------------
/*
  Reset GemDOS file system
*/
void GemDOS_Reset(void)
{
  int i;

  // Init file handles table
  for(i=0; i<MAX_FILE_HANDLES; i++) {
    // Was file open? If so close it
    if (FileHandles[i].bUsed)
      close(FileHandles[i].FileHandle);

    FileHandles[i].FileHandle = INVALID_HANDLE_VALUE;
    FileHandles[i].bUsed = FALSE;
  }

  // Reset
  bInitGemDOS = FALSE;
  CurrentDrive = nBootDrive;
  pDTA = NULL;
  DTAIndex = 0;
}

//-----------------------------------------------------------------------
/*
  Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
*/
void GemDOS_MemorySnapShot_Capture(BOOL bSave)
{
  unsigned int Addr;
  int i;

  // Save/Restore details
  MemorySnapShot_Store(&DTAIndex,sizeof(DTAIndex));
  MemorySnapShot_Store(&bInitGemDOS,sizeof(bInitGemDOS));
  if (bSave) {
    Addr = (unsigned int)pDTA-(unsigned int)STRam;
    MemorySnapShot_Store(&Addr,sizeof(Addr));
  }
  else {
    MemorySnapShot_Store(&Addr,sizeof(Addr));
    pDTA = (DTA *)((unsigned int)STRam+(unsigned int)Addr);
  }
  MemorySnapShot_Store(&CurrentDrive,sizeof(CurrentDrive));
  // Don't save file handles as files may have changed which makes
  // it impossible to get a valid handle back
  if (!bSave) {
    // Clear file handles
    for(i=0; i<MAX_FILE_HANDLES; i++) {
      FileHandles[i].FileHandle = INVALID_HANDLE_VALUE;
      FileHandles[i].bUsed = FALSE;
    }
    // And DTAs
/*FIXME*/
/*    for(i=0; i<MAX_DTAS_FILES; i++) {
      InternalDTAs[i].FileHandle = INVALID_HANDLE_VALUE;
      memset(&InternalDTAs[i].FindFileData,0x0,sizeof(WIN32_FIND_DATA));
    }
*/
  }
}

//-----------------------------------------------------------------------
/*
  Return free PC file handle table index, or -1 if error
*/
int GemDOS_FindFreeFileHandle(void)
{
  int i;

  /* Scan our file list for free slot */
  for(i=0; i<MAX_FILE_HANDLES; i++) {
    if (!FileHandles[i].bUsed)
      return(i);
  }

  /* Cannot open any more files, return error */
  return(-1);
}

//-----------------------------------------------------------------------
/*
  Check ST handle is within our table range, return TRUE if not
*/
BOOL GemDOS_IsInvalidFileHandle(int Handle)
{
  BOOL bInvalidHandle=FALSE;

  /* Check handle was valid with our handle table */
  if ( (Handle<0) || (Handle>=MAX_FILE_HANDLES) )
    bInvalidHandle = TRUE;
  else if (!FileHandles[Handle].bUsed)
    bInvalidHandle = TRUE;

  return(bInvalidHandle);
}

//-----------------------------------------------------------------------
/*
  Find drive letter from a filename, eg C,D... and return as drive ID(C:2, D:3...)
*/
int GemDOS_FindDriveNumber(char *pszFileName)
{
  /* Does have 'A:' or 'C:' etc.. at start of string? */
  if ( (pszFileName[0]!='\0') && (pszFileName[1]==':') ) {
    if ( (pszFileName[0]>='a') && (pszFileName[0]<='z') )
      return(pszFileName[0]-'a');
    else if ( (pszFileName[0]>='A') && (pszFileName[0]<='Z') )
      return(pszFileName[0]-'A');
  }

  return(CurrentDrive);
}

//-----------------------------------------------------------------------
/*
  Return drive ID(C:2, D:3 etc...) or -1 if not one of our emulation hard-drives
*/
int GemDOS_IsFileNameAHardDrive(char *pszFileName)
{
  int DriveLetter;

  /* Do we even have a hard-drive? */
  if (ConfigureParams.HardDisc.nDriveList!=DRIVELIST_NONE) {
    // Find drive letter(as number)
    DriveLetter = GemDOS_FindDriveNumber(pszFileName);
    // Does match one of our drives?
    if ( (DriveLetter>=2) && (DriveLetter<=DRIVELIST_TO_DRIVE_INDEX(ConfigureParams.HardDisc.nDriveList)) )
      return(DriveLetter);
  }

  // No, let TOS handle it
  return(-1);
}

//-----------------------------------------------------------------------
/*
  Use hard-drive directory, current ST directory and filename to create full path
*/
void GemDOS_CreateHardDriveFileName(int Drive,char *pszFileName,char *pszDestName)
{
  int DirIndex = Misc_LimitInt(Drive-2, 0,ConfigureParams.HardDisc.nDriveList-1);
//  debug << "::" << pszFileName << endl;
  /* Combine names */
  if (File_IsRootFileName(pszFileName))
    sprintf(pszDestName,"%s%s",ConfigureParams.HardDisc.szHardDiscDirectories[DirIndex],File_RemoveFileNameDrive(pszFileName));
  else {
    if (File_DoesFileNameEndWithSlash(szCurrentDir))
      sprintf(pszDestName,"%s%s%s",ConfigureParams.HardDisc.szHardDiscDirectories[DirIndex],File_RemoveFileNameDrive(szCurrentDir),File_RemoveFileNameDrive(pszFileName));
    else
      sprintf(pszDestName,"%s%s/%s",ConfigureParams.HardDisc.szHardDiscDirectories[DirIndex],File_RemoveFileNameDrive(szCurrentDir),File_RemoveFileNameDrive(pszFileName));
  }
  // Remove any '/'s at end of filenames
  File_RemoveFileNameTrailingSlashes(pszDestName);

  // And make all upper case, as original ST
  strupr(pszDestName);
//  debug << "\t" << pszDestName << endl;
}

//-----------------------------------------------------------------------
/*
  Covert from FindFirstFile/FindNextFile attribute to GemDOS format
*/
char GemDOS_ConvertAttribute(int dwFileAttributes)
{
  char Attrib=0;
/* FIXME */
/*
  // Look up attributes
  if (dwFileAttributes&FILE_ATTRIBUTE_READONLY)
    Attrib |= GEMDOS_FILE_ATTRIB_READONLY;
  if (dwFileAttributes&FILE_ATTRIBUTE_HIDDEN)
    Attrib |= GEMDOS_FILE_ATTRIB_HIDDEN;
  if (dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)
    Attrib |= GEMDOS_FILE_ATTRIB_SUBDIRECTORY;
*/
  return(Attrib);
}

//-----------------------------------------------------------------------
/*
  GEMDOS Cauxin
  Call 0x3
*/
BOOL GemDOS_Cauxin(unsigned long Params)
{
  unsigned char Char;

  // Wait here until a character is ready
  while(!RS232_GetStatus());

  // And read character
  RS232_ReadBytes(&Char,1);
  Regs[REG_D0] = Char;

  return(TRUE);
}

//-----------------------------------------------------------------------
/*
  GEMDOS Cauxout
  Call 0x4
*/
BOOL GemDOS_Cauxout(unsigned long Params)
{
  unsigned char Char;

  // Send character to RS232
  Char = STMemory_ReadWord(Params+SIZE_WORD);
  RS232_TransferBytesTo(&Char,1);

  return(TRUE);
}

//-----------------------------------------------------------------------
/*
  GEMDOS Cprnout
  Call 0x5
*/
BOOL GemDOS_Cprnout(unsigned long Params)
{
  unsigned char Char;

  // Send character to printer(or file)
  Char = STMemory_ReadWord(Params+SIZE_WORD);
  Printer_TransferByteTo(Char);
  Regs[REG_D0] = -1;                // Printer OK

  return(TRUE);
}

//-----------------------------------------------------------------------
/*
  GEMDOS Set drive (0=A,1=B,2=C etc...)
  Call 0xE
*/
BOOL GemDOS_SetDrv(unsigned long Params)
{
  // Read details from stack for our own use
  CurrentDrive = STMemory_ReadWord(Params+SIZE_WORD);
//  debug << "CurrentDrive: " << CurrentDrive << endl;

  // Still re-direct to TOS
  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  GEMDOS Cprnos
  Call 0x11
*/
BOOL GemDOS_Cprnos(unsigned long Params)
{
  Regs[REG_D0] = -1;                // Printer OK

  return(TRUE);
}

//-----------------------------------------------------------------------
/*
  GEMDOS Cauxis
  Call 0x12
*/
BOOL GemDOS_Cauxis(unsigned long Params)
{
  // Read our RS232 state
  if (RS232_GetStatus())
    Regs[REG_D0] = -1;              // Chars waiting
  else
    Regs[REG_D0] = 0;

  return(TRUE);
}

//-----------------------------------------------------------------------
/*
  GEMDOS Cauxos
  Call 0x13
*/
BOOL GemDOS_Cauxos(unsigned long Params)
{
  Regs[REG_D0] = -1;                // Device ready

  return(TRUE);
}

//-----------------------------------------------------------------------
/*
  GEMDOS Set Disc Transfer Address (DTA)
  Call 0x1A
*/
BOOL GemDOS_SetDTA(unsigned long Params)
{
  // Look up on stack to find where DTA is! Store as PC pointer
  pDTA = (DTA *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
  
//  char szString[256];
//  sprintf(szString,"0x%X",STMemory_ReadLong(Params+SIZE_WORD));
//  debug << " to " << szString << endl;

  // Still re-direct to TOS
  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  GEMDOS MkDir
  Call 0x39
*/
BOOL GemDOS_MkDir(unsigned long Params)
{  
  char szDirPath[MAX_PATH];
  char *pDirName;
  int Drive;

  // Find directory to make
  pDirName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
//  debug << pDirName << endl;
  Drive = GemDOS_IsFileNameAHardDrive(pDirName);
//  debug << Drive << endl;
  if (ISHARDDRIVE(Drive)) {
    // Copy old directory, as if calls fails keep this one
    GemDOS_CreateHardDriveFileName(Drive,pDirName,szDirPath);

    // Attempt to make directory
    if ( mkdir(szDirPath, 0755)==0 )
      Regs[REG_D0] = GEMDOS_EOK;
    else
      Regs[REG_D0] = GEMDOS_EACCDN;        // Access denied

    return(TRUE);
  }
  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  GEMDOS RmDir
  Call 0x3A
*/
BOOL GemDOS_RmDir(unsigned long Params)
{  
  char szDirPath[MAX_PATH];
  char *pDirName;
  int Drive;

  // Find directory to make
  pDirName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
  Drive = GemDOS_IsFileNameAHardDrive(pDirName);
  if (ISHARDDRIVE(Drive)) {
    // Copy old directory, as if calls fails keep this one
    GemDOS_CreateHardDriveFileName(Drive,pDirName,szDirPath);

    // Attempt to make directory
    if ( rmdir(szDirPath)==0 )
      Regs[REG_D0] = GEMDOS_EOK;
    else
      Regs[REG_D0] = GEMDOS_EACCDN;        // Access denied

    return(TRUE);
  }
  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  GEMDOS ChDir
  Call 0x3B
*/
BOOL GemDOS_ChDir(unsigned long Params)
{  
  char szDirPath[MAX_PATH];
  char *pDirName;
  int Drive;

  // Find new directory
  pDirName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
//  debug << pDirName << endl;
  Drive = GemDOS_IsFileNameAHardDrive(pDirName);
  if (ISHARDDRIVE(Drive)) {
    // Check path exists, else error
    GemDOS_CreateHardDriveFileName(Drive,"",szDirPath);

    if ( chdir(szDirPath)==0 ) {
      strcpy(szCurrentDir,pDirName);
      Regs[REG_D0] = GEMDOS_EOK;
    }
    else
      Regs[REG_D0] = GEMDOS_EPTHNF;        // Path not found

    return(TRUE);
  }

  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  GEMDOS Create file
  Call 0x3C
*/
BOOL GemDOS_Create(unsigned long Params)
{
  char szActualFileName[MAX_PATH];
  char *pszFileName;
  unsigned int Access;
  int Drive,Index,Mode;

  // Find filename
  pszFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
  Mode = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG);
  Drive = GemDOS_IsFileNameAHardDrive(pszFileName);
  if (ISHARDDRIVE(Drive)) {
    // And convert to hard drive filename
    GemDOS_CreateHardDriveFileName(Drive,pszFileName,szActualFileName);

    // Find slot to store file handle, as need to return WORD handle for ST (NOTE PC's Window handles are all LONGS)
    Index = GemDOS_FindFreeFileHandle();
    if (Index==-1) {
      // No free handles, return error code
      Regs[REG_D0] = GEMDOS_ENHNDL;       // No more handles
      return(TRUE);
    }
    else {
#ifdef ENABLE_SAVING
      // Select mode
      switch(Mode&0x01) {                 // Top bits used in some TOSes
        case 0:                           // Read/Write
//FIXME          Access = GENERIC_READ|GENERIC_WRITE;
          break;
        case 1:                           // Write only
//FIXME          Access = GENERIC_WRITE;
          break;
      }

//FIXME      FileHandles[Index].FileHandle = CreateFile(szActualFileName,Access,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
      if (FileHandles[Index].FileHandle!=INVALID_HANDLE_VALUE) {
        /* Tag handle table entry as used and return handle */
        FileHandles[Index].bUsed = TRUE;
        Regs[REG_D0] = Index+BASE_FILEHANDLE;  // Return valid ST file handle from range 6 to 45! (ours start from 0)

        return(TRUE);
      }
      else {
        Regs[REG_D0] = GEMDOS_EFILNF;     // File not found
        return(TRUE);
      }
#else
      Regs[REG_D0] = GEMDOS_EFILNF;       // File not found
      return(TRUE);
#endif
    }
  }

  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  GEMDOS Open file
  Call 0x3D
*/
BOOL GemDOS_Open(unsigned long Params)
{
  char szActualFileName[MAX_PATH];
  char *pszFileName;
  unsigned int Access;
  int Drive,Index,Mode;

  // Find filename
  pszFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
  Mode = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG);
  Drive = GemDOS_IsFileNameAHardDrive(pszFileName);
//  Debug_File("Open %s\n",pszFileName);
  if (ISHARDDRIVE(Drive)) {
    // And convert to hard drive filename
    GemDOS_CreateHardDriveFileName(Drive,pszFileName,szActualFileName);

    // Find slot to store file handle, as need to return WORD handle for ST (NOTE PC's Window handles are all LONGS)
    Index = GemDOS_FindFreeFileHandle();
    if (Index==-1) {
      // No free handles, return error code
      Regs[REG_D0] = GEMDOS_ENHNDL;       // No more handles
      return(TRUE);
    }
    else {
      // Select mode
      switch(Mode&0x03) {                 // Top bits used in some TOSes
        case 0:                           // Read only
//FIXME          Access = GENERIC_READ;
          break;
        case 1:                           // Write only
//FIXME          Access = GENERIC_WRITE;
          break;
        case 2:                           // Read/Write
//FIXME          Access = GENERIC_READ|GENERIC_WRITE;
          break;
      }

      // Open file
//FIXME      FileHandles[Index].FileHandle = CreateFile(szActualFileName,Access,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
      if (FileHandles[Index].FileHandle!=INVALID_HANDLE_VALUE) {
        // Tag handle table entry as used and return handle
        FileHandles[Index].bUsed = TRUE;
        Regs[REG_D0] = Index+BASE_FILEHANDLE;  // Return valid ST file handle from range 6 to 45! (ours start from 0)

        return(TRUE);
      }
      else {
        Regs[REG_D0] = GEMDOS_EFILNF;     // File not found
        return(TRUE);
      }
    }
  }

  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  GEMDOS Close file
  Call 0x3E  
*/
BOOL GemDOS_Close(unsigned long Params)
{
  int Handle;

  // Find our handle - may belong to TOS
  Handle = STMemory_ReadWord(Params+SIZE_WORD)-BASE_FILEHANDLE;

  // Check handle was valid
  if (GemDOS_IsInvalidFileHandle(Handle)) {
    // No assume was TOS
    return(FALSE);
  }
  else {
    // Close file and free up handle table
    close(FileHandles[Handle].FileHandle);
    FileHandles[Handle].bUsed = FALSE;
    // Return no error
    Regs[REG_D0] = GEMDOS_EOK;
    return(TRUE);
  }
}

//-----------------------------------------------------------------------
/*
  GEMDOS Read file
  Call 0x3F
*/
BOOL GemDOS_Read(unsigned long Params)
{
  char *pBuffer;
  unsigned long nBytesRead,Size,CurrentPos,FileSize,nBytesLeft;
  int Handle;

  // Read details from stack
  Handle = STMemory_ReadWord(Params+SIZE_WORD)-BASE_FILEHANDLE;
  Size = STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD);
  pBuffer = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD+SIZE_LONG));

  // Check handle was valid
  if (GemDOS_IsInvalidFileHandle(Handle)) {
    // No assume was TOS
    return(FALSE);
  }
  else {
    StatusBar_SetIcon(STATUS_ICON_HARDDRIVE,ICONSTATE_UPDATE);

    // To quick check to see where our file pointer is and how large the file is
    CurrentPos = lseek(FileHandles[Handle].FileHandle, 0, SEEK_CUR);
    FileSize = lseek(FileHandles[Handle].FileHandle, 0, SEEK_END);
    lseek(FileHandles[Handle].FileHandle, CurrentPos, SEEK_SET);

    nBytesLeft = FileSize-CurrentPos;

    // Check for End Of File
    if (nBytesLeft<0) {
      Regs[REG_D0] = GEMDOS_ERROR;

      return(TRUE);
    }
    else {
      // Limit to size of file to prevent windows error
      if (Size>FileSize)
        Size = FileSize;
      // And read data in
      nBytesRead = read(FileHandles[Handle].FileHandle, pBuffer, Size);
//???      FlushFileBuffers(FileHandles[Handle].FileHandle);

      // Return number of bytes read
      Regs[REG_D0] = nBytesRead;

      return(TRUE);
    }
  }
}

//-----------------------------------------------------------------------
/*
  GEMDOS Write file
  Call 0x40
*/
BOOL GemDOS_Write(unsigned long Params)
{
  char *pBuffer;
  unsigned long Size,nBytesWritten;
  int Handle;

#ifdef ENABLE_SAVING
  // Read details from stack
  Handle = STMemory_ReadWord(Params+SIZE_WORD)-BASE_FILEHANDLE;
  Size = STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD);
  pBuffer = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD+SIZE_LONG));

  // Check handle was valid
  if (GemDOS_IsInvalidFileHandle(Handle)) {
    // No assume was TOS
    return(FALSE);
  }
  else {
    StatusBar_SetIcon(STATUS_ICON_HARDDRIVE,ICONSTATE_UPDATE);

    nBytesWritten = write(FileHandles[Handle].FileHandle, pBuffer, Size);
    if (nBytesWritten>=0) {
//???      FlushFileBuffers(FileHandles[Handle].FileHandle);

      Regs[REG_D0] = nBytesWritten;      // OK
    }
    else
      Regs[REG_D0] = GEMDOS_EACCDN;      // Access denied(ie read-only)

    return(TRUE);
  }
#endif

  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  GEMDOS UnLink(Delete) file
  Call 0x41
*/
BOOL GemDOS_UnLink(unsigned long Params)
{
#ifdef ENABLE_SAVING
  char szActualFileName[MAX_PATH];
  char *pszFileName;
  int Drive;

  // Find filename
  pszFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
  Drive = GemDOS_IsFileNameAHardDrive(pszFileName);
  if (ISHARDDRIVE(Drive)) {
    // And convert to hard drive filename
    GemDOS_CreateHardDriveFileName(Drive,pszFileName,szActualFileName);

    // Now delete file??
    if ( unlink(szActualFileName)==0 )
      Regs[REG_D0] = GEMDOS_EOK;          // OK
    else
      Regs[REG_D0] = GEMDOS_EFILNF;       // File not found

    return(TRUE);
  }
#endif

  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  GEMDOS File seek
  Call 0x42
*/
BOOL GemDOS_LSeek(unsigned long Params)
{
  long Offset;
  int Handle,Mode;

  // Read details from stack
  Offset = (long)STMemory_ReadLong(Params+SIZE_WORD);
  Handle = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG)-BASE_FILEHANDLE;
  Mode = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_WORD);

  // Check handle was valid
  if (GemDOS_IsInvalidFileHandle(Handle)) {
    // No assume was TOS
    return(FALSE);
  }
  else {
    // Return offset from start of file
    Regs[REG_D0] = lseek(FileHandles[Handle].FileHandle, Offset, Mode);
    return(TRUE);
  }
}

//-----------------------------------------------------------------------
/*
  PExec Load And Go - Redirect to cart' routine at address 0xFA1000

  If loading from hard-drive(ie drive ID 2 or more) set condition codes to run own GEMDos routines
*/
void GemDOS_Pexec_LoadAndGo(unsigned long Params)
{
  // Hard-drive?
  if (CurrentDrive>=2)                // If not using A: or B:, use my own routines to load
    SR = (SR&0xff00) | SR_OVERFLOW;
}

//-----------------------------------------------------------------------
/*
  PExec Load But Don't Go - Redirect to cart' routine at address 0xFA1000
*/
void GemDOS_Pexec_LoadDontGo(unsigned long Params)
{
  // Hard-drive?
  if (CurrentDrive>=2)
    SR = (SR&0xff00) | SR_OVERFLOW;
}

//-----------------------------------------------------------------------
/*
  GEMDOS PExec handler
  Call 0x4B
*/
BOOL GemDOS_Pexec(unsigned long Params)
{
  unsigned short int Mode;

  // Find PExec mode
  Mode = STMemory_ReadWord(Params+SIZE_WORD);
//  Debug_File("Pexec %d (Drv:%d)\n",Mode,CurrentDrive);

  // Re-direct as needed
  switch(Mode) {
    case 0:      // Load and go
      GemDOS_Pexec_LoadAndGo(Params);
      return(FALSE);
    case 3:      // Load, don't go
      GemDOS_Pexec_LoadDontGo(Params);
      return(FALSE);
    case 4:      // Just go
      return(FALSE);
    case 5:      // Create basepage
      return(FALSE);
    case 6:
      return(FALSE);

    default:
      return(FALSE);
  }

  // Still re-direct to TOS
  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  GEMDOS Find first file
  Call 0x4E
*/
BOOL GemDOS_SFirst(unsigned long Params)
{
  int FatDate, FatTime;
  char szActualFileName[MAX_PATH];
  char *pszFileName;
  unsigned short int Attr;
  int Drive;

  // Find filename to search for
  pszFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
  Attr = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG);
//  debug << "SFirst: " << pszFileName << endl;
//  M68000_OutputHistory();

  Drive = GemDOS_IsFileNameAHardDrive(pszFileName);
  if (ISHARDDRIVE(Drive)) {
    StatusBar_SetIcon(STATUS_ICON_HARDDRIVE,ICONSTATE_UPDATE);

    // And convert to hard drive filename
    GemDOS_CreateHardDriveFileName(Drive,pszFileName,szActualFileName);

    // Populate DTA, set index for our use
    STMemory_WriteWord_PCSpace(pDTA->index,DTAIndex);
    STMemory_WriteLong_PCSpace(pDTA->magic,DTA_MAGIC_NUMBER);
    strcpy(pDTA->dta_pat,"");
    pDTA->dta_sattrib = 0;
    pDTA->dta_attrib = 0;

    // Were we looking for the volume label? Read directly from drive
    if (Attr&GEMDOS_FILE_ATTRIB_VOLUME_LABEL) {
      // Default and find drive from filename
      strcpy(pDTA->dta_name,"");
      File_GetFileNameDrive(pszFileName);
//FIXME      if (GetVolumeInformation(pszFileName,pDTA->dta_name,TOS_NAMELEN,NULL,NULL,NULL,NULL,0))
//        strupr(pDTA->dta_name);
      Regs[REG_D0] = GEMDOS_EOK;          // Got volume
      return(TRUE);
    }

    // Scan for first file
/* FIXME */
/*
    InternalDTAs[DTAIndex].FileHandle = FindFirstFile(szActualFileName,&InternalDTAs[DTAIndex].FindFileData);
    if (InternalDTAs[DTAIndex].FileHandle==INVALID_HANDLE_VALUE) {
      // No files of that match, return error code
      Regs[REG_D0] = GEMDOS_EFILNF;        // File not found
      return(TRUE);
    }
    else {
      // Repeat find until have useable filename! The PC returns '.' and '..' - ignore '.'!
      while( !stricmp(InternalDTAs[DTAIndex].FindFileData.cFileName,".") ) {
        if (FindNextFile(InternalDTAs[DTAIndex].FileHandle,&InternalDTAs[DTAIndex].FindFileData)==0) {
          // If this is all there is, then error
          Regs[REG_D0] = GEMDOS_ENMFIL;    // No more files
          return(TRUE);
        }
      }

      // And make all upper case, as original ST
      strupr(InternalDTAs[DTAIndex].FindFileData.cFileName);
      strcpy(pDTA->dta_name,InternalDTAs[DTAIndex].FindFileData.cFileName);

      // Fill remaining details, as PC
//FIXME      STMemory_WriteLong_PCSpace(pDTA->dta_size,InternalDTAs[DTAIndex].FindFileData.nFileSizeLow);
//FIXME      Misc_TimeDataToDos(&InternalDTAs[DTAIndex].FindFileData.ftLastWriteTime,&FatDate,&FatTime);
      STMemory_WriteWord_PCSpace(pDTA->dta_time,FatTime);
      STMemory_WriteWord_PCSpace(pDTA->dta_date,FatDate);
//FIXME      pDTA->dta_attrib = GemDOS_ConvertAttribute(InternalDTAs[DTAIndex].FindFileData.dwFileAttributes);

      Regs[REG_D0] = GEMDOS_EOK;

      DTAIndex++;
      DTAIndex&=(MAX_DTAS_FILES-1);
    }
    return(TRUE);
*/
  }
  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  GEMDOS Search Next
  Call 0x4F
*/
BOOL GemDOS_SNext(unsigned long Params)
{
  int FatDate, FatTime;
  int DTAIndex;

  // Was DTA ours or TOS?
  if (STMemory_ReadLong_PCSpace(pDTA->magic)==DTA_MAGIC_NUMBER) {
    StatusBar_SetIcon(STATUS_ICON_HARDDRIVE,ICONSTATE_UPDATE);

    // Find index into our list of structures
    DTAIndex = STMemory_ReadWord_PCSpace(pDTA->index)&(MAX_DTAS_FILES-1);
/*FIXME
    if (FindNextFile(InternalDTAs[DTAIndex].FileHandle,&InternalDTAs[DTAIndex].FindFileData)==0) {
      Regs[REG_D0] = GEMDOS_ENMFIL;        // No more files
      return(TRUE);
    }
    // Find next file on hard drive
    else {
      // And make all upper case, as original ST
      strupr(InternalDTAs[DTAIndex].FindFileData.cFileName);
      strcpy(pDTA->dta_name,InternalDTAs[DTAIndex].FindFileData.cFileName);
      // Fill remaining details, as PC
      STMemory_WriteLong_PCSpace(pDTA->dta_size,InternalDTAs[DTAIndex].FindFileData.nFileSizeLow);
      Misc_TimeDataToDos(&InternalDTAs[DTAIndex].FindFileData.ftLastWriteTime,&FatDate,&FatTime);
      STMemory_WriteWord_PCSpace(pDTA->dta_time,FatTime);
      STMemory_WriteWord_PCSpace(pDTA->dta_date,FatDate);
      pDTA->dta_attrib = GemDOS_ConvertAttribute(InternalDTAs[DTAIndex].FindFileData.dwFileAttributes);
  
      Regs[REG_D0] = GEMDOS_EOK;
      return(TRUE);
    }
*/
  }

  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  GEMDOS Rename
  Call 0x56
*/
BOOL GemDOS_Rename(unsigned long Params)
{
  char *pszNewFileName,*pszOldFileName;
  char szNewActualFileName[MAX_PATH],szOldActualFileName[MAX_PATH];
  int NewDrive, OldDrive;

  // Read details from stack
  pszOldFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD));
  pszNewFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD+SIZE_LONG));

  NewDrive = GemDOS_IsFileNameAHardDrive(pszNewFileName);
  OldDrive = GemDOS_IsFileNameAHardDrive(pszOldFileName);
  if (ISHARDDRIVE(NewDrive) && ISHARDDRIVE(OldDrive)) {
    // And convert to hard drive filenames
    GemDOS_CreateHardDriveFileName(NewDrive,pszNewFileName,szNewActualFileName);
    GemDOS_CreateHardDriveFileName(OldDrive,pszOldFileName,szOldActualFileName);

    // Rename files
    if ( rename(szOldActualFileName,szNewActualFileName)==0 )
      Regs[REG_D0] = GEMDOS_EOK;
    else
      Regs[REG_D0] = GEMDOS_EACCDN;        // Access denied
    return(TRUE);
  }

  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  GEMDOS GSDToF
  Call 0x57
*/
BOOL GemDOS_GSDToF(unsigned long Params)
{
/*FIXME*/
/*
  BY_HANDLE_FILE_INFORMATION FileInfo;
  WORD FatDate,FatTime;
  DATETIME DateTime;
  char *pBuffer;
  int Handle,Flag;

  // Read details from stack
  pBuffer = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
  Handle = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG)-BASE_FILEHANDLE;
  Flag = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_LONG);

  // Check handle was valid
  if (GemDOS_IsInvalidFileHandle(Handle)) {
    // No assume was TOS
    return(FALSE);
  }
  else {
    Regs[REG_D0] = GEMDOS_ERROR;  // Invalid parameter

    if (Flag==0) {    // Read time
      if (GetFileInformationByHandle(FileHandles[Handle].FileHandle,&FileInfo)) {
        if (FileTimeToDosDateTime(&FileInfo.ftCreationTime,&FatDate,&FatTime)) {
          DateTime.hour = FatTime>>11;
          DateTime.minute = FatTime>>5;
          DateTime.second = FatTime;
          DateTime.year = FatDate>>9;
          DateTime.month = FatDate>>5;
          DateTime.day = FatDate;

          Regs[REG_D0] = GEMDOS_EOK;
        }
      }
    }
    else if (Flag==1) {
      Regs[REG_D0] = GEMDOS_EOK;
    }
    
     return(TRUE);
  }
*/
  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  This is called when we get a GemDOS exception. We then re-direct vector to our
  own routine. This forces execution through TOS which sets up the stack etc... and
  then calls our own routine in the cart' space which has the illegal instruction
  'GEMDOS_OPCODE'.
*/
BOOL GemDOS(void)
{
  unsigned long OldGemDOSVector;

  // Init Gemdos if not already
  if (!bInitGemDOS) {
    OldGemDOSVector = STMemory_ReadLong(0x84);
    STMemory_WriteLong(CART_OLDGEMDOS,OldGemDOSVector);  // Store original gemdos handler
    STMemory_WriteLong(0x84,CART_GEMDOS);                // And redirect to new one (see cart.s)

    bInitGemDOS = TRUE;
  }

  // Now execute as normal, we may intercept it again later (see cart.s)
  return(FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  Run GEMDos call, and re-direct if need to. Used to handle hard-disc emulation etc...
  This sets the condition codes(in SR), which are used in the 'cart.s' program to decide if we
  need to run old GEM vector, or PExec or nothing.

  This method keeps the stack and other states consistant with the original ST which is very important
  for the PExec call and maximum compatibility through-out
*/
void GemDOS_OpCode(void)
{
  unsigned short int GemDOSCall,CallingSReg;
  unsigned long Params;

  /* Read SReg from stack to see if parameters are on User or Super stack (We enter here ALWAYS in super mode) */
  CallingSReg = STMemory_ReadWord(Regs[REG_A7]);
  if ((CallingSReg&SR_SUPERMODE)==0)      /* Calling from user mode */
    Params = Regs[REG_A8];
  else              /* Calling from super mode */
    Params = Regs[REG_A7]+SIZE_WORD+SIZE_LONG;

  /* Default to run TOS GemDos (SR_NEG run Gemdos, SR_ZERO already done, SR_OVERFLOW run own 'Pexec' */
  SR &= SR_CLEAR_OVERFLOW;
  SR &= SR_CLEAR_ZERO;
  SR |= SR_NEG;

  /* Find pointer to call parameters */
  GemDOSCall = STMemory_ReadWord(Params);
#ifdef DEBUG_TO_FILE
  Debug_File("GemDOS 0x%X (%s)\n",GemDOSCall,pszGemDOSNames[GemDOSCall]);
#endif

  /* Intercept call */
  switch(GemDOSCall) {
    case 0x3:
      if (GemDOS_Cauxin(Params))
        SR |= SR_ZERO;
      break;
    case 0x4:
      if (GemDOS_Cauxout(Params))
        SR |= SR_ZERO;
      break;
    case 0x5:
      if (GemDOS_Cprnout(Params))
        SR |= SR_ZERO;
      break;
    case 0xe:
      if (GemDOS_SetDrv(Params))
        SR |= SR_ZERO;
      break;      
    case 0x11:
      if (GemDOS_Cprnos(Params))
        SR |= SR_ZERO;
      break;
    case 0x12:
      if (GemDOS_Cauxis(Params))
        SR |= SR_ZERO;
      break;
    case 0x13:
      if (GemDOS_Cauxos(Params))
        SR |= SR_ZERO;
      break;
    case 0x1a:
      if (GemDOS_SetDTA(Params))
        SR |= SR_ZERO;
      break;
    case 0x39:
      if (GemDOS_MkDir(Params))
        SR |= SR_ZERO;
      break;
    case 0x3a:
      if (GemDOS_RmDir(Params))
        SR |= SR_ZERO;
      break;
    case 0x3b:
      if (GemDOS_ChDir(Params))
        SR |= SR_ZERO;
      break;
    case 0x3c:
      if (GemDOS_Create(Params))
        SR |= SR_ZERO;
      break;
    case 0x3d:
      if (GemDOS_Open(Params))
        SR |= SR_ZERO;
      break;
    case 0x3e:
      if (GemDOS_Close(Params))
        SR |= SR_ZERO;
      break;
    case 0x3f:
      if (GemDOS_Read(Params))
        SR |= SR_ZERO;
      break;
    case 0x40:
      if (GemDOS_Write(Params))
        SR |= SR_ZERO;
      break;
    case 0x41:
      if (GemDOS_UnLink(Params))
        SR |= SR_ZERO;
      break;
    case 0x42:
      if (GemDOS_LSeek(Params))
        SR |= SR_ZERO;
      break;
    case 0x4b:
      if (GemDOS_Pexec(Params))
        SR |= SR_ZERO;
      break;
    case 0x4e:
      if (GemDOS_SFirst(Params))
        SR |= SR_ZERO;
      break;
    case 0x4f:
      if (GemDOS_SNext(Params))
        SR |= SR_ZERO;
      break;
    case 0x56:
      if (GemDOS_Rename(Params))
        SR |= SR_ZERO;
      break;
    case 0x57:
      if (GemDOS_GSDToF(Params))
        SR |= SR_ZERO;
      break;
  }

  /* Write back to emulation condition codes, used for code re-direction */
  EmuCCode = SR<<4;
}


//-----------------------------------------------------------------------
/*
  Re-direct execution to old GEM calls, used in 'cart.s'
*/
void GemDOS_RunOldOpCode(void)
{
  /* Set 'PC' to that of old GemDOS routines (see 'old_gemdos' in cart.s) */
  m68k_setpc( STMemory_ReadLong(0xfa1004) );    /* Address of 'old_gemdos' in cart.s */
/*  __asm {
    mov    ecx,[STRAM_OFFSET+0xfa1004]    // Address of 'old_gemdos' in cart.s
    bswap  ecx
    and    ecx,0x00ffffff
    mov    esi,ecx
    add    esi,STRAM_OFFSET      // New PC
    RET
  }
*/
}
