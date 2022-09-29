/*
  Hatari - xbios.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_XBIOS_H
#define HATARI_XBIOS_H

extern bool XBios(void);
extern void XBios_Info(FILE *fp, uint32_t dummy);
extern void XBios_EnableCommands(bool enable);

#endif
