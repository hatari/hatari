/*   SDLMain.m - main entry point for our Cocoa-ized SDL app
       Initial Version: Darrell Walisser <dwaliss1@purdue.edu>
       Non-NIB-Code & other changes: Max Horn <max@quendi.de>

       Feb-Mar 2006, SŽbastien Molines - Added prefs & create floppy
       June 2006, SŽbastien Molines - Added capture and memory snapshot

    Feel free to customize this file to suit your needs
*/

#import "SDL.h"
#import "SDLMain.h"
#import "Shared.h"
#import <sys/param.h> /* for MAXPATHLEN */
#import <unistd.h>

#include "dialog.h"
#include "floppy.h"
#include "reset.h"
#include "screenSnapShot.h"
#include "memorySnapShot.h"
#include "sound.h"

static int    gArgc;
static char  **gArgv;
static BOOL   gFinderLaunch;

@interface SDLApplication : NSApplication
@end

@implementation SDLApplication
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

- (IBAction)prefsMenu:(id)sender
{
	static int in_propdialog =  0;
	
	if (in_propdialog)
		return;
	++in_propdialog;
    Dialog_DoProperty();
	--in_propdialog;
}

- (IBAction)warmReset:(id)sender
{
	int b;
    
	b = NSRunAlertPanel (@"Warm reset!", 
			@"Really reset the emulator? All current work will be lost. Click Cancel to continue without reset.",
			@"OK", @"Cancel", nil);
	//printf("b=%i\n",b);
	if (b == 1)
		Reset_Warm();
}

- (IBAction)coldReset:(id)sender
{
	int b;
    
	b = NSRunAlertPanel (@"Cold reset!", 
			@"Really reset the emulator? All current work will be lost. Click Cancel to continue without reset.",
			@"OK", @"Cancel", nil);
	//printf("b=%i\n",b);
	if (b == 1)
		Reset_Cold();
}

- (IBAction)insertDiskA:(id)sender
{
    NSString *path = nil;
    NSOpenPanel *openPanel = [ NSOpenPanel openPanel ];
    
    if ( [ openPanel runModalForDirectory:nil
             file:@"SavedGame" types:nil ] ) {
             
        path = [ [ openPanel filenames ] objectAtIndex:0 ];
    }
    
	if (path != nil)
	{
		// Make a non-const C string out of it
		const char* constSzPath = [path cString];
		size_t cbPath = strlen(constSzPath) + 1;
		char szPath[cbPath];
		strncpy(szPath, constSzPath, cbPath);	
	
		Floppy_InsertDiskIntoDrive(0, szPath);
	}
}

- (IBAction)insertDiskB:(id)sender
{
    NSString *path = nil;
    NSOpenPanel *openPanel = [ NSOpenPanel openPanel ];
    
    if ( [ openPanel runModalForDirectory:nil
             file:@"SavedGame" types:nil ] ) {
             
        path = [ [ openPanel filenames ] objectAtIndex:0 ];
    }
    
	if (path != nil)
	{
		// Make a non-const C string out of it
		const char* constSzPath = [path cString];
		size_t cbPath = strlen(constSzPath) + 1;
		char szPath[cbPath];
		strncpy(szPath, constSzPath, cbPath);	
	
		Floppy_InsertDiskIntoDrive(1, szPath);
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
		return !ScreenSnapShot_AreWeRecording();
	}
	if (item == endCaptureAnim)
	{
		return ScreenSnapShot_AreWeRecording();
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

- (IBAction)captureScreen:(id)sender
{
	GuiOsx_PauseAndSwitchToCocoaUI();
	ScreenSnapShot_SaveScreen();
	GuiOsx_ResumeFromCocoaUI();
}

- (IBAction)captureAnimation:(id)sender
{
	GuiOsx_PauseAndSwitchToCocoaUI();
	ScreenSnapShot_BeginRecording(DialogParams.Screen.bCaptureChange, 25);
	GuiOsx_ResumeFromCocoaUI();
}

- (IBAction)endCaptureAnimation:(id)sender
{
	GuiOsx_PauseAndSwitchToCocoaUI();
	ScreenSnapShot_EndRecording();
	GuiOsx_ResumeFromCocoaUI();
}

- (IBAction)captureSound:(id)sender
{
	GuiOsx_PauseAndSwitchToCocoaUI();

	// Get the path from the user settings
	NSString *preferredPath = [[NSString stringWithCString:(ConfigureParams.Sound.szYMCaptureFileName)] stringByAbbreviatingWithTildeInPath];

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
		fileToPreselect = @"hatari.wav";
	}	

	// Create and configure a SavePanel for choosing what file to write
	NSSavePanel *savePanel = [NSSavePanel savePanel];
	[savePanel setAllowedFileTypes:[NSArray arrayWithObjects:@"ym", @"wav", nil]];
	[savePanel setExtensionHidden:NO];
	[savePanel setMessage:@"Please specify an .ym or a .wav file."];	// TODO: Move to localizable resources
	
	// Run the SavePanel, then check if the user clicked OK and selected at least one file
    if (NSFileHandlingPanelOKButton == [savePanel runModalForDirectory:directoryToOpen file:fileToPreselect] )
	{
		// Get the path to the selected file
		NSString *path = [savePanel filename];
		
		// Store the path in the user settings
		GuiOsx_ExportPathString(path, ConfigureParams.Sound.szYMCaptureFileName, sizeof(ConfigureParams.Sound.szYMCaptureFileName));

		// Begin capture
		Sound_BeginRecording(ConfigureParams.Sound.szYMCaptureFileName);
    }	

	GuiOsx_ResumeFromCocoaUI();
}

- (IBAction)endCaptureSound:(id)sender
{
	GuiOsx_PauseAndSwitchToCocoaUI();
	Sound_EndRecording();
	GuiOsx_ResumeFromCocoaUI();
}

- (IBAction)saveMemorySnap:(id)sender
{
	GuiOsx_PauseAndSwitchToCocoaUI();

	// Get the path from the user settings
	NSString *preferredPath = [[NSString stringWithCString:(ConfigureParams.Memory.szMemoryCaptureFileName)] stringByAbbreviatingWithTildeInPath];

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
		// Currently no path: we will open the user's directory with the default filename.
		directoryToOpen = [@"~" stringByExpandingTildeInPath];
		fileToPreselect = @"hatari.sav";
	}	

	// Create and configure a SavePanel for choosing what file to write
	NSSavePanel *savePanel = [NSSavePanel savePanel];
	[savePanel setExtensionHidden:NO];
	
	// Run the SavePanel, then check if the user clicked OK and selected at least one file
    if (NSFileHandlingPanelOKButton == [savePanel runModalForDirectory:directoryToOpen file:fileToPreselect] )
	{
		// Get the path to the selected file
		NSString *path = [savePanel filename];
		
		// Store the path in the user settings
		GuiOsx_ExportPathString(path, ConfigureParams.Memory.szMemoryCaptureFileName, sizeof(ConfigureParams.Memory.szMemoryCaptureFileName));

		// Perform the memory snapshot save
		MemorySnapShot_Capture(ConfigureParams.Memory.szMemoryCaptureFileName);
    }	

	GuiOsx_ResumeFromCocoaUI();
}

- (IBAction)restoreMemorySnap:(id)sender
{
	GuiOsx_PauseAndSwitchToCocoaUI();

	// Create and configure an OpenPanel
    NSOpenPanel *openPanel = [NSOpenPanel openPanel];

	// Get the path from the user settings
	NSString *oldPath = [NSString stringWithCString:(ConfigureParams.Memory.szMemoryCaptureFileName)];

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
		MemorySnapShot_Restore([path cString]);
    }
	
	GuiOsx_ResumeFromCocoaUI();
}

- (IBAction)help:(id)sender
{
	[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"http://hatari.sourceforge.net/"]];
	/*
	NSRunAlertPanel (@"Help", 
			@"Sorry, there is no built-in help available.\n\nPlease visit http://hatari.sourceforge.net/ for more information.\n\n",
			@"OK", nil, nil);
	*/
}


/* Set the working directory to the .app's parent directory */
- (void) setupWorkingDirectory:(BOOL)shouldChdir
{
    char parentdir[MAXPATHLEN];
    char *c;
    
    strncpy ( parentdir, gArgv[0], sizeof(parentdir) );
    c = (char*) parentdir;

    while (*c != '\0')     /* go to end */
        c++;
    
    while (*c != '/')      /* back up to parent */
        c--;
    
    *c++ = '\0';             /* cut off last part (binary name) */
  
    if (shouldChdir)
    {
      assert ( chdir (parentdir) == 0 );   /* chdir to the binary app's parent */
      assert ( chdir ("../../../") == 0 ); /* chdir to the .app's parent */
    }
}


/* Called when the internal event loop has just started running */
- (void) applicationDidFinishLaunching: (NSNotification *) note
{
    int status;

    /* Set the working directory to the .app's parent directory */
    [self setupWorkingDirectory:gFinderLaunch];

    /* Hand off to main application code */
    status = SDL_main (gArgc, gArgv);

    /* We're done, thank you for playing */
    exit(status);
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
    buffer = NSAllocateMemoryPages(bufferSize*sizeof(unichar));
    
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
    int i;
    
    /* This is passed if we are launched by double-clicking */
    if ( argc >= 2 && strncmp (argv[1], "-psn", 4) == 0 ) {
        gArgc = 1;
	gFinderLaunch = YES;
    } else {
        gArgc = argc;
	gFinderLaunch = NO;
    }
    gArgv = (char**) malloc (sizeof(*gArgv) * (gArgc+1));
    assert (gArgv != NULL);
    for (i = 0; i < gArgc; i++)
        gArgv[i] = argv[i];
    gArgv[i] = NULL;

    [SDLApplication poseAsClass:[NSApplication class]];
    NSApplicationMain (argc, (const char**)argv);

    return 0;
}
