/*
  Hatari - options.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_OPTIONS_H
#define HATARI_OPTIONS_H

extern bool bLoadAutoSave;
extern bool bLoadMemorySave;
extern bool AviRecordOnStartup;
extern bool BenchmarkMode;

extern bool Opt_IsAtariProgram(const char *path);
extern bool Opt_ShowError(unsigned int optid, const char *value, const char *error);
extern int Opt_ValueAlignMinMax(int value, int align, int min, int max);
extern bool Opt_ParseParameters(int argc, const char * const argv[]);
extern char *Opt_MatchOption(const char *text, int state);

#endif /* HATARI_OPTIONS_H */
