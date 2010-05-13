/*
  Hatari - PrefsController.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#import <Cocoa/Cocoa.h>

@interface PrefsController : NSObject
{
    IBOutlet NSWindow *window;
    IBOutlet NSButton *autoInsertB;
    IBOutlet NSButton *blitter;
    IBOutlet NSButton *bootFromHD;
    IBOutlet NSButton *captureOnChange;
    IBOutlet NSTextField *cartridgeImage;
    IBOutlet NSMatrix *colorDepth;
    IBOutlet NSButton *compatibleCpu;
    IBOutlet NSMatrix *cpuClock;
    IBOutlet NSMatrix *cpuType;
    IBOutlet NSPopUpButton *currentJoystick;
    IBOutlet NSTextField *defaultImagesLocation;
    IBOutlet NSButton *enableAutoFire;
    IBOutlet NSButton *enableMidi;
    IBOutlet NSButton *enablePrinter;
    IBOutlet NSButton *enableRS232;
    IBOutlet NSButton *enableSound;
    IBOutlet NSTextField *floppyImageA;
    IBOutlet NSTextField *floppyImageB;
    IBOutlet NSButton *force8bpp;
    IBOutlet NSButton *showStatusBar;
    IBOutlet NSPopUpButton *frameSkip;
    IBOutlet NSTextField *gemdosImage;
    IBOutlet NSTextField *hdImage;
    IBOutlet NSTextField *ideMasterHdImage;
	IBOutlet NSTextField *ideSlaveHdImage;
    IBOutlet NSButton *interleaved;
    IBOutlet NSPopUpButton *joystickDown;
    IBOutlet NSPopUpButton *joystickFire;
    IBOutlet NSPopUpButton *joystickLeft;
    IBOutlet NSMatrix *joystickMode;
    IBOutlet NSPopUpButton *joystickRight;
    IBOutlet NSPopUpButton *joystickUp;
    IBOutlet NSMatrix *keyboardMapping;
    IBOutlet NSTextField *keyboardMappingFile;
    IBOutlet NSMatrix *machineType;
    IBOutlet NSMatrix *monitor;
    IBOutlet NSButton *patchTimerD;
    IBOutlet NSPopUpButton *enableDSP;
    IBOutlet NSMatrix *playbackQuality;
    IBOutlet NSTextField *printToFile;
    IBOutlet NSMatrix *ramSize;
    IBOutlet NSTextField *readRS232FromFile;
    IBOutlet NSPopUpButton *realJoystick;
    IBOutlet NSButton *realTime;
    IBOutlet NSMatrix *resolution;
    IBOutlet NSButton *slowFDC;
    IBOutlet NSTextField *tosImage;
    IBOutlet NSButton *useBorders;
    IBOutlet NSButton *useVDIResolution;
    IBOutlet NSTextField *writeMidiToFile;
    IBOutlet NSMatrix *writeProtection;
    IBOutlet NSTextField *writeRS232ToFile;
    IBOutlet NSButton *zoomSTLowRes;
    IBOutlet NSSlider *nSpec512Treshold;
	
	IBOutlet NSButton *falconTTRatio;
	IBOutlet NSButton *fullScreen;
	IBOutlet NSButton *ledDisks;
	
	IBOutlet NSTextField *maxZoomedWidth;
	IBOutlet NSTextField *maxZoomedHeight;
	IBOutlet NSStepper *widthStepper;
	IBOutlet NSStepper *heightStepper;
	
	

    IBOutlet NSTextField *configFile;

	BOOL bInitialized;
	int cRealJoysticks;
	int nCurrentJoystick;
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
- (void)setAllControls;
- (void)saveAllControls;
- (void)insertFloppyImageIntoDrive:(int)drive forTextField:(NSTextField*)floppyTextField;
- (BOOL)choosePathForControl:(NSTextField*)textField chooseDirectories:(bool)chooseDirectories defaultInitialDir:(NSString*)defaultInitialDir;
- (void)initKeysDropDown:(NSPopUpButton*)dropDown;
- (void)setJoystickControls;
- (void)saveJoystickControls;
- (IBAction)updateEnabledStates:(id)sender;
- (IBAction)setWidth:(id)sender;
- (IBAction)setHeight:(id)sender;



@end
