/*   SDLMain.m - main entry point for our Cocoa-ized SDL app
       Initial Version: Darrell Walisser <dwaliss1@purdue.edu>
       Non-NIB-Code & other changes: Max Horn <max@quendi.de>

    Feel free to customize this file to suit your needs
*/

#include "SDL.h"
#include "SDLMain.h"
#include <sys/param.h> /* for MAXPATHLEN */
#include <unistd.h>

// for Hatari

#include "dialog.h"
#include "floppy.h"
#include "reset.h"
#include "screenSnapShot.h"
#include "memorySnapShot.h"
#include "sound.h"
#include "screen.h"
#include "PrefsController.h"
#include "Shared.h"
#include "video.h"
#include "avi_record.h"
#include "../debug/debugui.h"
#include "clocks_timings.h"
#include "change.h"

// for Hatari


/* For some reaon, Apple removed setAppleMenu from the headers in 10.4,
 but the method still is there and works. To avoid warnings, we declare
 it ourselves here. */
@interface NSApplication(SDL_Missing_Methods)
- (void)setAppleMenu:(NSMenu *)menu;
@end

/* Use this flag to determine whether we use SDLMain.nib or not */
#define		SDL_USE_NIB_FILE	1

/* Use this flag to determine whether we use CPS (docking) or not */
#define		SDL_USE_CPS		1
#ifdef SDL_USE_CPS
/* Portions of CPS.h */
typedef struct CPSProcessSerNum
{
	UInt32		lo;
	UInt32		hi;
} CPSProcessSerNum;

extern OSErr	CPSGetCurrentProcess( CPSProcessSerNum *psn);
extern OSErr 	CPSEnableForegroundOperation( CPSProcessSerNum *psn, UInt32 _arg2, UInt32 _arg3, UInt32 _arg4, UInt32 _arg5);
extern OSErr	CPSSetFrontProcess( CPSProcessSerNum *psn);

#endif /* SDL_USE_CPS */

static int    gArgc;
static char  **gArgv;
static BOOL   gFinderLaunch;
static BOOL   gCalledAppMainline = FALSE;

static NSString *getApplicationName(void)
{
    const NSDictionary *dict;
    NSString *appName = 0;

    /* Determine the application name */
    dict = (const NSDictionary *)CFBundleGetInfoDictionary(CFBundleGetMainBundle());
    if (dict)
        appName = [dict objectForKey: @"CFBundleName"];
    
    if (![appName length])
        appName = [[NSProcessInfo processInfo] processName];

    return appName;
}

#if SDL_USE_NIB_FILE
/* A helper category for NSString */
@interface NSString (ReplaceSubString)
- (NSString *)stringByReplacingRange:(NSRange)aRange with:(NSString *)aString;
@end
#endif

@interface NSApplication (SDLApplication)
@end

@implementation NSApplication (SDLApplication)
/* Invoked from the Quit menu item */
- (void)terminate:(id)sender
{
    /* Post a SDL_QUIT event */
    SDL_Event event;
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);
}
@end

/* The main class of the application, the application's delegate */
@implementation SDLMain


/* Set the working directory to the .app's parent directory */
- (void) setupWorkingDirectory:(BOOL)shouldChdir
{
    if (shouldChdir)
    {
        char parentdir[MAXPATHLEN];
        CFURLRef url = CFBundleCopyBundleURL(CFBundleGetMainBundle());
        CFURLRef url2 = CFURLCreateCopyDeletingLastPathComponent(0, url);
        if (CFURLGetFileSystemRepresentation(url2, 1, (UInt8 *)parentdir, MAXPATHLEN)) {
            chdir(parentdir);   /* chdir to the binary app's parent */
        }
        CFRelease(url);
        CFRelease(url2);
    }
}

#if SDL_USE_NIB_FILE

/* Fix menu to contain the real app name instead of "SDL App" */
- (void)fixMenu:(NSMenu *)aMenu withAppName:(NSString *)appName
{
    NSRange aRange;
    NSEnumerator *enumerator;
    NSMenuItem *menuItem;

    aRange = [[aMenu title] rangeOfString:@"SDL App"];
    if (aRange.length != 0)
        [aMenu setTitle: [[aMenu title] stringByReplacingRange:aRange with:appName]];

    enumerator = [[aMenu itemArray] objectEnumerator];
    while ((menuItem = [enumerator nextObject]))
    {
        aRange = [[menuItem title] rangeOfString:@"SDL App"];
        if (aRange.length != 0)
            [menuItem setTitle: [[menuItem title] stringByReplacingRange:aRange with:appName]];
        if ([menuItem hasSubmenu])
            [self fixMenu:[menuItem submenu] withAppName:appName];
    }
}

#else

static void setApplicationMenu(void)
{
    /* warning: this code is very odd */
    NSMenu *appleMenu;
    NSMenuItem *menuItem;
    NSString *title;
    NSString *appName;
    
    appName = getApplicationName();
    appleMenu = [[NSMenu alloc] initWithTitle:@""];
    
    /* Add menu items */
    title = [@"About " stringByAppendingString:appName];
    [appleMenu addItemWithTitle:title action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];

    [appleMenu addItem:[NSMenuItem separatorItem]];

    title = [@"Hide " stringByAppendingString:appName];
    [appleMenu addItemWithTitle:title action:@selector(hide:) keyEquivalent:@"h"];

    menuItem = (NSMenuItem *)[appleMenu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
    [menuItem setKeyEquivalentModifierMask:(NSAlternateKeyMask|NSCommandKeyMask)];

    [appleMenu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];

    [appleMenu addItem:[NSMenuItem separatorItem]];

    title = [@"Quit " stringByAppendingString:appName];
    [appleMenu addItemWithTitle:title action:@selector(terminate:) keyEquivalent:@"q"];

    
    /* Put menu into the menubar */
    menuItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    [menuItem setSubmenu:appleMenu];
    [[NSApp mainMenu] addItem:menuItem];

    /* Tell the application object that this is now the application menu */
    [NSApp setAppleMenu:appleMenu];

    /* Finally give up our references to the objects */
    [appleMenu release];
    [menuItem release];
}

/* Create a window menu */
static void setupWindowMenu(void)
{
    NSMenu      *windowMenu;
    NSMenuItem  *windowMenuItem;
    NSMenuItem  *menuItem;

    windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    
    /* "Minimize" item */
    menuItem = [[NSMenuItem alloc] initWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [windowMenu addItem:menuItem];
    [menuItem release];
    
    /* Put menu into the menubar */
    windowMenuItem = [[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""];
    [windowMenuItem setSubmenu:windowMenu];
    [[NSApp mainMenu] addItem:windowMenuItem];
    
    /* Tell the application object that this is now the window menu */
    [NSApp setWindowsMenu:windowMenu];

    /* Finally give up our references to the objects */
    [windowMenu release];
    [windowMenuItem release];
}

/* Replacement for NSApplicationMain */
static void CustomApplicationMain (int argc, char **argv)
{
    NSAutoreleasePool	*pool = [[NSAutoreleasePool alloc] init];
    SDLMain				*sdlMain;

    /* Ensure the application object is initialised */
    [NSApplication sharedApplication];
    
#ifdef SDL_USE_CPS
    {
        CPSProcessSerNum PSN;
        /* Tell the dock about us */
        if (!CPSGetCurrentProcess(&PSN))
            if (!CPSEnableForegroundOperation(&PSN,0x03,0x3C,0x2C,0x1103))
                if (!CPSSetFrontProcess(&PSN))
                    [NSApplication sharedApplication];
    }
#endif /* SDL_USE_CPS */

    /* Set up the menubar */
    [NSApp setMainMenu:[[NSMenu alloc] init]];
    setApplicationMenu();
    setupWindowMenu();

    /* Create SDLMain and make it the app delegate */
    sdlMain = [[SDLMain alloc] init];
    [NSApp setDelegate:sdlMain];
    
    /* Start the main event loop */
    [NSApp run];
    
    [sdlMain release];
    [pool release];
}

#endif


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

    if (!gFinderLaunch)  /* MacOS is passing command line args. */
        return FALSE;

    if (gCalledAppMainline)  /* app has started, ignore this document. */
        return FALSE;

    temparg = [filename UTF8String];
    arglen = SDL_strlen(temparg) + 1;
    arg = (char *) SDL_malloc(arglen);
    if (arg == NULL)
        return FALSE;

    newargv = (char **) realloc(gArgv, sizeof (char *) * (gArgc + 2));
    if (newargv == NULL)
    {
        SDL_free(arg);
        return FALSE;
    }
    gArgv = newargv;

    SDL_strlcpy(arg, temparg, arglen);
    gArgv[gArgc++] = arg;
    gArgv[gArgc] = NULL;
    return TRUE;
}


/* Called when the internal event loop has just started running */
- (void) applicationDidFinishLaunching: (NSNotification *) note
{
    int status;

    /* Set the working directory to the .app's parent directory */
    [self setupWorkingDirectory:gFinderLaunch];

#if SDL_USE_NIB_FILE
    /* Set the main menu to contain the real app name instead of "SDL App" */
    [self fixMenu:[NSApp mainMenu] withAppName:getApplicationName()];
#endif

    /* Hand off to main application code */
    gCalledAppMainline = TRUE;
    status = SDL_main (gArgc, gArgv);

    /* We're done, thank you for playing */
    exit(status);
}

// Hatari Stuff
- (IBAction)prefsMenu:(id)sender
{
	static int in_propdialog =  0;

	if (in_propdialog)
		return;
	++in_propdialog;
	Dialog_DoProperty();
	--in_propdialog;
}

- (IBAction) openPreferences:(id)sender 
{
	[[PrefsController prefs] loadPrefs:sender];
}


- (IBAction)debugUI:(id)sender
{
	DebugUI(REASON_USER);
}

- (IBAction)warmReset:(id)sender
{
	int b;

	b = NSRunAlertPanel (
						 NSLocalizedStringFromTable(@"Warm reset",@"Localizable",@"comment"),
						 NSLocalizedStringFromTable(@"Really reset the emulator?",@"Localizable",@"comment"),
						 NSLocalizedStringFromTable(@"OK",@"Localizable",@"comment"), 
						 NSLocalizedStringFromTable(@"Cancel",@"Localizable",@"comment"), nil);
	//printf("b=%i\n",b);
	if (b == 1)
		Reset_Warm();
} 

- (IBAction)coldReset:(id)sender
{
	int b;

	b = NSRunAlertPanel (
						 NSLocalizedStringFromTable(@"Cold reset!",@"Localizable",@"comment"), 
						 NSLocalizedStringFromTable(@"Really reset the emulator?",@"Localizable",@"comment") ,
						 NSLocalizedStringFromTable(@"OK",@"Localizable",@"comment"), 
						 NSLocalizedStringFromTable(@"Cancel",@"Localizable",@"comment"), nil);
	//printf("b=%i\n",b);
	if (b == 1)
		Reset_Cold();
}

- (IBAction)insertDiskA:(id)sender
{
	NSString *path = nil;
	NSOpenPanel *openPanel = [ NSOpenPanel openPanel ];

	if ( [ openPanel runModalForDirectory:nil
									 file:@"SavedGame" types:nil ] )
	{
		path = [ [ openPanel filenames ] objectAtIndex:0 ];
	}

	if (path != nil)
	{
		// Make a non-const C string out of it
		const char* constSzPath = [path cStringUsingEncoding:NSASCIIStringEncoding];
		size_t cbPath = strlen(constSzPath) + 1;
		char szPath[cbPath];
		strncpy(szPath, constSzPath, cbPath);

		Floppy_SetDiskFileName(0, szPath, NULL);
		Floppy_InsertDiskIntoDrive(0);
	}
}

- (IBAction)insertDiskB:(id)sender
{
	NSString *path = nil;
	NSOpenPanel *openPanel = [ NSOpenPanel openPanel ];

	if ( [ openPanel runModalForDirectory:nil
									 file:@"SavedGame" types:nil ] )
	{
		path = [ [ openPanel filenames ] objectAtIndex:0 ];
	}

	if (path != nil)
	{
		// Make a non-const C string out of it
		const char* constSzPath = [path cStringUsingEncoding:NSASCIIStringEncoding];
		size_t cbPath = strlen(constSzPath) + 1;
		char szPath[cbPath];
		strncpy(szPath, constSzPath, cbPath);	

		Floppy_SetDiskFileName(1, szPath, NULL);
		Floppy_InsertDiskIntoDrive(1);
	}
}

/*-----------------------------------------------------------------------*/
/*
 Controls the enabled state of the menu items
 */
- (BOOL)validateMenuItem:(NSMenuItem*)item
{
	if (item == beginCaptureAnim)
	{
		return !Avi_AreWeRecording();
	}
	if (item == endCaptureAnim)
	{
		return Avi_AreWeRecording();
	}
	if (item == beginCaptureSound)
	{
		return !Sound_AreWeRecording();
	}
	if (item == endCaptureSound)
	{
		return Sound_AreWeRecording();
	}

	return YES;
}

- (NSString*)displayFileSelection:(const char*)pathInParams preferredFileName:(NSString*)preferredFileName allowedExtensions:(NSArray*)allowedExtensions
{
	
	// Get the path from the user settings
	NSString *preferredPath = [[NSString stringWithCString:(pathInParams) encoding:NSASCIIStringEncoding] stringByAbbreviatingWithTildeInPath];
	
	// Determine the directory and filename
	NSString *directoryToOpen;
	NSString *fileToPreselect;
	if ((preferredPath != nil) && ([preferredPath length] > 0))
	{
		// There is existing path: we will open its directory with its file pre-selected.
		directoryToOpen = [preferredPath stringByDeletingLastPathComponent];
		fileToPreselect = [preferredPath lastPathComponent];
	}
	else
	{
		// Currently no path: we will open the user's directory with no file selected.
		directoryToOpen = [@"~" stringByExpandingTildeInPath];
		fileToPreselect = preferredFileName;
	}	
	
	// Create and configure a SavePanel for choosing what file to write
	NSSavePanel *savePanel = [NSSavePanel savePanel];
	[savePanel setAllowedFileTypes:allowedExtensions];
	[savePanel setExtensionHidden:NO];
	NSString* extensionList = [allowedExtensions componentsJoinedByString:@" or a ."];
	
	[savePanel setMessage:[NSString stringWithFormat:@"Please specify a .%@ file",extensionList]];	// TODO: Move to localizable resources	
	// Run the SavePanel, then check if the user clicked OK and selected at least one file
	if (NSFileHandlingPanelOKButton == [savePanel runModalForDirectory:directoryToOpen file:fileToPreselect] )
		return [[savePanel URL] path];
	return nil;
}

- (IBAction)captureScreen:(id)sender
{
	GuiOsx_Pause();
	ScreenSnapShot_SaveScreen();
	GuiOsx_Resume();
}

- (IBAction)captureAnimation:(id)sender
{
	GuiOsx_Pause();
	if(!Avi_AreWeRecording()) {
		NSString* path = [self displayFileSelection:ConfigureParams.Video.AviRecordFile preferredFileName:@"hatari.avi" 
									 allowedExtensions:[NSArray arrayWithObjects:@"avi", nil]];
		
		if(path) {
			GuiOsx_ExportPathString(path, ConfigureParams.Video.AviRecordFile, sizeof(ConfigureParams.Video.AviRecordFile));
			Avi_StartRecording ( ConfigureParams.Video.AviRecordFile , ConfigureParams.Screen.bCrop ,
					ConfigureParams.Video.AviRecordFps == 0 ?
					ClocksTimings_GetVBLPerSec ( ConfigureParams.System.nMachineType , nScreenRefreshRate ) :
					(Uint32)ConfigureParams.Video.AviRecordFps << CLOCKS_TIMINGS_SHIFT_VBL ,
				1 << CLOCKS_TIMINGS_SHIFT_VBL ,
				ConfigureParams.Video.AviRecordVcodec );
		}
		
	} else {
		Avi_StopRecording();
	}
	GuiOsx_Resume();
}

- (IBAction)endCaptureAnimation:(id)sender
{
	//?
}

- (IBAction)captureSound:(id)sender
{
	GuiOsx_Pause();
	NSString* path = [self displayFileSelection:ConfigureParams.Sound.szYMCaptureFileName preferredFileName:@"hatari.wav" 
								 allowedExtensions:[NSArray arrayWithObjects:@"ym", @"wav", nil]];
	if(path) {
		GuiOsx_ExportPathString(path, ConfigureParams.Sound.szYMCaptureFileName, sizeof(ConfigureParams.Sound.szYMCaptureFileName));
		Sound_BeginRecording(ConfigureParams.Sound.szYMCaptureFileName);
	}
	GuiOsx_Resume();
}

- (IBAction)endCaptureSound:(id)sender
{
	GuiOsx_Pause();
	Sound_EndRecording();
	GuiOsx_Resume();
}

- (IBAction)saveMemorySnap:(id)sender
{
	GuiOsx_Pause();

	NSString* path = [self displayFileSelection:ConfigureParams.Memory.szMemoryCaptureFileName preferredFileName:@"hatari.sav" 
								 allowedExtensions:[NSArray arrayWithObjects:@"sav",nil]];
	if(path) {
		GuiOsx_ExportPathString(path, ConfigureParams.Memory.szMemoryCaptureFileName, sizeof(ConfigureParams.Memory.szMemoryCaptureFileName));
		MemorySnapShot_Capture(ConfigureParams.Memory.szMemoryCaptureFileName, TRUE);
	}
	
	GuiOsx_Resume();
}

- (IBAction)restoreMemorySnap:(id)sender
{
	GuiOsx_Pause();

	// Create and configure an OpenPanel
	NSOpenPanel *openPanel = [NSOpenPanel openPanel];

	// Get the path from the user settings
	NSString *oldPath = [NSString stringWithCString:(ConfigureParams.Memory.szMemoryCaptureFileName) encoding:NSASCIIStringEncoding];

	// Determine the directory and filename
	NSString *directoryToOpen;
	NSString *fileToPreselect;
	if ((oldPath != nil) && ([oldPath length] > 0))
	{
		// There is existing path: we will open its directory with its file pre-selected.
		directoryToOpen = [oldPath stringByDeletingLastPathComponent];
		fileToPreselect = [oldPath lastPathComponent];
	}
	else
	{
		// Currently no path: we will open the user's directory with no file selected.
		directoryToOpen = [@"~" stringByExpandingTildeInPath];
		fileToPreselect = nil;
	}

	// Run the OpenPanel, then check if the user clicked OK and selected at least one file
	if ( (NSOKButton == [openPanel runModalForDirectory:directoryToOpen file:fileToPreselect types:nil] )
	    && ([[openPanel filenames] count] > 0) )
	{
		// Get the path to the selected file
		NSString *path = [[openPanel filenames] objectAtIndex:0];
		
		// Perform the memory snapshot load
		MemorySnapShot_Restore([path cStringUsingEncoding:NSASCIIStringEncoding], TRUE);
	}

	GuiOsx_Resume();
}

- (IBAction)doFullScreen:(id)sender
{
	// A call to Screen_EnterFullScreen() would be required, but this causes a crash when using SDL runtime 1.2.11, probably due to conflicts between Cocoa and SDL.
	// Therefore we simulate the fullscreen key press instead
	
	SDL_KeyboardEvent event;
	event.type = SDL_KEYDOWN;
	event.which = 0;
	event.state = SDL_PRESSED;
	event.keysym.sym = SDLK_F11;
	SDL_PushEvent((SDL_Event*)&event);	// Send the F11 key press
	event.type = SDL_KEYUP;
	event.state = SDL_RELEASED;
	SDL_PushEvent((SDL_Event*)&event);	// Send the F11 key release
}


- (IBAction)help:(id)sender
{
NSString *l_aide ;
	
	l_aide = [[NSBundle mainBundle] pathForResource:@"manual" ofType:@"html" inDirectory:@"AideHatari"] ;
	
	if (![[NSWorkspace sharedWorkspace] openFile:l_aide withApplication:@"HelpViewer"])
		if (![[NSWorkspace sharedWorkspace] openFile:l_aide withApplication:@"Help Viewer"])
             [[NSWorkspace sharedWorkspace] openFile:l_aide] ;
}

- (IBAction)compat:(id)sender
{
NSString *C_aide ;
	
	C_aide = [[NSBundle mainBundle] pathForResource:@"compatibility" ofType:@"html" inDirectory:@"AideHatari"] ;
	
	if (![[NSWorkspace sharedWorkspace] openFile:C_aide withApplication:@"HelpViewer"])
		if (![[NSWorkspace sharedWorkspace] openFile:C_aide withApplication:@"Help Viewer"])
             [[NSWorkspace sharedWorkspace] openFile:C_aide] ;
}

- (IBAction)openConfig:(id)sender 
{
	BOOL applyChanges = true;
	NSString *ConfigFile = [NSString stringWithCString:(sConfigFileName) encoding:NSASCIIStringEncoding];
	NSOpenPanel *openPanel = [ NSOpenPanel openPanel ];
	
	CNF_PARAMS CurrentParams;
	
	
	// Backup of configuration settings to CurrentParams (which we will only
	// commit back to the configuration settings if choosing user confirm)
	CurrentParams = ConfigureParams;
	
	GuiOsx_Pause();
	
	if ( [ openPanel runModalForDirectory:nil file:ConfigFile types:nil ] )
	{
		ConfigFile = [ [ openPanel filenames ] objectAtIndex:0 ];
	}
	else
	{
		ConfigFile = nil;
	}
	
	//[openPanel release];
	
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
		
		
	}
	
	GuiOsx_Resume();
	//[ConfigFile release];
}


- (IBAction)saveConfig:(id)sender {
}

@end


@implementation NSString (ReplaceSubString)

- (NSString *)stringByReplacingRange:(NSRange)aRange with:(NSString *)aString
{
    unsigned int bufferSize;
    unsigned int selfLen = [self length];
    unsigned int aStringLen = [aString length];
    unichar *buffer;
    NSRange localRange;
    NSString *result;

    bufferSize = selfLen + aStringLen - aRange.length;
    buffer = (unichar *)NSAllocateMemoryPages(bufferSize*sizeof(unichar));
    
    /* Get first part into buffer */
    localRange.location = 0;
    localRange.length = aRange.location;
    [self getCharacters:buffer range:localRange];
    
    /* Get middle part into buffer */
    localRange.location = 0;
    localRange.length = aStringLen;
    [aString getCharacters:(buffer+aRange.location) range:localRange];
     
    /* Get last part into buffer */
    localRange.location = aRange.location + aRange.length;
    localRange.length = selfLen - localRange.location;
    [self getCharacters:(buffer+aRange.location+aStringLen) range:localRange];
    
    /* Build output string */
    result = [NSString stringWithCharacters:buffer length:bufferSize];
    
    NSDeallocateMemoryPages(buffer, bufferSize);
    
    return result;
}

@end



#ifdef main
#  undef main
#endif


/* Main entry point to executable - should *not* be SDL_main! */
int main (int argc, char **argv)
{
    /* Copy the arguments into a global variable */
    /* This is passed if we are launched by double-clicking */
    if ( argc >= 2 && strncmp (argv[1], "-psn", 4) == 0 ) {
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

