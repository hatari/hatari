/*
  Hatari - harddisk.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_RETRO_HARDDISK_H
#define HATARI_RETRO_HARDDISK_H

void HardDisk_InsertIde(const char *path);
void HardDisk_InsertAcsi(const char *path);
void HardDisk_SetGemdosDrive(const char *path);

#endif /* HATARI_RETRO_HARDDISK_H */
