/*
  Hatari

  Create Blank .ST/.MSA Disc Images
*/

#include "main.h"
#include "dialog.h"
#include "file.h"
#include "floppy.h"
#include "memAlloc.h"
#include "misc.h"
#include "msa.h"
#include "st.h"


/*-----------------------------------------------------------------------*/
/*
           40 track SS   40 track DS   80 track SS   80 track DS
 0- 1   Branch instruction to boot program if executable
 2- 7   'Loader'
 8-10   24-bit serial number
11-12   BPS    512           512           512           512
13      SPC     1             2             2             2
14-15   RES     1             1             1             1
16      FAT     2             2             2             2
17-18   DIR     64           112           112           112
19-20   SEC    360           720           720          1440
21      MEDIA  252           253           248           249 (isn't used by ST-BIOS)
22-23   SPF     2             2             5             5
24-25   SPT     9             9             9             9
26-27   SIDE    1             2             1             2
28-29   HID     0             0             0             0
510-511 CHECKSUM
*/

typedef struct {
  int nTracks,nSectors,nSides;
} STANDARDDISCSIZES;

#define DEFAULT_STANDARDDISC  1
#define MAX_STANDARDDISCSIZES  2
static STANDARDDISCSIZES StandardDiscSizes[MAX_STANDARDDISCSIZES] = {
  80,9,1,  /* 80 tracks, single sided (360k) */
  80,9,2   /* 80 tracks, double sided (720k) */
};
static char *pszStandardDiscNames[] = {
  "80 tracks, single sided (360k)",
  "80 tracks, double sided (720k)",
  NULL  /* term */
};

#define NUM_TRACKSTEXTS    3
#define NUM_SECTORSTEXTS  3
#define NUM_SIDESTEXTS    2
static char *pszTracksText[] = {
  "80","81","82",NULL
};
static char *pszSectorsText[] = {
  "9","10","11",NULL
};
static char *pszSidesText[] = {
  "1","2",NULL
};

/* FIXME
static int Custom_DialogItems[] = {
  IDC_STATICTRACKS,
  IDC_STATICSECTORS,
  IDC_STATICSIDES,
  IDC_EDITTRACKS,
  IDC_EDITSECTORS,
  IDC_EDITSIDES,
  IDC_SPINTRACKS,
  IDC_SPINSECTORS,
  IDC_SPINSIDES,
  IDC_STATICSIZE,
  0,  //term
};
*/

static int CustomTracks=0,CustomSectors=0,CustomSides=1;    /* Default settings, 80 tracks, 9 sectors, 2 sides */
static BOOL bInsertIntoDrive=FALSE;                         /* Insert disc image into drive when complete? */
static BOOL bCustomDiscImage=FALSE;                         /* Is custom or standard disc size? */
static int ChosenDiscType;                                  /* Index into StandardDiscSizes[] of chosen image type */
static int ChosenDiscDrive;                                 /* Drive we are making disc image in, eg 0=A:, 1=B: */
static char CreateBlankImageFileName[MAX_FILENAME_LENGTH];


/*-----------------------------------------------------------------------*/
/*
  Create .ST/.MSA disc image according to 'Tracks,Sector,Sides' and save as filename
*/
void CreateBlankImage_CreateFile(/*HWND hDlg,*/char *pszFileName,int nTracks, int nSectors, int nSides, BOOL bInsertIntoDrive)
{
  unsigned char *pDiscFile;
  unsigned long DiscSize;
  unsigned short int SPC,DIR,MEDIA,SPF;
  char szString[MAX_FILENAME_LENGTH];
  BOOL bRet=FALSE;

  /* Calculate size of disc image */
  DiscSize = nTracks * (nSectors*NUMBYTESPERSECTOR) * nSides;
  /* Allocate space for our 'file', and blank */
  pDiscFile = (unsigned char *)Memory_Alloc(DiscSize);
  Memory_Clear(pDiscFile,DiscSize);

  /* Fill in boot-sector, this would better as a structure but 'C' pads the variables out */
  Memory_Set(pDiscFile+2,0x4e,6);                           /* 2-7 'Loader' */
  *(unsigned char *)(pDiscFile+8) = rand();                 /* 8-10 24-bit serial number */
  *(unsigned char *)(pDiscFile+9) = rand();
  *(unsigned char *)(pDiscFile+10) = rand();
  *(unsigned short int *)(pDiscFile+11) = NUMBYTESPERSECTOR;  /* 11-12 BPS */
  if ( (nTracks==40) && (nSides==1) ) SPC = 1;
  else SPC = 2;
  *(unsigned char *)(pDiscFile+13) = SPC;                   /* 13 SPC */
  *(unsigned short int *)(pDiscFile+14) = 1;                /* 14-15 RES */
  *(unsigned char *)(pDiscFile+16) = 2;                     /* 16 FAT */
  if (SPC==1) DIR = 64;
  else DIR = 112;
  *(unsigned short int *)(pDiscFile+17) = DIR;              /* 17-18 DIR */
  *(unsigned short int *)(pDiscFile+19) = nTracks*nSectors*nSides;  /* 19-20 SEC */
  if (nTracks==40) MEDIA = 252;
  else MEDIA = 248;
  if (nSides==2) MEDIA++;
  *(unsigned char *)(pDiscFile+21) = MEDIA;                 /* 21 MEDIA */
  if (nTracks>=80) SPF = 5;
  else SPF = 2;
  *(unsigned short int *)(pDiscFile+22) = SPF;              /* 22-23 SPF */
  *(unsigned short int *)(pDiscFile+24) = nSectors;         /* 24-25 SPT */
  *(unsigned short int *)(pDiscFile+26) = nSides;           /* 26-27 SIDE */
  *(unsigned short int *)(pDiscFile+28) = 0;                /* 28-29 HID */

  /* Ask if OK to overwrite, if exists? */
  if (File_QueryOverwrite(/*hDlg,*/pszFileName)) {
    /* Save image to file, as .ST or compressed .MSA */
    if (File_FileNameIsMSA(pszFileName))
      bRet = MSA_WriteDisc(pszFileName,pDiscFile,DiscSize);
    else if (File_FileNameIsST(pszFileName))
      bRet = ST_WriteDisc(pszFileName,pDiscFile,DiscSize);

    /* Did create successfully? */
    if (bRet) {
      /* Say OK, */
//      MessageBox(hDlg,"Disc image created successfully.",PROG_NAME,MB_OK | MB_ICONINFORMATION);
      /* Insert into drive A: or B: ? */
      if (bInsertIntoDrive)
        Floppy_InsertDiscIntoDrive(ChosenDiscDrive,pszFileName);
    }
    else {
      /* Warn user we were unable to create image */
      sprintf(szString,"Unable to create disc image '%s'.",pszFileName);
//      MessageBox(hDlg,szString,PROG_NAME,MB_OK | MB_ICONSTOP);
    }
  }

  /* Free image */
  Memory_Free(pDiscFile);
}


/*-----------------------------------------------------------------------*/
/*
  Enable dialog items according to choice of standard or custom size
*/
void CreateBlankImage_EnableDialog(/*HWND hDlg,*/BOOL bState)
{
  /* Standard disc size */
//  Dialog_EnableItem(hDlg,IDC_IMAGECOMBO,bState);

  /* Custom disc size */
//  Dialog_EnableItems(hDlg,Custom_DialogItems,!bState);
}


/*-----------------------------------------------------------------------*/
/*
  Find number of Tracks,Sectors and Sides from chosen dialog settings
*/
void CreateBlankImage_FindTracksSectorsSides(int *nTracks, int *nSectors, int *nSides, BOOL bCustom, int ChosenDiscType)
{
  if (bCustom) {
    *nTracks = atoi(pszTracksText[CustomTracks]);
    *nSectors = atoi(pszSectorsText[CustomSectors]);
    *nSides = atoi(pszSidesText[CustomSides]);
  }
  else {
    *nTracks = StandardDiscSizes[ChosenDiscType].nTracks;
    *nSectors = StandardDiscSizes[ChosenDiscType].nSectors;
    *nSides = StandardDiscSizes[ChosenDiscType].nSides;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Show size of custom disc in dialog
*/
void CreateBlankImage_ShowDiscImageCapacity(/*HWND hDlg*/)
{
  char szString[256];
  int DiscSize,nTracks,nSectors,nSides;

  /* Find size of disc image */
  CreateBlankImage_FindTracksSectorsSides(&nTracks,&nSectors,&nSides,TRUE,ChosenDiscType);
  DiscSize = nTracks*nSectors*nSides*NUMBYTESPERSECTOR;

  sprintf(szString,"Capactity: %dk (%d bytes)", DiscSize/1024,DiscSize);
//  Dialog_SetText(hDlg,IDC_STATICSIZE,szString);
}


/*-----------------------------------------------------------------------*/
/*
  Set dialog edit box details for Tracks,Sectors and Sides
*/

void CreateBlankImage_SetDiscImageTracksDetail(/*HWND hDlg,*/int NewCustomTracks)
{
//  Dialog_SetText(hDlg,IDC_EDITTRACKS,pszTracksText[CustomTracks=NewCustomTracks]);
//  CreateBlankImage_ShowDiscImageCapacity(hDlg);
}

void CreateBlankImage_SetDiscImageSectorsDetail(/*HWND hDlg,*/int NewCustomSectors)
{
//  Dialog_SetText(hDlg,IDC_EDITSECTORS,pszSectorsText[CustomSectors=NewCustomSectors]);
//  CreateBlankImage_ShowDiscImageCapacity(hDlg);
}

void CreateBlankImage_SetDiscImageSidesDetail(/*HWND hDlg,*/int NewCustomSides)
{
//  Dialog_SetText(hDlg,IDC_EDITSIDES,pszSidesText[CustomSides=NewCustomSides]);
//  CreateBlankImage_ShowDiscImageCapacity(hDlg);
}


/*-----------------------------------------------------------------------*/
/*
  Handle create disc dialog messages
*/
BOOL CreateBlankImage_DiscImageDialog(/*HWND hDlg, UINT Message, WPARAM wParam, LPARAM lParam*/)
{
#if 0
  NMUPDOWN *pUpDown;
  char szString[MAX_FILENAME_LENGTH];
  int nTracks,nSectors,nSides;
  int Offset;

  switch(Message) {
    case WM_INITDIALOG:
      // Set Insert into drive checkbox
      Dialog_SetButton(hDlg,IDC_CHECK_INSERTINTODRIVE,bInsertIntoDrive);
      // Fill in combobox
      Dialog_SetComboBoxItems(hDlg,IDC_IMAGECOMBO,pszStandardDiscNames,DEFAULT_STANDARDDISC);
      // And select, defaults to 720k disc image size
      ChosenDiscType = DEFAULT_STANDARDDISC;  // 80 tracks, 9 sectors, 2 sides 720k
      // Set default choice
      Dialog_SetButton(hDlg,IDC_STDDISCSIZERADIO,!bCustomDiscImage);
      Dialog_SetButton(hDlg,IDC_CUSTOMDISCSIZERADIO,bCustomDiscImage);
      // Set custom spin items
      Dialog_SetSpinList(hDlg,IDC_EDITTRACKS,IDC_SPINTRACKS,pszTracksText,NUM_TRACKSTEXTS,CustomTracks);
      Dialog_SetSpinList(hDlg,IDC_EDITSECTORS,IDC_SPINSECTORS,pszSectorsText,NUM_SECTORSTEXTS,CustomSectors);
      Dialog_SetSpinList(hDlg,IDC_EDITSIDES,IDC_SPINSIDES,pszSidesText,NUM_SIDESTEXTS,CustomSides);

      CreateBlankImage_ShowDiscImageCapacity(hDlg);
      CreateBlankImage_EnableDialog(hDlg,!bCustomDiscImage);
      return(TRUE);
    
    case WM_NOTIFY:
      switch(wParam) {
        // Handle spin controls
        case IDC_SPINTRACKS:
          pUpDown = (NMUPDOWN *)lParam;
          Offset = Misc_LimitInt(pUpDown->iPos + pUpDown->iDelta, 0,NUM_TRACKSTEXTS-1);
          Dialog_UpdateSpinList(hDlg,IDC_EDITTRACKS,pszTracksText,NUM_TRACKSTEXTS,Offset);
          CreateBlankImage_SetDiscImageTracksDetail(hDlg,Offset);
          return(TRUE);
        case IDC_SPINSECTORS:
          pUpDown = (NMUPDOWN *)lParam;
          Offset = Misc_LimitInt(pUpDown->iPos + pUpDown->iDelta, 0,NUM_SECTORSTEXTS-1);
          Dialog_UpdateSpinList(hDlg,IDC_EDITSECTORS,pszSectorsText,NUM_SECTORSTEXTS,Offset);
          CreateBlankImage_SetDiscImageSectorsDetail(hDlg,Offset);
          return(TRUE);
        case IDC_SPINSIDES:
          pUpDown = (NMUPDOWN *)lParam;
          Offset = Misc_LimitInt(pUpDown->iPos + pUpDown->iDelta, 0,NUM_SIDESTEXTS-1);
          Dialog_UpdateSpinList(hDlg,IDC_EDITSIDES,pszSidesText,NUM_SIDESTEXTS,Offset);
          CreateBlankImage_SetDiscImageSidesDetail(hDlg,Offset);
          return(TRUE);
      }

      break;

    case WM_COMMAND:
      switch(wParam) {
        case IDC_STDDISCSIZERADIO:
        case IDC_CUSTOMDISCSIZERADIO:
          bCustomDiscImage ^= TRUE;
          CreateBlankImage_EnableDialog(/*hDlg,*/!bCustomDiscImage);
          return(TRUE);
        case IDC_IMAGECOMBO:
          return(TRUE);

        case IDOK:
          // Read back dialog choices
          bInsertIntoDrive = Dialog_ReadButton(hDlg,IDC_CHECK_INSERTINTODRIVE);
          bCustomDiscImage = Dialog_ReadButton(hDlg,IDC_CUSTOMDISCSIZERADIO);
          // Get type of disc(for standard size)
          ChosenDiscType = Dialog_GetSelectedComboBoxItem(hDlg,IDC_IMAGECOMBO);
          // Find disc details,(if chosen or custom type)
          CreateBlankImage_FindTracksSectorsSides(&nTracks,&nSectors,&nSides,bCustomDiscImage,ChosenDiscType);
          // Confirm with user and create image
          sprintf(szString,"Create disc image '%s'\n(%d Track, %d Sectors, %d Sides) ?",CreateBlankImageFileName,nTracks,nSectors,nSides);
          if (MessageBox(hDlg,szString,PROG_NAME,MB_YESNO | MB_ICONQUESTION)==IDYES)
            CreateBlankImage_CreateFile(hDlg,CreateBlankImageFileName,nTracks,nSectors,nSides,bInsertIntoDrive);
        case IDCANCEL:
          EndDialog(hDlg,0);
          return(TRUE);
        }
      break;
  }

  return(FALSE);
#endif
}


/*-----------------------------------------------------------------------*/
/*
  Create output disassembly dialog
*/
BOOL CreateBlankImage_DoDialog(/*HWND hDlg,*/int Drive,char *pszFileName)
{
//  PROC lpfnDlgProc;
 
  /* Is filename valid, ie .ST or .MSA? */
  if (File_FileNameIsST(pszFileName) || File_FileNameIsMSA(pszFileName)) {
    /* Store drive for later(when create image) */
    ChosenDiscDrive = Drive;
    /* Store filename for later */
    strcpy(CreateBlankImageFileName,pszFileName);
    /* Open dialog */
//    lpfnDlgProc = MakeProcInstance((PROC)CreateBlankImage_DiscImageDialog,hInst);
//    DialogBoxParam(hInst,MAKEINTRESOURCE(IDD_DIALOG_BLANKIMAGE),hDlg,(DLGPROC)lpfnDlgProc,NULL);
    return(TRUE);
  }
  else
//    MessageBox(hWnd,"Invalid disc filename. Please re-enter.",PROG_NAME,MB_OK | MB_ICONSTOP);

  return(FALSE);
}
