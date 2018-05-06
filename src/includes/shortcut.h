/*
  Hatari - shortcut.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

/* If pressed short-cut key modifier or standalone short-cut key,
 * retain keypress until safe to execute (start of VBL). Returns
 * true if key was shortcut, false otherwise */
extern bool ShortCut_CheckKeys(int modkey, int symkey, bool press);
/* Invoke shortcut identified by name */
extern bool Shortcut_Invoke(const char *shortcut);
/* Act on the stored keypress (in VBL) */
extern void ShortCut_ActKey(void);
