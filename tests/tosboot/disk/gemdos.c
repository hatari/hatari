/*
 * Hatari - gemdos.c
 *
 * Copyright (C) 2012 by Eero Tamminen
 *
 * This file is distributed under the GNU Public License, version 2 or
 * at your option any later version. Read the file gpl.txt for details.
 * 
 * Atari code for testing some basic Hatari GEMDOS device and file handle
 * operations in co-operations with tos_tester.py Hatari test driver.
 * 
 * Full test should eventually be something like:
 * - clear screen
 * - open INPUT_FILE for reading
 * - write INPUT_FILE content to OUTPUT_FILE
 * - close OUTPUT_FILE and INPUT_FILE
 * - make OUTPUT_FILE read-only
 * - TODO: execute another instance of tester with option that
 *         tell the second instance to do rest of tests
 * - TODO: dup stdin & stdout handles
 * - open OUTPUT_FILE for reading
 * - TODO: force that to stdin
 * . open printer for writing
 * - TODO: force stdout to printer
 * - TODO: read stdin and write it to stdout
 *   - for now, directly copy OUTPUT_FILE -> printer
 * - TODO: restore stdin & stdout
 * - close OUTPUT_FILE & printer
 * - truncate OUTPUT_FILE -> fail
 * - make OUTPUT_FILE writable
 * - truncate OUTPUT_FILE
 * 
 * As last step, output SUCCESS/FAILURE to MIDI to indicate whether
 * everything (except for the expected failure) succeeded.
 * 
 * TOS tester will additionally verify that the pipeline worked fine
 * by comparing the input and printer output file contents match,
 * i.e. that this pipeline worked fine:
 *	INPUT_FILE --(OUTPUT_FILE)--> printer
 * 
 * And that OUTPUT_FILE is again empty after test.
 */
#include "common.h"

int main()
{
	clear_screen();

	copy_file(INPUT_FILE, OUTPUT_FILE);
	write_printer(OUTPUT_FILE);
	truncate_file(OUTPUT_FILE);

	write_midi();
	wait_key();
	return 0;
}
