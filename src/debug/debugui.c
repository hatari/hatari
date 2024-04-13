/*
  Hatari - debugui.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  debugui.c - this is the code for the mini-debugger. When the pause button is
  pressed, the emulator is (hopefully) halted and this little CLI can be used
  (in the terminal box) for debugging tasks like memory and register dumps.
*/
const char DebugUI_fileid[] = "Hatari debugui.c";

#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <SDL.h>

#include "config.h"

#if HAVE_LIBREADLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "main.h"
#include "change.h"
#include "configuration.h"
#include "file.h"
#include "log.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "screenSnapShot.h"
#include "options.h"
#include "reset.h"
#include "screen.h"
#include "statusbar.h"
#include "str.h"

#include "debug_priv.h"
#include "breakcond.h"
#include "debugcpu.h"
#include "debugdsp.h"
#include "68kDisass.h"
#include "debugInfo.h"
#include "debugui.h"
#include "evaluate.h"
#include "history.h"
#include "profile.h"
#include "symbols.h"
#include "vars.h"

FILE *debugOutput;

static dbgcommand_t *debugCommand;
static int debugCommands;

/* stores last 'e' command result as hex, used for TAB-completion */
static char lastResult[10];

/* array of files from which to read debugger commands after debugger is initialized */
static char **parseFileNames;
static int parseFiles;

/* to which directory to change after (potentially recursed) scripts parsing finishes */
static char *finalDir;


/**
 * Save/Restore snapshot of debugging session variables
 */
void DebugUI_MemorySnapShot_Capture(const char *path, bool bSave)
{
	char *filename;

	filename = Str_Alloc(strlen(path) + strlen(".debug"));
	strcpy(filename, path);
	strcat(filename, ".debug");
	
	if (bSave)
	{
		/* save breakpoints as debugger input file */
		BreakCond_Save(filename);
	}
	else
	{
		/* remove current CPU and DSP breakpoints */
		BreakCond_Command("all", false);
		BreakCond_Command("all", true);

		if (File_Exists(filename))
		{
			/* and parse back the saved breakpoints */
			DebugUI_ParseFile(filename, true, true);
		}
	}
	free(filename);
}


/**
 * Close a log file if open, and set it to default stream.
 */
static void DebugUI_SetLogDefault(void)
{
	if (debugOutput != stderr)
	{
		if (debugOutput)
		{
			File_Close(debugOutput);
			fprintf(stderr, "Debug log closed.\n");
		}
		debugOutput = stderr;
	}
}


/**
 * Open (or close) given log file.
 */
static int DebugUI_SetLogFile(int nArgc, char *psArgs[])
{
	if (debugOutput != stderr)
	{
		fprintf(stderr, "Debug log closed.\n");
		File_Close(debugOutput);
	}
	debugOutput = stderr;

	if (nArgc > 1)
	{
		if ((debugOutput = File_Open(psArgs[1], "w")))
		{
			fprintf(stderr, "Debug log '%s' opened.\n", psArgs[1]);
		}
		else
		{
			fprintf(stderr, "Debug log '%s' opening FAILED.\n", psArgs[1]);
			debugOutput = stderr;
		}
	}
	return DEBUGGER_CMDDONE;
}


/**
 * Helper to print given value in all supported number bases
 */
static void DebugUI_PrintValue(uint32_t value)
{
	bool one, ones;
	int bit;

	fputs("= %", stderr);
	ones = false;
	for (bit = 31; bit >= 0; bit--)
	{
		one = value & (1U << bit);
		if (one || ones)
		{
			fputc(one ? '1':'0', stderr);
			ones = true;
		}
	}
	if (!ones)
		fputc('0', stderr);
	if (value & 0x80000000)
		fprintf(stderr, " (bin), #%u/%d (dec), $%x (hex)\n", value, (int)value, value);
	else
		fprintf(stderr, " (bin), #%u (dec), $%x (hex)\n", value, value);

	sprintf(lastResult, "%x", value);
}


/**
 * Command: Evaluate an expression with CPU reg and symbol parsing.
 */
static int DebugUI_Evaluate(int nArgc, char *psArgs[])
{
	const char *errstr, *expression = (const char *)psArgs[1];
	uint32_t result;
	int offset;

	if (nArgc < 2)
	{
		return DebugUI_PrintCmdHelp(psArgs[0]);
	}

	errstr = Eval_Expression(expression, &result, &offset, false);
	if (errstr)
		fprintf(stderr, "ERROR in the expression:\n'%s'\n%*c-%s\n",
			expression, offset+3, '^', errstr);
	else
		DebugUI_PrintValue(result);
	return DEBUGGER_CMDDONE;
}


/**
 * Check whether given string is a two letter command starting with 'd'
 * or a long command starting with "dsp". String should be trimmed.
 * Return true if given string is command for DSP, false otherwise.
 */
static bool DebugUI_IsForDsp(const char *cmd)
{
	return ((cmd[0] == 'd' && isalpha((unsigned char)cmd[1])
	                       && !isalpha((unsigned char)cmd[2]))
	        || strncmp(cmd, "dsp", 3) == 0);
}

/**
 * Evaluate everything include within single or double quotes ("" or '')
 * and replace them with the result.
 * Caller needs to free the returned string separately.
 * 
 * Return new string with expressions (potentially) expanded, or
 * NULL when there's an error in the expression.
 */
static char *DebugUI_EvaluateExpressions(const char *initial)
{
	int offset, count, diff, inputlen;
	char *end, *start, *input;
	const char *errstr;
	char valuestr[12];
	uint32_t value;
	bool fordsp;

	/* input is split later on, need to save len here */
	input = strdup(initial);
	if (!input)
	{
		perror("ERROR: Input string alloc failed\n");
		return NULL;
	}
	fordsp = DebugUI_IsForDsp(input);
	inputlen = strlen(input);
	start = input;

	while ((count = strcspn(start, "\"'")) && *(start+count))
	{
		start += count;
		end = strchr(start+1, *start);
		if (!end)
		{
			fprintf(stderr, "ERROR: matching '%c' missing from '%s'!\n", *start, start);
			free(input);
			return NULL;
		}
		
		if (end == start+1)
		{
			/* empty expression */
			memmove(start, start+2, strlen(start+2)+1);
			continue;
		}

		*end = '\0';
		errstr = Eval_Expression(start+1, &value, &offset, fordsp);
		if (errstr) {
			*end = *start; /* restore expression mark */
			fprintf(stderr, "Expression ERROR:\n'%s'\n%*c-%s\n",
				input, (int)(start-input)+offset+3, '^', errstr);
			free(input);
			return NULL;
		}
		end++;

		count = sprintf(valuestr, "$%x", value);
		fprintf(stderr, "- '%s' -> %s\n", start+1, valuestr);

		diff = end-start;
		if (count < diff)
		{
			memcpy(start, valuestr, count);
			start += count;
			memmove(start, end, strlen(end) + 1);
		} else {
			/* value won't fit to expression, expand string */
			char *tmp;
			inputlen += count-diff+1;
			tmp = malloc(inputlen+1);
			if (!tmp)
			{
				perror("ERROR: Input string alloc failed\n");
				free(input);
				return NULL;
			}

			memcpy(tmp, input, start-input);
			start = tmp+(start-input);
			memcpy(start, valuestr, count);
			start += count;
			memcpy(start, end, strlen(end) + 1);

			free(input);
			input = tmp;
		}
	}
	/* no (more) expressions to evaluate */
	return input;
}


/**
 * Command: Store and restore emulation state
 */
static int DebugUI_DoMemorySnap(int argc, char *argv[])
{
	const char *file;

	if (argc > 1)
		file = argv[1];
	else
		file = ConfigureParams.Memory.szMemoryCaptureFileName;

	/* [NP] TODO : we need to restart emulation to complete restore, */
	/* it can't be done immediately. Try to call m68k_go() and go back automatically to debugger ? */
	if (strcmp(argv[0], "stateload") == 0)
		MemorySnapShot_Restore(file, true);
	else
		MemorySnapShot_Capture_Immediate(file, true);

	return DEBUGGER_CMDDONE;
}


/**
 * Command: Set command line and debugger options
 */
static int DebugUI_SetOptions(int argc, char *argv[])
{
	CNF_PARAMS current;
	static const struct {
		const char name[4];
		int base;
	} bases[] = {
		{ "bin", 2 },
		{ "dec", 10 },
		{ "hex", 16 }
	};
	const char *arg;
	int i;
	
	if (argc < 2)
	{
		return DebugUI_PrintCmdHelp(argv[0]);
	}
	arg = argv[1];

	for (i = 0; i < ARRAY_SIZE(bases); i++)
	{
		if (strcasecmp(bases[i].name, arg) == 0)
		{
			if (ConfigureParams.Debugger.nNumberBase != bases[i].base)
			{
				fprintf(stderr, "Switched default number base from %d to %d-based (%s) values.\n",
					ConfigureParams.Debugger.nNumberBase,
					bases[i].base, bases[i].name);
				ConfigureParams.Debugger.nNumberBase = bases[i].base;
			} else {
				fprintf(stderr, "Already in '%s' mode.\n", bases[i].name);
			}
			return DEBUGGER_CMDDONE;
		}
	}

	/* get configuration changes */
	current = ConfigureParams;

	/* Parse and apply options */
	if (Opt_ParseParameters(argc, (const char * const *)argv))
	{
		ConfigureParams.Screen.bFullScreen = false;
		Change_CopyChangedParamsToConfiguration(&current, &ConfigureParams, false);
	}
	else
	{
		ConfigureParams = current;
	}

	return DEBUGGER_CMDDONE;
}


/**
 * Command: Screenshot
 */
static int DebugUI_Screenshot(int argc, char *argv[])
{
	if (argc == 2)
		ScreenSnapShot_SaveToFile(argv[1]);
	else
		return DebugUI_PrintCmdHelp(argv[0]);
	return DEBUGGER_CMDDONE;
}

/**
 * Command: Set tracing
 */
static int DebugUI_SetTracing(int argc, char *argv[])
{
	const char *errstr;
	if (argc != 2)
	{
		return DebugUI_PrintCmdHelp(argv[0]);
	}
	errstr = Log_SetTraceOptions(argv[1]);
	if (errstr && errstr[0])
		fprintf(stderr, "ERROR: %s\n", errstr);

	return DEBUGGER_CMDDONE;
}

/**
 * Command: Change Hatari work directory
 */
static int DebugUI_ChangeDir(int argc, char *argv[])
{
	if (argc == 3 && strcmp("-f", argv[2]) == 0)
	{
		if (finalDir)
			free(finalDir);
		finalDir = strdup(argv[1]);
		fprintf(stderr, "Will switch to '%s' dir after all scripts have finished.\n", argv[1]);
		return DEBUGGER_CMDDONE;
	}
	if (argc == 2)
	{
		if (chdir(argv[1]) == 0)
			return DEBUGGER_CMDDONE;
		perror("ERROR");
	}
	return DebugUI_PrintCmdHelp(argv[0]);
}

/**
 * Command: Print strings to debug log output, with escape handling.
 */
static int DebugUI_Echo(int argc, char *argv[])
{
	if (argc < 2)
		return DebugUI_PrintCmdHelp(argv[0]);
	for (int i = 1; i < argc; i++)
	{
		Str_UnEscape(argv[i]);
		fputs(argv[i], debugOutput);
	}
	return DEBUGGER_CMDDONE;
}

/**
 * Command: Rename file
 */
static int DebugUI_Rename(int argc, char *argv[])
{
	if (argc == 3)
	{
		if (rename(argv[1], argv[2]) == 0)
			return DEBUGGER_CMDDONE;
		perror("ERROR");
	}
	return DebugUI_PrintCmdHelp(argv[0]);
}


/**
 * Command: Reset emulation
 */
static char *DebugUI_MatchReset(const char *text, int state)
{
	static const char* types[] = {	"cold", "hard", "soft", "warm" };
	return DebugUI_MatchHelper(types, ARRAY_SIZE(types), text, state);
}
static int DebugUI_Reset(int argc, char *argv[])
{
	if (argc != 2)
		return DebugUI_PrintCmdHelp(argv[0]);

	if (strcmp(argv[1], "soft") == 0 || strcmp(argv[1], "warm") == 0)
		Reset_Warm();
	else if (strcmp(argv[1], "cold") == 0 || strcmp(argv[1], "hard") == 0)
		Reset_Cold();
	else
		return DebugUI_PrintCmdHelp(argv[0]);
	return DEBUGGER_END;
}


/**
 * Command: Read debugger commands from a file
 */
static int DebugUI_CommandsFromFile(int argc, char *argv[])
{
	if (argc == 2)
		DebugUI_ParseFile(argv[1], true, true);
	else
		DebugUI_PrintCmdHelp(argv[0]);
	return DEBUGGER_CMDDONE;
}


/**
 * Command: Quit emulator
 */
static int DebugUI_QuitEmu(int nArgc, char *psArgv[])
{
	int exitval;

	if (nArgc > 2)
		return DebugUI_PrintCmdHelp(psArgv[0]);

	if (nArgc == 2)
		exitval = atoi(psArgv[1]);
	else
		exitval = 0;

	ConfigureParams.Log.bConfirmQuit = false;
	Main_RequestQuit(exitval);
	return DEBUGGER_END;
}


/**
 * Print help text for one command
 */
int DebugUI_PrintCmdHelp(const char *psCmd)
{
	dbgcommand_t *cmd;
	int i;

	/* Search the command ... */
	for (cmd = debugCommand, i = 0; i < debugCommands; i++, cmd++)
	{
		if (!debugCommand[i].pFunction)
			continue;
		if ((*(cmd->sShortName) && !strcmp(psCmd, cmd->sShortName))
		    || !strcmp(psCmd, cmd->sLongName))
		{
			bool bShort = *(cmd->sShortName);
			/* ... and print help text */
			if (bShort)
			{
				fprintf(stderr, "'%s' or '%s' - %s\n",
					cmd->sLongName,
					cmd->sShortName,
					cmd->sShortDesc);
			}
			else
			{
				fprintf(stderr, "'%s' - %s\n",
					cmd->sLongName,
					cmd->sShortDesc);
			}
			fprintf(stderr, "Usage:  %s %s\n",
				bShort ? cmd->sShortName : cmd->sLongName,
				cmd->sUsage);
			return DEBUGGER_CMDDONE;
		}
	}

	fprintf(stderr, "Unknown command '%s'\n", psCmd);
	return DEBUGGER_CMDDONE;
}


/**
 * Command: Print debugger help screen.
 */
static int DebugUI_Help(int nArgc, char *psArgs[])
{
	int i;

	if (nArgc > 1)
	{
		return DebugUI_PrintCmdHelp(psArgs[1]);
	}

	for (i = 0; i < debugCommands; i++)
	{
		if (!debugCommand[i].pFunction)
		{
			fprintf(stderr, "\n%s:\n", debugCommand[i].sLongName);
			continue;
		}
		fprintf(stderr, " %12s (%2s) : %s\n", debugCommand[i].sLongName,
			debugCommand[i].sShortName, debugCommand[i].sShortDesc);
	}

	fprintf(stderr,
		"\n"
		"If value is prefixed with '$', it's a hexadecimal, if with '#', it's\n"
		"a normal decimal, if with '%%', it's a binary decimal. Prefix can\n"
		"be skipped for numbers in the default number base (currently %d).\n"
		"\n"
		"Any expression given in quotes (within \"\"), will be evaluated\n"
		"before given to the debugger command.  Any register and symbol\n"
		"names in the expression are replaced by their values.\n"
		"\n"
		"Note that address ranges like '$fc0000-$fc0100' should have no\n"
		"spaces between the range numbers.\n"
		"\n"
		"'help <command>' gives more help.\n", ConfigureParams.Debugger.nNumberBase);
	return DEBUGGER_CMDDONE;
}


/**
 * Parse debug command and execute it.
 */
static int DebugUI_ParseCommand(const char *input_orig)
{
	char *psArgs[64], *input;
	const char *delim;
	static char sLastCmd[80] = { '\0' };
	int nArgc, cmd = -1;
	int i, retval;

	input = strdup(input_orig);
	psArgs[0] = strtok(input, " \t");

	if (psArgs[0] == NULL)
	{
		if (strlen(sLastCmd) > 0)
			psArgs[0] = sLastCmd;
		else
		{
			free(input);
			return DEBUGGER_CMDDONE;
		}
	}

	/* Search the command ... */
	for (i = 0; i < debugCommands; i++)
	{
		if (!debugCommand[i].pFunction)
			continue;
		if (!strcmp(psArgs[0], debugCommand[i].sShortName) ||
		    !strcmp(psArgs[0], debugCommand[i].sLongName))
		{
			cmd = i;
			break;
		}
	}
	if (cmd == -1)
	{
		fprintf(stderr, "Command '%s' not found.\n"
			"Use 'help' to view a list of available commands.\n",
			psArgs[0]);
		free(input);
		return DEBUGGER_CMDDONE;
	}

	if (debugCommand[cmd].bNoParsing)
		delim = "";
	else
		delim = " \t";

	/* Separate arguments and put the pointers into psArgs */
	for (nArgc = 1; nArgc < ARRAY_SIZE(psArgs); nArgc++)
	{
		psArgs[nArgc] = strtok(NULL, delim);
		if (psArgs[nArgc] == NULL)
			break;
	}
	if (nArgc >= ARRAY_SIZE(psArgs))
	{
		fprintf(stderr, "Error: too many arguments (currently up to %d supported)\n",
			ARRAY_SIZE(psArgs));
		retval = DEBUGGER_CMDCONT;
	}
	else
	{
		/* ... and execute the function */
		retval = debugCommand[i].pFunction(nArgc, psArgs);
	}
	/* Save commando string if it can be repeated */
	if (retval == DEBUGGER_CMDCONT || retval == DEBUGGER_ENDCONT)
	{
		if (psArgs[0] != sLastCmd)
			Str_Copy(sLastCmd, psArgs[0], sizeof(sLastCmd));
		if (retval == DEBUGGER_ENDCONT)
			retval = DEBUGGER_END;
	}
	else
		sLastCmd[0] = '\0';
	free(input);
	return retval;
}


/* See "info:readline" e.g. in Konqueror for readline usage. */

/**
 * Generic readline match callback helper.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char *DebugUI_MatchHelper(const char **strings, int items, const char *text, int state)
{
	static int i, len;
	
	if (!state)
	{
		/* first match */
		len = strlen(text);
		i = 0;
	}
	/* next match */
	while (i < items) {
		if (strncasecmp(strings[i++], text, len) == 0)
			return (strdup(strings[i-1]));
	}
	return NULL;
}

/**
 * Readline match callback for long command name completion.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
static char *DebugUI_MatchCommand(const char *text, int state)
{
	static int i, len;
	const char *name;
	
	if (!state)
	{
		/* first match */
		len = strlen(text);
		i = 0;
	}
	/* next match */
	while (i < debugCommands)
	{
		name = debugCommand[i].sLongName;
		if (debugCommand[i++].pFunction &&
		    strncmp(name, text, len) == 0)
			return (strdup(name));
	}
	return NULL;
}


#if HAVE_LIBREADLINE
/**
 * Readline match callback returning last result.
 */
static char *DebugUI_MatchLast(const char *text, int state)
{
	if (state)
		return NULL;
	return strdup(lastResult);
}

/**
 * Readline completion callback. Returns matches.
 */
static char **DebugUI_Completion(const char *text, int a, int b)
{
	int i, cmd, quotes, end, start = 0;
	char *str, buf[32];
	size_t len;

	/* check where's the first word (ignore white space) */
	while (start < rl_point && isspace((unsigned char)rl_line_buffer[start]))
		start++;
	end = start;
	while (end < rl_point && !isspace((unsigned char)rl_line_buffer[end]))
		end++;

	if (end >= rl_point)
		/* first word on line */
		return rl_completion_matches(text, DebugUI_MatchCommand);
	
	/* complete '$' with last result? */
	if (lastResult[0] && rl_line_buffer[rl_point-1] == '$')
		return rl_completion_matches(text, DebugUI_MatchLast);

	/* check which command args are to be completed */
	len = end - start;
	if (len >= sizeof(buf))
		len = sizeof(buf)-1;
	memcpy(buf, &(rl_line_buffer[start]), len);
	buf[len] = '\0';

	/* expression completion needed (= open quote)? */
	str = strchr(&(rl_line_buffer[end]), '"');
	quotes = 0;
	while (str)
	{
		quotes++;
		str = strchr(str+1, '"');
	}
	if (quotes & 1)
	{
		if (DebugUI_IsForDsp(buf))
			return rl_completion_matches(text, Symbols_MatchDspAddress);
		return rl_completion_matches(text, Symbols_MatchCpuAddress);
	}

	/* do command argument completion */
	cmd = -1;
	for (i = 0; i < debugCommands; i++)
	{
		if (!debugCommand[i].pFunction)
			continue;
		if (!strcmp(buf, debugCommand[i].sShortName) ||
		    !strcmp(buf, debugCommand[i].sLongName))
		{
			cmd = i;
			break;
		}
	}
	if (cmd < 0)
	{
		rl_attempted_completion_over = true;
		return NULL;
	}
	if (debugCommand[cmd].pMatch)
		return rl_completion_matches(text, debugCommand[cmd].pMatch);
	else
		return rl_completion_matches(text, rl_filename_completion_function);
}

/**
 * Add non-repeated command to readline history
 * and free the given string
 */
static void DebugUI_FreeCommand(char *input)
{
	if (input && *input)
	{
		HIST_ENTRY *hist = history_get(history_length);
		/* don't store duplicate successive entries */
		if (!hist || !hist->line || strcmp(hist->line, input) != 0)
		{
			add_history(input);
		}
		free(input);
	}
}

/**
 * Read a command line from the keyboard and return a pointer to the string.
 * Only string returned by this function can be given for it as argument!
 * The string will be stored into command history buffer.
 * @return	Pointer to the string which should be given back to this
 *              function or DebugUI_FreeCommand() for re-use/history.
 *              Returns NULL when error occurred.
 */
static char *DebugUI_GetCommand(char *input)
{
	/* We need this indirection for libedit's rl_readline_name which is
	 * not declared as "const char *" (i.e. this is necessary for macOS) */
	static char hatari_readline_name[] = "Hatari";

	/* Allow conditional parsing of the ~/.inputrc file. */
	rl_readline_name = hatari_readline_name;
	
	/* Tell the completer that we want a crack first. */
	rl_attempted_completion_function = DebugUI_Completion;
	DebugUI_FreeCommand(input);
	return Str_Trim(readline("> "));
}

/**
 * Get readlines idea of the terminal size
 */
static void DebugUI_GetScreenSize(int *rows, int *cols)
{
	rl_get_screen_size(rows, cols);
}

#else /* !HAVE_LIBREADLINE */

/**
 * Free Command input string
 */
static void DebugUI_FreeCommand(char *input)
{
	free(input);
}

/**
 * Get number of lines/columns for terminal output
 */
static void DebugUI_GetScreenSize(int *rows, int *cols)
{
	const char *p;

	*rows = 24;
	*cols = 80;
	if ((p = getenv("LINES")) != NULL)
		*rows = (int)strtol(p, NULL, 0);
	if ((p = getenv("COLUMNS")) != NULL)
		*cols = (int)strtol(p, NULL, 0);
}

/**
 * Read a command line from the keyboard and return a pointer to the string.
 * Only string returned by this function can be given for it as argument!
 * @return	Pointer to the string which should be given back to this
 *              function or DebugUI_FreeCommand() for re-use/freeing.
 *              Returns NULL when error occurred.
 */
static char *DebugUI_GetCommand(char *input)
{
	fprintf(stderr, "> ");
	if (!input)
	{
		input = malloc(256);
		if (!input)
			return NULL;
	}
	input[0] = '\0';
	if (fgets(input, 256, stdin) == NULL)
	{
		free(input);
		return NULL;
	}
	return Str_Trim(input);
}

#endif /* !HAVE_LIBREADLINE */

/**
 * How many lines to "page" when user invokes calling command.
 *
 * If config value is >=0, use that.  If it's negative, get number of lines
 * from screensize. If even that's not defined, fall back to default value.
 *
 * @return Number of lines to output at the time.
 */
int DebugUI_GetPageLines(int config, int defvalue)
{
	int rows, cols;

	if (config >= 0) {
		return config;
	}
	DebugUI_GetScreenSize(&rows, &cols);
	/* leave 1 line for pager prompt */
	if (--rows > 0) {
		return rows;
	}
	return defvalue;
}


static const dbgcommand_t uicommand[] =
{
	{ NULL, NULL, "Generic commands", NULL, NULL, NULL, false },
	/* NULL as match function will complete file names */
	{ DebugUI_ChangeDir, NULL,
	  "cd", "",
	  "change directory",
	  "<directory> [-f]\n"
	  "\tChange Hatari work directory. With '-f', directory is\n"
	  "\tchanged only after all script files have been parsed.",
	  false },
	{ DebugUI_Echo, NULL,
	  "echo", "",
	  "output given string(s)",
	  "<strings>\n",
	  false },
	{ DebugUI_Evaluate, Vars_MatchCpuVariable,
	  "evaluate", "e",
	  "evaluate an expression",
	  "<expression>\n"
	  "\tEvaluate an expression and show the result.  Expression can\n"
	  "\tinclude CPU register & symbol and Hatari variable names.\n"
	  "\tThose are replaced by their values. Supported operators in\n"
	  "\texpressions are, in the descending order of precedence:\n"
	  "\t\t(), +, -, ~, *, /, +, -, >>, <<, ^, &, |\n"
	  "\tParenthesis fetch long value from the given address,\n"
	  "\tunless .<width> suffix is given. Prefixes can be\n"
	  "\tused only in start of line or parenthesis.\n"
	  "\tFor example:\n"
	  "\t\t~%101 & $f0f0f ^ (d0 + 0x21).w\n"
	  "\tResult value is shown as binary, decimal and hexadecimal.\n"
	  "\tAfter this, '$' will TAB-complete to last result value.",
	  true },
	{ DebugUI_Help, DebugUI_MatchCommand,
	  "help", "h",
	  "print help",
	  "[command]\n"
	  "\tPrint help text for available commands.",
	  false },
	{ History_Parse, History_Match,
	  "history", "hi",
	  "show last CPU and/or DSP PC values + instructions",
	  "cpu|dsp|on|off|<count> [limit]|save <file>\n"
	  "\t'cpu' and 'dsp' enable program counter history tracking for given\n"
	  "\tprocessor, 'on' tracks them both, 'off' will disable history.\n"
	  "\tOptional 'limit' will set how many past addresses are tracked.\n"
	  "\tGiving just count will show (at max) given number of last saved PC\n"
	  "\tvalues and instructions currently at corresponding RAM addresses.",
	  false },
	{ DebugInfo_Command, DebugInfo_MatchInfo,
	  "info", "i",
	  "show machine/OS information",
	  "[subject [arg]]\n"
	  "\tPrint information on requested subject or list them if\n"
	  "\tno subject given.",
	  false },
	{ DebugInfo_Command, DebugInfo_MatchLock,
	  "lock", "",
	  "specify information to show on entering the debugger",
	  "[subject [args]]\n"
	  "\tLock what information should be shown every time debugger\n"
	  "\tis entered, or list available options if no subject's given.",
	  false },
	{ DebugUI_SetLogFile, NULL,
	  "logfile", "f",
	  "set (memdump/disasm/registers) log file",
	  "[filename]\n"
	  "\tOpen log file, no argument closes the log file. Output of\n"
	  "\tregister & memory dumps and disassembly will be written to it.",
	  false },
	{ DebugUI_CommandsFromFile, NULL,
	  "parse", "p",
	  "get debugger commands from file",
	  "[filename]\n"
	  "\tRead debugger commands from given file and do them.\n"
	  "\tCurrent directory is script directory during this.\n"
	  "\tTo specify directory to be used also for breakpoint\n"
	  "\tscripts execution, use '-f' option for 'cd' command.",
	  false },
	{ DebugUI_Rename, NULL,
	  "rename", "",
	  "rename given file",
	  "<old> <new>\n"
	  "\tRename file with <old> name to <new>.",
	  false },
	{ DebugUI_Reset, DebugUI_MatchReset,
	  "reset", "",
	  "reset emulation",
	  "<soft|hard>\n",
	  false },
	{ DebugUI_Screenshot, NULL,
	  "screenshot", "",
	  "save screenshot to given file",
	  "<filename>\n",
	  false },
	{ DebugUI_SetOptions, Opt_MatchOption,
	  "setopt", "o",
	  "set Hatari command line and debugger options",
	  "[bin|dec|hex|<command line options>]\n"
	  "\tSpecial 'bin', 'dec' and 'hex' arguments change the default\n"
	  "\tnumber base used in debugger.  <TAB> lists available command\n"
	  "\tline options, 'setopt --help' their descriptions.",
	  false },
	{ DebugUI_DoMemorySnap, NULL,
	  "stateload", "",
	  "restore emulation state",
	  "[filename]\n"
	  "\tRestore emulation snapshot from default or given file",
	  false },
	{ DebugUI_DoMemorySnap, NULL,
	  "statesave", "",
	  "save emulation state",
	  "[filename]\n"
	  "\tSave emulation snapshot to default or given file",
	  false },
	{ DebugUI_SetTracing, Log_MatchTrace,
	  "trace", "t",
	  "select Hatari tracing settings",
	  "[set1,set2...]\n"
	  "\tSelect Hatari tracing settings. 'help' shows all the available\n"
	  "\tsettings.  For example, to enable CPU disassembly and VBL\n"
	  "\ttracing, use:\n\t\ttrace cpu_disasm,video_hbl",
	  false },
	{ Vars_List, NULL,
	  "variables", "v",
	  "List builtin symbols / variables",
	  "\n"
	  "\tList Hatari debugger builtin symbols / variables and their values.\n"
	  "\tThey're accepted by breakpoints and evaluate command.",
	  false },
	{ DebugUI_QuitEmu, NULL,
	  "quit", "q",
	  "quit emulator",
	  "[exit value]\n"
	  "\tLeave debugger and quit emulator with given exit value.",
	  false }
};


/**
 * Debugger user interface initialization.
 */
void DebugUI_Init(void)
{
	const dbgcommand_t *cpucmd, *dspcmd;
	int cpucmds, dspcmds;

	Log_ResetMsgRepeat();

	/* already initialized? */
	if (debugCommands)
		return;

	if (!debugOutput)
		DebugUI_SetLogDefault();

	/* if you want disassembly or memdumping to start/continue from
	 * specific address, you can set them in these functions.
	 */
	dspcmds = DebugDsp_Init(&dspcmd);
	cpucmds = DebugCpu_Init(&cpucmd);

	/* on first time copy the command structures to a single table */
	debugCommands = ARRAY_SIZE(uicommand);
	debugCommand = malloc(sizeof(dbgcommand_t) * (dspcmds + cpucmds + debugCommands));
	assert(debugCommand);
	
	memcpy(debugCommand, uicommand, sizeof(dbgcommand_t) * debugCommands);
	memcpy(&debugCommand[debugCommands], cpucmd, sizeof(dbgcommand_t) * cpucmds);
	debugCommands += cpucmds;
	memcpy(&debugCommand[debugCommands], dspcmd, sizeof(dbgcommand_t) * dspcmds);
	debugCommands += dspcmds;


	if (parseFiles)
	{
		int i;
		for (i = 0; i < parseFiles; i++)
		{
			DebugUI_ParseFile(parseFileNames[i], true, true);
			free(parseFileNames[i]);
		}
		free(parseFileNames);
		parseFileNames = NULL;
		parseFiles = 0;
	}
}

/**
 * Debugger user interface de-initialization / free.
 */
void DebugUI_UnInit(void)
{
	Profile_CpuFree();
	Profile_DspFree();
	Symbols_FreeAll();
	free(debugCommand);
	debugCommands = 0;
}


/**
 * Add debugger command files during Hatari startup before things
 * needed by the debugger are initialized so that it can be parsed
 * when debugger itself gets initialized.
 * Return true if file exists and it could be added, false otherwise.
 */
bool DebugUI_AddParseFile(const char *path)
{
	if (!File_Exists(path))
	{
		fprintf(stderr, "ERROR: debugger input file '%s' missing.\n", path);
		return false;
	}
	parseFileNames = realloc(parseFileNames, (parseFiles+1)*sizeof(char*));
	if (!parseFileNames)
	{
		perror("DebugUI_AddParseFile");
		return false;
	}
	parseFileNames[parseFiles++] = strdup(path);
	return true;
}


/**
 * Debugger user interface main function.
 */
void DebugUI(debug_reason_t reason)
{
	int cmdret, alertLevel;
	char *expCmd, *psCmd = NULL;
	static const char *welcome =
		"\n----------------------------------------------------------------------"
		"\nYou have entered debug mode. Type c to continue emulation, h for help.\n";
	static bool recursing;

	if (recursing)
	{
		fprintf(stderr, "WARNING: recursive call to DebugUI (through profiler debug option?)!\n");
		recursing = false;
		return;
	}
	recursing = true;

	History_Mark(reason);

	if (bInFullScreen)
		Screen_ReturnFromFullScreen();

	/* Make sure mouse isn't grabbed regardless of where
	 * this is invoked from.  E.g. returning from fullscreen
	 * enables grab if that was enabled on windowed mode.
	 */
	SDL_SetRelativeMouseMode(false);

	DebugUI_Init();

	if (welcome)
	{
		fputs(welcome, stderr);
		welcome = NULL;
	}
	DebugCpu_InitSession();
	DebugDsp_InitSession();
	Symbols_LoadCurrentProgram();
	DebugInfo_ShowSessionInfo();

	/* override paused message so that user knows to look into console
	 * on how to continue in case he invoked the debugger by accident.
	 */
	Statusbar_AddMessage("Console Debugger", 100);
	Statusbar_Update(sdlscrn, true);

	/* disable normal GUI alerts while on console */
	alertLevel = Log_SetAlertLevel(LOG_FATAL);

	cmdret = DEBUGGER_CMDDONE;
	do
	{
		/* Read command from the keyboard and give previous
		 * command for freeing / adding to history
		 */
		psCmd = DebugUI_GetCommand(psCmd);
		if (!psCmd)
			break;

		/* returns new expression expanded string */
		if (!(expCmd = DebugUI_EvaluateExpressions(psCmd)))
			continue;

		/* Parse and execute the command string */
		cmdret = DebugUI_ParseCommand(expCmd);
		free(expCmd);
	}
	while (cmdret != DEBUGGER_END);

	/* free exit command */
	DebugUI_FreeCommand(psCmd);

	Log_SetAlertLevel(alertLevel);

	DebugCpu_SetDebugging();
	DebugDsp_SetDebugging();

	recursing = false;
}


/**
 * Read debugger commands from a file.  If 'reinit' is set (as it
 * normally should), reinitialize breakpoints etc. afterwards.
 * Processed command lines are printed if 'verbose' is set.
 * return false for error, true for success.
 */
bool DebugUI_ParseFile(const char *path, bool reinit, bool verbose)
{
	int recurse;
	static int recursing;
	char *olddir, *dir, *cmd, *expanded, *slash;
	char input[256];
	FILE *fp;

	if (verbose)
		fprintf(stderr, "Reading debugger commands from '%s'...\n", path);
	if (!(fp = fopen(path, "r")))
	{
		perror("ERROR");
		return false;
	}

	/* change to directory where the debugger file resides */
	olddir = NULL;
	dir = strdup(path);
	slash = strrchr(dir, PATHSEP);
	if (slash)
	{
		olddir = malloc(FILENAME_MAX);
		if (olddir)
		{
			if (!getcwd(olddir, FILENAME_MAX))
				strcpy(olddir, ".");
		}
		*slash = '\0';
		if (chdir(dir) != 0)
		{
			perror("ERROR");
			free(olddir);
			free(dir);
			fclose(fp);
			return false;
		}
		if (verbose)
			fprintf(stderr, "Changed to input file dir '%s'.\n", dir);
	}
	free(dir);

	recurse = recursing;
	recursing = true;

	while (fgets(input, sizeof(input), fp) != NULL)
	{
		/* ignore empty and comment lines */
		cmd = Str_Trim(input);
		if (!*cmd || *cmd == '#')
			continue;

		/* returns new string if input needed expanding! */
		expanded = DebugUI_EvaluateExpressions(input);
		if (!expanded)
			continue;

		cmd = Str_Trim(expanded);
		if (verbose)
			fprintf(stderr, "> %s\n", cmd);
		DebugUI_ParseCommand(cmd);
		free(expanded);
	}
	recursing = false;

	fclose(fp);

	if (olddir)
	{
		if (chdir(olddir) != 0)
			perror("ERROR");
		else if (verbose)
			fprintf(stderr, "Changed back to '%s' dir.\n", olddir);
		free(olddir);
	}

	if (!recurse)
	{
		/* current script (or something called by it) specified final dir */
		if (finalDir)
		{
			if (chdir(finalDir) != 0)
				perror("ERROR");
			else if(verbose)
				fprintf(stderr, "Delayed change to '%s' dir.\n", finalDir);
			free(finalDir);
			finalDir = NULL;
		}
		/* only top-level (non-recursed) call has valid re-init info,
		 * as that's the only one that can get directly called from
		 * breakpoints
		 */
		if (reinit)
		{
			DebugCpu_SetDebugging();
			DebugDsp_SetDebugging();
		}
	}
	return true;
}


/**
 * Remote/parallel debugger line usage API.
 * Return false for failed command, true for success.
 */
bool DebugUI_ParseLine(const char *input)
{
	char *expanded;
	int ret = 0;

	DebugUI_Init();

	/* returns new string if input needed expanding! */
	expanded = DebugUI_EvaluateExpressions(input);
	if (expanded)
	{
		fprintf(stderr, "> %s\n", expanded);
		ret = DebugUI_ParseCommand(expanded);
		free(expanded);

		DebugCpu_SetDebugging();
		DebugDsp_SetDebugging();
	}
	return (ret == DEBUGGER_CMDDONE);
}

/**
 * Debugger invocation based on exception
 */
void DebugUI_Exceptions(int nr, long pc)
{
	static struct {
		int flag;
		const char *name;
	} ex[] = {
		{ EXCEPT_BUS,       "Bus error" },              /* 2 */
		{ EXCEPT_ADDRESS,   "Address error" },          /* 3 */
		{ EXCEPT_ILLEGAL,   "Illegal instruction" },	/* 4 */
		{ EXCEPT_ZERODIV,   "Div by zero" },		/* 5 */
		{ EXCEPT_CHK,       "CHK" },			/* 6 */
		{ EXCEPT_TRAPV,     "TRAPCc/TRAPV" },		/* 7 */
		{ EXCEPT_PRIVILEGE, "Privilege violation" },	/* 8 */
		{ EXCEPT_TRACE,     "Trace" },			/* 9 */
		{ EXCEPT_LINEA,     "Line-A" },			/* 10 */
		{ EXCEPT_LINEF,     "Line-F" }			/* 11 */
	};
	nr -= 2;
	if (nr < 0  || nr >= ARRAY_SIZE(ex))
		return;
	if (!(ExceptionDebugMask & ex[nr].flag))
		return;
	fprintf(stderr,"%s exception at 0x%lx!\n", ex[nr].name, pc);
	DebugUI(REASON_CPU_EXCEPTION);
}
