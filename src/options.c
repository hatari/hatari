/*
  Hatari - options.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Functions for showing and parsing all of Hatari's command line options.
  
  To add a new option:
  - Add option ID to the enum
  - Add the option information to HatariOptions[]
  - Add required actions for that ID to switch in Opt_ParseParameters()
*/
const char Options_fileid[] = "Hatari options.c : " __DATE__ " " __TIME__;

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "main.h"
#include "options.h"
#include "configuration.h"
#include "control.h"
#include "file.h"
#include "floppy.h"
#include "screen.h"
#include "video.h"
#include "vdi.h"
#include "joy.h"
#include "log.h"

#include "hatari-glue.h"


bool bLoadAutoSave;        /* Load autosave memory snapshot at startup */
bool bLoadMemorySave;      /* Load memory snapshot provided via option at startup */
bool bBiosIntercept;       /* whether UAE should intercept Bios & XBios calls */


/*  List of supported options. */
enum {
	OPT_HEADER,	/* options section header */
	OPT_HELP,		/* general options */
	OPT_VERSION,
	OPT_CONFIRMQUIT,
	OPT_CONFIGFILE,
	OPT_FASTFORWARD,
	OPT_MONO,		/* display options */
	OPT_MONITOR,
	OPT_FULLSCREEN,
	OPT_WINDOW,
	OPT_GRAB,
	OPT_ZOOM,
	OPT_FRAMESKIPS,
	OPT_BORDERS,
	OPT_STATUSBAR,
	OPT_DRIVE_LED,
	OPT_SPEC512,
	OPT_FORCEBPP,
	OPT_VDI,		/* VDI options */
	OPT_VDI_PLANES,
	OPT_VDI_WIDTH,
	OPT_VDI_HEIGHT,
	OPT_JOYSTICK,		/* device options */
	OPT_JOYSTICK0,
	OPT_JOYSTICK1,
	OPT_JOYSTICK2,
	OPT_JOYSTICK3,
	OPT_JOYSTICK4,
	OPT_JOYSTICK5,
	OPT_PRINTER,
	OPT_MIDI_IN,
	OPT_MIDI_OUT,
	OPT_RS232_IN,
	OPT_RS232_OUT,
	OPT_DISKA,		/* disk options */
	OPT_DISKB,
	OPT_SLOWFLOPPY,
	OPT_HARDDRIVE,
	OPT_ACSIHDIMAGE,
	OPT_IDEHDIMAGE,
	OPT_MEMSIZE,		/* memory options */
	OPT_TOS,
	OPT_CARTRIDGE,
	OPT_MEMSTATE,
	OPT_CPULEVEL,		/* CPU options */
	OPT_CPUCLOCK,
	OPT_COMPATIBLE,
	OPT_MACHINE,		/* system options */
	OPT_BLITTER,
	OPT_DSP,
	OPT_SOUND,
	OPT_KEYMAPFILE,
	OPT_DEBUG,		/* debug options */
	OPT_BIOSINTERCEPT,
	OPT_TRACE,
	OPT_TRACEFILE,
	OPT_CONTROLSOCKET,
	OPT_LOGFILE,
	OPT_LOGLEVEL,
	OPT_ALERTLEVEL,
	OPT_RUNVBLS,
	OPT_ERROR,
	OPT_CONTINUE
};

typedef struct {
	unsigned int id;	/* option ID */
	const char *chr;	/* short option */
	const char *str;	/* long option */
	const char *arg;	/* type name for argument, if any */
	const char *desc;	/* option description */
} opt_t;

/* it's easier to edit these if they are kept in the same order as the enums */
static const opt_t HatariOptions[] = {
	
	{ OPT_HEADER, NULL, NULL, NULL, "General" },
	{ OPT_HELP,      "-h", "--help",
	  NULL, "Print this help text and exit" },
	{ OPT_VERSION,   "-v", "--version",
	  NULL, "Print version number and exit" },
	{ OPT_CONFIRMQUIT, NULL, "--confirm-quit",
	  "<bool>", "Whether Hatari confirms quit" },
	{ OPT_CONFIGFILE, "-c", "--configfile",
	  "<file>", "Use <file> instead of the default hatari config file" },
	{ OPT_FASTFORWARD, NULL, "--fast-forward",
	  "<bool>", "Help skipping stuff on fast machine" },

	{ OPT_HEADER, NULL, NULL, NULL, "Display" },
	{ OPT_MONO,      "-m", "--mono",
	  NULL, "Start in monochrome mode instead of color" },
	{ OPT_MONITOR,      NULL, "--monitor",
	  "<x>", "Select monitor type (x = mono/rgb/vga/tv)" },
	{ OPT_FULLSCREEN,"-f", "--fullscreen",
	  NULL, "Start emulator in fullscreen mode" },
	{ OPT_WINDOW,    "-w", "--window",
	  NULL, "Start emulator in window mode" },
	{ OPT_GRAB, NULL, "--grab",
	  NULL, "Grab mouse (also) in window mode" },
	{ OPT_ZOOM, "-z", "--zoom",
	  "<x>", "Double ST low resolution (1=no, 2=yes)" },
	{ OPT_FRAMESKIPS, NULL, "--frameskips",
	  "<x>", "Skip <x> frames after each shown frame (0=off, >4=auto/max)" },
	{ OPT_BORDERS, NULL, "--borders",
	  "<bool>", "Show screen borders (for overscan demos etc)" },
	{ OPT_STATUSBAR, NULL, "--statusbar",
	  "<bool>", "Show statusbar (floppy leds etc)" },
	{ OPT_DRIVE_LED,   NULL, "--drive-led",
	  "<bool>", "Show overlay drive led when statusbar isn't shown" },
	{ OPT_SPEC512, NULL, "--spec512",
	  "<x>", "Spec512 palette threshold (0 <= x <= 512, 0=disable)" },
	{ OPT_FORCEBPP, NULL, "--bpp",
	  "<x>", "Force internal bitdepth (x = 8/15/16/32, 0=disable)" },
	
	{ OPT_HEADER, NULL, NULL, NULL, "VDI" },
	{ OPT_VDI,	NULL, "--vdi",
	  "<bool>", "Whether to use VDI screen mode" },
	{ OPT_VDI_PLANES,NULL, "--vdi-planes",
	  "<x>", "VDI mode bit-depth (x = 1/2/4)" },
	{ OPT_VDI_WIDTH,     NULL, "--vdi-width",
	  "<w>", "VDI mode width (320 < w <= 1024)" },
	{ OPT_VDI_HEIGHT,     NULL, "--vdi-height",
	  "<h>", "VDI mode height (200 < h <= 768)" },
	
	{ OPT_HEADER, NULL, NULL, NULL, "Devices" },
	{ OPT_JOYSTICK,  "-j", "--joystick",
	  "<port>", "Emulate joystick with cursor keys in given port (0-5)" },
	/* these have to be exactly the same as I'm relying compiler giving
	 * them the same same string pointer when strings are identical
	 */
	{ OPT_JOYSTICK0, NULL, "--joy<port>",
	  "<type>", "Set joystick type (none/keys/real) for given port" },
	{ OPT_JOYSTICK1, NULL, "--joy<port>",
	  "<type>", "Set joystick type (none/keys/real) for given port" },
	{ OPT_JOYSTICK2, NULL, "--joy<port>",
	  "<type>", "Set joystick type (none/keys/real) for given port" },
	{ OPT_JOYSTICK3, NULL, "--joy<port>",
	  "<type>", "Set joystick type (none/keys/real) for given port" },
	{ OPT_JOYSTICK4, NULL, "--joy<port>",
	  "<type>", "Set joystick type (none/keys/real) for given port" },
	{ OPT_JOYSTICK5, NULL, "--joy<port>",
	  "<type>", "Set joystick type (none/keys/real) for given port" },
	{ OPT_PRINTER,   NULL, "--printer",
	  "<file>", "Enable printer support and write data to <file>" },
	{ OPT_MIDI_IN,   NULL, "--midi-in",
	  "<file>", "Enable MIDI and use <file> as the input device" },
	{ OPT_MIDI_OUT,  NULL, "--midi-out",
	  "<file>", "Enable MIDI and use <file> as the output device" },
	{ OPT_RS232_IN,  NULL, "--rs232-in",
	  "<file>", "Enable serial port and use <file> as the input device" },
	{ OPT_RS232_OUT, NULL, "--rs232-out",
	  "<file>", "Enable serial port and use <file> as the output device" },
	
	{ OPT_HEADER, NULL, NULL, NULL, "Disk" },
	{ OPT_DISKA, NULL, "--disk-a",
	  "<file>", "Set disk image for floppy drive A" },
	{ OPT_DISKB, NULL, "--disk-b",
	  "<file>", "Set disk image for floppy drive B" },
	{ OPT_SLOWFLOPPY,   NULL, "--slowfdc",
	  "<bool>", "Slow down floppy disk access emulation" },
	{ OPT_HARDDRIVE, "-d", "--harddrive",
	  "<dir>", "Emulate harddrive (partitions) with <dir> contents" },
	{ OPT_ACSIHDIMAGE,   NULL, "--acsi",
	  "<file>", "Emulate an ACSI harddrive with an image <file>" },
	{ OPT_IDEHDIMAGE,   NULL, "--ide",
	  "<file>", "Emulate an IDE harddrive with an image <file>" },
	
	{ OPT_HEADER, NULL, NULL, NULL, "Memory" },
	{ OPT_MEMSIZE,   "-s", "--memsize",
	  "<x>", "ST RAM size (x = size in MiB from 0 to 14, 0 = 512KiB)" },
	{ OPT_TOS,       "-t", "--tos",
	  "<file>", "Use TOS image <file>" },
	{ OPT_CARTRIDGE, NULL, "--cartridge",
	  "<file>", "Use ROM cartridge image <file>" },
	{ OPT_MEMSTATE,   NULL, "--memstate",
	  "<file>", "Load memory snap-shot <file>" },
	
	{ OPT_HEADER, NULL, NULL, NULL, "CPU" },
	{ OPT_CPULEVEL,  NULL, "--cpulevel",
	  "<x>", "Set the CPU type (x => 680x0) (EmuTOS/TOS 2.06 only!)" },
	{ OPT_CPUCLOCK,  NULL, "--cpuclock",
	  "<x>", "Set the CPU clock (x = 8/16/32)" },
	{ OPT_COMPATIBLE, NULL, "--compatible",
	  "<bool>", "Use a more compatible (but slower) 68000 CPU mode" },
	
	{ OPT_HEADER, NULL, NULL, NULL, "Misc system" },
	{ OPT_MACHINE,   NULL, "--machine",
	  "<x>", "Select machine type (x = st/ste/tt/falcon)" },
	{ OPT_BLITTER,   NULL, "--blitter",
	  "<bool>", "Use blitter emulation (ST only)" },
	{ OPT_DSP,       NULL, "--dsp",
	  "<x>", "DSP emulation (x = none/dummy/emu, Falcon only)" },
	{ OPT_SOUND,   NULL, "--sound",
	  "<x>", "Sound frequency (x=off/6000-50066, off=fastest)" },
	{ OPT_KEYMAPFILE, "-k", "--keymap",
	  "<file>", "Read (additional) keyboard mappings from <file>" },
	
	{ OPT_HEADER, NULL, NULL, NULL, "Debug" },
	{ OPT_DEBUG,     "-D", "--debug",
	  NULL, "Enable Hatari debugger console interface" },
	{ OPT_BIOSINTERCEPT, NULL, "--bios-intercept",
	  NULL, "Enable Bios/XBios interception (experimental)" },
	{ OPT_TRACE,   NULL, "--trace",
	  "<trace1,...>", "Activate emulation tracing, see '--trace help'" },
	{ OPT_TRACEFILE, NULL, "--trace-file",
	  "<file>", "Save trace output to <file> (default=stderr)" },
#if HAVE_UNIX_DOMAIN_SOCKETS
	{ OPT_CONTROLSOCKET, NULL, "--control-socket",
	  "<file>", "Hatari reads options from given socket at run-time" },
#endif
	{ OPT_LOGFILE, NULL, "--log-file",
	  "<file>", "Save log output to <file> (default=stderr)" },
	{ OPT_LOGLEVEL, NULL, "--log-level",
	  "<x>", "Log output level (x=debug/todo/info/warn/error/fatal)" },
	{ OPT_ALERTLEVEL, NULL, "--alert-level",
	  "<x>", "Show dialog for log messages above given level" },
	{ OPT_RUNVBLS, NULL, "--run-vbls",
	  "<x>", "Exit after x VBLs" },

	{ OPT_ERROR, NULL, NULL, NULL, NULL }
};


/**
 * Show version string and license.
 */
static void Opt_ShowVersion(void)
{
	printf("\n" PROG_NAME
	       " - the Atari ST, STE, TT and Falcon emulator.\n\n");
	printf("Hatari is free software licensed under the GNU General"
	       " Public License.\n\n");
}


/**
 * Calculate option + value len
 */
static unsigned int Opt_OptionLen(const opt_t *opt)
{
	unsigned int len;
	len = strlen(opt->str);
	if (opt->arg)
	{
		len += strlen(opt->arg);
		len += 1;
		/* with arg, short options go to another line */
	}
	else
	{
		if (opt->chr)
		{
			/* ' or -c' */
			len += 6;
		}
	}
	return len;
}


/**
 * Show single option
 */
static void Opt_ShowOption(const opt_t *opt, unsigned int maxlen)
{
	char buf[64];
	if (!maxlen)
	{
		maxlen = Opt_OptionLen(opt);
	}
	assert(maxlen < sizeof(buf));
	if (opt->arg)
	{
		sprintf(buf, "%s %s", opt->str, opt->arg);
		printf("  %-*s %s\n", maxlen, buf, opt->desc);
		if (opt->chr)
		{
			printf("    or %s %s\n", opt->chr, opt->arg);
		}
	}
	else
	{
		if (opt->chr)
		{
			sprintf(buf, "%s or %s", opt->str, opt->chr);
			printf("  %-*s %s\n", maxlen, buf, opt->desc);
		}
		else
		{
			printf("  %-*s %s\n", maxlen, opt->str, opt->desc);
		}
	}
}

/**
 * Show options for section starting from 'start_opt',
 * return next option after this section.
 */
static const opt_t *Opt_ShowHelpSection(const opt_t *start_opt)
{
	const opt_t *opt, *last;
	unsigned int len, maxlen = 0;
	const char *previous = NULL;

	/* find longest option name and check option IDs */
	for (opt = start_opt; opt->id != OPT_HEADER && opt->id != OPT_ERROR; opt++)
	{
		len = Opt_OptionLen(opt);
		if (len > maxlen)
		{
			maxlen = len;
		}
	}
	last = opt;
	
	/* output all options */
	for (opt = start_opt; opt != last; opt++)
	{
		if (previous != opt->str)
		{
			Opt_ShowOption(opt, maxlen);
		}
		previous = opt->str;
	}
	return last;
}


/**
 * Show help text.
 */
static void Opt_ShowHelp(void)
{
	const opt_t *opt = HatariOptions;

	Opt_ShowVersion();
	printf("Usage:\n hatari [options] [disk image name]\n");

	while(opt->id != OPT_ERROR)
	{
		if (opt->id == OPT_HEADER)
		{
			assert(opt->desc);
			printf("\n%s options:\n", opt->desc);
			opt++;
		}
		opt = Opt_ShowHelpSection(opt);
	}
	printf("\nSpecial option values:\n");
	printf("<bool>\tDisable by using 'n', 'no', 'off', 'false', or '0'\n");
	printf("\tEnable by using 'y', 'yes', 'on', 'true' or '1'\n");
	printf("<file>\tDevices accept also special 'stdout' and 'stderr' file names\n");
	printf("\t(if you use stdout for midi or printer, set log to stderr).\n");
	printf("\tSetting the file to 'none', disables given device or disk\n");
}


/**
 * Show Hatari version and usage.
 * If 'error' given, show that error message.
 * If 'optid' != OPT_ERROR, tells for which option the error is,
 * otherwise 'value' is show as the option user gave.
 * Return FALSE if error string was given, otherwise TRUE
 */
static bool Opt_ShowError(unsigned int optid, const char *value, const char *error)
{
	const opt_t *opt;

	Opt_ShowVersion();
	printf("Usage:\n hatari [options] [disk image name]\n\n"
	       "Try option \"-h\" or \"--help\" to display more information.\n");

	if (error)
	{
		if (optid == OPT_ERROR)
		{
			fprintf(stderr, "\nError: %s (%s)\n", error, value);
		}
		else
		{
			for (opt = HatariOptions; opt->id != OPT_ERROR; opt++)
			{
				if (optid == opt->id)
					break;
			}
			if (value != NULL)
			{
				fprintf(stderr, "\nError while parsing parameter for %s:\n"
					" %s (%s)\n", opt->str, error, value);
			}
			else
			{
				fprintf(stderr, "\nError (%s): %s\n", opt->str, error);
			}
			Opt_ShowOption(opt, 0);
		}
		return FALSE;
	}
	return TRUE;
}


/**
 * If 'conf' given, set it:
 * - TRUE if given option 'arg' is y/yes/on/true/1
 * - FALSE if given option 'arg' is n/no/off/false/0
 * Return FALSE for any other value, otherwise TRUE
 */
static bool Opt_Bool(const char *arg, int optid, bool *conf)
{
	const char *enablers[] = { "y", "yes", "on", "true", "1", NULL };
	const char *disablers[] = { "n", "no", "off", "false", "0", NULL };
	const char **bool_str, *orig = arg;
	char *input, *str;

	input = strdup(arg);
	str = input;
	while (*str)
	{
		*str++ = tolower(*arg++);
	}
	for (bool_str = enablers; *bool_str; bool_str++)
	{
		if (strcmp(input, *bool_str) == 0)
		{
			free(input);
			if (conf)
			{
				*conf = TRUE;
			}
			return TRUE;
		}
	}
	for (bool_str = disablers; *bool_str; bool_str++)
	{
		if (strcmp(input, *bool_str) == 0)
		{
			free(input);
			if (conf)
			{
				*conf = FALSE;
			}
			return TRUE;
		}
	}
	free(input);
	return Opt_ShowError(optid, orig, "Not a <bool> value");
}


/**
 * checks str argument agaist options of type "--option<digit>".
 * If match is found, returns ID for that, otherwise OPT_CONTINUE
 * and OPT_ERROR for errors.
 */
static int Opt_CheckBracketValue(const opt_t *opt, const char *str)
{
	const char *bracket, *optstr;
	size_t offset;
	int digit, i;

	if (!opt->str)
	{
		return OPT_CONTINUE;
	}
	bracket = strchr(opt->str, '<');
	if (!bracket)
	{
		return OPT_CONTINUE;
	}
	offset = bracket - opt->str;
	if (strlen(str) != offset + 1)
	{
		return OPT_CONTINUE;
	}
	digit = str[offset] - '0';
	if (digit < 0 || digit > 9)
	{
		return OPT_CONTINUE;
	}
	optstr = opt->str;
	for (i = 0; opt->str == optstr; opt++, i++)
	{
		if (i == digit)
		{
			return opt->id;
		}
	}
	/* fprintf(stderr, "opt: %s (%d), str: %s (%d), digit: %d\n",
		opt->str, offset+1, str, strlen(str), digit);
	 */
	return OPT_ERROR;
}


/**
 * matches string under given index in the argv against all Hatari
 * short and long options. If match is found, returns ID for that,
 * otherwise shows help and returns OPT_ERROR.
 * 
 * Checks also that if option is supposed to have argument,
 * whether there's one.
 */
static int Opt_WhichOption(int argc, const char *argv[], int idx)
{
	const opt_t *opt;
	const char *str = argv[idx];
	int id;

	for (opt = HatariOptions; opt->id != OPT_ERROR; opt++)
	{	
		if ((opt->str && !strcmp(str, opt->str)) ||
		    (opt->chr && !strcmp(str, opt->chr)))
		{
			
			if (opt->arg)
			{
				if (idx+1 >= argc)
				{
					Opt_ShowError(opt->id, NULL, "Missing argument");
					return OPT_ERROR;
				}
				/* early check for bools */
				if (strcmp(opt->arg, "<bool>") == 0)
				{
					if (!Opt_Bool(argv[idx+1], opt->id, NULL))
					{
						return OPT_ERROR;
					}
				}
			}
			return opt->id;
		}
		id = Opt_CheckBracketValue(opt, str);
		if (id == OPT_ERROR)
		{
			break;
		}
		if (id != OPT_CONTINUE)
		{
			return id;
		}
	}
	Opt_ShowError(OPT_ERROR, argv[idx], "Unrecognized option");
	return OPT_ERROR;
}


/**
 * If 'checkexits' is TRUE, assume 'src' is a file and check whether it
 * exists before copying 'src' to 'dst'. Otherwise just copy option src
 * string to dst.
 * If a pointer to (bool) 'option' is given, set that option to TRUE.
 * - However, if src is "none", leave dst unmodified & set option to FALSE.
 *   ("none" is used to disable options related to file arguments)
 * Return FALSE if there were errors, otherwise TRUE
 */
static bool Opt_StrCpy(int optid, bool checkexist, char *dst, const char *src, size_t dstlen, bool *option)
{
	if (strlen(src) >= dstlen)
	{
		return Opt_ShowError(optid, src, "File name too long!");
	}
	if (checkexist && !File_Exists(src))
	{
		return Opt_ShowError(optid, src, "Given file doesn't exist (or has wrong file permissions)!");
	}
	if (option)
	{
		if(strcmp(src, "none") == 0)
		{
			*option = FALSE;
			return TRUE;
		}
		else
		{
			*option = TRUE;
		}
	}
	strcpy(dst, src);
	return TRUE;
}


/**
 * parse all Hatari command line options and set Hatari state accordingly.
 * Returns TRUE if everything was OK, FALSE otherwise.
 */
bool Opt_ParseParameters(int argc, const char *argv[])
{
	int ncpu, skips, zoom, planes, cpuclock, threshold, memsize, port, freq;
	const char *errstr;
	int i, ok = TRUE;

	/* Defaults for loading initial memory snap-shots */
	bLoadMemorySave = FALSE;
	bLoadAutoSave = ConfigureParams.Memory.bAutoSave;

	for(i = 1; i < argc; i++)
	{
		if (argv[i][0] != '-')
		{
			if (Floppy_SetDiskFileName(0, argv[i], NULL))
			{
				ConfigureParams.HardDisk.bBootFromHardDisk = FALSE;
				bLoadAutoSave = FALSE;
			}
			else
				return Opt_ShowError(OPT_ERROR, argv[i], "Not an option nor disk image");
			continue;
		}
    
		/* WhichOption() checks also that there is an argument,
		 * so we don't need to check that below
		 */
		switch(Opt_WhichOption(argc, argv, i))
		{
		
			/* general options */
		case OPT_HELP:
			Opt_ShowHelp();
			return FALSE;
			
		case OPT_VERSION:
			Opt_ShowVersion();
			return FALSE;

		case OPT_CONFIRMQUIT:
			ok = Opt_Bool(argv[++i], OPT_CONFIRMQUIT, &ConfigureParams.Log.bConfirmQuit);
			break;

		case OPT_FASTFORWARD:
			ok = Opt_Bool(argv[++i], OPT_FASTFORWARD, &ConfigureParams.System.bFastForward);
			break;
			
		case OPT_CONFIGFILE:
			i += 1;
			ok = Opt_StrCpy(OPT_CONFIGFILE, TRUE, sConfigFileName,
					argv[i], sizeof(sConfigFileName), NULL);
			if (ok)
			{
				Configuration_Load(NULL);
				bLoadAutoSave = ConfigureParams.Memory.bAutoSave;
			}
			break;
		
			/* display options */
		case OPT_MONO:
			ConfigureParams.Screen.nMonitorType = MONITOR_TYPE_MONO;
			bLoadAutoSave = FALSE;
			break;

		case OPT_MONITOR:
			i += 1;
			if (strcasecmp(argv[i], "mono") == 0)
			{
				ConfigureParams.Screen.nMonitorType = MONITOR_TYPE_MONO;
			}
			else if (strcasecmp(argv[i], "rgb") == 0)
			{
				ConfigureParams.Screen.nMonitorType = MONITOR_TYPE_RGB;
			}
			else if (strcasecmp(argv[i], "vga") == 0)
			{
				ConfigureParams.Screen.nMonitorType = MONITOR_TYPE_VGA;
			}
			else if (strcasecmp(argv[i], "tv") == 0)
			{
				ConfigureParams.Screen.nMonitorType = MONITOR_TYPE_TV;
			}
			else
			{
				return Opt_ShowError(OPT_MONITOR, argv[i], "Unknown monitor type");
			}
			bLoadAutoSave = FALSE;
			break;
			
		case OPT_FULLSCREEN:
			ConfigureParams.Screen.bFullScreen = TRUE;
			break;
			
		case OPT_WINDOW:
			ConfigureParams.Screen.bFullScreen = FALSE;
			break;

		case OPT_GRAB:
			bGrabMouse = TRUE;
			break;
			
		case OPT_ZOOM:
			zoom = atoi(argv[++i]);
			if (zoom < 1)
			{
				return Opt_ShowError(OPT_ZOOM, argv[i], "Invalid zoom value");
			}
			if (zoom > 1)
			{
				/* TODO: only doubling supported for now */
				ConfigureParams.Screen.bZoomLowRes = TRUE;
			}
			else
			{
				ConfigureParams.Screen.bZoomLowRes = FALSE;
			}
			break;
			
		case OPT_FRAMESKIPS:
			skips = atoi(argv[++i]);
			if (skips < 0)
			{
				return Opt_ShowError(OPT_FRAMESKIPS, argv[i],
						     "Invalid frame skip value");
			}
			else if (skips > 8)
			{
				Log_Printf(LOG_WARN, "Extravagant frame skip value %d!\n", skips);
			}
			ConfigureParams.Screen.nFrameSkips = skips;
			break;
			
		case OPT_BORDERS:
			ok = Opt_Bool(argv[++i], OPT_BORDERS, &ConfigureParams.Screen.bAllowOverscan);
			break;
			
		case OPT_STATUSBAR:
			ok = Opt_Bool(argv[++i], OPT_STATUSBAR, &ConfigureParams.Screen.bShowStatusbar);
			break;
			
		case OPT_DRIVE_LED:
			ok = Opt_Bool(argv[++i], OPT_DRIVE_LED, &ConfigureParams.Screen.bShowDriveLed);
			break;
			
		case OPT_SPEC512:
			threshold = atoi(argv[++i]);
			if (threshold < 0 || threshold > 512)
			{
				return Opt_ShowError(OPT_SPEC512, argv[i],
						     "Invalid palette writes per line threshold for Spec512");
			}
			ConfigureParams.Screen.nSpec512Threshold = threshold;
			break;
			
		case OPT_FORCEBPP:
			planes = atoi(argv[++i]);
			switch(planes)
			{
			case 32:
			case 16:
			case 15:
			case 8:
				break;       /* supported */
			case 24:
				planes = 32; /* We do not support 24 bpp (yet) */
				break;
			default:
				return Opt_ShowError(OPT_FORCEBPP, argv[i], "Invalid bit depth");
			}
			ConfigureParams.Screen.nForceBpp = planes;
			break;

			/* VDI options */
		case OPT_VDI:
			ok = Opt_Bool(argv[++i], OPT_VDI, &ConfigureParams.Screen.bUseExtVdiResolutions);
			if (ok)
			{
				bLoadAutoSave = FALSE;
			}
			break;

		case OPT_VDI_PLANES:
			planes = atoi(argv[++i]);
			switch(planes)
			{
			 case 1:
				ConfigureParams.Screen.nVdiColors = GEMCOLOR_2;
				break;
			 case 2:
				ConfigureParams.Screen.nVdiColors = GEMCOLOR_4;
				break;
			 case 4:
				ConfigureParams.Screen.nVdiColors = GEMCOLOR_16;
				break;
			 default:
				return Opt_ShowError(OPT_VDI_PLANES, argv[i], "Unsupported VDI bit-depth");
			}
			ConfigureParams.Screen.bUseExtVdiResolutions = TRUE;
			bLoadAutoSave = FALSE;
			break;

		case OPT_VDI_WIDTH:
			ConfigureParams.Screen.nVdiWidth = atoi(argv[++i]);
			ConfigureParams.Screen.bUseExtVdiResolutions = TRUE;
			bLoadAutoSave = FALSE;
			break;

		case OPT_VDI_HEIGHT:
			ConfigureParams.Screen.nVdiHeight = atoi(argv[++i]);
			ConfigureParams.Screen.bUseExtVdiResolutions = TRUE;
			bLoadAutoSave = FALSE;
			break;
		
			/* devices options */
		case OPT_JOYSTICK:
			i++;
			if (strlen(argv[i]) != 1 ||
			    !Joy_SetCursorEmulation(argv[i][0] - '0'))
			{
				return Opt_ShowError(OPT_JOYSTICK, argv[i], "Invalid joystick port");
			}
			break;

		case OPT_JOYSTICK0:
		case OPT_JOYSTICK1:
		case OPT_JOYSTICK2:
		case OPT_JOYSTICK3:
		case OPT_JOYSTICK4:
		case OPT_JOYSTICK5:
			port = argv[i][strlen(argv[i])-1] - '0';
			assert(port >= 0 && port < JOYSTICK_COUNT);
			i += 1;
			if (strcasecmp(argv[i], "none") == 0)
			{
				ConfigureParams.Joysticks.Joy[port].nJoystickMode = JOYSTICK_DISABLED;
			}
			else if (strcasecmp(argv[i], "keys") == 0)
			{
				ConfigureParams.Joysticks.Joy[port].nJoystickMode = JOYSTICK_KEYBOARD;
			}
			else if (strcasecmp(argv[i], "real") == 0)
			{
				ConfigureParams.Joysticks.Joy[port].nJoystickMode = JOYSTICK_REALSTICK;
			}
			else
			{
				return Opt_ShowError(OPT_JOYSTICK0+port, argv[i], "Invalid joystick type");
			}
			break;
			
		case OPT_PRINTER:
			i += 1;
			ok = Opt_StrCpy(OPT_PRINTER, FALSE, ConfigureParams.Printer.szPrintToFileName,
					argv[i], sizeof(ConfigureParams.Printer.szPrintToFileName),
					&ConfigureParams.Printer.bEnablePrinting);
			break;
			
		case OPT_MIDI_IN:
			i += 1;
			/* FALSE: "" can be used to disable midi input */
			ok = Opt_StrCpy(OPT_MIDI_IN, FALSE, ConfigureParams.Midi.sMidiInFileName,
					argv[i], sizeof(ConfigureParams.Midi.sMidiInFileName),
					&ConfigureParams.Midi.bEnableMidi);
			break;
			
		case OPT_MIDI_OUT:
			i += 1;
			ok = Opt_StrCpy(OPT_MIDI_OUT, TRUE, ConfigureParams.Midi.sMidiOutFileName,
					argv[i], sizeof(ConfigureParams.Midi.sMidiOutFileName),
					&ConfigureParams.Midi.bEnableMidi);
			break;
      
		case OPT_RS232_IN:
			i += 1;
			ok = Opt_StrCpy(OPT_RS232_IN, TRUE, ConfigureParams.RS232.szInFileName,
					argv[i], sizeof(ConfigureParams.RS232.szInFileName),
					&ConfigureParams.RS232.bEnableRS232);
			break;
      
		case OPT_RS232_OUT:
			i += 1;
			ok = Opt_StrCpy(OPT_RS232_OUT, TRUE, ConfigureParams.RS232.szOutFileName,
					argv[i], sizeof(ConfigureParams.RS232.szOutFileName),
					&ConfigureParams.RS232.bEnableRS232);
			break;

			/* disk options */
		case OPT_DISKA:
			i += 1;
			if (Floppy_SetDiskFileName(0, argv[i], NULL))
			{
				ConfigureParams.HardDisk.bBootFromHardDisk = FALSE;
				bLoadAutoSave = FALSE;
			}
			else
				return Opt_ShowError(OPT_ERROR, argv[i], "Not a disk image");
			break;

		case OPT_DISKB:
			i += 1;
			if (Floppy_SetDiskFileName(1, argv[i], NULL))
				bLoadAutoSave = FALSE;
			else
				return Opt_ShowError(OPT_ERROR, argv[i], "Not a disk image");
			break;

		case OPT_HARDDRIVE:
			i += 1;
			ok = Opt_StrCpy(OPT_HARDDRIVE, FALSE, ConfigureParams.HardDisk.szHardDiskDirectories[0],
					argv[i], sizeof(ConfigureParams.HardDisk.szHardDiskDirectories[0]),
					&ConfigureParams.HardDisk.bUseHardDiskDirectories);
			if (ok && ConfigureParams.HardDisk.bUseHardDiskDirectories)
			{
				ConfigureParams.HardDisk.bBootFromHardDisk = TRUE;
			}
			bLoadAutoSave = FALSE;
			break;

		case OPT_ACSIHDIMAGE:
			i += 1;
			ok = Opt_StrCpy(OPT_ACSIHDIMAGE, TRUE, ConfigureParams.HardDisk.szHardDiskImage,
					argv[i], sizeof(ConfigureParams.HardDisk.szHardDiskImage),
					&ConfigureParams.HardDisk.bUseHardDiskImage);
			if (ok)
			{
				bLoadAutoSave = FALSE;
			}
			break;
			
		case OPT_IDEHDIMAGE:
			i += 1;
			ok = Opt_StrCpy(OPT_IDEHDIMAGE, TRUE, ConfigureParams.HardDisk.szIdeHardDiskImage,
					argv[i], sizeof(ConfigureParams.HardDisk.szIdeHardDiskImage),
					&ConfigureParams.HardDisk.bUseIdeHardDiskImage);
			if (ok)
			{
				bLoadAutoSave = FALSE;
			}
			break;
			
		case OPT_SLOWFLOPPY:
			ok = Opt_Bool(argv[++i], OPT_SLOWFLOPPY, &ConfigureParams.DiskImage.bSlowFloppy);
			if (ok)
			{
				bLoadAutoSave = FALSE;
			}
			break;
			
			/* Memory options */
		case OPT_MEMSIZE:
			memsize = atoi(argv[++i]);
			if (memsize < 0 || memsize > 14)
			{
				return Opt_ShowError(OPT_MEMSIZE, argv[i], "Invalid memory size");
			}
			ConfigureParams.Memory.nMemorySize = memsize;
			bLoadAutoSave = FALSE;
			break;
      
		case OPT_TOS:
			i += 1;
			ok = Opt_StrCpy(OPT_TOS, TRUE, ConfigureParams.Rom.szTosImageFileName,
					argv[i], sizeof(ConfigureParams.Rom.szTosImageFileName),
					NULL);
			if (ok)
			{
				bLoadAutoSave = FALSE;
			}
			break;
			
		case OPT_CARTRIDGE:
			i += 1;
			ok = Opt_StrCpy(OPT_CARTRIDGE, TRUE, ConfigureParams.Rom.szCartridgeImageFileName,
					argv[i], sizeof(ConfigureParams.Rom.szCartridgeImageFileName),
					NULL);
			if (ok)
			{
				bLoadAutoSave = FALSE;
			}
			break;

		case OPT_MEMSTATE:
			i += 1;
			ok = Opt_StrCpy(OPT_MEMSTATE, TRUE, ConfigureParams.Memory.szMemoryCaptureFileName,
					argv[i], sizeof(ConfigureParams.Memory.szMemoryCaptureFileName),
					NULL);
			if (ok)
			{
				bLoadMemorySave = TRUE;
				bLoadAutoSave = FALSE;
			}
			break;
			
			/* CPU options */
		case OPT_CPULEVEL:
			/* UAE core uses cpu_level variable */
			ncpu = atoi(argv[++i]);
			if(ncpu < 0 || ncpu > 4)
			{
				return Opt_ShowError(OPT_CPULEVEL, argv[i], "Invalid CPU level");
			}
			ConfigureParams.System.nCpuLevel = ncpu;
			bLoadAutoSave = FALSE;
			break;
			
		case OPT_CPUCLOCK:
			cpuclock = atoi(argv[++i]);
			if(cpuclock != 8 && cpuclock != 16 && cpuclock != 32)
			{
				return Opt_ShowError(OPT_CPUCLOCK, argv[i], "Invalid CPU clock");
			}
			ConfigureParams.System.nCpuFreq = cpuclock;
			bLoadAutoSave = FALSE;
			break;
			
		case OPT_COMPATIBLE:
			ok = Opt_Bool(argv[++i], OPT_COMPATIBLE, &ConfigureParams.System.bCompatibleCpu);
			if (ok)
			{
				bLoadAutoSave = FALSE;
			}
			break;

			/* system options */
		case OPT_MACHINE:
			i += 1;
			if (strcasecmp(argv[i], "st") == 0)
			{
				ConfigureParams.System.nMachineType = MACHINE_ST;
				ConfigureParams.System.nCpuLevel = 0;
				ConfigureParams.System.nCpuFreq = 8;
			}
			else if (strcasecmp(argv[i], "ste") == 0)
			{
				ConfigureParams.System.nMachineType = MACHINE_STE;
				ConfigureParams.System.nCpuLevel = 0;
				ConfigureParams.System.nCpuFreq = 8;
			}
			else if (strcasecmp(argv[i], "tt") == 0)
			{
				ConfigureParams.System.nMachineType = MACHINE_TT;
				ConfigureParams.System.nCpuLevel = 3;
				ConfigureParams.System.nCpuFreq = 32;
			}
			else if (strcasecmp(argv[i], "falcon") == 0)
			{
				ConfigureParams.System.nMachineType = MACHINE_FALCON;
				ConfigureParams.System.nCpuLevel = 3;
				ConfigureParams.System.nCpuFreq = 16;
			}
			else
			{
				return Opt_ShowError(OPT_MACHINE, argv[i], "Unknown machine type");
			}
			bLoadAutoSave = FALSE;
			break;
			
		case OPT_BLITTER:
			ok = Opt_Bool(argv[++i], OPT_BLITTER, &ConfigureParams.System.bBlitter);
			if (ok)
			{
				bLoadAutoSave = FALSE;
			}
			break;			

		case OPT_DSP:
			i += 1;
			if (strcasecmp(argv[i], "none") == 0)
			{
				ConfigureParams.System.nDSPType = DSP_TYPE_NONE;
			}
			else if (strcasecmp(argv[i], "dummy") == 0)
			{
				ConfigureParams.System.nDSPType = DSP_TYPE_DUMMY;
			}
			else if (strcasecmp(argv[i], "emu") == 0)
			{
#if ENABLE_DSP_EMU
				ConfigureParams.System.nDSPType = DSP_TYPE_EMU;
#else
				return Opt_ShowError(OPT_DSP, argv[i], "DSP type 'emu' support not compiled in");
#endif
			}
			else
			{
				return Opt_ShowError(OPT_DSP, argv[i], "Unknown DSP type");
			}
			bLoadAutoSave = FALSE;
			break;
			
		case OPT_SOUND:
			i += 1;
			if (strcasecmp(argv[i], "off") == 0)
			{
				ConfigureParams.Sound.bEnableSound = FALSE;
			}
			else
			{
				freq = atoi(argv[i]);
				if (freq < 6000 || freq > 50066)
				{
					return Opt_ShowError(OPT_SOUND, argv[i], "Unsupported sound frequency");
				}
				ConfigureParams.Sound.nPlaybackFreq = freq;
				ConfigureParams.Sound.bEnableSound = TRUE;
			}
			break;

		case OPT_KEYMAPFILE:
			i += 1;
			ok = Opt_StrCpy(OPT_KEYMAPFILE, TRUE, ConfigureParams.Keyboard.szMappingFileName,
					argv[i], sizeof(ConfigureParams.Keyboard.szMappingFileName),
					NULL);
			if (ok)
			{
				ConfigureParams.Keyboard.nKeymapType = KEYMAP_LOADED;
			}
			break;
			
			/* debug options */
		case OPT_DEBUG:
			if (bEnableDebug)
			{
				/* called at run time (e.g. from debugger) */
				fprintf(stderr, "Debug mode disabled.\n");
				bEnableDebug = FALSE;
			}
			else
			{
				bEnableDebug = TRUE;
			}
			break;

		case OPT_BIOSINTERCEPT:
			bBiosIntercept = TRUE;
			break;
			
		case OPT_TRACE:
			i += 1;
			if (Log_SetTraceOptions(argv[i]) == 0)
			{
				return Opt_ShowError(OPT_TRACE, argv[i], "Error parsing trace options (use --trace help for available list)!");
			}
			break;

		case OPT_TRACEFILE:
			i += 1;
			ok = Opt_StrCpy(OPT_TRACEFILE, FALSE, ConfigureParams.Log.sTraceFileName,
					argv[i], sizeof(ConfigureParams.Log.sTraceFileName),
					NULL);
			break;

		case OPT_CONTROLSOCKET:
			i += 1;
			errstr = Control_SetSocket(argv[i]);
			if (errstr)
			{
				return Opt_ShowError(OPT_CONTROLSOCKET, argv[i], errstr);
			}
			break;

		case OPT_LOGFILE:
			i += 1;
			ok = Opt_StrCpy(OPT_LOGFILE, FALSE, ConfigureParams.Log.sLogFileName,
					argv[i], sizeof(ConfigureParams.Log.sLogFileName),
					NULL);
			break;

		case OPT_LOGLEVEL:
			i += 1;
			ConfigureParams.Log.nTextLogLevel = Log_ParseOptions(argv[i]);
			if (ConfigureParams.Log.nTextLogLevel == LOG_NONE)
			{
				return Opt_ShowError(OPT_LOGLEVEL, argv[i], "Unknown log level!");
			}
			break;

		case OPT_ALERTLEVEL:
			i += 1;
			ConfigureParams.Log.nAlertDlgLogLevel = Log_ParseOptions(argv[i]);
			if (ConfigureParams.Log.nAlertDlgLogLevel == LOG_NONE)
			{
				return Opt_ShowError(OPT_ALERTLEVEL, argv[i], "Unknown alert level!");
			}
			break;

		case OPT_RUNVBLS:
			nRunVBLs = atol(argv[++i]);
			break;
		       
		case OPT_ERROR:
			/* unknown option or missing option parameter */
			return FALSE;

		default:
			return Opt_ShowError(OPT_ERROR, argv[i], "Internal Hatari error, unhandled option");
		}
		if (!ok)
		{
			/* Opt_Bool() or Opt_StrCpy() failed */
			return FALSE;
		}
	}

	return TRUE;
}
