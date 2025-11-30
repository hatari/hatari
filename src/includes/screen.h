/*
  Hatari - screen.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_SCREEN_H
#define HATARI_SCREEN_H

extern bool bGrabMouse;
extern bool bInFullScreen;

uint32_t Screen_MapRGB(uint8_t red, uint8_t green, uint8_t blue);
void Screen_Init(void);
void Screen_UnInit(void);
bool Screen_Lock(void);
void Screen_UnLock(void);
void Screen_ClearScreen(void);
void Screen_EnterFullScreen(void);
void Screen_ReturnFromFullScreen(void);
bool Screen_UngrabMouse(void);
void Screen_GrabMouseIfNecessary(void);
void Screen_ModeChanged(bool bForceChange);
bool Screen_Draw(bool bForceFlip);
void Screen_GetPixelFormat(uint32_t *rmask, uint32_t *gmask, uint32_t *bmask,
                           int *rshift, int *gshift, int *bshift);
void Screen_GetDimension(uint32_t **pixels, int *width, int *height, int *pitch);
void Screen_GetDesktopSize(int *width, int *height);
void Screen_SetTextureScale(int width, int height, int win_width,
                            int win_height, bool bForceCreation);
bool Screen_SetVideoSize(int width, int height, bool bForceChange);
void Screen_GenConvUpdate(bool update_statusbar);
uint32_t Screen_GetGenConvWidth(void);
uint32_t Screen_GetGenConvHeight(void);
int Screen_SaveBMP(const char *filename);
void Screen_StatusbarMessage(const char *msg, uint32_t msecs);
int Screen_GetUISocket(void);
void Screen_SetTitle(const char *title);
void Screen_MinimizeWindow(void);
uint32_t Screen_GetMouseState(int *mx, int *my);

#endif  /* ifndef HATARI_SCREEN_H */
