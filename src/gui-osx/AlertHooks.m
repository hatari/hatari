/*
  Hatari - AlertHooks.m

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Hooked alerts, to be used instead of SDL alert windows

  June 2006, Sébastien Molines - Created
*/

#import <Cocoa/Cocoa.h>
#import "AlertHooks.h"
#import "Shared.h"

#ifdef ALERT_HOOKS 

/*-----------------------------------------------------------------------*/
/*
  Displays a Cocoa alert
*/
int HookedAlertNotice(const char* szMessage)
{
//	NSLog(@"Notice: %@",  [NSString stringWithCString:szMessage encoding:NSASCIIStringEncoding] ) ;
	return (NSAlertDefaultReturn == NSRunInformationalAlertPanel(@"Hatari", localize([NSString stringWithCString:szMessage encoding:NSASCIIStringEncoding]), 
															localize(@"Ok"), nil, nil));
}

/*-----------------------------------------------------------------------*/
/*
  Displays a Cocoa alert with a choice (OK and Cancel buttons)
*/
int HookedAlertQuery(const char* szMessage)
{
//	NSLog(@"Alerte: %@",  [NSString stringWithCString:szMessage encoding:NSASCIIStringEncoding] ) ;
	return (NSAlertDefaultReturn == NSRunAlertPanel(@"Hatari", localize([NSString stringWithCString:szMessage encoding:NSASCIIStringEncoding]),
															localize(@"Ok"), localize(@"Cancel"), nil));
}

#endif
