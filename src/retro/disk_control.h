/*
  Hatari - disk_control.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_RETRO_DISK_CONTROL_H
#define HATARI_RETRO_DISK_CONTROL_H

void DiskControl_Init(void);
void DiskControl_NewGame(const char *path);
void DiskControl_UnInit(void);

#endif /* HATARI_RETRO_DISK_CONTROL_H */
