/*
  Hatari - CreateFloppyController.m

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Helper code used by the other Cocoa code files

  June 2006, Sébastien Molines - Created
*/

#import <Cocoa/Cocoa.h>
#import "Shared.h"
#import "AlertHooks.h"
#include "main.h"

@implementation ModalWrapper

// Runs an NSWindow modally
- (void)runModal:(NSWindow*)window
{
	// Grab the window
	modalWindow = window;

	// Set the window's delegate
	[window setDelegate:self];

	// Change emulation and UI state
	GuiOsx_Pause();
	
	// Run it as modal
	[NSApp runModalForWindow:window];

	// Restore emulation and UI state
	GuiOsx_Resume();
}

// On closure of the NSWindow, end the modal session
- (void) windowWillClose:(NSNotification *)notification
{
	NSWindow *windowAboutToClose = [notification object];
	
	// Is this our modal window?
	if (windowAboutToClose == modalWindow)
	{
		// Stop the modal loop
		[NSApp stopModal];
	}
}

@end

/*-----------------------------------------------------------------------*/
/*
  Helper function to write the contents of a path as an NSString to a string
*/
void GuiOsx_ExportPathString(NSString* path, char* szTarget, size_t cchTarget)
{
	NSCAssert((szTarget), @"Target buffer must not be null.");
	NSCAssert((cchTarget > 0), @"Target buffer size must be greater than zero.");

	// Copy the string
	strncpy(szTarget, [[path stringByExpandingTildeInPath] cStringUsingEncoding:[NSString defaultCStringEncoding]], cchTarget);
	
	// Make sure it is null terminated (as strncpy does not null-terminate if the buffer is too small)
	szTarget[cchTarget - 1] = 0;
}

/*-----------------------------------------------------------------------*/
/*
  Pauses emulation
*/
void GuiOsx_Pause(void)
{
	// Pause emulation
	Main_PauseEmulation(false);
}

/*-----------------------------------------------------------------------*/
/*
  Switches back to emulation mode
*/
void GuiOsx_Resume(void)
{
	// Resume emulation
	Main_UnPauseEmulation();
}
