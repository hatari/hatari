
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "file.h"
#include "filename.h"
#include "dialog.h"
#include "file_archive.h"

/* Fake functions that is required for linking */
bool DlgAlert_Query(const char *text)
{
	return true;
}

uint8_t *Archive_ReadFirstFile(const char *pName, long *pSize, const char * const pExts[])
{
	return NULL;
}
bool	Archive_FileNameIsSupported ( const char *FileName )
{
	return false;
}

static void strcpy_path(char *dst, const char *src, int bufsize)
{
	int i;

	for (i = 0; i < bufsize; i++)
	{
		if (src[i] == '/')
			dst[i] = PATHSEP;
		else
			dst[i] = src[i];

		if (!dst[i])
			break;
	}

	assert(i < bufsize);
}

static int strcmp_path(const char *ref, const char *expected)
{
	char exbuf[256];

	strcpy_path(exbuf, expected, sizeof(exbuf));
	return strcmp(ref, exbuf);
}

static int Test_RemoveSlashes(const char *input, const char *expected)
{
	char str[256];

	printf("Testing FileName_RemoveSlashes(\"%s\")...\t", input);
	strcpy_path(str, input, sizeof(str));
	FileName_RemoveSlashes(str);
	if (strcmp_path(str, expected) != 0)
	{
		puts("FAIL");
		return 1;
	}

	puts("OK");
	return 0;
}

static int Test_AddSlashToEndFileName(const char *input, const char *expected)
{
	char str[256];

	printf("Testing FileName_AddSlashToEnd(\"%s\")...\t", input);
	strcpy_path(str, input, sizeof(str));
	FileName_AddSlashToEnd(str);
	if (strcmp_path(str, expected) != 0)
	{
		puts("FAIL");
		return 1;
	}

	puts("OK");
	return 0;
}

static int Test_DoesFileExtensionMatch(const char *input, const char *ext,
                                       bool bShouldMatch)
{
	printf("Testing FileName_DoesExtensionMatch(\"%s\", \"%s\")...\t",
	       input, ext);
	if (FileName_DoesExtensionMatch(input, ext) != bShouldMatch)
	{
		puts("FAIL");
		return 1;
	}

	puts("OK");
	return 0;
}

static int Test_MakeValidPathName(const char *input, const char *expected)
{
	char str[256];

	printf("Testing File_MakeValidPathName(\"%s\")...\t", input);
	strcpy_path(str, input, sizeof(str));
	File_MakeValidPathName(str);
	if (strcmp_path(str, expected) != 0)
	{
		puts("FAIL");
		return 1;
	}

	puts("OK");
	return 0;
}

int main(int argc, char *argv[])
{
	int ret = 0;

	ret |= Test_RemoveSlashes("some-name/", "some-name");
	ret |= Test_RemoveSlashes("/some-name", "/some-name");
	ret |= Test_RemoveSlashes("/", "/");
	ret |= Test_RemoveSlashes("", "");

	ret |= Test_AddSlashToEndFileName("some-dir-name", "some-dir-name/");
	ret |= Test_AddSlashToEndFileName("some-dir-name/", "some-dir-name/");
	ret |= Test_AddSlashToEndFileName("/", "/");
	ret |= Test_AddSlashToEndFileName("", "");

	ret |= Test_DoesFileExtensionMatch("somedisk.msa", "MSA", true);
	ret |= Test_DoesFileExtensionMatch("somedisk.msa", ".MSA", true);
	ret |= Test_DoesFileExtensionMatch("somedisk.msa", ".MS", false);
	ret |= Test_DoesFileExtensionMatch("somedisk.msa", ".sa", false);
	ret |= Test_DoesFileExtensionMatch("somedisk.msa", "", true);
	ret |= Test_DoesFileExtensionMatch("", ".msa", false);

	ret |= Test_MakeValidPathName("/", "/");
	ret |= Test_MakeValidPathName("/some-nonexisting-file-name", "/");
	ret |= Test_MakeValidPathName("some-nonexisting-file-name/", "/");
	ret |= Test_MakeValidPathName("", "");

	return ret;
}
