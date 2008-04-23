/*
  Hatari - options.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_OPTIONS_H
#define HATARI_OPTIONS_H

extern BOOL bLoadAutoSave;
extern BOOL bLoadMemorySave;
extern BOOL bBiosIntercept;

/* Parses all Hatari command line options, sets Hatari state and
 * boot disk image name (empty if not set) accordingly.
 * Returns TRUE if everything was OK, FALSE otherwise.
 */
extern BOOL Opt_ParseParameters(int argc, char *argv[],
				char *bootdisk, size_t bootlen);

#endif /* HATARI_OPTIONS_H */
