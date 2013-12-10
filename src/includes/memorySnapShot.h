/*
  Hatari - memorySnapShot.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/


extern void MemorySnapShot_Skip(int Nb);
extern void MemorySnapShot_Store(void *pData, int Size);
extern void MemorySnapShot_Capture(const char *pszFileName, bool bConfirm);
extern void MemorySnapShot_Restore(const char *pszFileName, bool bConfirm);
