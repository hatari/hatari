/*
  Hatari - str.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  String functions.
*/
const char Str_fileid[] = "Hatari str.c";

#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <locale.h>
#include <assert.h>
#include "configuration.h"
#include "str.h"

/* Used only by Str_Filename2TOSname() */
static void Str_HostToAtari(const char *source, char *dest, char replacementChar);


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
		if (!isspace((unsigned char)buffer[i]))
			break;
	}

	if (i > 0 && i < linelen)
	{
		linelen -= i;
		memmove(buffer, buffer + i, linelen);
	}

	for (i = linelen; i > 0; i--)
	{
		if (!isspace((unsigned char)buffer[i-1]))
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
		*str = toupper((unsigned char)*str);
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
		*str = tolower((unsigned char)*str);
		str++;
	}
	return pString;
}

/**
 * Allocate memory for a string and check for out-of memory (and exit the
 * program in that case, since there is likely nothing we can do if we even
 * can not allocate small strings anymore).
 *
 * @len  Length of the string (without the trailing NUL character)
 */
char *Str_Alloc(int len)
{
	char *newstr = malloc(len + 1);

	if (!newstr)
	{
		perror("string allocation failed");
		exit(1);
	}

	newstr[0] = newstr[len] = 0;

	return newstr;
}

/**
 * This function is like strdup, but also checks for out-of memory and exits
 * the program in that case (there is likely nothing we can do if we even can
 * not allocate small strings anymore).
 */
char *Str_Dup(const char *str)
{
	char *newstr;

	if (!str)
		return NULL;

	newstr = strdup(str);
	if (!newstr)
	{
		perror("string duplication failed");
		exit(1);
	}

	return newstr;
}

/**
 * Copy string from pSrc to pDest, taking the destination buffer size
 * into account.
 * This function is similar to strscpy() in the Linux-kernel, it
 * is a replacement for strlcpy() (which cannot be used on untrusted
 * source strings since it tries to find out its length). Our
 * function here returns -E2BIG instead if the string does not
 * fit the destination buffer.
 */
long Str_Copy(char *pDest, const char *pSrc, long nBufLen)
{
	long nCount = 0;

	if (nBufLen == 0)
		return -E2BIG;

	while (nBufLen) {
		char c;

		c = pSrc[nCount];
		pDest[nCount] = c;
		if (!c)
			return nCount;
		nCount++;
		nBufLen--;
	}

	/* Hit buffer length without finding a NUL; force NUL-termination. */
	if (nCount > 0)
		pDest[nCount - 1] = '\0';

	return -E2BIG;
}

/**
 * truncate string at first unprintable char (e.g. newline).
 */
#if 0
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
#endif

/**
 * check if string is valid hex number.
 */
#if 0
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
#endif

/**
 * Convert \e, \n, \t, \\ backslash escapes in given string to
 * corresponding byte values, anything else as left as-is.
 */
void Str_UnEscape(char *s1)
{
	char *s2 = s1;
	for (; *s1; s1++)
	{
		if (*s1 != '\\')
		{
			*s2++ = *s1;
			continue;
		}
		s1++;
		switch(*s1)
		{
		case 'e':
			*s2++ = '\e';
			break;
		case 'n':
			*s2++ = '\n';
			break;
		case 't':
			*s2++ = '\t';
			break;
		case '\\':
			*s2++ = '\\';
			break;
		default:
			s1--;
			*s2++ = '\\';
		}
	}
	assert(s2 < s1);
	*s2 = '\0';
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

	/* convert host string encoding to AtariST character set */
	Str_HostToAtari(source, src, INVALID_CHAR);
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

		/* limit part before extension to 8 chars */
		if (dot - src > 8)
			memmove(src + 8, dot, strlen(dot) + 1);
	}
	else if (len > 8)
		src[8] = '\0';

	strcpy(dst, src);
	free(src);

	/* upcase and replace rest of invalid characters */
	for (tmp = dst; *tmp; tmp++)
	{
		/* invalid characters above 0x80 have already been replaced */
		if (((unsigned char)*tmp) < 32 || *tmp == 127)
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
					break;
				default:
					if (((unsigned char)*tmp) < 128)
					*tmp = toupper((unsigned char)*tmp);
			}
		}
	}
}


/* ---------------------------------------------------------------------- */

/* Implementation of character set conversions */

/* Maps AtariST characters 0x80..0xFF to unicode code points
 * see http://www.unicode.org/Public/MAPPINGS/VENDORS/MISC/ATARIST.TXT
 */
static int mapAtariToUnicode[128] =
{
	0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
	0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
	0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
	0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x00DF, 0x0192,
	0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
	0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
	0x00E3, 0x00F5, 0x00D8, 0x00F8, 0x0153, 0x0152, 0x00C0, 0x00C3,
	0x00D5, 0x00A8, 0x00B4, 0x2020, 0x00B6, 0x00A9, 0x00AE, 0x2122,
	0x0133, 0x0132, 0x05D0, 0x05D1, 0x05D2, 0x05D3, 0x05D4, 0x05D5,
	0x05D6, 0x05D7, 0x05D8, 0x05D9, 0x05DB, 0x05DC, 0x05DE, 0x05E0,
	0x05E1, 0x05E2, 0x05E4, 0x05E6, 0x05E7, 0x05E8, 0x05E9, 0x05EA,
	0x05DF, 0x05DA, 0x05DD, 0x05E3, 0x05E5, 0x00A7, 0x2227, 0x221E,
	0x03B1, 0x03B2, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
	0x03A6, 0x0398, 0x03A9, 0x03B4, 0x222E, 0x03C6, 0x2208, 0x2229,
	0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
	0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x00B3, 0x00AF
};

/* Hashtable which maps unicode code points to AtariST characters 0x80..0xFF.
 * The last 9 bits of the unicode code point provide a hash function
 * without collisions.
 */
static char mapUnicodeToAtari[512];
static bool characterMappingsInitialized = false;

/**
 * This function initializes the mapUnicodeToAtari[] hashtable.
 */
static void initCharacterMappings(void)
{
	int i;
	for (i = 0; i < 128; i++)
	{
		mapUnicodeToAtari[mapAtariToUnicode[i] & 511] = i;
	}
	characterMappingsInitialized = true;

#if defined(WIN32) || defined(USE_LOCALE_CHARSET)
	setlocale(LC_ALL, "");
#endif
}

#if !(defined(WIN32) || defined(USE_LOCALE_CHARSET))
/**
 * Convert a 0-terminated string in the AtariST character set to a 0-terminated
 * UTF-8 encoded string. destLen is the number of available bytes in dest[].
 * A single character of the AtariST charset can consume up to 3 bytes in UTF-8.
 */
static void Str_AtariToUtf8(const char *source, char *dest, int destLen)
{
	int c;
	while (*source)
	{
		c = *source++ & 255;
		if (c >= 128)
		{
			c = mapAtariToUnicode[c & 127];
		}
		if (c < 128 && destLen > 1)
		{
			*dest++ = c;                        /* 0xxxxxxx */
			destLen--;
		}
		else if (c < 2048 && destLen > 2)
		{
			*dest++ = (c >> 6) | 192;           /* 110xxxxx */
			*dest++ = (c & 63) | 128;           /* 10xxxxxx */
			destLen -= 2;
		}
		else if (destLen > 3)
		{
			*dest++ = (c >> 12) | 224;          /* 1110xxxx */
			*dest++ = ((c >> 6) & 63) | 128;    /* 10xxxxxx */
			*dest++ = (c & 63) | 128;           /* 10xxxxxx */
			destLen -= 3;
		}
	}
	*dest = 0;
}

/**
 * Convert a 0-terminated utf-8 encoded string to a 0-terminated string
 * in the AtariST character set.
 * replacementChar is inserted when there is no mapping.
 */
static void Str_Utf8ToAtari(const char *source, char *dest, char replacementChar)
{
	int c, c2, c3, i;
	if (!characterMappingsInitialized) { initCharacterMappings(); }

	while (*source)
	{
		c = *source++ & 255;
		if (c < 128)            /* single-byte utf-8 code (0xxxxxxx) */
		{
			*dest++ = c;
		}
		else if (c < 192)       /* invalid utf-8 encoding (10xxxxxx) */
		{
			*dest++ = replacementChar;
		}
		else                    /* multi-byte utf-8 code */
		{
			if (c < 224)        /* 110xxxxx, 10xxxxxx */
			{
				c2 = *source++;
				c = ((c & 31) << 6) | (c2 & 63);
			}
			else if (c < 240)   /* 1110xxxx, 10xxxxxx, 10xxxxxx */
			{
				c2 = *source++;
				c3 = *source++;
				c = ((c & 15) << 12) | ((c2 & 63) << 6) | (c3 & 63);
			}

			/* find AtariST character code for unicode code point c */
			i = mapUnicodeToAtari[c & 511];
			*dest++ = (mapAtariToUnicode[i] == c ? i + 128 : replacementChar);
		}
	}
	*dest = 0;
}

#else

/**
 * Convert a string from the AtariST character set into the host representation as
 * defined by the current locale. Characters which do not exist in character set
 * of the host as defined by the locale will be replaced by replacementChar.
 */
static void Str_AtariToLocal(const char *source, char *dest, int destLen, char replacementChar)
{
	int c, i;
	if (!characterMappingsInitialized) { initCharacterMappings(); }

	while (*source && destLen > (int)MB_CUR_MAX)
	{
		c = *source++ & 255;
		if (c >= 128)
			c = mapAtariToUnicode[c & 127];
		/* convert the unicode code point c to a character in the current locale */
		i = wctomb(dest, c);
		if (i < 0)
		{
			*dest = replacementChar;
			i = 1;
		}
		dest += i;
		destLen -= i;
	}
	*dest = 0;
}

/**
 * Convert a string from the character set defined by current host locale into the
 * AtariST character set. Characters which do not exist in the AtariST character set
 * will be replaced by replacementChar.
 */
static void Str_LocalToAtari(const char *source, char *dest, char replacementChar)
{
	int i;
	wchar_t c;
	if (!characterMappingsInitialized) { initCharacterMappings(); }

	while (*source)
	{
		/* convert a character from the current locale into an unicode code point */
		i = mbtowc(&c, source, 4);
		if (i < 0)
		{
			c = replacementChar;
			i = 1;
		}
		source += i;
		if (c >= 128)
		{
			/* find AtariST character code for unicode code point c */
			i = mapUnicodeToAtari[c & 511];
			c = (mapAtariToUnicode[i] == c ? i + 128 : replacementChar);
		}
		*dest++ = c;
	}
	*dest = 0;
}
#endif


void Str_AtariToHost(const char *source, char *dest, int destLen, char replacementChar)
{
	if (!ConfigureParams.HardDisk.bFilenameConversion)
	{
		Str_Copy(dest, source, destLen);
		return;
	}
#if defined(WIN32) || defined(USE_LOCALE_CHARSET)
	Str_AtariToLocal(source, dest, destLen, replacementChar);
#else
	Str_AtariToUtf8(source, dest, destLen);
#endif
}

static void Str_HostToAtari(const char *source, char *dest, char replacementChar)
{
	if (!ConfigureParams.HardDisk.bFilenameConversion)
	{
		strcpy(dest, source);
		return;
	}
#if defined(WIN32) || defined(USE_LOCALE_CHARSET)
	Str_LocalToAtari(source, dest, replacementChar);
#else
	Str_Utf8ToAtari(source, dest, replacementChar);
#endif
}


/* This table is needed to convert the UTF-8 representation of paths with
 * diacritical marks from the decomposed form (as returned by OSX) into the
 * precomposed form. Combining unicode characters are 0x0300..0x036F.
 * This table contains only those characters which are part of the AtariST
 * character set.
 */
static int mapDecomposedPrecomposed[] =
{
	'A', 0x0300, 0xC0,
	'A', 0x0301, 0xC1,
	'A', 0x0302, 0xC2,
	'A', 0x0303, 0xC3,
	'A', 0x0308, 0xC4,
	'A', 0x030A, 0xC5,
	'C', 0x0327, 0xC7,
	'E', 0x0300, 0xC8,
	'E', 0x0301, 0xC9,
	'E', 0x0302, 0xCA,
	'E', 0x0308, 0xCB,
	'I', 0x0300, 0xCC,
	'I', 0x0301, 0xCD,
	'I', 0x0302, 0xCE,
	'I', 0x0308, 0xCF,
	'N', 0x0303, 0xD1,
	'O', 0x0300, 0xD2,
	'O', 0x0301, 0xD3,
	'O', 0x0302, 0xD4,
	'O', 0x0303, 0xD5,
	'O', 0x0308, 0xD6,
	'U', 0x0300, 0xD9,
	'U', 0x0301, 0xDA,
	'U', 0x0302, 0xDB,
	'U', 0x0308, 0xDC,
	'Y', 0x0301, 0xDD,
	'a', 0x0300, 0xE0,
	'a', 0x0301, 0xE1,
	'a', 0x0302, 0xE2,
	'a', 0x0303, 0xE3,
	'a', 0x0308, 0xE4,
	'a', 0x030A, 0xE5,
	'c', 0x0327, 0xE7,
	'e', 0x0300, 0xE8,
	'e', 0x0301, 0xE9,
	'e', 0x0302, 0xEA,
	'e', 0x0308, 0xEB,
	'i', 0x0300, 0xEC,
	'i', 0x0301, 0xED,
	'i', 0x0302, 0xEE,
	'i', 0x0308, 0xEF,
	'n', 0x0303, 0xF1,
	'o', 0x0300, 0xF2,
	'o', 0x0301, 0xF3,
	'o', 0x0302, 0xF4,
	'o', 0x0303, 0xF5,
	'o', 0x0308, 0xF6,
	'u', 0x0300, 0xF9,
	'u', 0x0301, 0xFA,
	'u', 0x0302, 0xFB,
	'u', 0x0308, 0xFC,
	'y', 0x0301, 0xFD,
	'y', 0x0308, 0xFF,
	0
};

/**
 * Convert decomposed unicode characters (sequence of a letter
 * and a combining character) in an UTF-8 encoded string into
 * the precomposed UTF-8 encoded form. Only characters which
 * exist in the AtariST character set are converted.
 * This is needed for OSX which returns filesystem paths in the
 * decomposed form (NFD).
 */
void Str_DecomposedToPrecomposedUtf8(const char *source, char *dest)
{
	int c, c1, i;
	while (*source)
	{
		c = *source++ & 255;
		/* do we have a combining character behind the current character */
		if ((source[0] & 0xFC) == 0xCC)	    /* 0x03XX is in UTF-8: 110011xx 10xxxxxx */
		{
			c1 = ((source[0] & 31) << 6) | (source[1] & 63);
			for (i = 0; mapDecomposedPrecomposed[i]; i += 3)
			{
				if (mapDecomposedPrecomposed[i] == c && mapDecomposedPrecomposed[i + 1] == c1)
				{
					c = mapDecomposedPrecomposed[i + 2];  /* precomposed unicode code point */
					*dest++ = 0xC0 | (c >> 6);            /* UTF-8 first byte:  110xxxxx */
					c = 0x80 + (c & 63);                  /* UTF-8 second byte: 10xxxxxx */
					source += 2;
					break;
				}
			}
		}
		*dest++ = c;
	}
	*dest = 0;
}

/* ---------------------------------------------------------------------- */



/**
 * Print an Hex/Ascii dump of Len bytes located at *p
 * Each line consists of Width bytes, printed as an hexa value and as a char
 * (non printable chars are replaced by a '.')
 * The Suffix string is added at the beginning of each line.
 */
void	Str_Dump_Hex_Ascii ( char *p , int Len , int Width , const char *Suffix , FILE *pFile )
{
	int	nb;
	char	buf_hex[ 200*3 ];				/* max for 200 bytes per line */
	char	buf_ascii[ 200 ];
	char	*p_h;
	char	*p_a;
	unsigned char c;
	int	offset;
	

	nb = 0;
	offset = 0;
	p_h = buf_hex;
	p_a = buf_ascii;
	while ( Len > 0 )
	{
		c = *p++;
		sprintf ( p_h , "%2.2x " , c );
		if ( ( c < 0x20 ) || ( c >= 0x7f ) )
			c = '.';
		sprintf ( p_a , "%c" , c );

		p_h += 3;
		p_a += 1;
		
		Len--;
		nb++;
		if ( ( nb % Width == 0 ) || ( Len == 0 ) )
		{
			fprintf ( pFile , "%s%6.6x: %-*s   %-*s\n" , Suffix , offset , Width*3 , buf_hex , Width , buf_ascii );
			offset = nb;
			p_h = buf_hex;
			p_a = buf_ascii;
		}
		
	}
}
