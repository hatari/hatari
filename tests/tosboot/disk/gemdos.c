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
 * - open 'test' for writing -> fail
 * - make 'test' writable
 * - remove 'test'
 * - open 'test' for writing
 * - open 'text' for reading
 * - write 'text' content to 'test'
 * - close 'test' and 'text'
 * - dup stdin & stdout handles
 * - make 'test' read-only
 * - open 'test' for reading
 * - force that to stdin
 * . open printer for writing
 * - force stdout to printer
 * - read stdin and write it to stdout
 * - restore stdin & stdout
 * - close test & printer
 * As last step, output to MIDI 'success' that everything succeeded
 * (except first expected failure) or 'failure' if something failed.
 * 
 * TOS tester will additionally verify that the pipeline worked fine
 * by comparing the input and output file contents:
 *   'text' --'test'--> printer
 */
#ifdef __PUREC__	/* or AHCC */
# include <tos.h>
#else
# include <osbind.h>
#endif
#include <stdio.h>

#define OUTPUT_FILE "test"

/* NOTE: write_midi() assumes messages are of same size! */
static const char success[] = "success";
static const char failure[] = "failure";

static const char *msg;  /* either success or failure */


/* device/file opening with error handling */
static long open_device(const char *path, int attr)
{
	long handle = Fopen(path, attr);
	if (handle < 0) {
		printf("ERROR: Fopen(%c: '%s', %d) -> %d\r\n", 'A'+Dgetdrv(), path, FO_WRITE, handle);
		msg = failure;
	}
	return handle;
}

/* device/file msg writing */
static void write_device(const char *path)
{
	long handle = open_device(path, FO_WRITE);
	if (handle >= 0) {
		int bytes = Fwrite(handle, sizeof(success)-1, success);
		if (bytes != sizeof(success)-1) {
			printf("ERROR: Fwrite(%ld, %d, '%s') -> %d\r\n", handle, sizeof(success)-1, success, bytes);
			msg = failure;
		}
		Fwrite(handle, 1, "\n");
		if (Fclose(handle) != 0) {
			Cconws("ERROR: file close failed\r\n");
			msg = failure;
		}
	}
}

static void write_printer(void)
{
	Cconws("Printer...\r\n");
        if (Cprnos()) {
		write_device("PRN:");
	} else {
		Cconws("ERROR: 'PRN:' not ready!\r\n");
		msg = failure;
	}
}

static void write_disk(void)
{
	Cconws("Disk...\r\n");
	write_device(OUTPUT_FILE);
}

#if TEST_REDIRECTION /* TODO */
static void stdin_to_printer(void)
{
	long handle = open_device("PRN:", FO_WRITE);
	if (handle >= 0) {
		/* TODO: dup & force stdout to printer */
	}
}

static void stdin_from_file(const char *path)
{
	long handle = open_device(path, FO_WRITE);
	if (handle >= 0) {
		/* TODO: dup & force stdin from file */
	}
}

static void stdin_stdout_reset(void)
{
	/* TODO: force stdin & stdout back to normal */
}
#endif

static void write_midi(void)
{
	Cconws("Midi...\r\n");
	Midiws(sizeof(success)-1, msg);
	Midiws(0, "\n");
}

static void wait_key(void)
{
	/* eat buffered keys */
	while (Cconis()) {
		Cconin();
	}
	Cconws("<press a key>\r\n");
	Cconin();
}

int main()
{
	msg = success;   /* anything failing changes this */
	Cconws("\033E"); /* clear screen */

#if TEST_REDIRECTION
	stdin_from_file("text");
	stdout_to_printer();
	stdin_stdout_reset();
#endif
	write_disk();
	write_printer();
	write_midi();

	wait_key();
	return 0;
}
