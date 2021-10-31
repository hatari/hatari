/* Print out all the keysyms we have, just to verify them */

#include <stdio.h>
#include "SDL.h"

int main(int argc, char *argv[])
{
	int count;
	SDL_Scancode scan;
	/* key names are queried from host display subsystem */
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "Couldn't initialize SDL: %s\n",
			SDL_GetError());
		exit(1);
	}
	count = 0;
	printf("Available SDL scancodes:\n");
	for (scan = SDL_SCANCODE_UNKNOWN; scan < SDL_NUM_SCANCODES; scan++) {
		if (SDL_GetKeyFromScancode(scan)) {
			printf("- 0x%03x (%d): \"%s\"\n",
		       scan, scan, SDL_GetScancodeName(scan));
			count++;
		}
	}
	printf("= %d scancodes.\n", count);
	SDL_Quit();
	return 0;
}
