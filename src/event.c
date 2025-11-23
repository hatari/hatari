/*
  Hatari - event.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Functions called to do user configured actions at specific points ("events")
  of Hatari emulation.  Currently these actions are emulator (not emulation)
  setting changes.
*/
const char Event_fileid[] = "Hatari event.c";

#include "main.h"
#include "avi_record.h"
#include "configuration.h"
#include "debugui.h"
#include "event.h"
#include "log.h"
#include "timing.h"

static event_actions_t resetActions;
static event_actions_t infLoadActions;
static event_actions_t prgExecActions;

/* Invalid value regardless of option */
#define EVENT_ACT_UNSET -1


/**
 * Init event actions struct members to unset state
 */
void Event_Init(void)
{
	static const event_actions_t unset = {
		/* string options */
		.parseFile     = NULL,
		.logLevel      = NULL,
		.traceFlags    = NULL,
		.exceptionMask = NULL,
		/* integer options */
		.slowDown   = EVENT_ACT_UNSET,
		.frameSkips = EVENT_ACT_UNSET,
		.runVBLs    = EVENT_ACT_UNSET,
		/* bool options */
		.aviRecord   = EVENT_ACT_UNSET,
		.fastForward = EVENT_ACT_UNSET,
	};

	resetActions = unset;
	infLoadActions = unset;
	prgExecActions = unset;
}

/**
 * Check given option argument for event prefix and set
 * given string pointer to a point after the prefix.
 *
 * Return pointer to specified actions struct, or NULL for no match.
 */
event_actions_t *Event_GetPrefixActions(const char **str)
{
	struct {
		const char *prefix;
		event_actions_t *actions;
	} act[] = {
		{ "boot:", &resetActions },
		{ "inf:", &infLoadActions },
		{ "prg:", &prgExecActions },
	};
	for (int i = 0; i < ARRAY_SIZE(act); i++) {
		const char *prefix = act[i].prefix;
		size_t len = strlen(prefix);

		if (strncmp(*str, prefix, len) == 0) {
			*str += len;
			return act[i].actions;
		}
	}
	return NULL;
}

/* ----------------------------------------------------------- */

/**
 * Perform actions specified in the references actions structure
 */
static void Event_PerformActions(event_actions_t *act)
{
	/* change AVI recording? */
	if (act->aviRecord != EVENT_ACT_UNSET) {
		if (act->aviRecord != Avi_AreWeRecording()) {
			Avi_ToggleRecording();
		}
		Log_Printf(LOG_WARN, "AVI recording: %s\n", act->aviRecord ? "true" : "false");
	}

	/* change fast forwarding? */
	if (act->fastForward != EVENT_ACT_UNSET) {
		ConfigureParams.System.bFastForward = act->fastForward;
		Log_Printf(LOG_WARN, "Fast forward: %s\n", act->fastForward ? "true" : "false");
	}

	/* set frame skip? */
	if (act->frameSkips != EVENT_ACT_UNSET) {
		ConfigureParams.Screen.nFrameSkips = act->frameSkips;
		Log_Printf(LOG_WARN, "Frame skips: %d\n", act->frameSkips);
	}

	/* set slowdown? */
	if (act->slowDown != EVENT_ACT_UNSET) {
		Timing_SetVBLSlowdown(act->slowDown);
		Log_Printf(LOG_WARN, "Slow down: %dx\n", act->slowDown);
	}

	/* set runVBLs? */
	if (act->runVBLs != EVENT_ACT_UNSET) {
		Timing_SetRunVBLs(act->runVBLs);
		Log_Printf(LOG_WARN, "Exit after %d VBLs.\n", act->runVBLs);
	}

	/* parse debugger commands? */
	if (act->parseFile) {
		Log_Printf(LOG_WARN, "Debugger file: '%s'\n", act->parseFile);
		DebugUI_AddParseFile(act->parseFile);
	}

	/* change log level? */
	if (act->logLevel) {
		ConfigureParams.Log.nTextLogLevel = Log_ParseOptions(act->logLevel);
		Log_Printf(LOG_WARN, "Log level: '%s'\n", act->logLevel);
		Log_SetLevels();
	}

	/* set tracing? */
	if (act->traceFlags) {
		Log_SetTraceOptions(act->traceFlags);
		Log_Printf(LOG_WARN, "Trace flags: '%s'\n", act->traceFlags);
	}

	/* set exception debug mask? */
	if (act->exceptionMask) {
		Log_SetExceptionDebugMask(act->exceptionMask);
		Log_Printf(LOG_WARN, "Exception flags: '%s'\n", act->exceptionMask);
	}
}

/**
 * Perform actions related to emulation boot/reset event
 */
void Event_DoResetActions(void)
{
	Event_PerformActions(&resetActions);
}

/**
 * Perform actions related to Atari program (GEMDOS HD) Pexec() event
 */
void Event_DoPrgExecActions(void)
{
	Event_PerformActions(&prgExecActions);
}

/**
 * Perform actions related to virtual TOS INF loading event
 */
void Event_DoInfLoadActions(void)
{
	Event_PerformActions(&infLoadActions);

	/* legacy/backwards compatible action */
	if (ConfigureParams.Debugger.nExceptionDebugMask & EXCEPT_AUTOSTART) {
		int mask = ConfigureParams.Debugger.nExceptionDebugMask & ~EXCEPT_AUTOSTART;
		if (ExceptionDebugMask != mask) {
			ExceptionDebugMask = mask;
			Log_Printf(LOG_WARN, "Exception flags: 0x%x)\n", mask);
		}
	}
}
