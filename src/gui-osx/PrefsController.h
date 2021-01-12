/*
  Hatari - PrefsController.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#import <Cocoa/Cocoa.h>


#if (!defined MAC_OS_X_VERSION_10_12) || MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_12
#define NSAlertStyleInformational NSInformationalAlertStyle
#endif

@interface PrefsController : NSObject
{
	// Preferences window
	IBOutlet NSWindow *window ;
	IBOutlet NSView   *partage ;
	IBOutlet NSView   *hartage ;

	// Disks
	IBOutlet NSTextField *floppyImageA;
	IBOutlet NSButton    *enableDriveA;
	IBOutlet NSButton    *driveA_NumberOfHeads;
	IBOutlet NSTextField *floppyImageB;
	IBOutlet NSButton    *enableDriveB;
	IBOutlet NSButton    *driveB_NumberOfHeads;
	IBOutlet NSButton    *autoInsertB;
	IBOutlet NSButton    *fastFDC;
	IBOutlet NSMatrix    *floppyWriteProtection;
	IBOutlet NSTextField *defaultImagesLocation;
	IBOutlet NSTextField *hdImage;
	IBOutlet NSTextField *ideMasterHdImage;
	IBOutlet NSTextField *ideSlaveHdImage;
	IBOutlet NSTextField *gemdosImage;
	IBOutlet NSButton    *bootFromHD ;
	IBOutlet NSMatrix    *HDWriteProtection;

	IBOutlet NSButton *bFilenameConversion;
	IBOutlet NSButton *nGemdosDrive;
	// ROM
	IBOutlet NSTextField *tosImage;
	IBOutlet NSTextField *cartridgeImage;

	// Atari screen
	IBOutlet NSMatrix *monitor;
	IBOutlet NSButton *useBorders;
	IBOutlet NSButton *falconTTRatio;
	IBOutlet NSButton *zoomSTLowRes;
	IBOutlet NSButton *useVDIResolution;
	IBOutlet NSMatrix *resolution;
	IBOutlet NSMatrix *colorDepth;

	// Display
	IBOutlet NSButton *showStatusBar;
	IBOutlet NSButton *fullScreen;
	IBOutlet NSButton *ledDisks;
	IBOutlet NSPopUpButton *frameSkip;
	IBOutlet NSTextField *maxZoomedWidth;				// N
	IBOutlet NSTextField *maxZoomedHeight;				// N
	IBOutlet NSButton *keepDesktopResolution;
	IBOutlet NSButton *SDL2UseGpuScaling;
	IBOutlet NSButton *SDL2Resizable ;
	IBOutlet NSButton *SDL2UseVSync ;

	// Hidestatus, Capture only, Avi codec, Avi FPS
	// Sound
	IBOutlet NSButton *enableSound;
	IBOutlet NSMatrix *playbackQuality;
	IBOutlet NSMatrix *YMVoicesMixing;

	// System

	IBOutlet NSMatrix *cpuType;
	IBOutlet NSMatrix *cpuClock;
	IBOutlet NSMatrix *machineType;
	IBOutlet NSMatrix *ramSize;					// ram size
	IBOutlet NSButton *compatibleCpu; 			// bCompatibleCpu
	IBOutlet NSButton *blitter;
	IBOutlet NSButton *realTime;
	IBOutlet NSButton *patchTimerD;
	IBOutlet NSButton *FastBootPatch;
	IBOutlet NSPopUpButton *videoTiming;
	// for WinUAE CPU core
	IBOutlet NSButton *cycleExactCPU;			//bCycleExactCpu
	IBOutlet NSButton *MMU_Emulation;
	IBOutlet NSButton *adressSpace24;			// bAddressSpace24
	IBOutlet NSStepper *TTRAMSizeStepper; 		// MS 12-2016
	IBOutlet NSTextField *TTRAMSizeValue;		// MS 12-2016
	//IBOutlet NSButton *CompatibleFPU;
	IBOutlet NSMatrix *FPUType;

	IBOutlet NSButtonCell *bCell68060;

	// load/save state
	IBOutlet NSPopUpButton *enableDSP;

	// Joysticks
	IBOutlet NSPopUpButton *currentJoystick;
	IBOutlet NSMatrix *joystickMode;
	IBOutlet NSPopUpButton *realJoystick;
	IBOutlet NSPopUpButton *joystickUp;
	IBOutlet NSPopUpButton *joystickRight;
	IBOutlet NSPopUpButton *joystickDown;
	IBOutlet NSPopUpButton *joystickLeft;
	IBOutlet NSPopUpButton *joystickFire;
	IBOutlet NSButton *enableAutoFire;

	// Keyboard
	IBOutlet NSMatrix *keyboardMapping;
	IBOutlet NSTextField *keyboardMappingFile;
        // T
		// Disable Key Repeat

	// Peripheral
	IBOutlet NSButton *enablePrinter;
	IBOutlet NSTextField *printToFile;					// T
	IBOutlet NSButton *enableRS232;
	IBOutlet NSTextField *writeRS232ToFile;				// T
	IBOutlet NSTextField *readRS232FromFile;			// T
	IBOutlet NSButton *enableMidi;
	IBOutlet NSTextField *writeMidiToFile;				// T
	__unsafe_unretained IBOutlet NSPopUpButton *midiInPort;
	__unsafe_unretained IBOutlet NSPopUpButton *midiOutPort;

	// Other

	__unsafe_unretained IBOutlet NSButtonCell *confirmQuit;
	IBOutlet NSButton *captureOnChange;
	IBOutlet NSButton *interleaved;
	IBOutlet NSSlider *nSpec512Treshold;
	IBOutlet NSStepper *widthStepper;
	IBOutlet NSStepper *heightStepper;
	IBOutlet NSTextField *configFile;		// T ??

	BOOL		bInitialized;
	int			cRealJoysticks;
	int			nCurrentJoystick;

	BOOL		applyChanges ;							// moved from  
	NSOpenPanel	*opnPanel ;
	NSSavePanel *savPanel ;

	NSMutableString		*cartridge ;
	NSMutableString		*imgeDir ;
	NSMutableString		*floppyA ;
	NSMutableString		*floppyB ;
	NSMutableString		*gemdos ;
	NSMutableString		*hrdDisk ;
	NSMutableString		*masterIDE ;
	NSMutableString		*slaveIDE ;
	NSMutableString		*keyboard ;
	NSMutableString		*midiOut ;
	NSMutableString		*printit ;
	NSMutableString		*rs232In ;
	NSMutableString		*rs232Out ;
	NSMutableString		*TOS ;
	NSMutableString		*configNm ;
}
- (IBAction)changeViewedJoystick:(id)sender;
- (IBAction)chooseCartridgeImage:(id)sender;
- (IBAction)chooseDefaultImagesLocation:(id)sender;
- (IBAction)chooseFloppyImageA:(id)sender;
- (IBAction)chooseFloppyImageB:(id)sender;
- (IBAction)chooseGemdosImage:(id)sender;
- (IBAction)chooseHdImage:(id)sender;
- (IBAction)chooseIdeMasterHdImage:(id)sender;
- (IBAction)chooseIdeSlaveHdImage:(id)sender;
- (IBAction)chooseKeyboardMappingFile:(id)sender;
- (IBAction)chooseMidiOutputFile:(id)sender;
- (IBAction)choosePrintToFile:(id)sender;
- (IBAction)chooseRS232InputFile:(id)sender;
- (IBAction)chooseRS232OutputFile:(id)sender;
- (IBAction)chooseTosImage:(id)sender;
- (IBAction)commitAndClose:(id)sender;
- (IBAction)ejectFloppyA:(id)sender;
- (IBAction)ejectFloppyB:(id)sender;
- (IBAction)ejectGemdosImage:(id)sender;
- (IBAction)ejectHdImage:(id)sender;
- (IBAction)ejectIdeMasterHdImage:(id)sender;
- (IBAction)ejectIdeSlaveHdImage:(id)sender;
- (IBAction)loadPrefs:(id)sender;
- (IBAction)saveConfigAs:(id)sender;
- (IBAction)loadConfigFrom:(id)sender;
- (IBAction)aller:(id)sender ;						// add
- (IBAction)halle:(id)sender ;						// add
- (IBAction)finished:(id)sender;					// add

- (void)setAllControls;
- (void)saveAllControls;
- (void)insertFloppyImageIntoDrive:(int)drive forTextField:(NSTextField*)floppyTextField  realPath:(NSMutableString *)realPath ;
- (BOOL)choosePathForControl:(NSTextField*)textField chooseDirectories:(BOOL)chooseDirectories defaultInitialDir:(NSString*)defaultInitialDir 
																					mutString:(NSMutableString *)mutString what:(NSArray *)what ;
- (void)initKeysDropDown:(NSPopUpButton*)dropDown;
- (void)setJoystickControls;
- (void)saveJoystickControls;
- (IBAction)updateEnabledStates:(id)sender;
- (IBAction)setWidth:(id)sender;
- (IBAction)setHeight:(id)sender;
//System RAM Stepper
- (IBAction)setTTRAMSize:(id)sender;


+(PrefsController*)prefs ;

@end
