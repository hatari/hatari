/*
  Hatari - PrefsController.m

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Preferences window controller implementation file

  Feb-Mar 2006, Sébastien Molines - Created
  Jan 2006, Sébastien Molines - Updated for recent emulator updates
*/

// TODO: Set the default paths to MacOS-friendly values
// TODO: Move hardcoded string to localizable resources (e.g. string "Reset the emulator?")


#import "PrefsController.h"
#import "Shared.h"

#include "main.h"
#include "configuration.h"
#include "change.h"
#include "dialog.h"
#include "file.h"
#include "floppy.h"
#include "screen.h"
#include "sdlgui.h"


// Macros to transfer data between Cocoa controls and Hatari data structures
#define EXPORT_TEXTFIELD(nstextfield, target) GuiOsx_ExportPathString([nstextfield stringValue], target, sizeof((target)))
#define EXPORT_NTEXTFIELD(nstextfield, target) target = [nstextfield intValue]
#define EXPORT_SWITCH(nsbutton, target) target = ([(nsbutton) state] == NSOnState)
#define EXPORT_RADIO(nsmatrix, target) target = [[(nsmatrix) selectedCell] tag]
#define EXPORT_DROPDOWN(nspopupbutton, target) target = [[(nspopupbutton) selectedItem] tag]
#define EXPORT_SLIDER(nsslider, target) target = [(nsslider) intValue]

#define IMPORT_TEXTFIELD(nstextfield, source) [(nstextfield) setStringValue:[[NSString stringWithCString:(source) encoding:NSASCIIStringEncoding] stringByAbbreviatingWithTildeInPath]]
#define IMPORT_NTEXTFIELD(nstextfield, source) [(nstextfield) setIntValue:(source)]
#define IMPORT_SWITCH(nsbutton, source) [(nsbutton) setState:((source))? NSOnState : NSOffState]
#define IMPORT_RADIO(nsmatrix, source) [(nsmatrix) selectCellWithTag:(source)]
#define IMPORT_DROPDOWN(nspopupbutton, source) [(nspopupbutton) selectItemAtIndex:[(nspopupbutton) indexOfItemWithTag:(source)]]
#define IMPORT_SLIDER(nsslider,source) [(nsslider) setIntValue:source]


// Back up of the current configuration parameters
CNF_PARAMS CurrentParams;


// Keys to be listed in the Joysticks dropdowns
SDLKey Preferences_KeysForJoysticks[] =
{
	SDLK_BACKSPACE,
	SDLK_TAB,
	SDLK_CLEAR,
	SDLK_RETURN,
	SDLK_PAUSE,
	SDLK_ESCAPE,
	SDLK_SPACE,
	SDLK_EXCLAIM,
	SDLK_QUOTEDBL,
	SDLK_HASH,
	SDLK_DOLLAR,
	SDLK_AMPERSAND,
	SDLK_QUOTE,
	SDLK_LEFTPAREN,
	SDLK_RIGHTPAREN,
	SDLK_ASTERISK,
	SDLK_PLUS,
	SDLK_COMMA,
	SDLK_MINUS,
	SDLK_PERIOD,
	SDLK_SLASH,
	SDLK_0,
	SDLK_1,
	SDLK_2,
	SDLK_3,
	SDLK_4,
	SDLK_5,
	SDLK_6,
	SDLK_7,
	SDLK_8,
	SDLK_9,
	SDLK_COLON,
	SDLK_SEMICOLON,
	SDLK_LESS,
	SDLK_EQUALS,
	SDLK_GREATER,
	SDLK_QUESTION,
	SDLK_AT,
	SDLK_LEFTBRACKET,
	SDLK_BACKSLASH,
	SDLK_RIGHTBRACKET,
	SDLK_CARET,
	SDLK_UNDERSCORE,
	SDLK_BACKQUOTE,
	SDLK_a,
	SDLK_b,
	SDLK_c,
	SDLK_d,
	SDLK_e,
	SDLK_f,
	SDLK_g,
	SDLK_h,
	SDLK_i,
	SDLK_j,
	SDLK_k,
	SDLK_l,
	SDLK_m,
	SDLK_n,
	SDLK_o,
	SDLK_p,
	SDLK_q,
	SDLK_r,
	SDLK_s,
	SDLK_t,
	SDLK_u,
	SDLK_v,
	SDLK_w,
	SDLK_x,
	SDLK_y,
	SDLK_z,
	SDLK_DELETE,
	SDLK_KP0,
	SDLK_KP1,
	SDLK_KP2,
	SDLK_KP3,
	SDLK_KP4,
	SDLK_KP5,
	SDLK_KP6,
	SDLK_KP7,
	SDLK_KP8,
	SDLK_KP9,
	SDLK_KP_PERIOD,
	SDLK_KP_DIVIDE,
	SDLK_KP_MULTIPLY,
	SDLK_KP_MINUS,
	SDLK_KP_PLUS,
	SDLK_KP_ENTER,
	SDLK_KP_EQUALS,
	SDLK_UP,
	SDLK_DOWN,
	SDLK_RIGHT,
	SDLK_LEFT,
	SDLK_INSERT,
	SDLK_HOME,
	SDLK_END,
	SDLK_PAGEUP,
	SDLK_PAGEDOWN,
	SDLK_F1,
	SDLK_F2,
	SDLK_F3,
	SDLK_F4,
	SDLK_F5,
	SDLK_F6,
	SDLK_F7,
	SDLK_F8,
	SDLK_F9,
	SDLK_F10,
	SDLK_F11,
	SDLK_F12,
	SDLK_F13,
	SDLK_F14,
	SDLK_F15,
	SDLK_NUMLOCK,
	SDLK_CAPSLOCK,
	SDLK_SCROLLOCK,
	SDLK_RSHIFT,
	SDLK_LSHIFT,
	SDLK_RCTRL,
	SDLK_LCTRL,
	SDLK_RALT,
	SDLK_LALT,
	SDLK_RMETA,
	SDLK_LMETA,
	SDLK_LSUPER,
	SDLK_RSUPER,
	SDLK_MODE,
	SDLK_COMPOSE,
	SDLK_HELP,
	SDLK_PRINT,
	SDLK_SYSREQ,
	SDLK_BREAK,
	SDLK_MENU,
	SDLK_POWER,
	SDLK_EURO,
	SDLK_UNDO
};

size_t Preferences_cKeysForJoysticks = sizeof(Preferences_KeysForJoysticks) / sizeof(Preferences_KeysForJoysticks[0]);

#define DLGSOUND_11KHZ      0
#define DLGSOUND_12KHZ      1
#define DLGSOUND_16KHZ      2
#define DLGSOUND_22KHZ      3
#define DLGSOUND_25KHZ      4
#define DLGSOUND_32KHZ      5
#define DLGSOUND_44KHZ      6
#define DLGSOUND_48KHZ      7
#define DLGSOUND_50KHZ      8

static const int nSoundFreqs[] =
{
	11025,
	12517,
	16000,
	22050,
	25033,
	32000,
	44100,
	48000,
	50066
};

@implementation PrefsController


/*-----------------------------------------------------------------------*/
/*
  Helper method for Choose buttons
  Returns: TRUE is the user selected a path, FALSE if he/she aborted
*/
- (BOOL)choosePathForControl:(NSTextField*)textField chooseDirectories:(bool)chooseDirectories defaultInitialDir:(NSString*)defaultInitialDir
{
	// Create and configure an OpenPanel
    NSOpenPanel *openPanel = [NSOpenPanel openPanel];
	[openPanel setCanChooseDirectories: chooseDirectories];
	[openPanel setCanChooseFiles: !chooseDirectories];

	NSString *directoryToOpen;
	NSString *fileToPreselect;
	NSString *oldPath = [textField stringValue];
	if ((oldPath != nil) && ([oldPath length] > 0))
	{
		// There is existing path: we will open its directory with its file pre-selected.
		directoryToOpen = [oldPath stringByDeletingLastPathComponent];
		fileToPreselect = [oldPath lastPathComponent];
	}
	else
	{
		// Currently no path: we will open the user's directory with no file selected.
		directoryToOpen = [defaultInitialDir stringByExpandingTildeInPath];
		fileToPreselect = nil;
	}
	
	// Run the OpenPanel, then check if the user clicked OK and selected at least one file
    if ( (NSOKButton == [openPanel runModalForDirectory:directoryToOpen file:fileToPreselect types:nil] )
	    && ([[openPanel filenames] count] > 0) )
	{
		// Get the path to the selected file
		NSString *path = [[openPanel filenames] objectAtIndex:0];
		
		// Set the control to it (abbreviated if possible)
		[textField setStringValue:[path stringByAbbreviatingWithTildeInPath]];
		
		// Signal completion
		return true;
    }
	
	// Signal that the selection was aborted
	return FALSE;
}


/*-----------------------------------------------------------------------*/
/*
  Helper method to insert a floppy image
  TODO: Add code to restrict to known file types
*/
- (void)insertFloppyImageIntoDrive:(int)drive forTextField:(NSTextField*)floppyTextField
{
	if ([self choosePathForControl:floppyTextField chooseDirectories:FALSE defaultInitialDir:[defaultImagesLocation stringValue]])
	{
		// Get the full path to the selected file
		NSString *path = [[floppyTextField stringValue] stringByExpandingTildeInPath];
		
		// Make a non-const C string out of it
		const char* constSzPath = [path cStringUsingEncoding:NSASCIIStringEncoding];
		size_t cbPath = strlen(constSzPath) + 1;
		char szPath[cbPath];
		strncpy(szPath, constSzPath, cbPath);

		// Insert the floppy image at this path
		Floppy_SetDiskFileName(drive, szPath, NULL);
	}
}


/*-----------------------------------------------------------------------*/
/*
  Methods for all the "Choose" buttons
*/
- (IBAction)chooseCartridgeImage:(id)sender;
{
	[self choosePathForControl: cartridgeImage chooseDirectories:FALSE defaultInitialDir:@"~"];
}

- (IBAction)chooseDefaultImagesLocation:(id)sender
{
	[self choosePathForControl: defaultImagesLocation chooseDirectories:TRUE defaultInitialDir:@"~"];
}

- (IBAction)chooseFloppyImageA:(id)sender
{
	[self insertFloppyImageIntoDrive:0 forTextField: floppyImageA];
}

- (IBAction)chooseFloppyImageB:(id)sender
{
	[self insertFloppyImageIntoDrive:1 forTextField: floppyImageB];
}

- (IBAction)chooseGemdosImage:(id)sender
{
	[self choosePathForControl: gemdosImage chooseDirectories:TRUE defaultInitialDir:@"~"];
}

- (IBAction)chooseHdImage:(id)sender
{
	[self choosePathForControl: hdImage chooseDirectories:FALSE defaultInitialDir:@"~"];
}

- (IBAction)chooseIdeMasterHdImage:(id)sender
{
	[self choosePathForControl: ideMasterHdImage chooseDirectories:FALSE defaultInitialDir:@"~"];
}

- (IBAction)chooseIdeSlaveHdImage:(id)sender
{
	[self choosePathForControl: ideSlaveHdImage chooseDirectories:FALSE defaultInitialDir:@"~"];
}

- (IBAction)chooseKeyboardMappingFile:(id)sender
{
	[self choosePathForControl: keyboardMappingFile chooseDirectories:FALSE defaultInitialDir:@"~"];
}

- (IBAction)chooseMidiOutputFile:(id)sender
{
	[self choosePathForControl: writeMidiToFile chooseDirectories:FALSE defaultInitialDir:@"~"];
}

- (IBAction)choosePrintToFile:(id)sender
{
	[self choosePathForControl: printToFile chooseDirectories:FALSE defaultInitialDir:@"~"];
}

- (IBAction)chooseRS232InputFile:(id)sender
{
	[self choosePathForControl: readRS232FromFile chooseDirectories:FALSE defaultInitialDir:@"~"];
}

- (IBAction)chooseRS232OutputFile:(id)sender
{
	[self choosePathForControl: writeRS232ToFile chooseDirectories:FALSE defaultInitialDir:@"~"];
}

- (IBAction)chooseTosImage:(id)sender;
{
	[self choosePathForControl: tosImage chooseDirectories:FALSE defaultInitialDir:@"~"];
}


/*-----------------------------------------------------------------------*/
/*
  Methods for the "Eject" buttons
*/
- (IBAction)ejectFloppyA:(id)sender
{
	Floppy_SetDiskFileNameNone(0);
	
	// Refresh the control
	[floppyImageA setStringValue:@""];
}

- (IBAction)ejectFloppyB:(id)sender
{
	Floppy_SetDiskFileNameNone(1);

	// Refresh the control
	[floppyImageB setStringValue:@""];
}

- (IBAction)ejectGemdosImage:(id)sender
{
	// Clear the control. Later. saveAllControls will set the ConfigureParams accordingly to signal this is ejected
	[gemdosImage setStringValue:@""];
}

- (IBAction)ejectHdImage:(id)sender
{
	// Clear the control. Later. saveAllControls will set the ConfigureParams accordingly to signal this is ejected
	[hdImage setStringValue:@""];
}

- (IBAction)ejectIdeMasterHdImage:(id)sender
{
	// Clear the control. Later. saveAllControls will set the ConfigureParams accordingly to signal this is ejected
	[ideMasterHdImage setStringValue:@""];
}

- (IBAction)ejectIdeSlaveHdImage:(id)sender
{
	// Clear the control. Later. saveAllControls will set the ConfigureParams accordingly to signal this is ejected
	[ideSlaveHdImage setStringValue:@""];
}

/*-----------------------------------------------------------------------*/
/**
 * Methods for the "Load Config" button
 */

- (IBAction)loadConfigFrom:(id)sender
{
    NSString *ConfigFile = [NSString stringWithCString:(sConfigFileName) encoding:NSASCIIStringEncoding];
    NSOpenPanel *openPanel = [ NSOpenPanel openPanel ];
    
    if ( [ openPanel runModalForDirectory:nil file:ConfigFile types:nil ] )
	{
        ConfigFile = [ [ openPanel filenames ] objectAtIndex:0 ];
    }
	else
	{
		ConfigFile = nil;
	}

	if (ConfigFile != nil)
	{
		// Make a non-const C string out of it
		const char* constSzPath = [ConfigFile cStringUsingEncoding:NSASCIIStringEncoding];
		size_t cbPath = strlen(constSzPath) + 1;
		char szPath[cbPath];
		strncpy(szPath, constSzPath, cbPath);	

		// Load the config into ConfigureParams
		Configuration_Load(szPath);
		strcpy(sConfigFileName,szPath);
		// Refresh all the controls to match ConfigureParams
		[self setAllControls];
	}
}

/**
 * Methods for the "Load Config" button
 */
- (IBAction)saveConfigAs:(id)sender
{
	char splitpath[FILENAME_MAX], splitname[FILENAME_MAX];

	// Update the ConfigureParams from the controls
	[self saveAllControls];

	File_SplitPath(sConfigFileName, splitpath, splitname, NULL);

    NSSavePanel *savePanel = [ NSSavePanel savePanel ];

	NSString* defaultDir = [NSString stringWithCString:splitpath encoding:NSASCIIStringEncoding];
    NSString *ConfigFile = [NSString stringWithCString:splitname encoding:NSASCIIStringEncoding];
    
    if ( ![ savePanel runModalForDirectory:defaultDir file:ConfigFile ] )
	{
		return;
	}

    ConfigFile = [ savePanel filename ];
    
	if (ConfigFile != nil)
	{
		// Make a non-const C string out of it
		const char* constSzPath = [ConfigFile cStringUsingEncoding:NSASCIIStringEncoding];
		size_t cbPath = strlen(constSzPath) + 1;
		char szPath[cbPath];
		strncpy(szPath, constSzPath, cbPath);	

		// Save the config from ConfigureParams
		strcpy(sConfigFileName, szPath);
		Configuration_Save();
	}
}


/*-----------------------------------------------------------------------*/
/*
  Commits and closes
*/
- (IBAction)commitAndClose:(id)sender
{
	BOOL applyChanges = true;

	// The user clicked OK
	[self saveAllControls];
	
	// If a reset is required, ask the user first
	if (Change_DoNeedReset(&CurrentParams, &ConfigureParams))
	{
		applyChanges = ( 0 == NSRunAlertPanel (
											   NSLocalizedStringFromTable(@"Reset the emulator",@"Localizable",@"comment"), 
											   NSLocalizedStringFromTable(@"Must be reset",@"Localizable",@"comment"),
											   NSLocalizedStringFromTable(@"Don't reset",@"Localizable",@"comment"), 
											   NSLocalizedStringFromTable(@"Reset",@"Localizable",@"comment"), nil) );
	}
	
	// Commit the new configuration
	if (applyChanges)
	{
		Change_CopyChangedParamsToConfiguration(&CurrentParams, &ConfigureParams, true);
	}
	else
	{
		ConfigureParams = CurrentParams;
	}

	// Close the window
	[window close];	
}

- (void)initKeysDropDown:(NSPopUpButton*)dropDown
{
	[dropDown removeAllItems];
	unsigned int i;
	for (i = 0; i < Preferences_cKeysForJoysticks; i++)
	{
		SDLKey key = Preferences_KeysForJoysticks[i];
		const char* szKeyName = SDL_GetKeyName(key);
		[dropDown addItemWithTitle:[[NSString stringWithCString:szKeyName encoding:NSASCIIStringEncoding] capitalizedString]];	
		[[dropDown lastItem] setTag:key];
	}
}


/*-----------------------------------------------------------------------*/
/*
  Displays the Preferences dialog
*/
- (IBAction)loadPrefs:(id)sender
{
	if (!bInitialized)
	{
		// Note: These inits cannot be done in awakeFromNib as by this time SDL is not yet initialized.

		// Fill the keyboard dropdowns
		[self initKeysDropDown:joystickUp];
		[self initKeysDropDown:joystickRight];
		[self initKeysDropDown:joystickDown];
		[self initKeysDropDown:joystickLeft];		
		[self initKeysDropDown:joystickFire];
		
		// Get and store the number of real joysticks
		cRealJoysticks = SDL_NumJoysticks();

		// Fill the real joysticks dropdown, if any are available
		if (cRealJoysticks > 0)
		{
			[realJoystick removeAllItems];
			int i;
			for (i = 0; i < cRealJoysticks; i++)
			{
				const char* szJoystickName = SDL_JoystickName(i);
				[realJoystick addItemWithTitle:[[NSString stringWithCString:szJoystickName encoding:NSASCIIStringEncoding] capitalizedString]];	
				[[realJoystick lastItem] setTag:i];	
			}
		}
		else	// No real joysticks: Disable the controls
		{
			[[joystickMode cellWithTag:1] setEnabled:FALSE];
			[realJoystick setEnabled:FALSE];
		}
		
		bInitialized = true;
	}


	// Backup of configuration settings to CurrentParams (which we will only
	// commit back to the configuration settings if choosing OK)
	CurrentParams = ConfigureParams;

	[self setAllControls];

	// Display the window
	ModalWrapper *mw=[[ModalWrapper alloc] init];
	
	[mw runModal:window];
	
	[mw release];
	
	//GuiOsx_Pause();
	
	//[[NSApplication sharedApplication] runModalForWindow:window];

}


/*-----------------------------------------------------------------------*/
/*
  Updates the controls following a change in the joystick selection
*/
- (IBAction)changeViewedJoystick:(id)sender
{
	// Save the pre-joystick controls, as we are about to change them
	[self saveJoystickControls];
	
	// Refresh the per-joystick controls
	[self setJoystickControls];
	
	// Update the controls' enabled states
	[self updateEnabledStates:self];
}


/*-----------------------------------------------------------------------*/
/*
  Initializes all controls
*/
- (void)setAllControls
{
	// Import the floppy paths into their controls.
	IMPORT_TEXTFIELD(floppyImageA, ConfigureParams.DiskImage.szDiskFileName[0]); 
	IMPORT_TEXTFIELD(floppyImageB, ConfigureParams.DiskImage.szDiskFileName[1]); 
	
	// Import all the preferences into their controls
	IMPORT_SWITCH(autoInsertB, ConfigureParams.DiskImage.bAutoInsertDiskB);
    IMPORT_SWITCH(blitter, ConfigureParams.System.bBlitter);
	IMPORT_SWITCH(bootFromHD, ConfigureParams.HardDisk.bBootFromHardDisk);	
    IMPORT_SWITCH(captureOnChange, ConfigureParams.Screen.bCrop);
    IMPORT_TEXTFIELD(cartridgeImage, ConfigureParams.Rom.szCartridgeImageFileName);
    IMPORT_RADIO(colorDepth, ConfigureParams.Screen.nVdiColors);
    IMPORT_SWITCH(compatibleCpu, ConfigureParams.System.bCompatibleCpu);
    IMPORT_RADIO(cpuClock, ConfigureParams.System.nCpuFreq);
    IMPORT_RADIO(cpuType, ConfigureParams.System.nCpuLevel);
	IMPORT_TEXTFIELD(defaultImagesLocation, ConfigureParams.DiskImage.szDiskImageDirectory);
    IMPORT_SWITCH(enableMidi, ConfigureParams.Midi.bEnableMidi);
    IMPORT_SWITCH(enablePrinter, ConfigureParams.Printer.bEnablePrinting);
    IMPORT_SWITCH(enableRS232, ConfigureParams.RS232.bEnableRS232);
    IMPORT_SWITCH(enableSound, ConfigureParams.Sound.bEnableSound);
    IMPORT_DROPDOWN(frameSkip, ConfigureParams.Screen.nFrameSkips);
    IMPORT_RADIO(keyboardMapping, ConfigureParams.Keyboard.nKeymapType);
    IMPORT_TEXTFIELD(keyboardMappingFile, ConfigureParams.Keyboard.szMappingFileName);
    IMPORT_RADIO(machineType, ConfigureParams.System.nMachineType);
    IMPORT_RADIO(monitor, ConfigureParams.Screen.nMonitorType);
    IMPORT_SWITCH(patchTimerD, ConfigureParams.System.bPatchTimerD);
    IMPORT_TEXTFIELD(printToFile, ConfigureParams.Printer.szPrintToFileName);
    IMPORT_RADIO(ramSize, ConfigureParams.Memory.nMemorySize);
    IMPORT_TEXTFIELD(readRS232FromFile, ConfigureParams.RS232.szInFileName);
    IMPORT_SWITCH(realTime, ConfigureParams.System.bRealTimeClock);
    IMPORT_SWITCH(fastFDC, ConfigureParams.DiskImage.FastFloppy);
    IMPORT_TEXTFIELD(tosImage, ConfigureParams.Rom.szTosImageFileName);
    IMPORT_SWITCH(useBorders, ConfigureParams.Screen.bAllowOverscan);
    IMPORT_SWITCH(useVDIResolution, ConfigureParams.Screen.bUseExtVdiResolutions);
    IMPORT_TEXTFIELD(writeMidiToFile, ConfigureParams.Midi.sMidiOutFileName);
	IMPORT_RADIO(floppyWriteProtection, ConfigureParams.DiskImage.nWriteProtection);
	IMPORT_RADIO(HDWriteProtection, ConfigureParams.HardDisk.nWriteProtection);
    IMPORT_TEXTFIELD(writeRS232ToFile, ConfigureParams.RS232.szOutFileName);
    // IMPORT_SWITCH(zoomSTLowRes, ConfigureParams.Screen.bZoomLowRes);
	IMPORT_SWITCH(showStatusBar, ConfigureParams.Screen.bShowStatusbar);
	IMPORT_DROPDOWN(enableDSP,ConfigureParams.System.nDSPType);
	IMPORT_TEXTFIELD(configFile, sConfigFileName);

	// 12/04/2010
	IMPORT_SWITCH(falconTTRatio, ConfigureParams.Screen.bAspectCorrect);
	IMPORT_SWITCH(fullScreen, ConfigureParams.Screen.bFullScreen);
	IMPORT_SWITCH(ledDisks, ConfigureParams.Screen.bShowDriveLed);
	IMPORT_SWITCH(keepDesktopResolution, ConfigureParams.Screen.bKeepResolution);
	
	//v1.6.1
	IMPORT_SWITCH(FastBootPatch,ConfigureParams.System.bFastBoot);
	IMPORT_RADIO(YMVoicesMixing,ConfigureParams.Sound.YmVolumeMixing);
	
	//deal with the Max Zoomed Stepper
	IMPORT_NTEXTFIELD(maxZoomedWidth, ConfigureParams.Screen.nMaxWidth);
	IMPORT_NTEXTFIELD(maxZoomedHeight, ConfigureParams.Screen.nMaxHeight);
	
	[widthStepper setDoubleValue:[maxZoomedWidth intValue]];
	[heightStepper setDoubleValue:[maxZoomedHeight intValue]];
	
	
	
	
	[(force8bpp) setState:((ConfigureParams.Screen.nForceBpp==8))? NSOnState : NSOffState];

	
	int i;
	
	for (i = 0; i <= DLGSOUND_50KHZ-DLGSOUND_11KHZ; i++)
	{
		if (ConfigureParams.Sound.nPlaybackFreq > nSoundFreqs[i]-500
		    && ConfigureParams.Sound.nPlaybackFreq < nSoundFreqs[i]+500)
		{
			[playbackQuality selectCellWithTag:(i)];
			break;
		}
	}
	
	
	if (ConfigureParams.Screen.nVdiWidth >= 1024)
		[resolution selectCellWithTag:(2)];
	else if (ConfigureParams.Screen.nVdiWidth >= 768)
		[resolution selectCellWithTag:(1)];
	else
		[resolution selectCellWithTag:(0)];

	// If the HD flag is set, load the HD path, otherwise make it blank
	if (ConfigureParams.HardDisk.bUseHardDiskImage)
	{
		IMPORT_TEXTFIELD(hdImage, ConfigureParams.HardDisk.szHardDiskImage);	
	}
	else
	{
		[hdImage setStringValue:@""];
	}
	
	// If the IDE HD flag is set, load the IDE HD path, otherwise make it blank
	//Master
	if (ConfigureParams.HardDisk.bUseIdeMasterHardDiskImage)
	{
		IMPORT_TEXTFIELD(ideMasterHdImage, ConfigureParams.HardDisk.szIdeMasterHardDiskImage);	
	}
	else
	{
		[ideMasterHdImage setStringValue:@""];
	}
	//Slave
	if (ConfigureParams.HardDisk.bUseIdeSlaveHardDiskImage)
	{
		IMPORT_TEXTFIELD(ideSlaveHdImage, ConfigureParams.HardDisk.szIdeSlaveHardDiskImage);	
	}
	else
	{
		[ideSlaveHdImage setStringValue:@""];
	}
	
	// If the Gemdos flag is set, load the Gemdos path, otherwise make it blank
	if (ConfigureParams.HardDisk.bUseHardDiskDirectories)
	{
		IMPORT_TEXTFIELD(gemdosImage, ConfigureParams.HardDisk.szHardDiskDirectories[0]);
	}
	else
	{
		[gemdosImage setStringValue:@""];
	}
	
	// Set the per-joystick controls		
	[self setJoystickControls];
	
	// Update the controls' enabled states
	[self updateEnabledStates:self];	
}


/*-----------------------------------------------------------------------*/
/*
  Updates the enabled states of controls who depend on other controls
*/
- (IBAction)updateEnabledStates:(id)sender
{
	// Joystick key controls are only enabled if "Use keyboard" is selected
	int nJoystickMode;
	EXPORT_RADIO(joystickMode, nJoystickMode);
	BOOL bUsingKeyboard = (nJoystickMode == JOYSTICK_KEYBOARD);
	[joystickUp setEnabled:bUsingKeyboard];		
	[joystickRight setEnabled:bUsingKeyboard];		
	[joystickDown setEnabled:bUsingKeyboard];		
	[joystickLeft setEnabled:bUsingKeyboard];		
	[joystickFire setEnabled:bUsingKeyboard];		

	// Resolution and colour depth depend on Extended GEM VDI resolution
	BOOL bUsingVDI;
	EXPORT_SWITCH(useVDIResolution, bUsingVDI);
	[resolution setEnabled:bUsingVDI];		
	[colorDepth setEnabled:bUsingVDI];
	
	// Playback quality depends on enable sound
	BOOL bSoundEnabled;
    EXPORT_SWITCH(enableSound, bSoundEnabled);
	[playbackQuality setEnabled:bSoundEnabled];
}


/*-----------------------------------------------------------------------*/
/*
  Updates the joystick controls to match the new joystick selection
*/
- (void)setJoystickControls
{
	// Get and persist the ID of the newly selected joystick
	EXPORT_DROPDOWN(currentJoystick, nCurrentJoystick);

	// Data validation: If the JoyID is out of bounds, correct it and, if set to use real joystick, change to disabled
	if ( (ConfigureParams.Joysticks.Joy[nCurrentJoystick].nJoyId < 0)
	|| (ConfigureParams.Joysticks.Joy[nCurrentJoystick].nJoyId >= cRealJoysticks) )
	{
		ConfigureParams.Joysticks.Joy[nCurrentJoystick].nJoyId = 0;
		if (ConfigureParams.Joysticks.Joy[nCurrentJoystick].nJoystickMode == JOYSTICK_REALSTICK)
		{
			ConfigureParams.Joysticks.Joy[nCurrentJoystick].nJoystickMode = JOYSTICK_DISABLED;
		}	
	}

	// Don't change the realJoystick dropdown if none is available (to keep "(None available)" selected)
	if (cRealJoysticks > 0)
	{
		IMPORT_DROPDOWN(realJoystick, ConfigureParams.Joysticks.Joy[nCurrentJoystick].nJoyId);
	}

	IMPORT_RADIO(joystickMode, ConfigureParams.Joysticks.Joy[nCurrentJoystick].nJoystickMode);
	IMPORT_DROPDOWN(joystickUp, ConfigureParams.Joysticks.Joy[nCurrentJoystick].nKeyCodeUp);
	IMPORT_DROPDOWN(joystickRight, ConfigureParams.Joysticks.Joy[nCurrentJoystick].nKeyCodeRight);
	IMPORT_DROPDOWN(joystickDown, ConfigureParams.Joysticks.Joy[nCurrentJoystick].nKeyCodeDown);
	IMPORT_DROPDOWN(joystickLeft, ConfigureParams.Joysticks.Joy[nCurrentJoystick].nKeyCodeLeft);
	IMPORT_DROPDOWN(joystickFire, ConfigureParams.Joysticks.Joy[nCurrentJoystick].nKeyCodeFire);
	IMPORT_SWITCH(enableAutoFire, ConfigureParams.Joysticks.Joy[nCurrentJoystick].bEnableAutoFire);
}


/*-----------------------------------------------------------------------*/
/*
  Saves the setting for the joystick currently being viewed
*/
- (void)saveJoystickControls
{
	EXPORT_RADIO(joystickMode, ConfigureParams.Joysticks.Joy[nCurrentJoystick].nJoystickMode);	
	EXPORT_DROPDOWN(realJoystick, ConfigureParams.Joysticks.Joy[nCurrentJoystick].nJoyId);
	EXPORT_DROPDOWN(joystickUp, ConfigureParams.Joysticks.Joy[nCurrentJoystick].nKeyCodeUp);
	EXPORT_DROPDOWN(joystickRight, ConfigureParams.Joysticks.Joy[nCurrentJoystick].nKeyCodeRight);
	EXPORT_DROPDOWN(joystickDown, ConfigureParams.Joysticks.Joy[nCurrentJoystick].nKeyCodeDown);
	EXPORT_DROPDOWN(joystickLeft, ConfigureParams.Joysticks.Joy[nCurrentJoystick].nKeyCodeLeft);
	EXPORT_DROPDOWN(joystickFire, ConfigureParams.Joysticks.Joy[nCurrentJoystick].nKeyCodeFire);
	EXPORT_SWITCH(enableAutoFire, ConfigureParams.Joysticks.Joy[nCurrentJoystick].bEnableAutoFire);
}


/*-----------------------------------------------------------------------*/
/*
  Saves the settings for all controls
*/
- (void)saveAllControls
{
	// Export the preference controls into their vars
	EXPORT_SWITCH(autoInsertB, ConfigureParams.DiskImage.bAutoInsertDiskB);
    EXPORT_SWITCH(blitter, ConfigureParams.System.bBlitter);
	EXPORT_SWITCH(bootFromHD, ConfigureParams.HardDisk.bBootFromHardDisk);
    EXPORT_SWITCH(captureOnChange, ConfigureParams.Screen.bCrop);
    EXPORT_TEXTFIELD(cartridgeImage, ConfigureParams.Rom.szCartridgeImageFileName);
    EXPORT_RADIO(colorDepth, ConfigureParams.Screen.nVdiColors);
    EXPORT_SWITCH(compatibleCpu, ConfigureParams.System.bCompatibleCpu);
    EXPORT_RADIO(cpuClock, ConfigureParams.System.nCpuFreq);
    EXPORT_RADIO(cpuType, ConfigureParams.System.nCpuLevel);
	EXPORT_TEXTFIELD(defaultImagesLocation, ConfigureParams.DiskImage.szDiskImageDirectory);
    EXPORT_SWITCH(enableMidi, ConfigureParams.Midi.bEnableMidi);
    EXPORT_SWITCH(enablePrinter, ConfigureParams.Printer.bEnablePrinting);
    EXPORT_SWITCH(enableRS232, ConfigureParams.RS232.bEnableRS232);
    EXPORT_SWITCH(enableSound, ConfigureParams.Sound.bEnableSound);
    EXPORT_DROPDOWN(frameSkip, ConfigureParams.Screen.nFrameSkips);
    EXPORT_RADIO(keyboardMapping, ConfigureParams.Keyboard.nKeymapType);
    EXPORT_TEXTFIELD(keyboardMappingFile, ConfigureParams.Keyboard.szMappingFileName);
    EXPORT_RADIO(machineType, ConfigureParams.System.nMachineType);
    EXPORT_RADIO(monitor, ConfigureParams.Screen.nMonitorType);
    EXPORT_SWITCH(patchTimerD, ConfigureParams.System.bPatchTimerD);
    EXPORT_TEXTFIELD(printToFile, ConfigureParams.Printer.szPrintToFileName);
    EXPORT_RADIO(ramSize, ConfigureParams.Memory.nMemorySize);
    EXPORT_TEXTFIELD(readRS232FromFile, ConfigureParams.RS232.szInFileName);
    EXPORT_SWITCH(realTime, ConfigureParams.System.bRealTimeClock);
    EXPORT_SWITCH(fastFDC, ConfigureParams.DiskImage.FastFloppy);
    EXPORT_TEXTFIELD(tosImage, ConfigureParams.Rom.szTosImageFileName);
    EXPORT_SWITCH(useBorders, ConfigureParams.Screen.bAllowOverscan);
    EXPORT_SWITCH(useVDIResolution, ConfigureParams.Screen.bUseExtVdiResolutions);
    EXPORT_TEXTFIELD(writeMidiToFile, ConfigureParams.Midi.sMidiOutFileName);
	EXPORT_RADIO(floppyWriteProtection, ConfigureParams.DiskImage.nWriteProtection);
    EXPORT_RADIO(HDWriteProtection, ConfigureParams.HardDisk.nWriteProtection);
	EXPORT_TEXTFIELD(writeRS232ToFile, ConfigureParams.RS232.szOutFileName);
    // EXPORT_SWITCH(zoomSTLowRes, ConfigureParams.Screen.bZoomLowRes);
	EXPORT_SWITCH(showStatusBar,ConfigureParams.Screen.bShowStatusbar);
	EXPORT_DROPDOWN(enableDSP,ConfigureParams.System.nDSPType);
	
	EXPORT_SWITCH(falconTTRatio, ConfigureParams.Screen.bAspectCorrect);
	EXPORT_SWITCH(fullScreen, ConfigureParams.Screen.bFullScreen);
	EXPORT_SWITCH(ledDisks, ConfigureParams.Screen.bShowDriveLed);
	EXPORT_SWITCH(keepDesktopResolution, ConfigureParams.Screen.bKeepResolution);
	
	//v1.6.1
	EXPORT_SWITCH(FastBootPatch,ConfigureParams.System.bFastBoot);
	EXPORT_RADIO(YMVoicesMixing,ConfigureParams.Sound.YmVolumeMixing);
	
	EXPORT_NTEXTFIELD(maxZoomedWidth, ConfigureParams.Screen.nMaxWidth);
	EXPORT_NTEXTFIELD(maxZoomedHeight, ConfigureParams.Screen.nMaxHeight);

	ConfigureParams.Screen.nForceBpp = ([force8bpp state] == NSOnState) ? 8 : 0;

	ConfigureParams.Sound.nPlaybackFreq = nSoundFreqs[[[playbackQuality selectedCell] tag]];
			
	switch ([[resolution selectedCell] tag])
	{
	 case 0:
		ConfigureParams.Screen.nVdiWidth = 640;
		ConfigureParams.Screen.nVdiHeight = 480;
		break;
	 case 1:
		ConfigureParams.Screen.nVdiWidth = 800;
		ConfigureParams.Screen.nVdiHeight = 600;
		break;
	 case 2:
		ConfigureParams.Screen.nVdiWidth = 1024;
		ConfigureParams.Screen.nVdiHeight = 768;
		break;
	}

	// Define the HD flag, and export the HD path if one is selected
	if ([[hdImage stringValue] length] > 0)
	{
		EXPORT_TEXTFIELD(hdImage, ConfigureParams.HardDisk.szHardDiskImage);
		ConfigureParams.HardDisk.bUseHardDiskImage = true;
	}
	else
	{
		ConfigureParams.HardDisk.bUseHardDiskImage = false;
	}
	
	// Define the IDE HD flag, and export the IDE HD path if one is selected
	if ([[ideMasterHdImage stringValue] length] > 0)
	{
		EXPORT_TEXTFIELD(ideMasterHdImage, ConfigureParams.HardDisk.szIdeMasterHardDiskImage);
		ConfigureParams.HardDisk.bUseIdeMasterHardDiskImage = true;
	}
	else
	{
		ConfigureParams.HardDisk.bUseIdeMasterHardDiskImage = false;
	}
	
	// IDE Slave
	if ([[ideSlaveHdImage stringValue] length] > 0)
	{
		EXPORT_TEXTFIELD(ideSlaveHdImage, ConfigureParams.HardDisk.szIdeSlaveHardDiskImage);
		ConfigureParams.HardDisk.bUseIdeSlaveHardDiskImage = true;
	}
	else
	{
		ConfigureParams.HardDisk.bUseIdeSlaveHardDiskImage = false;
	}
	
	// Define the Gemdos flag, and export the Gemdos path if one is selected
	if ([[gemdosImage stringValue] length] > 0)
	{
		EXPORT_TEXTFIELD(gemdosImage, ConfigureParams.HardDisk.szHardDiskDirectories[0]);
		ConfigureParams.HardDisk.bUseHardDiskDirectories = true;
	}
	else
	{
		ConfigureParams.HardDisk.bUseHardDiskDirectories = false;
	}
	
	// Save the per-joystick controls		
	[self saveJoystickControls];	
}

// Max Zoomed Adjust

- (IBAction) setWidth:(id)sender;
{
	NSLog(@"Change Max Zoom width: %ld", [sender intValue]);
    [maxZoomedWidth setIntValue: [sender intValue]];
}

- (IBAction) setHeight:(id)sender;
{
	NSLog(@"Change Max Zoom height: %ld", [sender intValue]);
    [maxZoomedHeight setIntValue: [sender intValue]];
}

+(PrefsController*)prefs
{
	static PrefsController* prefs = nil;
	if (!prefs)
		prefs = [[PrefsController alloc] init];
	
	return prefs;
}



@end
