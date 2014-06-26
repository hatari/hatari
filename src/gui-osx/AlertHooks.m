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
	NSString *message ;
	NSRange  cantTOS, firstPv, lastPv ;
	NSAlert  *lalerte ;

	message = [NSString stringWithCString:szMessage encoding:NSASCIIStringEncoding] ;
	//NSLog(@"Notice: %@", message ) ;
	cantTOS = [message rangeOfString:@"Can not load TOS file:"] ;
	firstPv = [message rangeOfString:@"'"] ;
	lastPv = [message rangeOfString:@"'" options:NSBackwardsSearch] ;

	if ((cantTOS.location == NSNotFound) || (firstPv.location==lastPv.location))
		return (NSAlertDefaultReturn == NSRunInformationalAlertPanel(@"Hatari", localize(message), localize(@"Ok"), nil, nil));

	firstPv.location++ ; firstPv.length = lastPv.location-firstPv.location ;
	lalerte = [NSAlert alertWithMessageText:@"Hatari" defaultButton:localize(@"Ok") alternateButton:nil otherButton:nil
	           informativeTextWithFormat:localize(@"Can not load TOS file:"), [NSApp pathUser:[message substringWithRange:firstPv]]] ;

	[lalerte runModal] ;
	return YES ;
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
