/*
  Hatari - str.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  String functions.
*/
const char Str_fileid[] = "Hatari str.c : " __DATE__ " " __TIME__;

#include <ctype.h>
#include <stdbool.h>
#include <SDL_types.h>
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
 * Convert string to lowercase
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
 * truncate string at first unprintable char (e.g. newline)
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
 * Parse a number, decimal unless prefixed with '$' which signifies hex
 * or prefixed with '%' which signifies binary value.
 * Return true for success and false for error.
 */
bool Str_GetNumber(const char *value, Uint32 *number)
{
	int i;
	const char *str;
	switch (value[0]) {
	case '$':	/* hexadecimal */
		if (sscanf(value+1, "%x", number) != 1)
		{
			fprintf(stderr, "Invalid hexadecimal value '%s'!\n", value);
			return false;
		}
		break;
	case '%':	/* binary */
		*number = 0;
		for (str = value+1, i = 0; *str && i < 32; str++, i++)
		{
			*number <<= 1;
			switch (*str) {
			case '0':
				break;
			case '1':
				*number |= 1;
				break;
			default:
				fprintf(stderr, "Invalid binary value '%s'!\n", value);
				return false;
			}
		}
		if (*str || !i)
		{
			fprintf(stderr, "Invalid number of binary digits in '%s'!\n", value);
			return false;
		}
		break;
	default:	/* decimal */
		if (sscanf(value, "%u", number) != 1)
		{
			fprintf(stderr, "Invalid decimal value '%s'!\n", value);
			return false;
		}
	}
	return true;
}


/**
 * Get a an adress range, eg. "$fa0000-$fa0100"
 * returns:
 *  0 if OK,
 * -1 if not syntaxically a range,
 * -2 if values are invalid,
 * -3 if syntaxically range, but not value-wise.
 */
static int getRange(char *str1, Uint32 *lower, Uint32 *upper)
{
	bool fDash = false;
	char *str2 = str1;
	int ret = 0;

	while (*str2)
	{
		if (*str2 == '-')
		{
			*str2++ = '\0';
			fDash = true;
			break;
		}
		str2++;
	}
	if (!fDash)
		return -1;

	if (!Str_GetNumber(str1, lower))
		ret = -2;
	else if (!Str_GetNumber(str2, upper))
		ret = -2;
	else if (*lower > *upper)
		ret = -3;
	*--str2 = '-';
	return ret;
}


/**
 * Parse an adress range, eg. "$fa0000[-$fa0100]" + show appropriate warnings
 * returns:
 * -1 if invalid address or range,
 *  0 if single address,
 * +1 if a range.
 */
int Str_ParseRange(char *str, Uint32 *lower, Uint32 *upper)
{
	switch (getRange(str, lower, upper))
	{
	case 0:
		return 1;
	case -1:
		/* single address, not a range */
		if (!Str_GetNumber(str, lower))
			return -1;
		return 0;
	case -2:
		fprintf(stderr,"Invalid address values in '%s'!\n", str);
		return -1;
	case -3:
		fprintf(stderr,"Invalid range (%x > %x)!\n", *lower, *upper);
		return -1;
	}
	fprintf(stderr, "INTERNAL ERROR: Unknown getRange() return value.\n");
	return -1;
}
