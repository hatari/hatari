/*
  Hatari - PrefsController.m

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

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
#define EXPORT_SWITCH(nsbutton, target) target = ([(nsbutton) state] == NSOnState)
#define EXPORT_RADIO(nsmatrix, target) target = [[(nsmatrix) selectedCell] tag]
#define EXPORT_DROPDOWN(nspopupbutton, target) target = [[(nspopupbutton) selectedItem] tag]
#define IMPORT_TEXTFIELD(nstextfield, source) [(nstextfield) setStringValue:[[NSString stringWithCString:(source)] stringByAbbreviatingWithTildeInPath]]
#define IMPORT_SWITCH(nsbutton, source) [(nsbutton) setState:((source))? NSOnState : NSOffState]
#define IMPORT_RADIO(nsmatrix, source) [(nsmatrix) selectCellWithTag:(source)]
#define IMPORT_DROPDOWN(nspopupbutton, source) [(nspopupbutton) selectItemAtIndex:[(nspopupbutton) indexOfItemWithTag:(source)]]

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
		return TRUE;
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
		const char* constSzPath = [path cString];
		size_t cbPath = strlen(constSzPath) + 1;
		char szPath[cbPath];
		strncpy(szPath, constSzPath, cbPath);

		// Insert the floppy image at this path
		Floppy_InsertDiskIntoDrive(drive, szPath, cbPath);
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
	Floppy_EjectDiskFromDrive(0, FALSE);
	
	// Refresh the control
	[floppyImageA setStringValue:@""];
}

- (IBAction)ejectFloppyB:(id)sender
{
	Floppy_EjectDiskFromDrive(1, FALSE);

	// Refresh the control
	[floppyImageB setStringValue:@""];
}

- (IBAction)ejectGemdosImage:(id)sender
{
	// Clear the control. Later. saveAllControls will set the DialogParams accordingly to signal this is ejected
	[gemdosImage setStringValue:@""];
}

- (IBAction)ejectHdImage:(id)sender
{
	// Clear the control. Later. saveAllControls will set the DialogParams accordingly to signal this is ejected
	[hdImage setStringValue:@""];
}


/*-----------------------------------------------------------------------*/
/*
  Methods for the "Load Config" and "Save Config" buttons
*/
- (IBAction)loadConfig:(id)sender
{
	// Load the config into DialogParams
	Dialog_LoadParams();
	
	// Refresh all the controls to match DialogParams
	[self setAllControls];
}

- (IBAction)saveConfig:(id)sender
{
	// Update the DialogParams from the controls
	[self saveAllControls];

	// Save the DialogParams to the config file
	Dialog_SaveParams();
}


/*-----------------------------------------------------------------------*/
/*
  Commits and closes
*/
- (IBAction)commitAndClose:(id)sender
{
	BOOL applyChanges = TRUE;

	// The user clicked OK
	[self saveAllControls];
	
	// If a reset is required, ask the user first
	if (Change_DoNeedReset(&DialogParams))
	{
		applyChanges = ( 0 == NSRunAlertPanel (@"Reset the emulator?", 
		@"The emulator must be reset in order to apply your changes.\nAll current work will be lost.",
		@"Don't reset", @"Reset", nil) );
	}
	
	// Commit the new configuration
	if (applyChanges)
	{
		Change_CopyChangedParamsToConfiguration(&DialogParams, FALSE);
	}

	// Close the window
	[window close];	
}

- (void)initKeysDropDown:(NSPopUpButton*)dropDown
{
	[dropDown removeAllItems];
	int i;
	for (i = 0; i < Preferences_cKeysForJoysticks; i++)
	{
		SDLKey key = Preferences_KeysForJoysticks[i];
		const char* szKeyName = SDL_GetKeyName(key);
		[dropDown addItemWithTitle:[[NSString stringWithCString:szKeyName] capitalizedString]];	
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
				[realJoystick addItemWithTitle:[[NSString stringWithCString:szJoystickName] capitalizedString]];	
				[[realJoystick lastItem] setTag:i];	
			}
		}
		else	// No real joysticks: Disable the controls
		{
			[[joystickMode cellWithTag:1] setEnabled:FALSE];
			[realJoystick setEnabled:FALSE];
		}
		
		bInitialized = TRUE;
	}


	// Copy configuration settings to DialogParams (which we will only commit back to the configuration settings if choosing OK)
	DialogParams = ConfigureParams;

	[self setAllControls];

	// Display the window
	[[ModalWrapper alloc] runModal:window];
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
	// Note: Floppy images are exposed in the prefs dialog, however they aren't stored with the prefs and won't need to be saved on exit.
	IMPORT_TEXTFIELD(floppyImageA, EmulationDrives[0].szFileName); 
	IMPORT_TEXTFIELD(floppyImageB, EmulationDrives[1].szFileName); 
	
	// Import all the preferences into their controls
	IMPORT_SWITCH(autoInsertB, DialogParams.DiskImage.bAutoInsertDiskB);
    IMPORT_SWITCH(blitter, DialogParams.System.bBlitter);
	IMPORT_SWITCH(bootFromHD, DialogParams.HardDisk.bBootFromHardDisk);	
    IMPORT_SWITCH(captureOnChange, DialogParams.Screen.bCaptureChange);
    IMPORT_TEXTFIELD(cartridgeImage, DialogParams.Rom.szCartridgeImageFileName);
    IMPORT_RADIO(colorDepth, DialogParams.Screen.nVdiColors);
    IMPORT_SWITCH(compatibleCpu, DialogParams.System.bCompatibleCpu);
    IMPORT_RADIO(cpuClock, DialogParams.System.nCpuFreq);
    IMPORT_RADIO(cpuType, DialogParams.System.nCpuLevel);
	IMPORT_TEXTFIELD(defaultImagesLocation, DialogParams.DiskImage.szDiskImageDirectory);
    IMPORT_SWITCH(enableMidi, DialogParams.Midi.bEnableMidi);
    IMPORT_SWITCH(enablePrinter, DialogParams.Printer.bEnablePrinting);
    IMPORT_SWITCH(enableRS232, DialogParams.RS232.bEnableRS232);
    IMPORT_SWITCH(enableSound, DialogParams.Sound.bEnableSound);
    IMPORT_DROPDOWN(frameSkip, DialogParams.Screen.nFrameSkips);
    IMPORT_RADIO(keyboardMapping, DialogParams.Keyboard.nKeymapType);
    IMPORT_TEXTFIELD(keyboardMappingFile, DialogParams.Keyboard.szMappingFileName);
    IMPORT_RADIO(machineType, DialogParams.System.nMachineType);
    IMPORT_RADIO(monitor, DialogParams.Screen.nMonitorType);
    IMPORT_SWITCH(patchTimerD, DialogParams.System.bPatchTimerD);
    IMPORT_RADIO(playbackQuality, DialogParams.Sound.nPlaybackQuality);
    IMPORT_TEXTFIELD(printToFile, DialogParams.Printer.szPrintToFileName);
    IMPORT_RADIO(ramSize, DialogParams.Memory.nMemorySize);
    IMPORT_TEXTFIELD(readRS232FromFile, DialogParams.RS232.szInFileName);
    IMPORT_SWITCH(realTime, DialogParams.System.bRealTimeClock);
    IMPORT_SWITCH(slowFDC, DialogParams.System.bSlowFDC);
    IMPORT_TEXTFIELD(tosImage, DialogParams.Rom.szTosImageFileName);
    IMPORT_SWITCH(useBorders, DialogParams.Screen.bAllowOverscan);
    IMPORT_SWITCH(useVDIResolution, DialogParams.Screen.bUseExtVdiResolutions);
    IMPORT_TEXTFIELD(writeMidiToFile, DialogParams.Midi.szMidiOutFileName);
	IMPORT_RADIO(writeProtection, DialogParams.DiskImage.nWriteProtection);
    IMPORT_TEXTFIELD(writeRS232ToFile, DialogParams.RS232.szOutFileName);
    IMPORT_SWITCH(zoomSTLowRes, DialogParams.Screen.bZoomLowRes);

	[(force8bpp) setState:((DialogParams.Screen.nForceBpp==8))? NSOnState : NSOffState];

	if (DialogParams.Screen.nVdiWidth >= 1024)
		[resolution selectCellWithTag:(2)];
	else if (DialogParams.Screen.nVdiWidth >= 768)
		[resolution selectCellWithTag:(1)];
	else
		[resolution selectCellWithTag:(0)];

	// If the HD flag is set, load the HD path, otherwise make it blank
	if (DialogParams.HardDisk.bUseHardDiskImage)
	{
		IMPORT_TEXTFIELD(hdImage, DialogParams.HardDisk.szHardDiskImage);	
	}
	else
	{
		[hdImage setStringValue:@""];
	}
	
	// If the Gemdos flag is set, load the Gemdos path, otherwise make it blank
	if (DialogParams.HardDisk.bUseHardDiskDirectories)
	{
		IMPORT_TEXTFIELD(gemdosImage, DialogParams.HardDisk.szHardDiskDirectories[0]);
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
	if ( (DialogParams.Joysticks.Joy[nCurrentJoystick].nJoyId < 0)
	|| (DialogParams.Joysticks.Joy[nCurrentJoystick].nJoyId >= cRealJoysticks) )
	{
		DialogParams.Joysticks.Joy[nCurrentJoystick].nJoyId = 0;
		if (DialogParams.Joysticks.Joy[nCurrentJoystick].nJoystickMode == JOYSTICK_REALSTICK)
		{
			DialogParams.Joysticks.Joy[nCurrentJoystick].nJoystickMode = JOYSTICK_DISABLED;
		}	
	}

	// Don't change the realJoystick dropdown if none is available (to keep "(None available)" selected)
	if (cRealJoysticks > 0)
	{
		IMPORT_DROPDOWN(realJoystick, DialogParams.Joysticks.Joy[nCurrentJoystick].nJoyId);
	}

	IMPORT_RADIO(joystickMode, DialogParams.Joysticks.Joy[nCurrentJoystick].nJoystickMode);
	IMPORT_DROPDOWN(joystickUp, DialogParams.Joysticks.Joy[nCurrentJoystick].nKeyCodeUp);
	IMPORT_DROPDOWN(joystickRight, DialogParams.Joysticks.Joy[nCurrentJoystick].nKeyCodeRight);
	IMPORT_DROPDOWN(joystickDown, DialogParams.Joysticks.Joy[nCurrentJoystick].nKeyCodeDown);
	IMPORT_DROPDOWN(joystickLeft, DialogParams.Joysticks.Joy[nCurrentJoystick].nKeyCodeLeft);
	IMPORT_DROPDOWN(joystickFire, DialogParams.Joysticks.Joy[nCurrentJoystick].nKeyCodeFire);
	IMPORT_SWITCH(enableAutoFire, DialogParams.Joysticks.Joy[nCurrentJoystick].bEnableAutoFire);
}


/*-----------------------------------------------------------------------*/
/*
  Saves the setting for the joystick currently being viewed
*/
- (void)saveJoystickControls
{
	EXPORT_RADIO(joystickMode, DialogParams.Joysticks.Joy[nCurrentJoystick].nJoystickMode);	
	EXPORT_DROPDOWN(realJoystick, DialogParams.Joysticks.Joy[nCurrentJoystick].nJoyId);
	EXPORT_DROPDOWN(joystickUp, DialogParams.Joysticks.Joy[nCurrentJoystick].nKeyCodeUp);
	EXPORT_DROPDOWN(joystickRight, DialogParams.Joysticks.Joy[nCurrentJoystick].nKeyCodeRight);
	EXPORT_DROPDOWN(joystickDown, DialogParams.Joysticks.Joy[nCurrentJoystick].nKeyCodeDown);
	EXPORT_DROPDOWN(joystickLeft, DialogParams.Joysticks.Joy[nCurrentJoystick].nKeyCodeLeft);
	EXPORT_DROPDOWN(joystickFire, DialogParams.Joysticks.Joy[nCurrentJoystick].nKeyCodeFire);
	EXPORT_SWITCH(enableAutoFire, DialogParams.Joysticks.Joy[nCurrentJoystick].bEnableAutoFire);
}


/*-----------------------------------------------------------------------*/
/*
  Saves the settings for all controls
*/
- (void)saveAllControls
{
	// Export the preference controls into their vars
	EXPORT_SWITCH(autoInsertB, DialogParams.DiskImage.bAutoInsertDiskB);
    EXPORT_SWITCH(blitter, DialogParams.System.bBlitter);
	EXPORT_SWITCH(bootFromHD, DialogParams.HardDisk.bBootFromHardDisk);
    EXPORT_SWITCH(captureOnChange, DialogParams.Screen.bCaptureChange);
    EXPORT_TEXTFIELD(cartridgeImage, DialogParams.Rom.szCartridgeImageFileName);
    EXPORT_RADIO(colorDepth, DialogParams.Screen.nVdiColors);
    EXPORT_SWITCH(compatibleCpu, DialogParams.System.bCompatibleCpu);
    EXPORT_RADIO(cpuClock, DialogParams.System.nCpuFreq);
    EXPORT_RADIO(cpuType, DialogParams.System.nCpuLevel);
	EXPORT_TEXTFIELD(defaultImagesLocation, DialogParams.DiskImage.szDiskImageDirectory);
    EXPORT_SWITCH(enableMidi, DialogParams.Midi.bEnableMidi);
    EXPORT_SWITCH(enablePrinter, DialogParams.Printer.bEnablePrinting);
    EXPORT_SWITCH(enableRS232, DialogParams.RS232.bEnableRS232);
    EXPORT_SWITCH(enableSound, DialogParams.Sound.bEnableSound);
    EXPORT_DROPDOWN(frameSkip, DialogParams.Screen.nFrameSkips);
    EXPORT_RADIO(keyboardMapping, DialogParams.Keyboard.nKeymapType);
    EXPORT_TEXTFIELD(keyboardMappingFile, DialogParams.Keyboard.szMappingFileName);
    EXPORT_RADIO(machineType, DialogParams.System.nMachineType);
    EXPORT_RADIO(monitor, DialogParams.Screen.nMonitorType);
    EXPORT_SWITCH(patchTimerD, DialogParams.System.bPatchTimerD);
    EXPORT_RADIO(playbackQuality, DialogParams.Sound.nPlaybackQuality);
    EXPORT_TEXTFIELD(printToFile, DialogParams.Printer.szPrintToFileName);
    EXPORT_RADIO(ramSize, DialogParams.Memory.nMemorySize);
    EXPORT_TEXTFIELD(readRS232FromFile, DialogParams.RS232.szInFileName);
    EXPORT_SWITCH(realTime, DialogParams.System.bRealTimeClock);
    EXPORT_SWITCH(slowFDC, DialogParams.System.bSlowFDC);
    EXPORT_TEXTFIELD(tosImage, DialogParams.Rom.szTosImageFileName);
    EXPORT_SWITCH(useBorders, DialogParams.Screen.bAllowOverscan);
    EXPORT_SWITCH(useVDIResolution, DialogParams.Screen.bUseExtVdiResolutions);
    EXPORT_TEXTFIELD(writeMidiToFile, DialogParams.Midi.szMidiOutFileName);
	EXPORT_RADIO(writeProtection, DialogParams.DiskImage.nWriteProtection);
    EXPORT_TEXTFIELD(writeRS232ToFile, DialogParams.RS232.szOutFileName);
    EXPORT_SWITCH(zoomSTLowRes, DialogParams.Screen.bZoomLowRes);	

	DialogParams.Screen.nForceBpp = ([force8bpp state] == NSOnState) ? 8 : 16;

	switch ([[resolution selectedCell] tag])
	{
	 case 0:
		DialogParams.Screen.nVdiWidth = 640;
		DialogParams.Screen.nVdiHeight = 480;
		break;
	 case 1:
		DialogParams.Screen.nVdiWidth = 800;
		DialogParams.Screen.nVdiHeight = 600;
		break;
	 case 2:
		DialogParams.Screen.nVdiWidth = 1024;
		DialogParams.Screen.nVdiHeight = 768;
		break;
	}

	// Define the HD flag, and export the HD path if one is selected
	if ([[hdImage stringValue] length] > 0)
	{
		EXPORT_TEXTFIELD(hdImage, DialogParams.HardDisk.szHardDiskImage);
		DialogParams.HardDisk.bUseHardDiskImage = TRUE;
	}
	else
	{
		DialogParams.HardDisk.bUseHardDiskImage = FALSE;
	}
	
	// Define the Gemdos flag, and export the Gemdos path if one is selected
	if ([[gemdosImage stringValue] length] > 0)
	{
		EXPORT_TEXTFIELD(gemdosImage, DialogParams.HardDisk.szHardDiskDirectories[0]);
		DialogParams.HardDisk.bUseHardDiskDirectories = TRUE;
	}
	else
	{
		DialogParams.HardDisk.bUseHardDiskDirectories = FALSE;
	}
	
	// Save the per-joystick controls		
	[self saveJoystickControls];	
}

@end
