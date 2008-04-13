/*
  Hatari - str.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  String functions.
*/
const char Str_rcsid[] = "Hatari $Id: str.c,v 1.1 2008-04-13 22:11:37 thothy Exp $";

#include <ctype.h>

#include "str.h"


/**
 * Remove spaces from beginning and end of a string
 */
char *Str_Trim(char *buffer)
{
	const char SPACE = ' ';
	const char TABULA = '\t';
	int i, linelen;

	if (buffer == NULL)
		return NULL;

	linelen = strlen(buffer);

	for (i = 0; i < linelen; i++)
	{
		if (buffer[i] != SPACE && buffer[i] != TABULA)
			break;
	}

	if (i > 0 && i < linelen)
	{
		linelen -= i;
		memmove(buffer, buffer + i, linelen);
	}

	for (i = linelen; i > 0; i--)
	{
		int j = i - 1;
		if (buffer[j] != SPACE && buffer[j] != TABULA)
			break;
	}

	buffer[i] = '\0';

	return buffer;
}


/**
 * Convert a string to uppercase.
 */
void Str_ToUpper(char *pString)
{
	while (*pString)
	{
		*pString = toupper(*pString);
		pString++;
	}
}
