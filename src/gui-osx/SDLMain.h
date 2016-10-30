/*
    SDLMain.m - main entry point for our Cocoa-ized SDL app
       Initial Version: Darrell Walisser <dwaliss1@purdue.edu>
       Non-NIB-Code & other changes: Max Horn <max@quendi.de>
      Modifications for Hatari by Miguel Saro and Jerome Vernet

    Feel free to customize this file to suit your needs
*/

#ifndef _SDLMain_h_
#define _SDLMain_h_
#import <Cocoa/Cocoa.h>
#import "SDL.h"


@interface HatariAppDelegate : NSObject // SDLApplication// NSObject  // <NSApplicationDelegate>
{
    IBOutlet NSMenuItem *beginCaptureAnim;
    IBOutlet NSMenuItem *endCaptureAnim;
    IBOutlet NSMenuItem *beginCaptureSound;
    IBOutlet NSMenuItem *endCaptureSound;
    IBOutlet NSMenuItem *pauseMenuItem;

    BOOL emulationPaused;

}
- (IBAction)PauseMenu:(id)sender;

- (IBAction)openConfig:(id)sender;
- (IBAction)saveConfig:(id)sender;
- (IBAction)prefsMenu:(id)sender;

//- (IBAction)openPreferences:(id)sender;

- (IBAction)warmReset:(id)sender;
- (IBAction)coldReset:(id)sender;
- (IBAction)insertDiskA:(id)sender;
- (IBAction)insertDiskB:(id)sender;
- (IBAction)help:(id)sender;
- (IBAction)compat:(id)sender;
- (IBAction)captureScreen:(id)sender;
- (IBAction)captureAnimation:(id)sender;
- (IBAction)endCaptureAnimation:(id)sender;
//- (IBAction)captureAnimation_AVI:(id)sender;;
//- (IBAction)endCaptureAnimation_AVI:(id)sender;
- (IBAction)captureSound:(id)sender;
- (IBAction)endCaptureSound:(id)sender;
- (IBAction)saveMemorySnap:(id)sender;
- (IBAction)restoreMemorySnap:(id)sender;
- (IBAction)doFullScreen:(id)sender;
- (IBAction)debugUI:(id)sender;
- (IBAction)quit:(id)sender;

- (BOOL)validateMenuItem:(NSMenuItem*)item;
- (void)setupWorkingDirectory:(BOOL)shouldChdir ;
- (BOOL)application:(NSApplication *)theApplication openFile:(NSString *)filename ;
- (void)insertDisk:(int)disque ;
- (BOOL)validateMenuItem:(NSMenuItem*)item ;
- (NSString*)displayFileSelection:(const char*)pathInParams preferredFileName:(NSString*)preferredFileName allowedExtensions:(NSArray*)allowedExtensions ;


@end

#endif /* _SDLMain_h_ */
