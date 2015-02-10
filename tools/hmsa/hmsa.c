/*
  Hatari tool: MSA and ST disk image creator and converter - hmsa.c
 
  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "hmsa.h"
#include "main.h"	/* bool etc. */
#include "createBlankImage.h"
#include "file.h"
#include "msa.h"

/* prototypes for dummy log/alert functions below */
#include "dialog.h"
#include "log.h"

/**
 * Print suitable output prefix based on log level
 */
static void print_prefix(LOGTYPE nType)
{
	const char *sType;
	switch (nType) {
	case LOG_FATAL:
	case LOG_ERROR:
		sType = "ERROR: ";
		break;
	case LOG_WARN:
		sType = "WARNING: ";
		break;
	default:
		return;
	}
	fputs(sType, stdout);
}

/* output newline if it's missing from text */
static void do_newline(const char *text)
{
	if (text[strlen(text)-1] != '\n')
		fputs("\n", stdout);
}

/**
 * Output Hatari log string.
 */
void Log_Printf(LOGTYPE nType, const char *psFormat, ...)
{
	va_list argptr;
	print_prefix(nType);
	va_start(argptr, psFormat);
	vfprintf(stdout, psFormat, argptr);
	va_end(argptr);
}

/**
 * Output Hatari Alert dialog string.
 */
void Log_AlertDlg(LOGTYPE nType, const char *psFormat, ...)
{
	va_list argptr;
	print_prefix(nType);
	va_start(argptr, psFormat);
	vfprintf(stdout, psFormat, argptr);
	va_end(argptr);
	do_newline(psFormat);
}

/**
 * Output Hatari Query dialog string.
 */
int DlgAlert_Query(const char *text)
{
	puts(text);
	do_newline(text);
	return TRUE;
}


/**
 * ../src/file.c requires zip.c, which calls IPF_FileNameIsIPF
 * We create an empty function to replace it, as we don't use IPF here
 * and don't want to compile with all the IPF related files.
 * We do it also for STX.
 */
extern bool IPF_FileNameIsIPF(const char *pszFileName, bool bAllowGZ);		/* function prototype */
bool IPF_FileNameIsIPF(const char *pszFileName, bool bAllowGZ)
{
	return FALSE;
}
extern bool STX_FileNameIsSTX(const char *pszFileName, bool bAllowGZ);		/* function prototype */
bool STX_FileNameIsSTX(const char *pszFileName, bool bAllowGZ)
{
	return FALSE;
}


/**
 * Create MSA or ST image of requested size.
 * return error string or NULL for success.
 */
static const char* create_image(const char *filename, const char *sizeid)
{
	int tracks, sectors, sides;
	tracks = 80;
	
	if (strcasecmp(sizeid, "ss") == 0) {
		sides = 1;
		sectors = 9;
	} else if (strcasecmp(sizeid, "ds") == 0) {
		sides = 2;
		sectors = 9;
	} else if (strcasecmp(sizeid, "hd") == 0) {
		sides = 2;
		sectors = 18;
	} else if (strcasecmp(sizeid, "ed") == 0) {
		sides = 2;
		sectors = 36;
	} else {
		return "ERROR: given disk size isn't one of supported ones!\n";
	}
	if (CreateBlankImage_CreateFile(filename, tracks, sectors, sides)) {
		return NULL;
	}
	return "ERROR: Disk creation failed.\n";
}

/**
 * Print program usage
 */
static void usage(const char *name)
{
		printf("\n\
Hatari MSA (Magic Shadow Archiver) / ST disk image creator & converter v0.3.\n\
\n\
Usage:  %s FILENAME [DISK SIZE]\n\
\n\
If you give only one parameter - the file name of an existing MSA\n\
or ST disk image, this image will be converted to the other disk image\n\
format under a suitable new file name.  Disk image format is recognized\n\
based on the file name extension (.msa or .st).\n\
\n\
If the given file doesn't exist and you give also a disk size\n\
(SS, DS, HD, ED), an empty disk of the given size will be created.\n\
\n\
This software is distributed under the GNU General Public License, version 2\n\
or at your option any later version. Please read the file gpl.txt for details.\n\
\n",
		       name);
}

/**
 * Command line argument parsing and new disk creation
 */
int main(int argc, char *argv[])
{
	bool isMsa;
	int retval;
	long disksize;
	unsigned char *diskbuf;
	const char *srcfile, *srcdot;
	char *dstfile, *dstdot;
	int ImageType;
	int drive;

	if(argc < 2 || argv[1][0] == '-') {
		usage(argv[0]);
		return 0;
	}

	srcfile = argv[1];
	srcdot = strrchr(srcfile, '.');
	if(srcdot == NULL) {
		usage(argv[0]);
		fprintf(stderr, "ERROR: extension missing for file name %s!\n", argv[1]);
		return -1;
	}

	if (strcasecmp(srcdot, ".msa") == 0) {
		isMsa = true;
	} else if (strcasecmp(srcdot, ".st") == 0) {
		isMsa = false;
	} else {
		usage(argv[0]);
		fprintf(stderr, "ERROR: unrecognized file name extension %s (not .msa or .st)!\n", srcdot);
		return -1;
	}

	if (!File_Exists(srcfile)) {
		const char *errstr;
		if (argc != 3) {
			usage(argv[0]);
			fprintf(stderr, "ERROR: disk size for the new disk image not given!\n");
			return -1;
		}
		errstr = create_image(srcfile, argv[2]);
		if (errstr) {
			usage(argv[0]);
			fputs(errstr, stderr);
			return -1;
		}
		return 0;
	}

	dstfile = malloc(strlen(srcfile) + 6);
	if (dstfile == NULL) {
		fprintf(stderr, "ERROR: No memory for new disk name!\n");
		return -1;
	}

	strcpy(dstfile, srcfile);
	dstdot = strrchr(dstfile, '.');
	if (isMsa) {
		/* Convert MSA to ST disk image */
		strcpy(dstdot, ".st");
	} else {
		/* Convert ST to MSA disk image */
		strcpy(dstdot, ".msa");
	}

	if (File_Exists(dstfile)) {
		fprintf(stderr, "ERROR: Destination disk image %s exists already!\n", dstfile);
		free(dstfile);
		return -1;
	}

	drive = 0;                              /* drive is not used for ST/MSA/DIM, set it to 0 */

	if (isMsa) {
		/* Read the source disk image */
		diskbuf = MSA_ReadDisk(drive, srcfile, &disksize, &ImageType);
		if (!diskbuf || disksize < 512*8) {
			fprintf(stderr, "ERROR: could not read MSA disk %s!\n", srcfile);
			retval = -1;
		} else {
			printf("Converting %s to %s (%li Bytes).\n", srcfile, dstfile, disksize);
			retval = File_Save(dstfile, diskbuf, disksize, FALSE);
		}
	} else {
		/* Just read disk image directly into buffer */
		disksize = 0;
		diskbuf = File_Read(srcfile, &disksize, NULL);
		if (!diskbuf || disksize < 512*8) {
			fprintf(stderr, "ERROR: could not read ST disk %s!\n", srcfile);
			retval = -1;
		} else {
			printf("Converting %s to %s (%li Bytes).\n", srcfile, dstfile, disksize);
			retval = MSA_WriteDisk(drive, dstfile, diskbuf, disksize);
		}
	}

	if (diskbuf) {
		free(diskbuf);
	}
	free(dstfile);

	return retval;
}
