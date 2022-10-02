/*
 * Hatari - cfgopts.c
 *
 * The functions in this file are used to load and save the ASCII
 * configuration file.
 * Original information text follows:
 */
/*<<---------------[         cfgopts.c        ]------------------------/
/                                                                      /
/  Functional                                                          /
/     Description: Configuration file I/O                              /
/                                                                      /
/  Input         : Configuration file name                             /
/                  Configuration parameters in a structure             /
/                                                                      /
/  Process       : Interpret information by parameter and read or      /
/                  write back to the configuration file.               /
/                                                                      /
/  Output        : updated configuration file or updated structure.    /
/                                                                      /
/  Programmer    : Jeffry J. Brickley                                  /
/                                                                      /
/---------------------------------------------------------------------*/

/*---------------------------------------------------------------------/
/
/  Description:  CfgOpts is based on GETOPTS by Bob Stout.  It will
/                process a configuration file based one words and
/                store it in a structure pointing to physical data
/                area for each storage item.
/  i.e. ???.CFG:
/    Port=1
/    work_space=C:\temp
/    menus=TRUE
/    user=Jeffry Brickley
/  will write to the following structure:
/    struct Config_Tag configs[] = {
/    {"port",       Int_Tag,    &port_number},
/    {"work_space", String_Tag,  &work_space},
/    {"menus",      Bool_Tag, &menu_flag},
/    {"user",       String_Tag,  &User_name},
/    {NULL,         Error_Tag,   NULL}
/    };
/  Note that the structure must always be terminated by a NULL row as
/     was the same with GETOPTS.  This however is slightly more
/     complicated than scanning the command line (but not by much) for
/     data as there can be more variety in words than letters and an
/     number of data items limited only by memory.
/
/  Like the original code from which this was taken, this is released
/  to the Public Domain.  I cannot make any guarantees other than these
/  work for me and I find them useful.  Feel free to pass these on to
/  a friend, but please do not charge him....
/
/---------------------------------------------------------------------*/
const char CfgOpts_fileid[] = "Hatari cfgopts.c";

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "main.h"
#include "cfgopts.h"
#include "str.h"
#include "keymap.h"
#include "log.h"


static int parse_input_config_entry(const struct Config_Tag *ptr)
{
	const char *next;
	int type = ptr->type;

	/* get actual config value */
	next = Str_Trim(strtok(NULL, ""));
	if (next == NULL)
	{
		if (type == String_Tag || type == Key_Tag)
			next = ""; /* field with empty string */
		else
			type = Error_Tag;
	}

	switch (type)      /* check type */
	{
	 case Bool_Tag:
		if (!strcasecmp(next,"FALSE"))
			*(bool *)ptr->buf = false;
		else if (!strcasecmp(next,"TRUE"))
			*(bool *)ptr->buf = true;
		break;

	 case Char_Tag:
		sscanf(next, "%c", (char *)ptr->buf);
		break;

	 case Short_Tag:
		sscanf(next, "%hd", (short *)ptr->buf);
		break;

	 case Int_Tag:
		sscanf(next, "%d", (int *)ptr->buf);
		break;

	 case Long_Tag:
		sscanf(next, "%ld", (long *)ptr->buf);
		break;

	 case Float_Tag:
		sscanf(next, "%g", (float *)ptr->buf);
		break;

	 case Double_Tag:
		sscanf(next, "%lg", (double *)ptr->buf);
		break;

	 case String_Tag:
		strcpy((char *)ptr->buf, next);
		break;

	 case Key_Tag:
		*(int *)ptr->buf =  Keymap_GetKeyFromName(next);
		break;

	 case Error_Tag:
	 default:
		return -1;
	}

	return 0;
}

/**
 * ---------------------------------------------------------------------/
 * /   reads from an input configuration (INI) file.
 * /---------------------------------------------------------------------
 * >>------[   input_config()   ]-------------[ 08-02-95 14:02PM ]------/
 * / return value:
 * /     int                     ; number of records read or -1 on error
 * / parameters:
 * /     char *filename          ; filename of INI style file
 * /     struct Config_Tag configs[]; Configuration structure
 * /     char *header            ; INI header name (i.e. "[TEST]")
 * /-------------------------------------------------------------------<<
 */
int input_config(const char *filename, const struct Config_Tag configs[], const char *header)
{
	const struct Config_Tag *ptr;
	int count = 0, lineno = 0;
	FILE *file;
	char *fptr,*tok;
	char line[1024];

	file = fopen(filename,"r");
	if (file == NULL)
		return -1;                 /* return error designation. */

	if (header != NULL)
	{
		do
		{
			fptr = Str_Trim(fgets(line, sizeof(line), file));  /* get input line */
			if (fptr == NULL)
				break;
		}
		while (strncmp(fptr, header, strlen(header)));
	}

	if ( !feof(file) )
		do
		{
			fptr = Str_Trim(fgets(line, sizeof(line), file));   /* get input line */
			if (fptr == NULL)
				continue;
			lineno++;
			if (fptr[0] == '#')
				continue;                       /* skip comments */
			if (fptr[0] == '[')
				continue;                       /* skip next header */
			tok = Str_Trim(strtok(fptr, "="));      /* get first token */
			if (tok == NULL)
				continue;
			for (ptr = configs; ptr->buf; ++ptr)    /* scan for token */
			{
				if (!strcmp(tok, ptr->code))    /* got a match? */
				{
					if (parse_input_config_entry(ptr) == 0)
						count++;
					else
						Log_Printf(LOG_WARN, "Error in Config file %s on line %d\n",
						       filename, lineno);
				}
			}
		}
		while (fptr != NULL && fptr[0] != '[');

	fclose(file);
	return count;
}


/**
 *  Write out an settings line
 */
static int write_token(FILE *outfile, const struct Config_Tag *ptr)
{
	fprintf(outfile,"%s = ",ptr->code);

	switch (ptr->type)    /* check type */
	{
	 case Bool_Tag:
		fprintf(outfile,"%s\n", *((bool *)(ptr->buf)) ? "TRUE" : "FALSE");
		break;

	 case Char_Tag:
		fprintf(outfile, "%c\n", *((char *)(ptr->buf)));
		break;

	 case Short_Tag:
		fprintf(outfile, "%hd\n", *((short *)(ptr->buf)));
		break;

	 case Int_Tag:
		fprintf(outfile, "%d\n", *((int *)(ptr->buf)));
		break;

	 case Long_Tag:
		fprintf(outfile, "%ld\n", *((long *)(ptr->buf)));
		break;

	 case Float_Tag:
		fprintf(outfile, "%g\n", *((float *)ptr->buf));
		break;

	 case Double_Tag:
		fprintf(outfile, "%g\n", *((double *)ptr->buf));
		break;

	 case String_Tag:
		fprintf(outfile, "%s\n",(char *)ptr->buf);
		break;

	 case Key_Tag:
		fprintf(outfile, "%s\n", Keymap_GetKeyName(*(int *)ptr->buf));
		break;

	 case Error_Tag:
	 default:
		Log_Printf(LOG_WARN, "Internal error in Config structure (contact developers)\n");
		return -1;
	}

	return 0;
}


/**
 * Write given section header and tokens for that
 * Return number of written tokens
 */
static int write_header_tokens(FILE *fp, const struct Config_Tag *ptr, const char *header)
{
	int count = 0;

	if (header != NULL)
	{
		fprintf(fp, "%s\n", header);
	}

	for (; ptr->buf; ++ptr)        /* scan for token */
	{
		if (write_token(fp, ptr) == 0)
			++count;
	}

	fprintf(fp, "\n");

	return count;
}


/**
 * ---------------------------------------------------------------------/
 * /   updates an input configuration (INI) file from a structure.
 * /---------------------------------------------------------------------
 * >>------[   update_config()  ]-------------[ 08-02-95 14:02PM ]------/
 * / return value:
 * /     int                     ; Number of records read & updated
 * / parameters:
 * /     char *filename          ; filename of INI file
 * /     struct Config_Tag configs[]; Configuration structure
 * /     char *header            ; INI header name (i.e. "[TEST]")
 * /-------------------------------------------------------------------<<
 */
int update_config(const char *filename, const struct Config_Tag configs[], const char *header)
{
	const struct Config_Tag *ptr;
	int count=0, retval;
	FILE *cfgfile, *tempfile;
	char *fptr, *tok;
	char line[1024];
	bool bUseTempCfg = false;
	const char *sTempCfgName = "_temp_.cfg";

	cfgfile = fopen(filename, "r");

	/* If the cfg file does not yet exists, we can create it directly: */
	if (cfgfile == NULL)
	{
		cfgfile = fopen(filename, "w");
		if (cfgfile == NULL)
			return -1;                             /* return error designation. */
		count = write_header_tokens(cfgfile, configs, header);
		fclose(cfgfile);
		return count;
	}

	tempfile = tmpfile();                        /* Open a temporary file for output */
 	if (tempfile == NULL)
 	{
		/* tmpfile() failed, let's try a normal open */
		tempfile = fopen(sTempCfgName, "w+");
		bUseTempCfg = true;
	}
	if (tempfile == NULL)
	{
		perror("update_config");
		fclose(cfgfile);
		return -1;                         /* return error designation. */
	}

	if (header != NULL)
	{
		int headerlen = strlen(header);
		do
		{
			fptr = Str_Trim(fgets(line, sizeof(line), cfgfile));  /* get input line */
			if (fptr == NULL)
				break;
			fprintf(tempfile, "%s\n", fptr);
		}
		while (strncmp(fptr, header, headerlen));
	}

	if (feof(cfgfile))
	{
		count += write_header_tokens(tempfile, configs, header);
	}
	else
	{
		char *savedtokenflags = NULL;   /* Array to log the saved tokens */
		int numtokens = 0;              /* Total number of tokens to save */

		/* Find total number of tokens: */
		for (ptr=configs; ptr->buf; ++ptr)
		{
			numtokens += 1;
		}
		if (numtokens)
		{
			savedtokenflags = calloc(numtokens, sizeof(char));
		}

		for(;;)
		{
			fptr = Str_Trim(fgets(line, sizeof(line), cfgfile));  /* get input line */
			/* error or eof? */
			if (fptr == NULL)
				break;
			if (fptr[0] == '#')
			{
				fprintf(tempfile, "%s\n", fptr);
				continue;                                 /* skip comments */
			}
			if (fptr[0] == '[')
			{
				break;
			}

			tok = Str_Trim(strtok(fptr, "="));               /* get first token */
			if (tok != NULL)
			{
				int i = 0;
				for (ptr = configs; ptr->buf; ++ptr, i++) /* scan for token */
				{
					if (!strcmp(tok, ptr->code))           /* got a match? */
					{
						if (write_token(tempfile, ptr) == 0)
						{
							if (savedtokenflags)
								savedtokenflags[i] = true;
							count += 1;
						}
					}
				}
			}
		}

		/* Write remaining (new?) tokens that were not in the configuration file, yet */
		if (count != numtokens && savedtokenflags != NULL)
		{
			int i;
			for (ptr = configs, i = 0; ptr->buf; ++ptr, i++)
			{
				if (!savedtokenflags[i])
				{
					if (write_token(tempfile, ptr) == 0)
					{
						count += 1;
						Log_Printf(LOG_INFO, "Wrote new token %s -> %s \n", header, ptr->code);
					}
				}
			}
		}

		if (savedtokenflags)
		{
			free(savedtokenflags);
			savedtokenflags = NULL;
		}

		if (!feof(cfgfile) && fptr != NULL)
			fprintf(tempfile, "\n%s\n", line);

		for(;;)
		{
			fptr = Str_Trim(fgets(line, sizeof(line), cfgfile));  /* get input line */
			if (fptr == NULL)
				break;
			fprintf(tempfile, "%s\n", fptr);
		}
	}


	/* Re-open the config file for writing: */
	fclose(cfgfile);
	cfgfile = fopen(filename, "wb");
	if (cfgfile == NULL || fseek(tempfile, 0, SEEK_SET) != 0)
	{
		retval = -1;
		goto cleanup;
	}

	/* Now copy the temporary file to the configuration file: */
	retval = count;
	while(!(feof(tempfile) || ferror(cfgfile)))
	{
		size_t copycount;
		copycount = fread(line, sizeof(char), sizeof(line), tempfile);
		if (copycount == 0)
			break;
		if (fwrite(line, sizeof(char), copycount, cfgfile) != copycount)
		{
			retval = -1;
			break;
		}
	}
cleanup:
	if (cfgfile)
	{
		if (ferror(cfgfile))
			perror("update_config");
		fclose(cfgfile);
	}
	if (tempfile)
	{
		/* tmpfile() is removed automatically on close */
		fclose(tempfile);
		if (bUseTempCfg)
			unlink(sTempCfgName);
	}
	return retval;
}

