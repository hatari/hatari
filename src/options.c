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
#include "conv_st.h"
#include "console.h"
#include "control.h"
#include "debugui.h"
#include "event.h"
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
#include "timing.h"
#include "tos.h"
#include "lilo.h"
#include "screenSnapShot.h"
#ifdef WIN32
#include "gui-win/opencon.h"
#endif


bool bLoadAutoSave;        /* Load autosave memory snapshot at startup */
bool bLoadMemorySave;      /* Load memory snapshot provided via option at startup */
bool AviRecordEnabled;     /* Whether AVI recording should be active or not */
bool BenchmarkMode;	   /* Start in benchmark mode (try to run at maximum emulation */
			   /* speed allowed by the CPU). Disable audio/video for best results */

static bool bBiosIntercept;

/* Opt_StrCpy() option types.
 *
 * If both a dir/file string and a bool enabling given device are
 * provided, "" / "none" as dir/file string disables given device
 */
typedef enum {
	CHECK_NONE,	/* create file if it does not exist */
	CHECK_FILE,	/* given file needs to exist */
	CHECK_DIR	/* given dir needs to exist */
} fs_check_t;


/*  List of supported options. */
typedef enum {
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
	OPT_FF_KEY_REPEAT,

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
	OPT_VSYNC,
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
	OPT_SCSIVERSION,
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
	OPT_CPU_DATA_CACHE,
	OPT_CPU_CYCLE_EXACT,
	OPT_CPU_ADDR24,
	OPT_FPU_TYPE,
/*	OPT_FPU_JIT_COMPAT, */
	OPT_FPU_SOFTFLOAT,
	OPT_MMU,

	OPT_MACHINE,		/* system options */
	OPT_BLITTER,
	OPT_DSP,
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
	OPT_SYMLOAD,
	OPT_LILO,
	OPT_BIOSINTERCEPT,
	OPT_CONOUT,
	OPT_DISASM,
	OPT_MEMCONV,
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

	/* needs to be after last valid option, to terminate options help */
	OPT_ERROR,
	OPT_CONTINUE
} opt_id_t;

typedef struct {
	const char *key;
	int value;
} opt_keyval_t;

typedef struct {
	opt_id_t id;		/* option ID */
	const char *chr;	/* short option */
	const char *str;	/* long option */
	const char *arg;	/* type name for argument, if any */
	const char *desc;	/* option description */
} opt_t;

/* These shoul be kept in the same order as the enums */
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
	{ OPT_FF_KEY_REPEAT, NULL, "--fast-forward-key-repeat",
	  "<bool>", "Use keyboard auto repeat in fast forward mode" },

	{ OPT_HEADER, NULL, NULL, NULL, "Common display" },
	{ OPT_MONO,      "-m", "--mono",
	  NULL, "Start in monochrome mode instead of color" },
	{ OPT_MONITOR,      NULL, "--monitor",
	  "<x>", "Select monitor type (x = mono/rgb/vga/tv)" },
	{ OPT_TOS_RESOLUTION,   NULL, "--tos-res",
	  "<x>", "TOS resolution (x = {st,tt,tc}{low,med,high})" },
	{ OPT_FULLSCREEN,"-f", "--fullscreen",
	  NULL, "Start emulator in fullscreen mode" },
	{ OPT_WINDOW,    "-w", "--window",
	  NULL, "Start emulator in windowed mode" },
	{ OPT_GRAB, NULL, "--grab",
	  NULL, "Grab mouse (also) in windowed mode" },
	{ OPT_RESIZABLE,    NULL, "--resizable",
	  "<bool>", "Allow window resizing" },
	{ OPT_FRAMESKIPS, NULL, "--frameskips",
	  "<int>", "Skip <int> frames for every shown one (0-64, 0=off, >4=auto)" },
	{ OPT_SLOWDOWN, NULL, "--slowdown",
	  "<int>", "VBL wait time multiplier (1-30, default 1)" },
	{ OPT_MOUSE_WARP, NULL, "--mousewarp",
	  "<bool>", "Center host mouse on reset & resolution changes" },
	{ OPT_STATUSBAR, NULL, "--statusbar",
	  "<bool>", "Show statusbar (floppy leds etc)" },
	{ OPT_DRIVE_LED,   NULL, "--drive-led",
	  "<bool>", "Show overlay drive led when statusbar isn't shown" },
	{ OPT_MAXWIDTH, NULL, "--max-width",
	  "<int>", "Maximum Hatari screen width before scaling (320-)" },
	{ OPT_MAXHEIGHT, NULL, "--max-height",
	  "<int>", "Maximum Hatari screen height before scaling (200-)" },
	{ OPT_ZOOM, "-z", "--zoom",
	  "<x>", "Hatari screen/window scaling factor (1.0 - 8.0)" },
	{ OPT_VSYNC,   NULL, "--vsync",
	  "<bool>", "Limit screen updates to host monitor refresh rate" },
	{ OPT_DISABLE_VIDEO,   NULL, "--disable-video",
	  "<bool>", "Run emulation without displaying video (audio only)" },

	{ OPT_HEADER, NULL, NULL, NULL, "ST/STE specific display" },
	{ OPT_BORDERS, NULL, "--borders",
	  "<bool>", "Show screen borders (for overscan demos etc)" },
	{ OPT_SPEC512, NULL, "--spec512",
	  "<int>", "Spec512 palette threshold (0-512, 0=disable)" },
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
	  "<int>", "VDI mode bit-depth (1/2/4/8)" },
	{ OPT_VDI_WIDTH,     NULL, "--vdi-width",
	  "<int>", "VDI mode width (320-2048)" },
	{ OPT_VDI_HEIGHT,     NULL, "--vdi-height",
	  "<int>", "VDI mode height (160-1280)" },

	{ OPT_HEADER, NULL, NULL, NULL, "Screen capture" },
	{ OPT_SCREEN_CROP, NULL, "--crop",
	  "<bool>", "Remove statusbar from screen capture" },
	{ OPT_AVIRECORD, NULL, "--avirecord",
	  "<bool>", "Enable/disable AVI recording" },
	{ OPT_AVIRECORD_VCODEC, NULL, "--avi-vcodec",
	  "<x>", "Select AVI video codec (x = bmp/png)" },
	{ OPT_AVI_PNG_LEVEL, NULL, "--png-level",
	  "<int>", "Select AVI PNG compression level (0-9)" },
	{ OPT_AVIRECORD_FPS, NULL, "--avi-fps",
	  "<int>", "Force AVI frame rate (1-100, 50/60/71/...)" },
	{ OPT_AVIRECORD_FILE, NULL, "--avi-file",
	  "<file>", "Use <file> to record AVI" },
	{ OPT_SCRSHOT_DIR, NULL, "--screenshot-dir",
	  "<dir>", "Save screenshots in the directory <dir>" },
	{ OPT_SCRSHOT_FORMAT, NULL, "--screenshot-format",
	  "<x>", "Select file format (x = bmp/png/neo/ximg)" },

	{ OPT_HEADER, NULL, NULL, NULL, "Devices" },
	{ OPT_JOYSTICK,  "-j", "--joystick",
	  "<int>", "Emulate joystick with cursor keys in given port (0-5)" },
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
	  "<int>", "Set number of heads for drive A (1=single sided, 2=double sided)" },
	{ OPT_DRIVEB_HEADS, NULL, "--drive-b-heads",
	  "<int>", "Set number of heads for drive B (1=single sided, 2=double sided)" },
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
	{ OPT_SCSIVERSION,   NULL, "--scsi-ver",
	  "<id>=<version>", "Which SCSI version (1-2) to emulate for given drive ID" },
	{ OPT_IDEMASTERHDIMAGE,   NULL, "--ide-master",
	  "<file>", "Emulate an IDE 0 (master) harddrive with an image <file>" },
	{ OPT_IDESLAVEHDIMAGE,   NULL, "--ide-slave",
	  "<file>", "Emulate an IDE 1 (slave) harddrive with an image <file>" },
	{ OPT_IDEBYTESWAP,   NULL, "--ide-swap",
	  "<id>=<x>", "Set IDE (0/1) byte-swap option (off/on/auto)" },

	{ OPT_HEADER, NULL, NULL, NULL, "Memory" },
	{ OPT_MEMSIZE,   "-s", "--memsize",
	  "<int>", "ST RAM size (0-14 MiB, 0 = 512KiB ; else size in KiB)" },
	{ OPT_TT_RAM,   NULL, "--ttram",
	  "<int>", "TT RAM size (0-1024 MiB, in steps of 4)" },
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
	  "<int>", "Set the CPU clock (8/16/32)" },
	{ OPT_COMPATIBLE, NULL, "--compatible",
	  "<bool>", "Use (more compatible) prefetch mode for CPU" },
	{ OPT_CPU_DATA_CACHE, NULL, "--data-cache",
	  "<bool>", "Emulate (>=030) CPU data cache" },
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
	{ OPT_RTC_YEAR,   NULL, "--rtc-year",
	  "<int>", "Set initial year for RTC (0/1980-2079, 0=use host)" },
	{ OPT_TIMERD,    NULL, "--timer-d",
	  "<bool>", "Patch Timer-D (about doubles ST emulation speed)" },
	{ OPT_FASTBOOT, NULL, "--fast-boot",
	  "<bool>", "Patch TOS and memvalid system variables for faster boot" },

	{ OPT_HEADER, NULL, NULL, NULL, "Sound" },
	{ OPT_MICROPHONE,   NULL, "--mic",
	  "<bool>", "Enable/disable (Falcon only) microphone" },
	{ OPT_SOUND,   NULL, "--sound",
	  "<x>", "Sound frequency (off/6000-50066, off=fastest)" },
	{ OPT_SOUNDBUFFERSIZE,   NULL, "--sound-buffer-size",
	  "<int>", "Sound buffer size in ms (0/10-100, 0=default)" },
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
	{ OPT_SYMLOAD, NULL, "--symload",
	  "<mode>", "Program symbols autoloading mode (exec/debugger/off)" },
	{ OPT_LILO, NULL, "--lilo", "<x>", "Boot Linux (see manual page)" },
	{ OPT_BIOSINTERCEPT, NULL, "--bios-intercept",
	  "<bool>", "Enable/disable XBIOS command parsing support" },
	{ OPT_CONOUT,   NULL, "--conout",
	  "<int>", "Catch console device output (0-7, 2=VT-52 terminal)" },
	{ OPT_DISASM,   NULL, "--disasm",
	  "<x>", "Set disassembly options (help/uae/ext/<bitmask>)" },
	{ OPT_MEMCONV,   NULL, "--memconv",
	  "<bool>", "Enable locale conversion for non-ASCII Atari chars" },
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
	  "<int>", "Exit after <int> VBLs (1-)" },
	{ OPT_BENCHMARK, NULL, "--benchmark",
	  NULL, "Start in benchmark mode (use with --run-vbls)" },

	{ OPT_ERROR, NULL, NULL, NULL, NULL }
};


/**
 * Show version string and license.
 */
static void Opt_ShowVersion(void)
{
#ifdef WIN32
	/* Opt_ShowVersion() is called for all info exit paths,
	 * so having this here should enable console for everything
	 * relevant on Windows.
	 */
	Win_ForceCon();
#endif
	printf("\n" PROG_NAME
	       " - the Atari ST, STE, TT and Falcon emulator.\n\n"
	       "Hatari is free software licensed under the GNU General"
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
	printf("\nSpecial option values:\n"
	       "<bool>\tDisable by using 'n', 'no', 'off', 'false', or '0'\n"
	       "\tEnable by using 'y', 'yes', 'on', 'true' or '1'\n"
	       "<file>\tDevices accept also special 'stdout' and 'stderr' file names\n"
	       "\t(if you use stdout for midi or printer, set log to stderr).\n"
	       "<dir>/<file>\t'none' or '' disables given device / disk.\n"
	       "'<boot|inf|prg>:' event prefix delays option value setting.\n");
}


/**
 * Show Hatari version and usage.
 * If 'error' given, show that error message.
 * If 'optid' != OPT_ERROR, tells for which option the error is,
 * otherwise 'value' is show as the option user gave.
 * Return false if error string was given, otherwise true
 */
bool Opt_ShowError(int optid, const char *value, const char *error)
{
	const opt_t *opt;

	assert(optid > 0);  /* enum zero is OPT_HEADER */

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
				/* enum signedness is implementation dependent,
				 * only in C23 one could specify enum type
				 */
				if ((opt_id_t)optid == opt->id)
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
 * If <conf> given, set it to parsed (decimal) integer value.
 * Rounds value up to next <align>, for non-zero <align>.
 * Return false if parsing failed or value was out of specified
 * <min>-<max> range, otherwise return true
 */
static bool Opt_Int(const char *arg, opt_id_t optid, int *conf, int min, int max, int align)
{
	char *endptr;
	int value;

	errno = 0;
	value = strtol(arg, &endptr, 10);

	if (errno != 0)
		Opt_ShowError(optid, arg, strerror(errno));
	else if (endptr == arg || *endptr != '\0')
		Opt_ShowError(optid, arg, "not an integer");
	else if (value < min || value > max)
		Opt_ShowError(optid, arg, "<int> out of range");
	else
	{
		if (align > 1)
			value = ((value+align-1)/align)*align;
		*conf = value;
		return true;
	}
	return false;
}

/**
 * If 'conf' given, set it:
 * - true if given option 'arg' is y/yes/on/true/1
 * - false if given option 'arg' is n/no/off/false/0
 * Return false for any other value + show error, otherwise return true
 */
static bool Opt_Bool(const char *arg, opt_id_t optid, bool *conf)
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
 * Set 'conf' to a value matching the provided (case-insensitive)
 * 'key' in the provided 'keyval' array (of 'count' items).
 *
 * Return false if there's no match, otherwise true
 */
static bool Opt_SetKeyVal(const char *key, const opt_keyval_t *keyval, int count, int *conf)
{
	assert(conf);
	for (int i = 0; i < count; i++)
	{
		if (strcasecmp(key, keyval[i].key) == 0)
		{
			*conf = keyval[i].value;
			return true;
		}
	}
	return false;
}

/**
 * Set 'conf' to parsed country code value.
 * Return false for any other value, otherwise true
 */
static bool Opt_CountryCode(const char *arg, opt_id_t optid, int *conf)
{
	assert(conf);
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
 * Parse "<drive>=<value>". If single digit "<drive>" and/or '=' missing,
 * assume drive ID 0, and interpret whole arg as "<value>".
 * Return parsed "<value>", and set "<drive>".
 */
static const char *Opt_DriveValue(const char *arg, int *drive)
{
	if (strlen(arg) > 2 && isdigit((unsigned char)arg[0]) && arg[1] == '=')
	{
		*drive = arg[0] - '0';
		return arg + 2;
	}
	*drive = 0;
	return arg;
}


/**
 * checks str argument against options of type "--option<digit>".
 * If match is found, returns ID for that, otherwise OPT_CONTINUE
 * and OPT_ERROR for errors.
 */
static opt_id_t Opt_CheckBracketValue(const opt_t *opt, const char *str)
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
 * If option is supposed to have argument, checks that there's one,
 * and increments index accordingly on success.
 */
static opt_id_t Opt_WhichOption(int argc, const char * const argv[], int *idx)
{
	const opt_t *opt;
	const char *str = argv[*idx];
	opt_id_t id;

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
			int argi = *idx + 1;
			if (argi >= argc)
			{
				Opt_ShowError(opt->id, NULL, "Missing argument");
				return OPT_ERROR;
			}
			*idx = argi;
		}
		return opt->id;
	}
	Opt_ShowError(OPT_ERROR, argv[*idx], "Unrecognized option");
	return OPT_ERROR;
}


/**
 * Copy option 'path' value to 'dst' string, unless 'check' is requested
 * and given item does not exist.
 *
 * If a pointer to (bool) 'enabled' variable is given, set that true,
 * unless 'path' is "" (or "none"), in which case 'dst' is left unmodified,
 * and  'enabled' (= bool enabling given device) is set to false.
 *
 * Return false if there were errors, otherwise true
 */
static bool Opt_StrCpy(opt_id_t optid, fs_check_t check, char *dst, const char *path, size_t dstlen, bool *enabled)
{
	const char *error = NULL;

	switch (check)
	{
	case CHECK_NONE:
		break;
	case CHECK_FILE:
		if (!File_Exists(path))
		{
			error = "Given file does not exist, or permissions prevent access to it!";
		}
		break;
	case CHECK_DIR:
		if (!File_DirExists(path))
		{
			error = "Given directory does not exist, or permissions prevent access to it!";
		}
		break;
	}

	if (enabled)
	{
		*enabled = false;
		if(!*path)
		{
			/* "" disables unconditionally */
			return true;
		}
		if (error)
		{
			/* "none" disables when item does not exist */
			if (strcasecmp(path, "none") == 0)
			{
				return true;
			}
			return Opt_ShowError(optid, path, error);
		}
		/* no error => enable device option */
		*enabled = true;
	}
	else if (error)
	{
		return Opt_ShowError(optid, path, error);
	}

	if (strlen(path) >= dstlen)
	{
		return Opt_ShowError(optid, path, "Path too long!");
	}
	strcpy(dst, path);
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

	/* returns zero (= OPT_HEADER) for error, option ID for success */
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

	/* GEMDOS HDD directory (as path arg, or dir for the Atari program)? */
	if (File_DirExists(path))
	{
		Log_Printf(LOG_DEBUG, "ARG = GEMDOS HD dir: %s\n", path);
		if (Opt_StrCpy(OPT_HARDDRIVE, CHECK_NONE, ConfigureParams.HardDisk.szHardDiskDirectories[0],
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
	/* something wrong if path to an existing prg has no valid dir */
	assert(!dir);

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
 * Returns true if everything was OK to continue, false not. In latter case
 * "exitval" is set to suitable exit value.
 */
bool Opt_ParseParameters(int argc, const char * const argv[], int *exitval)
{
	/* variables used only by a 1-2 options */
	int drive, level, port;

	/* common variables */
	const char *errstr, *str, *opt, *arg;
	bool valid, enabled, ok = true;
	event_actions_t *event;
	opt_id_t optid;
	float zoom;
	size_t len;
	int i, val;

	/* Defaults for loading initial memory snap-shots */
	bLoadMemorySave = false;
	bLoadAutoSave = ConfigureParams.Memory.bAutoSave;

	/* when false is returned, it's by default an error */
	*exitval = 1;

	for(i = 1; i < argc; i++)
	{
		/* last argument can be a non-option */
		if (argv[i][0] != '-' && i+1 == argc)
			return Opt_HandleArgument(argv[i]) && Opt_ValidateOptions();

		/* WhichOption() checks also that there is an argument,
		 * for options that need one, so we don't need to check
		 * that below.  It also increments it automatically.
		 */
		opt = argv[i];
		optid = Opt_WhichOption(argc, argv, &i);
		arg = argv[i];

		switch(optid)
		{
			/* general options */
		case OPT_HELP:
			Opt_ShowHelp();
			*exitval = 0;
			return false;

		case OPT_VERSION:
			Opt_ShowVersion();
			*exitval = 0;
			return false;

		case OPT_CONFIRMQUIT:
			ok = Opt_Bool(arg, OPT_CONFIRMQUIT, &ConfigureParams.Log.bConfirmQuit);
			break;

		case OPT_FASTFORWARD:
			event = Event_GetPrefixActions(&arg);
			if (!Opt_Bool(arg, OPT_FASTFORWARD, &enabled))
			{
				return false;
			}
			if (event)
			{
				event->fastForward = enabled;
				break;
			}
			Log_Printf(LOG_DEBUG, "Fast forward = %s.\n", arg);
			ConfigureParams.System.bFastForward = enabled;
			break;

		case OPT_AUTOSTART:
			if (!(ok = INF_SetAutoStart(arg, OPT_AUTOSTART)))
			{
				return Opt_ShowError(OPT_AUTOSTART, arg, "Invalid drive and/or path specified for autostart program");
			}
			break;

		case OPT_FF_KEY_REPEAT:
			ok = Opt_Bool(arg, OPT_FF_KEY_REPEAT, &ConfigureParams.Keyboard.bFastForwardKeyRepeat);
			break;

		case OPT_CONFIGFILE:
			ok = Opt_StrCpy(OPT_CONFIGFILE, CHECK_FILE, sConfigFileName,
					arg, sizeof(sConfigFileName), NULL);
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
		{
			static const opt_keyval_t keyval[] = {
				{"mono", MONITOR_TYPE_MONO},
				{"rgb",  MONITOR_TYPE_RGB},
				{"vga",  MONITOR_TYPE_VGA},
				{"tv",   MONITOR_TYPE_TV},
			};
			if (!Opt_SetKeyVal(arg, keyval, ARRAY_SIZE(keyval), &val))
			{
				return Opt_ShowError(OPT_MONITOR, arg, "Unknown monitor type");
			}
			ConfigureParams.Screen.nMonitorType = val;
			bLoadAutoSave = false;
			break;
		}

		case OPT_TOS_RESOLUTION:
			if (!INF_SetResolution(arg, OPT_TOS_RESOLUTION))
			{
				return Opt_ShowError(OPT_TOS_RESOLUTION, arg, "Invalid resolution");
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
			ok = Opt_Bool(arg, OPT_RESIZABLE, &ConfigureParams.Screen.bResizable);
			break;

		case OPT_FRAMESKIPS:
			event = Event_GetPrefixActions(&arg);
			if (!Opt_Int(arg, OPT_FRAMESKIPS, &val, 0, 64, 0))
			{
				return false;
			}
			if (val > 8)
			{
				Log_Printf(LOG_WARN, "Extravagant frame skip value %d!\n", val);
			}
			if (event)
			{
				event->frameSkips = val;
				break;
			}
			ConfigureParams.Screen.nFrameSkips = val;
			break;

		case OPT_SLOWDOWN:
			event = Event_GetPrefixActions(&arg);
			if (!Opt_Int(arg, OPT_SLOWDOWN, &val, 1, 30, 0))
			{
				return false;
			}
			if (event)
			{
				event->slowDown = val;
				break;
			}
			errstr = Timing_SetVBLSlowdown(val);
			if (errstr)
			{
				return Opt_ShowError(OPT_SLOWDOWN, arg, errstr);
			}
			break;

		case OPT_MOUSE_WARP:
			ok = Opt_Bool(arg, OPT_MOUSE_WARP, &ConfigureParams.Screen.bMouseWarp);
			break;

		case OPT_STATUSBAR:
			ok = Opt_Bool(arg, OPT_STATUSBAR, &ConfigureParams.Screen.bShowStatusbar);
			break;

		case OPT_DRIVE_LED:
			ok = Opt_Bool(arg, OPT_DRIVE_LED, &ConfigureParams.Screen.bShowDriveLed);
			break;

		case OPT_VSYNC:
			ok = Opt_Bool(arg, OPT_VSYNC, &ConfigureParams.Screen.bUseVsync);
			break;

		case OPT_DISABLE_VIDEO:
			ok = Opt_Bool(arg, OPT_DISABLE_VIDEO, &ConfigureParams.Screen.DisableVideo);
			break;

			/* ST/STE display options */
		case OPT_BORDERS:
			ok = Opt_Bool(arg, OPT_BORDERS, &ConfigureParams.Screen.bAllowOverscan);
			break;

		case OPT_SPEC512:
			ok = Opt_Int(arg, OPT_SLOWDOWN, &ConfigureParams.Screen.nSpec512Threshold, 0, 512, 0);
			break;

		case OPT_ZOOM:
			zoom = atof(arg);
			if (zoom < 1.0 || zoom > 8.0)
			{
				return Opt_ShowError(OPT_ZOOM, arg, "Invalid zoom value");
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
		{
			static const opt_keyval_t keyval[] = {
				{"random", VIDEO_TIMING_MODE_RANDOM},
				{"ws1", VIDEO_TIMING_MODE_WS1},
				{"ws2", VIDEO_TIMING_MODE_WS2},
				{"ws3", VIDEO_TIMING_MODE_WS3},
				{"ws4", VIDEO_TIMING_MODE_WS4},
			};
			if (!Opt_SetKeyVal(arg, keyval, ARRAY_SIZE(keyval), &val))
			{
				return Opt_ShowError(OPT_VIDEO_TIMING, arg, "Unknown video timing mode");
			}
			ConfigureParams.System.VideoTimingMode = val;
			break;
		}

			/* Falcon/TT display options */
		case OPT_RESOLUTION:
			ok = Opt_Bool(arg, OPT_RESOLUTION, &ConfigureParams.Screen.bKeepResolution);
			break;

		case OPT_MAXWIDTH:
			ok = Opt_Int(arg, OPT_MAXWIDTH, &ConfigureParams.Screen.nMaxWidth, 320, INT_MAX, 0);
			break;

		case OPT_MAXHEIGHT:
			ok = Opt_Int(arg, OPT_MAXHEIGHT, &ConfigureParams.Screen.nMaxHeight, 200, INT_MAX, 0);
			break;

		case OPT_FORCE_MAX:
			ok = Opt_Bool(arg, OPT_FORCE_MAX, &ConfigureParams.Screen.bForceMax);
			break;

		case OPT_ASPECT:
			ok = Opt_Bool(arg, OPT_ASPECT, &ConfigureParams.Screen.bAspectCorrect);
			break;

			/* screen capture options */
		case OPT_SCREEN_CROP:
			ok = Opt_Bool(arg, OPT_SCREEN_CROP, &ConfigureParams.Screen.bCrop);
			break;

		case OPT_AVIRECORD:
			event = Event_GetPrefixActions(&arg);
			if (!Opt_Bool(arg, OPT_AVIRECORD, &enabled))
			{
				return false;
			}
			if (event)
			{
				event->aviRecord = enabled;
				break;
			}
			Log_Printf(LOG_DEBUG, "AVI Recording = %s.\n", arg);
			if (!enabled && Avi_AreWeRecording())
			{
				Avi_ToggleRecording();
			}
			else
			{
				/* must assume it's Hatari startup */
				AviRecordEnabled = enabled;
			}
			break;

		case OPT_AVIRECORD_VCODEC:
		{
			static const opt_keyval_t keyval[] = {
				{"bmp", AVI_RECORD_VIDEO_CODEC_BMP},
				{"png", AVI_RECORD_VIDEO_CODEC_PNG},
			};
			if (!Opt_SetKeyVal(arg, keyval, ARRAY_SIZE(keyval), &val))
			{
				return Opt_ShowError(OPT_AVIRECORD_VCODEC, arg, "Unknown video codec");
			}
			ConfigureParams.Video.AviRecordVcodec = val;
			break;
		}

		case OPT_AVI_PNG_LEVEL:
			if (!Opt_Int(arg, OPT_AVI_PNG_LEVEL, &val, 0, 9, 0))
			{
				return false;
			}
			Avi_SetCompressionLevel(val);
			break;

		case OPT_AVIRECORD_FPS:
			ok = Opt_Int(arg, OPT_AVIRECORD_FPS, &ConfigureParams.Video.AviRecordFps, 1, 100, 0);
			break;

		case OPT_AVIRECORD_FILE:
			ok = Opt_StrCpy(OPT_AVIRECORD_FILE, CHECK_NONE, ConfigureParams.Video.AviRecordFile,
					arg, sizeof(ConfigureParams.Video.AviRecordFile), NULL);
			break;

		case OPT_SCRSHOT_DIR:
			ok = Opt_StrCpy(OPT_SCRSHOT_DIR, CHECK_DIR, ConfigureParams.Screen.szScreenShotDir,
					arg, sizeof(ConfigureParams.Screen.szScreenShotDir), NULL);
			break;

		case OPT_SCRSHOT_FORMAT:
		{
			static const opt_keyval_t keyval[] = {
				{"bmp",  SCREEN_SNAPSHOT_BMP},
				{"png",  SCREEN_SNAPSHOT_PNG},
				{"neo",  SCREEN_SNAPSHOT_NEO},
				{"ximg", SCREEN_SNAPSHOT_XIMG},
			};
			if (!Opt_SetKeyVal(arg, keyval, ARRAY_SIZE(keyval), &val))
			{
				return Opt_ShowError(OPT_SCRSHOT_FORMAT, arg, "Unknown screenshot format");
			}
			ConfigureParams.Screen.ScreenShotFormat = val;
			break;
		}

			/* VDI options */
		case OPT_VDI:
			ok = Opt_Bool(arg, OPT_VDI, &ConfigureParams.Screen.bUseExtVdiResolutions);
			if (ok)
			{
				bLoadAutoSave = false;
			}
			break;

		case OPT_VDI_PLANES:
			if (!Opt_Int(arg, OPT_VDI_PLANES, &val, 1, 8, 0))
			{
				return false;
			}
			switch(val)
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
			 case 8:
				ConfigureParams.Screen.nVdiColors = GEMCOLOR_256;
				break;
			 default:
				return Opt_ShowError(OPT_VDI_PLANES, arg, "Unsupported VDI bit-depth");
			}
			ConfigureParams.Screen.bUseExtVdiResolutions = true;
			bLoadAutoSave = false;
			break;

		case OPT_VDI_WIDTH:
			ok = Opt_Int(arg, OPT_VDI_WIDTH, &ConfigureParams.Screen.nVdiWidth, 320, 2048, 16);
			ConfigureParams.Screen.bUseExtVdiResolutions = true;
			bLoadAutoSave = false;
			break;

		case OPT_VDI_HEIGHT:
			ok = Opt_Int(arg, OPT_VDI_HEIGHT, &ConfigureParams.Screen.nVdiHeight, 160, 1280, 8);
			ConfigureParams.Screen.bUseExtVdiResolutions = true;
			bLoadAutoSave = false;
			break;

			/* devices options */
		case OPT_JOYSTICK:
			if(!Opt_Int(arg, OPT_JOYSTICK, &val, 0, JOYSTICK_COUNT-1, 0))
			{
				return false;
			}
			Joy_SetCursorEmulation(val);
			break;

		case OPT_JOYSTICK0:
		case OPT_JOYSTICK1:
		case OPT_JOYSTICK2:
		case OPT_JOYSTICK3:
		case OPT_JOYSTICK4:
		case OPT_JOYSTICK5:
		{
			port = opt[strlen(opt)-1] - '0';
			if (port < 0 || port >= JOYSTICK_COUNT)
			{
				return Opt_ShowError(OPT_JOYSTICK0, opt, "Invalid joystick port");
			}
			static const opt_keyval_t keyval[] = {
				{"none", JOYSTICK_DISABLED},
				{"off",  JOYSTICK_DISABLED},
				{"keys", JOYSTICK_KEYBOARD},
				{"real", JOYSTICK_REALSTICK},
			};
			if (!Opt_SetKeyVal(arg, keyval, ARRAY_SIZE(keyval), &val))
			{
				return Opt_ShowError(OPT_JOYSTICK0+port, arg, "Invalid joystick type");
			}
			ConfigureParams.Joysticks.Joy[port].nJoystickMode = val;
			break;
		}

		case OPT_PRINTER:
			ok = Opt_StrCpy(OPT_PRINTER, CHECK_NONE, ConfigureParams.Printer.szPrintToFileName,
					arg, sizeof(ConfigureParams.Printer.szPrintToFileName),
					&ConfigureParams.Printer.bEnablePrinting);
			break;

#ifdef HAVE_PORTMIDI
		case OPT_MIDI:
			ok = Opt_Bool(arg, OPT_MIDI, &ConfigureParams.Midi.bEnableMidi);
			break;
#else
		case OPT_MIDI_IN:
			ok = Opt_StrCpy(OPT_MIDI_IN, CHECK_FILE, ConfigureParams.Midi.sMidiInFileName,
					arg, sizeof(ConfigureParams.Midi.sMidiInFileName),
					&ConfigureParams.Midi.bEnableMidi);
			break;

		case OPT_MIDI_OUT:
			ok = Opt_StrCpy(OPT_MIDI_OUT, CHECK_NONE, ConfigureParams.Midi.sMidiOutFileName,
					arg, sizeof(ConfigureParams.Midi.sMidiOutFileName),
					&ConfigureParams.Midi.bEnableMidi);
			break;
#endif

		case OPT_RS232_IN:
			ok = Opt_StrCpy(OPT_RS232_IN, CHECK_FILE, ConfigureParams.RS232.szInFileName,
					arg, sizeof(ConfigureParams.RS232.szInFileName),
					&ConfigureParams.RS232.bEnableRS232);
			break;

		case OPT_RS232_OUT:
			ok = Opt_StrCpy(OPT_RS232_OUT, CHECK_NONE, ConfigureParams.RS232.szOutFileName,
					arg, sizeof(ConfigureParams.RS232.szOutFileName),
					&ConfigureParams.RS232.bEnableRS232);
			break;

		case OPT_SCCA_IN:
			ok = Opt_StrCpy(OPT_SCCA_IN, CHECK_FILE, ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_A_SERIAL],
					arg, sizeof(ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_A_SERIAL]),
					&ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_A_SERIAL]);
			break;
		case OPT_SCCA_OUT:
			ok = Opt_StrCpy(OPT_SCCA_OUT, CHECK_NONE, ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_A_SERIAL],
					arg, sizeof(ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_A_SERIAL]),
					&ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_A_SERIAL]);
			break;
		case OPT_SCCA_LAN_IN:
			ok = Opt_StrCpy(OPT_SCCA_LAN_IN, CHECK_FILE, ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_A_LAN],
					arg, sizeof(ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_A_LAN]),
					&ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_A_LAN]);
			break;
		case OPT_SCCA_LAN_OUT:
			ok = Opt_StrCpy(OPT_SCCA_LAN_OUT, CHECK_NONE, ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_A_LAN],
					arg, sizeof(ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_A_LAN]),
					&ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_A_LAN]);
			break;
		case OPT_SCCB_IN:
			ok = Opt_StrCpy(OPT_SCCB_IN, CHECK_FILE, ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_B],
					arg, sizeof(ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_B]),
					&ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_B]);
			break;
		case OPT_SCCB_OUT:
			ok = Opt_StrCpy(OPT_SCCB_OUT, CHECK_NONE, ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_B],
					arg, sizeof(ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_B]),
					&ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_B]);
			break;

			/* disk options */
		case OPT_DRIVEA:
			ok = Opt_Bool(arg, OPT_DRIVEA, &ConfigureParams.DiskImage.EnableDriveA);
			break;

		case OPT_DRIVEB:
			ok = Opt_Bool(arg, OPT_DRIVEB, &ConfigureParams.DiskImage.EnableDriveB);
			break;

		case OPT_DRIVEA_HEADS:
			ok = Opt_Int(arg, OPT_DRIVEA_HEADS, &ConfigureParams.DiskImage.DriveA_NumberOfHeads, 1, 2, 0);
			break;

		case OPT_DRIVEB_HEADS:
			ok = Opt_Int(arg, OPT_DRIVEB_HEADS, &ConfigureParams.DiskImage.DriveB_NumberOfHeads, 1, 2, 0);
			break;

		case OPT_DISKA:
			if (Floppy_SetDiskFileName(0, arg, NULL))
			{
				ConfigureParams.HardDisk.bBootFromHardDisk = false;
				bLoadAutoSave = false;
			}
			else
				return Opt_ShowError(OPT_ERROR, arg, "Not a disk image");
			break;

		case OPT_DISKB:
			if (Floppy_SetDiskFileName(1, arg, NULL))
				bLoadAutoSave = false;
			else
				return Opt_ShowError(OPT_ERROR, arg, "Not a disk image");
			break;

		case OPT_FASTFLOPPY:
			ok = Opt_Bool(arg, OPT_FASTFLOPPY, &ConfigureParams.DiskImage.FastFloppy);
			break;

		case OPT_WRITEPROT_FLOPPY:
		{
			static const opt_keyval_t keyval[] = {
				{"off",  WRITEPROT_OFF},
				{"on",   WRITEPROT_ON},
				{"auto", WRITEPROT_AUTO},
			};
			if (!Opt_SetKeyVal(arg, keyval, ARRAY_SIZE(keyval), &val))
			{
				return Opt_ShowError(OPT_WRITEPROT_FLOPPY, arg, "Unknown option value");
			}
			ConfigureParams.DiskImage.nWriteProtection = val;
			break;
		}

		case OPT_WRITEPROT_HD:
		{
			static const opt_keyval_t keyval[] = {
				{"off",  WRITEPROT_OFF},
				{"on",   WRITEPROT_ON},
				{"auto", WRITEPROT_AUTO},
			};
			if (!Opt_SetKeyVal(arg, keyval, ARRAY_SIZE(keyval), &val))
			{
				return Opt_ShowError(OPT_WRITEPROT_HD, arg, "Unknown option value");
			}
			ConfigureParams.HardDisk.nWriteProtection = val;
			break;
		}

		case OPT_GEMDOS_CASE:
		{
			static const opt_keyval_t keyval[] = {
				{"off",   GEMDOS_NOP},
				{"upper", GEMDOS_UPPER},
				{"lower", GEMDOS_LOWER},
			};
			if (!Opt_SetKeyVal(arg, keyval, ARRAY_SIZE(keyval), &val))
			{
				return Opt_ShowError(OPT_GEMDOS_CASE, arg, "Unknown option value");
			}
			ConfigureParams.HardDisk.nGemdosCase = val;
			break;
		}

		case OPT_GEMDOS_HOSTTIME:
			if (strcasecmp(arg, "atari") == 0)
				ConfigureParams.HardDisk.bGemdosHostTime = false;
			else if (strcasecmp(arg, "host") == 0)
				ConfigureParams.HardDisk.bGemdosHostTime = true;
			else
				return Opt_ShowError(OPT_GEMDOS_HOSTTIME, arg, "Unknown option value");
			break;

		case OPT_GEMDOS_CONVERT:
			ok = Opt_Bool(arg, OPT_GEMDOS_CONVERT, &ConfigureParams.HardDisk.bFilenameConversion);
			break;

		case OPT_GEMDOS_DRIVE:
			if (strcasecmp(arg, "skip") == 0)
			{
				ConfigureParams.HardDisk.nGemdosDrive = DRIVE_SKIP;
				break;
			}
			else if (strlen(arg) == 1)
			{
				drive = toupper(arg[0]);
				if (drive >= 'C' && drive <= 'Z')
				{
					drive = drive - 'C' + DRIVE_C;
					ConfigureParams.HardDisk.nGemdosDrive = drive;
					break;
				}
			}
			return Opt_ShowError(OPT_GEMDOS_DRIVE, arg, "Invalid <drive>");

		case OPT_HARDDRIVE:
			ok = Opt_StrCpy(OPT_HARDDRIVE, CHECK_DIR, ConfigureParams.HardDisk.szHardDiskDirectories[0],
					arg, sizeof(ConfigureParams.HardDisk.szHardDiskDirectories[0]),
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
			str = Opt_DriveValue(arg, &drive);
			if (drive < 0 || drive >= MAX_ACSI_DEVS)
				return Opt_ShowError(OPT_ACSIHDIMAGE, str, "Invalid ACSI drive <id>, must be 0-7");

			ok = Opt_StrCpy(OPT_ACSIHDIMAGE, CHECK_FILE, ConfigureParams.Acsi[drive].sDeviceFile,
					str, sizeof(ConfigureParams.Acsi[drive].sDeviceFile),
					&ConfigureParams.Acsi[drive].bUseDevice);
			if (ok)
			{
				bLoadAutoSave = false;
			}
			break;

		case OPT_SCSIHDIMAGE:
			str = Opt_DriveValue(arg, &drive);
			if (drive < 0 || drive >= MAX_SCSI_DEVS)
				return Opt_ShowError(OPT_SCSIHDIMAGE, str, "Invalid SCSI drive <id>, must be 0-7");

			ok = Opt_StrCpy(OPT_SCSIHDIMAGE, CHECK_FILE, ConfigureParams.Scsi[drive].sDeviceFile,
					str, sizeof(ConfigureParams.Scsi[drive].sDeviceFile),
					&ConfigureParams.Scsi[drive].bUseDevice);
			if (ok)
			{
				bLoadAutoSave = false;
			}
			break;

		case OPT_SCSIVERSION:
			str = Opt_DriveValue(arg, &drive);
			if (drive < 0 || drive >= MAX_SCSI_DEVS)
			{
				return Opt_ShowError(OPT_SCSIVERSION, str, "Invalid SCSI drive <id>, must be 0-7");
			}
			val = atoi(str);
			if(val != 1 && val != 2)
			{
				return Opt_ShowError(OPT_SCSIVERSION, arg, "Invalid SCSI version");
			}
			ConfigureParams.Scsi[drive].nScsiVersion = val;
			break;

		case OPT_IDEMASTERHDIMAGE:
			ok = Opt_StrCpy(OPT_IDEMASTERHDIMAGE, CHECK_FILE, ConfigureParams.Ide[0].sDeviceFile,
					arg, sizeof(ConfigureParams.Ide[0].sDeviceFile),
					&ConfigureParams.Ide[0].bUseDevice);
			if (ok)
			{
				bLoadAutoSave = false;
			}
			break;

		case OPT_IDESLAVEHDIMAGE:
			ok = Opt_StrCpy(OPT_IDESLAVEHDIMAGE, CHECK_FILE, ConfigureParams.Ide[1].sDeviceFile,
					arg, sizeof(ConfigureParams.Ide[1].sDeviceFile),
					&ConfigureParams.Ide[1].bUseDevice);
			if (ok)
			{
				bLoadAutoSave = false;
			}
			break;

		case OPT_IDEBYTESWAP:
		{
			str = Opt_DriveValue(arg, &drive);
			if (drive < 0 || drive > 1)
			{
				return Opt_ShowError(OPT_IDEBYTESWAP, str, "Invalid IDE drive <id>, must be 0/1");
			}
			static const opt_keyval_t keyval[] = {
				{"off",  BYTESWAP_OFF},
				{"on",   BYTESWAP_ON},
				{"auto", BYTESWAP_AUTO},
			};
			if (!Opt_SetKeyVal(str, keyval, ARRAY_SIZE(keyval), &val))
			{
				return Opt_ShowError(OPT_IDEBYTESWAP, arg, "Invalid byte-swap setting");
			}
			ConfigureParams.Ide[drive].nByteSwap = val;
			break;
		}

			/* Memory options */
		case OPT_MEMSIZE:
			if (!Opt_Int(arg, OPT_MEMSIZE, &val, 0, 1024, 0))
			{
				return false;
			}
			val = STMemory_RAM_Validate_Size_KB ( val );
			if (val < 0)
			{
				return Opt_ShowError(OPT_MEMSIZE, arg, "Invalid memory size");
			}
			ConfigureParams.Memory.STRamSize_KB = val;
			bLoadAutoSave = false;
			break;

		case OPT_TT_RAM:
			ok = Opt_Int(arg, OPT_TT_RAM, &val, 0, 1024, 4);
			ConfigureParams.Memory.TTRamSize_KB = val * 1024;
			bLoadAutoSave = false;
			break;

		case OPT_TOS:
			ok = Opt_StrCpy(OPT_TOS, CHECK_FILE, ConfigureParams.Rom.szTosImageFileName,
					arg, sizeof(ConfigureParams.Rom.szTosImageFileName),
					&bUseTos);
			if (ok || !bUseTos)
			{
				bLoadAutoSave = false;
			}
			break;

		case OPT_PATCHTOS:
			ok = Opt_Bool(arg, OPT_PATCHTOS, &ConfigureParams.Rom.bPatchTos);
			break;

		case OPT_CARTRIDGE:
			ok = Opt_StrCpy(OPT_CARTRIDGE, CHECK_FILE, ConfigureParams.Rom.szCartridgeImageFileName,
					arg, sizeof(ConfigureParams.Rom.szCartridgeImageFileName),
					NULL);
			if (ok)
			{
				bLoadAutoSave = false;
			}
			break;

		case OPT_MEMSTATE:
			ok = Opt_StrCpy(OPT_MEMSTATE, CHECK_FILE, ConfigureParams.Memory.szMemoryCaptureFileName,
					arg, sizeof(ConfigureParams.Memory.szMemoryCaptureFileName),
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
			if (!Opt_Int(arg, OPT_CPULEVEL, &val, 0, 6, 0))
			{
				return false;
			}
			if(val == 5)
			{
				return Opt_ShowError(OPT_CPULEVEL, arg, "Invalid CPU level");
			}
			if (val == 6)	/* Special case for 68060, nCpuLevel should be 5 */
				val = 5;
			ConfigureParams.System.nCpuLevel = val;
			bLoadAutoSave = false;
			break;

		case OPT_CPUCLOCK:
			if (!Opt_Int(arg, OPT_CPUCLOCK, &val, 8, 32, 0))
			{
				return false;
			}
			if(val != 8 && val != 16 && val != 32)
			{
				return Opt_ShowError(OPT_CPUCLOCK, arg, "Invalid CPU clock");
			}
			Configuration_ChangeCpuFreq(val);
			bLoadAutoSave = false;
			break;

		case OPT_COMPATIBLE:
			ok = Opt_Bool(arg, OPT_COMPATIBLE, &ConfigureParams.System.bCompatibleCpu);
			if (ok)
			{
				bLoadAutoSave = false;
			}
			break;
		case OPT_CPU_ADDR24:
			ok = Opt_Bool(arg, OPT_CPU_ADDR24, &ConfigureParams.System.bAddressSpace24);
			bLoadAutoSave = false;
			break;

		case OPT_CPU_DATA_CACHE:
			ok = Opt_Bool(arg, OPT_CPU_DATA_CACHE, &ConfigureParams.System.bCpuDataCache);
			bLoadAutoSave = false;
			break;

		case OPT_CPU_CYCLE_EXACT:
			ok = Opt_Bool(arg, OPT_CPU_CYCLE_EXACT, &ConfigureParams.System.bCycleExactCpu);
			bLoadAutoSave = false;
			break;

		case OPT_FPU_TYPE:
		{
			static const opt_keyval_t keyval[] = {
				{"none",  FPU_NONE},
				{"off",   FPU_NONE},
				{"68881", FPU_68881},
				{"68882", FPU_68882},
				{"internal", FPU_CPU},
			};
			if (!Opt_SetKeyVal(arg, keyval, ARRAY_SIZE(keyval), &val))
			{
				return Opt_ShowError(OPT_FPU_TYPE, arg, "Unknown FPU type");
			}
			ConfigureParams.System.n_FPUType = val;
			bLoadAutoSave = false;
			break;
		}
/*
		case OPT_FPU_JIT_COMPAT:
			ok = Opt_Bool(arg, OPT_FPU_COMPATIBLE, &ConfigureParams.System.bCompatibleFPU);
			break;
*/
		case OPT_FPU_SOFTFLOAT:
			ok = Opt_Bool(arg, OPT_FPU_SOFTFLOAT, &ConfigureParams.System.bSoftFloatFPU);
			break;

		case OPT_MMU:
			ok = Opt_Bool(arg, OPT_MMU, &ConfigureParams.System.bMMU);
			bLoadAutoSave = false;
			break;

			/* system options */
		case OPT_MACHINE:
			if (strcasecmp(arg, "st") == 0)
			{
				ConfigureParams.System.nMachineType = MACHINE_ST;
				ConfigureParams.System.nCpuLevel = 0;
				Configuration_ChangeCpuFreq ( 8 );
			}
			else if (strcasecmp(arg, "megast") == 0)
			{
				ConfigureParams.System.nMachineType = MACHINE_MEGA_ST;
				ConfigureParams.System.nCpuLevel = 0;
				Configuration_ChangeCpuFreq ( 8 );
			}
			else if (strcasecmp(arg, "ste") == 0)
			{
				ConfigureParams.System.nMachineType = MACHINE_STE;
				ConfigureParams.System.nCpuLevel = 0;
				Configuration_ChangeCpuFreq ( 8 );
			}
			else if (strcasecmp(arg, "megaste") == 0)
			{
				ConfigureParams.System.nMachineType = MACHINE_MEGA_STE;
				ConfigureParams.System.nCpuLevel = 0;
				Configuration_ChangeCpuFreq ( 8 );
			}
			else if (strcasecmp(arg, "tt") == 0)
			{
				ConfigureParams.System.nMachineType = MACHINE_TT;
				ConfigureParams.System.nCpuLevel = 3;
				Configuration_ChangeCpuFreq ( 32 );
			}
			else if (strcasecmp(arg, "falcon") == 0)
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
				return Opt_ShowError(OPT_MACHINE, arg, "Unknown machine type");
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
			ok = Opt_Bool(arg, OPT_BLITTER, &ConfigureParams.System.bBlitter);
			if (ok)
			{
				bLoadAutoSave = false;
			}
			break;

		case OPT_TIMERD:
			ok = Opt_Bool(arg, OPT_TIMERD, &ConfigureParams.System.bPatchTimerD);
			break;

		case OPT_FASTBOOT:
			ok = Opt_Bool(arg, OPT_FASTBOOT, &ConfigureParams.System.bFastBoot);
			break;

		case OPT_DSP:
		{
			static const opt_keyval_t keyval[] = {
				{"none",  DSP_TYPE_NONE},
				{"off",   DSP_TYPE_NONE},
				{"dummy", DSP_TYPE_DUMMY},
				{"emu",  DSP_TYPE_EMU},
			};
			if (!Opt_SetKeyVal(arg, keyval, ARRAY_SIZE(keyval), &val))
			{
				return Opt_ShowError(OPT_DSP, arg, "Unknown DSP type");
			}
#if !ENABLE_DSP_EMU
			if (val == DSP_TYPE_EMU)
			{
				return Opt_ShowError(OPT_DSP, arg, "DSP type 'emu' support not compiled in");
			}
#endif
			ConfigureParams.System.nDSPType = val;
			bLoadAutoSave = false;
			break;
		}

		case OPT_RTC_YEAR:
			if (!Opt_Int(arg, OPT_RTC_YEAR, &val, 0, 2079, 0))
			{
				return false;
			}
			if(val && val < 1980)
			{
				return Opt_ShowError(OPT_RTC_YEAR, arg, "Invalid RTC year");
			}
			ConfigureParams.System.nRtcYear = val;
			break;

			/* sound options */
		case OPT_YM_MIXING:
		{
			static const opt_keyval_t keyval[] = {
				{"linear",  YM_LINEAR_MIXING},
				{"table",   YM_TABLE_MIXING},
				{"model",   YM_MODEL_MIXING},
			};
			if (!Opt_SetKeyVal(arg, keyval, ARRAY_SIZE(keyval), &val))
			{
				return Opt_ShowError(OPT_YM_MIXING, arg, "Unknown YM mixing method");
			}
			ConfigureParams.Sound.YmVolumeMixing = val;
			break;
		}

		case OPT_SOUND:
			if (strcasecmp(arg, "off") == 0)
			{
				ConfigureParams.Sound.bEnableSound = false;
			}
			else
			{
				if (!Opt_Int(arg, OPT_SOUND, &val, 6000, 50066, 0))
				{
					return false;
				}
				ConfigureParams.Sound.nPlaybackFreq = val;
				ConfigureParams.Sound.bEnableSound = true;
			}
			Log_Printf(LOG_DEBUG, "Sound %s, frequency = %d.\n",
			        ConfigureParams.Sound.bEnableSound ? "ON" : "OFF",
			        ConfigureParams.Sound.nPlaybackFreq);
			break;

		case OPT_SOUNDBUFFERSIZE:
			if (!Opt_Int(arg, OPT_SOUNDBUFFERSIZE, &val, 0, 100, 0))
			{
				return false;
			}
			if (val && val < 10)
			{
				return Opt_ShowError(OPT_SOUNDBUFFERSIZE, arg, "Unsupported sound buffer size");
			}
			Log_Printf(LOG_DEBUG, "Sound buffer size = %d ms.\n", val);
			ConfigureParams.Sound.SdlAudioBufferSize = val;
			break;

		case OPT_SOUNDSYNC:
			ok = Opt_Bool(arg, OPT_SOUNDSYNC, &ConfigureParams.Sound.bEnableSoundSync);
			break;

		case OPT_MICROPHONE:
			ok = Opt_Bool(arg, OPT_MICROPHONE, &ConfigureParams.Sound.bEnableMicrophone);
			break;

		case OPT_COUNTRY_CODE:
			ok = Opt_CountryCode(arg, OPT_COUNTRY_CODE, &ConfigureParams.Keyboard.nCountryCode);
			break;

		case OPT_LANGUAGE:
			ok = Opt_CountryCode(arg, OPT_LANGUAGE, &ConfigureParams.Keyboard.nLanguage);
			break;

		case OPT_KBD_LAYOUT:
			ok = Opt_CountryCode(arg, OPT_KBD_LAYOUT, &ConfigureParams.Keyboard.nKbdLayout);
			break;

		case OPT_KEYMAPFILE:
			ok = Opt_StrCpy(OPT_KEYMAPFILE, CHECK_FILE, ConfigureParams.Keyboard.szMappingFileName,
					arg, sizeof(ConfigureParams.Keyboard.szMappingFileName),
					&valid);
			if (ok && !valid)
			{
				ConfigureParams.Keyboard.szMappingFileName[0] = 0;
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
			event = Event_GetPrefixActions(&arg);
			errstr = Log_CheckExceptionDebugMask(arg);
			if (errstr)
			{
				if (!errstr[0])
				{
					/* silent parsing termination */
					return false;
				}
				return Opt_ShowError(OPT_EXCEPTIONS, arg, errstr);
			}
			if (event)
			{
				event->exceptionMask = arg;
				break;
			}

			Log_SetExceptionDebugMask(arg);
			if (ExceptionDebugMask)
			{
				/* already enabled, change run-time config */
				int oldmask = ExceptionDebugMask;
				ExceptionDebugMask = ConfigureParams.Debugger.nExceptionDebugMask;
				Log_Printf(LOG_INFO, "Exception debugging changed (0x%x -> 0x%x).\n",
				        oldmask, ExceptionDebugMask);
			}
			break;

		case OPT_SYMLOAD:
		{
			static const opt_keyval_t keyval[] = {
				{"off",      SYM_AUTOLOAD_OFF},
				{"debugger", SYM_AUTOLOAD_DEBUGGER},
				{"exec",     SYM_AUTOLOAD_EXEC},
			};
			if (!Opt_SetKeyVal(arg, keyval, ARRAY_SIZE(keyval), &val))
			{
				return Opt_ShowError(OPT_SYMLOAD, arg, "Unknown option value");
			}
			ConfigureParams.Debugger.nSymbolsAutoLoad = val;
			break;
		}

		case OPT_LILO:
			len = strlen(arg);
			len += strlen(ConfigureParams.Lilo.szCommandLine);
			if (arg[0] && len+2 < sizeof(ConfigureParams.Lilo.szCommandLine))
			{
				strcat(ConfigureParams.Lilo.szCommandLine, " ");
				strcat(ConfigureParams.Lilo.szCommandLine, arg);
			}
			else if (arg[0])
			{
				return Opt_ShowError(OPT_LILO, arg, "kernel command line too long");
			}
			bLoadAutoSave = false;
			bUseLilo = true;
			bUseTos = false;
			break;

		case OPT_BIOSINTERCEPT:
			ok = Opt_Bool(arg, OPT_BIOSINTERCEPT, &bBiosIntercept);
			Log_Printf(LOG_DEBUG, "XBIOS 11/20/255 Hatari versions %sabled: "
			        "Dbmsg(), Scrdmp(), HatariControl().\n",
			        bBiosIntercept ? "en" : "dis");
			XBios_EnableCommands(bBiosIntercept);
			break;

		case OPT_CONOUT:
			if (!Opt_Int(arg, OPT_CONOUT, &val, 0, 7, 0))
			{
				return false;
			}
			if (!Console_SetDevice(val))
			{
				return Opt_ShowError(OPT_CONOUT, arg, "Invalid console device vector number");
			}
			break;

		case OPT_MEMCONV:
			ok = Opt_Bool(arg, OPT_MEMCONV, &ConfigureParams.Debugger.bMemConvLocale);
			Log_Printf(LOG_DEBUG, "Memory output locale conversion %s.\n", ConfigureParams.Debugger.bMemConvLocale ? "enabled" : "disabled");
			break;

		case OPT_NATFEATS:
			ok = Opt_Bool(arg, OPT_NATFEATS, &ConfigureParams.Log.bNatFeats);
			Log_Printf(LOG_DEBUG, "Native Features %s.\n", ConfigureParams.Log.bNatFeats ? "enabled" : "disabled");
			break;

		case OPT_DISASM:
			errstr = Disasm_ParseOption(arg);
			if (errstr)
			{
				if (!errstr[0])
				{
					/* silent parsing termination */
					return false;
				}
				return Opt_ShowError(OPT_DISASM, arg, errstr);
			}
			break;

		case OPT_TRACE:
			event = Event_GetPrefixActions(&arg);
			errstr = Log_CheckTraceOptions(arg);
			if (errstr)
			{
				if (!errstr[0])
				{
					/* silent parsing termination */
					return false;
				}
				return Opt_ShowError(OPT_TRACE, arg, errstr);
			}
			if (event)
			{
				event->traceFlags = arg;
				break;
			}
			Log_SetTraceOptions(arg);
			break;

		case OPT_TRACEFILE:
			ok = Opt_StrCpy(OPT_TRACEFILE, CHECK_NONE, ConfigureParams.Log.sTraceFileName,
					arg, sizeof(ConfigureParams.Log.sTraceFileName),
					NULL);
			break;

		case OPT_MSG_REPEAT:
			Log_ToggleMsgRepeat();
			break;

		case OPT_CONTROLSOCKET:
			errstr = Control_SetSocket(arg);
			if (errstr)
			{
				return Opt_ShowError(OPT_CONTROLSOCKET, arg, errstr);
			}
			break;

		case OPT_CMDFIFO:
			errstr = Control_SetFifo(arg);
			if (errstr)
			{
				return Opt_ShowError(OPT_CMDFIFO, arg, errstr);
			}
			break;

		case OPT_LOGFILE:
			ok = Opt_StrCpy(OPT_LOGFILE, CHECK_NONE, ConfigureParams.Log.sLogFileName,
					arg, sizeof(ConfigureParams.Log.sLogFileName),
					NULL);
			break;

		case OPT_PARSE:
			if ((event = Event_GetPrefixActions(&arg)))
			{
				event->parseFile = arg;
				break;
			}
			ok = DebugUI_AddParseFile(arg);
			break;

		case OPT_SAVECONFIG:
			/* Hatari-UI needs Hatari config to start */
			Configuration_Save();
			exit(0);
			break;

		case OPT_LOGLEVEL:
			event = Event_GetPrefixActions(&arg);
			level = Log_ParseOptions(arg);
			if (level == LOG_NONE)
			{
				return Opt_ShowError(OPT_LOGLEVEL, arg, "Unknown log level!");
			}
			if (event)
			{
				event->logLevel = arg;
				break;
			}
			ConfigureParams.Log.nTextLogLevel = level;
			Log_SetLevels();
			break;

		case OPT_ALERTLEVEL:
			ConfigureParams.Log.nAlertDlgLogLevel = Log_ParseOptions(arg);
			if (ConfigureParams.Log.nAlertDlgLogLevel == LOG_NONE)
			{
				return Opt_ShowError(OPT_ALERTLEVEL, arg, "Unknown alert level!");
			}
			Log_SetLevels();
			break;

		case OPT_RUNVBLS:
			event = Event_GetPrefixActions(&arg);
			if (!Opt_Int(arg, OPT_RUNVBLS, &val, 1, INT_MAX, 0))
			{
				return false;
			}
			if (event)
			{
				event->runVBLs = val;
				break;
			}
			Log_Printf(LOG_DEBUG, "Exit after %d VBLs.\n", val);
			Timing_SetRunVBLs(val);
			break;

		case OPT_BENCHMARK:
			BenchmarkMode = true;
			break;

		case OPT_ERROR:
			/* unknown option or missing option parameter */
			return false;

		default:
			return Opt_ShowError(OPT_ERROR, arg, "Internal Hatari error, unhandled option");
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
 * parse Hatari command line options for setting up logging / tracing
 * before Hatari is properly initialized. Returns false for
 * unrecognized options and invalid trace / log settings.
 */
bool Opt_InitLogging(int argc, const char * const argv[])
{
	const char *errstr, *arg;
	bool ok = true;
	opt_id_t optid;
	int i, level;

	for(i = 1; i < argc; i++)
	{
		/* end of options? */
		if (argv[i][0] != '-')
			return true;

		/* WhichOption() checks also that there is an argument,
		 * for options that need one, so we don't need to check
		 * that below.  It also increments it automatically.
		 */
		optid = Opt_WhichOption(argc, argv, &i);
		arg = argv[i];

		switch(optid)
		{
		case OPT_TRACE:
			(void)Event_GetPrefixActions(&arg);
			errstr = Log_CheckTraceOptions(arg);
			if (errstr)
			{
				if (!errstr[0])
				{
					/* silent parsing termination? */
					return false;
				}
				return Opt_ShowError(OPT_TRACE, arg, errstr);
			}
			Log_SetTraceOptions(arg);
			break;

		case OPT_TRACEFILE:
			ok = Opt_StrCpy(OPT_TRACEFILE, CHECK_NONE, ConfigureParams.Log.sTraceFileName,
					arg, sizeof(ConfigureParams.Log.sTraceFileName),
					NULL);
			break;

		case OPT_LOGFILE:
			ok = Opt_StrCpy(OPT_LOGFILE, CHECK_NONE, ConfigureParams.Log.sLogFileName,
					arg, sizeof(ConfigureParams.Log.sLogFileName),
					NULL);
			break;

		case OPT_LOGLEVEL:
			(void)Event_GetPrefixActions(&arg);
			level = Log_ParseOptions(arg);
			if (level == LOG_NONE)
			{
				return Opt_ShowError(OPT_LOGLEVEL, arg, "Unknown log level!");
			}
			ConfigureParams.Log.nTextLogLevel = level;
			Log_SetLevels();
			break;

		case OPT_ERROR:
			/* unknown option or missing option parameter */
			return false;

		default:
			continue;
		}

		if (!ok)
		{
			/* Opt_*() check function failed */
			return false;
		}
	}

	return true;
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
