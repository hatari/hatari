/*
  Hatari tool: Magic Shadow Archiver - floppy.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/


#define NUMBYTESPERSECTOR    512         /* All disks are 512 bytes per sector */


void Floppy_FindDiskDetails(const Uint8 *pBuffer, int nImageBytes,
                            unsigned short *pnSectorsPerTrack, unsigned short *pnSides);

