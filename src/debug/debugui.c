/*
  Hatari - debugui.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  debugui.c - this is the code for the mini-debugger. When the pause button is
  pressed, the emulator is (hopefully) halted and this little CLI can be used
  (in the terminal box) for debugging tasks like memory and register dumps.
*/
const char DebugUI_fileid[] = "Hatari debugui.c : " __DATE__ " " __TIME__;

#include <ctype.h>
#include <stdio.h>
#include <unistd.h>

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
#include "options.h"
#include "screen.h"
#include "statusbar.h"
#include "str.h"

#include "debug_priv.h"
#include "breakcond.h"
#include "debugcpu.h"
#include "debugdsp.h"
#include "debugInfo.h"
#include "debugui.h"
#include "evaluate.h"
#include "symbols.h"

int bExceptionDebugging;

FILE *debugOutput;

static dbgcommand_t *debugCommand;
static int debugCommands;

/* stores last value shown on screen, used for TAB-completion */
static char lastResult[10];


/**
 * Save/Restore snapshot of debugging session variables
 */
void DebugUI_MemorySnapShot_Capture(bool bSave)
{
	DebugCpu_MemorySnapShot_Capture(bSave);
	DebugDsp_MemorySnapShot_Capture(bSave);
	BreakCond_MemorySnapShot_Capture(bSave);
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
	File_Close(debugOutput);
	debugOutput = NULL;

	if (nArgc > 1)
		debugOutput = File_Open(psArgs[1], "w");

	if (debugOutput)
		fprintf(stderr, "Debug log '%s' opened.\n", psArgs[1]);
	else
		debugOutput = stderr;

	return DEBUGGER_CMDDONE;
}


/**
 * Helper to print given value in all supported number bases
 */
static void DebugUI_PrintValue(Uint32 value)
{
	bool one, ones;
	int bit;

	fputs("= %", stderr);
	ones = false;
	for (bit = 31; bit >= 0; bit--)
	{
		one = value & (1<<bit);
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
 * Commmand: Evaluate an expression with CPU reg and symbol parsing.
 */
static int DebugUI_Evaluate(int nArgc, char *psArgs[])
{
	const char *errstr, *expression = (const char *)psArgs[1];
	Uint32 result;
	int offset;

	if (nArgc < 2)
	{
		DebugUI_PrintCmdHelp(psArgs[0]);
		return DEBUGGER_CMDDONE;
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
	return ((cmd[0] == 'd' && cmd[1] && !cmd[2])
		|| strncmp(cmd, "dsp", 3) == 0);
}

/**
 * Evaluate everything include within "" and replace them with the result.
 * String given as an argument may be re-allocated if it needs to be
 * expanded. Caller needs to free the returned string if it's not NULL.
 * 
 * Return string with expressions expanded or NULL for an error.
 */
static char *DebugUI_EvaluateExpressions(char *input)
{
	int offset, count, diff, inputlen;
	char *end, *start;
	const char *errstr;
	char valuestr[12];
	Uint32 value;
	bool fordsp;

	/* input is split later on, need to save len here */
	fordsp = DebugUI_IsForDsp(input);
	inputlen = strlen(input);
	start = input;
	
	while ((start = strchr(start, '"')))
	{
		end = strchr(start+1, '"');
		if (!end)
		{
			fprintf(stderr, "ERROR: matching '\"' missing from '%s'!\n", start);
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
			*end = '"';
			fprintf(stderr, "Expression ERROR:\n'%s'\n%*c-%s\n",
				input, start-input+offset+3, '^', errstr);
			free(input);
			return NULL;
		}
		end++;
		
		count = sprintf(valuestr, "$%x", value);
		fprintf(stderr, "- \"%s\" -> %s\n", start+1, valuestr);

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

	if (strcmp(argv[0], "stateload") == 0)
		MemorySnapShot_Restore(file, true);
	else
		MemorySnapShot_Capture(file, true);

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
		DebugUI_PrintCmdHelp(argv[0]);
		return DEBUGGER_CMDDONE;
	}
	arg = argv[1];
	
	for (i = 0; i < ARRAYSIZE(bases); i++)
	{
		if (strcasecmp(bases[i].name, arg) == 0)
		{
			if (ConfigureParams.Log.nNumberBase != bases[i].base)
			{
				fprintf(stderr, "Switched default number base from %d to %d-based (%s) values.\n",
					ConfigureParams.Log.nNumberBase,
					bases[i].base, bases[i].name);
				ConfigureParams.Log.nNumberBase = bases[i].base;
			} else {
				fprintf(stderr, "Already in '%s' mode.\n", bases[i].name);
			}
			return DEBUGGER_CMDDONE;
		}
	}

	/* get configuration changes */
	current = ConfigureParams;

	/* Parse and apply options */
	if (Opt_ParseParameters(argc, (const char**)argv))
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
 * Command: Set tracing
 */
static int DebugUI_SetTracing(int argc, char *argv[])
{
	const char *errstr;
	if (argc != 2)
	{
		DebugUI_PrintCmdHelp(argv[0]);
		return DEBUGGER_CMDDONE;
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
	if (argc == 2)
	{
		if (chdir(argv[1]) == 0)
			return DEBUGGER_CMDDONE;
		perror("ERROR");
	}
	DebugUI_PrintCmdHelp(argv[0]);
	return DEBUGGER_CMDDONE;
}


/**
 * Command: Execute a system command
 */
static int DebugUI_Exec(int argc, char *argv[])
{
	if (argc == 2)
		system(argv[1]);
	else
		DebugUI_PrintCmdHelp(argv[0]);
	return DEBUGGER_CMDDONE;
}


/**
 * Command: Quit emulator
 */
static int DebugUI_QuitEmu(int nArgc, char *psArgv[])
{
	bQuitProgram = true;
	M68000_SetSpecial(SPCFLAG_BRK);   /* Assure that CPU core shuts down */
	return DEBUGGER_END;
}


/**
 * Print help text for one command
 */
void DebugUI_PrintCmdHelp(const char *psCmd)
{
	dbgcommand_t *cmd;
	int i;

	/* Search the command ... */
	for (cmd = debugCommand, i = 0; i < debugCommands; i++, cmd++)
	{
		if (!debugCommand[i].pFunction)
			continue;
		if (!strcmp(psCmd, cmd->sLongName)
		    || (cmd->sShortName && *(cmd->sShortName)
			&& !strcmp(psCmd, cmd->sShortName)))
		{
			bool bShort = cmd->sShortName && *(cmd->sShortName);
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
			return;
		}
	}

	fprintf(stderr, "Unknown command '%s'\n", psCmd);
}


/**
 * Command: Print debugger help screen.
 */
static int DebugUI_Help(int nArgc, char *psArgs[])
{
	int i;

	if (nArgc > 1)
	{
		DebugUI_PrintCmdHelp(psArgs[1]);
		return DEBUGGER_CMDDONE;
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
		"'help <command>' gives more help.\n", ConfigureParams.Log.nNumberBase);
	return DEBUGGER_CMDDONE;
}


/**
 * Parse debug command and execute it.
 */
int DebugUI_ParseCommand(char *input)
{
	char *psArgs[64];
	const char *delim;
	static char sLastCmd[80] = { '\0' };
	int nArgc, cmd = -1;
	int i, retval;

	psArgs[0] = strtok(input, " \t");

	if (psArgs[0] == NULL)
	{
		if (strlen(sLastCmd) > 0)
			psArgs[0] = sLastCmd;
		else
			return DEBUGGER_CMDDONE;
	}

	/* Search the command ... */
	for (i = 0; i < debugCommands; i++)
	{
		if (!debugCommand[i].pFunction)
			continue;
		if (!strcmp(psArgs[0], debugCommand[i].sLongName) ||
		    !strcmp(psArgs[0], debugCommand[i].sShortName))
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
		return DEBUGGER_CMDDONE;
	}

	if (debugCommand[cmd].bNoParsing)
		delim = "";
	else
		delim = " \t";

	/* Separate arguments and put the pointers into psArgs */
	for (nArgc = 1; nArgc < ARRAYSIZE(psArgs); nArgc++)
	{
		psArgs[nArgc] = strtok(NULL, delim);
		if (psArgs[nArgc] == NULL)
			break;
	}

	if (!debugOutput) {
		/* make sure also calls from control.c work */
		DebugUI_SetLogDefault();
	}

	/* ... and execute the function */
	retval = debugCommand[i].pFunction(nArgc, psArgs);
	/* Save commando string if it can be repeated */
	if (retval == DEBUGGER_CMDCONT)
		strncpy(sLastCmd, psArgs[0], sizeof(sLastCmd));
	else
		sLastCmd[0] = '\0';
	return retval;
}


#if HAVE_LIBREADLINE
/* See "info:readline" e.g. in Konqueror for readline usage. */

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
	struct {
		const char *name;
		char* (*match)(const char *, int);
	} cmd[] = {
		{ "a", Symbols_MatchCpuCodeAddress },
		{ "address", Symbols_MatchCpuCodeAddress },
		{ "b", BreakCond_MatchCpuVariable },
		{ "breakpoint", BreakCond_MatchCpuVariable },
		{ "cd", rl_filename_completion_function },
		{ "da", Symbols_MatchDspCodeAddress },
		{ "dspaddress", Symbols_MatchDspCodeAddress },
		{ "db", BreakCond_MatchDspVariable },
		{ "dspbreak", BreakCond_MatchDspVariable },
		{ "dspsymbols", rl_filename_completion_function },
		{ "e", Symbols_MatchCpuAddress },
		{ "evaluate", Symbols_MatchCpuAddress },
		{ "exec", rl_filename_completion_function },
		{ "f", rl_filename_completion_function },
		{ "h", DebugUI_MatchCommand },
		{ "help", DebugUI_MatchCommand },
		{ "i", DebugInfo_MatchInfo },
		{ "info", DebugInfo_MatchInfo },
		{ "lock", DebugInfo_MatchLock },
		{ "logfile", rl_filename_completion_function },
		{ "l", rl_filename_completion_function },
		{ "loadbin", rl_filename_completion_function },
		{ "s", rl_filename_completion_function },
		{ "savebin", rl_filename_completion_function },
		{ "stateload", rl_filename_completion_function },
		{ "statesave", rl_filename_completion_function },
		{ "symbols", rl_filename_completion_function },
		{ "o", Opt_MatchOption },
		{ "setopt", Opt_MatchOption },
		{ "t", Log_MatchTrace },
		{ "trace", Log_MatchTrace }
	};
	int i, quotes, end, start = 0;
	char *str, buf[32];
	size_t len;

	/* check where's the first word (ignore white space) */
	while (start < rl_point && isspace(rl_line_buffer[start]))
		start++;
	end = start;
	while (end < rl_point && !isspace(rl_line_buffer[end]))
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
	for (i = 0; i < ARRAYSIZE(cmd); i++)
	{
		if (strcmp(buf, cmd[i].name) == 0)
			return rl_completion_matches(text, cmd[i].match);
	}
	rl_attempted_completion_over = true;
	return NULL;
}

/**
 * Read a command line from the keyboard and return a pointer to the string.
 * @return	Pointer to the string which should be deallocated free()
 *              after use. Returns NULL when error occured.
 */
static char *DebugUI_GetCommand(void)
{
	char *input;

	/* Allow conditional parsing of the ~/.inputrc file. */
	rl_readline_name = "Hatari";
	
	/* Tell the completer that we want a crack first. */
	rl_attempted_completion_function = DebugUI_Completion;

	input = readline("> ");
	if (!input)
		return NULL;

	input = Str_Trim(input);
	if (input[0])
		add_history(input);

	return input;
}

#else /* !HAVE_LIBREADLINE */

/**
 * Read a command line from the keyboard and return a pointer to the string.
 * @return	Pointer to the string which should be deallocated free()
 *              after use. Returns NULL when error occured.
 */
static char *DebugUI_GetCommand(void)
{
	char *input;
	fprintf(stderr, "> ");
	input = malloc(256);
	if (!input)
		return NULL;
	input[0] = '\0';
	if (fgets(input, 256, stdin) == NULL)
	{
		free(input);
		return NULL;
	}
	return Str_Trim(input);
}

#endif /* !HAVE_LIBREADLINE */


static const dbgcommand_t uicommand[] =
{
	{ NULL, "Generic commands", NULL, NULL, NULL, false },
	{ DebugUI_ChangeDir, "cd", "",
	  "change directory",
	  "<directory>\n"
	  "\tChange Hatari work directory.",
	  false },
	{ DebugUI_Evaluate, "evaluate", "e",
	  "evaluate an expression",
	  "<expression>\n"
	  "\tEvaluate an expression and show the result.  Expression can\n"
	  "\tinclude CPU register and symbol names, those are replaced\n"
	  "\tby their values. Supported operators in expressions are,\n"
	  "\tin the decending order of precedence:\n"
	  "\t\t(), +, -, ~, *, /, +, -, >>, <<, ^, &, |\n"
	  "\tFor example:\n"
	  "\t\t((0x21 * 0x200) + (-5)) ^ (~%111 & $f0f0f0)\n"
	  "\tResult value is shown as binary, decimal and hexadecimal.\n"
	  "\tAfter this, '$' will TAB-complete to last result value.",
	  true },
	{ DebugUI_Exec, "exec", "",
	  "execute a shell command",
	  "<command line>\n"
	  "\tRun the given command with the system shell.",
	  true },
	{ DebugUI_Help, "help", "h",
	  "print help",
	  "[command]\n"
	  "\tPrint help text for available commands.",
	  false },
	{ DebugInfo_Command, "info", "i",
	  "show machine/OS information",
	  "[subject [arg]]\n"
	  "\tPrint information on requested subject or list them if\n"
	  "\tno subject given.",
	  false },
	{ DebugInfo_Command, "lock", "",
	  "lock info to show on entering debugger",
	  "[subject [args]]\n"
	  "\tLock requested information to be shown every time debugger\n"
	  "\tis entered or list available options if no subject's given.",
	  false },
	{ DebugUI_SetLogFile, "logfile", "f",
	  "open or close log file",
	  "[filename]\n"
	  "\tOpen log file, no argument closes the log file. Output of\n"
	  "\tregister & memory dumps and disassembly will be written to it.",
	  false },
	{ DebugUI_SetOptions, "setopt", "o",
	  "set Hatari command line and debugger options",
	  "[<bin|dec|hex>|<command line options>]\n"
	  "\tSet Hatari options. For example to enable exception catching,\n"
	  "\tuse following command line option: 'setopt --debug'. Special\n"
	  "\t'bin', 'dec' and 'hex' arguments change the default number base\n"
	  "\tused in debugger.",
	  false },
	{ DebugUI_DoMemorySnap, "stateload", "",
	  "restore emulation state",
	  "[filename]\n"
	  "\tRestore emulation snapshot from default or given file",
	  false },
	{ DebugUI_DoMemorySnap, "statesave", "",
	  "save emulation state",
	  "[filename]\n"
	  "\tSave emulation snapshot to default or given file",
	  false },
	{ DebugUI_SetTracing, "trace", "t",
	  "select Hatari tracing settings",
	  "[set1,set2...]\n"
	  "\tSelect Hatari tracing settings. For example to enable CPU\n"
	  "\tdisassembly and VBL tracing, use:  trace cpu_disasm,video_hbl",
	  false },
	{ DebugUI_QuitEmu, "quit", "q",
	  "quit emulator",
	  "\n"
	  "\tLeave debugger and quit emulator.",
	  false }
};


/**
 * Debugger user interface initialization.
 */
static void DebugUI_Init(void)
{
	const dbgcommand_t *cpucmd, *dspcmd;
	int cpucmds, dspcmds;

	/* already intialized? */
	if (debugCommands)
		return;

	/* if you want disassembly or memdumping to start/continue from
	 * specific address, you can set them in these functions.
	 */
	dspcmds = DebugDsp_Init(&dspcmd);
	cpucmds = DebugCpu_Init(&cpucmd);

	/* on first time copy the command structures to a single table */
	debugCommands = ARRAYSIZE(uicommand);
	debugCommand = malloc(sizeof(dbgcommand_t) * (dspcmds + cpucmds + debugCommands));
	assert(debugCommand);
	
	memcpy(debugCommand, uicommand, sizeof(dbgcommand_t) * debugCommands);
	memcpy(&debugCommand[debugCommands], cpucmd, sizeof(dbgcommand_t) * cpucmds);
	debugCommands += cpucmds;
	memcpy(&debugCommand[debugCommands], dspcmd, sizeof(dbgcommand_t) * dspcmds);
	debugCommands += dspcmds;
}


/**
 * Debugger user interface main function.
 */
void DebugUI(void)
{
	int cmdret, alertLevel;
	char *psCmd;
	static const char *welcome =
		"\n----------------------------------------------------------------------"
		"\nYou have entered debug mode. Type c to continue emulation, h for help.\n";
	
	if (bInFullScreen)
		Screen_ReturnFromFullScreen();

	DebugUI_Init();

	if (welcome)
	{
		fputs(welcome, stderr);
		welcome = NULL;
	}
	DebugCpu_InitSession();
	DebugDsp_InitSession();
	DebugInfo_ShowSessionInfo();

	/* override paused message so that user knows to look into console
	 * on how to continue in case he invoked the debugger by accident.
	 */
	Statusbar_AddMessage("Console Debugger", 100);
	Statusbar_Update(sdlscrn);

	/* disable normal GUI alerts while on console */
	alertLevel = Log_SetAlertLevel(LOG_FATAL);

	cmdret = DEBUGGER_CMDDONE;
	do
	{
		/* Read command from the keyboard */
		psCmd = DebugUI_GetCommand();
		if (!psCmd)
			break;

		/* expand all quoted expressions */
		if (!(psCmd = DebugUI_EvaluateExpressions(psCmd)))
			continue;

		/* Parse and execute the command string */
		cmdret = DebugUI_ParseCommand(psCmd);
		free(psCmd);
	}
	while (cmdret != DEBUGGER_END);

	Log_SetAlertLevel(alertLevel);
	DebugUI_SetLogDefault();

	DebugCpu_SetDebugging();
	DebugDsp_SetDebugging();
}


/**
 * Remote/parallel debugger usage API
 */
int DebugUI_RemoteParse(char *input)
{
	int ret;

	DebugUI_Init();
	
	ret = DebugUI_ParseCommand(input);

	DebugCpu_SetDebugging();
	DebugDsp_SetDebugging();

	return ret;
}
