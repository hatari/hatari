/*
  Hatari
*/

#define MAX_VDI_WIDTH  1024
#define MAX_VDI_HEIGHT  768
#define MAX_VDI_PLANES  4

extern BOOL bHoldScreenDisplay;
extern int LineABase,FontBase;
extern unsigned int VDI_OldPC;
extern BOOL bUseVDIRes;
extern int VDIWidth,VDIHeight,VDIRes;
extern int VDIPlanes,VDIColours,VDICharHeight;

extern void VDI_SetResolution(int GEMRes,int GEMColour);
extern BOOL VDI(void);
extern void VDI_OpCode(void);
extern void VDI_FixDesktopInf(void);
extern void VDI_LineA(void);
