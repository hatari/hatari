/*
  Hatari - control.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This code processes commands from the Hatari control socket
*/
const char Control_fileid[] = "Hatari control.c : " __DATE__ " " __TIME__;

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
#include "configuration.h"
#include "control.h"
#include "debugui.h"
#include "file.h"
#include "ikbd.h"
#include "keymap.h"
#include "log.h"
#include "midi.h"
#include "printer.h"
#include "rs232.h"
#include "shortcut.h"
#include "str.h"

typedef enum {
	DO_DISABLE,
	DO_ENABLE,
	DO_TOGGLE
} action_t;

/* socket from which control command line options are read */
static int ControlSocket;
/* Whether to send embedded window info */
static bool bSendEmbedInfo;
/* Pausing triggered remotely (battery save pause) */
static bool bRemotePaused;

/* pre-declared local functions */
static int Control_GetUISocket(void);


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
 * Parse event name and synthetize corresponding event to emulation
 * Return FALSE if name parsing failed, TRUE otherwise
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
 * Parse device name and enable/disable/toggle & init/uninit it according
 * to action.  Return FALSE if name parsing failed, TRUE otherwise
 */
static bool Control_DeviceAction(const char *name, action_t action)
{
	/* Note: e.g. RTC would require restarting emulation
	 * and HD-boot setting emulation reboot.  Devices
	 * listed here work just with init/uninit.
	 */
	struct {
		const char *name;
		bool *pvalue;
		void(*init)(void);
		void(*uninit)(void);
	} item[] = {
		{ "printer", &ConfigureParams.Printer.bEnablePrinting, Printer_Init, Printer_UnInit },
		{ "rs232",   &ConfigureParams.RS232.bEnableRS232, RS232_Init, RS232_UnInit },
		{ "midi",    &ConfigureParams.Midi.bEnableMidi, Midi_Init, Midi_UnInit },
		{ NULL, NULL, NULL, NULL }
	};
	int i;
	bool value;
	for (i = 0; item[i].name; i++)
	{
		if (strcmp(name, item[i].name) == 0)
		{
			switch (action) {
			case DO_TOGGLE:
				value = !*(item[i].pvalue);
				break;
			case DO_ENABLE:
				value = TRUE;
				break;
			case DO_DISABLE:
			default:
				value = FALSE;
				break;
			}
			*(item[i].pvalue) = value;
			if (value) {
				item[i].init();
			} else {
				item[i].uninit();
			}
			fprintf(stderr, "%s: %s\n", name, value?"ON":"OFF");
			return TRUE;
		}
	}
	fprintf(stderr, "WARNING: unknown device '%s'\n\n", name);
	fprintf(stderr, "Accepted devices are:\n");
	for (i = 0; item[i].name; i++)
	{
		fprintf(stderr, "- %s\n", item[i].name);
	}
	return FALSE;
}

/*-----------------------------------------------------------------------*/
/**
 * Parse path type name and set the path to given value.
 * Return FALSE if name parsing failed, TRUE otherwise
 */
static bool Control_SetPath(char *name)
{
	struct {
		const char *name;
		char *path;
	} item[] = {
		{ "memauto",  ConfigureParams.Memory.szAutoSaveFileName },
		{ "memsave",  ConfigureParams.Memory.szMemoryCaptureFileName },
		{ "midiin",   ConfigureParams.Midi.sMidiInFileName },
		{ "midiout",  ConfigureParams.Midi.sMidiOutFileName },
		{ "printout", ConfigureParams.Printer.szPrintToFileName },
		{ "soundout", ConfigureParams.Sound.szYMCaptureFileName },
		{ "rs232in",  ConfigureParams.RS232.szInFileName },
		{ "rs232out", ConfigureParams.RS232.szOutFileName },
		{ NULL, NULL }
	};
	int i;
	char *arg;
	const char *value;
	
	/* argument? */
	arg = strchr(name, ' ');
	if (arg) {
		*arg = '\0';
		value = Str_Trim(arg+1);
	} else {
		return FALSE;
	}
	
	for (i = 0; item[i].name; i++)
	{
		if (strcmp(name, item[i].name) == 0)
		{
			fprintf(stderr, "%s: %s -> %s\n", name, item[i].path, value);
			strncpy(item[i].path, value, FILENAME_MAX-1);
			return TRUE;
		}
	}
	fprintf(stderr, "WARNING: unknown path type '%s'\n\n", name);
	fprintf(stderr, "Accepted paths types are:\n");
	for (i = 0; item[i].name; i++)
	{
		fprintf(stderr, "- %s\n", item[i].name);
	}
	return FALSE;
}

/*-----------------------------------------------------------------------*/
/**
 * Show Hatari remote usage info and return FALSE
 */
static bool Control_Usage(const char *cmd)
{
	fprintf(stderr, "ERROR: unrecognized hatari command: '%s'", cmd);
	fprintf(stderr,
		"Supported commands are:\n"
		"- hatari-debug <Debug UI command>\n"
		"- hatari-event <event to simulate>\n"
		"- hatari-option <command line options>\n"
		"- hatari-enable/disable/toggle <device name>\n"
		"- hatari-path <config name> <new path>\n"
		"- hatari-shortcut <shortcut name>\n"
		"- hatari-embed-info\n"
		"- hatari-stop\n"
		"- hatari-cont\n"
		"The last two can be used to stop and continue the Hatari emulation.\n"
		"All commands need to be separated by newlines.\n"
		);
	return FALSE;
}

/*-----------------------------------------------------------------------*/
/**
 * Parse Hatari debug/event/option/toggle/path/shortcut command buffer.
 * Given buffer is modified in-place.
 */
static void Control_ProcessBuffer(char *buffer)
{
	char *cmd, *cmdend, *arg;
	int ok = TRUE;
	
	cmd = buffer;
	do {
		/* command terminator? */
		cmdend  = strchr(cmd, '\n');
		if (cmdend) {
			*cmdend = '\0';
		}
		/* arguments? */
		arg = strchr(cmd, ' ');
		if (arg) {
			*arg = '\0';
			arg = Str_Trim(arg+1);
		}
		if (arg) {
			if (strcmp(cmd, "hatari-option") == 0) {
				ok = Change_ApplyCommandline(arg);
			} else if (strcmp(cmd, "hatari-debug") == 0) {
				ok = DebugUI_ParseCommand(arg);
			} else if (strcmp(cmd, "hatari-shortcut") == 0) {
				ok = Shortcut_Invoke(arg);
			} else if (strcmp(cmd, "hatari-event") == 0) {
				ok = Control_InsertEvent(arg);
			} else if (strcmp(cmd, "hatari-path") == 0) {
				ok = Control_SetPath(arg);
			} else if (strcmp(cmd, "hatari-enable") == 0) {
				ok = Control_DeviceAction(arg, DO_ENABLE);
			} else if (strcmp(cmd, "hatari-disable") == 0) {
				ok = Control_DeviceAction(arg, DO_DISABLE);
			} else if (strcmp(cmd, "hatari-toggle") == 0) {
				ok = Control_DeviceAction(arg, DO_TOGGLE);
			} else {
				ok = Control_Usage(cmd);
			}
		} else {
			if (strcmp(cmd, "hatari-embed-info") == 0) {
				fprintf(stderr, "Embedded window ID change messages = ON\n");
				bSendEmbedInfo = TRUE;
			} else if (strcmp(cmd, "hatari-stop") == 0) {
				Main_PauseEmulation(TRUE);
				bRemotePaused = TRUE;
			} else if (strcmp(cmd, "hatari-cont") == 0) {
				Main_UnPauseEmulation();
				bRemotePaused = FALSE;
			} else {
				ok = Control_Usage(cmd);
			}
		}
		if (cmdend) {
			cmd = cmdend + 1;
		}
	} while (ok && cmdend && *cmd);
}


/*-----------------------------------------------------------------------*/
/**
 * Check ControlSocket for new commands and execute them.
 * Commands should be separated by newlines.
 * 
 * Return TRUE if remote pause ON (and connected), FALSE otherwise
 */
bool Control_CheckUpdates(void)
{
	/* just using all trace options with +/- are about 300 chars */
	char buffer[400];
	struct timeval tv;
	fd_set readfds;
	ssize_t bytes;
	int status, sock;

	/* socket of file? */
	if (ControlSocket) {
		sock = ControlSocket;
	} else {
		return FALSE;
	}
	
	/* ready for reading? */
	tv.tv_usec = tv.tv_sec = 0;
	do {
		FD_ZERO(&readfds);
		FD_SET(sock, &readfds);
		if (bRemotePaused) {
			/* return only when there're UI events
			 * (redraws etc) to save battery:
			 *   http://bugzilla.libsdl.org/show_bug.cgi?id=323
			 */
			int uisock = Control_GetUISocket();
			if (uisock) {
				FD_SET(uisock, &readfds);
				if (uisock < sock) {
					uisock = sock;
				}
			}
			status = select(uisock+1, &readfds, NULL, NULL, NULL);
		} else {
			status = select(sock+1, &readfds, NULL, NULL, &tv);
		}
		if (status < 0) {
			perror("Control socket select() error");
			return FALSE;
		}
		/* nothing to process here */
		if (status == 0) {
			return bRemotePaused;
		}
		if (!FD_ISSET(sock, &readfds)) {
			return bRemotePaused;
		}
		
		/* assume whole command can be read in one go */
		bytes = read(sock, buffer, sizeof(buffer)-1);
		if (bytes < 0)
		{
			perror("Control socket read");
			return FALSE;
		}
		if (bytes == 0) {
			/* closed */
			close(ControlSocket);
			ControlSocket = 0;
			return FALSE;
		}
		buffer[bytes] = '\0';
		Control_ProcessBuffer(buffer);

	} while (bRemotePaused);
	
	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * Open given control socket.
 * Return NULL for success, otherwise an error string
 */
const char *Control_SetSocket(const char *socketpath)
{
	struct sockaddr_un address;
	int newsock;
	
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
/* In addition to X11 headers, SDL needs to support X11 */
#include <SDL_config.h>
#if SDL_VIDEO_DRIVER_X11
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

		/* hide WM window for Hatari */
		XUnmapWindow(display, wm_win);
		/* reparent main Hatari window to given parent */
		XReparentWindow(display, sdl_win, parent_win, 0, 0);

		/* whether to send new window size */
		if (bSendEmbedInfo && ControlSocket) {
			fprintf(stderr, "New %dx%d SDL window with ID: %lx\n",
				width, height, sdl_win);
			sprintf(buffer, "%dx%d", width, height);
			write(ControlSocket, buffer, strlen(buffer));
		}
	}
	info.info.x11.unlock_func();
}

/**
 * Return the X connection socket or zero
 */
static int Control_GetUISocket(void)
{
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (!SDL_GetWMInfo(&info)) {
		Log_Printf(LOG_WARN, "Failed to get SDL_GetWMInfo()\n");
		return 0;
	}
	return ConnectionNumber(info.info.x11.display);
}

#endif	/* SDL_VIDEO_DRIVER_X11 */
#else	/* HAVE_X11 */

static int Control_GetUISocket(void)
{
	return 0;
}
void Control_ReparentWindow(int width, int height, bool noembed)
{
	/* TODO: implement the Windows part.  SDL sources offer example */
	Log_Printf(LOG_TODO, "Support for Hatari window reparenting not built in\n");
}

#endif /* HAVE_X11 */

#endif /* HAVE_UNIX_DOMAIN_SOCKETS */
