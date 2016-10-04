/*
 * Simple program:  Loop, watching keystrokes
 *
 * Note that you need to call SDL_PollEvent() or SDL_WaitEvent() to 
 * pump the event loop and catch keystrokes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"

#if SDL_MAJOR_VERSION > 1
#define SDL_FULLSCREEN SDL_WINDOW_FULLSCREEN
#define SDL_keysym SDL_Keysym
#define KMOD_LMETA KMOD_LGUI
#define KMOD_RMETA KMOD_RGUI
#endif


/* Call this instead of exit(), so we can clean up SDL: atexit() is evil. */
static void quit(int rc)
{
	SDL_Quit();
	exit(rc);
}

static void print_modifiers(void)
{
	int mod;
	printf(" modifiers:");
	mod = SDL_GetModState();
	if(!mod) {
		printf(" (none)");
		return;
	}
	if(mod & KMOD_LSHIFT)
		printf(" LSHIFT");
	if(mod & KMOD_RSHIFT)
		printf(" RSHIFT");
	if(mod & KMOD_LCTRL)
		printf(" LCTRL");
	if(mod & KMOD_RCTRL)
		printf(" RCTRL");
	if(mod & KMOD_LALT)
		printf(" LALT");
	if(mod & KMOD_RALT)
		printf(" RALT");
	if(mod & KMOD_LMETA)
		printf(" LMETA");
	if(mod & KMOD_RMETA)
		printf(" RMETA");
	if(mod & KMOD_NUM)
		printf(" NUMLOCK");
	if(mod & KMOD_CAPS)
		printf(" CAPS");
	if(mod & KMOD_MODE)
		printf(" MODE");
}

static void PrintKey(SDL_keysym *sym, int pressed)
{
	/* Print the keycode, name and state */
	if ( sym->sym ) {
		printf("Key %s: 0x%2x - %s ",
		       pressed ?  "pressed " : "released",
		       sym->sym, SDL_GetKeyName(sym->sym));
	} else {
		printf("Unknown Key (scancode = 0x%2x) %s ",
		       sym->scancode,
		       pressed ?  "pressed" : "released");
	}
	print_modifiers();

#if SDL_MAJOR_VERSION < 2
	/* Print the translated character, if one exists */
	if ( sym->unicode ) {
		/* Is it a control-character? */
		if ( sym->unicode < ' ' ) {
			printf(" (^%c)", sym->unicode+'@');
		} else {
# ifdef UNICODE
			printf(" (%c)", sym->unicode);
# else
			/* This is a Latin-1 program, so only show 8-bits */
			if ( !(sym->unicode & 0xFF00) )
				printf(" (%c)", sym->unicode);
			else
				printf(" (0x%X)", sym->unicode);
# endif
		}
	}
#endif
	printf("\n");
}

int main(int argc, char *argv[])
{
#if SDL_MAJOR_VERSION > 1
	SDL_Window *window;
#endif
	Uint32 videoflags = 0;
	SDL_Event event;
	int wd = 640;
	int ht = 480;
	int done;

	/* Initialize SDL */
	if ( SDL_Init(SDL_INIT_VIDEO) < 0 ) {
		fprintf(stderr, "Couldn't initialize SDL: %s\n", SDL_GetError());
		return(1);
	}

	while( argc > 1 ) {
		--argc;
		if ( argv[argc] && !strcmp(argv[argc], "-fullscreen") ) {
			videoflags = SDL_FULLSCREEN;
		} else {
			fprintf(stderr, "Usage: %s [-fullscreen]\n", argv[0]);
			quit(1);
		}
	}

#if SDL_MAJOR_VERSION > 1
	if (videoflags & SDL_WINDOW_FULLSCREEN) {
		SDL_DisplayMode dm;
		videoflags |= SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS | SDL_WINDOW_INPUT_GRABBED;
		if (SDL_GetDesktopDisplayMode(0, &dm)) {
			fprintf(stderr, "SDL_GetDesktopDisplayMode failed: %s", SDL_GetError());
			quit(1);
		}
		wd = dm.w;
		ht = dm.h;
	} else {
		videoflags = SDL_WINDOW_RESIZABLE;
	}

	window = SDL_CreateWindow("CheckKeys",
				  SDL_WINDOWPOS_UNDEFINED,
				  SDL_WINDOWPOS_UNDEFINED,
				  wd, ht, videoflags);
	if (!window) {
		fprintf(stderr, "Failed to create %dx%d window: %s\n",
			wd, ht, SDL_GetError());
		quit(2);
	}
#else   /* SDL1 */
	videoflags |= SDL_SWSURFACE;
	if ( SDL_SetVideoMode(wd, ht, 0, videoflags) == NULL ) {
		fprintf(stderr, "Couldn't set %dx%d video mode: %s\n",
			wd, ht, SDL_GetError());
		quit(2);
	}

	/* Enable UNICODE translation for keyboard input */
	SDL_EnableUNICODE(1);

	/* Enable auto repeat for keyboard input */
	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY,
	                    SDL_DEFAULT_REPEAT_INTERVAL);
#endif

	puts("Click to the window to quit.\n");

	/* Watch keystrokes */
	done = 0;
	while ( !done ) {
		/* Check for events */
		SDL_WaitEvent(&event);
		switch (event.type) {
			case SDL_KEYDOWN:
				PrintKey(&event.key.keysym, 1);
				break;
			case SDL_KEYUP:
				PrintKey(&event.key.keysym, 0);
				break;
			case SDL_MOUSEBUTTONDOWN:
				/* Any button press quits the app... */
			case SDL_QUIT:
				done = 1;
				break;
			default:
				break;
		}
	}

	SDL_Quit();
	return 0;
}
