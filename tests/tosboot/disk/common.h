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

/* files which content is checked by tos_tester.py */
#define INPUT_FILE "text"
#define OUTPUT_FILE "test"

/* strings checked by tos_tester.py from midi output */
#define SUCCESS "success"
#define FAILURE "failure"

/* output either success or failure to midi at end of test */
extern void write_midi(void);

/* output given file content to console */
extern void write2console(const char *input);
/* output given file content to printer */
extern void write2printer(const char *input);
/* output given file content to serial port */
extern void write2serial(const char *input);

/* copy input file to output file and then sets it read-only */
extern void copy_file(const char *input, const char *output);

/* try truncating given file which should be read-only
 * (=expected fail), then change it to read/write and
 * retry truncation (=expected success).
 */
extern void truncate_file(const char *readonly);

extern void clear_screen(void);
extern void wait_enter(void);
