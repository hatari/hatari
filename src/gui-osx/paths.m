/*
 Hatari - paths.m

 This file is distributed under the GNU General Public License, version 2
 or at your option any later version. Read the file gpl.txt for details.

 macOS Objective-C helper function for accessing user configured
 screenshot path

 */

#import <Foundation/Foundation.h>

#include "main.h"
#include "paths.h"
#include "str.h"

/**
 * Returns the user configured screenshot path property, or if none is found
 * uses the macOS default of ~/Desktop/. Caller is responsible for freeing
 * the returned pointer.
 */
char *Paths_GetMacScreenShotDir(void) {

	/* Allocate memory for storing the path string */
	char *psPath = malloc(FILENAME_MAX);
	if (!psPath)
	{
		fprintf(stderr, "Out of memory (Paths_GetMacScreenShotDir)\n");
		exit(-1);
	}

	NSString *keyValue = nil;
	NSUserDefaults *defaults = [[NSUserDefaults alloc] init];
	if(defaults != nil) {
		[defaults addSuiteNamed:@"com.apple.screencapture.plist"];
		keyValue = [defaults stringForKey:@"location"];
	}
	if (keyValue!=nil) {
		Str_Copy(psPath, [keyValue UTF8String], FILENAME_MAX);
	} else {
		snprintf(psPath, FILENAME_MAX, "%s%c%s",
			 Paths_GetUserHome(), PATHSEP, "Desktop");
	}

	return psPath;
}
