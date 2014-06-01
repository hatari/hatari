/*
  Hatari - Shared.m

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Helper code used by the other Cocoa code files

  June 2006, Sébastien Molines - Created
  2013, M. SARO
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

	// Copy the string getCString:maxLength:encoding:
	[path getCString:szTarget maxLength:cchTarget-1 encoding:NSASCIIStringEncoding] ;
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

//-----------------------------------------------------------------------------------------------------------
// Add global services.  6 methods

@implementation NSApplication (service)

// ouvrir un fichier ou dossier
//
- (NSString *)ouvrir:(BOOL)chooseDir defoDir:(NSString *)defoDir defoFile:(NSString *)defoFile types:(NSArray *)types
{
	return [self ouvrir:chooseDir defoDir:defoDir defoFile:defoFile types:types titre:nil] ;
}

/* informations
NSFileManager *gestion = [NSFileManager defaultManager] ;

  if ([gestion instancesRespondToSelector:@selector(machin)])
   {  // utilisation de  machin  }
 else
   {  // utilisation de truc de 10.5  } ;
*/

- (NSString *)ouvrir:(BOOL)chooseDir defoDir:(NSString *)defoDir defoFile:(NSString *)defoFile types:(NSArray *)types titre:(NSString *)titre
{
NSOpenPanel *openPanel ;
NSArray  *lesURLs = nil ;
BOOL	btOk ;

	openPanel = [NSOpenPanel openPanel];
	[openPanel	setCanChooseDirectories: chooseDir];
	[openPanel	setCanChooseFiles: !chooseDir];
	[openPanel	setAllowsMultipleSelection:NO] ;
	if (types != nil) 
	 {	[openPanel	setAllowedFileTypes:types] ;
		[openPanel	setAllowsOtherFileTypes:YES] ;  } ;
	if (titre != nil)  [openPanel setTitle:titre] ;

/*	if ([NSOpenPanel instancesRespondToSelector:@selector(setNameFieldStringValue:)])
	 {
		if (defoDir!=nil)  [openPanel setDirectoryURL:[NSURL URLWithString:defoDir]] ;	// A partir de 10.6
		if (defoFile!=nil) [openPanel setNameFieldStringValue:defoFile] ;
		btOk = [openPanel runModal] == NSOKButton ;										// Ok ?
	 }
	else																				// */
		btOk = [openPanel runModalForDirectory:defoDir file:defoFile] == NSOKButton	;	// Ok ? deprecated en 10.6

	if (btOk)
	 {	lesURLs = [openPanel URLs] ;
		if ((lesURLs != nil) && ([lesURLs count] != 0))
				return [[lesURLs objectAtIndex:0] path] ;
	 } ;
	return @"" ;
}

// sauver un fichier
//
- (NSString *)sauver:(BOOL)creatDir defoDir:(NSString *)defoDir defoFile:(NSString *)defoFile types:(NSArray *)types
{
	return [self sauver:creatDir defoDir:defoDir defoFile:defoFile types:types titre:nil] ;
}

- (NSString *)sauver:(BOOL)creatDir defoDir:(NSString *)defoDir defoFile:(NSString *)defoFile types:(NSArray *)types titre:(NSString *)titre
{
NSSavePanel *sauvPanel ;
NSURL	*lURL ;
BOOL	btOk ;

	sauvPanel = [NSSavePanel savePanel];
	[sauvPanel setCanCreateDirectories:creatDir];
	if (types != nil)
	 {	[sauvPanel setAllowedFileTypes:types] ;
		[sauvPanel setAllowsOtherFileTypes:YES] ; } ;
	if (titre != nil)  [sauvPanel setTitle:titre] ;

/*	if ([NSSavePanel instancesRespondToSelector:@selector(setNameFieldStringValue:)])
	 {
		if (defoDir!=nil)  [sauvPanel setDirectoryURL:[NSURL URLWithString:defoDir]] ;	// A partir de 10.6
		if (defoFile!=nil) [sauvPanel setNameFieldStringValue:defoFile] ;
		btOk = [sauvPanel runModal] == NSOKButton ;										// Ok?
	 }
	else																				// */
		btOk = [sauvPanel runModalForDirectory:defoDir file:defoFile] == NSOKButton ;	// Ok ? deprecated en 10.6
	
	if (btOk)
	 {	lURL = [sauvPanel URL] ;
		if (lURL != nil)
			return [lURL path] ;
	 } ;
	return @"" ;
}

// retourne le chemin localisé
//
- (NSString *)localpath:(NSString *)cuila :(NSFileManager *)gerer				// réentrante
{
NSString	*lafin ;
NSArray		*Les_composantes ;

	if (cuila == nil) return @"" ;
	if ([cuila length] == 0) return @"" ;
	if (![gerer fileExistsAtPath:cuila])
	 {	lafin = [cuila lastPathComponent] ;
	 	return [[self localpath:[cuila stringByDeletingLastPathComponent] :gerer] stringByAppendingPathComponent:lafin] ;
	 } ;
	Les_composantes = [gerer componentsToDisplayForPath:cuila] ;				// convert in matrix
	if ( [Les_composantes count] != 0)
		return [NSString pathWithComponents:Les_composantes] ;					// return localized path
	else
		return cuila ;
}

- (NSString *)localpath:(NSString *)celuila										// retourne un chemin localisé complet
{
	NSFileManager *gestion = [NSFileManager defaultManager] ;					// call "default manager"
	return [self localpath:celuila :gestion] ;
}

//  retourne un chemin localisé relatif au compte utilisateur (si possible)   ~/Bureau/
//
- (NSString *)pathUser:(NSString *)celuici
{
NSString *ici ;
NSString *chemin ;

	chemin = [self localpath:celuici] ;
	if ([chemin length] == 0) return @"" ;
	ici = [self localpath:[@"~/" stringByExpandingTildeInPath]] ;
	if (([chemin rangeOfString:ici].location) != NSNotFound)
		return [NSString stringWithFormat:@"~%@", [chemin substringFromIndex:[ici length]]] ;
	return chemin ;
}

@end


