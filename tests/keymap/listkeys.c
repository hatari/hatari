/* Print out all the keysyms we have, just to verify them */

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"

int main(int argc, char *argv[])
{
#if SDL_MAJOR_VERSION > 1
	#warning "SDL2 doesn't support iterating over all keys"
	printf("Sorry, SDL2 doesn't support iterating over all keys.\n");
#else
	SDLKey key;
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "Couldn't initialize SDL: %s\n",
			SDL_GetError());
		exit(1);
	}

	for (key = SDLK_FIRST; key < SDLK_LAST; ++key) {
		printf("Key #%d, \"%s\"\n", key, SDL_GetKeyName(key));
	}
	SDL_Quit();
#endif
	return 0;
}
