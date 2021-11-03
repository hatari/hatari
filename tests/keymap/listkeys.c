/* Print out all the keysyms we have, just to verify them */

#include <stdio.h>
#include "SDL.h"

int main(int argc, char *argv[])
{
	const char *name;
	int count, empty;
	SDL_Keycode key, kmin, kmax;
	SDL_Scancode scan;

	/* key names are queried from host display subsystem */
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "Couldn't initialize SDL: %s\n",
			SDL_GetError());
		exit(1);
	}

	count = 0;
	printf("Available SDL scancodes (with corresponding keycode):\n");
	for (scan = SDL_SCANCODE_UNKNOWN; scan < SDL_NUM_SCANCODES; scan++) {
		if (!SDL_GetKeyFromScancode(scan)) {
			continue;
		}
		printf("- 0x%03x (%3d): \"%s\"\n",
		       scan, scan, SDL_GetScancodeName(scan));
		count++;
	}
	printf("= %d scancodes.\n", count);

	kmin = 0x7fffffff;
	kmax = empty = count = 0;
	printf("\nNamed SDL keycodes (corresponding to above scancodes):\n");
	for (scan = SDL_SCANCODE_UNKNOWN; scan < SDL_NUM_SCANCODES; scan++) {
		key = SDL_GetKeyFromScancode(scan);
		if (!key) {
			continue;
		}
		name = SDL_GetKeyName(key);
		/* skip empty names, as they *all*
		 * seem to have same 0x4000000 code
		 */
		if (!(name && *name)) {
			if (key < kmin) {
				kmin = key;
			}
			if (key > kmax) {
				kmax = key;
			}
			empty++;
			continue;
		}
		printf("- 0x%08x: \"%s\"\n", key, name);
		count++;
	}
	printf("= %d keycodes", count);
	if (empty) {
		printf(" (+ %d no-name ones in range 0x%08x-0x%08x)",
		       empty, kmin, kmax);
	}
	printf(".\n");

	SDL_Quit();
	return 0;
}
