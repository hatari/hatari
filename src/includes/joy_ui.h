/*
  Hatari - joy_ui.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_JOYUI_H
#define HATARI_JOYUI_H

typedef struct
{
	int XPos, YPos;       /* the actually read axis values in range of -32768...0...32767 */
	int Buttons;          /* JOYREADING_BUTTON1 */
} JOYREADING;

void JoyUI_Init(void);
void JoyUI_UnInit(void);
const char *JoyUI_GetName(int id);
int JoyUI_GetMaxId(void);
bool JoyUI_ValidateJoyId(int i);
bool JoyUI_ReadJoystick(int nStJoyId, JOYREADING *pJoyReading);
int JoyUI_GetRealFireButtons(int nStJoyId);

#endif
