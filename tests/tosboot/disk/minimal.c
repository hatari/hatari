/*
 * Hatari - minimal.c
 *
 * Copyright (C) 2012 by Eero Tamminen
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 * 
 * Atari code for testing some basic Hatari device functionality
 * operations in co-operations with tos_tester.py Hatari test driver.
 * 
 * This is a reduced version from gemdos.c which doesn't modify
 * the disk (image) contents.  Unlike with GEMDOS HD emu, disk
 * image content modifications shouldn't need testing.
 */
#include "common.h"

int main()
{
	clear_screen();
	write2console(INPUT_FILE);
	write2printer(INPUT_FILE);
	write2serial(INPUT_FILE);
	write_midi();
	wait_enter();
	return 0;
}
