/*
  Hatari - configuration.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Configuration File

  The configuration file is now stored in an ASCII format to allow the user
  to edit the file manually.
*/
const char Configuration_fileid[] = "Hatari configuration.c";

#include <SDL_keyboard.h>
#include <SDL_joystick.h>

#include "main.h"
#include "configuration.h"
#include "cfgopts.h"
#include "audio.h"
#include "sound.h"
#include "file.h"
#include "log.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "paths.h"
#include "screen.h"
#include "statusbar.h"
#include "vdi.h"
#include "video.h"
#include "avi_record.h"
#include "clocks_timings.h"
#include "68kDisass.h"
#include "disasm.h"
#include "fdc.h"
#include "dsp.h"
#include "joy.h"
#include "falcon/crossbar.h"
#include "stMemory.h"
#include "tos.h"
#include "screenSnapShot.h"


CNF_PARAMS ConfigureParams;                 /* List of configuration for the emulator */
char sConfigFileName[FILENAME_MAX];         /* Stores the name of the configuration file */


/* Used to load/save logging options */
static const struct Config_Tag configs_Log[] =
{
	{ "sLogFileName", String_Tag, ConfigureParams.Log.sLogFileName },
	{ "sTraceFileName", String_Tag, ConfigureParams.Log.sTraceFileName },
	{ "nTextLogLevel", Int_Tag, &ConfigureParams.Log.nTextLogLevel },
	{ "nAlertDlgLogLevel", Int_Tag, &ConfigureParams.Log.nAlertDlgLogLevel },
	{ "bConfirmQuit", Bool_Tag, &ConfigureParams.Log.bConfirmQuit },
	{ "bNatFeats", Bool_Tag, &ConfigureParams.Log.bNatFeats },
	{ "bConsoleWindow", Bool_Tag, &ConfigureParams.Log.bConsoleWindow },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save debugger options */
static const struct Config_Tag configs_Debugger[] =
{
	{ "nNumberBase", Int_Tag, &ConfigureParams.Debugger.nNumberBase },
	{ "nSymbolLines", Int_Tag, &ConfigureParams.Debugger.nSymbolLines },
	{ "nMemdumpLines", Int_Tag, &ConfigureParams.Debugger.nMemdumpLines },
	{ "nDisasmLines", Int_Tag, &ConfigureParams.Debugger.nDisasmLines },
	{ "nBacktraceLines", Int_Tag, &ConfigureParams.Debugger.nBacktraceLines },
	{ "nExceptionDebugMask", Int_Tag, &ConfigureParams.Debugger.nExceptionDebugMask },
	{ "nDisasmOptions", Int_Tag, &ConfigureParams.Debugger.nDisasmOptions },
	{ "bDisasmUAE", Bool_Tag, &ConfigureParams.Debugger.bDisasmUAE },
	{ "bSymbolsAutoLoad", Bool_Tag, &ConfigureParams.Debugger.bSymbolsAutoLoad },
	{ "bMatchAllSymbols", Bool_Tag, &ConfigureParams.Debugger.bMatchAllSymbols },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save screen options */
static const struct Config_Tag configs_Screen[] =
{
	{ "nMonitorType", Int_Tag, &ConfigureParams.Screen.nMonitorType },
	{ "nFrameSkips", Int_Tag, &ConfigureParams.Screen.nFrameSkips },
	{ "bFullScreen", Bool_Tag, &ConfigureParams.Screen.bFullScreen },
	{ "bKeepResolution", Bool_Tag, &ConfigureParams.Screen.bKeepResolution },
	{ "bResizable", Bool_Tag, &ConfigureParams.Screen.bResizable },
	{ "bAllowOverscan", Bool_Tag, &ConfigureParams.Screen.bAllowOverscan },
	{ "nSpec512Threshold", Int_Tag, &ConfigureParams.Screen.nSpec512Threshold },
	{ "bAspectCorrect", Bool_Tag, &ConfigureParams.Screen.bAspectCorrect },
	{ "bUseExtVdiResolutions", Bool_Tag, &ConfigureParams.Screen.bUseExtVdiResolutions },
	{ "nVdiWidth", Int_Tag, &ConfigureParams.Screen.nVdiWidth },
	{ "nVdiHeight", Int_Tag, &ConfigureParams.Screen.nVdiHeight },
	{ "nVdiColors", Int_Tag, &ConfigureParams.Screen.nVdiColors },
	{ "bMouseWarp", Bool_Tag, &ConfigureParams.Screen.bMouseWarp },
	{ "bShowStatusbar", Bool_Tag, &ConfigureParams.Screen.bShowStatusbar },
	{ "bShowDriveLed", Bool_Tag, &ConfigureParams.Screen.bShowDriveLed },
	{ "bCrop", Bool_Tag, &ConfigureParams.Screen.bCrop },
	{ "bForceMax", Bool_Tag, &ConfigureParams.Screen.bForceMax },
	{ "nMaxWidth", Int_Tag, &ConfigureParams.Screen.nMaxWidth },
	{ "nMaxHeight", Int_Tag, &ConfigureParams.Screen.nMaxHeight },
	{ "nZoomFactor", Float_Tag, &ConfigureParams.Screen.nZoomFactor },
	{ "bUseSdlRenderer", Bool_Tag, &ConfigureParams.Screen.bUseSdlRenderer },
	{ "ScreenShotFormat", Int_Tag, &ConfigureParams.Screen.ScreenShotFormat },
	{ "bUseVsync", Bool_Tag, &ConfigureParams.Screen.bUseVsync },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save joystick 0 options */
static const struct Config_Tag configs_Joystick0[] =
{
	{ "nJoystickMode", Int_Tag, &ConfigureParams.Joysticks.Joy[0].nJoystickMode },
	{ "bEnableAutoFire", Bool_Tag, &ConfigureParams.Joysticks.Joy[0].bEnableAutoFire },
	{ "bEnableJumpOnFire2", Bool_Tag, &ConfigureParams.Joysticks.Joy[0].bEnableJumpOnFire2 },
	{ "nJoyId", Int_Tag, &ConfigureParams.Joysticks.Joy[0].nJoyId },
	{ "nJoyBut1Index", Int_Tag, &ConfigureParams.Joysticks.Joy[0].nJoyButMap[0] },
	{ "nJoyBut2Index", Int_Tag, &ConfigureParams.Joysticks.Joy[0].nJoyButMap[1] },
	{ "nJoyBut3Index", Int_Tag, &ConfigureParams.Joysticks.Joy[0].nJoyButMap[2] },
	{ "kUp", Key_Tag, &ConfigureParams.Joysticks.Joy[0].nKeyCodeUp },
	{ "kDown", Key_Tag, &ConfigureParams.Joysticks.Joy[0].nKeyCodeDown },
	{ "kLeft", Key_Tag, &ConfigureParams.Joysticks.Joy[0].nKeyCodeLeft },
	{ "kRight", Key_Tag, &ConfigureParams.Joysticks.Joy[0].nKeyCodeRight },
	{ "kFire", Key_Tag, &ConfigureParams.Joysticks.Joy[0].nKeyCodeFire },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save joystick 1 options */
static const struct Config_Tag configs_Joystick1[] =
{
	{ "nJoystickMode", Int_Tag, &ConfigureParams.Joysticks.Joy[1].nJoystickMode },
	{ "bEnableAutoFire", Bool_Tag, &ConfigureParams.Joysticks.Joy[1].bEnableAutoFire },
	{ "bEnableJumpOnFire2", Bool_Tag, &ConfigureParams.Joysticks.Joy[1].bEnableJumpOnFire2 },
	{ "nJoyId", Int_Tag, &ConfigureParams.Joysticks.Joy[1].nJoyId },
	{ "nJoyBut1Index", Int_Tag, &ConfigureParams.Joysticks.Joy[1].nJoyButMap[0] },
	{ "nJoyBut2Index", Int_Tag, &ConfigureParams.Joysticks.Joy[1].nJoyButMap[1] },
	{ "nJoyBut3Index", Int_Tag, &ConfigureParams.Joysticks.Joy[1].nJoyButMap[2] },
	{ "kUp", Key_Tag, &ConfigureParams.Joysticks.Joy[1].nKeyCodeUp },
	{ "kDown", Key_Tag, &ConfigureParams.Joysticks.Joy[1].nKeyCodeDown },
	{ "kLeft", Key_Tag, &ConfigureParams.Joysticks.Joy[1].nKeyCodeLeft },
	{ "kRight", Key_Tag, &ConfigureParams.Joysticks.Joy[1].nKeyCodeRight },
	{ "kFire", Key_Tag, &ConfigureParams.Joysticks.Joy[1].nKeyCodeFire },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save joystick 2 options (joypad A) */
static const struct Config_Tag configs_Joystick2[] =
{
	{ "nJoystickMode", Int_Tag, &ConfigureParams.Joysticks.Joy[2].nJoystickMode },
	{ "bEnableAutoFire", Bool_Tag, &ConfigureParams.Joysticks.Joy[2].bEnableAutoFire },
	{ "bEnableJumpOnFire2", Bool_Tag, &ConfigureParams.Joysticks.Joy[2].bEnableJumpOnFire2 },
	{ "nJoyId", Int_Tag, &ConfigureParams.Joysticks.Joy[2].nJoyId },
	{ "nJoyBut1Index", Int_Tag, &ConfigureParams.Joysticks.Joy[2].nJoyButMap[0] },
	{ "nJoyBut2Index", Int_Tag, &ConfigureParams.Joysticks.Joy[2].nJoyButMap[1] },
	{ "nJoyBut3Index", Int_Tag, &ConfigureParams.Joysticks.Joy[2].nJoyButMap[2] },
	{ "kUp", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeUp },
	{ "kDown", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeDown },
	{ "kLeft", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeLeft },
	{ "kRight", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeRight },
	{ "kFire", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeFire },
	{ "kButtonB", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeB },
	{ "kButtonC", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeC },
	{ "kButtonOption", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeOption },
	{ "kButtonPause", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodePause },
	{ "kButtonStar", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeStar },
	{ "kButtonHash", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeHash },
	{ "kButton0", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeNum[0] },
	{ "kButton1", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeNum[1] },
	{ "kButton2", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeNum[2] },
	{ "kButton3", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeNum[3] },
	{ "kButton4", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeNum[4] },
	{ "kButton5", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeNum[5] },
	{ "kButton6", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeNum[6] },
	{ "kButton7", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeNum[7] },
	{ "kButton8", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeNum[8] },
	{ "kButton9", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeNum[9] },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save joystick 3 options (joypad B) */
static const struct Config_Tag configs_Joystick3[] =
{
	{ "nJoystickMode", Int_Tag, &ConfigureParams.Joysticks.Joy[3].nJoystickMode },
	{ "bEnableAutoFire", Bool_Tag, &ConfigureParams.Joysticks.Joy[3].bEnableAutoFire },
	{ "bEnableJumpOnFire2", Bool_Tag, &ConfigureParams.Joysticks.Joy[3].bEnableJumpOnFire2 },
	{ "nJoyId", Int_Tag, &ConfigureParams.Joysticks.Joy[3].nJoyId },
	{ "nJoyBut1Index", Int_Tag, &ConfigureParams.Joysticks.Joy[3].nJoyButMap[0] },
	{ "nJoyBut2Index", Int_Tag, &ConfigureParams.Joysticks.Joy[3].nJoyButMap[1] },
	{ "nJoyBut3Index", Int_Tag, &ConfigureParams.Joysticks.Joy[3].nJoyButMap[2] },
	{ "kUp", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeUp },
	{ "kDown", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeDown },
	{ "kLeft", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeLeft },
	{ "kRight", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeRight },
	{ "kFire", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeFire },
	{ "kButtonB", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeB },
	{ "kButtonC", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeC },
	{ "kButtonOption", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeOption },
	{ "kButtonPause", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodePause },
	{ "kButtonStar", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeStar },
	{ "kButtonHash", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeHash },
	{ "kButton0", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeNum[0] },
	{ "kButton1", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeNum[1] },
	{ "kButton2", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeNum[2] },
	{ "kButton3", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeNum[3] },
	{ "kButton4", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeNum[4] },
	{ "kButton5", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeNum[5] },
	{ "kButton6", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeNum[6] },
	{ "kButton7", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeNum[7] },
	{ "kButton8", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeNum[8] },
	{ "kButton9", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeNum[9] },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save joystick 4 options */
static const struct Config_Tag configs_Joystick4[] =
{
	{ "nJoystickMode", Int_Tag, &ConfigureParams.Joysticks.Joy[4].nJoystickMode },
	{ "bEnableAutoFire", Bool_Tag, &ConfigureParams.Joysticks.Joy[4].bEnableAutoFire },
	{ "bEnableJumpOnFire2", Bool_Tag, &ConfigureParams.Joysticks.Joy[4].bEnableJumpOnFire2 },
	{ "nJoyId", Int_Tag, &ConfigureParams.Joysticks.Joy[4].nJoyId },
	{ "nJoyBut1Index", Int_Tag, &ConfigureParams.Joysticks.Joy[4].nJoyButMap[0] },
	{ "nJoyBut2Index", Int_Tag, &ConfigureParams.Joysticks.Joy[4].nJoyButMap[1] },
	{ "nJoyBut3Index", Int_Tag, &ConfigureParams.Joysticks.Joy[4].nJoyButMap[2] },
	{ "kUp", Key_Tag, &ConfigureParams.Joysticks.Joy[4].nKeyCodeUp },
	{ "kDown", Key_Tag, &ConfigureParams.Joysticks.Joy[4].nKeyCodeDown },
	{ "kLeft", Key_Tag, &ConfigureParams.Joysticks.Joy[4].nKeyCodeLeft },
	{ "kRight", Key_Tag, &ConfigureParams.Joysticks.Joy[4].nKeyCodeRight },
	{ "kFire", Key_Tag, &ConfigureParams.Joysticks.Joy[4].nKeyCodeFire },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save joystick 5 options */
static const struct Config_Tag configs_Joystick5[] =
{
	{ "nJoystickMode", Int_Tag, &ConfigureParams.Joysticks.Joy[5].nJoystickMode },
	{ "bEnableAutoFire", Bool_Tag, &ConfigureParams.Joysticks.Joy[5].bEnableAutoFire },
	{ "bEnableJumpOnFire2", Bool_Tag, &ConfigureParams.Joysticks.Joy[5].bEnableJumpOnFire2 },
	{ "nJoyId", Int_Tag, &ConfigureParams.Joysticks.Joy[5].nJoyId },
	{ "nJoyBut1Index", Int_Tag, &ConfigureParams.Joysticks.Joy[5].nJoyButMap[0] },
	{ "nJoyBut2Index", Int_Tag, &ConfigureParams.Joysticks.Joy[5].nJoyButMap[1] },
	{ "nJoyBut3Index", Int_Tag, &ConfigureParams.Joysticks.Joy[5].nJoyButMap[2] },
	{ "kUp", Key_Tag, &ConfigureParams.Joysticks.Joy[5].nKeyCodeUp },
	{ "kDown", Key_Tag, &ConfigureParams.Joysticks.Joy[5].nKeyCodeDown },
	{ "kLeft", Key_Tag, &ConfigureParams.Joysticks.Joy[5].nKeyCodeLeft },
	{ "kRight", Key_Tag, &ConfigureParams.Joysticks.Joy[5].nKeyCodeRight },
	{ "kFire", Key_Tag, &ConfigureParams.Joysticks.Joy[5].nKeyCodeFire },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save keyboard options */
static const struct Config_Tag configs_Keyboard[] =
{
	{ "bDisableKeyRepeat", Bool_Tag, &ConfigureParams.Keyboard.bDisableKeyRepeat },
	{ "nKeymapType", Int_Tag, &ConfigureParams.Keyboard.nKeymapType },
	{ "nCountryCode", Int_Tag, &ConfigureParams.Keyboard.nCountryCode },
	{ "nKbdLayout", Int_Tag, &ConfigureParams.Keyboard.nKbdLayout },
	{ "nLanguage", Int_Tag, &ConfigureParams.Keyboard.nLanguage },
	{ "szMappingFileName", String_Tag, ConfigureParams.Keyboard.szMappingFileName },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save shortcut key bindings with modifiers options */
static const struct Config_Tag configs_ShortCutWithMod[] =
{
	{ "kOptions",    Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_OPTIONS] },
	{ "kFullScreen", Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_FULLSCREEN] },
	{ "kBorders",    Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_BORDERS] },
	{ "kMouseMode",  Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_MOUSEGRAB] },
	{ "kColdReset",  Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_COLDRESET] },
	{ "kWarmReset",  Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_WARMRESET] },
	{ "kScreenShot", Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_SCREENSHOT] },
	{ "kBossKey",    Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_BOSSKEY] },
	{ "kCursorEmu",  Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_CURSOREMU] },
	{ "kFastForward",Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_FASTFORWARD] },
	{ "kRecAnim",    Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_RECANIM] },
	{ "kRecSound",   Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_RECSOUND] },
	{ "kSound",      Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_SOUND] },
	{ "kPause",      Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_PAUSE] },
	{ "kDebugger",   Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_DEBUG] },
	{ "kQuit",       Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_QUIT] },
	{ "kLoadMem",    Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_LOADMEM] },
	{ "kSaveMem",    Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_SAVEMEM] },
	{ "kInsertDiskA",Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_INSERTDISKA] },
	{ "kSwitchJoy0", Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_JOY_0] },
	{ "kSwitchJoy1", Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_JOY_1] },
	{ "kSwitchPadA", Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_PAD_A] },
	{ "kSwitchPadB", Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_PAD_B] },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save shortcut key bindings without modifiers options */
static const struct Config_Tag configs_ShortCutWithoutMod[] =
{
	{ "kOptions",    Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_OPTIONS] },
	{ "kFullScreen", Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_FULLSCREEN] },
	{ "kBorders",    Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_BORDERS] },
	{ "kMouseMode",  Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_MOUSEGRAB] },
	{ "kColdReset",  Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_COLDRESET] },
	{ "kWarmReset",  Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_WARMRESET] },
	{ "kScreenShot", Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_SCREENSHOT] },
	{ "kBossKey",    Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_BOSSKEY] },
	{ "kCursorEmu",  Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_CURSOREMU] },
	{ "kFastForward",Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_FASTFORWARD] },
	{ "kRecAnim",    Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_RECANIM] },
	{ "kRecSound",   Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_RECSOUND] },
	{ "kSound",      Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_SOUND] },
	{ "kPause",      Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_PAUSE] },
	{ "kDebugger",   Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_DEBUG] },
	{ "kQuit",       Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_QUIT] },
	{ "kLoadMem",    Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_LOADMEM] },
	{ "kSaveMem",    Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_SAVEMEM] },
	{ "kInsertDiskA",Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_INSERTDISKA] },
	{ "kSwitchJoy0", Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_JOY_0] },
	{ "kSwitchJoy1", Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_JOY_1] },
	{ "kSwitchPadA", Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_PAD_A] },
	{ "kSwitchPadB", Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_PAD_B] },
	{ NULL , Error_Tag, NULL }
};


/* Used to load/save sound options */
static const struct Config_Tag configs_Sound[] =
{
	{ "bEnableMicrophone", Bool_Tag, &ConfigureParams.Sound.bEnableMicrophone },
	{ "bEnableSound", Bool_Tag, &ConfigureParams.Sound.bEnableSound },
	{ "bEnableSoundSync", Bool_Tag, &ConfigureParams.Sound.bEnableSoundSync },
	{ "nPlaybackFreq", Int_Tag, &ConfigureParams.Sound.nPlaybackFreq },
	{ "nSdlAudioBufferSize", Int_Tag, &ConfigureParams.Sound.SdlAudioBufferSize },
	{ "szYMCaptureFileName", String_Tag, ConfigureParams.Sound.szYMCaptureFileName },
	{ "YmVolumeMixing", Int_Tag, &ConfigureParams.Sound.YmVolumeMixing },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save memory options */
static const struct Config_Tag configs_Memory[] =
{
	{ "nMemorySize", Int_Tag, &ConfigureParams.Memory.STRamSize_KB },
	{ "nTTRamSize", Int_Tag, &ConfigureParams.Memory.TTRamSize_KB },
	{ "bAutoSave", Bool_Tag, &ConfigureParams.Memory.bAutoSave },
	{ "szMemoryCaptureFileName", String_Tag, ConfigureParams.Memory.szMemoryCaptureFileName },
	{ "szAutoSaveFileName", String_Tag, ConfigureParams.Memory.szAutoSaveFileName },
	{ NULL , Error_Tag, NULL }
};


/* Used to load/save floppy options */
static const struct Config_Tag configs_Floppy[] =
{
	{ "bAutoInsertDiskB", Bool_Tag, &ConfigureParams.DiskImage.bAutoInsertDiskB },
	{ "FastFloppy", Bool_Tag, &ConfigureParams.DiskImage.FastFloppy },
	{ "EnableDriveA", Bool_Tag, &ConfigureParams.DiskImage.EnableDriveA },
	{ "DriveA_NumberOfHeads", Int_Tag, &ConfigureParams.DiskImage.DriveA_NumberOfHeads },
	{ "EnableDriveB", Bool_Tag, &ConfigureParams.DiskImage.EnableDriveB },
	{ "DriveB_NumberOfHeads", Int_Tag, &ConfigureParams.DiskImage.DriveB_NumberOfHeads },
	{ "nWriteProtection", Int_Tag, &ConfigureParams.DiskImage.nWriteProtection },
	{ "szDiskAZipPath", String_Tag, ConfigureParams.DiskImage.szDiskZipPath[0] },
	{ "szDiskAFileName", String_Tag, ConfigureParams.DiskImage.szDiskFileName[0] },
	{ "szDiskBZipPath", String_Tag, ConfigureParams.DiskImage.szDiskZipPath[1] },
	{ "szDiskBFileName", String_Tag, ConfigureParams.DiskImage.szDiskFileName[1] },
	{ "szDiskImageDirectory", String_Tag, ConfigureParams.DiskImage.szDiskImageDirectory },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save HD options */
static const struct Config_Tag configs_HardDisk[] =
{
	{ "nGemdosDrive", Int_Tag, &ConfigureParams.HardDisk.nGemdosDrive },
	{ "bBootFromHardDisk", Bool_Tag, &ConfigureParams.HardDisk.bBootFromHardDisk },
	{ "bUseHardDiskDirectory", Bool_Tag, &ConfigureParams.HardDisk.bUseHardDiskDirectories },
	{ "szHardDiskDirectory", String_Tag, ConfigureParams.HardDisk.szHardDiskDirectories[DRIVE_C] },
	{ "nGemdosCase", Int_Tag, &ConfigureParams.HardDisk.nGemdosCase },
	{ "nWriteProtection", Int_Tag, &ConfigureParams.HardDisk.nWriteProtection },
	{ "bFilenameConversion", Bool_Tag, &ConfigureParams.HardDisk.bFilenameConversion },
	{ "bGemdosHostTime", Bool_Tag, &ConfigureParams.HardDisk.bGemdosHostTime },
	{ NULL , Error_Tag, NULL }
};

static const struct Config_Tag configs_HardDisk_Old[] =
{	/* only used for loading */
	{ "bUseHardDiskImage", Bool_Tag, &ConfigureParams.Acsi[0].bUseDevice },
	{ "szHardDiskImage", String_Tag, ConfigureParams.Acsi[0].sDeviceFile },
	{ "bUseIdeMasterHardDiskImage", Bool_Tag, &ConfigureParams.Ide[0].bUseDevice },
	{ "szIdeMasterHardDiskImage", String_Tag, ConfigureParams.Ide[0].sDeviceFile },
	{ "bUseIdeSlaveHardDiskImage", Bool_Tag, &ConfigureParams.Ide[1].bUseDevice },
	{ "szIdeSlaveHardDiskImage", String_Tag, ConfigureParams.Ide[1].sDeviceFile },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save ACSI options */
static const struct Config_Tag configs_Acsi[] =
{
	{ "bUseDevice0", Bool_Tag, &ConfigureParams.Acsi[0].bUseDevice },
	{ "sDeviceFile0", String_Tag, ConfigureParams.Acsi[0].sDeviceFile },
	{ "nBlockSize0", Int_Tag, &ConfigureParams.Acsi[0].nBlockSize },
	{ "bUseDevice1", Bool_Tag, &ConfigureParams.Acsi[1].bUseDevice },
	{ "sDeviceFile1", String_Tag, ConfigureParams.Acsi[1].sDeviceFile },
	{ "nBlockSize1", Int_Tag, &ConfigureParams.Acsi[1].nBlockSize },
	{ "bUseDevice2", Bool_Tag, &ConfigureParams.Acsi[2].bUseDevice },
	{ "sDeviceFile2", String_Tag, ConfigureParams.Acsi[2].sDeviceFile },
	{ "nBlockSize2", Int_Tag, &ConfigureParams.Acsi[2].nBlockSize },
	{ "bUseDevice3", Bool_Tag, &ConfigureParams.Acsi[3].bUseDevice },
	{ "sDeviceFile3", String_Tag, ConfigureParams.Acsi[3].sDeviceFile },
	{ "nBlockSize3", Int_Tag, &ConfigureParams.Acsi[3].nBlockSize },
	{ "bUseDevice4", Bool_Tag, &ConfigureParams.Acsi[4].bUseDevice },
	{ "sDeviceFile4", String_Tag, ConfigureParams.Acsi[4].sDeviceFile },
	{ "nBlockSize4", Int_Tag, &ConfigureParams.Acsi[4].nBlockSize },
	{ "bUseDevice5", Bool_Tag, &ConfigureParams.Acsi[5].bUseDevice },
	{ "sDeviceFile5", String_Tag, ConfigureParams.Acsi[5].sDeviceFile },
	{ "nBlockSize5", Int_Tag, &ConfigureParams.Acsi[5].nBlockSize },
	{ "bUseDevice6", Bool_Tag, &ConfigureParams.Acsi[6].bUseDevice },
	{ "sDeviceFile6", String_Tag, ConfigureParams.Acsi[6].sDeviceFile },
	{ "nBlockSize6", Int_Tag, &ConfigureParams.Acsi[6].nBlockSize },
	{ "bUseDevice7", Bool_Tag, &ConfigureParams.Acsi[7].bUseDevice },
	{ "sDeviceFile7", String_Tag, ConfigureParams.Acsi[7].sDeviceFile },
	{ "nBlockSize7", Int_Tag, &ConfigureParams.Acsi[7].nBlockSize },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save SCSI options */
static const struct Config_Tag configs_Scsi[] =
{
	{ "bUseDevice0", Bool_Tag, &ConfigureParams.Scsi[0].bUseDevice },
	{ "sDeviceFile0", String_Tag, ConfigureParams.Scsi[0].sDeviceFile },
	{ "nBlockSize0", Int_Tag, &ConfigureParams.Scsi[0].nBlockSize },
	{ "bUseDevice1", Bool_Tag, &ConfigureParams.Scsi[1].bUseDevice },
	{ "sDeviceFile1", String_Tag, ConfigureParams.Scsi[1].sDeviceFile },
	{ "nBlockSize1", Int_Tag, &ConfigureParams.Scsi[1].nBlockSize },
	{ "bUseDevice2", Bool_Tag, &ConfigureParams.Scsi[2].bUseDevice },
	{ "sDeviceFile2", String_Tag, ConfigureParams.Scsi[2].sDeviceFile },
	{ "nBlockSize2", Int_Tag, &ConfigureParams.Scsi[2].nBlockSize },
	{ "bUseDevice3", Bool_Tag, &ConfigureParams.Scsi[3].bUseDevice },
	{ "sDeviceFile3", String_Tag, ConfigureParams.Scsi[3].sDeviceFile },
	{ "nBlockSize3", Int_Tag, &ConfigureParams.Scsi[3].nBlockSize },
	{ "bUseDevice4", Bool_Tag, &ConfigureParams.Scsi[4].bUseDevice },
	{ "sDeviceFile4", String_Tag, ConfigureParams.Scsi[4].sDeviceFile },
	{ "nBlockSize4", Int_Tag, &ConfigureParams.Scsi[4].nBlockSize },
	{ "bUseDevice5", Bool_Tag, &ConfigureParams.Scsi[5].bUseDevice },
	{ "sDeviceFile5", String_Tag, ConfigureParams.Scsi[5].sDeviceFile },
	{ "nBlockSize5", Int_Tag, &ConfigureParams.Scsi[5].nBlockSize },
	{ "bUseDevice6", Bool_Tag, &ConfigureParams.Scsi[6].bUseDevice },
	{ "sDeviceFile6", String_Tag, ConfigureParams.Scsi[6].sDeviceFile },
	{ "nBlockSize6", Int_Tag, &ConfigureParams.Scsi[6].nBlockSize },
	{ "bUseDevice7", Bool_Tag, &ConfigureParams.Scsi[7].bUseDevice },
	{ "sDeviceFile7", String_Tag, ConfigureParams.Scsi[7].sDeviceFile },
	{ "nBlockSize7", Int_Tag, &ConfigureParams.Scsi[7].nBlockSize },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save IDE options */
static const struct Config_Tag configs_Ide[] =
{
	{ "bUseDevice0", Bool_Tag, &ConfigureParams.Ide[0].bUseDevice },
	{ "nByteSwap0", Int_Tag, &ConfigureParams.Ide[0].nByteSwap },
	{ "sDeviceFile0", String_Tag, ConfigureParams.Ide[0].sDeviceFile },
	{ "nBlockSize0", Int_Tag, &ConfigureParams.Ide[0].nBlockSize },
	{ "nDeviceType0", Int_Tag, &ConfigureParams.Ide[0].nDeviceType },
	{ "bUseDevice1", Bool_Tag, &ConfigureParams.Ide[1].bUseDevice },
	{ "nByteSwap1", Int_Tag, &ConfigureParams.Ide[1].nByteSwap },
	{ "sDeviceFile1", String_Tag, ConfigureParams.Ide[1].sDeviceFile },
	{ "nBlockSize1", Int_Tag, &ConfigureParams.Ide[1].nBlockSize },
	{ "nDeviceType1", Int_Tag, &ConfigureParams.Ide[1].nDeviceType },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save ROM options */
static const struct Config_Tag configs_Rom[] =
{
	{ "szTosImageFileName", String_Tag, ConfigureParams.Rom.szTosImageFileName },
	{ "bPatchTos", Bool_Tag, &ConfigureParams.Rom.bPatchTos },
	{ "szCartridgeImageFileName", String_Tag, ConfigureParams.Rom.szCartridgeImageFileName },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save LILO options, names are same as with Aranym */
static const struct Config_Tag configs_Lilo[] =
{
	{ "Args", String_Tag, ConfigureParams.Lilo.szCommandLine },
	{ "Kernel", String_Tag, ConfigureParams.Lilo.szKernelFileName },
	{ "Symbols", String_Tag, ConfigureParams.Lilo.szKernelSymbols },
	{ "Ramdisk", String_Tag, ConfigureParams.Lilo.szRamdiskFileName },
	{ "HaltOnReboot", Bool_Tag, &ConfigureParams.Lilo.bHaltOnReboot },
	{ "KernelToFastRam", Bool_Tag, &ConfigureParams.Lilo.bKernelToFastRam },
	{ "RamdiskToFastRam", Bool_Tag, &ConfigureParams.Lilo.bRamdiskToFastRam },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save RS232 options */
static const struct Config_Tag configs_Rs232[] =
{
	{ "bEnableRS232", Bool_Tag, &ConfigureParams.RS232.bEnableRS232 },
	{ "szOutFileName", String_Tag, ConfigureParams.RS232.szOutFileName },
	{ "szInFileName", String_Tag, ConfigureParams.RS232.szInFileName },
	{ "EnableSccA", Bool_Tag, &ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_A_SERIAL] },
	{ "SccAOutFileName", String_Tag, ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_A_SERIAL] },
	{ "SccAInFileName", String_Tag, ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_A_SERIAL] },
	{ "EnableSccALan", Bool_Tag, &ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_A_LAN] },
	{ "SccALanOutFileName", String_Tag, ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_A_LAN] },
	{ "SccALanInFileName", String_Tag, ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_A_LAN] },
	{ "EnableSccB", Bool_Tag, &ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_B] },
	{ "SccBOutFileName", String_Tag, ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_B] },
	{ "SccBInFileName", String_Tag, ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_B] },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save printer options */
static const struct Config_Tag configs_Printer[] =
{
	{ "bEnablePrinting", Bool_Tag, &ConfigureParams.Printer.bEnablePrinting },
	{ "szPrintToFileName", String_Tag, ConfigureParams.Printer.szPrintToFileName },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save MIDI options */
static const struct Config_Tag configs_Midi[] =
{
	{ "bEnableMidi", Bool_Tag, &ConfigureParams.Midi.bEnableMidi },
	{ "sMidiInFileName", String_Tag, ConfigureParams.Midi.sMidiInFileName },
	{ "sMidiOutFileName", String_Tag, ConfigureParams.Midi.sMidiOutFileName },
	{ "sMidiInPortName", String_Tag, ConfigureParams.Midi.sMidiInPortName },
	{ "sMidiOutPortName", String_Tag, ConfigureParams.Midi.sMidiOutPortName },
	{ NULL , Error_Tag, NULL }
};

/* Used to load system options from old config files */
static int nOldMachineType;
static bool bOldRealTimeClock;
static const struct Config_Tag configs_System_Old[] =
{
	{ "nMachineType", Int_Tag, &nOldMachineType },
	{ "bRealTimeClock", Bool_Tag, &bOldRealTimeClock },
	{ NULL , Error_Tag, NULL }
};
/* Used to load/save system options */
static const struct Config_Tag configs_System[] =
{
	{ "nCpuLevel", Int_Tag, &ConfigureParams.System.nCpuLevel },
	{ "nCpuFreq", Int_Tag, &ConfigureParams.System.nCpuFreq },
	{ "bCompatibleCpu", Bool_Tag, &ConfigureParams.System.bCompatibleCpu },
	{ "nModelType", Int_Tag, &ConfigureParams.System.nMachineType },
	{ "bBlitter", Bool_Tag, &ConfigureParams.System.bBlitter },
	{ "nDSPType", Int_Tag, &ConfigureParams.System.nDSPType },
	{ "nVMEType", Int_Tag, &ConfigureParams.System.nVMEType },
	{ "nRtcYear", Int_Tag, &ConfigureParams.System.nRtcYear },
	{ "bPatchTimerD", Bool_Tag, &ConfigureParams.System.bPatchTimerD },
	{ "bFastBoot", Bool_Tag, &ConfigureParams.System.bFastBoot },
	{ "bFastForward", Bool_Tag, &ConfigureParams.System.bFastForward },
	{ "bAddressSpace24", Bool_Tag, &ConfigureParams.System.bAddressSpace24 },
	{ "bCycleExactCpu", Bool_Tag, &ConfigureParams.System.bCycleExactCpu },
	{ "n_FPUType", Int_Tag, &ConfigureParams.System.n_FPUType },
/* JIT	{ "bCompatibleFPU", Bool_Tag, &ConfigureParams.System.bCompatibleFPU }, */
	{ "bSoftFloatFPU", Bool_Tag, &ConfigureParams.System.bSoftFloatFPU },
	{ "bMMU", Bool_Tag, &ConfigureParams.System.bMMU },
	{ "VideoTiming", Int_Tag, &ConfigureParams.System.VideoTimingMode },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save video options */
static const struct Config_Tag configs_Video[] =
{
	{ "AviRecordVcodec", Int_Tag, &ConfigureParams.Video.AviRecordVcodec },
	{ "AviRecordFps", Int_Tag, &ConfigureParams.Video.AviRecordFps },
	{ "AviRecordFile", String_Tag, ConfigureParams.Video.AviRecordFile },
	{ NULL , Error_Tag, NULL }
};


/*-----------------------------------------------------------------------*/
/**
 * Set default configuration values.
 */
void Configuration_SetDefault(void)
{
	int i, maxjoy;
	const char *psHomeDir;
	const char *psWorkingDir;

	psHomeDir = Paths_GetHatariHome();
	psWorkingDir = Paths_GetWorkingDir();

	/* Clear parameters */
	memset(&ConfigureParams, 0, sizeof(CNF_PARAMS));

	/* Set defaults for logging and tracing */
	strcpy(ConfigureParams.Log.sLogFileName, "stderr");
	strcpy(ConfigureParams.Log.sTraceFileName, "stderr");
	ConfigureParams.Log.nTextLogLevel = LOG_INFO;
	ConfigureParams.Log.nAlertDlgLogLevel = LOG_ERROR;
	ConfigureParams.Log.bConfirmQuit = true;
	ConfigureParams.Log.bNatFeats = false;
	ConfigureParams.Log.bConsoleWindow = false;

	/* Set defaults for debugger */
	ConfigureParams.Debugger.nNumberBase = 10;
	ConfigureParams.Debugger.nSymbolLines = -1; /* <0: use terminal size */
	ConfigureParams.Debugger.nMemdumpLines = -1; /* <0: use terminal size */
	ConfigureParams.Debugger.nDisasmLines = -1; /* <0: use terminal size */
	ConfigureParams.Debugger.nBacktraceLines = 0; /* <=0: show all */
	ConfigureParams.Debugger.nExceptionDebugMask = DEFAULT_EXCEPTIONS;
	/* external one has nicer output, but isn't as complete as UAE one */
	ConfigureParams.Debugger.bDisasmUAE = true;
	ConfigureParams.Debugger.bSymbolsAutoLoad = true;
	ConfigureParams.Debugger.bMatchAllSymbols = false;
	ConfigureParams.Debugger.nDisasmOptions = Disasm_GetOptions();
	Disasm_Init();

	/* Set defaults for floppy disk images */
	ConfigureParams.DiskImage.bAutoInsertDiskB = true;
	ConfigureParams.DiskImage.FastFloppy = false;
	ConfigureParams.DiskImage.nWriteProtection = WRITEPROT_OFF;

	ConfigureParams.DiskImage.EnableDriveA = true;
	FDC_Drive_Set_Enable ( 0 , ConfigureParams.DiskImage.EnableDriveA );
	ConfigureParams.DiskImage.DriveA_NumberOfHeads = 2;
	FDC_Drive_Set_NumberOfHeads ( 0 , ConfigureParams.DiskImage.DriveA_NumberOfHeads );

	ConfigureParams.DiskImage.EnableDriveB = true;
	FDC_Drive_Set_Enable ( 1 , ConfigureParams.DiskImage.EnableDriveB );
	ConfigureParams.DiskImage.DriveB_NumberOfHeads = 2;
	FDC_Drive_Set_NumberOfHeads ( 1 , ConfigureParams.DiskImage.DriveB_NumberOfHeads );

	for (i = 0; i < MAX_FLOPPYDRIVES; i++)
	{
		ConfigureParams.DiskImage.szDiskZipPath[i][0] = '\0';
		ConfigureParams.DiskImage.szDiskFileName[i][0] = '\0';
	}
	strcpy(ConfigureParams.DiskImage.szDiskImageDirectory, psWorkingDir);
	File_AddSlashToEndFileName(ConfigureParams.DiskImage.szDiskImageDirectory);

	/* Set defaults for hard disks */
	ConfigureParams.HardDisk.bBootFromHardDisk = false;
	ConfigureParams.HardDisk.bFilenameConversion = false;
	ConfigureParams.HardDisk.bGemdosHostTime = false;
	ConfigureParams.HardDisk.nGemdosCase = GEMDOS_NOP;
	ConfigureParams.HardDisk.nWriteProtection = WRITEPROT_OFF;
	ConfigureParams.HardDisk.nGemdosDrive = DRIVE_C;
	ConfigureParams.HardDisk.bUseHardDiskDirectories = false;
	for (i = 0; i < MAX_HARDDRIVES; i++)
	{
		strcpy(ConfigureParams.HardDisk.szHardDiskDirectories[i], psWorkingDir);
		File_CleanFileName(ConfigureParams.HardDisk.szHardDiskDirectories[i]);
	}

	/* ACSI */
	for (i = 0; i < MAX_ACSI_DEVS; i++)
	{
		ConfigureParams.Acsi[i].bUseDevice = false;
		strcpy(ConfigureParams.Acsi[i].sDeviceFile, psWorkingDir);
		ConfigureParams.Acsi[i].nBlockSize = 512;
	}
	/* SCSI */
	for (i = 0; i < MAX_SCSI_DEVS; i++)
	{
		ConfigureParams.Scsi[i].bUseDevice = false;
		strcpy(ConfigureParams.Scsi[i].sDeviceFile, psWorkingDir);
		ConfigureParams.Scsi[i].nBlockSize = 512;
	}
	/* IDE */
	for (i = 0; i < MAX_IDE_DEVS; i++)
	{
		ConfigureParams.Ide[i].bUseDevice = false;
		ConfigureParams.Ide[i].nByteSwap = BYTESWAP_AUTO;
		strcpy(ConfigureParams.Ide[i].sDeviceFile, psWorkingDir);
		ConfigureParams.Ide[i].nBlockSize = 512;
	}

	/* Set defaults for Joysticks */
	maxjoy = Joy_GetMaxId();
	for (i = 0; i < JOYSTICK_COUNT; i++)
	{
		ConfigureParams.Joysticks.Joy[i].nJoystickMode = JOYSTICK_DISABLED;
		ConfigureParams.Joysticks.Joy[i].bEnableAutoFire = false;
		ConfigureParams.Joysticks.Joy[i].bEnableJumpOnFire2 = true;
		ConfigureParams.Joysticks.Joy[i].nJoyId = (i > maxjoy ? maxjoy : i);
		for (int j = 0; j < JOYSTICK_BUTTONS; j++)
			ConfigureParams.Joysticks.Joy[i].nJoyButMap[j] = j;
		ConfigureParams.Joysticks.Joy[i].nKeyCodeUp = SDLK_UP;
		ConfigureParams.Joysticks.Joy[i].nKeyCodeDown = SDLK_DOWN;
		ConfigureParams.Joysticks.Joy[i].nKeyCodeLeft = SDLK_LEFT;
		ConfigureParams.Joysticks.Joy[i].nKeyCodeRight = SDLK_RIGHT;
		ConfigureParams.Joysticks.Joy[i].nKeyCodeFire = SDLK_RCTRL;
	}

	for (i = 0; i <= 9; i++)
		ConfigureParams.Joysticks.Joy[JOYID_JOYPADA].nKeyCodeNum[i] = SDLK_0 + i;
	ConfigureParams.Joysticks.Joy[JOYID_JOYPADA].nKeyCodeB = SDLK_b;
	ConfigureParams.Joysticks.Joy[JOYID_JOYPADA].nKeyCodeC = SDLK_c;
	ConfigureParams.Joysticks.Joy[JOYID_JOYPADA].nKeyCodeOption = SDLK_o;
	ConfigureParams.Joysticks.Joy[JOYID_JOYPADA].nKeyCodePause = SDLK_p;
	ConfigureParams.Joysticks.Joy[JOYID_JOYPADA].nKeyCodeHash = SDLK_HASH;
	ConfigureParams.Joysticks.Joy[JOYID_JOYPADA].nKeyCodeStar = SDLK_PLUS;

	if (SDL_NumJoysticks() > 0)
	{
		/* ST Joystick #1 is default joystick */
		ConfigureParams.Joysticks.Joy[1].nJoyId = 0;
		ConfigureParams.Joysticks.Joy[0].nJoyId = (maxjoy ? 1 : 0);
		ConfigureParams.Joysticks.Joy[1].nJoystickMode = JOYSTICK_REALSTICK;
		ConfigureParams.Joysticks.Joy[1].bEnableJumpOnFire2 = false;
	}

	/* Set defaults for Keyboard */
	ConfigureParams.Keyboard.bDisableKeyRepeat = false;
	ConfigureParams.Keyboard.nKeymapType = KEYMAP_SYMBOLIC;
	ConfigureParams.Keyboard.nCountryCode = TOS_LANG_UNKNOWN;
	ConfigureParams.Keyboard.nKbdLayout = TOS_LANG_UNKNOWN;
	ConfigureParams.Keyboard.nLanguage = TOS_LANG_UNKNOWN;
	strcpy(ConfigureParams.Keyboard.szMappingFileName, "");

	/* Set defaults for Shortcuts */
	ConfigureParams.Shortcut.withoutModifier[SHORTCUT_OPTIONS] = SDLK_F12;
	ConfigureParams.Shortcut.withoutModifier[SHORTCUT_FULLSCREEN] = SDLK_F11;
	ConfigureParams.Shortcut.withoutModifier[SHORTCUT_PAUSE] = SDLK_PAUSE;

	ConfigureParams.Shortcut.withModifier[SHORTCUT_DEBUG] = SDLK_PAUSE;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_OPTIONS] = SDLK_o;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_FULLSCREEN] = SDLK_f;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_BORDERS] = SDLK_b;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_MOUSEGRAB] = SDLK_m;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_COLDRESET] = SDLK_c;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_WARMRESET] = SDLK_r;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_SCREENSHOT] = SDLK_g;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_BOSSKEY] = SDLK_i;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_CURSOREMU] = SDLK_j;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_FASTFORWARD] = SDLK_x;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_RECANIM] = SDLK_a;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_RECSOUND] = SDLK_y;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_SOUND] = SDLK_s;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_QUIT] = SDLK_q;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_LOADMEM] = SDLK_l;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_SAVEMEM] = SDLK_k;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_INSERTDISKA] = SDLK_d;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_JOY_0] = SDLK_F1;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_JOY_1] = SDLK_F2;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_PAD_A] = SDLK_F3;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_PAD_B] = SDLK_F4;

	/* Set defaults for Memory */
	ConfigureParams.Memory.STRamSize_KB = 1024;	/* 1 MiB */
	ConfigureParams.Memory.TTRamSize_KB = 0;	/* disabled */
	ConfigureParams.Memory.bAutoSave = false;
	File_MakePathBuf(ConfigureParams.Memory.szMemoryCaptureFileName,
	                 sizeof(ConfigureParams.Memory.szMemoryCaptureFileName),
	                 psHomeDir, "hatari", "sav");
	File_MakePathBuf(ConfigureParams.Memory.szAutoSaveFileName,
	                 sizeof(ConfigureParams.Memory.szAutoSaveFileName),
	                 psHomeDir, "auto", "sav");

	/* Set defaults for Printer */
	ConfigureParams.Printer.bEnablePrinting = false;
	File_MakePathBuf(ConfigureParams.Printer.szPrintToFileName,
	                 sizeof(ConfigureParams.Printer.szPrintToFileName),
	                 psHomeDir, "hatari", "prn");

	/* Set defaults for MFP RS232 (ST/MegaST/STE/MegaSTE/TT) */
	ConfigureParams.RS232.bEnableRS232 = false;
	strcpy(ConfigureParams.RS232.szOutFileName, "/dev/modem");
	strcpy(ConfigureParams.RS232.szInFileName, "/dev/modem");
	/* Set defaults for SCC RS232 ( MegaSTE/TT/Falcon) */
	ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_A_SERIAL] = false;
	strcpy(ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_A_SERIAL], "/dev/modem");
	strcpy(ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_A_SERIAL], "/dev/modem");
	ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_A_LAN] = false;
	strcpy(ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_A_LAN], "/dev/modem");
	strcpy(ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_A_LAN], "/dev/modem");
	ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_B] = false;
	strcpy(ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_B], "/dev/modem");
	strcpy(ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_B], "/dev/modem");

	/* Set defaults for MIDI */
	ConfigureParams.Midi.bEnableMidi = false;
	strcpy(ConfigureParams.Midi.sMidiInFileName, "/dev/snd/midiC1D0");
	strcpy(ConfigureParams.Midi.sMidiOutFileName, "/dev/snd/midiC1D0");
	strcpy(ConfigureParams.Midi.sMidiInPortName, "Off");
	strcpy(ConfigureParams.Midi.sMidiOutPortName, "Off");

	/* Set defaults for Screen */
	ConfigureParams.Screen.bFullScreen = false;
	ConfigureParams.Screen.bKeepResolution = true;
	ConfigureParams.Screen.bResizable = true;
	ConfigureParams.Screen.nFrameSkips = AUTO_FRAMESKIP_LIMIT;
	ConfigureParams.Screen.bAllowOverscan = true;
	ConfigureParams.Screen.nSpec512Threshold = 1;
	ConfigureParams.Screen.bAspectCorrect = true;
	ConfigureParams.Screen.nMonitorType = MONITOR_TYPE_RGB;
	ConfigureParams.Screen.bUseExtVdiResolutions = false;
	ConfigureParams.Screen.nVdiWidth = 640;
	ConfigureParams.Screen.nVdiHeight = 480;
	ConfigureParams.Screen.nVdiColors = GEMCOLOR_16;
	ConfigureParams.Screen.bMouseWarp = true;
	ConfigureParams.Screen.bShowStatusbar = true;
	ConfigureParams.Screen.bShowDriveLed = true;
	ConfigureParams.Screen.bCrop = false;
	/* use approximately similar Hatari framebuffer/window size
	 * on all resolutions (like real Atari monitor would do) by
	 * allowing ST low resolution to be doubled (see screen.c)
	 */
	ConfigureParams.Screen.nMaxWidth = 2*NUM_VISIBLE_LINE_PIXELS;
	ConfigureParams.Screen.nMaxHeight = 2*NUM_VISIBLE_LINES+STATUSBAR_MAX_HEIGHT;
	ConfigureParams.Screen.bForceMax = false;
	ConfigureParams.Screen.DisableVideo = false;
	ConfigureParams.Screen.nZoomFactor = 1.0;
	ConfigureParams.Screen.bUseSdlRenderer = true;
	ConfigureParams.Screen.bUseVsync = false;
#if HAVE_LIBPNG
	ConfigureParams.Screen.ScreenShotFormat = SCREEN_SNAPSHOT_PNG;
#else
	ConfigureParams.Screen.ScreenShotFormat = SCREEN_SNAPSHOT_BMP;
#endif

	/* Set defaults for Sound */
	ConfigureParams.Sound.bEnableMicrophone = true;
	ConfigureParams.Sound.bEnableSound = true;
	ConfigureParams.Sound.bEnableSoundSync = false;
	ConfigureParams.Sound.nPlaybackFreq = 44100;
	File_MakePathBuf(ConfigureParams.Sound.szYMCaptureFileName,
	                 sizeof(ConfigureParams.Sound.szYMCaptureFileName),
	                 psWorkingDir, "hatari", "wav");
	ConfigureParams.Sound.SdlAudioBufferSize = 0;
	ConfigureParams.Sound.YmVolumeMixing = YM_TABLE_MIXING;

	/* Set defaults for Rom */
	File_MakePathBuf(ConfigureParams.Rom.szTosImageFileName,
	                 sizeof(ConfigureParams.Rom.szTosImageFileName),
	                 Paths_GetDataDir(), "tos", "img");
	ConfigureParams.Rom.bPatchTos = true;
	strcpy(ConfigureParams.Rom.szCartridgeImageFileName, "");

	/* Set defaults for Lilo */
	strcpy(ConfigureParams.Lilo.szCommandLine,
	       "root=/dev/ram video=atafb:vga16 load_ramdisk=1");
	File_MakePathBuf(ConfigureParams.Lilo.szKernelFileName,
	                 sizeof(ConfigureParams.Lilo.szKernelFileName),
	                 Paths_GetDataDir(), "vmlinuz", NULL);
	File_MakePathBuf(ConfigureParams.Lilo.szRamdiskFileName,
	                 sizeof(ConfigureParams.Lilo.szRamdiskFileName),
	                 Paths_GetDataDir(), "initrd", NULL);
	ConfigureParams.Lilo.szKernelSymbols[0] = '\0';
	ConfigureParams.Lilo.bRamdiskToFastRam = true;
	ConfigureParams.Lilo.bKernelToFastRam = true;
	ConfigureParams.Lilo.bHaltOnReboot = true;

	/* Set defaults for System */
	ConfigureParams.System.nMachineType = MACHINE_ST;
	ConfigureParams.System.nCpuLevel = 0;
	ConfigureParams.System.nCpuFreq = 8;	nCpuFreqShift = 0;
	ConfigureParams.System.nDSPType = DSP_TYPE_NONE;
	ConfigureParams.System.nVMEType = VME_TYPE_DUMMY; /* for TOS MegaSTE detection */
	ConfigureParams.System.nRtcYear = 0;
	ConfigureParams.System.bAddressSpace24 = true;
	ConfigureParams.System.n_FPUType = FPU_NONE;
	ConfigureParams.System.bCompatibleFPU = true; /* JIT */
	ConfigureParams.System.bSoftFloatFPU = false;
	ConfigureParams.System.bMMU = false;
	ConfigureParams.System.bCycleExactCpu = true;
	ConfigureParams.System.VideoTimingMode = VIDEO_TIMING_MODE_WS3;
	ConfigureParams.System.bCompatibleCpu = true;
	ConfigureParams.System.bBlitter = false;
	ConfigureParams.System.bPatchTimerD = false;
	ConfigureParams.System.bFastBoot = false;
	ConfigureParams.System.bFastForward = false;

	/* Set defaults for Video */
#if HAVE_LIBPNG
	ConfigureParams.Video.AviRecordVcodec = AVI_RECORD_VIDEO_CODEC_PNG;
#else
	ConfigureParams.Video.AviRecordVcodec = AVI_RECORD_VIDEO_CODEC_BMP;
#endif
	ConfigureParams.Video.AviRecordFps = 0;			/* automatic FPS */
	File_MakePathBuf(ConfigureParams.Video.AviRecordFile,
	                 sizeof(ConfigureParams.Video.AviRecordFile),
	                 psWorkingDir, "hatari", "avi");

	/* Initialize the configuration file name */
	if (File_MakePathBuf(sConfigFileName, sizeof(sConfigFileName),
	                     psHomeDir, "hatari", "cfg"))
	{
		strcpy(sConfigFileName, "hatari.cfg");
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Copy details from configuration structure into global variables for system,
 * clean file names, etc...  Called from main.c and dialog.c files.
 */
void Configuration_Apply(bool bReset)
{
	int i;
	int size;

	if (bReset)
	{
		/* Set resolution change */
		bUseVDIRes = ConfigureParams.Screen.bUseExtVdiResolutions;
		bUseHighRes = ((!bUseVDIRes) && ConfigureParams.Screen.nMonitorType == MONITOR_TYPE_MONO)
			|| (bUseVDIRes && ConfigureParams.Screen.nVdiColors == GEMCOLOR_2);
		if (bUseHighRes)
		{
			STRes = ST_HIGH_RES;
		}
		if (bUseVDIRes)
		{
			/* rest of VDI setup done in TOS init */
			bVdiAesIntercept = true;
		}
	}
	if (ConfigureParams.Screen.nFrameSkips < AUTO_FRAMESKIP_LIMIT)
	{
		nFrameSkips = ConfigureParams.Screen.nFrameSkips;
	}

	/* Check/convert ST RAM size in KB */
	size = STMemory_RAM_Validate_Size_KB ( ConfigureParams.Memory.STRamSize_KB );
	if ( size < 0 )
	{
		size = 1024;
		Log_Printf(LOG_WARN, "Unsupported %d KB ST-RAM amount, defaulting to %d KB\n",
			   ConfigureParams.Memory.STRamSize_KB, size);
	}
	ConfigureParams.Memory.STRamSize_KB = size;
	STMemory_Init ( ConfigureParams.Memory.STRamSize_KB * 1024 );

	/* Update variables depending on the new CPU Freq (to do before other ClocksTimings_xxx functions) */
	Configuration_ChangeCpuFreq ( ConfigureParams.System.nCpuFreq );

	/* Init clocks for this machine */
	ClocksTimings_InitMachine ( ConfigureParams.System.nMachineType );

	/* Set video timings for this machine */
	Video_SetTimings ( ConfigureParams.System.nMachineType , ConfigureParams.System.VideoTimingMode );

	/* Sound settings */
	/* SDL sound buffer in ms (or 0 for using the default value from SDL) */
	SdlAudioBufferSize = ConfigureParams.Sound.SdlAudioBufferSize;
	if (SdlAudioBufferSize < 10 && SdlAudioBufferSize != 0)
		SdlAudioBufferSize = 10;		/* min of 10 ms */
	else if (SdlAudioBufferSize > 100)
		SdlAudioBufferSize = 100;		/* max of 100 ms */

	/* Set playback frequency */
	Audio_SetOutputAudioFreq(ConfigureParams.Sound.nPlaybackFreq);

	/* YM Mixing */
	if ( ( ConfigureParams.Sound.YmVolumeMixing != YM_LINEAR_MIXING )
	  && ( ConfigureParams.Sound.YmVolumeMixing != YM_TABLE_MIXING  )
	  && ( ConfigureParams.Sound.YmVolumeMixing != YM_MODEL_MIXING ) )
		ConfigureParams.Sound.YmVolumeMixing = YM_TABLE_MIXING;

	YmVolumeMixing = ConfigureParams.Sound.YmVolumeMixing;
	Sound_SetYmVolumeMixing();

	/* Falcon : update clocks values if sound freq changed  */
	if ( Config_IsMachineFalcon() )
		Crossbar_Recalculate_Clocks_Cycles();

	/* Check/constrain CPU settings and change corresponding
	 * cpu_model/cpu_compatible/cpu_cycle_exact/... variables
	 */
//fprintf (stderr,"M68000_CheckCpuSettings conf 1\n" );
	M68000_CheckCpuSettings();
//fprintf (stderr,"M68000_CheckCpuSettings conf 2\n" );

	/* Clean file and directory names */
	File_MakeAbsoluteName(ConfigureParams.Rom.szTosImageFileName);
	if (strlen(ConfigureParams.Rom.szCartridgeImageFileName) > 0)
		File_MakeAbsoluteName(ConfigureParams.Rom.szCartridgeImageFileName);
	if (strlen(ConfigureParams.Lilo.szKernelFileName) > 0)
		File_MakeAbsoluteName(ConfigureParams.Lilo.szKernelFileName);
	if (strlen(ConfigureParams.Lilo.szKernelSymbols) > 0)
		File_MakeAbsoluteName(ConfigureParams.Lilo.szKernelSymbols);
	if (strlen(ConfigureParams.Lilo.szRamdiskFileName) > 0)
		File_MakeAbsoluteName(ConfigureParams.Lilo.szRamdiskFileName);
	File_CleanFileName(ConfigureParams.HardDisk.szHardDiskDirectories[0]);
	File_MakeAbsoluteName(ConfigureParams.HardDisk.szHardDiskDirectories[0]);
	File_MakeAbsoluteName(ConfigureParams.Memory.szMemoryCaptureFileName);
	File_MakeAbsoluteName(ConfigureParams.Sound.szYMCaptureFileName);
	if (strlen(ConfigureParams.Keyboard.szMappingFileName) > 0)
		File_MakeAbsoluteName(ConfigureParams.Keyboard.szMappingFileName);
	File_MakeAbsoluteName(ConfigureParams.Video.AviRecordFile);
	for (i = 0; i < MAX_ACSI_DEVS; i++)
	{
		File_MakeAbsoluteName(ConfigureParams.Acsi[i].sDeviceFile);
	}
	for (i = 0; i < MAX_SCSI_DEVS; i++)
	{
		File_MakeAbsoluteName(ConfigureParams.Scsi[i].sDeviceFile);
	}
	for (i = 0; i < MAX_IDE_DEVS; i++)
	{
		File_MakeAbsoluteName(ConfigureParams.Ide[i].sDeviceFile);
	}

	/* make path names absolute, but handle special file names */
	File_MakeAbsoluteSpecialName(ConfigureParams.Log.sLogFileName);
	File_MakeAbsoluteSpecialName(ConfigureParams.Log.sTraceFileName);
	File_MakeAbsoluteSpecialName(ConfigureParams.RS232.szInFileName);
	File_MakeAbsoluteSpecialName(ConfigureParams.RS232.szOutFileName);
	File_MakeAbsoluteSpecialName(ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_A_SERIAL]);
	File_MakeAbsoluteSpecialName(ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_A_SERIAL]);
	File_MakeAbsoluteSpecialName(ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_A_LAN]);
	File_MakeAbsoluteSpecialName(ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_A_LAN]);
	File_MakeAbsoluteSpecialName(ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_B]);
	File_MakeAbsoluteSpecialName(ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_B]);
	File_MakeAbsoluteSpecialName(ConfigureParams.Midi.sMidiInFileName);
	File_MakeAbsoluteSpecialName(ConfigureParams.Midi.sMidiOutFileName);
	File_MakeAbsoluteSpecialName(ConfigureParams.Printer.szPrintToFileName);

	/* Enable/disable floppy drives */
	FDC_Drive_Set_Enable ( 0 , ConfigureParams.DiskImage.EnableDriveA );
	FDC_Drive_Set_Enable ( 1 , ConfigureParams.DiskImage.EnableDriveB );
	FDC_Drive_Set_NumberOfHeads ( 0 , ConfigureParams.DiskImage.DriveA_NumberOfHeads );
	FDC_Drive_Set_NumberOfHeads ( 1 , ConfigureParams.DiskImage.DriveB_NumberOfHeads );

	/* Update disassembler */
	Disasm_Init();

#if ENABLE_DSP_EMU
	/* Enable DSP ? */
	if ( ConfigureParams.System.nDSPType == DSP_TYPE_EMU )
		DSP_Enable ();
	else
		DSP_Disable ();
#endif
}


/*-----------------------------------------------------------------------*/
/**
 * Load a settings section from the configuration file.
 */
static int Configuration_LoadSection(const char *pFilename, const struct Config_Tag configs[], const char *pSection)
{
	int ret;

	ret = input_config(pFilename, configs, pSection);

	if (ret < 0)
		Log_Printf(LOG_ERROR, "cannot load configuration file %s (section %s).\n",
		        pFilename, pSection);

	return ret;
}


/*-----------------------------------------------------------------------*/
/**
 * Load program setting from configuration file. If psFileName is NULL, use
 * the configuration file given in configuration / last selected by user.
 */
void Configuration_Load(const char *psFileName)
{
	if (psFileName == NULL)
		psFileName = sConfigFileName;

	if (!File_Exists(psFileName))
	{
		Log_Printf(LOG_DEBUG, "Configuration file %s not found.\n", psFileName);
		return;
	}

	/* Try to load information from old config files */
	nOldMachineType = -1;
	Configuration_LoadSection(psFileName, configs_System_Old, "[System]");
	switch (nOldMachineType)
	{
	 case 0:
		if (!bOldRealTimeClock)
			ConfigureParams.System.nMachineType = MACHINE_ST;
		else
			ConfigureParams.System.nMachineType = MACHINE_MEGA_ST;
		break;
	 case 1:
		ConfigureParams.System.nMachineType = MACHINE_STE;
		break;
	 case 2:
		ConfigureParams.System.nMachineType = MACHINE_TT;
		break;
	 case 3:
		ConfigureParams.System.nMachineType = MACHINE_FALCON;
		break;
	}
	Configuration_LoadSection(psFileName, configs_HardDisk_Old, "[HardDisk]");

	/* Now the regular loading of the sections:
	 * Start with Log so that logging works as early as possible */
	Configuration_LoadSection(psFileName, configs_Log, "[Log]");
	Log_SetLevels();

	Configuration_LoadSection(psFileName, configs_Debugger, "[Debugger]");
	Configuration_LoadSection(psFileName, configs_Screen, "[Screen]");
	Configuration_LoadSection(psFileName, configs_Joystick0, "[Joystick0]");
	Configuration_LoadSection(psFileName, configs_Joystick1, "[Joystick1]");
	Configuration_LoadSection(psFileName, configs_Joystick2, "[Joystick2]");
	Configuration_LoadSection(psFileName, configs_Joystick3, "[Joystick3]");
	Configuration_LoadSection(psFileName, configs_Joystick4, "[Joystick4]");
	Configuration_LoadSection(psFileName, configs_Joystick5, "[Joystick5]");
	Configuration_LoadSection(psFileName, configs_Keyboard, "[Keyboard]");
	Configuration_LoadSection(psFileName, configs_ShortCutWithMod, "[KeyShortcutsWithMod]");
	Configuration_LoadSection(psFileName, configs_ShortCutWithoutMod, "[KeyShortcutsWithoutMod]");
	Configuration_LoadSection(psFileName, configs_Sound, "[Sound]");
	Configuration_LoadSection(psFileName, configs_Memory, "[Memory]");
	Configuration_LoadSection(psFileName, configs_Floppy, "[Floppy]");
	Configuration_LoadSection(psFileName, configs_HardDisk, "[HardDisk]");
	Configuration_LoadSection(psFileName, configs_Acsi, "[ACSI]");
	Configuration_LoadSection(psFileName, configs_Scsi, "[SCSI]");
	Configuration_LoadSection(psFileName, configs_Ide, "[IDE]");
	Configuration_LoadSection(psFileName, configs_Rom, "[ROM]");
	Configuration_LoadSection(psFileName, configs_Lilo, "[LILO]");
	Configuration_LoadSection(psFileName, configs_Rs232, "[RS232]");
	Configuration_LoadSection(psFileName, configs_Printer, "[Printer]");
	Configuration_LoadSection(psFileName, configs_Midi, "[Midi]");
	Configuration_LoadSection(psFileName, configs_System, "[System]");
	Configuration_LoadSection(psFileName, configs_Video, "[Video]");
}


/*-----------------------------------------------------------------------*/
/**
 * Save a settings section to configuration file
 */
static int Configuration_SaveSection(const char *pFilename, const struct Config_Tag configs[], const char *pSection)
{
	int ret;

	ret = update_config(pFilename, configs, pSection);

	if (ret < 0)
		Log_Printf(LOG_ERROR, "cannot save configuration file %s (section %s)\n",
			   pFilename, pSection);

	return ret;
}


/*-----------------------------------------------------------------------*/
/**
 * Save program setting to configuration file
 */
void Configuration_Save(void)
{
	if (Configuration_SaveSection(sConfigFileName, configs_Log, "[Log]") < 0)
	{
		Log_AlertDlg(LOG_ERROR, "Error saving config file.");
		return;
	}
	Configuration_SaveSection(sConfigFileName, configs_Debugger, "[Debugger]");
	Configuration_SaveSection(sConfigFileName, configs_Screen, "[Screen]");
	Configuration_SaveSection(sConfigFileName, configs_Joystick0, "[Joystick0]");
	Configuration_SaveSection(sConfigFileName, configs_Joystick1, "[Joystick1]");
	Configuration_SaveSection(sConfigFileName, configs_Joystick2, "[Joystick2]");
	Configuration_SaveSection(sConfigFileName, configs_Joystick3, "[Joystick3]");
	Configuration_SaveSection(sConfigFileName, configs_Joystick4, "[Joystick4]");
	Configuration_SaveSection(sConfigFileName, configs_Joystick5, "[Joystick5]");
	Configuration_SaveSection(sConfigFileName, configs_Keyboard, "[Keyboard]");
	Configuration_SaveSection(sConfigFileName, configs_ShortCutWithMod, "[KeyShortcutsWithMod]");
	Configuration_SaveSection(sConfigFileName, configs_ShortCutWithoutMod, "[KeyShortcutsWithoutMod]");
	Configuration_SaveSection(sConfigFileName, configs_Sound, "[Sound]");
	Configuration_SaveSection(sConfigFileName, configs_Memory, "[Memory]");
	Configuration_SaveSection(sConfigFileName, configs_Floppy, "[Floppy]");
	Configuration_SaveSection(sConfigFileName, configs_HardDisk, "[HardDisk]");
	Configuration_SaveSection(sConfigFileName, configs_Acsi, "[ACSI]");
	Configuration_SaveSection(sConfigFileName, configs_Scsi, "[SCSI]");
	Configuration_SaveSection(sConfigFileName, configs_Ide, "[IDE]");
	Configuration_SaveSection(sConfigFileName, configs_Rom, "[ROM]");
	Configuration_SaveSection(sConfigFileName, configs_Lilo, "[LILO]");
	Configuration_SaveSection(sConfigFileName, configs_Rs232, "[RS232]");
	Configuration_SaveSection(sConfigFileName, configs_Printer, "[Printer]");
	Configuration_SaveSection(sConfigFileName, configs_Midi, "[Midi]");
	Configuration_SaveSection(sConfigFileName, configs_System, "[System]");
	Configuration_SaveSection(sConfigFileName, configs_Video, "[Video]");
}


/*-----------------------------------------------------------------------*/
/**
 * Save/restore snapshot of configuration variables
 * ('MemorySnapShot_Store' handles type)
 */
void Configuration_MemorySnapShot_Capture(bool bSave)
{
	int i;

	MemorySnapShot_Store(ConfigureParams.Rom.szTosImageFileName, sizeof(ConfigureParams.Rom.szTosImageFileName));
	MemorySnapShot_Store(ConfigureParams.Rom.szCartridgeImageFileName, sizeof(ConfigureParams.Rom.szCartridgeImageFileName));

	MemorySnapShot_Store(ConfigureParams.Lilo.szKernelFileName, sizeof(ConfigureParams.Lilo.szKernelFileName));
	MemorySnapShot_Store(ConfigureParams.Lilo.szRamdiskFileName, sizeof(ConfigureParams.Lilo.szRamdiskFileName));

	MemorySnapShot_Store(&ConfigureParams.Memory.STRamSize_KB, sizeof(ConfigureParams.Memory.STRamSize_KB));
	MemorySnapShot_Store(&ConfigureParams.Memory.TTRamSize_KB, sizeof(ConfigureParams.Memory.TTRamSize_KB));

	MemorySnapShot_Store(&ConfigureParams.DiskImage.szDiskFileName[0], sizeof(ConfigureParams.DiskImage.szDiskFileName[0]));
	MemorySnapShot_Store(&ConfigureParams.DiskImage.szDiskZipPath[0], sizeof(ConfigureParams.DiskImage.szDiskZipPath[0]));
	MemorySnapShot_Store(&ConfigureParams.DiskImage.EnableDriveA, sizeof(ConfigureParams.DiskImage.EnableDriveA));
	MemorySnapShot_Store(&ConfigureParams.DiskImage.DriveA_NumberOfHeads, sizeof(ConfigureParams.DiskImage.DriveA_NumberOfHeads));
	MemorySnapShot_Store(&ConfigureParams.DiskImage.szDiskFileName[1], sizeof(ConfigureParams.DiskImage.szDiskFileName[1]));
	MemorySnapShot_Store(&ConfigureParams.DiskImage.szDiskZipPath[1], sizeof(ConfigureParams.DiskImage.szDiskZipPath[1]));
	MemorySnapShot_Store(&ConfigureParams.DiskImage.EnableDriveB, sizeof(ConfigureParams.DiskImage.EnableDriveB));
	MemorySnapShot_Store(&ConfigureParams.DiskImage.DriveB_NumberOfHeads, sizeof(ConfigureParams.DiskImage.DriveB_NumberOfHeads));

	MemorySnapShot_Store(&ConfigureParams.HardDisk.bUseHardDiskDirectories, sizeof(ConfigureParams.HardDisk.bUseHardDiskDirectories));
	MemorySnapShot_Store(ConfigureParams.HardDisk.szHardDiskDirectories[DRIVE_C], sizeof(ConfigureParams.HardDisk.szHardDiskDirectories[DRIVE_C]));
	for (i = 0; i < MAX_ACSI_DEVS; i++)
	{
		MemorySnapShot_Store(&ConfigureParams.Acsi[i].bUseDevice, sizeof(ConfigureParams.Acsi[i].bUseDevice));
		MemorySnapShot_Store(ConfigureParams.Acsi[i].sDeviceFile, sizeof(ConfigureParams.Acsi[i].sDeviceFile));
	}
	for (i = 0; i < MAX_SCSI_DEVS; i++)
	{
		MemorySnapShot_Store(&ConfigureParams.Scsi[i].bUseDevice, sizeof(ConfigureParams.Scsi[i].bUseDevice));
		MemorySnapShot_Store(ConfigureParams.Scsi[i].sDeviceFile, sizeof(ConfigureParams.Scsi[i].sDeviceFile));
	}
	for (i = 0; i < MAX_IDE_DEVS; i++)
	{
		MemorySnapShot_Store(&ConfigureParams.Ide[i].bUseDevice, sizeof(ConfigureParams.Ide[i].bUseDevice));
		MemorySnapShot_Store(&ConfigureParams.Ide[i].nByteSwap, sizeof(ConfigureParams.Ide[i].nByteSwap));
		MemorySnapShot_Store(ConfigureParams.Ide[i].sDeviceFile, sizeof(ConfigureParams.Ide[i].sDeviceFile));
	}

	MemorySnapShot_Store(&ConfigureParams.Screen.nMonitorType, sizeof(ConfigureParams.Screen.nMonitorType));
	MemorySnapShot_Store(&ConfigureParams.Screen.bUseExtVdiResolutions, sizeof(ConfigureParams.Screen.bUseExtVdiResolutions));
	MemorySnapShot_Store(&ConfigureParams.Screen.nVdiWidth, sizeof(ConfigureParams.Screen.nVdiWidth));
	MemorySnapShot_Store(&ConfigureParams.Screen.nVdiHeight, sizeof(ConfigureParams.Screen.nVdiHeight));
	MemorySnapShot_Store(&ConfigureParams.Screen.nVdiColors, sizeof(ConfigureParams.Screen.nVdiColors));

	MemorySnapShot_Store(&ConfigureParams.System.nCpuLevel, sizeof(ConfigureParams.System.nCpuLevel));
	MemorySnapShot_Store(&ConfigureParams.System.nCpuFreq, sizeof(ConfigureParams.System.nCpuFreq));
	MemorySnapShot_Store(&ConfigureParams.System.bCompatibleCpu, sizeof(ConfigureParams.System.bCompatibleCpu));
	MemorySnapShot_Store(&ConfigureParams.System.nMachineType, sizeof(ConfigureParams.System.nMachineType));
	MemorySnapShot_Store(&ConfigureParams.System.bBlitter, sizeof(ConfigureParams.System.bBlitter));
	MemorySnapShot_Store(&ConfigureParams.System.nDSPType, sizeof(ConfigureParams.System.nDSPType));
	/* TODO: enable after VME/SCU interrupt emulation is implemented
	MemorySnapShot_Store(&ConfigureParams.System.nVMEType, sizeof(ConfigureParams.System.nVMEType));
	 */
	MemorySnapShot_Store(&ConfigureParams.System.bPatchTimerD, sizeof(ConfigureParams.System.bPatchTimerD));
	MemorySnapShot_Store(&ConfigureParams.System.bAddressSpace24, sizeof(ConfigureParams.System.bAddressSpace24));

	MemorySnapShot_Store(&ConfigureParams.System.bCycleExactCpu, sizeof(ConfigureParams.System.bCycleExactCpu));
	MemorySnapShot_Store(&ConfigureParams.System.n_FPUType, sizeof(ConfigureParams.System.n_FPUType));
	MemorySnapShot_Store(&ConfigureParams.System.bCompatibleFPU, sizeof(ConfigureParams.System.bCompatibleFPU));
	MemorySnapShot_Store(&ConfigureParams.System.bMMU, sizeof(ConfigureParams.System.bMMU));

	MemorySnapShot_Store(&MachineClocks,sizeof(MachineClocks));

	MemorySnapShot_Store(&ConfigureParams.DiskImage.FastFloppy, sizeof(ConfigureParams.DiskImage.FastFloppy));

	if (!bSave)
		Configuration_Apply(true);
}



/*-----------------------------------------------------------------------*/
/**
 * This function should be called each time the CPU freq is changed.
 * It will update the main configuration, as well as the corresponding
 * value for nCpuFreqShift
 *
 * In case the new CPU freq is different from the current CPU freq, we
 * also call MClocksTimings_UpdateCpuFreqEmul and 68000_ChangeCpuFreq
 * to update some low level hardware related values
 */
void Configuration_ChangeCpuFreq ( int CpuFreq_new )
{
	int	CpuFreq_old = ConfigureParams.System.nCpuFreq;

//fprintf ( stderr , "changing cpu freq %d -> %d\n" , ConfigureParams.System.nCpuFreq , CpuFreq_new );

	/* In case value is not exactly 8, 16 or 32, then we change it so */
	if ( CpuFreq_new < 12 )
	{
		ConfigureParams.System.nCpuFreq = 8;
		nCpuFreqShift = 0;
	}
	else if ( CpuFreq_new > 26 )
	{
		ConfigureParams.System.nCpuFreq = 32;
		nCpuFreqShift = 2;
	}
	else
	{
		ConfigureParams.System.nCpuFreq = 16;
		nCpuFreqShift = 1;
	}

	ClocksTimings_UpdateCpuFreqEmul ( ConfigureParams.System.nMachineType , nCpuFreqShift );

	if ( CpuFreq_old != CpuFreq_new )
	{
		M68000_ChangeCpuFreq();
	}
}

#ifdef EMSCRIPTEN
void Configuration_ChangeMemory ( int RamSizeKb )
{
	ConfigureParams.Memory.STRamSize_KB = RamSizeKb;
	int size = STMemory_RAM_Validate_Size_KB ( ConfigureParams.Memory.STRamSize_KB );
	if ( size < 0 )
	{
		size = 1024;
		Log_Printf(LOG_WARN, "Unsupported %d KB ST-RAM amount, defaulting to %d KB\n",
			   ConfigureParams.Memory.STRamSize_KB, size);
	}
	ConfigureParams.Memory.STRamSize_KB = size;
	STMemory_Init ( ConfigureParams.Memory.STRamSize_KB * 1024 );
}

void Configuration_ChangeTos ( const char* szTosImageFileName )
{
		if(strlen(szTosImageFileName)<4096){
			strcpy(ConfigureParams.Rom.szTosImageFileName,szTosImageFileName);
		}
}

void Configuration_ChangeSystem ( int nMachineType )
{
	switch (nMachineType)
	{
	 case 0:
		if (!bOldRealTimeClock)
			ConfigureParams.System.nMachineType = MACHINE_ST;
		else
			ConfigureParams.System.nMachineType = MACHINE_MEGA_ST;
		break;
	 case 1:
		ConfigureParams.System.nMachineType = MACHINE_STE;
		break;
	 case 2:
		ConfigureParams.System.nMachineType = MACHINE_TT;
		break;
	 case 3:
		ConfigureParams.System.nMachineType = MACHINE_FALCON;
		break;
	}
}

void Configuration_ChangeUseHardDiskDirectories ( bool bUseHardDiskDirectories )
{
	ConfigureParams.HardDisk.bUseHardDiskDirectories=bUseHardDiskDirectories;
}

void Configuration_ChangeFastForward ( bool bFastForwardActive )
{
	ConfigureParams.System.bFastForward=bFastForwardActive;
}

#endif
