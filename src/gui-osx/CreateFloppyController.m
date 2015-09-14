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

char szPath[FILENAME_MAX] ;

- (IBAction)createFloppyImage:(id)sender
{
	BOOL		cRet;
	int			ret, cTracks, cSectors, cSides ;
	NSString	*defaultDir ;
	NSString	*newCnf ;

	// Get the default images directory
	defaultDir = [NSString stringWithCString:ConfigureParams.DiskImage.szDiskImageDirectory encoding:NSASCIIStringEncoding];

	// Run the SavePanel, then check if the user clicked OK
	newCnf = [NSApp hsavefile:YES defoDir:defaultDir defoFile:nil types:[NSArray arrayWithObjects: allF, nil] ] ;
	if ([newCnf length] != 0)
	{
		[newCnf getCString:szPath maxLength:FILENAME_MAX-1 encoding:NSASCIIStringEncoding] ;
		// Get the tracks, sectors and sides values
		cTracks = [[tracks selectedCell] tag];
		cSectors = [[sectors selectedCell] tag];
		cSides = [[sides selectedCell] tag];

		// Create the image
		cRet=CreateBlankImage_CreateFile(szPath, cTracks, cSectors, cSides, NULL);
		if(cRet==TRUE)
		 {	ret = [NSApp myAlerte:NSInformationalAlertStyle Txt:nil firstB:localize(@"Ignore") alternateB:@"  A:  "
															otherB:@"  B:  " informativeTxt:@""] ;
			if (ret == NSAlertDefaultReturn)
						return ;

			printf("%d\n",ret);
			ret = ret == NSAlertAlternateReturn ? 0 : 1 ;
			Floppy_SetDiskFileName(ret, szPath, NULL);
			Floppy_InsertDiskIntoDrive(ret);
		 } ;
	 } ;
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
