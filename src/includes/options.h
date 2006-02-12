/*
  Hatari - options.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_OPTIONS_H
#define HATARI_OPTIONS_H

/* parses all Hatari command line options and sets Hatari state accordingly,
 * returns boot disk image name (empty if not set)
 */
extern void Opt_ParseParameters(int argc, char *argv[],
				char *bootdisk, size_t bootlen);

#endif /* HATARI_OPTIONS_H */
