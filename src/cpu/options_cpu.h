/*
  Hatari - options_cpu.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef OPTIONS_CPU_H
#define OPTIONS_CPU_H

#define MAX_CUSTOM_MEMORY_ADDRS 2

struct uae_prefs {
	/*
	struct strlist *all_lines;
	*/

	TCHAR description[256];
	TCHAR info[256];
	int config_version;
	TCHAR config_hardware_path[MAX_DPATH];
	TCHAR config_host_path[MAX_DPATH];
	TCHAR config_window_title[256];

	bool illegal_mem;
	bool use_serial;
	bool serial_demand;
	bool serial_hwctsrts;
	bool serial_direct;
	int serial_stopbits;
	bool parallel_demand;
	int parallel_matrix_emulation;
	bool parallel_postscript_emulation;
	bool parallel_postscript_detection;
	int parallel_autoflush_time;
	TCHAR ghostscript_parameters[256];
	bool use_gfxlib;
	bool socket_emu;

	bool start_debugger;
	bool start_gui;

	/*
	KbdLang keyboard_lang;
	*/

	int produce_sound;
	int sound_stereo;
	int sound_stereo_separation;
	int sound_mixed_stereo_delay;
	int sound_freq;
	int sound_maxbsiz;
	int sound_interpol;
	int sound_filter;
	int sound_filter_type;
	int sound_volume;
	int sound_volume_cd;
	bool sound_stereo_swap_paula;
	bool sound_stereo_swap_ahi;
	bool sound_auto;

	int sampler_freq;
	int sampler_buffer;
	bool sampler_stereo;

	int comptrustbyte;
	int comptrustword;
	int comptrustlong;
	int comptrustnaddr;
	bool compnf;
	bool compfpu;
	bool comp_midopt;
	bool comp_lowopt;
	bool fpu_strict;
	bool fpu_softfloat;

	bool comp_hardflush;
	bool comp_constjump;
	bool comp_oldsegv;

	int cachesize;
	int optcount[10];

	bool avoid_cmov;

	/*
        int gfx_framerate, gfx_autoframerate;
        struct wh gfx_size_win;
        struct wh gfx_size_fs;
        struct wh gfx_size;
        struct wh gfx_size_win_xtra[6];
        struct wh gfx_size_fs_xtra[6];
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
        int gfx_saturation, gfx_luminance, gfx_contrast, gfx_gamma;
        bool gfx_blackerthanblack;
        int gfx_api;
        int color_mode;
        int gfx_extrawidth;
        bool lightboost_strobo;

        struct gfx_filterdata gf[2];

        float rtg_horiz_zoom_mult;
        float rtg_vert_zoom_mult;
	*/

	bool immediate_blits;
	int waiting_blits;
	unsigned int chipset_mask;
	bool ntscmode;
	bool genlock;
        int monitoremu;
        double chipset_refreshrate;
	/*
        struct chipset_refresh cr[MAX_CHIPSET_REFRESH + 2];
	*/
        int cr_selected;
	int collision_level;
	int leds_on_screen;
	int leds_on_screen_mask[2];
	/*
	struct wh osd_pos;
	*/
	int keyboard_leds[3];
	bool keyboard_leds_in_use;
	int scsi;
	bool sana2;
	bool uaeserial;
	int catweasel;
	int cpu_idle;
	int ppc_cpu_idle;
	bool cpu_cycle_exact;
	int cpu_clock_multiplier;
	int cpu_frequency;
	bool blitter_cycle_exact;
	int floppy_speed;
	int floppy_write_length;
	int floppy_random_bits_min;
	int floppy_random_bits_max;
	int floppy_auto_ext2;
	int cd_speed;
	bool tod_hack;
	uae_u32 maprom;
	bool rom_readwrite;
	int turbo_emulation;
	bool headless;
	int filesys_limit;
	int filesys_max_name;
	int filesys_max_file_size;
	int uaescsidevmode;
	bool reset_delay;

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
	int cs_cdtvcard;
	int cs_ide;
	bool cs_pcmcia;
	bool cs_a1000ram;
	int cs_fatgaryrev;
	int cs_ramseyrev;
	int cs_agnusrev;
	int cs_deniserev;
	int cs_mbdmac;
	bool cs_cdtvscsi;
	bool cs_cdtvcr;
	bool cs_df0idhw;
	bool cs_slowmemisfast;
	bool cs_resetwarning;
	bool cs_denisenoehb;
	bool cs_dipagnus;
	bool cs_agnusbltbusybug;
	bool cs_ciatodbug;
	bool cs_z3autoconfig;
	bool cs_1mchipjumper;
	int cs_hacks;

	TCHAR romfile[MAX_DPATH];
	TCHAR romident[256];
	TCHAR romextfile[MAX_DPATH];
	uae_u32 romextfile2addr;
	TCHAR romextfile2[MAX_DPATH];
	TCHAR romextident[256];
	TCHAR acceleratorromfile[MAX_DPATH];
	TCHAR acceleratorromident[256];
	TCHAR acceleratorextromfile[MAX_DPATH];
	TCHAR acceleratorextromident[256];
	TCHAR flashfile[MAX_DPATH];
	TCHAR rtcfile[MAX_DPATH];
	TCHAR cartfile[MAX_DPATH];
	TCHAR cartident[256];
	int cart_internal;
	TCHAR pci_devices[256];
	TCHAR prtname[256];
	TCHAR sername[256];
	TCHAR amaxromfile[MAX_DPATH];
	TCHAR a2065name[MAX_DPATH];
	/*
	struct cdslot cdslots[MAX_TOTAL_SCSI_DEVICES];
	*/
	TCHAR quitstatefile[MAX_DPATH];
	TCHAR statefile[MAX_DPATH];
	TCHAR inprecfile[MAX_DPATH];
	bool inprec_autoplay;

	/*
	struct multipath path_floppy;
	struct multipath path_hardfile;
	struct multipath path_rom;
	struct multipath path_cd;
	*/

	int m68k_speed;
	double m68k_speed_throttle;
	int cpu_level;				/* Hatari */
	int cpu_model;
	int mmu_model;
	int cpu060_revision;
	int fpu_model;
	int fpu_revision;
	int ppc_mode;
	TCHAR ppc_model[32];
	bool cpu_compatible;
	bool int_no_unimplemented;
	bool fpu_no_unimplemented;
	bool address_space_24;
	bool picasso96_nocustom;
	int picasso96_modeflags;

	uae_u32 z3autoconfig_start;
	uae_u32 z3fastmem_size, z3fastmem2_size;
	uae_u32 z3chipmem_size;
	uae_u32 z3chipmem_start;
	uae_u32 fastmem_size, fastmem2_size;
	bool fastmem_autoconfig;
	uae_u32 chipmem_size;
	uae_u32 bogomem_size;
	uae_u32 mbresmem_low_size;
	uae_u32 mbresmem_high_size;
	uae_u32 rtgmem_size;
	int cpuboard_type;
	uae_u32 cpuboardmem1_size;
	uae_u32 cpuboardmem2_size;
	int ppc_implementation;
	bool rtg_hardwareinterrupt;
	bool rtg_hardwaresprite;
	int rtgmem_type;
	bool rtg_more_compatible;
	uae_u32 custom_memory_addrs[MAX_CUSTOM_MEMORY_ADDRS];
	uae_u32 custom_memory_sizes[MAX_CUSTOM_MEMORY_ADDRS];

	bool kickshifter;
	bool filesys_no_uaefsdb;
	bool filesys_custom_uaefsdb;
	bool mmkeyboard;
	int uae_hide;
	bool clipboard_sharing;
	bool native_code;
	bool uae_hide_autoconfig;
	int z3_mapping_mode;

	int mountitems;
	/*
	struct uaedev_config_data mountconfig[MOUNT_CONFIG_SIZE];
	*/

	int nr_floppies;
	/*
	struct floppyslot floppyslots[4];
	bool floppy_read_only;
	TCHAR dfxlist[MAX_SPARE_DRIVES][MAX_DPATH];
	*/
	int dfxclickvolume;
	int dfxclickchannelmask;

	/*
	TCHAR luafiles[MAX_LUA_STATES][MAX_DPATH];
	*/

	/* Target specific options */

	bool win32_middle_mouse;
	bool win32_logfile;
	bool win32_notaskbarbutton;
	bool win32_nonotificationicon;
	bool win32_alwaysontop;
	bool win32_powersavedisabled;
	bool win32_minimize_inactive;
	int win32_statusbar;
	bool win32_start_minimized;
	bool win32_start_uncaptured;

	int win32_active_capture_priority;
	bool win32_active_nocapture_pause;
	bool win32_active_nocapture_nosound;
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
#ifdef WITH_SLIRP
	struct slirp_redir slirp_redirs[MAX_SLIRP_REDIRS]; 
#endif
	int statecapturerate, statecapturebuffersize;

	/* input */

	/*
	struct jport jports[MAX_JPORTS];
	*/
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
	bool input_magic_mouse;
	int input_magic_mouse_cursor;
	/*
	int input_keyboard_type;
	struct uae_input_device joystick_settings[MAX_INPUT_SETTINGS][MAX_INPUT_DEVICES];
	struct uae_input_device mouse_settings[MAX_INPUT_SETTINGS][MAX_INPUT_DEVICES];
	struct uae_input_device keyboard_settings[MAX_INPUT_SETTINGS][MAX_INPUT_DEVICES];
	struct uae_input_device internalevent_settings[MAX_INPUT_SETTINGS][INTERNALEVENT_COUNT];
	TCHAR input_config_name[GAMEPORT_INPUT_SETTINGS][256];
	int dongle;
	*/
	int input_contact_bounce;
};

extern struct uae_prefs currprefs, changed_prefs;
extern void fixup_cpu (struct uae_prefs *prefs);


extern void check_prefs_changed_cpu (void);

#endif /* OPTIONS_CPU_H */
