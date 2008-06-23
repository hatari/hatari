/*
  Hatari - control.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This code processes commands from the Hatari control socket
*/
const char control_rcsid[] = "Hatari $Id: control.c,v 1.3 2008-06-23 20:56:58 eerot Exp $";

#include "config.h"
#if HAVE_UNIX_DOMAIN_SOCKETS

# include <sys/socket.h>
#include <sys/un.h>

#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <ctype.h>

#include "main.h"
#include "change.h"
#include "control.h"
#include "debugui.h"
#include "file.h"
#include "ikbd.h"
#include "keymap.h"
#include "log.h"
#include "shortcut.h"
#include "str.h"

/* socket from which control command line options are read */
static int ControlSocket;
static FILE *ControlFile;
/* Whether to send embedded window info */
static bool ControlSendEmbedInfo;

/*-----------------------------------------------------------------------*/
/**
 * Parse key command and synthetize key press/release
 * corresponding to given keycode or character.
 * Return FALSE if parsing failed, TRUE otherwise
 * 
 * This can be used by external Hatari UI(s) for
 * string macros, or on devices which lack keyboard
 */
static bool Control_InsertKey(const char *event)
{
	const char *key = NULL;
	bool press;

	if (strncmp(event, "keypress ", 9) == 0) {
		key = &event[9];
		press = TRUE;
	} else if (strncmp(event, "keyrelease ", 11) == 0) {
		key = &event[11];
		press = FALSE;
	}
	if (!(key && key[0])) {
		fprintf(stderr, "ERROR: event '%s' contains no key press/release\n", event);
		return FALSE;
	}
	if (key[1]) {
		char *endptr;
		/* multiple characters, assume it's a keycode */
		int keycode = strtol(key, &endptr, 0);
		/* not a valid number or keycode is out of range? */
		if (*endptr || keycode < 0 || keycode > 255) {
			fprintf(stderr, "ERROR: '%s' is not valid key code, got %d\n",
				key, keycode);
			return FALSE;
		}
		IKBD_PressSTKey(keycode, press);
	} else {
		Keymap_SimulateCharacter(key[0], press);
	}
	fprintf(stderr, "Simulated %s key %s\n",
		key, (press?"press":"release"));
	return TRUE;
}

/*-----------------------------------------------------------------------*/
/**
 * Parse event string and synthetize corresponding event to emulation
 * Return FALSE if parsing failed, TRUE otherwise
 * 
 * This can be used by external Hatari UI(s) on devices which input
 * methods differ from normal keyboard and mouse, such as high DPI
 * touchscreen (no right/middle button, inaccurate clicks)
 */
static bool Control_InsertEvent(const char *event)
{
	if (strcmp(event, "doubleclick") == 0) {
		Keyboard.LButtonDblClk = 1;
		return TRUE;
	}
	if (strcmp(event, "rightpress") == 0) {
		Keyboard.bRButtonDown |= BUTTON_MOUSE;
		return TRUE;
	}
	if (strcmp(event, "rightrelease") == 0) {
		Keyboard.bRButtonDown &= ~BUTTON_MOUSE;
		return TRUE;
	}
	if (Control_InsertKey(event)) {
		return TRUE;
	}
	fprintf(stderr, "ERROR: unrecognized event: '%s'\n", event);
	fprintf(stderr, "Supported events are:\n");
	fprintf(stderr, "- doubleclick\n");
	fprintf(stderr, "- rightpress\n");
	fprintf(stderr, "- rightrelease\n");
	fprintf(stderr, "- keypress <character>\n");
	fprintf(stderr, "- keyrelease <character>\n");
	fprintf(stderr, "<character> can be either a single ASCII char or keycode.\n");
	return FALSE;	
}


/*-----------------------------------------------------------------------*/
/**
 * Parse Hatari option/shortcut/event command buffer.
 * Given buffer is modified in-place.
 * 
 * Returns TRUE until it's OK to return back to emulation
 */
static bool Control_ProcessBuffer(char *buffer)
{
	static bool paused;
	char *cmd, *cmdend;
	int ok = TRUE;
	
	cmd = buffer;
	do {
		/* command terminator? */
		cmdend  = strchr(cmd, '\n');
		if (cmdend) {
			*cmdend = '\0';
		}
		/* process... */
		if (strncmp(cmd, "hatari-debug ", 13) == 0) {
			fprintf(stderr, "%s\n", cmd);
			ok = DebugUI_ParseCommand(Str_Trim(cmd+13));
		} else if (strncmp(cmd, "hatari-event ", 13) == 0) {
			ok = Control_InsertEvent(Str_Trim(cmd+13));
		} else if (strncmp(cmd, "hatari-option ", 14) == 0) {
			ok = Change_ApplyCommandline(cmd+14);
		} else if (strncmp(cmd, "hatari-shortcut ", 16) == 0) {
			ok = Shortcut_Invoke(Str_Trim(cmd+16));
		} else if (strcmp(cmd, "hatari-embed-info") == 0) {
			fprintf(stderr, "Embedded window ID change messages = ON\n");
			ControlSendEmbedInfo = TRUE;
		} else if (strcmp(cmd, "hatari-stop") == 0) {
			if (!paused) {
				fprintf(stderr, "Hatari emulation stopped\n");
				paused = TRUE;
			}
		} else if (strcmp(cmd, "hatari-cont") == 0) {
			if (paused) {
				fprintf(stderr, "Hatari emulation continued\n");
				paused = FALSE;
			}
		} else {
			fprintf(stderr, "ERROR: unrecognized hatari command: '%s'\n", cmd);
			fprintf(stderr, "Supported commands are:\n");
			fprintf(stderr, "- hatari-debug <Debug UI command>\n");
			fprintf(stderr, "- hatari-event <event to simulate>\n");
			fprintf(stderr, "- hatari-option <command line options>\n");
			fprintf(stderr, "- hatari-shortcut <shortcut name>\n");
			fprintf(stderr, "- hatari-embed-info\n");
			fprintf(stderr, "- hatari-stop\n");
			fprintf(stderr, "- hatari-cont\n");
			fprintf(stderr, "The last two can be used to stop and continue the Hatari emulation.\n");
			fprintf(stderr, "All commands need to be separated by newlines.\n");
			ok = FALSE;
		}
		if (cmdend) {
			cmd = cmdend + 1;
		}
	} while (ok && cmdend && *cmd);
	return paused;
}


/*-----------------------------------------------------------------------*/
/**
 * Check ControlSocket for new commands and execute them.
 * Commands should be separated by newlines.
 */
void Control_CheckUpdates(void)
{
	/* just using all trace options with +/- are about 300 chars */
	char buffer[400];
	struct timeval tv;
	fd_set readfds;
	ssize_t bytes;
	int status, sock;
	bool paused;

	/* socket of file? */
	if (ControlSocket) {
		sock = ControlSocket;
	} else if (ControlFile) {
		sock = fileno(ControlFile);
	} else {
		return;
	}
	
	/* ready for reading? */
	paused = FALSE;
	tv.tv_usec = tv.tv_sec = 0;
	do {
		FD_ZERO(&readfds);
		FD_SET(sock, &readfds);
		if (paused) {
			status = select(sock+1, &readfds, NULL, NULL, NULL);
		} else {
			status = select(sock+1, &readfds, NULL, NULL, &tv);
		}
		if (status < 0) {
			perror("Control socket select() error");
			return;
		}
		if (status == 0) {
			return;
		}
		
		/* assume whole command can be read in one go */
		bytes = read(sock, buffer, sizeof(buffer)-1);
		if (bytes < 0)
		{
			perror("Control socket read");
			return;
		}
		if (bytes == 0) {
			/* closed */
			if (ControlSocket) {
				close(ControlSocket);
				ControlSocket = 0;
			} else {
				ControlFile = NULL;
			}
			return;
		}
		buffer[bytes] = '\0';
		paused = Control_ProcessBuffer(buffer);

	} while (paused);
}


/*-----------------------------------------------------------------------*/
/**
 * Open given control socket.  "stdin" is opened as file.
 * Return NULL for success, otherwise an error string
 */
const char *Control_SetSocket(const char *socketpath)
{
	struct sockaddr_un address;
	int newsock;

	if (strcmp(socketpath, "stdin") == 0)
	{
		ControlFile = stdin;
		return NULL;
	}
	
	newsock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (newsock < 0)
	{
		perror("socket creation");
		return "Can't create AF_UNIX socket";
	}

	address.sun_family = AF_UNIX;
	strncpy(address.sun_path, socketpath, sizeof(address.sun_path));
	address.sun_path[sizeof(address.sun_path)-1] = '\0';
	Log_Printf(LOG_INFO, "Connecting to control socket '%s'...\n", address.sun_path);
	if (connect(newsock, &address, sizeof(address)) < 0)
	{
		perror("socket connect");
		close(newsock);
		return "connection to control socket failed";
	}
				
	if (ControlSocket) {
		close(ControlSocket);
	}
	ControlSocket = newsock;
	Log_Printf(LOG_INFO, "new control socket is '%s'\n", socketpath);
	return NULL;
}


/*-----------------------------------------------------------------------
 * Currently works only on X11.
 * 
 * SDL_syswm.h automatically includes everything else needed.
 */
#if HAVE_X11

#include <SDL_syswm.h>

/**
 * Reparent Hatari window if so requested.  Needs to be done inside
 * Hatari because if SDL itself is requested to reparent itself,
 * SDL window stops accepting any input (specifically done like
 * this in SDL backends for some reason).
 * 
 * 'noembed' argument tells whether the SDL window should be embedded
 * or not.
 *
 * If the window is embedded (which means that SDL WM window needs
 * to be hidden) when SDL is asked to fullscreen, Hatari window just
 * disappears when returning back from fullscreen.  I.e. call this
 * with noembed=TRUE _before_ fullscreening and any other time with
 * noembed=FALSE after changing window size.  You can do this by
 * giving bInFullscreen as the noembed value.
 */
void Control_ReparentWindow(int width, int height, bool noembed)
{
	Display *display;
	Window parent_win, sdl_win, wm_win;
	const char *parent_win_id;
	SDL_SysWMinfo info;

	parent_win_id = getenv("PARENT_WIN_ID");
	if (!parent_win_id) {
		return;
	}
	parent_win = strtol(parent_win_id, NULL, 0);
	if (!parent_win) {
		Log_Printf(LOG_WARN, "Invalid PARENT_WIN_ID value '%s'\n", parent_win_id);
		return;
	}

	SDL_VERSION(&info.version);
	if (!SDL_GetWMInfo(&info)) {
		Log_Printf(LOG_WARN, "Failed to get SDL_GetWMInfo()\n");
		return;
	}
	display = info.info.x11.display;
	sdl_win = info.info.x11.window;
	wm_win = info.info.x11.wmwindow;
	info.info.x11.lock_func();
	if (noembed) {
		/* show WM window again */
		XMapWindow(display, wm_win);
	} else {
		char buffer[12];  /* 32-bits in hex (+ '\r') + '\n' + '\0' */
		int sock;

		/* hide WM window for Hatari */
		XUnmapWindow(display, wm_win);
		/* reparent main Hatari window to given parent */
		XReparentWindow(display, sdl_win, parent_win, 0, 0);

		/* whether to send new window size */
		if (ControlSendEmbedInfo) {
			/* socket or file? */
			if (ControlSocket) {
				sock = ControlSocket;
			} else if (ControlFile) {
				sock = fileno(ControlFile);
			} else {
				sock = 0;
			}
			if (sock) {
				fprintf(stderr, "New %dx%d SDL window with ID: %x\n",
					width, height, sdl_win);
				sprintf(buffer, "%dx%d", width, height);
				write(sock, buffer, strlen(buffer));
			}
		}
	}
	info.info.x11.unlock_func();
}

#else
void Control_ReparentWindow(int width, int height, bool noembed)
{
	/* TODO: implement the Windows part.  SDL sources offer example */
	Log_Printf(LOG_INFO, "Support for Hatari window reparenting not built in\n");
}
#endif /* HAVE_X11 */


#endif /* HAVE_UNIX_DOMAIN_SOCKETS */
