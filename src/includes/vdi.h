/*
  Hatari
*/

#ifndef VDI_H
#define VDI_H

#define MAX_VDI_WIDTH  1024
#define MAX_VDI_HEIGHT  768
#define MAX_VDI_PLANES  4

enum
{
  GEMRES_640x480,
  GEMRES_800x600,
  GEMRES_1024x768
};

enum
{
  GEMCOLOUR_2,
  GEMCOLOUR_4,
  GEMCOLOUR_16
};

extern int LineABase,FontBase;
extern unsigned int VDI_OldPC;
extern BOOL bUseVDIRes;
extern int VDIWidth,VDIHeight,VDIRes;
extern int VDIPlanes,VDIColours,VDICharHeight;

extern void VDI_SetResolution(int GEMRes, int GEMColour);
extern BOOL VDI(void);
extern void VDI_FixDesktopInf(void);
extern void VDI_LineA(void);
extern void VDI_Complete(void);

#endif  /* VDI_H */
