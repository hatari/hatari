/*
  Hatari - vdi.h

  This file is distributed under the GNU Public License, version 2. or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_VDI_H
#define HATARI_VDI_H

#define MAX_VDI_PLANES  4
/* TOS needs width to be aligned to 128/planes and height to VDI text
 * height (16 in 1-plane mode, 8 otherwise).   Use TT hi-res as max
 * (max size affects memory usage).
 */
#define MAX_VDI_WIDTH  1280
#define MAX_VDI_HEIGHT  960
/* next in-all-bitdepths aligned size up from smallest ST res. */
#define MIN_VDI_WIDTH   384
#define MIN_VDI_HEIGHT  208


enum
{
  GEMCOLOR_2,
  GEMCOLOR_4,
  GEMCOLOR_16
};

extern Uint32 VDI_OldPC;
extern bool bUseVDIRes;
extern int VDIWidth,VDIHeight;
extern int VDIRes,VDIPlanes;

extern int VDI_Limit(int value, int align, int min, int max);
extern void VDI_SetResolution(int GEMColor, int WidthRequest, int HeightRequest);
extern bool VDI(void);
extern void VDI_FixDesktopInf(void);
extern void VDI_LineA(Uint32 LineABase, Uint32 FontBase);
extern void VDI_Complete(void);

#endif  /* HATARI_VDI_H */
