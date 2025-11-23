/*
  Hatari - event.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_EVENT_H
#define HATARI_EVENT_H

/* option arguments that can be changed on Hatari "events" */
typedef struct {
	const char *parseFile;
	const char *logLevel;
	const char *traceFlags;
	const char *exceptionMask;
	/* normal integer options */
	int slowDown;
	int frameSkips;
	int runVBLs;
	/* bool options are (ints, as they need 3 values) */
	int aviRecord;
	int fastForward;
} event_actions_t;

extern void Event_Init(void);
extern event_actions_t *Event_GetPrefixActions(const char **str);

extern void Event_DoResetActions(void);
extern void Event_DoInfLoadActions(void);
extern void Event_DoPrgExecActions(void);

#endif /* ifndef HATARI_EVENT_H */
