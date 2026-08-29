/*
  Hatari - options.c
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libretro.h>

#include "configuration.h"
#include "fdc.h"
#include "main_retro.h"
#include "options.h"
#include "reset.h"
#include "video.h"

/* ------------------------------------------------------------------- */
/* Categories                                                          */
/* ------------------------------------------------------------------- */
static struct retro_core_option_v2_category option_cats_us[] = {
   { "system",  "System",       "Configure the emulated Atari machine." },
   { "cpu",     "CPU / FPU",    "Configure CPU and FPU emulation." },
   { "rom",     "ROM",          "Configure TOS ROM handling." },
   { "memory",  "Memory",       "Configure ST-RAM and TT-RAM size." },
   { "floppy",  "Floppy Disks", "Configure floppy disk drive emulation." },
   { "screen",  "Atari Screen", "Configure the emulated Atari video output." },
   { "devices", "Devices",      "Configure joystick ports." },
   { NULL, NULL, NULL },
};

/* ------------------------------------------------------------------- */
/* Definitions                                                         */
/* ------------------------------------------------------------------- */
static struct retro_core_option_v2_definition option_defs_us[] = {

   /* ---------------- System ---------------- */
   {
      "hatari_machinetype",
      "Machine Type",
      "Machine Type",
      "Select the Atari machine to emulate. Requires restarting content.",
      NULL,
      "system",
      {
         { "st",      "ST" },
         { "megast",  "Mega ST" },
         { "ste",     "STE" },
         { "megaste", "Mega STE" },
         { "tt",      "TT" },
         { "falcon",  "Falcon" },
         { NULL, NULL },
      },
      "st"
   },
   {
      "hatari_dsp_type",
      "DSP Emulation (Falcon)",
      "DSP Emulation (Falcon)",
      "Select how the Falcon DSP is emulated. Requires restarting content.",
      NULL,
      "system",
      {
         { "none",  "None" },
         { "dummy", "Dummy" },
         { "emu",   "Emulated" },
         { NULL, NULL },
      },
      "none"
   },
   {
      "hatari_blitter",
      "Blitter (ST/STE)",
      "Blitter (ST/STE)",
      "Enable Blitter emulation. Requires restarting content.",
      NULL,
      "system",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "hatari_fastboot",
      "Fast Boot",
      "Fast Boot",
      "Patch TOS and memory-valid system variables for a faster boot. Requires restarting content.",
      NULL,
      "system",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "hatari_timerd",
      "Patch Timer-D",
      "Patch Timer-D",
      "Patch Timer-D, roughly doubling ST emulation speed. Requires restarting content.",
      NULL,
      "system",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },

   /* ---------------- CPU / FPU ---------------- */
   {
      "hatari_cpu_level",
      "CPU Type",
      "CPU Type",
      "Select the emulated 680x0 CPU level. Requires restarting content.",
      NULL,
      "cpu",
      {
         { "0", "68000" },
         { "1", "68010" },
         { "2", "68020" },
         { "3", "68030" },
         { "4", "68040" },
         { "6", "68060" },
         { NULL, NULL },
      },
      "0"
   },
   {
      "hatari_cpu_clock",
      "CPU Clock",
      "CPU Clock",
      "Select the emulated CPU clock speed. Requires restarting content.",
      NULL,
      "cpu",
      {
         { "8",  "8 MHz" },
         { "16", "16 MHz" },
         { "32", "32 MHz" },
         { NULL, NULL },
      },
      "8"
   },
   {
      "hatari_cpu_compatible",
      "Prefetch Mode",
      "Prefetch Mode",
      "Use a more compatible (but slower) CPU prefetch mode. Requires restarting content.",
      NULL,
      "cpu",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "hatari_cpu_cycle_exact",
      "Cycle Exact CPU",
      "Cycle Exact CPU",
      "Use cycle exact CPU emulation. Requires restarting content.",
      NULL,
      "cpu",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "hatari_cpu_data_cache",
      "CPU Data Cache",
      "CPU Data Cache",
      "Emulate the CPU data cache on CPUs that support it (>=68030). Requires restarting content.",
      NULL,
      "cpu",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "hatari_cpu_addr24",
      "24-bit Addressing",
      "24-bit Addressing",
      "Use 24-bit instead of 32-bit addressing mode. Requires restarting content.",
      NULL,
      "cpu",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "hatari_fpu_type",
      "FPU Type",
      "FPU Type",
      "Select the emulated FPU. Requires restarting content.",
      NULL,
      "cpu",
      {
         { "none",     "None" },
         { "68881",    "68881" },
         { "68882",    "68882" },
         { "internal", "Internal (CPU)" },
         { NULL, NULL },
      },
      "none"
   },
   {
      "hatari_fpu_softfloat",
      "Software FPU",
      "Software FPU",
      "Use full software FPU emulation instead of the faster core. Requires restarting content.",
      NULL,
      "cpu",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "hatari_mmu",
      "MMU Emulation",
      "MMU Emulation",
      "Enable MMU emulation. Requires restarting content.",
      NULL,
      "cpu",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },

   /* ---------------- ROM ---------------- */
   {
      "hatari_patch_tos",
      "Patch TOS",
      "Patch TOS",
      "Apply Hatari's compatibility patches to the loaded TOS image. Requires restarting content.",
      NULL,
      "rom",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "enabled"
   },

   /* ---------------- Memory ---------------- */
   {
      "hatari_memory_size",
      "ST-RAM Size",
      "ST-RAM Size",
      "Amount of ST-RAM to emulate. Requires restarting content.",
      NULL,
      "memory",
      {
         { "512",   "512 KB" },
         { "1024",  "1 MB" },
         { "2048",  "2 MB" },
         { "2560",  "2.5 MB" },
         { "4096",  "4 MB" },
         { "8192",  "8 MB" },
         { "10240", "10 MB" },
         { "14336", "14 MB" },
         { NULL, NULL },
      },
      "1024"
   },
   {
      "hatari_ttram_size",
      "TT-RAM Size",
      "TT-RAM Size",
      "Amount of 32-bit TT-RAM to emulate in addition to ST-RAM. Requires restarting content.",
      NULL,
      "memory",
      {
         { "0",    "Disabled" },
         { "4",    "4 MB" },
         { "8",    "8 MB" },
         { "16",   "16 MB" },
         { "32",   "32 MB" },
         { "64",   "64 MB" },
         { "128",  "128 MB" },
         { "256",  "256 MB" },
         { "512",  "512 MB" },
         { "1024", "1024 MB" },
         { NULL, NULL },
      },
      "0"
   },

   /* ---------------- Floppy Disks ---------------- */
   {
      "hatari_auto_insert_disk_b",
      "Auto Insert Disk B",
      "Auto Insert Disk B",
      "When a disk is inserted into drive A, automatically look for and insert a matching disk B image (e.g. 'game_a.st' -> 'game_b.st').",
      NULL,
      "floppy",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "hatari_fast_floppy",
      "Fast Floppy Access",
      "Fast Floppy Access",
      "Speed up floppy disk controller emulation. Can break programs relying on accurate FDC timing.",
      NULL,
      "floppy",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "hatari_floppy_write_protection",
      "Floppy Write Protection",
      "Floppy Write Protection",
      "Write protect floppy disk image contents. 'Auto' checks the host file's write permissions.",
      NULL,
      "floppy",
      {
         { "off",  "Off" },
         { "on",   "On" },
         { "auto", "Auto" },
         { NULL, NULL },
      },
      "off"
   },
   {
      "hatari_drive_a_enable",
      "Drive A Enabled",
      "Drive A Enabled",
      "Enable emulated floppy drive A.",
      NULL,
      "floppy",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "hatari_drive_a_heads",
      "Drive A Heads",
      "Drive A Heads",
      "Number of heads for drive A.",
      NULL,
      "floppy",
      {
         { "1", "Single Sided" },
         { "2", "Double Sided" },
         { NULL, NULL },
      },
      "2"
   },
   {
      "hatari_drive_b_enable",
      "Drive B Enabled",
      "Drive B Enabled",
      "Enable emulated floppy drive B.",
      NULL,
      "floppy",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "hatari_drive_b_heads",
      "Drive B Heads",
      "Drive B Heads",
      "Number of heads for drive B.",
      NULL,
      "floppy",
      {
         { "1", "Single Sided" },
         { "2", "Double Sided" },
         { NULL, NULL },
      },
      "2"
   },

   /* ---------------- Atari Screen ---------------- */
   {
      "hatari_monitor_type",
      "Monitor Type",
      "Monitor Type",
      "Select the emulated monitor type. Requires restarting content for full effect.",
      NULL,
      "screen",
      {
         { "mono", "Monochrome" },
         { "rgb",  "RGB / Color" },
         { "vga",  "VGA (TT/Falcon)" },
         { "tv",   "TV (Falcon)" },
         { NULL, NULL },
      },
      "rgb"
   },
   {
      "hatari_video_timing",
      "Video Timing (ST/STE)",
      "Video Timing (ST/STE)",
      "Wakeup state used for MMU/GLUE video timing emulation on ST/STE.",
      NULL,
      "screen",
      {
         { "random", "Random" },
         { "ws1",    "WS1" },
         { "ws2",    "WS2" },
         { "ws3",    "WS3 (default)" },
         { "ws4",    "WS4" },
         { NULL, NULL },
      },
      "ws3"
   },
   {
      "hatari_borders",
      "Show Screen Borders",
      "Show Screen Borders",
      "Show ST/STE screen borders (needed by some overscan demos).",
      NULL,
      "screen",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "hatari_spec512_threshold",
      "Spec512 Palette Threshold",
      "Spec512 Palette Threshold",
      "Number of palette changes per line above which Spec512-style images are detected. 'Disabled' turns off Spec512 support.",
      NULL,
      "screen",
      {
         { "0",   "Disabled" },
         { "1",   "1" },
         { "4",   "4" },
         { "16",  "16" },
         { "64",  "64" },
         { "128", "128" },
         { "192", "192" },
         { "256", "256" },
         { "512", "512" },
         { NULL, NULL },
      },
      "1"
   },
   {
      "hatari_aspect_correct",
      "Aspect Ratio Correction (TT/Falcon)",
      "Aspect Ratio Correction (TT/Falcon)",
      "Correct the monitor aspect ratio for TT/Falcon video modes.",
      NULL,
      "screen",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },

   /* ---------------- Devices ---------------- */
   {
      "hatari_joystick_port0",
      "Joystick Port 0",
      "Joystick Port 0",
      "Select how ST joystick port 0 is controlled.",
      NULL,
      "devices",
      {
         { "none", "Disabled" },
         { "keys", "Emulated with keyboard" },
         { "real", "RetroPad" },
         { NULL, NULL },
      },
      "none"
   },
   {
      "hatari_joystick_port1",
      "Joystick Port 1",
      "Joystick Port 1",
      "Select how ST joystick port 1 is controlled.",
      NULL,
      "devices",
      {
         { "none", "Disabled" },
         { "keys", "Emulated with keyboard" },
         { "real", "RetroPad" },
         { NULL, NULL },
      },
      "real"
   },
   {
      "hatari_joystick_autofire",
      "Joystick Autofire",
      "Joystick Autofire",
      "Enable autofire on the emulated joystick ports.",
      NULL,
      "devices",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "hatari_joystick_jump_fire2",
      "Joystick Button 2 = Jump",
      "Joystick Button 2 = Jump",
      "Map the second RetroPad fire button to Up (jump), for games that use it as a shortcut.",
      NULL,
      "devices",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },

   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};

static struct retro_core_options_v2 options_us = {
   option_cats_us,
   option_defs_us
};

/* ------------------------------------------------------------------- */
/* Registration helper, fallback for older frontends                   */
/* ------------------------------------------------------------------- */
void libretro_set_core_options(retro_environment_t environ_cb,
      bool *categories_supported)
{
   unsigned version = 0;

   if (!environ_cb || !categories_supported)
      return;

   *categories_supported = false;

   if (!environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version))
      version = 0;

   if (version >= 2)
   {
      *categories_supported = environ_cb(
            RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &options_us);
   }
   else if (version == 1)
   {
      /* Flatten option_defs_us into a v1 (non-categorized) array */
      size_t i;
      size_t num_options = 0;
      struct retro_core_option_definition *option_v1_defs;

      for (; option_defs_us[num_options].key; num_options++) ;

      option_v1_defs = (struct retro_core_option_definition *)
            calloc(num_options + 1, sizeof(*option_v1_defs));
      if (!option_v1_defs)
         return;

      for (i = 0; i < num_options; i++)
      {
         struct retro_core_option_v2_definition *v2_def = &option_defs_us[i];
         struct retro_core_option_definition *v1_def     = &option_v1_defs[i];
         size_t j;

         v1_def->key           = v2_def->key;
         v1_def->desc          = v2_def->desc;
         v1_def->info          = v2_def->info;
         v1_def->default_value = v2_def->default_value;

         for (j = 0; j < RETRO_NUM_CORE_OPTION_VALUES_MAX; j++)
         {
            v1_def->values[j].value = v2_def->values[j].value;
            v1_def->values[j].label = v2_def->values[j].label;
            if (!v2_def->values[j].value)
               break;
         }
      }

      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, option_v1_defs);
      free(option_v1_defs);
   }
   else
   {
      /* Legacy interface: convert into a retro_variable array */
      size_t i;
      size_t num_options = 0;
      struct retro_variable *variables;
      char **values_buf;

      for (; option_defs_us[num_options].key; num_options++) ;

      variables  = (struct retro_variable *)
            calloc(num_options + 1, sizeof(*variables));
      values_buf = (char **)calloc(num_options, sizeof(*values_buf));

      if (!variables || !values_buf)
      {
         free(variables);
         free(values_buf);
         return;
      }

      for (i = 0; i < num_options; i++)
      {
         struct retro_core_option_v2_definition *option_def = &option_defs_us[i];
         const char *default_value = option_def->default_value;
         size_t buf_len = strlen(option_def->desc) + 4;
         size_t j;

         for (j = 0; option_def->values[j].value; j++)
            buf_len += strlen(option_def->values[j].value) + 2;

         values_buf[i] = (char *)calloc(buf_len, sizeof(char));
         if (!values_buf[i])
            continue;

         strcpy(values_buf[i], option_def->desc);
         strcat(values_buf[i], "; ");

         /* Default value goes first, as required by the interface */
         if (default_value)
         {
            strcat(values_buf[i], default_value);
         }
         for (j = 0; option_def->values[j].value; j++)
         {
            const char *value = option_def->values[j].value;

            if (default_value && strcmp(value, default_value) == 0)
               continue;

            strcat(values_buf[i], "|");
            strcat(values_buf[i], value);
         }

         variables[i].key   = option_def->key;
         variables[i].value = values_buf[i];
      }

      environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);

      for (i = 0; i < num_options; i++)
         free(values_buf[i]);
      free(values_buf);
      free(variables);
   }
}

/* ------------------------------------------------------------------- */
/* Core options helpers                                                */
/* ------------------------------------------------------------------- */
static bool Core_GetVariable(const char *key, struct retro_variable *var)
{
	var->key = key;
	var->value = NULL;
	return environment_cb(RETRO_ENVIRONMENT_GET_VARIABLE, var) && var->value;
}
 
static bool Core_VarBool(const char *key, bool defval)
{
	struct retro_variable var;
	if (Core_GetVariable(key, &var))
		return (strcmp(var.value, "enabled") == 0);
	return defval;
}
 
static int Core_VarInt(const char *key, int defval)
{
	struct retro_variable var;
	if (Core_GetVariable(key, &var))
		return atoi(var.value);
	return defval;
}
 
static const char *Core_VarStr(const char *key, const char *defval)
{
	struct retro_variable var;
	if (Core_GetVariable(key, &var))
		return var.value;
	return defval;
}

/**
 * Apply boot options.
 */
void Core_ApplyBootOptions(void)
{
	const char *str;
 
	/* --- System --- */
	str = Core_VarStr("hatari_machinetype", "st");
	if (!strcmp(str, "st"))
		ConfigureParams.System.nMachineType = MACHINE_ST;
	else if (!strcmp(str, "megast"))
		ConfigureParams.System.nMachineType = MACHINE_MEGA_ST;
	else if (!strcmp(str, "ste"))
		ConfigureParams.System.nMachineType = MACHINE_STE;
	else if (!strcmp(str, "megaste"))
		ConfigureParams.System.nMachineType = MACHINE_MEGA_STE;
	else if (!strcmp(str, "tt"))
		ConfigureParams.System.nMachineType = MACHINE_TT;
	else if (!strcmp(str, "falcon"))
		ConfigureParams.System.nMachineType = MACHINE_FALCON;
 
	str = Core_VarStr("hatari_dsp_type", "none");
	if (!strcmp(str, "none"))
		ConfigureParams.System.nDSPType = DSP_TYPE_NONE;
	else if (!strcmp(str, "dummy"))
		ConfigureParams.System.nDSPType = DSP_TYPE_DUMMY;
#if ENABLE_DSP_EMU
	else if (!strcmp(str, "emu"))
		ConfigureParams.System.nDSPType = DSP_TYPE_EMU;
#endif
 
	ConfigureParams.System.bBlitter = Core_VarBool("hatari_blitter", false);
	ConfigureParams.System.bFastBoot = Core_VarBool("hatari_fastboot", false);
	ConfigureParams.System.bPatchTimerD = Core_VarBool("hatari_timerd", false);
 
	/* --- CPU / FPU --- */
	{
		int level = Core_VarInt("hatari_cpu_level", 0);
		/* 68060 is represented as level 5, see options.c */
		ConfigureParams.System.nCpuLevel = (level == 6) ? 5 : level;
	}
	Configuration_ChangeCpuFreq(Core_VarInt("hatari_cpu_clock", 8));
 
	ConfigureParams.System.bCompatibleCpu = Core_VarBool("hatari_cpu_compatible", true);
	ConfigureParams.System.bCycleExactCpu = Core_VarBool("hatari_cpu_cycle_exact", true);
	ConfigureParams.System.bCpuDataCache = Core_VarBool("hatari_cpu_data_cache", true);
	ConfigureParams.System.bAddressSpace24 = Core_VarBool("hatari_cpu_addr24", true);
 
	str = Core_VarStr("hatari_fpu_type", "none");
	if (!strcmp(str, "none"))
		ConfigureParams.System.n_FPUType = FPU_NONE;
	else if (!strcmp(str, "68881"))
		ConfigureParams.System.n_FPUType = FPU_68881;
	else if (!strcmp(str, "68882"))
		ConfigureParams.System.n_FPUType = FPU_68882;
	else if (!strcmp(str, "internal"))
		ConfigureParams.System.n_FPUType = FPU_CPU;
 
	ConfigureParams.System.bSoftFloatFPU = Core_VarBool("hatari_fpu_softfloat", false);
	ConfigureParams.System.bMMU = Core_VarBool("hatari_mmu", false);
 
	/* --- ROM --- */
	ConfigureParams.Rom.bPatchTos = Core_VarBool("hatari_patch_tos", true);
 
	/* --- Memory --- */
	ConfigureParams.Memory.STRamSize_KB = Core_VarInt("hatari_memory_size", 1024);
	ConfigureParams.Memory.TTRamSize_KB = Core_VarInt("hatari_ttram_size", 0) * 1024;
 
	/* Reconfigure and reboot so machine/CPU/memory/ROM changes take effect */
	Configuration_Apply(true);
	Reset_Cold();
	has_cpu_config_changed = true;
}
 
/**
 * Apply live changes.
 */
void Core_ApplyRuntimeOptions(void)
{
	const char *str;
	int i;
 
	/* --- Floppy Disks --- */
	ConfigureParams.DiskImage.bAutoInsertDiskB = Core_VarBool("hatari_auto_insert_disk_b", true);
	ConfigureParams.DiskImage.FastFloppy = Core_VarBool("hatari_fast_floppy", false);
 
	str = Core_VarStr("hatari_floppy_write_protection", "off");
	if (!strcmp(str, "off"))
		ConfigureParams.DiskImage.nWriteProtection = WRITEPROT_OFF;
	else if (!strcmp(str, "on"))
		ConfigureParams.DiskImage.nWriteProtection = WRITEPROT_ON;
	else if (!strcmp(str, "auto"))
		ConfigureParams.DiskImage.nWriteProtection = WRITEPROT_AUTO;
 
	ConfigureParams.DiskImage.EnableDriveA = Core_VarBool("hatari_drive_a_enable", true);
	ConfigureParams.DiskImage.EnableDriveB = Core_VarBool("hatari_drive_b_enable", true);
	ConfigureParams.DiskImage.DriveA_NumberOfHeads = Core_VarInt("hatari_drive_a_heads", 2);
	ConfigureParams.DiskImage.DriveB_NumberOfHeads = Core_VarInt("hatari_drive_b_heads", 2);
 
	FDC_Drive_Set_Enable(0, ConfigureParams.DiskImage.EnableDriveA);
	FDC_Drive_Set_Enable(1, ConfigureParams.DiskImage.EnableDriveB);
	FDC_Drive_Set_NumberOfHeads(0, ConfigureParams.DiskImage.DriveA_NumberOfHeads);
	FDC_Drive_Set_NumberOfHeads(1, ConfigureParams.DiskImage.DriveB_NumberOfHeads);
 
	/* --- Atari Screen --- */
	str = Core_VarStr("hatari_monitor_type", "rgb");
	if (!strcmp(str, "mono"))
		ConfigureParams.Screen.nMonitorType = MONITOR_TYPE_MONO;
	else if (!strcmp(str, "rgb"))
		ConfigureParams.Screen.nMonitorType = MONITOR_TYPE_RGB;
	else if (!strcmp(str, "vga"))
		ConfigureParams.Screen.nMonitorType = MONITOR_TYPE_VGA;
	else if (!strcmp(str, "tv"))
		ConfigureParams.Screen.nMonitorType = MONITOR_TYPE_TV;
 
	ConfigureParams.Screen.bAllowOverscan = Core_VarBool("hatari_borders", false);
	ConfigureParams.Screen.nSpec512Threshold = Core_VarInt("hatari_spec512_threshold", 1);
	ConfigureParams.Screen.bAspectCorrect = Core_VarBool("hatari_aspect_correct", true);
 
	str = Core_VarStr("hatari_video_timing", "ws3");
	{
		int mode = VIDEO_TIMING_MODE_WS3;
		if (!strcmp(str, "random"))
			mode = VIDEO_TIMING_MODE_RANDOM;
		else if (!strcmp(str, "ws1"))
			mode = VIDEO_TIMING_MODE_WS1;
		else if (!strcmp(str, "ws2"))
			mode = VIDEO_TIMING_MODE_WS2;
		else if (!strcmp(str, "ws3"))
			mode = VIDEO_TIMING_MODE_WS3;
		else if (!strcmp(str, "ws4"))
			mode = VIDEO_TIMING_MODE_WS4;
 
		ConfigureParams.System.VideoTimingMode = mode;
		Video_SetTimings(ConfigureParams.System.nMachineType, mode);
	}
 
	/* Reflect the (possibly changed) monitor type in the running config,
	 * without forcing a full CPU/memory reset */
	Configuration_Apply(true);
 
	/* --- Devices --- */
	{
		static const struct { const char *name; JOYSTICKMODE mode; } joymodes[] = {
			{ "none", JOYSTICK_DISABLED },
			{ "keys", JOYSTICK_KEYBOARD },
			{ "real", JOYSTICK_REALSTICK },
		};
		const int n_modes = (int)(sizeof(joymodes) / sizeof(joymodes[0]));
 
		str = Core_VarStr("hatari_joystick_port0", "none");
		for (i = 0; i < n_modes; i++)
			if (!strcmp(str, joymodes[i].name))
				ConfigureParams.Joysticks.Joy[0].nJoystickMode = joymodes[i].mode;
 
		str = Core_VarStr("hatari_joystick_port1", "real");
		for (i = 0; i < n_modes; i++)
			if (!strcmp(str, joymodes[i].name))
				ConfigureParams.Joysticks.Joy[1].nJoystickMode = joymodes[i].mode;
	}
 
	{
		bool autofire  = Core_VarBool("hatari_joystick_autofire", false);
		bool jumpfire2 = Core_VarBool("hatari_joystick_jump_fire2", true);
 
		for (i = 0; i < JOYSTICK_COUNT; i++)
		{
			ConfigureParams.Joysticks.Joy[i].bEnableAutoFire = autofire;
			ConfigureParams.Joysticks.Joy[i].bEnableJumpOnFire2 = jumpfire2;
		}
	}
}
