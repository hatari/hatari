/*
  Hatari - str.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  String functions.
*/
const char Str_rcsid[] = "Hatari $Id: str.c,v 1.3 2008-05-04 17:43:02 thothy Exp $";

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
void Str_ToUpper(char *pString)
{
	while (*pString)
	{
		*pString = toupper(*pString);
		pString++;
	}
}


/**
 * Convert string to lowercase
 */
void Str_ToLower(char *pString)
{
	while (*pString)
	{
		*pString = tolower(*pString);
		pString++;
	}
}


/**
 * truncate string at first unprintable char (e.g. newline)
 */
void Str_Trunc(char *str)
{
	int i=0;
	while (str[i] != '\0')
	{
		if (!isprint((unsigned)str[i]))
			str[i] = '\0';
		i++;
	}
}


/**
 * check if string is valid hex number.
 */
bool Str_IsHex(const char *str)
{
	int i=0;
	while (str[i] != '\0' && str[i] != ' ')
	{
		if (!isxdigit((unsigned)str[i]))
			return false;
		i++;
	}
	return true;
}
