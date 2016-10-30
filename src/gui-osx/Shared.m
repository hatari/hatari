/*
  Hatari - Shared.m

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Helper code used by the other Cocoa code files

  June 2006, Sébastien Molines - Created
  2013, 2016 M. SARO
*/

#import <Cocoa/Cocoa.h>
#import "Shared.h"
#import "AlertHooks.h"
#import "main.h"

@implementation ModalWrapper

// Runs an NSWindow modally
- (void)runModal:(NSWindow*)window
{
	// Grab the window
	modalWindow = window;

	// Set the window's delegate
	[window setDelegate:self];

	// Change emulation and UI state
	GuiOsx_Pause(true);
	
	// Run it as modal
	[NSApp runModalForWindow:window];

	// Restore emulation and UI state
	GuiOsx_Resume();
}

// On closure of the NSWindow, end the modal session
- (void) windowWillClose:(NSNotification *)notification
{
	NSWindow *windowAboutToClose = notification.object ;
	
	// Is this our modal window?
	if (windowAboutToClose == modalWindow)
	{
		// Stop the modal loop
		[NSApp stopModal];
	}
}

@end

/*-----------------------------------------------------------------------*/
/*Helper function to write the contents of a path as an NSString to a string*/
/*----------------------------------------------------------------------*/
void GuiOsx_ExportPathString(NSString* path, char* szTarget, size_t cchTarget)
{
	NSCAssert((szTarget), @"Target buffer must not be null.");
	NSCAssert((cchTarget > 0), @"Target buffer size must be greater than zero.");

	// Copy the string getCString:maxLength:encoding:
	[path getCString:szTarget maxLength:cchTarget-1 encoding:NSASCIIStringEncoding] ;
}

/*----------------------------------------------------------------------*/
/*Pauses emulation														*/
/* Added the visualize option for 2.0.0									*/
/*----------------------------------------------------------------------*/
void GuiOsx_Pause(bool visualize)
{
	// Pause emulation
	Main_PauseEmulation(visualize);
}

/*-----------------------------------------------------------------------*/
/*Switches back to emulation mode										*/
/*----------------------------------------------------------------------*/
void GuiOsx_Resume(void)
{
	// Resume emulation
	Main_UnPauseEmulation();
}

/*----------------------------------------------------------------------*/
// Add global services.  6 methods
/*----------------------------------------------------------------------*/

@implementation NSApplication (service)

// Open file or directory
//
- (NSString *)hopenfile:(BOOL)chooseDir defoDir:(NSString *)defoDir defoFile:(NSString *)defoFile types:(NSArray *)types
{
	return [self hopenfile:chooseDir defoDir:defoDir defoFile:defoFile types:types titre:nil] ;
}

/*----------------------------------------------------------------------*/
- (NSString *)hopenfile:(BOOL)chooseDir defoDir:(NSString *)defoDir defoFile:(NSString *)defoFile types:(NSArray *)types titre:(NSString *)titre
{
NSOpenPanel *openPanel ;
	NSArray     *lesURLs = nil ;
	BOOL        btOk ;

	openPanel = [NSOpenPanel openPanel];
	openPanel.canChooseDirectories = chooseDir ;
	openPanel.canChooseFiles = !chooseDir;
	openPanel.allowsMultipleSelection = NO ;
	if (types != nil) 
	 {	openPanel.allowedFileTypes = types ;
		openPanel.allowsOtherFileTypes = YES ;  } ;
	if (titre != nil) openPanel.title = titre ;

//	if ([openPanel respondsToSelector:@selector(setDirectoryURL:)])
	 {	if (defoDir!=nil)  openPanel.directoryURL = [NSURL fileURLWithPath:defoDir isDirectory:YES] ;	// A partir de 10.6
		if (defoFile!=nil) openPanel.nameFieldStringValue = defoFile ;
		btOk = [openPanel runModal] == NSModalResponseOK ;                                         			// Ok ?
	 }
//	else
//	 	btOk = [openPanel runModalForDirectory:defoDir file:defoFile] == NSModalResponseOK;					// avant 10.6

	if (btOk)
	 {	lesURLs = openPanel.URLs ;
		if ((lesURLs != nil) && (lesURLs.count != 0))
				return [[lesURLs objectAtIndex:0] path] ;
	 } ;
	return @"" ;
}

/*----------------------------------------------------------------------*/
// Save file
//
- (NSString *)hsavefile:(BOOL)creatDir defoDir:(NSString *)defoDir defoFile:(NSString *)defoFile types:(NSArray *)types
{
	return [self hsavefile:creatDir defoDir:defoDir defoFile:defoFile types:types titre:nil] ;
}
/*----------------------------------------------------------------------*/
- (NSString *)hsavefile:(BOOL)creatDir defoDir:(NSString *)defoDir defoFile:(NSString *)defoFile types:(NSArray *)types titre:(NSString *)titre
{
	NSSavePanel *savPanel ;
	NSURL       *lURL ;
	BOOL        btOk ;

	savPanel = [NSSavePanel savePanel];
	savPanel.canCreateDirectories = creatDir ;
	if (types != nil)
	 {	savPanel.allowedFileTypes = types ;
		savPanel.allowsOtherFileTypes = YES ; } ;
	if (titre != nil)  savPanel.title = titre ;

//	if ([savPanel respondsToSelector:@selector(setDirectoryURL:)])
	 {	if (defoDir!=nil)  savPanel.directoryURL = [NSURL fileURLWithPath:defoDir isDirectory:YES] ;	// A partir de 10.6
		if (defoFile!=nil) savPanel.nameFieldStringValue = defoFile ;
		btOk = [savPanel runModal] == NSModalResponseOK;												// Ok?
	 }
//	else
//		btOk = [savPanel runModalForDirectory:defoDir file:defoFile] == NSOKButton ;					// Ok ? deprecated en 10.6

	if (btOk)
	 {	lURL = savPanel.URL ;
		if (lURL != nil)
			return lURL.path ;
	 } ;
	return @"" ;
}
/*----------------------------------------------------------------------*/
// Returne localized path
//
- (NSString *)localpath:(NSString *)thepath :(NSFileManager *)afilemanager
{
	NSString	*thend ;
	NSArray		*thelist ;

	if (thepath == nil) return @"" ;
	if (thepath.length == 0) return @"" ;
	if (![afilemanager fileExistsAtPath:thepath])
	 {	thend = thepath.lastPathComponent ;
	 	return [[self localpath:thepath.stringByDeletingLastPathComponent :afilemanager] stringByAppendingPathComponent:thend] ;
	 } ;
	thelist = [afilemanager  componentsToDisplayForPath:thepath] ;				// convert in matrix
	if ( thelist.count != 0)
		return [NSString pathWithComponents:thelist] ;                          // return localized path
	else
		return thepath ;
}

- (NSString *)localpath:(NSString *)thepath										// return a full localized path
{
	NSFileManager *afilemanager = [NSFileManager defaultManager] ;              // call "default manager"
	return [self localpath:thepath :afilemanager] ;
}
/*----------------------------------------------------------------------*/
//  return a localized path related to user home directoryr   ~/
//
- (NSString *)pathUser:(NSString *)thepath
{
	NSString *here ;
	NSString *apath ;

	apath = [self localpath:thepath] ;
	if (apath.length == 0) return @"" ;
	here = [self localpath: @"~/".stringByExpandingTildeInPath ] ;
	if (([apath rangeOfString:here].location) != NSNotFound)
		return [NSString stringWithFormat:@"~%@", [apath substringFromIndex:here.length ]] ;
	return apath;
}
/*----------------------------------------------------------------------*/
// NSAlert available 10.3 to 10.9 ..... 10.11
//
- (NSInteger)myAlerte:(NSUInteger)style Txt:(NSString *)Txt firstB:(NSString *)firstB alternateB:(NSString *)alternateB
										otherB:(NSString *)otherB informativeTxt:(NSString *)informativeT
{
	NSAlert		*lalerte ;
	NSInteger	ret ;

	lalerte = [[NSAlert alloc] init] ;
	lalerte.alertStyle = style ;
	lalerte.messageText = Txt == nil ? @"Hatari" : Txt ;
	[lalerte addButtonWithTitle:firstB] ;
	if (alternateB != nil)	[lalerte addButtonWithTitle:alternateB] ;
	if (otherB != nil)		[lalerte addButtonWithTitle:otherB] ;
	if (informativeT!= nil) [lalerte setInformativeText:informativeT] ;
	ret = [lalerte runModal] ;
	[lalerte release] ;
	switch (ret) {
	case NSAlertFirstButtonReturn :
		return NSAlertFirstButtonReturn;
	case NSAlertSecondButtonReturn:
		return NSAlertSecondButtonReturn;
	default:
		return NSAlertFirstButtonReturn;
	} ;
}

@end
