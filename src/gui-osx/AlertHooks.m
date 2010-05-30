/*
  Hatari - AlertHooks.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Hooked alerts, to be used instead of SDL alert windows

  June 2006, Sébastien Molines - Created
*/

#import <Cocoa/Cocoa.h>
#import "AlertHooks.h"

#ifdef ALERT_HOOKS 

/*-----------------------------------------------------------------------*/
/*
  Displays a Cocoa alert
*/
int HookedAlertNotice(const char* szMessage)
{
	return (NSAlertDefaultReturn == NSRunInformationalAlertPanel(@"Hatari", [NSString stringWithCString:szMessage encoding:NSASCIIStringEncoding], nil, nil, nil));
}

/*-----------------------------------------------------------------------*/
/*
  Displays a Cocoa alert with a choice (OK and Cancel buttons)
*/
int HookedAlertQuery(const char* szMessage)
{
	return (NSAlertDefaultReturn == NSRunAlertPanel(@"Hatari", [NSString stringWithCString:szMessage encoding:NSASCIIStringEncoding],
													NSLocalizedStringFromTable(@"Ok",@"Localizable",@"comment"), 
													NSLocalizedStringFromTable(@"Cancel",@"Localizable",@"comment"), nil));
}

#endif
