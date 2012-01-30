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
 * - open 'text' for reading
 * - write 'text' content to 'test'
 * - close 'test' and 'text'
 * - make 'test' read-only
 * - TODO: execute another instance of itself with options
 *         that tell the second instance to do rest of testing
 * - TODO: dup stdin & stdout handles
 * - open 'test' for reading
 * - TODO: force that to stdin
 * . open printer for writing
 * - TODO: force stdout to printer
 * - TODO: read stdin and write it to stdout
 *   - for now, directly copy 'test' -> 'printer'
 * - TODO: restore stdin & stdout
 * - close test & printer
 * - truncate 'test' -> fail
 * - make 'test' writable
 * - truncate 'test'
 * As last step, output to MIDI 'success' that everything succeeded
 * (except the expected failure) or 'failure' if something failed.
 * 
 * TOS tester will additionally verify that the pipeline worked fine
 * by comparing the input and output file contents:
 *   'text' --'test'--> printer
 * And that 'text' is again empty after test.
 */
#ifdef __PUREC__	/* or AHCC */
# include <tos.h>
#else
# include <osbind.h>
#endif
#include <stdio.h>
#include <stdbool.h>

#define INPUT_FILE "text"
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
		printf("ERROR: Fopen(%c: '%s', %d) -> %d\r\n", 'A'+Dgetdrv(), path, attr, handle);
		msg = failure;
	}
	return handle;
}

/* device/file closing with error handling */
static void close_device(long handle)
{
	if (Fclose(handle) != 0) {
		Cconws("ERROR: file close failed\r\n");
		msg = failure;
	}
}

static void print_ioerror(const char *op, int handle, int bufsize, char *buffer, long count)
{
	if (!count) {
		return;
	}
	printf("ERROR: %s(%d, %d, %p) -> %d\r\n", op, handle, bufsize, buffer, count);
	msg = failure;
}

/* writing INPUT_FILE content to given device/file */
static void write_device(const char *from, const char *to)
{
	long handle1, handle2;
	long count1, count2;
	char buffer[64];

	handle1 = open_device(from, FO_READ);
	if (handle1 < 0) {
		return;
	}
	handle2 = open_device(to, FO_WRITE);
	if (handle2 < 0) {
		return;
	}
	while (1) {
		/* copy file contents */
		count1 = Fread(handle1, sizeof(buffer), buffer);
		if (count1 <= 0 || count1 > sizeof(buffer)) {
			print_ioerror("Fread", handle1, sizeof(buffer), buffer, count1);
			break;
		}
		count2 = Fwrite(handle2, count1, buffer);
		if (count1 <= 0) {
			print_ioerror("Fwrite", handle2, count1, buffer, count2);
			break;
		}
	}
	close_device(handle1);
	close_device(handle2);
}

static void write_printer(void)
{
	Cconws(OUTPUT_FILE " -> PRN:\r\n");
        if (Cprnos()) {
		write_device(OUTPUT_FILE, "PRN:");
	} else {
		Cconws("ERROR: 'PRN:' not ready!\r\n");
		msg = failure;
	}
}

static void set_mode(const char *path, int mode)
{
	/* TODO: AHCC misses FA_SET (1) */
	int result = Fattrib(path, 1, mode);
	if (result != mode) {
		printf("ERROR: Fattrib(%s, 1, %d) -> %d\r\n", path, mode, result);
		msg = failure;
	}
}

static void write_file(void)
{
	Cconws(INPUT_FILE " -> " OUTPUT_FILE "\r\n");
	write_device(INPUT_FILE, OUTPUT_FILE);
	set_mode(OUTPUT_FILE, FA_READONLY);
}

static void truncate_file(void)
{
	long handle;
	Cconws("Truncate " OUTPUT_FILE "\r\n");
	handle = Fcreate(OUTPUT_FILE, 0);
	if (handle >= 0) {
		printf("ERROR: truncate succeeded, Fcreate(" OUTPUT_FILE ", 0) -> %d\r\n", handle);
		msg = failure;
		close_device(handle);
	}
	set_mode(OUTPUT_FILE, 0);
	handle = Fcreate(OUTPUT_FILE, 0);
	if (handle >= 0) {
		close_device(handle);
	} else {
		printf("ERROR: truncate failed, Fcreate(" OUTPUT_FILE ", 0) -> %d\r\n", handle);
		msg = failure;
	}
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
	write_file();
	write_printer();
	truncate_file();
	write_midi();

	wait_key();
	return 0;
}
