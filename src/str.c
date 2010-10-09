/*
  Hatari - str.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  String functions.
*/
const char Str_fileid[] = "Hatari str.c : " __DATE__ " " __TIME__;

#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>
#include <SDL_types.h>
#include "configuration.h"
#include "str.h"


/**
 * Remove whitespace from beginning and end of a string.
 * Returns the trimmed string (string content is moved
 * so that it still starts from the same address)
 */
char *Str_Trim(char *buffer)
{
	int i, linelen;

	if (buffer == NULL)
		return NULL;

	linelen = strlen(buffer);

	for (i = 0; i < linelen; i++)
	{
		if (!isspace(buffer[i]))
			break;
	}

	if (i > 0 && i < linelen)
	{
		linelen -= i;
		memmove(buffer, buffer + i, linelen);
	}

	for (i = linelen; i > 0; i--)
	{
		if (!isspace(buffer[i-1]))
			break;
	}

	buffer[i] = '\0';

	return buffer;
}


/**
 * Convert a string to uppercase in place.
 */
char *Str_ToUpper(char *pString)
{
	char *str = pString;
	while (*str)
	{
		*str = toupper(*str);
		str++;
	}
	return pString;
}


/**
 * Convert string to lowercase in place.
 */
char *Str_ToLower(char *pString)
{
	char *str = pString;
	while (*str)
	{
		*str = tolower(*str);
		str++;
	}
	return pString;
}


/**
 * truncate string at first unprintable char (e.g. newline).
 */
char *Str_Trunc(char *pString)
{
	int i = 0;
	char *str = pString;
	while (str[i] != '\0')
	{
		if (!isprint((unsigned)str[i]))
		{
			str[i] = '\0';
			break;
		}
		i++;
	}
	return pString;
}


/**
 * check if string is valid hex number.
 */
bool Str_IsHex(const char *str)
{
	int i = 0;
	while (str[i] != '\0' && str[i] != ' ')
	{
		if (!isxdigit((unsigned)str[i]))
			return false;
		i++;
	}
	return true;
}


/**
 * Convert potentially too long host filenames to 8.3 TOS filenames
 * by truncating extension and part before it, replacing invalid
 * GEMDOS file name characters with INVALID_CHAR + upcasing the result.
 * 
 * Matching them from the host file system should first try exact
 * case-insensitive match, and then with a pattern that takes into
 * account the conversion done in here.
 */
void Str_Filename2TOSname(const char *source, char *dst)
{
	char *dot, *tmp, *src;
	int len;

	src = strdup(source); /* dup so that it can be modified */
	len = strlen(src);

	/* does filename have an extension? */
	dot = strrchr(src, '.');
	if (dot)
	{
		/* limit extension to 3 chars */
		if (src + len - dot > 3)
			dot[4] = '\0';

		/* if there are extra dots, convert them */
		for (tmp = src; tmp < dot; tmp++)
			if (*tmp == '.')
				*tmp = INVALID_CHAR;
	}

	/* does name now fit to 8 (+3) chars? */
	if (len <= 8 || (dot && len <= 12))
		strcpy(dst, src);
	else
	{
		/* name (still) too long, cut part before extension */
		strncpy(dst, src, 8);
		if (dot)
			strcpy(dst+8, dot);
		else
			dst[8] = '\0';
	}
	free(src);

	/* replace other invalid chars than '.' in filename */
	for (tmp = dst; *tmp; tmp++)
	{
		if (*tmp < 33 || *tmp > 126)
			*tmp = INVALID_CHAR;
		else
		{
			switch (*tmp)
			{
				case '*':
				case '/':
				case ':':
				case '?':
				case '\\':
				case '{':
				case '}':
					*tmp = INVALID_CHAR;
			}
		}
	}
	Str_ToUpper(dst);
}
