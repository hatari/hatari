/*
 * Hatari - gemdos.c
 *
 * Copyright (C) 2012 by Eero Tamminen
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
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
 * - output OUTPUT_FILE to CON:
 *   - open OUTPUT_FILE for reading
 *   - TODO: force that to stdin
 *   - open "CON:" (console) for writing
 *   - TODO: force stdout there
 *   - TODO: read stdin and write it to stdout
 *     - for now, directly copy OUTPUT_FILE -> "CON:"
 *   - TODO: restore stdin & stdout
 *    - close OUTPUT_FILE & "CON:"
 * - output same file similarly to PRN: (printer)
 *   - NOTE: this fails for TOS v1.02 - v2.06 when done in a program
 *     auto-started from the AUTO-folder or from DESKTOP.INF
 * - output same file similarly to AUX: (serial)
 * - truncate OUTPUT_FILE -> expected to fail
 * - make OUTPUT_FILE writable
 * - truncate OUTPUT_FILE
 * 
 * As last step, output SUCCESS/FAILURE to MIDI to indicate whether
 * everything (except for the expected failure) succeeded.
 * 
 * TOS tester will additionally verify that the pipeline worked fine
 * by checking that the device output file contents match what's
 * expected i.e. this whole pipeline works as expected:
 *	INPUT_FILE --(OUTPUT_FILE)--> device
 * 
 * And that OUTPUT_FILE is again empty after test.
 */
#include "common.h"

int main()
{
	clear_screen();

	copy_file(INPUT_FILE, OUTPUT_FILE);
	write2console(OUTPUT_FILE);
	write2printer(OUTPUT_FILE);
	write2serial(OUTPUT_FILE);
	truncate_file(OUTPUT_FILE);

	write_midi();
	wait_enter();
	return 0;
}
