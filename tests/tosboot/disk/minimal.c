/*
 * Hatari - minimal.c
 *
 * Copyright (C) 2012 by Eero Tamminen
 *
 * This file is distributed under the GNU Public License, version 2 or
 * at your option any later version. Read the file gpl.txt for details.
 * 
 * Atari code for testing some basic Hatari device functionality
 * operations in co-operations with tos_tester.py Hatari test driver.
 * 
 * This is a reduced version from gemdos.c which doesn't modify
 * the disk (image) contents.  Unlike with GEMDOS HD emu, disk
 * image content modifications shouldn't need testing.
 * 
 * What is tested:
 * - clear screen
 * - open INPUT_FILE for reading
 * - open 'PRN:' (printer) for writing
 * - copy INPUT_FILE -> printer
 * - close INPUT_FILE & printer
 * 
 * As last step, output SUCCESS/FAILURE to MIDI to indicate whether
 * everything (except for the expected failure) succeeded.
 * 
 * TOS tester will additionally verify that the pipeline worked fine
 * by comparing the input and printer output file contents match.
 */
#include "common.h"

int main()
{
	clear_screen();
	write_printer(INPUT_FILE);
	write_midi();
	wait_key();
	return 0;
}
