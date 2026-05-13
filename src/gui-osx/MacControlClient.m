/*
  Hatari - MacControlClient.m
  Copyright (C) 2026 by manni07
  Created: 2026-05-13

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#import "MacControlClient.h"

#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static NSString * const MacControlClientErrorDomain = @"org.hatari.MacControlClient";

static NSError *MacControlClientMakeError(NSInteger code, NSString *message)
{
	NSDictionary *userInfo;

	userInfo = [NSDictionary dictionaryWithObject:message
	                                       forKey:NSLocalizedDescriptionKey];
	return [NSError errorWithDomain:MacControlClientErrorDomain
	                           code:code
	                       userInfo:userInfo];
}

@implementation MacControlClient

- (instancetype)initWithSocketPath:(NSString *)path
{
	self = [super init];
	if (self)
	{
		socketPath = [path copy];
		serverSocket = -1;
		controlSocket = -1;
	}
	return self;
}

- (void)dealloc
{
	[self close];
	[socketPath release];
	[super dealloc];
}

- (BOOL)startListeningWithError:(NSError **)error
{
	struct sockaddr_un addr;
	const char *path;
	int fd;

	path = [socketPath fileSystemRepresentation];
	if (!path || strlen(path) >= sizeof(addr.sun_path))
	{
		if (error)
			*error = MacControlClientMakeError(ENAMETOOLONG, @"Control socket path is too long.");
		return NO;
	}

	[self close];
	unlink(path);

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
	{
		if (error)
			*error = MacControlClientMakeError(errno, [NSString stringWithUTF8String:strerror(errno)]);
		return NO;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0
	    || listen(fd, 1) < 0)
	{
		if (error)
			*error = MacControlClientMakeError(errno, [NSString stringWithUTF8String:strerror(errno)]);
		close(fd);
		unlink(path);
		return NO;
	}

	serverSocket = fd;
	return YES;
}

- (BOOL)acceptConnectionWithTimeout:(NSTimeInterval)timeout error:(NSError **)error
{
	fd_set readfds;
	struct timeval tv;
	int ready;

	if (serverSocket < 0)
	{
		if (error)
			*error = MacControlClientMakeError(ENOTCONN, @"Control socket is not listening.");
		return NO;
	}

	FD_ZERO(&readfds);
	FD_SET(serverSocket, &readfds);
	tv.tv_sec = (time_t)timeout;
	tv.tv_usec = (suseconds_t)((timeout - tv.tv_sec) * 1000000.0);

	ready = select(serverSocket + 1, &readfds, NULL, NULL, &tv);
	if (ready <= 0)
	{
		if (error)
		{
			*error = MacControlClientMakeError(ready == 0 ? ETIMEDOUT : errno,
			                                  ready == 0 ? @"Timed out waiting for Hatari control socket."
			                                             : [NSString stringWithUTF8String:strerror(errno)]);
		}
		return NO;
	}

	controlSocket = accept(serverSocket, NULL, NULL);
	if (controlSocket < 0)
	{
		if (error)
			*error = MacControlClientMakeError(errno, [NSString stringWithUTF8String:strerror(errno)]);
		return NO;
	}
	return YES;
}

- (BOOL)sendCommand:(NSString *)command response:(NSString **)response error:(NSError **)error
{
	NSData *data;
	NSMutableData *reply;
	char buffer[1024];
	ssize_t nread;
	const char newline = '\n';

	if (controlSocket < 0)
	{
		if (error)
			*error = MacControlClientMakeError(ENOTCONN, @"Hatari control socket is not connected.");
		return NO;
	}

	data = [command dataUsingEncoding:NSUTF8StringEncoding];
	if (write(controlSocket, [data bytes], [data length]) < 0
	    || write(controlSocket, &newline, 1) < 0)
	{
		if (error)
			*error = MacControlClientMakeError(errno, [NSString stringWithUTF8String:strerror(errno)]);
		return NO;
	}

	reply = [NSMutableData data];
	while ((nread = read(controlSocket, buffer, sizeof(buffer))) > 0)
	{
		[reply appendBytes:buffer length:(NSUInteger)nread];
		if (memchr(buffer, '\n', (size_t)nread))
			break;
	}
	if (nread < 0)
	{
		if (error)
			*error = MacControlClientMakeError(errno, [NSString stringWithUTF8String:strerror(errno)]);
		return NO;
	}

	if (response)
	{
		*response = [[[NSString alloc] initWithData:reply
		                                  encoding:NSUTF8StringEncoding] autorelease];
	}
	return YES;
}

- (void)close
{
	const char *path;

	if (controlSocket >= 0)
	{
		close(controlSocket);
		controlSocket = -1;
	}
	if (serverSocket >= 0)
	{
		close(serverSocket);
		serverSocket = -1;
	}
	path = [socketPath fileSystemRepresentation];
	if (path)
		unlink(path);
}

@end
