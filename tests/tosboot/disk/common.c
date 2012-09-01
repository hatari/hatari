/*
 * Hatari - common.c
 *
 * Copyright (C) 2012 by Eero Tamminen
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
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

/* write given file content to to given device/file with GEMDOS */
static void write_gemdos_device(const char *from, const char *to)
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
		if (count2 <= 0) {
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

void write2console(const char *input)
{
	printf("\r\n%s -> CON: (console)\r\n", input);
        if (Cconos()) {
		write_gemdos_device(input, "CON:");
	} else {
		Cconws("ERROR: 'CON:' not ready!\r\n");
		msg = failure;
	}
}

void write2printer(const char *input)
{
	printf("\r\n%s -> PRN: (printer)\r\n", input);
        if (Cprnos()) {
		write_gemdos_device(input, "PRN:");
	} else {
		Cconws("ERROR: 'PRN:' not ready!\r\n");
		msg = failure;
	}
}

void write2serial(const char *input)
{
	printf("\r\n%s -> AUX: (serial)\r\n", input);
        if (Cauxos()) {
		write_gemdos_device(input, "AUX:");
	} else {
		Cconws("ERROR: 'AUX:' not ready!\r\n");
		msg = failure;
	}
}

void copy_file(const char *input, const char *output)
{
	printf("\r\n%s -> %s\r\n", input, output);
	write_gemdos_device(input, output);
	set_mode(output, FA_READONLY);
}

void truncate_file(const char *readonly)
{
	long handle;
	printf("\r\nTruncate -> %s\r\n", readonly);
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
	printf("\r\nResult -> Midi (%s)\r\n", success);
	Vsync();  /* sync screen output before sending result */
	Midiws(sizeof(success)-2, msg);
	Midiws(0, "\n");
}

void clear_screen(void)
{
	printf("\033EGEMDOS version = 0x%x\r\n", Sversion());
}

static int is_enter(long key)
{
	int scancode = (key >> 16) & 0xff;
	/* return or enter? */
	if (scancode == 28 || scancode == 114) {
		return 1;
	}
	return 0;
}

void wait_enter(void)
{
	/* eat buffered keys */
	while (Cconis()) {
		Cconin();
	}
	Cconws("\r\n<press Enter>\r\n");
	while (!is_enter(Cconin()));
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
