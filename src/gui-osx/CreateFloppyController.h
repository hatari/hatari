/*
  Hatari - CreateFloppyController.m

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#import <Cocoa/Cocoa.h>

@interface CreateFloppyController : NSObject
{
    IBOutlet NSWindow *window;
    IBOutlet NSMatrix *sectors;
    IBOutlet NSMatrix *sides;
    IBOutlet NSPopUpButton *tracks;
}
- (void)awakeFromNib;
- (IBAction)runModal:(id)sender;
- (IBAction)createFloppyImage:(id)sender;

@end
