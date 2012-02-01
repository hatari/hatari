/*
 * Hatari - common.c
 *
 * Copyright (C) 2012 by Eero Tamminen
 *
 * This file is distributed under the GNU Public License, version 2 or
 * at your option any later version. Read the file gpl.txt for details.
 * 
 * Common functions for gemdos.c and minimal.c testers.
 */
#ifdef __PUREC__	/* or AHCC */
# include <tos.h>
#else
# include <osbind.h>
#endif
#include <stdio.h>
#include "common.h"

/*------- success / failure ----- */

/* anything failing changes 'msg' to failure,
 * write_midi() expects them to be of same lenght.
 */
static const char success[] = SUCCESS;
static const char failure[] = FAILURE;
static const char *msg = success;

/* ------- helper functions ------ */

/* device/file opening with error handling */
static long open_device(const char *path, int attr)
{
	long handle = Fopen(path, attr);
	if (handle < 0) {

		printf("ERROR: Fopen(%c: '%s', %d) -> %ld\r\n", 'A'+Dgetdrv(), path, attr, handle);
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
	printf("ERROR: %s(%d, %d, %p) -> %ld\r\n", op, handle, bufsize, buffer, count);
	msg = failure;
}

/* write given file content to to given device/file */
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
		if (count1 <= 0 || count1 > (long)sizeof(buffer)) {
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

/* set given mode to given file */
static void set_mode(const char *path, int mode)
{
	/* TODO: AHCC misses FA_SET (1) */
	int result = Fattrib(path, 1, mode);
	if (result != mode) {
		printf("ERROR: Fattrib(%s, 1, %d) -> %d\r\n", path, mode, result);
		msg = failure;
	}
}

/* --------- public functions ---------
 * documented in the header
 */

void write_printer(const char *input)
{
	printf("%s -> :PRN:\r\n", input);
        if (Cprnos()) {
		write_device(input, "PRN:");
	} else {
		Cconws("ERROR: 'PRN:' not ready!\r\n");
		msg = failure;
	}
}

void copy_file(const char *input, const char *output)
{
	printf("%s -> %s\r\n", input, output);
	write_device(input, output);
	set_mode(output, FA_READONLY);
}

void truncate_file(const char *readonly)
{
	long handle;
	printf("Truncate -> %s\r\n", readonly);
	handle = Fcreate(readonly, 0);
	if (handle >= 0) {
		printf("ERROR: truncate succeeded, Fcreate(\"%s\", 0) -> %ld\r\n", readonly, handle);
		msg = failure;
		close_device(handle);
	}
	set_mode(readonly, 0);
	handle = Fcreate(readonly, 0);
	if (handle >= 0) {
		close_device(handle);
	} else {
		printf("ERROR: truncate failed, Fcreate(\"%s\", 0) -> %ld\r\n", readonly, handle);
		msg = failure;
	}
}

void write_midi(void)
{
	Cconws("Midi...\r\n");
	Midiws(sizeof(success)-2, msg);
	Midiws(0, "\n");
}

void clear_screen(void)
{
	Cconws("\033E");
}

void wait_key(void)
{
	/* eat buffered keys */
	while (Cconis()) {
		Cconin();
	}
	Cconws("<press a key>\r\n");
	Cconin();
}

/* ------- TODO ------ */

#if TEST_REDIRECTION
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
