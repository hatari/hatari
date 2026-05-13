/*
  Hatari - MacControlClient.h
  Copyright (C) 2026 by manni07
  Created: 2026-05-13

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#import <Foundation/Foundation.h>

@interface MacControlClient : NSObject
{
	NSString *socketPath;
	int serverSocket;
	int controlSocket;
}

- (instancetype)initWithSocketPath:(NSString *)path;
- (BOOL)startListeningWithError:(NSError **)error;
- (BOOL)acceptConnectionWithTimeout:(NSTimeInterval)timeout error:(NSError **)error;
- (BOOL)sendCommand:(NSString *)command response:(NSString **)response error:(NSError **)error;
- (void)close;

@end
