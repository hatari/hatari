/*
  Hatari - configuration.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Configuration File

  The configuration file is now stored in an ASCII format to allow the user
  to edit the file manually.
*/
const char Configuration_fileid[] = "Hatari configuration.c : " __DATE__ " " __TIME__;

#include <SDL_keyboard.h>

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
#include "fdc.h"
#include "dsp.h"
#include "joy.h"


CNF_PARAMS ConfigureParams;                 /* List of configuration for the emulator */
char sConfigFileName[FILENAME_MAX];         /* Stores the name of the configuration file */


/* Used to load/save logging options */
static const struct Config_Tag configs_Log[] =
{
	{ "sLogFileName", String_Tag, ConfigureParams.Log.sLogFileName },
	{ "sTraceFileName", String_Tag, ConfigureParams.Log.sTraceFileName },
	{ "nExceptionDebugMask", Int_Tag, &ConfigureParams.Log.nExceptionDebugMask },
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
	{ "nDisasmLines", Int_Tag, &ConfigureParams.Debugger.nDisasmLines },
	{ "nMemdumpLines", Int_Tag, &ConfigureParams.Debugger.nMemdumpLines },
	{ "nDisasmOptions", Int_Tag, &ConfigureParams.Debugger.nDisasmOptions },
	{ "bDisasmUAE", Bool_Tag, &ConfigureParams.Debugger.bDisasmUAE },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save screen options */
static const struct Config_Tag configs_Screen[] =
{
	{ "nMonitorType", Int_Tag, &ConfigureParams.Screen.nMonitorType },
	{ "nFrameSkips", Int_Tag, &ConfigureParams.Screen.nFrameSkips },
	{ "bFullScreen", Bool_Tag, &ConfigureParams.Screen.bFullScreen },
	{ "bKeepResolution", Bool_Tag, &ConfigureParams.Screen.bKeepResolution },
#if !WITH_SDL2
	{ "bKeepResolutionST", Bool_Tag, &ConfigureParams.Screen.bKeepResolutionST },
#endif
	{ "bAllowOverscan", Bool_Tag, &ConfigureParams.Screen.bAllowOverscan },
	{ "nSpec512Threshold", Int_Tag, &ConfigureParams.Screen.nSpec512Threshold },
	{ "nForceBpp", Int_Tag, &ConfigureParams.Screen.nForceBpp },
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
#if WITH_SDL2
	{ "nRenderScaleQuality", Int_Tag, &ConfigureParams.Screen.nRenderScaleQuality },
	{ "bUseVsync", Int_Tag, &ConfigureParams.Screen.bUseVsync },
#endif
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save joystick 0 options */
#if !WITH_SDL2
static const struct Config_Tag configs_Joystick0_Sdl1[] =
{
	{ "nKeyCodeUp", Int_Tag, &ConfigureParams.Joysticks.Joy[0].nKeyCodeUp },
	{ "nKeyCodeDown", Int_Tag, &ConfigureParams.Joysticks.Joy[0].nKeyCodeDown },
	{ "nKeyCodeLeft", Int_Tag, &ConfigureParams.Joysticks.Joy[0].nKeyCodeLeft },
	{ "nKeyCodeRight", Int_Tag, &ConfigureParams.Joysticks.Joy[0].nKeyCodeRight },
	{ "nKeyCodeFire", Int_Tag, &ConfigureParams.Joysticks.Joy[0].nKeyCodeFire },
	{ NULL , Error_Tag, NULL }
};
#endif
static const struct Config_Tag configs_Joystick0[] =
{
	{ "nJoystickMode", Int_Tag, &ConfigureParams.Joysticks.Joy[0].nJoystickMode },
	{ "bEnableAutoFire", Bool_Tag, &ConfigureParams.Joysticks.Joy[0].bEnableAutoFire },
	{ "bEnableJumpOnFire2", Bool_Tag, &ConfigureParams.Joysticks.Joy[0].bEnableJumpOnFire2 },
	{ "nJoyId", Int_Tag, &ConfigureParams.Joysticks.Joy[0].nJoyId },
	{ "kUp", Key_Tag, &ConfigureParams.Joysticks.Joy[0].nKeyCodeUp },
	{ "kDown", Key_Tag, &ConfigureParams.Joysticks.Joy[0].nKeyCodeDown },
	{ "kLeft", Key_Tag, &ConfigureParams.Joysticks.Joy[0].nKeyCodeLeft },
	{ "kRight", Key_Tag, &ConfigureParams.Joysticks.Joy[0].nKeyCodeRight },
	{ "kFire", Key_Tag, &ConfigureParams.Joysticks.Joy[0].nKeyCodeFire },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save joystick 1 options */
#if !WITH_SDL2
static const struct Config_Tag configs_Joystick1_Sdl1[] =
{
	{ "nKeyCodeUp", Int_Tag, &ConfigureParams.Joysticks.Joy[1].nKeyCodeUp },
	{ "nKeyCodeDown", Int_Tag, &ConfigureParams.Joysticks.Joy[1].nKeyCodeDown },
	{ "nKeyCodeLeft", Int_Tag, &ConfigureParams.Joysticks.Joy[1].nKeyCodeLeft },
	{ "nKeyCodeRight", Int_Tag, &ConfigureParams.Joysticks.Joy[1].nKeyCodeRight },
	{ "nKeyCodeFire", Int_Tag, &ConfigureParams.Joysticks.Joy[1].nKeyCodeFire },
	{ NULL , Error_Tag, NULL }
};
#endif
static const struct Config_Tag configs_Joystick1[] =
{
	{ "nJoystickMode", Int_Tag, &ConfigureParams.Joysticks.Joy[1].nJoystickMode },
	{ "bEnableAutoFire", Bool_Tag, &ConfigureParams.Joysticks.Joy[1].bEnableAutoFire },
	{ "bEnableJumpOnFire2", Bool_Tag, &ConfigureParams.Joysticks.Joy[1].bEnableJumpOnFire2 },
	{ "nJoyId", Int_Tag, &ConfigureParams.Joysticks.Joy[1].nJoyId },
	{ "kUp", Key_Tag, &ConfigureParams.Joysticks.Joy[1].nKeyCodeUp },
	{ "kDown", Key_Tag, &ConfigureParams.Joysticks.Joy[1].nKeyCodeDown },
	{ "kLeft", Key_Tag, &ConfigureParams.Joysticks.Joy[1].nKeyCodeLeft },
	{ "kRight", Key_Tag, &ConfigureParams.Joysticks.Joy[1].nKeyCodeRight },
	{ "kFire", Key_Tag, &ConfigureParams.Joysticks.Joy[1].nKeyCodeFire },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save joystick 2 options */
#if !WITH_SDL2
static const struct Config_Tag configs_Joystick2_Sdl1[] =
{
	{ "nKeyCodeUp", Int_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeUp },
	{ "nKeyCodeDown", Int_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeDown },
	{ "nKeyCodeLeft", Int_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeLeft },
	{ "nKeyCodeRight", Int_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeRight },
	{ "nKeyCodeFire", Int_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeFire },
	{ NULL , Error_Tag, NULL }
};
#endif
static const struct Config_Tag configs_Joystick2[] =
{
	{ "nJoystickMode", Int_Tag, &ConfigureParams.Joysticks.Joy[2].nJoystickMode },
	{ "bEnableAutoFire", Bool_Tag, &ConfigureParams.Joysticks.Joy[2].bEnableAutoFire },
	{ "bEnableJumpOnFire2", Bool_Tag, &ConfigureParams.Joysticks.Joy[2].bEnableJumpOnFire2 },
	{ "nJoyId", Int_Tag, &ConfigureParams.Joysticks.Joy[2].nJoyId },
	{ "kUp", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeUp },
	{ "kDown", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeDown },
	{ "kLeft", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeLeft },
	{ "kRight", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeRight },
	{ "kFire", Key_Tag, &ConfigureParams.Joysticks.Joy[2].nKeyCodeFire },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save joystick 3 options */
#if !WITH_SDL2
static const struct Config_Tag configs_Joystick3_Sdl1[] =
{
	{ "nKeyCodeUp", Int_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeUp },
	{ "nKeyCodeDown", Int_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeDown },
	{ "nKeyCodeLeft", Int_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeLeft },
	{ "nKeyCodeRight", Int_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeRight },
	{ "nKeyCodeFire", Int_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeFire },
	{ NULL , Error_Tag, NULL }
};
#endif
static const struct Config_Tag configs_Joystick3[] =
{
	{ "nJoystickMode", Int_Tag, &ConfigureParams.Joysticks.Joy[3].nJoystickMode },
	{ "bEnableAutoFire", Bool_Tag, &ConfigureParams.Joysticks.Joy[3].bEnableAutoFire },
	{ "bEnableJumpOnFire2", Bool_Tag, &ConfigureParams.Joysticks.Joy[3].bEnableJumpOnFire2 },
	{ "nJoyId", Int_Tag, &ConfigureParams.Joysticks.Joy[3].nJoyId },
	{ "kUp", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeUp },
	{ "kDown", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeDown },
	{ "kLeft", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeLeft },
	{ "kRight", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeRight },
	{ "kFire", Key_Tag, &ConfigureParams.Joysticks.Joy[3].nKeyCodeFire },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save joystick 4 options */
#if !WITH_SDL2
static const struct Config_Tag configs_Joystick4_Sdl1[] =
{
	{ "nKeyCodeUp", Int_Tag, &ConfigureParams.Joysticks.Joy[4].nKeyCodeUp },
	{ "nKeyCodeDown", Int_Tag, &ConfigureParams.Joysticks.Joy[4].nKeyCodeDown },
	{ "nKeyCodeLeft", Int_Tag, &ConfigureParams.Joysticks.Joy[4].nKeyCodeLeft },
	{ "nKeyCodeRight", Int_Tag, &ConfigureParams.Joysticks.Joy[4].nKeyCodeRight },
	{ "nKeyCodeFire", Int_Tag, &ConfigureParams.Joysticks.Joy[4].nKeyCodeFire },
	{ NULL , Error_Tag, NULL }
};
#endif
static const struct Config_Tag configs_Joystick4[] =
{
	{ "nJoystickMode", Int_Tag, &ConfigureParams.Joysticks.Joy[4].nJoystickMode },
	{ "bEnableAutoFire", Bool_Tag, &ConfigureParams.Joysticks.Joy[4].bEnableAutoFire },
	{ "bEnableJumpOnFire2", Bool_Tag, &ConfigureParams.Joysticks.Joy[4].bEnableJumpOnFire2 },
	{ "nJoyId", Int_Tag, &ConfigureParams.Joysticks.Joy[4].nJoyId },
	{ "kUp", Key_Tag, &ConfigureParams.Joysticks.Joy[4].nKeyCodeUp },
	{ "kDown", Key_Tag, &ConfigureParams.Joysticks.Joy[4].nKeyCodeDown },
	{ "kLeft", Key_Tag, &ConfigureParams.Joysticks.Joy[4].nKeyCodeLeft },
	{ "kRight", Key_Tag, &ConfigureParams.Joysticks.Joy[4].nKeyCodeRight },
	{ "kFire", Key_Tag, &ConfigureParams.Joysticks.Joy[4].nKeyCodeFire },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save joystick 5 options */
#if !WITH_SDL2
static const struct Config_Tag configs_Joystick5_Sdl1[] =
{
	{ "nKeyCodeUp", Int_Tag, &ConfigureParams.Joysticks.Joy[5].nKeyCodeUp },
	{ "nKeyCodeDown", Int_Tag, &ConfigureParams.Joysticks.Joy[5].nKeyCodeDown },
	{ "nKeyCodeLeft", Int_Tag, &ConfigureParams.Joysticks.Joy[5].nKeyCodeLeft },
	{ "nKeyCodeRight", Int_Tag, &ConfigureParams.Joysticks.Joy[5].nKeyCodeRight },
	{ "nKeyCodeFire", Int_Tag, &ConfigureParams.Joysticks.Joy[5].nKeyCodeFire },
	{ NULL , Error_Tag, NULL }
};
#endif
static const struct Config_Tag configs_Joystick5[] =
{
	{ "nJoystickMode", Int_Tag, &ConfigureParams.Joysticks.Joy[5].nJoystickMode },
	{ "bEnableAutoFire", Bool_Tag, &ConfigureParams.Joysticks.Joy[5].bEnableAutoFire },
	{ "bEnableJumpOnFire2", Bool_Tag, &ConfigureParams.Joysticks.Joy[5].bEnableJumpOnFire2 },
	{ "nJoyId", Int_Tag, &ConfigureParams.Joysticks.Joy[5].nJoyId },
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
	{ "szMappingFileName", String_Tag, ConfigureParams.Keyboard.szMappingFileName },
	{ NULL , Error_Tag, NULL }
};

#if !WITH_SDL2
/* Used to load/save shortcut key bindings with modifiers options */
static const struct Config_Tag configs_ShortCutWithMod_Sdl1[] =
{
	{ "keyOptions",    Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_OPTIONS] },
	{ "keyFullScreen", Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_FULLSCREEN] },
	{ "keyMouseMode",  Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_MOUSEGRAB] },
	{ "keyColdReset",  Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_COLDRESET] },
	{ "keyWarmReset",  Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_WARMRESET] },
	{ "keyScreenShot", Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_SCREENSHOT] },
	{ "keyBossKey",    Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_BOSSKEY] },
	{ "keyCursorEmu",  Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_CURSOREMU] },
	{ "keyFastForward",Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_FASTFORWARD] },
	{ "keyRecAnim",    Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_RECANIM] },
	{ "keyRecSound",   Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_RECSOUND] },
	{ "keySound",      Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_SOUND] },
	{ "keyPause",      Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_PAUSE] },
	{ "keyDebugger",   Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_DEBUG] },
	{ "keyQuit",       Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_QUIT] },
	{ "keyLoadMem",    Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_LOADMEM] },
	{ "keySaveMem",    Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_SAVEMEM] },
	{ "keyInsertDiskA",Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_INSERTDISKA] },
	{ "keySwitchJoy0", Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_JOY_0] },
	{ "keySwitchJoy1", Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_JOY_1] },
	{ "keySwitchPadA", Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_PAD_A] },
	{ "keySwitchPadB", Int_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_PAD_B] },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save shortcut key bindings without modifiers options */
static const struct Config_Tag configs_ShortCutWithoutMod_Sdl1[] =
{
	{ "keyOptions",    Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_OPTIONS] },
	{ "keyFullScreen", Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_FULLSCREEN] },
	{ "keyMouseMode",  Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_MOUSEGRAB] },
	{ "keyColdReset",  Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_COLDRESET] },
	{ "keyWarmReset",  Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_WARMRESET] },
	{ "keyScreenShot", Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_SCREENSHOT] },
	{ "keyBossKey",    Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_BOSSKEY] },
	{ "keyCursorEmu",  Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_CURSOREMU] },
	{ "keyFastForward",Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_FASTFORWARD] },
	{ "keyRecAnim",    Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_RECANIM] },
	{ "keyRecSound",   Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_RECSOUND] },
	{ "keySound",      Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_SOUND] },
	{ "keyPause",      Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_PAUSE] },
	{ "keyDebugger",   Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_DEBUG] },
	{ "keyQuit",       Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_QUIT] },
	{ "keyLoadMem",    Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_LOADMEM] },
	{ "keySaveMem",    Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_SAVEMEM] },
	{ "keyInsertDiskA",Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_INSERTDISKA] },
	{ "keySwitchJoy0", Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_JOY_0] },
	{ "keySwitchJoy1", Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_JOY_1] },
	{ "keySwitchPadA", Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_PAD_A] },
	{ "keySwitchPadB", Int_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_PAD_B] },
	{ NULL , Error_Tag, NULL }
};
#endif

/* Used to load/save shortcut key bindings with modifiers options */
static const struct Config_Tag configs_ShortCutWithMod[] =
{
	{ "kOptions",    Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_OPTIONS] },
	{ "kFullScreen", Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_FULLSCREEN] },
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
	{ "nMemorySize", Int_Tag, &ConfigureParams.Memory.nMemorySize },
	{ "nTTRamSize", Int_Tag, &ConfigureParams.Memory.nTTRamSize },
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
	{ "bUseHardDiskImage", Bool_Tag, &ConfigureParams.Acsi[0].bUseDevice },
	{ "szHardDiskImage", String_Tag, ConfigureParams.Acsi[0].sDeviceFile },
	{ "bUseIdeMasterHardDiskImage", Bool_Tag, &ConfigureParams.HardDisk.bUseIdeMasterHardDiskImage },
	{ "bUseIdeSlaveHardDiskImage", Bool_Tag, &ConfigureParams.HardDisk.bUseIdeSlaveHardDiskImage },
	{ "szIdeMasterHardDiskImage", String_Tag, ConfigureParams.HardDisk.szIdeMasterHardDiskImage },
	{ "szIdeSlaveHardDiskImage", String_Tag, ConfigureParams.HardDisk.szIdeSlaveHardDiskImage },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save ACSI options */
static const struct Config_Tag configs_Acsi[] =
{
	// { "bUseDevice0", Bool_Tag, &ConfigureParams.Acsi[0].bUseDevice },
	// { "sDeviceFile0", String_Tag, ConfigureParams.Acsi[0].sDeviceFile },
	{ "bUseDevice1", Bool_Tag, &ConfigureParams.Acsi[1].bUseDevice },
	{ "sDeviceFile1", String_Tag, ConfigureParams.Acsi[1].sDeviceFile },
	{ "bUseDevice2", Bool_Tag, &ConfigureParams.Acsi[2].bUseDevice },
	{ "sDeviceFile2", String_Tag, ConfigureParams.Acsi[2].sDeviceFile },
	{ "bUseDevice3", Bool_Tag, &ConfigureParams.Acsi[3].bUseDevice },
	{ "sDeviceFile3", String_Tag, ConfigureParams.Acsi[3].sDeviceFile },
	{ "bUseDevice4", Bool_Tag, &ConfigureParams.Acsi[4].bUseDevice },
	{ "sDeviceFile4", String_Tag, ConfigureParams.Acsi[4].sDeviceFile },
	{ "bUseDevice5", Bool_Tag, &ConfigureParams.Acsi[5].bUseDevice },
	{ "sDeviceFile5", String_Tag, ConfigureParams.Acsi[5].sDeviceFile },
	{ "bUseDevice6", Bool_Tag, &ConfigureParams.Acsi[6].bUseDevice },
	{ "sDeviceFile6", String_Tag, ConfigureParams.Acsi[6].sDeviceFile },
	{ "bUseDevice7", Bool_Tag, &ConfigureParams.Acsi[7].bUseDevice },
	{ "sDeviceFile7", String_Tag, ConfigureParams.Acsi[7].sDeviceFile },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save SCSI options */
static const struct Config_Tag configs_Scsi[] =
{
	{ "bUseDevice1", Bool_Tag, &ConfigureParams.Scsi[1].bUseDevice },
	{ "sDeviceFile1", String_Tag, ConfigureParams.Scsi[1].sDeviceFile },
	{ "bUseDevice2", Bool_Tag, &ConfigureParams.Scsi[2].bUseDevice },
	{ "sDeviceFile2", String_Tag, ConfigureParams.Scsi[2].sDeviceFile },
	{ "bUseDevice3", Bool_Tag, &ConfigureParams.Scsi[3].bUseDevice },
	{ "sDeviceFile3", String_Tag, ConfigureParams.Scsi[3].sDeviceFile },
	{ "bUseDevice4", Bool_Tag, &ConfigureParams.Scsi[4].bUseDevice },
	{ "sDeviceFile4", String_Tag, ConfigureParams.Scsi[4].sDeviceFile },
	{ "bUseDevice5", Bool_Tag, &ConfigureParams.Scsi[5].bUseDevice },
	{ "sDeviceFile5", String_Tag, ConfigureParams.Scsi[5].sDeviceFile },
	{ "bUseDevice6", Bool_Tag, &ConfigureParams.Scsi[6].bUseDevice },
	{ "sDeviceFile6", String_Tag, ConfigureParams.Scsi[6].sDeviceFile },
	{ "bUseDevice7", Bool_Tag, &ConfigureParams.Scsi[7].bUseDevice },
	{ "sDeviceFile7", String_Tag, ConfigureParams.Scsi[7].sDeviceFile },
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

/* Used to load/save RS232 options */
static const struct Config_Tag configs_Rs232[] =
{
	{ "bEnableRS232", Bool_Tag, &ConfigureParams.RS232.bEnableRS232 },
	{ "szOutFileName", String_Tag, ConfigureParams.RS232.szOutFileName },
	{ "szInFileName", String_Tag, ConfigureParams.RS232.szInFileName },
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
	{ "bPatchTimerD", Bool_Tag, &ConfigureParams.System.bPatchTimerD },
	{ "bFastBoot", Bool_Tag, &ConfigureParams.System.bFastBoot },
	{ "bFastForward", Bool_Tag, &ConfigureParams.System.bFastForward },
	{ "bAddressSpace24", Bool_Tag, &ConfigureParams.System.bAddressSpace24 },

#if ENABLE_WINUAE_CPU
	{ "bCycleExactCpu", Bool_Tag, &ConfigureParams.System.bCycleExactCpu },
	{ "n_FPUType", Int_Tag, &ConfigureParams.System.n_FPUType },
	{ "bCompatibleFPU", Bool_Tag, &ConfigureParams.System.bCompatibleFPU },
	{ "bMMU", Bool_Tag, &ConfigureParams.System.bMMU },
#endif
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
	ConfigureParams.Log.nExceptionDebugMask = DEFAULT_EXCEPTIONS;
	ConfigureParams.Log.nTextLogLevel = LOG_TODO;
	ConfigureParams.Log.nAlertDlgLogLevel = LOG_ERROR;
	ConfigureParams.Log.bConfirmQuit = true;
	ConfigureParams.Log.bNatFeats = false;
	ConfigureParams.Log.bConsoleWindow = false;

	/* Set defaults for debugger */
	ConfigureParams.Debugger.nNumberBase = 10;
	ConfigureParams.Debugger.nDisasmLines = 8;
	ConfigureParams.Debugger.nMemdumpLines = 8;
	/* external one has nicer output, but isn't as complete as UAE one */
	ConfigureParams.Debugger.bDisasmUAE = false;
	ConfigureParams.Debugger.nDisasmOptions = Disasm_GetOptions();

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
	ConfigureParams.HardDisk.nGemdosCase = GEMDOS_NOP;
	ConfigureParams.HardDisk.nWriteProtection = WRITEPROT_OFF;
	ConfigureParams.HardDisk.nGemdosDrive = DRIVE_C;
	ConfigureParams.HardDisk.bUseHardDiskDirectories = false;
	for (i = 0; i < MAX_HARDDRIVES; i++)
	{
		strcpy(ConfigureParams.HardDisk.szHardDiskDirectories[i], psWorkingDir);
		File_CleanFileName(ConfigureParams.HardDisk.szHardDiskDirectories[i]);
	}
	ConfigureParams.HardDisk.bUseIdeMasterHardDiskImage = false;
	strcpy(ConfigureParams.HardDisk.szIdeMasterHardDiskImage, psWorkingDir);
	ConfigureParams.HardDisk.bUseIdeSlaveHardDiskImage = false;
	strcpy(ConfigureParams.HardDisk.szIdeSlaveHardDiskImage, psWorkingDir);

	/* ACSI */
	for (i = 0; i < MAX_ACSI_DEVS; i++)
	{
		ConfigureParams.Acsi[i].bUseDevice = false;
		strcpy(ConfigureParams.Acsi[i].sDeviceFile, psWorkingDir);
	}

	/* SCSI */
	for (i = 0; i < MAX_SCSI_DEVS; i++)
	{
		ConfigureParams.Scsi[i].bUseDevice = false;
		strcpy(ConfigureParams.Scsi[i].sDeviceFile, psWorkingDir);
	}

	/* Set defaults for Joysticks */
	maxjoy = Joy_GetMaxId();
	for (i = 0; i < JOYSTICK_COUNT; i++)
	{
		ConfigureParams.Joysticks.Joy[i].nJoystickMode = JOYSTICK_DISABLED;
		ConfigureParams.Joysticks.Joy[i].bEnableAutoFire = false;
		ConfigureParams.Joysticks.Joy[i].bEnableJumpOnFire2 = false;
		ConfigureParams.Joysticks.Joy[i].nJoyId = (i > maxjoy ? maxjoy : i);
		ConfigureParams.Joysticks.Joy[i].nKeyCodeUp = SDLK_UP;
		ConfigureParams.Joysticks.Joy[i].nKeyCodeDown = SDLK_DOWN;
		ConfigureParams.Joysticks.Joy[i].nKeyCodeLeft = SDLK_LEFT;
		ConfigureParams.Joysticks.Joy[i].nKeyCodeRight = SDLK_RIGHT;
		ConfigureParams.Joysticks.Joy[i].nKeyCodeFire = SDLK_RCTRL;
	}
	if (SDL_NumJoysticks() > 0)
	{
		/* ST Joystick #1 is default joystick */
		ConfigureParams.Joysticks.Joy[1].nJoyId = 0;
		ConfigureParams.Joysticks.Joy[0].nJoyId = (maxjoy ? 1 : 0);
		ConfigureParams.Joysticks.Joy[1].nJoystickMode = JOYSTICK_REALSTICK;
	}

	/* Set defaults for Keyboard */
	ConfigureParams.Keyboard.bDisableKeyRepeat = false;
	ConfigureParams.Keyboard.nKeymapType = KEYMAP_SYMBOLIC;
	strcpy(ConfigureParams.Keyboard.szMappingFileName, "");
  
	/* Set defaults for Shortcuts */
	ConfigureParams.Shortcut.withoutModifier[SHORTCUT_OPTIONS] = SDLK_F12;
	ConfigureParams.Shortcut.withoutModifier[SHORTCUT_FULLSCREEN] = SDLK_F11;
	ConfigureParams.Shortcut.withoutModifier[SHORTCUT_PAUSE] = SDLK_PAUSE;
  
	ConfigureParams.Shortcut.withModifier[SHORTCUT_DEBUG] = SDLK_PAUSE;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_OPTIONS] = SDLK_o;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_FULLSCREEN] = SDLK_f;
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
	ConfigureParams.Memory.nMemorySize = 1;     /* 1 MiB */
	ConfigureParams.Memory.nTTRamSize = 0;     /* disabled */
	ConfigureParams.Memory.bAutoSave = false;
	sprintf(ConfigureParams.Memory.szMemoryCaptureFileName, "%s%chatari.sav",
	        psHomeDir, PATHSEP);
	sprintf(ConfigureParams.Memory.szAutoSaveFileName, "%s%cauto.sav",
	        psHomeDir, PATHSEP);

	/* Set defaults for Printer */
	ConfigureParams.Printer.bEnablePrinting = false;
	sprintf(ConfigureParams.Printer.szPrintToFileName, "%s%chatari.prn",
	        psHomeDir, PATHSEP);

	/* Set defaults for RS232 */
	ConfigureParams.RS232.bEnableRS232 = false;
	strcpy(ConfigureParams.RS232.szOutFileName, "/dev/modem");
	strcpy(ConfigureParams.RS232.szInFileName, "/dev/modem");

	/* Set defaults for MIDI */
	ConfigureParams.Midi.bEnableMidi = false;
	strcpy(ConfigureParams.Midi.sMidiInFileName, "/dev/snd/midiC1D0");
	strcpy(ConfigureParams.Midi.sMidiOutFileName, "/dev/snd/midiC1D0");

	/* Set defaults for Screen */
	ConfigureParams.Screen.bFullScreen = false;
	ConfigureParams.Screen.bKeepResolution = true;
#if !WITH_SDL2
	ConfigureParams.Screen.bKeepResolutionST = false;
#endif
	ConfigureParams.Screen.nFrameSkips = AUTO_FRAMESKIP_LIMIT;
	ConfigureParams.Screen.bAllowOverscan = true;
	ConfigureParams.Screen.nSpec512Threshold = 1;
	ConfigureParams.Screen.nForceBpp = 0;
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
	/* gives zoomed Falcon/TT windows about same size as ST/STE windows */
	ConfigureParams.Screen.nMaxWidth = 2*NUM_VISIBLE_LINE_PIXELS;
	ConfigureParams.Screen.nMaxHeight = 2*NUM_VISIBLE_LINES+STATUSBAR_MAX_HEIGHT;
	ConfigureParams.Screen.bForceMax = false;
#if WITH_SDL2
	ConfigureParams.Screen.nRenderScaleQuality = 0;
	ConfigureParams.Screen.bUseVsync = false;
#endif

	/* Set defaults for Sound */
	ConfigureParams.Sound.bEnableMicrophone = true;
	ConfigureParams.Sound.bEnableSound = true;
	ConfigureParams.Sound.bEnableSoundSync = false;
	ConfigureParams.Sound.nPlaybackFreq = 44100;
	sprintf(ConfigureParams.Sound.szYMCaptureFileName, "%s%chatari.wav",
	        psWorkingDir, PATHSEP);
	ConfigureParams.Sound.SdlAudioBufferSize = 0;
	ConfigureParams.Sound.YmVolumeMixing = YM_TABLE_MIXING;

	/* Set defaults for Rom */
	sprintf(ConfigureParams.Rom.szTosImageFileName, "%s%ctos.img",
	        Paths_GetDataDir(), PATHSEP);
	ConfigureParams.Rom.bPatchTos = true;
	strcpy(ConfigureParams.Rom.szCartridgeImageFileName, "");

	/* Set defaults for System */
	ConfigureParams.System.nMachineType = MACHINE_ST;
	ConfigureParams.System.nCpuLevel = 0;
	ConfigureParams.System.nCpuFreq = 8;
	ConfigureParams.System.nDSPType = DSP_TYPE_NONE;
	ConfigureParams.System.bAddressSpace24 = true;
#if ENABLE_WINUAE_CPU
	ConfigureParams.System.n_FPUType = FPU_NONE;
	ConfigureParams.System.bCompatibleFPU = true;
	ConfigureParams.System.bMMU = false;
	ConfigureParams.System.bCycleExactCpu = true;
#endif
	ConfigureParams.System.VideoTimingMode = VIDEO_TIMING_MODE_WS3;
	ConfigureParams.System.bCompatibleCpu = true;
	ConfigureParams.System.bBlitter = false;
	ConfigureParams.System.bPatchTimerD = true;
	ConfigureParams.System.bFastBoot = false;
	ConfigureParams.System.bFastForward = false;

	/* Set defaults for Video */
#if HAVE_LIBPNG
	ConfigureParams.Video.AviRecordVcodec = AVI_RECORD_VIDEO_CODEC_PNG;
#else
	ConfigureParams.Video.AviRecordVcodec = AVI_RECORD_VIDEO_CODEC_BMP;
#endif
	ConfigureParams.Video.AviRecordFps = 0;			/* automatic FPS */
	sprintf(ConfigureParams.Video.AviRecordFile, "%s%chatari.avi", psWorkingDir, PATHSEP);

	/* Initialize the configuration file name */
	if (strlen(psHomeDir) < sizeof(sConfigFileName)-13)
		sprintf(sConfigFileName, "%s%chatari.cfg", psHomeDir, PATHSEP);
	else
		strcpy(sConfigFileName, "hatari.cfg");

#if defined(__AMIGAOS4__)
	/* Fix default path names on Amiga OS */
	sprintf(ConfigureParams.Rom.szTosImageFileName, "%stos.img", Paths_GetDataDir());
#endif
}


/*-----------------------------------------------------------------------*/
/**
 * Copy details from configuration structure into global variables for system,
 * clean file names, etc...  Called from main.c and dialog.c files.
 */
void Configuration_Apply(bool bReset)
{
	int i;

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
			VDI_SetResolution(ConfigureParams.Screen.nVdiColors,
			                  ConfigureParams.Screen.nVdiWidth,
			                  ConfigureParams.Screen.nVdiHeight);
			bVdiAesIntercept = true;
		}
	}
	if (ConfigureParams.Screen.nFrameSkips < AUTO_FRAMESKIP_LIMIT)
	{
		nFrameSkips = ConfigureParams.Screen.nFrameSkips;
	}
	if (ConfigureParams.Screen.nForceBpp < 15)	/* Avoid 8-bit depth */
	{
		ConfigureParams.Screen.nForceBpp = 0;
	}

	/* Init clocks for this machine */
	ClocksTimings_InitMachine ( ConfigureParams.System.nMachineType );

	/* Set video timings for this machine */
	Video_SetTimings ( ConfigureParams.System.nMachineType , ConfigureParams.System.VideoTimingMode );

	/* Sound settings */
	/* SDL sound buffer in ms */
	SdlAudioBufferSize = ConfigureParams.Sound.SdlAudioBufferSize;
	if ( SdlAudioBufferSize == 0 )			/* use default setting for SDL */
		;
	else if ( SdlAudioBufferSize < 10 )		/* min of 10 ms */
		SdlAudioBufferSize = 10;
	else if ( SdlAudioBufferSize > 100 )		/* max of 100 ms */
		SdlAudioBufferSize = 100;

	/* Set playback frequency */
	Audio_SetOutputAudioFreq(ConfigureParams.Sound.nPlaybackFreq);

	/* YM Mixing */
	if ( ( ConfigureParams.Sound.YmVolumeMixing != YM_LINEAR_MIXING )
	  && ( ConfigureParams.Sound.YmVolumeMixing != YM_TABLE_MIXING  )
	  && ( ConfigureParams.Sound.YmVolumeMixing != YM_MODEL_MIXING ) )
		ConfigureParams.Sound.YmVolumeMixing = YM_TABLE_MIXING;

	YmVolumeMixing = ConfigureParams.Sound.YmVolumeMixing;
	Sound_SetYmVolumeMixing();

	/* Check/constrain CPU settings and change corresponding
	 * UAE cpu_level & cpu_compatible variables
	 */
	M68000_CheckCpuSettings();

	/* Clean file and directory names */
	File_MakeAbsoluteName(ConfigureParams.Rom.szTosImageFileName);
	if (strlen(ConfigureParams.Rom.szCartridgeImageFileName) > 0)
		File_MakeAbsoluteName(ConfigureParams.Rom.szCartridgeImageFileName);
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

	/* make path names absolute, but handle special file names */
	File_MakeAbsoluteSpecialName(ConfigureParams.Log.sLogFileName);
	File_MakeAbsoluteSpecialName(ConfigureParams.Log.sTraceFileName);
	File_MakeAbsoluteSpecialName(ConfigureParams.RS232.szInFileName);
	File_MakeAbsoluteSpecialName(ConfigureParams.RS232.szOutFileName);
	File_MakeAbsoluteSpecialName(ConfigureParams.Midi.sMidiInFileName);
	File_MakeAbsoluteSpecialName(ConfigureParams.Midi.sMidiOutFileName);
	File_MakeAbsoluteSpecialName(ConfigureParams.Printer.szPrintToFileName);

	/* Enable/disable floppy drives */
	FDC_Drive_Set_Enable ( 0 , ConfigureParams.DiskImage.EnableDriveA );
	FDC_Drive_Set_Enable ( 1 , ConfigureParams.DiskImage.EnableDriveB );
	FDC_Drive_Set_NumberOfHeads ( 0 , ConfigureParams.DiskImage.DriveA_NumberOfHeads );
	FDC_Drive_Set_NumberOfHeads ( 1 , ConfigureParams.DiskImage.DriveB_NumberOfHeads );

        /* Update disassembler */
#if ENABLE_WINUAE_CPU
        Disasm_SetCPUType ( ConfigureParams.System.nCpuLevel , ConfigureParams.System.n_FPUType );
#else
        Disasm_SetCPUType ( ConfigureParams.System.nCpuLevel , 0 );
#endif

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
		fprintf(stderr, "Can not load configuration file %s (section %s).\n",
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

#if !WITH_SDL2	/* for old SDL1 keycode compatibility */
	Configuration_LoadSection(psFileName, configs_ShortCutWithMod_Sdl1, "[ShortcutsWithModifiers]");
	Configuration_LoadSection(psFileName, configs_ShortCutWithoutMod_Sdl1, "[ShortcutsWithoutModifiers]");
	Configuration_LoadSection(psFileName, configs_Joystick0_Sdl1, "[Joystick0]");
	Configuration_LoadSection(psFileName, configs_Joystick1_Sdl1, "[Joystick1]");
	Configuration_LoadSection(psFileName, configs_Joystick2_Sdl1, "[Joystick2]");
	Configuration_LoadSection(psFileName, configs_Joystick3_Sdl1, "[Joystick3]");
	Configuration_LoadSection(psFileName, configs_Joystick4_Sdl1, "[Joystick4]");
	Configuration_LoadSection(psFileName, configs_Joystick5_Sdl1, "[Joystick5]");
#endif

	Configuration_LoadSection(psFileName, configs_Log, "[Log]");
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
	Configuration_LoadSection(psFileName, configs_Rom, "[ROM]");
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
		fprintf(stderr, "Error while updating section %s in %s\n", pSection, pFilename);

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
	/*Configuration_SaveSection(sConfigFileName, configs_Acsi, "[ACSI]");*/
	/*Configuration_SaveSection(sConfigFileName, configs_Scsi, "[SCSI]");*/
	Configuration_SaveSection(sConfigFileName, configs_Rom, "[ROM]");
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

	MemorySnapShot_Store(&ConfigureParams.Memory.nMemorySize, sizeof(ConfigureParams.Memory.nMemorySize));
	MemorySnapShot_Store(&ConfigureParams.Memory.nTTRamSize, sizeof(ConfigureParams.Memory.nTTRamSize));

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
	/* for (i = 0; i < MAX_SCSI_DEVS; i++)
	{
		MemorySnapShot_Store(&ConfigureParams.Scsi[i].bUseDevice, sizeof(ConfigureParams.Scsi[i].bUseDevice));
		MemorySnapShot_Store(ConfigureParams.Scsi[i].sDeviceFile, sizeof(ConfigureParams.Scsi[i].sDeviceFile));
	}*/

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
	MemorySnapShot_Store(&bOldRealTimeClock, sizeof(bOldRealTimeClock));	/* TODO: Can be removed later */
	MemorySnapShot_Store(&ConfigureParams.System.bPatchTimerD, sizeof(ConfigureParams.System.bPatchTimerD));
	MemorySnapShot_Store(&ConfigureParams.System.bAddressSpace24, sizeof(ConfigureParams.System.bAddressSpace24));

#if ENABLE_WINUAE_CPU
	MemorySnapShot_Store(&ConfigureParams.System.bCycleExactCpu, sizeof(ConfigureParams.System.bCycleExactCpu));
	MemorySnapShot_Store(&ConfigureParams.System.n_FPUType, sizeof(ConfigureParams.System.n_FPUType));
	MemorySnapShot_Store(&ConfigureParams.System.bCompatibleFPU, sizeof(ConfigureParams.System.bCompatibleFPU));
	MemorySnapShot_Store(&ConfigureParams.System.bMMU, sizeof(ConfigureParams.System.bMMU));
#endif

	MemorySnapShot_Store(&ConfigureParams.DiskImage.FastFloppy, sizeof(ConfigureParams.DiskImage.FastFloppy));

	if (!bSave)
		Configuration_Apply(true);
}
