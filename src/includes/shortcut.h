/*
  Hatari - shortcut.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/


typedef struct
{
  unsigned short Key;
  BOOL bShiftPressed;
  BOOL bCtrlPressed;
} SHORTCUT_KEY;

extern SHORTCUT_KEY ShortCutKey;

extern void ShortCut_CheckKeys(void);
