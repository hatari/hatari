/*
  Hatari
*/

#ifndef MAIN_H
#include "main.h"
#endif

/* Standard available ST memory configurations */
enum {
  MEMORYSIZE_512,
  MEMORYSIZE_1024,
  MEMORYSIZE_2MB,
  MEMORYSIZE_4MB
};

/* List of TOS settings for different memory size */
typedef struct {
  unsigned long PhysTop;       /* phys top */
  unsigned long MemoryConfig;  /* 512k configure 0x00=128k 0x01=512k 0x10=2Mb 11=reserved eg 0x1010 = 4Mb */
  unsigned long MemoryEnd;     /* Above this address causes a BusError */
} MEMORY_INFO;

extern unsigned short int TOSVersion;
extern unsigned long TOSAddress,TOSSize;
extern unsigned int ConnectedDriveMask;
extern BOOL bOverrideTOSImage;
extern char szTOSImageOverrideFileName[MAX_FILENAME_LENGTH];
extern char *pszTOSNameExts[];

extern void TOS_MemorySnapShot_Capture(BOOL bSave);
extern void TOS_LoadImage(void);
extern void TOS_FixRom(void);
extern void TOS_SetDefaultMemoryConfig(void);
