/*
  Hatari - control.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This code processes commands from the Hatari control socket
*/
const char Control_fileid[] = "Hatari control.c";

#include "config.h"

#if HAVE_UNIX_DOMAIN_SOCKETS
# include <sys/socket.h>
# include <sys/stat.h>	/* mkfifo() */
# include <sys/un.h>
# include <fcntl.h>
#endif

#include <sys/types.h>
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <unistd.h>
#include <ctype.h>
#include <assert.h>

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
#include "scc.h"
#include "shortcut.h"
#include "str.h"
#include "screen.h"

typedef enum {
	DO_DISABLE,
	DO_ENABLE,
	DO_TOGGLE
} action_t;

/* Whether to send embedded window info */
static bool bSendEmbedInfo;
/* Pausing triggered remotely (battery save pause) */
static bool bRemotePaused;


/*-----------------------------------------------------------------------*/
/**
 * Parse key command and synthesize key press/release
 * corresponding to given keycode or character.
 * Return false if parsing failed, true otherwise
 * 
 * This can be used by external Hatari UI(s) for
 * string macros, or on devices which lack keyboard
 */
static bool Control_InsertKey(const char *event)
{
	const char *key = NULL;
	bool up, down;

	if (strncmp(event, "keypress ", 9) == 0) {
		key = &event[9];
		down = up = true;
	} else if (strncmp(event, "keydown ", 8) == 0) {
		key = &event[8];
		down = true;
		up = false;
	} else if (strncmp(event, "keyup ", 6) == 0) {
		key = &event[6];
		down = false;
		up = true;
	}
	if (!(key && key[0])) {
		fprintf(stderr, "ERROR: '%s' contains no key press/down/up event\n", event);
		return false;
	}
	if (key[1]) {
		char *endptr;
		/* multiple characters, assume it's a keycode */
		int keycode = strtol(key, &endptr, 0);
		/* not a valid number or keycode is out of range? */
		if (*endptr || keycode < 0 || keycode > 255) {
			fprintf(stderr, "ERROR: '%s' isn't a valid key scancode, got value %d\n",
				key, keycode);
			return false;
		}
		if (down) {
			IKBD_PressSTKey(keycode, true);
		}
		if (up) {
			IKBD_PressSTKey(keycode, false);
		}
	} else {
		if (!isalnum((unsigned char)key[0])) {
			fprintf(stderr, "ERROR: non-alphanumeric character '%c' needs to be given as keycode\n", key[0]);
			return false;
		}
		if (down) {
			Keymap_SimulateCharacter(key[0], true);
		}
		if (up) {
			Keymap_SimulateCharacter(key[0], false);
		}
	}
#if 0
	fprintf(stderr, "Simulated key %s of %d\n",
		(down? (up? "press":"down") :"up"), key);
#endif
	return true;
}

/*-----------------------------------------------------------------------*/
/**
 * Parse event name and synthesize corresponding event to emulation
 * Return false if name parsing failed, true otherwise
 * 
 * This can be used by external Hatari UI(s) on devices which input
 * methods differ from normal keyboard and mouse, such as high DPI
 * touchscreen (no right/middle button, inaccurate clicks)
 */
static bool Control_InsertEvent(const char *event)
{
	if (strcmp(event, "doubleclick") == 0) {
		Keyboard.LButtonDblClk = 1;
		return true;
	}
	if (strcmp(event, "rightdown") == 0) {
		Keyboard.bRButtonDown |= BUTTON_MOUSE;
		return true;
	}
	if (strcmp(event, "rightup") == 0) {
		Keyboard.bRButtonDown &= ~BUTTON_MOUSE;
		return true;
	}
	if (Control_InsertKey(event)) {
		return true;
	}
	fprintf(stderr, "ERROR: unrecognized event: '%s'\n", event);
	fprintf(stderr,
		"Supported mouse button and key events are:\n"
		"- doubleclick\n"
		"- rightdown\n"
		"- rightup\n"
		"- keypress <key>\n"
		"- keydown <key>\n"
		"- keyup <key>\n"
		"<key> can be either a single ASCII character or an ST scancode\n"
		"(e.g. space has scancode of 57 and enter 28).\n"
		);
	return false;	
}

/*-----------------------------------------------------------------------*/
/**
 * Parse device name and enable/disable/toggle & init/uninit it according
 * to action.  Return false if name parsing failed, true otherwise
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
		void(*reset)(void);
	} item[] = {
		{ "printer", &ConfigureParams.Printer.bEnablePrinting, Printer_Init, Printer_UnInit, NULL },
		{ "rs232",   &ConfigureParams.RS232.bEnableRS232, RS232_Init, RS232_UnInit, NULL },
		{ "scca",    &ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_A_SERIAL], SCC_Init, SCC_UnInit, NULL },
		{ "sccalan", &ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_A_LAN], SCC_Init, SCC_UnInit, NULL },
		{ "sccb",    &ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_B], SCC_Init, SCC_UnInit, NULL },
		{ "midi",    &ConfigureParams.Midi.bEnableMidi, Midi_Init, Midi_UnInit, Midi_Reset },
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
				value = true;
				break;
			case DO_DISABLE:
			default:
				value = false;
				break;
			}
			*(item[i].pvalue) = value;
			if (value) {
				item[i].init();
				if (item[i].reset)
					item[i].reset();
			} else {
				item[i].uninit();
			}
			fprintf(stderr, "%s: %s\n", name, value?"ON":"OFF");
			return true;
		}
	}
	fprintf(stderr, "WARNING: unknown device '%s'\n\n", name);
	fprintf(stderr, "Accepted devices are:\n");
	for (i = 0; item[i].name; i++)
	{
		fprintf(stderr, "- %s\n", item[i].name);
	}
	return false;
}

/*-----------------------------------------------------------------------*/
/**
 * Parse path type name and set the path to given value.
 * Return false if name parsing failed, true otherwise
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
		{ "sccain",   ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_A_SERIAL] },
		{ "sccaout",  ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_A_SERIAL] },
		{ "sccalanin",ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_A_LAN] },
		{ "sccalanout",ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_A_LAN] },
		{ "sccbin",   ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_B] },
		{ "sccbout",  ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_B] },
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
		return false;
	}
	
	for (i = 0; item[i].name; i++)
	{
		if (strcmp(name, item[i].name) == 0)
		{
			fprintf(stderr, "%s: %s -> %s\n", name, item[i].path, value);
			strncpy(item[i].path, value, FILENAME_MAX-1);
			return true;
		}
	}
	fprintf(stderr, "WARNING: unknown path type '%s'\n\n", name);
	fprintf(stderr, "Accepted paths types are:\n");
	for (i = 0; item[i].name; i++)
	{
		fprintf(stderr, "- %s\n", item[i].name);
	}
	return false;
}

/*-----------------------------------------------------------------------*/
/**
 * Show Hatari remote usage info and return false
 */
static bool Control_Usage(const char *cmd)
{
	fprintf(stderr, "ERROR: unrecognized hatari command: '%s'!\n", cmd);
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
		"All commands need to be separated by newlines.  Spaces in command\n"
		"line option arguments need to be quoted with \\.\n"
		);
	return false;
}

/*-----------------------------------------------------------------------*/
/**
 * Parse Hatari debug/event/option/toggle/path/shortcut command buffer.
 */
void Control_ProcessBuffer(const char *orig)
{
	char *cmd, *cmdend, *arg, *buffer;
	int ok = true;

	/* this is called from several different places,
	 * so take a copy of the original buffer so
	 * that it can be sliced & diced
	 */
	buffer = strdup(orig);
	assert(buffer);

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
				ok = DebugUI_ParseLine(arg);
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
				bSendEmbedInfo = true;
			} else if (strcmp(cmd, "hatari-stop") == 0) {
				Main_PauseEmulation(true);
				bRemotePaused = true;
			} else if (strcmp(cmd, "hatari-cont") == 0) {
				Main_UnPauseEmulation();
				bRemotePaused = false;
			} else {
				ok = Control_Usage(cmd);
			}
		}
		if (cmdend) {
			cmd = cmdend + 1;
		}
	} while (ok && cmdend && *cmd);
	free(buffer);
}


#if HAVE_UNIX_DOMAIN_SOCKETS

/* one-way fifo which Hatari creates and reads commands from */
static char *FifoPath;
static int ControlFifo;

/* two-way socket to which Hatari connects, reads control commands
 * from, and where the command responses (if any) are written to
 */
static int ControlSocket;

/* pre-declared local functions */
static int Control_GetUISocket(void);


/*-----------------------------------------------------------------------*/
/**
 * Check ControlSocket for new commands and execute them.
 * Commands should be separated by newlines.
 * 
 * Return true if remote pause ON (and connected), false otherwise
 */
bool Control_CheckUpdates(void)
{
	/* setting all trace options, or paths takes a lot of space */
	char buffer[4096];
	struct timeval tv;
	fd_set readfds;
	ssize_t bytes;
	int status, sock;

	if (ControlFifo) {
		/* assume whole command can be read in one go */
		bytes = read(ControlFifo, buffer, sizeof(buffer)-1);
		if (bytes < 0) {
			perror("command FIFO read error");
			return false;
		}
		if (bytes == 0) {
			/* non-blocking read, nothing to read */
			return false;
		}
		buffer[bytes] = '\0';
		Control_ProcessBuffer(buffer);
		return false;
	}

	/* socket of file? */
	if (ControlSocket) {
		sock = ControlSocket;
	} else {
		return false;
	}
	
	/* ready for reading? */
	tv.tv_usec = tv.tv_sec = 0;
	do {
		FD_ZERO(&readfds);
		FD_SET(sock, &readfds);
		if (bRemotePaused) {
			/* return only when there're UI events
			 * (redraws etc) to save battery:
			 * https://github.com/libsdl-org/SDL-1.2/issues/222
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
			return false;
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
		if (bytes < 0) {
			perror("Control socket read error");
			return false;
		}
		if (bytes == 0) {
			/* closed */
			fprintf(stderr, "ready control socket with 0 bytes available -> close socket\n");
			close(ControlSocket);
			ControlSocket = 0;
			return false;
		}
		buffer[bytes] = '\0';
		Control_ProcessBuffer(buffer);

	} while (bRemotePaused);
	
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Close and remove FIFO file
 */
void Control_RemoveFifo(void)
{
	if (ControlFifo) {
		close(ControlFifo);
		ControlFifo = 0;
	}
	if (FifoPath) {
		Log_Printf(LOG_DEBUG, "removing command FIFO: %s\n", FifoPath);
		if (remove(FifoPath) < 0)
		{
			perror("Remove FIFO failed");
		}
		free(FifoPath);
		FifoPath = NULL;
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Open given command FIFO
 * Return NULL for success, otherwise an error string
 */
const char *Control_SetFifo(const char *path)
{
	int fifo;

	if (ControlSocket) {
		return "Can't use a FIFO at the same time with a control socket";
	}

	Control_RemoveFifo();
	Log_Printf(LOG_DEBUG, "creating command FIFO: %s\n", path);

	if (mkfifo(path, S_IRUSR | S_IWUSR)) {
		perror("FIFO creation error");
		return "Can't create FIFO file";
	}
	FifoPath = strdup(path);

	fifo = open(path, O_RDONLY | O_NONBLOCK);
	if (fifo < 0) {
		perror("FIFO open error");
		Control_RemoveFifo();
		return "opening non-blocking read-only FIFO failed";
	}
	ControlFifo = fifo;
	return NULL;
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

	if (ControlFifo) {
		return "Can't use a FIFO at the same time with a control socket";
	}
	
	newsock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (newsock < 0) {
		perror("socket creation error");
		return "Can't create AF_UNIX socket";
	}

	address.sun_family = AF_UNIX;
	strncpy(address.sun_path, socketpath, sizeof(address.sun_path));
	address.sun_path[sizeof(address.sun_path)-1] = '\0';
	Log_Printf(LOG_INFO, "Connecting to control socket '%s'...\n", address.sun_path);
	if (connect(newsock, (struct sockaddr *)&address, sizeof(address)) < 0) {
		perror("socket connect error");
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

#include <SDL_config.h>

/* X11 available and SDL_config.h states that SDL supports X11 */
#if HAVE_X11 && SDL_VIDEO_DRIVER_X11
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
 * with noembed=true _before_ fullscreening and any other time with
 * noembed=false after changing window size.  You can do this by
 * giving bInFullscreen as the noembed value.
 */
void Control_ReparentWindow(int width, int height, bool noembed)
{
	Display *display;
	Window parent_win, sdl_win;
	const char *parent_win_id;
	SDL_SysWMinfo info;
	Window wm_win;
	Window dw1, *dw2;
	unsigned int nwin;

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
	if (!SDL_GetWindowWMInfo(sdlWindow, &info)) {
		Log_Printf(LOG_WARN, "Failed to get SDL_GetWMInfo()\n");
		return;
	}

	display = info.info.x11.display;
	sdl_win = info.info.x11.window;
	XQueryTree(display, sdl_win, &dw1, &wm_win, &dw2, &nwin);

	if (noembed)
	{
		/* show WM window again */
		XMapWindow(display, wm_win);
	}
	else
	{
		if (parent_win != wm_win) {
			/* hide WM window for Hatari */
			XUnmapWindow(display, wm_win);

			/* reparent main Hatari window to given parent */
			XReparentWindow(display, sdl_win, parent_win, 0, 0);
		}
		/* whether to send new window size */
		if (bSendEmbedInfo && ControlSocket)
		{
			char buffer[12]; /* 32-bits in hex (+ '\r') + '\n' + '\0' */

			Log_Printf(LOG_INFO, "New %dx%d SDL window with ID: %lx\n",
				width, height, sdl_win);
			sprintf(buffer, "%dx%d", width, height);
			if (write(ControlSocket, buffer, strlen(buffer)) < 0)
				perror("Control_ReparentWindow write error");
		}
	}

	XSync(display, false);
}

/**
 * Return the X connection socket or zero
 */
static int Control_GetUISocket(void)
{
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(sdlWindow, &info)) {
		Log_Printf(LOG_WARN, "Failed to get SDL_GetWMInfo()\n");
		return 0;
	}
	return ConnectionNumber(info.info.x11.display);
}

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
