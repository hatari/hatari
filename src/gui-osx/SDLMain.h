/*   SDLMain.m - main entry point for our Cocoa-ized SDL app
       Initial Version: Darrell Walisser <dwaliss1@purdue.edu>
       Non-NIB-Code & other changes: Max Horn <max@quendi.de>

    Feel free to customize this file to suit your needs
*/

#import <Cocoa/Cocoa.h>

@interface SDLMain : NSObject
{
    IBOutlet NSMenuItem *beginCaptureAnim;
    IBOutlet NSMenuItem *endCaptureAnim;
    IBOutlet NSMenuItem *beginCaptureSound;
    IBOutlet NSMenuItem *endCaptureSound;
}
- (IBAction)prefsMenu:(id)sender;
- (IBAction)warmReset:(id)sender;
- (IBAction)coldReset:(id)sender;
- (IBAction)insertDiskA:(id)sender;
- (IBAction)insertDiskB:(id)sender;
- (IBAction)help:(id)sender;
- (IBAction)captureScreen:(id)sender;
- (IBAction)captureAnimation:(id)sender;
- (IBAction)endCaptureAnimation:(id)sender;
- (IBAction)captureSound:(id)sender;
- (IBAction)endCaptureSound:(id)sender;
- (IBAction)saveMemorySnap:(id)sender;
- (IBAction)restoreMemorySnap:(id)sender;
- (BOOL)validateMenuItem:(NSMenuItem*)item;


@end
