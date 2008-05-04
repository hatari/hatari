/*
  Hatari - change.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This code handles run-time configuration changes. We keep all our
  configuration details in a structure called 'ConfigureParams'.  Before
  doing he changes, a backup copy is done of this structure. When
  the changes are done, these are compared to see whether emulator
   needs to be rebooted
*/
const char change_rcsid[] = "Hatari $Id: change.c,v 1.5 2008-05-04 20:03:34 thothy Exp $";

#include "config.h"

#if HAVE_UNIX_DOMAIN_SOCKETS
#include <sys/socket.h>
#include <sys/un.h>
#endif

#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <ctype.h>

#include "main.h"
#include "configuration.h"
#include "audio.h"
#include "change.h"
#include "dialog.h"
#include "file.h"
#include "floppy.h"
#include "gemdos.h"
#include "hdc.h"
#include "ikbd.h"
#include "ioMem.h"
#include "joy.h"
#include "keymap.h"
#include "log.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "options.h"
#include "printer.h"
#include "reset.h"
#include "rs232.h"
#include "screen.h"
#include "screenSnapShot.h"
#include "shortcut.h"
#include "sound.h"
#include "str.h"
#include "tos.h"
#include "vdi.h"
#include "video.h"
#include "sdlgui.h"
#include "hatari-glue.h"
#if ENABLE_DSP_EMU
# include "falcon/dsp.h"
#endif


/* socket from which control command line options are read */
#if HAVE_UNIX_DOMAIN_SOCKETS
static int ControlSocket;
static FILE *ControlFile;
#endif


/*-----------------------------------------------------------------------*/
/**
 * Check if need to warn user that changes will take place after reset.
 * Return TRUE if wants to reset.
 */
BOOL Change_DoNeedReset(CNF_PARAMS *changed)
{
	/* Did we change monitor type? If so, must reset */
	if (ConfigureParams.Screen.nMonitorType != changed->Screen.nMonitorType
	    && (changed->System.nMachineType == MACHINE_FALCON
	        || ConfigureParams.Screen.nMonitorType == MONITOR_TYPE_MONO
	        || changed->Screen.nMonitorType == MONITOR_TYPE_MONO))
		return TRUE;

	/* Did change to GEM VDI display? */
	if (ConfigureParams.Screen.bUseExtVdiResolutions != changed->Screen.bUseExtVdiResolutions)
		return TRUE;

	/* Did change GEM resolution or color depth? */
	if (changed->Screen.bUseExtVdiResolutions &&
	    (ConfigureParams.Screen.nVdiWidth != changed->Screen.nVdiWidth
	     || ConfigureParams.Screen.nVdiHeight != changed->Screen.nVdiHeight
	     || ConfigureParams.Screen.nVdiColors != changed->Screen.nVdiColors))
		return TRUE;

	/* Did change TOS ROM image? */
	if (strcmp(changed->Rom.szTosImageFileName, ConfigureParams.Rom.szTosImageFileName))
		return TRUE;

	/* Did change HD image? */
	if (changed->HardDisk.bUseHardDiskImage != ConfigureParams.HardDisk.bUseHardDiskImage
	    || (strcmp(changed->HardDisk.szHardDiskImage, ConfigureParams.HardDisk.szHardDiskImage)
	        && changed->HardDisk.bUseHardDiskImage))
		return TRUE;

	/* Did change GEMDOS drive? */
	if (changed->HardDisk.bUseHardDiskDirectories != ConfigureParams.HardDisk.bUseHardDiskDirectories
	    || (strcmp(changed->HardDisk.szHardDiskDirectories[0], ConfigureParams.HardDisk.szHardDiskDirectories[0])
	        && changed->HardDisk.bUseHardDiskDirectories))
		return TRUE;

	/* Did change machine type? */
	if (changed->System.nMachineType != ConfigureParams.System.nMachineType)
		return TRUE;

	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * Copy details back to configuration and perform reset.
 */
void Change_CopyChangedParamsToConfiguration(CNF_PARAMS *changed, BOOL bForceReset)
{
	BOOL NeedReset;
	BOOL bReInitGemdosDrive = FALSE, bReInitAcsiEmu = FALSE;
	BOOL bReInitIoMem = FALSE;

	/* Do we need to warn user of that changes will only take effect after reset? */
	if (bForceReset)
		NeedReset = bForceReset;
	else
		NeedReset = Change_DoNeedReset(changed);

	/* Do need to change resolution? Need if change display/overscan settings */
	/*(if switch between Colour/Mono cause reset later) */
	if (!NeedReset &&
	    (changed->Screen.nForceBpp != ConfigureParams.Screen.nForceBpp
	     || changed->Screen.bZoomLowRes != ConfigureParams.Screen.bZoomLowRes
	     || changed->Screen.bAllowOverscan != ConfigureParams.Screen.bAllowOverscan))
	{
		ConfigureParams.Screen.nForceBpp = changed->Screen.nForceBpp;
		ConfigureParams.Screen.bZoomLowRes = changed->Screen.bZoomLowRes;
		ConfigureParams.Screen.bAllowOverscan = changed->Screen.bAllowOverscan;

		Screen_ModeChanged();
	}

	/* Did set new printer parameters? */
	if (changed->Printer.bEnablePrinting != ConfigureParams.Printer.bEnablePrinting
	    || changed->Printer.bPrintToFile != ConfigureParams.Printer.bPrintToFile
	    || strcmp(changed->Printer.szPrintToFileName,ConfigureParams.Printer.szPrintToFileName))
	{
		Printer_CloseAllConnections();
	}

	/* Did set new RS232 parameters? */
	if (changed->RS232.bEnableRS232 != ConfigureParams.RS232.bEnableRS232
	    || strcmp(changed->RS232.szOutFileName, ConfigureParams.RS232.szOutFileName)
	    || strcmp(changed->RS232.szInFileName, ConfigureParams.RS232.szInFileName))
	{
		RS232_UnInit();
	}

	/* Did stop sound? Or change playback Hz. If so, also stop sound recording */
	if (!changed->Sound.bEnableSound || changed->Sound.nPlaybackQuality != ConfigureParams.Sound.nPlaybackQuality)
	{
		if (Sound_AreWeRecording())
			Sound_EndRecording();
		Audio_UnInit();
	}

	/* Did change GEMDOS drive? */
	if (changed->HardDisk.bUseHardDiskDirectories != ConfigureParams.HardDisk.bUseHardDiskDirectories
	    || (strcmp(changed->HardDisk.szHardDiskDirectories[0], ConfigureParams.HardDisk.szHardDiskDirectories[0])
	        && changed->HardDisk.bUseHardDiskDirectories))
	{
		GemDOS_UnInitDrives();
		bReInitGemdosDrive = TRUE;
	}

	/* Did change HD image? */
	if (changed->HardDisk.bUseHardDiskImage != ConfigureParams.HardDisk.bUseHardDiskImage
	    || (strcmp(changed->HardDisk.szHardDiskImage, ConfigureParams.HardDisk.szHardDiskImage)
	        && changed->HardDisk.bUseHardDiskImage))
	{
		HDC_UnInit();
		bReInitAcsiEmu = TRUE;
	}

	/* Did change blitter, rtc or system type? */
	if (changed->System.bBlitter != ConfigureParams.System.bBlitter
#if ENABLE_DSP_EMU
	    || changed->System.nDSPType != ConfigureParams.System.nDSPType
#endif
	    || changed->System.bRealTimeClock != ConfigureParams.System.bRealTimeClock
	    || changed->System.nMachineType != ConfigureParams.System.nMachineType)
	{
		IoMem_UnInit();
		bReInitIoMem = TRUE;
	}
	
#if ENABLE_DSP_EMU
	/* Disabled DSP? */
	if (changed->System.nDSPType == DSP_TYPE_EMU &&
	    (changed->System.nDSPType != ConfigureParams.System.nDSPType))
	{
		DSP_UnInit();
	}
#endif

	/* Copy details to configuration, so can be saved out or set on reset */
	ConfigureParams = *changed;

	/* Copy details to global, if we reset copy them all */
	Configuration_Apply(NeedReset);

#if ENABLE_DSP_EMU
	if (ConfigureParams.System.nDSPType == DSP_TYPE_EMU)
	{
		DSP_Init();
	}
#endif

	/* Set keyboard remap file */
	if (ConfigureParams.Keyboard.nKeymapType == KEYMAP_LOADED)
		Keymap_LoadRemapFile(ConfigureParams.Keyboard.szMappingFileName);

	/* Mount a new HD image: */
	if (bReInitAcsiEmu && ConfigureParams.HardDisk.bUseHardDiskImage)
	{
		HDC_Init(ConfigureParams.HardDisk.szHardDiskImage);
	}

	/* Mount a new GEMDOS drive? */
	if (bReInitGemdosDrive && ConfigureParams.HardDisk.bUseHardDiskDirectories)
	{
		GemDOS_InitDrives();
	}

	/* Restart audio sub system if necessary: */
	if (ConfigureParams.Sound.bEnableSound && !bSoundWorking)
	{
		Audio_Init();
	}

	/* Re-initialize the RS232 emulation: */
	if (ConfigureParams.RS232.bEnableRS232 && !bConnectedRS232)
	{
		RS232_Init();
	}

	/* Re-init IO memory map? */
	if (bReInitIoMem)
	{
		IoMem_Init();
	}

	/* Do we need to perform reset? */
	if (NeedReset)
	{
		Reset_Cold();
	}

	/* Go into/return from full screen if flagged */
	if (!bInFullScreen && changed->Screen.bFullScreen)
		Screen_EnterFullScreen();
	else if (bInFullScreen && !changed->Screen.bFullScreen)
		Screen_ReturnFromFullScreen();
}


/*-----------------------------------------------------------------------*/
/**
 * Change given Hatari options
 * Return FALSE if parsing failed, TRUE otherwise
 */
static BOOL Change_Options(int argc, char *argv[])
{
	BOOL bOK;
	CNF_PARAMS original, changed;

	Main_PauseEmulation();

	/* get configuration changes */
	original = ConfigureParams;
	ConfigureParams.Screen.bFullScreen = bInFullScreen;
	bOK = Opt_ParseParameters(argc, argv, NULL, 0);
	changed = ConfigureParams;
	ConfigureParams = original;

	/* Check if reset is required and ask user if he really wants to continue */
	if (bOK && Change_DoNeedReset(&changed) &&
	    ConfigureParams.Log.nAlertDlgLogLevel >= LOG_INFO) {
		bOK = DlgAlert_Query("The emulated system must be "
				     "reset to apply these changes. "
				     "Apply changes now and reset "
				     "the emulator?");
	}
	/* Copy details to configuration */
	if (bOK) {
		Change_CopyChangedParamsToConfiguration(&changed, FALSE);
	}

	Main_UnPauseEmulation();
	return bOK;
}


/*-----------------------------------------------------------------------*/
/**
 * Parse given command line and change Hatari options accordingly
 * Return FALSE if parsing failed or there were no args, TRUE otherwise
 */
static BOOL Change_ApplyCommandline(char *cmdline)
{
	int i, argc, inarg;
	char **argv;
	BOOL ret;

	/* count args */
	inarg = argc = 0;
	for (i = 0; cmdline[i]; i++)
	{
		if (isspace(cmdline[i]))
		{
			inarg = 0;
			continue;
		}
		if (!inarg)
		{
			inarg++;
			argc++;
		}
	}
	if (!argc)
	{
		return FALSE;
	}
	/* 2 = "hatari" + NULL */
	argv = malloc((argc+2) * sizeof(char*));
	if (!argv)
	{
		perror("command line alloc");
		return FALSE;
	}

	/* parse them to array */
	fprintf(stderr, "Command line with '%d' arguments:\n", argc);
	inarg = argc = 0;
	argv[argc++] = "hatari";
	for (i = 0; cmdline[i]; i++)
	{
		if (isspace(cmdline[i]))
		{
			cmdline[i] = '\0';
			if (inarg)
			{
				fprintf(stderr, "- '%s'\n", argv[argc-1]);
			}
			inarg = 0;
			continue;
		}
		if (!inarg)
		{
			argv[argc++] = &(cmdline[i]);
			inarg++;
		}
	}
	if (inarg)
	{
		fprintf(stderr, "- '%s'\n", argv[argc-1]);
	}
	argv[argc] = NULL;
	
	/* do args */
	ret = Change_Options(argc, argv);
	free(argv);
	return ret;
}


/*-----------------------------------------------------------------------*/
/**
 * Parse key string and synthetize corresponding key press/release
 * Return FALSE if parsing failed, TRUE otherwise
 * 
 * This can be used by external Hatari UI(s) on devices which lack keyboard
 */
static BOOL Change_InsertKey(const char *event)
{
	char *endptr;
	long int value;
	int offset, press;

	if (strncmp(event, "keypress ", 9) == 0) {
		press = TRUE;
		offset = 9;
	} else if (strncmp(event, "keyrelease ", 11) == 0) {
		press = FALSE;
		offset = 11;
	} else {
		fprintf(stderr, "ERROR: event '%s' no key press/release\n", event);
		return FALSE;
	}
	value = strtol(event+offset, &endptr, 0);
	/* not a valid number or value is out of range */
	if (!*(event+offset) || *endptr || value < 0 || value > 255) {
		fprintf(stderr, "ERROR: value '%s' not valid key code, got %ld\n",
			event+offset, value);
		return FALSE;
	}
	/* Simulate press/release of a key with given ST keycode */
	IKBD_PressSTKey(value, press);
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
static BOOL Change_InsertEvent(const char *event)
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
	if (Change_InsertKey(event)) {
		return TRUE;
	}
	fprintf(stderr, "ERROR: unrecognized event: '%s'\n", event);
	fprintf(stderr, "Supported events are:\n");
	fprintf(stderr, "- doubleclick\n");
	fprintf(stderr, "- rightpress\n");
	fprintf(stderr, "- rightrelease\n");
	fprintf(stderr, "- keypress <key code>\n");
	fprintf(stderr, "- keyrelease <keycode>\n");
	return FALSE;	
}


/*-----------------------------------------------------------------------*/
/**
 * Parse Hatari option/shortcut/event command buffer.
 * Given buffer is modified in-place.
 * Returns FALSE on error, otherwise TRUE.
 */
BOOL Change_ProcessBuffer(char *buffer)
{
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
		if (strncmp(cmd, "hatari-option ", 14) == 0) {
			ok &= Change_ApplyCommandline(cmd+14);
		} else if (strncmp(cmd, "hatari-shortcut ", 16) == 0) {
			ok &= Shortcut_Invoke(Str_Trim(cmd+16));
		} else if (strncmp(cmd, "hatari-event ", 13) == 0) {
			ok &= Change_InsertEvent(Str_Trim(cmd+13));
		} else {
			fprintf(stderr, "ERROR: unrecognized input\n\t'%s'\n", Str_Trim(cmd));
			ok = FALSE;
		}
		if (cmdend) {
			cmd = cmdend + 1;
		}
	} while (cmdend && *cmd);
	return ok;
}


/*-----------------------------------------------------------------------*/
/**
 * Check ControlSocket for new commands and execute them.
 * Commands should be separated by newlines.
 * Return TRUE if everthing is OK, FALSE on error.
 */
extern BOOL Change_CheckUpdates(void)
{
#if !HAVE_UNIX_DOMAIN_SOCKETS    /* supports select only for sockets */
	return TRUE;
#else
	/* just using all trace options with +/- are about 300 chars */
	char buffer[400];
	struct timeval tv;
	fd_set readfds;
	ssize_t bytes;
	int status, sock;

	/* socket of file? */
	if (ControlSocket) {
		sock = ControlSocket;
	} else if (ControlFile) {
		sock = fileno(ControlFile);
	} else {
		return TRUE;
	}
	
	/* ready for reading? */
	FD_ZERO(&readfds);
	FD_SET(sock, &readfds);
	tv.tv_usec = tv.tv_sec = 0;
	status = select(sock+1, &readfds, NULL, NULL, &tv);
	if (status < 0) {
		perror("Control socket select() error");
		return FALSE;
	}
	if (status == 0) {
		return TRUE;
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
		if (ControlSocket) {
			close(ControlSocket);
			ControlSocket = 0;
		} else {
			ControlFile = NULL;
		}
		return TRUE;
	}
	buffer[bytes] = '\0';
	return Change_ProcessBuffer(buffer);
#endif
}


/*-----------------------------------------------------------------------*/
/**
 * Open given control socket.  "stdin" is opened as file.
 * Return NULL for success, otherwise an error string
 */
const char *Change_SetControlSocket(const char *socketpath)
{
#if !HAVE_UNIX_DOMAIN_SOCKETS
	return "Control socket is not supported on this platform.";
#else
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
#endif
}
