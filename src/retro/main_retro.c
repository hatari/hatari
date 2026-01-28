
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <libretro.h>

#include "main.h"
#include "main_retro.h"
#include "hatari-glue.h"
#include "dialog.h"
#include "m68000.h"
#include "reset.h"
#include "screen.h"
#include "sound.h"
#include "tos.h"
#include "vdi.h"
#include "version.h"

static bool has_cpu_config_changed = true;

retro_environment_t environment_cb;
retro_video_refresh_t video_refresh_cb;
retro_input_poll_t input_poll_cb;
retro_input_state_t input_state_cb;


RETRO_API void retro_set_environment(retro_environment_t cb)
{
	static enum retro_pixel_format pixelformat = RETRO_PIXEL_FORMAT_XRGB8888;
	static bool no_game = true;

	environment_cb = cb;

	/* Hatari only supports 32-bit bits per pixel */
	cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixelformat);

	/* Hatari can start without game disks */
	cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)
{
	video_refresh_cb = cb;
}

RETRO_API void retro_set_input_poll(retro_input_poll_t cb)
{
	input_poll_cb = cb;
}

RETRO_API void retro_set_input_state(retro_input_state_t cb)
{
	input_state_cb = cb;
}

RETRO_API unsigned retro_api_version(void)
{
	return RETRO_API_VERSION;
}

RETRO_API void retro_init(void)
{
	char name[] = "hatari";
	char *argv[1] = { name };

	Main_Init(1, (char **)argv);
}

RETRO_API void retro_deinit(void)
{
	Main_UnInit();
}

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
	memset(info, 0, sizeof(*info));
	info->library_name = "hatari";
	info->library_version = PROG_NAME;
	info->need_fullpath = false;
	info->valid_extensions = "st|msa|dim|stx";
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
	uint32_t *pixels;
	int width, height, pitch;

	memset(info, 0, sizeof(*info));

	Screen_GetDimension(&pixels, &width, &height, &pitch);
	info->geometry.base_width = width;
	info->geometry.base_height = height;
	info->geometry.max_width = MAX_VDI_WIDTH;
	info->geometry.max_height = MAX_VDI_HEIGHT;
	info->geometry.aspect_ratio = (float)width / (float)height;
	info->timing.fps = 50.0f;

	info->timing.sample_rate = nAudioFrequency;
}

RETRO_API void retro_reset(void)
{
	Reset_Warm();
}

RETRO_API void retro_run(void)
{
	M68000_UnsetSpecial(SPCFLAG_BRK);

	if (has_cpu_config_changed)
	{
		has_cpu_config_changed = false;
		UAE_Set_Quit_Reset(false);
		m68k_go(true);
	}
	else
	{
		quit_program = 0;
		m68k_run();
	}
/*
	int width, height, pitch;
	uint32_t *pixels;

	Screen_GetDimension(&pixels, &width, &height, &pitch);
	video_refresh_cb(pixels, width, height, pitch);
*/
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
}

RETRO_API size_t retro_serialize_size(void)
{
	return 0;
}

RETRO_API bool retro_serialize(void *data, size_t size)
{
	return false;
}

RETRO_API bool retro_unserialize(const void *data, size_t size)
{
	return false;
}

RETRO_API void retro_cheat_reset(void)
{
}

RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
}

RETRO_API bool retro_load_game(const struct retro_game_info *game)
{
	return true;
}

RETRO_API bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
	return false;
}

RETRO_API void retro_unload_game(void)
{
}

RETRO_API unsigned retro_get_region(void)
{
	return RETRO_REGION_PAL;
}

RETRO_API void* retro_get_memory_data(unsigned id)
{
	// This interface seems to be for automatically creating save files,
	// but this core should save to specially named floppy files image instead.
	return NULL;
}

RETRO_API size_t retro_get_memory_size(unsigned id)
{
	return 0;
}


void Main_RequestQuit(int exitval)
{
}

/**
 * Set exit value and enable quit flag
 */
void Main_SetQuitValue(int exitval)
{
	bQuitProgram = true;
	M68000_SetSpecial(SPCFLAG_BRK);
}


/**
 * Error exit wrapper
 */
void Main_ErrorExit(const char *msg1, const char *msg2, int errval)
{
	if (msg1)
	{
		if (msg2)
			Log_Printf(LOG_ERROR, "%s - %s\n", msg1, msg2);
		else
			Log_Printf(LOG_ERROR, "%s\n", msg1);
	}

	bQuitProgram = true;
	M68000_SetSpecial(SPCFLAG_BRK);
}


bool DlgAlert_Query(const char *text)
{
	Log_Printf(LOG_DEBUG, "DlgAlert_Query: %s\n", text);
	return false;
}

bool DlgAlert_Notice(const char *text)
{
	Log_Printf(LOG_DEBUG, "DlgAlert_Notice: %s\n", text);
	return false;
}

void Dialog_HaltDlg(void)
{
}

int Dialog_MainDlg(bool *bReset, bool *bLoadedSnapshot)
{
	*bReset = false;
	*bLoadedSnapshot = false;
	return 0;
}

char* DlgFloppy_ShortCutSel(const char *path_and_name, char **zip_path)
{
	return NULL;
}
