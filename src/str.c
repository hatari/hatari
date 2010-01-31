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
 * Check whether first string ends to given second string.
 * Return pointer to the place where second string starts in first, or NULL.
 */
char *Str_EndsWith(char *str1, const char *str2)
{
	int len1, len2;
	
	len1 = strlen(str1);
	len2 = strlen(str2);
	if (len1 < len2)
		return NULL;
	
	str1 = str1 + len1 - len2;
	if (strcmp(str1, str2) == 0)
		return str1;

	return NULL;
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
