/*
  Hatari - createBlankImage.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

extern int CreateBlankImage_GetDiscImageCapacity(int nTracks, int nSectors, int nSides);
extern void CreateBlankImage_CreateFile(char *pszFileName, int nTracks, int nSectors, int nSides);
