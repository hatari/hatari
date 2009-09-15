/*
  Hatari - options.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_OPTIONS_H
#define HATARI_OPTIONS_H

extern bool bLoadAutoSave;
extern bool bLoadMemorySave;
extern bool bBiosIntercept;

/* Parses all Hatari command line options and sets Hatari state.
 * Returns TRUE if everything was OK, FALSE otherwise.
 */
extern bool Opt_ParseParameters(int argc, const char *argv[]);

/* readline callback function for the debugger setopt command */
extern char *Opt_MatchOption(const char *text, int state);

#endif /* HATARI_OPTIONS_H */
