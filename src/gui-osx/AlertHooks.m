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
bool HookedAlertNotice(const char* szMessage)
{
	NSString *message ;
	NSRange  cantTOS, firstPv, lastPv ;

	message = [NSString stringWithCString:szMessage encoding:NSASCIIStringEncoding] ;
	//NSLog(@"Notice: %@", message ) ;
	cantTOS = [message rangeOfString:@"Can not load TOS file:"] ;
	firstPv = [message rangeOfString:@"'"] ;
	lastPv = [message rangeOfString:@"'" options:NSBackwardsSearch] ;

	if ((cantTOS.location == NSNotFound) || (firstPv.location==lastPv.location))         // TOS can be found
		return ([NSApp myAlerte:NSAlertStyleInformational Txt:nil firstB:localize(@"Ok") alternateB:localize(@"Cancel")
			otherB:nil informativeTxt:message ] == NSAlertFirstButtonReturn
			//NSAlertDefaultReturn
			);
	else			// TOS can be found
		return ([NSApp myAlerte:NSAlertStyleCritical Txt:nil firstB:localize(@"Ok") alternateB:nil otherB:nil
		        informativeTxt:localize(@"Can not load TOS file:") ]  == NSAlertFirstButtonReturn) ;
}

/*----------------------------------------------------------------------*/
/* Displays a Cocoa alert with a choice (OK and Cancel buttons)			*/
/*----------------------------------------------------------------------*/

bool HookedAlertQuery(const char* szMessage)
{
	NSString *message ;
	int ret;

	message = localize([NSString stringWithCString:szMessage encoding:NSASCIIStringEncoding]) ;
	ret=  [NSApp myAlerte:NSAlertStyleInformational Txt:nil firstB:localize(@"Ok") alternateB:localize(@"Cancel")
	       otherB:nil informativeTxt:message ] ;
	if(ret==NSAlertFirstButtonReturn)
		return true; //OK
	else
		return false; // otherwise false
}

#endif
