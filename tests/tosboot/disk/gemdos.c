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
 */
#ifdef __PUREC__	/* or AHCC */
# include <tos.h>
#else
# include <osbind.h>
#endif
#include <stdio.h>

#define OUTPUT_FILE "test"

static const char msg[] = "done";

static void write_device(const char *path)
{
	long handle;
	handle = Fopen(path, FO_WRITE);
	if (handle >= 0) {
		Fwrite(handle, sizeof(msg)-1, msg);
		Fwrite(handle, 1, "\n");
		Fclose(handle);
	} else {
		printf("ERROR: Fopen(%c: '%s', %d) -> %d\n", 'A'+Dgetdrv(), path, FO_WRITE, handle);
	}
}

static void write_printer(void)
{
	Cconws("Printer...\r\n");
        if (Cprnos()) {
		write_device("PRN:");
	} else {
		Cconws("ERROR: 'PRN:' not ready!\r\n");
	}
}

static void write_disk(void)
{
	Cconws("Disk...\r\n");
	write_device(OUTPUT_FILE);
}

static void write_midi(void)
{
	Cconws("Midi...\r\n");
	/* TODO: output different text if there was errors! */
	Midiws(sizeof(msg)-1, msg);
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
	Cconws("\033E"); /* clear screen */
	write_disk();
	write_printer();
	write_midi();
	wait_key();
	return 0;
}
