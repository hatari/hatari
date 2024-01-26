/*
  Hatari - options.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Functions for showing and parsing all of Hatari's command line options.

  To add a new option:
  - Add option ID to the enum
  - Add the option information to HatariOptions[]
  - Add required actions for that ID to switch in Opt_ParseParameters()
*/
const char Options_fileid[] = "Hatari options.c";

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "main.h"
#include "version.h"
#include "options.h"
#include "configuration.h"
#include "console.h"
#include "control.h"
#include "debugui.h"
#include "file.h"
#include "floppy.h"
#include "fdc.h"
#include "screen.h"
#include "statusbar.h"
#include "sound.h"
#include "video.h"
#include "vdi.h"
#include "joy.h"
#include "log.h"
#include "inffile.h"
#include "paths.h"
#include "avi_record.h"
#include "hatari-glue.h"
#include "68kDisass.h"
#include "xbios.h"
#include "stMemory.h"
#include "tos.h"
#include "lilo.h"
#include "screenSnapShot.h"


bool bLoadAutoSave;        /* Load autosave memory snapshot at startup */
bool bLoadMemorySave;      /* Load memory snapshot provided via option at startup */
bool AviRecordOnStartup;   /* Start avi recording at startup */
bool BenchmarkMode;	   /* Start in benchmark mode (try to run at maximum emulation */
			   /* speed allowed by the CPU). Disable audio/video for best results */

static bool bBiosIntercept;

/*  List of supported options. */
enum {
	OPT_HEADER,	/* options section header */

	OPT_HELP,		/* general options */
	OPT_VERSION,
	OPT_CONFIRMQUIT,
	OPT_CONFIGFILE,
	OPT_KEYMAPFILE,
	OPT_COUNTRY_CODE,
	OPT_KBD_LAYOUT,
	OPT_LANGUAGE,
	OPT_FASTFORWARD,
	OPT_AUTOSTART,

	OPT_MONO,		/* common display options */
	OPT_MONITOR,
	OPT_TOS_RESOLUTION,
	OPT_FULLSCREEN,
	OPT_WINDOW,
	OPT_GRAB,
	OPT_RESIZABLE,
	OPT_FRAMESKIPS,
	OPT_SLOWDOWN,
	OPT_MOUSE_WARP,
	OPT_STATUSBAR,
	OPT_DRIVE_LED,
	OPT_MAXWIDTH,
	OPT_MAXHEIGHT,
	OPT_ZOOM,
	OPT_DISABLE_VIDEO,

	OPT_BORDERS,		/* ST/STE display options */
	OPT_SPEC512,
	OPT_VIDEO_TIMING,

	OPT_RESOLUTION,		/* TT/Falcon display options */
	OPT_FORCE_MAX,
	OPT_ASPECT,

	OPT_VDI,		/* VDI options */
	OPT_VDI_PLANES,
	OPT_VDI_WIDTH,
	OPT_VDI_HEIGHT,

	OPT_SCREEN_CROP,        /* screen capture options */
	OPT_AVIRECORD,
	OPT_AVIRECORD_VCODEC,
	OPT_AVI_PNG_LEVEL,
	OPT_AVIRECORD_FPS,
	OPT_AVIRECORD_FILE,
	OPT_SCRSHOT_DIR,
	OPT_SCRSHOT_FORMAT,

	OPT_JOYSTICK,		/* device options */
	OPT_JOYSTICK0,
	OPT_JOYSTICK1,
	OPT_JOYSTICK2,
	OPT_JOYSTICK3,
	OPT_JOYSTICK4,
	OPT_JOYSTICK5,
	OPT_PRINTER,
#ifdef HAVE_PORTMIDI
	OPT_MIDI,
#else
	OPT_MIDI_IN,
	OPT_MIDI_OUT,
#endif
	OPT_RS232_IN,
	OPT_RS232_OUT,
	OPT_SCCA_IN,
	OPT_SCCA_OUT,
	OPT_SCCA_LAN_IN,
	OPT_SCCA_LAN_OUT,
	OPT_SCCB_IN,
	OPT_SCCB_OUT,

	OPT_DRIVEA,		/* floppy options */
	OPT_DRIVEB,
	OPT_DRIVEA_HEADS,
	OPT_DRIVEB_HEADS,
	OPT_DISKA,
	OPT_DISKB,
	OPT_FASTFLOPPY,
	OPT_WRITEPROT_FLOPPY,

	OPT_HARDDRIVE,		/* HD options */
	OPT_WRITEPROT_HD,
	OPT_GEMDOS_CASE,
	OPT_GEMDOS_HOSTTIME,
	OPT_GEMDOS_CONVERT,
	OPT_GEMDOS_DRIVE,
	OPT_ACSIHDIMAGE,
	OPT_SCSIHDIMAGE,
	OPT_IDEMASTERHDIMAGE,
	OPT_IDESLAVEHDIMAGE,
	OPT_IDEBYTESWAP,

	OPT_MEMSIZE,		/* memory options */
	OPT_TT_RAM,
	OPT_MEMSTATE,

	OPT_TOS,		/* ROM options */
	OPT_PATCHTOS,
	OPT_CARTRIDGE,

	OPT_CPULEVEL,		/* CPU options */
	OPT_CPUCLOCK,
	OPT_COMPATIBLE,
	OPT_CPU_CYCLE_EXACT,
	OPT_CPU_ADDR24,
	OPT_FPU_TYPE,
/*	OPT_FPU_JIT_COMPAT, */
	OPT_FPU_SOFTFLOAT,
	OPT_MMU,

	OPT_MACHINE,		/* system options */
	OPT_BLITTER,
	OPT_DSP,
	OPT_VME,
	OPT_RTC_YEAR,
	OPT_TIMERD,
	OPT_FASTBOOT,

	OPT_MICROPHONE,		/* sound options */
	OPT_SOUND,
	OPT_SOUNDBUFFERSIZE,
	OPT_SOUNDSYNC,
	OPT_YM_MIXING,

#ifdef WIN32
	OPT_WINCON,		/* debug options */
#endif
	OPT_DEBUG,
	OPT_EXCEPTIONS,
	OPT_LILO,
	OPT_BIOSINTERCEPT,
	OPT_CONOUT,
	OPT_DISASM,
	OPT_NATFEATS,
	OPT_TRACE,
	OPT_TRACEFILE,
	OPT_MSG_REPEAT,
	OPT_PARSE,
	OPT_SAVECONFIG,
	OPT_CONTROLSOCKET,
	OPT_CMDFIFO,
	OPT_LOGFILE,
	OPT_LOGLEVEL,
	OPT_ALERTLEVEL,
	OPT_RUNVBLS,
	OPT_BENCHMARK,
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
	  "<file>", "Read (additional) configuration values from <file>" },
	{ OPT_KEYMAPFILE, "-k", "--keymap",
	  "<file>", "Read (additional) keyboard mappings from <file>" },
	{ OPT_COUNTRY_CODE, NULL, "--country",
	  "<x>", "Set country code for multi-code EmuTOS ROM" },
	{ OPT_KBD_LAYOUT, NULL, "--layout",
	  "<x>", "Set (TT/Falcon) NVRAM keyboard layout" },
	{ OPT_LANGUAGE, NULL, "--language",
	  "<x>", "Set (TT/Falcon) NVRAM language" },
	{ OPT_FASTFORWARD, NULL, "--fast-forward",
	  "<bool>", "Help skipping stuff on fast machine" },
	{ OPT_AUTOSTART, NULL, "--auto",
	  "<x>", "Atari program autostarting with Atari path" },

	{ OPT_HEADER, NULL, NULL, NULL, "Common display" },
	{ OPT_MONO,      "-m", "--mono",
	  NULL, "Start in monochrome mode instead of color" },
	{ OPT_MONITOR,      NULL, "--monitor",
	  "<x>", "Select monitor type (x = mono/rgb/vga/tv)" },
	{ OPT_TOS_RESOLUTION,   NULL, "--tos-res",
	  "<x>", "TOS resolution (x = low/med/high/ttlow/ttmed)" },
	{ OPT_FULLSCREEN,"-f", "--fullscreen",
	  NULL, "Start emulator in fullscreen mode" },
	{ OPT_WINDOW,    "-w", "--window",
	  NULL, "Start emulator in windowed mode" },
	{ OPT_GRAB, NULL, "--grab",
	  NULL, "Grab mouse (also) in windowed mode" },
	{ OPT_RESIZABLE,    NULL, "--resizable",
	  "<bool>", "Allow window resizing" },
	{ OPT_FRAMESKIPS, NULL, "--frameskips",
	  "<x>", "Skip <x> frames after each shown frame (0=off, >4=auto/max)" },
	{ OPT_SLOWDOWN, NULL, "--slowdown",
	  "<x>", "VBL wait time multiplier (1-30, default 1)" },
	{ OPT_MOUSE_WARP, NULL, "--mousewarp",
	  "<bool>", "Center host mouse on reset & resolution changes" },
	{ OPT_STATUSBAR, NULL, "--statusbar",
	  "<bool>", "Show statusbar (floppy leds etc)" },
	{ OPT_DRIVE_LED,   NULL, "--drive-led",
	  "<bool>", "Show overlay drive led when statusbar isn't shown" },
	{ OPT_MAXWIDTH, NULL, "--max-width",
	  "<x>", "Maximum Hatari screen width before scaling" },
	{ OPT_MAXHEIGHT, NULL, "--max-height",
	  "<x>", "Maximum Hatari screen height before scaling" },
	{ OPT_ZOOM, "-z", "--zoom",
	  "<x>", "Hatari screen/window scaling factor (1.0 - 8.0)" },
	{ OPT_DISABLE_VIDEO,   NULL, "--disable-video",
	  "<bool>", "Run emulation without displaying video (audio only)" },

	{ OPT_HEADER, NULL, NULL, NULL, "ST/STE specific display" },
	{ OPT_BORDERS, NULL, "--borders",
	  "<bool>", "Show screen borders (for overscan demos etc)" },
	{ OPT_SPEC512, NULL, "--spec512",
	  "<x>", "Spec512 palette threshold (0 <= x <= 512, 0=disable)" },
	{ OPT_VIDEO_TIMING,   NULL, "--video-timing",
	  "<x>", "Wakeup State for MMU/GLUE (x=ws1/ws2/ws3/ws4/random, default ws3)" },

	{ OPT_HEADER, NULL, NULL, NULL, "TT/Falcon specific display" },
	{ OPT_RESOLUTION, NULL, "--desktop",
	  "<bool>", "Keep desktop resolution on fullscreen" },
	{ OPT_FORCE_MAX, NULL, "--force-max",
	  "<bool>", "Resolution fixed to given max values" },
	{ OPT_ASPECT, NULL, "--aspect",
	  "<bool>", "Monitor aspect ratio correction" },

	{ OPT_HEADER, NULL, NULL, NULL, "VDI" },
	{ OPT_VDI,	NULL, "--vdi",
	  "<bool>", "Whether to use VDI screen mode" },
	{ OPT_VDI_PLANES,NULL, "--vdi-planes",
	  "<x>", "VDI mode bit-depth (x = 1/2/4)" },
	{ OPT_VDI_WIDTH,     NULL, "--vdi-width",
	  "<w>", "VDI mode width (320 < w <= 2048)" },
	{ OPT_VDI_HEIGHT,     NULL, "--vdi-height",
	  "<h>", "VDI mode height (200 < h <= 1280)" },

	{ OPT_HEADER, NULL, NULL, NULL, "Screen capture" },
	{ OPT_SCREEN_CROP, NULL, "--crop",
	  "<bool>", "Remove statusbar from screen capture" },
	{ OPT_AVIRECORD, NULL, "--avirecord",
	  NULL, "Start AVI recording" },
	{ OPT_AVIRECORD_VCODEC, NULL, "--avi-vcodec",
	  "<x>", "Select AVI video codec (x = bmp/png)" },
	{ OPT_AVI_PNG_LEVEL, NULL, "--png-level",
	  "<x>", "Select AVI PNG compression level (x = 0-9)" },
	{ OPT_AVIRECORD_FPS, NULL, "--avi-fps",
	  "<x>", "Force AVI frame rate (x = 50/60/71/...)" },
	{ OPT_AVIRECORD_FILE, NULL, "--avi-file",
	  "<file>", "Use <file> to record AVI" },
	{ OPT_SCRSHOT_DIR, NULL, "--screenshot-dir",
	  "<dir>", "Save screenshots in the directory <dir>" },
	{ OPT_SCRSHOT_FORMAT, NULL, "--screenshot-format",
	  "<x>", "Select file format (x = bmp/png/neo/ximg)" },

	{ OPT_HEADER, NULL, NULL, NULL, "Devices" },
	{ OPT_JOYSTICK,  "-j", "--joystick",
	  "<port>", "Emulate joystick with cursor keys in given port (0-5)" },
	/* these have to be exactly the same as I'm relying compiler giving
	 * them the same same string pointer when strings are identical
	 * (Opt_ShowHelpSection() skips successive options with same help
	 * pointer).
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
#ifdef HAVE_PORTMIDI
	{ OPT_MIDI,   NULL, "--midi",
	  "<bool>", "Whether to use MIDI (with PortMidi devices)" },
#else
	{ OPT_MIDI_IN,   NULL, "--midi-in",
	  "<file>", "Enable MIDI and use <file> as the input device" },
	{ OPT_MIDI_OUT,  NULL, "--midi-out",
	  "<file>", "Enable MIDI and use <file> as the output device" },
#endif
	{ OPT_RS232_IN,  NULL, "--rs232-in",
	  "<file>", "Enable serial port and use <file> as the input device" },
	{ OPT_RS232_OUT, NULL, "--rs232-out",
	  "<file>", "Enable serial port and use <file> as the output device" },
	{ OPT_SCCA_IN, NULL, "--scc-a-in",
	  "<file>", "Enable SCC channel A and use <file> as the input" },
	{ OPT_SCCA_OUT, NULL, "--scc-a-out",
	  "<file>", "Enable SCC channel A and use <file> as the output" },
	{ OPT_SCCA_LAN_IN, NULL, "--scc-a-lan-in",
	  "<file>", "Enable LAN on SCC channel A and use <file> as the input" },
	{ OPT_SCCA_LAN_OUT, NULL, "--scc-a-lan-out",
	  "<file>", "Enable LAN on SCC channel A and use <file> as the output" },
	{ OPT_SCCB_IN, NULL, "--scc-b-in",
	  "<file>", "Enable SCC channel B and use <file> as the input" },
	{ OPT_SCCB_OUT, NULL, "--scc-b-out",
	  "<file>", "Enable SCC channel B and use <file> as the output" },

	{ OPT_HEADER, NULL, NULL, NULL, "Floppy drive" },
	{ OPT_DRIVEA, NULL, "--drive-a",
	  "<bool>", "Enable/disable drive A (default is on)" },
	{ OPT_DRIVEB, NULL, "--drive-b",
	  "<bool>", "Enable/disable drive B (default is on)" },
	{ OPT_DRIVEA_HEADS, NULL, "--drive-a-heads",
	  "<x>", "Set number of heads for drive A (1=single sided, 2=double sided)" },
	{ OPT_DRIVEB_HEADS, NULL, "--drive-b-heads",
	  "<x>", "Set number of heads for drive B (1=single sided, 2=double sided)" },
	{ OPT_DISKA, NULL, "--disk-a",
	  "<file>", "Set disk image for floppy drive A" },
	{ OPT_DISKB, NULL, "--disk-b",
	  "<file>", "Set disk image for floppy drive B" },
	{ OPT_FASTFLOPPY,   NULL, "--fastfdc",
	  "<bool>", "Speed up floppy disk access emulation (can break some programs)" },
	{ OPT_WRITEPROT_FLOPPY, NULL, "--protect-floppy",
	  "<x>", "Write protect floppy image contents (on/off/auto)" },

	{ OPT_HEADER, NULL, NULL, NULL, "Hard drive" },
	{ OPT_HARDDRIVE, "-d", "--harddrive",
	  "<dir>", "Emulate harddrive partition(s) with <dir> contents" },
	{ OPT_WRITEPROT_HD, NULL, "--protect-hd",
	  "<x>", "Write protect harddrive <dir> contents (on/off/auto)" },
	{ OPT_GEMDOS_CASE, NULL, "--gemdos-case",
	  "<x>", "Forcibly up/lowercase new GEMDOS dir/filenames (off/upper/lower)" },
	{ OPT_GEMDOS_HOSTTIME, NULL, "--gemdos-time",
	  "<x>", "Which timestamps to use for GEMDOS files (atari/host)" },
	{ OPT_GEMDOS_CONVERT, NULL, "--gemdos-conv",
	  "<bool>", "Atari GEMDOS <-> host (UTF-8) file name conversion" },
	{ OPT_GEMDOS_DRIVE, NULL, "--gemdos-drive",
	  "<drive>", "Assign GEMDOS HD <dir> to drive letter <drive> (C-Z, skip)" },
	{ OPT_ACSIHDIMAGE,   NULL, "--acsi",
	  "<id>=<file>", "Emulate an ACSI harddrive (0-7) with an image <file>" },
	{ OPT_SCSIHDIMAGE,   NULL, "--scsi",
	  "<id>=<file>", "Emulate a SCSI harddrive (0-7) with an image <file>" },
	{ OPT_IDEMASTERHDIMAGE,   NULL, "--ide-master",
	  "<file>", "Emulate an IDE 0 (master) harddrive with an image <file>" },
	{ OPT_IDESLAVEHDIMAGE,   NULL, "--ide-slave",
	  "<file>", "Emulate an IDE 1 (slave) harddrive with an image <file>" },
	{ OPT_IDEBYTESWAP,   NULL, "--ide-swap",
	  "<id>=<x>", "Set IDE (0/1) byte-swap option (off/on/auto)" },

	{ OPT_HEADER, NULL, NULL, NULL, "Memory" },
	{ OPT_MEMSIZE,   "-s", "--memsize",
	  "<x>", "ST RAM size (x = size in MiB from 0 to 14, 0 = 512KiB ; else size in KiB)" },
	{ OPT_TT_RAM,   NULL, "--ttram",
	  "<x>", "TT RAM size (x = size in MiB from 0 to 1024, in steps of 4)" },
	{ OPT_MEMSTATE,   NULL, "--memstate",
	  "<file>", "Load memory snap-shot <file>" },

	{ OPT_HEADER, NULL, NULL, NULL, "ROM" },
	{ OPT_TOS,       "-t", "--tos",
	  "<file>", "Use TOS image <file>" },
	{ OPT_PATCHTOS, NULL, "--patch-tos",
	  "<bool>", "Apply TOS patches (experts only, leave it enabled!)" },
	{ OPT_CARTRIDGE, NULL, "--cartridge",
	  "<file>", "Use ROM cartridge image <file>" },

	{ OPT_HEADER, NULL, NULL, NULL, "CPU/FPU/bus" },
	{ OPT_CPULEVEL,  NULL, "--cpulevel",
	  "<x>", "Set the CPU type (x => 680x0) (EmuTOS/TOS 2.06 only!)" },
	{ OPT_CPUCLOCK,  NULL, "--cpuclock",
	  "<x>", "Set the CPU clock (x = 8/16/32)" },
	{ OPT_COMPATIBLE, NULL, "--compatible",
	  "<bool>", "Use a more compatible (but slower) prefetch mode for CPU" },
	{ OPT_CPU_CYCLE_EXACT, NULL, "--cpu-exact",
	  "<bool>", "Use cycle exact CPU emulation" },
	{ OPT_CPU_ADDR24, NULL, "--addr24",
	  "<bool>", "Use 24-bit instead of 32-bit addressing mode" },
	{ OPT_FPU_TYPE, NULL, "--fpu",
	  "<x>", "FPU type (x=none/68881/68882/internal)" },
	/*{ OPT_FPU_JIT_COMPAT, NULL, "--fpu-compatible",
	  "<bool>", "Use more compatible, but slower FPU JIT emulation" },*/
	{ OPT_FPU_SOFTFLOAT, NULL, "--fpu-softfloat",
	  "<bool>", "Use full software FPU emulation" },
	{ OPT_MMU, NULL, "--mmu",
	  "<bool>", "Use MMU emulation" },

	{ OPT_HEADER, NULL, NULL, NULL, "Misc system" },
	{ OPT_MACHINE,   NULL, "--machine",
	  "<x>", "Select machine type (x = st/megast/ste/megaste/tt/falcon)" },
	{ OPT_BLITTER,   NULL, "--blitter",
	  "<bool>", "Use blitter emulation (ST only)" },
	{ OPT_DSP,       NULL, "--dsp",
	  "<x>", "DSP emulation (x = none/dummy/emu, Falcon only)" },
	{ OPT_VME,	NULL, "--vme",
	  "<x>", "VME mode (x = none/dummy, MegaSTE/TT only)" },
	{ OPT_RTC_YEAR,   NULL, "--rtc-year",
	  "<x>", "Set initial year for RTC (0, 1980 <= x < 2080)" },
	{ OPT_TIMERD,    NULL, "--timer-d",
	  "<bool>", "Patch Timer-D (about doubles ST emulation speed)" },
	{ OPT_FASTBOOT, NULL, "--fast-boot",
	  "<bool>", "Patch TOS and memvalid system variables for faster boot" },

	{ OPT_HEADER, NULL, NULL, NULL, "Sound" },
	{ OPT_MICROPHONE,   NULL, "--mic",
	  "<bool>", "Enable/disable (Falcon only) microphone" },
	{ OPT_SOUND,   NULL, "--sound",
	  "<x>", "Sound frequency (x=off/6000-50066, off=fastest)" },
	{ OPT_SOUNDBUFFERSIZE,   NULL, "--sound-buffer-size",
	  "<x>", "Sound buffer size in ms (x=0/10-100, 0=SDL default)" },
	{ OPT_SOUNDSYNC,   NULL, "--sound-sync",
	  "<bool>", "Sound synchronized emulation (on|off, off=default)" },
	{ OPT_YM_MIXING,   NULL, "--ym-mixing",
	  "<x>", "YM sound mixing method (x=linear/table/model)" },

	{ OPT_HEADER, NULL, NULL, NULL, "Debug" },
#ifdef WIN32
	{ OPT_WINCON, "-W", "--wincon",
	  NULL, "Open console window (Windows only)" },
#endif
	{ OPT_DEBUG,     "-D", "--debug",
	  NULL, "Toggle whether CPU exceptions invoke debugger" },
	{ OPT_EXCEPTIONS, NULL, "--debug-except",
	  "<flags>", "Exceptions invoking debugger, see '--debug-except help'" },
	{ OPT_LILO, NULL, "--lilo", "<x>", "Boot Linux (see manual page)" },
	{ OPT_BIOSINTERCEPT, NULL, "--bios-intercept",
	  "<bool>", "Enable/disable XBIOS command parsing support" },
	{ OPT_CONOUT,   NULL, "--conout",
	  "<device>", "Show console output (0-7, 2=VT-52 terminal)" },
	{ OPT_DISASM,   NULL, "--disasm",
	  "<x>", "Set disassembly options (help/uae/ext/<bitmask>)" },
	{ OPT_NATFEATS, NULL, "--natfeats",
	  "<bool>", "Whether Native Features support is enabled" },
	{ OPT_TRACE,   NULL, "--trace",
	  "<flags>", "Activate emulation tracing, see '--trace help'" },
	{ OPT_TRACEFILE, NULL, "--trace-file",
	  "<file>", "Save trace output to <file> (default=stderr)" },
	{ OPT_MSG_REPEAT, NULL, "--msg-repeat",
	  NULL, "Toggle log/trace message repeats (default=suppress)" },
	{ OPT_PARSE, NULL, "--parse",
	  "<file>", "Parse/execute debugger commands from <file>" },
	{ OPT_SAVECONFIG, NULL, "--saveconfig",
	  NULL, "Save current Hatari configuration and exit" },
#if HAVE_UNIX_DOMAIN_SOCKETS
	{ OPT_CONTROLSOCKET, NULL, "--control-socket",
	  "<file>", "Hatari connects to given socket for commands" },
	{ OPT_CMDFIFO, NULL, "--cmd-fifo",
	  "<file>", "Hatari creates & reads commands from given fifo" },
#endif
	{ OPT_LOGFILE, NULL, "--log-file",
	  "<file>", "Save log output to <file> (default=stderr)" },
	{ OPT_LOGLEVEL, NULL, "--log-level",
	  "<x>", "Log output level (x=debug/todo/info/warn/error/fatal)" },
	{ OPT_ALERTLEVEL, NULL, "--alert-level",
	  "<x>", "Show dialog for log messages above given level" },
	{ OPT_RUNVBLS, NULL, "--run-vbls",
	  "<x>", "Exit after x VBLs" },
	{ OPT_BENCHMARK, NULL, "--benchmark",
	  NULL, "Start in benchmark mode (use with --run-vbls)" },

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
	printf("Usage:\n hatari [options] [directory|disk image|Atari program]\n");

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
 * Return false if error string was given, otherwise true
 */
bool Opt_ShowError(unsigned int optid, const char *value, const char *error)
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
				fprintf(stderr,
					"\nError while parsing argument \"%s\" for option \"%s\":\n"
					"  %s\n", value, opt->str, error);
			}
			else
			{
				fprintf(stderr, "\nError (%s): %s\n", opt->str, error);
			}
			fprintf(stderr, "\nOption usage:\n");
			Opt_ShowOption(opt, 0);
		}
		return false;
	}
	return true;
}


/**
 * Return given value after constraining it within "min" and "max" values
 * and making it evenly divisible by "align"
 */
int Opt_ValueAlignMinMax(int value, int align, int min, int max)
{
	if (value > max)
	{
		/* align down */
		return (max/align)*align;
	}
	if (value < min)
	{
		/* align up */
		min += align-1;
		return (min/align)*align;
	}
	return (value/align)*align;
}


/**
 * If 'conf' given, set it:
 * - true if given option 'arg' is y/yes/on/true/1
 * - false if given option 'arg' is n/no/off/false/0
 * Return false for any other value, otherwise true
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
		*str++ = tolower((unsigned char)*arg++);
	}
	for (bool_str = enablers; *bool_str; bool_str++)
	{
		if (strcmp(input, *bool_str) == 0)
		{
			free(input);
			if (conf)
			{
				*conf = true;
			}
			return true;
		}
	}
	for (bool_str = disablers; *bool_str; bool_str++)
	{
		if (strcmp(input, *bool_str) == 0)
		{
			free(input);
			if (conf)
			{
				*conf = false;
			}
			return true;
		}
	}
	free(input);
	return Opt_ShowError(optid, orig, "Not a <bool> value");
}

/**
 * If 'conf' given, set it to parsed country code value
 * Return false for any other value, otherwise true
 */
static bool Opt_CountryCode(const char *arg, int optid, int *conf)
{
	int val = TOS_ParseCountryCode(arg);
	if (val != TOS_LANG_UNKNOWN)
	{
		*conf = val;
		return true;
	}
	Opt_ShowError(optid, arg, "Invalid value");
	TOS_ShowCountryCodes();
	return false;

}

/**
 * checks str argument against options of type "--option<digit>".
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
	if (strncmp(opt->str, str, offset) != 0)
	{
		return OPT_CONTINUE;
	}
	digit = str[offset] - '0';
	if (digit < 0 || digit > 9)
	{
		return OPT_CONTINUE;
	}
	if (str[offset+1])
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
static int Opt_WhichOption(int argc, const char * const argv[], int idx)
{
	const opt_t *opt;
	const char *str = argv[idx];
	int id;

	for (opt = HatariOptions; opt->id != OPT_ERROR; opt++)
	{
		/* exact option name matches? */
		if (!((opt->str && !strcmp(str, opt->str)) ||
		      (opt->chr && !strcmp(str, opt->chr))))
		{
			/* no, maybe name<digit> matches? */
			id = Opt_CheckBracketValue(opt, str);
			if (id == OPT_CONTINUE)
			{
				continue;
			}
			if (id == OPT_ERROR)
			{
				break;
			}
		}
		/* matched, check args */
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
	Opt_ShowError(OPT_ERROR, argv[idx], "Unrecognized option");
	return OPT_ERROR;
}


/**
 * If 'checkexits' is true, assume 'src' is a file and check whether it
 * exists before copying 'src' to 'dst'. Otherwise just copy option src
 * string to dst.
 * If a pointer to (bool) 'option' is given, set that option to true.
 * - However, if src is "none", leave dst unmodified & set option to false.
 *   ("none" is used to disable options related to file arguments)
 * Return false if there were errors, otherwise true
 */
static bool Opt_StrCpy(int optid, bool checkexist, char *dst, const char *src, size_t dstlen, bool *option)
{
	if (option)
	{
		*option = false;
		if(strcasecmp(src, "none") == 0)
		{
			return true;
		}
	}
	if (strlen(src) >= dstlen)
	{
		return Opt_ShowError(optid, src, "File name too long!");
	}
	if (checkexist && !File_Exists(src))
	{
		return Opt_ShowError(optid, src, "Given file doesn't exist or permissions prevent access to it!");
	}
	if (option)
	{
		*option = true;
	}
	strcpy(dst, src);
	return true;
}


/**
 * Do final validation for the earlier + parsed options
 *
 * Return false if they fail validation.
 */
static bool Opt_ValidateOptions(void)
{
	const char *err, *val;
	int opt_id;

	if ((opt_id = INF_ValidateAutoStart(&val, &err)))
	{
		return Opt_ShowError(opt_id, val, err);
	}
	return true;
}


/**
 * Return true if given path points to an Atari program, false otherwise.
 */
bool Opt_IsAtariProgram(const char *path)
{
	bool ret = false;
	uint8_t test[2];
	FILE *fp;

	if (File_Exists(path) && (fp = fopen(path, "rb")))
	{
		/* file starts with GEMDOS magic? */
		if (fread(test, 1, 2, fp) == 2 &&
		    test[0] == 0x60 && test[1] == 0x1A)
		{
			ret = true;
		}
		fclose(fp);
	}
	return ret;
}

/**
 * Handle last (non-option) argument.  It can be a path or filename.
 * Filename can be a disk image or Atari program.
 * Return false if it's none of these.
 */
static bool Opt_HandleArgument(const char *path)
{
	char *dir = NULL;

	/* Atari program? */
	if (Opt_IsAtariProgram(path))
	{
		const char *prgname = strrchr(path, PATHSEP);
		if (prgname)
		{
			dir = strdup(path);
			dir[prgname-path] = '\0';
			prgname++;
		}
		else
		{
			dir = strdup(Paths_GetWorkingDir());
			prgname = path;
		}
		Log_Printf(LOG_DEBUG, "ARG = autostart program: %s\n", prgname);

		/* after above, dir should point to valid dir,
		 * then make sure that given program from that
		 * dir will be started.
		 */
		if (bUseTos)
		{
			INF_SetAutoStart(prgname, OPT_AUTOSTART);
		}
		else
		{
			TOS_SetTestPrgName(path);
		}
	}
	if (dir)
	{
		path = dir;
	}

	/* GEMDOS HDD directory (as argument, or for the Atari program)? */
	if (File_DirExists(path))
	{
		Log_Printf(LOG_DEBUG, "ARG = GEMDOS HD dir: %s\n", path);
		if (Opt_StrCpy(OPT_HARDDRIVE, false, ConfigureParams.HardDisk.szHardDiskDirectories[0],
			       path, sizeof(ConfigureParams.HardDisk.szHardDiskDirectories[0]),
			       &ConfigureParams.HardDisk.bUseHardDiskDirectories)
		    && ConfigureParams.HardDisk.bUseHardDiskDirectories)
		{
			ConfigureParams.HardDisk.bBootFromHardDisk = true;
		}
		bLoadAutoSave = false;
		if (dir)
		{
			free(dir);
		}
		return true;
	}
	else
	{
		if (dir)
		{
			/* if dir is set, it should be valid... */
			Log_Printf(LOG_ERROR, "Given atari program path '%s' doesn't exist (anymore?)!\n", dir);
			free(dir);
			exit(1);
		}
	}

	/* disk image? */
	if (Floppy_SetDiskFileName(0, path, NULL))
	{
		Log_Printf(LOG_DEBUG, "ARG = floppy image: %s\n", path);
		ConfigureParams.HardDisk.bBootFromHardDisk = false;
		bLoadAutoSave = false;
		return true;
	}

	return Opt_ShowError(OPT_ERROR, path, "Not a disk image, Atari program or directory");
}

/**
 * parse all Hatari command line options and set Hatari state accordingly.
 * Returns true if everything was OK, false otherwise.
 */
bool Opt_ParseParameters(int argc, const char * const argv[])
{
	int ncpu, skips, planes, cpuclock, threshold, memsize;
	int dev, port, freq, temp, drive, year;
	const char *errstr, *str;
	int i, ok = true;
	float zoom;
	int val;

	/* Defaults for loading initial memory snap-shots */
	bLoadMemorySave = false;
	bLoadAutoSave = ConfigureParams.Memory.bAutoSave;

	for(i = 1; i < argc; i++)
	{
		/* last argument can be a non-option */
		if (argv[i][0] != '-' && i+1 == argc)
			return Opt_HandleArgument(argv[i]) && Opt_ValidateOptions();

		/* WhichOption() checks also that there is an argument,
		 * so we don't need to check that below
		 */
		switch(Opt_WhichOption(argc, argv, i))
		{

			/* general options */
		case OPT_HELP:
			Opt_ShowHelp();
			return false;

		case OPT_VERSION:
			Opt_ShowVersion();
			return false;

		case OPT_CONFIRMQUIT:
			ok = Opt_Bool(argv[++i], OPT_CONFIRMQUIT, &ConfigureParams.Log.bConfirmQuit);
			break;

		case OPT_FASTFORWARD:
			ok = Opt_Bool(argv[++i], OPT_FASTFORWARD, &ConfigureParams.System.bFastForward);
			break;

		case OPT_AUTOSTART:
			if (!(ok = INF_SetAutoStart(argv[++i], OPT_AUTOSTART)))
			{
				return Opt_ShowError(OPT_AUTOSTART, argv[i], "Invalid drive and/or path specified for autostart program");
			}
			break;

		case OPT_CONFIGFILE:
			i += 1;
			/* true -> file needs to exist */
			ok = Opt_StrCpy(OPT_CONFIGFILE, true, sConfigFileName,
					argv[i], sizeof(sConfigFileName), NULL);
			if (ok)
			{
				Configuration_Load(NULL);
				bLoadAutoSave = ConfigureParams.Memory.bAutoSave;
			}
			break;

			/* common display options */
		case OPT_MONO:
			ConfigureParams.Screen.nMonitorType = MONITOR_TYPE_MONO;
			bLoadAutoSave = false;
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
			bLoadAutoSave = false;
			break;

		case OPT_TOS_RESOLUTION:
			i += 1;
			if (!INF_SetResolution(argv[i], OPT_TOS_RESOLUTION))
			{
				return Opt_ShowError(OPT_TOS_RESOLUTION, argv[i], "Invalid resolution");
			}
			break;

		case OPT_FULLSCREEN:
			ConfigureParams.Screen.bFullScreen = true;
			break;

		case OPT_WINDOW:
			ConfigureParams.Screen.bFullScreen = false;
			break;

		case OPT_GRAB:
			bGrabMouse = true;
			break;

		case OPT_RESIZABLE:
			ok = Opt_Bool(argv[++i], OPT_RESIZABLE, &ConfigureParams.Screen.bResizable);
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

		case OPT_SLOWDOWN:
			val = atoi(argv[++i]);
			errstr = Main_SetVBLSlowdown(val);
			if (errstr)
			{
				return Opt_ShowError(OPT_SLOWDOWN, argv[i], errstr);
			}
			Log_Printf(LOG_DEBUG, "Slow down host VBL wait by factor of %d.\n", val);
			break;

		case OPT_MOUSE_WARP:
			ok = Opt_Bool(argv[++i], OPT_MOUSE_WARP, &ConfigureParams.Screen.bMouseWarp);
			break;

		case OPT_STATUSBAR:
			ok = Opt_Bool(argv[++i], OPT_STATUSBAR, &ConfigureParams.Screen.bShowStatusbar);
			break;

		case OPT_DRIVE_LED:
			ok = Opt_Bool(argv[++i], OPT_DRIVE_LED, &ConfigureParams.Screen.bShowDriveLed);
			break;

		case OPT_DISABLE_VIDEO:
			ok = Opt_Bool(argv[++i], OPT_DISABLE_VIDEO, &ConfigureParams.Screen.DisableVideo);
			break;

			/* ST/STE display options */
		case OPT_BORDERS:
			ok = Opt_Bool(argv[++i], OPT_BORDERS, &ConfigureParams.Screen.bAllowOverscan);
			break;

		case OPT_SPEC512:
			threshold = atoi(argv[++i]);
			if (threshold < 0 || threshold > 512)
			{
				return Opt_ShowError(OPT_SPEC512, argv[i],
						     "Invalid palette writes per line threshold for Spec512");
			}
			Log_Printf(LOG_DEBUG, "Spec512 threshold = %d palette writes per line.\n", threshold);
			ConfigureParams.Screen.nSpec512Threshold = threshold;
			break;

		case OPT_ZOOM:
			zoom = atof(argv[++i]);
			if (zoom < 1.0 || zoom > 8.0)
			{
				return Opt_ShowError(OPT_ZOOM, argv[i], "Invalid zoom value");
			}
			ConfigureParams.Screen.nMaxWidth = NUM_VISIBLE_LINE_PIXELS;
			ConfigureParams.Screen.nMaxHeight = NUM_VISIBLE_LINES;
			/* double ST-low always so that resulting screen size
			 * is approximately same size with same zoom factor
			 * regardless of the machine or monitor type
			 */
			ConfigureParams.Screen.nMaxWidth *= 2;
			ConfigureParams.Screen.nMaxHeight *= 2;
			ConfigureParams.Screen.nZoomFactor = zoom;
			ConfigureParams.Screen.nMaxHeight += STATUSBAR_MAX_HEIGHT;
			break;

		case OPT_VIDEO_TIMING:
			i += 1;
			if (strcasecmp(argv[i], "random") == 0)
				ConfigureParams.System.VideoTimingMode = VIDEO_TIMING_MODE_RANDOM;
			else if (strcasecmp(argv[i], "ws1") == 0)
				ConfigureParams.System.VideoTimingMode = VIDEO_TIMING_MODE_WS1;
			else if (strcasecmp(argv[i], "ws2") == 0)
				ConfigureParams.System.VideoTimingMode = VIDEO_TIMING_MODE_WS2;
			else if (strcasecmp(argv[i], "ws3") == 0)
				ConfigureParams.System.VideoTimingMode = VIDEO_TIMING_MODE_WS3;
			else if (strcasecmp(argv[i], "ws4") == 0)
				ConfigureParams.System.VideoTimingMode = VIDEO_TIMING_MODE_WS4;
			else
				return Opt_ShowError(OPT_VIDEO_TIMING, argv[i], "Unknown video timing mode");
			break;

			/* Falcon/TT display options */
		case OPT_RESOLUTION:
			ok = Opt_Bool(argv[++i], OPT_RESOLUTION, &ConfigureParams.Screen.bKeepResolution);
			break;

		case OPT_MAXWIDTH:
			ConfigureParams.Screen.nMaxWidth = atoi(argv[++i]);
			break;

		case OPT_MAXHEIGHT:
			ConfigureParams.Screen.nMaxHeight = atoi(argv[++i]);
			break;

		case OPT_FORCE_MAX:
			ok = Opt_Bool(argv[++i], OPT_FORCE_MAX, &ConfigureParams.Screen.bForceMax);
			break;

		case OPT_ASPECT:
			ok = Opt_Bool(argv[++i], OPT_ASPECT, &ConfigureParams.Screen.bAspectCorrect);
			break;

			/* screen capture options */
		case OPT_SCREEN_CROP:
			ok = Opt_Bool(argv[++i], OPT_SCREEN_CROP, &ConfigureParams.Screen.bCrop);
			break;

		case OPT_AVIRECORD:
			AviRecordOnStartup = true;
			break;

		case OPT_AVIRECORD_VCODEC:
			i += 1;
			if (strcasecmp(argv[i], "bmp") == 0)
			{
				ConfigureParams.Video.AviRecordVcodec = AVI_RECORD_VIDEO_CODEC_BMP;
			}
			else if (strcasecmp(argv[i], "png") == 0)
			{
				ConfigureParams.Video.AviRecordVcodec = AVI_RECORD_VIDEO_CODEC_PNG;
			}
			else
			{
				return Opt_ShowError(OPT_AVIRECORD_VCODEC, argv[i], "Unknown video codec");
			}
			break;

		case OPT_AVI_PNG_LEVEL:
			i += 1;
			if (!Avi_SetCompressionLevel(argv[i]))
				return Opt_ShowError(OPT_AVI_PNG_LEVEL, argv[i], "Invalid compression level");
			break;

		case OPT_AVIRECORD_FPS:
			val = atoi(argv[++i]);
			if (val < 0 || val > 100)
			{
				return Opt_ShowError(OPT_AVIRECORD_FPS, argv[i],
							"Invalid frame rate for avi recording");
			}
			Log_Printf(LOG_DEBUG, "AVI recording FPS = %d.\n", val);
			ConfigureParams.Video.AviRecordFps = val;
			break;

		case OPT_AVIRECORD_FILE:
			i += 1;
			/* false -> file is created if it doesn't exist */
			ok = Opt_StrCpy(OPT_AVIRECORD_FILE, false, ConfigureParams.Video.AviRecordFile,
					argv[i], sizeof(ConfigureParams.Video.AviRecordFile), NULL);
			break;

		case OPT_SCRSHOT_DIR:
			i += 1;
			Paths_SetScreenShotDir(argv[i]);
			break;

		case OPT_SCRSHOT_FORMAT:
			i += 1;
			if (strcasecmp(argv[i], "bmp") == 0)
			{
				ConfigureParams.Screen.ScreenShotFormat = SCREEN_SNAPSHOT_BMP;
			}
			else if (strcasecmp(argv[i], "png") == 0)
			{
				ConfigureParams.Screen.ScreenShotFormat = SCREEN_SNAPSHOT_PNG;
			}
			else if (strcasecmp(argv[i], "neo") == 0)
			{
				ConfigureParams.Screen.ScreenShotFormat = SCREEN_SNAPSHOT_NEO;
			}
			else if (strcasecmp(argv[i], "ximg") == 0)
			{
				ConfigureParams.Screen.ScreenShotFormat = SCREEN_SNAPSHOT_XIMG;
			}
			else
			{
				return Opt_ShowError(OPT_SCRSHOT_FORMAT, argv[i], "Unknown screenshot format");
			}
			break;

			/* VDI options */
		case OPT_VDI:
			ok = Opt_Bool(argv[++i], OPT_VDI, &ConfigureParams.Screen.bUseExtVdiResolutions);
			if (ok)
			{
				bLoadAutoSave = false;
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
			ConfigureParams.Screen.bUseExtVdiResolutions = true;
			bLoadAutoSave = false;
			break;

		case OPT_VDI_WIDTH:
			ConfigureParams.Screen.nVdiWidth = atoi(argv[++i]);
			ConfigureParams.Screen.bUseExtVdiResolutions = true;
			bLoadAutoSave = false;
			break;

		case OPT_VDI_HEIGHT:
			ConfigureParams.Screen.nVdiHeight = atoi(argv[++i]);
			ConfigureParams.Screen.bUseExtVdiResolutions = true;
			bLoadAutoSave = false;
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
			if (port < 0 || port >= JOYSTICK_COUNT)
			{
				return Opt_ShowError(OPT_JOYSTICK0, argv[i], "Invalid joystick port");
			}
			i += 1;
			if (strcasecmp(argv[i], "none") == 0 || strcasecmp(argv[i], "off") == 0)
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
			/* "none" can be used to disable printer */
			ok = Opt_StrCpy(OPT_PRINTER, false, ConfigureParams.Printer.szPrintToFileName,
					argv[i], sizeof(ConfigureParams.Printer.szPrintToFileName),
					&ConfigureParams.Printer.bEnablePrinting);
			break;

#ifdef HAVE_PORTMIDI
		case OPT_MIDI:
			ok = Opt_Bool(argv[++i], OPT_MIDI, &ConfigureParams.Midi.bEnableMidi);
			break;
#else
		case OPT_MIDI_IN:
			i += 1;
			ok = Opt_StrCpy(OPT_MIDI_IN, true, ConfigureParams.Midi.sMidiInFileName,
					argv[i], sizeof(ConfigureParams.Midi.sMidiInFileName),
					&ConfigureParams.Midi.bEnableMidi);
			break;

		case OPT_MIDI_OUT:
			i += 1;
			ok = Opt_StrCpy(OPT_MIDI_OUT, false, ConfigureParams.Midi.sMidiOutFileName,
					argv[i], sizeof(ConfigureParams.Midi.sMidiOutFileName),
					&ConfigureParams.Midi.bEnableMidi);
			break;
#endif

		case OPT_RS232_IN:
			i += 1;
			ok = Opt_StrCpy(OPT_RS232_IN, true, ConfigureParams.RS232.szInFileName,
					argv[i], sizeof(ConfigureParams.RS232.szInFileName),
					&ConfigureParams.RS232.bEnableRS232);
			break;

		case OPT_RS232_OUT:
			i += 1;
			ok = Opt_StrCpy(OPT_RS232_OUT, false, ConfigureParams.RS232.szOutFileName,
					argv[i], sizeof(ConfigureParams.RS232.szOutFileName),
					&ConfigureParams.RS232.bEnableRS232);
			break;

		case OPT_SCCA_IN:
			i += 1;
			ok = Opt_StrCpy(OPT_SCCA_IN, true, ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_A_SERIAL],
					argv[i], sizeof(ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_A_SERIAL]),
					&ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_A_SERIAL]);
			break;
		case OPT_SCCA_OUT:
			i += 1;
			ok = Opt_StrCpy(OPT_SCCA_OUT, false, ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_A_SERIAL],
					argv[i], sizeof(ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_A_SERIAL]),
					&ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_A_SERIAL]);
			break;
		case OPT_SCCA_LAN_IN:
			i += 1;
			ok = Opt_StrCpy(OPT_SCCA_LAN_IN, true, ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_A_LAN],
					argv[i], sizeof(ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_A_LAN]),
					&ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_A_LAN]);
			break;
		case OPT_SCCA_LAN_OUT:
			i += 1;
			ok = Opt_StrCpy(OPT_SCCA_LAN_OUT, false, ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_A_LAN],
					argv[i], sizeof(ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_A_LAN]),
					&ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_A_LAN]);
			break;
		case OPT_SCCB_IN:
			i += 1;
			ok = Opt_StrCpy(OPT_SCCB_IN, true, ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_B],
					argv[i], sizeof(ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_B]),
					&ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_B]);
			break;
		case OPT_SCCB_OUT:
			i += 1;
			ok = Opt_StrCpy(OPT_SCCB_OUT, false, ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_B],
					argv[i], sizeof(ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_B]),
					&ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_B]);
			break;

			/* disk options */
		case OPT_DRIVEA:
			ok = Opt_Bool(argv[++i], OPT_DRIVEA, &ConfigureParams.DiskImage.EnableDriveA);
			break;

		case OPT_DRIVEB:
			ok = Opt_Bool(argv[++i], OPT_DRIVEB, &ConfigureParams.DiskImage.EnableDriveB);
			break;

		case OPT_DRIVEA_HEADS:
			val = atoi(argv[++i]);
			if(val != 1 && val != 2)
			{
				return Opt_ShowError(OPT_DRIVEA_HEADS, argv[i], "Invalid number of heads");
			}
			ConfigureParams.DiskImage.DriveA_NumberOfHeads = val;
			break;

		case OPT_DRIVEB_HEADS:
			val = atoi(argv[++i]);
			if(val != 1 && val != 2)
			{
				return Opt_ShowError(OPT_DRIVEB_HEADS, argv[i], "Invalid number of heads");
			}
			ConfigureParams.DiskImage.DriveB_NumberOfHeads = val;
			break;

		case OPT_DISKA:
			i += 1;
			if (Floppy_SetDiskFileName(0, argv[i], NULL))
			{
				ConfigureParams.HardDisk.bBootFromHardDisk = false;
				bLoadAutoSave = false;
			}
			else
				return Opt_ShowError(OPT_ERROR, argv[i], "Not a disk image");
			break;

		case OPT_DISKB:
			i += 1;
			if (Floppy_SetDiskFileName(1, argv[i], NULL))
				bLoadAutoSave = false;
			else
				return Opt_ShowError(OPT_ERROR, argv[i], "Not a disk image");
			break;

		case OPT_FASTFLOPPY:
			ok = Opt_Bool(argv[++i], OPT_FASTFLOPPY, &ConfigureParams.DiskImage.FastFloppy);
			break;

		case OPT_WRITEPROT_FLOPPY:
			i += 1;
			if (strcasecmp(argv[i], "off") == 0)
				ConfigureParams.DiskImage.nWriteProtection = WRITEPROT_OFF;
			else if (strcasecmp(argv[i], "on") == 0)
				ConfigureParams.DiskImage.nWriteProtection = WRITEPROT_ON;
			else if (strcasecmp(argv[i], "auto") == 0)
				ConfigureParams.DiskImage.nWriteProtection = WRITEPROT_AUTO;
			else
				return Opt_ShowError(OPT_WRITEPROT_FLOPPY, argv[i], "Unknown option value");
			break;

		case OPT_WRITEPROT_HD:
			i += 1;
			if (strcasecmp(argv[i], "off") == 0)
				ConfigureParams.HardDisk.nWriteProtection = WRITEPROT_OFF;
			else if (strcasecmp(argv[i], "on") == 0)
				ConfigureParams.HardDisk.nWriteProtection = WRITEPROT_ON;
			else if (strcasecmp(argv[i], "auto") == 0)
				ConfigureParams.HardDisk.nWriteProtection = WRITEPROT_AUTO;
			else
				return Opt_ShowError(OPT_WRITEPROT_HD, argv[i], "Unknown option value");
			break;

		case OPT_GEMDOS_CASE:
			i += 1;
			if (strcasecmp(argv[i], "off") == 0)
				ConfigureParams.HardDisk.nGemdosCase = GEMDOS_NOP;
			else if (strcasecmp(argv[i], "upper") == 0)
				ConfigureParams.HardDisk.nGemdosCase = GEMDOS_UPPER;
			else if (strcasecmp(argv[i], "lower") == 0)
				ConfigureParams.HardDisk.nGemdosCase = GEMDOS_LOWER;
			else
				return Opt_ShowError(OPT_GEMDOS_CASE, argv[i], "Unknown option value");
			break;

		case OPT_GEMDOS_HOSTTIME:
			i += 1;
			if (strcasecmp(argv[i], "atari") == 0)
				ConfigureParams.HardDisk.bGemdosHostTime = false;
			else if (strcasecmp(argv[i], "host") == 0)
				ConfigureParams.HardDisk.bGemdosHostTime = true;
			else
				return Opt_ShowError(OPT_GEMDOS_HOSTTIME, argv[i], "Unknown option value");
			break;

		case OPT_GEMDOS_CONVERT:
			ok = Opt_Bool(argv[++i], OPT_GEMDOS_CONVERT, &ConfigureParams.HardDisk.bFilenameConversion);
			break;

		case OPT_GEMDOS_DRIVE:
			i += 1;
			if (strcasecmp(argv[i], "skip") == 0)
			{
				ConfigureParams.HardDisk.nGemdosDrive = DRIVE_SKIP;
				break;
			}
			else if (strlen(argv[i]) == 1)
			{
				drive = toupper(argv[i][0]);
				if (drive >= 'C' && drive <= 'Z')
				{
					drive = drive - 'C' + DRIVE_C;
					ConfigureParams.HardDisk.nGemdosDrive = drive;
					break;
				}
			}
			return Opt_ShowError(OPT_GEMDOS_DRIVE, argv[i], "Invalid <drive>");

		case OPT_HARDDRIVE:
			i += 1;
			ok = Opt_StrCpy(OPT_HARDDRIVE, false, ConfigureParams.HardDisk.szHardDiskDirectories[0],
					argv[i], sizeof(ConfigureParams.HardDisk.szHardDiskDirectories[0]),
					&ConfigureParams.HardDisk.bUseHardDiskDirectories);
			if (ok && ConfigureParams.HardDisk.bUseHardDiskDirectories &&
			    ConfigureParams.HardDisk.szHardDiskDirectories[0][0])
			{
				ConfigureParams.HardDisk.bBootFromHardDisk = true;
			}
			else
			{
				ConfigureParams.HardDisk.bUseHardDiskDirectories = false;
				ConfigureParams.HardDisk.bBootFromHardDisk = false;
			}
			bLoadAutoSave = false;
			break;

		case OPT_ACSIHDIMAGE:
			i += 1;
			str = argv[i];
			if (strlen(str) > 2 && isdigit((unsigned char)str[0]) && str[1] == '=')
			{
				drive = str[0] - '0';
				if (drive < 0 || drive >= MAX_ACSI_DEVS)
					return Opt_ShowError(OPT_ACSIHDIMAGE, str, "Invalid ACSI drive <id>, must be 0-7");
				str += 2;
			}
			else
			{
				drive = 0;
			}
			ok = Opt_StrCpy(OPT_ACSIHDIMAGE, true, ConfigureParams.Acsi[drive].sDeviceFile,
					str, sizeof(ConfigureParams.Acsi[drive].sDeviceFile),
					&ConfigureParams.Acsi[drive].bUseDevice);
			if (ok)
			{
				bLoadAutoSave = false;
			}
			break;

		case OPT_SCSIHDIMAGE:
			i += 1;
			str = argv[i];
			if (strlen(str) > 2 && isdigit((unsigned char)str[0]) && str[1] == '=')
			{
				drive = str[0] - '0';
				if (drive < 0 || drive >= MAX_SCSI_DEVS)
					return Opt_ShowError(OPT_SCSIHDIMAGE, str, "Invalid SCSI drive <id>, must be 0-7");
				str += 2;
			}
			else
			{
				drive = 0;
			}
			ok = Opt_StrCpy(OPT_SCSIHDIMAGE, true, ConfigureParams.Scsi[drive].sDeviceFile,
					str, sizeof(ConfigureParams.Scsi[drive].sDeviceFile),
					&ConfigureParams.Scsi[drive].bUseDevice);
			if (ok)
			{
				bLoadAutoSave = false;
			}
			break;

		case OPT_IDEMASTERHDIMAGE:
			i += 1;
			ok = Opt_StrCpy(OPT_IDEMASTERHDIMAGE, true, ConfigureParams.Ide[0].sDeviceFile,
					argv[i], sizeof(ConfigureParams.Ide[0].sDeviceFile),
					&ConfigureParams.Ide[0].bUseDevice);
			if (ok)
			{
				bLoadAutoSave = false;
			}
			break;

		case OPT_IDESLAVEHDIMAGE:
			i += 1;
			ok = Opt_StrCpy(OPT_IDESLAVEHDIMAGE, true, ConfigureParams.Ide[1].sDeviceFile,
					argv[i], sizeof(ConfigureParams.Ide[1].sDeviceFile),
					&ConfigureParams.Ide[1].bUseDevice);
			if (ok)
			{
				bLoadAutoSave = false;
			}
			break;

		case OPT_IDEBYTESWAP:
			i += 1;
			str = argv[i];
			if (strlen(str) > 2 && isdigit((unsigned char)str[0]) && str[1] == '=')
			{
				drive = str[0] - '0';
				if (drive < 0 || drive > 1)
					return Opt_ShowError(OPT_IDEBYTESWAP, str, "Invalid IDE drive <id>, must be 0/1");
				str += 2;
			}
			else
			{
				drive = 0;
			}
			if (strcasecmp(str, "off") == 0)
				ConfigureParams.Ide[drive].nByteSwap = BYTESWAP_OFF;
			else if (strcasecmp(str, "on") == 0)
				ConfigureParams.Ide[drive].nByteSwap = BYTESWAP_ON;
			else if (strcasecmp(str, "auto") == 0)
				ConfigureParams.Ide[drive].nByteSwap = BYTESWAP_AUTO;
			else
				return Opt_ShowError(OPT_IDEBYTESWAP, argv[i], "Invalid byte-swap setting");
			break;

			/* Memory options */
		case OPT_MEMSIZE:
			memsize = atoi(argv[++i]);
			memsize = STMemory_RAM_Validate_Size_KB ( memsize );
			if (memsize < 0)
			{
				return Opt_ShowError(OPT_MEMSIZE, argv[i], "Invalid memory size");
			}
			ConfigureParams.Memory.STRamSize_KB = memsize;
			bLoadAutoSave = false;
			break;

		case OPT_TT_RAM:
			memsize = atoi(argv[++i]);
			ConfigureParams.Memory.TTRamSize_KB = Opt_ValueAlignMinMax(memsize+3, 4, 0, 1024) * 1024;
			bLoadAutoSave = false;
			break;

		case OPT_TOS:
			i += 1;
			ok = Opt_StrCpy(OPT_TOS, true, ConfigureParams.Rom.szTosImageFileName,
					argv[i], sizeof(ConfigureParams.Rom.szTosImageFileName),
					&bUseTos);
			if (ok || !bUseTos)
			{
				bLoadAutoSave = false;
			}
			break;

		case OPT_PATCHTOS:
			ok = Opt_Bool(argv[++i], OPT_PATCHTOS, &ConfigureParams.Rom.bPatchTos);
			break;

		case OPT_CARTRIDGE:
			i += 1;
			ok = Opt_StrCpy(OPT_CARTRIDGE, true, ConfigureParams.Rom.szCartridgeImageFileName,
					argv[i], sizeof(ConfigureParams.Rom.szCartridgeImageFileName),
					NULL);
			if (ok)
			{
				bLoadAutoSave = false;
			}
			break;

		case OPT_MEMSTATE:
			i += 1;
			ok = Opt_StrCpy(OPT_MEMSTATE, true, ConfigureParams.Memory.szMemoryCaptureFileName,
					argv[i], sizeof(ConfigureParams.Memory.szMemoryCaptureFileName),
					NULL);
			if (ok)
			{
				bLoadMemorySave = true;
				bLoadAutoSave = false;
			}
			break;

			/* CPU options */
		case OPT_CPULEVEL:
			/* UAE core uses cpu_level variable */
			ncpu = atoi(argv[++i]);
			if(ncpu < 0 || ncpu == 5 || ncpu > 6)
			{
				return Opt_ShowError(OPT_CPULEVEL, argv[i], "Invalid CPU level");
			}
			if ( ncpu == 6 )			/* Special case for 68060, nCpuLevel should be 5 */
				ncpu = 5;
			ConfigureParams.System.nCpuLevel = ncpu;
			bLoadAutoSave = false;
			break;

		case OPT_CPUCLOCK:
			cpuclock = atoi(argv[++i]);
			if(cpuclock != 8 && cpuclock != 16 && cpuclock != 32)
			{
				return Opt_ShowError(OPT_CPUCLOCK, argv[i], "Invalid CPU clock");
			}
			Configuration_ChangeCpuFreq ( cpuclock );
			bLoadAutoSave = false;
			break;

		case OPT_COMPATIBLE:
			ok = Opt_Bool(argv[++i], OPT_COMPATIBLE, &ConfigureParams.System.bCompatibleCpu);
			if (ok)
			{
				bLoadAutoSave = false;
			}
			break;
		case OPT_CPU_ADDR24:
			ok = Opt_Bool(argv[++i], OPT_CPU_ADDR24, &ConfigureParams.System.bAddressSpace24);
			bLoadAutoSave = false;
			break;

		case OPT_CPU_CYCLE_EXACT:
			ok = Opt_Bool(argv[++i], OPT_CPU_CYCLE_EXACT, &ConfigureParams.System.bCycleExactCpu);
			bLoadAutoSave = false;
			break;

		case OPT_FPU_TYPE:
			i += 1;
			if (strcasecmp(argv[i], "none") == 0 || strcasecmp(argv[i], "off") == 0)
			{
				ConfigureParams.System.n_FPUType = FPU_NONE;
			}
			else if (strcasecmp(argv[i], "68881") == 0)
			{
				ConfigureParams.System.n_FPUType = FPU_68881;
			}
			else if (strcasecmp(argv[i], "68882") == 0)
			{
				ConfigureParams.System.n_FPUType = FPU_68882;
			}
			else if (strcasecmp(argv[i], "internal") == 0)
			{
				ConfigureParams.System.n_FPUType = FPU_CPU;
			}
			else
			{
				return Opt_ShowError(OPT_FPU_TYPE, argv[i], "Unknown FPU type");
			}
			bLoadAutoSave = false;
			break;
/*
		case OPT_FPU_JIT_COMPAT:
			ok = Opt_Bool(argv[++i], OPT_FPU_COMPATIBLE, &ConfigureParams.System.bCompatibleFPU);
			break;
*/
		case OPT_FPU_SOFTFLOAT:
			ok = Opt_Bool(argv[++i], OPT_FPU_SOFTFLOAT, &ConfigureParams.System.bSoftFloatFPU);
			break;

		case OPT_MMU:
			ok = Opt_Bool(argv[++i], OPT_MMU, &ConfigureParams.System.bMMU);
			bLoadAutoSave = false;
			break;

			/* system options */
		case OPT_MACHINE:
			i += 1;
			if (strcasecmp(argv[i], "st") == 0)
			{
				ConfigureParams.System.nMachineType = MACHINE_ST;
				ConfigureParams.System.nCpuLevel = 0;
				Configuration_ChangeCpuFreq ( 8 );
			}
			else if (strcasecmp(argv[i], "megast") == 0)
			{
				ConfigureParams.System.nMachineType = MACHINE_MEGA_ST;
				ConfigureParams.System.nCpuLevel = 0;
				Configuration_ChangeCpuFreq ( 8 );
			}
			else if (strcasecmp(argv[i], "ste") == 0)
			{
				ConfigureParams.System.nMachineType = MACHINE_STE;
				ConfigureParams.System.nCpuLevel = 0;
				Configuration_ChangeCpuFreq ( 8 );
			}
			else if (strcasecmp(argv[i], "megaste") == 0)
			{
				ConfigureParams.System.nMachineType = MACHINE_MEGA_STE;
				ConfigureParams.System.nCpuLevel = 0;
				Configuration_ChangeCpuFreq ( 8 );
			}
			else if (strcasecmp(argv[i], "tt") == 0)
			{
				ConfigureParams.System.nMachineType = MACHINE_TT;
				ConfigureParams.System.nCpuLevel = 3;
				Configuration_ChangeCpuFreq ( 32 );
			}
			else if (strcasecmp(argv[i], "falcon") == 0)
			{
#if ENABLE_DSP_EMU
				ConfigureParams.System.nDSPType = DSP_TYPE_EMU;
#endif
				ConfigureParams.System.nMachineType = MACHINE_FALCON;
				ConfigureParams.System.nCpuLevel = 3;
				Configuration_ChangeCpuFreq ( 16 );
			}
			else
			{
				return Opt_ShowError(OPT_MACHINE, argv[i], "Unknown machine type");
			}
			if (Config_IsMachineST() || Config_IsMachineSTE())
			{
				ConfigureParams.System.bMMU = false;
				ConfigureParams.System.bAddressSpace24 = true;
			}
			if (Config_IsMachineTT())
			{
				ConfigureParams.System.bCompatibleFPU = true;
				ConfigureParams.System.n_FPUType = FPU_68882;
			}
			else
			{
				ConfigureParams.System.n_FPUType = FPU_NONE;	/* TODO: or leave it as-is? */
			}
			bLoadAutoSave = false;
			break;

		case OPT_BLITTER:
			ok = Opt_Bool(argv[++i], OPT_BLITTER, &ConfigureParams.System.bBlitter);
			if (ok)
			{
				bLoadAutoSave = false;
			}
			break;

		case OPT_TIMERD:
			ok = Opt_Bool(argv[++i], OPT_TIMERD, &ConfigureParams.System.bPatchTimerD);
			break;

		case OPT_FASTBOOT:
			ok = Opt_Bool(argv[++i], OPT_FASTBOOT, &ConfigureParams.System.bFastBoot);
			break;

		case OPT_DSP:
			i += 1;
			if (strcasecmp(argv[i], "none") == 0 || strcasecmp(argv[i], "off") == 0)
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
			bLoadAutoSave = false;
			break;

		case OPT_VME:
			i += 1;
			if (strcasecmp(argv[i], "dummy") == 0)
			{
				ConfigureParams.System.nVMEType = VME_TYPE_DUMMY;
			}
			else if (strcasecmp(argv[i], "none") == 0 || strcasecmp(argv[i], "off") == 0)
			{
				ConfigureParams.System.nVMEType = VME_TYPE_NONE;
			}
			else
			{
				return Opt_ShowError(OPT_VME, argv[i], "Unknown VME type");
			}
			bLoadAutoSave = false; /* TODO: needed? */
			break;

		case OPT_RTC_YEAR:
			year = atoi(argv[++i]);
			if(year && (year < 1980 || year >= 2080))
			{
				return Opt_ShowError(OPT_RTC_YEAR, argv[i], "Invalid RTC year");
			}
			ConfigureParams.System.nRtcYear = year;
			break;

			/* sound options */
		case OPT_YM_MIXING:
			i += 1;
			if (strcasecmp(argv[i], "linear") == 0)
			{
				ConfigureParams.Sound.YmVolumeMixing = YM_LINEAR_MIXING;
			}
			else if (strcasecmp(argv[i], "table") == 0)
			{
				ConfigureParams.Sound.YmVolumeMixing = YM_TABLE_MIXING;
			}
			else if (strcasecmp(argv[i], "model") == 0)
			{
				ConfigureParams.Sound.YmVolumeMixing = YM_MODEL_MIXING;
			}
			else
			{
				return Opt_ShowError(OPT_YM_MIXING, argv[i], "Unknown YM mixing method");
			}
			break;

		case OPT_SOUND:
			i += 1;
			if (strcasecmp(argv[i], "off") == 0)
			{
				ConfigureParams.Sound.bEnableSound = false;
			}
			else
			{
				freq = atoi(argv[i]);
				if (freq < 6000 || freq > 50066)
				{
					return Opt_ShowError(OPT_SOUND, argv[i], "Unsupported sound frequency");
				}
				ConfigureParams.Sound.nPlaybackFreq = freq;
				ConfigureParams.Sound.bEnableSound = true;
			}
			Log_Printf(LOG_DEBUG, "Sound %s, frequency = %d.\n",
			        ConfigureParams.Sound.bEnableSound ? "ON" : "OFF",
			        ConfigureParams.Sound.nPlaybackFreq);
			break;

		case OPT_SOUNDBUFFERSIZE:
			i += 1;
			temp = atoi(argv[i]);
			if ( temp == 0 )			/* use default setting for SDL */
				;
			else if (temp < 10 || temp > 100)
				{
					return Opt_ShowError(OPT_SOUNDBUFFERSIZE, argv[i], "Unsupported sound buffer size");
				}
			Log_Printf(LOG_DEBUG, "SDL sound buffer size = %d ms.\n", temp);
			ConfigureParams.Sound.SdlAudioBufferSize = temp;
			break;

		case OPT_SOUNDSYNC:
			ok = Opt_Bool(argv[++i], OPT_SOUNDSYNC, &ConfigureParams.Sound.bEnableSoundSync);
			break;

		case OPT_MICROPHONE:
			ok = Opt_Bool(argv[++i], OPT_MICROPHONE, &ConfigureParams.Sound.bEnableMicrophone);
			break;

		case OPT_COUNTRY_CODE:
			ok = Opt_CountryCode(argv[++i], OPT_COUNTRY_CODE, &ConfigureParams.Keyboard.nCountryCode);
			break;

		case OPT_LANGUAGE:
			ok = Opt_CountryCode(argv[++i], OPT_LANGUAGE, &ConfigureParams.Keyboard.nLanguage);
			break;

		case OPT_KBD_LAYOUT:
			ok = Opt_CountryCode(argv[++i], OPT_KBD_LAYOUT, &ConfigureParams.Keyboard.nKbdLayout);
			break;

		case OPT_KEYMAPFILE:
			i += 1;
			ok = Opt_StrCpy(OPT_KEYMAPFILE, true, ConfigureParams.Keyboard.szMappingFileName,
					argv[i], sizeof(ConfigureParams.Keyboard.szMappingFileName),
					NULL);
			if (ok)
			{
				ConfigureParams.Keyboard.nKeymapType = KEYMAP_LOADED;
			}
			break;

			/* debug options */
#ifdef WIN32
		case OPT_WINCON:
			ConfigureParams.Log.bConsoleWindow = true;
			break;
#endif
		case OPT_DEBUG:
			if (ExceptionDebugMask)
			{
				ExceptionDebugMask = EXCEPT_NONE;
				Log_Printf(LOG_INFO, "Exception debugging disabled.\n");
			}
			else
			{
				ExceptionDebugMask = ConfigureParams.Debugger.nExceptionDebugMask;
				Log_Printf(LOG_INFO, "Exception debugging enabled (0x%x).\n", ExceptionDebugMask);
			}
			break;

		case OPT_EXCEPTIONS:
			i += 1;
			/* sets ConfigureParams.Debugger.nExceptionDebugMask */
			errstr = Log_SetExceptionDebugMask(argv[i]);
			if (errstr)
			{
				if (!errstr[0])
				{
					/* silent parsing termination */
					return false;
				}
				return Opt_ShowError(OPT_EXCEPTIONS, argv[i], errstr);
			}
			if (ExceptionDebugMask)
			{
				/* already enabled, change run-time config */
				int oldmask = ExceptionDebugMask;
				ExceptionDebugMask = ConfigureParams.Debugger.nExceptionDebugMask;
				Log_Printf(LOG_INFO, "Exception debugging changed (0x%x -> 0x%x).\n",
				        oldmask, ExceptionDebugMask);
			}
			break;

		case OPT_LILO: {
			size_t len;
			len = strlen(argv[++i]);
			len += strlen(ConfigureParams.Lilo.szCommandLine);
			if (argv[i][0] && len+2 < sizeof(ConfigureParams.Lilo.szCommandLine))
			{
				strcat(ConfigureParams.Lilo.szCommandLine, " ");
				strcat(ConfigureParams.Lilo.szCommandLine, argv[i]);
			}
			else if (argv[i][0])
			{
				return Opt_ShowError(OPT_LILO, argv[i], "kernel command line too long");
			}
			bLoadAutoSave = false;
			bUseLilo = true;
			bUseTos = false;
			break;
		}

		case OPT_BIOSINTERCEPT:
			ok = Opt_Bool(argv[++i], OPT_BIOSINTERCEPT, &bBiosIntercept);
			Log_Printf(LOG_DEBUG, "XBIOS 11/20/255 Hatari versions %sabled: "
			        "Dbmsg(), Scrdmp(), HatariControl().\n",
			        bBiosIntercept ? "en" : "dis");
			XBios_EnableCommands(bBiosIntercept);
			break;

		case OPT_CONOUT:
			i += 1;
			dev = atoi(argv[i]);
			if (!Console_SetDevice(dev))
			{
				return Opt_ShowError(OPT_CONOUT, argv[i], "Invalid console device vector number");
			}
			Log_Printf(LOG_DEBUG, "Xcounout device %d vector redirection enabled.\n", dev);
			break;

		case OPT_NATFEATS:
			ok = Opt_Bool(argv[++i], OPT_NATFEATS, &ConfigureParams.Log.bNatFeats);
			Log_Printf(LOG_DEBUG, "Native Features %s.\n", ConfigureParams.Log.bNatFeats ? "enabled" : "disabled");
			break;

		case OPT_DISASM:
			i += 1;
			errstr = Disasm_ParseOption(argv[i]);
			if (errstr)
			{
				if (!errstr[0])
				{
					/* silent parsing termination */
					return false;
				}
				return Opt_ShowError(OPT_DISASM, argv[i], errstr);
			}
			break;

		case OPT_TRACE:
			i += 1;
			errstr = Log_SetTraceOptions(argv[i]);
			if (errstr)
			{
				if (!errstr[0])
				{
					/* silent parsing termination */
					return false;
				}
				return Opt_ShowError(OPT_TRACE, argv[i], errstr);
			}
			break;

		case OPT_TRACEFILE:
			i += 1;
			ok = Opt_StrCpy(OPT_TRACEFILE, false, ConfigureParams.Log.sTraceFileName,
					argv[i], sizeof(ConfigureParams.Log.sTraceFileName),
					NULL);
			break;

		case OPT_MSG_REPEAT:
			Log_ToggleMsgRepeat();
			break;

		case OPT_CONTROLSOCKET:
			i += 1;
			errstr = Control_SetSocket(argv[i]);
			if (errstr)
			{
				return Opt_ShowError(OPT_CONTROLSOCKET, argv[i], errstr);
			}
			break;

		case OPT_CMDFIFO:
			i += 1;
			errstr = Control_SetFifo(argv[i]);
			if (errstr)
			{
				return Opt_ShowError(OPT_CMDFIFO, argv[i], errstr);
			}
			break;

		case OPT_LOGFILE:
			i += 1;
			ok = Opt_StrCpy(OPT_LOGFILE, false, ConfigureParams.Log.sLogFileName,
					argv[i], sizeof(ConfigureParams.Log.sLogFileName),
					NULL);
			break;

		case OPT_PARSE:
			i += 1;
			ok = DebugUI_AddParseFile(argv[i]);
			break;

		case OPT_SAVECONFIG:
			/* Hatari-UI needs Hatari config to start */
			Configuration_Save();
			exit(0);
			break;

		case OPT_LOGLEVEL:
			i += 1;
			ConfigureParams.Log.nTextLogLevel = Log_ParseOptions(argv[i]);
			if (ConfigureParams.Log.nTextLogLevel == LOG_NONE)
			{
				return Opt_ShowError(OPT_LOGLEVEL, argv[i], "Unknown log level!");
			}
			Log_SetLevels();
			break;

		case OPT_ALERTLEVEL:
			i += 1;
			ConfigureParams.Log.nAlertDlgLogLevel = Log_ParseOptions(argv[i]);
			if (ConfigureParams.Log.nAlertDlgLogLevel == LOG_NONE)
			{
				return Opt_ShowError(OPT_ALERTLEVEL, argv[i], "Unknown alert level!");
			}
			Log_SetLevels();
			break;

		case OPT_RUNVBLS:
			val = atoi(argv[++i]);
			Log_Printf(LOG_DEBUG, "Exit after %d VBLs.\n", val);
			Main_SetRunVBLs(val);
			break;

		case OPT_BENCHMARK:
			BenchmarkMode = true;
			break;

		case OPT_ERROR:
			/* unknown option or missing option parameter */
			return false;

		default:
			return Opt_ShowError(OPT_ERROR, argv[i], "Internal Hatari error, unhandled option");
		}
		if (!ok)
		{
			/* Opt_Bool() or Opt_StrCpy() failed */
			return false;
		}
	}

	return Opt_ValidateOptions();
}

/**
 * Readline match callback for option name completion.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char *Opt_MatchOption(const char *text, int state)
{
	static int i, len;
	const char *name;

	if (!state)
	{
		/* first match */
		len = strlen(text);
		i = 0;
	}
	/* next match */
	while (i < ARRAY_SIZE(HatariOptions))
	{
		name = HatariOptions[i++].str;
		if (name && strncasecmp(name, text, len) == 0)
			return (strdup(name));
	}
	return NULL;
}
