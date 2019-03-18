/*
 Hatari - lilo.h

 This file is distributed under the GNU General Public License, version 2
 or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef LILO_H
#define LILO_H

extern bool bUseLilo;

/**
 * return true if Linux loading succeeds
 */
extern bool lilo_init(void);

#endif /* LILO_H */
