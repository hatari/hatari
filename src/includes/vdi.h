/*
  Hatari
*/

#ifndef HATARI_VDI_H
#define HATARI_VDI_H

#define MAX_VDI_WIDTH  1024
#define MAX_VDI_HEIGHT  768
#define MAX_VDI_PLANES  4

/* these VDIRes values can overlap with ST_*_RES values in VideoShifterByte,
 * so everything using that should check for bUseVDIRes first. */
enum
{
  GEMRES_640x480,
  GEMRES_800x600,
  GEMRES_1024x768,
  GEMRES_OTHER
};

enum
{
  GEMCOLOUR_2,
  GEMCOLOUR_4,
  GEMCOLOUR_16
};

extern Uint32 VDI_OldPC;
extern BOOL bUseVDIRes;
extern int VDIWidth,VDIHeight;
extern int VDIRes,VDIPlanes;

extern void VDI_SetResolution(int GEMRes, int GEMColour);
extern BOOL VDI(void);
extern void VDI_FixDesktopInf(void);
extern void VDI_LineA(Uint32 LineABase, Uint32 FontBase);
extern void VDI_Complete(void);

#endif  /* HATARI_VDI_H */
