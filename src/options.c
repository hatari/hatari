/*
  Hatari - options.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Functions for showing and parsing all of Hatari's command line options.
  
  To add a new option:
  - Add option ID to the enum
  - Add the option information to HatariOptions[]
  - Add required actions for that ID to switch in Opt_ParseParameters()

  2007-09-27   [NP]    Add parsing for the '--trace' option.
  2008-03-01   [ET]    Add option sections
*/

const char Main_rcsid[] = "Hatari $Id: options.c,v 1.35 2008-03-01 17:59:13 eerot Exp $";

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "main.h"
#include "options.h"
#include "configuration.h"
#include "file.h"
#include "screen.h"
#include "video.h"
#include "vdi.h"
#include "joy.h"
#include "trace.h"

#include "hatari-glue.h"


BOOL bLoadAutoSave;        /* Load autosave memory snapshot at startup */
BOOL bLoadMemorySave;      /* Load memory snapshot provided via option at startup */


/*  List of supported options. */
enum {
	OPT_HEADER,	/* options section header */
	OPT_HELP,		/* general options */
	OPT_VERSION,
	OPT_CONFIRMQUIT,
	OPT_MONO,		/* display options */
	OPT_MONITOR,
	OPT_FULLSCREEN,
	OPT_WINDOW,
	OPT_ZOOM,
	OPT_FRAMESKIPS,
	OPT_FORCE8BPP,
	OPT_BORDERS,
	OPT_VDI_PLANES,
	OPT_VDI_WIDTH,
	OPT_VDI_HEIGHT,
	OPT_JOYSTICK,		/* device options */
	OPT_PRINTER,
	OPT_MIDI,
	OPT_RS232,
	OPT_ACSIHDIMAGE,	/* disk options */
	OPT_IDEHDIMAGE,
	OPT_HARDDRIVE,
	OPT_TOS,		/* system options */
	OPT_CARTRIDGE,
	OPT_CPULEVEL,
	OPT_COMPATIBLE,
	OPT_BLITTER,
	OPT_DSP,
	OPT_MEMSIZE,
	OPT_MEMSTATE,
	OPT_CONFIGFILE,
	OPT_KEYMAPFILE,
	OPT_SLOWFDC,
	OPT_MACHINE,
	OPT_NOSOUND,
	OPT_DEBUG,		/* debug options */
	OPT_LOG,
	OPT_TRACE,
	OPT_NONE,
};

typedef struct {
	unsigned int id;	/* option ID */
	const char *chr;	/* short option */
	const char *str;	/* long option */
	const char *arg;	/* name for argument, if any */
	const char *desc;	/* option description */
} opt_t;

/* it's easier to edit these if they are kept in the same order as the enums */
static const opt_t HatariOptions[] = {
	
	{ OPT_HEADER, NULL, NULL, NULL, "General" },
	{ OPT_HELP,      "-h", "--help",
	  NULL, "Print this help text and exit" },
	{ OPT_VERSION,   "-v", "--version",
	  NULL, "Print version number and exit" },
	{ OPT_CONFIRMQUIT,NULL, "--confirm-quit",
	  "<x>", "Whether Hatari confirms quit (y/n)" },
	
	{ OPT_HEADER, NULL, NULL, NULL, "Display" },
	{ OPT_MONO,      "-m", "--mono",
	  NULL, "Start in monochrome mode instead of color (deprecated)" },
	{ OPT_MONITOR,      NULL, "--monitor",
	  "<x>", "Select monitor type (x = mono/rgb/vga/tv)" },
	{ OPT_FULLSCREEN,"-f", "--fullscreen",
	  NULL, "Start emulator in fullscreen mode" },
	{ OPT_WINDOW,    "-w", "--window",
	  NULL, "Start emulator in window mode" },
	{ OPT_ZOOM, "-z", "--zoom",
	  "<x>", "Double ST low resolution (1=no, 2=yes)" },
	{ OPT_FRAMESKIPS, NULL, "--frameskips",
	  "<x>", "Skip <x> frames after each displayed frame (0 <= x <= 8)" },
	{ OPT_FORCE8BPP, NULL, "--force8bpp",
	  NULL, "Use 8-bit color depths only (for older host computers)" },
	{ OPT_BORDERS, NULL, "--borders",
	  NULL, "Show screen borders (for overscan demos etc)" },
	{ OPT_VDI_PLANES,NULL, "--vdi-planes",
	  "<x>", "VDI resolution bit-depth (x = 1/2/4)" },
	{ OPT_VDI_WIDTH,     NULL, "--vdi-width",
	  "<w>", "Use VDI resolution with width w (320 < w <= 1024)" },
	{ OPT_VDI_HEIGHT,     NULL, "--vdi-height",
	  "<h>", "VDI resolution with height h (200 < h <= 768)" },
	
	{ OPT_HEADER, NULL, NULL, NULL, "Devices" },
	{ OPT_JOYSTICK,  "-j", "--joystick",
	  "<port>", "Emulate joystick with cursor keys in given port (0-5)" },
	{ OPT_PRINTER,   NULL, "--printer",
	  "<file>", "Enable printer support and write data to <file>" },
	{ OPT_MIDI,      NULL, "--midi",
	  "<file>", "Enable midi support and write midi data to <file>" },
	{ OPT_RS232,     NULL, "--rs232",
	  "<file>", "Enable serial port support and use <file> as the device" },
	
	{ OPT_HEADER, NULL, NULL, NULL, "Disk" },
	{ OPT_ACSIHDIMAGE,   NULL, "--acsi",
	  "<file>", "Emulate an ACSI harddrive with an image <file>" },
	{ OPT_IDEHDIMAGE,   NULL, "--ide",
	  "<file>", "Emulate an IDE harddrive using <file> (not working yet)" },
	{ OPT_HARDDRIVE, "-d", "--harddrive",
	  "<dir>", "Emulate an ST harddrive (<dir> = root directory)" },
	
	{ OPT_HEADER, NULL, NULL, NULL, "System" },
	{ OPT_TOS,       "-t", "--tos",
	  "<file>", "Use TOS image <file>" },
	{ OPT_CARTRIDGE, NULL, "--cartridge",
	  "<file>", "Use ROM cartridge image <file>" },
	{ OPT_CPULEVEL,  NULL, "--cpulevel",
	  "<x>", "Set the CPU type (x => 680x0) (TOS 2.06 only!)" },
	{ OPT_COMPATIBLE,NULL, "--compatible",
	  NULL, "Use a more compatible (but slower) 68000 CPU mode" },
	{ OPT_BLITTER,   NULL, "--blitter",
	  NULL, "Enable blitter emulation (ST only)" },
	{ OPT_DSP,       NULL, "--dsp",
	  "<x>", "DSP emulation (x=none/dummy/emu, for Falcon mode only)" },
	{ OPT_MEMSIZE,   "-s", "--memsize",
	  "<x>", "ST RAM size. x = size in MiB from 0 to 14, 0 for 512KiB" },
	{ OPT_MEMSTATE,   NULL, "--memstate",
	  "<file>", "Load memory snap-shot <file>" },
	{ OPT_CONFIGFILE,"-c", "--configfile",
	  "<file>", "Use <file> instead of the ~/.hatari.cfg config file" },
	{ OPT_KEYMAPFILE,"-k", "--keymap",
	  "<file>", "Read (additional) keyboard mappings from <file>" },
	{ OPT_SLOWFDC,   NULL, "--slowfdc",
	  NULL, "Slow down FDC emulation (deprecated)" },
	{ OPT_MACHINE,   NULL, "--machine",
	  "<x>", "Select machine type (x = st/ste/tt/falcon)" },
	{ OPT_NOSOUND,   NULL, "--nosound",
	  NULL, "Disable sound (faster!)" },
	
	{ OPT_HEADER, NULL, NULL, NULL, "Debug" },
	{ OPT_DEBUG,     "-D", "--debug",
	  NULL, "Allow debug interface" },
	{ OPT_LOG,     NULL, "--log",
	  "<file>", "Save log to <file>" },
	{ OPT_TRACE,   NULL, "--trace",
	  "<trace1,...>", "Activate debug traces, see --trace help for options" },
	
	{ OPT_NONE, NULL, NULL, NULL, NULL }
};


/**
 * Show version string and license.
 */
static void Opt_ShowVersion(void)
{
	printf("\nThis is %s - the Atari ST, STE, TT and Falcon emulator.\n\n",
	       PROG_NAME);
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

	/* find longest option name and check option IDs */
	for (opt = start_opt; opt->id != OPT_HEADER && opt->id != OPT_NONE; opt++)
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
		Opt_ShowOption(opt, maxlen);
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

	while(opt->id != OPT_NONE)
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
	printf("<file>\t'stdout' and 'stderr' can be used for devices\n");
	printf("\tIf you use stdout for midi or printer, set log to stderr!\n");
}


/**
 * Show Hatari options and exit().
 * If 'error' given, show that error message.
 * If 'option' != OPT_NONE, tells for which option the error is,
 * otherwise 'value' is show as the option user gave.
 */
static void Opt_ShowExit(unsigned int option, const char *value, const char *error)
{
	const opt_t *opt;

	Opt_ShowVersion();
	printf("Usage:\n hatari [options] [disk image name]\n\n"
	       "Try option \"-h\" or \"--help\" to display more information.\n");

	if (error)
	{
		if (option == OPT_NONE)
		{
			fprintf(stderr, "\nError: %s (%s)\n", error, value);
		}
		else
		{
			for (opt = HatariOptions; opt->id != OPT_NONE; opt++)
			{
				if (option == opt->id)
					break;
			}
			if (value != NULL)
			{
				fprintf(stderr, "\nError while parsing parameter %s :\n"
					" %s (%s)\n", opt->str, error, value);
			}
			else
			{
				fprintf(stderr, "\nError (%s): %s\n", opt->str, error);
			}
			Opt_ShowOption(opt, 0);
		}
		exit(1);
	}
	exit(0);
}


/**
 * matches string under given index in the argv against all Hatari
 * short and long options. If match is found, returns ID for that,
 * otherwise shows help.
 * 
 * Checks also that if option is supposed to have argument,
 * whether there's one.
 */
static int Opt_WhichOption(int argc, char *argv[], int idx)
{
	const opt_t *opt;
	const char *str = argv[idx];

	for (opt = HatariOptions; opt->id != OPT_NONE; opt++)
	{	
		if ((opt->str && !strcmp(str, opt->str)) ||
		    (opt->chr && !strcmp(str, opt->chr)))
		{
			
			if (opt->arg && idx+1 >= argc)
			{
				Opt_ShowExit(opt->id, NULL, "Missing argument");
			}
			return opt->id;
		}
	}
	Opt_ShowExit(OPT_NONE, argv[idx], "Unrecognized option");
	return OPT_NONE;
}


/**
 * return
 * - true if given string is y, Y, yes, YES
 * - false if given string is n, N, no, NO
 * otherwise exit
 */
static int Opt_YesNo(const char *arg, int opt)
{
	int ret = FALSE;
	char *input, *str;
	const char *orig;
	str = strdup(arg);
	input = str;
	orig = arg;
	while (*str)
	{
		*str++ = tolower(*arg++);
	}
	if (strcmp("y", input) == 0 || strcmp("yes", input) == 0)
	{
		ret = TRUE;
	}
	else if (strcmp("n", input) == 0 || strcmp("no", input) == 0)
	{
		ret = FALSE;
	}
	else
	{
		Opt_ShowExit(opt, orig, "Unrecognized value");
	}
	free(input);
	return ret;
}


/**
 * Copy option string, check string length and optionally test if file exists.
 * Bail out on errors.
 */
static void Opt_StrCpy(int option, BOOL checkexist, char *dst, char *src, size_t dstlen)
{
	if (checkexist && !File_Exists(src))
	{
		Opt_ShowExit(option, src, "Given file doesn't exist (or has wrong file permissions)!\n");
	}

	if (strlen(src) < dstlen)
	{
		strcpy(dst, src);
	}
	else
	{
		Opt_ShowExit(option, src, "File name too long!\n");
	}
}


/**
 * Check for any passed parameters, return boot disk
 */
void Opt_ParseParameters(int argc, char *argv[],
			 char *bootdisk, size_t bootlen)
{
	int i, ncpu, skips, zoom, planes;

	/* Defaults for loading initial memory snap-shots*/
	bLoadMemorySave = FALSE;
	bLoadAutoSave = ConfigureParams.Memory.bAutoSave;

	for(i = 1; i < argc; i++)
	{	
		if (argv[i][0] != '-')
		{
			/* Possible passed disk image filename */
			if (argv[i][0] && File_Exists(argv[i]) &&
			    strlen(argv[i]) < bootlen)
			{
				strcpy(bootdisk, argv[i]);
				File_MakeAbsoluteName(bootdisk);
				bLoadAutoSave = FALSE;
			}
			else
			{
				Opt_ShowExit(OPT_NONE, argv[i], "Not an option nor disk image");
			}
			continue;
		}
    
		/* WhichOption() checks also that there is an argument,
		 * so we don't need to check that below
		 */
		switch(Opt_WhichOption(argc, argv, i))
		{

		case OPT_HELP:
			Opt_ShowHelp();
			exit(0);
			break;
			
		case OPT_VERSION:
			Opt_ShowVersion();
			exit(0);
			break;

		case OPT_CONFIRMQUIT:
			ConfigureParams.Log.bConfirmQuit = Opt_YesNo(argv[++i], OPT_CONFIRMQUIT);
			break;
			
		case OPT_MONO:
			ConfigureParams.Screen.MonitorType = MONITOR_TYPE_MONO;
			bLoadAutoSave = FALSE;
			break;

		case OPT_MONITOR:
			i += 1;
			if (strcasecmp(argv[i], "mono") == 0)
			{
				ConfigureParams.Screen.MonitorType = MONITOR_TYPE_MONO;
			}
			else if (strcasecmp(argv[i], "rgb") == 0)
			{
				ConfigureParams.Screen.MonitorType = MONITOR_TYPE_RGB;
			}
			else if (strcasecmp(argv[i], "vga") == 0)
			{
				ConfigureParams.Screen.MonitorType = MONITOR_TYPE_VGA;
			}
			else if (strcasecmp(argv[i], "tv") == 0)
			{
				ConfigureParams.Screen.MonitorType = MONITOR_TYPE_TV;
			}
			else
			{
				Opt_ShowExit(OPT_MONITOR, argv[i], "Unknown monitor type");
			}
			bLoadAutoSave = FALSE;
			break;
			
		case OPT_FULLSCREEN:
			ConfigureParams.Screen.bFullScreen = TRUE;
			break;
			
		case OPT_WINDOW:
			ConfigureParams.Screen.bFullScreen = FALSE;
			break;
			
		case OPT_ZOOM:
			zoom = atoi(argv[++i]);
			if (zoom < 1)
			{
				Opt_ShowExit(OPT_ZOOM, argv[i], "Invalid zoom value");
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
			if (skips < 0 || skips > 8)
			{
				Opt_ShowExit(OPT_FRAMESKIPS, argv[i],
				             "Invalid frame skip value");
			}
			ConfigureParams.Screen.FrameSkips = skips;
			break;
			
		case OPT_FORCE8BPP:
			ConfigureParams.Screen.bForce8Bpp = TRUE;
			break;
			
		case OPT_BORDERS:
			ConfigureParams.Screen.bAllowOverscan = TRUE;
			break;
			
		case OPT_JOYSTICK:
			i++;
			if (!Joy_SetCursorEmulation(argv[i][0] - '0'))
			{
				/* TODO: replace this with an error message
				 * for the next version.  For now assume
				 * that -j was used without an argument
				 * which earlier defaulted to port 1
				 */
				fprintf(stderr, "WARNING: assuming -j <port> argument is missing and defaulting to port 1.\n");
				Joy_SetCursorEmulation(1);
				i--;
			}
			break;
			
		case OPT_NOSOUND:
			ConfigureParams.Sound.bEnableSound = FALSE;
			break;
			
		case OPT_DEBUG:
			bEnableDebug = TRUE;
			break;
			
		case OPT_LOG:
			i += 1;
			Opt_StrCpy(OPT_LOG, FALSE, ConfigureParams.Log.sLogFileName,
			           argv[i], sizeof(ConfigureParams.Log.sLogFileName));
			break;
			
		case OPT_PRINTER:
			i += 1;
			Opt_StrCpy(OPT_PRINTER, FALSE, ConfigureParams.Printer.szPrintToFileName,
			           argv[i], sizeof(ConfigureParams.Printer.szPrintToFileName));
			ConfigureParams.Printer.bEnablePrinting = TRUE;
			break;
			
		case OPT_MIDI:
			i += 1;
			Opt_StrCpy(OPT_MIDI, FALSE, ConfigureParams.Midi.szMidiOutFileName,
			           argv[i], sizeof(ConfigureParams.Midi.szMidiOutFileName));
			ConfigureParams.Midi.bEnableMidi = TRUE;
			break;
      
		case OPT_RS232:
			i += 1;
			Opt_StrCpy(OPT_RS232, TRUE, ConfigureParams.RS232.szInFileName,
			           argv[i], sizeof(ConfigureParams.RS232.szInFileName));
			strncpy(ConfigureParams.RS232.szOutFileName, argv[i],
			        sizeof(ConfigureParams.RS232.szOutFileName));
			ConfigureParams.RS232.bEnableRS232 = TRUE;
			break;
			
		case OPT_ACSIHDIMAGE:
			i += 1;
			Opt_StrCpy(OPT_ACSIHDIMAGE, TRUE, ConfigureParams.HardDisk.szHardDiskImage,
			           argv[i], sizeof(ConfigureParams.HardDisk.szHardDiskImage));
			ConfigureParams.HardDisk.bUseHardDiskImage = TRUE;
			bLoadAutoSave = FALSE;
			break;
			
		case OPT_IDEHDIMAGE:
			i += 1;
			Opt_StrCpy(OPT_IDEHDIMAGE, TRUE, ConfigureParams.HardDisk.szIdeHardDiskImage,
			           argv[i], sizeof(ConfigureParams.HardDisk.szIdeHardDiskImage));
			ConfigureParams.HardDisk.bUseIdeHardDiskImage = TRUE;
			bLoadAutoSave = FALSE;
			break;

		case OPT_HARDDRIVE:
			i += 1;
			Opt_StrCpy(OPT_HARDDRIVE, FALSE, ConfigureParams.HardDisk.szHardDiskDirectories[0],
			           argv[i], sizeof(ConfigureParams.HardDisk.szHardDiskDirectories[0]));
			ConfigureParams.HardDisk.bUseHardDiskDirectories = TRUE;
			ConfigureParams.HardDisk.bBootFromHardDisk = TRUE;
			bLoadAutoSave = FALSE;
			break;
			
		case OPT_TOS:
			i += 1;
			Opt_StrCpy(OPT_TOS, TRUE, ConfigureParams.Rom.szTosImageFileName,
			           argv[i], sizeof(ConfigureParams.Rom.szTosImageFileName));
			bLoadAutoSave = FALSE;
			break;
      
		case OPT_CARTRIDGE:
			i += 1;
			Opt_StrCpy(OPT_CARTRIDGE, TRUE, ConfigureParams.Rom.szCartridgeImageFileName,
			           argv[i], sizeof(ConfigureParams.Rom.szCartridgeImageFileName));
			bLoadAutoSave = FALSE;
			break;
			
		case OPT_CPULEVEL:
			/* UAE core uses cpu_level variable */
			ncpu = atoi(argv[++i]);
			if(ncpu < 0 || ncpu > 4)
			{
				fprintf(stderr, "CPU level %d is invalid (valid: 0-4), set to 0.\n", ncpu);
				ncpu = 0;
			}
			ConfigureParams.System.nCpuLevel = ncpu;
			bLoadAutoSave = FALSE;
			break;
			
		case OPT_COMPATIBLE:
			ConfigureParams.System.bCompatibleCpu = TRUE;
			bLoadAutoSave = FALSE;
			break;
			
		case OPT_BLITTER:
			ConfigureParams.System.bBlitter = TRUE;
			bLoadAutoSave = FALSE;
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
				Opt_ShowExit(OPT_NONE, argv[i], "DSP type 'emu' support not compiled in");
#endif
			}
			else
			{
				Opt_ShowExit(OPT_NONE, argv[i], "Unknown DSP type");
			}
			bLoadAutoSave = FALSE;
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
				Opt_ShowExit(OPT_NONE, argv[i], "Unsupported VDI bit-depth");
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
			
		case OPT_SLOWFDC:
			ConfigureParams.System.bSlowFDC = TRUE;
			bLoadAutoSave = FALSE;
			break;
			
		case OPT_MEMSIZE:
			ConfigureParams.Memory.nMemorySize = atoi(argv[++i]);
			if (ConfigureParams.Memory.nMemorySize < 0 ||
			    ConfigureParams.Memory.nMemorySize > 14)
			{
				fprintf(stderr, "Memory size %d is invalid (valid: 0-14MB), set to 1.\n",
					ConfigureParams.Memory.nMemorySize);
				ConfigureParams.Memory.nMemorySize = 1;
			}
			bLoadAutoSave = FALSE;
			break;
			
		case OPT_CONFIGFILE:
			i += 1;
			Opt_StrCpy(OPT_CONFIGFILE, TRUE, sConfigFileName,
			           argv[i], sizeof(sConfigFileName));
			Configuration_Load(NULL);
			bLoadAutoSave = ConfigureParams.Memory.bAutoSave;
			break;

		case OPT_KEYMAPFILE:
			i += 1;
			Opt_StrCpy(OPT_KEYMAPFILE, TRUE, ConfigureParams.Keyboard.szMappingFileName,
			           argv[i], sizeof(ConfigureParams.Keyboard.szMappingFileName));
			ConfigureParams.Keyboard.nKeymapType = KEYMAP_LOADED;
			break;
			
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
				Opt_ShowExit(OPT_NONE, argv[i], "Unknown machine type");
			}
			bLoadAutoSave = FALSE;
			break;
			
		case OPT_TRACE:
			i += 1;
			if (ParseTraceOptions(argv[i]) == 0)
			{
				Opt_ShowExit(OPT_NONE, argv[i], "Error parsing trace options (use --trace help for available list)!\n");
			}
			break;

		case OPT_MEMSTATE:
			i += 1;
			Opt_StrCpy(OPT_MEMSTATE, TRUE, ConfigureParams.Memory.szMemoryCaptureFileName,
			           argv[i], sizeof(ConfigureParams.Memory.szMemoryCaptureFileName));
			bLoadMemorySave = TRUE;
			bLoadAutoSave = FALSE;
			break;

		default:
			Opt_ShowExit(OPT_NONE, argv[i], "Program didn't handle documented option");
		}
	}
}
