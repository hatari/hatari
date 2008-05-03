/*
  Hatari - vdi.h

  This file is distributed under the GNU Public License, version 2. or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_VDI_H
#define HATARI_VDI_H

#define MAX_VDI_WIDTH  1024
#define MIN_VDI_WIDTH   384
#define MAX_VDI_HEIGHT  768
#define MIN_VDI_HEIGHT  240
#define MAX_VDI_PLANES  4


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
