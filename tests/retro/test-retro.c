/*
 * Test that we can successfully load the libretro core and run it
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <libretro.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void (*lr_set_environment)(retro_environment_t cb);
static void (*lr_set_video_refresh)(retro_video_refresh_t cb);
static void (*lr_set_input_poll)(retro_input_poll_t cb);
static void (*lr_set_input_state)(retro_input_state_t cb);
static unsigned int (*lr_api_version)(void);
static void (*lr_init)(void);
static void (*lr_deinit)(void);
static void (*lr_run)(void);

static bool screen_refreshed;


static void *test_dlsym(void *dlh, const char *symname)
{
	void *fun;

	fun = dlsym(dlh, symname);
	if (!fun)
	{
		fprintf(stderr, "Failed to get function symbol '%s':\n%s\n",
		        symname, dlerror());
		exit(EXIT_FAILURE);
	}

	return fun;
}


static void init_funcs(void *dlh)
{
	lr_set_environment = test_dlsym(dlh, "retro_set_environment");
	lr_set_video_refresh= test_dlsym(dlh, "retro_set_video_refresh");
	lr_set_input_poll = test_dlsym(dlh, "retro_set_input_poll");
	lr_set_input_state = test_dlsym(dlh, "retro_set_input_state");
	lr_api_version = test_dlsym(dlh, "retro_api_version");
	lr_init = test_dlsym(dlh, "retro_init");
	lr_deinit = test_dlsym(dlh, "retro_deinit");
	lr_run = test_dlsym(dlh, "retro_run");
}


static bool env_cb(unsigned cmd, void *data)
{
	switch (cmd)
	{
	 case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
	 case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
	 case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
	 case RETRO_ENVIRONMENT_SET_GEOMETRY:
		return true;
	 default:
		fprintf(stderr, "Unexpected env setting 0x%x\n", cmd);
		return false;
	}
}

static void refresh_cb(const void *data, unsigned width, unsigned height,
                       size_t pitch)
{
	assert(data && width && height && pitch);
	screen_refreshed = true;
}

static int16_t input_state_cb(unsigned int port, unsigned intdevice,
                              unsigned int index, unsigned int id)
{
	return 0;
}

int main(int argc, char *argv[])
{
	void *dlh;

	if (argc != 2 || argv[1][0] == '-')
	{
		printf("Usage: %s <path-to-libretro-hatari.so>\n", argv[0]);
		return 0;
	}

	printf("Testing dlopen:\t\t\t\t");
	dlh = dlopen(argv[1], RTLD_NOW);
	if (!dlh)
	{
		puts("ERROR");
		fprintf(stderr, "Failed to open '%s':\n%s\n", argv[1],
		        dlerror());
		return EXIT_FAILURE;
	}
	puts("OK");

	init_funcs(dlh);

	printf("Testing retro_api_version:\t\t");
	if (lr_api_version() != RETRO_API_VERSION)
	{
		puts("ERROR");
		fprintf(stderr, "Wrong RETRO_API_VERSION!\n");
		return EXIT_FAILURE;
	}
	puts("OK");

	printf("Setting retro_set_environment:\t\t");
	lr_set_environment(env_cb);
	puts("OK");

	printf("Setting retro_set_video_refresh:\t");
	lr_set_video_refresh(refresh_cb);
	puts("OK");

	printf("Setting retro_set_input_state:\t\t");
	lr_set_input_state(input_state_cb);
	puts("OK");

#if 0	/* This only works if we can be sure that Hatari can load a tos.img */
	printf("Initializing core...\n");
	lr_init();

	printf("Testing retro_run:\t\t\t");
	screen_refreshed = false;
	lr_run();
	lr_run();
	if (!screen_refreshed)
	{
		puts("ERROR: Screen has not been refreshed");
		return EXIT_FAILURE;
	}
	puts("OK");

	printf("Testing retro_deinit:\t\t\t");
	lr_deinit();
	puts("OK");
#endif

	dlclose(dlh);

	puts("All tests finished successfully.");

	return 0;
}
