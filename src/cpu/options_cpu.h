/*
  Hatari - options_cpu.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef UAE_OPTIONS_H
#define UAE_OPTIONS_H

#include "uae/types.h"

//#include "traps.h"

#ifndef WINUAE_FOR_HATARI



#define UAEMAJOR 6
#define UAEMINOR 0
#define UAESUBREV 0

#define MAX_AMIGADISPLAYS 4

typedef enum { KBD_LANG_US, KBD_LANG_DK, KBD_LANG_DE, KBD_LANG_SE, KBD_LANG_FR, KBD_LANG_IT, KBD_LANG_ES } KbdLang;

extern long int version;

#define MAX_PATHS 8

struct multipath {
	TCHAR path[MAX_PATHS][PATH_MAX];
};

#define PATH_NONE -1
#define PATH_FLOPPY 0
#define PATH_CD 1
#define PATH_DIR 2
#define PATH_HDF 3
#define PATH_FS 4
#define PATH_TAPE 5
#define PATH_GENLOCK_IMAGE 6
#define PATH_GENLOCK_VIDEO 7
#define PATH_GEO 8
#define PATH_ROM 9

struct strlist {
	struct strlist *next;
	TCHAR *option, *value;
	int unknown;
};

#define MAX_TOTAL_SCSI_DEVICES 8

/* maximum number native input devices supported (single type) */
#define MAX_INPUT_DEVICES 20
/* maximum number of native input device's buttons and axles supported */
#define MAX_INPUT_DEVICE_EVENTS 256
/* 4 different customization settings */
#define MAX_INPUT_SETTINGS 4
#define GAMEPORT_INPUT_SETTINGS 3 // last slot is for gameport panel mappings

#define MAX_INPUT_SUB_EVENT 8
#define MAX_INPUT_SUB_EVENT_ALL 9
#define SPARE_SUB_EVENT 8

#define INTERNALEVENT_COUNT 1

struct uae_input_device {
	TCHAR *name;
	TCHAR *configname;
	uae_s16 eventid[MAX_INPUT_DEVICE_EVENTS][MAX_INPUT_SUB_EVENT_ALL];
	TCHAR *custom[MAX_INPUT_DEVICE_EVENTS][MAX_INPUT_SUB_EVENT_ALL];
	uae_u64 flags[MAX_INPUT_DEVICE_EVENTS][MAX_INPUT_SUB_EVENT_ALL];
	uae_s8 port[MAX_INPUT_DEVICE_EVENTS][MAX_INPUT_SUB_EVENT_ALL];
	uae_s16 extra[MAX_INPUT_DEVICE_EVENTS];
	uae_s8 enabled;
};

#define MAX_JPORTS_CUSTOM 6
#define MAX_JPORTS 4
#define NORMAL_JPORTS 2
#define MAX_JPORT_NAME 128
#define MAX_JPORT_CONFIG 256
struct jport_custom {
	TCHAR custom[MAX_DPATH];
};
struct inputdevconfig {
	TCHAR name[MAX_JPORT_NAME];
	TCHAR configname[MAX_JPORT_CONFIG];
	TCHAR shortid[16];
};
struct jport_dev {
	int id;
	int mode; // 0=def,1=mouse,2=joy,3=anajoy,4=lightpen
	int submode;
	int autofire;
	struct inputdevconfig idc;
};
#define MAX_JPORT_DEVS 8
struct jport {
	struct jport_dev jd[MAX_JPORT_DEVS];
	bool nokeyboardoverride;
	bool changed;
};
#define JPORT_UNPLUGGED -2
#define JPORT_NONE -1

#define JPORT_AF_NORMAL 1
#define JPORT_AF_TOGGLE 2
#define JPORT_AF_ALWAYS 3
#define JPORT_AF_TOGGLENOAF 4

#define KBTYPE_AMIGA 0
#define KBTYPE_PC1 1
#define KBTYPE_PC2 2

#define MAX_SPARE_DRIVES 20
#define MAX_CUSTOM_MEMORY_ADDRS 2

#define CONFIG_TYPE_ALL -1
#define CONFIG_TYPE_DEFAULT 0
#define CONFIG_TYPE_HARDWARE 1
#define CONFIG_TYPE_HOST 2
#define CONFIG_TYPE_NORESET 4
#define CONFIG_BLEN 2560

#define MOUSEUNTRAP_NONE 0
#define MOUSEUNTRAP_MIDDLEBUTTON 1
#define MOUSEUNTRAP_MAGIC 2
#define MOUSEUNTRAP_BOTH 3

#define TABLET_OFF 0
#define TABLET_MOUSEHACK 1
#define TABLET_REAL 2

#ifdef WITH_SLIRP
#define MAX_SLIRP_REDIRS 32
struct slirp_redir
{
	int proto;
	int srcport;
	int dstport;
	unsigned long addr;
};
#endif

struct cdslot
{
	TCHAR name[MAX_DPATH];
	bool inuse;
	bool delayed;
	bool temporary;
	int type;
};
struct floppyslot
{
	TCHAR df[MAX_DPATH];
	int dfxtype;
	int dfxsubtype;
	TCHAR dfxsubtypeid[32];
	int dfxclick;
	TCHAR dfxclickexternal[256];
	bool forcedwriteprotect;
};

#define ASPECTMULT 1024
#define WH_NATIVE 1
struct wh {
	int x, y;
	int width, height;
	int special;
};

#define MOUNT_CONFIG_SIZE 30
#define UAEDEV_DIR 0
#define UAEDEV_HDF 1
#define UAEDEV_CD 2
#define UAEDEV_TAPE 3

#define HD_LEVEL_SCSI_1 0
#define HD_LEVEL_SCSI_2 1
#define HD_LEVEL_SASI 2
#define HD_LEVEL_SASI_ENHANCED 2
#define HD_LEVEL_SASI_CHS 3

#define HD_LEVEL_ATA_1 0
#define HD_LEVEL_ATA_2 1
#define HD_LEVEL_ATA_2S 2

#define BOOTPRI_NOAUTOBOOT -128
#define BOOTPRI_NOAUTOMOUNT -129
#define ISAUTOBOOT(ci) ((ci)->bootpri > BOOTPRI_NOAUTOBOOT)
#define ISAUTOMOUNT(ci) ((ci)->bootpri > BOOTPRI_NOAUTOMOUNT)
#define MAX_UAEDEV_BADBLOCKS 8
struct uaedev_badblock
{
	uae_u32 first;
	uae_u32 last;
};
struct uaedev_config_info {
	int type;
	TCHAR devname[MAX_DPATH];
	TCHAR volname[MAX_DPATH];
	TCHAR rootdir[MAX_DPATH];
	bool readonly;
	bool lock;
	bool loadidentity;
	int bootpri;
	TCHAR filesys[MAX_DPATH];
	TCHAR geometry[MAX_DPATH];
	int lowcyl;
	int highcyl; // zero if detected from size
	int cyls; // calculated/corrected highcyl
	int surfaces;
	int sectors;
	int reserved;
	int blocksize;
	bool chs;
	uae_u64 max_lba;
	int controller_type;
	int controller_type_unit;
	int controller_unit;
	int controller_media_type; // 1 = CF IDE, 0 = normal
	int unit_feature_level;
	int unit_special_flags;
	bool physical_geometry; // if false: use defaults
	int pcyls, pheads, psecs;
	int flags;
	int buffers;
	int bufmemtype;
	int stacksize;
	int priority;
	uae_u32 mask;
	int maxtransfer;
	uae_u32 dostype;
	int unit;
	int interleave;
	int sectorsperblock;
	int forceload;
	int device_emu_unit;
	bool inject_icons;
	int badblock_num;
	struct uaedev_badblock badblocks[MAX_UAEDEV_BADBLOCKS];
	int uae_unitnum; // mountunit nr
};

struct uaedev_config_data
{
	struct uaedev_config_info ci;
	int configoffset; // HD config entry index
	int unitnum; // scsi unit number (if tape currently)
};

enum { CP_GENERIC = 1, CP_CDTV, CP_CDTVCR, CP_CD32, CP_A500, CP_A500P, CP_A600,
	CP_A1000, CP_A1200, CP_A2000, CP_A3000, CP_A3000T, CP_A4000, CP_A4000T,
	CP_VELVET, CP_CASABLANCA, CP_DRACO };

#define IDE_A600A1200 1
#define IDE_A4000 2

#define GFX_WINDOW 0
#define GFX_FULLSCREEN 1
#define GFX_FULLWINDOW 2

#define AUTOSCALE_NONE 0
#define AUTOSCALE_STATIC_AUTO 1
#define AUTOSCALE_STATIC_NOMINAL 2
#define AUTOSCALE_STATIC_MAX 3
#define AUTOSCALE_NORMAL 4
#define AUTOSCALE_RESIZE 5
#define AUTOSCALE_CENTER 6
#define AUTOSCALE_MANUAL 7 // use gfx_xcenter_pos and gfx_ycenter_pos
#define AUTOSCALE_INTEGER 8
#define AUTOSCALE_INTEGER_AUTOSCALE 9
#define AUTOSCALE_SEPARATOR 10
#define AUTOSCALE_OVERSCAN_BLANK 11

#define MONITOREMU_NONE 0
#define MONITOREMU_AUTO 1
#define MONITOREMU_A2024 2
#define MONITOREMU_GRAFFITI 3
#define MONITOREMU_HAM_E 4
#define MONITOREMU_HAM_E_PLUS 5
#define MONITOREMU_VIDEODAC18 6
#define MONITOREMU_AVIDEO12 7
#define MONITOREMU_AVIDEO24 8
#define MONITOREMU_FIRECRACKER24 9
#define MONITOREMU_DCTV 10
#define MONITOREMU_OPALVISION 11
#define MONITOREMU_COLORBURST 12

#define OVERSCANMODE_OVERSCAN 3
#define OVERSCANMODE_BROADCAST 4
#define OVERSCANMODE_EXTREME 5
#define OVERSCANMODE_ULTRA 6

#define MAX_FILTERSHADERS 4

#define MAX_CHIPSET_REFRESH 10
#define MAX_CHIPSET_REFRESH_TOTAL (MAX_CHIPSET_REFRESH + 2)
#define CHIPSET_REFRESH_PAL (MAX_CHIPSET_REFRESH + 0)
#define CHIPSET_REFRESH_NTSC (MAX_CHIPSET_REFRESH + 1)
struct chipset_refresh
{
	bool inuse;
	int index;
	bool locked;
	bool rtg;
	bool exit;
	bool defaultdata;
	int horiz;
	int vert;
	int lace;
	int resolution;
	int resolution_pct;
	int ntsc;
	int vsync;
	int framelength;
	float rate;
	TCHAR label[16];
	TCHAR commands[256];
	TCHAR filterprofile[64];
};

#define APMODE_NATIVE 0
#define APMODE_RTG 1

struct apmode
{
	int gfx_fullscreen;
	int gfx_display;
	int gfx_vsync;
	// 0 = immediate flip
	// -1 = wait for flip, before frame ends
	// 1 = wait for flip, after new frame has started
	int gfx_vflip;
	// doubleframemode strobo
	bool gfx_strobo;
	int gfx_vsyncmode;
	int gfx_backbuffers;
	bool gfx_interlaced;
	int gfx_refreshrate;
};

#define MAX_LUA_STATES 16


#define MAX_FILTERDATA 3
#define GF_NORMAL 0
#define GF_RTG 1
#define GF_INTERLACE 2
struct gfx_filterdata
{
	int enable;
	int gfx_filter;
	TCHAR gfx_filtershader[2 * MAX_FILTERSHADERS + 1][MAX_DPATH];
	TCHAR gfx_filtermask[2 * MAX_FILTERSHADERS + 1][MAX_DPATH];
	TCHAR gfx_filteroverlay[MAX_DPATH];
	struct wh gfx_filteroverlay_pos;
	int gfx_filteroverlay_overscan;
	int gfx_filter_scanlines;
	int gfx_filter_scanlineratio;
	int gfx_filter_scanlinelevel;
	int gfx_filter_scanlineoffset;
	float gfx_filter_horiz_zoom, gfx_filter_vert_zoom;
	float gfx_filter_horiz_zoom_mult, gfx_filter_vert_zoom_mult;
	float gfx_filter_horiz_offset, gfx_filter_vert_offset;
	int gfx_filter_left_border, gfx_filter_right_border;
	int gfx_filter_top_border, gfx_filter_bottom_border;
	int gfx_filter_filtermodeh, gfx_filter_filtermodev;
	int gfx_filter_bilinear;
	int gfx_filter_noise, gfx_filter_blur;
	int gfx_filter_saturation, gfx_filter_luminance, gfx_filter_contrast;
	int gfx_filter_gamma, gfx_filter_gamma_ch[3];
	int gfx_filter_keep_aspect, gfx_filter_aspect;
	int gfx_filter_autoscale;
	int gfx_filter_integerscalelimit;
	int gfx_filter_keep_autoscale_aspect;
	int gfx_filter_rotation;
	bool changed;
};

#define MAX_DUPLICATE_EXPANSION_BOARDS 5
#define MAX_AVAILABLE_DUPLICATE_EXPANSION_BOARDS 4
#define MAX_EXPANSION_BOARDS 20
#define ROMCONFIG_CONFIGTEXT_LEN 256
struct boardromconfig;
struct romconfig
{
	TCHAR romfile[MAX_DPATH];
	TCHAR romident[256];
	uae_u32 board_ram_size;
	bool autoboot_disabled;
	bool inserted;
	bool dma24bit;
	int device_id;
	int device_settings;
	int subtype;
	void *unitdata;
	TCHAR configtext[ROMCONFIG_CONFIGTEXT_LEN];
	uae_u16 manufacturer;
	uae_u8 product;
	uae_u8 autoconfig[16];
	struct boardromconfig *back;
};
#define MAX_BOARD_ROMS 2
struct boardromconfig
{
	int device_type;
	int device_num;
	int device_order;
	struct romconfig roms[MAX_BOARD_ROMS];
};
#define MAX_RTG_BOARDS 4
struct rtgboardconfig
{
	int rtg_index;
	int rtgmem_type;
	uae_u32 rtgmem_size;
	int device_order;
	int monitor_id;
	bool autoswitch;
};
struct boardloadfile
{
	uae_u32 loadoffset;
	uae_u32 fileoffset, filesize;
	TCHAR loadfile[MAX_DPATH];
};
#define MAX_ROM_BOARDS 4
struct romboard
{
	uae_u32 size;
	uae_u32 start_address;
	uae_u32 end_address;
	struct boardloadfile lf;
};
#define MAX_RAM_BOARDS 4
struct ramboard
{
	uae_u32 size;
	uae_u16 manufacturer;
	uae_u8 product;
	uae_u8 autoconfig[16];
	bool autoconfig_inuse;
	bool manual_config;
	bool no_reset_unmap;
	int device_order;
	uae_u32 start_address;
	uae_u32 end_address;
	uae_u32 write_address;
	bool readonly;
	bool nodma;
	bool force16bit;
	bool chipramtiming;
	int fault;
	struct boardloadfile lf;
};
struct expansion_params
{
	int device_order;
};

#define Z3MAPPING_AUTO 0
#define Z3MAPPING_UAE 1
#define Z3MAPPING_REAL 2

#define GFX_SIZE_EXTRA_NUM 6
struct monconfig
{
	struct wh gfx_size_win;
	struct wh gfx_size_fs;
	struct wh gfx_size;
	struct wh gfx_size_win_xtra[GFX_SIZE_EXTRA_NUM];
	struct wh gfx_size_fs_xtra[GFX_SIZE_EXTRA_NUM];
};


#endif		/* !WINUAE_FOR_HATARI */

struct uae_prefs {
#ifndef WINUAE_FOR_HATARI
	struct strlist *all_lines;

	TCHAR description[256];
	TCHAR category[256];
	TCHAR tags[256];
	TCHAR info[256];
	int config_version;
	TCHAR config_hardware_path[MAX_DPATH];
	TCHAR config_host_path[MAX_DPATH];
	TCHAR config_all_path[MAX_DPATH];
	TCHAR config_path[MAX_DPATH];
	TCHAR config_window_title[256];
	int got_fs2_hdf2;

	bool illegal_mem;
	bool debug_mem;
	bool use_serial;
	bool serial_demand;
	bool serial_hwctsrts;
	bool serial_rtsctsdtrdtecd;
	bool serial_ri;
	bool serial_direct;
	int serial_stopbits;
	int serial_crlf;
	bool parallel_demand;
	int parallel_matrix_emulation;
	bool parallel_postscript_emulation;
	bool parallel_postscript_detection;
	int parallel_autoflush_time;
	TCHAR ghostscript_parameters[256];
	bool use_gfxlib;
	bool socket_emu;

	bool start_debugger;
	int debugging_features;
	TCHAR debugging_options[MAX_DPATH];
	bool start_gui;

	KbdLang keyboard_lang;

	int produce_sound;
	int sound_stereo;
	int sound_stereo_separation;
	int sound_mixed_stereo_delay;
	int sound_freq;
	int sound_maxbsiz;
	int sound_interpol;
	int sound_filter;
	int sound_filter_type;
	int sound_volume_master;
	int sound_volume_paula;
	int sound_volume_cd;
	int sound_volume_board;
	int sound_volume_midi;
	int sound_volume_genlock;
	bool sound_stereo_swap_paula;
	bool sound_stereo_swap_ahi;
	bool sound_auto;
	bool sound_cdaudio;
	bool sound_volcnt;

	int sampler_freq;
	int sampler_buffer;
	bool sampler_stereo;
#endif

	int comptrustbyte;
	int comptrustword;
	int comptrustlong;
	int comptrustnaddr;
	bool compnf;
	bool compfpu;
	bool comp_hardflush;
	bool comp_constjump;
	bool comp_catchfault;
	int cachesize;
	bool cachesize_inhibit;
	TCHAR jitblacklist[MAX_DPATH];
	bool fpu_strict;
	int fpu_mode;

#ifndef WINUAE_FOR_HATARI
	struct monconfig gfx_monitor[MAX_AMIGADISPLAYS];
	int gfx_framerate, gfx_autoframerate;
	bool gfx_autoresolution_vga;
	int gfx_autoresolution;
	int gfx_autoresolution_delay;
	int gfx_autoresolution_minv, gfx_autoresolution_minh;
	bool gfx_scandoubler;
	struct apmode gfx_apmode[2];
	int gfx_resolution;
	int gfx_vresolution;
	int gfx_lores_mode;
	int gfx_pscanlines, gfx_iscanlines;
	int gfx_xcenter, gfx_ycenter;
	int gfx_xcenter_pos, gfx_ycenter_pos;
	int gfx_xcenter_size, gfx_ycenter_size;
	int gfx_max_horizontal, gfx_max_vertical;
	int gfx_saturation, gfx_luminance, gfx_contrast, gfx_gamma, gfx_gamma_ch[3];
	bool gfx_blackerthanblack;
	int gfx_threebitcolors;
	int gfx_api;
	int gfx_api_options;
	int color_mode;
	int gfx_extrawidth;
	int gfx_extraheight;
	bool lightboost_strobo;
	int lightboost_strobo_ratio;
	bool gfx_grayscale;
	bool lightpen_crosshair;
	int lightpen_offset[2];
	int gfx_display_sections;
	int gfx_variable_sync;
	bool gfx_windowed_resize;
	int gfx_overscanmode;
	int gfx_monitorblankdelay;
	int gfx_rotation;
	int gfx_ntscpixels;
	uae_u32 gfx_bordercolor;

	struct gfx_filterdata gf[3];

	float rtg_horiz_zoom_mult;
	float rtg_vert_zoom_mult;

	bool immediate_blits;
	int waiting_blits;
	float blitter_speed_throttle;
	unsigned int chipset_mask;
	bool chipset_hr;
	bool keyboard_connected;
#endif
	bool ntscmode;
#ifndef WINUAE_FOR_HATARI
	bool genlock;
	int genlock_image;
	int genlock_mix;
	int genlock_scale;
	int genlock_aspect;
	int genlock_effects;
	int genlock_offset_x, genlock_offset_y;
	uae_u64 ecs_genlock_features_colorkey_mask[4];
	uae_u8 ecs_genlock_features_plane_mask;
	bool genlock_alpha;
	TCHAR genlock_image_file[MAX_DPATH];
	TCHAR genlock_video_file[MAX_DPATH];
	int monitoremu;
	int monitoremu_mon;
	float chipset_refreshrate;
	struct chipset_refresh cr[MAX_CHIPSET_REFRESH + 2];
	int cr_selected;
	int collision_level;
	int leds_on_screen;
	int leds_on_screen_mask[2];
	int leds_on_screen_multiplier[2];
	int power_led_dim;
	struct wh osd_pos;
	int keyboard_leds[3];
	bool keyboard_leds_in_use;
	int scsi;
	bool sana2;
	bool uaeserial;
	int catweasel;
#endif
	int cpu_idle;
	int ppc_cpu_idle;
	bool cpu_cycle_exact;
	int cpu_clock_multiplier;
	int cpu_frequency;
	bool blitter_cycle_exact;
	bool cpu_memory_cycle_exact;
#ifndef WINUAE_FOR_HATARI
	int floppy_speed;
	int floppy_write_length;
	int floppy_random_bits_min;
	int floppy_random_bits_max;
	int floppy_auto_ext2;
	int cd_speed;
	bool tod_hack;
	uae_u32 maprom;
	int boot_rom;
	bool rom_readwrite;
	int turbo_emulation;
	int turbo_emulation_limit;
	bool turbo_boot;
	int turbo_boot_delay;
	bool headless;
	int filesys_limit;
	int filesys_max_name;
	int filesys_max_file_size;
	bool filesys_inject_icons;
	TCHAR filesys_inject_icons_tool[MAX_DPATH];
	TCHAR filesys_inject_icons_project[MAX_DPATH];
	TCHAR filesys_inject_icons_drawer[MAX_DPATH];
	int uaescsidevmode;
#endif
	bool reset_delay;
	bool crash_auto_reset;
	int monitorblankdelay;

#ifndef WINUAE_FOR_HATARI
	int cs_compatible;
	int cs_ciaatod;
	int cs_rtc;
	int cs_rtc_adjust;
	int cs_rtc_adjust_mode;
	bool cs_ksmirror_e0;
	bool cs_ksmirror_a8;
	bool cs_ciaoverlay;
	bool cs_cd32cd;
	bool cs_cd32c2p;
	bool cs_cd32nvram;
	bool cs_cd32fmv;
	int cs_cd32nvram_size;
	bool cs_cdtvcd;
	bool cs_cdtvram;
	int cs_ide;
	bool cs_pcmcia;
	bool cs_a1000ram;
	int cs_fatgaryrev;
	int cs_ramseyrev;
	int cs_agnusrev;
	int cs_deniserev;
	int cs_mbdmac;
	bool cs_cdtvcr;
	bool cs_df0idhw;
	bool cs_resetwarning;
	bool cs_denisenoehb;
	bool cs_dipagnus;
	bool cs_agnusbltbusybug;
	bool cs_ciatodbug;
	bool cs_z3autoconfig;
	bool cs_1mchipjumper;
	bool cs_cia6526;
	bool cs_bytecustomwritebug;
	bool cs_color_burst;
	bool cs_romisslow;
	bool cs_toshibagary;
	bool cs_bkpthang;
	int cs_unmapped_space;
	int cs_hacks;
	int cs_ciatype[2];
	int cs_kbhandshake;
	int cs_hvcsync;
	int cs_eclockphase;
	int cs_eclocksync;
	bool cs_memorypatternfill;
	bool cs_ipldelay;
	bool cs_floppydatapullup;
	uae_u32 seed;

	struct boardromconfig expansionboard[MAX_EXPANSION_BOARDS];

	TCHAR romfile[MAX_DPATH];
	TCHAR romident[256];
	TCHAR romextfile[MAX_DPATH];
	uae_u32 romextfile2addr;
	TCHAR romextfile2[MAX_DPATH];
	TCHAR romextident[256];
	TCHAR flashfile[MAX_DPATH];
	TCHAR rtcfile[MAX_DPATH];
	TCHAR cartfile[MAX_DPATH];
	TCHAR cartident[256];
	int cart_internal;
	TCHAR pci_devices[256];
	TCHAR prtname[256];
	TCHAR sername[256];
	TCHAR a2065name[MAX_DPATH];
	TCHAR ne2000pciname[MAX_DPATH];
	TCHAR ne2000pcmcianame[MAX_DPATH];
	TCHAR picassoivromfile[MAX_DPATH];
	struct cdslot cdslots[MAX_TOTAL_SCSI_DEVICES];
	TCHAR quitstatefile[MAX_DPATH];
	TCHAR statefile[MAX_DPATH];
	TCHAR statefile_path[MAX_DPATH];
	TCHAR inprecfile[MAX_DPATH];
	TCHAR trainerfile[MAX_DPATH];
	bool inprec_autoplay;
	bool refresh_indicator;

	struct multipath path_floppy;
	struct multipath path_hardfile;
	struct multipath path_rom;
	struct multipath path_cd;
#endif

	int m68k_speed;
	float m68k_speed_throttle;
	float x86_speed_throttle;
	int cpu_model;
	int mmu_model;
	bool mmu_ec;
	int cpu060_revision;
	int fpu_model;
	int fpu_revision;
	int ppc_mode;
	TCHAR ppc_model[32];
	bool cpu_compatible;
	bool cpu_thread;
	bool int_no_unimplemented;
	bool fpu_no_unimplemented;
	bool address_space_24;
	bool cpu_data_cache;
	bool picasso96_nocustom;
	int picasso96_modeflags;
	int cpu_model_fallback;

#ifndef WINUAE_FOR_HATARI
	uae_u32 z3autoconfig_start;
	struct ramboard z3fastmem[MAX_RAM_BOARDS];
	struct ramboard fastmem[MAX_RAM_BOARDS];
	struct romboard romboards[MAX_ROM_BOARDS];
	struct ramboard z3chipmem;
	struct ramboard chipmem;
	struct ramboard bogomem;
	struct ramboard mbresmem_low;
	struct ramboard mbresmem_high;
	struct ramboard mem25bit;
	uae_u32 debugmem_start;
	uae_u32 debugmem_size;
	int cpuboard_type;
	int cpuboard_subtype;
	int cpuboard_settings;
	struct ramboard cpuboardmem1;
	struct ramboard cpuboardmem2;
	int ppc_implementation;
	bool rtg_hardwareinterrupt;
	bool rtg_hardwaresprite;
	bool rtg_more_compatible;
	bool rtg_multithread;
	bool rtg_overlay;
	bool rtg_vgascreensplit;
	bool rtg_paletteswitch;
	bool rtg_dacswitch;
	struct rtgboardconfig rtgboards[MAX_RTG_BOARDS];
	uae_u32 custom_memory_addrs[MAX_CUSTOM_MEMORY_ADDRS];
	uae_u32 custom_memory_sizes[MAX_CUSTOM_MEMORY_ADDRS];
	uae_u32 custom_memory_mask[MAX_CUSTOM_MEMORY_ADDRS];
	int uaeboard;
	bool uaeboard_nodiag;
	int uaeboard_order;

	bool kickshifter;
	bool scsidevicedisable;
	bool filesys_no_uaefsdb;
	bool filesys_custom_uaefsdb;
	bool mmkeyboard;
	int uae_hide;
	bool clipboard_sharing;
	bool native_code;
	bool uae_hide_autoconfig;
	int z3_mapping_mode;
	bool autoconfig_custom_sort;
	bool obs_sound_toccata;
	bool obs_sound_toccata_mixer;
	bool obs_sound_es1370;
	bool obs_sound_fm801;
	bool cputester;

	int mountitems;
	struct uaedev_config_data mountconfig[MOUNT_CONFIG_SIZE];

	int nr_floppies;
	struct floppyslot floppyslots[4];
	bool floppy_read_only;
	bool harddrive_read_only;
	TCHAR dfxlist[MAX_SPARE_DRIVES][MAX_DPATH];
	int dfxclickvolume_disk[4];
	int dfxclickvolume_empty[4];
	int dfxclickchannelmask;

	TCHAR luafiles[MAX_LUA_STATES][MAX_DPATH];

	/* Target specific options */

	bool win32_logfile;
	bool win32_notaskbarbutton;
	bool win32_nonotificationicon;
	bool win32_gui_alwaysontop;
	bool win32_main_alwaysontop;
	bool win32_powersavedisabled;
	bool win32_minimize_inactive;
	bool win32_capture_always;
	int win32_statusbar;
	bool win32_start_minimized;
	bool win32_start_uncaptured;

	int win32_active_capture_priority;
	bool win32_active_nocapture_pause;
	bool win32_active_nocapture_nosound;
	int win32_active_input;
	int win32_inactive_priority;
	bool win32_inactive_pause;
	bool win32_inactive_nosound;
	int win32_inactive_input;
	int win32_iconified_priority;
	bool win32_iconified_pause;
	bool win32_iconified_nosound;
	int win32_iconified_input;

	bool win32_rtgmatchdepth;
	bool win32_rtgallowscaling;
	int win32_rtgscaleaspectratio;
	int win32_rtgvblankrate;
	bool win32_borderless;
	bool win32_ctrl_F11_is_quit;
	bool win32_automount_removable;
	bool win32_automount_drives;
	bool win32_automount_cddrives;
	bool win32_automount_netdrives;
	bool win32_automount_removabledrives;
	int win32_midioutdev;
	int win32_midiindev;
	bool win32_midirouter;
	int win32_uaescsimode;
	int win32_soundcard;
	int win32_samplersoundcard;
	bool win32_norecyclebin;
	int win32_guikey;
	int win32_kbledmode;
	bool win32_blankmonitors;
	TCHAR win32_commandpathstart[MAX_DPATH];
	TCHAR win32_commandpathend[MAX_DPATH];
	TCHAR win32_parjoyport0[MAX_DPATH];
	TCHAR win32_parjoyport1[MAX_DPATH];
	TCHAR win32_guipage[32];
	TCHAR win32_guiactivepage[32];
	bool win32_filesystem_mangle_reserved_names;
	bool win32_shutdown_notification;
	bool win32_warn_exit;
	win32_gui_control;
	win32_videograb_balance;
	bool right_control_is_right_win_key;
#ifdef WITH_SLIRP
	struct slirp_redir slirp_redirs[MAX_SLIRP_REDIRS];
#endif
	int statecapturerate, statecapturebuffersize;
	int aviout_width, aviout_height, aviout_xoffset, aviout_yoffset;
	int screenshot_width, screenshot_height, screenshot_xoffset, screenshot_yoffset;
	int screenshot_min_width, screenshot_min_height;
	int screenshot_max_width, screenshot_max_height;
	int screenshot_output_width, screenshot_output_height;
	int screenshot_xmult, screenshot_ymult;

	/* input */

	struct jport jports[MAX_JPORTS];
	struct jport_custom jports_custom[MAX_JPORTS_CUSTOM];
	int input_selected_setting;
	int input_joymouse_multiplier;
	int input_joymouse_deadzone;
	int input_joystick_deadzone;
	int input_joymouse_speed;
	int input_analog_joystick_mult;
	int input_analog_joystick_offset;
	int input_autofire_linecnt;
	int input_mouse_speed;
	int input_tablet;
	bool tablet_library;
	int input_mouse_untrap;
	int input_magic_mouse_cursor;
	int input_keyboard_type;
	bool input_autoswitch;
	bool input_autoswitchleftright;
	bool input_advancedmultiinput;
	struct uae_input_device joystick_settings[MAX_INPUT_SETTINGS][MAX_INPUT_DEVICES];
	struct uae_input_device mouse_settings[MAX_INPUT_SETTINGS][MAX_INPUT_DEVICES];
	struct uae_input_device keyboard_settings[MAX_INPUT_SETTINGS][MAX_INPUT_DEVICES];
	struct uae_input_device internalevent_settings[MAX_INPUT_SETTINGS][INTERNALEVENT_COUNT];
	TCHAR input_config_name[GAMEPORT_INPUT_SETTINGS][256];
	int dongle;
	int input_contact_bounce;
	int input_device_match_mask;
#endif
};

#ifndef WINUAE_FOR_HATARI
extern int config_changed;
extern void config_check_vsync (void);
extern void set_config_changed (void);

/* Contains the filename of .uaerc */
extern TCHAR optionsfile[];

extern void cfgfile_write (struct zfile *, const TCHAR *option, const TCHAR *format,...);
extern void cfgfile_dwrite (struct zfile *, const TCHAR *option, const TCHAR *format,...);
extern void cfgfile_target_write (struct zfile *, const TCHAR *option, const TCHAR *format,...);
extern void cfgfile_target_dwrite (struct zfile *, const TCHAR *option, const TCHAR *format,...);

extern void cfgfile_write_bool (struct zfile *f, const TCHAR *option, bool b);
extern void cfgfile_dwrite_bool (struct zfile *f,const  TCHAR *option, bool b);
extern void cfgfile_target_write_bool (struct zfile *f, const TCHAR *option, bool b);
extern void cfgfile_target_dwrite_bool (struct zfile *f, const TCHAR *option, bool b);

extern void cfgfile_write_str(struct zfile *f, const TCHAR *option, const TCHAR *value);
extern void cfgfile_write_str_escape(struct zfile *f, const TCHAR *option, const TCHAR *value);
extern void cfgfile_dwrite_str(struct zfile *f, const TCHAR *option, const TCHAR *value);
extern void cfgfile_target_write_str(struct zfile *f, const TCHAR *option, const TCHAR *value);
extern void cfgfile_target_dwrite_str(struct zfile *f, const TCHAR *option, const TCHAR *value);
extern void cfgfile_target_dwrite_str_escape(struct zfile *f, const TCHAR *option, const TCHAR *value);

extern void cfgfile_backup (const TCHAR *path);
extern struct uaedev_config_data *add_filesys_config (struct uae_prefs *p, int index, struct uaedev_config_info*);
extern bool get_hd_geometry (struct uaedev_config_info *);
extern void uci_set_defaults (struct uaedev_config_info *uci, bool rdb);

extern void error_log (const TCHAR*, ...);
extern TCHAR *get_error_log (void);
extern bool is_error_log (void);

extern void default_prefs (struct uae_prefs *, bool, int);
extern void discard_prefs (struct uae_prefs *, int);
extern void copy_prefs(struct uae_prefs *src, struct uae_prefs *dst);
extern void copy_inputdevice_prefs(struct uae_prefs *src, struct uae_prefs *dst);

int parse_cmdline_option (struct uae_prefs *, TCHAR, const TCHAR*);

extern int cfgfile_yesno(const TCHAR *option, const TCHAR *value, const TCHAR *name, bool *location);
extern int cfgfile_intval(const TCHAR *option, const TCHAR *value, const TCHAR *name, int *location, int scale);
extern int cfgfile_strval(const TCHAR *option, const TCHAR *value, const TCHAR *name, int *location, const TCHAR *table[], int more);
extern int cfgfile_string(const TCHAR *option, const TCHAR *value, const TCHAR *name, TCHAR *location, int maxsz);
extern int cfgfile_string_escape(const TCHAR *option, const TCHAR *value, const TCHAR *name, TCHAR *location, int maxsz);
extern bool cfgfile_option_find(const TCHAR *s, const TCHAR *option);
extern TCHAR *cfgfile_option_get(const TCHAR *s, const TCHAR *option);
extern TCHAR *cfgfile_subst_path(const TCHAR *path, const TCHAR *subst, const TCHAR *file);

extern TCHAR *target_expand_environment (const TCHAR *path, TCHAR *out, int maxlen);
extern int target_parse_option (struct uae_prefs *, const TCHAR *option, const TCHAR *value, int type);
extern void target_save_options (struct zfile*, struct uae_prefs *);
extern void target_default_options (struct uae_prefs *, int type);
extern void target_fixup_options (struct uae_prefs *);
extern int target_cfgfile_load (struct uae_prefs *, const TCHAR *filename, int type, int isdefault);
extern void cfgfile_save_options (struct zfile *f, struct uae_prefs *p, int type);
extern int target_get_display (const TCHAR*);
extern const TCHAR *target_get_display_name (int, bool);
extern void target_multipath_modified(struct uae_prefs *);
extern void cfgfile_resolve_path_out_load(const TCHAR *path, TCHAR *out, int size, int type);
extern void cfgfile_resolve_path_load(TCHAR *path, int size, int type);
extern void cfgfile_resolve_path_out_save(const TCHAR *path, TCHAR *out, int size, int type);
extern void cfgfile_resolve_path_save(TCHAR *path, int size, int type);

extern struct uae_prefs *cfgfile_open(const TCHAR *filename, int *type);
extern void cfgfile_close(struct uae_prefs *p);
extern int cfgfile_load (struct uae_prefs *p, const TCHAR *filename, int *type, int ignorelink, int userconfig);
extern int cfgfile_save (struct uae_prefs *p, const TCHAR *filename, int);
extern void cfgfile_parse_line (struct uae_prefs *p, TCHAR *, int);
extern void cfgfile_parse_lines (struct uae_prefs *p, const TCHAR *, int);
extern int cfgfile_parse_option (struct uae_prefs *p, const TCHAR *option, TCHAR *value, int);
extern int cfgfile_get_description (struct uae_prefs *p, const TCHAR *filename, TCHAR *description, TCHAR *category, TCHAR *tags, TCHAR *hostlink, TCHAR *hardwarelink, int *type);
extern void cfgfile_show_usage (void);
extern int cfgfile_searchconfig(const TCHAR *in, int index, TCHAR *out, int outsize);
extern uae_u32 cfgfile_uaelib(TrapContext *ctx, int mode, uae_u32 name, uae_u32 dst, uae_u32 maxlen);
extern uae_u32 cfgfile_uaelib_modify(TrapContext *ctx, uae_u32 mode, uae_u32 parms, uae_u32 size, uae_u32 out, uae_u32 outsize);
extern uae_u32 cfgfile_modify (uae_u32 index, const TCHAR *parms, uae_u32 size, TCHAR *out, uae_u32 outsize);
extern void cfgfile_addcfgparam (TCHAR *);
extern int built_in_prefs (struct uae_prefs *p, int model, int config, int compa, int romcheck);
extern int built_in_chipset_prefs (struct uae_prefs *p);
extern int cmdlineparser (const TCHAR *s, TCHAR *outp[], int max);
extern void fixup_prefs_dimensions (struct uae_prefs *prefs);
extern void fixup_prefs (struct uae_prefs *prefs, bool userconfig);
extern void fixup_cpu (struct uae_prefs *prefs);
extern void cfgfile_compatibility_romtype(struct uae_prefs *p);
extern void cfgfile_compatibility_rtg(struct uae_prefs *p);
extern bool cfgfile_detect_art(struct uae_prefs *p, TCHAR *path);
extern const TCHAR *cfgfile_getconfigdata(size_t *len);
extern bool cfgfile_createconfigstore(struct uae_prefs *p);
extern void cfgfile_get_shader_config(struct uae_prefs *p, int rtg);

extern void check_prefs_changed_custom (void);
extern void check_prefs_changed_cpu (void);
extern void check_prefs_changed_audio (void);
extern void check_prefs_changed_cd (void);
extern int check_prefs_changed_gfx (void);

extern struct uae_prefs currprefs, changed_prefs;

extern int machdep_init (void);
extern void machdep_free (void);

#else		/* !WINUAE_FOR_HATARI */

extern struct uae_prefs currprefs, changed_prefs;
extern void fixup_cpu (struct uae_prefs *prefs);
extern void check_prefs_changed_cpu (void);
extern void error_log (const TCHAR*, ...);

#endif

#endif /* UAE_OPTIONS_H */
