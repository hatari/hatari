/*
  Hatari tool: Magic Shadow Archiver - hmsa.c
 
  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#include <stdio.h>
#include <stdlib.h>
#include <endian.h>
#include <string.h>

#include "hmsa.h"
#include "file.h"
#include "msa.h"


/**
 * Output string to log file
 */
void Log_Printf(int nType, const char *psFormat, ...)
{
	va_list argptr;

	if (nType <= 4)
	{
		va_start(argptr, psFormat);
		vfprintf(stdout, psFormat, argptr);
		va_end(argptr);
	}
}


/**
 * Query user
 */
int DlgAlert_Query(const char *text)
{
	puts(text);
	return TRUE;
}


/**
 * Output string to log file
 */
int main(int argc, char *argv[])
{
	long nImageBytes;
	int retval;
	unsigned char *diskBuffer;
	char *sourceFileName;
	char *targetFileName;

	if(argc!=2 || argv[1][0]=='-')
	{
		printf("\rHatari Magic Shadow (Un-)Archiver version 0.2.0.\n\n"
		       "Usage:  %s FILENAME\n\n"
		       "This program converts a MSA disk image into a ST disk image and vice versa.\n"
		       "It is distributed under the GNU Public License, version 2 or at your\n"
		       "option any later version. Please read the file gpl.txt for details.\n",
		       argv[0]);
		return 0;
	}

	if(strlen(argv[1]) < 4)
	{
		fprintf(stderr,"Invalid file name %s\n", argv[1]);
		return -1;
	}

	sourceFileName = argv[1];
	targetFileName = malloc(strlen(sourceFileName) + 6);

	if (targetFileName == NULL)
	{
		fprintf(stderr,"Not enough memory\n");
		return -1;
	}

	if (strcasecmp(&sourceFileName[strlen(sourceFileName)-3], "msa") == 0)
	{
		/* Convert MSA to ST disk image */

		/* Read the source disk image */
		diskBuffer = MSA_ReadDisk(sourceFileName, &nImageBytes);
		if (!diskBuffer || nImageBytes < 512*8)
		{
			fprintf(stderr,"Could not read MSA disk!\n");
			return -1;
		}

		strcpy(targetFileName, sourceFileName);
		if (targetFileName[strlen(targetFileName)-4] == '.')
			targetFileName[strlen(targetFileName)-4] = 0;  /* Strip MSA extension */
		strcat(targetFileName, ".st");

		printf("Converting %s to %s (%li Bytes)\n", sourceFileName, targetFileName, nImageBytes);

		retval = File_Save(targetFileName, diskBuffer, nImageBytes, FALSE);
	}
	else
	{
		/* Convert ST to MSA disk image */
		nImageBytes = 0;

		/* Read the source disk image: Just load directly into buffer */
		diskBuffer = File_Read(sourceFileName, NULL, &nImageBytes, NULL);
		if (!diskBuffer || nImageBytes < 512*8)
		{
			fprintf(stderr,"Could not read ST disk!\n");
			return -1;
		}

		strcpy(targetFileName, sourceFileName);
		if (targetFileName[strlen(targetFileName)-3] == '.')
			targetFileName[strlen(targetFileName)-3] = 0;  /* Strip ST extension */
		strcat(targetFileName, ".msa");

		printf("Converting %s to %s (%li Bytes)\n", sourceFileName, targetFileName, nImageBytes);

		retval = MSA_WriteDisk(targetFileName, diskBuffer, nImageBytes);
	}

	free(targetFileName);
	free(diskBuffer);

	return retval;
}
