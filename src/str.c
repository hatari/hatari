/*
  Hatari - str.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  String functions.
*/
const char Str_rcsid[] = "Hatari $Id: str.c,v 1.4 2008-11-09 20:02:34 eerot Exp $";

#include <ctype.h>
#include <stdbool.h>

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
char *Str_ToUpper(char *str)
{
	int i;
	for (i = 0; str[i]; i++)
		str[i] = toupper(str[i]);
	return str;
}


/**
 * Convert string to lowercase
 */
char *Str_ToLower(char *str)
{
	int i;
	for (i = 0; str[i]; i++)
		str[i] = tolower(str[i]);
	return str;
}


/**
 * truncate string at first unprintable char (e.g. newline)
 */
char *Str_Trunc(char *str)
{
	int i;
	for (i = 0; str[i]; i++)
	{
		if (!isprint((unsigned)str[i]))
		{
			str[i] = '\0';
			break;
		}
	}
	return str;
}


/**
 * check if string is valid hex number.
 */
bool Str_IsHex(const char *str)
{
	int i;
	for (i = 0; str[i] != '\0' && str[i] != ' '; i++)
	{
		if (!isxdigit((unsigned)str[i]))
			return false;
	}
	return true;
}
