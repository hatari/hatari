/*
  Hatari - Shared.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#import <Cocoa/Cocoa.h>

// Wrapper to run an NSWindow modally
@protocol NSWindowDelegate;
@interface ModalWrapper : NSWindowController <NSWindowDelegate>
{
    IBOutlet NSWindow *modalWindow;
}
- (void)runModal:(NSWindow*)window;
- (void)windowWillClose:(NSNotification*)notification;
@end

// Helper function to write the contents of a path as an NSString to a string
void GuiOsx_ExportPathString(NSString* path, char* szTarget, size_t cchTarget);

// Pauses emulation and gets ready to use Cocoa UI
void GuiOsx_Pause(void);

// Switches back to emulation mode and resume emulation
void GuiOsx_Resume(void);
