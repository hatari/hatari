/*
  Hatari - CreateFloppyController.m

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Create floppy image window controller implementation file

  Feb-Mar 2006, Sébastien Molines - Created
*/

#import "CreateFloppyController.h"
#import "Shared.h"

#include "main.h"
#include "configuration.h"
#include "createBlankImage.h"
#include "floppy.h"

@implementation CreateFloppyController

- (IBAction)createFloppyImage:(id)sender
{
	BOOL cRet;
	
	// Create a SavePanel
	NSSavePanel *savePanel = [NSSavePanel savePanel];

	// Set its allowed file types
	NSArray* allowedFileTypes = [NSArray arrayWithObjects: @"st", @"msa", @"dim", @"gz", nil];
	[savePanel setAllowedFileTypes:allowedFileTypes];
	
	// Get the default images directory
	NSString* defaultDir = [NSString stringWithCString:ConfigureParams.DiskImage.szDiskImageDirectory encoding:NSASCIIStringEncoding];

	// Run the SavePanel, then check if the user clicked OK
    if ( NSOKButton == [savePanel runModalForDirectory:defaultDir file:nil] )
	{
		// Get the path to the chosen file
		NSString *path = [savePanel filename];
	
		// Make a non-const C string out of it
		const char* constSzPath = [path cStringUsingEncoding:NSASCIIStringEncoding];
		size_t cbPath = strlen(constSzPath) + 1;
		char szPath[cbPath];
		strncpy(szPath, constSzPath, cbPath);
					
		// Get the tracks, sectors and sides values
		int cTracks = [[tracks selectedCell] tag];
		int cSectors = [[sectors selectedCell] tag];
		int cSides = [[sides selectedCell] tag];
					
		// Create the image
		cRet=CreateBlankImage_CreateFile(szPath, cTracks, cSectors, cSides);
		if(cRet==TRUE)
		{
			int ret = NSRunAlertPanel(@"Hatari", @"Insert newly created disk in", @"Ignore", @"A:", @"B:");
			if (ret != NSAlertDefaultReturn)
			{
				printf("%d\n",ret);
				if(ret==-1) ret=1; //0=>Drive 0, -1=>Drive 1
				
				Floppy_SetDiskFileName(ret, szPath, NULL);
				Floppy_InsertDiskIntoDrive(ret);
			}
		}
			
	}
}

- (void)awakeFromNib
{
	// Fill the "Tracks" dropdown
    [tracks removeAllItems];
	int i;
	for (i = 40; i <= 85; i++)
	{
		[tracks addItemWithTitle:[NSString stringWithFormat:@"%d", i]];	
		[[tracks lastItem] setTag:i];
	}
	
	// Select the default value of 80 tracks
	[tracks selectItemAtIndex:[tracks indexOfItemWithTag:80]]; // Equivalent to Tiger-only [tracks selectItemWithTag:80];


}

- (IBAction)runModal:(id)sender
{
	ModalWrapper *mw=[[ModalWrapper alloc] init];
	[mw runModal:window];
	[mw release];
}


@end
