/*
  Hatari - gemdos.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  GEMDOS intercept routines.
  These are used mainly for hard drive redirection of high level file routines.

  Now case is handled by using glob. See the function
  GemDOS_CreateHardDriveFileName for that. It also knows about symlinks.
  A filename is recognized on its eight first characters, do don't try to
  push this too far, or you'll get weirdness ! (But I can even run programs
  directly from a mounted cd in lower cases, so I guess it's working well !).

  Bugs/things to fix:
  * RS232/Printing
  * rmdir routine, can't remove dir with files in it. (another tos/unix difference)
  * Fix bugs, there are probably a few lurking around in here..
*/
static char rcsid[] = "Hatari $Id: gemdos.c,v 1.13 2003-03-24 17:24:18 emanne Exp $";

#include <sys/stat.h>
#include <time.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>
#include <glob.h>

#include "main.h"
#include "cart.h"
#include "tos.h"
#include "debug.h"
#include "decode.h"
#include "dialog.h"
#include "file.h"
#include "floppy.h"
#include "hdc.h"
#include "gemdos.h"
#include "m68000.h"
#include "memAlloc.h"
#include "memorySnapShot.h"
#include "misc.h"
#include "printer.h"
#include "rs232.h"
#include "stMemory.h"

#include "uae-cpu/hatari-glue.h"

/* #define GEMDOS_VERBOSE */
#define ENABLE_SAVING             /* Turn on saving stuff */

#define INVALID_HANDLE_VALUE -1

#ifndef MAX_PATH
#define MAX_PATH 256
#endif


/* structure with all the drive-specific data for our emulated drives */
EMULATEDDRIVE **emudrives = NULL;

typedef struct
{
  BOOL bUsed;
  FILE *FileHandle;
  char szActualName[MAX_PATH];   /* used by F_DATIME (0x57) */
} FILE_HANDLE;

typedef struct
{
  BOOL bUsed;
  int  nentries;                   /* number of entries in fs directory */
  int  centry;                     /* current entry # */
  struct dirent **found;           /* legal files */
  char path[MAX_PATH];             /* sfirst path */
} INTERNAL_DTA;

FILE_HANDLE  FileHandles[MAX_FILE_HANDLES];
INTERNAL_DTA InternalDTAs[MAX_DTAS_FILES];
int DTAIndex;                                 /* Circular index into above */
BOOL bInitGemDOS;                             /* Have we re-directed GemDOS vector to our own routines yet? */
DTA *pDTA;                                    /* Our GEMDOS hard drive Disc Transfer Address structure */
unsigned short int CurrentDrive;              /* Current drive (0=A,1=B,2=C etc...) */

/* List of GEMDos functions... */
char *pszGemDOSNames[] = {
  "Term",                 /*0x00*/
  "Conin",                /*0x01*/
  "ConOut",               /*0x02*/
  "Auxiliary Input",      /*0x03*/
  "Auxiliary Output",     /*0x04*/
  "Printer Output",       /*0x05*/
  "RawConIO",             /*0x06*/
  "Direct Conin no echo", /*0x07*/
  "Conin no echo",        /*0x08*/
  "Print line",           /*0x09*/
  "ReadLine",             /*0x0a*/
  "ConStat",              /*0x0b*/
  "",                     /*0x0c*/
  "",                     /*0x0d*/
  "SetDrv",               /*0x0e*/
  "",                     /*0x0f*/
  "Conout Stat",          /*0x10*/
  "PrtOut Stat",          /*0x11*/
  "Auxin Stat",           /*0x12*/
  "AuxOut Stat",          /*0x13*/
  "",                     /*0x14*/
  "",                     /*0x15*/
  "",                     /*0x16*/
  "",                     /*0x17*/
  "",                     /*0x18*/
  "Current Disk",         /*0x19*/
  "Set DTA",              /*0x1a*/
  "",                     /*0x1b*/
  "",                     /*0x1c*/
  "",                     /*0x1d*/
  "",                     /*0x1e*/
  "",                     /*0x1f*/
  "Super",                /*0x20*/
  "",                     /*0x21*/
  "",                     /*0x22*/
  "",                     /*0x23*/
  "",                     /*0x24*/
  "",                     /*0x25*/
  "",                     /*0x26*/
  "",                     /*0x27*/
  "",                     /*0x28*/
  "",                     /*0x29*/
  "Get Date",             /*0x2a*/
  "Set Date",             /*0x2b*/
  "Get Time",             /*0x2c*/
  "Set Time",             /*0x2d*/
  "",                     /*0x2e*/
  "Get DTA",              /*0x2f*/
  "Get Version Number",   /*0x30*/
  "Keep Process",         /*0x31*/
  "",                     /*0x32*/
  "",                     /*0x33*/
  "",                     /*0x34*/
  "",                     /*0x35*/
  "Get Disk Free Space",  /*0x36*/
  "",           /*0x37*/
  "",           /*0x38*/
  "MkDir",      /*0x39*/
  "RmDir",      /*0x3a*/
  "ChDir",      /*0x3b*/
  "Create",     /*0x3c*/
  "Open",       /*0x3d*/
  "Close",      /*0x3e*/
  "Read",       /*0x3f*/
  "Write",      /*0x40*/
  "UnLink",     /*0x41*/
  "LSeek",      /*0x42*/
  "ChMod",      /*0x43*/
  "",           /*0x44*/
  "Dup",        /*0x45*/
  "Force",      /*0x46*/
  "GetDir",     /*0x47*/
  "Malloc",     /*0x48*/
  "MFree",      /*0x49*/
  "SetBlock",   /*0x4a*/
  "Exec",       /*0x4b*/
  "Term",       /*0x4c*/
  "",           /*0x4d*/
  "SFirst",     /*0x4e*/
  "SNext",      /*0x4f*/
  "",           /*0x50*/
  "",           /*0x51*/
  "",           /*0x52*/
  "",           /*0x53*/
  "",           /*0x54*/
  "",           /*0x55*/
  "Rename",     /*0x56*/
  "GSDTof"      /*0x57*/
};

unsigned char GemDOS_ConvertAttribute(mode_t mode);




/*-------------------------------------------------------*/
/*
  Routines to convert time and date to MSDOS format.
  Originally from the STonX emulator. (cheers!)
*/
unsigned short time2dos (time_t t)
{
	struct tm *x;
	x = localtime (&t);
	return (x->tm_sec>>1)|(x->tm_min<<5)|(x->tm_hour<<11);
}

unsigned short date2dos (time_t t)
{
	struct tm *x;
	x = localtime (&t);
	return x->tm_mday | ((x->tm_mon+1)<<5) | ( ((x->tm_year-80>0)?x->tm_year-80:0) << 9);
}


/*
   Convert a string to uppercase
*/
void strupr(char *string)
{
  while(*string)
  {
    *string = toupper(*string);
    string++;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Populate a DATETIME structure with file info
*/
BOOL GetFileInformation(char *name, DATETIME *DateTime)
{
  struct stat filestat;
  int n;
  struct tm *x;

  n = stat(name, &filestat);
  if( n != 0 ) return(FALSE);
  x = localtime( &filestat.st_mtime );

  DateTime->word1 = 0;
  DateTime->word2 = 0;

  DateTime->word1 |= (x->tm_mday & 0x1F);         /* 5 bits */
  DateTime->word1 |= (x->tm_mon & 0x0F)<<5;       /* 4 bits */
  DateTime->word1 |= (((x->tm_year-80>0)?x->tm_year-80:0) & 0x7F)<<9;      /* 7 bits*/

  DateTime->word2 |= (x->tm_sec & 0x1F);          /* 5 bits */
  DateTime->word2 |= (x->tm_min & 0x3F)<<5;       /* 6 bits */
  DateTime->word2 |= (x->tm_hour & 0x1F)<<11;     /* 5 bits */

  return(TRUE);
}


/*-----------------------------------------------------------------------*/
/*
  Populate the DTA buffer with file info
*/
int PopulateDTA(char *path, struct dirent *file)
{
  char tempstr[MAX_PATH];
  struct stat filestat;
  int n;

  sprintf(tempstr, "%s/%s", path, file->d_name);
  n = stat(tempstr, &filestat);
  if(n != 0) return(FALSE); /* return on error */

  if(!pDTA) return(FALSE); /* no DTA pointer set */
  strupr(file->d_name);    /* convert to atari-style uppercase */
  strncpy(pDTA->dta_name,file->d_name,TOS_NAMELEN); /* FIXME: better handling of long file names */
  STMemory_WriteLong_PCSpace(pDTA->dta_size, (long)filestat.st_size);
  STMemory_WriteWord_PCSpace(pDTA->dta_time, time2dos(filestat.st_mtime));
  STMemory_WriteWord_PCSpace(pDTA->dta_date, date2dos(filestat.st_mtime));
  pDTA->dta_attrib = GemDOS_ConvertAttribute(filestat.st_mode);

  return(TRUE);
}


/*-----------------------------------------------------------------------*/
/*
  Clear a used DTA structure.
*/
void ClearInternalDTA(){
  int i;

  /* clear the old DTA structure */
  if(InternalDTAs[DTAIndex].found != NULL){
    for(i=0; i <InternalDTAs[DTAIndex].nentries; i++)
      free(InternalDTAs[DTAIndex].found[i]);
    free(InternalDTAs[DTAIndex].found);
  }
  InternalDTAs[DTAIndex].bUsed = FALSE;
}


/*-----------------------------------------------------------------------*/
/*
  Match a file to a dir mask.
*/
static int match (char *pat, char *name)
{
  /* make uppercase copies */
  char p0[MAX_PATH], n0[MAX_PATH];
  strcpy(p0, pat);
  strcpy(n0, name);
  strupr(p0); strupr(n0);

  if(name[0] == '.') return(FALSE);                   /* no .* files */
  if (strcmp(pat,"*.*")==0) return(TRUE);
  else if (strcasecmp(pat,name)==0) return(TRUE);
  else
    {
      char *p=p0,*n=n0;
      for(;*n;)
	{
	  if (*p=='*') {while (*n && *n != '.') n++;p++;}
	  else if (*p=='?' && *n) {n++;p++;}
	  else if (*p++ != *n++) return(FALSE);
	}
      if (*p==0)
	{
	  return(TRUE);
	}
    }
  return(FALSE);
}

/*-----------------------------------------------------------------------*/
/*
  Parse directory from sfirst mask
  - e.g.: input:  "hdemudir/auto/mask*.*" outputs: "hdemudir/auto"
*/
void fsfirst_dirname(char *string, char *new){
  int i=0;

  sprintf(new, string);
  /* convert to front slashes. */
  i=0;
  while(new[i] != '\0'){
    if(new[i] == '\\') new[i] = '/';
    i++;
  }
  while(string[i] != '\0'){new[i] = string[i]; i++;} /* find end of string */
  while(new[i] != '/') i--; /* find last slash */
  new[i] = '\0';

}

/*-----------------------------------------------------------------------*/
/*
  Parse directory mask, e.g. "*.*"
*/
void fsfirst_dirmask(char *string, char *new){
  int i=0, j=0;
  while(string[i] != '\0')i++;   /* go to end of string */
  while(string[i] != '/') i--;   /* find last slash */
  i++;
  while(string[i] != '\0')new[j++] = string[i++]; /* go to end of string */
  new[j++] = '\0';
}

/*-----------------------------------------------------------------------*/
/*
  Initialize GemDOS/PC file system
*/
void GemDOS_Init(void)
{
  int i;
  bInitGemDOS = FALSE;

  /* Clear handles structure */
  Memory_Clear(FileHandles, sizeof(FILE_HANDLE)*MAX_FILE_HANDLES);
  /* Clear DTAs */
  for(i=0; i<MAX_DTAS_FILES; i++)
  {
    InternalDTAs[i].bUsed = FALSE;
    InternalDTAs[i].nentries = 0;
    InternalDTAs[i].found = NULL;
  }
  DTAIndex = 0;
}

/*-----------------------------------------------------------------------*/
/*
  Reset GemDOS file system
*/
void GemDOS_Reset()
{
  int i;

  /* Init file handles table */
  for(i=0; i<MAX_FILE_HANDLES; i++)
  {
    /* Was file open? If so close it */
    if (FileHandles[i].bUsed)
      fclose(FileHandles[i].FileHandle);

    FileHandles[i].FileHandle = NULL;
    FileHandles[i].bUsed = FALSE;
  }

  for(i=0; i<MAX_DTAS_FILES; i++)
  {
    InternalDTAs[i].bUsed = FALSE;
    InternalDTAs[i].nentries = 0;
    InternalDTAs[i].found = NULL;
  }

  /* Reset */
  bInitGemDOS = FALSE;
  CurrentDrive = nBootDrive;
  pDTA = NULL;
  DTAIndex = 0;
}


/*-----------------------------------------------------------------------*/
/*
  Initialize a GEMDOS drive.
  Only 1 emulated drive allowed, as of yet.
*/
void GemDOS_InitDrives()
{
  int i;

  /* intialize data for harddrive emulation: */
  if(!GEMDOS_EMU_ON)
  {
    emudrives = malloc( MAX_HARDDRIVES*sizeof(EMULATEDDRIVE *) );
    for(i=0; i<MAX_HARDDRIVES; i++)
      emudrives[0] = malloc( sizeof(EMULATEDDRIVE) );
  }

  for(i=0; i<MAX_HARDDRIVES; i++)
  {
    /* set emulation directory string */
    strcpy(emudrives[i]->hd_emulation_dir, ConfigureParams.HardDisc.szHardDiscDirectories[i]);

    /* remove trailing slash, if any in the directory name */
    File_CleanFileName(emudrives[i]->hd_emulation_dir);

    /* set drive to 2 + number of ACSI partitions */
    emudrives[i]->hd_letter = 2 + nPartitions + i;

    ConfigureParams.HardDisc.nDriveList += 1;

    fprintf(stderr, "Hard drive emulation, %c: <-> %s\n",
            emudrives[i]->hd_letter + 'A', emudrives[i]->hd_emulation_dir);
  }
}

/*-----------------------------------------------------------------------*/
/*
  Un-init the GEMDOS drive
*/
void GemDOS_UnInitDrives()
{
  int i;

  GemDOS_Reset();        /* Close all open files on emulated drive*/

  if(GEMDOS_EMU_ON)
  {
    for(i=0; i<MAX_HARDDRIVES; i++)
    {
      free(emudrives[i]);    /* Release memory */
      ConfigureParams.HardDisc.nDriveList -= 1;
    }

    free(emudrives);
    emudrives = NULL;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
*/
void GemDOS_MemorySnapShot_Capture(BOOL bSave)
{
  unsigned int Addr;
  int i;

  /* Save/Restore details */
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
  /* Don't save file handles as files may have changed which makes
     it impossible to get a valid handle back */
  if (!bSave) {
    /* Clear file handles  */
    for(i=0; i<MAX_FILE_HANDLES; i++) {
      FileHandles[i].FileHandle = NULL;
      FileHandles[i].bUsed = FALSE;
    }
  }
}

/*-----------------------------------------------------------------------*/
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

/*-----------------------------------------------------------------------*/
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

/*-----------------------------------------------------------------------*/
/*
  Find drive letter from a filename, eg C,D... and return as drive ID(C:2, D:3...)
  returns the current drive number if none is specified.
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

/*-----------------------------------------------------------------------*/
/*
  Return drive ID(C:2, D:3 etc...) or -1 if not one of our emulation hard-drives
*/
int GemDOS_IsFileNameAHardDrive(char *pszFileName)
{
  int DriveLetter;

  /* Do we even have a hard-drive? */
  if(GEMDOS_EMU_ON)
    {
      DriveLetter = GemDOS_FindDriveNumber(pszFileName);
      /* add support for multiple drives here.. */
      if( DriveLetter == emudrives[0]->hd_letter )
	return(DriveLetter);
    }
  /* Not a high-level redirected drive */
  return(-1);

  /* this code is depreciated */
  /* Do we even have a hard-drive? */
  if (ConfigureParams.HardDisc.nDriveList!=DRIVELIST_NONE) {
    /* Find drive letter(as number) */
    DriveLetter = GemDOS_FindDriveNumber(pszFileName);
    /* Does match one of our drives? */
    if ( (DriveLetter>=2) && (DriveLetter<=DRIVELIST_TO_DRIVE_INDEX(ConfigureParams.HardDisc.nDriveList)) )
      return(DriveLetter);
  }

  /* No, let TOS handle it */
  return(-1);
}


/*-----------------------------------------------------------------------*/
/*
  Use hard-drive directory, current ST directory and filename to create full path
*/
void GemDOS_CreateHardDriveFileName(int Drive,char *pszFileName,char *pszDestName)
{
  /*  int DirIndex = Misc_LimitInt(Drive-2, 0,ConfigureParams.HardDisc.nDriveList-1); */
  int i;
  char *s,*start;

  if(pszFileName[0] == '\0') return; /* check for valid string */

  /* case full filename "C:\foo\bar" */
  if(pszFileName[1] == ':') {
    sprintf(pszDestName, "%s%s", emudrives[0]->hd_emulation_dir, File_RemoveFileNameDrive(pszFileName));
  }
  /* case referenced from root:  "\foo\bar" */
  else if(pszFileName[0] == '\\'){
    sprintf(pszDestName, "%s%s", emudrives[0]->hd_emulation_dir, pszFileName);
  }
  /* case referenced from current directory */
  else {
    sprintf(pszDestName, "%s%s",  emudrives[0]->fs_currpath, pszFileName);
  }

  /* convert to front slashes. */
  i=0; s=pszDestName; start=NULL;
  while((s = strchr(s+1,'\\'))) {
    if (!start) {
      start = s;
      continue;
    }
    {
      glob_t globbuf;
      char old1,old2,dest[256];
      int len,j,found;

      *start++ = '/';
      old1 = *start; *start++ = '*';
      old2 = *start; *start = 0;
      glob(pszDestName,GLOB_ONLYDIR,NULL,&globbuf);
      *start-- = old2; *start = old1;
      *s = 0;
      len = strlen(pszDestName);
      found = 0;
      for (j=0; j<globbuf.gl_pathc; j++) {
	if (!strncasecmp(globbuf.gl_pathv[j],pszDestName,len)) {
	  /* we found a matching name... */
	  sprintf(dest,"%s%c%s",globbuf.gl_pathv[j],'/',s+1);
	  strcpy(pszDestName,dest);
	  j = globbuf.gl_pathc;
	  found = 1;
	}
      }
      globfree(&globbuf);
      if (!found) {
	/* didn't find it. Let's try normal files (it might be a symlink) */
	*start++ = '*';
	*start = 0;
	glob(pszDestName,0,NULL,&globbuf);
	*start-- = old2; *start = old1;
	for (j=0; j<globbuf.gl_pathc; j++) {
	  if (!strncasecmp(globbuf.gl_pathv[j],pszDestName,len)) {
	    /* we found a matching name... */
	    sprintf(dest,"%s%c%s",globbuf.gl_pathv[j],'/',s+1);
	    strcpy(pszDestName,dest);
	    j = globbuf.gl_pathc;
	    found = 1;
	  }
	}
	globfree(&globbuf);
	if (!found) {           /* really nothing ! */
	  *s = '/';
	  fprintf(stderr,"no path for %s\n",pszDestName);
	}
      }
    }
    start = s;
  }

  if (!start) start = strrchr(pszDestName,'/'); // path already converted ?

  if (start) {
    *start++ = '/';     /* in case there was only 1 anti slash */
    if (*start && !strchr(start,'?') && !strchr(start,'*')) {
      /* We have a complete name after the path, not a wildcard */
      glob_t globbuf;
      char old1,old2,dest[256];
      int len,j,found;

      old1 = *start; *start++ = '*';
      old2 = *start; *start = 0;
      glob(pszDestName,0,NULL,&globbuf);
      *start-- = old2; *start = old1;
      len = strlen(pszDestName);
      found = 0;
      for (j=0; j<globbuf.gl_pathc; j++) {
	if (!strncasecmp(globbuf.gl_pathv[j],pszDestName,len)) {
	  /* we found a matching name... */
	  strcpy(pszDestName,globbuf.gl_pathv[j]);
	  j = globbuf.gl_pathc;
	  found = 1;
	}
      }
      if (!found) {
	/* It's often normal, the gem uses this to test for existence */
	/* of desktop.inf or newdesk.inf for example. */
	//fprintf(stderr,"didn't find filename %s\n",pszDestName);
      }
      globfree(&globbuf);
    }
  }

  // fprintf(stderr,"conv %s -> %s\n",pszFileName,pszDestName);
}


/*-----------------------------------------------------------------------*/
/*
  Covert from FindFirstFile/FindNextFile attribute to GemDOS format
*/
unsigned char GemDOS_ConvertAttribute(mode_t mode)
{
  unsigned char Attrib=0;

  /* FIXME: More attributes */
  if(S_ISDIR(mode)) Attrib |= GEMDOS_FILE_ATTRIB_SUBDIRECTORY;

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

/*-----------------------------------------------------------------------*/
/*
  GEMDOS Cauxin
  Call 0x3
*/
BOOL GemDOS_Cauxin(unsigned long Params)
{
  unsigned char Char;

  /* Wait here until a character is ready */
  while(!RS232_GetStatus());

  /* And read character */
  RS232_ReadBytes(&Char,1);
  Regs[REG_D0] = Char;

  return(TRUE);
}

/*-----------------------------------------------------------------------*/
/*
  GEMDOS Cauxout
  Call 0x4
*/
BOOL GemDOS_Cauxout(unsigned long Params)
{
  unsigned char Char;

  /* Send character to RS232 */
  Char = STMemory_ReadWord(Params+SIZE_WORD);
  RS232_TransferBytesTo(&Char,1);

  return(TRUE);
}

/*-----------------------------------------------------------------------*/
/*
  GEMDOS Cprnout
  Call 0x5
*/
BOOL GemDOS_Cprnout(unsigned long Params)
{
  unsigned char Char;

  /* Send character to printer(or file) */
  Char = STMemory_ReadWord(Params+SIZE_WORD);
  Printer_TransferByteTo(Char);
  Regs[REG_D0] = -1;                /* Printer OK */

  return(TRUE);
}

/*-----------------------------------------------------------------------*/
/*
  GEMDOS Set drive (0=A,1=B,2=C etc...)
  Call 0xE
*/
BOOL GemDOS_SetDrv(unsigned long Params)
{
  /* Read details from stack for our own use */
  CurrentDrive = STMemory_ReadWord(Params+SIZE_WORD);

  /* Still re-direct to TOS */
  return(FALSE);
}

/*-----------------------------------------------------------------------*/
/*
  GEMDOS Cprnos
  Call 0x11
*/
BOOL GemDOS_Cprnos(unsigned long Params)
{
  Regs[REG_D0] = -1;                /* Printer OK */

  return(TRUE);
}

/*-----------------------------------------------------------------------*/
/*
  GEMDOS Cauxis
  Call 0x12
*/
BOOL GemDOS_Cauxis(unsigned long Params)
{
  /* Read our RS232 state */
  if (RS232_GetStatus())
    Regs[REG_D0] = -1;              /* Chars waiting */
  else
    Regs[REG_D0] = 0;

  return(TRUE);
}

/*-----------------------------------------------------------------------*/
/*
  GEMDOS Cauxos
  Call 0x13
*/
BOOL GemDOS_Cauxos(unsigned long Params)
{
  Regs[REG_D0] = -1;                /* Device ready */

  return(TRUE);
}

/*-----------------------------------------------------------------------*/
/*
  GEMDOS Set Disc Transfer Address (DTA)
  Call 0x1A
*/
BOOL GemDOS_SetDTA(unsigned long Params)
{
  /* Look up on stack to find where DTA is! Store as PC pointer */
  pDTA = (DTA *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));

  return(FALSE);
}

/*-----------------------------------------------------------------------*/
/*
  GEMDOS Dfree Free disc space.
  Call 0x39
*/
BOOL GemDOS_DFree(unsigned long Params)
{
  int Drive;
  unsigned long Address;

  Address = (unsigned long)STMemory_ReadLong(Params+SIZE_WORD);
  Drive = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG);
  /* is it our drive? */
  if((Drive == 0 && CurrentDrive >= 2) || Drive >= 3){
    /* FIXME: Report actual free drive space */

    STMemory_WriteLong(Address,  10*2048);           /* free clusters (mock 10 Mb) */
    STMemory_WriteLong(Address+SIZE_LONG, 50*2048 ); /* total clusters (mock 50 Mb) */

    STMemory_WriteLong(Address+SIZE_LONG*2, 512 );   /* bytes per sector */
    STMemory_WriteLong(Address+SIZE_LONG*3, 1 );     /* sectors per cluster */
    return (TRUE);
  } else return(FALSE); /* redirect to TOS */
}

/*-----------------------------------------------------------------------*/
/*
  GEMDOS MkDir
  Call 0x39
*/
BOOL GemDOS_MkDir(unsigned long Params)
{
  char szDirPath[MAX_PATH];
  char *pDirName;
  int Drive;

  /* Find directory to make */
  pDirName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));

  Drive = GemDOS_IsFileNameAHardDrive(pDirName);

  if (ISHARDDRIVE(Drive)) {
    /* Copy old directory, as if calls fails keep this one */
    GemDOS_CreateHardDriveFileName(Drive,pDirName,szDirPath);

    /* Attempt to make directory */
    if ( mkdir(szDirPath, 0755)==0 )
      Regs[REG_D0] = GEMDOS_EOK;
    else
      Regs[REG_D0] = GEMDOS_EACCDN;        /* Access denied */

    return(TRUE);
  }
  return(FALSE);
}

/*-----------------------------------------------------------------------*/
/*
  GEMDOS RmDir
  Call 0x3A
*/
BOOL GemDOS_RmDir(unsigned long Params)
{
  char szDirPath[MAX_PATH];
  char *pDirName;
  int Drive;

  /* Find directory to make */
  pDirName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
  Drive = GemDOS_IsFileNameAHardDrive(pDirName);
  if (ISHARDDRIVE(Drive)) {
    /* Copy old directory, as if calls fails keep this one */
    GemDOS_CreateHardDriveFileName(Drive,pDirName,szDirPath);

    /* Attempt to make directory */
    if ( rmdir(szDirPath)==0 )
      Regs[REG_D0] = GEMDOS_EOK;
    else
      Regs[REG_D0] = GEMDOS_EACCDN;        /* Access denied */

    return(TRUE);
  }
  return(FALSE);
}

/*-----------------------------------------------------------------------*/
/*
  GEMDOS ChDir
  Call 0x3B
*/
BOOL GemDOS_ChDir(unsigned long Params)
{
  char szDirPath[MAX_PATH];
  char *pDirName;
  int Drive;
  int n;

  /* Find new directory */
  pDirName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));

  Drive = GemDOS_IsFileNameAHardDrive(pDirName);

  if (ISHARDDRIVE(Drive)) {

    char dest[256];
    struct stat buf;

    GemDOS_CreateHardDriveFileName(Drive,pDirName,szDirPath);
    if (stat(szDirPath,&buf)) { // error
      Regs[REG_D0] = GEMDOS_EPTHNF;
      return TRUE;
    }

    strcat(szDirPath, "/");

    /* remove any trailing slashes */
    if (szDirPath[strlen(szDirPath)-2]=='/')
      szDirPath[strlen(szDirPath)-1] = '\0';     /* then remove it! */

    strcpy(emudrives[0]->fs_currpath, szDirPath);
    Regs[REG_D0] = GEMDOS_EOK;
    return(TRUE);
  }

  return(FALSE);
}

/*-----------------------------------------------------------------------*/
/*
  GEMDOS Create file
  Call 0x3C
*/
BOOL GemDOS_Create(unsigned long Params)
{
  char szActualFileName[MAX_PATH];
  char *pszFileName;
  char *rwflags[] = { "w+", /* read / write (truncate if exists) */
		      "wb"  /* write only */
  };
  int Drive,Index,Mode;

  /* Find filename */
  pszFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
  Mode = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG);
  Drive = GemDOS_IsFileNameAHardDrive(pszFileName);
  if (ISHARDDRIVE(Drive)) {
    /* And convert to hard drive filename */
    GemDOS_CreateHardDriveFileName(Drive,pszFileName,szActualFileName);

    /* Find slot to store file handle, as need to return WORD handle for ST */
    Index = GemDOS_FindFreeFileHandle();
    if (Index==-1) {
      /* No free handles, return error code */
      Regs[REG_D0] = GEMDOS_ENHNDL;       /* No more handles */
      return(TRUE);
    }
    else {
#ifdef ENABLE_SAVING

      FileHandles[Index].FileHandle = fopen(szActualFileName, rwflags[Mode&0x01]);

      if (FileHandles[Index].FileHandle != NULL) {
        /* Tag handle table entry as used and return handle */
        FileHandles[Index].bUsed = TRUE;
        Regs[REG_D0] = Index+BASE_FILEHANDLE;  /* Return valid ST file handle from range 6 to 45! (ours start from 0) */
        return(TRUE);
      }
      else {
        Regs[REG_D0] = GEMDOS_EFILNF;     /* File not found */
        return(TRUE);
      }
#else
      Regs[REG_D0] = GEMDOS_EFILNF;       /* File not found */
      return(TRUE);
#endif
    }
  }

  return(FALSE);
}

/*-----------------------------------------------------------------------*/
/*
  GEMDOS Open file
  Call 0x3D
*/
BOOL GemDOS_Open(unsigned long Params)
{
  char szActualFileName[MAX_PATH];
  char *pszFileName;
  char *open_modes[] = { "rb", "wb", "r+" };  /* convert atari modes to stdio modes */
  int Drive,Index,Mode;

  /* Find filename */
  pszFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
  Mode = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG);
  Drive = GemDOS_IsFileNameAHardDrive(pszFileName);

  if (ISHARDDRIVE(Drive)) {
    /* And convert to hard drive filename */
    GemDOS_CreateHardDriveFileName(Drive,pszFileName,szActualFileName);
    /* Find slot to store file handle, as need to return WORD handle for ST  */
    Index = GemDOS_FindFreeFileHandle();
    if (Index == -1) {
      /* No free handles, return error code */
      Regs[REG_D0] = GEMDOS_ENHNDL;       /* No more handles */
      return(TRUE);
    }

    /* Open file */
    FileHandles[Index].FileHandle =  fopen(szActualFileName, open_modes[Mode&0x03]);

    sprintf(FileHandles[Index].szActualName,"%s",szActualFileName);

    if (FileHandles[Index].FileHandle != NULL) {
      /* Tag handle table entry as used and return handle */
      FileHandles[Index].bUsed = TRUE;
      Regs[REG_D0] = Index+BASE_FILEHANDLE;  /* Return valid ST file handle from range 6 to 45! (ours start from 0) */
      return(TRUE);
    }
    Regs[REG_D0] = GEMDOS_EFILNF;     /* File not found/ error opening */
    return(TRUE);
  }

  return(FALSE);
}

/*-----------------------------------------------------------------------*/
/*
  GEMDOS Close file
  Call 0x3E
*/
BOOL GemDOS_Close(unsigned long Params)
{
  int Handle;

  /* Find our handle - may belong to TOS */
  Handle = STMemory_ReadWord(Params+SIZE_WORD)-BASE_FILEHANDLE;

  /* Check handle was valid */
  if (GemDOS_IsInvalidFileHandle(Handle)) {
    /* No assume was TOS */
    return(FALSE);
  }
  else {
    /* Close file and free up handle table */
    fclose(FileHandles[Handle].FileHandle);
    FileHandles[Handle].bUsed = FALSE;
    /* Return no error */
    Regs[REG_D0] = GEMDOS_EOK;
    return(TRUE);
  }
}

/*-----------------------------------------------------------------------*/
/*
  GEMDOS Read file
  Call 0x3F
*/
BOOL GemDOS_Read(unsigned long Params)
{
  char *pBuffer;
  unsigned long nBytesRead,Size,CurrentPos,FileSize;
  long nBytesLeft;
  int Handle;

  /* Read details from stack */
  Handle = STMemory_ReadWord(Params+SIZE_WORD)-BASE_FILEHANDLE;
  Size = STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD);
  pBuffer = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD+SIZE_LONG));

  /* Check handle was valid */
  if (GemDOS_IsInvalidFileHandle(Handle)) {
    /* No -  assume was TOS */
    return(FALSE);
  }
  else {

    /* To quick check to see where our file pointer is and how large the file is */
    CurrentPos = ftell(FileHandles[Handle].FileHandle);
    fseek(FileHandles[Handle].FileHandle, 0, SEEK_END);
    FileSize = ftell(FileHandles[Handle].FileHandle);
    fseek(FileHandles[Handle].FileHandle, CurrentPos, SEEK_SET);

    nBytesLeft = FileSize-CurrentPos;

    /* Check for End Of File */
    if (nBytesLeft == 0) {
      /* FIXME: should we return zero (bytes read) or an error? */
       Regs[REG_D0] = 0;
      return(TRUE);
    }
    else {
      /* Limit to size of file to prevent windows error */
      if (Size>FileSize)
        Size = FileSize;
      /* And read data in */
      nBytesRead = fread(pBuffer, 1, Size, FileHandles[Handle].FileHandle);

      /* Return number of bytes read */
      Regs[REG_D0] = nBytesRead;

      return(TRUE);
    }
  }
}

/*-----------------------------------------------------------------------*/
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
  /* Read details from stack */
  Handle = STMemory_ReadWord(Params+SIZE_WORD)-BASE_FILEHANDLE;
  Size = STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD);
  pBuffer = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD+SIZE_LONG));

  /* Check handle was valid */
  if (GemDOS_IsInvalidFileHandle(Handle)) {
    /* No assume was TOS */
    return(FALSE);
  }
  else {

    nBytesWritten = fwrite(pBuffer, 1, Size, FileHandles[Handle].FileHandle);
    if (nBytesWritten>=0) {

      Regs[REG_D0] = nBytesWritten;      /* OK */
    }
    else
      Regs[REG_D0] = GEMDOS_EACCDN;      /* Access denied(ie read-only) */

    return(TRUE);
  }
#endif

  return(FALSE);
}

/*-----------------------------------------------------------------------*/
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

  /* Find filename */
  pszFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
  Drive = GemDOS_IsFileNameAHardDrive(pszFileName);
  if (ISHARDDRIVE(Drive)) {
    /* And convert to hard drive filename */
    GemDOS_CreateHardDriveFileName(Drive,pszFileName,szActualFileName);

    /* Now delete file?? */
    if ( unlink(szActualFileName)==0 )
      Regs[REG_D0] = GEMDOS_EOK;          /* OK */
    else
      Regs[REG_D0] = GEMDOS_EFILNF;       /* File not found */

    return(TRUE);
  }
#endif

  return(FALSE);
}

/*-----------------------------------------------------------------------*/
/*
  GEMDOS File seek
  Call 0x42
*/
BOOL GemDOS_LSeek(unsigned long Params)
{
  long Offset;
  int Handle,Mode;

  /* Read details from stack */
  Offset = (long)STMemory_ReadLong(Params+SIZE_WORD);
  Handle = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG)-BASE_FILEHANDLE;
  Mode = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_WORD);

  /* Check handle was valid */
  if (GemDOS_IsInvalidFileHandle(Handle)) {
    /* No assume was TOS */
    return(FALSE);
  }
  else {
    /* Return offset from start of file */
    fseek(FileHandles[Handle].FileHandle, Offset, Mode);
    Regs[REG_D0] = ftell(FileHandles[Handle].FileHandle);
    return(TRUE);
  }
}

/*-----------------------------------------------------------------------*/
/*
  GEMDOS Get Directory
  Call 0x47
*/
int GemDOS_GetDir(unsigned long Params){
  unsigned long Address;
  unsigned short Drive;

  Address = (long)STMemory_ReadLong(Params+SIZE_WORD);
  Drive = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG);
  /* is it our drive? */
  if((Drive == 0 && CurrentDrive >= 2) || Drive >= 3){
    STMemory_WriteByte(Address, '\0' );
    Regs[REG_D0] = GEMDOS_EOK;          /* OK */
    return(TRUE);
  } else return(FALSE);
}

/*-----------------------------------------------------------------------*/
/*
  PExec Load And Go - Redirect to cart' routine at address 0xFA1000

  If loading from hard-drive(ie drive ID 2 or more) set condition codes to run own GEMDos routines
*/
int GemDOS_Pexec_LoadAndGo(unsigned long Params)
{
  /* add multiple disk support here too */
  /* Hard-drive? */
  if( CurrentDrive == emudrives[0]->hd_letter )
    {
      /* If not using A: or B:, use my own routines to load */
      return(CALL_PEXEC_ROUTINE);
    }
  else return(FALSE);
}

/*-----------------------------------------------------------------------*/
/*
  PExec Load But Don't Go - Redirect to cart' routine at address 0xFA1000
*/
int GemDOS_Pexec_LoadDontGo(unsigned long Params)
{
  /* Hard-drive? */
  if( CurrentDrive == emudrives[0]->hd_letter )
    {
      return(CALL_PEXEC_ROUTINE);
    } else return(FALSE);
}

/*-----------------------------------------------------------------------*/
/*
  GEMDOS PExec handler
  Call 0x4B
*/
int GemDOS_Pexec(unsigned long Params)
{
  unsigned short int Mode;

  /* Find PExec mode */
  Mode = STMemory_ReadWord(Params+SIZE_WORD);

  /* Re-direct as needed */
  switch(Mode) {
  case 0:      /* Load and go */
    return(GemDOS_Pexec_LoadAndGo(Params));
  case 3:      /* Load, don't go */
    return(GemDOS_Pexec_LoadDontGo(Params));
  case 4:      /* Just go */
    return(FALSE);
  case 5:      /* Create basepage */
    return(FALSE);
  case 6:
    return(FALSE);

  default:
    return(FALSE);
  }

  /* Still re-direct to TOS */
  return(FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  GEMDOS Search Next
  Call 0x4F
*/
BOOL GemDOS_SNext(unsigned long Params)
{
  struct dirent **temp;
  int Index;

  /* Was DTA ours or TOS? */
  if (STMemory_ReadLong_PCSpace(pDTA->magic)==DTA_MAGIC_NUMBER) {

    /* Find index into our list of structures */
    Index = STMemory_ReadWord_PCSpace(pDTA->index)&(MAX_DTAS_FILES-1);

    if(InternalDTAs[Index].centry >= InternalDTAs[Index].nentries){
      Regs[REG_D0] = GEMDOS_ENMFIL;    /* No more files */
      return(TRUE);
    }

    temp = InternalDTAs[Index].found;
    if(PopulateDTA(InternalDTAs[Index].path, temp[InternalDTAs[Index].centry++]) == FALSE){
      fprintf(stderr,"\tError setting DTA.\n");
      return(TRUE);
    }

    Regs[REG_D0] = GEMDOS_EOK;
    return(TRUE);
  }

  return(FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  GEMDOS Find first file
  Call 0x4E
*/
BOOL GemDOS_SFirst(unsigned long Params)
{
  char szActualFileName[MAX_PATH];
  char tempstr[MAX_PATH];
  char *pszFileName;
  struct dirent **files;
  unsigned short int Attr;
  int Drive;
  DIR *fsdir;
  int i,j,k;

  /* Find filename to search for */
  pszFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
  Attr = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG);

  Drive = GemDOS_IsFileNameAHardDrive(pszFileName);
  if (ISHARDDRIVE(Drive)) {

    /* And convert to hard drive filename */
    GemDOS_CreateHardDriveFileName(Drive,pszFileName,szActualFileName);

    /* Populate DTA, set index for our use */
    STMemory_WriteWord_PCSpace(pDTA->index,DTAIndex);
    STMemory_WriteLong_PCSpace(pDTA->magic,DTA_MAGIC_NUMBER); /* set our dta magic num */

    if(InternalDTAs[DTAIndex].bUsed == TRUE) ClearInternalDTA();
    InternalDTAs[DTAIndex].bUsed = TRUE;

    /* Were we looking for the volume label? */
    if (Attr&GEMDOS_FILE_ATTRIB_VOLUME_LABEL) {
      /* Volume name */
      strcpy(pDTA->dta_name,"EMULATED.001");
      Regs[REG_D0] = GEMDOS_EOK;          /* Got volume */
      return(TRUE);
    }

    /* open directory */
    fsfirst_dirname(szActualFileName, InternalDTAs[DTAIndex].path);
    fsdir = opendir(InternalDTAs[DTAIndex].path);

    if( fsdir == NULL ){
      Regs[REG_D0] = GEMDOS_EPTHNF;        /* Path not found */
      return(TRUE);
    }
    /* close directory */
    closedir( fsdir );

    InternalDTAs[DTAIndex].nentries = scandir(InternalDTAs[DTAIndex].path, &files, 0, alphasort);
    if( InternalDTAs[DTAIndex].nentries < 0 ){
      Regs[REG_D0] = GEMDOS_EFILNF;        /* File (directory actually) not found */
      return(TRUE);
    }

    InternalDTAs[DTAIndex].centry = 0;        /* current entry is 0 */
    fsfirst_dirmask(szActualFileName, tempstr); /* get directory mask */

    /* Create and populate a list of matching files. */

    j = 0;                     /* count number of entries matching mask */
    for(i=0;i<InternalDTAs[DTAIndex].nentries;i++)
      if(match(tempstr, files[i]->d_name)) j++;

    InternalDTAs[DTAIndex].found = (struct dirent **)malloc(sizeof(struct dirent *) * j);

    /* copy the dirent pointers for files matching the mask to our list */
    k = 0;
    for(i=0;i<InternalDTAs[DTAIndex].nentries;i++)
      if(match(tempstr, files[i]->d_name)){
	InternalDTAs[DTAIndex].found[k] = files[i];
	k++;
      }

    InternalDTAs[DTAIndex].nentries = j; /* set number of legal entries */

    if(InternalDTAs[DTAIndex].nentries == 0){
      /* No files of that match, return error code */
      Regs[REG_D0] = GEMDOS_EFILNF;        /* File not found */
      return(TRUE);
    }

    /* Scan for first file (SNext uses no parameters) */
    GemDOS_SNext(0);
    /* increment DTA index */
    DTAIndex++;
    DTAIndex&=(MAX_DTAS_FILES-1);

    return(TRUE);
  }
  return(FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  GEMDOS Rename
  Call 0x56
*/
BOOL GemDOS_Rename(unsigned long Params)
{
  char *pszNewFileName,*pszOldFileName;
  char szNewActualFileName[MAX_PATH],szOldActualFileName[MAX_PATH];
  int NewDrive, OldDrive;

  /* Read details from stack */
  pszOldFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD));
  pszNewFileName = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD+SIZE_LONG));

  NewDrive = GemDOS_IsFileNameAHardDrive(pszNewFileName);
  OldDrive = GemDOS_IsFileNameAHardDrive(pszOldFileName);
  if (ISHARDDRIVE(NewDrive) && ISHARDDRIVE(OldDrive)) {
    /* And convert to hard drive filenames */
    GemDOS_CreateHardDriveFileName(NewDrive,pszNewFileName,szNewActualFileName);
    GemDOS_CreateHardDriveFileName(OldDrive,pszOldFileName,szOldActualFileName);

    /* Rename files */
    if ( rename(szOldActualFileName,szNewActualFileName)==0 )
      Regs[REG_D0] = GEMDOS_EOK;
    else
      Regs[REG_D0] = GEMDOS_EACCDN;        /* Access denied */
    return(TRUE);
  }

  return(FALSE);
}

/*-----------------------------------------------------------------------*/
/*
  GEMDOS GSDToF
  Call 0x57
*/
BOOL GemDOS_GSDToF(unsigned long Params)
{
  DATETIME DateTime;
  unsigned long pBuffer;
  int Handle,Flag;

  /* Read details from stack */
  pBuffer = STMemory_ReadLong(Params+SIZE_WORD);
  Handle = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG)-BASE_FILEHANDLE;
  Flag = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_LONG);

  /* Check handle was valid */
  if (GemDOS_IsInvalidFileHandle(Handle)) {
    /* No assume was TOS */
    return(FALSE);
  }

  /* Set time/date stamp? Do nothing. */
  if( Flag == 1 ){
    Regs[REG_D0] = GEMDOS_EOK;
    return (TRUE);
  }

  Regs[REG_D0] = GEMDOS_ERROR;  /* Invalid parameter */

  if (GetFileInformation(FileHandles[Handle].szActualName, &DateTime) == TRUE){
    STMemory_WriteWord(pBuffer, DateTime.word1);
    STMemory_WriteWord(pBuffer+2, DateTime.word2);
    Regs[REG_D0] = GEMDOS_EOK;
  }
  return (TRUE);
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
  short RunOld;


  /* Read SReg from stack to see if parameters are on User or Super stack  */
  MakeSR();  /* update value of SR */
  CallingSReg = STMemory_ReadWord(Regs[REG_A7]);
  if ((CallingSReg&SR_SUPERMODE)==0)      /* Calling from user mode */
    Params = regs.usp;
  else {
    Params = Regs[REG_A7]+SIZE_WORD+SIZE_LONG;  /* super stack */
    if( cpu_level>0 )
      Params += SIZE_WORD;   /* Skip extra word whe CPU is >=68010 */
  }

  /* Default to run TOS GemDos (SR_NEG run Gemdos, SR_ZERO already done, SR_OVERFLOW run own 'Pexec' */
  RunOld = TRUE;
  SR &= SR_CLEAR_OVERFLOW;
  SR &= SR_CLEAR_ZERO;
  SR |= SR_NEG;

  /* Find pointer to call parameters */
  GemDOSCall = STMemory_ReadWord(Params);

#ifdef GEMDOS_VERBOSE
  if(GemDOSCall <= 0x57)
    fprintf(stderr, "GemDOS 0x%X (%s)\n",GemDOSCall,pszGemDOSNames[GemDOSCall]);
  if(!GemDOSCall){
    fprintf(stderr, "Warning!!\n");
    DebugUI();
  }
#endif

  /* Intercept call */
  switch(GemDOSCall) {
/*    case 0x3: */
/*      if (GemDOS_Cauxin(Params)) */
/*        RunOld = FALSE; */
/*      break; */
/*    case 0x4: */
/*      if (GemDOS_Cauxout(Params)) */
/*        RunOld = FALSE; */
/*      break; */
/*    case 0x5: */
/*      if (GemDOS_Cprnout(Params)) */
/*        RunOld = FALSE; */
/*      break; */
  case 0xe:
    if (GemDOS_SetDrv(Params))
      RunOld = FALSE;
    break;
/*    case 0x11: */
/*        if (GemDOS_Cprnos(Params)) */
/*          RunOld = FALSE; */
/*        break; */
/*      case 0x12: */
/*        if (GemDOS_Cauxis(Params)) */
/*          RunOld = FALSE; */
/*        break; */
/*      case 0x13: */
/*        if (GemDOS_Cauxos(Params)) */
/*          RunOld = FALSE; */
/*        break; */
    case 0x1a:
      if (GemDOS_SetDTA(Params))
       RunOld = FALSE;
      break;
    case 0x36:
      if (GemDOS_DFree(Params))
        RunOld = FALSE;
      break;
    case 0x39:
      if (GemDOS_MkDir(Params))
        RunOld = FALSE;
      break;
    case 0x3a:
      if (GemDOS_RmDir(Params))
        RunOld = FALSE;
      break;
    case 0x3b:
      if (GemDOS_ChDir(Params))
        RunOld = FALSE;
      break;
    case 0x3c:
      if (GemDOS_Create(Params))
        RunOld = FALSE;
      break;
    case 0x3d:
      if (GemDOS_Open(Params))
        RunOld = FALSE;
      break;
    case 0x3e:
      if (GemDOS_Close(Params))
        RunOld = FALSE;
      break;
    case 0x3f:
      if (GemDOS_Read(Params))
        RunOld = FALSE;
      break;
    case 0x40:
      if (GemDOS_Write(Params))
        RunOld = FALSE;
      break;
    case 0x41:
      if (GemDOS_UnLink(Params))
        RunOld = FALSE;
      break;
    case 0x42:
      if (GemDOS_LSeek(Params))
        RunOld = FALSE;
      break;
    case 0x47:
      if (GemDOS_GetDir(Params))
        RunOld = FALSE;
      break;
    case 0x4b:
      if(GemDOS_Pexec(Params) == CALL_PEXEC_ROUTINE);
      RunOld = CALL_PEXEC_ROUTINE;
      break;
    case 0x4e:
      if (GemDOS_SFirst(Params))
	RunOld = FALSE;
      break;
    case 0x4f:
      if (GemDOS_SNext(Params))
        RunOld = FALSE;
      break;
    case 0x56:
      if (GemDOS_Rename(Params))
        RunOld = FALSE;
      break;
    case 0x57:
      if (GemDOS_GSDToF(Params))
        RunOld = FALSE;
      break;
  }

  switch(RunOld){
  case FALSE:       /* skip over branch to pexec to RTE */
    SR |= SR_ZERO;
    break;
  case CALL_PEXEC_ROUTINE:   /* branch to pexec, then redirect to old gemdos. */
    SR |= SR_OVERFLOW;
    break;
  }

  MakeFromSR();  /* update the flags from the SR register */
}

/*-----------------------------------------------------------------------*/
/*
  GemDOS_Boot - routine called on the first occurence of the gemdos opcode.
  (this should be in the cartridge bootrom)
  Sets up our gemdos handler (or, if we don't need one, just turn off keyclicks)
 */

void GemDOS_Boot()
{

  bInitGemDOS = TRUE;
#ifdef GEMDOS_VERBOSE
  fprintf(stderr, "Gemdos_Boot()\n");
#endif
  /* install our gemdos handler, if -e or --harddrive option used */
  if(GEMDOS_EMU_ON){

    /* Patch pexec code - coded value is 4, but must be 6 for TOS > 1.00 */
    if(TosVersion > 0x0100)
      STMemory_WriteByte(CART_PEXEC_TOS, 0x06);

    /* Save old GEMDOS handler adress */
    STMemory_WriteLong(CART_OLDGEMDOS, STMemory_ReadLong(0x0084));
    /* Setup new GEMDOS handler, see cartimg.c */
    STMemory_WriteLong(0x0084, CART_GEMDOS);
  }
}

/*
  GemDOS_RunOldOpCode()
  Has been relocated to a routine in hatari-glue.c
*/


