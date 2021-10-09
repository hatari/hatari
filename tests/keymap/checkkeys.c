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


/* Call this instead of exit(), so we can clean up SDL: atexit() is evil. */
static void quit(int rc)
{
	SDL_Quit();
	exit(rc);
}

static void print_modifiers(void)
{
	int mod;
	printf(" -");
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
	if(mod & KMOD_LGUI)
		printf(" LGUI");
	if(mod & KMOD_RGUI)
		printf(" RGUI");
	if(mod & KMOD_NUM)
		printf(" NUMLOCK");
	if(mod & KMOD_CAPS)
		printf(" CAPS");
	if(mod & KMOD_MODE)
		printf(" MODE");
}

static void PrintKey(SDL_Keysym *sym, int pressed)
{
	/* Print the keycode, scancode, their names names and state */
	if ( sym->sym ) {
		printf("Key %s: 0x%2x/0x%2x (%d/%d) - %s - %s",
		       pressed ?  "pressed " : "released",
		       sym->sym, sym->scancode,
		       sym->sym, sym->scancode,
		       SDL_GetKeyName(sym->sym),
		       SDL_GetScancodeName(sym->scancode));
	} else {
		printf("Unknown Key, scancode = 0x%2x (%d) - %s",
		       sym->scancode, sym->scancode,
		       pressed ?  "pressed" : "released");
	}
	print_modifiers();

	printf("\n");
}

int main(int argc, char *argv[])
{
	SDL_Window *window;
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
			videoflags = SDL_WINDOW_FULLSCREEN;
		} else {
			fprintf(stderr, "Usage: %s [-fullscreen]\n", argv[0]);
			quit(1);
		}
	}

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

	puts("Click to the window to quit.\n");

	/* Watch keystrokes */
	done = 0;
	puts("Status: hex sym/scan (dec) - sym - scan - modifiers\n");
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
