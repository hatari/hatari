/*
  Hatari

  This is normal 'C' code to handle our options dialog. We keep all our configuration details
  in a variable 'ConfigureParams'. When we open our dialog we copy this and then when we 'OK'
  or 'Cancel' the dialog we can compare and makes the necessary changes. As the 'ConfigureParams'
  is always up to date this is where we load/save data to the registry.
*/

#include "main.h"
#include "configuration.h"
#include "audio.h"
#include "debug.h"
#include "dialog.h"
#include "file.h"
#include "floppy.h"
#include "reset.h"
#include "joy.h"
#include "keymap.h"
#include "m68000.h"
#include "memAlloc.h"
#include "memorySnapShot.h"
#include "misc.h"
#include "printer.h"
#include "rs232.h"
#include "screen.h"
#include "sound.h"
#include "tos.h"
#include "vdi.h"
#include "video.h"
#include "view.h"



DLG_PARAMS ConfigureParams,DialogParams;    /* List of configuration for system and dialog(so can choose 'Cancel') */
BOOL bOKDialog;                             /* Did user 'OK' dialog? */
int nLastOpenPage = 0;                      /* Last property page opened(so can re-open on this next time) */


//-----------------------------------------------------------------------
/*
  Check if need to warn user that changes will take place after reset
  Return TRUE if wants to reset
*/
/*
BOOL Dialog_DoNeedReset(void)
{
  // Did we change colour/mono monitor? If so, must reset
  if (ConfigureParams.Screen.bUseHighRes!=DialogParams.Screen.bUseHighRes)
    return(TRUE);
  // Did change to GEM VDI display?
  if (ConfigureParams.TOSGEM.bUseExtGEMResolutions!=DialogParams.TOSGEM.bUseExtGEMResolutions)
    return(TRUE);
  // Did change GEM resolution or colour depth?
  if ( (ConfigureParams.TOSGEM.nGEMResolution!=DialogParams.TOSGEM.nGEMResolution)
   || (ConfigureParams.TOSGEM.nGEMColours!=DialogParams.TOSGEM.nGEMColours) )
    return(TRUE);

  return(FALSE);
}
*/

//-----------------------------------------------------------------------
/*
  Copy details back to configuration and perform reset
*/
/*
void Dialog_CopyDialogParamsToConfiguration(BOOL bForceReset)
{
  BOOL NeedReset;

  // Do we need to warn user of that changes will only take effect after reset?
  if (bForceReset)
    NeedReset = bForceReset;
  else
    NeedReset = Dialog_DoNeedReset();

  // Do need to change DirectX resolution? Need if change display/overscan settings (if switch between Colour/Mono cause reset later)
  if (bInFullScreen) {
    if ( (DialogParams.Screen.ChosenDisplayMode!=ConfigureParams.Screen.ChosenDisplayMode)
     || (DialogParams.Screen.Advanced.bAllowOverscan!=ConfigureParams.Screen.Advanced.bAllowOverscan) ) {
      Screen_ReturnFromFullScreen();
      ConfigureParams.Screen.ChosenDisplayMode = DialogParams.Screen.ChosenDisplayMode;
      ConfigureParams.Screen.Advanced.bAllowOverscan = DialogParams.Screen.Advanced.bAllowOverscan;
      Screen_EnterFullScreen();
    }
  }
  // Did set new printer parameters?
  if ( (DialogParams.Printer.bEnablePrinting!=ConfigureParams.Printer.bEnablePrinting)
   || (DialogParams.Printer.bPrintToFile!=ConfigureParams.Printer.bPrintToFile)
   || (stricmp(DialogParams.Printer.szPrintToFileName,ConfigureParams.Printer.szPrintToFileName)) )
    Printer_CloseAllConnections();
  // Did set new RS232 parameters?
  if ( (DialogParams.RS232.bEnableRS232!=ConfigureParams.RS232.bEnableRS232)
   || (DialogParams.RS232.nCOMPort!=ConfigureParams.RS232.nCOMPort) )
    RS232_CloseCOMPort();
  // Did stop sound? Or change playback Hz. If so, also stop sound recording
  if ( (!DialogParams.Sound.bEnableSound) || (DialogParams.Sound.nPlaybackQuality!=ConfigureParams.Sound.nPlaybackQuality) ) {
    if (Sound_AreWeRecording())
      Sound_EndRecording(NULL);
  }

  // Copy details to configuration, so can be saved out or set on reset
  ConfigureParams = DialogParams;
  // And write to configuration now, so don't loose
  Configuration_UnInit();

  // Copy details to global, if we reset copy them all
  Dialog_CopyDetailsFromConfiguration(NeedReset);
  // Set keyboard remap file
  Keymap_LoadRemapFile(ConfigureParams.Keyboard.szMappingFileName);
  // Set new sound playback rate
  DAudio_ReCreateDirectSoundBuffer();
  // Resize window if need
  if (!ConfigureParams.TOSGEM.bUseExtGEMResolutions)
    View_ResizeWindowToFull();

  // Do we need to perform reset?
  if (NeedReset) {
    Reset_Cold();
    Main_UnPauseEmulation();
    View_ToggleWindowsMouse(MOUSE_ST);
  }

  // Go into/return from full screen if flagged
  if ( (!bInFullScreen) && (DialogParams.Screen.bFullScreen) )
    Screen_EnterFullScreen();
  else if ( bInFullScreen && (!DialogParams.Screen.bFullScreen) )
    Screen_ReturnFromFullScreen();
}
*/

//-----------------------------------------------------------------------
/*
  Default elements of configuration structure
*/
/*
void Dialog_DefaultConfigurationDetails(void)
{
  // Clear parameters
  Memory_Clear(&ConfigureParams,sizeof(DLG_PARAMS));

  // Set defaults
  Dialog_Configure_SetDefaults();
  Dialog_Screen_SetDefaults();
  Dialog_Joysticks_SetDefaults();
  Dialog_Keyboard_SetDefaults();
  Dialog_Sound_SetDefaults();
  Dialog_Memory_SetDefaults();
  Dialog_DiscImage_SetDefaults();
  Dialog_HardDisc_SetDefaults();
  Dialog_TOSGEM_SetDefaults();
  Dialog_RS232_SetDefaults();
  Dialog_Printer_SetDefaults();
  Dialog_Favourites_SetDefaults();
}
*/

//-----------------------------------------------------------------------
/*
  Copy details from configuration structure into global variables for system
*/
/*
void Dialog_CopyDetailsFromConfiguration(BOOL bReset)
{
  // Set new timer thread
  Main_SetSpeedThreadTimer(ConfigureParams.Configure.nMinMaxSpeed);
  // Set resolution change
  if (bReset) {
    bUseVDIRes = ConfigureParams.TOSGEM.bUseExtGEMResolutions;
    bUseHighRes = ConfigureParams.Screen.bUseHighRes || (bUseVDIRes && (ConfigureParams.TOSGEM.nGEMColours==GEMCOLOUR_2));
    VDI_SetResolution(VDIModeOptions[ConfigureParams.TOSGEM.nGEMResolution],ConfigureParams.TOSGEM.nGEMColours);
  }
  // Set playback frequency
  DAudio_SetOutputAudioFreq(ConfigureParams.Sound.nPlaybackQuality);

  // Remove back-slashes, etc.. from names
  File_CleanFileName(ConfigureParams.TOSGEM.szTOSImageFileName);
}
*/

//-----------------------------------------------------------------------
/*
  Open Property sheet Options dialog
  Return TRUE is use chose OK, or FALSE if cancel!
*/
/*
BOOL Dialog_DoProperty(int StartingPage,BOOL bForceReset)
{
    PROPSHEETPAGE psp[NUM_PROPERTY_PAGES];
    PROPSHEETHEADER psh;
  int i;

  // Copy details to DialogParams(this is so can restore if 'Cancel' dialog)
  ConfigureParams.Screen.bFullScreen = bInFullScreen;
  DialogParams = ConfigureParams;

  // Create property pages for dialog
  for(i=0; i<NUM_PROPERTY_PAGES; i++) {
      psp[i].dwSize = sizeof(PROPSHEETPAGE);
    psp[i].dwFlags = PSP_USETITLE | PSP_HASHELP;
      psp[i].hInstance = hInst;
    psp[i].pszTemplate = MAKEINTRESOURCE(DialogPages[i].PageIDD);
      psp[i].pszIcon = NULL;
    psp[i].pfnDlgProc = DialogPages[i].pDlgProc;
      psp[i].pszTitle = DialogPages[i].pTitle;
    psp[i].lParam = 0;
    psp[i].pfnCallback = NULL;
  }

  // Set up property page
    psh.dwSize = sizeof(PROPSHEETHEADER);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW | PSH_HASHELP | PSH_USECALLBACK;
    psh.hwndParent = hWnd;
    psh.hInstance = hInst;
    psh.pszIcon = NULL;
    psh.pszCaption = (LPSTR)"Hatari Options";
    psh.nPages = sizeof(psp) / sizeof(PROPSHEETPAGE);
    psh.nStartPage = StartingPage;
    psh.ppsp = (LPCPROPSHEETPAGE)&psp;
    psh.pfnCallback = Dialog_PropertyCallBack;    // Callback to store of window handle

  // Clear handles(used to access other pages of dialog from another)
  Dialog_Configure_ClearHandle();
  Dialog_Screen_ClearHandle();
  Dialog_Sound_ClearHandle();

  bOKDialog = FALSE;                // Reset OK flag
  bSaveMemoryState = FALSE;
  bRestoreMemoryState = FALSE;
    PropertySheet(&psh);

  // Copy details to configuration, and ask user if wishes to reset
  if (bOKDialog)
    Dialog_CopyDialogParamsToConfiguration(bForceReset);
  // Did want to save/restore memory save? If did, need to re-enter emulation mode so can save in 'safe-zone'
  if (bSaveMemoryState || bRestoreMemoryState) {
    // Back into emulation mode, when next VBL occurs state will be safed - otherwise registers are unknown
    View_ToggleWindowsMouse(MOUSE_ST);
  }

  return(bOKDialog);
}
*/

//-----------------------------------------------------------------------
/*
  Simple call back to store off hWnd of Property sheet dialog so can choose 'OK' when dbl-click Favourites
*/
/*
BOOL CALLBACK Dialog_PropertyCallBack(HWND hDlg,UINT wParam, LONG lParam)
{
  // Just store, handle when Initialised
  PropHWnd =  hDlg;

  return(FALSE);
}
*/

//-----------------------------------------------------------------------
/*
  Set status of dialog button using flag
*/
/*
void Dialog_SetButton(HWND hDlg,int ButtonID,int Flag)
{
  if (hDlg) {
    if (Flag)
      CheckDlgButton(hDlg,ButtonID,BST_CHECKED);
    else
      CheckDlgButton(hDlg,ButtonID,BST_UNCHECKED);
  }
}
*/

//-----------------------------------------------------------------------
/*
  Read status of dialog button
*/
/*
BOOL Dialog_ReadButton(HWND hDlg,int ButtonID)
{
  if (IsDlgButtonChecked(hDlg,ButtonID)==BST_CHECKED)
    return(TRUE);
  else
    return(FALSE);
}

//-----------------------------------------------------------------------
/*
  Enable dialog item
*/
/*
void Dialog_EnableItem(HWND hDlg,int ButtonID,int State)
{
  EnableWindow(GetDlgItem(hDlg,ButtonID),State);
}
*/

//-----------------------------------------------------------------------
/*
  Enable dialog items
*/
/*
void Dialog_EnableItems(HWND hDlg,int *pButtonIDs,int State)
{
  int i=0;

  // Enable each button in list
  while(pButtonIDs[i]) {
    Dialog_EnableItem(hDlg,pButtonIDs[i],State);
    i++;
  }
}
*/

//-----------------------------------------------------------------------
/*
  Show dialog items, in range(ie controls all have consecutive IDs - dodgy very very handy!)
*/
/*
void Dialog_ShowItemRange(HWND hDlg,int LowButtonID,int HighButtonID,int Show)
{
  int i;

  for(i=LowButtonID; i<=HighButtonID; i++)
    ShowWindow(GetDlgItem(hDlg,i),Show);
}
*/

//-----------------------------------------------------------------------
/*
  Set text item in dialog
*/
/*
void Dialog_SetText(HWND hDlg,int ButtonID,char *szString)
{
  SendDlgItemMessage(hDlg,ButtonID,WM_SETTEXT,0,(LPARAM)szString);
}
*/

//-----------------------------------------------------------------------
/*
  Read text item from dialog
*/
/*
void Dialog_ReadText(HWND hDlg,int ButtonID,char *szString)
{
  SendDlgItemMessage(hDlg,ButtonID,WM_GETTEXT,1024,(LPARAM)szString);
}
*/

//-----------------------------------------------------------------------
/*
  Set Trackbar control range and select
*/
/*
void Dialog_SetTrackBar(HWND hDlg, int nTrackBarID, int nMin, int nMax, int nSelected)
{
  SendDlgItemMessage(hDlg,nTrackBarID,TBM_SETRANGE,TRUE,(LPARAM)MAKELONG(nMin,nMax));
  SendDlgItemMessage(hDlg,nTrackBarID,TBM_SETPOS,TRUE,(LPARAM)nSelected);
}
*/

//-----------------------------------------------------------------------
/*
  Read Trackbar control selection
*/
/*
int Dialog_GetTrackBar(HWND hDlg, int nTrackBarID)
{
  int nSelectedItem;

  // Read selected item, if error default to first item in list
  nSelectedItem = SendDlgItemMessage(hDlg,nTrackBarID,TBM_GETPOS,0,(LONG)0);
  if (nSelectedItem==CB_ERR)
    nSelectedItem = 0;

  return(nSelectedItem);
}
*/

//-----------------------------------------------------------------------
/*
  Add string items to a Combo Box
*/
/*
void Dialog_SetComboBoxItems(HWND hDlg, int ComboBoxID, char *pComboBoxStrings[], int nSelectedItem)
{
  int i=0;

  // Reset items
  SendDlgItemMessage(hDlg,ComboBoxID,CB_RESETCONTENT,0,(LONG)0);
  // Add items strings
  while(pComboBoxStrings[i]) {
    if (strlen(pComboBoxStrings[i])>0)
      SendDlgItemMessage(hDlg,ComboBoxID,CB_ADDSTRING,0,(LONG)pComboBoxStrings[i]);
    i++;
  }

  // And select chosen item
  SendDlgItemMessage(hDlg,ComboBoxID,CB_SETCURSEL,nSelectedItem,(LONG)0);
}
*/

//-----------------------------------------------------------------------
/*
  Select string items in a Combo Box
*/
/*
void Dialog_ComboBoxSelectString(HWND hDlg, int ComboBoxID, char *pszSelectedString)
{
  // And select chosen item
  SendDlgItemMessage(hDlg,ComboBoxID,CB_SELECTSTRING,0,(LONG)pszSelectedString);
}
*/

//-----------------------------------------------------------------------
/*
  Read selected item index from a Combo Box
*/
/*
int Dialog_GetSelectedComboBoxItem(HWND hDlg, int ComboBoxID)
{
  int nSelectedItem;

  // Read selected item, if error default to first item in list
  nSelectedItem = SendDlgItemMessage(hDlg,ComboBoxID,CB_GETCURSEL,0,(LONG)0);
  if (nSelectedItem==CB_ERR)
    nSelectedItem = 0;

  return(nSelectedItem);
}
*/
//-----------------------------------------------------------------------
/*
  Add string items to a List Box
*/
/*
void Dialog_SetListBoxItems(HWND hDlg, int ListBoxID, char *pListBoxStrings[], int nSelectedItem)
{
  int i=0;

  // Reset items
  SendDlgItemMessage(hDlg,ListBoxID,LB_RESETCONTENT,0,(LONG)0);
  // Add items strings
  while(pListBoxStrings[i]) {
    SendDlgItemMessage(hDlg,ListBoxID,LB_ADDSTRING,0,(LONG)pListBoxStrings[i]);
    i++;
  }

  // And select chosen item
  SendDlgItemMessage(hDlg,ListBoxID,LB_SETCURSEL,nSelectedItem,(LONG)0);
}
*/

//-----------------------------------------------------------------------
/*
  Read selected item index from a List Box
*/
/*
int Dialog_GetSelectedListBoxItem(HWND hDlg, int ListBoxID)
{
  int nSelectedItem;

  // Read selected item, if error default to first item in list
  nSelectedItem = SendDlgItemMessage(hDlg,ListBoxID,LB_GETCURSEL,0,(LONG)0);
  if (nSelectedItem==LB_ERR)
    nSelectedItem = 0;

  return(nSelectedItem);
}
*/

//-----------------------------------------------------------------------
/*
  Set Spin control range and select item
*/
/*
int Dialog_SetSpinList(HWND hDlg, int nEditBoxID, int nSpinID, char *pSpinStrings[], int nItems, int nSelectedItem)
{
  // Limit selection
  nSelectedItem = Misc_LimitInt(nSelectedItem, 0,nItems-1);

  // Fill text with selected item
  Dialog_SetText(hDlg,nEditBoxID,pSpinStrings[nSelectedItem]);
  // Set range and selection of Spin control
  if (nItems==1) {
    Dialog_EnableItem(hDlg,nSpinID,FALSE);
  }
  else {
    SendDlgItemMessage(hDlg,nSpinID,UDM_SETRANGE,0,(LPARAM)MAKELONG(nItems-1,0));
    SendDlgItemMessage(hDlg,nSpinID,UDM_SETPOS,0,(LPARAM)nSelectedItem);
    Dialog_EnableItem(hDlg,nSpinID,TRUE);
  }

  return(nSelectedItem);
}
*/

//-----------------------------------------------------------------------
/*
  Get Spin control item
*/
/*
int Dialog_GetSpinList(HWND hDlg, int nSpinID)
{
  return( LOWORD(SendDlgItemMessage(hDlg,nSpinID,UDM_GETPOS,0,(LPARAM)0)) );
}
*/

//-----------------------------------------------------------------------
/*
  Update Spin control with new selection, called from WM_VSCROLL
*/
/*
int Dialog_UpdateSpinList(HWND hDlg, int nEditBoxID, char *pSpinStrings[], int nNumSpinItems, int nNewSelectedItem)
{
  // Is item within range?
  if (nNewSelectedItem<0)
    nNewSelectedItem = 0;
  if (nNewSelectedItem>=nNumSpinItems)
    nNewSelectedItem = nNumSpinItems-1;

  // Fill text with new selected item
  Dialog_SetText(hDlg,nEditBoxID,pSpinStrings[nNewSelectedItem]);

  // Return new value
  return(nNewSelectedItem);
}
*/

//-----------------------------------------------------------------------
/*
  Set number of dialog radio buttons
*/
/*
void Dialog_SetRadioButtons(HWND hDlg,int StartButtonID,int EndButtonID,int nSelectedItem)
{
  CheckRadioButton(hDlg,StartButtonID,EndButtonID,StartButtonID+nSelectedItem);  // Run ST speed or max?  
}
*/

//-----------------------------------------------------------------------
/*
  Read status of dialog radio buttons, return index of chosen option from '0'
*/
/*
int Dialog_ReadRadioButtons(HWND hDlg,int StartButtonID,int EndButtonID)
{
  int i;

  for(i=StartButtonID; i<=EndButtonID; i++) {
    if (IsDlgButtonChecked(hDlg,i)==BST_CHECKED)
      return(i-StartButtonID);
  }

  return(0);
}
*/

//-----------------------------------------------------------------------
/*
  Add 'Column' to List View
*/
/*
void Dialog_AddListViewColumn(HWND hDlg, int ListViewID, int Order, char *pString, int Width)
{
  LVCOLUMN LvColumn;

  // Build structure
  LvColumn.mask = LVCF_FMT|LVCF_TEXT|LVCF_WIDTH;
  LvColumn.fmt = LVCFMT_LEFT;
  LvColumn.cx = Width;
  LvColumn.pszText = pString;
  LvColumn.cchTextMax = 0;
  LvColumn.iSubItem = 0;
  LvColumn.iImage = 0;
  LvColumn.iOrder = 0;

  // Add Column
  SendDlgItemMessage(hDlg,ListViewID,LVM_INSERTCOLUMN,Order,(LONG)&LvColumn);
}
*/
