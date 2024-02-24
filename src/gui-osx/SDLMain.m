/*
    SDLMain.m - main entry point for our Cocoa-ized SDL app
       Initial Version: Darrell Walisser <dwaliss1@purdue.edu>
       Non-NIB-Code & other changes: Max Horn <max@quendi.de>
       Modifications for Hatari by Miguel Saro and Jerome Vernet

    Feel free to customize this file to suit your needs
*/

/* Use this flag to determine whether we use SDLMain.nib or not */
#define		SDL_USE_NIB_FILE	1

/* Use this flag to determine whether we use CPS (docking) or not */
#define		SDL_USE_CPS			1

#import "SDL.h"
#import "SDLMain.h"
#import <sys/param.h> // for MAXPATHLEN
#import <unistd.h>

// for Hatari
#import "dialog.h"
#import "floppy.h"
#import "reset.h"
#import "screenSnapShot.h"
#import "memorySnapShot.h"
#import "sound.h"
#import "screen.h"
#import "PrefsController.h"
#import "Shared.h"
#import "video.h"
#import "avi_record.h"
#import "debugui.h"
#import "clocks_timings.h"
#import "change.h"

extern void Main_RequestQuit(int exitval) ;

#ifdef SDL_USE_CPS
// Portions of CPS.h
typedef struct CPSProcessSerNum
{
	UInt32		lo;
	UInt32		hi;
} CPSProcessSerNum;

extern OSErr	CPSGetCurrentProcess( CPSProcessSerNum *psn);
extern OSErr 	CPSEnableForegroundOperation( CPSProcessSerNum *psn, UInt32 _arg2, UInt32 _arg3, UInt32 _arg4, UInt32 _arg5);
extern OSErr	CPSSetFrontProcess( CPSProcessSerNum *psn);

#endif // SDL_USE_CPS

static int		gArgc;
static char		**gArgv;
static BOOL		gFinderLaunch = NO ;
static BOOL		gCalledAppMainline = NO ;


// The main class of the application, the application's delegate
//
@implementation HatariAppDelegate

char szPath[FILENAME_MAX] ;											// for general use

// Set the working directory to the .app's parent directory
- (void) setupWorkingDirectory:(BOOL)shouldChdir
{
	if (shouldChdir)
		chdir([[[NSBundle mainBundle].bundlePath stringByDeletingLastPathComponent] cStringUsingEncoding:NSASCIIStringEncoding]) ;
}


/*
 * Catch document open requests...this lets us notice files when the app
 *  was launched by double-clicking a document, or when a document was
 *  dragged/dropped on the app's icon. You need to have a
 *  CFBundleDocumentsType section in your Info.plist to get this message,
 *  apparently.
 *
 * Files are added to gArgv, so to the app, they'll look like command line
 *  arguments. Previously, apps launched from the finder had nothing but
 *  an argv[0].
 *
 * This message may be received multiple times to open several docs on launch.
 *
 * This message is ignored once the app's mainline has been called.
 */
- (BOOL)application:(NSApplication *)theApplication openFile:(NSString *)filename
{
	const char *temparg;
	size_t arglen;
	char *arg;
	char **newargv;

	if (!gFinderLaunch)			// MacOS is passing command line args.
		return FALSE;

	if (gCalledAppMainline)		// app has started, ignore this document.
		return FALSE;

	temparg = filename.UTF8String ;
	arglen = SDL_strlen(temparg) + 1 ;
	arg = (char *) SDL_malloc(arglen) ;
	if (arg == NULL)
		return FALSE;

	newargv = (char **) realloc(gArgv, sizeof (char *) * (gArgc + 2)) ;
	if (newargv == NULL)
	{
		SDL_free(arg);
		return FALSE;
	}
	gArgv = newargv ;

	SDL_strlcpy(arg, temparg, arglen) ;
	gArgv[gArgc++] = arg ;
	gArgv[gArgc] = NULL ;
	return TRUE;
}

/*----------------------------------------------------------------------*/
// Called when the internal event loop has just started running
/*----------------------------------------------------------------------*/
- (void) applicationDidFinishLaunching: (NSNotification *) note
{
	int status;

	// Set the working directory to the .app's parent directory
	[self setupWorkingDirectory:gFinderLaunch];

	//setenv ("SDL_ENABLEAPPEVENTS", "1", 1) ;

	// Hand off to main application code
	emulationPaused=NO;
	gCalledAppMainline = TRUE;
	status = SDL_main (gArgc, gArgv) ;

	// We're done, thank you for playing 
	exit(status) ;
}

/*----------------------------------------------------------------------*/
// Hatari Stuff
/*----------------------------------------------------------------------*/
- (IBAction)prefsMenu:(id)sender
{
	static int in_propdialog =  0;

	if (in_propdialog)
		return ;
	++in_propdialog ;
	Dialog_DoProperty() ;
	--in_propdialog;
}

/*- (IBAction) openPreferences:(id)sender
{
	[[PrefsController prefs] loadPrefs:sender];
}														// */

/*----------------------------------------------------------------------*/
- (IBAction)debugUI:(id)sender
{
	DebugUI(REASON_USER);
}
/*----------------------------------------------------------------------*/
- (IBAction)warmReset:(id)sender
{
	if ([NSApp myAlerte:NSInformationalAlertStyle Txt:localize(@"Warm reset!") firstB:localize(@"OK") alternateB:localize(@"Cancel")
			otherB:nil informativeTxt:localize(@"Really reset the emulator?")] == NSAlertFirstButtonReturn )
		Reset_Warm();
} 
/*----------------------------------------------------------------------*/
- (IBAction)coldReset:(id)sender
{
	if ([NSApp myAlerte:NSInformationalAlertStyle Txt:localize(@"Cold reset") firstB:localize(@"OK") alternateB:localize(@"Cancel")
			otherB:nil informativeTxt:localize(@"Really reset the emulator?")] == NSAlertFirstButtonReturn )
		Reset_Cold();
}
/*----------------------------------------------------------------------*/
- (IBAction)insertDiskA:(id)sender
{
	[self insertDisk:0] ;
}
/*----------------------------------------------------------------------*/
- (IBAction)insertDiskB:(id)sender
{
	[self insertDisk:1] ;
}
/*----------------------------------------------------------------------*/
- (void)insertDisk:(int)disque 
{
	NSString	*aDisk ;

	aDisk = [NSApp hopenfile:NO defoDir:nil defoFile:@""] ;
	if (aDisk.length == 0) return ;                 		// user canceled

	[aDisk getCString:szPath maxLength:FILENAME_MAX-1 encoding:NSASCIIStringEncoding] ;
	Floppy_SetDiskFileName(disque, szPath, NULL) ;
	Floppy_InsertDiskIntoDrive(disque) ;
}
/*----------------------------------------------------------------------*/
- (IBAction)quit:(id)sender
{
	Main_RequestQuit(0) ;
}

/*----------------------------------------------------------------------*/
/*Controls the enabled state of the menu items							*/
/*----------------------------------------------------------------------*/
- (BOOL)validateMenuItem:(NSMenuItem*)item
{
	if (item == beginCaptureAnim)
	{
		return !Avi_AreWeRecording() ;
	}
	if (item == endCaptureAnim)
	{
		return Avi_AreWeRecording() ;
	}
	if (item == beginCaptureSound)
	{
		return !Sound_AreWeRecording() ;
	}
	if (item == endCaptureSound)
	{
		return Sound_AreWeRecording() ;
	}

	return YES;
}

/*----------------------------------------------------------------------*/

- (NSString*)displayFileSelection:(const char*)pathInParams preferredFileName:(NSString*)preferredFileName allowedExtensions:(NSArray*)allowedExtensions
{
	// BOOL test ;
	NSString *directoryToOpen;
	NSString *fileToPreselect;
	NSString *preferredPath;
	NSString *extensionText;
	NSString *selectFile;

	// Get the path from the user settings
	preferredPath = [[NSString stringWithCString:pathInParams encoding:NSASCIIStringEncoding] stringByAbbreviatingWithTildeInPath];

	if ((preferredPath != nil) && (preferredPath.length > 0))					// Determine the directory and filename
	 {
		directoryToOpen = preferredPath.stringByDeletingLastPathComponent ;		// Existing path: we use it
		fileToPreselect = preferredPath.lastPathComponent;
	 }
	else
	 {
		directoryToOpen = [@"~" stringByExpandingTildeInPath];					// No path: we use the user's directory
		fileToPreselect = preferredFileName;
	 }	;

	if(bInFullScreen)
		Screen_ReturnFromFullScreen();
	//  SavePanel for choosing what file to write
	extensionText = [NSString stringWithFormat:localize(@"Please specify a .%@ file"), [allowedExtensions componentsJoinedByString:localize(@" or a .")] ];

	selectFile = [NSApp hsavefile:YES defoDir:directoryToOpen defoFile:fileToPreselect types:allowedExtensions titre:extensionText ] ;
	if (selectFile.length != 0 )
		return selectFile ;

	return nil;
}
/*----------------------------------------------------------------------*/
- (IBAction)captureScreen:(id)sender
{
	GuiOsx_Pause(false);
	ScreenSnapShot_SaveScreen();
	GuiOsx_Resume();
}
/*----------------------------------------------------------------------*/
- (IBAction)captureAnimation:(id)sender
{
	GuiOsx_Pause(false);
	if(!Avi_AreWeRecording()) {
		NSString* path = [self displayFileSelection:ConfigureParams.Video.AviRecordFile preferredFileName:@"hatari.avi"
									 allowedExtensions:@[@"avi"]];

		if(path) {
			GuiOsx_ExportPathString(path, ConfigureParams.Video.AviRecordFile, sizeof(ConfigureParams.Video.AviRecordFile));
			Avi_StartRecording ( ConfigureParams.Video.AviRecordFile , ConfigureParams.Screen.bCrop ,
					ConfigureParams.Video.AviRecordFps == 0 ?
					ClocksTimings_GetVBLPerSec ( ConfigureParams.System.nMachineType , nScreenRefreshRate ) :
					ClocksTimings_GetVBLPerSec ( ConfigureParams.System.nMachineType , ConfigureParams.Video.AviRecordFps ) ,
				1 << CLOCKS_TIMINGS_SHIFT_VBL ,
				ConfigureParams.Video.AviRecordVcodec );
		}
	} else {
		Avi_StopRecording();
	}
	GuiOsx_Resume();
}
/*----------------------------------------------------------------------*/
- (IBAction)endCaptureAnimation:(id)sender
{
	GuiOsx_Pause(false);
	Avi_StopRecording();
	GuiOsx_Resume();
}
/*----------------------------------------------------------------------*/
- (IBAction)captureSound:(id)sender
{
	GuiOsx_Pause(true);
	NSString* path = [self displayFileSelection:ConfigureParams.Sound.szYMCaptureFileName preferredFileName:@"hatari.wav"
								 allowedExtensions:@[@"ym", @"wav"]];
	if(path) {
		GuiOsx_ExportPathString(path, ConfigureParams.Sound.szYMCaptureFileName, sizeof(ConfigureParams.Sound.szYMCaptureFileName));
		Sound_BeginRecording(ConfigureParams.Sound.szYMCaptureFileName);
	}
	GuiOsx_Resume();
}
/*----------------------------------------------------------------------*/
- (IBAction)endCaptureSound:(id)sender
{
	GuiOsx_Pause(false);
	Sound_EndRecording();
	GuiOsx_Resume();
}
/*----------------------------------------------------------------------*/
- (IBAction)saveMemorySnap:(id)sender
{
	GuiOsx_Pause(true);

	NSString* path = [self displayFileSelection:ConfigureParams.Memory.szMemoryCaptureFileName preferredFileName:@"hatari.sav"
        allowedExtensions:@[@"sav"]];
	if(path) {
		GuiOsx_ExportPathString(path, ConfigureParams.Memory.szMemoryCaptureFileName, sizeof(ConfigureParams.Memory.szMemoryCaptureFileName));
		MemorySnapShot_Capture(ConfigureParams.Memory.szMemoryCaptureFileName, TRUE);
	}

	GuiOsx_Resume();
}
/*----------------------------------------------------------------------*/
- (IBAction)restoreMemorySnap:(id)sender
{
	NSString *directoryToOpen;
	NSString *fileToPreselect;
	NSString *oldPath ;
	NSString *newPath ;

	GuiOsx_Pause(true);

	// Get the path from the user settings
	oldPath = [NSString stringWithCString:(ConfigureParams.Memory.szMemoryCaptureFileName) encoding:NSASCIIStringEncoding];

	if ((oldPath != nil) && (oldPath.length > 0))						// Determine directory and filename
	 {	directoryToOpen = oldPath.stringByDeletingLastPathComponent ;	// existing path: we use it.
		fileToPreselect = oldPath.lastPathComponent ; }
	else
	 {	directoryToOpen = @"~".stringByExpandingTildeInPath ;			// Currently no path: we use user's directory
		fileToPreselect = nil; } ;

	newPath = [NSApp hopenfile:NO defoDir:directoryToOpen defoFile:fileToPreselect] ;
	if (newPath.length != 0)											// Perform the memory snapshot load
		MemorySnapShot_Restore([newPath cStringUsingEncoding:NSASCIIStringEncoding], TRUE);

	GuiOsx_Resume();
}
/*----------------------------------------------------------------------*/
- (IBAction)doFullScreen:(id)sender
{
	// A call to Screen_EnterFullScreen() would be required, but this causes a crash when using
	// SDL runtime 1.2.11, probably due to conflicts between Cocoa and SDL.
	// Therefore we simulate the fullscreen key press instead

	SDL_KeyboardEvent event;
	memset(&event, 0, sizeof(event));
	event.type = SDL_KEYDOWN;
	event.state = SDL_PRESSED;
	event.keysym.sym = SDLK_F11;
	SDL_PushEvent((SDL_Event*)&event);	// Send the F11 key press
	event.type = SDL_KEYUP;
	event.state = SDL_RELEASED;
	SDL_PushEvent((SDL_Event*)&event);	// Send the F11 key release
}

/*----------------------------------------------------------------------*/
- (IBAction)help:(id)sender
{
	NSString *the_help;

	the_help = [[NSBundle mainBundle] pathForResource:@"manual" ofType:@"html" inDirectory:@"HatariHelp"];

	[[NSWorkspace sharedWorkspace] openFile:the_help];
}
/*----------------------------------------------------------------------*/
- (IBAction)compat:(id)sender
{
	NSString *the_help ;

	the_help = [[NSBundle mainBundle] pathForResource:@"compatibility" ofType:@"html" inDirectory:@"HatariHelp"] ;

	[[NSWorkspace sharedWorkspace] openFile:the_help];
}
/*----------------------------------------------------------------------*/
- (IBAction)PauseMenu:(id)sender {
	if(!emulationPaused)
	{
		GuiOsx_Pause(true);
		emulationPaused=YES;
		[pauseMenuItem setState:NSOnState];
	}
	else
	{
		GuiOsx_Resume();
		emulationPaused=NO;
		[pauseMenuItem setState:NSOffState];
	}
}
/*----------------------------------------------------------------------*/
- (IBAction)openConfig:(id)sender
{
	BOOL		applyChanges ;
	NSString	*ConfigFile, *newCfg ;
	CNF_PARAMS	CurrentParams;

	applyChanges = true ;
	ConfigFile = [NSString stringWithCString:(sConfigFileName) encoding:NSASCIIStringEncoding];

	// Backup of configuration settings to CurrentParams (which we will only
	// commit back to the configuration settings if choosing user confirm)
	CurrentParams = ConfigureParams;

	bool bWasRunning = GuiOsx_Pause(true);

	newCfg = [NSApp hopenfile:NO defoDir:nil defoFile:ConfigFile] ;

	if (newCfg.length != 0)
	{
		[newCfg getCString:szPath maxLength:FILENAME_MAX-1 encoding:NSASCIIStringEncoding] ;	// get Cstring  szPath
		Configuration_Load(szPath) ;															// Load the config into ConfigureParams
		strcpy(sConfigFileName,szPath) ;

		// Refresh all the controls to match ConfigureParams
		if (Change_DoNeedReset(&CurrentParams, &ConfigureParams))
		 	applyChanges = [NSApp myAlerte:NSInformationalAlertStyle Txt:localize(@"Reset the emulator") firstB:localize(@"Don't reset")
				alternateB:localize(@"Reset") otherB:nil informativeTxt:@"" ] == NSAlertSecondButtonReturn ;
		if (applyChanges)
			Change_CopyChangedParamsToConfiguration(&CurrentParams, &ConfigureParams, true); 	// Ok with Reset
		else
			ConfigureParams = CurrentParams;   //Restore previous Params.
	} ;

	if (bWasRunning) {
		GuiOsx_Resume();
	}
}

/*----------------------------------------------------------------------*/
- (IBAction)saveConfig:(id)sender {
}

@end
/*----------------------------------------------------------------------*/
static int IsRootCwd(void)
{
	char buf[MAXPATHLEN];
	char *cwd = getcwd(buf, sizeof (buf));
	return (cwd && (strcmp(cwd, "/") == 0));
}
/*----------------------------------------------------------------------*/
static int IsTenPointNineOrLater(void)
{
	// OK for 10.9, but before ??
	NSOperatingSystemVersion systemVersion = [[NSProcessInfo processInfo] operatingSystemVersion];
	return (systemVersion.majorVersion == 10 && systemVersion.minorVersion >= 9)
			|| systemVersion.majorVersion > 10;
}
/*----------------------------------------------------------------------*/
static int IsFinderLaunch(const int argc, char **argv)
{
	/* -psn_XXX is passed if we are launched from Finder in 10.8 and earlier */
	if (argc >= 2 && strncmp(argv[1], "-psn", 4) == 0) {
		return 1;
	}
	if (IsTenPointNineOrLater() && argc == 1 && IsRootCwd()) {
		/* we might still be launched from the Finder; on 10.9+, you might not
		get the -psn command line anymore. Check version, if there's no
		command line, and if our current working directory is "/". */
		return 1;
	}
	return 0;					/* not a Finder launch. */
}

#ifdef main
#  undef main
#endif
/*----------------------------------------------------------------------*/
// Main entry point to executable - should *not* be SDL_main!
/*----------------------------------------------------------------------*/
int main (int argc, char **argv)
{
	// Copy the arguments into a global variable
	if (IsFinderLaunch(argc, argv)) {
		gArgv = (char **) SDL_malloc(sizeof (char *) * 2);
		gArgv[0] = argv[0];
		gArgv[1] = NULL;
		gArgc = 1;
		gFinderLaunch = YES;
	} else {
		int i;
		gArgc = argc;
		gArgv = (char **) SDL_malloc(sizeof (char *) * (argc+1));
		for (i = 0; i <= argc; i++)
			gArgv[i] = argv[i];
		gFinderLaunch = NO;
	}

#if SDL_USE_NIB_FILE
	NSApplicationMain (argc, (const char**)argv);
#else
	CustomApplicationMain (argc, argv);
#endif
	return 0;
}
