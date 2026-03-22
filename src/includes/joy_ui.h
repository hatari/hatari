/*
  Hatari - joy_ui.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_JOYUI_H
#define HATARI_JOYUI_H

#define JOYREADING_BUTTON1  1		/* bit 0, regular fire button */
#define JOYREADING_BUTTON2  2		/* bit 1, space / jump button */
#define JOYREADING_BUTTON3  4		/* bit 2, autofire button */

typedef struct
{
	int XPos, YPos;       /* the actually read axis values in range of -32768...0...32767 */
	int Buttons;          /* JOYREADING_BUTTON1 */
} JOYREADING;

void JoyUI_Init(void);
void JoyUI_UnInit(void);
const char *JoyUI_GetName(int id);
int JoyUI_GetMaxId(void);
int JoyUI_NumJoysticks(void);
bool JoyUI_ValidateJoyId(int i);
void JoyUI_SetDefaultKeys(int stjoy_id);
bool JoyUI_ReadJoystick(int nStJoyId, JOYREADING *pJoyReading);
int JoyUI_GetRealFireButtons(int nStJoyId);

#endif
