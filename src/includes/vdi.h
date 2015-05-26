/*
  Hatari - vdi.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_VDI_H
#define HATARI_VDI_H

#define MAX_VDI_PLANES  4
/* TOS needs screen width to be aligned to 128/planes and height to VDI text
 * height (16, larger of these, is used in calculating the limits in Hatari).
 * Max. screen size supported by EmuTOS (and TOS v3.x?) is 300kB.
 *
 * Below MAX_VDI_* values are reasonable limits for monochrome resolutions.
 * 300kB limits screen size to max. resolution of 2048x1200 or 1920x1280,
 * which allows FHD (1920×1080), WUXGA (1920x1200) and QWXGA (2048x1152)
 * standard resolutions.
 * 
 * On 4-color mode, 300kB limits screen size to 1280x960 or 1200x1024
 * which allows HD (1280x720), WXGA (1280x768) and XGA+ (1152x864)
 * standard resolutions.
 * 
 * On 16-color mode, 300kB limits screen size to 1024x576 or 800x768,
 * which fills nicely qHD (960x540), DVGA (960x640) and WSVGA (1024x600)
 * standard resolution displays.
 */
#define MAX_VDI_BYTES 300*1024
#define MAX_VDI_WIDTH  2048
#define MAX_VDI_HEIGHT 1280
/* smaller doesn't make sense even for testing */
#define MIN_VDI_WIDTH   320
#define MIN_VDI_HEIGHT  208


enum
{
  GEMCOLOR_2,
  GEMCOLOR_4,
  GEMCOLOR_16
};

extern Uint32 VDI_OldPC;
extern bool bUseVDIRes, bVdiAesIntercept;
extern int VDIWidth,VDIHeight;
extern int VDIRes,VDIPlanes;

extern void VDI_SetResolution(int GEMColor, int WidthRequest, int HeightRequest);
extern void AES_Info(FILE *fp, Uint32 bShowOpcodes);
extern void VDI_Info(FILE *fp, Uint32 bShowOpcodes);
extern bool VDI_AES_Entry(void);
extern void VDI_LineA(Uint32 LineABase, Uint32 FontBase);
extern void VDI_Complete(void);
extern void VDI_Reset(void);

#endif  /* HATARI_VDI_H */
